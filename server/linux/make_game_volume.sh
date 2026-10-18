#!/bin/sh
# Populate a Docker volume with the STOCK CoD + CoD:UO paks the Linux test server needs.
#
#   sh server/linux/make_game_volume.sh "/mnt/d/Call of Duty - Copy" [volume]   (default coduo-uo-game)
#
# Why a copy and not the install mounted directly: on a Windows drive seen from WSL
# (drvfs / 9p) inode numbers do not fit in 32 bits, and coduo_lnxded (2005, 32-bit,
# non-LFS stat/readdir) gets EOVERFLOW on every file - it then finds "0 files in pk3
# files" and dies with "Couldn't load default_mp.cfg". On the volume (ext4) inodes are
# small. Only the stock paks are copied: the test is about the mod, not map packs.
# Idempotent: a file already there with the same size is skipped. ~2.2 GB.
set -eu
SRC="$1"
VOL="${2:-coduo-uo-game}"
docker volume create "$VOL" >/dev/null
docker run --rm -v "$SRC":/src:ro -v "$VOL":/game i386/debian:bullseye-slim sh -c '
  set -eu
  mkdir -p /game/Main /game/uo
  for f in /src/Main/pak*.pk3 /src/Main/localized_*pak*.pk3 \
           /src/uo/pakuo*.pk3 /src/uo/localized_*pakuo*.pk3; do
    [ -f "$f" ] || continue
    d=/game/$(basename "$(dirname "$f")")/$(basename "$f")
    if [ ! -f "$d" ] || [ "$(stat -c %s "$f")" != "$(stat -c %s "$d")" ]; then cp "$f" "$d"; fi
  done
  du -sh /game/Main /game/uo'
