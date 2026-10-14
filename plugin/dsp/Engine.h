// Engine.h — Cymbalis modal engine, C++ port of the Python V19 reference.
//
// Implemented: PORT_MAP stages 0-2, the linear core.
//   0  baked data and an exact numpy PCG64 / uniform() / choice() (Bake.h)
//   1  strike coupling: velocity tables, position weights, random phases
//   2  modal free ring and state advance, slot calibration, 120 Hz high-pass on
//      the right channel
// Not implemented yet: everything after stage 2. Bakes that need it are
// rejected by loadBake().
//
// Live-engine decisions (docs/PORT_MAP.md):
//   - The engine clock and the strike-phase random sequence start at
//     construction (plugin load) and never restart.
//   - Every strike plays at its exact sample.
//   - One shared modal state for all slots. Each slot's calibration level is
//     applied to its own strikes.
//
// The arithmetic follows multi_strike_engine_v19.py operation for operation:
// evaluation order, the analytic free ring per segment, and the per-segment
// skip threshold. Build with FP contraction off (see CMakeLists.txt).
#pragma once

#include "Bake.h"
#include "Pcg64.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace cymbalis
{

class Engine
{
public:
    explicit Engine(std::shared_ptr<const BakeData> bake);

    const BakeData& bake() const noexcept { return *bake_; }
    int slotIndex(const std::string& name) const { return bake_->slotIndex(name); }

    // Queue a strike for the next process() call, at sampleOffset samples into
    // that block. Offsets at or past the block end carry over to later blocks.
    // Returns false if the queue is full.
    bool strike(int slot, double velocity, int64_t sampleOffset);

    // Render numSamples of stereo output, overwriting left/right.
    void process(double* left, double* right, int numSamples);

    // Samples rendered since construction (the engine clock).
    uint64_t clock() const noexcept { return clock_; }
    // Strikes applied since construction.
    uint64_t strikesApplied() const noexcept { return strikesApplied_; }
    // Modes rendered in the current segment.
    int activeModes() const noexcept { return static_cast<int>(active_.size()); }

private:
    struct PendingStrike
    {
        int slot;
        double velocity;
        int64_t offset;
        uint64_t order;
    };

    void renderSpan(double* left, double* right, int n);
    void advanceState(int64_t samples);
    void applyStrike(int slot, double velocity);
    void startSegment(double skipThreshold);
    void velocityParticipation(const VelocityTable& t, double v, std::vector<double>& out) const;

    std::shared_ptr<const BakeData> bake_;
    const int n_;
    const double sr_;
    const double negLog1000_;

    // Modal state (calibrated amplitudes), real and imaginary parts.
    std::vector<double> zr_, zi_;
    // Per-mode constants.
    std::vector<double> omega_; // (2*pi) * f, as Python's `2.*np.pi*f`
    // Current segment: amplitude and phase at segment start, modes to render.
    std::vector<double> segA_, segPhi_;
    std::vector<int> active_;
    int64_t segPos_ = 0; // samples rendered in the current segment

    Pcg64 rng_;
    uint64_t clock_ = 0;
    uint64_t strikesApplied_ = 0;

    // Right-channel high-pass (scipy sosfilt state, direct form II transposed).
    double hpZ0_ = 0.0, hpZ1_ = 0.0;

    // Strike queue and per-strike scratch (preallocated, no allocation in process()).
    std::vector<PendingStrike> pending_;
    uint64_t pendingOrder_ = 0;
    std::vector<double> scratchPhase_, scratchVel_, scratchBellVel_, scratchPosW_;
};

} // namespace cymbalis
