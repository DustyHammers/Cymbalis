#pragma once

// Tuning code only (CYMBALIS_WITH_TUNING, docs/TUNING.md): the TUNING page's panel, shown in
// the editor by its TUNING tab (docs/UI_SPEC.md section 6). Plain JUCE controls in the editor's
// palette and font; every value acts on the current preset, live.

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

// The SIZZLE CHAIN group's header (docs/UI_SPEC.md section 6, "Pages"): drawn like a part
// header (its band, bronze rule and name, the subtitle in the subtitle colour), with CHAIN and an
// OFF | ON pair of boxes at its right, drawn like the stick boxes. The boxes show and set the
// preset's chain switch (session state, not a tuning value).
class ChainHeader final : public juce::Component
{
public:
    explicit ChainHeader (const TuningLookAndFeel& laf);

    void setOn (bool on);
    bool isOn() const { return onBox->getToggleState(); }
    std::function<void (bool)> onSwitch;

    void paint (juce::Graphics& g) override;
    void resized() override;
    static constexpr int height = 56;

private:
    const TuningLookAndFeel& laf;
    std::unique_ptr<juce::Button> offBox, onBox;
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

// The velocity layer the layered rows (attack gain, onset delay, onset speed) show: SOFT
// (velocity 0.3) or HARD (1.0); LINK (an edit sets both layers) and COPY SOFT -> HARD.
class LayerBar final : public juce::Component
{
public:
    LayerBar();

    bool soft() const { return softButton.getToggleState(); }
    bool linked() const { return link.getToggleState(); }
    std::function<void()> onLayer, onCopy;

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    juce::Label note;
    juce::TextButton softButton { "SOFT (0.3)" }, hardButton { "HARD (1.0)" }, copy { "COPY SOFT -> HARD" };
    juce::ToggleButton link { "LINK" };
};

// When each band of the selected articulation's last strike peaked, in ms after the strike,
// under the band columns.
class PeakRow final : public juce::Component
{
public:
    explicit PeakRow (TuningLookAndFeel& small);
    ~PeakRow() override;

    void setPeaks (const std::array<float, kExcitationBands>& ms, bool valid);

    void resized() override;
    static constexpr int height = 44;

private:
    juce::Label title;
    std::array<juce::Label, kExcitationBands> values;
    std::array<float, kExcitationBands> shown {};
    bool shownValid = false, first = true;
};

// The per-band values of the chosen articulation: a selector and one BandRow per BandField,
// the velocity layer bar above the layered rows and the peak readout under them.
class BandEq final : public juce::Component
{
public:
    explicit BandEq (const ui::Assets& assets);

    void setArticulations (const juce::StringArray& names);
    int selected() const { return selector.getSelectedItemIndex(); }
    BandRow& row (size_t field) { return *rows[field]; }
    ValueRow& attackTime() { return *attackTimeRow; } // the attack time, under the attack gains
    LayerBar& layers() { return layerBar; }
    PeakRow& peaks() { return *peakRow; }

    std::function<void()> onSelect;

    void resized() override;
    static int height();

private:
    static constexpr int kSelectorHeight = 40;
    TuningLookAndFeel small; // the band sliders' numbers and labels, in a smaller size
    juce::Label title;
    juce::ComboBox selector;
    std::vector<std::unique_ptr<BandRow>> rows;
    std::unique_ptr<ValueRow> attackTimeRow;
    LayerBar layerBar;
    std::unique_ptr<PeakRow> peakRow;
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
    // Hidden (the editor's CYMBAL page), its timer stops; shown again, it shows the current
    // preset (rebuilt if it changed), its values and the meters at once.
    void visibilityChanged() override;

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
    std::unique_ptr<ToggleRow> restartRow, envelopeRow, highPassRow, hitVariationRow, freeRingRow;
    std::unique_ptr<ChainHeader> chainHeader; // the presets with a sizzle chain only
    std::unique_ptr<BandEq> bandEq;
    TuningLookAndFeel bandLook; // the whole-cymbal band rows' numbers and labels
    std::vector<std::unique_ptr<BandRow>> cymbalBandRows;
    std::unique_ptr<juce::FileChooser> chooser;

    // What the meter strip shows, as drawn: the timer repaints a line only when it changes.
    struct MeterText
    {
        juce::String output, hit, hitLevels;
        bool clip = false;
    };
    MeterText meterText() const;

    CymbalisProcessor::Meters meters;
    MeterText shownMeters;
    // What the meter text is made from, as the timer last made it: the text is made, compared
    // and repainted only when this has changed (in silence, or while the values hold, not at all).
    struct MeterInputs
    {
        float heldPeak = 0.0f, outRms = 0.0f, hitVelocity = 0.0f, hitPeak = 0.0f, hitRms = 0.0f;
        bool clip = false, hitComplete = false;
        int hitRow = -1, hitEntry = -1, preset = -2;
        bool operator== (const MeterInputs& o) const noexcept;
    };
    MeterInputs shownInputs;
    float heldPeak = 0.0f;
    double heldAt = 0.0;
    bool modified = false;
    juce::Rectangle<int> meterFrame, outputLine, clipBox, hitLines; // set in resized()
};

} // namespace cymbalis::tuning
