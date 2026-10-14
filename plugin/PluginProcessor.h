#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "FastEngine.h"
#include "SlotTable.h"

#include <array>
#include <atomic>
#include <memory>

// Cymbalis: the Nova Ride preset (baked, nl_amount 0, compiled in) played by the real-time
// engine. MIDI note-ons on the selected channel strike the slots whose notes they match,
// at their exact sample. Parameters: exactly the four of docs/UI_SPEC.md section 9.
class CymbalisProcessor final : public juce::AudioProcessor
{
public:
    CymbalisProcessor();
    ~CymbalisProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- for the editor (message thread)
    juce::AudioProcessorValueTreeState& parameters() noexcept { return apvts; }

    // The preset: its display name and, if it failed to load, why (the plugin is then silent).
    juce::String presetName() const { return "NOVA RIDE"; }
    const juce::String& presetError() const noexcept { return loadError; }

    int midiChannel() const noexcept { return channel.load(); }
    void setMidiChannel (int newChannel) noexcept;

    // Note of a slot, -1 = unassigned.
    int slotNote (int slot) const noexcept { return notes[(size_t) slot].load(); }
    void setSlotNote (int slot, int note) noexcept;

    // MIDI learn: the next note-on on the selected channel is assigned to this slot.
    void startMidiLearn (int slot) noexcept { learnSlot.store (slot); }
    void cancelMidiLearn() noexcept { learnSlot.store (-1); }
    int midiLearnSlot() const noexcept { return learnSlot.load(); }

    // Editor size saved with the session (0 x 0 = never set).
    juce::Point<int> editorSize() const noexcept { return { editorWidth.load(), editorHeight.load() }; }
    void setEditorSize (int w, int h) noexcept;

    static constexpr const char* paramVelocitySensitivity = "velocity_sensitivity";
    static constexpr const char* paramVolumeDb = "volume_db";
    static constexpr const char* paramBlend1 = "blend_1";
    static constexpr const char* paramBlend2 = "blend_2";

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void loadPreset();

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* velocitySensitivity = nullptr;
    std::atomic<float>* volumeDb = nullptr;
    std::atomic<float>* blend1 = nullptr;
    std::atomic<float>* blend2 = nullptr;

    std::shared_ptr<const cymbalis::BakeData> bake;
    std::unique_ptr<cymbalis::FastEngine> engine; // null when the preset failed to load
    std::array<int, cymbalis::kNumSlots> engineSlot {}; // bake slot of each editor slot
    juce::String loadError;

    std::atomic<int> channel { cymbalis::kDefaultMidiChannel };
    std::array<std::atomic<int>, cymbalis::kNumSlots> notes;
    std::atomic<int> learnSlot { -1 };
    std::atomic<int> editorWidth { 0 }, editorHeight { 0 };

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> gain { 1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CymbalisProcessor)
};
