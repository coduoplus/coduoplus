// test_competitive_sv.cpp - competitive.cfg -> sv_competitive* (netcode/competitive_sv.cpp)
//
// The server publishes; the client (netcode/competitive.cpp) joins the parts with a space,
// splits on blanks, and reads each token as name=value (lock) or name=min:max (clamp).
// This checks the publisher's output against exactly that reading, plus the limits the
// engine imposes (a cvar value over 256 chars aborts the server at the next map change).

#include "shared/competitive_spec.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace patches;

static int g_failed = 0;
static void check(bool ok, const char* what) { printf("  [%s] %s\n", ok ? "OK" : "FAIL", what); if (!ok) ++g_failed; }

// the client's reading of a joined spec: token -> (name, rhs)
static std::vector<std::pair<std::string, std::string>> client_read(const std::vector<std::string>& parts) {
    std::string joined;
    for (const std::string& p : parts) if (!p.empty()) { if (!joined.empty()) joined += " "; joined += p; }
    std::vector<std::pair<std::string, std::string>> out;
    size_t i = 0;
    while (i < joined.size()) {
        while (i < joined.size() && joined[i] == ' ') ++i;
        size_t j = joined.find(' ', i); if (j == std::string::npos) j = joined.size();
        const std::string tok = joined.substr(i, j - i);
        const size_t eq = tok.find('=');
        if (!tok.empty() && eq != std::string::npos) out.push_back({tok.substr(0, eq), tok.substr(eq + 1)});
        i = j;
    }
    return out;
}

int main() {
    // the shipped example, as an admin would copy it
    const char* example =
        "# netcode\r\n"
        "snaps 40\r\n"
        "cl_maxpackets 125\r\n"
        "rate 90000\r\n"
        "com_maxfps 125 333\r\n"
        "\r\n"
        "   cl_timenudge  -20   0\r\n"
        "; another comment style\r\n"
        "// and another\r\n"
        "m_yaw 0.022\r\n"
        "cl_packetdup 1 5";
    std::vector<std::string> rej;
    const std::string spec = competitive_build_spec(example, &rej);
    printf("spec: %s\n", spec.c_str());
    check(spec == "snaps=40 cl_maxpackets=125 rate=90000 com_maxfps=125:333 cl_timenudge=-20:0 "
                  "m_yaw=0.022 cl_packetdup=1:5", "exact values -> name=v, ranges -> name=lo:hi, comments skipped");
    check(rej.empty(), "nothing refused in a clean file");

    // malformed lines are refused, not passed on
    rej.clear();
    const std::string bad = competitive_build_spec(
        "snaps 40\n"
        "rate\n"                         // value missing
        "cg_fov 80 95 100\n"             // too many tokens
        "name \"x\"\n"                   // quote would break `set "..."`
        "say hi;quit\n"                  // ';' would chain a console command
        "r_mode 3 # inline comment\n"    // inline comment = 4 tokens
        "sv_x a\\b\n", &rej);           // backslash would break the info string
    check(bad == "snaps=40", "only the well-formed line is published");
    char what[96]; snprintf(what, sizeof(what), "%d malformed lines refused and reported", (int)rej.size());
    check(rej.size() == 6, what);

    // empty / comments only -> empty spec (clients stay vanilla)
    check(competitive_build_spec("# nothing\n\n", nullptr).empty(), "comments only -> empty spec");

    // chunking: every part <= 220 chars, cut between entries, client reads all entries back
    std::string big;
    std::vector<std::pair<std::string, std::string>> want;
    for (int i = 0; i < 30; ++i) {   // ~650 chars: spans 3-4 parts, fits
        char line[64]; snprintf(line, sizeof(line), "cvar_number_%02d %d %d\n", i, i, i + 100);
        big += line;
        char nm[32], rhs[32]; snprintf(nm, sizeof(nm), "cvar_number_%02d", i); snprintf(rhs, sizeof(rhs), "%d:%d", i, i + 100);
        want.push_back({nm, rhs});
    }
    const std::string big_spec = competitive_build_spec(big, nullptr);
    std::vector<std::string> parts;
    const bool fit = competitive_chunk_spec(big_spec, parts);
    bool sizes_ok = parts.size() == (size_t)COMP_SV_CHUNKS;
    for (const std::string& p : parts) sizes_ok = sizes_ok && p.size() <= (size_t)COMP_SV_CHUNK_MAX &&
                                                  (p.empty() || (p.front() != ' ' && p.back() != ' '));
    check(sizes_ok, "4 parts, each <= 220 chars, no leading/trailing blank");
    const auto got = client_read(parts);
    snprintf(what, sizeof(what), "spec of %d chars fits (%s), client reads %d/%d entries back intact",
             (int)big_spec.size(), fit ? "yes" : "no", (int)got.size(), (int)want.size());
    check(fit && got == want, what);

    // too long: refused gracefully, what fits is still intact entries
    std::string huge;
    for (int i = 0; i < 120; ++i) { char l[64]; snprintf(l, sizeof(l), "some_long_cvar_name_%03d 12345 67890\n", i); huge += l; }
    const bool fit2 = competitive_chunk_spec(competitive_build_spec(huge, nullptr), parts);
    bool intact = true;
    for (const auto& kv : client_read(parts)) intact = intact && kv.second == "12345:67890";
    check(!fit2 && intact, "over-long spec reported, the published part is whole entries only");

    // the shipped example config parses cleanly
    printf("%s\n", g_failed ? "FAILED" : "ALL OK");
    return g_failed ? 1 : 0;
}
