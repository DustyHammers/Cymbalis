// cymbalis_edgering — an EDGE strike's ring is not cut by the next strike (docs/PORT_MAP.md,
// live-engine decision "An envelope strike keeps its ring when another strike follows"), in
// memory, with the plugin's engine (FastEngine). CI runs it on every build.
//
//   cymbalis_edgering BAKE.json ...
//
// On every bake with NPZ edge data: EDGE at 0.8 alone, against EDGE at 0.8 followed 0.8 s later
// by BOW, and by BELL (STINE: CENTER), at velocity 1e-6 (EdgeRing.h), at 48 kHz in 128-sample
// blocks; and the same with the second strike 1 ms later, off the control grid. Per case the
// edge's level change in seven bands over 0.05-2 s after the second strike, and the difference
// over the 4 ms after it. It fails if a band falls by more than 1 dB or the difference is above
// -30 dB re the edge alone. Bakes without NPZ edge data (the hi-hat) are named and skipped.
// Exit status 1 if a case fails.

#include "Bake.h"
#include "EdgeRing.h"
#include "FastEngine.h"

#include <cstdio>
#include <exception>
#include <string>

namespace
{
constexpr double kBandLimitDb = -1.0;
constexpr double kDipLimitDb = -30.0;

std::string nameOf(const std::string& path)
{
    std::string s = path.substr(path.find_last_of('/') + 1);
    return s.substr(0, s.find('.'));
}
} // namespace

int main(int argc, char** argv)
{
    using namespace cymbalis::edgering;
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: cymbalis_edgering BAKE.json ...\n");
        return 2;
    }
    bool ok = true;
    int cases = 0;
    std::printf("| Preset | Second strike | After | ");
    for (int b = 0; b < kBands; ++b)
        std::printf("%s | ", kBandNames[b]);
    std::printf("Dip (4 ms) | |\n|---|---|---|");
    for (int b = 0; b <= kBands + 1; ++b)
        std::printf("---|");
    std::printf("\n");
    for (int i = 1; i < argc; ++i)
    {
        std::shared_ptr<const cymbalis::BakeData> bake;
        try
        {
            bake = cymbalis::loadBake(argv[i]);
        }
        catch (const std::exception& e)
        {
            std::fprintf(stderr, "cymbalis_edgering: %s: %s\n", argv[i], e.what());
            return 2;
        }
        const int edge = bake->slotIndex("EDGE");
        if (! bake->edgeNpz.present || edge < 0)
        {
            std::printf("| %s | (no NPZ edge data: skipped) | | | | | | | | | | |\n", nameOf(argv[i]).c_str());
            continue;
        }
        for (const char* second : { "BOW", "BELL", "CENTER" })
        {
            const int s = bake->slotIndex(second);
            if (s < 0)
                continue;
            for (const double after : { 0.8, 0.801 })
            {
                const Result r = measure<cymbalis::FastEngine>(bake, edge, s, after);
                bool pass = r.dipDb <= kDipLimitDb;
                std::printf("| %s | %s | %.3f s | ", nameOf(argv[i]).c_str(), second, after);
                for (int b = 0; b < kBands; ++b)
                {
                    std::printf("%+.1f | ", r.band[b]);
                    pass = pass && r.band[b] >= kBandLimitDb;
                }
                std::printf("%.1f dB | %s |\n", r.dipDb, pass ? "ok" : "FAILED");
                ok = ok && pass;
                ++cases;
            }
        }
    }
    std::printf("\nThe edge's level change (dB) 0.05-2 s after the second strike (none may fall by more than 1 dB) and "
                "the difference over the 4 ms after it (dB re the edge alone; at most -30 dB).\n%d cases: %s\n",
                cases, ok ? "all passed" : "a case FAILED");
    return ok ? 0 : 1;
}
