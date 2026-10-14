#include "FastEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cymbalis
{

namespace
{
// 128-bit float vectors (GCC/Clang vector extension): NEON on arm64, SSE on x86_64.
// Nothing here needs AVX.
typedef float v4f __attribute__((vector_size(16)));
typedef double v2d __attribute__((vector_size(16)));

inline v4f ld(const float* p) noexcept
{
    v4f v;
    std::memcpy(&v, p, sizeof v);
    return v;
}

inline void st(float* p, v4f v) noexcept { std::memcpy(p, &v, sizeof v); }

inline v2d ld2(const double* p) noexcept
{
    v2d v;
    std::memcpy(&v, p, sizeof v);
    return v;
}

inline void st2(double* p, v2d v) noexcept { std::memcpy(p, &v, sizeof v); }

constexpr int kGroup = 16;            // nPad_ is a multiple of this (and of every kernel group)
constexpr double kHpCutoffHz = 120.0; // render() line 703: _hp(out[:,1], 120), 2nd-order Butterworth
constexpr int kDropEvery = 8;         // control blocks between checks for modes below the threshold
constexpr double kSmallAngle = 0.05;  // the series below are used for wander steps under this (rad)

// Vectors of 4 modes per kernel group: enough independent oscillators to hide the
// multiply latency without running out of registers (16 SSE registers, 32 NEON).
#if defined(__aarch64__) || defined(__arm64__)
constexpr int kVecPerGroup = 4;
#else
constexpr int kVecPerGroup = 2;
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
} // namespace

FastEngine::FastEngine(std::shared_ptr<const BakeData> bake) : bake_(std::move(bake)), n_(bake_->numModes)
{
    const BakeData& b = *bake_;
    const EngineParams& p = b.params;
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
    for (size_t i = 0; i < n; ++i)
    {
        omega_[i] = b.twoPi * b.freqHz[i];
        cosLp_[i] = std::cos(b.lfoPhase[i]);
    }
    beoPkT_.assign(n, 0.0);
    scratchBandScale_.assign(size_t(std::max(b.velTiltBands, 1)), 1.0);
    if (p.contactPermode)
    {
        contactPool_.resize(kMaxContacts);
        for (auto& c : contactPool_)
        {
            c.cr.assign(n, 0.0);
            c.ci.assign(n, 0.0);
        }
    }
    actIdx_.assign(np, 0);
    for (auto* v : {&yr_, &yi_, &sr32_, &si32_, &wl_, &wr_, &dwl_, &dwr_})
        v->assign(np, 0.0f);
    for (auto* v : {&Yr_, &Yi_, &p1r_, &p1i_, &PKr_, &PKi_, &cr_, &ci_, &qr_, &qi_, &phiW_, &negDlr_, &cosLpA_,
                    &pl_, &pr_, &mf_, &pt_, &invPt_, &bfA_, &gm1_, &gBound_})
        v->assign(np, 0.0);
    kinks_.resize(np);
    accL_.assign(size_t(kMaxControlBlock) * 4, 0.0f);
    accR_.assign(size_t(kMaxControlBlock) * 4, 0.0f);
    scratchPhase_.resize(n);
    scratchVel_.resize(n);
    scratchBellVel_.resize(n);
    scratchPosW_.resize(n);
    rng_ = b.renderStream; // plugin load: the strike-phase sequence starts here
}

void FastEngine::prepare(double sampleRate)
{
    const BakeData& b = *bake_;
    const EngineParams& p = b.params;
    const size_t n = size_t(n_);
    if (sr_ > 0.0)
    {
        timeBase_ += static_cast<double>(clock_) / sr_;
        clock_ = 0;
    }
    sr_ = sampleRate;
    // About 0.7 ms between control points at every supported rate.
    K_ = sr_ > 50000.0 ? 64 : 32;

    // Per-sample steps at the host rate.
    stepR_.resize(n);
    stepI_.resize(n);
    rot1R_.resize(n);
    rot1I_.resize(n);
    for (size_t i = 0; i < n; ++i)
    {
        const double ph = omega_[i] / sr_;
        rot1R_[i] = std::cos(ph);
        rot1I_[i] = std::sin(ph);
        const double e = std::exp(-b.log1000 / (b.t60S[i] * sr_));
        stepR_[i] = rot1R_[i] * e;
        stepI_[i] = rot1I_[i] * e;
    }

    // Contact force (_apply_strike lines 1073-1091). The taus and sigmas are samples at the
    // bake's rate (44.1 kHz); here they are converted to samples at the host rate, so the
    // contact keeps its duration in seconds. At 44.1 kHz this is Python's matrix.
    force_.clear();
    contactLen_ = 0;
    if (p.contactPermode)
    {
        const double ratio = sr_ / b.sampleRate;
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
        contactLen_ = static_cast<int>(tauM * ratio + 3.0 * sigMax * ratio) + 1;
        const size_t len = size_t(contactLen_);
        // Stored sample-major (len x numModes) so each contact sample reads one contiguous row.
        force_.assign(len * n, 0.0);
        std::vector<double> row(len);
        for (size_t i = 0; i < n; ++i)
        {
            double sum = 0.0;
            for (size_t k = 0; k < len; ++k)
            {
                const double x = (static_cast<double>(k) - tau[i]) / sig[i];
                row[k] = std::exp(-0.5 * (x * x));
                sum += row[k];
            }
            const double denom = sum + 1e-9;
            for (size_t k = 0; k < len; ++k)
                force_[k * n + i] = row[k] / denom;
        }
    }

    butterworthHighPass2(kHpCutoffHz, sr_, hpB0_, hpB1_, hpB2_, hpA1_, hpA2_);

    // Silence: no ringing, no running contact, a fresh (silent) segment.
    std::fill(zr_.begin(), zr_.end(), 0.0);
    std::fill(zi_.begin(), zi_.end(), 0.0);
    activeContacts_ = 0;
    hpZ0_ = hpZ1_ = 0.0;
    segStartTime_ = timeSeconds();
    segContactSamples_ = 0;
    startFreePhase();
}

double FastEngine::timeSeconds() const noexcept
{
    return sr_ > 0.0 ? timeBase_ + static_cast<double>(clock_) / sr_ : timeBase_;
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
    int pos = 0;
    while (pos < numSamples)
    {
        if (activeContacts_ > 0)
        {
            renderContactSample(left + pos, right + pos);
            ++pos;
        }
        else
        {
            renderFree(left + pos, right + pos, numSamples - pos);
            pos = numSamples;
        }
    }

    // Right-channel high-pass (render() line 703), direct form II transposed, in double.
    double z0 = hpZ0_, z1 = hpZ1_;
    for (int j = 0; j < numSamples; ++j)
    {
        const double x = right[j];
        const double y = hpB0_ * x + z0;
        z0 = hpB1_ * x - hpA1_ * y + z1;
        z1 = hpB2_ * x - hpA2_ * y;
        right[j] = static_cast<float>(y);
    }
    hpZ0_ = z0;
    hpZ1_ = z1;
    clock_ += static_cast<uint64_t>(numSamples);
}

// Contact phase, one sample, as Engine::renderContactSample, with the host-rate force and step.
void FastEngine::renderContactSample(float* left, float* right) noexcept
{
    const BakeData& b = *bake_;
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
    // Output Re(state) . pick, then one undamped step. Partial sums in two 2-lane vectors.
    const double* __restrict pickL = b.pickL.data();
    const double* __restrict pickR = b.pickR.data();
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
    ++segContactSamples_;
    if (activeContacts_ == 0)
        startFreePhase();
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
            beginControlBlock();
        const int m = std::min(K_ - j, n - pos);
        std::fill(accL_.begin(), accL_.begin() + 4 * m, 0.0f);
        std::fill(accR_.begin(), accR_.begin() + 4 * m, 0.0f);
        if (nAct_ > 0)
        {
            if (flatGain_)
                kernel<kVecPerGroup, false>(m);
            else
                kernel<kVecPerGroup, true>(m);
        }
        for (int k = 0; k < m; ++k)
        {
            const float* a = accL_.data() + 4 * k;
            const float* c = accR_.data() + 4 * k;
            left[pos + k] = (a[0] + a[1]) + (a[2] + a[3]);
            right[pos + k] = (c[0] + c[1]) + (c[2] + c[3]);
        }
        if (nKinks_ > 0)
            renderKinks(left + pos, right + pos, j, m);
        freePos_ += m;
        pos += m;
    }
}

// m samples of every ringing mode. Lanes are modes; G vectors (4G modes) per group keep
// enough independent oscillators in flight. Per mode and sample:
//   out += Re(y) * w;  y *= s;  w += dw (Ramp only)
template <int G, bool Ramp>
void FastEngine::kernel(int m) noexcept
{
    const int groups = (nAct_ + 4 * G - 1) / (4 * G);
    float* accL = accL_.data();
    float* accR = accR_.data();
    for (int g = 0; g < groups; ++g)
    {
        const size_t o = size_t(g) * 4 * G;
        v4f yr[G], yi[G], sr[G], si[G], wl[G], wr[G], dl[G], dr[G];
        for (int q = 0; q < G; ++q)
        {
            const size_t k = o + size_t(4 * q);
            yr[q] = ld(&yr_[k]);
            yi[q] = ld(&yi_[k]);
            sr[q] = ld(&sr32_[k]);
            si[q] = ld(&si32_[k]);
            wl[q] = ld(&wl_[k]);
            wr[q] = ld(&wr_[k]);
            if (Ramp)
            {
                dl[q] = ld(&dwl_[k]);
                dr[q] = ld(&dwr_[k]);
            }
        }
        for (int j = 0; j < m; ++j)
        {
            v4f al = yr[0] * wl[0];
            v4f ar = yr[0] * wr[0];
            for (int q = 1; q < G; ++q)
            {
                al += yr[q] * wl[q];
                ar += yr[q] * wr[q];
            }
            st(accL + 4 * j, ld(accL + 4 * j) + al);
            st(accR + 4 * j, ld(accR + 4 * j) + ar);
            for (int q = 0; q < G; ++q)
            {
                const v4f t = yr[q] * sr[q] - yi[q] * si[q];
                yi[q] = yr[q] * si[q] + yi[q] * sr[q];
                yr[q] = t;
                if (Ramp)
                {
                    wl[q] += dl[q];
                    wr[q] += dr[q];
                }
            }
        }
        for (int q = 0; q < G; ++q)
        {
            const size_t k = o + size_t(4 * q);
            st(&yr_[k], yr[q]);
            st(&yi_[k], yi[q]);
            if (Ramp)
            {
                st(&wl_[k], wl[q]);
                st(&wr_[k], wr[q]);
            }
        }
    }
}

// The modes whose ramp corner lies in this block, samples j0 .. j0+m of the block, with
// the ramp evaluated per sample (lines 896-901) and the onset envelope interpolated.
void FastEngine::renderKinks(float* left, float* right, int j0, int m) noexcept
{
    const double bf = bake_->params.birthFrac;
    const double invK = 1.0 / static_cast<double>(K_);
    for (int q = 0; q < nKinks_; ++q)
    {
        KinkMode& km = kinks_[size_t(q)];
        double yr = km.yr, yi = km.yi;
        for (int j = 0; j < m; ++j)
        {
            const int jb = j0 + j;
            const double te = blockTe0_ + static_cast<double>(jb) / sr_;
            double g = km.mf * std::max(std::min(1.0, bf + ((1.0 - bf) * te) / km.pt), 0.0);
            if (km.gm1 != 0.0)
                g *= 1.0 + km.gm1 * (blockEnv0_ + (blockEnv1_ - blockEnv0_) * (static_cast<double>(jb) * invK));
            left[j] += static_cast<float>(yr * km.pl * g);
            right[j] += static_cast<float>(yr * km.pr * g);
            const double t = yr * km.sr - yi * km.si;
            yi = yr * km.si + yi * km.sr;
            yr = t;
        }
        km.yr = yr;
        km.yi = yi;
    }
}

// Every kDropEvery blocks: modes whose amplitude, times the largest gain they can still
// get, is below the skip threshold stop being rendered. |Y| only decays, so they never
// come back within the segment. The others are compacted to the front.
void FastEngine::dropQuietModes() noexcept
{
    const double thr2 = segThreshold_ * segThreshold_;
    int w = 0;
    for (int r = 0; r < nAct_; ++r)
    {
        const size_t s = size_t(r);
        if ((Yr_[s] * Yr_[s] + Yi_[s] * Yi_[s]) * (gBound_[s] * gBound_[s]) < thr2)
            continue;
        if (w != r)
        {
            const size_t d = size_t(w);
            actIdx_[d] = actIdx_[s];
            for (auto* v : {&Yr_, &Yi_, &p1r_, &p1i_, &PKr_, &PKi_, &cr_, &ci_, &qr_, &qi_, &phiW_, &negDlr_,
                            &cosLpA_, &pl_, &pr_, &mf_, &pt_, &invPt_, &bfA_, &gm1_, &gBound_})
                (*v)[d] = (*v)[s];
        }
        ++w;
    }
    const int end = std::min(nPad_, roundUp(nAct_, kGroup));
    for (int k = w; k < end; ++k)
    {
        const size_t s = size_t(k);
        yr_[s] = yi_[s] = sr32_[s] = si32_[s] = 0.0f;
        wl_[s] = wr_[s] = dwl_[s] = dwr_[s] = 0.0f;
    }
    nAct_ = w;
}

// Control point at the start of a block. Per ringing mode: the oscillator step for the
// block (decay and frequency, times the wander phase increment spread over the block),
// the output weights at both ends of the block, and the double-precision anchor for the
// next block. The loops are branch-free so the compiler can vectorise them.
void FastEngine::beginControlBlock() noexcept
{
    const double invK = 1.0 / static_cast<double>(K_);
    const int64_t blk = freePos_ / K_;
    const double t0 = segTBase_ + static_cast<double>(blk * K_) / sr_;
    const double t1 = segTBase_ + static_cast<double>((blk + 1) * K_) / sr_;
    const double te0 = t0 - beoTStrike_, te1 = t1 - beoTStrike_;
    double env0 = 0.0, env1 = 0.0;
    if (segOnsetOn_)
    {
        env0 = std::exp(-std::max(te0, 0.0) / segOnsetTau_);
        env1 = std::exp(-std::max(te1, 0.0) / segOnsetTau_);
    }
    blockTe0_ = te0;
    blockEnv0_ = env0;
    blockEnv1_ = env1;

    if (blk % kDropEvery == 0)
        dropQuietModes();
    const int na = nAct_;

    // Modes whose onset ramp reaches 1 inside this block (lines 896-901: the ramp is
    // min(1, ...)), with their oscillator at the start of the block.
    nKinks_ = 0;
    if (segRampOn_ && te0 < segMaxPt_)
    {
        for (int a = 0; a < na; ++a)
        {
            const double pt = pt_[size_t(a)];
            if (pt > 0.0 && te0 < pt && pt < te1)
            {
                KinkMode& km = kinks_[size_t(nKinks_++)];
                km.yr = Yr_[size_t(a)];
                km.yi = Yi_[size_t(a)];
                km.pt = pt;
                km.sr = static_cast<double>(a); // index, replaced by the step below
            }
        }
    }

    double* __restrict Yr = Yr_.data();
    double* __restrict Yi = Yi_.data();
    double* __restrict cr = cr_.data();
    double* __restrict ci = ci_.data();
    double* __restrict phiW = phiW_.data();
    const double* __restrict qr = qr_.data();
    const double* __restrict qi = qi_.data();
    const double* __restrict negDlr = negDlr_.data();
    const double* __restrict cosLp = cosLpA_.data();
    const double* __restrict p1r = p1r_.data();
    const double* __restrict p1i = p1i_.data();
    const double* __restrict PKr = PKr_.data();
    const double* __restrict PKi = PKi_.data();
    float* __restrict yr = yr_.data();
    float* __restrict yi = yi_.data();
    float* __restrict s32r = sr32_.data();
    float* __restrict s32i = si32_.data();

    if (wanderSmall_)
    {
        for (int a = 0; a < na; ++a)
        {
            // LFO phasor to the next control point; wander phase there (line 867).
            const double cn = cr[a] * qr[a] - ci[a] * qi[a];
            const double sn = cr[a] * qi[a] + ci[a] * qr[a];
            cr[a] = cn;
            ci[a] = sn;
            const double phiN = negDlr[a] * (cn - cosLp[a]);
            const double d = phiN - phiW[a];
            phiW[a] = phiN;
            // exp(i d / K) for the per-sample step (|d/K| < 0.002), exp(i d) for the anchor.
            const double x = d * invK, x2 = x * x;
            const double c1 = 1.0 - 0.5 * x2;
            const double s1 = x - x * x2 * (1.0 / 6.0);
            const double d2 = d * d;
            const double cK = 1.0 - d2 * (0.5 - d2 * (1.0 / 24.0 - d2 * (1.0 / 720.0)));
            const double sK = d * (1.0 - d2 * (1.0 / 6.0 - d2 * (1.0 / 120.0 - d2 * (1.0 / 5040.0))));
            s32r[a] = static_cast<float>(p1r[a] * c1 - p1i[a] * s1);
            s32i[a] = static_cast<float>(p1r[a] * s1 + p1i[a] * c1);
            const double y0r = Yr[a], y0i = Yi[a];
            yr[a] = static_cast<float>(y0r);
            yi[a] = static_cast<float>(y0i);
            const double ar = y0r * PKr[a] - y0i * PKi[a];
            const double ai = y0r * PKi[a] + y0i * PKr[a];
            Yr[a] = ar * cK - ai * sK;
            Yi[a] = ar * sK + ai * cK;
        }
    }
    else
    {
        for (int a = 0; a < na; ++a)
        {
            const double cn = cr[a] * qr[a] - ci[a] * qi[a];
            const double sn = cr[a] * qi[a] + ci[a] * qr[a];
            cr[a] = cn;
            ci[a] = sn;
            const double phiN = negDlr[a] * (cn - cosLp[a]);
            const double d = phiN - phiW[a];
            phiW[a] = phiN;
            const double c1 = std::cos(d * invK), s1 = std::sin(d * invK);
            const double cK = std::cos(d), sK = std::sin(d);
            s32r[a] = static_cast<float>(p1r[a] * c1 - p1i[a] * s1);
            s32i[a] = static_cast<float>(p1r[a] * s1 + p1i[a] * c1);
            const double y0r = Yr[a], y0i = Yi[a];
            yr[a] = static_cast<float>(y0r);
            yi[a] = static_cast<float>(y0i);
            const double ar = y0r * PKr[a] - y0i * PKi[a];
            const double ai = y0r * PKi[a] + y0i * PKr[a];
            Yr[a] = ar * cK - ai * sK;
            Yi[a] = ar * sK + ai * cK;
        }
    }

    // Output gain at both ends of the block: ramp (lines 896-901), onset gain (905-913),
    // MF scale (917-919). Modes without a ramp have bfA = 1 and invPt = 0.
    {
        const double* __restrict mf = mf_.data();
        const double* __restrict bfA = bfA_.data();
        const double* __restrict invPt = invPt_.data();
        const double* __restrict gm1 = gm1_.data();
        const double* __restrict pl = pl_.data();
        const double* __restrict pr = pr_.data();
        float* __restrict wl = wl_.data();
        float* __restrict wr = wr_.data();
        float* __restrict dwl = dwl_.data();
        float* __restrict dwr = dwr_.data();
        int moving = 0; // modes whose gain changes by more than 1e-7 over the block
        for (int a = 0; a < na; ++a)
        {
            const double r0 = std::max(std::min(1.0, bfA[a] + (1.0 - bfA[a]) * te0 * invPt[a]), 0.0);
            const double r1 = std::max(std::min(1.0, bfA[a] + (1.0 - bfA[a]) * te1 * invPt[a]), 0.0);
            const double g0 = mf[a] * r0 * (1.0 + gm1[a] * env0);
            const double g1 = mf[a] * r1 * (1.0 + gm1[a] * env1);
            const double dg = (g1 - g0) * invK;
            wl[a] = static_cast<float>(pl[a] * g0);
            wr[a] = static_cast<float>(pr[a] * g0);
            dwl[a] = static_cast<float>(pl[a] * dg);
            dwr[a] = static_cast<float>(pr[a] * dg);
            moving += std::fabs(g1 - g0) > 1e-7 * std::fabs(g0) ? 1 : 0;
        }
        flatGain_ = moving == 0;
    }

    // The corner modes are rendered by renderKinks() for this block.
    for (int q = 0; q < nKinks_; ++q)
    {
        KinkMode& km = kinks_[size_t(q)];
        const size_t a = static_cast<size_t>(km.sr);
        km.sr = sr32_[a];
        km.si = si32_[a];
        km.pl = pl_[a];
        km.pr = pr_[a];
        km.mf = mf_[a];
        km.gm1 = gm1_[a];
        wl_[a] = wr_[a] = dwl_[a] = dwr_[a] = 0.0f;
    }
}

// Lines 924-926: state *= exp(1j*2pi*f*d) * exp(-LOG1000*d/T60), d = free-phase samples / SR.
void FastEngine::advanceState(int64_t samples) noexcept
{
    if (samples <= 0) return;
    const double d = static_cast<double>(samples) / sr_;
    const double* t60 = bake_->t60S.data();
    const double k = -bake_->log1000 * d;
    for (size_t i = 0; i < size_t(n_); ++i)
    {
        const double ph = omega_[i] * d;
        const double e = std::exp(k / t60[i]);
        const double mr = std::cos(ph) * e;
        const double mi = std::sin(ph) * e;
        const double r = zr_[i] * mr - zi_[i] * mi;
        const double im = zr_[i] * mi + zi_[i] * mr;
        zr_[i] = r;
        zi_[i] = im;
    }
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

// One strike, as Engine::applyStrike, with the position given by the caller.
void FastEngine::strike(int slotIndex, double velocity, double position) noexcept
{
    if (!isPrepared() || slotIndex < 0 || slotIndex >= numSlots()) return;
    const SlotData& s = bake_->slots[size_t(slotIndex)];
    const BakeData& b = *bake_;
    const EngineParams& p = b.params;
    const size_t n = size_t(n_);

    if (freeStarted_)
        advanceState(freePos_);

    const double range = b.phaseHigh - b.phaseLow;
    for (size_t i = 0; i < n; ++i)
        scratchPhase_[i] = rng_.uniform(b.phaseLow, range);

    const double v = clamp01(velocity);
    velocityParticipation(s.bowVp, v, scratchVel_.data());

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
        const double bellFracV = clamp01((pos - 0.5) / 0.5);
        velocityParticipation(s.bellVp, v, scratchBellVel_.data());
        for (size_t i = 0; i < n; ++i)
            scratchVel_[i] = (1.0 - bellFracV) * scratchVel_[i] + bellFracV * scratchBellVel_[i];
    }
    if (s.edgeLevelBoost > 1.0 && edgeFrac > 0.0)
    {
        const double g = 1.0 + (s.edgeLevelBoost - 1.0) * velocity * edgeFrac;
        for (size_t i = 0; i < n; ++i)
            scratchPosW_[i] = scratchPosW_[i] * g;
    }

    const double calib = s.calibScalar;
    Contact* contact = nullptr;
    if (p.contactPermode)
    {
        if (activeContacts_ == kMaxContacts)
        {
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
    for (size_t i = 0; i < n; ++i)
    {
        const double m = (b.excNorm[i] * numpyScalarPower(scratchVel_[i], s.velGainPower)) * scratchPosW_[i];
        const double cr = (m * std::cos(scratchPhase_[i])) * calib;
        const double ci = (m * std::sin(scratchPhase_[i])) * calib;
        if (contact != nullptr)
        {
            contact->cr[i] = cr;
            contact->ci[i] = ci;
        }
        else
        {
            zr_[i] += cr;
            zi_[i] += ci;
        }
    }
    wanderScale_ = 1.0 + velocity * edgeFrac * p.edgeWanderDepth;
    wanderRateScale_ = 1.0 + velocity * edgeFrac * p.edgeWanderRate;
    wanderRateScale_ = std::max(wanderRateScale_, 0.1);
    washDamp_ = std::max(0.3, 1.0 + p.velLenTilt * (velocity - p.velLenTiltRef));

    ++strikesApplied_;
    lastSlot_ = slotIndex;
    const double strikeTime = timeSeconds();
    const double rawPos = position;

    if (p.bandEnvOnset)
    {
        const double ef = clamp01(1.0 - rawPos / 0.5);
        for (size_t i = 0; i < n; ++i)
            beoPkT_[i] = ef * b.pkTEdge[i] + (1.0 - ef) * b.pkTBow[i];
        beoTStrike_ = strikeTime;
        beoEf_ = ef;
        beoHas_ = true;
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
                beoPkT_[i] = beoPkT_[i] * sm;
            }
        }
    }

    if (p.lfDepth > 0.0)
    {
        const double dt = lfTLast_ > 0.0 ? strikeTime - lfTLast_ : 0.0;
        lfS_ *= std::exp(-dt / p.lfTau);
        lfScaleNow_ = p.lfMax > 1.0 ? std::min(1.0 + lfS_ * p.lfDepth, p.lfMax)
                                    : std::max(1.0 - lfS_ * p.lfDepth, p.lfMin);
        const double w = rawPos <= 0.25 ? p.lfWEdge : (rawPos >= 0.75 ? p.lfWBell : p.lfWBow);
        lfS_ += velocity * w;
        lfTLast_ = strikeTime;
        if (p.lfWanderRateBoost > 0.0)
            wanderRateScale_ = std::max(wanderRateScale_, 1.0 + lfS_ * p.lfWanderRateBoost);
    }

    if (p.mfDepth > 0.0)
    {
        const double dt = mfTLast_ > 0.0 ? strikeTime - mfTLast_ : 0.0;
        mfS_ *= std::exp(-dt / p.mfTau);
        mfScaleNow_ = p.mfMax > 1.0 ? std::min(1.0 + mfS_ * p.mfDepth, p.mfMax)
                                    : std::max(1.0 - mfS_ * p.mfDepth, p.mfMin);
        const double w = rawPos <= 0.25 ? p.mfWEdge : (rawPos >= 0.75 ? p.mfWBell : p.mfWBow);
        mfS_ += velocity * w;
        mfTLast_ = strikeTime;
    }

    segStartTime_ = strikeTime;
    segContactSamples_ = 0;
    freeStarted_ = false;
    freePos_ = 0;
    if (activeContacts_ == 0)
        startFreePhase();
}

// Start of the free phase: the modes at or above the skip threshold (as Engine), and for
// each one its oscillator, damped step, LFO phasor and output factors for the segment.
void FastEngine::startFreePhase() noexcept
{
    const BakeData& b = *bake_;
    const EngineParams& p = b.params;
    const double threshold = b.ampSkipThreshold * (lastSlot_ >= 0 ? b.slots[size_t(lastSlot_)].calibScalar : 1.0);

    segMfOn_ = !b.mfBandpass.empty() && std::fabs(mfScaleNow_ - 1.0) > 1e-6;
    segRampOn_ = p.bandEnvOnset && beoHas_ && beoEf_ > 0.1;
    const SlotData* lastSlot = lastSlot_ >= 0 ? &b.slots[size_t(lastSlot_)] : nullptr;
    segOnsetOn_ = lastSlot != nullptr && lastSlot->onsetGainsActive && beoHas_ && beoEf_ < 0.9;
    if (segOnsetOn_)
        segOnsetTau_ = lastSlot->onsetTauS;

    const double tBase = segStartTime_ + static_cast<double>(segContactSamples_) / sr_;
    segTBase_ = tBase;
    segThreshold_ = threshold;
    segMaxPt_ = 0.0;
    double maxWanderStep = 0.0;
    const double K = static_cast<double>(K_);
    const double depth = p.wanderDepth;
    const double damp = std::max(washDamp_, 0.1);

    const int oldAct = nAct_;
    int a = 0;
    for (int i = 0; i < n_; ++i)
    {
        const size_t k = size_t(i);
        const double zr = zr_[k], zi = zi_[k];
        if (std::hypot(zr, zi) < threshold)
            continue;
        const size_t d = size_t(a);
        actIdx_[d] = i;

        // Damped step: decay with T60 / wash_damp (line 861), and its K-th power.
        const double T = washDamp_ == 1.0 ? b.t60S[k] : b.t60S[k] / damp;
        const double e1 = std::exp(-b.log1000 / (T * sr_));
        p1r_[d] = rot1R_[k] * e1;
        p1i_[d] = rot1I_[k] * e1;
        // p1^K by repeated squaring (K is a power of two).
        double pr = p1r_[d], pi = p1i_[d];
        for (int sq = K_; sq > 1; sq >>= 1)
        {
            const double t = pr * pr - pi * pi;
            pi = 2.0 * pr * pi;
            pr = t;
        }
        PKr_[d] = pr;
        PKi_[d] = pi;

        // LFO wander (lines 864-867), evaluated at absolute engine time.
        const double D = b.driftHz[k] * depth * wanderScale_;
        const double lr = b.lfoRateHz[k] * wanderRateScale_;
        const double negDlr = -(D / lr);
        double rotR = 1.0, rotI = 0.0;
        if (negDlr != 0.0)
        {
            const double wlr = b.twoPi * lr;
            const double arg = (wlr * tBase) + b.lfoPhase[k];
            cr_[d] = std::cos(arg);
            ci_[d] = std::sin(arg);
            const double qa = wlr * K / sr_; // LFO phase per control block, about 0.01 rad
            if (std::fabs(qa) < kSmallAngle)
            {
                const double q2 = qa * qa;
                qr_[d] = 1.0 - q2 * (0.5 - q2 * (1.0 / 24.0 - q2 * (1.0 / 720.0)));
                qi_[d] = qa * (1.0 - q2 * (1.0 / 6.0 - q2 * (1.0 / 120.0 - q2 * (1.0 / 5040.0))));
            }
            else
            {
                qr_[d] = std::cos(qa);
                qi_[d] = std::sin(qa);
            }
            phiW_[d] = negDlr * (cr_[d] - cosLp_[k]);
            rotR = std::cos(phiW_[d]);
            rotI = std::sin(phiW_[d]);
            // |d phi / dt| <= |D/lr| * 2 pi lr, so this bounds the phase step per block.
            maxWanderStep = std::max(maxWanderStep, std::fabs(negDlr) * wlr * K / sr_);
        }
        else
        {
            cr_[d] = 1.0;
            ci_[d] = qi_[d] = phiW_[d] = 0.0;
            qr_[d] = 1.0;
        }
        negDlr_[d] = negDlr;
        cosLpA_[d] = cosLp_[k];
        Yr_[d] = zr * rotR - zi * rotI;
        Yi_[d] = zr * rotI + zi * rotR;

        pl_[d] = b.pickL[k];
        pr_[d] = b.pickR[k];
        mf_[d] = segMfOn_ ? 1.0 + (mfScaleNow_ - 1.0) * b.mfBandpass[k] : 1.0;
        const double pt = segRampOn_ ? beoPkT_[k] : 0.0;
        pt_[d] = (segRampOn_ && pt > 0.001) ? pt : 0.0;
        invPt_[d] = pt_[d] > 0.0 ? 1.0 / pt_[d] : 0.0;
        bfA_[d] = pt_[d] > 0.0 ? p.birthFrac : 1.0;
        segMaxPt_ = std::max(segMaxPt_, pt_[d]);
        double gm1 = 0.0;
        if (segOnsetOn_)
        {
            const double g0 = lastSlot->onsetGains[size_t(b.bowOnsetModeBand[k])];
            gm1 = std::fabs(g0 - 1.0) > 1e-3 ? g0 - 1.0 : 0.0;
        }
        gm1_[d] = gm1;
        gBound_[d] = std::fabs(mf_[d]) * std::max(1.0, 1.0 + gm1) * std::max(std::fabs(pl_[d]), std::fabs(pr_[d]));
        ++a;
    }
    const int end = std::min(nPad_, roundUp(std::max(oldAct, a), kGroup));
    for (int k = a; k < end; ++k)
    {
        const size_t s = size_t(k);
        yr_[s] = yi_[s] = sr32_[s] = si32_[s] = 0.0f;
        wl_[s] = wr_[s] = dwl_[s] = dwr_[s] = 0.0f;
    }
    nAct_ = a;
    nKinks_ = 0;
    wanderSmall_ = maxWanderStep < kSmallAngle;
    freeStarted_ = true;
    freePos_ = 0;
}

} // namespace cymbalis
