#!/bin/bash

USER=${USER:-ictsc}
BASEIMAGE="base"

set -x

sudo useradd -s /bin/bash -m -G sudo -p '$6$feoWBZt29xWXIJhO$oHQCbGSFGnUgzrVw9gaXk0lbAuF7bHAfI8spR.a8H3IXF./6rq3quIoezF7Fs9EWMkna4qTGPN08u5O0ss6V20' user

sudo apt update
sudo apt install -y \
	build-essential \
	clang \
	curl \
	linux-buildinfo-5.3.0-23-generic \
	linux-headers-5.3.0-23-generic \
	linux-image-5.3.0-23-generic \
	linux-modules-5.3.0-23-generic \
	linux-modules-extra-5.3.0-23-generic \
	linux-tools-5.3.0-23-generic \
	neovim \
	systemd-container \
	tcpdump \
	wget

if ! (ls -lh /var/lib/machines.raw | grep -q 4.0G); then
	sudo umount /var/lib/machines
	sudo losetup -d /dev/loop0
	sudo rm -rf /var/lib/machines.raw
	sudo dd if=/dev/zero of=/var/lib/machines.raw bs=1M count=4096
	sudo losetup /dev/loop0 /var/lib/machines.raw
	sudo mkfs.btrfs /dev/loop0
	sudo mount /dev/loop0 /var/lib/machines
fi


if ! (which docker >/dev/null 2>/dev/null); then
	curl -L get.docker.com | sudo bash
fi

if ! (id ${USER} | grep -q docker); then
	sudo usermod -aG docker ${USER} 
fi

for file in $(find rootfs -type f); do
	SRC="${file}"
	DST="${file#rootfs}"
	
	sudo mkdir -p "$(dirname "${DST}")"
	sudo cp "${SRC}" "${DST}"
done


if ! (machinectl list-images | grep -q ${BASEIMAGE}); then
	sudo btrfs subvolume create /var/lib/machines/${BASEIMAGE}
	sudo debootstrap bionic /var/lib/machines/${BASEIMAGE} http://archive.ubuntu.com/ubuntu/
	sudo machinectl read-only base
fi

machinectl list-images | grep -q m1 || sudo machinectl clone ${BASEIMAGE} m1
machinectl list-images | grep -q m2 || sudo machinectl clone ${BASEIMAGE} m2

sudo systemd-nspawn -D /var/lib/machines/m1 bash -c "rm -rf /etc/resolv.conf && echo 'nameserver 8.8.8.8' > /etc/resolv.conf"
sudo systemd-nspawn -D /var/lib/machines/m1 bash -c "apt update && apt install -y nginx openssh-server"
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

sudo systemd-nspawn -D /var/lib/machines/m2 bash -c "rm -rf /etc/resolv.conf && echo 'nameserver 8.8.8.8' > /etc/resolv.conf"
sudo systemd-nspawn -D /var/lib/machines/m2 bash -c "apt update && apt install -y apache2 openssh-server"
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

cd /opt/forwarder
make build
make stop
make start
