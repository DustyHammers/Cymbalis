// Bit-identity of the branch's FastEngine against main's, in memory, over the dense sequence
// (a strike every 24 ms, slots in turn, velocities cycling) followed by a sparse tail.
#include "Bake.h"

#define FastEngine FastEngineMain
#include "main/FastEngine.h"
#undef FastEngine
#include "FastEngine.h"

#include <algorithm>
#include <chrono>
#include <thread>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#elif defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#if defined(__x86_64__)
#include <xmmintrin.h>
#endif

struct Hit { int64_t sample; int slot; double velocity; double position; };

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
            if (at > pos) { e.process(L.data() + start + pos, R.data() + start + pos, at - pos); pos = at; }
            e.strike(hits[h].slot, hits[h].velocity, hits[h].position);
        }
        if (pos < n) e.process(L.data() + start + pos, R.data() + start + pos, n - pos);
    }
}

static bool gPaced = false;
static int gRuns = 3;

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

// Block times of an engine over the dense sequence (no output check), free-running or, with
// gPaced, each block waiting for its deadline on a real-time thread.
template <class E>
std::vector<double> timeBlocks(const std::shared_ptr<const cymbalis::BakeData>& bake, double sr, int block, std::vector<uint8_t>& hasStrike)
{
    if (gPaced)
        makeRealtime(1000.0 * block / sr);
    const double vel[] = {0.95, 0.4, 0.8, 0.6, 1.0, 0.3, 0.7};
    const int slots = int(bake->slots.size());
    E e(bake);
    e.prepare(sr);
    std::vector<float> L(static_cast<size_t>(block)), R(static_cast<size_t>(block));
    std::vector<double> t;
    hasStrike.clear();
    int64_t k = 0, next = 0;
    const int lead = int(1.0 * sr / block), timed = int(4.0 * sr / block);
    const auto start = std::chrono::steady_clock::now();
    for (int b = 0; b < lead + timed; ++b)
    {
        const int64_t from = int64_t(b) * block;
        int pos = 0, strikes = 0;
        const auto t0 = std::chrono::steady_clock::now();
        while (next < from + block)
        {
            const int at = int(next - from);
            if (at > pos) { e.process(L.data() + pos, R.data() + pos, at - pos); pos = at; }
            const int slot = int(k % slots);
            e.strike(slot, vel[k % 7], bake->slots[size_t(slot)].position);
            ++strikes; ++k;
            next = int64_t(std::floor(double(k) * 0.024 * sr));
        }
        if (pos < block) e.process(L.data() + pos, R.data() + pos, block - pos);
        const auto t1 = std::chrono::steady_clock::now();
        if (b >= lead) { t.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count()); hasStrike.push_back(uint8_t(strikes > 0)); }
        if (gPaced)
            std::this_thread::sleep_until(start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                                      std::chrono::duration<double>(double(b + 1) * block / sr)));
    }
    return t;
}

int timing(int argc, char** argv)
{
    for (int i = 2; i < argc; ++i)
    {
        auto bake = cymbalis::loadBake(argv[i]);
        for (double sr : {44100.0, 96000.0})
            for (int block : {128, 64})
            {
                std::vector<double> mo, mn;
                std::vector<uint8_t> st;
                for (int r = 0; r < gRuns; ++r)
                {
                    auto a = timeBlocks<cymbalis::FastEngineMain>(bake, sr, block, st);
                    auto b = timeBlocks<cymbalis::FastEngine>(bake, sr, block, st);
                    if (r == 0) { mo = a; mn = b; }
                    else for (size_t j = 0; j < a.size(); ++j) { mo[j] = std::min(mo[j], a[j]); mn[j] = std::min(mn[j], b[j]); }
                }
                auto stats = [&](const std::vector<double>& v, int sel) {
                    std::vector<double> x;
                    for (size_t j = 0; j < v.size(); ++j) if (sel < 0 || st[j] == sel) x.push_back(v[j]);
                    std::sort(x.begin(), x.end());
                    char buf[128];
                    std::snprintf(buf, sizeof buf, "%5.0f %5.0f %5.0f %5.0f", x[x.size() / 2], x[std::min(x.size() - 1, x.size() * 99 / 100)], x[std::min(x.size() - 1, x.size() * 999 / 1000)], x.back());
                    return std::string(buf);
                };
                std::printf("%-20s %4.1f/%3d  main: all %s | strike %s    new: all %s | strike %s\n",
                            std::string(argv[i]).substr(std::string(argv[i]).find_last_of('/') + 1).c_str(), sr / 1000.0, block,
                            stats(mo, -1).c_str(), stats(mo, 1).c_str(), stats(mn, -1).c_str(), stats(mn, 1).c_str());
            }
    }
    return 0;
}

int main(int argc, char** argv)
{
    // --time [--paced] [--runs N] BAKE...: main's engine against this one, alternately, the
    // smallest of the runs per block: median, p99, p99.9 and worst (us), all blocks | blocks
    // with a strike.
    if (argc > 1 && std::string(argv[1]) == "--time")
    {
        std::vector<char*> rest = {argv[0], argv[1]};
        for (int i = 2; i < argc; ++i)
        {
            const std::string a = argv[i];
            if (a == "--paced") gPaced = true;
            else if (a == "--runs" && i + 1 < argc) gRuns = std::atoi(argv[++i]);
            else rest.push_back(argv[i]);
        }
        std::printf("columns: median p99 p99.9 worst (us), smallest of %d runs per block, %s\n", gRuns,
                    gPaced ? "paced in real time on a real-time thread" : "blocks back to back");
        return timing(int(rest.size()), rest.data());
    }
#if defined(__x86_64__)
    _mm_setcsr(_mm_getcsr() | 0x8040);
#elif defined(__aarch64__)
    uint64_t fpcr; asm volatile("mrs %0, fpcr" : "=r"(fpcr)); asm volatile("msr fpcr, %0" : : "r"(fpcr | (uint64_t(1) << 24)));
#endif
    const double vel[] = {0.95, 0.4, 0.8, 0.6, 1.0, 0.3, 0.7};
    bool allSame = true;
    double worstRel = 0.0;
    // --mechanisms: the energy cascade (which draws from the strike-phase sequence), loud-band
    // and re-hit damping on, through setTuning, the same for both engines.
    bool mech = false;
    int first = 1;
    if (argc > 1 && std::string(argv[1]) == "--mechanisms") { mech = true; first = 2; }
    for (int i = first; i < argc; ++i)
    {
        auto bake = cymbalis::loadBake(argv[i]);
        const int slots = int(bake->slots.size());
        cymbalis::Tuning t = bake->tuning;
        t.cymbal.cascadeAmount = 0.8;
        t.cymbal.cascadeThresholdDb = -40.0;
        t.cymbal.loudDampAmount = 1.5;
        t.cymbal.loudDampThresholdDb = -30.0;
        t.cymbal.rehitDampAmount = 0.6;
        for (double sr : {44100.0, 96000.0})
            for (int block : {128, 64})
            {
                std::vector<Hit> hits;
                int k = 0;
                for (; k * 0.024 < 5.0; ++k) // dense: 5 s
                    hits.push_back({int64_t(std::floor(k * 0.024 * sr)), k % slots, vel[k % 7], bake->slots[size_t(k % slots)].position});
                // sparse tail: after 1.5 s of ringing, strikes 0.4 s apart, positions in between
                for (int j = 0; j < 8; ++j, ++k)
                    hits.push_back({int64_t((6.5 + 0.4 * j) * sr), k % slots, vel[k % 7], 0.15 * j});
                const int64_t total = int64_t(11.0 * sr);
                cymbalis::FastEngineMain a(bake);
                cymbalis::FastEngine b(bake);
                a.prepare(sr);
                b.prepare(sr);
                if (mech)
                {
                    a.setTuning(t, nullptr);
                    b.setTuning(t, nullptr);
                }
                std::vector<float> La, Ra, Lb, Rb;
                render(a, hits, total, block, La, Ra);
                render(b, hits, total, block, Lb, Rb);
                double peak = 0.0, diff = 0.0;
                size_t differing = 0, firstDiff = SIZE_MAX;
                for (size_t s = 0; s < La.size(); ++s)
                {
                    peak = std::max({peak, double(std::fabs(La[s])), double(std::fabs(Ra[s]))});
                    const double d = std::max(std::fabs(double(La[s]) - Lb[s]), std::fabs(double(Ra[s]) - Rb[s]));
                    const bool same = std::memcmp(&La[s], &Lb[s], 4) == 0 && std::memcmp(&Ra[s], &Rb[s], 4) == 0;
                    if (!same) { ++differing; firstDiff = std::min(firstDiff, s); }
                    diff = std::max(diff, d);
                }
                const double rel = peak > 0.0 ? diff / peak : 0.0;
                worstRel = std::max(worstRel, rel);
                allSame = allSame && differing == 0;
                std::printf("%-28s %6.1f kHz block %3d: %s  peak %.4f  max |diff| / peak %.3g  samples differing %zu",
                            std::string(argv[i]).substr(std::string(argv[i]).find_last_of('/') + 1).c_str(), sr / 1000.0, block,
                            differing == 0 ? "bit-identical" : "DIFFERENT    ", peak, rel, differing);
                if (differing) std::printf(" (first at %.4f s)", double(firstDiff) / sr);
                std::printf("\n");
            }
    }
    std::printf("%s; largest |diff| / peak %.3g\n", allSame ? "ALL BIT-IDENTICAL" : "NOT bit-identical", worstRel);
    return allSame ? 0 : 1;
}
