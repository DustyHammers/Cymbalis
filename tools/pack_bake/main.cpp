// cymbalis_pack_bake — packs a baked preset (JSON, written by tools/bake_preset.py) into the
// compact form the plugin embeds (plugin/PackedBake.h). CMake runs it on every bake in
// resources/presets/ when it builds the plugin's binary data.
//
//   cymbalis_pack_bake IN.bake.json OUT.bake.pack
//
// It checks its own output before writing it: the packed form must unpack to the same JSON
// tree, bit for bit, and that tree must load as a bake. Otherwise the build fails here.
#include "Bake.h"
#include "PackedBake.h"

#include <cstdio>

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
        cymbalis::loadBakeFromJson (back, name);
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
