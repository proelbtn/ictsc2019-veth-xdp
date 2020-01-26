DOCKER_OPTS := -it --privileged --net host -v $(shell pwd):/host -v /lib/modules/$(shell uname -r):/lib/modules/$(shell uname -r) -v /usr/src/linux-headers-$(shell uname -r | sed "s:-generic::"):/usr/src/linux-headers-$(shell uname -r | sed "s:-generic::") -v /usr/src/linux-headers-$(shell uname -r):/usr/src/linux-headers-$(shell uname -r) -v /sys/kernel/debug:/sys/kernel/debug --workdir /host

all:

build:
	DOCKER_BUILDKIT=1 docker build -t forwarder .

debug:
	docker run ${DOCKER_OPTS} --rm forwarder /bin/bash

start:
	docker run ${DOCKER_OPTS} --name forwarder --restart=always -d forwarder

stop:
	docker rm -f forwarder
