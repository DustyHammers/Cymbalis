#pragma once

// The sizzle chain's settings (docs/PORT_MAP.md, "Sizzle chain"; docs/TUNING.md, "Sizzle chain"):
// the eight values a bead chain resting on a ride is tuned with. Plain C++, one small struct with
// its own keys and no ties to the TUNING page, so that it can move as it is to the combined kit
// plugin (Kris, 2026-11-26). They are tuning values (the tuning block, tuning files,
// tools/apply_tuning.py); the chain's on/off switch is not here: it is session state, per preset
// (CymbalisProcessor), not a host parameter. The same defaults on every preset; only the presets
// whose bake has a chain block (STINE FLAT RIDE, HAMMERED THIN RIDE, SWISH, RAVEN and EXTRA PAPER
// THIN 24") have a chain.

#include <array>

namespace cymbalis
{

struct ChainSettings
{
    double chainLevelDb = 0.0;  // SIZZLE LEVEL: dB added to every band of the sizzle
    double chainEdgeDb = 6.0;   // EDGE DRIVE: the drive's weight at position 0.0, dB
    double chainBowDb = 0.0;    // BOW DRIVE: at 0.5
    double chainBellDb = -10.0; // BELL DRIVE: at 1.0 (linear in dB between the three)
    double chainVelocity = 1.0; // VELOCITY EFFECT: the drive x (v / 0.9) ^ this (0: none)
    double chainStopDb = -28.0; // STOPS AT: the gate closes 6 dB under this, dB below a full hit
    double chainSwing = 1.2;    // SWING: how much the sizzle follows the plate's motion
    double chainDamping = 0.35; // DAMPING: every mode's t60 / (1 + this x (f / 4 kHz) ^ 1.5)
};

// Each value's tuning-block key (in the "cymbal" object) and its range.
struct ChainKey
{
    const char* key;
    double ChainSettings::*member;
    double lo, hi;
};

inline const std::array<ChainKey, 8>& chainKeys()
{
    static const std::array<ChainKey, 8> keys { {
        { "chain_level_db", &ChainSettings::chainLevelDb, -20.0, 12.0 },
        { "chain_edge_db", &ChainSettings::chainEdgeDb, -30.0, 12.0 },
        { "chain_bow_db", &ChainSettings::chainBowDb, -30.0, 12.0 },
        { "chain_bell_db", &ChainSettings::chainBellDb, -30.0, 12.0 },
        { "chain_velocity", &ChainSettings::chainVelocity, 0.0, 2.0 },
        { "chain_stop_db", &ChainSettings::chainStopDb, -50.0, -10.0 },
        { "chain_swing", &ChainSettings::chainSwing, 0.0, 3.0 },
        { "chain_damping", &ChainSettings::chainDamping, 0.0, 1.0 },
    } };
    return keys;
}

// Whether every value lies in its range.
inline bool chainSettingsValid (const ChainSettings& s)
{
    for (const auto& k : chainKeys())
        if (! (s.*(k.member) >= k.lo && s.*(k.member) <= k.hi))
            return false;
    return true;
}

} // namespace cymbalis
