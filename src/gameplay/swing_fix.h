#ifndef COD1RELOADED_SWING_FIX_H
#define COD1RELOADED_SWING_FIX_H

#include <windows.h>
#include <stdint.h>

#include "shared/coduoplus_shared.h"

// cod2x BG_PlayerAngles swing fixes: model follows the camera without lag.
//
// CoD:UO port - BOTH SIDES OR NEITHER. The fix changes the pose BG_PlayerAngles
// builds, and that pose is not just drawn: the server builds the skeleton its
// bullet traces test from the same function. Upstream measured the drawn-vs-
// tested gap at up to ~10 units on a leaner when only the client is patched, and
// mirrors the four constants on its own server (cod1plushookserver swing_sync.c).
// Every UO server is vanilla, so here:
//   - a server running CoDUO+ (dedicated or listen) patches its own
//     uo_game_mp_x86.dll - which carries the same bg code at the SAME RVAs - and
//     publishes the SYSTEMINFO cvar SWING_SERVERINFO_CVAR = "1";
//   - the client applies the fix to its cgame only while the server it is
//     connected to publishes that cvar, and puts the vanilla values back as soon
//     as it does not (vanilla server, or a server whose mirror failed).
// So what a CoD:UO+ player sees is always what the server tests.

namespace patches {

// The sites of the four patches inside one module (RVAs). uo_cgame and uo_game
// compile the same bg_ source, so the code sites coincide; only the module's
// own globals (the bg_swingSpeed cvar value and the pooled 0.3f) differ.
struct SwingSites {
    uintptr_t legs_tolerance_push;   // push 40.0  (BG_SwingAngles swingTolerance, legs)
    uintptr_t torso_pitch_speed_push;// push 0.15  (really the LEAN swing channel)
    uintptr_t torso_yaw_mov;         // mov esi,[bg_swingSpeed.value] (torso call only)
    uintptr_t torso_yaw_dvar;        // ...its operand's original target
    uintptr_t movefrac_fmul_op;      // operand of fmul [0.3f] (movementYaw pull)
    uintptr_t movefrac_const;        // ...its original target
};

// uo_cgame_mp_x86.dll 1.51 RVAs, BG_PlayerAngles. UO's function is CoD1's plus one
// extra block (a 0x106000 flags test before the torso yaw swing), so every site
// moved; CoD1 RVAs in brackets.
//
// push imm32 args of BG_SwingAngles(dest, swingTol, clampTol, speed, angle, swinging):
//   300047aa  68 00 00 16 43   push 150.0   ; clampTolerance
//   300047af  68 00 00 20 42   push 40.0    ; legs swingTolerance        [0x451d]
//   30004832  68 9a 99 19 3e   push 0.15    ; torso pitch speed           [0x459d]
//   30004837  68 00 00 34 42   push 45.0
constexpr uintptr_t CGAME_SWING_LEGS_TOLERANCE_PUSH_RVA  = 0x47af;
constexpr uintptr_t CGAME_SWING_TORSO_PITCH_SPEED_PUSH_RVA = 0x4832;

// torso yaw swingSpeed dvar: 300046f8  8b 35 08 ff 44 30  mov esi,[0x3044ff08]  [0x4468]
// redirect operand at +2. (CoD1's dvar was 0x301dd388.) Read at 5 sites, torso first,
// then the legs (0x4726/0x4780/0x4796/0x47a5): only the torso one is redirected -
// forcing the legs to 1.0 snaps them to the walk direction (the aimwalk bug).
constexpr uintptr_t CGAME_SWING_TORSO_YAW_MOV_RVA  = 0x46f8;
constexpr uintptr_t CGAME_SWING_TORSO_YAW_DVAR_RVA = 0x44ff08;

// torso yaw movementYaw*0.3 + view: 300046a9  d8 0d a0 be 07 30  fmul [0.3f]  [0x444b]
// redirect operand at 0x46ab. The 0.3f constant has 9 readers, so it cannot be edited.
constexpr uintptr_t CGAME_SWING_TORSO_YAW_MOVEFRAC_FMUL_RVA  = 0x46ab;
constexpr uintptr_t CGAME_SWING_TORSO_YAW_MOVEFRAC_CONST_RVA = 0x7bea0;

constexpr SwingSites CGAME_SWING_SITES = {
    CGAME_SWING_LEGS_TOLERANCE_PUSH_RVA, CGAME_SWING_TORSO_PITCH_SPEED_PUSH_RVA,
    CGAME_SWING_TORSO_YAW_MOV_RVA, CGAME_SWING_TORSO_YAW_DVAR_RVA,
    CGAME_SWING_TORSO_YAW_MOVEFRAC_FMUL_RVA, CGAME_SWING_TORSO_YAW_MOVEFRAC_CONST_RVA,
};

// uo_game_mp_x86.dll 1.51 (base 0x20000000): the same four code sites, byte for
// byte; bg_swingSpeed.value at 0x201ecac8 (.bss), the pooled 0.3f at 0x2007d138
// (10 readers here).
constexpr SwingSites GAME_SWING_SITES = {
    0x47af, 0x4832, 0x46f8, 0x1ecac8, 0x46ab, 0x7d138,
};

// SWING_SERVERINFO_CVAR (shared/coduoplus_shared.h): the SYSTEMINFO|ROM cvar a CoDUO+
// server - Windows (here) or Linux (server/linux/) - publishes while its game module
// carries the mirror. "1" = the server tests the swing-fixed pose; anything else = vanilla.

extern "C" float g_torso_yaw_speed_live;
extern "C" float g_torso_yaw_movefrac_live;

struct SwingFixConfig {
    bool  enable;
    float legs_tolerance;     // 40 -> 0
    float torso_pitch_speed;  // 0.15 -> 1.0
};

extern SwingFixConfig g_swing_fix_config;

// Bring one module's BG_PlayerAngles to the fixed (on=true) or vanilla values.
// Idempotent and cheap when already in that state (6 reads). Never writes unless
// every site holds either the vanilla or our value (wrong DLL -> no-op, logged once).
// Returns true when all four sites now hold the wanted state.
bool swing_set(HMODULE module, const SwingSites& sites, bool on, const char* who);

// Watcher ticks. Server: mirror into uo_game + publish SWING_SERVERINFO_CVAR.
// Client: follow the connected server's SWING_SERVERINFO_CVAR on the cgame.
void swing_server_tick();
void swing_client_tick();

}  // namespace patches

#endif
