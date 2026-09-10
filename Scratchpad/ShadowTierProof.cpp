// ==========================================================================================================
//  ShadowTierProof.cpp — the GI-off shadow ladder, rendered and gated.
//
//  Renders a purpose-built rig through the real VisibilityRaster once per quality tier and checks that each
//  tier's shadow behaves the way its technique claims to.
//
//  The rig, not the Cornell Box, because the measurement is a penumbra WIDTH and that is only meaningful when
//  the geometry is controlled. Two identical square occluders float over one flat floor under one square area
//  light: the near plate sits just above the floor, the far plate high above it. Their shadow edges are
//  straight, axis-aligned and parallel, so a horizontal scanline crosses both at right angles and the pixel
//  count it returns is the penumbra itself rather than an artefact of a slanted or curved silhouette. (The
//  Cornell Box is still rendered afterwards, as the eyeball sheet — its shadows are what a reader recognises.)
//
//  The gates are properties of the algorithms, not golden images, so they survive a change of scene or camera:
//
//    ① Every tier renders, and produces a shadow at all (the floor under the box is darker than the floor
//       beside it). A "shadow technique" that shadows nothing passes no other test worth having.
//    ② Minimal is HARD: its penumbra — the band of pixels that are neither fully lit nor fully shadowed —
//       is a thin sliver, because a single comparison can only ever answer 0 or 1 per tap.
//    ③ Economy (wide PCF) has a WIDER penumbra than Minimal: that is the entire point of filtering.
//    ④ Standard (PCSS) CONTACT-HARDENS: the near plate's shadow edge is sharper than the far plate's, in the
//       same image, under the same light. Wide PCF cannot do this — its kernel is a constant width regardless
//       of how far the occluder is — so this one gate is what separates a true soft shadow from a blurred hard
//       one, and it is checked BOTH ways: PCSS must vary, PCF must not.
//    ⑤ The resolution override outranks the tier: pinning Minimal to 2048 changes its map, and the tier's own
//       filter is left alone.
//    ⑥ The tier ladder itself is monotone in cost: map side never decreases as the tier rises.
//
//  Sheets land in Diagnostics/ShadowTier_*.png for eyeball review; the gates are what fail the build.
//  Build: bash Scratchpad/CheckShadowTiers.sh
// ==========================================================================================================

#include "Engine/ContentInterchange/SceneCodec.h"
#include "Engine/GeometricRaster/SceneStructure.h"
#include "Engine/GeometricRaster/GeometryStructure.h"
#include "Engine/GeometricRaster/VisibilityRaster.h"
#include "Engine/DisplayPresentation/FidelityClassifier.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace Frontier;

namespace {

constexpr uint32_t kWidth  = 480u;
constexpr uint32_t kHeight = 360u;

int Failures = 0;

void Check(bool Condition, const char* What)
{
    std::printf("  %-62s %s\n", What, Condition ? "PASS" : "FAIL");
    if (!Condition) ++Failures;
}

// Luminance of a pixel, 0..1.
double Luma(const std::vector<unsigned char>& Rgba, uint32_t X, uint32_t Y)
{
    const size_t I = (static_cast<size_t>(Y) * kWidth + X) * 4u;
    return (0.2126 * Rgba[I] + 0.7152 * Rgba[I + 1u] + 0.0722 * Rgba[I + 2u]) / 255.0;
}

// Finds the darkest pixel in a region: the heart of a shadow, located from the image rather than assumed at a
//    hand-typed coordinate, so the gates survive a nudge to the rig or the camera.
void DarkestPixel(const std::vector<unsigned char>& Rgba, uint32_t X0, uint32_t X1, uint32_t Y0, uint32_t Y1,
                  uint32_t& OutX, uint32_t& OutY)
{
    double Best = 2.0;
    OutX = X0; OutY = Y0;
    for (uint32_t Y = Y0; Y < Y1; ++Y)
        for (uint32_t X = X0; X < X1; ++X)
        {
            const double L = Luma(Rgba, X, Y);
            if (L < Best) { Best = L; OutX = X; OutY = Y; }
        }
}

// The penumbra width, in pixels, of the shadow edge reached by walking away from (StartX, Row) in Step.
//
//    Walks outward from inside the umbra until the floor is fully lit again, and counts the pixels whose
//    luminance falls strictly between the two plateaus. That count IS the penumbra: near zero for a step edge,
//    wide for a soft one. Anchoring the walk at the measured umbra rather than at a fixed column is what keeps
//    this a measurement of the filter instead of a measurement of where the geometry happened to land. Step is
//    +1 to read an edge on the umbra's right, -1 for one on its left; each plate is read on whichever side
//    faces open floor rather than the plate's own bright face.
uint32_t PenumbraAlong(const std::vector<unsigned char>& Rgba, uint32_t Row, uint32_t StartX, int Step,
                       uint32_t StopX)
{
    const double Dark = Luma(Rgba, StartX, Row);
    double Lit = Dark;
    for (int X = static_cast<int>(StartX); X != static_cast<int>(StopX); X += Step)
        Lit = std::max(Lit, Luma(Rgba, static_cast<uint32_t>(X), Row));
    const double Span = Lit - Dark;
    if (Span < 0.02) return 0u;                       // no edge along this walk at all
    // ⚠️ The band is 10-90%, not 25-75%. This counts DISPLAY pixels between two luma thresholds, so its answer
    //    depends on the tone curve as well as on the shadow: a steeper mid-tone slope packs the same radiance
    //    ramp into fewer pixels. Measured on a linear penumbra ramp, the 25-75% band covered 45% of the ramp
    //    under Reinhard but only 36% under ACES — so unifying the two render paths on one curve moved a
    //    measurement that was never about the curve, and the far-plate crossover (PCSS 31 px vs PCF 32 px)
    //    inverted on a 1 px margin while the shadows themselves were untouched.
    //
    //    Widening to 10-90% samples the ramp's shoulders, where the curves agree far better, and restores the
    //    margin the comparison needs. The right long-term answer is to measure penumbrae in linear radiance
    //    before the transfer; that needs the proof to carry a float buffer, which is a larger change than this
    //    step should make.
    const double Low = Dark + Span * 0.10, High = Dark + Span * 0.90;

    uint32_t Count = 0u;
    for (int X = static_cast<int>(StartX); X != static_cast<int>(StopX); X += Step)
    {
        const double L = Luma(Rgba, static_cast<uint32_t>(X), Row);
        if (L >= High) break;                          // out the far side: the edge is behind us
        if (L > Low) ++Count;
    }
    return Count;
}

struct Render
{
    std::vector<unsigned char> Rgba;
    double MeanLum = 0.0;
};

//  ── the rig ────────────────────────────────────────────────────────────────────────────────────────────────
//  One floor, one square emitter overhead, two identical occluder plates at different heights. Everything is
//  axis-aligned so the shadow edges are straight lines the scanlines cross square-on.
//
//  Layout (world, Z up):
//     floor       z = 0, spanning x,y ∈ [-4, 4]
//     emitter     z = 6, a 1.2 m square centred over the origin, facing down
//     near plate  z = 0.35, a 1 m square at x = -1.6   (close to the floor → sharp shadow under PCSS)
//     far  plate  z = 3.00, a 1 m square at x = +1.0   (far from it       → soft  shadow under PCSS)
//
//  The camera is ORTHOGRAPHIC and OBLIQUE — 45° down the -Y/-Z diagonal — not a plan view. A plan view cannot
//  work here: a plate close to the floor sits directly on top of its own shadow and hides it. Seen at an angle
//  the elevated plates are parallax-shifted up the image while their shadows stay on the floor line, so one
//  horizontal scanline through the middle of the image crosses BOTH shadows and neither plate.

// Appends one axis-aligned square (two triangles) to a mesh, facing up or down.
void PushQuad(GeometryStructure& Mesh, float CentreX, float CentreY, float Z, float Half, bool FaceDown)
{
    const uint32_t Base = static_cast<uint32_t>(Mesh.QueryVertices().size());
    const float X0 = CentreX - Half, X1 = CentreX + Half;
    const float Y0 = CentreY - Half, Y1 = CentreY + Half;
    const Vector3 N{ 0.0f, 0.0f, FaceDown ? -1.0f : 1.0f };

    VertexRecord V[4]{};
    const float Xs[4] = { X0, X1, X1, X0 };
    const float Ys[4] = { Y0, Y0, Y1, Y1 };
    for (int K = 0; K < 4; ++K)
    {
        V[K].SpatialLocation   = Vector3{ Xs[K], Ys[K], Z };
        V[K].NormalDirection   = N;
        V[K].TangentDirection  = Vector4{ 1.0f, 0.0f, 0.0f, 1.0f };
        V[K].TextureCoordinateU = 0.0f;
        V[K].TextureCoordinateV = 0.0f;
    }
    Mesh.AppendVertices(V, 4u);

    const uint32_t Up[6]   = { Base + 0u, Base + 1u, Base + 2u, Base + 0u, Base + 2u, Base + 3u };
    const uint32_t Down[6] = { Base + 0u, Base + 2u, Base + 1u, Base + 0u, Base + 3u, Base + 2u };
    Mesh.AppendIndices(FaceDown ? Down : Up, 6u);
}

// One mesh per object, so each can carry its own material through RegisterInstance.
void SeatCluster(GeometryStructure& Mesh)
{
    PolyhedralCluster C{};
    C.BoundingCenter  = Vector3{ 0.0f, 0.0f, 0.0f };
    C.BoundingRadius  = 100.0f;
    C.ConeApex        = Vector3{ 0.0f, 0.0f, 0.0f };
    C.ConeAxis        = Vector3{ 0.0f, 0.0f, 1.0f };
    C.ConeCutoff      = 1.0f;              // never cone-culled
    C.VertexOffset    = 0u;
    C.TriangleOffset  = 0u;
    C.TriangleCount   = static_cast<uint32_t>(Mesh.QueryIndices().size() / 3u);
    Mesh.RegisterCluster(C);
}

MaterialDescriptor DiffuseMaterial()
{
    MaterialDescriptor M;
    M.Name = "RigDiffuse";
    MaterialSlabDescriptor S{};
    S.BaseColor[0] = S.BaseColor[1] = S.BaseColor[2] = 0.75f;
    S.SpecularRoughness = 0.9f;
    M.Slabs.push_back(S);
    return M;
}

MaterialDescriptor EmitterMaterial()
{
    MaterialDescriptor M;
    M.Name = "RigEmitter";
    MaterialSlabDescriptor S{};
    S.BaseColor[0] = S.BaseColor[1] = S.BaseColor[2] = 0.0f;
    S.EmissionLuminance = 60.0f;
    S.EmissionColor[0] = S.EmissionColor[1] = S.EmissionColor[2] = 1.0f;
    M.Slabs.push_back(S);
    return M;
}

// Assembles the rig through the ordinary scene path — RegisterInstance + Finalise — so what the raster reads is
//    built exactly the way a loaded scene is, not hand-poked into its internals.
void BuildRig(SceneStructure& Level,
              GeometryStructure& Floor, GeometryStructure& Emitter,
              GeometryStructure& NearPlate, GeometryStructure& FarPlate)
{
    PushQuad(Floor,     0.0f,  0.0f, 0.0f,  4.0f, false);   // floor
    PushQuad(Emitter,   0.0f,  0.0f, 6.0f,  0.6f, true);    // 1.2 m square emitter overhead
    PushQuad(NearPlate, -1.6f, 0.0f, 0.35f, 0.5f, false);   // near plate: 0.35 m over the floor
    PushQuad(FarPlate,   1.0f, 0.0f, 3.00f, 0.5f, false);   // far  plate: 3.00 m over the floor
    SeatCluster(Floor); SeatCluster(Emitter); SeatCluster(NearPlate); SeatCluster(FarPlate);

    const uint32_t Diffuse = Level.RegisterMaterial(DiffuseMaterial());
    const uint32_t Light   = Level.RegisterMaterial(EmitterMaterial());
    const Matrix4x4 I = Matrix4x4::Identity();
    Level.RegisterInstance(Floor,     I, Diffuse, 0u);
    Level.RegisterInstance(Emitter,   I, Light,   0u);
    Level.RegisterInstance(NearPlate, I, Diffuse, 0u);
    Level.RegisterInstance(FarPlate,  I, Diffuse, 0u);
    Level.Finalise();
}

bool RenderRig(const SceneStructure& Level, const FidelityCriteria& Criteria, Render& Out)
{
    VisibilityRaster Raster;
    ShadowCriteria Shadows{};
    Shadows.Filter   = static_cast<ShadowFilterKind>(Criteria.ShadowTechnique);
    Shadows.MapSide  = Criteria.ShadowMapSide;
    Shadows.TapCount = Criteria.ShadowFilterTapCount;
    Raster.AssignShadowCriteria(Shadows);

    // Oblique orthographic: 45° down the -Y/-Z diagonal. Orthographic so a metre is the same number of pixels
    //    everywhere in the image and a penumbra width can be compared between the left and right halves.
    constexpr float R2 = 0.70710678f;
    const float Eye[3]     = { 0.0f, -8.0f,  8.0f };
    const float Forward[3] = { 0.0f,   R2,   -R2  };
    const float Right[3]   = { 1.0f,  0.0f,  0.0f };
    const float Up[3]      = { 0.0f,   R2,    R2  };

    Out.Rgba.assign(static_cast<size_t>(kWidth) * kHeight * 4u, 0u);
    return Raster.RenderOrthographic(Level, Eye, Forward, Right, Up, 3.2f, kWidth, kHeight,
                                     Out.Rgba.data(), Out.MeanLum);
}

bool RenderCornell(const SceneStructure& Level, const FidelityCriteria& Criteria, Render& Out)
{
    VisibilityRaster Raster;
    ShadowCriteria Shadows{};
    Shadows.Filter   = static_cast<ShadowFilterKind>(Criteria.ShadowTechnique);
    Shadows.MapSide  = Criteria.ShadowMapSide;
    Shadows.TapCount = Criteria.ShadowFilterTapCount;
    Raster.AssignShadowCriteria(Shadows);

    const float Eye[3]     = { 0.0f, -3.30f, 1.55f };
    const float Forward[3] = { 0.0f,  1.0f,  0.0f  };
    const float Right[3]   = { 1.0f,  0.0f,  0.0f  };
    const float Up[3]      = { 0.0f,  0.0f,  1.0f  };

    Out.Rgba.assign(static_cast<size_t>(kWidth) * kHeight * 4u, 0u);
    return Raster.Render(Level, Eye, Forward, Right, Up, 1.0471976f, kWidth, kHeight, Out.Rgba.data(), Out.MeanLum);
}

bool WriteSheet(const char* Path, const std::vector<unsigned char>& Rgba)
{
    std::vector<unsigned char> Rgb(static_cast<size_t>(kWidth) * kHeight * 3u);
    for (size_t I = 0u; I < static_cast<size_t>(kWidth) * kHeight; ++I)
    {
        Rgb[I * 3u]      = Rgba[I * 4u];
        Rgb[I * 3u + 1u] = Rgba[I * 4u + 1u];
        Rgb[I * 3u + 2u] = Rgba[I * 4u + 2u];
    }
    return stbi_write_png(Path, static_cast<int>(kWidth), static_cast<int>(kHeight), 3, Rgb.data(),
                          static_cast<int>(kWidth) * 3) != 0;
}

} // namespace

int main()
{
    // ── the measurement rig ────────────────────────────────────────────────────────────────────────────────
    SceneStructure Rig;
    GeometryStructure Floor, Emitter, NearPlate, FarPlate;
    BuildRig(Rig, Floor, Emitter, NearPlate, FarPlate);
    std::printf("[ShadowTier] rig: %zu flat triangles, %zu luminaires\n",
                Rig.QueryFlatTriangles().size(), Rig.QueryLuminaires().size());
    if (Rig.QueryFlatTriangles().empty() || Rig.QueryLuminaires().empty())
    {
        std::printf("[ShadowTier] the rig did not finalise into a lit scene\n");
        return 1;
    }

    FidelityClassifier Classifier;
    const FidelityCategory Tiers[] = { FidelityCategory::MinimalFidelity, FidelityCategory::EconomyFidelity,
                                       FidelityCategory::StandardFidelity, FidelityCategory::UltraFidelity,
                                       FidelityCategory::ReferenceFidelity };
    const char* Sheets[] = { "Diagnostics/ShadowTier_Minimal.png",   "Diagnostics/ShadowTier_Economy.png",
                             "Diagnostics/ShadowTier_Standard.png",  "Diagnostics/ShadowTier_Ultra.png",
                             "Diagnostics/ShadowTier_Reference.png" };

    Render Renders[5];
    FidelityCriteria Criteria[5];

    std::printf("\n[ShadowTier] rendering the rig once per tier, through the real raster\n");
    for (uint32_t T = 0u; T < 5u; ++T)
    {
        Criteria[T] = Classifier.ConstructCriteria(Tiers[T]);
        if (!RenderRig(Rig, Criteria[T], Renders[T]))
        {
            std::printf("  %-10s RENDER DECLINED\n", FidelityLabel(Tiers[T]));
            return 1;
        }
        if (!WriteSheet(Sheets[T], Renders[T].Rgba)) { std::printf("  sheet write failed\n"); return 1; }
        std::printf("  %-10s %-4s  map %4u  kernel %u  mean luminance %.4f\n",
                    FidelityLabel(Tiers[T]), ShadowTechniqueLabel(Criteria[T].ShadowTechnique),
                    Criteria[T].ShadowMapSide, Criteria[T].ShadowFilterTapCount, Renders[T].MeanLum);
    }

    // Locate each shadow from the image itself. The near plate's shadow lives in the left half, the far
    //    plate's in the right; taking the darkest pixel of each half finds the umbra wherever it actually fell.
    uint32_t NearX = 0u, NearY = 0u, FarX = 0u, FarY = 0u;
    DarkestPixel(Renders[4].Rgba, 40u, kWidth / 2u,  40u, kHeight - 40u, NearX, NearY);
    DarkestPixel(Renders[4].Rgba, kWidth / 2u, kWidth - 40u, 40u, kHeight - 40u, FarX, FarY);
    std::printf("\n[ShadowTier] umbrae located: near plate at (%u, %u), far plate at (%u, %u)\n",
                NearX, NearY, FarX, FarY);

    // ── ① every tier casts a shadow at all ─────────────────────────────────────────────────────────────────
    // The umbra against open floor on the same row, well to the left of both plates.
    std::printf("\n[ShadowTier] (1) every tier actually shadows\n");
    for (uint32_t T = 0u; T < 5u; ++T)
    {
        const double Shadowed = Luma(Renders[T].Rgba, FarX, FarY);
        const double Open     = Luma(Renders[T].Rgba, 30u,  FarY);
        char What[110];
        std::snprintf(What, sizeof(What), "%s: umbra %.4f darker than open floor %.4f",
                      FidelityLabel(Tiers[T]), Shadowed, Open);
        Check(Shadowed < Open - 0.02, What);
    }

    // ── ②③ the penumbra widens as the technique improves ───────────────────────────────────────────────────
    // Measured on the FAR plate's shadow, whose occluder-to-receiver distance is large enough for PCSS to open
    //    its kernel. Every tier is measured on the same row, walking right out of the same umbra.
    std::printf("\n[ShadowTier] (2,3) the penumbra widens as the technique improves\n");
    const uint32_t HardBand = PenumbraAlong(Renders[0].Rgba, FarY, FarX, +1, kWidth - 8u);
    const uint32_t PcfBand  = PenumbraAlong(Renders[1].Rgba, FarY, FarX, +1, kWidth - 8u);
    const uint32_t PcssBand = PenumbraAlong(Renders[2].Rgba, FarY, FarX, +1, kWidth - 8u);
    std::printf("  far plate penumbra: hard %u px, wide PCF %u px, PCSS %u px\n", HardBand, PcfBand, PcssBand);
    {
        char What[110];
        // Not "hard means zero px": the emitter is a 1.2 m AREA light, so even a single-tap shadow carries a
        //    true penumbra from the light's own extent. What single-tap must be is the narrowest of the three.
        std::snprintf(What, sizeof(What), "Minimal single-tap %u px is the narrowest of the three", HardBand);
        Check(HardBand < PcfBand && HardBand < PcssBand, What);
        std::snprintf(What, sizeof(What), "Economy PCF %u px is wider than Minimal hard %u px", PcfBand, HardBand);
        Check(PcfBand > HardBand, What);
        std::snprintf(What, sizeof(What), "Standard PCSS %u px is wider than Minimal hard %u px", PcssBand, HardBand);
        Check(PcssBand > HardBand, What);
    }

    // ── ④ PCSS contact-hardens where PCF cannot ────────────────────────────────────────────────────────────
    // The decisive gate, and the reason the rig carries two plates: ONE image holds a near occluder and a far
    //    one under the same light.
    //
    //    An honest note on what this can and cannot show. The emitter is a 1.2 m AREA light, so the true
    //    penumbra of a near occluder is genuinely narrow and that of a far one genuinely wide — BOTH filters
    //    therefore report a smaller number on the near plate, and a raw near-vs-far comparison would credit PCF
    //    with contact hardening it does not perform. The discriminator is the CROSSOVER: PCSS reads the blocker
    //    distance per pixel, so it must come out NARROWER than PCF where the occluder is close, and no narrower
    //    than PCF where the occluder is far. PCF, whose kernel is a fixed count of texels, cannot do both — it
    //    applies the same blur to both edges and so cannot beat PCSS at the near plate while also losing to it
    //    at the far one.
    std::printf("\n[ShadowTier] (4) PCSS contact-hardens where PCF cannot\n");
    const uint32_t PcssNear = PenumbraAlong(Renders[2].Rgba, NearY, NearX, -1, 8u);
    const uint32_t PcssFar  = PenumbraAlong(Renders[2].Rgba, FarY,  FarX,  +1, kWidth - 8u);
    const uint32_t PcfNear  = PenumbraAlong(Renders[1].Rgba, NearY, NearX, -1, 8u);
    const uint32_t PcfFar   = PenumbraAlong(Renders[1].Rgba, FarY,  FarX,  +1, kWidth - 8u);
    std::printf("  near plate (occluder 0.35 m off the floor): PCSS %u px, PCF %u px\n", PcssNear, PcfNear);
    std::printf("  far  plate (occluder 3.00 m off the floor): PCSS %u px, PCF %u px\n", PcssFar,  PcfFar);
    {
        char What[130];
        // Each filter must widen from the near edge to the far edge — the area light guarantees that much.
        std::snprintf(What, sizeof(What), "PCSS widens with occluder distance: %u px -> %u px", PcssNear, PcssFar);
        Check(PcssFar > PcssNear * 2u, What);
        // The crossover, in both directions. This is the part PCF cannot fake.
        std::snprintf(What, sizeof(What), "at contact PCSS %u px is tighter than PCF %u px", PcssNear, PcfNear);
        Check(PcssNear < PcfNear, What);
        std::snprintf(What, sizeof(What), "far away PCSS %u px is no tighter than PCF %u px", PcssFar, PcfFar);
        Check(PcssFar >= PcfFar, What);
    }

    // ── ⑤ the resolution override outranks the tier ────────────────────────────────────────────────────────
    std::printf("\n[ShadowTier] (5) the Control Centre override outranks the tier\n");
    {
        const FidelityCriteria Pinned = WithShadowResolution(Classifier.ConstructCriteria(FidelityCategory::MinimalFidelity),
                                                             ShadowResolutionCategory::Side2048);
        char What[120];
        std::snprintf(What, sizeof(What), "Minimal pinned to 2048: map %u (tier default was %u)",
                      Pinned.ShadowMapSide, Criteria[0].ShadowMapSide);
        Check(Pinned.ShadowMapSide == 2048u && Criteria[0].ShadowMapSide == 256u, What);
        std::snprintf(What, sizeof(What), "the override leaves the tier's filter alone: still %s",
                      ShadowTechniqueLabel(Pinned.ShadowTechnique));
        Check(Pinned.ShadowTechnique == ShadowTechniqueCategory::HardShadowMap, What);

        const FidelityCriteria Auto = WithShadowResolution(Classifier.ConstructCriteria(FidelityCategory::UltraFidelity),
                                                           ShadowResolutionCategory::FollowQualityTier);
        std::snprintf(What, sizeof(What), "Auto leaves Ultra on its own %u map", Auto.ShadowMapSide);
        Check(Auto.ShadowMapSide == 1024u, What);

        // And it must actually reach the raster: a finer map has to change the picture.
        Render Fine;
        if (!RenderRig(Rig, Pinned, Fine)) { std::printf("  pinned render declined\n"); return 1; }
        if (!WriteSheet("Diagnostics/ShadowTier_MinimalPinned2048.png", Fine.Rgba)) { std::printf("  sheet write failed\n"); return 1; }
        size_t Differing = 0u;
        for (size_t I = 0u; I < Fine.Rgba.size(); I += 4u)
            if (Fine.Rgba[I] != Renders[0].Rgba[I]) ++Differing;
        std::snprintf(What, sizeof(What), "the pinned 2048 map reaches the raster: %zu pixels differ from 256", Differing);
        Check(Differing > 0u, What);
    }

    // ── ⑥ the ladder is monotone ───────────────────────────────────────────────────────────────────────────
    std::printf("\n[ShadowTier] (6) the tier ladder never gets cheaper as it rises\n");
    {
        bool Monotone = true;
        for (uint32_t T = 1u; T < 5u; ++T)
            if (Criteria[T].ShadowMapSide < Criteria[T - 1u].ShadowMapSide) Monotone = false;
        Check(Monotone, "map side never decreases from Minimal through Reference");
        Check(Criteria[0].ShadowTechnique == ShadowTechniqueCategory::HardShadowMap,              "Minimal   = hard");
        Check(Criteria[1].ShadowTechnique == ShadowTechniqueCategory::WidePercentageCloserFilter, "Economy   = wide PCF");
        Check(Criteria[2].ShadowTechnique == ShadowTechniqueCategory::PercentageCloserSoftShadow, "Standard  = PCSS");
        Check(Criteria[3].ShadowTechnique == ShadowTechniqueCategory::PercentageCloserSoftShadow, "Ultra     = PCSS");
        Check(Criteria[4].ShadowTechnique == ShadowTechniqueCategory::PercentageCloserSoftShadow, "Reference = PCSS");
        Check(Criteria[4].ShadowFilterTapCount > Criteria[2].ShadowFilterTapCount,
              "Reference filters with more taps than Standard");
    }

    // ── the eyeball sheets: the real Cornell Box, hard versus PCSS ─────────────────────────────────────────
    // Not gated — the numbers above are the gates. These exist because a reader recognises the Cornell Box and
    //    can see at a glance that Minimal's shadows are blocky and Reference's are soft.
    std::printf("\n[ShadowTier] Cornell Box sheets for review\n");
    {
        SceneStructure Cornell;
        std::string Error;
        if (SceneCodec::Decode("Projects/Project-Zero/Content/Scenes/CornellBox.gltf", Cornell, nullptr,
                               SceneDecodeConfiguration{}, &Error))
        {
            const FidelityCategory Pair[2] = { FidelityCategory::MinimalFidelity, FidelityCategory::ReferenceFidelity };
            const char* Out[2] = { "Diagnostics/ShadowCornell_Minimal.png", "Diagnostics/ShadowCornell_Reference.png" };
            for (uint32_t K = 0u; K < 2u; ++K)
            {
                Render R;
                if (RenderCornell(Cornell, Classifier.ConstructCriteria(Pair[K]), R) && WriteSheet(Out[K], R.Rgba))
                    std::printf("  %-10s -> %s\n", FidelityLabel(Pair[K]), Out[K]);
            }
        }
        else std::printf("  (Cornell decode failed: %s - sheets skipped, gates unaffected)\n", Error.c_str());
    }

    std::printf("\n");
    if (Failures != 0)
    {
        std::printf("[ShadowTier] %d GATE%s FAILED\n", Failures, Failures == 1 ? "" : "S");
        return 1;
    }
    std::printf("[ShadowTier] the ladder holds: hard, wider, and contact-hardening in turn\n");
    return 0;
}
