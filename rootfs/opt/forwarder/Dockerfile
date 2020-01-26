# syntax=docker/dockerfile:experimental
FROM fedora:30

RUN --mount=type=cache,target=/var/cache/dnf dnf install -y bcc bpftool bpftrace clang iproute kmod python3-pyroute2

COPY * /

CMD "/controller.py"
