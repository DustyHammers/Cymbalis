// cymbalis_pack_bake — packs a baked preset (JSON, written by tools/bake_preset.py) into the
// compact form the plugin embeds (plugin/PackedBake.h). CMake runs it on every bake in
// resources/presets/ when it builds the plugin's binary data.
//
//   cymbalis_pack_bake IN.bake.json OUT.bake.pack
//
// It checks its own output before writing it: the packed form must unpack to the same JSON
// tree, bit for bit, and that tree must load as a bake. A bake in the table below must also
// give EDGE VOICE FROM BOW's check values at 100 % (docs/TUNING.md). Otherwise the build
// fails here.
#include "Bake.h"
#include "PackedBake.h"
#include "Tuning.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <set>
#include <string>
#include <vector>

namespace
{
// docs/TUNING.md, "EDGE VOICE FROM BOW": at 100 %, the modes with alpha = 1 and with
// 0 < alpha < 1, and how many of the 20 longest-ringing edge modes (top velocity row of the
// voiced velw x sqrt(T60)) are among the bow's 20 (exc_norm x sqrt(T60)).
struct VoiceCheck
{
    const char* bake;
    int alphaOne, alphaPartial, shared;
};
const VoiceCheck voiceChecks[] = {
    { "smoke_flat_ride.bake.json", 206, 469, 14 },
    { "swish.bake.json", 226, 378, 6 },
    { "konstan.bake.json", 67, 556, 10 },
};

// The indices of the `k` largest of `x` (n values).
std::set<size_t> topK (const std::vector<double>& x, size_t k)
{
    std::vector<size_t> idx (x.size());
    std::iota (idx.begin(), idx.end(), size_t (0));
    std::stable_sort (idx.begin(), idx.end(), [&] (size_t a, size_t b) { return x[a] > x[b]; });
    return std::set<size_t> (idx.begin(), idx.begin() + (long) std::min (k, idx.size()));
}

void checkEdgeVoice (const cymbalis::BakeData& b, const std::string& name)
{
    for (const auto& c : voiceChecks)
    {
        if (name != c.bake)
            continue;
        const auto& z = b.edgeNpz;
        const size_t n = z.bandIndex.size(), nv = z.velocities.size();
        int one = 0, partial = 0;
        for (double a : z.voiceAlpha)
        {
            one += a == 1.0 ? 1 : 0;
            partial += a > 0.0 && a < 1.0 ? 1 : 0;
        }
        std::vector<double> velw (z.velw.size()), bow (n), edge (n);
        cymbalis::edgeVoiceVelw (b, 100.0, velw.data());
        for (size_t i = 0; i < n; ++i)
        {
            bow[i] = b.excNorm[i] * std::sqrt (b.pythonT60S[i]);
            edge[i] = velw[(nv - 1) * n + i] * std::sqrt (b.pythonT60S[i]);
        }
        const auto bowTop = topK (bow, 20), edgeTop = topK (edge, 20);
        int shared = 0;
        for (size_t i : edgeTop)
            shared += bowTop.count (i) != 0 ? 1 : 0;
        if (one != c.alphaOne || partial != c.alphaPartial || shared != c.shared)
            throw cymbalis::BakeError (name + ": EDGE VOICE FROM BOW gives alpha = 1 for " + std::to_string (one)
                                       + " modes, 0 < alpha < 1 for " + std::to_string (partial) + ", "
                                       + std::to_string (shared) + " shared of 20; the check table has "
                                       + std::to_string (c.alphaOne) + ", " + std::to_string (c.alphaPartial) + ", "
                                       + std::to_string (c.shared));
        std::printf ("%s: EDGE VOICE FROM BOW matches the check table (alpha = 1: %d, 0 < alpha < 1: %d, shared at 100 %%: %d of 20)\n",
                     name.c_str(), one, partial, shared);
    }
}
} // namespace

int main (int argc, char** argv)
{
    if (argc != 3)
    {
        std::fprintf (stderr, "usage: cymbalis_pack_bake IN.bake.json OUT.bake.pack\n");
        return 2;
    }
    const auto in = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    const auto out = juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]);
    const auto name = in.getFileName().toStdString();
    try
    {
        const auto root = cymbalis::json::parseFile (in.getFullPathName().toStdString());
        const auto packed = cymbalis::packed::pack (root);
        const auto back = cymbalis::packed::unpack (packed.getData(), packed.getSize(), name);
        if (! cymbalis::packed::identical (root, back))
            throw cymbalis::BakeError (name + ": the packed preset does not unpack to the same JSON");
        checkEdgeVoice (*cymbalis::loadBakeFromJson (back, name), name);
        if (! out.replaceWithData (packed.getData(), packed.getSize()))
            throw cymbalis::BakeError ("cannot write " + out.getFullPathName().toStdString());
        std::printf ("%s: %lld bytes of JSON, %lld bytes packed\n", name.c_str(), (long long) in.getSize(),
                     (long long) packed.getSize());
        return 0;
    }
    catch (const std::exception& e)
    {
        std::fprintf (stderr, "cymbalis_pack_bake: %s\n", e.what());
        return 1;
    }
}
