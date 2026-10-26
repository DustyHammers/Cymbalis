// cymbalis_bench — CPU cost of FastEngine, the plugin's real-time engine.
//
//   cymbalis_bench --bake BAKE.json [--sample-rate 48000 ...] [--seconds 60] [--block 256]
//
// Plays every slot of the bake on a busy pattern: a strike every 1/8 s cycling through
// the slots at their default positions, velocities cycling 0.3 .. 1.0, plus a second,
// simultaneous strike on another slot every 0.5 s (10 strikes per second in all). It
// renders --seconds of audio in host-sized blocks, split at each strike as the plugin
// does, and reports the time spent in the engine as a percentage of one core
// (render time / audio time x 100). Denormals are flushed to zero, as in the plugin.
// It also reports the output's peak and fails if any sample is not finite. Nothing is
// written to disk.
//
//   cymbalis_bench --dense --bake BAKE.json [--bake ...] [options]
//
// Dense playing instead: a strike every 24 ms, block times and strike() times (Dense.cpp).
#include "Bake.h"
#include "Dense.h"
#include "FastEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#endif

namespace
{
// Flush denormals to zero, as the plugin does (juce::ScopedNoDenormals in processBlock).
void noDenormals()
{
#if defined(__x86_64__) || defined(_M_X64)
    _mm_setcsr(_mm_getcsr() | 0x8040); // FTZ | DAZ
#elif defined(__aarch64__)
    uint64_t fpcr;
    asm volatile("mrs %0, fpcr" : "=r"(fpcr));
    asm volatile("msr fpcr, %0" : : "r"(fpcr | (uint64_t(1) << 24))); // FZ
#endif
}
} // namespace

int main(int argc, char** argv)
{
    noDenormals();
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--dense")
        {
            try
            {
                return runDense(argc, argv);
            }
            catch (const std::exception& e)
            {
                std::fprintf(stderr, "cymbalis_bench: %s\n", e.what());
                return 1;
            }
        }
    std::string bakePath;
    std::vector<double> rates;
    double seconds = 60.0;
    int block = 256;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("missing value for " + a);
            return argv[++i];
        };
        if (a == "--bake") bakePath = next();
        else if (a == "--sample-rate") rates.push_back(std::stod(next()));
        else if (a == "--seconds") seconds = std::stod(next());
        else if (a == "--block") block = std::stoi(next());
        else
        {
            std::fprintf(stderr, "usage: cymbalis_bench --bake BAKE.json [--sample-rate SR ...] [--seconds S] [--block N]\n");
            return 2;
        }
    }
    if (bakePath.empty()) return 2;
    if (rates.empty()) rates = {44100.0, 48000.0, 88200.0, 96000.0};

    try
    {
        auto bake = cymbalis::loadBake(bakePath);
        const int slots = static_cast<int>(bake->slots.size());
        std::printf("| Sample rate | Control block | Host block | Strikes | CPU (%% of one core) | Mean strike (us) | "
                    "Block p99 (us) | Worst block (us) | Block budget (us) | Peak |\n");
        std::printf("|---|---|---|---|---|---|---|---|---|---|\n");
        bool allFinite = true;
        for (const double sr : rates)
        {
            cymbalis::FastEngine engine(bake);
            engine.prepare(sr);
            const int64_t total = static_cast<int64_t>(seconds * sr);
            struct Hit { int64_t sample; int slot; double velocity; };
            std::vector<Hit> hits;
            const double vel[] = {0.3, 0.45, 0.6, 0.75, 0.9, 1.0, 0.5};
            int k = 0;
            for (double t = 0.0; t < seconds; t += 0.125, ++k)
            {
                const int64_t s = static_cast<int64_t>(t * sr);
                hits.push_back({s, k % slots, vel[k % 7]});
                if (k % 4 == 0) hits.push_back({s, (k / 4 + 2) % slots, vel[(k + 3) % 7]});
            }
            std::vector<float> L(static_cast<size_t>(block)), R(static_cast<size_t>(block));
            size_t h = 0;
            double engineSeconds = 0.0, strikeSeconds = 0.0, worstBlock = 0.0;
            float peak = 0.0f;
            bool finite = true;
            std::vector<double> blockTimes;
            blockTimes.reserve(static_cast<size_t>(total / block + 1));
            using clock = std::chrono::steady_clock;
            for (int64_t start = 0; start < total; start += block)
            {
                const int n = static_cast<int>(std::min<int64_t>(block, total - start));
                const auto b0 = clock::now();
                int pos = 0;
                for (; h < hits.size() && hits[h].sample < start + n; ++h)
                {
                    const int at = static_cast<int>(hits[h].sample - start);
                    if (at > pos)
                    {
                        engine.process(L.data() + pos, R.data() + pos, at - pos);
                        pos = at;
                    }
                    const auto s0 = clock::now();
                    engine.strike(hits[h].slot, hits[h].velocity, bake->slots[size_t(hits[h].slot)].position);
                    strikeSeconds += std::chrono::duration<double>(clock::now() - s0).count();
                }
                if (pos < n) engine.process(L.data() + pos, R.data() + pos, n - pos);
                const double dt = std::chrono::duration<double>(clock::now() - b0).count();
                engineSeconds += dt;
                worstBlock = std::max(worstBlock, dt);
                blockTimes.push_back(dt);
                for (int i = 0; i < n; ++i)
                {
                    finite = finite && std::isfinite(L[size_t(i)]) && std::isfinite(R[size_t(i)]);
                    peak = std::max({peak, std::fabs(L[size_t(i)]), std::fabs(R[size_t(i)])});
                }
            }
            std::sort(blockTimes.begin(), blockTimes.end());
            const double p99 = blockTimes[std::min(blockTimes.size() - 1, blockTimes.size() * 99 / 100)];
            const double audio = static_cast<double>(total) / sr;
            std::printf("| %.0f Hz | %d | %d | %zu | %.2f %% | %.0f | %.0f | %.0f | %.0f | %s |\n", sr,
                        engine.controlBlock(), block, hits.size(), 100.0 * engineSeconds / audio,
                        1e6 * strikeSeconds / double(hits.size()), 1e6 * p99, 1e6 * worstBlock, 1e6 * block / sr,
                        finite ? std::to_string(peak).c_str() : "NOT FINITE");
            allFinite = allFinite && finite;
        }
        if (!allFinite)
        {
            std::fprintf(stderr, "cymbalis_bench: the engine produced a sample that is not finite\n");
            return 1;
        }
        return 0;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "cymbalis_bench: %s\n", e.what());
        return 1;
    }
}
