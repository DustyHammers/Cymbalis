#pragma once

// The "hits" mode of CymbalisUiTest (docs/UI_SPEC.md section 10). Unlike the other steps it
// runs Cymbalis's own processor and editor, compiled into this tool, so that it can time
// them. An audio thread plays note-ons in real time, a strike every 24 ms so that 16 marks
// are active, and processBlock is timed at 44.1 kHz with 256- and 128-sample blocks, with the
// audio thread at normal priority and as a real-time thread (as a host's audio thread is),
// in four cases:
//
//   editor closed              no editor
//   editor open, hit display   the editor at 1536 x 1024 in a window, its hit display running
//   editor open, dummy paint   instead, the editor of a second processor that this tool feeds
//                              the same hits on the message thread: the same painting,
//                              sharing nothing with the processor that plays
//   editor open, no hits       that second editor, given no hits
//
// Each case runs three times, the cases in turn, and the editor closed once more at the end.
// For each case: processBlock's median and 99th percentile over all its runs, each run's
// worst block (and the worst against the block's time budget), the audio thread's CPU time
// per block, the CPU time of the rest of the process, and WindowServer's (macOS); then the
// editor open with the hit display against the editor closed. The editors' paint calls are
// timed too, and after the hit display's last hit, its paints in the following second are
// counted and whether its own timer still runs is read. The editor's window is drawn as the
// plugin wrappers draw it (AudioProcessorEditor::wantsLayerBackedView). The timing starts
// once JUCE's splash screen, shown when the first editor opens, has gone.

#include <juce_events/juce_events.h>

#include <functional>
#include <memory>
#include <vector>

class HitTiming final : private juce::Timer
{
public:
    explicit HitTiming (std::function<void (int exitCode)> finished);
    ~HitTiming() override;

    enum class Editor
    {
        none,      // no editor
        own,       // the editor of the processor that plays
        otherFed,  // the editor of a second processor, fed the same hits
        otherIdle  // the editor of a second processor, given no hits
    };
    struct Result;

private:
    struct Run;
    struct Step
    {
        Editor editor;
        int block;
        bool realtime;
        bool checkAfter; // after the last hit, count the hit display's paints and read its timer
    };

    void timerCallback() override;
    void finishRun();
    void report();

    std::function<void (int)> finished;
    std::vector<Step> steps;
    std::vector<Result> results;
    std::unique_ptr<Run> run;
    size_t next = 0;
    int phase = 0;
    double phaseStartMs = 0.0;
    double windowServerStartMs = -1.0;
    size_t lingerPaints = 0;
    double lingerCpuMs = 0.0;
};
