// tools/cymbalis_identity/wire.cpp — the wireframe cymbal's colours from the CPU path
// (wire::bronze, wire::segmentColour, wire::surfaceColour: what the still drawing uses, and what
// the shaders do on the GPU), compiled once with REF's plugin/ui and once with the working tree's
// (wire.sh). Plain C++, no JUCE.
//
//   wire dump DIR
//       Every shape in CymbalShapes.h: at rest and in four frames after three hits (bow, edge and
//       bell, one with the cymbal raised and its wobble halved), each mesh segment's colour and each
//       surface vertex's colour (ytl from its own screen height, as the still drawing), then
//       bronze() over a grid of its inputs (shade, ytl, every metal, factor, dent light and glow)
//       with the shape's roseK. Raw floats, one file per shape: DIR/NN_key.bin.
//
//   wire compare REF_DIR DIR
//       Per shape: every shape but NOVA RIDE bit for bit as REF; on NOVA RIDE (roseK 0.62), how many
//       colours differ. And each shape's two rose ends from bronze(): the light end at full light,
//       the dark end solved from bronze() at no light, each to 8 bits, against the expected hex
//       (NOVA RIDE #452E24 and #8A6D5D, every other shape #704A3A and #DEB096). Exit status 1 if one
//       fails.
//
// REF's model may predate CymbalShape::roseK and bronze()'s roseK: the calls below fall back to
// REF's own signatures (roseK 1) where it has none.

#include "ui/CymbalModel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
using namespace cymbalis::ui;

struct Named
{
    const CymbalShape* shape;
    const char* name;
};
const Named kShapes[] = {
    { &kNovaShape, "NOVA RIDE" },
    { &kHammeredShape, "HAMMERED THIN RIDE" },
    { &kSmokeShape, "STINE FLAT RIDE (SMOKE's shape)" },
    { &kSwishShape, "SWISH" },
    { &kRavenShape, "RAVEN" },
    { &kKonstanShape, "KONSTAN CRASH 18\"" },
    { &kPaperThinShape, "EXTRA PAPER THIN 24\"" },
    { &kAzCrashShape, "CRASH 16\"" },
    { &kHiHatTopShape, "HI-HAT top" },
    { &kHiHatBottomShape, "HI-HAT bottom" },
};

// The shape's roseK, or 1 where the model has none.
template <class S>
auto roseOf (const S& s, int) -> decltype (double (s.roseK))
{
    return s.roseK;
}
template <class S>
double roseOf (const S&, long)
{
    return 1.0;
}

// bronze() with roseK, or without it (roseK 1) where the model has none.
template <class M>
auto bronzeK (double shade, double ytl, M metal, double factor, double dent, double glow, double k, int)
    -> decltype (wire::bronze (shade, ytl, metal, factor, dent, glow, k))
{
    return wire::bronze (shade, ytl, metal, factor, dent, glow, k);
}
template <class M>
wire::Rgb bronzeK (double shade, double ytl, M metal, double factor, double dent, double glow, double, long)
{
    return wire::bronze (shade, ytl, metal, factor, dent, glow);
}

// surfaceColour() with roseK, or without it.
template <class V>
auto surfaceK (const V& v, double ytl, double k, int) -> decltype (wire::surfaceColour (v, ytl, k))
{
    return wire::surfaceColour (v, ytl, k);
}
template <class V>
wire::Rgb surfaceK (const V& v, double ytl, double, long)
{
    return wire::surfaceColour (v, ytl);
}

std::string fileOf (const std::string& dir, size_t i)
{
    char n[8];
    std::snprintf (n, sizeof (n), "%02zu", i);
    return dir + "/" + n + "_" + kShapes[i].shape->key + ".bin";
}

void put (std::vector<float>& out, wire::Rgb c)
{
    out.push_back (c.r);
    out.push_back (c.g);
    out.push_back (c.b);
}

std::vector<float> colours (const CymbalShape& shape)
{
    const double k = roseOf (shape, 0);
    const wire::Geometry g (shape);
    std::vector<float> out;
    wire::Frame f;
    f.resize (g);
    std::vector<float> disp ((size_t) g.numVertices(), 0.0f);
    std::vector<wire::Hit> hits;
    hits.push_back (wire::makeHit (g, 0.0, 0.5, 0.9, false, 0.7));   // bow
    hits.push_back (wire::makeHit (g, 40.0, 0.0, 1.0, true, 2.4));   // edge
    hits.push_back (wire::makeHit (g, 90.0, 1.0, 0.6, false, 4.0));  // bell
    struct At
    {
        int numHits;
        double nowMs, lift, wobble;
    };
    const At frames[] = { { 0, 0.0, 0.0, 1.0 }, { 1, 30.0, 0.0, 1.0 }, { 3, 150.0, 0.0, 1.0 }, { 3, 400.0, 0.03, 0.5 }, { 3, 1500.0, 0.0, 1.0 } };
    std::vector<double> shade ((size_t) g.numVertices()), ytl ((size_t) g.numVertices());
    for (const auto& at : frames)
    {
        f.lift = at.lift;
        f.wobble = at.wobble;
        wire::evaluate (g, at.numHits > 0 ? hits.data() : nullptr, at.numHits, at.nowMs, f, disp);
        const auto ytlOf = [&f] (double sy) { return 1.0 - (sy - f.yTop) / std::max (f.yBottom - f.yTop, 1.0); };
        for (int x = 0; x < g.numVertices(); ++x)
        {
            double sx = 0.0, sy = 0.0;
            wire::projectVertex (g, f, g.camera, x, sx, sy);
            shade[(size_t) x] = wire::shadeAt (g, f, x);
            ytl[(size_t) x] = ytlOf (sy);
        }
        for (int s = 0; s < (int) g.lineIndices.size() / 2; ++s)
        {
            const auto a = g.lineIndices[(size_t) (2 * s)], b = g.lineIndices[(size_t) (2 * s + 1)];
            put (out, wire::segmentColour (g, f, s, shade[a], ytl[a], shade[b], ytl[b]));
        }
        for (const auto& v : g.surface)
        {
            double sx = 0.0, sy = 0.0;
            wire::projectSurface (v, f, g.camera, sx, sy);
            put (out, surfaceK (v, ytlOf (sy), k, 0));
        }
    }
    for (double sh : { 0.0, 0.5, 0.86, 0.9, 0.93, 0.97, 1.0 })
        for (double y : { 0.0, 0.3, 0.7, 1.0 })
            for (int m = 0; m <= 4; ++m)
                for (double factor : { 0.3, 0.8, 1.0, 1.15 })
                    for (double dent : { 0.25, 0.7, 1.0, 1.3, 1.9, 2.5 })
                        for (double glow : { 0.0, 0.4, 1.0 })
                            put (out, bronzeK (sh, y, (wire::Metal) m, factor, dent, glow, k, 0));
    return out;
}

int dump (const std::string& dir)
{
    for (size_t i = 0; i < std::size (kShapes); ++i)
    {
        const auto c = colours (*kShapes[i].shape);
        std::ofstream o (fileOf (dir, i), std::ios::binary);
        o.write (reinterpret_cast<const char*> (c.data()), (std::streamsize) (c.size() * sizeof (float)));
        if (! o)
        {
            std::fprintf (stderr, "wire: cannot write %s\n", fileOf (dir, i).c_str());
            return 2;
        }
    }
    return 0;
}

std::vector<float> load (const std::string& path)
{
    std::ifstream in (path, std::ios::binary | std::ios::ate);
    if (! in)
        return {};
    std::vector<float> v ((size_t) in.tellg() / sizeof (float));
    in.seekg (0);
    in.read (reinterpret_cast<char*> (v.data()), (std::streamsize) (v.size() * sizeof (float)));
    return v;
}

// A colour channel to 8 bits.
int byteOf (double c)
{
    return (int) std::lround (c * 255.0);
}

int compare (const std::string& refDir, const std::string& dir)
{
    bool ok = true;
    for (size_t i = 0; i < std::size (kShapes); ++i)
    {
        const auto& shape = *kShapes[i].shape;
        const double k = roseOf (shape, 0);
        const auto a = load (fileOf (refDir, i)), b = load (fileOf (dir, i));
        size_t differ = 0;
        double largest = 0.0;
        for (size_t j = 0; j < std::min (a.size(), b.size()); ++j)
            if (std::memcmp (&a[j], &b[j], sizeof (float)) != 0)
            {
                ++differ;
                largest = std::max (largest, (double) std::abs (a[j] - b[j]));
            }
        const size_t n = b.size();
        // Its two rose ends from bronze(): plain metal, no dent, no glow.
        const wire::Rgb bright = bronzeK (2.0, 1.0, wire::Metal::plain, 1.0, 1.0, 0.0, k, 0); // full light: base 1
        const wire::Rgb low = bronzeK (0.0, 0.0, wire::Metal::plain, 1.0, 1.0, 0.0, k, 0);    // no light: base 0.25^1.4
        const double b0 = std::pow (0.25, 1.4);
        const auto darkOf = [b0] (float c, float l) { return ((double) c - (double) l * b0) / (1.0 - b0); };
        const int got[6] = { byteOf (darkOf (low.r, bright.r)), byteOf (darkOf (low.g, bright.g)), byteOf (darkOf (low.b, bright.b)),
                             byteOf (bright.r), byteOf (bright.g), byteOf (bright.b) };
        // NOVA RIDE's darker rose bronze (2026-11-27); every other shape as REF, bit for bit.
        const bool nova = kShapes[i].shape == &kNovaShape;
        const int want[6] = { nova ? 0x45 : 0x70, nova ? 0x2e : 0x4a, nova ? 0x24 : 0x3a, nova ? 0x8a : 0xde, nova ? 0x6d : 0xb0, nova ? 0x5d : 0x96 };
        const bool endsOk = std::memcmp (got, want, sizeof (got)) == 0;
        std::printf ("%-34s roseK %g: rose ends #%02X%02X%02X, #%02X%02X%02X (%s); ", kShapes[i].name, k, got[0], got[1], got[2], got[3],
                     got[4], got[5], endsOk ? "as expected" : "FAILED");
        if (a.size() != b.size() || n == 0)
        {
            std::printf ("FAILED: %zu colours against REF's %zu\n", n / 3, a.size() / 3);
            ok = false;
        }
        else if (! nova)
        {
            std::printf ("%s (%zu colours)\n", differ == 0 ? "bit for bit as REF" : "FAILED: not bit for bit as REF", n / 3);
            if (differ != 0)
                std::printf ("    %zu of %zu channels differ, largest by %.3g\n", differ, n, largest);
            ok = ok && differ == 0;
        }
        else
            std::printf ("%zu of %zu channels differ from REF (the darker rose bronze), largest by %.3f\n", differ, n, largest);
        ok = ok && endsOk;
    }
    std::printf ("\n%s\n", ok ? "Wireframe colours checks passed." : "A wireframe colours check FAILED.");
    return ok ? 0 : 1;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc == 3 && std::string (argv[1]) == "dump")
        return dump (argv[2]);
    if (argc == 4 && std::string (argv[1]) == "compare")
        return compare (argv[2], argv[3]);
    std::fprintf (stderr, "usage: wire dump DIR | wire compare REF_DIR DIR\n");
    return 2;
}
