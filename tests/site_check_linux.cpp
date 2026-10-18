// site_check_linux.cpp - the Linux server library's address table (server/linux/
// sites_151b.h) checked offline against coduo_lnxded / game.mp.uo.i386.so 1.51b.
//
//   site_check_linux <dir with coduo_lnxded and uo/game.mp.uo.i386.so>
//
// Same idea as site_check.cpp for the Windows binaries: every expected byte sequence
// the library compares at run time must be in the files, and each address is pinned to
// what it means (exported symbol, relocation, constant value, call target), so a wrong
// table fails here instead of on someone's server. Plain C++, no ELF headers needed:
// it runs on the Windows build machine too.

#include "sites_151b.h"
#include "shared/coduoplus_shared.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

int g_pass = 0, g_fail = 0;
void check(bool ok, const char* what) {
    if (ok) { ++g_pass; return; }
    ++g_fail;
    printf("FAIL %s\n", what);
}
#define CHECK(c) check((c), #c)

struct Elf {
    std::vector<uint8_t> file;
    struct Seg { uint32_t vaddr, memsz, off, filesz; };
    std::vector<Seg> segs;
    std::map<std::string, uint32_t> dynsym;            // name -> value
    std::map<std::string, uint32_t> jump_slots;        // name -> GOT slot address
    std::map<std::string, uint32_t> glob_dat;

    uint32_t u32f(uint32_t off) const { uint32_t v = 0; if (off + 4 <= file.size()) memcpy(&v, &file[off], 4); return v; }
    uint16_t u16f(uint32_t off) const { uint16_t v = 0; if (off + 2 <= file.size()) memcpy(&v, &file[off], 2); return v; }

    bool load(const std::string& path) {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) return false;
        uint8_t b[65536]; size_t n;
        while ((n = fread(b, 1, sizeof(b), f)) > 0) file.insert(file.end(), b, b + n);
        fclose(f);
        if (file.size() < 52 || memcmp(file.data(), "\x7f" "ELF", 4) != 0 || file[4] != 1) return false;
        const uint32_t phoff = u32f(28), shoff = u32f(32);
        const uint16_t phent = u16f(42), phnum = u16f(44), shent = u16f(46), shnum = u16f(48);
        for (int i = 0; i < phnum; ++i) {
            const uint32_t p = phoff + i * phent;
            if (u32f(p) == 1)   // PT_LOAD
                segs.push_back({u32f(p + 8), u32f(p + 20), u32f(p + 4), u32f(p + 16)});
        }
        // section headers: .dynsym/.dynstr and the REL tables
        struct Sh { uint32_t type, addr, off, size, link, entsize; };
        std::vector<Sh> sh;
        for (int i = 0; i < shnum; ++i) {
            const uint32_t s = shoff + i * shent;
            sh.push_back({u32f(s + 4), u32f(s + 12), u32f(s + 16), u32f(s + 20), u32f(s + 24), u32f(s + 36)});
        }
        int dynsym_i = -1;
        for (int i = 0; i < (int)sh.size(); ++i) if (sh[i].type == 11) dynsym_i = i;   // SHT_DYNSYM
        if (dynsym_i < 0) return true;
        const Sh& ds = sh[dynsym_i];
        const Sh& str = sh[ds.link];
        std::vector<std::string> names;
        for (uint32_t o = 0; o + 16 <= ds.size; o += 16) {
            const uint32_t e = ds.off + o;
            const char* nm = (const char*)&file[str.off + u32f(e)];
            names.push_back(nm);
            if (u16f(e + 14) != 0 && nm[0]) dynsym[nm] = u32f(e + 4);   // defined
        }
        for (const Sh& s : sh) {
            if (s.type != 9) continue;   // SHT_REL
            for (uint32_t o = 0; o + 8 <= s.size; o += 8) {
                const uint32_t r_off = u32f(s.off + o), info = u32f(s.off + o + 4);
                const uint32_t symi = info >> 8, type = info & 0xff;
                if (symi >= names.size()) continue;
                if (type == 7) jump_slots[names[symi]] = r_off;   // R_386_JUMP_SLOT
                if (type == 6) glob_dat[names[symi]] = r_off;     // R_386_GLOB_DAT
            }
        }
        return true;
    }
    // byte at a virtual address (0 if not file-backed)
    const uint8_t* at(uint32_t va, uint32_t n) const {
        for (const Seg& s : segs)
            if (va >= s.vaddr && va + n <= s.vaddr + s.filesz) return &file[s.off + va - s.vaddr];
        return nullptr;
    }
    bool eq(uint32_t va, const uint8_t* b, uint32_t n) const { const uint8_t* p = at(va, n); return p && memcmp(p, b, n) == 0; }
    uint32_t u32(uint32_t va) const { const uint8_t* p = at(va, 4); uint32_t v = 0; if (p) memcpy(&v, p, 4); return v; }
    float f32(uint32_t va) const { const uint8_t* p = at(va, 4); float v = 0; if (p) memcpy(&v, p, 4); return v; }
    uint32_t call_target(uint32_t va) const { return va + 5 + u32(va + 1); }
};

void check_sites(const Elf& e, uint32_t base, const coduo_site_t* s, int n) {
    for (int i = 0; i < n; ++i) {
        char what[96];
        snprintf(what, sizeof(what), "site '%s' at 0x%08x matches", s[i].what, (unsigned)(base + s[i].at));
        check(e.eq(base + s[i].at, s[i].bytes, s[i].len), what);
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: site_check_linux <dir>\n"); return 2; }
    Elf exe, game;
    const std::string dir = argv[1];
    if (!exe.load(dir + "/coduo_lnxded") || !game.load(dir + "/uo/game.mp.uo.i386.so")) {
        printf("cannot load the 1.51b Linux server binaries from %s\n", dir.c_str());
        return 2;
    }

    // ---- engine -----------------------------------------------------------------
    check_sites(exe, 0, LNX_EXE_SITES, sizeof(LNX_EXE_SITES) / sizeof(LNX_EXE_SITES[0]));
    CHECK(LNX_SNAPS_CMP_IMM == 0x0808d48d + 3 && exe.at(LNX_SNAPS_CMP_IMM, 1)[0] == 30);
    CHECK(LNX_SNAPS_MOV_IMM == 0x0808d493 + 3 && exe.u32(LNX_SNAPS_MOV_IMM) == 30);
    // the clamp reads the "snaps" userinfo key a few instructions before
    {
        bool snaps_ref = false;
        const uint8_t* p = exe.at(0x0808d44f, 8);
        snaps_ref = p && exe.u32(0x0808d453) != 0 && exe.at(exe.u32(0x0808d453), 6) &&
                    memcmp(exe.at(exe.u32(0x0808d453), 6), "snaps\0", 6) == 0;
        CHECK(snaps_ref);
    }
    // ...and stores 1000 / snaps in client_t+0x9b038 (snapshotMsec, as on Windows)
    CHECK(exe.eq(0x0808d4ae, (const uint8_t*)"\x89\x81\x38\xb0\x09\x00", 6));
    // shortversion: operand points at "1.51", the next store names "shortversion"
    CHECK(exe.u32(LNX_SHORTVERSION_OPERAND) == LNX_SHORTVERSION_ORIG);
    CHECK(exe.at(exe.u32(0x080719dc), 13) && memcmp(exe.at(exe.u32(0x080719dc), 13), "shortversion\0", 13) == 0);
    // the dlopen PLT stub jumps through the relocation's slot
    CHECK(exe.jump_slots.count("dlopen") && exe.jump_slots.at("dlopen") == LNX_EXE_GOT_DLOPEN);
    CHECK(exe.u32(0x08049f1c + 2) == LNX_EXE_GOT_DLOPEN);
    // same for the large-inode wrappers: slot = relocation = what the PLT stub jumps through
    CHECK(exe.jump_slots.count("__xstat") && exe.jump_slots.at("__xstat") == LNX_EXE_GOT_XSTAT);
    CHECK(exe.jump_slots.count("readdir") && exe.jump_slots.at("readdir") == LNX_EXE_GOT_READDIR);
    CHECK(exe.jump_slots.count("closedir") && exe.jump_slots.at("closedir") == LNX_EXE_GOT_CLOSEDIR);
    CHECK(exe.u32(0x08049a8c + 2) == LNX_EXE_GOT_XSTAT);
    CHECK(exe.u32(0x08049e9c + 2) == LNX_EXE_GOT_READDIR);
    CHECK(exe.u32(0x08049cac + 2) == LNX_EXE_GOT_CLOSEDIR);
    // ...and they are the only inode-sensitive file calls it imports (no stat64, lstat,
    // fstat, scandir, glob ... that would still fail on a big-inode filesystem)
    for (const char* f : {"__lxstat", "__fxstat", "stat", "lstat", "fstat", "scandir", "glob", "readdir_r"})
        check(!exe.jump_slots.count(f), (std::string("engine does not import ") + f).c_str());

    // ---- game module -------------------------------------------------------------
    check_sites(game, 0, LNX_GAME_SITES, sizeof(LNX_GAME_SITES) / sizeof(LNX_GAME_SITES[0]));
    struct { const char* name; uint32_t rva; } syms[] = {
        {"vmMain", LNX_G_VMMAIN}, {"ClientConnect", LNX_G_CLIENTCONNECT}, {"G_RunFrame", LNX_G_RUNFRAME},
        {"trap_Cvar_Register", LNX_G_TRAP_CVAR_REGISTER}, {"trap_Cvar_Set", LNX_G_TRAP_CVAR_SET},
        {"trap_Cvar_VariableStringBuffer", LNX_G_TRAP_CVAR_STRINGBUF},
        {"trap_GetUserinfo", LNX_G_TRAP_GETUSERINFO},
        {"Bullet_Fire", LNX_G_BULLET_FIRE}, {"trap_LinkEntity", LNX_G_TRAP_LINKENTITY},
        {"g_entities", LNX_G_ENTITIES}, {"G_DObjCalcPose", LNX_G_DOBJCALCPOSE}, {"bg_swingSpeed", LNX_G_BG_SWINGSPEED},
    };
    for (auto& s : syms) {
        char what[96];
        snprintf(what, sizeof(what), "export %s at 0x%x", s.name, s.rva);
        check(game.dynsym.count(s.name) && game.dynsym.at(s.name) == s.rva, what);
    }
    // GOT slots we repoint = the JUMP_SLOT relocations, and the PLT stubs use them
    CHECK(game.jump_slots.count("ClientConnect") && game.jump_slots.at("ClientConnect") == LNX_G_GOT_CLIENTCONNECT);
    CHECK(game.jump_slots.count("G_RunFrame") && game.jump_slots.at("G_RunFrame") == LNX_G_GOT_RUNFRAME);
    CHECK(game.u32(0x000186c4 + 2) == LNX_G_GOT_CLIENTCONNECT - LNX_G_GOT);
    CHECK(game.u32(0x00018464 + 2) == LNX_G_GOT_RUNFRAME - LNX_G_GOT);
    // vmMain's calls go to those PLT stubs
    CHECK(game.call_target(0x00056b43) == 0x000186c4);
    CHECK(game.call_target(0x00056bca) == 0x00018464);
    // antilag: Bullet_Fire's GOT slot, its PLT stub, FireWeapon's call through it, and
    // trap_LinkEntity is syscall 53 (SV_LinkEntity)
    CHECK(game.jump_slots.count("Bullet_Fire") && game.jump_slots.at("Bullet_Fire") == LNX_G_GOT_BULLET_FIRE);
    CHECK(game.u32(0x00016e34 + 2) == LNX_G_GOT_BULLET_FIRE - LNX_G_GOT);
    CHECK(game.call_target(0x0007be48) == 0x00016e34);
    CHECK(game.eq(LNX_G_TRAP_LINKENTITY + 0x19, (const uint8_t*)"\xc7\x04\x24\x35\x00\x00\x00", 7));
    // the entity fields antilag_core reads, at their anchors: inuse byte (G_InitGentity),
    // r.currentAngles / r.currentOrigin (G_DObjGetWorldBoneIndexMatrix), pers.cmd
    // (ClientThink), sessionState == SPECTATOR (ClientThink_real), health (G_Damage)
    CHECK(game.eq(0x000796e3, (const uint8_t*)"\xc6\x80\x6c\x01\x00\x00\x01", 7));
    CHECK(game.eq(0x000790e5, (const uint8_t*)"\x05\x48\x01\x00\x00", 5));
    CHECK(game.eq(0x000790f5, (const uint8_t*)"\x8b\x80\x3c\x01\x00\x00", 6));
    CHECK(game.eq(0x00041d9e, (const uint8_t*)"\x05\x24\x45\x00\x00", 5));
    CHECK(game.eq(0x00040e63, (const uint8_t*)"\x83\xb8\x04\x45\x00\x00\x02", 7));
    CHECK(game.eq(0x000501b5, (const uint8_t*)"\x8b\x80\x40\x02\x00\x00", 6));
    // the trap syscall numbers (mov [esp],N before the syscall pointer call)
    CHECK(game.eq(LNX_G_TRAP_CVAR_REGISTER + 0x2e, (const uint8_t*)"\xc7\x04\x24\x04\x00\x00\x00", 7));
    CHECK(game.eq(LNX_G_TRAP_CVAR_SET + 0x20, (const uint8_t*)"\xc7\x04\x24\x06\x00\x00\x00", 7));
    CHECK(game.eq(LNX_G_TRAP_GETUSERINFO + 0x27, (const uint8_t*)"\xc7\x04\x24\x21\x00\x00\x00", 7));
    // swing: the movefrac fld reads the 0.3f at GOT-0x1a1c0, the yaw load's GOT slot is
    // bg_swingSpeed's GLOB_DAT, and the immediates are 40.0 / 0.15
    CHECK(game.f32(LNX_G_GOT - 0x1a1c0) == 0.3f);
    CHECK(game.u32(LNX_G_SW_MOVEFRAC_FLD + 2) == (uint32_t)(-0x1a1c0));
    CHECK(game.glob_dat.count("bg_swingSpeed") && game.glob_dat.at("bg_swingSpeed") == LNX_G_GOT + 0xeec);
    CHECK(game.f32(LNX_G_SW_LEGS_TOL_IMM) == 40.0f);
    CHECK(game.f32(LNX_G_SW_PITCH_SPEED_IMM) == 0.15f);
    // the shared values are what the Windows client uses
    CHECK(SWING_LEGS_TOLERANCE == 0.0f && SWING_TORSO_PITCH_SPEED == 1.0f &&
          SWING_TORSO_YAW_SPEED == 1.0f && SWING_TORSO_YAW_MOVEFRAC == 0.0f);

    printf("site_check_linux: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
