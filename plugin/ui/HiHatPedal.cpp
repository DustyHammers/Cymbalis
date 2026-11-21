#include "ui/HiHatPedal.h"

#include "ui/UiAssets.h"

#include <array>
#include <cmath>

namespace cymbalis::ui
{

namespace
{
// The reference's placement (hihat_pedal): the heel end x0, the floor y0 and the footboard's
// length; the far side offset by dx, dy (up and right).
constexpr float kX0 = 58.0f, kY0 = 735.0f, kLength = 230.0f;
constexpr float kDx = 12.0f, kDy = -8.0f;
} // namespace

HiHatPedal::HiHatPedal()
{
    setInterceptsMouseClicks (false, false);
    // Its opening, for screen readers (and tools/cymbalis_uitest).
    setTitle ("HI-HAT PEDAL");
    setDescription ("open " + juce::String (shown, 3));
}

void HiHatPedal::setOpening (float o)
{
    o = juce::jlimit (0.0f, 1.0f, o);
    if (juce::exactlyEqual (o, shown))
        return;
    shown = o;
    setDescription ("open " + juce::String (shown, 3));
    repaint();
}

void HiHatPedal::paint (juce::Graphics& g)
{
    using P = juce::Point<float>;
    g.addTransform (juce::AffineTransform::translation ((float) -area().getX(), (float) -area().getY()));
    const auto col = palette::pedalLine, hi = palette::pedalHighlight;
    const auto dim = juce::Colour::fromFloatRGBA (col.getFloatRed() * 0.55f, col.getFloatGreen() * 0.55f, col.getFloatBlue() * 0.55f, 1.0f);
    const auto line = [&g] (P a, P b, juce::Colour c) {
        g.setColour (c);
        g.drawLine ({ a, b }, 1.0f);
    };
    const auto circle = [&g] (P c, float r, juce::Colour colour) {
        g.setColour (colour);
        g.drawEllipse (c.x - r, c.y - r, 2.0f * r, 2.0f * r, 1.0f);
    };
    const P depth (kDx, kDy);
    // A quadrilateral, and its far side and the edges joining them in the dim colour.
    const auto box = [&] (std::array<P, 4> p, juce::Colour c) {
        for (size_t k = 0; k < 4; ++k)
            line (p[k], p[(k + 1) % 4], c);
        for (size_t k = 0; k < 4; ++k)
            line (p[k] + depth, p[(k + 1) % 4] + depth, dim);
        for (size_t k = 0; k < 4; ++k)
            line (p[k], p[k] + depth, dim);
    };

    const float fx = kX0 + kLength + 12.0f; // the front frame
    // the base plate
    box ({ P (kX0 - 6.0f, kY0), P (fx + 34.0f, kY0), P (fx + 34.0f, kY0 - 5.0f), P (kX0 - 6.0f, kY0 - 5.0f) }, col);
    // the heel plate and the hinge
    const P hinge (kX0 + 10.0f, kY0 - 15.0f);
    box ({ P (kX0 - 4.0f, kY0 - 5.0f), P (kX0 + 26.0f, kY0 - 5.0f), P (kX0 + 26.0f, kY0 - 12.0f), P (kX0 - 4.0f, kY0 - 12.0f) }, dim);
    circle (hinge, 6.0f, hi);
    circle (hinge, 2.0f, hi);
    // the footboard, hinged at the heel, its toe rising with the opening: 4 degrees closed, 20 open
    const float angle = juce::degreesToRadians (4.0f + 16.0f * shown);
    const float ca = std::cos (angle), sa = std::sin (angle), thickness = 8.0f;
    const P p0 = hinge, p1 (hinge.x + kLength * ca, hinge.y - kLength * sa);
    const P n (-sa * thickness, -ca * thickness);
    const P p3 = p0 + n, p2 = p1 + n;
    box ({ p0, p1, p2, p3 }, hi);
    for (int k = 1; k < 10; ++k) // grip ridges across the footboard's top
    {
        const P a = p3 + (p2 - p3) * ((float) k / 10.0f);
        line (a, a + depth, dim);
    }
    // the front frame: two posts with the spring between, a top bar and the sprocket
    const float top = kY0 - 165.0f;
    for (const float x : { fx, fx + 22.0f })
    {
        line (P (x, kY0 - 5.0f), P (x, top), col);
        line (P (x, kY0 - 5.0f) + depth, P (x, top) + depth, dim);
    }
    line (P (fx - 4.0f, top), P (fx + 26.0f, top), col);
    line (P (fx - 4.0f, top), P (fx - 4.0f, top) + depth, dim);
    line (P (fx + 26.0f, top), P (fx + 26.0f, top) + depth, dim);
    line (P (fx - 4.0f, top) + depth, P (fx + 26.0f, top) + depth, dim);
    const P sprocket (fx + 11.0f, top - 9.0f);
    circle (sprocket, 9.0f, hi);
    circle (sprocket, 3.0f, hi);
    for (int y = (int) (top + 20.0f); y < (int) (kY0 - 30.0f); y += 10) // the spring, inside the frame
        line (P (fx + 4.0f, (float) y), P (fx + 18.0f, (float) y + 5.0f), dim);
    // the strap from the toe up over the sprocket, rising with the toe
    line (P (p2.x - 4.0f, p2.y + 2.0f), P (sprocket.x - 9.0f, sprocket.y), hi);
}

} // namespace cymbalis::ui
