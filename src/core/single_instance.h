#ifndef COD1RELOADED_SINGLE_INSTANCE_H
#define COD1RELOADED_SINGLE_INSTANCE_H

#include <windows.h>
#include <stdint.h>

// Two CoDMP.exe at once = shared config, shared logs, two clients fighting over
// the same net_port: "ca nique tout" (AMD alt-tab report, 2026-08-24).
//
// The engine itself can spawn a second copy: CoDMP.exe builds the command line
// `%s monkey %d %d "%s"` (verified at 0x467fc0-0x468084: OpenProcess on the
// parent PID, then CreateProcessA), the idTech3 "relaunch and wait for the old
// process to die" pattern. When the parent hangs - which is exactly what a lost
// GL context / display-mode change does on AMD after an alt-tab out of exclusive
// fullscreen - nobody ever exits and both stay alive. A player double-clicking
// the shortcut on a game that looks frozen produces the same end state.
//
// Guard, called as early as possible in DLL_PROCESS_ATTACH:
//   - a legit `monkey` relaunch is ALWAYS allowed through (and it actively
//     helps: it waits for the parent, then kills it if it is wedged);
//   - a plain duplicate launch is refused: the existing window is brought to
//     the front and this process exits before it touches a single file.
// The mutex is namespaced by the exe PATH, so a dev folder and the main install
// still run side by side.
namespace patches {

void single_instance_guard();

// The engine's own crash check (CoDUOMP.exe 1.51, 0x46c490, run from Com_Init and
// from WinMain unless the command line has "allowdupe"): it keeps the running PID in
// "__<exe name>" next to the exe, deletes it on a clean quit, and at launch, when that
// file names a process that is no longer running (crash, kill, power loss), shows
//   46c503  6a 33          push 0x33 (MB_YESNOCANCEL | MB_ICONWARNING)
//   46c505  push "WIN_IMPROPER_QUIT_TITLE" / call SEH_LocalizeTextMessage ...
//   46c523  ff 15 8c 62 58 00  call [MessageBoxA]
// Yes = safe mode (0x389fd2c = 1), No = carry on, Cancel = quit. It shows before the
// localization is loaded, so the player only sees the raw WIN_IMPROPER_QUIT_* keys -
// a box nobody can read, on every launch after a crash. We jump straight to the "No"
// branch (0x46c550: rewrite the PID file, return 1). The "already running" half of
// the check (0x46c2f0, before it) is untouched.
constexpr uintptr_t CODMP_IMPROPER_QUIT_PROMPT_VA = 0x46c503;
constexpr uintptr_t CODMP_IMPROPER_QUIT_NO_VA     = 0x46c550;

bool skip_improper_quit_prompt();

}  // namespace patches

#endif
