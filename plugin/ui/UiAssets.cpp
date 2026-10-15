#include "UiAssets.h"

#include <BinaryData.h>

#include <cstring>

namespace cymbalis::ui
{

namespace
{
const char* resource (const char* originalFilename, int& size)
{
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        if (juce::String (BinaryData::originalFilenames[i]) == originalFilename)
            return BinaryData::getNamedResource (BinaryData::namedResourceList[i], size);
    size = 0;
    return nullptr;
}

juce::Image image (const char* name)
{
    int size = 0;
    const char* data = resource (name, size);
    return data != nullptr ? juce::ImageCache::getFromMemory (data, size) : juce::Image();
}

// Cap height of a TrueType/OpenType font in em units (OS/2 sCapHeight / head unitsPerEm),
// or 0 if the tables are missing.
float capHeightInEm (const char* data, int size)
{
    const auto* d = reinterpret_cast<const unsigned char*> (data);
    auto u16 = [d] (int o) { return (int) ((d[o] << 8) | d[o + 1]); };
    auto u32 = [d] (int o) { return ((juce::uint32) d[o] << 24) | ((juce::uint32) d[o + 1] << 16) | ((juce::uint32) d[o + 2] << 8) | d[o + 3]; };
    if (size < 12)
        return 0.0f;
    const int numTables = u16 (4);
    int head = -1, os2 = -1;
    for (int i = 0; i < numTables && 12 + 16 * i + 16 <= size; ++i)
    {
        const int rec = 12 + 16 * i;
        const auto off = (int) u32 (rec + 8);
        if (std::memcmp (d + rec, "head", 4) == 0) head = off;
        if (std::memcmp (d + rec, "OS/2", 4) == 0) os2 = off;
    }
    if (head < 0 || os2 < 0 || head + 20 > size || os2 + 90 > size || u16 (os2) < 2)
        return 0.0f;
    const int upem = u16 (head + 18);
    const int cap = (int) (juce::int16) u16 (os2 + 88);
    return upem > 0 && cap > 0 ? (float) cap / (float) upem : 0.0f;
}

juce::GlyphArrangement arrange (const juce::Font& font, const juce::String& text)
{
    juce::GlyphArrangement ga;
    ga.addLineOfText (font, text, 0.0f, 0.0f);
    return ga;
}
} // namespace

Assets::Assets()
{
    background = image ("CYMBALIS_Background_logo.png");
    for (size_t k = 0; k < photos.size(); ++k)
        photos[k] = image (kPresets[k].photoFile);
    knobStrip = image ("knob.png");

    // Calibri Light when resources/ui/calibril.ttf is in the build, Carlito Regular otherwise
    // (UI_SPEC section 1).
    int size = 0;
    const char* fontData = resource ("calibril.ttf", size);
    if (fontData != nullptr)
    {
        typeface = juce::Typeface::createSystemTypefaceFor (fontData, (size_t) size);
        isCalibriLight = typeface != nullptr;
    }
    if (typeface == nullptr && (fontData = resource ("Carlito-Regular.ttf", size)) != nullptr)
        typeface = juce::Typeface::createSystemTypefaceFor (fontData, (size_t) size);

    if (typeface != nullptr && fontData != nullptr)
    {
        const float cap = capHeightInEm (fontData, size);
        juce::Font probe (typeface);
        probe.setHeight (100.0f);
        const float ink = inkBounds (probe, "H").getHeight();
        if (cap > 0.0f && ink > 0.0f)
            emPerHeight = ink / (100.0f * cap);
    }
}

// The spec's sizes are em sizes in design units (Kris's measurements with Carlito).
juce::Font Assets::font (float sizePx, float spacingPx) const
{
    juce::Font f = typeface != nullptr ? juce::Font (typeface) : juce::Font (sizePx);
    f.setHeight (sizePx / emPerHeight);
    if (spacingPx != 0.0f)
        f.setExtraKerningFactor (spacingPx / f.getHeight());
    return f;
}

juce::Rectangle<float> inkBounds (const juce::Font& font, const juce::String& text)
{
    juce::Path p;
    arrange (font, text).createPath (p);
    return p.getBounds();
}

void drawInkText (juce::Graphics& g, const juce::Font& font, const juce::String& text, juce::Point<float> inkTopLeft,
                  juce::Colour colour)
{
    juce::Path p;
    arrange (font, text).createPath (p);
    const auto b = p.getBounds();
    g.setColour (colour);
    g.fillPath (p, juce::AffineTransform::translation (inkTopLeft.x - b.getX(), inkTopLeft.y - b.getY()));
}

void drawInkTextCentred (juce::Graphics& g, const juce::Font& font, const juce::String& text,
                         juce::Point<float> centre, juce::Colour colour)
{
    juce::Path p;
    arrange (font, text).createPath (p);
    const auto b = p.getBounds();
    g.setColour (colour);
    g.fillPath (p, juce::AffineTransform::translation (centre.x - b.getCentreX(), centre.y - b.getCentreY()));
}

juce::String noteName (int note)
{
    if (note < 0 || note > 127)
        return {};
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[note % 12]) + juce::String (note / 12 - 2);
}

} // namespace cymbalis::ui
