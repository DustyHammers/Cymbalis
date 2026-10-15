// cymbalis_trims — level trims measured with FastEngine, the plugin's own engine.
//
//   cymbalis_trims --bake BAKE.json [--reference NOVA_BAKE.json] [--velocity V] [--window S]
//                  [--target SLOT=DB ...] [--json TRIMS.json]
//
// For every slot of the bake (one per overlay: each row and stick choice of the plugin
// plays one of them), a fresh engine with no level trim plays one strike of velocity V
// (default 0.9) at the slot's base position at t = 0. The RMS of the first S seconds
// (default 1; both channels, i.e. the mean level) is compared with the same strike on BOW
// (tip): the bake's own bow_tip slot or, with --reference, the bow_tip slot of that bake
// (Nova Ride), so every preset is matched to the same level. The trim puts the slot at its
// target level relative to BOW (tip): trim = target + 20 log10(RMS(BOW tip) / RMS(slot)),
// with a target of 0 dB unless --target gives one. The trims are printed and, with --json,
// written for tools/set_level_trims.py, which stores them in the bake. The audio is only
// measured in memory; nothing is written but the JSON.
#include "Bake.h"
#include "FastEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
constexpr const char* kReferenceOverlay = "bow_tip"; // Nova BOW (tip)
constexpr double kReferencePosition = 0.5;

// RMS of the first windowSeconds of one strike; its peak over that window goes to *peak.
double strikeRms(const std::shared_ptr<const cymbalis::BakeData>& bake, int slot, double sampleRate, double velocity,
                 double windowSeconds, double* peak = nullptr)
{
    cymbalis::FastEngine engine(bake);
    engine.prepare(sampleRate);
    engine.strike(slot, velocity, bake->slots[size_t(slot)].position);
    const long total = std::lround(windowSeconds * sampleRate);
    constexpr int block = 512;
    std::vector<float> L(block), R(block);
    double sum = 0.0, top = 0.0;
    for (long start = 0; start < total; start += block)
    {
        const int n = static_cast<int>(std::min<long>(block, total - start));
        engine.process(L.data(), R.data(), n);
        for (int i = 0; i < n; ++i)
        {
            sum += double(L[size_t(i)]) * L[size_t(i)] + double(R[size_t(i)]) * R[size_t(i)];
            top = std::max({top, std::fabs(double(L[size_t(i)])), std::fabs(double(R[size_t(i)]))});
        }
    }
    if (peak != nullptr)
        *peak = top;
    return std::sqrt(sum / (2.0 * double(total)));
}
} // namespace

int main(int argc, char** argv)
{
    std::string bakePath, jsonPath, referencePath;
    double velocity = 0.9, windowSeconds = 1.0;
    std::map<std::string, double> targets;
    bool usage = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--bake" && i + 1 < argc) bakePath = argv[++i];
        else if (a == "--json" && i + 1 < argc) jsonPath = argv[++i];
        else if (a == "--reference" && i + 1 < argc) referencePath = argv[++i];
        else if (a == "--velocity" && i + 1 < argc) velocity = std::stod(argv[++i]);
        else if (a == "--window" && i + 1 < argc) windowSeconds = std::stod(argv[++i]);
        else if (a == "--target" && i + 1 < argc)
        {
            const std::string t = argv[++i];
            const auto eq = t.find('=');
            if (eq == std::string::npos || eq == 0) usage = true;
            else targets[t.substr(0, eq)] = std::stod(t.substr(eq + 1));
        }
        else usage = true;
    }
    if (usage || bakePath.empty() || !(velocity > 0.0) || !(windowSeconds > 0.0))
    {
        std::fprintf(stderr, "usage: cymbalis_trims --bake BAKE.json [--reference NOVA_BAKE.json] [--velocity V] "
                             "[--window S] [--target SLOT=DB ...] [--json TRIMS.json]\n");
        return 2;
    }
    try
    {
        // Measure without the trims the bakes may already hold.
        auto untrimmed = [](const std::string& path) {
            auto b = std::make_shared<cymbalis::BakeData>(*cymbalis::loadBake(path));
            for (auto& s : b->slots)
                s.levelTrim = 1.0;
            return b;
        };
        auto bake = untrimmed(bakePath);
        auto refBake = referencePath.empty() ? bake : untrimmed(referencePath);
        const double sr = bake->sampleRate;
        const int numSlots = static_cast<int>(bake->slots.size());
        for (const auto& t : targets)
            if (bake->slotIndex(t.first) < 0)
                throw std::runtime_error("--target names a slot the bake does not have: " + t.first);

        int ref = -1;
        for (int k = 0; k < static_cast<int>(refBake->slots.size()); ++k)
            if (refBake->slots[size_t(k)].overlay == kReferenceOverlay &&
                refBake->slots[size_t(k)].position == kReferencePosition)
                ref = k;
        if (ref < 0)
            throw std::runtime_error(std::string("the reference bake has no ") + kReferenceOverlay +
                                     " slot at position 0.5");

        std::vector<double> rms(static_cast<size_t>(numSlots)), target(static_cast<size_t>(numSlots), 0.0),
            gainDb(static_cast<size_t>(numSlots)), peak(static_cast<size_t>(numSlots));
        const double refRms = strikeRms(refBake, ref, refBake->sampleRate, velocity, windowSeconds);
        for (int k = 0; k < numSlots; ++k)
        {
            const auto& s = bake->slots[size_t(k)];
            rms[size_t(k)] = strikeRms(bake, k, sr, velocity, windowSeconds, &peak[size_t(k)]);
            if (const auto t = targets.find(s.name); t != targets.end())
                target[size_t(k)] = t->second;
            gainDb[size_t(k)] = target[size_t(k)] + 20.0 * std::log10(refRms / rms[size_t(k)]);
        }
        const std::string refName =
            (referencePath.empty() ? std::string() : std::string("Nova ")) + refBake->slots[size_t(ref)].name;

        std::printf("Velocity %.2f, RMS of the first %.3f s, relative to %s (RMS %.6g)\n\n", velocity, windowSeconds,
                    refName.c_str(), refRms);
        std::printf("| Slot | Overlay | Position | RMS | Target (dB) | Trim (dB) | Peak with trim |\n"
                    "|---|---|---|---|---|---|---|\n");
        for (int k = 0; k < numSlots; ++k)
        {
            const auto& s = bake->slots[size_t(k)];
            std::printf("| %s | %s | %.2f | %.6g | %+.2f | %+.2f | %.3f |\n", s.name.c_str(),
                        s.overlay.empty() ? "none" : s.overlay.c_str(), s.position, rms[size_t(k)], target[size_t(k)],
                        gainDb[size_t(k)], peak[size_t(k)] * std::pow(10.0, gainDb[size_t(k)] / 20.0));
        }

        if (!jsonPath.empty())
        {
            std::FILE* f = std::fopen(jsonPath.c_str(), "w");
            if (f == nullptr)
                throw std::runtime_error("cannot write " + jsonPath);
            std::fprintf(f, "{\n  \"measured_by\": \"tools/cymbalis_trims (FastEngine)\",\n");
            std::fprintf(f, "  \"sample_rate\": %.17g,\n  \"velocity\": %.17g,\n  \"strike_time_s\": 0.0,\n", sr, velocity);
            std::fprintf(f, "  \"window_s\": %.17g,\n  \"reference_slot\": \"%s\",\n  \"reference_rms\": %.17g,\n",
                         windowSeconds, refName.c_str(), refRms);
            std::fprintf(f, "  \"slots\": {\n");
            for (int k = 0; k < numSlots; ++k)
            {
                const auto& s = bake->slots[size_t(k)];
                std::fprintf(f, "    \"%s\": {\"position\": %.17g, \"rms\": %.17g, \"target_db\": %.17g, \"gain_db\": %.17g}%s\n",
                             s.name.c_str(), s.position, rms[size_t(k)], target[size_t(k)], gainDb[size_t(k)],
                             k + 1 < numSlots ? "," : "");
            }
            std::fprintf(f, "  }\n}\n");
            std::fclose(f);
        }
        return 0;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "cymbalis_trims: %s\n", e.what());
        return 1;
    }
}
