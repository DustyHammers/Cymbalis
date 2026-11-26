#pragma once

#include "PluginProcessor.h"
#include "ui/HiHatPedal.h"
#include "ui/HitDisplay.h"
#include "ui/UiAssets.h"

#include <array>
#include <functional>
#include <memory>

#if CYMBALIS_TUNING
namespace cymbalis::tuning
{
class TuningPanel;
}
#endif

namespace cymbalis::ui
{

// A knob, drawn in design units (UI_SPEC section 6, "The knob"): vertical drag, double-click
// resets to the default. Around it, its position arc (UI_SPEC section 6): from 7 o'clock for
// a unipolar knob, from 12 o'clock to either side for a bipolar one (Pan). The component is
// the knob's 128 x 128 square plus a margin that holds the arc; only the knob's square takes
// the mouse.
class Knob final : public juce::Slider
{
public:
    explicit Knob (bool bipolar);
    void paint (juce::Graphics& g) override;
    bool hitTest (int x, int y) override;

    static constexpr int margin = 4;              // around the knob's square, for the arc
    static constexpr int size = 128 + 2 * margin; // the component's width and height

private:
    const bool bipolar;
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

// The PEDAL row's box (the hi-hat; UI_SPEC section 3), drawn like a stick box: the pedal's MIDI
// CC, "CC 1" to "CC 127". A click opens a grid of CC 1-127.
class PedalBox final : public juce::Component
{
public:
    explicit PedalBox (const Assets& assets);

    void setCc (int cc);
    void setOpen (bool shouldBeOpen);
    std::function<void()> onClick;

    void paint (juce::Graphics& g) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override;

private:
    const Assets& assets;
    int cc = 1;
    bool open = false;
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

    // Called by the editor when its size, and so this component's transform, changes: the
    // TUNING page's panel follows the page area on screen (UI_SPEC section 6).
    void editorResized();

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
    void openPedalMenu();

    void paintLabels (juce::Graphics& g);
    void paintRows (juce::Graphics& g);
    void paintLines (juce::Graphics& g);
    juce::Rectangle<int> rowLabelArea (int slot) const;

    // The pages (UI_SPEC section 6): with the tuning code, CYMBAL (0) and TUNING (1), chosen by
    // the tabs; without it, only the CYMBAL page. applyPage() shows the open page's components
    // (those of the preset) and hides the other's.
    bool cymbalPage() const noexcept { return page == 0; }
    void applyPage();
    int page = 0;

    CymbalisProcessor& processor;
    Assets assets;

    // The wireframe cymbal (under every control), and the hits it moves with.
    std::unique_ptr<CymbalView> cymbalView;
    std::unique_ptr<HitTracker> hitTracker;

    std::array<KnobSpec, 5> knobSpecs;
    juce::Path logoPath; // the DHammers logo, in design units (paintLabels)
    std::array<std::unique_ptr<Knob>, 5> knobs;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 5> attachments;
    int draggingKnob = -1;

    DropdownBox presetBox, channelBox;
#if CYMBALIS_TUNING
    // The CYMBAL and TUNING tabs (left column), and the TUNING page's panel: made when the page
    // first opens, kept until the editor closes, and drawn at one pixel per pixel in the page
    // area's rectangle on screen (layoutTuningPanel), not scaled with the editor.
    class TabButton;
    std::array<std::unique_ptr<TabButton>, 2> tabs;
    std::unique_ptr<tuning::TuningPanel> tuningPanel;
    juce::Rectangle<int> tuningPanelArea; // the panel's area in design units, rounded inwards
    void showPage (int page);
    void layoutTuningPanel();
#endif
    std::array<std::unique_ptr<NoteBox>, kNumSlots> noteBoxes;
    std::array<std::unique_ptr<StickBox>, kNumSlots> stickBoxes;
    // The hi-hat's pedal (left column) and its PEDAL row, shown on a preset with a pedal.
    HiHatPedal pedal;
    PedalBox pedalBox;

    // Popup: a transparent layer that closes it on any click outside, and the panel.
    std::unique_ptr<juce::Component> popupLayer;
    std::unique_ptr<juce::Component> popup;
    std::function<void()> popupClosed;

    std::array<int, kNumSlots> shownNotes {};
    std::array<int, kNumSlots> shownSticks {};
    int shownChannel = -1;
    int shownPreset = -1;
    int shownPedalCc = -1;
    float shownOpening = -1.0f; // the pedal's opening the hats and the pedal show
    // The sizzle chain's envelope as the cymbal shows it (smoothed: a rise at once, a fall with a
    // 60 ms release, at most 1.5), and when it was last read.
    float chainEnvelope = 0.0f;
    double chainReadMs = 0.0;
    juce::String shownError;
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
    bool sizeIsSet = false; // the constructor has chosen the opening size
    std::unique_ptr<cymbalis::ui::Content> content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CymbalisEditor)
};
