// vibrance.cpp - game-only digital vibrance. See vibrance.h.
//
// NVIDIA: nvapi.dll exports one function, nvapi_QueryInterface(id), and every API is
// fetched by its interface id. The DVC ("digital vibrance control") calls are not in
// the public SDK header; they are what the Control Panel slider drives, and these are
// the ids vibrance tools use:
//   NvAPI_Initialize                       0x0150e828
//   NvAPI_GetAssociatedNvidiaDisplayHandle 0x35c29134   "\\.\DISPLAY2" -> handle
//   NvAPI_GetDVCInfoEx                     0x0e45002d   {ver, cur, min, max, default}
//   NvAPI_SetDVCLevelEx                    0x4a82c2b1
// The Ex pair works in Control Panel percent: 0..100, default 50 (read on an RTX 5070 Ti,
// driver 32.0.16.1692, 2026-10-18). The older non-Ex pair is 0..63 with 0 = 50 % and
// cannot go below the default, so it is not used.
//
// AMD: ADL. A 32-bit process on 64-bit Windows loads atiadlxy.dll (atiadlxx.dll is the
// 64-bit one there, and the 32-bit one on 32-bit Windows). Saturation is
// ADL_Display_Color_Get/Set(ADL_DISPLAY_COLOR_SATURATION) on an (adapter, display)
// pair, usually 0..200 with default 100.
//
// "Only for the game": the level goes on the monitor the game window is on while it has
// focus, and that monitor's own level (whatever the player set in the driver panel) goes
// back on alt-tab, on a monitor change and at exit. Same crash insurance as gamma_fix:
// while our level is live, the desktop's is written next to the exe, and the next launch
// puts it back if the process died without restoring.

#include "video/vibrance.h"

#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "video/window_patch.h"       // get_game_window()
#include "ui/gl_overlay.h"            // overlay_game_window() fallback
#include "features/settings_menu.h"   // menu_ini_writeback
#include "core/logger.h"

namespace patches {

VibranceConfig g_vibrance_config = {};

namespace {

// one display's control, in driver units
struct Range { int cur, min, max, def; };

// both driver APIs are __cdecl, FARPROC is __stdcall: go through void* on purpose
template <class T> T proc(HMODULE m, const char* name) {
    return (T)(void*)GetProcAddress(m, name);
}

// ---------------------------------------------------------------- NVIDIA
typedef void* (__cdecl* NvQueryInterface_t)(unsigned);
typedef int   (__cdecl* NvInitialize_t)();
typedef int   (__cdecl* NvDisplayHandle_t)(const char*, void**);
typedef int   (__cdecl* NvDvcEx_t)(void*, unsigned, void*);

struct NvDvcInfoEx { unsigned version; int cur, min, max, def; };
constexpr unsigned NV_DVC_INFO_EX_VER = sizeof(NvDvcInfoEx) | (1u << 16);

NvDisplayHandle_t nv_handle = nullptr;
NvDvcEx_t         nv_get    = nullptr;
NvDvcEx_t         nv_set    = nullptr;

bool nv_load() {
    HMODULE m = LoadLibraryA("nvapi.dll");
    if (!m) return false;
    NvQueryInterface_t qi = proc<NvQueryInterface_t>(m, "nvapi_QueryInterface");
    NvInitialize_t init = qi ? (NvInitialize_t)qi(0x0150e828) : nullptr;
    if (qi) {
        nv_handle = (NvDisplayHandle_t)qi(0x35c29134);
        nv_get    = (NvDvcEx_t)qi(0x0e45002d);
        nv_set    = (NvDvcEx_t)qi(0x4a82c2b1);
    }
    if (!init || !nv_handle || !nv_get || !nv_set || init() != 0) {
        FreeLibrary(m);
        return false;
    }
    return true;
}

bool nv_read(const char* dev, void** h, NvDvcInfoEx* i) {
    if (nv_handle(dev, h) != 0) return false;
    memset(i, 0, sizeof(*i));
    i->version = NV_DVC_INFO_EX_VER;
    return nv_get(*h, 0, i) == 0;
}

bool nv_range(const char* dev, Range* r) {
    void* h = nullptr;
    NvDvcInfoEx i;
    if (!nv_read(dev, &h, &i)) return false;
    *r = { i.cur, i.min, i.max, i.def };
    return true;
}

bool nv_write(const char* dev, int v) {
    void* h = nullptr;
    NvDvcInfoEx i;
    if (!nv_read(dev, &h, &i)) return false;   // the set call takes the filled struct back
    i.cur = v;
    return nv_set(h, 0, &i) == 0;
}

// ---------------------------------------------------------------- AMD
typedef void* (__stdcall* AdlMalloc_t)(int);
typedef int (__cdecl* AdlCreate_t)(AdlMalloc_t, int);
typedef int (__cdecl* AdlNumAdapters_t)(int*);
typedef int (__cdecl* AdlAdapterInfo_t)(void*, int);
typedef int (__cdecl* AdlDisplayInfo_t)(int, int*, void**, int);
typedef int (__cdecl* AdlColorGet_t)(int, int, int, int*, int*, int*, int*, int*);
typedef int (__cdecl* AdlColorSet_t)(int, int, int, int);

// adl_structures.h AdapterInfo, Windows layout
struct AdlAdapterInfo {
    int  iSize, iAdapterIndex;
    char strUDID[256];
    int  iBusNumber, iDeviceNumber, iFunctionNumber, iVendorID;
    char strAdapterName[256], strDisplayName[256];
    int  iPresent, iExist;
    char strDriverPath[256], strDriverPathExt[256], strPNPString[256];
    int  iOSDisplayIndex;
};

// adl_structures.h ADLDisplayInfo (ADLDisplayID inlined)
struct AdlDisplayInfo {
    int  iDisplayLogicalIndex, iDisplayPhysicalIndex;
    int  iDisplayLogicalAdapterIndex, iDisplayPhysicalAdapterIndex;
    int  iDisplayControllerIndex;
    char strDisplayName[256], strDisplayManufacturerName[256];
    int  iDisplayType, iDisplayOutputType, iDisplayConnector;
    int  iDisplayInfoMask, iDisplayInfoValue;
};

constexpr int ADL_OK                       = 0;
constexpr int ADL_DISPLAY_COLOR_SATURATION = 1 << 2;
constexpr int ADL_DISPLAY_CONNECTED_MAPPED = 0x1 | 0x2;   // DISPLAYCONNECTED | DISPLAYMAPPED

AdlNumAdapters_t adl_num      = nullptr;
AdlAdapterInfo_t adl_adapters = nullptr;
AdlDisplayInfo_t adl_displays = nullptr;
AdlColorGet_t    adl_get      = nullptr;
AdlColorSet_t    adl_set      = nullptr;

void* __stdcall adl_malloc(int n) { return malloc(n); }

bool amd_load() {
    HMODULE m = LoadLibraryA("atiadlxy.dll");
    if (!m) m = LoadLibraryA("atiadlxx.dll");
    if (!m) return false;
    AdlCreate_t create = proc<AdlCreate_t>(m, "ADL_Main_Control_Create");
    adl_num      = proc<AdlNumAdapters_t>(m, "ADL_Adapter_NumberOfAdapters_Get");
    adl_adapters = proc<AdlAdapterInfo_t>(m, "ADL_Adapter_AdapterInfo_Get");
    adl_displays = proc<AdlDisplayInfo_t>(m, "ADL_Display_DisplayInfo_Get");
    adl_get      = proc<AdlColorGet_t>(m, "ADL_Display_Color_Get");
    adl_set      = proc<AdlColorSet_t>(m, "ADL_Display_Color_Set");
    if (!create || !adl_num || !adl_adapters || !adl_displays || !adl_get || !adl_set ||
        create(adl_malloc, 1) != ADL_OK) {
        FreeLibrary(m);
        return false;
    }
    return true;
}

// ADL addresses a display as (adapter index, logical display index): find the pair
// behind the Windows device name of the game's monitor.
bool amd_find(const char* dev, int* adapter, int* display) {
    int n = 0;
    if (adl_num(&n) != ADL_OK || n <= 0 || n > 256) return false;
    AdlAdapterInfo* ai = (AdlAdapterInfo*)calloc(n, sizeof(AdlAdapterInfo));
    if (!ai) return false;
    bool found = false;
    if (adl_adapters(ai, n * (int)sizeof(AdlAdapterInfo)) == ADL_OK) {
        for (int a = 0; a < n && !found; ++a) {
            if (_stricmp(ai[a].strDisplayName, dev) != 0) continue;
            int nd = 0;
            AdlDisplayInfo* di = nullptr;
            if (adl_displays(ai[a].iAdapterIndex, &nd, (void**)&di, 0) == ADL_OK && di) {
                for (int d = 0; d < nd; ++d) {
                    if ((di[d].iDisplayInfoValue & ADL_DISPLAY_CONNECTED_MAPPED) !=
                        ADL_DISPLAY_CONNECTED_MAPPED) continue;
                    if (di[d].iDisplayLogicalAdapterIndex != ai[a].iAdapterIndex) continue;
                    *adapter = ai[a].iAdapterIndex;
                    *display = di[d].iDisplayLogicalIndex;
                    found = true;
                    break;
                }
            }
            free(di);   // allocated through adl_malloc
        }
    }
    free(ai);
    return found;
}

bool amd_range(const char* dev, Range* r) {
    int a, d, step;
    if (!amd_find(dev, &a, &d)) return false;
    return adl_get(a, d, ADL_DISPLAY_COLOR_SATURATION,
                   &r->cur, &r->def, &r->min, &r->max, &step) == ADL_OK;
}

bool amd_write(const char* dev, int v) {
    int a, d;
    return amd_find(dev, &a, &d) && adl_set(a, d, ADL_DISPLAY_COLOR_SATURATION, v) == ADL_OK;
}

// ---------------------------------------------------------------- common
volatile LONG g_backend   = 0;        // vibrance_backend()
volatile LONG g_want_probe = 0;       // the menu asked which driver is there

bool read_range(const char* dev, Range* r) {
    return g_backend == 1 ? nv_range(dev, r) : amd_range(dev, r);
}
bool write_level(const char* dev, int v) {
    return g_backend == 1 ? nv_write(dev, v) : amd_write(dev, v);
}

// percent (50 = driver default) -> driver units, through the driver's own default so
// both halves of the slider reach the ends of the range
int to_native(int pct, const Range& r) {
    if (pct <= 50) return r.min + ((r.def - r.min) * pct + 25) / 50;
    return r.def + ((r.max - r.def) * (pct - 50) + 25) / 50;
}

bool monitor_device_of_window(HWND w, char out[32]) {
    HMONITOR mon = MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXA mi;
    ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoA(mon, &mi)) return false;
    lstrcpynA(out, mi.szDevice, 32);
    return true;
}

SRWLOCK g_lock = SRWLOCK_INIT;
char g_dev[32] = "";   // the display we changed
int  g_desk    = 0;    // its own level before that, driver units
int  g_applied = -1;   // the percent on it now, -1 = nothing of ours

// Crash insurance, gamma_fix's coduoplus.gammadesk scheme: the file exists exactly
// while our level is live. Text: "<backend> <level> <device>".
void desk_file_path(char* out, size_t n) {
    char base[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, base, MAX_PATH);
    char* sl = strrchr(base, '\\');
    if (sl) sl[1] = 0;
    snprintf(out, n, "%scoduoplus.vibrancedesk", base);
}

void persist_desk_locked() {
    char path[MAX_PATH];
    desk_file_path(path, sizeof(path));
    if (FILE* f = fopen(path, "w")) {
        fprintf(f, "%ld %d %s\n", (long)g_backend, g_desk, g_dev);
        fclose(f);
    }
}

void delete_desk_file() {
    char path[MAX_PATH];
    desk_file_path(path, sizeof(path));
    DeleteFileA(path);
}

bool desk_file_exists() {
    char path[MAX_PATH];
    desk_file_path(path, sizeof(path));
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

// previous session died with our level on the screen: put the desktop's back
void recover_stale_desk() {
    char path[MAX_PATH];
    desk_file_path(path, sizeof(path));
    FILE* f = fopen(path, "r");
    if (!f) return;
    long be = 0;
    int lv = 0;
    char dev[32] = "";
    const bool ok = fscanf(f, "%ld %d %31s", &be, &lv, dev) == 3;
    fclose(f);
    if (ok && be == g_backend && write_level(dev, lv))
        logger::logf("vibrance: previous session died with the game level on - %s "
                     "restored to %d", dev, lv);
    DeleteFileA(path);   // one shot either way
}

void probe_backend() {
    LONG b = -1;
    if (nv_load()) b = 1;
    else if (amd_load()) b = 2;
    InterlockedExchange(&g_backend, b);
    logger::logf("vibrance: %s", b == 1 ? "NVIDIA (NVAPI DVC)"
                               : b == 2 ? "AMD (ADL saturation)"
                                        : "no NVIDIA/AMD driver API - unavailable");
    if (b > 0) recover_stale_desk();
}

void restore_locked() {
    if (g_applied < 0) return;
    if (!write_level(g_dev, g_desk))
        logger::logf("vibrance: could not restore %s to %d", g_dev, g_desk);
    delete_desk_file();
    g_applied = -1;
    g_dev[0] = 0;
}

bool apply_locked(const char* dev, int pct) {
    Range r;
    if (!read_range(dev, &r) || r.max <= r.min) return false;
    const bool first = g_applied < 0;
    if (first) {   // nothing of ours on it yet: this is the desktop's own level
        g_desk = r.cur;
        lstrcpynA(g_dev, dev, sizeof(g_dev));
        persist_desk_locked();
    }
    const int v = to_native(pct, r);
    if (v != r.cur && !write_level(dev, v)) {
        if (first) { delete_desk_file(); g_dev[0] = 0; }
        return false;
    }
    if (first)
        logger::logf("vibrance: %s %d%% (driver %d, desktop %d, range %d..%d default %d)",
                     dev, pct, v, g_desk, r.min, r.max, r.def);
    g_applied = pct;
    return true;
}

}  // namespace

int vibrance_backend() {
    if (g_backend == 0) InterlockedExchange(&g_want_probe, 1);
    return (int)g_backend;
}

void vibrance_set_level(int level, bool persist) {
    if (level > 100) level = 100;
    if (level < -1) level = -1;
    InterlockedExchange((volatile LONG*)&g_vibrance_config.level, level);
    if (persist) {
        char b[8];
        if (level < 0) snprintf(b, sizeof(b), "off");
        else snprintf(b, sizeof(b), "%d", level);
        menu_ini_writeback("digital_vibrance", b);
    }
}

// Watcher tick (~200 Hz, self-throttled to 4 Hz), the same focus rules as gamma_fix.
void vibrance_tick() {
    static DWORD last = 0;
    const DWORD now = GetTickCount();
    if (now - last < 250) return;
    last = now;

    const int level = g_vibrance_config.level;
    if (g_backend == 0) {
        // off, nothing left over by a dead session, menu not asking: no driver DLL loaded
        if (level < 0 && !g_want_probe && !desk_file_exists()) return;
        probe_backend();
    }
    if (g_backend < 0) return;

    HWND w = get_game_window();
    if (!w) w = overlay_game_window();
    char dev[32] = "";
    const bool want = level >= 0 && w && GetForegroundWindow() == w && !IsIconic(w) &&
                      monitor_device_of_window(w, dev);

    // a refused apply is not retried every tick, only once the level or monitor changes
    static int  failed_level = -1;
    static char failed_dev[32] = "";

    AcquireSRWLockExclusive(&g_lock);
    if (g_applied >= 0 && (!want || strcmp(dev, g_dev) != 0))
        restore_locked();                            // alt-tab, monitor change, turned off
    if (want && g_applied != level &&
        !(failed_level == level && strcmp(failed_dev, dev) == 0)) {
        if (apply_locked(dev, level)) {
            failed_level = -1;
        } else {
            failed_level = level;
            lstrcpynA(failed_dev, dev, sizeof(failed_dev));
            logger::logf("vibrance: driver refused %d%% on %s", level, dev);
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
}

void vibrance_shutdown() {
    if (g_backend <= 0) return;
    AcquireSRWLockExclusive(&g_lock);
    restore_locked();                                // never leave the desktop modified
    ReleaseSRWLockExclusive(&g_lock);
}

}  // namespace patches
