#!/usr/bin/python3

from ctypes import Structure, c_int, c_uint16, c_uint32
from ipaddress import IPv4Address
import json

from bcc import BPF
from pyroute2 import IPRoute


class Endpoint(Structure):
        _fields_ = [("addr", c_uint32), ("port", c_uint16)]


def get_endpoint(entry):
    addr = int.from_bytes(IPv4Address(entry["addr"]).packed, byteorder="big")
    port = c_uint16(entry["port"])
    return Endpoint(addr, port)


def main():
    with open("./forwarder.c", "r") as f:
        text = f.read()

    with open("./config.json", "r") as f:
        conf = json.loads(f.read())

    b = BPF(text=text)
    ext_fn = b.load_func("entry_external", BPF.XDP)
    int_fn = b.load_func("entry_internal", BPF.XDP)

    ip = IPRoute()
    devmap = b.get_table("devmap")
    for link in ip.get_links():
        idx = link["index"]
        devmap[c_int(idx)] = c_int(idx)

    dnat_entries = b.get_table("dnat_entries")
    snat_entries = b.get_table("snat_entries")

    for entry in conf["entries"]:
        f, t = get_endpoint(entry["from"]), get_endpoint(entry["to"])
        dnat_entries[f] = t
        snat_entries[t] = f

    for interface in conf["interfaces"]["external"]:
        b.attach_xdp(interface, ext_fn, 0)

    for interface in conf["interfaces"]["internal"]:
        b.attach_xdp(interface, int_fn, 0)

    b.trace_print()

if __name__ == "__main__":
    main()
