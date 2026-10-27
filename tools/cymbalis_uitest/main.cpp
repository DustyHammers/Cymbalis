// CymbalisUiTest — clicks through the Cymbalis editor inside a real plugin host (macOS).
//
//   CymbalisUiTest PLUGIN STEPS...
//
// PLUGIN is a path to Cymbalis.vst3, "AU" for the installed Cymbalis Audio Unit, or
// "AU:CODE" for an installed Audio Unit with that plugin code. The tool hosts
// the plugin with JUCE's VST3 or AU hosting code, opens its editor in a window and posts
// real mouse events (NSEvents) to that window, so they travel through the window server
// path and the plugin wrapper exactly as a host's would. After each step it reads the
// plugin's saved state through the host API and checks the result:
//
//   edge      nothing: the EDGE note of a new instance                -> note_EDGE 64
//   channel   click the MIDI CHANNEL box, then channel 5 in the grid  -> midiChannel 5
//   drag      drag the BOW note box up by 40 design units             -> note_BOW 67
//   reset     click the BOW note box, then RESET DEFAULT              -> note_BOW 62
//   none      click the BELL note box, then NONE                      -> note_BELL -1
//   edgereset click the EDGE note box, NONE, then again, RESET DEFAULT -> note_EDGE 64
//   list      click the BELL2 note box, then the note row below C#3   -> note_BELL2 62
//   stick     click the BELL stick box, then TIP                      -> stick_BELL TIP
//   preset    click the CYMBAL box, then HAMMERED THIN RIDE            -> preset hammered_thin_ride
//   shoulder  (after preset) click the BELL stick box, then SHOULDER  -> stick_hammered_thin_ride_BELL SHOULDER
//   swish     click the CYMBAL box, then SWISH (the fourth)          -> preset swish
//   raven     click the CYMBAL box, then RAVEN (the fifth)           -> preset raven
//   wheel     the scroll wheel over the five knobs, a note box, a stick box, the CYMBAL and
//             MIDI CHANNEL boxes                                     -> state and parameters unchanged
//
// The Tuning window (docs/TUNING.md); these read the windows, not the saved state:
//
//   tuning       click TUNING        -> the Tuning window is visible and in front of the editor
//   tuningclose  click TUNING again  -> the Tuning window is hidden
//
// and, with the Tuning window open, this one reads the saved state again:
//
//   wheeltuning  the scroll wheel over the Tuning window's sliders and numeric boxes, down
//                its list (the wheel scrolls the list)               -> state and parameters unchanged
//
// Exit code 0 when every step passed.
//
//   CymbalisUiTest hits
//
// runs no plugin: it times Cymbalis's own processor and editor, compiled into this tool
// (HitTiming.h): processBlock while a strike every 24 ms keeps 16 marks active, with the
// editor closed, open at 1536 x 1024 with the hit display, open with a dummy of the same
// painting and open with no hits, and the editor's paint calls.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <algorithm>
#include <cstdio>
#include <functional>

#include "HitTiming.h"
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
        // The whole element: <Session .../>, or <Session ...> with its Tuning children.
        const char* tagEnd = std::find (start, end, '>');
        if (tagEnd == end)
            return {};
        if (tagEnd[-1] == '/')
            return juce::String::fromUTF8 (start, (int) (tagEnd + 1 - start));
        const char close[] = "</Session>";
        const char* stop = std::search (tagEnd, end, close, close + sizeof (close) - 1);
        return stop != end ? juce::String::fromUTF8 (start, (int) (stop + sizeof (close) - 1 - start)) : juce::String();
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
        if (args.size() == 1 && args[0] == "hits")
        {
            hitTiming = std::make_unique<HitTiming> ([this] (int code) {
                setApplicationReturnValue (code);
                quit();
            });
            return;
        }
        if (args.size() < 2)
        {
            std::printf ("usage: CymbalisUiTest PLUGIN STEPS...  or  CymbalisUiTest hits\n");
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
        hitTiming.reset();
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

    // The saved session settings and every parameter's value, through the host API.
    juce::String snapshot()
    {
        juce::MemoryBlock state;
        plugin->getStateInformation (state);
        auto s = findSession (state);
        for (auto* p : plugin->getParameters())
            s << " | " << p->getName (64) << " " << juce::String (p->getValue(), 9);
        return s;
    }

    // Unchanged since `before`, or what changed and what failed while turning the wheel.
    juce::String unchanged()
    {
        const auto now = snapshot();
        if (wheelError.isNotEmpty())
            return "wheel failed: " + wheelError;
        return now == before ? juce::String ("unchanged") : "changed: " + now + "\n      was:     " + before;
    }

    void addStep (const juce::String& name)
    {
        Step s { name, {}, {}, {}, {} };
        auto wait = [] {};
        if (name == "edge")
        {
            s.actions = { wait };
            s.key = "note_EDGE";
            s.expected = "64";
        }
        else if (name == "channel")
        {
            // The MIDI CHANNEL box (1404, 30, 110 x 36, top right; UI_SPEC section 3). Grid popup:
            // 4 x 61 wide plus 2 x 8 (260), below the box (top 66 + 3) and right-aligned with it
            // (left 1514 - 260); 4 rows of 40. Channel 5 starts the second row.
            s.actions = { click (1459.0f, 48.0f), wait, click (1254.0f + 8.0f + 30.0f, 69.0f + 8.0f + 40.0f + 20.0f), wait };
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
        else if (name == "edgereset")
        {
            s.actions = { click (1457.0f, 312.0f), wait, click (1420.0f, menuItemY (325.0f, 2)), wait,
                          click (1457.0f, 312.0f), wait, click (1420.0f, menuItemY (325.0f, 1)), wait };
            s.key = "note_EDGE";
            s.expected = "64";
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
        else if (name == "shoulder")
        {
            // The TIP / SHOULDER popup below the stick box: inset 8, items 32 high; SHOULDER second.
            s.actions = { click (1314.0f, 152.0f), wait, click (1314.0f, 165.0f + 3.0f + 8.0f + 32.0f + 16.0f), wait };
            s.key = "stick_hammered_thin_ride_BELL";
            s.expected = "SHOULDER";
        }
        else if (name == "tuning" || name == "tuningclose")
        {
            // The TUNING button (PluginEditor.cpp kTuningButton: 61, 501, 277 x 62), under the
            // CYMBAL box.
            s.actions = { click (199.0f, 532.0f), wait, wait };
            s.key = "Tuning window";
            s.expected = name == "tuning" ? "visible-front" : "hidden";
            s.probe = [this] { return cymbalis::uitest::tuningWindowState (*editor); };
        }
        else if (name == "wheel")
        {
            // The five knobs (UI_SPEC section 3), the BOW note box, the BELL stick box, the
            // CYMBAL and MIDI CHANNEL boxes.
            s.actions = { [this] {
                             before = snapshot();
                             wheelError = {};
                             const juce::Point<float> points[] = { { 226.0f, 881.0f }, { 600.0f, 881.0f }, { 974.0f, 881.0f }, { 1349.0f, 642.0f },
                                                                   { 1350.0f, 881.0f }, { 1457.0f, 232.0f }, { 1314.0f, 152.0f },
                                                                   { 199.0f, 402.0f },  { 1459.0f, 48.0f } };
                             for (auto p : points)
                                 wheelError += cymbalis::uitest::wheel (*editor, at (p.x, p.y));
                         },
                          wait };
            s.key = "after the wheel";
            s.expected = "unchanged";
            s.probe = [this] { return unchanged(); };
        }
        else if (name == "wheeltuning")
        {
            // Down the window in steps, over the value rows' names (30 % of the width), sliders
            // (55 %) and numeric boxes (91 %); the band rows' sliders span the width. Each turn
            // scrolls the list on, so the rows under the pointer change.
            s.actions.push_back ([this] {
                before = snapshot();
                wheelError = {};
            });
            for (int i = 0; i < 16; ++i)
                s.actions.push_back ([this, i] {
                    for (float fx : { 0.3f, 0.55f, 0.91f })
                        for (float fy = 0.2f + 0.01f * (float) i; fy < 0.97f; fy += 0.16f)
                            wheelError += cymbalis::uitest::wheelTuning (*editor, fx, fy);
                });
            s.actions.push_back (wait);
            s.key = "after the wheel";
            s.expected = "unchanged";
            s.probe = [this] { return unchanged(); };
        }
        else if (name == "preset")
        {
            // List below the CYMBAL box (61, 371, 277 x 62): 4 units down, inset 8, rows of 40.
            s.actions = { click (199.0f, 402.0f), wait, click (199.0f, 433.0f + 4.0f + 8.0f + 2.0f * 40.0f + 20.0f), wait };
            s.key = "preset";
            s.expected = "hammered_thin_ride";
        }
        else if (name == "swish")
        {
            // The same list; SWISH is its fourth row.
            s.actions = { click (199.0f, 402.0f), wait, click (199.0f, 433.0f + 4.0f + 8.0f + 3.0f * 40.0f + 20.0f), wait };
            s.key = "preset";
            s.expected = "swish";
        }
        else if (name == "raven")
        {
            // The same list; RAVEN is its fifth row.
            s.actions = { click (199.0f, 402.0f), wait, click (199.0f, 433.0f + 4.0f + 8.0f + 4.0f * 40.0f + 20.0f), wait };
            s.key = "preset";
            s.expected = "raven";
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

    std::unique_ptr<HitTiming> hitTiming;
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::AudioProcessorEditor* editor = nullptr;
    std::unique_ptr<Window> window;
    std::vector<Step> steps;
    int stepIndex = 0, actionIndex = 0, passed = 0;
    juce::String before, wheelError; // the wheel steps
};

START_JUCE_APPLICATION (UiTestApp)
