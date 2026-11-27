#pragma once

// The edge ring check (docs/PORT_MAP.md, live-engine decision "An envelope strike keeps its ring
// when another strike follows"), in memory, for any engine class with FastEngine's interface
// (cymbalis_edgering runs it on the plugin's engine; tools/cymbalis_identity/check.cpp's
// --edge-ring on REF's too, for the figures before the fix).
//
// EDGE at 0.8 alone, against EDGE at 0.8 followed `after` seconds later by a strike on `second`
// (BOW, BELL) at velocity 1e-6, at 48 kHz in 128-sample blocks: the second strike adds nothing,
// so the edge's ring should not change.
//   - bands: the edge's level change (dB) in seven bands over 0.05-2 s after the second strike,
//     both channels' energy (one FFT of the stretch, Hann window);
//   - dip: the difference's energy over the 4 ms after the second strike, dB re the edge alone
//     there (both channels).

#include "Bake.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <memory>
#include <vector>

namespace cymbalis::edgering
{

constexpr int kBands = 7;
constexpr double kBandEdges[kBands + 1] = { 0.0, 300.0, 700.0, 1500.0, 3000.0, 6000.0, 12000.0, 22000.0 };
constexpr const char* kBandNames[kBands] = { "0-300", "300-700", "700-1.5k", "1.5-3k", "3-6k", "6-12k", "12-22k" };
constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 128;
constexpr double kEdgeVelocity = 0.8;
constexpr double kSecondVelocity = 1e-6;
constexpr double kBandsFrom = 0.05, kBandsTo = 2.0; // s after the second strike
constexpr double kDipS = 0.004;

struct Result
{
    double band[kBands] {}; // dB, with the second strike against the edge alone
    double dipDb = -400.0;  // dB re the edge alone
};

// Renders `total` samples in blocks, split at each strike as the plugin does.
template <class E>
void render(E& e, const std::vector<std::pair<int64_t, int>>& hits, const std::vector<double>& velocity,
            const std::vector<double>& position, int64_t total, std::vector<float>& L, std::vector<float>& R)
{
    L.assign(size_t(total), 0.0f);
    R.assign(size_t(total), 0.0f);
    size_t h = 0;
    for (int64_t start = 0; start < total; start += kBlock)
    {
        const int n = int(std::min<int64_t>(kBlock, total - start));
        int pos = 0;
        for (; h < hits.size() && hits[h].first < start + n; ++h)
        {
            const int at = int(hits[h].first - start);
            if (at > pos)
            {
                e.process(L.data() + start + pos, R.data() + start + pos, at - pos);
                pos = at;
            }
            e.strike(hits[h].second, velocity[h], position[h]);
        }
        if (pos < n)
            e.process(L.data() + start + pos, R.data() + start + pos, n - pos);
    }
}

// In-place radix-2 FFT (size a power of two).
inline void fft(std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * 3.14159265358979323846 / double(len);
        const std::complex<double> w(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> u(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const std::complex<double> x = a[i + k], y = a[i + k + len / 2] * u;
                a[i + k] = x + y;
                a[i + k + len / 2] = x - y;
                u *= w;
            }
        }
    }
}

// Each band's energy, both channels, over samples [i0, i1) (Hann window).
inline void bandEnergy(const std::vector<float>& L, const std::vector<float>& R, size_t i0, size_t i1, double* out)
{
    const size_t len = i1 - i0;
    size_t n = 1;
    while (n < len)
        n <<= 1;
    std::fill(out, out + kBands, 0.0);
    for (const auto* x : { &L, &R })
    {
        std::vector<std::complex<double>> a(n);
        for (size_t i = 0; i < len; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * double(i) / double(len - 1));
            a[i] = double((*x)[i0 + i]) * w;
        }
        fft(a);
        for (size_t k = 1; k < n / 2; ++k)
        {
            const double f = double(k) * kSampleRate / double(n);
            for (int b = 0; b < kBands; ++b)
                if (f >= kBandEdges[b] && f < kBandEdges[b + 1])
                    out[b] += std::norm(a[k]);
        }
    }
}

template <class E>
Result measure(const std::shared_ptr<const BakeData>& bake, int edge, int second, double after)
{
    const int64_t at = int64_t(std::floor(after * kSampleRate));
    const int64_t total = at + int64_t(std::ceil((kBandsTo + 0.05) * kSampleRate));
    std::vector<float> La, Ra, Lb, Rb;
    {
        E e(bake);
        e.prepare(kSampleRate);
        render(e, { { 0, edge } }, { kEdgeVelocity }, { bake->slots[size_t(edge)].position }, total, La, Ra);
    }
    {
        E e(bake);
        e.prepare(kSampleRate);
        render(e, { { 0, edge }, { at, second } }, { kEdgeVelocity, kSecondVelocity },
               { bake->slots[size_t(edge)].position, bake->slots[size_t(second)].position }, total, Lb, Rb);
    }
    Result r;
    const size_t i0 = size_t(at + int64_t(std::lround(kBandsFrom * kSampleRate)));
    const size_t i1 = size_t(at + int64_t(std::lround(kBandsTo * kSampleRate)));
    double ea[kBands], eb[kBands];
    bandEnergy(La, Ra, i0, i1, ea);
    bandEnergy(Lb, Rb, i0, i1, eb);
    for (int b = 0; b < kBands; ++b)
        r.band[b] = ea[b] > 0.0 && eb[b] > 0.0 ? 10.0 * std::log10(eb[b] / ea[b]) : 0.0;
    double ref = 0.0, diff = 0.0;
    const size_t d1 = size_t(at + int64_t(std::lround(kDipS * kSampleRate)));
    for (size_t i = size_t(at); i < d1; ++i)
    {
        ref += double(La[i]) * La[i] + double(Ra[i]) * Ra[i];
        const double dl = double(Lb[i]) - La[i], dr = double(Rb[i]) - Ra[i];
        diff += dl * dl + dr * dr;
    }
    r.dipDb = ref > 0.0 ? (diff > 0.0 ? 10.0 * std::log10(diff / ref) : -400.0) : 0.0;
    return r;
}

} // namespace cymbalis::edgering
