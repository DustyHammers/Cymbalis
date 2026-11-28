#pragma once

// The edge attack check (docs/PORT_MAP.md, live-engine decision "An envelope strike's contact
// plays at its envelope level"), in memory, for any engine class with FastEngine's interface
// (cymbalis_edgeclick runs it on the plugin's engine; tools/cymbalis_identity/check.cpp's
// --edge-click on REF's too, for the figures before the fix).
//
// 36 EDGE hits, at velocities 0.35, 0.5, 0.65, 0.8, 0.9 and 1.0 in turn, 3 s apart, HIT VARIATION
// as baked, at 44.1 kHz in 128-sample blocks. Per hit and channel: the largest |second difference|
// over the hand-over sample (the strike + the contact's length) and the 2 samples either side,
// against its median over the next 2 ms. A step at the hand-over shows as a large ratio.

#include "Bake.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace cymbalis::edgeclick
{

constexpr double kSampleRate = 44100.0;
constexpr int kBlock = 128;
constexpr int kHits = 36;
constexpr double kSpacingS = 3.0;
constexpr double kVelocities[] = { 0.35, 0.5, 0.65, 0.8, 0.9, 1.0 };

struct Render
{
    std::vector<float> L, R;
    std::vector<int64_t> strikes; // each hit's sample
    int contact = 0;              // the contact's length, samples (0: none)
};

// The 36 hits through engine E, split at each strike as the plugin does.
template <class E>
Render render(const std::shared_ptr<const BakeData>& bake, int edge)
{
    Render r;
    E e(bake);
    e.prepare(kSampleRate);
    r.contact = e.contactLength();
    const int64_t total = int64_t(std::ceil(kHits * kSpacingS * kSampleRate));
    r.L.assign(size_t(total), 0.0f);
    r.R.assign(size_t(total), 0.0f);
    for (int h = 0; h < kHits; ++h)
        r.strikes.push_back(int64_t(std::floor(h * kSpacingS * kSampleRate)));
    size_t h = 0;
    const double pos = bake->slots[size_t(edge)].position;
    for (int64_t start = 0; start < total; start += kBlock)
    {
        const int n = int(std::min<int64_t>(kBlock, total - start));
        int at0 = 0;
        for (; h < r.strikes.size() && r.strikes[h] < start + n; ++h)
        {
            const int at = int(r.strikes[h] - start);
            if (at > at0)
            {
                e.process(r.L.data() + start + at0, r.R.data() + start + at0, at - at0);
                at0 = at;
            }
            e.strike(edge, kVelocities[h % 6], pos);
        }
        if (at0 < n)
            e.process(r.L.data() + start + at0, r.R.data() + start + at0, n - at0);
    }
    return r;
}

// Per hit, one channel's ratio: max |d2| over the hand-over +-2 samples / median |d2| over the
// next 2 ms (d2[n] = x[n+1] - 2 x[n] + x[n-1]).
inline std::vector<double> ratios(const std::vector<float>& x, const Render& r)
{
    std::vector<double> out;
    const int64_t next = int64_t(std::lround(0.002 * kSampleRate));
    const auto d2 = [&x](int64_t n) { return std::fabs(double(x[size_t(n + 1)]) - 2.0 * x[size_t(n)] + x[size_t(n - 1)]); };
    for (const int64_t s : r.strikes)
    {
        const int64_t h = s + r.contact;
        double peak = 0.0;
        for (int64_t n = h - 2; n <= h + 2; ++n)
            peak = std::max(peak, d2(n));
        std::vector<double> after;
        for (int64_t n = h + 3; n <= h + 2 + next; ++n)
            after.push_back(d2(n));
        std::nth_element(after.begin(), after.begin() + long(after.size() / 2), after.end());
        const double med = after[after.size() / 2];
        out.push_back(med > 0.0 ? peak / med : 0.0);
    }
    return out;
}

struct Stats
{
    double median = 0.0, p90 = 0.0, max = 0.0;
};

inline Stats stats(std::vector<double> v)
{
    std::sort(v.begin(), v.end());
    const auto at = [&v](double q) {
        const double k = q * double(v.size() - 1);
        const size_t lo = size_t(k);
        const size_t hi = std::min(lo + 1, v.size() - 1);
        return v[lo] + (v[hi] - v[lo]) * (k - double(lo));
    };
    return { at(0.5), at(0.9), v.back() };
}

} // namespace cymbalis::edgeclick
