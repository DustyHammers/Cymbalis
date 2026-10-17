#pragma once

// Tuning build only (docs/TUNING.md): what the tuning window shows for each value, and the
// tuning file format.

#include <juce_core/juce_core.h>

#include "Bake.h"

#include <array>
#include <vector>

namespace cymbalis::tuning
{

enum class Group
{
    cymbal,
    wander,
    mf,
    lf,
    edgeEnvelope
};

const char* groupName (Group g);

// Which presets a value applies to.
enum class Needs
{
    always,
    npzEdge,          // presets with NPZ edge data (602, Bettis)
    noVelocityTables, // presets whose bow has no velocity participation table (602, Bettis)
    envelope          // presets with an NPZ band envelope (602, Bettis)
};

bool applies (Needs needs, const BakeData& bake);

struct Field
{
    const char* key;   // JSON key (cymbalKeys())
    const char* label;
    const char* unit;
    Group group;
    Needs needs;
    double lo, hi;     // slider range (widened to include a baked value outside it)
    int decimals;
    double CymbalTuning::*member;
    bool rebuild;      // the cymbal is rebuilt when it changes (ring length)
};

// Every numeric per-cymbal value, in display order.
const std::vector<Field>& cymbalFields();

// The articulation values.
struct ArticulationField
{
    const char* key;
    const char* label;
    double lo, hi;
    int decimals;
    double ArticulationTuning::*member;
};
const std::vector<ArticulationField>& articulationFields();

// The per-band values of an articulation: one value per one-third-octave band each, saved
// as all 22 (lowest band first) when any differs from the bake.
struct BandField
{
    const char* key;
    const char* title;    // row title in the window
    const char* unit;
    double lo, hi, flat;  // slider range; FLAT sets every band to `flat` (no change)
    int decimals;
    std::array<double, kExcitationBands> ArticulationTuning::*member;
    bool logarithmic;     // the slider is logarithmic (speed)
};
const std::vector<BandField>& bandFields();

constexpr const char* kFormat = "cymbalis-tuning";
constexpr int kVersion = 1;

// A tuning file (docs/TUNING.md): the values that differ from the bake, and an "effective"
// copy of all of them.
juce::String toJson (const BakeData& bake, const juce::String& presetId, const Tuning& t, const juce::String& producer);

// Reads a tuning file for this preset and bake into `out` (the bake's values, then the
// file's). Returns an error message, or an empty string.
juce::String fromJson (const juce::String& text, const BakeData& bake, const juce::String& presetId, Tuning& out);

// Whether any value differs from the bake.
bool isModified (const BakeData& bake, const Tuning& t);

} // namespace cymbalis::tuning
