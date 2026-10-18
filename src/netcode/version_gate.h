#ifndef COD1RELOADED_VERSION_GATE_H
#define COD1RELOADED_VERSION_GATE_H

#include <windows.h>
#include <stdint.h>

namespace patches {

struct VersionGateConfig {
    bool enable = true;
    bool allow_unversioned = true; // allow client coduoplus==0 (vanilla players, bots)
};

extern VersionGateConfig g_version_gate_config;

// Where the gate hooks, per uo_game_mp_x86.dll build. The two builds seen in the
// wild compile ClientConnect to the same place but vmMain 16 bytes apart, so the
// build is recognised by its PE link timestamp and only a known build is hooked.
struct VersionGateSites {
    uint32_t    timestamp;      // IMAGE_FILE_HEADER.TimeDateStamp of uo_game_mp_x86.dll
    const char* label;
    uintptr_t   clientconnect;  // RVA: char* ClientConnect(int clientNum, int firstTime) cdecl
    uintptr_t   cc_callsite;    // RVA: the E8 in vmMain's GAME_CLIENT_CONNECT (cmd 4) case
};
constexpr VersionGateSites VERSION_GATE_BUILDS[] = {
    // stock 1.51 (pakuo07.pk3, 2004-12-07). vmMain 0x2002ab40, call at +0x59.
    {0x41b532ae, "1.51 (2004-12-07)", 0x1dbb0, 0x2ab99},
    // later build (2005-01-10) some installs/servers carry. vmMain 0x2002ab50.
    {0x41e30286, "2005-01-10",        0x1dbb0, 0x2aba9},
};
// Both builds: dllEntry is `mov eax,[esp+4] ; mov [0x20088278],eax ; ret` - the
// engine's syscall entry point, which every trap_ goes through.
constexpr uintptr_t VERSION_GATE_SYSCALL_PTR_RVA = 0x88278;
// G_GET_USERINFO: syscall 33 -> SV_GetUserinfo (CoDUOMP.exe 0x45f0d0, the function
// with "SV_GetUserinfo: bufferSize == %i"). Same number as CoD1.
constexpr int G_GET_USERINFO = 33;

// Log the configuration (DllMain).
void version_gate_start();
// Watcher tick: whenever a uo_game_mp_x86.dll is loaded (the engine loads a fresh
// one on every map change), redirect its ClientConnect call so the server rejects
// clients whose "coduoplus" userinfo version is below ours. Vanilla players carry
// no such key and are let in (allow_unversioned).
void version_gate_tick();

}  // namespace patches

#endif
