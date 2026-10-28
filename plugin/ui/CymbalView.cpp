#include "ui/CymbalView.h"

#include <algorithm>
#include <cmath>

namespace cymbalis::ui
{

using namespace juce::gl;

std::atomic<bool> CymbalView::openGLAllowed { true };

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr float kGlowHalf = 9.0f;          // the glow's half width: 3 sigma, sigma 3 design units
constexpr float kGlowPerUnit = 0.0665f;    // its peak per unit of line width: 0.5 / (3 sqrt (2 pi))
constexpr double kStartTimeoutMs = 3000.0; // no OpenGL context started by then: the still drawing
constexpr int kMaxRings = 32;              // the most mesh rings the shaders take (the shapes have 28 or 29)

juce::String vec3Of (wire::Rgb c)
{
    return "vec3 (" + juce::String (c.r, 6) + ", " + juce::String (c.g, 6) + ", " + juce::String (c.b, 6) + ")";
}

// The colours and the reference's colour(), for the shaders.
juce::String bronzeGlsl()
{
    using namespace wire::colour;
    return "const vec3 kDark = " + vec3Of (roseDark) + ", kLight = " + vec3Of (roseLight) + ", kLathe = " + vec3Of (lathe)
           + ", kGold = " + vec3Of (gold) + ", kHot = " + vec3Of (hot) + ";\n"
           + R"(vec3 bronze (float shade, float ytl, bool lathe, float glow)
{
    float light = clamp (((shade - 0.86) / 0.14) * 0.8 + 0.2 * ytl, 0.0, 1.0);
    vec3 c = kDark + (kLight - kDark) * pow (0.25 + 0.75 * light, 1.4);
    if (lathe)
        c = c * 0.55 + kLathe * (0.30 + 0.35 * light);
    return c + (kHot - c) * glow;
}
)";
}

// The camera and the tilt and spin: a point at radius r, angle theta and height z, to design
// units on screen.
const char* const kProjectGlsl = R"(uniform vec2 uTilt;    // the wobble's tilt, Z -= 2.2 (X T0 + Y T1)
uniform float uSpin;   // radians
uniform vec3 uCam;     // sin and cos of the elevation, the perspective distance
uniform vec3 uCentre;  // the cymbal's centre on screen and its scale, design units
uniform vec2 uOrigin;  // this view's top-left, design units
uniform float uPx;     // pixels per design unit
uniform vec2 uY;       // the cymbal's top and bottom on screen, design units
vec2 project (float r, float theta, float z)
{
    float phi = theta + uSpin;
    float X = r * cos (phi), Y = r * sin (phi);
    float Z = z - 2.2 * (X * uTilt.x + Y * uTilt.y);
    float yv = Y * uCam.x - Z * uCam.y, zv = Y * uCam.y + Z * uCam.x;
    return uCentre.xy + uCentre.z * vec2 (X, yv) * (uCam.z / (uCam.z - zv));
}
float topToBottom (float sy) { return 1.0 - (sy - uY.x) / max (uY.y - uY.x, 1.0); }
)";

// A line segment as a quad in pixels, built in the vertex shader (no geometry shader: Apple's
// OpenGL runs those on the CPU): each segment is 4 vertices, each knowing both ends; `aCorner` is
// (0 at the first end or 1 at the second, -1 or 1 across). The quad reaches `uHalf` either side
// of the line and `uCap` past its ends (so that the joints of a ring close).
const char* const kQuadGlsl = R"(uniform vec2 uViewPx;
uniform float uHalf;
uniform float uCap;
in vec2 aCorner;
out vec3 vColour;
out float vAcross;
vec4 quadCorner (vec2 a, vec2 b)
{
    vec2 d = b - a;
    float len = length (d);
    vec2 dir = len > 1.0e-4 ? d / len : vec2 (1.0, 0.0);
    vec2 n = vec2 (-dir.y, dir.x) * uHalf, e = dir * uCap;
    vec2 q = (aCorner.x < 0.5 ? a - e : b + e) + n * aCorner.y;
    vAcross = aCorner.y;
    return vec4 (q.x / uViewPx.x * 2.0 - 1.0, 1.0 - q.y / uViewPx.y * 2.0, 0.0, 1.0);
}
)";

// The mesh: each segment's ends are mesh vertices, whose height, slopes and glow (per frame) are
// read from the state texture (column j, row i: ring i, sample j). Its colour from its ends'
// averages (the reference's col()). With uGlowGain above 0 it draws the segment's glow
// instead, only where the line is brighter than 150/255.
const char* const kMeshVertex = R"(in vec2 aEnds; // the ends' mesh vertex indices
uniform sampler2D uState;
uniform float uRings[kMaxRings];
uniform vec4 uLathe; // two lathe zones (from, to)
uniform float uGlowGain;
struct End
{
    vec2 p;
    float r, shade, sy, glow;
};
End meshEnd (float index)
{
    int k = int (index + 0.5), i = k / kSamples, j = k - kSamples * i;
    vec4 s = texelFetch (uState, ivec2 (j, i), 0);
    float th = 6.283185307 * float (j) / float (kSamples), phi = th + uSpin;
    End e;
    e.r = uRings[i];
    vec2 q = project (e.r, th, s.x);
    // The normal from the height field, with the tilt's slope (the reference's shade).
    float dzr = s.y - 2.2 * (cos (phi) * uTilt.x + sin (phi) * uTilt.y);
    float dzt = s.z - 2.2 * (-sin (phi) * uTilt.x + cos (phi) * uTilt.y);
    float nx = -(dzr * cos (th) - dzt * sin (th)), ny = -(dzr * sin (th) + dzt * cos (th));
    e.shade = clamp ((ny * -0.55 + 0.83) / sqrt (nx * nx + ny * ny + 1.0), 0.0, 1.0);
    e.p = (q - uOrigin) * uPx;
    e.sy = q.y;
    e.glow = s.w;
    return e;
}
void main()
{
    End a = meshEnd (aEnds.x), b = meshEnd (aEnds.y);
    float r = 0.5 * (a.r + b.r);
    bool lathe = (r >= uLathe.x && r <= uLathe.y) || (r >= uLathe.z && r <= uLathe.w);
    vec3 c = bronze (0.5 * (a.shade + b.shade), topToBottom (0.5 * (a.sy + b.sy)), lathe, 0.5 * (a.glow + b.glow));
    if (uGlowGain > 0.0)
    {
        if (max (c.r, max (c.g, c.b)) <= 150.0 / 255.0)
        {
            vColour = vec3 (0.0);
            vAcross = 0.0;
            gl_Position = vec4 (2.0, 2.0, 2.0, 1.0); // outside: nothing drawn
            return;
        }
        c *= uGlowGain;
    }
    vColour = c;
    gl_Position = quadCorner (a.p, b.p);
}
)";

// The surface lines: each segment's ends (radius, angle, height: the profile's, no flex) and its
// kind, colour from the ends' average.
const char* const kSurfaceVertex = R"(in vec3 aA;
in vec3 aB;
in vec3 aKind; // kind (0 hammer, 1 lathe, 2 groove), its factor, in a lathe zone
vec3 colourAt (vec3 e, out vec2 p)
{
    vec2 s = project (e.x, e.y, e.z);
    float ytl = topToBottom (s.y);
    p = (s - uOrigin) * uPx;
    if (aKind.x < 0.5)
        return bronze (0.9, ytl, aKind.z > 0.5, 0.0) * aKind.y;
    if (aKind.x < 1.5)
        return bronze (0.9, ytl, aKind.z > 0.5, 0.0) * (0.30 + 0.25 * ytl);
    return kGold * (0.30 + 0.32 * ytl) * aKind.y;
}
void main()
{
    vec2 pa, pb;
    vec3 ca = colourAt (aA, pa), cb = colourAt (aB, pb);
    vColour = 0.5 * (ca + cb);
    gl_Position = quadCorner (pa, pb);
}
)";

// A line, or its glow: Gaussian across, sigma a third of the half width.
const char* const kLineFragment = R"(in vec3 vColour;
in float vAcross;
uniform float uGlowGain;
out vec4 fragColour;
void main()
{
    float k = uGlowGain > 0.0 ? exp (-4.5 * vAcross * vAcross) : 1.0;
    fragColour = vec4 (vColour * k, 1.0);
}
)";

const char* const kBackgroundVertex = R"(in vec2 aPos; // 0 .. 1 from the top-left
out vec2 vUv;
void main()
{
    vUv = aPos;
    gl_Position = vec4 (aPos.x * 2.0 - 1.0, 1.0 - aPos.y * 2.0, 0.0, 1.0);
}
)";
const char* const kBackgroundFragment = R"(in vec2 vUv;
uniform sampler2D uTexture;
out vec4 fragColour;
void main()
{
    fragColour = vec4 (texture (uTexture, vUv).rgb, 1.0);
}
)";

// A flash: the disc blurred (wire::flashProfile), drawn out to 3 radii.
const char* const kFlashVertex = R"(in vec2 aPos; // -1 .. 1
uniform vec2 uCentrePx;
uniform float uExtentPx;
uniform vec2 uViewPx;
out vec2 vLocal;
void main()
{
    vLocal = aPos;
    vec2 q = uCentrePx + aPos * uExtentPx;
    gl_Position = vec4 (q.x / uViewPx.x * 2.0 - 1.0, 1.0 - q.y / uViewPx.y * 2.0, 0.0, 1.0);
}
)";
const char* const kFlashFragment = R"(in vec2 vLocal;
uniform vec3 uColour;
uniform float uProfile[49]; // at 0, 1/16 .. 3 radii
out vec4 fragColour;
void main()
{
    float x = 48.0 * length (vLocal);
    int i = int (x);
    float k = i < 48 ? mix (uProfile[i], uProfile[i + 1], x - float (i)) : 0.0;
    fragColour = vec4 (uColour * k, 1.0);
}
)";

// The lathe zones as a vec4 (an unused zone never matches).
std::array<float, 4> latheZones (const CymbalShape& s)
{
    std::array<float, 4> z { 2.0f, 2.0f, 2.0f, 2.0f };
    for (int k = 0; k < s.numLathe && k < 2; ++k)
    {
        z[(size_t) (2 * k)] = (float) s.lathe[(size_t) k].from;
        z[(size_t) (2 * k + 1)] = (float) s.lathe[(size_t) k].to;
    }
    return z;
}
} // namespace

//==============================================================================
struct CymbalView::Gpu
{
    std::unique_ptr<juce::OpenGLShaderProgram> mesh, surface, back, flash;
    GLuint meshVao = 0, meshVbo = 0, meshIndex = 0, stateTexture = 0;
    GLuint surfaceVao = 0, surfaceVbo = 0, surfaceIndex = 0;
    GLuint quadVao = 0, quadVbo = 0, flashVao = 0, flashVbo = 0;
    GLuint texture = 0;

    std::shared_ptr<const wire::Geometry> geometry;
    int meshSegments = 0, thinMeshSegments = 0, surfaceSegments = 0, thinSurfaceSegments = 0;
    wire::Frame frame;
    std::vector<float> disp, state;
    std::array<wire::Hit, wire::kMaxHits> hits {};
    int numHits = 0;

    static GLint at (const juce::OpenGLShaderProgram& p, const char* name)
    {
        return glGetUniformLocation (p.getProgramID(), name);
    }

    // A program from its shaders (GLSL 1.50, core profile), its attributes at the locations given.
    static std::unique_ptr<juce::OpenGLShaderProgram> build (juce::OpenGLContext& c, const juce::String& vertex, const juce::String& fragment,
                                                             std::initializer_list<const char*> attributes)
    {
        auto p = std::make_unique<juce::OpenGLShaderProgram> (c);
        const juce::String version = "#version 150\n";
        if (! p->addShader (version + vertex, GL_VERTEX_SHADER) || ! p->addShader (version + fragment, GL_FRAGMENT_SHADER))
        {
            DBG ("CymbalView: " << p->getLastError());
            return nullptr;
        }
        GLuint location = 0;
        for (auto* a : attributes)
            glBindAttribLocation (p->getProgramID(), location++, a);
        if (! p->link())
        {
            DBG ("CymbalView: " << p->getLastError());
            return nullptr;
        }
        return p;
    }

    // Two triangles per segment: its quad's corners 4q .. 4q + 3.
    static void uploadQuadIndices (GLuint buffer, int segments)
    {
        std::vector<std::uint32_t> ix;
        ix.reserve ((size_t) segments * 6);
        for (std::uint32_t q = 0; q < (std::uint32_t) segments; ++q)
            for (std::uint32_t c : { 0u, 1u, 2u, 2u, 1u, 3u })
                ix.push_back (4 * q + c);
        glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, buffer);
        glBufferData (GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr) (ix.size() * sizeof (std::uint32_t)), ix.data(), GL_STATIC_DRAW);
    }

    void upload (std::shared_ptr<const wire::Geometry> g)
    {
        geometry = std::move (g);
        if (geometry == nullptr)
            return;
        const auto& geo = *geometry;
        if (geo.numRings > kMaxRings)
        {
            jassertfalse; // more rings than the mesh shader's uRings holds
            geometry = nullptr;
            return;
        }
        frame.resize (geo);
        disp.assign ((size_t) geo.numVertices(), 0.0f);
        state.assign ((size_t) geo.numVertices() * 4, 0.0f);
        constexpr float corners[4][2] = { { 0.0f, -1.0f }, { 0.0f, 1.0f }, { 1.0f, -1.0f }, { 1.0f, 1.0f } };

        // The mesh: per quad corner, the corner and its segment's two mesh vertices.
        meshSegments = (int) geo.lineIndices.size() / 2;
        thinMeshSegments = geo.numThinIndices / 2;
        std::vector<float> quads;
        quads.reserve ((size_t) meshSegments * 16);
        for (int k = 0; k < meshSegments; ++k)
            for (const auto& c : corners)
                for (float v : { c[0], c[1], (float) geo.lineIndices[(size_t) (2 * k)], (float) geo.lineIndices[(size_t) (2 * k + 1)] })
                    quads.push_back (v);
        glBindVertexArray (meshVao);
        glBindBuffer (GL_ARRAY_BUFFER, meshVbo);
        glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (quads.size() * sizeof (float)), quads.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray (0);
        glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof (float), nullptr);
        glEnableVertexAttribArray (1);
        glVertexAttribPointer (1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof (float), (const void*) (2 * sizeof (float)));
        uploadQuadIndices (meshIndex, meshSegments);

        // The mesh's state per frame: one texel per vertex (kSamples x rings).
        glBindTexture (GL_TEXTURE_2D, stateTexture);
        glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA32F, wire::kSamples, geo.numRings, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture (GL_TEXTURE_2D, 0);
        std::vector<float> rings (geo.rings.begin(), geo.rings.end());
        mesh->use();
        glUniform1fv (at (*mesh, "uRings"), geo.numRings, rings.data());
        glUniform1i (at (*mesh, "uState"), 1);

        // The surface lines: per quad corner, the corner, both ends and the segment's kind.
        surfaceSegments = (int) geo.surface.size() / 2;
        thinSurfaceSegments = geo.numThinSurface / 2;
        std::vector<float> lines;
        lines.reserve ((size_t) surfaceSegments * 44);
        for (int k = 0; k < surfaceSegments; ++k)
        {
            const auto& a = geo.surface[(size_t) (2 * k)];
            const auto& b = geo.surface[(size_t) (2 * k + 1)];
            for (const auto& c : corners)
                for (float v : { c[0], c[1], a.r, a.theta, a.z, b.r, b.theta, b.z, a.kind, a.param, a.lathe })
                    lines.push_back (v);
        }
        glBindVertexArray (surfaceVao);
        glBindBuffer (GL_ARRAY_BUFFER, surfaceVbo);
        glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (lines.size() * sizeof (float)), lines.data(), GL_STATIC_DRAW);
        const auto stride = (GLsizei) (11 * sizeof (float));
        glEnableVertexAttribArray (0);
        glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, stride, nullptr);
        for (GLuint a = 1; a <= 3; ++a)
        {
            glEnableVertexAttribArray (a);
            glVertexAttribPointer (a, 3, GL_FLOAT, GL_FALSE, stride, (const void*) ((size_t) (2 + 3 * (a - 1)) * sizeof (float)));
        }
        uploadQuadIndices (surfaceIndex, surfaceSegments);
        glBindVertexArray (0);
        glUseProgram (0);
    }

    void release()
    {
        for (auto* vao : { &meshVao, &surfaceVao, &quadVao, &flashVao })
            if (*vao != 0)
                glDeleteVertexArrays (1, vao);
        for (auto* b : { &meshVbo, &meshIndex, &surfaceVbo, &surfaceIndex, &quadVbo, &flashVbo })
            if (*b != 0)
                glDeleteBuffers (1, b);
        for (auto* t : { &texture, &stateTexture })
            if (*t != 0)
                glDeleteTextures (1, t);
        mesh.reset();
        surface.reset();
        back.reset();
        flash.reset();
    }
};

//==============================================================================
CymbalView::CymbalView (const Assets& a) : assets (a)
{
    setOpaque (true);
    setInterceptsMouseClicks (false, false);
    if (assets.background.isValid())
        background = assets.background.getClippedImage (area()).convertedToFormat (juce::Image::ARGB).createCopy();
    if (openGLAllowed.load())
        attachOpenGL();
}

CymbalView::~CymbalView()
{
    stopTimer();
    detachOpenGL();
}

void CymbalView::setOpenGLAllowed (bool allowed) noexcept
{
    openGLAllowed = allowed;
}

void CymbalView::attachOpenGL()
{
    if (attached)
        return;
    juce::OpenGLPixelFormat format;
    format.multisamplingLevel = 4; // antialiased lines
    context.setPixelFormat (format);
    context.setMultisamplingEnabled (true);
    context.setOpenGLVersionRequired (juce::OpenGLContext::openGL3_2);
    context.setComponentPaintingEnabled (false);
    context.setContinuousRepainting (false);
    context.setRenderer (this);
    context.attachTo (*this);
    attached = true;
    attachedMs = juce::Time::getMillisecondCounterHiRes();
    startTimer (250); // watches the context start
}

void CymbalView::detachOpenGL()
{
    if (! attached)
        return;
    context.detach(); // closes the context on its thread (openGLContextClosing) and waits
    attached = false;
    glActive = false;
    glStarted = false;
    glCreated = false;
}

// Message thread: OpenGL takes over once its context and shaders are ready. Until then the still
// drawing shows; it stays when the shaders fail, or when no context has started kStartTimeoutMs
// after the view shows (JUCE reports no failure to create one). Compiling the shaders may take
// longer than that, so a context that has started is waited for.
void CymbalView::timerCallback()
{
    if (! attached)
    {
        stopTimer();
        return;
    }
    if (glFailed.load())
    {
        detachOpenGL();
        stopTimer();
        repaint();
        return;
    }
    if (glCreated.load())
    {
        glActive = true;
        stopTimer();
        context.triggerRepaint();
        return;
    }
    if (! isShowing() || glStarted.load())
        attachedMs = juce::Time::getMillisecondCounterHiRes();
    else if (juce::Time::getMillisecondCounterHiRes() - attachedMs > kStartTimeoutMs)
    {
        glFailed = true;
        detachOpenGL();
        stopTimer();
        repaint();
    }
}

void CymbalView::setPreset (int preset, const juce::String& loadError)
{
    if (preset != shownPreset)
    {
        shownPreset = preset;
        shownGeometry = std::make_shared<const wire::Geometry> (*kPresets[(size_t) preset].shape);
    }
    error = loadError;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        pendingGeometry = shownGeometry;
        numPendingHits = 0;
    }
    {
        const juce::SpinLock::ScopedLockType sl (statsLock);
        counts.hits = 0;
    }
    // A load error is drawn with JUCE's drawing, over the background.
    if (error.isNotEmpty())
        detachOpenGL();
    else if (openGLAllowed.load() && ! glFailed.load())
        attachOpenGL();
    repaint();
    if (glActive.load())
        context.triggerRepaint();
}

void CymbalView::setHits (const wire::Hit* hits, int count)
{
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        numPendingHits = juce::jlimit (0, wire::kMaxHits, count);
        std::copy (hits, hits + numPendingHits, pendingHits.begin());
    }
    {
        const juce::SpinLock::ScopedLockType sl (statsLock);
        ++counts.updates;
        counts.hits = juce::jlimit (0, wire::kMaxHits, count);
    }
    if (glActive.load())
        context.triggerRepaint();
}

juce::String CymbalView::renderer() const
{
    const juce::SpinLock::ScopedLockType sl (statsLock);
    return glRenderer;
}

CymbalView::Stats CymbalView::stats() const noexcept
{
    const juce::SpinLock::ScopedLockType sl (statsLock);
    return counts;
}

void CymbalView::resetStats() noexcept
{
    const juce::SpinLock::ScopedLockType sl (statsLock);
    counts = { 0, 0.0, 0.0, 0, counts.hits };
}

//==============================================================================
// Render thread.
void CymbalView::newOpenGLContextCreated()
{
    glStarted = true;
    {
        const juce::SpinLock::ScopedLockType sl (statsLock);
        glRenderer = juce::String::fromUTF8 ((const char*) glGetString (GL_RENDERER));
    }
    auto g = std::make_unique<Gpu>();
    const auto constants = "const int kSamples = " + juce::String (wire::kSamples) + ";\nconst int kMaxRings = " + juce::String (kMaxRings) + ";\n";
    const juce::String common = constants + bronzeGlsl() + kProjectGlsl + kQuadGlsl;
    g->mesh = Gpu::build (context, common + kMeshVertex, kLineFragment, { "aCorner", "aEnds" });
    g->surface = Gpu::build (context, common + kSurfaceVertex, kLineFragment, { "aCorner", "aA", "aB", "aKind" });
    g->back = Gpu::build (context, kBackgroundVertex, kBackgroundFragment, { "aPos" });
    g->flash = Gpu::build (context, kFlashVertex, kFlashFragment, { "aPos" });
    if (g->mesh == nullptr || g->surface == nullptr || g->back == nullptr || g->flash == nullptr)
    {
        g->release();
        glFailed = true;
        return;
    }

    glGenVertexArrays (1, &g->meshVao);
    glGenVertexArrays (1, &g->surfaceVao);
    glGenVertexArrays (1, &g->quadVao);
    glGenVertexArrays (1, &g->flashVao);
    for (auto* b : { &g->meshVbo, &g->meshIndex, &g->surfaceVbo, &g->surfaceIndex, &g->quadVbo, &g->flashVbo })
        glGenBuffers (1, b);
    glGenTextures (1, &g->stateTexture);

    const float unit[] = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f };
    glBindVertexArray (g->quadVao);
    glBindBuffer (GL_ARRAY_BUFFER, g->quadVbo);
    glBufferData (GL_ARRAY_BUFFER, sizeof (unit), unit, GL_STATIC_DRAW);
    glEnableVertexAttribArray (0);
    glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    const float centred[] = { -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f };
    glBindVertexArray (g->flashVao);
    glBindBuffer (GL_ARRAY_BUFFER, g->flashVbo);
    glBufferData (GL_ARRAY_BUFFER, sizeof (centred), centred, GL_STATIC_DRAW);
    glEnableVertexAttribArray (0);
    glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray (0);
    g->flash->use();
    glUniform1fv (Gpu::at (*g->flash, "uProfile"), wire::kFlashProfileSize, wire::flashProfile().data());
    glUseProgram (0);

    // The area's part of the background, top row first.
    glGenTextures (1, &g->texture);
    glBindTexture (GL_TEXTURE_2D, g->texture);
    if (background.isValid())
    {
        const juce::Image::BitmapData data (background, juce::Image::BitmapData::readOnly);
        std::vector<std::uint8_t> rgba ((size_t) (background.getWidth() * background.getHeight() * 4));
        for (int y = 0; y < background.getHeight(); ++y)
            for (int x = 0; x < background.getWidth(); ++x)
            {
                const auto c = data.getPixelColour (x, y);
                auto* p = rgba.data() + 4 * (y * background.getWidth() + x);
                p[0] = c.getRed();
                p[1] = c.getGreen();
                p[2] = c.getBlue();
                p[3] = 255;
            }
        glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, background.getWidth(), background.getHeight(), 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        glGenerateMipmap (GL_TEXTURE_2D);
    }
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture (GL_TEXTURE_2D, 0);

    gpu = std::move (g);
    glCreated = true;
}

void CymbalView::openGLContextClosing()
{
    if (gpu != nullptr)
        gpu->release();
    gpu.reset();
}

void CymbalView::renderOpenGL()
{
    if (gpu == nullptr)
        return;
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    auto& g = *gpu;
    std::shared_ptr<const wire::Geometry> newGeometry;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (pendingGeometry != g.geometry)
            newGeometry = pendingGeometry;
        g.numHits = numPendingHits;
        std::copy (pendingHits.begin(), pendingHits.begin() + numPendingHits, g.hits.begin());
    }
    if (newGeometry != nullptr)
        g.upload (std::move (newGeometry));

    GLint viewport[4] = {};
    glGetIntegerv (GL_VIEWPORT, viewport);
    const float viewW = (float) viewport[2], viewH = (float) viewport[3];
    const float px = (float) context.getRenderingScale(); // physical pixels per design unit

    glDisable (GL_DEPTH_TEST);
    glDisable (GL_CULL_FACE);
    glDisable (GL_BLEND);
    glClearColor (0.0f, 0.0f, 0.0f, 1.0f);
    glClear (GL_COLOR_BUFFER_BIT);

    // The background.
    g.back->use();
    glActiveTexture (GL_TEXTURE0);
    glBindTexture (GL_TEXTURE_2D, g.texture);
    glUniform1i (Gpu::at (*g.back, "uTexture"), 0);
    glBindVertexArray (g.quadVao);
    glDrawArrays (GL_TRIANGLE_STRIP, 0, 4);

    if (g.geometry != nullptr)
    {
        const auto& geo = *g.geometry;
        wire::evaluate (geo, g.hits.data(), g.numHits, juce::Time::getMillisecondCounterHiRes(), g.frame, g.disp);
        const auto& f = g.frame;
        for (size_t x = 0; x < (size_t) geo.numVertices(); ++x)
        {
            g.state[4 * x] = f.height[x];
            g.state[4 * x + 1] = f.dzr[x];
            g.state[4 * x + 2] = f.dzt[x];
            g.state[4 * x + 3] = f.glow[x];
        }
        glActiveTexture (GL_TEXTURE1);
        glBindTexture (GL_TEXTURE_2D, g.stateTexture);
        glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, wire::kSamples, geo.numRings, GL_RGBA, GL_FLOAT, g.state.data());
        glActiveTexture (GL_TEXTURE0);

        const wire::Camera cam;
        const auto lathe = latheZones (geo.shape);
        const auto common = [&] (const juce::OpenGLShaderProgram& p) {
            glUniform2f (Gpu::at (p, "uTilt"), (float) f.tiltX, (float) f.tiltY);
            glUniform1f (Gpu::at (p, "uSpin"), (float) (f.spinDeg * kPi / 180.0));
            glUniform3f (Gpu::at (p, "uCam"), (float) cam.sinE, (float) cam.cosE, (float) wire::Camera::distance);
            glUniform3f (Gpu::at (p, "uCentre"), (float) wire::Camera::cx, (float) wire::Camera::cy, (float) wire::Camera::scale);
            glUniform2f (Gpu::at (p, "uOrigin"), (float) area().getX(), (float) area().getY());
            glUniform1f (Gpu::at (p, "uPx"), px);
            glUniform2f (Gpu::at (p, "uY"), (float) f.yTop, (float) f.yBottom);
            glUniform2f (Gpu::at (p, "uViewPx"), viewW, viewH);
        };
        const auto lines = [&] (const juce::OpenGLShaderProgram& p, float widthUnits, float glowGain) {
            const float half = glowGain > 0.0f ? kGlowHalf : 0.5f * widthUnits;
            glUniform1f (Gpu::at (p, "uHalf"), half * px);
            glUniform1f (Gpu::at (p, "uCap"), glowGain > 0.0f ? 0.0f : 0.5f * widthUnits * px);
            glUniform1f (Gpu::at (p, "uGlowGain"), glowGain);
        };

        // Segments first to first + count of a quad buffer: two triangles each.
        const auto segments = [] (int first, int count) {
            glDrawElements (GL_TRIANGLES, 6 * count, GL_UNSIGNED_INT, (const void*) ((size_t) first * 6 * sizeof (std::uint32_t)));
        };

        // The mesh and the surface lines, lighten (max) over the background: 1 and 2 units wide.
        glEnable (GL_BLEND);
        glBlendFunc (GL_ONE, GL_ONE);
        glBlendEquation (GL_MAX);
        g.mesh->use();
        common (*g.mesh);
        glUniform4f (Gpu::at (*g.mesh, "uLathe"), lathe[0], lathe[1], lathe[2], lathe[3]);
        glBindVertexArray (g.meshVao);
        lines (*g.mesh, 1.0f, 0.0f);
        segments (0, g.thinMeshSegments);
        lines (*g.mesh, 2.0f, 0.0f);
        segments (g.thinMeshSegments, g.meshSegments - g.thinMeshSegments);

        g.surface->use();
        common (*g.surface);
        glUniform1f (Gpu::at (*g.surface, "uGlowGain"), 0.0f);
        glBindVertexArray (g.surfaceVao);
        lines (*g.surface, 1.0f, 0.0f);
        segments (0, g.thinSurfaceSegments);
        lines (*g.surface, 2.0f, 0.0f);
        segments (g.thinSurfaceSegments, g.surfaceSegments - g.thinSurfaceSegments);

        // The glow around the bright mesh lines, added.
        glBlendEquation (GL_FUNC_ADD);
        g.mesh->use();
        glBindVertexArray (g.meshVao);
        lines (*g.mesh, 1.0f, kGlowPerUnit);
        segments (0, g.thinMeshSegments);
        lines (*g.mesh, 2.0f, 2.0f * kGlowPerUnit);
        segments (g.thinMeshSegments, g.meshSegments - g.thinMeshSegments);

        // The flashes where the hits struck, added.
        if (f.numFlashes > 0)
        {
            g.flash->use();
            glBindVertexArray (g.flashVao);
            glUniform2f (Gpu::at (*g.flash, "uViewPx"), viewW, viewH);
            for (int k = 0; k < f.numFlashes; ++k)
            {
                const auto& fl = f.flashes[(size_t) k];
                glUniform2f (Gpu::at (*g.flash, "uCentrePx"), (fl.x - (float) area().getX()) * px, (fl.y - (float) area().getY()) * px);
                glUniform1f (Gpu::at (*g.flash, "uExtentPx"), 3.0f * fl.radius * px);
                glUniform3f (Gpu::at (*g.flash, "uColour"), wire::colour::flash.r * fl.intensity, wire::colour::flash.g * fl.intensity,
                             wire::colour::flash.b * fl.intensity);
                glDrawArrays (GL_TRIANGLE_STRIP, 0, 4);
            }
        }
        glDisable (GL_BLEND);
    }
    glBindVertexArray (0);
    glUseProgram (0);

    const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
    const juce::SpinLock::ScopedLockType sl (statsLock);
    ++counts.frames;
    counts.totalMs += ms;
    counts.worstMs = std::max (counts.worstMs, ms);
}

//==============================================================================
// Without OpenGL: the cymbal at rest, drawn once per preset or size with JUCE's drawing.
void CymbalView::paint (juce::Graphics& g)
{
    if (glActive.load())
        return; // OpenGL draws this area
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (error.isNotEmpty())
    {
        g.fillAll (palette::background);
        if (assets.background.isValid())
            g.drawImageAt (background, 0, 0);
        const auto box = getLocalBounds().toFloat().reduced (60.0f, 200.0f);
        drawInkTextCentred (g, assets.font (17.1f, 3.0f), "PRESET FAILED TO LOAD", { box.getCentreX(), box.getY() + 20.0f }, palette::label);
        g.setColour (palette::secondary);
        g.setFont (assets.font (15.6f));
        g.drawFittedText (error, box.withTrimmedTop (60.0f).toNearestInt(), juce::Justification::centredTop, 12);
        return;
    }
    if (shownGeometry == nullptr)
        return;
    if (! still.isValid() || std::abs (scale - stillScale) > 1.0e-3f * scale || stillGeometry != shownGeometry.get())
    {
        still = renderStill (scale);
        stillScale = scale;
        stillGeometry = shownGeometry.get();
    }
    g.drawImageTransformed (still, juce::AffineTransform::scale (1.0f / scale));
}

// The background, and the mesh and surface lines at rest lightening it (max), at `scale` pixels
// per design unit.
juce::Image CymbalView::renderStill (float scale) const
{
    const auto& geo = *shownGeometry;
    const int w = juce::jmax (1, juce::roundToInt ((float) getWidth() * scale)), h = juce::jmax (1, juce::roundToInt ((float) getHeight() * scale));
    juce::Image image (juce::Image::ARGB, w, h, true);
    {
        juce::Graphics ig (image);
        ig.fillAll (palette::background);
        ig.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        ig.addTransform (juce::AffineTransform::scale (scale));
        if (background.isValid())
            ig.drawImageAt (background, 0, 0);
    }

    wire::Frame f;
    f.resize (geo);
    std::vector<float> disp ((size_t) geo.numVertices(), 0.0f);
    wire::evaluate (geo, nullptr, 0, 0.0, f, disp);
    const wire::Camera cam;
    const auto origin = area().getPosition().toDouble();
    std::vector<juce::Point<float>> at ((size_t) geo.numVertices());
    std::vector<double> shade ((size_t) geo.numVertices());
    for (int x = 0; x < geo.numVertices(); ++x)
    {
        double sx = 0.0, sy = 0.0;
        wire::projectVertex (geo, f, cam, x, sx, sy);
        at[(size_t) x] = { (float) (sx - origin.x), (float) (sy - origin.y) };
        shade[(size_t) x] = wire::shadeAt (f, x);
    }
    const auto ytl = [&f, &origin] (double y) { return 1.0 - (y + origin.y - f.yTop) / std::max (f.yBottom - f.yTop, 1.0); };
    const auto toColour = [] (wire::Rgb c) {
        const auto u8 = [] (float v) { return (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (v * 255.0f)); };
        return juce::Colour (u8 (c.r), u8 (c.g), u8 (c.b));
    };

    juce::Image layer (juce::Image::ARGB, w, h, true);
    {
        juce::Graphics lg (layer);
        lg.fillAll (juce::Colours::black);
        lg.addTransform (juce::AffineTransform::scale (scale));
        for (size_t k = 0; k < geo.lineIndices.size(); k += 2)
        {
            const auto a = geo.lineIndices[k], b = geo.lineIndices[k + 1];
            const double r = 0.5 * (geo.rings[a / wire::kSamples] + geo.rings[b / wire::kSamples]);
            const auto c = wire::bronze (0.5 * (shade[a] + shade[b]), ytl (0.5 * (at[a].y + at[b].y)), geo.inLathe (r));
            lg.setColour (toColour (c));
            lg.drawLine ({ at[a], at[b] }, (int) k < geo.numThinIndices ? 1.0f : 2.0f);
        }
        for (size_t k = 0; k < geo.surface.size(); k += 2)
        {
            juce::Point<float> p[2];
            wire::Rgb c[2];
            for (size_t e = 0; e < 2; ++e)
            {
                double sx = 0.0, sy = 0.0;
                wire::projectSurface (geo.surface[k + e], f, cam, sx, sy);
                p[e] = { (float) (sx - origin.x), (float) (sy - origin.y) };
                c[e] = wire::surfaceColour (geo.surface[k + e], ytl (sy - origin.y));
            }
            lg.setColour (toColour ({ 0.5f * (c[0].r + c[1].r), 0.5f * (c[0].g + c[1].g), 0.5f * (c[0].b + c[1].b) }));
            lg.drawLine ({ p[0], p[1] }, (int) k < geo.numThinSurface ? 1.0f : 2.0f);
        }
    }

    // Lighten: the brighter of the background and the lines, per channel.
    juce::Image::BitmapData out (image, juce::Image::BitmapData::readWrite);
    const juce::Image::BitmapData in (layer, juce::Image::BitmapData::readOnly);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const auto a = out.getPixelColour (x, y), b = in.getPixelColour (x, y);
            out.setPixelColour (x, y, juce::Colour (std::max (a.getRed(), b.getRed()), std::max (a.getGreen(), b.getGreen()),
                                                    std::max (a.getBlue(), b.getBlue())));
        }
    return image;
}

} // namespace cymbalis::ui
