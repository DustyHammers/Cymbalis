// cymbalis_trims — level trims measured with FastEngine, the plugin's own engine.
//
//   cymbalis_trims --bake BAKE.json [--reference NOVA_BAKE.json] [--json TRIMS.json]
//
// For every slot of the bake (one per overlay: each row and stick choice of the plugin
// plays one of them), a fresh engine with no level trim plays one velocity-0.9 strike at
// the slot's base position at t = 0. The RMS of the first second (both channels) gives
// the trim that matches it to BOW (tip): trim = RMS(BOW tip) / RMS(slot). BOW (tip) is the
// bake's own bow_tip slot, or, with --reference, the bow_tip slot of that bake (Nova Ride),
// so every preset is matched to the same level. The trims are printed and, with --json,
// written for tools/set_level_trims.py, which stores them in the bake. The audio is only
// measured in memory; nothing is written but the JSON.
#include "Bake.h"
#include "FastEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
constexpr double kVelocity = 0.9;
constexpr double kWindowSeconds = 1.0;
constexpr const char* kReferenceOverlay = "bow_tip"; // Nova BOW (tip)
constexpr double kReferencePosition = 0.5;

double strikeRms(const std::shared_ptr<const cymbalis::BakeData>& bake, int slot, double sampleRate)
{
    cymbalis::FastEngine engine(bake);
    engine.prepare(sampleRate);
    engine.strike(slot, kVelocity, bake->slots[size_t(slot)].position);
    const long total = std::lround(kWindowSeconds * sampleRate);
    constexpr int block = 512;
    std::vector<float> L(block), R(block);
    double sum = 0.0;
    for (long start = 0; start < total; start += block)
    {
        const int n = static_cast<int>(std::min<long>(block, total - start));
        engine.process(L.data(), R.data(), n);
        for (int i = 0; i < n; ++i)
            sum += double(L[size_t(i)]) * L[size_t(i)] + double(R[size_t(i)]) * R[size_t(i)];
    }
    return std::sqrt(sum / (2.0 * double(total)));
}
} // namespace

int main(int argc, char** argv)
{
    std::string bakePath, jsonPath, referencePath;
    bool usage = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--bake" && i + 1 < argc) bakePath = argv[++i];
        else if (a == "--json" && i + 1 < argc) jsonPath = argv[++i];
        else if (a == "--reference" && i + 1 < argc) referencePath = argv[++i];
        else usage = true;
    }
    if (usage || bakePath.empty())
    {
        std::fprintf(stderr, "usage: cymbalis_trims --bake BAKE.json [--reference NOVA_BAKE.json] [--json TRIMS.json]\n");
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

        int ref = -1;
        for (int k = 0; k < static_cast<int>(refBake->slots.size()); ++k)
            if (refBake->slots[size_t(k)].overlay == kReferenceOverlay &&
                refBake->slots[size_t(k)].position == kReferencePosition)
                ref = k;
        if (ref < 0)
            throw std::runtime_error(std::string("the reference bake has no ") + kReferenceOverlay +
                                     " slot at position 0.5");

        std::vector<double> rms(static_cast<size_t>(numSlots));
        for (int k = 0; k < numSlots; ++k)
            rms[size_t(k)] = strikeRms(bake, k, sr);
        const double refRms = strikeRms(refBake, ref, refBake->sampleRate);
        const std::string refName =
            (referencePath.empty() ? std::string() : std::string("Nova ")) + refBake->slots[size_t(ref)].name;

        std::printf("| Slot | Overlay | Position | 1 s RMS | Trim (dB) |\n|---|---|---|---|---|\n");
        for (int k = 0; k < numSlots; ++k)
        {
            const auto& s = bake->slots[size_t(k)];
            std::printf("| %s | %s | %.2f | %.6g | %+.2f |\n", s.name.c_str(), s.overlay.empty() ? "none" : s.overlay.c_str(),
                        s.position, rms[size_t(k)], 20.0 * std::log10(refRms / rms[size_t(k)]));
        }

        if (!jsonPath.empty())
        {
            std::FILE* f = std::fopen(jsonPath.c_str(), "w");
            if (f == nullptr)
                throw std::runtime_error("cannot write " + jsonPath);
            std::fprintf(f, "{\n  \"measured_by\": \"tools/cymbalis_trims (FastEngine)\",\n");
            std::fprintf(f, "  \"sample_rate\": %.17g,\n  \"velocity\": %.17g,\n  \"strike_time_s\": 0.0,\n", sr, kVelocity);
            std::fprintf(f, "  \"window_s\": %.17g,\n  \"reference_slot\": \"%s\",\n  \"reference_rms_1s\": %.17g,\n",
                         kWindowSeconds, refName.c_str(), refRms);
            std::fprintf(f, "  \"slots\": {\n");
            for (int k = 0; k < numSlots; ++k)
            {
                const auto& s = bake->slots[size_t(k)];
                std::fprintf(f, "    \"%s\": {\"position\": %.17g, \"rms_1s\": %.17g, \"gain_db\": %.17g}%s\n", s.name.c_str(),
                             s.position, rms[size_t(k)], 20.0 * std::log10(refRms / rms[size_t(k)]),
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
