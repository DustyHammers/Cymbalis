#pragma once

// The five articulation slots (docs/UI_SPEC.md sections 7-8, docs/PORT_MAP.md
// "Live-engine decisions"). `bakeName` is the slot's name in the baked preset, which
// carries its overlay data and calibration.

#include <array>

namespace cymbalis
{

enum class Blend
{
    none,
    bellToBow, // Blend Position 1: position 1.0 - 0.5 * blend_1
    bowToEdge  // Blend Position 2: position 0.5 - 0.5 * blend_2
};

struct SlotInfo
{
    const char* label;    // row label in the editor
    const char* bakeName; // slot name in the bake
    const char* stick;    // fixed stick label ("" for none)
    int defaultNote;      // MIDI note, -1 = unassigned
    Blend blend;
    double basePosition;  // 0 edge, 0.5 bow, 1 bell
};

constexpr int kNumSlots = 5;

constexpr std::array<SlotInfo, kNumSlots> kSlots {{
    { "BELL", "BELL", "SHOULDER", 60, Blend::bellToBow, 1.0 },
    { "BOW", "BOW", "TIP", 62, Blend::bowToEdge, 0.5 },
    { "EDGE", "EDGE", "", -1, Blend::none, 0.0 },
    { "BELL2", "BELL2", "TIP", 61, Blend::bellToBow, 1.0 },
    { "BOW2", "BOW2", "SHOULDER", 63, Blend::bowToEdge, 0.5 },
}};

constexpr int kDefaultMidiChannel = 16;

// Strike position of a slot for the current blend knobs (UI_SPEC section 9).
inline double slotPosition (const SlotInfo& s, double blend1, double blend2) noexcept
{
    switch (s.blend)
    {
        case Blend::bellToBow: return 1.0 - 0.5 * blend1;
        case Blend::bowToEdge: return 0.5 - 0.5 * blend2;
        case Blend::none: break;
    }
    return s.basePosition;
}

} // namespace cymbalis
