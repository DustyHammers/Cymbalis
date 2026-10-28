#pragma once

// The wireframe cymbal's model (docs/UI_SPEC.md section 11; the approved reference is
// docs/ui/wireframe_reference.py): the mesh and surface lines of a preset's shape, the camera,
// the colours, and the motion of the cymbal after hits. Plain C++, no JUCE: CymbalView draws it
// with OpenGL, or still with JUCE's drawing where OpenGL is not available.
//
// Units: the cymbal's radius is 1 (heights too are fractions of R); the screen is in design
// units of the 1536 x 1024 artwork.

#include "CymbalShapes.h"

#include <array>
#include <cstdint>
#include <vector>

namespace cymbalis::ui::wire
{

constexpr int kSamples = 80;   // samples around each ring
constexpr int kSpokeStep = 2;  // a spoke on every second sample: 40 spokes
constexpr int kModes = 21;     // plate modes m = 0 .. 6, n = 0 .. 2
constexpr int kMaxHits = 16;
constexpr double kHitLifeMs = 6000.0; // a hit moves the cymbal for 6 s

// Mode k = 3 m + n, with z the n-th zero of J_m.
struct Mode
{
    int m, n;
    double z;
};
const std::array<Mode, kModes>& modes();

// J_m(x) as (1/pi) times the integral over 0 .. pi of cos(m t - x sin t), a 64-point midpoint sum.
double besselJ (int m, double x);

// The camera of the reference's project(): elevation 38 degrees, centre 787,400, 345 design units
// per cymbal radius, perspective distance 5 radii.
struct Camera
{
    static constexpr double elevationDeg = 38.0, cx = 787.0, cy = 400.0, scale = 345.0, distance = 5.0;
    double sinE, cosE;
    Camera();
    // Screen position (design units) of the point X, Y, Z.
    void project (double X, double Y, double Z, double& sx, double& sy) const noexcept;
};

// Colours, RGB 0 .. 1.
struct Rgb
{
    float r, g, b;
};
namespace colour
{
constexpr Rgb roseDark { 0x70 / 255.0f, 0x4a / 255.0f, 0x3a / 255.0f };
constexpr Rgb roseLight { 0xde / 255.0f, 0xb0 / 255.0f, 0x96 / 255.0f };
constexpr Rgb lathe { 0xcd / 255.0f, 0xa0 / 255.0f, 0x6e / 255.0f };
constexpr Rgb gold { 0xec / 255.0f, 0xc8 / 255.0f, 0x8c / 255.0f };
constexpr Rgb hot { 0xff / 255.0f, 0xee / 255.0f, 0xd7 / 255.0f };
constexpr Rgb flash { 0xff / 255.0f, 0xf5 / 255.0f, 0xe6 / 255.0f };
} // namespace colour

// What a line is made of at its radius (the reference's colour() zones, in its order): a tint
// zone (gold metal, no lathe lines), a gold zone (gold lathing), a lathe zone, or plain bronze.
enum class Metal : std::uint8_t
{
    plain = 0,
    lathe = 1,
    gold = 2,
    tint = 3
};

// The colour of a line (the reference's colour(), and Cymbal2's dent light): the light from its
// shade (the normal against the light) and ytl (1 at the cymbal's top on screen, 0 at its
// bottom); its metal, `factor` being lum(r) x the zone's strength for gold and lum(r) for tint;
// the dent light factor `dent` (1: no dent); and the glow g that mixes it toward HOT.
Rgb bronze (double shade, double ytl, Metal metal, double factor, double dent = 1.0, double glow = 0.0) noexcept;

enum class Zone : std::uint8_t
{
    bell,
    bow,
    edge
};

// A surface line vertex (pairs are segments). They follow the cymbal's tilt and spin, not its
// flex, at the profile's height + 0.0015.
enum class SurfaceKind : std::uint8_t
{
    hammer = 0,    // the surface colour x param (0.8; 1.0 for the spiral)
    lathe = 1,     // the surface colour x (0.30 + 0.25 ytl)
    groove = 2,    // GOLD x (0.30 + 0.32 ytl) x param (the groove's intensity); 2 units
    edge = 3,      // GOLD x (0.55 + 0.40 ytl) x param (the zone's strength); 2 units
    goldLathe = 4, // the surface colour x (0.50 + 0.35 ytl) x param (lum(r))
    goldLine = 5   // GOLD x (0.50 + 0.35 ytl) x param (lum(r) x strength): the dented mesh's lathing
};
struct SurfaceVertex
{
    float r, theta, z;
    float kind, param;
    float metal, factor; // the surface's Metal and factor there (see bronze)
};

// Everything about a preset's cymbal that does not move: built once per preset.
class Geometry
{
public:
    explicit Geometry (const CymbalShape& shape);

    const CymbalShape& shape;
    double bell;

    // The profile's height at radius fraction r (clipped to 0 .. 1): PCHIP through the points,
    // and its slope.
    double height (double r) const noexcept;
    double slope (double r) const noexcept;
    // The photo's brightness at r (the reference's lum(): the table smoothed, divided by its 98th
    // percentile over entries 5 .. 79, clipped to 0.3 .. 1.15); 1 without a table.
    double lum (double r) const noexcept;
    // The metal at r, and its factor (see bronze).
    Metal metalAt (double r, double& factor) const noexcept;

    // The mesh: its vertices (radius, angle, rest height: the profile's, plus the dents; the
    // dent light factor f), and its line segments as vertex index pairs, the 1-unit lines first,
    // then the 2-unit lines (the rim, and the bell ring where shape.bellLine). Each segment's
    // metal and factor at its first and second end (segmentMetal[4 k ..]).
    std::vector<double> vertexR, vertexTheta, vertexZ;
    std::vector<float> vertexDent;
    int numVertices() const noexcept { return (int) vertexR.size(); }
    std::vector<std::uint32_t> lineIndices;
    int numThinIndices = 0;
    std::vector<float> segmentMetal;
    // How a segment is coloured: from its ends' averaged light at its middle's metal (the
    // reference's Cymbal), or as the average of its two ends' colours (Cymbal2, dented).
    bool perVertexColour = false;

    // The ring mesh (not dented): rings x kSamples vertices, vertex i * kSamples + j at rings[i],
    // 2 pi j / kSamples.
    std::vector<double> rings;
    int numRings = 0;
    int bellRing = 0; // the ring nearest the bell radius
    std::vector<double> ringHeight; // the profile's height at each ring
    std::vector<double> ringStiff;  // how much each ring flexes: clip((r - (bell - 0.1)) / 0.15, 0.12, 1)

    // The dented mesh (shape.dented): a fine grid of shape.dents.radial radii from 0.02 to 1 by
    // shape.dents.around angles; each vertex is one of its samples (vertexFine: radial index,
    // angle index), only those the simplified lines keep.
    std::vector<double> fineR;
    std::vector<double> fineStiff, fineStiffSlope, fineSlope; // stiffness, its slope, the profile's slope
    std::vector<std::uint16_t> vertexRadial, vertexAngle;
    int numDents = 0;
    int segmentsBeforeSimplifying = 0;

    // Surface lines as vertex pairs: 1-unit lines first, then the 2-unit ones (grooves, edges).
    std::vector<SurfaceVertex> surface;
    int numThinSurface = 0;

    // Per mode: its normalisation (the largest |J_m(z r)| for r in 0 .. 1), and J_m(z r) / that
    // at every ring (radial[k * numRings + i]); for the dented mesh at every fine radius, with
    // its slope along r (fineRadial, fineRadialSlope[k * fineR.size() + i]).
    std::array<double, kModes> norm {};
    std::vector<float> radial, fineRadial, fineRadialSlope;
    // cos and sin of m theta_j for m = 0 .. 6 and the samples around (kSamples, or
    // shape.dents.around when dented): cosM[m * around + j].
    std::vector<float> cosM, sinM;
    int around = kSamples;

private:
    std::vector<double> px, py, pd; // PCHIP knots (r / R, z / R) and slopes
    std::array<double, 200> lumTable {};
    bool hasLum = false;
    void buildLum();
    void buildMesh();
    void buildDentedMesh();
    void buildSurface();
    void buildModes();
    double strengthAt (double r) const noexcept;
};

// A strike, as the cymbal shows it.
struct Hit
{
    double startMs = 0.0;
    float velocity = 0.0f; // engine velocity 0 .. 1
    bool shoulder = false;
    Zone zone = Zone::bow;
    float angle = 0.0f;  // radians, clockwise on screen from 3 o'clock (the reference's 45 degrees +- 12)
    float radius = 0.0f; // fraction of R
    std::array<float, kModes> weight {}; // each mode's share, the largest 1
};

// Radius fraction of a strike position p (0 edge, 0.5 bow, 1 bell; UI_SPEC section 10):
// 1.0 -> 0.6 bell, 0.5 -> (bell + 0.95) / 2, 0.0 -> 0.97, linear between them.
double radiusFraction (double position, double bell) noexcept;

// A hit at position p, velocity v and angle (radians), its zone and mode weights (set_hits).
Hit makeHit (const Geometry& g, double startMs, double position, double velocity, bool shoulder, double angle);

// The flash duration of a hit, T = 0.25 + 0.25 v seconds.
inline double flashSeconds (double velocity) noexcept { return 0.25 + 0.25 * velocity; }

// The flash's shape, as the reference draws it: a filled disc of its radius blurred with a
// Gaussian of sigma 0.6 x the radius (0.75 at the centre, nothing left by 3 radii). As sigma
// follows the radius, the shape is the same at every radius: its brightness at u / 16 radii
// from the centre, u = 0 .. 48.
constexpr int kFlashProfileSize = 49;
const std::array<float, kFlashProfileSize>& flashProfile();

// The cymbal at one moment: per mesh vertex its height (rest height and flex), the slope of the
// smooth surface (profile and flex, no dents) along the radius and around (1/r d/dtheta) and its
// glow; the tilt and spin; the top and bottom of the cymbal on screen; and the flashes.
struct Frame
{
    std::vector<float> height, dzr, dzt, glow;
    std::vector<float> coef; // the dented mesh's flex per radius and m (scratch)
    double tiltX = 0.0, tiltY = 0.0, spinDeg = 0.0;
    double yTop = 0.0, yBottom = 1.0;
    struct Flash
    {
        float x, y;      // design units
        float radius;    // design units
        float intensity; // 0 .. 1, times the flash colour
    };
    std::array<Flash, kMaxHits> flashes {};
    int numFlashes = 0;
    bool moving = false; // some hit is younger than kHitLifeMs

    void resize (const Geometry& g);
};

// Evaluates the cymbal at nowMs with the hits (modal, wobble and flashes of the reference).
// `scratch` holds the flex; it and `out` are sized by Frame::resize first, so this allocates
// nothing.
void evaluate (const Geometry& g, const Hit* hits, int numHits, double nowMs, Frame& out,
               std::vector<float>& scratch) noexcept;

// The light per vertex (the reference's shade, of the smooth surface), from the frame's slopes
// and tilt: used by the still drawing; the OpenGL path does the same in its vertex shader.
double shadeAt (const Geometry& g, const Frame& f, int vertex) noexcept;
// Screen position of mesh vertex `vertex`, with the frame's tilt and spin.
void projectVertex (const Geometry& g, const Frame& f, const Camera& cam, int vertex, double& sx, double& sy) noexcept;
// The colour of mesh segment `k` (the vertex pair lineIndices[2k], [2k + 1]), given each end's
// shade and ytl.
Rgb segmentColour (const Geometry& g, const Frame& f, int k, double shadeA, double ytlA, double shadeB, double ytlB) noexcept;
// Screen position of a surface vertex, with the frame's tilt and spin.
void projectSurface (const SurfaceVertex& v, const Frame& f, const Camera& cam, double& sx, double& sy) noexcept;
// The colour of a surface vertex at ytl.
Rgb surfaceColour (const SurfaceVertex& v, double ytl) noexcept;

} // namespace cymbalis::ui::wire
