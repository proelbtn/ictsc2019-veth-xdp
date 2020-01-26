#!/bin/sh

while [ "$(ip link show ve-m1 | grep 'state UP')" = "" ]
do
	sleep 1
done

sleep 3

ip address flush ve-m1
ip address add 10.123.1.254/24 dev ve-m1

while [ "$(ip link show ve-m2 | grep 'state UP')" = "" ]
do
	sleep 1
done

sleep 3

ip address flush ve-m2
ip address add 10.123.2.254/24 dev ve-m2
