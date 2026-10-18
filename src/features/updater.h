#ifndef COD1RELOADED_UPDATER_H
#define COD1RELOADED_UPDATER_H

#include <windows.h>

namespace patches {

// build_number_from() in protocol_patch.cpp derives cod1x_build from this string
// (1.6.5 -> 10605), so this is the ONLY place a release version is written.
// CoD:UO port: CoDUO+ has its own release line. It starts at 0.x so its build
// numbers (0.1.0 -> 100) can never be mistaken for a CoD1 COD1.6X build (1.6.x
// -> 106xx) by anything that compares them.
constexpr const char* COD1RELOADED_VERSION = "0.2.0";
// The upstream COD1.6X releases this port follows: based on 1.6.6 (cod1plus/client
// 6f8fec0), with every non-PunkBuster change of release/1.6.9 (3787d88) ported on top.
constexpr const char* COD1RELOADED_UPSTREAM_VERSION = "1.6.6+1.6.9";

struct UpdaterConfig {
    bool        enable;
    char        manifest_url[512];  // empty = disabled
    bool        auto_download;
    bool        show_dialog;
};

extern UpdaterConfig g_updater_config;

void updater_start();          // non-blocking, after logger::init

// rename .dll.new -> .dll; call early before anything uses the DLL
void updater_apply_pending();

}  // namespace patches

#endif
