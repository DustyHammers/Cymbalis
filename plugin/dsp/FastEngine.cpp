#include "FastEngine.h"
#include "EngineProfile.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <complex>
#include <cstring>

namespace cymbalis
{

namespace
{
// 128-bit vectors (GCC/Clang vector extension): NEON on arm64, SSE on x86_64. No AVX.
typedef float v4f __attribute__((vector_size(16)));
typedef double v2d __attribute__((vector_size(16)));
typedef int32_t v4i __attribute__((vector_size(16)));

inline v4f ld(const float* p) noexcept
{
    v4f v;
    std::memcpy(&v, p, sizeof v);
    return v;
}

inline void st(float* p, v4f v) noexcept { std::memcpy(p, &v, sizeof v); }

inline v4f splat(float x) noexcept { return v4f {x, x, x, x}; }

inline v4f select(v4i mask, v4f a, v4f b) noexcept { return (v4f) (((v4i) a & mask) | ((v4i) b & ~mask)); }
inline v4f vclamp01(v4f x) noexcept
{
    const v4f zero = splat(0.0f), one = splat(1.0f);
    return select(x < zero, zero, select(x > one, one, x));
}

inline v2d ld2(const double* p) noexcept
{
    v2d v;
    std::memcpy(&v, p, sizeof v);
    return v;
}

inline void st2(double* p, v2d v) noexcept { std::memcpy(p, &v, sizeof v); }

constexpr int kGroup = 16;            // nPad_ is a multiple of this (and of every kernel group)
constexpr double kHpCutoffHz = 120.0; // render() line 703: _hp(out[:,1], 120), 2nd-order Butterworth
constexpr int kDropEvery = 4;         // control blocks between checks for quiet modes and components
constexpr double kSmallAngle = 0.05;  // the series below are used for wander steps under this (rad)
constexpr double kSettled = 1e-6;     // an onset envelope below this (times its largest gain) is over
constexpr double kWashDampFloor = 0.3; // line 1129: wash_damp = max(0.3, ...)
constexpr double kScaleSmoothS = 0.02;  // one-pole smoothing of the LF and MF scales
constexpr double kOutputGainSmoothS = 0.02; // one-pole smoothing of the output gain
constexpr double kHpRampS = 0.02;       // a change of the right-channel high-pass moves over this
constexpr double kLfoSeriesMax = 0.25;  // cosSinSeries() is exact in double below this (rad)
constexpr int kCascadeMaxSpread = 6;   // the energy cascade's farthest target band
// Share of the moved energy of a target band d bands above the source (halving per band),
// before normalising over the targets there are.
constexpr double kCascadeShare[kCascadeMaxSpread + 1] = {0.0, 1.0, 0.5, 0.25, 0.125, 0.0625, 0.03125};

// Vectors of 4 modes per kernel group: enough independent oscillators to hide the
// multiply latency without running out of registers (16 SSE registers, 32 NEON).
#if defined(__aarch64__) || defined(__arm64__)
constexpr int kVecPerGroup = 2;
#else
constexpr int kVecPerGroup = 1;
#endif

double clamp01(double x) { return std::min(std::max(x, 0.0), 1.0); }

int roundUp(int x, int m) { return (x + m - 1) / m * m; }

// Same as Engine.cpp: np.interp for a scalar x, and numpy `arr ** p` for a scalar exponent.
double numpyInterp(double x, const double* xp, const double* fp, int len, int stride)
{
    if (x > xp[len - 1]) return fp[(len - 1) * stride];
    if (x < xp[0]) return fp[0];
    const int j = static_cast<int>(std::upper_bound(xp, xp + len, x) - xp) - 1;
    if (j == len - 1) return fp[j * stride];
    if (xp[j] == x) return fp[j * stride];
    const double slope = (fp[(j + 1) * stride] - fp[j * stride]) / (xp[j + 1] - xp[j]);
    return slope * (x - xp[j]) + fp[j * stride];
}

double numpyScalarPower(double x, double p)
{
    if (p == 1.0) return x;
    if (p == 2.0) return x * x;
    if (p == 0.5) return std::sqrt(x);
    if (p == -1.0) return 1.0 / x;
    if (p == 0.0) return 1.0;
    return std::pow(x, p);
}

// Bilinear-transform Butterworth high-pass, order 2, as scipy butter(2, fc/(SR/2), 'high').
void butterworthHighPass2(double fc, double sr, double& b0, double& b1, double& b2, double& a1, double& a2)
{
    const double k = std::tan(3.14159265358979323846 * fc / sr);
    const double norm = 1.0 / (1.0 + std::sqrt(2.0) * k + k * k);
    b0 = norm;
    b1 = -2.0 * norm;
    b2 = norm;
    a1 = 2.0 * (k * k - 1.0) * norm;
    a2 = (1.0 - std::sqrt(2.0) * k + k * k) * norm;
}

// Bilinear-transform Butterworth band-pass, order 2, as scipy butter(2, [lo, hi] / (SR/2),
// 'band') (dsp_filters._bp): the analog prototype's poles moved to the band (lp2bp_zpk) and
// mapped with the bilinear transform at prewarped edges (bilinear_zpk). Its four poles are
// two conjugate pairs and its zeros +1, +1, -1, -1, so it is two sections
// b0 (1 - z^-2) / (1 + a1 z^-1 + a2 z^-2), the whole gain in the first. Does not allocate.
void butterworthBandPass2(double lo, double hi, double sr, double b0[2], double a1[2], double a2[2]) noexcept
{
    using C = std::complex<double>;
    const double pi = 3.14159265358979323846;
    const double w1 = 4.0 * std::tan(pi * lo / sr), w2 = 4.0 * std::tan(pi * hi / sr);
    const double bw = w2 - w1, wo2 = w1 * w2;
    const C pl = C(-std::sqrt(0.5), std::sqrt(0.5)) * (bw / 2.0); // prototype pole x bw / 2
    const C r = std::sqrt(pl * pl - wo2);
    const C P[2] = {pl + r, pl - r}; // the other two are their conjugates
    double den = 1.0;
    for (const C& q : P)
        den *= std::norm(4.0 - q);
    const double k = bw * bw * 16.0 / den;
    for (int s = 0; s < 2; ++s)
    {
        const C z = (4.0 + P[s]) / (4.0 - P[s]);
        a1[s] = -2.0 * z.real();
        a2[s] = std::norm(z);
        b0[s] = s == 0 ? k : 1.0;
    }
}

// (r + i im)^m by squaring.
void complexPower(double r, double im, int64_t m, double& outR, double& outI)
{
    double ar = 1.0, ai = 0.0;
    while (m > 0)
    {
        if (m & 1)
        {
            const double t = ar * r - ai * im;
            ai = ar * im + ai * r;
            ar = t;
        }
        const double t = r * r - im * im;
        im = 2.0 * r * im;
        r = t;
        m >>= 1;
    }
    outR = ar;
    outI = ai;
}

// cos and sin of a small angle by their series to x^11 (error below 1e-16 for |x| <= 0.25),
// std::cos / std::sin above that.
inline void cosSin(double x, double& c, double& s) noexcept
{
    if (std::fabs(x) <= kLfoSeriesMax)
    {
        const double x2 = x * x;
        c = 1.0 - x2 * (0.5 - x2 * (1.0 / 24.0 - x2 * (1.0 / 720.0 - x2 * (1.0 / 40320.0 - x2 * (1.0 / 3628800.0)))));
        s = x * (1.0 - x2 * (1.0 / 6.0 - x2 * (1.0 / 120.0 - x2 * (1.0 / 5040.0 - x2 * (1.0 / 362880.0 - x2 * (1.0 / 39916800.0))))));
    }
    else
    {
        c = std::cos(x);
        s = std::sin(x);
    }
}

// hypot(x, y) < t, with hypot only where it can decide: x^2 + y^2 against t^2 carries a
// relative error of a few 1e-16, so outside t^2 (1 +- 1e-12) it gives hypot's answer.
struct BelowHypot
{
    explicit BelowHypot(double t) noexcept : t(t), lo(t * t * (1.0 - 1e-12)), hi(t * t * (1.0 + 1e-12)), squares(t > 1e-150) {}
    bool operator()(double x, double y) const noexcept
    {
        if (squares)
        {
            const double r2 = x * x + y * y;
            if (r2 < lo)
                return true;
            if (r2 > hi)
                return false;
        }
        return std::hypot(x, y) < t;
    }
    double t, lo, hi;
    bool squares; // t^2 is a normal number
};

// The ramp of lines 896-901: min(1, bf + (1 - bf) * te / pt), then max(., 0).
inline double ramp(double bf, double te, double invPt) noexcept
{
    return std::max(std::min(1.0, bf + (1.0 - bf) * te * invPt), 0.0);
}
} // namespace

FastEngine::FastEngine(std::shared_ptr<const BakeData> bake)
    : bake_(std::move(bake)), n_(bake_->numModes), params_(bake_->params)
{
    const BakeData& b = *bake_;
    const EngineParams& p = params_;
    if (p.contactPermode && (p.tipTau0 != p.shldTau0 || p.tipTauMax != p.shldTauMax))
        throw BakeError("FastEngine: the contact length depends on the strike position (tip and shoulder taus "
                        "differ); only a position-independent contact is implemented");

    // The 44.1 kHz high-pass of the bake must be what butterworthHighPass2 computes.
    {
        double b0, b1, b2, a1, a2;
        butterworthHighPass2(kHpCutoffHz, b.sampleRate, b0, b1, b2, a1, a2);
        const double mine[6] = {b0, b1, b2, 1.0, a1, a2};
        for (int i = 0; i < 6; ++i)
            if (std::fabs(mine[i] - b.hpSos[i]) > 1e-12)
                throw BakeError("FastEngine: the bake's high-pass is not a 2nd-order 120 Hz Butterworth");
    }

    const size_t n = size_t(n_);
    nPad_ = roundUp(n_, kGroup);
    const size_t np = size_t(nPad_);
    zr_.assign(n, 0.0);
    zi_.assign(n, 0.0);
    omega_.resize(n);
    cosLp_.resize(n);
    negLambda_.resize(n);
    for (size_t i = 0; i < n; ++i)
    {
        omega_[i] = b.twoPi * b.freqHz[i];
        cosLp_[i] = std::cos(b.lfoPhase[i]);
        negLambda_[i] = -b.log1000 / b.t60S[i];
    }
    scratchBandScale_.assign(size_t(std::max(b.velTiltBands, 1)), 1.0);

    // Per-slot and NPZ values the tuning can change (docs/TUNING.md), and the output gain.
    for (const auto& slot : b.slots)
    {
        slotTrim_.push_back(slot.levelTrim);
        slotCurve_.push_back(slot.articulation);
    }
    // Each slot's excitation band gains, per mode.
    excGain_.assign(b.slots.size() * n, 1.0f);
    excGainOn_.assign(b.slots.size(), 0);
    for (size_t s = 0; s < b.slots.size(); ++s)
        if (hasBandGains(b.slots[s].articulation))
        {
            excitationModeGains(b.slots[s].articulation, b.freqHz.data(), n_, excGain_.data() + s * n);
            excGainOn_[s] = 1;
        }
    // Each mode's band and each slot's onset shift (delay, speed per band).
    modeBand_.resize(n);
    for (size_t i = 0; i < n; ++i)
        modeBand_[i] = static_cast<uint8_t>(excitationBand(b.freqHz[i]));
    onsetShiftOn_.assign(b.slots.size(), 0);
    shiftLayers_.assign(b.slots.size(), 0);
    attackLayers_.assign(b.slots.size(), 0);
    for (size_t s = 0; s < b.slots.size(); ++s)
    {
        onsetShiftOn_[s] = hasOnsetShift(b.slots[s].articulation) ? 1 : 0;
        shiftLayers_[s] = hasShiftLayers(b.slots[s].articulation) ? 1 : 0;
        attackLayers_[s] = hasAttackLayers(b.slots[s].articulation) ? 1 : 0;
    }
#if CYMBALIS_TUNING
    onsetPeakMs_.assign(b.slots.size(), {});
    for (auto& a : onsetPeakMs_)
        a.fill(-1.0f);
#endif
    // Each slot's attack band gains, and each mode's distance from 1 kHz in log frequency.
    for (size_t i = 0; i < n; ++i)
        bandHasModes_[modeBand_[i]] = true;
    attackGm1_.assign(b.slots.size() * size_t(kExcitationBands), 0.0);
    attackGm1Max_.assign(b.slots.size(), 0.0);
    attackTauS_.assign(b.slots.size(), 0.08);
    attackOn_.assign(b.slots.size(), 0);
    for (size_t s = 0; s < b.slots.size(); ++s)
        computeAttack(s);
    logF1k_.resize(n);
    for (size_t i = 0; i < n; ++i)
        logF1k_[i] = std::log(b.freqHz[i] / 1000.0);
    ringBandNow_ = b.tuning.cymbal.ringBand;
    ringLengthBuilt_ = b.tuning.cymbal.ringLength;
    remapExp_ = b.edgeNpz.remapExponent;
    envWindowS_ = b.edgeNpz.onsetS;
    frameRestart_ = b.edgeNpz.frameDecayRestart;
    outGain_ = outGainTarget_ = std::pow(10.0, b.outputGainDb / 20.0);

    // LF and MF accumulators: each mode's weight in their scales (lines 391-405), from the
    // bake where it has them. The LF scale applies in both modes:
    // lf + (1 - lf) sigmoid = 1 + (lf - 1)(1 - sigmoid).
    lfOn_ = p.lfDepth > 0.0;
    mfOn_ = p.mfDepth > 0.0;
    lfW_.assign(n, 0.0);
    mfW_.assign(n, 0.0);
    if (! b.lfSigmoid.empty())
        for (size_t i = 0; i < n; ++i)
            lfW_[i] = 1.0 - b.lfSigmoid[i];
    else
        computeLfWeights();
    if (! b.mfBandpass.empty())
        mfW_ = b.mfBandpass;
    else
        computeMfWeights();
    // Wander (lines 864-867): D / lr = (drift x wander_depth / lfoUnit) x (depth scale /
    // (wander_rate_hz x rate scale)).
    lfoUnit_.assign(n, 0.0);
    if (p.wanderRateHz > 0.0)
        for (size_t i = 0; i < n; ++i)
            lfoUnit_[i] = b.lfoRateHz[i] / p.wanderRateHz;
    wC_.assign(n, 0.0);
    W_.assign(n, 0.0);
    cW_ = cosLp_; // at t = 0 the LFO phase is lfoPhase and the wander phase 0, as in Python
    computeWanderTables();
    tauRate_ = p.wanderRateHz;
    contactRot_ = lfOn_ || mfOn_ || wanderOn_;
    // Output gains: the bake's (the pick vectors, or its stereo gains).
    gainL_ = b.gainL.size() == n ? b.gainL : b.pickL;
    gainR_ = b.gainR.size() == n ? b.gainR : b.pickR;
    for (auto* v : {&pendL_, &pendR_, &rampL_, &rampR_})
        v->assign(n, 0.0);
    hpOn_ = b.tuning.cymbal.highPassOn;
    hpHz_ = b.tuning.cymbal.highPassHz;
    hpMix_ = hpOn_ ? 1.0 : 0.0;
    cuLr_ = gainL_;
    cuRr_ = gainR_;
    cuLi_.assign(n, 0.0);
    cuRi_.assign(n, 0.0);
    // The shared oscillator's decay (see oscillatorWash).
    oscWash_ = oscillatorWash(b, p);

    // hertz_vel_scale's reference spectrum: 1 / H(f, tau_ref) = sqrt(1 + (pi f tau_ref)^2).
    hertzRef_.resize(n);
    computeHertzRef();

    const EdgeNpz& z = b.edgeNpz;
    if (z.present)
    {
        remapTable_.resize(z.velocities.size());
        computeRemapTable();
    }
    envBake_ = z.present;
    envOn_ = z.present && z.envelopeActive;
    if (envBake_)
    {
        // An envelope strike's level follows its band envelope, not the T60 decay, so the
        // shared oscillator must not decay (oscillatorWash): every component carries its
        // whole decay.
        bandStart_.assign(size_t(z.numBands), 0);
        bandEnd_.assign(size_t(z.numBands), 0);
        for (int k = 0; k < n_; ++k)
        {
            const int band = z.bandIndex[size_t(k)];
            if (k > 0 && band < z.bandIndex[size_t(k - 1)])
                throw BakeError("FastEngine: the NPZ envelope bands must rise with the mode index");
            if (k == 0 || band != z.bandIndex[size_t(k - 1)])
                bandStart_[size_t(band)] = k;
            bandEnd_[size_t(band)] = k + 1;
        }
        bandVal0_.assign(size_t(z.numBands), 0.0);
        bandVal1_.assign(size_t(z.numBands), 0.0);
        envPeakS_.assign(size_t(z.numBands), 0.0);
        // The onset-shift band of each envelope band: that of its middle mode (602 and Bettis:
        // the same one-third octaves, to about 0.1 %; Swish: 20 bands of its own, 100 Hz-10 kHz).
        npzShiftBand_.assign(size_t(z.numBands), 0);
        for (int band = 0; band < z.numBands; ++band)
        {
            const size_t bs = size_t(band);
            npzShiftBand_[bs] = bandEnd_[bs] > bandStart_[bs]
                                    ? modeBand_[size_t((bandStart_[bs] + bandEnd_[bs] - 1) / 2)]
                                    : static_cast<uint8_t>(std::min(band, kExcitationBands - 1));
        }
    }
    if (p.contactPermode)
    {
        contactPool_.resize(kMaxContacts);
        for (auto& c : contactPool_)
        {
            c.cr.assign(n, 0.0);
            c.ci.assign(n, 0.0);
        }
    }
    components_.resize(kMaxComponents);
    for (auto& c : components_)
    {
        for (auto* v : {&c.Br, &c.Bi, &c.D, &c.dmul, &c.invDmul, &c.gm1, &c.rsl})
            v->assign(np, 0.0f);
        c.rc0.assign(np, 1.0f);
        for (auto* v : {&c.epsK, &c.pt})
            v->assign(np, 0.0);
        if (envBake_)
            for (auto* v : {&c.Er, &c.Ei, &c.fd, &c.em0, &c.em1, &c.emax})
                v->assign(np, 0.0f);
    }

    actIdx_.assign(np, 0);
    actPos_.assign(n, -1);
    for (auto* v : {&yr_, &yi_, &sr32_, &si32_, &ar_, &ai_, &dar_, &dai_, &pl32_, &pr32_, &dpl32_, &dpr32_})
        v->assign(np, 0.0f);
    for (auto* v : {&Yr_, &Yi_, &cr_, &ci_, &qr_, &qi_, &phiW0_, &phiW_, &gBound_})
        v->assign(np, 0.0);
    for (auto* v : {&A0r_, &A0i_, &A1r_, &A1i_, &dropW_, &dropE_, &rotR_, &rotI_, &sumR_, &sumI_})
        v->assign(np, 0.0f);
    // Energy cascade: each mode's share weight (exc_norm), the BOW articulation (its
    // threshold reference), the block gains, and the settings of the bake's tuning.
    cascW_.assign(np, 0.0f);
    modeE_.assign(np, 0.0);
    for (size_t i = 0; i < n; ++i)
        cascW_[i] = static_cast<float>(std::max(b.excNorm[i], 0.0));
    cascSlot_ = -1;
    for (size_t s = 0; s < b.slots.size(); ++s)
        if (b.slots[s].id == "bow")
            cascSlot_ = static_cast<int>(s);
    if (cascSlot_ < 0) // no BOW: the articulation struck nearest the bow position
    {
        cascSlot_ = 0;
        for (size_t s = 1; s < b.slots.size(); ++s)
            if (std::fabs(b.slots[s].position - 0.5) < std::fabs(b.slots[size_t(cascSlot_)].position - 0.5))
                cascSlot_ = static_cast<int>(s);
    }
    blkG_.assign(np, 1.0f);
    injR_.assign(np, 0.0f);
    injI_.assign(np, 0.0f);
    oscG_.assign(np, 1.0);
    setMechanisms(b.tuning.cymbal);
    kinks_.resize(np);
    accL_.assign(size_t(kMaxControlBlock) * 4, 0.0f);
    accR_.assign(size_t(kMaxControlBlock) * 4, 0.0f);
    scratchPhase_.resize(n);
    scratchVel_.resize(n);
    scratchBellVel_.resize(n);
    scratchPosW_.resize(n);
    scratchPkT_.resize(n);
    phaseCos_.resize(n);
    phaseSin_.resize(n);
    rng_ = b.renderStream; // the strike-phase sequence starts from this preset's seed
    aheadDone_ = 0;
}

// About 2.8 ms between control points at every supported rate. With NPZ edge data (a band
// envelope, or one the tuning can switch on), a whole number of blocks per 20 ms frame of the
// in-frame decay (line 889), so its restarts fall on control points: 7 x 126 at 44.1 kHz,
// 8 x 120 at 48 kHz, 7 x 252 at 88.2 kHz, 8 x 240 at 96 kHz.
int FastEngine::controlBlockFor(const BakeData& b, double sampleRate, int* frameBlocks) noexcept
{
    int K = sampleRate > 50000.0 ? 256 : 128;
    if (b.edgeNpz.present)
    {
        const double frame = b.edgeNpz.frameDecayPeriodS * sampleRate;
        const int blocks = std::max(1, static_cast<int>(std::lround(frame / K)));
        K = std::min(kMaxControlBlock, std::max(16, static_cast<int>(std::lround(frame / blocks))));
        if (frameBlocks != nullptr)
            *frameBlocks = blocks;
    }
    return K;
}

// The shared oscillator's decay: T60 / the smallest wash_damp a strike with a velocity in
// 0..1 can have, so every strike's own decay relative to it is >= 0 (it only falls). With
// NPZ edge data (a band envelope, or one the tuning can switch on) it has none: every strike
// carries its whole decay.
double FastEngine::oscillatorWash(const BakeData& b, const EngineParams& p) noexcept
{
    if (b.edgeNpz.present)
        return 0.0;
    return std::max(kWashDampFloor, 1.0 + p.velLenTilt * ((p.velLenTilt >= 0.0 ? 0.0 : 1.0) - p.velLenTiltRef));
}

// Contact force (_apply_strike lines 1073-1091). The taus and sigmas are samples at the
// bake's rate (44.1 kHz); here they are converted to samples at the host rate, so the
// contact keeps its duration in seconds. At 44.1 kHz this is Python's matrix. Stored
// sample-major (len x numModes) so each contact sample reads one contiguous row.
void FastEngine::contactForce(const BakeData& b, const EngineParams& p, double sampleRate, std::vector<double>& force,
                              int& len)
{
    const size_t n = size_t(b.numModes);
    const double ratio = sampleRate / b.sampleRate;
    const double tau0 = p.tipTau0, tauM = p.tipTauMax; // position-independent (checked)
    const double fMax = *std::max_element(b.freqHz.begin(), b.freqHz.end());
    std::vector<double> tau(n), sig(n);
    double sigMax = -1.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double fNorm = std::sqrt(b.freqHz[i] / (fMax + 1e-9));
        const double t44 = tauM - (tauM - tau0) * fNorm;
        const double s44 = std::max(t44 * p.sigmaRatio, 1.0);
        tau[i] = t44 * ratio;
        sig[i] = s44 * ratio;
        sigMax = std::max(sigMax, s44);
    }
    len = static_cast<int>(tauM * ratio + 3.0 * sigMax * ratio) + 1;
    const size_t L = size_t(len);
    force.assign(L * n, 0.0);
    std::vector<double> row(L);
    for (size_t i = 0; i < n; ++i)
    {
        double sum = 0.0;
        for (size_t k = 0; k < L; ++k)
        {
            const double x = (static_cast<double>(k) - tau[i]) / sig[i];
            row[k] = std::exp(-0.5 * (x * x));
            sum += row[k];
        }
        const double denom = sum + 1e-9;
        for (size_t k = 0; k < L; ++k)
            force[k * n + i] = row[k] / denom;
    }
}

// Every decay coefficient of every mode at a rate and control block, from its decay rate
// -ln(1000) / T60: the undamped per-sample step (contact phase), the shared oscillator's
// per-sample and per-block steps (decay T60 / oscWash) and the contact's response
// H = sum_j f_j step^(len - j), the state a unit coupling leaves at the end of a contact.
// Allocates only when `out` does not have the bake's sizes.
void FastEngine::computeDecay(const BakeData& b, double sampleRate, int K, double oscWash, const double* negLambda,
                              const std::vector<double>& force, int contactLen, DecaySet& out)
{
    const size_t n = size_t(b.numModes), np = size_t(roundUp(b.numModes, kGroup));
    for (auto* v : {&out.negLambda, &out.stepR, &out.stepI, &out.oscR, &out.oscI, &out.PKr, &out.PKi, &out.Hr, &out.Hi})
        if (v->size() != n)
            v->resize(n);
    for (auto* v : {&out.H32r, &out.H32i})
        if (v->size() != np)
            v->assign(np, 0.0f);
    out.sampleRate = sampleRate;
    out.controlBlock = K;
    for (size_t i = 0; i < n; ++i)
    {
        const double omega = b.twoPi * b.freqHz[i];
        const double c1 = std::cos(omega / sampleRate), s1 = std::sin(omega / sampleRate);
        const double e = std::exp(negLambda[i] / sampleRate);
        out.negLambda[i] = negLambda[i];
        out.stepR[i] = c1 * e;
        out.stepI[i] = s1 * e;
        const double eo = std::exp(negLambda[i] * oscWash / sampleRate);
        out.oscR[i] = c1 * eo;
        out.oscI[i] = s1 * eo;
        const double eK = std::exp(negLambda[i] * oscWash * K / sampleRate);
        out.PKr[i] = std::cos(omega * K / sampleRate) * eK;
        out.PKi[i] = std::sin(omega * K / sampleRate) * eK;
        double hr = 0.0, hi = 0.0;
        for (size_t k = 0; k < size_t(contactLen); ++k)
        {
            hr += force[k * n + i];
            const double t = hr * out.stepR[i] - hi * out.stepI[i];
            hi = hr * out.stepI[i] + hi * out.stepR[i];
            hr = t;
        }
        out.Hr[i] = hr;
        out.Hi[i] = hi;
        out.H32r[i] = static_cast<float>(hr);
        out.H32i[i] = static_cast<float>(hi);
    }
}

// The coefficients of `d` become the engine's (vectors swapped: no allocation); `d` holds
// the previous ones.
void FastEngine::takeDecay(DecaySet& d) noexcept
{
    std::swap(negLambda_, d.negLambda);
    std::swap(stepR_, d.stepR);
    std::swap(stepI_, d.stepI);
    std::swap(oscR_, d.oscR);
    std::swap(oscI_, d.oscI);
    std::swap(PKr_, d.PKr);
    std::swap(PKi_, d.PKi);
    std::swap(Hr_, d.Hr);
    std::swap(Hi_, d.Hi);
    std::swap(H32r_, d.H32r);
    std::swap(H32i_, d.H32i);
}

void FastEngine::prepare(double sampleRate)
{
    const BakeData& b = *bake_;
    const EngineParams& p = params_;
    if (sr_ > 0.0)
    {
        timeBase_ += static_cast<double>(clock_) / sr_;
        clock_ = 0;
    }
    sr_ = sampleRate;
    K_ = controlBlockFor(b, sr_, &frameBlocks_);

    // A per-band ring length set live (Tuning window) and not yet taken over: its decay rates,
    // the rest is computed here for this rate.
    if (decayPending_)
    {
        std::copy(pendDecay_.negLambda.begin(), pendDecay_.negLambda.end(), negLambda_.begin());
        decayPending_ = false;
    }
    // Contact force (_apply_strike lines 1073-1091) and every decay coefficient at the host rate.
    if (p.contactPermode)
        contactForce(b, p, sr_, force_, contactLen_);
    else
    {
        force_.clear();
        contactLen_ = 0;
    }
    DecaySet d;
    computeDecay(b, sr_, K_, oscWash_, negLambda_.data(), force_, contactLen_, d);
    pendDecay_ = d; // preallocated for live changes (the same sizes)
    takeDecay(d);
    clearBlockGains();
    cascComp_ = -1;
    cascAct_.fill(0.0);
    dampX_.fill(0.0);
    rhoAny_ = false;
    updateMechanisms(); // its coefficients at this rate, and its threshold with this H

    highPassTargets(hpHz_, hpCoef_);
    hpRampLeft_ = 0;
    hpMix_ = hpOn_ ? 1.0 : 0.0;
    outGainCoef_ = 1.0 - std::exp(-1.0 / (kOutputGainSmoothS * sr_));
    // Stick click: room for the longest burst the tuning allows, per voice.
    stickMaxLen_ = static_cast<int>(kStickLenMaxMs * 0.001 * sr_) + 1;
    stickNoise_.assign(size_t(kStickVoices) * size_t(stickMaxLen_), 0.0f);
    for (size_t v = 0; v < stick_.size(); ++v)
    {
        stick_[v] = StickVoice {};
        stick_[v].noise = stickNoise_.data() + v * size_t(stickMaxLen_);
    }
    stickActive_ = 0;

    // Silence: no ringing, no running contact, no components, a fresh (silent) segment.
    std::fill(zr_.begin(), zr_.end(), 0.0);
    std::fill(zi_.begin(), zi_.end(), 0.0);
    for (auto& c : components_)
        c.used = false;
    activeContacts_ = 0;
    hpZ0_ = hpZ1_ = 0.0;
    // Nothing rings: new output gains apply at once.
    gainRamp_ = false;
    if (gainPending_)
    {
        gainL_ = pendL_;
        gainR_ = pendR_;
        gainPending_ = false;
    }
    sampleNow_ = 0;
    baseSample_ = 0;
    startFreePhase();
}

double FastEngine::timeSeconds() const noexcept
{
    return sr_ > 0.0 ? timeBase_ + static_cast<double>(clock_) / sr_ : timeBase_;
}

int FastEngine::activeComponents() const noexcept
{
    int count = 0;
    for (const auto& c : components_)
        count += c.used ? 1 : 0;
    return count;
}

void FastEngine::process(float* left, float* right, int numSamples) noexcept
{
    if (numSamples <= 0) return;
    if (!isPrepared())
    {
        std::fill(left, left + numSamples, 0.0f);
        std::fill(right, right + numSamples, 0.0f);
        return;
    }
    sampleNow_ = static_cast<int64_t>(clock_);
    if (aheadDone_ < n_ || !(rng_ == aheadFrom_))
        drawPhasesAhead((n_ + 3) / 4);
    int pos = 0;
    while (pos < numSamples)
    {
        if (activeContacts_ > 0)
        {
            CYM_PROF_BEGIN(contactSample);
            renderContactSample(left + pos, right + pos);
            CYM_PROF_END(contactSample);
            ++pos;
        }
        else
        {
            renderFree(left + pos, right + pos, numSamples - pos);
            pos = numSamples;
        }
    }

    // Stick clicks, into both channels (lines 1210-1211), before the high-pass as in render().
    if (stickActive_ > 0)
    {
        CYM_PROF_BEGIN(stickRender);
        renderStick(left, right, numSamples);
        CYM_PROF_END(stickRender);
    }
    CYM_PROF_BEGIN(highPass);

    // Right-channel high-pass (render() line 703; its on/off and frequency are tuning values),
    // direct form II transposed, in double.
    if (hpRampLeft_ == 0 && hpMix_ == 1.0)
    {
        const double b0 = hpCoef_[0], b1 = hpCoef_[1], b2 = hpCoef_[2], a1 = hpCoef_[3], a2 = hpCoef_[4];
        double z0 = hpZ0_, z1 = hpZ1_;
        for (int j = 0; j < numSamples; ++j)
        {
            const double x = right[j];
            const double y = b0 * x + z0;
            z0 = b1 * x - a1 * y + z1;
            z1 = b2 * x - a2 * y;
            right[j] = static_cast<float>(y);
        }
        hpZ0_ = z0;
        hpZ1_ = z1;
    }
    else if (hpRampLeft_ > 0 || hpMix_ != 0.0)
    {
        // Moving to new settings: the coefficients and the share of the filtered signal
        // change linearly, sample by sample (a linear path between two stable 2nd-order
        // high-passes stays stable and keeps the zeros at DC).
        double z0 = hpZ0_, z1 = hpZ1_;
        for (int j = 0; j < numSamples; ++j)
        {
            if (hpRampLeft_ > 0)
            {
                if (--hpRampLeft_ == 0)
                {
                    std::copy(hpTarget_, hpTarget_ + 5, hpCoef_);
                    hpMix_ = hpOn_ ? 1.0 : 0.0;
                }
                else
                {
                    for (int i = 0; i < 5; ++i)
                        hpCoef_[i] += hpStep_[i];
                    hpMix_ += hpMixStep_;
                }
            }
            const double x = right[j];
            const double y = hpCoef_[0] * x + z0;
            z0 = hpCoef_[1] * x - hpCoef_[3] * y + z1;
            z1 = hpCoef_[2] * x - hpCoef_[4] * y;
            right[j] = static_cast<float>(x + hpMix_ * (y - x));
        }
        hpZ0_ = z0;
        hpZ1_ = z1;
    }
    else
        hpZ0_ = hpZ1_ = 0.0; // off: the right channel as it is; the filter starts clear when switched on

    // Output gain of the preset (docs/TUNING.md), smoothed when it changes.
    if (outGain_ != 1.0 || outGainTarget_ != 1.0)
    {
        double g = outGain_;
        if (std::fabs(outGainTarget_ - g) <= 1e-7 * outGainTarget_)
        {
            g = outGainTarget_;
            const float gf = static_cast<float>(g);
            for (int j = 0; j < numSamples; ++j)
            {
                left[j] *= gf;
                right[j] *= gf;
            }
        }
        else
            for (int j = 0; j < numSamples; ++j)
            {
                g += (outGainTarget_ - g) * outGainCoef_;
                left[j] = static_cast<float>(left[j] * g);
                right[j] = static_cast<float>(right[j] * g);
            }
        outGain_ = g;
    }
    CYM_PROF_END(highPass);
    clock_ += static_cast<uint64_t>(numSamples);
}

// Contact phase, one sample, as Engine::renderContactSample, with the host-rate force and
// step: the whole state (every component and the running injections) without the strikes'
// own factors (PORT_MAP f.4, as in Python), but with the cymbal-wide LF/MF gain and wander
// phase of the latest strike, so that neither steps at a contact (PORT_MAP, live-engine
// decision 6).
void FastEngine::renderContactSample(float* left, float* right) noexcept
{
    const int n = n_;
    double* __restrict zr = zr_.data();
    double* __restrict zi = zi_.data();
    for (int c = 0; c < activeContacts_; ++c)
    {
        const Contact& ct = contactPool_[size_t(c)];
        const double* __restrict force = force_.data() + size_t(ct.k) * size_t(n);
        const double* __restrict cr = ct.cr.data();
        const double* __restrict ci = ct.ci.data();
        for (int i = 0; i < n; ++i)
        {
            zr[i] += force[i] * cr[i];
            zi[i] += force[i] * ci[i];
        }
    }
    // Output Re(state x u) . pick (u = gain x exp(i W), 1 without the cymbal-wide factors),
    // then one undamped step. Partial sums in two 2-lane vectors.
    if (contactRot_)
    {
        renderContactRotated(left, right);
        return;
    }
    const double* __restrict pickL = gainL_.data();
    const double* __restrict pickR = gainR_.data();
    const double* __restrict mr = stepR_.data();
    const double* __restrict mi = stepI_.data();
    v2d al0 = {0.0, 0.0}, al1 = {0.0, 0.0}, ar0 = {0.0, 0.0}, ar1 = {0.0, 0.0};
    int i = 0;
    for (; i + 4 <= n; i += 4)
    {
        const v2d r0 = ld2(zr + i), r1 = ld2(zr + i + 2);
        const v2d m0 = ld2(zi + i), m1 = ld2(zi + i + 2);
        al0 += r0 * ld2(pickL + i);
        al1 += r1 * ld2(pickL + i + 2);
        ar0 += r0 * ld2(pickR + i);
        ar1 += r1 * ld2(pickR + i + 2);
        const v2d sr0 = ld2(mr + i), sr1 = ld2(mr + i + 2);
        const v2d si0 = ld2(mi + i), si1 = ld2(mi + i + 2);
        st2(zr + i, r0 * sr0 - m0 * si0);
        st2(zr + i + 2, r1 * sr1 - m1 * si1);
        st2(zi + i, r0 * si0 + m0 * sr0);
        st2(zi + i + 2, r1 * si1 + m1 * sr1);
    }
    double sl[4] = {al0[0], al0[1], al1[0], al1[1]};
    double sr[4] = {ar0[0], ar0[1], ar1[0], ar1[1]};
    for (; i < n; ++i)
    {
        sl[0] += zr[i] * pickL[i];
        sr[0] += zr[i] * pickR[i];
        const double r = zr[i] * mr[i] - zi[i] * mi[i];
        zi[i] = zr[i] * mi[i] + zi[i] * mr[i];
        zr[i] = r;
    }
    left[0] = static_cast<float>((sl[0] + sl[1]) + (sl[2] + sl[3]));
    right[0] = static_cast<float>((sr[0] + sr[1]) + (sr[2] + sr[3]));
    endContactSample();
}

// The same with the cymbal-wide factor u: out = Re(z) Re(u) pick - Im(z) Im(u) pick.
void FastEngine::renderContactRotated(float* left, float* right) noexcept
{
    const int n = n_;
    double* __restrict zr = zr_.data();
    double* __restrict zi = zi_.data();
    const double* __restrict lr = cuLr_.data();
    const double* __restrict li = cuLi_.data();
    const double* __restrict rr = cuRr_.data();
    const double* __restrict ri = cuRi_.data();
    const double* __restrict mr = stepR_.data();
    const double* __restrict mi = stepI_.data();
    v2d al0 = {0.0, 0.0}, al1 = {0.0, 0.0}, ar0 = {0.0, 0.0}, ar1 = {0.0, 0.0};
    int i = 0;
    for (; i + 4 <= n; i += 4)
    {
        const v2d r0 = ld2(zr + i), r1 = ld2(zr + i + 2);
        const v2d m0 = ld2(zi + i), m1 = ld2(zi + i + 2);
        al0 += r0 * ld2(lr + i) - m0 * ld2(li + i);
        al1 += r1 * ld2(lr + i + 2) - m1 * ld2(li + i + 2);
        ar0 += r0 * ld2(rr + i) - m0 * ld2(ri + i);
        ar1 += r1 * ld2(rr + i + 2) - m1 * ld2(ri + i + 2);
        const v2d sr0 = ld2(mr + i), sr1 = ld2(mr + i + 2);
        const v2d si0 = ld2(mi + i), si1 = ld2(mi + i + 2);
        st2(zr + i, r0 * sr0 - m0 * si0);
        st2(zr + i + 2, r1 * sr1 - m1 * si1);
        st2(zi + i, r0 * si0 + m0 * sr0);
        st2(zi + i + 2, r1 * si1 + m1 * sr1);
    }
    double sl[4] = {al0[0], al0[1], al1[0], al1[1]};
    double sr[4] = {ar0[0], ar0[1], ar1[0], ar1[1]};
    for (; i < n; ++i)
    {
        sl[0] += zr[i] * lr[i] - zi[i] * li[i];
        sr[0] += zr[i] * rr[i] - zi[i] * ri[i];
        const double r = zr[i] * mr[i] - zi[i] * mi[i];
        zi[i] = zr[i] * mi[i] + zi[i] * mr[i];
        zr[i] = r;
    }
    left[0] = static_cast<float>((sl[0] + sl[1]) + (sl[2] + sl[3]));
    right[0] = static_cast<float>((sr[0] + sr[1]) + (sr[2] + sr[3]));
    endContactSample();
}

// After a contact sample: the contacts move on; the free phase starts when all have ended.
void FastEngine::endContactSample() noexcept
{
    int kept = 0;
    for (int c = 0; c < activeContacts_; ++c)
    {
        Contact& ct = contactPool_[size_t(c)];
        if (++ct.k < contactLen_)
        {
            if (kept != c) std::swap(contactPool_[size_t(kept)], ct);
            ++kept;
        }
    }
    activeContacts_ = kept;
    ++sampleNow_;
    if (activeContacts_ == 0)
    {
        CYM_PROF_BEGIN(freeStart);
        startFreePhase();
        CYM_PROF_END(freeStart);
    }
}

// Free ring. The control grid starts at the first free-phase sample, so the output does
// not depend on how the host splits it into blocks.
void FastEngine::renderFree(float* left, float* right, int n) noexcept
{
    int pos = 0;
    while (pos < n)
    {
        const int j = static_cast<int>(freePos_ % K_);
        if (j == 0)
        {
            if (gainRamp_)
                endGainRamp();
            if (decayPending_)
                applyDecay();
            beginControlBlock();
        }
        const int m = std::min(K_ - j, n - pos);
        CYM_PROF_BEGIN(kernel);
        std::fill(accL_.begin(), accL_.begin() + 4 * m, 0.0f);
        std::fill(accR_.begin(), accR_.begin() + 4 * m, 0.0f);
        if (nAct_ > 0)
        {
            if (gainRamp_)
                kernel<kVecPerGroup, true>(m);
            else
                kernel<kVecPerGroup, false>(m);
        }
        for (int k = 0; k < m; ++k)
        {
            const float* a = accL_.data() + 4 * k;
            const float* c = accR_.data() + 4 * k;
            left[pos + k] = (a[0] + a[1]) + (a[2] + a[3]);
            right[pos + k] = (c[0] + c[1]) + (c[2] + c[3]);
        }
        CYM_PROF_END(kernel);
        if (nKinks_ > 0)
        {
            CYM_PROF_BEGIN(kinks);
            renderKinks(left + pos, right + pos, j, m);
            CYM_PROF_END(kinks);
        }
        freePos_ += m;
        sampleNow_ += m;
        pos += m;
    }
}

// m samples of every ringing mode. Lanes are modes; G vectors (4G modes) per group. Per
// mode and sample: out = Re(y * a);  L += out * gainL;  R += out * gainR;  y *= s;  a += da.
// y is the shared oscillator (frequency, undamped decay, wander), a the components' sum.
// With Ramp (a block after new output gains) the gains also move by their step per sample.
template <int G, bool Ramp>
void FastEngine::kernel(int m) noexcept
{
    const int groups = (nAct_ + 4 * G - 1) / (4 * G);
    float* accL = accL_.data();
    float* accR = accR_.data();
    for (int g = 0; g < groups; ++g)
    {
        const size_t o = size_t(g) * 4 * G;
        v4f yr[G], yi[G], sr[G], si[G], ar[G], ai[G], dr[G], di[G], pl[G], pr[G];
        [[maybe_unused]] v4f dpl[G], dpr[G];
        for (int q = 0; q < G; ++q)
        {
            const size_t k = o + size_t(4 * q);
            yr[q] = ld(&yr_[k]);
            yi[q] = ld(&yi_[k]);
            sr[q] = ld(&sr32_[k]);
            si[q] = ld(&si32_[k]);
            ar[q] = ld(&ar_[k]);
            ai[q] = ld(&ai_[k]);
            dr[q] = ld(&dar_[k]);
            di[q] = ld(&dai_[k]);
            pl[q] = ld(&pl32_[k]);
            pr[q] = ld(&pr32_[k]);
            if constexpr (Ramp)
            {
                dpl[q] = ld(&dpl32_[k]);
                dpr[q] = ld(&dpr32_[k]);
            }
        }
        for (int j = 0; j < m; ++j)
        {
            v4f out = yr[0] * ar[0] - yi[0] * ai[0];
            v4f al = out * pl[0];
            v4f aR = out * pr[0];
            for (int q = 1; q < G; ++q)
            {
                out = yr[q] * ar[q] - yi[q] * ai[q];
                al += out * pl[q];
                aR += out * pr[q];
            }
            st(accL + 4 * j, ld(accL + 4 * j) + al);
            st(accR + 4 * j, ld(accR + 4 * j) + aR);
            for (int q = 0; q < G; ++q)
            {
                const v4f t = yr[q] * sr[q] - yi[q] * si[q];
                yi[q] = yr[q] * si[q] + yi[q] * sr[q];
                yr[q] = t;
                ar[q] += dr[q];
                ai[q] += di[q];
                if constexpr (Ramp)
                {
                    pl[q] += dpl[q];
                    pr[q] += dpr[q];
                }
            }
        }
        for (int q = 0; q < G; ++q)
        {
            const size_t k = o + size_t(4 * q);
            st(&yr_[k], yr[q]);
            st(&yi_[k], yi[q]);
            st(&ar_[k], ar[q]);
            st(&ai_[k], ai[q]);
            if constexpr (Ramp)
            {
                st(&pl32_[k], pl[q]);
                st(&pr32_[k], pr[q]);
            }
        }
    }
}

// A component-mode whose ramp reaches 1 inside the current block: oscillator x amplitude at
// the block start, for renderKinks(), which steps it with the shared oscillator's step
// times the component's own decay per sample.
void FastEngine::addKink(const Component& c, int mode, double d0, double te0) noexcept
{
    const size_t k = size_t(mode);
    const int a = actPos_[k];
    if (a < 0 || nKinks_ >= static_cast<int>(kinks_.size()))
        return; // a silent mode holds nothing
    const size_t sa = size_t(a);
    const double mr = c.Br[k] * d0, mi = c.Bi[k] * d0;
    const double yr = yr_[sa], yi = yi_[sa];
    KinkMode& km = kinks_[size_t(nKinks_++)];
    km.yr = yr * mr - yi * mi;
    km.yi = yr * mi + yi * mr;
    km.sa = a;
    km.dps = 1.0 + (static_cast<double>(c.dmul[k]) - 1.0) / static_cast<double>(K_);
    km.pl = pl32_[sa];
    km.pr = pr32_[sa];
    km.dpl = gainRamp_ ? dpl32_[sa] : 0.0;
    km.dpr = gainRamp_ ? dpr32_[sa] : 0.0;
    km.gm1 = c.onsetOn ? c.gm1[k] : 0.0;
    km.pt = c.pt[k];
    km.te0 = te0;
    km.env0 = c.env0;
    km.env1 = c.env1;
    km.g0 = modeGain(k, blkLf0_, blkMf0_);
    km.g1 = modeGain(k, blkLf1_, blkMf1_);
    if (c.rhoOn) // its re-hit damping
    {
        km.g0 *= static_cast<double>(c.rho0);
        km.g1 *= static_cast<double>(c.rho1);
    }
}

// The component-modes whose ramp corner lies in this block, samples j0 .. j0+m of the
// block, with the ramp evaluated per sample (lines 896-901) and the onset envelope
// interpolated.
void FastEngine::renderKinks(float* left, float* right, int j0, int m) noexcept
{
    const double bf = params_.birthFrac;
    const double invK = 1.0 / static_cast<double>(K_);
    for (int q = 0; q < nKinks_; ++q)
    {
        KinkMode& km = kinks_[size_t(q)];
        double yr = km.yr, yi = km.yi;
        const double invPt = 1.0 / km.pt;
        const double sr = static_cast<double>(sr32_[size_t(km.sa)]) * km.dps;
        const double si = static_cast<double>(si32_[size_t(km.sa)]) * km.dps;
        for (int j = 0; j < m; ++j)
        {
            const int jb = j0 + j;
            const double te = km.te0 + static_cast<double>(jb) / sr_;
            double g = ramp(bf, te, invPt);
            if (km.gm1 != 0.0)
                g *= 1.0 + km.gm1 * (km.env0 + (km.env1 - km.env0) * (static_cast<double>(jb) * invK));
            g *= km.g0 + (km.g1 - km.g0) * (static_cast<double>(jb) * invK);
            left[j] += static_cast<float>(yr * (km.pl + km.dpl * static_cast<double>(jb)) * g);
            right[j] += static_cast<float>(yr * (km.pr + km.dpr * static_cast<double>(jb)) * g);
            const double t = yr * sr - yi * si;
            yi = yr * si + yi * sr;
            yr = t;
        }
        km.yr = yr;
        km.yi = yi;
    }
}

// Every kDropEvery blocks: modes whose total amplitude bound is below the skip threshold
// stop being rendered (their components are cleared: what is left is below 1e-9 x level),
// and components that are below it in every mode are released.
void FastEngine::dropQuietModes() noexcept
{
    const double thr2 = segThreshold_ * segThreshold_;
    // Per mode: |oscillator|^2 x (bound of the output gains)^2, zero for silent modes.
    std::fill(dropW_.begin(), dropW_.end(), 0.0f);
    std::fill(dropE_.begin(), dropE_.end(), 0.0f);
    for (int r = 0; r < nAct_; ++r)
    {
        const size_t s = size_t(r);
        dropW_[size_t(actIdx_[s])] =
            static_cast<float>((Yr_[s] * Yr_[s] + Yi_[s] * Yi_[s]) * (gBound_[s] * gBound_[s]));
    }
    // Per component: |B D|^2 summed into each mode's energy; released if below everywhere.
    int nUsed = 0;
    const v4f t2 = splat(static_cast<float>(thr2));
    float* __restrict E = dropE_.data();
    const float* __restrict W = dropW_.data();
    for (auto& c : components_)
    {
        if (!c.used || c.pending)
            continue;
        ++nUsed;
        const float* __restrict D = c.D.data();
        const float* __restrict Br = c.Br.data();
        const float* __restrict Bi = c.Bi.data();
        v4i alive = {0, 0, 0, 0};
        for (int o = 0; o < nPad_; o += 4)
        {
            const v4f d = ld(D + o), r = ld(Br + o), m = ld(Bi + o);
            v4f ce = (r * r + m * m) * (d * d);
            if (c.envOn) // and what its band envelope can still give it
            {
                const v4f er = ld(c.Er.data() + o), ei = ld(c.Ei.data() + o), mx = ld(c.emax.data() + o);
                ce += (er * er + ei * ei) * (mx * mx);
            }
            st(E + o, ld(E + o) + ce);
            alive |= (ce * ld(W + o) >= t2);
        }
        if (!(alive[0] | alive[1] | alive[2] | alive[3]))
            c.used = false;
    }

    int w = 0;
    for (int r = 0; r < nAct_; ++r)
    {
        const size_t s = size_t(r);
        const size_t k = size_t(actIdx_[s]);
        // |sum B D G| <= sqrt(nUsed * sum |B D|^2) * gBound (Cauchy-Schwarz)
        if (double(nUsed) * double(E[k]) * double(W[k]) < thr2)
        {
            for (auto& c : components_)
                if (c.used && !c.pending)
                {
                    c.Br[k] = c.Bi[k] = 0.0f;
                    if (c.envOn)
                        c.Er[k] = c.Ei[k] = 0.0f;
                }
            actPos_[k] = -1;
            continue;
        }
        if (w != r)
        {
            const size_t d = size_t(w);
            actIdx_[d] = actIdx_[s];
            for (auto* v : {&Yr_, &Yi_, &cr_, &ci_, &qr_, &qi_, &phiW0_, &phiW_, &gBound_})
                (*v)[d] = (*v)[s];
            pl32_[d] = pl32_[s];
            pr32_[d] = pr32_[s];
        }
        actPos_[k] = w;
        ++w;
    }
    const int end = std::min(nPad_, roundUp(nAct_, kGroup));
    for (int k = w; k < end; ++k)
    {
        const size_t s = size_t(k);
        yr_[s] = yi_[s] = sr32_[s] = si32_[s] = 0.0f;
        ar_[s] = ai_[s] = dar_[s] = dai_[s] = pl32_[s] = pr32_[s] = 0.0f;
    }
    nAct_ = w;
}

// Control point at the start of a block. The cymbal-wide LF/MF scales and the wander rate
// are evaluated here. The shared oscillator of each ringing mode gets its step for the
// block (frequency and undamped decay, times the wander phase increment spread over the
// block) and its double-precision anchor for the next block. Then the components are summed
// into each mode's amplitude at both ends of the block, times the LF/MF gain there.
void FastEngine::beginControlBlock() noexcept
{
    CYM_PROF_BEGIN(controlBlock);
    const BakeData& b = *bake_;
    const double invK = 1.0 / static_cast<double>(K_);
    const int64_t blk = freePos_ / K_;
    const double t0 = segTBase_ + static_cast<double>(blk * K_) / sr_;
    const double t1 = segTBase_ + static_cast<double>((blk + 1) * K_) / sr_;

    if (blkGainOn_ || injOn_)
    {
        CYM_PROF_BEGIN(blockCommit);
        commitBlockGains();
        CYM_PROF_END(blockCommit);
    }
    if (cascOn_)
    {
        CYM_PROF_BEGIN(blockRenorm);
        renormOscillators();
        CYM_PROF_END(blockRenorm);
    }
    if (blk % kDropEvery == 0)
    {
        CYM_PROF_BEGIN(blockDrop);
        dropQuietModes();
        CYM_PROF_END(blockDrop);
    }
    CYM_PROF_BEGIN(blockWander);
    if (gainPending_)
        startGainRamp();
    const int na = nAct_;
    const int* idx = actIdx_.data();

    applyPendingWander(t0);

    // LF and MF scales at the block's ends: the accumulators' targets at t0, smoothed.
    const bool scales = lfOn_ || mfOn_ || lfSm_ != 1.0 || mfSm_ != 1.0;
    if (scales)
    {
        smoothScalesTo(t0);
        blkLf0_ = lfSm_;
        blkMf0_ = mfSm_;
        smoothScalesTo(t1, t0);
        blkLf1_ = lfSm_;
        blkMf1_ = mfSm_;
        blkT0_ = t0;
        blkT1_ = t1;
    }

    // Wander rate for this block: wander_rate_hz x the rate scale (the LF boost follows the
    // decaying accumulator); the LFO time moves on at the previous rate up to t0.
    bool newRate = false;
    double gW = 0.0, rate = 1.0;
    if (wanderOn_)
    {
        rate = params_.wanderRateHz * wanderRate(t0);
        tauRef_ += tauRate_ * (t0 - tauRefTime_);
        tauRefTime_ = t0;
        tauRate_ = rate;
        newRate = rate != blkRate_;
        blkRate_ = rate;
        gW = wanderScale_ / rate;
    }
    CYM_PROF_END(blockWander);

    // Shared oscillator.
    CYM_PROF_BEGIN(blockOscillators);
    {
        double* __restrict Yr = Yr_.data();
        double* __restrict Yi = Yi_.data();
        double* __restrict cr = cr_.data();
        double* __restrict ci = ci_.data();
        double* __restrict qr = qr_.data();
        double* __restrict qi = qi_.data();
        double* __restrict phiW0 = phiW0_.data();
        double* __restrict phiW = phiW_.data();
        float* __restrict yr = yr_.data();
        float* __restrict yi = yi_.data();
        float* __restrict s32r = sr32_.data();
        float* __restrict s32i = si32_.data();
        const double qScale = b.twoPi * rate * static_cast<double>(K_) / sr_;
        for (int a = 0; a < na; ++a)
        {
            const size_t k = size_t(idx[a]);
            // LFO phasor to the next control point; the wander phase changes by
            // wC (depth / rate) x the change of cos(LFO phase) (line 867, integrated).
            double d = 0.0;
            if (wanderOn_)
            {
                if (newRate)
                    cosSin(lfoUnit_[k] * qScale, qr[a], qi[a]);
                const double cn = cr[a] * qr[a] - ci[a] * qi[a];
                const double sn = cr[a] * qi[a] + ci[a] * qr[a];
                d = wC_[k] * gW * (cn - cr[a]);
                cr[a] = cn;
                ci[a] = sn;
                phiW0[a] = phiW[a];
                phiW[a] += d;
            }
            double c1, s1, cK, sK;
            if (wanderSmall_)
            {
                // exp(i d / K) for the per-sample step (|d/K| < 0.002), exp(i d) for the anchor.
                const double x = d * invK, x2 = x * x;
                c1 = 1.0 - 0.5 * x2;
                s1 = x - x * x2 * (1.0 / 6.0);
                const double d2 = d * d;
                cK = 1.0 - d2 * (0.5 - d2 * (1.0 / 24.0 - d2 * (1.0 / 720.0)));
                sK = d * (1.0 - d2 * (1.0 / 6.0 - d2 * (1.0 / 120.0 - d2 * (1.0 / 5040.0))));
            }
            else
            {
                c1 = std::cos(d * invK);
                s1 = std::sin(d * invK);
                cK = std::cos(d);
                sK = std::sin(d);
            }
            s32r[a] = static_cast<float>(oscR_[k] * c1 - oscI_[k] * s1);
            s32i[a] = static_cast<float>(oscR_[k] * s1 + oscI_[k] * c1);
            const double y0r = Yr[a], y0i = Yi[a];
            yr[a] = static_cast<float>(y0r);
            yi[a] = static_cast<float>(y0i);
            const double pr = y0r * PKr_[k] - y0i * PKi_[k];
            const double pi = y0r * PKi_[k] + y0i * PKr_[k];
            Yr[a] = pr * cK - pi * sK;
            Yi[a] = pr * sK + pi * cK;
        }
    }
    CYM_PROF_END(blockOscillators);

    // Components: each mode's amplitude at both ends of the block.
    CYM_PROF_BEGIN(blockSum);
    sumComponents(t0, t1);
    CYM_PROF_END(blockSum);
    if (cascOn_ || dampOn_)
    {
        CYM_PROF_BEGIN(blockBands);
        bandBlock();
        CYM_PROF_END(blockBands);
    }
    CYM_PROF_BEGIN(blockGains);

    // Cymbal-wide LF/MF gains at the block's ends (lines 914-919), then the per-sample increment.
    for (int a = 0; a < na; ++a)
    {
        const size_t s = size_t(a);
        const size_t k = size_t(idx[a]);
        const double g0 = scales ? modeGain(k, blkLf0_, blkMf0_) : 1.0;
        const double g1 = scales ? modeGain(k, blkLf1_, blkMf1_) : 1.0;
        const double r0 = A0r_[k] * g0, i0 = A0i_[k] * g0;
        ar_[s] = static_cast<float>(r0);
        ai_[s] = static_cast<float>(i0);
        dar_[s] = static_cast<float>((A1r_[k] * g1 - r0) * invK);
        dai_[s] = static_cast<float>((A1i_[k] * g1 - i0) * invK);
    }
    CYM_PROF_END(blockGains);
    CYM_PROF_END(controlBlock);
}

// Accumulator value at t: decayed with tau since its last strike; 0 before the first.
double FastEngine::Accumulator::at(double t, double tau) const noexcept
{
    return started ? s0 * std::exp(-std::max(t - t0, 0.0) / tau) : 0.0;
}

// The part its scale follows: everything but the latest strike's own contribution, which
// Python adds after taking that strike's scale (lines 656-665).
double FastEngine::Accumulator::forScale(double t, double tau) const noexcept
{
    return started ? (s0 - latest) * std::exp(-std::max(t - t0, 0.0) / tau) : 0.0;
}

// A strike adds `amount` at t (render() lines 665 and 687).
void FastEngine::Accumulator::add(double t, double tau, double amount) noexcept
{
    s0 = at(t, tau) + amount;
    latest = amount;
    t0 = t;
    started = true;
}

// Wander rate scale at t: the latest strike's (edge_wander_rate), or the LF accumulator's
// boost if that is larger (line 670), following the accumulator as it decays.
double FastEngine::wanderRate(double t) const noexcept
{
    const EngineParams& p = params_;
    double r = edgeRateScale_;
    if (lfOn_ && p.lfWanderRateBoost > 0.0)
        r = std::max(r, 1.0 + lfAcc_.at(t, p.lfTau) * p.lfWanderRateBoost);
    return r;
}

// The smoothed LF and MF scales move to time t towards the accumulators' targets at
// tTarget (lines 656-659 and 678-681: boost up to max_scale above 1, else suppress down to
// min_scale).
void FastEngine::smoothScalesTo(double t, double tTarget) noexcept
{
    const EngineParams& p = params_;
    if (!(t > smTime_))
        return;
    const double a = std::exp(-(t - smTime_) / kScaleSmoothS);
    // An accumulator that is off (depth 0) has the target 1.
    double lfTarget = 1.0, mfTarget = 1.0;
    if (lfOn_)
    {
        const double S = lfAcc_.forScale(tTarget, p.lfTau);
        lfTarget = p.lfMax > 1.0 ? std::min(1.0 + S * p.lfDepth, p.lfMax) : std::max(1.0 - S * p.lfDepth, p.lfMin);
    }
    if (mfOn_)
    {
        const double S = mfAcc_.forScale(tTarget, p.mfTau);
        mfTarget = p.mfMax > 1.0 ? std::min(1.0 + S * p.mfDepth, p.mfMax) : std::max(1.0 - S * p.mfDepth, p.mfMin);
    }
    lfSm_ = lfTarget + (lfSm_ - lfTarget) * a;
    mfSm_ = mfTarget + (mfSm_ - mfTarget) * a;
    smTime_ = t;
}

// At a strike at time t, before anything of the strike is added: the LF/MF scales there,
// each mode's wander phase there (as rendered up to now: inside the current control block,
// the linear interpolation the oscillator makes), and from them the contact phase's
// output factor.
void FastEngine::cymbalWideAtStrike(double t) noexcept
{
    const BakeData& b = *bake_;
    const size_t n = size_t(n_);
    const bool inBlock = freeStarted_ && blkT1_ > blkT0_ && t >= blkT0_ && t <= blkT1_;
    if (lfOn_ || mfOn_)
    {
        if (inBlock && smTime_ >= t)
        {
            const double f = (t - blkT0_) / (blkT1_ - blkT0_);
            lfSm_ = blkLf0_ + (blkLf1_ - blkLf0_) * f;
            mfSm_ = blkMf0_ + (blkMf1_ - blkMf0_) * f;
            smTime_ = t;
        }
        else
            smoothScalesTo(t);
    }
    if (wanderOn_)
    {
        const double tau = tauRef_ + tauRate_ * (t - tauRefTime_);
        const double g = wanderScale_ / tauRate_;
        const int j = static_cast<int>(freePos_ % K_);
        const double frac = j == 0 ? 1.0 : static_cast<double>(j) / static_cast<double>(K_);
        for (size_t k = 0; k < n; ++k)
        {
            if (wC_[k] == 0.0)
                continue;
            const double c = std::cos(b.twoPi * lfoUnit_[k] * tau + b.lfoPhase[k]);
            const int a = freeStarted_ ? actPos_[k] : -1;
            if (a >= 0)
                W_[k] = phiW0_[size_t(a)] + (phiW_[size_t(a)] - phiW0_[size_t(a)]) * frac;
            else
                W_[k] += wC_[k] * g * (c - cW_[k]);
            cW_[k] = c;
        }
        tauRef_ = tau;
        tauRefTime_ = t;
    }
    applyPendingWander(t);
    if (contactRot_)
        for (size_t k = 0; k < n; ++k)
        {
            const double gain = modeGain(k, lfSm_, mfSm_);
            const double ur = W_[k] != 0.0 ? gain * std::cos(W_[k]) : gain;
            const double ui = W_[k] != 0.0 ? gain * std::sin(W_[k]) : 0.0;
            cuLr_[k] = ur * gainL_[k];
            cuLi_[k] = ui * gainL_[k];
            cuRr_[k] = ur * gainR_[k];
            cuRi_[k] = ui * gainR_[k];
        }
}

// Every mode's amplitude at t0 and t1 (A0, A1, mode order): the components summed four
// modes per vector over every mode (silent modes hold zero). Settled components only
// decay; the others also carry their onset gains and, from the edge, their onset ramp.
void FastEngine::sumComponents(double t0, double t1) noexcept
{
    int nS = 0, nO = 0, nR = 0, nE = 0;
    bool rho = false;
    for (auto& c : components_)
    {
        if (!c.used || c.pending)
            continue;
        // Re-hit damping: the ramp's gain at the block's ends. A finished ramp goes into the
        // amplitudes (its gain there is its end value, so nothing changes).
        if (c.rhoOn)
        {
            if (c.rhoT0 >= 0.0 && t0 >= c.rhoT0 + c.rhoT)
            {
                const v4f g = splat(static_cast<float>(c.rhoB));
                for (int o = 0; o < nPad_; o += 4)
                {
                    st(c.Br.data() + o, ld(c.Br.data() + o) * g);
                    st(c.Bi.data() + o, ld(c.Bi.data() + o) * g);
                    if (c.envOn)
                    {
                        st(c.Er.data() + o, ld(c.Er.data() + o) * g);
                        st(c.Ei.data() + o, ld(c.Ei.data() + o) * g);
                    }
                }
                c.rhoOn = false;
                c.rho0 = c.rho1 = 1.0f;
            }
            else
            {
                c.rho0 = static_cast<float>(rhoAt(c, t0));
                c.rho1 = static_cast<float>(rhoAt(c, t1));
                rho = true;
            }
        }
        // After its onset window an envelope strike rings with its own decaying amplitude
        // (line 881: past onset, normal T60).
        if (c.envOn && t0 - c.tStrike >= c.envWindow)
            c.envOn = false;
        if (!c.settled)
        {
            const double te0 = t0 - c.tStrike, te1 = t1 - c.tStrike;
            c.env0 = c.onsetOn ? std::exp(-std::max(te0, 0.0) / c.onsetTau) : 0.0;
            c.env1 = c.onsetOn ? std::exp(-std::max(te1, 0.0) / c.onsetTau) : 0.0;
            if ((!c.onsetOn || c.env0 * c.gm1Max < kSettled) && (!c.rampOn || te0 >= c.ptMax) && !c.envOn)
                c.settled = true;
        }
        if (c.envOn)
            envList_[size_t(nE++)] = &c;
        else if (c.settled)
            settledList_[size_t(nS++)] = &c;
        else if (c.rampOn)
            rampList_[size_t(nR++)] = &c;
        else
            onsetList_[size_t(nO++)] = &c;
    }
    // Envelope strikes: the in-frame decay restarts on a frame boundary; the band
    // envelope at both ends of the block, per mode.
    for (int q = 0; q < nE; ++q)
    {
        Component& c = *envList_[size_t(q)];
        if (c.frameRestart && c.frameBlocksLeft <= 0)
        {
            std::fill(c.fd.begin(), c.fd.end(), 1.0f);
            c.frameBlocksLeft = frameBlocks_;
        }
        const int nb = bake_->edgeNpz.numBands;
        for (int band = 0; band < nb; ++band)
        {
            const size_t bs = size_t(band);
            if (bandEnd_[bs] <= bandStart_[bs])
                continue;
            // With an onset shift the band envelope is read at (t - delay) x speed; before the
            // delay it holds its first frame (envelopeAt clamps at 0).
            double te0 = t0 - c.tStrike, te1 = t1 - c.tStrike;
            if (c.envShift)
            {
                const size_t j = npzShiftBand_[bs];
                te0 = (te0 - static_cast<double>(c.envDelay[j])) * static_cast<double>(c.envSpeed[j]);
                te1 = (te1 - static_cast<double>(c.envDelay[j])) * static_cast<double>(c.envSpeed[j]);
            }
            std::fill(c.em0.begin() + bandStart_[bs], c.em0.begin() + bandEnd_[bs],
                      static_cast<float>(envelopeAt(c, band, te0)));
            std::fill(c.em1.begin() + bandStart_[bs], c.em1.begin() + bandEnd_[bs],
                      static_cast<float>(envelopeAt(c, band, te1)));
        }
    }
    rhoAny_ = rho;
    nKinks_ = 0;
    if (rhoAny_)
        sumBlock<true>(t0, t1, nS, nO, nR, nE);
    else
        sumBlock<false>(t0, t1, nS, nO, nR, nE);
    for (int q = 0; q < nE; ++q)
        --envList_[size_t(q)]->frameBlocksLeft;

}

// A component's re-hit damping gain at time t: smoothstep from rhoA to rhoB over rhoT from
// rhoT0 (rhoA before the ramp starts).
double FastEngine::rhoAt(const Component& c, double t) const noexcept
{
    if (c.rhoT0 < 0.0)
        return c.rhoA;
    const double x = std::min(std::max((t - c.rhoT0) / c.rhoT, 0.0), 1.0);
    return c.rhoA + (c.rhoB - c.rhoA) * (x * x * (3.0 - 2.0 * x));
}

// The components summed into each mode's amplitude at both ends of the block (A0, A1, mode
// order), four modes per vector over every mode (silent modes hold zero). Rho: some
// component has a re-hit damping ramp; each component's contribution is then scaled by its
// gain at the block's ends (1 without a ramp).
template <bool Rho>
void FastEngine::sumBlock(double t0, double t1, int nS, int nO, int nR, int nE) noexcept
{
    float* __restrict a0rP = A0r_.data();
    float* __restrict a0iP = A0i_.data();
    float* __restrict a1rP = A1r_.data();
    float* __restrict a1iP = A1i_.data();
    const v4f one = splat(1.0f);
    const int np = nPad_;
    for (int o = 0; o < np; o += 8)
    {
        v4f a0r[2] = {}, a0i[2] = {}, a1r[2] = {}, a1i[2] = {};
        for (int q = 0; q < nS; ++q)
        {
            Component& c = *settledList_[size_t(q)];
            [[maybe_unused]] const v4f q0 = splat(c.rho0), q1 = splat(c.rho1);
            float* D = c.D.data() + o;
            const float* dm = c.dmul.data() + o;
            const float* br = c.Br.data() + o;
            const float* bi = c.Bi.data() + o;
            for (int h = 0; h < 2; ++h)
            {
                const v4f d0 = ld(D + 4 * h), d1 = d0 * ld(dm + 4 * h);
                st(D + 4 * h, d1);
                v4f x0 = d0, x1 = d1;
                if constexpr (Rho)
                {
                    x0 *= q0;
                    x1 *= q1;
                }
                const v4f r = ld(br + 4 * h), m = ld(bi + 4 * h);
                a0r[h] += r * x0;
                a0i[h] += m * x0;
                a1r[h] += r * x1;
                a1i[h] += m * x1;
            }
        }
        for (int q = 0; q < nO; ++q)
        {
            Component& c = *onsetList_[size_t(q)];
            [[maybe_unused]] const v4f q0 = splat(c.rho0), q1 = splat(c.rho1);
            const v4f e0 = splat(static_cast<float>(c.env0)), e1 = splat(static_cast<float>(c.env1));
            float* D = c.D.data() + o;
            const float* dm = c.dmul.data() + o;
            const float* br = c.Br.data() + o;
            const float* bi = c.Bi.data() + o;
            const float* gm = c.gm1.data() + o;
            for (int h = 0; h < 2; ++h)
            {
                const v4f d0 = ld(D + 4 * h), d1 = d0 * ld(dm + 4 * h);
                st(D + 4 * h, d1);
                const v4f g = ld(gm + 4 * h);
                v4f x0 = d0 * (one + g * e0), x1 = d1 * (one + g * e1);
                if constexpr (Rho)
                {
                    x0 *= q0;
                    x1 *= q1;
                }
                const v4f r = ld(br + 4 * h), m = ld(bi + 4 * h);
                a0r[h] += r * x0;
                a0i[h] += m * x0;
                a1r[h] += r * x1;
                a1i[h] += m * x1;
            }
        }
        for (int q = 0; q < nR; ++q)
        {
            Component& c = *rampList_[size_t(q)];
            [[maybe_unused]] const v4f q0 = splat(c.rho0), q1 = splat(c.rho1);
            // env0/env1 are 0 without onset gains, so the gain factor is then 1.
            const v4f e0 = splat(static_cast<float>(c.env0)), e1 = splat(static_cast<float>(c.env1));
            const v4f te0 = splat(static_cast<float>(t0 - c.tStrike));
            const v4f te1 = splat(static_cast<float>(t1 - c.tStrike));
            float* D = c.D.data() + o;
            const float* dm = c.dmul.data() + o;
            const float* br = c.Br.data() + o;
            const float* bi = c.Bi.data() + o;
            const float* gm = c.gm1.data() + o;
            const float* rc = c.rc0.data() + o;
            const float* rs = c.rsl.data() + o;
            for (int h = 0; h < 2; ++h)
            {
                const v4f d0 = ld(D + 4 * h), d1 = d0 * ld(dm + 4 * h);
                st(D + 4 * h, d1);
                const v4f g = ld(gm + 4 * h);
                const v4f c0 = ld(rc + 4 * h), sl = ld(rs + 4 * h);
                const v4f l0 = c0 + sl * te0, l1 = c0 + sl * te1;
                // The ramp reaches 1 inside this block (te0 < pt < te1): those
                // component-modes are rendered per sample in renderKinks().
                const v4i kink = (l0 < one) & (l1 > one);
                const v4f r0 = vclamp01(l0), r1 = vclamp01(l1);
                v4f x0 = d0 * (one + g * e0) * r0, x1 = d1 * (one + g * e1) * r1;
                if constexpr (Rho)
                {
                    x0 *= q0;
                    x1 *= q1;
                }
                if (kink[0] | kink[1] | kink[2] | kink[3])
                {
                    for (int l = 0; l < 4; ++l)
                        if (kink[l])
                            addKink(c, o + 4 * h + l, d0[l], t0 - c.tStrike);
                    x0 = (v4f) ((v4i) x0 & ~kink);
                    x1 = (v4f) ((v4i) x1 & ~kink);
                }
                const v4f r = ld(br + 4 * h), m = ld(bi + 4 * h);
                a0r[h] += r * x0;
                a0i[h] += m * x0;
                a1r[h] += r * x1;
                a1i[h] += m * x1;
            }
        }
        for (int q = 0; q < nE; ++q)
        {
            // |coupling| x band envelope x in-frame decay (x onset gains): lines 886-894.
            // D keeps stepping for the ring after the window.
            Component& c = *envList_[size_t(q)];
            [[maybe_unused]] const v4f q0 = splat(c.rho0), q1 = splat(c.rho1);
            const v4f e0 = splat(static_cast<float>(c.env0)), e1 = splat(static_cast<float>(c.env1));
            float* D = c.D.data() + o;
            float* F = c.fd.data() + o;
            const float* dm = c.dmul.data() + o;
            const float* er = c.Er.data() + o;
            const float* ei = c.Ei.data() + o;
            const float* gm = c.gm1.data() + o;
            const float* m0 = c.em0.data() + o;
            const float* m1 = c.em1.data() + o;
            // Without the frame restart the in-frame decay is off: F stays 1.
            const v4f fstep = splat(c.frameRestart ? 1.0f : 0.0f);
            for (int h = 0; h < 2; ++h)
            {
                const v4f step = ld(dm + 4 * h);
                st(D + 4 * h, ld(D + 4 * h) * step);
                const v4f f0 = ld(F + 4 * h), f1 = f0 * (fstep * step + (one - fstep));
                st(F + 4 * h, f1);
                const v4f g = ld(gm + 4 * h);
                v4f x0 = ld(m0 + 4 * h) * f0 * (one + g * e0);
                v4f x1 = ld(m1 + 4 * h) * f1 * (one + g * e1);
                if constexpr (Rho)
                {
                    x0 *= q0;
                    x1 *= q1;
                }
                const v4f r = ld(er + 4 * h), m = ld(ei + 4 * h);
                a0r[h] += r * x0;
                a0i[h] += m * x0;
                a1r[h] += r * x1;
                a1i[h] += m * x1;
            }
        }
        for (int h = 0; h < 2; ++h)
        {
            st(a0rP + o + 4 * h, a0r[h]);
            st(a0iP + o + 4 * h, a0i[h]);
            st(a1rP + o + 4 * h, a1r[h]);
            st(a1iP + o + 4 * h, a1i[h]);
        }
    }
}

// At a strike during the free phase: every component's amplitude is brought to this
// sample (its own decay and the shared oscillator's rotation and decay since the segment's
// reference time), and the whole state is their sum. That is where the contact phase
// starts. Modes that are not ringing hold zero in every component.
void FastEngine::rebaseComponents() noexcept
{
    const int j = static_cast<int>(freePos_ % K_);
    // D holds each component's decay at the next control point. Inside a block that is the
    // block's end, and the value here lies between it and the block's start (D / dmul), as
    // the rendering interpolates it.
    const float frac = static_cast<float>(j) / static_cast<float>(K_);
    const double d = static_cast<double>(freePos_) / sr_;
    // The shared oscillator's rotation and decay since the reference time (no wander).
    std::fill(rotR_.begin(), rotR_.end(), 0.0f);
    std::fill(rotI_.begin(), rotI_.end(), 0.0f);
    for (int a = 0; a < nAct_; ++a)
    {
        const size_t s = size_t(actIdx_[size_t(a)]);
        const double e = std::exp(negLambda_[s] * oscWash_ * d);
        rotR_[s] = static_cast<float>(std::cos(omega_[s] * d) * e);
        rotI_[s] = static_cast<float>(std::sin(omega_[s] * d) * e);
    }
    // The energy cascade's changes: this block's, the part rendered so far (all of it at a
    // control point, where they belong to the block that ends there), and the gains of the
    // earlier blocks of the segment.
    if (blkGainOn_ || oscGainOn_ || injOn_)
    {
        const double w = j == 0 ? 1.0 : static_cast<double>(j) / static_cast<double>(K_);
        for (int a = 0; a < nAct_; ++a)
        {
            const size_t s = size_t(actIdx_[size_t(a)]);
            const double g = oscG_[s] * (1.0 + (static_cast<double>(blkG_[s]) - 1.0) * w);
            rotR_[s] = static_cast<float>(rotR_[s] * g);
            rotI_[s] = static_cast<float>(rotI_[s] * g);
        }
        if (injOn_ && cascComp_ >= 0 && components_[size_t(cascComp_)].cascade)
        {
            Component& c = components_[size_t(cascComp_)];
            const float wf = static_cast<float>(w);
            for (int k = 0; k < n_; ++k)
            {
                c.Br[size_t(k)] += injR_[size_t(k)] * wf;
                c.Bi[size_t(k)] += injI_[size_t(k)] * wf;
            }
        }
        clearBlockGains();
    }
    std::fill(sumR_.begin(), sumR_.end(), 0.0f);
    std::fill(sumI_.begin(), sumI_.end(), 0.0f);
    const v4f fr = splat(frac);
    for (auto& c : components_)
    {
        if (!c.used || c.pending)
            continue;
        // Re-hit damping: its gain here, as rendered, goes into the amplitudes; the rest of its
        // ramp continues relative to it.
        float rr = 1.0f;
        if (c.rhoOn)
        {
            rr = j == 0 ? c.rho1 : c.rho0 + (c.rho1 - c.rho0) * frac;
            if (! (rr > 0.0f))
                rr = 0.0f;
            c.rhoA = rr > 0.0f ? c.rhoA / rr : 1.0;
            c.rhoB = rr > 0.0f ? c.rhoB / rr : 1.0;
            c.rho0 = c.rho1 = 1.0f;
        }
        const v4f rv = splat(rr);
        float* __restrict Br = c.Br.data();
        float* __restrict Bi = c.Bi.data();
        const float* __restrict D = c.D.data();
        const float* __restrict inv = c.invDmul.data();
        for (int o = 0; o < nPad_; o += 4)
        {
            v4f dt = ld(D + o);
            if (j != 0)
            {
                const v4f d0 = dt * ld(inv + o);
                dt = d0 + (dt - d0) * fr;
            }
            if (c.rhoOn)
                dt *= rv;
            const v4f br = ld(Br + o) * dt, bi = ld(Bi + o) * dt;
            const v4f ur = ld(rotR_.data() + o), ui = ld(rotI_.data() + o);
            const v4f nr = br * ur - bi * ui, ni = br * ui + bi * ur;
            st(Br + o, nr);
            st(Bi + o, ni);
            st(sumR_.data() + o, ld(sumR_.data() + o) + nr);
            st(sumI_.data() + o, ld(sumI_.data() + o) + ni);
        }
        // An envelope strike's |coupling| only turns with the oscillator (it has no decay).
        if (c.envOn)
            multiplyAmplitudes(c.Er.data(), c.Ei.data(), rotR_.data(), rotI_.data());
        if (c.envOn && c.rhoOn)
            for (int o = 0; o < nPad_; o += 4)
            {
                st(c.Er.data() + o, ld(c.Er.data() + o) * rv);
                st(c.Ei.data() + o, ld(c.Ei.data() + o) * rv);
            }
    }
    for (int k = 0; k < n_; ++k)
    {
        zr_[size_t(k)] = sumR_[size_t(k)];
        zi_[size_t(k)] = sumI_[size_t(k)];
    }
    baseSample_ = sampleNow_;
}

// B *= u per mode (complex), four modes per vector.
void FastEngine::multiplyAmplitudes(Component& c, const float* ur, const float* ui) noexcept
{
    multiplyAmplitudes(c.Br.data(), c.Bi.data(), ur, ui);
}

void FastEngine::multiplyAmplitudes(float* __restrict Br, float* __restrict Bi, const float* ur, const float* ui) noexcept
{
    for (int o = 0; o < nPad_; o += 4)
    {
        const v4f br = ld(Br + o), bi = ld(Bi + o), r = ld(ur + o), m = ld(ui + o);
        st(Br + o, br * r - bi * m);
        st(Bi + o, br * m + bi * r);
    }
}

// Own decay of one mode of a component over a control block, relative to the shared
// oscillator: exp(-ln1000 * eps * K / (T60 * SR)), eps = wash_damp - 0.3 >= 0.
void FastEngine::setDecay(Component& c, int mode) noexcept
{
    const size_t s = size_t(mode);
    const double dm = std::exp(negLambda_[s] * c.epsK[s] * static_cast<double>(K_) / sr_);
    c.dmul[s] = static_cast<float>(dm);
    c.invDmul[s] = static_cast<float>(1.0 / dm);
}

// Adds `from` into `into`; both refer to the same sample. The decay becomes the
// energy-weighted mean per mode. dropEnvelopes: what is left of both onset envelopes is
// dropped (used only when no settled pair exists).
void FastEngine::mergeComponents(Component& into, Component& from, bool dropEnvelopes) noexcept
{
    CYM_PROF_BEGIN(merge);
    double eInto = 0.0, eFrom = 0.0;
    for (int k = 0; k < n_; ++k)
    {
        const size_t s = size_t(k);
        const double ar = into.Br[s], ai = into.Bi[s], br = from.Br[s], bi = from.Bi[s];
        const double ea = ar * ar + ai * ai;
        const double eb = br * br + bi * bi;
        if (ea + eb > 0.0)
            into.epsK[s] = (ea * into.epsK[s] + eb * from.epsK[s]) / (ea + eb);
        into.Br[s] = static_cast<float>(ar + br);
        into.Bi[s] = static_cast<float>(ai + bi);
        setDecay(into, k);
        eInto += ea;
        eFrom += eb;
    }
    if (eInto + eFrom > 0.0)
        into.eps = (eInto * into.eps + eFrom * from.eps) / (eInto + eFrom);
    into.order = std::max(into.order, from.order);
    into.cascade = false;
    // Re-hit damping: what is left of either ramp, weighted by energy (merges happen where
    // both ramps are at 1: at a strike, or between components without one).
    if (into.rhoOn || from.rhoOn)
    {
        const double ra = into.rhoOn ? into.rhoB : 1.0, rb = from.rhoOn ? from.rhoB : 1.0;
        if (! into.rhoOn)
        {
            into.rhoT0 = from.rhoT0;
            into.rhoT = from.rhoT;
        }
        into.rhoB = eInto + eFrom > 0.0 ? (eInto * ra + eFrom * rb) / (eInto + eFrom) : ra;
        into.rhoA = 1.0;
        into.rhoOn = true;
        into.rho0 = into.rho1 = 1.0f;
    }
    if (dropEnvelopes)
    {
        into.onsetOn = into.rampOn = into.envOn = false;
        into.settled = true;
    }
    from.used = false;
    CYM_PROF_END(merge);
}

// A free component slot. When the pool is full, the two settled components with the
// closest decay are merged; failing that, the two oldest are merged.
FastEngine::Component& FastEngine::allocateComponent() noexcept
{
    for (auto& c : components_)
        if (!c.used)
            return c;
    CYM_PROF_COUNT(poolFull);
    Component* a = nullptr;
    Component* b = nullptr;
    double best = 1e300;
    for (auto& x : components_)
        for (auto& y : components_)
            if (&x < &y && x.settled && y.settled && !x.pending && !y.pending && std::fabs(x.eps - y.eps) < best)
            {
                best = std::fabs(x.eps - y.eps);
                a = &x;
                b = &y;
            }
    bool dropEnvelopes = false;
    if (a == nullptr)
    {
        dropEnvelopes = true;
        for (auto& x : components_)
        {
            if (x.pending)
                continue;
            if (a == nullptr || x.order < a->order)
            {
                b = a;
                a = &x;
            }
            else if (b == nullptr || x.order < b->order)
                b = &x;
        }
    }
    if (a == nullptr || b == nullptr)
    {
        // Every slot holds a contact that is still running (kMaxComponents strikes within
        // one contact length): reuse the oldest; its strike joins the new one.
        Component* oldest = &components_[0];
        for (auto& x : components_)
            if (x.order < oldest->order)
                oldest = &x;
        oldest->used = false;
        CYM_PROF_COUNT(reuseOldest);
        return *oldest;
    }
    if (dropEnvelopes)
        CYM_PROF_COUNT(mergeOldest);
    else
        CYM_PROF_COUNT(mergeSettled);
    // Keep the newer one's identity in `into` so the freed slot is the other.
    mergeComponents(*a, *b, dropEnvelopes);
    return *b;
}

void FastEngine::velocityParticipation(const VelocityTable& t, double v, double* out) const noexcept
{
    const int nv = t.numVel;
    const double* norms = t.norms.data();
    const double* curves = t.curves.data();
    if (v <= norms[0])
    {
        const double s = v / (norms[0] + 1e-9);
        for (int i = 0; i < n_; ++i)
            out[i] = curves[size_t(i) * size_t(nv)] * s;
    }
    else if (v >= norms[nv - 1])
    {
        for (int i = 0; i < n_; ++i)
            out[i] = curves[size_t(i) * size_t(nv) + size_t(nv - 1)];
    }
    else
    {
        int idx = static_cast<int>(std::lower_bound(norms, norms + nv, v) - norms) - 1;
        idx = std::min(std::max(idx, 0), nv - 2);
        const double tt = (v - norms[idx]) / (norms[idx + 1] - norms[idx] + 1e-12);
        for (int i = 0; i < n_; ++i)
        {
            const double* row = curves + size_t(i) * size_t(nv);
            out[i] = row[idx] * (1.0 - tt) + row[idx + 1] * tt;
        }
    }
}

// One strike, as Engine::applyStrike, with the position given by the caller. Its coupling,
// wash_damp and onset envelopes become a new component; the accumulators, wander scales and
// MF state stay cymbal-wide.
// hertz_velocity.py hertz_vel_scale(): v^2 x H(f, tau(v)) / H(f, tau_ref) per mode, with
// H(f, tau) = 1 / sqrt(1 + (pi f tau)^2), tau(v) = tau_ref v^-0.2, v clipped to 1e-4..1.
void FastEngine::hertzVelocity(double velocity, double* out) const noexcept
{
    const BakeData& b = *bake_;
    const double v = std::min(std::max(velocity, 1e-4), 1.0);
    const double tauV = (params_.tauRefMs * 1e-3) * std::pow(1.0 / v, 0.2);
    const double amplitude = std::pow(v, 2.0);
    const double k = 3.14159265358979323846 * tauV;
    for (int i = 0; i < n_; ++i)
    {
        const double x = k * b.freqHz[size_t(i)];
        out[i] = amplitude * (hertzRef_[size_t(i)] / std::sqrt(1.0 + x * x));
    }
}

// Lines 1047-1057: the NPZ velw row for this velocity, peak-normalised. The velocity is
// remapped through sqrt (the remap exponent, 0.5 unless tuned) of its 0..1 position among
// the captured velocities, so the rows in between are reached more gradually; below the
// softest capture its row is used. Python
// adds the lowest captured velocity to the remapped value a second time (line 1051), which
// shifts every strike toward the louder captures and never reaches the softest; the plugin
// does not (PORT_MAP, "602 and Bettis").
void FastEngine::npzCoupling(double velocity, double* out) const noexcept
{
    const EdgeNpz& z = bake_->edgeNpz;
    const int nv = static_cast<int>(z.velocities.size());
    const double v0 = z.velocities.front(), v1 = z.velocities.back();
    const double u = clamp01((velocity - v0) / (v1 - v0));
    const double x = remapExp_ == 0.5 ? std::sqrt(u) : std::pow(u, remapExp_);
    const double mapped = numpyInterp(x, remapTable_.data(), z.velocities.data(), nv, 1);
    double peak = 0.0;
    for (int i = 0; i < n_; ++i)
    {
        out[i] = numpyInterp(mapped, z.velocities.data(), z.velw.data() + i, nv, n_);
        peak = std::max(peak, out[i]);
    }
    if (z.normalize && peak > 1e-12)
        for (int i = 0; i < n_; ++i)
            out[i] = out[i] / peak;
}

// Lines 540-573: the band envelope of an edge strike, interpolated between the NPZ
// velocities, peak-normalised and times the per-band RMS correction.
void FastEngine::startEnvelope(Component& c, double velocity) noexcept
{
    const EdgeNpz& z = bake_->edgeNpz;
    const int nv = static_cast<int>(z.velocities.size());
    if (velocity <= z.velocities.front())
    {
        c.envV0 = 0;
        c.envVt = 0.0;
    }
    else if (velocity >= z.velocities.back())
    {
        c.envV0 = nv - 2;
        c.envVt = 1.0;
    }
    else
    {
        int j = static_cast<int>(std::upper_bound(z.velocities.begin(), z.velocities.end(), velocity) -
                                 z.velocities.begin()) - 1;
        j = std::min(std::max(j, 0), nv - 2);
        c.envV0 = j;
        c.envVt = (velocity - z.velocities[size_t(j)]) / (z.velocities[size_t(j) + 1] - z.velocities[size_t(j)]);
    }
    const int nf = z.numFrames, nb = z.numBands;
    const double* r0 = z.envelopes.data() + size_t(c.envV0) * size_t(nf) * size_t(nb);
    const double* r1 = r0 + size_t(nf) * size_t(nb);
    double peak = 0.0;
    for (int i = 0; i < nf * nb; ++i)
        peak = std::max(peak, r0[i] + (r1[i] - r0[i]) * c.envVt);
    c.envScale = peak > 1e-12 ? 1.0 / peak : 1.0;
    for (int band = 0; band < nb; ++band)
    {
        double m = 0.0;
        int kPeak = 0;
        for (int k = 0; k < nf; ++k)
        {
            const size_t at = size_t(k) * size_t(nb) + size_t(band);
            const double x = r0[at] + (r1[at] - r0[at]) * c.envVt;
            if (x > m)
            {
                m = x;
                kPeak = k;
            }
        }
        envPeakS_[size_t(band)] = kPeak * z.frameDurS;
        m *= c.envScale * (z.rmsCorrection.empty() ? 1.0 : z.rmsCorrection[size_t(band)]);
        std::fill(c.emax.begin() + bandStart_[size_t(band)], c.emax.begin() + bandEnd_[size_t(band)],
                  static_cast<float>(m));
    }
    std::copy(c.Br.begin(), c.Br.end(), c.Er.begin());
    std::copy(c.Bi.begin(), c.Bi.end(), c.Ei.begin());
    std::fill(c.fd.begin(), c.fd.end(), 1.0f);
    c.frameBlocksLeft = c.frameRestart ? frameBlocks_ : INT_MAX;
}

// The band envelope of one band at te seconds after the strike (lines 560-567: frame
// values interpolated linearly, held after the last frame).
double FastEngine::envelopeAt(const Component& c, int band, double te) const noexcept
{
    const EdgeNpz& z = bake_->edgeNpz;
    const int nf = z.numFrames, nb = z.numBands;
    const double kf = std::min(std::max(te / z.frameDurS, 0.0), static_cast<double>(nf - 1));
    const int k0 = static_cast<int>(kf);
    const int k1 = std::min(k0 + 1, nf - 1);
    const double fr = kf - k0;
    const double* r0 = z.envelopes.data() + size_t(c.envV0) * size_t(nf) * size_t(nb);
    const double* r1 = r0 + size_t(nf) * size_t(nb);
    auto at = [&](int k) {
        const size_t i = size_t(k) * size_t(nb) + size_t(band);
        return r0[i] + (r1[i] - r0[i]) * c.envVt;
    };
    const double corr = z.rmsCorrection.empty() ? 1.0 : z.rmsCorrection[size_t(band)];
    return (at(k0) * (1.0 - fr) + at(k1) * fr) * c.envScale * corr;
}

// The in-frame decay (line 892: exp(-ln1000 (t mod 20 ms) / T60)) at `elapsed` seconds
// after the strike, and the control blocks until it next restarts, rounded to a block.
void FastEngine::restartFrames(Component& c, double elapsed) noexcept
{
    if (! c.frameRestart)
    {
        std::fill(c.fd.begin(), c.fd.end(), 1.0f);
        c.frameBlocksLeft = INT_MAX;
        return;
    }
    const double period = bake_->edgeNpz.frameDecayPeriodS;
    const double phase = std::fmod(std::max(elapsed, 0.0), period);
    int left = static_cast<int>(std::lround((period - phase) * sr_ / static_cast<double>(K_)));
    if (left <= 0)
    {
        std::fill(c.fd.begin(), c.fd.end(), 1.0f);
        c.frameBlocksLeft = frameBlocks_;
        return;
    }
    c.frameBlocksLeft = std::min(left, frameBlocks_);
    for (int k = 0; k < n_; ++k)
        c.fd[size_t(k)] = static_cast<float>(std::exp(negLambda_[size_t(k)] * c.epsK[size_t(k)] * phase));
}

// LF weight of every mode, 1 - sigmoid at lf_cutoff_hz.
void FastEngine::computeLfWeights() noexcept
{
    const BakeData& b = *bake_;
    for (int i = 0; i < n_; ++i)
        lfW_[size_t(i)] = 1.0 - lfSigmoid(b.freqHz[size_t(i)], params_.lfCutoffHz);
}

// MF weight of every mode, the band-pass from mf_lo_hz to mf_hi_hz.
void FastEngine::computeMfWeights() noexcept
{
    const BakeData& b = *bake_;
    for (int i = 0; i < n_; ++i)
        mfW_[size_t(i)] = mfBandpass(b.freqHz[size_t(i)], params_.mfLoHz, params_.mfHiHz);
}

// Each mode's wander amplitude per unit of (depth scale / rate) and whether any mode wanders.
void FastEngine::computeWanderTables() noexcept
{
    const BakeData& b = *bake_;
    maxDriftDepth_ = 0.0;
    for (int i = 0; i < n_; ++i)
    {
        const size_t k = size_t(i);
        wC_[k] = 0.0;
        if (params_.wanderDepth != 0.0 && params_.wanderRateHz > 0.0 && b.driftHz[k] != 0.0 && lfoUnit_[k] > 0.0)
        {
            wC_[k] = -(b.driftHz[k] * params_.wanderDepth) / lfoUnit_[k];
            maxDriftDepth_ = std::max(maxDriftDepth_, std::fabs(b.driftHz[k] * params_.wanderDepth));
        }
    }
    wanderOn_ = maxDriftDepth_ > 0.0;
}

// hertz_vel_scale's reference spectrum: 1 / H(f, tau_ref) = sqrt(1 + (pi f tau_ref)^2).
void FastEngine::computeHertzRef() noexcept
{
    const BakeData& b = *bake_;
    for (int i = 0; i < n_; ++i)
    {
        const double x = 3.14159265358979323846 * b.freqHz[size_t(i)] * (params_.tauRefMs * 1e-3);
        hertzRef_[size_t(i)] = std::sqrt(1.0 + x * x);
    }
}

// Line 1049: the NPZ velocities normalised to 0..1, raised to the remap exponent (sqrt).
void FastEngine::computeRemapTable() noexcept
{
    const EdgeNpz& z = bake_->edgeNpz;
    const double v0 = z.velocities.front(), v1 = z.velocities.back();
    for (size_t k = 0; k < remapTable_.size(); ++k)
    {
        const double u = (z.velocities[k] - v0) / (v1 - v0);
        remapTable_[k] = remapExp_ == 0.5 ? std::sqrt(u) : std::pow(u, remapExp_);
    }
}

// A wander change from setTuning, at time t (a control point, a strike or the start of a
// free phase): the LFO time continues at the old rate up to t and at the new one after it,
// and the wander phase keeps its value, so neither jumps.
void FastEngine::applyPendingWander(double t) noexcept
{
    if (! wanderPending_)
        return;
    wanderPending_ = false;
    const BakeData& b = *bake_;
    const bool wasOn = wanderOn_;
    if (wasOn && freeStarted_)
        for (int a = 0; a < nAct_; ++a) // the ringing modes' wander phase, at t
            W_[size_t(actIdx_[size_t(a)])] = phiW_[size_t(a)];
    tauRef_ += tauRate_ * (t - tauRefTime_);
    tauRefTime_ = t;
    params_.wanderDepth = pendingWanderDepth_;
    params_.wanderRateHz = pendingWanderRateHz_;
    computeWanderTables();
    tauRate_ = params_.wanderRateHz * wanderRate(t);
    blkRate_ = -1.0; // the LFO steps follow the new rate from the next control point
    contactRot_ = contactRot_ || wanderOn_;
    if (! freeStarted_)
        return;
    wanderSmall_ = b.twoPi * maxDriftDepth_ * std::fabs(wanderScale_) * static_cast<double>(K_) / sr_ < kSmallAngle;
    if (wanderOn_ && ! wasOn)
        for (int a = 0; a < nAct_; ++a) // the ringing modes' LFO phasors start here
        {
            const size_t s = size_t(a), k = size_t(actIdx_[s]);
            const double arg = b.twoPi * lfoUnit_[k] * tauRef_ + b.lfoPhase[k];
            cr_[s] = std::cos(arg);
            ci_[s] = std::sin(arg);
            cW_[k] = cr_[s];
        }
}

// The drop bound of every ringing mode's output gains, for the current LF/MF settings.
void FastEngine::updateGainBounds() noexcept
{
    const double lfHi = std::max({1.0, params_.lfMax, lfSm_});
    const double mfHi = std::max({1.0, params_.mfMax, mfSm_});
    for (int a = 0; a < nAct_; ++a)
    {
        const size_t s = size_t(a), k = size_t(actIdx_[s]);
        gBound_[s] = modeGain(k, lfHi, mfHi) * std::max(1.0, 1.0 + segGm1Max_) * outputBound(k);
    }
}

double FastEngine::outputBound(size_t k) const noexcept
{
    const double g = std::max(std::fabs(gainL_[k]), std::fabs(gainR_[k]));
    return gainRamp_ ? std::max({g, std::fabs(rampL_[k]), std::fabs(rampR_[k])}) : g;
}

// A control point of the free phase after a ramp block: the gains are the ramp's target,
// exactly (the float copies were stepped towards them).
void FastEngine::endGainRamp() noexcept
{
    std::swap(gainL_, rampL_);
    std::swap(gainR_, rampR_);
    gainRamp_ = false;
    for (int a = 0; a < nAct_; ++a)
    {
        const size_t s = size_t(a), k = size_t(actIdx_[s]);
        pl32_[s] = static_cast<float>(gainL_[k]);
        pr32_[s] = static_cast<float>(gainR_[k]);
    }
    std::fill(dpl32_.begin(), dpl32_.end(), 0.0f);
    std::fill(dpr32_.begin(), dpr32_.end(), 0.0f);
    updateGainBounds();
}

// New output gains at a control point of the free phase: over this block every ringing
// mode's gains go linearly from the gains in use to the new ones.
void FastEngine::startGainRamp() noexcept
{
    std::swap(rampL_, pendL_);
    std::swap(rampR_, pendR_);
    gainPending_ = false;
    gainRamp_ = true;
    const float invK = 1.0f / static_cast<float>(K_);
    for (int a = 0; a < nAct_; ++a)
    {
        const size_t s = size_t(a), k = size_t(actIdx_[s]);
        dpl32_[s] = (static_cast<float>(rampL_[k]) - pl32_[s]) * invK;
        dpr32_[s] = (static_cast<float>(rampR_[k]) - pr32_[s]) * invK;
    }
    updateGainBounds();
}

// A strike inside a ramp block: the gains stay where the ramp is (the contact phase plays
// them), and the rest of the change waits for the next control point of the free phase.
void FastEngine::interruptGainRamp() noexcept
{
    // How far the block has gone: all of it when its last sample is rendered (the next
    // control point is not reached yet).
    const int64_t j = freePos_ % K_;
    const double f = j == 0 ? 1.0 : static_cast<double>(j) / static_cast<double>(K_);
    for (size_t k = 0; k < size_t(n_); ++k)
    {
        gainL_[k] += (rampL_[k] - gainL_[k]) * f;
        gainR_[k] += (rampR_[k] - gainR_[k]) * f;
    }
    if (! gainPending_)
    {
        std::swap(pendL_, rampL_);
        std::swap(pendR_, rampR_);
        gainPending_ = true;
    }
    gainRamp_ = false;
    std::fill(dpl32_.begin(), dpl32_.end(), 0.0f);
    std::fill(dpr32_.begin(), dpr32_.end(), 0.0f);
}

void FastEngine::highPassTargets(double hz, double* coef) const noexcept
{
    butterworthHighPass2(hz, sr_, coef[0], coef[1], coef[2], coef[3], coef[4]);
}

void FastEngine::startHighPassRamp() noexcept
{
    highPassTargets(hpHz_, hpTarget_);
    const int n = std::max(1, static_cast<int>(std::lround(kHpRampS * sr_)));
    for (int i = 0; i < 5; ++i)
        hpStep_[i] = (hpTarget_[i] - hpCoef_[i]) / n;
    hpMixStep_ = ((hpOn_ ? 1.0 : 0.0) - hpMix_) / n;
    hpRampLeft_ = n;
}

// A slot's attack band gains (docs/TUNING.md), from its articulation's values: per band the
// gain - 1 (0 within 1e-3 of 0 dB, as Python's onset gains, line 438), the largest |gain - 1|
// over the bands with modes, and the attack time constant (ms x 0.001, as line 435).
void FastEngine::computeAttack(size_t slot) noexcept
{
    const ArticulationTuning& a = slotCurve_[slot];
    double* gm1 = attackGm1_.data() + slot * size_t(kExcitationBands);
    double largest = 0.0;
    for (int j = 0; j < kExcitationBands; ++j)
    {
        const double g = std::pow(10.0, a.attackDb[size_t(j)] / 20.0);
        gm1[j] = std::fabs(g - 1.0) > 1e-3 ? g - 1.0 : 0.0;
        if (bandHasModes_[size_t(j)])
            largest = std::max(largest, std::fabs(gm1[j]));
    }
    attackGm1Max_[slot] = largest;
    attackOn_[slot] = largest > 0.0 ? 1 : 0;
    attackTauS_[slot] = std::max(a.attackMs, 0.1) * 0.001;
}

// The whole-cymbal mechanisms' settings (docs/TUNING.md: energy cascade, loud-band damping,
// re-hit damping), from a tuning: amount 0 switches each off.
void FastEngine::setMechanisms(const CymbalTuning& c) noexcept
{
    auto band = [](double x) {
        return static_cast<int>(std::min<long>(std::max<long>(std::lround(x), 0), kExcitationBands - 1));
    };
    cascAmount_ = std::min(std::max(c.cascadeAmount, 0.0), 1.0);
    cascThrDb_ = c.cascadeThresholdDb;
    cascSpeedS_ = std::max(c.cascadeSpeedMs, 0.1) * 1e-3;
    cascReleaseS_ = std::max(c.cascadeReleaseMs, 0.1) * 1e-3;
    cascSpread_ = static_cast<int>(std::min<long>(std::max<long>(std::lround(c.cascadeSpread), 1), kCascadeMaxSpread));
    cascLo_ = band(c.cascadeLowBand);
    cascHi_ = band(c.cascadeHighBand);
    dampAmount_ = std::max(c.loudDampAmount, 0.0);
    dampThrDb_ = c.loudDampThresholdDb;
    dampTilt_ = std::min(std::max(c.loudDampTilt, -1.0), 1.0);
    rehitAmount_ = std::min(std::max(c.rehitDampAmount, 0.0), 1.0);
    rehitTimeS_ = std::max(c.rehitDampTimeMs, 1.0) * 1e-3;
    for (size_t s = 0; s < rehitW_.size(); ++s)
        rehitW_[s] = std::max(c.rehitWeight[s], 0.0);
    updateMechanisms();
}

// Their coefficients at the prepared rate and control block, and the band thresholds.
void FastEngine::updateMechanisms() noexcept
{
    cascOn_ = cascAmount_ > 0.0;
    if (! cascOn_)
        cascAct_.fill(0.0);
    dampOn_ = dampAmount_ > 0.0;
    if (! dampOn_)
        dampX_.fill(0.0);
    rehitOn_ = rehitAmount_ > 0.0;
    // Tilt: weight 1 - tilt at the lowest band, 1 + tilt at the highest, linear in between.
    for (int j = 0; j < kExcitationBands; ++j)
        dampTiltW_[size_t(j)] =
            1.0 + dampTilt_ * (static_cast<double>(j) - 0.5 * (kExcitationBands - 1)) / (0.5 * (kExcitationBands - 1));
    if (sr_ <= 0.0)
        return;
    const double dt = static_cast<double>(K_) / sr_;
    cascAtk_ = 1.0 - std::exp(-dt / cascSpeedS_);
    cascRel_ = 1.0 - std::exp(-dt / cascReleaseS_);
    dampCoef_ = 1.0 - std::exp(-dt / kScaleSmoothS);
    if (cascOn_ || dampOn_)
        computeBandRef();
}

// The thresholds' reference: each band's energy after a velocity-1 strike of the BOW
// articulation at its position, as that strike's contact leaves it (|coupling x H|^2 summed
// over the band's modes), with the articulation's calibration, level and band gains as they
// are now. Does not allocate.
void FastEngine::computeBandRef() noexcept
{
    const BakeData& b = *bake_;
    const size_t slot = size_t(cascSlot_);
    const SlotData& s = b.slots[slot];
    const size_t n = size_t(n_);
    if (s.hasBowVp)
        velocityParticipation(s.bowVp, 1.0, scratchVel_.data());
    else
        hertzVelocity(1.0, scratchVel_.data());
    // The strike's position weights and velocity blend (as strike() at velocity 1).
    const double pos = clamp01(s.position);
    const double edgeFrac = clamp01(1.0 - pos / 0.5);
    for (size_t i = 0; i < n; ++i)
        scratchPosW_[i] = pos <= 0.5 ? b.edgeWeight[i] + (pos / 0.5) * (1.0 - b.edgeWeight[i])
                                     : (1.0 - clamp01((pos - 0.5) / 0.5)) + clamp01((pos - 0.5) / 0.5) * b.bellWeight[i];
    if (pos > 0.5 && s.hasBellVp)
    {
        const double bellFracV = clamp01((pos - 0.5) / 0.5);
        velocityParticipation(s.bellVp, 1.0, scratchBellVel_.data());
        for (size_t i = 0; i < n; ++i)
            scratchVel_[i] = (1.0 - bellFracV) * scratchVel_[i] + bellFracV * scratchBellVel_[i];
    }
    const double boost = s.edgeLevelBoost > 1.0 && edgeFrac > 0.0 ? 1.0 + (s.edgeLevelBoost - 1.0) * edgeFrac : 1.0;
    const double calib = s.calibScalar * slotTrim_[slot] * boost; // the velocity curve is 0 dB at 1
    const float* exc = excGainOn_[slot] != 0 ? excGain_.data() + slot * n : nullptr;
    const bool contact = params_.contactPermode && Hr_.size() == n;
    bandRef_.fill(0.0);
    for (size_t i = 0; i < n; ++i)
    {
        double m = b.excNorm[i] * numpyScalarPower(scratchVel_[i], s.velGainPower) * scratchPosW_[i] * calib;
        if (exc != nullptr)
            m *= static_cast<double>(exc[i]);
        const double h2 = contact ? Hr_[i] * Hr_[i] + Hi_[i] * Hi_[i] : 1.0;
        bandRef_[modeBand_[i]] += m * m * h2;
    }
    const double thr = std::pow(10.0, cascThrDb_ / 10.0), dthr = std::pow(10.0, dampThrDb_ / 10.0);
    for (int j = 0; j < kExcitationBands; ++j)
    {
        cascThr_[size_t(j)] = bandRef_[size_t(j)] * thr;
        dampThr_[size_t(j)] = bandRef_[size_t(j)] * dthr;
    }
}

// Re-hit damping, at a strike (after the re-anchoring, so every ramp is at 1 here): each
// strike already ringing is scaled to 1 - amount x velocity x the struck articulation's
// weight (at least 0) over the ramp time, as a smoothstep from the start of the free phase,
// and what is left of an earlier ramp is carried into it. The struck component and strikes
// still in their contact are not scaled. Off (and no ramp running): nothing changes.
void FastEngine::startRehit(const Component& struck, int slot, double velocity) noexcept
{
    const double s =
        rehitOn_ ? std::max(0.0, 1.0 - rehitAmount_ * clamp01(velocity) * rehitW_[size_t(slot) % rehitW_.size()]) : 1.0;
    for (auto& c : components_)
    {
        if (! c.used || c.pending || &c == &struck || (! c.rhoOn && s >= 1.0))
            continue;
        const double rest = c.rhoOn ? c.rhoB : 1.0;
        c.rhoOn = true;
        c.rhoA = 1.0;
        c.rhoB = rest * s;
        c.rhoT0 = -1.0;
        c.rhoT = rehitTimeS_;
        c.rho0 = c.rho1 = 1.0f;
    }
}

// Loud-band damping and the energy cascade, at a control point, once the components are
// summed: A1 holds each mode's amplitude at the block's end relative to the shared
// oscillator, Y the oscillator there. Each band's ringing energy (all strikes, before the
// cymbal-wide LF/MF scales) is sum |Y A1|^2 over its ringing modes.
//
// Loud-band damping: x, how far the band is above its threshold (0 at it, 1 at 20 dB
// above, capped), smoothed (20 ms); its modes decay faster by the gain
// exp(-ln(1000) / T60 x amount x tilt weight x x x block) per block.
//
// Energy cascade:
//   - A band's activation follows 1 while it is above its threshold and 0 below it, one-pole
//     with the attack time (SPEED) or the release time.
//   - A band between the lowest and highest band gives the fraction
//     1 - exp(-amount x activation x block / SPEED) of its energy (after the damping) to the
//     bands up to SPREAD above it (within the highest band, ringing): halving per band,
//     normalised. Its modes are scaled by sqrt(1 - fraction).
//   - A target band's energy goes to its ringing modes in proportion to exc_norm, into the
//     cascade component: added in phase with what that component already holds in the
//     mode (a new mode takes a phase from the engine's random sequence), so the moved energy
//     arrives exactly. It decays with the mode's T60. Its interference with what else rings
//     in the mode averages out over a band's modes, as when strikes add.
//
// Both scale the source modes through the shared oscillator's gain at the block's end (so
// every strike in them alike) and this block's end amplitude carries every change (the next
// control point puts it into the state), so nothing steps. Without a free component for the
// moved energy nothing moves in this block. Does not allocate.
void FastEngine::bandBlock() noexcept
{
    const int na = nAct_;
    const int* idx = actIdx_.data();
    const double dt = static_cast<double>(K_) / sr_;
    std::array<double, kExcitationBands> energy {}, weight {}, out {}, in {};
    for (int a = 0; a < na; ++a)
    {
        const size_t s = size_t(a), k = size_t(idx[a]);
        const double ar = A1r_[k], ai = A1i_[k];
        modeE_[s] = (Yr_[s] * Yr_[s] + Yi_[s] * Yi_[s]) * (ar * ar + ai * ai);
        energy[modeBand_[k]] += modeE_[s];
        weight[modeBand_[k]] += static_cast<double>(cascW_[k]);
    }
    // A ringing mode's gain over this block: the oscillator's anchor at the block's end,
    // this block's end amplitude, and the gain the next control point commits.
    auto gain = [&](size_t s, size_t k, double g) {
        Yr_[s] *= g;
        Yi_[s] *= g;
        A1r_[k] = static_cast<float>(A1r_[k] * g);
        A1i_[k] = static_cast<float>(A1i_[k] * g);
        blkG_[k] = static_cast<float>(blkG_[k] * g);
        blkGainOn_ = true;
    };
    bool gains = false;

    if (dampOn_)
    {
        bool any = false;
        for (int j = 0; j < kExcitationBands; ++j)
        {
            const size_t b = size_t(j);
            double x = 0.0;
            if (energy[b] > dampThr_[b])
                x = dampThr_[b] > 0.0 ? std::min(10.0 * std::log10(energy[b] / dampThr_[b]) / 20.0, 1.0) : 1.0;
            dampX_[b] += (x - dampX_[b]) * dampCoef_;
            if (dampX_[b] < 1e-9)
                dampX_[b] = 0.0;
            any = any || dampX_[b] > 0.0;
        }
        if (any)
        {
            const double f = dampAmount_ * dt;
            energy.fill(0.0);
            for (int a = 0; a < na; ++a)
            {
                const size_t s = size_t(a), k = size_t(idx[a]), b = modeBand_[k];
                const double w = std::max(dampTiltW_[b], 0.0) * dampX_[b];
                if (w > 0.0)
                {
                    const double g = std::exp(negLambda_[k] * f * w);
                    gain(s, k, g);
                    modeE_[s] *= g * g;
                    gains = true;
                }
                energy[b] += modeE_[s];
            }
        }
    }

    bool moves = false;
    if (cascOn_)
        for (int j = cascLo_; j <= cascHi_; ++j)
        {
            const size_t b = size_t(j);
            const double target = energy[b] > cascThr_[b] ? 1.0 : 0.0;
            double& act = cascAct_[b];
            act += (target - act) * (target > act ? cascAtk_ : cascRel_);
            if (act < 1e-9)
            {
                act = 0.0;
                continue;
            }
            if (! (energy[b] > 0.0))
                continue;
            double shares = 0.0;
            for (int d = 1; d <= cascSpread_ && j + d <= cascHi_; ++d)
                if (weight[size_t(j + d)] > 0.0)
                    shares += kCascadeShare[d];
            if (! (shares > 0.0))
                continue;
            const double f = 1.0 - std::exp(-cascAmount_ * act * dt / cascSpeedS_);
            const double moved = f * energy[b];
            out[b] = f;
            for (int d = 1; d <= cascSpread_ && j + d <= cascHi_; ++d)
                if (weight[size_t(j + d)] > 0.0)
                    in[size_t(j + d)] += moved * kCascadeShare[d] / shares;
            moves = true;
        }
    Component* casc = moves ? cascadeComponent() : nullptr;
    if (casc != nullptr)
        // Sources.
        for (int a = 0; a < na; ++a)
        {
            const size_t s = size_t(a), k = size_t(idx[a]);
            const double f = out[modeBand_[k]];
            if (f > 0.0)
            {
                gain(s, k, std::sqrt(1.0 - f));
                gains = true;
            }
        }
    // Component-modes rendered per sample in this block (onset ramps) end with the gain.
    if (gains)
        for (int q = 0; q < nKinks_; ++q)
        {
            KinkMode& km = kinks_[size_t(q)];
            km.g1 *= static_cast<double>(blkG_[size_t(idx[km.sa])]);
        }
    if (casc == nullptr)
        return;
    // Targets: the cascade component's amplitude in each mode, raised by the mode's share.
    const double twoPi = bake_->twoPi;
    for (int a = 0; a < na; ++a)
    {
        const size_t s = size_t(a), k = size_t(idx[a]);
        const size_t band = modeBand_[k];
        if (! (in[band] > 0.0) || cascW_[k] <= 0.0f)
            continue;
        const double y2 = Yr_[s] * Yr_[s] + Yi_[s] * Yi_[s];
        const double d = casc->D[k];
        if (! (y2 > 0.0) || ! (d > 0.0))
            continue;
        const double add = in[band] * static_cast<double>(cascW_[k]) / weight[band] / y2; // relative to the oscillator
        const double br = casc->Br[k], bi = casc->Bi[k];
        const double m2 = (br * br + bi * bi) * d * d;
        double dr, di;
        if (m2 > 0.0)
        {
            const double r = std::sqrt(1.0 + add / m2) - 1.0;
            dr = br * r;
            di = bi * r;
        }
        else
        {
            const double phi = rng_.uniform(0.0, twoPi);
            const double m = std::sqrt(add) / d;
            dr = m * std::cos(phi);
            di = m * std::sin(phi);
        }
        injR_[k] = static_cast<float>(injR_[k] + dr);
        injI_[k] = static_cast<float>(injI_[k] + di);
        injOn_ = true;
        const double g = blkG_[k]; // this block's end amplitude is at the end gain
        A1r_[k] = static_cast<float>(A1r_[k] + g * dr * d);
        A1i_[k] = static_cast<float>(A1i_[k] + g * di * d);
    }
}

// The cascade component, or a new one (empty, decaying with each mode's T60). Without a free
// slot two settled strikes merge (as allocateComponent does at a strike), their own decay
// taken into their amplitudes first so that both refer to the next control point. nullptr
// when none can be freed. Does not allocate.
FastEngine::Component* FastEngine::cascadeComponent() noexcept
{
    if (cascComp_ >= 0)
    {
        Component& c = components_[size_t(cascComp_)];
        if (c.used && c.cascade)
            return &c;
        cascComp_ = -1;
    }
    Component* slot = nullptr;
    for (auto& c : components_)
        if (! c.used)
        {
            slot = &c;
            break;
        }
    if (slot == nullptr)
    {
        Component* a = nullptr;
        Component* b = nullptr;
        double best = 1e300;
        for (auto& x : components_)
            for (auto& y : components_)
                if (&x < &y && x.settled && y.settled && ! x.pending && ! y.pending && ! x.rhoOn && ! y.rhoOn &&
                    std::fabs(x.eps - y.eps) < best)
                {
                    best = std::fabs(x.eps - y.eps);
                    a = &x;
                    b = &y;
                }
        if (a == nullptr)
            return nullptr;
        for (Component* x : {a, b})
            for (int o = 0; o < nPad_; o += 4)
            {
                const v4f dk = ld(x->D.data() + o);
                st(x->Br.data() + o, ld(x->Br.data() + o) * dk);
                st(x->Bi.data() + o, ld(x->Bi.data() + o) * dk);
                st(x->D.data() + o, splat(1.0f));
            }
        mergeComponents(*a, *b, false);
        slot = b;
    }
    Component& c = *slot;
    c.used = true;
    c.pending = false;
    c.cascade = true;
    c.rhoOn = false;
    c.rho0 = c.rho1 = 1.0f;
    c.order = ++componentOrder_;
    c.contactStart = sampleNow_;
    c.tStrike = timeBase_ + static_cast<double>(sampleNow_) / sr_;
    c.onsetOn = c.rampOn = c.envOn = c.envShift = false;
    c.settled = true;
    c.eps = std::max(1.0 - oscWash_, 0.0); // wash_damp 1: the mode's own T60
    std::fill(c.epsK.begin(), c.epsK.end(), c.eps);
    std::fill(c.Br.begin(), c.Br.end(), 0.0f);
    std::fill(c.Bi.begin(), c.Bi.end(), 0.0f);
    std::fill(c.D.begin(), c.D.end(), 1.0f);
    for (int k = 0; k < n_; ++k)
        setDecay(c, k);
    cascComp_ = static_cast<int>(slot - components_.data());
    return slot;
}

// At a control point: the previous block's cascade changes go into the state. Its gains join
// the segment's product (which a strike's rebase applies); its additions go into the cascade
// component.
void FastEngine::commitBlockGains() noexcept
{
    if (blkGainOn_)
    {
        for (int k = 0; k < n_; ++k)
            if (blkG_[size_t(k)] != 1.0f)
            {
                oscG_[size_t(k)] *= static_cast<double>(blkG_[size_t(k)]);
                blkG_[size_t(k)] = 1.0f;
            }
        blkGainOn_ = false;
        oscGainOn_ = true;
    }
    if (injOn_)
    {
        if (cascComp_ >= 0 && components_[size_t(cascComp_)].used && components_[size_t(cascComp_)].cascade)
        {
            Component& c = components_[size_t(cascComp_)];
            for (int o = 0; o < nPad_; o += 4)
            {
                st(c.Br.data() + o, ld(c.Br.data() + o) + ld(injR_.data() + o));
                st(c.Bi.data() + o, ld(c.Bi.data() + o) + ld(injI_.data() + o));
            }
        }
        std::fill(injR_.begin(), injR_.end(), 0.0f);
        std::fill(injI_.begin(), injI_.end(), 0.0f);
        injOn_ = false;
    }
}

// Energy cascade: a ringing mode can go on receiving energy while its shared oscillator
// falls far below 1 (its own decay over a long segment, the cascade's gains), and so can the
// cascade component's own decay (D). The component's amplitude, relative to both, would then
// outgrow float. Where the oscillator is below 1e-3 its magnitude moves into every
// component's amplitude in that mode and the oscillator returns to 1; where the cascade
// component's D is below 1e-3 it moves into that component's amplitude and D returns to 1.
// What rings does not change. The segment's gain product follows (a strike's re-anchoring
// uses it). At a control point, before the block starts.
void FastEngine::renormOscillators() noexcept
{
    Component* casc = cascComp_ >= 0 && components_[size_t(cascComp_)].used && components_[size_t(cascComp_)].cascade
                          ? &components_[size_t(cascComp_)]
                          : nullptr;
    for (int a = 0; a < nAct_; ++a)
    {
        const size_t s = size_t(a);
        if (casc != nullptr)
        {
            const size_t k = size_t(actIdx_[s]);
            const float d = casc->D[k];
            if (d < 1e-3f)
            {
                casc->Br[k] *= d;
                casc->Bi[k] *= d;
                casc->D[k] = 1.0f;
            }
        }
        const double y2 = Yr_[s] * Yr_[s] + Yi_[s] * Yi_[s];
        if (! (y2 < 1e-6) || ! (y2 > 0.0))
            continue;
        const double g = std::sqrt(y2);
        const size_t k = size_t(actIdx_[s]);
        Yr_[s] /= g;
        Yi_[s] /= g;
        oscG_[k] /= g;
        oscGainOn_ = true;
        const float gf = static_cast<float>(g);
        for (auto& c : components_)
            if (c.used && ! c.pending)
            {
                c.Br[k] *= gf;
                c.Bi[k] *= gf;
                if (c.envOn)
                {
                    c.Er[k] *= gf;
                    c.Ei[k] *= gf;
                }
            }
    }
}

// No cascade changes pending and no gains in the segment (a new segment, or silence).
void FastEngine::clearBlockGains() noexcept
{
    if (blkGainOn_)
        std::fill(blkG_.begin(), blkG_.end(), 1.0f);
    if (oscGainOn_)
        std::fill(oscG_.begin(), oscG_.end(), 1.0);
    if (injOn_)
    {
        std::fill(injR_.begin(), injR_.end(), 0.0f);
        std::fill(injI_.begin(), injI_.end(), 0.0f);
    }
    blkGainOn_ = oscGainOn_ = injOn_ = false;
}

#if CYMBALIS_TUNING
void FastEngine::setTuning(const Tuning& t, const float* modeGains, const DecaySet* decay, const double* gainL,
                           const double* gainR) noexcept
{
    const CymbalTuning& c = t.cymbal;
    const size_t n = size_t(n_);
    // Per strike.
    for (size_t s = 0; s < slotTrim_.size() && s < size_t(t.numArticulations); ++s)
    {
        slotTrim_[s] = std::pow(10.0, t.articulations[s].levelDb / 20.0);
        slotCurve_[s] = t.articulations[s];
        float* row = excGain_.data() + s * n;
        if (modeGains != nullptr)
            std::copy(modeGains + s * n, modeGains + (s + 1) * n, row);
        else
            excitationModeGains(t.articulations[s], bake_->freqHz.data(), n_, row);
        excGainOn_[s] = hasBandGains(t.articulations[s]) ? 1 : 0;
        onsetShiftOn_[s] = hasOnsetShift(t.articulations[s]) ? 1 : 0;
        shiftLayers_[s] = hasShiftLayers(t.articulations[s]) ? 1 : 0;
        attackLayers_[s] = hasAttackLayers(t.articulations[s]) ? 1 : 0;
        computeAttack(s);
    }
    // Per-band ring lengths: the new coefficients wait for the next control point
    // (applyDecay). A change of the ring length itself rebuilds the engine (applyTuning), and
    // the new engine has both.
    if (c.ringBand != ringBandNow_ && c.ringLength == ringLengthBuilt_)
    {
        ringBandNow_ = c.ringBand;
        // Each mode's decay rate -ln(1000) / T60 with the new factors.
        auto rates = [&](double* out) {
            tunedT60(c, bake_->pythonT60S.data(), bake_->freqHz.data(), n_, out);
            for (size_t i = 0; i < n; ++i)
                out[i] = -bake_->log1000 / out[i];
        };
        if (sr_ <= 0.0)
            rates(negLambda_.data()); // not prepared: prepare() computes the rest
        else if (decay != nullptr && decay->sampleRate == sr_ && decay->controlBlock == K_ &&
                 decay->negLambda.size() == n && decay->H32r.size() == pendDecay_.H32r.size())
        {
            auto copy = [](const auto& from, auto& to) { std::copy(from.begin(), from.end(), to.begin()); };
            copy(decay->negLambda, pendDecay_.negLambda);
            copy(decay->stepR, pendDecay_.stepR);
            copy(decay->stepI, pendDecay_.stepI);
            copy(decay->oscR, pendDecay_.oscR);
            copy(decay->oscI, pendDecay_.oscI);
            copy(decay->PKr, pendDecay_.PKr);
            copy(decay->PKi, pendDecay_.PKi);
            copy(decay->Hr, pendDecay_.Hr);
            copy(decay->Hi, pendDecay_.Hi);
            copy(decay->H32r, pendDecay_.H32r);
            copy(decay->H32i, pendDecay_.H32i);
            decayPending_ = true;
        }
        else
        {
            // Not computed for this rate: here (no allocation, the sizes are prepare()'s).
            rates(pendDecay_.negLambda.data());
            computeDecay(*bake_, sr_, K_, oscWash_, pendDecay_.negLambda.data(), force_, contactLen_, pendDecay_);
            decayPending_ = true;
        }
    }
    if (c.edgeRemapExponent != remapExp_ && c.edgeRemapExponent > 0.0)
    {
        remapExp_ = c.edgeRemapExponent;
        if (! remapTable_.empty())
            computeRemapTable();
    }
    if (c.tauRefMs != params_.tauRefMs && c.tauRefMs > 0.0)
    {
        params_.tauRefMs = c.tauRefMs;
        computeHertzRef();
    }
    envWindowS_ = c.edgeEnvWindowS;
    frameRestart_ = c.frameDecayRestart;
    envOn_ = envBake_ && c.edgeEnvelope; // from the next strike
    params_.edgeWanderDepth = c.edgeWanderDepth;
    params_.edgeWanderRate = c.edgeWanderRate;

    // LF and MF accumulators (their scales are smoothed towards the new targets).
    const bool lfCut = c.lfCutoffHz != params_.lfCutoffHz;
    const bool mfBand = c.mfLoHz != params_.mfLoHz || c.mfHiHz != params_.mfHiHz;
    params_.lfDepth = c.lfDepth;
    params_.lfTau = c.lfTau;
    params_.lfMin = c.lfMin;
    params_.lfMax = c.lfMax;
    params_.lfCutoffHz = c.lfCutoffHz;
    params_.lfWBow = c.lfWBow;
    params_.lfWEdge = c.lfWEdge;
    params_.lfWBell = c.lfWBell;
    params_.lfWanderRateBoost = c.lfWanderRateBoost;
    params_.mfDepth = c.mfDepth;
    params_.mfTau = c.mfTau;
    params_.mfMin = c.mfMin;
    params_.mfMax = c.mfMax;
    params_.mfWBow = c.mfWBow;
    params_.mfWEdge = c.mfWEdge;
    params_.mfWBell = c.mfWBell;
    params_.mfLoHz = c.mfLoHz;
    params_.mfHiHz = c.mfHiHz;
    lfOn_ = params_.lfDepth > 0.0;
    mfOn_ = params_.mfDepth > 0.0;
    if (lfCut)
        computeLfWeights();
    if (mfBand)
        computeMfWeights();
    contactRot_ = contactRot_ || lfOn_ || mfOn_;
    if (freeStarted_)
        updateGainBounds();

    // Wander depth and rate: at the next control point.
    if (c.wanderDepth != params_.wanderDepth || c.wanderRateHz != params_.wanderRateHz)
    {
        wanderPending_ = true;
        pendingWanderDepth_ = c.wanderDepth;
        pendingWanderRateHz_ = c.wanderRateHz;
    }

    outGainTarget_ = std::pow(10.0, c.outputGainDb / 20.0);

    // Output gains (stereo): new ones go to the next control point of the free phase and are
    // ramped over its block; while nothing rings they apply at once.
    {
        const double* L = gainL != nullptr ? gainL : bake_->gainL.data();
        const double* R = gainR != nullptr ? gainR : bake_->gainR.data();
        const auto& toL = gainPending_ ? pendL_ : gainRamp_ ? rampL_ : gainL_;
        const auto& toR = gainPending_ ? pendR_ : gainRamp_ ? rampR_ : gainR_;
        if (! std::equal(toL.begin(), toL.end(), L) || ! std::equal(toR.begin(), toR.end(), R))
        {
            if (! isPrepared() || isSilent())
            {
                std::copy(L, L + n, gainL_.begin());
                std::copy(R, R + n, gainR_.begin());
                gainPending_ = gainRamp_ = false;
                std::fill(dpl32_.begin(), dpl32_.end(), 0.0f);
                std::fill(dpr32_.begin(), dpr32_.end(), 0.0f);
            }
            else
            {
                std::copy(L, L + n, pendL_.begin());
                std::copy(R, R + n, pendR_.begin());
                gainPending_ = true;
            }
        }
    }
    // Right-channel high-pass: to the new settings over kHpRampS.
    if (c.highPassOn != hpOn_ || c.highPassHz != hpHz_)
    {
        hpOn_ = c.highPassOn;
        hpHz_ = c.highPassHz;
        if (isPrepared())
            startHighPassRamp();
        else
            hpMix_ = hpOn_ ? 1.0 : 0.0; // prepare() computes the coefficients
    }

    // Energy cascade and loud-band damping from the next control point, re-hit damping from
    // the next strike. Their thresholds follow the BOW articulation's values set above.
    setMechanisms(c);
}
#endif

void FastEngine::continueFrom(const FastEngine& previous) noexcept
{
    timeBase_ = previous.timeSeconds();
    clock_ = 0;
    sampleNow_ = 0;
    baseSample_ = 0;
}

bool FastEngine::isSilent() const noexcept
{
    if (nAct_ != 0 || activeContacts_ != 0 || stickActive_ != 0)
        return false;
    for (const auto& c : components_)
        if (c.used)
            return false;
    return true;
}


// Draws up to `count` more of the next strike's phases (from where rng_ is now, starting over
// if rng_ has moved since) and evaluates their cos and sin, as strike() would.
void FastEngine::drawPhasesAhead(int count) noexcept
{
    if (!(rng_ == aheadFrom_) || aheadDone_ == 0)
    {
        aheadFrom_ = rng_;
        aheadRng_ = rng_;
        aheadDone_ = 0;
    }
    const BakeData& b = *bake_;
    const double range = b.phaseHigh - b.phaseLow;
    const int end = std::min(n_, aheadDone_ + count);
    for (int i = aheadDone_; i < end; ++i)
    {
        const double phase = aheadRng_.uniform(b.phaseLow, range);
        phaseCos_[size_t(i)] = std::cos(phase);
        phaseSin_[size_t(i)] = std::sin(phase);
    }
    aheadDone_ = end;
}

void FastEngine::strike(int slotIndex, double velocity, double position) noexcept
{
    if (!isPrepared() || slotIndex < 0 || slotIndex >= numSlots()) return;
    CYM_PROF_BEGIN(strike);
    const SlotData& s = bake_->slots[size_t(slotIndex)];
    const BakeData& b = *bake_;
    const EngineParams& p = params_;
    const size_t n = size_t(n_);
    sampleNow_ = static_cast<int64_t>(clock_);

    CYM_PROF_BEGIN(strikeRebase);
    if (gainRamp_)
        interruptGainRamp();
    if (freeStarted_)
        rebaseComponents();
    CYM_PROF_END(strikeRebase);
    const double strikeTime = timeSeconds();
    CYM_PROF_BEGIN(strikeCymbalWide);
    cymbalWideAtStrike(strikeTime);
    CYM_PROF_END(strikeCymbalWide);

    CYM_PROF_BEGIN(strikePhases);
    const double range = b.phaseHigh - b.phaseLow;
    drawPhasesAhead(n_); // the rest of them, if they are still the next in the sequence
    rng_ = aheadRng_;
    aheadDone_ = 0;
    CYM_PROF_END(strikePhases);

    CYM_PROF_BEGIN(strikeVelocity);
    const double v = clamp01(velocity);
    if (s.hasBowVp)
        velocityParticipation(s.bowVp, v, scratchVel_.data());
    else
        hertzVelocity(velocity, scratchVel_.data()); // line 979
    CYM_PROF_END(strikeVelocity);

    CYM_PROF_BEGIN(strikePosition);
    const double pos = clamp01(position);
    const double edgeFrac = clamp01(1.0 - pos / 0.5);
    if (pos <= 0.5)
    {
        const double tBlend = pos / 0.5;
        for (size_t i = 0; i < n; ++i)
            scratchPosW_[i] = b.edgeWeight[i] + tBlend * (1.0 - b.edgeWeight[i]);
    }
    else
    {
        const double bellFrac = clamp01((pos - 0.5) / 0.5);
        for (size_t i = 0; i < n; ++i)
            scratchPosW_[i] = (1.0 - bellFrac) * 1.0 + bellFrac * b.bellWeight[i];
    }
    if (pos > 0.5 && s.hasBellVp)
    {
        CYM_PROF_BEGIN(strikeVelocity); // the bell blend, counted with the velocity law
        const double bellFracV = clamp01((pos - 0.5) / 0.5);
        velocityParticipation(s.bellVp, v, scratchBellVel_.data());
        for (size_t i = 0; i < n; ++i)
            scratchVel_[i] = (1.0 - bellFracV) * scratchVel_[i] + bellFracV * scratchBellVel_[i];
        CYM_PROF_END(strikeVelocity);
    }
    if (s.edgeLevelBoost > 1.0 && edgeFrac > 0.0)
    {
        const double g = 1.0 + (s.edgeLevelBoost - 1.0) * velocity * edgeFrac;
        for (size_t i = 0; i < n; ++i)
            scratchPosW_[i] = scratchPosW_[i] * g;
    }
    CYM_PROF_END(strikePosition);

    // The slot's calibration level, its level trim and its velocity curve (docs/TUNING.md)
    // scale its own strike.
    const double curveDb = velocityCurveDb(slotCurve_[size_t(slotIndex)], velocity);
    const double calib =
        s.calibScalar * slotTrim_[size_t(slotIndex)] * (curveDb != 0.0 ? std::pow(10.0, curveDb / 20.0) : 1.0);
    CYM_PROF_BEGIN(strikeAlloc);
    Component& comp = allocateComponent();
    comp.used = true;
    comp.cascade = false;
    comp.rhoOn = false;
    comp.rho0 = comp.rho1 = 1.0f;
    CYM_PROF_END(strikeAlloc);
    CYM_PROF_BEGIN(strikeRehit);
    startRehit(comp, slotIndex, velocity);
    CYM_PROF_END(strikeRehit);
    comp.order = ++componentOrder_;
    comp.contactStart = sampleNow_;
    CYM_PROF_BEGIN(strikeContact);
    Contact* contact = nullptr;
    if (p.contactPermode)
    {
        if (activeContacts_ == kMaxContacts)
        {
            CYM_PROF_COUNT(contactFlush);
            // Pool full: the oldest contact's remaining force goes in at once.
            Contact& old = contactPool_[0];
            const size_t len = size_t(contactLen_);
            for (size_t i = 0; i < n; ++i)
            {
                double rest = 0.0;
                for (size_t k = size_t(old.k); k < len; ++k)
                    rest += force_[k * n + i];
                zr_[i] += rest * old.cr[i];
                zi_[i] += rest * old.ci[i];
            }
            std::rotate(contactPool_.begin(), contactPool_.begin() + 1, contactPool_.begin() + activeContacts_);
            --activeContacts_;
        }
        contact = &contactPool_[size_t(activeContacts_++)];
        contact->k = 0;
    }
    comp.pending = contact != nullptr;
    CYM_PROF_END(strikeContact);
    // Lines 1043-1061: an edge strike of a preset with NPZ data takes its coupling from the
    // velw tables, with a second set of random phases (Python draws both).
    const bool npzEdge = b.edgeNpz.present && edgeFrac > 0.1 && b.edgeNpz.useVelw;
    double npzLevel = 0.0;
    if (npzEdge)
    {
        CYM_PROF_BEGIN(strikeNpz);
        npzCoupling(velocity, scratchBellVel_.data());
        npzLevel = numpyScalarPower(velocity, b.edgeNpz.levelVelGainPower);
        for (size_t i = 0; i < n; ++i)
            scratchPhase_[i] = rng_.uniform(b.phaseLow, range);
        for (size_t i = 0; i < n; ++i)
        {
            phaseCos_[i] = std::cos(scratchPhase_[i]);
            phaseSin_[i] = std::sin(scratchPhase_[i]);
        }
        CYM_PROF_END(strikeNpz);
    }
    CYM_PROF_BEGIN(strikeCoupling);
    // This articulation's excitation band gains (docs/TUNING.md) scale the energy the strike
    // puts into each mode, whatever the coupling path.
    const float* excGain = excGainOn_[size_t(slotIndex)] != 0 ? excGain_.data() + size_t(slotIndex) * n : nullptr;
    // Brightness vs velocity (docs/TUNING.md): a tilt of tiltDb per octave around 1 kHz,
    // gain (f / 1 kHz) ^ (tiltDb ln 10 / (20 ln 2)) per mode.
    const double tiltDb = brightnessTiltDb(slotCurve_[size_t(slotIndex)], velocity);
    const double tiltExp = tiltDb * 0.16609640474436813; // ln 10 / (20 ln 2)
    for (size_t i = 0; i < n; ++i)
    {
        double m = npzEdge ? scratchBellVel_[i] * npzLevel
                           : (b.excNorm[i] * numpyScalarPower(scratchVel_[i], s.velGainPower)) * scratchPosW_[i];
        if (excGain != nullptr)
            m *= static_cast<double>(excGain[i]);
        if (tiltDb != 0.0)
            m *= std::exp(tiltExp * logF1k_[i]);
        const double cr = (m * phaseCos_[i]) * calib;
        const double ci = (m * phaseSin_[i]) * calib;
        comp.Br[i] = static_cast<float>(cr); // the coupling until its contact ends (startFreePhase)
        comp.Bi[i] = static_cast<float>(ci);
        if (contact != nullptr)
        {
            contact->cr[i] = cr;
            contact->ci[i] = ci;
        }
        else
        {
            zr_[i] += cr; // line 1065: instantaneous injection
            zi_[i] += ci;
        }
    }
    CYM_PROF_END(strikeCoupling);
    // Lines 1114-1125: this strike sets the wander depth and rate scales. The wander phase is
    // integrated, so the change does not make it jump.
    wanderScale_ = 1.0 + velocity * edgeFrac * p.edgeWanderDepth;
    edgeRateScale_ = std::max(1.0 + velocity * edgeFrac * p.edgeWanderRate, 0.1);

    // Lines 1127-1130: this strike's velocity-dependent decay, T60 / wash_damp. It applies
    // to this strike's energy only, relative to the shared oscillator's T60 / 0.3.
    CYM_PROF_BEGIN(strikeDecay);
    const double washDamp = std::max(kWashDampFloor, 1.0 + p.velLenTilt * (velocity - p.velLenTiltRef));
    comp.eps = std::max(washDamp - oscWash_, 0.0); // 0 only for a velocity outside 0..1
    for (size_t i = 0; i < n; ++i)
        comp.epsK[i] = comp.eps;
    if (!comp.pending)
        for (int k = 0; k < n_; ++k) // without a contact phase the decay starts now
            setDecay(comp, k);
    CYM_PROF_END(strikeDecay);

    ++strikesApplied_;
    lastSlot_ = slotIndex;
    const double rawPos = position;
    comp.tStrike = strikeTime;

    // render() lines 621-650: this strike's onset ramp (pk_t blended by edge_frac, with the
    // velocity tilt); its attack band gains below.
    comp.rampOn = false;
    comp.onsetOn = false;
    comp.ptMax = 0.0;
    comp.gm1Max = 0.0;
    // Lines 690-694 and 869-894: with an NPZ band envelope an edge strike's level follows
    // the envelope for its onset window, and the onset ramp does not apply (line 896).
    comp.envOn = envOn_ && edgeFrac > 0.1;
    comp.envWindow = envWindowS_;
    comp.frameRestart = frameRestart_;
    if (comp.envOn)
    {
        CYM_PROF_BEGIN(strikeEnvelope);
        startEnvelope(comp, velocity);
        CYM_PROF_END(strikeEnvelope);
    }
    CYM_PROF_BEGIN(strikeOnsetShift);
    // This articulation's onset shift (docs/TUNING.md), kept with the strike: a later change
    // applies from the next strike.
    const ArticulationTuning& art = slotCurve_[size_t(slotIndex)];
    const bool shift = onsetShiftOn_[size_t(slotIndex)] != 0;
    // Its velocity layers: with SOFT and HARD layers that differ, each band's delay and speed
    // at this strike's velocity; else the articulation's own values.
    const double* dly = art.delayMs.data();
    const double* spd = art.speed.data();
    if (shift && shiftLayers_[size_t(slotIndex)] != 0)
    {
        layeredShift(art, onsetLayerT(velocity), strikeDelay_.data(), strikeSpeed_.data());
        dly = strikeDelay_.data();
        spd = strikeSpeed_.data();
    }
    comp.envShift = comp.envOn && shift;
    if (comp.envShift)
        for (size_t j = 0; j < size_t(kExcitationBands); ++j)
        {
            comp.envDelay[j] = static_cast<float>(dly[j] * 1e-3);
            comp.envSpeed[j] = static_cast<float>(spd[j]);
        }
    CYM_PROF_END(strikeOnsetShift);
    CYM_PROF_BEGIN(strikeRamp);
    if (p.bandEnvOnset)
    {
        const double ef = clamp01(1.0 - rawPos / 0.5);
        if (!b.pkTEdgeByVel.empty())
        {
            // Bettis format (lines 628-631): the edge pk_t interpolated by velocity per mode.
            const int nv = static_cast<int>(b.pkTEdgeVelocities.size());
            for (size_t i = 0; i < n; ++i)
                scratchPkT_[i] = ef * numpyInterp(velocity, b.pkTEdgeVelocities.data(), b.pkTEdgeByVel.data() + i,
                                                  nv, n_) +
                                 (1.0 - ef) * b.pkTBow[i];
        }
        else
            for (size_t i = 0; i < n; ++i)
                scratchPkT_[i] = ef * b.pkTEdge[i] + (1.0 - ef) * b.pkTBow[i];
        if (p.velTilt && ef > 1e-3)
        {
            const int nv = static_cast<int>(b.velTiltVelocities.size());
            const int nb = b.velTiltBands;
            for (int band = 0; band < nb; ++band)
            {
                const double frac =
                    numpyInterp(velocity, b.velTiltVelocities.data(), b.velTiltFractions.data() + band, nv, nb);
                const double ref = b.velTiltReference[size_t(band)];
                const double ratio = ref > 1e-9 ? frac / ref : 1.0;
                scratchBandScale_[size_t(band)] = 1.0 / std::sqrt(std::max(ratio, 0.01));
            }
            for (size_t i = 0; i < n; ++i)
            {
                double sm = scratchBandScale_[size_t(b.velTiltModeBand[i])];
                sm = 1.0 + ef * (sm - 1.0);
                scratchPkT_[i] = scratchPkT_[i] * sm;
            }
        }
        // Lines 848-851: the onset ramp runs for strikes towards the edge. The articulation's
        // onset shift moves each mode's peak time to peak / speed + delay, never below 0. A
        // strike without a ramp has peak time 0, so there a delay brings the band in over
        // the delay and the speed has nothing to act on.
        const bool baseRamp = ef > 0.1 && !comp.envOn;
        comp.rampOn = baseRamp || (shift && !comp.envOn);
        if (comp.rampOn)
            for (size_t i = 0; i < n; ++i)
            {
                // ramp = clamp(bf + (1 - bf) * te / pt, 0, 1) (lines 896-901); 1 without a peak time
                double pt = baseRamp && scratchPkT_[i] > 0.001 ? scratchPkT_[i] : 0.0;
                if (shift)
                {
                    const size_t j = modeBand_[i];
                    pt = pt / spd[j] + dly[j] * 1e-3;
                    pt = pt > 0.001 ? pt : 0.0;
                }
                comp.pt[i] = pt;
                comp.rc0[i] = pt > 0.0 ? static_cast<float>(p.birthFrac) : 1.0f;
                comp.rsl[i] = pt > 0.0 ? static_cast<float>((1.0 - p.birthFrac) / pt) : 0.0f;
                comp.ptMax = std::max(comp.ptMax, pt);
            }
        if (comp.rampOn && !baseRamp && comp.ptMax <= 0.0)
            comp.rampOn = false; // the shift leaves every peak at 0: no ramp
    }
    CYM_PROF_END(strikeRamp);
    CYM_PROF_BEGIN(strikeAttack);
    // Attack band gains (docs/TUNING.md): this articulation's gain per band at the strike,
    // fading to 0 dB with its attack time (lines 902-913: Nova's onset gains are these). As
    // in Python they do not apply to a bow or bell strike blended to the edge (edge fraction
    // 0.9 or more); an edge articulation's apply at the edge.
    {
        const size_t slot = size_t(slotIndex);
        const double ef = clamp01(1.0 - rawPos / 0.5);
        // Its velocity layers: with SOFT and HARD layers that differ, each band's gain at this
        // strike's velocity (as computeAttack); else the slot's.
        const double* gm1 = attackGm1_.data() + slot * size_t(kExcitationBands);
        double gm1Max = attackGm1Max_[slot];
        bool on = attackOn_[slot] != 0;
        if (attackLayers_[slot] != 0)
        {
            layeredAttack(art, onsetLayerT(velocity), strikeAttackDb_.data());
            gm1Max = 0.0;
            for (size_t j = 0; j < size_t(kExcitationBands); ++j)
            {
                const double g = std::pow(10.0, strikeAttackDb_[j] / 20.0);
                strikeGm1_[j] = std::fabs(g - 1.0) > 1e-3 ? g - 1.0 : 0.0;
                if (bandHasModes_[j])
                    gm1Max = std::max(gm1Max, std::fabs(strikeGm1_[j]));
            }
            gm1 = strikeGm1_.data();
            on = gm1Max > 0.0;
        }
        comp.onsetOn = on && (ef < 0.9 || s.position <= 0.25);
        if (comp.onsetOn)
        {
            comp.onsetTau = attackTauS_[slot];
            for (size_t i = 0; i < n; ++i)
                comp.gm1[i] = static_cast<float>(gm1[modeBand_[i]]);
            comp.gm1Max = gm1Max;
        }
    }
    CYM_PROF_END(strikeAttack);
    comp.settled = !comp.rampOn && !comp.onsetOn && !comp.envOn;
#if CYMBALIS_TUNING
    CYM_PROF_BEGIN(strikeOnsetPeaks);
    onsetPeaks(comp, slotIndex);
    CYM_PROF_END(strikeOnsetPeaks);
#endif
    CYM_PROF_BEGIN(strikeAccum);

    // render() lines 652-688: the LF and MF accumulators. This strike's contribution is
    // added after its scale is taken, as in Python: until the next strike the scales follow
    // the accumulators without it, decaying continuously (forScale). The wander-rate boost
    // follows all of S (line 670).
    if (lfOn_)
        lfAcc_.add(strikeTime, p.lfTau,
                   velocity * (rawPos <= 0.25 ? p.lfWEdge : (rawPos >= 0.75 ? p.lfWBell : p.lfWBow)));
    if (mfOn_)
        mfAcc_.add(strikeTime, p.mfTau,
                   velocity * (rawPos <= 0.25 ? p.mfWEdge : (rawPos >= 0.75 ? p.mfWBell : p.mfWBow)));
    // The LFO time runs at the new rate from here (line 670 with the new accumulator value).
    if (wanderOn_)
        tauRate_ = params_.wanderRateHz * wanderRate(strikeTime);
    CYM_PROF_END(strikeAccum);

    // render() line 620: the stick click follows the strike (its noise after the strike's
    // phases in the random sequence), at the strike's level.
    CYM_PROF_BEGIN(strikeStick);
    startStick(slotIndex, velocity, rawPos, calib);
    CYM_PROF_END(strikeStick);

    freeStarted_ = false;
    freePos_ = 0;
    if (activeContacts_ == 0)
    {
        CYM_PROF_BEGIN(strikeFreeStart);
        startFreePhase();
        CYM_PROF_END(strikeFreeStart);
    }
    CYM_PROF_END(strike);
}

// The stick click of a strike (_add_transient lines 1194-1211): a noise burst of the
// articulation's length, standard normal values from the strike-phase random sequence
// (Box-Muller, two uniforms per pair) times exp(-k / decay), through the band-pass, times
// stick_gain x the position weight x velocity^1.7 and the strike's level (calibration, trim,
// velocity curve; Python scales the whole output by the calibration). Python runs at the bake's
// rate; the noise is scaled by sqrt(rate / bake rate) so the filtered burst keeps its level
// at every rate. With all voices busy the oldest is replaced. Does not allocate.
void FastEngine::startStick(int slot, double velocity, double position, double level) noexcept
{
    const ArticulationTuning& a = slotCurve_[size_t(slot)];
    if (! (a.stickGain > 0.0) || stickMaxLen_ <= 0)
        return;
    const SlotData& s = bake_->slots[size_t(slot)];
    const double bowFrac = clamp01(1.0 - std::fabs(position - 0.5) / 0.5);
    const double bellFrac = clamp01((position - 0.5) / 0.5);
    const double edgeFrac = clamp01(1.0 - position / 0.5);
    const double w = bowFrac * s.stickBowWeight + bellFrac * s.stickBellWeight + edgeFrac * s.stickEdgeWeight;
    if (! (w > 1e-6))
        return;
    const double lenMs = std::min(std::max(a.stickLenMs, kStickLenMinMs), kStickLenMaxMs);
    const int len = std::min(static_cast<int>(lenMs * 0.001 * sr_), stickMaxLen_);
    if (len <= 0)
        return;
    StickVoice* v = nullptr;
    for (auto& x : stick_)
        if (! x.on)
        {
            v = &x;
            break;
        }
    if (v == nullptr)
    {
        v = &stick_[0];
        for (auto& x : stick_)
            if (x.order < v->order)
                v = &x;
        --stickActive_;
    }
    const double decay = std::max(a.stickDecayMs, kStickDecayMinMs) * 0.001 * sr_;
    const double step = std::exp(-1.0 / decay);
    const double scale = std::sqrt(sr_ / bake_->sampleRate);
    double env = scale;
    for (int k = 0; k < len; k += 2)
    {
        const double u1 = 1.0 - rng_.nextDouble(); // (0, 1]
        const double u2 = rng_.nextDouble();
        const double r = std::sqrt(-2.0 * std::log(u1));
        const double th = bake_->twoPi * u2;
        v->noise[k] = static_cast<float>(r * std::cos(th) * env);
        env *= step;
        if (k + 1 < len)
        {
            v->noise[k + 1] = static_cast<float>(r * std::sin(th) * env);
            env *= step;
        }
    }
    const double nyq = 0.5 * sr_;
    const double lo = std::min(std::max(a.stickLoHz, 20.0), 0.9 * nyq);
    const double hi = std::min(std::max(a.stickHiHz, lo * 1.01), 0.95 * nyq);
    butterworthBandPass2(lo, hi, sr_, v->b0, v->a1, v->a2);
    v->s1[0] = v->s1[1] = v->s2[0] = v->s2[1] = 0.0;
    v->k = 0;
    v->len = len;
    v->g = a.stickGain * w * std::pow(std::max(velocity, 0.0), 1.7) * level;
    v->order = ++stickOrder_;
    v->on = true;
    ++stickActive_;
}

#if CYMBALIS_TUNING
// When each band of a strike peaks (onsetPeaksMs), from its onset state just after the strike:
// per mode, with a band envelope when its envelope band peaks, read at (t - delay) x speed
// with the onset shift; with an onset ramp its peak time; else 0. Per band the mean over its
// modes weighted by |coupling|^2 (Br, Bi hold the coupling until the contact ends).
void FastEngine::onsetPeaks(const Component& c, int slot) noexcept
{
    std::array<double, kExcitationBands> sum {}, w {};
    for (int i = 0; i < n_; ++i)
    {
        const size_t k = size_t(i);
        const double e = double(c.Br[k]) * c.Br[k] + double(c.Bi[k]) * c.Bi[k];
        if (! (e > 0.0))
            continue;
        double t = 0.0;
        if (c.envOn)
        {
            const size_t band = size_t(bake_->edgeNpz.bandIndex[k]);
            t = envPeakS_[band];
            if (c.envShift)
            {
                const size_t j = npzShiftBand_[band];
                t = std::max(0.0, static_cast<double>(c.envDelay[j]) + t / static_cast<double>(c.envSpeed[j]));
            }
        }
        else if (c.rampOn)
            t = c.pt[k];
        sum[modeBand_[k]] += e * t;
        w[modeBand_[k]] += e;
    }
    for (size_t j = 0; j < size_t(kExcitationBands); ++j)
        onsetPeakMs_[size_t(slot)][j] = w[j] > 0.0 ? static_cast<float>(1000.0 * sum[j] / w[j]) : -1.0f;
}
#endif

// The running stick clicks into both channels. A voice stops once its burst is over and its
// filter state has rung down below 1e-9 (in units of the noise), or a second after the burst.
void FastEngine::renderStick(float* left, float* right, int n) noexcept
{
    for (auto& v : stick_)
    {
        if (! v.on)
            continue;
        const double b00 = v.b0[0], b01 = v.b0[1], a10 = v.a1[0], a11 = v.a1[1], a20 = v.a2[0], a21 = v.a2[1];
        double s10 = v.s1[0], s20 = v.s2[0], s11 = v.s1[1], s21 = v.s2[1];
        int k = v.k;
        for (int j = 0; j < n; ++j, ++k)
        {
            const double x = k < v.len ? static_cast<double>(v.noise[k]) : 0.0;
            const double y0 = b00 * x + s10;
            s10 = s20 - a10 * y0;
            s20 = -b00 * x - a20 * y0;
            const double y1 = b01 * y0 + s11;
            s11 = s21 - a11 * y1;
            s21 = -b01 * y0 - a21 * y1;
            const float out = static_cast<float>(v.g * y1);
            left[j] += out;
            right[j] += out;
        }
        v.k = k;
        v.s1[0] = s10;
        v.s2[0] = s20;
        v.s1[1] = s11;
        v.s2[1] = s21;
        const double rest = std::fabs(s10) + std::fabs(s20) + std::fabs(s11) + std::fabs(s21);
        if (k >= v.len && (rest < 1e-9 || static_cast<double>(k - v.len) > sr_))
        {
            v.on = false;
            --stickActive_;
        }
    }
}

// Per-band ring lengths changed (setTuning), at a control point of the free phase: every
// strike is brought to this sample with the old decay and the segment starts again here, as
// at a strike without a contact, so the new decay applies from this sample on without a step.
void FastEngine::applyDecay() noexcept
{
    const double t = timeBase_ + static_cast<double>(sampleNow_) / sr_;
    rebaseComponents();
    cymbalWideAtStrike(t);
    takeDecay(pendDecay_);
    decayPending_ = false;
    // Each ringing strike's own decay with the new T60 (silent modes hold nothing).
    for (auto& c : components_)
        if (c.used && ! c.pending)
            for (int a = 0; a < nAct_; ++a)
                setDecay(c, actIdx_[size_t(a)]);
    // The cascade component can take energy in any mode later: all of them.
    if (cascComp_ >= 0 && components_[size_t(cascComp_)].used && components_[size_t(cascComp_)].cascade)
        for (int k = 0; k < n_; ++k)
            setDecay(components_[size_t(cascComp_)], k);
    if (cascOn_)
        computeBandRef();
    freeStarted_ = false;
    startFreePhase(true);
}

// Start of the free phase: the components take their amplitudes at this sample (finished
// contacts become components), the modes at or above the skip threshold (as Engine) get
// their shared oscillator, and the segment's cymbal-wide factors are set.
void FastEngine::startFreePhase(bool sameGrid) noexcept
{
    const BakeData& b = *bake_;
    const EngineParams& p = params_;
    const double threshold =
        b.ampSkipThreshold *
        (lastSlot_ >= 0 ? b.slots[size_t(lastSlot_)].calibScalar * slotTrim_[size_t(lastSlot_)] : 1.0);
    const int64_t m = sampleNow_ - baseSample_; // contact samples since the components' base

    // Older components step (undamped, as the contact phase did) to this sample; new ones
    // take their contact's response (coupling x H x step^extra, extra: samples since the
    // contact ended); then every component's own decay restarts here.
    CYM_PROF_BEGIN(freeStartRotate);
    bool older = false;
    for (const auto& c : components_)
        older = older || (c.used && !c.pending);
    if (m > 0 && older)
    {
        for (int k = 0; k < n_; ++k)
        {
            double ur, ui;
            complexPower(stepR_[size_t(k)], stepI_[size_t(k)], m, ur, ui);
            rotR_[size_t(k)] = static_cast<float>(ur);
            rotI_[size_t(k)] = static_cast<float>(ui);
        }
        for (auto& c : components_)
            if (c.used && !c.pending)
            {
                multiplyAmplitudes(c, rotR_.data(), rotI_.data());
                if (c.envOn)
                    multiplyAmplitudes(c.Er.data(), c.Ei.data(), rotR_.data(), rotI_.data());
            }
    }
    CYM_PROF_END(freeStartRotate);
    CYM_PROF_BEGIN(freeStartPending);
    for (auto& c : components_)
    {
        if (!c.used || !c.pending)
            continue;
        const int64_t extra = std::max<int64_t>(0, sampleNow_ - (c.contactStart + contactLen_));
        if (extra == 0)
        {
            multiplyAmplitudes(c, H32r_.data(), H32i_.data());
            continue;
        }
        for (int k = 0; k < n_; ++k)
        {
            const size_t s = size_t(k);
            double er, ei;
            complexPower(stepR_[s], stepI_[s], extra, er, ei);
            const double hr = Hr_[s] * er - Hi_[s] * ei, hi = Hr_[s] * ei + Hi_[s] * er;
            const double cr = c.Br[s], ci = c.Bi[s];
            c.Br[s] = static_cast<float>(cr * hr - ci * hi);
            c.Bi[s] = static_cast<float>(cr * hi + ci * hr);
        }
    }
    CYM_PROF_END(freeStartPending);
    CYM_PROF_BEGIN(freeStartDecay);
    const double tFree = timeBase_ + static_cast<double>(sampleNow_) / sr_;
    for (auto& c : components_)
    {
        if (!c.used)
            continue;
        if (c.pending)
        {
            for (int k = 0; k < n_; ++k)
                setDecay(c, k);
            c.pending = false;
            if (c.envOn) // the envelope scales what the contact left
            {
                std::copy(c.Br.begin(), c.Br.end(), c.Er.begin());
                std::copy(c.Bi.begin(), c.Bi.end(), c.Ei.begin());
            }
        }
        std::fill(c.D.begin(), c.D.end(), 1.0f);
        if (c.envOn && ! sameGrid) // the new control grid starts here: find this strike's frame phase on it
            restartFrames(c, tFree - c.tStrike);
        if (c.rhoOn && c.rhoT0 < 0.0) // a re-hit damping ramp starts with the free phase
            c.rhoT0 = tFree;
    }
    baseSample_ = sampleNow_;
    CYM_PROF_END(freeStartDecay);
    CYM_PROF_BEGIN(freeStartModes);

    segThreshold_ = threshold;
    const double tBase = timeBase_ + static_cast<double>(sampleNow_) / sr_;
    segTBase_ = tBase;
    applyPendingWander(tBase);
    const double K = static_cast<double>(K_);
    double gm1Max = 0.0;
    for (const auto& c : components_)
        if (c.used && c.onsetOn)
            gm1Max = std::max(gm1Max, c.gm1Max);
    segGm1Max_ = gm1Max;
    // Wander: the LFO time here, and the depth / rate since the latest strike, which the
    // wander phase is integrated with up to here. The first block sets the LFO steps.
    const double tau = wanderOn_ ? tauRef_ + tauRate_ * (tBase - tauRefTime_) : 0.0;
    const double gW = wanderOn_ ? wanderScale_ / tauRate_ : 0.0;
    blkRate_ = -1.0;
    // The largest LF/MF scales the segment can reach (for the drop bound).
    const double lfHi = lfOn_ ? std::max({1.0, p.lfMax, lfSm_}) : 1.0;
    const double mfHi = mfOn_ ? std::max({1.0, p.mfMax, mfSm_}) : 1.0;

    const int oldAct = nAct_;
    int a = 0;
    std::fill(actPos_.begin(), actPos_.end(), -1);
    const BelowHypot below(threshold);
    for (int i = 0; i < n_; ++i)
    {
        const size_t k = size_t(i);
        if (below(zr_[k], zi_[k]))
        {
            // Below the skip threshold for this segment (as Engine): what it holds is dropped.
            for (auto& c : components_)
                if (c.used)
                {
                    c.Br[k] = c.Bi[k] = 0.0f;
                    if (c.envOn)
                        c.Er[k] = c.Ei[k] = 0.0f;
                }
            continue;
        }
        const size_t d = size_t(a);
        actIdx_[d] = i;
        actPos_[k] = a;

        // LFO wander (lines 864-867), integrated: the wander phase continues from the latest
        // strike.
        double rotR = 1.0, rotI = 0.0;
        if (wanderOn_ && wC_[k] != 0.0)
        {
            const double arg = b.twoPi * lfoUnit_[k] * tau + b.lfoPhase[k];
            cr_[d] = std::cos(arg);
            ci_[d] = std::sin(arg);
            const double W = W_[k] + wC_[k] * gW * (cr_[d] - cW_[k]);
            phiW0_[d] = phiW_[d] = W;
            rotR = std::cos(W);
            rotI = std::sin(W);
        }
        else
        {
            // No wander: the phase stays where it is (0 unless the tuning switched wander off).
            cr_[d] = 1.0;
            ci_[d] = 0.0;
            phiW0_[d] = phiW_[d] = W_[k];
            if (W_[k] != 0.0)
            {
                rotR = std::cos(W_[k]);
                rotI = std::sin(W_[k]);
            }
        }
        qr_[d] = 1.0;
        qi_[d] = 0.0;
        // The shared oscillator starts at the wander phase; the components carry the amplitude.
        Yr_[d] = rotR;
        Yi_[d] = rotI;
        pl32_[d] = static_cast<float>(gainL_[k]);
        pr32_[d] = static_cast<float>(gainR_[k]);
        gBound_[d] = modeGain(k, lfHi, mfHi) * std::max(1.0, 1.0 + gm1Max) * outputBound(k);
        ++a;
    }
    const int end = std::min(nPad_, roundUp(std::max(oldAct, a), kGroup));
    for (int k = a; k < end; ++k)
    {
        const size_t s = size_t(k);
        yr_[s] = yi_[s] = sr32_[s] = si32_[s] = 0.0f;
        ar_[s] = ai_[s] = dar_[s] = dai_[s] = pl32_[s] = pr32_[s] = 0.0f;
    }
    nAct_ = a;
    nKinks_ = 0;
    // |d W / dt| <= 2 pi |drift x depth| x depth scale, so this bounds the phase step per block.
    wanderSmall_ = b.twoPi * maxDriftDepth_ * std::fabs(wanderScale_) * K / sr_ < kSmallAngle;
    freeStarted_ = true;
    freePos_ = 0;
    freeStartSample_ = sampleNow_;
    CYM_PROF_END(freeStartModes);
}

} // namespace cymbalis
