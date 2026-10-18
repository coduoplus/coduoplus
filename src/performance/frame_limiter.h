#ifndef COD1RELOADED_FRAME_LIMITER_H
#define COD1RELOADED_FRAME_LIMITER_H

#include <windows.h>
#include <stdint.h>

namespace patches {

// CoDUOMP.exe 1.51, preferred base 0x00400000. Com_Frame's wait is the stock
// Quake 3 loop, and the function it spins on is Com_EventLoop:
//
//   43c681  a1 8c 90 92 04     mov  eax,[com_maxfps]      ; minMsec = 1000/com_maxfps
//   ...                                                     ; (1 if 0 or dedicated)
//   43c6a7  e8 14 e5 ff ff     call Com_EventLoop (0x43abc0) <- rel32 repointed
//   43c6ac  8b 0d 30 02 98 00  mov  ecx,[lastTime]
//   ...                        if (lastTime > t) lastTime = t
//   43c6c3  2b c1              sub  eax,ecx                ; msec = t - lastTime
//   43c6c7  3b f7              cmp  esi,edi
//   43c6c9  7c dc              jl   0x43c6a7               <- made 2 NOPs
//
// Com_EventLoop drains key/mouse/console/packet events and returns the engine
// clock. Replacing it with a pure wait (what the CoD1 patch does - CoD1 then
// relies on Com_Frame's SECOND Com_EventLoop) would starve a dedicated server:
// in UO that second call (0x43c899) is skipped when `dedicated` is set, so the
// one in the loop is the only one. The replacement therefore WAITS for the QPC
// deadline and THEN calls the real Com_EventLoop once; the jl is removed so the
// engine's coarse integer-ms gate (the cause of "250 shows 240-248") cannot
// re-loop and add a frame on top of ours. When the limiter does not apply
// (com_maxfps 0, dedicated) the replacement reproduces the original loop
// itself, so behaviour there is exactly vanilla.
constexpr uintptr_t CODMP_FRAME_LIMIT_CALL_OPCODE_RVA   = 0x0003c6a7; // 'E8'
constexpr uintptr_t CODMP_FRAME_LIMIT_CALL_OPERAND_RVA  = 0x0003c6a8; // rel32
constexpr uintptr_t CODMP_FRAME_LIMIT_ORIGINAL_TARGET   = 0x0043abc0; // Com_EventLoop
constexpr uintptr_t CODMP_FRAME_LIMIT_LOOP_JL_VA        = 0x0043c6c9; // 7c dc -> 90 90
constexpr uintptr_t CODMP_FRAME_LIMIT_LASTTIME_VA       = 0x00980230; // static int lastTime

// cvar_t* slots written right after Cvar_Get in Com_Init; integer at +0x20
constexpr uintptr_t CODMP_COM_MAXFPS_DVAR_SLOT_VA       = 0x0492908c; // com_maxfps (43be91)
constexpr uintptr_t CODMP_COM_DEDICATED_DVAR_SLOT_VA    = 0x049290a4; // com_dedicated
constexpr uintptr_t CODMP_DVAR_INTEGER_OFFSET           = 0x20;

// The engine's clock. Sys_Milliseconds() is inlined at all 51 of its call sites as
//     if (!sys_timeInitialized) { sys_timeBase = timeGetTime(); sys_timeInitialized = 1; }
//     return timeGetTime() - sys_timeBase;
// ms since the game started. Key events, mouse events, packets and cls.realtime
// are all stamped on it, so the time the wait reports must be on it too (upstream
// 1.6.9 a9dd4a2: a foreign epoch quantised keyboard movement to whole frames).
constexpr uintptr_t CODMP_SYS_TIMEBASE_VA               = 0x0489bc3c;
constexpr uintptr_t CODMP_SYS_TIMEINIT_VA               = 0x0389fddc;

// Late mouse sampling (upstream 1.6.9 809a439, see frame_limiter.cpp):
//   IN_MouseMove()   0x469c20  GetCursorPos - centre, SetCursorPos(centre), Sys_QueEvent(SE_MOUSE)
//   IN_Frame's gate before it reaches IN_MouseMove (469e93..469eb9):
//     s_wmv.mouseInitialized 0x9cdbc4, s_wmv.mouseActive 0x9cdbc0 (CoD1: 0x93b3a4 / 0x93b3a0)
// UO has no separate Sys_SendKeyEvents: the PeekMessage pump is inlined into
// Sys_GetEvent (0x46bb80), which Com_EventLoop calls - so the keys and buttons
// are already pumped late by the Com_EventLoop call that follows the wait.
constexpr uintptr_t CODMP_IN_MOUSEMOVE_VA               = 0x00469c20;
constexpr uintptr_t CODMP_MOUSE_INITIALIZED_VA          = 0x009cdbc4;
constexpr uintptr_t CODMP_MOUSE_ACTIVE_VA               = 0x009cdbc0;

struct FrameLimiterConfig {
    bool enable;
    int  deadline_bias_us; // +us added to deadline; -500 -> effective 250.5 if running under
    bool late_input;       // sample the mouse AFTER the wait (ini input_late_sampling)
};

extern FrameLimiterConfig g_frame_limiter_config;

// What the wait reads from / calls in the engine, behind pointers so
// tests/test_frame_limiter.cpp can run the very same wait inside a copy of the
// patched Com_Frame loop against a fake engine.
struct FrameLimiterEngine {
    int  (*maxfps)();          // com_maxfps->integer (<= 0: no cap)
    int  (*dedicated)();       // com_dedicated->integer
    int  (*engine_ms)();       // Sys_Milliseconds()
    int  (*event_loop)();      // Com_EventLoop(): drains every queued event, returns engine ms
    int* last_time;            // Com_Frame's lastTime
    void (*late_input)();      // may be null: read the mouse now, the wait is over
};

// What the last window of frames looked like (session_report.cpp prints it).
struct FrameLimiterStats {
    long   frames;        // limited frames completed
    long   late;          // frames whose wake came > 1 ms after their deadline (scheduler)
    long   long_frames;   // frame periods over 1.5x the target
    double max_frame_ms;  // longest frame period
};
void frame_limiter_stats(FrameLimiterStats* out, bool reset);   // out may be null

// One call of Com_Frame's (patched, single-pass) loop: waits for the frame's
// deadline, drains the engine's events, returns the value Com_Frame stores in
// com_frameTime. See frame_limiter.cpp for the rules it obeys.
int frame_wait(const FrameLimiterEngine& e);
void frame_limiter_reset();    // forget the cadence (tests)

bool apply_frame_limiter_patch();

}  // namespace patches

#endif
