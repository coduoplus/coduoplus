#include "image.h"

#include <cstdio>
#include <cstring>
#include <strings.h>

namespace {
uint16_t rd16(const std::vector<uint8_t>& f, size_t o) { uint16_t v; memcpy(&v, &f[o], 2); return v; }
uint32_t rd32(const std::vector<uint8_t>& f, size_t o) { uint32_t v; memcpy(&v, &f[o], 4); return v; }
}  // namespace

bool Image::load(const std::string& file) {
    path = file;
    FILE* fp = fopen(file.c_str(), "rb");
    if (!fp) return false;
    std::vector<uint8_t> f;
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) f.insert(f.end(), buf, buf + n);
    fclose(fp);
    if (f.size() < 0x200 || f[0] != 'M' || f[1] != 'Z') return false;

    const uint32_t e = rd32(f, 0x3c);
    if (e + 0xf8 > f.size() || rd32(f, e) != 0x00004550) return false;  // "PE\0\0"
    const uint16_t nsec = rd16(f, e + 6);
    timestamp = rd32(f, e + 8);
    const uint16_t opt_size = rd16(f, e + 20);
    const uint32_t opt = e + 24;
    base = rd32(f, opt + 28);
    size = rd32(f, opt + 56);
    const uint32_t hdr_size = rd32(f, opt + 60);
    import_dir_rva = rd32(f, opt + 96 + 8);  // DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]
    mem.assign(size, 0);
    memcpy(mem.data(), f.data(), hdr_size < f.size() ? hdr_size : f.size());

    uint32_t s = opt + opt_size;
    for (int i = 0; i < nsec; ++i, s += 40) {
        char name[9] = {0};
        memcpy(name, &f[s], 8);
        const uint32_t vsize = rd32(f, s + 8), rva = rd32(f, s + 12);
        const uint32_t rsize = rd32(f, s + 16), roff = rd32(f, s + 20);
        const uint32_t chars = rd32(f, s + 36);
        uint32_t take = rsize < vsize || vsize == 0 ? rsize : vsize;
        if (roff + take > f.size()) take = (uint32_t)(f.size() - roff);
        if (rva + take > size) return false;
        memcpy(&mem[rva], &f[roff], take);
        sections.push_back({name, rva, vsize ? vsize : rsize, (chars & 0x20000000) != 0});
    }
    return true;
}

bool Image::bytes_eq(uint32_t va, const std::vector<uint8_t>& b) const {
    return has(va, b.size()) && memcmp(at(va), b.data(), b.size()) == 0;
}

bool Image::cstr_eq(uint32_t va, const char* s) const {
    const size_t n = strlen(s) + 1;
    return has(va, n) && memcmp(at(va), s, n) == 0;
}

bool Image::is_code(uint32_t va) const {
    const uint32_t rva = va - base;
    for (const Section& s : sections)
        if (s.code && rva >= s.rva && rva < s.rva + s.vsize) return true;
    return false;
}

std::vector<uint32_t> Image::find_cstr(const char* s) const {
    std::vector<uint32_t> out;
    const size_t n = strlen(s) + 1;
    for (size_t i = 0; i + n <= mem.size(); ++i) {
        if (mem[i] != (uint8_t)s[0] || memcmp(&mem[i], s, n) != 0) continue;
        if (i > 0 && mem[i - 1] != 0) continue;
        out.push_back(base + (uint32_t)i);
    }
    return out;
}

std::vector<uint32_t> Image::xrefs(uint32_t v) const {
    std::vector<uint32_t> out;
    for (const Section& s : sections) {
        if (!s.code) continue;
        for (uint32_t r = s.rva; r + 4 <= s.rva + s.vsize && r + 4 <= size; ++r) {
            uint32_t x;
            memcpy(&x, &mem[r], 4);
            if (x == v) out.push_back(base + r);
        }
    }
    return out;
}

namespace {

// Walks one DLL's import descriptor, calling fn(name, iat_slot_va) per by-name import.
template <typename Fn>
void walk_imports(const Image& im, const char* dll, Fn fn) {
    if (!im.import_dir_rva) return;
    for (uint32_t d = im.base + im.import_dir_rva; im.u32(d + 12); d += 20) {
        if (strcasecmp((const char*)im.at(im.base + im.u32(d + 12)), dll) != 0) continue;
        const uint32_t oft = im.u32(d), ft = im.u32(d + 16);
        const uint32_t names = im.base + (oft ? oft : ft);
        for (uint32_t i = 0; im.u32(names + 4 * i); ++i) {
            const uint32_t thunk = im.u32(names + 4 * i);
            if (thunk & 0x80000000u) continue;  // by ordinal
            fn((const char*)im.at(im.base + thunk + 2), im.base + ft + 4 * i);
        }
    }
}

}  // namespace

std::vector<std::string> Image::imports_of(const char* dll) const {
    std::vector<std::string> out;
    walk_imports(*this, dll, [&](const char* name, uint32_t) { out.push_back(name); });
    return out;
}

uint32_t Image::iat_slot(const char* dll, const char* func) const {
    uint32_t slot = 0;
    walk_imports(*this, dll, [&](const char* name, uint32_t va) {
        if (strcmp(name, func) == 0) slot = va;
    });
    return slot;
}
