#ifndef COD1RELOADED_PROTOCOL_PATCH_H
#define COD1RELOADED_PROTOCOL_PATCH_H

#include <windows.h>
#include <stdint.h>

namespace patches {

// CoDUOMP.exe 1.51 (base 0x400000). The network protocol is 22 (0x16) and lives
// as an imm8 at six places (CoD1 has five: UO also registers a "protocol" cvar).
// Each VA below is the imm8 byte itself, the instruction starts one byte earlier.
constexpr uintptr_t CODMP_PROTO_SERVER_CMP_VA  = 0x459b93; // SV_DirectConnect  459b91 83 fe 16  cmp esi,22 (gatekeeper)
constexpr uintptr_t CODMP_PROTO_CLIENT_EMIT_VA = 0x411f32; // CL_ConnectionlessPacket 411f31 6a 16 ; userinfo "protocol" (va "%i")
constexpr uintptr_t CODMP_PROTO_GETINFO_VA     = 0x461789; // SVC_Info          461788 6a 16 ; infoResponse "protocol"
constexpr uintptr_t CODMP_PROTO_CVAR_VA        = 0x46054a; // SV_Init           460549 6a 16 ; Cvar_Get("protocol", va("%i"), ROM)
constexpr uintptr_t CODMP_PROTO_BOT_VA         = 0x45c52f; // SV_AddBot         45c52e 6a 16 ; bot userinfo "\protocol\%d"
constexpr uintptr_t CODMP_PROTO_REJECTMSG_VA   = 0x459bcd; // SV_DirectConnect  459bcc 6a 16 ; "(should be %i)" cosmetic
constexpr uint8_t   PROTOCOL_VANILLA_UO        = 22;

// Master hostname string, shared by CL_GlobalServers (4164e4), SV_MasterHeartbeat
// (460f4e/460f5d/460f87) and its resolver. Overwritten in place; the slot is 28
// bytes ("coduomaster.activision.com" + 2 NULs) before the next string at 0x5930b8.
constexpr uintptr_t CODMP_MASTER_HOST_VA  = 0x59309c;
constexpr uintptr_t CODMP_MASTER_CACHE_VA = 0x9cd930; // heartbeat's cached netadr (type at +0); zero to force re-resolve
constexpr size_t    CODMP_MASTER_HOST_MAX = 27;

// Server browser. CL_GlobalServers (41653c) formats "getservers %s" with Cmd_Argv(2),
// and the UI passes the protocol there ("globalservers %d %d full empty", 22); the
// field is 14 bytes + 2 padding before the hostname. CL_ServerInfoPacket (415647)
// drops servers whose advertised protocol differs from a .rdata float (22.0f, only
// used there) unless debug_protocol is set.
constexpr uintptr_t CODMP_GETSERVERS_FMT_VA    = 0x59308c;
constexpr size_t    CODMP_GETSERVERS_FMT_LEN   = 14;     // "getservers %s\0"
constexpr uintptr_t CODMP_BROWSER_PROTO_FLT_VA = 0x5b9f24;

extern int  g_protocol_version;                 // hardcoded: 22 = vanilla UO (see protocol_patch.cpp)
extern char g_master_host[CODMP_MASTER_HOST_MAX + 1]; // hardcoded: "" = keep the engine's master
extern int  g_net_version;                      // 16 = "1.6" feature level; client cvar + server min

// Second server list. Every piece of the protocol patch is a memory poke, so the
// client can be moved between two ecosystems at runtime without a restart:
//   mode 0 = primary -> g_protocol_version, g_master_host
//   mode 1 = legacy  -> protocol 22, coduomaster.activision.com (stock UO)
// Probed 2026-10-17: coduomaster.activision.com:20610 (CNAME to demonware) answers
// `getservers 22` with ~111 servers. While the primary is also vanilla UO (the
// default, see protocol_patch.cpp) both modes reach the same list.
constexpr const char* CODMP_MASTER_HOST_LEGACY = "coduomaster.activision.com";  // 26 chars, fits
constexpr int         PROTOCOL_LEGACY_15       = PROTOCOL_VANILLA_UO;

// Cvar API (CoDUOMP.exe 1.51 @0x400000). cvar_t is the plain Quake 3 struct,
// identical to CoD1's: name+0 string+4 resetString+8 latched+0xc flags+0x10
// modified+0x14 modificationCount+0x18 value+0x1c integer+0x20, stride 0x2c,
// array at 0x9827a0 (Cvar_Get 43d856..: index * 0x2c).
constexpr uintptr_t CODMP_CVAR_GET_VA   = 0x0043d9e0; // cvar_t* Cvar_Get(name, def, flags) cdecl
constexpr uintptr_t CODMP_CVAR_COUNT_VA = 0x04927ea0; // cvar count (>0 once cvar system is up)
// idTech3 cvar flags. USERINFO is 0x02, NOT 0x01 - 0x01 is ARCHIVE. Proven from
// CoDMP.exe itself: the engine registers `snaps` and `rate` (both userinfo cvars)
// with `push 3` = ARCHIVE|USERINFO, and `cl_maxpackets` (archive only) with `push 1`.
// With the old 0x01 value every cvar we published as "userinfo" was really archive-
// only, so it NEVER reached the server: that is why upstream's server always logged
// "no report (old client?)" even for an up-to-date client, and why the
// server-side version gate never saw a build number.
constexpr int       CVAR_ARCHIVE        = 0x01;
constexpr int       CVAR_USERINFO       = 0x02;
constexpr int       CVAR_ROM            = 0x40;

bool apply_protocol_patch();        // protocol bump + master repoint in CoDMP.exe
void register_client_version_cvar(); // register USERINFO "coduoplus" cvar (once, after Com_Init)
void netmode_tick();                // watcher: follow cod1x_masterlist, re-poke on change
int  netmode_current();             // 0 = 1.6, 1 = legacy 1.5

}  // namespace patches

#endif
