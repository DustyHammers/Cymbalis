#pragma once

// The "hits" mode of CymbalisUiTest (docs/UI_SPEC.md section 10). Unlike the other steps it
// runs Cymbalis's own processor and editor, compiled into this tool, so that it can time
// them: the editor at 1536 x 1024 in a window, an audio thread that plays note-ons in real
// time so that 16 marks are active for 2 s, the editor's paint calls timed, and processBlock
// timed for the same MIDI with no editor, with the editor open, and with no editor again.
// The timing starts once JUCE's splash screen, shown when an editor opens, has gone.

#include <juce_events/juce_events.h>

#include <functional>
#include <memory>

class HitTiming final : private juce::Timer
{
public:
    explicit HitTiming (std::function<void (int exitCode)> finished);
    ~HitTiming() override;

private:
    struct Run;
    void timerCallback() override;

    std::function<void (int)> finished;
    std::unique_ptr<Run> before, withEditor, after;
    double regionIntoImageMs = 0.0, editorIntoImageMs = 0.0; // medians
    int phase = 0;
    double phaseStartMs = 0.0;
};
