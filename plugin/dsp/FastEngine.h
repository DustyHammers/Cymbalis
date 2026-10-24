// FastEngine.h — the real-time Cymbalis engine used by the plugin.
//
// It renders the model of Engine (the exact port of Python V19) with the same strike logic,
// segment structure, live-engine decisions and contact phase, with one deliberate departure
// that Kris asked for (docs/PORT_MAP.md, "Superposition in the plugin"):
//
//   Each strike's own effects apply only to the energy that strike added. The onset band
//   gains and their tau, the edge onset ramp with its velocity tilt, and the
//   velocity-dependent decay (wash_damp / vel_len_tilt) belong to the strike. What stays
//   cymbal-wide, as in Python: the shared modal state (strikes add together), the LF and
//   MF accumulators' scales, the LF accumulator's wander-rate boost, and the wander itself.
//   For a single strike on its own this is the same computation as before. During a
//   strike's contact phase the whole state is output with only the cymbal-wide LF/MF gain
//   and wander phase (PORT_MAP f.4), so for those samples older strikes' gains and ramps
//   pause.
//
//   The LF and MF accumulators decay continuously; their scales are evaluated at every
//   control point and smoothed (one-pole, 20 ms). The LF scale applies in both its modes.
//   Each mode's wander phase is integrated over time, so changes of the wander rate and
//   depth never make it jump (PORT_MAP, "Live-engine decisions", decision 6).
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
//     edge data (602, Bettis, Swish) take an edge strike's coupling from the NPZ velw tables
//     and, with a band envelope, drive that strike's level from it for the onset window
//     (PORT_MAP, "602 and Bettis", "Swish"). For those the shared oscillator carries no decay
//     and the control block is 1/7 or 1/8 of the envelope's 20 ms frame.
//   - Energy cascade (docs/TUNING.md; PORT_MAP, plugin mechanism 10), off at amount 0: at
//     every control point the ringing energy of each one-third-octave band is measured; a
//     band above its threshold gives energy to the bands above it. The source modes are
//     scaled down through the shared oscillator's gain (every strike in them alike); the
//     energy arrives in the target modes as a component of its own (cascade component) that
//     decays with each mode's T60. Both change over the block, with no step.
//   - Loud-band damping (plugin mechanism 11), off at amount 0: from the same measurement,
//     the modes of a band above its threshold decay faster, through the same oscillator gain.
//   - Re-hit damping (plugin mechanism 12), off at amount 0: a strike scales the strikes
//     already ringing down over a short ramp, a gain per component evaluated at the control
//     points; the new strike's own energy is not scaled.
//   - Stick click (_add_transient lines 1194-1211; Swish's bow tip), off at gain 0: a strike
//     starts a band-passed noise burst from the strike-phase random sequence, mixed into
//     both channels before the right-channel high-pass. It is the engine's only noise source.
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

    // Per-mode decay coefficients at one sample rate and control block: the decay rate
    // -ln(1000) / T60, the undamped per-sample step (contact phase), the shared oscillator's
    // per-sample and per-block steps (decay T60 / oscWash) and the contact's response H.
    // prepare() computes them; the live per-band ring lengths (Tuning window) compute them
    // off the audio thread (computeDecay) and hand them to setTuning.
    struct DecaySet
    {
        double sampleRate = 0.0;
        int controlBlock = 0;
        std::vector<double> negLambda, stepR, stepI, oscR, oscI, PKr, PKi, Hr, Hi;
        std::vector<float> H32r, H32i; // H in float, padded
    };
    // The control block at a rate (and the blocks per 20 ms frame with a band envelope).
    static int controlBlockFor(const BakeData& b, double sampleRate, int* frameBlocks = nullptr) noexcept;
    // The shared oscillator's wash_damp (0 with a band envelope).
    static double oscillatorWash(const BakeData& b, const EngineParams& p) noexcept;
    // The contact force at a rate, len x numModes. Allocates.
    static void contactForce(const BakeData& b, const EngineParams& p, double sampleRate, std::vector<double>& force,
                             int& len);
    // Every coefficient from each mode's decay rate (negLambda). Allocates only when `out`
    // does not have the bake's sizes.
    static void computeDecay(const BakeData& b, double sampleRate, int K, double oscWash, const double* negLambda,
                             const std::vector<double>& force, int contactLen, DecaySet& out);

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

#if CYMBALIS_TUNING
    // Tuning code only (CYMBALIS_TUNING, docs/TUNING.md): the live tuning values, every one but the ring
    // length (which needs a new bake: applyTuning). Per-strike values apply from the next
    // strike; the accumulators' values from the next control point, smoothed; wander depth
    // and rate at the next control point without a phase jump; the output gain smoothed.
    // modeGains holds every slot's per-mode excitation gains (slot s at s x numModes, from
    // excitationModeGains), precomputed off the audio thread; with nullptr they are computed
    // here from the band gains of `t`. A change of the per-band ring lengths takes `decay`,
    // computed off the audio thread for this engine's rate and control block (else it is
    // computed here), and applies it at the next control point: the ringing strikes decay
    // at the new rates from there, with no step. gainL and gainR are every mode's output
    // gains for the stereo values of `t` (stereoGains), computed off the audio thread;
    // nullptr: the bake's (BakeData::gainL, gainR). New gains go from one value to the other
    // over the next control block of the free phase (a change during a contact waits for its
    // end); while nothing rings they apply at once. The right-channel high-pass moves to new
    // settings over 20 ms. Does not allocate.
    void setTuning(const Tuning& t, const float* modeGains, const DecaySet* decay = nullptr,
                   const double* gainL = nullptr, const double* gainR = nullptr) noexcept;
    // When each one-third-octave band of the slot's last strike peaks, in ms after the strike
    // (-1: the band has no energy in it, or the slot has not been struck): per mode the end of
    // its onset ramp, or with a band envelope when its envelope band peaks (after the onset
    // shift), else 0; per band the mean over its modes, weighted by their energy at the strike.
    const std::array<float, kExcitationBands>& onsetPeaksMs(int slot) const noexcept { return onsetPeakMs_[size_t(slot)]; }
#endif

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
        bool cascade = false;     // it holds the energy cascade's moved energy and nothing else
        uint64_t order = 0;       // creation order, for "oldest"
        int64_t contactStart = 0; // engine sample where its contact began
        double eps = 0.0;         // decay relative to the oscillator's (wash_damp - oscWash_), energy-weighted
        double tStrike = 0.0;     // engine time of the strike
        bool onsetOn = false;     // attack band gains (its articulation's; not blended to the edge)
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
        double envWindow = 1.5;     // its envelope window (edge_band_env_onset_s)
        bool frameRestart = true;   // its in-frame decay restarts every frame (else: none)
        // Its articulation's onset shift at the strike (docs/TUNING.md): band j's envelope
        // is read at (t - envDelay[j]) x envSpeed[j] (seconds). Only with envShift.
        bool envShift = false;
        std::array<float, kExcitationBands> envDelay {}, envSpeed {};
        // Per mode, padded to nPad_. The arrays the control loop streams are float.
        std::vector<float> Br, Bi;      // amplitude at the segment's reference time
        std::vector<float> D;           // its own decay since then, at the next control point
        std::vector<float> dmul, invDmul; // that decay over one control block, and its inverse
        std::vector<float> gm1;         // onset gain - 1
        std::vector<float> rc0, rsl;    // ramp = clamp(rc0 + rsl * t, 0, 1): bf, (1 - bf) / pt (1, 0: none)
        std::vector<double> epsK;       // wash_damp - oscWash_ (merged components differ per mode)
        std::vector<double> pt;         // ramp peak time (0 = none)
        // Re-hit damping: a later strike scales this one by rhoA + (rhoB - rhoA) x
        // smoothstep((t - rhoT0) / rhoT) (rhoT0 < 0: from the start of the next free phase),
        // rho0 and rho1 at the current block's ends. Off: 1.
        bool rhoOn = false;
        double rhoA = 1.0, rhoB = 1.0, rhoT0 = 0.0, rhoT = 0.01;
        float rho0 = 1.0f, rho1 = 1.0f;
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
        double dpl, dpr; // the output gains' step per sample during a gain ramp, else 0
        double g0, g1;  // cymbal-wide LF/MF gain at the block's ends
    };

    void renderContactSample(float* left, float* right) noexcept;
    void renderContactRotated(float* left, float* right) noexcept;
    void endContactSample() noexcept;
    void renderFree(float* left, float* right, int n) noexcept;
    void beginControlBlock() noexcept;
    void sumComponents(double t0, double t1) noexcept;
    template <bool Rho>
    void sumBlock(double t0, double t1, int nS, int nO, int nR, int nE) noexcept;
    double rhoAt(const Component& c, double t) const noexcept;
    void dropQuietModes() noexcept;
    void renderKinks(float* left, float* right, int j0, int m) noexcept;
    void addKink(const Component& c, int mode, double d0, double te0) noexcept;
    template <int G, bool Ramp>
    void kernel(int m) noexcept;
    void rebaseComponents() noexcept;
    void multiplyAmplitudes(Component& c, const float* ur, const float* ui) noexcept;
    void multiplyAmplitudes(float* re, float* im, const float* ur, const float* ui) noexcept;
    Component& allocateComponent() noexcept;
    void mergeComponents(Component& into, Component& from, bool dropEnvelopes) noexcept;
    void setDecay(Component& c, int mode) noexcept;
    void startFreePhase(bool sameGrid = false) noexcept;
    void takeDecay(DecaySet& d) noexcept;
    void applyDecay() noexcept;
    double wanderRate(double t) const noexcept;
    void smoothScalesTo(double t, double tTarget) noexcept;
    void smoothScalesTo(double t) noexcept { smoothScalesTo(t, t); }
    // Cymbal-wide LF x MF gain of one mode for the scales lf and mf (lines 914-919).
    double modeGain(size_t mode, double lf, double mf) const noexcept
    {
        return (1.0 + (lf - 1.0) * lfW_[mode]) * (1.0 + (mf - 1.0) * mfW_[mode]);
    }
    void cymbalWideAtStrike(double t) noexcept;
    void computeLfWeights() noexcept;
    void computeMfWeights() noexcept;
    void computeWanderTables() noexcept;
    void computeHertzRef() noexcept;
    void computeRemapTable() noexcept;
    void computeAttack(size_t slot) noexcept;
    void setMechanisms(const CymbalTuning& c) noexcept;
    void updateMechanisms() noexcept;
    void computeBandRef() noexcept;
    void bandBlock() noexcept;
    void startRehit(const Component& struck, int slot, double velocity) noexcept;
    Component* cascadeComponent() noexcept;
    void commitBlockGains() noexcept;
    void clearBlockGains() noexcept;
    void renormOscillators() noexcept;
    void applyPendingWander(double t) noexcept;
    void updateGainBounds() noexcept;
    // The largest output gain of a mode: of the gains in use and, during a ramp, its target.
    double outputBound(size_t mode) const noexcept;
    // Output gain ramps (stereo, setTuning): at a control point of the free phase, the end of
    // the last block's ramp and the start of a pending one; at a strike, the gains where the
    // ramp is and the rest of it pending.
    void endGainRamp() noexcept;
    void startGainRamp() noexcept;
    void interruptGainRamp() noexcept;
    // The right-channel high-pass coefficients at a frequency and this rate, and a move to
    // the current settings (hpOn_, hpHz_) over kHpRampS.
    void highPassTargets(double hz, double* coef) const noexcept;
    void startHighPassRamp() noexcept;
    void velocityParticipation(const VelocityTable& t, double v, double* out) const noexcept;
    void hertzVelocity(double velocity, double* out) const noexcept;
    void npzCoupling(double velocity, double* out) const noexcept;
    void startEnvelope(Component& c, double velocity) noexcept;
    void startStick(int slot, double velocity, double position, double level) noexcept;
#if CYMBALIS_TUNING
    void onsetPeaks(const Component& c, int slot) noexcept;
#endif
    void renderStick(float* left, float* right, int n) noexcept;
    double envelopeAt(const Component& c, int band, double te) const noexcept;
    void restartFrames(Component& c, double elapsed) noexcept;

    std::shared_ptr<const BakeData> bake_;
    const int n_;

    // Values the plugin tuning can change (docs/TUNING.md): copies of the bake's, which the
    // engine reads instead of the bake. Only the live tuning changes them (setTuning).
    EngineParams params_;
    std::vector<double> slotTrim_;              // per slot: level trim (gain)
    std::vector<ArticulationTuning> slotCurve_; // per slot: velocity curve
    // Per slot: excitation gain of every mode (slot s at s x n_, from its band gains), and
    // whether any of them is not 1.
    std::vector<float> excGain_;
    std::vector<uint8_t> excGainOn_;
    // Each mode's one-third-octave band (excitationBand), and per slot whether its onset
    // shift (slotCurve_[s].delayMs, .speed) is not the identity.
    std::vector<uint8_t> modeBand_;
    std::vector<uint8_t> onsetShiftOn_;
    // Per slot: its attack band gains (docs/TUNING.md) as gain - 1 per band (0 within 1e-3 of
    // 0 dB; slot s at s x kExcitationBands), the largest |gain - 1| over the bands that have
    // modes, whether any is not 0, and the attack time constant (s).
    std::vector<double> attackGm1_, attackGm1Max_, attackTauS_;
    std::vector<uint8_t> attackOn_;
    // Per slot: whether the SOFT and HARD velocity layers differ, of its onset shift and of
    // its attack gains (docs/TUNING.md). Then a strike takes each band between them at its
    // velocity (strike*_ hold that strike's values); else the slot's values above.
    std::vector<uint8_t> shiftLayers_, attackLayers_;
    std::array<double, kExcitationBands> strikeDelay_ {}, strikeSpeed_ {}, strikeAttackDb_ {}, strikeGm1_ {};
#if CYMBALIS_TUNING
    std::vector<std::array<float, kExcitationBands>> onsetPeakMs_; // per slot (onsetPeaksMs)
#endif
    std::array<bool, kExcitationBands> bandHasModes_ {};
    // Brightness vs velocity: ln(f / 1 kHz) per mode.
    std::vector<double> logF1k_;
    double remapExp_ = 0.5;    // NPZ velocity remap exponent (0.5: sqrt, line 1049)
    double envWindowS_ = 1.5;  // NPZ band envelope window after an edge strike
    bool frameRestart_ = true; // the in-frame decay restarts every 20 ms
    double outGain_ = 1.0, outGainTarget_ = 1.0, outGainCoef_ = 1.0; // output gain, smoothed per sample
    // Wander changes from setTuning wait for the next control point.
    bool wanderPending_ = false;
    double pendingWanderDepth_ = 0.0, pendingWanderRateHz_ = 0.0;
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
    // Per-band ring lengths (docs/TUNING.md): the band factors negLambda_ holds, the ring
    // length the engine was built with, and new coefficients waiting for a control point.
    std::array<double, kExcitationBands> ringBandNow_ = kUnitBands;
    double ringLengthBuilt_ = 1.0;
    DecaySet pendDecay_;
    bool decayPending_ = false;
    double oscWash_ = 1.0;              // wash_damp of the shared oscillator's decay (the smallest a strike has)

    // Energy cascade (docs/TUNING.md). cascOn_: amount > 0 (otherwise none of it runs).
    bool cascOn_ = false;
    double cascAmount_ = 0.0, cascThrDb_ = -12.0, cascSpeedS_ = 0.1, cascReleaseS_ = 0.15;
    int cascSpread_ = 2, cascLo_ = 0, cascHi_ = kExcitationBands - 1;
    double cascAtk_ = 0.0, cascRel_ = 0.0;   // activation one-pole coefficients per control block
    int cascSlot_ = 0;                       // the BOW articulation (the threshold's reference)
    int cascComp_ = -1;                      // the cascade component (index), -1: none
    std::array<double, kExcitationBands> bandRef_ {};  // band energy of a velocity-1 BOW strike (also the damping's reference)
    std::array<double, kExcitationBands> cascThr_ {};  // threshold energy per band
    std::array<double, kExcitationBands> cascAct_ {};  // activation per band (0 .. 1)
    std::vector<float> cascW_;               // per mode: its share weight of the moved energy (exc_norm)
    std::vector<double> modeE_;              // per ringing mode: its energy at this block's end (scratch)

    // Loud-band damping (docs/TUNING.md). dampOn_: amount > 0.
    bool dampOn_ = false;
    double dampAmount_ = 0.0, dampThrDb_ = -12.0, dampTilt_ = 0.0, dampCoef_ = 0.0;
    std::array<double, kExcitationBands> dampThr_ {};  // threshold energy per band
    std::array<double, kExcitationBands> dampX_ {};    // how far above it (0 .. 1), smoothed
    std::array<double, kExcitationBands> dampTiltW_ {}; // tilt weight per band (0 .. 2)

    // Re-hit damping (docs/TUNING.md). rehitOn_: amount > 0. rhoAny_: some component has a
    // ramp (the control loop then applies the components' gains).
    bool rehitOn_ = false, rhoAny_ = false;
    double rehitAmount_ = 0.0, rehitTimeS_ = 0.01;
    std::array<double, kMaxArticulations> rehitW_ {};
    // Gains of the ringing modes through the shared oscillator (mode order): this block's,
    // reaching its value at the block's end, and the product of the earlier blocks' since the
    // segment started (with renormOscillators()' factors); and additions to the cascade
    // component's amplitudes at this block's end. The next control point puts them into the state (commitBlockGains); a strike
    // inside the block takes the part already rendered.
    std::vector<float> blkG_, injR_, injI_;
    std::vector<double> oscG_;
    bool blkGainOn_ = false, oscGainOn_ = false, injOn_ = false;
    std::vector<double> hertzRef_;      // sqrt(1 + (pi f tau_ref)^2) per mode (hertz_vel_scale)

    // NPZ band envelope (bakes with NPZ edge data, whether or not the envelope is on: the
    // tuning can switch it): the shared oscillator has no decay (oscWash_ = 0, every strike
    // carries its whole decay), and the control block divides the 20 ms frame of the
    // in-frame decay into frameBlocks_ blocks. envOn_: edge strikes follow the envelope
    // (edge_npz.envelope_active; EDGE ONSET ENVELOPE in the Tuning window).
    bool envBake_ = false, envOn_ = false;
    int frameBlocks_ = 0;
    std::vector<int> bandStart_, bandEnd_; // mode range of each envelope band
    std::vector<uint8_t> npzShiftBand_;    // the onset-shift band of each envelope band
    std::vector<double> remapTable_;       // the NPZ velocities normalised to 0..1, ^ remapExp_ (line 1049)
    std::vector<double> bandVal0_, bandVal1_; // per band, scratch for one block
    std::vector<double> envPeakS_;            // per band: when the latest envelope strike's envelope peaks (s)

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
    double wanderScale_ = 1.0;   // wander depth scale set by the latest strike (edge_wander_depth)
    double edgeRateScale_ = 1.0; // wander rate scale set by the latest strike (edge_wander_rate)
    std::vector<double> scratchBandScale_;

    // LF and MF accumulators (render() lines 652-688), decaying continuously:
    // S(t) = s0 exp(-(t - t0) / tau), t0 the latest strike. Its scale follows the part
    // without that strike's own contribution (`latest`), which enters at the next strike, as
    // in Python. `started` is the explicit "there was a strike" flag (Python tests
    // t_last > 0, f.8).
    struct Accumulator
    {
        double s0 = 0.0, latest = 0.0, t0 = 0.0;
        bool started = false;
        double at(double t, double tau) const noexcept;       // all of S
        double forScale(double t, double tau) const noexcept; // S without the latest strike
        void add(double t, double tau, double amount) noexcept;
    };
    Accumulator lfAcc_, mfAcc_;
    bool lfOn_ = false, mfOn_ = false; // lf_sens_depth > 0, mf_sens_depth > 0
    std::vector<double> lfW_, mfW_;    // per mode: LF weight (1 - sigmoid), MF band-pass weight; 0 when off
    // Their scales after the 20 ms smoothing, at time smTime_, and at the current control
    // block's ends (the gains are interpolated between them).
    double lfSm_ = 1.0, mfSm_ = 1.0, smTime_ = 0.0;
    double blkT0_ = 0.0, blkT1_ = 0.0, blkLf0_ = 1.0, blkLf1_ = 1.0, blkMf0_ = 1.0, blkMf1_ = 1.0;

    // Wander, integrated over time. The LFO phase of mode k is 2 pi lfoUnit_k tau + lfoPhase_k,
    // where tau, the LFO time, rises at wander_rate_hz x the wander rate scale: tauRef_ at
    // tauRefTime_, then tauRate_ per second. Its wander phase W changes by
    // wC_k (wanderScale_ / tauRate_) times the change of cos(LFO phase), which is line 867
    // while the rate and depth stay constant. W_ also holds a mode's phase when wander is off.
    bool wanderOn_ = false;
    double tauRef_ = 0.0, tauRefTime_ = 0.0, tauRate_ = 1.0;
    double blkRate_ = -1.0;   // rate scale of the current control block (-1: none yet)
    double maxDriftDepth_ = 0.0; // largest |drift x wander_depth| of a mode with wander
    std::vector<double> lfoUnit_; // per mode: LFO rate / wander_rate_hz (uniform 0.7..1.3, line 210)
    std::vector<double> wC_;  // per mode: -drift x wander_depth / lfoUnit (0: no wander)
    std::vector<double> W_, cW_; // per mode: wander phase at the latest strike, and cos of its LFO phase then
    // Contact phase: the cymbal-wide gain x exp(i W) of each mode at the latest strike,
    // times each output gain (off when the bake has no LF/MF accumulator and no wander).
    bool contactRot_ = false;
    std::vector<double> cuLr_, cuLi_, cuRr_, cuRi_;

    // Output gains per mode (mode order; stereo, docs/TUNING.md): in use (gainL_, gainR_),
    // waiting for the next control point of the free phase (pend*), and the target of the
    // current block's ramp (ramp*). The float copies of the ringing modes (pl32_, pr32_)
    // then move by dpl32_, dpr32_ per sample.
    std::vector<double> gainL_, gainR_, pendL_, pendR_, rampL_, rampR_;
    bool gainPending_ = false, gainRamp_ = false;

    // Segment state.
    double segThreshold_ = 0.0;
    double segGm1Max_ = 0.0; // largest onset gain - 1 of the segment's components
    bool wanderSmall_ = true; // the wander phase moves < 0.05 rad per block for every mode

    // Ringing modes of the free phase, compacted (nAct_ used, nPad_ allocated).
    int nAct_ = 0;
    std::vector<int> actIdx_; // mode of each ringing slot
    std::vector<int> actPos_; // ringing slot of each mode, -1 when silent
    // float oscillator and step; complex amplitude and its per-sample increment; output gains
    // and their step per sample during a gain ramp
    std::vector<float> yr_, yi_, sr32_, si32_, ar_, ai_, dar_, dai_, pl32_, pr32_, dpl32_, dpr32_;
    // double control-rate state per active mode
    std::vector<double> Yr_, Yi_;   // oscillator at the next control point (wander included)
    std::vector<double> cr_, ci_;   // LFO phasor at the next control point
    std::vector<double> qr_, qi_;   // LFO step over one control block (at blkRate_)
    std::vector<double> phiW0_, phiW_; // wander phase at the current block's start and at the next control point
    std::vector<double> gBound_;    // bound of the mode's output gains (dropping)
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

    // Right-channel high-pass at the host rate (direct form II transposed, double): on or off
    // and its frequency (the tuning's), its coefficients in use (b0 b1 b2 a1 a2) and how much
    // of its output is used (1: on, 0: off). A change moves both linearly over hpRampLeft_
    // samples (hpStep_ per sample) to hpTarget_.
    bool hpOn_ = true;
    double hpHz_ = 120.0;
    double hpCoef_[5] = {}, hpStep_[5] = {}, hpTarget_[5] = {};
    double hpMix_ = 1.0, hpMixStep_ = 0.0;
    int hpRampLeft_ = 0;
    double hpZ0_ = 0.0, hpZ1_ = 0.0;

    // Stick click voices (startStick): each plays one burst, drawn at its strike into its
    // part of stickNoise_ (stickMaxLen_ samples each), through its own band-pass (order-2
    // Butterworth as two sections b0 (1 - z^-2) / (1 + a1 z^-1 + a2 z^-2), direct form II
    // transposed), times its gain. It ends when its burst is over and its filter has rung down.
    struct StickVoice
    {
        bool on = false;
        uint64_t order = 0;
        int k = 0, len = 0;  // next sample of the burst, its length
        double g = 0.0;      // gain x position weight x velocity^1.7 x level
        double b0[2] {}, a1[2] {}, a2[2] {};
        double s1[2] {}, s2[2] {};
        float* noise = nullptr;
    };
    static constexpr int kStickVoices = 8;
    std::array<StickVoice, kStickVoices> stick_ {};
    std::vector<float> stickNoise_;
    int stickMaxLen_ = 0, stickActive_ = 0;
    uint64_t stickOrder_ = 0;

    // Per-strike scratch (preallocated).
    std::vector<double> scratchPhase_, scratchVel_, scratchBellVel_, scratchPosW_, scratchPkT_;
};

} // namespace cymbalis
