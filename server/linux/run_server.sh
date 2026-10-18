#!/bin/sh
# Start coduo_lnxded with coduoplus_sv.so preloaded, inside the server/linux image.
#
#   docker run --rm -p 28997:28997/udp \
#     -v <coduo-lnxded dir>:/bins:ro       (coduo_lnxded + uo/game.mp.uo.i386.so, 1.51b)
#     -v <game install>:/game:ro           (Main/ and uo/ of a CoD:UO 1.51 install)
#     -v <repo>:/src:ro                    (build/linux/coduoplus_sv.so, competitive.cfg.example)
#     coduoplus-linux sh /src/server/linux/run_server.sh [extra +set args]
#
# The server needs one directory with main/ (CoD base pk3s), uo/ (UO pk3s + the game
# module) and the binary. It is assembled in /srv out of symlinks, so the mounted
# install stays read-only; everything the server writes goes to /srv/home.
# COMPETITIVE=1 copies competitive.cfg.example in as the server's competitive.cfg.
set -eu
mkdir -p /srv/uo /srv/home
cp /bins/coduo_lnxded /srv/coduo_lnxded
ln -sfn /game/Main /srv/main
for f in /game/uo/*.pk3; do ln -sf "$f" /srv/uo/; done
cp /bins/uo/game.mp.uo.i386.so /srv/uo/
if [ "${COMPETITIVE:-0}" = 1 ]; then cp /src/competitive.cfg.example /srv/competitive.cfg; fi
cd /srv
exec env LD_PRELOAD="${CODUOPLUS_LIB:-/src/build/linux/coduoplus_sv.so}" ./coduo_lnxded \
    +set fs_basepath /srv +set fs_homepath /srv/home \
    +set dedicated 1 +set net_ip 0.0.0.0 +set net_port 28997 +set sv_punkbuster 0 \
    "$@" +map mp_harbor
