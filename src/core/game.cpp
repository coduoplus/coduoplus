#include "core/game.h"

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

}  // namespace patches
