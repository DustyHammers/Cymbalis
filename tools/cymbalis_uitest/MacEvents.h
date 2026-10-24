#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace cymbalis::uitest
{
// Posts a left click (mouse down, then up) at `p`, a point in `target`'s coordinates, to
// the native window that holds `target`.
void click (juce::Component& target, juce::Point<float> p);

// Presses at `from`, moves to `to` in steps and releases there.
void drag (juce::Component& target, juce::Point<float> from, juce::Point<float> to);

// The Tuning window (a window of this process whose title contains "Tuning"),
// seen from the window that holds `editor`: "visible-front" when it is visible, on screen in
// front of the editor's window (window server order), attached to it as a child window and
// on the active Space; "hidden" when it exists but is not visible; "none" when there is
// none; otherwise what failed.
juce::String tuningWindowState (juce::Component& editor);
} // namespace cymbalis::uitest
