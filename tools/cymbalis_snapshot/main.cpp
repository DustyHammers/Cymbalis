// CymbalisSnapshot — renders the plugin editor to PNG files.
//
//   CymbalisSnapshot OUT_DIR [PREFIX]
//
// Writes OUT_DIR/PREFIX-1536x1024.png and OUT_DIR/PREFIX-960x640.png (PREFIX defaults to
// "Cymbalis-editor"), the largest and smallest editor sizes, with every parameter and
// setting at its default, then OUT_DIR/PREFIX-<preset id>.png at the largest size for each
// preset.
//
// JUCE shows its splash logo in the editors a process opens during the two seconds after
// the first one is painted. The first editor here is painted only to start that time and
// let it pass, so the snapshots show the editor itself.
#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <cstdio>
#include <vector>

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf (stderr, "usage: CymbalisSnapshot OUT_DIR [PREFIX]\n");
        return 2;
    }
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    const juce::String prefix = argc > 2 ? juce::String (argv[2]) : juce::String ("Cymbalis-editor");
    dir.createDirectory();

    CymbalisProcessor processor;
    if (processor.presetError().isNotEmpty())
        std::fprintf (stderr, "warning: preset failed to load: %s\n", processor.presetError().toRawUTF8());
    {
        // JUCE starts the splash time at its first paint; editors opened 2 s later have none.
        std::unique_ptr<juce::AudioProcessorEditor> first (processor.createEditor());
        first->createComponentSnapshot (first->getLocalBounds(), true, 1.0f);
        juce::Thread::sleep (2500);
    }

    struct Shot { int preset, w, h; juce::String suffix; };
    std::vector<Shot> shots { { 0, 1536, 1024, "1536x1024" }, { 0, 960, 640, "960x640" } };
    for (int k = 0; k < cymbalis::kNumPresets; ++k)
        shots.push_back ({ k, 1536, 1024, cymbalis::kPresets[(size_t) k].id });

    int failures = 0;
    for (const auto& shot : shots)
    {
        processor.setPreset (shot.preset);
        if (processor.presetError().isNotEmpty())
            std::fprintf (stderr, "warning: %s failed to load: %s\n", processor.presetName().toRawUTF8(),
                          processor.presetError().toRawUTF8());
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
        editor->setSize (shot.w, shot.h);
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const auto file = dir.getChildFile (prefix + "-" + shot.suffix + ".png");
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat png;
        if (! out.openedOk() || ! png.writeImageToStream (image, out))
        {
            std::fprintf (stderr, "cannot write %s\n", file.getFullPathName().toRawUTF8());
            ++failures;
            continue;
        }
        std::printf ("%s (%d x %d)\n", file.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
    }
    return failures == 0 ? 0 : 1;
}
