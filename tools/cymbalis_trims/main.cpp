// cymbalis_trims — level trims measured with FastEngine, the plugin's own engine.
//
//   cymbalis_trims --bake BAKE.json [--reference NOVA_BAKE.json] [--velocity V] [--window S]
//                  [--spear-target SLOT=DB ...] [--peak-limit P] [--peak-hits N] [--same-as SLOT=SOURCE ...]
//                  [--json TRIMS.json]
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
// A trim is the level at the slot's own position, so HIT VARIATION (docs/TUNING.md) is
// switched off while measuring: every strike plays at the slot's position and velocity V.
//
// --same-as SLOT=SOURCE gives SLOT the trim of SOURCE (a slot without --same-as), so that it
// keeps its level relative to SOURCE: the hi-hat's TIP and FOOT keep their captured level
// relative to EDGE. Its strike is measured all the same (its level vs BOW is reported). On the
// hi-hat every strike is closed (the pedal at rest).
//
// The trims are printed and, with --json, written for tools/set_level_trims.py, which stores
// them in the bake. The audio is only measured in memory; nothing is written but the JSON.
//
//   cymbalis_trims --bake BAKE.json --skip-thresholds [--peak-hits N] [--json SKIP.json]
//
// The skip threshold instead (docs/PORT_MAP.md, "One silence rule: 140 dB below a full hit"):
// per slot, the peak of a velocity-1.0 hit at the slot's base position (the median of the peaks
// of N hits, default 16, each with the next phase draw; first second, both channels; HIT
// VARIATION off, no trim) against the slot's level (its calibration; a trim scales both alike),
// and the factor that puts FastEngine's skip threshold 140 dB below that peak:
// skip_threshold_rel = 10^((peak dB re level - 140) / 20), the threshold being
// skip_threshold_rel x calibration x trim. With --json, written for tools/set_skip_thresholds.py,
// which stores the factors in the bake.
//
//   cymbalis_trims --bake BAKE.json --chain-ref [--json CHAIN.json]
//
// The sizzle chain's reference level instead (docs/PORT_MAP.md, "Sizzle chain"; the bake needs a
// chain block, tools/set_chain_ref.py --init): ref_ms, the mean square of the output's 40-1000 Hz
// band (4th-order Butterworth high-pass at 40 Hz and low-pass at 1 kHz, mono mean of L and R)
// 0.3-1.0 s after a lone BOW strike at velocity 0.9 at its base position, with the chain on (its
// damping; no sizzle, which lies above 3.5 kHz) and HIT VARIATION off, the bake's trims and
// tuning as played, divided by the output gain squared (the sizzle passes through the output
// gain as the cymbal does). With --json, written for tools/set_chain_ref.py, which stores it.
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
constexpr double kSkipBelowFullHitDb = 140.0; // the silence rule (Kris, 2026-11-25)
constexpr int kSkipHits = 16;

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

// Peak of one hit: the given percentile (default the 99th) of the peaks of `hits` hits, each
// with the next phase draw (prepare() stops the ringing and keeps the random sequence). With
// hits = 1 it is the first hit's peak.
double hitPeak(const std::shared_ptr<const cymbalis::BakeData>& bake, int slot, double sampleRate, double velocity,
               int hits, double percentile = kPeakPercentile)
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
    const int k = std::max(0, static_cast<int>(std::ceil(percentile * hits)) - 1);
    return peaks[size_t(k)];
}

// A 2nd-order section (direct form II transposed, double).
struct Biquad
{
    double b0, b1, b2, a1, a2, z1 = 0.0, z2 = 0.0;
    double run(double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};
// Butterworth sections at fc (bilinear, prewarped): high-pass or low-pass, quality q.
Biquad section(double fc, double sr, double q, bool highPass)
{
    const double w0 = 2.0 * 3.14159265358979323846 * fc / sr, cw = std::cos(w0), alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    Biquad s {};
    if (highPass)
    {
        s.b0 = (1.0 + cw) * 0.5 / a0;
        s.b1 = -(1.0 + cw) / a0;
    }
    else
    {
        s.b0 = (1.0 - cw) * 0.5 / a0;
        s.b1 = (1.0 - cw) / a0;
    }
    s.b2 = s.b0;
    s.a1 = -2.0 * cw / a0;
    s.a2 = (1.0 - alpha) / a0;
    return s;
}

// --chain-ref: the sizzle chain's ref_ms (see the top of this file).
int chainRef(const std::shared_ptr<const cymbalis::BakeData>& bake, const std::string& jsonPath)
{
    if (!bake->chain.present)
        throw std::runtime_error("the bake has no chain block (tools/set_chain_ref.py --init)");
    const int slot = bake->slotIndex("BOW");
    if (slot < 0)
        throw std::runtime_error("the bake has no BOW slot");
    const double sr = bake->sampleRate, velocity = 0.9, t0 = 0.3, t1 = 1.0;
    cymbalis::FastEngine engine(bake);
    engine.prepare(sr);
    engine.setChain(true);
    const double position = bake->slots[size_t(slot)].position;
    engine.strike(slot, velocity, position);
    // 4th-order Butterworth high-pass at 40 Hz and low-pass at 1 kHz (two sections each, q 0.5412
    // and 1.3066).
    constexpr double q1 = 0.54119610014619701, q2 = 1.3065629648763766;
    Biquad f[4] = { section(40.0, sr, q1, true), section(40.0, sr, q2, true), section(1000.0, sr, q1, false),
                    section(1000.0, sr, q2, false) };
    const long total = std::lround(t1 * sr), from = std::lround(t0 * sr);
    constexpr int block = 512;
    std::vector<float> L(block), R(block);
    double sum = 0.0;
    long count = 0;
    for (long start = 0; start < total; start += block)
    {
        const int n = static_cast<int>(std::min<long>(block, total - start));
        engine.process(L.data(), R.data(), n);
        for (int i = 0; i < n; ++i)
        {
            double y = 0.5 * (double(L[size_t(i)]) + double(R[size_t(i)]));
            for (auto& s : f)
                y = s.run(y);
            if (start + i >= from)
            {
                sum += y * y;
                ++count;
            }
        }
    }
    const double outGain = std::pow(10.0, bake->tuning.cymbal.outputGainDb / 20.0);
    const double ms = sum / double(count), ref = ms / (outGain * outGain);
    std::printf("Sizzle chain reference: BOW (slot %d, position %.2f), velocity %.1f, 40-1000 Hz, %.1f-%.1f s, chain damping on, "
                "HIT VARIATION off\n  mean square %.6g after the output gain (%+.1f dB), ref_ms %.6g before it (%.2f dB re full scale)\n",
                slot, position, velocity, t0, t1, ms, bake->tuning.cymbal.outputGainDb, ref, 10.0 * std::log10(ref));
    if (!jsonPath.empty())
    {
        FILE* fj = std::fopen(jsonPath.c_str(), "w");
        if (fj == nullptr)
            throw std::runtime_error("cannot write " + jsonPath);
        std::fprintf(fj, "{\n  \"measured_by\": \"tools/cymbalis_trims --chain-ref (FastEngine)\",\n");
        std::fprintf(fj, "  \"sample_rate\": %.17g,\n  \"slot\": \"BOW\",\n  \"position\": %.17g,\n  \"velocity\": %.17g,\n", sr, position,
                     velocity);
        std::fprintf(fj, "  \"band_hz\": [40, 1000],\n  \"window_s\": [%.17g, %.17g],\n  \"output_gain_db\": %.17g,\n", t0, t1,
                     bake->tuning.cymbal.outputGainDb);
        std::fprintf(fj, "  \"ref_ms\": %.17g\n}\n", ref);
        std::fclose(fj);
    }
    return 0;
}

// --skip-thresholds: per slot, the median peak of a velocity-1.0 hit against the slot's level,
// and the skip threshold factor 140 dB below it.
int skipThresholds(const std::shared_ptr<const cymbalis::BakeData>& bake, int hits, const std::string& jsonPath)
{
    const double sr = bake->sampleRate;
    const int numSlots = static_cast<int>(bake->slots.size());
    std::vector<double> ratioDb(static_cast<size_t>(numSlots)), rel(static_cast<size_t>(numSlots));
    std::printf("Velocity 1.0, peak of one hit: median of %d hits (first %.1f s, both channels), no trim, HIT VARIATION "
                "off; skip threshold %.0f dB below it\n\n| Slot | Position | Level (calibration) | Full-hit peak | "
                "Peak re level (dB) | skip_threshold_rel | Was (amp_skip_threshold) re peak (dB) |\n|---|---|---|---|---|---|---|\n",
                hits, kPeakSeconds, kSkipBelowFullHitDb);
    for (int k = 0; k < numSlots; ++k)
    {
        const auto& s = bake->slots[size_t(k)];
        const double peak = hitPeak(bake, k, sr, 1.0, hits, 0.5);
        const double level = s.calibScalar;
        ratioDb[size_t(k)] = 20.0 * std::log10(peak / level);
        rel[size_t(k)] = std::pow(10.0, (ratioDb[size_t(k)] - kSkipBelowFullHitDb) / 20.0);
        std::printf("| %s | %.2f | %.6g | %.6g | %+.2f | %.4g | %.1f |\n", s.name.c_str(), s.position, level, peak,
                    ratioDb[size_t(k)], rel[size_t(k)], 20.0 * std::log10(bake->ampSkipThreshold * level / peak));
    }
    if (!jsonPath.empty())
    {
        std::FILE* f = std::fopen(jsonPath.c_str(), "w");
        if (f == nullptr)
            throw std::runtime_error("cannot write " + jsonPath);
        std::fprintf(f, "{\n  \"measured_by\": \"tools/cymbalis_trims --skip-thresholds (FastEngine)\",\n");
        std::fprintf(f, "  \"sample_rate\": %.17g,\n  \"velocity\": 1.0,\n  \"peak_hits\": %d,\n"
                        "  \"peak_statistic\": \"median\",\n  \"peak_window_s\": %.17g,\n  \"below_full_hit_db\": %.17g,\n",
                     sr, hits, kPeakSeconds, kSkipBelowFullHitDb);
        std::fprintf(f, "  \"slots\": {\n");
        for (int k = 0; k < numSlots; ++k)
            std::fprintf(f, "    \"%s\": {\"position\": %.17g, \"full_hit_peak_db_re_level\": %.17g, \"skip_threshold_rel\": %.17g}%s\n",
                         bake->slots[size_t(k)].name.c_str(), bake->slots[size_t(k)].position, ratioDb[size_t(k)],
                         rel[size_t(k)], k + 1 < numSlots ? "," : "");
        std::fprintf(f, "  }\n}\n");
        std::fclose(f);
    }
    return 0;
}
} // namespace

int main(int argc, char** argv)
{
    std::string bakePath, jsonPath, referencePath;
    double velocity = 0.9, windowSeconds = 1.0, peakLimit = 0.0;
    int peakHits = 1024;
    bool skipMode = false, chainMode = false, peakHitsGiven = false;
    std::map<std::string, double> targets;
    std::map<std::string, std::string> sameAs;
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
        else if (a == "--peak-hits" && i + 1 < argc)
        {
            peakHits = std::stoi(argv[++i]);
            peakHitsGiven = true;
        }
        else if (a == "--skip-thresholds") skipMode = true;
        else if (a == "--chain-ref") chainMode = true;
        else if (a == "--spear-target" && i + 1 < argc)
        {
            const std::string t = argv[++i];
            const auto eq = t.find('=');
            if (eq == std::string::npos || eq == 0) usage = true;
            else targets[t.substr(0, eq)] = std::stod(t.substr(eq + 1));
        }
        else if (a == "--same-as" && i + 1 < argc)
        {
            const std::string t = argv[++i];
            const auto eq = t.find('=');
            if (eq == std::string::npos || eq == 0 || eq + 1 == t.size()) usage = true;
            else sameAs[t.substr(0, eq)] = t.substr(eq + 1);
        }
        else usage = true;
    }
    if (usage || bakePath.empty() || !(velocity > 0.0) || !(windowSeconds > 0.0) || peakLimit < 0.0 || peakHits < 1)
    {
        std::fprintf(stderr, "usage: cymbalis_trims --bake BAKE.json [--reference NOVA_BAKE.json] [--velocity V] "
                             "[--window S] [--spear-target SLOT=DB ...] [--peak-limit P] [--peak-hits N] "
                             "[--same-as SLOT=SOURCE ...] [--json TRIMS.json]\n"
                             "       cymbalis_trims --bake BAKE.json --skip-thresholds [--peak-hits N] [--json SKIP.json]\n"
                             "       cymbalis_trims --bake BAKE.json --chain-ref [--json CHAIN.json]\n");
        return 2;
    }
    try
    {
        // Measure without the trims the bakes may already hold, and without HIT VARIATION.
        auto untrimmed = [](const std::string& path) {
            auto b = std::make_shared<cymbalis::BakeData>(*cymbalis::loadBake(path));
            for (auto& s : b->slots)
                s.levelTrim = 1.0;
            b->tuning.cymbal.hitVariation = false;
            return b;
        };
        if (chainMode)
        {
            // As played: the bake's trims and tuning, HIT VARIATION off.
            auto b = std::make_shared<cymbalis::BakeData>(*cymbalis::loadBake(bakePath));
            b->tuning.cymbal.hitVariation = false;
            return chainRef(b, jsonPath);
        }
        auto bake = untrimmed(bakePath);
        if (skipMode)
            return skipThresholds(bake, peakHitsGiven ? peakHits : kSkipHits, jsonPath);
        auto refBake = referencePath.empty() ? bake : untrimmed(referencePath);
        const double sr = bake->sampleRate;
        const int numSlots = static_cast<int>(bake->slots.size());
        for (const auto& t : targets)
            if (bake->slotIndex(t.first) < 0)
                throw std::runtime_error("--spear-target names a slot the bake does not have: " + t.first);
        for (const auto& t : sameAs)
            if (bake->slotIndex(t.first) < 0 || bake->slotIndex(t.second) < 0 || sameAs.count(t.second) != 0 ||
                targets.count(t.first) != 0)
                throw std::runtime_error("--same-as " + t.first + "=" + t.second +
                                         ": both slots must exist, the source must have its own trim, and the slot no SPEAR target");

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
        // --same-as: the source's trim, and the level vs BOW that gives.
        for (const auto& t : sameAs)
        {
            auto& r = res[size_t(bake->slotIndex(t.first))];
            r.gainDb = res[size_t(bake->slotIndex(t.second))].gainDb;
            r.capped = false;
            r.levelDb = r.gainDb + 20.0 * std::log10(r.rms / refRms);
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
                if (!targets.empty() && sameAs.count(s.name) == 0)
                    std::fprintf(f, ", \"spear_target_db\": %.17g", r.target);
                if (const auto same = sameAs.find(s.name); same != sameAs.end())
                    std::fprintf(f, ", \"same_as\": \"%s\"", same->second.c_str());
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
