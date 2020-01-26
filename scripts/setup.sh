#!/bin/bash

USER=${USER:-ictsc}
BASEIMAGE="ubuntu-bionic-amd64"
PASSWORD="$(echo -n veth-is-an-virtual-ethernet-device | md5sum | cut -c1-8)"

set -x

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
	sudo debootstrap --arch=amd64 bionic /var/lib/machines/${BASEIMAGE}
fi

if ! (machinectl list-images | grep -q m1); then
	sudo machinectl clone ${BASEIMAGE} m1
	sudo systemd-nspawn -D /var/lib/machines/m1 useradd -m admin
	sudo systemd-nspawn -D /var/lib/machines/m1 bash -c "yes ${PASSWORD} | passwd admin"
fi

if ! (machinectl list-images | grep -q m2); then
	sudo machinectl clone ${BASEIMAGE} m2
	sudo systemd-nspawn -D /var/lib/machines/m2 useradd -m admin
	sudo systemd-nspawn -D /var/lib/machines/m2 bash -c "yes ${PASSWORD} | passwd admin"
fi

sudo systemctl daemon-reload

