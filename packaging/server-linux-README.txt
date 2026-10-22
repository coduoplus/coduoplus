CoDUO+ {VERSION} - Linux dedicated server for Call of Duty: United Offensive 1.51b
https://github.com/coduoplus/coduoplus

Two ways to run it. Both need the game's pk3s (main/ and uo/ from any CoD:UO 1.51
install, Windows or Linux) and the 1.51b Linux server binaries (coduo_lnxded,
uo/game.mp.uo.i386.so: coduo-lnxded-1.51b-bins.tar.bz2).

A. Docker (easiest) - docs/DOCKER.md
     cp .env.example .env              # set CODUO_GAME_DIR and CODUO_BINS_DIR
     mkdir -p config
     cp competitive.cfg.example config/competitive.cfg
     cp server.cfg.example config/server.cfg       # then edit it
     docker compose up -d
   The image is ghcr.io/coduoplus/coduoplus-server:{VERSION} (:latest = newest).

B. Directly - docs/LINUX.md
     ./coduoplus-server.sh /path/to/coduo
   /path/to/coduo has coduo_lnxded, uo/game.mp.uo.i386.so and the pk3s. The script
   preloads coduoplus_sv.so, creates competitive.cfg and uo/server.cfg from the
   examples the first time, and runs +exec server.cfg +map_rotate (pass your own
   engine arguments after the path to replace that). PORT and DEDICATED (2 =
   internet, 1 = LAN) come from the environment. coduo_lnxded needs the 32-bit
   libstdc++.so.5 (Debian/Ubuntu: libstdc++5:i386).

What the server gets: 40-tick (snaps cap 30 -> 40), competitive.cfg pushed live to
CoDUO+ clients, the swing-fix mirror, the version gate, CoDUO+ in the server list,
and pk3s readable from any filesystem (large-inode fix). PunkBuster is not supported.
