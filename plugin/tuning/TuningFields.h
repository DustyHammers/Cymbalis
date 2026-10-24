#pragma once

// Tuning code only (CYMBALIS_WITH_TUNING, docs/TUNING.md): what the tuning window shows for each value, and the
// tuning file format.

#include <juce_core/juce_core.h>

#include "Bake.h"
#include "ShoulderProfiles.h"

#include <array>
#include <vector>

namespace cymbalis::tuning
{

enum class Group
{
    cymbal,
    stereo,
    wander,
    mf,
    lf,
    edgeEnvelope,
    cascade,
    loudDamping,
    rehitDamping
};

const char* groupName (Group g);

// Which presets a value applies to.
enum class Needs
{
    always,
    npzEdge,          // presets with NPZ edge data (602, Bettis)
    noVelocityTables, // presets whose bow has no velocity participation table (602, Bettis, Swish)
    envelope          // presets with an NPZ band envelope, on or switched off (602, Bettis, Swish)
};

bool applies (Needs needs, const BakeData& bake);

// Which articulations a whole-cymbal value acts on: every one, or only those struck at some
// places. The window names them after the value's label.
enum class Scope
{
    all,
    edge,         // entries at the edge (position 0.25 or below)
    bow,          // entries on the bow (between 0.25 and 0.75)
    bell,         // entries at the bell or centre (0.75 or above)
    hertz         // entries off the edge without velocity tables (hertz_vel_scale)
};
// The ids of the articulations of `bake` in `scope`, in upper case, joined by ", " (empty
// for Scope::all).
juce::String scopeNames (Scope scope, const BakeData& bake);

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
    Scope scope = Scope::all;
    double step = 0.0; // slider step (0: continuous)
    bool band = false; // a one-third-octave band index, shown as the band's centre frequency
};

// Every numeric per-cymbal value, in display order.
const std::vector<Field>& cymbalFields();

// The articulation values.
struct ArticulationField
{
    const char* key;
    const char* label;
    const char* unit;
    double lo, hi;
    int decimals;
    double ArticulationTuning::*member;
    bool inBands = false; // shown in the BANDS section (attack time), not with the others
};
const std::vector<ArticulationField>& articulationFields();

// A per-band value: one value per one-third-octave band, saved as all 22 (lowest band
// first) when any differs from the bake.
struct BandSpec
{
    const char* key;
    const char* title;    // row title in the window
    const char* unit;
    double lo, hi, flat;  // slider range; FLAT sets every band to `flat` (no change)
    int decimals;
    bool logarithmic;     // the slider is logarithmic (speed, ring length)
    const char* flatLabel = "FLAT"; // the button that sets every band to `flat`
    bool pan = false;     // a pan, -100 .. 100, shown and typed as L100 .. C .. R100
};
// Per articulation. A layered value (onset delay, onset speed, attack gain) has a SOFT layer
// (velocity 0.3) besides `member`, its HARD layer (velocity 1.0); a tuning file holds both,
// the SOFT one under softKey (a file without it uses the HARD values for both).
struct BandField
{
    BandSpec spec;
    std::array<double, kExcitationBands> ArticulationTuning::*member;
    std::array<double, kExcitationBands> ArticulationTuning::*softMember = nullptr;
    const char* softKey = nullptr;
};
const std::vector<BandField>& bandFields();
// Per cymbal.
struct CymbalBandField
{
    BandSpec spec;
    Group group;
    std::array<double, kExcitationBands> CymbalTuning::*member;
};
const std::vector<CymbalBandField>& cymbalBandFields();

// NOVA SHOULDER PROFILE (ShoulderProfiles.h): the band gains, attack gains and attack time
// for a shoulder articulation that is a copy of its tip (SMOKE FLAT RIDE, HAMMERED THIN RIDE,
// SWISH's bell_shoulder): the bell profile for bell_shoulder, the bow profile for
// bow_shoulder and center_shoulder; nullptr for any other.
const NovaShoulderProfile* novaShoulderProfile (const BakeData& bake, const juce::String& presetId, size_t slot);

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
