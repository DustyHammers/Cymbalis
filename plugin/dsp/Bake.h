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

struct SlotData
{
    std::string name;
    std::string overlay;       // empty = no overlay
    double position = 0.0;     // 0 edge, 0.5 bow, 1 bell
    double calibScalar = 1.0;  // the slot engine's _calib_scalar
    VelocityTable bowVp;
    bool hasBellVp = false;
    VelocityTable bellVp;
    double velGainPower = 1.0;
    double edgeLevelBoost = 1.0;
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

    Pcg64 renderStream;  // strike phases: state when the plugin loads
    double phaseLow = 0.0, phaseHigh = 0.0;

    std::vector<SlotData> slots;
    json::Value overrides; // the overrides the bake was made with
    std::string path;
    std::vector<std::string> report; // what loadBake verified

    int slotIndex(const std::string& name) const;
};

std::shared_ptr<const BakeData> loadBake(const std::string& path);

} // namespace cymbalis
