#include "HitTiming.h"

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ui/CymbalView.h"

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
constexpr double kLeadSeconds = 1.0;  // the hits build up to 16 before the timing starts
constexpr double kTimedSeconds = 3.0;
constexpr int kRounds = 3; // each case runs this many times, the cases in turn
constexpr double kHitEverySeconds = 0.024; // the 16 newest hits are kept
constexpr int kNotes[] = { 60, 62, 64, 61, 63 }; // the default notes: BELL, BOW, EDGE, BELL2, BOW2
constexpr int kVelocity = 114;                    // 0.9
constexpr double kGlStartMs = 4000.0;            // OpenGL has 3 s to start (CymbalView), then the still drawing
constexpr double kQuietAfterMs = 6500.0;         // every hit is older than 6 s by then

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

// A window whose content, `holder`, holds the editor; every paint of it is timed, with the
// area it covers (its clip's bounding box).
struct PaintClock final : public juce::DocumentWindow
{
    // Only the runs with an editor put this window on the desktop (Run), with the flags the
    // plugin wrappers would use.
    PaintClock() : DocumentWindow ("CymbalisUiTest hits", juce::Colours::black, 0, false)
    {
        setUsingNativeTitleBar (true);
        holder.setCachedComponentImage (new PaintTimer (holder, recording, times, [this] (juce::Graphics& g) {
            const auto clip = g.getClipBounds().getIntersection (holder.getLocalBounds());
            areaSum += (double) clip.getWidth() * (double) clip.getHeight();
        }));
    }

    juce::Component holder;
    std::atomic<bool> recording { false };
    std::vector<double> times;
    double areaSum = 0.0;
    cymbalis::ui::CymbalView* view = nullptr; // the editor's cymbal
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
        case HitTiming::Editor::idle: return "editor open, idle";
        case HitTiming::Editor::hits: return "editor open, hits";
    }
    return "";
}
} // namespace

struct HitTiming::Result
{
    Editor editor = Editor::none;
    int block = 0, preset = -1;
    bool realtime = false, wasRealtime = false;
    std::vector<double> blockMs, blockCpuMs;      // the timed blocks
    double restOfProcess = 0.0, windowServer = -1.0; // % of one core
    std::vector<double> paintMs;                  // the editor's paint calls
    double areaShareSum = 0.0;                    // the paints' areas, as shares of the editor
    bool openGL = false;                          // OpenGL drew the cymbal
    juce::String renderer;                        // its renderer
    int meshSegments = 0, segmentsBefore = 0, surfaceSegments = 0, dents = 0; // its geometry
    double buildMs = 0.0;                         // and how long that took to build
    cymbalis::ui::CymbalView::Stats gl;           // its frames over the timed part
    double glWallMs = 0.0;                        // the time they were counted over
    long hitsSum = 0;                             // the hits it showed, sampled every 50 ms
    int hitSamples = 0, hitsMin = 0;
    int framesAfter = -1, paintsAfter = -1, updatesAfter = -1; // in the second once every hit is over 6 s old
    double cpuAfter = 0.0;                        // the process's CPU over that second, % of one core
};

struct HitTiming::Run
{
    Run (Editor kind, int blockSize, bool rt, int preset) : editorKind (kind), block (blockSize), realtime (rt)
    {
        processor.prepareToPlay (kRate, block);
        if (preset >= 0)
            processor.setPreset (preset);
        if (kind == Editor::none)
            return;
        CymbalisProcessor* shown = &processor;
        if (kind == Editor::idle)
        {
            other = std::make_unique<CymbalisProcessor>();
            other->prepareToPlay (kRate, block);
            if (preset >= 0)
                other->setPreset (preset);
            shown = other.get();
        }
        editor.reset (shown->createEditorIfNeeded());
        editor->setSize (1536, 1024); // the maximum size
        clock.holder.setSize (editor->getWidth(), editor->getHeight());
        clock.holder.addAndMakeVisible (*editor);
        clock.view = findChild<cymbalis::ui::CymbalView> (*editor);
        clock.setContentNonOwned (&clock.holder, true);
        // As detail::PluginUtilities::getDesktopFlags: asynchronous CoreGraphics in a layer-backed
        // view unless the editor asks for synchronous drawing.
        clock.addToDesktop (clock.getDesktopWindowStyleFlags()
                            | (editor->wantsLayerBackedView() ? 0 : juce::ComponentPeer::windowRequiresSynchronousCoreGraphicsRendering));
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

    // Plays the note-ons in real time, block by block, and times processBlock over the timed
    // part: wall time and the thread's CPU time. Nothing here allocates once it runs.
    void start()
    {
        begin = std::chrono::steady_clock::now();
        audio = std::thread ([this] {
            const double budgetMs = 1000.0 * block / kRate;
            if (realtime)
                wasRealtime = makeRealtime (budgetMs);
            juce::AudioBuffer<float> buffer (2, block);
            juce::MidiBuffer midi;
            midi.ensureSize (1024);
            const int lead = (int) std::lround (kLeadSeconds * kRate / block);
            const int timed = (int) std::lround (kTimedSeconds * kRate / block);
            blockMs.reserve ((size_t) timed);
            blockCpuMs.reserve ((size_t) timed);
            const double hitEvery = kHitEverySeconds * kRate;
            double nextHit = 0.0;
            int hit = 0;
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
                for (; nextHit < from + block; nextHit += hitEvery)
                    midi.addEvent (juce::MidiMessage::noteOn (processor.midiChannel(), kNotes[hit++ % 5], (juce::uint8) kVelocity),
                                   (int) (nextHit - from));
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
            done = true;
        });
    }

    Editor editorKind;
    int block;
    bool realtime;
    CymbalisProcessor processor;
    std::unique_ptr<CymbalisProcessor> other;
    PaintClock clock;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    std::thread audio;
    std::chrono::steady_clock::time_point begin;
    std::atomic<bool> done { false };
    bool wasRealtime = false;
    std::vector<double> blockMs, blockCpuMs;
    double restOfProcess = 0.0;

    // The cymbal over the timed part, read on the message thread (every 50 ms).
    bool glCounting = false;
    double glStartMs = 0.0;
    long hitsSum = 0;
    int hitSamples = 0, hitsMin = 1 << 30;
};

HitTiming::HitTiming (std::function<void (int)> f) : finished (std::move (f))
{
    // Each block size, at normal priority and then real-time: the three cases in turn, three
    // times, then the editor closed once more. Once (the first run with hits), the quiet after
    // the last hit is checked.
    for (const bool realtime : { false, true })
        for (const int block : kBlocks)
        {
            for (int round = 0; round < kRounds; ++round)
                for (const auto e : { Editor::none, Editor::idle, Editor::hits })
                    steps.push_back ({ e, block, realtime, e == Editor::hits && round == 0 && ! realtime && block == kBlocks[0] });
            steps.push_back ({ Editor::none, block, realtime, false });
        }
    // Once more with HAMMERED THIN RIDE selected, and once each with SWISH, RAVEN and KONSTAN
    // CRASH 18" (the dented meshes).
    for (const auto* shape : { &cymbalis::ui::kHammeredShape, &cymbalis::ui::kSwishShape, &cymbalis::ui::kRavenShape,
                               &cymbalis::ui::kKonstanShape })
        for (int p = 0; p < cymbalis::kNumPresets; ++p)
            if (cymbalis::kPresets[(size_t) p].shape == shape)
                for (const auto e : { Editor::none, Editor::idle, Editor::hits })
                    steps.push_back ({ e, kBlocks[0], false, false, p });
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
        case 0: // the next run
        {
            const auto& s = steps[next];
            run = std::make_unique<Run> (s.editor, s.block, s.realtime, s.preset);
            phaseStartMs = now;
            phase = 1;
            return;
        }
        case 1: // with an editor, timed once it has painted, OpenGL has started (or given up)
                // and JUCE's splash screen has gone (shown for 2 s, then fading for 2 s, when
                // the first editor opens)
            if (run->editor != nullptr
                && (now - phaseStartMs < 1000.0
                    || (run->clock.view != nullptr && ! run->clock.view->usesOpenGL() && now - phaseStartMs < kGlStartMs)
                    || (findChild<juce::JUCESplashScreen> (*run->editor) != nullptr && now - phaseStartMs < 8000.0)))
                return;
            phaseStartMs = now;
            windowServerStartMs = windowServerCpuMs();
            run->start();
            phase = 2;
            return;
        case 2:
        {
            if (auto* view = run->clock.view; view != nullptr && ! run->done)
            {
                if (run->clock.recording && ! run->glCounting)
                {
                    view->resetStats();
                    run->glCounting = true;
                    run->glStartMs = now;
                }
                else if (run->glCounting)
                {
                    const int shown = view->stats().hits;
                    run->hitsSum += shown;
                    ++run->hitSamples;
                    run->hitsMin = std::min (run->hitsMin, shown);
                }
            }
            if (! run->done)
                return;
            run->audio.join();
            const double windowServerMs = windowServerCpuMs();
            const auto& s = steps[next];
            Result r;
            r.editor = s.editor;
            r.block = s.block;
            r.preset = s.preset;
            r.realtime = s.realtime;
            r.wasRealtime = run->wasRealtime;
            r.blockMs = run->blockMs;
            r.blockCpuMs = run->blockCpuMs;
            r.restOfProcess = run->restOfProcess;
            if (windowServerStartMs >= 0.0 && windowServerMs >= 0.0)
                r.windowServer = 100.0 * (windowServerMs - windowServerStartMs) / (now - phaseStartMs);
            const auto& c = run->clock;
            r.paintMs = c.times;
            const double editorArea = (double) c.holder.getWidth() * (double) c.holder.getHeight();
            r.areaShareSum = editorArea > 0.0 ? c.areaSum / editorArea : 0.0;
            if (c.view != nullptr && run->glCounting)
            {
                r.openGL = c.view->usesOpenGL();
                r.renderer = c.view->renderer();
                if (const auto geo = c.view->geometry())
                {
                    r.meshSegments = (int) geo->lineIndices.size() / 2;
                    r.segmentsBefore = geo->segmentsBeforeSimplifying;
                    r.surfaceSegments = (int) geo->surface.size() / 2;
                    r.dents = geo->numDents;
                    r.buildMs = geo->buildMs;
                }
                r.gl = c.view->stats();
                r.glWallMs = now - run->glStartMs;
                r.hitsSum = run->hitsSum;
                r.hitSamples = run->hitSamples;
                r.hitsMin = run->hitSamples > 0 ? run->hitsMin : 0;
            }
            results.push_back (std::move (r));
            if (s.checkAfter && c.view != nullptr)
            {
                phaseStartMs = now;
                phase = 3;
                return;
            }
            finishRun();
            return;
        }
        case 3: // after the last hit: once every hit is older than 6 s, count over one second
            if (now - phaseStartMs < kQuietAfterMs)
                return;
            lingerPaints = run->clock.times.size();
            lingerFrames = run->clock.view->stats().frames;
            lingerUpdates = run->clock.view->stats().updates;
            lingerCpuMs = processCpuMs();
            run->clock.recording = true;
            phaseStartMs = now;
            phase = 4;
            return;
        case 4:
        {
            if (now - phaseStartMs < 1000.0)
                return;
            run->clock.recording = false;
            const auto st = run->clock.view->stats();
            auto& r = results.back();
            r.framesAfter = st.frames - lingerFrames;
            r.updatesAfter = st.updates - lingerUpdates;
            r.paintsAfter = (int) (run->clock.times.size() - lingerPaints);
            r.cpuAfter = 100.0 * (processCpuMs() - lingerCpuMs) / (now - phaseStartMs);
            finishRun();
            return;
        }
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
    std::printf ("hits: a strike every %.0f ms at velocity 0.9 (notes 60-64), %.1f kHz, editor 1536 x 1024, %d runs of %.0f s per case\n",
                 1000.0 * kHitEverySeconds, kRate / 1000.0, kRounds, kTimedSeconds);
    std::printf ("  CPU: %s\n", cpuSummary().toRawUTF8());
    bool ran = true;
    const auto percent = [] (double a, double b) { return b > 0.0 ? 100.0 * (a - b) / b : 0.0; };
    for (size_t i = 0; i < results.size();)
    {
        const auto& first = results[i];
        size_t end = i;
        while (end < results.size() && results[end].block == first.block && results[end].realtime == first.realtime
               && results[end].preset == first.preset)
            ++end;
        const double budget = 1000.0 * first.block / kRate;
        std::printf ("\n  %saudio thread %s, %d-sample blocks (budget %.3f ms)\n",
                     first.preset >= 0 ? (juce::String (cymbalis::kPresets[(size_t) first.preset].name) + " selected: ").toRawUTF8() : "",
                     ! first.realtime ? "at normal priority" : first.wasRealtime ? "real-time" : "real-time REFUSED (normal priority)",
                     first.block, budget);
        for (size_t j = i; j < end; ++j)
            if (results[j].meshSegments > 0)
            {
                const auto& r = results[j];
                std::printf ("    the cymbal: %d mesh segments", r.meshSegments);
                if (r.segmentsBefore > 0)
                    std::printf (" (%d before simplifying, %d dents)", r.segmentsBefore, r.dents);
                std::printf (", %d surface line segments; built in %.0f ms\n", r.surfaceSegments, r.buildMs);
                break;
            }
        std::printf ("    %-22s %s\n", "", "processBlock over all runs (ms)     worst of each run (ms)       its CPU (ms)    rest of the   WindowServer");
        std::printf ("    %-22s %s\n", "case", " median     p99   worst / budget                                 median     p99   process");
        struct Case
        {
            Stats wall, cpu, runWorsts;
            std::vector<double> worsts;
            double rest = 0.0, windowServer = 0.0;
            int windowServerRuns = 0;
        };
        std::array<Case, 3> cases;
        for (int e = 0; e < 3; ++e)
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
            std::printf ("    %-22s %7.3f %7.3f %7.3f %6.1f %%  %s   %7.3f %7.3f   %5.1f %%       %s\n", caseName ((Editor) e), k.wall.median,
                         k.wall.p99, k.wall.worst, 100.0 * k.wall.worst / budget, worsts, k.cpu.median, k.cpu.p99, k.rest, ws);
        }
        const auto& closed = cases[(size_t) Editor::none];
        const auto& open = cases[(size_t) Editor::hits];
        std::printf ("    editor open with hits against closed: median %+.0f %%, p99 %+.0f %%, worst %+.0f %% "
                     "(the runs' worsts, median of them: %+.0f %%)\n",
                     percent (open.wall.median, closed.wall.median), percent (open.wall.p99, closed.wall.p99),
                     percent (open.wall.worst, closed.wall.worst), percent (open.runWorsts.median, closed.runWorsts.median));
        std::printf ("    (CPU: %% of one core; the rest of the process includes the editor's paints and the cymbal's OpenGL frames)\n");
        for (const auto e : { Editor::idle, Editor::hits })
        {
            std::vector<double> paints;
            double areaShares = 0.0, glMs = 0.0, glWorst = 0.0, glWallMs = 0.0;
            long frames = 0, hitsSum = 0, hitSamples = 0;
            int hitsMin = 1 << 30, runs = 0, openGL = 0;
            juce::String renderer;
            const Result* checked = nullptr;
            for (size_t j = i; j < end; ++j)
            {
                const auto& r = results[j];
                if (r.editor != e)
                    continue;
                ran = ran && r.glWallMs > 0.0; // the editor and its cymbal were there
                paints.insert (paints.end(), r.paintMs.begin(), r.paintMs.end());
                areaShares += r.areaShareSum;
                openGL += r.openGL ? 1 : 0;
                if (r.renderer.isNotEmpty())
                    renderer = r.renderer;
                frames += r.gl.frames;
                glMs += r.gl.totalMs;
                glWorst = std::max (glWorst, r.gl.worstMs);
                glWallMs += r.glWallMs;
                hitsSum += r.hitsSum;
                hitSamples += r.hitSamples;
                if (r.hitSamples > 0)
                    hitsMin = std::min (hitsMin, r.hitsMin);
                ++runs;
                if (r.framesAfter >= 0)
                    checked = &r;
            }
            const auto p = statsOf (paints);
            const auto drawn = openGL == runs   ? "OpenGL (" + renderer + ")"
                               : openGL == 0 ? juce::String ("still drawing (no OpenGL)")
                                             : "OpenGL in some runs only (" + renderer + ")";
            std::printf ("    %-22s cymbal: %s; editor paints %3zu (average %.3f ms, worst %.3f ms, %3.0f %% of the editor per paint); "
                         "OpenGL frames %4ld (%.1f per second), render average %.3f ms, worst %.3f ms",
                         caseName (e), drawn.toRawUTF8(),
                         p.n, p.mean, p.worst, p.n > 0 ? 100.0 * areaShares / (double) p.n : 0.0, frames,
                         glWallMs > 0.0 ? 1000.0 * (double) frames / glWallMs : 0.0, frames > 0 ? glMs / (double) frames : 0.0, glWorst);
            if (e == Editor::hits)
                std::printf ("; hits shown mean %.1f, fewest %d", hitSamples > 0 ? (double) hitsSum / (double) hitSamples : 0.0,
                             hitSamples > 0 ? hitsMin : 0);
            std::printf ("\n");
            if (checked != nullptr)
            {
                ran = ran && checked->framesAfter == 0 && checked->updatesAfter == 0;
                std::printf ("    after the last hit, once every hit is older than 6 s, in 1 s: %d OpenGL frames, %d editor paints, "
                             "%d hit updates (the hit tracker's ticks), process CPU %.1f %% -> %s\n",
                             checked->framesAfter, checked->paintsAfter, checked->updatesAfter, checked->cpuAfter,
                             checked->framesAfter == 0 && checked->updatesAfter == 0 ? "at rest" : "STILL DRAWING");
            }
        }
        i = end;
    }
    std::fflush (stdout);
    results.clear();
    if (finished)
        finished (ran ? 0 : 1);
}
