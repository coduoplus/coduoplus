// Server-side client version gate: enforces a MINIMUM client version via the
// "coduoplus" userinfo cvar. Upstream relies on protocol 10 so only its own clients
// reach ClientConnect; the UO port keeps protocol 22 (protocol_patch.cpp), so vanilla
// players arrive here too and are let through by allow_unversioned - the gate only
// turns away CoDUO+ builds older than this one.

#include "netcode/version_gate.h"
#include "core/game.h"
#include "netcode/protocol_patch.h"  // g_net_version
#include "netcode/version_patch.h"   // g_short_version_buffer
#include "core/logger.h"

#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <stdint.h>

namespace patches {

VersionGateConfig g_version_gate_config;

namespace {

// uo_game_mp_x86.dll sites: per build, see VERSION_GATE_BUILDS in the header.
// (CoD1 game_mp_x86.dll: ClientConnect 0x1b130, call 0x2741a, syscall ptr 0x6d684.)

// Base of the uo_game the wrapper serves. The engine loads a new instance on every
// map change, and a dedicated server was seen loading it relocated (0x241f0000), so
// this is refreshed on every install, never assumed.
volatile uintptr_t g_game_base = 0;
char* (__cdecl *g_clientconnect_orig)(int, int) = nullptr;
char g_reject_msg[160] = "This server requires a newer coduoplus.";

// userinfo = "\key\val\key\val..."; return atoi of key's value, 0 if absent.
int info_value_int(const char* info, const char* key) {
    const size_t klen = strlen(key);
    const char* p = info;
    while (*p == '\\') {
        const char* k = p + 1;
        const char* ke = strchr(k, '\\');
        if (!ke) break;
        const char* v = ke + 1;
        const char* ve = strchr(v, '\\');
        const size_t vlen = ve ? (size_t)(ve - v) : strlen(v);
        if ((size_t)(ke - k) == klen && strncmp(k, key, klen) == 0) {
            char buf[32];
            const size_t n = vlen < 31 ? vlen : 31;
            memcpy(buf, v, n);
            buf[n] = '\0';
            return atoi(buf);
        }
        if (!ve) break;
        p = ve;
    }
    return 0;
}

char* __cdecl clientconnect_wrapper(int clientNum, int firstTime) {
    typedef int (__cdecl *syscall_t)(int, ...);
    syscall_t G_syscall = *(syscall_t*)(g_game_base + VERSION_GATE_SYSCALL_PTR_RVA);

    char userinfo[1024];
    userinfo[0] = '\0';
    if (G_syscall)
        G_syscall(G_GET_USERINFO, clientNum, (int)(uintptr_t)userinfo, (int)sizeof(userinfo));

    const int cver = info_value_int(userinfo, "coduoplus");
    const bool unversioned = (cver == 0);
    if (cver < g_net_version && !(unversioned && g_version_gate_config.allow_unversioned)) {
        logger::logf("version_gate: reject client %d (coduoplus=%d < %d)",
                     clientNum, cver, g_net_version);
        return g_reject_msg;
    }
    return g_clientconnect_orig ? g_clientconnect_orig(clientNum, firstTime) : nullptr;
}

const VersionGateSites* build_of(uintptr_t base) {
    auto dos = (const IMAGE_DOS_HEADER*)base;
    auto nt  = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    for (const VersionGateSites& b : VERSION_GATE_BUILDS)
        if (nt->FileHeader.TimeDateStamp == b.timestamp) return &b;
    return nullptr;
}

bool install_callsite_redirect(uintptr_t base, const VersionGateSites& b) {
    uint8_t* call = (uint8_t*)(base + b.cc_callsite);
    if (*call != 0xE8) {
        logger::logf("version_gate: callsite @+0x%lx not E8 (0x%02x), abort",
                     (unsigned long)b.cc_callsite, *call);
        return false;
    }
    const int32_t cur_rel = *(int32_t*)(call + 1);
    const uintptr_t cur_target = (uintptr_t)(call + 5) + cur_rel;
    if (cur_target != base + b.clientconnect) {
        logger::logf("version_gate: callsite -> 0x%08x != ClientConnect 0x%08x, abort",
                     (unsigned)cur_target, (unsigned)(base + b.clientconnect));
        return false;
    }
    g_game_base = base;
    g_clientconnect_orig = (char* (__cdecl*)(int, int))(base + b.clientconnect);
    const int32_t new_rel = (int32_t)((uintptr_t)&clientconnect_wrapper - (uintptr_t)(call + 5));
    DWORD old = 0;
    if (!VirtualProtect(call + 1, 4, PAGE_EXECUTE_READWRITE, &old)) return false;
    *(int32_t*)(call + 1) = new_rel;
    VirtualProtect(call + 1, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), call, 5);
    return true;
}

}  // namespace

void version_gate_start() {
    if (!port::GAME_VERSION) {
        logger::logf("version_gate: not ported - off");
        return;
    }
    logger::logf("version_gate: %s (min coduoplus=%d, unversioned/vanilla %s)",
                 g_version_gate_config.enable ? "on" : "disabled", g_net_version,
                 g_version_gate_config.allow_unversioned ? "allowed" : "REJECTED");
}

// Upstream installed this once from a thread and never looked again, so the gate
// silently disappeared at the first map change (the engine unloads the game module
// and loads a fresh copy). Checked every watcher tick instead: one read when in place.
void version_gate_tick() {
    if (!port::GAME_VERSION || !g_version_gate_config.enable || !game_exe_ok()) return;
    HMODULE gm = GetModuleHandleA(GAME_GAME_DLL);
    if (!gm) return;
    const uintptr_t base = (uintptr_t)gm;

    // A build we refused stays refused for that module instance: log once, not every tick.
    static uintptr_t s_refused_base = 0;
    if (base == s_refused_base) return;
    const VersionGateSites* b = build_of(base);
    if (!b) {
        if (s_refused_base != base) {
            s_refused_base = base;
            logger::logf("version_gate: unknown uo_game_mp_x86.dll build - gate not installed");
        }
        return;
    }
    const uint8_t* call = (const uint8_t*)(base + b->cc_callsite);
    const uintptr_t target = (uintptr_t)(call + 5) + *(const int32_t*)(call + 1);
    if (*call == 0xE8 && target == (uintptr_t)&clientconnect_wrapper) return;  // in place

    snprintf(g_reject_msg, sizeof(g_reject_msg),
             "Update coduoplus to %s+ to join this server.", g_short_version_buffer);
    if (install_callsite_redirect(base, *b))
        logger::logf("version_gate: ClientConnect gate installed in %s build at 0x%08x "
                     "(min coduoplus=%d)", b->label, (unsigned)base, g_net_version);
    else
        s_refused_base = base;
}

}  // namespace patches
