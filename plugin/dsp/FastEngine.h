// FastEngine.h — the real-time Cymbalis engine used by the plugin.
//
// It renders the same model as Engine (the exact port of Python V19), with the same
// segment structure, strike logic, live-engine decisions and engine-as-is behaviours
// (docs/PORT_MAP.md), but computes the free ring differently:
//
//   - Each ringing mode is a recursive complex oscillator, y <- y * s, in float, instead
//     of a per-sample exp() and cos(). At every control point (every 32 samples at 44.1
//     and 48 kHz, 64 at 88.2 and 96 kHz, about 0.7 ms) the oscillator is re-anchored from
//     a double-precision value, so float rounding never accumulates over a ring.
//   - The LFO wander, onset ramp, onset gains and MF scale are evaluated at the control
//     points only. Within a control block the wander phase is interpolated linearly (it
//     becomes part of the oscillator step) and each mode's output gain is interpolated
//     linearly. A mode whose onset ramp reaches 1 inside a block gets the exact ramp, sample
//     by sample, for that block, because a straight line cannot follow the corner.
//   - Modes whose amplitude falls below the skip threshold (1e-9 x calibration, the
//     threshold the exact engine applies at each segment start) stop being rendered.
//   - Every rate-dependent coefficient is computed from the host sample rate: the
//     per-sample mode steps, the contact force (whose Python taus are samples at 44.1
//     kHz) and the right-channel 120 Hz high-pass. Times (pk_t, LFO rates, T60s, the
//     accumulator time constants) are in seconds and do not depend on the rate.
//
// The strike itself (velocity tables, position weight, phases, coupling, contact,
// accumulators) is computed in double exactly as in Engine. Each strike is scaled by its
// slot's calibration and, when the bake has one, its level trim (SlotData::levelTrim). The strike position is an
// argument, so the plugin's blend knobs can move it (docs/UI_SPEC.md section 9).
//
// Thread use: the constructor and prepare() allocate and must not run on the audio
// thread. strike() and process() never allocate, lock or touch files.
#pragma once

#include "Bake.h"
#include "Pcg64.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace cymbalis
{

class FastEngine
{
public:
    static constexpr int kMaxControlBlock = 64;
    static constexpr int kMaxContacts = 64; // overlapping contacts before the oldest is flushed

    // Throws BakeError if the bake needs something this engine does not do.
    explicit FastEngine(std::shared_ptr<const BakeData> bake);

    const BakeData& bake() const noexcept { return *bake_; }
    int numSlots() const noexcept { return static_cast<int>(bake_->slots.size()); }
    int slotIndex(const std::string& name) const { return bake_->slotIndex(name); }

    // Sets the sample rate and computes every rate-dependent coefficient. Allocates.
    // The cymbal stops ringing; the engine clock (in seconds) and the strike-phase random
    // sequence continue from where they were (PORT_MAP live-engine decision 2).
    void prepare(double sampleRate);
    bool isPrepared() const noexcept { return sr_ > 0.0; }
    double sampleRate() const noexcept { return sr_; }
    int controlBlock() const noexcept { return K_; }

    // Strikes `slot` at `position` (0 edge, 0.5 bow, 1 bell) on the next sample that
    // process() renders. Call it between process() calls to place a strike exactly.
    void strike(int slot, double velocity, double position) noexcept;

    // Renders numSamples of stereo output, overwriting left and right.
    void process(float* left, float* right, int numSamples) noexcept;

    // Engine time of the next sample, in seconds since construction.
    double timeSeconds() const noexcept;
    uint64_t strikesApplied() const noexcept { return strikesApplied_; }
    int activeModes() const noexcept { return activeContacts_ > 0 ? n_ : nAct_; }
    int contactLength() const noexcept { return contactLen_; }

private:
    struct Contact
    {
        int k = 0; // next force sample
        std::vector<double> cr, ci;
    };

    // A mode whose onset ramp reaches 1 inside the current control block.
    struct KinkMode
    {
        double yr, yi, sr, si; // oscillator and its step for this block
        double pl, pr, mf, gm1, pt;
    };

    void renderContactSample(float* left, float* right) noexcept;
    void renderFree(float* left, float* right, int n) noexcept;
    void beginControlBlock() noexcept;
    void dropQuietModes() noexcept;
    void renderKinks(float* left, float* right, int j0, int m) noexcept;
    template <int G, bool Ramp>
    void kernel(int m) noexcept;
    void advanceState(int64_t samples) noexcept;
    void startFreePhase() noexcept;
    void velocityParticipation(const VelocityTable& t, double v, double* out) const noexcept;

    std::shared_ptr<const BakeData> bake_;
    const int n_;
    int nPad_ = 0; // n_ rounded up to the kernel's mode group
    double sr_ = 0.0;
    int K_ = 32;   // control block, samples

    // Engine clock: timeBase_ + clock_ / sr_ seconds. timeBase_ absorbs earlier rates.
    double timeBase_ = 0.0;
    uint64_t clock_ = 0;
    uint64_t strikesApplied_ = 0;
    Pcg64 rng_;

    // Modal state (calibrated, double): the running state during a contact, and the
    // state at the start of the free phase afterwards.
    std::vector<double> zr_, zi_;
    std::vector<double> omega_, cosLp_;
    std::vector<double> stepR_, stepI_; // undamped per-sample step at the host rate (contact phase)
    std::vector<double> rot1R_, rot1I_; // exp(i omega / SR)

    // Contact force at the host rate, numModes x contactLen_, DC-normalised per mode.
    std::vector<double> force_;
    int contactLen_ = 0;
    std::vector<Contact> contactPool_;
    int activeContacts_ = 0;

    // Current segment.
    double segStartTime_ = 0.0;
    int segContactSamples_ = 0;
    bool freeStarted_ = false;
    int64_t freePos_ = 0;
    double segTBase_ = 0.0; // absolute time of the first free-phase sample
    int lastSlot_ = -1;

    // Per-strike engine state that Python keeps on the engine object (as in Engine).
    double wanderScale_ = 1.0, wanderRateScale_ = 1.0;
    double lfS_ = 0.0, lfTLast_ = 0.0, lfScaleNow_ = 1.0;
    double washDamp_ = 1.0;
    double mfS_ = 0.0, mfTLast_ = 0.0, mfScaleNow_ = 1.0;
    bool beoHas_ = false;
    double beoTStrike_ = 0.0, beoEf_ = 0.0;
    std::vector<double> beoPkT_;
    std::vector<double> scratchBandScale_;

    // Segment flags (Engine::startFreePhase) and control-point state.
    bool segMfOn_ = false, segRampOn_ = false, segOnsetOn_ = false;
    double segOnsetTau_ = 0.08;
    double segThreshold_ = 0.0;
    double segMaxPt_ = 0.0;   // latest ramp corner (pk_t) among the ringing modes
    bool wanderSmall_ = true; // the wander phase moves < 0.05 rad per block for every mode
    bool flatGain_ = false;   // every mode's gain is constant in this block
    double blockTe0_ = 0.0, blockEnv0_ = 0.0, blockEnv1_ = 0.0;

    // Ringing modes of the free phase, compacted, structure of arrays (nAct_ used, nPad_ allocated).
    int nAct_ = 0;
    std::vector<int> actIdx_;
    // float oscillator, step, output weights and their per-sample increments
    std::vector<float> yr_, yi_, sr32_, si32_, wl_, wr_, dwl_, dwr_;
    // double control-rate state
    std::vector<double> Yr_, Yi_;   // oscillator at the next control point
    std::vector<double> p1r_, p1i_; // damped per-sample step
    std::vector<double> PKr_, PKi_; // p1^K
    std::vector<double> cr_, ci_;   // LFO phasor exp(i (2 pi lr t + lp)) at the next control point
    std::vector<double> qr_, qi_;   // LFO step over one control block
    std::vector<double> phiW_;      // wander phase at the next control point
    std::vector<double> negDlr_, cosLpA_;
    std::vector<double> pl_, pr_, mf_, pt_, invPt_, bfA_, gm1_, gBound_;

    std::vector<KinkMode> kinks_;
    int nKinks_ = 0;

    // Per-sample accumulators of one control block, one 4-lane vector per sample.
    std::vector<float> accL_, accR_;

    // Right-channel high-pass at the host rate (direct form II transposed, double).
    double hpB0_ = 0.0, hpB1_ = 0.0, hpB2_ = 0.0, hpA1_ = 0.0, hpA2_ = 0.0;
    double hpZ0_ = 0.0, hpZ1_ = 0.0;

    // Per-strike scratch (preallocated).
    std::vector<double> scratchPhase_, scratchVel_, scratchBellVel_, scratchPosW_;
};

} // namespace cymbalis
