#include "HitTiming.h"

#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <utility>

#include <pthread.h>
#include <unistd.h>
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
constexpr double kTimedSeconds = 3.0;
constexpr double kProfileTimedSeconds = 10.0; // the profiling run plays on while sample attaches and samples
constexpr double kHitEverySeconds = 0.024; // marks last 0.475 s at velocity 0.9: about 20 would be active, 16 are kept
constexpr int kNotes[] = { 60, 62, 64, 61, 63 }; // the default notes: BELL, BOW, EDGE, BELL2, BOW2
constexpr int kVelocity = 114;                    // 0.9
constexpr int kFeedBlock = 32;                    // the second processor's blocks, on the message thread

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

constexpr int kProfileSeconds = 2;
#if JUCE_MAC
juce::File profileFile() { return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("cymbalis_hits.sample.txt"); }
#endif

// Starts macOS's sample tool on this process for `seconds`, every 1 ms, into profileFile().
std::unique_ptr<juce::ChildProcess> startSampler (int seconds)
{
    auto p = std::make_unique<juce::ChildProcess>();
#if JUCE_MAC
    profileFile().deleteFile();
    if (p->start (juce::StringArray { "/usr/bin/sample", juce::String (getpid()), juce::String (seconds), "1", "-mayDie", "-file",
                                      profileFile().getFullPathName() }))
        return p;
#else
    juce::ignoreUnused (seconds);
#endif
    return nullptr;
}

// The sample's call graph, cut down to where the time goes: every frame (and thread) that was
// busy, not waiting, for at least 2 % of the samples, with that share of one core.
juce::String readProfile (juce::ChildProcess* sampler)
{
    juce::String out;
#if JUCE_MAC
    if (sampler == nullptr)
        return "profile: sample could not be started\n";
    const auto output = sampler->readAllProcessOutput();
    sampler->waitForProcessToFinish (20000);
    const auto lines = juce::StringArray::fromLines (profileFile().loadFileAsString());
    if (lines.size() < 5)
        return "profile: no sample output (" + output.trim() + ")\n";

    static const char* const waits[] = { "mach_msg2_trap", "mach_msg_trap", "__psynch_cvwait", "__semwait_signal", "__workq_kernreturn",
                                         "kevent", "kevent64", "kevent_id", "__select", "semaphore_wait_trap", "semaphore_timedwait_trap",
                                         "__ulock_wait", "__ulock_wait2", "__psynch_mutexwait", "nanosleep", "__sigsuspend", "read", "__wait4",
                                         "poll", "__pthread_kill", "swtch_pri" };
    struct Frame
    {
        int depth = 0, count = 0, waiting = 0, parent = -1, childCounts = 0;
        juce::String line, name;
    };
    std::vector<Frame> frames;
    std::vector<int> stack;
    int perThread = 0; // samples of one thread
    bool inGraph = false;
    for (const auto& line : lines)
    {
        if (line.startsWith ("Call graph:"))
        {
            inGraph = true;
            continue;
        }
        if (! inGraph)
            continue;
        if (line.trim().isEmpty() || line.startsWith ("Total number in stack"))
            break;
        const int digit = line.indexOfAnyOf ("0123456789");
        if (digit < 0)
            continue;
        Frame f;
        f.depth = digit;
        const auto rest = line.substring (digit);
        f.count = rest.getIntValue();
        f.name = rest.fromFirstOccurrenceOf (" ", false, false).upToFirstOccurrenceOf ("  ", false, false).trim();
        f.line = line.substring (juce::jmin (digit, 4)).trimEnd();
        if (perThread == 0 && f.name.startsWith ("Thread_"))
            perThread = f.count;
        while (! stack.empty() && frames[(size_t) stack.back()].depth >= f.depth)
            stack.pop_back();
        f.parent = stack.empty() ? -1 : stack.back();
        if (f.parent >= 0)
            frames[(size_t) f.parent].childCounts += f.count;
        stack.push_back ((int) frames.size());
        frames.push_back (f);
    }
    if (frames.empty() || perThread <= 0)
        return "profile: no call graph in the sample output\n";
    // Waiting samples, from the leaves up: a frame's own samples (at the top of the stack) count
    // as waiting when it is a wait.
    for (size_t i = frames.size(); i-- > 0;)
    {
        auto& f = frames[i];
        bool isWait = false;
        for (auto* w : waits)
            isWait = isWait || f.name == w;
        if (isWait)
            f.waiting += f.count - f.childCounts;
        if (f.parent >= 0)
            frames[(size_t) f.parent].waiting += f.waiting;
    }
    out << "profile: the hit display running, " << kProfileSeconds << " s sampled every 1 ms (" << perThread
        << " samples per thread); frames busy (not waiting) for at least 2 % of them, as % of one core:\n";
    int shown = 0;
    for (const auto& f : frames)
    {
        const int busy = f.count - f.waiting;
        if (100 * busy < 2 * perThread)
            continue;
        if (++shown > 160)
        {
            out << "  ...\n";
            break;
        }
        out << juce::String::formatted ("  %5.1f %% ", 100.0 * busy / perThread) << f.line.substring (0, 170) << "\n";
    }
#else
    juce::ignoreUnused (sampler);
#endif
    return out;
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

const char* caseName (HitTiming::Editor e, bool after, bool synchronous)
{
    switch (e)
    {
        case HitTiming::Editor::none: return after ? "editor closed (after)" : "editor closed (before)";
        case HitTiming::Editor::own: return synchronous ? "hit display, synchronous" : "editor open, hit display";
        case HitTiming::Editor::otherFed: return "editor open, dummy paint";
        case HitTiming::Editor::otherIdle: return "editor open, no hits";
    }
    return "";
}
} // namespace

struct HitTiming::Result
{
    Editor editor = Editor::none;
    bool after = false, synchronous = false;
    int block = 0;
    bool realtime = false, wasRealtime = false;
    Stats wall, cpu;
    double restOfProcess = 0.0, windowServer = -1.0, feed = 0.0; // % of one core
    Stats paints, layer;
    double areaPercent = 0.0, marksMean = 0.0;
    int marksMin = 0;
    double regionIntoImageMs = -1.0, editorIntoImageMs = -1.0;
    int paintsAfter = -1;          // the hit display's paints in the second from 0.6 s after the last hit
    double cpuAfter = 0.0;         // the process's CPU over that second, % of one core
    int timerAfter = -1;           // whether the hit display's own timer still ran at its end (1) or not (0)
    juce::Rectangle<int> lastClip;
    float scale = 0.0f;
};

struct HitTiming::Run final : private juce::Timer
{
    Run (Editor kind, int blockSize, bool rt, bool synchronous, double seconds)
        : editorKind (kind), block (blockSize), realtime (rt), timedSeconds (seconds)
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
        // view unless the editor asks for synchronous drawing (or this case forces it).
        const bool sync = synchronous || ! editor->wantsLayerBackedView();
        clock.addToDesktop (clock.getDesktopWindowStyleFlags()
                            | (sync ? juce::ComponentPeer::windowRequiresSynchronousCoreGraphicsRendering : 0));
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
            const int timed = (int) std::lround (timedSeconds * kRate / block);
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
        for (; (double) feedNext * kHitEverySeconds <= elapsed; ++feedNext)
            feedMidi.addEvent (juce::MidiMessage::noteOn (other->midiChannel(), kNotes[feedNext % 5], (juce::uint8) kVelocity),
                               std::min (at++, kFeedBlock - 1));
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
    double timedSeconds;
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
    int feedNext = 0;
    double feedMs = 0.0;
};

HitTiming::HitTiming (std::function<void (int)> f) : finished (std::move (f))
{
    // Each block size, at normal priority and then real-time: the cases in turn.
    for (const bool realtime : { false, true })
        for (const int block : kBlocks)
        {
            steps.push_back ({ Editor::none, block, realtime, false });
            steps.push_back ({ Editor::own, block, realtime, false });
            steps.push_back ({ Editor::own, block, realtime, true });
            steps.push_back ({ Editor::otherFed, block, realtime, false });
            steps.push_back ({ Editor::otherIdle, block, realtime, false });
            steps.push_back ({ Editor::none, block, realtime, false });
        }
#if JUCE_MAC
    // Last, not timed: where the process's CPU goes while the hit display runs.
    steps.push_back ({ Editor::own, kBlocks[0], false, false, true });
#endif
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
        case 0: // the next case; an editor's first paint renders its cached images
        {
            const auto& s = steps[next];
            run = std::make_unique<Run> (s.editor, s.block, s.realtime, s.synchronous, s.profile ? kProfileTimedSeconds : kTimedSeconds);
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
            if (steps[next].profile)
            {
                if (! samplerStarted && run->clock.recording)
                {
                    samplerStarted = true;
                    sampler = startSampler (kProfileSeconds);
                }
                if (! run->done || (sampler != nullptr && sampler->isRunning()))
                    return;
                run->audio.join();
                profile = readProfile (sampler.get());
                sampler.reset();
                finishRun();
                return;
            }
            if (! run->done)
                return;
            run->audio.join();
            const double windowServerMs = windowServerCpuMs();
            const auto& s = steps[next];
            Result r;
            r.editor = s.editor;
            r.after = s.editor == Editor::none && next > 0 && steps[next - 1].editor != Editor::none;
            r.synchronous = s.synchronous;
            r.block = s.block;
            r.realtime = s.realtime;
            r.wasRealtime = run->wasRealtime;
            r.wall = statsOf (run->blockMs);
            r.cpu = statsOf (run->blockCpuMs);
            r.restOfProcess = run->restOfProcess;
            if (windowServerStartMs >= 0.0 && windowServerMs >= 0.0)
                r.windowServer = 100.0 * (windowServerMs - windowServerStartMs) / (now - phaseStartMs);
            r.feed = run->timedWallMs > 0.0 ? 100.0 * run->feedMs / run->timedWallMs : 0.0;
            const auto& c = run->clock;
            r.paints = statsOf (c.times);
            r.layer = statsOf (c.hitTimes);
            const double editorArea = (double) c.holder.getWidth() * (double) c.holder.getHeight();
            r.areaPercent = r.paints.n > 0 && editorArea > 0.0 ? 100.0 * c.areaSum / (double) r.paints.n / editorArea : 0.0;
            r.marksMin = r.paints.n > 0 && c.hits != nullptr ? c.marksMin : 0;
            r.marksMean = r.paints.n > 0 ? (double) c.marksSum / (double) r.paints.n : 0.0;
            r.lastClip = c.lastClip;
            r.scale = c.scale;
            if (s.editor == Editor::own && ! s.synchronous && ! s.realtime && s.block == kBlocks[0] && r.paints.n > 0)
            {
                const auto [region, whole] = run->paintIntoImage (10);
                r.regionIntoImageMs = region.median;
                r.editorIntoImageMs = whole.median;
            }
            results.push_back (r);
            if (s.editor == Editor::own)
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
    std::printf ("hits: a strike every %.0f ms at velocity 0.9 (notes 60-64), %.1f kHz, editor 1536 x 1024, %.0f s timed per case\n",
                 1000.0 * kHitEverySeconds, kRate / 1000.0, kTimedSeconds);
    std::printf ("  CPU: %s\n", cpuSummary().toRawUTF8());
    bool ran = true;
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
        std::printf ("    %-26s %s\n", "", "    processBlock (ms)          worst   its CPU (ms)     rest of the   WindowServer");
        std::printf ("    %-26s %s\n", "case", " median     p99   worst   / budget  median     p99   process       (whole run)");
        for (size_t j = i; j < end; ++j)
        {
            const auto& r = results[j];
            ran = ran && r.wall.n > 0;
            char ws[32] = "-";
            if (r.windowServer >= 0.0)
                std::snprintf (ws, sizeof (ws), "%5.1f %%", r.windowServer);
            std::printf ("    %-26s %7.3f %7.3f %7.3f %8.1f %% %7.3f %7.3f   %5.1f %%       %s\n", caseName (r.editor, r.after, r.synchronous),
                         r.wall.median, r.wall.p99, r.wall.worst, 100.0 * r.wall.worst / budget, r.cpu.median, r.cpu.p99,
                         r.restOfProcess, ws);
        }
        std::printf ("    (CPU: %% of one core; the rest of the process includes the editor's painting and, with the dummy, feeding its processor)\n");
        for (size_t j = i; j < end; ++j)
        {
            const auto& r = results[j];
            if (r.editor == Editor::none)
                continue;
            if (r.editor != Editor::otherIdle)
                ran = ran && r.paints.n > 0 && r.layer.n > 0;
            std::printf ("    %-26s paints %3zu, average %.3f ms, worst %.3f ms; marks layer %3zu, average %.3f ms; %3.0f %% of the editor per paint; marks mean %.1f",
                         caseName (r.editor, false, r.synchronous), r.paints.n, r.paints.mean, r.paints.worst, r.layer.n, r.layer.mean,
                         r.areaPercent, r.marksMean);
            if (r.editor == Editor::otherFed)
                std::printf ("; feeding its processor %.1f %%", r.feed);
            if (r.paintsAfter >= 0)
                std::printf ("; after the last hit: %d paints in 1 s, its timer %s, process CPU %.1f %%", r.paintsAfter,
                             r.timerAfter == 0 ? "stopped" : r.timerAfter == 1 ? "STILL RUNNING" : "unknown", r.cpuAfter);
            std::printf ("\n");
        }
        for (size_t j = i; j < end; ++j)
        {
            const auto& r = results[j];
            if (r.regionIntoImageMs < 0.0)
                continue;
            std::printf ("    hit display: editor paint calls average %.3f ms (target: average under 2 ms) -> %s; marks active at least %d (at most %d)\n",
                         r.paints.mean, r.paints.mean < 2.0 ? "within target" : "ABOVE TARGET", r.marksMin,
                         cymbalis::ui::HitLayer::maxMarks);
            std::printf ("    the last paint's region (%s) painted into an image: median %.3f ms; the whole editor: median %.3f ms; %.2f physical pixels per unit\n",
                         r.lastClip.toString().toRawUTF8(), r.regionIntoImageMs, r.editorIntoImageMs, (double) r.scale);
        }
        i = end;
    }
    if (profile.isNotEmpty())
        std::printf ("\n%s", profile.toRawUTF8());
    std::fflush (stdout);
    results.clear();
    if (finished)
        finished (ran ? 0 : 1);
}
