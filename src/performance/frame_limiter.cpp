// QPC frame limiter: engine ms-math caps maxfps=250 at ~240-248. Enforce the real
// value: exactly 250, exactly 333.
//
// CoD:UO port: Com_Frame's wait loop spins on Com_EventLoop (0x43abc0), so the
// replacement waits and then still calls it - see frame_limiter.h for the loop
// and why the CoD1 "replace the call with a wait" would starve a UO dedicated
// server. The loop's jl is removed, so each frame is ONE call of frame_wait():
//
//   1. WAIT for the QPC deadline (exact cadence, anchored on the deadline).
//   2. LATE MOUSE (upstream 1.6.9 809a439). The engine reads the mouse in IN_Frame,
//      before Com_Frame and so before the wait: movement made during the wait was
//      only seen next frame. Once the wait is over IN_MouseMove runs again (under
//      IN_Frame's own gate), queueing what moved meanwhile.
//   3. DRAIN EVENTS: the real Com_EventLoop. Its Sys_GetEvent pumps the Windows
//      queue (keys, buttons, wheel) and the packets, and dispatches the late mouse
//      event - CL_MouseEvent ADDS to cl.mouseDx - so CL_Frame builds this frame's
//      command from input a few microseconds old. One frame less click latency.
//   4. REPORT the time, obeying three rules (upstream 1.6.9 a9dd4a2):
//      a. THE ENGINE'S EPOCH. com_frameTime is not only the loop's yardstick:
//         CL_KeyState / IN_KeyUp subtract it from key events' Sys_Milliseconds
//         stamps. Another epoch (CoD1 used QPC-since-boot until 1.6.9) makes a
//         press count a whole frame and a release lose its fraction.
//      b. REAL time, never the deadline. Returning the theoretical deadline was
//         tried upstream and REVERTED: it is the clock of snapshot interpolation,
//         animation blending and stance transitions; a synthetic clock drifts from
//         the network timeline and remote player models flicker ("on voit le joueur
//         briller", reported live on crouch spam).
//      c. CONTINUOUS. Sys_Milliseconds steps with the timer interrupt, so at an
//         exact 4 ms period its whole-ms delta reads 3 or 5 every so often. A QPC
//         clock calibrated onto Sys_Milliseconds' epoch (checked every 1024 frames,
//         re-synced past 3 ms of drift) steps by exactly 4.
//
// With com_maxfps 0 or on a dedicated server (the engine's own condition for the
// cap) the wait is not in charge and the vanilla loop is reproduced exactly.

#include "performance/frame_limiter.h"
#include "core/logger.h"
#include "core/game.h"
#include "netcode/version_patch.h" // CODMP_PREFERRED_BASE

#include <cstdio>
#include <immintrin.h>   // _mm_pause — not pulled in transitively by older mingw <windows.h>
#include <timeapi.h>     // timeGetTime (winmm)

namespace patches {

FrameLimiterConfig g_frame_limiter_config = {
    /* enable           */ true,
    /* deadline_bias_us */ 0,
    /* late_input       */ true,
};

namespace {

LARGE_INTEGER g_qpc_freq = {};
LONGLONG      g_next_deadline = 0;   // theoretical, NOT the last wake time
LONGLONG      g_step_frac = 0;       // running remainder of freq/maxfps
int           g_last_maxfps = 0;
LONGLONG      g_epoch = 0;           // QPC count at which the engine's clock read 0
bool          g_epoch_set = false;
unsigned      g_calls = 0;
bool          g_applied = false;
// statistics of the current window (session report)
LONGLONG      g_prev_end = 0;
LONGLONG      g_st_max_ticks = 0;
long          g_st_frames = 0, g_st_late = 0, g_st_long = 0;

// --- the real engine, behind FrameLimiterEngine --------------------------------

int read_cvar_slot_int(uintptr_t slot_va) {
    void* dvar = *(void* volatile*)slot_va;
    if (!dvar) return 0;
    return *(int*)((char*)dvar + CODMP_DVAR_INTEGER_OFFSET);
}
int engine_maxfps()    { return read_cvar_slot_int(CODMP_COM_MAXFPS_DVAR_SLOT_VA); }
int engine_dedicated() { return read_cvar_slot_int(CODMP_COM_DEDICATED_DVAR_SLOT_VA); }

// Sys_Milliseconds(), exactly as the engine inlines it (same globals, same lazy init).
int engine_sys_milliseconds() {
    volatile int*   initialized = (volatile int*)CODMP_SYS_TIMEINIT_VA;
    volatile DWORD* base        = (volatile DWORD*)CODMP_SYS_TIMEBASE_VA;
    if (!*initialized) { *base = timeGetTime(); *initialized = 1; }
    return (int)(timeGetTime() - *base);
}

typedef int  (__cdecl *com_event_loop_fn)();
typedef void (__cdecl *engine_void_fn)();
com_event_loop_fn g_com_event_loop = nullptr;  // the real Com_EventLoop, set by the patch
int engine_event_loop() { return g_com_event_loop(); }

// the wait is over: sample the mouse again, under IN_Frame's own gate
void engine_late_input() {
    if (*(volatile int*)CODMP_MOUSE_INITIALIZED_VA && *(volatile int*)CODMP_MOUSE_ACTIVE_VA)
        ((engine_void_fn)CODMP_IN_MOUSEMOVE_VA)();
}

const FrameLimiterEngine g_engine = {
    engine_maxfps,
    engine_dedicated,
    engine_sys_milliseconds,
    engine_event_loop,
    (int*)CODMP_FRAME_LIMIT_LASTTIME_VA,
    engine_late_input,
};

// --- clock ----------------------------------------------------------------------

// our clock: QPC on the engine's epoch (rule 4a + 4c)
inline int clock_ms(LONGLONG qpc) {
    return (int)((qpc - g_epoch) * 1000 / g_qpc_freq.QuadPart);
}

void calibrate(LONGLONG qpc, const FrameLimiterEngine& e) {
    const LONGLONG cand = qpc - (LONGLONG)e.engine_ms() * g_qpc_freq.QuadPart / 1000;
    if (!g_epoch_set) { g_epoch = cand; g_epoch_set = true; return; }
    // the stepped clock alone accounts for +-1 ms between two samples; 3 is drift
    const LONGLONG diff_ms = (cand - g_epoch) * 1000 / g_qpc_freq.QuadPart;
    if (diff_ms >= 3 || diff_ms <= -3) {
        g_epoch = cand;
        static int s_logged = 0;
        if (s_logged++ < 5)
            logger::logf("frame_limiter: clock re-synced to Sys_Milliseconds (%lld ms apart)", diff_ms);
    }
}

inline void spin_once(LONGLONG remaining_ticks) {
    const LONGLONG remaining_us = remaining_ticks * 1000000LL / g_qpc_freq.QuadPart;
    // Sleep(1) can overshoot well past a millisecond even at 1ms timer
    // resolution; at 250fps the whole frame is 4ms, so only sleep while
    // there is real slack and spin the tail.
    if (remaining_us > 2500) Sleep(1);
    else                     _mm_pause();
}

// What the engine's own loop did around each Com_EventLoop call (43c6ac..43c6c9),
// now that its jl is gone: drain events until at least min_msec engine-ms passed
// since the last frame. Returns the engine time the frame runs at. (Vanilla path.)
int event_loop_until(const FrameLimiterEngine& e, int min_msec) {
    for (;;) {
        const int t = e.event_loop();
        if (*e.last_time > t) *e.last_time = t;  // clock went backwards: engine resyncs too
        if (t - *e.last_time >= min_msec) return t;
        _mm_pause();
    }
}

}  // namespace

void frame_limiter_stats(FrameLimiterStats* out, bool reset) {
    if (out) {
        if (g_qpc_freq.QuadPart == 0) QueryPerformanceFrequency(&g_qpc_freq);
        out->frames = g_st_frames;
        out->late = g_st_late;
        out->long_frames = g_st_long;
        out->max_frame_ms = g_qpc_freq.QuadPart
            ? (double)g_st_max_ticks * 1000.0 / (double)g_qpc_freq.QuadPart : 0.0;
    }
    if (reset) { g_st_frames = 0; g_st_late = 0; g_st_long = 0; g_st_max_ticks = 0; }
}

void frame_limiter_reset() {
    g_prev_end = 0;
    frame_limiter_stats(nullptr, true);
    g_next_deadline = 0;
    g_step_frac = 0;
    g_last_maxfps = 0;
    g_epoch_set = false;
    g_calls = 0;
}

int frame_wait(const FrameLimiterEngine& e) {
    if (g_qpc_freq.QuadPart == 0) {
        QueryPerformanceFrequency(&g_qpc_freq);
    }

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    const int maxfps    = e.maxfps();
    const int dedicated = e.dedicated();

    // Same condition the engine uses for its own cap (43c689..43c69b): a dedicated
    // server is paced by sv_fps inside the event loop's NET_Sleep, not com_maxfps.
    if (dedicated || !(maxfps > 0 && maxfps <= 10000)) {
        // Limiter not in charge: behave exactly like the vanilla loop, minMsec included.
        g_next_deadline = 0;
        g_prev_end = 0;
        const int min_msec = (!dedicated && maxfps > 0) ? 1000 / maxfps : 1;
        return event_loop_until(e, min_msec);
    }

    if (!g_epoch_set || (++g_calls & 1023) == 0) calibrate(now.QuadPart, e);

    // EXACT frame step: freq/maxfps rarely divides evenly, and truncating loses a
    // fraction of a tick every frame - visible as a permanent 1-2 fps shortfall.
    // Carry the remainder so the average is exactly maxfps.
    const LONGLONG step_int = g_qpc_freq.QuadPart / maxfps;
    const LONGLONG step_rem = g_qpc_freq.QuadPart % maxfps;
    const LONGLONG bias_ticks = (LONGLONG)g_frame_limiter_config.deadline_bias_us
                                * g_qpc_freq.QuadPart / 1000000LL;

    if (maxfps != g_last_maxfps || g_next_deadline == 0) {
        g_last_maxfps   = maxfps;
        g_step_frac     = 0;
        g_next_deadline = now.QuadPart + step_int + bias_ticks;
    }

    // 1. wait
    const LONGLONG deadline = g_next_deadline;
    while (now.QuadPart < deadline) {
        spin_once(deadline - now.QuadPart);
        QueryPerformanceCounter(&now);
    }
    if (now.QuadPart - deadline > g_qpc_freq.QuadPart / 1000) ++g_st_late;

    // ANCHOR ON THE DEADLINE, not on the wake time: waking late is normal (sleep
    // granularity, scheduling) and re-anchoring on it made every frame start its
    // budget late, so the rate drifted permanently below the target - the
    // "248 instead of 250" flicker. Advancing the theoretical clock instead makes
    // a late frame steal from the next one and the average stay exact.
    LONGLONG step = step_int;
    g_step_frac += step_rem;
    if (g_step_frac >= maxfps) { g_step_frac -= maxfps; step += 1; }
    g_next_deadline += step;

    // Hitch / alt-tab / map load: if we fell more than a few frames behind, do not
    // sprint to catch up - restart the cadence from now.
    if (now.QuadPart > g_next_deadline + 4 * step_int) {
        g_next_deadline = now.QuadPart + step_int + bias_ticks;
        g_step_frac = 0;
    }

    // 2. late mouse, 3. drain this frame's events
    if (g_frame_limiter_config.late_input && e.late_input) e.late_input();
    e.event_loop();

    // 4. report: continuous engine-epoch clock. The 1 ms floor is vanilla's minimum
    // frame (minMsec is never below 1 at <= 1000 fps); it only bites above 1000 fps.
    QueryPerformanceCounter(&now);
    int ret = clock_ms(now.QuadPart);
    if (*e.last_time > ret) *e.last_time = ret;   // as Com_Frame does right after the call
    while (ret - *e.last_time < 1) {
        _mm_pause();
        QueryPerformanceCounter(&now);
        ret = clock_ms(now.QuadPart);
    }

    ++g_st_frames;
    if (g_prev_end) {
        const LONGLONG period = now.QuadPart - g_prev_end;
        if (period > g_st_max_ticks) g_st_max_ticks = period;
        if (period > step_int + step_int / 2) ++g_st_long;
    }
    g_prev_end = now.QuadPart;
    return ret;
}

// __cdecl no-arg, returns engine ms in eax. Replaces the Com_EventLoop call in
// Com_Frame's wait loop (0x43c6a7).
extern "C" int __cdecl frame_wait_replacement() {
    return frame_wait(g_engine);
}

bool apply_frame_limiter_patch() {
    if (g_applied) return true;
    if (!game_exe_ok()) {
        logger::logf("frame_limiter: not CoDUOMP.exe 1.51 - skipped");
        return false;
    }
    if (!g_frame_limiter_config.enable) {
        logger::logf("frame_limiter: disabled in config, skipping");
        return true;
    }

    HMODULE exe = GetModuleHandleA(NULL);
    if (!exe) {
        logger::logf("frame_limiter: GetModuleHandleA(NULL) returned null");
        return false;
    }

    const uintptr_t exe_base = (uintptr_t)exe;
    const uintptr_t opcode_addr  = exe_base + CODMP_FRAME_LIMIT_CALL_OPCODE_RVA;
    const uintptr_t operand_addr = exe_base + CODMP_FRAME_LIMIT_CALL_OPERAND_RVA;

    // expect CALL rel32
    const uint8_t opcode = *(const uint8_t*)opcode_addr;
    if (opcode != 0xE8) {
        logger::logf("frame_limiter: opcode inattendu (0x%02x, attendu 0xE8)", opcode);
        return false;
    }

    // verify current target == Com_EventLoop (0x43abc0)
    const int32_t current_offset = *(const int32_t*)operand_addr;
    const uintptr_t current_target = (uintptr_t)((intptr_t)opcode_addr + 5 + current_offset);
    const uintptr_t expected_target = exe_base + (CODMP_FRAME_LIMIT_ORIGINAL_TARGET - CODMP_PREFERRED_BASE);
    if (current_target != expected_target) {
        logger::logf(
            "frame_limiter: cible inattendue 0x%08x (attendu 0x%08x) - patch annule",
            (unsigned)current_target, (unsigned)expected_target);
        return false;
    }

    // The loop-back branch must be the stock `jl 0x43c6a7` (7c dc) too: the two
    // patches only make sense together, so check both before writing either.
    BYTE* jl = (BYTE*)(exe_base + (CODMP_FRAME_LIMIT_LOOP_JL_VA - CODMP_PREFERRED_BASE));
    if (jl[0] != 0x7c || jl[1] != 0xdc) {
        logger::logf("frame_limiter: loop branch @0x%08x is %02x %02x (expected 7c dc) - patch annule",
                     (unsigned)(uintptr_t)jl, jl[0], jl[1]);
        return false;
    }

    g_com_event_loop = (com_event_loop_fn)expected_target;

    const uintptr_t hook = (uintptr_t)&frame_wait_replacement;
    const int32_t new_offset = (int32_t)((intptr_t)hook - (intptr_t)(opcode_addr + 5));

    // Code pages are PAGE_EXECUTE_READ; writing needs the EXECUTE kept, since the
    // main thread may be running this very loop while we patch it.
    DWORD old_protect = 0;
    if (!VirtualProtect((void*)operand_addr, 4, PAGE_EXECUTE_READWRITE, &old_protect)) {
        logger::logf("frame_limiter: VirtualProtect a echoue");
        return false;
    }
    *(int32_t*)operand_addr = new_offset;
    VirtualProtect((void*)operand_addr, 4, old_protect, &old_protect);

    if (VirtualProtect(jl, 2, PAGE_EXECUTE_READWRITE, &old_protect)) {
        // One 16-bit store, so the CPU never sees half a patched branch.
        *(volatile uint16_t*)jl = 0x9090;  // nop ; nop
        VirtualProtect(jl, 2, old_protect, &old_protect);
    }
    FlushInstructionCache(GetCurrentProcess(), (void*)opcode_addr, 0x30);

    g_applied = true;
    logger::logf(
        "frame_limiter: Com_EventLoop call at CoDUOMP+0x%lx -> our wait (0x%08x), loop jl removed, "
        "late mouse %s",
        (unsigned long)CODMP_FRAME_LIMIT_CALL_OPCODE_RVA, (unsigned)hook,
        g_frame_limiter_config.late_input ? "on" : "off");
    return true;
}

}  // namespace patches
