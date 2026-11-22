#ifndef COD1RELOADED_FOV_SENS_MATH_H
#define COD1RELOADED_FOV_SENS_MATH_H

// The math behind m_fovscale (input/fov_sens.h), kept free of Windows and the game so
// tests/test_fov_sens.cpp can check it.
//
// Stock UO scales the mouse by fov_x / cg_fov (CG_CalcFov, uo_cgame 0x30040352): 1.0
// at hip, a LINEAR ratio in ADS. Linear is not what the eye sees. What stays the same
// on screen when the FOV changes is the speed at the crosshair, and that goes with
// tan(fov/2) ("0% monitor distance", the focal-length ratio):
//
//     scale = tan(fov / 2) / tan(ref / 2)
//
// ref is the FOV the player's sensitivity is tuned at. At fov == ref the scale is 1, a
// wider view turns faster and a zoom turns slower, by exactly how much the picture
// shrank or grew at its centre. Hor+ multiplies tan(fov_x/2) by a constant (aspect *
// 3/4), so as long as fov and ref go through the same transform the ratio is the one
// in cg_fov terms: Hor+, stretched and classic all feel the same at a given cg_fov.

#include <cmath>

namespace patches {
namespace fov_sens {

constexpr double kDefaultRef = 80.0;    // UO's own cg_fov default
constexpr double kMinRef = 10.0;
constexpr double kMaxRef = 160.0;       // CG_CalcFov's own upper clamp

// A reference the player typed: anything outside what a FOV can be falls back to 80.
inline double clamp_ref(double ref) {
    if (!(ref >= kMinRef && ref <= kMaxRef)) return kDefaultRef;   // also catches NaN
    return ref;
}

inline double scale(double fov_deg, double ref_deg) {
    constexpr double k = 3.14159265358979323846 / 360.0;            // degrees -> half angle
    const double r = std::tan(ref_deg * k);
    const double f = std::tan(fov_deg * k);
    if (!(r > 0.0) || !(f > 0.0)) return fov_deg / ref_deg;         // degenerate: stock ratio
    return f / r;
}

}  // namespace fov_sens
}  // namespace patches

#endif
