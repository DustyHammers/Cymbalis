#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "FastEngine.h"
#include "SlotTable.h"

#include <array>
#include <atomic>
#include <memory>
#include <vector>

// Cymbalis: one cymbal engine (FastEngine) playing one of three baked presets (nl_amount 0,
// compiled in, parsed at load). MIDI note-ons on the selected channel strike the slots whose
// notes they match, at their exact sample. Choosing a preset loads its bake into the engine:
// the new engine state is built off the audio thread and handed over without locks.
// Parameters: exactly the four of docs/UI_SPEC.md section 9; the preset is session state,
// like the MIDI channel.
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

    // The current preset (cymbalis::kPresets), its display name and, if it failed to load,
    // why (that preset is then silent).
    int presetIndex() const noexcept { return presetTarget.load(); }
    void setPreset (int index) noexcept;
    juce::String presetName() const { return cymbalis::kPresets[(size_t) presetIndex()].name; }
    juce::String presetError() const;

    // What a row's stick box shows in the current preset (Stick::none: a dash) and whether
    // it opens the TIP / SHOULDER popup.
    cymbalis::Stick shownStick (int slot) const noexcept;
    bool stickSelectable (int slot) const noexcept;

    int midiChannel() const noexcept { return channel.load(); }
    void setMidiChannel (int newChannel) noexcept;

    // Note of a slot, -1 = unassigned.
    int slotNote (int slot) const noexcept { return notes[(size_t) slot].load(); }
    void setSlotNote (int slot, int note) noexcept;

    // The stick chosen for a slot (TIP or SHOULDER) in presets that offer a choice on it
    // (StickDisplay::choice); kept while a preset without the choice plays. Not a parameter.
    cymbalis::Stick slotStick (int slot) const noexcept { return (cymbalis::Stick) sticks[(size_t) slot].load(); }
    void setSlotStick (int slot, cymbalis::Stick stick) noexcept;

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
    // A baked preset: data, parsed once at plugin load.
    struct Preset
    {
        std::shared_ptr<const cymbalis::BakeData> bake; // null when it failed to load
        // Bake entry of each slot for TIP [0] and SHOULDER [1]; -1 = the slot is silent.
        std::array<std::array<int, 2>, cymbalis::kNumSlots> entry {};
        juce::String error;
    };

    // The cymbal: the engine loaded with one preset's bake.
    struct Cymbal
    {
        int preset = 0;
        std::unique_ptr<cymbalis::FastEngine> engine; // null if the engine could not be built
    };

    class Loader;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void parsePresets();
    std::unique_ptr<Cymbal> buildCymbal (int preset, double sampleRate);
    void loaderTick();
    void strikeSlots (Cymbal& c, int note, double velocity, double b1, double b2, float* left, float* right, int& pos,
                      int at);

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* velocitySensitivity = nullptr;
    std::atomic<float>* volumeDb = nullptr;
    std::atomic<float>* blend1 = nullptr;
    std::atomic<float>* blend2 = nullptr;

    std::array<Preset, cymbalis::kNumPresets> presets;
    std::atomic<int> presetTarget { 0 }; // set by the editor or the session

    // Ownership of cymbals. The audio thread plays `current` and, for 30 ms after a switch,
    // fades out `fading`. The loader thread builds the next cymbal into `pending`, which the
    // audio thread takes at the start of a block, and frees what the audio thread puts in
    // `retired`. Outside a fade only one cymbal exists. prepareToPlay (audio stopped) may
    // touch all of them, under buildLock against the loader.
    Cymbal* current = nullptr;
    Cymbal* fading = nullptr;
    std::atomic<Cymbal*> pending { nullptr };
    std::atomic<Cymbal*> retired { nullptr };
    std::atomic<int> scheduledPreset { 0 }; // preset of the newest cymbal (pending or current)
    std::atomic<double> preparedRate { 0.0 };
    juce::CriticalSection buildLock; // loader and prepareToPlay only, never the audio thread
    std::array<std::atomic<bool>, cymbalis::kNumPresets> buildFailed {};
    std::array<juce::String, cymbalis::kNumPresets> buildError; // set before buildFailed
    std::unique_ptr<Loader> loader;

    int fadePos = 0, fadeLength = 1;
    std::vector<float> fadeLeft, fadeRight; // allocated in prepareToPlay

    std::atomic<int> channel { cymbalis::kDefaultMidiChannel };
    std::array<std::atomic<int>, cymbalis::kNumSlots> notes;
    std::array<std::atomic<int>, cymbalis::kNumSlots> sticks; // cymbalis::Stick
    std::atomic<int> learnSlot { -1 };
    std::atomic<int> editorWidth { 0 }, editorHeight { 0 };

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> gain { 1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CymbalisProcessor)
};
