#include "TuningWindow.h"

#include <cmath>

namespace cymbalis::tuning
{

namespace
{
constexpr int kRowHeight = 28;
constexpr int kHeaderHeight = 30;
constexpr int kMeterHeight = 96;
constexpr int kTopHeight = 44;
constexpr float kTextSize = 15.0f;

juce::String dbfs (float x)
{
    return x > 1e-6f ? juce::String (20.0 * std::log10 ((double) x), 1) + " dBFS" : juce::String ("-inf dBFS");
}

// A section title in the list.
class Header final : public juce::Component
{
public:
    Header (const juce::String& t, const TuningLookAndFeel& l) : text (t), laf (l) {}
    void paint (juce::Graphics& g) override
    {
        g.setColour (ui::palette::divider);
        g.fillRect (0, getHeight() - 1, getWidth(), 1);
        g.setColour (ui::palette::label);
        g.setFont (laf.font (kTextSize));
        g.drawText (text, 8, 0, getWidth() - 16, getHeight(), juce::Justification::bottomLeft);
    }

private:
    juce::String text;
    const TuningLookAndFeel& laf;
};
} // namespace

// =====================================================================================
TuningLookAndFeel::TuningLookAndFeel (const ui::Assets& a) : assets (a)
{
    using namespace ui::palette;
    setColourScheme ({ background, popup, popup, boxOutline, label, bronze, label, bronze, label });
    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::DocumentWindow::textColourId, label);
    setColour (juce::Label::textColourId, label);
    setColour (juce::Slider::backgroundColourId, rowOutline);
    setColour (juce::Slider::trackColourId, bronze);
    setColour (juce::Slider::thumbColourId, openOutline);
    setColour (juce::Slider::textBoxTextColourId, label);
    setColour (juce::Slider::textBoxBackgroundColourId, popup);
    setColour (juce::Slider::textBoxOutlineColourId, rowOutline);
    setColour (juce::TextButton::buttonColourId, popup);
    setColour (juce::TextButton::textColourOffId, label);
    setColour (juce::TextButton::textColourOnId, label);
    setColour (juce::ComboBox::outlineColourId, boxOutline);
    setColour (juce::TextEditor::backgroundColourId, popup);
    setColour (juce::TextEditor::textColourId, label);
    setColour (juce::TextEditor::highlightColourId, bronze);
    setColour (juce::TextEditor::outlineColourId, boxOutline);
    setColour (juce::ToggleButton::textColourId, label);
    setColour (juce::ToggleButton::tickColourId, openOutline);
    setColour (juce::ToggleButton::tickDisabledColourId, secondary);
    setColour (juce::ScrollBar::thumbColourId, boxOutline);
    setColour (juce::AlertWindow::backgroundColourId, popup);
    setColour (juce::AlertWindow::textColourId, label);
    setColour (juce::AlertWindow::outlineColourId, boxOutline);
    if (assets.typeface != nullptr)
        setDefaultSansSerifTypeface (assets.typeface);
}

juce::Font TuningLookAndFeel::font (float size) const { return assets.font (size); }
juce::Font TuningLookAndFeel::getLabelFont (juce::Label&) { return font (kTextSize); }
juce::Font TuningLookAndFeel::getTextButtonFont (juce::TextButton&, int) { return font (kTextSize); }
juce::Font TuningLookAndFeel::getAlertWindowMessageFont() { return font (kTextSize); }
juce::Font TuningLookAndFeel::getAlertWindowTitleFont() { return font (kTextSize + 2.0f); }

// =====================================================================================
namespace
{
// A small reset button: a circular arrow, drawn.
class Reset final : public juce::Button
{
public:
    Reset() : juce::Button ("reset to baked") { setTooltip ("Reset to the baked value"); }
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced (5.0f);
        g.setColour (! isEnabled() ? ui::palette::divider : (down ? ui::palette::openOutline : (over ? ui::palette::bronze : ui::palette::secondary)));
        juce::Path arc;
        arc.addCentredArc (r.getCentreX(), r.getCentreY(), r.getWidth() * 0.4f, r.getHeight() * 0.4f, 0.0f, 0.6f, 5.6f, true);
        g.strokePath (arc, juce::PathStrokeType (1.6f));
        const auto tip = arc.getPointAlongPath (arc.getLength());
        juce::Path head;
        head.addTriangle (tip.translated (-3.5f, -1.0f), tip.translated (3.0f, -2.5f), tip.translated (0.5f, 3.5f));
        g.fillPath (head);
    }
};
} // namespace

ValueRow::ValueRow (const juce::String& n, const juce::String& u, double lo, double hi, int decimals, bool onRelease)
    : name ({}, n), unit (u)
{
    addAndMakeVisible (name);
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 90, kRowHeight - 6);
    slider.setTextBoxIsEditable (true);
    slider.setRange (lo, hi, 0.0);
    slider.setNumDecimalPlacesToDisplay (decimals);
    slider.setTextValueSuffix (unit.isEmpty() ? juce::String() : " " + unit);
    slider.setChangeNotificationOnlyOnRelease (onRelease);
    slider.onValueChange = [this] {
        if (! updating && onChange)
            onChange (slider.getValue());
        repaint();
    };
    addAndMakeVisible (slider);
    reset = std::make_unique<Reset>();
    reset->onClick = [this] {
        if (onChange)
            onChange (baked);
    };
    addAndMakeVisible (*reset);
}

void ValueRow::setValues (double value, double b)
{
    baked = b;
    // A value outside the range (a baked or loaded one) widens it.
    if (value < slider.getMinimum() || value > slider.getMaximum() || b < slider.getMinimum() || b > slider.getMaximum())
        slider.setRange (std::min ({ slider.getMinimum(), value, b }), std::max ({ slider.getMaximum(), value, b }), 0.0);
    const juce::ScopedValueSetter<bool> s (updating, true);
    slider.setValue (value, juce::dontSendNotification);
    reset->setEnabled (! juce::exactlyEqual (value, baked));
    repaint();
}

void ValueRow::resized()
{
    auto r = getLocalBounds().reduced (4, 2);
    reset->setBounds (r.removeFromRight (kRowHeight - 4));
    name.setBounds (r.removeFromLeft (r.getWidth() * 2 / 5));
    slider.setBounds (r);
}

void ValueRow::paint (juce::Graphics& g)
{
    // A changed value is marked with a bronze bar at the left.
    if (! juce::exactlyEqual (slider.getValue(), baked))
    {
        g.setColour (ui::palette::bronze);
        g.fillRect (0, 4, 3, getHeight() - 8);
    }
}

ToggleRow::ToggleRow (const juce::String& n) : toggle (n)
{
    toggle.onClick = [this] {
        if (! updating && onChange)
            onChange (toggle.getToggleState());
        repaint();
    };
    addAndMakeVisible (toggle);
    reset = std::make_unique<Reset>();
    reset->onClick = [this] {
        if (onChange)
            onChange (baked);
    };
    addAndMakeVisible (*reset);
}

void ToggleRow::setValues (bool value, bool b)
{
    baked = b;
    const juce::ScopedValueSetter<bool> s (updating, true);
    toggle.setToggleState (value, juce::dontSendNotification);
    reset->setEnabled (value != baked);
    repaint();
}

void ToggleRow::resized()
{
    auto r = getLocalBounds().reduced (4, 2);
    reset->setBounds (r.removeFromRight (kRowHeight - 4));
    toggle.setBounds (r);
}

void ToggleRow::paint (juce::Graphics& g)
{
    if (toggle.getToggleState() != baked)
    {
        g.setColour (ui::palette::bronze);
        g.fillRect (0, 4, 3, getHeight() - 8);
    }
}

// =====================================================================================
TuningPanel::TuningPanel (CymbalisProcessor& p, const ui::Assets& a) : processor (p), assets (a), lookAndFeel (a)
{
    setLookAndFeel (&lookAndFeel);
    saveButton.onClick = [this] { save(); };
    loadButton.onClick = [this] { load(); };
    resetAllButton.onClick = [this] {
        if (bake != nullptr)
        {
            processor.setTuning (preset, bake->tuning);
            showValues();
        }
    };
    for (auto* b : { &saveButton, &loadButton, &resetAllButton })
        addAndMakeVisible (*b);
    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
    rebuild();
    startTimerHz (30);
}

TuningPanel::~TuningPanel()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void TuningPanel::change (const std::function<void (Tuning&)>& edit)
{
    if (bake == nullptr)
        return;
    auto t = processor.tuning (preset);
    edit (t);
    processor.setTuning (preset, t);
    showValues();
}

void TuningPanel::rebuild()
{
    preset = processor.presetIndex();
    bake = processor.presetBake (preset);
    headers.clear();
    rows.clear();
    refreshers.clear();
    restartRow.reset();
    list.removeAllChildren();
    int y = 0;
    auto addHeader = [&] (const juce::String& text) {
        auto h = std::make_unique<Header> (text, lookAndFeel);
        h->setBounds (0, y, 10, kHeaderHeight);
        y += kHeaderHeight;
        list.addAndMakeVisible (*h);
        headers.push_back (std::move (h));
    };
    auto place = [&] (juce::Component& c) {
        c.setBounds (0, y, 10, kRowHeight);
        y += kRowHeight;
        list.addAndMakeVisible (c);
    };
    if (bake != nullptr)
    {
        // Articulations: one bake entry each.
        for (size_t s = 0; s < bake->slots.size(); ++s)
        {
            const auto& slot = bake->slots[s];
            addHeader ("ARTICULATION  " + juce::String (articulationId (slot.name, slot.overlay)).toUpperCase() + "  ("
                       + juce::String (slot.name) + ")");
            for (const auto& f : articulationFields())
            {
                auto row = std::make_unique<ValueRow> (f.label, "dB", f.lo, f.hi, f.decimals, false);
                auto* r = row.get();
                const auto member = f.member;
                row->onChange = [this, s, member] (double v) { change ([&] (Tuning& t) { t.articulations[s].*member = v; }); };
                refreshers.push_back ([this, r, s, member] {
                    r->setValues (processor.tuning (preset).articulations[s].*member, bake->tuning.articulations[s].*member);
                });
                place (*row);
                rows.push_back (std::move (row));
            }
        }
        // Per cymbal, by group.
        for (auto group : { Group::cymbal, Group::wander, Group::mf, Group::lf, Group::edgeEnvelope })
        {
            bool any = false;
            for (const auto& f : cymbalFields())
                any = any || (f.group == group && applies (f.needs, *bake));
            if (! any)
                continue;
            addHeader (groupName (group));
            for (const auto& f : cymbalFields())
            {
                if (f.group != group || ! applies (f.needs, *bake))
                    continue;
                auto row = std::make_unique<ValueRow> (f.label, f.unit, f.lo, f.hi, f.decimals, f.rebuild);
                auto* r = row.get();
                const auto member = f.member;
                row->onChange = [this, member] (double v) { change ([&] (Tuning& t) { t.cymbal.*member = v; }); };
                refreshers.push_back ([this, r, member] {
                    r->setValues (processor.tuning (preset).cymbal.*member, bake->tuning.cymbal.*member);
                });
                place (*row);
                rows.push_back (std::move (row));
            }
            if (group == Group::edgeEnvelope)
            {
                restartRow = std::make_unique<ToggleRow> ("20 ms frame-decay restart (off: no in-frame decay)");
                restartRow->onChange = [this] (bool v) { change ([&] (Tuning& t) { t.cymbal.frameDecayRestart = v; }); };
                place (*restartRow);
            }
        }
    }
    list.setSize (viewport.getWidth() > 0 ? viewport.getMaximumVisibleWidth() : 600, y + 8);
    resized();
    showValues();
}

void TuningPanel::showValues()
{
    for (auto& r : refreshers)
        r();
    if (restartRow != nullptr && bake != nullptr)
        restartRow->setValues (processor.tuning (preset).cymbal.frameDecayRestart, bake->tuning.cymbal.frameDecayRestart);
    modified = bake != nullptr && isModified (*bake, processor.tuning (preset));
    repaint (0, 0, getWidth(), kTopHeight);
}

void TuningPanel::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (kTopHeight).reduced (8, 7);
    resetAllButton.setBounds (top.removeFromRight (110));
    top.removeFromRight (6);
    loadButton.setBounds (top.removeFromRight (90));
    top.removeFromRight (6);
    saveButton.setBounds (top.removeFromRight (90));
    r.removeFromTop (kMeterHeight);
    viewport.setBounds (r);
    const int w = viewport.getMaximumVisibleWidth();
    list.setSize (w, list.getHeight());
    for (auto* c : list.getChildren())
        c->setBounds (c->getX(), c->getY(), w, c->getHeight());
}

void TuningPanel::paint (juce::Graphics& g)
{
    using namespace ui::palette;
    g.fillAll (background);
    g.setFont (lookAndFeel.font (17.0f));
    g.setColour (label);
    const juce::String name = processor.presetName();
    g.drawText (name, 12, 0, 260, kTopHeight, juce::Justification::centredLeft);
    if (modified)
    {
        g.setColour (bronze);
        g.setFont (lookAndFeel.font (kTextSize));
        g.drawText ("MODIFIED", 12 + 10 + (int) lookAndFeel.font (17.0f).getStringWidthFloat (name), 0, 100, kTopHeight,
                    juce::Justification::centredLeft);
    }

    // Meters.
    auto m = getLocalBounds().withTop (kTopHeight).withHeight (kMeterHeight).reduced (12, 6);
    g.setColour (divider);
    g.drawRect (m.expanded (4, 2), 1);
    g.setFont (lookAndFeel.font (kTextSize));
    auto line1 = m.removeFromTop (m.getHeight() / 3);
    g.setColour (label);
    g.drawText ("OUTPUT (after Volume)   peak " + dbfs (heldPeak) + "   RMS " + dbfs (meters.outRms), line1.withTrimmedRight (70),
                juce::Justification::centredLeft);
    clipBox = line1.removeFromRight (60).reduced (2, 3);
    if (meters.clip)
    {
        g.setColour (bronze);
        g.fillRect (clipBox);
    }
    g.setColour (meters.clip ? label : boxOutline);
    g.drawRect (clipBox, 1);
    g.drawText ("CLIP", clipBox, juce::Justification::centred);

    g.setColour (label);
    juce::String hit = "LAST HIT (before Volume)   ";
    if (meters.hitRow >= 0 && bake != nullptr && meters.hitEntry >= 0 && meters.hitEntry < (int) bake->slots.size())
    {
        const auto& slot = bake->slots[(size_t) meters.hitEntry];
        hit << kSlots[(size_t) meters.hitRow].label << " - " << juce::String (articulationId (slot.name, slot.overlay))
            << "   velocity " << juce::String (meters.hitVelocity, 3);
        g.drawText (hit, m.removeFromTop (m.getHeight() / 2), juce::Justification::centredLeft);
        g.drawText ("peak " + juce::String (meters.hitPeak, 3) + " (" + dbfs (meters.hitPeak) + ")   RMS of the first 500 ms "
                        + dbfs (meters.hitRms) + (meters.hitComplete ? "" : "   (measuring)"),
                    m, juce::Justification::centredLeft);
    }
    else
        g.drawText (hit + "-", m.removeFromTop (m.getHeight() / 2), juce::Justification::centredLeft);
}

void TuningPanel::mouseDown (const juce::MouseEvent& e)
{
    if (clipBox.contains (e.getPosition()))
    {
        processor.resetClip();
        heldPeak = 0.0f;
    }
}

void TuningPanel::timerCallback()
{
    if (processor.presetIndex() != preset)
        rebuild();
    meters = processor.readMeters();
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (meters.outPeak >= heldPeak || now - heldAt > 1500.0)
    {
        heldPeak = meters.outPeak;
        heldAt = now;
    }
    repaint (0, kTopHeight, getWidth(), kMeterHeight);
}

void TuningPanel::save()
{
    if (bake == nullptr)
        return;
    const juce::String id = kPresets[(size_t) preset].id;
    chooser = std::make_unique<juce::FileChooser> (
        "Save the " + processor.presetName() + " tuning",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (id + ".tuning.json"), "*.json");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, id] (const juce::FileChooser& fc) {
                              const auto file = fc.getResult();
                              if (file == juce::File() || bake == nullptr)
                                  return;
                              const auto text = toJson (*bake, id, processor.tuning (preset), JucePlugin_Name);
                              if (! file.replaceWithText (text))
                                  juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Tuning not saved",
                                                                          "Could not write " + file.getFullPathName() + ".");
                          });
}

void TuningPanel::load()
{
    if (bake == nullptr)
        return;
    chooser = std::make_unique<juce::FileChooser> ("Load a tuning for " + processor.presetName(),
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.json");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc) {
                              const auto file = fc.getResult();
                              if (file == juce::File() || bake == nullptr)
                                  return;
                              Tuning t;
                              const auto error = fromJson (file.loadFileAsString(), *bake, kPresets[(size_t) preset].id, t);
                              if (error.isNotEmpty())
                              {
                                  juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                                          "Tuning file refused", error);
                                  return;
                              }
                              processor.setTuning (preset, t);
                              showValues();
                          });
}

// =====================================================================================
TuningWindow::TuningWindow (CymbalisProcessor& p, const ui::Assets& assets, juce::Component& ownerComponent)
    : juce::DocumentWindow (juce::String (JucePlugin_Name) + " - Tuning", ui::palette::background,
                            juce::DocumentWindow::closeButton),
      owner (ownerComponent)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new TuningPanel (p, assets), false);
    setResizable (true, false);
    setResizeLimits (520, 360, 1200, 2000);
    centreWithSize (660, 820);
}

TuningWindow::~TuningWindow()
{
    detach (*this);
}

void TuningWindow::show()
{
    setVisible (true);
    attachAbove (*this, owner);
    toFront (true);
}

void TuningWindow::hide()
{
    detach (*this);
    setVisible (false);
}

#if ! JUCE_MAC
// Without a native child window: kept on top.
void attachAbove (juce::Component& window, juce::Component&)
{
    window.setAlwaysOnTop (true);
}

void detach (juce::Component&) {}
#endif

} // namespace cymbalis::tuning
