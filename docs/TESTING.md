# Testing CoDUO+

A patch that lands one byte off doesn't fail to compile. It corrupts a
function, or silently changes gameplay. So the tests check the *game's* binaries
and a *running game*, not just the mod's own code. Three automated layers, plus
a manual checklist for what automation can't reach.

```text
ctest
 ├─ fetch_binaries     collect + hash-verify the stock CoD:UO 1.51 binaries
 ├─ site_check         every patch site, offline, against those binaries
 ├─ site_check_2005    the same against the 2005 uo_game build (skipped if absent)
 ├─ ui_pk3             generate the UO main-menu pk3 from the install
 ├─ doc_links          every relative link / #anchor in the docs resolves
 ├─ frame_limiter      the real wait inside a copy of the patched Com_Frame loop (~20 s)
 ├─ ini_lock           the imposed-keys rewrite over the shipped coduoplus.ini
 ├─ competitive_sv     competitive.cfg -> sv_competitive*, checked against the client's reading
 └─ smoke_dedicated    (opt-in) run the real game with the mod as a server
```

## Running

```sh
# configure once (path to a CoD:UO 1.51 install; it is only read)
cmake -B build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
      -DCODUO_GAME_DIR="D:/Call of Duty - Copy" [-DCODUO_SMOKE=ON]
cmake --build build
ctest --test-dir build --output-on-failure
```

or `.\build.ps1 -Test`. `release.ps1` runs the suite and refuses to package a
failing build.

The smoke test **launches the game**, so it's off unless `-DCODUO_SMOKE=ON`, and
it needs the build deployed first (`.\deploy.ps1`). It runs as a dedicated
server, which opens only a console window and doesn't grab the screen or
mouse, so it can run while someone is playing on the same PC.

## 1. fetch_binaries — the exact files the addresses came from

`tests/fetch_binaries.py <game> <out>` copies `CoDUOMP.exe` and extracts the
three MP modules from **`uo/pakuo07.pk3`**, not from the loose `uo/*.dll` files.
The engine re-extracts whichever DLL a server wants, so loose copies can belong
to a mod. On the test machine the loose `uo_game_mp_x86.dll` is a different
build. Every file is checked against the SHA-256 of stock 1.51; a mismatch fails
the run.

If the loose `uo_game_mp_x86.dll` is exactly the known 2005-01-10 build, a
second set is written to `<out>-2005` for `site_check_2005`.

## 2. site_check — every patch site, offline

`tests/site_check.cpp` maps each PE image the way the Windows loader would
(`tests/image.cpp`) and checks every address the mod uses, **using the mod's
own header constants**. If a header changes, the test checks the new value.

The checks go further than the runtime guards. They pin each address to its
meaning:

- the byte or operand the patch overwrites is the vanilla value;
- the instruction around it is the expected opcode;
- the **context** matches: the string pushed next to it (`"r_fullscreen"`,
  `"snaps"`, `"GAME_INVALIDPASSWORD"`), the call target (`Com_EventLoop`,
  `BG_SwingAngles`, `SV_GetUserinfo` through the engine's syscall jump table),
  the constants nearby (`80.0f`, `160.0f`, `1/640`), the number of readers of a
  global;
- cross-module facts: all 77 `mss32.dll` imports of `CoDUOMP.exe` are
  forwarded by `mss32_proxy.def`; `uo_cgame` and `uo_game` contain
  byte-identical `BG_PlayerAngles` apart from relocations (the premise of the
  swing server mirror).

Output, one line per feature group:

```text
identity           ON     7 checks  ok
proxy              ON    78 checks  ok
engine             ON    94 checks  ok
cgame_fov          ON     8 checks  ok
cgame_widescreen   ON    19 checks  ok
cgame_swing        ON    15 checks  ok
cgame_stance       ON     5 checks  ok
cgame_lean         off    0 checks  -
cgame_draw2d       off    0 checks  -
game_swing         ON    16 checks  ok
game_viewheight    off    0 checks  -
game_version       ON    11 checks  ok
game_antilag       off    0 checks  -
```

Rules (enforced): a group switched ON in `core/game.h` must have checks, and
all must pass. A group that is off is still checked if it has checks, so a
branch can land addresses before enabling the feature.

**The test catches real mistakes.** Shifting `CODMP_SNAPS_CAP_CMP_VA` by one
byte fails two checks (tried). While writing it, five assertions failed on the
first run; each was a wrong *assumption in the test* (string prefixes, operand
offsets, a linker import thunk), investigated and documented before the
address was trusted.

## 3. frame_limiter — the wait itself, against a fake engine

`tests/test_frame_limiter.cpp` (after upstream 1.6.9's `tools/test_frame_limiter.cpp`)
runs the real `frame_wait()` inside a copy of UO's *patched* `Com_Frame` loop:
one pass per frame, because the `jl` is removed. The engine side is faked: a
`Sys_Milliseconds` with its own epoch, an event loop and a late-mouse hook that
count their calls. At 250, 125 and 333 fps, with and without overrunning frames,
it checks:

- the average period is 1000/com_maxfps within 1%;
- no frame is doubled;
- the reported time is within 2 ms of the engine's clock;
- the event loop runs exactly once per frame, after the late mouse read;
- at 250 fps at least 95% of Com_Frame's whole-ms deltas are exactly 4. The
  continuous clock gives 100%; reporting the stepped engine clock instead
  gives about 61% and fails (tried);
- with `com_maxfps 0` and on a dedicated server, the vanilla loop: `minMsec` 1,
  the engine's own clock, no late mouse.

## 4. ini_lock — the imposed-keys rewrite

`tests/test_ini_lock.cpp` (after upstream's) runs `ini_lock_apply()` on copies of
the shipped `coduoplus.ini`. It checks that the shipped file already holds the
locked values; that a player's edits to all seven are put back while the other
119 lines stay identical and in place; that a second pass changes nothing; that
deleted keys are re-appended under the header; that CRLF and LF files keep their
line ending; and that the runtime values are forced.

## 5. competitive_sv — the server spec publisher

`tests/test_competitive_sv.cpp` parses `competitive.cfg` text into the spec and
splits it into the four ≤220-char SYSTEMINFO parts. It checks the result against
the client's own reading (join with a space, `name=value` / `name=min:max`): exact
values and ranges, comments skipped, and malformed lines refused. Quotes, `;`,
backslashes, inline comments and wrong token counts would break the
`set "…"` command or the info string. A 30-rule file round-trips intact through
the chunks; an over-long one is reported and only whole entries are published.

## 6. ui_pk3

Builds `dist/zz_coduoplus_ui.pk3` from the install's `uo/pakuo03.pk3`. It fails
if UO's `main.menu` isn't the stock file, if the AUTO-UPDATE anchor is missing,
or if the result loses CRLF or balanced braces.

## 7. smoke_dedicated — the real game, running the mod

`tests/smoke_dedicated.py <game>`:

1. starts `CoDUOMP.exe +set dedicated 1 +set net_port 28999 +map mp_harbor`
   from the install, with the deployed `mss32.dll`;
2. polls `getstatus` until the server answers (map loaded, game module running);
3. checks the server advertises **protocol 22** and **`shortversion CoDUO+ …`**
   (the proxy loaded, DllMain ran, the version patch is live);
4. performs three real **connect handshakes** (`tools/re/q3connect.py`:
   `getchallenge`, then a Huffman-compressed `connect` with
   `tools/re/q3huff.py`, as the real client does), which exercise the
   server-side ClientConnect hook:

   | userinfo | expected | why |
   | --- | --- | --- |
   | `coduoplus=5` | `error` + "Update coduoplus to CoDUO+ 0.2.0+ …" | older CoDUO+ build: refused by our gate |
   | (vanilla) | `connectResponse` | vanilla players are let in |
   | `coduoplus=16` | `connectResponse` | current build |

5. for the run, installs `competitive.cfg.example` as the server's
   `competitive.cfg` (and removes it afterwards), and requires
   `engine holds sv_competitive "snaps=40 cl_maxpackets=125 …" (SYSTEMINFO yes)`:
   the spec really is in the cvar clients receive;
6. kills the server and reads `coduoplus.log`, which must contain every
   ENGINE patch's "applied" line, the swing server mirror, the published
   `sv_coduoplus_swing 1`, the version gate installed and its rejection. It
   must contain **none** of the phrases a patch logs when it refuses
   (`abort`, `SKIPPED`, `expected 0x`, `do not match`, …) and no window
   restyle (a dedicated server has no game window).

A typical run:

```text
server: map=mp_harbor protocol=22 shortversion=CoDUO+ 0.2.0
connect {'coduoplus': '5'} -> error | Update coduoplus to CoDUO+ 0.2.0+ to join this server.
connect vanilla            -> connectResponse
connect {'coduoplus': '16'} -> connectResponse
smoke_dedicated: ok (23 checks of the log, 0 problems)
```

## What is not covered automatically

A dedicated server loads no renderer, no input, and no cgame, so nothing
client-side is exercised by the smoke test: FOV unlock, Hor+/stretch, swing
client half, overlay menu, raw input, borderless window, gamma, frame rate.
Their *addresses* are covered by `site_check`; their *behaviour* needs a client.

## Manual client checklist

Run on the test install after `.\deploy.ps1` and
`python -I ui_pk3/build_pk3.py --deploy`. Close the game afterwards and send
`coduoplus.log`.

1. **Starts, sound works.** Main menu shows `CoDUO+ 0.2.0` bottom right and a
   gold **CoDUO+ SETTINGS** item between QUIT and AUTO-UPDATE.
2. **Overlay menu.** Click CoDUO+ SETTINGS, or press Ctrl+M: the overlay opens
   and the mouse moves its cursor. Esc closes it.
3. **Server browser.** Join Game → Internet → refresh: the usual UO list fills
   (protocol 22, stock master).
4. **Local game.** `/devmap mp_harbor` (listen server: the client *and* the
   server half of the swing fix run). The log should show `swing_fix [server]: ON`,
   `server publishes sv_coduoplus_swing 1`, then `swing_fix [client]: ON`.
5. **Vanilla server.** Join any public server. The log should show
   `swing_fix [client]: vanilla` (the swing fix stays off where the server
   doesn't mirror it).
6. **Competitive spec.** With `competitive.cfg.example` copied to
   `competitive.cfg` before `/devmap mp_harbor`, `/snaps` and `/cl_maxpackets`
   read 40 and 125 and refuse changes ("is read only"). Leaving releases them.
7. **FOV.** `/cg_fov 95` takes effect without `sv_cheats`; `/cg_fov 120` is
   clamped back to 95.
8. **GPU sync.** After a minute in game the log has no
   `gpu_sync: the GPU sets the frame rate` line on a machine that holds the fps
   cap (it should on any modern GPU at 250).
9. **Frame rate.** `/com_maxfps 250`, `/cg_drawfps 1`: a steady 250 (not
   240–248). `/com_maxfps 125`: 125. Input and network stay responsive (the
   limiter still runs `Com_EventLoop` every frame).
10. **Raw input.** `/m_rinput 1`, move the mouse: aim works, `m_rinput_hz`
   reports a rate while moving.
11. **Map change.** `/map mp_arnhem` from the devmap: the log shows the cgame
   reload being re-patched (`uo_cgame_mp_x86.dll reloaded`), FOV still unlocked.
12. **Alt-tab** in and out a few times; no freeze, gamma restored on the desktop.
13. **Session line.** After a minute, and when quitting, the log has a line like
    `session 60 s: 250.0 fps (15000 frames, 0 late wakes, 0 long, max 4.3 ms) | GPU 0.50 ms …`.
    Paste it with any "it stutters" report.
