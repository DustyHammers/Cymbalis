#include "ui/HitDisplay.h"

#include "ui/UiAssets.h"

#include <cmath>

namespace cymbalis::ui
{

namespace
{
constexpr float quarterPi = 0.785398163f;
constexpr float edgeFraction = 0.9f;  // from here out a hit is an arc along the rim
constexpr float arcReference = 0.97f; // the radius fraction the arc images are drawn at
constexpr float ringStep = 1.07f;     // radius ratio between cached spots and rings
constexpr float unseen = 0.004f;      // an opacity below this changes no 8-bit pixel: not drawn

// Ripple ring: 2.5 px, slightly soft. Bell ring: 2.2 px, crisp. Edge arc: soft edges of
// about 4 px; its inner arc 4 px.
constexpr float rippleHalf = 1.25f, rippleSoft = 1.5f;
constexpr float bellRingHalf = 1.1f;
constexpr float arcSoft = 4.0f, innerHalf = 2.0f, innerSoft = 1.5f;

// Radius fraction of a strike position: 1.0 -> 0.6 x bell, 0.5 -> (bell + 0.95) / 2, 0.0 -> 0.97,
// linear between them.
float radiusFraction (float position, float bell)
{
    const float p = juce::jlimit (0.0f, 1.0f, position);
    const float atBell = 0.6f * bell, atBow = 0.5f * (bell + 0.95f), atEdge = 0.97f;
    return p >= 0.5f ? atBow + (atBell - atBow) * (p - 0.5f) * 2.0f : atEdge + (atBow - atEdge) * p * 2.0f;
}

float spotRadiusFor (float velocity, bool shoulder) { return (24.0f + 40.0f * velocity) * (shoulder ? 1.3f : 1.0f); }
float arcSpan (float v) { return juce::degreesToRadians (20.0f + 28.0f * v); }
float arcStroke (float v) { return 10.0f + 8.0f * v; }

// Coverage of a band of half-width `half` around 0 at distance `d`, with a linear edge `soft` wide.
float band (float d, float half, float soft) { return juce::jlimit (0.0f, 1.0f, (half + 0.5f * soft - d) / soft); }

// A coloured image of `bounds` (design units) at `scale` pixels per unit, `colourAt` giving
// each pixel's colour. With `tile` above 0 it is kept as tiles of that many pixels, and
// tiles with nothing in them are dropped.
template <typename ColourAt>
std::vector<std::pair<juce::Image, juce::Point<int>>> renderTiles (juce::Rectangle<float> bounds, float scale, int tile,
                                                                     ColourAt colourAt)
{
    const int w = juce::jmax (1, (int) std::ceil (bounds.getWidth() * scale));
    const int h = juce::jmax (1, (int) std::ceil (bounds.getHeight() * scale));
    juce::Image image (juce::Image::ARGB, w, h, true);
    {
        juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < h; ++y)
        {
            const float py = bounds.getY() + ((float) y + 0.5f) / scale;
            for (int x = 0; x < w; ++x)
            {
                const auto c = colourAt (bounds.getX() + ((float) x + 0.5f) / scale, py);
                if (c.getAlpha() > 0)
                    data.setPixelColour (x, y, c);
            }
        }
    }
    std::vector<std::pair<juce::Image, juce::Point<int>>> tiles;
    if (tile <= 0)
    {
        tiles.emplace_back (image, juce::Point<int>());
        return tiles;
    }
    const juce::Image::BitmapData data (image, juce::Image::BitmapData::readOnly);
    for (int ty = 0; ty < h; ty += tile)
        for (int tx = 0; tx < w; tx += tile)
        {
            const auto area = juce::Rectangle<int> (tx, ty, tile, tile).getIntersection (image.getBounds());
            bool any = false;
            for (int y = area.getY(); y < area.getBottom() && ! any; ++y)
                for (int x = area.getX(); x < area.getRight() && ! any; ++x)
                    any = reinterpret_cast<const juce::PixelARGB*> (data.getPixelPointer (x, y))->getAlpha() > 0;
            if (any)
                tiles.emplace_back (image.getClippedImage (area).createCopy(), area.getPosition());
        }
    return tiles;
}
} // namespace

HitLayer::HitLayer (CymbalisProcessor& p) : processor (p)
{
    setInterceptsMouseClicks (false, false);
    setOpaque (false);
}

void HitLayer::setDisc (juce::Point<float> centre, float radius, float bellRadius)
{
    for (int i = 0; i < numMarks; ++i)
        repaint (marks[(size_t) i].shown);
    numMarks = 0;
    latest.fill (0);
    updateLit();
    stopTimer();
    discCentre = centre;
    discRadius = radius;
    bellZone = bellRadius;
    discClip.clear();
    discClip.addEllipse (juce::Rectangle<float> (2.0f * radius, 2.0f * radius).withCentre (centre));
    discBounds = discClip.getBounds().getSmallestIntegerContainer().expanded (1);
    arcScale = 0.0f; // the arcs follow the radius
}

// While marks are active this layer's own timer reads the FIFO (one 30 Hz frame clock);
// otherwise the editor's timer does, and a hit starts this layer's timer.
void HitLayer::poll()
{
    if (isTimerRunning())
        return;
    const double now = juce::Time::getMillisecondCounterHiRes();
    CymbalisProcessor::HitEvent e;
    bool any = false;
    while (processor.popHit (e))
    {
        addMark (e, now);
        any = true;
    }
    if (any)
        step (now);
}

void HitLayer::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    CymbalisProcessor::HitEvent e;
    while (processor.popHit (e))
        addMark (e, now);
    step (now);
}

// Lays the marks out at `now`: repaints what each covered and covers now, drops the
// finished ones, and keeps the timer running only while a mark is active.
void HitLayer::step (double now)
{
    frameMs = now;
    for (int i = numMarks - 1; i >= 0; --i)
    {
        auto& m = marks[(size_t) i];
        const bool done = now - m.startMs >= m.durationMs;
        const auto e = done ? juce::Rectangle<int>() : extent (m, now);
        repaint (m.shown.getUnion (e));
        m.shown = e;
        if (done)
            removeMark (i);
    }
    updateLit();
    if (numMarks == 0)
        stopTimer();
    else if (! isTimerRunning())
        startTimerHz (30);
}

void HitLayer::addMark (const CymbalisProcessor::HitEvent& e, double now)
{
    if (e.slot < 0 || e.slot >= kNumSlots)
        return;
    if (numMarks == maxMarks) // the oldest goes
    {
        repaint (marks[0].shown);
        removeMark (0);
    }
    auto& m = marks[(size_t) numMarks++];
    m = {};
    m.slot = e.slot;
    m.velocity = juce::jlimit (0.0f, 1.0f, e.velocity);
    m.shoulder = e.stick == (std::int8_t) Stick::shoulder;
    m.fraction = radiusFraction (e.position, bellZone);
    m.edge = m.fraction >= edgeFraction;
    m.bell = m.fraction < bellZone;
    m.ripple = m.velocity > 0.5f;
    // 4 to 5 o'clock: 45 degrees clockwise from 3 o'clock, +-12 degrees at random.
    m.angle = quarterPi + juce::degreesToRadians (24.0f * (random.nextFloat() - 0.5f));
    m.spotRadius = spotRadiusFor (m.velocity, m.shoulder);
    m.arcStep = juce::roundToInt (m.velocity * (float) (arcSteps - 1));
    m.centre = discCentre + juce::Point<float> (std::cos (m.angle), std::sin (m.angle)) * (m.fraction * discRadius);
    m.startMs = now;
    m.durationMs = 1000.0 * (0.25 + 0.25 * m.velocity);
    m.order = nextOrder++;
    if (m.edge)
    {
        // The arc's extent: its sector, sampled.
        const float v = (float) m.arcStep / (float) (arcSteps - 1);
        const float a = m.fraction * discRadius, half = 0.5f * arcStroke (v) + arcSoft;
        const float span = 0.5f * arcSpan (v) + arcSoft / a;
        juce::Rectangle<float> r (m.centre, m.centre);
        for (int i = 0; i <= 16; ++i)
        {
            const float phi = m.angle - span + 2.0f * span * (float) i / 16.0f;
            const juce::Point<float> dir (std::cos (phi), std::sin (phi));
            r = r.getUnion (juce::Rectangle<float> (discCentre + dir * (a - half), discCentre + dir * (a + half)));
        }
        m.arcBounds = r.getSmallestIntegerContainer().expanded (2).getIntersection (discBounds);
    }
    latest[(size_t) m.slot] = m.order;
}

void HitLayer::removeMark (int index)
{
    for (int i = index; i + 1 < numMarks; ++i)
        marks[(size_t) i] = marks[(size_t) i + 1];
    --numMarks;
}

// The area a mark covers at `now`: the spot and its ripple's current size, or the arc.
juce::Rectangle<int> HitLayer::extent (const Mark& m, double now) const
{
    if (m.edge)
        return m.arcBounds;
    const float t = juce::jlimit (0.0f, 1.0f, (float) ((now - m.startMs) / m.durationMs));
    const float k = (1.0f - t) * (1.0f - t);
    float reach = m.spotRadius * (1.0f - 0.3f * t);
    if (m.ripple && 0.35f * m.velocity * k >= unseen)
        reach = juce::jmax (reach, m.spotRadius * (1.0f + 1.6f * t) + rippleHalf + rippleSoft);
    return juce::Rectangle<float> (2.0f * reach, 2.0f * reach)
        .withCentre (m.centre)
        .getSmallestIntegerContainer()
        .expanded (2)
        .getIntersection (discBounds);
}

void HitLayer::updateLit()
{
    for (int s = 0; s < kNumSlots; ++s)
    {
        bool on = false;
        for (int i = 0; i < numMarks; ++i)
            on = on || marks[(size_t) i].order == latest[(size_t) s];
        if (on != lit[(size_t) s])
        {
            lit[(size_t) s] = on;
            if (onRowLitChanged)
                onRowLitChanged (s);
        }
    }
}

// The spots, ripple rings and bell rings at radii 7 % apart, coloured, at `scale` pixels
// per design unit. They are drawn unscaled, at whole pixels.
void HitLayer::buildSprites (float scale)
{
    const auto make = [scale] (float radius, float reach, int tile, auto colourAt) {
        const auto bounds = juce::Rectangle<float> (2.0f * reach, 2.0f * reach).withCentre ({});
        Sprite s;
        for (auto& t : renderTiles (bounds, scale, tile, colourAt))
            s.tiles.push_back ({ t.first, t.second });
        s.origin = bounds.getTopLeft();
        s.radius = radius;
        s.scale = scale;
        return s;
    };
    juce::ColourGradient grad (palette::hitCore, 0.0f, 0.0f, palette::hitBronze.withAlpha (0.0f), 1.0f, 0.0f, true);
    grad.addColour (0.12, palette::hitCream.withAlpha (0.95f));
    grad.addColour (0.25, palette::hitGold.withAlpha (0.75f));
    grad.addColour (0.60, palette::hitBronze.withAlpha (0.28f));
    const auto spot = [&] (float r) {
        return make (r, r + 1.0f, 0, [&] (float x, float y) {
            const float d = std::sqrt (x * x + y * y) / r;
            return d < 1.0f ? grad.getColourAtPosition (d) : juce::Colours::transparentBlack;
        });
    };
    const auto ring = [&] (float r, float half, float soft, juce::Colour c) {
        return make (r, r + half + soft, 32, [=] (float x, float y) {
            return c.withMultipliedAlpha (band (std::abs (std::sqrt (x * x + y * y) - r), half, soft));
        });
    };
    spots.clear();
    for (float r = 0.7f * spotRadiusFor (0.0f, false); r < spotRadiusFor (1.0f, true) * ringStep; r *= ringStep)
        spots.push_back (spot (r));
    ripples.clear();
    for (float r = spotRadiusFor (0.5f, false); r < 2.6f * spotRadiusFor (1.0f, true) * ringStep; r *= ringStep)
        ripples.push_back (ring (r, rippleHalf, rippleSoft, palette::hitGold));
    bellRings.clear();
    for (float r = 0.45f * 0.7f * spotRadiusFor (0.0f, false); r < 0.45f * spotRadiusFor (1.0f, true) * ringStep; r *= ringStep)
        bellRings.push_back (ring (r, bellRingHalf, 1.0f / scale, palette::hitBellRing));
    spriteScale = scale;
}

// The edge arcs at velocity 0, 0.2 .. 1, coloured: each centred on 3 o'clock (so that its
// image is no wider than the arc), at 0.97 of the disc's radius, positioned relative to the
// disc's centre, and clipped to the disc already (they are drawn rotated, which a clip would
// make slow); the inner arc for velocities above 0.5.
void HitLayer::buildArcs (float scale)
{
    const float a = arcReference * discRadius, disc = discRadius;
    const auto makeArc = [scale, a, disc] (float half, float soft, float halfSpan, juce::Colour c) {
        const float reach = half + soft;
        const float spread = halfSpan + soft / a;
        juce::Rectangle<float> bounds;
        for (int i = 0; i <= 16; ++i)
        {
            const float phi = -spread + 2.0f * spread * (float) i / 16.0f;
            const juce::Point<float> dir (std::cos (phi), std::sin (phi));
            const juce::Rectangle<float> seg (dir * (a - reach), dir * (a + reach));
            bounds = i == 0 ? seg : bounds.getUnion (seg);
        }
        bounds = bounds.expanded (1.0f);
        Sprite s;
        for (auto& t : renderTiles (bounds, scale, 0, [=] (float x, float y) {
                 const float rho = std::sqrt (x * x + y * y);
                 const float along = std::abs (std::atan2 (y, x)) * rho;
                 const float inside = juce::jlimit (0.0f, 1.0f, disc - rho + 0.5f);
                 return c.withMultipliedAlpha (band (std::abs (rho - a), half, soft) * band (along, halfSpan * rho, soft) * inside);
             }))
            s.tiles.push_back ({ t.first, t.second });
        s.origin = bounds.getTopLeft();
        s.radius = a;
        s.scale = scale;
        return s;
    };
    for (int i = 0; i < arcSteps; ++i)
    {
        const float v = (float) i / (float) (arcSteps - 1);
        arcOuter[(size_t) i] = makeArc (0.5f * arcStroke (v), arcSoft, 0.5f * arcSpan (v), palette::hitArc);
        arcInner[(size_t) i] = v > 0.5f ? makeArc (innerHalf, innerSoft, 0.3f * arcSpan (v), palette::hitCream) : Sprite {};
    }
    arcScale = scale;
}

void HitLayer::paint (juce::Graphics& g)
{
    if (numMarks == 0)
        return;
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (std::abs (scale - spriteScale) > 1.0e-3f * scale)
        buildSprites (scale);
    if (std::abs (scale - arcScale) > 1.0e-3f * scale)
        buildArcs (scale);

    // Edge arcs first: rotated, and already clipped to the disc.
    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    for (int i = 0; i < numMarks; ++i)
        if (marks[(size_t) i].edge)
            drawMark (g, marks[(size_t) i], frameMs, true);

    // Then the spots and rings, unscaled at whole pixels, clipped to the disc where one
    // reaches past it.
    bool clip = false;
    for (int i = 0; i < numMarks && ! clip; ++i)
    {
        if (marks[(size_t) i].edge)
            continue;
        const auto r = marks[(size_t) i].shown.toFloat();
        for (auto corner : { r.getTopLeft(), r.getTopRight(), r.getBottomLeft(), r.getBottomRight() })
            clip = clip || corner.getDistanceFrom (discCentre) > discRadius;
    }
    if (clip)
        g.reduceClipRegion (discClip);
    for (int i = 0; i < numMarks; ++i)
        if (! marks[(size_t) i].edge)
            drawMark (g, marks[(size_t) i], frameMs, false);
}

// A sprite centred on `at`, unscaled, its corner on a whole pixel.
void HitLayer::drawAt (juce::Graphics& g, const Sprite& s, juce::Point<float> at) const
{
    const auto corner = ((at + s.origin) * s.scale).roundToInt();
    for (auto& t : s.tiles)
        g.drawImageTransformed (t.image, juce::AffineTransform::translation ((float) (corner.x + t.offset.x), (float) (corner.y + t.offset.y))
                                             .scaled (1.0f / s.scale));
}

// An arc, placed by `place` (rotation and scale about the disc's centre).
void HitLayer::drawArc (juce::Graphics& g, const Sprite& s, const juce::AffineTransform& place) const
{
    for (auto& t : s.tiles)
        g.drawImageTransformed (t.image, juce::AffineTransform::scale (1.0f / s.scale).translated (s.origin).followedBy (place));
}

void HitLayer::drawMark (juce::Graphics& g, const Mark& m, double now, bool arcs) const
{
    const float t = (float) ((now - m.startMs) / m.durationMs);
    if (t >= 1.0f || m.edge != arcs)
        return;
    const float k = (1.0f - t) * (1.0f - t);
    const float v = m.velocity;
    const auto opacity = [&g] (float a) { g.setColour (juce::Colours::black.withAlpha (juce::jlimit (0.0f, 1.0f, a))); };
    if (m.edge)
    {
        const auto place = juce::AffineTransform::scale (m.fraction / arcReference).rotated (m.angle).translated (discCentre);
        opacity ((0.35f + 0.35f * v) * k);
        drawArc (g, arcOuter[(size_t) m.arcStep], place);
        if (v > 0.5f)
        {
            opacity (0.5f * k);
            drawArc (g, arcInner[(size_t) m.arcStep], place);
        }
        return;
    }
    const auto nearest = [] (const std::vector<Sprite>& sizes, float radius) -> const Sprite& {
        const int i = juce::roundToInt (std::log (radius / sizes.front().radius) / std::log (ringStep));
        return sizes[(size_t) juce::jlimit (0, (int) sizes.size() - 1, i)];
    };
    const float r = m.spotRadius * (1.0f - 0.3f * t);
    if ((0.35f + 0.65f * v) * k >= unseen)
    {
        opacity ((0.35f + 0.65f * v) * k);
        drawAt (g, nearest (spots, r), m.centre);
    }
    if (m.ripple && 0.35f * v * k >= unseen)
    {
        opacity (0.35f * v * k);
        drawAt (g, nearest (ripples, m.spotRadius * (1.0f + 1.6f * t)), m.centre);
    }
    if (m.bell && 0.85f * k >= unseen)
    {
        opacity (0.85f * k);
        drawAt (g, nearest (bellRings, 0.45f * r), m.centre);
    }
}

} // namespace cymbalis::ui
