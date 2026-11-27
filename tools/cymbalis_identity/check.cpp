// check.cpp — FastEngine of the working tree against FastEngine at another commit (run.sh
// builds both into this program: the other one's class is renamed FastEngineRef). Nothing is
// written to disk.
//
//   check [--mechanisms] BAKE.json ...
//       Output bit for bit, in memory: a strike every 24 ms for 5 s (slots in turn at their
//       default positions, velocities cycling), then strikes 0.4 s apart at positions in
//       between, 11 s in all, at 44.1 and 96 kHz with 128- and 64-sample blocks.
//       --mechanisms: the energy cascade (which draws from the strike-phase sequence), loud-band
//       and re-hit damping on, the same for both engines. Per case: bit-identical or not, the
//       largest |difference| relative to the peak, how many samples differ. Exit status 1 if
//       any sample differs.
//
//   check --bound BAKE.json ...
//       For a change meant to differ only far below hearing (docs/PORT_MAP.md, "One silence
//       rule: 140 dB below a full hit"), in memory, at 44.1 and 96 kHz with 128-sample blocks:
//       single hits on every row at velocities 0.2, 0.5 and 1.0, 120 s each; the --dense
//       pattern for 60 s (a strike every 24 ms, slots in turn, velocities cycling; on the
//       hi-hat the pedal moving as in --dense), with the bake's tuning. The difference (working
//       tree - REF) in any 10 ms window (RMS, both channels) must stay 120 dB below the peak of
//       a velocity-1.0 hit on the preset's BOW (the hi-hat's TIP), as REF plays it; and in the
//       first 30 s after each single hit, the RMS of every 1 s window must be within 0.01 dB of
//       REF's wherever REF's is above -100 dB re that hit's peak. Then, at 44.1 kHz, one hit at
//       0.8 on each row: how long until each engine is silent (isSilent). Exit status 1 if any
//       bound is broken.
//
//   check --nova-edge-v2 REF_NOVA_BAKE.json NOVA_BAKE.json
//       NOVA RIDE edge v2 (docs/PORT_MAP.md, "NOVA RIDE edge v2"), REF's engine on REF's NOVA bake
//       against the working tree's on the edge v2 bake: NOVA's BOW, BELL, BELL2 and BOW2 (lone
//       strikes at 0.3, 0.6, 0.9 and 1.0, BOW2 at the bow end of BLEND POSITION, and the four
//       rows in turn for 11 s, at 44.1 kHz / 128 and 96 kHz / 64, HIT VARIATION off and on),
//       bit for bit with the bake's edge NPZ block taken out (the added modes alone), and with
//       it as reported (the NPZ block gives NOVA the NPZ presets' engine structure); the new
//       EDGE at 0.35, 0.65, 0.8, 0.95 and 1.0: its level (mono mean, RMS of 0.05-0.55 s after a
//       lone strike, HIT VARIATION as baked) against the approved renders (within 1.5 dB), its
//       L/R balance (within 0.5 dB of centre) and L-R correlation over the first 2 s (within
//       BOW's, BELL's and BOW2's at the same velocities), and the share of 50 Hz in the 2-8 kHz
//       envelope's modulation over 0.5-2.5 s (at most 1.5 %). Exit status 1 if one fails.
//
//   check --splash BAKE.json ...
//       The HI-HAT's foot splash and fast-tap FOOT (docs/PORT_MAP.md, "Hi-hat"), in memory, the
//       working tree against REF: every other preset bit for bit (the identity sequences above);
//       the hi-hat bit for bit wherever there is no splash and no fast tap (TIP, EDGE and FOOT
//       strikes with the pedal as --dense moves it, opening and closing slowly, closing and staying
//       closed, and open/closed 8ths at 120 bpm with no splash); the splash gestures' levels (mono
//       mean of both channels, dB re full scale, in windows after the stomp) against the prototype's;
//       the join (high-passed level per 5 ms around the splash); the fast taps (a 5 ms tap plays
//       one FOOT; taps the smoothed openness reaches play as today); the cost of the gestures.
//       Exit status 1 if anything that must be bit for bit is not.
//
//   check --time [--runs N] [--sample-rate SR ...] BAKE.json ...
//       Block times of both engines in one process on a real-time thread (as a host's audio
//       thread): at every block's deadline both render that block, one after the other (which
//       one first alternates), so a slow moment of the machine falls on both; the smallest of
//       the runs per block (the engines are deterministic, so block i does the same work in every
//       run). A strike every 24 ms, and flams (two strikes in one block, the second 1 ms later or
//       half a block, whichever is shorter), at 128- and 64-sample blocks (default 44.1 kHz).
#include "Bake.h"

#define FastEngine FastEngineRef
#include "ref/FastEngine.h"
#undef FastEngine
#include "FastEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#elif defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif
#if defined(__x86_64__)
#include <xmmintrin.h>
#endif

namespace
{
using Clock = std::chrono::steady_clock;
constexpr double kVelocities[] = {0.95, 0.4, 0.8, 0.6, 1.0, 0.3, 0.7};

// Denormals flushed, as juce::ScopedNoDenormals in processBlock.
void noDenormals()
{
#if defined(__x86_64__)
    _mm_setcsr(_mm_getcsr() | 0x8040);
#elif defined(__aarch64__)
    uint64_t fpcr;
    asm volatile("mrs %0, fpcr" : "=r"(fpcr));
    asm volatile("msr fpcr, %0" : : "r"(fpcr | (uint64_t(1) << 24)));
#endif
}

bool makeRealtime(double periodMs)
{
#if defined(__APPLE__)
    mach_timebase_info_data_t base {};
    mach_timebase_info(&base);
    const auto ticks = [&](double ms) { return uint32_t(ms * 1.0e6 * double(base.denom) / double(base.numer)); };
    thread_time_constraint_policy_data_t policy {};
    policy.period = ticks(periodMs);
    policy.computation = ticks(0.5 * periodMs);
    policy.constraint = ticks(periodMs);
    policy.preemptible = 1;
    return thread_policy_set(pthread_mach_thread_np(pthread_self()), THREAD_TIME_CONSTRAINT_POLICY,
                             thread_policy_t(&policy), THREAD_TIME_CONSTRAINT_POLICY_COUNT) == KERN_SUCCESS;
#elif defined(__linux__)
    (void) periodMs;
    sched_param param {};
    param.sched_priority = sched_get_priority_max(SCHED_FIFO) - 1;
    return pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) == 0;
#else
    (void) periodMs;
    return false;
#endif
}

std::string name(const std::string& path)
{
    std::string s = path.substr(path.find_last_of('/') + 1);
    return s.substr(0, s.find('.'));
}

struct Hit
{
    int64_t sample;
    int slot;
    double velocity, position;
};

// Renders `total` samples in blocks, split at each strike as the plugin does.
template <class E>
void render(E& e, const std::vector<Hit>& hits, int64_t total, int block, std::vector<float>& L, std::vector<float>& R)
{
    L.assign(size_t(total), 0.0f);
    R.assign(size_t(total), 0.0f);
    size_t h = 0;
    for (int64_t start = 0; start < total; start += block)
    {
        const int n = int(std::min<int64_t>(block, total - start));
        int pos = 0;
        for (; h < hits.size() && hits[h].sample < start + n; ++h)
        {
            const int at = int(hits[h].sample - start);
            if (at > pos)
            {
                e.process(L.data() + start + pos, R.data() + start + pos, at - pos);
                pos = at;
            }
            e.strike(hits[h].slot, hits[h].velocity, hits[h].position);
        }
        if (pos < n)
            e.process(L.data() + start + pos, R.data() + start + pos, n - pos);
    }
}

int identity(const std::vector<std::string>& bakes, bool mechanisms)
{
    bool same = true;
    double worst = 0.0;
    for (const auto& path : bakes)
    {
        auto bake = cymbalis::loadBake(path);
        const int slots = int(bake->slots.size());
        cymbalis::Tuning t = bake->tuning;
        t.cymbal.cascadeAmount = 0.8;
        t.cymbal.cascadeThresholdDb = -40.0;
        t.cymbal.loudDampAmount = 1.5;
        t.cymbal.loudDampThresholdDb = -30.0;
        t.cymbal.rehitDampAmount = 0.6;
        for (const double sr : {44100.0, 96000.0})
            for (const int block : {128, 64})
            {
                std::vector<Hit> hits;
                int k = 0;
                for (; k * 0.024 < 5.0; ++k)
                    hits.push_back({int64_t(std::floor(k * 0.024 * sr)), k % slots, kVelocities[k % 7],
                                    bake->slots[size_t(k % slots)].position});
                for (int j = 0; j < 8; ++j, ++k)
                    hits.push_back({int64_t((6.5 + 0.4 * j) * sr), k % slots, kVelocities[k % 7], 0.15 * j});
                const int64_t total = int64_t(11.0 * sr);
                cymbalis::FastEngineRef a(bake);
                cymbalis::FastEngine b(bake);
                a.prepare(sr);
                b.prepare(sr);
                if (mechanisms)
                {
                    a.setTuning(t, nullptr);
                    b.setTuning(t, nullptr);
                }
                std::vector<float> La, Ra, Lb, Rb;
                render(a, hits, total, block, La, Ra);
                render(b, hits, total, block, Lb, Rb);
                double peak = 0.0, diff = 0.0;
                size_t differing = 0;
                for (size_t s = 0; s < La.size(); ++s)
                {
                    peak = std::max({peak, double(std::fabs(La[s])), double(std::fabs(Ra[s]))});
                    diff = std::max({diff, std::fabs(double(La[s]) - Lb[s]), std::fabs(double(Ra[s]) - Rb[s])});
                    differing += std::memcmp(&La[s], &Lb[s], 4) != 0 || std::memcmp(&Ra[s], &Rb[s], 4) != 0;
                }
                const double rel = peak > 0.0 ? diff / peak : 0.0;
                worst = std::max(worst, rel);
                same = same && differing == 0;
                std::printf("%-20s %5.1f kHz / %3d: %s  peak %.4f  max |diff| / peak %.3g  samples differing %zu\n",
                            name(path).c_str(), sr / 1000.0, block, differing == 0 ? "bit-identical" : "DIFFERENT    ", peak,
                            rel, differing);
            }
    }
    std::printf("%s%s; largest |diff| / peak %.3g\n", same ? "all bit-identical" : "NOT bit-identical",
                mechanisms ? " (cascade and dampings on)" : "", worst);
    return same ? 0 : 1;
}

// --bound ------------------------------------------------------------------------------------

// The pedal of --dense (tools/cymbalis_bench/Dense.h): a CC every 10 ms, closed to open and back
// every 1.5 s, in the CC's 127 steps.
double pedalOpenness(double t)
{
    const double o = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * t / 1.5);
    return std::round(127.0 * o) / 127.0;
}

// The difference of two renders of the same events, as they are made: the largest RMS of the
// difference over 10 ms windows and its largest sample, and per 1 s window both renders' RMS.
struct Compare
{
    int window = 0, inWindow = 0;
    double winSum = 0.0, worstWin = 0.0, worstSample = 0.0, peakA = 0.0;
    std::vector<double> secA, secB; // per second: sum of squares
    double secLen = 0.0;
    int64_t done = 0;
    explicit Compare(double sr) : window(int(std::lround(0.01 * sr))), secLen(sr) {}
    void add(const float* la, const float* ra, const float* lb, const float* rb, int n)
    {
        for (int i = 0; i < n; ++i, ++done)
        {
            const double dl = double(lb[i]) - la[i], dr = double(rb[i]) - ra[i];
            winSum += dl * dl + dr * dr;
            worstSample = std::max({worstSample, std::fabs(dl), std::fabs(dr)});
            peakA = std::max({peakA, double(std::fabs(la[i])), double(std::fabs(ra[i]))});
            if (++inWindow == window)
            {
                worstWin = std::max(worstWin, std::sqrt(winSum / (2.0 * window)));
                winSum = 0.0;
                inWindow = 0;
            }
            const size_t sec = size_t(double(done) / secLen);
            if (sec < 30)
            {
                if (secA.size() <= sec)
                {
                    secA.resize(sec + 1, 0.0);
                    secB.resize(sec + 1, 0.0);
                }
                secA[sec] += double(la[i]) * la[i] + double(ra[i]) * ra[i];
                secB[sec] += double(lb[i]) * lb[i] + double(rb[i]) * rb[i];
            }
        }
    }
};

// Renders the same events through both engines block by block (split at each event, as the
// plugin does) and compares them. slot -1: the pedal at `velocity`.
template <class A, class B>
void renderBoth(A& a, B& b, const std::vector<Hit>& hits, int64_t total, int block, Compare& c)
{
    const size_t bs = static_cast<size_t>(block);
    std::vector<float> La(bs), Ra(bs), Lb(bs), Rb(bs);
    size_t h = 0;
    for (int64_t start = 0; start < total; start += block)
    {
        const int n = int(std::min<int64_t>(block, total - start));
        int pos = 0;
        for (; h < hits.size() && hits[h].sample < start + n; ++h)
        {
            const int at = int(hits[h].sample - start);
            if (at > pos)
            {
                a.process(La.data() + pos, Ra.data() + pos, at - pos);
                b.process(Lb.data() + pos, Rb.data() + pos, at - pos);
                pos = at;
            }
            if (hits[h].slot < 0)
            {
                a.setPedal(hits[h].velocity);
                b.setPedal(hits[h].velocity);
            }
            else
            {
                a.strike(hits[h].slot, hits[h].velocity, hits[h].position);
                b.strike(hits[h].slot, hits[h].velocity, hits[h].position);
            }
        }
        if (pos < n)
        {
            a.process(La.data() + pos, Ra.data() + pos, n - pos);
            b.process(Lb.data() + pos, Rb.data() + pos, n - pos);
        }
        c.add(La.data(), Ra.data(), Lb.data(), Rb.data(), n);
    }
}

double db(double x)
{
    return 20.0 * std::log10(std::max(x, 1e-300));
}

struct BoundRow
{
    std::string text, tails;
    bool pass = true;
    double worstDb = -400.0; // re the reference peak, all sequences
};

BoundRow boundFor(const std::string& path)
{
    noDenormals(); // a new thread may not have the main thread's mode
    BoundRow out;
    auto bake = cymbalis::loadBake(path);
    const int slots = int(bake->slots.size());
    int refSlot = bake->slotIndex("BOW");
    if (refSlot < 0)
        refSlot = bake->slotIndex("TIP");
    if (refSlot < 0)
        refSlot = 0;
    constexpr int block = 128;
    char line[512];
    for (const double sr : {44100.0, 96000.0})
    {
        // The reference: a velocity-1.0 hit on BOW (TIP), as REF plays it, first second.
        double refPeak = 0.0;
        {
            cymbalis::FastEngineRef a(bake);
            cymbalis::FastEngine b(bake);
            a.prepare(sr);
            b.prepare(sr);
            Compare c(sr);
            renderBoth(a, b, {{0, refSlot, 1.0, bake->slots[size_t(refSlot)].position}}, int64_t(sr), block, c);
            refPeak = c.peakA;
        }
        double hitWorst = 0.0, hitWorstSample = 0.0, rmsDev = 0.0;
        int rmsWindows = 0;
        for (int s = 0; s < slots; ++s)
            for (const double v : {0.2, 0.5, 1.0})
            {
                cymbalis::FastEngineRef a(bake);
                cymbalis::FastEngine b(bake);
                a.prepare(sr);
                b.prepare(sr);
                Compare c(sr);
                renderBoth(a, b, {{0, s, v, bake->slots[size_t(s)].position}}, int64_t(120.0 * sr), block, c);
                hitWorst = std::max(hitWorst, c.worstWin);
                hitWorstSample = std::max(hitWorstSample, c.worstSample);
                for (size_t k = 0; k < c.secA.size(); ++k)
                {
                    const double ra = std::sqrt(c.secA[k] / (2.0 * sr)), rb = std::sqrt(c.secB[k] / (2.0 * sr));
                    if (ra > c.peakA * 1e-5)
                    {
                        rmsDev = std::max(rmsDev, std::fabs(db(rb) - db(ra)));
                        ++rmsWindows;
                    }
                }
            }
        // The --dense pattern, 60 s, the pedal moving on the hi-hat.
        std::vector<Hit> hits;
        for (int k = 0; k * 0.024 < 60.0; ++k)
            hits.push_back({int64_t(std::floor(k * 0.024 * sr)), k % slots, kVelocities[k % 7],
                            bake->slots[size_t(k % slots)].position});
        if (bake->hihat.present)
        {
            for (int64_t m = 0; double(m) * 0.01 < 60.0; ++m)
                hits.push_back({int64_t(std::floor(double(m) * 0.01 * sr)), -1, pedalOpenness(double(m) * 0.01), 0.0});
            std::stable_sort(hits.begin(), hits.end(), [](const Hit& x, const Hit& y) { return x.sample < y.sample; });
        }
        cymbalis::FastEngineRef a(bake);
        cymbalis::FastEngine b(bake);
        a.prepare(sr);
        b.prepare(sr);
        Compare dense(sr);
        renderBoth(a, b, hits, int64_t(60.0 * sr), block, dense);
        const double hitDb = db(hitWorst / refPeak), denseDb = db(dense.worstWin / refPeak);
        const bool pass = hitDb <= -120.0 && denseDb <= -120.0 && rmsDev <= 0.01;
        out.pass = out.pass && pass;
        out.worstDb = std::max({out.worstDb, hitDb, denseDb});
        std::snprintf(line, sizeof line, "| %s | %.1f kHz | %s %.4f | %.1f dB (%.1f) | %.1f dB (%.1f) | %.5f dB (%d) | %s |\n",
                      name(path).c_str(), sr / 1000.0, bake->slots[size_t(refSlot)].name.c_str(), refPeak, hitDb,
                      db(hitWorstSample / refPeak), denseDb, db(dense.worstSample / refPeak), rmsDev, rmsWindows,
                      pass ? "yes" : "NO");
        out.text += line;
    }
    // How long one hit at 0.8 on each row rings until each engine is silent (44.1 kHz, at most 300 s).
    for (int s = 0; s < slots; ++s)
    {
        cymbalis::FastEngineRef a(bake);
        cymbalis::FastEngine b(bake);
        a.prepare(44100.0);
        b.prepare(44100.0);
        a.strike(s, 0.8, bake->slots[size_t(s)].position);
        b.strike(s, 0.8, bake->slots[size_t(s)].position);
        const size_t bs = static_cast<size_t>(block);
        std::vector<float> L(bs), R(bs);
        double ta = -1.0, tb = -1.0;
        for (int64_t start = 0; start < int64_t(300.0 * 44100.0) && (ta < 0.0 || tb < 0.0); start += block)
        {
            if (ta < 0.0)
            {
                a.process(L.data(), R.data(), block);
                if (a.isSilent())
                    ta = double(start + block) / 44100.0;
            }
            if (tb < 0.0)
            {
                b.process(L.data(), R.data(), block);
                if (b.isSilent())
                    tb = double(start + block) / 44100.0;
            }
        }
        std::snprintf(line, sizeof line, "| %s | %s | %s | %s |\n", name(path).c_str(), bake->slots[size_t(s)].name.c_str(),
                      ta < 0.0 ? "over 300 s" : (std::to_string(int(std::lround(ta * 10.0)) / 10.0).substr(0, 5) + " s").c_str(),
                      tb < 0.0 ? "over 300 s" : (std::to_string(int(std::lround(tb * 10.0)) / 10.0).substr(0, 5) + " s").c_str());
        out.tails += line;
    }
    return out;
}

int bound(const std::vector<std::string>& bakes)
{
    std::vector<BoundRow> rows(bakes.size());
    std::vector<std::thread> threads;
    std::mutex m;
    size_t next = 0;
    const unsigned n = std::max(1u, std::min<unsigned>(std::thread::hardware_concurrency(), unsigned(bakes.size())));
    std::string error;
    for (unsigned t = 0; t < n; ++t)
        threads.emplace_back([&] {
            for (;;)
            {
                size_t i;
                {
                    std::lock_guard<std::mutex> lock(m);
                    if (next >= bakes.size() || !error.empty())
                        return;
                    i = next++;
                }
                try
                {
                    rows[i] = boundFor(bakes[i]);
                }
                catch (const std::exception& e)
                {
                    std::lock_guard<std::mutex> lock(m);
                    error = bakes[i] + ": " + e.what();
                }
            }
        });
    for (auto& t : threads)
        t.join();
    if (!error.empty())
        throw std::runtime_error(error);
    std::printf("\nThe difference (working tree - REF), 128-sample blocks: the largest RMS over 10 ms windows (both "
                "channels) and, in brackets, the largest sample, in dB re the reference (a velocity-1.0 hit on BOW, "
                "the hi-hat's TIP, as REF plays it, first second). Single hits: every row at 0.2, 0.5 and 1.0, 120 s "
                "each. Dense: the --dense pattern, 60 s. RMS: in the first 30 s after each single hit, the largest "
                "|difference| of 1 s window RMS where REF's is above -100 dB re the hit (windows checked). Bounds: "
                "-120 dB and 0.01 dB.\n\n");
    std::printf("| Preset | Rate | Reference peak | Single hits | Dense | RMS, first 30 s | Pass |\n|---|---|---|---|---|---|---|\n");
    bool pass = true;
    for (const auto& r : rows)
    {
        std::printf("%s", r.text.c_str());
        pass = pass && r.pass;
    }
    std::printf("\nOne hit at 0.8, 44.1 kHz: time until the engine is silent (isSilent).\n\n| Preset | Row | REF | Working "
                "tree |\n|---|---|---|---|\n");
    for (const auto& r : rows)
        std::printf("%s", r.tails.c_str());
    double worst = -400.0;
    for (const auto& r : rows)
        worst = std::max(worst, r.worstDb);
    std::printf("\n%s; the largest difference over 10 ms windows: %.1f dB re the reference.\n",
                pass ? "Every bound holds" : "A BOUND IS BROKEN", worst);
    return pass ? 0 : 1;
}

// --splash ------------------------------------------------------------------------------------

// Renders `total` samples in blocks, split at each event; slot -1: the pedal at `velocity`.
template <class E>
void renderEvents(E& e, const std::vector<Hit>& hits, int64_t total, int block, std::vector<float>& L, std::vector<float>& R)
{
    L.assign(size_t(total), 0.0f);
    R.assign(size_t(total), 0.0f);
    size_t h = 0;
    for (int64_t start = 0; start < total; start += block)
    {
        const int n = int(std::min<int64_t>(block, total - start));
        int pos = 0;
        for (; h < hits.size() && hits[h].sample < start + n; ++h)
        {
            const int at = int(hits[h].sample - start);
            if (at > pos)
            {
                e.process(L.data() + start + pos, R.data() + start + pos, at - pos);
                pos = at;
            }
            if (hits[h].slot < 0)
                e.setPedal(hits[h].velocity);
            else
                e.strike(hits[h].slot, hits[h].velocity, hits[h].position);
        }
        if (pos < n)
            e.process(L.data() + start + pos, R.data() + start + pos, n - pos);
    }
}

void sortHits(std::vector<Hit>& hits)
{
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& x, const Hit& y) { return x.sample < y.sample; });
}

// The pedal along a piecewise-linear path of (time s, openness) points: a CC every `step` seconds
// on each slope (each point's own value at its time), in the CC's 127 steps.
void pedalPath(std::vector<Hit>& out, double sr, const std::vector<std::pair<double, double>>& pts, double step = 0.001)
{
    const auto cc = [](double o) { return std::round(127.0 * std::min(std::max(o, 0.0), 1.0)) / 127.0; };
    for (size_t i = 0; i < pts.size(); ++i)
    {
        out.push_back({int64_t(std::floor(pts[i].first * sr)), -1, cc(pts[i].second), 0.0});
        if (i + 1 < pts.size() && pts[i + 1].second != pts[i].second)
            for (double t = pts[i].first + step; t < pts[i + 1].first - 1e-9; t += step)
            {
                const double a = (t - pts[i].first) / (pts[i + 1].first - pts[i].first);
                out.push_back({int64_t(std::floor(t * sr)), -1, cc(pts[i].second + a * (pts[i + 1].second - pts[i].second)), 0.0});
            }
    }
}

struct Pair
{
    std::vector<float> La, Ra, Lb, Rb;
    uint64_t footA = 0, footB = 0, splashB = 0;
    double splashTime = -1.0, cpuB = 0.0;
    size_t differing() const
    {
        size_t d = 0;
        for (size_t i = 0; i < La.size(); ++i)
            d += std::memcmp(&La[i], &Lb[i], 4) != 0 || std::memcmp(&Ra[i], &Rb[i], 4) != 0;
        return d;
    }
    int64_t firstDiff() const
    {
        for (size_t i = 0; i < La.size(); ++i)
            if (std::memcmp(&La[i], &Lb[i], 4) != 0 || std::memcmp(&Ra[i], &Rb[i], 4) != 0)
                return int64_t(i);
        return -1;
    }
};

Pair renderPair(const std::shared_ptr<const cymbalis::BakeData>& bake, double sr, const std::vector<Hit>& hits, double seconds,
                const cymbalis::Tuning* tuning = nullptr)
{
    Pair p;
    cymbalis::FastEngineRef a(bake);
    cymbalis::FastEngine b(bake);
    a.prepare(sr);
    b.prepare(sr);
    if (tuning != nullptr)
    {
        a.setTuning(*tuning, nullptr);
        b.setTuning(*tuning, nullptr);
    }
    const int64_t total = int64_t(seconds * sr);
    renderEvents(a, hits, total, 128, p.La, p.Ra);
    const auto t0 = Clock::now();
    renderEvents(b, hits, total, 128, p.Lb, p.Rb);
    p.cpuB = std::chrono::duration<double>(Clock::now() - t0).count();
    p.footA = a.footStrikes();
    p.footB = b.footStrikes();
    p.splashB = b.splashStrikes();
    p.splashTime = b.lastSplashTime();
    return p;
}

// Mono mean of both channels, RMS over [t0, t1) s, in dB re full scale.
double levelDb(const std::vector<float>& L, const std::vector<float>& R, double sr, double t0, double t1)
{
    const size_t a = size_t(std::max(0.0, t0 * sr)), b = std::min(L.size(), size_t(t1 * sr));
    double sum = 0.0;
    for (size_t i = a; i < b; ++i)
    {
        const double m = 0.5 * (double(L[i]) + R[i]);
        sum += m * m;
    }
    return b > a ? db(std::sqrt(sum / double(b - a))) : -400.0;
}

int splash(const std::vector<std::string>& bakes)
{
    bool ok = true;
    std::vector<std::string> others;
    std::string hihatPath;
    for (const auto& path : bakes)
    {
        if (cymbalis::loadBake(path)->hihat.present)
            hihatPath = path;
        else
            others.push_back(path);
    }
    std::printf("Every other preset (the identity sequences, with the bake's tuning and with the cascade and dampings on):\n");
    ok = identity(others, false) == 0 && ok;
    ok = identity(others, true) == 0 && ok;
    if (hihatPath.empty())
    {
        std::printf("no hi-hat bake given\n");
        return ok ? 0 : 1;
    }
    auto bake = cymbalis::loadBake(hihatPath);
    const int tip = bake->slotIndex("TIP"), edge = bake->slotIndex("EDGE"), foot = bake->slotIndex("FOOT");
    const auto pos = [&](int slot) { return bake->slots[size_t(slot)].position; };

    // 1. Bit for bit wherever there is no splash and no fast tap.
    std::printf("\nHI-HAT bit for bit (no splash, no fast tap), 128-sample blocks:\n\n| Pattern | Rate | Samples differing | FOOT (REF / now) | SPLASH |\n|---|---|---|---|---|\n");
    for (const double sr : {44100.0, 96000.0})
    {
        struct Case
        {
            const char* name;
            std::vector<Hit> hits;
            double seconds;
        };
        std::vector<Case> cases;
        {
            // TIP, EDGE and FOOT every 24 ms, the pedal as --dense moves it.
            Case c { "TIP, EDGE and FOOT every 24 ms, the pedal as --dense", {}, 30.0 };
            const int slots3[] = {tip, edge, foot};
            for (int k = 0; k * 0.024 < c.seconds; ++k)
                c.hits.push_back({int64_t(std::floor(k * 0.024 * sr)), slots3[k % 3], kVelocities[k % 7], pos(slots3[k % 3])});
            for (int64_t m = 0; double(m) * 0.01 < c.seconds; ++m)
                c.hits.push_back({int64_t(std::floor(double(m) * 0.01 * sr)), -1, pedalOpenness(double(m) * 0.01), 0.0});
            sortHits(c.hits);
            cases.push_back(std::move(c));
        }
        {
            // TIP and EDGE every 125 ms, the pedal opening and closing slowly (every 4 s).
            Case c { "TIP and EDGE every 125 ms, the pedal opening and closing every 4 s", {}, 30.0 };
            for (int k = 0; k * 0.125 < c.seconds; ++k)
                c.hits.push_back({int64_t(std::floor(k * 0.125 * sr)), k % 2 ? edge : tip, kVelocities[k % 7], pos(k % 2 ? edge : tip)});
            for (int64_t m = 0; double(m) * 0.01 < c.seconds; ++m)
            {
                const double t = double(m) * 0.01;
                c.hits.push_back({int64_t(std::floor(t * sr)), -1, std::round(127.0 * (0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * t / 4.0))) / 127.0, 0.0});
            }
            sortHits(c.hits);
            cases.push_back(std::move(c));
        }
        {
            // Open for 1 s with EDGE hits, closed over 30 ms and held closed 2 s with TIP hits, again.
            Case c { "Open 1 s (EDGE), closed over 30 ms and held 2 s (TIP), repeated", {}, 18.0 };
            for (double t0 = 0.0; t0 < c.seconds; t0 += 3.0)
            {
                pedalPath(c.hits, sr, {{t0, 1.0}, {t0 + 1.0, 1.0}, {t0 + 1.03, 0.0}, {t0 + 2.99, 0.0}});
                for (int k = 0; k < 4; ++k)
                    c.hits.push_back({int64_t(std::floor((t0 + 0.1 + 0.2 * k) * sr)), edge, kVelocities[k % 7], pos(edge)});
                for (int k = 0; k < 8; ++k)
                    c.hits.push_back({int64_t(std::floor((t0 + 1.2 + 0.2 * k) * sr)), tip, kVelocities[(k + 3) % 7], pos(tip)});
            }
            sortHits(c.hits);
            cases.push_back(std::move(c));
        }
        {
            // Open/closed 8ths at 120 bpm: the pedal closing on the beat (TIP, closed) and reopening
            // for the next "and" (EDGE, open): the reopen 200 ms after the beat.
            Case c { "Open/closed 8ths at 120 bpm (reopen 200 ms after the beat)", {}, 16.0 };
            for (double tb = 0.5; tb < c.seconds - 0.5; tb += 0.5)
            {
                c.hits.push_back({int64_t(std::floor(tb * sr)), -1, 0.0, 0.0});
                c.hits.push_back({int64_t(std::floor((tb + 0.03) * sr)), tip, 0.7, pos(tip)});
                c.hits.push_back({int64_t(std::floor((tb + 0.2) * sr)), -1, 1.0, 0.0});
                c.hits.push_back({int64_t(std::floor((tb + 0.25) * sr)), edge, 0.85, pos(edge)});
            }
            c.hits.insert(c.hits.begin(), {0, -1, 1.0, 0.0});
            sortHits(c.hits);
            cases.push_back(std::move(c));
        }
        for (const auto& c : cases)
        {
            const Pair p = renderPair(bake, sr, c.hits, c.seconds);
            const size_t d = p.differing();
            ok = ok && d == 0 && p.splashB == 0;
            std::printf("| %s | %.1f kHz | %zu | %llu / %llu | %llu |\n", c.name, sr / 1000.0, d, (unsigned long long) p.footA,
                        (unsigned long long) p.footB, (unsigned long long) p.splashB);
        }
    }
    {
        // The same 8ths with SPLASH WINDOW at 250 ms: splashes fire (why 150 ms).
        std::vector<Hit> hits {{0, -1, 1.0, 0.0}};
        const double sr = 44100.0;
        for (double tb = 0.5; tb < 15.5; tb += 0.5)
        {
            hits.push_back({int64_t(std::floor(tb * sr)), -1, 0.0, 0.0});
            hits.push_back({int64_t(std::floor((tb + 0.03) * sr)), tip, 0.7, pos(tip)});
            hits.push_back({int64_t(std::floor((tb + 0.2) * sr)), -1, 1.0, 0.0});
            hits.push_back({int64_t(std::floor((tb + 0.25) * sr)), edge, 0.85, pos(edge)});
        }
        sortHits(hits);
        for (const double ms : {100.0, 150.0, 250.0})
        {
            cymbalis::Tuning t = bake->tuning;
            t.cymbal.hhSplashWindowMs = ms;
            const Pair p = renderPair(bake, sr, hits, 16.0, &t);
            std::printf("- The 8ths with SPLASH WINDOW %.0f ms: %llu splashes over %llu FOOTs; %s.\n", ms, (unsigned long long) p.splashB,
                        (unsigned long long) p.footB, p.differing() == 0 ? "bit for bit as REF" : "differs from REF");
        }
    }

    // 2. The splash gestures (the stomp at 1 s, the pedal open before it), against the prototype.
    struct Gesture
    {
        const char* name;
        std::vector<std::pair<double, double>> path;
        double proto[3];
        bool splash;
    };
    const double T0 = 1.0;
    const std::vector<Gesture> gestures {
        {"Fully open: 1 -> 0 over 30 ms, 30 ms closed, 0 -> 1 over 70 ms", {{0.0, 1.0}, {T0, 1.0}, {T0 + 0.03, 0.0}, {T0 + 0.06, 0.0}, {T0 + 0.13, 1.0}}, {-48, -59, -65}, true},
        {"Half open: 0.5 -> 0 over 100 ms, 30 ms closed, 0 -> 1 over 100 ms", {{0.0, 0.5}, {T0, 0.5}, {T0 + 0.1, 0.0}, {T0 + 0.13, 0.0}, {T0 + 0.23, 1.0}}, {-64, -77, -85}, true},
        {"Held closed 0.4 s before the release", {{0.0, 1.0}, {T0, 1.0}, {T0 + 0.03, 0.0}, {T0 + 0.43, 0.0}, {T0 + 0.5, 1.0}}, {0, 0, 0}, false},
        {"Splash, closed again 1.2 s later", {{0.0, 1.0}, {T0, 1.0}, {T0 + 0.03, 0.0}, {T0 + 0.06, 0.0}, {T0 + 0.13, 1.0}, {T0 + 1.2, 1.0}, {T0 + 1.23, 0.0}}, {0, 0, 0}, true},
    };
    const double windows[3][2] = {{0.2, 0.6}, {1.0, 1.5}, {2.0, 3.0}};
    std::printf("\nThe splash gestures, 44.1 kHz: level of the mono mean of both channels (dB re full scale, after the output gain, "
                "HIT VARIATION as baked) in windows after the stomp, REF (today) / now (the prototype's).\n\n"
                "| Gesture | 0.2-0.6 s | 1.0-1.5 s | 2-3 s | FOOT / SPLASH now |\n|---|---|---|---|---|\n");
    double cpu = 0.0, audio = 0.0;
    Pair first;
    for (size_t g = 0; g < gestures.size(); ++g)
    {
        const double sr = 44100.0;
        std::vector<Hit> hits;
        pedalPath(hits, sr, gestures[g].path);
        sortHits(hits);
        const Pair p = renderPair(bake, sr, hits, T0 + 3.5);
        cpu += p.cpuB;
        audio += T0 + 3.5;
        std::printf("| %s |", gestures[g].name);
        for (const auto& w : windows)
        {
            const double a = levelDb(p.La, p.Ra, sr, T0 + w[0], T0 + w[1]), b = levelDb(p.Lb, p.Rb, sr, T0 + w[0], T0 + w[1]);
            const size_t k = size_t(&w - &windows[0]);
            if (gestures[g].proto[0] != 0)
                std::printf(" %.0f / %.0f (%.0f) |", a, b, gestures[g].proto[k]);
            else
                std::printf(" %.0f / %.0f |", a, b);
        }
        std::printf(" %llu / %llu |\n", (unsigned long long) p.footB, (unsigned long long) p.splashB);
        if (!gestures[g].splash)
        {
            const bool same = p.differing() == 0 && p.splashB == 0;
            ok = ok && same;
            std::printf("|   (held closed: %s) | | | | |\n", same ? "no splash, bit for bit as REF" : "DIFFERS FROM REF");
        }
        if (g == 0)
            first = p;
    }
    // 3. The join: the high-passed (2nd-order Butterworth, 1 kHz) mono level per 5 ms around the
    // splash of the fully open gesture.
    if (first.splashTime >= 0.0)
    {
        const double sr = 44100.0;
        const double w0 = 2.0 * 3.14159265358979323846 * 1000.0 / sr, cw = std::cos(w0), al = std::sin(w0) / std::sqrt(2.0);
        const double b0 = (1 + cw) / 2 / (1 + al), b1 = -(1 + cw) / (1 + al), b2 = b0, a1 = -2 * cw / (1 + al), a2 = (1 - al) / (1 + al);
        std::vector<double> hp(first.Lb.size());
        double z1 = 0, z2 = 0;
        for (size_t i = 0; i < hp.size(); ++i)
        {
            const double x = 0.5 * (double(first.Lb[i]) + first.Rb[i]);
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            hp[i] = y;
        }
        const int64_t at = int64_t(std::llround(first.splashTime * sr)), win = int64_t(0.005 * sr);
        std::printf("\nThe join (fully open gesture, the splash at %.1f ms after the stomp): high-passed (1 kHz) mono level per 5 ms, "
                    "dB re full scale, from 30 ms before the splash to 30 ms after:\n\n", 1000.0 * (first.splashTime - T0));
        double prev = 0.0, worst = 0.0;
        for (int k = -6; k < 6; ++k)
        {
            double sum = 0.0;
            for (int64_t i = at + k * win; i < at + (k + 1) * win; ++i)
                sum += hp[size_t(i)] * hp[size_t(i)];
            const double l = db(std::sqrt(sum / double(win)));
            std::printf("%s%.1f", k == -6 ? "" : ", ", l);
            if (k > -6)
                worst = std::max(worst, std::fabs(l - prev));
            prev = l;
        }
        std::printf("\n\nLargest step between neighbouring 5 ms windows: %.1f dB.\n", worst);
        // The splash on its own (now - REF, broadband mono) per 5 ms from its first sample, and REF
        // (the FOOT alone) over the 30 ms before it: no onset peak means no click.
        std::printf("The splash alone (now - REF) per 5 ms from its first sample, dB:");
        for (int k = 0; k < 6; ++k)
        {
            double sum = 0.0;
            for (int64_t i = at + k * win; i < at + (k + 1) * win; ++i)
            {
                const double d = 0.5 * ((double(first.Lb[size_t(i)]) - first.La[size_t(i)]) + (double(first.Rb[size_t(i)]) - first.Ra[size_t(i)]));
                sum += d * d;
            }
            std::printf("%s %.1f", k == 0 ? "" : ",", db(std::sqrt(sum / double(win))));
        }
        double prevRef = 0.0, worstRef = 0.0;
        for (int k = -6; k < 0; ++k)
        {
            double sum = 0.0;
            for (int64_t i = at + k * win; i < at + (k + 1) * win; ++i)
            {
                const double m = 0.5 * (double(first.La[size_t(i)]) + first.Ra[size_t(i)]);
                sum += m * m;
            }
            const double l = db(std::sqrt(sum / double(win)));
            if (k > -6)
                worstRef = std::max(worstRef, std::fabs(l - prevRef));
            prevRef = l;
        }
        std::printf(".\nREF alone (the FOOT) over the 30 ms before: largest step between neighbouring 5 ms windows %.1f dB.\n", worstRef);
    }

    // 4. The fast taps: the pedal resting at CC 25 (openness 0.197: armed, under the splash's
    // reopen level), a tap to CC 0 of d ms.
    std::printf("\nThe fast taps (the pedal at CC 25, a tap to CC 0 for d ms, 44.1 kHz):\n\n| Tap | FOOT (REF / now) | SPLASH now | Bit for bit |\n|---|---|---|---|\n");
    for (const double d : {0.005, 0.010, 0.020, 0.040})
    {
        const double sr = 44100.0;
        std::vector<Hit> hits {{0, -1, 25.0 / 127.0, 0.0}, {int64_t(T0 * sr), -1, 0.0, 0.0}, {int64_t((T0 + d) * sr), -1, 25.0 / 127.0, 0.0}};
        const Pair p = renderPair(bake, sr, hits, T0 + 1.0);
        const bool same = p.differing() == 0;
        if (p.footA > 0)
            ok = ok && same; // the smoothed openness reached it: as today
        std::printf("| %.0f ms | %llu / %llu | %llu | %s |\n", d * 1000.0, (unsigned long long) p.footA, (unsigned long long) p.footB,
                    (unsigned long long) p.splashB, same ? "yes" : "no");
    }
    {
        // A 5 ms tap from fully open: the fast-tap FOOT finds the pedal still above the reopen level.
        const double sr = 44100.0;
        std::vector<Hit> hits {{0, -1, 1.0, 0.0}, {int64_t(T0 * sr), -1, 0.0, 0.0}, {int64_t((T0 + 0.005) * sr), -1, 1.0, 0.0}};
        const Pair p = renderPair(bake, sr, hits, T0 + 3.5);
        std::printf("\nA 5 ms tap from fully open: FOOT %llu / %llu, SPLASH %llu (at %.1f ms after the tap); level 0.2-0.6 s %.0f / %.0f dB.\n",
                    (unsigned long long) p.footA, (unsigned long long) p.footB, (unsigned long long) p.splashB,
                    p.splashTime >= 0.0 ? 1000.0 * (p.splashTime - T0) : -1.0, levelDb(p.La, p.Ra, sr, T0 + 0.2, T0 + 0.6),
                    levelDb(p.Lb, p.Rb, sr, T0 + 0.2, T0 + 0.6));
    }
    std::printf("\nCost: the splash gestures rendered at %.0fx real time (now, one thread, 128-sample blocks).\n", audio / std::max(cpu, 1e-9));
    std::printf("\n%s\n", ok ? "Everything that must be bit for bit is." : "SOMETHING THAT MUST BE BIT FOR BIT IS NOT.");
    return ok ? 0 : 1;
}

// The block sequence of a run: the strikes in order, a strike every 24 ms or flams.
std::vector<Hit> strikesFor(const cymbalis::BakeData& bake, double sr, int block, bool flams)
{
    const int slots = int(bake.slots.size());
    const int gap = std::min(int(std::lround(0.001 * sr)), block / 2);
    std::vector<Hit> hits;
    for (int k = 0; k * 0.024 < 5.0; ++k)
    {
        const int s = k % slots;
        if (!flams)
            hits.push_back({int64_t(std::floor(k * 0.024 * sr)), s, kVelocities[k % 7], bake.slots[size_t(s)].position});
        else
        {
            // Both in one block: the first 4 samples into it.
            const int64_t at = (int64_t(std::ceil(k * 0.024 * sr)) + block - 1) / block * block + 4;
            const int s2 = (k + 2) % slots;
            hits.push_back({at, s, kVelocities[k % 7], bake.slots[size_t(s)].position});
            hits.push_back({at + gap, s2, kVelocities[(k + 3) % 7], bake.slots[size_t(s2)].position});
        }
    }
    return hits;
}

// One block of an engine from `h` on: its time (us) and its strikes.
template <class E>
double renderBlock(E& e, const std::vector<Hit>& hits, size_t& h, int64_t from, int block, float* L, float* R, int& count)
{
    int pos = 0;
    count = 0;
    const auto t0 = Clock::now();
    for (; h < hits.size() && hits[h].sample < from + block; ++h, ++count)
    {
        const int at = int(hits[h].sample - from);
        if (at > pos)
        {
            e.process(L + pos, R + pos, at - pos);
            pos = at;
        }
        e.strike(hits[h].slot, hits[h].velocity, hits[h].position);
    }
    if (pos < block)
        e.process(L + pos, R + pos, block - pos);
    return std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
}

// One run of both engines in step, after a 1 s lead: each block's time per engine (ref, this)
// and its number of strikes.
void timeRun(const std::shared_ptr<const cymbalis::BakeData>& bake, double sr, int block, bool flams,
             std::vector<double> (&t)[2], std::vector<uint8_t>& strikes)
{
    makeRealtime(1000.0 * block / sr);
    cymbalis::FastEngineRef a(bake);
    cymbalis::FastEngine b(bake);
    a.prepare(sr);
    b.prepare(sr);
    const std::vector<Hit> hits = strikesFor(*bake, sr, block, flams);
    std::vector<float> L(static_cast<size_t>(block)), R(static_cast<size_t>(block));
    const int lead = int(1.0 * sr / block), timed = int(4.0 * sr / block);
    size_t ha = 0, hb = 0;
    t[0].clear();
    t[1].clear();
    strikes.clear();
    const auto start = Clock::now();
    for (int i = 0; i < lead + timed; ++i)
    {
        const int64_t from = int64_t(i) * block;
        int n = 0;
        double ta, tb;
        if (i % 2 == 0)
        {
            ta = renderBlock(a, hits, ha, from, block, L.data(), R.data(), n);
            tb = renderBlock(b, hits, hb, from, block, L.data(), R.data(), n);
        }
        else
        {
            tb = renderBlock(b, hits, hb, from, block, L.data(), R.data(), n);
            ta = renderBlock(a, hits, ha, from, block, L.data(), R.data(), n);
        }
        if (i >= lead)
        {
            t[0].push_back(ta);
            t[1].push_back(tb);
            strikes.push_back(uint8_t(n));
        }
        std::this_thread::sleep_until(start + std::chrono::duration_cast<Clock::duration>(
                                                  std::chrono::duration<double>(double(i + 1) * block / sr)));
    }
}

struct Summary
{
    double median = 0.0, worst = 0.0;
};

Summary summary(const std::vector<double>& t, const std::vector<uint8_t>& strikes, int count)
{
    std::vector<double> x;
    for (size_t i = 0; i < t.size(); ++i)
        if (int(strikes[i]) == count)
            x.push_back(t[i]);
    if (x.empty())
        return {};
    std::sort(x.begin(), x.end());
    return {x[x.size() / 2], x.back()};
}

int timing(const std::vector<std::string>& bakes, std::vector<double> rates, int runs)
{
    if (rates.empty())
        rates = {44100.0};
    std::printf("Block times (us, and %% of the block's duration): the ref engine and this one in one process "
                "on a real-time thread, both rendering each block at its deadline (which one first alternates), "
                "the smallest of %d runs per block; median and worst of the blocks with no strike, one strike, "
                "two strikes (a flam).\n\n",
                runs);
    std::printf("| Preset | Rate | Block | Budget | Engine | No strike: median | One strike: median | worst | "
                "Flam: median | worst |\n|---|---|---|---|---|---|---|---|---|---|\n");
    for (const auto& path : bakes)
    {
        auto bake = cymbalis::loadBake(path);
        for (const double sr : rates)
            for (const int block : {128, 64})
            {
                const double budget = 1e6 * block / sr;
                std::vector<double> single[2], flam[2], a[2], f[2];
                std::vector<uint8_t> sSingle, sFlam;
                for (int r = 0; r < runs; ++r)
                {
                    timeRun(bake, sr, block, false, a, sSingle);
                    timeRun(bake, sr, block, true, f, sFlam);
                    for (int e = 0; e < 2; ++e)
                    {
                        if (r == 0)
                        {
                            single[e] = a[e];
                            flam[e] = f[e];
                        }
                        else
                            for (size_t i = 0; i < a[e].size(); ++i)
                            {
                                single[e][i] = std::min(single[e][i], a[e][i]);
                                flam[e][i] = std::min(flam[e][i], f[e][i]);
                            }
                    }
                }
                for (int e = 0; e < 2; ++e)
                {
                    const Summary none = summary(single[e], sSingle, 0), one = summary(single[e], sSingle, 1),
                                  two = summary(flam[e], sFlam, 2);
                    const auto pc = [&](double us) { return 100.0 * us / budget; };
                    std::printf("| %s | %.1f kHz | %d | %.0f us | %s | %.0f (%.0f %%) | %.0f (%.0f %%) | %.0f (%.0f %%) | "
                                "%.0f (%.0f %%) | %.0f (%.0f %%) |\n",
                                name(path).c_str(), sr / 1000.0, block, budget, e == 0 ? "main" : "branch", none.median,
                                pc(none.median), one.median, pc(one.median), one.worst, pc(one.worst), two.median,
                                pc(two.median), two.worst, pc(two.worst));
                }
            }
    }
    return 0;
}

// --nova-edge-v2 ------------------------------------------------------------------------------
// NOVA RIDE edge v2 (docs/PORT_MAP.md, "NOVA RIDE edge v2"): REF's engine on REF's NOVA bake
// against the working tree's on its NOVA bake (2304 modes, the edge NPZ block).

// Lone strike from a fresh engine: `seconds` at `sr` in `block`-sample blocks, interleaved L R.
template <class E>
std::vector<float> lone(const std::shared_ptr<const cymbalis::BakeData>& b, int slot, double v, double pos, double sr,
                        double seconds, int block)
{
    E e(b);
    e.prepare(sr);
    std::vector<Hit> hits {{0, slot, v, pos}};
    std::vector<float> L, R;
    render(e, hits, int64_t(seconds * sr), block, L, R);
    std::vector<float> out(2 * L.size());
    for (size_t i = 0; i < L.size(); ++i)
    {
        out[2 * i] = L[i];
        out[2 * i + 1] = R[i];
    }
    return out;
}

// 4th-order Butterworth band-pass (two high-pass and two low-pass RBJ biquads), in double.
std::vector<double> bandPass(const std::vector<double>& x, double sr, double lo, double hi)
{
    struct Bq
    {
        double b0, b1, b2, a1, a2, z1 = 0.0, z2 = 0.0;
        double run(double v)
        {
            const double y = b0 * v + z1;
            z1 = b1 * v - a1 * y + z2;
            z2 = b2 * v - a2 * y;
            return y;
        }
    };
    auto make = [sr](bool high, double f, double q) {
        const double w = 2.0 * 3.14159265358979323846 * f / sr, c = std::cos(w), al = std::sin(w) / (2.0 * q), a0 = 1.0 + al;
        const double k = high ? (1.0 + c) / 2.0 : (1.0 - c) / 2.0;
        return Bq {k / a0, (high ? -2.0 * k : 2.0 * k) / a0, k / a0, -2.0 * c / a0, (1.0 - al) / a0};
    };
    Bq f[4] = {make(true, lo, 0.5412), make(true, lo, 1.3066), make(false, hi, 0.5412), make(false, hi, 1.3066)};
    std::vector<double> y(x.size());
    for (size_t i = 0; i < x.size(); ++i)
    {
        double v = x[i];
        for (auto& q : f)
            v = q.run(v);
        y[i] = v;
    }
    return y;
}

// The share of 50 Hz in the modulation of the 2-8 kHz band's envelope over 0.5-2.5 s (the 20 ms
// frame-decay restart's sawtooth): the band's power in 1 ms frames, over its own 100 ms moving
// average, minus 1; its spectrum (Hann window, 0.5 Hz bins); the power within 1 Hz of 50 Hz as a
// share of the power from 1 to 450 Hz.
double share50Hz(const std::vector<float>& st, double sr)
{
    std::vector<double> mono(st.size() / 2);
    for (size_t i = 0; i < mono.size(); ++i)
        mono[i] = 0.5 * (double(st[2 * i]) + st[2 * i + 1]);
    const auto y = bandPass(mono, sr, 2000.0, 8000.0);
    const double fr = sr / 1000.0;
    std::vector<double> p;
    for (int k = 0;; ++k)
    {
        const size_t a = size_t(std::lround(k * fr)), b = size_t(std::lround((k + 1) * fr));
        if (b > y.size())
            break;
        double s2 = 0.0;
        for (size_t i = a; i < b; ++i)
            s2 += y[i] * y[i];
        p.push_back(s2 / double(b - a));
    }
    const int i0 = 500, i1 = 2500, half = 50; // 1 kHz frames; the moving average +-50 ms
    if (int(p.size()) < i1 + half)
        return -1.0;
    std::vector<double> m;
    for (int i = i0; i < i1; ++i)
    {
        double t = 0.0;
        for (int j = i - half; j <= i + half; ++j)
            t += p[size_t(j)];
        t /= double(2 * half + 1);
        m.push_back(t > 0.0 ? p[size_t(i)] / t - 1.0 : 0.0);
    }
    const size_t N = m.size();
    double total = 0.0, at50 = 0.0;
    for (size_t k = 2; k <= 900; ++k) // 0.5 Hz bins: 1-450 Hz
    {
        double re = 0.0, im = 0.0;
        for (size_t i = 0; i < N; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * double(i) / double(N - 1));
            const double ph = -2.0 * 3.14159265358979323846 * double(k) * double(i) / double(N);
            re += w * m[i] * std::cos(ph);
            im += w * m[i] * std::sin(ph);
        }
        const double pw = re * re + im * im;
        total += pw;
        if (k >= 98 && k <= 102)
            at50 += pw;
    }
    return total > 0.0 ? at50 / total : 0.0;
}

int novaEdgeV2(const std::string& refPath, const std::string& path)
{
    bool ok = true;
    auto fail = [&](const std::string& what) {
        std::printf("  FAILED: %s\n", what.c_str());
        ok = false;
    };
    const auto refBake = cymbalis::loadBake(refPath);
    const auto bake = cymbalis::loadBake(path);
    std::printf("NOVA RIDE edge v2: REF's engine on %s (%d modes) against the working tree's on %s (%d modes, %zu added)\n\n",
                refPath.c_str(), refBake->numModes, path.c_str(), bake->numModes, bake->addedModes.size());
    if (bake->addedModes.empty() || !bake->edgeNpz.present)
        fail("the bake has no added modes or no edge NPZ block");
    // The same bake without its edge NPZ block: the added modes alone (the NPZ block changes the
    // engine's structure for every strike: no decay on the shared oscillator, the control block
    // dividing the 20 ms frame).
    auto noNpz = std::make_shared<cymbalis::BakeData>(*bake);
    noNpz->edgeNpz = cymbalis::EdgeNpz {};
    const char* rows[] = {"BOW", "BELL", "BELL2", "BOW2"};

    // 1. NOVA's BOW, BELL, BELL2 and BOW2.
    struct Cmp
    {
        double peak = 0.0, diff = 0.0;
        size_t differing = 0;
        void add(const std::vector<float>& a, const std::vector<float>& b)
        {
            for (size_t i = 0; i < a.size(); ++i)
            {
                peak = std::max(peak, double(std::fabs(a[i])));
                diff = std::max(diff, std::fabs(double(a[i]) - b[i]));
                differing += std::memcmp(&a[i], &b[i], 4) != 0;
            }
        }
        std::string text() const
        {
            if (differing == 0)
                return "bit-identical";
            char buf[96];
            std::snprintf(buf, sizeof(buf), "%zu samples differ, largest |diff| %.1f dB re the peak", differing,
                          20.0 * std::log10(diff / peak));
            return buf;
        }
    };
    std::printf("| Rows | Rate / block | HIT VARIATION | Added modes alone (no NPZ block) | The edge v2 bake |\n|---|---|---|---|---|\n");
    double worstFull = 0.0;
    for (const auto& [sr, block] : std::vector<std::pair<double, int>> {{44100.0, 128}, {96000.0, 64}})
        for (bool hv : {false, true})
        {
            auto withHv = [hv](const std::shared_ptr<const cymbalis::BakeData>& b) {
                auto c = std::make_shared<cymbalis::BakeData>(*b);
                c->tuning.cymbal.hitVariation = hv;
                return std::shared_ptr<const cymbalis::BakeData>(c);
            };
            const auto r = withHv(refBake), a = withHv(noNpz), f = withHv(bake);
            Cmp loneA, loneF;
            for (const char* row : rows)
                for (double v : {0.3, 0.6, 0.9, 1.0})
                {
                    const int slot = r->slotIndex(row);
                    const double pos = r->slots[size_t(slot)].position;
                    const auto ref = lone<cymbalis::FastEngineRef>(r, slot, v, pos, sr, 6.0, block);
                    loneA.add(ref, lone<cymbalis::FastEngine>(a, slot, v, pos, sr, 6.0, block));
                    loneF.add(ref, lone<cymbalis::FastEngine>(f, slot, v, pos, sr, 6.0, block));
                }
            // BOW2 with BLEND POSITION 2 at the bow (position 0.5 - 0.5 x 0, SlotTable.h).
            {
                const int slot = r->slotIndex("BOW2");
                const auto ref = lone<cymbalis::FastEngineRef>(r, slot, 0.8, 0.5, sr, 6.0, block);
                loneA.add(ref, lone<cymbalis::FastEngine>(a, slot, 0.8, 0.5, sr, 6.0, block));
                loneF.add(ref, lone<cymbalis::FastEngine>(f, slot, 0.8, 0.5, sr, 6.0, block));
            }
            // The four rows in turn: a strike every 24 ms for 5 s, velocities cycling, then 0.4 s
            // apart at their positions, 11 s (the identity sequence without EDGE).
            Cmp seqA, seqF;
            {
                std::vector<Hit> hits;
                int k = 0;
                for (; k * 0.024 < 5.0; ++k)
                {
                    const int slot = r->slotIndex(rows[k % 4]);
                    hits.push_back({int64_t(std::floor(k * 0.024 * sr)), slot, kVelocities[k % 7], r->slots[size_t(slot)].position});
                }
                for (int j = 0; j < 8; ++j, ++k)
                {
                    const int slot = r->slotIndex(rows[k % 4]);
                    hits.push_back({int64_t((6.5 + 0.4 * j) * sr), slot, kVelocities[k % 7], r->slots[size_t(slot)].position});
                }
                const int64_t total = int64_t(11.0 * sr);
                auto run = [&](auto& e) {
                    e.prepare(sr);
                    std::vector<float> L, R;
                    render(e, hits, total, block, L, R);
                    std::vector<float> out(2 * L.size());
                    for (size_t i = 0; i < L.size(); ++i)
                    {
                        out[2 * i] = L[i];
                        out[2 * i + 1] = R[i];
                    }
                    return out;
                };
                cymbalis::FastEngineRef er(r);
                cymbalis::FastEngine ea(a), ef(f);
                const auto ref = run(er);
                seqA.add(ref, run(ea));
                seqF.add(ref, run(ef));
            }
            std::printf("| lone strikes at 0.3, 0.6, 0.9, 1.0; BOW2 at the bow end of BLEND POSITION | %.1f kHz / %d | %s | %s | %s |\n",
                        sr / 1000.0, block, hv ? "on" : "off", loneA.text().c_str(), loneF.text().c_str());
            std::printf("| the four rows in turn, 11 s | %.1f kHz / %d | %s | %s | %s |\n", sr / 1000.0, block, hv ? "on" : "off",
                        seqA.text().c_str(), seqF.text().c_str());
            if (loneA.differing != 0 || seqA.differing != 0)
                fail("the added modes change NOVA's BOW, BELL, BELL2 or BOW2");
            for (const Cmp* c : {&loneF, &seqF})
                if (c->differing != 0)
                    worstFull = std::max(worstFull, c->diff / c->peak);
        }
    std::printf("\nThe edge v2 bake's other rows: largest |diff| %.1f dB re the peak (reported: the edge NPZ block gives NOVA the "
                "NPZ presets' engine structure, docs/PORT_MAP.md, \"NOVA RIDE edge v2\")\n",
                worstFull > 0.0 ? 20.0 * std::log10(worstFull) : -999.0);

    // 2. The new EDGE: levels against the approved renders (44.1 kHz, mono mean, dB re full
    // scale after the output gain, RMS of 0.05-0.55 s after a lone strike, HIT VARIATION as baked).
    const double sr = 44100.0;
    const int edge = bake->slotIndex("EDGE");
    const double edgePos = bake->slots[size_t(edge)].position;
    struct Approved
    {
        double v, db;
    };
    const Approved approved[] = {{0.35, -40.9}, {0.65, -37.6}, {0.8, -30.1}, {0.95, -29.4}, {1.0, -27.6}};
    auto stats = [](const std::vector<float>& st, double sr2, double t0, double t1, double& monoDbOut, double& lrDb, double& corr) {
        const size_t a = size_t(t0 * sr2), b = std::min(st.size() / 2, size_t(t1 * sr2));
        double m2 = 0.0, l2 = 0.0, r2 = 0.0, lr = 0.0;
        for (size_t i = a; i < b; ++i)
        {
            const double l = st[2 * i], r = st[2 * i + 1], m = 0.5 * (l + r);
            m2 += m * m;
            l2 += l * l;
            r2 += r * r;
            lr += l * r;
        }
        monoDbOut = 10.0 * std::log10(m2 / double(b - a));
        lrDb = 10.0 * std::log10(l2 / r2);
        corr = lr / std::sqrt(l2 * r2);
    };
    std::printf("\nEDGE, lone strikes (44.1 kHz, HIT VARIATION as baked):\n\n| Velocity | Level 0.05-0.55 s | Approved | Difference | "
                "L/R (0-2 s) | L-R correlation (0-2 s) | 50 Hz share |\n|---|---|---|---|---|---|---|\n");
    double cLo = 1e9, cHi = -1e9;
    std::vector<double> edgeCorr;
    for (const auto& ap : approved)
    {
        const auto st = lone<cymbalis::FastEngine>(bake, edge, ap.v, edgePos, sr, 3.0, 128);
        double lvl, lr, corr, d0, d1;
        stats(st, sr, 0.05, 0.55, lvl, d0, d1);
        stats(st, sr, 0.0, 2.0, d0, lr, corr);
        const double sh = share50Hz(st, sr);
        edgeCorr.push_back(corr);
        std::printf("| %.2f | %.1f dB | %.1f dB | %+.1f dB | %+.2f dB | %+.3f | %.2f %% |\n", ap.v, lvl, ap.db, lvl - ap.db, lr, corr, 100.0 * sh);
        if (std::fabs(lvl - ap.db) > 1.5)
            fail("EDGE at " + std::to_string(ap.v) + ": more than 1.5 dB from the approved render");
        if (std::fabs(lr) > 0.5)
            fail("EDGE at " + std::to_string(ap.v) + ": L/R balance more than 0.5 dB from centre");
        if (!(sh <= 0.015))
            fail("EDGE at " + std::to_string(ap.v) + ": the 50 Hz share is above 1.5 %");
    }
    std::printf("\nThe L-R correlation (0-2 s) of NOVA's BOW, BELL and BOW2 at the same velocities:\n\n| Row |");
    for (const auto& ap : approved)
        std::printf(" %.2f |", ap.v);
    std::printf("\n|---|---|---|---|---|---|\n");
    for (const char* row : {"BOW", "BELL", "BOW2"})
    {
        const int slot = bake->slotIndex(row);
        std::printf("| %s |", row);
        for (const auto& ap : approved)
        {
            const auto st = lone<cymbalis::FastEngine>(bake, slot, ap.v, bake->slots[size_t(slot)].position, sr, 2.0, 128);
            double d0, d1, corr;
            stats(st, sr, 0.0, 2.0, d0, d1, corr);
            cLo = std::min(cLo, corr);
            cHi = std::max(cHi, corr);
            std::printf(" %+.3f |", corr);
        }
        std::printf("\n");
    }
    for (double c : edgeCorr)
        if (c < cLo || c > cHi)
            fail("EDGE's L-R correlation " + std::to_string(c) + " is outside BOW's, BELL's and BOW2's range");
    std::printf("\nEDGE's correlation %+.3f to %+.3f, within BOW's, BELL's and BOW2's %+.3f to %+.3f: %s\n",
                *std::min_element(edgeCorr.begin(), edgeCorr.end()), *std::max_element(edgeCorr.begin(), edgeCorr.end()), cLo, cHi,
                ok ? "yes" : "see above");

    // The 50 Hz share for comparison: REF's EDGE (the bow shoulder at the edge) and the new EDGE
    // with the 20 ms frame-decay restart switched on (velocity 0.8).
    {
        const auto mainEdge = lone<cymbalis::FastEngineRef>(refBake, refBake->slotIndex("EDGE"), 0.8,
                                                             refBake->slots[size_t(refBake->slotIndex("EDGE"))].position, sr, 3.0, 128);
        auto on = std::make_shared<cymbalis::BakeData>(*bake);
        on->tuning.cymbal.frameDecayRestart = true;
        on->edgeNpz.frameDecayRestart = true;
        const auto restart = lone<cymbalis::FastEngine>(on, edge, 0.8, edgePos, sr, 3.0, 128);
        std::printf("50 Hz share at 0.8 for comparison: REF's EDGE %.2f %%, the new EDGE with the frame-decay restart on %.2f %%\n",
                    100.0 * share50Hz(mainEdge, sr), 100.0 * share50Hz(restart, sr));
    }
    std::printf("\n%s\n", ok ? "NOVA RIDE edge v2 checks passed." : "A NOVA RIDE edge v2 check FAILED.");
    return ok ? 0 : 1;
}
} // namespace

int main(int argc, char** argv)
{
    noDenormals();
    bool time = false, mechanisms = false, boundMode = false, splashMode = false, novaMode = false;
    int runs = 4;
    std::vector<double> rates;
    std::vector<std::string> bakes;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--time")
            time = true;
        else if (a == "--mechanisms")
            mechanisms = true;
        else if (a == "--bound")
            boundMode = true;
        else if (a == "--splash")
            splashMode = true;
        else if (a == "--nova-edge-v2")
            novaMode = true;
        else if (a == "--runs" && i + 1 < argc)
            runs = std::max(1, std::atoi(argv[++i]));
        else if (a == "--sample-rate" && i + 1 < argc)
            rates.push_back(std::atof(argv[++i]));
        else
            bakes.push_back(a);
    }
    if (bakes.empty())
    {
        std::fprintf(stderr, "usage: check [--mechanisms] BAKE.json ... | check --bound BAKE.json ... | check --splash BAKE.json ... | "
                             "check --time [--runs N] [--sample-rate SR ...] BAKE.json ...\n");
        return 2;
    }
    try
    {
        if (boundMode)
            return bound(bakes);
        if (splashMode)
            return splash(bakes);
        if (novaMode)
        {
            if (bakes.size() != 2)
                throw std::runtime_error("--nova-edge-v2 takes REF's NOVA bake and the working tree's");
            return novaEdgeV2(bakes[0], bakes[1]);
        }
        return time ? timing(bakes, rates, runs) : identity(bakes, mechanisms);
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "check: %s\n", e.what());
        return 1;
    }
}
