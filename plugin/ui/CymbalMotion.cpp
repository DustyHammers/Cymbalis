#include "ui/CymbalMotion.h"

#include <cmath>

namespace cymbalis::ui
{

namespace
{
// Per strike position: tilt A (per unit velocity), decay tau (s), wobble rate f (turns per
// second), shift D (design units) and spin (degrees). Linear in the position between bell
// (1.0), bow (0.5) and edge (0.0), so the blend knobs move smoothly between them.
struct Settings
{
    float tilt, tau, rate, shift, spin;
};
constexpr Settings bellSettings { 0.002f, 0.30f, 2.6f, 0.2f, 0.0f };
constexpr Settings bowSettings { 0.006f, 0.50f, 2.0f, 0.5f, 0.0f };
constexpr Settings edgeSettings { 0.025f, 1.30f, 1.4f, 2.1f, 0.15f };

Settings settingsAt (float position)
{
    const float p = juce::jlimit (0.0f, 1.0f, position);
    const auto mix = [] (const Settings& a, const Settings& b, float t) {
        return Settings { a.tilt + (b.tilt - a.tilt) * t, a.tau + (b.tau - a.tau) * t, a.rate + (b.rate - a.rate) * t,
                          a.shift + (b.shift - a.shift) * t, a.spin + (b.spin - a.spin) * t };
    };
    return p >= 0.5f ? mix (bowSettings, bellSettings, (p - 0.5f) * 2.0f) : mix (edgeSettings, bowSettings, p * 2.0f);
}

constexpr float attackS = 0.02f;   // r = (1 - e^(-a / 0.02)) e^(-a / tau)
constexpr float lifetimes = 7.0f;  // a hit ends when its age passes 7 tau
constexpr float oval = 0.85f;      // the low point's path round the rim: 0.85 across the hit's direction
constexpr float restTilt = 0.0005f, restShift = 0.1f, restSpinDegrees = 0.005f;

// The overlay at full strength, k = 0.025 and above: the shading +-1.2 k, so +-3 % at the
// rim, and the light band 15 % at its middle. Both follow min (k, 0.025), so one image at
// full strength, drawn with opacity min (k, 0.025) / 0.025, gives both.
constexpr float fullTilt = 0.025f;
constexpr float shadeAtFull = 1.2f * fullTilt;
constexpr float bandAtFull = 0.15f;
constexpr float bandCentre = -0.35f;          // of R, on the raised side (-u)
constexpr float bandSigma = 0.18f / 2.3548f;  // of R: 0.18 R wide at half height
constexpr float bandRed = 1.0f, bandGreen = 0.93f, bandBlue = 0.80f;
} // namespace

void CymbalMotion::setDisc (juce::Point<float> c, float r)
{
    centre = c;
    radius = r;
    numHits = 0;
    isMoving = canMove = false;
    pose = {};
    tiltAmount = tiltAngle = shiftBound = 0.0f;
    overlay = {};
    overlayScale = 0.0f;
}

void CymbalMotion::addHit (float velocity, float position, float angle, double now)
{
    if (numHits == maxHits) // the oldest goes
    {
        for (int i = 0; i + 1 < numHits; ++i)
            hits[(size_t) i] = hits[(size_t) i + 1];
        --numHits;
    }
    const auto s = settingsAt (position);
    hits[(size_t) numHits++] = { now, juce::jlimit (0.0f, 1.0f, velocity), angle, s.tilt, s.tau, s.rate, s.shift, s.spin };
}

void CymbalMotion::step (double now)
{
    int kept = 0;
    for (int i = 0; i < numHits; ++i)
        if ((now - hits[(size_t) i].startMs) * 0.001 <= lifetimes * hits[(size_t) i].tau)
            hits[(size_t) kept++] = hits[(size_t) i];
    numHits = kept;

    // Tilt T, shift S and spin phi summed over the hits, and the bounds they decay under
    // (|d| <= 1 and r <= e^(-a / tau)), which say whether anything can still move.
    juce::Point<float> tilt, shift;
    float spin = 0.0f, tiltReach = 0.0f, shiftReachNow = 0.0f, spinReach = 0.0f;
    for (int i = 0; i < numHits; ++i)
    {
        const auto& h = hits[(size_t) i];
        const float a = juce::jmax (0.0f, (float) ((now - h.startMs) * 0.001));
        const float envelope = std::exp (-a / h.tau);
        const float r = (1.0f - std::exp (-a / attackS)) * envelope;
        const float phase = juce::MathConstants<float>::twoPi * h.rate * a;
        const float c = std::cos (phase), s = std::sin (phase);
        const juce::Point<float> u (std::cos (h.angle), std::sin (h.angle)); // toward the hit
        const juce::Point<float> w (-u.y, u.x);                              // u turned 90 degrees clockwise
        const auto d = u * c + w * (oval * s);
        tilt += d * (h.tilt * h.velocity * r);
        shift += d * (h.shift * h.velocity * r);
        spin += h.spin * h.velocity * r * c;
        tiltReach += h.tilt * h.velocity * envelope;
        shiftReachNow += h.shift * h.velocity * envelope;
        spinReach += h.spin * h.velocity * envelope;
    }
    canMove = tiltReach >= restTilt || shiftReachNow >= restShift || spinReach >= restSpinDegrees;
    shiftBound = shiftReachNow;

    const float k = tilt.getDistanceFromOrigin();
    isMoving = k >= restTilt || shift.getDistanceFromOrigin() >= restShift || std::abs (spin) >= restSpinDegrees;
    if (! isMoving)
    {
        pose = {};
        tiltAmount = tiltAngle = 0.0f;
        return;
    }
    // p' = c - S + Rot (phi) (I - k u u^T) (p - c): squashed by k along the tilt, turned, shifted.
    tiltAmount = k;
    tiltAngle = k > 0.0f ? std::atan2 (tilt.y, tilt.x) : 0.0f;
    const auto t = k > 0.0f ? tilt / k : juce::Point<float> (1.0f, 0.0f);
    const juce::AffineTransform squash (1.0f - k * t.x * t.x, -k * t.x * t.y, 0.0f, -k * t.x * t.y, 1.0f - k * t.y * t.y, 0.0f);
    pose = juce::AffineTransform::translation (-centre)
               .followedBy (squash)
               .followedBy (juce::AffineTransform::rotation (juce::degreesToRadians (spin)))
               .followedBy (juce::AffineTransform::translation (centre - shift));
}

void CymbalMotion::drawOverlay (juce::Graphics& g) const
{
    const float opacity = juce::jmin (tiltAmount, fullTilt) / fullTilt;
    if (! isMoving || opacity < 1.0f / 255.0f)
        return;
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    prepare (scale);
    g.setColour (juce::Colours::black.withAlpha (opacity));
    g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
    g.drawImageTransformed (overlay, juce::AffineTransform::scale (1.0f / overlayScale)
                                         .translated (overlayOrigin)
                                         .followedBy (juce::AffineTransform::rotation (tiltAngle))
                                         .followedBy (juce::AffineTransform::translation (centre))
                                         .followedBy (pose));
}

void CymbalMotion::prepare (float scale) const
{
    if (std::abs (scale - overlayScale) > 1.0e-3f * scale)
        buildOverlay (scale);
}

// The shading and the light band at full strength, in the disc, with the tilt along +x: the
// +x half (dipping away) darker, the -x half lighter, and the warm band across the disc at
// x = -0.35 R. Centred on (0, 0), at `scale` pixels per design unit.
void CymbalMotion::buildOverlay (float scale) const
{
    const float reach = radius + 1.0f;
    const int size = juce::jmax (1, (int) std::ceil (2.0f * reach * scale));
    overlayOrigin = { -reach, -reach };
    overlay = juce::Image (juce::Image::ARGB, size, size, true);
    juce::Image::BitmapData data (overlay, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < size; ++y)
    {
        const float py = -reach + ((float) y + 0.5f) / scale;
        for (int x = 0; x < size; ++x)
        {
            const float px = -reach + ((float) x + 0.5f) / scale;
            const float rho = std::sqrt (px * px + py * py);
            const float inside = juce::jlimit (0.0f, 1.0f, (radius - rho) * scale + 0.5f);
            if (inside <= 0.0f)
                continue;
            const float xn = px / radius;
            const float shade = shadeAtFull * juce::jmin (1.0f, std::abs (xn));
            const float shadeLevel = xn > 0.0f ? 0.0f : 1.0f; // black on the +x half, white on the -x half
            const float b = (xn - bandCentre) / bandSigma;
            const float band = bandAtFull * std::exp (-0.5f * b * b);
            // The band over the shading (premultiplied), then the disc.
            const float alpha = band + shade * (1.0f - band);
            if (alpha <= 0.0f)
                continue;
            const float under = shade * (1.0f - band) * shadeLevel;
            data.setPixelColour (x, y, juce::Colour::fromFloatRGBA ((band * bandRed + under) / alpha, (band * bandGreen + under) / alpha,
                                                                    (band * bandBlue + under) / alpha, alpha * inside));
        }
    }
    overlayScale = scale;
}

} // namespace cymbalis::ui
