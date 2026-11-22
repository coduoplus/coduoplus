#ifndef COD1RELOADED_FOV_SENS_H
#define COD1RELOADED_FOV_SENS_H

// m_fovscale: the same mouse feel at every FOV (math: input/fov_sens_math.h).
//
// Where the engine gets its FOV scale. Every frame CG_DrawActiveFrame sends the cgame's
// zoom sensitivity to the engine with syscall 0x55 (trap_SetUserCmdValue), which stores
// it as cl.cgameSensitivity (CoDUOMP.exe 0x04963778). CL_MouseMove (0x40c330) multiplies
// every mouse delta by it, before m_yaw / m_pitch:
//
//     angle += m_yaw * mx * cgameSensitivity * (sensitivity + accel * speed)
//
// The cgame computes that value at the end of CG_CalcFov (uo_cgame_mp_x86.dll 1.51):
//
//   30040343  fstp  dword [refdef.fov_x]      ; ST0 = fov_y, ST1 = fov_x
//   3004034c  fstp  dword [refdef.fov_y]      ; ST0 = fov_x
//   30040352  fdiv  dword [cg_fov.value]      ; d8 35 c8 f7 52 30
//   30040358  fstp  dword [cg.zoomSensitivity]; d9 1d f0 9a 48 30
//   3004035e  ret
//
// so stock is fov_x / cg_fov, where fov_x is AFTER our Hor+ hook widened it: with Hor+
// at 16:9 the stock hip-fire scale is about 1.2, not 1.
//
// The hook replaces the fdiv with `call fovsens_wrapper ; nop`. With m_fovscale 0 the
// wrapper runs that same fdiv, so the result is bit for bit stock. With m_fovscale 1 it
// returns tan(fov_x/2) / tan(ref_x/2) instead, ref_x being m_fovscale_ref through the
// same Hor+ transform the game applied to fov_x. cg_shock_mouse_sensitivityscale is
// applied after, by the cgame, as before.
//
// Installed only once a player turns m_fovscale on (fov_sens_tick), then kept on that
// cgame instance (off = stock fdiv). A map change reloads the cgame with vanilla bytes;
// the tick puts the hook back.

#include <stdint.h>

namespace patches {

// uo_cgame_mp_x86.dll 1.51 RVAs (base 0x30000000), checked by tests/site_check.cpp.
constexpr uintptr_t CGAME_FOVSENS_SITE_RVA   = 0x00040352;  // fdiv [cg_fov.value], 6 bytes
constexpr uintptr_t CGAME_CG_FOV_VALUE_RVA   = 0x0052f7c8;  // cg_fov vmCvar_t .value (float)
constexpr uintptr_t CGAME_ZOOMSENS_RVA       = 0x00489af0;  // cg.zoomSensitivity (float)
constexpr uintptr_t CGAME_ZOOMSENS_READ_RVA  = 0x000426be;  // CG_DrawActiveFrame: mov eax,[zoomSens]

void fov_sens_tick();   // watcher thread: register cvars, follow m_fovscale, (re)install

}  // namespace patches

#endif
