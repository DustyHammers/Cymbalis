// ChainOff.cpp — cymbalis_chain_off: the sizzle chain cannot change the sound once it is off
// (docs/PORT_MAP.md, "Sizzle chain", "Off is off"; Kris, 2026-11-27). Every case renders an engine
// or a processor whose chain is switched as the case says against the same playing on one whose
// chain is never on, in lockstep, sample by sample. In memory; nothing is written to disk.
//
//   cymbalis_chain_off [--full] [--quiet]
//
// The playing: EDGE, BOW and BELL (STINE: CENTER) at velocities 0.3 to 1.0, with flams (a second
// strike 4-25 ms after the first) and rolls (6-10 strikes 35-60 ms apart), from a fixed seed.
//
// FastEngine (the chain's own code and the engine's use of it):
//   A  the chain toggled on and off 25 times at random samples (process() split there) while
//      nothing rings; the playing (chain off); silence; toggled again; the playing again
//                                                         -> bit-identical throughout
//   B  the playing for 3 s with the chain on; off; until both engines report silence (the 140 dB
//      rule); the playing                                  -> bit-identical from the first strike
//                                                            after the silence
//   C  the playing with the chain off while its eight values change, each to both ends of its
//      range, every 0.4 s                                  -> bit-identical throughout
//   D  the playing for 24 s, the chain on for its first 4 s -> the difference never rises at a
//      strike made after the switch: over the 20 ms from each strike against the 20 ms before it,
//      by at most 0.5 dB; its level over time and its largest value against the signal reported
//   G  after B's second playing, the block at which each engine reports silence -> the same block
//
// CymbalisProcessor (its per-block switch, preset switching, its session):
//   A  the switch toggled between blocks of random sizes (1 to the block size) while nothing
//      rings, the other presets' switches too; the playing (MIDI)  -> bit-identical throughout
//   C  the chain's values changed with setTuning while off, during the playing -> bit-identical
//   E1 chain on for X, the playing; to another chain preset Y and back; chain off; the playing
//                                                          -> bit-identical from the switch to Y
//   E2 chain on for X, the playing; to NOVA RIDE (no chain), the playing; back to X with the
//      chain off; the playing                              -> bit-identical from the switch to NOVA
//   F1 chain on, saved, restored into a new processor, switched off; the playing -> bit-identical
//   F2 chain on, the playing; saved and restored into the same processor (ringing); off; until
//      both outputs are silent for 1 s; the playing        -> bit-identical from the first strike
//                                                            after the silence
//   F3 chain on, the playing, off, saved; restored into a new processor; the playing
//                                                          -> bit-identical throughout
// Preset switches go through prepareToPlay, which builds the new cymbal at once (as the loader
// thread does, then the audio thread takes it); so they are deterministic in both processors.
//
// Without --full: STINE FLAT RIDE and HAMMERED THIN RIDE, 48 kHz, 512-sample blocks, HIT VARIATION
// on (every pull request). --full: the five presets with a chain, 44.1, 48 and 96 kHz, 64- and
// 512-sample blocks, HIT VARIATION on and off. Exit status 1 if any case fails.

#include "PluginProcessor.h"
#include "SlotTable.h"

#include "Bake.h"
#include "ChainSettings.h"
#include "FastEngine.h"
#include "Tuning.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace
{
using cymbalis::BakeData;
using cymbalis::FastEngine;

// The playing: row 0 BELL (STINE: CENTER), 1 BOW, 2 EDGE.
struct Strike
{
    double t;
    int row;
    double v;
};

std::vector<Strike> playing (double t0, double seconds, std::uint32_t seed)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<double> u (0.0, 1.0);
    const auto velocity = [&] { return 0.3 + 0.7 * u (rng); };
    const auto row = [&] { return std::min (2, int (3.0 * u (rng))); };
    std::vector<Strike> out;
    double t = t0;
    while (t < t0 + seconds)
    {
        const double r = u (rng);
        if (r < 0.12) // a roll on one row
        {
            const int n = 6 + int (5.0 * u (rng)), w = row();
            const double gap = 0.035 + 0.025 * u (rng);
            for (int k = 0; k < n && t < t0 + seconds; ++k, t += gap)
                out.push_back ({ t, w, velocity() });
            t += 0.1;
        }
        else if (r < 0.27) // a flam
        {
            const int a = row(), b = row();
            out.push_back ({ t, a, velocity() });
            out.push_back ({ t + 0.004 + 0.021 * u (rng), b, velocity() });
            t += 0.17 + 0.15 * u (rng);
        }
        else
        {
            out.push_back ({ t, row(), velocity() });
            t += 0.12 + 0.2 * u (rng);
        }
    }
    std::stable_sort (out.begin(), out.end(), [] (const Strike& a, const Strike& b) { return a.t < b.t; });
    return out;
}

bool sameBits (float a, float b)
{
    return std::memcmp (&a, &b, sizeof (float)) == 0;
}

// Sample-by-sample comparison from `from` on.
struct Compare
{
    std::int64_t from = 0, firstDiff = -1, diffs = 0;
    double maxDiff = 0.0;
    void add (std::int64_t at, const float* a, const float* b, int n)
    {
        for (int i = 0; i < n; ++i)
            if (at + i >= from && ! sameBits (a[i], b[i]))
            {
                if (firstDiff < 0)
                    firstDiff = at + i;
                ++diffs;
                maxDiff = std::max (maxDiff, std::fabs (double (a[i]) - double (b[i])));
            }
    }
    bool identical() const { return diffs == 0; }
    std::string text (double sr) const
    {
        if (identical())
            return "bit-identical";
        char buf[160];
        std::snprintf (buf, sizeof (buf), "DIFFERS from %.4f s (%lld samples, largest %.3g)", double (firstDiff) / sr, (long long) diffs, maxDiff);
        return buf;
    }
};

// ---------------------------------------------------------------------------------------------
// FastEngine

struct Event
{
    std::int64_t at;
    enum Kind { strike, chainOn, chainOff, tuning } kind;
    int slot = 0;
    double v = 0.0;
    int tuningIndex = 0;
};

struct EnginePair
{
    EnginePair (std::shared_ptr<const BakeData> b, double rate, int blockSize)
        : bake (std::move (b)), test (bake), ref (bake), sr (rate), block (blockSize)
    {
        test.prepare (sr);
        ref.prepare (sr);
        rows[0] = bake->slotIndex ("BELL") >= 0 ? bake->slotIndex ("BELL") : bake->slotIndex ("CENTER");
        rows[1] = bake->slotIndex ("BOW");
        rows[2] = bake->slotIndex ("EDGE");
        tunings.push_back (bake->tuning);
    }

    std::shared_ptr<const BakeData> bake;
    FastEngine test, ref;
    double sr;
    int block;
    int rows[3] {};
    std::vector<cymbalis::Tuning> tunings; // [0]: the bake's
    std::int64_t now = 0;
    Compare cmp { INT64_MAX };
    bool record = false;
    std::vector<float> recTest, recRef; // L then R interleaved per sample pair, while recording
    std::int64_t silent[2] = { -1, -1 };

    std::int64_t samples (double t) const { return std::int64_t (std::llround (t * sr)); }

    std::vector<Event> strikes (const std::vector<Strike>& p) const
    {
        std::vector<Event> out;
        for (const auto& s : p)
            out.push_back ({ samples (s.t), Event::strike, rows[s.row], s.v });
        return out;
    }

    void apply (FastEngine& e, const Event& ev)
    {
        switch (ev.kind)
        {
            case Event::strike: e.strike (ev.slot, ev.v, bake->slots[size_t (ev.slot)].position); break;
            case Event::chainOn: e.setChain (true); break;
            case Event::chainOff: e.setChain (false); break;
            case Event::tuning: e.setTuning (tunings[size_t (ev.tuningIndex)], nullptr); break;
        }
    }

    void span (FastEngine& e, const std::vector<Event>& ev, size_t& k, std::int64_t a, std::int64_t b, float* L, float* R)
    {
        std::int64_t pos = a;
        while (k < ev.size() && ev[k].at < b)
        {
            if (ev[k].at > pos)
            {
                e.process (L + (pos - a), R + (pos - a), int (ev[k].at - pos));
                pos = ev[k].at;
            }
            apply (e, ev[k]);
            ++k;
        }
        if (pos < b)
            e.process (L + (pos - a), R + (pos - a), int (b - pos));
    }

    // Host blocks on the grid of `block` samples from 0, each engine split at its own events.
    void run (std::vector<Event> testEv, std::vector<Event> refEv, std::int64_t until)
    {
        const auto byTime = [] (const Event& a, const Event& b) { return a.at < b.at; };
        std::stable_sort (testEv.begin(), testEv.end(), byTime);
        std::stable_sort (refEv.begin(), refEv.end(), byTime);
        size_t kt = 0, kr = 0;
        std::vector<float> tl (static_cast<size_t> (block)), tr (static_cast<size_t> (block)), rl (static_cast<size_t> (block)), rr (static_cast<size_t> (block));
        while (now < until)
        {
            const std::int64_t end = std::min (until, (now / block + 1) * block);
            const int n = int (end - now);
            std::fill (tl.begin(), tl.end(), 0.0f);
            std::fill (tr.begin(), tr.end(), 0.0f);
            std::fill (rl.begin(), rl.end(), 0.0f);
            std::fill (rr.begin(), rr.end(), 0.0f);
            span (test, testEv, kt, now, end, tl.data(), tr.data());
            span (ref, refEv, kr, now, end, rl.data(), rr.data());
            cmp.add (now, tl.data(), rl.data(), n);
            cmp.add (now, tr.data(), rr.data(), n);
            if (record)
                for (int i = 0; i < n; ++i)
                {
                    recTest.push_back (tl[size_t (i)]);
                    recTest.push_back (tr[size_t (i)]);
                    recRef.push_back (rl[size_t (i)]);
                    recRef.push_back (rr[size_t (i)]);
                }
            if (end % block == 0)
            {
                if (silent[0] < 0 && test.isSilent())
                    silent[0] = end;
                if (silent[1] < 0 && ref.isSilent())
                    silent[1] = end;
            }
            now = end;
        }
    }

    // Host blocks with no events until both engines report silence (at most `maxS` seconds);
    // then on to the next block boundary. False if they did not fall silent.
    bool runUntilSilent (double maxS)
    {
        silent[0] = silent[1] = -1;
        const std::int64_t limit = now + samples (maxS);
        while (now < limit && (silent[0] < 0 || silent[1] < 0))
            run ({}, {}, std::min (limit, (now / block + 1) * block));
        return silent[0] >= 0 && silent[1] >= 0;
    }
};

std::shared_ptr<const BakeData> withHitVariation (const std::shared_ptr<const BakeData>& b, bool on)
{
    auto copy = std::make_shared<BakeData> (*b);
    copy->tuning.cymbal.hitVariation = on;
    return copy;
}

struct Result
{
    std::string name, text;
    bool pass;
};

// Case A: toggled while nothing rings.
Result engineA (const std::shared_ptr<const BakeData>& bake, double sr, int block)
{
    EnginePair e (bake, sr, block);
    e.cmp.from = 0;
    std::mt19937 rng (11);
    const auto toggles = [&] (std::int64_t from, std::int64_t to) {
        std::vector<std::int64_t> at;
        std::uniform_int_distribution<std::int64_t> pick (from, to - 1);
        for (int k = 0; k < 25; ++k)
            at.push_back (pick (rng));
        std::sort (at.begin(), at.end());
        std::vector<Event> ev;
        for (size_t k = 0; k < at.size(); ++k)
            ev.push_back ({ at[k], k % 2 == 0 ? Event::chainOn : Event::chainOff });
        ev.push_back ({ to - 1, Event::chainOff }); // inside the phase: run() takes events before its end
        return ev;
    };
    // The playing starts once the last switch-off's 50 ms fade has ended (fadeEnd below).
    const double fadeEnd = cymbalis::SizzleChain::kFadeS + 0.01;
    auto t1 = toggles (0, e.samples (3.0));
    e.run (t1, {}, e.samples (3.0));
    const auto p1 = e.strikes (playing (3.0 + fadeEnd, 6.0, 101));
    e.run (p1, p1, e.samples (9.0 + fadeEnd));
    bool ok = e.runUntilSilent (240.0);
    const double t0 = double (e.now) / sr;
    auto t2 = toggles (e.now, e.now + e.samples (3.0));
    e.run (t2, {}, e.now + e.samples (3.0));
    const auto p2 = e.strikes (playing (t0 + 3.0 + fadeEnd, 6.0, 102));
    e.run (p2, p2, e.now + e.samples (6.0 + fadeEnd));
    ok = ok && e.cmp.identical();
    return { "A", e.cmp.text (sr), ok };
}

// The fade window: the chain on for 0.5 s with nothing struck, off, and an EDGE strike at 1.0
// one sample later, within the sizzle's 50 ms fade-out. Reported: how far the strike's sizzle
// reaches (largest difference against the never-on strike's peak) and when it ends. Checked:
// from the fade's end (one host block later), the left channel bit-identical and the right
// within 1e-6 of the peak (the right-channel high-pass's trace of the sizzle dying away).
Result engineFade (const std::shared_ptr<const BakeData>& bake, double sr, int block)
{
    EnginePair e (bake, sr, block);
    e.record = true;
    const std::int64_t at = e.samples (1.0);
    std::vector<Event> te { { e.samples (0.5), Event::chainOn }, { at - 1, Event::chainOff }, { at, Event::strike, e.rows[2], 1.0 } };
    std::vector<Event> re { { at, Event::strike, e.rows[2], 1.0 } };
    e.run (te, re, e.samples (2.0));
    const std::int64_t end = at + e.samples (cymbalis::SizzleChain::kFadeS) + block;
    double peak = 0.0, inFade = 0.0, left = 0.0, right = 0.0;
    std::int64_t last = -1;
    for (size_t i = 0; i < e.recRef.size(); ++i)
    {
        const std::int64_t n = std::int64_t (i / 2);
        const double d = std::fabs (double (e.recTest[i]) - double (e.recRef[i]));
        peak = std::max (peak, double (std::fabs (e.recRef[i])));
        if (n < end)
            inFade = std::max (inFade, d);
        else
            (i % 2 == 0 ? left : right) = std::max (i % 2 == 0 ? left : right, d);
        if (i % 2 == 0 && d > 0.0)
            last = n;
    }
    const bool ok = left <= 0.0 && right <= 1e-6 * peak;
    char buf[220];
    std::snprintf (buf, sizeof (buf), "a strike 1 sample after switching off: its sizzle %.1f dB re its peak at most, the left channel back to bit-identical %.1f ms after the switch; after the fade: left %s, right within %.1f dB re the peak",
                   inFade > 0.0 ? 20.0 * std::log10 (inFade / peak) : -999.0, last >= 0 ? 1000.0 * double (last + 1 - at) / sr : 0.0,
                   left <= 0.0 ? "bit-identical" : "DIFFERS", right > 0.0 ? 20.0 * std::log10 (right / peak) : -999.0);
    return { "Fade window", buf, ok };
}

// Cases B and G: struck while on; silence; the playing.
std::pair<Result, Result> engineBG (const std::shared_ptr<const BakeData>& bake, double sr, int block)
{
    EnginePair e (bake, sr, block);
    auto p1 = e.strikes (playing (0.0, 3.0, 201));
    auto t1 = p1;
    t1.push_back ({ 0, Event::chainOn });
    t1.push_back ({ e.samples (3.0) - 1, Event::chainOff }); // inside the phase: run() takes events before its end
    e.run (t1, p1, e.samples (3.0));
    const bool silentFirst = e.runUntilSilent (240.0);
    const double silenceS = double (e.now) / sr;
    e.cmp.from = e.now;
    const auto p2 = e.strikes (playing (silenceS, 6.0, 202));
    e.run (p2, p2, e.now + e.samples (6.0));
    const bool silentAfter = e.runUntilSilent (240.0);
    char buf[200];
    std::snprintf (buf, sizeof (buf), "%s (silent at %.2f s)", e.cmp.text (sr).c_str(), silenceS);
    Result b { "B", buf, silentFirst && e.cmp.identical() };
    std::snprintf (buf, sizeof (buf), "%s (test %.3f s, never-on %.3f s)", e.silent[0] == e.silent[1] ? "same block" : "DIFFERENT BLOCKS",
                   double (e.silent[0]) / sr, double (e.silent[1]) / sr);
    Result g { "G", buf, silentAfter && e.silent[0] == e.silent[1] };
    return { b, g };
}

// Case C: the chain's values changed while off.
Result engineC (const std::shared_ptr<const BakeData>& bake, double sr, int block)
{
    EnginePair e (bake, sr, block);
    e.cmp.from = 0;
    for (const auto& k : cymbalis::chainKeys())
        for (const double v : { k.lo, k.hi })
        {
            auto t = bake->tuning;
            t.cymbal.*(k.member) = v;
            e.tunings.push_back (t);
        }
    const auto p = e.strikes (playing (0.0, 8.0, 301));
    auto te = p, re = p;
    for (int i = 1; i < (int) e.tunings.size(); ++i)
    {
        const std::int64_t at = e.samples (0.2 + 0.4 * (i - 1));
        te.push_back ({ at, Event::tuning, 0, 0.0, i });
        re.push_back ({ at, Event::tuning, 0, 0.0, 0 });
    }
    e.run (te, re, e.samples (8.0));
    return { "C", e.cmp.text (sr), e.cmp.identical() };
}

// Case D: played through the switch-off.
struct DResult
{
    Result result, criterion;
    double worstRiseDb = -1e9, worstRiseAt = 0.0, largestRel = 0.0, largestAt = 0.0;
    std::vector<std::pair<double, double>> levels; // (time, difference re the signal, dB) per 0.5 s
    std::vector<double> excess; // per 0.5 s: dB the difference stands over the ring-out alone's
    double worstExcess = -1e9, worstExcessAt = 0.0;
    int strikesAfter = 0, rises = 0;
};

// The merge leak's limit: in no 0.5 s window from the switch on, where the difference is above
// -120 dBFS, may it stand more than this over what the strikes made while on leave by themselves.
constexpr double kLeakLimitDb = 20.0;

DResult engineD (const std::shared_ptr<const BakeData>& bake, double sr, int block)
{
    constexpr double switchS = 4.0, totalS = 24.0;
    EnginePair e (bake, sr, block);
    e.record = true;
    const auto p = playing (0.0, totalS, 401);
    const auto ev = e.strikes (p);
    auto te = ev;
    te.push_back ({ 0, Event::chainOn });
    te.push_back ({ e.samples (switchS), Event::chainOff });
    e.run (te, ev, e.samples (totalS));

    // The ring-out alone: the same engines playing only the strikes made before the switch.
    EnginePair o (bake, sr, block);
    o.record = true;
    std::vector<Event> whileOn;
    for (const auto& x : ev)
        if (x.at < e.samples (switchS))
            whileOn.push_back (x);
    auto ote = whileOn;
    ote.push_back ({ 0, Event::chainOn });
    ote.push_back ({ o.samples (switchS), Event::chainOff });
    o.run (ote, whileOn, o.samples (totalS));

    DResult d;
    const size_t n = e.recTest.size() / 2;
    const auto rms = [&] (const std::vector<float>* a, const std::vector<float>* b, std::int64_t from, std::int64_t to) {
        double s = 0.0;
        from = std::max<std::int64_t> (from, 0);
        to = std::min<std::int64_t> (to, std::int64_t (n));
        for (std::int64_t i = from; i < to; ++i)
            for (int c = 0; c < 2; ++c)
            {
                const double x = double ((*a)[size_t (2 * i + c)]) - (b != nullptr ? double ((*b)[size_t (2 * i + c)]) : 0.0);
                s += x * x;
            }
        return to > from ? std::sqrt (s / double (2 * (to - from))) : 0.0;
    };
    const std::int64_t w = e.samples (0.02);
    for (const auto& s : p)
    {
        if (s.t < switchS)
            continue;
        ++d.strikesAfter;
        const std::int64_t at = e.samples (s.t);
        const double before = rms (&e.recTest, &e.recRef, at - w, at), after = rms (&e.recTest, &e.recRef, at, at + w);
        double rise = 0.0;
        if (after > 0.0)
            rise = before > 0.0 ? 20.0 * std::log10 (after / before) : 1e9;
        if (rise > 0.5)
            ++d.rises;
        if (rise > d.worstRiseDb)
        {
            d.worstRiseDb = rise;
            d.worstRiseAt = s.t;
        }
    }
    double peak = 0.0;
    for (float x : e.recRef)
        peak = std::max (peak, double (std::fabs (x)));
    for (size_t i = size_t (e.samples (switchS)) * 2; i < e.recTest.size(); ++i)
    {
        const double x = std::fabs (double (e.recTest[i]) - double (e.recRef[i]));
        if (peak > 0.0 && x / peak > d.largestRel)
        {
            d.largestRel = x / peak;
            d.largestAt = double (i / 2) / sr;
        }
    }
    for (double t = switchS; t + 0.5 <= totalS + 1e-9; t += 0.5)
    {
        const double diff = rms (&e.recTest, &e.recRef, e.samples (t), e.samples (t + 0.5));
        const double sig = rms (&e.recRef, nullptr, e.samples (t), e.samples (t + 0.5));
        const double ring = rms (&o.recTest, &o.recRef, o.samples (t), o.samples (t + 0.5));
        d.levels.push_back ({ t, diff > 0.0 && sig > 0.0 ? 20.0 * std::log10 (diff / sig) : -999.0 });
        // The leak: what the difference stands over the ring-out alone, where it is above -120 dBFS.
        const double ex = diff > 1e-6 ? 20.0 * std::log10 (diff / std::max (ring, 1e-12)) : -999.0;
        d.excess.push_back (ex);
        if (ex > d.worstExcess)
        {
            d.worstExcess = ex;
            d.worstExcessAt = t;
        }
    }
    char buf[300];
    std::snprintf (buf, sizeof (buf), "the difference stands at most %.1f dB over what the strikes made while on leave by themselves (at %.1f s; limit %.0f dB where it is above -120 dBFS); largest |difference| %.1f dB re the peak (at %.2f s)",
                   d.worstExcess, d.worstExcessAt, kLeakLimitDb, d.largestRel > 0.0 ? 20.0 * std::log10 (d.largestRel) : -999.0, d.largestAt);
    d.result = { "D (merge leak)", buf, d.worstExcess <= kLeakLimitDb };
    char rise[64];
    std::snprintf (rise, sizeof (rise), d.worstRiseDb > 1e8 ? "from silence" : "%.1f dB", d.worstRiseDb);
    std::snprintf (buf, sizeof (buf), "the task's 0.5 dB criterion: %d of %d strikes after the switch raise the difference by more (worst %s at %.2f s); reported, not checked (docs/PORT_MAP.md)",
                   d.rises, d.strikesAfter, rise, d.worstRiseAt);
    d.criterion = { "D (rise at a strike)", buf, true };
    return d;
}

// ---------------------------------------------------------------------------------------------
// CymbalisProcessor

struct ProcEvent
{
    std::int64_t at;
    enum Kind { noteOn, chainSwitch, otherSwitch } kind;
    int note = 0, velocity = 0;
    bool on = false;
    int preset = 0;
};

struct ProcPair
{
    ProcPair (double rate, int blockSize) : sr (rate), block (blockSize)
    {
        test = std::make_unique<CymbalisProcessor>();
        ref = std::make_unique<CymbalisProcessor>();
    }
    std::unique_ptr<CymbalisProcessor> test, ref;
    double sr;
    int block;
    std::int64_t now = 0;
    Compare cmp { INT64_MAX };
    bool randomBlocks = false;
    std::mt19937 rng { 5 };
    std::int64_t quietSince[2] = { 0, 0 };

    std::int64_t samples (double t) const { return std::int64_t (std::llround (t * sr)); }

    static void prepare (CymbalisProcessor& p, double rate, int blockSize) { p.prepareToPlay (rate, blockSize); }
    void prepareBoth()
    {
        prepare (*test, sr, block);
        prepare (*ref, sr, block);
    }
    // A preset switch, built at once by prepareToPlay in both.
    void setPreset (int k)
    {
        test->setPreset (k);
        ref->setPreset (k);
        prepareBoth();
    }
    void hitVariation (int preset, bool on)
    {
        for (auto* p : { test.get(), ref.get() })
        {
            auto t = p->tuning (preset);
            t.cymbal.hitVariation = on;
            p->setTuning (preset, t);
        }
    }

    std::vector<ProcEvent> notes (int preset, const std::vector<Strike>& p) const
    {
        std::vector<ProcEvent> out;
        for (const auto& s : p)
            out.push_back ({ samples (s.t), ProcEvent::noteOn, test->slotNote (preset, s.row == 0 ? 0 : s.row == 1 ? 1 : 2),
                             std::clamp (int (std::lround (s.v * 127.0)), 1, 127) });
        return out;
    }

    void run (std::vector<ProcEvent> testEv, std::vector<ProcEvent> refEv, std::int64_t until)
    {
        const auto byTime = [] (const ProcEvent& a, const ProcEvent& b) { return a.at < b.at; };
        std::stable_sort (testEv.begin(), testEv.end(), byTime);
        std::stable_sort (refEv.begin(), refEv.end(), byTime);
        size_t kt = 0, kr = 0;
        juce::AudioBuffer<float> bt (2, block), br (2, block);
        std::uniform_int_distribution<int> size (1, block);
        while (now < until)
        {
            const int n = int (std::min<std::int64_t> (until - now, randomBlocks ? size (rng) : block));
            const std::int64_t end = now + n;
            const auto events = [&] (CymbalisProcessor& p, const std::vector<ProcEvent>& ev, size_t& k, juce::MidiBuffer& midi) {
                // The switches change between blocks: before this block, those due by its end.
                for (; k < ev.size() && ev[k].at < end; ++k)
                {
                    if (ev[k].kind == ProcEvent::noteOn)
                        midi.addEvent (juce::MidiMessage::noteOn (p.midiChannel(), ev[k].note, (juce::uint8) ev[k].velocity),
                                       int (std::max<std::int64_t> (0, ev[k].at - now)));
                    else
                        p.setChainOn (ev[k].kind == ProcEvent::chainSwitch ? p.presetIndex() : ev[k].preset, ev[k].on);
                }
            };
            juce::MidiBuffer mt, mr;
            events (*test, testEv, kt, mt);
            events (*ref, refEv, kr, mr);
            bt.setSize (2, n, false, false, true);
            br.setSize (2, n, false, false, true);
            bt.clear();
            br.clear();
            test->processBlock (bt, mt);
            ref->processBlock (br, mr);
            for (int c = 0; c < 2; ++c)
                cmp.add (now, bt.getReadPointer (c), br.getReadPointer (c), n);
            for (int w = 0; w < 2; ++w)
            {
                const auto& b = w == 0 ? bt : br;
                bool zero = true;
                for (int c = 0; c < 2 && zero; ++c)
                    for (int i = 0; i < n && zero; ++i)
                        zero = juce::exactlyEqual (b.getSample (c, i), 0.0f);
                if (! zero)
                    quietSince[w] = end;
            }
            now = end;
        }
    }
    // Until both outputs have been silent (exact zeros) for 1 s, at most `maxS` seconds.
    bool runUntilQuiet (double maxS)
    {
        const std::int64_t limit = now + samples (maxS), second = samples (1.0);
        while (now < limit && (now - quietSince[0] < second || now - quietSince[1] < second))
            run ({}, {}, std::min (limit, now + block));
        return now < limit;
    }
};

Result procA (int preset, double sr, int block, bool hv)
{
    ProcPair p (sr, block);
    p.test->setPreset (preset);
    p.ref->setPreset (preset);
    p.hitVariation (preset, hv);
    p.prepareBoth();
    p.cmp.from = 0;
    p.randomBlocks = true;
    std::mt19937 rng (21);
    std::uniform_int_distribution<std::int64_t> pick (0, p.samples (3.0) - 1);
    std::vector<ProcEvent> te;
    for (int k = 0; k < 40; ++k)
    {
        const bool other = k % 4 == 3; // another preset's switch now and then
        te.push_back ({ pick (rng), other ? ProcEvent::otherSwitch : ProcEvent::chainSwitch, 0, 0, k % 2 == 0, other ? (preset + 1) % cymbalis::kNumPresets : 0 });
    }
    std::sort (te.begin(), te.end(), [] (const ProcEvent& a, const ProcEvent& b) { return a.at < b.at; });
    te.push_back ({ p.samples (3.0) - 1, ProcEvent::chainSwitch, 0, 0, false }); // inside the phase
    p.run (te, {}, p.samples (3.0));
    const double fadeEnd = cymbalis::SizzleChain::kFadeS + 0.01; // the playing after the last fade-out
    const auto n = p.notes (preset, playing (3.0 + fadeEnd, 6.0, 501));
    p.run (n, n, p.samples (9.0 + fadeEnd));
    return { "A (processor)", p.cmp.text (sr), p.cmp.identical() };
}

Result procC (int preset, double sr, int block, bool hv)
{
    ProcPair p (sr, block);
    p.test->setPreset (preset);
    p.ref->setPreset (preset);
    p.hitVariation (preset, hv);
    p.prepareBoth();
    p.cmp.from = 0;
    const auto n = p.notes (preset, playing (0.0, 7.0, 601));
    // The chain's values to each end of their ranges, one every 0.4 s, between blocks.
    std::vector<cymbalis::Tuning> values;
    for (const auto& k : cymbalis::chainKeys())
        for (const double v : { k.lo, k.hi })
        {
            auto t = p.test->tuning (preset);
            t.cymbal.*(k.member) = v;
            values.push_back (t);
        }
    for (size_t i = 0; i < values.size(); ++i)
    {
        const std::int64_t at = p.samples (0.2 + 0.4 * double (i));
        std::vector<ProcEvent> due;
        for (const auto& e : n)
            if (e.at >= p.now && e.at < at)
                due.push_back (e);
        p.run (due, due, at);
        p.test->setTuning (preset, values[i]);
        p.ref->setTuning (preset, p.ref->tuning (preset)); // the same call with its own values
    }
    std::vector<ProcEvent> rest;
    for (const auto& e : n)
        if (e.at >= p.now)
            rest.push_back (e);
    p.run (rest, rest, p.samples (7.0));
    return { "C (processor)", p.cmp.text (sr), p.cmp.identical() };
}

Result procE (int preset, int other, double sr, int block, bool hv, const char* name)
{
    ProcPair p (sr, block);
    p.test->setPreset (preset);
    p.ref->setPreset (preset);
    for (const int k : { preset, other })
        p.hitVariation (k, hv);
    p.prepareBoth();
    p.test->setChainOn (preset, true);
    auto n1 = p.notes (preset, playing (0.0, 2.0, 701));
    p.run (n1, n1, p.samples (2.0));
    p.cmp.from = p.now; // from the switch to the other preset on
    p.setPreset (other);
    auto n2 = p.notes (other, playing (2.0, 1.0, 702));
    p.run (n2, n2, p.samples (3.0));
    p.setPreset (preset);
    p.test->setChainOn (preset, false);
    auto n3 = p.notes (preset, playing (3.0, 6.0, 703));
    p.run (n3, n3, p.samples (9.0));
    return { name, p.cmp.text (sr), p.cmp.identical() };
}

std::vector<Result> procF (int preset, double sr, int block, bool hv)
{
    std::vector<Result> out;
    const auto restoreInto = [&] (CymbalisProcessor& from, CymbalisProcessor& into) {
        juce::MemoryBlock state;
        from.getStateInformation (state);
        into.setStateInformation (state.getData(), (int) state.getSize());
    };
    // F1: saved with the chain on (nothing struck), restored into a new processor, switched off.
    {
        ProcPair p (sr, block);
        p.test->setPreset (preset);
        p.ref->setPreset (preset);
        p.hitVariation (preset, hv);
        p.prepareBoth();
        p.test->setChainOn (preset, true);
        ProcPair q (sr, block);
        restoreInto (*p.test, *q.test);
        restoreInto (*p.ref, *q.ref);
        q.prepareBoth();
        const bool restoredOn = q.test->chainOn (preset);
        q.test->setChainOn (preset, false);
        q.cmp.from = 0;
        const auto n = q.notes (preset, playing (0.0, 6.0, 801));
        q.run (n, n, q.samples (6.0));
        out.push_back ({ "F1 (session)", q.cmp.text (sr) + (restoredOn ? "" : " (the switch was not restored on)"), restoredOn && q.cmp.identical() });
    }
    // F2: on, struck, saved and restored into the same processor while it rings, off, silence.
    {
        ProcPair p (sr, block);
        p.test->setPreset (preset);
        p.ref->setPreset (preset);
        p.hitVariation (preset, hv);
        p.prepareBoth();
        p.test->setChainOn (preset, true);
        const auto n1 = p.notes (preset, playing (0.0, 3.0, 802));
        p.run (n1, n1, p.samples (3.0));
        restoreInto (*p.test, *p.test);
        restoreInto (*p.ref, *p.ref);
        const bool restoredOn = p.test->chainOn (preset);
        p.test->setChainOn (preset, false);
        const bool quiet = p.runUntilQuiet (240.0);
        p.cmp.from = p.now;
        const double t0 = double (p.now) / sr;
        const auto n2 = p.notes (preset, playing (t0, 6.0, 803));
        p.run (n2, n2, p.now + p.samples (6.0));
        char buf[200];
        std::snprintf (buf, sizeof (buf), "%s (silent from %.2f s)%s", p.cmp.text (sr).c_str(), t0 - 1.0, restoredOn ? "" : " (the switch was not restored on)");
        out.push_back ({ "F2 (session)", buf, quiet && restoredOn && p.cmp.identical() });
    }
    // F3: on, struck, off, saved; restored into a new processor.
    {
        ProcPair p (sr, block);
        p.test->setPreset (preset);
        p.ref->setPreset (preset);
        p.hitVariation (preset, hv);
        p.prepareBoth();
        p.test->setChainOn (preset, true);
        const auto n1 = p.notes (preset, playing (0.0, 3.0, 804));
        p.run (n1, n1, p.samples (3.0));
        p.test->setChainOn (preset, false);
        ProcPair q (sr, block);
        restoreInto (*p.test, *q.test);
        restoreInto (*p.ref, *q.ref);
        q.prepareBoth();
        q.cmp.from = 0;
        const auto n2 = q.notes (preset, playing (0.0, 6.0, 805));
        q.run (n2, n2, q.samples (6.0));
        out.push_back ({ "F3 (session)", q.cmp.text (sr), ! q.test->chainOn (preset) && q.cmp.identical() });
    }
    return out;
}

int presetIndex (const char* id)
{
    for (int k = 0; k < cymbalis::kNumPresets; ++k)
        if (std::string (cymbalis::kPresets[(size_t) k].id) == id)
            return k;
    return -1;
}

// One configuration's cases, as table rows, and case D's levels line; its case count and failures.
struct ConfigOutput
{
    std::vector<std::string> rows;
    std::string dLine;
    int cases = 0, failures = 0;
    double engineS = 0.0, processorS = 0.0;
};

struct Config
{
    const char* id;
    int preset, other;
    std::shared_ptr<const BakeData> bake;
    double sr;
    int block;
    bool hv;
};

ConfigOutput runConfig (const Config& c, int nova, bool quiet)
{
    juce::ScopedNoDenormals noDenormals; // as processBlock does, for the engine-level cases too
    ConfigOutput out;
    const auto b = withHitVariation (c.bake, c.hv);
    std::vector<Result> results;
    const auto t0 = std::chrono::steady_clock::now();
    results.push_back (engineA (b, c.sr, c.block));
    const auto bg = engineBG (b, c.sr, c.block);
    results.push_back (bg.first);
    results.push_back (bg.second);
    results.push_back (engineC (b, c.sr, c.block));
    results.push_back (engineFade (b, c.sr, c.block));
    const auto d = engineD (b, c.sr, c.block);
    results.push_back (d.result);
    results.push_back (d.criterion);
    const auto t1 = std::chrono::steady_clock::now();
    results.push_back (procA (c.preset, c.sr, c.block, c.hv));
    results.push_back (procC (c.preset, c.sr, c.block, c.hv));
    results.push_back (procE (c.preset, c.other, c.sr, c.block, c.hv, "E1 (preset switch)"));
    results.push_back (procE (c.preset, nova, c.sr, c.block, c.hv, "E2 (to NOVA RIDE and back)"));
    for (auto& f : procF (c.preset, c.sr, c.block, c.hv))
        results.push_back (f);
    const auto t2 = std::chrono::steady_clock::now();
    out.engineS = std::chrono::duration<double> (t1 - t0).count();
    out.processorS = std::chrono::duration<double> (t2 - t1).count();
    char head[96];
    std::snprintf (head, sizeof (head), "| %s | %.1f kHz | %d | %s | ", c.id, c.sr / 1000.0, c.block, c.hv ? "on" : "off");
    for (const auto& r : results)
    {
        ++out.cases;
        out.failures += r.pass ? 0 : 1;
        if (! quiet || ! r.pass)
            out.rows.push_back (head + r.name + " | " + (r.pass ? "" : "FAILED: ") + r.text + " |");
    }
    out.dLine = std::string (c.id) + " " + std::to_string (int (c.sr)) + " Hz / " + std::to_string (c.block) + (c.hv ? ", HV on:" : ", HV off:");
    for (size_t w = 0; w < d.levels.size(); ++w)
    {
        const auto [t, db] = d.levels[w];
        if (std::fmod (t, 2.0) < 1e-9)
        {
            char buf[64];
            if (db < -998.0)
                std::snprintf (buf, sizeof (buf), " %.0fs none", t);
            else
                std::snprintf (buf, sizeof (buf), " %.0fs %.0f dB (%+.0f)", t, db, std::max (d.excess[w], -99.0));
            out.dLine += buf;
        }
    }
    return out;
}
} // namespace

int main (int argc, char** argv)
{
    bool full = false, quiet = false;
    int threads = (int) std::max (1u, std::thread::hardware_concurrency());
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--full")
            full = true;
        else if (a == "--quiet")
            quiet = true;
        else if (a == "--threads" && i + 1 < argc)
            threads = std::max (1, std::atoi (argv[++i]));
        else
        {
            std::fprintf (stderr, "usage: cymbalis_chain_off [--full] [--quiet] [--threads N]\n");
            return 2;
        }
    }
    juce::ScopedJuceInitialiser_GUI gui;
    const auto started = std::chrono::steady_clock::now();

    std::vector<const char*> presets { "stine_flat_ride", "hammered_thin_ride" };
    std::vector<double> rates { 48000.0 };
    std::vector<int> blocks { 512 };
    std::vector<bool> hvs { true };
    if (full)
    {
        presets = { "stine_flat_ride", "hammered_thin_ride", "swish", "raven", "paperthin" };
        rates = { 44100.0, 48000.0, 96000.0 };
        blocks = { 64, 512 };
        hvs = { true, false };
    }
    CymbalisProcessor bakes; // the plugin's own compiled-in bakes
    const int nova = presetIndex ("nova_ride");
    int failures = 0, cases = 0;
    std::vector<Config> configs;
    for (const char* id : presets)
    {
        const int k = presetIndex (id);
        const auto bake = k >= 0 ? bakes.presetBake (k) : nullptr;
        if (bake == nullptr || ! bake->chain.present)
        {
            std::printf ("%s: NO BAKE WITH A CHAIN\n", id);
            ++failures;
            continue;
        }
        const int other = presetIndex (std::string (id) == "hammered_thin_ride" ? "stine_flat_ride" : "hammered_thin_ride");
        for (const double sr : rates)
            for (const int block : blocks)
                for (const bool hv : hvs)
                    configs.push_back ({ id, k, other, bake, sr, block, hv });
    }
    threads = std::min (threads, (int) configs.size());
    std::printf ("The sizzle chain off: %s set (%zu presets, %zu rates, %zu block sizes, HIT VARIATION %s: %zu configurations, %d at a "
                 "time); each case against the same playing with the chain never on.\n\n",
                 full ? "the full" : "the reduced", presets.size(), rates.size(), blocks.size(), full ? "on and off" : "on", configs.size(), threads);
    // The configurations on `threads` threads (each case has engines and processors of its own);
    // printed in order once all have run.
    std::vector<ConfigOutput> outputs (configs.size());
    std::atomic<size_t> next { 0 };
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t)
        pool.emplace_back ([&] {
            for (size_t i; (i = next.fetch_add (1)) < configs.size();)
            {
                outputs[i] = runConfig (configs[i], nova, quiet);
                std::fprintf (stderr, "  %s %.0f Hz / %d, HIT VARIATION %s: %d of %d passed (%.0f s engine, %.0f s processor)\n", configs[i].id,
                              configs[i].sr, configs[i].block, configs[i].hv ? "on" : "off", outputs[i].cases - outputs[i].failures, outputs[i].cases,
                              outputs[i].engineS, outputs[i].processorS);
            }
        });
    for (auto& t : pool)
        t.join();
    std::printf ("| Preset | Rate | Block | HIT VAR | Case | Result |\n|---|---|---|---|---|---|\n");
    double engineS = 0.0, processorS = 0.0;
    for (const auto& o : outputs)
    {
        for (const auto& r : o.rows)
            std::printf ("%s\n", r.c_str());
        cases += o.cases;
        failures += o.failures;
        engineS += o.engineS;
        processorS += o.processorS;
    }
    std::printf ("\nCase D: the difference (chain on for the first 4 s, then off) against the signal, per 0.5 s window, every 2 s "
                 "from the switch; in brackets, dB over the ring-out alone (the strikes made while on, played by themselves):\n");
    for (const auto& o : outputs)
        std::printf ("- %s\n", o.dLine.c_str());
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - started).count();
    std::printf ("\n%d of %d cases passed in %.0f s (on %d threads; %.0f s of engine cases and %.0f s of processor cases in all).\n", cases - failures,
                 cases, secs, threads, engineS, processorS);
    return failures == 0 ? 0 : 1;
}
