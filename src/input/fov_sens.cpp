// m_fovscale: the same mouse feel at every FOV. See input/fov_sens.h for the site and
// input/fov_sens_math.h for the formula.

#include "input/fov_sens.h"
#include "input/fov_sens_math.h"

#include "core/game.h"
#include "core/logger.h"
#include "features/settings_menu.h"   // CODMP_CVAR_FINDVAR_VA
#include "netcode/protocol_patch.h"   // CODMP_CVAR_GET_VA, CVAR_ARCHIVE
#include "video/widescreen_fix.h"

#include <windows.h>
#include <cstring>

namespace patches {

// Read from the naked wrapper's asm: external linkage so -O2 keeps them.
extern "C" {
volatile int g_fovsens_on = 0;                       // m_fovscale, once the hook is in
const float* volatile g_fovsens_cg_fov_value = nullptr;  // &cg_fov.value of the hooked cgame
}

namespace {

typedef void* (__cdecl* Cvar_Get_t)(const char*, const char*, int);
const Cvar_Get_t Cvar_Get = (Cvar_Get_t)CODMP_CVAR_GET_VA;
constexpr int CVAR_OFF_VALUE   = 0x1c;   // cvar_t layout (CL_MouseMove reads +0x1c)
constexpr int CVAR_OFF_INTEGER = 0x20;

volatile float g_ref = (float)fov_sens::kDefaultRef;   // m_fovscale_ref, validated
volatile uintptr_t g_hooked_base = 0;                  // cgame the hook was put into

}  // namespace

// FOV scale for one frame, m_fovscale on. fov_x is what CG_CalcFov holds: the effective
// horizontal FOV (ADS zoom included) after Hor+. Called by fovsens_wrapper, game thread.
extern "C" float __cdecl fovsens_scale(float fov_x) {
    const float ref = g_ref;
    const uintptr_t base = g_hooked_base;
    const float ref_x = (base && widescreen_horplus_hooked(base))
                      ? widescreen_horplus_fov(ref) : ref;
    return (float)fov_sens::scale(fov_x, ref_x);
}

// Replaces `fdiv dword [cg_fov.value]`: ST0 = fov_x in, ST0 = scale out. EAX holds
// CG_CalcFov's return value at this point; it and ECX/EDX are preserved.
extern "C" __attribute__((naked)) void fovsens_wrapper() {
    asm(
        "cmpl $0, _g_fovsens_on\n\t"
        "jne 1f\n\t"
        "pushl %eax\n\t"                        // off: the stock instruction, verbatim
        "movl _g_fovsens_cg_fov_value, %eax\n\t"
        "fdivs (%eax)\n\t"
        "popl %eax\n\t"
        "ret\n"
        "1:\n\t"
        "pushl %eax\n\t"
        "pushl %ecx\n\t"
        "pushl %edx\n\t"
        "subl $4, %esp\n\t"
        "fstps (%esp)\n\t"                      // pop fov_x into the argument
        "call _fovsens_scale\n\t"               // result in ST0
        "addl $4, %esp\n\t"
        "popl %edx\n\t"
        "popl %ecx\n\t"
        "popl %eax\n\t"
        "ret\n\t"
    );
}

namespace {

bool hook_in(uintptr_t base) {
    const uint8_t* site = (const uint8_t*)(base + CGAME_FOVSENS_SITE_RVA);
    if (site[0] != 0xE8) return false;
    const int32_t rel = *(const int32_t*)(site + 1);
    return (uintptr_t)site + 5 + (uintptr_t)(intptr_t)rel == (uintptr_t)&fovsens_wrapper;
}

bool site_is_stock(uintptr_t base) {
    const uint8_t* s = (const uint8_t*)(base + CGAME_FOVSENS_SITE_RVA);
    return s[0] == 0xd8 && s[1] == 0x35 &&
           *(const uint32_t*)(s + 2) == (uint32_t)(base + CGAME_CG_FOV_VALUE_RVA) &&
           s[6] == 0xd9 && s[7] == 0x1d &&
           *(const uint32_t*)(s + 8) == (uint32_t)(base + CGAME_ZOOMSENS_RVA) &&
           s[12] == 0xc3;
}

// The 6 bytes are rewritten with ONE aligned 8-byte compare-exchange, so the game
// thread, which may be inside CG_CalcFov right now, sees either the old instruction or
// the new one, never half of each.
bool install(uintptr_t base) {
    if (hook_in(base)) { g_hooked_base = base; return true; }
    if (!site_is_stock(base)) {
        logger::logf("fov_sens: CG_CalcFov site @0x%08x is not the stock fdiv -> not installed",
                     (unsigned)(base + CGAME_FOVSENS_SITE_RVA));
        return false;
    }
    uint8_t* site = (uint8_t*)(base + CGAME_FOVSENS_SITE_RVA);
    const uintptr_t q = (uintptr_t)site & ~(uintptr_t)7;
    const size_t at = (uintptr_t)site - q;
    if (at + 6 > 8) {
        logger::logf("fov_sens: site straddles a qword -> not installed");
        return false;
    }
    g_fovsens_cg_fov_value = (const float*)(base + CGAME_CG_FOV_VALUE_RVA);  // before the jump exists

    uint8_t patch[6] = {0xE8, 0, 0, 0, 0, 0x90};
    const int32_t rel = (int32_t)((uintptr_t)&fovsens_wrapper - ((uintptr_t)site + 5));
    memcpy(patch + 1, &rel, 4);

    DWORD old = 0;
    if (!VirtualProtect((void*)q, 8, PAGE_EXECUTE_READWRITE, &old)) {
        logger::logf("fov_sens: VirtualProtect failed (%lu)", GetLastError());
        return false;
    }
    volatile LONGLONG* qp = (volatile LONGLONG*)q;
    const LONGLONG before = *qp;
    LONGLONG after = before;
    memcpy((uint8_t*)&after + at, patch, 6);
    const bool ok = InterlockedCompareExchange64(qp, after, before) == before;
    VirtualProtect((void*)q, 8, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)q, 8);
    if (!ok) {
        logger::logf("fov_sens: site changed while patching -> retry next tick");
        return false;
    }
    g_hooked_base = base;
    logger::logf("fov_sens: CG_CalcFov hooked @0x%08x (m_fovscale)",
                 (unsigned)(base + CGAME_FOVSENS_SITE_RVA));
    return true;
}

}  // namespace

void fov_sens_tick() {
    if (!port::CGAME_FOVSENS) return;

    static char* cv_on  = nullptr;
    static char* cv_ref = nullptr;
    if (!cv_on) {
        cv_on  = (char*)Cvar_Get("m_fovscale", "0", CVAR_ARCHIVE);
        cv_ref = (char*)Cvar_Get("m_fovscale_ref", "80", CVAR_ARCHIVE);
        if (!cv_on || !cv_ref) { cv_on = nullptr; return; }
        logger::logf("fov_sens: m_fovscale registered");
    }
    const bool want = *(const int*)(cv_on + CVAR_OFF_INTEGER) != 0;
    g_ref = (float)fov_sens::clamp_ref(*(const float*)(cv_ref + CVAR_OFF_VALUE));

    HMODULE cg = GetModuleHandleA(GAME_CGAME_DLL);
    const uintptr_t base = (uintptr_t)cg;
    bool hooked = cg && hook_in(base);
    if (want && cg && !hooked) {
        static DWORD last_try = 0;                 // a failing install must not spam the log
        const DWORD now = GetTickCount();
        if (now - last_try > 2000) {
            last_try = now;
            hooked = install(base);
        }
    }
    const int on = (want && hooked) ? 1 : 0;
    if (on != g_fovsens_on) {
        g_fovsens_on = on;
        logger::logf("fov_sens: m_fovscale %s (ref %.2f)", on ? "on" : "off", (double)g_ref);
    }
}

}  // namespace patches
