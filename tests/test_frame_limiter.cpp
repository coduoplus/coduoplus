// test_frame_limiter.cpp - runs the REAL wait (src/performance/frame_limiter.cpp) inside
// a copy of UO's PATCHED Com_Frame loop, against a fake engine, and checks the rules
// the wait must obey (see frame_limiter.cpp):
//
//   - exact cadence: the average frame period is 1000/com_maxfps (within 1 %);
//   - no doubled frame, even when a frame's own work overruns now and then;
//   - the reported clock is the ENGINE's (within a tick of Sys_Milliseconds);
//   - it is CONTINUOUS: at 250 fps the whole-ms delta Com_Frame computes is 4 on
//     (nearly) every frame - the stepped Sys_Milliseconds would give 3s and 5s -
//     and at 333 it is 3 (a 4 about once a second carries the remainder);
//   - each limited frame drains the event loop exactly once, AFTER the late mouse
//     read (that order is what puts the late mouse into this frame's command);
//   - com_maxfps 0 and dedicated reproduce the vanilla loop (minMsec = 1).
//
// Upstream's equivalent is tools/test_frame_limiter.cpp in cod1plus/client 1.6.9.
// Timing-based: it raises its priority and the 1 ms timer like the game does.

#include "performance/frame_limiter.h"

#include <windows.h>
#include <timeapi.h>
#include <immintrin.h>
#include <cmath>
#include <cstdio>

using namespace patches;

// ---- fake engine -----------------------------------------------------------------
static int   g_maxfps = 250, g_dedicated = 0;
static DWORD g_base = 0;          // sys_timeBase
static int   g_last_time = 0;     // Com_Frame's lastTime
static int   g_event_calls = 0, g_late_calls = 0, g_order_errors = 0;
static int fake_maxfps()    { return g_maxfps; }
static int fake_dedicated() { return g_dedicated; }
static int fake_engine_ms() { return (int)(timeGetTime() - g_base); }
static int fake_event_loop() { ++g_event_calls; return fake_engine_ms(); }
static void fake_late_input() { ++g_late_calls; if (g_late_calls != g_event_calls + 1) ++g_order_errors; }
static const FrameLimiterEngine g_fake = {
    fake_maxfps, fake_dedicated, fake_engine_ms, fake_event_loop, &g_last_time, fake_late_input,
};

// ---- harness ---------------------------------------------------------------------
static LARGE_INTEGER g_freq;
static double qpc_ms() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return (double)t.QuadPart * 1000.0 / g_freq.QuadPart; }
static void busy_ms(double ms) { const double end = qpc_ms() + ms; while (qpc_ms() < end) _mm_pause(); }

struct Stats {
    int frames = 0, doubled = 0, exact_delta = 0;
    double period_sum = 0, period_max = 0;
    long epoch_err_max = 0;
};

// UO's Com_Frame with the limiter patch in: the loop's jl is NOPed, so it is one pass:
//   com_frameTime = WAIT(); if (lastTime > com_frameTime) lastTime = com_frameTime;
//   msec = com_frameTime - lastTime;  lastTime = com_frameTime;  ... frame's work ...
static Stats run(int maxfps, int frames, double stall_ms, int stall_every) {
    Stats s;
    g_maxfps = maxfps; g_dedicated = 0;
    const double nominal = 1000.0 / maxfps;
    const int expect_delta = (int)lround(nominal);
    g_last_time = fake_engine_ms();
    double prev_end = 0;
    for (int f = 0; f < frames + 5; ++f) {
        const int t = frame_wait(g_fake);
        const double end = qpc_ms();
        const long err = labs((long)t - (long)fake_engine_ms());
        if (err > s.epoch_err_max) s.epoch_err_max = err;
        if (g_last_time > t) g_last_time = t;
        const int msec = t - g_last_time;
        g_last_time = t;
        if (f >= 5) {   // skip the warm-up frames (first deadline, calibration)
            const double period = end - prev_end;
            ++s.frames;
            s.period_sum += period;
            if (period > s.period_max) s.period_max = period;
            if (period > 1.6 * nominal) ++s.doubled;
            if (msec == expect_delta) ++s.exact_delta;
        }
        prev_end = end;
        if (stall_every && f % stall_every == stall_every - 1) busy_ms(stall_ms);
    }
    return s;
}

static int g_failed = 0;
static void check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? "OK" : "FAIL", what);
    if (!ok) ++g_failed;
}

int main() {
    QueryPerformanceFrequency(&g_freq);
    timeBeginPeriod(1);
    {   // Windows 11 ignores a windowless process's timer request unless it opts out
        // (process_priority.cpp does the same for the game)
        struct { ULONG Version, ControlMask, StateMask; } st = { 1, 0x1 | 0x4, 0 };
        typedef BOOL (WINAPI *SetProcessInformation_t)(HANDLE, int, LPVOID, DWORD);
        auto spi = (SetProcessInformation_t)GetProcAddress(GetModuleHandleA("kernel32.dll"), "SetProcessInformation");
        if (spi) spi(GetCurrentProcess(), 4, &st, sizeof(st));
    }
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    g_base = timeGetTime() - 12345;   // the game has been running 12.3 s

    struct Case { int maxfps; double stall; int every; const char* name; };
    const Case cases[] = {
        { 250, 0.0, 0,  "250 fps" },
        { 250, 4.8, 20, "250 fps, a 4.8 ms frame every 20" },
        { 125, 0.0, 0,  "125 fps" },
        { 125, 8.6, 20, "125 fps, an 8.6 ms frame every 20" },
        { 333, 0.0, 0,  "333 fps (odd step)" },
        { 333, 3.6, 20, "333 fps, a 3.6 ms frame every 20" },
    };
    const int N = 750;
    printf("%-36s %9s %9s %8s %8s %6s\n", "case", "avg ms", "max ms", "doubled", "exact", "epoch");
    for (const Case& c : cases) {
        frame_limiter_reset();
        g_event_calls = g_late_calls = g_order_errors = 0;
        const Stats s = run(c.maxfps, N, c.stall, c.every);
        const double nominal = 1000.0 / c.maxfps;
        const double avg = s.period_sum / s.frames;
        printf("%-36s %9.4f %9.3f %8d %7.1f%% %6ld\n", c.name, avg, s.period_max, s.doubled,
               100.0 * s.exact_delta / s.frames, s.epoch_err_max);
        char what[160];
        snprintf(what, sizeof(what), "%s: average period %.4f ms = 1000/%d within 1%%", c.name, avg, c.maxfps);
        check(fabs(avg - nominal) / nominal < 0.01, what);
        snprintf(what, sizeof(what), "%s: never a doubled frame (%d)", c.name, s.doubled);
        check(s.doubled == 0, what);
        snprintf(what, sizeof(what), "%s: reports the engine clock (max error %ld ms <= 2)", c.name, s.epoch_err_max);
        check(s.epoch_err_max <= 2, what);
        snprintf(what, sizeof(what), "%s: one event-loop drain per frame, after the late mouse (%d/%d, %d out of order)",
                 c.name, g_event_calls, g_late_calls, g_order_errors);
        check(g_event_calls == N + 5 && g_late_calls == N + 5 && g_order_errors == 0, what);
        if (!c.every && (c.maxfps == 250 || c.maxfps == 333)) {
            snprintf(what, sizeof(what), "%s: continuous clock - %.1f%% of deltas are exactly %d ms",
                     c.name, 100.0 * s.exact_delta / s.frames, (int)lround(1000.0 / c.maxfps));
            check(s.exact_delta >= s.frames * 95 / 100, what);
        }
    }

    // vanilla paths: com_maxfps 0 and dedicated run the engine's own loop - the event
    // loop is drained until at least 1 engine-ms passed, no late mouse, engine's value
    for (int mode = 0; mode < 2; ++mode) {
        frame_limiter_reset();
        g_maxfps = mode == 0 ? 0 : 250;
        g_dedicated = mode == 1;
        g_event_calls = g_late_calls = 0;
        g_last_time = fake_engine_ms();
        int ok = 1;
        for (int f = 0; f < 50; ++f) {
            const int t = frame_wait(g_fake);
            ok &= (t - g_last_time >= 1) && (t == fake_engine_ms() || t == fake_engine_ms() - 1);
            g_last_time = t;
        }
        char what[160];
        snprintf(what, sizeof(what), "%s: vanilla loop (minMsec 1, engine's own clock, no late mouse: %d)",
                 mode == 0 ? "com_maxfps 0" : "dedicated", g_late_calls);
        check(ok && g_late_calls == 0 && g_event_calls >= 50, what);
    }

    timeEndPeriod(1);
    printf("%s\n", g_failed ? "FAILED" : "ALL OK");
    return g_failed ? 1 : 0;
}
