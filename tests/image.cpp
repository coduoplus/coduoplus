#include "image.h"

#include <cstdio>
#include <cstring>

void Image::memcpy_(void* d, const void* s, size_t n) { memcpy(d, s, n); }

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
    mem.assign(size, 0);
    memcpy(mem.data(), f.data(), hdr_size < f.size() ? hdr_size : f.size());

    uint32_t s = opt + opt_size;
    for (int i = 0; i < nsec; ++i, s += 40) {
        const uint32_t vsize = rd32(f, s + 8), rva = rd32(f, s + 12);
        const uint32_t rsize = rd32(f, s + 16), roff = rd32(f, s + 20);
        uint32_t take = rsize < vsize || vsize == 0 ? rsize : vsize;
        if (roff + take > f.size()) take = (uint32_t)(f.size() - roff);
        if (rva + take > size) return false;
        memcpy(&mem[rva], &f[roff], take);
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
