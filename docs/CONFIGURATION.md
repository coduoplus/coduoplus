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

> `fov_unlock` is still listed in the shipped ini but **is not read** (upstream
> stopped reading it in 1.6.5). The unlock is compiled in as on. See
> [PORTING.md → FOV unlock](PORTING.md#fov-unlock-is-ported-as-is--read-this).

## Display

| Key | Default | What it does |
| --- | --- | --- |
| `fullscreen` | `on` | `on` keeps UO's exclusive fullscreen. `off` makes the game start windowed: it repoints the `r_fullscreen` default to `"0"` *and* rewrites a contradicting `seta r_fullscreen` in `uo\uoconfig_mp.cfg` (the ini wins). Combine with `window_borderless`. |
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
| `ruleset_fetch_enable`, `ruleset_url` | `on`, cod1plus/rulesets | The PunkBuster-style cvar ruleset a server can ask for (`sv_competitive_ruleset`). Only enforced while connected to a server that publishes one; vanilla UO servers don't. A built-in copy is the offline fallback, cached in `rulesets\`. |
| `pam_download_enable`, `pam_manifest_url` | `off`, *(empty)* | The INSTALL / UPDATE PAM button. Upstream's manifest is CoD1's rPAM, so it's off; point it at a UO mod manifest in the same format to use it. |
| `discord_rpc_enable`, `discord_rpc_client_id`, … | `on`, *(empty)* | Discord Rich Presence. Does nothing without an application id. |
| `demo_upload_*` | `off` | Upstream proof of concept: upload new demos from `uo\demos`. |
| `avatar_overlay_*` | `off` | Upstream proof of concept; its draw hook is not ported. |

## Diagnostics

| Key | What it does |
| --- | --- |
| `ctrl_dump`, `antilag_diag_*` | Upstream diagnostics for the (not ported) lean and antilag code. |

## Files the mod reads and writes

| File | Purpose |
| --- | --- |
| `coduoplus.ini` | this configuration |
| `coduoplus.log` / `coduoplus.prev.log` | this run's log / the previous run's (kept for crashes) |
| `uo\uoconfig_mp.cfg` | read by the display probe; `r_fullscreen` / `r_displayRefresh` may be rewritten to match the ini |
| `uo\coduoplus_autoexec.cfg` | only with `force_resolution = on` |
| `uo\zz_coduoplus_ui.pk3` | main-menu entry (generated, see `ui_pk3/build_pk3.py`) |
| `rulesets\` | ruleset cache |
| `mss32_original.dll` | the real Miles DLL every audio call is forwarded to |
