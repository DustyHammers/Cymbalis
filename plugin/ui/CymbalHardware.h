#pragma once

// Cymbal hardware: the felt, washer and wing nut on top of a cymbal's bell (docs/UI_SPEC.md
// section 11; the approved reference is docs/ui/wireframe_reference.py, bell_hardware). Every
// preset but the hi-hat, which keeps its clutch and rod (CymbalModel.h). Drawing-only geometry,
// plain C++ (no JUCE, no OpenGL), apart from the cymbal's mesh: built once per cymbal, in the
// cymbal's frame, it tilts and spins with the cymbal when it is struck. CymbalView draws it; the
// shared wireframe renderer planned for the kit (Cymbalis + Tympanis) can draw it the same way,
// and stands or other hardware could be added beside it.
//
// Units: the cymbal's radius is 1, as everywhere in the wireframe; sizes are given in inches and
// divided by the cymbal's radius in inches (true size). z is the height in the cymbal's frame.

#include "CymbalModel.h"

#include <array>
#include <vector>

namespace cymbalis::ui::wire
{

struct CymbalHardware
{
    // Its lines (HardwareLine: both ends with z the height itself, the colour and width), in
    // drawing order: the felt (its two end circles, a ring at half height, its side lines and its
    // 70 fibres), the washer, the wing nut's boss, then its two wings.
    std::vector<HardwareLine> lines;
    // Its solid parts, each as points whose convex hull on screen is the part's silhouette: the
    // cymbal lines behind it are hidden to 0.08 (its own lines stay drawn, its back lines
    // included). The felt, washer and boss (both end circles, 49 points each) and each wing (its
    // front and back outlines).
    std::vector<std::vector<std::array<double, 3>>> solids;
};

// The felt, washer and wing nut on the bell top of a cymbal of radius `radiusInches` (its R),
// standing at height `bellTop` (the cymbal's height at the centre, in R).
CymbalHardware bellHardware (double radiusInches, double bellTop);

// A point of the hardware (x, y, z in the cymbal's frame) on screen, with the frame's spin, tilt
// and lift: as projectHardware, standing on the frame's lift.
void projectHardwarePoint (const Camera& cam, const Frame& f, double x, double y, double z, double& sx, double& sy) noexcept;

// A solid's silhouette on screen (design units), with the frame's spin, tilt and lift: the convex
// hull of its points, in order around.
void hardwareSilhouette (const Camera& cam, const Frame& f, const std::vector<std::array<double, 3>>& solid,
                         std::vector<std::array<float, 2>>& hull);

} // namespace cymbalis::ui::wire
