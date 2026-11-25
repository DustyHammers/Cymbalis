#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace cymbalis::uitest
{
// Posts a left click (mouse down, then up) at `p`, a point in `target`'s coordinates, to
// the native window that holds `target`. A window of this process in front of that window at
// `p` is first moved aside, to the right of the point, and a line says so; putBack() returns it
// to where it was, once the click has been handled.
void click (juce::Component& target, juce::Point<float> p);
void putBack();

// Two clicks at `p` (the second a double-click), as click().
void doubleClick (juce::Component& target, juce::Point<float> p);

// The value text of the slider at `p` (a point in `target`'s coordinates) in the native window
// that holds `target`, as the window's accessibility elements give it: the value the slider
// shows. Otherwise what failed.
juce::String sliderText (juce::Component& target, juce::Point<float> p);

// The description (accessibility label) of the element titled `title` in the native window that
// holds `target`, as the window's accessibility elements give it (JUCE: a component's title and
// description). "no element titled ..." when none has that title.
juce::String elementText (juce::Component& target, const juce::String& title);

// Presses at `from`, moves to `to` in steps and releases there.
void drag (juce::Component& target, juce::Point<float> from, juce::Point<float> to);

// The same in parts, so the result can be read while the button is still down: press at `at`;
// move from `from` to `to` in steps with the button down (the view pressed keeps the drag,
// wherever it goes); release at `at`. The points are in `target`'s coordinates when pressed:
// the window keeps them while the target resizes under the drag.
void press (juce::Component& target, juce::Point<float> at);
void dragTo (juce::Component& target, juce::Point<float> from, juce::Point<float> to);
void release (juce::Component& target, juce::Point<float> at);

// Turns the scroll wheel over `p`, a point in `target`'s coordinates, in the native window
// that holds `target`: two steps down, then one up. Returns what failed, or nothing.
juce::String wheel (juce::Component& target, juce::Point<float> p);

// Clicks the middle of the element titled `title` (JUCE: a component's title) in the native
// window that holds `target`, such as the CYMBAL and TUNING tabs. Returns what failed, or nothing.
juce::String clickTitled (juce::Component& target, const juce::String& title);

// The editor's TUNING page (docs/UI_SPEC.md section 6): its panel is drawn in the page area,
// design units 460,100 to 1500,1000, of the editor in the window that holds `editor` (the host's
// editor component, which the plugin's editor fills: the design scaled to fit, centred).

// The scroll wheel, as wheel(), over the TUNING page at (fx, fy): fractions of the page area's
// width and height from its top left.
juce::String wheelTuning (juce::Component& editor, float fx, float fy);

// "shown" when the panel's SAVE... button is in the page area (the TUNING page shows), "hidden"
// when the window has no such button (the CYMBAL page shows); otherwise what failed.
juce::String tuningPageState (juce::Component& editor);

// The panel's layout as the window's accessibility elements give it, in screen points: the
// page area's size, the SAVE... button's size and RESET ALL's distance from the page area's
// right edge, as "page WxH, SAVE... WxH, RESET ALL N from the page's right"; otherwise what failed.
juce::String tuningLayout (juce::Component& editor);

// The panel's controls are found by their text through the window's accessibility elements
// (JUCE gives a label's text as its value, a button's as its title). A row in the list is
// first scrolled into view: the list to its top, then down with the scroll wheel until the row
// is between a quarter and three quarters of the page area's height. Clicks are real mouse
// events, as click(). Each returns what failed, or nothing.

// Clicks the slider of the row whose name starts with `rowName`, at `fraction` of the
// slider's width from its left.
juce::String clickTuningSlider (juce::Component& editor, const juce::String& rowName, float fraction);

// Clicks the centre of the button whose text is `text`; `inList`: a button in the list (else
// one of the panel's top bar).
juce::String clickTuningButton (juce::Component& editor, const juce::String& text, bool inList);

// Clicks the centre of the button whose text is `text` on the list row whose name starts with
// `rowName` (for a button several rows show, such as NOVA SHOULDER PROFILE).
juce::String clickTuningButtonBeside (juce::Component& editor, const juce::String& rowName, const juce::String& text);

// "shown" when the panel's list shows `text` (an exact match) somewhere down it, "none" when
// nothing does, otherwise what failed.
juce::String tuningShows (juce::Component& editor, const juce::String& text);

// From now on the next save or open panel (NSSavePanel, NSOpenPanel) is not shown: it answers
// at once, OK, with `file`. endFilePanels() puts the panels back.
void answerFilePanels (const juce::File& file);
void endFilePanels();
} // namespace cymbalis::uitest
