// uo/zz_coduoplus_ui.pk3, generated from the player's own uo/pakuo03.pk3 - see ui_pk3.h.
// The C++ twin of ui_pk3/build_pk3.py: same stock-file check, same item, same place.

#include "features/ui_pk3.h"
#include "core/game.h"
#include "core/logger.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <stdint.h>

extern "C" {
#include "puff.h"
}

namespace patches {
namespace ui_pk3 {

namespace {

// Same item as build_pk3.py's SETTINGS_ITEM (upstream's "1.6X SETTINGS", relabelled),
// tabs and CRLF like UO's file. test_ui_pk3 compares the two generators' output.
const char SETTINGS_ITEM[] =
    "\t\titemDef\r\n"
    "\t\t{\r\n"
    "\t\t\tname coduoplus_settings_btn\r\n"
    "\t\t\ttext \"CoDUO+ SETTINGS\"\r\n"
    "\t\t\ttype ITEM_TYPE_BUTTON\r\n"
    "\t\t\tstyle WINDOW_STYLE_EMPTY\r\n"
    "\t\t\ttextstyle ITEM_TEXTSTYLE_SHADOWED\r\n"
    "\t\t\trect 385 400 250 15\r\n"
    "\t\t\ttextalign 0\r\n"
    "\t\t\ttextscale .4\r\n"
    "\t\t\ttextalignx 0\r\n"
    "\t\t\ttextaligny 14\r\n"
    "\t\t\ttextfont UI_FONT_NORMAL\r\n"
    "\t\t\tforecolor .98 .82 .28 .9\r\n"
    "\t\t\tvisible 1\r\n"
    "\t\t\tmouseEnter\r\n"
    "\t\t\t{\r\n"
    "\t\t\t\tplay \"mouse_over\";\r\n"
    "\t\t\t}\r\n"
    "\t\t\taction\r\n"
    "\t\t\t{\r\n"
    "\t\t\t\tclose mods_menu;\r\n"
    "\t\t\t\tclose options_multi;\r\n"
    "\t\t\t\tplay \"mouse_click\";\r\n"
    "\t\t\t\tset cod1x_open_modern 1;\r\n"
    "\t\t\t}\r\n"
    "\t\t}\r\n"
    "\r\n";

uint32_t crc32_of(const void* data, size_t len) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c >> 1) ^ ((c & 1) ? 0xEDB88320u : 0);
            table[i] = c;
        }
        ready = true;
    }
    const uint8_t* p = (const uint8_t*)data;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) crc = (crc >> 8) ^ table[(crc ^ p[i]) & 0xFFu];
    return crc ^ 0xFFFFFFFFu;
}

uint32_t rd16(const char* p) { return (uint8_t)p[0] | ((uint8_t)p[1] << 8); }
uint32_t rd32(const char* p) { return rd16(p) | (rd16(p + 2) << 16); }
void put16(std::string& s, uint32_t v) { s += (char)(v & 0xff); s += (char)((v >> 8) & 0xff); }
void put32(std::string& s, uint32_t v) { put16(s, v & 0xffff); put16(s, v >> 16); }

size_t count_of(const std::string& s, const char* what) {
    size_t n = 0;
    for (size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + strlen(what))) ++n;
    return n;
}

// A zip, read through `rd(offset, length, out)`: a string in the test, a 146 MB pak on
// disk in the game - only the end record, the central directory and one member are read.
template <class Reader>
bool zip_read_with(Reader rd, uint32_t zip_size, const char* name, std::string& out, std::string& err) {
    // end of central directory: 22 bytes + a comment of up to 64 KB, at the very end
    const uint32_t tail_len = zip_size < 22 + 65535 ? zip_size : 22 + 65535;
    std::string tail;
    if (zip_size < 22 || !rd(zip_size - tail_len, tail_len, tail)) { err = "not a zip (too short)"; return false; }
    size_t eocd = std::string::npos;
    for (size_t i = tail.size() - 22 + 1; i-- > 0;)
        if (rd32(&tail[i]) == 0x06054b50u) { eocd = i; break; }
    if (eocd == std::string::npos) { err = "not a zip (no end record)"; return false; }
    const uint32_t entries = rd16(&tail[eocd + 10]);
    const uint32_t cd_size = rd32(&tail[eocd + 12]);
    const uint32_t cd_off  = rd32(&tail[eocd + 16]);
    std::string cd;
    if ((uint64_t)cd_off + cd_size > zip_size || !rd(cd_off, cd_size, cd)) { err = "bad central directory"; return false; }

    const size_t name_len = strlen(name);
    size_t p = 0;
    for (uint32_t e = 0; e < entries; ++e) {
        if (p + 46 > cd.size() || rd32(&cd[p]) != 0x02014b50u) { err = "bad central directory entry"; return false; }
        const uint32_t method = rd16(&cd[p + 10]);
        const uint32_t crc    = rd32(&cd[p + 16]);
        const uint32_t csize  = rd32(&cd[p + 20]);
        const uint32_t usize  = rd32(&cd[p + 24]);
        const uint32_t nlen   = rd16(&cd[p + 28]);
        const uint32_t xlen   = rd16(&cd[p + 30]);
        const uint32_t clen   = rd16(&cd[p + 32]);
        const uint32_t lofs   = rd32(&cd[p + 42]);
        if (p + 46 + nlen > cd.size()) { err = "bad central directory entry"; return false; }
        if (nlen == name_len && memcmp(&cd[p + 46], name, nlen) == 0) {
            std::string lh;
            if (!rd(lofs, 30, lh) || rd32(&lh[0]) != 0x04034b50u) { err = "bad local header"; return false; }
            const uint32_t data_off = lofs + 30 + rd16(&lh[26]) + rd16(&lh[28]);
            std::string comp;
            if ((uint64_t)data_off + csize > zip_size || !rd(data_off, csize, comp)) { err = "member past end"; return false; }
            if (method == 0) {
                out = comp;
            } else if (method == 8) {
                out.assign(usize, '\0');
                unsigned long dlen = usize, slen = csize;
                const int r = puff(usize ? (unsigned char*)&out[0] : (unsigned char*)NIL, &dlen,
                                   (const unsigned char*)comp.data(), &slen);
                if (r != 0 || dlen != usize) {
                    char b[64]; snprintf(b, sizeof(b), "inflate failed (puff %d)", r);
                    err = b; return false;
                }
            } else {
                char b[64]; snprintf(b, sizeof(b), "unsupported compression method %u", method);
                err = b; return false;
            }
            if (out.size() != usize || crc32_of(out.data(), out.size()) != crc) { err = "CRC mismatch"; return false; }
            return true;
        }
        p += 46 + nlen + xlen + clen;
    }
    err = std::string(name) + " not found";
    return false;
}

}  // namespace

bool zip_read(const std::string& zip, const char* name, std::string& out, std::string& err) {
    auto rd = [&](uint32_t off, uint32_t n, std::string& o) {
        if ((uint64_t)off + n > zip.size()) return false;
        o.assign(zip, off, n);
        return true;
    };
    return zip_read_with(rd, (uint32_t)zip.size(), name, out, err);
}

bool patch_main_menu(const std::string& stock, std::string& out, std::string& err) {
    if (stock.size() != STOCK_MENU_SIZE || crc32_of(stock.data(), stock.size()) != STOCK_MENU_CRC32) {
        err = "ui_mp/main.menu is not the stock 1.51 file";
        return false;
    }
    // right before the itemDef whose text is the auto-update entry (build_pk3.py's regex)
    const size_t name_at = stock.find("\r\n\t\t{\r\n\t\t\tname play\r\n\t\t\ttext \"@MENU_AUTO_UPDATE\"");
    const size_t at = name_at == std::string::npos ? name_at : stock.rfind("\t\titemDef", name_at);
    if (at == std::string::npos || stock.find_first_not_of(" \t\r\n", at + 9) != name_at + 4) {
        err = "could not find the AUTO-UPDATE item in UO main.menu";
        return false;
    }
    if (stock.find("rect 385 400") != std::string::npos) {
        err = "UO main.menu already has an item at y=400 - layout changed?";
        return false;
    }
    out = stock.substr(0, at) + SETTINGS_ITEM + stock.substr(at);
    // cheap structural checks: one more itemDef, braces still balanced, still CRLF
    if (count_of(out, "itemDef") != count_of(stock, "itemDef") + 1 ||
        count_of(out, "{") != count_of(out, "}") || count_of(out, "\n") != count_of(out, "\r\n")) {
        err = "patched main.menu failed its structural checks";
        return false;
    }
    return true;
}

std::string make_pk3(const char* name, const std::string& data) {
    const uint32_t crc = crc32_of(data.data(), data.size());
    const uint32_t nlen = (uint32_t)strlen(name);
    const uint32_t DOS_TIME = 0, DOS_DATE = 0x21;   // 1980-01-01 00:00: same bytes every time
    std::string z;
    put32(z, 0x04034b50u);                       // local file header
    put16(z, 20); put16(z, 0); put16(z, 0);      //   version, flags, method: stored
    put16(z, DOS_TIME); put16(z, DOS_DATE);
    put32(z, crc); put32(z, (uint32_t)data.size()); put32(z, (uint32_t)data.size());
    put16(z, nlen); put16(z, 0);
    z += name;
    z += data;
    const uint32_t cd_off = (uint32_t)z.size();
    put32(z, 0x02014b50u);                       // central directory
    put16(z, 20); put16(z, 20); put16(z, 0); put16(z, 0);
    put16(z, DOS_TIME); put16(z, DOS_DATE);
    put32(z, crc); put32(z, (uint32_t)data.size()); put32(z, (uint32_t)data.size());
    put16(z, nlen); put16(z, 0); put16(z, 0); put16(z, 0); put16(z, 0);
    put32(z, 0); put32(z, 0);                    //   attributes, local header at 0
    z += name;
    const uint32_t cd_size = (uint32_t)z.size() - cd_off;
    put32(z, 0x06054b50u);                       // end of central directory
    put16(z, 0); put16(z, 0); put16(z, 1); put16(z, 1);
    put32(z, cd_size); put32(z, cd_off); put16(z, 0);
    return z;
}

bool build(const std::string& pakuo03, std::string& pk3, std::string& err) {
    std::string stock, menu;
    if (!zip_read(pakuo03, "ui_mp/main.menu", stock, err)) return false;
    if (!patch_main_menu(stock, menu, err)) return false;
    pk3 = make_pk3("ui_mp/main.menu", menu);
    return true;
}

}  // namespace ui_pk3

namespace {

bool read_whole(const char* path, std::string& out) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    char buf[8192];
    size_t n;
    out.clear();
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0 && out.size() < (1u << 20)) out.append(buf, n);
    fclose(f);
    return true;
}

}  // namespace

namespace ui_pk3 {

Result generate_file(const char* pak, const char* dst, std::string& msg) {
    FILE* f = fopen(pak, "rb");
    if (!f) { msg = "not found"; return FAILED; }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    auto rd = [&](uint32_t off, uint32_t n, std::string& o) {
        o.assign(n, '\0');
        return fseek(f, (long)off, SEEK_SET) == 0 && (n == 0 || fread(&o[0], 1, n, f) == n);
    };
    std::string stock, menu;
    const bool ok = size > 0 && zip_read_with(rd, (uint32_t)size, "ui_mp/main.menu", stock, msg) &&
                    patch_main_menu(stock, menu, msg);
    fclose(f);
    if (!ok) return FAILED;

    const std::string pk3 = make_pk3("ui_mp/main.menu", menu);
    std::string have;
    if (read_whole(dst, have) && have == pk3) return UP_TO_DATE;
    // write beside, then swap in: the engine never sees a half-written pk3
    char tmp[MAX_PATH];
    snprintf(tmp, sizeof(tmp), "%s.tmp", dst);
    FILE* o = fopen(tmp, "wb");
    const bool written = o && fwrite(pk3.data(), 1, pk3.size(), o) == pk3.size();
    if (o) fclose(o);
    if (!written || !MoveFileExA(tmp, dst, MOVEFILE_REPLACE_EXISTING)) {
        char b[64]; snprintf(b, sizeof(b), "cannot write it (err=%lu)", GetLastError());
        msg = b;
        DeleteFileA(tmp);
        return FAILED;
    }
    msg = std::to_string(pk3.size()) + " bytes";
    return WRITTEN;
}

}  // namespace ui_pk3

void ui_pk3_prepare() {
    if (!game_exe_ok()) return;   // the menu is UO 1.51's: nothing to do for another exe
    if (launched_as_dedicated()) {
        logger::logf("ui_pk3: dedicated server - no menu pk3");
        return;
    }
    char dir[MAX_PATH];
    const DWORD len = GetModuleFileNameA(NULL, dir, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return;
    char* slash = strrchr(dir, '\\');
    if (!slash) return;
    slash[1] = 0;
    char pak[MAX_PATH], dst[MAX_PATH];
    snprintf(pak, sizeof(pak), "%s%s\\%s", dir, GAME_BASE_DIR, ui_pk3::STOCK_PAK);
    snprintf(dst, sizeof(dst), "%s%s\\%s", dir, GAME_BASE_DIR, ui_pk3::PK3_NAME);

    std::string msg;
    switch (ui_pk3::generate_file(pak, dst, msg)) {
    case ui_pk3::UP_TO_DATE: logger::logf("ui_pk3: %s up to date", dst); break;
    case ui_pk3::WRITTEN:    logger::logf("ui_pk3: %s generated from %s (%s)", dst, pak, msg.c_str()); break;
    case ui_pk3::FAILED:     logger::logf("ui_pk3: %s: %s - no CoDUO+ SETTINGS menu entry", pak, msg.c_str()); break;
    }
}

}  // namespace patches
