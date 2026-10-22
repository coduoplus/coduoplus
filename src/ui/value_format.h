#ifndef COD1RELOADED_VALUE_FORMAT_H
#define COD1RELOADED_VALUE_FORMAT_H

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Number display and fine stepping for the Ctrl+M overlay's sliders (ui/modern_menu.cpp).
// Header-only and engine-free so tests/test_ui_values.cpp can check it.
//
// Why: the overlay used to round sensitivity to one decimal for display - a config's
// `seta sensitivity "0.65"` showed as 0.7 - and its slider could only set multiples of
// 0.1. The value now shows what the engine really holds, -/+ step by 0.01, and the
// player can click the value and type one.

namespace patches {
namespace ui_values {

// At least 2 decimals, up to 4 (trailing zeros past the second trimmed):
// 0.65 -> "0.65", 5 -> "5.00", 2.375 -> "2.375", 1.23456 -> "1.2346".
inline void format_setting(double v, char* out, size_t n) {
    snprintf(out, n, "%.4f", v);
    char* dot = strchr(out, '.');
    if (!dot) return;
    char* end = out + strlen(out) - 1;
    while (end > dot + 2 && *end == '0') *end-- = '\0';
}

// One 0.01 step from v, in direction dir (+1 / -1), clamped to [lo, hi]. A value off
// the 0.01 grid goes to the nearest grid value in that direction first (0.655 -> +
// 0.66, - 0.65), so a step never skips past the next round number.
inline double step_hundredths(double v, int dir, double lo, double hi) {
    const double c = v * 100.0;
    double r = dir > 0 ? std::floor(c + 1e-6) + 1.0 : std::ceil(c - 1e-6) - 1.0;
    r /= 100.0;
    if (r < lo) r = lo;
    if (r > hi) r = hi;
    return r;
}

// ---- typed value fields --------------------------------------------------------
// The text being typed into a value field. It opens holding the current value,
// "selected" (fresh): the first character typed replaces it, Backspace clears it.
// Accepted: digits, and one decimal separator ('.' or ',' - a European keyboard's
// decimal key - both stored as '.'). Anything else is ignored.
struct FieldEdit {
    char buf[12] = {0};
    bool fresh = false;
};

inline void edit_begin(FieldEdit& e, double current) {
    format_setting(current, e.buf, sizeof(e.buf));
    e.fresh = true;
}

inline void edit_type(FieldEdit& e, const char* typed) {
    for (const char* p = typed; *p; ++p) {
        char c = *p == ',' ? '.' : *p;
        if (!((c >= '0' && c <= '9') || c == '.')) continue;
        if (e.fresh) { e.buf[0] = 0; e.fresh = false; }
        size_t n = strlen(e.buf);
        if (c == '.' && strchr(e.buf, '.')) continue;
        if (n + 1 >= 9) continue;                 // 8 characters: 0.0001 .. 99999999
        e.buf[n] = c;
        e.buf[n + 1] = 0;
    }
}

inline void edit_backspace(FieldEdit& e) {
    if (e.fresh) { e.buf[0] = 0; e.fresh = false; return; }
    size_t n = strlen(e.buf);
    if (n) e.buf[n - 1] = 0;
}

// The typed text as a value: false if it holds no digit ("" or "."), else the number
// clamped to [lo, hi].
inline bool parse_setting(const char* text, double lo, double hi, double* out) {
    bool digit = false;
    for (const char* p = text; *p; ++p) {
        if (*p >= '0' && *p <= '9') digit = true;
        else if (*p != '.') return false;
    }
    if (!digit) return false;
    double v = atof(text);
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    *out = v;
    return true;
}

}  // namespace ui_values
}  // namespace patches

#endif
