// Server-side lag compensation, CoDUOMP.exe side. See antilag.h; the rewind itself
// is src/shared/antilag_core.cpp (also in the Linux server library).
//
// Two redirects per game-module load, both plain `call rel32` rewrites like the
// version gate's (no prologue is stolen):
//   - vmMain's call to G_RunFrame -> antilag_runframe: original frame, then record();
//   - the five root calls of Bullet_Fire_Extended -> antilag_shot_thunk. The thunk
//     can't simply call the original - that would shift its stack arguments by our
//     return address - so it rewinds, swaps the caller's return address for
//     antilag_shot_post and jumps into the original with the caller's exact stack and
//     registers. The original returns into antilag_shot_post, which restores and
//     jumps back to the real return address. Root shots never nest (the recursive
//     calls go straight to the original), and a nested one would just not rewind.

#include "netcode/antilag.h"
#include "core/game.h"
#include "core/logger.h"
#include "netcode/protocol_patch.h"   // CODMP_CVAR_GET_VA, CODMP_CVAR_COUNT_VA
#include "netcode/version_gate.h"     // VERSION_GATE_SYSCALL_PTR_RVA
#include "shared/antilag_core.h"

#include <windows.h>
#include <cstdio>
#include <cstdint>

namespace patches {

AntilagConfig g_antilag_config;

namespace {

typedef void* (__cdecl* Cvar_Get_t)(const char*, const char*, int);
typedef int   (__cdecl* syscall_t)(int, ...);
typedef void  (__cdecl* G_RunFrame_t)(int);
constexpr int CVAR_OFF_INTEGER = 0x20;   // cvar_t layout, same as settings_menu.cpp

uintptr_t      g_base = 0;               // module the redirects are in
uint8_t*       g_entities = nullptr;
G_RunFrame_t   g_runframe = nullptr;
void*          g_cv_mode = nullptr;      // cvar_t* g_antilag
void*          g_cv_max = nullptr;       // cvar_t* g_antilagMaxMs
int            g_last_mode = -1, g_last_max = -1;

int cv_int(void* cv, int dflt) { return cv ? *(const int*)((const char*)cv + CVAR_OFF_INTEGER) : dflt; }

void __cdecl link_entity(void* ent) {
    syscall_t sc = *(syscall_t*)(g_base + VERSION_GATE_SYSCALL_PTR_RVA);
    if (sc) sc(G_LINKENTITY, ent);
}

}  // namespace

// ---- hooks (game thread) ------------------------------------------------------------
extern "C" {
void* g_antilag_extended = nullptr;      // original Bullet_Fire_Extended
void* g_antilag_real_ret = nullptr;      // caller's return address while rewound

void __cdecl antilag_runframe(int level_time) {
    g_runframe(level_time);
    const int mode = cv_int(g_cv_mode, 0), max_ms = cv_int(g_cv_max, ANTILAG_MAXMS_DEFAULT);
    if (mode != g_last_mode || max_ms != g_last_max) {
        logger::logf("antilag: %s=%d %s=%d%s", ANTILAG_CVAR, mode, ANTILAG_MAXMS_CVAR, max_ms,
                     mode ? "" : " (off: shots hit where players are now)");
        g_last_mode = mode;
        g_last_max = max_ms;
    }
    antilag::record(g_entities, level_time, link_entity);
}

// args -> arg1 of Bullet_Fire_Extended: [0]=self [1]=attacker ... [5]=depth
int __cdecl antilag_shot_pre(const uint32_t* args) {
    const int mode = cv_int(g_cv_mode, 0);
    if (mode <= 0 || args[5] != 0 || antilag::active()) return 0;
    const int shooter = antilag::shooter_of(g_entities, (const void*)(uintptr_t)args[1],
                                            (const void*)(uintptr_t)args[0]);
    if (shooter < 0) return 0;
    antilag::Shot s;
    const bool rewound =
        antilag::rewind(g_entities, shooter, cv_int(g_cv_max, ANTILAG_MAXMS_DEFAULT), link_entity, &s);
    if (mode >= 2)
        logger::logf("antilag: client %d shot, world at %d (cmd %d, %d ms back), %d player(s) moved, max %.1f units",
                     shooter, s.target_time, s.cmd_time, s.now - s.target_time, s.moved, s.max_dist);
    return rewound ? 1 : 0;
}

void __cdecl antilag_shot_restore() { antilag::restore(g_entities, link_entity); }

void antilag_shot_post();

__attribute__((naked)) void antilag_shot_thunk() {
    asm(
        "pushal\n\t"
        "pushfl\n\t"
        "leal 40(%esp), %eax\n\t"             // 36 bytes pushed: [esp+36] = return, +40 = arg1
        "pushl %eax\n\t"
        "call _antilag_shot_pre\n\t"
        "addl $4, %esp\n\t"
        "testl %eax, %eax\n\t"
        "jz 1f\n\t"
        "movl 36(%esp), %eax\n\t"             // rewound: come back through the post hook
        "movl %eax, _g_antilag_real_ret\n\t"
        "movl $_antilag_shot_post, 36(%esp)\n\t"
        "1:\n\t"
        "popfl\n\t"
        "popal\n\t"
        "jmp *_g_antilag_extended\n\t");
}

__attribute__((naked)) void antilag_shot_post() {
    asm(
        "pushl %eax\n\t"                      // Bullet_Fire_Extended's int result
        "pushl %ecx\n\t"
        "pushl %edx\n\t"
        "pushfl\n\t"
        "call _antilag_shot_restore\n\t"
        "popfl\n\t"
        "popl %edx\n\t"
        "popl %ecx\n\t"
        "popl %eax\n\t"
        "jmp *_g_antilag_real_ret\n\t");
}
}  // extern "C"

// ---- install ------------------------------------------------------------------------
namespace {

const AntilagSites* build_of(uintptr_t base) {
    auto dos = (const IMAGE_DOS_HEADER*)base;
    auto nt = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    for (const AntilagSites& b : ANTILAG_BUILDS)
        if (nt->FileHeader.TimeDateStamp == b.timestamp) return &b;
    return nullptr;
}

uintptr_t call_target(uintptr_t site) { return site + 5 + *(const int32_t*)(site + 1); }

bool is_call_to(uintptr_t site, uintptr_t target) {
    return *(const uint8_t*)site == 0xE8 && call_target(site) == target;
}

bool redirect(uintptr_t site, void* to) {
    const int32_t rel = (int32_t)((uintptr_t)to - (site + 5));
    DWORD old = 0;
    if (!VirtualProtect((void*)(site + 1), 4, PAGE_EXECUTE_READWRITE, &old)) return false;
    *(int32_t*)(site + 1) = rel;
    VirtualProtect((void*)(site + 1), 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)site, 5);
    return true;
}

}  // namespace

void antilag_start() {
    if (!port::GAME_ANTILAG) return;
    logger::logf("antilag: hosting default %s=%d %s=%d (ini antilag / antilag_max_ms)",
                 ANTILAG_CVAR, g_antilag_config.enable ? 1 : 0, ANTILAG_MAXMS_CVAR,
                 g_antilag_config.max_ms);
}

void antilag_tick() {
    if (!port::GAME_ANTILAG || !game_exe_ok()) return;
    HMODULE gm = GetModuleHandleA(GAME_GAME_DLL);
    if (!gm) return;
    const uintptr_t base = (uintptr_t)gm;
    // The module is reloaded on every map change, at the same base: "in place" is
    // decided by the call site itself, a refusal is remembered per module instance.
    static uintptr_t s_refused = 0;
    if (base == s_refused) return;
    const AntilagSites* b = build_of(base);
    if (!b) {
        s_refused = base;
        logger::logf("antilag: unknown uo_game_mp_x86.dll build - not installed");
        return;
    }
    if (is_call_to(base + b->runframe_site, (uintptr_t)&antilag_runframe)) return;   // in place
    // every site must still be the original call (a second mod, a patched DLL: leave it)
    bool ok = is_call_to(base + b->runframe_site, base + b->runframe);
    for (uintptr_t s : b->shot_sites) ok = ok && is_call_to(base + s, base + b->extended);
    if (!ok) {
        s_refused = base;
        logger::logf("antilag: %s build, but a hook site is not the stock call - not installed", b->label);
        return;
    }
    // cvars: serverinfo so browsers show it; +set / rcon values win over the ini default
    if (*(volatile int*)CODMP_CVAR_COUNT_VA > 0 && !g_cv_mode) {
        const Cvar_Get_t Cvar_Get = (Cvar_Get_t)CODMP_CVAR_GET_VA;
        char d[16];
        snprintf(d, sizeof(d), "%d", g_antilag_config.enable ? 1 : 0);
        g_cv_mode = Cvar_Get(ANTILAG_CVAR, d, CODUOPLUS_CVAR_SERVERINFO);
        snprintf(d, sizeof(d), "%d", g_antilag_config.max_ms);
        g_cv_max = Cvar_Get(ANTILAG_MAXMS_CVAR, d, 0);
    }

    g_base = base;
    g_entities = (uint8_t*)(base + ANTILAG_ENTITIES_RVA);
    g_runframe = (G_RunFrame_t)(base + b->runframe);
    g_antilag_extended = (void*)(base + b->extended);
    antilag::reset();
    g_last_mode = -1;
    bool all = redirect(base + b->runframe_site, (void*)&antilag_runframe);
    for (uintptr_t s : b->shot_sites) all = redirect(base + s, (void*)&antilag_shot_thunk) && all;
    logger::logf("antilag: %s in %s build at 0x%08x (%s / %s control it)",
                 all ? "installed" : "PARTLY installed", b->label, (unsigned)base, ANTILAG_CVAR,
                 ANTILAG_MAXMS_CVAR);
}

}  // namespace patches
