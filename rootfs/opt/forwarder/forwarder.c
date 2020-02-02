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

static inline __u16 fold_csum(__u64 check)
{
	for (int i = 0; i < 4; i++) check = (check & 0xffff) + (check >> 16);
	return (__u16)check;
}

static inline int rewrite_packet(
		struct xdp_md *ctx, 
		struct ethhdr *eth,
		struct iphdr *ip,
		struct tcphdr *tcp,
		struct endpoint_t *val,
		struct bpf_fib_lookup *params,
		enum direction_t dir)
{
	__be64 ip_check, tcp_check, l3_diff, l4_diff;

	switch (dir) {
		case DIR_INBOUND:
			l3_diff = (~ip->daddr) & 0xffffffff;
			l4_diff = (~tcp->dest) & 0xffff;
			ip->daddr = htonl(val->addr);
			tcp->dest = htons(val->port);
			break;
		case DIR_OUTBOUND:
			l3_diff = (~ip->saddr) & 0xffffffff;
			l4_diff = (~tcp->source) & 0xffff;
			ip->saddr = htonl(val->addr);
			tcp->source = htons(val->port);
			break;
		default:
			return XDP_DROP;
	}

	l3_diff += htonl(val->addr);
	l4_diff += htons(val->port);

	l4_diff += l3_diff;

	ip_check = (~ip->check & 0xffff) + l3_diff;
	ip->check = ~fold_csum(ip_check) & 0xffff;

	tcp_check = (~tcp->check & 0xffff) + l4_diff;
	tcp->check = ~fold_csum(tcp_check) & 0xffff;

	for (int i = 0; i < 6; i++) {
		eth->h_source[i] = params->smac[i];
		eth->h_dest[i] = params->dmac[i];
	}

	int ret = devmap.redirect_map(params->ifindex, 0);
	bpf_trace_printk("%d %d\n", params->ifindex, ret);
	return ret;
}

static inline int lookup_nexthop(
		struct xdp_md *ctx, 
		struct ethhdr *eth,
		struct iphdr *ip,
		struct tcphdr *tcp,
		struct endpoint_t *val,
		enum direction_t dir)
{
	struct bpf_fib_lookup params = {};
	params.family = AF_INET;
	params.ifindex = ctx->ingress_ifindex;

	switch (dir) {
		case DIR_INBOUND:
			params.ipv4_src = ip->saddr;
			params.ipv4_dst = htonl(val->addr);
			break;
		case DIR_OUTBOUND:
			params.ipv4_src = htonl(val->addr);
			params.ipv4_dst = ip->daddr;
			break;
		default:
			return XDP_DROP;
	}

	int ret = bpf_fib_lookup(ctx, &params, sizeof(params), 0);
	switch (ret) {
		case BPF_FIB_LKUP_RET_NOT_FWDED:
			return XDP_PASS;
		case BPF_FIB_LKUP_RET_FWD_DISABLED:
		case BPF_FIB_LKUP_RET_BLACKHOLE:
		case BPF_FIB_LKUP_RET_UNREACHABLE:
		case BPF_FIB_LKUP_RET_PROHIBIT:
		case BPF_FIB_LKUP_RET_FRAG_NEEDED:
		case BPF_FIB_LKUP_RET_UNSUPP_LWT:
			return XDP_DROP;
	}

	return rewrite_packet(ctx, eth, ip, tcp, val, &params, dir);
}

static inline int lookup_endpoint(
		struct xdp_md *ctx, 
		struct ethhdr *eth,
		struct iphdr *ip,
		struct tcphdr *tcp,
		enum direction_t dir)
{
	struct endpoint_t key = {}, *val = NULL;

	switch (dir) {
		case DIR_INBOUND:
			key.addr = ntohl(ip->daddr);
			key.port = ntohs(tcp->dest);
			val = dnat_entries.lookup(&key);
			break;
		case DIR_OUTBOUND:
			key.addr = ntohl(ip->saddr);
			key.port = ntohs(tcp->source);
			val = snat_entries.lookup(&key);
			break;
	}

	if (val == NULL) return XDP_PASS;
	return lookup_nexthop(ctx, eth, ip, tcp, val, dir);
}

static inline int process_tcphdr(
		struct xdp_md *ctx, 
		struct ethhdr *eth,
		struct iphdr *ip,
		enum direction_t dir)
{
	void *data_end = (void *)(long)ctx->data_end;

	struct tcphdr *tcp = (struct tcphdr *)(ip + 1);
	assert_len(tcp, data_end);

	return lookup_endpoint(ctx, eth, ip, tcp, dir);
}

static inline int process_iphdr(
		struct xdp_md *ctx, 
		struct ethhdr *eth,
		enum direction_t dir)
{
	void *data_end = (void *)(long)ctx->data_end;

	struct iphdr *ip = (struct iphdr *)(eth + 1);
	assert_len(ip, data_end);

	if (ip->protocol != IPPROTO_TCP) return XDP_PASS;
	if (ip->version != 4) return XDP_DROP;
	if (ip->ihl != 5) return XDP_PASS;

	return process_tcphdr(ctx, eth, ip, dir);
}

static inline int process_ethhdr(
		struct xdp_md *ctx, 
		enum direction_t dir)
{
	void *data = (void *)(long)ctx->data;
	void *data_end = (void *)(long)ctx->data_end;

	struct ethhdr *eth = data;
	assert_len(eth, data_end);

	if (eth->h_proto != htons(ETH_P_IP)) return XDP_PASS;

	return process_iphdr(ctx, eth, dir);
}

// DNAT用のentrypoint
int entry_external(struct xdp_md *ctx) {
	return process_ethhdr(ctx, DIR_INBOUND);
}

// SNAT用のentrypoint
int entry_internal(struct xdp_md *ctx) {
	return process_ethhdr(ctx, DIR_OUTBOUND);
}
