// Server-side publisher of the competitive spec - see competitive_sv.h.

#include "netcode/competitive_sv.h"
#include "core/game.h"
#include "core/logger.h"
#include "netcode/competitive.h"     // COMP_CVAR_SYSTEMINFO, COMP_CVAR_STRING
#include "netcode/protocol_patch.h"  // CODMP_CVAR_GET_VA, CODMP_CVAR_COUNT_VA
#include "features/settings_menu.h"  // CODMP_CVAR_FINDVAR_VA, CODMP_CBUF_EXECTEXT_VA

#include <windows.h>
#include <cstdio>
#include <cstring>

namespace patches {

namespace {

bool token_ok(const std::string& t) {
    if (t.empty() || t.size() > 31) return false;
    for (char c : t) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                        c == '_' || c == '.' || c == '-' || c == '+';
        if (!ok) return false;
    }
    return true;
}

}  // namespace

std::string competitive_build_spec(const std::string& text, std::vector<std::string>* rejected) {
    std::string spec;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(pos, e - pos);
        pos = e + 1;
        // tokens separated by blanks; a comment is a WHOLE line (upstream: an inline
        // comment would become part of the value)
        std::vector<std::string> tok;
        size_t i = 0;
        while (i < line.size()) {
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) ++i;
            size_t j = i;
            while (j < line.size() && line[j] != ' ' && line[j] != '\t' && line[j] != '\r') ++j;
            if (j > i) tok.push_back(line.substr(i, j - i));
            i = j;
        }
        if (tok.empty() || tok[0][0] == '#' || tok[0][0] == ';' || tok[0][0] == '/') continue;
        bool ok = (tok.size() == 2 || tok.size() == 3);
        for (const std::string& t : tok) ok = ok && token_ok(t);
        if (!ok) {
            if (rejected) rejected->push_back(line);
            continue;
        }
        std::string entry = tok[0] + "=" + tok[1];
        if (tok.size() == 3) entry += ":" + tok[2];
        if (!spec.empty()) spec += " ";
        spec += entry;
    }
    return spec;
}

bool competitive_chunk_spec(const std::string& spec, std::vector<std::string>& parts) {
    parts.assign(COMP_SV_CHUNKS, std::string());
    size_t p = 0;
    for (int i = 0; i < COMP_SV_CHUNKS && p < spec.size(); ++i) {
        size_t len = spec.size() - p;
        if (len > (size_t)COMP_SV_CHUNK_MAX) {
            len = COMP_SV_CHUNK_MAX;
            while (len > 0 && spec[p + len] != ' ') --len;   // cut before an entry
            if (len == 0) return false;                       // one entry > chunk: refuse
        }
        parts[i] = spec.substr(p, len);
        p += len;
        while (p < spec.size() && spec[p] == ' ') ++p;
    }
    return p >= spec.size();
}

namespace {

typedef void* (__cdecl* Cvar_Get_t)(const char*, const char*, int);
typedef void* (__cdecl* Cvar_FindVar_t)(const char*);
typedef void  (__cdecl* Cbuf_ExecuteText_t)(int, const char*);
constexpr int EXEC_APPEND = 2;

bool engine_ready() { return game_exe_ok() && *(volatile int*)CODMP_CVAR_COUNT_VA > 0; }

void chunk_name(int i, char* out, size_t n) {
    if (i == 0) snprintf(out, n, "sv_competitive");
    else        snprintf(out, n, "sv_competitive%d", i + 1);
}

// competitive.cfg next to the mod DLL (the game folder, with coduoplus.ini)
bool cfg_path(char* out, size_t n) {
    HMODULE self = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)&competitive_sv_tick, &self)) return false;
    char dll[MAX_PATH];
    const DWORD len = GetModuleFileNameA(self, dll, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return false;
    char* slash = strrchr(dll, '\\');
    if (!slash) return false;
    slash[1] = 0;
    return snprintf(out, n, "%scompetitive.cfg", dll) < (int)n;
}

bool read_file(const char* path, std::string& out, FILETIME* mtime) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &a)) return false;
    *mtime = a.ftLastWriteTime;
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    char buf[4096];
    size_t n;
    out.clear();
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0 && out.size() < 64 * 1024) out.append(buf, n);
    fclose(f);
    return true;
}

}  // namespace

void competitive_sv_tick() {
    if (!engine_ready()) return;
    // Only a process hosting a server has the game module loaded (dedicated, listen).
    if (!GetModuleHandleA(GAME_GAME_DLL)) return;

    static DWORD s_last = 0;
    const DWORD now = GetTickCount();
    if (s_last && now - s_last < 1000) return;
    s_last = now;

    static bool s_registered = false;
    if (!s_registered) {
        // SYSTEMINFO, not ROM: the values are then changed with a plain `set` through the
        // command buffer, i.e. on the main thread, never under the engine's feet.
        for (int i = 0; i < COMP_SV_CHUNKS; ++i) {
            char nm[32];
            chunk_name(i, nm, sizeof(nm));
            ((Cvar_Get_t)CODMP_CVAR_GET_VA)(nm, "", COMP_CVAR_SYSTEMINFO);
        }
        s_registered = true;
    }

    // Read back what the engine holds once the queued `set`s have run (main thread, next
    // frame): the proof the spec really went into the SYSTEMINFO cvars clients receive.
    static int s_verify = 0;   // ticks left to look; 0 = nothing pending
    if (s_verify > 0) {
        void* cv = ((Cvar_FindVar_t)CODMP_CVAR_FINDVAR_VA)("sv_competitive");
        const char* v = cv ? *(const char**)((char*)cv + COMP_CVAR_STRING) : nullptr;
        const uint32_t flags = cv ? *(const uint32_t*)((char*)cv + COMP_CVAR_FLAGS) : 0;
        if (v && *v) {
            logger::logf("competitive_sv: engine holds sv_competitive \"%s\" (flags 0x%x, SYSTEMINFO %s)",
                         v, flags, (flags & COMP_CVAR_SYSTEMINFO) ? "yes" : "NO");
            s_verify = 0;
        } else if (--s_verify == 0) {
            logger::logf("competitive_sv: sv_competitive still empty in the engine");
        }
    }

    // re-read only when the file appears, changes or goes away
    static bool     s_have = false;
    static FILETIME s_mtime = {};
    static std::string s_published;
    static bool     s_first = true;
    char path[MAX_PATH];
    if (!cfg_path(path, sizeof(path))) return;
    std::string text;
    FILETIME mt = {};
    const bool have = read_file(path, text, &mt);
    if (!s_first && have == s_have && (!have || CompareFileTime(&mt, &s_mtime) == 0)) {
        return;
    }
    s_first = false;
    s_have = have;
    s_mtime = mt;

    std::vector<std::string> rejected;
    const std::string spec = have ? competitive_build_spec(text, &rejected) : std::string();
    for (const std::string& r : rejected)
        logger::logf("competitive_sv: competitive.cfg line refused (format is `cvar value` or "
                     "`cvar min max`, letters/digits/_.+- only): %s", r.c_str());
    std::vector<std::string> parts;
    if (!competitive_chunk_spec(spec, parts))
        logger::logf("competitive_sv: competitive.cfg too long (max ~%d chars) - tail dropped",
                     COMP_SV_CHUNK_MAX * COMP_SV_CHUNKS);
    if (spec == s_published) return;
    s_published = spec;

    std::string cmd;
    for (int i = 0; i < COMP_SV_CHUNKS; ++i) {
        char nm[32];
        chunk_name(i, nm, sizeof(nm));
        cmd += std::string("set ") + nm + " \"" + parts[i] + "\"\n";
    }
    ((Cbuf_ExecuteText_t)CODMP_CBUF_EXECTEXT_VA)(EXEC_APPEND, cmd.c_str());
    s_verify = spec.empty() ? 0 : 5;
    logger::logf("competitive_sv: %s - publishing \"%s\"", have ? path : "no competitive.cfg",
                 spec.empty() ? "(empty: clients stay vanilla)" : spec.c_str());
}

}  // namespace patches
