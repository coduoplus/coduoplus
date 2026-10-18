# Netcode and hit registration — what decides a hit, and how to set it up

How a shot is registered in CoD:UO 1.51, what each setting changes, the server and
client configuration CoDUO+ is built for, why the server should run **`sv_fps 40`**,
and what is still missing for truly LAN-like hit registration over the internet.

Engine facts come from the CoD:UO reverse-engineering notes
(`coduo-1.6/analysis/netcode.md`, `client-settings.md`) and from the addresses
in [ADDRESSES.md](ADDRESSES.md).

## Contents

1. [How a shot travels](#how-a-shot-travels)
2. [How far you have to lead](#how-far-you-have-to-lead)
3. [What CoDUO+ changes](#what-coduo-changes)
4. [Server setup](#server-setup)
5. [Why sv_fps 40, and not more](#why-sv_fps-40-and-not-more)
6. [Client setup](#client-setup)
7. [Lag compensation](#lag-compensation)

---

## How a shot travels

```text
client                                   server
──────                                   ──────
renders the world from snapshots,        receives the packet -> ClientThink ->
one snapshot interval in the past        Pmove -> weapon fires -> bullet trace
builds a usercmd every frame       ───▶  against every player WHERE THEY ARE NOW
sends it when 1000/cl_maxpackets          (no rewind: UO has no lag compensation)
ms have passed (and only on a frame)
                                   ◀───  sends a snapshot every 1000/snaps ms,
                                          but only on a server frame (sv_fps)
```

The server traces the bullet against where each player is **when the shooter's
packet arrives**. The shooter aimed at a picture of the world that was already
old when it was drawn, and older still by the time the shot lands. Every
millisecond of that difference is distance the target has moved, which is
why you lead moving targets, and lead more on a higher ping.

## How far you have to lead

```text
lead_ms ≈ ping + 1000/snaps_per_sec + 5 + ½ · 1000/packets_per_sec + cl_timenudge
```

| Term | Where it comes from | Stock UO | CoDUO+ 40-tick |
| --- | --- | --- | --- |
| `ping` | the network, both ways | ping | ping |
| `1000/snaps` | the client renders one snapshot interval behind the newest | 50 ms (snaps 20) | **25 ms** (snaps 40) |
| `5` | the client clock settles just behind the next snapshot | 5 | 5 |
| `½·1000/pps` | average wait of a command for the next outgoing packet | 20 ms (`cl_maxpackets 30` at 125 fps = 25 pps) | **4 ms** (125 pps) |
| `cl_timenudge` | added to the render time (-30…+30) | 0 | **−20** (spec allows −20…0) |
| **total** | | **ping + 75 ms** | **ping + 14 ms** |

At run speed (`g_speed` 190 u/s), 10 ms is about 1.9 units of target movement:

| Ping | Stock lead | CoDUO+ 40-tick lead |
| --- | --- | --- |
| 25 | 100 ms / 19 u | 39 ms / 7 u |
| 50 | 125 ms / 24 u | 64 ms / 12 u |
| 100 | 175 ms / 33 u | 114 ms / 22 u |
| 150 | 225 ms / 43 u | 164 ms / 31 u |

Settings remove about **60 ms** of lead at every ping. The ping itself is handled by
[lag compensation](#lag-compensation).

## What CoDUO+ changes

| Piece | Side | What it does |
| --- | --- | --- |
| snaps cap 30 → 40 | server | `SV_UserinfoChanged` clamps a client's `snaps` to 30 (`0x45bbf6`/`0x45bbfa`). Without it a 40 Hz server still sends at most 30, rounded down to 20 because snapshots only leave on server frames. |
| cl_maxpackets cap 100 → 125 | client | `CL_ReadyToSendPacket` clamp (`0x40cc26`); an over-cap value now resets to 125, not 100 (`0x40cc2b`). |
| exact frame cadence | client | The packet gate is `realtime − lastPacket ≥ 1000/cl_maxpackets`, checked once per frame. The frame limiter's exact 4.000 ms frames at `com_maxfps 250` (continuous clock) give exactly 125 pps; the stepped engine clock jitters 3/5 ms and drops packets. |
| late mouse sampling | client | movement made during the frame wait enters this frame's command, not the next (about one frame less input delay). |
| **competitive spec publisher** | server | `competitive.cfg` next to the server's `mss32.dll` is pushed to CoDUO+ clients through systeminfo and **applied while connected**: `snaps 40`, `cl_maxpackets 125`, `rate 90000`, … locked or clamped. Without it the raised caps do nothing, because clients keep UO's defaults (`snaps 20`, `cl_maxpackets 30`, `rate 5000`). |
| swing fix, server mirror | server | the pose a CoD:UO+ player sees is the pose the server traces against ([PORTING.md](PORTING.md#swing-fix-only-where-the-server-mirrors-it)). |

Vanilla clients on a CoDUO+ server are untouched. They still benefit from the
server's `sv_fps 40` if they set `snaps 40` themselves (the cap is lifted
server-side), but their own packet cap stays 100.

## Server setup

Run the server with CoDUO+: on Windows `mss32.dll` + `mss32_original.dll` next to
`CoDUOMP.exe`; on Linux `LD_PRELOAD=./coduoplus_sv.so ./coduo_lnxded …`
([LINUX.md](LINUX.md)). Then:

**`competitive.cfg`** (copy `competitive.cfg.example` from the release): the
client half, pushed automatically:

```text
snaps 40
cl_maxpackets 125
com_maxfps 125 250
rate 90000
cl_nodelta 0
cl_packetdup 1 2
cl_timenudge -20 0
```

**server config** (what the spec can't set, because it's the server's own):

```text
set sv_fps 40              // 25 ms server frames; snaps must equal it (see below)
set sv_maxRate 0           // no server-side cap on "rate" (or >= 90000)
set g_smoothClients 1      // default; keeps the negative timenudge extrapolating players
set sv_padPackets 0        // default; padding bytes count against the rate
set g_synchronousClients 0 // default; commands are run as they arrive
```

**Check it worked:** the server's `coduoplus.log` has
`competitive_sv: engine holds sv_competitive "snaps=40 cl_maxpackets=125 …"`. On a
CoDUO+ client connected to it, `snaps` and `cl_maxpackets` read 40 and 125 and
refuse changes ("is read only"). `cl_showTimeDelta 1` should show a steady delta.

**Keep `competitive.cfg` short.** It travels in the systeminfo string, which the
engine caps at 1024 characters and which a stock UO server already overflows
(see [LINUX.md](LINUX.md#systeminfo-is-full-on-stock-uo-servers); the same holds on
Windows). The example has only the four 40-tick lines active.

**Upload bandwidth:** worst case per client ≈ `snaps × 1548 B` = 62 kB/s at 40, so
20 players ≈ 1.2 MB/s (≈ 10 Mbit/s). A typical snapshot is much smaller (delta
compressed). The server CPU does twice the game frames and twice the snapshot
building of stock 20 Hz.

**Gear (optional, untested):** COD1.6X forces `g_useGear 0` on its servers. In
CoD1, the optional backpacks and belts it attaches are ignored by bullets, so
players aim at silhouette that cannot be hit. UO also registers `g_useGear`
(default 1, latched) and attaches gear when it is set, but whether UO's gear
is bullet-solid has not been verified. `set g_useGear 0` makes models match their
hit volume either way, at the cost of the look.

## Why sv_fps 40, and not more

`snaps` must **equal** `sv_fps`. Snapshots leave only on server frames, at
`now + 1000/snaps`. `snaps 30` on a 40 Hz server gives 20 (every second frame),
and `snaps 60` on a 40 Hz server gives 40. So the real question is the server
frame rate, and **40** is the sweet spot for UO:

| sv_fps | frame | Interpolation delay | Problem |
| --- | --- | --- | --- |
| 20 (stock) | 50 ms | 50 ms | the baseline |
| 30 | 33.3 ms | 33 ms | 1000/30 is not an integer: frames drift between 33 and 34 ms |
| **40** | **25 ms** | **25 ms** | divides 1000 exactly, and every 50 ms multiple (script waits of 0.05 s, the units of most GSC timing) lands on a frame. Worst case 62 kB/s fits under the 90000 rate ceiling. |
| 50 | 20 ms | 20 ms | 0.05 s waits are 2.5 frames, so timings round to 60 ms. Worst case 77 kB/s, close to the ceiling. Only 5 ms better than 40. |
| 60 | 16.7 ms | 17 ms | not an integer frame; worst case 93 kB/s **exceeds** the client `rate` ceiling (90000), so busy-fight snapshots get rate-delayed, which is worse than 40 |
| 100+ | ≤ 10 ms | ≤ 10 ms | 155 kB/s+: needs the engine's rate clamp patched and much more upload; server CPU ×5; game logic that counts frames (killcam buffers, `nextCachedSnapshotFrames`) changes length |

Beyond 40 the gain is 5–15 ms of interpolation, against real costs: rate-limited
snapshots, script-timing rounding, CPU, and untested frame-count assumptions.
40 also needs no further engine patch, since CoDUO+ lifts the snaps cap to
exactly 40. It is the tick COD1.6X's servers run. If you go higher, change
`snaps` to match and patch the snaps cap further, and expect to need
`sv_maxRate` / `rate` beyond 90000, which is an engine clamp.

What to watch at 40 (from the UO notes): killcam length (its cache is counted
in frames), any custom script that counts frames instead of using time, and
server CPU under a full server.

## Client setup

On a CoDUO+ server with `competitive.cfg`, the spec sets the netcode for you.
Otherwise, or on any server running `sv_fps 40`:

| Setting | Value | Why |
| --- | --- | --- |
| `com_maxfps` | **125** or **250** | frame periods dividing 8 ms → exactly 125 pps (200 fps gives 100) |
| `cl_maxpackets` | **125** | the CoDUO+ cap (vanilla 100) |
| `snaps` | **= server's sv_fps** | 40 on a 40 Hz server (needs a CoDUO+ server), 20 on stock |
| `rate` | **90000** | below it, busy-fight snapshots wait |
| `cl_packetdup` | 1 (2 on Wi-Fi / lossy) | a lost packet loses no input |
| `cl_timenudge` | −20 (−10…0 on high ping or jittery links) | less to lead; costs a little extrapolation |
| `cl_nodelta` | 0 | small snapshots |

CoDUO+'s frame limiter, late mouse sampling and `gpu_sync` (all on, imposed) take
care of the client's own share.

## Lag compensation

Everything above shrinks the *extra* delay to about 14 ms. The **ping** term
is a different problem: without help, a 100 ms player has to lead 100 ms of
movement, because UO's server traces bullets against where targets are *now*.
CoDUO+ servers compensate for it: the server rewinds every other player to
where the shooter saw them, traces, and puts them back. Clients need nothing;
vanilla players benefit too.

| Cvar (server) | Default | |
| --- | --- | --- |
| `g_antilag` | `1` | `0` off, `1` on, `2` on and log every compensated shot (`[coduoplus] antilag: client 3 shot ... 87 ms back, 2 player(s) moved, max 31.4 units`) |
| `g_antilagMaxMs` | `300` | the furthest back a shot is compensated. A 600 ms player is compensated 300 ms, so nobody gets shot "around a corner" from half a second ago |

`g_antilag` is in serverinfo, so server browsers show it. Both cvars change
live (`rcon g_antilag 0`). Their defaults come from `CODUOPLUS_ANTILAG` /
`CODUOPLUS_ANTILAG_MAXMS` on Linux ([LINUX.md](LINUX.md)) and `antilag` /
`antilag_max_ms` in `coduoplus.ini` on a Windows server
([CONFIGURATION.md](CONFIGURATION.md)).

How it works (`src/shared/antilag_core.cpp`, the same code in the Windows mod and
the Linux library):

1. after every server frame (`G_RunFrame`), record each live player's
   `r.currentOrigin` and `r.currentAngles` with the level time: 128 frames,
   3.2 s at 40 Hz;
2. on every root bullet call (Linux: `Bullet_Fire`, which every weapon, turret
   and vehicle gun goes through; Windows: the five root calls of
   `Bullet_Fire_Extended`), move every other live player to where they were at
   the shooter's `usercmd.serverTime` (the time its client was drawing),
   interpolated between the two recorded frames around it and capped by
   `g_antilagMaxMs`. Relink them, run the shot (penetration, damage, kills),
   then restore every moved player exactly.

Why origin and angles are enough to move the hit boxes: the server places the
posed skeleton in the world from exactly those two fields
(`G_DObjGetWorldBoneIndexMatrix`). **Limitation:** the *pose* is the current
frame's, so a stance change (crouch/prone) or lean inside the rewind window is
tested in its new pose at the old position. Rewinding the animation state is
the next step.

With lag compensation on, players should keep `cl_timenudge` at 0 or slightly
negative only: the rewind targets the moment the client drew, and a large
negative nudge draws an extrapolated moment that never existed on the server.
