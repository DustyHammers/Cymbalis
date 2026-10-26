// Dense.cpp — cymbalis_bench --dense: FastEngine's block times in dense playing.
//
//   cymbalis_bench --dense --bake BAKE.json [--bake ...] [--sample-rate SR ...] [--block N ...]
//                  [--runs 3] [--lead 1] [--seconds 4] [--ftz-off] [--free] [--normal-priority]
//
// A strike every 24 ms, cycling through the bake's slots at their default positions with
// velocities cycling 0.95, 0.4, 0.8, 0.6, 1.0, 0.3, 0.7, rendered block by block in real time
// (each block waits for its deadline, as a host's audio thread does; --free renders without
// waiting) on a real-time thread where the OS allows it (macOS: a time-constraint thread
// with the block as its period; Linux: SCHED_FIFO, if permitted), denormals flushed to zero
// as in the plugin (processBlock's juce::ScopedNoDenormals). The blocks are split at each
// strike as the plugin does. Engine only: no host, no processor, no editor.
//
// Every case (bake x sample rate x block size, defaults 44.1 and 96 kHz, 128 and 64 samples)
// runs --runs times, the cases in turn, so that a slow moment of the machine falls on one
// run of a case, not all of them. The engine is deterministic (its random phases follow the
// bake's seed), so block i of every run does the same work: the smallest of the runs' times
// for block i is the engine's own cost, and a block slow in one run only is the machine's.
// --ftz-off adds each bake at the first rate and block size with denormals not flushed.
//
// For each case: block time median, p99, p99.9 and worst (and the worst against the block's
// budget), over the smallest-of-the-runs series and over every run; the same for the blocks
// with a strike and those without; strike() alone; and the number of active components. The
// profile build (cymbalis_bench_profile, FastEngine built with CYMBALIS_ENGINE_PROFILE) also
// times the engine's parts: what strike() spends its time on, what the slowest blocks spend
// theirs on, and what a strike into a full component pool does. Nothing is written to disk.
#include "Dense.h"

#include "Bake.h"
#include "EngineProfile.h"
#include "FastEngine.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#include <sys/sysctl.h>
#elif defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#endif

namespace
{
using Clock = std::chrono::steady_clock;

constexpr double kVelocities[] = {0.95, 0.4, 0.8, 0.6, 1.0, 0.3, 0.7};
constexpr int kNumVelocities = 7;
constexpr int kMaxComponents = 24; // FastEngine's component pool

// Denormals flushed to zero (FTZ and DAZ on x86, FZ on arm64), as juce::ScopedNoDenormals
// does, or not.
void setFlushToZero(bool on)
{
#if defined(__x86_64__) || defined(_M_X64)
    const unsigned csr = _mm_getcsr();
    _mm_setcsr(on ? (csr | 0x8040u) : (csr & ~0x8040u));
#elif defined(__aarch64__)
    uint64_t fpcr;
    asm volatile("mrs %0, fpcr" : "=r"(fpcr));
    const uint64_t fz = uint64_t(1) << 24;
    fpcr = on ? (fpcr | fz) : (fpcr & ~fz);
    asm volatile("msr fpcr, %0" : : "r"(fpcr));
#else
    (void) on;
#endif
}

// The calling thread as a host's audio thread: real-time, with the block as its period.
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

std::string machine()
{
    std::string cpu = "unknown CPU";
    const unsigned threads = std::thread::hardware_concurrency();
#if defined(__APPLE__)
    char brand[256] = {};
    size_t size = sizeof(brand);
    if (sysctlbyname("machdep.cpu.brand_string", brand, &size, nullptr, 0) == 0)
        cpu = brand;
    const auto count = [](const char* name) {
        int v = -1;
        size_t sz = sizeof(v);
        return sysctlbyname(name, &v, &sz, nullptr, 0) == 0 ? v : -1;
    };
    const int physical = count("hw.physicalcpu"), perf = count("hw.perflevel0.logicalcpu"),
              eff = count("hw.perflevel1.logicalcpu");
    std::string s = cpu + ", " + std::to_string(threads) + " logical cores, " + std::to_string(physical) + " physical";
    if (perf >= 0)
        s += ", " + std::to_string(perf) + " performance";
    if (eff >= 0)
        s += ", " + std::to_string(eff) + " efficiency";
    return s;
#else
    std::ifstream info("/proc/cpuinfo");
    for (std::string line; std::getline(info, line);)
        if (line.rfind("model name", 0) == 0)
        {
            const size_t colon = line.find(':');
            if (colon != std::string::npos)
                cpu = line.substr(colon + 2);
            break;
        }
    return cpu + ", " + std::to_string(threads) + " logical cores";
#endif
}

struct Options
{
    std::vector<std::string> bakes;
    std::vector<double> rates;
    std::vector<int> blocks;
    int runs = 3;
    double lead = 1.0;     // untimed: the component pool fills
    double seconds = 4.0;  // timed
    double every = 0.024;  // seconds between strikes
    bool ftzOff = false;
    bool paced = true;
    bool realtime = true;
};

struct Case
{
    size_t bake;
    double rate;
    int block;
    bool ftz;
};

#if CYMBALIS_ENGINE_PROFILE
using PartTimes = std::array<uint64_t, cymbalis::profile::kParts>;
#endif

// One run of a case: per timed block its time, its strikes and the active components after
// it; per timed strike its time.
struct Run
{
    std::vector<double> blockUs;
    std::vector<uint8_t> blockStrikes;
    std::vector<double> strikeUs;
    std::vector<int> activeAll; // after every block, the lead included
    bool realtime = false;
    bool finite = true;
    float peak = 0.0f;
#if CYMBALIS_ENGINE_PROFILE
    std::vector<PartTimes> blockParts; // each timed block's time per part
    cymbalis::profile::Totals totals;  // over the timed blocks
#endif
};

int64_t strikeSample(int64_t k, double every, double rate)
{
    return static_cast<int64_t>(std::floor(double(k) * every * rate));
}

Run runOnce(const std::shared_ptr<const cymbalis::BakeData>& bake, const Case& c, const Options& o)
{
    Run r;
    setFlushToZero(c.ftz);
    r.realtime = o.realtime && makeRealtime(1000.0 * c.block / c.rate);
    cymbalis::FastEngine engine(bake);
    engine.prepare(c.rate);
    const int slots = engine.numSlots();
    const int lead = static_cast<int>(std::lround(o.lead * c.rate / c.block));
    const int timed = static_cast<int>(std::lround(o.seconds * c.rate / c.block));
    const size_t strikesTimed = size_t(o.seconds / o.every) + 2;
    r.blockUs.reserve(size_t(timed));
    r.blockStrikes.reserve(size_t(timed));
    r.strikeUs.reserve(strikesTimed);
    r.activeAll.reserve(size_t(lead + timed));
#if CYMBALIS_ENGINE_PROFILE
    r.blockParts.reserve(size_t(timed));
    cymbalis::profile::Totals atLead;
#endif
    std::vector<float> L(size_t(c.block)), R(size_t(c.block));
    int64_t k = 0;
    int64_t next = strikeSample(0, o.every, c.rate);
    const auto start = Clock::now();
    for (int b = 0; b < lead + timed; ++b)
    {
        const bool recording = b >= lead;
#if CYMBALIS_ENGINE_PROFILE
        if (b == lead)
        {
            atLead = cymbalis::profile::totals;
            std::fill(std::begin(cymbalis::profile::totals.maxNs), std::end(cymbalis::profile::totals.maxNs), 0);
        }
        const PartTimes before = [] {
            PartTimes t;
            std::copy(std::begin(cymbalis::profile::totals.ns), std::end(cymbalis::profile::totals.ns), t.begin());
            return t;
        }();
#endif
        const int64_t from = int64_t(b) * c.block;
        int pos = 0, strikes = 0;
        const auto t0 = Clock::now();
        while (next < from + c.block)
        {
            const int at = static_cast<int>(next - from);
            if (at > pos)
            {
                engine.process(L.data() + pos, R.data() + pos, at - pos);
                pos = at;
            }
            const int slot = static_cast<int>(k % slots);
            const auto s0 = Clock::now();
            engine.strike(slot, kVelocities[k % kNumVelocities], bake->slots[size_t(slot)].position);
            const auto s1 = Clock::now();
            if (recording)
                r.strikeUs.push_back(std::chrono::duration<double, std::micro>(s1 - s0).count());
            ++strikes;
            ++k;
            next = strikeSample(k, o.every, c.rate);
        }
        if (pos < c.block)
            engine.process(L.data() + pos, R.data() + pos, c.block - pos);
        const auto t1 = Clock::now();
        if (recording)
        {
            r.blockUs.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
            r.blockStrikes.push_back(uint8_t(strikes));
#if CYMBALIS_ENGINE_PROFILE
            PartTimes d;
            for (size_t i = 0; i < d.size(); ++i)
                d[i] = cymbalis::profile::totals.ns[i] - before[i];
            r.blockParts.push_back(d);
#endif
        }
        r.activeAll.push_back(engine.activeComponents());
        for (int i = 0; i < c.block; ++i)
        {
            r.finite = r.finite && std::isfinite(L[size_t(i)]) && std::isfinite(R[size_t(i)]);
            r.peak = std::max({r.peak, std::fabs(L[size_t(i)]), std::fabs(R[size_t(i)])});
        }
        if (o.paced)
            std::this_thread::sleep_until(start + std::chrono::duration_cast<Clock::duration>(
                                                      std::chrono::duration<double>(double(b + 1) * c.block / c.rate)));
    }
#if CYMBALIS_ENGINE_PROFILE
    const auto& now = cymbalis::profile::totals;
    for (int i = 0; i < cymbalis::profile::kParts; ++i)
    {
        r.totals.ns[i] = now.ns[i] - atLead.ns[i];
        r.totals.calls[i] = now.calls[i] - atLead.calls[i];
        r.totals.maxNs[i] = now.maxNs[i];
    }
    for (int i = 0; i < cymbalis::profile::kEvents; ++i)
        r.totals.events[i] = now.events[i] - atLead.events[i];
#endif
    setFlushToZero(true);
    return r;
}

struct Stats
{
    double median = 0.0, p99 = 0.0, p999 = 0.0, worst = 0.0, mean = 0.0;
    size_t n = 0;
};

Stats statsOf(std::vector<double> v)
{
    Stats s;
    s.n = v.size();
    if (v.empty())
        return s;
    std::sort(v.begin(), v.end());
    const auto at = [&](double q) { return v[std::min(v.size() - 1, size_t(q * double(v.size())))]; };
    s.median = at(0.5);
    s.p99 = at(0.99);
    s.p999 = at(0.999);
    s.worst = v.back();
    double sum = 0.0;
    for (double x : v)
        sum += x;
    s.mean = sum / double(v.size());
    return s;
}

// Per index, the smallest of the runs' values: the engine's own cost.
std::vector<double> smallest(const std::vector<Run>& runs, std::vector<double> Run::*field)
{
    std::vector<double> m = runs[0].*field;
    for (size_t r = 1; r < runs.size(); ++r)
        for (size_t i = 0; i < m.size() && i < (runs[r].*field).size(); ++i)
            m[i] = std::min(m[i], (runs[r].*field)[i]);
    return m;
}

std::vector<double> pooled(const std::vector<Run>& runs, std::vector<double> Run::*field)
{
    std::vector<double> all;
    for (const auto& r : runs)
        all.insert(all.end(), (r.*field).begin(), (r.*field).end());
    return all;
}

std::vector<double> select(const std::vector<double>& v, const std::vector<uint8_t>& strikes, bool withStrike)
{
    std::vector<double> out;
    for (size_t i = 0; i < v.size(); ++i)
        if ((strikes[i] > 0) == withStrike)
            out.push_back(v[i]);
    return out;
}

std::string stem(const std::string& path)
{
    std::string s = path.substr(path.find_last_of('/') + 1);
    const size_t dot = s.find('.');
    return dot == std::string::npos ? s : s.substr(0, dot);
}

std::string caseName(const Options& o, const Case& c)
{
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%s %.1f kHz / %d%s", stem(o.bakes[c.bake]).c_str(), c.rate / 1000.0, c.block,
                  c.ftz ? "" : " (FTZ off)");
    return buf;
}

double budgetUs(const Case& c)
{
    return 1e6 * c.block / c.rate;
}

#if CYMBALIS_ENGINE_PROFILE
// The profile build's tables for one case (its first run).
void reportProfile(const Options& o, const Case& c, const Run& r)
{
    namespace pf = cymbalis::profile;
    const double strikes = double(std::max<uint64_t>(r.totals.calls[pf::strike], 1));
    const double strikeNs = double(r.totals.ns[pf::strike]);
    std::printf("\n#### %s: parts of strike() (%llu strikes)\n\n", caseName(o, c).c_str(),
                static_cast<unsigned long long>(r.totals.calls[pf::strike]));
    std::printf("| Part | Calls per strike | Mean per strike (us) | Longest call (us) | Share of strike() |\n|---|---|---|---|---|\n");
    const auto row = [&](int p, double per) {
        if (r.totals.calls[p] == 0)
            return;
        std::printf("| %s | %.2f | %.1f | %.1f | %.1f %% |\n", pf::partNames[p], double(r.totals.calls[p]) / per,
                    1e-3 * double(r.totals.ns[p]) / per, 1e-3 * double(r.totals.maxNs[p]),
                    strikeNs > 0.0 ? 100.0 * double(r.totals.ns[p]) / strikeNs : 0.0);
    };
    for (int p = pf::strike; p <= pf::merge; ++p)
        row(p, strikes);
    for (int p = pf::freeStartRotate; p <= pf::freeStartModes; ++p)
        row(p, strikes); // inside the strike's free start or the contact's end

    // The slowest 1 % of blocks against all blocks: where the time goes.
    std::vector<size_t> order(r.blockUs.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return r.blockUs[a] > r.blockUs[b]; });
    const size_t spikes = std::max<size_t>(1, order.size() / 100);
    const auto meanOf = [&](const std::vector<size_t>& idx, size_t count, auto value) {
        double sum = 0.0;
        for (size_t i = 0; i < count; ++i)
            sum += value(idx[i]);
        return count > 0 ? sum / double(count) : 0.0;
    };
    std::vector<size_t> all(order);
    const auto part = [&](int p) { return [&r, p](size_t i) { return 1e-3 * double(r.blockParts[i][size_t(p)]); }; };
    const auto block = [&](size_t i) { return r.blockUs[i]; };
    const double spikeBlock = meanOf(order, spikes, block), allBlock = meanOf(all, all.size(), block);
    std::printf("\n#### %s: the slowest 1 %% of blocks (%zu, mean %.0f us) against all (mean %.0f us)\n\n",
                caseName(o, c).c_str(), spikes, spikeBlock, allBlock);
    std::printf("| Part | Slowest blocks (us) | Share | All blocks (us) |\n|---|---|---|---|\n");
    struct Top
    {
        const char* name;
        std::function<double(size_t)> value;
    };
    const auto excl = [&](int outer, int inner) {
        return [&r, outer, inner](size_t i) {
            return 1e-3 * (double(r.blockParts[i][size_t(outer)]) - double(r.blockParts[i][size_t(inner)]));
        };
    };
    const std::vector<Top> tops = {
        {"strike() (all of it)", part(pf::strike)},
        {"contact phase samples (without the free start)", excl(pf::contactSample, pf::freeStart)},
        {"free phase start at the contact's end", part(pf::freeStart)},
        {"control points", part(pf::controlBlock)},
        {"free ring (kernel)", part(pf::kernel)},
        {"onset ramps ending in the block", part(pf::kinks)},
        {"stick clicks", part(pf::stickRender)},
        {"high-pass and output gain", part(pf::highPass)},
    };
    double accounted = 0.0, accountedAll = 0.0;
    for (const auto& t : tops)
    {
        const double s = meanOf(order, spikes, t.value), a = meanOf(all, all.size(), t.value);
        accounted += s;
        accountedAll += a;
        std::printf("| %s | %.0f | %.1f %% | %.0f |\n", t.name, s, spikeBlock > 0.0 ? 100.0 * s / spikeBlock : 0.0, a);
    }
    std::printf("| the rest (outside the parts, the timers) | %.0f | %.1f %% | %.0f |\n", spikeBlock - accounted,
                spikeBlock > 0.0 ? 100.0 * (spikeBlock - accounted) / spikeBlock : 0.0, allBlock - accountedAll);
    for (int p = pf::strikeRebase; p <= pf::strikeFreeStart; ++p)
    {
        const double s = meanOf(order, spikes, part(p)), a = meanOf(all, all.size(), part(p));
        if (s >= 1.0 || a >= 1.0)
            std::printf("| strike():%s | %.0f | %.1f %% | %.0f |\n", pf::partNames[p] + 1, s,
                        spikeBlock > 0.0 ? 100.0 * s / spikeBlock : 0.0, a);
    }
    for (int p = pf::blockCommit; p <= pf::blockGains; ++p)
    {
        const double s = meanOf(order, spikes, part(p)), a = meanOf(all, all.size(), part(p));
        std::printf("| control points:%s | %.0f | %.1f %% | %.0f |\n", pf::partNames[p] + 1, s,
                    spikeBlock > 0.0 ? 100.0 * s / spikeBlock : 0.0, a);
    }
    size_t spikeStrikes = 0, spikeControl = 0;
    for (size_t i = 0; i < spikes; ++i)
    {
        spikeStrikes += r.blockStrikes[order[i]] > 0 ? 1 : 0;
        spikeControl += r.blockParts[order[i]][pf::controlBlock] > 0 ? 1 : 0;
    }
    std::printf("\nOf the slowest blocks, %zu of %zu have a strike and %zu a control point.\n", spikeStrikes, spikes,
                spikeControl);

    std::printf("\n#### %s: component and contact pools\n\n| Event | Count |\n|---|---|\n", caseName(o, c).c_str());
    std::printf("| strikes | %llu |\n", static_cast<unsigned long long>(r.totals.calls[pf::strike]));
    for (int e = 0; e < pf::kEvents; ++e)
        std::printf("| %s | %llu |\n", pf::eventNames[e], static_cast<unsigned long long>(r.totals.events[e]));
    if (r.totals.calls[pf::merge] > 0)
        std::printf("| merge: mean / longest (us) | %.1f / %.1f |\n",
                    1e-3 * double(r.totals.ns[pf::merge]) / double(r.totals.calls[pf::merge]),
                    1e-3 * double(r.totals.maxNs[pf::merge]));
}
#endif

void report(const Options& o, const std::vector<Case>& cases, const std::vector<std::vector<Run>>& runs)
{
    std::printf("\n#### Block time (us), all blocks\n\n");
    std::printf("Smallest of the %d runs per block (the engine's own cost); then every run's blocks together, "
                "and each run's worst.\n\n",
                o.runs);
    std::printf("| Case | Budget | Median | p99 | p99.9 | Worst | Worst / budget | p99 / median | All runs: p99 | "
                "Worst per run |\n|---|---|---|---|---|---|---|---|---|---|\n");
    for (size_t ci = 0; ci < cases.size(); ++ci)
    {
        const auto& c = cases[ci];
        const Stats m = statsOf(smallest(runs[ci], &Run::blockUs));
        const Stats a = statsOf(pooled(runs[ci], &Run::blockUs));
        std::string perRun;
        for (const auto& r : runs[ci])
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%s%.0f", perRun.empty() ? "" : " / ", statsOf(r.blockUs).worst);
            perRun += buf;
        }
        std::printf("| %s | %.0f | %.0f | %.0f | %.0f | %.0f | %.0f %% | %.1f | %.0f | %s |\n", caseName(o, c).c_str(),
                    budgetUs(c), m.median, m.p99, m.p999, m.worst, 100.0 * m.worst / budgetUs(c),
                    m.median > 0.0 ? m.p99 / m.median : 0.0, a.p99, perRun.c_str());
    }

    std::printf("\n#### Blocks with a strike and blocks without (us, smallest of the runs)\n\n");
    std::printf("| Case | With: blocks | Median | p99 | Worst | Without: blocks | Median | p99 | Worst |\n"
                "|---|---|---|---|---|---|---|---|---|\n");
    for (size_t ci = 0; ci < cases.size(); ++ci)
    {
        const auto m = smallest(runs[ci], &Run::blockUs);
        const Stats w = statsOf(select(m, runs[ci][0].blockStrikes, true));
        const Stats wo = statsOf(select(m, runs[ci][0].blockStrikes, false));
        std::printf("| %s | %zu | %.0f | %.0f | %.0f | %zu | %.0f | %.0f | %.0f |\n", caseName(o, cases[ci]).c_str(), w.n,
                    w.median, w.p99, w.worst, wo.n, wo.median, wo.p99, wo.worst);
    }

    std::printf("\n#### strike() alone (us) and the component pool\n\n");
    std::printf("| Case | Strikes per run | Mean | p99 | Worst (smallest of the runs) | Worst (any run) | "
                "Active components: mean | Max | Blocks with the pool full |\n|---|---|---|---|---|---|---|---|---|\n");
    for (size_t ci = 0; ci < cases.size(); ++ci)
    {
        const Stats a = statsOf(pooled(runs[ci], &Run::strikeUs));
        const Stats m = statsOf(smallest(runs[ci], &Run::strikeUs));
        const auto& r0 = runs[ci][0];
        const size_t lead = r0.activeAll.size() - r0.blockUs.size();
        double sum = 0.0;
        int most = 0;
        size_t full = 0;
        for (size_t i = lead; i < r0.activeAll.size(); ++i)
        {
            sum += r0.activeAll[i];
            most = std::max(most, r0.activeAll[i]);
            full += r0.activeAll[i] >= kMaxComponents ? 1 : 0;
        }
        const double blocks = double(r0.activeAll.size() - lead);
        std::printf("| %s | %zu | %.0f | %.0f | %.0f | %.0f | %.1f | %d | %.0f %% |\n", caseName(o, cases[ci]).c_str(),
                    r0.strikeUs.size(), a.mean, a.p99, m.worst, a.worst, sum / blocks, most, 100.0 * double(full) / blocks);
    }

    // The pool over time: the first case of each bake (first run), from the first strike.
    std::printf("\n#### Active components, every 0.25 s from the first strike (first run)\n\n");
    for (size_t ci = 0; ci < cases.size(); ++ci)
    {
        if (ci > 0 && cases[ci].bake == cases[ci - 1].bake)
            continue;
        const auto& r0 = runs[ci][0];
        std::printf("- %s:", caseName(o, cases[ci]).c_str());
        const double per = 0.25 * cases[ci].rate / cases[ci].block;
        for (double b = 0.0; b < double(r0.activeAll.size()); b += per)
            std::printf(" %d", r0.activeAll[size_t(b)]);
        std::printf("\n");
    }

    if (o.ftzOff)
    {
        std::printf("\n#### Denormals flushed (as the plugin) and not (us, smallest of the runs)\n\n");
        std::printf("| Case | FTZ: median | p99 | Worst | Without FTZ: median | p99 | Worst |\n|---|---|---|---|---|---|---|\n");
        for (size_t ci = 0; ci < cases.size(); ++ci)
        {
            if (cases[ci].ftz)
                continue;
            for (size_t cj = 0; cj < cases.size(); ++cj)
                if (cases[cj].ftz && cases[cj].bake == cases[ci].bake && cases[cj].rate == cases[ci].rate
                    && cases[cj].block == cases[ci].block)
                {
                    const Stats on = statsOf(smallest(runs[cj], &Run::blockUs));
                    const Stats off = statsOf(smallest(runs[ci], &Run::blockUs));
                    std::printf("| %s | %.0f | %.0f | %.0f | %.0f | %.0f | %.0f |\n", caseName(o, cases[cj]).c_str(),
                                on.median, on.p99, on.worst, off.median, off.p99, off.worst);
                }
        }
    }

#if CYMBALIS_ENGINE_PROFILE
    for (size_t ci = 0; ci < cases.size(); ++ci)
        reportProfile(o, cases[ci], runs[ci][0]);
#endif
}
} // namespace

int runDense(int argc, char** argv)
{
    Options o;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc)
                throw std::runtime_error("missing value for " + a);
            return argv[++i];
        };
        if (a == "--dense")
            continue;
        if (a == "--bake")
            o.bakes.push_back(next());
        else if (a == "--sample-rate")
            o.rates.push_back(std::stod(next()));
        else if (a == "--block")
            o.blocks.push_back(std::stoi(next()));
        else if (a == "--runs")
            o.runs = std::max(1, std::stoi(next()));
        else if (a == "--lead")
            o.lead = std::stod(next());
        else if (a == "--seconds")
            o.seconds = std::stod(next());
        else if (a == "--ftz-off")
            o.ftzOff = true;
        else if (a == "--free")
            o.paced = false;
        else if (a == "--normal-priority")
            o.realtime = false;
        else
        {
            std::fprintf(stderr, "usage: cymbalis_bench --dense --bake BAKE.json [--bake ...] [--sample-rate SR ...] "
                                 "[--block N ...] [--runs 3] [--lead 1] [--seconds 4] [--ftz-off] [--free] "
                                 "[--normal-priority]\n");
            return 2;
        }
    }
    if (o.bakes.empty())
        return 2;
    if (o.rates.empty())
        o.rates = {44100.0, 96000.0};
    if (o.blocks.empty())
        o.blocks = {128, 64};

    std::vector<std::shared_ptr<const cymbalis::BakeData>> bakes;
    for (const auto& path : o.bakes)
        bakes.push_back(cymbalis::loadBake(path));
    std::vector<Case> cases;
    for (size_t b = 0; b < bakes.size(); ++b)
        for (const double rate : o.rates)
            for (const int block : o.blocks)
                cases.push_back({b, rate, block, true});
    if (o.ftzOff)
        for (size_t b = 0; b < bakes.size(); ++b)
            cases.push_back({b, o.rates[0], o.blocks[0], false});

    std::vector<std::vector<Run>> runs(cases.size());
    bool realtime = true, finite = true;
    for (int r = 0; r < o.runs; ++r)
        for (size_t ci = 0; ci < cases.size(); ++ci)
        {
#if CYMBALIS_ENGINE_PROFILE
            cymbalis::profile::totals = {};
#endif
            runs[ci].push_back(runOnce(bakes[cases[ci].bake], cases[ci], o));
            realtime = realtime && runs[ci].back().realtime;
            finite = finite && runs[ci].back().finite;
        }

    std::printf("Dense playing on %s: a strike every %.0f ms, slots in turn at their default positions, velocities "
                "0.95 0.4 0.8 0.6 1.0 0.3 0.7; %.0f s timed after %.0f s; %d runs per case, the cases in turn; %s; "
                "audio thread %s; denormals flushed unless marked.%s\n",
                machine().c_str(), 1000.0 * o.every, o.seconds, o.lead, o.runs,
                o.paced ? "each block waits for its deadline (real time)" : "blocks back to back",
                realtime ? "real-time" : "at normal priority (real-time not permitted)",
#if CYMBALIS_ENGINE_PROFILE
                " Profile build: the engine's parts are timed, which adds to every time."
#else
                ""
#endif
    );
    for (size_t b = 0; b < bakes.size(); ++b)
        std::printf("- %s: %d modes, %zu slots\n", stem(o.bakes[b]).c_str(), bakes[b]->numModes, bakes[b]->slots.size());
    report(o, cases, runs);
    if (!finite)
    {
        std::fprintf(stderr, "cymbalis_bench: the engine produced a sample that is not finite\n");
        return 1;
    }
    return 0;
}
