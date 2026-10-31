#include "Tuning.h"

#include "Bake.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>

namespace cymbalis
{

const std::vector<CymbalKey>& cymbalKeys()
{
    static const std::vector<CymbalKey> keys {
        { "output_gain_db", &CymbalTuning::outputGainDb },
        { "edge_velocity_remap_exponent", &CymbalTuning::edgeRemapExponent },
        { "tau_ref_ms", &CymbalTuning::tauRefMs },
        { "ring_length", &CymbalTuning::ringLength },
        { "edge_ring_keep_ms", &CymbalTuning::edgeRingKeepMs },
        { "wander_depth", &CymbalTuning::wanderDepth },
        { "wander_rate_hz", &CymbalTuning::wanderRateHz },
        { "edge_wander_depth", &CymbalTuning::edgeWanderDepth },
        { "edge_wander_rate", &CymbalTuning::edgeWanderRate },
        { "mf_sens_depth", &CymbalTuning::mfDepth },
        { "mf_sens_tau", &CymbalTuning::mfTau },
        { "mf_min_scale", &CymbalTuning::mfMin },
        { "mf_max_scale", &CymbalTuning::mfMax },
        { "mf_bow_weight", &CymbalTuning::mfWBow },
        { "mf_edge_weight", &CymbalTuning::mfWEdge },
        { "mf_bell_weight", &CymbalTuning::mfWBell },
        { "mf_lo_hz", &CymbalTuning::mfLoHz },
        { "mf_hi_hz", &CymbalTuning::mfHiHz },
        { "lf_sens_depth", &CymbalTuning::lfDepth },
        { "lf_sens_tau", &CymbalTuning::lfTau },
        { "lf_min_scale", &CymbalTuning::lfMin },
        { "lf_max_scale", &CymbalTuning::lfMax },
        { "lf_cutoff_hz", &CymbalTuning::lfCutoffHz },
        { "lf_bow_weight", &CymbalTuning::lfWBow },
        { "lf_edge_weight", &CymbalTuning::lfWEdge },
        { "lf_bell_weight", &CymbalTuning::lfWBell },
        { "lf_wander_rate_boost", &CymbalTuning::lfWanderRateBoost },
        { "edge_band_env_onset_s", &CymbalTuning::edgeEnvWindowS },
        { kEdgeVoiceKey, &CymbalTuning::edgeVoiceFromBow },
        { "cascade_amount", &CymbalTuning::cascadeAmount },
        { "cascade_threshold_db", &CymbalTuning::cascadeThresholdDb },
        { "cascade_speed_ms", &CymbalTuning::cascadeSpeedMs },
        { "cascade_release_ms", &CymbalTuning::cascadeReleaseMs },
        { "cascade_spread", &CymbalTuning::cascadeSpread },
        { "cascade_low_band", &CymbalTuning::cascadeLowBand },
        { "cascade_high_band", &CymbalTuning::cascadeHighBand },
        { "loud_damp_amount", &CymbalTuning::loudDampAmount },
        { "loud_damp_threshold_db", &CymbalTuning::loudDampThresholdDb },
        { "loud_damp_tilt", &CymbalTuning::loudDampTilt },
        { "rehit_damp_amount", &CymbalTuning::rehitDampAmount },
        { "rehit_damp_time_ms", &CymbalTuning::rehitDampTimeMs },
        { "stereo_width", &CymbalTuning::stereoWidth },
        { "right_highpass_hz", &CymbalTuning::highPassHz },
    };
    return keys;
}

std::string articulationId(const std::string& entryName, const std::string& overlay)
{
    if (! overlay.empty())
        return overlay;
    std::string id = entryName;
    for (auto& ch : id)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return id;
}

double excitationBandEdge(int j)
{
    return 11404.0 * std::pow(2.0, static_cast<double>(j - 21) / 3.0);
}

double excitationBandCentre(int band)
{
    return 11404.0 * std::pow(2.0, (static_cast<double>(band - 21) + 0.5) / 3.0);
}

namespace
{
// The bands' lower edges (index 1 .. 21 used), set up when the library loads, so that no
// call ever initialises anything.
const std::array<double, kExcitationBands> kBandLowerEdges = [] {
    std::array<double, kExcitationBands> e {};
    for (int j = 0; j < kExcitationBands; ++j)
        e[size_t(j)] = excitationBandEdge(j);
    return e;
}();
} // namespace

int excitationBand(double freqHz) noexcept
{
    int band = 0;
    while (band + 1 < kExcitationBands && freqHz >= kBandLowerEdges[size_t(band + 1)])
        ++band;
    return band;
}

bool hasBandGains(const ArticulationTuning& a) noexcept
{
    for (double g : a.bandDb)
        if (g != 0.0)
            return true;
    return false;
}

void tunedT60(const CymbalTuning& c, const double* pythonT60, const double* freqHz, int numModes, double* out) noexcept
{
    for (int i = 0; i < numModes; ++i)
        out[i] = pythonT60[i] * c.ringLength * c.ringBand[size_t(excitationBand(freqHz[i]))];
}

double brightnessTiltDb(const ArticulationTuning& a, double v) noexcept
{
    if (a.brightnessDbOct == 0.0)
        return 0.0;
    constexpr double x3 = -1.2039728043259361; // ln 0.3
    const double x = std::log(std::min(std::max(v, 1e-4), 1.0));
    return std::min(std::max(a.brightnessDbOct * x / x3, -24.0), 24.0);
}

void attackFromOnsetGains(const double* gains, int numGains, const double* edgesHz,
                          std::array<double, kExcitationBands>& outDb) noexcept
{
    for (int j = 0; j < kExcitationBands; ++j)
    {
        const double centre = excitationBandCentre(j);
        int k = 0;
        while (k + 1 < numGains && centre >= edgesHz[k + 1])
            ++k;
        outDb[size_t(j)] = 20.0 * std::log10(gains[k]);
    }
}

bool hasOnsetShift(const ArticulationTuning& a) noexcept
{
    for (int j = 0; j < kExcitationBands; ++j)
        if (a.delayMs[size_t(j)] != 0.0 || a.speed[size_t(j)] != 1.0 || a.delayMsSoft[size_t(j)] != 0.0 ||
            a.speedSoft[size_t(j)] != 1.0)
            return true;
    return false;
}

bool hasShiftLayers(const ArticulationTuning& a) noexcept
{
    return a.delayMs != a.delayMsSoft || a.speed != a.speedSoft;
}

bool hasAttackLayers(const ArticulationTuning& a) noexcept
{
    return a.attackDb != a.attackDbSoft;
}

double onsetLayerT(double v) noexcept
{
    constexpr double x3 = -1.2039728043259361; // ln 0.3
    const double x = std::log(std::min(std::max(v, 1e-4), 1.0));
    return std::min(std::max(x / x3, 0.0), 1.0);
}

void layeredShift(const ArticulationTuning& a, double t, double* delayMs, double* speed) noexcept
{
    for (size_t j = 0; j < size_t(kExcitationBands); ++j)
    {
        delayMs[j] = a.delayMs[j] + t * (a.delayMsSoft[j] - a.delayMs[j]);
        speed[j] = a.speed[j] == a.speedSoft[j]
                       ? a.speed[j]
                       : std::exp(std::log(a.speed[j]) + t * (std::log(a.speedSoft[j]) - std::log(a.speed[j])));
    }
}

void layeredAttack(const ArticulationTuning& a, double t, double* attackDb) noexcept
{
    for (size_t j = 0; j < size_t(kExcitationBands); ++j)
        attackDb[j] = a.attackDb[j] + t * (a.attackDbSoft[j] - a.attackDb[j]);
}

void excitationModeGains(const ArticulationTuning& a, const double* freqHz, int numModes, float* out) noexcept
{
    std::array<float, kExcitationBands> gain {};
    for (int j = 0; j < kExcitationBands; ++j)
        gain[size_t(j)] = static_cast<float>(std::pow(10.0, a.bandDb[size_t(j)] / 20.0));
    for (int k = 0; k < numModes; ++k)
        out[k] = gain[size_t(excitationBand(freqHz[k]))];
}

bool hasStereo(const CymbalTuning& c) noexcept
{
    if (c.stereoWidth != 100.0)
        return true;
    for (size_t j = 0; j < size_t(kExcitationBands); ++j)
        if (c.panBands[j] != 0.0 || c.widthBands[j] != 100.0)
            return true;
    return false;
}

void stereoGains(const CymbalTuning& c, const double* pickL, const double* pickR, const double* freqHz, int numModes,
                 double* outL, double* outR) noexcept
{
    constexpr double quarterPi = 0.78539816339744831;
    for (int k = 0; k < numModes; ++k)
    {
        const size_t j = size_t(excitationBand(freqHz[k]));
        const double pan = c.panBands[j] / 100.0;
        const double width = (c.widthBands[j] / 100.0) * (c.stereoWidth / 100.0);
        const double l = pickL[k], r = pickR[k];
        if (pan == 0.0 && width == 1.0)
        {
            outL[k] = l;
            outR[k] = r;
            continue;
        }
        const double level = std::sqrt(l * l + r * r);
        const double position = std::atan2(std::fabs(r), std::fabs(l)) / quarterPi - 1.0;
        const double moved = std::min(std::max(pan + position * width, -1.0), 1.0);
        const double angle = (moved + 1.0) * quarterPi;
        outL[k] = std::copysign(level * std::cos(angle), l);
        outR[k] = std::copysign(level * std::sin(angle), r);
    }
}

double velocityCurveDb(const ArticulationTuning& a, double v) noexcept
{
    if (a.vel06Db == 0.0 && a.vel03Db == 0.0)
        return 0.0;
    constexpr double x6 = -0.51082562376599068, x3 = -1.2039728043259361; // ln 0.6, ln 0.3
    const double x = std::log(std::min(std::max(v, 1e-4), 1.0));
    const double db = x >= x6 ? a.vel06Db * x / x6 : a.vel06Db + (a.vel03Db - a.vel06Db) * (x - x6) / (x3 - x6);
    return std::min(std::max(db, -60.0), 60.0);
}

void edgeVoiceAlpha(const BakeData& b, double* alpha)
{
    const EdgeNpz& z = b.edgeNpz;
    const size_t n = z.bandIndex.size();
    std::fill(alpha, alpha + n, 0.0);
    std::vector<double> t60;
    for (int band = 0; band < z.numBands; ++band)
    {
        t60.clear();
        for (size_t i = 0; i < n; ++i)
            if (z.bandIndex[i] == band && b.excNorm[i] > 0.0)
                t60.push_back(b.pythonT60S[i]);
        if (t60.size() < 2)
            continue;
        // The median (of an even count, the mean of the middle two, as numpy's).
        std::sort(t60.begin(), t60.end());
        const size_t m = t60.size() / 2;
        const double med = t60.size() % 2 != 0 ? t60[m] : 0.5 * (t60[m - 1] + t60[m]);
        for (size_t i = 0; i < n; ++i)
            if (z.bandIndex[i] == band && b.excNorm[i] > 0.0)
                alpha[i] = std::min(std::max((std::log2(b.pythonT60S[i] / med) + 1.0) / 2.0, 0.0), 1.0);
    }
}

void edgeVoiceVelw(const BakeData& b, double percent, double* out)
{
    const EdgeNpz& z = b.edgeNpz;
    std::copy(z.velw.begin(), z.velw.end(), out);
    const double a = std::min(std::max(percent, 0.0), kEdgeVoiceMax) / kEdgeVoiceMax;
    const size_t n = z.bandIndex.size(), nv = z.velocities.size(), nb = size_t(z.numBands);
    if (! (a > 0.0) || z.voiceAlpha.size() != n)
        return;
    std::vector<double> ns(nb, 0.0), ne(nb), nn(nb);
    for (size_t i = 0; i < n; ++i)
        ns[size_t(z.bandIndex[i])] += b.excNorm[i] * b.excNorm[i];
    for (auto& x : ns)
        x = std::sqrt(x);
    for (size_t v = 0; v < nv; ++v)
    {
        const double* row = z.velw.data() + v * n;
        std::fill(ne.begin(), ne.end(), 0.0);
        for (size_t i = 0; i < n; ++i)
            ne[size_t(z.bandIndex[i])] += row[i] * row[i];
        for (auto& x : ne)
            x = std::sqrt(x);
        auto mixed = [&](size_t i) {
            const size_t k = size_t(z.bandIndex[i]);
            const double w = a * z.voiceAlpha[i];
            return (1.0 - w) * row[i] / ne[k] + w * b.excNorm[i] / ns[k];
        };
        std::fill(nn.begin(), nn.end(), 0.0);
        for (size_t i = 0; i < n; ++i)
        {
            const size_t k = size_t(z.bandIndex[i]);
            if (ne[k] > 0.0 && ns[k] > 0.0)
                nn[k] += mixed(i) * mixed(i);
        }
        for (size_t i = 0; i < n; ++i)
        {
            const size_t k = size_t(z.bandIndex[i]);
            if (ne[k] > 0.0 && ns[k] > 0.0 && nn[k] > 0.0)
                out[v * n + i] = ne[k] * mixed(i) / std::sqrt(nn[k]);
        }
    }
}

// 1 / (1 + exp(-(f - cutoff) / 200)), line 393.
double lfSigmoid(double freqHz, double cutoffHz) noexcept
{
    return 1.0 / (1.0 + std::exp(-(freqHz - cutoffHz) / 200.0));
}

// Rises at lo, falls at hi (lines 401-403).
double mfBandpass(double freqHz, double loHz, double hiHz) noexcept
{
    const double lo = 1.0 / (1.0 + std::exp(-(freqHz - loHz) / 200.0));
    const double hi = 1.0 / (1.0 + std::exp(-(freqHz - hiHz) / 200.0));
    return lo * (1.0 - hi);
}

void applyTuning(BakeData& b, const Tuning& t)
{
    const CymbalTuning& c = t.cymbal;
    const CymbalTuning& py = b.untuned.cymbal; // the bake's own values
    const size_t n = b.freqHz.size();
    EngineParams& p = b.params;

    p.tauRefMs = c.tauRefMs;
    p.wanderDepth = c.wanderDepth;
    p.wanderRateHz = c.wanderRateHz;
    p.edgeWanderDepth = c.edgeWanderDepth;
    p.edgeWanderRate = c.edgeWanderRate;
    p.mfDepth = c.mfDepth;
    p.mfTau = c.mfTau;
    p.mfMin = c.mfMin;
    p.mfMax = c.mfMax;
    p.mfWBow = c.mfWBow;
    p.mfWEdge = c.mfWEdge;
    p.mfWBell = c.mfWBell;
    p.mfLoHz = c.mfLoHz;
    p.mfHiHz = c.mfHiHz;
    p.lfDepth = c.lfDepth;
    p.lfTau = c.lfTau;
    p.lfMin = c.lfMin;
    p.lfMax = c.lfMax;
    p.lfCutoffHz = c.lfCutoffHz;
    p.lfWBow = c.lfWBow;
    p.lfWEdge = c.lfWEdge;
    p.lfWBell = c.lfWBell;
    p.lfWanderRateBoost = c.lfWanderRateBoost;

    // Ring length: every mode's T60 times the multiplier and its band's multiplier, and the
    // contact-phase step with it.
    if (c.ringLength != py.ringLength || c.ringBand != py.ringBand || b.t60S.size() != n)
    {
        b.t60S.resize(n);
        tunedT60(c, b.pythonT60S.data(), b.freqHz.data(), static_cast<int>(n), b.t60S.data());
        for (size_t i = 0; i < n; ++i)
        {
            const double w = b.twoPi * b.freqHz[i] / b.sampleRate;
            const double e = std::exp(-b.log1000 / (b.t60S[i] * b.sampleRate));
            b.modeStepRe[i] = std::cos(w) * e;
            b.modeStepIm[i] = std::sin(w) * e;
        }
    }
    // Wander rate: the LFO rates are uniform(0.7, 1.3) x wander_rate_hz (line 210).
    if (c.wanderRateHz != py.wanderRateHz && py.wanderRateHz > 0.0)
        for (size_t i = 0; i < n; ++i)
            b.lfoRateHz[i] = b.pythonLfoRateHz[i] / py.wanderRateHz * c.wanderRateHz;
    // LF and MF weights, from the cutoff and band edges.
    if (c.lfDepth > 0.0 && (b.lfSigmoid.size() != n || c.lfCutoffHz != py.lfCutoffHz))
    {
        b.lfSigmoid.resize(n);
        for (size_t i = 0; i < n; ++i)
            b.lfSigmoid[i] = lfSigmoid(b.freqHz[i], c.lfCutoffHz);
    }
    if (c.mfDepth > 0.0 && (b.mfBandpass.size() != n || c.mfLoHz != py.mfLoHz || c.mfHiHz != py.mfHiHz))
    {
        b.mfBandpass.resize(n);
        for (size_t i = 0; i < n; ++i)
            b.mfBandpass[i] = mfBandpass(b.freqHz[i], c.mfLoHz, c.mfHiHz);
    }

    // Stereo: each mode's output gains from its pick vector entries and the stereo values.
    if (c.panBands != b.tuning.cymbal.panBands || c.widthBands != b.tuning.cymbal.widthBands
        || c.stereoWidth != b.tuning.cymbal.stereoWidth || b.gainL.size() != n)
    {
        b.gainL.resize(n);
        b.gainR.resize(n);
        stereoGains(c, b.pickL.data(), b.pickR.data(), b.freqHz.data(), static_cast<int>(n), b.gainL.data(), b.gainR.data());
    }

    b.outputGainDb = c.outputGainDb;
    b.edgeNpz.remapExponent = c.edgeRemapExponent;
    b.edgeNpz.onsetS = c.edgeEnvWindowS;
    b.edgeNpz.frameDecayRestart = c.frameDecayRestart;
    if (b.edgeNpz.present)
        b.edgeNpz.envelopeActive = c.edgeEnvelope;

    for (size_t s = 0; s < b.slots.size() && s < size_t(kMaxArticulations); ++s)
    {
        const ArticulationTuning& a = t.articulations[s];
        b.slots[s].levelTrim = std::pow(10.0, a.levelDb / 20.0);
        b.slots[s].articulation = a;
    }
    b.tuning = t;
}

Tuning parseTuningBlock(const json::Value& block, const BakeData& b, const Tuning& base)
{
    Tuning t = base;
    if (! block.isObject())
        throw std::runtime_error("tuning: not an object");
    if (const auto* f = block.find("format"); f == nullptr || ! f->isString() || f->string() != "cymbalis-tuning")
        throw std::runtime_error("tuning: format must be \"cymbalis-tuning\"");
    if (const auto* v = block.find("version"); v == nullptr || ! v->isNumber() || v->number() != 1.0)
        throw std::runtime_error("tuning: unsupported version");
    if (const auto* cy = block.find("cymbal"); cy != nullptr && ! cy->isNull())
    {
        for (const auto& kv : cy->object())
        {
            if (kv.first == kFrameDecayRestartKey)
            {
                t.cymbal.frameDecayRestart = kv.second.boolean();
                continue;
            }
            if (kv.first == kEdgeEnvelopeKey)
            {
                t.cymbal.edgeEnvelope = kv.second.boolean();
                continue;
            }
            if (kv.first == kHighPassKey)
            {
                t.cymbal.highPassOn = kv.second.boolean();
                continue;
            }
            if (kv.first == kPanBandsKey || kv.first == kWidthBandsKey)
            {
                const bool pan = kv.first == kPanBandsKey;
                const auto& a = kv.second.array();
                if (a.size() != size_t(kExcitationBands))
                    throw std::runtime_error("tuning: " + kv.first + " needs 22 values");
                for (size_t j = 0; j < a.size(); ++j)
                {
                    const double x = a[j].number();
                    if (! std::isfinite(x) || (pan ? std::fabs(x) > kPanMax : (x < 0.0 || x > kWidthMax)))
                        throw std::runtime_error("tuning: " + kv.first + (pan ? " values must lie in -100 to 100"
                                                                               : " values must lie in 0 to 200"));
                    (pan ? t.cymbal.panBands : t.cymbal.widthBands)[j] = x;
                }
                continue;
            }
            if (kv.first == kRingBandsKey)
            {
                const auto& a = kv.second.array();
                if (a.size() != size_t(kExcitationBands))
                    throw std::runtime_error("tuning: ring_length_bands needs 22 values");
                for (size_t j = 0; j < a.size(); ++j)
                {
                    const double x = a[j].number();
                    if (! std::isfinite(x) || ! (x > 0.0))
                        throw std::runtime_error("tuning: ring_length_bands values must be > 0");
                    t.cymbal.ringBand[j] = x;
                }
                continue;
            }
            if (kv.first == kRehitWeightsKey)
            {
                for (const auto& w : kv.second.object())
                {
                    int slot = -1;
                    for (size_t s = 0; s < b.slots.size(); ++s)
                        if (b.slots[s].id == w.first)
                            slot = static_cast<int>(s);
                    if (slot < 0 || slot >= kMaxArticulations)
                        throw std::runtime_error("tuning: rehit_damp_weights names no articulation \"" + w.first + "\"");
                    const double x = w.second.number();
                    if (! std::isfinite(x) || x < 0.0)
                        throw std::runtime_error("tuning: rehit_damp_weights values must be >= 0");
                    t.cymbal.rehitWeight[size_t(slot)] = x;
                }
                continue;
            }
            bool known = false;
            for (const auto& k : cymbalKeys())
                if (kv.first == k.key)
                {
                    const double x = kv.second.number();
                    if (! std::isfinite(x))
                        throw std::runtime_error("tuning: " + kv.first + " is not finite");
                    t.cymbal.*(k.member) = x;
                    known = true;
                }
            if (! known)
                throw std::runtime_error("tuning: unknown cymbal value \"" + kv.first + "\"");
        }
    }
    if (const auto* ar = block.find("articulations"); ar != nullptr && ! ar->isNull())
        for (const auto& kv : ar->object())
        {
            int slot = -1;
            for (size_t s = 0; s < b.slots.size(); ++s)
                if (b.slots[s].id == kv.first)
                    slot = static_cast<int>(s);
            if (slot < 0 || slot >= kMaxArticulations)
                throw std::runtime_error("tuning: the bake has no articulation \"" + kv.first + "\"");
            for (const auto& v : kv.second.object())
            {
                const double x = v.second.number();
                if (! std::isfinite(x))
                    throw std::runtime_error("tuning: " + kv.first + "." + v.first + " is not finite");
                if (v.first == kVel06Key) t.articulations[size_t(slot)].vel06Db = x;
                else if (v.first == kVel03Key) t.articulations[size_t(slot)].vel03Db = x;
                else if (v.first == kBrightnessKey) t.articulations[size_t(slot)].brightnessDbOct = x;
                else if (v.first == kStickGainKey) t.articulations[size_t(slot)].stickGain = x;
                else if (v.first == kStickLoKey) t.articulations[size_t(slot)].stickLoHz = x;
                else if (v.first == kStickHiKey) t.articulations[size_t(slot)].stickHiHz = x;
                else if (v.first == kStickLenKey) t.articulations[size_t(slot)].stickLenMs = x;
                else if (v.first == kStickDecayKey) t.articulations[size_t(slot)].stickDecayMs = x;
                else if (v.first == kStickBowWeightKey) t.articulations[size_t(slot)].stickBowWeight = x;
                else if (v.first == kStickBellWeightKey) t.articulations[size_t(slot)].stickBellWeight = x;
                else if (v.first == kStickEdgeWeightKey) t.articulations[size_t(slot)].stickEdgeWeight = x;
                else
                    throw std::runtime_error("tuning: unknown articulation value \"" + v.first + "\" (the level is "
                                             "slots[].level_trim.gain_db)");
            }
        }
    if (! (t.cymbal.ringLength > 0.0) || ! (t.cymbal.edgeRemapExponent > 0.0))
        throw std::runtime_error("tuning: ring_length and edge_velocity_remap_exponent must be > 0");
    if (! (t.cymbal.stereoWidth >= 0.0 && t.cymbal.stereoWidth <= kWidthMax))
        throw std::runtime_error("tuning: stereo_width must lie in 0 to 200");
    if (! (t.cymbal.highPassHz >= kHighPassMinHz && t.cymbal.highPassHz <= kHighPassMaxHz))
        throw std::runtime_error("tuning: right_highpass_hz must lie in 20 to 300");
    if (! (t.cymbal.edgeVoiceFromBow >= 0.0 && t.cymbal.edgeVoiceFromBow <= kEdgeVoiceMax))
        throw std::runtime_error("tuning: edge_voice_from_bow must lie in 0 to 100");
    for (int s = 0; s < t.numArticulations && s < kMaxArticulations; ++s)
    {
        const ArticulationTuning& a = t.articulations[size_t(s)];
        for (double w : { a.stickBowWeight, a.stickBellWeight, a.stickEdgeWeight })
            if (! (w >= 0.0 && w <= kStickWeightMax))
                throw std::runtime_error("tuning: the stick click weights must lie in 0 to 1");
    }
    return t;
}

} // namespace cymbalis
