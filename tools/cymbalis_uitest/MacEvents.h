#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace cymbalis::uitest
{
// Posts a left click (mouse down, then up) at `p`, a point in `target`'s coordinates, to
// the native window that holds `target`.
void click (juce::Component& target, juce::Point<float> p);

// Presses at `from`, moves to `to` in steps and releases there.
void drag (juce::Component& target, juce::Point<float> from, juce::Point<float> to);
} // namespace cymbalis::uitest
