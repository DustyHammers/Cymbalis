#pragma once

// The shape of each preset's wireframe cymbal (docs/UI_SPEC.md section 11), copied from the
// CYMBALS table of the approved reference, docs/ui/wireframe_reference.py (Kris, 2026-10-27).
// Data only: the mesh, the surface lines and the motion are built from it in CymbalModel.

#include <array>

namespace cymbalis::ui
{

// The surface texture on the bow.
enum class Hammer
{
    none,   // lathed only
    scale,  // rows of fish-scale arcs
    spiral, // scale-shaped marks along an Archimedean spiral
    dimple, // large irregular dents
    radial  // short radial strokes
};

struct ProfilePoint
{
    double r, z; // radius and height, inches
};

struct Zone
{
    double from, to; // fractions of R
};

struct CymbalShape
{
    const char* key;   // the reference's key
    double R;          // radius, inches
    double bell;       // bell radius, fraction of R
    std::array<ProfilePoint, 13> profile; // from the centre to the rim; heights between them PCHIP
    int numProfile;
    std::array<Zone, 2> lathe;     // drawn in the lathed colour
    int numLathe;
    std::array<Zone, 3> fineLathe; // fine lathe rings every 0.012 R
    int numFineLathe;
    Hammer hammer;
    Zone hammerZone;
    bool hasBellGrooves = false;   // deep lathe grooves on the bell (RAVEN)
    Zone bellGrooves {};
};

// NOVA RIDE
inline constexpr CymbalShape kNovaShape {
    "nova", 10.0, 0.40,
    {{ { 0.0, 1.55 }, { 1.0, 1.25 }, { 2.7, 0.80 }, { 3.3, 0.62 }, { 4.0, 0.36 }, { 4.4, 0.33 },
       { 5.85, 0.33 }, { 6.4, 0.43 }, { 7.1, 0.64 }, { 7.85, 0.80 }, { 8.45, 0.75 }, { 9.2, 0.45 },
       { 10.0, 0.0 } }}, 13,
    {{ { 0.27, 0.405 }, { 0.935, 1.0 } }}, 2,
    {}, 0,
    Hammer::scale, { 0.43, 0.925 },
};

// HAMMERED THIN RIDE (Bettis, 20" paper-thin ride): a large rounded lathed bell, a low, gently
// falling bow with large irregular hammer dents, a lathed band at the edge.
inline constexpr CymbalShape kHammeredShape {
    "hammered", 10.0, 0.19,
    {{ { 0.0, 1.50 }, { 0.5, 1.47 }, { 1.0, 1.36 }, { 1.4, 1.14 }, { 1.7, 0.88 }, { 1.9, 0.75 },
       { 2.0, 0.71 }, { 2.3, 0.68 }, { 4.0, 0.58 }, { 5.5, 0.48 }, { 7.5, 0.30 }, { 9.3, 0.10 },
       { 10.0, 0.0 } }}, 13,
    {{ { 0.0, 0.405 }, { 0.92, 1.0 } }}, 2,
    {{ { 0.03, 0.40 } }}, 1,
    Hammer::dimple, { 0.43, 0.92 },
};

// SMOKE FLAT RIDE (602 flat ride, 20"): no bell, a very low profile, dense radial hammering, a
// lathed edge band.
inline constexpr CymbalShape kSmokeShape {
    "smoke", 10.0, 0.16,
    {{ { 0.0, 0.42 }, { 1.0, 0.39 }, { 3.0, 0.31 }, { 6.0, 0.19 }, { 8.8, 0.07 }, { 9.4, 0.04 },
       { 10.0, 0.0 } }}, 7,
    {{ { 0.885, 1.0 } }}, 1,
    {{ { 0.89, 0.95 } }}, 1,
    Hammer::radial, { 0.03, 0.885 },
};

// SWISH (22" china): a small squat bell, a straight bow sloping down, the outer flange turned up.
// Lathed all over, no hammering. Drawn the same size as the 20" cymbals.
inline constexpr CymbalShape kSwishShape {
    "swish", 11.0, 0.22,
    {{ { 0.0, 1.55 }, { 1.4, 1.48 }, { 2.0, 1.25 }, { 2.35, 0.92 }, { 2.6, 0.80 }, { 4.5, 0.66 },
       { 7.0, 0.42 }, { 8.4, 0.24 }, { 8.75, 0.22 }, { 9.4, 0.30 }, { 10.2, 0.50 }, { 11.0, 0.78 } }}, 12,
    {{ { 0.0, 1.0 } }}, 1,
    {{ { 0.03, 0.21 }, { 0.24, 0.78 }, { 0.80, 1.0 } }}, 3,
    Hammer::none, { 0.0, 0.0 },
};

// RAVEN (20" dry ride): a small lathed bell with its own deep grooves, a moderately high bow with
// large scale-shaped hammering in a spiral, two bright lathed rings at the edge.
inline constexpr CymbalShape kRavenShape {
    "raven", 10.0, 0.22,
    {{ { 0.0, 1.22 }, { 0.5, 1.20 }, { 1.3, 1.06 }, { 1.85, 0.86 }, { 2.1, 0.76 }, { 2.3, 0.73 },
       { 5.0, 0.58 }, { 7.0, 0.40 }, { 9.0, 0.14 }, { 10.0, 0.0 } }}, 10,
    {{ { 0.0, 0.225 }, { 0.935, 1.0 } }}, 2,
    {}, 0,
    Hammer::spiral, { 0.25, 0.93 },
    true, { 0.035, 0.215 },
};

} // namespace cymbalis::ui
