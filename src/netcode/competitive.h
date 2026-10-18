#ifndef COD1RELOADED_COMPETITIVE_H
#define COD1RELOADED_COMPETITIVE_H

#include <windows.h>
#include <stdint.h>

namespace patches {

// Competitive netconfig = a real 40-tick experience.
//
// CoD1 and UO are both tuned for sv_fps 20. On an sv_fps 40 server, vanilla clients
// still cap snaps at 30 (server floors snapshotMsec = 1000/snaps at 33ms) and
// cl_maxpackets at 100, so the extra 25ms snapshots are skipped and the server-computed
// scoreboard ping (SV_CalcPings = average snapshot send->ack time) inflates. Lifting
// the caps and forcing snaps/cl_maxpackets/rate lets delivery reach 40/s, so each
// player's reported ping reflects their real link instead of the tickrate mismatch.
// (UO's own netcode notes: coduo-1.6/analysis/netcode.md, same conclusions.)
//
// All VAs are in CoDUOMP.exe 1.51 (image base 0x400000), re-found 2026-10-17.
//
// NOTE on where each patch takes effect (CoDUOMP.exe is both client and dedicated server):
//   - snaps cap  = SERVER-side (SV_UserinfoChanged sets client->snapshotMsec). It only
//     matters on the machine acting as server; inert on a pure client. Run the mod on
//     the (Windows) dedicated/listen server too. Linux cod_lnxded = separate RE.
//   - cl_maxpackets cap = CLIENT-side (CL_WritePacket). Matters on each player's client.
//   - forcing snaps/rate (userinfo) makes the client actually REQUEST 40 (a lifted cap
//     alone does nothing if the client keeps requesting 20).

// snaps max clamp in SV_UserinfoChanged (CoDUOMP.exe 1.51, 0x45ba80):
//   45bbf4  83 f9 1e        cmp ecx,0x1e     ; snaps > 30 ?
//   45bbf7  7e 05           jle +5
//   45bbf9  b9 1e 00 00 00  mov ecx,0x1e     ; -> 30
// -> 0x28 (40). Both the compare and the assignment move together.
constexpr uintptr_t CODMP_SNAPS_CAP_CMP_VA = 0x45bbf6; // imm of "cmp ecx,0x1e"
constexpr uintptr_t CODMP_SNAPS_CAP_MOV_VA = 0x45bbfa; // imm of "mov ecx,0x1e"
constexpr uint8_t   CODMP_SNAPS_CAP_OLD    = 0x1e;     // 30

// cl_maxpackets upper clamp in CL_ReadyToSendPacket (0x40cb60):
//   40cc24  83 f8 64        cmp eax,0x64     ; > 100 -> Cvar_Set2("cl_maxpackets","100",1)
// -> 0x7d (125). As upstream, the "100" reset string is left alone (it is shared
// with the handicap default), so a value above 125 still snaps back to 100.
constexpr uintptr_t CODMP_MAXPACKETS_CAP_VA = 0x40cc26; // imm of "cmp eax,0x64"
constexpr uint8_t   CODMP_MAXPACKETS_OLD    = 0x64;     // 100

// cvar_t* __cdecl Cvar_Set(const char* name, const char* value): a force=1 thunk,
//   43df58  6a 01  push 1 / push value / push name / call Cvar_Set2 (0x43dc50)
constexpr uintptr_t CODMP_CVAR_SET_VA = 0x43df50;
// cvar_t* __cdecl Cvar_FindVar(const char* name) - hash walk over 0x9987a0 buckets
constexpr uintptr_t CODMP_CVAR_FINDVAR_VA2 = 0x43d8f0;

// ---- cvar LOCK (the CoD1 equivalent of cod2x's dvar limit clamping) ----------------
// CoD2 dvar_t carries limits.integer.min/max, so cod2x just narrows them and the engine
// refuses out-of-range values. CoD1's cvar_t is the plain Q3 one (name+0, string+4,
// flags+0x10, value+0x1c, integer+0x20, stride 0x2c) - NO limits field. The equivalent
// lever is CVAR_ROM: Cvar_Set2 (UO 0x43dc50) tests flags & 0x40 and bails with
// "%s is read only." - so a ROM cvar cannot be changed by the player's console/config.
// Crucially the ROM/INIT/CHEAT checks only run when force==0 (decompile: the whole
// block sits under `if (param_3 == 0)`), and Cvar_Set passes force=1, so WE keep
// full write access while the player is locked. Same layout and logic as CoD1.
constexpr uint32_t COMP_CVAR_ROM   = 0x40;   // "is read only"      (engine rejects set)
constexpr uint32_t COMP_CVAR_INIT  = 0x10;   // "is write protected"
constexpr int      COMP_CVAR_STRING = 0x04;  // char* cvar->string
constexpr int      COMP_CVAR_FLAGS = 0x10;
constexpr int      COMP_CVAR_INT   = 0x20;

// CVAR_SYSTEMINFO: the server broadcasts these cvars to every client (systeminfo
// configstring) and the client engine SETS them locally - this is exactly how
// sv_cheats reaches clients. CONFIRMED in CoDUOMP.exe 1.51: sv_cheats is registered
// with flags imm8 0x48 = SYSTEMINFO(0x08)|ROM(0x40) at 4 sites (0x41a138, 0x43e140,
// 0x46067d, 0x4c3b11). So a server-set "g_competitive" carrying 0x08 propagates to
// all clients, which is cod2x's DVAR_SYSTEMINFO model, natively supported by UO.
constexpr uint32_t COMP_CVAR_SYSTEMINFO = 0x08;

// Live systeminfo lookup. CL_SystemInfoChanged (CoD1 0x4176f0; UO 1.51 0x418b80)
// applies the server's systeminfo by walking its "\key\value" pairs and calling
// Cvar_Set2(force) on EACH KEY PRESENT - a key the new server does not publish keeps
// whatever the PREVIOUS server set. So "the cvar is non-empty" never meant "this
// server publishes it": a player leaving a competitive server for a fun server kept the
// spec. The engine's own truth is the CS_SYSTEMINFO
// configstring, which the gamestate parser rewrites for every server / map:
//   cl.gameState.stringOffsets[1] @0x495c6f8  (int, into stringData)
//   cl.gameState.stringData       @0x495e6f4  (MAX_GAMESTATE_CHARS = 0x5000 in UO)
// UO evidence: CL_SystemInfoChanged starts with
//   418b91  8b 35 f8 c6 95 04   mov esi,[0x495c6f8]        ; stringOffsets[CS_SYSTEMINFO]
//   418b97  8d b6 f4 e6 95 04   lea esi,[esi+0x495e6f4]    ; + stringData
// and CL_ParseGamestate errors with MAX_GAMESTATE_CHARS when dataCount would pass 0x5000
// (stringOffsets[] is 0x2000 bytes = 2048 configstrings, then 0x5000 of data).
// Offset 0 = the shared empty string (cleared at disconnect) -> nothing published.
constexpr uintptr_t CODMP_GS_SYSTEMINFO_OFF_VA = 0x0495c6f8;
constexpr uintptr_t CODMP_GS_STRINGDATA_VA     = 0x0495e6f4;
constexpr int       CODMP_MAX_GAMESTATE_CHARS  = 0x5000;

struct CompetitiveConfig {
    bool enable;         // fallback when the server sends no g_competitive signal
    int  snaps;          // force snaps=          (<=0 = don't force)
    int  cl_maxpackets;  // force cl_maxpackets=  (<=0 = don't force)
    int  rate;           // force rate=           (<=0 = don't force)
    // cod2x parity: lock the cvars so the player cannot change them at all.
    bool lock_cvars;     // ROM-lock the forced cvars (competitive_lock_cvars)
    int  maxfps_min;     // com_maxfps allowed range, cod2x = 125..250 (0 = don't touch)
    int  maxfps_max;
};

extern CompetitiveConfig g_competitive_config;

bool apply_competitive_caps();   // byte-patch the snaps/cl_maxpackets caps (once, at attach)
void competitive_force_cvars();  // enforce + ROM-lock the netconfig cvars (polled)
// Value of `key` in the CURRENT server's systeminfo configstring (false = not published
// / not connected). Use this, not the mirrored cvar: the cvar keeps a previous server's value.
bool systeminfo_value(const char* key, char* out, int outsz);

}  // namespace patches

#endif
