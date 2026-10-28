#include "ui/HitDisplay.h"

#include <algorithm>

namespace cymbalis::ui
{

HitTracker::HitTracker (CymbalisProcessor& p, CymbalView& v) : processor (p), view (v) {}

HitTracker::~HitTracker()
{
    stopTimer();
}

void HitTracker::setGeometry (std::shared_ptr<const wire::Geometry> g)
{
    geometry = std::move (g);
    numHits = 0;
    litUntilMs.fill (0.0);
    step (juce::Time::getMillisecondCounterHiRes());
}

// While hits move, this tracker's own timer reads the FIFO (one 30 Hz frame clock); otherwise
// the editor's timer does, and a hit starts this one.
void HitTracker::poll()
{
    if (isTimerRunning())
        return;
    const double now = juce::Time::getMillisecondCounterHiRes();
    CymbalisProcessor::HitEvent e;
    bool any = false;
    while (processor.popHit (e))
    {
        addHit (e, now);
        any = true;
    }
    if (any)
        step (now);
}

void HitTracker::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    CymbalisProcessor::HitEvent e;
    while (processor.popHit (e))
        addHit (e, now);
    step (now);
}

// Drops the hits older than 6 s, updates the lit rows, and hands the hits to the cymbal (a new
// frame); the timer runs only while a hit moves.
void HitTracker::step (double now)
{
    int kept = 0;
    for (int i = 0; i < numHits; ++i)
        if (now - hits[(size_t) i].startMs < wire::kHitLifeMs)
            hits[(size_t) kept++] = hits[(size_t) i];
    numHits = kept;
    for (int s = 0; s < kNumSlots; ++s)
    {
        const bool on = now < litUntilMs[(size_t) s];
        if (on != lit[(size_t) s])
        {
            lit[(size_t) s] = on;
            if (onRowLitChanged)
                onRowLitChanged (s);
        }
    }
    view.setHits (hits.data(), numHits);
    if (numHits == 0)
        stopTimer();
    else if (! isTimerRunning())
        startTimerHz (30);
}

void HitTracker::addHit (const CymbalisProcessor::HitEvent& e, double now)
{
    if (e.slot < 0 || e.slot >= kNumSlots || geometry == nullptr)
        return;
    if (numHits == wire::kMaxHits) // the oldest goes
    {
        std::move (hits.begin() + 1, hits.begin() + numHits, hits.begin());
        --numHits;
    }
    // 4 to 5 o'clock: 45 degrees clockwise from 3 o'clock, +-12 degrees at random.
    const double angle = juce::degreesToRadians (45.0 + 24.0 * (random.nextDouble() - 0.5));
    const double velocity = juce::jlimit (0.0, 1.0, (double) e.velocity);
    hits[(size_t) numHits++] = wire::makeHit (*geometry, now, e.position, velocity, e.stick == (std::int8_t) Stick::shoulder, angle);
    litUntilMs[(size_t) e.slot] = now + 1000.0 * wire::flashSeconds (velocity);
}

} // namespace cymbalis::ui
