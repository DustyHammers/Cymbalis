// cymbalis_trims — level trims measured with FastEngine, the plugin's own engine.
//
//   cymbalis_trims --bake BAKE.json [--reference NOVA_BAKE.json] [--velocity V] [--window S]
//                  [--spear-target SLOT=DB ...] [--peak-limit P] [--peak-hits N] [--json TRIMS.json]
//
// For every slot of the bake (one per overlay: each row and stick choice of the plugin
// plays one of them), a fresh engine with no level trim plays one strike of velocity V
// (default 0.9) at the slot's base position at t = 0. The RMS of the first S seconds
// (default 1; both channels, i.e. the mean level) is compared with the same strike on BOW
// (tip): the bake's own bow_tip slot or, with --reference, the bow_tip slot of that bake
// (Nova Ride), so every preset is matched to the same level. The trim puts the slot at its
// target level relative to BOW (tip): trim = target + 20 log10(RMS(BOW tip) / RMS(slot)).
// The target is 0 dB unless --spear-target gives the level the slot's SPEAR captures have
// relative to the cymbal's bow.
//
// With --peak-limit, a slot with a SPEAR target goes no higher than the trim at which one
// strike of velocity V peaks at P before the Volume knob; slots without one are matched to
// BOW (tip) and never capped. Each strike draws its own random phases, so its peak varies
// from hit to hit: the peak of one hit is the 99th percentile of the peaks (first second,
// both channels) of N hits (default 1024), each with the next phase draw of the preset's
// sequence, as the plugin plays them. --peak-hits 1 uses the first hit after loading.
//
// The trims are printed and, with --json, written for tools/set_level_trims.py, which stores
// them in the bake. The audio is only measured in memory; nothing is written but the JSON.
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
constexpr double kPeakSeconds = 1.0;    // a hit's peak falls well inside its first second
constexpr double kPeakPercentile = 0.99;

// Renders seconds of the strike just made; returns the sum of squares of the first
// rmsSamples samples (both channels) and puts the peak of all of it in *peak.
double renderStrike(cymbalis::FastEngine& engine, double seconds, long rmsSamples, double* peak)
{
    const long total = std::lround(seconds * engine.sampleRate());
    constexpr int block = 512;
    std::vector<float> L(block), R(block);
    double sum = 0.0, top = 0.0;
    for (long start = 0; start < total; start += block)
    {
        const int n = static_cast<int>(std::min<long>(block, total - start));
        engine.process(L.data(), R.data(), n);
        for (int i = 0; i < n; ++i)
        {
            if (start + i < rmsSamples)
                sum += double(L[size_t(i)]) * L[size_t(i)] + double(R[size_t(i)]) * R[size_t(i)];
            top = std::max({top, std::fabs(double(L[size_t(i)])), std::fabs(double(R[size_t(i)]))});
        }
    }
    *peak = top;
    return sum;
}

// RMS of the first windowSeconds of the first strike after loading; its peak (over the
// window or the first second, whichever is longer) goes to *peak.
double strikeRms(const std::shared_ptr<const cymbalis::BakeData>& bake, int slot, double sampleRate, double velocity,
                 double windowSeconds, double* peak)
{
    cymbalis::FastEngine engine(bake);
    engine.prepare(sampleRate);
    engine.strike(slot, velocity, bake->slots[size_t(slot)].position);
    const long total = std::lround(windowSeconds * sampleRate);
    return std::sqrt(renderStrike(engine, std::max(windowSeconds, kPeakSeconds), total, peak) / (2.0 * double(total)));
}

// Peak of one hit: the 99th percentile of the peaks of `hits` hits, each with the next
// phase draw (prepare() stops the ringing and keeps the random sequence). With hits = 1 it
// is the first hit's peak.
double hitPeak(const std::shared_ptr<const cymbalis::BakeData>& bake, int slot, double sampleRate, double velocity,
               int hits)
{
    cymbalis::FastEngine engine(bake);
    std::vector<double> peaks(static_cast<size_t>(hits));
    for (auto& p : peaks)
    {
        engine.prepare(sampleRate);
        engine.strike(slot, velocity, bake->slots[size_t(slot)].position);
        renderStrike(engine, kPeakSeconds, 0, &p);
    }
    std::sort(peaks.begin(), peaks.end());
    const int k = std::max(0, static_cast<int>(std::ceil(kPeakPercentile * hits)) - 1);
    return peaks[size_t(k)];
}
} // namespace

int main(int argc, char** argv)
{
    std::string bakePath, jsonPath, referencePath;
    double velocity = 0.9, windowSeconds = 1.0, peakLimit = 0.0;
    int peakHits = 1024;
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
        else if (a == "--peak-limit" && i + 1 < argc) peakLimit = std::stod(argv[++i]);
        else if (a == "--peak-hits" && i + 1 < argc) peakHits = std::stoi(argv[++i]);
        else if (a == "--spear-target" && i + 1 < argc)
        {
            const std::string t = argv[++i];
            const auto eq = t.find('=');
            if (eq == std::string::npos || eq == 0) usage = true;
            else targets[t.substr(0, eq)] = std::stod(t.substr(eq + 1));
        }
        else usage = true;
    }
    if (usage || bakePath.empty() || !(velocity > 0.0) || !(windowSeconds > 0.0) || peakLimit < 0.0 || peakHits < 1)
    {
        std::fprintf(stderr, "usage: cymbalis_trims --bake BAKE.json [--reference NOVA_BAKE.json] [--velocity V] "
                             "[--window S] [--spear-target SLOT=DB ...] [--peak-limit P] [--peak-hits N] "
                             "[--json TRIMS.json]\n");
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
                throw std::runtime_error("--spear-target names a slot the bake does not have: " + t.first);

        int ref = -1;
        for (int k = 0; k < static_cast<int>(refBake->slots.size()); ++k)
            if (refBake->slots[size_t(k)].overlay == kReferenceOverlay &&
                refBake->slots[size_t(k)].position == kReferencePosition)
                ref = k;
        if (ref < 0)
            throw std::runtime_error(std::string("the reference bake has no ") + kReferenceOverlay +
                                     " slot at position 0.5");

        struct Result
        {
            double rms = 0.0, firstPeak = 0.0, peak = 0.0; // peaks without trim
            bool hasTarget = false, capped = false;
            double target = 0.0, targetGainDb = 0.0, gainDb = 0.0, levelDb = 0.0;
        };
        std::vector<Result> res(static_cast<size_t>(numSlots));
        double refPeak = 0.0;
        const double refRms = strikeRms(refBake, ref, refBake->sampleRate, velocity, windowSeconds, &refPeak);
        const bool measurePeaks = peakLimit > 0.0;
        for (int k = 0; k < numSlots; ++k)
        {
            const auto& s = bake->slots[size_t(k)];
            auto& r = res[size_t(k)];
            r.rms = strikeRms(bake, k, sr, velocity, windowSeconds, &r.firstPeak);
            r.peak = measurePeaks ? hitPeak(bake, k, sr, velocity, peakHits) : r.firstPeak;
            if (const auto t = targets.find(s.name); t != targets.end())
            {
                r.hasTarget = true;
                r.target = t->second;
            }
            r.targetGainDb = r.target + 20.0 * std::log10(refRms / r.rms);
            r.gainDb = r.targetGainDb;
            if (measurePeaks && r.hasTarget)
            {
                // Rounded down to 0.01 dB so that the stored trim stays within the limit.
                const double capDb = std::floor(100.0 * 20.0 * std::log10(peakLimit / r.peak)) / 100.0;
                if (capDb < r.targetGainDb)
                {
                    r.capped = true;
                    r.gainDb = capDb;
                }
            }
            r.levelDb = r.capped ? r.gainDb + 20.0 * std::log10(r.rms / refRms) : r.target;
        }
        const std::string refName =
            (referencePath.empty() ? std::string() : std::string("Nova ")) + refBake->slots[size_t(ref)].name;
        auto gain = [](double db) { return std::pow(10.0, db / 20.0); };
        const std::string peakName = measurePeaks && peakHits > 1
                                         ? "99th percentile of " + std::to_string(peakHits) + " hits"
                                         : std::string("first hit");

        std::printf("Velocity %.2f, RMS of the first %.3f s, relative to %s (RMS %.6g)", velocity, windowSeconds,
                    refName.c_str(), refRms);
        if (measurePeaks)
            std::printf("; SPEAR targets capped at a peak of %.3g (%s, first %.1f s)", peakLimit, peakName.c_str(),
                        kPeakSeconds);
        std::printf("\n\n| Slot | Overlay | Position | RMS | SPEAR target (dB) | Trim (dB) | Level vs BOW (dB) | "
                    "Peak with trim (%s) | Capped |\n|---|---|---|---|---|---|---|---|---|\n",
                    peakName.c_str());
        for (int k = 0; k < numSlots; ++k)
        {
            const auto& s = bake->slots[size_t(k)];
            const auto& r = res[size_t(k)];
            char target[32] = "-";
            if (r.hasTarget)
                std::snprintf(target, sizeof target, "%+.2f", r.target);
            std::printf("| %s | %s | %.2f | %.6g | %s | %+.2f | %+.2f | %.3f | %s |\n", s.name.c_str(),
                        s.overlay.empty() ? "none" : s.overlay.c_str(), s.position, r.rms, target, r.gainDb, r.levelDb,
                        r.peak * gain(r.gainDb), r.capped ? "yes" : "no");
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
            if (measurePeaks)
                std::fprintf(f, "  \"peak_limit\": %.17g,\n  \"peak_hits\": %d,\n  \"peak_percentile\": %.17g,\n"
                                "  \"peak_window_s\": %.17g,\n",
                             peakLimit, peakHits, 100.0 * kPeakPercentile, kPeakSeconds);
            std::fprintf(f, "  \"slots\": {\n");
            for (int k = 0; k < numSlots; ++k)
            {
                const auto& s = bake->slots[size_t(k)];
                const auto& r = res[size_t(k)];
                std::fprintf(f, "    \"%s\": {\"position\": %.17g, \"rms\": %.17g, \"gain_db\": %.17g, \"level_db\": %.17g",
                             s.name.c_str(), s.position, r.rms, r.gainDb, r.levelDb);
                if (!targets.empty())
                    std::fprintf(f, ", \"spear_target_db\": %.17g", r.target);
                if (measurePeaks)
                    std::fprintf(f, ", \"peak\": %.17g, \"first_hit_peak\": %.17g", r.peak * gain(r.gainDb),
                                 r.firstPeak * gain(r.gainDb));
                if (measurePeaks && r.hasTarget)
                {
                    std::fprintf(f, ", \"capped\": %s", r.capped ? "true" : "false");
                    if (r.capped)
                    {
                        char reason[400];
                        std::snprintf(reason, sizeof reason,
                                      "At its SPEAR target (%+.1f dB vs BOW, trim %+.2f dB) one velocity-%.1f hit would "
                                      "peak at %.2f (%s) before the Volume knob, above the %.2f limit; capped to the "
                                      "largest trim that keeps that peak at %.2f.",
                                      r.target, r.targetGainDb, velocity, r.peak * gain(r.targetGainDb),
                                      peakName.c_str(), peakLimit, peakLimit);
                        std::fprintf(f, ", \"uncapped_gain_db\": %.17g, \"capped_level_db\": %.17g, \"cap_reason\": \"%s\"",
                                     r.targetGainDb, r.levelDb, reason);
                    }
                }
                std::fprintf(f, "}%s\n", k + 1 < numSlots ? "," : "");
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
