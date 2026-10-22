# Porting COD1.6X to CoD:UO — how it was done, what was decided, and why

CoDUO+ is a port of [COD1.6X](https://github.com/cod1plus/client) (`cod1plus/client`,
`main` at `6f8fec0`, release **1.6.6**, plus every non-PunkBuster change of
`release/1.6.9` — see [Upstream 1.6.8 and 1.6.9](#upstream-168-and-169)) from Call of Duty 1.5 (`CoDMP.exe`) to
Call of Duty: United Offensive 1.51 (`CoDUOMP.exe`). This document covers the
method, every decision that departs from "same code, new addresses", and the
status of each feature. [ADDRESSES.md](ADDRESSES.md) is the full address map,
[ARCHITECTURE.md](ARCHITECTURE.md) how the mod runs inside the game, and
[TESTING.md](TESTING.md) how all of this is verified.

## Contents

1. [Why a port is mostly re-finding addresses](#why-a-port-is-mostly-re-finding-addresses)
2. [Method](#method)
3. [Safety rules the port adds](#safety-rules-the-port-adds)
4. [Feature status](#feature-status)
5. [Decisions, one by one](#decisions-one-by-one)
6. [What is left, and how to continue](#what-is-left-and-how-to-continue)

---

## Why a port is mostly re-finding addresses

COD1.6X is not a source mod. The game is closed-source 2003-era C, so every
change is a patch to the **running** program: overwrite a constant, repoint a
`call`, swap a pointer in the import table. Each patch names a precise address
in `CoDMP.exe`, `cgame_mp_x86.dll` or `game_mp_x86.dll`, and that address only
means something in that exact build.

CoD:UO is built from the same Quake 3–derived engine, extended. Most of the
code COD1.6X touches exists in UO, usually *identical* instruction for
instruction, but the linker put it elsewhere: new functions in front shift
everything after them. So the bulk of the port is answering one question per
patch site: **where is that same instruction in UO, and is it really the same?**

A few sites are not the same, and those are where the decisions below come from.

## Method

### 1. Inventory

Every hardcoded address in upstream's `src/` was listed: 96 named constants plus
inline literals across 38 files. Each was classified:

- **engine** (`CoDMP.exe`): cvar API, version, protocol, rate caps, frame
  limiter, mouse, refresh cap, systeminfo;
- **cgame** (`cgame_mp_x86.dll`): FOV, widescreen, swing, stance, lean, 2D hooks;
- **game** (`game_mp_x86.dll`): version gate, antilag, viewheight;
- **none**: modules that only talk to Win32/OpenGL (performance, gamma, the GL
  overlay menu, display probing, updater, Discord). These port by renaming
  things, not by RE.

### 2. Re-find each site by its meaning, not its bytes

Searching UO for a CoD1 byte pattern is unreliable: operands differ, and a short
pattern matches by coincidence. Each site was found through what it *does*:

- **Strings it uses.** `shortversion`, `r_fullscreen`, `com_maxfps`, `snaps`,
  `cl_maxpackets`, `"getservers %s"`, `"coduomaster.activision.com"`,
  `"GAME_INVALIDPASSWORD"`. The code that pushes a string's address is the code
  that registers or uses that cvar.
- **Neighbouring constants.** The legs swing tolerance is the `push 40.0` that
  follows `push 150.0`. The FOV clamp is the `fcomp` against `80.0f` reading
  `cg_fov.value`. Screen scale is `fild [x] ; fmul [1/640]`.
- **Call graph.** `CG_GetEffectiveFov` must have exactly two callers, followed by
  the `fild/fptan/fpatan` vertical-FOV math. ClientConnect has exactly one
  caller, inside the exported `vmMain`.
- **Side-by-side disassembly** of the CoD1 function and the UO candidate (MinGW
  `objdump` via `tools/re/dis.sh`, and the Ghidra exports in
  `coduo-1.6/analysis/`).

The helpers are in `tools/re/`: `pe.py` (PE mapping), `find.py` (string / imm32
xref / `call` search), `dis.sh` (objdump wrapper), `q3query.py` / `q3connect.py`
/ `q3huff.py` (talking to a live server).

### 3. Write the evidence down as a test

Each found address goes into `tests/site_check.cpp`. The test does more than
"the byte we overwrite is what we expect". It pins the address to its
**meaning**: the string pushed next to it, the call target, the global the code
reads, how many readers that global has. A byte match can be a coincidence;
four facts about the surrounding code are not. A one-byte mutation of a constant
is caught (tried).

### 4. Switch the feature on

Every feature group has a switch in `src/core/game.h` (`port::ENGINE`,
`port::CGAME_FOV`, …). A group is switched on only after its checks exist and
pass, and `site_check` fails if an enabled group has no checks. Until then the
group's code is compiled but never applied.

### 5. Run it

`tests/smoke_dedicated.py` starts the real game with the mod as a dedicated
server, queries it over UDP, performs real connect handshakes, and reads the
mod's log for every patch's "applied" line and the absence of any refusal. See
[TESTING.md](TESTING.md).

One feature per git branch (`port/engine`, `port/cgame-fov`, …), merged into
`main` once `ctest` passes.

## Safety rules the port adds

Upstream already verifies the bytes before most writes. The port tightens three
things that would otherwise go wrong on UO.

**A real build check instead of "exe is at 0x400000".** Upstream's guard was
`GetModuleHandle(NULL) == 0x400000`, which every 32-bit exe of that era passes,
including other UO patches. `game_exe_ok()` checks the PE link timestamp and the
`"1.51"` literal at its known address. Every engine call goes through it,
including the menus' cvar reads, which upstream left unguarded.

**Verify all sites before writing any.** The protocol patch used to check one of
its five bytes and then write all of them. It now checks all six, because half a
protocol change is worse than none. The frame limiter checks the call target
*and* the loop branch before touching either.

**Never assume a module's base.** `uo_game` was observed loaded at `0x241f0000`
on a dedicated server. All module patches work from `base + RVA`, and operand
checks compare against relocated values.

## Feature status

| Feature (upstream module) | Status | Notes |
| --- | --- | --- |
| mss32.dll proxy (`mss32_proxy.def`) | ✅ ported | All 77 `mss32.dll` imports of `CoDUOMP.exe` are forwarded (checked). |
| Build identity, names, paths (`core/game.*`) | ✅ new | UO module names, window class, `uo\uoconfig_mp.cfg`. |
| Version string in menus (`version_patch`) | ✅ ported | Shows `CoDUO+ 0.1.0`. |
| Protocol / master / browser (`protocol_patch`) | ✅ ported, **vanilla values** | [Decision](#protocol-and-master-server-stay-vanilla). |
| 40-tick caps: snaps 40, cl_maxpackets 125 (`competitive`) | ✅ ported | Client applies a server-pushed spec (lock / clamp). An over-cap `cl_maxpackets` resets to 125 (vanilla reset it to 100). |
| Linux dedicated server (`server/linux/coduoplus_sv.so`, new) | ✅ new | Every server-side feature (snaps 40, shortversion, swing mirror, version gate, spec publisher) for `coduo_lnxded` 1.51b. [LINUX.md](LINUX.md). |
| Server publisher of the competitive spec (`competitive_sv`, new) | ✅ new | Upstream's lives in its Linux server module; a UO server had none, so clients never got snaps 40 / 125 pps. Reads `competitive.cfg` next to the server's DLL. [NETCODE.md](NETCODE.md). |
| Imposed ini keys (`ini_lock`, 1.6.9) | ✅ ported | [Decision](#ini-lock-imposed-settings). |
| Session report (`session_report`, 1.6.9) | ✅ ported | One `session` line a minute in the log. |
| PunkBuster-replacement ruleset + cheat-cvar scan (`ruleset*`, `cheat_scan`) | 🗑️ removed | [Why](#punkbuster-code-removed). |
| Frame limiter (`frame_limiter`) | ✅ ported, **reworked**, 1.6.9 | Continuous engine-epoch clock and late mouse from 1.6.9. [Decision](#frame-limiter). |
| 1 ms timer, CPU affinity, priority, working set, FSO (`performance/*`) | ✅ ported | Win32 only. 1.6.9: the timer request survives an occluded window (Windows 11), working-set sizes no longer wrap in 32 bits. |
| GPU sync: `glFinish` after `SwapBuffers` (`gpu_sync`, 1.6.9) | ✅ ported | Chains with the overlay's `SwapBuffers` hook. |
| Raw mouse input `m_rinput` (`rinput`) | ✅ ported | Same two `GetCursorPos` callers. |
| Refresh-rate cap 200 → 1000 Hz (`settings_menu`) | ✅ ported | |
| Display probe, mode guard, fullscreen default (`video/*`) | ✅ ported, 1.6.9 | Now reads/writes `uo\uoconfig_mp.cfg`. 1.6.9: a `r_fullscreen 1` the player chose is never rewritten to 0. |
| Borderless window (`window_patch`) | ✅ ported, **fixed**, 1.6.9 | [Decision](#window-patch-only-touches-the-game-window). 1.6.9: ignores a minimized window; in exclusive fullscreen it only subclasses when `window_minimize_on_focus_loss` is on. |
| Per-monitor gamma (`gamma_fix`) | ✅ ported | GDI only. |
| Single-instance guard (`single_instance`) | ✅ ported | UO window class. |
| Ctrl+M overlay menu (`ui/*`) | ✅ ported | Paths and cvar reads point at UO. |
| Main-menu "SETTINGS" button (`ui_pk3`) | ✅ ported, **generated** | [Decision](#the-ui-pk3-is-generated-from-the-players-own-files). |
| cg_fov unlock 80–95 (`settings_menu`) | ✅ ported | [Note](#fov-unlock-is-ported-as-is--read-this). |
| Hor+ / stretched FOV (`widescreen_fix`) | ✅ ported | Off by default (`view_mode = classic`). |
| Swing fix: torso/legs follow the view (`swing_fix`) | ✅ ported, **client + server** | [Decision](#swing-fix-only-where-the-server-mirrors-it). |
| Stance blend hold (`stance_fix`) | ✅ ported (off) | Upstream ships it disabled since 1.6.1; same here. |
| Server version gate (`version_gate`) | ✅ ported, **fixed** | Both uo_game builds. Survives map changes (upstream's didn't). Vanilla players allowed. |
| Lean fix / controller smoothing (`lean_fix`, `shared/lean_controllers`) | ❌ not ported | [Why](#lean-fix). |
| Viewheight lerp speed (`viewheight_fix`) | ❌ not ported | [Why](#viewheight-fix). |
| Lag compensation (`antilag`, `shared/antilag_core`) | ✅ **new**, client-free, Windows + Linux servers | Upstream's was a CoD1-only experiment shipped disabled; rewritten for UO. Origin + angles, not pose: [NETCODE.md](NETCODE.md#lag-compensation). |
| CG_Draw2D hook: Discord in-match heartbeat, avatar overlay (`engine_2d`, `avatar_overlay`) | ❌ not ported | Discord does nothing without an application id; the avatar overlay is an off-by-default proof of concept. |
| Auto-updater (`updater`) | ✅ ported, CoDUO+'s own feed | [Decision](#updater-feed-coduos-own-never-upstreams). |
| PAM installer (`pam_install`) | ✅ ported, **off** | Same. |
| Discord Rich Presence (`discord_rpc`) | ✅ ported (idle) | Needs `discord_rpc_client_id`. |
| Demo upload (`demo_upload`) | ✅ ported (off) | Off by default upstream; looks in `uo\demos`. |

Not ported features stay compiled, gated off by their `port::` switch, so
nothing of upstream is lost and each can be continued in isolation.

## Decisions, one by one

### Protocol and master server stay vanilla

**Upstream** moves CoD1 from network protocol 6 to 10 and repoints the master
server to its own (`87.106.7.52`). COD1.6X clients then only see and join
COD1.6X servers. That separate ecosystem is what lets upstream change things
both sides must agree on (hitboxes, lean geometry).

**UO has no such ecosystem.** That master lists CoD1 servers only. A CoDUO+
client on protocol 23 would see an empty browser, and every existing UO server
would refuse it as a different version.

**So:** the machinery is ported and verified (six protocol sites, master slot,
netadr cache, `getservers` format, browser filter), but it applies UO's own
values: protocol 22 and `coduomaster.activision.com` (probed 2026-10-17: about
111 servers). CoDUO+ players use the normal browser and the normal servers.
To run a separate CoDUO+ ecosystem later, change `g_protocol_version` and
`g_master_host` in `netcode/protocol_patch.cpp` to a new protocol and a master
that serves it. Everything else is in place.

### Frame limiter

**Upstream** replaces one `call` inside `Com_Frame`'s wait loop with a QPC-based
wait, and in CoD1 relies on `Com_Frame`'s *second* `Com_EventLoop` (after the
loop) to process events.

**In UO the patched call is also `Com_EventLoop`**, in the stock Quake 3 loop:

```text
do {
    com_frameTime = Com_EventLoop();     // 0x43c6a7  <- the patched call
    if (lastTime > com_frameTime) lastTime = com_frameTime;
    msec = com_frameTime - lastTime;
} while (msec < minMsec);                // 0x43c6c9  jl
```

`Com_EventLoop` drains every key, mouse, console and network event. UO also has
the second call (`0x43c899`), but it is **skipped when `dedicated` is set**. On a
server the in-loop call is the only event drain, so a pure wait would starve it.

**So the UO replacement does each frame, in one call:**

1. **Wait** for the QPC deadline (upstream's exact-rate maths: carried
   remainder, deadline anchoring, resync after hitches).
2. **Late mouse** (upstream 1.6.9). The engine reads the mouse in `IN_Frame`,
   *before* the wait, so movement made during the wait used to reach only the
   next frame. Once the wait is over, `IN_MouseMove` (`0x469c20`) runs again,
   behind `IN_Frame`'s own gate (mouse initialised and active). `input_late_sampling`
   in the ini switches it off.
3. **Drain events**: the real `Com_EventLoop`. UO has no separate
   `Sys_SendKeyEvents`; the PeekMessage pump is inlined in `Sys_GetEvent`, which
   `Com_EventLoop` calls. This one call therefore pumps keys and buttons late
   too, and delivers the late mouse event into *this* frame's command.
4. **Report** a clock that follows upstream 1.6.9's three rules:
   - **the engine's epoch**, because key-event stamps are subtracted from it
     (`CL_KeyState`). CoD1 reported QPC-since-boot until 1.6.9, which quantised
     keyboard movement to whole frames;
   - **real time, never the deadline**, which upstream learned through flickering
     remote models;
   - **continuous.** It's a QPC clock calibrated onto `Sys_Milliseconds`'
     epoch (`sys_timeBase 0x489bc3c`), re-checked every 1024 frames. The
     stepped `Sys_Milliseconds` gives whole-ms deltas of 3 and 5 at an exact
     4 ms period; measured with `tests/test_frame_limiter.cpp`, it gets only 61%
     of 250 fps frames at exactly 4 ms, against 100% for the continuous clock.

The loop's `jl` is removed (`7c dc` → `90 90`), so the engine's integer-ms gate
(the cause of "250 shows 240–248") can't re-loop. That's also why upstream
1.6.9's "doubled frame after a late wake" fix has nothing to do in UO: there's
no second call to double. A 1 ms floor keeps vanilla's minimum frame time above
1000 fps.

With `com_maxfps 0` or on a dedicated server (the engine's own condition for
applying `com_maxfps`), the replacement reproduces the original loop exactly:
`minMsec` included, the engine's own clock, no late mouse.

### Window patch only touches the game window

Upstream finds "the game window" as any visible top-level window of the process
at least 200×200. On a dedicated server that's the WinConsole, and the first
smoke run turned the server console into a borderless full-monitor window. The
search now also requires the renderer's window class. This is an upstream bug,
fixed here.

### The UI pk3 is generated from the player's own files

Upstream's `zz_cod1x_ui.pk3` carries CoD1's `main.menu`, `menus.txt` and
`joinserver.menu`, edited. A pk3 file replaces the whole file, so in UO those
would replace UO's menus, and CoD1's `menus.txt` does not even list UO's.

`ui_pk3/build_pk3.py` takes `ui_mp/main.menu` from the install's
`uo/pakuo03.pk3`, checks it is byte-for-byte the stock 1.51 file, and inserts
upstream's settings item (relabelled "CoDUO+ Settings") at y=400, between QUIT
and AUTO-UPDATE. That slot is empty in UO's layout as in CoD1's. Nothing of
Activision's is kept in the repository. `joinserver.menu`'s 1.6/1.5 list
buttons are dropped, because they do nothing while the protocol is vanilla.

The DLL now builds this pk3 itself, on the player's machine, the same way
(`features/ui_pk3.cpp`), so players don't copy it and releases don't carry it.
`build_pk3.py` is kept as the reference the C++ output is tested against
([RELEASES.md](RELEASES.md#the-main-menu-entry-built-by-the-dll)).

### Updater feed: CoDUO+'s own, never upstream's

Upstream's manifest URL publishes the CoD1 `mss32.dll`. Fetching it on UO would
replace CoDUO+ with a build that hooks CoD1 addresses. So the updater's built-in
feed is CoDUO+'s own release feed,
`https://github.com/coduoplus/coduoplus/releases/latest/download/manifest.json`,
written by the release workflow ([RELEASES.md](RELEASES.md#updates)). Unlike
upstream, it is **off by default** (`updater_enable`, commented out in the
shipped ini): the player opts in. Up to 0.3.0 the default URL was empty, which
meant an updater that never ran even when enabled. An empty
`updater_manifest_url` in the ini keeps the built-in feed; a set one overrides
it. On a dedicated server the update dialog is skipped and only logged.

The PAM installer fetches CoD1's rPAM, so it stays off with an empty
`pam_manifest_url` until there is a UO feed.

### FOV unlock is ported as is — read this

Upstream unlocks `cg_fov` (normally cheat-protected at 80) to 80–95, and its own
servers allow the same range. That's fair inside the COD1.6X ecosystem, where
every client has it.

On UO servers, players without the mod stay at 80. **A CoDUO+ player at FOV 95
on a vanilla server sees more than everyone else, and a server or league that
checks `cg_fov` (PunkBuster rules) may kick for it.** It's ported as is, because
that's the upstream feature. The compiled switch is
`SettingsMenuConfig::fov_unlock` in `features/settings_menu.h`. The ini key
`fov_unlock` is **not read** (upstream stopped reading it in 1.6.5 so players
can't flip it). Revisit this before a public release; the natural UO version
is the same server opt-in the swing fix uses.

### Swing fix only where the server mirrors it

The swing fix changes four constants in `BG_PlayerAngles`, so the drawn torso
follows the view with no lag. That function doesn't just draw. The server
builds the skeleton its **bullet traces test** from the same function. Upstream
measured up to ~10 units between what is drawn and what is tested when only the
client is patched ("THE TWO SIDES MUST STAY IN STEP"), and patches its own
server to match (`cod1plushookserver/src/swing_sync.c`).

Every UO server is vanilla. A client-only port would make CoDUO+ players aim at
a torso that isn't where the server tests hits. So:

- **Server half.** A process hosting a server (dedicated or listen) applies the
  same four values to its `uo_game_mp_x86.dll`, which contains the same bg_ code
  at the same RVAs (the test checks the two copies are byte-identical apart from
  relocated globals). It then publishes the SYSTEMINFO|ROM cvar
  `sv_coduoplus_swing 1`. This is reapplied every tick, so map-change reloads
  are covered.
- **Client half.** The cgame gets the fix only while the server it's connected
  to publishes that flag, read from the live systeminfo configstring rather
  than the cvar (which would keep a previous server's value). On any other
  server the vanilla values are back within 5 ms.

Result: what a CoDUO+ player sees always matches what the server tests, on
every server.

### Lean fix

Upstream's lean fix hooks `BG_Player_DoControllersInternal` (the function that
turns animation state into bone controllers) and adds a lateral body shift and
back-bone reprojection, from one shared file compiled into both client and
server so the drawn and tested poses agree.

In UO this function **changed**, not just moved:

- **Calling convention.** CoD1 passes output buffer / clientInfo / entityState
  in `eax` / `ecx` / `ebx`. UO passes clientInfo in `eax`, entityState on the
  stack and the output in `esi`. Upstream's naked-asm trampoline is
  CoD1-specific.
- **Body.** UO adds a whole branch on `eFlags & 0x100000` (likely vehicles or
  mounted weapons) reading new clientInfo fields `+0x3f4..+0x3fc`.
- **Structures.** `entityState.clientNum` moved from `+0x90` to `+0x94`, and
  clientInfo grew (the stance hold field went from `+0x4a4` to `+0x4c0`). The
  crouch bit in `eFlags` and the engine's own lateral constant, both measured on
  live CoD1 servers upstream, would need re-measuring on UO.

On top of the RE, the fix only works with a matching server-side hitbox
(upstream's `lean_hitbox.c`, `pose_sync.c`, `hitbox_fix.c`), calibrated over
many releases with live probes on real servers ("leaning heads silently stop
registering" when the two disagree). Shipping a client lean change without
that, on vanilla servers, would recreate the exact desync upstream spent 1.6.1
to 1.6.13 fixing. It stays off (`port::CGAME_LEAN = false`). The UO locations
found so far (`0x30004da0`, call site `0x30005741`) are in
[ADDRESSES.md](ADDRESSES.md).

### Viewheight fix

Upstream patches the crouch view-height lerp speed (180 → 150) in
**`gamex86.dll`**. That's the *single-player* game module, which `CoDMP.exe`
never loads. The RVA it uses only holds 180.0f in the SP DLL. So in CoD1
multiplayer this patch has never run.

Porting it "for real" to `uo_game` would introduce a movement change upstream
never shipped. Because pmove runs on both sides, client prediction and server
would also disagree unless both were patched. Not ported; noted in
`core/main.cpp`.

### Ini lock (imposed settings)

Upstream 1.6.9 rewrites seven ini keys at every launch and locks `r_fullscreen`
read-only at 1 (`core/ini_lock.cpp`, upstream `b2bb8f4`). It's ported as is,
including 1.6.10's `COD1RELOADED_DEV_UNLOCK` test build. Two things in this tree
change behaviour because of it, exactly as in upstream 1.6.10, which carries
both the lock and the 1.6.6-era display code:

- the **borderless window** (`window_patch`) and the Ctrl+M overlay's
  "Borderless" mode can no longer take effect;
- `refresh_rate = max` activates 1.6.6's `max_hz_native_res`, which was on by
  default but idle while the shipped ini said `refresh_rate = 0`: a custom
  resolution that is only listed below the panel's maximum Hz is swapped for
  one that reaches it, and the 4:3 stretch is done by the mod.

Upstream enforces the lock through its server refusing older builds. A UO
server running CoDUO+ does the same through the version gate. On vanilla
servers it's a client-side default players can't edit. `tests/test_ini_lock.cpp`
runs the rewrite over the shipped `coduoplus.ini`.

### PunkBuster code removed

Upstream carries a client-side replacement for PunkBuster: `ruleset*` parses
PB's `pbsv.cfg` rule format, embeds the CoDBase PunkBuster cvar list (~430
rules, `rulesets/codbase-2023-05.pb`), downloads newer lists from
`cod1plus/rulesets`, enforces them while a server names a list in
`sv_competitive_ruleset`, and reports in userinfo `cod1x_rs`. `cheat_scan` is
PB's other half, a blocklist of cheat cvar names reported in `cod1x_ac`.

All of it was removed from CoDUO+ (files, ini keys, watcher ticks, the embedded
list). The server-pushed **competitive spec** (`sv_competitive*` → exact values
ROM-locked, ranges clamped, `netcode/competitive.cpp`) stays: it isn't
PunkBuster code, and the small numeric compare/clamp it borrowed from the ruleset
now lives in `competitive.cpp` itself, with the same rules (`""` counts as 0,
magnitude-scaled tolerance, integers printed without a decimal point).

What remains that mentions PunkBuster is the engine's own switch:
`smoke_dedicated.py` starts its test server with `+set sv_punkbuster 0`, which
turns the game's PunkBuster off.

### Upstream 1.6.8 and 1.6.9

Upstream's history forks after 1.6.5. **1.6.6** (this port's base) was later
*withdrawn* upstream; **1.6.7** re-released the 1.6.5 code; **1.6.8** is 1.6.7
plus only the PunkBuster-replacement cvar ruleset; **1.6.9** continues 1.6.8.
Taking 1.6.8 or 1.6.9 literally would therefore *remove* 1.6.6 features
(overlay menu, PAM installer, display probe, single-instance guard, refresh cap,
gamma work) and add PunkBuster code. Instead, CoDUO+ 0.2.0 keeps 1.6.6 and ports
every **new, non-PunkBuster** commit of 1.6.8..1.6.9:

| Upstream commit | What | In CoDUO+ |
| --- | --- | --- |
| `bbe6faf` (1.6.8) | PunkBuster-replacement ruleset | skipped (PunkBuster) |
| `871eadd`, `12d5cd4`, `da88414`, `3f04efd` | ruleset list syncs, generator | skipped (PunkBuster) |
| `a9dd4a2` | limiter on the engine clock, no doubled frame; timer kept when occluded | engine-epoch + no-doubled-frame were already true of the UO wait; **continuous clock** and the **Windows 11 timer opt-out** ported |
| `c8b4643` | working-set sizes in 64 bits | ported as is |
| `e1161b7` | exclusive fullscreen, mode guard, 1000 Hz cap | mostly already in 1.6.6; **one-direction fullscreen repair** and **window_patch refinements** ported |
| `041e3f8` | gpu_sync | ported, chained on the overlay's SwapBuffers hook |
| `809a439` | late input sampling, session report | **late mouse** ported (keys were already pumped late in UO); **session report** ported |
| `b2bb8f4`, `3787d88` | ini lock, gpu_sync auto | ported (+ 1.6.10's dev-unlock build option) |
| `9edaa29` | version bump | — |

### Version numbering

CoDUO+ started at **0.1.0** (1.6.6 port); **0.2.0** adds the 1.6.9 work. `cod1x_build` (sent in userinfo) is derived from the
version, so 0.1.0 → 100 can never be confused with a COD1.6X build (1.6.x →
106xx). The upstream base version is kept in
`COD1RELOADED_UPSTREAM_VERSION = "1.6.6+1.6.9"`.

### Internal names kept

`cod1x_*` cvars, `COD1RELOADED_*` macros and the `CODMP_*` constant names are
kept. Menus and the server spec depend on the cvar names, and keeping the
code names makes a future merge of upstream changes a matter of resolving
values, not renames. Everything a player sees says CoDUO+ (`coduoplus.ini`,
`coduoplus.log`, dialogs, version string, userinfo key `coduoplus`).

## What is left, and how to continue

In order of value:

1. **A client run.** The dedicated-server smoke test covers the engine and
   server side, but not the renderer, input or cgame (FOV, widescreen, swing
   client half, overlay menu, raw input, frame rate, late mouse, gpu_sync,
   session line). The checklist is in [TESTING.md](TESTING.md#manual-client-checklist).
2. **FOV policy** (above).
3. **Upstream `release/1.6.10`** and `test/lean-v7-cod2eq` (1.6.8 and 1.6.9 are
   done, see above): Discord presence with join, a demos tab, HD-texture
   packages, a menu hotkey option, hitbox overlays, bone probes. Most are UI/menu
   work that would port the same way; the hitbox work depends on the lean fix.
4. **Lean fix**, as a client+server pair, following the swing-fix pattern
   (shared code, server publishes, client follows).

To port another site: find it by meaning (strings, neighbours, call graph),
add its checks to `site_check.cpp`, switch its `port::` flag on, run `ctest`,
then the smoke test and, for client features, the client checklist.
