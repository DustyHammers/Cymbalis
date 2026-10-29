#pragma once

// RAVEN EDGE PROFILE (docs/TUNING.md; approved by Kris, 2026-10-29): RAVEN's edge-minus-bow
// difference per band of the band gains, in dB, for NOVA RIDE's edge articulation. Entered
// by hand from the approved spec; tools/shoulder_profile.py does not write this file.
//
// Source: RAVEN's SPEAR captures Bow V5-V7 and Edge V5-V7 (not in this repo). Every
// partial's amplitude^2 is summed per 10 ms frame into the 22 bands of the band gains.
//   * Band gains: each capture's band energy over the first 2 s, normalised to its total;
//     per band, edge minus bow in dB, averaged over the three velocity pairs.
//   * Attack gains: per band, the energy in the first 40 ms relative to 100-400 ms; edge
//     minus bow in dB, averaged the same way. Negative: the edge's band starts lower and
//     swells up (RAVEN's edge onset).
//   * Bands with fewer than 5 Nova modes are set to 0 dB (bands 1-4, 112-283 Hz). Both sets
//     are then smoothed over neighbouring bands (weights 1/4, 1/2, 1/4, renormalised over the
//     bands not set to 0 dB) and clamped to +/-12 dB.
//   * Attack time: per band above 566 Hz, the edge / bow amplitude ratio over the first
//     0.6 s (normalised at 0.3-0.6 s) fitted to 1 + (g - 1) exp(-t / tau); the median tau is
//     90 ms (quartiles 35-175 ms).

#include "Tuning.h"

namespace cymbalis::tuning
{
struct RavenEdgeProfile
{
    std::array<double, kExcitationBands> bandDb;     // band gains, lowest band first
    std::array<double, kExcitationBands> attackDb;   // attack gains (HARD and SOFT), lowest band first
    double attackMs;                                 // attack time
};

// Bands' lower edges: 89, 112, 141, 178, 225, 283, 356, 449, 566, 713, 898, 1131, 1426, 1796,
// 2263, 2851, 3592, 4526, 5702, 7184, 9051, 11404 Hz.
inline constexpr RavenEdgeProfile kRavenEdgeProfile {
    {
        -5.88, 0.00, 0.00, 0.00, 0.00, 7.59, 5.12, 0.22, -3.84, -2.77, 0.11,
        0.23, 0.85, 2.95, 4.74, 5.56, 5.22, 2.63, -2.04, -6.37, -10.06, -12.00,
    },
    {
        -12.00, 0.00, 0.00, 0.00, 0.00, -6.41, -0.71, 3.39, 1.60, -1.38, -3.70,
        -4.93, -5.04, -8.49, -11.72, -9.96, -8.67, -7.98, -5.71, -4.10, -4.53, -6.32,
    },
    90.0,
};

} // namespace cymbalis::tuning
