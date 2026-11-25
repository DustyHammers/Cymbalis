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
} // namespace

int main(int argc, char** argv)
{
    noDenormals();
    bool time = false, mechanisms = false, boundMode = false;
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
        else if (a == "--runs" && i + 1 < argc)
            runs = std::max(1, std::atoi(argv[++i]));
        else if (a == "--sample-rate" && i + 1 < argc)
            rates.push_back(std::atof(argv[++i]));
        else
            bakes.push_back(a);
    }
    if (bakes.empty())
    {
        std::fprintf(stderr, "usage: check [--mechanisms] BAKE.json ... | check --bound BAKE.json ... | check --time [--runs N] "
                             "[--sample-rate SR ...] BAKE.json ...\n");
        return 2;
    }
    try
    {
        if (boundMode)
            return bound(bakes);
        return time ? timing(bakes, rates, runs) : identity(bakes, mechanisms);
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "check: %s\n", e.what());
        return 1;
    }
}
