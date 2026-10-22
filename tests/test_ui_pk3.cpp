// test_ui_pk3.cpp - the in-DLL UI pk3 generator (features/ui_pk3.cpp)
//
//     test_ui_pk3                          portable checks only (no game needed)
//     test_ui_pk3 <game-dir> <python-pk3>  + against the install's own uo/pakuo03.pk3
//
// Portable: the stored-zip writer round-trips through the reader and is deterministic,
// a menu that is not the stock 1.51 file is refused, and the dedicated-server
// command-line parser (core/game.cpp) that keeps the pk3 and the update dialog off
// servers.
// With the game: the C++ generator's main.menu is byte for byte the one
// ui_pk3/build_pk3.py makes (read back from the Python pk3, which zlib DEFLATEd - so
// the puff inflater is exercised on a second encoder, after pakuo03.pk3's own).

#include "features/ui_pk3.h"
#include "core/game.h"

#include <cstdio>
#include <string>

using namespace patches;

static int g_failed = 0;
static void check(bool ok, const char* what) { printf("  [%s] %s\n", ok ? "OK" : "FAIL", what); if (!ok) ++g_failed; }

static bool read_file(const std::string& path, std::string& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[1 << 16];
    size_t n;
    out.clear();
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return true;
}

static size_t count_of(const std::string& s, const std::string& what) {
    size_t n = 0;
    for (size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + what.size())) ++n;
    return n;
}

int main(int argc, char** argv) {
    printf("portable:\n");
    {
        const std::string data = "hello\r\nworld\r\n";
        const std::string z = ui_pk3::make_pk3("ui_mp/main.menu", data);
        std::string back, err;
        check(ui_pk3::zip_read(z, "ui_mp/main.menu", back, err) && back == data, "stored pk3 reads back");
        check(ui_pk3::make_pk3("ui_mp/main.menu", data) == z, "pk3 bytes are deterministic");
        check(!ui_pk3::zip_read(z, "ui_mp/other.menu", back, err), "missing member is reported");
        std::string bad = z;
        bad[30 + 15 + 2] ^= 1;   // a byte of the member's data
        check(!ui_pk3::zip_read(bad, "ui_mp/main.menu", back, err) && err == "CRC mismatch", "corrupt member: CRC mismatch");
        check(!ui_pk3::zip_read("not a zip at all, not even close", "x", back, err), "garbage is not a zip");
        std::string out;
        check(!ui_pk3::patch_main_menu("menuDef { }", out, err), "non-stock main.menu refused");
    }
    {
        check(cmdline_sets_dedicated("CoDUOMP.exe +set dedicated 2 +exec server.cfg"), "dedicated 2");
        check(cmdline_sets_dedicated("\"C:\\Games\\CoD\\CoDUOMP.exe\" +set DEDICATED 1"), "DEDICATED 1, quoted exe");
        check(cmdline_sets_dedicated("CoDUOMP.exe +set dedicated \"2\" +map_rotate"), "dedicated \"2\"");
        check(!cmdline_sets_dedicated("CoDUOMP.exe +set dedicated 0"), "dedicated 0 is a client");
        check(!cmdline_sets_dedicated("CoDUOMP.exe +connect 1.2.3.4"), "no dedicated");
        check(!cmdline_sets_dedicated("CoDUOMP.exe +set name dedicated_fan"), "a name containing it");
        check(!cmdline_sets_dedicated("C:\\dedicated 2\\CoDUOMP.exe"), "nor a folder called \"dedicated 2\"");
        check(!cmdline_sets_dedicated("CoDUOMP.exe +set dedicated 12"), "dedicated 12 is not 1/2");
        check(!cmdline_sets_dedicated(nullptr), "no command line");
    }

    if (argc >= 3) {
        printf("game (%s):\n", argv[1]);
        std::string pak, err, pk3, menu, py_pk3, py_menu, stock;
        check(read_file(std::string(argv[1]) + "/uo/pakuo03.pk3", pak), "read uo/pakuo03.pk3");
        check(ui_pk3::zip_read(pak, "ui_mp/main.menu", stock, err), ("stock main.menu inflated " + err).c_str());
        check(ui_pk3::build(pak, pk3, err), ("generated the pk3 " + err).c_str());
        check(ui_pk3::zip_read(pk3, "ui_mp/main.menu", menu, err), "generated pk3 reads back");
        check(count_of(menu, "CoDUO+ Settings") == 1, "one CoDUO+ Settings item");
        check(count_of(menu, "itemDef") == count_of(stock, "itemDef") + 1, "exactly one itemDef added");
        check(menu.find("CoDUO+ Settings") < menu.find("@MENU_AUTO_UPDATE"), "inserted before AUTO-UPDATE");
        check(read_file(argv[2], py_pk3), "read build_pk3.py's pk3");
        check(ui_pk3::zip_read(py_pk3, "ui_mp/main.menu", py_menu, err), "inflated build_pk3.py's main.menu");
        check(!menu.empty() && menu == py_menu, "C++ main.menu == build_pk3.py main.menu, byte for byte");
        // the file path the DLL takes: seek-reads the pak, writes once, then only reads
        const std::string dst = std::string(argv[2]) + ".cpp-test.pk3";
        remove(dst.c_str());
        std::string msg, on_disk;
        check(ui_pk3::generate_file((std::string(argv[1]) + "/uo/pakuo03.pk3").c_str(), dst.c_str(), msg) ==
              ui_pk3::WRITTEN, "generate_file writes the pk3");
        check(read_file(dst, on_disk) && on_disk == pk3, "file on disk == generated pk3");
        check(ui_pk3::generate_file((std::string(argv[1]) + "/uo/pakuo03.pk3").c_str(), dst.c_str(), msg) ==
              ui_pk3::UP_TO_DATE, "second launch: up to date, not rewritten");
        check(ui_pk3::generate_file("no/such/pakuo03.pk3", dst.c_str(), msg) == ui_pk3::FAILED, "missing pak reported");
        remove(dst.c_str());
        std::string again;
        check(!ui_pk3::patch_main_menu(menu, again, err), "an already patched menu is refused");

        // Every script command in our item's action { } must be one UO's UI module
        // knows: it skips an unknown one SILENTLY (upstream's `set cvar 1;` made the
        // button do nothing). The command names are NUL-terminated strings in
        // uo_ui_mp_x86.dll's command table (pakuo07.pk3).
        std::string pak7, uidll;
        check(read_file(std::string(argv[1]) + "/uo/pakuo07.pk3", pak7) &&
              ui_pk3::zip_read(pak7, "uo_ui_mp_x86.dll", uidll, err), "read uo_ui_mp_x86.dll");
        const size_t item = menu.find("name coduoplus_settings_btn");
        const size_t act = menu.find("action", item);
        const size_t open = menu.find('{', act), close = menu.find('}', open);
        int commands = 0;
        bool all_known = item != std::string::npos && open != std::string::npos && close != std::string::npos;
        for (size_t p = open + 1; all_known && p < close;) {
            const size_t semi = menu.find(';', p);
            if (semi == std::string::npos || semi > close) break;
            std::string stmt = menu.substr(p, semi - p);
            const size_t b = stmt.find_first_not_of(" \t\r\n");
            if (b != std::string::npos) {
                const std::string cmd = stmt.substr(b, stmt.find_first_of(" \t", b) - b);
                const bool known = uidll.find(std::string(1, '\0') + cmd + std::string(1, '\0')) != std::string::npos;
                if (!known) printf("    unknown UI script command: \"%s\"\n", cmd.c_str());
                all_known = all_known && known;
                ++commands;
            }
            p = semi + 1;
        }
        check(all_known && commands == 4, "every command in the item is in UO's UI command table");
        check(menu.find("setcvar cod1x_open_modern \"1\";") != std::string::npos,
              "the item sets cod1x_open_modern with setcvar");
        printf("  generated pk3: %u bytes, main.menu %u -> %u bytes\n",
               (unsigned)pk3.size(), (unsigned)stock.size(), (unsigned)menu.size());
    }

    printf(g_failed ? "test_ui_pk3: %d FAILED\n" : "test_ui_pk3: all passed\n", g_failed);
    return g_failed ? 1 : 0;
}
