// Kit.cpp — cymbalis_bench --kit: a kit of cymbals rendered together through EnginePool
// (plugin/dsp/EnginePool.h), on one thread and on several (docs/PORT_MAP.md, "Multi-cymbal
// rendering (step 1)").
//
//   cymbalis_bench --kit [--presets DIR] [--pattern groove|stress ...] [--cymbals N ...]
//                  [--threads T ...] [--sample-rate SR ...] [--block N ...] [--seconds 60]
//                  [--runs 3] [--spin-us 50] [--no-quiet] [--free] [--normal-priority]
//
// The kit, in this order (the first N play): NOVA RIDE, HI-HAT (its pedal moving as in
// --dense, Dense.h), CRASH 16", KONSTAN CRASH 18", STINE FLAT RIDE, RAVEN, SWISH, EXTRA PAPER
// THIN 24", HAMMERED THIN RIDE, SMOKE FLAT RIDE (resources/presets/archive, read as a file).
//
// The patterns, each from fixed seeds (one random sequence per cymbal, so the first N cymbals
// play the same at every N and every sample rate):
//   groove  140 bpm in 4/4. The ride plays 8th notes on BOW, one in four on BELL (at random),
//           velocities 0.45-0.75; the hi-hat 8th notes on TIP, 0.45-0.75. Each other cymbal
//           crashes (EDGE, 0.8-1.0) once every 2 bars, cymbal k (3-10) on beat k - 3 of the two
//           bars. On bar 1 of every 4 the ride, CRASH 16" and KONSTAN hit on the same sample
//           (KONSTAN's crash moves from beat 1 to beat 0). It starts on bar 4, the lead (one
//           bar, in which cymbals 7-10 crash); the timed part starts on bar 1.
//   stress  Every cymbal every 200 ms, on a random row at a random velocity (0.3-1.0); every
//           4th hit on the same sample for every cymbal, the others 20 ms apart from cymbal to
//           cymbal. Lead 0.8 s; the timed part starts on a hit of every cymbal.
//   quiet   10 cymbals: the ride plays its groove part, the nine others are silent (never
//           struck: as rung out, FastEngine::isSilent). Compared with the ride alone (groove,
//           1 cymbal), it shows what a silent cymbal costs per block.
//
// Each run builds fresh engines (so every run of a case starts from the same state) and a pool
// with T threads (the calling thread and T - 1 workers), renders the lead back to back, waits
// for the workers to fall asleep, then renders the timed seconds block by block, each block
// waiting for its deadline, on a real-time thread as --dense does. For a case (pattern, cymbals,
// sample rate, block size), its runs go in turn: run 1 at every thread count, then run 2 ...,
// so a slow moment of the machine falls on one run, not on all of one thread count. Per block
// the smallest of the runs' times is taken (the work is the same in every run).
//
// Per case: the block time's mean, p99, p99.9 and worst as a percentage of the block's
// duration; the blocks over 100 % and over 70 %; the speed-up against one thread (mean, p99);
// the workers' CPU time per second of audio, spinning included, and the part of it spent
// rendering; the wake-up delay (dispatch to a worker starting: median, p99) and how many
// wake-ups found the worker asleep; the blocks the workers were woken for.
//
// It fails (exit status 1) if any run's mix differs from the one-thread mix of its case by a
// single bit in any block (the lead included), or if a sample is not finite. Timing never
// fails it. Nothing is written to disk.
#include "Kit.h"

#include "Bake.h"
#include "Dense.h"
#include "EnginePool.h"
#include "FastEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

namespace
{
using Clock = std::chrono::steady_clock;
using cymbalis::EnginePool;
using cymbalis::FastEngine;

struct KitCymbal
{
    const char* name;
    const char* file; // under the presets directory, without .bake.json
};
constexpr KitCymbal kKit[] = {
    {"NOVA RIDE", "nova_ride"},
    {"HI-HAT", "hihat"},
    {"CRASH 16\"", "azcrash"},
    {"KONSTAN CRASH 18\"", "konstan"},
    {"STINE FLAT RIDE", "stine_flat_ride"},
    {"RAVEN", "raven"},
    {"SWISH", "swish"},
    {"EXTRA PAPER THIN 24\"", "paperthin"},
    {"HAMMERED THIN RIDE", "hammered_thin_ride"},
    {"SMOKE FLAT RIDE", "archive/smoke_flat_ride"},
};
constexpr int kKitSize = int(sizeof(kKit) / sizeof(kKit[0]));

enum class Pattern
{
    groove,
    stress,
    quiet
};
const char* patternName(Pattern p)
{
    return p == Pattern::groove ? "groove" : p == Pattern::stress ? "stress" : "quiet";
}

constexpr double kBeatS = 60.0 / 140.0, kEighthS = kBeatS / 2.0, kBarS = 4.0 * kBeatS;
constexpr double kStressEveryS = 0.2, kStressStaggerS = 0.02;
double leadSeconds(Pattern p)
{
    return p == Pattern::stress ? 4.0 * kStressEveryS : kBarS;
}

// splitmix64: the patterns' random numbers, the same on every platform.
struct Random
{
    uint64_t s;
    uint64_t next()
    {
        uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    double unit() { return double(next() >> 11) * 0x1.0p-53; }
};

struct Hit
{
    int64_t sample;
    int cymbal;
    int slot;     // -1: the hi-hat's pedal
    double value; // velocity, or the pedal's openness
};

struct Kit
{
    std::vector<std::shared_ptr<const cymbalis::BakeData>> bakes;
};

int slotNamed(const cymbalis::BakeData& b, const char* name)
{
    for (size_t i = 0; i < b.slots.size(); ++i)
        if (b.slots[i].name == name)
            return int(i);
    throw std::runtime_error(std::string("no slot ") + name);
}

// The pattern's hits for the first `cymbals` of the kit over [0, totalS), sorted by sample.
std::vector<Hit> makeHits(Pattern p, const Kit& kit, int cymbals, double rate, double totalS)
{
    std::vector<Hit> hits;
    const auto at = [&](double t) { return static_cast<int64_t>(std::floor(t * rate)); };
    const bool groove = p != Pattern::stress;
    const int playing = p == Pattern::quiet ? 1 : cymbals;
    for (int c = 0; c < playing; ++c)
    {
        Random rnd { (groove ? 0x67726F6F7665ull : 0x737472657373ull) ^ (uint64_t(c + 1) * 0x2545F4914F6CDD1Dull) };
        const auto& b = *kit.bakes[size_t(c)];
        if (!groove)
        {
            for (int k = 0;; ++k)
            {
                const double t = kStressEveryS * k + (k % 4 == 0 ? 0.0 : kStressStaggerS * c);
                if (t >= totalS)
                    break;
                const int slot = std::min(int(b.slots.size()) - 1, int(rnd.unit() * double(b.slots.size())));
                hits.push_back({at(t), c, slot, 0.3 + 0.7 * rnd.unit()});
            }
        }
        else if (c == 0 || c == 1)
        {
            const int bow = c == 0 ? slotNamed(b, "BOW") : slotNamed(b, "TIP");
            const int bell = c == 0 ? slotNamed(b, "BELL") : bow;
            for (int m = 0; kEighthS * m < totalS; ++m)
            {
                const int slot = rnd.unit() < 0.25 ? bell : bow;
                hits.push_back({at(kEighthS * m), c, slot, 0.45 + 0.3 * rnd.unit()});
            }
        }
        else
        {
            // Bar B (0 = bar 1 of 4) starts at (B - 3) bars: the pattern starts on bar 4. A
            // crash every two bars, from the even bars.
            const int edge = slotNamed(b, "EDGE");
            for (int bar = 2;; bar += 2)
            {
                const double start = (bar - 3) * kBarS;
                if (start >= totalS)
                    break;
                const int beat = (bar % 4 == 0 && c == 3) ? 0 : c - 2;
                const double t = start + beat * kBeatS;
                if (t >= 0.0 && t < totalS)
                    hits.push_back({at(t), c, edge, 0.8 + 0.2 * rnd.unit()});
            }
        }
    }
    if (p != Pattern::quiet && cymbals > 1 && kit.bakes[1]->hihat.present)
        for (int64_t m = 0; double(m) * kPedalStepS < totalS; ++m)
            hits.push_back({at(double(m) * kPedalStepS), 1, -1, pedalOpenness(double(m) * kPedalStepS)});
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.sample < b.sample; });
    return hits;
}

struct Options
{
    std::string presets = "resources/presets";
    std::vector<Pattern> patterns;
    std::vector<int> cymbals, threads, blocks;
    std::vector<double> rates;
    double seconds = 60.0;
    int runs = 3;
    double spinUs = EnginePool::kDefaultSpinMicros;
    bool quiet = true, paced = true, realtime = true, threadsGiven = false;
};

struct Case
{
    Pattern pattern;
    int cymbals, threads;
    double rate;
    int block;
};

bool sameConfig(const Case& a, const Case& b)
{
    return a.pattern == b.pattern && a.cymbals == b.cymbals && a.rate == b.rate && a.block == b.block;
}

struct Run
{
    std::vector<double> blockUs;       // the timed blocks
    std::vector<uint64_t> hashes;      // every block's mix, the lead included
    double workerCpu = 0.0, workerRender = 0.0; // seconds, in the timed part
    std::vector<float> wakeUs;
    uint64_t wakes = 0, wakesFromSleep = 0, blocks = 0, shared = 0;
    bool realtime = false, workersRealtime = true, finite = true, othersSilent = true;
    float peak = 0.0f;
};

uint64_t hashBlock(const float* l, const float* r, int n)
{
    uint64_t h = 0xCBF29CE484222325ull;
    for (int i = 0; i < n; ++i)
    {
        uint32_t a, b;
        std::memcpy(&a, l + i, 4);
        std::memcpy(&b, r + i, 4);
        h = (h ^ a) * 0x100000001B3ull;
        h = (h ^ b) * 0x100000001B3ull;
    }
    return h;
}

Run runOnce(const Case& c, const Kit& kit, const Options& o)
{
    Run r;
    std::vector<std::unique_ptr<FastEngine>> engines;
    std::vector<FastEngine*> pointers;
    for (int i = 0; i < c.cymbals; ++i)
    {
        engines.push_back(std::make_unique<FastEngine>(kit.bakes[size_t(i)]));
        engines.back()->prepare(c.rate);
        pointers.push_back(engines.back().get());
    }
    EnginePool pool;
    EnginePool::Options po;
    po.threads = c.threads;
    po.spinMicros = o.spinUs;
    po.realtime = o.realtime;
    po.wakeSamples = 1 << 16;
    pool.prepare(pointers, c.rate, c.block, po);
    r.realtime = o.realtime && EnginePool::makeRealtime(1000.0 * c.block / c.rate);

    const double leadS = leadSeconds(c.pattern);
    const int lead = int(std::floor(leadS * c.rate / c.block));
    const int timed = int(std::lround(o.seconds * c.rate / c.block));
    const auto hits = makeHits(c.pattern, kit, c.cymbals, c.rate, double(lead + timed) * c.block / c.rate);
    r.blockUs.reserve(size_t(timed));
    r.hashes.reserve(size_t(lead + timed));
    std::vector<float> L(size_t(c.block)), R(size_t(c.block));
    size_t h = 0;
    double cpu0 = 0.0;
    auto start = Clock::now();
    for (int b = 0; b < lead + timed; ++b)
    {
        if (b == lead)
        {
            pool.waitIdle(100.0);
            pool.resetStats();
            cpu0 = pool.workerCpuSeconds();
            start = Clock::now();
        }
        const int64_t from = int64_t(b) * c.block;
        const auto t0 = Clock::now();
        for (; h < hits.size() && hits[h].sample < from + c.block; ++h)
        {
            const Hit& x = hits[h];
            const int offset = int(x.sample - from);
            if (x.slot < 0)
                pool.pedal(x.cymbal, offset, x.value);
            else
                pool.strike(x.cymbal, offset, x.slot, x.value, kit.bakes[size_t(x.cymbal)]->slots[size_t(x.slot)].position);
        }
        pool.render(L.data(), R.data(), c.block);
        const auto t1 = Clock::now();
        r.hashes.push_back(hashBlock(L.data(), R.data(), c.block));
        for (int i = 0; i < c.block; ++i)
        {
            r.finite = r.finite && std::isfinite(L[size_t(i)]) && std::isfinite(R[size_t(i)]);
            r.peak = std::max({r.peak, std::fabs(L[size_t(i)]), std::fabs(R[size_t(i)])});
        }
        if (b >= lead)
        {
            r.blockUs.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
            if (o.paced)
                std::this_thread::sleep_until(start + std::chrono::duration_cast<Clock::duration>(
                                                          std::chrono::duration<double>(double(b - lead + 1) * c.block / c.rate)));
        }
    }
    pool.waitIdle(100.0);
    r.workerCpu = pool.workerCpuSeconds() - cpu0;
    const auto s = pool.stats();
    r.blocks = s.blocks;
    r.shared = s.sharedBlocks;
    for (const auto& w : s.workers)
    {
        r.wakes += w.wakes;
        r.wakesFromSleep += w.wakesFromSleep;
        r.workerRender += w.renderSeconds;
        r.workersRealtime = r.workersRealtime && w.realtime;
    }
    r.wakeUs = pool.wakeDelaysMicros();
    if (c.pattern == Pattern::quiet)
        for (int i = 1; i < c.cymbals; ++i)
            r.othersSilent = r.othersSilent && engines[size_t(i)]->isSilent();
    return r;
}

struct Stats
{
    double mean = 0.0, p99 = 0.0, p999 = 0.0, worst = 0.0;
};

Stats statsOf(std::vector<double> v)
{
    Stats s;
    if (v.empty())
        return s;
    std::sort(v.begin(), v.end());
    const auto at = [&](double q) { return v[std::min(v.size() - 1, size_t(q * double(v.size())))]; };
    s.p99 = at(0.99);
    s.p999 = at(0.999);
    s.worst = v.back();
    double sum = 0.0;
    for (double x : v)
        sum += x;
    s.mean = sum / double(v.size());
    return s;
}

template <typename T>
T quantile(std::vector<T> v, double q)
{
    if (v.empty())
        return T(0);
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, size_t(q * double(v.size())))];
}

// Per block, the smallest of the runs' times.
std::vector<double> smallest(const std::vector<Run>& runs)
{
    std::vector<double> m = runs[0].blockUs;
    for (size_t r = 1; r < runs.size(); ++r)
        for (size_t i = 0; i < m.size() && i < runs[r].blockUs.size(); ++i)
            m[i] = std::min(m[i], runs[r].blockUs[i]);
    return m;
}

struct Summary
{
    Stats s;
    size_t over100 = 0, over70 = 0;
    double budget = 0.0;
    double cpuPct = 0.0, renderPct = 0.0; // the workers, % of one core
    double wakeMedian = 0.0, wakeP99 = 0.0, asleepPct = 0.0, sharedPct = 0.0;
    bool workers = false;
};

Summary summarise(const Case& c, const std::vector<Run>& runs, const Options& o)
{
    Summary m;
    m.budget = 1e6 * c.block / c.rate;
    const auto series = smallest(runs);
    m.s = statsOf(series);
    for (double x : series)
    {
        m.over100 += x > m.budget ? 1 : 0;
        m.over70 += x > 0.7 * m.budget ? 1 : 0;
    }
    m.workers = c.threads > 1;
    std::vector<float> wake;
    uint64_t wakes = 0, asleep = 0, blocks = 0, shared = 0;
    for (const auto& r : runs)
    {
        m.cpuPct += 100.0 * r.workerCpu / o.seconds / double(runs.size());
        m.renderPct += 100.0 * r.workerRender / o.seconds / double(runs.size());
        wake.insert(wake.end(), r.wakeUs.begin(), r.wakeUs.end());
        wakes += r.wakes;
        asleep += r.wakesFromSleep;
        blocks += r.blocks;
        shared += r.shared;
    }
    m.wakeMedian = quantile(wake, 0.5);
    m.wakeP99 = quantile(wake, 0.99);
    m.asleepPct = wakes > 0 ? 100.0 * double(asleep) / double(wakes) : 0.0;
    m.sharedPct = blocks > 0 ? 100.0 * double(shared) / double(blocks) : 0.0;
    return m;
}

std::string pct(double us, double budget)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f %%", 100.0 * us / budget);
    return buf;
}

std::string rateBlock(const Case& c)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f kHz / %d", c.rate / 1000.0, c.block);
    return buf;
}

int perfCores()
{
#if defined(__APPLE__)
    int v = -1;
    size_t sz = sizeof(v);
    if (sysctlbyname("hw.perflevel0.logicalcpu", &v, &sz, nullptr, 0) == 0 && v > 0)
        return v;
#endif
    return std::max(1u, std::thread::hardware_concurrency());
}

void report(const Options& o, const std::vector<Case>& cases, const std::vector<std::vector<Run>>& runs)
{
    std::vector<Summary> sums;
    for (size_t i = 0; i < cases.size(); ++i)
        sums.push_back(summarise(cases[i], runs[i], o));
    const auto oneThread = [&](size_t i) -> const Summary* {
        for (size_t j = 0; j < cases.size(); ++j)
            if (sameConfig(cases[j], cases[i]) && cases[j].threads == 1)
                return &sums[j];
        return nullptr;
    };

    for (const Pattern p : {Pattern::groove, Pattern::stress})
    {
        if (std::none_of(cases.begin(), cases.end(), [&](const Case& c) { return c.pattern == p; }))
            continue;
        std::printf("\n#### %s\n\n", p == Pattern::groove ? "Groove" : "Stress");
        std::printf("Block time as a percentage of the block's duration (smallest of the %d runs per block). Speed-up: one "
                    "thread's time over this one's. Workers' CPU: all the workers together, %% of one core, spinning "
                    "included, and the part spent rendering. Wake-up: dispatch to a worker starting.\n\n",
                    o.runs);
        std::printf("| Rate / block | Cymbals | Threads | Mean | p99 | p99.9 | Worst | Over 100 %% | Over 70 %% | "
                    "Speed-up: mean / p99 | Workers' CPU: all / rendering | Wake-up: median / p99 (us) | Woken from "
                    "sleep | Blocks shared |\n|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
        for (size_t i = 0; i < cases.size(); ++i)
        {
            const Case& c = cases[i];
            if (c.pattern != p)
                continue;
            const Summary& m = sums[i];
            const Summary* one = oneThread(i);
            char speed[48] = "–", cpu[48] = "–", wake[48] = "–", asleep[16] = "–", shared[16] = "–";
            if (c.threads > 1)
            {
                if (one != nullptr && m.s.mean > 0.0 && m.s.p99 > 0.0)
                    std::snprintf(speed, sizeof(speed), "%.2f / %.2f", one->s.mean / m.s.mean, one->s.p99 / m.s.p99);
                std::snprintf(cpu, sizeof(cpu), "%.1f / %.1f %%", m.cpuPct, m.renderPct);
                if (m.sharedPct > 0.0)
                {
                    std::snprintf(wake, sizeof(wake), "%.1f / %.1f", m.wakeMedian, m.wakeP99);
                    std::snprintf(asleep, sizeof(asleep), "%.0f %%", m.asleepPct);
                }
                std::snprintf(shared, sizeof(shared), "%.0f %%", m.sharedPct);
            }
            std::printf("| %s | %d | %d | %s | %s | %s | %s (%.0f us) | %zu | %zu | %s | %s | %s | %s | %s |\n",
                        rateBlock(c).c_str(), c.cymbals, c.threads, pct(m.s.mean, m.budget).c_str(),
                        pct(m.s.p99, m.budget).c_str(), pct(m.s.p999, m.budget).c_str(), pct(m.s.worst, m.budget).c_str(),
                        m.s.worst, m.over100, m.over70, speed, cpu, wake, asleep, shared);
        }
    }

    if (std::any_of(cases.begin(), cases.end(), [](const Case& c) { return c.pattern == Pattern::quiet; }))
    {
        std::printf("\n#### Quiet cymbals\n\n");
        std::printf("10 cymbals: the ride plays its groove part, the nine others are silent. Against the ride alone (groove, "
                    "1 cymbal, 1 thread): what the nine silent cymbals add per block, and per silent cymbal.\n\n");
        std::printf("| Rate / block | Threads | Mean | p99 | Worst | Ride alone: mean | Nine silent add | Per silent "
                    "cymbal | Blocks shared | Nine silent at the end |\n|---|---|---|---|---|---|---|---|---|---|\n");
        for (size_t i = 0; i < cases.size(); ++i)
        {
            const Case& c = cases[i];
            if (c.pattern != Pattern::quiet)
                continue;
            const Summary& m = sums[i];
            const Summary* alone = nullptr;
            for (size_t j = 0; j < cases.size(); ++j)
                if (cases[j].pattern == Pattern::groove && cases[j].cymbals == 1 && cases[j].threads == 1
                    && cases[j].rate == c.rate && cases[j].block == c.block)
                    alone = &sums[j];
            bool silent = true;
            for (const auto& r : runs[i])
                silent = silent && r.othersSilent;
            char aloneText[48] = "–", add[48] = "–", per[48] = "–";
            if (alone != nullptr)
            {
                const double d = m.s.mean - alone->s.mean;
                std::snprintf(aloneText, sizeof(aloneText), "%s", pct(alone->s.mean, m.budget).c_str());
                std::snprintf(add, sizeof(add), "%.1f us (%s)", d, pct(d, m.budget).c_str());
                std::snprintf(per, sizeof(per), "%.2f us (%s)", d / 9.0, pct(d / 9.0, m.budget).c_str());
            }
            std::printf("| %s | %d | %s | %s | %s | %s | %s | %s | %.0f %% | %s |\n", rateBlock(c).c_str(), c.threads,
                        pct(m.s.mean, m.budget).c_str(), pct(m.s.p99, m.budget).c_str(), pct(m.s.worst, m.budget).c_str(),
                        aloneText, add, per, m.sharedPct, silent ? "yes" : "NO");
        }
    }
}
} // namespace

int runKit(int argc, char** argv)
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
        if (a == "--kit")
            continue;
        if (a == "--presets")
            o.presets = next();
        else if (a == "--pattern")
        {
            const std::string p = next();
            if (p == "groove")
                o.patterns.push_back(Pattern::groove);
            else if (p == "stress")
                o.patterns.push_back(Pattern::stress);
            else
                throw std::runtime_error("unknown pattern " + p);
        }
        else if (a == "--cymbals")
            o.cymbals.push_back(std::min(kKitSize, std::max(1, std::stoi(next()))));
        else if (a == "--threads")
        {
            o.threads.push_back(std::max(1, std::stoi(next())));
            o.threadsGiven = true;
        }
        else if (a == "--sample-rate")
            o.rates.push_back(std::stod(next()));
        else if (a == "--block")
            o.blocks.push_back(std::stoi(next()));
        else if (a == "--seconds")
            o.seconds = std::stod(next());
        else if (a == "--runs")
            o.runs = std::max(1, std::stoi(next()));
        else if (a == "--spin-us")
            o.spinUs = std::max(0.0, std::stod(next()));
        else if (a == "--no-quiet")
            o.quiet = false;
        else if (a == "--free")
            o.paced = false;
        else if (a == "--normal-priority")
            o.realtime = false;
        else
        {
            std::fprintf(stderr, "usage: cymbalis_bench --kit [--presets DIR] [--pattern groove|stress ...] [--cymbals N ...] "
                                 "[--threads T ...] [--sample-rate SR ...] [--block N ...] [--seconds 60] [--runs 3] "
                                 "[--spin-us 50] [--no-quiet] [--free] [--normal-priority]\n");
            return 2;
        }
    }
    const int maxThreads = perfCores();
    if (o.patterns.empty())
        o.patterns = {Pattern::groove, Pattern::stress};
    if (o.cymbals.empty())
        o.cymbals = {1, 2, 4, 6, 10};
    if (o.threads.empty())
        for (int t = 1; t <= maxThreads; ++t)
            o.threads.push_back(t);
    o.threads.push_back(1); // one thread: the reference
    std::sort(o.threads.begin(), o.threads.end());
    o.threads.erase(std::unique(o.threads.begin(), o.threads.end()), o.threads.end());
    if (o.rates.empty())
        o.rates = {44100.0, 96000.0};
    if (o.blocks.empty())
        o.blocks = {128, 64};

    Kit kit;
    for (const auto& k : kKit)
        kit.bakes.push_back(cymbalis::loadBake(o.presets + "/" + k.file + ".bake.json"));

    // The cases, grouped by configuration (pattern, cymbals, rate, block), one thread first.
    // With one cymbal the pool never wakes a worker; it runs at one thread and the most only.
    std::vector<Case> cases;
    for (const double rate : o.rates)
        for (const int block : o.blocks)
        {
            for (const Pattern p : o.patterns)
                for (const int n : o.cymbals)
                    for (const int t : o.threads)
                        if (n > 1 || o.threadsGiven || t == 1 || t == o.threads.back())
                            cases.push_back({p, n, t, rate, block});
            if (o.quiet)
                for (const int t : {1, o.threads.back()})
                    cases.push_back({Pattern::quiet, kKitSize, t, rate, block});
        }
    cases.erase(std::unique(cases.begin(), cases.end(),
                            [](const Case& a, const Case& b) { return sameConfig(a, b) && a.threads == b.threads; }),
                cases.end());

    std::vector<std::vector<Run>> runs(cases.size());
    bool finite = true, realtime = true, workersRealtime = true;
    size_t compared = 0;
    std::vector<std::string> mismatches;
    const auto started = Clock::now();
    for (size_t first = 0; first < cases.size();)
    {
        size_t last = first;
        while (last < cases.size() && sameConfig(cases[last], cases[first]))
            ++last;
        for (int r = 0; r < o.runs; ++r)
            for (size_t ci = first; ci < last; ++ci)
            {
                runs[ci].push_back(runOnce(cases[ci], kit, o));
                const Run& run = runs[ci].back();
                finite = finite && run.finite;
                realtime = realtime && run.realtime;
                if (cases[ci].threads > 1)
                    workersRealtime = workersRealtime && run.workersRealtime;
                if (ci == first && r == 0)
                    continue; // the reference: one thread, run 1
                const Run& ref = runs[first][0];
                ++compared;
                const size_t n = std::min(ref.hashes.size(), run.hashes.size());
                for (size_t b = 0; b < std::max(ref.hashes.size(), run.hashes.size()); ++b)
                    if (b >= n || ref.hashes[b] != run.hashes[b])
                    {
                        char buf[160];
                        std::snprintf(buf, sizeof(buf), "%s, %d cymbals, %s, %d threads, run %d: block %zu differs",
                                      patternName(cases[ci].pattern), cases[ci].cymbals, rateBlock(cases[ci]).c_str(),
                                      cases[ci].threads, r + 1, b);
                        mismatches.push_back(buf);
                        break;
                    }
            }
        first = last;
    }
    const double took = std::chrono::duration<double>(Clock::now() - started).count();

    std::printf("Kit on %s: up to %d cymbals through the engine pool, threads %s (the calling thread included); %.1f s timed "
                "per run after a lead, %d runs per case; %s; calling thread %s, workers %s; workers spin %.0f us, then "
                "sleep; denormals flushed. %zu cases in %.0f s.\n",
                machine().c_str(), kKitSize,
                [&] {
                    std::string s;
                    for (const int t : o.threads)
                        s += (s.empty() ? "" : ", ") + std::to_string(t);
                    return s;
                }().c_str(),
                o.seconds, o.runs, o.paced ? "each block waits for its deadline (real time)" : "blocks back to back",
                realtime ? "real-time" : "at normal priority (real-time not permitted)",
                workersRealtime ? "real-time" : "at normal priority (real-time not permitted)", o.spinUs, cases.size(), took);
    std::printf("\nThe kit, in order:");
    for (int i = 0; i < kKitSize; ++i)
        std::printf("%s %d. %s (%d modes)", i == 0 ? "" : ",", i + 1, kKit[i].name, kit.bakes[size_t(i)]->numModes);
    std::printf(".\n\n- groove: 140 bpm; ride 8ths (BOW, BELL one in four, 0.45-0.75), hi-hat 8ths (TIP) with its pedal "
                "moving, each other cymbal's EDGE once every 2 bars, staggered by a beat; on bar 1 of every 4, ride, "
                "CRASH 16\" and KONSTAN on one sample. Lead: one bar; timed from bar 1.\n"
                "- stress: every cymbal every 200 ms, random rows and velocities (0.3-1.0); every 4th hit on one sample "
                "for every cymbal, the others 20 ms apart. Lead: 0.8 s.\n");
    report(o, cases, runs);
    std::printf("\nBit identity: %zu runs compared with their case's one-thread mix (every block, the lead included): %s.\n",
                compared, mismatches.empty() ? "all bit-identical" : "SOME DIFFER");
    for (const auto& m : mismatches)
        std::printf("- %s\n", m.c_str());
    if (!finite)
        std::fprintf(stderr, "cymbalis_bench: the kit produced a sample that is not finite\n");
    if (!mismatches.empty())
        std::fprintf(stderr, "cymbalis_bench: %zu runs' mixes differ from one thread's\n", mismatches.size());
    return finite && mismatches.empty() ? 0 : 1;
}
