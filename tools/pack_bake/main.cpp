// cymbalis_pack_bake — packs a baked preset (JSON, written by tools/bake_preset.py) into the
// compact form the plugin embeds (plugin/PackedBake.h). CMake runs it on every bake in
// resources/presets/ when it builds the plugin's binary data.
//
//   cymbalis_pack_bake IN.bake.json OUT.bake.pack
//
// It checks its own output before writing it: the packed form must unpack to the same JSON
// tree, bit for bit, and that tree must load as a bake. A bake in the table below must also
// give its exact EDGE BAND CORRECTION (computed at load) to 4 decimals (docs/TUNING.md).
// Otherwise the build fails here.
#include "Bake.h"
#include "PackedBake.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
// docs/TUNING.md, "EDGE BAND CORRECTION": each band's exact correction, lowest band first.
struct ExactCheck
{
    const char* bake;
    std::vector<double> exact;
};
const std::vector<ExactCheck> exactChecks {
    { "smoke_flat_ride.bake.json",
      { 6.003, 8.5076, 79.0722, 8.2007, 1.0592, 0.7172, 0.6728, 1.027, 1.7581, 1.382, 1.5368, 1.6926, 1.1347, 0.8985,
        0.7926, 0.5636, 0.628, 0.7881, 1.0067, 1.0897, 1.0027, 1.2846 } },
    { "hammered_thin_ride.bake.json",
      { 6.276, 3.6348, 23.3902, 6.7488, 4.6813, 1.9353, 0.3266, 0.5257, 0.5147, 0.9561, 1.4796, 1.0751, 1.2918, 1.6642,
        2.123, 1.4032, 1.7312, 2.5777, 3.0145, 4.722, 6.1719, 7.1324 } },
    { "swish.bake.json",
      { 14.5028, 28.8111, 32.7788, 4.0719, 7.7747, 5.1501, 3.3818, 1.1153, 1.4556, 1.7042, 1.9811, 1.3779, 0.7668, 0.553,
        0.7034, 0.7435, 0.9098, 0.7675, 0.7034, 0.5226 } },
    { "raven.bake.json",
      { 23.5992, 27.5156, 15.0682, 0.9655, 8.8895, 2.0727, 1.6207, 0.43, 0.5786, 1.1177, 0.8563, 1.7554, 1.0814, 1.3033,
        1.0389, 0.606, 0.7946, 0.9271, 1.1495, 1.3506, 2.0037, 3.3845 } },
    { "konstan.bake.json",
      { 7.6107, 11.3065, 35.4836, 9.9379, 1.8414, 2.9377, 1.1891, 1.247, 1.7413, 2.2766, 1.5684, 1.3532, 1.3948, 0.9394,
        0.78, 0.5694, 0.4865, 0.5261, 0.6777, 0.9046, 1.1956, 1.5512 } },
};

void checkExactCorrection (const cymbalis::BakeData& b, const std::string& name)
{
    for (const auto& c : exactChecks)
    {
        if (name != c.bake)
            continue;
        const auto& got = b.edgeNpz.exactCorrection;
        if (got.size() != c.exact.size())
            throw cymbalis::BakeError (name + ": the exact EDGE BAND CORRECTION has " + std::to_string (got.size())
                                       + " bands, the check table " + std::to_string (c.exact.size()));
        for (size_t j = 0; j < got.size(); ++j)
            if (std::fabs (std::round (got[j] * 1e4) / 1e4 - c.exact[j]) > 1e-9)
            {
                char msg[160];
                std::snprintf (msg, sizeof msg, ": the exact EDGE BAND CORRECTION of band %zu is %.6f, the check table %.4f", j,
                               got[j], c.exact[j]);
                throw cymbalis::BakeError (name + msg);
            }
        std::printf ("%s: exact EDGE BAND CORRECTION matches the check table (%zu bands)\n", name.c_str(), got.size());
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
        checkExactCorrection (*cymbalis::loadBakeFromJson (back, name), name);
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
