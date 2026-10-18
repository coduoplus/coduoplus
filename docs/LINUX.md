# CoDUO+ on a Linux server

`mss32.dll` only runs on Windows. A server running the **Linux** dedicated server
(`coduo_lnxded`) gets the same server-side features from **`coduoplus_sv.so`**, a
library loaded with `LD_PRELOAD`. This page covers what it does, how to run it, how
it was built, and how it's tested.

## What the server side of CoDUO+ is

Most of CoDUO+ is client-side and runs on each player's PC whatever the server is:
frame limiter, raw input, FOV, widescreen, gpu_sync and the overlay. These five
features belong to the **server**, and the Linux library provides all of them:

| # | Feature | Without it, on a Linux server |
| --- | --- | --- |
| 1 | **snaps cap 30 → 40** | at most 30 snapshots/s, so `sv_fps 40` gives 20 |
| 2 | **shortversion `CoDUO+ x.y.z`** | the browser shows a plain `1.51` server |
| 3 | **swing-fix mirror** + `sv_coduoplus_swing 1` | CoDUO+ clients keep the vanilla swing there (correct, but no fix) |
| 4 | **version gate** | outdated CoDUO+ clients get in |
| 5 | **competitive spec publisher** (`competitive.cfg`) | clients stay at `snaps 20` / `cl_maxpackets 30` / `rate 5000` |
| 6 | **large-inode file access** (not a CoDUO+ feature: a fix for the 2005 binary) | on a Windows drive under WSL/Docker Desktop, or a big XFS/btrfs disk: "0 files in pk3 files", the server won't start |

Shared parts are compiled from the same files on both sides:
`src/shared/coduoplus_shared.h` (the swing values, cvar names and net version)
and `src/shared/competitive_spec.cpp` (the `competitive.cfg` parser and chunker).
So a client and a server from the same tree can't disagree.

## Running it

**Easiest: the Docker image**, see [DOCKER.md](DOCKER.md) (`docker compose up -d` with
the example compose file). By hand:

Supported build: **CoD:UO 1.51b Linux server** (icculus, 2005-03-22:
`coduo_lnxded` sha256 `24b0d826…61f1`, `uo/game.mp.uo.i386.so` `4d2391c1…acb7`). On
any other build the library logs which byte didn't match and patches nothing;
the server runs vanilla.

```sh
# next to coduo_lnxded:
cp coduoplus_sv.so competitive.cfg.example .
mv competitive.cfg.example competitive.cfg        # optional: the 40-tick netcode spec
LD_PRELOAD=./coduoplus_sv.so ./coduo_lnxded +set dedicated 2 +set sv_fps 40 \
    +set sv_maxRate 0 +exec server.cfg +map_rotate
```

The console shows what was applied:

```text
[coduoplus] CoDUO+ 0.2.0 server library loaded (coduo_lnxded 1.51b)
[coduoplus] competitive: snaps cap 30 -> 40 applied
[coduoplus] version_patch: shortversion -> "CoDUO+ 0.2.0" applied
[coduoplus] engine: dlopen hook installed - every game module load is patched
[coduoplus] swing_fix [server]: ON in 0xee7ae000 (legs_tol=0.00 pitch_spd=1.00 ...)
[coduoplus] game: .../game.mp.uo.i386.so loaded at 0xee7ae000 - ClientConnect gate installed, G_RunFrame hook installed (min coduoplus=16)
[coduoplus] swing_fix: server publishes sv_coduoplus_swing 1 (engine holds "1")
[coduoplus] competitive_sv: competitive.cfg - publishing "snaps=40 cl_maxpackets=125 com_maxfps=125:250 rate=90000"
```

Requirements: 32-bit glibc **2.17 or newer**, which covers any distro of the last
ten years. `coduo_lnxded` itself still needs `libstdc++.so.5`, see its README.

| Environment variable | Effect |
| --- | --- |
| `CODUOPLUS_SWING=0` | don't mirror the swing fix (publish `0`: clients stay vanilla) |
| `CODUOPLUS_VERSION_GATE=0` | let every client in |
| `CODUOPLUS_SNAPS=0` | keep the vanilla snaps cap of 30 |
| `CODUOPLUS_COMPETITIVE_FILE` | path of `competitive.cfg` (default: the server's working directory) |
| `CODUOPLUS_LFS=0` | leave the engine's 32-bit `stat`/`readdir` alone (see below) |

`LD_PRELOAD` is inherited by everything the server starts; the library only acts
inside a process whose executable is `coduo_lnxded`.

## How it works

```text
LD_PRELOAD constructor (before main)
 ├─ check every engine site's bytes (sites_151b.h) -> else: do nothing
 ├─ snaps clamp 30 -> 40             808d490 imm8, 808d496 imm32
 ├─ shortversion default -> "CoDUO+" 80719d5 operand
 ├─ engine's __xstat/readdir/closedir GOT slots -> 64-bit wrappers (DOCKER.md)
 └─ engine's dlopen GOT slot -> ours 80fe1cc
                │
     every map: Sys_LoadDll -> dlopen("…/uo/game.mp.uo.i386.so")
                │
 our dlopen: real dlopen, then
 ├─ check exports (dlsym = base + RVA) and every game site's bytes -> else: vanilla
 ├─ swing: 4 code sites (below)
 └─ GOT slots of ClientConnect / G_RunFrame -> our hooks
                │
 G_RunFrame (game thread, every frame): register + publish SYSTEMINFO cvars
 ClientConnect: version gate, then the real ClientConnect
```

**Why hook `dlopen` through the engine's GOT.** The engine reloads the game module
on every map change, possibly at a new address, and patches must be in before
the engine calls `dllEntry`/`vmMain`. Hooking `dlopen` catches each load as it
happens. It's hooked by repointing the executable's own PLT slot
(`R_386_JUMP_SLOT dlopen` at `0x80fe1cc`) rather than by exporting a `dlopen`
symbol, which avoids depending on symbol-version binding rules. The real
`dlopen` must come from `dlsym(RTLD_NEXT)`, never `RTLD_DEFAULT`. `coduo_lnxded`
isn't position-independent, so it *exports its own PLT stub* as `dlopen`'s
canonical address; the first build fetched that stub, called itself through the
repointed slot, and crashed at the first map load.

**Why GOT slots for `ClientConnect` / `G_RunFrame`.** `vmMain` calls them through the
module's PLT (`jmp [ebx+0x7e0]` / `[ebx+0x748]`), which is the ELF counterpart of
the Windows import table. Repointing the slot catches every call and changes no
code; the original is called at the address `dlsym` returned.

**Swing, four sites in `BG_PlayerAngles`.** The same four constants the client
changes. GCC compiled them differently from MSVC, so the patches differ:

| Site | Original | Patched to |
| --- | --- | --- |
| movement-yaw pull `0x1de0e` | `fld [ebx-0x1a1c0]` (the 0.3f in .rodata) | `fld [&g_swing_movefrac]` (0.0) |
| torso yaw speed `0x1de9e` | `mov eax,[ebx+0xeec]` (GOT → `bg_swingSpeed`, `.value` read at `[eax+8]`) | `mov eax,&g_swing_yaw_speed-8 ; nop` (1.0) |
| legs swing tolerance `0x1e044` | `mov [esp+4],40.0f` | 0.0 |
| lean channel speed `0x1e16f` | `mov [esp+0xc],0.15f` | 1.0 |

`bg_swingSpeed` has five readers: the torso one is redirected, and the four legs
readers keep the cvar, exactly as on Windows. The patches are written inside
`dlopen`, before the module has run any code.

**Publishing (game thread).** From the first `G_RunFrame` of each load:
`trap_Cvar_Register` creates `sv_coduoplus_swing` (SYSTEMINFO|ROM) and
`sv_competitive`…`4` (SYSTEMINFO), and `trap_Cvar_Set` sets them. Using the game's
own traps means the engine's main thread does the work, with nothing done
behind its back. `competitive.cfg` is re-read when its mtime changes, at most
once a second.

**Version gate.** It reads the client's userinfo with `trap_GetUserinfo`. A
`coduoplus` value below 16 is refused with the same message as the Windows server;
no key (vanilla) means the client is let in.

All addresses and the bytes expected there are in `server/linux/sites_151b.h`, each
with its disassembly. That one table is used both by the library at run time and
by the offline test.

## Systeminfo is full on stock UO servers

Clients receive server settings through the `CS_SYSTEMINFO` string, which the
engine caps at **1024 characters**. It drops any key that doesn't fit and prints
"Info string length exceeded". A **vanilla** UO server is already over the cap:
measured with only the stock paks, 11 `ui_*` keys are dropped. With 60+ map pk3s
`sv_paks`/`sv_referencedPaks` are dropped too, mod or not. CoDUO+'s keys get
through because they're registered last and come first in the string (checked by
both smoke tests), but each character they use pushes another vanilla key out.
So:

- keep `competitive.cfg` to what you need (the example has the four 40-tick lines
  active and the rest commented out);
- keep the number of pk3s on the server down; `sv_pakNames`/`sv_paks` are the
  biggest entries.

## Testing

| Test | What |
| --- | --- |
| `site_check_linux` (61 checks, offline, runs on Windows) | every site's bytes, plus each address pinned to its meaning: exports via `.dynsym`, GOT slots via the `R_386_JUMP_SLOT`/`GLOB_DAT` relocations, PLT stubs, `vmMain`'s calls, trap syscall numbers, the 0.3f / 40.0 / 0.15 constants, the snaps userinfo key and `snapshotMsec` store |
| `smoke_linux` (WSL + Docker) | builds the **server image** ([DOCKER.md](DOCKER.md)) and runs the **real `coduo_lnxded`** in it against the Windows install, read-only, on mp_harbor (after a control run proving the large-inode fix is needed), then checks: Docker health; `getstatus` protocol 22 + `CoDUO+` shortversion; real connects (old CoDUO+ refused, vanilla and current accepted); `rcon systeminfo` holds `sv_coduoplus_swing 1` and the spec; **a map change to mp_arnhem** re-applies everything and clients still receive both; the console log has every patch and no refusal |
| tampered module (manual) | one swing constant changed in the `.so`: the library logs the mismatch, patches nothing, and the server runs vanilla |

Run them: `cmake -B build -DCODUO_LINUX_SMOKE=ON` and `ctest -R linux`. The test reads
the game from `CODUO_GAME_DIR`, the same install as the Windows tests.

Build the library by hand (the `artifact` stage of the server image's Dockerfile):

```sh
docker build -f server/linux/Dockerfile --target artifact --output type=local,dest=build/linux .
```

It's built on Debian 11 i386 from a fixed snapshot date (reproducible; the oldest
glibc we build on). The library links its C++ runtime statically with every symbol
hidden, so it can't clash with the `libstdc++.so.5` the server loads.
