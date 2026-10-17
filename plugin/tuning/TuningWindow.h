#pragma once

// Tuning build only (docs/TUNING.md): the Tuning window, opened by Cmd-click on the CYMBALIS
// logo. Plain JUCE controls in the editor's palette and font; every value acts on the
// current preset, live.

#include "PluginProcessor.h"
#include "TuningFields.h"
#include "ShoulderProfiles.h"
#include "ui/UiAssets.h"

#include <functional>
#include <memory>
#include <vector>

namespace cymbalis::tuning
{

class TuningLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    explicit TuningLookAndFeel (const ui::Assets& assets, float textSize = 15.0f);
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowTitleFont() override;
    juce::Font font (float size) const;

private:
    const ui::Assets& assets;
    float textSize;
};

// One value: name, slider with numeric entry, unit, and a reset-to-baked button. With a
// step the slider moves in steps; a band value (0 .. 21) shows and takes the band's centre
// frequency (a number below 50 typed in is a band index).
class ValueRow final : public juce::Component
{
public:
    ValueRow (const juce::String& name, const juce::String& unit, double lo, double hi, int decimals, bool onRelease,
              double step = 0.0, bool band = false);

    void setValues (double value, double baked);
    std::function<void (double)> onChange;

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    juce::Label name;
    juce::Slider slider;
    std::unique_ptr<juce::Button> reset;
    juce::String unit;
    double step = 0.0;
    double baked = 0.0;
    bool updating = false;
};

// An on/off value with a reset-to-baked button.
class ToggleRow final : public juce::Component
{
public:
    explicit ToggleRow (const juce::String& name);

    void setValues (bool value, bool baked);
    std::function<void (bool)> onChange;

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    juce::ToggleButton toggle;
    std::unique_ptr<juce::Button> reset;
    bool baked = true;
    bool updating = false;
};

// A labelled action: a note on the left, a button on the right.
class ButtonRow final : public juce::Component
{
public:
    ButtonRow (const juce::String& note, const juce::String& button);

    std::function<void()> onClick;

    void resized() override;

private:
    juce::Label note;
    juce::TextButton button;
};

// One per-band value of an articulation (a BandField: excitation gain, onset delay or onset
// speed) as a row of vertical sliders, one per one-third-octave band, like a graphic EQ:
// numeric entry under each slider, the band's centre frequency, a reset-to-baked button,
// and FLAT for the whole row. A changed band is marked like any changed value.
class BandRow final : public juce::Component
{
public:
    // `scope` names what FLAT acts on in its tooltip ("this articulation", "the cymbal").
    BandRow (const BandSpec& field, TuningLookAndFeel& small, const juce::String& scope);
    ~BandRow() override;

    void setValues (const std::array<double, kExcitationBands>& values, const std::array<double, kExcitationBands>& baked);

    std::function<void (int band, double value)> onBandChange;
    std::function<void()> onFlat;

    void resized() override;
    void paint (juce::Graphics& g) override;
    static constexpr int height = 236;

private:
    struct Column
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::Button> reset;
    };
    const BandSpec& field;
    juce::Label title;
    juce::TextButton flat { "FLAT" };
    std::array<std::unique_ptr<Column>, kExcitationBands> columns;
    std::array<double, kExcitationBands> baked {};
    bool updating = false;
};

// The per-band values of the chosen articulation: a selector and one BandRow per BandField.
class BandEq final : public juce::Component
{
public:
    explicit BandEq (const ui::Assets& assets);

    void setArticulations (const juce::StringArray& names);
    int selected() const { return selector.getSelectedItemIndex(); }
    BandRow& row (size_t field) { return *rows[field]; }

    std::function<void()> onSelect;

    void resized() override;
    static int height() { return kSelectorHeight + (int) bandFields().size() * BandRow::height; }

private:
    static constexpr int kSelectorHeight = 40;
    TuningLookAndFeel small; // the band sliders' numbers and labels, in a smaller size
    juce::Label title;
    juce::ComboBox selector;
    std::vector<std::unique_ptr<BandRow>> rows;
    bool updating = false;
};

class TuningPanel final : public juce::Component, private juce::Timer
{
public:
    TuningPanel (CymbalisProcessor& p, const ui::Assets& assets);
    ~TuningPanel() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;

private:
    void timerCallback() override;
    void rebuild();           // rows for the current preset
    void showValues();        // the current values in every row
    void change (const std::function<void (Tuning&)>& edit);
    void save();
    void load();

    CymbalisProcessor& processor;
    const ui::Assets& assets;
    TuningLookAndFeel lookAndFeel;
    int preset = -1;
    std::shared_ptr<const BakeData> bake;

    juce::TextButton saveButton { "SAVE..." }, loadButton { "LOAD..." }, resetAllButton { "RESET ALL" };
    juce::Viewport viewport;
    juce::Component list;
    std::vector<std::unique_ptr<juce::Component>> headers;
    std::vector<std::unique_ptr<ValueRow>> rows;
    std::vector<std::unique_ptr<ButtonRow>> buttonRows;
    std::vector<std::function<void()>> refreshers; // one per row: shows its current value
    std::unique_ptr<ToggleRow> restartRow;
    std::unique_ptr<BandEq> bandEq;
    TuningLookAndFeel bandLook; // the whole-cymbal band rows' numbers and labels
    std::vector<std::unique_ptr<BandRow>> cymbalBandRows;
    std::unique_ptr<juce::FileChooser> chooser;

    CymbalisProcessor::Meters meters;
    float heldPeak = 0.0f;
    double heldAt = 0.0;
    bool modified = false;
    juce::Rectangle<int> clipBox;
};

// The Tuning window. It is attached to the window that holds `owner` (the editor): on
// macOS a child window of it, so it stays in front of the editor and the host, on the same
// Space, and moves with it; elsewhere it is kept on top. It goes when the editor goes.
class TuningWindow final : public juce::DocumentWindow
{
public:
    TuningWindow (CymbalisProcessor& p, const ui::Assets& assets, juce::Component& owner);
    ~TuningWindow() override;

    // Visible, in front, attached to the editor's window, and clear of `keepClear` (the
    // TUNING button, in screen coordinates) so that the button stays clickable.
    void show (juce::Rectangle<int> keepClear);
    void hide();
    bool isShowing() const { return isVisible(); }
    void closeButtonPressed() override { hide(); }

private:
    void placeClearOf (juce::Rectangle<int> keepClear);

    juce::Component& owner;
    bool placed = false;
};

// Platform part (TuningWindowMac.mm on macOS): attach `window` above the native window
// that holds `owner`, or detach it.
void attachAbove (juce::Component& window, juce::Component& owner);
void detach (juce::Component& window);

} // namespace cymbalis::tuning
