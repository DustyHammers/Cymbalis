#include "CymbalHardware.h"

#include "dsp/Pcg64.h"

#include <algorithm>
#include <cmath>

namespace cymbalis::ui::wire
{

namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double twoPi = 2.0 * pi;

// The reference's colours (bell_hardware; BGR there), RGB.
constexpr Rgb felt { 0x90 / 255.0f, 0x8c / 255.0f, 0x8a / 255.0f };      // #908C8A, its fibres at 0.8: #73706E
constexpr Rgb washer { 0x60 / 255.0f, 0x5d / 255.0f, 0x5d / 255.0f };    // #605D5D
constexpr Rgb steel { 0xae / 255.0f, 0xaa / 255.0f, 0xa8 / 255.0f };     // #AEAAA8, the clutch's steel
constexpr Rgb highlight { 0xde / 255.0f, 0xd8 / 255.0f, 0xd4 / 255.0f }; // #DED8D4

Rgb times (Rgb c, float k) { return { c.r * k, c.g * k, c.b * k }; }

// Points of a hull: the felt, washer and boss have 98, each wing 120.
constexpr size_t kMaxSolidPoints = 128;
} // namespace

CymbalHardware bellHardware (double radiusInches, double bellTop)
{
    CymbalHardware h;
    const double inch = 1.0 / radiusInches, z0 = bellTop;
    const double feltR = 0.75 * inch, feltH = 0.55 * inch;   // 1.5" across, 0.55" tall
    const double washerR = 0.62 * inch, washerH = 0.07 * inch; // 1.24" across, 0.07" thick
    const double bossR = 0.22 * inch, bossH = 0.42 * inch;     // 0.44" across, 0.42" tall
    const double feltTop = z0 + feltH, washerTop = feltTop + washerH, bossTop = washerTop + bossH;

    // hh_draw_cyl, 1 unit wide: a vertical cylinder on the axis from za to zb of radius r, its
    // lower circle at 0.8 x its colour, its upper circle, any rings at 0.6 x, each n segments
    // around; and its two side lines (at x -r and +r).
    const auto cylinder = [&h] (double za, double zb, double r, Rgb c, int n, std::initializer_list<double> rings) {
        const auto circle = [&] (double z, Rgb colour) {
            for (int k = 0; k < n; ++k)
            {
                const double t0 = twoPi * k / n, t1 = twoPi * (k + 1) / n;
                h.lines.push_back ({ r * std::cos (t0), r * std::sin (t0), z, r * std::cos (t1), r * std::sin (t1), z, colour, 1.0f });
            }
        };
        circle (za, times (c, 0.8f));
        circle (zb, c);
        for (double z : rings)
            circle (z, times (c, 0.6f));
        for (double sx : { -1.0, 1.0 })
            h.lines.push_back ({ sx * r, 0.0, za, sx * r, 0.0, zb, c, 1.0f });
    };

    // The felt, with a ring at half height, and its fibres: 70 short vertical strokes over its
    // front half (y > 0 faces the camera), at angles 0.05 pi .. 0.95 pi each moved by up to
    // +-0.02, each from 5-30 % up the felt and 30-60 % of its height long. The spread is the
    // reference's numpy default_rng(4): the PCG64 state that seed starts from, drawn in the same
    // order (the 70 angle offsets, then per fibre its start and length).
    cylinder (z0, feltTop, feltR, felt, 64, { z0 + 0.5 * feltH });
    {
        Pcg64 rng (Pcg64::make128 (0x76339b92f88ff6f3ULL, 0x4f69f00c3a703764ULL), Pcg64::make128 (0xd1596989777d52acULL, 0xa8edb0f4250cac59ULL));
        std::array<double, 70> angle {};
        for (size_t k = 0; k < angle.size(); ++k)
            angle[k] = 0.05 * pi + (double) k * ((0.95 * pi - 0.05 * pi) / 69.0) + rng.uniform (-0.02, 0.02 - -0.02);
        const Rgb fibre = times (felt, 0.8f);
        for (double a : angle)
        {
            const double start = z0 + rng.uniform (0.05, 0.3 - 0.05) * feltH;
            const double end = start + rng.uniform (0.3, 0.6 - 0.3) * feltH;
            const double x = feltR * std::cos (a), y = feltR * std::sin (a);
            h.lines.push_back ({ x, y, start, x, y, end, fibre, 1.0f });
        }
    }
    cylinder (feltTop, washerTop, washerR, washer, 64, {});
    cylinder (washerTop, bossTop, bossR, steel, 48, { washerTop + 0.5 * bossH });

    // The wings: a rounded paddle each side along x, a plate 0.09" thick (its outline at y
    // -0.045" behind, at 0.7 x, and +0.045" in front), its outline an ellipse 0.54" wide and 0.8"
    // tall centred 0.27" out from the boss's side and 0.5" above the washer, tilted 18 degrees
    // outward (the reference's x' = boss + (x - boss) cos 18 + (z - washer top) sin 18 x 0.6, no
    // nearer the axis than 0.9 x the boss's radius); 60 points around, the last the first again.
    // Highlights across the plate at its outermost and topmost points.
    std::vector<std::array<double, 3>> wingSolids[2];
    for (int side = 0; side < 2; ++side)
    {
        const double s = side == 0 ? -1.0 : 1.0;
        const double tilt = 18.0 * pi / 180.0, half = 0.045 * inch;
        std::array<double, 60> X {}, Z {};
        for (size_t k = 0; k < X.size(); ++k)
        {
            const double t = -0.5 * pi + (double) k * (2.0 * pi / 59.0);
            const double xe = bossR + 0.27 * inch + 0.27 * inch * std::cos (t), ze = washerTop + 0.5 * inch + 0.4 * inch * std::sin (t);
            const double xr = bossR + (xe - bossR) * std::cos (tilt) + (ze - washerTop) * std::sin (tilt) * 0.6;
            X[k] = s * std::max (xr, 0.9 * bossR);
            Z[k] = ze;
        }
        for (const auto& [y, colour] : { std::pair<double, Rgb> { -half, times (steel, 0.7f) }, { half, steel } })
            for (size_t k = 0; k + 1 < X.size(); ++k)
                h.lines.push_back ({ X[k], y, Z[k], X[k + 1], y, Z[k + 1], colour, 1.0f });
        size_t outermost = 0, topmost = 0;
        for (size_t k = 1; k < X.size(); ++k)
        {
            if (std::abs (X[k]) > std::abs (X[outermost]))
                outermost = k;
            if (Z[k] > Z[topmost])
                topmost = k;
        }
        for (size_t k : { outermost, topmost })
            h.lines.push_back ({ X[k], -half, Z[k], X[k], half, Z[k], highlight, 1.0f });
        for (double y : { -half, half })
            for (size_t k = 0; k < X.size(); ++k)
                wingSolids[side].push_back ({ X[k], y, Z[k] });
    }

    // The silhouettes: the felt, washer and boss (their end circles, 49 points each, the last the
    // first again, as sil_cyl), then the wings.
    for (const auto& [za, zb, r] : { std::array<double, 3> { z0, feltTop, feltR }, { feltTop, washerTop, washerR }, { washerTop, bossTop, bossR } })
    {
        std::vector<std::array<double, 3>> points;
        for (double z : { za, zb })
            for (int k = 0; k < 49; ++k)
            {
                const double t = twoPi * k / 48.0;
                points.push_back ({ r * std::cos (t), r * std::sin (t), z });
            }
        h.solids.push_back (std::move (points));
    }
    for (auto& w : wingSolids)
        h.solids.push_back (std::move (w));
    return h;
}

void projectHardwarePoint (const Camera& cam, const Frame& f, double x, double y, double z, double& sx, double& sy) noexcept
{
    projectHardware (cam, f, f.lift, x, y, z, sx, sy);
}

void hardwareSilhouette (const Camera& cam, const Frame& f, const std::vector<std::array<double, 3>>& solid,
                         std::vector<std::array<float, 2>>& hull)
{
    std::array<std::array<double, 2>, kMaxSolidPoints> p {};
    const size_t n = std::min (solid.size(), p.size());
    for (size_t k = 0; k < n; ++k)
        projectHardwarePoint (cam, f, solid[k][0], solid[k][1], solid[k][2], p[k][0], p[k][1]);
    // Andrew's monotone chain, as silhouette().
    std::sort (p.begin(), p.begin() + (std::ptrdiff_t) n);
    const auto cross = [] (const std::array<double, 2>& o, const std::array<double, 2>& a, const std::array<double, 2>& b) {
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]);
    };
    std::array<std::array<double, 2>, 2 * kMaxSolidPoints> h {};
    size_t m = 0;
    for (size_t i = 0; i < n; ++i)
    {
        while (m >= 2 && cross (h[m - 2], h[m - 1], p[i]) <= 0.0)
            --m;
        h[m++] = p[i];
    }
    for (size_t i = n - 1, lower = m + 1; i-- > 0;)
    {
        while (m >= lower && cross (h[m - 2], h[m - 1], p[i]) <= 0.0)
            --m;
        h[m++] = p[i];
    }
    hull.clear();
    for (size_t i = 0; i + 1 < m; ++i) // the last is the first again
        hull.push_back ({ (float) h[i][0], (float) h[i][1] });
}

} // namespace cymbalis::ui::wire
