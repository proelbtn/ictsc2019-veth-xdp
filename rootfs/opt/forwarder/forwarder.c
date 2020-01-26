#define KBUILD_MODNAME "dummy"

#include <linux/types.h>
#include <uapi/linux/bpf.h>
#include <uapi/linux/if_ether.h>
#include <uapi/linux/ip.h>
#include <uapi/linux/in.h>
#include <uapi/linux/tcp.h>


#define assert_len(target, end) if ((void *)(target + 1) > end) return XDP_DROP;


enum direction_t {
	DIR_INBOUND,
	DIR_OUTBOUND,
};

struct endpoint_t {
	__u32 addr;
	__u16 port;
} __attribute__((__packed__));

BPF_HASH(dnat_entries, struct endpoint_t, struct endpoint_t);
BPF_HASH(snat_entries, struct endpoint_t, struct endpoint_t);
BPF_DEVMAP(devmap, 256);


static inline __u16 fold_checksum(__u64 check)
{
#pragma unroll
	for (int i = 0; i < 4; i++) check = (check & 0xffff) + (check >> 16);
	return (__u16)check;
}


static inline int forward_packet(
		struct xdp_md *ctx,
		struct ethhdr *eth, 
		struct iphdr *ip) 
{
	bpf_trace_printk("        forward_packet:\n");
	struct bpf_fib_lookup params = {};

	params.family = AF_INET;
	params.ifindex = ctx->ingress_ifindex;
	params.ipv4_src = ip->saddr;
	params.ipv4_dst = ip->daddr;

	int retval = bpf_fib_lookup(ctx, &params, sizeof(params), 0);
	bpf_trace_printk("          bpf_fib_lookup: %d\n", retval);
	switch (retval) {
		case BPF_FIB_LKUP_RET_NOT_FWDED:
		case BPF_FIB_LKUP_RET_NO_NEIGH:
			return XDP_PASS;
		case BPF_FIB_LKUP_RET_FWD_DISABLED:
		case BPF_FIB_LKUP_RET_BLACKHOLE:
		case BPF_FIB_LKUP_RET_UNREACHABLE:
		case BPF_FIB_LKUP_RET_PROHIBIT:
		case BPF_FIB_LKUP_RET_FRAG_NEEDED:
		case BPF_FIB_LKUP_RET_UNSUPP_LWT:
			return XDP_DROP;
	}

	bpf_trace_printk("          params.ifindex: %d\n", params.ifindex);

	for (int i = 0; i < 6; i++) {
		eth->h_source[i] = params.smac[i];
		eth->h_dest[i] = params.dmac[i];
	}

	retval = devmap.redirect_map(params.ifindex, 0);
	bpf_trace_printk("          devmap.redirect_map: %d\n", retval);
	return retval;
}


static inline int rewrite_packet(
		struct xdp_md *ctx,
		struct ethhdr *eth, 
		struct iphdr *ip, 
		struct tcphdr *tcp, 
		enum direction_t dir) 
{
	struct endpoint_t key, *val;
	__be64 ip_check, tcp_check, diff;

	bpf_trace_printk("    rewrite_packet:\n");

	bpf_trace_printk("      ip->saddr: 0x%x\n", ntohl(ip->saddr));
	bpf_trace_printk("      ip->daddr: 0x%x\n", ntohl(ip->daddr));
	bpf_trace_printk("      tcp->source: %d\n", ntohs(tcp->source));
	bpf_trace_printk("      tcp->dest: %d\n", ntohs(tcp->dest));

	switch (dir) {
		case DIR_INBOUND:
			key.addr = ntohl(ip->daddr);
			key.port = ntohs(tcp->dest);

			val = dnat_entries.lookup(&key);
			if (val == NULL) {
				bpf_trace_printk("        => XDP_PASS (no such entry in dnat_entries)\n");
				return XDP_PASS;
			}

			ip->daddr = htonl(val->addr);
			tcp->dest = htons(val->port);

			break;
		case DIR_OUTBOUND:
			key.addr = ntohl(ip->saddr);
			key.port = ntohs(tcp->source);

			val = snat_entries.lookup(&key);
			if (val == NULL) {
				bpf_trace_printk("        => XDP_PASS (no such entry in snat_entries)\n");
				return XDP_PASS;
			}

			ip->saddr = htonl(val->addr);
			tcp->source = htons(val->port);

			break;
		default:
			return XDP_ABORTED;
	}

	bpf_trace_printk("      ip->check: 0x%x\n", ip->check);
	bpf_trace_printk("      tcp->check: 0x%x\n", tcp->check);

	diff = ~htonl(key.addr) & 0xffffffff;
	diff += ~htons(key.port) & 0xffff;
	diff += htonl(val->addr);
	diff += htons(val->port);

	ip_check = (~ip->check & 0xffff) + diff;
	ip->check = ~fold_checksum(ip_check) & 0xffff;

	tcp_check = (~tcp->check & 0xffff) + diff;
	tcp->check = ~fold_checksum(tcp_check) & 0xffff;

	return forward_packet(ctx, eth, ip);
}

static inline int handle_packet(
		struct xdp_md *ctx, 
		enum direction_t dir) 
{
	bpf_trace_printk("  handle_packet:\n");

	void *data = (void *)(long)ctx->data;
	void *data_end = (void *)(long)ctx->data_end;

	struct ethhdr *eth = data;
	assert_len(eth, data_end);

	bpf_trace_printk("    eth->h_proto: 0x%x\n", ntohs(eth->h_proto));
	if (eth->h_proto != htons(ETH_P_IP)) {
		bpf_trace_printk("      => XDP_PASS (unsupported Ethernet type)\n");
		return XDP_PASS;
	}

	struct iphdr *ip = (struct iphdr *)(eth + 1);
	assert_len(ip, data_end);

	bpf_trace_printk("    ip->protocol: 0x%x\n", ip->protocol);
	if (ip->protocol != IPPROTO_TCP) {
		bpf_trace_printk("      => XDP_PASS (unsupported IP protocol)\n");
		return XDP_PASS;
	}

	bpf_trace_printk("    ip->version: 0x%x\n", ip->version);
	if (ip->version != 4) {
		bpf_trace_printk("      => XDP_DROP (unknown IP version)\n");
		return XDP_DROP;
	}

	bpf_trace_printk("    ip->ihl : 0x%x\n", ip->ihl);
	if (ip->ihl != 5) {
		bpf_trace_printk("      => XDP_PASS (unsupported IP option headers)\n");
		return XDP_PASS;
	}

	struct tcphdr *tcp = (struct tcphdr *)(ip + 1);
	assert_len(tcp, data_end);

	return rewrite_packet(ctx, eth, ip, tcp, dir);
}

int entry_external(struct xdp_md *ctx) {
	bpf_trace_printk("entry_external:\n");
	return handle_packet(ctx, DIR_INBOUND);
}

int entry_internal(struct xdp_md *ctx) {
	bpf_trace_printk("entry_internal:\n");
	return handle_packet(ctx, DIR_OUTBOUND);
}
