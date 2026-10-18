// Server-side lag compensation for a CoDUOMP.exe server (uo_game_mp_x86.dll). The
// history/rewind itself is src/shared/antilag_core.cpp, shared with the Linux server
// library; this file finds the hook points in the game module. docs/NETCODE.md.

#ifndef COD1RELOADED_ANTILAG_H
#define COD1RELOADED_ANTILAG_H

#include <stdint.h>

#include "shared/coduoplus_shared.h"

namespace patches {

struct AntilagConfig {
    bool enable = ANTILAG_DEFAULT != 0;   // default of g_antilag (ini: antilag)
    int  max_ms = ANTILAG_MAXMS_DEFAULT;  // default of g_antilagMaxMs (ini: antilag_max_ms)
    bool pose = ANTILAG_POSE_DEFAULT != 0; // default of g_antilagPose (ini: antilag_pose)
};
extern AntilagConfig g_antilag_config;

// Hook points per uo_game_mp_x86.dll build (recognised by PE link timestamp, like
// VERSION_GATE_BUILDS). Every address >= 0x20023270 is 16 bytes later in the 2005
// build (one fix in Cmd_GC grew the code); data did not move.
//
//   G_RunFrame(levelTime)            cdecl, called once, from vmMain case 12
//   Bullet_Fire_Extended(self, attacker, start, end, damage, depth, wp, self)
//                                    cdecl. Its root calls (depth 0) are the five
//                                    sites below: FireWeapon (Bullet_Fire inlined),
//                                    the Bullet_Fire wrapper turrets use, and three
//                                    vehicle/gunner paths. The two penetration calls
//                                    inside the function itself are left alone, so a
//                                    whole shot is one rewind.
struct AntilagSites {
    uint32_t    timestamp;      // IMAGE_FILE_HEADER.TimeDateStamp
    const char* label;
    uintptr_t   runframe;       // RVA: G_RunFrame
    uintptr_t   runframe_site;  // RVA: E8 in vmMain
    uintptr_t   extended;       // RVA: Bullet_Fire_Extended
    uintptr_t   shot_sites[5];  // RVA: E8 root calls of Bullet_Fire_Extended
};
constexpr AntilagSites ANTILAG_BUILDS[] = {
    {0x41b532ae, "1.51 (2004-12-07)", 0x2cf30, 0x2ac15, 0x55900,
     {0x56961, 0x558eb, 0x4bcdf, 0x4c2a8, 0x4c62a}},
    {0x41e30286, "2005-01-10",        0x2cf40, 0x2ac25, 0x55910,
     {0x56971, 0x558fb, 0x4bcef, 0x4c2b8, 0x4c63a}},
};
constexpr uintptr_t ANTILAG_ENTITIES_RVA = 0x118d40;  // g_entities[1024], both builds
constexpr int       G_LINKENTITY         = 53;        // syscall -> SV_LinkEntity (0x467050)

// Pose rewind: G_DObjCalcPose is three syscalls around the controllers callback
// (uo_game 0x20052800 / 2005 build 0x20052810: 111 CreateSkelForBones -> if 0: 115
// CalcAnim, ent+0x230 (ent, mask), 116 CalcSkel), so it is redone here through the
// syscall pointer instead of calling MSVC code that takes the entity in esi. The
// skeleton it leaves is read from the engine's cache (CoDUOMP.exe 1.51, syscall 111
// at 0x45cfe0): entnum -> short index -> info (0x5c each) -> +4 skeleton, +8 frame.
constexpr int       G_DOBJ_CREATESKEL    = 111;
constexpr int       G_DOBJ_CALCANIM      = 115;
constexpr int       G_DOBJ_CALCSKEL      = 116;
constexpr uint32_t  ENT_CONTROLLERS      = 0x230;     // void (*)(gentity*, int* mask)
constexpr uintptr_t CODMP_DOBJ_INDEX_VA  = 0x968918;  // short[] entnum -> info index
constexpr uintptr_t CODMP_DOBJ_INFO_VA   = 0x969228;  // info[]: +4 skeleton +8 frame +0x17 bones
constexpr uintptr_t CODMP_DOBJ_FRAME_VA  = 0x4927eb0; // skeleton frame counter

// Log configuration (DllMain).
void antilag_start();

// Watcher tick: on every uo_game_mp_x86.dll load (each map change), register the
// g_antilag cvars and redirect the hook sites. A no-op unless this process hosts.
void antilag_tick();

}  // namespace patches

#endif
