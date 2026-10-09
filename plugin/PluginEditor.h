#pragma once

#include "PluginProcessor.h"

// Placeholder window only — the real UI is built in Phase 4 after a mockup is approved.
class CymbalisEditor final : public juce::AudioProcessorEditor
{
public:
    explicit CymbalisEditor (CymbalisProcessor&);
    ~CymbalisEditor() override = default;

    void paint (juce::Graphics&) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CymbalisEditor)
};
