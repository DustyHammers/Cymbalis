#pragma once

#include "PluginProcessor.h"
#include "ui/UiAssets.h"

#include <array>
#include <functional>
#include <memory>

namespace cymbalis::ui
{

// A knob drawn from the filmstrip (UI_SPEC sections 1, 6): vertical drag, double-click
// resets to the default.
class Knob final : public juce::Slider
{
public:
    explicit Knob (const Assets& assets);
    void paint (juce::Graphics& g) override;

private:
    const Assets& assets;
};

// The CYMBAL and MIDI CHANNEL boxes: value text, dropdown arrow, hover and open outlines.
class DropdownBox final : public juce::Component
{
public:
    DropdownBox (const Assets& assets, juce::Point<float> arrowTopLeft);

    void setText (const juce::String& t);
    void setOpen (bool shouldBeOpen);
    std::function<void()> onClick;

    void paint (juce::Graphics& g) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent&) override;

private:
    const Assets& assets;
    juce::Point<float> arrow; // in this component's coordinates
    juce::String text;
    bool open = false;
};

// The MIDI NOTE box of one slot: vertical drag changes the note; a click opens the
// MIDI LEARN / RESET DEFAULT menu; it pulses LEARN while learning.
class NoteBox final : public juce::Component
{
public:
    explicit NoteBox (const Assets& assets);

    void setNote (int n);
    void setLearning (bool l, float pulse);
    void setOpen (bool shouldBeOpen);
    std::function<void (int)> onNoteDragged;
    std::function<void()> onClick;

    void paint (juce::Graphics& g) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    const Assets& assets;
    int note = -1;
    bool learning = false;
    float pulse = 1.0f;
    bool open = false;
    int dragStartNote = -1;
    bool dragged = false;
};

// A popup list or grid in the editor's palette. Items are laid out in `columns` columns,
// row by row. It closes on a choice or on a click anywhere else.
class PopupPanel final : public juce::Component
{
public:
    PopupPanel (const Assets& assets, juce::StringArray items, int selected, int columns, juce::Point<int> itemSize);

    std::function<void (int)> onChoose; // index of the chosen item

    void paint (juce::Graphics& g) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent& e) override;

    static constexpr int border = 2;
    static constexpr int padding = 6;

private:
    int itemAt (juce::Point<int> p) const;
    juce::Rectangle<int> itemBounds (int index) const;

    const Assets& assets;
    juce::StringArray items;
    int selected, columns;
    juce::Point<int> itemSize;
    int hover = -1;
};

// Everything in design units, 1536 x 1024. The editor scales it with one transform.
class Content final : public juce::Component, private juce::Slider::Listener
{
public:
    explicit Content (CymbalisProcessor& p);
    ~Content() override;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;

    // Called by the editor's timer: picks up notes learnt on the audio thread and pulses LEARN.
    void refresh();

private:
    struct KnobSpec
    {
        const char* paramId;
        juce::Point<float> centre;
        const char* label;
        juce::Point<float> labelInk;
        float spacing;
        juce::Colour colour;
    };

    void sliderValueChanged (juce::Slider*) override;
    void sliderDragStarted (juce::Slider*) override;
    void sliderDragEnded (juce::Slider*) override;

    juce::Rectangle<int> labelArea (int knob) const;
    void showPopup (std::unique_ptr<PopupPanel> panel, juce::Rectangle<int> bounds, std::function<void()> onClose);
    void closePopup();
    void openPresetMenu();
    void openChannelMenu();
    void openNoteMenu (int slot);

    void paintLabels (juce::Graphics& g);
    void paintRows (juce::Graphics& g);
    void paintLines (juce::Graphics& g);
    void paintError (juce::Graphics& g);

    CymbalisProcessor& processor;
    Assets assets;

    std::array<KnobSpec, 4> knobSpecs;
    std::array<std::unique_ptr<Knob>, 4> knobs;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 4> attachments;
    int draggingKnob = -1;

    DropdownBox presetBox, channelBox;
    std::array<std::unique_ptr<NoteBox>, kNumSlots> noteBoxes;

    // Popup: a transparent layer that closes it on any click outside, and the panel.
    std::unique_ptr<juce::Component> popupLayer;
    std::unique_ptr<PopupPanel> popup;
    std::function<void()> popupClosed;

    std::array<int, kNumSlots> shownNotes {};
    int shownChannel = -1;
    int shownLearn = -1;
    double learnStartMs = 0.0;
};

} // namespace cymbalis::ui

class CymbalisEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit CymbalisEditor (CymbalisProcessor&);
    ~CymbalisEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    class ResizerLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;
    };

    CymbalisProcessor& processorRef;
    ResizerLookAndFeel resizerLookAndFeel;
    std::unique_ptr<cymbalis::ui::Content> content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CymbalisEditor)
};
