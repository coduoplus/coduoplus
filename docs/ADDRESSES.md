# Address map: COD1.6X (CoD1 1.5) → CoDUO+ (CoD:UO 1.51)

Every address the mod reads, writes, or calls, with the CoD1 value from upstream,
the UO value, and the **evidence** that the UO value is right. The evidence column
is what `tests/site_check.cpp` asserts, so this table and the test can't
silently drift apart. If you change an address, change both.

Conventions:

- **VA** is an absolute virtual address at the module's preferred base.
  **RVA** is relative to the module base.
- `CoDUOMP.exe` is always at `0x00400000` (no relocations). Constants for it are
  VAs, or RVAs against `0x400000` where upstream used RVAs.
- `uo_cgame_mp_x86.dll` prefers `0x30000000` and `uo_game_mp_x86.dll` prefers
  `0x20000000`. **Both can be relocated.** A dedicated server was seen loading
  `uo_game` at `0x241f0000` and later `0x24140000`. The code always works from
  `module base + RVA` and never assumes a fixed VA.
- The constant names keep upstream's `CODMP_` / `CGAME_` prefixes on purpose, so
  a later merge of upstream changes still lines up. The *values* are UO's.

Binaries (SHA-256): `CoDUOMP.exe` `64a5aab2…d205`, `uo_cgame_mp_x86.dll`
`06bbb23a…7e1b`, `uo_game_mp_x86.dll` `584a0cc0…199e` (all from the 1.51 install
/ `pakuo07.pk3`). A second game DLL build, linked 2005-01-10 (`9ad513d1…fbfd3`),
is covered where it differs. See [Two uo_game builds](#two-uo_game-builds).

---

## Build identity — `src/core/game.h`

| What | Value | Evidence |
| --- | --- | --- |
| Exe image base | `0x00400000` | PE header |
| PE `TimeDateStamp` | `0x41b53340` (2004-12-07) | PE header. `game_exe_ok()` refuses any other build. |
| shortversion literal | VA `0x593cb0` = `"1.51\0\0\0\0win-x86"` | pushed by `Com_Init` at `0x43c0df` |
| Window class | `"CoD:United Offensive Multiplayer"` | `RegisterClassA` in `GLW_CreateWindow` (CoD1: `"CoDMP"`) |
| Base game folder / config | `uo`, `uoconfig_mp.cfg` | `fs_basegame` default `"uo"` (`FS_Startup`), `"exec uoconfig_mp.cfg"` in `Com_Init` |
| Module names | `uo_cgame_mp_x86.dll`, `uo_game_mp_x86.dll`, `uo_ui_mp_x86.dll` | `Sys_LoadDll` formats `uo_%s_mp_x86.dll` (`0x46b760`) |

## Engine: cvar and command API

| Function / global | CoD1 | UO 1.51 | Evidence |
| --- | --- | --- | --- |
| `Cvar_Get(name, def, flags)` | `0x43b880` | `0x43d9e0` | `Com_Init` registers `com_maxfps` through it (`push "com_maxfps"` at `0x43be6f`, `call` at `0x43be7a`) |
| cvar count (`> 0` = cvar system up) | `0x1912aec` | `0x4927ea0` | `Cvar_Get` bounds-checks it against `MAX_CVARS` (`0x7ff`) |
| `Cvar_FindVar(name)` | `0x43b790` | `0x43d8f0` | `push esi; push edi; mov edi,[esp+0xc]`, then hash bucket `[eax*4+0x9987a0]` |
| `Cvar_Set(name, value)` (force=1) | `0x43bea0` | `0x43df50` | `push 1 / push value / push name / call Cvar_Set2 (0x43dc50)` |
| `Cvar_Set2` ROM check | `0x43bcd8` | inside `0x43dc50` | references `"%s is read only.\n"`. The ROM/INIT/CHEAT checks only run when `force == 0`. |
| `Cbuf_ExecuteText(when, text)` | `0x42a180` | `0x42bcf0` | `switch(when)`. Default case: `Com_Error("\x15Cbuf_ExecuteText: bad exec_when")` |
| `cvar_t` layout | — | same as CoD1 | name `+0`, string `+4`, flags `+0x10`, value `+0x1c`, integer `+0x20`, stride `0x2c`, array `0x9827a0` |

## Engine: patches

| Patch | CoD1 | UO 1.51 | Original bytes / evidence |
| --- | --- | --- | --- |
| shortversion default `push` (opcode / operand) | `0x439f14` / `0x439f15` → `0x5a60d0` | `0x43c0df` / `0x43c0e0` → `0x593cb0` | `6a 44` (ROM\|SERVERINFO) before, `push "shortversion"` after |
| `r_fullscreen` default `push` | `0x46c256` / `0x46c257` → `0x5a7c54` | `0x4c3f42` / `0x4c3f43` → `0x5924a4` (`"1"`) | `6a 21` before, `push "r_fullscreen"` after, `call [ri.Cvar_Get]` |
| `r_displayRefresh` max 200.0f | imm at `0x4be6d1` | imm at `0x4c4001` | `push 0x43480000` right after the `r_displayRefresh` `Cvar_Get` (`0x4c3feb`), then `call R_CheckRange (0x4c12a0)` |
| snaps cap 30 → 40 (`cmp` / `mov` imm) | `0x459266` / `0x45926a` | `0x45bbf6` / `0x45bbfa` | `83 f9 1e  cmp ecx,30 ; 7e 05 ; b9 1e 00 00 00  mov ecx,30` in `SV_UserinfoChanged` (reads `"snaps"`) |
| cl_maxpackets cap 100 → 125 (imm) | `0x40c51d` | `0x40cc26` | `83 f8 64  cmp eax,100`, then `push "100"`, `push "cl_maxpackets"` |
| Frame limiter: `call` in `Com_Frame`'s wait loop | `0x43a4f5` → `0x438a70` | `0x43c6a7` → `0x43abc0` (**`Com_EventLoop`**) | target has a `0x8054`-byte frame and references `"Com_EventLoop: oversize packet"` |
| Frame limiter: loop branch | — (not patched upstream) | `0x43c6c9` `7c dc` (`jl 0x43c6a7`) → `90 90` | preceded by `3b f7  cmp esi,edi`. See [frame limiter](PORTING.md#frame-limiter) |
| `lastTime` (static int) | — | `0x980230` | `mov ecx,[0x980230]` right after the call |
| `com_maxfps` cvar slot | `0x1912acc` | `0x492908c` | `mov [0x492908c],eax` after its `Cvar_Get` (`0x43be91`), read at `0x43c681` |
| `dedicated` cvar slot | — | `0x49290a4` | `mov [0x49290a4],eax` after `Cvar_Get("dedicated")` (`0x43bdfe`) |
| Mouse centre X / Y | `0x93b398` / `0x93b228` | `0x9cdbb8` / `0x9cda48` | `SetCursorPos(x, y)` in `IN_GetMouseDelta` (`0x469ab0`) and `IN_MouseMove` (`0x469c20`), the only two `GetCursorPos` callers |
| `GetCursorPos` IAT slot | — | `0x5862a0` | 2 `call [slot]` (above) and 1 unreferenced linker thunk `jmp [slot]` at `0x56c2d6` |
| Live systeminfo: `stringOffsets[1]` / `stringData` | `0x148dcc0` / `0x148fcbc` | `0x495c6f8` / `0x495e6f4` | `CL_SystemInfoChanged` (`0x418b80`): `mov esi,[0x495c6f8] ; lea esi,[esi+0x495e6f4]` |
| `MAX_GAMESTATE_CHARS` | 16000 | `0x5000` | `CL_ParseGamestate` errors when `dataCount` passes `0x5000` |
| `sv_cheats` flags (proof of the SYSTEMINFO channel) | `0x48` | `0x48` (SYSTEMINFO\|ROM) | 4 registrations: `0x41a138`, `0x43e140`, `0x46067d`, `0x4c3b11` |

## Engine: network protocol and master server

UO's protocol is **22** (`0x16`). It appears as an imm8 at six places; CoD1 has
five, because UO also registers a `protocol` cvar. Each VA below is the imm8 byte.

| Site | CoD1 | UO 1.51 | Instruction |
| --- | --- | --- | --- |
| `SV_DirectConnect` gatekeeper | `0x4573f8` | `0x459b93` | `83 fe 16  cmp esi,22` (value of the `"protocol"` userinfo key) |
| client connect userinfo | `0x45d96e` | `0x411f32` | `6a 16 ; push "%i" ; call va` → `Info_SetValueForKey("protocol", …)` |
| `SVC_Info` infoResponse | `0x45eb57` | `0x461789` | `6a 16 ; push "%i" ; call va` |
| `SV_Init` `protocol` cvar | — | `0x46054a` | `6a 16 ; push "%i"`, then `Cvar_Get("protocol", …, 0x44)` |
| bot / test client userinfo | `0x411148` | `0x45c52f` | `6a 16`, then `push "connect \"…\\protocol\\%d\""` |
| reject message | `0x457423` | `0x459bcd` | `6a 16 ; push esi ; push "    rejected connect from protocol version %i (should be %i)\n"` |
| master hostname (in-place string) | `0x5a5568` (24 chars) | `0x59309c` (27 chars max) | `"coduomaster.activision.com"` + 2 NULs, next string at `0x5930b8`. Pushed by `CL_GlobalServers` (`0x4164e4`) and `SV_MasterHeartbeat` (`0x460f5d`). |
| master netadr cache | `0x93b110` | `0x9cd930` | `SV_MasterHeartbeat` resolves into it (`mov ebx,0x9cd930` at `0x460f62`) |
| master port | — | `20610` (`0x5082`) | `push 0x5082` → `htons`. Probed: ~111 servers answer `getservers 22`. |
| `getservers %s` format (14 bytes + 2 pad) | `0x5a5558` | `0x59308c` | pushed at `0x41653c`; argv(2) comes from the UI's `globalservers %d %d full empty` (22) |
| browser protocol filter (float) | `0x5a88b4` | `0x5b9f24` (22.0f) | `fld [0x5b9f24]` at `0x415647` in `CL_ServerInfoPacket`, its only reader |

## uo_cgame_mp_x86.dll (RVAs, base `0x30000000`)

| Feature / site | CoD1 RVA | UO RVA | Evidence |
| --- | --- | --- | --- |
| `cg_fov` cvarTable flags (`0x201` → `0x001`) | `0x769ec` | `0x8523c` | entry `{0x3052f7c0, "cg_fov", "80", 0x201}` at `0x30085230` |
| `CG_CalcFov` min-80 clamp `jp` (`7a` → `eb`) | `0x344e6` | `0x40008` | `fcomp [80.0f]` before it, reading `cg_fov.value` (`0x3052f7c8`); 160.0f max clamp after |
| `CG_GetEffectiveFov` | `0x344c0` | `0x3ffc0` | no args, hfov in ST(0), prologue `83 ec 08`; exactly two callers |
| …caller 1 (`CG_CalcViewValues`) | `0x345e1` | `0x402b1` | `push esi ; call` then the vfov trig |
| …caller 2 (`CG_DrawSkyBoxPortal`) | `0x350a5` | `0x42085` | `call ; fst [refdef.fov_x] ; fild [height] …` |
| vfov `fild [refdef.height]` operand | `0x345e8` | `0x402b8` → `0x30487a84` | then `fild [width]`, `fld st(2)`, `fmul`, `fptan`, `fstp`, `fdivp`, `fpatan` |
| vfov `fild [refdef.width]` operand | `0x345ee` | `0x402be` → `0x30487a80` | refdef: width `+8`, height `+0xc` |
| `glConfig.vidWidth` / `vidHeight` | `0x1d5a28` / `0x1d5a2c` | `0x447a88` / `0x447a8c` | `CG_Init`: `fild [vidWidth] ; fmul [1/640]`, `fild [vidHeight] ; fmul [1/480]` |
| swing: legs swingTolerance `push 40.0` | `0x451d` | `0x47af` | `push 150.0` before, `push ebp` after |
| swing: torso pitch speed `push 0.15` | `0x459d` | `0x4832` | `push 45.0` after |
| swing: torso yaw `mov esi,[bg_swingSpeed]` | `0x4468` (`0x301dd388`) | `0x46f8` (`0x3044ff08`) | `push esi ; push edx ; lea ebx,[edi+0x3b0]`. The cvar has 5 readers; this one is first. |
| swing: `fmul [0.3f]` operand (movement yaw pull) | `0x444d` (`0x3006b6f8`) | `0x46ab` (`0x3007bea0`) | `fadd [esp+0x18]` after. The 0.3f is shared by 9 readers. |
| stance hold `add ecx,0x190` imm32 | `0x397f` | `0x3b81` | `cmp eax,ebp ; je ; mov ecx,[cg.time]` before; `mov [edx+0x4c0],ecx` after (CoD1 field `+0x4a4`) |
| `BG_Player_DoControllersInternal` (lean, **not ported**) | `0x4960` | `0x4da0` | **different calling convention and body** — see [PORTING.md](PORTING.md#lean-fix) |
| …call site in `CG_Player_DoControllers` | `0x51d8` | `0x5741` | controller bone table `0x300823e0` loaded right after |

## uo_game_mp_x86.dll (RVAs, base `0x20000000`)

| Feature / site | UO RVA | Evidence |
| --- | --- | --- |
| swing (server mirror), all four code sites | same as uo_cgame: `0x47af`, `0x4832`, `0x46f8`, `0x46ab` | same bg_ source. The region `0x4680..0x4840` is byte-identical to uo_cgame's apart from relocated globals (checked). |
| …`bg_swingSpeed.value` | `0x1ecac8` (.bss) | 5 readers, torso first |
| …pooled `0.3f` | `0x7d138` | 10 readers |
| `ClientConnect` | `0x1dbb0` (both builds) | `sub esp,0x408`; references `"GAME_INVALIDPASSWORD"`; one caller |
| …its call in `vmMain` | `0x2ab99` (1.51) / `0x2aba9` (2005) | `vmMain` export at `0x2ab40` / `0x2ab50`; call within its switch |
| engine syscall pointer | `0x88278` (both builds) | `dllEntry`: `mov eax,[esp+4] ; mov [0x20088278],eax ; ret` |
| `G_GET_USERINFO` | syscall **33** | engine `SV_GameSystemCalls` (`0x45d820`): `movzx ecx,[n+0x45e988] ; jmp [ecx*4+0x45e724]`. Case 33 calls `0x45f0d0` (`"SV_GetUserinfo: bufferSize == %i"`). |

## Two uo_game builds

| | stock 1.51 | 2005 build |
| --- | --- | --- |
| Source | `uo/pakuo07.pk3` | loose `uo/uo_game_mp_x86.dll` in the test install, in no stock pk3 |
| `TimeDateStamp` | `0x41b532ae` (2004-12-07) | `0x41e30286` (2005-01-10) |
| SHA-256 | `584a0cc0…199e` | `9ad513d1…fbfd3` |
| swing sites | as above | identical |
| `vmMain` / ClientConnect call | `0x2ab40` / `0x2ab99` | `0x2ab50` / `0x2aba9` |

A dedicated server loads the loose file from `uo/`, so a server owner may run
either build. The version gate looks the build up by timestamp
(`VERSION_GATE_BUILDS`) and leaves an unknown build alone. `ctest` runs
`site_check` against both when the install has the 2005 file.
