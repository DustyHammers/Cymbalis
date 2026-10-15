#pragma once

// Drawing resources and helpers for the editor (docs/UI_SPEC.md).

#include <juce_gui_basics/juce_gui_basics.h>

#include "../SlotTable.h"

#include <array>

namespace cymbalis::ui
{

// All coordinates are design units of the 1536 x 1024 artwork (UI_SPEC section 2).
constexpr int kDesignWidth = 1536;
constexpr int kDesignHeight = 1024;

// Palette, sampled from the background artwork (UI_SPEC section 5).
namespace palette
{
const juce::Colour background { 0xff0f0e0f };
const juce::Colour label { 0xffa7a7a4 };       // main labels, headers, values
const juce::Colour knobLabel { 0xffa09c99 };   // Velocity Sensitivity, Volume
const juce::Colour secondary { 0xff695e52 };   // row labels, knob end labels, secondary text
const juce::Colour bronze { 0xff76624c };      // accent; hover outline
const juce::Colour boxOutline { 0xff534739 };  // preset and channel boxes; popup border
const juce::Colour rowOutline { 0xff463c30 };  // row boxes
const juce::Colour arrow { 0xff999489 };       // dropdown arrow
const juce::Colour divider { 0xff353231 };
const juce::Colour popup { 0xff141313 };       // popup background
const juce::Colour openOutline { 0xffa5a299 }; // outline of a box whose popup is open
} // namespace palette

// The images and the typeface, loaded once from BinaryData.
class Assets
{
public:
    Assets();

    juce::Image background; // CYMBALIS_Background_logo.png, logo included
    std::array<juce::Image, kNumPresets> photos; // one per preset (kPresets[].photoFile)
    juce::Image knobStrip;  // knob.png, 91 frames of 128 x 128
    juce::Typeface::Ptr typeface;
    bool isCalibriLight = false; // false: Carlito Regular stands in

    // Em size per unit of JUCE font height for this typeface. JUCE's "points" are the em
    // size with CoreText but the ascent with FreeType, so the editor measures it: the ink
    // height of "H" against the font's cap height (OS/2 table).
    float emPerHeight = 2048.0f / 2500.0f;

    // The UI font at `sizePx` (em size, design units) with `spacingPx` of letter spacing.
    juce::Font font (float sizePx, float spacingPx = 0.0f) const;

    static constexpr int knobFrames = 91;
    static constexpr int knobFrameSize = 128;
};

// Tight ink bounds of `text` set on a baseline at y = 0.
juce::Rectangle<float> inkBounds (const juce::Font& font, const juce::String& text);

// Draws `text` so that its ink starts at `inkTopLeft`.
void drawInkText (juce::Graphics& g, const juce::Font& font, const juce::String& text, juce::Point<float> inkTopLeft,
                  juce::Colour colour);

// Draws `text` with its ink centred on `centre`.
void drawInkTextCentred (juce::Graphics& g, const juce::Font& font, const juce::String& text,
                         juce::Point<float> centre, juce::Colour colour);

// "C3 60" style note naming (Logic/Ableton: 60 = C3). Empty name for -1 (unassigned).
juce::String noteName (int note);

} // namespace cymbalis::ui
