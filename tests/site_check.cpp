// Offline patch-site verification against the stock CoD:UO 1.51 binaries.
//
//   site_check <dir-with-CoDUOMP.exe-and-uo_*_mp_x86.dll>
//
// Every runtime patch in the mod checks the original bytes before it writes.
// This test runs the same comparison ahead of time, using the same constants
// from the mod's own headers. So a wrong address fails here, on the build
// machine, not as a skipped patch (or a crash) on a player's PC.
//
// Rules:
//  - Each feature group in core/game.h port:: has a check function below.
//  - A group that is switched ON must have checks, and all must pass.
//  - A group that is OFF is still checked if it has checks, so a branch can
//    land its addresses before flipping the switch.

#include "check.h"
#include "image.h"

#include "core/game.h"

#include <cstdio>
#include <cstring>
#include <string>

TestRun g_run;

namespace {

Image g_exe, g_cgame, g_game, g_ui;

// ---------------------------------------------------------------------------
// identity: the constants core/game.cpp uses to recognise CoDUOMP.exe 1.51
// ---------------------------------------------------------------------------
int check_identity() {
    using namespace patches;
    CHECK(g_exe.base == GAME_EXE_BASE);
    CHECK(g_exe.timestamp == GAME_PE_TIMESTAMP);
    CHECK(g_exe.bytes_eq(GAME_SHORTVERSION_STR_VA, {'1', '.', '5', '1', 0}));
    // module bases the engine expects (fixed, no ASLR in 2004 builds)
    CHECK(g_cgame.base == 0x30000000);
    CHECK(g_game.base == 0x20000000);
    CHECK(g_ui.base == 0x40000000);
    return 6;
}

struct Group {
    const char* name;
    bool enabled;
    int (*fn)();  // returns number of checks it ran
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: site_check <binaries-dir>\n");
        return 2;
    }
    const std::string dir = argv[1];
    struct { Image* img; const char* file; } files[] = {
        {&g_exe, "CoDUOMP.exe"},
        {&g_cgame, "uo_cgame_mp_x86.dll"},
        {&g_game, "uo_game_mp_x86.dll"},
        {&g_ui, "uo_ui_mp_x86.dll"},
    };
    for (auto& f : files) {
        if (!f.img->load(dir + "/" + f.file)) {
            printf("cannot load %s/%s (run tests/fetch_binaries.py first)\n", dir.c_str(), f.file);
            return 2;
        }
    }

    using namespace patches;
    const Group groups[] = {
        {"identity", true, check_identity},
    };

    for (const Group& g : groups) {
        g_run.group = g.name;
        const int before = g_run.fail;
        const int n = g.fn ? g.fn() : 0;
        if (g.enabled && n == 0) {
            ++g_run.fail;
            printf("FAIL [%s] group is enabled in core/game.h but has no checks\n", g.name);
        }
        printf("%-18s %-3s %3d checks %s\n", g.name, g.enabled ? "ON" : "off", n,
               g_run.fail == before ? "ok" : "FAILED");
    }
    printf("\n%d passed, %d failed\n", g_run.pass, g_run.fail);
    return g_run.fail ? 1 : 0;
}
