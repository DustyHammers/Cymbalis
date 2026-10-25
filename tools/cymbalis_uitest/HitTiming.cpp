#include "HitTiming.h"

#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <utility>
#include <vector>

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;
constexpr double kLeadSeconds = 0.6;  // the marks build up to 16 before the timing starts
constexpr double kTimedSeconds = 2.0;
constexpr double kHitEverySeconds = 0.024; // marks last 0.475 s at velocity 0.9: about 20 would be active, 16 are kept
constexpr int kNotes[] = { 60, 62, 64, 61, 63 }; // the default notes: BELL, BOW, EDGE, BELL2, BOW2
constexpr int kVelocity = 114;                    // 0.9

// Times every paint of a component. JUCE draws a component that has a CachedComponentImage
// by calling that image's paint() each time its parent paints it; this one paints the
// component and its children as usual, timed. (Timing paint() and paintOverChildren() of a
// parent would not do: JUCE skips the paint() of a component covered by an opaque one, as
// the editor is.)
struct PaintTimer final : public juce::CachedComponentImage
{
    PaintTimer (juce::Component& c, const std::atomic<bool>& on, std::vector<double>& out,
                std::function<void (juce::Graphics&)> each = {})
        : owner (c), recording (on), times (out), onPaint (std::move (each))
    {
    }
    void paint (juce::Graphics& g) override
    {
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        owner.paintEntireComponent (g, false);
        if (! recording.load())
            return;
        times.push_back (juce::Time::getMillisecondCounterHiRes() - t0);
        if (onPaint)
            onPaint (g);
    }
    bool invalidateAll() override { return true; } // true: the repaint goes on up
    bool invalidate (const juce::Rectangle<int>&) override { return true; }
    void releaseResources() override {}

    juce::Component& owner;
    const std::atomic<bool>& recording;
    std::vector<double>& times;
    std::function<void (juce::Graphics&)> onPaint;
};

// A window whose content, `holder`, holds the editor; every paint of it is timed, and so is
// the hit display's part of it. The area each paint covers (its clip's bounding box) and the
// physical pixel scale are kept too.
struct PaintClock final : public juce::DocumentWindow
{
    // Only the run with the editor puts its window on the desktop.
    explicit PaintClock (bool onDesktop) : DocumentWindow ("CymbalisUiTest hits", juce::Colours::black, 0, onDesktop)
    {
        setUsingNativeTitleBar (true);
        holder.setCachedComponentImage (new PaintTimer (holder, recording, times, [this] (juce::Graphics& g) {
            if (hits != nullptr)
            {
                marksMin = std::min (marksMin, hits->numActive());
                marksSum += hits->numActive();
            }
            const auto clip = g.getClipBounds().getIntersection (holder.getLocalBounds());
            areaSum += (double) clip.getWidth() * (double) clip.getHeight();
            lastClip = clip;
            scale = g.getInternalContext().getPhysicalPixelScaleFactor();
        }));
    }

    void watch (cymbalis::ui::HitLayer* layer)
    {
        hits = layer;
        if (layer != nullptr)
            layer->setCachedComponentImage (new PaintTimer (*layer, recording, hitTimes));
    }

    juce::Component holder;
    std::atomic<bool> recording { false };
    std::vector<double> times, hitTimes;
    const cymbalis::ui::HitLayer* hits = nullptr;
    int marksMin = 1 << 30;
    long marksSum = 0;
    double areaSum = 0.0;
    juce::Rectangle<int> lastClip;
    float scale = 0.0f;
};

template <typename T>
T* findChild (juce::Component& c)
{
    if (auto* t = dynamic_cast<T*> (&c))
        return t;
    for (auto* child : c.getChildren())
        if (auto* t = findChild<T> (*child))
            return t;
    return nullptr;
}

struct Stats
{
    size_t n = 0;
    double mean = 0.0, median = 0.0, worst = 0.0;
};

Stats statsOf (std::vector<double> v)
{
    Stats s;
    s.n = v.size();
    for (double x : v)
    {
        s.mean += x;
        s.worst = std::max (s.worst, x);
    }
    if (s.n > 0)
    {
        s.mean /= (double) s.n;
        std::nth_element (v.begin(), v.begin() + (std::ptrdiff_t) (s.n / 2), v.end());
        s.median = v[s.n / 2];
    }
    return s;
}
} // namespace

struct HitTiming::Run
{
    explicit Run (bool openEditor) : clock (openEditor)
    {
        processor.prepareToPlay (kRate, kBlock);
        if (! openEditor)
            return;
        editor.reset (processor.createEditorIfNeeded());
        editor->setSize (1536, 1024); // the maximum size
        clock.holder.setSize (editor->getWidth(), editor->getHeight());
        clock.holder.addAndMakeVisible (*editor);
        clock.watch (findChild<cymbalis::ui::HitLayer> (*editor));
        clock.setContentNonOwned (&clock.holder, true);
        clock.setTopLeftPosition (40, 40);
        clock.setVisible (true);
    }

    ~Run()
    {
        if (audio.joinable())
            audio.join();
        clock.clearContentComponent();
        if (editor != nullptr)
            clock.holder.removeChildComponent (editor.get());
        editor.reset();
    }

    // The last live paint's region and the whole editor, painted again into an image with the
    // same renderer, `n` times each (the first of each, which may render cached images at the
    // image's scale, is not counted): the cost of painting itself, without the window.
    std::pair<Stats, Stats> paintIntoImage (int n)
    {
        juce::Image image (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true, juce::NativeImageType());
        const auto time = [&] (juce::Rectangle<int> area) {
            std::vector<double> t;
            for (int i = 0; i <= n; ++i)
            {
                juce::Graphics g (image);
                g.reduceClipRegion (area);
                const double t0 = juce::Time::getMillisecondCounterHiRes();
                editor->paintEntireComponent (g, false);
                if (i > 0)
                    t.push_back (juce::Time::getMillisecondCounterHiRes() - t0);
            }
            return statsOf (t);
        };
        return { time (clock.lastClip), time (editor->getLocalBounds()) };
    }

    // Plays the note-ons in real time, block by block, and times processBlock over the
    // timed part. Nothing here allocates once it runs.
    void start()
    {
        audio = std::thread ([this] {
            juce::AudioBuffer<float> buffer (2, kBlock);
            juce::MidiBuffer midi;
            midi.ensureSize (1024);
            const int lead = (int) std::lround (kLeadSeconds * kRate / kBlock);
            const int timed = (int) std::lround (kTimedSeconds * kRate / kBlock);
            blockMs.reserve ((size_t) timed);
            const double hitEvery = kHitEverySeconds * kRate;
            double nextHit = 0.0;
            int hit = 0;
            const auto begin = std::chrono::steady_clock::now();
            for (int b = 0; b < lead + timed; ++b)
            {
                if (b == lead)
                    clock.recording = true;
                midi.clear();
                const double from = (double) b * kBlock;
                for (; nextHit < from + kBlock; nextHit += hitEvery)
                    midi.addEvent (juce::MidiMessage::noteOn (processor.midiChannel(), kNotes[hit++ % 5], (juce::uint8) kVelocity),
                                   (int) (nextHit - from));
                buffer.clear();
                const auto t0 = std::chrono::steady_clock::now();
                processor.processBlock (buffer, midi);
                const auto t1 = std::chrono::steady_clock::now();
                if (b >= lead)
                    blockMs.push_back (std::chrono::duration<double, std::milli> (t1 - t0).count());
                std::this_thread::sleep_until (begin + std::chrono::duration<double> ((double) (b + 1) * kBlock / kRate));
            }
            clock.recording = false;
            done = true;
        });
    }

    CymbalisProcessor processor;
    PaintClock clock;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    std::thread audio;
    std::atomic<bool> done { false };
    std::vector<double> blockMs;
};

HitTiming::HitTiming (std::function<void (int)> f) : finished (std::move (f))
{
    startTimer (50);
}

HitTiming::~HitTiming()
{
    stopTimer();
}

void HitTiming::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    switch (phase)
    {
        case 0: // the MIDI with no editor first
            before = std::make_unique<Run> (false);
            before->start();
            phase = 1;
            return;
        case 1: // then the editor opens; its first paint renders the cached images
            if (! before->done)
                return;
            before->audio.join();
            withEditor = std::make_unique<Run> (true);
            phaseStartMs = now;
            phase = 2;
            return;
        case 2: // timed once JUCE's splash screen has faded out (2 s shown, 2 s fading), so
                // that its repaints are not counted
            if (now - phaseStartMs < 1000.0
                || (findChild<juce::JUCESplashScreen> (*withEditor->editor) != nullptr && now - phaseStartMs < 8000.0))
                return;
            withEditor->start();
            phase = 3;
            return;
        case 3: // then the same MIDI with no editor again
            if (! withEditor->done)
                return;
            withEditor->audio.join();
            {
                const auto [region, whole] = withEditor->paintIntoImage (10);
                regionIntoImageMs = region.median;
                editorIntoImageMs = whole.median;
            }
            withEditor->clock.setVisible (false);
            after = std::make_unique<Run> (false);
            after->start();
            phase = 4;
            return;
        case 4:
        {
            if (! after->done)
                return;
            after->audio.join();
            stopTimer();
            const auto& c = withEditor->clock;
            const auto paints = statsOf (c.times);
            const auto layer = statsOf (c.hitTimes);
            const auto open = statsOf (withEditor->blockMs);
            const auto closedBefore = statsOf (before->blockMs), closedAfter = statsOf (after->blockMs);
            const bool ran = c.hits != nullptr && paints.n > 0 && open.n > 0 && closedBefore.n > 0 && closedAfter.n > 0;
            const double editorArea = (double) c.holder.getWidth() * (double) c.holder.getHeight();
            std::printf ("hits: editor 1536 x 1024, a strike every %.0f ms at velocity 0.9 (notes 60-64), timed over %.0f s\n",
                         1000.0 * kHitEverySeconds, kTimedSeconds);
            std::printf ("  marks active while timed: at least %d, mean %.1f (at most %d)\n", paints.n > 0 ? c.marksMin : 0,
                         paints.n > 0 ? (double) c.marksSum / (double) paints.n : 0.0, cymbalis::ui::HitLayer::maxMarks);
            std::printf ("  editor paint calls: %zu, average %.3f ms, median %.3f ms, worst %.3f ms (target: average under 2 ms) -> %s\n",
                         paints.n, paints.mean, paints.median, paints.worst, paints.mean < 2.0 ? "within target" : "ABOVE TARGET");
            std::printf ("    of which the marks layer: %zu paints, average %.3f ms, median %.3f ms, worst %.3f ms\n", layer.n, layer.mean,
                         layer.median, layer.worst);
            std::printf ("    area per paint: average %.0f %% of the editor (the clip's bounding box), %.2f physical pixels per unit\n",
                         paints.n > 0 ? 100.0 * c.areaSum / (double) paints.n / editorArea : 0.0, (double) c.scale);
            std::printf ("    the last paint's region (%s) painted into an image: median %.3f ms; the whole editor: median %.3f ms\n",
                         c.lastClip.toString().toRawUTF8(), regionIntoImageMs, editorIntoImageMs);
            const auto block = [] (const char* what, const Stats& b) {
                std::printf ("  processBlock, %-18s %zu blocks, average %.4f ms, median %.4f ms, worst %.4f ms\n", what, b.n, b.mean,
                             b.median, b.worst);
            };
            block ("no editor (before):", closedBefore);
            block ("editor open:", open);
            block ("no editor (after):", closedAfter);
            std::fflush (stdout);
            before.reset();
            withEditor.reset();
            after.reset();
            if (finished)
                finished (ran ? 0 : 1);
            return;
        }
        default: return;
    }
}
