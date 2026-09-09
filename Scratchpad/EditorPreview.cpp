//============================================================================================================================================
//                                                    EDITORPREVIEW.CPP
//============================================================================================================================================
// 🧩 Headless editor preview — the development editor over the LIVE Cornell level: the feed fills the roster from
//    the fresh glTF, the Tall Box carries the pick, and the viewport shows the level traced on the CPU through
//    the same traversal the renderer refits. Three sheets out (shut, open GI, open raster), no gates. No Vulkan,
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

// Next-event estimation over the level's own emissive triangles, two diffuse bounces, sky through the gaps.
void TraceView(const Frontier::SceneStructure& Level, const Frontier::TraversalIndex& Traversal,
               const Frontier::Vector3& Eye, const Frontier::Vector3& Forward,
               const Frontier::Vector3& Right, const Frontier::Vector3& Up, float FovYRadians,
               int W, int H, unsigned char* Rgba, float Exposure, double& MeanLum) noexcept
{
    using namespace Frontier;
    const auto& Flat    = Level.QueryFlatTriangles();
    const auto& Records = Level.QueryMaterials().QueryRecords();

    std::vector<LumiTri> Lumi;
    CollectLumi(Level, Lumi);
    std::printf("[Preview] %zu emissive triangles light the trace\n", Lumi.size());

    constexpr float kPi = 3.14159265359f;
    const float HalfH = std::tan(FovYRadians * 0.5f);
    const float HalfW = HalfH * static_cast<float>(W) / static_cast<float>(H);
    double LumSum = 0.0;
    const float Gain = std::pow(2.0f, Exposure);

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
                float Dx = Forward.x + Right.x * ((2.0f * U - 1.0f) * HalfW) + Up.x * ((1.0f - 2.0f * V) * HalfH);
                float Dy = Forward.y + Right.y * ((2.0f * U - 1.0f) * HalfW) + Up.y * ((1.0f - 2.0f * V) * HalfH);
                float Dz = Forward.z + Right.z * ((2.0f * U - 1.0f) * HalfW) + Up.z * ((1.0f - 2.0f * V) * HalfH);
                const float Dl = std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
                Dx /= Dl; Dy /= Dl; Dz /= Dl;

                float O[3] = { Eye.x, Eye.y, Eye.z };
                float D[3] = { Dx, Dy, Dz };
                float Thr[3] = { 1.0f, 1.0f, 1.0f };
                float Path[3] = { 0.0f, 0.0f, 0.0f };
                for (int Bounce = 0; Bounce <= 2; ++Bounce)
                {
                    float Dist = 0.0f; uint32_t Prim = 0u;
                    if (!TraceRay(O, D, Dist, Prim))
                    {
                        constexpr float Sky[3] = { 0.30f, 0.42f, 0.63f };
                        Path[0] += Thr[0] * Sky[0]; Path[1] += Thr[1] * Sky[1]; Path[2] += Thr[2] * Sky[2];
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
                const float Reinhard = (Acc[C] / static_cast<float>(kSamples)) * Gain;
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

// The visibility raster: projected triangles edge-walked under a depth buffer, shaded direct-only —
//    Lambert plus a GGX specular under the luminaires, four shadow samples a pixel, no bounces. The
//    shade-off look: normal lookdev PBR, like the raster without GI.
void RasterizeView(const Frontier::SceneStructure& Level, const Frontier::TraversalIndex& Traversal,
                   const Frontier::Vector3& Eye, const Frontier::Vector3& Forward,
                   const Frontier::Vector3& Right, const Frontier::Vector3& Up, float FovYRadians,
                   int W, int H, unsigned char* Rgba, float Exposure, double& MeanLum) noexcept
{
    using namespace Frontier;
    const auto& Flat    = Level.QueryFlatTriangles();
    const auto& Records = Level.QueryMaterials().QueryRecords();

    std::vector<LumiTri> Lumi;
    CollectLumi(Level, Lumi);
    std::printf("[Preview] %zu emissive triangles light the raster\n", Lumi.size());

    constexpr float kPi = 3.14159265359f;
    constexpr float kSky[3] = { 0.30f, 0.42f, 0.63f };
    constexpr int kLightSamples = 4;
    const float HalfH = std::tan(FovYRadians * 0.5f);
    const float HalfW = HalfH * static_cast<float>(W) / static_cast<float>(H);
    const float Gain  = std::pow(2.0f, Exposure);

    std::vector<float> Depth(static_cast<size_t>(W) * static_cast<size_t>(H), 1e30f);
    std::vector<float> Linear(static_cast<size_t>(W) * static_cast<size_t>(H) * 3u);
    for (size_t I = 0u; I < static_cast<size_t>(W) * static_cast<size_t>(H); ++I)
    {
        Linear[I * 3u] = kSky[0]; Linear[I * 3u + 1u] = kSky[1]; Linear[I * 3u + 2u] = kSky[2];
    }

    uint32_t Seed = 0xC0FFEEu;
    for (size_t T = 0u; T < Flat.size(); ++T)
    {
        uint32_t Slot = 0u;
        std::memcpy(&Slot, &Flat[T].MaterialSlot, sizeof(Slot));
        if (Slot >= Records.size())
            continue;
        const MaterialRecord& R = Records[Slot];

        const float V[3][3] = {
            { Flat[T].VertexAlphaX, Flat[T].VertexAlphaY, Flat[T].VertexAlphaZ },
            { Flat[T].VertexBetaX,  Flat[T].VertexBetaY,  Flat[T].VertexBetaZ },
            { Flat[T].VertexGammaX, Flat[T].VertexGammaY, Flat[T].VertexGammaZ },
        };
        float N[3] = { (V[1][1] - V[0][1]) * (V[2][2] - V[0][2]) - (V[1][2] - V[0][2]) * (V[2][1] - V[0][1]),
                       (V[1][2] - V[0][2]) * (V[2][0] - V[0][0]) - (V[1][0] - V[0][0]) * (V[2][2] - V[0][2]),
                       (V[1][0] - V[0][0]) * (V[2][1] - V[0][1]) - (V[1][1] - V[0][1]) * (V[2][0] - V[0][0]) };
        const float Nl = std::sqrt(N[0] * N[0] + N[1] * N[1] + N[2] * N[2]);
        if (Nl <= 0.0f)
            continue;
        N[0] /= Nl; N[1] /= Nl; N[2] /= Nl;

        // Camera space; the Cornell camera stands outside the room, so no triangle crosses the near plane.
        float Cx[3], Cy[3], Cz[3];
        bool Behind = false;
        for (int K = 0; K < 3; ++K)
        {
            const float Rx = V[K][0] - Eye.x, Ry = V[K][1] - Eye.y, Rz = V[K][2] - Eye.z;
            Cx[K] = Rx * Right.x + Ry * Right.y + Rz * Right.z;
            Cy[K] = Rx * Up.x    + Ry * Up.y    + Rz * Up.z;
            Cz[K] = Rx * Forward.x + Ry * Forward.y + Rz * Forward.z;
            if (Cz[K] < 0.05f)
                Behind = true;
        }
        if (Behind)
            continue;

        float Sx[3], Sy[3], InvZ[3];
        for (int K = 0; K < 3; ++K)
        {
            InvZ[K] = 1.0f / Cz[K];
            Sx[K] = (Cx[K] * InvZ[K] / HalfW * 0.5f + 0.5f) * static_cast<float>(W);
            Sy[K] = (1.0f - (Cy[K] * InvZ[K] / HalfH * 0.5f + 0.5f)) * static_cast<float>(H);
        }
        float Area = EdgeWeight(Sx[0], Sy[0], Sx[1], Sy[1], Sx[2], Sy[2]);
        if (Area == 0.0f)
            continue;
        if (Area < 0.0f)
        {
            std::swap(Sx[1], Sx[2]); std::swap(Sy[1], Sy[2]); std::swap(InvZ[1], InvZ[2]);
            Area = -Area;
        }
        int LoX = static_cast<int>(std::floor(std::fmin(Sx[0], std::fmin(Sx[1], Sx[2]))));
        int HiX = static_cast<int>(std::ceil(std::fmax(Sx[0], std::fmax(Sx[1], Sx[2]))));
        int LoY = static_cast<int>(std::floor(std::fmin(Sy[0], std::fmin(Sy[1], Sy[2]))));
        int HiY = static_cast<int>(std::ceil(std::fmax(Sy[0], std::fmax(Sy[1], Sy[2]))));
        if (LoX < 0) LoX = 0; if (HiX > W) HiX = W;
        if (LoY < 0) LoY = 0; if (HiY > H) HiY = H;
        const float InverseArea = 1.0f / Area;

        const float Alb[3] = { R.AlbedoR, R.AlbedoG, R.AlbedoB };
        const float Rough = R.Roughness, Metal = R.Metalness;
        const bool Emits = (R.EmissiveR + R.EmissiveG + R.EmissiveB) > 0.0f;
        for (int Y = LoY; Y < HiY; ++Y)
        {
            for (int X = LoX; X < HiX; ++X)
            {
                const float Px = static_cast<float>(X) + 0.5f;
                const float Py = static_cast<float>(Y) + 0.5f;
                const float W0 = EdgeWeight(Sx[1], Sy[1], Sx[2], Sy[2], Px, Py) * InverseArea;
                const float W1 = EdgeWeight(Sx[2], Sy[2], Sx[0], Sy[0], Px, Py) * InverseArea;
                const float W2 = EdgeWeight(Sx[0], Sy[0], Sx[1], Sy[1], Px, Py) * InverseArea;
                if (W0 < 0.0f || W1 < 0.0f || W2 < 0.0f)
                    continue;
                const float Z = 1.0f / (W0 * InvZ[0] + W1 * InvZ[1] + W2 * InvZ[2]);
                const size_t Idx = static_cast<size_t>(Y) * static_cast<size_t>(W) + static_cast<size_t>(X);
                if (Z >= Depth[Idx])
                    continue;
                Depth[Idx] = Z;

                float* Out = &Linear[Idx * 3u];
                if (Emits)
                {
                    Out[0] = R.EmissiveR; Out[1] = R.EmissiveG; Out[2] = R.EmissiveB;
                    continue;
                }
                const float U = (2.0f * Px / static_cast<float>(W) - 1.0f) * HalfW;
                const float Vv = (1.0f - 2.0f * Py / static_cast<float>(H)) * HalfH;
                float Dx = Forward.x + Right.x * U + Up.x * Vv;
                float Dy = Forward.y + Right.y * U + Up.y * Vv;
                float Dz = Forward.z + Right.z * U + Up.z * Vv;
                const float Dl = std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
                Dx /= Dl; Dy /= Dl; Dz /= Dl;
                float Ns[3] = { N[0], N[1], N[2] };
                if (Ns[0] * Dx + Ns[1] * Dy + Ns[2] * Dz > 0.0f) { Ns[0] = -Ns[0]; Ns[1] = -Ns[1]; Ns[2] = -Ns[2]; }
                const float P[3] = { Eye.x + Dx * Z, Eye.y + Dy * Z, Eye.z + Dz * Z };
                const float NdotV = -(Ns[0] * Dx + Ns[1] * Dy + Ns[2] * Dz);

                // The raster's fill: a flat ambient under the direct light, so unlit faces sit dark grey
                //    instead of pitch black (the no-GI look still needs to read as a render, not a void).
                float Acc[3] = { Alb[0] * (1.0f - Metal) * 0.045f, Alb[1] * (1.0f - Metal) * 0.045f,
                                 Alb[2] * (1.0f - Metal) * 0.045f };
                for (int Ls = 0; Ls < kLightSamples && !Lumi.empty(); ++Ls)
                {
                    const LumiTri& L = Lumi[XorShift(Seed) % Lumi.size()];
                    const float R1 = Rand01(Seed), R2 = Rand01(Seed);
                    const float Sq = std::sqrt(R1);
                    const float Lp[3] = { L.A[0] + (L.B[0] - L.A[0]) * (1.0f - Sq) + (L.C[0] - L.A[0]) * (Sq * R2),
                                          L.A[1] + (L.B[1] - L.A[1]) * (1.0f - Sq) + (L.C[1] - L.A[1]) * (Sq * R2),
                                          L.A[2] + (L.B[2] - L.A[2]) * (1.0f - Sq) + (L.C[2] - L.A[2]) * (Sq * R2) };
                    float Sd[3] = { Lp[0] - P[0], Lp[1] - P[1], Lp[2] - P[2] };
                    const float Sl = std::sqrt(Sd[0] * Sd[0] + Sd[1] * Sd[1] + Sd[2] * Sd[2]);
                    Sd[0] /= Sl; Sd[1] /= Sl; Sd[2] /= Sl;
                    const float NdotL = Ns[0] * Sd[0] + Ns[1] * Sd[1] + Ns[2] * Sd[2];
                    const float LdotL = -(L.N[0] * Sd[0] + L.N[1] * Sd[1] + L.N[2] * Sd[2]);
                    if (NdotL <= 0.0f || LdotL <= 0.0f)
                        continue;
                    const float So[3] = { P[0] + Ns[0] * 1e-4f, P[1] + Ns[1] * 1e-4f, P[2] + Ns[2] * 1e-4f };
                    float Sdist = 0.0f; uint32_t Sprim = 0u;
                    if (Traversal.TraceClosest(So, Sd, Sdist, Sprim) && Sdist < Sl * 0.9999f)
                        continue;
                    const float Geo = NdotL * LdotL * L.Area * static_cast<float>(Lumi.size()) / (Sl * Sl);
                    float Hx = Sd[0] - Dx, Hy = Sd[1] - Dy, Hz = Sd[2] - Dz;
                    const float Hl = std::sqrt(Hx * Hx + Hy * Hy + Hz * Hz);
                    Hx /= Hl; Hy /= Hl; Hz /= Hl;
                    const float NdotH = Ns[0] * Hx + Ns[1] * Hy + Ns[2] * Hz;
                    float Alpha = Rough * Rough;
                    if (Alpha < 0.05f)
                        Alpha = 0.05f;
                    const float A2 = Alpha * Alpha;
                    const float Denom = NdotH * NdotH * (A2 - 1.0f) + 1.0f;
                    const float Df = A2 / (kPi * Denom * Denom);
                    const float Kk = (Alpha + 1.0f) * (Alpha + 1.0f) * 0.125f;
                    const float G = (NdotV / (NdotV * (1.0f - Kk) + Kk)) * (NdotL / (NdotL * (1.0f - Kk) + Kk));
                    const float Cos5 = (1.0f - NdotH) * (1.0f - NdotH) * (1.0f - NdotH) * (1.0f - NdotH) * (1.0f - NdotH);
                    for (int C = 0; C < 3; ++C)
                    {
                        const float F0 = 0.04f + (Alb[C] - 0.04f) * Metal;
                        const float F = F0 + (1.0f - F0) * Cos5;
                        const float Spec = Df * G * F / (4.0f * NdotV * NdotL);
                        const float Diff = Alb[C] * (1.0f - Metal) / kPi;
                        Acc[C] += (Diff + Spec) * L.Le[C] * Geo;
                    }
                }
                Out[0] = Acc[0] / static_cast<float>(kLightSamples);
                Out[1] = Acc[1] / static_cast<float>(kLightSamples);
                Out[2] = Acc[2] / static_cast<float>(kLightSamples);
            }
        }
    }

    double LumSum = 0.0;
    for (int Y = 0; Y < H; ++Y)
    {
        for (int X = 0; X < W; ++X)
        {
            const size_t Idx = static_cast<size_t>(Y) * static_cast<size_t>(W) + static_cast<size_t>(X);
            unsigned char* Px = Rgba + Idx * 4u;
            for (int C = 0; C < 3; ++C)
            {
                const float Reinhard = Linear[Idx * 3u + static_cast<size_t>(C)] * Gain;
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

    unsigned char* GlyphSheet = nullptr;
    int GlyphSheetWidth = 0, GlyphSheetHeight = 0;
    IO.Fonts->GetTexDataAsRGBA32(&GlyphSheet, &GlyphSheetWidth, &GlyphSheetHeight);

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
    const uint32_t RowCount = Feed.FillRoster(Rows, Level);
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

    TraversalIndex Traversal;
    if (!Traversal.BuildBottomLevel(Level.QueryFlatTriangles(), false))
    {
        std::printf("[Preview] traversal build failed\n");
        return 1;
    }

    ReSTIRIntegratorConfiguration Config{};
    CelestialSolver Sky;
    std::vector<InstanceRecord> Live = Level.QueryInstances();
    EditorSheet PickedSheet = {};
    Editor.PickInstance(7u);   // Tall Box: the geometry sheet over a live centroid
    (void)Feed.BuildSheet(7u, Rows, RowCount, &PickedSheet, Config, Sky, Camera, Level, Live);
    std::printf("[Preview] pick: row 7 '%s'\n", Rows[7].Label);

    std::vector<unsigned char> Pixels(static_cast<size_t>(kWidth) * static_cast<size_t>(kHeight) * 3u);
    auto Tick = [&](float MouseX, float MouseY, bool Down)
    {
        IO.DeltaTime = 1.0f / 60.0f;
        IO.AddMousePosEvent(MouseX, MouseY);
        IO.AddMouseButtonEvent(0, Down);
        ImGui::NewFrame();
        Editor.Record(Rows, RowCount, &PickedSheet);
        ImGui::Render();
    };
    for (int i = 0; i < 10; ++i)
    {
        IO.DeltaTime = 1.0f / 60.0f;
        IO.AddMousePosEvent(-1.0f, -1.0f);
        IO.AddMouseButtonEvent(0, false);
        ImGui::NewFrame();
        Editor.Record(Rows, RowCount, &PickedSheet);
        ImGui::Render();
    }

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
              Camera.QueryFieldOfViewRadians(), ViewW, ViewH, View.data(), Editor.QueryExposure(), MeanLum);
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

    // Sheet one: the shade shut at boot over the GI view.
    Tick(-1.0f, -1.0f, false);
    Tick(-1.0f, -1.0f, false);
    if (!WriteSheet("Diagnostics/EditorPreviewShut.png"))
        return 1;

    // A tap on the notch slides the shade open; the switch centre seating proves the rows drew.
    const float NotchX = Editor.QueryNotchX(), NotchY = Editor.QueryNotchY();
    Tick(NotchX, NotchY, true);
    Tick(NotchX, NotchY, false);
    for (int i = 0; i < 40; ++i)
        Tick(-1.0f, -1.0f, false);
    std::printf("[Preview] notch tap at (%.0f, %.0f); switch at (%.0f, %.0f)\n",
                static_cast<double>(NotchX), static_cast<double>(NotchY),
                static_cast<double>(Editor.QueryGiSwitchX()), static_cast<double>(Editor.QueryGiSwitchY()));
    if (Editor.QueryGiSwitchX() < 0.0f)
    {
        std::printf("[Preview] the shade never opened\n");
        return 1;
    }
    if (!WriteSheet("Diagnostics/EditorPreview.png"))
        return 1;

    // A tap on the switch seats the raster path; the view re-seats through the visibility raster.
    const float GiX = Editor.QueryGiSwitchX(), GiY = Editor.QueryGiSwitchY();
    Tick(GiX, GiY, true);
    Tick(GiX, GiY, false);
    Tick(-1.0f, -1.0f, false);
    std::printf("[Preview] GI %s after the switch tap\n", Editor.QueryGiEnabled() ? "on" : "off");
    if (Editor.QueryGiEnabled())
    {
        std::printf("[Preview] the switch never toggled\n");
        return 1;
    }
    const auto RasterStart = std::chrono::steady_clock::now();
    double RasterLum = 0.0;
    RasterizeView(Level, Traversal, Eye, Fwd, Camera.QueryRightVector(), Camera.QueryUpwardVector(),
                  Camera.QueryFieldOfViewRadians(), ViewW, ViewH, View.data(), Editor.QueryExposure(), RasterLum);
    const double RasterMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - RasterStart).count();
    std::printf("[Preview] rasterized %d x %d in %.0f ms (mean luminance %.3f)\n", ViewW, ViewH, RasterMs, RasterLum);
    Tick(-1.0f, -1.0f, false);
    Tick(-1.0f, -1.0f, false);
    if (!WriteSheet("Diagnostics/EditorPreviewRaster.png"))
        return 1;
    return 0;
}
