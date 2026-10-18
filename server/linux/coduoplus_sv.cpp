/*
 * coduoplus_sv.cpp - CoDUO+ for the CoD:UO 1.51b LINUX dedicated server.
 *
 *   LD_PRELOAD=/path/to/coduoplus_sv.so ./coduo_lnxded +set dedicated 2 ...
 *
 * The Windows mod (mss32.dll) carries the server-side features for a CoDUOMP.exe
 * server; a Linux server runs neither that DLL nor its addresses. This library gives
 * a coduo_lnxded server the same features, so CoDUO+ players get the same behaviour
 * whichever OS the server runs:
 *
 *   1. snaps cap 30 -> 40           (SV_UserinfoChanged; 40-tick needs it)
 *   2. shortversion "CoDUO+ x.y.z"  (server browsers / getstatus show the mod)
 *   3. swing-fix mirror             (the four BG_PlayerAngles constants the client
 *                                    changes, applied to the server's game module, and
 *                                    SYSTEMINFO sv_coduoplus_swing 1 published - CoDUO+
 *                                    clients only use the fix where this is "1")
 *   4. version gate                 (ClientConnect refuses CoDUO+ clients older than
 *                                    CODUOPLUS_NET_VERSION; vanilla clients get in)
 *   5. competitive spec publisher   (competitive.cfg -> SYSTEMINFO sv_competitive*)
 *   6. large-inode file access      (the 2005 engine's 32-bit stat/readdir fail with
 *                                    EOVERFLOW on Windows drives under WSL / Docker
 *                                    Desktop and on big XFS/btrfs disks: it would find
 *                                    no pk3s at all)
 *   7. lag compensation             (g_antilag: shots are traced against where the
 *                                    other players were when the shooter saw them,
 *                                    shared/antilag_core.cpp - same code as Windows)
 *
 * The values both sides must agree on come from src/shared/coduoplus_shared.h, the
 * spec builder from src/shared/competitive_spec.cpp - the same files the Windows DLL
 * is built from. Every address is in sites_151b.h with its expected bytes; nothing is
 * written unless all of a group's bytes match, so any other build is left vanilla
 * (and says so on the console). docs/LINUX.md explains the how and why.
 *
 * Environment (all optional):
 *   CODUOPLUS_SWING=0          do not mirror the swing fix (and publish "0")
 *   CODUOPLUS_VERSION_GATE=0   let every client in
 *   CODUOPLUS_SNAPS=0          keep the vanilla snaps cap of 30
 *   CODUOPLUS_COMPETITIVE_FILE path of competitive.cfg (default: ./competitive.cfg)
 *   CODUOPLUS_LFS=0            leave the engine's stat/readdir alone
 *   CODUOPLUS_ANTILAG=0        default of g_antilag (1); `+set g_antilag` / rcon win
 *   CODUOPLUS_ANTILAG_MAXMS=n  default of g_antilagMaxMs (300)
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "shared/antilag_core.h"
#include "shared/coduoplus_shared.h"
#include "shared/competitive_spec.h"
#include "sites_151b.h"

#define API extern "C" __attribute__((visibility("default")))

namespace {

// ---------------------------------------------------------------------------------
// small helpers
// ---------------------------------------------------------------------------------
void lg(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fprintf(stdout, "[coduoplus] %s\n", buf);
    fflush(stdout);
}

bool env_off(const char* name) {
    const char* v = getenv(name);
    return v && (v[0] == '0' || v[0] == 'n' || v[0] == 'N' || v[0] == 'f' || v[0] == 'F');
}

// write `len` bytes at `addr`, whatever the page protection (code or GOT)
bool poke(uintptr_t addr, const void* data, size_t len) {
    const long ps = sysconf(_SC_PAGESIZE);
    const uintptr_t start = addr & ~(uintptr_t)(ps - 1);
    const uintptr_t end = (addr + len + ps - 1) & ~(uintptr_t)(ps - 1);
    if (mprotect((void*)start, end - start, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        lg("mprotect(0x%08x) failed", (unsigned)addr);
        return false;
    }
    memcpy((void*)addr, data, len);
    __builtin___clear_cache((char*)addr, (char*)addr + len);
    return true;
}

bool sites_match(uintptr_t base, const coduo_site_t* s, int n, const char* who) {
    bool ok = true;
    for (int i = 0; i < n; ++i) {
        if (memcmp((const void*)(base + s[i].at), s[i].bytes, s[i].len) != 0) {
            lg("%s: '%s' at 0x%08x does not match - unknown build, %s left vanilla",
               who, s[i].what, (unsigned)(base + s[i].at), who);
            ok = false;
        }
    }
    return ok;
}

bool in_coduo_lnxded() {
    char exe[512];
    const ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n <= 0) return false;
    exe[n] = 0;
    const char* slash = strrchr(exe, '/');
    return strcmp(slash ? slash + 1 : exe, "coduo_lnxded") == 0;
}

// ---------------------------------------------------------------------------------
// state
// ---------------------------------------------------------------------------------
char g_shortversion[64] = "CoDUO+ " CODUOPLUS_VERSION;

// swing values the patched game code reads (yaw speed through `[eax+8]`, movefrac by fld)
float g_swing_yaw_speed = SWING_TORSO_YAW_SPEED;
float g_swing_movefrac  = SWING_TORSO_YAW_MOVEFRAC;

typedef void* (*dlopen_t)(const char*, int);
dlopen_t g_real_dlopen = nullptr;

// game module (refreshed on every load: map changes reload it, maybe elsewhere)
typedef char* (*ClientConnect_t)(int, int);
typedef void  (*G_RunFrame_t)(int);
typedef void  (*trap_Cvar_Register_t)(void*, const char*, const char*, int);
typedef void  (*trap_Cvar_Set_t)(const char*, const char*);
typedef void  (*trap_Cvar_StringBuf_t)(const char*, char*, int);
typedef void  (*trap_GetUserinfo_t)(int, char*, int);
typedef void  (*Bullet_Fire_t)(void*, uint32_t, uint32_t, void*, void*);  // spread/damage passed through as raw bits
typedef void  (*trap_LinkEntity_t)(void*);
struct Game {
    uintptr_t             base = 0;
    ClientConnect_t       ClientConnect = nullptr;
    G_RunFrame_t          G_RunFrame = nullptr;
    trap_Cvar_Register_t  Cvar_Register = nullptr;
    trap_Cvar_Set_t       Cvar_Set = nullptr;
    trap_Cvar_StringBuf_t Cvar_StringBuf = nullptr;
    trap_GetUserinfo_t    GetUserinfo = nullptr;
    Bullet_Fire_t         Bullet_Fire = nullptr;
    trap_LinkEntity_t     LinkEntity = nullptr;
    uint8_t*              entities = nullptr;
    bool                  swing_mirrored = false;
    bool                  hooked = false;
    // per-load publishing state (reset on every load)
    bool                  registered = false;
    long                  next_cfg_check_ms = 0;
    bool                  cfg_have = false;
    time_t                cfg_mtime = 0;
    bool                  cfg_first = true;
    std::string           published;
    int                   verify_frames = 0;
    int                   antilag = 0;            // g_antilag, polled once a second
    int                   antilag_max_ms = ANTILAG_MAXMS_DEFAULT;
    long                  next_antilag_poll_ms = 0;
} g;

alignas(16) char g_vmcvar[1024];      // scratch vmCvar_t for trap_Cvar_Register
char g_reject[160];

long now_ms() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

// ---------------------------------------------------------------------------------
// 6. large inodes. The engine imports the non-LFS __xstat (32-bit struct stat) and
// readdir (32-bit struct dirent); glibc fails both with EOVERFLOW as soon as an inode
// number needs more than 32 bits. These replacements make the 64-bit calls and hand the
// engine the 32-bit structs it was compiled against, with the inode folded to 32 bits
// (the engine never looks at it). This file is built WITHOUT _FILE_OFFSET_BITS=64, so
// `struct stat` / `struct dirent` here ARE the 32-bit layouts the engine uses.
// ---------------------------------------------------------------------------------
uint32_t fold_ino(uint64_t i) { return (uint32_t)(i ^ (i >> 32)); }

int lfs_xstat(int /*ver: _STAT_VER, always 3 here*/, const char* path, struct stat* out) {
    struct stat64 s;
    if (stat64(path, &s) != 0) return -1;
    if (s.st_size > 0x7fffffffLL) { errno = EOVERFLOW; return -1; }   // 32-bit off_t
    memset(out, 0, sizeof(*out));
    out->st_dev = s.st_dev;
    out->st_ino = fold_ino(s.st_ino);
    out->st_mode = s.st_mode;
    out->st_nlink = s.st_nlink;
    out->st_uid = s.st_uid;
    out->st_gid = s.st_gid;
    out->st_rdev = s.st_rdev;
    out->st_size = (off_t)s.st_size;
    out->st_blksize = s.st_blksize;
    out->st_blocks = (blkcnt_t)s.st_blocks;
    out->st_atim = s.st_atim;
    out->st_mtim = s.st_mtim;
    out->st_ctim = s.st_ctim;
    return 0;
}

// readdir's result must stay valid until the next readdir on the SAME stream - and the
// engine's file listing recurses into subdirectories and then reads d_name of the outer
// entry again - so one buffer per open DIR*, dropped on closedir. Node-based map:
// references survive rehashing. Spinlock: no pthread dependency, calls are rare.
struct DirSlots {
    std::unordered_map<DIR*, struct dirent> map;
    int lock = 0;
    void acquire() { while (__atomic_exchange_n(&lock, 1, __ATOMIC_ACQUIRE)) {} }
    void release() { __atomic_store_n(&lock, 0, __ATOMIC_RELEASE); }
};
DirSlots* g_dirs = new DirSlots;   // never destroyed: the engine may list files at exit

struct dirent* lfs_readdir(DIR* d) {
    struct dirent64* e = readdir64(d);
    if (!e) return nullptr;
    g_dirs->acquire();
    struct dirent& o = g_dirs->map[d];
    g_dirs->release();
    o.d_ino = fold_ino(e->d_ino);
    o.d_off = (__off_t)e->d_off;
    o.d_reclen = sizeof(o);
    o.d_type = e->d_type;
    snprintf(o.d_name, sizeof(o.d_name), "%s", e->d_name);
    return &o;
}

int lfs_closedir(DIR* d) {
    g_dirs->acquire();
    g_dirs->map.erase(d);
    g_dirs->release();
    return closedir(d);
}

// ---------------------------------------------------------------------------------
// 4. version gate
// ---------------------------------------------------------------------------------
int info_value_int(const char* info, const char* key) {
    const size_t klen = strlen(key);
    const char* p = info;
    while (*p == '\\') {
        const char* k = p + 1;
        const char* ke = strchr(k, '\\');
        if (!ke) break;
        const char* v = ke + 1;
        const char* ve = strchr(v, '\\');
        const size_t vlen = ve ? (size_t)(ve - v) : strlen(v);
        if ((size_t)(ke - k) == klen && strncmp(k, key, klen) == 0) {
            char b[32];
            const size_t n = vlen < 31 ? vlen : 31;
            memcpy(b, v, n);
            b[n] = 0;
            return atoi(b);
        }
        if (!ve) break;
        p = ve;
    }
    return 0;
}

char* hk_ClientConnect(int clientNum, int firstTime) {
    if (!env_off("CODUOPLUS_VERSION_GATE") && g.GetUserinfo) {
        char userinfo[1024];
        userinfo[0] = 0;
        g.GetUserinfo(clientNum, userinfo, sizeof(userinfo));
        const int cver = info_value_int(userinfo, CODUOPLUS_USERINFO_KEY);
        // vanilla clients have no key (0) and are always let in, as on Windows
        if (cver != 0 && cver < CODUOPLUS_NET_VERSION) {
            lg("version_gate: reject client %d (coduoplus=%d < %d)", clientNum, cver,
               CODUOPLUS_NET_VERSION);
            return g_reject;
        }
    }
    return g.ClientConnect(clientNum, firstTime);
}

// ---------------------------------------------------------------------------------
// 6. lag compensation (game thread: G_RunFrame records, Bullet_Fire rewinds)
// ---------------------------------------------------------------------------------
const char* env_or(const char* name, const char* dflt) {
    const char* v = getenv(name);
    return (v && v[0]) ? v : dflt;
}

void poll_antilag() {
    const long now = now_ms();
    if (now < g.next_antilag_poll_ms) return;
    g.next_antilag_poll_ms = now + 1000;
    char v[32] = "";
    g.Cvar_StringBuf(ANTILAG_CVAR, v, sizeof(v));
    const int mode = atoi(v);
    g.Cvar_StringBuf(ANTILAG_MAXMS_CVAR, v, sizeof(v));
    const int max_ms = atoi(v);
    if (mode != g.antilag || max_ms != g.antilag_max_ms)
        lg("antilag: %s=%d %s=%d%s", ANTILAG_CVAR, mode, ANTILAG_MAXMS_CVAR, max_ms,
           mode ? "" : " (off: shots hit where players are now)");
    g.antilag = mode;
    g.antilag_max_ms = max_ms;
}

void hk_Bullet_Fire(void* attacker, uint32_t spread, uint32_t damage, void* wp, void* self) {
    bool rewound = false;
    if (g.antilag > 0 && !patches::antilag::active()) {
        const int shooter = patches::antilag::shooter_of(g.entities, attacker, self);
        patches::antilag::Shot s;
        if (shooter >= 0) {
            rewound = patches::antilag::rewind(g.entities, shooter, g.antilag_max_ms, g.LinkEntity, &s);
            if (g.antilag >= 2)
                lg("antilag: client %d shot, world at %d (cmd %d, %d ms back), %d player(s) moved, max %.1f units",
                   shooter, s.target_time, s.cmd_time, s.now - s.target_time, s.moved, s.max_dist);
        }
    }
    g.Bullet_Fire(attacker, spread, damage, wp, self);
    if (rewound) patches::antilag::restore(g.entities, g.LinkEntity);
}

// ---------------------------------------------------------------------------------
// 3 + 5. publishing (game thread: G_RunFrame)
// ---------------------------------------------------------------------------------
std::string cfg_path() {
    const char* e = getenv("CODUOPLUS_COMPETITIVE_FILE");
    return (e && e[0]) ? e : "competitive.cfg";
}

void publish_competitive() {
    const long now = now_ms();
    if (now < g.next_cfg_check_ms) return;
    g.next_cfg_check_ms = now + 1000;

    const std::string path = cfg_path();
    struct stat64 st;   // 64-bit: the file may well sit on a big-inode filesystem
    const bool have = stat64(path.c_str(), &st) == 0;
    if (!g.cfg_first && have == g.cfg_have && (!have || st.st_mtime == g.cfg_mtime)) return;
    const bool first = g.cfg_first;
    g.cfg_first = false;
    g.cfg_have = have;
    g.cfg_mtime = have ? st.st_mtime : 0;

    std::string text;
    if (have) {
        if (FILE* f = fopen(path.c_str(), "rb")) {
            char b[4096];
            size_t n;
            while ((n = fread(b, 1, sizeof(b), f)) > 0 && text.size() < 64 * 1024) text.append(b, n);
            fclose(f);
        }
    }
    std::vector<std::string> rejected;
    const std::string spec = have ? patches::competitive_build_spec(text, &rejected) : std::string();
    for (const std::string& r : rejected)
        lg("competitive_sv: competitive.cfg line refused (format is `cvar value` or "
           "`cvar min max`, letters/digits/_.+- only): %s", r.c_str());
    std::vector<std::string> parts;
    if (!patches::competitive_chunk_spec(spec, parts))
        lg("competitive_sv: competitive.cfg too long (max ~%d chars) - tail dropped",
           patches::COMP_SV_CHUNK_MAX * patches::COMP_SV_CHUNKS);
    if (spec == g.published && !first) return;   // the first result is always logged
    g.published = spec;
    for (int i = 0; i < patches::COMP_SV_CHUNKS; ++i)
        g.Cvar_Set(patches::competitive_chunk_name(i).c_str(), parts[i].c_str());
    lg("competitive_sv: %s - publishing \"%s\"", have ? path.c_str() : "no competitive.cfg",
       spec.empty() ? "(empty: clients stay vanilla)" : spec.c_str());
    g.verify_frames = spec.empty() ? 0 : 3;
}

void hk_G_RunFrame(int levelTime) {
    if (!g.registered) {
        g.registered = true;
        // swing flag: SYSTEMINFO|ROM, set through the trap (Cvar_Set, force)
        g.Cvar_Register(g_vmcvar, SWING_SERVERINFO_CVAR, "0",
                        CODUOPLUS_CVAR_SYSTEMINFO | CODUOPLUS_CVAR_ROM);
        g.Cvar_Set(SWING_SERVERINFO_CVAR, g.swing_mirrored ? "1" : "0");
        char v[16] = "";
        g.Cvar_StringBuf(SWING_SERVERINFO_CVAR, v, sizeof(v));
        lg("swing_fix: server publishes %s %s (engine holds \"%s\")", SWING_SERVERINFO_CVAR,
           g.swing_mirrored ? "1" : "0", v);
        for (int i = 0; i < patches::COMP_SV_CHUNKS; ++i)
            g.Cvar_Register(g_vmcvar, patches::competitive_chunk_name(i).c_str(), "",
                            CODUOPLUS_CVAR_SYSTEMINFO);
        // 6. lag compensation: serverinfo, so server browsers show it; a +set or rcon
        // value wins over this default
        char d[16];
        snprintf(d, sizeof(d), "%d", ANTILAG_DEFAULT);
        g.Cvar_Register(g_vmcvar, ANTILAG_CVAR, env_or("CODUOPLUS_ANTILAG", d), CODUOPLUS_CVAR_SERVERINFO);
        snprintf(d, sizeof(d), "%d", ANTILAG_MAXMS_DEFAULT);
        g.Cvar_Register(g_vmcvar, ANTILAG_MAXMS_CVAR, env_or("CODUOPLUS_ANTILAG_MAXMS", d), 0);
        g.antilag = -1;                       // force the first poll to log the state
    }
    publish_competitive();
    poll_antilag();
    if (g.verify_frames > 0 && --g.verify_frames == 0) {
        char v[256] = "";
        g.Cvar_StringBuf(COMP_SPEC_CVAR, v, sizeof(v));
        lg("competitive_sv: engine holds sv_competitive \"%s\"", v);
    }
    g.G_RunFrame(levelTime);
    // after the frame: the positions the snapshot about to go out shows
    patches::antilag::record(g.entities, levelTime, g.LinkEntity);
}

// ---------------------------------------------------------------------------------
// game module load
// ---------------------------------------------------------------------------------
template <typename T> T sym(void* h, const char* name, uintptr_t base, uintptr_t rva, bool* ok) {
    void* p = dlsym(h, name);
    if ((uintptr_t)p != base + rva) {
        lg("game: %s at %p, expected 0x%08x - unknown build", name, p, (unsigned)(base + rva));
        *ok = false;
    }
    return (T)p;
}

void on_game_loaded(void* h, const char* file) {
    void* vm = dlsym(h, "vmMain");
    Dl_info info;
    if (!vm || !dladdr(vm, &info)) return;
    const uintptr_t base = (uintptr_t)info.dli_fbase;

    Game ng;
    ng.base = base;
    bool ok = (uintptr_t)vm == base + LNX_G_VMMAIN;
    ng.ClientConnect  = sym<ClientConnect_t>(h, "ClientConnect", base, LNX_G_CLIENTCONNECT, &ok);
    ng.G_RunFrame     = sym<G_RunFrame_t>(h, "G_RunFrame", base, LNX_G_RUNFRAME, &ok);
    ng.Cvar_Register  = sym<trap_Cvar_Register_t>(h, "trap_Cvar_Register", base, LNX_G_TRAP_CVAR_REGISTER, &ok);
    ng.Cvar_Set       = sym<trap_Cvar_Set_t>(h, "trap_Cvar_Set", base, LNX_G_TRAP_CVAR_SET, &ok);
    ng.Cvar_StringBuf = sym<trap_Cvar_StringBuf_t>(h, "trap_Cvar_VariableStringBuffer", base, LNX_G_TRAP_CVAR_STRINGBUF, &ok);
    ng.GetUserinfo    = sym<trap_GetUserinfo_t>(h, "trap_GetUserinfo", base, LNX_G_TRAP_GETUSERINFO, &ok);
    sym<void*>(h, "bg_swingSpeed", base, LNX_G_BG_SWINGSPEED, &ok);
    ng.Bullet_Fire    = sym<Bullet_Fire_t>(h, "Bullet_Fire", base, LNX_G_BULLET_FIRE, &ok);
    ng.LinkEntity     = sym<trap_LinkEntity_t>(h, "trap_LinkEntity", base, LNX_G_TRAP_LINKENTITY, &ok);
    ng.entities       = sym<uint8_t*>(h, "g_entities", base, LNX_G_ENTITIES, &ok);
    ok = ok && sites_match(base, LNX_GAME_SITES, sizeof(LNX_GAME_SITES) / sizeof(LNX_GAME_SITES[0]), "game");
    if (!ok) {
        lg("game: %s at 0x%08x is not the 1.51b game module - no CoDUO+ features this map", file,
           (unsigned)base);
        g = Game();
        return;
    }

    // 3. swing mirror: the same four values the client uses
    if (!env_off("CODUOPLUS_SWING")) {
        bool sw = true;
        // 1. movefrac: fld [ebx-0x1a1c0] -> fld [&g_swing_movefrac]
        uint8_t fld[6] = { 0xd9, 0x05 };
        const uint32_t mf = (uint32_t)(uintptr_t)&g_swing_movefrac;
        memcpy(fld + 2, &mf, 4);
        sw &= poke(base + LNX_G_SW_MOVEFRAC_FLD, fld, 6);
        // 2. torso yaw speed: mov eax,[ebx+0xeec] -> mov eax,&g_swing_yaw_speed-8 ; nop
        uint8_t mov[6] = { 0xb8, 0, 0, 0, 0, 0x90 };
        const uint32_t ys = (uint32_t)(uintptr_t)&g_swing_yaw_speed - 8;
        memcpy(mov + 1, &ys, 4);
        sw &= poke(base + LNX_G_SW_YAW_LOAD, mov, 6);
        // 3. legs tolerance 40 -> SWING_LEGS_TOLERANCE, 4. lean speed 0.15 -> SWING_TORSO_PITCH_SPEED
        const float legs = SWING_LEGS_TOLERANCE, pitch = SWING_TORSO_PITCH_SPEED;
        sw &= poke(base + LNX_G_SW_LEGS_TOL_IMM, &legs, 4);
        sw &= poke(base + LNX_G_SW_PITCH_SPEED_IMM, &pitch, 4);
        ng.swing_mirrored = sw;
        lg("swing_fix [server]: %s in 0x%08x (legs_tol=%.2f pitch_spd=%.2f yaw_spd=%.2f yaw_movefrac=%.2f)",
           sw ? "ON" : "FAILED", (unsigned)base, legs, pitch, g_swing_yaw_speed, g_swing_movefrac);
    } else {
        lg("swing_fix [server]: off (CODUOPLUS_SWING=0) - clients keep the vanilla swing here");
    }

    // 4 + 5. hooks through the PLT slots vmMain uses
    const uintptr_t cc = (uintptr_t)&hk_ClientConnect, rf = (uintptr_t)&hk_G_RunFrame;
    ng.hooked = poke(base + LNX_G_GOT_CLIENTCONNECT, &cc, 4) && poke(base + LNX_G_GOT_RUNFRAME, &rf, 4);
    // 6. lag compensation: the Bullet_Fire PLT slot every weapon goes through. A new
    // module has new entities: no history carries over.
    patches::antilag::reset();
    const uintptr_t bf = (uintptr_t)&hk_Bullet_Fire;
    const bool al = ng.hooked && poke(base + LNX_G_GOT_BULLET_FIRE, &bf, 4);
    lg("antilag: Bullet_Fire hook %s (%s / %s control it)", al ? "installed" : "FAILED",
       ANTILAG_CVAR, ANTILAG_MAXMS_CVAR);
    g = ng;
    snprintf(g_reject, sizeof(g_reject), "Update coduoplus to %s+ to join this server.", g_shortversion);
    lg("game: %s loaded at 0x%08x - ClientConnect gate %s, G_RunFrame hook %s (min coduoplus=%d)",
       file, (unsigned)base, g.hooked ? "installed" : "FAILED", g.hooked ? "installed" : "FAILED",
       CODUOPLUS_NET_VERSION);
}

void* hk_dlopen(const char* file, int mode) {
    void* h = g_real_dlopen(file, mode);
    if (h && file && strstr(file, "game.mp.uo.i386.so")) on_game_loaded(h, file);
    return h;
}

// ---------------------------------------------------------------------------------
// load: patch the engine, hook its dlopen
// ---------------------------------------------------------------------------------
__attribute__((constructor)) void coduoplus_init() {
    // LD_PRELOAD is inherited by every child (shell-outs): only act in the server itself
    if (!in_coduo_lnxded()) return;
    lg("CoDUO+ %s server library loaded (coduo_lnxded 1.51b)", CODUOPLUS_VERSION);
    if (!sites_match(0, LNX_EXE_SITES, sizeof(LNX_EXE_SITES) / sizeof(LNX_EXE_SITES[0]), "engine")) {
        lg("engine: not coduo_lnxded 1.51b - nothing patched");
        return;
    }

    // 1. snaps cap
    if (!env_off("CODUOPLUS_SNAPS")) {
        const uint8_t b40 = 40;
        const uint32_t d40 = 40;
        const bool ok = poke(LNX_SNAPS_CMP_IMM, &b40, 1) && poke(LNX_SNAPS_MOV_IMM, &d40, 4);
        lg("competitive: snaps cap 30 -> 40 %s", ok ? "applied" : "FAILED");
    }
    // 2. shortversion
    {
        const uint32_t p = (uint32_t)(uintptr_t)g_shortversion;
        const bool ok = poke(LNX_SHORTVERSION_OPERAND, &p, 4);
        lg("version_patch: shortversion -> \"%s\" %s", g_shortversion, ok ? "applied" : "FAILED");
    }
    // 6. large inodes
    if (!env_off("CODUOPLUS_LFS")) {
        const uintptr_t xs = (uintptr_t)&lfs_xstat, rd = (uintptr_t)&lfs_readdir, cd = (uintptr_t)&lfs_closedir;
        const bool ok = poke(LNX_EXE_GOT_XSTAT, &xs, 4) && poke(LNX_EXE_GOT_READDIR, &rd, 4) &&
                        poke(LNX_EXE_GOT_CLOSEDIR, &cd, 4);
        lg("lfs: engine stat/readdir -> 64-bit wrappers %s (any filesystem, any inode size)",
           ok ? "installed" : "FAILED");
    }
    // game module loads. NOT dlsym(RTLD_DEFAULT, "dlopen"): coduo_lnxded is a non-PIE
    // executable, which exports its own PLT stub (0x8049f1c) as the canonical address of
    // every function it imports - so the default lookup hands back that stub, the stub
    // jumps through the GOT slot we are about to repoint, and the "real" dlopen becomes
    // our hook calling itself until the stack overflows (seen: segfault at the first map
    // load). RTLD_NEXT = the next object after this library, i.e. libc/libdl, at the
    // version the engine binds (dlopen@GLIBC_2.1).
    g_real_dlopen = (dlopen_t)dlvsym(RTLD_NEXT, "dlopen", "GLIBC_2.1");
    if (!g_real_dlopen) g_real_dlopen = (dlopen_t)dlsym(RTLD_NEXT, "dlopen");
    if ((uintptr_t)g_real_dlopen >= LNX_EXE_BASE && (uintptr_t)g_real_dlopen < 0x08100000u) {
        lg("engine: dlopen resolves into the executable (%p) - not hooking", (void*)g_real_dlopen);
        g_real_dlopen = nullptr;
    }
    const uintptr_t hk = (uintptr_t)&hk_dlopen;
    const bool ok = g_real_dlopen && poke(LNX_EXE_GOT_DLOPEN, &hk, 4);
    lg("engine: dlopen hook %s - every game module load is patched", ok ? "installed" : "FAILED");
}

}  // namespace

// Lets `nm -D` / tests see what is in the file.
API const char* coduoplus_sv_version() { return "CoDUO+ " CODUOPLUS_VERSION " server (coduo_lnxded 1.51b)"; }
