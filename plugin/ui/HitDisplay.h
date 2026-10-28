#pragma once

// The hits on the cymbal (docs/UI_SPEC.md sections 10 and 11). The processor writes one event per
// strike into a FIFO; the tracker reads them on the editor's timer, keeps up to 16 hits for the
// wireframe cymbal (CymbalView) to move with, and lights the row that played while its hit's
// flash lasts. Its own 30 Hz timer runs only while a hit is younger than 6 s: each tick hands
// the hits to the cymbal, which draws a new frame with them.

#include "PluginProcessor.h"
#include "ui/CymbalView.h"

#include <array>
#include <functional>
#include <memory>

namespace cymbalis::ui
{

class HitTracker final : private juce::Timer
{
public:
    HitTracker (CymbalisProcessor& p, CymbalView& view);
    ~HitTracker() override;

    // The current preset's cymbal: the hits so far go.
    void setGeometry (std::shared_ptr<const wire::Geometry> g);

    // Called by the editor's timer: takes the new hits from the processor's FIFO.
    void poll();

    // While a row's latest hit's flash lasts (T = 0.25 + 0.25 v seconds).
    bool rowLit (int slot) const noexcept { return lit[(size_t) slot]; }
    std::function<void (int slot)> onRowLitChanged;

    int numActive() const noexcept { return numHits; }
    bool moving() const noexcept { return isTimerRunning(); }

private:
    void timerCallback() override;
    void step (double nowMs);
    void addHit (const CymbalisProcessor::HitEvent& e, double nowMs);

    CymbalisProcessor& processor;
    CymbalView& view;
    std::shared_ptr<const wire::Geometry> geometry;
    juce::Random random;
    std::array<wire::Hit, wire::kMaxHits> hits {};
    int numHits = 0;
    std::array<double, kNumSlots> litUntilMs {};
    std::array<bool, kNumSlots> lit {};
};

} // namespace cymbalis::ui
