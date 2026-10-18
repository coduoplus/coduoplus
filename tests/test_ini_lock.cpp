// test_ini_lock.cpp - the .ini rewrite of core/ini_lock.cpp against the real shipped
// coduoplus.ini (after upstream 1.6.9's tools/test_ini_lock.cpp):
//   1. the locked keys land on their values, every other line stays byte for byte;
//   2. a second pass changes nothing (idempotent);
//   3. keys the player edited are put back in place;
//   4. keys the player deleted are re-appended under their own header;
//   5. CRLF and LF files both keep their line ending;
//   6. the runtime values are the locked ones whatever the file said.
//
//   test_ini_lock <shipped coduoplus.ini> <scratch dir>

#include "core/ini_lock.h"
#include "performance/frame_limiter.h"
#include "performance/gpu_sync.h"
#include "performance/fps_cap.h"
#include "performance/cpu_affinity.h"
#include "video/fullscreen_patch.h"
#include "features/settings_menu.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// the config globals the lock writes into (their modules are not linked here)
namespace patches {
FrameLimiterConfig  g_frame_limiter_config = { false, 0, false };
GpuSyncConfig       g_gpu_sync_config = { GPU_SYNC_OFF };
FpsCapConfig        g_fps_cap_config = { false };
CpuAffinityConfig   g_cpu_affinity_config = { 4, 0 };
FullscreenConfig    g_fullscreen_config = { true, false };
SettingsMenuConfig  g_settings_menu_config;
}  // namespace patches

using namespace patches;

static std::string slurp(const std::string& p) {
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return "";
    std::string s; char b[4096]; size_t n;
    while ((n = fread(b, 1, sizeof(b), f)) > 0) s.append(b, n);
    fclose(f);
    return s;
}
static void spit(const std::string& p, const std::string& s) {
    FILE* f = fopen(p.c_str(), "wb"); fwrite(s.data(), 1, s.size(), f); fclose(f);
}
static std::vector<std::string> lines_of(const std::string& s) {
    std::vector<std::string> v; size_t p = 0;
    while (p < s.size()) {
        size_t e = s.find('\n', p); if (e == std::string::npos) e = s.size();
        std::string l = s.substr(p, e - p);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        v.push_back(l); p = e + 1;
    }
    return v;
}
static std::string key_of(const std::string& l) {
    size_t eq = l.find('='); if (eq == std::string::npos) return "";
    std::string k = l.substr(0, eq); size_t a = k.find_first_not_of(" \t"), b = k.find_last_not_of(" \t");
    if (a == std::string::npos || k[a] == ';' || k[a] == '[') return "";
    return k.substr(a, b - a + 1);
}
static std::string value_of(const std::string& l) {
    size_t eq = l.find('='); std::string v = l.substr(eq + 1);
    size_t a = v.find_first_not_of(" \t"), b = v.find_last_not_of(" \t\r");
    return a == std::string::npos ? "" : v.substr(a, b - a + 1);
}
// set `key`'s value in an ini text (keeps the alignment before the '=')
static std::string with_value(const std::string& text, const char* key, const char* value) {
    std::string out;
    for (const std::string& l : lines_of(text)) {
        std::string line = l;
        if (key_of(l) == key) line = l.substr(0, l.find('=') + 1) + " " + value;
        out += line + (text.find("\r\n") != std::string::npos ? "\r\n" : "\n");
    }
    return out;
}
static std::string without_key(const std::string& text, const char* key) {
    std::string out;
    for (const std::string& l : lines_of(text))
        if (key_of(l) != key) out += l + (text.find("\r\n") != std::string::npos ? "\r\n" : "\n");
    return out;
}
static std::string to_lf(std::string s) { std::string o; for (char c : s) if (c != '\r') o += c; return o; }

static int g_failed = 0;
static void check(bool ok, const char* what) { printf("  [%s] %s\n", ok ? "OK" : "FAIL", what); if (!ok) ++g_failed; }

static const char* kLocked[][2] = {
    { "frame_limiter_enable", "on" }, { "force_1ms_timer", "on" }, { "gpu_sync", "auto" },
    { "input_late_sampling", "on" }, { "fullscreen", "on" }, { "refresh_rate", "max" },
    { "smoothness_cpu_cores", "0" },
};

static bool all_locked(const std::string& text) {
    for (auto& lk : kLocked) {
        bool ok = false;
        for (const std::string& l : lines_of(text)) if (key_of(l) == lk[0]) { ok = value_of(l) == lk[1]; }
        if (!ok) { printf("    %s is not %s\n", lk[0], lk[1]); return false; }
    }
    return true;
}

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: test_ini_lock <coduoplus.ini> <scratch dir>\n"); return 2; }
    const std::string shipped = slurp(argv[1]);
    if (shipped.empty()) { fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }
    const std::string dir = argv[2];
    CreateDirectoryA(dir.c_str(), nullptr);
    const std::string tmp = dir + "/test_ini_lock.ini";

    // the shipped ini already carries every locked key at its locked value
    check(all_locked(shipped), "shipped coduoplus.ini already holds the locked values");

    // 1. a player's ini with every locked key set otherwise: put back in place
    std::string player = shipped;
    for (auto& lk : kLocked)
        player = with_value(player, lk[0], !strcmp(lk[1], "on") ? "off" : (!strcmp(lk[1], "0") ? "4" : "0"));
    spit(tmp, player);
    ini_lock_apply(tmp.c_str());
    const std::string after = slurp(tmp);
    check(all_locked(after), "edited locked keys rewritten to their values");
    {
        const std::vector<std::string> a = lines_of(player), b = lines_of(after);
        bool same = a.size() == b.size();
        int kept = 0;
        for (size_t i = 0; same && i < a.size(); ++i) {
            bool locked = false;
            for (auto& lk : kLocked) locked |= key_of(a[i]) == lk[0];
            if (locked) continue;
            if (a[i] != b[i]) { same = false; printf("    differs at: %s\n", a[i].c_str()); }
            ++kept;
        }
        char what[96]; snprintf(what, sizeof(what), "%d other lines identical and in place", kept);
        check(same, what);
        check(after.find("; ----- IMPOSED BY THE MOD (rewritten") == std::string::npos, "nothing appended when every key is present");
    }

    // 2. idempotent
    ini_lock_apply(tmp.c_str());
    check(slurp(tmp) == after, "second pass leaves the file untouched");

    // 3. keys deleted by the player: re-appended under their own header
    spit(tmp, without_key(without_key(shipped, "gpu_sync"), "input_late_sampling"));
    ini_lock_apply(tmp.c_str());
    const std::string appended = slurp(tmp);
    check(all_locked(appended), "deleted locked keys re-appended");
    check(appended.find("; ----- IMPOSED BY THE MOD (rewritten") != std::string::npos, "...under the IMPOSED BY THE MOD header");

    // 4. line endings: the CRLF shipped file and an LF copy each keep theirs
    const bool shipped_crlf = shipped.find("\r\n") != std::string::npos;
    check(shipped_crlf == (after.find("\r\n") != std::string::npos), shipped_crlf ? "CRLF kept" : "LF kept");
    {
        std::string lf = to_lf(with_value(shipped, "refresh_rate", "0"));
        spit(tmp, lf);
        ini_lock_apply(tmp.c_str());
        const std::string a2 = slurp(tmp);
        check(a2.find('\r') == std::string::npos && all_locked(a2), "LF file rewritten with LF");
    }

    // 5. the runtime values are the locked ones
    check(g_frame_limiter_config.enable && g_frame_limiter_config.late_input && g_fps_cap_config.force_1ms_timer &&
          g_gpu_sync_config.mode == GPU_SYNC_AUTO && !g_fullscreen_config.force_windowed_default &&
          g_fullscreen_config.ini_key_present && !strcmp(g_settings_menu_config.refresh_rate, "max") &&
          g_cpu_affinity_config.cores_count == 0, "runtime values forced");

    DeleteFileA(tmp.c_str());
    printf("%s\n", g_failed ? "FAILED" : "ALL OK");
    return g_failed ? 1 : 0;
}
