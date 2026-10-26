#pragma once

// The hit display (docs/UI_SPEC.md section 10): for each strike a short-lived mark on the
// cymbal photo, where the strike landed, and the row that played it lit while its mark
// lasts. The processor writes one event per strike into a FIFO; this layer reads them on
// the editor's timer. Marks are drawn only from images rendered once per scale and preset,
// only the rectangles around them are repainted, and the layer's own timer runs only while
// a mark is active or the cymbal moves. Each hit also moves the cymbal (CymbalMotion): the
// marks are drawn with its pose, and the photo area draws the photo with it.

#include "PluginProcessor.h"
#include "ui/CymbalMotion.h"

#include <array>
#include <functional>
#include <vector>

namespace cymbalis::ui
{

class HitLayer final : public juce::Component, private juce::Timer
{
public:
    explicit HitLayer (CymbalisProcessor& p);

    // The disc of the current preset's photo, in this layer's coordinates, and its bell (or
    // centre) zone as a fraction of the radius. Clears the marks.
    void setDisc (juce::Point<float> centre, float radius, float bellRadius);

    // Called by the editor's timer: takes the new hits from the processor's FIFO.
    void poll();

    // Renders the marks' and the cymbal's images at this physical scale, ahead of the first
    // hit (the editor calls it once its size has settled).
    void prepare (float scale);

    // While a row's latest mark is active.
    bool rowLit (int slot) const noexcept { return lit[(size_t) slot]; }
    std::function<void (int slot)> onRowLitChanged;

    int numActive() const noexcept { return numMarks; }
    const CymbalMotion& motion() const noexcept { return cymbalMotion; }

    void paint (juce::Graphics& g) override;

    static constexpr int maxMarks = 16;

private:
    struct Mark
    {
        int slot = 0;
        float velocity = 0.0f;
        bool shoulder = false;
        bool edge = false;   // radius fraction >= 0.9: an arc along the rim instead of a spot
        bool bell = false;   // on the bell (radius fraction < bell radius): the bell ring too
        bool ripple = false; // velocity > 0.5
        float fraction = 0.0f; // radius fraction of the strike position
        float angle = 0.0f;    // radians, clockwise from 3 o'clock
        float spotRadius = 0.0f;
        int arcStep = 0;
        juce::Point<float> centre;
        juce::Rectangle<int> arcBounds; // an edge mark's extent (it does not move)
        double startMs = 0.0, durationMs = 1.0;
        juce::uint64 order = 0;
        juce::Rectangle<int> shown; // what the last frame covered
    };

    // A cached image of a shape, coloured, at `scale` pixels per design unit: its top-left
    // corner (design units) relative to the shape's reference point, and the shape's
    // radius. One image each, rings too: on macOS every image drawn is a separate GPU upload
    // and submission, so a few larger images cost far less than many small ones.
    struct Sprite
    {
        juce::Image image;
        juce::Point<float> origin;
        float radius = 0.0f;
        float scale = 1.0f;
    };

    static constexpr int arcSteps = 6; // edge arcs at velocity 0, 0.2 .. 1

    void timerCallback() override;
    void step (double nowMs);
    void addMark (const CymbalisProcessor::HitEvent& e, double nowMs);
    void removeMark (int index);
    juce::Rectangle<int> extent (const Mark& m, double nowMs) const;
    void updateLit();
    void buildSprites (float scale);
    void buildArcs (float scale);
    void drawMark (juce::Graphics& g, const Mark& m, double nowMs, bool arcs) const;
    void drawAt (juce::Graphics& g, const Sprite& s, juce::Point<float> at) const;
    void drawArc (juce::Graphics& g, const Sprite& s, const juce::AffineTransform& place) const;

    CymbalisProcessor& processor;
    juce::Random random;
    std::array<Mark, maxMarks> marks {};
    int numMarks = 0;
    juce::uint64 nextOrder = 1;
    double frameMs = 0.0; // the time the marks were last laid out (and are drawn at)
    std::array<juce::uint64, kNumSlots> latest {};
    std::array<bool, kNumSlots> lit {};

    juce::Point<float> discCentre;
    float discRadius = 1.0f, bellZone = 0.4f;
    juce::Path discClip;
    juce::Rectangle<int> discBounds;

    CymbalMotion cymbalMotion;
    float motionMargin = 0.0f; // the shift reach the last moving frame was repainted for

    float spriteScale = 0.0f, arcScale = 0.0f; // the physical scales they were built at (0: none)
    std::vector<Sprite> spots, ripples, bellRings; // at radii 7 % apart (spots: TIP and SHOULDER sizes)
    std::array<Sprite, arcSteps> arcOuter, arcInner;
};

} // namespace cymbalis::ui
