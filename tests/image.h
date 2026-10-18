#ifndef COD1RELOADED_TESTS_IMAGE_H
#define COD1RELOADED_TESTS_IMAGE_H

// Loads a PE file from disk and maps it the way the Windows loader would
// (sections at their RVAs, relative to the preferred image base), WITHOUT
// running it. Tests then read bytes at the same virtual addresses the mod's
// patches use at runtime, so "the patch expects bytes X at VA Y" can be
// checked offline against the shipped CoD:UO binaries.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

struct Image {
    struct Section {
        std::string name;
        uint32_t rva, vsize;
        bool code;
    };

    std::string path;
    uint32_t base = 0;
    uint32_t size = 0;
    uint32_t timestamp = 0;
    uint32_t import_dir_rva = 0;
    std::vector<Section> sections;
    std::vector<uint8_t> mem;  // size_of_image bytes, zero-filled like the loader

    bool load(const std::string& file);
    bool has(uint32_t va, size_t n) const {
        return va >= base && (uint64_t)va + n <= (uint64_t)base + size;
    }
    // Reads outside the image return a pointer to zeros instead of faulting, so a
    // wrong constant makes a check FAIL rather than crash the test.
    const uint8_t* at(uint32_t va) const {
        static const uint8_t zeros[64] = {0};
        return has(va, 1) ? &mem[va - base] : zeros;
    }
    uint8_t  u8(uint32_t va) const  { return *at(va); }
    uint32_t u32(uint32_t va) const { uint32_t v = 0; if (has(va, 4)) memcpy(&v, at(va), 4); return v; }
    float    f32(uint32_t va) const { float v = 0; if (has(va, 4)) memcpy(&v, at(va), 4); return v; }
    bool bytes_eq(uint32_t va, const std::vector<uint8_t>& b) const;
    bool cstr_eq(uint32_t va, const char* s) const;  // includes the terminating NUL
    // Destination of an E8 rel32 call at `va`.
    uint32_t call_target(uint32_t va) const { return va + 5 + u32(va + 1); }

    // VAs where the NUL-terminated string s starts (preceded by a NUL or at a
    // section start, so "%i" does not match the tail of "%s %i").
    std::vector<uint32_t> find_cstr(const char* s) const;
    // VAs inside executable sections where the 32-bit value v is encoded.
    std::vector<uint32_t> xrefs(uint32_t v) const;
    // Names imported from `dll` (case-insensitive), by-name imports only.
    std::vector<std::string> imports_of(const char* dll) const;
    // VA of the IAT slot for dll!func, 0 if not imported.
    uint32_t iat_slot(const char* dll, const char* func) const;

    bool is_code(uint32_t va) const;
};

#endif
