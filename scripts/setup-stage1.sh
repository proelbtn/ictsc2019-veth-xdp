#!/bin/bash

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

sudo reboot
