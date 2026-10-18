// cod2x BG_PlayerAngles swing fixes: model snaps to camera, no leg/torso lag.
// CoD:UO port: applied to the server's uo_game and the client's uo_cgame in
// step - see swing_fix.h for why one side alone is a hitbox desync.

#include "gameplay/swing_fix.h"
#include "core/game.h"
#include "core/logger.h"
#include "netcode/competitive.h"     // systeminfo_value, CODMP_CVAR_SET_VA, COMP_CVAR_*
#include "netcode/protocol_patch.h"  // CODMP_CVAR_GET_VA, CODMP_CVAR_COUNT_VA

#include <cstdio>
#include <cstring>

namespace patches {

// RESTORED (enzo 2026-07-31): the vanilla revert killed the validated feel. These
// values make the drawn torso/lean track the VIEW instantly (no dead zone) — which
// desyncs the drawing from the server's vanilla-swing hit skeleton by up to ~10u on
// leaners. The server-side counterpart (cod1plushookserver) must therefore FORCE its
// swing state to the view before posing for bullet tests: with the client locked to
// the view, the drawn pose is a pure function of the view angles — reproducible
// server-side with no history. THE TWO SIDES MUST STAY IN STEP.
// (CoD:UO: the server side is swing_server_tick() below, same values, same code.)
SwingFixConfig g_swing_fix_config = {
    /* enable             */ true,
    /* legs_tolerance     */ 0.0f,   // vanilla 40.0
    /* torso_pitch_speed  */ 1.0f,   // vanilla 0.15 (really the LEAN swing channel)
};

// torso yaw swingSpeed, read by redirected mov (patch 3). 1.0=locked to view, ~0.2=vanilla
extern "C" float g_torso_yaw_speed_live = 1.0f;

// movementYaw fraction added to torso yaw target, read by redirected fmul (patch 4). 0=lock to view, 0.3=vanilla
extern "C" float g_torso_yaw_movefrac_live = 0.0f;

namespace {

constexpr float VANILLA_LEGS_TOLERANCE    = 40.0f;
constexpr float VANILLA_TORSO_PITCH_SPEED = 0.15f;

bool write4(void* p, const void* v) {
    DWORD old = 0;
    if (!VirtualProtect(p, 4, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(p, v, 4);
    VirtualProtect(p, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 4);
    return true;
}

// `push imm32` holding either the vanilla float or ours -> set to the wanted one.
bool set_push_float(uintptr_t addr, float vanilla, float ours, bool on) {
    if (*(const uint8_t*)addr != 0x68) return false;  // not push imm32: wrong DLL
    float* imm = (float*)(addr + 1);
    const float want = on ? ours : vanilla;
    if (*imm == want) return true;
    if (*imm != vanilla && *imm != ours) return false;
    return write4(imm, &want);
}

// disp32 operand pointing at either the module's own global or ours -> set.
bool set_operand(uintptr_t op, uint32_t vanilla, uint32_t ours, bool on) {
    uint32_t* p = (uint32_t*)op;
    const uint32_t want = on ? ours : vanilla;
    if (*p == want) return true;
    if (*p != vanilla && *p != ours) return false;
    return write4(p, &want);
}

bool engine_ready() {
    return game_exe_ok() && *(volatile int*)CODMP_CVAR_COUNT_VA > 0;
}

}  // namespace

bool swing_set(HMODULE module, const SwingSites& s, bool on, const char* who) {
    if (!module) return false;
    const uintptr_t base = (uintptr_t)module;

    // The opcodes the operands belong to must be the expected ones before any write.
    const bool shapes_ok =
        *(const uint16_t*)(base + s.torso_yaw_mov) == 0x358b &&            // mov esi,[m32]
        *(const uint16_t*)(base + s.movefrac_fmul_op - 2) == 0x0dd8;        // fmul [m32]

    bool ok = shapes_ok;
    if (shapes_ok) {
        // patch 1: legs swingTolerance 40.0 -> legs_tolerance
        ok &= set_push_float(base + s.legs_tolerance_push, VANILLA_LEGS_TOLERANCE,
                             g_swing_fix_config.legs_tolerance, on);
        // patch 2: torso pitch (lean channel) swing speed 0.15 -> torso_pitch_speed
        ok &= set_push_float(base + s.torso_pitch_speed_push, VANILLA_TORSO_PITCH_SPEED,
                             g_swing_fix_config.torso_pitch_speed, on);
        // patch 3: torso yaw swingSpeed load -> g_torso_yaw_speed_live (torso only)
        ok &= set_operand(base + s.torso_yaw_mov + 2, (uint32_t)(base + s.torso_yaw_dvar),
                          (uint32_t)(uintptr_t)&g_torso_yaw_speed_live, on);
        // patch 4: head/weapon lock - 0.3*movementYaw pull -> g_torso_yaw_movefrac_live
        ok &= set_operand(base + s.movefrac_fmul_op, (uint32_t)(base + s.movefrac_const),
                          (uint32_t)(uintptr_t)&g_torso_yaw_movefrac_live, on);
    }

    // One log line per change of outcome, per side - this runs every 5 ms.
    static int last_client = -1, last_server = -1;
    int& last = (who && who[0] == 's') ? last_server : last_client;
    const int now = !ok ? 2 : (on ? 1 : 0);
    if (now != last) {
        last = now;
        if (!ok)
            logger::logf("  swing_fix [%s]: sites in module 0x%08x do not match - left alone "
                         "(wrong DLL build?)", who, (unsigned)base);
        else if (on)
            logger::logf("  swing_fix [%s]: ON in 0x%08x (legs_tol=%.2f pitch_spd=%.2f "
                         "yaw_spd=%.2f yaw_movefrac=%.2f)", who, (unsigned)base,
                         g_swing_fix_config.legs_tolerance, g_swing_fix_config.torso_pitch_speed,
                         g_torso_yaw_speed_live, g_torso_yaw_movefrac_live);
        else
            logger::logf("  swing_fix [%s]: vanilla in 0x%08x", who, (unsigned)base);
    }
    return ok;
}

void swing_server_tick() {
    if (!port::GAME_SWING || !engine_ready()) return;
    // Only a process that is running a server has the game module loaded (dedicated,
    // or a listen server / devmap). A pure client never publishes anything.
    HMODULE game = GetModuleHandleA(GAME_GAME_DLL);
    if (!game) return;

    const bool mirrored = g_swing_fix_config.enable &&
                          swing_set(game, GAME_SWING_SITES, true, "server");

    // Publish through systeminfo. Cvar_Get creates it once with SYSTEMINFO|ROM (the
    // player cannot set it); Cvar_Set (force) moves it. Re-sent on every change, and
    // the engine re-broadcasts systeminfo itself whenever a SYSTEMINFO cvar changes.
    static int published = -1;
    const int want = mirrored ? 1 : 0;
    if (want == published) return;
    typedef void* (__cdecl* Cvar_Get_t)(const char*, const char*, int);
    typedef void* (__cdecl* Cvar_Set_t)(const char*, const char*);
    ((Cvar_Get_t)CODMP_CVAR_GET_VA)(SWING_SERVERINFO_CVAR, "0", COMP_CVAR_SYSTEMINFO | COMP_CVAR_ROM);
    ((Cvar_Set_t)CODMP_CVAR_SET_VA)(SWING_SERVERINFO_CVAR, want ? "1" : "0");
    published = want;
    logger::logf("swing_fix: server publishes %s %d", SWING_SERVERINFO_CVAR, want);
}

void swing_client_tick() {
    if (!port::CGAME_SWING || !engine_ready()) return;
    HMODULE cgame = GetModuleHandleA(GAME_CGAME_DLL);
    if (!cgame) return;
    // The CURRENT server's systeminfo, not the cvar: a cvar would keep the previous
    // server's "1" after moving to a vanilla one (see systeminfo_value).
    char v[8];
    const bool server_mirrors = systeminfo_value(SWING_SERVERINFO_CVAR, v, sizeof(v)) &&
                                strcmp(v, "1") == 0;
    swing_set(cgame, CGAME_SWING_SITES, g_swing_fix_config.enable && server_mirrors, "client");
}

}  // namespace patches
