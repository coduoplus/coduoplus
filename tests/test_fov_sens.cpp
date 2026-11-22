// test_fov_sens.cpp - m_fovscale's formula (input/fov_sens_math.h)
//
// What it guards: the scale is 1 at the reference FOV, follows tan(fov/2) elsewhere
// (the crosshair moves across the screen at one speed), and does not care whether the
// FOV went through Hor+ first - fov_sens.cpp feeds it Hor+-widened values on widescreen.

#include "input/fov_sens_math.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>

using namespace patches::fov_sens;

static int g_failed = 0;
static void check(bool ok, const char* what) { printf("  [%s] %s\n", ok ? "OK" : "FAIL", what); if (!ok) ++g_failed; }
static bool near(double a, double b, double eps = 1e-4) { return std::fabs(a - b) < eps; }

// widescreen_horplus_hfov (video/widescreen_fix.cpp), restated: 4:3 hfov -> vfov ->
// hfov at `aspect`.
static double horplus(double fov_43, double aspect) {
    const double k = 3.14159265358979323846 / 360.0;
    const double v = 2.0 * std::atan(0.75 * std::tan(fov_43 * k));
    return 2.0 * std::atan(aspect * std::tan(v / 2.0)) / (2.0 * k);
}

int main() {
    printf("reference FOV:\n");
    check(near(scale(80, 80), 1.0, 1e-12), "80 at ref 80 -> 1 (hip-fire unchanged)");
    check(near(scale(95, 95), 1.0, 1e-12), "95 at ref 95 -> 1");

    printf("tan ratio, not linear:\n");
    check(near(scale(95, 80), 1.3006), "cg_fov 95, tuned at 80 -> 1.30x (stock: 1.00)");
    check(near(scale(20, 80), 0.2101), "a 20-degree scope at ref 80 -> 0.210 (stock: 0.25)");
    check(near(scale(55, 80), 0.6204), "55-degree iron sights at ref 80 -> 0.620 (stock: 0.69)");
    check(scale(20, 95) < scale(20, 80), "same scope, higher ref -> slower (scope picture identical)");

    printf("Hor+ does not change the result:\n");
    for (double aspect : {16.0 / 9.0, 16.0 / 10.0, 64.0 / 27.0}) {
        bool ok = true;
        for (double fov : {20.0, 55.0, 80.0, 95.0}) {
            for (double ref : {80.0, 90.0}) {
                ok &= near(scale(horplus(fov, aspect), horplus(ref, aspect)), scale(fov, ref), 1e-9);
            }
        }
        char what[64];
        snprintf(what, sizeof(what), "aspect %.3f: same scale as in 4:3 terms", aspect);
        check(ok, what);
    }
    check(near(horplus(80, 16.0 / 9.0) / 80.0, 1.2052), "(stock hip-fire with Hor+ 16:9 is 1.205x)");

    printf("reference input:\n");
    check(clamp_ref(90) == 90, "90 kept");
    check(clamp_ref(0) == kDefaultRef, "0 -> 80");
    check(clamp_ref(200) == kDefaultRef, "200 -> 80");
    check(clamp_ref(std::nan("")) == kDefaultRef, "NaN -> 80");

    printf(g_failed ? "\n%d FAILED\n" : "\nall passed\n", g_failed);
    return g_failed ? 1 : 0;
}
