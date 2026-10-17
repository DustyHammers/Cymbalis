// Bake.h — the engine's data, loaded from the JSON written by tools/bake_preset.py.
//
// loadBake() is PORT_MAP stage 0. It parses every array, proves the parse is
// bit-exact (per-array FNV-1a checksums), re-derives the pick vectors and LFO
// tables from the baked PCG64 states, and compares them bit-for-bit with
// numpy's values. It then refuses bakes that need a mechanism this engine does
// not implement yet (anything after stage 2). Every problem throws BakeError.
#pragma once

#include "Json.h"
#include "Pcg64.h"
#include "Tuning.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace cymbalis
{

struct BakeError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

struct VelocityTable
{
    std::vector<double> norms;  // measured velocities, ascending
    std::vector<double> curves; // numModes x numVel, row-major
    int numVel = 0;
};

// Engine-wide effective parameters: the base preset plus the bake's overrides. They are
// the same for every slot (loadBake checks this), so the engine stores them once.
struct EngineParams
{
    // Stage 3: per-mode contact (_apply_strike lines 1067-1093)
    bool contactPermode = false; // tip_contact_ms > 0 and permode_contact
    double tipTau0 = 3.0, tipTauMax = 10.0;
    double shldTau0 = 10.0, shldTauMax = 35.0;
    double sigmaRatio = 0.35;

    // Stage 4: LFO wander (lines 864-867, 1114-1119)
    double wanderDepth = 0.0, wanderRateHz = 1.5;
    double edgeWanderDepth = 0.0, edgeWanderRate = 0.0;

    // Stage 5: LF accumulator (render() lines 652-672): the LF scale (boost when
    // lf_max_scale > 1, else suppress; FastEngine applies it in both modes, PORT_MAP
    // live-engine decision 6) and the wander-rate boost.
    double lfDepth = 0.0, lfTau = 5.0, lfMin = 0.3, lfMax = 1.0;
    double lfWEdge = 1.0, lfWBow = 0.5, lfWBell = 0.1;
    double lfWanderRateBoost = 0.0;
    double lfCutoffHz = 2000.0;

    // Stage 6: velocity-dependent T60 (lines 1127-1130, 861-862)
    double velLenTilt = 0.0, velLenTiltRef = 0.787;

    // Stage 7: MF accumulator (render() lines 674-688, free ring lines 917-919)
    double mfDepth = 0.0, mfTau = 5.0, mfMin = 0.3, mfMax = 1.0;
    double mfWEdge = 1.0, mfWBow = 0.5, mfWBell = 0.1;
    double mfLoHz = 500.0, mfHiHz = 4000.0;

    // Stage 8: per-mode onset ramp (lines 621-650, 847-856, 895-901) and velocity tilt
    bool bandEnvOnset = false; // mode_pk_t data present
    double birthFrac = 0.5;    // band_env_birth_frac
    bool velTilt = false;      // vel_tilt data present

    // hertz_vel_scale (hertz_velocity.py), used by slots without a velocity participation
    // table (_apply_strike line 979): contact duration at velocity 1, ms.
    double tauRefMs = 0.0;
};

// NPZ-derived edge data (__init__ lines 329-345; 602 and Bettis). For a strike with
// edge_frac > 0.1 it replaces the coupling with a velocity-weighted table (velw, lines
// 1043-1061) and, when envelopeActive, drives each mode's level for onsetS seconds from a
// per-band envelope (lines 535-573, 869-894).
struct EdgeNpz
{
    bool present = false;
    std::vector<double> velocities; // measured velocities, ascending
    std::vector<double> velw;       // velocities x numModes, row-major
    std::vector<double> envelopes;  // velocities x numFrames x numBands, zero-padded
    int numFrames = 0, numBands = 0;
    std::vector<int> bandIndex;     // envelope band of each mode
    double frameDurS = 0.02;        // envelope frame length
    bool useVelw = true;            // edge_use_velw
    bool normalize = true;          // edge_velw_normalize
    double levelVelGainPower = 0.7; // exponent of the velocity level (line 1058)
    std::vector<double> rmsCorrection; // per band; empty = none (edge_rms_correction)
    bool envelopeActive = false;    // an edge band envelope file is set too (line 691)
    double onsetS = 1.5;            // edge_band_env_onset_s
    double frameDecayPeriodS = 0.02; // the in-frame decay restarts every 20 ms (line 889)
    // Plugin tuning (docs/TUNING.md): the exponent of the velw velocity remap (line 1049:
    // 0.5, sqrt) and whether the in-frame decay restarts every frame (false: no in-frame
    // decay; the level follows the band envelope alone).
    double remapExponent = 0.5;
    bool frameDecayRestart = true;
};

struct SlotData
{
    std::string name;
    std::string overlay;       // empty = no overlay
    // The articulation id (docs/TUNING.md): the entry's "articulation" key, else its overlay,
    // else its name in lower case. Unique within a bake.
    std::string id;
    double position = 0.0;     // 0 edge, 0.5 bow, 1 bell
    double calibScalar = 1.0;  // the slot engine's _calib_scalar
    // Plugin tuning of this articulation (docs/TUNING.md): its velocity curve (level offsets
    // at velocity 0.6 and 0.3 on top of the level law) and its excitation band gains
    // (slots[].excitation_band_gains_db). levelDb is unused here: see levelTrim.
    ArticulationTuning articulation;
    // Level trim (plugin bake only), as a gain: level_trim.gain_db of the bake, measured
    // with FastEngine so that a velocity-0.9 strike at the base position matches BOW (tip)
    // by 1-second RMS, and editable by hand. 1.0 when the bake has none. FastEngine applies
    // it with the calibration; Engine, the Python-matching path, does not.
    double levelTrim = 1.0;
    VelocityTable bowVp;
    bool hasBellVp = false;
    VelocityTable bellVp;
    double velGainPower = 1.0;
    double edgeLevelBoost = 1.0;
    // false: no velocity participation table; the bow velocity response is hertz_vel_scale.
    bool hasBowVp = true;

    // Stage 9: onset band gains of this slot's overlay (lines 425-442, 902-913).
    bool onsetGainsActive = false;
    std::vector<double> onsetGains; // per band (9 bands)
    double onsetTauS = 0.08;

    // Stage 3: contact force matrix for this slot's position, numModes x contactLen,
    // row-major. Each row is DC-normalised. Empty when contact is off.
    std::vector<double> contactForce;
    int contactLen = 0;
};

struct BakeData
{
    int formatVersion = 0;
    double sampleRate = 0.0;
    int numModes = 0;

    double log1000 = 0.0;          // np.log(1000.0)
    double twoPi = 0.0;            // 2.*np.pi
    double ampSkipThreshold = 0.0; // Python skips a mode for a segment when |z| < this

    // Shared per-mode arrays (identical for every slot).
    std::vector<double> freqHz, t60S, driftHz, excNorm, edgeWeight, bellWeight;
    std::vector<double> pickL, pickR, lfoRateHz, lfoPhase;

    double hpSos[6] = {}; // b0 b1 b2 a0 a1 a2, right channel only

    // Per-sample mode step of the contact phase: exp(1j*2pi*f/SR) * exp(-ln1000/(T60*SR)).
    std::vector<double> modeStepRe, modeStepIm;

    // MF band-pass weight per mode (lines 399-405); empty when the MF accumulator is off.
    std::vector<double> mfBandpass;
    // LF sigmoid per mode, 1 / (1 + exp(-(f - lf_cutoff_hz) / 200)) (lines 391-397); empty
    // when the LF accumulator is off.
    std::vector<double> lfSigmoid;

    // Onset ramp data (stage 8): peak times per mode (indexed by mode position) and the
    // velocity tilt table (velocities x bands), its reference row and each mode's band.
    std::vector<double> pkTBow, pkTEdge;
    // Velocity-bracketed edge pk_t (Bettis format, render() lines 628-631): one row of
    // numModes per velocity. When present, pkTEdge is empty.
    std::vector<double> pkTEdgeByVel, pkTEdgeVelocities;
    std::vector<double> velTiltVelocities, velTiltFractions, velTiltReference;
    std::vector<int> velTiltModeBand;
    int velTiltBands = 0;

    // Onset-gain band of each mode (stage 9, lines 431-438).
    std::vector<int> bowOnsetModeBand;

    EdgeNpz edgeNpz;

    EngineParams params;

    Pcg64 renderStream;  // strike phases: state when the plugin loads
    double phaseLow = 0.0, phaseHigh = 0.0;

    std::vector<SlotData> slots;
    json::Value overrides; // the overrides the bake was made with

    // Plugin tuning (docs/TUNING.md). `untuned` holds the bake's values without its tuning
    // block (level trims included), `tuning` the values in use. pythonT60S and
    // pythonLfoRateHz are the bake's arrays before the ring length and wander rate apply.
    Tuning untuned, tuning;
    bool hasTuningBlock = false;
    double outputGainDb = 0.0;
    std::vector<double> pythonT60S, pythonLfoRateHz;
    std::string path;
    std::vector<std::string> report; // what loadBake verified

    int slotIndex(const std::string& name) const;
};

std::shared_ptr<const BakeData> loadBake(const std::string& path);
// The same from a bake held in memory (the plugin's compiled-in preset). `name` labels errors.
std::shared_ptr<const BakeData> loadBakeFromText(const std::string& text, const std::string& name);

} // namespace cymbalis
