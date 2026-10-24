// Tuning.h — the values Kris tunes by ear (docs/TUNING.md), as plain data.
//
// A bake may carry a "tuning" block (written by tools/apply_tuning.py from a tuning file of
// the Tuning window). loadBake() applies it with applyTuning(), so BakeData holds the tuned
// values and BakeData::tuning records them. The Tuning window changes the same values live
// (FastEngine::setTuning) and rebuilds the engine for the ring length, with the same
// functions, so what it plays is what the plugin plays from the tuned bake.
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

// 1 in every band: no onset shift (speed), no ring-length change.
constexpr std::array<double, kExcitationBands> kUnitBands = [] {
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
    std::array<double, kExcitationBands> speed = kUnitBands;
    // Attack band gains: per band, a gain (dB) at the strike that fades back to 0 dB with the
    // attack time, exp(-t / time), as Nova's onset gains (bow_onset_band_gains,
    // bow_onset_tau_ms) did. Stored in the bake with the slot (slots[].attack_band_gains),
    // else taken from its onset gains: each band the octave band that contains it.
    std::array<double, kExcitationBands> attackDb {};
    double attackMs = 80.0;
    // Velocity layers of the onset shift and the attack gains: delayMs, speed and attackDb
    // above are the HARD layer (velocity 1.0), these the SOFT layer (velocity 0.3). A strike
    // takes each band between the two in log velocity (onsetLayerT): delay and attack gain
    // linearly, speed geometrically; at 0.3 and below the SOFT value. Equal layers (the
    // default): the same value at every velocity. Stored in the bake with the slot
    // (slots[].onset_shift, slots[].attack_band_gains).
    std::array<double, kExcitationBands> delayMsSoft {};
    std::array<double, kExcitationBands> speedSoft = kUnitBands;
    std::array<double, kExcitationBands> attackDbSoft {};
    // Brightness vs velocity: the spectral tilt of a velocity-0.3 strike relative to a
    // velocity-1.0 one, in dB per octave, pivoting at 1 kHz; 0 at velocity 1.0 and linear in
    // log velocity (brightnessTiltDb). Positive: soft strikes are brighter.
    double brightnessDbOct = 0.0;
    // Stick click (multi_strike_engine_v19.py _add_transient, lines 1194-1211): a band-passed
    // noise burst at the strike, gain x position weight x velocity^1.7, from the engine's
    // random stream. Gain 0: none. The bake's values are the articulation's stick_*
    // parameters (Python's defaults where it has none).
    double stickGain = 0.0;
    double stickLoHz = 2500.0, stickHiHz = 9000.0; // band-pass edges (Butterworth, order 2)
    double stickLenMs = 8.0;                       // length of the noise burst
    double stickDecayMs = 3.0;                     // its exponential decay
};

constexpr int kMaxArticulations = 8;

// Per cymbal. The defaults are the values of a bake without a tuning block.
struct CymbalTuning
{
    double outputGainDb = 0.0;       // whole preset
    double edgeRemapExponent = 0.5;  // NPZ velocity remap of edge strikes (0.5: sqrt)
    double tauRefMs = 0.0;           // hertz_vel_scale contact time at velocity 1 (presets without velocity tables)
    double ringLength = 1.0;         // multiplier of the baked T60 (t60_scale)
    // Per one-third-octave band: a further multiplier of the T60 of its modes (on top of
    // ringLength). 1 everywhere: no change.
    std::array<double, kExcitationBands> ringBand = kUnitBands;
    double wanderDepth = 0.0, wanderRateHz = 1.5;
    double edgeWanderDepth = 0.0, edgeWanderRate = 0.0;
    double mfDepth = 0.0, mfTau = 5.0, mfMin = 0.3, mfMax = 1.0;
    double mfWBow = 0.5, mfWEdge = 1.0, mfWBell = 0.1, mfLoHz = 500.0, mfHiHz = 4000.0;
    double lfDepth = 0.0, lfTau = 5.0, lfMin = 0.3, lfMax = 1.0, lfCutoffHz = 2000.0;
    double lfWBow = 0.5, lfWEdge = 1.0, lfWBell = 0.1, lfWanderRateBoost = 0.0;
    double edgeEnvWindowS = 1.5;     // NPZ band envelope window after an edge strike
    bool frameDecayRestart = true;   // in-frame decay restarting every 20 ms (false: none)
    // EDGE ONSET ENVELOPE: whether an edge strike's level follows the NPZ band envelope for
    // the window (presets with NPZ edge data; the bake's edge_npz.envelope_active).
    bool edgeEnvelope = false;
    // Energy cascade (FastEngine): while a band rings above the threshold, energy moves from
    // it into the bands above it. Amount 0: off. Bands are indices 0 .. 21 (as the band gains).
    double cascadeAmount = 0.0;        // 0 .. 1
    double cascadeThresholdDb = -12.0; // relative to the band's energy after a velocity-1 BOW strike
    double cascadeSpeedMs = 100.0;     // attack of the transfer, and its time constant at amount 1
    double cascadeReleaseMs = 150.0;   // how fast the transfer stops below the threshold
    double cascadeSpread = 2.0;        // how many bands up the energy goes (1 .. 6)
    double cascadeLowBand = 0.0, cascadeHighBand = 21.0; // the bands that take part
    // Loud-band damping (FastEngine): while a band rings above the threshold its modes decay
    // faster. Amount 0: off.
    double loudDampAmount = 0.0;        // 0 .. 4: decay rate x (1 + amount x how far above, capped)
    double loudDampThresholdDb = -12.0; // as the cascade's threshold
    double loudDampTilt = 0.0;          // -1 .. 1: more effect on the low (-) or high (+) bands
    // Re-hit damping (FastEngine): a strike first scales down what is already ringing by
    // amount x velocity x its articulation's weight, over the ramp time. Amount 0: off.
    double rehitDampAmount = 0.0;       // 0 .. 1
    double rehitDampTimeMs = 10.0;      // 1 .. 50 ms
    // Per articulation (bake slot order): its weight, 0 .. 2.
    std::array<double, kMaxArticulations> rehitWeight = [] {
        std::array<double, kMaxArticulations> a {};
        for (auto& x : a)
            x = 1.0;
        return a;
    }();
};

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
constexpr const char* kEdgeEnvelopeKey = "edge_band_envelope";
constexpr const char* kRingBandsKey = "ring_length_bands";
constexpr const char* kRehitWeightsKey = "rehit_damp_weights"; // {articulation id: weight}
constexpr double kRehitWeightMax = 2.0;
constexpr double kRingBandMin = 0.25, kRingBandMax = 4.0;
constexpr const char* kLevelKey = "level_db";
constexpr const char* kVel06Key = "vel_0.6_db";
constexpr const char* kVel03Key = "vel_0.3_db";
constexpr const char* kBandGainsKey = "band_gains_db";
constexpr double kBandGainMinDb = -24.0, kBandGainMaxDb = 12.0;
constexpr const char* kOnsetDelayKey = "onset_delay_ms";
constexpr const char* kOnsetSpeedKey = "onset_speed";
// The SOFT layers (the keys above are the HARD layers).
constexpr const char* kOnsetDelaySoftKey = "onset_delay_ms_soft";
constexpr const char* kOnsetSpeedSoftKey = "onset_speed_soft";
constexpr const char* kAttackGainsSoftKey = "attack_band_gains_db_soft";
constexpr double kSoftLayerVelocity = 0.3, kHardLayerVelocity = 1.0;
constexpr double kOnsetDelayMinMs = -50.0, kOnsetDelayMaxMs = 200.0;
constexpr double kOnsetSpeedMin = 0.25, kOnsetSpeedMax = 4.0;
constexpr const char* kAttackGainsKey = "attack_band_gains_db";
constexpr const char* kAttackTimeKey = "attack_time_ms";
constexpr double kAttackGainMinDb = -12.0, kAttackGainMaxDb = 12.0;
constexpr double kAttackTimeMinMs = 10.0, kAttackTimeMaxMs = 1000.0;
constexpr const char* kBrightnessKey = "brightness_db_per_oct";
constexpr double kBrightnessMin = -6.0, kBrightnessMax = 6.0;
// Stick click (Python's parameter names).
constexpr const char* kStickGainKey = "stick_gain";
constexpr const char* kStickLoKey = "stick_bp_lo";
constexpr const char* kStickHiKey = "stick_bp_hi";
constexpr const char* kStickLenKey = "stick_len_ms";
constexpr const char* kStickDecayKey = "stick_decay_ms";
constexpr double kStickGainMax = 24.0;
constexpr double kStickLoMinHz = 100.0, kStickLoMaxHz = 16000.0;
constexpr double kStickHiMinHz = 200.0, kStickHiMaxHz = 20000.0;
constexpr double kStickLenMinMs = 1.0, kStickLenMaxMs = 50.0;
constexpr double kStickDecayMinMs = 0.5, kStickDecayMaxMs = 50.0;

// The articulation id of a bake entry without an "articulation" key: its overlay, or its
// name in lower case (the loader's SlotData::id).
std::string articulationId(const std::string& entryName, const std::string& overlay);

// Level offset (dB) of the velocity curve at velocity v: 0 at 1.0, vel06Db at 0.6, vel03Db
// at 0.3, linear in log velocity in between and with the 0.6-to-0.3 slope below 0.3.
double velocityCurveDb(const ArticulationTuning& a, double v) noexcept;

// Whether any band gain of the articulation is not 0 dB.
bool hasBandGains(const ArticulationTuning& a) noexcept;
// Whether any band of the articulation has an onset delay or a speed other than 1, in either layer.
bool hasOnsetShift(const ArticulationTuning& a) noexcept;
// Whether the SOFT and HARD layers differ: of the onset shift (delay or speed), of the attack gains.
bool hasShiftLayers(const ArticulationTuning& a) noexcept;
bool hasAttackLayers(const ArticulationTuning& a) noexcept;
// The weight of the SOFT layer for a strike at velocity v: 0 at 1.0, 1 at 0.3 and below,
// linear in log velocity in between (as the velocity curve).
double onsetLayerT(double v) noexcept;
// A strike's onset delays (ms), speeds and attack gains (dB) per band at SOFT weight t.
void layeredShift(const ArticulationTuning& a, double t, double* delayMs, double* speed) noexcept;
void layeredAttack(const ArticulationTuning& a, double t, double* attackDb) noexcept;
// Brightness vs velocity: the tilt (dB per octave around 1 kHz) of a strike at velocity v:
// 0 at 1.0, brightnessDbOct at 0.3, linear in log velocity, within +/-24 dB per octave.
double brightnessTiltDb(const ArticulationTuning& a, double v) noexcept;
// Attack band gains from Nova-style onset gains: each band takes the gain (in dB) of the band
// of `edgesHz` (numGains bands, ascending edges) that contains its centre.
void attackFromOnsetGains(const double* gains, int numGains, const double* edgesHz,
                          std::array<double, kExcitationBands>& outDb) noexcept;
// Each mode's T60 for a ring length and per-band ring lengths: pythonT60 x ringLength x the
// factor of the mode's band (bands as for the band gains). With every factor 1 this is
// exactly pythonT60 x ringLength.
void tunedT60(const CymbalTuning& c, const double* pythonT60, const double* freqHz, int numModes, double* out) noexcept;
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
