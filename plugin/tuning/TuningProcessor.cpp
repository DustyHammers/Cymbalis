// Tuning code only (CYMBALIS_WITH_TUNING, docs/TUNING.md): the processor's tuning state, its hand-over to the
// audio thread, the meters and the session state.
#include "PluginProcessor.h"
#include "TuningFields.h"

#include <cmath>

namespace
{
constexpr double kHitWindowSeconds = 0.5;  // the last hit's peak and RMS are measured over this
constexpr double kOutputRmsSeconds = 0.3;  // output RMS smoothing
} // namespace

cymbalis::Tuning CymbalisProcessor::tuning (int preset) const
{
    const juce::ScopedLock sl (tuningLock);
    return tunings[(size_t) preset];
}

void CymbalisProcessor::setTuning (int preset, const cymbalis::Tuning& t)
{
    if (preset < 0 || preset >= cymbalis::kNumPresets)
        return;
    const juce::ScopedLock sl (tuningLock);
    tunings[(size_t) preset] = t;
    publishTuning (preset, t);
}

// Message thread (under tuningLock): the writer's buffer becomes the middle one. It carries
// the per-mode excitation gains of every slot, computed here, off the audio thread.
void CymbalisProcessor::publishTuning (int preset, const cymbalis::Tuning& t)
{
    auto& m = tuningBuffers[(size_t) tuningBack];
    m.preset = preset;
    m.tuning = t;
    m.decayValid = false;
    if (const auto& bake = presets[(size_t) preset].bake)
    {
        const size_t n = (size_t) bake->numModes;
        m.modeGains.resize (bake->slots.size() * n);
        for (size_t s = 0; s < bake->slots.size(); ++s)
            cymbalis::excitationModeGains (t.articulations[s], bake->freqHz.data(), bake->numModes, m.modeGains.data() + s * n);
        // Per-band ring lengths: every decay coefficient at the prepared rate, here, for the
        // cymbal to take at its next control point (computed when they change, then sent
        // with every message).
        const double rate = preparedRate.load();
        auto& w = writerDecay[(size_t) preset];
        if (rate > 0.0 && (w.valid || t.cymbal.ringBand != bake->tuning.cymbal.ringBand))
        {
            if (! w.valid || w.ringBand != t.cymbal.ringBand || ! juce::exactlyEqual (w.ringLength, t.cymbal.ringLength)
                || ! juce::exactlyEqual (w.rate, rate))
            {
                if (! juce::exactlyEqual (w.forceRate, rate))
                {
                    w.force.clear();
                    w.forceLen = 0;
                    if (bake->params.contactPermode)
                        cymbalis::FastEngine::contactForce (*bake, bake->params, rate, w.force, w.forceLen);
                    w.forceRate = rate;
                }
                std::vector<double> rates (n);
                cymbalis::tunedT60 (t.cymbal, bake->pythonT60S.data(), bake->freqHz.data(), bake->numModes, rates.data());
                for (auto& r : rates)
                    r = -bake->log1000 / r;
                cymbalis::FastEngine::computeDecay (*bake, rate, cymbalis::FastEngine::controlBlockFor (*bake, rate),
                                                    cymbalis::FastEngine::oscillatorWash (*bake, bake->params), rates.data(),
                                                    w.force, w.forceLen, w.set);
                w.valid = true;
                w.ringBand = t.cymbal.ringBand;
                w.ringLength = t.cymbal.ringLength;
                w.rate = rate;
            }
            m.decay = w.set;
            m.decayValid = true;
        }
    }
    tuningBack = tuningMiddle.exchange (tuningBack | kTuningNew) & 3;
}

// Audio thread, at the start of a block: new values go to the cymbal if they are for its
// preset. A cymbal that just took over gets the newest values seen for its preset (it was
// built with the values of its build time).
void CymbalisProcessor::takeTuning (bool newCymbal) noexcept
{
    bool fresh = false;
    if ((tuningMiddle.load() & kTuningNew) != 0)
    {
        tuningFront = tuningMiddle.exchange (tuningFront) & 3;
        fresh = true;
    }
    const auto& m = tuningBuffers[(size_t) tuningFront];
    if ((fresh || newCymbal) && current != nullptr && current->engine != nullptr && m.preset == current->preset)
        current->engine->setTuning (m.tuning, m.modeGains.data(), m.decayValid ? &m.decay : nullptr);
}

// Loader thread or prepareToPlay: a new cymbal starts with the current values of its preset.
// A ring length other than the bake's needs a bake with it (applyTuning), so the engine is
// built from a tuned copy of the bake.
void CymbalisProcessor::applyTuningToNewCymbal (Cymbal& c)
{
    const auto& bake = presets[(size_t) c.preset].bake;
    if (bake == nullptr)
        return;
    const auto t = tuning (c.preset);
    c.ring = t.cymbal.ringLength;
    if (! juce::exactlyEqual (t.cymbal.ringLength, bake->tuning.cymbal.ringLength) || t.cymbal.ringBand != bake->tuning.cymbal.ringBand)
    {
        auto copy = std::make_shared<cymbalis::BakeData> (*bake);
        cymbalis::applyTuning (*copy, t);
        c.engine = std::make_unique<cymbalis::FastEngine> (std::move (copy));
    }
    else
        c.engine = std::make_unique<cymbalis::FastEngine> (bake);
    c.engine->setTuning (t, nullptr);
}

// Audio thread. Before the Volume knob: the last hit's 500 ms. After it: the output meters.
void CymbalisProcessor::meterBlock (const float* left, const float* right, int n, bool afterVolume) noexcept
{
    if (! afterVolume)
    {
        if (hitLeft > 0)
        {
            const int from = juce::jlimit (0, n, hitStart);
            const int m = (int) std::min<long> (hitLeft, (long) (n - from));
            for (int i = from; i < from + m; ++i)
            {
                hitSum += (double) left[i] * left[i] + (double) right[i] * right[i];
                hitMax = std::max ({ hitMax, (double) std::abs (left[i]), (double) std::abs (right[i]) });
            }
            hitLeft -= m;
            hitCount += m;
            hitPeakOut.store ((float) hitMax);
            hitRmsOut.store (hitCount > 0 ? (float) std::sqrt (hitSum / (2.0 * (double) hitCount)) : 0.0f);
            if (hitLeft <= 0)
                hitComplete.store (true);
        }
        hitStart = 0;
        return;
    }
    double sum = 0.0;
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        sum += (double) left[i] * left[i] + (double) right[i] * right[i];
        peak = std::max ({ peak, std::abs (left[i]), std::abs (right[i]) });
    }
    float was = meterPeak.load();
    while (peak > was && ! meterPeak.compare_exchange_weak (was, peak)) {}
    if (peak >= 1.0f)
        meterClip.store (true);
    const double rate = preparedRate.load();
    if (n > 0 && rate > 0.0)
    {
        const double a = std::exp (-(double) n / (kOutputRmsSeconds * rate));
        meterMs = sum / (2.0 * n) + (meterMs - sum / (2.0 * n)) * a;
        meterRms.store ((float) std::sqrt (meterMs));
    }
}

// Audio thread, at a strike at sample `at` of the block.
void CymbalisProcessor::startHitMeter (int row, int entry, double velocity, int at) noexcept
{
    hitRow.store (row);
    hitEntry.store (entry);
    hitVelocity.store ((float) velocity);
    hitStart = at;
    hitLeft = (long) std::lround (kHitWindowSeconds * preparedRate.load());
    hitCount = 0;
    hitSum = hitMax = 0.0;
    hitPeakOut.store (0.0f);
    hitRmsOut.store (0.0f);
    hitComplete.store (false);
}

bool CymbalisProcessor::onsetPeaks (int preset, int entry, std::array<float, cymbalis::kExcitationBands>& out) const noexcept
{
    if (preset < 0 || preset >= cymbalis::kNumPresets || entry < 0 || entry >= cymbalis::kMaxArticulations
        || ! peakSet[(size_t) preset][(size_t) entry].load())
        return false;
    for (size_t j = 0; j < out.size(); ++j)
        out[j] = peakMs[(size_t) preset][(size_t) entry][j].load (std::memory_order_relaxed);
    return true;
}

CymbalisProcessor::Meters CymbalisProcessor::readMeters() noexcept
{
    Meters m;
    m.outPeak = meterPeak.exchange (0.0f);
    m.outRms = meterRms.load();
    m.clip = meterClip.load();
    m.hitRow = hitRow.load();
    m.hitEntry = hitEntry.load();
    m.hitVelocity = hitVelocity.load();
    m.hitPeak = hitPeakOut.load();
    m.hitRms = hitRmsOut.load();
    m.hitComplete = hitComplete.load();
    return m;
}

// The tuning of every preset that differs from its bake, as tuning files.
void CymbalisProcessor::writeTuningState (juce::XmlElement& session) const
{
    for (int k = 0; k < cymbalis::kNumPresets; ++k)
    {
        const auto& bake = presets[(size_t) k].bake;
        if (bake == nullptr)
            continue;
        const auto t = tuning (k);
        if (! cymbalis::tuning::isModified (*bake, t))
            continue;
        auto* e = session.createNewChildElement ("Tuning");
        e->setAttribute ("preset", cymbalis::kPresets[(size_t) k].id);
        e->addTextElement (cymbalis::tuning::toJson (*bake, cymbalis::kPresets[(size_t) k].id, t, JucePlugin_Name));
    }
}

// Every preset goes back to its bake's values, then takes the session's tuning if it has one.
void CymbalisProcessor::readTuningState (const juce::XmlElement& session)
{
    for (int k = 0; k < cymbalis::kNumPresets; ++k)
    {
        const auto& bake = presets[(size_t) k].bake;
        if (bake == nullptr)
            continue;
        cymbalis::Tuning t = bake->tuning;
        for (auto* e : session.getChildWithTagNameIterator ("Tuning"))
            if (e->getStringAttribute ("preset") == cymbalis::kPresets[(size_t) k].id)
            {
                cymbalis::Tuning loaded;
                if (cymbalis::tuning::fromJson (e->getAllSubText(), *bake, cymbalis::kPresets[(size_t) k].id, loaded).isEmpty())
                    t = loaded;
            }
        setTuning (k, t);
    }
}
