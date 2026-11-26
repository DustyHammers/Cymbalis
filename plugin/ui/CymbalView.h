#pragma once

// The wireframe cymbal (docs/UI_SPEC.md section 11): the cymbal area of the editor, drawn over
// the background image and under every control. It is drawn on the GPU through OpenGL, on the
// context's own thread: the area's part of the background as a texture, then the lines (the mesh
// and the surface lines, combined in a layer of their own as the reference does), the hardware on
// the bell (the felt, washer and wing nut, CymbalHardware.h; the hi-hat's clutch and rod), the glow
// and the flashes; on the presets with a sizzle chain, the chain while it is on (ChainDrawing.h).
// The static geometry is uploaded once per preset;
// per frame only the mesh's heights, slopes and glow and a few uniforms change. New frames are
// drawn only while a hit moves the cymbal (HitTracker asks for them at 30 Hz), and on a resize
// or a preset change. JUCE's software renderer never draws the moving cymbal, and the message
// thread paints nothing per frame.
//
// Without OpenGL (no context, or its shaders fail) the cymbal is drawn at rest with JUCE's
// drawing, once per preset or size: no motion, no glow. Hits then show only as the lit row.

#include "ChainDrawing.h"
#include "CymbalHardware.h"
#include "CymbalModel.h"
#include "UiAssets.h"

#include <juce_opengl/juce_opengl.h>

#include <atomic>
#include <memory>

namespace cymbalis::ui
{

class CymbalView final : public juce::Component, private juce::OpenGLRenderer, private juce::Timer
{
public:
    explicit CymbalView (const Assets& assets);
    ~CymbalView() override;

    // The cymbal area, design units: 400,40 to 1180,750, inside the right column's divider
    // (x 1188) and above the bottom row's (y 757).
    static juce::Rectangle<int> area() { return { 400, 40, 780, 710 }; }

    // The preset's cymbal; with a load error, the error instead.
    void setPreset (int preset, const juce::String& error);
    // The cymbal the hits strike (the hi-hat's top cymbal), and the hi-hat's bottom cymbal (else
    // none).
    std::shared_ptr<const wire::Geometry> geometry() const { return shownGeometry; }
    std::shared_ptr<const wire::Geometry> lowerGeometry() const { return shownLower; }

    // The hi-hat's opening, 0 closed to 1 open (the pedal's smoothed openness): its top cymbal,
    // clutch and rod rise by opening x 0.7" (message thread). A new frame is drawn with it.
    void setOpening (float opening);
    float opening() const noexcept { return shownOpening; }

    // The hits to show (message thread): with OpenGL, a new frame is drawn with them.
    void setHits (const wire::Hit* hits, int count);

    // The sizzle chain (message thread; the editor calls it on its timer, on the presets with a
    // chain): drawn while `on`, its beads lifted with `envelope`, the sound's chain envelope
    // smoothed by the editor (0: at rest; at most 1.5). With OpenGL a new frame is drawn while the
    // envelope is above 0 (unless hits are moving the cymbal: their frames show it), and once when
    // it falls to 0 or the switch changes; the still drawing shows the chain at rest while on.
    void setChain (bool on, float envelope);

    // Whether OpenGL draws the cymbal now. Its counts since it was made or reset (for
    // tools/cymbalis_uitest): the frames drawn and the render thread's time for them, the times
    // it was given hits; and how many hits it shows now.
    bool usesOpenGL() const noexcept { return glActive.load(); }
    struct Stats
    {
        int frames = 0;
        double totalMs = 0.0, worstMs = 0.0;
        int updates = 0, hits = 0;
        // The last frame's wobble (the hits' tilt, its size): the share drawn (the hi-hat's 0.15 +
        // 0.85 x its opening; else 1), the top cymbal's drawn and its full tilt, and the hi-hat's
        // bottom cymbal's (always 0: it stays still).
        double wobble = 1.0, tiltDrawn = 0.0, tiltFull = 0.0, tiltBottom = 0.0;
    };
    Stats stats() const noexcept;
    void resetStats() noexcept;
    juce::String renderer() const; // the OpenGL renderer's name, once a context has started

    // Off: the cymbal is always drawn still with JUCE's drawing (tools/cymbalis_snapshot,
    // which cannot capture OpenGL).
    static void setOpenGLAllowed (bool allowed) noexcept;

    void paint (juce::Graphics& g) override;

private:
    struct Gpu;
    class Accessibility;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
    // On the hi-hat, for screen readers (and tools/cymbalis_uitest): its opening, and the last
    // frame's wobble (Stats).
    juce::String hiHatDescription() const;

    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;
    void timerCallback() override;
    // Hidden (the editor's TUNING page), JUCE closes the OpenGL context and nothing renders;
    // the watch on the context's start stops, and starts again when the view shows.
    void visibilityChanged() override;

    void attachOpenGL();
    void detachOpenGL();
    juce::Image renderStill (float scale) const;

    const Assets& assets;
    juce::Image background; // the area's part of the background image
    std::shared_ptr<const wire::Geometry> shownGeometry, shownLower;
    // The felt, washer and wing nut on the bell (every preset but the hi-hat), built with the geometry.
    std::shared_ptr<const wire::CymbalHardware> shownHardware;
    // The sizzle chain, built for the preset the first time it is switched on; whether it is on
    // and its envelope, as last given.
    std::shared_ptr<const wire::ChainDrawing> shownChain;
    bool shownChainOn = false;
    float shownEnvelope = 0.0f;
    int shownPreset = -1;
    float shownOpening = 0.0f;
    juce::String error;

    // The still drawing (no OpenGL), at the physical scale, opening and chain it was made for.
    juce::Image still;
    float stillScale = 0.0f, stillOpening = -1.0f;
    bool stillChain = false;
    const wire::Geometry* stillGeometry = nullptr;

    // OpenGL. The geometry and hits for the render thread, under `lock`.
    juce::OpenGLContext context;
    std::atomic<bool> glActive { false }, glStarted { false }, glCreated { false }, glFailed { false };
    bool attached = false;
    double attachedMs = 0.0;
    juce::SpinLock lock;
    std::shared_ptr<const wire::Geometry> pendingGeometry, pendingLower;
    std::shared_ptr<const wire::CymbalHardware> pendingHardware;
    std::shared_ptr<const wire::ChainDrawing> pendingChain; // none while the chain is off
    float pendingEnvelope = 0.0f;
    std::array<wire::Hit, wire::kMaxHits> pendingHits {};
    int numPendingHits = 0;
    float pendingOpening = 0.0f;
    std::unique_ptr<Gpu> gpu; // render thread only

    mutable juce::SpinLock statsLock;
    Stats counts;            // under statsLock
    juce::String glRenderer; // under statsLock

    static std::atomic<bool> openGLAllowed;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CymbalView)
};

} // namespace cymbalis::ui
