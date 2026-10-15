#pragma once

// The five articulation slots (docs/UI_SPEC.md sections 7-8, docs/PORT_MAP.md
// "Live-engine decisions"). A bell or bow row plays one of two overlays, chosen with its
// stick box; the baked preset holds every overlay (tools/bake_preset.py), found by name.

#include <array>

namespace cymbalis
{

enum class Blend
{
    none,
    bellToBow, // Blend Position 1: position 1.0 - 0.5 * blend_1
    bowToEdge  // Blend Position 2: position 0.5 - 0.5 * blend_2
};

// Stick choice of a row. Saved with the session; not a parameter.
enum class Stick
{
    none = -1, // EDGE: no choice
    tip = 0,
    shoulder = 1
};

struct SlotInfo
{
    const char* label;           // row label in the editor
    const char* tipOverlay;      // overlay played with TIP ("" = no overlay)
    const char* shoulderOverlay; // overlay played with SHOULDER
    Stick defaultStick;          // Stick::none: the row has no stick choice
    int defaultNote;             // MIDI note, -1 = unassigned
    Blend blend;
    double basePosition;         // 0 edge, 0.5 bow, 1 bell
};

constexpr int kNumSlots = 5;

constexpr std::array<SlotInfo, kNumSlots> kSlots {{
    { "BELL", "bell_tip", "bell_shoulder", Stick::shoulder, 60, Blend::bellToBow, 1.0 },
    { "BOW", "bow_tip", "bow_shoulder", Stick::tip, 62, Blend::bowToEdge, 0.5 },
    { "EDGE", "", "", Stick::none, -1, Blend::none, 0.0 },
    { "BELL2", "bell_tip", "bell_shoulder", Stick::tip, 61, Blend::bellToBow, 1.0 },
    { "BOW2", "bow_tip", "bow_shoulder", Stick::shoulder, 63, Blend::bowToEdge, 0.5 },
}};

constexpr int kDefaultMidiChannel = 16;

inline bool hasStickChoice (const SlotInfo& s) noexcept { return s.defaultStick != Stick::none; }

inline const char* stickName (Stick s) noexcept
{
    return s == Stick::tip ? "TIP" : (s == Stick::shoulder ? "SHOULDER" : "");
}

inline const char* overlayFor (const SlotInfo& s, Stick stick) noexcept
{
    return stick == Stick::shoulder ? s.shoulderOverlay : s.tipOverlay;
}

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
