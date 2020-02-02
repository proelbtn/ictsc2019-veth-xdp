#!/bin/sh

while [ "$(ip link show ve-m1 | grep 'state UP')" = "" ]
do
	sleep 1
done

sleep 1

ip addr flush ve-m1
ip addr add 10.123.1.254/24 dev ve-m1

iptables -t filter -A FORWARD --src 10.123.1.0/24 -j ACCEPT
iptables -t nat -A POSTROUTING --src 10.123.1.0/24 -j MASQUERADE

while [ "$(ip link show ve-m2 | grep 'state UP')" = "" ]
do
	sleep 1
done

sleep 1

ip addr flush ve-m2
ip addr add 10.123.2.254/24 dev ve-m2

iptables -t filter -A FORWARD --src 10.123.2.0/24 -j ACCEPT
iptables -t nat -A POSTROUTING --src 10.123.2.0/24 -j MASQUERADE
