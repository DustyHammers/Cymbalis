#pragma once

// The five articulation rows (docs/UI_SPEC.md sections 7-8) and the three presets
// (UI_SPEC section 8, docs/PORT_MAP.md "Live-engine decisions"). Each preset maps every
// row to the bake entries it plays with TIP and with SHOULDER (by entry name in the
// preset's bake, tools/bake_preset.py), or to none, and says how its stick box shows.

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
    none = -1, // no choice: a dash
    tip = 0,
    shoulder = 1
};

// One articulation row, the same in every preset.
struct SlotInfo
{
    const char* label;           // row label in the editor
    Stick defaultStick;          // Nova Ride's default stick (Stick::none: no choice)
    int defaultNote;             // MIDI note, -1 = unassigned
    Blend blend;
    double basePosition;         // 0 edge, 0.5 bow, 1 bell
};

constexpr int kNumSlots = 5;

constexpr std::array<SlotInfo, kNumSlots> kSlots {{
    { "BELL", Stick::shoulder, 60, Blend::bellToBow, 1.0 },
    { "BOW", Stick::tip, 62, Blend::bowToEdge, 0.5 },
    { "EDGE", Stick::none, -1, Blend::none, 0.0 },
    { "BELL2", Stick::tip, 61, Blend::bellToBow, 1.0 },
    { "BOW2", Stick::shoulder, 63, Blend::bowToEdge, 0.5 },
}};

// How a row's stick box shows in a preset.
enum class StickDisplay
{
    choice,   // TIP / SHOULDER popup (Nova Ride's bell and bow rows)
    tip,      // fixed, shows TIP
    shoulder, // fixed, shows SHOULDER
    dash      // fixed, shows a dash
};

struct PresetSlot
{
    const char* tipEntry;      // bake entry played with TIP ("" = the row is silent)
    const char* shoulderEntry; // bake entry played with SHOULDER
    StickDisplay stick;
};

struct PresetInfo
{
    const char* id;        // saved with the session
    const char* name;      // shown in the CYMBAL box
    const char* bakeFile;  // resources/presets/
    const char* photoFile; // resources/ui/
    std::array<PresetSlot, kNumSlots> slots;
};

constexpr int kNumPresets = 3;

constexpr std::array<PresetInfo, kNumPresets> kPresets {{
    // Nova Ride bake entries: BELL bell_shoulder, BOW bow_tip, EDGE no overlay,
    // BELL2 bell_tip, BOW2 bow_shoulder.
    { "nova_ride", "NOVA RIDE", "nova_ride.bake.json", "Nova.png", {{
        { "BELL2", "BELL", StickDisplay::choice },
        { "BOW", "BOW2", StickDisplay::choice },
        { "EDGE", "EDGE", StickDisplay::dash },
        { "BELL2", "BELL", StickDisplay::choice },
        { "BOW", "BOW2", StickDisplay::choice },
    }} },
    // 602: bow and edge only; the bell rows are silent.
    { "smoke_flat_ride", "SMOKE FLAT RIDE", "smoke_flat_ride.bake.json", "SmokeFlatRide.png", {{
        { "", "", StickDisplay::dash },
        { "BOW", "BOW", StickDisplay::dash },
        { "EDGE", "EDGE", StickDisplay::dash },
        { "", "", StickDisplay::dash },
        { "BOW", "BOW", StickDisplay::dash },
    }} },
    // Bettis: no overlays; one entry per position.
    { "hammered_thin_ride", "HAMMERED THIN RIDE", "hammered_thin_ride.bake.json", "HammeredThinRide.png", {{
        { "BELL", "BELL", StickDisplay::tip },
        { "BOW", "BOW", StickDisplay::tip },
        { "EDGE", "EDGE", StickDisplay::shoulder },
        { "BELL", "BELL", StickDisplay::tip },
        { "BOW", "BOW", StickDisplay::tip },
    }} },
}};

constexpr int kDefaultMidiChannel = 16;

// Rows that offer a TIP / SHOULDER choice in some preset; their choice is saved with the
// session.
inline bool hasStickChoice (int slot) noexcept
{
    for (const auto& preset : kPresets)
        if (preset.slots[(size_t) slot].stick == StickDisplay::choice)
            return true;
    return false;
}

inline const char* stickName (Stick s) noexcept
{
    return s == Stick::tip ? "TIP" : (s == Stick::shoulder ? "SHOULDER" : "");
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
