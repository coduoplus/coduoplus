#ifndef COD1RELOADED_GAME_H
#define COD1RELOADED_GAME_H

#include <windows.h>
#include <stdint.h>

// Which game this build targets, in one place.
//
// This tree is the CoD:UO port of COD1.6X. Upstream hooks CoDMP.exe 1.5 at
// hardcoded addresses; every one of those was re-found in CoD:UO 1.51 (see
// docs/PORTING.md for the method and docs/ADDRESSES.md for the full map).
// An address is only meaningful for the exact build it was found in, so every
// patch asks game_exe_ok() before touching engine memory - not just "is the
// exe at 0x400000" like upstream, which any 0x400000 exe (another UO patch,
// CoD1, a cracked/repacked build) would pass, and then crash on the first
// poke into the wrong code.

namespace patches {

// Game modules. UO prefixes CoD1's names with "uo_": the engine builds
// "uo_%s_mp_x86.dll" in Sys_LoadDll (CoDUOMP.exe 0x46b760).
constexpr const char* GAME_EXE_NAME   = "CoDUOMP.exe";
constexpr const char* GAME_CGAME_DLL  = "uo_cgame_mp_x86.dll";
constexpr const char* GAME_GAME_DLL   = "uo_game_mp_x86.dll";
constexpr const char* GAME_UI_DLL     = "uo_ui_mp_x86.dll";

// Base game folder and the engine's archived-cvar file. UO registers
// fs_basegame with default "uo" (FS_Startup, decompile line 27988) and
// Com_WriteConfiguration writes "uoconfig_mp.cfg" ("exec uoconfig_mp.cfg" in
// Com_Init) into it. CoD1 uses Main\config_mp.cfg - and a UO install usually
// sits in the same folder as CoD1, so upstream's paths would read and REWRITE
// CoD1's config instead of UO's. Demos, screenshots and configs/ live under
// <basepath>\uo too.
constexpr const char* GAME_BASE_DIR  = "uo";
constexpr const char* GAME_CONFIG_MP = "uoconfig_mp.cfg";

// Main window class, registered in GLW_CreateWindow (CoDUOMP.exe decompile
// "CoD:United Offensive Multiplayer"). CoD1's is plain "CoDMP".
constexpr const char* GAME_WINDOW_CLASS = "CoD:United Offensive Multiplayer";

// CoDUOMP.exe 1.51 (sha256 64a5aab2...d205), fixed image base, no relocations.
constexpr uintptr_t GAME_EXE_BASE = 0x00400000;

// Identity probes. Both are read-only .rdata, so a match is a strong signal the
// running exe is the 1.51 build the address map was taken from:
//   - "1.51" is the shortversion default pushed by Com_Init @0x43c0df;
//   - the PE link timestamp, unique per build.
constexpr uintptr_t GAME_SHORTVERSION_STR_VA = 0x00593cb0;  // "1.51\0\0\0\0win-x86"
constexpr uintptr_t GAME_PE_TIMESTAMP        = 0x41b53340;  // IMAGE_FILE_HEADER.TimeDateStamp, 2004-12-07

// True when the running process is CoDUOMP.exe 1.51 at its preferred base AND
// the engine address map in this tree describes it (port::ENGINE). Every caller
// of an engine function pointer or engine global goes through this, so while a
// map is incomplete the engine code paths stay inert instead of jumping into
// CoD1 addresses. Cached after the first call; cheap to call from every tick.
bool game_exe_ok();

// Port status, one switch per feature group. A feature is enabled here only
// once every address it touches has been re-found in the UO 1.51 binaries and
// added to tests/site_check.cpp, so the offline test proves the bytes it
// expects are really there. Until then the feature is compiled but never
// applied: a CoD1 offset inside a UO module lands in unrelated code.
// docs/PORTING.md has the per-feature status table.
namespace port {
constexpr bool ENGINE           = true;   // CoDUOMP.exe: cvar API, version, protocol, caps, mouse, frame limiter
constexpr bool CGAME_FOV        = true;   // uo_cgame: cg_fov cheat flag + min clamp
constexpr bool CGAME_WIDESCREEN = true;   // uo_cgame: CG_CalcFov Hor+/stretch hooks
constexpr bool CGAME_SWING      = true;   // uo_cgame: torso/legs swing constants
constexpr bool CGAME_STANCE     = true;   // uo_cgame: stance blend hold time
constexpr bool CGAME_LEAN       = false;  // uo_cgame: DoControllers lean/body hook
constexpr bool CGAME_DRAW2D     = false;  // uo_cgame: CG_Draw2D / shader hooks (discord, avatar)
constexpr bool GAME_SWING       = true;   // uo_game: server mirror of the swing fix (+ systeminfo flag)
constexpr bool GAME_VIEWHEIGHT  = false;  // uo_game: PM_UpdateViewHeight lerp speed
constexpr bool GAME_VERSION     = true;   // uo_game: ClientConnect version gate
constexpr bool GAME_ANTILAG     = true;   // uo_game: lag compensation (shared/antilag_core)
}  // namespace port

}  // namespace patches

#endif
