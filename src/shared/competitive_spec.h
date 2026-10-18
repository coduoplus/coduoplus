// competitive_spec.h - competitive.cfg -> sv_competitive* spec, platform-free.
// Compiled into the Windows mod (netcode/competitive_sv.cpp) and the Linux server
// library (server/linux/coduoplus_sv.cpp); tested by tests/test_competitive_sv.cpp.
#ifndef CODUOPLUS_COMPETITIVE_SPEC_H
#define CODUOPLUS_COMPETITIVE_SPEC_H

#include "shared/coduoplus_shared.h"

#include <string>
#include <vector>

namespace patches {

constexpr int COMP_SV_CHUNK_MAX = COMP_SV_CHUNK_MAX_C;   // per-cvar payload, safely under 256
constexpr int COMP_SV_CHUNKS    = COMP_SV_CHUNKS_C;      // sv_competitive, sv_competitive2..4

// competitive.cfg text -> "name=v name=lo:hi ...". Lines whose tokens hold anything but
// [A-Za-z0-9_.+-] are refused (they would break the systeminfo string or the command
// that sets it) and listed in `rejected`.
std::string competitive_build_spec(const std::string& cfg_text, std::vector<std::string>* rejected);

// Split into exactly COMP_SV_CHUNKS parts of <= COMP_SV_CHUNK_MAX chars, cut on spaces (an
// entry is never split; the client joins the parts back with a space). False = did not
// fit, the tail is dropped.
bool competitive_chunk_spec(const std::string& spec, std::vector<std::string>& parts);

// "sv_competitive", "sv_competitive2", ...
std::string competitive_chunk_name(int i);

}  // namespace patches

#endif
