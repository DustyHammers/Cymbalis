#pragma once

// The "hits" and "motion" modes of CymbalisUiTest (docs/UI_SPEC.md section 10). Unlike the
// other steps they run Cymbalis's own processor and editor, compiled into this tool, so that
// they can time them: the editor at 1536 x 1024 in a window, an audio thread that plays
// note-ons in real time, the editor's paint calls timed, and processBlock timed for the same
// MIDI with no editor, with the editor open, and with no editor again. The timing starts once
// JUCE's splash screen, shown when an editor opens, has gone.
//
//   hits    a strike every 24 ms at velocity 0.9 on notes 60-64 in turn, 16 marks active
//           for 2 s (the hit display)
//   motion  EDGE at velocity 1.0 every 0.5 s and BOW at 0.8 every 0.125 s for 4 s (the
//           cymbal moving and marks running together)

#include <juce_events/juce_events.h>

#include <functional>
#include <memory>

class HitTiming final : private juce::Timer
{
public:
    enum class Mode
    {
        hits,
        motion
    };
    HitTiming (Mode mode, std::function<void (int exitCode)> finished);
    ~HitTiming() override;

private:
    struct Run;
    void timerCallback() override;

    Mode mode;
    std::function<void (int)> finished;
    std::unique_ptr<Run> before, withEditor, after;
    double regionIntoImageMs = 0.0, editorIntoImageMs = 0.0; // medians
    int phase = 0;
    double phaseStartMs = 0.0;
};
