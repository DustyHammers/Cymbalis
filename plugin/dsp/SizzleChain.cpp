#include "SizzleChain.h"

#include "Bake.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

namespace cymbalis
{

namespace
{
typedef float v4f __attribute__ ((vector_size (16)));
inline v4f ld (const float* p) noexcept
{
    v4f v;
    std::memcpy (&v, p, sizeof v);
    return v;
}
inline void st (float* p, v4f v) noexcept { std::memcpy (p, &v, sizeof v); }
inline v4f splat (float x) noexcept { return v4f { x, x, x, x }; }
inline float hsum (v4f v) noexcept { return (v[0] + v[1]) + (v[2] + v[3]); }

constexpr double kPi = 3.14159265358979323846;
constexpr double kLn1000 = 6.907755278982137;
constexpr int roundUp4 (int n) { return (n + 3) & ~3; }

// The sizzle's level per one-third-octave band at env = 1, dB re ref_ms (Kris's Spizz 22" capture,
// the approved level included): bands 1000 x 2^(n / 3) Hz for n = 6 .. 12 (4 to 16 kHz). No
// sizzle below 3.5 kHz; between the centres, linear in dB over the band index.
constexpr double kBandDb[7] = { -12.1, -7.4, -8.1, -10.1, -12.2, -13.7, -17.3 };
constexpr int kFirstBand = 6, kLastBand = 12;
constexpr double kLowestHz = 3500.0, kHighestHz = 16000.0;

// The level curve at frequency f, dB (between the band centres, held beyond the ends).
double levelDbAt (double f) noexcept
{
    const double x = 3.0 * std::log2 (f / 1000.0);
    if (x <= kFirstBand)
        return kBandDb[0];
    if (x >= kLastBand)
        return kBandDb[kLastBand - kFirstBand];
    const int lo = (int) std::floor (x);
    const double t = x - lo;
    return kBandDb[lo - kFirstBand] + t * (kBandDb[lo + 1 - kFirstBand] - kBandDb[lo - kFirstBand]);
}

// The band a mode belongs to, 6 .. 12 (its nearest band centre, the 4 kHz band from 3.5 kHz).
int bandOf (double f) noexcept
{
    return std::clamp ((int) std::lround (3.0 * std::log2 (f / 1000.0)), kFirstBand, kLastBand);
}

// The swing factor's mean square, E[max(0, 1 + SWING u r)^2], for u of the swing's statistics
// (unit RMS, clipped to +-3; Gaussian here) and r = max(0, 1 + 0.7 z) (z Gaussian), independent.
// The bands are calibrated at env = D g = 1 (Kris's C_b were measured on real sizzle, its flicker
// included), so the sound divides the factor by the root of this and the swing changes the
// flicker but not the level. For u: E[max(0, 1 + a u)^2] in closed form over the Gaussian, with
// the mass beyond +-3 at +-3; for r: Simpson's rule over z (with the mass of r = 0 apart).
double swingMeanSquare (double swing) noexcept
{
    if (! (swing > 0.0))
        return 1.0;
    const auto phi = [] (double x) { return 0.3989422804014327 * std::exp (-0.5 * x * x); };
    const auto cdf = [] (double x) { return 0.5 * std::erfc (-x * 0.7071067811865476); };
    const double tail = cdf (-3.0), phi3 = phi (3.0), cdf3 = cdf (3.0);
    const auto overU = [&] (double a) {
        if (a <= 0.0)
            return 1.0;
        const double c = std::max (-3.0, -1.0 / a), pc = phi (c), lo = 1.0 - 3.0 * a, hi = 1.0 + 3.0 * a;
        return (1.0 + a * a) * (cdf3 - cdf (c)) + 2.0 * a * (pc - phi3) + a * a * (c * pc - 3.0 * phi3)
               + tail * (hi * hi + (lo > 0.0 ? lo * lo : 0.0));
    };
    const double z0 = -1.0 / 0.7, z1 = 8.0; // r = 0 below z0
    constexpr int steps = 600;              // even
    const double h = (z1 - z0) / steps;
    double sum = 0.0;
    for (int i = 0; i <= steps; ++i)
    {
        const double z = z0 + h * i;
        const double w = i == 0 || i == steps ? 1.0 : (i % 2 == 1 ? 4.0 : 2.0);
        sum += w * phi (z) * overU (swing * (1.0 + 0.7 * z));
    }
    return cdf (z0) + sum * h / 3.0;
}

std::uint64_t splitMix (std::uint64_t& x) noexcept
{
    std::uint64_t z = (x += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}
} // namespace

double SizzleChain::nextUniform() noexcept
{
    // xorshift128+, its top 53 bits as [0, 1), to -sqrt(3) .. sqrt(3).
    std::uint64_t x = s0_;
    const std::uint64_t y = s1_;
    s0_ = y;
    x ^= x << 23;
    s1_ = x ^ y ^ (x >> 17) ^ (y >> 26);
    const double u = (double) ((s1_ + y) >> 11) * (1.0 / 9007199254740992.0);
    return (2.0 * u - 1.0) * 1.7320508075688772;
}

void SizzleChain::build (const BakeData& b, std::uint64_t seed, double refMs)
{
    n_ = b.numModes;
    refMs_ = std::max (refMs, 0.0);
    freq_ = b.freqHz;
    excNorm_ = b.excNorm;
    damp_.assign ((size_t) n_, 1.0);
    rateScratch_.assign ((size_t) n_, 0.0);
    gainL_.assign ((size_t) n_, 0.0);
    gainR_.assign ((size_t) n_, 0.0);
    negLambda_.assign ((size_t) n_, -kLn1000);

    // The drive's modes: under 1 kHz, weighted 300 Hz / f.
    loIdx_.clear();
    loW_.clear();
    for (int k = 0; k < n_; ++k)
        if (freq_[(size_t) k] < 1000.0 && freq_[(size_t) k] > 0.0)
        {
            loIdx_.push_back (k);
            loW_.push_back ((float) (300.0 / freq_[(size_t) k]));
        }
    nLo_ = (int) loIdx_.size();
    nLoPad_ = roundUp4 (std::max (nLo_, 1));
    loW_.resize ((size_t) nLoPad_, 0.0f);
    recC_.assign ((size_t) (kMaxStrikes * nLoPad_), 0.0f);
    recM_.assign ((size_t) (kMaxStrikes * nLoPad_), 0.0f);
    recUsed_.fill (false);

    // The swing's modes: the kSwingModes low modes with the largest weighted coupling (exc_norm x
    // 300 Hz / f), in mode order.
    std::vector<int> order (loIdx_);
    std::stable_sort (order.begin(), order.end(), [this] (int a, int c) {
        return excNorm_[(size_t) a] * 300.0 / freq_[(size_t) a] > excNorm_[(size_t) c] * 300.0 / freq_[(size_t) c];
    });
    order.resize ((size_t) std::min ((int) order.size(), kSwingModes));
    std::sort (order.begin(), order.end());
    swingIdx_ = order;
    nSwing_ = (int) swingIdx_.size();

    // The resonators: the modes from 3.5 to 16 kHz, each with a Rayleigh weight (sigma 1) from
    // the seeded generator, in mode order.
    std::uint64_t sm = seed;
    s0_ = splitMix (sm);
    s1_ = splitMix (sm);
    resIdx_.clear();
    resWeight_.clear();
    for (int k = 0; k < n_; ++k)
        if (freq_[(size_t) k] >= kLowestHz && freq_[(size_t) k] <= kHighestHz)
        {
            resIdx_.push_back (k);
            const double u = (nextUniform() / 1.7320508075688772 + 1.0) * 0.5; // [0, 1)
            resWeight_.push_back (std::sqrt (-2.0 * std::log (1.0 - u)));     // (0, 1]
        }
    numRes_ = (int) resIdx_.size();
    numResPad_ = roundUp4 (std::max (numRes_, 1));
    for (auto* v : { &a1_, &a2_, &y1_, &y2_, &cl_, &cr_ })
        v->assign ((size_t) numResPad_, 0.0f);
    built_ = true;
    setSettings (settings_);
}

double SizzleChain::strikeWeight (double position, double velocity) const noexcept
{
    const double p = std::clamp (position, 0.0, 1.0);
    const auto& s = settings_;
    const double db = p <= 0.5 ? s.chainEdgeDb + (s.chainBowDb - s.chainEdgeDb) * (p / 0.5)
                               : s.chainBowDb + (s.chainBellDb - s.chainBowDb) * ((p - 0.5) / 0.5);
    const double v = std::pow (std::max (velocity, 0.0) / 0.9, s.chainVelocity);
    const double w = std::pow (10.0, db / 20.0) * v;
    return w * w; // the strike's d^2 at 0.5 s
}

void SizzleChain::setOn (bool on) noexcept
{
    if (on == on_)
        return;
    on_ = on;
    frozen_ = false;
    if (on || ! built_)
        return;
    // Off: the envelope from here to the fade's end is D g at the switch, its value for the next
    // sample (the current block's line; at a block's end, the line's end): what is struck from
    // here on is tracked for a switch back on but does not reach the fading sizzle.
    const int done = kBlock - blockLeft_;
    const double t = blockLeft_ > 0 ? std::min (1.0, (double) (done + 1) / kBlock) : 1.0;
    frozenEnv_ = env0_ + (env1_ - env0_) * t;
    frozen_ = true;
}

void SizzleChain::prepare (double sampleRate, const double* negLambda, const double* gainL, const double* gainR) noexcept
{
    if (! built_)
        return;
    sr_ = sampleRate;
    fadeStep_ = 1.0 / (kFadeS * sr_);
    std::copy (negLambda, negLambda + n_, negLambda_.begin());
    std::copy (gainL, gainL + n_, gainL_.begin());
    std::copy (gainR, gainR + n_, gainR_.begin());
    for (int j = 0; j < nSwing_; ++j)
    {
        const double w = 2.0 * kPi * freq_[(size_t) swingIdx_[(size_t) j]] / sr_;
        swRotRe_[(size_t) j] = std::cos (w);
        swRotIm_[(size_t) j] = std::sin (w);
        swRe_[(size_t) j] = swIm_[(size_t) j] = 0.0;
        swDec_[(size_t) j] = 1.0;
    }
    // r(t)'s low-pass: 2nd-order Butterworth at 25 Hz (bilinear, prewarped), and its output's
    // standard deviation for unit-variance white noise from its impulse response.
    {
        const double w0 = 2.0 * kPi * 25.0 / sr_, cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * std::sqrt (0.5));
        const double a0 = 1.0 + alpha;
        lpB0_ = (1.0 - cw) * 0.5 / a0;
        lpB1_ = (1.0 - cw) / a0;
        lpB2_ = lpB0_;
        lpA1_ = -2.0 * cw / a0;
        lpA2_ = (1.0 - alpha) / a0;
        double z1 = 0.0, z2 = 0.0, energy = 0.0;
        for (int i = 0; i < (int) (2.0 * sr_); ++i)
        {
            const double x = i == 0 ? 1.0 : 0.0;
            const double y = lpB0_ * x + z1;
            z1 = lpB1_ * x - lpA1_ * y + z2;
            z2 = lpB2_ * x - lpA2_ * y;
            energy += y * y;
        }
        lpScale_ = 0.7 / std::sqrt (energy);
        lpZ1_ = lpZ2_ = 0.0;
    }
    recUsed_.fill (false);
    std::fill (recC_.begin(), recC_.end(), 0.0f);
    std::fill (y1_.begin(), y1_.end(), 0.0f);
    std::fill (y2_.begin(), y2_.end(), 0.0f);
    resonating_ = false;
    quietBlocks_ = 0;
    fade_ = on_ ? 1.0 : 0.0;
    frozen_ = false;
    blockLeft_ = 0;
    env0_ = env1_ = 0.0;
    driveNow_ = gateNow_ = 0.0;
    lastEnv_ = 0.0f;
    computeResonators();
}

void SizzleChain::setSettings (const ChainSettings& s) noexcept
{
    settings_ = s;
    if (! built_)
        return;
    if (s.chainSwing != swingNormFor_)
    {
        swingNormFor_ = s.chainSwing;
        swingNorm_ = s.chainSwing > 0.0 ? 1.0 / std::sqrt (swingMeanSquare (s.chainSwing)) : 1.0;
    }
    for (int k = 0; k < n_; ++k)
        damp_[(size_t) k] = 1.0 + s.chainDamping * std::pow (freq_[(size_t) k] / 4000.0, 1.5);
    computeWeights();
    if (sr_ > 0.0)
        computeResonators();
}

void SizzleChain::setDecay (const double* negLambda) noexcept
{
    if (! built_)
        return;
    std::copy (negLambda, negLambda + n_, negLambda_.begin());
    if (sr_ > 0.0)
        computeResonators();
}

void SizzleChain::setGains (const double* gainL, const double* gainR) noexcept
{
    if (! built_)
        return;
    std::copy (gainL, gainL + n_, gainL_.begin());
    std::copy (gainR, gainR + n_, gainR_.begin());
    if (sr_ > 0.0)
        computeResonators();
}

void SizzleChain::computeWeights() noexcept
{
    for (int s = 0; s < kMaxStrikes; ++s)
        if (recUsed_[(size_t) s])
            recWeight_[(size_t) s] = strikeWeight (recPos_[(size_t) s], recVel_[(size_t) s]);
}

// Each resonator's coefficients (its mode's chain-damped t60, at most 0.6 s) and its output gains:
// per band, the gain that makes the band's power at env = 1 (mono mean of L and R, unit-variance
// white noise in) C_b x ref_ms x SIZZLE LEVEL, the modes within the band shaped by the level curve
// and weighted by their Rayleigh weights.
void SizzleChain::computeResonators() noexcept
{
    std::array<double, kLastBand + 1> bandSum {};
    std::vector<double>& power = rateScratch_; // per resonator: its power for unit-variance white noise
    for (int r = 0; r < numRes_; ++r)
    {
        const size_t k = (size_t) resIdx_[(size_t) r];
        const double f = freq_[k];
        const double t60 = std::min (-kLn1000 / negLambda_[k] / damp_[k], kMaxT60S);
        const double rr = std::exp (-kLn1000 / (t60 * sr_)), c = std::cos (2.0 * kPi * f / sr_);
        a1_[(size_t) r] = (float) (2.0 * rr * c);
        a2_[(size_t) r] = (float) (-rr * rr);
        const double r2 = rr * rr;
        const double p = (1.0 + r2) / ((1.0 - r2) * ((1.0 + r2) * (1.0 + r2) - 4.0 * r2 * c * c));
        power[(size_t) r] = p;
        const int band = bandOf (f);
        const double shape = std::pow (10.0, (levelDbAt (f) - kBandDb[band - kFirstBand]) / 20.0);
        const double mono = 0.5 * (gainL_[k] + gainR_[k]);
        const double g = resWeight_[(size_t) r] * shape * mono;
        bandSum[(size_t) band] += g * g * p;
    }
    const double level = std::pow (10.0, settings_.chainLevelDb / 10.0);
    std::array<double, kLastBand + 1> bandGain {};
    for (int band = kFirstBand; band <= kLastBand; ++band)
    {
        const double target = std::pow (10.0, kBandDb[band - kFirstBand] / 10.0) * refMs_ * level;
        bandGain[(size_t) band] = bandSum[(size_t) band] > 1e-30 ? std::sqrt (target / bandSum[(size_t) band]) : 0.0;
    }
    for (int r = 0; r < numRes_; ++r)
    {
        const size_t k = (size_t) resIdx_[(size_t) r];
        const double f = freq_[k];
        const int band = bandOf (f);
        const double shape = std::pow (10.0, (levelDbAt (f) - kBandDb[band - kFirstBand]) / 20.0);
        const double g = bandGain[(size_t) band] * resWeight_[(size_t) r] * shape;
        cl_[(size_t) r] = (float) (g * gainL_[k]);
        cr_[(size_t) r] = (float) (g * gainR_[k]);
    }
}

void SizzleChain::strike (double position, double velocity, const float* re, const float* im, const double* rate) noexcept
{
    if (! built_ || sr_ <= 0.0 || nLo_ == 0)
        return;
    // Its displacement^2 at 0.5 s: sum over the low modes of (300 Hz / f x |a|)^2 e^(-2 rate 0.5).
    double half = 0.0;
    for (int i = 0; i < nLo_; ++i)
    {
        const size_t k = (size_t) loIdx_[(size_t) i];
        const double w = loW_[(size_t) i], a2 = (double) re[k] * re[k] + (double) im[k] * im[k];
        half += w * w * a2 * std::exp (-rate[k]);
    }
    if (! (half > 0.0))
        return;
    // A record: the first free one, else the weakest now.
    int slot = -1;
    double weakest = 0.0;
    for (int s = 0; s < kMaxStrikes; ++s)
    {
        if (! recUsed_[(size_t) s])
        {
            slot = s;
            break;
        }
        const double d2 = recWeight_[(size_t) s] * recSum_[(size_t) s];
        if (slot < 0 || d2 < weakest)
        {
            slot = s;
            weakest = d2;
        }
    }
    float* c = recC_.data() + (size_t) slot * (size_t) nLoPad_;
    float* m = recM_.data() + (size_t) slot * (size_t) nLoPad_;
    double sum = 0.0;
    const double blockS = (double) kBlock / sr_;
    for (int i = 0; i < nLo_; ++i)
    {
        const size_t k = (size_t) loIdx_[(size_t) i];
        const double w = loW_[(size_t) i], a2 = (double) re[k] * re[k] + (double) im[k] * im[k];
        const double ci = w * w * a2 / half;
        c[i] = (float) ci;
        m[i] = (float) std::exp (-2.0 * rate[k] * blockS);
        sum += ci;
    }
    for (int i = nLo_; i < nLoPad_; ++i)
        c[i] = m[i] = 0.0f;
    recSum_[(size_t) slot] = sum;
    recPos_[(size_t) slot] = position;
    recVel_[(size_t) slot] = velocity;
    recWeight_[(size_t) slot] = strikeWeight (position, velocity);
    recUsed_[(size_t) slot] = true;
    // The swing: its modes take this strike's weighted amplitudes, scaled as its drive, and its
    // decay.
    const double scale = std::sqrt (recWeight_[(size_t) slot] / half);
    for (int j = 0; j < nSwing_; ++j)
    {
        const size_t k = (size_t) swingIdx_[(size_t) j];
        const double w = 300.0 / freq_[k];
        swRe_[(size_t) j] += scale * w * re[k];
        swIm_[(size_t) j] += scale * w * im[k];
        swDec_[(size_t) j] = std::exp (-rate[k] / sr_);
    }
}

// The drive and gate at the current block's ends: the records' sums now, then one block of decay.
void SizzleChain::advanceBlock() noexcept
{
    const auto driveGate = [this] {
        double d2 = 0.0;
        for (int s = 0; s < kMaxStrikes; ++s)
            if (recUsed_[(size_t) s])
                d2 += recWeight_[(size_t) s] * recSum_[(size_t) s];
        const double d = std::sqrt (d2);
        const double g = d > 0.0 ? std::clamp ((20.0 * std::log10 (d) - settings_.chainStopDb) / 6.0, 0.0, 1.0) : 0.0;
        return std::array<double, 2> { d, g };
    };
    const auto start = driveGate();
    for (int s = 0; s < kMaxStrikes; ++s)
    {
        if (! recUsed_[(size_t) s])
            continue;
        float* c = recC_.data() + (size_t) s * (size_t) nLoPad_;
        const float* m = recM_.data() + (size_t) s * (size_t) nLoPad_;
        v4f acc = splat (0.0f);
        for (int i = 0; i < nLoPad_; i += 4)
        {
            const v4f x = ld (c + i) * ld (m + i);
            st (c + i, x);
            acc += x;
        }
        recSum_[(size_t) s] = (double) hsum (acc);
        // Dropped once it is 120 dB under a full hit's drive at 0.5 s.
        if (recWeight_[(size_t) s] * recSum_[(size_t) s] < 1e-12)
            recUsed_[(size_t) s] = false;
    }
    const auto end = driveGate();
    env0_ = start[0] * start[1];
    env1_ = end[0] * end[1];
    driveNow_ = end[0];
    gateNow_ = end[1];
}

bool SizzleChain::isSilent() const noexcept
{
    return ! built_ || (fade_ <= 0.0 && ! on_) || ! resonating_;
}

void SizzleChain::process (float* left, float* right, int n) noexcept
{
    if (! built_ || sr_ <= 0.0)
        return;
    const auto& s = settings_;
    double envSum = 0.0;
    int envCount = 0;
    float x[kBlock];
    float fd[kBlock];
    v4f accL[kBlock], accR[kBlock];
    int pos = 0;
    while (pos < n)
    {
        if (blockLeft_ == 0)
        {
            advanceBlock();
            blockLeft_ = kBlock;
        }
        const int m = std::min (blockLeft_, n - pos);
        const int done = kBlock - blockLeft_; // samples of this block already rendered
        const bool active = on_ || fade_ > 0.0;
        // The swing's RMS over its modes now.
        double rms2 = 0.0;
        for (int j = 0; j < nSwing_; ++j)
            rms2 += 0.5 * (swRe_[(size_t) j] * swRe_[(size_t) j] + swIm_[(size_t) j] * swIm_[(size_t) j]);
        const double invRms = rms2 > 1e-30 ? 1.0 / std::sqrt (rms2) : 0.0;
        for (int i = 0; i < m; ++i)
        {
            // u(t): the plate's displacement now, over its RMS, clipped; its modes move on.
            double sum = 0.0;
            for (int j = 0; j < nSwing_; ++j)
            {
                const size_t q = (size_t) j;
                sum += swRe_[q];
                const double re = (swRe_[q] * swRotRe_[q] - swIm_[q] * swRotIm_[q]) * swDec_[q];
                swIm_[q] = (swRe_[q] * swRotIm_[q] + swIm_[q] * swRotRe_[q]) * swDec_[q];
                swRe_[q] = re;
            }
            if (! active)
                continue;
            if (frozen_)
            {
                // Fading out: the envelope frozen at the switch (no swing: its mean square is 1).
                envSum += frozenEnv_;
                ++envCount;
                x[i] = (float) (nextUniform() * frozenEnv_);
                fade_ = std::max (0.0, fade_ - fadeStep_);
                fd[i] = (float) fade_;
                continue;
            }
            const double u = std::clamp (sum * invRms, -3.0, 3.0);
            // r(t): the low-passed noise, mean 1, standard deviation 0.7, clipped at 0.
            const double noise = nextUniform();
            const double y = lpB0_ * noise + lpZ1_;
            lpZ1_ = lpB1_ * noise - lpA1_ * y + lpZ2_;
            lpZ2_ = lpB2_ * noise - lpA2_ * y;
            const double r = std::max (0.0, 1.0 + lpScale_ * y);
            const double t = (double) (done + i + 1) / kBlock;
            const double dg = env0_ + (env1_ - env0_) * t;
            const double e = dg * std::max (0.0, 1.0 + s.chainSwing * u * r);
            envSum += e; // the drawing's envelope: the swing as it is
            ++envCount;
            x[i] = (float) (nextUniform() * e * swingNorm_); // the sound's: its swing of mean square 1
            // The fade, to 1 when on and to 0 when off, 50 ms.
            fade_ = on_ ? std::min (1.0, fade_ + fadeStep_) : std::max (0.0, fade_ - fadeStep_);
            fd[i] = (float) fade_;
        }
        if (active)
        {
            bool any = false;
            for (int i = 0; i < m; ++i)
            {
                accL[i] = accR[i] = splat (0.0f);
                any = any || x[i] != 0.0f;
            }
            if (any || resonating_)
            {
                for (int r = 0; r < numResPad_; r += 4)
                {
                    const v4f a1 = ld (a1_.data() + r), a2 = ld (a2_.data() + r), gl = ld (cl_.data() + r), gr = ld (cr_.data() + r);
                    v4f y1 = ld (y1_.data() + r), y2 = ld (y2_.data() + r);
                    for (int i = 0; i < m; ++i)
                    {
                        const v4f y0 = a1 * y1 + a2 * y2 + splat (x[i]);
                        y2 = y1;
                        y1 = y0;
                        accL[i] += gl * y0;
                        accR[i] += gr * y0;
                    }
                    st (y1_.data() + r, y1);
                    st (y2_.data() + r, y2);
                }
                for (int i = 0; i < m; ++i)
                {
                    left[pos + i] += fd[i] * hsum (accL[i]);
                    right[pos + i] += fd[i] * hsum (accR[i]);
                }
                resonating_ = true;
            }
            quietBlocks_ = any ? 0 : quietBlocks_;
        }
        blockLeft_ -= m;
        pos += m;
        if (blockLeft_ == 0 && resonating_)
        {
            // The resonators ring out: once off and faded, or 1.2 s (120 dB at a 0.6 s t60) with
            // nothing in, their state is cleared.
            if (env0_ == 0.0 && env1_ == 0.0)
                ++quietBlocks_;
            if ((! on_ && fade_ <= 0.0) || (double) quietBlocks_ * kBlock > 2.0 * kMaxT60S * sr_)
            {
                std::fill (y1_.begin(), y1_.end(), 0.0f);
                std::fill (y2_.begin(), y2_.end(), 0.0f);
                resonating_ = false;
                quietBlocks_ = 0;
            }
        }
    }
    lastEnv_ = envCount > 0 ? (float) (envSum / envCount) : 0.0f;
}

} // namespace cymbalis
