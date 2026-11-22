#pragma once

// The five articulation rows (docs/UI_SPEC.md sections 7-8) and the eight presets
// (UI_SPEC section 8, docs/PORT_MAP.md "Live-engine decisions"). Each preset maps every
// row to the bake entries it plays with TIP and with SHOULDER (by entry name in the
// preset's bake, tools/bake_preset.py or tools/bake_hihat.py), or to none, says how its stick
// box shows and, where it offers both sticks, which one a row starts with.

#include "ui/CymbalShapes.h"

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
    { "EDGE", 64, Blend::none, 0.0 },
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

constexpr int kRowNote = -2; // PresetSlot::defaultNote: the row's own (SlotInfo::defaultNote)

struct PresetSlot
{
    const char* tipEntry;      // bake entry played with TIP ("" = the row is silent: the preset has no such row)
    const char* shoulderEntry; // bake entry played with SHOULDER
    StickDisplay stick;
    Stick defaultStick;        // the stick a row with a choice starts with (Stick::none: no choice)
    const char* label = nullptr; // the row's label in this preset (nullptr: the row's own, SlotInfo::label)
    float labelSpacing = 0.0f;   // its letter spacing in design units (UI_SPEC section 4)
    int defaultNote = kRowNote;  // a preset with notes of its own (PresetInfo::ownNotes): this row's default
};

struct PresetInfo
{
    const char* id;        // saved with the session
    const char* name;      // shown in the CYMBAL box
    const char* bakeFile;  // resources/presets/; the plugin embeds its packed form (PackedBake.h)
    const ui::CymbalShape* shape; // its wireframe cymbal (UI_SPEC section 11); shape->bell is the bell (or centre) zone; nullptr: none drawn
    std::array<PresetSlot, kNumSlots> slots;
    const char* blend1Left = "BELL"; // the left end label of BLEND POSITION 1 (its right end: BOW)
    // Notes of its own (the hi-hat): its rows keep their own notes, apart from the notes the
    // cymbals share, with its own defaults (PresetSlot::defaultNote).
    bool ownNotes = false;
    // The Blend Position knobs move its rows' strike positions (slotPosition); otherwise each row
    // strikes at its base position, and the editor hides the knobs.
    bool blendKnobs = true;
    // A pedal (the hi-hat's, a MIDI CC): the editor shows it and its PEDAL row.
    bool pedal = false;
};

constexpr int kNumPresets = 8;

// Every EDGE row is SHOULDER only. The other rows choose TIP or SHOULDER: in every preset BELL
// and BOW start on TIP, BELL2 and BOW2 on SHOULDER (UI_SPEC section 8; Kris, 2026-10-31).
constexpr std::array<PresetInfo, kNumPresets> kPresets {{
    // Nova Ride bake entries: BELL bell_shoulder, BOW bow_tip, EDGE edge (a copy of
    // bow_shoulder played at the edge), BELL2 bell_tip, BOW2 bow_shoulder.
    { "nova_ride", "NOVA RIDE", "nova_ride.bake.json", &ui::kNovaShape, {{
        { "BELL2", "BELL", StickDisplay::choice, Stick::tip },
        { "BOW", "BOW2", StickDisplay::choice, Stick::tip },
        { "EDGE", "EDGE", StickDisplay::shoulder, Stick::none },
        { "BELL2", "BELL", StickDisplay::choice, Stick::shoulder },
        { "BOW", "BOW2", StickDisplay::choice, Stick::shoulder },
    }} },
    // 602: no bell. The bell rows play center (a copy of the bow, tuned on its own) and are
    // labelled CENTER and CENTER2, tighter than the other labels so that they end clear of
    // the stick box. The shoulder entries are copies of the tip ones, tuned on their own.
    { "smoke_flat_ride", "SMOKE FLAT RIDE", "smoke_flat_ride.bake.json", &ui::kSmokeShape, {{
        { "CENTER", "CENTER_SHOULDER", StickDisplay::choice, Stick::tip, "CENTER", 1.5f },
        { "BOW", "BOW_SHOULDER", StickDisplay::choice, Stick::tip },
        { "EDGE", "EDGE", StickDisplay::shoulder, Stick::none },
        { "CENTER", "CENTER_SHOULDER", StickDisplay::choice, Stick::shoulder, "CENTER2", 0.0f },
        { "BOW", "BOW_SHOULDER", StickDisplay::choice, Stick::shoulder },
    }}, "CENTER" },
    // Bettis: no overlays; one entry per position, and the shoulder copies of the bell and bow.
    { "hammered_thin_ride", "HAMMERED THIN RIDE", "hammered_thin_ride.bake.json", &ui::kHammeredShape, {{
        { "BELL", "BELL_SHOULDER", StickDisplay::choice, Stick::tip },
        { "BOW", "BOW_SHOULDER", StickDisplay::choice, Stick::tip },
        { "EDGE", "EDGE", StickDisplay::shoulder, Stick::none },
        { "BELL", "BELL_SHOULDER", StickDisplay::choice, Stick::shoulder },
        { "BOW", "BOW_SHOULDER", StickDisplay::choice, Stick::shoulder },
    }} },
    // Swish: BELL the base preset at the bell (TIP) and BELL_SHOULDER its copy; BOW and
    // BOW_SHOULDER the bow_tip and bow_shoulder overlays; EDGE the base preset at the edge.
    { "swish", "SWISH", "swish.bake.json", &ui::kSwishShape, {{
        { "BELL", "BELL_SHOULDER", StickDisplay::choice, Stick::tip },
        { "BOW", "BOW_SHOULDER", StickDisplay::choice, Stick::tip },
        { "EDGE", "EDGE", StickDisplay::shoulder, Stick::none },
        { "BELL", "BELL_SHOULDER", StickDisplay::choice, Stick::shoulder },
        { "BOW", "BOW_SHOULDER", StickDisplay::choice, Stick::shoulder },
    }} },
    // Raven (20" dry ride): as Bettis, no overlays; one entry per position, and the shoulder
    // copies of the bell and bow.
    { "raven", "RAVEN", "raven.bake.json", &ui::kRavenShape, {{
        { "BELL", "BELL_SHOULDER", StickDisplay::choice, Stick::tip },
        { "BOW", "BOW_SHOULDER", StickDisplay::choice, Stick::tip },
        { "EDGE", "EDGE", StickDisplay::shoulder, Stick::none },
        { "BELL", "BELL_SHOULDER", StickDisplay::choice, Stick::shoulder },
        { "BOW", "BOW_SHOULDER", StickDisplay::choice, Stick::shoulder },
    }} },
    // Extra Paper Thin Ride 24": as Raven, no overlays; one entry per position, and the shoulder
    // copies of the bell and bow.
    { "paperthin", "EXTRA PAPER THIN 24\"", "paperthin.bake.json", &ui::kPaperThinShape, {{
        { "BELL", "BELL_SHOULDER", StickDisplay::choice, Stick::tip },
        { "BOW", "BOW_SHOULDER", StickDisplay::choice, Stick::tip },
        { "EDGE", "EDGE", StickDisplay::shoulder, Stick::none },
        { "BELL", "BELL_SHOULDER", StickDisplay::choice, Stick::shoulder },
        { "BOW", "BOW_SHOULDER", StickDisplay::choice, Stick::shoulder },
    }} },
    // Konstan Crash 18": as Raven, no overlays; one entry per position (the bell inferred, not
    // captured), and the shoulder copies of the bell and bow.
    { "konstan", "KONSTAN CRASH 18\"", "konstan.bake.json", &ui::kKonstanShape, {{
        { "BELL", "BELL_SHOULDER", StickDisplay::choice, Stick::tip },
        { "BOW", "BOW_SHOULDER", StickDisplay::choice, Stick::tip },
        { "EDGE", "EDGE", StickDisplay::shoulder, Stick::none },
        { "BELL", "BELL_SHOULDER", StickDisplay::choice, Stick::shoulder },
        { "BOW", "BOW_SHOULDER", StickDisplay::choice, Stick::shoulder },
    }} },
    // HI-HAT (GF A60, 14"; docs/PORT_MAP.md, "Hi-hat"; a placeholder name until Kris names it):
    // the BOW row plays TIP and the EDGE row EDGE, each with its fixed stick, on General MIDI's
    // closed and open hi-hat notes; no BELL, BELL2 or BOW2 rows. FOOT has no row: the pedal (a
    // MIDI CC) plays it. Its wireframe is the pair of hats (the top cymbal's shape names the
    // bottom one); the Blend Position knobs do nothing on it.
    { "hihat", "HI-HAT", "hihat.bake.json", &ui::kHiHatTopShape, {{
        { "", "", StickDisplay::dash, Stick::none },
        { "TIP", "TIP", StickDisplay::tip, Stick::none, nullptr, 0.0f, 42 },
        { "EDGE", "EDGE", StickDisplay::shoulder, Stick::none, nullptr, 0.0f, 46 },
        { "", "", StickDisplay::dash, Stick::none },
        { "", "", StickDisplay::dash, Stick::none },
    }}, "BELL", true, false, true },
}};

constexpr int kDefaultMidiChannel = 16;
constexpr int kDefaultPedalCc = 1; // the hi-hat pedal's MIDI CC (the mod wheel), per instance

// Whether a preset has this row (an entry to play).
inline bool rowPlays (int preset, int slot) noexcept
{
    return *kPresets[(size_t) preset].slots[(size_t) slot].tipEntry != 0;
}

// A row's default note in a preset: its own where the preset has notes of its own, -1 for a
// row it does not have; else the row's.
inline int defaultNote (int preset, int slot) noexcept
{
    const auto& p = kPresets[(size_t) preset];
    if (! p.ownNotes)
        return kSlots[(size_t) slot].defaultNote;
    const int n = p.slots[(size_t) slot].defaultNote;
    return rowPlays (preset, slot) && n != kRowNote ? n : -1;
}

// The note set a preset plays: 0 the notes the cymbals share, 1 those of the preset with notes
// of its own (the hi-hat; one preset has them).
constexpr int kNumNoteSets = 2;
constexpr int kOwnNotePresets = [] {
    int count = 0;
    for (const auto& p : kPresets)
        count += p.ownNotes ? 1 : 0;
    return count;
}();
static_assert (kOwnNotePresets <= kNumNoteSets - 1, "one note set per preset with notes of its own");
inline int noteSet (int preset) noexcept
{
    return kPresets[(size_t) preset].ownNotes ? 1 : 0;
}

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

// Strike position of a slot for the current blend knobs (UI_SPEC section 9). A preset whose
// rows the knobs do not move (PresetInfo::blendKnobs) strikes each row at its base position.
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
