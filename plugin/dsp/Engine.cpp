#include "Engine.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace cymbalis
{

namespace
{
constexpr size_t kMaxPendingStrikes = 1024;
constexpr size_t kContactPool = 64;

double clamp01(double x) { return std::min(std::max(x, 0.0), 1.0); }

// np.interp(x, xp, fp) for a scalar x (numpy arr_interp, no precomputed slopes):
// fp[0] below the range, fp[-1] above it, exact fp[j] at a knot, otherwise
// slope * (x - xp[j]) + fp[j]. fp is read with a stride.
double numpyInterp(double x, const double* xp, const double* fp, int len, int stride)
{
    if (x > xp[len - 1]) return fp[(len - 1) * stride];
    if (x < xp[0]) return fp[0];
    const int j = static_cast<int>(std::upper_bound(xp, xp + len, x) - xp) - 1; // xp[j] <= x < xp[j+1]
    if (j == len - 1) return fp[j * stride];
    if (xp[j] == x) return fp[j * stride];
    const double slope = (fp[(j + 1) * stride] - fp[j * stride]) / (xp[j + 1] - xp[j]);
    return slope * (x - xp[j]) + fp[j * stride];
}

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

Engine::Engine(std::shared_ptr<const BakeData> bake, EngineConfig config)
    : bake_(std::move(bake)),
      config_(config),
      n_(bake_->numModes),
      sr_(bake_->sampleRate),
      negLog1000_(-bake_->log1000)
{
    // This is the Python-matching path of the retired A/B pipeline. Mechanisms ported later,
    // for the 602 and Bettis presets, exist only in FastEngine (PORT_MAP, "602 and Bettis").
    {
        std::string missing;
        for (const auto& s : bake_->slots)
            if (!s.hasBowVp)
                missing = " hertz_vel_scale (slot " + s.name + ")";
        if (bake_->edgeNpz.present)
            missing += " NPZ edge data";
        if (!bake_->pkTEdgeByVel.empty())
            missing += " velocity-bracketed edge pk_t";
        // Python applies the LF scale in suppress mode (lf_max_scale <= 1, line 914); this
        // path never did. FastEngine applies it in both modes (PORT_MAP, "Live-engine decisions", decision 6).
        if (bake_->params.lfDepth > 0.0 && bake_->params.lfMax <= 1.0)
            missing += " LF amplitude scaling (lf_max_scale <= 1)";
        if (bake_->hasTuningBlock)
            missing += " the bake's tuning block (docs/TUNING.md)";
        for (const auto& s : bake_->slots)
        {
            if (hasBandGains(s.articulation))
                missing += " excitation band gains (slot " + s.name + ")";
            if (hasOnsetShift(s.articulation))
                missing += " onset shifts (slot " + s.name + ")";
        }
        if (!missing.empty())
            throw BakeError("Engine does not implement:" + missing + "; use FastEngine");
    }
    const size_t n = static_cast<size_t>(n_);
    zr_.assign(n, 0.0);
    zi_.assign(n, 0.0);
    omega_.resize(n);
    cosLp_.resize(n);
    for (size_t i = 0; i < n; ++i)
    {
        omega_[i] = bake_->twoPi * bake_->freqHz[i];
        cosLp_[i] = std::cos(bake_->lfoPhase[i]);
    }
    segNegDlr_.assign(n, 0.0);
    segWlr_.assign(n, 0.0);
    segT60_.assign(n, 1.0);
    segMf_.assign(n, 1.0);
    beoPkT_.assign(n, 0.0);
    segOnsetGm1_.assign(n, 0.0);
    scratchBandScale_.assign(size_t(std::max(bake_->velTiltBands, 1)), 1.0);
    segA_.assign(n, 0.0);
    segPhi_.assign(n, 0.0);
    active_.reserve(n);
    if (bake_->params.contactPermode)
    {
        contactPool_.resize(kContactPool);
        for (auto& c : contactPool_)
        {
            c.cr.assign(n, 0.0);
            c.ci.assign(n, 0.0);
        }
    }
    rng_ = bake_->renderStream; // plugin load: the strike-phase sequence starts here
    pending_.reserve(kMaxPendingStrikes);
    scratchPhase_.resize(n);
    scratchVel_.resize(n);
    scratchBellVel_.resize(n);
    scratchPosW_.resize(n);
    startFreePhase(); // the segment before the first strike: silent
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
            render(left + pos, right + pos, at - pos);
            pos = at;
        }
        applyStrike(pending_[applied].slot, pending_[applied].velocity, static_cast<int64_t>(clock_) + at);
    }
    if (pos < numSamples)
        render(left + pos, right + pos, numSamples - pos);

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

void Engine::render(double* left, double* right, int n)
{
    int pos = 0;
    while (pos < n)
    {
        if (activeContacts_ > 0)
        {
            renderContactSample(left + pos, right + pos);
            ++pos;
        }
        else
        {
            renderFree(left + pos, right + pos, n - pos);
            pos = n;
        }
    }
}

// _render_segment lines 731-735, one sample: state += force[:, k] * coupling; the output is
// Re(state) . pick over every mode (no wander, ramp or other output factor); then the
// state advances one sample with the undamped mode step.
void Engine::renderContactSample(double* left, double* right)
{
    const BakeData& b = *bake_;
    const size_t n = size_t(n_);
    for (int c = 0; c < activeContacts_; ++c)
    {
        const Contact& ct = contactPool_[size_t(c)];
        const SlotData& s = b.slots[size_t(ct.slot)];
        const double* force = s.contactForce.data();
        const size_t len = size_t(s.contactLen);
        for (size_t i = 0; i < n; ++i)
        {
            const double f = force[i * len + size_t(ct.k)];
            zr_[i] += f * ct.cr[i];
            zi_[i] += f * ct.ci[i];
        }
    }
    double sumL = 0.0, sumR = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        sumL += zr_[i] * b.pickL[i];
        sumR += zr_[i] * b.pickR[i];
    }
    left[0] = sumL;
    right[0] = sumR;
    for (size_t i = 0; i < n; ++i)
    {
        const double mr = b.modeStepRe[i], mi = b.modeStepIm[i];
        const double r = zr_[i] * mr - zi_[i] * mi;
        const double im = zr_[i] * mi + zi_[i] * mr;
        zr_[i] = r;
        zi_[i] = im;
    }

    // Retire finished contacts, keeping the rest in order.
    int kept = 0;
    for (int c = 0; c < activeContacts_; ++c)
    {
        Contact& ct = contactPool_[size_t(c)];
        if (++ct.k < ct.len)
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

// _render_segment free ring (lines 841-922). Per mode, for t_local from the start of the
// free phase:
//   wander = -(D/lr) * (cos((2*pi*lr) * t_abs + lp) - cos(lp))           (line 867)
//   wave   = A * decay * cos((2*pi*f) * t_local + phi_0 + wander)        (line 868)
//   decay  = exp(-LOG1000 * t_local / t60), t60 = T60 / wash_damp         (lines 861-863)
//   then the output factors in Python's order, each a separate `wave *=`:
//   onset ramp (lines 896-901), onset gain (lines 905-913), MF scale (lines 917-919)
// t_abs = (segment start + n_contact/SR) + t_local is absolute engine time. Modes with
// D == 0 skip the wander, which would be exactly +-0.
void Engine::renderFree(double* left, double* right, int n)
{
    std::fill(left, left + n, 0.0);
    std::fill(right, right + n, 0.0);
    const double* pickL = bake_->pickL.data();
    const double* pickR = bake_->pickR.data();
    const double* lfoPhase = bake_->lfoPhase.data();
    const double tBase = segStartTime_ + static_cast<double>(segContactSamples_) / sr_;

    for (int c0 = 0; c0 < n; c0 += kChunk)
    {
        const int m = std::min(kChunk, n - c0);
        for (int j = 0; j < m; ++j)
        {
            const double t = static_cast<double>(freePos_ + c0 + j) / sr_; // np.arange(n_free)/SR
            chunkTl_[j] = t;
            chunkTabs_[j] = tBase + t;
            chunkNlt_[j] = negLog1000_ * t;
            chunkTel_[j] = chunkTabs_[j] - beoTStrike_;
        }
        if (segOnsetOn_)
            for (int j = 0; j < m; ++j)
                chunkEnv_[j] = std::exp(-std::max(chunkTel_[j], 0.0) / segOnsetTau_);
        double* L = left + c0;
        double* R = right + c0;
        for (const int i : active_)
        {
            const size_t k = size_t(i);
            const double A = segA_[k];
            const double phi0 = segPhi_[k];
            const double w = omega_[k];
            const double T = segT60_[k];
            const double pl = pickL[k];
            const double pr = pickR[k];
            const double negDlr = segNegDlr_[k];
            const double wlr = segWlr_[k], lp = lfoPhase[k], cosLp = cosLp_[k];
            const bool mfOn = segMfOn_;
            const double mf = segMf_[k];
            const double pt = segRampOn_ ? beoPkT_[k] : 0.0;
            const bool rampOn = segRampOn_ && pt > 0.001;
            const double bf = bake_->params.birthFrac;
            const double gm1 = segOnsetOn_ ? segOnsetGm1_[k] : 0.0;
            const bool onsetOn = gm1 != 0.0;
            for (int j = 0; j < m; ++j)
            {
                const double decay = std::exp(chunkNlt_[j] / T);
                double arg = (w * chunkTl_[j]) + phi0;
                if (negDlr != 0.0)
                    arg = arg + negDlr * (std::cos((wlr * chunkTabs_[j]) + lp) - cosLp);
                double wave = (A * decay) * std::cos(arg);
                if (rampOn)
                {
                    double ramp = std::min(1.0, bf + ((1.0 - bf) * chunkTel_[j]) / pt);
                    ramp = std::max(ramp, 0.0);
                    wave = wave * ramp;
                }
                if (onsetOn)
                    wave = wave * (1.0 + gm1 * chunkEnv_[j]);
                if (mfOn)
                    wave = wave * mf;
                L[j] += pl * wave;
                R[j] += pr * wave;
            }
        }
    }
    freePos_ += n;
}

// Lines 924-926: state *= exp(1j*2pi*f*d) * exp(-LOG1000*d/T60), d = free-phase samples / SR.
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

// One strike: the end of the previous segment, _apply_strike (lines 961-1130), and the
// start of a new segment.
void Engine::applyStrike(int slotIndex, double velocity, int64_t strikeSample)
{
    const SlotData& s = bake_->slots[size_t(slotIndex)];
    const BakeData& b = *bake_;
    const EngineParams& p = b.params;
    const size_t n = size_t(n_);

    // The previous segment ends here: advance the state over its free phase (lines 924-926).
    if (freeStarted_)
        advanceState(freePos_);
    if (config_.truncateContactsLikePython)
        activeContacts_ = 0; // Python drops whatever force a running contact had left

    // Line 1035: one uniform(0, 2pi, n) draw per strike, before anything else.
    const double range = b.phaseHigh - b.phaseLow;
    for (size_t i = 0; i < n; ++i)
        scratchPhase_[i] = rng_.uniform(b.phaseLow, range);

    // Lines 963-977: bow velocity participation.
    const double v = clamp01(velocity);
    velocityParticipation(s.bowVp, v, scratchVel_);

    // Lines 985-995: position weight.
    const double pos = clamp01(s.position);
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

    // Lines 1001-1015: blend toward the bell velocity table above the bow.
    if (pos > 0.5 && s.hasBellVp)
    {
        const double bellFracV = clamp01((pos - 0.5) / 0.5);
        velocityParticipation(s.bellVp, v, scratchBellVel_);
        for (size_t i = 0; i < n; ++i)
            scratchVel_[i] = (1.0 - bellFracV) * scratchVel_[i] + bellFracV * scratchBellVel_[i];
    }

    // Lines 1020-1022: edge level boost (1.0 for Nova, so skipped there).
    if (s.edgeLevelBoost > 1.0 && edgeFrac > 0.0)
    {
        const double g = 1.0 + (s.edgeLevelBoost - 1.0) * velocity * edgeFrac;
        for (size_t i = 0; i < n; ++i)
            scratchPosW_[i] = scratchPosW_[i] * g;
    }

    // Lines 1033-1035: coupling = exc_norm * vel**p * stick(=1) * pos_weight * exp(1j*phase).
    // The slot's calibration level scales its own strike.
    const double calib = s.calibScalar;
    Contact* contact = nullptr;
    if (p.contactPermode)
    {
        // Lines 1067-1093: the coupling enters through the force matrix over the next
        // contactLen samples. If the pool is full, the oldest contact's remaining force is
        // applied at once so that it still plays in full.
        if (activeContacts_ == static_cast<int>(contactPool_.size()))
        {
            Contact& old = contactPool_[0];
            const SlotData& os = b.slots[size_t(old.slot)];
            const size_t len = size_t(os.contactLen);
            for (size_t i = 0; i < n; ++i)
            {
                double rest = 0.0;
                for (size_t k = size_t(old.k); k < len; ++k)
                    rest += os.contactForce[i * len + k];
                zr_[i] += rest * old.cr[i];
                zi_[i] += rest * old.ci[i];
            }
            std::rotate(contactPool_.begin(), contactPool_.begin() + 1, contactPool_.begin() + activeContacts_);
            --activeContacts_;
        }
        contact = &contactPool_[size_t(activeContacts_++)];
        contact->slot = slotIndex;
        contact->k = 0;
        contact->len = s.contactLen;
    }
    for (size_t i = 0; i < n; ++i)
    {
        const double m = (b.excNorm[i] * numpyScalarPower(scratchVel_[i], s.velGainPower)) * scratchPosW_[i];
        const std::complex<double> e = std::exp(std::complex<double>(0.0, scratchPhase_[i]));
        const double cr = (m * e.real()) * calib;
        const double ci = (m * e.imag()) * calib;
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
    // Lines 1114-1125: wander depth and rate scales for this strike (rate jitter is rejected
    // by the stage gate).
    wanderScale_ = 1.0 + velocity * edgeFrac * p.edgeWanderDepth;
    wanderRateScale_ = 1.0 + velocity * edgeFrac * p.edgeWanderRate;
    wanderRateScale_ = std::max(wanderRateScale_, 0.1);

    // Lines 1127-1130: velocity-dependent T60 for the free ring of the coming segment. It applies
    // to every ringing mode; the state advance keeps the undamped T60, so the level jumps at the
    // next strike (PORT_MAP f.2).
    washDamp_ = std::max(0.3, 1.0 + p.velLenTilt * (velocity - p.velLenTiltRef));

    ++strikesApplied_;
    lastSlot_ = slotIndex;
    const double strikeTime = static_cast<double>(strikeSample) / sr_; // ev.time
    const double rawPos = s.position;                                 // float(ev.position)

    // render() lines 621-650: onset ramp target for this strike. pk_t blends the edge and
    // bow peak times by edge_frac; away from the bow, the velocity tilt rescales pk_t per
    // band. The ramp then applies to every ringing mode, timed from this strike (f.3).
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
                const double frac = numpyInterp(velocity, b.velTiltVelocities.data(),
                                                b.velTiltFractions.data() + band, nv, nb);
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

    // render() lines 652-672: LF accumulator. `_lf_t_last > 0` treats a previous strike at
    // exactly t = 0 as "no previous strike", so the next strike sees dt = 0 (PORT_MAP f.8).
    // In boost mode (lf_max_scale > 1) its only audible effect is the wander-rate boost.
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

    // render() lines 674-688: MF accumulator. The scale for this segment comes from S before
    // this strike is added; same t = 0 quirk as the LF accumulator.
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

    // A new segment starts at the strike (render() lines 606-613).
    segStartTime_ = strikeTime;
    segContactSamples_ = 0;
    freeStarted_ = false;
    freePos_ = 0;
    if (activeContacts_ == 0)
        startFreePhase();
}

// Start of the free phase (line 858): amplitude and phase of every mode. Python skips a
// mode for the rest of the segment when |z| < 1e-9 in uncalibrated units (line 859). Here
// the state is calibrated, so the threshold is scaled by the calibration of the slot that
// struck most recently.
void Engine::startFreePhase()
{
    const double threshold =
        bake_->ampSkipThreshold * (lastSlot_ >= 0 ? bake_->slots[size_t(lastSlot_)].calibScalar : 1.0);
    active_.clear();
    for (int i = 0; i < n_; ++i)
    {
        const size_t k = size_t(i);
        const double A = std::hypot(zr_[k], zi_[k]); // abs(z)
        segA_[k] = A;
        segPhi_[k] = std::atan2(zi_[k], zr_[k]);     // np.angle(z)
        if (!(A < threshold))
            active_.push_back(i);
    }

    // Decay T60, wander and MF constants for this segment (lines 861-866, 917-918).
    segMfOn_ = !bake_->mfBandpass.empty() && std::fabs(mfScaleNow_ - 1.0) > 1e-6;
    segRampOn_ = bake_->params.bandEnvOnset && beoHas_ && beoEf_ > 0.1; // line 848-851
    // Lines 905-913: the onset gains of the most recent strike's slot, timed from that strike,
    // on every ringing mode, except at the edge (edge_frac >= 0.9).
    const SlotData* lastSlot = lastSlot_ >= 0 ? &bake_->slots[size_t(lastSlot_)] : nullptr;
    segOnsetOn_ = lastSlot != nullptr && lastSlot->onsetGainsActive && beoHas_ && beoEf_ < 0.9;
    if (segOnsetOn_)
    {
        segOnsetTau_ = lastSlot->onsetTauS;
        for (const int i : active_)
        {
            const double g0 = lastSlot->onsetGains[size_t(bake_->bowOnsetModeBand[size_t(i)])];
            segOnsetGm1_[size_t(i)] = std::fabs(g0 - 1.0) > 1e-3 ? g0 - 1.0 : 0.0;
        }
    }
    const double* t60 = bake_->t60S.data();
    const double* drift = bake_->driftHz.data();
    const double* rate = bake_->lfoRateHz.data();
    const double depth = bake_->params.wanderDepth;
    for (const int i : active_)
    {
        const size_t k = size_t(i);
        segT60_[k] = washDamp_ == 1.0 ? t60[k] : t60[k] / std::max(washDamp_, 0.1);
        if (segMfOn_)
            segMf_[k] = 1.0 + (mfScaleNow_ - 1.0) * bake_->mfBandpass[k];
        const double D = drift[k] * depth * wanderScale_;
        const double lr = rate[k] * wanderRateScale_;
        segNegDlr_[k] = -(D / lr);
        segWlr_[k] = bake_->twoPi * lr;
    }

    freeStarted_ = true;
    freePos_ = 0;
}

} // namespace cymbalis
