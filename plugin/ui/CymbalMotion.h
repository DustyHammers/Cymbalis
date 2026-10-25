#pragma once

// Cymbal motion (docs/UI_SPEC.md section 10, "Cymbal motion"): every hit the hit display
// reads also tips, shifts and turns the cymbal a little, and the motion dies away. The photo
// and the hit marks are drawn with the summed pose, and the photo gets a tilt shading and a
// light band. At rest everything is drawn exactly as without motion. Message thread only:
// the audio thread does nothing for it beyond the hit FIFO it already writes.

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace cymbalis::ui
{

class CymbalMotion
{
public:
    static constexpr int maxHits = 16;

    // The disc of the current preset's photo, in the photo area's coordinates. Clears the
    // hits and the overlay image.
    void setDisc (juce::Point<float> centre, float radius);

    // A hit: engine velocity, strike position (0 edge, 0.5 bow, 1 bell, after the blend
    // knobs) and its mark's angle (radians clockwise from 3 o'clock). When 16 are kept, the
    // oldest goes.
    void addHit (float velocity, float position, float angle, double nowMs);

    // The pose at `now`; drops the hits that have ended (age above 7 tau).
    void step (double nowMs);

    // Whether this frame's pose differs from rest; at rest the photo and the marks are drawn
    // as without motion.
    bool moving() const noexcept { return isMoving; }
    // Whether the hits still kept can move the cymbal past the rest thresholds, now or later.
    bool active() const noexcept { return canMove; }
    // Photo-area coordinates before and after the motion: tilt, turn and shift about the
    // disc's centre (identity at rest).
    const juce::AffineTransform& transform() const noexcept { return pose; }
    // The largest shift the hits kept can give now (design units), for the repaint margin.
    float shiftReach() const noexcept { return shiftBound; }

    // The tilt shading and the light band over the photo, with this frame's pose, in the
    // disc (nothing at rest), from an image rendered at the physical scale: by prepare(),
    // or here if it is needed first.
    void drawOverlay (juce::Graphics& g) const;
    void prepare (float scale) const;

private:
    struct Hit
    {
        double startMs = 0.0;
        float velocity = 0.0f, angle = 0.0f;
        float tilt = 0.0f, tau = 1.0f, rate = 0.0f, shift = 0.0f, spin = 0.0f; // at its position
    };

    void buildOverlay (float scale) const;

    std::array<Hit, maxHits> hits {};
    int numHits = 0;

    juce::Point<float> centre;
    float radius = 1.0f;

    bool isMoving = false, canMove = false;
    juce::AffineTransform pose;
    float tiltAmount = 0.0f, tiltAngle = 0.0f, shiftBound = 0.0f;

    mutable juce::Image overlay; // centred on the disc, its top-left at overlayOrigin
    mutable juce::Point<float> overlayOrigin;
    mutable float overlayScale = 0.0f;
};

} // namespace cymbalis::ui
