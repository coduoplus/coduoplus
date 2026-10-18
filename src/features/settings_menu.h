#ifndef COD1RELOADED_SETTINGS_MENU_H
#define COD1RELOADED_SETTINGS_MENU_H

#include <windows.h>
#include <stdint.h>

namespace patches {

// In-game "1.6X SETTINGS" menu bridge: registers cvars the ui_mp/cod1x_settings.menu
// binds to (FOV + screen ratio), unlocks cg_fov, and
// mirrors live changes back into coduoplus.ini.
struct SettingsMenuConfig {
    bool enable       = true;
    bool fov_unlock   = true;   // patch cgame so cg_fov isn't cheat-locked/clamped to 80
    char menu_name[48] = "cod1x_settings";
    char menu_file[96] = "ui_mp/cod1x_settings.menu";
    // "auto" (engine/desktop default), "max" (highest Hz the display supports at the game's
    // resolution), or a number ("144"). Applied via seta r_displayRefresh + vid_restart.
    char refresh_rate[16] = "max";
    // refresh_rate = max: when the configured resolution cannot reach the display's
    // maximum Hz, run at the resolution that can (display_probe.cpp). Off = keep the
    // configured resolution and whatever Hz it lists.
    bool max_hz_native_res = true;
};

extern SettingsMenuConfig g_settings_menu_config;
void menu_ini_writeback(const char* key, const char* value);   // settings_menu.cpp, same .ini rules

// The FOV the mod allows, everywhere: the 1.6X menu slider, and a live clamp on
// cg_fov itself (console / uoconfig_mp.cfg cannot get around the slider). The cgame's
// own ceiling stays at 160 - never reached. Competitive policy (enzo, 2026-09-17):
// 95 is the max, same as the servers' competitive.cfg range.
constexpr int COD1X_FOV_MIN = 80;
constexpr int COD1X_FOV_MAX = 95;

// uo_cgame_mp_x86.dll 1.51 RVAs (base 0x30000000; CoD1 had 0x769ec / 0x344e6).
//   FLAGS    : cg_fov cvarTable entry {vmCvar 0x3052f7c0, "cg_fov", "80", flags} at
//              0x30085230; flags dword 0x00000201 (ARCHIVE|CHEAT) -> 0x00000001 (drop CHEAT)
//   MINCLAMP : CG_CalcFov reads cg_fov.value (0x3052f7c8) and clamps it to [80, 160]:
//                3003fffd  d8 1d b0 bf 07 30   fcomp [80.0f]
//                30040008  7a 0a               jp  +0x0a      <- 7A -> EB (jmp): no min clamp
//                3004000a  c7 44 24 04 00 00 a0 42  mov [fov],80.0f
//              (same shape as CoD1; the 160 max clamp right after stays)
constexpr uintptr_t CGAME_FOV_CVAR_FLAGS_RVA  = 0x0008523c;
constexpr uintptr_t CGAME_FOV_MINCLAMP_JP_RVA = 0x00040008;
// CoDUOMP.exe 1.51 R_Register, right after ri.Cvar_Get("r_displayRefresh","0",0x20):
//   4c4000  68 00 00 48 43   push 0x43480000   ; max 200.0f   <- imm patched
//   4c4005  6a 00            push 0            ; min 0
//   4c4007  b8 01 00 00 00   mov  eax,1        ; integral
//   4c4012  e8 89 d2 ff ff   call R_CheckRange (0x4c12a0)
constexpr uintptr_t CODMP_REFRESH_CAP_IMM_VA   = 0x004c4001;

// CoDUOMP.exe 1.51 (base 0x400000) engine fns not already declared in other headers.
constexpr uintptr_t CODMP_CVAR_FINDVAR_VA  = 0x0043d8f0; // cvar_t* Cvar_FindVar(name)
// void Cbuf_ExecuteText(int when, const char* text): 0 EXEC_NOW, 1 INSERT, 2 APPEND
// (switch at 42bcf4; anything else -> Com_Error "Cbuf_ExecuteText: bad exec_when")
constexpr uintptr_t CODMP_CBUF_EXECTEXT_VA = 0x0042bcf0;

void settings_menu_start();                        // DllMain: log enable state
void settings_menu_apply_to_cgame(HMODULE cgame);  // FOV unlock (idempotent, safe to re-run)
void settings_menu_patch_refresh_cap();            // DllMain: lift the engine's r_displayRefresh 200 Hz cap
void settings_menu_tick();                         // watcher thread: register cvars, poll key, poll ratio

}  // namespace patches

#endif
