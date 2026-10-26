#include "HitTiming.h"

#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <utility>

#include <pthread.h>
#include <sys/resource.h>
#include <time.h>
#if JUCE_MAC
 #include <mach/mach.h>
 #include <mach/mach_time.h>
 #include <mach/thread_policy.h>
 #include <sys/sysctl.h>
#elif JUCE_LINUX
 #include <sched.h>
#endif

namespace
{
constexpr double kRate = 44100.0;
constexpr int kBlocks[] = { 256, 128 };
constexpr double kLeadSeconds = 1.0;  // the cymbal is built and the marks build up to 16 before the timing starts
constexpr int kRounds = 3; // each case runs this many times, the cases in turn
constexpr int kFeedBlock = 32; // the second processor's blocks, on the message thread

// One stream of note-ons: its notes in turn, at one velocity, one every `everySeconds`.
struct Stream
{
    std::vector<int> notes;
    int velocity;
    double everySeconds;
};

struct Pattern
{
    const char* name;
    const char* description;
    double timedSeconds;
    std::vector<Stream> streams;
    double averageTargetMs, worstTargetMs; // for the editor's paint calls (0: none)
};

Pattern patternFor (HitTiming::Mode mode)
{
    if (mode == HitTiming::Mode::motion)
        return { "motion", "EDGE at velocity 1.0 every 0.5 s and BOW at 0.8 every 0.125 s", 4.0,
                 { { { 64 }, 127, 0.5 }, { { 62 }, 102, 0.125 } }, 4.0, 8.0 };
    // The default notes BELL, BOW, EDGE, BELL2, BOW2 at 0.9: marks last 0.475 s, so about 20
    // would be active; 16 are kept.
    return { "hits", "a strike every 24 ms at velocity 0.9 (notes 60-64)", 3.0, { { { 60, 62, 64, 61, 63 }, 114, 0.024 } }, 2.0, 0.0 };
}

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
    // Only the runs with an editor put this window on the desktop (Run), with the flags the
    // plugin wrappers would use.
    PaintClock() : DocumentWindow ("CymbalisUiTest hits", juce::Colours::black, 0, false)
    {
        setUsingNativeTitleBar (true);
        holder.setCachedComponentImage (new PaintTimer (holder, recording, times, [this] (juce::Graphics& g) {
            if (hits != nullptr)
            {
                marksMin = std::min (marksMin, hits->numActive());
                marksSum += hits->numActive();
                movingPaints += hits->motion().moving() ? 1 : 0;
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
    int movingPaints = 0; // paints with the cymbal moving
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
    double mean = 0.0, median = 0.0, p99 = 0.0, worst = 0.0;
};

Stats statsOf (std::vector<double> v)
{
    Stats s;
    s.n = v.size();
    if (s.n == 0)
        return s;
    std::sort (v.begin(), v.end());
    for (double x : v)
        s.mean += x;
    s.mean /= (double) s.n;
    s.median = v[s.n / 2];
    s.p99 = v[(size_t) std::ceil (0.99 * (double) s.n) - 1]; // nearest rank
    s.worst = v.back();
    return s;
}

// CPU time of the calling thread, and of the whole process, in ms.
double threadCpuMs() noexcept
{
    timespec t {};
    clock_gettime (CLOCK_THREAD_CPUTIME_ID, &t);
    return (double) t.tv_sec * 1.0e3 + (double) t.tv_nsec * 1.0e-6;
}

double processCpuMs() noexcept
{
    rusage r {};
    getrusage (RUSAGE_SELF, &r);
    const auto ms = [] (const timeval& t) { return (double) t.tv_sec * 1.0e3 + (double) t.tv_usec * 1.0e-3; };
    return ms (r.ru_utime) + ms (r.ru_stime);
}

// Makes the calling thread a real-time thread, as a host's audio thread is: on macOS a
// time-constraint thread with the block as its period (as Core Audio's I/O thread), on
// Linux SCHED_FIFO. False if the system refuses.
bool makeRealtime (double periodMs) noexcept
{
#if JUCE_MAC
    mach_timebase_info_data_t base {};
    mach_timebase_info (&base);
    const auto ticks = [&] (double ms) { return (uint32_t) (ms * 1.0e6 * (double) base.denom / (double) base.numer); };
    thread_time_constraint_policy_data_t policy {};
    policy.period = ticks (periodMs);
    policy.computation = ticks (0.5 * periodMs);
    policy.constraint = ticks (periodMs);
    policy.preemptible = 1;
    return thread_policy_set (pthread_mach_thread_np (pthread_self()), THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t) &policy,
                              THREAD_TIME_CONSTRAINT_POLICY_COUNT)
           == KERN_SUCCESS;
#elif JUCE_LINUX
    juce::ignoreUnused (periodMs);
    sched_param param {};
    param.sched_priority = sched_get_priority_max (SCHED_FIFO) - 1;
    return pthread_setschedparam (pthread_self(), SCHED_FIFO, &param) == 0;
#else
    juce::ignoreUnused (periodMs);
    return false;
#endif
}

// WindowServer's CPU time so far in ms, from ps (macOS); -1 where there is none.
double windowServerCpuMs()
{
#if JUCE_MAC
    juce::ChildProcess ps;
    if (! ps.start (juce::StringArray { "/bin/ps", "-axco", "time=,comm=" }))
        return -1.0;
    for (const auto& line : juce::StringArray::fromLines (ps.readAllProcessOutput()))
    {
        const auto t = line.trim();
        if (! t.endsWith ("WindowServer"))
            continue;
        double seconds = 0.0; // [[hours:]minutes:]seconds.hundredths
        for (const auto& field : juce::StringArray::fromTokens (t.upToFirstOccurrenceOf (" ", false, false), ":", ""))
            seconds = seconds * 60.0 + field.getDoubleValue();
        return seconds * 1.0e3;
    }
#endif
    return -1.0;
}

juce::String cpuSummary()
{
    juce::String s;
    s << juce::SystemStats::getCpuModel() << ", " << juce::SystemStats::getNumCpus() << " logical cores ("
      << juce::SystemStats::getNumPhysicalCpus() << " physical)";
#if JUCE_MAC
    const auto value = [] (const char* name) {
        int64_t v = 0;
        size_t size = sizeof (v);
        return sysctlbyname (name, &v, &size, nullptr, 0) == 0 ? v : (int64_t) -1;
    };
    const auto performance = value ("hw.perflevel0.logicalcpu"), efficiency = value ("hw.perflevel1.logicalcpu");
    if (performance > 0)
        s << ", " << (int) performance << " performance";
    if (efficiency > 0)
        s << ", " << (int) efficiency << " efficiency";
    s << ", cache line " << (int) value ("hw.cachelinesize") << " bytes";
#endif
    return s;
}


const char* caseName (HitTiming::Editor e)
{
    switch (e)
    {
        case HitTiming::Editor::none: return "editor closed";
        case HitTiming::Editor::own: return "editor open, hit display";
        case HitTiming::Editor::otherFed: return "editor open, dummy paint";
        case HitTiming::Editor::otherIdle: return "editor open, no hits";
    }
    return "";
}
} // namespace

struct HitTiming::Result
{
    Editor editor = Editor::none;
    int block = 0;
    bool realtime = false, wasRealtime = false;
    std::vector<double> blockMs, blockCpuMs;                     // the timed blocks
    double restOfProcess = 0.0, windowServer = -1.0, feed = 0.0; // % of one core
    std::vector<double> paintMs, layerMs;                        // the editor's paint calls, and the hit display's
    double areaShareSum = 0.0;                                   // the paints' areas, as shares of the editor
    long marksSum = 0;
    int marksMin = 0, movingPaints = 0;
    double regionIntoImageMs = -1.0, editorIntoImageMs = -1.0;
    int paintsAfter = -1; // the hit display's paints in the second from 0.6 s after the last hit
    int timerAfter = -1;  // whether its own timer still ran at the end of that second (1) or not (0)
    double cpuAfter = 0.0; // the process's CPU over that second, % of one core
    juce::Rectangle<int> lastClip;
    float scale = 0.0f;
};

struct HitTiming::Run final : private juce::Timer
{
    Run (Editor kind, int blockSize, bool rt, Pattern p) : editorKind (kind), block (blockSize), realtime (rt), pattern (std::move (p))
    {
        processor.prepareToPlay (kRate, block);
        if (kind == Editor::none)
            return;
        CymbalisProcessor* shown = &processor;
        if (kind != Editor::own)
        {
            other = std::make_unique<CymbalisProcessor>();
            other->prepareToPlay (kRate, kFeedBlock);
            shown = other.get();
        }
        editor.reset (shown->createEditorIfNeeded());
        editor->setSize (1536, 1024); // the maximum size
        clock.holder.setSize (editor->getWidth(), editor->getHeight());
        clock.holder.addAndMakeVisible (*editor);
        clock.watch (findChild<cymbalis::ui::HitLayer> (*editor));
        clock.setContentNonOwned (&clock.holder, true);
        // As detail::PluginUtilities::getDesktopFlags: asynchronous CoreGraphics in a layer-backed
        // view unless the editor asks for synchronous drawing.
        clock.addToDesktop (clock.getDesktopWindowStyleFlags()
                            | (editor->wantsLayerBackedView() ? 0 : juce::ComponentPeer::windowRequiresSynchronousCoreGraphicsRendering));
        clock.setTopLeftPosition (40, 40);
        clock.setVisible (true);
    }

    ~Run() override
    {
        stopTimer();
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

    // Plays the note-ons in real time, block by block, and times processBlock over the timed
    // part: wall time and the thread's CPU time. Nothing here allocates once it runs. The
    // second processor, if fed, gets the same hits on the message thread.
    void start()
    {
        begin = std::chrono::steady_clock::now();
        if (editorKind == Editor::otherFed)
        {
            feedNext.assign (pattern.streams.size(), 0);
            feedBuffer.setSize (2, kFeedBlock);
            feedMidi.ensureSize (1024);
            startTimerHz (30);
        }
        audio = std::thread ([this] {
            const double budgetMs = 1000.0 * block / kRate;
            if (realtime)
                wasRealtime = makeRealtime (budgetMs);
            juce::AudioBuffer<float> buffer (2, block);
            juce::MidiBuffer midi;
            midi.ensureSize (1024);
            const int lead = (int) std::lround (kLeadSeconds * kRate / block);
            const int timed = (int) std::lround (pattern.timedSeconds * kRate / block);
            blockMs.reserve ((size_t) timed);
            blockCpuMs.reserve ((size_t) timed);
            std::vector<double> nextHit (pattern.streams.size(), 0.0); // samples
            std::vector<size_t> hits (pattern.streams.size(), 0);
            double processStart = 0.0, threadStart = 0.0;
            auto timedStart = begin;
            for (int b = 0; b < lead + timed; ++b)
            {
                if (b == lead)
                {
                    processStart = processCpuMs();
                    threadStart = threadCpuMs();
                    timedStart = std::chrono::steady_clock::now();
                    clock.recording = true;
                }
                midi.clear();
                const double from = (double) b * block;
                for (size_t s = 0; s < pattern.streams.size(); ++s)
                {
                    const auto& st = pattern.streams[s];
                    for (; nextHit[s] < from + block; nextHit[s] += st.everySeconds * kRate)
                        midi.addEvent (juce::MidiMessage::noteOn (processor.midiChannel(), st.notes[hits[s]++ % st.notes.size()],
                                                                  (juce::uint8) st.velocity),
                                       (int) (nextHit[s] - from));
                }
                buffer.clear();
                const double c0 = threadCpuMs();
                const auto t0 = std::chrono::steady_clock::now();
                processor.processBlock (buffer, midi);
                const auto t1 = std::chrono::steady_clock::now();
                const double c1 = threadCpuMs();
                if (b >= lead)
                {
                    blockMs.push_back (std::chrono::duration<double, std::milli> (t1 - t0).count());
                    blockCpuMs.push_back (c1 - c0);
                }
                std::this_thread::sleep_until (begin + std::chrono::duration<double> ((double) (b + 1) * block / kRate));
            }
            clock.recording = false;
            const double timedMs = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - timedStart).count();
            const double threadMs = threadCpuMs() - threadStart;
            restOfProcess = 100.0 * (processCpuMs() - processStart - threadMs) / timedMs;
            timedWallMs = timedMs;
            done = true;
        });
    }

    // The second processor's hits, due by the same schedule, on the message thread.
    void timerCallback() override
    {
        const double elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - begin).count();
        feedMidi.clear();
        int at = 0;
        for (size_t s = 0; s < pattern.streams.size(); ++s)
        {
            const auto& st = pattern.streams[s];
            for (; (double) feedNext[s] * st.everySeconds <= elapsed; ++feedNext[s])
                feedMidi.addEvent (juce::MidiMessage::noteOn (other->midiChannel(), st.notes[feedNext[s] % st.notes.size()], (juce::uint8) st.velocity),
                                   std::min (at++, kFeedBlock - 1));
        }
        feedBuffer.clear();
        const bool timed = clock.recording.load();
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        other->processBlock (feedBuffer, feedMidi);
        if (timed)
            feedMs += juce::Time::getMillisecondCounterHiRes() - t0;
    }

    Editor editorKind;
    int block;
    bool realtime;
    const Pattern pattern;
    CymbalisProcessor processor;
    std::unique_ptr<CymbalisProcessor> other;
    PaintClock clock;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    std::thread audio;
    std::chrono::steady_clock::time_point begin;
    std::atomic<bool> done { false };
    bool wasRealtime = false;
    std::vector<double> blockMs, blockCpuMs;
    double restOfProcess = 0.0, timedWallMs = 0.0;

    juce::AudioBuffer<float> feedBuffer;
    juce::MidiBuffer feedMidi;
    std::vector<size_t> feedNext; // per stream
    double feedMs = 0.0;
};

HitTiming::HitTiming (Mode m, std::function<void (int)> f) : mode (m), finished (std::move (f))
{
    // Each block size, at normal priority and then real-time: the four cases in turn, three
    // times, then the editor closed once more.
    for (const bool realtime : { false, true })
        for (const int block : kBlocks)
        {
            for (int round = 0; round < kRounds; ++round)
                for (const auto e : { Editor::none, Editor::own, Editor::otherFed, Editor::otherIdle })
                    steps.push_back ({ e, block, realtime, round == 0 });
            steps.push_back ({ Editor::none, block, realtime, false });
        }
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
        case 0: // the next run; an editor's first paint renders its cached images
        {
            const auto& s = steps[next];
            run = std::make_unique<Run> (s.editor, s.block, s.realtime, patternFor (mode));
            phaseStartMs = now;
            phase = 1;
            return;
        }
        case 1: // with an editor, timed once it has painted and JUCE's splash screen has gone
                // (shown for 2 s, then fading for 2 s, when the first editor opens)
            if (run->editor != nullptr
                && (now - phaseStartMs < 1000.0
                    || (findChild<juce::JUCESplashScreen> (*run->editor) != nullptr && now - phaseStartMs < 8000.0)))
                return;
            phaseStartMs = now;
            windowServerStartMs = windowServerCpuMs();
            run->start();
            phase = 2;
            return;
        case 2:
        {
            if (! run->done)
                return;
            run->audio.join();
            const double windowServerMs = windowServerCpuMs();
            const auto& s = steps[next];
            Result r;
            r.editor = s.editor;
            r.block = s.block;
            r.realtime = s.realtime;
            r.wasRealtime = run->wasRealtime;
            r.blockMs = run->blockMs;
            r.blockCpuMs = run->blockCpuMs;
            r.restOfProcess = run->restOfProcess;
            if (windowServerStartMs >= 0.0 && windowServerMs >= 0.0)
                r.windowServer = 100.0 * (windowServerMs - windowServerStartMs) / (now - phaseStartMs);
            r.feed = run->timedWallMs > 0.0 ? 100.0 * run->feedMs / run->timedWallMs : 0.0;
            const auto& c = run->clock;
            r.paintMs = c.times;
            r.layerMs = c.hitTimes;
            const double editorArea = (double) c.holder.getWidth() * (double) c.holder.getHeight();
            r.areaShareSum = editorArea > 0.0 ? c.areaSum / editorArea : 0.0;
            r.marksSum = c.marksSum;
            r.movingPaints = c.movingPaints;
            r.marksMin = ! c.times.empty() && c.hits != nullptr ? c.marksMin : 0;
            r.lastClip = c.lastClip;
            r.scale = c.scale;
            if (s.first && s.editor == Editor::own && ! s.realtime && s.block == kBlocks[0] && ! c.times.empty())
            {
                const auto [region, whole] = run->paintIntoImage (10);
                r.regionIntoImageMs = region.median;
                r.editorIntoImageMs = whole.median;
            }
            results.push_back (std::move (r));
            // The hit display after its last hit (hits; with the motion, the cymbal may still move).
            if (s.first && s.editor == Editor::own && mode == Mode::hits)
            {
                phaseStartMs = now;
                phase = 3;
                return;
            }
            finishRun();
            return;
        }
        case 3: // the hit display after the last hit: its marks end within 0.5 s, then nothing
                // should paint; count over the second from 0.6 s after
            if (now - phaseStartMs < 600.0)
                return;
            lingerPaints = run->clock.hitTimes.size();
            lingerCpuMs = processCpuMs();
            run->clock.recording = true;
            phaseStartMs = now;
            phase = 4;
            return;
        case 4:
            if (now - phaseStartMs < 1000.0)
                return;
            run->clock.recording = false;
            if (run->clock.hits != nullptr)
                // The hit layer's timer is a private base; a C-style cast reaches it (as the
                // standard allows for an inaccessible base class) to read whether it runs.
                results.back().timerAfter = ((const juce::Timer*) run->clock.hits)->isTimerRunning() ? 1 : 0;
            results.back().paintsAfter = (int) (run->clock.hitTimes.size() - lingerPaints);
            results.back().cpuAfter = 100.0 * (processCpuMs() - lingerCpuMs) / (now - phaseStartMs);
            finishRun();
            return;
        case 5:
            stopTimer();
            report();
            return;
        default: return;
    }
}

void HitTiming::finishRun()
{
    run->clock.setVisible (false);
    run.reset();
    phase = ++next < steps.size() ? 0 : 5;
}

void HitTiming::report()
{
    const auto pattern = patternFor (mode);
    std::printf ("%s: %s, %.1f kHz, editor 1536 x 1024, %d runs of %.0f s per case\n", pattern.name, pattern.description, kRate / 1000.0,
                 kRounds, pattern.timedSeconds);
    std::printf ("  CPU: %s\n", cpuSummary().toRawUTF8());
    bool ran = true;
    const auto percent = [] (double a, double b) { return b > 0.0 ? 100.0 * (a - b) / b : 0.0; };
    for (size_t i = 0; i < results.size();)
    {
        const auto& first = results[i];
        size_t end = i;
        while (end < results.size() && results[end].block == first.block && results[end].realtime == first.realtime)
            ++end;
        const double budget = 1000.0 * first.block / kRate;
        std::printf ("\n  audio thread %s, %d-sample blocks (budget %.3f ms)\n",
                     ! first.realtime ? "at normal priority" : first.wasRealtime ? "real-time" : "real-time REFUSED (normal priority)",
                     first.block, budget);
        std::printf ("    %-26s %s\n", "", "processBlock over all runs (ms)     worst of each run (ms)       its CPU (ms)    rest of the   WindowServer");
        std::printf ("    %-26s %s\n", "case", " median     p99   worst / budget                                 median     p99   process");
        struct Case
        {
            Stats wall, cpu, runWorsts;
            std::vector<double> worsts;
            double rest = 0.0, windowServer = 0.0;
            int windowServerRuns = 0;
        };
        std::array<Case, 4> cases;
        for (int e = 0; e < 4; ++e)
        {
            std::vector<double> wall, cpu;
            auto& k = cases[(size_t) e];
            int runs = 0;
            for (size_t j = i; j < end; ++j)
            {
                const auto& r = results[j];
                if ((int) r.editor != e)
                    continue;
                ran = ran && ! r.blockMs.empty();
                wall.insert (wall.end(), r.blockMs.begin(), r.blockMs.end());
                cpu.insert (cpu.end(), r.blockCpuMs.begin(), r.blockCpuMs.end());
                k.worsts.push_back (statsOf (r.blockMs).worst);
                k.rest += r.restOfProcess;
                if (r.windowServer >= 0.0)
                {
                    k.windowServer += r.windowServer;
                    ++k.windowServerRuns;
                }
                ++runs;
            }
            k.wall = statsOf (wall);
            k.cpu = statsOf (cpu);
            k.runWorsts = statsOf (k.worsts);
            k.rest /= juce::jmax (1, runs);
            char worsts[64] = "";
            for (size_t w = 0; w < 4; ++w)
            {
                char one[16] = "        ";
                if (w < k.worsts.size())
                    std::snprintf (one, sizeof (one), " %7.3f", k.worsts[w]);
                std::strncat (worsts, one, sizeof (worsts) - std::strlen (worsts) - 1);
            }
            char ws[32] = "-";
            if (k.windowServerRuns > 0)
                std::snprintf (ws, sizeof (ws), "%5.1f %%", k.windowServer / k.windowServerRuns);
            std::printf ("    %-26s %7.3f %7.3f %7.3f %6.1f %%  %s   %7.3f %7.3f   %5.1f %%       %s\n", caseName ((Editor) e), k.wall.median,
                         k.wall.p99, k.wall.worst, 100.0 * k.wall.worst / budget, worsts, k.cpu.median, k.cpu.p99, k.rest, ws);
        }
        const auto& closed = cases[(size_t) Editor::none];
        const auto& open = cases[(size_t) Editor::own];
        std::printf ("    editor open with the hit display against closed: median %+.0f %%, p99 %+.0f %%, worst %+.0f %% "
                     "(the runs' worsts, median of them: %+.0f %%)\n",
                     percent (open.wall.median, closed.wall.median), percent (open.wall.p99, closed.wall.p99),
                     percent (open.wall.worst, closed.wall.worst), percent (open.runWorsts.median, closed.runWorsts.median));
        std::printf ("    (CPU: %% of one core; the rest of the process includes the editor's painting and, with the dummy, feeding its processor)\n");
        for (const auto e : { Editor::own, Editor::otherFed })
        {
            std::vector<double> paints, layer;
            double areaShares = 0.0, feed = 0.0;
            long marks = 0;
            int runs = 0, moving = 0;
            const Result* checked = nullptr;
            for (size_t j = i; j < end; ++j)
            {
                const auto& r = results[j];
                if (r.editor != e)
                    continue;
                ran = ran && ! r.paintMs.empty() && ! r.layerMs.empty();
                paints.insert (paints.end(), r.paintMs.begin(), r.paintMs.end());
                layer.insert (layer.end(), r.layerMs.begin(), r.layerMs.end());
                areaShares += r.areaShareSum;
                marks += r.marksSum;
                moving += r.movingPaints;
                feed += r.feed;
                ++runs;
                if (r.paintsAfter >= 0)
                    checked = &r;
            }
            const auto p = statsOf (paints), l = statsOf (layer);
            std::printf ("    %-26s paints %3zu, average %.3f ms, worst %.3f ms; marks layer average %.3f ms; %3.0f %% of the editor per paint; marks mean %.1f",
                         caseName (e), p.n, p.mean, p.worst, l.mean, p.n > 0 ? 100.0 * areaShares / (double) p.n : 0.0,
                         p.n > 0 ? (double) marks / (double) p.n : 0.0);
            if (mode == Mode::motion)
                std::printf ("; with the cymbal moving %d", moving);
            if (e == Editor::otherFed)
                std::printf ("; feeding its processor %.1f %%", feed / juce::jmax (1, runs));
            if (checked != nullptr)
                std::printf ("; after the last hit: %d paints in 1 s, its timer %s, process CPU %.1f %%", checked->paintsAfter,
                             checked->timerAfter == 0 ? "stopped" : checked->timerAfter == 1 ? "STILL RUNNING" : "unknown", checked->cpuAfter);
            std::printf ("\n");
            if (e == Editor::own && ! first.realtime && first.block == kBlocks[0])
            {
                if (mode == Mode::hits)
                    ran = ran && checked != nullptr && checked->timerAfter == 0;
                const bool within = p.mean < pattern.averageTargetMs && (pattern.worstTargetMs <= 0.0 || p.worst < pattern.worstTargetMs);
                char target[64];
                if (pattern.worstTargetMs > 0.0)
                    std::snprintf (target, sizeof (target), "average under %.0f ms, worst under %.0f ms", pattern.averageTargetMs, pattern.worstTargetMs);
                else
                    std::snprintf (target, sizeof (target), "average under %.0f ms", pattern.averageTargetMs);
                std::printf ("    editor paint calls: average %.3f ms, worst %.3f ms (target: %s) -> %s\n", p.mean, p.worst, target,
                             within ? "within target" : "ABOVE TARGET");
                if (checked != nullptr && checked->regionIntoImageMs >= 0.0)
                    std::printf ("    the last paint's region (%s) painted into an image: median %.3f ms; the whole editor: median %.3f ms; %.2f physical pixels per unit\n",
                                 checked->lastClip.toString().toRawUTF8(), checked->regionIntoImageMs, checked->editorIntoImageMs,
                                 (double) checked->scale);
            }
        }
        i = end;
    }
    std::fflush (stdout);
    results.clear();
    if (finished)
        finished (ran ? 0 : 1);
}
