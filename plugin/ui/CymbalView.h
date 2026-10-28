#pragma once

// The wireframe cymbal (docs/UI_SPEC.md section 11): the cymbal area of the editor, drawn over
// the background image and under every control. It is drawn on the GPU through OpenGL, on the
// context's own thread: the area's part of the background as a texture, then the mesh and the
// surface lines, the glow and the flashes. The static geometry is uploaded once per preset;
// per frame only the mesh's heights, slopes and glow and a few uniforms change. New frames are
// drawn only while a hit moves the cymbal (HitTracker asks for them at 30 Hz), and on a resize
// or a preset change. JUCE's software renderer never draws the moving cymbal, and the message
// thread paints nothing per frame.
//
// Without OpenGL (no context, or its shaders fail) the cymbal is drawn at rest with JUCE's
// drawing, once per preset or size: no motion, no glow. Hits then show only as the lit row.

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
    std::shared_ptr<const wire::Geometry> geometry() const { return shownGeometry; }

    // The hits to show (message thread): with OpenGL, a new frame is drawn with them.
    void setHits (const wire::Hit* hits, int count);

    // Whether OpenGL draws the cymbal now. Its counts since it was made or reset (for
    // tools/cymbalis_uitest): the frames drawn and the render thread's time for them, the times
    // it was given hits; and how many hits it shows now.
    bool usesOpenGL() const noexcept { return glActive.load(); }
    struct Stats
    {
        int frames = 0;
        double totalMs = 0.0, worstMs = 0.0;
        int updates = 0, hits = 0;
    };
    Stats stats() const noexcept;
    void resetStats() noexcept;

    // Off: the cymbal is always drawn still with JUCE's drawing (tools/cymbalis_snapshot,
    // which cannot capture OpenGL).
    static void setOpenGLAllowed (bool allowed) noexcept;

    void paint (juce::Graphics& g) override;

private:
    struct Gpu;

    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;
    void timerCallback() override;

    void attachOpenGL();
    void detachOpenGL();
    juce::Image renderStill (float scale) const;

    const Assets& assets;
    juce::Image background; // the area's part of the background image
    std::shared_ptr<const wire::Geometry> shownGeometry;
    int shownPreset = -1;
    juce::String error;

    // The still drawing (no OpenGL), at the physical scale it was made for.
    juce::Image still;
    float stillScale = 0.0f;
    const wire::Geometry* stillGeometry = nullptr;

    // OpenGL. The geometry and hits for the render thread, under `lock`.
    juce::OpenGLContext context;
    std::atomic<bool> glActive { false }, glStarted { false }, glCreated { false }, glFailed { false };
    bool attached = false;
    double attachedMs = 0.0;
    juce::SpinLock lock;
    std::shared_ptr<const wire::Geometry> pendingGeometry;
    std::array<wire::Hit, wire::kMaxHits> pendingHits {};
    int numPendingHits = 0;
    std::unique_ptr<Gpu> gpu; // render thread only

    mutable juce::SpinLock statsLock;
    Stats counts; // under statsLock

    static std::atomic<bool> openGLAllowed;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CymbalView)
};

} // namespace cymbalis::ui
