#ifndef COD1RELOADED_SWING_FIX_H
#define COD1RELOADED_SWING_FIX_H

#include <windows.h>
#include <stdint.h>

// cod2x BG_PlayerAngles swing fixes: model follows the camera without lag.

namespace patches {

// uo_cgame_mp_x86.dll 1.51 RVAs, BG_PlayerAngles (shared bg code, also compiled into
// uo_game at the same RVAs). UO's function is CoD1's plus one extra block (a 0x106000
// flags test before the torso yaw swing), so every site moved; CoD1 RVAs in brackets.
//
// push imm32 args of BG_SwingAngles(dest, swingTol, clampTol, speed, angle, swinging):
//   300047aa  68 00 00 16 43   push 150.0   ; clampTolerance
//   300047af  68 00 00 20 42   push 40.0    ; legs swingTolerance        [0x451d]
//   30004832  68 9a 99 19 3e   push 0.15    ; torso pitch speed           [0x459d]
//   30004837  68 00 00 34 42   push 45.0
constexpr uintptr_t CGAME_SWING_LEGS_TOLERANCE_PUSH_RVA  = 0x47af;
constexpr uintptr_t CGAME_SWING_TORSO_PITCH_SPEED_PUSH_RVA = 0x4832;

// torso yaw swingSpeed dvar: 300046f8  8b 35 08 ff 44 30  mov esi,[0x3044ff08]  [0x4468]
// redirect operand at +2. (CoD1's dvar was 0x301dd388.)
constexpr uintptr_t CGAME_SWING_TORSO_YAW_MOV_RVA  = 0x46f8;
constexpr uintptr_t CGAME_SWING_TORSO_YAW_DVAR_RVA = 0x44ff08;
extern "C" float g_torso_yaw_speed_live;

// torso yaw movementYaw*0.3 + view: 300046a9  d8 0d a0 be 07 30  fmul [0.3f]  [0x444b]
// redirect operand at 0x46ab. The 0.3f constant has 9 readers, so it cannot be edited.
constexpr uintptr_t CGAME_SWING_TORSO_YAW_MOVEFRAC_FMUL_RVA  = 0x46ab;
constexpr uintptr_t CGAME_SWING_TORSO_YAW_MOVEFRAC_CONST_RVA = 0x7bea0;
extern "C" float g_torso_yaw_movefrac_live;

struct SwingFixConfig {
    bool  enable;
    float legs_tolerance;     // 40 -> 0
    float torso_pitch_speed;  // 0.15 -> 1.0
};

extern SwingFixConfig g_swing_fix_config;

bool apply_swing_fix(HMODULE cgame_module);  // idempotent; no-op on DLL mismatch

}  // namespace patches

#endif
