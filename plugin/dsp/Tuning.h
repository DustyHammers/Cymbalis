// Tuning.h — the values Kris tunes by ear (docs/TUNING.md), as plain data.
//
// A bake may carry a "tuning" block (written by tools/apply_tuning.py from a tuning file of
// the Cymbalis Tuning build). loadBake() applies it with applyTuning(), so BakeData holds
// the tuned values and BakeData::tuning records them. The tuning build changes the same
// values live (FastEngine::setTuning) and rebuilds the engine for the ring length, with the
// same functions, so what it plays is what the normal build plays from the tuned bake.
#pragma once

#include "Json.h"

#include <array>
#include <string>
#include <vector>

namespace cymbalis
{

struct BakeData;

// The 22 one-third-octave bands of the excitation band gains: band j runs from
// 11404 x 2^((j - 21) / 3) to 11404 x 2^((j - 20) / 3) Hz, 89.1-112.2 Hz up to 11404-14368 Hz
// (the onset data's octave edges 89, 178, ... 11404, in thirds).
constexpr int kExcitationBands = 22;
double excitationBandEdge(int j);    // j = 0 .. kExcitationBands
double excitationBandCentre(int band); // geometric centre
int excitationBand(double freqHz) noexcept; // below the first edge 0, above the last 21

// A speed of 1 in every band: no onset shift.
constexpr std::array<double, kExcitationBands> kUnitSpeeds = [] {
    std::array<double, kExcitationBands> a {};
    for (auto& x : a)
        x = 1.0;
    return a;
}();

// Per articulation (one bake entry).
struct ArticulationTuning
{
    double levelDb = 0.0; // level at velocity 1.0: the slot's level trim (slots[].level_trim.gain_db)
    double vel06Db = 0.0; // level offset at velocity 0.6, on top of the level law
    double vel03Db = 0.0; // level offset at velocity 0.3 (and its slope from 0.6 continues below)
    // Excitation gain of each band (dB): scales the energy this articulation's strikes put
    // into the modes of the band, in every coupling path. Stored in the bake with the slot
    // (slots[].excitation_band_gains_db).
    std::array<double, kExcitationBands> bandDb {};
    // Onset shift of each band: its onset starts delayMs later (negative: earlier) and runs
    // speed times as fast. A band envelope is read at (t - delay) x speed; an onset ramp
    // peaks at peak time / speed + delay. Stored in the bake with the slot
    // (slots[].onset_shift).
    std::array<double, kExcitationBands> delayMs {};
    std::array<double, kExcitationBands> speed = kUnitSpeeds;
};

// Per cymbal. The defaults are the values of a bake without a tuning block.
struct CymbalTuning
{
    double outputGainDb = 0.0;       // whole preset
    double edgeRemapExponent = 0.5;  // NPZ velocity remap of edge strikes (0.5: sqrt)
    double tauRefMs = 0.0;           // hertz_vel_scale contact time at velocity 1 (presets without velocity tables)
    double ringLength = 1.0;         // multiplier of the baked T60 (t60_scale)
    double wanderDepth = 0.0, wanderRateHz = 1.5;
    double edgeWanderDepth = 0.0, edgeWanderRate = 0.0;
    double mfDepth = 0.0, mfTau = 5.0, mfMin = 0.3, mfMax = 1.0;
    double mfWBow = 0.5, mfWEdge = 1.0, mfWBell = 0.1, mfLoHz = 500.0, mfHiHz = 4000.0;
    double lfDepth = 0.0, lfTau = 5.0, lfMin = 0.3, lfMax = 1.0, lfCutoffHz = 2000.0;
    double lfWBow = 0.5, lfWEdge = 1.0, lfWBell = 0.1, lfWanderRateBoost = 0.0;
    double edgeEnvWindowS = 1.5;     // NPZ band envelope window after an edge strike
    bool frameDecayRestart = true;   // in-frame decay restarting every 20 ms (false: none)
};

constexpr int kMaxArticulations = 8;

struct Tuning
{
    CymbalTuning cymbal;
    std::array<ArticulationTuning, kMaxArticulations> articulations {};
    int numArticulations = 0; // = the bake's slots, in bake order
};

// The JSON key of every numeric cymbal value (Python / bake names where they exist).
struct CymbalKey
{
    const char* key;
    double CymbalTuning::*member;
};
const std::vector<CymbalKey>& cymbalKeys();
constexpr const char* kFrameDecayRestartKey = "edge_frame_decay_restart";
constexpr const char* kLevelKey = "level_db";
constexpr const char* kVel06Key = "vel_0.6_db";
constexpr const char* kVel03Key = "vel_0.3_db";
constexpr const char* kBandGainsKey = "band_gains_db";
constexpr double kBandGainMinDb = -24.0, kBandGainMaxDb = 12.0;
constexpr const char* kOnsetDelayKey = "onset_delay_ms";
constexpr const char* kOnsetSpeedKey = "onset_speed";
constexpr double kOnsetDelayMinMs = -50.0, kOnsetDelayMaxMs = 200.0;
constexpr double kOnsetSpeedMin = 0.25, kOnsetSpeedMax = 4.0;

// The articulation id of a bake entry without an "articulation" key: its overlay, or its
// name in lower case (the loader's SlotData::id).
std::string articulationId(const std::string& entryName, const std::string& overlay);

// Level offset (dB) of the velocity curve at velocity v: 0 at 1.0, vel06Db at 0.6, vel03Db
// at 0.3, linear in log velocity in between and with the 0.6-to-0.3 slope below 0.3.
double velocityCurveDb(const ArticulationTuning& a, double v) noexcept;

// Whether any band gain of the articulation is not 0 dB.
bool hasBandGains(const ArticulationTuning& a) noexcept;
// Whether any band of the articulation has an onset delay or a speed other than 1.
bool hasOnsetShift(const ArticulationTuning& a) noexcept;
// Each mode's excitation gain (linear) from the articulation's band gains: out[k] for the
// mode at freqHz[k], k < numModes. Does not allocate.
void excitationModeGains(const ArticulationTuning& a, const double* freqHz, int numModes, float* out) noexcept;

// Per-mode weights of the LF and MF scales (multi_strike_engine_v19.py lines 391-405).
double lfSigmoid(double freqHz, double cutoffHz) noexcept;
double mfBandpass(double freqHz, double loHz, double hiHz) noexcept;

// Writes `t` into the bake: parameters, the per-mode arrays that follow from them (T60,
// LFO rates, LF and MF weights), the NPZ edge settings and every slot's values. Arrays are
// recomputed only where a value differs from the bake's own (Python) value, so the untuned
// values stay bit-exact. Allocates.
void applyTuning(BakeData& bake, const Tuning& t);

// The bake's "tuning" block (format of docs/TUNING.md) read on top of `base`; throws
// std::runtime_error on a malformed block.
Tuning parseTuningBlock(const json::Value& block, const BakeData& bake, const Tuning& base);

} // namespace cymbalis
