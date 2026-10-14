#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <BinaryData.h>

#include <cmath>

namespace
{
constexpr double kVelocityFloor = 100.0 / 127.0; // vf in UI_SPEC section 9
constexpr const char* kPresetFile = "nova_ride.bake.json";
constexpr const char* kPresetId = "nova_ride";

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

CymbalisProcessor::CymbalisProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "CymbalisParameters", createParameterLayout())
{
    velocitySensitivity = apvts.getRawParameterValue (paramVelocitySensitivity);
    volumeDb = apvts.getRawParameterValue (paramVolumeDb);
    blend1 = apvts.getRawParameterValue (paramBlend1);
    blend2 = apvts.getRawParameterValue (paramBlend2);
    for (size_t i = 0; i < notes.size(); ++i)
        notes[i].store (cymbalis::kSlots[i].defaultNote);
    loadPreset();
}

CymbalisProcessor::~CymbalisProcessor() = default;

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

// The baked Nova Ride preset is compiled in. If it cannot be loaded, the engine stays null:
// the plugin outputs silence and the editor shows loadError.
void CymbalisProcessor::loadPreset()
{
    try
    {
        int size = 0;
        const char* data = findResource (kPresetFile, size);
        if (data == nullptr)
            throw cymbalis::BakeError ("the Nova Ride preset is missing from the plugin");
        bake = cymbalis::loadBakeFromText (std::string (data, (size_t) size), "Nova Ride preset");
        auto e = std::make_unique<cymbalis::FastEngine> (bake);
        for (size_t i = 0; i < engineSlot.size(); ++i)
        {
            engineSlot[i] = e->slotIndex (cymbalis::kSlots[i].bakeName);
            if (engineSlot[i] < 0)
                throw cymbalis::BakeError (std::string ("the preset has no slot ") + cymbalis::kSlots[i].bakeName);
        }
        engine = std::move (e); // the engine clock and the strike-phase sequence start here
    }
    catch (const std::exception& e)
    {
        engine.reset();
        loadError = juce::String (e.what());
        if (loadError.isEmpty())
            loadError = "unknown error";
    }
}

void CymbalisProcessor::prepareToPlay (double sampleRate, int)
{
    setLatencySamples (0);
    // A new rate recomputes the engine's coefficients (and stops the ring). The same rate
    // again keeps the cymbal ringing. Either way the clock and random sequence continue.
    if (engine != nullptr && ! juce::exactlyEqual (engine->sampleRate(), sampleRate))
        engine->prepare (sampleRate);
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
    if (engine == nullptr || ! engine->isPrepared() || buffer.getNumChannels() < 2)
    {
        buffer.clear();
        return;
    }

    float* left = buffer.getWritePointer (0);
    float* right = buffer.getWritePointer (1);
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

        const int at = juce::jlimit (0, n, meta.samplePosition);
        const double vIn = (d[2] & 0x7f) / 127.0;
        const double velocity = kVelocityFloor + sens * (vIn - kVelocityFloor);
        for (size_t s = 0; s < cymbalis::kSlots.size(); ++s)
        {
            if (notes[s].load() != note)
                continue;
            if (at > pos)
            {
                engine->process (left + pos, right + pos, at - pos);
                pos = at;
            }
            engine->strike (engineSlot[s], velocity, cymbalis::slotPosition (cymbalis::kSlots[s], b1, b2));
        }
    }
    if (pos < n)
        engine->process (left + pos, right + pos, n - pos);

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

void CymbalisProcessor::setMidiChannel (int newChannel) noexcept
{
    channel.store (juce::jlimit (1, 16, newChannel));
}

void CymbalisProcessor::setSlotNote (int slot, int note) noexcept
{
    if (slot >= 0 && slot < cymbalis::kNumSlots)
        notes[(size_t) slot].store (juce::jlimit (-1, 127, note));
}

void CymbalisProcessor::setEditorSize (int w, int h) noexcept
{
    editorWidth.store (w);
    editorHeight.store (h);
}

// The parameters, plus the session settings that are not automatable: MIDI channel,
// note per slot, preset and editor size (UI_SPEC section 9).
void CymbalisProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    if (xml == nullptr)
        return;
    auto* session = xml->createNewChildElement ("Session");
    session->setAttribute ("preset", kPresetId);
    session->setAttribute ("midiChannel", channel.load());
    for (size_t i = 0; i < notes.size(); ++i)
        session->setAttribute (juce::String ("note_") + cymbalis::kSlots[i].label, notes[i].load());
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
            setSlotNote ((int) i, session->getIntAttribute (juce::String ("note_") + cymbalis::kSlots[i].label,
                                                            cymbalis::kSlots[i].defaultNote));
        setEditorSize (session->getIntAttribute ("editorWidth", 0), session->getIntAttribute ("editorHeight", 0));
        // "preset": Nova Ride is the only preset, so there is nothing else to restore.
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
