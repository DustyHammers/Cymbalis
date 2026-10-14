#include "Engine.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace cymbalis
{

namespace
{
constexpr size_t kMaxPendingStrikes = 1024;

double clamp01(double x) { return std::min(std::max(x, 0.0), 1.0); }

// numpy `arr ** p` for a scalar exponent: fast paths for 1, 2, 0.5, -1, 0; libm pow otherwise.
double numpyScalarPower(double x, double p)
{
    if (p == 1.0) return x;
    if (p == 2.0) return x * x;
    if (p == 0.5) return std::sqrt(x);
    if (p == -1.0) return 1.0 / x;
    if (p == 0.0) return 1.0;
    return std::pow(x, p);
}
} // namespace

Engine::Engine(std::shared_ptr<const BakeData> bake)
    : bake_(std::move(bake)),
      n_(bake_->numModes),
      sr_(bake_->sampleRate),
      negLog1000_(-bake_->log1000)
{
    const size_t n = static_cast<size_t>(n_);
    zr_.assign(n, 0.0);
    zi_.assign(n, 0.0);
    omega_.resize(n);
    for (size_t i = 0; i < n; ++i)
        omega_[i] = bake_->twoPi * bake_->freqHz[i];
    segA_.assign(n, 0.0);
    segPhi_.assign(n, 0.0);
    active_.reserve(n);
    rng_ = bake_->renderStream; // plugin load: the strike-phase sequence starts here
    pending_.reserve(kMaxPendingStrikes);
    scratchPhase_.resize(n);
    scratchVel_.resize(n);
    scratchBellVel_.resize(n);
    scratchPosW_.resize(n);
}

bool Engine::strike(int slot, double velocity, int64_t sampleOffset)
{
    if (slot < 0 || slot >= static_cast<int>(bake_->slots.size())) return false;
    if (pending_.size() >= kMaxPendingStrikes) return false;
    pending_.push_back({slot, velocity, std::max<int64_t>(sampleOffset, 0), pendingOrder_++});
    return true;
}

void Engine::process(double* left, double* right, int numSamples)
{
    if (numSamples <= 0) return;

    // Due strikes in time order; equal offsets keep the order they were queued in
    // (Python sorts strikes with a stable sort).
    std::sort(pending_.begin(), pending_.end(), [](const PendingStrike& a, const PendingStrike& b) {
        return a.offset != b.offset ? a.offset < b.offset : a.order < b.order;
    });

    int pos = 0;
    size_t applied = 0;
    for (; applied < pending_.size() && pending_[applied].offset < numSamples; ++applied)
    {
        const int at = static_cast<int>(pending_[applied].offset);
        if (at > pos)
        {
            renderSpan(left + pos, right + pos, at - pos);
            pos = at;
        }
        applyStrike(pending_[applied].slot, pending_[applied].velocity);
    }
    if (pos < numSamples)
        renderSpan(left + pos, right + pos, numSamples - pos);

    pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(applied));
    for (auto& p : pending_)
        p.offset -= numSamples;

    // render() line 703: out[:,1] = _hp(out[:,1], 120), scipy sosfilt, direct form II transposed.
    const double b0 = bake_->hpSos[0], b1 = bake_->hpSos[1], b2 = bake_->hpSos[2];
    const double a1 = bake_->hpSos[4], a2 = bake_->hpSos[5];
    double z0 = hpZ0_, z1 = hpZ1_;
    for (int j = 0; j < numSamples; ++j)
    {
        const double x = right[j];
        const double y = b0 * x + z0;
        z0 = b1 * x - a1 * y + z1;
        z1 = b2 * x - a2 * y;
        right[j] = y;
    }
    hpZ0_ = z0;
    hpZ1_ = z1;

    clock_ += static_cast<uint64_t>(numSamples);
}

// _render_segment free ring (lines 857-922), stage 0-2 subset: every factor after the
// cosine is exactly 1, and wander is exactly +-0.
void Engine::renderSpan(double* left, double* right, int n)
{
    std::fill(left, left + n, 0.0);
    std::fill(right, right + n, 0.0);
    const double* t60 = bake_->t60S.data();
    const double* pickL = bake_->pickL.data();
    const double* pickR = bake_->pickR.data();
    for (const int i : active_)
    {
        const double A = segA_[size_t(i)];
        const double phi0 = segPhi_[size_t(i)];
        const double w = omega_[size_t(i)];
        const double T = t60[i];
        const double pl = pickL[i];
        const double pr = pickR[i];
        for (int j = 0; j < n; ++j)
        {
            const double t = static_cast<double>(segPos_ + j) / sr_;   // np.arange(n)/SR
            const double decay = std::exp((negLog1000_ * t) / T);       // np.exp(-LOG1000*t_local/t60)
            const double wave = (A * decay) * std::cos((w * t) + phi0); // A*decay*np.cos(2.*np.pi*f*t_local+phi_0)
            left[j] += pl * wave;
            right[j] += pr * wave;
        }
    }
    segPos_ += n;
}

// Lines 924-926: state *= exp(1j*2pi*f*d) * exp(-LOG1000*d/T60), d = segment samples / SR.
void Engine::advanceState(int64_t samples)
{
    if (samples <= 0) return;
    const double d = static_cast<double>(samples) / sr_;
    const double* t60 = bake_->t60S.data();
    const double k = negLog1000_ * d;
    for (size_t i = 0; i < size_t(n_); ++i)
    {
        const std::complex<double> rot = std::exp(std::complex<double>(0.0, omega_[i] * d));
        const double e = std::exp(k / t60[i]);
        const double mr = rot.real() * e;
        const double mi = rot.imag() * e;
        const double r = zr_[i] * mr - zi_[i] * mi;
        const double im = zr_[i] * mi + zi_[i] * mr;
        zr_[i] = r;
        zi_[i] = im;
    }
}

void Engine::velocityParticipation(const VelocityTable& t, double v, std::vector<double>& out) const
{
    const int nv = t.numVel;
    const double* norms = t.norms.data();
    const double* curves = t.curves.data();
    if (v <= norms[0])
    {
        const double s = v / (norms[0] + 1e-9);
        for (int i = 0; i < n_; ++i)
            out[size_t(i)] = curves[size_t(i) * size_t(nv)] * s;
    }
    else if (v >= norms[nv - 1])
    {
        for (int i = 0; i < n_; ++i)
            out[size_t(i)] = curves[size_t(i) * size_t(nv) + size_t(nv - 1)];
    }
    else
    {
        int idx = static_cast<int>(std::lower_bound(norms, norms + nv, v) - norms) - 1; // np.searchsorted, 'left'
        idx = std::min(std::max(idx, 0), nv - 2);
        const double tt = (v - norms[idx]) / (norms[idx + 1] - norms[idx] + 1e-12);
        for (int i = 0; i < n_; ++i)
        {
            const double* row = curves + size_t(i) * size_t(nv);
            out[size_t(i)] = row[idx] * (1.0 - tt) + row[idx + 1] * tt;
        }
    }
}

// _apply_strike (lines 961-1066), stage 1 subset, followed by the start of a new segment.
void Engine::applyStrike(int slotIndex, double velocity)
{
    const SlotData& s = bake_->slots[size_t(slotIndex)];
    const BakeData& b = *bake_;

    // The previous segment ends here: advance the state by its length.
    advanceState(segPos_);
    segPos_ = 0;

    // Line 1035: one uniform(0, 2pi, n) draw per strike, before anything else.
    const double range = b.phaseHigh - b.phaseLow;
    for (int i = 0; i < n_; ++i)
        scratchPhase_[size_t(i)] = rng_.uniform(b.phaseLow, range);

    // Lines 963-977: bow velocity participation.
    const double v = clamp01(velocity);
    velocityParticipation(s.bowVp, v, scratchVel_);

    // Lines 985-995: position weight.
    const double pos = clamp01(s.position);
    const double edgeFrac = clamp01(1.0 - pos / 0.5);
    if (pos <= 0.5)
    {
        const double tBlend = pos / 0.5;
        for (int i = 0; i < n_; ++i)
            scratchPosW_[size_t(i)] = b.edgeWeight[size_t(i)] + tBlend * (1.0 - b.edgeWeight[size_t(i)]);
    }
    else
    {
        const double bellFrac = clamp01((pos - 0.5) / 0.5);
        for (int i = 0; i < n_; ++i)
            scratchPosW_[size_t(i)] = (1.0 - bellFrac) * 1.0 + bellFrac * b.bellWeight[size_t(i)];
    }

    // Lines 1001-1015: blend toward the bell velocity table above the bow.
    if (pos > 0.5 && s.hasBellVp)
    {
        const double bellFracV = clamp01((pos - 0.5) / 0.5);
        velocityParticipation(s.bellVp, v, scratchBellVel_);
        for (int i = 0; i < n_; ++i)
            scratchVel_[size_t(i)] = (1.0 - bellFracV) * scratchVel_[size_t(i)] + bellFracV * scratchBellVel_[size_t(i)];
    }

    // Lines 1020-1022: edge level boost (1.0 for Nova, so skipped there).
    if (s.edgeLevelBoost > 1.0 && edgeFrac > 0.0)
    {
        const double g = 1.0 + (s.edgeLevelBoost - 1.0) * velocity * edgeFrac;
        for (int i = 0; i < n_; ++i)
            scratchPosW_[size_t(i)] = scratchPosW_[size_t(i)] * g;
    }

    // Lines 1033-1035 and 1065: coupling = exc_norm * vel**p * stick(=1) * pos_weight * exp(1j*phase),
    // added to the state. The slot's calibration level scales its own strike.
    const double calib = s.calibScalar;
    for (int i = 0; i < n_; ++i)
    {
        const size_t k = size_t(i);
        const double m = (b.excNorm[k] * numpyScalarPower(scratchVel_[k], s.velGainPower)) * scratchPosW_[k];
        const std::complex<double> e = std::exp(std::complex<double>(0.0, scratchPhase_[k]));
        const double cr = m * e.real();
        const double ci = m * e.imag();
        zr_[k] += cr * calib;
        zi_[k] += ci * calib;
    }
    ++strikesApplied_;

    // Python skips a mode for the whole segment when |z| < 1e-9 in uncalibrated units (line 859).
    // Here the state is calibrated, so the threshold is scaled by the striking slot's calibration.
    startSegment(b.ampSkipThreshold * calib);
}

void Engine::startSegment(double skipThreshold)
{
    active_.clear();
    for (int i = 0; i < n_; ++i)
    {
        const size_t k = size_t(i);
        const double A = std::hypot(zr_[k], zi_[k]); // abs(z)
        segA_[k] = A;
        segPhi_[k] = std::atan2(zi_[k], zr_[k]);     // np.angle(z)
        if (!(A < skipThreshold))
            active_.push_back(i);
    }
    segPos_ = 0;
}

} // namespace cymbalis
