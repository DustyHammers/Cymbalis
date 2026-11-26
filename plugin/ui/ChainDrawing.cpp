#include "ChainDrawing.h"

#include <algorithm>
#include <cmath>

namespace cymbalis::ui::wire
{

namespace
{
constexpr double pi = 3.14159265358979323846;

// splitmix64: the seeded values (the beads' rates and phases, and the noise's steps).
std::uint64_t mix (std::uint64_t x) noexcept
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

// 0 .. 1 from a 64-bit value.
double unit (std::uint64_t x) noexcept
{
    return (double) (x >> 11) * (1.0 / 9007199254740992.0);
}
} // namespace

ChainDrawing chainDrawing (const Geometry& g, double radiusInches)
{
    ChainDrawing c;
    const double inch = 1.0 / radiusInches;
    // The felt and washer (CymbalHardware.h): the felt 1.5" across and 0.55" tall on the bell top,
    // the washer 0.07" thick on it.
    const double feltR = 0.75 * inch, feltH = 0.55 * inch, washerH = 0.07 * inch;
    const double washerMid = g.restHeight (0.0) + feltH + 0.5 * washerH;
    c.beadRadius = inch / 16.0;
    c.maxLift = 0.12 * inch;
    const double spacing = 0.16 * inch;
    const double r0 = 1.05 * feltR, rLand = 1.9 * feltR, r1 = 0.62;
    const auto theta = [] (double r) { return 118.0 * pi / 180.0 + 0.06 * std::sin (9.0 * r) * r + 0.03 * std::sin (23.0 * r) * r; };
    const auto onSurface = [&] (double r) { return g.surfaceHeight (r, theta (r)) + c.beadRadius; };
    const double zLand = onSurface (rLand);

    // The path, finely: r from r0 to r1, its points and its length along it.
    constexpr int n = 4000;
    std::vector<double> x (n + 1), y (n + 1), z (n + 1), s (n + 1, 0.0);
    for (int i = 0; i <= n; ++i)
    {
        const double r = r0 + (r1 - r0) * i / n, t = theta (r);
        x[(size_t) i] = r * std::cos (t);
        y[(size_t) i] = r * std::sin (t);
        z[(size_t) i] = r < rLand ? washerMid + (zLand - washerMid) * (r - r0) / (rLand - r0) : onSurface (r);
        if (i > 0)
            s[(size_t) i] = s[(size_t) i - 1]
                            + std::sqrt ((x[(size_t) i] - x[(size_t) i - 1]) * (x[(size_t) i] - x[(size_t) i - 1])
                                         + (y[(size_t) i] - y[(size_t) i - 1]) * (y[(size_t) i] - y[(size_t) i - 1])
                                         + (z[(size_t) i] - z[(size_t) i - 1]) * (z[(size_t) i] - z[(size_t) i - 1]));
    }
    // A bead every 0.16" along it, from its start.
    for (int i = 1, k = 0; k < kMaxChainBeads; ++k)
    {
        const double at = k * spacing;
        if (at > s[(size_t) n])
            break;
        while (i < n && s[(size_t) i] < at)
            ++i;
        const double f = s[(size_t) i] > s[(size_t) i - 1] ? (at - s[(size_t) i - 1]) / (s[(size_t) i] - s[(size_t) i - 1]) : 0.0;
        const double bx = x[(size_t) i - 1] + f * (x[(size_t) i] - x[(size_t) i - 1]);
        const double by = y[(size_t) i - 1] + f * (y[(size_t) i] - y[(size_t) i - 1]);
        const double bz = z[(size_t) i - 1] + f * (z[(size_t) i] - z[(size_t) i - 1]);
        c.beads.push_back ({ std::hypot (bx, by), std::atan2 (by, bx), bz });
    }

    // Each bead's motion: a rate of 8 to 20 Hz and a phase, from the seed.
    c.seed = 0x5155a1e5c4a17ULL;
    for (size_t b = 0; b < c.beads.size(); ++b)
    {
        c.rateHz.push_back (8.0 + 12.0 * unit (mix (c.seed ^ (2 * b + 1))));
        c.phase.push_back (unit (mix (c.seed ^ (2 * b + 2))));
    }
    return c;
}

double chainNoise (const ChainDrawing& c, size_t bead, double seconds) noexcept
{
    // Random values at steps 1 / rate apart, joined smoothly (smoothstep).
    const double t = seconds * c.rateHz[bead] + c.phase[bead];
    const double k = std::floor (t), f = t - k;
    const auto at = [&] (double step) {
        return unit (mix (c.seed + 0x632be59bd9b4e019ULL * (bead + 1) + (std::uint64_t) (std::int64_t) step * 0x9e3779b97f4a7c15ULL));
    };
    const double w = f * f * (3.0 - 2.0 * f);
    return at (k) + (at (k + 1.0) - at (k)) * w;
}

void chainLifts (const ChainDrawing& c, double envelope, double seconds, float* lifts) noexcept
{
    for (size_t b = 0; b < c.beads.size(); ++b)
        lifts[b] = envelope > 0.0 ? (float) (c.maxLift * envelope * chainNoise (c, b, seconds)) : 0.0f;
}

void projectBead (const Camera& cam, const Frame& f, const ChainDrawing& c, size_t bead, double lift, double& sx, double& sy,
                  double& radius) noexcept
{
    // As projectHardware (spin, then the tilt's plane), and the perspective at the bead for its
    // radius on screen.
    const auto& b = c.beads[bead];
    const double phi = b.theta + f.spinDeg * pi / 180.0;
    const double X = b.r * std::cos (phi), Y = b.r * std::sin (phi);
    const double Z = b.z + lift + f.lift - 2.2 * (X * f.tiltX + Y * f.tiltY);
    cam.project (X, Y, Z, sx, sy);
    const double zv = Y * cam.cosE + Z * cam.sinE;
    radius = cam.scale * c.beadRadius * cam.distance / (cam.distance - zv);
}

} // namespace cymbalis::ui::wire
