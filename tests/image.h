#ifndef COD1RELOADED_TESTS_IMAGE_H
#define COD1RELOADED_TESTS_IMAGE_H

// Loads a PE file from disk and maps it the way the Windows loader would
// (sections at their RVAs, relative to the preferred image base), WITHOUT
// running it. Tests then read bytes at the same virtual addresses the mod's
// patches use at runtime, so "the patch expects bytes X at VA Y" can be
// checked offline against the shipped CoD:UO binaries.

#include <cstdint>
#include <string>
#include <vector>

struct Image {
    std::string path;
    uint32_t base = 0;
    uint32_t size = 0;
    uint32_t timestamp = 0;
    std::vector<uint8_t> mem;  // size_of_image bytes, zero-filled like the loader

    bool load(const std::string& file);
    bool has(uint32_t va, size_t n) const {
        return va >= base && (uint64_t)va + n <= (uint64_t)base + size;
    }
    const uint8_t* at(uint32_t va) const { return &mem[va - base]; }
    uint8_t  u8(uint32_t va) const  { return *at(va); }
    uint32_t u32(uint32_t va) const { uint32_t v; memcpy_(&v, at(va), 4); return v; }
    float    f32(uint32_t va) const { float v; memcpy_(&v, at(va), 4); return v; }
    bool bytes_eq(uint32_t va, const std::vector<uint8_t>& b) const;
    bool cstr_eq(uint32_t va, const char* s) const;  // includes the terminating NUL
    // Destination of an E8 rel32 call at `va`.
    uint32_t call_target(uint32_t va) const { return va + 5 + u32(va + 1); }

private:
    static void memcpy_(void* d, const void* s, size_t n);
};

#endif
