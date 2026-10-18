# CoDUO+

**CoDUO+** is a client mod for **Call of Duty: United Offensive 1.51** multiplayer
(`CoDUOMP.exe`). It's a port of [COD1.6X](https://github.com/cod1plus/client) (the
CoD1 mod: release 1.6.6, plus the non-PunkBuster work of 1.6.9) to UO: exact frame cap, raw mouse input, widescreen,
borderless window, 40-tick rate caps, player-model swing fix, a settings
overlay, and more. It's one DLL dropped into the game folder. Nothing on disk
is modified, and vanilla servers and players are unaffected.

**Status: 0.2.0 — development build.** The engine and server side are verified
against the stock binaries and on a running dedicated server. The client side
(rendering, input, menus) is verified offline but **not yet played**. See
[docs/TESTING.md](docs/TESTING.md#manual-client-checklist).

---

## What you get

| | |
| --- | --- |
| 🎯 **Exact FPS cap** | `com_maxfps 250` gives 250, not 240–248 (QPC-timed wait + 1 ms timer), on a continuous engine clock. |
| ⚡ **Less input latency** | Mouse movement made during the frame wait reaches this frame, not the next; `glFinish` after each frame keeps one frame in flight (auto-off when the GPU is the limit). |
| 📋 **Session line** | Once a minute the log says how the game ran: fps, late and long frames, GPU wait, display mode. |
| 🖱️ **Raw mouse input** | `m_rinput 1`: the device's own counts, no Windows acceleration or speed slider. `m_rinput_hz` shows the real polling rate. |
| 🖥️ **Widescreen** | Hor+ FOV for 16:9 / 21:9, or the stretched-4:3 look at native resolution. Off by default. |
| 🔒 **Same settings for everyone** | Frame pacing, input path, exclusive fullscreen at the display's max Hz are imposed (upstream 1.6.9's ini lock), so the borderless window is not available in release builds. Per-monitor gamma, instant minimize on alt-tab. |
| 🔭 **FOV 80–95** | `cg_fov` unlocked within 80–95 ([note](docs/PORTING.md#fov-unlock-is-ported-as-is--read-this)). |
| 🧍 **Swing fix** | Torso and legs follow your view without lag. Applied only on servers that run CoDUO+ and test the same pose, so what you see is always what the server hits ([why](docs/PORTING.md#swing-fix-only-where-the-server-mirrors-it)). |
| 🌐 **40-tick** | snaps cap 30 → 40, cl_maxpackets 100 → 125. A CoDUO+ server pushes its `competitive.cfg` (snaps 40, 125 pps, rate 90000 …) to CoDUO+ clients. Setup and the hit-reg maths: [docs/NETCODE.md](docs/NETCODE.md). |
| ⚙️ **Settings overlay** | Ctrl+M, or **CoDUO+ SETTINGS** in the main menu. |
| 🐧 **Linux servers too** | `coduoplus_sv.so` (LD_PRELOAD) gives a `coduo_lnxded` 1.51b server every server-side feature below. [docs/LINUX.md](docs/LINUX.md) |
| 🧰 **Server side** | Running CoDUO+ on a dedicated server: swing mirror for its players, a version gate that turns away *outdated* CoDUO+ builds (vanilla players are always welcome), uncapped snaps. |
| 🛡️ **Safe by construction** | Every patch checks the exact original bytes first; on any other exe or DLL build it patches nothing and says so in the log. |

Not (yet) ported from COD1.6X: the lean fix, antilag, the viewheight tweak,
and the in-match 2D draw hook. [docs/PORTING.md](docs/PORTING.md#feature-status)
has the full table and the reasons.

## Install

1. Open your game folder, the one with `CoDUOMP.exe`.
2. **Rename `mss32.dll` to `mss32_original.dll`.** Don't skip this: CoDUO+'s
   `mss32.dll` forwards all sound to the original. Without it the game won't
   start.
3. Copy **`mss32.dll`** and **`coduoplus.ini`** next to `CoDUOMP.exe`.
4. Optional: copy **`zz_coduoplus_ui.pk3`** into the `uo\` folder for a main-menu
   entry. Ctrl+M opens the settings without it.
5. Start `CoDUOMP.exe` as usual. `coduoplus.log` appears next to the DLL.

Your folder ends up with:

| File | |
| --- | --- |
| `mss32.dll` | CoDUO+ |
| `mss32_original.dll` | the game's original Miles sound DLL |
| `coduoplus.ini` | settings ([docs/CONFIGURATION.md](docs/CONFIGURATION.md)) |
| `uo\zz_coduoplus_ui.pk3` | main-menu entry |

## Uninstall

Delete `mss32.dll`, `coduoplus.ini` and `uo\zz_coduoplus_ui.pk3`, then rename
`mss32_original.dll` back to `mss32.dll`. The game is vanilla again.

## Troubleshooting

- **Game won't start / no sound:** step 2 was skipped, so `mss32_original.dll`
  is missing.
- **A feature does nothing:** look in `coduoplus.log`. Each patch logs a line
  when it applies, and a reason when it refuses (e.g. not the 1.51 exe).
- **Swing fix not active online:** expected on vanilla servers; the log says
  `swing_fix [client]: vanilla`.

## Build

```powershell
.\build.ps1            # 32-bit MinGW (WinLibs i686 in C:\tools\mingw32, or MSYS2 mingw32)
.\build.ps1 -Test      # + the test suite against your CoD:UO install
.\deploy.ps1           # copy into a test install (renames the Miles DLL on first run)
.\release.ps1          # tested build + player zip in dist\
```

Details in [BUILD.md](BUILD.md).

## Documentation

| Document | What's in it |
| --- | --- |
| [docs/LINUX.md](docs/LINUX.md) | The Linux server library: what it does, running it, how it hooks the engine, testing |
| [docs/NETCODE.md](docs/NETCODE.md) | How a shot is registered, server/client setup for 40-tick, why sv_fps 40, what lag compensation would add |
| [docs/PORTING.md](docs/PORTING.md) | How the port was done, every decision and why, feature status, what's left |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | How the DLL loads, the startup sequence, the watcher thread, safe patching, client↔server agreement |
| [docs/ADDRESSES.md](docs/ADDRESSES.md) | Every address, CoD1 → UO, with the evidence |
| [docs/CI.md](docs/CI.md) | GitHub workflows, branch protection, cutting a release |
| [docs/TESTING.md](docs/TESTING.md) | The test layers, how to run them, the manual client checklist |
| [docs/CONFIGURATION.md](docs/CONFIGURATION.md) | Every `coduoplus.ini` key |
| [BUILD.md](BUILD.md) | Toolchain, build, deploy, release |
| [CHANGELOG.md](CHANGELOG.md) | CoDUO+ releases, then upstream COD1.6X's history |

## Credits

COD1.6X is by the [cod1plus](https://github.com/cod1plus) team. Nearly all of
the mod's design and code is theirs; this repository keeps their full git
history. Some fixes are ported from [CoD2x](https://github.com/callofduty2x).
CoD:UO reverse-engineering notes in [docs/ADDRESSES.md](docs/ADDRESSES.md).

## Requirements

Call of Duty: United Offensive, multiplayer, patched to **1.51**
(`CoDUOMP.exe` SHA-256 `64a5aab2…d205`). Other versions are detected and left
alone.
