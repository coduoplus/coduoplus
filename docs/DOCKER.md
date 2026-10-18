# Running a CoDUO+ server in Docker

The `coduoplus-server` image runs the CoD:UO 1.51b **Linux** dedicated server
(`coduo_lnxded`) with CoDUO+'s server library preloaded. That gives every
server-side CoDUO+ feature, as described in [LINUX.md](LINUX.md). It works on any
Linux host with Docker, and on Windows through WSL 2 / Docker Desktop.

Files: `server/linux/Dockerfile`, `entrypoint.sh`, `healthcheck.sh`,
`server.cfg.example`, `docker-compose.yml`, `.env.example`.

## Quick start (compose)

```sh
cd server/linux
cp .env.example .env                     # where the game files are, port, LAN/internet
mkdir -p config
cp ../../competitive.cfg.example config/competitive.cfg    # the 40-tick netcode spec
cp server.cfg.example config/server.cfg                    # hostname, rotation, ... - edit it
docker compose up -d                     # the published image (add --build to build this checkout)
docker compose logs -f
```

The example [`docker-compose.yml`](../server/linux/docker-compose.yml):

```yaml
services:
  coduo:
    image: ${CODUOPLUS_IMAGE:-ghcr.io/coduoplus/coduoplus-server:latest}
    build: { context: ../.., dockerfile: server/linux/Dockerfile }
    restart: unless-stopped
    stdin_open: true          # `docker attach coduoplus` = the server console
    tty: true                 #   (detach: Ctrl-P Ctrl-Q, not Ctrl-C)
    ports: ["${PORT:-28960}:${PORT:-28960}/udp"]
    environment: { PORT: ..., DEDICATED: ..., CODUOPLUS_SWING: ..., ... }
    volumes:
      - ${CODUO_GAME_DIR:-./game}:/game:ro     # main/ + uo/ with the pk3s
      - ${CODUO_BINS_DIR:-./bins}:/bins:ro     # coduo_lnxded + uo/game.mp.uo.i386.so
      - ./config:/config:ro                    # server.cfg, competitive.cfg
      - coduo-data:/data                       # logs, games_mp.log
    command: ["+exec", "server.cfg", "+map_rotate"]
```

To run **several servers** (say a public one and a match one), add a service per
server, each with its own `PORT`, `config` folder and data volume. They can share
`/game` and `/bins`.

## What you have to provide

**The image contains no game files.** The binaries and pk3s are Activision's and
are mounted at run time, which is also why the image can be published.

| Mount | Contents | Where from |
| --- | --- | --- |
| `/game` (ro) | `main/` (or `Main/`) with the CoD pk3s, `uo/` with the UO pk3s, and optionally mod folders (`fs_game`) | a CoD:UO 1.51 install: Windows or Linux, retail or Steam |
| `/bins` (ro) | `coduo_lnxded` and `uo/game.mp.uo.i386.so`, **1.51b** | `coduo-lnxded-1.51b-bins.tar.bz2` (icculus; mirrored on ModDB / [cod.pm](https://cod.pm/guide/a7a40b/call-of-duty-1-server-on-linux-installing-and-configuring)). They can also sit inside `/game` |
| `/config` (ro) | `server.cfg`, `competitive.cfg`, any other `*.cfg` | yours. Without a `server.cfg` the [example](../server/linux/server.cfg.example) is used |
| `/data` (rw) | `fs_homepath`: everything the server writes | a named volume |

Map packs: put the pk3s in `/game/uo` like on any server. Keep the count down,
though: see [systeminfo is full](LINUX.md#systeminfo-is-full-on-stock-uo-servers).

## Configuration

| Variable | Default | |
| --- | --- | --- |
| `PORT` | `28960` | UDP port, inside the container and published |
| `DEDICATED` | `2` | `2` = internet (heartbeats to the master server), `1` = LAN only |
| `CODUOPLUS_SWING` | `1` | `0` = no swing-fix mirror (clients keep vanilla swing) |
| `CODUOPLUS_VERSION_GATE` | `1` | `0` = let outdated CoDUO+ clients in |
| `CODUOPLUS_SNAPS` | `1` | `0` = vanilla snaps cap of 30 |
| `CODUOPLUS_ANTILAG` | `1` | default of `g_antilag` ([lag compensation](NETCODE.md#lag-compensation)): `0` off, `2` logs every compensated shot; rcon changes it live |
| `CODUOPLUS_ANTILAG_MAXMS` | `300` | default of `g_antilagMaxMs` |
| `CODUOPLUS_LFS` | `1` | `0` = don't wrap the engine's stat/readdir ([why](#game-files-on-any-filesystem)) |
| `CODUOPLUS_COMPETITIVE_FILE` | `/config/competitive.cfg` | the netcode spec pushed to CoDUO+ clients |

**Engine arguments** go in `command:` (or after the image name in `docker run`).
They *replace* the default `+exec server.cfg +map_rotate`, so keep that in. The
entrypoint always adds `fs_basepath`, `fs_homepath`, `dedicated`, `net_ip`,
`net_port` and `sv_punkbuster 0`; PunkBuster isn't supported.

**`competitive.cfg` is live.** The library checks the file once a second and
publishes a changed spec immediately, so you can edit `config/competitive.cfg`
without restarting. `server.cfg` is copied in at start, so a change to it needs
`docker compose restart` (or an `exec` from rcon).

The server console: `docker attach coduoplus` (detach with Ctrl-P Ctrl-Q). Or set
`rconpassword` in `server.cfg` and use any rcon tool.

## What the entrypoint does, and why

1. **Finds the binaries** in `/bins`, else `/game`, and stops with a clear message
   if they're missing (CI checks this message).
2. **Builds `/srv`**: the engine wants one base folder holding `main/`, `uo/` and
   the game module. `/srv` is made of symlinks into the read-only mounts
   (`main` → `/game/Main`, because a Windows install spells it with a capital M,
   and one link per pk3), plus a copy of the two binaries. Other folders in `/game`
   (mods) are linked in too, and the mounts are never written to.
3. **Copies `/config/*.cfg`** to `/data/uo`, where `+exec` looks for them.
4. **Execs `coduo_lnxded`** under `tini`, with `LD_PRELOAD=coduoplus_sv.so`. `tini`
   is PID 1 and forwards `SIGTERM`, so `docker stop` takes a fraction of a second
   instead of a 10 s timeout and a kill. The server runs as the unprivileged user
   `coduo` (uid 1000).

**Health check:** every 30 s the container sends a real `getinfo` to the server
over UDP (`healthcheck.sh`, using bash's `/dev/udp`). `docker ps` shows `healthy`
only while the server is answering.

## Game files on any filesystem

`coduo_lnxded` dates from 2005 and uses the 32-bit `stat()`/`readdir()`. glibc
fails them with `EOVERFLOW` whenever a file's inode number doesn't fit in 32
bits, and the server then reports **`0 files in pk3 files`** and quits with
"Couldn't load default_mp.cfg". That happens with a Windows drive mounted into
WSL or Docker Desktop (`/mnt/d/...`), and with large XFS or btrfs disks.

The library fixes it by repointing the engine's `__xstat`/`readdir`/`closedir`
imports to wrappers around the 64-bit calls. The wrappers return the 32-bit
structs the engine expects, with the inode folded to 32 bits (the engine never
uses it). So on Windows you can mount the install directly:

```sh
CODUO_GAME_DIR="/mnt/d/Call of Duty" docker compose up -d
```

The smoke test proves both sides of this, on a Windows drive: with
`CODUOPLUS_LFS=0` the engine finds 0 pk3s, and with the wrappers it finds all of
them (56 269 files from 83 pk3s in the test install). This applies to plain
`LD_PRELOAD` use too, not just the image.

## Building

```sh
# from the repository root - the context must be the root (src/shared is compiled in)
docker build -f server/linux/Dockerfile -t coduoplus-server .
# just the library, for a non-Docker server:
docker build -f server/linux/Dockerfile --target artifact --output type=local,dest=build/linux .
```

| Stage | Base | Why |
| --- | --- | --- |
| `build` | Debian 11 i386, at the base image's **snapshot.debian.org** date | 32-bit; the oldest glibc we build on, so the `.so` needs only glibc 2.17; the frozen snapshot makes it reproducible (the live bullseye-security mirror 404'd during development) |
| `artifact` | `scratch` | only `coduoplus_sv.so`, used by CI and releases |
| `server` | Debian 12 i386, live | it faces the internet, so it gets security updates; `libstdc++.so.5` (which the 2005 server needs and no current distro ships) is fetched from the Debian archive and **checked against a pinned sha256** |

The stages are pinned to `linux/386` whatever the build host, hence the
`check=skip` directive on the Dockerfile's first line. `docker build --check`
passes clean, and CI runs it.

## Releases

A `vX.Y.Z` tag publishes, once every test has passed, the image
`ghcr.io/coduoplus/coduoplus-server:X.Y.Z` (and `:latest`), plus
`coduoplus_sv.so` and this `docker-compose.yml` as release assets. See
[CI.md](CI.md).

## Tested

`smoke_linux` ([TESTING.md](TESTING.md)) builds **this image** and runs it as in
production: read-only `/game` (the Windows install on `D:`, through WSL),
`/bins` and `/config`. It checks:

- the large-inode control run;
- Docker's health check reports `healthy`;
- protocol 22 and the `CoDUO+` shortversion;
- real connects against the version gate;
- `rcon systeminfo` holds the swing flag and the spec, before and after a map change;
- the console log has every patch;
- `docker stop` finishes in under 5 s.

`docker compose up` with the example file was also run by hand:

- reachable through the published UDP port from Windows `localhost` and the LAN IP;
- `config/server.cfg` applied (hostname, gametype, rotation);
- an edit to `config/competitive.cfg` published within 2 s.
