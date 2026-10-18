/*
 * antilag_core.h - server-side lag compensation, the part both servers share.
 *
 * Compiled into the Windows mod (mss32.dll, src/netcode/antilag.cpp hooks
 * uo_game_mp_x86.dll) and the Linux server library (server/linux/coduoplus_sv.cpp
 * hooks game.mp.uo.i386.so). Pure memory work on the game's own entity array, no OS
 * headers: the platform files find the array, the link function and the hook points,
 * this file does the history and the rewind. docs/NETCODE.md#lag-compensation.
 *
 * How a shot is compensated:
 *   - end of every server frame (G_RunFrame): record()   every live player's
 *     r.currentOrigin, r.currentAngles, r.mins/maxs and - with a PoseSource - the
 *     posed skeleton (the hit boxes), stamped with level.time;
 *   - a bullet is fired (root Bullet_Fire* call): rewind() moves every OTHER live player
 *     to where they were at the shooter's usercmd serverTime - the time the shooter's
 *     client was drawing - interpolated between the two recorded frames around it,
 *     capped at max_ms, and relinks them;
 *   - the trace (and its penetrations, damage, death) runs against that world;
 *   - restore() puts every moved player back, exactly, and relinks.
 *
 * Hit boxes = pose + placement. The server places a player's skeleton in the world
 * from r.currentAngles (ent+0x148) and r.currentOrigin (ent+0x13c)
 * (G_DObjGetWorldBoneIndexMatrix, game.mp.uo.i386.so 0x790b4). The skeleton itself is
 * computed by G_DObjCalcPose (anim tree -> controllers -> bones) into an engine-side
 * block cached per server frame: DObj info +4 = skeleton, +8 = the frame it belongs to,
 * block = 0x30 bytes of "bone computed" masks + 96 bytes per bone, bone count at info
 * +0x17 (CoDUOMP.exe 0x45cfe0 / coduo_lnxded 0x808ea25, syscall 111). A trace that
 * needs hit locations reuses that block when its masks say the bones are done. So
 * recording the block after each frame and copying an old one over the current one
 * for the length of a shot rewinds the pose - stance, lean, aim pitch - exactly as the
 * server computed it then. Without a PoseSource only placement is rewound.
 */
#ifndef CODUOPLUS_ANTILAG_CORE_H
#define CODUOPLUS_ANTILAG_CORE_H

#include <stdint.h>

namespace patches {
namespace antilag {

static_assert(sizeof(void*) == 4, "the game's structures are 32-bit");

// gentity_t / gclient_t fields. Identical in uo_game_mp_x86.dll 1.51 + 1.51b (MSVC)
// and game.mp.uo.i386.so 1.51b (GCC): same source, plain int/float members. Each one
// was read off both builds; tests/site_check*.cpp re-checks the anchors.
constexpr uint32_t ENT_STRIDE        = 0x34c;   // sizeof(gentity_t)
constexpr uint32_t ENT_ORIGIN        = 0x13c;   // r.currentOrigin (vec3)
constexpr uint32_t ENT_ANGLES        = 0x148;   // r.currentAngles (vec3)
constexpr uint32_t ENT_CLIENT        = 0x160;   // gclient_t*, null for non-players
constexpr uint32_t ENT_INUSE         = 0x16c;   // byte, G_InitGentity sets 1
constexpr uint32_t ENT_HEALTH        = 0x240;   // int, G_Damage subtracts from it
constexpr uint32_t CL_SESSIONSTATE   = 0x4504;  // 0 playing, 1 dead, 2 spectator
constexpr uint32_t CL_CONNECTED      = 0x4520;  // 2 = CON_CONNECTED
constexpr uint32_t CL_CMD_SERVERTIME = 0x4524;  // pers.cmd.serverTime (trap_GetUsercmd)

constexpr int MAX_CLIENTS = 64;
constexpr int SLOTS       = 128;               // 3.2 s at sv_fps 40, 6.4 s at 20
constexpr float TELEPORT_DIST = 256.0f;        // more than this between two frames: no lerp

constexpr uint32_t ENT_MINS          = 0x108;   // r.mins (vec3): prone/crouch boxes are lower
constexpr uint32_t ENT_MAXS          = 0x114;   // r.maxs (vec3)
constexpr int POSE_SLOTS  = 16;                // poses kept: 400 ms at sv_fps 40, 800 ms at 20

typedef void (*LinkFn)(void* ent);             // trap_LinkEntity

// Engine side of the pose, per platform (src/netcode/antilag.cpp, server/linux).
struct PoseSource {
    bool     (*has_dobj)(int entnum);                     // a DObj to pose at all
    void     (*calc_pose)(void* ent);                     // G_DObjCalcPose, every bone
    uint8_t* (*skeleton)(int entnum, uint32_t* size);     // this frame's block, or null
};

struct Shot {
    int   shooter;      // client number
    int   now;          // newest recorded level.time
    int   cmd_time;     // shooter's usercmd serverTime
    int   target_time;  // what the world was rewound to (cmd_time, capped)
    int   moved;        // players moved
    int   posed;        // of those, how many also got their old pose
    float max_dist;     // largest displacement, units
};

// Pose rewind on (source given) or off (null). Set once per game-module load.
void set_pose_source(const PoseSource* src);

// First client with a recorded pose, and that skeleton's size (for the log). -1 = none.
int recorded_pose(uint32_t* size);

// A new game module (map load / restart): forget the history.
void reset();

// End of every G_RunFrame, with the level time that frame ran at. `ents` = g_entities.
// Also self-heals: if a restore was ever missed, it is done here first.
void record(uint8_t* ents, int level_time, LinkFn link);

// Client number of the first argument that is a live player's entity, else -1.
// (Player weapons pass the player twice; a mounted turret passes the turret and the
// player using it.)
int shooter_of(const uint8_t* ents, const void* a, const void* b);

// Rewind every live player except `shooter`. true = something moved (then restore()
// MUST follow). Fills `out` (optional) either way.
bool rewind(uint8_t* ents, int shooter, int max_ms, LinkFn link, Shot* out);

// Put back everything rewind() moved, relinking each entity.
void restore(uint8_t* ents, LinkFn link);

// A rewind is in place (nested shots are not compensated twice).
bool active();

}  // namespace antilag
}  // namespace patches

#endif /* CODUOPLUS_ANTILAG_CORE_H */
