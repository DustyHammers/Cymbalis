#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "FastEngine.h"
#include "SlotTable.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

// Cymbalis: one cymbal engine (FastEngine) playing one of three baked presets (nl_amount 0,
// compiled in, parsed at load). MIDI note-ons on the selected channel strike the slots whose
// notes they match, at their exact sample. Choosing a preset loads its bake into the engine:
// the new engine state is built off the audio thread and handed over without locks.
// Parameters: exactly the five of docs/UI_SPEC.md section 9; the preset is session state,
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

    // The stick chosen for a slot (TIP or SHOULDER) in the current preset, where the preset
    // offers a choice on it (StickDisplay::choice). Each preset keeps its own choices. Not a
    // parameter.
    cymbalis::Stick slotStick (int slot) const noexcept { return slotStick (presetIndex(), slot); }
    cymbalis::Stick slotStick (int preset, int slot) const noexcept
    {
        return (cymbalis::Stick) sticks[(size_t) preset][(size_t) slot].load();
    }
    void setSlotStick (int slot, cymbalis::Stick stick) noexcept { setSlotStick (presetIndex(), slot, stick); }
    void setSlotStick (int preset, int slot, cymbalis::Stick stick) noexcept;

    // MIDI learn: the next note-on on the selected channel is assigned to this slot.
    void startMidiLearn (int slot) noexcept { learnSlot.store (slot); }
    void cancelMidiLearn() noexcept { learnSlot.store (-1); }
    int midiLearnSlot() const noexcept { return learnSlot.load(); }

    // Editor size saved with the session (0 x 0 = never set).
    juce::Point<int> editorSize() const noexcept { return { editorWidth.load(), editorHeight.load() }; }
    void setEditorSize (int w, int h) noexcept;

    // Hit display (docs/UI_SPEC.md section 10): one event per strike a note-on plays. The
    // audio thread only writes it into a preallocated single-producer, single-consumer FIFO
    // (pushHit); when the FIFO is full, as when no editor reads it, the event is dropped.
    // The editor, the only reader, takes them on its timer.
    struct HitEvent
    {
        std::int8_t slot;   // row, 0..4 (cymbalis::kSlots)
        std::int8_t stick;  // cymbalis::Stick: TIP or SHOULDER, as the row's stick box shows it
        float position;     // 0 edge, 0.5 bow, 1 bell, after the blend knobs
        float velocity;     // engine velocity, after Velocity Sensitivity
    };
    static constexpr int hitFifoSize = 64;
    bool popHit (HitEvent& e) noexcept;
    void clearHits() noexcept; // the editor, when it opens: drops what waited without one

#if CYMBALIS_TUNING
    // ---- tuning code only (CYMBALIS_WITH_TUNING, docs/TUNING.md), message thread
    // The bake of a preset (null if it failed to load), its baked tuning values and the
    // current ones. setTuning applies every value live; a new ring length rebuilds the
    // cymbal through the preset-switch path.
    std::shared_ptr<const cymbalis::BakeData> presetBake (int preset) const { return presets[(size_t) preset].bake; }
    cymbalis::Tuning tuning (int preset) const;
    void setTuning (int preset, const cymbalis::Tuning& t);

    // Meters: the output after the Volume and Pan knobs (peak since the last read, RMS over about
    // 300 ms, clip latched until resetClip), and the last hit before the Volume knob (its row
    // and bake entry, velocity, and peak and RMS over its first 500 ms).
    struct Meters
    {
        float outPeak = 0.0f, outRms = 0.0f;
        bool clip = false;
        int hitRow = -1, hitEntry = -1;
        float hitVelocity = 0.0f, hitPeak = 0.0f, hitRms = 0.0f;
        bool hitComplete = false;
    };
    Meters readMeters() noexcept;
    void resetClip() noexcept { meterClip.store (false); }
    // When each band of the last strike of a bake entry peaked (ms after the strike, -1: no
    // energy there; FastEngine::onsetPeaksMs). False until the entry has been struck.
    bool onsetPeaks (int preset, int entry, std::array<float, cymbalis::kExcitationBands>& out) const noexcept;
#endif

    static constexpr const char* paramVelocitySensitivity = "velocity_sensitivity";
    static constexpr const char* paramVolumeDb = "volume_db";
    static constexpr const char* paramBlend1 = "blend_1";
    static constexpr const char* paramBlend2 = "blend_2";
    static constexpr const char* paramPan = "pan";

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
#if CYMBALIS_TUNING
        double ring = 1.0; // ring length it was built with
#endif
    };

    class Loader;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void parsePresets();
    std::unique_ptr<Cymbal> buildCymbal (int preset, double sampleRate);
    void loaderTick();
    void strikeSlots (Cymbal& c, int note, double velocity, double b1, double b2, float* left, float* right, int& pos,
                      int at);
    void pushHit (int slot, cymbalis::Stick stick, double position, double velocity) noexcept; // audio thread

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* velocitySensitivity = nullptr;
    std::atomic<float>* volumeDb = nullptr;
    std::atomic<float>* blend1 = nullptr;
    std::atomic<float>* blend2 = nullptr;
    std::atomic<float>* pan = nullptr;

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
    std::array<std::array<std::atomic<int>, cymbalis::kNumSlots>, cymbalis::kNumPresets> sticks; // cymbalis::Stick
    std::atomic<int> learnSlot { -1 };
    std::atomic<int> editorWidth { 0 }, editorHeight { 0 };

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> gain { 1.0f };
    juce::SmoothedValue<float> panPosition { 0.0f }; // linear: it runs through 0 to negative values

    // Hit display FIFO: the audio thread writes, the editor reads (lock-free, no allocation).
    juce::AbstractFifo hitFifo { hitFifoSize + 1 }; // room for hitFifoSize events
    std::array<HitEvent, hitFifoSize + 1> hitBuffer {};

#if CYMBALIS_TUNING
    // Tuning state per preset, owned by the message thread; the loader reads it under the lock.
    mutable juce::CriticalSection tuningLock; // message and loader threads, never the audio thread
    std::array<cymbalis::Tuning, cymbalis::kNumPresets> tunings {};
    double scheduledRing = 1.0; // ring length of the newest cymbal (pending or current)
    // Values for the audio thread, through a triple buffer (no lock, no allocation): the
    // writer fills `tuningBack` and swaps it with the middle; the audio thread takes the
    // middle when it is new.
    struct TuningMessage
    {
        int preset = -1;
        cymbalis::Tuning tuning;
        std::vector<float> modeGains; // every slot's per-mode excitation gains, computed by the writer
        std::vector<double> velw;     // the NPZ velw table for EDGE VOICE FROM BOW (presets with NPZ edge data)
        bool velwValid = false;
        // The decay coefficients for its per-band ring lengths at the prepared rate, computed by
        // the writer when they changed (decayValid).
        cymbalis::FastEngine::DecaySet decay;
        bool decayValid = false;
        // Every mode's output gains for its stereo values, from the writer (gainsValid): the
        // bake's gains for the bake's own values, else computed.
        std::vector<double> gainL, gainR;
        bool gainsValid = false;
    };
    // Per preset (message thread): the decay coefficients for its latest per-band ring lengths
    // at the prepared rate, sent with every message once they differ from the bake (so a
    // message that overtakes another still carries them), and the contact force they use.
    struct WriterDecay
    {
        bool valid = false;
        std::array<double, cymbalis::kExcitationBands> ringBand {};
        double ringLength = 0.0, rate = 0.0;
        cymbalis::FastEngine::DecaySet set;
        std::vector<double> force;
        int forceLen = 0;
        double forceRate = 0.0;
    };
    std::array<WriterDecay, cymbalis::kNumPresets> writerDecay;
    std::array<TuningMessage, 3> tuningBuffers {};
    int tuningBack = 0, tuningFront = 2;  // writer's and audio thread's buffers
    std::atomic<int> tuningMiddle { 1 };  // index | kTuningNew
    static constexpr int kTuningNew = 4;
    void publishTuning (int preset, const cymbalis::Tuning& t); // under tuningLock
    void takeTuning (bool newCymbal) noexcept;                  // audio thread
    void applyTuningToNewCymbal (Cymbal& c);                    // loader / prepareToPlay

    // Meters (written by the audio thread).
    void meterBlock (const float* left, const float* right, int n, bool afterVolume) noexcept;
    void startHitMeter (int row, int entry, double velocity, int at) noexcept;
    std::atomic<float> meterPeak { 0.0f }, meterRms { 0.0f };
    std::atomic<bool> meterClip { false };
    double meterMs = 0.0; // mean square, smoothed (audio thread)
    std::atomic<int> hitRow { -1 }, hitEntry { -1 };
    std::atomic<float> hitVelocity { 0.0f }, hitPeakOut { 0.0f }, hitRmsOut { 0.0f };
    std::atomic<bool> hitComplete { false };
    // The last strike's band peaks per preset and entry (written by the audio thread; a reader
    // may see a mix of two strikes' values for a moment).
    std::array<std::array<std::array<std::atomic<float>, cymbalis::kExcitationBands>, cymbalis::kMaxArticulations>,
               cymbalis::kNumPresets> peakMs {};
    std::array<std::array<std::atomic<bool>, cymbalis::kMaxArticulations>, cymbalis::kNumPresets> peakSet {};
    int hitStart = 0;      // sample of the current block where the last hit began
    long hitLeft = 0;      // samples of its 500 ms still to measure
    long hitCount = 0;     // samples measured
    double hitSum = 0.0, hitMax = 0.0;
    void writeTuningState (juce::XmlElement& session) const;
    void readTuningState (const juce::XmlElement& session);
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CymbalisProcessor)
};
