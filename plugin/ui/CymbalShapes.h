#pragma once

// The shape of each preset's wireframe cymbal (docs/UI_SPEC.md section 11), copied from the
// CYMBALS table of the approved reference, docs/ui/wireframe_reference.py: shapes, lathe zones
// and hammering approved by Kris on 2026-10-27; NOVA RIDE, HAMMERED THIN RIDE and SMOKE FLAT
// RIDE changed on 2026-10-28 (gold lathing measured from the photos, Nova's new bell and bow,
// Hammered's dents, Smoke's lighter gold), SWISH the same day (round 3: gold lathing, a
// copper bell and band, shallow dents in three zones) and RAVEN (round 4: measured lathing,
// power-hammer dents along a spiral). Data only: the mesh, the surface lines and the motion are
// built from it in CymbalModel.

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

// A gold zone: gold metal with lathe lines every goldStep; strength dims it (the reference's
// optional third value, gold_k).
struct GoldZone
{
    double from, to, strength;
};

// A zone of dents packed at random without overlap (the reference's dent_zones, SWISH): from,
// to (fractions of R), how many to place, the range of a (along) and of b/a, and a depth scale.
struct DentZone
{
    double from, to;
    int count;
    double aMin, aMax, ratioMin, ratioMax, depthScale;
};

// Power-hammer dents along a spiral (the reference's dent_spiral, RAVEN). From r = zone.from,
// angle 0, while r < zone.to: the size factor k runs linearly from k0 to k1 across the zone; the
// step along the spiral is spacing x k / max(r, 0.05); each step places a dent (unless U < skip) at
// angle + U(-1, 1) x jitterAlong x step and radius + U(-1, 1) x jitterAcross x pitch x k, its
// a = this->a x k x g and b = this->b x k x g (g = U(sizeMin, sizeMax)), its a axis along the
// radius (rotation its angle + U(-1, 1) x rot), its depth 0.0042 (a / 0.03)^0.7 x
// U(1 - depthVar, 1 + depthVar); then the angle grows by step x U(1 - stepVar, 1 + stepVar) and r
// by pitch x k x step / 2 pi.
struct DentSpiral
{
    double from, to;   // zone, fractions of R
    double k0, k1;     // size factor at its inner and outer radius
    double a, b;       // half axes at k = 1: along the radius, across
    double spacing, pitch;
    double jitterAlong, jitterAcross;
    double sizeMin, sizeMax;
    double rot, skip, stepVar, depthVar;
};

// The hammering pressed into the mesh as dents (the reference's Cymbal2, its approved defaults;
// SWISH and RAVEN set their own depth, contrast, ring step and dents).
struct Dents
{
    int radial = 197;        // radial samples from 0.02 to 1.0, 0.005 R apart
    int around = 480;        // samples around
    int ringStep = 4;        // rings on every ringStep-th radial sample, the bell ring and the rim
    int spokeEvery = 12;     // a spoke on every 12th sample around: 40 spokes
    double depth = 2.4;      // dent_depth
    double size = 2.0;       // dent_size: the large dents' size factor ('dimple')
    int smallDents = 110;    // small dents in the gaps ('dimple')
    double contrast = 5.0;   // dent_contrast: f = clip(1 + contrast * dshade, 0.25, 1.9)
    double simplify = 0.25;  // Douglas-Peucker tolerance, design units
    std::array<DentZone, 3> zones {}; // dent_zones: when there are any, they replace the hammering
    int numZones = 0;
    bool hasSpiral = false;  // dent_spiral: it replaces the hammering and the zones
    DentSpiral spiral {};
    bool linesFollowDents = false; // the lathe and edge lines follow the dents (lines_follow_dents)
};

// Each photo's mean brightness by radius, measured 2026-10-28 from resources/ui/<photo>.png:
// 200 values, 0.005 R steps from the centre (the reference's NOVA_PHOTO_LUM and so on). They
// shade the gold lathing.
inline constexpr std::array<double, 200> kNovaPhotoLum { {
    4.7, 5.1, 6.1, 6.9, 7.7, 60.5, 121.6, 110.4, 112.0, 109.3, 110.9, 111.9, 108.7, 107.5,
    117.0, 92.3, 98.7, 97.3, 95.5, 95.1, 93.8, 94.1, 93.4, 92.3, 93.8, 92.6, 93.7, 91.8,
    92.4, 89.3, 91.6, 92.1, 93.9, 92.9, 96.1, 99.1, 101.5, 104.0, 106.7, 110.1, 112.7, 116.4,
    123.9, 118.9, 102.3, 97.4, 96.7, 97.1, 98.7, 98.6, 98.0, 101.7, 100.2, 101.5, 103.2, 102.8,
    103.0, 105.6, 105.7, 105.2, 108.0, 108.3, 108.5, 109.8, 110.7, 110.4, 110.0, 111.2, 114.5, 115.2,
    128.3, 108.1, 77.0, 79.6, 80.5, 109.5, 120.5, 115.0, 119.5, 94.6, 64.8, 62.6, 60.5, 58.4,
    57.0, 56.1, 55.0, 54.9, 54.8, 55.0, 55.0, 55.6, 55.7, 55.7, 55.6, 55.3, 54.8, 55.4,
    54.7, 54.9, 55.7, 56.0, 56.2, 56.3, 56.6, 57.0, 57.5, 57.9, 58.8, 59.4, 59.7, 60.4,
    60.5, 61.5, 62.2, 63.0, 63.4, 64.3, 65.3, 65.3, 65.5, 66.6, 67.9, 68.6, 69.7, 70.2,
    71.0, 72.5, 73.0, 72.8, 73.8, 74.2, 75.9, 76.2, 78.5, 80.6, 80.6, 81.8, 82.7, 83.9,
    83.2, 83.0, 82.4, 81.8, 81.5, 80.8, 81.0, 82.3, 83.2, 82.3, 82.3, 82.7, 83.6, 82.7,
    83.7, 82.9, 84.1, 83.1, 83.9, 83.6, 83.9, 83.0, 82.4, 81.7, 81.1, 81.1, 80.5, 80.2,
    78.8, 78.5, 77.6, 76.7, 76.6, 76.0, 75.6, 73.9, 73.1, 71.4, 73.0, 72.6, 72.1, 71.3,
    72.6, 71.3, 71.3, 69.7, 69.4, 68.6, 68.6, 67.9, 67.7, 67.6, 73.4, 93.1, 93.8, 92.2,
    91.5, 89.9, 87.3, 90.4,
} };

inline constexpr std::array<double, 200> kHammeredPhotoLum { {
    0.0, 0.0, 0.0, 0.0, 0.0, 83.2, 81.7, 77.1, 79.7, 76.9, 76.9, 76.2, 79.3, 76.0,
    81.8, 77.0, 66.3, 72.0, 71.3, 68.9, 70.3, 66.7, 67.7, 72.4, 70.2, 63.4, 65.0, 62.7,
    64.5, 64.6, 65.1, 64.5, 65.8, 66.5, 69.8, 68.5, 73.5, 76.0, 76.8, 78.6, 79.7, 84.0,
    80.3, 78.7, 77.7, 77.9, 78.5, 80.1, 80.4, 78.2, 78.6, 78.9, 77.2, 77.6, 76.1, 72.7,
    73.6, 72.7, 73.7, 73.1, 71.9, 71.3, 76.6, 77.1, 76.5, 77.4, 75.8, 73.3, 71.6, 71.8,
    72.4, 73.3, 75.2, 72.7, 73.7, 72.1, 71.6, 70.4, 71.9, 73.4, 71.5, 67.3, 62.9, 59.8,
    56.2, 52.6, 54.8, 55.2, 54.5, 55.9, 55.1, 55.8, 54.9, 52.9, 53.6, 52.6, 53.1, 52.7,
    51.1, 52.2, 50.7, 50.7, 51.9, 51.2, 53.6, 54.7, 54.7, 54.6, 54.2, 54.4, 55.3, 54.3,
    55.1, 55.2, 55.3, 54.9, 54.5, 55.9, 56.3, 55.9, 55.2, 55.6, 55.3, 55.8, 56.0, 57.4,
    57.0, 56.6, 57.5, 57.5, 57.4, 57.6, 57.6, 56.7, 56.6, 57.4, 59.3, 58.7, 58.9, 60.0,
    59.3, 59.5, 59.5, 60.1, 58.8, 57.8, 57.0, 57.2, 56.7, 56.7, 56.8, 57.8, 57.9, 58.2,
    59.2, 59.0, 59.2, 61.7, 62.6, 63.2, 63.7, 63.6, 64.6, 63.3, 63.8, 64.6, 62.9, 62.1,
    61.9, 60.4, 60.5, 60.3, 58.6, 57.9, 57.5, 56.7, 57.8, 57.9, 57.9, 58.3, 60.8, 64.2,
    65.2, 66.5, 67.4, 70.8, 70.5, 70.0, 69.7, 69.8, 69.8, 69.4, 68.9, 67.6, 66.8, 66.4,
    66.5, 66.7, 63.8, 68.0,
} };

// SWISH's from Kris's new photo of 2026-10-28 (left and right radii; the reference's
// SWISH_PHOTO_LUM), not from resources/ui/Swish.png.
inline constexpr std::array<double, 200> kSwishPhotoLum { {
    83.9, 85.2, 85.7, 89.1, 85.2, 71.3, 66.8, 73.0, 69.6, 68.4, 61.0, 62.6, 67.2, 65.9,
    61.9, 61.0, 66.8, 75.4, 75.4, 73.7, 68.6, 66.7, 70.7, 72.5, 72.5, 74.3, 75.8, 76.5,
    77.5, 84.6, 89.1, 90.4, 91.1, 90.1, 96.8, 93.6, 94.9, 102.8, 119.7, 118.6, 118.2, 121.1,
    118.2, 121.4, 121.3, 119.3, 117.1, 116.3, 116.2, 116.6, 118.5, 115.6, 119.1, 117.6, 118.2, 117.8,
    116.6, 118.9, 116.9, 117.6, 117.8, 117.2, 115.4, 116.9, 115.0, 114.9, 114.2, 114.6, 116.4, 114.9,
    117.6, 117.5, 117.7, 115.8, 116.5, 116.0, 117.7, 119.6, 121.0, 120.0, 120.4, 120.2, 119.1, 120.3,
    119.9, 119.6, 120.1, 120.8, 121.5, 120.2, 120.9, 118.6, 117.9, 119.4, 121.4, 120.0, 118.0, 116.2,
    115.6, 116.2, 114.6, 117.9, 114.8, 99.2, 73.9, 68.5, 68.7, 67.3, 70.2, 72.0, 67.8, 64.8,
    66.0, 66.9, 69.4, 67.0, 69.5, 72.5, 73.1, 74.4, 71.5, 72.6, 72.7, 72.7, 69.6, 71.8,
    70.8, 69.4, 69.0, 69.2, 69.3, 75.9, 67.8, 70.3, 68.8, 64.2, 69.4, 67.0, 64.6, 64.1,
    69.6, 74.5, 73.6, 68.1, 67.3, 69.5, 71.5, 72.9, 82.6, 97.5, 108.9, 109.0, 107.2, 107.8,
    108.1, 105.8, 105.1, 107.1, 106.5, 106.8, 107.5, 111.5, 118.2, 115.4, 117.9, 122.4, 127.1, 133.6,
    132.4, 130.0, 126.4, 122.1, 119.5, 120.6, 120.5, 119.7, 117.7, 121.3, 125.2, 134.1, 128.0, 133.8,
    127.2, 128.9, 131.4, 130.7, 129.9, 124.9, 122.7, 118.4, 122.8, 122.9, 126.4, 120.6, 118.5, 115.8,
    114.3, 115.3, 127.6, 56.2,
} };

// RAVEN's from Raven.png (rim 353.75 px = 10"), remapped to the wireframe: the photo's bell (edge
// 2.85") scaled onto the wireframe's smaller bell (edge 2.2"), the bow and edge kept (the
// reference's RAVEN_PHOTO_LUM, used as it is).
inline constexpr std::array<double, 200> kRavenPhotoLum { {
    33.6, 32.7, 35.9, 45.0, 51.5, 50.5, 44.5, 48.2, 81.6, 93.9, 101.9, 114.0, 114.1, 107.3,
    95.2, 94.3, 88.0, 90.9, 85.7, 83.3, 86.1, 86.2, 74.9, 95.0, 76.9, 85.6, 78.8, 84.6,
    83.0, 79.6, 88.8, 69.8, 75.7, 77.9, 108.7, 111.8, 110.7, 110.0, 108.2, 109.3, 111.5, 110.5,
    112.6, 114.3, 87.5, 79.0, 70.4, 64.8, 59.8, 54.8, 49.7, 44.7, 42.6, 43.4, 44.3, 45.1,
    45.9, 46.7, 47.5, 48.2, 50.5, 54.1, 55.7, 55.4, 55.2, 55.9, 55.8, 56.6, 57.0, 56.7,
    56.1, 56.0, 55.7, 56.3, 56.3, 56.0, 55.6, 54.7, 54.9, 56.7, 58.1, 58.3, 57.6, 56.8,
    56.9, 57.0, 56.9, 57.4, 57.3, 57.6, 56.9, 56.2, 56.9, 57.0, 58.2, 59.4, 59.7, 60.8,
    60.3, 60.4, 60.7, 61.3, 62.1, 62.1, 62.2, 63.1, 62.9, 63.7, 64.0, 64.7, 65.4, 65.6,
    64.9, 64.7, 64.5, 64.6, 63.8, 64.8, 64.5, 65.0, 66.1, 66.9, 66.5, 67.5, 68.5, 69.6,
    70.5, 70.8, 71.3, 72.0, 71.6, 70.8, 70.8, 71.8, 72.0, 73.1, 74.5, 74.8, 74.6, 75.1,
    74.9, 75.5, 76.8, 77.3, 77.2, 78.0, 77.5, 76.4, 74.9, 74.1, 73.7, 73.7, 74.0, 75.3,
    76.7, 77.7, 78.4, 79.1, 79.3, 79.9, 79.7, 78.8, 77.8, 77.9, 77.4, 76.8, 76.8, 76.6,
    76.2, 75.8, 75.9, 75.7, 75.9, 75.9, 74.5, 73.0, 72.4, 73.2, 73.5, 72.5, 71.8, 71.4,
    71.8, 72.3, 72.3, 72.9, 74.1, 74.8, 74.6, 81.3, 122.3, 107.5, 94.0, 85.2, 71.9, 99.3,
    130.5, 141.3, 140.9, 140.4,
} };

inline constexpr std::array<double, 200> kSmokePhotoLum { {
    0.0, 0.0, 0.0, 0.0, 0.0, 68.1, 100.8, 100.0, 89.0, 79.4, 81.0, 82.2, 82.5, 82.4,
    79.8, 78.0, 76.7, 77.0, 77.1, 81.6, 83.3, 85.9, 88.0, 90.2, 95.5, 101.5, 102.0, 102.3,
    99.8, 99.4, 102.5, 106.7, 106.4, 106.9, 107.6, 109.4, 112.4, 114.3, 115.3, 114.6, 115.1, 116.9,
    118.9, 119.8, 119.4, 120.7, 119.9, 121.0, 122.9, 123.4, 122.8, 118.9, 119.1, 118.6, 119.9, 119.7,
    119.6, 120.0, 120.0, 119.9, 122.6, 121.6, 121.3, 121.4, 123.0, 120.4, 122.2, 120.5, 120.5, 118.7,
    116.7, 116.2, 116.7, 115.6, 113.9, 113.3, 112.6, 113.4, 115.0, 115.0, 114.3, 114.9, 113.6, 114.7,
    113.5, 114.0, 112.6, 113.0, 112.3, 111.6, 111.3, 111.7, 110.6, 108.7, 106.9, 107.3, 107.2, 108.2,
    106.2, 104.3, 104.1, 103.2, 102.4, 102.5, 103.0, 103.7, 103.4, 102.0, 101.4, 100.5, 99.9, 98.6,
    98.8, 97.9, 97.5, 96.5, 95.6, 94.7, 95.0, 93.3, 92.0, 90.5, 89.7, 89.6, 89.3, 89.1,
    87.5, 87.6, 87.1, 86.6, 85.7, 85.5, 85.7, 84.0, 82.7, 80.7, 79.2, 79.4, 78.5, 78.4,
    77.5, 76.9, 75.8, 76.0, 75.3, 76.1, 76.7, 76.6, 76.4, 74.6, 74.4, 74.0, 73.7, 73.3,
    72.9, 72.8, 71.7, 70.8, 70.5, 70.4, 71.3, 71.4, 70.6, 69.5, 70.1, 69.7, 69.0, 69.3,
    70.0, 70.2, 69.3, 68.4, 56.3, 44.3, 39.1, 59.7, 87.6, 99.7, 91.3, 68.5, 47.7, 40.5,
    39.5, 39.2, 38.5, 37.7, 37.3, 37.6, 37.5, 37.7, 38.3, 48.2, 79.2, 91.0, 73.0, 49.8,
    40.9, 39.4, 39.5, 43.5,
} };

struct CymbalShape
{
    const char* key;   // the reference's key
    double R;          // radius, inches
    double bell;       // bell radius, fraction of R (also the hit display's bell zone)
    std::array<ProfilePoint, 15> profile; // from the centre to the rim; heights between them PCHIP
    int numProfile;
    std::array<Zone, 2> lathe;     // drawn in the lathed colour
    int numLathe;
    std::array<Zone, 3> fineLathe; // fine lathe rings every 0.012 R
    int numFineLathe;
    Hammer hammer;
    Zone hammerZone;
    bool hasBellGrooves = false;   // deep lathe grooves on the bell (RAVEN)
    Zone bellGrooves {};

    // 2026-10-28
    std::array<GoldZone, 4> gold {}; // gold zones, with lathe lines every goldStep
    int numGold = 0;
    Zone tint {};                    // gold metal without lathe lines
    bool hasTint = false;
    std::array<double, 2> goldLines {}; // extra edge lines
    int numGoldLines = 0;
    bool zoneEdges = true;           // edge lines at the gold zones' bounds
    double goldStep = 0.012;         // lathe line spacing in the gold zones
    std::array<Zone, 2> copper {};   // copper patina (replaces the bronze or gold colour; dented mesh)
    int numCopper = 0;
    const std::array<double, 200>* photoLum = nullptr; // the photo's brightness by radius
    int lumSmooth = 9;               // its moving-average window
    bool bellLine = true;            // the bell ring 2 units wide (else 1, like the other rings)
    bool dented = false;             // the hammering pressed into the mesh (Dents)
    Dents dents {};
};

// NOVA RIDE (2026-10-28): a bell like Raven's (its edge at 2.2"), a longer, shallower bow down to
// the flat ring; gold lathing measured from Nova.png: one lathed area from the hole to 3.55",
// a narrow ring 3.73-3.97", a band at the rim from 9.58". Scale hammering drawn on the bow.
inline constexpr CymbalShape kNovaShape = [] {
    CymbalShape s {};
    s.key = "nova";
    s.R = 10.0;
    s.bell = 0.22;
    s.profile = { { { 0.0, 0.90 }, { 0.5, 0.88 }, { 1.3, 0.74 }, { 1.8, 0.53 }, { 2.05, 0.41 }, { 2.25, 0.35 },
                    { 2.45, 0.33 }, { 4.1, 0.33 }, { 5.0, 0.40 }, { 6.0, 0.55 }, { 7.0, 0.71 }, { 7.85, 0.80 },
                    { 8.45, 0.75 }, { 9.2, 0.45 }, { 10.0, 0.0 } } };
    s.numProfile = 15;
    s.hammer = Hammer::scale;
    s.hammerZone = { 0.43, 0.925 };
    s.gold = { { { 0.025, 0.355, 1.0 }, { 0.373, 0.397, 1.0 }, { 0.958, 1.0, 1.0 } } };
    s.numGold = 3;
    s.goldStep = 0.006;
    s.photoLum = &kNovaPhotoLum;
    s.bellLine = false;
    return s;
}();

// HAMMERED THIN RIDE (Bettis, 20" paper-thin ride): a large rounded bell, a low, gently falling
// bow with large irregular hammer dents pressed into the mesh (2026-10-28); gold lathing measured
// from its photo: one area from the hole to 4.05", a dimmer band at the rim from 9.0".
inline constexpr CymbalShape kHammeredShape = [] {
    CymbalShape s {};
    s.key = "hammered";
    s.R = 10.0;
    s.bell = 0.19;
    s.profile = { { { 0.0, 1.50 }, { 0.5, 1.47 }, { 1.0, 1.36 }, { 1.4, 1.14 }, { 1.7, 0.88 }, { 1.9, 0.75 },
                    { 2.0, 0.71 }, { 2.3, 0.68 }, { 4.0, 0.58 }, { 5.5, 0.48 }, { 7.5, 0.30 }, { 9.3, 0.10 },
                    { 10.0, 0.0 } } };
    s.numProfile = 13;
    s.hammer = Hammer::dimple;
    s.hammerZone = { 0.43, 0.92 };
    s.gold = { { { 0.025, 0.405, 1.0 }, { 0.90, 1.0, 0.6 } } };
    s.numGold = 2;
    s.goldStep = 0.006;
    s.photoLum = &kHammeredPhotoLum;
    s.bellLine = false;
    s.dented = true;
    return s;
}();

// SMOKE FLAT RIDE (602 flat ride, 20"): no bell, a very low profile, dense radial hammering to
// 8.55" on a lighter gold body (2026-10-28), then the dark lathed edge band measured from its
// photo, with two bright narrow rings.
inline constexpr CymbalShape kSmokeShape = [] {
    CymbalShape s {};
    s.key = "smoke";
    s.R = 10.0;
    s.bell = 0.16;
    s.profile = { { { 0.0, 0.42 }, { 1.0, 0.39 }, { 3.0, 0.31 }, { 6.0, 0.19 }, { 8.8, 0.07 }, { 9.4, 0.04 },
                    { 10.0, 0.0 } } };
    s.numProfile = 7;
    s.hammer = Hammer::radial;
    s.hammerZone = { 0.03, 0.855 };
    s.tint = { 0.0, 0.857 };
    s.hasTint = true;
    s.gold = { { { 0.858, 1.0, 1.0 } } };
    s.numGold = 1;
    s.goldLines = { 0.885, 0.965 };
    s.numGoldLines = 2;
    s.zoneEdges = false;
    s.goldStep = 0.004;
    s.photoLum = &kSmokePhotoLum;
    s.lumSmooth = 1;
    s.bellLine = false;
    return s;
}();

// SWISH (22" china, round 3, 2026-10-28, from Kris's new photos): a squat copper bell (its edge
// at 2.1", its top 1.85"), a straight bow sloping down, the outer flange turned up. Gold lathing
// 2.1-5.7" and 8.2" to the rim, a hammered copper band 5.7-8.2"; the hammering pressed into the
// mesh in three packed zones, shallow, the lathe lines following the dents. Drawn the same size
// as the 20" cymbals.
inline constexpr CymbalShape kSwishShape = [] {
    CymbalShape s {};
    s.key = "swish";
    s.R = 11.0;
    s.bell = 0.19;
    s.profile = { { { 0.0, 1.85 }, { 0.6, 1.81 }, { 1.2, 1.63 }, { 1.6, 1.33 }, { 1.9, 1.02 }, { 2.1, 0.86 },
                    { 2.3, 0.79 }, { 4.5, 0.66 }, { 7.0, 0.42 }, { 8.4, 0.24 }, { 8.75, 0.22 }, { 9.4, 0.30 },
                    { 10.2, 0.50 }, { 11.0, 0.78 } } };
    s.numProfile = 14;
    s.hammer = Hammer::none;
    s.gold = { { { 0.19, 0.52, 1.0 }, { 0.745, 1.0, 1.0 } } };
    s.numGold = 2;
    s.goldStep = 0.014;
    s.copper = { { { 0.0, 0.19 }, { 0.52, 0.745 } } };
    s.numCopper = 2;
    s.photoLum = &kSwishPhotoLum;
    s.bellLine = false;
    s.dented = true;
    s.dents.depth = 1.5;
    s.dents.contrast = 6.0;
    s.dents.ringStep = 5;
    s.dents.zones = { { { 0.04, 0.185, 60, 0.017, 0.024, 0.6, 0.95, 1.0 },    // the bell
                        { 0.525, 0.74, 400, 0.019, 0.027, 0.55, 0.95, 1.0 },  // the copper band
                        { 0.83, 0.985, 160, 0.022, 0.034, 0.45, 0.8, 0.8 } } }; // near the rim
    s.dents.numZones = 3;
    s.dents.linesFollowDents = true;
    return s;
}();

// RAVEN (20" dry ride; round 4, 2026-10-28): a small bell, a moderately high bow. Lathing
// measured from Raven.png, the bell's zones scaled onto the wireframe's smaller bell: a dark ring
// at the hole, a smooth gold lathed ring, deep wavy lathe grooves, a smooth lathed flange to the
// bell's edge; at the rim a narrow bright lathed ring, a dark groove and a lathed band. The bow is
// power-hammered: shallow dents pressed into the mesh along a regular spiral, running along the
// radius and growing toward the edge.
inline constexpr CymbalShape kRavenShape = [] {
    CymbalShape s {};
    s.key = "raven";
    s.R = 10.0;
    s.bell = 0.22;
    s.profile = { { { 0.0, 1.22 }, { 0.5, 1.20 }, { 1.3, 1.06 }, { 1.85, 0.86 }, { 2.1, 0.76 }, { 2.3, 0.73 },
                    { 5.0, 0.58 }, { 7.0, 0.40 }, { 9.0, 0.14 }, { 10.0, 0.0 } } };
    s.numProfile = 10;
    s.hammer = Hammer::none;
    s.hammerZone = { 0.235, 0.935 };
    s.hasBellGrooves = true;
    s.bellGrooves = { 0.073, 0.166 };
    s.gold = { { { 0.039, 0.073, 1.0 }, { 0.170, 0.22, 1.0 }, { 0.945, 0.965, 1.0 }, { 0.975, 1.0, 1.0 } } };
    s.numGold = 4;
    s.goldStep = 0.006;
    s.photoLum = &kRavenPhotoLum;
    s.bellLine = false;
    s.dented = true;
    s.dents.ringStep = 4;
    s.dents.depth = 1.2;
    s.dents.contrast = 12.0;
    s.dents.hasSpiral = true;
    s.dents.spiral = { 0.24, 0.93, 0.6, 1.5, 0.032, 0.018, 0.05, 0.065, 0.06, 0.04, 0.95, 1.05, 0.05, 0.0, 0.02, 0.05 };
    return s;
}();

} // namespace cymbalis::ui
