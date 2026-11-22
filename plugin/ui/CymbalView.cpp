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
constexpr int kTexWidth = 256;             // the mesh's vertex textures: vertex k at (k % 256, k / 256)

juce::String vec3Of (wire::Rgb c)
{
    return "vec3 (" + juce::String (c.r, 6) + ", " + juce::String (c.g, 6) + ", " + juce::String (c.b, 6) + ")";
}

// The colours and the reference's colour() (wire::bronze), for the shaders.
juce::String bronzeGlsl()
{
    using namespace wire::colour;
    return "const vec3 kDark = " + vec3Of (roseDark) + ", kLight = " + vec3Of (roseLight) + ", kLathe = " + vec3Of (lathe)
           + ", kGold = " + vec3Of (gold) + ", kCopper = " + vec3Of (copper) + ", kHot = " + vec3Of (hot) + ";\n"
           + R"(// metal: 0 plain, 1 lathe, 2 gold, 3 tint, 4 copper (wire::Metal); dent: the dent light factor
vec3 bronze (float shade, float ytl, float metal, float factor, float dent, float glow)
{
    float light = clamp (((shade - 0.86) / 0.14) * 0.8 + 0.2 * ytl, 0.0, 1.0);
    vec3 c = kDark + (kLight - kDark) * pow (0.25 + 0.75 * light, 1.4);
    if (metal > 3.5)
        c = min (kCopper * factor * (0.95 + 0.55 * light), vec3 (1.0));
    else if (metal > 2.5)
        c = c * 0.15 + kGold * (0.62 + 0.38 * light) * factor;
    else if (metal > 1.5)
        c = c * 0.25 + kGold * (0.42 + 0.48 * light) * factor;
    else if (metal > 0.5)
        c = c * 0.55 + kLathe * (0.30 + 0.35 * light);
    c = dent > 1.0 ? c + (kLight * 1.15 - c) * min (dent - 1.0, 1.0) : c * dent;
    return c + (kHot - c) * glow;
}
)";
}

// The camera and the tilt and spin: a point at radius r, angle theta and height z, to design
// units on screen.
const char* const kProjectGlsl = R"(uniform vec2 uTilt;    // the wobble's tilt, Z -= 2.2 (X T0 + Y T1)
uniform float uSpin;   // radians
uniform float uLift;   // the cymbal raised by this (the hi-hat's top cymbal, with the pedal)
uniform vec3 uCam;     // sin and cos of the elevation, the perspective distance
uniform vec3 uCentre;  // the cymbal's centre on screen and its scale, design units
uniform vec2 uOrigin;  // this view's top-left, design units
uniform float uPx;     // pixels per design unit
uniform vec2 uY;       // the cymbal's top and bottom on screen, design units
vec2 project (float r, float theta, float z)
{
    float phi = theta + uSpin;
    float X = r * cos (phi), Y = r * sin (phi);
    float Z = z + uLift - 2.2 * (X * uTilt.x + Y * uTilt.y);
    float yv = Y * uCam.x - Z * uCam.y, zv = Y * uCam.y + Z * uCam.x;
    return uCentre.xy + uCentre.z * vec2 (X, yv) * (uCam.z / (uCam.z - zv));
}
float topToBottom (float sy) { return 1.0 - (sy - uY.x) / max (uY.y - uY.x, 1.0); }
)";

// A line segment as a quad in pixels, built in the vertex shader (no geometry shader): each
// segment is 4 vertices, each knowing both ends; `aCorner` is (0 at the first end or 1 at the
// second, -1 or 1 across). The quad reaches `across` either side of the line and `beyond` past
// its ends (so that the joints of a ring close).
const char* const kQuadGlsl = R"(uniform vec2 uViewPx;
in vec2 aCorner;
out vec3 vColour;
out float vAcross;
vec4 quadCorner (vec2 a, vec2 b, float across, float beyond)
{
    vec2 d = b - a;
    float len = length (d);
    vec2 dir = len > 1.0e-4 ? d / len : vec2 (1.0, 0.0);
    vec2 n = vec2 (-dir.y, dir.x) * across, e = dir * beyond;
    vec2 q = (aCorner.x < 0.5 ? a - e : b + e) + n * aCorner.y;
    vAcross = aCorner.y;
    return vec4 (q.x / uViewPx.x * 2.0 - 1.0, 1.0 - q.y / uViewPx.y * 2.0, 0.0, 1.0);
}
)";

// The mesh: each segment's ends are mesh vertices, read by index from two textures: uStatic
// (radius, angle, dent light factor) and uState (per frame: height, the smooth surface's slopes
// along and around, glow). Its colour from its ends' averaged light at its middle's metal (the
// reference's col()), or, with uPerVertex, the average of its ends' colours (the dented mesh).
// aMetal: each end's metal and factor. With uGlowGain above 0 it draws the segment's glow
// instead, only where the line is brighter than 150/255.
const char* const kMeshVertex = R"(in vec2 aEnds; // the ends' mesh vertex indices
in vec4 aMetal;
uniform sampler2D uStatic;
uniform sampler2D uState;
uniform float uPerVertex;
uniform float uGlowGain;
uniform float uHalf; // pixels either side of the line
uniform float uCap;  // pixels past its ends
struct End
{
    vec2 p;
    float shade, sy, glow, dent;
};
End meshEnd (float index)
{
    int k = int (index + 0.5);
    ivec2 at = ivec2 (k % kTexWidth, k / kTexWidth);
    vec4 v = texelFetch (uStatic, at, 0), s = texelFetch (uState, at, 0);
    float th = v.y, phi = th + uSpin;
    End e;
    vec2 q = project (v.x, th, s.x);
    // The normal from the height field, with the tilt's slope (the reference's shade).
    float dzr = s.y - 2.2 * (cos (phi) * uTilt.x + sin (phi) * uTilt.y);
    float dzt = s.z - 2.2 * (-sin (phi) * uTilt.x + cos (phi) * uTilt.y);
    float ny = -(dzr * sin (th) + dzt * cos (th));
    e.shade = clamp ((ny * -0.55 + 0.83) / sqrt (dzr * dzr + dzt * dzt + 1.0), 0.0, 1.0);
    e.p = (q - uOrigin) * uPx;
    e.sy = q.y;
    e.glow = s.w;
    e.dent = v.z;
    return e;
}
void main()
{
    End a = meshEnd (aEnds.x), b = meshEnd (aEnds.y);
    vec3 c;
    if (uPerVertex > 0.5)
        c = 0.5 * (bronze (a.shade, topToBottom (a.sy), aMetal.x, aMetal.y, a.dent, a.glow)
                   + bronze (b.shade, topToBottom (b.sy), aMetal.z, aMetal.w, b.dent, b.glow));
    else
        c = bronze (0.5 * (a.shade + b.shade), topToBottom (0.5 * (a.sy + b.sy)), aMetal.x, aMetal.y, 0.5 * (a.dent + b.dent),
                    0.5 * (a.glow + b.glow));
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
    gl_Position = quadCorner (a.p, b.p, uHalf, uCap);
}
)";

// The surface lines: each segment's ends (radius, angle, height: the profile's, no flex; on
// SWISH with the dents) and its kind (wire::SurfaceKind), colour from the ends' average, then
// the dent light (wire::lineDent) from the ends' average dent light factor.
const char* const kSurfaceVertex = R"(in vec3 aA;
in vec3 aB;
in vec4 aKind;  // kind, its factor, the surface's metal and factor there
in vec2 aWidth; // design units; the dent light factor
vec3 colourAt (vec3 e, out vec2 p)
{
    vec2 s = project (e.x, e.y, e.z);
    float ytl = topToBottom (s.y);
    p = (s - uOrigin) * uPx;
    vec3 surface = bronze (0.9, ytl, aKind.z, aKind.w, 1.0, 0.0);
    if (aKind.x < 0.5)
        return surface * aKind.y; // hammer marks
    if (aKind.x < 1.5)
        return surface * (0.30 + 0.25 * ytl); // fine lathe
    if (aKind.x < 2.5)
        return kGold * (0.30 + 0.32 * ytl) * aKind.y; // bell grooves
    if (aKind.x < 3.5)
        return kGold * (0.55 + 0.40 * ytl) * aKind.y; // edge lines
    if (aKind.x < 4.5)
        return surface * (0.50 + 0.35 * ytl) * aKind.y; // gold lathing
    if (aKind.x < 5.5)
        return kGold * (0.50 + 0.35 * ytl) * aKind.y;   // the dented mesh's gold lathing
    return kCopper * aKind.w * (0.45 + 0.35 * ytl) * aKind.y; // grooves in a copper zone (copperK)
}
void main()
{
    vec2 pa, pb;
    vec3 ca = colourAt (aA, pa), cb = colourAt (aB, pb), c = 0.5 * (ca + cb);
    float f = aWidth.y;
    vColour = f > 1.0 ? c + (kHot * 0.95 - c) * min (f - 1.0, 1.0) : c * f;
    gl_Position = quadCorner (pa, pb, 0.5 * aWidth.x * uPx, 0.5 * aWidth.x * uPx);
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

// A texture over the whole view: the background (its top row first), or the lines' layer (an
// OpenGL framebuffer's, bottom row first: uFlip).
const char* const kBackgroundVertex = R"(in vec2 aPos; // 0 .. 1 from the top-left
uniform float uFlip;
out vec2 vUv;
void main()
{
    vUv = vec2 (aPos.x, uFlip > 0.5 ? 1.0 - aPos.y : aPos.y);
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

// The hi-hat's clutch and rod: lines whose ends are already on screen (design units), each with
// its colour and width.
const char* const kFlatVertex = R"(in vec2 aA;
in vec2 aB;
in vec4 aColour; // rgb, width in design units
uniform vec2 uOrigin;
uniform float uPx;
void main()
{
    vColour = aColour.rgb;
    gl_Position = quadCorner ((aA - uOrigin) * uPx, (aB - uOrigin) * uPx, 0.5 * aColour.w * uPx, 0.5 * aColour.w * uPx);
}
)";

// A polygon on screen (design units), as a triangle fan: in one colour, or with uUseTexture the
// background under it (the view's texture, its top row first).
const char* const kFillVertex = R"(in vec2 aPos;
uniform vec2 uOrigin;
uniform float uPx;
uniform vec2 uViewPx;
out vec2 vUv;
void main()
{
    vec2 q = (aPos - uOrigin) * uPx;
    vUv = q / uViewPx;
    gl_Position = vec4 (q.x / uViewPx.x * 2.0 - 1.0, 1.0 - q.y / uViewPx.y * 2.0, 0.0, 1.0);
}
)";
const char* const kFillFragment = R"(in vec2 vUv;
uniform vec3 uColour;
uniform float uUseTexture;
uniform sampler2D uTexture;
out vec4 fragColour;
void main()
{
    fragColour = vec4 (uUseTexture > 0.5 ? texture (uTexture, vUv).rgb : uColour, 1.0);
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

} // namespace

//==============================================================================
struct CymbalView::Gpu
{
    std::unique_ptr<juce::OpenGLShaderProgram> mesh, surface, back, flash, flat, fill;
    GLuint quadVao = 0, quadVbo = 0, flashVao = 0, flashVbo = 0;
    GLuint texture = 0;
    // The hi-hat's clutch and rod (flat: lines already on screen), and the polygons that hide
    // what is behind its top cymbal and its clutch and rod (fill).
    GLuint flatVao = 0, flatVbo = 0, flatIndex = 0, fillVao = 0, fillVbo = 0;
    int flatSegments = 0;
    // A layer of lines: multisampled (as the view's own framebuffer), then resolved into a
    // texture, at the view's size. ok is false if the driver refuses it.
    struct Layer
    {
        GLuint fbo = 0, samples = 0, resolveFbo = 0, texture = 0;
        int w = 0, h = 0;
        bool ok = false;

        // True when it is ready at w x h; `made` tells whether it was (re)made.
        bool prepare (int width, int height, bool& made)
        {
            made = false;
            if (width == w && height == h)
                return ok;
            release();
            made = true;
            w = width;
            h = height;
            GLint maxSamples = 0;
            glGetIntegerv (GL_MAX_SAMPLES, &maxSamples);
            glGenFramebuffers (1, &fbo);
            glGenRenderbuffers (1, &samples);
            glBindRenderbuffer (GL_RENDERBUFFER, samples);
            glRenderbufferStorageMultisample (GL_RENDERBUFFER, juce::jmin (4, (int) maxSamples), GL_RGBA8, w, h);
            glBindFramebuffer (GL_FRAMEBUFFER, fbo);
            glFramebufferRenderbuffer (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, samples);
            ok = glCheckFramebufferStatus (GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
            glGenTextures (1, &texture);
            glBindTexture (GL_TEXTURE_2D, texture);
            glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindTexture (GL_TEXTURE_2D, 0);
            glGenFramebuffers (1, &resolveFbo);
            glBindFramebuffer (GL_FRAMEBUFFER, resolveFbo);
            glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
            ok = ok && glCheckFramebufferStatus (GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
            return ok;
        }

        // The multisampled lines into the texture.
        void resolve() const
        {
            glBindFramebuffer (GL_READ_FRAMEBUFFER, fbo);
            glBindFramebuffer (GL_DRAW_FRAMEBUFFER, resolveFbo);
            glBlitFramebuffer (0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        }

        void release()
        {
            for (auto* f : { &fbo, &resolveFbo })
                if (*f != 0)
                    glDeleteFramebuffers (1, f);
            if (samples != 0)
                glDeleteRenderbuffers (1, &samples);
            if (texture != 0)
                glDeleteTextures (1, &texture);
            fbo = samples = resolveFbo = texture = 0;
            w = h = 0;
            ok = false;
        }
    };
    Layer layer;
    // The hi-hat's bottom cymbal, which never moves: drawn once (and on a resize) into a layer of
    // its own, which each frame starts from.
    Layer lowerLayer;
    bool lowerDrawn = false;

    // One cymbal's buffers: the preset's cymbal (or the hi-hat's top cymbal), and the hi-hat's
    // bottom cymbal.
    struct Part
    {
        GLuint meshVao = 0, meshVbo = 0, meshIndex = 0, staticTexture = 0, stateTexture = 0;
        GLuint surfaceVao = 0, surfaceVbo = 0, surfaceIndex = 0;
        std::shared_ptr<const wire::Geometry> geometry;
        int meshSegments = 0, thinMeshSegments = 0, surfaceSegments = 0;
        int textureRows = 0;
        wire::Frame frame;
        std::vector<float> disp, state;

        void create()
        {
            glGenVertexArrays (1, &meshVao);
            glGenVertexArrays (1, &surfaceVao);
            for (auto* b : { &meshVbo, &meshIndex, &surfaceVbo, &surfaceIndex })
                glGenBuffers (1, b);
            glGenTextures (1, &staticTexture);
            glGenTextures (1, &stateTexture);
        }

        void release()
        {
            for (auto* vao : { &meshVao, &surfaceVao })
                if (*vao != 0)
                    glDeleteVertexArrays (1, vao);
            for (auto* b : { &meshVbo, &meshIndex, &surfaceVbo, &surfaceIndex })
                if (*b != 0)
                    glDeleteBuffers (1, b);
            for (auto* t : { &staticTexture, &stateTexture })
                if (*t != 0)
                    glDeleteTextures (1, t);
            meshVao = surfaceVao = meshVbo = meshIndex = surfaceVbo = surfaceIndex = staticTexture = stateTexture = 0;
        }

        void upload (std::shared_ptr<const wire::Geometry> g)
        {
            geometry = std::move (g);
            if (geometry == nullptr)
                return;
            const auto& geo = *geometry;
            frame.resize (geo);
            disp.assign ((size_t) geo.numVertices(), 0.0f);
            textureRows = (geo.numVertices() + kTexWidth - 1) / kTexWidth;
            state.assign ((size_t) (textureRows * kTexWidth) * 4, 0.0f);
            constexpr float corners[4][2] = { { 0.0f, -1.0f }, { 0.0f, 1.0f }, { 1.0f, -1.0f }, { 1.0f, 1.0f } };

            // The mesh: per quad corner, the corner, its segment's two mesh vertices, and their metal.
            meshSegments = (int) geo.lineIndices.size() / 2;
            thinMeshSegments = geo.numThinIndices / 2;
            std::vector<float> quads;
            quads.reserve ((size_t) meshSegments * 32);
            for (int k = 0; k < meshSegments; ++k)
            {
                const float* metal = geo.segmentMetal.data() + 4 * k;
                for (const auto& c : corners)
                    for (float v : { c[0], c[1], (float) geo.lineIndices[(size_t) (2 * k)], (float) geo.lineIndices[(size_t) (2 * k + 1)],
                                     metal[0], metal[1], metal[2], metal[3] })
                        quads.push_back (v);
            }
            glBindVertexArray (meshVao);
            glBindBuffer (GL_ARRAY_BUFFER, meshVbo);
            glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (quads.size() * sizeof (float)), quads.data(), GL_STATIC_DRAW);
            const auto meshStride = (GLsizei) (8 * sizeof (float));
            glEnableVertexAttribArray (0);
            glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, meshStride, nullptr);
            glEnableVertexAttribArray (1);
            glVertexAttribPointer (1, 2, GL_FLOAT, GL_FALSE, meshStride, (const void*) (2 * sizeof (float)));
            glEnableVertexAttribArray (2);
            glVertexAttribPointer (2, 4, GL_FLOAT, GL_FALSE, meshStride, (const void*) (4 * sizeof (float)));
            uploadQuadIndices (meshIndex, meshSegments);

            // The mesh's vertices, one texel each: static (radius, angle, dent light factor), and
            // their state per frame.
            std::vector<float> fixed ((size_t) (textureRows * kTexWidth) * 4, 0.0f);
            for (size_t x = 0; x < (size_t) geo.numVertices(); ++x)
            {
                fixed[4 * x] = (float) geo.vertexR[x];
                fixed[4 * x + 1] = (float) geo.vertexTheta[x];
                fixed[4 * x + 2] = geo.vertexDent[x];
            }
            for (auto [vertexTexture, data] : { std::pair<GLuint, const float*> { staticTexture, fixed.data() }, { stateTexture, nullptr } })
            {
                glBindTexture (GL_TEXTURE_2D, vertexTexture);
                glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA32F, kTexWidth, textureRows, 0, GL_RGBA, GL_FLOAT, data);
                glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            }
            glBindTexture (GL_TEXTURE_2D, 0);

            // The surface lines: per quad corner, the corner, both ends, the segment's kind, width and
            // dent light factor (its ends' average).
            surfaceSegments = (int) geo.surface.size() / 2;
            std::vector<float> lines;
            lines.reserve ((size_t) surfaceSegments * 56);
            for (int k = 0; k < surfaceSegments; ++k)
            {
                const auto& a = geo.surface[(size_t) (2 * k)];
                const auto& b = geo.surface[(size_t) (2 * k + 1)];
                for (const auto& c : corners)
                    for (float v : { c[0], c[1], a.r, a.theta, a.z, b.r, b.theta, b.z, a.kind, a.param, a.metal, a.factor, a.width,
                                     0.5f * (a.dent + b.dent) })
                        lines.push_back (v);
            }
            glBindVertexArray (surfaceVao);
            glBindBuffer (GL_ARRAY_BUFFER, surfaceVbo);
            glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (lines.size() * sizeof (float)), lines.data(), GL_STATIC_DRAW);
            const auto stride = (GLsizei) (14 * sizeof (float));
            glEnableVertexAttribArray (0);
            glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, stride, nullptr);
            for (GLuint a = 1; a <= 3; ++a)
            {
                glEnableVertexAttribArray (a);
                glVertexAttribPointer (a, a < 3 ? 3 : 4, GL_FLOAT, GL_FALSE, stride, (const void*) ((size_t) (2 + 3 * (a - 1)) * sizeof (float)));
            }
            glEnableVertexAttribArray (4);
            glVertexAttribPointer (4, 2, GL_FLOAT, GL_FALSE, stride, (const void*) (12 * sizeof (float)));
            uploadQuadIndices (surfaceIndex, surfaceSegments);
            glBindVertexArray (0);
        }

        // The cymbal at `nowMs` with the hits, raised by `lift`, into the state texture.
        void evaluate (const wire::Hit* hits, int numHits, double nowMs, double lift)
        {
            const auto& geo = *geometry;
            frame.lift = lift;
            wire::evaluate (geo, hits, numHits, nowMs, frame, disp);
            for (size_t x = 0; x < (size_t) geo.numVertices(); ++x)
            {
                state[4 * x] = frame.height[x];
                state[4 * x + 1] = frame.dzr[x];
                state[4 * x + 2] = frame.dzt[x];
                state[4 * x + 3] = frame.glow[x];
            }
            glBindTexture (GL_TEXTURE_2D, stateTexture);
            glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, kTexWidth, textureRows, GL_RGBA, GL_FLOAT, state.data());
            glBindTexture (GL_TEXTURE_2D, 0);
        }
    };
    std::array<Part, 2> parts; // the cymbal (the hi-hat's top), the hi-hat's bottom cymbal
    bool lowerEvaluated = false; // the bottom cymbal never moves: its state is set once
    // The top cymbal's lift, tilt and spin the clutch and rod were last uploaded with.
    std::array<double, 4> shownPose { -1.0, 0.0, 0.0, 0.0 };

    std::array<wire::Hit, wire::kMaxHits> hits {};
    int numHits = 0;
    float opening = 0.0f;
    // The hi-hat's polygons this frame (fill): their points, and each fan's first point and count.
    std::vector<float> fans;
    std::vector<std::array<int, 2>> fanRanges;
    std::vector<std::array<float, 2>> hull;

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

    // The clutch and rod on screen, standing on the top cymbal's bell top zb (raised with it),
    // tilted and spun with it, into the flat buffer: per quad corner, the corner, both ends, the
    // colour and width.
    void uploadHardware (const wire::Camera& cam, const wire::Frame& top, double zb)
    {
        const auto& hw = wire::hiHatHardware();
        constexpr float corners[4][2] = { { 0.0f, -1.0f }, { 0.0f, 1.0f }, { 1.0f, -1.0f }, { 1.0f, 1.0f } };
        std::vector<float> quads;
        quads.reserve (hw.lines.size() * 40);
        for (const auto& l : hw.lines)
        {
            double ax = 0.0, ay = 0.0, bx = 0.0, by = 0.0;
            wire::projectHardware (cam, top, zb, l.x0, l.y0, l.z0, ax, ay);
            wire::projectHardware (cam, top, zb, l.x1, l.y1, l.z1, bx, by);
            for (const auto& c : corners)
                for (float v : { c[0], c[1], (float) ax, (float) ay, (float) bx, (float) by, l.colour.r, l.colour.g, l.colour.b, l.width })
                    quads.push_back (v);
        }
        glBindVertexArray (flatVao);
        glBindBuffer (GL_ARRAY_BUFFER, flatVbo);
        glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (quads.size() * sizeof (float)), quads.data(), GL_DYNAMIC_DRAW);
        const auto stride = (GLsizei) (10 * sizeof (float));
        glEnableVertexAttribArray (0);
        glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, stride, nullptr);
        glEnableVertexAttribArray (1);
        glVertexAttribPointer (1, 2, GL_FLOAT, GL_FALSE, stride, (const void*) (2 * sizeof (float)));
        glEnableVertexAttribArray (2);
        glVertexAttribPointer (2, 2, GL_FLOAT, GL_FALSE, stride, (const void*) (4 * sizeof (float)));
        glEnableVertexAttribArray (3);
        glVertexAttribPointer (3, 4, GL_FLOAT, GL_FALSE, stride, (const void*) (6 * sizeof (float)));
        if (flatSegments != (int) hw.lines.size())
        {
            flatSegments = (int) hw.lines.size();
            uploadQuadIndices (flatIndex, flatSegments);
        }
        glBindVertexArray (0);
    }

    void release()
    {
        layer.release();
        lowerLayer.release();
        for (auto& part : parts)
            part.release();
        for (auto* vao : { &quadVao, &flashVao, &flatVao, &fillVao })
            if (*vao != 0)
                glDeleteVertexArrays (1, vao);
        for (auto* b : { &quadVbo, &flashVbo, &flatVbo, &flatIndex, &fillVbo })
            if (*b != 0)
                glDeleteBuffers (1, b);
        if (texture != 0)
            glDeleteTextures (1, &texture);
        mesh.reset();
        surface.reset();
        back.reset();
        flash.reset();
        flat.reset();
        fill.reset();
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
        // A preset without a wireframe: nothing is drawn. The hi-hat: its top cymbal (the one
        // the hits strike) and its bottom cymbal.
        const auto* shape = kPresets[(size_t) preset].shape;
        shownGeometry = shape != nullptr ? std::make_shared<const wire::Geometry> (*shape) : nullptr;
        shownLower = shape != nullptr && shape->lower != nullptr ? std::make_shared<const wire::Geometry> (*shape->lower) : nullptr;
        // On the hi-hat, its opening and wobble, for screen readers (and tools/cymbalis_uitest):
        // hiHatDescription, made when asked for.
        setTitle (shownLower != nullptr ? "HI-HAT" : "");
    }
    error = loadError;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        pendingGeometry = shownGeometry;
        pendingLower = shownLower;
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

void CymbalView::setOpening (float o)
{
    o = juce::jlimit (0.0f, 1.0f, o);
    if (juce::exactlyEqual (o, shownOpening))
        return;
    shownOpening = o;
    bool hitsMoving = false;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        pendingOpening = o;
        hitsMoving = numPendingHits > 0;
    }
    if (shownLower == nullptr)
        return; // only the hi-hat opens
    if (! glActive.load())
        repaint();
    else if (! hitsMoving)
        context.triggerRepaint(); // while hits move, their next frame (30 Hz) draws it
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
    counts = { 0, 0.0, 0.0, 0, counts.hits, counts.wobble, counts.tiltDrawn, counts.tiltFull, counts.tiltBottom };
}

// The hi-hat's description, made when it is asked for: "open 0.504; wobble 0.578, top tilt
// 0.00123 of 0.00213, bottom tilt 0" (the last frame's).
juce::String CymbalView::hiHatDescription() const
{
    if (shownLower == nullptr)
        return {};
    const auto s = stats();
    return "open " + juce::String (shownOpening, 3) + "; wobble " + juce::String (s.wobble, 3) + ", top tilt " + juce::String (s.tiltDrawn, 6)
           + " of " + juce::String (s.tiltFull, 6) + ", bottom tilt " + juce::String (s.tiltBottom, 6);
}

class CymbalView::Accessibility final : public juce::AccessibilityHandler
{
public:
    explicit Accessibility (CymbalView& v) : juce::AccessibilityHandler (v, juce::AccessibilityRole::unspecified), view (v) {}
    juce::String getDescription() const override { return view.hiHatDescription(); }

private:
    CymbalView& view;
};

std::unique_ptr<juce::AccessibilityHandler> CymbalView::createAccessibilityHandler()
{
    return std::make_unique<Accessibility> (*this);
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
    const juce::String common = "const int kTexWidth = " + juce::String (kTexWidth) + ";\n" + bronzeGlsl() + kProjectGlsl + kQuadGlsl;
    g->mesh = Gpu::build (context, common + kMeshVertex, kLineFragment, { "aCorner", "aEnds", "aMetal" });
    g->surface = Gpu::build (context, common + kSurfaceVertex, kLineFragment, { "aCorner", "aA", "aB", "aKind", "aWidth" });
    g->back = Gpu::build (context, kBackgroundVertex, kBackgroundFragment, { "aPos" });
    g->flash = Gpu::build (context, kFlashVertex, kFlashFragment, { "aPos" });
    g->flat = Gpu::build (context, juce::String (kQuadGlsl) + kFlatVertex, kLineFragment, { "aCorner", "aA", "aB", "aColour" });
    g->fill = Gpu::build (context, kFillVertex, kFillFragment, { "aPos" });
    if (g->mesh == nullptr || g->surface == nullptr || g->back == nullptr || g->flash == nullptr || g->flat == nullptr || g->fill == nullptr)
    {
        g->release();
        glFailed = true;
        return;
    }

    for (auto& part : g->parts)
        part.create();
    for (auto* vao : { &g->quadVao, &g->flashVao, &g->flatVao, &g->fillVao })
        glGenVertexArrays (1, vao);
    for (auto* b : { &g->quadVbo, &g->flashVbo, &g->flatVbo, &g->flatIndex, &g->fillVbo })
        glGenBuffers (1, b);

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
    auto& top = g.parts[0];
    auto& bottom = g.parts[1];
    std::shared_ptr<const wire::Geometry> newGeometry, newLower;
    bool geometryChanged = false;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (pendingGeometry != top.geometry || pendingLower != bottom.geometry)
        {
            newGeometry = pendingGeometry;
            newLower = pendingLower;
            geometryChanged = true; // also to none (a preset without a wireframe)
        }
        g.numHits = numPendingHits;
        std::copy (pendingHits.begin(), pendingHits.begin() + numPendingHits, g.hits.begin());
        g.opening = pendingOpening;
    }
    if (geometryChanged)
    {
        top.upload (std::move (newGeometry));
        bottom.upload (std::move (newLower));
        g.lowerEvaluated = false;
        g.lowerDrawn = false;
        g.shownPose[0] = -1.0;
    }

    GLint viewport[4] = {}, target = 0;
    glGetIntegerv (GL_VIEWPORT, viewport);
    glGetIntegerv (GL_FRAMEBUFFER_BINDING, &target); // the view's framebuffer
    const float viewW = (float) viewport[2], viewH = (float) viewport[3];
    const float px = (float) context.getRenderingScale(); // physical pixels per design unit
    const auto origin = area().getPosition().toFloat();

    glDisable (GL_DEPTH_TEST);
    glDisable (GL_CULL_FACE);
    glDisable (GL_BLEND);
    glClearColor (0.0f, 0.0f, 0.0f, 1.0f);
    glClear (GL_COLOR_BUFFER_BIT);

    // A texture over the whole view.
    const auto drawTexture = [&] (GLuint texture, bool flip) {
        g.back->use();
        glActiveTexture (GL_TEXTURE0);
        glBindTexture (GL_TEXTURE_2D, texture);
        glUniform1i (Gpu::at (*g.back, "uTexture"), 0);
        glUniform1f (Gpu::at (*g.back, "uFlip"), flip ? 1.0f : 0.0f);
        glBindVertexArray (g.quadVao);
        glDrawArrays (GL_TRIANGLE_STRIP, 0, 4);
    };

    if (top.geometry == nullptr)
        drawTexture (g.texture, false);
    else
    {
        const auto& geo = *top.geometry;
        const bool hiHat = bottom.geometry != nullptr;
        // The hi-hat's top cymbal, its clutch and rod rise with the pedal's opening, and swing
        // with the hits by 0.15 + 0.85 x the opening (closed, the bottom cymbal holds the top one
        // still); the bottom cymbal stays, and never moves: its state is set once.
        const double lift = hiHat ? (double) g.opening * wire::kHiHatMaxGap : 0.0;
        top.frame.wobble = hiHat ? 0.15 + 0.85 * (double) g.opening : 1.0;
        top.evaluate (g.hits.data(), g.numHits, juce::Time::getMillisecondCounterHiRes(), lift);
        if (hiHat && ! g.lowerEvaluated)
        {
            bottom.evaluate (nullptr, 0, 0.0, 0.0);
            g.lowerEvaluated = true;
        }

        // The hi-hat's polygons, as triangle fans: the top cymbal's outline on screen (its rim, as
        // it moves), which hides the bottom cymbal; and the clutch and rod's silhouettes, which
        // hide the cymbal lines behind them. The clutch and rod's lines, when the opening changed.
        g.fans.clear();
        g.fanRanges.clear();
        if (hiHat)
        {
            const auto fan = [&g] (const auto& points, auto pointAt) {
                const int first = (int) g.fans.size() / 2;
                double cx = 0.0, cy = 0.0;
                for (size_t k = 0; k < points.size(); ++k)
                {
                    const auto q = pointAt (k);
                    cx += q[0];
                    cy += q[1];
                }
                const double n = (double) juce::jmax ((size_t) 1, points.size());
                g.fans.push_back ((float) (cx / n));
                g.fans.push_back ((float) (cy / n));
                for (size_t k = 0; k <= points.size(); ++k)
                {
                    const auto q = pointAt (k % points.size());
                    g.fans.push_back ((float) q[0]);
                    g.fans.push_back ((float) q[1]);
                }
                g.fanRanges.push_back ({ first, (int) g.fans.size() / 2 - first });
            };
            fan (geo.rimVertices, [&] (size_t k) {
                std::array<double, 2> q {};
                wire::projectVertex (geo, top.frame, geo.camera, (int) geo.rimVertices[k], q[0], q[1]);
                return q;
            });
            const double zb = geo.restHeight (0.0) + lift; // the top cymbal's bell top
            for (const auto& solid : wire::hiHatHardware().solids)
            {
                wire::silhouette (geo.camera, top.frame, solid, zb, g.hull);
                fan (g.hull, [&g] (size_t k) { return std::array<double, 2> { g.hull[k][0], g.hull[k][1] }; });
            }
            glBindVertexArray (g.fillVao);
            glBindBuffer (GL_ARRAY_BUFFER, g.fillVbo);
            glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (g.fans.size() * sizeof (float)), g.fans.data(), GL_STREAM_DRAW);
            glEnableVertexAttribArray (0);
            glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
            glBindVertexArray (0);
            const std::array<double, 4> pose { lift, top.frame.tiltX, top.frame.tiltY, top.frame.spinDeg };
            if (pose != g.shownPose)
            {
                g.uploadHardware (geo.camera, top.frame, zb);
                g.shownPose = pose;
            }
        }

        // The uniforms a cymbal's lines share: its frame (tilt, spin, lift, top and bottom on
        // screen) and its camera.
        const auto common = [&] (const juce::OpenGLShaderProgram& p, const Gpu::Part& part) {
            const auto& f = part.frame;
            const auto& cam = part.geometry->camera;
            glUniform2f (Gpu::at (p, "uTilt"), (float) f.tiltX, (float) f.tiltY);
            glUniform1f (Gpu::at (p, "uSpin"), (float) (f.spinDeg * kPi / 180.0));
            glUniform1f (Gpu::at (p, "uLift"), (float) f.lift);
            glUniform3f (Gpu::at (p, "uCam"), (float) cam.sinE, (float) cam.cosE, (float) cam.distance);
            glUniform3f (Gpu::at (p, "uCentre"), (float) cam.cx, (float) cam.cy, (float) cam.scale);
            glUniform2f (Gpu::at (p, "uOrigin"), origin.x, origin.y);
            glUniform1f (Gpu::at (p, "uPx"), px);
            glUniform2f (Gpu::at (p, "uY"), (float) f.yTop, (float) f.yBottom);
            glUniform2f (Gpu::at (p, "uViewPx"), viewW, viewH);
        };
        // Segments first to first + count of a quad buffer: two triangles each.
        const auto segments = [] (int first, int count) {
            glDrawElements (GL_TRIANGLES, 6 * count, GL_UNSIGNED_INT, (const void*) ((size_t) first * 6 * sizeof (std::uint32_t)));
        };
        // A cymbal's mesh, 1 and 2 units wide; with a glow gain, its glow instead.
        const auto drawMesh = [&] (const Gpu::Part& part, float glowGain) {
            g.mesh->use();
            common (*g.mesh, part);
            glUniform1i (Gpu::at (*g.mesh, "uStatic"), 1);
            glUniform1i (Gpu::at (*g.mesh, "uState"), 2);
            glUniform1f (Gpu::at (*g.mesh, "uPerVertex"), part.geometry->perVertexColour ? 1.0f : 0.0f);
            glActiveTexture (GL_TEXTURE1);
            glBindTexture (GL_TEXTURE_2D, part.staticTexture);
            glActiveTexture (GL_TEXTURE2);
            glBindTexture (GL_TEXTURE_2D, part.stateTexture);
            glActiveTexture (GL_TEXTURE0);
            glBindVertexArray (part.meshVao);
            for (const bool wide : { false, true })
            {
                const float width = wide ? 2.0f : 1.0f, gain = glowGain * width;
                glUniform1f (Gpu::at (*g.mesh, "uHalf"), (gain > 0.0f ? kGlowHalf : 0.5f * width) * px);
                glUniform1f (Gpu::at (*g.mesh, "uCap"), gain > 0.0f ? 0.0f : 0.5f * width * px);
                glUniform1f (Gpu::at (*g.mesh, "uGlowGain"), gain);
                if (wide)
                    segments (part.thinMeshSegments, part.meshSegments - part.thinMeshSegments);
                else
                    segments (0, part.thinMeshSegments);
            }
        };
        // A cymbal's surface lines in their order, each over those before it.
        const auto drawSurface = [&] (const Gpu::Part& part) {
            g.surface->use();
            common (*g.surface, part);
            glUniform1f (Gpu::at (*g.surface, "uGlowGain"), 0.0f);
            glBindVertexArray (part.surfaceVao);
            segments (0, part.surfaceSegments);
        };

        // The lines' layer, as the reference draws it: the surface lines over one another in their
        // order, lightened (max) by the mesh; on the dented mesh, the surface lines over the mesh.
        // Then the layer lightens the background. (Without a layer, everything lightens the
        // background directly.)
        bool remade = false;
        const bool layered = g.layer.prepare (viewport[2], viewport[3], remade);
        if (layered)
        {
            glBindFramebuffer (GL_FRAMEBUFFER, g.layer.fbo);
            glViewport (0, 0, viewport[2], viewport[3]);
            glClear (GL_COLOR_BUFFER_BIT);
        }
        else
            drawTexture (g.texture, false);
        glBlendFunc (GL_ONE, GL_ONE);
        glBlendEquation (GL_MAX);
        const auto overwrite = [layered] {
            if (layered)
                glDisable (GL_BLEND);
            else
                glEnable (GL_BLEND);
        };
        if (! hiHat)
        {
            if (geo.perVertexColour)
            {
                glEnable (GL_BLEND);
                drawMesh (top, 0.0f);
                overwrite();
                drawSurface (top);
            }
            else
            {
                overwrite();
                drawSurface (top);
                glEnable (GL_BLEND);
                drawMesh (top, 0.0f);
            }
        }
        else
        {
            // The hi-hat, as hihat_draw: the bottom cymbal; hidden where the top cymbal is in
            // front of it; the top cymbal over it; the clutch and rod's silhouettes clearing the
            // lines behind them (to 0.08); then the clutch and rod. Each cymbal's glow goes with
            // it (the reference blooms each cymbal as it draws it), so it is hidden with it.
            // Without a layer, what is hidden shows the background instead.
            const auto cymbal = [&] (const Gpu::Part& part) {
                glEnable (GL_BLEND);
                glBlendFunc (GL_ONE, GL_ONE);
                glBlendEquation (GL_MAX);
                drawMesh (part, 0.0f);
                overwrite();
                drawSurface (part);
                glEnable (GL_BLEND);
                glBlendEquation (GL_FUNC_ADD);
                drawMesh (part, kGlowPerUnit);
            };
            const auto hide = [&] (size_t fanIndex, float keep) {
                g.fill->use();
                glUniform2f (Gpu::at (*g.fill, "uOrigin"), origin.x, origin.y);
                glUniform1f (Gpu::at (*g.fill, "uPx"), px);
                glUniform2f (Gpu::at (*g.fill, "uViewPx"), viewW, viewH);
                glUniform3f (Gpu::at (*g.fill, "uColour"), keep, keep, keep);
                glUniform1f (Gpu::at (*g.fill, "uUseTexture"), layered ? 0.0f : 1.0f);
                glUniform1i (Gpu::at (*g.fill, "uTexture"), 0);
                glActiveTexture (GL_TEXTURE0);
                glBindTexture (GL_TEXTURE_2D, g.texture);
                if (layered)
                {
                    // multiplies what is there by `keep`
                    glEnable (GL_BLEND);
                    glBlendFunc (GL_ZERO, GL_SRC_COLOR);
                    glBlendEquation (GL_FUNC_ADD);
                }
                else
                    glDisable (GL_BLEND);
                glBindVertexArray (g.fillVao);
                const auto range = g.fanRanges[fanIndex];
                glDrawArrays (GL_TRIANGLE_FAN, range[0], range[1]);
                glBlendFunc (GL_ONE, GL_ONE);
            };
            // The bottom cymbal never moves: drawn once into its own layer, which the lines'
            // layer starts from each frame.
            bool lowerRemade = false;
            if (layered && g.lowerLayer.prepare (viewport[2], viewport[3], lowerRemade))
            {
                if (lowerRemade || ! g.lowerDrawn)
                {
                    glBindFramebuffer (GL_FRAMEBUFFER, g.lowerLayer.fbo);
                    glClear (GL_COLOR_BUFFER_BIT);
                    cymbal (bottom);
                    g.lowerLayer.resolve();
                    g.lowerDrawn = true;
                    glBindFramebuffer (GL_FRAMEBUFFER, g.layer.fbo);
                }
                glDisable (GL_BLEND);
                drawTexture (g.lowerLayer.texture, true);
            }
            else
                cymbal (bottom);
            hide (0, 0.0f);
            cymbal (top);
            for (size_t k = 1; k < g.fanRanges.size(); ++k)
                hide (k, 0.08f);
            glEnable (GL_BLEND);
            glBlendFunc (GL_ONE, GL_ONE);
            glBlendEquation (GL_MAX);
            g.flat->use();
            glUniform2f (Gpu::at (*g.flat, "uOrigin"), origin.x, origin.y);
            glUniform1f (Gpu::at (*g.flat, "uPx"), px);
            glUniform2f (Gpu::at (*g.flat, "uViewPx"), viewW, viewH);
            glUniform1f (Gpu::at (*g.flat, "uGlowGain"), 0.0f);
            glBindVertexArray (g.flatVao);
            segments (0, g.flatSegments);
        }
        if (layered)
        {
            g.layer.resolve();
            glBindFramebuffer (GL_FRAMEBUFFER, (GLuint) target);
            glViewport (viewport[0], viewport[1], viewport[2], viewport[3]);
            glDisable (GL_BLEND);
            drawTexture (g.texture, false);
            glEnable (GL_BLEND);
            glBlendFunc (GL_ONE, GL_ONE);
            glBlendEquation (GL_MAX);
            drawTexture (g.layer.texture, true);
        }

        // The glow around the bright mesh lines, added (the hi-hat's went with its cymbals).
        glEnable (GL_BLEND);
        glBlendFunc (GL_ONE, GL_ONE);
        glBlendEquation (GL_FUNC_ADD);
        if (! hiHat)
            drawMesh (top, kGlowPerUnit);

        // The flashes where the hits struck, added.
        const auto& f = top.frame;
        if (f.numFlashes > 0)
        {
            g.flash->use();
            glBindVertexArray (g.flashVao);
            glUniform2f (Gpu::at (*g.flash, "uViewPx"), viewW, viewH);
            for (int k = 0; k < f.numFlashes; ++k)
            {
                const auto& fl = f.flashes[(size_t) k];
                glUniform2f (Gpu::at (*g.flash, "uCentrePx"), (fl.x - origin.x) * px, (fl.y - origin.y) * px);
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
    if (top.geometry != nullptr)
    {
        const auto& f = top.frame;
        counts.wobble = f.wobble;
        counts.tiltDrawn = std::hypot (f.tiltX, f.tiltY);
        counts.tiltFull = f.wobble > 0.0 ? counts.tiltDrawn / f.wobble : 0.0;
        counts.tiltBottom = bottom.geometry != nullptr ? std::hypot (bottom.frame.tiltX, bottom.frame.tiltY) : 0.0;
    }
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
    const float opening = shownLower != nullptr ? shownOpening : 0.0f;
    if (! still.isValid() || std::abs (scale - stillScale) > 1.0e-3f * scale || stillGeometry != shownGeometry.get()
        || ! juce::exactlyEqual (opening, stillOpening))
    {
        still = renderStill (scale);
        stillScale = scale;
        stillGeometry = shownGeometry.get();
        stillOpening = opening;
    }
    g.drawImageTransformed (still, juce::AffineTransform::scale (1.0f / scale));
}

// The background, and the mesh and surface lines at rest lightening it (max), at `scale` pixels
// per design unit. The hi-hat as in renderOpenGL, at its opening: the bottom cymbal, hidden inside
// the top cymbal's outline, the top cymbal, the clutch and rod's silhouettes clearing the lines
// behind them (to 0.08), and the clutch and rod lightening it all.
juce::Image CymbalView::renderStill (float scale) const
{
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
    const auto origin = area().getPosition().toDouble();
    const auto toColour = [] (wire::Rgb c) {
        const auto u8 = [] (float v) { return (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (v * 255.0f)); };
        return juce::Colour (u8 (c.r), u8 (c.g), u8 (c.b));
    };
    const auto blackLayer = [w, h] {
        juce::Image i (juce::Image::ARGB, w, h, true);
        juce::Graphics (i).fillAll (juce::Colours::black);
        return i;
    };
    const bool hiHat = shownLower != nullptr;
    const double lift = hiHat ? (double) shownOpening * wire::kHiHatMaxGap : 0.0;

    // A cymbal at rest, raised by `lift`: its mesh into `layer`; its surface lines over one another
    // in their order, over the mesh on the dented mesh, else into a layer of their own that the
    // mesh lightens (as in renderOpenGL).
    juce::Image layer = blackLayer(), surfaceLayer;
    const auto drawCymbal = [&] (const wire::Geometry& geo, double raise) {
        wire::Frame f;
        f.resize (geo);
        f.lift = raise;
        std::vector<float> disp ((size_t) geo.numVertices(), 0.0f);
        wire::evaluate (geo, nullptr, 0, 0.0, f, disp);
        const auto& cam = geo.camera;
        std::vector<juce::Point<float>> at ((size_t) geo.numVertices());
        std::vector<double> shade ((size_t) geo.numVertices());
        for (int x = 0; x < geo.numVertices(); ++x)
        {
            double sx = 0.0, sy = 0.0;
            wire::projectVertex (geo, f, cam, x, sx, sy);
            at[(size_t) x] = { (float) (sx - origin.x), (float) (sy - origin.y) };
            shade[(size_t) x] = wire::shadeAt (geo, f, x);
        }
        const auto ytl = [&f, &origin] (double y) { return 1.0 - (y + origin.y - f.yTop) / std::max (f.yBottom - f.yTop, 1.0); };
        {
            juce::Graphics lg (layer);
            lg.addTransform (juce::AffineTransform::scale (scale));
            for (int k = 0; k < (int) geo.lineIndices.size() / 2; ++k)
            {
                const auto a = geo.lineIndices[(size_t) (2 * k)], b = geo.lineIndices[(size_t) (2 * k + 1)];
                lg.setColour (toColour (wire::segmentColour (geo, f, k, shade[a], ytl (at[a].y), shade[b], ytl (at[b].y))));
                lg.drawLine ({ at[a], at[b] }, 2 * k < geo.numThinIndices ? 1.0f : 2.0f);
            }
        }
        if (! geo.perVertexColour && ! surfaceLayer.isValid())
            surfaceLayer = blackLayer();
        {
            juce::Graphics lg (geo.perVertexColour ? layer : surfaceLayer);
            lg.addTransform (juce::AffineTransform::scale (scale));
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
                const double dent = 0.5 * (geo.surface[k].dent + geo.surface[k + 1].dent);
                lg.setColour (toColour (wire::lineDent ({ 0.5f * (c[0].r + c[1].r), 0.5f * (c[0].g + c[1].g), 0.5f * (c[0].b + c[1].b) }, dent)));
                lg.drawLine ({ p[0], p[1] }, geo.surface[k].width);
            }
        }
    };
    // A polygon in design units, filled black at `opacity` over the layer: what it hides keeps
    // 1 - opacity of its light.
    const auto hide = [&] (const juce::Path& path, float opacity) {
        juce::Graphics lg (layer);
        lg.addTransform (juce::AffineTransform::translation ((float) -origin.x, (float) -origin.y).scaled (scale));
        lg.setColour (juce::Colours::black.withAlpha (opacity));
        lg.fillPath (path);
    };
    juce::Image hardware;
    if (hiHat)
    {
        drawCymbal (*shownLower, 0.0);
        const auto& geo = *shownGeometry;
        {
            // the top cymbal's outline: its rim, raised
            wire::Frame f;
            f.resize (geo);
            f.lift = lift;
            std::vector<float> disp ((size_t) geo.numVertices(), 0.0f);
            wire::evaluate (geo, nullptr, 0, 0.0, f, disp);
            juce::Path rim;
            for (size_t k = 0; k < geo.rimVertices.size(); ++k)
            {
                double sx = 0.0, sy = 0.0;
                wire::projectVertex (geo, f, geo.camera, (int) geo.rimVertices[k], sx, sy);
                if (k == 0)
                    rim.startNewSubPath ((float) sx, (float) sy);
                else
                    rim.lineTo ((float) sx, (float) sy);
            }
            rim.closeSubPath();
            hide (rim, 1.0f);
        }
        drawCymbal (geo, lift);
        const double zb = geo.restHeight (0.0) + lift;
        const wire::Frame atRest; // no tilt or spin
        std::vector<std::array<float, 2>> hull;
        for (const auto& solid : wire::hiHatHardware().solids)
        {
            wire::silhouette (geo.camera, atRest, solid, zb, hull);
            juce::Path p;
            for (size_t k = 0; k < hull.size(); ++k)
                k == 0 ? p.startNewSubPath (hull[k][0], hull[k][1]) : p.lineTo (hull[k][0], hull[k][1]);
            p.closeSubPath();
            hide (p, 0.92f);
        }
        hardware = blackLayer();
        juce::Graphics hg (hardware);
        hg.addTransform (juce::AffineTransform::translation ((float) -origin.x, (float) -origin.y).scaled (scale));
        for (const auto& l : wire::hiHatHardware().lines)
        {
            double ax = 0.0, ay = 0.0, bx = 0.0, by = 0.0;
            wire::projectHardware (geo.camera, atRest, zb, l.x0, l.y0, l.z0, ax, ay);
            wire::projectHardware (geo.camera, atRest, zb, l.x1, l.y1, l.z1, bx, by);
            hg.setColour (toColour (l.colour));
            hg.drawLine ((float) ax, (float) ay, (float) bx, (float) by, l.width);
        }
    }
    else
        drawCymbal (*shownGeometry, 0.0);

    // Lighten: the brighter of the background and the lines, per channel.
    juce::Image::BitmapData out (image, juce::Image::BitmapData::readWrite);
    const auto lighten = [&out] (const juce::Image& lines) {
        const juce::Image::BitmapData in (lines, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < out.height; ++y)
            for (int x = 0; x < out.width; ++x)
            {
                const auto a = out.getPixelColour (x, y), b = in.getPixelColour (x, y);
                out.setPixelColour (x, y, juce::Colour (std::max (a.getRed(), b.getRed()), std::max (a.getGreen(), b.getGreen()),
                                                        std::max (a.getBlue(), b.getBlue())));
            }
    };
    lighten (layer);
    if (surfaceLayer.isValid())
        lighten (surfaceLayer);
    if (hardware.isValid())
        lighten (hardware);
    return image;
}

} // namespace cymbalis::ui
