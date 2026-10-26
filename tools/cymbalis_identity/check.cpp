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
//   check --time [--runs N] [--sample-rate SR ...] BAKE.json ...
//       Block times of both engines, alternately in one process, each block waiting for its
//       deadline on a real-time thread (as a host's audio thread), the smallest of the runs per
//       block (the engines are deterministic, so block i does the same work in every run): a
//       strike every 24 ms, and flams (two strikes in one block, the second 1 ms later or half
//       a block, whichever is shorter), at 128- and 64-sample blocks (default 44.1 kHz).
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

// One run: every block's time (us) and its number of strikes, after a 1 s lead.
template <class E>
std::vector<double> timeBlocks(const std::shared_ptr<const cymbalis::BakeData>& bake, double sr, int block, bool flams,
                               std::vector<uint8_t>& strikes)
{
    makeRealtime(1000.0 * block / sr);
    const int slots = int(bake->slots.size());
    E e(bake);
    e.prepare(sr);
    std::vector<Hit> hits;
    const int gap = std::min(int(std::lround(0.001 * sr)), block / 2);
    for (int k = 0; k * 0.024 < 5.0; ++k)
    {
        const int s = k % slots;
        if (!flams)
            hits.push_back({int64_t(std::floor(k * 0.024 * sr)), s, kVelocities[k % 7], bake->slots[size_t(s)].position});
        else
        {
            // Both in one block: the first 4 samples into it.
            const int64_t at = (int64_t(std::ceil(k * 0.024 * sr)) + block - 1) / block * block + 4;
            const int s2 = (k + 2) % slots;
            hits.push_back({at, s, kVelocities[k % 7], bake->slots[size_t(s)].position});
            hits.push_back({at + gap, s2, kVelocities[(k + 3) % 7], bake->slots[size_t(s2)].position});
        }
    }
    std::vector<float> L(static_cast<size_t>(block)), R(static_cast<size_t>(block));
    std::vector<double> t;
    strikes.clear();
    const int lead = int(1.0 * sr / block), timed = int(4.0 * sr / block);
    size_t h = 0;
    const auto start = Clock::now();
    for (int b = 0; b < lead + timed; ++b)
    {
        const int64_t from = int64_t(b) * block;
        int pos = 0, n = 0;
        const auto t0 = Clock::now();
        for (; h < hits.size() && hits[h].sample < from + block; ++h, ++n)
        {
            const int at = int(hits[h].sample - from);
            if (at > pos)
            {
                e.process(L.data() + pos, R.data() + pos, at - pos);
                pos = at;
            }
            e.strike(hits[h].slot, hits[h].velocity, hits[h].position);
        }
        if (pos < block)
            e.process(L.data() + pos, R.data() + pos, block - pos);
        const auto t1 = Clock::now();
        if (b >= lead)
        {
            t.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
            strikes.push_back(uint8_t(n));
        }
        std::this_thread::sleep_until(start + std::chrono::duration_cast<Clock::duration>(
                                                  std::chrono::duration<double>(double(b + 1) * block / sr)));
    }
    return t;
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
    std::printf("Block times (us, and %% of the block's duration): main's engine (ref) and this one, "
                "alternately in one process, paced in real time on a real-time thread, the smallest of %d runs "
                "per block; median and worst of the blocks with no strike, one strike, two strikes (a flam).\n\n",
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
                std::vector<double> single[2], flam[2];
                std::vector<uint8_t> sSingle, sFlam;
                for (int r = 0; r < runs; ++r)
                    for (int e = 0; e < 2; ++e)
                    {
                        auto a = e == 0 ? timeBlocks<cymbalis::FastEngineRef>(bake, sr, block, false, sSingle)
                                        : timeBlocks<cymbalis::FastEngine>(bake, sr, block, false, sSingle);
                        auto f = e == 0 ? timeBlocks<cymbalis::FastEngineRef>(bake, sr, block, true, sFlam)
                                        : timeBlocks<cymbalis::FastEngine>(bake, sr, block, true, sFlam);
                        if (r == 0)
                        {
                            single[e] = a;
                            flam[e] = f;
                        }
                        else
                            for (size_t i = 0; i < a.size(); ++i)
                            {
                                single[e][i] = std::min(single[e][i], a[i]);
                                flam[e][i] = std::min(flam[e][i], f[i]);
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
    bool time = false, mechanisms = false;
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
        else if (a == "--runs" && i + 1 < argc)
            runs = std::max(1, std::atoi(argv[++i]));
        else if (a == "--sample-rate" && i + 1 < argc)
            rates.push_back(std::atof(argv[++i]));
        else
            bakes.push_back(a);
    }
    if (bakes.empty())
    {
        std::fprintf(stderr, "usage: check [--mechanisms] BAKE.json ... | check --time [--runs N] [--sample-rate SR ...] BAKE.json ...\n");
        return 2;
    }
    try
    {
        return time ? timing(bakes, rates, runs) : identity(bakes, mechanisms);
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "check: %s\n", e.what());
        return 1;
    }
}
