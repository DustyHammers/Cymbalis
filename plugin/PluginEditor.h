#pragma once

#include "PluginProcessor.h"
#include "ui/HitDisplay.h"
#include "ui/UiAssets.h"

#include <array>
#include <functional>
#include <memory>

#if CYMBALIS_TUNING
namespace cymbalis::tuning
{
class TuningWindow;
}
#endif

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
    enum class Align
    {
        centre, // the CYMBAL box
        left    // the MIDI CHANNEL box
    };
    DropdownBox (const Assets& assets, juce::Point<float> arrowTopLeft, Align valueAlign = Align::centre);

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
    Align align;
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
    void setLit (bool l); // the hit display lights the row that played
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
    bool lit = false;
    int dragStartNote = -1;
    bool dragged = false;
};

// A stick box (STICK ARTICULATION column). Rows with a choice open a TIP / SHOULDER popup;
// the others show their fixed stick, or a dash, and do nothing.
class StickBox final : public juce::Component
{
public:
    explicit StickBox (const Assets& assets);

    void setStick (Stick s, bool selectable);
    void setOpen (bool shouldBeOpen);
    void setLit (bool l); // the hit display lights the row that played
    std::function<void()> onClick;

    void paint (juce::Graphics& g) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override;

private:
    const Assets& assets;
    Stick stick = Stick::none;
    bool selectable = false;
    bool open = false;
    bool lit = false;
};

// The note box's menu: MIDI LEARN, RESET DEFAULT and NONE, then a scrollable list of every
// note, C-2 (0) to G8 (127), with its number. The current note is selected and in view.
class NoteMenu final : public juce::Component
{
public:
    NoteMenu (const Assets& assets, int currentNote);

    enum Action { learn = 0, resetDefault = 1, none = 2 };
    std::function<void (int)> onAction; // Action
    std::function<void (int)> onNote;   // MIDI note

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent& e) override;

    // Layout, in design units (tools/cymbalis_uitest uses the same numbers).
    static constexpr int width = 170;
    static constexpr int inset = 8;       // border + padding
    static constexpr int actionHeight = 34;
    static constexpr int gap = 8;         // between the actions and the list
    static constexpr int rowHeight = 26;
    static constexpr int visibleRows = 10;
    static constexpr int rowsAboveCurrent = 4; // the current note is the 5th visible row
    static int height() { return 2 * inset + 3 * actionHeight + gap + visibleRows * rowHeight; }

private:
    class List;
    int actionAt (juce::Point<int> p) const;
    juce::Rectangle<int> actionBounds (int i) const;

    const Assets& assets;
    std::unique_ptr<List> list;
    juce::Viewport viewport;
    int hover = -1;
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
    void showPopup (std::unique_ptr<juce::Component> panel, juce::Rectangle<int> bounds, std::function<void()> onClose);
    void closePopup();
    void openPresetMenu();
    void openChannelMenu();
    void openNoteMenu (int slot);
    void openStickMenu (int slot);

    void paintLabels (juce::Graphics& g);
    void paintRows (juce::Graphics& g);
    void paintLines (juce::Graphics& g);
    juce::Rectangle<int> rowLabelArea (int slot) const;

    CymbalisProcessor& processor;
    Assets assets;

    // The wireframe cymbal (under every control), and the hits it moves with.
    std::unique_ptr<CymbalView> cymbalView;
    std::unique_ptr<HitTracker> hitTracker;

    std::array<KnobSpec, 5> knobSpecs;
    std::array<std::unique_ptr<Knob>, 5> knobs;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 5> attachments;
    int draggingKnob = -1;

    DropdownBox presetBox, channelBox;
#if CYMBALIS_TUNING
    // The TUNING button (under the CYMBAL box) opens and closes the Tuning window.
    class TuningButton;
    std::unique_ptr<TuningButton> tuningButton;
    void toggleTuningWindow (bool open);
#endif
    std::array<std::unique_ptr<NoteBox>, kNumSlots> noteBoxes;
    std::array<std::unique_ptr<StickBox>, kNumSlots> stickBoxes;

    // Popup: a transparent layer that closes it on any click outside, and the panel.
    std::unique_ptr<juce::Component> popupLayer;
    std::unique_ptr<juce::Component> popup;
    std::function<void()> popupClosed;

    std::array<int, kNumSlots> shownNotes {};
    std::array<int, kNumSlots> shownSticks {};
    int shownChannel = -1;
    int shownPreset = -1;
    juce::String shownError;
    int shownLearn = -1;
    double learnStartMs = 0.0;
#if CYMBALIS_TUNING
    std::unique_ptr<tuning::TuningWindow> tuningWindow; // TUNING button, or Cmd-click on the logo
#endif
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
    bool sizeIsSet = false; // the constructor has chosen the opening size
    std::unique_ptr<cymbalis::ui::Content> content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CymbalisEditor)
};
