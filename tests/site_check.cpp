// Offline patch-site verification against the stock CoD:UO 1.51 binaries.
//
//   site_check <dir-with-CoDUOMP.exe-and-uo_*_mp_x86.dll> [<path-to-mss32_proxy.def>]
//
// Every runtime patch in the mod checks the original bytes before it writes.
// This test runs the same comparison ahead of time, using the same constants
// from the mod's own headers. So a wrong address fails here, on the build
// machine, not as a skipped patch (or a crash) on a player's PC.
//
// The checks deliberately go one step further than the runtime guards: besides
// "the byte we overwrite is what we expect", they pin the address to its
// MEANING - the push next to it names the right cvar, the call goes to the
// function we think it does, the global is the one that function reads. That is
// what turns a byte match (which a coincidence can satisfy) into evidence.
//
// Rules:
//  - Each feature group in core/game.h port:: has a check function below.
//  - A group that is switched ON must have checks, and all must pass.
//  - A group that is OFF is still checked if it has checks, so a branch can
//    land its addresses before flipping the switch.

#include "check.h"
#include "image.h"

#include "core/game.h"
#include "features/settings_menu.h"
#include "input/rinput.h"
#include "netcode/competitive.h"
#include "netcode/protocol_patch.h"
#include "netcode/version_patch.h"
#include "performance/frame_limiter.h"
#include "video/fullscreen_patch.h"
#include "video/widescreen_fix.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

TestRun g_run;

namespace {

Image g_exe, g_cgame, g_game, g_ui;
std::string g_def_path;

constexpr uint32_t EXE = 0x400000;  // RVA constants in the headers are relative to this

// --- small helpers over an Image -----------------------------------------------

// `push imm32` at va whose operand points at the NUL-terminated string s.
bool push_str(const Image& im, uint32_t va, const char* s) {
    return im.has(va, 5) && im.u8(va) == 0x68 && im.cstr_eq(im.u32(va + 1), s);
}

// Does the 32-bit value v appear anywhere in [va, va+len)? Used to show a function
// reads a given global without pinning the exact instruction.
bool contains_u32(const Image& im, uint32_t va, uint32_t len, uint32_t v) {
    for (uint32_t a = va; a + 4 <= va + len; ++a)
        if (im.u32(a) == v) return true;
    return false;
}

bool is_call_to(const Image& im, uint32_t va, uint32_t target) {
    return im.u8(va) == 0xe8 && im.call_target(va) == target;
}

// ---------------------------------------------------------------------------
// identity: the constants core/game.cpp uses to recognise CoDUOMP.exe 1.51
// ---------------------------------------------------------------------------
void check_identity() {
    using namespace patches;
    CHECK(g_exe.base == GAME_EXE_BASE);
    CHECK(g_exe.timestamp == GAME_PE_TIMESTAMP);
    CHECK(g_exe.bytes_eq(GAME_SHORTVERSION_STR_VA, {'1', '.', '5', '1', 0}));
    // module bases the engine expects (fixed, no ASLR in 2004 builds)
    CHECK(g_cgame.base == 0x30000000);
    CHECK(g_game.base == 0x20000000);
    CHECK(g_ui.base == 0x40000000);
    // window class used by the single-instance guard
    CHECK_MSG(!g_exe.find_cstr(GAME_WINDOW_CLASS).empty(), "window class string present in exe");
}

// ---------------------------------------------------------------------------
// proxy: mss32.dll is loaded through CoDUOMP.exe's import table, so the loader
// refuses to start the game if the proxy lacks ANY name the exe imports.
// ---------------------------------------------------------------------------
void check_proxy() {
    std::ifstream f(g_def_path);
    CHECK_MSG(f.good(), "mss32_proxy.def readable");
    std::set<std::string> exported;
    std::string line;
    while (std::getline(f, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string name = line.substr(0, eq);
        name.erase(0, name.find_first_not_of(" \t"));
        name.erase(name.find_last_not_of(" \t\r") + 1);
        exported.insert(name);
    }
    const std::vector<std::string> imports = g_exe.imports_of("mss32.dll");
    CHECK_MSG(imports.size() > 50, "CoDUOMP.exe imports from mss32.dll found");
    for (const std::string& name : imports)
        CHECK_MSG(exported.count(name) == 1, ("forwarded: " + name).c_str());
}

// ---------------------------------------------------------------------------
// engine: every CoDUOMP.exe address the ENGINE group uses
// ---------------------------------------------------------------------------
void check_engine_cvar_api() {
    using namespace patches;
    // Cvar_Get: com_maxfps is registered through it in Com_Init
    CHECK(push_str(g_exe, 0x43be6f, "com_maxfps"));
    CHECK(is_call_to(g_exe, 0x43be7a, CODMP_CVAR_GET_VA));
    // ...and it bounds-checks the cvar count against MAX_CVARS (0x7ff) - the global
    // the watcher polls to know the cvar system is up.
    CHECK(contains_u32(g_exe, CODMP_CVAR_GET_VA, 0x200, CODMP_CVAR_COUNT_VA));
    // Cvar_FindVar: "push esi; push edi; mov edi,[esp+0xc]" then hash-table lookup
    CHECK(g_exe.bytes_eq(CODMP_CVAR_FINDVAR_VA, {0x56, 0x57, 0x8b, 0x7c, 0x24, 0x0c}));
    CHECK(g_exe.bytes_eq(CODMP_CVAR_FINDVAR_VA + 0x0d, {0x8b, 0x34, 0x85, 0xa0, 0x87, 0x99, 0x00}));
    CHECK(CODMP_CVAR_FINDVAR_VA2 == CODMP_CVAR_FINDVAR_VA);
    // Cvar_Set = Cvar_Set2(name, value, force=1)
    CHECK(g_exe.bytes_eq(CODMP_CVAR_SET_VA, {0x8b, 0x44, 0x24, 0x08, 0x8b, 0x4c, 0x24, 0x04,
                                             0x6a, 0x01, 0x50, 0x51}));
    const uint32_t cvar_set2 = g_exe.call_target(CODMP_CVAR_SET_VA + 12);
    CHECK(g_exe.u8(CODMP_CVAR_SET_VA + 12) == 0xe8);
    // Cvar_Set2 is the function that refuses ROM cvars - the competitive lock relies on it
    const std::vector<uint32_t> ro = g_exe.find_cstr("%s is read only.\n");
    CHECK(ro.size() == 1 && contains_u32(g_exe, cvar_set2, 0x300, ro.empty() ? 0 : ro[0]));
    // Cbuf_ExecuteText: switch on exec_when, else Com_Error("... bad exec_when")
    CHECK(g_exe.bytes_eq(CODMP_CBUF_EXECTEXT_VA, {0x8b, 0x44, 0x24, 0x04, 0x83, 0xe8, 0x00}));
    //   42bcff  c7 44 24 08 <msg>  mov [esp+8], "\x15Cbuf_ExecuteText: bad exec_when"
    // (0x15 is the engine's print-level prefix byte)
    CHECK(g_exe.bytes_eq(CODMP_CBUF_EXECTEXT_VA + 0x0f, {0xc7, 0x44, 0x24, 0x08}));
    CHECK(g_exe.cstr_eq(g_exe.u32(CODMP_CBUF_EXECTEXT_VA + 0x13), "\x15" "Cbuf_ExecuteText: bad exec_when"));
}

void check_engine_version_fullscreen_refresh() {
    using namespace patches;
    // shortversion: push 0x44 / push "1.51" / push "shortversion"
    const uint32_t sv = EXE + CODMP_SHORTVERSION_PUSH_OPCODE_RVA;
    CHECK(sv + 1 == EXE + CODMP_SHORTVERSION_PUSH_OPERAND_RVA);
    CHECK(g_exe.bytes_eq(sv - 2, {0x6a, 0x44}));
    CHECK(g_exe.u8(sv) == 0x68 && g_exe.u32(sv + 1) == CODMP_SHORTVERSION_ORIGINAL_PTR);
    CHECK(g_exe.cstr_eq(CODMP_SHORTVERSION_ORIGINAL_PTR, "1.51"));
    CHECK(push_str(g_exe, sv + 5, "shortversion"));

    // r_fullscreen: push 0x21 / push "1" / push "r_fullscreen"
    const uint32_t fs = EXE + CODMP_RFULLSCREEN_PUSH_OPCODE_RVA;
    CHECK(fs + 1 == EXE + CODMP_RFULLSCREEN_PUSH_OPERAND_RVA);
    CHECK(g_exe.bytes_eq(fs - 2, {0x6a, 0x21}));
    CHECK(g_exe.u8(fs) == 0x68 && g_exe.u32(fs + 1) == CODMP_RFULLSCREEN_ORIGINAL_PTR);
    CHECK(g_exe.cstr_eq(CODMP_RFULLSCREEN_ORIGINAL_PTR, "1"));
    CHECK(push_str(g_exe, fs + 5, "r_fullscreen"));

    // r_displayRefresh range max 200.0f, right after its Cvar_Get
    CHECK(push_str(g_exe, 0x4c3feb, "r_displayRefresh"));
    CHECK(g_exe.u8(CODMP_REFRESH_CAP_IMM_VA - 1) == 0x68);
    CHECK(g_exe.u32(CODMP_REFRESH_CAP_IMM_VA) == 0x43480000u);  // 200.0f
    CHECK(CODMP_REFRESH_CAP_IMM_VA - 0x4c3feb < 0x20);
}

void check_engine_frame_limiter() {
    using namespace patches;
    const uint32_t call = EXE + CODMP_FRAME_LIMIT_CALL_OPCODE_RVA;
    CHECK(call + 1 == EXE + CODMP_FRAME_LIMIT_CALL_OPERAND_RVA);
    CHECK(is_call_to(g_exe, call, CODMP_FRAME_LIMIT_ORIGINAL_TARGET));
    // Com_EventLoop: 0x8054-byte frame (the 32 KB packet buffer) ...
    CHECK(g_exe.bytes_eq(CODMP_FRAME_LIMIT_ORIGINAL_TARGET, {0xb8, 0x54, 0x80, 0x00, 0x00}));
    // ... and its "oversize packet" error string
    const std::vector<uint32_t> ov = g_exe.find_cstr("Com_EventLoop: oversize packet\n");
    CHECK(ov.size() == 1 && contains_u32(g_exe, CODMP_FRAME_LIMIT_ORIGINAL_TARGET, 0x300, ov[0]));
    // the loop: mov ecx,[lastTime] right after the call, jl back to the call
    CHECK(g_exe.bytes_eq(call + 5, {0x8b, 0x0d}) && g_exe.u32(call + 7) == CODMP_FRAME_LIMIT_LASTTIME_VA);
    CHECK(g_exe.bytes_eq(CODMP_FRAME_LIMIT_LOOP_JL_VA, {0x7c, 0xdc}));
    CHECK(CODMP_FRAME_LIMIT_LOOP_JL_VA + 2 - 0x24 == call);  // jl target is the call
    CHECK(g_exe.bytes_eq(CODMP_FRAME_LIMIT_LOOP_JL_VA - 2, {0x3b, 0xf7}));  // cmp esi,edi
    // minMsec reads com_maxfps from the slot we read, and dedicated from the other
    CHECK(g_exe.u8(0x43c681) == 0xa1 && g_exe.u32(0x43c682) == CODMP_COM_MAXFPS_DVAR_SLOT_VA);
    CHECK(contains_u32(g_exe, 0x43c681, 0x20, CODMP_COM_DEDICATED_DVAR_SLOT_VA));
    // slots are the ones Com_Init stores Cvar_Get("com_maxfps"/"dedicated") into
    CHECK(g_exe.u8(0x43be91) == 0xa3 && g_exe.u32(0x43be92) == CODMP_COM_MAXFPS_DVAR_SLOT_VA);
    CHECK(push_str(g_exe, 0x43bdec, "dedicated"));
    CHECK(g_exe.u8(0x43bdfe) == 0xa3 && g_exe.u32(0x43bdff) == CODMP_COM_DEDICATED_DVAR_SLOT_VA);
    CHECK(CODMP_DVAR_INTEGER_OFFSET == 0x20);
}

void check_engine_protocol() {
    using namespace patches;
    CHECK(g_exe.cstr_eq(0x5942f0, "%i"));
    // every site holds 22
    const uint32_t sites[] = {CODMP_PROTO_SERVER_CMP_VA, CODMP_PROTO_CLIENT_EMIT_VA,
                              CODMP_PROTO_GETINFO_VA, CODMP_PROTO_CVAR_VA,
                              CODMP_PROTO_BOT_VA, CODMP_PROTO_REJECTMSG_VA};
    for (uint32_t va : sites) CHECK_MSG(g_exe.u8(va) == PROTOCOL_VANILLA_UO, "protocol site holds 22");
    // SV_DirectConnect: cmp esi,22 / je, reading the "protocol" userinfo key
    CHECK(g_exe.bytes_eq(CODMP_PROTO_SERVER_CMP_VA - 2, {0x83, 0xfe}));
    CHECK(contains_u32(g_exe, CODMP_PROTO_SERVER_CMP_VA - 0x30, 0x30, 0x5942e4));  // "protocol"
    CHECK(g_exe.cstr_eq(0x5942e4, "protocol"));
    // reject message: push 22 / push esi / push "rejected connect ... (should be %i)"
    CHECK(g_exe.u8(CODMP_PROTO_REJECTMSG_VA - 1) == 0x6a);
    CHECK(g_exe.u8(CODMP_PROTO_REJECTMSG_VA + 1) == 0x56);
    CHECK(push_str(g_exe, CODMP_PROTO_REJECTMSG_VA + 2,
                   "    rejected connect from protocol version %i (should be %i)\n"));
    // the three va("%i", 22) sites
    for (uint32_t va : {CODMP_PROTO_CLIENT_EMIT_VA, CODMP_PROTO_GETINFO_VA, CODMP_PROTO_CVAR_VA}) {
        CHECK_MSG(g_exe.u8(va - 1) == 0x6a && push_str(g_exe, va + 1, "%i"), "push 22 ; push \"%i\"");
    }
    // ...the cvar one feeds Cvar_Get("protocol", ..., 0x44)
    CHECK(push_str(g_exe, CODMP_PROTO_CVAR_VA + 0x10, "protocol"));
    // bot userinfo: push 22 / push edx / lea eax,[esp+0x64] / push "connect ...\protocol\%d"
    CHECK(g_exe.u8(CODMP_PROTO_BOT_VA - 1) == 0x6a);
    CHECK(g_exe.u8(CODMP_PROTO_BOT_VA + 6) == 0x68 &&
          strstr((const char*)g_exe.at(g_exe.u32(CODMP_PROTO_BOT_VA + 7)), "\\protocol\\%d\"") != nullptr);

    // master hostname slot, and room for CODMP_MASTER_HOST_MAX chars + NUL
    CHECK(g_exe.cstr_eq(CODMP_MASTER_HOST_VA, "coduomaster.activision.com"));
    CHECK(g_exe.u8(CODMP_MASTER_HOST_VA + 26) == 0 && g_exe.u8(CODMP_MASTER_HOST_VA + 27) == 0);
    CHECK(CODMP_MASTER_HOST_VA + CODMP_MASTER_HOST_MAX + 1 == 0x5930b8);  // next string
    CHECK(strlen(CODMP_MASTER_HOST_LEGACY) <= CODMP_MASTER_HOST_MAX);
    CHECK(push_str(g_exe, 0x4164e4, "coduomaster.activision.com"));       // CL_GlobalServers
    CHECK(push_str(g_exe, 0x460f5d, "coduomaster.activision.com"));       // SV_MasterHeartbeat
    CHECK(g_exe.u8(0x460f62) == 0xbb && g_exe.u32(0x460f63) == CODMP_MASTER_CACHE_VA);  // ...resolves into the cache
    // getservers format: 14 bytes + 2 of padding, then the master hostname
    CHECK(g_exe.cstr_eq(CODMP_GETSERVERS_FMT_VA, "getservers %s"));
    CHECK(CODMP_GETSERVERS_FMT_VA + 16 == CODMP_MASTER_HOST_VA);
    CHECK(push_str(g_exe, 0x41653c, "getservers %s"));
    // browser filter float: fld [22.0f], read only by CL_ServerInfoPacket
    CHECK(g_exe.f32(CODMP_BROWSER_PROTO_FLT_VA) == 22.0f);
    CHECK(g_exe.bytes_eq(0x415647, {0xd9, 0x05}) && g_exe.u32(0x415649) == CODMP_BROWSER_PROTO_FLT_VA);
    CHECK(g_exe.xrefs(CODMP_BROWSER_PROTO_FLT_VA).size() == 1);
}

void check_engine_competitive() {
    using namespace patches;
    // snaps cap: cmp ecx,30 / jle / mov ecx,30 in SV_UserinfoChanged
    CHECK(g_exe.bytes_eq(CODMP_SNAPS_CAP_CMP_VA - 2, {0x83, 0xf9, CODMP_SNAPS_CAP_OLD}));
    CHECK(g_exe.bytes_eq(CODMP_SNAPS_CAP_MOV_VA - 1, {0xb9, CODMP_SNAPS_CAP_OLD, 0, 0, 0}));
    CHECK(CODMP_SNAPS_CAP_MOV_VA - CODMP_SNAPS_CAP_CMP_VA == 4);
    CHECK(contains_u32(g_exe, CODMP_SNAPS_CAP_CMP_VA - 0x60, 0x60, 0x593800));  // reads "snaps"
    CHECK(g_exe.cstr_eq(0x593800, "snaps"));
    // cl_maxpackets cap: cmp eax,100 ... push "100" ; push "cl_maxpackets"
    CHECK(g_exe.bytes_eq(CODMP_MAXPACKETS_CAP_VA - 2, {0x83, 0xf8, CODMP_MAXPACKETS_OLD}));
    CHECK(push_str(g_exe, CODMP_MAXPACKETS_CAP_VA + 5, "100"));
    CHECK(push_str(g_exe, CODMP_MAXPACKETS_CAP_VA + 10, "cl_maxpackets"));
    // live systeminfo: CL_SystemInfoChanged reads stringOffsets[1] + stringData
    CHECK(g_exe.bytes_eq(0x418b91, {0x8b, 0x35}) && g_exe.u32(0x418b93) == CODMP_GS_SYSTEMINFO_OFF_VA);
    CHECK(g_exe.bytes_eq(0x418b97, {0x8d, 0xb6}) && g_exe.u32(0x418b99) == CODMP_GS_STRINGDATA_VA);
    // stringOffsets[2048] then stringData: [1] is 4 bytes in, data starts 0x2000 after [0]
    CHECK(CODMP_GS_SYSTEMINFO_OFF_VA - 4 + 0x2000 == CODMP_GS_STRINGDATA_VA);
    CHECK(CODMP_MAX_GAMESTATE_CHARS == 0x5000);
    // sv_cheats is SYSTEMINFO|ROM (0x48) - the channel the competitive spec rides on
    CHECK(g_exe.bytes_eq(0x46067d, {0x6a, COMP_CVAR_SYSTEMINFO | COMP_CVAR_ROM}) &&
          push_str(g_exe, 0x460684, "sv_cheats"));
}

void check_engine_mouse() {
    using namespace patches;
    // IN_GetMouseDelta / IN_MouseMove: GetCursorPos, then SetCursorPos(centre x, centre y)
    for (uint32_t fn : {0x469ab0u, 0x469c20u}) {
        CHECK(contains_u32(g_exe, fn, 0x30, CODMP_MOUSE_CENTER_X_VA));
        CHECK(contains_u32(g_exe, fn, 0x30, CODMP_MOUSE_CENTER_Y_VA));
    }
    // exactly two call sites of GetCursorPos (call [IAT]) - both in the functions above.
    // The slot has a third reference: the linker's import thunk `jmp [GetCursorPos]` at
    // 0x56c2d6, which nothing calls. Either way every caller goes through the IAT slot
    // rinput hooks, so none can bypass it.
    const uint32_t gcp = g_exe.iat_slot("user32.dll", "GetCursorPos");
    CHECK(gcp == 0x5862a0);
    int calls = 0;
    for (uint32_t r : g_exe.xrefs(gcp)) {
        if (g_exe.bytes_eq(r - 2, {0xff, 0x15})) {
            ++calls;
            CHECK_MSG(r - 2 > 0x469ab0 && r - 2 < 0x469c80, "GetCursorPos called from IN_* only");
        } else {
            CHECK_MSG(g_exe.bytes_eq(r - 2, {0xff, 0x25}) && g_exe.xrefs(r - 2).empty(),
                      "other GetCursorPos ref is an unreferenced import thunk");
        }
    }
    CHECK(calls == 2);
}

void check_engine() {
    check_engine_cvar_api();
    check_engine_version_fullscreen_refresh();
    check_engine_frame_limiter();
    check_engine_protocol();
    check_engine_competitive();
    check_engine_mouse();
}

// ---------------------------------------------------------------------------
// cgame: uo_cgame_mp_x86.dll (base 0x30000000)
// ---------------------------------------------------------------------------
constexpr uint32_t CG = 0x30000000;

void check_cgame_fov() {
    using namespace patches;
    // cvarTable entry: {vmCvar_t*, name, default, flags}; flags is the 4th dword
    const uint32_t entry = CG + CGAME_FOV_CVAR_FLAGS_RVA - 12;
    CHECK(g_cgame.cstr_eq(g_cgame.u32(entry + 4), "cg_fov"));
    CHECK(g_cgame.cstr_eq(g_cgame.u32(entry + 8), "80"));
    CHECK(g_cgame.u32(CG + CGAME_FOV_CVAR_FLAGS_RVA) == 0x201);  // ARCHIVE|CHEAT
    const uint32_t vmcvar = g_cgame.u32(entry);
    // CG_CalcFov: mov ecx,[cg_fov.value] ... fcomp [80.0f] ... jp +0x0a ; mov [fov],80.0f
    const uint32_t jp = CG + CGAME_FOV_MINCLAMP_JP_RVA;
    CHECK(g_cgame.bytes_eq(jp, {0x7a, 0x0a}));
    CHECK(g_cgame.bytes_eq(jp + 2, {0xc7, 0x44, 0x24, 0x04, 0x00, 0x00, 0xa0, 0x42}));  // 80.0f
    CHECK(g_cgame.bytes_eq(jp - 0x0b, {0xd8, 0x1d}) && g_cgame.f32(g_cgame.u32(jp - 0x09)) == 80.0f);
    CHECK(g_cgame.bytes_eq(jp - 0x19, {0x8b, 0x0d}) && g_cgame.u32(jp - 0x17) == vmcvar + 8);  // .value
    // the max clamp (160) right after is left in place
    CHECK(g_cgame.bytes_eq(jp + 0x10, {0xd8, 0x1d}) && g_cgame.f32(g_cgame.u32(jp + 0x12)) == 160.0f);
}

void check_cgame_widescreen() {
    using namespace patches;
    const uint32_t calcfov = CG + CGAME_CALCFOV_RVA;
    const uint32_t call1 = CG + CGAME_CALCFOV_CALL1_RVA, call2 = CG + CGAME_CALCFOV_CALL2_RVA;
    // CG_GetEffectiveFov has exactly the two callers we hook
    CHECK(is_call_to(g_cgame, call1, calcfov));
    CHECK(is_call_to(g_cgame, call2, calcfov));
    int callers = 0;
    for (const Image::Section& s : g_cgame.sections) {
        if (!s.code) continue;
        for (uint32_t va = CG + s.rva; va + 5 <= CG + s.rva + s.vsize; ++va)
            if (g_cgame.u8(va) == 0xe8 && g_cgame.call_target(va) == calcfov) ++callers;
    }
    CHECK(callers == 2);
    // it takes no stack args: both call sites are a bare call (CG_CalcFov pushes esi
    // as a callee-save, not an argument) and the function ends `add esp,8 ; ret`
    CHECK(g_cgame.bytes_eq(call1 - 1, {0x56}));
    CHECK(g_cgame.bytes_eq(calcfov, {0x83, 0xec, 0x08}));
    // vfov trig right after call1: fild [height] ; fild [width] ; ... fptan ... fpatan
    CHECK(g_cgame.bytes_eq(CG + CGAME_VFOV_HEIGHT_OP_RVA - 2, {0xdb, 0x05}));
    CHECK(g_cgame.bytes_eq(CG + CGAME_VFOV_WIDTH_OP_RVA - 2, {0xdb, 0x05}));
    CHECK(g_cgame.u32(CG + CGAME_VFOV_HEIGHT_OP_RVA) == CG + CGAME_REFDEF_HEIGHT_RVA);
    CHECK(g_cgame.u32(CG + CGAME_VFOV_WIDTH_OP_RVA) == CG + CGAME_REFDEF_WIDTH_RVA);
    CHECK(CGAME_VFOV_HEIGHT_OP_RVA == CGAME_CALCFOV_CALL1_RVA + 7);
    CHECK(CGAME_VFOV_WIDTH_OP_RVA == CGAME_CALCFOV_CALL1_RVA + 13);
    CHECK(g_cgame.bytes_eq(call1 + 0x22, {0xd9, 0xf2}));  // fptan
    CHECK(g_cgame.bytes_eq(call1 + 0x28, {0xd9, 0xf3}));  // fpatan
    // refdef: x, y, width (+8), height (+0xc) - height is 4 above width
    CHECK(CGAME_REFDEF_HEIGHT_RVA == CGAME_REFDEF_WIDTH_RVA + 4);
    // the skybox caller reads the same refdef fields
    CHECK(g_cgame.bytes_eq(call2 + 11, {0xdb, 0x05}) && g_cgame.u32(call2 + 13) == CG + CGAME_REFDEF_HEIGHT_RVA);
    // glConfig.vidWidth/Height: CG_Init scales them by 1/640 and 1/480
    CHECK(g_cgame.bytes_eq(0x3002e003, {0xdb, 0x05}) && g_cgame.u32(0x3002e005) == CG + CGAME_VIDWIDTH_RVA);
    CHECK(g_cgame.bytes_eq(0x3002e010, {0xd8, 0x0d}) && g_cgame.f32(g_cgame.u32(0x3002e012)) == 1.0f / 640.0f);
    CHECK(g_cgame.bytes_eq(0x3002e01c, {0xdb, 0x05}) && g_cgame.u32(0x3002e01e) == CG + CGAME_VIDHEIGHT_RVA);
    CHECK(g_cgame.bytes_eq(0x3002e022, {0xd8, 0x0d}) && g_cgame.f32(g_cgame.u32(0x3002e024)) == 1.0f / 480.0f);
}

struct Group {
    const char* name;
    bool enabled;
    void (*fn)();
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: site_check <binaries-dir> [<mss32_proxy.def>]\n");
        return 2;
    }
    const std::string dir = argv[1];
    g_def_path = argc > 2 ? argv[2] : "src/mss32_proxy.def";
    struct { Image* img; const char* file; } files[] = {
        {&g_exe, "CoDUOMP.exe"},
        {&g_cgame, "uo_cgame_mp_x86.dll"},
        {&g_game, "uo_game_mp_x86.dll"},
        {&g_ui, "uo_ui_mp_x86.dll"},
    };
    for (auto& f : files) {
        if (!f.img->load(dir + "/" + f.file)) {
            printf("cannot load %s/%s (run tests/fetch_binaries.py first)\n", dir.c_str(), f.file);
            return 2;
        }
    }

    using namespace patches;
    const Group groups[] = {
        {"identity", true, check_identity},
        {"proxy", true, check_proxy},
        {"engine", port::ENGINE, check_engine},
        {"cgame_fov", port::CGAME_FOV, check_cgame_fov},
        {"cgame_widescreen", port::CGAME_WIDESCREEN, check_cgame_widescreen},
        {"cgame_swing", port::CGAME_SWING, nullptr},
        {"cgame_stance", port::CGAME_STANCE, nullptr},
        {"cgame_lean", port::CGAME_LEAN, nullptr},
        {"cgame_draw2d", port::CGAME_DRAW2D, nullptr},
        {"game_viewheight", port::GAME_VIEWHEIGHT, nullptr},
        {"game_version", port::GAME_VERSION, nullptr},
        {"game_antilag", port::GAME_ANTILAG, nullptr},
    };

    for (const Group& g : groups) {
        g_run.group = g.name;
        const int fail_before = g_run.fail;
        const int total_before = g_run.pass + g_run.fail;
        if (g.fn) g.fn();
        const int n = g_run.pass + g_run.fail - total_before;
        if (g.enabled && n == 0) {
            ++g_run.fail;
            printf("FAIL [%s] group is enabled in core/game.h but has no checks\n", g.name);
        }
        printf("%-18s %-3s %4d checks  %s\n", g.name, g.enabled ? "ON" : "off", n,
               n == 0 ? "-" : (g_run.fail == fail_before ? "ok" : "FAILED"));
    }
    printf("\n%d passed, %d failed\n", g_run.pass, g_run.fail);
    return g_run.fail ? 1 : 0;
}
