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
//   sticks    nothing: the sticks of a new instance, every preset     -> BELL and BOW TIP,
//             BELL2 and BOW2 SHOULDER (CENTER and CENTER2 on SMOKE FLAT RIDE)
//   channel   click the MIDI CHANNEL box, then channel 5 in the grid  -> midiChannel 5
//   drag      drag the BOW note box up by 40 design units             -> note_BOW 67
//   reset     click the BOW note box, then RESET DEFAULT              -> note_BOW 62
//   none      click the BELL note box, then NONE                      -> note_BELL -1
//   edgereset click the EDGE note box, NONE, then again, RESET DEFAULT -> note_EDGE 64
//   list      click the BELL2 note box, then the note row below C#3   -> note_BELL2 62
//   stick     click the BELL stick box, then SHOULDER                 -> stick_BELL SHOULDER
//   preset    click the CYMBAL box, then HAMMERED THIN RIDE            -> preset hammered_thin_ride
//   shoulder  (after preset) click the BELL stick box, then SHOULDER  -> stick_hammered_thin_ride_BELL SHOULDER
//   swish     click the CYMBAL box, then SWISH (the fourth)          -> preset swish
//   raven     click the CYMBAL box, then RAVEN (the fifth)           -> preset raven
//   konstan   click the CYMBAL box, then KONSTAN CRASH 18" (the sixth) -> preset konstan
//   hihat     click the CYMBAL box, then HI-HAT (the seventh)          -> preset hihat
//   hihatplay (on HI-HAT) MIDI through the host into the plugin, on the session's channel and
//             pedal CC: notes 42 and 46 (its rows' defaults, BOW and EDGE), each must sound;
//             the pedal open, 46 must ring on long after a closed one has gone; the pedal
//             closed again, FOOT must sound without a note       -> played
//   hihatpedal (on HI-HAT, after hihatplay) the pedal CC through the host into the plugin at 0,
//             64, 127 and 0 again, each held 1 s: the hats and the pedal drawing must show that
//             opening (their accessibility descriptions, "open 0.504")
//                                                                    -> followed
//   pedalchannel (on HI-HAT, after hihatplay) MIDI CHANNEL set to 16 in its grid, then the pedal
//             CC sent on channel 1 (as a keyboard's mod wheel), sweeping 0 -> 127 -> 0 in steps:
//             after each, the hats and the pedal drawing must show its opening, and over the
//             sweep the output must start one sound, FOOT, on the close    -> followed, 1 FOOT
//   pedalcc   (on HI-HAT) click the PEDAL box, then CC 4 in its grid; then CC 1; then the state
//             saved with CC 4 set again through the host            -> pedalCc 4 saved, 1 picked,
//             4 restored, and the box shows each (its accessibility description)
//   blendhidden (on HI-HAT) no slider at either Blend Position knob   -> hidden
//   blendshown  (on another preset) both Blend Position knobs give their values -> shown
//   wheel     the scroll wheel over the five knobs, a note box, a stick box, the CYMBAL and
//             MIDI CHANNEL boxes                                     -> state and parameters unchanged
//   arcs      (Tuning window closed) for each of the five knobs: drag it 40 design units, double-
//             click it, automate its parameter through the host, and back to its default
//                                                                    -> each moves the parameter and
//             the value the knob shows (its accessibility value), which its arc is drawn from
//
// The Tuning window (docs/TUNING.md); these read the windows, not the saved state:
//
//   tuning       click the CYMBALIS title (top left)  -> the Tuning window is visible and in
//                                                        front of the editor
//   tuningclose  click it again                       -> the Tuning window is hidden
//
// and, with the Tuning window open, this one reads the saved state again:
//
//   wheeltuning  the scroll wheel over the Tuning window's sliders and numeric boxes, down
//                its list (the wheel scrolls the list)               -> state and parameters unchanged
//
// and these, with the Tuning window open, read the tuning the saved state holds for the
// current preset (its tuning file, docs/TUNING.md):
//
//   voice        click EDGE VOICE FROM BOW's slider a quarter of the way along
//                                                                    -> edge_voice_from_bow moved
//                (to between 10 and 60 %, from the bake's 0 or 100)
//   clickweights click the first articulation's STICK CLICK bow, bell and edge weight sliders a
//                quarter of the way along                            -> the three weights moved
//                (to between 0.1 and 0.6, from the bake's 0 or 1)
//   hitvariation click HIT VARIATION's switch, then its POSITION SPREAD and VELOCITY SPREAD
//                sliders halfway along                               -> the switch flipped, the
//                spreads moved (to above 0.06 and 6 %, from 0.04 and 3 %; the slider's
//                frame holds its text box too, so halfway is about 0.6 of the range)
//   savetuning   click SAVE...; the save panel answers a file in a temporary folder
//                                                                    -> the file holds the current tuning
//   loadtuning   (after savetuning) click RESET ALL, then LOAD...; the open panel answers that file
//                                                                    -> the tuning is the file's again
//
// The panels are not shown: in this process they answer at once (MacEvents.h).
//
//   profile      (on SWISH) click NOVA SHOULDER PROFILE, the first in the list: bow_shoulder's
//                                                                    -> bow_shoulder's band gains,
//                attack gains and attack time are Nova's bow profile (ShoulderProfiles.h)
//   noprofile    (on NOVA RIDE) look for NOVA SHOULDER PROFILE down the list -> none
//   hihattuning  (on HI-HAT) the list shows the values that act on it and not the others
//                (stick click weights, position spread, EDGE VOICE FROM BOW, tau_ref)
//                                                                    -> as expected
//
//   nova      click the CYMBAL box, then NOVA RIDE (the first)       -> preset nova_ride
//   smoke     click the CYMBAL box, then SMOKE FLAT RIDE (the second) -> preset smoke_flat_ride
//
// Exit code 0 when every step passed.
//
//   CymbalisUiTest hits
//
// runs no plugin: it times Cymbalis's own processor and editor, compiled into this tool
// (HitTiming.h): processBlock while a strike every 24 ms keeps 16 hits moving the cymbal,
// with the editor closed, open at 1536 x 1024 and idle, and open with the hits; the editor's
// paint calls and the cymbal's OpenGL frames.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <iterator>
#include <memory>
#include <vector>

#include "HitTiming.h"
#include "MacEvents.h"
#include "ShoulderProfiles.h"
#include "SlotTable.h"

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

// The tuning file the session holds for its current preset (a preset whose tuning differs from
// its bake's), parsed; void when it has none.
juce::var currentTuning (const juce::String& session)
{
    if (auto xml = juce::parseXML (session))
        for (auto* t : xml->getChildWithTagNameIterator ("Tuning"))
            if (t->getStringAttribute ("preset") == xml->getStringAttribute ("preset"))
                return juce::JSON::parse (t->getAllSubText());
    return {};
}

// The temporary tuning file of savetuning and loadtuning.
juce::File tuningFile()
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("CymbalisUiTest");
    dir.createDirectory();
    return dir.getChildFile ("ui_test.tuning.json");
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

// arcs: a knob (UI_SPEC section 3), its host parameter, and what each action did to both.
struct ArcKnob
{
    const char* name; // the parameter's name
    juce::Point<float> centre;
};
const ArcKnob kArcKnobs[] = { { "Velocity Sensitivity", { 487.0f, 881.0f } },
                              { "Volume", { 787.0f, 881.0f } },
                              { "Blend Position 1", { 1349.0f, 642.0f } },
                              { "Blend Position 2", { 1350.0f, 881.0f } },
                              { "Pan", { 1087.0f, 881.0f } } };

struct ArcCheck
{
    juce::AudioProcessorParameter* param = nullptr;
    bool down = false; // dragged down (from above the middle) or up
    float def = 0.0f, start = 0.0f, dragged = 0.0f, reset = 0.0f, target = 0.0f, automated = 0.0f, restored = 0.0f;
    juce::String textStart, textDragged, textReset, textAutomated, textRestored;
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
        const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        const auto screen = display != nullptr ? display->totalArea : juce::Rectangle<int>();
        std::printf ("editor %d x %d, screen %d x %d\n", editor->getWidth(), editor->getHeight(), screen.getWidth(),
                     screen.getHeight());
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

    // An action that is run again at each tick until `done` holds or `ticks` ticks have
    // passed; `*waited` gets the ticks it took. For results that arrive asynchronously (the
    // file panels' answers), so that a slow machine is waited for rather than failed.
    std::function<void()> waitUntil (std::function<bool()> done, int ticks, std::shared_ptr<int> waited)
    {
        return [this, done, ticks, waited] {
            if (! done() && ++*waited < ticks)
                --actionIndex; // this action again at the next tick
        };
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

    // hihatplay: MIDI into the hosted plugin through its processBlock (the test host has no
    // audio device), on the session's MIDI channel and pedal CC. Renders in blocks of 512 at
    // 48 kHz; returns "played" or what did not sound.
    juce::String playHiHat()
    {
        std::unique_ptr<juce::XmlElement> xml (juce::parseXML (sessionNow()));
        if (xml == nullptr)
            return "no session";
        const int ch = xml->getIntAttribute ("midiChannel", 16), cc = xml->getIntAttribute ("pedalCc", 1);
        const int tipNote = xml->getIntAttribute ("note_hihat_BOW", -1), edgeNote = xml->getIntAttribute ("note_hihat_EDGE", -1);
        std::printf ("      channel %d, pedal CC %d, BOW note %d, EDGE note %d\n", ch, cc, tipNote, edgeNote);
        if (tipNote != 42 || edgeNote != 46)
            return "the hi-hat's notes are " + juce::String (tipNote) + " and " + juce::String (edgeNote) + ", not 42 and 46";
        constexpr int block = 512;
        constexpr double rate = 48000.0;
        juce::AudioBuffer<float> buffer (juce::jmax (2, plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels()), block);
        // Mean square of `seconds` rendered after `midi` (at the first sample), both channels.
        auto render = [&] (double seconds, const juce::MidiMessage* msg) {
            double sum = 0.0;
            long count = 0;
            const int blocks = juce::roundToInt (seconds * rate / block);
            for (int b = 0; b < blocks; ++b)
            {
                juce::MidiBuffer midi;
                if (b == 0 && msg != nullptr)
                    midi.addEvent (*msg, 0);
                buffer.clear();
                plugin->processBlock (buffer, midi);
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < block; ++i)
                        sum += double (buffer.getSample (c, i)) * buffer.getSample (c, i);
                count += 2 * block;
            }
            return sum / (double) juce::jmax (1L, count);
        };
        auto dB = [] (double ms) { return 10.0 * std::log10 (juce::jmax (ms, 1e-30)); };
        const auto tip = juce::MidiMessage::noteOn (ch, tipNote, (juce::uint8) 100);
        const auto edge = juce::MidiMessage::noteOn (ch, edgeNote, (juce::uint8) 100);
        const auto open = juce::MidiMessage::controllerEvent (ch, cc, 127), shut = juce::MidiMessage::controllerEvent (ch, cc, 0);
        render (0.5, &shut); // the plugin takes the HI-HAT cymbal its loader built; the pedal closed
        const double tipMs = render (0.3, &tip);
        render (2.5, nullptr);
        const double edgeMs = render (0.3, &edge);
        render (0.7, nullptr);
        const double closedTail = render (0.5, nullptr); // 1.0 .. 1.5 s after the closed EDGE
        render (2.0, nullptr);
        render (0.1, &open);
        render (1.0, &edge);
        const double openTail = render (0.5, nullptr);   // 1.0 .. 1.5 s after the open EDGE
        render (0.5, &shut); // closes the pedal on it: choked, and FOOT
        render (2.5, nullptr);
        render (0.3, &open); // open again with nothing ringing ...
        const double footMs = render (0.2, &shut);       // ... and closed: FOOT alone
        std::printf ("      BOW (TIP) %.1f dB, EDGE closed %.1f dB, 1-1.5 s after: closed %.1f dB, open %.1f dB; FOOT %.1f dB\n",
                     dB (tipMs), dB (edgeMs), dB (closedTail), dB (openTail), dB (footMs));
        if (! (tipMs > 1e-8))
            return "BOW (TIP) is silent";
        if (! (edgeMs > 1e-8))
            return "EDGE is silent";
        if (! (openTail > 100.0 * closedTail))
            return "the open EDGE does not ring on 20 dB above the closed one";
        if (! (footMs > 1e-8))
            return "the pedal closing plays no FOOT";
        return "played";
    }

    // hihatpedal: the pedal CC at `value` through the hosted plugin's processBlock, on the
    // session's MIDI channel and pedal CC, then 1 s rendered (the engine smooths it over 10 ms).
    juce::String sendPedal (int value)
    {
        std::unique_ptr<juce::XmlElement> xml (juce::parseXML (sessionNow()));
        if (xml == nullptr)
            return "no session";
        const int ch = xml->getIntAttribute ("midiChannel", 16), cc = xml->getIntAttribute ("pedalCc", 1);
        constexpr int block = 512;
        juce::AudioBuffer<float> buffer (juce::jmax (2, plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels()), block);
        for (int b = 0; b < juce::roundToInt (48000.0 / block); ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0)
                midi.addEvent (juce::MidiMessage::controllerEvent (ch, cc, value), 0);
            buffer.clear();
            plugin->processBlock (buffer, midi);
        }
        return {};
    }

    // pedalchannel: the plugin's output followed block by block, counting the sounds it starts
    // (a block above -90 dB after at least 5 blocks below -110 dB) and in which step of the sweep.
    struct Onsets
    {
        int quietBlocks = 0;
        std::vector<int> steps;
        void add (const juce::AudioBuffer<float>& b, int numSamples, int step)
        {
            double sum = 0.0;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < numSamples; ++i)
                    sum += double (b.getSample (c, i)) * b.getSample (c, i);
            const double dB = 10.0 * std::log10 (juce::jmax (sum / (2.0 * numSamples), 1e-30));
            if (dB > -90.0 && quietBlocks >= 5)
                steps.push_back (step);
            quietBlocks = dB < -110.0 ? quietBlocks + 1 : 0;
        }
    };

    // pedalchannel: the pedal CC `cc` on MIDI channel `midiChannel`, ramped from `from` to `to`
    // over 0.25 s (one value a block) and held 0.25 s (from < 0: 2 s with no CC), through the
    // hosted plugin's processBlock, its output followed by `onsets` as sweep step `step`.
    void rampPedal (int midiChannel, int cc, int from, int to, Onsets& onsets, int step)
    {
        constexpr int block = 512, ramp = 23; // 0.25 s at 48 kHz
        juce::AudioBuffer<float> buffer (juce::jmax (2, plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels()), block);
        const int blocks = from < 0 ? 4 * 2 * ramp : 2 * ramp;
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer midi;
            if (from >= 0 && b < ramp)
                midi.addEvent (juce::MidiMessage::controllerEvent (midiChannel, cc, (int) std::lround (from + (to - from) * (b + 1) / (double) ramp)), 0);
            buffer.clear();
            plugin->processBlock (buffer, midi);
            onsets.add (buffer, block, step);
        }
    }

    // The saved session settings, through the host API.
    juce::String sessionNow()
    {
        juce::MemoryBlock state;
        plugin->getStateInformation (state);
        return findSession (state);
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
        else if (name == "sticks")
        {
            // A new instance: in every preset BELL and BOW start on TIP, BELL2 and BOW2 on
            // SHOULDER (UI_SPEC section 8). NOVA RIDE's keys have no preset id (stick_BELL). Only
            // rows with a TIP / SHOULDER choice have one (none on HI-HAT).
            s.actions = { wait };
            s.key = "sticks";
            s.expected = "defaults";
            s.probe = [this] {
                juce::String wrong;
                std::unique_ptr<juce::XmlElement> xml (juce::parseXML (sessionNow()));
                if (xml == nullptr)
                    return juce::String ("no session");
                for (int k = 0; k < cymbalis::kNumPresets; ++k)
                    for (const auto& [row, slot] : { std::pair<const char*, int> { "BELL", 0 }, { "BOW", 1 }, { "BELL2", 3 }, { "BOW2", 4 } })
                    {
                        if (! cymbalis::hasStickChoice (k, slot))
                            continue;
                        const auto key = juce::String ("stick_") + (k == 0 ? juce::String() : juce::String (cymbalis::kPresets[(size_t) k].id) + "_") + row;
                        const juce::String expected = juce::String (row).endsWith ("2") ? "SHOULDER" : "TIP";
                        const auto value = xml->getStringAttribute (key, "?");
                        if (value != expected)
                            wrong << " " << key << "=" << value;
                    }
                return wrong.isEmpty() ? juce::String ("defaults") : "not the defaults:" + wrong;
            };
        }
        else if (name == "list")
        {
            s.actions = { click (1457.0f, 392.0f), wait, click (1420.0f, listRowBelowCurrentY (405.0f)), wait };
            s.key = "note_BELL2";
            s.expected = "62";
        }
        else if (name == "stick")
        {
            // The TIP / SHOULDER popup below the stick box: SHOULDER second (NOVA's BELL starts on TIP).
            s.actions = { click (1314.0f, 152.0f), wait, click (1314.0f, 165.0f + 3.0f + 8.0f + 32.0f + 16.0f), wait };
            s.key = "stick_BELL";
            s.expected = "SHOULDER";
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
            // The CYMBALIS title in the background (PluginEditor.cpp kTitleInk: 66, 97, 339 x 36),
            // at its middle.
            s.actions = { click (235.5f, 115.0f), wait, wait };
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
                             const juce::Point<float> points[] = { { 487.0f, 881.0f }, { 787.0f, 881.0f }, { 1087.0f, 881.0f }, { 1349.0f, 642.0f },
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
        else if (name == "arcs")
        {
            // Each knob's arc is drawn from the knob's position (PluginEditor.cpp Knob::paint):
            // dragging it, double-clicking it (back to its default) and automating its parameter
            // through the host must each move both the parameter and the value the knob shows,
            // read through its accessibility element. The Tuning window must be closed (it can
            // cover the knobs). Each action is read back at the next tick.
            auto checks = std::make_shared<std::vector<ArcCheck>> (std::size (kArcKnobs));
            for (size_t k = 0; k < std::size (kArcKnobs); ++k)
            {
                const auto c = kArcKnobs[k];
                s.actions.push_back ([this, checks, k, c] {
                    auto& a = (*checks)[k];
                    for (auto* p : plugin->getParameters())
                        if (p->getName (64) == c.name)
                            a.param = p;
                    if (a.param == nullptr)
                        return;
                    a.def = a.param->getDefaultValue();
                    a.start = a.param->getValue();
                    a.textStart = cymbalis::uitest::sliderText (*editor, at (c.centre.x, c.centre.y));
                    a.down = a.start > 0.5f;
                    const float dy = a.down ? 40.0f : -40.0f; // design units: 40 / 300 of the knob's range
                    cymbalis::uitest::drag (*editor, at (c.centre.x, c.centre.y), at (c.centre.x, c.centre.y + dy));
                });
                s.actions.push_back ([this, checks, k, c] {
                    auto& a = (*checks)[k];
                    if (a.param == nullptr)
                        return;
                    a.dragged = a.param->getValue();
                    a.textDragged = cymbalis::uitest::sliderText (*editor, at (c.centre.x, c.centre.y));
                    cymbalis::uitest::doubleClick (*editor, at (c.centre.x, c.centre.y));
                });
                s.actions.push_back ([this, checks, k, c] {
                    auto& a = (*checks)[k];
                    if (a.param == nullptr)
                        return;
                    a.reset = a.param->getValue();
                    a.textReset = cymbalis::uitest::sliderText (*editor, at (c.centre.x, c.centre.y));
                    a.target = a.def > 0.5f ? 0.25f : 0.75f;
                    a.param->setValueNotifyingHost (a.target);
                });
                s.actions.push_back ([this, checks, k, c] {
                    auto& a = (*checks)[k];
                    if (a.param == nullptr)
                        return;
                    a.automated = a.param->getValue();
                    a.textAutomated = cymbalis::uitest::sliderText (*editor, at (c.centre.x, c.centre.y));
                    a.param->setValueNotifyingHost (a.def);
                });
                s.actions.push_back ([this, checks, k, c] {
                    auto& a = (*checks)[k];
                    if (a.param == nullptr)
                        return;
                    a.restored = a.param->getValue();
                    a.textRestored = cymbalis::uitest::sliderText (*editor, at (c.centre.x, c.centre.y));
                });
            }
            s.key = "the five knobs";
            s.expected = "followed";
            s.probe = [checks] {
                juce::String wrong;
                for (size_t k = 0; k < checks->size(); ++k)
                {
                    const auto& a = (*checks)[k];
                    const juce::String knob = kArcKnobs[k].name;
                    if (a.param == nullptr)
                    {
                        wrong << " no parameter \"" << knob << "\";";
                        continue;
                    }
                    auto shown = [] (float v, const juce::String& t) { return juce::String (v, 3) + " \"" + t + "\""; };
                    std::printf ("      %s: %s | drag %s %s | double-click %s | automation %s | back %s\n", knob.toRawUTF8(),
                                 shown (a.start, a.textStart).toRawUTF8(), a.down ? "down" : "up",
                                 shown (a.dragged, a.textDragged).toRawUTF8(), shown (a.reset, a.textReset).toRawUTF8(),
                                 shown (a.automated, a.textAutomated).toRawUTF8(), shown (a.restored, a.textRestored).toRawUTF8());
                    auto near = [] (float x, float y) { return std::abs (x - y) < 0.002f; };
                    auto readable = [] (const juce::String& t) { return t.isNotEmpty() && ! t.startsWith ("no "); };
                    if (! readable (a.textStart))
                        wrong << " " << knob << ": its value not readable (" << a.textStart << ");";
                    const float moved = a.dragged - a.start;
                    if ((a.down ? -moved : moved) < 0.05f || a.textDragged == a.textStart)
                        wrong << " " << knob << ": the drag did not move it;";
                    if (! near (a.reset, a.def) || a.textReset == a.textDragged || (near (a.start, a.def) && a.textReset != a.textStart))
                        wrong << " " << knob << ": the double-click did not return it to its default;";
                    if (! near (a.automated, a.target) || a.textAutomated == a.textReset)
                        wrong << " " << knob << ": the automation did not move it;";
                    if (! near (a.restored, a.def) || a.textRestored != a.textReset)
                        wrong << " " << knob << ": the automation back to its default did not move it back;";
                }
                return wrong.isEmpty() ? juce::String ("followed") : "not followed:" + wrong;
            };
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
        else if (name == "voice")
        {
            s.actions = { [this] { actionError = cymbalis::uitest::clickTuningSlider (*editor, "EDGE VOICE FROM BOW", 0.25f); },
                          wait, wait };
            s.key = "edge_voice_from_bow";
            s.expected = "moved";
            s.probe = [this] {
                if (actionError.isNotEmpty())
                    return actionError;
                const auto t = currentTuning (sessionNow());
                const auto& v = t["effective"]["cymbal"]["edge_voice_from_bow"];
                if (v.isVoid())
                    return juce::String ("no edge_voice_from_bow in the preset's tuning");
                std::printf ("      edge_voice_from_bow %s %%\n", v.toString().toRawUTF8());
                return (double) v > 10.0 && (double) v < 60.0 ? juce::String ("moved") : "at " + v.toString();
            };
        }
        else if (name == "clickweights")
        {
            // The first articulation's rows come first in the list (scrollToShow starts at the top).
            for (const char* row : { "STICK CLICK bow weight", "STICK CLICK bell weight", "STICK CLICK edge weight" })
            {
                s.actions.push_back ([this, row] {
                    if (actionError.isEmpty())
                        actionError = cymbalis::uitest::clickTuningSlider (*editor, row, 0.25f);
                });
                s.actions.push_back (wait);
            }
            s.actions.push_back (wait);
            s.key = "stick click weights";
            s.expected = "moved";
            s.probe = [this] {
                if (actionError.isNotEmpty())
                    return actionError;
                const auto t = currentTuning (sessionNow());
                auto* arts = t["effective"]["articulations"].getDynamicObject();
                if (arts == nullptr || arts->getProperties().isEmpty())
                    return juce::String ("no articulations in the preset's tuning");
                const auto first = arts->getProperties().getName (0).toString();
                const auto& a = arts->getProperties().getValueAt (0);
                juce::String values;
                bool moved = true;
                for (const char* key : { "stick_bow_weight", "stick_bell_weight", "stick_edge_weight" })
                {
                    const auto& v = a[key];
                    values << " " << key << " " << v.toString();
                    moved = moved && ! v.isVoid() && (double) v > 0.1 && (double) v < 0.6;
                }
                std::printf ("      %s:%s\n", first.toRawUTF8(), values.toRawUTF8());
                return moved ? juce::String ("moved") : first + ":" + values;
            };
        }
        else if (name == "hitvariation")
        {
            s.actions = { [this] {
                             hitBefore = currentTuning (sessionNow())["effective"]["cymbal"]["hit_variation"];
                             actionError = cymbalis::uitest::clickTuningButton (*editor, "HIT VARIATION (off: every strike plays as without it)", true);
                         },
                          wait,
                          [this] {
                              if (actionError.isEmpty())
                                  actionError = cymbalis::uitest::clickTuningSlider (*editor, "POSITION SPREAD", 0.5f);
                          },
                          wait,
                          [this] {
                              if (actionError.isEmpty())
                                  actionError = cymbalis::uitest::clickTuningSlider (*editor, "VELOCITY SPREAD", 0.5f);
                          },
                          wait, wait };
            s.key = "hit variation";
            s.expected = "moved";
            s.probe = [this] {
                if (actionError.isNotEmpty())
                    return actionError;
                if (! hitBefore.isBool())
                    return juce::String ("no hit_variation in the preset's tuning before the clicks");
                const auto c = currentTuning (sessionNow())["effective"]["cymbal"];
                const auto& on = c["hit_variation"];
                const auto& ps = c["hit_position_spread"];
                const auto& vs = c["hit_velocity_spread_pct"];
                const auto values = "hit_variation " + hitBefore.toString() + " -> " + on.toString() + ", hit_position_spread "
                                    + ps.toString() + ", hit_velocity_spread_pct " + vs.toString();
                std::printf ("      %s\n", values.toRawUTF8());
                const bool moved = on.isBool() && (bool) on != (bool) hitBefore && ! ps.isVoid() && (double) ps > 0.06
                                   && (double) ps <= 0.15 && ! vs.isVoid() && (double) vs > 6.0 && (double) vs <= 15.0;
                return moved ? juce::String ("moved") : values;
            };
        }
        else if (name == "profile")
        {
            s.actions = { [this] { actionError = cymbalis::uitest::clickTuningButton (*editor, "NOVA SHOULDER PROFILE", true); },
                          wait, wait };
            s.key = "bow_shoulder";
            s.expected = "bow profile";
            s.probe = [this] {
                if (actionError.isNotEmpty())
                    return actionError;
                const auto a = currentTuning (sessionNow())["effective"]["articulations"]["bow_shoulder"];
                const auto& p = cymbalis::tuning::kNovaShoulderBow;
                auto same = [] (const juce::var& v, const std::array<double, cymbalis::kExcitationBands>& want) {
                    const auto* arr = v.getArray();
                    if (arr == nullptr || arr->size() != (int) want.size())
                        return false;
                    for (int j = 0; j < arr->size(); ++j)
                        if (std::abs ((double) (*arr)[j] - want[(size_t) j]) > 1e-9)
                            return false;
                    return true;
                };
                const bool bands = same (a["band_gains_db"], p.bandDb), attack = same (a["attack_band_gains_db"], p.attackDb),
                           soft = same (a["attack_band_gains_db_soft"], p.attackDb);
                const bool time = ! a["attack_time_ms"].isVoid() && std::abs ((double) a["attack_time_ms"] - p.attackMs) < 1e-9;
                std::printf ("      bow_shoulder: band gains %s, attack gains %s / %s, attack time %s ms\n", bands ? "bow profile" : "other",
                             attack ? "bow profile" : "other", soft ? "bow profile" : "other", a["attack_time_ms"].toString().toRawUTF8());
                return bands && attack && soft && time ? juce::String ("bow profile") : juce::String ("not the bow profile");
            };
        }
        else if (name == "noprofile")
        {
            s.actions = { [this] { actionError = cymbalis::uitest::tuningShows (*editor, "NOVA SHOULDER PROFILE"); }, wait };
            s.key = "NOVA SHOULDER PROFILE";
            s.expected = "none";
            s.probe = [this] { return actionError; };
        }
        else if (name == "savetuning")
        {
            // The panel answers asynchronously: wait for the file (up to about 10 s), with the
            // stand-in panels in place until then.
            auto waited = std::make_shared<int> (0);
            s.actions = { [this] {
                             tuningFile().deleteFile();
                             cymbalis::uitest::answerFilePanels (tuningFile());
                             actionError = cymbalis::uitest::clickTuningButton (*editor, "SAVE...", false);
                         },
                          waitUntil ([this] { return actionError.isNotEmpty() || tuningFile().existsAsFile(); }, 30, waited), wait };
            s.key = "tuning file";
            s.expected = "saved";
            s.probe = [this, waited] {
                cymbalis::uitest::endFilePanels();
                std::printf ("      the file after %d tick(s) of 350 ms\n", *waited);
                if (actionError.isNotEmpty())
                    return actionError;
                if (! tuningFile().existsAsFile())
                    return juce::String ("no file written");
                const auto saved = juce::JSON::parse (tuningFile());
                const auto now = currentTuning (sessionNow());
                if (now.isVoid() || saved["preset"] != now["preset"]
                    || juce::JSON::toString (saved["effective"]) != juce::JSON::toString (now["effective"]))
                    return juce::String ("the file differs from the session's tuning");
                return juce::String ("saved");
            };
        }
        else if (name == "loadtuning")
        {
            // As savetuning: wait for the file's tuning to arrive (up to about 10 s).
            auto waited = std::make_shared<int> (0);
            auto loaded = [this] {
                const auto now = currentTuning (sessionNow());
                return ! now.isVoid()
                       && juce::JSON::toString (juce::JSON::parse (tuningFile())["effective"]) == juce::JSON::toString (now["effective"]);
            };
            s.actions = { [this] { actionError = cymbalis::uitest::clickTuningButton (*editor, "RESET ALL", false); },
                          wait,
                          [this] {
                              // RESET ALL: the preset's tuning is its bake's again, so the session has none.
                              if (actionError.isEmpty() && ! currentTuning (sessionNow()).isVoid())
                                  actionError = "RESET ALL left the preset's tuning";
                              if (actionError.isNotEmpty())
                                  return;
                              cymbalis::uitest::answerFilePanels (tuningFile());
                              actionError = cymbalis::uitest::clickTuningButton (*editor, "LOAD...", false);
                          },
                          waitUntil ([this, loaded] { return actionError.isNotEmpty() || loaded(); }, 30, waited), wait };
            s.key = "tuning";
            s.expected = "loaded";
            s.probe = [this, waited] {
                cymbalis::uitest::endFilePanels();
                std::printf ("      the tuning after %d tick(s) of 350 ms\n", *waited);
                if (actionError.isNotEmpty())
                    return actionError;
                const auto saved = juce::JSON::parse (tuningFile());
                const auto now = currentTuning (sessionNow());
                if (now.isVoid() || juce::JSON::toString (saved["effective"]) != juce::JSON::toString (now["effective"]))
                    return juce::String ("the session's tuning is not the file's");
                return juce::String ("loaded");
            };
        }
        else if (name == "nova" || name == "smoke")
        {
            // The list below the CYMBAL box; NOVA RIDE is its first row, SMOKE FLAT RIDE its second.
            const float row = name == "nova" ? 0.0f : 1.0f;
            s.actions = { click (199.0f, 402.0f), wait, click (199.0f, 433.0f + 4.0f + 8.0f + row * 40.0f + 20.0f), wait };
            s.key = "preset";
            s.expected = name == "nova" ? "nova_ride" : "smoke_flat_ride";
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
        else if (name == "hihat")
        {
            // The same list; HI-HAT is its seventh row.
            s.actions = { click (199.0f, 402.0f), wait, click (199.0f, 433.0f + 4.0f + 8.0f + 6.0f * 40.0f + 20.0f), wait };
            s.key = "preset";
            s.expected = "hihat";
        }
        else if (name == "hihatplay")
        {
            s.actions = { wait };
            s.key = "hi-hat";
            s.expected = "played";
            s.probe = [this] { return playHiHat(); };
        }
        else if (name == "hihatpedal")
        {
            // Each value held, then read a tick later: the editor follows the pedal on its timer.
            auto seen = std::make_shared<std::vector<std::pair<int, juce::String>>>();
            for (int value : { 0, 64, 127, 0 })
            {
                s.actions.push_back ([this, value] {
                    if (actionError.isEmpty())
                        actionError = sendPedal (value);
                });
                s.actions.push_back ([this, seen, value] {
                    seen->push_back ({ value, cymbalis::uitest::elementText (*editor, "HI-HAT") + " | "
                                                  + cymbalis::uitest::elementText (*editor, "HI-HAT PEDAL") });
                });
            }
            s.key = "hats and pedal";
            s.expected = "followed";
            s.probe = [this, seen] {
                if (actionError.isNotEmpty())
                    return actionError;
                juce::String wrong;
                for (const auto& [value, text] : *seen)
                {
                    std::printf ("      pedal CC %d: hats | pedal: %s\n", value, text.toRawUTF8());
                    const auto parts = juce::StringArray::fromTokens (text, "|", "");
                    bool ok = parts.size() == 2;
                    for (const auto& part : parts)
                    {
                        const auto t = part.trim();
                        ok = ok && t.startsWith ("open ") && std::abs (t.fromFirstOccurrenceOf ("open ", false, false).getDoubleValue() - value / 127.0) <= 0.01;
                    }
                    if (! ok)
                        wrong << " CC " << value << ": " << text << ";";
                }
                return wrong.isEmpty() ? juce::String ("followed") : "not followed:" + wrong;
            };
        }
        else if (name == "pedalchannel")
        {
            // MIDI CHANNEL 16: the grid's last item (4 rows of 4, items 61 x 40, inset 8, its left
            // 1514 - 260 = 1254, its top 69). Then the pedal CC (the session's: CC 1 before
            // pedalcc) on channel 1, 2 s of quiet first, then each step ramped and held; a tick
            // later the hats and the pedal are read (the editor follows on its timer).
            auto onsets = std::make_shared<Onsets>();
            auto seen = std::make_shared<std::vector<std::pair<int, juce::String>>>();
            auto setup = std::make_shared<juce::String>();
            auto cc = std::make_shared<int> (1);
            s.actions = { click (1459.0f, 48.0f), wait, click (1254.0f + 8.0f + 3.0f * 61.0f + 30.5f, 69.0f + 8.0f + 3.0f * 40.0f + 20.0f), wait,
                          [this, onsets, setup, cc] {
                              std::unique_ptr<juce::XmlElement> xml (juce::parseXML (sessionNow()));
                              const int ch = xml != nullptr ? xml->getIntAttribute ("midiChannel", -1) : -1;
                              *cc = xml != nullptr ? xml->getIntAttribute ("pedalCc", -1) : -1;
                              *setup = "MIDI CHANNEL " + juce::String (ch) + ", pedal CC " + juce::String (*cc) + " on channel 1";
                              rampPedal (1, *cc, -1, 0, *onsets, 0);
                          } };
            const int sweep[] = { 32, 64, 96, 127, 96, 64, 32, 0 };
            for (size_t k = 0; k < std::size (sweep); ++k)
            {
                const int from = k == 0 ? 0 : sweep[k - 1], to = sweep[k];
                s.actions.push_back ([this, onsets, cc, from, to, k] { rampPedal (1, *cc, from, to, *onsets, (int) k + 1); });
                s.actions.push_back ([this, seen, to] {
                    seen->push_back ({ to, cymbalis::uitest::elementText (*editor, "HI-HAT") + " | "
                                               + cymbalis::uitest::elementText (*editor, "HI-HAT PEDAL") });
                });
            }
            s.actions.push_back ([this, onsets, cc] { rampPedal (1, *cc, -1, 0, *onsets, 9); });
            s.key = "pedal CC on channel 1";
            s.expected = "followed, 1 FOOT";
            s.probe = [seen, onsets, setup] {
                std::printf ("      %s\n", setup->toRawUTF8());
                juce::String wrong;
                if (*setup != "MIDI CHANNEL 16, pedal CC 1 on channel 1")
                    wrong << " " << *setup << ";";
                for (const auto& [value, text] : *seen)
                {
                    std::printf ("      CC %d: hats | pedal: %s\n", value, text.toRawUTF8());
                    const auto parts = juce::StringArray::fromTokens (text, "|", "");
                    bool ok = parts.size() == 2;
                    for (const auto& part : parts)
                    {
                        const auto t = part.trim();
                        ok = ok && t.startsWith ("open ") && std::abs (t.fromFirstOccurrenceOf ("open ", false, false).getDoubleValue() - value / 127.0) <= 0.01;
                    }
                    if (! ok)
                        wrong << " CC " << value << ": " << text << ";";
                }
                juce::String at;
                for (int step : onsets->steps)
                    at << " " << step;
                std::printf ("      sounds started in sweep step(s):%s (8: the close to 0)\n", at.isEmpty() ? " none" : at.toRawUTF8());
                if (onsets->steps.size() != 1 || onsets->steps[0] != 8)
                    wrong << " FOOT: " << (int) onsets->steps.size() << " sound(s), in step(s)" << (at.isEmpty() ? juce::String (" none") : at) << ";";
                return wrong.isEmpty() ? juce::String ("followed, 1 FOOT") : "not as expected:" + wrong;
            };
        }
        else if (name == "pedalcc")
        {
            // The PEDAL box (PluginEditor.cpp kPedalBox: 1265, 380, 99 x 25). Its grid: 8 a row,
            // items 40 x 24, inset 8, 3 below the box (top 408), its left at x 1196 (clear of the
            // cymbal area). CC n is item n - 1.
            const auto item = [this] (int cc) {
                const int i = cc - 1;
                return click (1196.0f + 8.0f + 40.0f * (float) (i % 8) + 20.0f, 408.0f + 8.0f + 24.0f * (float) (i / 8) + 12.0f);
            };
            auto saved = std::make_shared<juce::MemoryBlock>();
            auto seen = std::make_shared<juce::StringArray>();
            const auto look = [this, seen] {
                seen->add (attribute (sessionNow(), "pedalCc") + " (" + cymbalis::uitest::elementText (*editor, "PEDAL CC") + ")");
            };
            s.actions = { click (1314.0f, 392.0f), wait, item (4), wait,
                          [this, saved, look] {
                              look();
                              plugin->getStateInformation (*saved);
                          },
                          click (1314.0f, 392.0f), wait, item (1), wait,
                          [this, saved, look] {
                              look();
                              plugin->setStateInformation (saved->getData(), (int) saved->getSize());
                          },
                          wait, look, wait };
            s.key = "pedal CC";
            s.expected = "4 (CC 4), 1 (CC 1), 4 (CC 4)";
            s.probe = [seen] { return seen->joinIntoString (", "); };
        }
        else if (name == "blendhidden" || name == "blendshown")
        {
            // The Blend Position knobs (UI_SPEC section 3): hidden on HI-HAT, so no slider at
            // their points; on the other presets each gives its value.
            s.actions = { wait };
            s.key = "Blend Position knobs";
            s.expected = name == "blendhidden" ? "hidden" : "shown";
            s.probe = [this] {
                const auto one = cymbalis::uitest::sliderText (*editor, at (1349.0f, 642.0f));
                const auto two = cymbalis::uitest::sliderText (*editor, at (1350.0f, 881.0f));
                const auto none = [] (const juce::String& t) { return t == "no slider at the point"; };
                if (none (one) && none (two))
                    return juce::String ("hidden");
                if (! none (one) && ! none (two) && ! one.startsWith ("no ") && ! two.startsWith ("no "))
                    return juce::String ("shown");
                return "Blend Position 1: " + one + ", 2: " + two;
            };
        }
        else if (name == "hihattuning")
        {
            s.actions = { wait };
            s.key = "hi-hat tuning";
            s.expected = "as expected";
            s.probe = [this] {
                const std::pair<const char*, const char*> rows[] = {
                    { "STICK CLICK gain (0 = none)", "shown" },
                    { "Kept onset (ring length acts after it): TIP, EDGE, FOOT", "shown" },
                    { "VELOCITY SPREAD", "shown" },
                    { "STICK CLICK bow weight", "none" },
                    { "POSITION SPREAD (0 edge, 0.5 bow, 1 bell)", "none" },
                    { "Bow brightness vs velocity (tau_ref): TIP", "none" },
                    { "EDGE VOICE FROM BOW (0: the edge's own modes, 100: its long-ringing modes take the bow's): EDGE, FOOT",
                      "none" },
                };
                for (const auto& r : rows)
                {
                    const auto got = cymbalis::uitest::tuningShows (*editor, r.first);
                    std::printf ("      \"%s\": %s\n", r.first, got.toRawUTF8());
                    if (got != r.second)
                        return juce::String (r.first) + ": " + got + " (expected " + r.second + ")";
                }
                return juce::String ("as expected");
            };
        }
        else if (name == "konstan")
        {
            // The same list; KONSTAN CRASH 18" is its sixth row.
            s.actions = { click (199.0f, 402.0f), wait, click (199.0f, 433.0f + 4.0f + 8.0f + 5.0f * 40.0f + 20.0f), wait };
            s.key = "preset";
            s.expected = "konstan";
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
            for (const auto& f : failures)
                std::printf ("failed: %s\n", f.toRawUTF8());
            std::printf ("%d of %d steps passed\n", passed, (int) steps.size());
            setApplicationReturnValue (passed == (int) steps.size() ? 0 : 1);
            quit();
            return;
        }
        // A window that click() moved aside for the previous tick's click goes back to where
        // it was (on a small screen, the Tuning window: MacEvents.h).
        cymbalis::uitest::putBack();
        auto& step = steps[(size_t) stepIndex];
        if (actionIndex == 0)
            actionError = {};
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
        {
            // The session without its Tuning elements (each preset's tuning file, long), so that
            // the step's lines stay near the end of the log.
            juce::String brief = session;
            if (auto x = juce::parseXML (session))
            {
                while (auto* t = x->getChildByName ("Tuning"))
                    x->removeChildElement (t, true);
                brief = x->toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());
            }
            std::printf ("      session (without its tunings): %s\n", brief.toRawUTF8());
            failures.add (step.name + " " + step.key + " = " + (value.isEmpty() ? juce::String ("?") : value) + " (expected "
                          + step.expected + ")");
        }
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
    juce::String actionError;        // what failed in the current step's actions
    juce::var hitBefore;             // hitvariation: the switch before the click
    juce::StringArray failures;      // the failed steps, listed again at the end
};

START_JUCE_APPLICATION (UiTestApp)
