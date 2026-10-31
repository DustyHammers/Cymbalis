#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace cymbalis::uitest
{
// Posts a left click (mouse down, then up) at `p`, a point in `target`'s coordinates, to
// the native window that holds `target`. A window of this process in front of that window at
// `p` (the Tuning window, on a small screen) is first moved aside, to the right of the point,
// and a line says so; putBack() returns it to where it was, once the click has been handled.
void click (juce::Component& target, juce::Point<float> p);
void putBack();

// Presses at `from`, moves to `to` in steps and releases there.
void drag (juce::Component& target, juce::Point<float> from, juce::Point<float> to);

// Turns the scroll wheel over `p`, a point in `target`'s coordinates, in the native window
// that holds `target`: two steps down, then one up. Returns what failed, or nothing.
juce::String wheel (juce::Component& target, juce::Point<float> p);

// The same over the Tuning window, at (fx, fy): fractions of its content's width and height
// from the top left.
juce::String wheelTuning (juce::Component& editor, float fx, float fy);

// The Tuning window (a window of this process whose title contains "Tuning"),
// seen from the window that holds `editor`: "visible-front" when it is visible, on screen in
// front of the editor's window (window server order), attached to it as a child window and
// on the active Space; "hidden" when it exists but is not visible; "none" when there is
// none; otherwise what failed.
juce::String tuningWindowState (juce::Component& editor);

// The Tuning window's controls are found by their text through the window's accessibility
// elements (JUCE gives a label's text as its value, a button's as its title). A row in the
// list is first scrolled into view: the list to its top, then down with the scroll wheel
// until the row is between a quarter and three quarters of the window's height. Clicks are
// real mouse events, as click(), and a window in front of the Tuning window at the point is
// moved aside or reported the same way. Each returns what failed, or nothing.

// Clicks the slider of the row whose name starts with `rowName`, at `fraction` of the
// slider's width from its left.
juce::String clickTuningSlider (juce::Component& editor, const juce::String& rowName, float fraction);

// Clicks the centre of the button whose text is `text`; `inList`: a button in the list (else
// one of the window's top bar).
juce::String clickTuningButton (juce::Component& editor, const juce::String& text, bool inList);

// "shown" when the Tuning window's list shows `text` (an exact match) somewhere down it,
// "none" when nothing does, otherwise what failed.
juce::String tuningShows (juce::Component& editor, const juce::String& text);

// From now on the next save or open panel (NSSavePanel, NSOpenPanel) is not shown: it answers
// at once, OK, with `file`. endFilePanels() puts the panels back.
void answerFilePanels (const juce::File& file);
void endFilePanels();
} // namespace cymbalis::uitest
