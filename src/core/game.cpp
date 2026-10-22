#include "core/game.h"

#include <cctype>
#include <cstring>

namespace patches {

namespace {

bool probe() {
    BYTE* base = (BYTE*)GetModuleHandleA(NULL);
    if ((uintptr_t)base != GAME_EXE_BASE) return false;

    auto dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    if (nt->FileHeader.TimeDateStamp != GAME_PE_TIMESTAMP) return false;

    // .rdata is mapped from the file at load, so this reads the shipped bytes
    // even before Com_Init has run.
    return memcmp((const void*)GAME_SHORTVERSION_STR_VA, "1.51\0", 5) == 0;
}

}  // namespace

bool game_exe_ok() {
    // -1 unknown, 0 no, 1 yes. Benign race: every thread computes the same answer.
    static volatile int s_state = -1;
    if (s_state < 0) s_state = probe() ? 1 : 0;
    return port::ENGINE && s_state == 1;
}

bool launched_as_dedicated() { return cmdline_sets_dedicated(GetCommandLineA()); }

bool cmdline_sets_dedicated(const char* cl) {
    if (!cl) return false;
    // every whole-word "dedicated" (case-insensitive), followed by blanks and 1 or 2
    for (const char* p = cl; *p; ++p) {
        if (_strnicmp(p, "dedicated", 9) != 0) continue;
        if (p != cl && !isspace((unsigned char)p[-1]) && p[-1] != '"') continue;
        const char* v = p + 9;
        if (!isspace((unsigned char)*v)) continue;
        while (isspace((unsigned char)*v) || *v == '"') ++v;
        if ((*v == '1' || *v == '2') && (v[1] == 0 || isspace((unsigned char)v[1]) || v[1] == '"'))
            return true;
    }
    return false;
}

}  // namespace patches
