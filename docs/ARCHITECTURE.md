# How CoDUO+ runs inside the game

How the DLL gets into the game, what runs when, how patches are applied and
kept applied, and how the client and a server agree on the patches that affect
both. For *where* each patch lands, see [ADDRESSES.md](ADDRESSES.md); for *why*
the UO port differs from upstream, see [PORTING.md](PORTING.md).

## Contents

1. [Getting loaded: the mss32.dll proxy](#getting-loaded-the-mss32dll-proxy)
2. [Startup sequence](#startup-sequence)
3. [The watcher thread](#the-watcher-thread)
4. [Module lifecycle: cgame and game come and go](#module-lifecycle-cgame-and-game-come-and-go)
5. [How a patch is applied safely](#how-a-patch-is-applied-safely)
6. [Talking to the engine](#talking-to-the-engine)
7. [Client ↔ server agreement through systeminfo](#client--server-agreement-through-systeminfo)
8. [Source layout](#source-layout)

---

## Getting loaded: the mss32.dll proxy

`CoDUOMP.exe` imports 77 functions from `mss32.dll`, the Miles Sound System. The
Windows loader resolves imports by searching the exe's folder first. CoDUO+
builds its own `mss32.dll`:

```text
CoDUOMP.exe ──imports──▶ mss32.dll (CoDUO+)  ──forwards every export──▶ mss32_original.dll (Miles)
                              │
                              └─ DllMain: start the mod
```

`src/mss32_proxy.def` declares each Miles export as a *forwarder*
(`_AIL_startup@0=mss32_original._AIL_startup@0`). The loader resolves those
straight to the real Miles DLL, so audio calls never pass through our code.
The player renames the original to `mss32_original.dll`, which is why install
step 1 matters: without it the forwarders point nowhere and the game won't start.

Advantages of this route: no launcher, no injector, and nothing on disk is
modified. Every patch lives in memory and vanishes when the game exits.
The same DLL is also built unproxied as `coduoplus.dll` for manual injection
while debugging.

## Startup sequence

`DllMain(DLL_PROCESS_ATTACH)` (`src/core/main.cpp`) runs inside the loader,
before `CoDUOMP.exe`'s own `WinMain`. At that point the engine hasn't
initialised anything, so only work that touches no engine state happens here:

1. **Single-instance guard.** A second copy of the game from the same folder is
   refused (except the engine's own `monkey` relaunch).
2. **Logger, config.** `coduoplus.log` (the previous run's is kept as
   `coduoplus.prev.log`) and `coduoplus.ini`.
3. **Process-wide tuning.** 1 ms timer, CPU affinity, priority, working-set
   lock, FSO registry tweak.
4. **Static engine patches.** These only rewrite already-mapped code and data
   of `CoDUOMP.exe`, so they're safe this early, and they *must* run this early:
   - shortversion default pointer (before `Com_Init` registers the cvar);
   - protocol / master / browser bytes;
   - snaps and cl_maxpackets caps;
   - r_displayRefresh cap;
   - r_fullscreen default (only if `fullscreen = off`);
   - frame-limiter call and loop branch;
   - IAT hooks: `GetCursorPos` (raw input), `SwapBuffers` (overlay),
     `SetDeviceGammaRamp` (gamma), `ChangeDisplaySettingsA` (mode guard).
5. **Display probe.** Reads `uo\uoconfig_mp.cfg` and fixes contradictions with
   the ini before the renderer starts (e.g. a resolution a window can't show).
6. **Start the watcher thread.**

Every engine patch first asks `game_exe_ok()`: is this `CoDUOMP.exe` 1.51,
judged by PE timestamp and the `"1.51"` literal? On any other exe the mod
loads, logs, and patches nothing.

## The watcher thread

One thread, `patch_watcher_thread`, loops every 5 ms for the life of the
process. Work that needs the engine running or a module loaded happens here:

| Tick | What it does |
| --- | --- |
| once the cvar system is up (`cvar count > 0`) | register the mod's cvars (userinfo `coduoplus` / `cod1x_build`, `m_rinput`, `cod1x_*` menu cvars); enforce a server's competitive spec / ruleset; follow `m_rinput`; follow the master-list toggle |
| `swing_server_tick` | if `uo_game` is loaded (we are a server): keep the swing mirror applied, publish `sv_coduoplus_swing` |
| `version_gate_tick` | if `uo_game` is loaded: keep the ClientConnect gate installed |
| `swing_client_tick` | if `uo_cgame` is loaded: swing on/off according to the current server's systeminfo |
| `widescreen_update_stretch` | feed the stretched-mode aspect, install FOV hooks if the view mode changed live |
| `gamma_fix_tick` | per-monitor gamma on focus/monitor changes |
| `monitor_cgame` | apply cgame patches to a newly loaded or reloaded `uo_cgame` |

Every tick is idempotent and cheap when nothing changed: it reads a few bytes,
sees they're already in the wanted state, and returns.

## Module lifecycle: cgame and game come and go

The engine loads `uo_cgame_mp_x86.dll` when a client enters a map and
`uo_game_mp_x86.dll` when it hosts one, and **unloads and reloads both on every
map change**. A reloaded module has its original bytes again, so every patch
must be reapplied, often at the same base address.

- **cgame:** `monitor_cgame` notices a new module handle *or* a sentinel: a
  patched site that reads vanilla again, e.g. the `cg_fov` flags back at `0x201`.
  It must act within milliseconds of the load, before `CG_Init` runs.
- **game:** the server-side ticks simply re-check their sites every tick.

Module bases are never assumed. `uo_game` was seen loaded at `0x241f0000` and
`0x24140000` instead of `0x20000000`. Every patch computes `base + RVA` from
the live module handle, and compares operands against relocated values.

## How a patch is applied safely

Every write follows the same pattern:

1. **Identify.** Is this the build the address came from? For the exe:
   `game_exe_ok()`. For uo_game's version gate: PE timestamp looked up in
   `VERSION_GATE_BUILDS`.
2. **Verify.** Does the site hold *exactly* the vanilla bytes, or exactly our
   bytes (a re-run)? Opcodes are checked as well as operands: a `push imm32` is
   `68`, a `call rel32` is `E8` *and* goes to the expected function. If a
   feature has several sites, all of them are verified before any is written.
3. **Write** through `VirtualProtect(PAGE_EXECUTE_READWRITE)` and back, then
   `FlushInstructionCache`.
4. **Log** the outcome once per change, never per tick.

Anything unexpected means *don't write*: a skipped feature is a log line, a
wrong write is a crash or a silent gameplay change.

## Talking to the engine

The mod calls a handful of engine functions directly through their addresses
(`src/netcode/protocol_patch.h`, `competitive.h`, `settings_menu.h`):

| Function | Used for |
| --- | --- |
| `Cvar_Get(name, default, flags)` | registering mod cvars; flags `0x02` USERINFO, `0x08` SYSTEMINFO, `0x40` ROM |
| `Cvar_FindVar(name)` | reading any cvar (`cvar_t`: string `+4`, flags `+0x10`, value `+0x1c`, integer `+0x20`) |
| `Cvar_Set(name, value)` | `Cvar_Set2(…, force=1)`: bypasses ROM/CHEAT, which is how the mod can lock a cvar to a value the player can't change |
| `Cbuf_ExecuteText(2, text)` | queue console commands (`vid_restart`, `writeconfig`, …) to run on the main thread |

They're called from the watcher thread, as upstream does. The engine is
single-threaded but these routines touch no state its main loop holds across
calls. Anything that must run *on* the main thread goes through
`Cbuf_ExecuteText`.

## Client ↔ server agreement through systeminfo

Some patches change what the **server tests**, not just what the client draws.
The swing fix changes the pose bullets are traced against. Such a patch is only
correct when both sides apply it. CoDUO+ uses the engine's own channel for
server→client configuration: **SYSTEMINFO cvars**. A cvar registered with flag
`0x08` on the server is copied into the `CS_SYSTEMINFO` configstring, and every
client receives it with the gamestate. That's how `sv_cheats` reaches clients.

```text
server (CoDUOMP.exe + CoDUO+)                      client (CoDUOMP.exe + CoDUO+)
─────────────────────────────                      ─────────────────────────────
uo_game loaded  ─▶ swing_set(uo_game, ON)
                ─▶ Cvar_Get("sv_coduoplus_swing", SYSTEMINFO|ROM) = "1"
                         │
                   CS_SYSTEMINFO  "\…\sv_coduoplus_swing\1\…"
                         └────────── gamestate ──────────▶ systeminfo_value("sv_coduoplus_swing") == "1"
                                                           ─▶ swing_set(uo_cgame, ON)

vanilla server: no such key in CS_SYSTEMINFO        ─▶ swing_set(uo_cgame, OFF)  (vanilla values)
```

The client reads the **live configstring** (`cl.gameState`, see
`systeminfo_value` in `netcode/competitive.cpp`), not the cvar. The engine's
`CL_SystemInfoChanged` only sets cvars that are *present*, so a cvar would keep
the previous server's "1" after moving to a vanilla server.

The same channel carries upstream's competitive spec (`sv_competitive*`) and
ruleset id, which a UO server running CoDUO+ could publish in the same way.

## Source layout

| Folder | Contents |
| --- | --- |
| `src/core/` | `main.cpp` (DllMain, watcher), `game.*` (build identity, names, `port::` switches), `patches.cpp` (ini loading), logger, IAT hook helper, single-instance guard, toasts |
| `src/netcode/` | protocol/master, competitive caps + server spec, PB-style ruleset (parse/fetch/eval), cheat scan, version patch, server version gate, antilag (off) |
| `src/gameplay/` | swing (client + server), stance, lean (off), viewheight (off), anim clamp (reverted upstream) |
| `src/video/` | Hor+/stretch FOV, borderless window, fullscreen default, display probe, mode guard, gamma |
| `src/performance/` | frame limiter, 1 ms timer, CPU affinity, priority, working set, FSO |
| `src/input/` | raw mouse input (`m_rinput`) |
| `src/ui/` | Ctrl+M OpenGL overlay menu (drawn from `SwapBuffers`) |
| `src/features/` | settings-menu bridge, updater, PAM installer, Discord, demo upload, 2D draw hook + avatar overlay (off) |
| `src/shared/` | upstream's client/server shared lean code (unused while lean is off) |
| `tests/` | offline site checks, PE mapper, dedicated-server smoke test |
| `tools/re/` | RE helpers: PE/xref search, disassembly, UDP query/connect, Q3 Huffman |
| `ui_pk3/` | generator for the UO main-menu pk3 |
