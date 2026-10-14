// Engine.h — Cymbalis modal engine, C++ port of the Python V19 reference.
//
// Implemented (docs/PORT_MAP.md, section e):
//   0  baked data and an exact numpy PCG64 / uniform() / choice() (Bake.h)
//   1  strike coupling: velocity tables, position weights, random phases
//   2  modal free ring and state advance, slot calibration, right-channel 120 Hz high-pass
//   3  per-mode Gaussian contact
//   4  LFO wander (phase modulation in the free ring, absolute engine time)
//   5  LF accumulator -> wander rate
// Not implemented: everything after stage 5. Bakes that need it are rejected by loadBake().
// NL (stage 10) is off in the plugin.
//
// Live-engine decisions (docs/PORT_MAP.md):
//   - The engine clock and the strike-phase random sequence start at construction
//     (plugin load) and never restart.
//   - Every strike plays at its exact sample. A strike never cuts short a contact that is
//     still running (EngineConfig::truncateContactsLikePython = false).
//   - One modal state is shared by all slots. Each slot's calibration level is applied to
//     its own strikes.
//
// The engine follows Python's segment structure. A segment runs from one strike to the
// next. It starts with a contact phase, rendered sample by sample (_render_segment lines
// 725-736), followed by the analytic free ring (lines 841-926). The arithmetic follows
// multi_strike_engine_v19.py operation for operation, so build with FP contraction off
// (see CMakeLists.txt).
#pragma once

#include "Bake.h"
#include "Pcg64.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace cymbalis
{

struct EngineConfig
{
    // false (live engine): a strike that arrives while an earlier contact is still running
    // lets that contact finish; the contact phase lasts until every running contact is done.
    // true (Python-matching path): a new strike discards the rest of a running contact, as
    // in Python (N_contact = min(len, N), then _active_contact = None).
    // The two differ only when strikes come less than one contact length apart. The A/B
    // sequences never do that.
    bool truncateContactsLikePython = false;
};

class Engine
{
public:
    explicit Engine(std::shared_ptr<const BakeData> bake, EngineConfig config = {});

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
    // Modes rendered in the current free phase.
    int activeModes() const noexcept { return static_cast<int>(active_.size()); }

private:
    struct PendingStrike
    {
        int slot;
        double velocity;
        int64_t offset;
        uint64_t order;
    };

    // A running contact: this strike's calibrated coupling, fed in through the slot's
    // force matrix one sample at a time.
    struct Contact
    {
        int slot = 0;
        int k = 0;   // next force sample
        int len = 0; // force samples in total
        std::vector<double> cr, ci;
    };

    void render(double* left, double* right, int n);
    void renderContactSample(double* left, double* right);
    void renderFree(double* left, double* right, int n);
    void advanceState(int64_t samples);
    void applyStrike(int slot, double velocity, int64_t strikeSample);
    void startFreePhase();
    void velocityParticipation(const VelocityTable& t, double v, std::vector<double>& out) const;

    std::shared_ptr<const BakeData> bake_;
    const EngineConfig config_;
    const int n_;
    const double sr_;
    const double negLog1000_;

    // Modal state (calibrated amplitudes), real and imaginary parts.
    std::vector<double> zr_, zi_;
    std::vector<double> omega_; // (2*pi) * f, as Python's `2.*np.pi*f`
    std::vector<double> cosLp_; // cos(lfo phase), Python's `np.cos(lp)`

    // Running contacts. A pool sized at construction; activeContacts_ are in use, oldest first.
    std::vector<Contact> contactPool_;
    int activeContacts_ = 0;

    // Current segment (Python's _render_segment call).
    double segStartTime_ = 0.0;   // t_abs: the strike time, in seconds
    int segContactSamples_ = 0;   // n_contact
    bool freeStarted_ = false;    // free phase reached
    int64_t freePos_ = 0;         // free-phase samples rendered (n_free so far)
    std::vector<double> segA_, segPhi_; // amplitude and phase at the start of the free phase
    std::vector<int> active_;           // modes rendered in the free phase (|z| >= threshold)
    int lastSlot_ = -1;                 // slot of the most recent strike

    // Per-strike engine state that Python keeps on the engine object.
    double wanderScale_ = 1.0;     // _wander_scale
    double wanderRateScale_ = 1.0; // _wander_rate_scale
    double lfS_ = 0.0, lfTLast_ = 0.0, lfScaleNow_ = 1.0; // _lf_S, _lf_t_last, _lf_scale_now

    // Free-phase constants per mode, fixed for the segment: -(D/lr) and (2*pi)*lr of the wander.
    std::vector<double> segNegDlr_, segWlr_;

    // Per-sample values shared by all modes, for one chunk of the free phase.
    static constexpr int kChunk = 256;
    double chunkTl_[kChunk];   // t_local
    double chunkTabs_[kChunk]; // t_abs_arr
    double chunkNlt_[kChunk];  // -LOG1000 * t_local

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
