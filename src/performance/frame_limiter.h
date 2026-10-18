#ifndef COD1RELOADED_FRAME_LIMITER_H
#define COD1RELOADED_FRAME_LIMITER_H

#include <windows.h>
#include <stdint.h>

namespace patches {

// CoDUOMP.exe 1.51, preferred base 0x00400000. Com_Frame's wait is the stock
// Quake 3 loop, and the function it spins on is Com_EventLoop - NOT a bare clock
// like the CoD1 call upstream replaces:
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
// clock. Replacing it with a pure wait (what the CoD1 patch does) would starve
// input and networking. So the replacement WAITS for the QPC deadline and THEN
// calls the real Com_EventLoop once, returning its time; and the jl is removed
// so the engine's coarse integer-ms gate (the cause of "250 shows 240-248")
// cannot re-loop and add a frame on top of ours. When the limiter does not
// apply (com_maxfps 0, dedicated) the replacement reproduces the original loop
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

struct FrameLimiterConfig {
    bool enable;
    int  deadline_bias_us; // +us added to deadline; -500 -> effective 250.5 if running under
};

extern FrameLimiterConfig g_frame_limiter_config;

bool apply_frame_limiter_patch();

}  // namespace patches

#endif
