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
//   check --chain BAKE.json ...
//       The sizzle chain (docs/PORT_MAP.md, "Sizzle chain"), the working tree's engine alone, in
//       memory at 48 kHz, 128-sample blocks, HIT VARIATION off, every bake with a chain block: the
//       sizzle alone (chain on minus chain on with the sizzle muted, which keeps the chain's
//       damping), mono mean of L and R, dB re full scale after the output gain, 0.3-0.8 s after a
//       lone strike on EDGE, BOW and BELL (STINE: CENTER) at 0.9 and 0.5, against the approved
//       prototype's where it has them (reported; about +-3 dB expected); when the gate shuts; the
//       order (EDGE above BOW above BELL at both velocities, soft hits quieter and stopping sooner,
//       the sizzle gone 0.6 s after the gate shuts); the stereo (the sizzle's L/R balance within
//       1 dB of the cymbal's own in the sizzle's band, 3.5-16 kHz; its L-R correlation against the
//       cymbal's there, reported for every row); switching while a BOW hit rings (on to off and
//       off to on at 1 s, against the sizzle muted and switched alike, as the damping follows the
//       switch: no sample-to-sample step larger than the sizzle's own before or after, nothing
//       added 50 ms after switching off, nothing before switching on). Exit status 1 if an
//       order, balance or switching check fails; where the task's expectation and the spec's
//       formulas disagree (a soft hit stopping later, the correlation), a NOTE.
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
#define SizzleChain SizzleChainRef // REF's own sizzle chain, if it has one (run.sh)
#include "ref/FastEngine.h"
#undef SizzleChain
#undef FastEngine
#include "FastEngine.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
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

// --chain ------------------------------------------------------------------------------------

struct Stereo
{
    std::vector<float> L, R;
};

// A lone strike on `slot` at `v`, `seconds` rendered at 48 kHz in 128-sample blocks, the chain
// `on`; at `switchAt` s (a block boundary) the switch goes to `switchTo`. `gateShut`: the first
// block end after the strike with the gate shut (s), or -1.
Stereo chainRender(const std::shared_ptr<const cymbalis::BakeData>& b, int slot, double v, double seconds, bool on,
                   double switchAt = -1.0, bool switchTo = false, double* gateShut = nullptr)
{
    constexpr double sr = 48000.0;
    constexpr int block = 128;
    cymbalis::FastEngine e(b);
    e.prepare(sr);
    e.setChain(on);
    e.strike(slot, v, b->slots[size_t(slot)].position);
    const int64_t n = int64_t(seconds * sr), sw = switchAt < 0.0 ? -1 : int64_t(switchAt * sr) / block * block;
    Stereo out;
    out.L.assign(size_t(n), 0.0f);
    out.R.assign(size_t(n), 0.0f);
    if (gateShut != nullptr)
        *gateShut = -1.0;
    for (int64_t i = 0; i < n; i += block)
    {
        if (i == sw)
            e.setChain(switchTo);
        const int m = int(std::min<int64_t>(block, n - i));
        e.process(out.L.data() + i, out.R.data() + i, m);
        if (gateShut != nullptr && *gateShut < 0.0 && e.chainGate() <= 0.0)
            *gateShut = double(i + m) / sr;
    }
    return out;
}

// The 4th-order Butterworth band-pass 3.5-16 kHz (the sizzle's modes) of a 48 kHz signal: two
// high-pass and two low-pass biquads (RBJ), in double.
std::vector<float> sizzleBand(const std::vector<float>& x)
{
    struct Biquad
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
    auto make = [](bool high, double f, double q) {
        const double w = 2.0 * 3.14159265358979323846 * f / 48000.0, c = std::cos(w), al = std::sin(w) / (2.0 * q), a0 = 1.0 + al;
        const double k = high ? (1.0 + c) / 2.0 : (1.0 - c) / 2.0;
        return Biquad { k / a0, (high ? -2.0 * k : 2.0 * k) / a0, k / a0, -2.0 * c / a0, (1.0 - al) / a0 };
    };
    Biquad f[4] = { make(true, 3500.0, 0.5412), make(true, 3500.0, 1.3066), make(false, 16000.0, 0.5412), make(false, 16000.0, 1.3066) };
    std::vector<float> y(x.size());
    for (size_t i = 0; i < x.size(); ++i)
    {
        double v = x[i];
        for (auto& b : f)
            v = b.run(v);
        y[i] = float(v);
    }
    return y;
}

// Mean square of the mono mean of (a - b) (b may be null) over [t0, t1) s at 48 kHz, in dB.
double monoDb(const Stereo& a, const Stereo* b, double t0, double t1)
{
    const size_t i0 = size_t(t0 * 48000.0), i1 = std::min(a.L.size(), size_t(t1 * 48000.0));
    double sum = 0.0;
    for (size_t i = i0; i < i1; ++i)
    {
        double m = 0.5 * (double(a.L[i]) + a.R[i]);
        if (b != nullptr)
            m -= 0.5 * (double(b->L[i]) + b->R[i]);
        sum += m * m;
    }
    return 10.0 * std::log10(std::max(sum / double(std::max<size_t>(1, i1 - i0)), 1e-300));
}

int chain(const std::vector<std::string>& bakes)
{
    // The approved prototype's sizzle alone, dB re full scale (the task's table).
    struct Proto
    {
        const char* preset;
        const char* row;
        double v09, v05;
    };
    const Proto proto[] = {
        { "stine_flat_ride", "EDGE", -41, -47 },       { "stine_flat_ride", "BOW", -45, -52 },
        { "stine_flat_ride", "CENTER", -55, -61 },     { "hammered_thin_ride", "EDGE", -40, -45 },
        { "hammered_thin_ride", "BOW", -44, -50 },     { "hammered_thin_ride", "BELL", -52, -60 },
    };
    bool ok = true;
    auto fail = [&](const std::string& what) {
        std::printf("  FAILED: %s\n", what.c_str());
        ok = false;
    };
    // Where the spec's formulas themselves give another result than the task expects: reported.
    auto note = [](const std::string& what) { std::printf("  NOTE: %s\n", what.c_str()); };
    for (const auto& path : bakes)
    {
        auto loaded = cymbalis::loadBake(path);
        if (!loaded->chain.present)
        {
            std::printf("%s: no sizzle chain, skipped\n", name(path).c_str());
            continue;
        }
        auto base = std::make_shared<cymbalis::BakeData>(*loaded);
        base->tuning.cymbal.hitVariation = false;
        auto muted = std::make_shared<cymbalis::BakeData>(*base); // the chain's damping, no sizzle
        muted->tuning.cymbal.chainLevelDb = -400.0;
        std::printf("\n%s: ref_ms %.6g, %d resonators (3.5-16 kHz), %zu swing modes\n", name(path).c_str(), base->chain.refMs,
                    cymbalis::FastEngine(base).chainResonators(), size_t(cymbalis::SizzleChain::kSwingModes));
        std::printf("| Row | Velocity | Sizzle 0.3-0.8 s | Prototype | Difference | Cymbal (damped) | Gate shut | Sizzle 0.6-0.8 s after |\n");
        std::printf("|---|---|---|---|---|---|---|---|\n");
        std::vector<std::pair<std::string, std::array<double, 2>>> levels;
        for (const char* row : { "EDGE", "BOW", "BELL", "CENTER" })
        {
            const int slot = base->slotIndex(row);
            if (slot < 0)
                continue;
            std::array<double, 2> lv {}, sh {};
            for (int k = 0; k < 2; ++k)
            {
                const double v = k == 0 ? 0.9 : 0.5;
                double shut = -1.0;
                const auto on = chainRender(base, slot, v, 14.0, true, -1.0, false, &shut);
                const auto off = chainRender(muted, slot, v, 14.0, true);
                lv[size_t(k)] = monoDb(on, &off, 0.3, 0.8);
                sh[size_t(k)] = shut;
                const double after = shut > 0.0 ? monoDb(on, &off, shut + 0.6, shut + 0.8) : 0.0;
                std::string protoText = "-", diffText = "-";
                for (const auto& p : proto)
                    if (name(path) == p.preset && std::string(row) == p.row)
                    {
                        const double want = k == 0 ? p.v09 : p.v05;
                        char buf[32];
                        std::snprintf(buf, sizeof(buf), "%.0f dB", want);
                        protoText = buf;
                        std::snprintf(buf, sizeof(buf), "%+.1f dB", lv[size_t(k)] - want);
                        diffText = buf;
                    }
                char shutText[32];
                std::snprintf(shutText, sizeof(shutText), shut > 0.0 ? "%.2f s" : "over 14 s", shut);
                std::printf("| %s | %.1f | %.1f dB | %s | %s | %.1f dB | %s | %s |\n", row, v, lv[size_t(k)], protoText.c_str(),
                            diffText.c_str(), monoDb(off, nullptr, 0.3, 0.8), shutText,
                            shut > 0.0 ? (std::to_string(int(std::lround(after - lv[size_t(k)]))) + " dB re 0.3-0.8 s").c_str() : "-");
                if (shut > 0.0 && !(after < lv[size_t(k)] - 50.0))
                    fail(std::string(row) + " at " + std::to_string(v) + ": the sizzle goes on after the gate shuts");
            }
            if (!(lv[1] < lv[0]))
                fail(std::string(row) + ": the soft hit is not quieter");
            if (!(sh[1] > 0.0 && (sh[0] < 0.0 || sh[1] < sh[0])))
                note(std::string(row) + ": the soft hit stops later than the hard one (its displacement, from its own modes' "
                                        "amplitudes, decays more slowly after 0.5 s)");
            levels.push_back({ row, lv });
        }
        for (int k = 0; k < 2; ++k)
            for (size_t r = 1; r < levels.size(); ++r)
                if (!(levels[r - 1].second[size_t(k)] > levels[r].second[size_t(k)]))
                    fail(levels[r - 1].first + " is not above " + levels[r].first + " at " + (k == 0 ? "0.9" : "0.5"));

        // Stereo: the sizzle's L/R balance and correlation against the cymbal's own (BOW at 0.9):
        // the cymbal's whole output, reported, and its 3.5-16 kHz band (the modes the sizzle rings
        // on, with the engine's own gains for them), checked.
        {
            const int slot = base->slotIndex("BOW");
            const auto on = chainRender(base, slot, 0.9, 0.8, true);
            const auto off = chainRender(muted, slot, 0.9, 0.8, true);
            auto stats = [](const Stereo& a, const Stereo* b, double& balance, double& corr) {
                double ll = 0.0, rr = 0.0, lr = 0.0;
                for (size_t i = size_t(0.3 * 48000.0); i < size_t(0.8 * 48000.0); ++i)
                {
                    const double l = double(a.L[i]) - (b != nullptr ? b->L[i] : 0.0f);
                    const double r = double(a.R[i]) - (b != nullptr ? b->R[i] : 0.0f);
                    ll += l * l;
                    rr += r * r;
                    lr += l * r;
                }
                balance = 10.0 * std::log10(ll / rr);
                corr = lr / std::sqrt(ll * rr);
            };
            double sb = 0.0, sc = 0.0, cb = 0.0, cc = 0.0, hb = 0.0, hc = 0.0;
            stats(on, &off, sb, sc);
            stats(off, nullptr, cb, cc);
            const Stereo band { sizzleBand(off.L), sizzleBand(off.R) };
            stats(band, nullptr, hb, hc);
            std::printf("Stereo (BOW at 0.9, 0.3-0.8 s): sizzle L/R %+.2f dB, correlation %.3f; the cymbal's own 3.5-16 kHz L/R "
                        "%+.2f dB, correlation %.3f (its whole output: %+.2f dB, %.3f)\n", sb, sc, hb, hc, cb, cc);
            for (const char* row : { "EDGE", "BELL", "CENTER" })
                if (const int r = base->slotIndex(row); r >= 0)
                {
                    const auto o = chainRender(muted, r, 0.9, 0.8, true), on2 = chainRender(base, r, 0.9, 0.8, true);
                    const Stereo ob { sizzleBand(o.L), sizzleBand(o.R) };
                    double b2 = 0.0, c2 = 0.0, b3 = 0.0, c3 = 0.0;
                    stats(ob, nullptr, b2, c2);
                    stats(on2, &o, b3, c3);
                    std::printf("  %s at 0.9: sizzle L/R %+.2f dB, correlation %.3f; the cymbal's own 3.5-16 kHz L/R %+.2f dB, correlation %.3f\n",
                                row, b3, c3, b2, c2);
                }
            if (!(std::fabs(sb - hb) <= 1.0))
                fail("the sizzle's L/R balance is more than 1 dB from the cymbal's (3.5-16 kHz)");
            if (!(std::fabs(sc - hc) <= 0.1))
                note("the sizzle's L-R correlation is more than 0.1 from the BOW hit's (3.5-16 kHz): the sizzle has one image "
                     "(fixed random weights over every mode in the band, the engine's own gains), the cymbal's moves with the strike "
                     "(the rows below)");
        }

        // Switching while a BOW hit at 0.9 rings, at 1 s (a block boundary at 48 kHz / 128: 375 blocks).
        {
            const int slot = base->slotIndex("BOW");
            const size_t at = size_t(1.0 * 48000.0), fade = size_t(cymbalis::SizzleChain::kFadeS * 48000.0);
            auto maxStep = [](const Stereo& a, const Stereo& b, size_t i0, size_t i1) {
                double m = 0.0;
                for (size_t i = std::max<size_t>(i0, 1); i < i1; ++i)
                    for (int c = 0; c < 2; ++c)
                    {
                        const auto& x = c == 0 ? a.L : a.R;
                        const auto& y = c == 0 ? b.L : b.R;
                        m = std::max(m, std::fabs((double(x[i]) - y[i]) - (double(x[i - 1]) - y[i - 1])));
                    }
                return m;
            };
            auto maxAbs = [](const Stereo& a, const Stereo& b, size_t i0, size_t i1) {
                double m = 0.0;
                for (size_t i = i0; i < i1; ++i)
                    m = std::max({ m, std::fabs(double(a.L[i]) - b.L[i]), std::fabs(double(a.R[i]) - b.R[i]) });
                return m;
            };
            // On to off: against the muted render switched alike (the same damping, which follows
            // the switch): the sizzle fading out.
            const auto onOff = chainRender(base, slot, 0.9, 1.5, true, 1.0, false);
            const auto ref = chainRender(muted, slot, 0.9, 1.5, true);
            const auto refOnOff = chainRender(muted, slot, 0.9, 1.5, true, 1.0, false);
            const double before = maxStep(onOff, refOnOff, at - size_t(0.2 * 48000.0), at), during = maxStep(onOff, refOnOff, at, at + fade);
            // After the fade the sizzle adds nothing: the left channel bit for bit; the right one
            // within float rounding (the right-channel high-pass's state still holds a trace of
            // it, which decays with the filter's own response).
            double leftAfter = 0.0, rightAfter = 0.0, peak = 0.0;
            for (size_t i = 0; i < onOff.L.size(); ++i)
            {
                peak = std::max({ peak, std::fabs(double(refOnOff.L[i])), std::fabs(double(refOnOff.R[i])) });
                if (i >= at + fade + 128)
                {
                    leftAfter = std::max(leftAfter, std::fabs(double(onOff.L[i]) - refOnOff.L[i]));
                    rightAfter = std::max(rightAfter, std::fabs(double(onOff.R[i]) - refOnOff.R[i]));
                }
            }
            // Off to on: against the muted render switched alike (the ringing hit takes the
            // damping at the switch): the sizzle fading in, at once (its drive was tracked while
            // off). Before the switch, against the chain off throughout.
            const auto offOn = chainRender(base, slot, 0.9, 1.5, false, 1.0, true);
            const auto refOffOn = chainRender(muted, slot, 0.9, 1.5, false, 1.0, true);
            const auto plain = chainRender(base, slot, 0.9, 1.5, false);
            const auto onFromStart = chainRender(base, slot, 0.9, 1.5, true);
            const double early = maxAbs(offOn, plain, 0, at), into = maxStep(offOn, refOffOn, at, at + fade),
                         after = maxStep(offOn, refOffOn, at + fade, at + fade + size_t(0.2 * 48000.0));
            const double levelIn = monoDb(offOn, &refOffOn, 1.1, 1.4), levelOn = monoDb(onFromStart, &ref, 1.1, 1.4);
            std::printf("Switching at 1 s (BOW at 0.9): on to off, largest step %.3g in the fade against %.3g before, largest "
                        "difference after the fade %.3g left, %.3g right (%.0f dB re the peak); off to on, largest difference before "
                        "%.3g, largest step %.3g in the fade against %.3g after, sizzle 1.1-1.4 s %.1f dB (on from the start: "
                        "%.1f dB)\n",
                        during, before, leftAfter, rightAfter, db(rightAfter / peak), early, into, after, levelIn, levelOn);
            if (!(during <= 1.2 * before))
                fail("switching off steps more than the sizzle itself");
            if (!(leftAfter == 0.0 && rightAfter <= 3e-6 * peak))
                fail("the sizzle goes on after switching off and its 50 ms fade");
            if (!(early == 0.0))
                fail("the chain switched off changes the output");
            if (!(into <= 1.2 * after))
                fail("switching on steps more than the sizzle itself");
        }
    }
    std::printf("\n%s\n", ok ? "Sizzle chain checks passed." : "A sizzle chain check FAILED.");
    return ok ? 0 : 1;
}
} // namespace

int main(int argc, char** argv)
{
    noDenormals();
    bool time = false, mechanisms = false, boundMode = false, splashMode = false, chainMode = false;
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
        else if (a == "--chain")
            chainMode = true;
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
                             "check --chain BAKE.json ... | check --time [--runs N] [--sample-rate SR ...] BAKE.json ...\n");
        return 2;
    }
    try
    {
        if (boundMode)
            return bound(bakes);
        if (splashMode)
            return splash(bakes);
        if (chainMode)
            return chain(bakes);
        return time ? timing(bakes, rates, runs) : identity(bakes, mechanisms);
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "check: %s\n", e.what());
        return 1;
    }
}
