// Unit test for src/shared/antilag_core.cpp on a fake g_entities / gclient array laid
// out like the game's (same offsets, same strides). Runs anywhere (portable label).

#include "shared/antilag_core.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace patches::antilag;

namespace {

int g_fail = 0, g_pass = 0;
#define CHECK(c) do { if (c) ++g_pass; else { ++g_fail; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

bool near(float a, float b, float eps = 0.01f) { return std::fabs(a - b) <= eps; }

constexpr uint32_t CLIENT_SIZE = 0x4734;
std::vector<uint8_t> g_ents(1024 * ENT_STRIDE);
std::vector<uint8_t> g_clients(MAX_CLIENTS * CLIENT_SIZE);
int g_links = 0;
void fake_link(void*) { ++g_links; }

uint8_t* ent(int i) { return g_ents.data() + i * ENT_STRIDE; }
uint8_t* cl(int i) { return g_clients.data() + i * CLIENT_SIZE; }
void wr_int(uint8_t* p, int v) { memcpy(p, &v, 4); }
void wr3(uint8_t* p, float x, float y, float z) { float v[3] = { x, y, z }; memcpy(p, v, 12); }
float rd(uint8_t* p, int k) { float v; memcpy(&v, p + 4 * k, 4); return v; }

void make_player(int i) {
    uint8_t* e = ent(i);
    const uint32_t c = (uint32_t)(uintptr_t)cl(i);
    memcpy(e + ENT_CLIENT, &c, 4);
    e[ENT_INUSE] = 1;
    wr_int(e + ENT_HEALTH, 100);
    wr_int(cl(i) + CL_SESSIONSTATE, 0);
    wr_int(cl(i) + CL_CONNECTED, 2);
}

void reset_world() {
    std::fill(g_ents.begin(), g_ents.end(), 0);
    std::fill(g_clients.begin(), g_clients.end(), 0);
    reset();
    g_links = 0;
}

// players 0..3; player 1 runs along x at 10 units per 25 ms frame, yaw 350 -> +4/frame
void run_frames(int frames, int t0 = 0) {
    for (int f = 0; f < frames; ++f) {
        const int t = t0 + f * 25;
        wr3(ent(1) + ENT_ORIGIN, 10.0f * f, 0, 0);
        wr3(ent(1) + ENT_ANGLES, 0, std::fmod(350.0f + 4.0f * f, 360.0f), 0);
        wr3(ent(2) + ENT_ORIGIN, 500, 500, 0);
        wr3(ent(0) + ENT_ORIGIN, -100, 0, 0);
        record(g_ents.data(), t, fake_link);
    }
}

}  // namespace

int main() {
    // ---- interpolation between two frames, angle wrap, shooter left alone ----------
    reset_world();
    for (int i = 0; i < 4; ++i) make_player(i);
    run_frames(10);                                     // t = 0..225
    wr_int(cl(0) + CL_CMD_SERVERTIME, 160);             // between 150 (f6) and 175 (f7)
    Shot s;
    CHECK(rewind(g_ents.data(), 0, 300, fake_link, &s));
    CHECK(s.now == 225 && s.cmd_time == 160 && s.target_time == 160);
    CHECK(near(rd(ent(1) + ENT_ORIGIN, 0), 64.0f));     // 60 + 0.4*10
    // yaw: f6 = 14, f7 = 18 -> 15.6 ; and wrap: f2 = 358, f3 = 2 handled below
    CHECK(near(rd(ent(1) + ENT_ANGLES, 1), 15.6f));
    CHECK(near(rd(ent(0) + ENT_ORIGIN, 0), -100.0f));   // shooter not moved
    CHECK(s.moved == 3);                                // 1, 2, 3 (3 stands at origin)
    CHECK(active());
    CHECK(near(s.max_dist, 90.0f - 64.0f));             // player 1: 90 -> 64
    // nested shot while rewound: refused
    CHECK(!rewind(g_ents.data(), 0, 300, fake_link, nullptr));
    restore(g_ents.data(), fake_link);
    CHECK(!active());
    CHECK(near(rd(ent(1) + ENT_ORIGIN, 0), 90.0f) && near(rd(ent(1) + ENT_ANGLES, 1), 26.0f));
    CHECK(g_links == 6);                                // 3 moved + 3 restored

    // ---- yaw crossing 360 -> 0 interpolates the short way ------------------------
    wr_int(cl(0) + CL_CMD_SERVERTIME, 60);              // f2 (50, yaw 358) .. f3 (75, yaw 2)
    CHECK(rewind(g_ents.data(), 0, 300, fake_link, &s));
    const float yaw = rd(ent(1) + ENT_ANGLES, 1);       // 358 + 0.4*4 = 359.6
    CHECK(near(yaw, 359.6f));
    restore(g_ents.data(), fake_link);

    // ---- cap: a 999 ms client only goes back max_ms -----------------------------
    wr_int(cl(0) + CL_CMD_SERVERTIME, -800);
    CHECK(rewind(g_ents.data(), 0, 100, fake_link, &s));
    CHECK(s.target_time == 125);                        // 225 - 100
    CHECK(near(rd(ent(1) + ENT_ORIGIN, 0), 50.0f));     // frame 5
    restore(g_ents.data(), fake_link);

    // ---- a client ahead of the server is treated as "now" -----------------------
    wr_int(cl(0) + CL_CMD_SERVERTIME, 9999);
    CHECK(rewind(g_ents.data(), 0, 300, fake_link, &s));
    CHECK(s.target_time == 225 && near(rd(ent(1) + ENT_ORIGIN, 0), 90.0f));
    restore(g_ents.data(), fake_link);

    // ---- dead / spectating / disconnected players are never moved ---------------
    wr_int(ent(2) + ENT_HEALTH, 0);
    wr_int(cl(3) + CL_SESSIONSTATE, 2);
    wr_int(cl(0) + CL_CMD_SERVERTIME, 160);
    CHECK(rewind(g_ents.data(), 0, 300, fake_link, &s));
    CHECK(s.moved == 1);                                // only player 1
    restore(g_ents.data(), fake_link);
    wr_int(ent(2) + ENT_HEALTH, 100);
    wr_int(cl(3) + CL_SESSIONSTATE, 0);

    // ---- a target that was dead in the frames around t is left where it is ------
    reset_world();
    for (int i = 0; i < 3; ++i) make_player(i);
    wr_int(ent(1) + ENT_HEALTH, 0);
    run_frames(4);                                      // player 1 dead for t 0..75
    wr_int(ent(1) + ENT_HEALTH, 100);
    run_frames(4, 100);                                 // alive t 100..175, x 0..30
    wr_int(cl(0) + CL_CMD_SERVERTIME, 60);
    rewind(g_ents.data(), 0, 300, fake_link, &s);
    CHECK(near(rd(ent(1) + ENT_ORIGIN, 0), 30.0f));     // untouched: no history then
    restore(g_ents.data(), fake_link);

    // ---- teleport between two frames: no lerp across the map --------------------
    reset_world();
    make_player(0); make_player(1);
    wr3(ent(1) + ENT_ORIGIN, 0, 0, 0);    record(g_ents.data(), 0, fake_link);
    wr3(ent(1) + ENT_ORIGIN, 2000, 0, 0); record(g_ents.data(), 25, fake_link);
    wr_int(cl(0) + CL_CMD_SERVERTIME, 5);
    rewind(g_ents.data(), 0, 300, fake_link, &s);
    CHECK(near(rd(ent(1) + ENT_ORIGIN, 0), 0.0f));      // nearer frame, not x=400
    restore(g_ents.data(), fake_link);

    // ---- time going backwards (map_restart) drops the history -------------------
    record(g_ents.data(), 10, fake_link);               // < 25
    wr_int(cl(0) + CL_CMD_SERVERTIME, 0);
    rewind(g_ents.data(), 0, 300, fake_link, &s);
    CHECK(s.now == 10);
    restore(g_ents.data(), fake_link);

    // ---- missed restore is healed by the next record -----------------------------
    reset_world();
    make_player(0); make_player(1);
    wr3(ent(1) + ENT_ORIGIN, 0, 0, 0);  record(g_ents.data(), 0, fake_link);
    wr3(ent(1) + ENT_ORIGIN, 10, 0, 0); record(g_ents.data(), 25, fake_link);
    wr_int(cl(0) + CL_CMD_SERVERTIME, 0);
    CHECK(rewind(g_ents.data(), 0, 300, fake_link, &s));
    CHECK(near(rd(ent(1) + ENT_ORIGIN, 0), 0.0f));
    record(g_ents.data(), 50, fake_link);               // restore first, then record
    CHECK(!active() && near(rd(ent(1) + ENT_ORIGIN, 0), 10.0f));

    // ---- shooter_of: only player-slot entities with a client ---------------------
    CHECK(shooter_of(g_ents.data(), ent(1), nullptr) == 1);
    CHECK(shooter_of(g_ents.data(), ent(200), ent(0)) == 0);    // turret ent, then player
    CHECK(shooter_of(g_ents.data(), ent(5), nullptr) == -1);     // no client
    CHECK(shooter_of(g_ents.data(), ent(1) + 4, nullptr) == -1); // not an entity start
    // no history at all: nothing to do
    reset();
    CHECK(!rewind(g_ents.data(), 0, 300, fake_link, &s));

    printf("antilag: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
