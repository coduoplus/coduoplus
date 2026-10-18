#ifndef COD1RELOADED_STANCE_FIX_H
#define COD1RELOADED_STANCE_FIX_H

#include <windows.h>
#include <stdint.h>

namespace patches {

// 3rd-person stance-transition sync (the cod2x stance port, CoD1-shaped).
//
// CoD1 already times stance changes natively: BG_SetNewAnimation (cgame 0x3870) arms
// a blend-hold `ci+0x4a4 = cg.time + 400ms` whenever the legs anim crosses a stance
// class (stand<->crouch<->prone), and every blend started inside that window is
// force-extended to it. But the 1st-person view/hitbox transitions in 150ms (down) /
// 200ms (up) -> the model enemies see lags the real hitbox by up to 250ms during
// crouch spam. Fix = re-time that hold to match the 1st person. One imm32:
//   `add ecx, 0x190` @ cgame+0x397d, imm32 @ +0x397f.
// Independent from viewheight_lerp_speed (gamex86 rate-path): no interaction.
struct StanceFixConfig {
    // OFF since 2026-08-01: shortening the model's stance blend made it track a
    // crouch-spammer closely, which players read as flicker. The 1.6.0 behaviour
    // (vanilla 400 ms blend + viewheight_lerp_speed 150) keeps the model slow and
    // readable, which is what makes up-down abuse punishable. Re-enabling this
    // without also re-tuning viewheight_lerp_speed brings the flicker back.
    bool enable   = false;
    int  blend_ms = 250;   // only used when enable=true; 400=vanilla
};

extern StanceFixConfig g_stance_fix_config;

// uo_cgame 1.51: BG_SetNewAnimation compares the old and new legs anim's stance class
// (two calls to 0x30003a40) and, when they differ, arms the hold:
//   30003b75  8b 0d 30 a0 53 30   mov ecx,[cg.time]
//   30003b7f  81 c1 90 01 00 00   add ecx,0x190          <- imm32 @ +0x3b81 (CoD1 +0x397f)
//   30003b85  89 8a c0 04 00 00   mov [edx+0x4c0],ecx    ; ci field (CoD1 +0x4a4)
// Two other `add ecx,0x190` in uo_cgame (0x678f, and in CoD1 0x6643) are a different
// 400/200 ms choice and are NOT this site.
constexpr uintptr_t CGAME_STANCE_HOLD_IMM_RVA = 0x00003b81; // imm32 of `add ecx, 0x190`
constexpr uint32_t  CGAME_STANCE_HOLD_VANILLA = 0x190;      // 400 ms

bool apply_stance_fix(HMODULE cgame_module);  // idempotent; re-run on cgame reload
void stance_fix_hot_repoke();                 // hot-reload: re-poke on value/enable change

}  // namespace patches

#endif
