#pragma once

// The five articulation rows (docs/UI_SPEC.md sections 7-8) and the four presets
// (UI_SPEC section 8, docs/PORT_MAP.md "Live-engine decisions"). Each preset maps every
// row to the bake entries it plays with TIP and with SHOULDER (by entry name in the
// preset's bake, tools/bake_preset.py), or to none, says how its stick box shows and, where
// it offers both sticks, which one a row starts with.

#include <array>

namespace cymbalis
{

enum class Blend
{
    none,
    bellToBow, // Blend Position 1: position 1.0 - 0.5 * blend_1
    bowToEdge  // Blend Position 2: position 0.5 - 0.5 * blend_2
};

// Stick choice of a row, per preset. Saved with the session; not a parameter.
enum class Stick
{
    none = -1, // no choice: a dash
    tip = 0,
    shoulder = 1
};

// One articulation row, the same in every preset.
struct SlotInfo
{
    const char* label;           // row label in the editor (unless the preset names the row), and its session key
    int defaultNote;             // MIDI note, -1 = unassigned
    Blend blend;
    double basePosition;         // 0 edge, 0.5 bow, 1 bell
};

constexpr int kNumSlots = 5;

constexpr std::array<SlotInfo, kNumSlots> kSlots {{
    { "BELL", 60, Blend::bellToBow, 1.0 },
    { "BOW", 62, Blend::bowToEdge, 0.5 },
    { "EDGE", -1, Blend::none, 0.0 },
    { "BELL2", 61, Blend::bellToBow, 1.0 },
    { "BOW2", 63, Blend::bowToEdge, 0.5 },
}};

// How a row's stick box shows in a preset.
enum class StickDisplay
{
    choice,   // TIP / SHOULDER popup (every preset's bell and bow rows)
    tip,      // fixed, shows TIP
    shoulder, // fixed, shows SHOULDER
    dash      // fixed, shows a dash
};

struct PresetSlot
{
    const char* tipEntry;      // bake entry played with TIP ("" = the row is silent)
    const char* shoulderEntry; // bake entry played with SHOULDER
    StickDisplay stick;
    Stick defaultStick;        // the stick a row with a choice starts with (Stick::none: no choice)
    const char* label = nullptr; // the row's label in this preset (nullptr: the row's own, SlotInfo::label)
    float labelSpacing = 0.0f;   // its letter spacing in design units (UI_SPEC section 4)
};

struct PresetInfo
{
    const char* id;        // saved with the session
    const char* name;      // shown in the CYMBAL box
    const char* bakeFile;  // resources/presets/
    const char* photoFile; // resources/ui/
    std::array<PresetSlot, kNumSlots> slots;
    const char* blend1Left = "BELL"; // the left end label of BLEND POSITION 1 (its right end: BOW)
};

constexpr int kNumPresets = 4;

// Every EDGE row is SHOULDER only. The other rows choose TIP or SHOULDER and start with the
// stick of the articulation they played before the shoulder versions existed.
constexpr std::array<PresetInfo, kNumPresets> kPresets {{
    // Nova Ride bake entries: BELL bell_shoulder, BOW bow_tip, EDGE edge (a copy of
    // bow_shoulder played at the edge), BELL2 bell_tip, BOW2 bow_shoulder.
    { "nova_ride", "NOVA RIDE", "nova_ride.bake.json", "Nova.png", {{
        { "BELL2", "BELL", StickDisplay::choice, Stick::shoulder },
        { "BOW", "BOW2", StickDisplay::choice, Stick::tip },
        { "EDGE", "EDGE", StickDisplay::shoulder, Stick::none },
        { "BELL2", "BELL", StickDisplay::choice, Stick::tip },
        { "BOW", "BOW2", StickDisplay::choice, Stick::shoulder },
    }} },
    // 602: no bell. The bell rows play center (a copy of the bow, tuned on its own) and are
    // labelled CENTER and CENTER2, tighter than the other labels so that they end clear of
    // the stick box. The shoulder entries are copies of the tip ones, tuned on their own.
    { "smoke_flat_ride", "SMOKE FLAT RIDE", "smoke_flat_ride.bake.json", "SmokeFlatRide.png", {{
        { "CENTER", "CENTER_SHOULDER", StickDisplay::choice, Stick::tip, "CENTER", 1.5f },
        { "BOW", "BOW_SHOULDER", StickDisplay::choice, Stick::tip },
        { "EDGE", "EDGE", StickDisplay::shoulder, Stick::none },
        { "CENTER", "CENTER_SHOULDER", StickDisplay::choice, Stick::tip, "CENTER2", 0.0f },
        { "BOW", "BOW_SHOULDER", StickDisplay::choice, Stick::tip },
    }}, "CENTER" },
    // Bettis: no overlays; one entry per position, and the shoulder copies of the bell and bow.
    { "hammered_thin_ride", "HAMMERED THIN RIDE", "hammered_thin_ride.bake.json", "HammeredThinRide.png", {{
        { "BELL", "BELL_SHOULDER", StickDisplay::choice, Stick::tip },
        { "BOW", "BOW_SHOULDER", StickDisplay::choice, Stick::tip },
        { "EDGE", "EDGE", StickDisplay::shoulder, Stick::none },
        { "BELL", "BELL_SHOULDER", StickDisplay::choice, Stick::tip },
        { "BOW", "BOW_SHOULDER", StickDisplay::choice, Stick::tip },
    }} },
    // Swish: BELL the base preset at the bell (TIP) and BELL_SHOULDER its copy; BOW and
    // BOW_SHOULDER the bow_tip and bow_shoulder overlays; EDGE the base preset at the edge.
    { "swish", "SWISH", "swish.bake.json", "Swish.png", {{
        { "BELL", "BELL_SHOULDER", StickDisplay::choice, Stick::tip },
        { "BOW", "BOW_SHOULDER", StickDisplay::choice, Stick::tip },
        { "EDGE", "EDGE", StickDisplay::shoulder, Stick::none },
        { "BELL", "BELL_SHOULDER", StickDisplay::choice, Stick::tip },
        { "BOW", "BOW_SHOULDER", StickDisplay::choice, Stick::tip },
    }} },
}};

constexpr int kDefaultMidiChannel = 16;

// Whether a row offers a TIP / SHOULDER choice in a preset; that choice is saved with the
// session, per preset.
inline bool hasStickChoice (int preset, int slot) noexcept
{
    return kPresets[(size_t) preset].slots[(size_t) slot].stick == StickDisplay::choice;
}

// The label of a row in a preset.
inline const char* rowLabel (int preset, int slot) noexcept
{
    const char* l = kPresets[(size_t) preset].slots[(size_t) slot].label;
    return l != nullptr ? l : kSlots[(size_t) slot].label;
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
