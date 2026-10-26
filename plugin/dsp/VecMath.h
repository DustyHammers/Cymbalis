// VecMath.h — exp, sin and cos of double arrays, two at a time (NEON on arm64, SSE on x86_64).
//
// FastEngine's strikes evaluate these per mode: the components brought to the strike, the
// cymbal-wide wander and contact factors, each strike's own decay, the merged decay of two
// components and the in-frame decay of envelope strikes. libm evaluates one value per call;
// these evaluate two per vector with fdlibm's algorithms (FreeBSD msun e_exp.c, k_sin.c,
// k_cos.c and the medium case of e_rem_pio2.c), each within 1 ulp, so a result is libm's or
// one of its neighbours. Arguments outside the ranges below (and NaN or infinity) are left
// to libm itself, one value at a time.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

namespace cymbalis::vecmath
{

typedef double v2d __attribute__((vector_size(16)));
typedef int64_t v2l __attribute__((vector_size(16)));

constexpr double kExpMax = 708.0;      // |x| up to this: exp in the vector
constexpr double kSinCosMax = 1.0e6;   // |x| up to this: sin and cos in the vector (n < 2^20)

inline v2d load2(const double* p) noexcept
{
    v2d v;
    std::memcpy(&v, p, sizeof v);
    return v;
}

inline void store2(double* p, v2d v) noexcept { std::memcpy(p, &v, sizeof v); }

inline v2d splat2(double x) noexcept { return v2d {x, x}; }

// Round to the nearest integer (ties to even), for |x| < 2^51.
inline v2d roundNearest(v2d x) noexcept
{
    const v2d magic = splat2(6755399441055744.0); // 1.5 x 2^52
    return (x + magic) - magic;
}

inline v2d selectBits(v2l mask, v2d a, v2d b) noexcept { return (v2d) (((v2l) a & mask) | ((v2l) b & ~mask)); }

// exp(x) for |x| <= kExpMax (e_exp.c: x = k ln2 + r, |r| <= ln2 / 2).
inline v2d expCore(v2d x) noexcept
{
    const v2d ln2Hi = splat2(6.93147180369123816490e-01), ln2Lo = splat2(1.90821492927058770002e-10);
    const v2d invLn2 = splat2(1.44269504088896338700e+00);
    const v2d P1 = splat2(1.66666666666666019037e-01), P2 = splat2(-2.77777777770155933842e-03),
              P3 = splat2(6.61375632143793436117e-05), P4 = splat2(-1.65339022054652515390e-06),
              P5 = splat2(4.13813679705723846039e-08);
    const v2d one = splat2(1.0), two = splat2(2.0);
    const v2d k = roundNearest(x * invLn2);
    const v2d hi = x - k * ln2Hi; // exact: ln2Hi has 32 significant bits, |k| < 2^11
    const v2d lo = k * ln2Lo;
    const v2d r = hi - lo;
    const v2d t = r * r;
    const v2d c = r - t * (P1 + t * (P2 + t * (P3 + t * (P4 + t * P5))));
    const v2d y = one - ((lo - (r * c) / (two - c)) - hi);
    // y x 2^k: k + 1023 in the exponent field.
    const v2l e = (__builtin_convertvector(k, v2l) + 1023) << 52;
    return y * (v2d) e;
}

// sin and cos of x for |x| <= kSinCosMax: x = n pi/2 + (y0 + y1), |y0| <= pi/4, with pi/2 in
// three 33-bit parts so that n times each is exact (e_rem_pio2.c, all three rounds).
inline void sinCosCore(v2d x, v2d& s, v2d& c) noexcept
{
    const v2d invPio2 = splat2(6.36619772367581382433e-01);
    const v2d pio2_1 = splat2(1.57079632673412561417e+00), pio2_2 = splat2(6.07710050630396597660e-11),
              pio2_2t = splat2(2.02226624879595063154e-21), pio2_3 = splat2(2.02226624871116645580e-21),
              pio2_3t = splat2(8.47842766036889956997e-32);
    const v2d n = roundNearest(x * invPio2);
    v2d r = x - n * pio2_1;
    v2d t = r;
    v2d w = n * pio2_2;
    r = t - w;
    w = n * pio2_2t - ((t - r) - w);
    t = r;
    w = n * pio2_3;
    r = t - w;
    w = n * pio2_3t - ((t - r) - w);
    const v2d y0 = r - w;
    const v2d y1 = (r - y0) - w;

    // k_sin.c with the tail y1.
    const v2d S1 = splat2(-1.66666666666666324348e-01), S2 = splat2(8.33333333332248946124e-03),
              S3 = splat2(-1.98412698298579493134e-04), S4 = splat2(2.75573137070700676789e-06),
              S5 = splat2(-2.50507602534068634195e-08), S6 = splat2(1.58969099521155010221e-10);
    const v2d half = splat2(0.5), one = splat2(1.0);
    const v2d z = y0 * y0;
    const v2d v = z * y0;
    const v2d rs = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
    const v2d ks = y0 - ((z * (half * y1 - v * rs) - y1) - v * S1);

    // k_cos.c with the tail y1.
    const v2d C1 = splat2(4.16666666666666019037e-02), C2 = splat2(-1.38888888888741095749e-03),
              C3 = splat2(2.48015872894767294178e-05), C4 = splat2(-2.75573143513906633035e-07),
              C5 = splat2(2.08757232129817482790e-09), C6 = splat2(-1.13596475577881948265e-11);
    const v2d rc = z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));
    const v2d hz = half * z;
    const v2d wc = one - hz;
    const v2d kc = wc + (((one - wc) - hz) + (z * rc - y0 * y1));

    // Quadrant n mod 4: sin = ks, kc, -ks, -kc; cos = kc, -ks, -kc, ks.
    const v2l q = __builtin_convertvector(n, v2l);
    const v2l swap = (q & 1) != 0;
    const v2l signBit = (v2l) splat2(-0.0);
    const v2l sinSign = ((q & 2) != 0) & signBit;
    const v2l cosSign = (((q + 1) & 2) != 0) & signBit;
    s = (v2d) ((v2l) selectBits(swap, kc, ks) ^ sinSign);
    c = (v2d) ((v2l) selectBits(swap, ks, kc) ^ cosSign);
}

// True where a lane needs libm: |x| above `limit`, NaN or infinity.
inline bool anyOutside(v2d x, double limit) noexcept
{
    const v2d magnitude = (v2d) ((v2l) x & ~(v2l) splat2(-0.0));
    const v2l inside = magnitude <= splat2(limit);
    return !(inside[0] && inside[1]);
}

// out[i] = exp(x[i]) for i < n (out may be x).
inline void expArray(const double* x, double* out, int n) noexcept
{
    int i = 0;
    for (; i + 2 <= n; i += 2)
    {
        const v2d v = load2(x + i);
        if (anyOutside(v, kExpMax))
        {
            out[i] = std::exp(x[i]);
            out[i + 1] = std::exp(x[i + 1]);
            continue;
        }
        store2(out + i, expCore(v));
    }
    for (; i < n; ++i)
        out[i] = std::exp(x[i]);
}

// s[i] = sin(x[i]), c[i] = cos(x[i]) for i < n.
inline void sinCosArray(const double* x, double* s, double* c, int n) noexcept
{
    int i = 0;
    for (; i + 2 <= n; i += 2)
    {
        const v2d v = load2(x + i);
        if (anyOutside(v, kSinCosMax))
        {
            for (int j = i; j < i + 2; ++j)
            {
                const double a = x[j];
                s[j] = std::sin(a);
                c[j] = std::cos(a);
            }
            continue;
        }
        v2d vs, vc;
        sinCosCore(v, vs, vc);
        store2(s + i, vs);
        store2(c + i, vc);
    }
    for (; i < n; ++i)
    {
        const double a = x[i];
        s[i] = std::sin(a);
        c[i] = std::cos(a);
    }
}

} // namespace cymbalis::vecmath
