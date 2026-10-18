#!/bin/bash
# Docker HEALTHCHECK: the server must answer an out-of-band getinfo on $PORT.
# bash's /dev/udp opens a connected UDP socket, so the reply can be read back.
exec 3<>"/dev/udp/127.0.0.1/${PORT:-28960}" || exit 1
printf '\xff\xff\xff\xffgetinfo hc' >&3
reply=$(timeout 3 head -c 16 <&3 | tr -d '\377')
exec 3>&-
[[ "$reply" == infoResponse* ]]
