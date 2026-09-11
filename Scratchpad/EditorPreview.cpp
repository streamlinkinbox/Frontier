//============================================================================================================================================
//                                                    EDITORPREVIEW.CPP
//============================================================================================================================================
// 🧩 Headless editor preview — the development editor over the LIVE Cornell level: the feed fills the roster from
//    the fresh glTF, the Tall Box carries the pick, and the viewport shows the level traced on the CPU through
//    the same traversal the renderer refits. Eight sheets out (shut, picked camera, open, raster, shut raster, hub, ortho front, top). No Vulkan,
//    no GLFW, no window. Run via Scratchpad/CheckEditorPreview.sh.

#ifndef FRONTIER_DEVELOPMENT
#error "the preview must define FRONTIER_DEVELOPMENT, or the editor records nothing"
#endif
#include <imgui.h>

#include "EditorHost.h"
#include "PngWriteShim.h"

#include "Projects/Project-Zero/Source/EditorFeedSequence.h"
#include "Engine/ContentInterchange/SceneCodec.h"
#include "Engine/GeometricRaster/TraversalIndex.h"
#include "Engine/GeometricRaster/VisibilityRaster.h"
#include "Engine/DisplayPresentation/TypefaceRegistry.h"
#include "Engine/DisplayPresentation/FidelityClassifier.h"
#include "Engine/DisplayPresentation/CelestialTier.h"
#include "Projects/Project-Zero/Source/CelestialSequence.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static_assert(sizeof(ImDrawIdx) == 2u, "the rasteriser below walks 16-bit indices");

namespace {

constexpr int kWidth  = 1280;
constexpr int kHeight = 720;
constexpr int kSamples = 64;

constexpr unsigned char kGround[3] = { 5u, 5u, 5u };   // Frontier ground #050505

struct Rgba
{
    float R, G, B, A;
};

Rgba UnpackColour(uint32_t Packed) noexcept
{
    return { static_cast<float>(Packed & 0xFFu) / 255.0f,
             static_cast<float>((Packed >> 8) & 0xFFu) / 255.0f,
             static_cast<float>((Packed >> 16) & 0xFFu) / 255.0f,
             static_cast<float>((Packed >> 24) & 0xFFu) / 255.0f };
}

void OverlayPixel(unsigned char* Pixel, Rgba Over) noexcept
{
    const float Keep = 1.0f - Over.A;
    Pixel[0] = static_cast<unsigned char>(Over.R * 255.0f * Over.A + static_cast<float>(Pixel[0]) * Keep + 0.5f);
    Pixel[1] = static_cast<unsigned char>(Over.G * 255.0f * Over.A + static_cast<float>(Pixel[1]) * Keep + 0.5f);
    Pixel[2] = static_cast<unsigned char>(Over.B * 255.0f * Over.A + static_cast<float>(Pixel[2]) * Keep + 0.5f);
}

Rgba SampleSheet(const unsigned char* Sheet, int SheetW, int SheetH, float U, float V) noexcept
{
    int X = static_cast<int>(U * static_cast<float>(SheetW));
    int Y = static_cast<int>(V * static_cast<float>(SheetH));
    if (X < 0) X = 0; if (X >= SheetW) X = SheetW - 1;
    if (Y < 0) Y = 0; if (Y >= SheetH) Y = SheetH - 1;
    const unsigned char* Texel = Sheet + (static_cast<size_t>(Y) * static_cast<size_t>(SheetW) + static_cast<size_t>(X)) * 4u;
    return { static_cast<float>(Texel[0]) / 255.0f, static_cast<float>(Texel[1]) / 255.0f,
             static_cast<float>(Texel[2]) / 255.0f, static_cast<float>(Texel[3]) / 255.0f };
}

float EdgeWeight(float Ax, float Ay, float Bx, float By, float Px, float Py) noexcept
{
    return (Px - Ax) * (By - Ay) - (Py - Ay) * (Bx - Ax);
}

// The proof's rasteriser, plus the viewport: draw commands carrying the view id sample the traced rows.
void RasterizeList(const ImDrawList* List, const unsigned char* GlyphSheet, int GlyphSheetWidth, int GlyphSheetHeight,
                   const unsigned char* ViewRgba, int ViewW, int ViewH, ImTextureID ViewId,
                   unsigned char* Pixels, ImVec2 Origin, ImVec2 PixelScale) noexcept
{
    const ImDrawVert* Corners = List->VtxBuffer.Data;
    const ImDrawIdx*  Order   = List->IdxBuffer.Data;
    for (int Command = 0; Command < List->CmdBuffer.Size; ++Command)
    {
        const ImDrawCmd* Cmd = &List->CmdBuffer[Command];
        // Either _TexData or _TexID is set, never both: the view arrives as a bare id (see AssignView),
        //    everything else through the atlas. GetTexID() asserts headless (nothing was ever "uploaded").
        const bool IsView = ViewRgba != nullptr && Cmd->TexRef._TexData == nullptr
            && Cmd->TexRef._TexID == ViewId;
        int ScissorLeft   = static_cast<int>((Cmd->ClipRect.x - Origin.x) * PixelScale.x);
        int ScissorTop    = static_cast<int>((Cmd->ClipRect.y - Origin.y) * PixelScale.y);
        int ScissorRight  = static_cast<int>((Cmd->ClipRect.z - Origin.x) * PixelScale.x);
        int ScissorBottom = static_cast<int>((Cmd->ClipRect.w - Origin.y) * PixelScale.y);
        if (ScissorLeft < 0) ScissorLeft = 0;
        if (ScissorRight > kWidth) ScissorRight = kWidth;
        if (ScissorTop < 0) ScissorTop = 0;
        if (ScissorBottom > kHeight) ScissorBottom = kHeight;

        for (unsigned int I = 0u; I < Cmd->ElemCount; I += 3u)
        {
            const ImDrawVert& A = Corners[Order[Cmd->IdxOffset + I] + Cmd->VtxOffset];
            const ImDrawVert& B = Corners[Order[Cmd->IdxOffset + I + 1u] + Cmd->VtxOffset];
            const ImDrawVert& C = Corners[Order[Cmd->IdxOffset + I + 2u] + Cmd->VtxOffset];
            const float SignedArea = EdgeWeight(A.pos.x, A.pos.y, B.pos.x, B.pos.y, C.pos.x, C.pos.y);
            if (SignedArea == 0.0f)
                continue;

            int LoX = static_cast<int>(std::floor(std::fmin(A.pos.x, std::fmin(B.pos.x, C.pos.x))));
            int HiX = static_cast<int>(std::ceil(std::fmax(A.pos.x, std::fmax(B.pos.x, C.pos.x))));
            int LoY = static_cast<int>(std::floor(std::fmin(A.pos.y, std::fmin(B.pos.y, C.pos.y))));
            int HiY = static_cast<int>(std::ceil(std::fmax(A.pos.y, std::fmax(B.pos.y, C.pos.y))));
            if (LoX < ScissorLeft) LoX = ScissorLeft; if (HiX > ScissorRight) HiX = ScissorRight;
            if (LoY < ScissorTop) LoY = ScissorTop; if (HiY > ScissorBottom) HiY = ScissorBottom;

            const Rgba TintedA = UnpackColour(A.col);
            const Rgba TintedB = UnpackColour(B.col);
            const Rgba TintedC = UnpackColour(C.col);
            const float InverseArea = 1.0f / SignedArea;
            for (int Y = LoY; Y < HiY; ++Y)
            {
                for (int X = LoX; X < HiX; ++X)
                {
                    const float Px = static_cast<float>(X) + 0.5f;
                    const float Py = static_cast<float>(Y) + 0.5f;
                    const float W0 = EdgeWeight(B.pos.x, B.pos.y, C.pos.x, C.pos.y, Px, Py) * InverseArea;
                    const float W1 = EdgeWeight(C.pos.x, C.pos.y, A.pos.x, A.pos.y, Px, Py) * InverseArea;
                    const float W2 = EdgeWeight(A.pos.x, A.pos.y, B.pos.x, B.pos.y, Px, Py) * InverseArea;
                    if (W0 < 0.0f || W1 < 0.0f || W2 < 0.0f)
                        continue;
                    const float U = W0 * A.uv.x + W1 * B.uv.x + W2 * C.uv.x;
                    const float V = W0 * A.uv.y + W1 * B.uv.y + W2 * C.uv.y;
                    const Rgba Glyph = IsView ? SampleSheet(ViewRgba, ViewW, ViewH, U, V)
                                              : SampleSheet(GlyphSheet, GlyphSheetWidth, GlyphSheetHeight, U, V);
                    const Rgba Tinted = { (W0 * TintedA.R + W1 * TintedB.R + W2 * TintedC.R) * Glyph.R,
                                          (W0 * TintedA.G + W1 * TintedB.G + W2 * TintedC.G) * Glyph.G,
                                          (W0 * TintedA.B + W1 * TintedB.B + W2 * TintedC.B) * Glyph.B,
                                          (W0 * TintedA.A + W1 * TintedB.A + W2 * TintedC.A) * Glyph.A };
                    OverlayPixel(&Pixels[(static_cast<size_t>(Y) * kWidth + static_cast<size_t>(X)) * 3u], Tinted);
                }
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          CPU TRACE
//------------------------------------------------------------------------------------------------------------------------

uint32_t XorShift(uint32_t& S) noexcept
{
    S ^= S << 13u; S ^= S >> 17u; S ^= S << 5u;
    return S;
}

float Rand01(uint32_t& S) noexcept { return static_cast<float>(XorShift(S)) / 4294967296.0f; }

struct LumiTri
{
    float A[3], B[3], C[3];
    float N[3];
    float Area;
    float Le[3];
};

// The level's emissive triangles as samplable luminaires; both the tracer and the raster draw from it.
void CollectLumi(const Frontier::SceneStructure& Level, std::vector<LumiTri>& Lumi)
{
    using namespace Frontier;
    const auto& Flat    = Level.QueryFlatTriangles();
    const auto& Records = Level.QueryMaterials().QueryRecords();

    for (size_t T = 0u; T < Flat.size(); ++T)
    {
        uint32_t Slot = 0u;
        std::memcpy(&Slot, &Flat[T].MaterialSlot, sizeof(Slot));
        if (Slot >= Records.size())
            continue;
        const MaterialRecord& R = Records[Slot];
        if (R.EmissiveR + R.EmissiveG + R.EmissiveB <= 0.0f)
            continue;
        LumiTri L{};
        L.A[0] = Flat[T].VertexAlphaX; L.A[1] = Flat[T].VertexAlphaY; L.A[2] = Flat[T].VertexAlphaZ;
        L.B[0] = Flat[T].VertexBetaX;  L.B[1] = Flat[T].VertexBetaY;  L.B[2] = Flat[T].VertexBetaZ;
        L.C[0] = Flat[T].VertexGammaX; L.C[1] = Flat[T].VertexGammaY; L.C[2] = Flat[T].VertexGammaZ;
        const float Ux = L.B[0] - L.A[0], Uy = L.B[1] - L.A[1], Uz = L.B[2] - L.A[2];
        const float Vx = L.C[0] - L.A[0], Vy = L.C[1] - L.A[1], Vz = L.C[2] - L.A[2];
        float Nx = Uy * Vz - Uz * Vy, Ny = Uz * Vx - Ux * Vz, Nz = Ux * Vy - Uy * Vx;
        const float Len = std::sqrt(Nx * Nx + Ny * Ny + Nz * Nz);
        if (Len <= 0.0f)
            continue;
        L.N[0] = Nx / Len; L.N[1] = Ny / Len; L.N[2] = Nz / Len;
        L.Area   = Len * 0.5f;
        L.Le[0]  = R.EmissiveR; L.Le[1] = R.EmissiveG; L.Le[2] = R.EmissiveB;
        Lumi.push_back(L);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                THE SKY THE PREVIEW SHOWS
//------------------------------------------------------------------------------------------------------------------------

// One celestial state for the preview, so the traced view and the raster view cannot show different weather.
//    Filled from CelestialSequence exactly as Project Zero fills it — the point of the preview is to show what
//    the project renders, not a second sky configured by hand here.
Frontier::AtmosphereMedium SkyMedium{};
Frontier::AtmosphereLight  SkyLight{};
Frontier::TwilightSettings SkyTwilight{};
float    SunElevationDegrees = 0.0f;
uint32_t SkySamples      = 16u;
uint32_t SkyLightSamples = 6u;

// The preview's own world. Prepared once at the first use; the same defaults Project Zero starts with, so the
//    sheet shows the weather somebody opening the editor would actually see.
Frontier::ProjectZero::CelestialSequence PreviewSky;
bool PreviewSkyReady = false;

void PreparePreviewSky() noexcept
{
    if (PreviewSkyReady) return;
    PreviewSky.Prepare();
    // A late-afternoon sun rakes the box and makes the sky's contribution legible; noon overhead would light
    //    the scene almost identically with or without an atmosphere, which would prove nothing.
    PreviewSky.Observation.LocalHours = 16.4f;
    const float Origin[3] = { 0.0f, 0.0f, 2.0f };
    PreviewSky.Tick(0.0f, Origin, 0.0f);
    PreviewSkyReady = true;
}

void SeatPreviewSky(const Frontier::ProjectZero::CelestialSequence& Sky, const Frontier::CelestialBudget& Budget) noexcept
{
    SkyMedium   = Sky.Medium;
    SkyLight    = Sky.Light;
    SkyTwilight = Sky.Twilight;
    for (int C = 0; C < 3; ++C) SkyLight.Direction[C] = Sky.Frame().Sun.Direction[C];
    SunElevationDegrees = Sky.Frame().Sun.Elevation;
    SkySamples      = Budget.AtmosphereSamples;
    SkyLightSamples = Budget.AtmosphereLightSamples;
}

// Next-event estimation over the level's own emissive triangles, two diffuse bounces, sky through the gaps.
void TraceView(const Frontier::SceneStructure& Level, const Frontier::TraversalIndex& Traversal,
               const Frontier::Vector3& Eye, const Frontier::Vector3& Forward,
               const Frontier::Vector3& Right, const Frontier::Vector3& Up, float FovYRadians,
               bool Ortho, float OrthoHalfH,
               int W, int H, unsigned char* Rgba, double& MeanLum) noexcept
{
    using namespace Frontier;
    const auto& Flat    = Level.QueryFlatTriangles();
    const auto& Records = Level.QueryMaterials().QueryRecords();

    // ⚠️ Seated here rather than at the caller. The first TraceView runs well before the raster block that also
    //    wants the sky, so seating it there left the traced sheets rendering against an unprepared atmosphere —
    //    a black sky in the GI view and a correct one in the raster view, from the same state. Seating at the
    //    point of use makes that impossible.
    PreparePreviewSky();
    {
        const FidelityCriteria Tier = FidelityClassifier{}.ConstructCriteria(FidelityCategory::StandardFidelity);
        SeatPreviewSky(PreviewSky, CelestialTier::BudgetFor(Tier));
    }

    std::vector<LumiTri> Lumi;
    CollectLumi(Level, Lumi);
    std::printf("[Preview] %zu emissive triangles light the trace\n", Lumi.size());

    constexpr float kPi = 3.14159265359f;
    const float HalfH = Ortho ? OrthoHalfH : std::tan(FovYRadians * 0.5f);
    const float HalfW = HalfH * static_cast<float>(W) / static_cast<float>(H);
    double LumSum = 0.0;

    auto TraceRay = [&](const float O[3], const float D[3], float& Dist, uint32_t& Prim) -> bool
    {
        return Traversal.TraceClosest(O, D, Dist, Prim);
    };
    auto HitPoint = [&](const float O[3], const float D[3], float Dist, float P[3])
    {
        P[0] = O[0] + D[0] * Dist; P[1] = O[1] + D[1] * Dist; P[2] = O[2] + D[2] * Dist;
    };
    auto TriNormal = [&](uint32_t Prim, float N[3])
    {
        const TriangleIndex& T = Flat[Prim];
        const float Ux = T.VertexBetaX - T.VertexAlphaX, Uy = T.VertexBetaY - T.VertexAlphaY, Uz = T.VertexBetaZ - T.VertexAlphaZ;
        const float Vx = T.VertexGammaX - T.VertexAlphaX, Vy = T.VertexGammaY - T.VertexAlphaY, Vz = T.VertexGammaZ - T.VertexAlphaZ;
        float Nx = Uy * Vz - Uz * Vy, Ny = Uz * Vx - Ux * Vz, Nz = Ux * Vy - Uy * Vx;
        const float Len = std::sqrt(Nx * Nx + Ny * Ny + Nz * Nz);
        if (Len > 0.0f) { Nx /= Len; Ny /= Len; Nz /= Len; } else { Nx = 0.0f; Ny = 0.0f; Nz = 1.0f; }
        N[0] = Nx; N[1] = Ny; N[2] = Nz;
    };
    auto TriAlbedo = [&](uint32_t Prim, float Alb[3])
    {
        uint32_t Slot = 0u;
        std::memcpy(&Slot, &Flat[Prim].MaterialSlot, sizeof(Slot));
        if (Slot < Records.size())
        {
            Alb[0] = Records[Slot].AlbedoR; Alb[1] = Records[Slot].AlbedoG; Alb[2] = Records[Slot].AlbedoB;
        }
        else { Alb[0] = Alb[1] = Alb[2] = 0.8f; }
    };

    for (int Y = 0; Y < H; ++Y)
    {
        for (int X = 0; X < W; ++X)
        {
            uint32_t Seed = static_cast<uint32_t>(Y * W + X) * 2654435761u + 1u;
            float Acc[3] = { 0.0f, 0.0f, 0.0f };
            for (int S = 0; S < kSamples; ++S)
            {
                const float U = (static_cast<float>(X) + Rand01(Seed)) / static_cast<float>(W);
                const float V = (static_cast<float>(Y) + Rand01(Seed)) / static_cast<float>(H);
                float Dx, Dy, Dz, Ox, Oy, Oz;
                if (Ortho)
                {
                    Ox = Eye.x + Right.x * ((2.0f * U - 1.0f) * HalfW) + Up.x * ((1.0f - 2.0f * V) * HalfH);
                    Oy = Eye.y + Right.y * ((2.0f * U - 1.0f) * HalfW) + Up.y * ((1.0f - 2.0f * V) * HalfH);
                    Oz = Eye.z + Right.z * ((2.0f * U - 1.0f) * HalfW) + Up.z * ((1.0f - 2.0f * V) * HalfH);
                    Dx = Forward.x; Dy = Forward.y; Dz = Forward.z;
                }
                else
                {
                    Dx = Forward.x + Right.x * ((2.0f * U - 1.0f) * HalfW) + Up.x * ((1.0f - 2.0f * V) * HalfH);
                    Dy = Forward.y + Right.y * ((2.0f * U - 1.0f) * HalfW) + Up.y * ((1.0f - 2.0f * V) * HalfH);
                    Dz = Forward.z + Right.z * ((2.0f * U - 1.0f) * HalfW) + Up.z * ((1.0f - 2.0f * V) * HalfH);
                    const float Dl = std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
                    Dx /= Dl; Dy /= Dl; Dz /= Dl;
                    Ox = Eye.x; Oy = Eye.y; Oz = Eye.z;
                }

                float O[3] = { Ox, Oy, Oz };
                float D[3] = { Dx, Dy, Dz };
                float Thr[3] = { 1.0f, 1.0f, 1.0f };
                float Path[3] = { 0.0f, 0.0f, 0.0f };
                for (int Bounce = 0; Bounce <= 2; ++Bounce)
                {
                    float Dist = 0.0f; uint32_t Prim = 0u;
                    if (!TraceRay(O, D, Dist, Prim))
                    {
                        // ⚠️ THIS IS THE GI-ON HALF OF THE SKY, and it is the whole reason a celestial system has
                        //    to reach the tracer rather than only the raster. A flat constant here was fine while
                        //    the sky was a backdrop: an escaped ray took one blue number and stopped. With a real
                        //    atmosphere the escaped ray is a LIGHT SAMPLE — the sky is the scene's largest
                        //    emitter, and it is what makes an unlit face take on the colour of the air above it.
                        //
                        //    It applies at every bounce, not just the primary: bounce 0 draws the sky behind the
                        //    geometry, bounces 1 and 2 are the indirect light coming down out of it, which is
                        //    exactly the contribution the old constant flattened.
                        const AtmosphereSample Sky = AtmosphereModel::Integrate(
                            SkyMedium, SkyLight, 2.0f, D, SkySamples, SkyLightSamples);
                        float Escaped[3] = { Sky.Radiance[0], Sky.Radiance[1], Sky.Radiance[2] };
                        float Glow[3];
                        const float Bearing = std::atan2(D[0], D[1]);
                        const float SunBearing = std::atan2(SkyLight.Direction[0], SkyLight.Direction[1]);
                        float Delta = std::fabs(Bearing - SunBearing);
                        if (Delta > 3.14159265f) Delta = 6.2831853f - Delta;
                        Twilight::Evaluate(D, SunElevationDegrees, Delta, SkyTwilight, Glow);
                        for (int C = 0; C < 3; ++C) Escaped[C] += Glow[C];
                        Path[0] += Thr[0] * Escaped[0]; Path[1] += Thr[1] * Escaped[1]; Path[2] += Thr[2] * Escaped[2];
                        break;
                    }
                    float P[3], N[3];
                    HitPoint(O, D, Dist, P);
                    TriNormal(Prim, N);
                    if (N[0] * D[0] + N[1] * D[1] + N[2] * D[2] > 0.0f) { N[0] = -N[0]; N[1] = -N[1]; N[2] = -N[2]; }
                    uint32_t Slot = 0u;
                    std::memcpy(&Slot, &Flat[Prim].MaterialSlot, sizeof(Slot));
                    const bool Emits = Slot < Records.size()
                        && (Records[Slot].EmissiveR + Records[Slot].EmissiveG + Records[Slot].EmissiveB) > 0.0f;
                    if (Emits)
                    {
                        Path[0] += Thr[0] * Records[Slot].EmissiveR;
                        Path[1] += Thr[1] * Records[Slot].EmissiveG;
                        Path[2] += Thr[2] * Records[Slot].EmissiveB;
                        break;
                    }
                    if (Bounce == 2 || Lumi.empty())
                        break;
                    float Alb[3];
                    TriAlbedo(Prim, Alb);
                    // Direct: one emissive triangle, one point, one shadow ray.
                    const LumiTri& L = Lumi[XorShift(Seed) % Lumi.size()];
                    const float R1 = Rand01(Seed), R2 = Rand01(Seed);
                    const float Sq = std::sqrt(R1);
                    const float Lp[3] = { L.A[0] + (L.B[0] - L.A[0]) * (1.0f - Sq) + (L.C[0] - L.A[0]) * (Sq * R2),
                                          L.A[1] + (L.B[1] - L.A[1]) * (1.0f - Sq) + (L.C[1] - L.A[1]) * (Sq * R2),
                                          L.A[2] + (L.B[2] - L.A[2]) * (1.0f - Sq) + (L.C[2] - L.A[2]) * (Sq * R2) };
                    float Sd[3] = { Lp[0] - P[0], Lp[1] - P[1], Lp[2] - P[2] };
                    const float Sl = std::sqrt(Sd[0] * Sd[0] + Sd[1] * Sd[1] + Sd[2] * Sd[2]);
                    Sd[0] /= Sl; Sd[1] /= Sl; Sd[2] /= Sl;
                    const float NdotL = N[0] * Sd[0] + N[1] * Sd[1] + N[2] * Sd[2];
                    const float LdotL = -(L.N[0] * Sd[0] + L.N[1] * Sd[1] + L.N[2] * Sd[2]);
                    if (NdotL > 0.0f && LdotL > 0.0f)
                    {
                        const float So[3] = { P[0] + N[0] * 1e-4f, P[1] + N[1] * 1e-4f, P[2] + N[2] * 1e-4f };
                        float Sdist = 0.0f; uint32_t Sprim = 0u;
                        if (!TraceRay(So, Sd, Sdist, Sprim) || Sdist >= Sl * 0.9999f)
                        {
                            const float W = NdotL * LdotL * L.Area * static_cast<float>(Lumi.size()) / (Sl * Sl * kPi);
                            Path[0] += Thr[0] * Alb[0] * L.Le[0] * W;
                            Path[1] += Thr[1] * Alb[1] * L.Le[1] * W;
                            Path[2] += Thr[2] * Alb[2] * L.Le[2] * W;
                        }
                    }
                    // Cosine bounce about the shading normal.
                    const float R3 = Rand01(Seed), R4 = Rand01(Seed);
                    const float Ph = 2.0f * kPi * R4, Sr = std::sqrt(R3), St = std::sqrt(1.0f - R3);
                    const float Lw[3] = { std::cos(Ph) * Sr, std::sin(Ph) * Sr, St };
                    const bool NearX = std::fabs(N[0]) > 0.9f;
                    float Tt[3] = { NearX ? 0.0f : 1.0f, NearX ? 1.0f : 0.0f, 0.0f };
                    float T[3] = { Tt[1] * N[2] - Tt[2] * N[1], Tt[2] * N[0] - Tt[0] * N[2], Tt[0] * N[1] - Tt[1] * N[0] };
                    const float Tl = std::sqrt(T[0] * T[0] + T[1] * T[1] + T[2] * T[2]);
                    T[0] /= Tl; T[1] /= Tl; T[2] /= Tl;
                    const float Bt[3] = { N[1] * T[2] - N[2] * T[1], N[2] * T[0] - N[0] * T[2], N[0] * T[1] - N[1] * T[0] };
                    D[0] = T[0] * Lw[0] + Bt[0] * Lw[1] + N[0] * Lw[2];
                    D[1] = T[1] * Lw[0] + Bt[1] * Lw[1] + N[1] * Lw[2];
                    D[2] = T[2] * Lw[0] + Bt[2] * Lw[1] + N[2] * Lw[2];
                    O[0] = P[0] + N[0] * 1e-4f; O[1] = P[1] + N[1] * 1e-4f; O[2] = P[2] + N[2] * 1e-4f;
                    Thr[0] *= Alb[0]; Thr[1] *= Alb[1]; Thr[2] *= Alb[2];
                }
                Acc[0] += Path[0]; Acc[1] += Path[1]; Acc[2] += Path[2];
            }
            unsigned char* Px = Rgba + (static_cast<size_t>(Y) * static_cast<size_t>(W) + static_cast<size_t>(X)) * 4u;
            for (int C = 0; C < 3; ++C)
            {
                const float Reinhard = (Acc[C] / static_cast<float>(kSamples));
                const float Mapped    = Reinhard / (1.0f + Reinhard);
                const float Gamma     = std::pow(Mapped < 0.0f ? 0.0f : Mapped, 1.0f / 2.2f);
                Px[C] = static_cast<unsigned char>(Gamma * 255.0f + 0.5f);
            }
            Px[3] = 255u;
            LumSum += 0.2126 * Px[0] + 0.7152 * Px[1] + 0.0722 * Px[2];
        }
    }
    MeanLum = LumSum / (static_cast<double>(W) * static_cast<double>(H) * 255.0);
}

} // namespace

// Poses a render off the viewport's orbit: the eye hangs behind the target along the orbit's forward,
//    with the solver's basis (and its pole guard) rebuilt around it.
void OrbitPose(const Frontier::ViewportOrbit& O, float Eye[3], float F[3], float R[3], float U[3]) noexcept
{
    const float Cy = std::cos(O.Yaw), Sy = std::sin(O.Yaw);
    const float Cp = std::cos(O.Pitch), Sp = std::sin(O.Pitch);
    F[0] = Sy * Cp; F[1] = Cy * Cp; F[2] = Sp;
    float Rx = F[1], Ry = -F[0];
    const float Rl = std::sqrt(Rx * Rx + Ry * Ry);
    if (Rl < 1e-4f) { Rx = Cy; Ry = -Sy; }
    else            { Rx /= Rl; Ry /= Rl; }
    R[0] = Rx; R[1] = Ry; R[2] = 0.0f;
    U[0] = Ry * F[2]; U[1] = -Rx * F[2]; U[2] = Rx * F[1] - Ry * F[0];
    Eye[0] = O.Target[0] - F[0] * O.Distance;
    Eye[1] = O.Target[1] - F[1] * O.Distance;
    Eye[2] = O.Target[2] - F[2] * O.Distance;
}

int main()
{
    using namespace Frontier;
    using namespace Frontier::ProjectZero;

    ImGui::CreateContext();
    ImGuiIO& IO = ImGui::GetIO();
    IO.DisplaySize = ImVec2(static_cast<float>(kWidth), static_cast<float>(kHeight));
    IO.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    IO.IniFilename = nullptr;
    IO.LogFilename = nullptr;
    IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;

    EditorHost Editor;
    Editor.ApplyTheme();

    static TypefaceRegistry Typefaces;
    const uint32_t FamilyCount = Typefaces.Load("EngineContent/FontArchives");
    TypefaceRegistry::Install(&Typefaces);
    std::printf("[Preview] typefaces: %u families\n", FamilyCount);

    unsigned char* GlyphSheet = nullptr;
    int GlyphSheetWidth = 0, GlyphSheetHeight = 0;
    IO.Fonts->GetTexDataAsRGBA32(&GlyphSheet, &GlyphSheetWidth, &GlyphSheetHeight);

    if (!Editor.SeatShade(kWidth, kHeight))
    {
        std::printf("[Preview] the shade never seated\n");
        return 1;
    }
    Editor.AssignProjectName("Project-Zero");

    SceneStructure Level;
    std::string Error;
    if (!SceneCodec::Decode("Projects/Project-Zero/Content/Scenes/CornellBox.gltf", Level, nullptr,
                            SceneDecodeConfiguration{}, &Error))
    {
        std::printf("[Preview] decode failed: %s\n", Error.c_str());
        return 1;
    }

    EditorFeedSequence Feed;
    EditorInstance Rows[kMaxEditorInstances] = {};
    uint32_t RowCount = Feed.FillRoster(Rows, Level);
    // The celestial entities, exactly as GameExecution appends them — the preview's whole purpose is to show
    //    what the project shows, so the roster is built the same way rather than described a second time.
    PreparePreviewSky();
    RowCount += PreviewSky.AppendRoster(Rows, RowCount, Frontier::kMaxEditorInstances);
    std::printf("[Preview] roster: %u rows over %zu placements\n", RowCount, Level.QueryPlacements().size());

    // The game's own boot camera for the Cornell level (see GameExecution's camera branch).
    FlyThroughConfiguration CameraConfig{ 2.5f, 3.0f, 0.00125f, 0.5f, 12.0f };
    FlyThroughSolver Camera(CameraConfig);
    Camera.AssignSpatialLocation(Vector3{ 0.0f, -3.30f, 1.55f });
    Camera.AssignOrientationEuler(0.0f, 0.0f, 0.0f);
    const Vector3 Eye = Camera.Convert<Vector3>();
    const Vector3 Fwd = Camera.QueryForwardVector();
    std::printf("[Preview] eye (%.2f, %.2f, %.2f) forward (%.2f, %.2f, %.2f) fov %.1f deg\n",
                Eye.x, Eye.y, Eye.z, Fwd.x, Fwd.y, Fwd.z,
                static_cast<double>(Camera.QueryFieldOfViewRadians() * 57.29578f));

    // The viewport orbit's home reads the boot camera, aimed at the level's middle.
    Frontier::ViewportOrbit Home;
    float Middle[3] = { 0.0f, 0.0f, 0.0f };
    Frontier::ProjectZero::QueryLevelCentre(Level, Middle);
    Home.Yaw   = Camera.QueryYawRadians();
    Home.Pitch = Camera.QueryPitchRadians();
    {
        const float Dx = Eye.x - Middle[0], Dy = Eye.y - Middle[1], Dz = Eye.z - Middle[2];
        Home.Distance = std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
    }
    Home.Target[0] = Middle[0]; Home.Target[1] = Middle[1]; Home.Target[2] = Middle[2];
    Home.Ortho = false; Home.ViewPoint = 0u; Home.Revision = 0u;
    Editor.SeatViewportOrbit(Home);
    std::printf("[Preview] orbit home: yaw %.2f pitch %.2f dist %.2f target (%.2f, %.2f, %.2f)\n",
                static_cast<double>(Home.Yaw), static_cast<double>(Home.Pitch),
                static_cast<double>(Home.Distance),
                static_cast<double>(Middle[0]), static_cast<double>(Middle[1]),
                static_cast<double>(Middle[2]));

    TraversalIndex Traversal;
    if (!Traversal.BuildBottomLevel(Level.QueryFlatTriangles(), false))
    {
        std::printf("[Preview] traversal build failed\n");
        return 1;
    }

    std::vector<InstanceRecord> Live = Level.QueryInstances();
    EditorSheet PickedSheet = {};
    Editor.PickInstance(7u);   // Tall Box: the geometry sheet over a live centroid
    (void)Feed.BuildSheet(7u, Rows, RowCount, &PickedSheet, Camera, Level, Live);
    std::printf("[Preview] pick: row 7 '%s'\n", Rows[7].Label);

    std::vector<unsigned char> Pixels(static_cast<size_t>(kWidth) * static_cast<size_t>(kHeight) * 3u);
    float CursorX = -1.0f, CursorY = -1.0f;
    bool Held = false;
    auto Tick = [&](float MouseX, float MouseY, bool Down)
    {
        IO.DeltaTime = 1.0f / 60.0f;
        Editor.TickShade(MouseX, MouseY, Down, 0.0f, 1.0f / 60.0f);
        if (Editor.ShadeCoversPointer())
        {
            IO.AddMousePosEvent(-1.0f, -1.0f);
            IO.AddMouseButtonEvent(0, false);
        }
        else
        {
            IO.AddMousePosEvent(MouseX, MouseY);
            IO.AddMouseButtonEvent(0, Down);
        }
        ImGui::NewFrame();
        Editor.Record(Rows, RowCount, &PickedSheet);
        ImGui::Render();
    };
    // Frames pass with the contact exactly where it is (the Rig's Idle); Park clears the hover
    //    before a sheet so no tinted control poses as the resting look.
    auto Idle = [&](int Frames)
    {
        for (int I = 0; I < Frames; ++I)
            Tick(CursorX, CursorY, Held);
    };
    auto Press = [&]
    {
        Held = true;
        Tick(CursorX, CursorY, true);
    };
    auto Release = [&]
    {
        Held = false;
        Tick(CursorX, CursorY, false);
    };
    auto MoveTo = [&](float X, float Y, int Frames)
    {
        const float X0 = CursorX, Y0 = CursorY;
        for (int I = 1; I <= Frames; ++I)
        {
            const float T = static_cast<float>(I) / static_cast<float>(Frames);
            CursorX = X0 + (X - X0) * T;
            CursorY = Y0 + (Y - Y0) * T;
            Tick(CursorX, CursorY, Held);
        }
    };
    auto Tap = [&](float X, float Y)
    {
        MoveTo(X, Y, 8);
        Press();
        Idle(3);
        Release();
    };
    auto Park = [&]
    {
        MoveTo(-1.0f, -1.0f, 6);
        Idle(2);
    };
    auto Figures = [&](const char* Tag)
    {
        const ControlCentreSettings& S = Editor.QueryShadeSettings();
        std::printf("[Preview] %s: open %d page %u | GI %s AA %s FPS %s Notif %s Q %s scale %.2f rev %u\n",
                    Tag, Editor.QueryShadeOpen() ? 1 : 0, Editor.QueryShadePage(),
                    S.GlobalIllumination ? "on" : "off", S.AntiAliasing ? "on" : "off",
                    S.FrameRateOverlay ? "on" : "off", S.Notifications ? "on" : "off",
                    FidelityLabel(S.Quality), static_cast<double>(S.RenderScale), S.Revision);
    };
    Idle(10);

    const int ViewW = static_cast<int>(Editor.QueryViewWidth());
    const int ViewH = static_cast<int>(Editor.QueryViewHeight());
    std::printf("[Preview] view rect %d x %d\n", ViewW, ViewH);
    if (ViewW < 16 || ViewH < 16)
    {
        std::printf("[Preview] view rect never seated\n");
        return 1;
    }
    std::vector<unsigned char> View(static_cast<size_t>(ViewW) * static_cast<size_t>(ViewH) * 4u);
    const auto TraceStart = std::chrono::steady_clock::now();
    double MeanLum = 0.0;
    TraceView(Level, Traversal, Eye, Fwd, Camera.QueryRightVector(), Camera.QueryUpwardVector(),
              Camera.QueryFieldOfViewRadians(), false, 0.0f, ViewW, ViewH, View.data(), MeanLum);
    const double TraceMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - TraceStart).count();
    std::printf("[Preview] traced %d x %d in %.0f ms (mean luminance %.3f)\n", ViewW, ViewH, TraceMs, MeanLum);

    Editor.AssignView(View.data(), static_cast<uint32_t>(ViewW), static_cast<uint32_t>(ViewH));
    const ImTextureID ViewId = static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(View.data()));
    auto WriteSheet = [&](const char* Sheet)
    {
        for (size_t I = 0u; I < Pixels.size(); I += 3u)
        {
            Pixels[I] = kGround[0]; Pixels[I + 1u] = kGround[1]; Pixels[I + 2u] = kGround[2];
        }
        const ImDrawData* Drawings = ImGui::GetDrawData();
        for (int Index = 0; Index < Drawings->CmdListsCount; ++Index)
            RasterizeList(Drawings->CmdLists[Index], GlyphSheet, GlyphSheetWidth, GlyphSheetHeight,
                          View.data(), ViewW, ViewH, ViewId, Pixels.data(),
                          Drawings->DisplayPos, Drawings->FramebufferScale);
        if (stbi_write_png(Sheet, kWidth, kHeight, 3, Pixels.data(), kWidth * 3) == 0)
        {
            std::printf("[Preview] the sheet would not write\n");
            return false;
        }
        std::printf("[Preview] wrote %s\n", Sheet);
        return true;
    };
    // Sheet one: the notch at rest over the GI view.
    Park();
    Figures("shut");
    if (!WriteSheet("Diagnostics/EditorPreviewShut.png"))
        return 1;

    // The Cameras folder ships the stock Main Camera by default: pick it and prove the sheet follows.
    uint32_t CameraRow = kNoEditorInstance;
    for (uint32_t R = 0u; R < RowCount; ++R)
        if (Rows[R].Category == EditorInstanceCategory::Camera) { CameraRow = R; break; }
    if (CameraRow == kNoEditorInstance)
    {
        std::printf("[Preview] the roster carries no Camera\n");
        return 1;
    }
    Editor.PickInstance(CameraRow);
    (void)Feed.BuildSheet(CameraRow, Rows, RowCount, &PickedSheet, Camera, Level, Live);
    if (PickedSheet.GroupCount != 3u)
    {
        std::printf("[Preview] the picked Camera built %u groups\n", PickedSheet.GroupCount);
        return 1;
    }
    Idle(10);
    Idle(10);
    Park();
    if (!WriteSheet("Diagnostics/EditorPreviewCamera.png"))
        return 1;

    // A tap on the notch carries the shade open; the dashboard's own figures prove the card went live.
    Tap(Editor.QueryNotchX(), Editor.QueryNotchY());
    Idle(120);
    Park();
    Figures("open");
    if (!Editor.QueryShadeOpen())
    {
        std::printf("[Preview] the notch never opened\n");
        return 1;
    }
    if (!WriteSheet("Diagnostics/EditorPreview.png"))
        return 1;

    // The pill drags the render scale down and back; the figures must follow both ways. The carry
    //    mirrors the Rig's own: press near the track's end, carry to 46.67% of its span, release.
    const float PillX0 = Editor.QueryPillX0(), PillX1 = Editor.QueryPillX1(), PillY = Editor.QueryPillY();
    MoveTo(PillX1 - 2.0f, PillY, 8);
    Press();
    MoveTo(PillX0 + (PillX1 - PillX0) * 0.4667f, PillY, 40);
    Release();
    Idle(6);
    Figures("pill 60");
    if (Editor.QueryRenderScale() < 0.55f || Editor.QueryRenderScale() > 0.65f)
    {
        std::printf("[Preview] the pill never dragged\n");
        return 1;
    }

    // A tap on the GI disc seats the raster path; the view re-seats through the engine's
    //    visibility raster at the pill's scale, upscaled into the view rows.
    Tap(Editor.QueryGiTileX(), Editor.QueryGiTileY());
    Idle(6);
    Figures("GI off");
    if (Editor.QueryGiEnabled())
    {
        std::printf("[Preview] the disc never toggled\n");
        return 1;
    }
    const float Scale = Editor.QueryRenderScale();
    const int Rw = static_cast<int>(static_cast<float>(ViewW) * Scale) < 16
        ? 16 : static_cast<int>(static_cast<float>(ViewW) * Scale);
    const int Rh = static_cast<int>(static_cast<float>(ViewH) * Scale) < 16
        ? 16 : static_cast<int>(static_cast<float>(ViewH) * Scale);
    std::vector<unsigned char> Small(static_cast<size_t>(Rw) * static_cast<size_t>(Rh) * 4u);
    const float EyeP[3] = { Eye.x, Eye.y, Eye.z };
    const Vector3 FwdV = Fwd, RightV = Camera.QueryRightVector(), UpV = Camera.QueryUpwardVector();
    const float FwdP[3] = { FwdV.x, FwdV.y, FwdV.z };
    const float RightP[3] = { RightV.x, RightV.y, RightV.z };
    const float UpP[3] = { UpV.x, UpV.y, UpV.z };
    VisibilityRaster Raster;
    // The shade's tier and its shadow-resolution override decide the technique and the map side; the raster is
    //    told once, before the render, exactly as the GPU path is told through ShadowFrameConfiguration.
    {
        const FidelityCriteria Criteria = Editor.QueryShadeCriteria();
        ShadowCriteria Shadows{};
        Shadows.Filter   = static_cast<ShadowFilterKind>(Criteria.ShadowTechnique);
        Shadows.MapSide  = Criteria.ShadowMapSide;
        Shadows.TapCount = Criteria.ShadowFilterTapCount;
        Raster.AssignShadowCriteria(Shadows);
        std::printf("[Preview] shadows: %s, %u x %u map, %u tap kernel\n",
                    ShadowTechniqueLabel(Criteria.ShadowTechnique), Criteria.ShadowMapSide,
                    Criteria.ShadowMapSide, Criteria.ShadowFilterTapCount);

        // The sky, from the same tier and through the same sequence Project Zero uses. Both preview views take
        //    it: the raster directly, and the tracer through SeatPreviewSky, so the traced and rastered halves
        //    of this sheet cannot disagree about the weather.
        PreparePreviewSky();
        const CelestialBudget Sky = CelestialTier::BudgetFor(Criteria);
        PreviewSky.ApplyTo(Raster, Sky);
        SeatPreviewSky(PreviewSky, Sky);
        std::printf("[Preview] sky: sun %+.2f deg, %u x %u atmosphere samples, cloud %s\n",
                    static_cast<double>(PreviewSky.Frame().Sun.Elevation),
                    Sky.AtmosphereSamples, Sky.AtmosphereLightSamples,
                    PreviewSky.Cloud.Enabled ? "on" : "off");
    }
    const auto RasterStart = std::chrono::steady_clock::now();
    double RasterLum = 0.0;
    if (!Raster.Render(Level, EyeP, FwdP, RightP, UpP, Camera.QueryFieldOfViewRadians(),
                       static_cast<uint32_t>(Rw), static_cast<uint32_t>(Rh), Small.data(), RasterLum))
    {
        std::printf("[Preview] the visibility raster declined the render\n");
        return 1;
    }
    const double RasterMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - RasterStart).count();
    for (int Y = 0; Y < ViewH; ++Y)
        for (int X = 0; X < ViewW; ++X)
        {
            const size_t Src = (static_cast<size_t>(Y) * static_cast<size_t>(Rh) / static_cast<size_t>(ViewH)
                * static_cast<size_t>(Rw) + static_cast<size_t>(X) * static_cast<size_t>(Rw) / static_cast<size_t>(ViewW)) * 4u;
            const size_t Dst = (static_cast<size_t>(Y) * static_cast<size_t>(ViewW) + static_cast<size_t>(X)) * 4u;
            View[Dst] = Small[Src]; View[Dst + 1u] = Small[Src + 1u];
            View[Dst + 2u] = Small[Src + 2u]; View[Dst + 3u] = 255u;
        }
    std::printf("[Preview] rasterized %d x %d at scale %.2f in %.0f ms (mean luminance %.3f)\n",
                Rw, Rh, static_cast<double>(Scale), RasterMs, RasterLum);
    Park();
    if (!WriteSheet("Diagnostics/EditorPreviewRaster.png"))
        return 1;

    // Sheet four: the shade tapped shut and the raster re-seated at full scale, so the render
    //    itself can be looked at without the upscale chunk.
    Tap(Editor.QueryNotchX(), Editor.QueryNotchY());
    Idle(120);
    Figures("shut raster");
    if (Editor.QueryShadeOpen())
    {
        std::printf("[Preview] the notch never shut\n");
        return 1;
    }
    double FullLum = 0.0;
    if (!Raster.Render(Level, EyeP, FwdP, RightP, UpP, Camera.QueryFieldOfViewRadians(),
                       static_cast<uint32_t>(ViewW), static_cast<uint32_t>(ViewH), View.data(), FullLum))
    {
        std::printf("[Preview] the full-scale raster declined the render\n");
        return 1;
    }
    std::printf("[Preview] rasterized %d x %d at full scale (mean luminance %.3f)\n", ViewW, ViewH, FullLum);
    Park();
    if (!WriteSheet("Diagnostics/EditorPreviewRasterShut.png"))
        return 1;

    // The shade back open for the drag home: the pill only takes the pointer while live.
    Tap(Editor.QueryNotchX(), Editor.QueryNotchY());
    Idle(120);

    MoveTo(PillX0 + (PillX1 - PillX0) * 0.4667f, PillY, 8);
    Press();
    MoveTo(PillX1 - 1.0f, PillY, 40);
    Release();
    Idle(6);
    Figures("pill home");
    if (Editor.QueryRenderScale() < 0.99f)
    {
        std::printf("[Preview] the pill never dragged home\n");
        return 1;
    }

    // Sheet five: the gear carries the settings hub in — render, appearance, input, notifications.
    Tap(Editor.QueryGearX(), Editor.QueryGearY());
    Idle(90);
    Park();
    Figures("hub");
    if (Editor.QueryShadePage() != 1u)
    {
        std::printf("[Preview] the gear never raised the hub\n");
        return 1;
    }
    if (!WriteSheet("Diagnostics/EditorPreviewHub.png"))
        return 1;

    // The shade shuts for the views pass: the pill's rows pose the orbit, and each pose re-seats the
    //    view — Front under the orthographic projection through the tracer, Top through the raster.
    Tap(Editor.QueryNotchX(), Editor.QueryNotchY());
    Idle(120);
    Figures("views shut");
    if (Editor.QueryShadeOpen())
    {
        std::printf("[Preview] the notch never shut for the views\n");
        return 1;
    }
    const auto OrbitFigures = [&](const char* Tag)
    {
        const ViewportOrbit& O = Editor.QueryViewportOrbit();
        std::printf("[Preview] %s: snap %u yaw %.3f pitch %.3f ortho %d dist %.2f rev %u\n",
                    Tag, O.ViewPoint, static_cast<double>(O.Yaw), static_cast<double>(O.Pitch),
                    O.Ortho ? 1 : 0, static_cast<double>(O.Distance), O.Revision);
    };
    // Orthographic first: home under the parallel projection, which the snaps below keep.
    Tap(532.0f, 108.0f);
    Idle(10);
    Tap(607.0f, 172.0f);
    Idle(3);
    OrbitFigures("ortho home");
    if (Editor.QueryViewportOrbit().ViewPoint != 0u || !Editor.QueryViewportOrbit().Ortho)
    {
        std::printf("[Preview] the Orthographic row never posed\n");
        return 1;
    }
    // The level's bounds, for framing the parallel poses off the room instead of the eye distance.
    float LoB[3] = { 0.0f, 0.0f, 0.0f }, HiB[3] = { 0.0f, 0.0f, 0.0f };
    {
        const auto& Flat = Level.QueryFlatTriangles();
        LoB[0] = HiB[0] = Flat[0].VertexAlphaX;
        LoB[1] = HiB[1] = Flat[0].VertexAlphaY;
        LoB[2] = HiB[2] = Flat[0].VertexAlphaZ;
        for (const TriangleIndex& T : Flat)
        {
            const float Vx[3] = { T.VertexAlphaX, T.VertexBetaX, T.VertexGammaX };
            const float Vy[3] = { T.VertexAlphaY, T.VertexBetaY, T.VertexGammaY };
            const float Vz[3] = { T.VertexAlphaZ, T.VertexBetaZ, T.VertexGammaZ };
            for (int K = 0; K < 3; ++K)
            {
                if (Vx[K] < LoB[0]) LoB[0] = Vx[K]; if (Vx[K] > HiB[0]) HiB[0] = Vx[K];
                if (Vy[K] < LoB[1]) LoB[1] = Vy[K]; if (Vy[K] > HiB[1]) HiB[1] = Vy[K];
                if (Vz[K] < LoB[2]) LoB[2] = Vz[K]; if (Vz[K] > HiB[2]) HiB[2] = Vz[K];
            }
        }
        std::printf("[Preview] level bounds x %.2f..%.2f y %.2f..%.2f z %.2f..%.2f\n",
                    static_cast<double>(LoB[0]), static_cast<double>(HiB[0]),
                    static_cast<double>(LoB[1]), static_cast<double>(HiB[1]),
                    static_cast<double>(LoB[2]), static_cast<double>(HiB[2]));
    }
    // Front through the tracer: the eye hangs off -Y on the orbit's figures, parallel rays, the room
    //    filling the frame.
    Tap(532.0f, 108.0f);
    Idle(10);
    Tap(607.0f, 211.0f);
    Idle(3);
    OrbitFigures("front");
    {
        const ViewportOrbit& O = Editor.QueryViewportOrbit();
        if (O.ViewPoint != 1u || !O.Ortho)
        {
            std::printf("[Preview] the Front row never posed\n");
            return 1;
        }
        float EyeO[3], FO[3], RO[3], UO[3];
        OrbitPose(O, EyeO, FO, RO, UO);
        const float Aspect = static_cast<float>(ViewW) / static_cast<float>(ViewH);
        const float ZSpan = HiB[2] - LoB[2], XSpan = HiB[0] - LoB[0];
        const float FrontHalfH = 0.5f * (ZSpan > XSpan / Aspect ? ZSpan : XSpan / Aspect) * 1.1f;
        std::printf("[Preview] front eye (%.2f, %.2f, %.2f) half-height %.2f\n",
                    static_cast<double>(EyeO[0]), static_cast<double>(EyeO[1]),
                    static_cast<double>(EyeO[2]), static_cast<double>(FrontHalfH));
        const Vector3 EyeV{ EyeO[0], EyeO[1], EyeO[2] };
        const Vector3 FwdV{ FO[0], FO[1], FO[2] };
        const Vector3 RightV{ RO[0], RO[1], RO[2] };
        const Vector3 UpV{ UO[0], UO[1], UO[2] };
        double FrontLum = 0.0;
        const auto FrontStart = std::chrono::steady_clock::now();
        TraceView(Level, Traversal, EyeV, FwdV, RightV, UpV,
                  Camera.QueryFieldOfViewRadians(), true, FrontHalfH,
                  ViewW, ViewH, View.data(), FrontLum);
        const double FrontMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - FrontStart).count();
        std::printf("[Preview] front ortho traced in %.0f ms (mean luminance %.3f)\n", FrontMs, FrontLum);
        if (!(FrontLum > 0.05) || !(FrontLum < 3.0))
        {
            std::printf("[Preview] the front ortho trace went dark or blown\n");
            return 1;
        }
    }
    Idle(6);
    Park();
    if (!WriteSheet("Diagnostics/EditorPreviewOrtho.png"))
        return 1;
    // Top through the raster: the eye drops inside the room (off the ceiling height), parallel primary.
    float CeilZ = Home.Target[2] + 1.0f;
    for (const TriangleIndex& T : Level.QueryFlatTriangles())
    {
        if (T.VertexAlphaZ > CeilZ) CeilZ = T.VertexAlphaZ;
        if (T.VertexBetaZ > CeilZ)  CeilZ = T.VertexBetaZ;
        if (T.VertexGammaZ > CeilZ) CeilZ = T.VertexGammaZ;
    }
    Tap(532.0f, 108.0f);
    Idle(10);
    Tap(607.0f, 331.0f);
    Idle(3);
    OrbitFigures("top");
    {
        const ViewportOrbit& O = Editor.QueryViewportOrbit();
        if (O.ViewPoint != 5u || !O.Ortho)
        {
            std::printf("[Preview] the Top row never posed\n");
            return 1;
        }
        ViewportOrbit TopO = O;
        TopO.Distance = (CeilZ - O.Target[2]) * 0.55f;
        if (TopO.Distance < 0.4f)
            TopO.Distance = 0.4f;
        ++TopO.Revision;
        Editor.SeatViewportOrbit(TopO);
        float EyeO[3], FO[3], RO[3], UO[3];
        OrbitPose(TopO, EyeO, FO, RO, UO);
        std::printf("[Preview] top eye (%.2f, %.2f, %.2f) under ceiling %.2f\n",
                    static_cast<double>(EyeO[0]), static_cast<double>(EyeO[1]),
                    static_cast<double>(EyeO[2]), static_cast<double>(CeilZ));
        double TopLum = 0.0;
        if (!Raster.RenderOrthographic(Level, EyeO, FO, RO, UO, 1.5f,
                       static_cast<uint32_t>(ViewW), static_cast<uint32_t>(ViewH), View.data(), TopLum))
        {
            std::printf("[Preview] the top ortho raster declined the render\n");
            return 1;
        }
        std::printf("[Preview] top ortho rasterized (mean luminance %.3f)\n", TopLum);
        if (!(TopLum > 0.05) || !(TopLum < 3.0))
        {
            std::printf("[Preview] the top ortho raster went dark or blown\n");
            return 1;
        }
    }
    Idle(6);
    Park();
    if (!WriteSheet("Diagnostics/EditorPreviewTop.png"))
        return 1;

    // Last of all, so its frames perturb no capture above: the Moons row proves a celestial sheet follows
    //    the pick — five groups (Solved plus one per roster slot) through the real inspector — and an edit
    //    on the rendered sheet lands back on the sequence, the BuildSheet → panel → ApplySheet round-trip
    //    GameExecution runs every tick the row stays picked.
    uint32_t MoonsRow = kNoEditorInstance;
    for (uint32_t R = 0u; R < RowCount; ++R)
        if (std::strcmp(Rows[R].Label, "Moons") == 0) { MoonsRow = R; break; }
    if (MoonsRow == kNoEditorInstance)
    {
        std::printf("[Preview] the roster carries no Moons row\n");
        return 1;
    }
    Editor.PickInstance(MoonsRow);
    PreviewSky.BuildSheet(Frontier::ProjectZero::CelestialEntity::Moons, PickedSheet);
    if (PickedSheet.GroupCount != 5u)
    {
        std::printf("[Preview] the picked Moons built %u groups\n", PickedSheet.GroupCount);
        return 1;
    }
    Idle(10);
    Park();
    if (!WriteSheet("Diagnostics/EditorPreviewMoons.png"))
        return 1;
    Frontier::EditorProperty* BrightProp = nullptr;
    for (uint32_t G = 0u; G < PickedSheet.GroupCount; ++G)
        for (uint32_t P = 0u; P < PickedSheet.Groups[G].PropertyCount; ++P)
        {
            Frontier::EditorProperty& Prop = PickedSheet.Groups[G].Properties[P];
            if (std::strcmp(Prop.Label, "M1 Bright") == 0) { Prop.Figure = 4.2f; BrightProp = &Prop; }
        }
    if (BrightProp == nullptr)
    {
        std::printf("[Preview] the moons sheet carries no M1 Bright\n");
        return 1;
    }
    PreviewSky.ApplySheet(Frontier::ProjectZero::CelestialEntity::Moons, PickedSheet);
    if (PreviewSky.MoonSlots[0].Bright != 4.2f)
    {
        std::printf("[Preview] the moons edit never landed\n");
        return 1;
    }
    BrightProp->Figure = 1.6f;
    PreviewSky.ApplySheet(Frontier::ProjectZero::CelestialEntity::Moons, PickedSheet);
    return 0;
}
