// CymbalisUiTest — clicks through the Cymbalis editor inside a real plugin host (macOS).
//
//   CymbalisUiTest PLUGIN STEPS...
//
// PLUGIN is a path to Cymbalis.vst3 (or Cymbalis Tuning.vst3), "AU" for the installed
// Cymbalis Audio Unit, or "AU:CymT" for the one with that plugin code. The tool hosts
// the plugin with JUCE's VST3 or AU hosting code, opens its editor in a window and posts
// real mouse events (NSEvents) to that window, so they travel through the window server
// path and the plugin wrapper exactly as a host's would. After each step it reads the
// plugin's saved state through the host API and checks the result:
//
//   channel   click the MIDI CHANNEL box, then channel 5 in the grid  -> midiChannel 5
//   drag      drag the BOW note box up by 40 design units             -> note_BOW 67
//   reset     click the BOW note box, then RESET DEFAULT              -> note_BOW 62
//   none      click the BELL note box, then NONE                      -> note_BELL -1
//   list      click the BELL2 note box, then the note row below C#3   -> note_BELL2 62
//   stick     click the BELL stick box, then TIP                      -> stick_BELL TIP
//   preset    click the CYMBAL box, then HAMMERED THIN RIDE            -> preset hammered_thin_ride
//
// Cymbalis Tuning only (docs/TUNING.md); these read the windows, not the saved state:
//
//   tuning       click TUNING        -> the Tuning window is visible and in front of the editor
//   tuningclose  click TUNING again  -> the Tuning window is hidden
//
// Exit code 0 when every step passed.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <algorithm>
#include <cstdio>
#include <functional>

#include "MacEvents.h"

namespace
{
// Layout constants of the editor (design units of the 1536 x 1024 artwork).
constexpr float kDesignWidth = 1536.0f, kDesignHeight = 1024.0f;

juce::String findSession (const juce::MemoryBlock& data, int depth = 0)
{
    // The state holds binary headers (zero bytes), so search the bytes, not a string.
    const char* begin = (const char*) data.getData();
    const char* end = begin + data.getSize();
    const char tag[] = "<Session ";
    const char* start = std::search (begin, end, tag, tag + sizeof (tag) - 1);
    if (start != end)
    {
        const char close[] = "/>";
        const char* stop = std::search (start, end, close, close + 2);
        return stop != end ? juce::String::fromUTF8 (start, (int) (stop + 2 - start)) : juce::String();
    }
    if (depth > 3)
        return {};
    // JUCE's VST3 host wraps the plugin's state in XML with base64 text (MemoryBlock encoding).
    if (auto xml = juce::AudioProcessor::getXmlFromBinary (data.getData(), (int) data.getSize()))
        for (auto* child : xml->getChildIterator())
        {
            juce::MemoryBlock inner;
            if (inner.fromBase64Encoding (child->getAllSubText().trim()))
            {
                auto s = findSession (inner, depth + 1);
                if (s.isNotEmpty())
                    return s;
            }
        }
    return {};
}

juce::String attribute (const juce::String& session, const juce::String& name)
{
    if (auto xml = juce::parseXML (session))
        return xml->getStringAttribute (name);
    return {};
}

class Window final : public juce::DocumentWindow
{
public:
    Window() : DocumentWindow ("CymbalisUiTest", juce::Colours::black, 0)
    {
        setUsingNativeTitleBar (true);
    }
    void closeButtonPressed() override { juce::JUCEApplication::quit(); }
};

struct Step
{
    juce::String name;
    std::vector<std::function<void()>> actions; // one per tick
    juce::String key, expected;
    std::function<juce::String()> probe; // when set, the value checked instead of a state key
};
} // namespace

class UiTestApp final : public juce::JUCEApplication, private juce::Timer
{
public:
    const juce::String getApplicationName() override { return "CymbalisUiTest"; }
    const juce::String getApplicationVersion() override { return "1.0"; }

    void initialise (const juce::String&) override
    {
        auto args = getCommandLineParameterArray();
        if (args.size() < 2)
        {
            std::printf ("usage: CymbalisUiTest PLUGIN STEPS...\n");
            setApplicationReturnValue (2);
            quit();
            return;
        }
        if (! load (args[0]))
        {
            setApplicationReturnValue (1);
            quit();
            return;
        }
        for (int i = 1; i < args.size(); ++i)
            addStep (args[i]);
        startTimer (350);
    }

    void shutdown() override
    {
        stopTimer();
        if (plugin != nullptr)
            if (auto* ed = plugin->getActiveEditor())
            {
                window->clearContentComponent();
                delete ed;
            }
        window.reset();
        plugin.reset();
    }

private:
    bool load (const juce::String& what)
    {
        juce::AudioPluginFormatManager formats;
        formats.addDefaultFormats();
        juce::OwnedArray<juce::PluginDescription> found;
        for (auto* f : formats.getFormats())
        {
            if (what == "AU" || what.startsWith ("AU:"))
            {
                if (f->getName() != "AudioUnit")
                    continue;
                const auto code = what == "AU" ? juce::String ("Cymb") : what.fromFirstOccurrenceOf ("AU:", false, false);
                for (auto& id : f->searchPathsForPlugins ({}, false, false))
                    if (id.contains ("," + code + ","))
                        f->findAllTypesForFile (found, id);
            }
            else if (f->fileMightContainThisPluginType (what))
                f->findAllTypesForFile (found, what);
        }
        if (found.isEmpty())
        {
            std::printf ("FAIL: no Cymbalis plugin found for '%s'\n", what.toRawUTF8());
            return false;
        }
        juce::String error;
        plugin = formats.createPluginInstance (*found[0], 48000.0, 512, error);
        if (plugin == nullptr)
        {
            std::printf ("FAIL: cannot load %s: %s\n", found[0]->name.toRawUTF8(), error.toRawUTF8());
            return false;
        }
        plugin->prepareToPlay (48000.0, 512);
        std::printf ("hosting %s (%s)\n", found[0]->name.toRawUTF8(), found[0]->pluginFormatName.toRawUTF8());
        editor = plugin->createEditorIfNeeded();
        window = std::make_unique<Window>();
        window->setContentNonOwned (editor, true);
        window->setTopLeftPosition (80, 80);
        window->setVisible (true);
        window->toFront (true);
        std::printf ("editor %d x %d\n", editor->getWidth(), editor->getHeight());
        std::fflush (stdout);
        return true;
    }

    // Design point -> point in the host's editor component (the editor scales the design to
    // fit and centres it).
    juce::Point<float> at (float x, float y) const
    {
        const float w = (float) editor->getWidth(), h = (float) editor->getHeight();
        const float s = juce::jmin (w / kDesignWidth, h / kDesignHeight);
        return { (w - s * kDesignWidth) * 0.5f + x * s, (h - s * kDesignHeight) * 0.5f + y * s };
    }

    std::function<void()> click (float x, float y)
    {
        return [this, x, y] { cymbalis::uitest::click (*editor, at (x, y)); };
    }

    void addStep (const juce::String& name)
    {
        Step s { name, {}, {}, {} };
        auto wait = [] {};
        if (name == "channel")
        {
            // Grid popup: 4 x 61 wide plus 2 x 8, centred over the box, above it; 4 rows of 40.
            s.actions = { click (770.0f, 967.0f), wait, click (770.0f - 130.0f + 8.0f + 30.0f, 756.0f + 8.0f + 40.0f + 20.0f), wait };
            s.key = "midiChannel";
            s.expected = "5";
        }
        else if (name == "drag")
        {
            s.actions = { [this] { cymbalis::uitest::drag (*editor, at (1457.0f, 232.0f), at (1457.0f, 192.0f)); }, wait };
            s.key = "note_BOW";
            s.expected = "67";
        }
        else if (name == "reset")
        {
            s.actions = { click (1457.0f, 232.0f), wait, click (1420.0f, menuItemY (245.0f, 1)), wait };
            s.key = "note_BOW";
            s.expected = "62";
        }
        else if (name == "none")
        {
            s.actions = { click (1457.0f, 152.0f), wait, click (1420.0f, menuItemY (165.0f, 2)), wait };
            s.key = "note_BELL";
            s.expected = "-1";
        }
        else if (name == "list")
        {
            s.actions = { click (1457.0f, 392.0f), wait, click (1420.0f, listRowBelowCurrentY (405.0f)), wait };
            s.key = "note_BELL2";
            s.expected = "62";
        }
        else if (name == "stick")
        {
            s.actions = { click (1314.0f, 152.0f), wait, click (1314.0f, 165.0f + 3.0f + 8.0f + 17.0f), wait };
            s.key = "stick_BELL";
            s.expected = "TIP";
        }
        else if (name == "tuning" || name == "tuningclose")
        {
            // The TUNING button (PluginEditor.cpp kTuningButton: 1404, 30, 110 x 36), top right.
            s.actions = { click (1459.0f, 48.0f), wait, wait };
            s.key = "Tuning window";
            s.expected = name == "tuning" ? "visible-front" : "hidden";
            s.probe = [this] { return cymbalis::uitest::tuningWindowState (*editor); };
        }
        else if (name == "preset")
        {
            // List below the CYMBAL box (61, 371, 277 x 62): 4 units down, inset 8, rows of 40.
            s.actions = { click (199.0f, 402.0f), wait, click (199.0f, 433.0f + 4.0f + 8.0f + 2.0f * 40.0f + 20.0f), wait };
            s.key = "preset";
            s.expected = "hammered_thin_ride";
        }
        else
        {
            std::printf ("unknown step %s\n", name.toRawUTF8());
            return;
        }
        steps.push_back (s);
    }

    // Note menu 3 units below a note box whose bottom is boxBottom (PluginEditor.h, NoteMenu):
    // inset 8, three actions 34 high, a gap of 8, then rows 26 high; the current note is the
    // 5th visible row.
    static float menuItemY (float boxBottom, int item) { return boxBottom + 3.0f + 8.0f + 34.0f * (float) item + 17.0f; }
    static float listRowBelowCurrentY (float boxBottom) { return boxBottom + 3.0f + 8.0f + 3.0f * 34.0f + 8.0f + 5.0f * 26.0f + 13.0f; }

    void timerCallback() override
    {
        if (stepIndex >= (int) steps.size())
        {
            stopTimer();
            std::printf ("%d of %d steps passed\n", passed, (int) steps.size());
            setApplicationReturnValue (passed == (int) steps.size() ? 0 : 1);
            quit();
            return;
        }
        auto& step = steps[(size_t) stepIndex];
        if (actionIndex < (int) step.actions.size())
        {
            step.actions[(size_t) actionIndex++]();
            return;
        }
        juce::MemoryBlock state;
        plugin->getStateInformation (state);
        const auto session = findSession (state);
        const auto value = step.probe ? step.probe() : attribute (session, step.key);
        const bool ok = value == step.expected;
        passed += ok ? 1 : 0;
        std::printf ("%s: %s %s = %s (expected %s)\n", ok ? "PASS" : "FAIL", step.name.toRawUTF8(), step.key.toRawUTF8(),
                     value.isEmpty() ? "?" : value.toRawUTF8(), step.expected.toRawUTF8());
        if (! ok)
            std::printf ("      session: %s\n", session.toRawUTF8());
        std::fflush (stdout);
        ++stepIndex;
        actionIndex = 0;
    }

    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::AudioProcessorEditor* editor = nullptr;
    std::unique_ptr<Window> window;
    std::vector<Step> steps;
    int stepIndex = 0, actionIndex = 0, passed = 0;
};

START_JUCE_APPLICATION (UiTestApp)
