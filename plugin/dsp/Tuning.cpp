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

void excitationModeGains(const ArticulationTuning& a, const double* freqHz, int numModes, float* out) noexcept
{
    std::array<float, kExcitationBands> gain {};
    for (int j = 0; j < kExcitationBands; ++j)
        gain[size_t(j)] = static_cast<float>(std::pow(10.0, a.bandDb[size_t(j)] / 20.0));
    for (int k = 0; k < numModes; ++k)
        out[k] = gain[size_t(excitationBand(freqHz[k]))];
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

    // Ring length: every mode's T60 times the multiplier, and the contact-phase step with it.
    if (c.ringLength != py.ringLength || b.t60S.size() != n)
    {
        b.t60S.resize(n);
        for (size_t i = 0; i < n; ++i)
        {
            b.t60S[i] = b.pythonT60S[i] * c.ringLength;
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

    b.outputGainDb = c.outputGainDb;
    b.edgeNpz.remapExponent = c.edgeRemapExponent;
    b.edgeNpz.onsetS = c.edgeEnvWindowS;
    b.edgeNpz.frameDecayRestart = c.frameDecayRestart;

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
                if (articulationId(b.slots[s].name, b.slots[s].overlay) == kv.first)
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
                else
                    throw std::runtime_error("tuning: unknown articulation value \"" + v.first + "\" (the level is "
                                             "slots[].level_trim.gain_db)");
            }
        }
    if (! (t.cymbal.ringLength > 0.0) || ! (t.cymbal.edgeRemapExponent > 0.0))
        throw std::runtime_error("tuning: ring_length and edge_velocity_remap_exponent must be > 0");
    return t;
}

} // namespace cymbalis
