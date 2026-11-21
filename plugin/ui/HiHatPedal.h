#pragma once

// The hi-hat's pedal (docs/UI_SPEC.md section 11, "The pedal"; the approved reference is
// hihat_pedal in docs/ui/wireframe_reference.py): a side-profile wireframe in the editor's left
// column, shown on the hi-hat only. Its footboard's toe rises with the pedal's smoothed opening
// (4 degrees closed, 20 open). It is not joined to the cymbals and takes no clicks.

#include <juce_gui_basics/juce_gui_basics.h>

namespace cymbalis::ui
{

class HiHatPedal final : public juce::Component
{
public:
    HiHatPedal();

    // Its area, design units: the drawing at any opening (base plate from x0 - 6 = 52, the
    // sprocket's top at 552, the base plate's bottom at y0 = 735) and half a line around it.
    static juce::Rectangle<int> area() { return { 48, 548, 302, 192 }; }

    // 0 closed to 1 open; repaints its own area when it changes.
    void setOpening (float opening);
    float opening() const noexcept { return shown; }

    void paint (juce::Graphics& g) override;

private:
    float shown = 0.0f;
};

} // namespace cymbalis::ui
