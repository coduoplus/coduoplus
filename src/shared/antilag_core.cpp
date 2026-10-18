// Server-side lag compensation, shared by the Windows mod and the Linux server
// library. See antilag_core.h.

#include "shared/antilag_core.h"

#include <cmath>
#include <cstring>

namespace patches {
namespace antilag {

namespace {

struct Vec3 { float v[3]; };

struct Sample {
    uint8_t valid;
    Vec3    origin;
    Vec3    angles;
};

struct Ring {
    int    time[SLOTS];
    Sample s[SLOTS][MAX_CLIENTS];
    int    head = -1;      // newest slot
    int    count = 0;      // filled slots
};
Ring g_ring;

struct Moved {
    int  num;
    Vec3 origin;
    Vec3 angles;
};
Moved g_moved[MAX_CLIENTS];
int   g_moved_count = 0;

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

// Where player `num` was at `t`. false = no usable history (not alive in the frames
// around t, or t older than the ring).
bool position_at(int num, int t, Vec3* org, Vec3* ang) {
    int after = -1, before = -1;
    for (int n = 0; n < g_ring.count; ++n) {
        const int slot = (g_ring.head - n + SLOTS) % SLOTS;
        if (g_ring.time[slot] >= t) after = slot;
        if (g_ring.time[slot] <= t) { before = slot; break; }
    }
    if (before < 0) return false;                    // older than the history
    if (after < 0) after = before;                    // newer than the newest frame
    const Sample& b = g_ring.s[before][num];
    const Sample& a = g_ring.s[after][num];
    if (!b.valid || !a.valid) return false;           // spawned or died in between
    const int tb = g_ring.time[before], ta = g_ring.time[after];
    if (ta == tb || dist(a.origin, b.origin) > TELEPORT_DIST) {
        // exact frame, or a teleport between the two frames: take the nearer one
        const Sample& s = (ta - t < t - tb) ? a : b;
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

}  // namespace

void reset() {
    g_ring.head = -1;
    g_ring.count = 0;
    g_moved_count = 0;     // a new module has new entities: nothing to put back
}

bool active() { return g_moved_count > 0; }

void record(uint8_t* ents, int level_time, LinkFn link) {
    if (!ents) return;
    if (g_moved_count > 0) restore(ents, link);       // self-heal, never expected
    // level time went backwards (map_restart reuses the module): old history is wrong
    if (g_ring.count > 0 && level_time < g_ring.time[g_ring.head]) reset();

    const int slot = (g_ring.head + 1) % SLOTS;
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        const uint8_t* e = ent_at(ents, i);
        Sample& s = g_ring.s[slot][i];
        s.valid = live_player(e) ? 1 : 0;
        if (s.valid) {
            get3(e + ENT_ORIGIN, &s.origin);
            get3(e + ENT_ANGLES, &s.angles);
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
    Shot shot = { shooter, 0, 0, 0, 0, 0.0f };
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
            if (!position_at(i, t, &org, &ang)) continue;
            Moved& m = g_moved[g_moved_count++];
            m.num = i;
            get3(e + ENT_ORIGIN, &m.origin);
            get3(e + ENT_ANGLES, &m.angles);
            const float d = dist(m.origin, org);
            if (d > shot.max_dist) shot.max_dist = d;
            put3(e + ENT_ORIGIN, org);
            put3(e + ENT_ANGLES, ang);
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
        uint8_t* e = ent_at(ents, g_moved[k].num);
        put3(e + ENT_ORIGIN, g_moved[k].origin);
        put3(e + ENT_ANGLES, g_moved[k].angles);
        // still the same, in-use entity (a kill leaves the corpse linked): relink at
        // its real position so its bounds are right again
        if (e[ENT_INUSE] && link) link(e);
    }
    g_moved_count = 0;
}

}  // namespace antilag
}  // namespace patches
