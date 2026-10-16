#pragma once

// Tuning build only (docs/TUNING.md): the Tuning window, opened by Cmd-click on the CYMBALIS
// logo. Plain JUCE controls in the editor's palette and font; every value acts on the
// current preset, live.

#include "PluginProcessor.h"
#include "TuningFields.h"
#include "ui/UiAssets.h"

#include <functional>
#include <memory>
#include <vector>

namespace cymbalis::tuning
{

class TuningLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    explicit TuningLookAndFeel (const ui::Assets& assets);
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowTitleFont() override;
    juce::Font font (float size) const;

private:
    const ui::Assets& assets;
};

// One value: name, slider with numeric entry, unit, and a reset-to-baked button.
class ValueRow final : public juce::Component
{
public:
    ValueRow (const juce::String& name, const juce::String& unit, double lo, double hi, int decimals, bool onRelease);

    void setValues (double value, double baked);
    std::function<void (double)> onChange;

    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    juce::Label name;
    juce::Slider slider;
    std::unique_ptr<juce::Button> reset;
    juce::String unit;
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
    std::vector<std::function<void()>> refreshers; // one per row: shows its current value
    std::unique_ptr<ToggleRow> restartRow;
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

    void show(); // visible, in front, attached to the editor's window
    void hide();
    bool isShowing() const { return isVisible(); }
    void closeButtonPressed() override { hide(); }

private:
    juce::Component& owner;
};

// Platform part (TuningWindowMac.mm on macOS): attach `window` above the native window
// that holds `owner`, or detach it.
void attachAbove (juce::Component& window, juce::Component& owner);
void detach (juce::Component& window);

} // namespace cymbalis::tuning
