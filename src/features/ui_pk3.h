#ifndef COD1RELOADED_UI_PK3_H
#define COD1RELOADED_UI_PK3_H

#include <string>

// uo/zz_coduoplus_ui.pk3: UO's own main menu plus the "CoDUO+ Settings" entry,
// generated ON THE PLAYER'S MACHINE from the player's own uo/pakuo03.pk3.
//
// It used to be built by ui_pk3/build_pk3.py and shipped next to mss32.dll. That
// meant publishing a modified Activision file (ui_mp/main.menu) in every release,
// and one more file a player had to copy to the right folder. Generating it here
// does neither: the DLL is all a player installs, and the menu is always made from
// the menu that player's game already has. build_pk3.py stays as the reference
// implementation; test_ui_pk3 proves both produce the same main.menu, byte for byte.

namespace patches {

// Called from DllMain, before the engine scans its pk3s (FS_InitFilesystem), so the
// first scan already sees the file. File I/O only - safe under the loader lock.
// Skipped on a dedicated server (no menus there). Writes the pk3 only when it is
// missing or differs from what would be generated, so a normal launch only reads.
void ui_pk3_prepare();

namespace ui_pk3 {

constexpr const char* PK3_NAME   = "zz_coduoplus_ui.pk3";
constexpr const char* STOCK_PAK  = "pakuo03.pk3";
// ui_mp/main.menu as shipped in CoD:UO 1.51's uo/pakuo03.pk3 (sha256 609111fd...,
// the value build_pk3.py checks). Size + CRC-32 from the pak's central directory:
// the C++ side checks those instead, so it needs no hash library under the loader lock.
constexpr unsigned STOCK_MENU_SIZE  = 12560;
constexpr unsigned STOCK_MENU_CRC32 = 0x08743fceu;

// Pure functions, no logging, no game: used by ui_pk3_prepare() and by the test.
// Read one member of a zip (stored or deflated) into `out`, its CRC checked.
bool zip_read(const std::string& zip, const char* name, std::string& out, std::string& err);
// Stock main.menu -> main.menu with the CoDUO+ Settings item, or false + err.
bool patch_main_menu(const std::string& stock, std::string& out, std::string& err);
// A one-member stored zip, deterministic (fixed timestamp): same input, same bytes.
std::string make_pk3(const char* name, const std::string& data);
// The whole pk3 for a given pakuo03.pk3 image.
bool build(const std::string& pakuo03, std::string& pk3, std::string& err);
// What ui_pk3_prepare() does, given the paths: reads only what it needs of the pak
// (146 MB), writes dst only if it differs, atomically. msg: the reason, or the size.
enum Result { FAILED, UP_TO_DATE, WRITTEN };
Result generate_file(const char* pakuo03_path, const char* dst_path, std::string& msg);

}  // namespace ui_pk3
}  // namespace patches

#endif
