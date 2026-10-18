#ifndef COD1RELOADED_VERSION_PATCH_H
#define COD1RELOADED_VERSION_PATCH_H

#include <windows.h>
#include <stdint.h>

namespace patches {

// CoDUOMP.exe 1.51 RVAs vs image base 0x00400000. The CODMP_ names are kept from
// upstream so a later upstream merge lines up; the values are UO's.
// Com_Init registers shortversion at 0x43c0dd:
//   43c0dd  6a 44           push 0x44            ; CVAR_ROM|CVAR_SERVERINFO
//   43c0df  68 b0 3c 59 00  push 0x593cb0        ; "1.51" default   <- operand patched
//   43c0e4  68 24 81 59 00  push 0x598124        ; "shortversion"
//   43c0ee  e8 ed 18 00 00  call Cvar_Get (0x43d9e0)
constexpr uintptr_t CODMP_PREFERRED_BASE = 0x00400000;
constexpr uintptr_t CODMP_SHORTVERSION_PUSH_OPCODE_RVA  = 0x0003c0df; // opcode 0x68
constexpr uintptr_t CODMP_SHORTVERSION_PUSH_OPERAND_RVA = 0x0003c0e0; // 4 bytes to patch
constexpr uintptr_t CODMP_SHORTVERSION_ORIGINAL_PTR     = 0x00593cb0; // -> "1.51\0"

constexpr size_t SHORT_VERSION_MAX_LEN = 63;

// engine treats dvar as CVAR_ROM (flags 0x44) so never writes here
extern char g_short_version_buffer[SHORT_VERSION_MAX_LEN + 1];

bool apply_short_version_patch();

}  // namespace patches

#endif
