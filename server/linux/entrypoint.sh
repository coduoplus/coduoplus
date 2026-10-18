#!/bin/sh
# Container entrypoint: assemble a server directory out of the read-only mounts, then
# exec coduo_lnxded with coduoplus_sv.so preloaded. docs/DOCKER.md.
#
# Mounts:
#   /game    (ro)  a CoD:UO install or server dir: main/ or Main/ (CoD pk3s), uo/ (UO
#                  pk3s), optionally mod folders (fs_game) and the 1.51b Linux binaries
#                  (coduo_lnxded + uo/game.mp.uo.i386.so) - from icculus' bins archive
#   /bins    (ro)  optional: the Linux binaries, if they are not inside /game
#   /config  (ro)  optional: server.cfg, competitive.cfg, any other *.cfg
#   /data    (rw)  fs_homepath: everything the server writes (logs, games_mp.log)
#
# Environment: PORT (28960), DEDICATED (2 = internet / master server, 1 = LAN),
# CODUOPLUS_SWING / CODUOPLUS_VERSION_GATE / CODUOPLUS_SNAPS (=0 turns one off),
# CODUOPLUS_ANTILAG / CODUOPLUS_ANTILAG_MAXMS (g_antilag defaults, read by the library),
# CODUOPLUS_COMPETITIVE_FILE (/config/competitive.cfg; re-read when it changes).
set -eu

die() { echo "[entrypoint] $*" >&2; exit 1; }

# ---- the binaries ----------------------------------------------------------------
BIN=""
for d in /bins /game; do
    if [ -f "$d/coduo_lnxded" ] && [ -f "$d/uo/game.mp.uo.i386.so" ]; then BIN=$d; break; fi
done
[ -n "$BIN" ] || die "coduo_lnxded + uo/game.mp.uo.i386.so (1.51b) not found in /bins or /game"

# ---- the base paks ---------------------------------------------------------------
MAIN=""
for d in /game/main /game/Main; do [ -d "$d" ] && MAIN=$d && break; done
[ -n "$MAIN" ] || die "/game has no main/ (or Main/) folder with the CoD pk3s"
[ -d /game/uo ] || die "/game has no uo/ folder with the UO pk3s"

# ---- /srv: symlinks into the mounts, plus the two binaries copied -----------------
# The engine wants one base directory holding main/, uo/ and the game module; the
# mounts stay read-only and untouched.
rm -rf /srv/* 2>/dev/null || true
mkdir -p /srv/uo
ln -s "$MAIN" /srv/main                       # Windows installs spell it "Main"
for f in /game/uo/*.pk3; do [ -e "$f" ] && ln -s "$f" /srv/uo/; done
cp "$BIN/uo/game.mp.uo.i386.so" /srv/uo/
cp "$BIN/coduo_lnxded" /srv/coduo_lnxded
chmod u+x /srv/coduo_lnxded
# mod folders (fs_game), e.g. a PAM: every other directory of /game
for d in /game/*/; do
    n=$(basename "$d")
    case "$n" in main|Main|uo) continue ;; esac
    [ -e "/srv/$n" ] || ln -s "/game/$n" "/srv/$n"
done
n_pk3=$(ls /srv/uo/*.pk3 "$MAIN"/*.pk3 2>/dev/null | wc -l)
[ "$n_pk3" -gt 0 ] || die "no pk3 files under /game/uo and $MAIN"

# ---- configs: /config/*.cfg -> fs_homepath/uo, where +exec finds them -------------
mkdir -p /data/uo
if [ -d /config ]; then
    for f in /config/*.cfg; do [ -f "$f" ] && cp "$f" /data/uo/; done
fi
if [ ! -f /data/uo/server.cfg ]; then
    echo "[entrypoint] no /config/server.cfg - using the example (server/linux/server.cfg.example)"
    cp /opt/coduoplus/server.cfg.example /data/uo/server.cfg
fi
if [ ! -f "$CODUOPLUS_COMPETITIVE_FILE" ]; then
    echo "[entrypoint] no $CODUOPLUS_COMPETITIVE_FILE - no netcode spec is pushed to clients"
fi

echo "[entrypoint] binaries from $BIN, $n_pk3 pk3s, port $PORT, dedicated $DEDICATED"
cd /srv
exec env LD_PRELOAD=/opt/coduoplus/coduoplus_sv.so ./coduo_lnxded \
    +set fs_basepath /srv +set fs_homepath /data \
    +set dedicated "$DEDICATED" +set net_ip 0.0.0.0 +set net_port "$PORT" \
    +set sv_punkbuster 0 \
    "$@"
