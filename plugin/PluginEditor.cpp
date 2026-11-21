#include "PluginEditor.h"

#if CYMBALIS_TUNING
#include "tuning/TuningWindow.h"
#endif

#include <cmath>

namespace cymbalis::ui
{

namespace
{
// Font sizes and letter spacing for values in boxes and popups. UI_SPEC section 4 gives the
// labels; values use the same font (section 4, last line).
constexpr float kHeaderSize = 17.1f;
constexpr float kRowSize = 15.6f;
constexpr float kBoxValueSpacing = 3.2f;   // CYMBAL and MIDI CHANNEL boxes, TUNING
constexpr float kBoxValueLeft = 12.0f;     // a box value's ink inset when it does not sit centred
constexpr float kStickValueSpacing = 2.0f; // stick boxes
constexpr float kNoteValueSpacing = 1.0f;  // note boxes
constexpr float kPopupSpacing = 2.5f;

// Row box tops (BELL, BOW, EDGE, BELL2, BOW2), UI_SPEC section 3.
constexpr std::array<int, kNumSlots> kRowTop { 140, 220, 300, 380, 460 };
constexpr std::array<float, kNumSlots> kRowLabelY { 148.0f, 228.0f, 307.0f, 387.0f, 468.0f };
constexpr std::array<float, kNumSlots> kRowLabelSpacing { 3.33f, 2.88f, 2.83f, 3.50f, 3.25f };
const juce::Rectangle<int> kStickBox (1265, 0, 99, 25);
const juce::Rectangle<int> kNoteBox (1424, 0, 66, 25);
const juce::Rectangle<int> kPresetBox (61, 371, 277, 62);
const juce::Rectangle<int> kChannelBox (1404, 30, 110, 36); // top right, above the column headers (y 98)
constexpr float kChannelLabelRight = 1384.0f; // MIDI CHANNEL ends here, on the box's line, to its left
constexpr float kKnobLabelTop = 784.0f;       // Velocity Sensitivity, Volume and Pan, centred on their knobs
// The DHammers logo (UI_SPEC section 4): Pinyon Script at em size 48, its ink centred under
// the CYMBAL and TUNING boxes, level with the bottom row's -/+ and L/R marks.
constexpr float kLogoSize = 48.0f;
const juce::Point<float> kLogoCentre (199.5f, 950.0f);
#if CYMBALIS_TUNING
const juce::Rectangle<int> kLogo (40, 80, 390, 130); // the CYMBALIS logo and its tagline in the background
const juce::Rectangle<int> kTuningButton (61, 501, 277, 62); // under the CYMBAL box, its size
// The Tuning window opens below this area, right-aligned with it, and keeps it clear: the
// TUNING button's place before layout v3, where the MIDI CHANNEL box is now.
const juce::Rectangle<int> kTuningWindowAnchor (1404, 30, 110, 36);
#endif

const juce::String emDash = juce::String::fromUTF8 ("\xe2\x80\x94");

// Baseline that centres capital letters vertically in `area`.
float capBaseline (const juce::Font& f, juce::Rectangle<float> area)
{
    const auto h = inkBounds (f, "H");
    return area.getCentreY() + h.getHeight() * 0.5f;
}

// `text` centred in `area`: horizontally by its ink, vertically by the capital height.
void drawValue (juce::Graphics& g, const juce::Font& f, const juce::String& text, juce::Rectangle<float> area,
                juce::Colour c)
{
    const auto b = inkBounds (f, text);
    drawInkText (g, f, text, { area.getCentreX() - b.getWidth() * 0.5f, capBaseline (f, area) + b.getY() }, c);
}

// `text` at the left of `area` (its ink from area's left edge), vertically by the capital height.
void drawValueLeft (juce::Graphics& g, const juce::Font& f, const juce::String& text, juce::Rectangle<float> area,
                    juce::Colour c)
{
    drawInkText (g, f, text, { area.getX(), capBaseline (f, area) + inkBounds (f, text).getY() }, c);
}

void drawTriangle (juce::Graphics& g, juce::Point<float> topLeft)
{
    juce::Path p;
    p.addTriangle (topLeft, topLeft.translated (13.0f, 0.0f), topLeft.translated (6.5f, 9.0f));
    g.setColour (palette::arrow);
    g.fillPath (p);
}

void drawBoxOutline (juce::Graphics& g, juce::Rectangle<int> r, juce::Colour c)
{
    g.setColour (c);
    g.drawRect (r, 2);
}
} // namespace

#if CYMBALIS_TUNING
// =====================================================================================
// Tuning code (CYMBALIS_WITH_TUNING): the TUNING button, drawn like the CYMBAL box without
// its arrow.
class Content::TuningButton final : public juce::Component
{
public:
    explicit TuningButton (const Assets& a) : assets (a) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void setOpen (bool o)
    {
        if (o != open)
        {
            open = o;
            repaint();
        }
    }
    std::function<void()> onClick;
    void paint (juce::Graphics& g) override
    {
        drawBoxOutline (g, getLocalBounds(), open ? palette::openOutline : (isMouseOver() ? palette::bronze : palette::boxOutline));
        drawValue (g, assets.font (kHeaderSize, kBoxValueSpacing), "TUNING", getLocalBounds().toFloat(), palette::label);
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (onClick && getLocalBounds().contains (e.getPosition()))
            onClick();
    }

private:
    const Assets& assets;
    bool open = false;
};
#endif

// =====================================================================================
namespace
{
// The position arc (UI_SPEC section 6): radius to the middle of the line, line width, and
// the sweep, the knob's own range, in degrees each side of 12 o'clock.
constexpr float kArcRadius = 63.0f;
constexpr float kArcWidth = 2.5f;
constexpr float kArcSweep = 150.0f;

juce::Point<float> onCircle (juce::Point<float> c, float radius, float degrees)
{
    const float a = juce::degreesToRadians (degrees); // clockwise from 12 o'clock
    return { c.x + radius * std::sin (a), c.y - radius * std::cos (a) };
}

// The arc from `from` to `to` (degrees clockwise from 12 o'clock, in either order), with butt
// ends, its colour changing along it: `colourAt` (an angle) for each step of at most 1
// degree. The line is the arc's outline, as a clip, filled step by step; each step reaches
// half a step into the next, which is drawn over it, and the first and last reach past the
// ends, so the clip alone draws the line's edges and ends: no seams between steps.
template <typename ColourAt>
void paintArc (juce::Graphics& g, juce::Point<float> c, float from, float to, ColourAt colourAt)
{
    const float lo = std::min (from, to), hi = std::max (from, to);
    if (hi - lo < 1.0e-3f)
        return;
    juce::Path arc, line;
    arc.addCentredArc (c.x, c.y, kArcRadius, kArcRadius, 0.0f, juce::degreesToRadians (lo), juce::degreesToRadians (hi), true);
    juce::PathStrokeType (kArcWidth, juce::PathStrokeType::curved, juce::PathStrokeType::butt).createStrokedPath (line, arc);
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (line);
    const int steps = std::max (1, (int) std::ceil (hi - lo));
    const float step = (hi - lo) / (float) steps;
    const float reach = kArcRadius + kArcWidth; // past the line's outer edge
    for (int i = 0; i < steps; ++i)
    {
        const float a0 = i == 0 ? lo - 1.0f : lo + step * (float) i;
        const float a1 = i == steps - 1 ? hi + 1.0f : lo + step * ((float) i + 1.5f);
        juce::Path wedge;
        wedge.startNewSubPath (c);
        wedge.lineTo (onCircle (c, reach, a0));
        wedge.lineTo (onCircle (c, reach, a1));
        wedge.closeSubPath();
        g.setColour (colourAt (lo + step * ((float) i + 0.5f)));
        g.fillPath (wedge);
    }
}
} // namespace

Knob::Knob (const Assets& a, bool isBipolar) : assets (a), bipolar (isBipolar)
{
    setSliderStyle (juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setMouseDragSensitivity (300);
    setScrollWheelEnabled (false); // only a drag (or double-click, or the host) changes a value
}

bool Knob::hitTest (int x, int y)
{
    return getLocalBounds().reduced (margin).contains (x, y); // the filmstrip's square, as before the arc
}

void Knob::paint (juce::Graphics& g)
{
    // p: the knob's position 0-1, which picks the filmstrip frame and lights the arc.
    const float p = (float) juce::jlimit (0.0, 1.0, valueToProportionOfLength (getValue()));
    if (assets.knobStrip.isValid())
    {
        const int frame = juce::jlimit (0, Assets::knobFrames - 1, juce::roundToInt (p * (float) (Assets::knobFrames - 1)));
        const int s = Assets::knobFrameSize;
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (assets.knobStrip, margin, margin, s, s, 0, frame * s, s, s);
    }

    // The arc: the track over the whole sweep, then the lit part. Unipolar: from 7 o'clock
    // by p, dark bronze at 7 o'clock to bright gold at 5 o'clock. Bipolar: from 12 o'clock to
    // the knob's side, dark bronze at the centre to bright gold at either end.
    const auto c = getLocalBounds().toFloat().getCentre();
    paintArc (g, c, -kArcSweep, kArcSweep, [] (float) { return palette::arcTrack; });
    const auto gold = [] (float t) { return palette::arcDark.interpolatedWith (palette::arcBright, juce::jlimit (0.0f, 1.0f, t)); };
    if (bipolar)
        paintArc (g, c, 0.0f, 2.0f * kArcSweep * (p - 0.5f), [&gold] (float a) { return gold (std::abs (a) / kArcSweep); });
    else
        paintArc (g, c, -kArcSweep, -kArcSweep + 2.0f * kArcSweep * p,
                  [&gold] (float a) { return gold ((a + kArcSweep) / (2.0f * kArcSweep)); });
}

// =====================================================================================
DropdownBox::DropdownBox (const Assets& a, juce::Point<float> arrowTopLeft, Align valueAlign)
    : assets (a), arrow (arrowTopLeft), align (valueAlign)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void DropdownBox::setText (const juce::String& t)
{
    text = t;
    repaint();
}

void DropdownBox::setOpen (bool shouldBeOpen)
{
    open = shouldBeOpen;
    repaint();
}

void DropdownBox::paint (juce::Graphics& g)
{
    const auto outline = open ? palette::openOutline : (isMouseOver() ? palette::bronze : palette::boxOutline);
    drawBoxOutline (g, getLocalBounds(), outline);
    drawTriangle (g, arrow);
    auto area = getLocalBounds().toFloat();
    auto font = assets.font (kHeaderSize, kBoxValueSpacing);
    if (align == Align::left)
    {
        drawValueLeft (g, font, text, area.withTrimmedLeft (kBoxValueLeft), palette::label);
        return;
    }
    // Centred in the box. A name too long for that is centred between the left edge and
    // the arrow instead, with tighter letter spacing until it fits.
    const float right = arrow.x - 10.0f;
    if (area.getCentreX() + inkBounds (font, text).getWidth() * 0.5f > right)
    {
        area = area.withLeft (kBoxValueLeft).withRight (right);
        for (float spacing = kBoxValueSpacing - 0.25f; spacing >= 0.5f && inkBounds (font, text).getWidth() > area.getWidth();
             spacing -= 0.25f)
            font = assets.font (kHeaderSize, spacing);
    }
    drawValue (g, font, text, area, palette::label);
}

void DropdownBox::mouseDown (const juce::MouseEvent&)
{
    if (onClick)
        onClick();
}

// =====================================================================================
NoteBox::NoteBox (const Assets& a) : assets (a)
{
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

void NoteBox::setNote (int n)
{
    note = n;
    repaint();
}

void NoteBox::setLearning (bool l, float p)
{
    if (l != learning || (l && std::abs (p - pulse) > 0.01f))
    {
        learning = l;
        pulse = p;
        repaint();
    }
}

void NoteBox::setOpen (bool shouldBeOpen)
{
    open = shouldBeOpen;
    repaint();
}

void NoteBox::setLit (bool l)
{
    if (l != lit)
    {
        lit = l;
        repaint();
    }
}

void NoteBox::paint (juce::Graphics& g)
{
    const auto outline = open ? palette::openOutline : (isMouseOver() || lit ? palette::bronze : palette::rowOutline);
    drawBoxOutline (g, getLocalBounds(), outline);
    const auto area = getLocalBounds().toFloat();
    const auto f = assets.font (kRowSize, kNoteValueSpacing);
    if (learning)
    {
        drawValue (g, f, "LEARN", area, palette::label.withMultipliedAlpha (pulse));
        return;
    }
    if (note < 0)
    {
        drawValue (g, f, emDash, area, palette::secondary);
        return;
    }
    // Name in the value colour, number in the secondary colour, on one baseline.
    const juce::String name = noteName (note), number (note);
    const auto bn = inkBounds (f, name), bm = inkBounds (f, number);
    const float gap = 6.0f;
    const float x0 = area.getCentreX() - (bn.getWidth() + gap + bm.getWidth()) * 0.5f;
    const float base = capBaseline (f, area);
    drawInkText (g, f, name, { x0, base + bn.getY() }, palette::label);
    drawInkText (g, f, number, { x0 + bn.getWidth() + gap, base + bm.getY() }, palette::secondary);
}

void NoteBox::mouseDown (const juce::MouseEvent&)
{
    dragStartNote = note;
    dragged = false;
}

// Vertical drag: one semitone per 8 design units, up is higher. Below 0 is unassigned.
void NoteBox::mouseDrag (const juce::MouseEvent& e)
{
    const int dy = e.getDistanceFromDragStartY();
    if (! dragged && std::abs (dy) < 4)
        return;
    dragged = true;
    const int start = dragStartNote < 0 ? 60 : dragStartNote;
    const int n = juce::jlimit (-1, 127, start - dy / 8);
    if (n != note && onNoteDragged)
        onNoteDragged (n);
}

void NoteBox::mouseUp (const juce::MouseEvent&)
{
    if (! dragged && onClick)
        onClick();
}

// =====================================================================================
StickBox::StickBox (const Assets& a) : assets (a) {}

void StickBox::setStick (Stick s, bool canChoose)
{
    stick = s;
    selectable = canChoose;
    setMouseCursor (selectable ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

void StickBox::setOpen (bool shouldBeOpen)
{
    open = shouldBeOpen;
    repaint();
}

void StickBox::setLit (bool l)
{
    if (l != lit)
    {
        lit = l;
        repaint();
    }
}

void StickBox::paint (juce::Graphics& g)
{
    auto outline = lit ? palette::bronze : palette::rowOutline;
    if (selectable)
        outline = open ? palette::openOutline : (isMouseOver() || lit ? palette::bronze : palette::rowOutline);
    drawBoxOutline (g, getLocalBounds(), outline);
    const auto f = assets.font (kRowSize, kStickValueSpacing);
    if (stick == Stick::none)
        drawValue (g, f, emDash, getLocalBounds().toFloat(), palette::secondary);
    else
        drawValue (g, f, stickName (stick), getLocalBounds().toFloat(), palette::label);
}

void StickBox::mouseUp (const juce::MouseEvent& e)
{
    if (selectable && onClick && e.mouseWasClicked())
        onClick();
}

// =====================================================================================
// The scrolling part of the note menu: one row per MIDI note.
class NoteMenu::List final : public juce::Component
{
public:
    List (const Assets& a, int current) : assets (a), currentNote (current)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    std::function<void (int)> onNote;

    void paint (juce::Graphics& g) override
    {
        const auto clip = g.getClipBounds();
        const int first = juce::jmax (0, clip.getY() / rowHeight);
        const int last = juce::jmin (127, clip.getBottom() / rowHeight);
        const auto f = assets.font (kRowSize, kNoteValueSpacing);
        for (int n = first; n <= last; ++n)
        {
            const auto r = rowBounds (n);
            if (n == currentNote)
            {
                g.setColour (palette::openOutline);
                g.drawRect (r.reduced (2, 1), 1);
            }
            else if (n == hover)
            {
                g.setColour (palette::bronze);
                g.drawRect (r.reduced (2, 1), 1);
            }
            // Name in the value colour, number in the secondary colour, as in the note box.
            const juce::String name = noteName (n), number (n);
            const auto area = r.toFloat();
            const float base = capBaseline (f, area);
            const auto bn = inkBounds (f, name), bm = inkBounds (f, number);
            drawInkText (g, f, name, { area.getX() + 14.0f, base + bn.getY() }, palette::label);
            drawInkText (g, f, number, { area.getRight() - 14.0f - bm.getWidth(), base + bm.getY() }, palette::secondary);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override { setHover (e.y / rowHeight); }
    void mouseExit (const juce::MouseEvent&) override { setHover (-1); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        const int n = e.y / rowHeight;
        if (e.mouseWasClicked() && n >= 0 && n <= 127 && onNote)
            onNote (n);
    }

    juce::Rectangle<int> rowBounds (int n) const { return { 0, n * rowHeight, getWidth(), rowHeight }; }

private:
    void setHover (int h)
    {
        if (h == hover)
            return;
        if (hover >= 0) repaint (rowBounds (hover));
        hover = h;
        if (hover >= 0) repaint (rowBounds (hover));
    }

    const Assets& assets;
    int currentNote;
    int hover = -1;
};

namespace
{
const char* const kNoteActions[] = { "MIDI LEARN", "RESET DEFAULT", "NONE" };
}

NoteMenu::NoteMenu (const Assets& a, int currentNote) : assets (a)
{
    list = std::make_unique<List> (assets, currentNote);
    list->onNote = [this] (int n) {
        if (onNote)
            onNote (n);
    };
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    viewport.setSingleStepSizes (rowHeight, rowHeight);
    viewport.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, palette::bronze);
    viewport.getVerticalScrollBar().setColour (juce::ScrollBar::trackColourId, palette::popup);
    viewport.setViewedComponent (list.get(), false);
    addAndMakeVisible (viewport);
    setSize (width, height());
    const int first = juce::jlimit (0, 128 - visibleRows, (currentNote < 0 ? 60 : currentNote) - rowsAboveCurrent);
    viewport.setViewPosition (0, first * rowHeight);
}

juce::Rectangle<int> NoteMenu::actionBounds (int i) const
{
    return { inset, inset + i * actionHeight, width - 2 * inset, actionHeight };
}

int NoteMenu::actionAt (juce::Point<int> p) const
{
    for (int i = 0; i < 3; ++i)
        if (actionBounds (i).contains (p))
            return i;
    return -1;
}

void NoteMenu::resized()
{
    const int top = inset + 3 * actionHeight + gap;
    viewport.setBounds (inset, top, width - 2 * inset, visibleRows * rowHeight);
    list->setSize (viewport.getMaximumVisibleWidth(), 128 * rowHeight);
}

void NoteMenu::paint (juce::Graphics& g)
{
    g.fillAll (palette::popup);
    drawBoxOutline (g, getLocalBounds(), palette::boxOutline);
    const auto f = assets.font (kRowSize, kPopupSpacing);
    for (int i = 0; i < 3; ++i)
    {
        const auto r = actionBounds (i);
        if (i == hover)
        {
            g.setColour (palette::bronze);
            g.drawRect (r.reduced (2), 1);
        }
        drawValue (g, f, kNoteActions[i], r.toFloat(), palette::label);
    }
    g.setColour (palette::divider);
    g.fillRect (inset, inset + 3 * actionHeight + gap / 2, width - 2 * inset, 1);
}

void NoteMenu::mouseMove (const juce::MouseEvent& e)
{
    const int h = actionAt (e.getPosition());
    if (h != hover)
    {
        hover = h;
        repaint();
    }
}

void NoteMenu::mouseExit (const juce::MouseEvent&)
{
    hover = -1;
    repaint();
}

void NoteMenu::mouseUp (const juce::MouseEvent& e)
{
    const int i = actionAt (e.getPosition());
    if (i >= 0 && onAction)
        onAction (i);
}

// =====================================================================================
PopupPanel::PopupPanel (const Assets& a, juce::StringArray i, int sel, int cols, juce::Point<int> size)
    : assets (a), items (std::move (i)), selected (sel), columns (juce::jmax (1, cols)), itemSize (size)
{
    const int rows = (items.size() + columns - 1) / columns;
    setSize (columns * itemSize.x + 2 * (border + padding), rows * itemSize.y + 2 * (border + padding));
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

juce::Rectangle<int> PopupPanel::itemBounds (int index) const
{
    const int inset = border + padding;
    return { inset + (index % columns) * itemSize.x, inset + (index / columns) * itemSize.y, itemSize.x, itemSize.y };
}

int PopupPanel::itemAt (juce::Point<int> p) const
{
    for (int i = 0; i < items.size(); ++i)
        if (itemBounds (i).contains (p))
            return i;
    return -1;
}

void PopupPanel::paint (juce::Graphics& g)
{
    g.fillAll (palette::popup);
    drawBoxOutline (g, getLocalBounds(), palette::boxOutline);
    const auto f = assets.font (kRowSize, kPopupSpacing);
    for (int i = 0; i < items.size(); ++i)
    {
        const auto r = itemBounds (i);
        if (i == hover)
        {
            g.setColour (palette::bronze);
            g.drawRect (r.reduced (2), 1);
        }
        const bool bright = selected < 0 || i == selected || i == hover;
        drawValue (g, f, items[i], r.toFloat(), bright ? palette::label : palette::secondary);
    }
}

void PopupPanel::mouseMove (const juce::MouseEvent& e)
{
    const int h = itemAt (e.getPosition());
    if (h != hover)
    {
        hover = h;
        repaint();
    }
}

void PopupPanel::mouseExit (const juce::MouseEvent&)
{
    hover = -1;
    repaint();
}

void PopupPanel::mouseUp (const juce::MouseEvent& e)
{
    const int i = itemAt (e.getPosition());
    if (i >= 0 && onChoose)
        onChoose (i);
}

// =====================================================================================
namespace
{
// Catches clicks outside the popup and closes it.
class PopupLayer final : public juce::Component
{
public:
    explicit PopupLayer (std::function<void()> close) : onClose (std::move (close)) { setInterceptsMouseClicks (true, true); }
    void mouseDown (const juce::MouseEvent&) override
    {
        if (onClose)
            onClose();
    }

private:
    std::function<void()> onClose;
};
} // namespace

Content::Content (CymbalisProcessor& p)
    : processor (p),
      knobSpecs { {
          // The bottom row: Velocity Sensitivity, Volume and Pan, 300 apart under the cymbal,
          // Volume under its centre (x 787). Their labels are centred in the constructor.
          { CymbalisProcessor::paramVelocitySensitivity, { 487.0f, 881.0f }, "VELOCITY SENSITIVITY", { 0.0f, kKnobLabelTop }, 3.09f, palette::knobLabel },
          { CymbalisProcessor::paramVolumeDb, { 787.0f, 881.0f }, "VOLUME", { 0.0f, kKnobLabelTop }, 3.40f, palette::knobLabel },
          { CymbalisProcessor::paramBlend1, { 1349.0f, 642.0f }, "BLEND POSITION 1", { 1269.0f, 545.0f }, 3.18f, palette::label },
          { CymbalisProcessor::paramBlend2, { 1350.0f, 881.0f }, "BLEND POSITION 2", { 1270.0f, 783.0f }, 2.98f, palette::label },
          { CymbalisProcessor::paramPan, { 1087.0f, 881.0f }, "PAN", { 0.0f, kKnobLabelTop }, 3.40f, palette::knobLabel },
      } },
      presetBox (assets, { 306.0f - (float) kPresetBox.getX(), 398.0f - (float) kPresetBox.getY() }),
      channelBox (assets, { 1484.0f - (float) kChannelBox.getX(), 44.0f - (float) kChannelBox.getY() }, DropdownBox::Align::left)
{
    setSize (kDesignWidth, kDesignHeight);
    setOpaque (true);

    // The wireframe cymbal, behind every control, and the hits it moves with. Hits from before
    // the editor opened are not shown.
    processor.clearHits();
    cymbalView = std::make_unique<CymbalView> (assets);
    cymbalView->setBounds (CymbalView::area());
    addAndMakeVisible (*cymbalView);
    hitTracker = std::make_unique<HitTracker> (processor, *cymbalView);
    hitTracker->onRowLitChanged = [this] (int s) {
        const bool on = hitTracker->rowLit (s);
        noteBoxes[(size_t) s]->setLit (on);
        stickBoxes[(size_t) s]->setLit (on);
        repaint (rowLabelArea (s));
    };

    // The DHammers logo's outline, its ink centred on kLogoCentre.
    if (assets.logoTypeface != nullptr)
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (assets.logoFont (kLogoSize), "DHammers", 0.0f, 0.0f);
        ga.createPath (logoPath);
        const auto ink = logoPath.getBounds();
        logoPath.applyTransform (juce::AffineTransform::translation (kLogoCentre.x - ink.getCentreX(), kLogoCentre.y - ink.getCentreY()));
    }

    // Velocity Sensitivity, Volume and Pan: each label centred on its knob.
    for (size_t i : { size_t (0), size_t (1), size_t (4) })
    {
        auto& spec = knobSpecs[i];
        spec.labelInk.x = spec.centre.x - inkBounds (assets.font (kHeaderSize, spec.spacing), spec.label).getWidth() * 0.5f;
    }

    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& spec = knobSpecs[i];
        knobs[i] = std::make_unique<Knob> (assets, juce::String (spec.paramId) == CymbalisProcessor::paramPan);
        // The filmstrip's 128 x 128 at the knob's centre, and the arc's margin around it.
        const int s = Knob::size;
        knobs[i]->setBounds (juce::roundToInt (spec.centre.x) - s / 2, juce::roundToInt (spec.centre.y) - s / 2, s, s);
        attachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processor.parameters(), spec.paramId, *knobs[i]);
        if (auto* param = processor.parameters().getParameter (spec.paramId))
            knobs[i]->setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
        knobs[i]->addListener (this);
        addAndMakeVisible (*knobs[i]);
    }

    presetBox.setBounds (kPresetBox);
    presetBox.setText (processor.presetName());
    presetBox.onClick = [this] { openPresetMenu(); };
    addAndMakeVisible (presetBox);

    channelBox.setBounds (kChannelBox);
    channelBox.onClick = [this] { openChannelMenu(); };
    addAndMakeVisible (channelBox);

#if CYMBALIS_TUNING
    tuningButton = std::make_unique<TuningButton> (assets);
    tuningButton->setBounds (kTuningButton);
    tuningButton->onClick = [this] { toggleTuningWindow (tuningWindow == nullptr || ! tuningWindow->isShowing()); };
    addAndMakeVisible (*tuningButton);
#endif

    for (int s = 0; s < kNumSlots; ++s)
    {
        auto box = std::make_unique<NoteBox> (assets);
        box->setBounds (kNoteBox.withY (kRowTop[(size_t) s]));
        box->onNoteDragged = [this, s] (int n) {
            processor.setSlotNote (s, n);
            refresh();
        };
        box->onClick = [this, s] { openNoteMenu (s); };
        addAndMakeVisible (*box);
        noteBoxes[(size_t) s] = std::move (box);
    }
    shownNotes.fill (-2);

    for (int s = 0; s < kNumSlots; ++s)
    {
        auto box = std::make_unique<StickBox> (assets);
        box->setBounds (kStickBox.withY (kRowTop[(size_t) s]));
        box->onClick = [this, s] { openStickMenu (s); };
        addAndMakeVisible (*box);
        stickBoxes[(size_t) s] = std::move (box);
    }
    shownSticks.fill (-2);

    addMouseListener (this, true); // a click anywhere cancels MIDI learn
    refresh();
}

Content::~Content()
{
    hitTracker.reset(); // before the cymbal it feeds
#if CYMBALIS_TUNING
    tuningWindow.reset(); // the Tuning window goes with the editor
#endif
    removeMouseListener (this);
    for (auto& k : knobs)
        k->removeListener (this);
}

void Content::refresh()
{
    for (int s = 0; s < kNumSlots; ++s)
    {
        const int n = processor.slotNote (s);
        if (n != shownNotes[(size_t) s])
        {
            shownNotes[(size_t) s] = n;
            noteBoxes[(size_t) s]->setNote (n);
        }
    }
    const int preset = processor.presetIndex();
    if (preset != shownPreset || processor.presetError() != shownError)
    {
        shownPreset = preset;
        shownError = processor.presetError();
        presetBox.setText (processor.presetName());
        cymbalView->setPreset (preset, shownError);
        hitTracker->setGeometry (cymbalView->geometry());
        // Only the rows the preset has (the hi-hat has two): the others' boxes are hidden.
        for (int s = 0; s < kNumSlots; ++s)
        {
            noteBoxes[(size_t) s]->setVisible (rowPlays (preset, s));
            stickBoxes[(size_t) s]->setVisible (rowPlays (preset, s));
        }
        repaint();
    }
    hitTracker->poll();
    for (int s = 0; s < kNumSlots; ++s)
    {
        const auto stick = processor.shownStick (s);
        const bool selectable = processor.stickSelectable (s);
        const int key = (int) stick + 4 * (selectable ? 1 : 0);
        if (key != shownSticks[(size_t) s])
        {
            shownSticks[(size_t) s] = key;
            stickBoxes[(size_t) s]->setStick (stick, selectable);
        }
    }
    const int ch = processor.midiChannel();
    if (ch != shownChannel)
    {
        shownChannel = ch;
        channelBox.setText (juce::String (ch));
    }
    const int learn = processor.midiLearnSlot();
    if (learn != shownLearn)
    {
        shownLearn = learn;
        learnStartMs = juce::Time::getMillisecondCounterHiRes();
    }
    const double t = (juce::Time::getMillisecondCounterHiRes() - learnStartMs) * 0.001;
    const float pulse = 0.6f + 0.4f * (float) std::cos (juce::MathConstants<double>::twoPi * 1.25 * t);
    for (int s = 0; s < kNumSlots; ++s)
        noteBoxes[(size_t) s]->setLearning (s == learn, pulse);
#if CYMBALIS_TUNING
    tuningButton->setOpen (tuningWindow != nullptr && tuningWindow->isShowing());
#endif
}

#if CYMBALIS_TUNING
void Content::toggleTuningWindow (bool open)
{
    if (open)
    {
        if (tuningWindow == nullptr)
            tuningWindow = std::make_unique<tuning::TuningWindow> (processor, assets, *this);
        tuningWindow->show (localAreaToGlobal (kTuningWindowAnchor));
    }
    else if (tuningWindow != nullptr)
        tuningWindow->hide();
    tuningButton->setOpen (open);
}
#endif

void Content::mouseDown (const juce::MouseEvent& e)
{
#if CYMBALIS_TUNING
    // Cmd-click on the logo opens the Tuning window too (docs/TUNING.md).
    if (e.eventComponent == this && e.mods.isCommandDown() && kLogo.contains (e.getPosition()))
    {
        toggleTuningWindow (true);
        return;
    }
#endif
    const int learn = processor.midiLearnSlot();
    if (learn < 0 || popup != nullptr)
        return;
    if (e.eventComponent != noteBoxes[(size_t) learn].get())
    {
        processor.cancelMidiLearn();
        refresh();
    }
}

// The area of a knob's header, wide enough for its readout.
juce::Rectangle<int> Content::labelArea (int knob) const
{
    const auto& spec = knobSpecs[(size_t) knob];
    const auto b = inkBounds (assets.font (kHeaderSize, spec.spacing), spec.label);
    return juce::Rectangle<float> (spec.labelInk.x, spec.labelInk.y, b.getWidth(), b.getHeight())
        .expanded (140.0f, 12.0f)
        .toNearestInt();
}

void Content::sliderValueChanged (juce::Slider* s)
{
    if (draggingKnob >= 0 && knobs[(size_t) draggingKnob].get() == s)
        repaint (labelArea (draggingKnob));
}

void Content::sliderDragStarted (juce::Slider* s)
{
    for (size_t i = 0; i < knobs.size(); ++i)
        if (knobs[i].get() == s)
            draggingKnob = (int) i;
    if (draggingKnob >= 0)
        repaint (labelArea (draggingKnob));
}

void Content::sliderDragEnded (juce::Slider*)
{
    if (draggingKnob >= 0)
        repaint (labelArea (draggingKnob));
    draggingKnob = -1;
}

void Content::showPopup (std::unique_ptr<juce::Component> panel, juce::Rectangle<int> bounds, std::function<void()> onClose)
{
    closePopup();
    popupClosed = std::move (onClose);
    popupLayer = std::make_unique<PopupLayer> ([this] { closePopup(); });
    popupLayer->setBounds (getLocalBounds());
    addAndMakeVisible (*popupLayer);
    popup = std::move (panel);
    popup->setBounds (bounds);
    addAndMakeVisible (*popup);
}

void Content::closePopup()
{
    if (popup == nullptr)
        return;
    // Deleting the panel from inside its own mouse callback is unsafe, so defer it.
    auto* p = popup.release();
    auto* l = popupLayer.release();
    p->setVisible (false);
    l->setVisible (false);
    juce::MessageManager::callAsync ([p, l] {
        delete p;
        delete l;
    });
    if (auto f = std::move (popupClosed))
        f();
    popupClosed = nullptr;
}

void Content::openPresetMenu()
{
    juce::StringArray names;
    for (const auto& preset : kPresets)
        names.add (preset.name);
    auto panel = std::make_unique<PopupPanel> (assets, names, processor.presetIndex(), 1,
                                               juce::Point<int> { kPresetBox.getWidth() - 2 * (PopupPanel::border + PopupPanel::padding), 40 });
    panel->onChoose = [this] (int i) {
        processor.setPreset (i);
        closePopup();
        refresh();
    };
    const auto b = kPresetBox.withY (kPresetBox.getBottom() + 4).withHeight (panel->getHeight());
    presetBox.setOpen (true);
    showPopup (std::move (panel), b, [this] { presetBox.setOpen (false); });
}

// A 4 x 4 grid of channels 1-16, below the box and right-aligned with it.
void Content::openChannelMenu()
{
    juce::StringArray items;
    for (int c = 1; c <= 16; ++c)
        items.add (juce::String (c));
    auto panel = std::make_unique<PopupPanel> (assets, items, processor.midiChannel() - 1, 4, juce::Point<int> { 61, 40 });
    panel->onChoose = [this] (int i) {
        processor.setMidiChannel (i + 1);
        closePopup();
        refresh();
    };
    const int w = panel->getWidth(), h = panel->getHeight();
    const juce::Rectangle<int> b (kChannelBox.getRight() - w, kChannelBox.getBottom() + 3, w, h);
    channelBox.setOpen (true);
    showPopup (std::move (panel), b, [this] { channelBox.setOpen (false); });
}

// MIDI LEARN, RESET DEFAULT, NONE and the note list, right-aligned below the note box.
void Content::openNoteMenu (int slot)
{
    auto menu = std::make_unique<NoteMenu> (assets, processor.slotNote (slot));
    menu->onAction = [this, slot] (int action) {
        closePopup();
        if (action == NoteMenu::learn)
            processor.startMidiLearn (slot);
        else
        {
            processor.cancelMidiLearn();
            processor.setSlotNote (slot, action == NoteMenu::resetDefault ? defaultNote (processor.presetIndex(), slot) : -1);
        }
        refresh();
    };
    menu->onNote = [this, slot] (int note) {
        closePopup();
        processor.cancelMidiLearn();
        processor.setSlotNote (slot, note);
        refresh();
    };
    auto* box = noteBoxes[(size_t) slot].get();
    const int w = menu->getWidth(), h = menu->getHeight();
    const juce::Rectangle<int> b (box->getRight() - w, box->getBottom() + 3, w, h);
    box->setOpen (true);
    showPopup (std::move (menu), b, [box] { box->setOpen (false); });
}

// TIP / SHOULDER below the stick box.
void Content::openStickMenu (int slot)
{
    const int current = processor.slotStick (slot) == Stick::shoulder ? 1 : 0;
    auto panel = std::make_unique<PopupPanel> (assets, juce::StringArray { "TIP", "SHOULDER" }, current, 1,
                                               juce::Point<int> { 130, 32 });
    panel->onChoose = [this, slot] (int i) {
        closePopup();
        processor.setSlotStick (slot, i == 0 ? Stick::tip : Stick::shoulder);
        refresh();
    };
    auto* box = stickBoxes[(size_t) slot].get();
    const juce::Rectangle<int> b (box->getX(), box->getBottom() + 3, panel->getWidth(), panel->getHeight());
    box->setOpen (true);
    showPopup (std::move (panel), b, [box] { box->setOpen (false); });
}

void Content::paint (juce::Graphics& g)
{
    // The cymbal area is its own (opaque) component, CymbalView.
    g.fillAll (palette::background);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    if (assets.background.isValid())
        g.drawImage (assets.background, getLocalBounds().toFloat());
    paintLines (g);
    paintLabels (g);
    paintRows (g);
}

void Content::paintLines (juce::Graphics& g)
{
    g.setColour (palette::divider);
    g.fillRect (39, 757, 1161 - 39, 1); // above the bottom row
    g.fillRect (1188, 110, 1, 850 - 110);

    // - and + under Velocity Sensitivity and Volume, at fixed offsets from each knob
    g.setColour (palette::secondary);
    auto minus = [&g] (float x, float y) { g.fillRect (juce::Rectangle<float> (x, y, 11.0f, 1.0f)); };
    auto plus = [&g] (float cx, float cy) {
        g.fillRect (juce::Rectangle<float> (cx - 5.0f, cy - 0.5f, 10.0f, 1.0f));
        g.fillRect (juce::Rectangle<float> (cx - 0.5f, cy - 4.5f, 1.0f, 9.0f));
    };
    for (size_t i : { size_t (0), size_t (1) })
    {
        const auto c = knobSpecs[i].centre;
        minus (c.x - 81.0f, c.y + 81.0f);
        plus (c.x + 73.0f, c.y + 82.0f);
    }
}

void Content::paintLabels (juce::Graphics& g)
{
    const auto header = [this, &g] (const char* text, float x, float y, float spacing, juce::Colour c) {
        drawInkText (g, assets.font (kHeaderSize, spacing), text, { x, y }, c);
    };
    const auto small = [this, &g] (const char* text, float x, float y, float spacing) {
        drawInkText (g, assets.font (kRowSize, spacing), text, { x, y }, palette::secondary);
    };

    header ("CYMBAL", 160.0f, 348.0f, 3.45f, palette::label);
    // MIDI CHANNEL on its box's line, to the left: its ink ends at x 1384, centred on the box.
    const auto channelInk = inkBounds (assets.font (kHeaderSize, 3.11f), "MIDI CHANNEL");
    header ("MIDI CHANNEL", kChannelLabelRight - channelInk.getWidth(), kChannelBox.toFloat().getCentreY() - channelInk.getHeight() * 0.5f,
            3.11f, palette::label);
    header ("STICK ARTICULATION", 1201.0f, 98.0f, 3.29f, palette::label);
    header ("MIDI NOTE", 1424.0f, 98.0f, 3.00f, palette::label);

    // Knob headers; while a knob is dragged its readout replaces its header.
    for (size_t i = 0; i < knobSpecs.size(); ++i)
    {
        const auto& spec = knobSpecs[i];
        const auto f = assets.font (kHeaderSize, spec.spacing);
        if ((int) i == draggingKnob)
        {
            const auto lb = inkBounds (f, spec.label);
            juce::String readout;
            if (auto* param = processor.parameters().getParameter (spec.paramId))
                readout = param->getCurrentValueAsText();
            drawInkTextCentred (g, f, readout, { spec.labelInk.x + lb.getWidth() * 0.5f, spec.labelInk.y + lb.getHeight() * 0.5f },
                                spec.colour);
        }
        else
            drawInkText (g, f, spec.label, spec.labelInk, spec.colour);
    }

    // BLEND POSITION 1 runs from the preset's bell rows (BELL; CENTER on SMOKE FLAT RIDE) to BOW.
    small (kPresets[(size_t) processor.presetIndex()].blend1Left, 1249.0f, 720.0f, 3.33f);
    small ("BOW", 1409.0f, 720.0f, 2.88f);
    small ("BOW", 1249.0f, 958.0f, 2.88f);
    small ("EDGE", 1409.0f, 958.0f, 2.83f);

    // L and R under Pan, each centred 73 units either side of its knob.
    const auto rowFont = assets.font (kRowSize);
    const float panX = knobSpecs[4].centre.x;
    for (auto [text, x] : { std::pair<const char*, float> { "L", panX - 73.0f }, { "R", panX + 73.0f } })
        drawInkText (g, rowFont, text, { x - inkBounds (rowFont, text).getWidth() * 0.5f, 958.0f }, palette::secondary);

    // The DHammers logo: a vertical gradient over its ink, top to bottom.
    if (! logoPath.isEmpty())
    {
        const auto ink = logoPath.getBounds();
        g.setGradientFill (juce::ColourGradient::vertical (palette::logoTop, ink.getY(), palette::logoBottom, ink.getBottom()));
        g.fillPath (logoPath);
    }
}

void Content::paintRows (juce::Graphics& g)
{
    // The preset's row labels (SlotTable.h), drawn like the row's own.
    const auto& preset = kPresets[(size_t) processor.presetIndex()];
    for (size_t s = 0; s < kSlots.size(); ++s)
    {
        if (! rowPlays (processor.presetIndex(), (int) s))
            continue; // a row the preset does not have
        const auto& ps = preset.slots[s];
        const float spacing = ps.label != nullptr ? ps.labelSpacing : kRowLabelSpacing[s];
        drawInkText (g, assets.font (kRowSize, spacing), rowLabel (processor.presetIndex(), (int) s),
                     { 1201.0f, kRowLabelY[s] }, hitTracker->rowLit ((int) s) ? palette::openOutline : palette::secondary);
    }
}

// A row's label, which the hit display lights.
juce::Rectangle<int> Content::rowLabelArea (int slot) const
{
    return { 1196, kRowTop[(size_t) slot] - 2, 68, 29 };
}

} // namespace cymbalis::ui

// =====================================================================================
CymbalisEditor::CymbalisEditor (CymbalisProcessor& p) : AudioProcessorEditor (&p), processorRef (p)
{
    using namespace cymbalis::ui;
    // Read the saved size first: setting the resize limits below already resizes the editor
    // (to the minimum), and resized() records every size as the session's editor size.
    int w = p.editorSize().x;

    content = std::make_unique<Content> (p);
    addAndMakeVisible (*content);

    // Resizable at a fixed 3:2, from 960 x 640 to 1536 x 1024 (UI_SPEC section 2).
    setResizable (true, true);
    setResizeLimits (960, 640, kDesignWidth, kDesignHeight);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) kDesignWidth / (double) kDesignHeight);
    setLookAndFeel (&resizerLookAndFeel);

    // The saved size, or the largest size that fits the screen.
    if (w < 960)
    {
        juce::Rectangle<int> area (0, 0, kDesignWidth, kDesignHeight);
        if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            area = d->userArea;
        w = juce::jmin (kDesignWidth, area.getWidth() - 40, juce::roundToInt ((area.getHeight() - 80) * 1.5));
    }
    w = juce::jlimit (960, kDesignWidth, w);
    sizeIsSet = true;
    setSize (w, juce::roundToInt (w * (double) kDesignHeight / (double) kDesignWidth));
    startTimerHz (30);
}

CymbalisEditor::~CymbalisEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void CymbalisEditor::paint (juce::Graphics& g)
{
    g.fillAll (cymbalis::ui::palette::background);
}

void CymbalisEditor::resized()
{
    // Everything is painted in design units and scaled with one transform. If a host gives
    // the editor a size that is not 3:2, the whole design still fits, centred.
    using namespace cymbalis::ui;
    const float scale = juce::jmin ((float) getWidth() / (float) kDesignWidth, (float) getHeight() / (float) kDesignHeight);
    content->setTransform (juce::AffineTransform::scale (scale).translated (
        ((float) getWidth() - scale * (float) kDesignWidth) * 0.5f, ((float) getHeight() - scale * (float) kDesignHeight) * 0.5f));
    content->setBounds (0, 0, kDesignWidth, kDesignHeight);
    if (sizeIsSet)
        processorRef.setEditorSize (getWidth(), getHeight());
}

void CymbalisEditor::timerCallback()
{
    content->refresh();
}

void CymbalisEditor::ResizerLookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool isMouseOver,
                                                            bool isMouseDragging)
{
    using namespace cymbalis::ui;
    g.setColour (isMouseDragging ? palette::openOutline : (isMouseOver ? palette::bronze : palette::boxOutline));
    const float s = (float) juce::jmin (w, h);
    for (float i = 0.3f; i < 1.0f; i += 0.3f)
        g.drawLine ((float) w - s * i, (float) h, (float) w, (float) h - s * i, 1.0f);
}
