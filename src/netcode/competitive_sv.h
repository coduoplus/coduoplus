#ifndef COD1RELOADED_COMPETITIVE_SV_H
#define COD1RELOADED_COMPETITIVE_SV_H

// Server half of the competitive netconfig: what makes CoDUO+ CLIENTS actually run
// snaps 40 / cl_maxpackets 125 / rate ... on a CoDUO+ server.
//
// The client half (competitive.cpp) has been there since 0.1.0: it reads the
// CURRENT server's systeminfo for sv_competitive, sv_competitive2..4 and locks
// ("name=value") or clamps ("name=min:max") the named cvars. COD1.6X publishes that
// spec from its Linux server module (cod1plushookserver competitive_sv.c); a UO
// server had nothing to publish it, so the lifted caps (snaps 40, cl_maxpackets 125)
// never came into play - clients kept snaps 20 / cl_maxpackets 30.
//
// This is that publisher, for a Windows CoDUOMP.exe server running CoDUO+ (dedicated
// or listen): the admin writes competitive.cfg next to mss32.dll, in upstream's format
//     # comment
//     snaps 40              -> "snaps=40"            locked on the client (ROM)
//     com_maxfps 125 250    -> "com_maxfps=125:250"  clamped into the range
// The spec is split at entry boundaries into SYSTEMINFO cvars of <= 220 chars (the
// engine aborts the server on a cvar value over 256 at the next map change), re-read
// every second when the file changes, and cleared when the file goes away. Vanilla
// clients ignore it. No file = nothing published = vanilla behaviour.

#include <string>
#include <vector>

namespace patches {

constexpr int COMP_SV_CHUNK_MAX = 220;   // per-cvar payload, safely under the 256 limit
constexpr int COMP_SV_CHUNKS    = 4;     // sv_competitive, sv_competitive2..4 (~880 chars)

// competitive.cfg text -> "name=v name=lo:hi ..." (pure; tests/test_competitive_sv.cpp).
// Lines whose tokens hold anything but [A-Za-z0-9_.+-] are refused (they would break
// the systeminfo string or the console command that sets it) and listed in `rejected`.
std::string competitive_build_spec(const std::string& cfg_text, std::vector<std::string>* rejected);

// Split a spec into exactly COMP_SV_CHUNKS parts of <= COMP_SV_CHUNK_MAX chars, cut on
// spaces (an entry is never split; the client joins the parts back with a space).
// Returns false if the spec did not fit (the tail is dropped).
bool competitive_chunk_spec(const std::string& spec, std::vector<std::string>& parts);

// Watcher tick: only acts while this process hosts a server (uo_game loaded).
void competitive_sv_tick();

}  // namespace patches

#endif
