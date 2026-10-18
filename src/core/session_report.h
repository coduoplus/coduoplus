#ifndef COD1RELOADED_SESSION_REPORT_H
#define COD1RELOADED_SESSION_REPORT_H
// One line a minute, and one at the end, that says how the game actually ran:
//     session 60 s: 250.0 fps (15000 frames, 0 late wakes, 0 long, max 4.3 ms) | GPU 0.50 ms
//     (max 1.4) | 1920x1080 @ 320 Hz exclusive | com_maxfps 250 | rinput on
// A player who says "it lags" pastes that line; the numbers come from the frame limiter
// (cadence), gpu_sync (GPU wait), mode_guard (display mode) and the engine's cvars.
//
// Ported from upstream 1.6.9 (809a439). UO differences: "late wakes" counts frames whose
// wait ended over 1 ms after its deadline (the UO wait never re-loops, so upstream's
// "second call of the loop" count would always read 0), and nothing is reported on a
// dedicated server, where the limiter is not in charge.
namespace patches {

void session_report_tick();                 // watcher thread, cheap: writes every 60 s
void session_report_final(const char* why); // once: window closing / DLL detach

}  // namespace patches

#endif
