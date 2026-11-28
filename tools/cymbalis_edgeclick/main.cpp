// cymbalis_edgeclick — no click at the hand-over from an EDGE strike's contact to its free phase
// (docs/PORT_MAP.md, live-engine decision "An envelope strike's contact plays at its envelope
// level"), in memory, with the plugin's engine (FastEngine). CI runs it on every build.
//
//   cymbalis_edgeclick BAKE.json ...
//
// On every bake with NPZ edge data and a contact phase: 36 EDGE hits, 0.35-1.0 in turn, 3 s apart,
// HIT VARIATION as baked, at 44.1 kHz in 128-sample blocks (EdgeClick.h). Per channel, the largest
// |second difference| over the hand-over and the 2 samples either side against its median over the
// next 2 ms: median, 90th percentile and max of the hits. NOVA RIDE: at most 4x, 8x and 15x. SWISH:
// no worse than before the fix (its figures on main then, below). Bakes without NPZ edge data or a
// contact phase are named and skipped. Exit status 1 if a limit is exceeded.

#include "Bake.h"
#include "EdgeClick.h"
#include "FastEngine.h"

#include <cstdio>
#include <exception>
#include <string>

namespace
{
struct Limit
{
    const char* preset;
    double median, p90, max;
};
// NOVA RIDE: the task's limits. SWISH: its figures before the fix (main at b9dafda, Linux: median
// 2.728, 90th percentile 4.744, max 7.408, the larger channel's), rounded up to 0.1.
constexpr Limit kLimits[] = {
    { "nova_ride", 4.0, 8.0, 15.0 },
    { "swish", 2.8, 4.8, 7.5 },
};

std::string nameOf(const std::string& path)
{
    std::string s = path.substr(path.find_last_of('/') + 1);
    return s.substr(0, s.find('.'));
}
} // namespace

int main(int argc, char** argv)
{
    using namespace cymbalis::edgeclick;
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: cymbalis_edgeclick BAKE.json ...\n");
        return 2;
    }
    bool ok = true;
    std::printf("| Preset | Channel | Median | 90 %% | Max | Limits | |\n|---|---|---|---|---|---|---|\n");
    for (int i = 1; i < argc; ++i)
    {
        std::shared_ptr<const cymbalis::BakeData> bake;
        try
        {
            bake = cymbalis::loadBake(argv[i]);
        }
        catch (const std::exception& e)
        {
            std::fprintf(stderr, "cymbalis_edgeclick: %s: %s\n", argv[i], e.what());
            return 2;
        }
        const std::string name = nameOf(argv[i]);
        const int edge = bake->slotIndex("EDGE");
        if (! bake->edgeNpz.present || edge < 0)
        {
            std::printf("| %s | (no NPZ edge data: skipped) | | | | | |\n", name.c_str());
            continue;
        }
        {
            cymbalis::FastEngine probe(bake);
            probe.prepare(kSampleRate);
            if (probe.contactLength() == 0)
            {
                std::printf("| %s | (no contact phase: skipped) | | | | | |\n", name.c_str());
                continue;
            }
        }
        const Render r = render<cymbalis::FastEngine>(bake, edge);
        const Limit* limit = nullptr;
        for (const auto& l : kLimits)
            if (name == l.preset)
                limit = &l;
        for (int c = 0; c < 2; ++c)
        {
            const Stats s = stats(ratios(c == 0 ? r.L : r.R, r));
            bool pass = true;
            char lim[64] = "(none)";
            if (limit != nullptr)
            {
                pass = s.median <= limit->median && s.p90 <= limit->p90 && s.max <= limit->max;
                std::snprintf(lim, sizeof lim, "%.1f / %.1f / %.1f", limit->median, limit->p90, limit->max);
            }
            std::printf("| %s | %s | %.1f | %.1f | %.1f | %s | %s |\n", name.c_str(), c == 0 ? "L" : "R", s.median, s.p90, s.max, lim,
                        pass ? "ok" : "FAILED");
            ok = ok && pass;
        }
    }
    std::printf("\nThe second difference at the hand-over (+-2 samples) against its median over the next 2 ms, 36 EDGE hits.\n%s\n",
                ok ? "All within their limits." : "A limit is exceeded: FAILED.");
    return ok ? 0 : 1;
}
