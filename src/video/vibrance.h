#ifndef COD1RELOADED_VIBRANCE_H
#define COD1RELOADED_VIBRANCE_H

// Game-only digital vibrance (NVIDIA NVAPI / AMD ADL saturation). The level is the
// driver's colour-saturation control on the monitor the game is on, set while the
// game window has focus and given back to the desktop's own level on alt-tab, on a
// monitor change and at exit - so nothing outside the game ever sees it.
//
// Level is a percentage in the NVIDIA Control Panel's terms: 50 = the driver default
// (neutral), 0 = greyscale, 100 = the driver maximum. AMD's saturation range is mapped
// onto it through its own default (100 -> 50 %).
//
// Driver calls only ever run on the watcher thread (vibrance_tick) and in
// vibrance_shutdown - never from DllMain ATTACH, where loading nvapi.dll would run
// its initialisation under the loader lock.

namespace patches {

struct VibranceConfig {
    int level = -1;   // ini: digital_vibrance = off | 0..100 (-1 = off, driver untouched)
};

extern VibranceConfig g_vibrance_config;

// Which driver API carries the control on this machine: 0 not probed yet,
// 1 NVIDIA, 2 AMD, -1 neither (Intel, hybrid laptop panel on the iGPU, old driver).
int  vibrance_backend();
// Menu: change the level live (-1 = off). persist = also write coduoplus.ini.
void vibrance_set_level(int level, bool persist);

void vibrance_tick();       // watcher thread: apply on focus, restore on focus loss
void vibrance_shutdown();   // WM_DESTROY / DllMain DETACH: give the desktop its level back

}  // namespace patches

#endif
