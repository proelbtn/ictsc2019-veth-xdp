#!/bin/bash

BASEIMAGE="base"

if ! (machinectl list-images | grep -q ${BASEIMAGE}); then
	sudo btrfs subvolume create /var/lib/machines/${BASEIMAGE}
	sudo debootstrap bionic /var/lib/machines/${BASEIMAGE} http://archive.ubuntu.com/ubuntu/
	sudo machinectl read-only base
fi

machinectl list-images | grep -q m1 || sudo machinectl clone ${BASEIMAGE} m1
machinectl list-images | grep -q m2 || sudo machinectl clone ${BASEIMAGE} m2

sudo systemd-nspawn -D /var/lib/machines/m1 bash -c "useradd -s /bin/bash -m user && yes PlzComeToICTSC2020 | passwd user"
sudo systemd-nspawn -D /var/lib/machines/m1 bash -c "rm -rf /etc/resolv.conf && echo 'nameserver 8.8.8.8' > /etc/resolv.conf"
sudo systemd-nspawn -D /var/lib/machines/m1 bash -c "apt update && apt install -y ethtool nginx openssh-server tcpdump"
sudo systemd-nspawn -D /var/lib/machines/m1 bash -c "cat <<EOF | tee /etc/netplan/00_host0.yaml && netplan generate
network:
  version: 2
  renderer: networkd
  ethernets:
    host0:
      addresses:
        - 10.123.1.1/24
      gateway4: 10.123.1.254
      nameservers:
        addresses:
          - 8.8.8.8
          - 8.8.4.4
EOF"

sudo systemd-nspawn -D /var/lib/machines/m2 bash -c "useradd -s /bin/bash -m user && yes PlzComeToICTSC2020 | passwd user"
sudo systemd-nspawn -D /var/lib/machines/m2 bash -c "rm -rf /etc/resolv.conf && echo 'nameserver 8.8.8.8' > /etc/resolv.conf"
sudo systemd-nspawn -D /var/lib/machines/m2 bash -c "apt update && apt install -y apache2 ethtool openssh-server tcpdump"
sudo systemd-nspawn -D /var/lib/machines/m2 bash -c "cat <<EOF | tee /etc/netplan/00_host0.yaml && netplan generate
network:
  version: 2
  renderer: networkd
  ethernets:
    host0:
      addresses:
        - 10.123.2.1/24
      gateway4: 10.123.2.254
      nameservers:
        addresses:
          - 8.8.8.8
          - 8.8.4.4
EOF"

sudo machinectl enable m1
sudo machinectl enable m2

sudo systemctl daemon-reload
sudo systemctl enable setup-veth

cd /opt/forwarder
make build
make stop
make start
