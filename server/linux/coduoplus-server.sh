#!/bin/sh
# CoDUO+ Linux dedicated server, without Docker: coduo_lnxded 1.51b with coduoplus_sv.so
# preloaded (docs/LINUX.md). Run it from anywhere:
#
#   ./coduoplus-server.sh /path/to/coduo [engine args...]
#
# /path/to/coduo holds coduo_lnxded, uo/game.mp.uo.i386.so and the game's main/ + uo/
# pk3s. Engine args default to `+exec server.cfg +map_rotate` (server.cfg in uo/ there,
# or in ~/.callofduty/uo/). competitive.cfg - the netcode spec CoDUO+ clients receive -
# is read from that folder; this script creates it from the example the first time.
#
# Environment: PORT (28960), DEDICATED (2 = internet, 1 = LAN), and the library's own
# switches CODUOPLUS_SWING / _VERSION_GATE / _SNAPS / _LFS / _COMPETITIVE_FILE (LINUX.md).
set -eu

here=$(cd "$(dirname "$0")" && pwd)
game=${1:?usage: $0 /path/to/coduo [engine args...]}
shift
game=$(cd "$game" && pwd)

[ -x "$game/coduo_lnxded" ] || { echo "coduo_lnxded not found (or not executable) in $game" >&2; exit 1; }
[ -f "$game/uo/game.mp.uo.i386.so" ] || { echo "uo/game.mp.uo.i386.so not found in $game" >&2; exit 1; }
[ -f "$game/competitive.cfg" ] || cp "$here/competitive.cfg.example" "$game/competitive.cfg"
[ -f "$game/uo/server.cfg" ] || [ -f "$HOME/.callofduty/uo/server.cfg" ] || cp "$here/server.cfg.example" "$game/uo/server.cfg"

[ $# -gt 0 ] || set -- +exec server.cfg +map_rotate
cd "$game"
exec env LD_PRELOAD="$here/coduoplus_sv.so" ./coduo_lnxded \
    +set dedicated "${DEDICATED:-2}" +set net_port "${PORT:-28960}" +set sv_punkbuster 0 "$@"
