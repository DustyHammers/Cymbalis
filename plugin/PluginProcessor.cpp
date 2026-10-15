#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <BinaryData.h>

#include <cmath>

namespace
{
constexpr double kVelocityFloor = 100.0 / 127.0; // vf in UI_SPEC section 9
constexpr double kPresetFadeSeconds = 0.03;      // a ringing preset fades out over this after a switch
constexpr int kFadeChunk = 1024;                 // samples of the fading preset rendered at a time

const char* findResource (const char* originalFilename, int& size)
{
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        if (juce::String (BinaryData::originalFilenames[i]) == originalFilename)
            return BinaryData::getNamedResource (BinaryData::namedResourceList[i], size);
    size = 0;
    return nullptr;
}

juce::String percentText (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }

const juce::String middleDot = juce::String::fromUTF8 ("\xc2\xb7");
} // namespace

// Builds the next cymbal and frees retired ones, off the audio thread. It polls: the audio
// thread only stores atomics and never wakes it (that would lock).
class CymbalisProcessor::Loader final : public juce::Thread
{
public:
    explicit Loader (CymbalisProcessor& p) : juce::Thread ("Cymbalis loader"), owner (p) {}
    void run() override
    {
        while (! threadShouldExit())
        {
            owner.loaderTick();
            wait (10);
        }
    }

private:
    CymbalisProcessor& owner;
};

CymbalisProcessor::CymbalisProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "CymbalisParameters", createParameterLayout())
{
    velocitySensitivity = apvts.getRawParameterValue (paramVelocitySensitivity);
    volumeDb = apvts.getRawParameterValue (paramVolumeDb);
    blend1 = apvts.getRawParameterValue (paramBlend1);
    blend2 = apvts.getRawParameterValue (paramBlend2);
    for (size_t i = 0; i < notes.size(); ++i)
    {
        notes[i].store (cymbalis::kSlots[i].defaultNote);
        sticks[i].store ((int) cymbalis::kSlots[i].defaultStick);
    }
    parsePresets();
    // The cymbal: one engine, loaded with the first preset. Its clock and its strike-phase
    // sequence start here and carry over to every preset loaded later.
    current = buildCymbal (0, 0.0).release();
    loader = std::make_unique<Loader> (*this);
    loader->startThread();
}

CymbalisProcessor::~CymbalisProcessor()
{
    loader->stopThread (2000);
    delete pending.exchange (nullptr);
    delete retired.exchange (nullptr);
    delete fading;
    delete current;
}


// Exactly the four parameters of docs/UI_SPEC.md section 9.
juce::AudioProcessorValueTreeState::ParameterLayout CymbalisProcessor::createParameterLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> params;

    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { paramVelocitySensitivity, 1 }, "Velocity Sensitivity", NormalisableRange<float> (0.0f, 1.0f), 1.0f,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return percentText (v); })
            .withValueFromStringFunction ([] (const String& s) { return s.retainCharacters ("0123456789.-").getFloatValue() / 100.0f; })));

    // Knob curve: dB = 6 - 66 (1 - p)^2 for knob position p, so 0 dB sits at p = 0.70.
    NormalisableRange<float> volumeRange (
        -60.0f, 6.0f,
        [] (float, float, float p) { return 6.0f - 66.0f * (1.0f - p) * (1.0f - p); },
        [] (float, float, float db) { return 1.0f - std::sqrt (jlimit (0.0f, 66.0f, 6.0f - db) / 66.0f); });
    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { paramVolumeDb, 1 }, "Volume", volumeRange, 0.0f,
        AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) {
                const float r = std::abs (v) < 0.05f ? 0.0f : v; // "+0.0 dB", never "-0.0 dB"
                return (r >= 0.0f ? "+" : "") + String (r, 1) + " dB";
            })
            .withValueFromStringFunction ([] (const String& s) { return s.retainCharacters ("0123456789.-+").getFloatValue(); })));

    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { paramBlend1, 1 }, "Blend Position 1", NormalisableRange<float> (0.0f, 1.0f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
            const int bow = roundToInt (v * 100.0f);
            return "BELL " + String (100 - bow) + " " + middleDot + " " + String (bow) + " BOW";
        })));

    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { paramBlend2, 1 }, "Blend Position 2", NormalisableRange<float> (0.0f, 1.0f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
            const int edge = roundToInt (v * 100.0f);
            return "BOW " + String (100 - edge) + " " + middleDot + " " + String (edge) + " EDGE";
        })));

    return { params.begin(), params.end() };
}

// The baked presets are compiled in and parsed here: they are data. A preset that cannot be
// loaded stays silent and the editor shows its error.
void CymbalisProcessor::parsePresets()
{
    for (size_t k = 0; k < presets.size(); ++k)
    {
        const auto& info = cymbalis::kPresets[k];
        auto& preset = presets[k];
        try
        {
            int size = 0;
            const char* data = findResource (info.bakeFile, size);
            if (data == nullptr)
                throw cymbalis::BakeError (std::string ("the ") + info.name + " preset is missing from the plugin");
            auto bake = cymbalis::loadBakeFromText (std::string (data, (size_t) size), info.name);
            auto entryFor = [&] (const char* name) {
                if (*name == 0)
                    return -1; // a silent row
                const int e = bake->slotIndex (name);
                if (e < 0)
                    throw cymbalis::BakeError (std::string ("the ") + info.name + " preset has no " + name + " entry");
                return e;
            };
            for (size_t s = 0; s < preset.entry.size(); ++s)
            {
                preset.entry[s][0] = entryFor (info.slots[s].tipEntry);
                preset.entry[s][1] = entryFor (info.slots[s].shoulderEntry);
            }
            preset.bake = std::move (bake);
        }
        catch (const std::exception& e)
        {
            preset.bake.reset();
            preset.error = juce::String (e.what());
            if (preset.error.isEmpty())
                preset.error = "unknown error";
        }
    }
}

// The engine loaded with a preset's bake, prepared at sampleRate (if known). Allocates: never
// on the audio thread.
std::unique_ptr<CymbalisProcessor::Cymbal> CymbalisProcessor::buildCymbal (int preset, double sampleRate)
{
    auto c = std::make_unique<Cymbal>();
    c->preset = preset;
    const auto& bake = presets[(size_t) preset].bake;
    if (bake == nullptr)
        return c;
    try
    {
        c->engine = std::make_unique<cymbalis::FastEngine> (bake);
        if (sampleRate > 0.0)
            c->engine->prepare (sampleRate);
    }
    catch (const std::exception& e)
    {
        c->engine.reset();
        if (! buildFailed[(size_t) preset].load())
        {
            buildError[(size_t) preset] = juce::String (e.what());
            buildFailed[(size_t) preset].store (true);
        }
    }
    return c;
}

// Loader thread: frees the cymbal the audio thread retired, and builds the chosen preset's
// cymbal at the prepared rate when it is not the newest one.
void CymbalisProcessor::loaderTick()
{
    delete retired.exchange (nullptr);
    const juce::ScopedLock sl (buildLock);
    const double rate = preparedRate.load();
    const int target = presetTarget.load();
    if (rate <= 0.0 || target == scheduledPreset.load())
        return;
    auto c = buildCymbal (target, rate);
    scheduledPreset.store (target);
    // A cymbal still pending was never taken by the audio thread: it is ours to free.
    delete pending.exchange (c.release());
}

juce::String CymbalisProcessor::presetError() const
{
    const int k = presetIndex();
    if (presets[(size_t) k].error.isNotEmpty())
        return presets[(size_t) k].error;
    return buildFailed[(size_t) k].load() ? buildError[(size_t) k] : juce::String();
}

void CymbalisProcessor::prepareToPlay (double sampleRate, int)
{
    setLatencySamples (0);
    // The audio thread is stopped here. The cymbal ends up loaded with the chosen preset and
    // prepared at this rate. A new rate stops the ring; the same rate again keeps it. The
    // clock and the strike-phase sequence continue either way.
    const juce::ScopedLock sl (buildLock);
    delete pending.exchange (nullptr);
    delete retired.exchange (nullptr);
    delete fading;
    fading = nullptr;
    const int target = presetTarget.load();
    if (current == nullptr || current->preset != target)
    {
        auto c = buildCymbal (target, sampleRate);
        if (c->engine != nullptr && current != nullptr && current->engine != nullptr)
            c->engine->continueFrom (*current->engine); // also from a cymbal never prepared: time 0
        delete current;
        current = c.release();
    }
    else if (current->engine != nullptr && ! juce::exactlyEqual (current->engine->sampleRate(), sampleRate))
        current->engine->prepare (sampleRate);
    scheduledPreset.store (target);
    preparedRate.store (sampleRate);

    fadeLength = std::max (1, juce::roundToInt (kPresetFadeSeconds * sampleRate));
    fadePos = 0;
    fadeLeft.assign (kFadeChunk, 0.0f);
    fadeRight.assign (kFadeChunk, 0.0f);
    gain.reset (sampleRate, 0.02);
    gain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (volumeDb->load(), -1000.0f));
}

void CymbalisProcessor::releaseResources() {}

bool CymbalisProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void CymbalisProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, n);
    if (buffer.getNumChannels() < 2)
    {
        buffer.clear();
        return;
    }
    float* left = buffer.getWritePointer (0);
    float* right = buffer.getWritePointer (1);

    // Preset switch: take the cymbal the loader built, if any. It continues the engine clock
    // and strike-phase sequence and plays from this block on. If the old cymbal is ringing it
    // fades out alongside for kPresetFadeSeconds; then the loader frees it. While a fade or a
    // free is outstanding the next switch waits. Nothing here allocates, frees or locks.
    if (fading == nullptr && retired.load() == nullptr)
    {
        Cymbal* next = pending.load();
        if (next != nullptr && pending.compare_exchange_strong (next, nullptr))
        {
            const double rate = preparedRate.load();
            if (next->engine != nullptr && ! juce::exactlyEqual (next->engine->sampleRate(), rate))
                retired.store (next); // built for another rate: the loader frees it and builds again
            else
            {
                if (next->engine != nullptr && current != nullptr && current->engine != nullptr)
                    next->engine->continueFrom (*current->engine);
                Cymbal* old = current;
                current = next;
                if (old != nullptr && old->engine != nullptr && old->engine->isPrepared() && ! old->engine->isSilent()
                    && ! fadeLeft.empty())
                {
                    fading = old;
                    fadePos = 0;
                }
                else if (old != nullptr)
                    retired.store (old);
            }
        }
    }
    auto* engine = current != nullptr ? current->engine.get() : nullptr;
    const bool playing = engine != nullptr && engine->isPrepared();

    const int ch = channel.load();
    const double sens = velocitySensitivity->load();
    const double b1 = blend1->load();
    const double b2 = blend2->load();

    int pos = 0;
    for (const auto meta : midi)
    {
        // Raw bytes, so a long (sysex) event never allocates. Only note-ons with a velocity
        // above 0 on the selected channel count; note-off, sustain and the rest are ignored.
        const juce::uint8* d = meta.data;
        if (meta.numBytes < 3 || (d[0] & 0xf0) != 0x90 || d[2] == 0 || (d[0] & 0x0f) + 1 != ch)
            continue;
        const int note = d[1] & 0x7f;
        int learning = learnSlot.load();
        if (learning >= 0 && learnSlot.compare_exchange_strong (learning, -1))
            notes[(size_t) learning].store (note);
        if (! playing)
            continue;
        const double vIn = (d[2] & 0x7f) / 127.0;
        const double velocity = kVelocityFloor + sens * (vIn - kVelocityFloor);
        strikeSlots (*current, note, velocity, b1, b2, left, right, pos, juce::jlimit (0, n, meta.samplePosition));
    }
    if (playing)
    {
        if (pos < n)
            engine->process (left + pos, right + pos, n - pos);
    }
    else
        buffer.clear (0, n);

    if (fading != nullptr)
    {
        int done = 0;
        while (done < n && fadePos < fadeLength)
        {
            const int m = std::min ({ n - done, (int) fadeLeft.size(), fadeLength - fadePos });
            fading->engine->process (fadeLeft.data(), fadeRight.data(), m);
            for (int i = 0; i < m; ++i)
            {
                // raised cosine from 1 to 0
                const float g = 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * (float) (fadePos + i) / (float) fadeLength);
                left[done + i] += g * fadeLeft[(size_t) i];
                right[done + i] += g * fadeRight[(size_t) i];
            }
            done += m;
            fadePos += m;
        }
        if (fadePos >= fadeLength)
        {
            retired.store (fading); // freed by the loader
            fading = nullptr;
        }
    }

    gain.setTargetValue (juce::Decibels::decibelsToGain (volumeDb->load(), -1000.0f));
    if (gain.isSmoothing())
    {
        for (int i = 0; i < n; ++i)
        {
            const float g = gain.getNextValue();
            left[i] *= g;
            right[i] *= g;
        }
    }
    else
    {
        buffer.applyGain (0, 0, n, gain.getTargetValue());
        buffer.applyGain (1, 0, n, gain.getTargetValue());
    }
}

// Strikes every slot whose note is `note`, at sample `at` of the block, after rendering the
// block up to there. A slot plays its preset's entry for the chosen stick where the slot
// table offers a choice (StickDisplay::choice), else its fixed entry; silent slots are skipped.
void CymbalisProcessor::strikeSlots (Cymbal& c, int note, double velocity, double b1, double b2, float* left,
                                     float* right, int& pos, int at)
{
    const auto& info = cymbalis::kPresets[(size_t) c.preset];
    const auto& preset = presets[(size_t) c.preset];
    for (size_t s = 0; s < cymbalis::kSlots.size(); ++s)
    {
        if (notes[s].load() != note)
            continue;
        const bool choice = info.slots[s].stick == cymbalis::StickDisplay::choice;
        const int stick = choice && sticks[s].load() == (int) cymbalis::Stick::shoulder ? 1 : 0;
        const int entry = preset.entry[s][(size_t) stick];
        if (entry < 0)
            continue;
        if (at > pos)
        {
            c.engine->process (left + pos, right + pos, at - pos);
            pos = at;
        }
        c.engine->strike (entry, velocity, cymbalis::slotPosition (cymbalis::kSlots[s], b1, b2));
    }
}

void CymbalisProcessor::setPreset (int index) noexcept
{
    if (index >= 0 && index < cymbalis::kNumPresets)
        presetTarget.store (index);
}

cymbalis::Stick CymbalisProcessor::shownStick (int slot) const noexcept
{
    switch (cymbalis::kPresets[(size_t) presetIndex()].slots[(size_t) slot].stick)
    {
        case cymbalis::StickDisplay::choice: return slotStick (slot);
        case cymbalis::StickDisplay::tip: return cymbalis::Stick::tip;
        case cymbalis::StickDisplay::shoulder: return cymbalis::Stick::shoulder;
        case cymbalis::StickDisplay::dash: break;
    }
    return cymbalis::Stick::none;
}

bool CymbalisProcessor::stickSelectable (int slot) const noexcept
{
    return cymbalis::kPresets[(size_t) presetIndex()].slots[(size_t) slot].stick == cymbalis::StickDisplay::choice;
}

void CymbalisProcessor::setMidiChannel (int newChannel) noexcept
{
    channel.store (juce::jlimit (1, 16, newChannel));
}

void CymbalisProcessor::setSlotNote (int slot, int note) noexcept
{
    if (slot >= 0 && slot < cymbalis::kNumSlots)
        notes[(size_t) slot].store (juce::jlimit (-1, 127, note));
}

void CymbalisProcessor::setSlotStick (int slot, cymbalis::Stick stick) noexcept
{
    if (slot >= 0 && slot < cymbalis::kNumSlots && cymbalis::hasStickChoice (slot) && stick != cymbalis::Stick::none)
        sticks[(size_t) slot].store ((int) stick);
}

void CymbalisProcessor::setEditorSize (int w, int h) noexcept
{
    editorWidth.store (w);
    editorHeight.store (h);
}

// The parameters, plus the session settings that are not automatable: MIDI channel,
// note and stick per slot, preset and editor size (UI_SPEC section 9).
void CymbalisProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    if (xml == nullptr)
        return;
    auto* session = xml->createNewChildElement ("Session");
    session->setAttribute ("preset", cymbalis::kPresets[(size_t) presetIndex()].id);
    session->setAttribute ("midiChannel", channel.load());
    for (size_t i = 0; i < notes.size(); ++i)
    {
        session->setAttribute (juce::String ("note_") + cymbalis::kSlots[i].label, notes[i].load());
        if (cymbalis::hasStickChoice ((int) i)) // kept whatever preset plays
            session->setAttribute (juce::String ("stick_") + cymbalis::kSlots[i].label,
                                   cymbalis::stickName ((cymbalis::Stick) sticks[i].load()));
    }
    session->setAttribute ("editorWidth", editorWidth.load());
    session->setAttribute ("editorHeight", editorHeight.load());
    copyXmlToBinary (*xml, destData);
}

// Safe before prepareToPlay: it only sets parameters and atomics.
void CymbalisProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;
    if (auto* session = xml->getChildByName ("Session"))
    {
        setMidiChannel (session->getIntAttribute ("midiChannel", cymbalis::kDefaultMidiChannel));
        for (size_t i = 0; i < notes.size(); ++i)
        {
            const auto& slot = cymbalis::kSlots[i];
            setSlotNote ((int) i, session->getIntAttribute (juce::String ("note_") + slot.label, slot.defaultNote));
            if (cymbalis::hasStickChoice ((int) i))
            {
                const auto stick = session->getStringAttribute (juce::String ("stick_") + slot.label,
                                                                cymbalis::stickName (slot.defaultStick));
                setSlotStick ((int) i, stick == "TIP" ? cymbalis::Stick::tip : cymbalis::Stick::shoulder);
            }
        }
        setEditorSize (session->getIntAttribute ("editorWidth", 0), session->getIntAttribute ("editorHeight", 0));
        const auto id = session->getStringAttribute ("preset", cymbalis::kPresets[0].id);
        int preset = 0;
        for (int k = 0; k < cymbalis::kNumPresets; ++k)
            if (id == cymbalis::kPresets[(size_t) k].id)
                preset = k;
        setPreset (preset);
        xml->removeChildElement (session, true);
    }
    learnSlot.store (-1);
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* CymbalisProcessor::createEditor()
{
    return new CymbalisEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CymbalisProcessor();
}
