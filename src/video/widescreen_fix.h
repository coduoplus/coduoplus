#ifndef COD1RELOADED_WIDESCREEN_FIX_H
#define COD1RELOADED_WIDESCREEN_FIX_H

#include <windows.h>

namespace patches {

enum class AspectMode {
    Auto,
    R_4_3,
    R_16_9,
    R_16_10,
    R_21_9,
    Custom,
};

struct WidescreenConfig {
    bool       horplus_fov_enable;
    AspectMode aspect_mode;
    float      custom_ratio;
    bool       horplus_hook_caller2;  // also hook CG_DrawSkyBoxPortal so sky matches scene

    bool   force_resolution;  // r_mode -1 + r_customwidth/height via autoexec
    int    width;
    int    height;
    int    refresh_hz;  // 0 = engine auto-detect

    bool   stretch_enable;   // "stretched" mode: render a 4:3 vfov into the widescreen
                             // buffer -> horizontal stretch (wider models, classic comp look)
};

extern WidescreenConfig g_widescreen_config;

// uo_cgame_mp_x86.dll 1.51 RVAs (base 0x30000000). UO's CG_CalcFov is the CoD1 code
// instruction for instruction, at new addresses (CoD1 RVAs in brackets):
//   3003ffc0  CG_GetEffectiveFov: no args, hfov in ST(0)            [0x344c0]
//   300402b0  push esi ; call CG_GetEffectiveFov                    [0x345e1]
//   300402b6  fild [refdef.height] ; fild [refdef.width] ; fld st(2) ;
//             fmul k ; fptan ; fstp st0 ; fdivp ; fpatan            [0x345e6..]
//   30042085  call CG_GetEffectiveFov ; fst [refdef.fov_x] ...      [0x350a5]
// CALCFOV = CG_GetEffectiveFov, two callers.
constexpr uintptr_t CGAME_CALCFOV_RVA       = 0x0003ffc0;
constexpr uintptr_t CGAME_CALCFOV_CALL1_RVA = 0x000402b1; // CG_CalcViewValues tail
constexpr uintptr_t CGAME_CALCFOV_CALL2_RVA = 0x00042085; // CG_DrawSkyBoxPortal

// Stretch: the vfov trig in CG_CalcViewValues reads refdef.width/height via two `fild`
// instructions; their disp32 operands sit at these RVAs. Redirect them to mod-owned ints
// (g_stretch_w/h) so we can feed a forced 4:3 ratio (= horizontal stretch) live.
constexpr uintptr_t CGAME_VFOV_HEIGHT_OP_RVA = 0x000402b8; // operand of `fild [refdef.height]`
constexpr uintptr_t CGAME_VFOV_WIDTH_OP_RVA  = 0x000402be; // operand of `fild [refdef.width]`
// Original operand values of the two fild sites above. In uo_cgame 1.51 the operand
// at RVA 0x402b8 is 84 7a 48 30 = 0x30487a84 (the other site holds ...7a80).
// (Upstream history, CoD1: its first constants were 0x140000 too high, which made the
// sanity check fail on every load -> "stretch operands redirected (PARTIAL)".)
//
// WHICH IS WHICH: traced through the FPU in CG_CalcFov (CoD1 @0x345e0, UO @0x402b0):
//   fild [d33c] ; fild [d338] ; fld st(2) ; fmul k ; fptan ; fstp ; fdivp st(1) ; fpatan
// resolves to  fov_y = atan( [d33c] * tan(fov_x/2) / [d338] ), and the Q3 formula is
// fov_y = atan(height * tan(fov_x/2) / width). So **d33c is HEIGHT and d338 is WIDTH** -
// the opposite of the old naming. With the names swapped, feeding 4 and 3 for stretched
// produced a 3:4 (portrait) aspect, which is why stretched looked wrong. In the
// non-stretched path the two swaps cancelled out, so classic/widescreen still looked
// right - which is exactly why this stayed hidden.
// (CoD1's d33c/d338 = UO's 7a84/7a80: refdef.width at +8, height at +0xc, as in Q3.)
constexpr uintptr_t CGAME_REFDEF_HEIGHT_RVA  = 0x00487a84; // read by the fild @0x402b6
constexpr uintptr_t CGAME_REFDEF_WIDTH_RVA   = 0x00487a80; // read by the fild @0x402bc
// glConfig dims. These were swapped too: with the game running 1440x1080 the mod logged
// "seed dims 1080x1440", i.e. 0x1d5a28 holds the WIDTH and 0x1d5a2c the HEIGHT (normal
// struct order, width first). Combined with the swapped refdef names above, the error
// cancelled out in the non-stretched path - so nothing looked broken until stretched
// actually started working. Both are now named correctly; do not "re-align" them.
// UO: CG_Init calls trap_GetGlconfig(&cgs.glconfig = 0x30447a04) (syscall 0x4e), then
//   3002e003  fild [0x30447a88] ; fmul [1/640] ; fstp [cgs.screenXScale]
//   3002e01c  fild [0x30447a8c] ; fmul [1/480] ; fstp [cgs.screenYScale]
// so vidWidth/vidHeight are glconfig+0x84/+0x88, width first - same as CoD1.
constexpr uintptr_t CGAME_VIDWIDTH_RVA       = 0x00447a88; // glConfig.vidWidth  (=1440)
constexpr uintptr_t CGAME_VIDHEIGHT_RVA      = 0x00447a8c; // glConfig.vidHeight (=1080)

float widescreen_get_aspect_ratio();
float widescreen_horplus_hfov(float cg_fov_43, float actual_aspect);

// Aspect ratio as a human string: "auto" / "4:3" / "16:9" / "16:10" / "21:9",
// or a plain number like "1.6" (= a custom ratio). Shared by the .ini loader and the
// in-game menu so both speak the same values.
void widescreen_set_aspect(const char* s);        // parse string -> aspect_mode (+ custom_ratio)
void widescreen_get_aspect(char* out, size_t n);  // aspect_mode/custom_ratio -> string

void widescreen_fix_apply();  // DllMain: autoexec.cfg + aspect detection
bool widescreen_apply_to_cgame(HMODULE cgame_module);  // install hooks (Hor+ and stretch), idempotent
void widescreen_update_stretch();  // per-tick: feed g_stretch_w/h (4:3 when stretched, else real dims)

}  // namespace patches

#endif
