# Configuration — coduoplus.ini

`coduoplus.ini` sits next to `mss32.dll` (the folder with `CoDUOMP.exe`). Every key
lives in the `[coduoplus]` section; if the header is missing, Windows' profile
API ignores the **whole file** and the log says so. Booleans accept `on`/`off`
(also `1`/`0`, `true`/`false`). Changes apply at the next launch, except where
the in-game menu writes them itself.

The shipped defaults keep UO **looking and playing like vanilla**: your
resolution, fullscreen mode and FOV are untouched until you ask.

## What is deliberately *not* configurable

Following upstream's "competitive release" rule, anything that could give an
edge is compiled in, not read from the ini, so it can't be tuned in a text
file mid-match. That covers the swing values, the stance blend, the lean knobs,
the protocol and master server, the rate caps (snaps 40, cl_maxpackets 125), and
the FOV range (80–95). To change one, rebuild. The reasons for each value are
in [PORTING.md](PORTING.md).

### Keys the mod imposes (ini lock, upstream 1.6.9)

Seven keys are **rewritten to fixed values at every launch**, so everybody plays
with the same frame pacing, input path and presentation. An edited one is put
back in place; a deleted one is re-appended under
`; ----- IMPOSED BY THE MOD …`. Every other line of the file is kept byte for byte.
The values used at runtime are these, whatever the file says:

| Key | Imposed value |
| --- | --- |
| `fullscreen` | `on`, and `r_fullscreen` is held at 1, **read-only** (console and overlay can't change it) |
| `refresh_rate` | `max`, and `r_displayRefresh` is re-asserted at the display's highest listed Hz |
| `frame_limiter_enable` | `on` |
| `force_1ms_timer` | `on` |
| `gpu_sync` | `auto` |
| `input_late_sampling` | `on` |
| `smoothness_cpu_cores` | `0` (no CPU affinity) |

Consequences: the **borderless window and the overlay's "Borderless" display mode
can't take effect** (`r_fullscreen` is locked at 1), and with `refresh_rate = max`
the 1.6.6 `max_hz_native_res` rule applies (below). Upstream 1.6.10 ships the
same combination. A test DLL without the lock: build with
`-DCOD1RELOADED_DEV_UNLOCK=ON` (output in `build\devunlock\`, never ship it).

> `fov_unlock` is still listed in the shipped ini but **is not read** (upstream
> stopped reading it in 1.6.5). The unlock is compiled in as on. See
> [PORTING.md → FOV unlock](PORTING.md#fov-unlock-is-ported-as-is--read-this).

## Display

| Key | Default | What it does |
| --- | --- | --- |
| `fullscreen` | `on` | `on` keeps UO's exclusive fullscreen. `off` makes the game start windowed by repointing the `r_fullscreen` default to `"0"`. `uo\uoconfig_mp.cfg` is only corrected in one direction (upstream 1.6.9): `on` repairs a stale `seta r_fullscreen 0`, but a `1` there is the player's own choice (Options menu) and is never rewritten. Combine `off` with `window_borderless`. |
| `window_borderless` | `on` | When windowed, strip the border and cover the monitor (instant alt-tab, no mode switch). No effect in exclusive fullscreen. |
| `window_follow_current_monitor` | `on` | Borderless window follows the monitor it's on. |
| `window_minimize_on_focus_loss` | `on` | Minimise when another window takes focus. |
| `window_monitor` | `-1` | Monitor index for the borderless window (`-1` = current). Old name `window_monitor_index` still read. |
| `view_mode` | `classic` | `classic` = vanilla FOV. `widescreen` = Hor+ (a 16:9 screen sees more at the sides instead of a stretched 4:3). `stretched` = 4:3 vertical FOV on the wide buffer (the "1440×1080 stretched" look, rendered at native resolution). Old `widescreen_fov` / `widescreen_horplus_fov` booleans still read. |
| `aspect_ratio` | `auto` | Aspect Hor+ corrects for: `auto` (from the framebuffer), `4:3`, `16:9`, `16:10`, `21:9`, or a number like `1.6`. |
| `refresh_rate` | `0` | Screen Hz (`144`), `max` (highest the display supports at the game's resolution), or `0`/`auto`. Applied as `seta r_displayRefresh` + `vid_restart`. The engine's 200 Hz ceiling is lifted to 1000. |
| `max_hz_native_res` | `on` | With `refresh_rate = max`: if a custom resolution is only listed at a lower Hz, use the resolution that reaches the maximum and stretch 4:3 in the mod. |
| `force_resolution`, `resolution` | `off`, — | Force `r_mode -1` + `r_customwidth/height` (`resolution = 1920x1080`) through `uo\coduoplus_autoexec.cfg`. Older `widescreen_*` keys still read. |
| `gamma_fix_enable` | (on) | Per-monitor hardware gamma: brightness applies to the game's monitor only and is restored on alt-tab/exit. |
| `digital_vibrance` | `off` | Game-only digital vibrance (NVIDIA) / saturation (AMD), `0`-`100` in NVIDIA Control Panel terms: `50` = driver default, `0` greyscale, `100` maximum. Set on the game's monitor while the game has focus; that monitor's own level comes back on alt-tab, monitor change and exit (and at the next launch if the game crashed: `coduoplus.vibrancedesk`). Ctrl+M → Display writes it. |

A window can only show a resolution your **desktop** is in. Only exclusive
fullscreen switches the display mode. The display probe logs (and where
possible fixes) the impossible combinations before the renderer starts.

## Mouse

| Key | Default | What it does |
| --- | --- | --- |
| `raw_mouse_input` | `off` | Default for the archived cvar `m_rinput`: read the mouse from the device (`WM_INPUT`) instead of the Windows cursor. No pointer acceleration, no desktop speed slider, no pixel rounding. Your sensitivity is unchanged. Once you set `m_rinput` in the console, your config wins. `m_rinput_hz` / `m_rinput_hz_max` report the measured polling rate while the mouse moves. |

## In-game menu

| Key | Default | What it does |
| --- | --- | --- |
| `menu_enable` | `on` | The Ctrl+M overlay and the main-menu **CoDUO+ SETTINGS** item (needs `uo\zz_coduoplus_ui.pk3`). |

## Performance (anti-stutter)

| Key | Default | What it does |
| --- | --- | --- |
| `frame_limiter_enable` | `on` | Exact `com_maxfps`: QPC-timed wait so 250 means 250, not 240–248. Still runs the engine's event loop every frame. See [PORTING.md → Frame limiter](PORTING.md#frame-limiter). |
| `gpu_sync` | `auto` | `glFinish` right after every `SwapBuffers`: one frame in flight, never two queued in the driver, so the frame carrying your mouse movement is on screen before the next movement is read. `auto` switches itself off for the session if the wait fills more than half the frame for 10 s (the GPU sets the fps). `on` / `off` force it. The wait goes into the log. (upstream 1.6.9) |
| `input_late_sampling` | `on` | Read the mouse again right after the limiter's wait, so movement made during the wait reaches the frame being built, not the next one (upstream 1.6.9). Keys and buttons are already pumped after the wait in UO. Needs `frame_limiter_enable`. |
| `frame_limiter_bias_us` | `0` | Microseconds added to each frame deadline (negative = slightly faster). |
| `force_1ms_timer` | `on` | `timeBeginPeriod(1)` so sleeps and `com_maxfps` are precise. |
| `smoothness_process_priority` | `high` | `normal`, `above_normal`, `high`, `realtime`, `default`. |
| `smoothness_main_thread_priority` | `highest` | `normal`, `above_normal`, `highest`, `time_critical`, `default`. |
| `smoothness_disable_power_throttling` | `on` | Opt the process out of Windows EcoQoS throttling. |
| `smoothness_cpu_cores`, `smoothness_cpu_first_core` | `4`, `0` | Pin the process to N cores starting at the given one. |
| `smoothness_lock_working_set`, `_min_mb`, `_max_mb` | `on`, `128`, `5000` | Keep the game's memory resident. |
| `smoothness_disable_fso` | `on` | Registry opt-out of Windows Fullscreen Optimizations for this exe (`HKCU\…\AppCompatFlags\Layers`; takes effect next launch). |

## Online features

| Key | Default | What it does |
| --- | --- | --- |
| `updater_enable`, `updater_auto_download`, `updater_show_dialog` | `on` | The auto-updater. Idle while `updater_manifest_url` is empty. |
| `updater_manifest_url` | *(empty)* | A CoDUO+ release manifest (`{"version","download_url","notes"}`). **Never** point it at `cod1plus/client`: that feed ships the CoD1 DLL. |
| `pam_download_enable`, `pam_manifest_url` | `off`, *(empty)* | The INSTALL / UPDATE PAM button. Upstream's manifest is CoD1's rPAM, so it's off; point it at a UO mod manifest in the same format to use it. |
| `discord_rpc_enable`, `discord_rpc_client_id`, … | `on`, *(empty)* | Discord Rich Presence. Does nothing without an application id. |
| `demo_upload_*` | `off` | Upstream proof of concept: upload new demos from `uo\demos`. |
| `avatar_overlay_*` | `off` | Upstream proof of concept; its draw hook is not ported. |

## Diagnostics

| Key | What it does |
| --- | --- |
| `ctrl_dump` | Upstream diagnostic for the (not ported) lean code. |

## Hosting

Only used when this game runs a server.

| Key | Default | What it does |
| --- | --- | --- |
| `antilag` | `on` | Default of the server cvar `g_antilag` ([lag compensation](NETCODE.md#lag-compensation)). `+set` / rcon still win. |
| `antilag_max_ms` | `300` | Default of `g_antilagMaxMs`, the furthest back a shot is compensated. |
| `antilag_pose` | `on` | Default of `g_antilagPose`: also rewind the target's pose (stance, lean, aim). |

## Server: competitive.cfg

On a machine **hosting** a server (dedicated or listen), `competitive.cfg` next to
`mss32.dll` is published to CoDUO+ clients as `sv_competitive` (+`2..4`) and applied
by them while connected: `cvar value` locks, `cvar min max` clamps. It is re-read
every second and cleared when the file goes away. See `competitive.cfg.example`
and [NETCODE.md](NETCODE.md#server-setup).

## Files the mod reads and writes

| File | Purpose |
| --- | --- |
| `coduoplus.ini` | this configuration |
| `coduoplus.log` / `coduoplus.prev.log` | this run's log / the previous run's (kept for crashes) |
| `uo\uoconfig_mp.cfg` | read by the display probe; `r_fullscreen` / `r_displayRefresh` may be rewritten to match the ini |
| `uo\coduoplus_autoexec.cfg` | only with `force_resolution = on` |
| `uo\zz_coduoplus_ui.pk3` | main-menu entry (generated, see `ui_pk3/build_pk3.py`) |
| `competitive.cfg` | (server only) the netcode spec pushed to clients |
| `mss32_original.dll` | the real Miles DLL every audio call is forwarded to |
