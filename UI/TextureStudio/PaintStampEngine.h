/*============================================================================================================================================
                                                  PAINTSTAMPENGINE.H
============================================================================================================================================*/
// 🖌️ Native port of the Harbor Paint Workshop stamp engine (the physical instrument model developed in
//    _ClaudeScratch/painting-menu-server/HarborPaintWorkshop.html, builder Stages C-J): a CPU raster
//    surface (colour + height + tack canvases), three surface fields (wetness, loose dry pigment, marker
//    solvent), a per-event canvas sampler, coherent tooth noise, a drip system, and the eleven stamp
//    instruments - tuft paint brush, aerosol spray, dry media, pencil, alcohol marker, sand block,
//    ballistic splatter/splash (paint/blood/mud/water), dust-glitter-rust settling, soot, a medium-aware
//    eraser and the digital brush. Formulas and constants mirror the HTML reference; where the HTML used
//    sphere-exact projection (normalize + equirect worldToUV) this port uses the hit's local tangent
//    frame + UV Jacobian, which is the same math linearised - and what lets it run on the Teapot UDIMs.
//
// 📝 Scope, stated plainly: this is the STAMP logic only. The HTML workshop's three grid/particle fluid
//    systems (smoke fluid solver, stain thin-film grid, 3D pour beads) are not stamps and are not ported
//    here; soot covers the smoke tool's stamp path. The engine is surface-agnostic: callers supply hits
//    (point, normal, tangent frame, UV Jacobian). MakeSphereHit reproduces the HTML sphere exactly for
//    parity tests; the Texture Paint panel builds hits from its teapot topology instead.

#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <random>
#include <vector>

namespace Frontier
{

enum class StampTool { PaintBrush, Spray, Dry, Pencil, Marker, Sand, Splat, Dust, Soot, Eraser, Digital };
enum class BrushShape { Round, Flat, Filbert, Fan };
enum class PaintMedium { Oil, Acrylic, Water };
enum class SprayNozzle { Fat, Skinny };
enum class DryKind { Chalk, Pastel, Conte, Charcoal };
enum class MarkerNib { Chisel, Brush, Fine };
enum class SandMode { Linear, Circular };
enum class SplatLiquid { Paint, Blood, Mud, Water };
enum class DustKind { Dust, Glitter, Rust };

// 🎨 Every instrument's knobs, field-for-field with the HTML workshop's P object (defaults included).
struct StampParams
{
    // tuft paint brush
    BrushShape Shape = BrushShape::Round;
    PaintMedium Medium = PaintMedium::Oil;
    std::array<float, 3> Colour{196.f, 92.f, 42.f}; // #c45c2a, in 0..255 like the HTML math
    int Hairs = 72;
    float Radius = 0.026f; // tuft half-width at the ferrule (world units; HTML sphere: 1 unit = 100 mm)
    float Stiff = 0.45f, Splay = 1.1f, Press = 0.55f, Visc = 0.72f, Load = 0.85f;
    float Width = 1.2f, Thick = 0.7f, Drip = 0.65f, Alpha = 0.75f, Bloom = 0.18f;
    // spray can
    SprayNozzle Nozzle = SprayNozzle::Fat;
    std::array<float, 3> SprayColour{196.f, 92.f, 42.f};
    float SprayPressure = 0.55f, Standoff = 0.4f, Cone = 0.7f, SprayRuns = 0.35f;
    // dry media
    DryKind Dry = DryKind::Chalk;
    std::array<float, 3> DryColour{232.f, 220.f, 200.f};
    float DryHardness = 0.45f, DrySize = 0.028f, DrySide = 0.15f, DryPress = 0.5f, DryTooth = 0.55f, DryDust = 0.4f;
    // pencil
    float PencilGrade = 0.45f, PencilSize = 0.012f, PencilSide = 0.08f, PencilPress = 0.45f, PencilShine = 0.35f;
    // marker
    MarkerNib Nib = MarkerNib::Chisel;
    std::array<float, 3> MarkerColour{26.f, 58.f, 138.f};
    float MarkerSize = 0.028f, MarkerAngle = 0.25f, MarkerPress = 0.55f, MarkerBleed = 0.4f, MarkerLoad = 0.85f, MarkerInk = 0.85f;
    // sand
    SandMode SandMotion = SandMode::Linear;
    float SandGrit = 120.f, SandSize = 0.04f, SandPress = 0.55f;
    // splatter & splash
    SplatLiquid Liquid = SplatLiquid::Paint;
    std::array<float, 3> SplatColour{122.f, 16.f, 24.f};
    float SplatVolume = 0.55f, SplatBody = 0.45f, SplatForce = 0.85f, SplatSatellites = 0.65f;
    // dust / glitter / rust
    DustKind Particle = DustKind::Dust;
    std::array<float, 3> DustColour{196.f, 176.f, 137.f};
    float DustSize = 0.05f, DustRate = 0.75f, DustTack = 0.85f, DustGrain = 1.f;
    // soot
    std::array<float, 3> SootColour{28.f, 24.f, 20.f};
    float SootHeat = 0.65f, SootSpread = 0.55f, SootDensity = 0.5f, SootFlakes = 0.35f, SootWaves = 0.65f;
    // eraser
    float EraserSize = 0.035f, EraserStrength = 0.8f, EraserSoftness = 0.35f;
    // digital
    std::array<float, 3> DigitalColour{43.f, 74.f, 203.f};
    float DigitalSize = 0.03f, DigitalHardness = 0.8f, DigitalOpacity = 0.85f;
};

// 📍 One surface contact. Point/Normal/AxisX/AxisY are world-space; U,V are authoring UV (the engine
//    divides by its configured UV range); DUvDx/DUvDy are d(authoringUV) per world unit along each axis.
struct StampHit
{
    float Point[3]{0, 0, 0};
    float Normal[3]{0, 1, 0};
    float AxisX[3]{1, 0, 0}; // HTML "tan" before per-stamp velocity rotation
    float AxisY[3]{0, 0, 1}; // HTML "bit"
    float ViewDir[3]{0, 0, 1}; // surface -> camera (spray grazing rake)
    float U = 0, V = 0;
    float DUvDx[2]{0, 0};
    float DUvDy[2]{0, 0};
    float Pressure = 0.7f; // 0 = pen with no pressure (HTML treats as 0.7)
};

class PaintStampEngine
{
  public:
    PaintStampEngine();

    // 🧱 Surface setup. Canvas is WidthPx x HeightPx; fields are FieldW x FieldH; authoring UV spans
    //    [0,UvRangeU] x [0,UvRangeV] over the canvas (UDIM strip: UvRangeU = tile count). WrapU enables
    //    equirect-style seam copies (sphere parity); leave off for chart atlases.
    void Configure(int WidthPx, int HeightPx, int FieldW, int FieldH, float UvRangeU, float UvRangeV,
        std::array<float, 3> BaseColour, bool WrapU);
    void Clear() noexcept;
    void SetSeed(uint32_t Seed) { Rng.seed(Seed); }
    // 🌐 Optional surface normal lookup by authoring UV (dust settling / auto-settle realism).
    void ConfigureNormalLookup(std::function<bool(float U, float V, float Out[3])> Lookup) { NormalLookup = std::move(Lookup); }

    [[nodiscard]] StampParams &Parameters() noexcept { return P; }
    [[nodiscard]] const StampParams &Parameters() const noexcept { return P; }
    [[nodiscard]] bool HasPaint() const noexcept { return Painted; }
    [[nodiscard]] int CanvasWidth() const noexcept { return W; }
    [[nodiscard]] int CanvasHeight() const noexcept { return H; }

    // 🖌️ Stroke lifecycle (mirrors the HTML pointerdown/move/up contract).
    void BeginStroke(StampTool Tool);
    void Stamp(const StampHit &Hit, double TimeSeconds);
    void EndStroke();
    // ⏱️ Field relaxation: wetness/solvent drying, drips, dust auto-settle (call at ~60 Hz steps).
    void StepFields(double DeltaSeconds);
    void Dip();           // refill tuft ink to Load in the current colour
    void RebuildTuft();   // re-pack hairs for Shape/Hairs

    // 🔬 Readback (preview sampling + validation). U,V are authoring UV.
    [[nodiscard]] std::array<float, 4> SampleCanvas(float U, float V) const noexcept; // rgb 0..1 + coverage 0..1
    [[nodiscard]] float SampleHeight(float U, float V) const noexcept;                // film 0..1 above base
    [[nodiscard]] float WetAt(float U, float V) const noexcept;
    [[nodiscard]] float DryAt(float U, float V) const noexcept;
    [[nodiscard]] const std::vector<std::array<uint8_t, 4>> &ColourPixels() const noexcept { return ColourCanvas; }
    [[nodiscard]] const std::vector<uint8_t> &HeightPixels() const noexcept { return HeightCanvas; }
    [[nodiscard]] const std::vector<uint8_t> &CoveragePixels() const noexcept { return CoverageCanvas; }
    // 📊 Ballistic evidence from the last splat burst (mean flight of sub-mm vs multi-mm droplets).
    struct SplatStats { float MeanFlightSmall = 0, MeanFlightLarge = 0; int Droplets = 0; };
    [[nodiscard]] SplatStats LastSplatStats() const noexcept { return SplatEvidence; }
    [[nodiscard]] size_t DripCount() const noexcept { return Drips.size(); }

    // 🌐 HTML-parity sphere adapter: point on a sphere of Radius centred at origin, equirect UV.
    [[nodiscard]] static StampHit MakeSphereHit(const float Point[3], float Radius, float Pressure = 0.7f);
    static void SpherePointFromUv(float U, float V, float Radius, float Out[3]) noexcept;

  private:
    // ── surface ──────────────────────────────────────────────────────────────────────────────────
    int W = 1024, H = 1024, FW = 256, FH = 256;
    float RangeU = 1.f, RangeV = 1.f;
    std::array<float, 3> Base{212.f, 207.f, 200.f};
    bool WrapU = true;
    bool Painted = false;
    std::vector<std::array<uint8_t, 4>> ColourCanvas;
    std::vector<uint8_t> HeightCanvas;   // 128 = base coat, like the HTML bump canvas
    std::vector<uint8_t> TackCanvas;     // dust glue film (0 = dry)
    std::vector<uint8_t> CoverageCanvas; // how much stamp paint sits here (layer compositing)
    std::vector<float> WetField, DryField, SolventField;
    StampParams P;
    std::mt19937 Rng{1337};
    std::function<bool(float, float, float[3])> NormalLookup;

    // ── per-stamp frame (built from a StampHit, optionally velocity-rotated) ────────────────────
    struct Frame
    {
        float U = 0, V = 0;          // canvas UV of the hit (0..1 across the canvas)
        float Dx[2]{0, 0};           // d(canvasUV) per world unit along AxisX
        float Dy[2]{0, 0};           // ... along AxisY
        float AxisX[3]{1, 0, 0}, AxisY[3]{0, 0, 1}, Normal[3]{0, 1, 0}, Point[3]{0, 0, 0};
        float Pressure = 0.7f;
        float PxScale = 1.f;         // HTML px constants -> this canvas' px
    };
    [[nodiscard]] Frame MakeFrame(const StampHit &Hit) const noexcept;
    void RotateFrameToMotion(Frame &F, const float FromPoint[3]) const noexcept;
    [[nodiscard]] std::array<float, 2> UvAt(const Frame &F, float Ox, float Oy) const noexcept; // canvas UV
    void Downhill(const Frame &F, float &OutX, float &OutY, float &OutSlope) const noexcept;

    // ── CPU raster (Canvas2D equivalents: source-over / multiply, grey height strokes) ───────────
    enum class Blend { Over, Multiply };
    bool EraseCoverage = false; // eraser pass: colour ops reduce coverage instead of raising it
    void BlendPixel(int X, int Y, const std::array<float, 3> &Rgb, float Alpha, Blend Mode) noexcept;
    void HeightPixel(int X, int Y, float Grey, float Alpha) noexcept;
    void TackPixel(int X, int Y, float Grey, float Alpha) noexcept;
    void FillCirclePx(float Cx, float Cy, float R, const std::array<float, 3> &Rgb, float Alpha, Blend Mode) noexcept;
    void FillEllipsePx(float Cx, float Cy, float Rx, float Ry, float Rot, const std::array<float, 3> &Rgb, float Alpha, Blend Mode) noexcept;
    void FillPolygonPx(const std::vector<std::array<float, 2>> &Pts, const std::array<float, 3> &Rgb, float Alpha, Blend Mode) noexcept;
    void StrokeLinePx(float X0, float Y0, float X1, float Y1, float Width, const std::array<float, 3> &Rgb, float Alpha, Blend Mode) noexcept;
    void HeightCirclePx(float Cx, float Cy, float R, float Grey, float Alpha) noexcept;
    void HeightEllipsePx(float Cx, float Cy, float Rx, float Ry, float Rot, float Grey, float Alpha) noexcept;
    void HeightLinePx(float X0, float Y0, float X1, float Y1, float Width, float Grey, float Alpha) noexcept;
    void SoftDabPx(bool Height, float Cx, float Cy, float Rx, float Ry, const std::array<float, 3> &Rgb, float Alpha, float Inner) noexcept;
    void UvLine(float U0, float V0, float U1, float V1, const std::function<void(float, float, float, float)> &Draw) const; // canvas UV, seam aware

    // ── fields / noise / sampler ─────────────────────────────────────────────────────────────────
    [[nodiscard]] float FieldAt(const std::vector<float> &F, float U, float V) const noexcept; // canvas UV
    void FieldDisc(std::vector<float> &F, float U, float V, float RadiusUV, float Amount, bool Additive) noexcept;
    void WetDisc(float U, float V, float RadiusUV, float Amount) noexcept;
    void DryDisc(float U, float V, float RadiusUV, float Amount) noexcept;
    void SolventDisc(float U, float V, float RadiusUV, float Amount) noexcept;
    void WetSegment(float U0, float V0, float U1, float V1, float Amount) noexcept;
    [[nodiscard]] float Rand() noexcept;        // 0..1
    [[nodiscard]] float Gauss() noexcept;       // ~N(0,1), Box-Muller like the HTML spGauss
    [[nodiscard]] static float ToothAt(float U, float V) noexcept;   // cell hash (HTML verbatim)
    [[nodiscard]] static float ValueNoise(float X, float Y) noexcept;
    [[nodiscard]] static float ToothRidge(float U, float V) noexcept;
    [[nodiscard]] static std::array<float, 3> PigmentMix(const std::array<float, 3> &A, float MassA, const std::array<float, 3> &B, float MassB) noexcept;
    struct Sampler
    {
        int X0 = 0, Y0 = 0, Wd = 1, Ht = 1;
        std::vector<std::array<uint8_t, 4>> Colour;
        std::vector<uint8_t> Height;
        // returns {r,g,b in 0..255, film 0..1} at canvas UV - the HTML makeSampler contract
        [[nodiscard]] std::array<float, 4> Sample(const PaintStampEngine &Engine, float U, float V) const noexcept;
    };
    [[nodiscard]] Sampler MakeSampler(float MinU, float MinV, float MaxU, float MaxV) const; // canvas UV

    // ── drips (UV-space port of the HTML stepDrips; downhill captured at spawn) ──────────────────
    struct Drip
    {
        float U = 0, V = 0, PrevU = 0, PrevV = 0;
        bool HasPrev = false;
        float DirU = 0, DirV = 0;      // downhill in canvas UV per world unit (not normalised)
        float Slope = 0, Ny = 0;       // surface slope facts at spawn
        float WorldPerUv = 1.f;        // |dUV| -> world conversion at spawn
        std::array<float, 3> Colour{0, 0, 0};
        float Mass = 0.5f, Speed = 1.f, Width = 1.f, Travel = 0;
        int Delay = 0, Age = 0;
        bool FromSpray = false;
    };
    std::vector<Drip> Drips;
    void SpawnDrip(const Frame &F, float Ox, float Oy, const std::array<float, 3> &Colour, float Mass, float Speed, float Width, int Delay, bool FromSpray);
    void StepDrips() noexcept;

    // ── stroke state ─────────────────────────────────────────────────────────────────────────────
    StampTool Tool = StampTool::PaintBrush;
    bool InStroke = false;
    bool HaveLast = false;
    float LastPoint[3]{0, 0, 0};
    float LastU = 0, LastV = 0; // canvas UV of the previous stamp hit (stroke interpolation)
    float LastUvPerWorld = 0.25f;
    double LastTime = 0;
    // paint brush
    struct Hair
    {
        float Ox = 0, Oy = 0, Len = 1;
        float Ink = 0;
        std::array<float, 3> Colour{0, 0, 0};
        bool HasPrev = false;
        float PrevU = 0, PrevV = 0;
        float TipU = 0, TipV = 0;
        bool HasTip = false;
        float Seed = 0;
    };
    std::vector<Hair> Hairs;
    bool HaveYaw = false;
    float BrushYaw = 0, BrushZ = 0;
    bool FreshTouch = true;
    // spray state
    float SprayEnv = 0, CanHeat = 1, SprayAcc = 0;
    double SprayClock = 0;
    bool HaveSprayClock = false;
    // pencil / marker state
    float PenWear = 0;
    double PenClock = 0, MarkClock = 0;
    float MarkSeed = 0;
    // sand state
    struct SandGrain { float Ox, Oy, Depth, Phase, PrevU, PrevV; bool HasPrev; };
    std::vector<SandGrain> SandGrains;
    float SandOrbit = 0;
    // splat state (drag A->B, burst on EndStroke)
    bool SplatArmed = false;
    StampHit SplatA{}, SplatB{};
    SplatStats SplatEvidence{};
    // dust state
    bool HaveRustAnchor = false;
    float RustAnchorU = 0, RustAnchorV = 0;
    double SootClock = 0;

    // ── stamps ───────────────────────────────────────────────────────────────────────────────────
    void StampPaintBrush(const StampHit &Hit, double TimeSeconds);
    void DepositHair(Hair &H, float U, float V, const Sampler &S, float Press, float Speed, float Reach) noexcept;
    void StampSpray(const StampHit &Hit, double TimeSeconds);
    void StampDry(const StampHit &Hit, double TimeSeconds);
    void StampPencil(const StampHit &Hit, double TimeSeconds);
    void StampMarker(const StampHit &Hit, double TimeSeconds);
    void StampSand(const StampHit &Hit, double TimeSeconds);
    void SplatBurst(const StampHit &A, const StampHit &B);
    void SplatMark(const Frame &F, float Px, float Py, float Ang, float Len, float Wid, float Alpha, bool Ragged, float Film, bool Multiply, const std::array<float, 3> &Rgb) noexcept;
    void StampDust(const StampHit &Hit, double TimeSeconds);
    bool StickParticle(float U, float V, const float Normal[3], const Frame &F) noexcept; // canvas UV
    void StampSoot(const StampHit &Hit, double TimeSeconds);
    void StampEraser(const StampHit &Hit, double TimeSeconds);
    void StampDigital(const StampHit &Hit, double TimeSeconds);
    void AutoSettle() noexcept;

    [[nodiscard]] float TackAt(float U, float V) const noexcept; // canvas UV
    void EatTack(float U, float V, float Amount) noexcept;
};

} // namespace Frontier
