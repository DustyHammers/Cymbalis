#include "PluginEditor.h"

CymbalisEditor::CymbalisEditor (CymbalisProcessor& p)
    : AudioProcessorEditor (&p)
{
    setSize (480, 240);
}

void CymbalisEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0d0d0d));

    g.setColour (juce::Colour (0xffb8965a));
    g.setFont (juce::Font (28.0f));
    g.drawText ("CYMBALIS", getLocalBounds().withTrimmedBottom (40), juce::Justification::centred);

    g.setColour (juce::Colour (0xffc8c8c8));
    g.setFont (juce::Font (13.0f));
    g.drawText ("v" JucePlugin_VersionString "  -  build skeleton",
                getLocalBounds().withTrimmedTop (60), juce::Justification::centred);
}
