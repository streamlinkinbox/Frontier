//============================================================================================================================================
// 📦 Scratchpad/ReSTIRImageProof.cpp — renders the Cornell box through a CPU transcription of the ReSTIR DI kernel
//============================================================================================================================================
// WHY A TRANSCRIPTION, AND WHAT THAT COSTS
//
// ReSTIRViewport.slang runs on the GPU. This sandbox has none, so the only way to SEE what the round-10 ReSTIR
//    changes do to an image is to run the same algebra on the CPU. That is what this file is: the reservoir /
//    RIS / merge sequence of the kernel, transcribed, over the same scene data the GPU would receive
//    (SceneStructure's flat triangles and luminaire alias table).
//
// ⚠️ Be clear about the limits, because an image is persuasive in a way a number is not:
//    • This is NOT the shipped kernel. It is a second implementation of the same algorithm, so it can only show
//      that the ALGEBRA behaves as claimed — it cannot catch a bug that lives in the GLSL alone.
//    • Shading is Lambert, not the kernel's OpenPBR lobe set. The target function p̂ therefore differs in
//      magnitude from PHatFull's. What is faithful is the STRUCTURE: p̂ ∝ (BSDF · emission · cosθ / d²), the
//      reservoir update rule, the M-clamp, the pairwise-MIS merge, and the carried selection p̂ from #5.
//    • Visibility is brute-force ray-triangle, not the CWBVH. Slower, identical answers.
//
// What it CAN show, and the reason it exists: the difference between spatial-tap counts (#4) at equal sample
//    budget, which is the one round-10 change that alters the image by design and that no existing proof covers.
//
// Modes:  taps=<n>   spatial neighbours per pixel (the #4 knob)
//         spp=<n>    initial candidates M per pixel
//         carry=0|1  #5: carry p̂ of the selection (1) or re-derive it (0) — must be image-identical
//         out=<path>

#include "Engine/ContentInterchange/SceneCodec.h"
#include "Engine/GeometricRaster/SceneStructure.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

using namespace Frontier;

namespace
{

constexpr uint32_t kWidth  = 480u;
constexpr uint32_t kHeight = 360u;
constexpr float    kEps    = 1e-4f;
constexpr float    kPi     = 3.14159265358979f;

// R6 constants, mirrored from ReSTIRViewport.slang.
constexpr uint32_t kTemporalMClamp    = 20u;
constexpr float    kTemporalNormalCos = 0.906f;   // ~25°
constexpr float    kTemporalDepthTol  = 0.10f;

struct Vec3
{
    float X = 0.0f, Y = 0.0f, Z = 0.0f;
    Vec3() = default;
    Vec3(float A, float B, float C) : X(A), Y(B), Z(C) {}
    Vec3 operator+(const Vec3& O) const { return { X + O.X, Y + O.Y, Z + O.Z }; }
    Vec3 operator-(const Vec3& O) const { return { X - O.X, Y - O.Y, Z - O.Z }; }
    Vec3 operator*(float S)       const { return { X * S, Y * S, Z * S }; }
};
float Dot(const Vec3& A, const Vec3& B) { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
Vec3  Cross(const Vec3& A, const Vec3& B) { return { A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X }; }
float Length(const Vec3& A) { return std::sqrt(Dot(A, A)); }
Vec3  Normalise(const Vec3& A) { const float L = Length(A); return L > 0.0f ? A * (1.0f / L) : A; }

// PCG, identical in form to the kernel's, so the sampling character matches.
uint32_t PcgHash(uint32_t Input)
{
    uint32_t State = Input * 747796405u + 2891336453u;
    uint32_t Word  = ((State >> ((State >> 28u) + 4u)) ^ State) * 277803737u;
    return (Word >> 22u) ^ Word;
}
float RandFloat(uint32_t& Seed)
{
    Seed = PcgHash(Seed);
    return static_cast<float>(Seed) * (1.0f / 4294967296.0f);
}
uint32_t MakeSeed(uint32_t X, uint32_t Y, uint32_t Frame)
{
    return PcgHash(X * 1973u + Y * 9277u + Frame * 26699u);
}

struct Triangle
{
    Vec3     A, B, C;
    Vec3     Normal;
    Vec3     Albedo;
    Vec3     Emission;
    float    Area = 0.0f;
};

struct Hit
{
    bool     Valid = false;
    float    T = 0.0f;
    Vec3     Position, Normal, Albedo, Emission;
};

// Möller–Trumbore, the same test the traversal leaf performs.
bool IntersectTriangle(const Triangle& Tri, const Vec3& Origin, const Vec3& Direction, float TMax, float& OutT)
{
    const Vec3 E1 = Tri.B - Tri.A, E2 = Tri.C - Tri.A;
    const Vec3 P  = Cross(Direction, E2);
    const float Det = Dot(E1, P);
    if (std::fabs(Det) < 1e-9f) return false;
    const float Inv = 1.0f / Det;
    const Vec3  T   = Origin - Tri.A;
    const float U   = Dot(T, P) * Inv;
    if (U < 0.0f || U > 1.0f) return false;
    const Vec3  Q   = Cross(T, E1);
    const float V   = Dot(Direction, Q) * Inv;
    if (V < 0.0f || U + V > 1.0f) return false;
    const float Dist = Dot(E2, Q) * Inv;
    if (Dist <= kEps || Dist >= TMax) return false;
    OutT = Dist;
    return true;
}

struct Scene
{
    std::vector<Triangle> Triangles;
    std::vector<uint32_t> Lights;       // indices into Triangles with emission
    std::vector<float>    LightCdf;     // power-proportional, the alias table's continuous cousin
    float                 TotalPower = 0.0f;

    Hit Trace(const Vec3& Origin, const Vec3& Direction, float TMax) const
    {
        Hit Result;
        float Best = TMax;
        for (const Triangle& Tri : Triangles)
        {
            float T;
            if (IntersectTriangle(Tri, Origin, Direction, Best, T)) { Best = T; Result.Valid = true; Result.T = T;
                Result.Normal = Tri.Normal; Result.Albedo = Tri.Albedo; Result.Emission = Tri.Emission; }
        }
        if (Result.Valid) Result.Position = Origin + Direction * Result.T;
        return Result;
    }

    bool Occluded(const Vec3& Origin, const Vec3& Target) const
    {
        Vec3 D = Target - Origin;
        const float Dist = Length(D);
        if (Dist <= 2.0f * kEps) return false;
        D = D * (1.0f / Dist);
        const float TMax = Dist - 2.0f * kEps;
        for (const Triangle& Tri : Triangles)
        {
            float T;
            if (IntersectTriangle(Tri, Origin, D, TMax, T)) return true;
        }
        return false;
    }

    // Power-proportional pick, the same distribution the Walker alias table encodes.
    uint32_t PickLight(uint32_t& Seed, float& OutPdf) const
    {
        const float U = RandFloat(Seed) * TotalPower;
        uint32_t Index = 0u;
        while (Index + 1u < LightCdf.size() && LightCdf[Index] < U) ++Index;
        Index = std::min(Index, static_cast<uint32_t>(Lights.size() - 1u));
        const Triangle& Tri = Triangles[Lights[Index]];
        const float Power = (Tri.Emission.X + Tri.Emission.Y + Tri.Emission.Z) * Tri.Area;
        OutPdf = TotalPower > 0.0f ? Power / TotalPower : 1.0f;
        return Index;
    }

    Vec3 SampleLightPoint(uint32_t LightIndex, float U1, float U2) const
    {
        const Triangle& Tri = Triangles[Lights[LightIndex]];
        float S = U1, T = U2;
        if (S + T > 1.0f) { S = 1.0f - S; T = 1.0f - T; }
        return Tri.A + (Tri.B - Tri.A) * S + (Tri.C - Tri.A) * T;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                  THE TARGET FUNCTION — p̂, the kernel's PHatFull in Lambert form
//------------------------------------------------------------------------------------------------------------------------
// Structure preserved: BSDF · emission · cosθ_surface · cosθ_light · area / d². The kernel folds the light-facing
//    cosine and the area into its solid-angle measure the same way.
float TargetFunction(const Scene& Level, const Hit& Surface, uint32_t LightIndex, const Vec3& LightPoint)
{
    const Vec3 ToLight = LightPoint - Surface.Position;
    const float Dist2  = Dot(ToLight, ToLight);
    if (Dist2 < 1e-9f) return 0.0f;
    const float Dist = std::sqrt(Dist2);
    const Vec3  L    = ToLight * (1.0f / Dist);

    const float NdotL = Dot(Surface.Normal, L);
    if (NdotL <= 0.0f) return 0.0f;

    const Triangle& Light = Level.Triangles[Level.Lights[LightIndex]];
    const float LdotL = -Dot(Light.Normal, L);
    if (LdotL <= 0.0f) return 0.0f;

    const Vec3 Brdf = Surface.Albedo * (1.0f / kPi);
    const Vec3 Contribution{ Brdf.X * Light.Emission.X, Brdf.Y * Light.Emission.Y, Brdf.Z * Light.Emission.Z };
    const float Luma = 0.2126f * Contribution.X + 0.7152f * Contribution.Y + 0.0722f * Contribution.Z;
    return Luma * NdotL * LdotL * Light.Area / Dist2;
}

struct Reservoir
{
    uint32_t SelectedLight = 0u;
    Vec3     SelectedPoint;
    float    WeightSum = 0.0f;
    uint32_t SampleCount = 0u;
    float    UnbiasedWeight = 0.0f;
    float    Depth = 0.0f;
    Vec3     Normal;
    bool     Live = false;
};

void ResampleCandidate(Reservoir& Res, const Vec3& Point, uint32_t Light, float Weight, uint32_t& Seed)
{
    Res.WeightSum   += Weight;
    Res.SampleCount += 1u;
    if (RandFloat(Seed) * Res.WeightSum <= Weight)
    {
        Res.SelectedPoint = Point;
        Res.SelectedLight = Light;
    }
}

struct Options
{
    uint32_t    Taps  = 4u;
    uint32_t    Spp   = 4u;
    bool        Carry = true;
    uint32_t    Frames = 8u;
    std::string Out = "Diagnostics/ReSTIR_Render.png";
};

}   // namespace

int main(int argc, char** argv)
{
    Options Opt;
    for (int I = 1; I < argc; ++I)
    {
        const std::string_view Arg = argv[I];
        if      (Arg.rfind("taps=", 0) == 0)   Opt.Taps   = static_cast<uint32_t>(std::atoi(argv[I] + 5));
        else if (Arg.rfind("spp=", 0) == 0)    Opt.Spp    = static_cast<uint32_t>(std::atoi(argv[I] + 4));
        else if (Arg.rfind("carry=", 0) == 0)  Opt.Carry  = std::atoi(argv[I] + 6) != 0;
        else if (Arg.rfind("frames=", 0) == 0) Opt.Frames = static_cast<uint32_t>(std::atoi(argv[I] + 7));
        else if (Arg.rfind("out=", 0) == 0)    Opt.Out    = argv[I] + 4;
    }

    // ── The scene, decoded exactly as the engine decodes it ────────────────────────────────────────────────────
    SceneStructure Level;
    std::string Error;
    if (!SceneCodec::Decode("Projects/Project-Zero/Content/Scenes/CornellBox.gltf", Level, nullptr,
                            SceneDecodeConfiguration{}, &Error))
    {
        std::printf("[ReSTIRImage] could not decode the Cornell box: %s\n", Error.c_str());
        return 1;
    }

    Scene World;
    const std::vector<TriangleIndex>& Flat = Level.QueryFlatTriangles();
    const MaterialIndex& Materials = Level.QueryMaterials();
    for (const TriangleIndex& T : Flat)
    {
        Triangle Tri;
        Tri.A = { T.VertexAlphaX, T.VertexAlphaY, T.VertexAlphaZ };
        Tri.B = { T.VertexBetaX,  T.VertexBetaY,  T.VertexBetaZ  };
        Tri.C = { T.VertexGammaX, T.VertexGammaY, T.VertexGammaZ };
        const Vec3 N = Cross(Tri.B - Tri.A, Tri.C - Tri.A);
        Tri.Area   = 0.5f * Length(N);
        Tri.Normal = Normalise(N);

        uint32_t Slot = 0u;
        std::memcpy(&Slot, &T.MaterialSlot, sizeof(Slot));
        const std::vector<MaterialRecord>& Records = Materials.QueryRecords();
        if (Slot < Records.size())
        {
            const MaterialRecord& M = Records[Slot];
            Tri.Albedo   = Vec3{ M.AlbedoR,   M.AlbedoG,   M.AlbedoB   };
            Tri.Emission = Vec3{ M.EmissiveR, M.EmissiveG, M.EmissiveB };
        }
        World.Triangles.push_back(Tri);
    }

    float Running = 0.0f;
    for (uint32_t I = 0u; I < World.Triangles.size(); ++I)
    {
        const Triangle& Tri = World.Triangles[I];
        const float Power = (Tri.Emission.X + Tri.Emission.Y + Tri.Emission.Z) * Tri.Area;
        if (Power > 0.0f) { World.Lights.push_back(I); Running += Power; World.LightCdf.push_back(Running); }
    }
    World.TotalPower = Running;

    std::printf("[ReSTIRImage] %zu triangles, %zu emissive\n", World.Triangles.size(), World.Lights.size());
    std::printf("[ReSTIRImage] taps %u, spp %u, carry %s, %u frames -> %s\n",
                Opt.Taps, Opt.Spp, Opt.Carry ? "on" : "off", Opt.Frames, Opt.Out.c_str());
    if (World.Lights.empty()) { std::printf("[ReSTIRImage] no emissive geometry\n"); return 1; }

    // ── Camera: the Cornell seating the other proofs use ───────────────────────────────────────────────────────
    const Vec3 Eye{ 0.0f, -3.30f, 1.55f };
    const Vec3 Forward{ 0.0f, 1.0f, 0.0f };
    const Vec3 Up{ 0.0f, 0.0f, 1.0f };
    const Vec3 Right = Normalise(Cross(Forward, Up));
    const float TanHalf = std::tan(1.0471976f * 0.5f);
    const float Aspect  = static_cast<float>(kWidth) / static_cast<float>(kHeight);

    std::vector<Reservoir> Previous(static_cast<size_t>(kWidth) * kHeight);
    std::vector<Reservoir> Current(static_cast<size_t>(kWidth) * kHeight);
    std::vector<float>     Accum(static_cast<size_t>(kWidth) * kHeight * 3u, 0.0f);

    for (uint32_t Frame = 0u; Frame < Opt.Frames; ++Frame)
    {
        for (uint32_t Y = 0u; Y < kHeight; ++Y)
        {
            for (uint32_t X = 0u; X < kWidth; ++X)
            {
                const size_t Pixel = static_cast<size_t>(Y) * kWidth + X;
                uint32_t Seed = MakeSeed(X, Y, Frame);

                // Primary ray (jittered, as the kernel's AA does).
                const float Jx = Opt.Frames > 1u ? RandFloat(Seed) : 0.5f;
                const float Jy = Opt.Frames > 1u ? RandFloat(Seed) : 0.5f;
                const float Sx = (2.0f * (static_cast<float>(X) + Jx) / static_cast<float>(kWidth)  - 1.0f) * TanHalf * Aspect;
                const float Sy = (1.0f - 2.0f * (static_cast<float>(Y) + Jy) / static_cast<float>(kHeight)) * TanHalf;
                const Vec3 Direction = Normalise(Forward + Right * Sx + Up * Sy);

                const Hit Surface = World.Trace(Eye, Direction, 1e30f);
                Reservoir Res;
                Vec3 Colour;

                if (!Surface.Valid)
                {
                    Current[Pixel] = Res;
                    Accum[Pixel * 3u + 0u] += 0.0f; Accum[Pixel * 3u + 1u] += 0.0f; Accum[Pixel * 3u + 2u] += 0.0f;
                    continue;
                }

                if (Surface.Emission.X + Surface.Emission.Y + Surface.Emission.Z > 0.0f)
                {
                    Colour = Surface.Emission;
                    Current[Pixel] = Res;
                    Accum[Pixel * 3u + 0u] += Colour.X; Accum[Pixel * 3u + 1u] += Colour.Y; Accum[Pixel * 3u + 2u] += Colour.Z;
                    continue;
                }

                // ── Initial candidates: M light samples, resampled with RIS ────────────────────────────────────
                for (uint32_t S = 0u; S < Opt.Spp; ++S)
                {
                    float Pdf = 1.0f;
                    const uint32_t Light = World.PickLight(Seed, Pdf);
                    const float U1 = RandFloat(Seed), U2 = RandFloat(Seed);
                    const Vec3 Point = World.SampleLightPoint(Light, U1, U2);
                    const float PHat = TargetFunction(World, Surface, Light, Point);
                    ResampleCandidate(Res, Point, Light, Pdf > 0.0f ? PHat / Pdf : 0.0f, Seed);
                }

                float PSelected = 0.0f;
                if (Res.WeightSum > 0.0f && Res.SampleCount > 0u)
                {
                    PSelected = TargetFunction(World, Surface, Res.SelectedLight, Res.SelectedPoint);
                    Res.UnbiasedWeight = PSelected > 0.0f
                        ? Res.WeightSum / (static_cast<float>(Res.SampleCount) * PSelected) : 0.0f;
                }
                Res.Depth  = Surface.T;
                Res.Normal = Surface.Normal;
                Res.Live   = true;

                // ── Temporal reuse — same pixel (no motion in this still) ──────────────────────────────────────
                if (Frame > 0u)
                {
                    const Reservoir& Prev = Previous[Pixel];
                    const bool Valid = Prev.Live && Prev.SampleCount > 0u
                        && Dot(Surface.Normal, Prev.Normal) > kTemporalNormalCos
                        && std::fabs(Surface.T - Prev.Depth) / std::max(Surface.T, 1e-3f) < kTemporalDepthTol;
                    if (Valid)
                    {
                        const uint32_t MCapped = std::min(Prev.SampleCount, kTemporalMClamp * Res.SampleCount);
                        const float PPrev = TargetFunction(World, Surface, Prev.SelectedLight, Prev.SelectedPoint);
                        // #5: p̂ of the current selection is carried, not re-derived. carry=0 re-derives it, which
                        //    must produce the identical image — that is the switch this mode exists to test.
                        const float PCur  = Opt.Carry ? PSelected
                                                      : TargetFunction(World, Surface, Res.SelectedLight, Res.SelectedPoint);
                        const float WCur  = PCur  * Res.UnbiasedWeight * static_cast<float>(Res.SampleCount);
                        const float WPrev = PPrev * Prev.UnbiasedWeight * static_cast<float>(MCapped);
                        const float Total = WCur + WPrev;
                        if (Total > 0.0f && RandFloat(Seed) * Total <= WPrev)
                        {
                            Res.SelectedPoint = Prev.SelectedPoint;
                            Res.SelectedLight = Prev.SelectedLight;
                            PSelected = PPrev;
                        }
                        Res.SampleCount += MCapped;
                        Res.WeightSum    = Total;
                        const float PFin = Opt.Carry ? PSelected
                                                     : TargetFunction(World, Surface, Res.SelectedLight, Res.SelectedPoint);
                        Res.UnbiasedWeight = PFin > 0.0f ? Total / (static_cast<float>(Res.SampleCount) * PFin) : 0.0f;
                        PSelected = PFin;
                    }
                }

                // ── Spatial reuse — the #4 cross, Taps neighbours from the stable previous buffer ──────────────
                if (Frame > 0u && Opt.Taps > 0u)
                {
                    uint32_t TapSeed = PcgHash(MakeSeed(X, Y, Frame) ^ 0x9E3779B9u);
                    const float Angle  = RandFloat(TapSeed) * 6.28318531f;
                    const float Radius = (6.0f + 18.0f * RandFloat(TapSeed));
                    for (uint32_t Tap = 0u; Tap < Opt.Taps; ++Tap)
                    {
                        const float Theta = Angle + static_cast<float>(Tap) * (6.28318531f / static_cast<float>(Opt.Taps));
                        const int Ox = static_cast<int>(std::lround(Radius * std::cos(Theta)));
                        const int Oy = static_cast<int>(std::lround(Radius * std::sin(Theta)));
                        if (Ox == 0 && Oy == 0) continue;
                        const int Nx = static_cast<int>(X) + Ox, Ny = static_cast<int>(Y) + Oy;
                        if (Nx < 0 || Ny < 0 || Nx >= static_cast<int>(kWidth) || Ny >= static_cast<int>(kHeight)) continue;

                        const Reservoir& Neigh = Previous[static_cast<size_t>(Ny) * kWidth + static_cast<size_t>(Nx)];
                        if (!Neigh.Live || Neigh.SampleCount == 0u) continue;
                        if (Dot(Surface.Normal, Neigh.Normal) <= kTemporalNormalCos) continue;
                        if (std::fabs(Surface.T - Neigh.Depth) / std::max(Surface.T, 1e-3f) >= kTemporalDepthTol) continue;

                        const uint32_t MCapped = std::min(Neigh.SampleCount, kTemporalMClamp * Res.SampleCount);
                        const float PNeigh = TargetFunction(World, Surface, Neigh.SelectedLight, Neigh.SelectedPoint);
                        const float PSelf  = Opt.Carry ? PSelected
                                                       : TargetFunction(World, Surface, Res.SelectedLight, Res.SelectedPoint);
                        const float WSelf  = PSelf  * Res.UnbiasedWeight * static_cast<float>(Res.SampleCount);
                        const float WNeigh = PNeigh * Neigh.UnbiasedWeight * static_cast<float>(MCapped);
                        const float Total  = WSelf + WNeigh;
                        if (Total > 0.0f && RandFloat(Seed) * Total <= WNeigh)
                        {
                            Res.SelectedPoint = Neigh.SelectedPoint;
                            Res.SelectedLight = Neigh.SelectedLight;
                            PSelected = PNeigh;
                        }
                        Res.SampleCount += MCapped;
                        Res.WeightSum    = Total;
                        const float PM = Opt.Carry ? PSelected
                                                   : TargetFunction(World, Surface, Res.SelectedLight, Res.SelectedPoint);
                        Res.UnbiasedWeight = PM > 0.0f ? Total / (static_cast<float>(Res.SampleCount) * PM) : 0.0f;
                        PSelected = PM;
                    }
                }

                // ── Shade the surviving selection, visibility re-traced at this pixel ──────────────────────────
                if (Res.UnbiasedWeight > 0.0f && Res.SampleCount > 0u)
                {
                    const Vec3 Origin = Surface.Position + Surface.Normal * (kEps * 10.0f);
                    if (!World.Occluded(Origin, Res.SelectedPoint))
                    {
                        const Vec3  ToLight = Res.SelectedPoint - Surface.Position;
                        const float Dist2   = std::max(Dot(ToLight, ToLight), 1e-9f);
                        const Vec3  L       = ToLight * (1.0f / std::sqrt(Dist2));
                        const Triangle& Light = World.Triangles[World.Lights[Res.SelectedLight]];
                        const float NdotL = std::max(0.0f, Dot(Surface.Normal, L));
                        const float LdotL = std::max(0.0f, -Dot(Light.Normal, L));
                        const float Geometry = NdotL * LdotL * Light.Area / Dist2;
                        const Vec3  Brdf = Surface.Albedo * (1.0f / kPi);
                        Colour = Vec3{ Brdf.X * Light.Emission.X, Brdf.Y * Light.Emission.Y, Brdf.Z * Light.Emission.Z }
                               * (Geometry * Res.UnbiasedWeight);
                    }
                }

                // A small ambient term so the unlit corners are not pure black, matching the raster's kAmbient.
                Colour = Colour + Surface.Albedo * 0.045f;

                Current[Pixel] = Res;
                Accum[Pixel * 3u + 0u] += Colour.X;
                Accum[Pixel * 3u + 1u] += Colour.Y;
                Accum[Pixel * 3u + 2u] += Colour.Z;
            }
        }
        Previous.swap(Current);
        std::printf("  frame %u/%u\n", Frame + 1u, Opt.Frames);
        std::fflush(stdout);
    }

    // ── Resolve: mean, Reinhard, gamma — the kernel's tail ─────────────────────────────────────────────────────
    std::vector<unsigned char> Rgb(static_cast<size_t>(kWidth) * kHeight * 3u);
    const float InvFrames = 1.0f / static_cast<float>(Opt.Frames);
    for (size_t I = 0u; I < static_cast<size_t>(kWidth) * kHeight; ++I)
    {
        for (uint32_t C = 0u; C < 3u; ++C)
        {
            float V = Accum[I * 3u + C] * InvFrames;
            V = V / (1.0f + V);
            V = std::pow(std::max(V, 0.0f), 1.0f / 2.2f);
            Rgb[I * 3u + C] = static_cast<unsigned char>(std::clamp(V * 255.0f + 0.5f, 0.0f, 255.0f));
        }
    }
    stbi_write_png(Opt.Out.c_str(), static_cast<int>(kWidth), static_cast<int>(kHeight), 3,
                   Rgb.data(), static_cast<int>(kWidth) * 3);
    std::printf("[ReSTIRImage] wrote %s\n", Opt.Out.c_str());
    return 0;
}
