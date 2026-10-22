#ifndef COD1RELOADED_VALUE_FORMAT_H
#define COD1RELOADED_VALUE_FORMAT_H

#include <cmath>
#include <cstdio>
#include <cstring>

// Number display and fine stepping for the Ctrl+M overlay's sliders (ui/modern_menu.cpp).
// Header-only and engine-free so tests/test_ui_values.cpp can check it.
//
// Why: the overlay used to round sensitivity to one decimal for display - a config's
// `seta sensitivity "0.65"` showed as 0.7 - and its slider could only set multiples of
// 0.1. The label now shows the value the engine really holds, and -/+ step by 0.01.

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

}  // namespace ui_values
}  // namespace patches

#endif
