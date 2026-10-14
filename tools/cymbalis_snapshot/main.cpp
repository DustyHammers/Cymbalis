// cymbalis_snapshot — renders the plugin editor to PNG files.
//
//   cymbalis_snapshot OUT_DIR [PREFIX]
//
// Writes OUT_DIR/PREFIX-1536x1024.png and OUT_DIR/PREFIX-960x640.png (PREFIX defaults to
// "Cymbalis-editor"), the largest and smallest editor sizes, with every parameter and
// setting at its default.
//
// JUCE shows its splash logo in the editors a process opens during the two seconds after
// the first one is painted. The first editor here is painted only to start that time and
// let it pass, so the snapshots show the editor itself.
#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <cstdio>

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf (stderr, "usage: cymbalis_snapshot OUT_DIR [PREFIX]\n");
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

    int failures = 0;
    for (const auto size : { juce::Point<int> { 1536, 1024 }, juce::Point<int> { 960, 640 } })
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
        editor->setSize (size.x, size.y);
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const auto file = dir.getChildFile (prefix + "-" + juce::String (size.x) + "x" + juce::String (size.y) + ".png");
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
