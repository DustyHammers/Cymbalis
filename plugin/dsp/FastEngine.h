// FastEngine.h — the real-time Cymbalis engine used by the plugin.
//
// It renders the model of Engine (the exact port of Python V19) with the same strike logic,
// segment structure, live-engine decisions and contact phase, with one deliberate departure
// that Kris asked for (docs/PORT_MAP.md, "Superposition in the plugin"):
//
//   Each strike's own effects apply only to the energy that strike added. The onset band
//   gains and their tau, the edge onset ramp with its velocity tilt, and the
//   velocity-dependent decay (wash_damp / vel_len_tilt) belong to the strike. What stays
//   cymbal-wide, as in Python: the shared modal state (strikes add together), the MF
//   accumulator's damping, the LF accumulator's wander-rate boost, and the wander itself.
//   For a single strike on its own this is the same computation as before. During a
//   strike's contact phase the whole state is output without any factor, as in Python
//   (PORT_MAP f.4), so for those samples older strikes' gains and ramps pause too.
//
// How it is computed:
//
//   - Every strike becomes a component: per mode, its complex amplitude, its own decay
//     and its own onset envelopes.
//   - Per mode, one recursive complex oscillator in float carries what all components share:
//     frequency, wander phase and the slowest decay a strike can have (T60 / the smallest
//     wash_damp of a velocity in 0..1), so every component's own decay relative to it only
//     ever falls. The oscillator is re-anchored from double at every control point (128
//     samples at 44.1/48 kHz, 256 at 88.2/96 kHz: about 2.8 ms).
//   - At each control point the components are summed into one complex amplitude per mode
//     (times the shared MF scale), four modes per vector, in float; within the block it is
//     interpolated linearly. The output of a mode is Re(oscillator x amplitude) times its
//     pick. A component whose onset ramp reaches 1 inside a block is rendered sample by
//     sample with the exact ramp there.
//   - Components are kept in a fixed pool. When it is full, two components whose envelopes
//     have settled and whose decays are closest are merged (amplitudes added, decay weighted
//     by energy); failing that, the two oldest are merged and their remaining onset
//     envelopes are dropped.
//   - Modes and components below the skip threshold (1e-9 x level) stop being rendered.
//   - Every rate-dependent coefficient comes from the host sample rate: mode steps, the
//     contact force (Python's taus are 44.1 kHz samples) and the right-channel 120 Hz
//     high-pass.
//   - Presets without velocity participation tables use hertz_vel_scale. Presets with NPZ
//     edge data (602, Bettis) take an edge strike's coupling from the NPZ velw tables and,
//     with a band envelope, drive that strike's level from it for the onset window
//     (PORT_MAP, "602 and Bettis"). For those the shared oscillator carries no decay and
//     the control block is 1/7 or 1/8 of the envelope's 20 ms frame.
//
// Thread use: the constructor and prepare() allocate and must not run on the audio
// thread. strike() and process() never allocate, lock or touch files.
#pragma once

#include "Bake.h"
#include "Pcg64.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace cymbalis
{

class FastEngine
{
public:
    static constexpr int kMaxControlBlock = 256;
    static constexpr int kMaxContacts = 64;   // overlapping contacts before the oldest is flushed
    static constexpr int kMaxComponents = 24; // strikes kept apart before merging

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

    // Takes over another engine's clock when it replaces it (a preset switch): the engine
    // clock continues from that engine's time. The strike-phase sequence is this engine's own,
    // from its preset's seed (as in Python). Does not allocate.
    void continueFrom(const FastEngine& previous) noexcept;
    // Nothing rings: no ringing mode, no running contact, no strike energy.
    bool isSilent() const noexcept;

    // Engine time of the next sample, in seconds since construction.
    double timeSeconds() const noexcept;
    uint64_t strikesApplied() const noexcept { return strikesApplied_; }
    int activeModes() const noexcept { return activeContacts_ > 0 ? n_ : nAct_; }
    int activeComponents() const noexcept;
    int contactLength() const noexcept { return contactLen_; }

private:
    struct Contact
    {
        int k = 0; // next force sample
        std::vector<double> cr, ci;
    };

    // One strike's energy and its own envelopes. Per-mode arrays are indexed by mode.
    struct Component
    {
        bool used = false;
        bool pending = false;     // its contact is still running; Br/Bi hold the coupling
        uint64_t order = 0;       // creation order, for "oldest"
        int64_t contactStart = 0; // engine sample where its contact began
        double eps = 0.0;         // decay relative to the oscillator's (wash_damp - oscWash_), energy-weighted
        double tStrike = 0.0;     // engine time of the strike
        bool onsetOn = false;     // onset band gains (its slot's overlay, not at the edge)
        double onsetTau = 0.08, gm1Max = 0.0;
        bool rampOn = false;      // edge onset ramp
        double ptMax = 0.0;
        bool settled = true;      // its onset envelopes have run out: it only decays
        double env0 = 0.0, env1 = 0.0; // onset envelope at the current block's ends
        // NPZ edge band envelope (render() lines 535-573, 869-894): for its first onsetS
        // seconds this strike's level per mode is |coupling| x band envelope x the decay
        // since the start of the current 20 ms frame, instead of its decaying amplitude.
        bool envOn = false;
        int envV0 = 0;              // velocity interpolation of the envelopes: rows envV0, envV0 + 1
        double envVt = 0.0;         //   and the weight of the second
        double envScale = 1.0;      // 1 / peak of the interpolated envelope (line 549-550)
        int frameBlocksLeft = 0;    // control blocks until the in-frame decay restarts
        // Per mode, padded to nPad_. The arrays the control loop streams are float.
        std::vector<float> Br, Bi;      // amplitude at the segment's reference time
        std::vector<float> D;           // its own decay since then, at the next control point
        std::vector<float> dmul, invDmul; // that decay over one control block, and its inverse
        std::vector<float> gm1;         // onset gain - 1
        std::vector<float> rc0, rsl;    // ramp = clamp(rc0 + rsl * t, 0, 1): bf, (1 - bf) / pt (1, 0: none)
        std::vector<double> epsK;       // wash_damp - oscWash_ (merged components differ per mode)
        std::vector<double> pt;         // ramp peak time (0 = none)
        // Envelope strikes only (allocated when the bake has an NPZ band envelope):
        std::vector<float> Er, Ei;      // |coupling| with this strike's phase
        std::vector<float> fd;          // in-frame decay at the next control point
        std::vector<float> em0, em1;    // band envelope of each mode at the block's ends
        std::vector<float> emax;        // largest band envelope of each mode (bound for dropping)
    };

    // A component of one mode whose onset ramp reaches 1 inside the current block.
    struct KinkMode
    {
        double yr, yi;  // oscillator x amplitude
        int sa;         // ringing slot of the mode (its oscillator step)
        double dps;     // the component's own decay per sample
        double pl, pr, gm1, pt, te0, env0, env1;
    };

    void renderContactSample(float* left, float* right) noexcept;
    void renderFree(float* left, float* right, int n) noexcept;
    void beginControlBlock() noexcept;
    void sumComponents(double t0, double t1) noexcept;
    void dropQuietModes() noexcept;
    void renderKinks(float* left, float* right, int j0, int m) noexcept;
    void addKink(const Component& c, int mode, double d0, double te0) noexcept;
    template <int G>
    void kernel(int m) noexcept;
    void rebaseComponents() noexcept;
    void multiplyAmplitudes(Component& c, const float* ur, const float* ui) noexcept;
    void multiplyAmplitudes(float* re, float* im, const float* ur, const float* ui) noexcept;
    Component& allocateComponent() noexcept;
    void mergeComponents(Component& into, Component& from, bool dropEnvelopes) noexcept;
    void setDecay(Component& c, int mode) noexcept;
    void startFreePhase() noexcept;
    void velocityParticipation(const VelocityTable& t, double v, double* out) const noexcept;
    void hertzVelocity(double velocity, double* out) const noexcept;
    void npzCoupling(double velocity, double* out) const noexcept;
    void startEnvelope(Component& c, double velocity) noexcept;
    double envelopeAt(const Component& c, int band, double te) const noexcept;
    void restartFrames(Component& c, double elapsed) noexcept;

    std::shared_ptr<const BakeData> bake_;
    const int n_;
    int nPad_ = 0; // n_ rounded up to the kernel's mode group
    double sr_ = 0.0;
    int K_ = 32;   // control block, samples

    // Engine clock: timeBase_ + clock_ / sr_ seconds. timeBase_ absorbs earlier rates.
    double timeBase_ = 0.0;
    uint64_t clock_ = 0;
    uint64_t strikesApplied_ = 0;
    uint64_t componentOrder_ = 0;
    Pcg64 rng_;

    // Total modal state (calibrated, double) during a contact phase, as in Engine.
    std::vector<double> zr_, zi_;
    std::vector<double> omega_, cosLp_;
    std::vector<double> stepR_, stepI_; // undamped per-sample step at the host rate (contact phase)
    std::vector<double> oscR_, oscI_;   // shared oscillator: per-sample step (decay T60 / oscWash_)
    std::vector<double> PKr_, PKi_;     // shared oscillator: step over one control block
    std::vector<double> negLambda_;     // -ln(1000) / T60 per mode (undamped decay rate, 1/s)
    double oscWash_ = 1.0;              // wash_damp of the shared oscillator's decay (the smallest a strike has)
    std::vector<double> hertzRef_;      // sqrt(1 + (pi f tau_ref)^2) per mode (hertz_vel_scale)

    // NPZ band envelope (bakes that have one): the shared oscillator has no decay
    // (oscWash_ = 0, every strike carries its whole decay), and the control block divides
    // the 20 ms frame of the in-frame decay into frameBlocks_ blocks.
    bool envBake_ = false;
    int frameBlocks_ = 0;
    std::vector<int> bandStart_, bandEnd_; // mode range of each envelope band
    std::vector<double> sqrtVelsNorm_;     // sqrt of the NPZ velocities normalised to 0..1 (line 1049)
    std::vector<double> bandVal0_, bandVal1_; // per band, scratch for one block

    // Contact force at the host rate (contactLen_ x numModes, DC-normalised per mode) and
    // its response H = sum_j f_j step^(len - j): the state a unit coupling leaves at the
    // end of a contact.
    std::vector<double> force_;
    std::vector<double> Hr_, Hi_;
    std::vector<float> H32r_, H32i_; // H in float (padded), for components
    int contactLen_ = 0;
    std::vector<Contact> contactPool_;
    int activeContacts_ = 0;

    std::vector<Component> components_;
    int64_t baseSample_ = 0; // engine sample the components' amplitudes refer to
    int64_t sampleNow_ = 0;  // engine sample being rendered (between process() calls: the next one)

    // Current segment.
    bool freeStarted_ = false;
    int64_t freePos_ = 0;
    int64_t freeStartSample_ = 0;
    double segTBase_ = 0.0; // absolute time of the first free-phase sample
    int lastSlot_ = -1;

    // Cymbal-wide state that Python keeps on the engine object.
    double wanderScale_ = 1.0, wanderRateScale_ = 1.0;
    double lfS_ = 0.0, lfTLast_ = 0.0, lfScaleNow_ = 1.0;
    double mfS_ = 0.0, mfTLast_ = 0.0, mfScaleNow_ = 1.0;
    std::vector<double> scratchBandScale_;

    // Segment state.
    bool segMfOn_ = false;
    double segThreshold_ = 0.0;
    bool wanderSmall_ = true; // the wander phase moves < 0.05 rad per block for every mode

    // Ringing modes of the free phase, compacted (nAct_ used, nPad_ allocated).
    int nAct_ = 0;
    std::vector<int> actIdx_; // mode of each ringing slot
    std::vector<int> actPos_; // ringing slot of each mode, -1 when silent
    // float oscillator and step; complex amplitude and its per-sample increment; pick
    std::vector<float> yr_, yi_, sr32_, si32_, ar_, ai_, dar_, dai_, pl32_, pr32_;
    // double control-rate state per active mode
    std::vector<double> Yr_, Yi_;   // oscillator at the next control point (wander included)
    std::vector<double> cr_, ci_;   // LFO phasor exp(i (2 pi lr t + lp)) at the next control point
    std::vector<double> qr_, qi_;   // LFO step over one control block
    std::vector<double> phiW_;      // wander phase at the next control point
    std::vector<double> negDlr_, mf_, gBound_;
    // the components' sum at the two ends of the block, per mode (mode order, nPad_)
    std::vector<float> A0r_, A0i_, A1r_, A1i_;
    std::vector<float> dropW_, dropE_; // dropQuietModes(): per-mode weight and energy (mode order)
    std::vector<float> rotR_, rotI_, sumR_, sumI_; // per-mode scratch of strikes (mode order)
    // components by how the control loop treats them (rebuilt every block)
    std::array<Component*, kMaxComponents> settledList_ {}, onsetList_ {}, rampList_ {}, envList_ {};

    std::vector<KinkMode> kinks_;
    int nKinks_ = 0;

    // Per-sample accumulators of one control block, one 4-lane vector per sample.
    std::vector<float> accL_, accR_;

    // Right-channel high-pass at the host rate (direct form II transposed, double).
    double hpB0_ = 0.0, hpB1_ = 0.0, hpB2_ = 0.0, hpA1_ = 0.0, hpA2_ = 0.0;
    double hpZ0_ = 0.0, hpZ1_ = 0.0;

    // Per-strike scratch (preallocated).
    std::vector<double> scratchPhase_, scratchVel_, scratchBellVel_, scratchPosW_, scratchPkT_;
};

} // namespace cymbalis
