#!/bin/sh
# Build coduoplus_sv.so. Run inside the image from server/linux/Dockerfile:
#
#   docker build -t coduoplus-linux server/linux
#   docker run --rm -v "$PWD":/src coduoplus-linux sh server/linux/build.sh
#
# -> build/linux/coduoplus_sv.so
#
# Static libstdc++/libgcc with every symbol of theirs hidden: coduo_lnxded loads the
# 2002-era libstdc++.so.5 into the same process, and our C++ runtime must not be seen
# by (or see) it. Only glibc is linked dynamically.
set -eu
cd "$(dirname "$0")/../.."
mkdir -p build/linux
g++ -std=c++17 -O2 -fPIC -shared -fvisibility=hidden -Wall -Wextra \
    -Isrc -Iserver/linux \
    server/linux/coduoplus_sv.cpp src/shared/competitive_spec.cpp src/shared/antilag_core.cpp \
    -static-libstdc++ -static-libgcc -Wl,--exclude-libs,ALL -Wl,-z,defs \
    -ldl -o build/linux/coduoplus_sv.so
# what the host's glibc must provide
echo "glibc symbol versions needed:"
objdump -T build/linux/coduoplus_sv.so | grep -oE 'GLIBC_[0-9.]+' | sort -uV | tail -1
ls -l build/linux/coduoplus_sv.so
