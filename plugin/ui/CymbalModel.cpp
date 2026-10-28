#include "CymbalModel.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace cymbalis::ui::wire
{

namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double twoPi = 2.0 * pi;
constexpr std::array<double, 6> kArcT { 0.0, 0.2, 0.4, 0.6, 0.8, 1.0 }; // points along a hammer arc
constexpr int kM = 7;                                                    // m = 0 .. 6

// The wobble of the reference (MOT): amplitude, decay (s), frequency (Hz) and spin (degrees),
// per zone.
struct Motion
{
    double amplitude, tau, frequency, spin;
};
constexpr std::array<Motion, 3> kMotion { {
    { 0.002, 0.3, 2.6, 0.0 },  // bell
    { 0.006, 0.5, 2.0, 0.0 },  // bow
    { 0.025, 1.3, 1.4, 0.15 }, // edge
} };

// numpy.linspace (start, stop, num): start + i * step, the last exactly stop.
std::vector<double> linspace (double start, double stop, int num)
{
    std::vector<double> v ((size_t) num);
    const double step = num > 1 ? (stop - start) / (double) (num - 1) : 0.0;
    for (int i = 0; i < num; ++i)
        v[(size_t) i] = (double) i * step + start;
    if (num > 1)
        v.back() = stop;
    return v;
}

// numpy.arange (start, stop, step) for doubles, value for value: ceil ((stop - start) / step)
// values, start, start + step, then start + i * delta with delta = (start + step) - start (as
// numpy fills them). The photo brightness is looked up by r / 0.005, so the last bits matter.
std::vector<double> arange (double start, double stop, double step)
{
    const int n = std::max (0, (int) std::ceil ((stop - start) / step));
    std::vector<double> v ((size_t) n);
    const double delta = (start + step) - start;
    for (int i = 0; i < n; ++i)
        v[(size_t) i] = i == 0 ? start : i == 1 ? start + step : start + (double) i * delta;
    return v;
}

int sign (double x) { return (x > 0.0) - (x < 0.0); }

// scipy's PchipInterpolator slope at an end (_edge_case).
double pchipEdge (double h0, double h1, double m0, double m1)
{
    const double d = ((2.0 * h0 + h1) * m0 - h0 * m1) / (h0 + h1);
    if (sign (d) != sign (m0))
        return 0.0;
    if (sign (m0) != sign (m1) && std::abs (d) > 3.0 * std::abs (m0))
        return 3.0 * m0;
    return d;
}

// A uniform number in [a, b) from a fixed-seed sequence (the surface lines' layout, the dents).
struct Uniform
{
    std::mt19937_64 gen { 11 };
    double operator() (double a, double b) { return a + (b - a) * (double) (gen() >> 11) * 0x1.0p-53; }
};

// numpy.gradient along a line of samples at coordinates x: second order inside, first order at
// the ends.
double gradientAt (const double* y, const double* x, int n, int i, int stride = 1)
{
    const auto Y = [&] (int k) { return y[k * stride]; };
    if (i == 0)
        return (Y (1) - Y (0)) / (x[1] - x[0]);
    if (i == n - 1)
        return (Y (n - 1) - Y (n - 2)) / (x[n - 1] - x[n - 2]);
    const double hs = x[i] - x[i - 1], hd = x[i + 1] - x[i];
    return (-hd / (hs * (hd + hs))) * Y (i - 1) + ((hd - hs) / (hs * hd)) * Y (i) + (hs / (hd * (hd + hs))) * Y (i + 1);
}

// The light of a surface with slopes dzr, dzt at angle theta (the reference's shade).
double shadeOf (double dzr, double dzt, double theta)
{
    const double ny = -(dzr * std::sin (theta) + dzt * std::cos (theta));
    return std::clamp ((ny * -0.55 + 0.83) / std::sqrt (dzr * dzr + dzt * dzt + 1.0), 0.0, 1.0);
}

// Douglas-Peucker, as the reference's _simplify_lines keep(): the samples of the polyline P to
// keep, its ends and every `floor`th always; a sample splits a span when its largest coordinate
// difference from the span's chord, interpolated by sample index, exceeds `tolerance`.
std::vector<int> simplify (const std::vector<std::array<double, 4>>& P, int floor, double tolerance)
{
    const int n = (int) P.size();
    std::vector<char> keep ((size_t) n, 0);
    keep.front() = keep.back() = 1;
    for (int i = 0; i < n; i += floor)
        keep[(size_t) i] = 1;
    std::vector<std::pair<int, int>> stack { { 0, n - 1 } };
    while (! stack.empty())
    {
        const auto [a, b] = stack.back();
        stack.pop_back();
        if (b <= a + 1)
            continue;
        double worst = -1.0;
        int at = a + 1;
        for (int i = a + 1; i < b; ++i)
        {
            const double t = (double) (i - a) / (double) (b - a);
            double d = 0.0;
            for (size_t c = 0; c < 4; ++c)
                d = std::max (d, std::abs (P[(size_t) i][c] - (P[(size_t) a][c] + (P[(size_t) b][c] - P[(size_t) a][c]) * t)));
            if (d > worst)
            {
                worst = d;
                at = i;
            }
        }
        if (worst > tolerance)
        {
            keep[(size_t) at] = 1;
            stack.push_back ({ a, at });
            stack.push_back ({ at, b });
        }
    }
    std::vector<int> kept;
    for (int i = 0; i < n; ++i)
        if (keep[(size_t) i] != 0)
            kept.push_back (i);
    return kept;
}
} // namespace

const std::array<Mode, kModes>& modes()
{
    // z: the zeros of J_m (UI_SPEC section 11).
    static const std::array<Mode, kModes> table { {
        { 0, 0, 2.4048 }, { 0, 1, 5.5201 }, { 0, 2, 8.6537 },
        { 1, 0, 3.8317 }, { 1, 1, 7.0156 }, { 1, 2, 10.1735 },
        { 2, 0, 5.1356 }, { 2, 1, 8.4172 }, { 2, 2, 11.6198 },
        { 3, 0, 6.3802 }, { 3, 1, 9.7610 }, { 3, 2, 13.0152 },
        { 4, 0, 7.5883 }, { 4, 1, 11.0647 }, { 4, 2, 14.3725 },
        { 5, 0, 8.7715 }, { 5, 1, 12.3386 }, { 5, 2, 15.7002 },
        { 6, 0, 9.9361 }, { 6, 1, 13.5893 }, { 6, 2, 17.0038 },
    } };
    return table;
}

double besselJ (int m, double x)
{
    constexpr int n = 64;
    double sum = 0.0;
    for (int k = 0; k < n; ++k)
    {
        const double t = ((double) k + 0.5) * pi / (double) n;
        sum += std::cos ((double) m * t - x * std::sin (t));
    }
    return sum / (double) n;
}

const std::array<float, kFlashProfileSize>& flashProfile()
{
    // The disc of radius 1 blurred at sigma 0.6, at distance d: the integral over the disc of
    // the Gaussian, in polar coordinates rho, t around its centre (midpoint sums, 128 x 64).
    static const auto table = [] {
        constexpr double sigma = 0.6, s2 = sigma * sigma;
        constexpr int nr = 128, nt = 64;
        std::array<float, kFlashProfileSize> f {};
        for (int u = 0; u < kFlashProfileSize; ++u)
        {
            const double d = u / 16.0;
            double sum = 0.0;
            for (int i = 0; i < nr; ++i)
            {
                const double rho = (i + 0.5) / nr;
                double ring = 0.0;
                for (int k = 0; k < nt; ++k)
                {
                    const double t = (k + 0.5) * pi / nt;
                    ring += std::exp (-(rho * rho + d * d - 2.0 * rho * d * std::cos (t)) / (2.0 * s2));
                }
                sum += rho / s2 * ring / nt / nr;
            }
            f[(size_t) u] = (float) sum;
        }
        return f;
    }();
    return table;
}

Camera::Camera()
    : sinE (std::sin (elevationDeg * pi / 180.0)), cosE (std::cos (elevationDeg * pi / 180.0))
{
}

void Camera::project (double X, double Y, double Z, double& sx, double& sy) const noexcept
{
    const double yv = Y * sinE - Z * cosE;
    const double zv = Y * cosE + Z * sinE;
    const double persp = distance / (distance - zv);
    sx = cx + scale * X * persp;
    sy = cy + scale * yv * persp;
}



Rgb bronze (double shade, double ytl, Metal metal, double factor, double dent, double glow) noexcept
{
    const double light = std::clamp (((shade - 0.86) / 0.14) * 0.8 + 0.2 * ytl, 0.0, 1.0);
    const double base = std::pow (0.25 + 0.75 * light, 1.4);
    const auto channel = [&] (float dark, float bright, float lathed, float gold, float hot) {
        double c = dark + (bright - dark) * base;
        switch (metal)
        {
            case Metal::tint: c = c * 0.15 + gold * (0.62 + 0.38 * light) * factor; break;
            case Metal::gold: c = c * 0.25 + gold * (0.42 + 0.48 * light) * factor; break;
            case Metal::lathe: c = c * 0.55 + lathed * (0.30 + 0.35 * light); break;
            case Metal::plain: break;
        }
        // A dent's walls: the lit side brightens toward the highlight, the shadowed side fades.
        c = dent > 1.0 ? c + (bright * 1.15 - c) * std::min (dent - 1.0, 1.0) : c * dent;
        return (float) (c + (hot - c) * glow);
    };
    using namespace colour;
    return { channel (roseDark.r, roseLight.r, colour::lathe.r, colour::gold.r, hot.r),
             channel (roseDark.g, roseLight.g, colour::lathe.g, colour::gold.g, hot.g),
             channel (roseDark.b, roseLight.b, colour::lathe.b, colour::gold.b, hot.b) };
}

//==============================================================================
Geometry::Geometry (const CymbalShape& s) : shape (s), bell (s.bell)
{
    // The profile in fractions of R, and scipy's PCHIP slopes (Fritsch-Carlson, with its
    // weighted harmonic mean and end conditions).
    for (int k = 0; k < s.numProfile; ++k)
    {
        px.push_back (s.profile[(size_t) k].r / s.R);
        py.push_back (s.profile[(size_t) k].z / s.R);
    }
    const size_t n = px.size();
    std::vector<double> h (n - 1), m (n - 1);
    for (size_t k = 0; k + 1 < n; ++k)
    {
        h[k] = px[k + 1] - px[k];
        m[k] = (py[k + 1] - py[k]) / h[k];
    }
    pd.assign (n, 0.0);
    if (n == 2)
        pd[0] = pd[1] = m[0];
    else
    {
        for (size_t k = 1; k + 1 < n; ++k)
        {
            if (sign (m[k]) != sign (m[k - 1]) || sign (m[k]) == 0 || sign (m[k - 1]) == 0)
                continue;
            const double w1 = 2.0 * h[k] + h[k - 1], w2 = h[k] + 2.0 * h[k - 1];
            pd[k] = (w1 + w2) / (w1 / m[k - 1] + w2 / m[k]);
        }
        pd[0] = pchipEdge (h[0], h[1], m[0], m[1]);
        pd[n - 1] = pchipEdge (h[n - 2], h[n - 3], m[n - 2], m[n - 3]);
    }
    buildLum();
    if (s.dented)
        buildDentedMesh();
    else
        buildMesh();
    buildSurface();
    buildModes();
}

double Geometry::height (double r) const noexcept
{
    const double x = std::clamp (r, 0.0, 1.0);
    size_t k = 0;
    while (k + 2 < px.size() && x > px[k + 1])
        ++k;
    const double hk = px[k + 1] - px[k], t = (x - px[k]) / hk, t2 = t * t, t3 = t2 * t;
    return py[k] * (2.0 * t3 - 3.0 * t2 + 1.0) + hk * pd[k] * (t3 - 2.0 * t2 + t) + py[k + 1] * (-2.0 * t3 + 3.0 * t2)
           + hk * pd[k + 1] * (t3 - t2);
}

double Geometry::slope (double r) const noexcept
{
    const double x = std::clamp (r, 0.0, 1.0);
    size_t k = 0;
    while (k + 2 < px.size() && x > px[k + 1])
        ++k;
    const double hk = px[k + 1] - px[k], t = (x - px[k]) / hk, t2 = t * t;
    return (py[k] * (6.0 * t2 - 6.0 * t) + py[k + 1] * (-6.0 * t2 + 6.0 * t)) / hk + pd[k] * (3.0 * t2 - 4.0 * t + 1.0)
           + pd[k + 1] * (3.0 * t2 - 2.0 * t);
}

// The reference's lum(): a moving average of lumSmooth samples (padded with the edge values),
// divided by the 98th percentile of entries 5 .. 79 (numpy's linear interpolation).
void Geometry::buildLum()
{
    if (shape.photoLum == nullptr)
        return;
    const auto& raw = *shape.photoLum;
    const int n = std::max (1, shape.lumSmooth), pad = n / 2, size = (int) raw.size();
    for (int i = 0; i < size; ++i)
    {
        double sum = 0.0;
        for (int t = 0; t < n; ++t)
            sum += raw[(size_t) std::clamp (i + t - pad, 0, size - 1)] / n;
        lumTable[(size_t) i] = sum;
    }
    std::vector<double> part (lumTable.begin() + 5, lumTable.begin() + 80);
    std::sort (part.begin(), part.end());
    const double at = 0.98 * (double) (part.size() - 1);
    const size_t lo = (size_t) at;
    const double p98 = part[lo] + (part[std::min (lo + 1, part.size() - 1)] - part[lo]) * (at - (double) lo);
    for (auto& v : lumTable)
        v /= p98;
    hasLum = true;
}

double Geometry::lum (double r) const noexcept
{
    if (! hasLum)
        return 1.0;
    const int i = std::min ((int) (r / 0.005), (int) lumTable.size() - 1);
    return std::clamp (lumTable[(size_t) std::max (i, 0)], 0.3, 1.15);
}

double Geometry::strengthAt (double r) const noexcept
{
    for (int k = 0; k < shape.numGold; ++k)
        if (shape.gold[(size_t) k].from <= r && r <= shape.gold[(size_t) k].to)
            return shape.gold[(size_t) k].strength;
    return 1.0;
}

Metal Geometry::metalAt (double r, double& factor) const noexcept
{
    factor = 1.0;
    if (shape.hasTint && shape.tint.from <= r && r <= shape.tint.to)
    {
        factor = lum (r);
        return Metal::tint;
    }
    for (int k = 0; k < shape.numGold; ++k)
        if (shape.gold[(size_t) k].from <= r && r <= shape.gold[(size_t) k].to)
        {
            factor = lum (r) * shape.gold[(size_t) k].strength;
            return Metal::gold;
        }
    for (int k = 0; k < shape.numLathe; ++k)
        if (shape.lathe[(size_t) k].from <= r && r <= shape.lathe[(size_t) k].to)
            return Metal::lathe;
    return Metal::plain;
}

// Rings: linspace(0.03, bell, max(4, int(bell / 0.04))), then linspace(bell + 0.035, 0.92,
// max(6, int((0.92 - bell) / 0.035))), then 0.94, 0.96, 0.98 and 1.0. Spokes on every second
// sample, from the first ring to the rim. A segment's metal is that of its middle.
void Geometry::buildMesh()
{
    rings = linspace (0.03, bell, std::max (4, (int) (bell / 0.04)));
    for (double r : linspace (bell + 0.035, 0.92, std::max (6, (int) ((0.92 - bell) / 0.035))))
        rings.push_back (r);
    for (double r : { 0.94, 0.96, 0.98, 1.0 })
        rings.push_back (r);
    numRings = (int) rings.size();
    bellRing = 0;
    for (int i = 1; i < numRings; ++i)
        if (std::abs (rings[(size_t) i] - bell) < std::abs (rings[(size_t) bellRing] - bell))
            bellRing = i;
    for (int i = 0; i < numRings; ++i)
    {
        const double r = rings[(size_t) i];
        ringHeight.push_back (height (r));
        ringStiff.push_back (std::clamp ((r - (bell - 0.1)) / 0.15, 0.12, 1.0));
        for (int j = 0; j < kSamples; ++j)
        {
            vertexR.push_back (r);
            vertexTheta.push_back (twoPi * j / kSamples);
            vertexZ.push_back (ringHeight.back());
            vertexDent.push_back (1.0f);
        }
    }

    const auto at = [] (int i, int j) { return (std::uint32_t) (i * kSamples + j); };
    const auto ring = [&] (int i) {
        for (int j = 0; j < kSamples; ++j)
        {
            lineIndices.push_back (at (i, j));
            lineIndices.push_back (at (i, (j + 1) % kSamples));
        }
    };
    const bool thickBell = shape.bellLine && bellRing != numRings - 1;
    for (int i = 0; i < numRings; ++i)
        if (! (thickBell && i == bellRing) && i != numRings - 1)
            ring (i);
    for (int j = 0; j < kSamples; j += kSpokeStep)
        for (int i = 0; i + 1 < numRings; ++i)
        {
            lineIndices.push_back (at (i, j));
            lineIndices.push_back (at (i + 1, j));
        }
    numThinIndices = (int) lineIndices.size();
    if (thickBell)
        ring (bellRing);
    ring (numRings - 1);

    for (size_t k = 0; k < lineIndices.size(); k += 2)
    {
        double factor = 1.0;
        const auto metal = metalAt (0.5 * (vertexR[lineIndices[k]] + vertexR[lineIndices[k + 1]]), factor);
        for (int e = 0; e < 2; ++e)
        {
            segmentMetal.push_back ((float) metal);
            segmentMetal.push_back ((float) factor);
        }
    }
}

// The hammering pressed into the mesh (the reference's Cymbal2): a fine grid, the dents
// (_make_dents, 'dimple'), the dent light factor at rest, and the rings and spokes simplified
// once (_simplify_lines). Each segment is coloured from its two ends.
void Geometry::buildDentedMesh()
{
    const auto& D = shape.dents;
    const int nr = D.radial, nt = D.around;
    around = nt;
    perVertexColour = true;
    fineR = linspace (0.02, 1.0, nr);
    const double dTheta = twoPi / nt;
    std::vector<double> cosT ((size_t) nt), sinT ((size_t) nt);
    for (int j = 0; j < nt; ++j)
    {
        cosT[(size_t) j] = std::cos (j * dTheta);
        sinT[(size_t) j] = std::sin (j * dTheta);
    }

    // The dents: (x, y, a along, b across, rotation, depth), in units of R.
    struct Dent
    {
        double x, y, a, b, rot, depth;
    };
    std::vector<Dent> dents;
    Uniform uniform;
    const double z0 = shape.hammerZone.from, z1 = shape.hammerZone.to;
    const int large = (int) (pi * (z1 * z1 - z0 * z0) / 0.011 / std::pow (D.size, 1.5));
    for (int k = 0; k < large; ++k)
    {
        const double r = std::sqrt (uniform (z0 * z0, z1 * z1)), t = uniform (0.0, twoPi);
        const double grow = (r - z0) / (z1 - z0);
        const double a = uniform (0.012, 0.026) * (0.8 + 1.6 * grow) * D.size, b = a * uniform (0.45, 0.9);
        const double rot = uniform (0.0, pi);
        dents.push_back ({ r * std::cos (t), r * std::sin (t), a, b, rot, 0.0042 * std::pow (a / 0.03, 0.7) * uniform (0.6, 1.2) });
    }
    // smaller dents in the gaps between the large ones (no overlap)
    for (int tries = 0, added = 0; added < D.smallDents && tries < 20000; ++tries)
    {
        const double r = std::sqrt (uniform (z0 * z0, z1 * z1)), t = uniform (0.0, twoPi);
        const double grow = (r - z0) / (z1 - z0);
        const double a = uniform (0.018, 0.028) * (0.85 + 0.5 * grow), b = a * uniform (0.5, 0.9);
        const double x = r * std::cos (t), y = r * std::sin (t);
        if (std::any_of (dents.begin(), dents.end(),
                         [&] (const Dent& d) { return std::hypot (x - d.x, y - d.y) < 0.9 * (a + d.a); }))
            continue;
        const double rot = uniform (0.0, pi);
        dents.push_back ({ x, y, a, b, rot, 0.0042 * std::pow (a / 0.03, 0.7) * uniform (0.6, 1.1) });
        ++added;
    }
    numDents = (int) dents.size();

    // Their depth on the grid: dz = -depth x dent_depth x exp (-1.6 q), q = (u/a)^2 + (v/b)^2 in
    // the dent's rotated frame, within 3 a of its centre in x and y (no lip).
    std::vector<double> dz ((size_t) (nr * nt), 0.0);
    const double dr = fineR[1] - fineR[0];
    for (const auto& d : dents)
    {
        const double reach = 3.0 * d.a * std::sqrt (2.0), rc = std::hypot (d.x, d.y), tc = std::atan2 (d.y, d.x);
        const int i0 = std::max (0, (int) std::floor ((rc - reach - fineR[0]) / dr) - 1);
        const int i1 = std::min (nr - 1, (int) std::ceil ((rc + reach - fineR[0]) / dr) + 1);
        const double cr = std::cos (d.rot), sr = std::sin (d.rot);
        for (int i = i0; i <= i1; ++i)
        {
            const double r = fineR[(size_t) i];
            int j0 = 0, j1 = nt - 1;
            if (reach < r)
            {
                const double half = std::asin (reach / r);
                j0 = (int) std::floor ((tc - half) / dTheta) - 1;
                j1 = (int) std::ceil ((tc + half) / dTheta) + 1;
                if (j1 - j0 >= nt)
                {
                    j0 = 0;
                    j1 = nt - 1;
                }
            }
            for (int jj = j0; jj <= j1; ++jj)
            {
                const int j = ((jj % nt) + nt) % nt;
                const double dx = r * cosT[(size_t) j] - d.x, dy = r * sinT[(size_t) j] - d.y;
                if (std::abs (dx) >= 3.0 * d.a || std::abs (dy) >= 3.0 * d.a)
                    continue;
                const double u = dx * cr + dy * sr, v = -dx * sr + dy * cr;
                const double q = (u / d.a) * (u / d.a) + (v / d.b) * (v / d.b);
                dz[(size_t) (i * nt + j)] -= d.depth * D.depth * std::exp (-1.6 * q);
            }
        }
    }

    // The rest heights, and the dent light factor f = clip (1 + contrast x (shade with the dents -
    // shade of the smooth surface), 0.25, 1.9), from the slopes on the grid.
    std::vector<double> z ((size_t) (nr * nt)), smooth ((size_t) nr), f ((size_t) (nr * nt));
    for (int i = 0; i < nr; ++i)
    {
        smooth[(size_t) i] = height (fineR[(size_t) i]);
        for (int j = 0; j < nt; ++j)
            z[(size_t) (i * nt + j)] = smooth[(size_t) i] + dz[(size_t) (i * nt + j)];
    }
    for (int i = 0; i < nr; ++i)
    {
        const double r = fineR[(size_t) i], profileSlope = gradientAt (smooth.data(), fineR.data(), nr, i);
        for (int j = 0; j < nt; ++j)
        {
            const double dzr = gradientAt (z.data() + j, fineR.data(), nr, i, nt);
            const auto Z = [&] (int jj) { return z[(size_t) (i * nt + (jj + nt) % nt)]; };
            const double dzt = (Z (j + 1) - Z (j - 1)) * 0.5 / dTheta / std::max (r, 1.0e-3);
            const double theta = j * dTheta;
            const double dshade = shadeOf (dzr, dzt, theta) - shadeOf (profileSlope, 0.0, theta);
            f[(size_t) (i * nt + j)] = std::clamp (1.0 + D.contrast * dshade, 0.25, 1.9);
        }
    }

    // The lines: rings on every ringStep-th radial sample, the bell ring and the rim; spokes on
    // every spokeEvery-th sample around, through every radial sample. Simplified on the rest
    // position in design units, plus 4 x f.
    std::vector<int> ringRows;
    for (int i = 0; i < nr; i += D.ringStep)
        ringRows.push_back (i);
    int bellRow = 0;
    for (int i = 1; i < nr; ++i)
        if (std::abs (fineR[(size_t) i] - bell) < std::abs (fineR[(size_t) bellRow] - bell))
            bellRow = i;
    ringRows.push_back (nr - 1);
    ringRows.push_back (bellRow);
    std::sort (ringRows.begin(), ringRows.end());
    ringRows.erase (std::unique (ringRows.begin(), ringRows.end()), ringRows.end());
    std::vector<int> spokeColumns;
    for (int j = 0; j < nt; j += D.spokeEvery)
        spokeColumns.push_back (j);
    segmentsBeforeSimplifying = (int) ringRows.size() * nt + (int) spokeColumns.size() * (nr - 1);

    const double U = Camera::scale;
    const auto point = [&] (int i, int j) {
        const double r = fineR[(size_t) i];
        return std::array<double, 4> { r * cosT[(size_t) j] * U, r * sinT[(size_t) j] * U, z[(size_t) (i * nt + j)] * U,
                                       4.0 * f[(size_t) (i * nt + j)] };
    };
    std::vector<int> id ((size_t) (nr * nt), -1);
    const auto vertex = [&] (int i, int j) {
        auto& v = id[(size_t) (i * nt + j)];
        if (v < 0)
        {
            v = (int) vertexR.size();
            vertexR.push_back (fineR[(size_t) i]);
            vertexTheta.push_back (j * dTheta);
            vertexZ.push_back (z[(size_t) (i * nt + j)]);
            vertexDent.push_back ((float) f[(size_t) (i * nt + j)]);
            vertexRadial.push_back ((std::uint16_t) i);
            vertexAngle.push_back ((std::uint16_t) j);
        }
        return (std::uint32_t) v;
    };
    std::vector<std::uint32_t> thick;
    for (int i : ringRows)
    {
        std::vector<std::array<double, 4>> P;
        for (int j = 0; j <= nt; ++j)
            P.push_back (point (i, j % nt)); // closed: the last joins the first
        auto kept = simplify (P, 6, D.simplify);
        kept.pop_back(); // the first again
        auto& into = (i == nr - 1 || (i == bellRow && shape.bellLine)) ? thick : lineIndices;
        for (size_t q = 0; q < kept.size(); ++q)
        {
            into.push_back (vertex (i, kept[q]));
            into.push_back (vertex (i, kept[(q + 1) % kept.size()]));
        }
    }
    for (int j : spokeColumns)
    {
        std::vector<std::array<double, 4>> P;
        for (int i = 0; i < nr; ++i)
            P.push_back (point (i, j));
        const auto kept = simplify (P, 5, D.simplify);
        for (size_t q = 0; q + 1 < kept.size(); ++q)
        {
            lineIndices.push_back (vertex (kept[q], j));
            lineIndices.push_back (vertex (kept[q + 1], j));
        }
    }
    numThinIndices = (int) lineIndices.size();
    lineIndices.insert (lineIndices.end(), thick.begin(), thick.end());

    for (auto x : lineIndices)
    {
        double factor = 1.0;
        const auto metal = metalAt (vertexR[x], factor);
        segmentMetal.push_back ((float) metal);
        segmentMetal.push_back ((float) factor);
    }
    for (int i = 0; i < nr; ++i)
    {
        const double raw = (fineR[(size_t) i] - (bell - 0.1)) / 0.15;
        fineStiff.push_back (std::clamp (raw, 0.12, 1.0));
        fineStiffSlope.push_back (raw > 0.12 && raw < 1.0 ? 1.0 / 0.15 : 0.0);
        fineSlope.push_back (slope (fineR[(size_t) i]));
    }
}

// The surface lines of _build_texture (Cymbal2._lines for the dented mesh), in its order: the
// hammering, the bell grooves, the edge lines, the fine lathe rings and the gold lathing (on the
// dented mesh the grooves, the gold lathing, then the edge lines). Any fixed-seed sequence will
// do: the pattern matches the reference in size, spacing and spread, not mark for mark.
void Geometry::buildSurface()
{
    Uniform uniform;
    const auto vertexAt = [this] (double r, double t, SurfaceKind kind, double param, double metalR) {
        double factor = 1.0;
        const auto metal = metalAt (metalR, factor);
        const float width = kind == SurfaceKind::groove || kind == SurfaceKind::edge ? 2.0f : 1.0f;
        return SurfaceVertex { (float) r, (float) t, (float) (height (r) + 0.0015), (float) kind, (float) param, (float) metal, (float) factor,
                               width };
    };
    // A polyline, its metal that at its mean radius.
    const auto polyline = [&] (const double* r, const double* t, int count, SurfaceKind kind, double param) {
        double mean = 0.0;
        for (int q = 0; q < count; ++q)
            mean += r[q];
        mean /= count;
        for (int q = 0; q + 1 < count; ++q)
            for (int e : { q, q + 1 })
                surface.push_back (vertexAt (r[e], t[e], kind, param, mean));
    };
    // A full ring at r, 48 segments (the reference's 97 points, every second one).
    const auto fullRing = [&] (double r, SurfaceKind kind, double param) {
        for (int q = 0; q < 48; ++q)
            for (int e : { q, q + 1 })
                surface.push_back (vertexAt (r, twoPi * e / 48.0, kind, param, r));
    };
    // The edge lines: the gold zones' bounds between `low` and 0.999 (with zoneEdges), and the
    // goldLines; GOLD x (0.55 + 0.40 ytl) x the zone's strength, 2 units.
    const auto edges = [&] (double low, bool zoneBounds) {
        std::vector<double> at;
        for (int k = 0; k < shape.numGold && zoneBounds; ++k)
            for (double r : { shape.gold[(size_t) k].from, shape.gold[(size_t) k].to })
                if (low < r && r < 0.999)
                    at.push_back (r);
        for (int k = 0; k < shape.numGoldLines; ++k)
            at.push_back (shape.goldLines[(size_t) k]);
        std::sort (at.begin(), at.end());
        at.erase (std::unique (at.begin(), at.end()), at.end());
        for (double r : at)
            fullRing (r, SurfaceKind::edge, strengthAt (r));
    };
    // RAVEN's deep lathe grooves on the bell: irregular spacing, bold, in a paler gold.
    const auto grooves = [&] {
        if (shape.hasBellGrooves)
            for (double r = shape.bellGrooves.from; r < shape.bellGrooves.to; r += uniform (0.011, 0.026))
                fullRing (r, SurfaceKind::groove, uniform (0.55, 1.0));
    };

    if (shape.dented)
    {
        // The dents replace the drawn hammering. Gold lathing (GOLD x (0.50 + 0.35 ytl) x lum x
        // strength), then its edges, whose bounds count from 0.03 (as Cymbal2._lines).
        grooves();
        for (int k = 0; k < shape.numGold; ++k)
            for (double r : arange (shape.gold[(size_t) k].from, shape.gold[(size_t) k].to, shape.goldStep))
                fullRing (r, SurfaceKind::goldLine, lum (r) * strengthAt (r));
        edges (0.03, true);
        return;
    }

    const double z0 = shape.hammerZone.from, z1 = shape.hammerZone.to;
    std::array<double, 13> rr {}, tt {};
    switch (shape.hammer)
    {
        case Hammer::scale:
            for (double r = z0; r < z1 - 0.01; r += 0.03)
            {
                const double step = 0.05 / r;
                for (double t0 = uniform (0.0, step); t0 < twoPi; t0 += step * uniform (0.92, 1.08))
                {
                    const double jitter = uniform (-0.004, 0.004);
                    for (size_t q = 0; q < kArcT.size(); ++q)
                    {
                        tt[q] = t0 + step * 0.82 * kArcT[q];
                        rr[q] = r + jitter + 0.014 * std::sin (pi * kArcT[q]);
                    }
                    polyline (rr.data(), tt.data(), (int) kArcT.size(), SurfaceKind::hammer, 0.8);
                }
            }
            break;
        case Hammer::dimple:
        {
            // irregular dents, placed at random, larger towards the edge
            const int n = (int) (pi * (z1 * z1 - z0 * z0) / 0.011);
            for (int k = 0; k < n; ++k)
            {
                const double r = std::sqrt (uniform (z0 * z0, z1 * z1)), t = uniform (0.0, twoPi);
                const double grow = (r - z0) / (z1 - z0);
                const double a = uniform (0.012, 0.026) * (0.8 + 1.6 * grow), b = a * uniform (0.35, 0.9), rot = uniform (0.0, pi);
                const double p1 = uniform (0.0, 6.3), s1 = uniform (0.3, 1.0), p2 = uniform (0.0, 6.3);
                for (int q = 0; q < 13; ++q)
                {
                    const double u = twoPi * (q == 12 ? 0 : q) / 12.0;
                    const double wob = 1.0 + 0.25 * std::sin (2.0 * u + p1) * s1 + 0.15 * std::sin (3.0 * u + p2);
                    const double dx = wob * (a * std::cos (u) * std::cos (rot) - b * std::sin (u) * std::sin (rot));
                    const double dy = wob * (a * std::cos (u) * std::sin (rot) + b * std::sin (u) * std::cos (rot));
                    const double x = r * std::cos (t) + dx, y = r * std::sin (t) + dy;
                    rr[(size_t) q] = std::hypot (x, y);
                    tt[(size_t) q] = std::atan2 (y, x);
                }
                polyline (rr.data(), tt.data(), 13, SurfaceKind::hammer, 0.8);
            }
            break;
        }
        case Hammer::spiral:
        {
            // large scale-shaped marks along an Archimedean spiral
            const double pitch = 0.065, width = 0.105;
            double th = 0.0;
            for (double r = z0; r < z1; r = z0 + pitch * th / twoPi)
            {
                const double step = width / r;
                const double jitter = uniform (-0.005, 0.005);
                for (size_t q = 0; q < kArcT.size(); ++q)
                {
                    tt[q] = th + step * 0.85 * kArcT[q];
                    rr[q] = z0 + pitch * tt[q] / twoPi + 0.032 * std::sin (pi * kArcT[q]) + jitter;
                }
                polyline (rr.data(), tt.data(), (int) kArcT.size(), SurfaceKind::hammer, 1.0);
                th += step * uniform (0.95, 1.05);
            }
            break;
        }
        case Hammer::radial:
        {
            const int n = (int) (pi * (z1 * z1 - z0 * z0) / 0.0025);
            const double from = std::max (z0, 0.05);
            for (int k = 0; k < n; ++k)
            {
                const double r = std::sqrt (uniform (from * from, z1 * z1)), t = uniform (0.0, twoPi);
                const double L = uniform (0.015, 0.035);
                rr[0] = r - L / 2.0;
                rr[1] = r + L / 2.0;
                tt[0] = t;
                tt[1] = t + uniform (-0.01, 0.01);
                polyline (rr.data(), tt.data(), 2, SurfaceKind::hammer, 0.8);
            }
            break;
        }
        case Hammer::none: break;
    }
    grooves();
    edges (0.02, shape.zoneEdges);
    for (int z = 0; z < shape.numFineLathe; ++z)
        for (double r : arange (shape.fineLathe[(size_t) z].from, shape.fineLathe[(size_t) z].to, 0.012))
            fullRing (r, SurfaceKind::lathe, 0.0);
    // the gold lathing: the surface colour x (0.50 + 0.35 ytl) x lum(r)
    for (int k = 0; k < shape.numGold; ++k)
        for (double r : arange (shape.gold[(size_t) k].from, shape.gold[(size_t) k].to, shape.goldStep))
            fullRing (r, SurfaceKind::goldLathe, lum (r));
}

void Geometry::buildModes()
{
    const size_t nf = fineR.size();
    radial.assign ((size_t) (kModes * numRings), 0.0f);
    fineRadial.assign (kModes * nf, 0.0f);
    fineRadialSlope.assign (kModes * nf, 0.0f);
    for (int k = 0; k < kModes; ++k)
    {
        const auto& md = modes()[(size_t) k];
        double peak = 1.0e-6;
        for (int s = 0; s <= 200; ++s)
            peak = std::max (peak, std::abs (besselJ (md.m, md.z * s / 200.0)));
        norm[(size_t) k] = peak;
        for (int i = 0; i < numRings; ++i)
            radial[(size_t) (k * numRings + i)] = (float) (besselJ (md.m, md.z * rings[(size_t) i]) / peak);
        // and on the fine grid, with d/dr: z J_m'(z r), J_m' = (J_(m-1) - J_(m+1)) / 2, J_-1 = -J_1
        for (size_t i = 0; i < nf; ++i)
        {
            const double x = md.z * fineR[i];
            const double below = md.m == 0 ? -besselJ (1, x) : besselJ (md.m - 1, x);
            fineRadial[(size_t) k * nf + i] = (float) (besselJ (md.m, x) / peak);
            fineRadialSlope[(size_t) k * nf + i] = (float) (md.z * 0.5 * (below - besselJ (md.m + 1, x)) / peak);
        }
    }
    cosM.assign ((size_t) (kM * around), 0.0f);
    sinM.assign ((size_t) (kM * around), 0.0f);
    for (int m = 0; m < kM; ++m)
        for (int j = 0; j < around; ++j)
        {
            const double t = twoPi * j / around;
            cosM[(size_t) (m * around + j)] = (float) std::cos (m * t);
            sinM[(size_t) (m * around + j)] = (float) std::sin (m * t);
        }
}

//==============================================================================
double radiusFraction (double position, double bell) noexcept
{
    const double p = std::clamp (position, 0.0, 1.0);
    const double atBell = 0.6 * bell, atBow = 0.5 * (bell + 0.95), atEdge = 0.97;
    return p >= 0.5 ? atBow + (atBell - atBow) * (p - 0.5) * 2.0 : atEdge + (atBow - atEdge) * p * 2.0;
}
Hit makeHit (const Geometry& g, double startMs, double position, double velocity, bool shoulder, double angle)
{
    Hit h;
    h.startMs = startMs;
    h.velocity = (float) std::clamp (velocity, 0.0, 1.0);
    h.shoulder = shoulder;
    h.angle = (float) angle;
    const double r = radiusFraction (position, g.bell);
    h.radius = (float) r;
    h.zone = r < g.bell ? Zone::bell : r >= 0.9 ? Zone::edge : Zone::bow;
    const double v = h.velocity;
    double top = 1.0e-12;
    std::array<double, kModes> w {};
    for (int k = 0; k < kModes; ++k)
    {
        const auto& md = modes()[(size_t) k];
        const double amp = std::abs (besselJ (md.m, md.z * r)) / g.norm[(size_t) k];
        const double pref = h.zone == Zone::bell ? 1.0 / (1 + md.m)
                            : h.zone == Zone::bow ? 1.0 / (1 + std::abs (md.m - 2))
                                                  : (1 + md.m) / 7.0;
        w[(size_t) k] = amp * pref * std::exp (-(md.m + 1.5 * md.n) / (2.0 + 6.0 * v));
        top = std::max (top, w[(size_t) k]);
    }
    for (int k = 0; k < kModes; ++k)
        h.weight[(size_t) k] = (float) (w[(size_t) k] / top);
    return h;
}

void Frame::resize (const Geometry& g)
{
    const size_t n = (size_t) g.numVertices();
    for (auto* v : { &height, &dzr, &dzt, &glow })
        v->assign (n, 0.0f);
    coef.assign (4 * (size_t) kM * g.fineR.size(), 0.0f);
}

void evaluate (const Geometry& g, const Hit* hits, int numHits, double nowMs, Frame& out, std::vector<float>& disp) noexcept
{
    const int nv = g.numVertices();
    const bool dented = g.shape.dented;
    const int nr = g.numRings;
    const size_t nf = g.fineR.size();
    // The dented mesh's flex per fine radius i and m: disp = sum over m of A cos m theta +
    // B sin m theta, and the radial slopes of A and B (coef[((q * 7 + m) * nf + i], q = A, B,
    // A', B').
    const auto C = [&] (int q, int m) { return out.coef.data() + ((size_t) q * kM + (size_t) m) * nf; };
    if (dented)
        std::fill (out.coef.begin(), out.coef.end(), 0.0f);
    else
        std::fill (disp.begin(), disp.end(), 0.0f);
    double tx = 0.0, ty = 0.0, spin = 0.0;
    out.numFlashes = 0;
    out.moving = false;
    const Camera cam;
    for (int h = 0; h < numHits; ++h)
    {
        const auto& hit = hits[h];
        const double age = (nowMs - hit.startMs) * 0.001;
        if (age < 0.0 || age * 1000.0 >= kHitLifeMs)
            continue;
        out.moving = true;
        const double v = hit.velocity, ang = hit.angle;
        // The plate modes (modal): each decays with tau and flexes at its visual rate.
        for (int k = 0; k < kModes; ++k)
        {
            const double w = hit.weight[(size_t) k];
            if (w < 0.05)
                continue;
            const auto& md = modes()[(size_t) k];
            const double tau = 0.25 + 1.2 / (1 + md.m + 2 * md.n) * (hit.zone == Zone::edge ? 1.6 : 1.0);
            double a = v * w * std::exp (-age / tau) * (1.0 - std::exp (-age / 0.03));
            if (a < 1.0e-3)
                continue;
            a *= std::cos (twoPi * (2.0 + 0.8 * (md.m + md.n)) * age);
            const float ca = (float) std::cos (md.m * ang), sa = (float) std::sin (md.m * ang);
            if (dented)
            {
                const float* R = g.fineRadial.data() + (size_t) k * nf;
                const float* Rs = g.fineRadialSlope.data() + (size_t) k * nf;
                float *A = C (0, md.m), *B = C (1, md.m), *As = C (2, md.m), *Bs = C (3, md.m);
                const float ac = (float) a * ca, as = (float) a * sa;
                for (size_t i = 0; i < nf; ++i)
                {
                    A[i] += ac * R[i];
                    B[i] += as * R[i];
                    As[i] += ac * Rs[i];
                    Bs[i] += as * Rs[i];
                }
                continue;
            }
            const float* cm = g.cosM.data() + md.m * kSamples;
            const float* sm = g.sinM.data() + md.m * kSamples;
            for (int i = 0; i < nr; ++i)
            {
                const float amp = (float) a * g.radial[(size_t) (k * nr + i)];
                float* d = disp.data() + i * kSamples;
                for (int j = 0; j < kSamples; ++j)
                    d[j] += amp * (cm[j] * ca + sm[j] * sa);
            }
        }
        // The wobble: a slow tilt around the strike's direction, and a little spin.
        const auto& mot = kMotion[(size_t) hit.zone];
        const double ramp = (1.0 - std::exp (-age / 0.02)) * std::exp (-age / mot.tau);
        const double c = std::cos (twoPi * mot.frequency * age), s = std::sin (twoPi * mot.frequency * age);
        const double ux = std::cos (ang), uy = std::sin (ang);
        tx += mot.amplitude * v * ramp * (c * ux - 0.85 * s * uy);
        ty += mot.amplitude * v * ramp * (c * uy + 0.85 * s * ux);
        spin += mot.spin * v * ramp * c;
        // The flash where it struck, at the cymbal's rest position.
        const double T = flashSeconds (v);
        if (age < T)
        {
            double sx = 0.0, sy = 0.0;
            cam.project (hit.radius * std::cos (ang), hit.radius * std::sin (ang), g.height (hit.radius), sx, sy);
            const double k = (1.0 - age / T) * (1.0 - age / T);
            auto& f = out.flashes[(size_t) out.numFlashes++];
            f.x = (float) sx;
            f.y = (float) sy;
            f.radius = (float) ((10.0 + 22.0 * v) * (1.0 - 0.3 * age / T) * (hit.shoulder ? 1.3 : 1.0));
            f.intensity = (float) ((0.4 + 0.6 * v) * k);
        }
    }
    out.tiltX = tx;
    out.tiltY = ty;
    out.spinDeg = spin;

    float peak = 0.0f;
    if (dented)
    {
        // Heights (the rest height, dents included, and the flex scaled by the stiffness), the
        // smooth surface's slopes (profile and flex), and the glow, at each vertex.
        const int around = g.around;
        for (int x = 0; x < nv; ++x)
        {
            const size_t i = g.vertexRadial[(size_t) x];
            const int j = g.vertexAngle[(size_t) x];
            float d = 0.0f, dr = 0.0f, dt = 0.0f;
            for (int m = 0; m < kM; ++m)
            {
                const float cm = g.cosM[(size_t) (m * around + j)], sm = g.sinM[(size_t) (m * around + j)];
                const float a = C (0, m)[i], b = C (1, m)[i];
                d += a * cm + b * sm;
                dr += C (2, m)[i] * cm + C (3, m)[i] * sm;
                dt += (float) m * (b * cm - a * sm);
            }
            const auto stiff = (float) g.fineStiff[i], stiffSlope = (float) g.fineStiffSlope[i];
            const float flex = d * stiff;
            out.height[(size_t) x] = (float) g.vertexZ[(size_t) x] + 0.010f * flex;
            out.dzr[(size_t) x] = (float) g.fineSlope[i] + 0.010f * (dr * stiff + d * stiffSlope);
            out.dzt[(size_t) x] = 0.010f * stiff * dt / (float) std::max (g.fineR[i], 1.0e-3);
            out.glow[(size_t) x] = std::abs (flex);
            peak = std::max (peak, std::abs (flex));
        }
    }
    else
    {
        // Heights (the flex scaled by the ring's stiffness) and the glow.
        for (int i = 0; i < nr; ++i)
        {
            const float stiff = (float) g.ringStiff[(size_t) i], base = (float) g.ringHeight[(size_t) i];
            for (int j = 0; j < kSamples; ++j)
            {
                const int x = i * kSamples + j;
                const float d = disp[(size_t) x] * stiff;
                out.height[(size_t) x] = base + 0.010f * d;
                out.glow[(size_t) x] = std::abs (d);
                peak = std::max (peak, std::abs (d));
            }
        }
        // Slopes of the height: along the radius (numpy.gradient over the rings: second order
        // inside, first order at the ends) and around (central, periodic).
        const auto& r = g.rings;
        for (int i = 0; i < nr; ++i)
        {
            for (int j = 0; j < kSamples; ++j)
            {
                const auto H = [&] (int ii, int jj) { return (double) out.height[(size_t) (ii * kSamples + jj)]; };
                double d;
                if (i == 0)
                    d = (H (1, j) - H (0, j)) / (r[1] - r[0]);
                else if (i == nr - 1)
                    d = (H (i, j) - H (i - 1, j)) / (r[(size_t) i] - r[(size_t) i - 1]);
                else
                {
                    const double hs = r[(size_t) i] - r[(size_t) i - 1], hd = r[(size_t) i + 1] - r[(size_t) i];
                    d = (-hd / (hs * (hd + hs))) * H (i - 1, j) + ((hd - hs) / (hs * hd)) * H (i, j) + (hs / (hd * (hd + hs))) * H (i + 1, j);
                }
                const double dt = (H (i, (j + 1) % kSamples) - H (i, (j + kSamples - 1) % kSamples)) * 0.5 / (twoPi / kSamples)
                                  / std::max (r[(size_t) i], 1.0e-3);
                out.dzr[(size_t) (i * kSamples + j)] = (float) d;
                out.dzt[(size_t) (i * kSamples + j)] = (float) dt;
            }
        }
    }
    const float level = peak > 1.0e-4f ? std::sqrt (std::min (1.0f, peak / 0.35f)) : 0.0f;
    for (int x = 0; x < nv; ++x)
        out.glow[(size_t) x] = level > 0.0f ? std::pow (out.glow[(size_t) x] / std::max (peak, 1.0e-6f), 1.8f) * level : 0.0f;

    // The cymbal's top and bottom on screen.
    double top = 1.0e9, bottom = -1.0e9;
    for (int x = 0; x < nv; ++x)
    {
        double sx = 0.0, sy = 0.0;
        projectVertex (g, out, cam, x, sx, sy);
        top = std::min (top, sy);
        bottom = std::max (bottom, sy);
    }
    out.yTop = top;
    out.yBottom = bottom;
}

double shadeAt (const Geometry& g, const Frame& f, int x) noexcept
{
    const double theta = g.vertexTheta[(size_t) x];
    const double phi = theta + f.spinDeg * pi / 180.0;
    // The tilt's plane, Z -= 2.2 (X T0 + Y T1), adds a constant slope.
    const double dzr = f.dzr[(size_t) x] - 2.2 * (std::cos (phi) * f.tiltX + std::sin (phi) * f.tiltY);
    const double dzt = f.dzt[(size_t) x] - 2.2 * (-std::sin (phi) * f.tiltX + std::cos (phi) * f.tiltY);
    return shadeOf (dzr, dzt, theta);
}

void projectVertex (const Geometry& g, const Frame& f, const Camera& cam, int x, double& sx, double& sy) noexcept
{
    const double r = g.vertexR[(size_t) x];
    const double phi = g.vertexTheta[(size_t) x] + f.spinDeg * pi / 180.0;
    const double X = r * std::cos (phi), Y = r * std::sin (phi);
    cam.project (X, Y, f.height[(size_t) x] - 2.2 * (X * f.tiltX + Y * f.tiltY), sx, sy);
}

Rgb segmentColour (const Geometry& g, const Frame& f, int k, double shadeA, double ytlA, double shadeB, double ytlB) noexcept
{
    const auto a = g.lineIndices[(size_t) (2 * k)], b = g.lineIndices[(size_t) (2 * k + 1)];
    const float* m = g.segmentMetal.data() + 4 * k;
    if (g.perVertexColour)
    {
        const auto ca = bronze (shadeA, ytlA, (Metal) (int) m[0], m[1], g.vertexDent[a], f.glow[a]);
        const auto cb = bronze (shadeB, ytlB, (Metal) (int) m[2], m[3], g.vertexDent[b], f.glow[b]);
        return { 0.5f * (ca.r + cb.r), 0.5f * (ca.g + cb.g), 0.5f * (ca.b + cb.b) };
    }
    return bronze (0.5 * (shadeA + shadeB), 0.5 * (ytlA + ytlB), (Metal) (int) m[0], m[1], 0.5 * (g.vertexDent[a] + g.vertexDent[b]),
                   0.5 * (f.glow[a] + f.glow[b]));
}

void projectSurface (const SurfaceVertex& v, const Frame& f, const Camera& cam, double& sx, double& sy) noexcept
{
    const double phi = v.theta + f.spinDeg * pi / 180.0;
    const double X = v.r * std::cos (phi), Y = v.r * std::sin (phi);
    cam.project (X, Y, v.z - 2.2 * (X * f.tiltX + Y * f.tiltY), sx, sy);
}

Rgb surfaceColour (const SurfaceVertex& v, double ytl) noexcept
{
    const auto scaled = [] (Rgb c, double k) { return Rgb { (float) (c.r * k), (float) (c.g * k), (float) (c.b * k) }; };
    const auto surfaceBronze = [&] { return bronze (0.9, ytl, (Metal) (int) v.metal, v.factor); };
    switch ((SurfaceKind) (int) v.kind)
    {
        case SurfaceKind::hammer: return scaled (surfaceBronze(), v.param);
        case SurfaceKind::lathe: return scaled (surfaceBronze(), 0.30 + 0.25 * ytl);
        case SurfaceKind::groove: return scaled (colour::gold, (0.30 + 0.32 * ytl) * v.param);
        case SurfaceKind::edge: return scaled (colour::gold, (0.55 + 0.40 * ytl) * v.param);
        case SurfaceKind::goldLathe: return scaled (surfaceBronze(), (0.50 + 0.35 * ytl) * v.param);
        case SurfaceKind::goldLine: return scaled (colour::gold, (0.50 + 0.35 * ytl) * v.param);
    }
    return {};
}

} // namespace cymbalis::ui::wire
