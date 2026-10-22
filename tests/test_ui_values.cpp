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

    printf("typing a value:\n");
    {
        FieldEdit e;
        edit_begin(e, 0.65);
        check(strcmp(e.buf, "0.65") == 0 && e.fresh, "opens with the current value, selected");
        edit_type(e, "1.2");
        check(strcmp(e.buf, "1.2") == 0, "first key replaces the selected value");
        edit_type(e, "5");
        check(strcmp(e.buf, "1.25") == 0, "then typing appends");
        edit_backspace(e);
        check(strcmp(e.buf, "1.2") == 0, "Backspace removes the last character");
        edit_type(e, ".9");
        check(strcmp(e.buf, "1.29") == 0, "a second decimal point is ignored");
        edit_type(e, "a -x");
        check(strcmp(e.buf, "1.29") == 0, "letters, minus and spaces are ignored");
        edit_begin(e, 2.0);
        edit_type(e, "0,7");
        check(strcmp(e.buf, "0.7") == 0, "a decimal comma becomes a point");
        edit_begin(e, 2.0);
        edit_backspace(e);
        check(e.buf[0] == 0 && !e.fresh, "Backspace on the selected value clears it");
        edit_type(e, "123456789012");
        check(strlen(e.buf) == 8, "at most 8 characters");
        edit_begin(e, 2.0);
        edit_type(e, ".65");
        double v = 0;
        check(parse_setting(e.buf, 0.01, 100, &v) && near(v, 0.65), "\".65\" commits 0.65");
    }
    {
        double v = -1;
        check(!parse_setting("", 0.01, 100, &v), "empty text: nothing committed");
        check(!parse_setting(".", 0.01, 100, &v), "a lone point: nothing committed");
        check(parse_setting("0", 0.01, 100, &v) && near(v, 0.01), "0 clamps to the minimum 0.01");
        check(parse_setting("250", 0.01, 100, &v) && near(v, 100), "250 clamps to the maximum");
        check(parse_setting("0", 0, 10, &v) && near(v, 0), "acceleration 0 is allowed");
        check(parse_setting("2.375", 0.01, 100, &v) && near(v, 2.375), "2.375 kept exactly");
    }

    printf(g_failed ? "test_ui_values: %d FAILED\n" : "test_ui_values: all passed\n", g_failed);
    return g_failed ? 1 : 0;
}
