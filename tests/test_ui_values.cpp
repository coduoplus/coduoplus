// test_ui_values.cpp - the overlay's value labels and 0.01 steps (ui/value_format.h)
//
// The bug this guards: a config's `seta sensitivity "0.65"` showed as 0.7 in Ctrl+M,
// because the label rounded to tenths. The label must show what the engine holds.

#include "ui/value_format.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace patches::ui_values;

static int g_failed = 0;
static void check(bool ok, const char* what) { printf("  [%s] %s\n", ok ? "OK" : "FAIL", what); if (!ok) ++g_failed; }

static bool label_is(const char* cvar_text, const char* want) {
    char out[24];
    format_setting(atof(cvar_text), out, sizeof(out));
    if (strcmp(out, want) != 0) printf("    %s -> \"%s\", want \"%s\"\n", cvar_text, out, want);
    return strcmp(out, want) == 0;
}

static bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

int main() {
    printf("labels show the engine's value:\n");
    check(label_is("0.65", "0.65"), "0.65 reads 0.65 (was 0.7)");
    check(label_is("5", "5.00"), "5 reads 5.00");
    check(label_is("1.5", "1.50"), "1.5 reads 1.50");
    check(label_is("2.375", "2.375"), "2.375 keeps its third decimal");
    check(label_is("1.23456", "1.2346"), "at most 4 decimals");
    check(label_is("0.05", "0.05"), "0.05 (below the slider's 0.1)");
    check(label_is("30", "30.00"), "30");
    check(label_is("0", "0.00"), "0 (acceleration off)");

    printf("0.01 steps:\n");
    check(near(step_hundredths(0.65, +1, 0.01, 30), 0.66), "0.65 + -> 0.66");
    check(near(step_hundredths(0.65, -1, 0.01, 30), 0.64), "0.65 - -> 0.64");
    check(near(step_hundredths(0.655, +1, 0.01, 30), 0.66), "off-grid 0.655 + -> 0.66");
    check(near(step_hundredths(0.655, -1, 0.01, 30), 0.65), "off-grid 0.655 - -> 0.65");
    check(near(step_hundredths(atof("0.7"), -1, 0.01, 30), 0.69), "0.7 - -> 0.69 (float noise ignored)");
    check(near(step_hundredths(0.01, -1, 0.01, 30), 0.01), "clamped at the minimum");
    check(near(step_hundredths(30, +1, 0.01, 30), 30), "clamped at the maximum");

    printf(g_failed ? "test_ui_values: %d FAILED\n" : "test_ui_values: all passed\n", g_failed);
    return g_failed ? 1 : 0;
}
