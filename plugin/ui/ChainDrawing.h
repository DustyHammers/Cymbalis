#pragma once

// The sizzle chain on the cymbal (docs/UI_SPEC.md section 11, "The sizzle chain"; Kris,
// 2026-11-26, mockup 3A_chain_one_strand.png): one silver strand of beads from under the wing
// nut's washer out across the bow, on STINE FLAT RIDE, HAMMERED THIN RIDE, SWISH, RAVEN and EXTRA
// PAPER THIN 24", drawn while the chain is on. A drawing piece of its own, as the felt, washer and
// wing nut are (CymbalHardware.h): plain C++ (no JUCE, no OpenGL), built once per preset in the
// cymbal's frame, so it tilts and spins with the cymbal when it is struck; per frame only each
// bead's lift changes (chainLifts), with the sound's chain envelope. CymbalView draws it; the
// shared wireframe renderer planned for the kit can draw it the same way.
//
// Units: the cymbal's radius is 1; sizes in inches are divided by the cymbal's radius in inches.

#include "CymbalModel.h"

#include <cstdint>
#include <vector>

namespace cymbalis::ui::wire
{

// Beads a chain can have (the shaders' array of lifts); a 24" cymbal's has about 45.
constexpr int kMaxChainBeads = 96;

// Its colours (RGB): each bead's outline, its highlight dot, the link wire.
namespace chainColour
{
constexpr Rgb bead { 0xae / 255.0f, 0xaa / 255.0f, 0xa8 / 255.0f };      // #AEAAA8
constexpr Rgb highlight { 0xd0 / 255.0f, 0xcc / 255.0f, 0xca / 255.0f }; // #D0CCCA
constexpr Rgb wire { 0x5f / 255.0f, 0x5d / 255.0f, 0x5c / 255.0f };      // #5F5D5C
} // namespace chainColour
// A bead on screen: its outline (a circle at its radius, 1 unit wide, kChainRingSegments
// segments), and its highlight dot, up-left of its centre by kChainDotOffset x its radius, of
// kChainDotScale x its radius.
constexpr int kChainRingSegments = 16;
constexpr double kChainDotOffset = 0.35, kChainDotScale = 0.3;

struct ChainDrawing
{
    // A bead's centre at rest in the cymbal's frame: its radius and angle (as the mesh's) and its
    // height.
    struct Bead
    {
        double r, theta, z;
    };
    // From under the washer outward, one every 0.16" along the path.
    std::vector<Bead> beads;
    double beadRadius = 0.0; // 1/16" (1/8" across)
    double maxLift = 0.0;    // 0.12": a bead's lift at envelope 1 and n_b 1
    // Each bead's smooth random motion n_b: its rate (8 to 20 Hz) and phase, seeded.
    std::vector<double> rateHz, phase;
    std::uint64_t seed = 0;
};

// The chain on a cymbal of radius `radiusInches` (its R), with the felt and washer of
// CymbalHardware.h on its bell top. The path: from under the washer, at 1.05 x the felt's
// radius, out to r = 0.62 along theta (r) = 118 degrees + 0.06 sin (9 r) r + 0.03 sin (23 r) r
// (radians for the wander); on the surface with its dents (Geometry::surfaceHeight), raised by
// the bead's radius, except out to 1.9 x the felt's radius, where it hangs straight from the
// washer's mid-height down onto the surface.
ChainDrawing chainDrawing (const Geometry& g, double radiusInches);

// n_b (t): bead b's smooth random value, 0 to 1, at `seconds` (value noise at its rate, its
// steps joined by a smoothstep).
double chainNoise (const ChainDrawing& c, size_t bead, double seconds) noexcept;

// Each bead's lift off the surface at `seconds` with the envelope e (0: at rest): 0.12" x e x
// n_b (t), into `lifts` (one per bead).
void chainLifts (const ChainDrawing& c, double envelope, double seconds, float* lifts) noexcept;

// A bead's centre on screen (design units), raised by `lift`, with the frame's spin, tilt and
// lift, and its radius there.
void projectBead (const Camera& cam, const Frame& f, const ChainDrawing& c, size_t bead, double lift, double& sx, double& sy,
                  double& radius) noexcept;

} // namespace cymbalis::ui::wire
