// Server-side lag compensation, shared by the Windows mod and the Linux server
// library. See antilag_core.h.

#include "shared/antilag_core.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace patches {
namespace antilag {

namespace {

struct Vec3 { float v[3]; };

struct Sample {
    uint8_t valid;
    Vec3    origin;
    Vec3    angles;
    Vec3    mins;
    Vec3    maxs;
};

struct Ring {
    int    time[SLOTS];
    Sample s[SLOTS][MAX_CLIENTS];
    int    head = -1;      // newest slot
    int    count = 0;      // filled slots
};
Ring g_ring;

// Poses: per client, POSE_SLOTS skeleton blocks of one size (the model's bone count).
// pose_time[k] says which ring frame block k holds; a size change (another model)
// drops that client's poses.
struct PoseStore {
    uint8_t* buf = nullptr;            // POSE_SLOTS * size
    uint32_t size = 0;
    int      time[POSE_SLOTS];
    uint8_t  valid[POSE_SLOTS];
};
PoseStore g_pose[MAX_CLIENTS];
const PoseSource* g_src = nullptr;

struct Moved {
    int      num;
    Vec3     origin, angles, mins, maxs;
    uint8_t* skel;                     // the current block we overwrote, or null
    uint32_t skel_size;
    uint8_t* saved;                    // its contents
};
Moved g_moved[MAX_CLIENTS];
uint8_t* g_saved[MAX_CLIENTS];         // per-client save buffers for current poses
uint32_t g_saved_cap[MAX_CLIENTS];
int      g_moved_count = 0;

inline uint8_t* ent_at(uint8_t* ents, int i) { return ents + (uint32_t)i * ENT_STRIDE; }

inline const uint8_t* client_of(const uint8_t* e) {
    uint32_t p;
    memcpy(&p, e + ENT_CLIENT, 4);
    return (const uint8_t*)(uintptr_t)p;
}

inline int rd_int(const uint8_t* p) { int v; memcpy(&v, p, 4); return v; }

// Alive, in the game, connected: the only entities the server tests bullets against
// as players. Such an entity is always linked (player_die keeps a corpse linked too),
// so relinking one never makes something solid that was not.
bool live_player(const uint8_t* e) {
    const uint8_t* cl = client_of(e);
    return cl && e[ENT_INUSE] && rd_int(e + ENT_HEALTH) > 0 &&
           rd_int(cl + CL_SESSIONSTATE) == 0 && rd_int(cl + CL_CONNECTED) == 2;
}

inline void get3(const uint8_t* p, Vec3* out) { memcpy(out->v, p, 12); }
inline void put3(uint8_t* p, const Vec3& in) { memcpy(p, in.v, 12); }

float angle_delta(float a, float b) {           // b - a, shortest way, in (-180, 180]
    float d = std::fmod(b - a, 360.0f);
    if (d > 180.0f) d -= 360.0f;
    if (d <= -180.0f) d += 360.0f;
    return d;
}

float dist(const Vec3& a, const Vec3& b) {
    const float x = a.v[0] - b.v[0], y = a.v[1] - b.v[1], z = a.v[2] - b.v[2];
    return std::sqrt(x * x + y * y + z * z);
}

// This frame's skeleton of a live player, computed if need be. null = none.
uint8_t* current_skeleton(uint8_t* e, int num, uint32_t* size) {
    if (!g_src || !g_src->has_dobj(num)) return nullptr;
    g_src->calc_pose(e);
    uint8_t* p = g_src->skeleton(num, size);
    return (p && *size >= 0x30) ? p : nullptr;
}

void record_pose(uint8_t* e, int num, int level_time, int ring_slot) {
    PoseStore& ps = g_pose[num];
    const int k = ring_slot % POSE_SLOTS;
    ps.valid[k] = 0;
    uint32_t size = 0;
    const uint8_t* p = current_skeleton(e, num, &size);
    if (!p) return;
    if (size != ps.size) {                      // first pose, or another model
        uint8_t* nb = (uint8_t*)realloc(ps.buf, (size_t)size * POSE_SLOTS);
        if (!nb) return;
        ps.buf = nb;
        ps.size = size;
        memset(ps.valid, 0, sizeof(ps.valid));
    }
    memcpy(ps.buf + (size_t)k * size, p, size);
    ps.time[k] = level_time;
    ps.valid[k] = 1;
}

// Ring slots on both sides of t: before (time <= t) and after (time >= t).
bool frames_around(int t, int* before, int* after) {
    *after = -1;
    *before = -1;
    for (int n = 0; n < g_ring.count; ++n) {
        const int slot = (g_ring.head - n + SLOTS) % SLOTS;
        if (g_ring.time[slot] >= t) *after = slot;
        if (g_ring.time[slot] <= t) { *before = slot; break; }
    }
    if (*before < 0) return false;                   // older than the history
    if (*after < 0) *after = *before;                // newer than the newest frame
    return true;
}

// Where player `num` was at `t`, plus the ring slot nearest to t (for its pose and
// bounds). false = no usable history (not alive in the frames around t).
bool position_at(int num, int t, Vec3* org, Vec3* ang, int* nearest) {
    int before, after;
    if (!frames_around(t, &before, &after)) return false;
    const Sample& b = g_ring.s[before][num];
    const Sample& a = g_ring.s[after][num];
    if (!b.valid || !a.valid) return false;           // spawned or died in between
    const int tb = g_ring.time[before], ta = g_ring.time[after];
    *nearest = (ta - t < t - tb) ? after : before;
    if (ta == tb || dist(a.origin, b.origin) > TELEPORT_DIST) {
        // exact frame, or a teleport between the two frames: take the nearer one
        const Sample& s = g_ring.s[*nearest][num];
        *org = s.origin;
        *ang = s.angles;
        return true;
    }
    const float f = (float)(t - tb) / (float)(ta - tb);
    for (int k = 0; k < 3; ++k) {
        org->v[k] = b.origin.v[k] + f * (a.origin.v[k] - b.origin.v[k]);
        ang->v[k] = b.angles.v[k] + f * angle_delta(b.angles.v[k], a.angles.v[k]);
    }
    return true;
}

// The recorded pose of `num` at ring slot `slot`, or null.
const uint8_t* pose_at(int num, int slot, uint32_t* size) {
    const PoseStore& ps = g_pose[num];
    const int k = slot % POSE_SLOTS;
    if (!ps.buf || !ps.valid[k] || ps.time[k] != g_ring.time[slot]) return nullptr;
    *size = ps.size;
    return ps.buf + (size_t)k * ps.size;
}

uint8_t* save_buffer(int num, uint32_t size) {
    if (g_saved_cap[num] < size) {
        uint8_t* nb = (uint8_t*)realloc(g_saved[num], size);
        if (!nb) return nullptr;
        g_saved[num] = nb;
        g_saved_cap[num] = size;
    }
    return g_saved[num];
}

}  // namespace

void set_pose_source(const PoseSource* src) {
    g_src = src;
    for (PoseStore& ps : g_pose) memset(ps.valid, 0, sizeof(ps.valid));
}

int recorded_pose(uint32_t* size) {
    for (int i = 0; i < MAX_CLIENTS; ++i)
        for (int k = 0; k < POSE_SLOTS; ++k)
            if (g_pose[i].buf && g_pose[i].valid[k]) { *size = g_pose[i].size; return i; }
    return -1;
}

void reset() {
    g_ring.head = -1;
    g_ring.count = 0;
    g_moved_count = 0;     // a new module has new entities: nothing to put back
    for (PoseStore& ps : g_pose) memset(ps.valid, 0, sizeof(ps.valid));
}

bool active() { return g_moved_count > 0; }

void record(uint8_t* ents, int level_time, LinkFn link) {
    if (!ents) return;
    if (g_moved_count > 0) restore(ents, link);       // self-heal, never expected
    // level time went backwards (map_restart reuses the module): old history is wrong
    if (g_ring.count > 0 && level_time < g_ring.time[g_ring.head]) reset();

    const int slot = (g_ring.head + 1) % SLOTS;
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        uint8_t* e = ent_at(ents, i);
        Sample& s = g_ring.s[slot][i];
        s.valid = live_player(e) ? 1 : 0;
        if (s.valid) {
            get3(e + ENT_ORIGIN, &s.origin);
            get3(e + ENT_ANGLES, &s.angles);
            get3(e + ENT_MINS, &s.mins);
            get3(e + ENT_MAXS, &s.maxs);
            record_pose(e, i, level_time, slot);
        } else {
            g_pose[i].valid[slot % POSE_SLOTS] = 0;
        }
    }
    g_ring.time[slot] = level_time;
    g_ring.head = slot;
    if (g_ring.count < SLOTS) ++g_ring.count;
}

int shooter_of(const uint8_t* ents, const void* a, const void* b) {
    if (!ents) return -1;
    const void* cand[2] = { a, b };
    for (const void* p : cand) {
        const uintptr_t lo = (uintptr_t)ents, hi = lo + MAX_CLIENTS * ENT_STRIDE;
        const uintptr_t v = (uintptr_t)p;
        if (v < lo || v >= hi || (v - lo) % ENT_STRIDE != 0) continue;
        const uint8_t* e = (const uint8_t*)p;
        if (client_of(e) && e[ENT_INUSE]) return (int)((v - lo) / ENT_STRIDE);
    }
    return -1;
}

bool rewind(uint8_t* ents, int shooter, int max_ms, LinkFn link, Shot* out) {
    Shot shot = { shooter, 0, 0, 0, 0, 0, 0.0f };
    bool moved_any = false;
    if (ents && link && shooter >= 0 && shooter < MAX_CLIENTS && g_ring.count > 0 &&
        g_moved_count == 0) {
        const uint8_t* cl = client_of(ent_at(ents, shooter));
        shot.now = g_ring.time[g_ring.head];
        shot.cmd_time = cl ? rd_int(cl + CL_CMD_SERVERTIME) : shot.now;
        int t = shot.cmd_time;
        if (t > shot.now) t = shot.now;                // a client ahead of the server
        if (max_ms < 0) max_ms = 0;
        if (shot.now - t > max_ms) t = shot.now - max_ms;
        shot.target_time = t;
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            if (i == shooter) continue;
            uint8_t* e = ent_at(ents, i);
            if (!live_player(e)) continue;
            Vec3 org, ang;
            int nearest;
            if (!position_at(i, t, &org, &ang, &nearest)) continue;
            Moved& m = g_moved[g_moved_count++];
            m.num = i;
            m.skel = nullptr;
            get3(e + ENT_ORIGIN, &m.origin);
            get3(e + ENT_ANGLES, &m.angles);
            get3(e + ENT_MINS, &m.mins);
            get3(e + ENT_MAXS, &m.maxs);
            const float d = dist(m.origin, org);
            if (d > shot.max_dist) shot.max_dist = d;
            const Sample& near = g_ring.s[nearest][i];
            put3(e + ENT_ORIGIN, org);
            put3(e + ENT_ANGLES, ang);
            put3(e + ENT_MINS, near.mins);              // a crouched box then, not now
            put3(e + ENT_MAXS, near.maxs);
            // pose: this frame's block, fully computed, overwritten by the old one
            uint32_t old_size = 0, cur_size = 0;
            const uint8_t* old_pose = pose_at(i, nearest, &old_size);
            uint8_t* cur = old_pose ? current_skeleton(e, i, &cur_size) : nullptr;
            uint8_t* save = (cur && cur_size == old_size) ? save_buffer(i, cur_size) : nullptr;
            if (save) {
                memcpy(save, cur, cur_size);
                memcpy(cur, old_pose, old_size);
                m.skel = cur;
                m.skel_size = cur_size;
                m.saved = save;
                ++shot.posed;
            }
            link(e);
        }
        shot.moved = g_moved_count;
        moved_any = g_moved_count > 0;
    }
    if (out) *out = shot;
    return moved_any;
}

void restore(uint8_t* ents, LinkFn link) {
    for (int k = g_moved_count - 1; k >= 0; --k) {
        Moved& m = g_moved[k];
        uint8_t* e = ent_at(ents, m.num);
        put3(e + ENT_ORIGIN, m.origin);
        put3(e + ENT_ANGLES, m.angles);
        put3(e + ENT_MINS, m.mins);
        put3(e + ENT_MAXS, m.maxs);
        // the block is still this frame's (no frame ran during the shot); if the engine
        // reports another one, ours was already dropped and there is nothing to undo
        if (m.skel && g_src) {
            uint32_t size = 0;
            if (g_src->has_dobj(m.num) && g_src->skeleton(m.num, &size) == m.skel && size == m.skel_size)
                memcpy(m.skel, m.saved, m.skel_size);
        }
        // still the same, in-use entity (a kill leaves the corpse linked): relink at
        // its real position so its bounds are right again
        if (e[ENT_INUSE] && link) link(e);
    }
    g_moved_count = 0;
}

}  // namespace antilag
}  // namespace patches
