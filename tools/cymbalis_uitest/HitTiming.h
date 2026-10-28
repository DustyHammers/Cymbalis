#pragma once

// The "hits" mode of CymbalisUiTest (docs/UI_SPEC.md sections 10 and 11). Unlike the other
// steps it runs Cymbalis's own processor and editor, compiled into this tool, so that it can
// time them. An audio thread plays note-ons in real time, a strike every 24 ms so that 16 hits
// move the cymbal, and processBlock is timed at 44.1 kHz with 256- and 128-sample blocks, with
// the audio thread at normal priority and as a real-time thread (as a host's audio thread is),
// in three cases:
//
//   editor closed           no editor
//   editor open, idle       the editor of a second processor, given no hits: open, nothing moves
//   editor open, hits       the editor at 1536 x 1024 in a window, the wireframe cymbal moving
//
// Each case runs three times, the cases in turn, and the editor closed once more at the end.
// Then the three cases run once more with HAMMERED THIN RIDE selected, once with SWISH and once
// with RAVEN, the heaviest cymbals to draw (their dented meshes), at normal priority with
// 256-sample blocks.
// For each case: processBlock's median and 99th percentile over all its runs, each run's
// worst block (and the worst against the block's time budget), the audio thread's CPU time
// per block, the CPU time of the rest of the process, and WindowServer's (macOS); then the
// editor open with hits against the editor closed. With an editor open: whether OpenGL draws
// the cymbal, the editor's paint calls (on the message thread) and the cymbal's OpenGL frames
// (their number, rate and render-thread time). Once, after the last hit, the run waits until
// every hit is older than 6 s and then counts, over one second, the frames, the paints and the
// hit updates (the hit tracker's ticks): all should be none. The editor's window is drawn as
// the plugin wrappers draw it (AudioProcessorEditor::wantsLayerBackedView). The timing starts
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
        none, // no editor
        idle, // the editor of a second processor, given no hits
        hits  // the editor of the processor that plays
    };
    struct Result;

private:
    struct Run;
    struct Step
    {
        Editor editor;
        int block;
        bool realtime;
        bool checkAfter; // after the last hit, count the frames, paints and hit updates
        int preset = -1; // the preset selected (cymbalis::kPresets), or -1: the default
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
    int lingerFrames = 0, lingerUpdates = 0;
    double lingerCpuMs = 0.0;
};
