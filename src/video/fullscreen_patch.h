#ifndef COD1RELOADED_FULLSCREEN_PATCH_H
#define COD1RELOADED_FULLSCREEN_PATCH_H

#include <windows.h>
#include <stdint.h>

namespace patches {

// CoDUOMP.exe 1.51 RVAs, image base 0x00400000. R_Register registers r_fullscreen
// through the refimport's ri.Cvar_Get pointer:
//   4c3f40  6a 21              push 0x21         ; CVAR_ARCHIVE|CVAR_LATCH
//   4c3f42  68 a4 24 59 00     push 0x5924a4     ; "1"   <- operand patched
//   4c3f47  68 5c ee 59 00     push 0x59ee5c     ; "r_fullscreen"
//   4c3f51  ff 15 20 8c 89 04  call [0x4898c20]  ; ri.Cvar_Get
constexpr uintptr_t CODMP_RFULLSCREEN_PUSH_OPCODE_RVA  = 0x000c3f42; // opcode 0x68
constexpr uintptr_t CODMP_RFULLSCREEN_PUSH_OPERAND_RVA = 0x000c3f43; // 4 bytes to patch
constexpr uintptr_t CODMP_RFULLSCREEN_ORIGINAL_PTR     = 0x005924a4; // ptr to "1\0"

struct FullscreenConfig {
    // gotcha: CVAR_ARCHIVE seta in config_mp.cfg overrides this default
    bool force_windowed_default;
    // true when coduoplus.ini spells out `fullscreen = on/off`: then the .ini is the
    // player's decision and display_probe rewrites a disagreeing seta r_fullscreen in
    // config_mp.cfg (see enforce_ini_fullscreen). false = key absent, config wins.
    bool ini_key_present;
};

extern FullscreenConfig g_fullscreen_config;

// idempotent
bool apply_fullscreen_patch();

}  // namespace patches

#endif
