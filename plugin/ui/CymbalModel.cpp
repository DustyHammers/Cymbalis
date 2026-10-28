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

// A uniform number in [a, b) from a fixed-seed sequence (the surface lines' layout).
struct Uniform
{
    std::mt19937_64 gen { 11 };
    double operator() (double a, double b) { return a + (b - a) * (double) (gen() >> 11) * 0x1.0p-53; }
};
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

Rgb bronze (double shade, double ytl, bool lathe, double glow) noexcept
{
    const double light = std::clamp (((shade - 0.86) / 0.14) * 0.8 + 0.2 * ytl, 0.0, 1.0);
    const double f = std::pow (0.25 + 0.75 * light, 1.4);
    const auto mix = [&] (float dark, float bright, float lathed, float hot) {
        double c = dark + (bright - dark) * f;
        if (lathe)
            c = c * 0.55 + lathed * (0.30 + 0.35 * light);
        return (float) (c + (hot - c) * glow);
    };
    using namespace colour;
    return { mix (roseDark.r, roseLight.r, colour::lathe.r, hot.r), mix (roseDark.g, roseLight.g, colour::lathe.g, hot.g),
             mix (roseDark.b, roseLight.b, colour::lathe.b, hot.b) };
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

bool Geometry::inLathe (double r) const noexcept
{
    for (int k = 0; k < shape.numLathe; ++k)
        if (shape.lathe[(size_t) k].from <= r && r <= shape.lathe[(size_t) k].to)
            return true;
    return false;
}

// Rings: linspace(0.03, bell, max(4, int(bell / 0.04))), then linspace(bell + 0.035, 0.92,
// max(6, int((0.92 - bell) / 0.035))), then 0.94, 0.96, 0.98 and 1.0. Spokes on every second
// sample, from the first ring to the rim.
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
    for (double r : rings)
    {
        ringHeight.push_back (height (r));
        ringStiff.push_back (std::clamp ((r - (bell - 0.1)) / 0.15, 0.12, 1.0));
    }

    const auto at = [] (int i, int j) { return (std::uint32_t) (i * kSamples + j); };
    const auto ring = [&] (int i) {
        for (int j = 0; j < kSamples; ++j)
        {
            lineIndices.push_back (at (i, j));
            lineIndices.push_back (at (i, (j + 1) % kSamples));
        }
    };
    for (int i = 0; i < numRings; ++i)
        if (i != bellRing && i != numRings - 1)
            ring (i);
    for (int j = 0; j < kSamples; j += kSpokeStep)
        for (int i = 0; i + 1 < numRings; ++i)
        {
            lineIndices.push_back (at (i, j));
            lineIndices.push_back (at (i + 1, j));
        }
    numThinIndices = (int) lineIndices.size();
    ring (bellRing);
    ring (numRings - 1);
}

// The surface lines of _build_texture: the hammering, the bell grooves (2 units wide, so last)
// and the fine lathe rings every 0.012 R. Any fixed-seed sequence will do: the pattern matches
// the reference in size, spacing and spread, not mark for mark.
void Geometry::buildSurface()
{
    Uniform uniform;
    std::vector<SurfaceVertex> grooves;
    const auto polyline = [this] (const double* r, const double* t, int count, SurfaceKind kind, double param) {
        double mean = 0.0;
        for (int q = 0; q < count; ++q)
            mean += r[q];
        const float lathe = inLathe (mean / count) ? 1.0f : 0.0f;
        for (int q = 0; q + 1 < count; ++q)
            for (int e : { q, q + 1 })
                surface.push_back ({ (float) r[e], (float) t[e], (float) (height (r[e]) + 0.0015), (float) kind, (float) param, lathe });
    };
    // A full ring at r, 48 segments (the reference's 97 points, every second one).
    const auto fullRing = [this] (std::vector<SurfaceVertex>& into, double r, SurfaceKind kind, double param) {
        const float z = (float) (height (r) + 0.0015), lathe = inLathe (r) ? 1.0f : 0.0f;
        for (int q = 0; q < 48; ++q)
            for (int e : { q, q + 1 })
                into.push_back ({ (float) r, (float) (twoPi * e / 48.0), z, (float) kind, (float) param, lathe });
    };

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
    if (shape.hasBellGrooves)
    {
        // deep lathe grooves on the bell: irregular spacing, bold, in a paler gold
        for (double r = shape.bellGrooves.from; r < shape.bellGrooves.to; r += uniform (0.011, 0.026))
            fullRing (grooves, r, SurfaceKind::groove, uniform (0.55, 1.0));
    }
    for (int z = 0; z < shape.numFineLathe; ++z)
    {
        const auto& zone = shape.fineLathe[(size_t) z];
        const int count = (int) std::ceil ((zone.to - zone.from) / 0.012); // numpy.arange (from, to, 0.012)
        for (int k = 0; k < count; ++k)
            fullRing (surface, zone.from + k * 0.012, SurfaceKind::lathe, 0.0);
    }
    numThinSurface = (int) surface.size();
    surface.insert (surface.end(), grooves.begin(), grooves.end());
}

void Geometry::buildModes()
{
    radial.assign ((size_t) (kModes * numRings), 0.0f);
    for (int k = 0; k < kModes; ++k)
    {
        const auto& md = modes()[(size_t) k];
        double peak = 1.0e-6;
        for (int s = 0; s <= 200; ++s)
            peak = std::max (peak, std::abs (besselJ (md.m, md.z * s / 200.0)));
        norm[(size_t) k] = peak;
        for (int i = 0; i < numRings; ++i)
            radial[(size_t) (k * numRings + i)] = (float) (besselJ (md.m, md.z * rings[(size_t) i]) / peak);
    }
    for (int m = 0; m < 7; ++m)
        for (int j = 0; j < kSamples; ++j)
        {
            const double t = twoPi * j / kSamples;
            cosM[(size_t) m][(size_t) j] = (float) std::cos (m * t);
            sinM[(size_t) m][(size_t) j] = (float) std::sin (m * t);
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
}

void evaluate (const Geometry& g, const Hit* hits, int numHits, double nowMs, Frame& out, std::vector<float>& disp) noexcept
{
    const int nr = g.numRings, nv = g.numVertices();
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
            const auto& cm = g.cosM[(size_t) md.m];
            const auto& sm = g.sinM[(size_t) md.m];
            for (int i = 0; i < nr; ++i)
            {
                const float amp = (float) a * g.radial[(size_t) (k * nr + i)];
                float* d = disp.data() + i * kSamples;
                for (int j = 0; j < kSamples; ++j)
                    d[j] += amp * (cm[(size_t) j] * ca + sm[(size_t) j] * sa);
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

    // Heights (the flex scaled by the ring's stiffness) and the glow.
    float peak = 0.0f;
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
    const float level = peak > 1.0e-4f ? std::sqrt (std::min (1.0f, peak / 0.35f)) : 0.0f;
    for (int x = 0; x < nv; ++x)
        out.glow[(size_t) x] = level > 0.0f ? std::pow (out.glow[(size_t) x] / std::max (peak, 1.0e-6f), 1.8f) * level : 0.0f;

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

double shadeAt (const Frame& f, int x) noexcept
{
    const double theta = twoPi * (x % kSamples) / kSamples;
    const double phi = theta + f.spinDeg * pi / 180.0;
    // The tilt's plane, Z -= 2.2 (X T0 + Y T1), adds a constant slope.
    const double dzr = f.dzr[(size_t) x] - 2.2 * (std::cos (phi) * f.tiltX + std::sin (phi) * f.tiltY);
    const double dzt = f.dzt[(size_t) x] - 2.2 * (-std::sin (phi) * f.tiltX + std::cos (phi) * f.tiltY);
    const double nx = -(dzr * std::cos (theta) - dzt * std::sin (theta));
    const double ny = -(dzr * std::sin (theta) + dzt * std::cos (theta));
    return std::clamp ((ny * -0.55 + 0.83) / std::sqrt (nx * nx + ny * ny + 1.0), 0.0, 1.0);
}

void projectVertex (const Geometry& g, const Frame& f, const Camera& cam, int x, double& sx, double& sy) noexcept
{
    const double r = g.rings[(size_t) (x / kSamples)];
    const double phi = twoPi * (x % kSamples) / kSamples + f.spinDeg * pi / 180.0;
    const double X = r * std::cos (phi), Y = r * std::sin (phi);
    cam.project (X, Y, f.height[(size_t) x] - 2.2 * (X * f.tiltX + Y * f.tiltY), sx, sy);
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
    switch ((SurfaceKind) (int) v.kind)
    {
        case SurfaceKind::hammer: return scaled (bronze (0.9, ytl, v.lathe > 0.5f), v.param);
        case SurfaceKind::lathe: return scaled (bronze (0.9, ytl, v.lathe > 0.5f), 0.30 + 0.25 * ytl);
        case SurfaceKind::groove: return scaled (colour::gold, (0.30 + 0.32 * ytl) * v.param);
    }
    return {};
}

} // namespace cymbalis::ui::wire
