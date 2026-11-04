//============================================================================================================================
// 🧩 VehiclePhysics exhibit — SceneRender.cpp
//============================================================================================================================
// 📝 Fast flat/matcap software render of the actual ControlVehicle, for visual proof of the Phase-7 physics. Geometry is
//    the authored geometry itself: the demonstration-archive facet streams (ControlVehicleBody/Tyre/Rim.facet, encoded
//    from Frontier's ControlVehicleStructure / RubberFL / RimFL .inl archives by FacetArchiveCodec.py), assembled the
//    way FacetStructure.js assembles them (right wheels Y-mirrored with winding restored, rim hard-shaded and scaled to
//    the configured radius, tyre smooth). Materials are the authored linear albedos of MaterialSpecification.js.
//    Every frame is posed by VehicleSolver. Spring tyres use the demo's analytical fallback; projection and solid
//    modes interpolate their nodal displacements as in VehicleVisual.wgsl. The cone scene is
//    stepped by Jolt 5.2.0 through the same chassis-proxy exchange as JoltScene.cpp. The rasterizer is a small
//    z-buffered triangle renderer (lambert + hemisphere + Blinn sheen, planar projected shadow, sRGB output) — a
//    proof visual, not the engine renderer.
//    Usage: SceneRender <mode> <facetdir> <outdir>   modes: hero | tyre-stills | gif-drive | gif-burnout | gif-jolt
//============================================================================================================================

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Engine/Physics/TyreCarcassLattice.h"
#include "Engine/Physics/TyreDeformation.h"
#include "Engine/Physics/VehicleSolver.h"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

using namespace Frontier;

namespace
{
    constexpr double Pi = 3.141592653589793;

    // --- small float vector helpers ---
    struct F3 { float X = 0, Y = 0, Z = 0; };
    F3 operator+(F3 A, F3 B) { return { A.X + B.X, A.Y + B.Y, A.Z + B.Z }; }
    F3 operator-(F3 A, F3 B) { return { A.X - B.X, A.Y - B.Y, A.Z - B.Z }; }
    F3 operator*(F3 A, float S) { return { A.X * S, A.Y * S, A.Z * S }; }
    float Dot(F3 A, F3 B) { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
    F3 Cross(F3 A, F3 B) { return { A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X }; }
    float Len(F3 A) { return std::sqrt(Dot(A, A)); }
    F3 Norm(F3 A) { const float L = Len(A); return L > 1e-12f ? A * (1.0f / L) : F3{}; }
    float ClampF(float V, float Lo, float Hi) { return V < Lo ? Lo : (V > Hi ? Hi : V); }

    F3 RotateD(const double Q[4], F3 V)
    {
        const F3 Axis = { (float)Q[0], (float)Q[1], (float)Q[2] };
        const F3 Twice = Cross(Axis, V) * 2.0f;
        return V + Twice * (float)Q[3] + Cross(Axis, Twice);
    }
    void MulQuat(const double L[4], const double R[4], double Out[4])
    {
        Out[0] = L[3] * R[0] + L[0] * R[3] + L[1] * R[2] - L[2] * R[1];
        Out[1] = L[3] * R[1] - L[0] * R[2] + L[1] * R[3] + L[2] * R[0];
        Out[2] = L[3] * R[2] + L[0] * R[1] - L[1] * R[0] + L[2] * R[3];
        Out[3] = L[3] * R[3] - L[0] * R[0] - L[1] * R[1] - L[2] * R[2];
    }
    void AxisQuat(double Ax, double Ay, double Az, double Angle, double Out[4])
    {
        const double S = std::sin(0.5 * Angle);
        Out[0] = Ax * S; Out[1] = Ay * S; Out[2] = Az * S; Out[3] = std::cos(0.5 * Angle);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                   FACET STREAMS
    //--------------------------------------------------------------------------------------------------------------------

    struct FacetMesh
    {
        std::vector<F3> Positions;            // [m] authored frame
        std::vector<F3> VertexNormals;        // [m] averaged from corner normals (smooth streams)
        std::vector<uint32_t> Facets;         // 3 per facet
        std::vector<uint32_t> Families;       // 1 per facet
        bool Smooth = false;
    };

    bool LoadFacet(const std::string& Path, FacetMesh& Out)
    {
        std::FILE* File = std::fopen(Path.c_str(), "rb");
        if (!File) { std::printf("cannot open %s\n", Path.c_str()); return false; }
        std::fseek(File, 0, SEEK_END);
        const long Size = std::ftell(File);
        std::fseek(File, 0, SEEK_SET);
        std::vector<uint8_t> Data((size_t)Size);
        if (std::fread(Data.data(), 1, Data.size(), File) != Data.size()) { std::fclose(File); return false; }
        std::fclose(File);
        if (Size < 48 || std::memcmp(Data.data(), "FCT1", 4) != 0) { std::printf("bad facet magic %s\n", Path.c_str()); return false; }
        uint32_t VertexCount, FacetCount, Flags;
        std::memcpy(&VertexCount, Data.data() + 4, 4);
        std::memcpy(&FacetCount, Data.data() + 8, 4);
        std::memcpy(&Flags, Data.data() + 12, 4);
        const float* Positions = reinterpret_cast<const float*>(Data.data() + 48);
        const uint32_t* Facets = reinterpret_cast<const uint32_t*>(Data.data() + 48 + (size_t)VertexCount * 12);
        const uint32_t* Families = Facets + (size_t)FacetCount * 3;
        Out.Positions.resize(VertexCount);
        for (uint32_t I = 0; I < VertexCount; ++I)
            Out.Positions[I] = { Positions[I * 3], Positions[I * 3 + 1], Positions[I * 3 + 2] };
        Out.Facets.assign(Facets, Facets + (size_t)FacetCount * 3);
        Out.Families.assign(Families, Families + FacetCount);
        Out.VertexNormals.assign(VertexCount, F3{});
        Out.Smooth = (Flags & 1) != 0;
        if (Out.Smooth)
        {
            const float* Corner = reinterpret_cast<const float*>(Families + FacetCount);
            for (uint32_t F = 0; F < FacetCount; ++F)
                for (int C = 0; C < 3; ++C)
                {
                    const uint32_t V = Out.Facets[F * 3 + C];
                    const float* N = Corner + (F * 9 + C * 3);
                    Out.VertexNormals[V] = Out.VertexNormals[V] + F3{ N[0], N[1], N[2] };
                }
            for (F3& N : Out.VertexNormals) N = Norm(N);
        }
        std::printf("loaded %s: %u verts %u facets smooth=%d\n", Path.c_str(), VertexCount, FacetCount, (int)Out.Smooth);
        return true;
    }

    // 📝 FacetStructure.js ProjectWheelFacets: right wheels mirror Y (positions + normals), corners 1↔2 swapped to
    //    restore winding; the rim scales to the configured radius and renders hard-shaded.
    FacetMesh MirrorY(const FacetMesh& Source, float Scale)
    {
        FacetMesh Out = Source;
        for (size_t I = 0; I < Out.Positions.size(); ++I)
        {
            Out.Positions[I] = { Source.Positions[I].X * Scale, -Source.Positions[I].Y * Scale, Source.Positions[I].Z * Scale };
            Out.VertexNormals[I] = { Source.VertexNormals[I].X, -Source.VertexNormals[I].Y, Source.VertexNormals[I].Z };
        }
        for (size_t F = 0; F < Out.Facets.size(); F += 3)
            std::swap(Out.Facets[F + 1], Out.Facets[F + 2]);
        return Out;
    }
    FacetMesh Scaled(const FacetMesh& Source, float Scale)
    {
        FacetMesh Out = Source;
        for (F3& P : Out.Positions) P = P * Scale;
        return Out;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                   MATERIALS (MaterialSpecification.js, linear)
    //--------------------------------------------------------------------------------------------------------------------

    struct Material { float Albedo[3]; float Roughness; float Metal; float Emission[3]; };
    Material ResolveMaterial(uint32_t Family)
    {
        switch (Family)
        {
            case 6:  return { { 0.530f, 0.624f, 0.0f }, 0.0775f, 0.0f, { 0, 0, 0 } };            // BodyCoat (×0.78 headroom)
            case 7:  return { { 0.026f, 0.028f, 0.030f }, 0.02f, 0.0f, { 0, 0, 0 } };            // VehicleGlass
            case 8:  return { { 0.012f, 0.014f, 0.018f }, 0.50f, 0.0f, { 0, 0, 0 } };            // VehiclePlastic
            case 10: return { { 0.06f, 0.22f, 0.62f }, 0.22f, 1.0f, { 0.0f, 0.0033f, 0.012f } }; // Hub (blue mag)
            case 12: return { { 0.8f, 0.8f, 0.8f }, 0.50f, 0.0f, { 0, 0, 0 } };                  // VehicleTrim
            case 13: return { { 0.55f, 0.57f, 0.60f }, 0.10f, 0.0f, { 0.55f, 0.55f, 0.52f } };    // Headlight (scaled emission)
            case 14: return { { 0.8f, 0.8f, 0.8f }, 0.10f, 0.0f, { 0.8f, 0.0f, 0.013f } };        // Taillight
            case 15: return { { 0.8f, 0.8f, 0.8f }, 0.50f, 0.0f, { 0, 0, 0 } };                  // VehicleDefault
            case 9:  return { { 0.0374f, 0.0374f, 0.0374f }, 0.76087f, 0.0f, { 0, 0, 0 } };      // Tyre / RimBase
            default: return { { 0.4f, 0.4f, 0.42f }, 0.6f, 0.0f, { 0, 0, 0 } };
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                   RASTERIZER
    //--------------------------------------------------------------------------------------------------------------------

    struct Camera
    {
        F3 Position, Right, Up, Forward;
        float TanHalfFovY = 0.5f, Aspect = 1.0f;
    };
    Camera LookAt(F3 Position, F3 Target, float FovYDegrees, float Aspect)
    {
        Camera C;
        C.Position = Position;
        C.Forward = Norm(Target - Position);
        C.Right = Norm(Cross(C.Forward, F3{ 0, 0, 1 }));
        C.Up = Cross(C.Right, C.Forward);
        C.TanHalfFovY = std::tan(FovYDegrees * (float)Pi / 360.0f);
        C.Aspect = Aspect;
        return C;
    }

    struct Renderer
    {
        int W = 0, H = 0;
        std::vector<float> Colour;    // linear RGB
        std::vector<float> Depth;
        std::vector<F3> WorldPos;     // per-pixel world position (ground shadow test)
        Camera Cam;
        F3 SunDir = Norm(F3{ 0.55f, -0.48f, 0.68f });   // toward the sun (behind the hero camera)
        F3 SunColour = { 1.30f, 1.22f, 1.05f };

        void Init(int Width, int Height)
        {
            W = Width; H = Height;
            Colour.assign((size_t)W * H * 3, 0.0f);
            Depth.assign((size_t)W * H, 1.0e30f);
            WorldPos.assign((size_t)W * H, F3{ 0, 0, 1.0e9f });
        }
        void Background()
        {
            for (int Y = 0; Y < H; ++Y)
            {
                const float T = (float)Y / (float)H;                       // 0 top
                const float Horizon = std::pow(T, 1.6f);
                const F3 Sky = { 0.10f + 0.35f * Horizon, 0.20f + 0.34f * Horizon, 0.42f + 0.30f * Horizon };
                for (int X = 0; X < W; ++X)
                {
                    float* C = &Colour[((size_t)Y * W + X) * 3];
                    C[0] = Sky.X; C[1] = Sky.Y; C[2] = Sky.Z;
                }
            }
            std::fill(Depth.begin(), Depth.end(), 1.0e30f);
            std::fill(WorldPos.begin(), WorldPos.end(), F3{ 0, 0, 1.0e9f });
        }
        // Project: returns view depth; Screen in pixels (origin bottom-left math, flipped at write).
        float Project(F3 P, float& Sx, float& Sy) const
        {
            const F3 D = P - Cam.Position;
            const float Z = Dot(D, Cam.Forward);
            const float X = Dot(D, Cam.Right);
            const float Y = Dot(D, Cam.Up);
            if (Z < 0.05f) { Sx = Sy = 0; return Z; }
            Sx = (0.5f + 0.5f * X / (Z * Cam.TanHalfFovY * Cam.Aspect)) * (float)W;
            Sy = (0.5f + 0.5f * Y / (Z * Cam.TanHalfFovY)) * (float)H;
            return Z;
        }
        F3 Shade(F3 Albedo, float Roughness, float Metal, F3 Emission, F3 N, F3 World) const
        {
            const float NdotL = std::fmax(0.0f, Dot(N, SunDir));
            const float HemiMix = 0.5f + 0.5f * N.Z;
            const F3 Hemi = { 0.10f + 0.20f * HemiMix, 0.12f + 0.26f * HemiMix, 0.15f + 0.37f * HemiMix };
            F3 Out = { Albedo.X * (Hemi.X + SunColour.X * NdotL), Albedo.Y * (Hemi.Y + SunColour.Y * NdotL),
                       Albedo.Z * (Hemi.Z + SunColour.Z * NdotL) };
            // 📝 Reuse the view distance for haze; matte ground needs no half-vector or power evaluation.
            const F3 ViewOffset = Cam.Position - World;
            const float Distance = Len(ViewOffset);
            if (Roughness < 0.85f && NdotL > 0.0f)
            {
                const F3 View = ViewOffset * (1.0f / std::fmax(Distance, 1.0e-12f));
                const F3 Half = Norm(View + SunDir);
                const float Exponent = 2.0f + (1.0f - Roughness) * 220.0f;
                const float Spec = std::pow(std::fmax(0.0f, Dot(N, Half)), Exponent) *
                                   (0.04f + 0.5f * (1.0f - Roughness));
                const F3 SpecTint = Metal > 0.5f ? Albedo * 3.0f : F3{ 1, 1, 1 };
                Out = Out + SpecTint * (Spec * NdotL * 1.2f);
            }
            Out = Out + Emission;
            const float Haze = 1.0f - std::exp(-Distance * 0.0016f);
            const F3 HazeColour = { 0.52f, 0.59f, 0.72f };
            return Out * (1.0f - Haze * 0.55f) + HazeColour * (Haze * 0.55f);
        }
        // Smooth = interpolate normals; otherwise the first normal rules (flat).
        void Triangle(F3 P0, F3 P1, F3 P2, F3 N0, F3 N1, F3 N2, const Material& Mat, bool Smooth, F3 AlbedoMul)
        {
            float Sx[3], Sy[3], Zv[3];
            const F3 Ps[3] = { P0, P1, P2 };
            for (int I = 0; I < 3; ++I)
            {
                Zv[I] = Project(Ps[I], Sx[I], Sy[I]);
                if (Zv[I] < 0.05f) return;
            }
            const float MinX = std::fmax(0.0f, std::fmin(Sx[0], std::fmin(Sx[1], Sx[2])));
            const float MaxX = std::fmin((float)W - 1, std::fmax(Sx[0], std::fmax(Sx[1], Sx[2])));
            const float MinY = std::fmax(0.0f, std::fmin(Sy[0], std::fmin(Sy[1], Sy[2])));
            const float MaxY = std::fmin((float)H - 1, std::fmax(Sy[0], std::fmax(Sy[1], Sy[2])));
            const float Denom = (Sy[1] - Sy[2]) * (Sx[0] - Sx[2]) + (Sx[2] - Sx[1]) * (Sy[0] - Sy[2]);
            if (std::fabs(Denom) < 1e-9f) return;
            const F3 Ns[3] = { N0, N1, N2 };
            const float InvZ[3] = { 1.0f / Zv[0], 1.0f / Zv[1], 1.0f / Zv[2] };
            for (int Py = (int)MinY; Py <= (int)MaxY; ++Py)
                for (int Px = (int)MinX; Px <= (int)MaxX; ++Px)
                {
                    const float X = Px + 0.5f, Y = Py + 0.5f;
                    const float W0 = ((Sy[1] - Sy[2]) * (X - Sx[2]) + (Sx[2] - Sx[1]) * (Y - Sy[2])) / Denom;
                    const float W1 = ((Sy[2] - Sy[0]) * (X - Sx[2]) + (Sx[0] - Sx[2]) * (Y - Sy[2])) / Denom;
                    const float W2 = 1.0f - W0 - W1;
                    if (W0 < 0.0f || W1 < 0.0f || W2 < 0.0f) continue;
                    const float InvDepth = W0 * InvZ[0] + W1 * InvZ[1] + W2 * InvZ[2];
                    const float Z = 1.0f / InvDepth;
                    const int Row = H - 1 - Py;
                    const size_t Index = (size_t)Row * W + Px;
                    if (Z >= Depth[Index]) continue;
                    Depth[Index] = Z;
                    const F3 World = (Ps[0] * (W0 * InvZ[0]) + Ps[1] * (W1 * InvZ[1]) + Ps[2] * (W2 * InvZ[2])) * Z;
                    WorldPos[Index] = World;
                    F3 N = N0;
                    if (Smooth)
                        N = Norm((Ns[0] * (W0 * InvZ[0]) + Ns[1] * (W1 * InvZ[1]) + Ns[2] * (W2 * InvZ[2])) * Z);
                    const F3 Albedo = { Mat.Albedo[0] * AlbedoMul.X, Mat.Albedo[1] * AlbedoMul.Y, Mat.Albedo[2] * AlbedoMul.Z };
                    const F3 Emis = { Mat.Emission[0], Mat.Emission[1], Mat.Emission[2] };
                    const F3 C = Shade(Albedo, Mat.Roughness, Mat.Metal, Emis, N, World);
                    float* Out = &Colour[Index * 3];
                    Out[0] = C.X; Out[1] = C.Y; Out[2] = C.Z;
                }
        }
        // Planar projected shadow: rasterize onto the stored ground pixels only.
        void ShadowTriangle(F3 P0, F3 P1, F3 P2)
        {
            if (SunDir.Z <= 0.001f) return;
            // 📝 Block direct sunlight, not the blue hemispherical fill. No additional shadow buffer.
            const F3 ShadowRatio = { 0.30f / (0.30f + SunColour.X * SunDir.Z),
                                     0.38f / (0.38f + SunColour.Y * SunDir.Z),
                                     0.52f / (0.52f + SunColour.Z * SunDir.Z) };
            const auto Flatten = [&](F3 P)
            {
                const float T = P.Z / SunDir.Z;
                return F3{ P.X - SunDir.X * T, P.Y - SunDir.Y * T, 0.0f };
            };
            const F3 Fs[3] = { Flatten(P0), Flatten(P1), Flatten(P2) };
            float Sx[3], Sy[3];
            for (int I = 0; I < 3; ++I)
                if (Project(Fs[I], Sx[I], Sy[I]) < 0.05f) return;
            const float MinX = std::fmax(0.0f, std::fmin(Sx[0], std::fmin(Sx[1], Sx[2])));
            const float MaxX = std::fmin((float)W - 1, std::fmax(Sx[0], std::fmax(Sx[1], Sx[2])));
            const float MinY = std::fmax(0.0f, std::fmin(Sy[0], std::fmin(Sy[1], Sy[2])));
            const float MaxY = std::fmin((float)H - 1, std::fmax(Sy[0], std::fmax(Sy[1], Sy[2])));
            const float Denom = (Sy[1] - Sy[2]) * (Sx[0] - Sx[2]) + (Sx[2] - Sx[1]) * (Sy[0] - Sy[2]);
            if (std::fabs(Denom) < 1e-9f) return;
            for (int Py = (int)MinY; Py <= (int)MaxY; ++Py)
                for (int Px = (int)MinX; Px <= (int)MaxX; ++Px)
                {
                    const float X = Px + 0.5f, Y = Py + 0.5f;
                    const float W0 = ((Sy[1] - Sy[2]) * (X - Sx[2]) + (Sx[2] - Sx[1]) * (Y - Sy[2])) / Denom;
                    const float W1 = ((Sy[2] - Sy[0]) * (X - Sx[2]) + (Sx[0] - Sx[2]) * (Y - Sy[2])) / Denom;
                    const float W2 = 1.0f - W0 - W1;
                    if (W0 < 0.0f || W1 < 0.0f || W2 < 0.0f) continue;
                    const size_t Index = (size_t)(H - 1 - Py) * W + Px;
                    if (WorldPos[Index].Z > 0.02f) continue;      // ground pixels only (z == 0)
                    float* Out = &Colour[Index * 3];
                    Out[0] *= ShadowRatio.X; Out[1] *= ShadowRatio.Y; Out[2] *= ShadowRatio.Z;
                    WorldPos[Index].Z = 0.03f;                    // darken once
                }
        }
        void GroundPlane()
        {
            // Intersect view rays with the flat test ground. One scanline has a constant view depth,
            // so this avoids clipped tile holes, thousands of checker triangles and per-fragment shading.
            // This is still the cheap test raster, not the engine's terrain or a fidelity reference.
            const float NdotL = std::fmax(0.0f, SunDir.Z);
            const F3 Base = { 0.255f * (0.30f + SunColour.X * NdotL),
                              0.255f * (0.38f + SunColour.Y * NdotL),
                              0.265f * (0.52f + SunColour.Z * NdotL) };
            const F3 Haze = { 0.52f, 0.59f, 0.72f };
            const float StepX = 2.0f * Cam.TanHalfFovY * Cam.Aspect / W;
            for (int Y = 0; Y < H; ++Y)
            {
                const float Vertical = (1.0f - 2.0f * (Y + 0.5f) / H) * Cam.TanHalfFovY;
                const F3 Ray = Cam.Forward + Cam.Up * Vertical + Cam.Right * ((0.5f - 0.5f * W) * StepX);
                if (Ray.Z >= -1e-7f) continue;
                const float Z = -Cam.Position.Z / Ray.Z;
                if (Z < 0.05f) continue;
                const float Fog = 0.55f * ClampF(Z * 0.0016f, 0.0f, 1.0f);
                const F3 Ground = Base * (1.0f - Fog) + Haze * Fog;
                F3 World = Cam.Position + Ray * Z;
                const F3 Delta = Cam.Right * (StepX * Z);
                for (int X = 0; X < W; ++X)
                {
                    const size_t Index = (size_t)Y * W + X;
                    Depth[Index] = Z;
                    WorldPos[Index] = { World.X, World.Y, 0.0f };
                    float* Out = &Colour[Index * 3];
                    Out[0] = Ground.X; Out[1] = Ground.Y; Out[2] = Ground.Z;
                    World = World + Delta;
                }
            }
        }
        void Disc(F3 Centre, float RadiusWorld, F3 ColourLinear)
        {
            float Sx, Sy;
            const float Z = Project(Centre, Sx, Sy);
            if (Z < 0.05f) return;
            const float RadiusPx = RadiusWorld / (Z * Cam.TanHalfFovY) * 0.5f * (float)H;
            const int R = std::max(1, (int)std::lround(RadiusPx));
            const int Cx = (int)Sx, Cy = (int)Sy;
            for (int Dy = -R; Dy <= R; ++Dy)
                for (int Dx = -R; Dx <= R; ++Dx)
                {
                    if (Dx * Dx + Dy * Dy > R * R) continue;
                    const int Px = Cx + Dx, Py = Cy + Dy;
                    if (Px < 0 || Py < 0 || Px >= W || Py >= H) continue;
                    const size_t Index = (size_t)(H - 1 - Py) * W + Px;
                    if (Z > Depth[Index] + 0.05f) continue;
                    Depth[Index] = Z;
                    float* Out = &Colour[Index * 3];
                    Out[0] = ColourLinear.X; Out[1] = ColourLinear.Y; Out[2] = ColourLinear.Z;
                }
        }
        bool WritePpm(const std::string& Path) const
        {
            std::FILE* File = std::fopen(Path.c_str(), "wb");
            if (!File) return false;
            std::fprintf(File, "P6\n%d %d\n255\n", W, H);
            std::vector<uint8_t> Bytes((size_t)W * H * 3);
            for (size_t I = 0; I < Bytes.size(); ++I)
            {
                const float Exposed = std::fmax(0.0f, Colour[I]) * 1.5f;
                const float Tonemapped = Exposed / (1.0f + Exposed);
                Bytes[I] = (uint8_t)std::lround(255.0f * ClampF(std::pow(Tonemapped, 1.0f / 2.2f), 0.0f, 1.0f));
            }
            std::fwrite(Bytes.data(), 1, Bytes.size(), File);
            std::fclose(File);
            return true;
        }
    };

    //--------------------------------------------------------------------------------------------------------------------
    //                                                   SCENE ASSEMBLY
    //--------------------------------------------------------------------------------------------------------------------

    struct VehicleMeshes
    {
        FacetMesh Body, TyreLeft, TyreRight, RimLeft, RimRight;
    };

    bool LoadVehicle(const std::string& Dir, VehicleMeshes& Out)
    {
        FacetMesh Tyre, Rim;
        if (!LoadFacet(Dir + "/ControlVehicleBody.facet", Out.Body)) return false;
        if (!LoadFacet(Dir + "/ControlVehicleTyre.facet", Tyre)) return false;
        if (!LoadFacet(Dir + "/ControlVehicleRim.facet", Rim)) return false;
        const float RimScale = 0.34460f / 0.344818f;
        Out.TyreLeft = Tyre;
        Out.TyreRight = MirrorY(Tyre, 1.0f);
        Out.RimLeft = Scaled(Rim, RimScale);
        Out.RimLeft.Smooth = false;                       // the rim renders hard-shaded (FacetStructure.js)
        Out.RimRight = MirrorY(Rim, RimScale);
        Out.RimRight.Smooth = false;
        return true;
    }

    void DrawMesh(Renderer& R, const FacetMesh& Mesh, const std::vector<F3>& WorldVerts,
                  const std::vector<F3>& WorldNormals, bool Smooth, F3 AlbedoMul, bool ShadowOnly)
    {
        for (size_t F = 0; F < Mesh.Facets.size(); F += 3)
        {
            const uint32_t I0 = Mesh.Facets[F], I1 = Mesh.Facets[F + 1], I2 = Mesh.Facets[F + 2];
            if (ShadowOnly)
            {
                R.ShadowTriangle(WorldVerts[I0], WorldVerts[I1], WorldVerts[I2]);
                continue;
            }
            const Material Mat = ResolveMaterial(Mesh.Families[F / 3]);
            if (Smooth)
                R.Triangle(WorldVerts[I0], WorldVerts[I1], WorldVerts[I2], WorldNormals[I0], WorldNormals[I1], WorldNormals[I2], Mat, true, AlbedoMul);
            else
            {
                const F3 N = Norm(Cross(WorldVerts[I1] - WorldVerts[I0], WorldVerts[I2] - WorldVerts[I0]));
                R.Triangle(WorldVerts[I0], WorldVerts[I1], WorldVerts[I2], N, N, N, Mat, false, AlbedoMul);
            }
        }
    }

    struct WheelRenderState
    {
        std::vector<F3> TyreWorld, TyreNormalsWorld, RimWorld;
    };

    // Pose the solver's wheels and choose the active analytical or nodal visual deformation path.
    void PoseVehicle(const VehicleSolver& Car, const VehicleMeshes& Meshes,
                     std::vector<F3>& BodyWorld, WheelRenderState Wheels[4])
    {
        const float CoMZ = 0.11285f;
        BodyWorld.resize(Meshes.Body.Positions.size());
        for (size_t I = 0; I < BodyWorld.size(); ++I)
        {
            const F3 Local = { Meshes.Body.Positions[I].X, Meshes.Body.Positions[I].Y, Meshes.Body.Positions[I].Z - CoMZ };
            const F3 Rotated = RotateD(Car.Orientation, Local);
            BodyWorld[I] = { Rotated.X + (float)Car.Position[0], Rotated.Y + (float)Car.Position[1], Rotated.Z + (float)Car.Position[2] };
        }
        for (int Wheel = 0; Wheel < 4; ++Wheel)
        {
            const FacetMesh& Tyre = (Wheel & 1) ? Meshes.TyreRight : Meshes.TyreLeft;
            const FacetMesh& Rim = (Wheel & 1) ? Meshes.RimRight : Meshes.RimLeft;
            double Hub[3], Quat[4];
            Car.QueryWheelPose(Wheel, Hub, Quat);
            const TyreDeformationRecord Record = ResolveTyreDeformationRecord(Car.Wheels[Wheel], Car.Orientation);
            const TyreContinuumPositions* Rest = nullptr;
            const TyreContinuumPositions* Local = nullptr;
            if (Car.QueryCarcassModel() == CarcassProjectionModel)
            {
                Rest = &Car.QueryCarcassProjection(Wheel).QueryRestPositions();
                Local = &Car.QueryCarcassProjection(Wheel).QueryLocalPositions();
            }
            else if (Car.QueryCarcassModel() == CarcassSolidModel)
            {
                Rest = &Car.QueryCarcassSolid(Wheel).QueryRestPositions();
                Local = &Car.QueryCarcassSolid(Wheel).QueryLocalPositions();
            }
            WheelRenderState& State = Wheels[Wheel];
            State.TyreWorld.resize(Tyre.Positions.size());
            State.TyreNormalsWorld.resize(Tyre.Positions.size());
            for (size_t I = 0; I < Tyre.Positions.size(); ++I)
            {
                const float In[3] = { Tyre.Positions[I].X, Tyre.Positions[I].Y, Tyre.Positions[I].Z };
                const float NIn[3] = { Tyre.VertexNormals[I].X, Tyre.VertexNormals[I].Y, Tyre.VertexNormals[I].Z };
                float OutP[3], OutN[3];
                if (Rest && Local) ResolveContinuumWheelVertex(Record, true, *Rest, *Local, In, NIn, OutP, OutN);
                else ResolveDeformedWheelVertex(Record, true, In, NIn, OutP, OutN);
                const F3 P = RotateD(Quat, F3{ OutP[0], OutP[1], OutP[2] });
                State.TyreWorld[I] = { P.X + (float)Hub[0], P.Y + (float)Hub[1], P.Z + (float)Hub[2] };
                State.TyreNormalsWorld[I] = Norm(RotateD(Quat, F3{ OutN[0], OutN[1], OutN[2] }));
            }
            State.RimWorld.resize(Rim.Positions.size());
            for (size_t I = 0; I < Rim.Positions.size(); ++I)
            {
                const F3 P = RotateD(Quat, Rim.Positions[I]);
                State.RimWorld[I] = { P.X + (float)Hub[0], P.Y + (float)Hub[1], P.Z + (float)Hub[2] };
            }
        }
    }

    void RenderVehicle(Renderer& R, const VehicleMeshes& Meshes,
                       const std::vector<F3>& BodyWorld, WheelRenderState Wheels[4],
                       bool HideRubber, const float HeatGlow[4])
    {
        for (int Pass = 0; Pass < 2; ++Pass)
        {
            const bool ShadowOnly = Pass == 0;
            DrawMesh(R, Meshes.Body, BodyWorld, BodyWorld, false, { 1, 1, 1 }, ShadowOnly);
            for (int Wheel = 0; Wheel < 4; ++Wheel)
            {
                const FacetMesh& Tyre = (Wheel & 1) ? Meshes.TyreRight : Meshes.TyreLeft;
                const FacetMesh& Rim = (Wheel & 1) ? Meshes.RimRight : Meshes.RimLeft;
                if (!HideRubber)
                {
                    const float Heat = HeatGlow ? HeatGlow[Wheel] : 0.0f;
                    const F3 Mul = { 1.0f + Heat * 6.0f, 1.0f + Heat * 1.2f, 1.0f };
                    DrawMesh(R, Tyre, Wheels[Wheel].TyreWorld, Wheels[Wheel].TyreNormalsWorld, true, ShadowOnly ? F3{ 1, 1, 1 } : Mul, ShadowOnly);
                }
                DrawMesh(R, Rim, Wheels[Wheel].RimWorld, Wheels[Wheel].RimWorld, false, { 1, 1, 1 }, ShadowOnly);
            }
        }
    }

    void DrawBox(Renderer& R, F3 Centre, F3 Half, const double Quat[4], F3 Albedo)
    {
        const F3 Corners[8] = {
            { -Half.X, -Half.Y, -Half.Z }, { Half.X, -Half.Y, -Half.Z }, { Half.X, Half.Y, -Half.Z }, { -Half.X, Half.Y, -Half.Z },
            { -Half.X, -Half.Y, Half.Z }, { Half.X, -Half.Y, Half.Z }, { Half.X, Half.Y, Half.Z }, { -Half.X, Half.Y, Half.Z }
        };
        F3 Wc[8];
        for (int I = 0; I < 8; ++I) Wc[I] = RotateD(Quat, Corners[I]) + Centre;
        const int Faces[6][4] = { { 0, 1, 2, 3 }, { 5, 4, 7, 6 }, { 4, 0, 3, 7 }, { 1, 5, 6, 2 }, { 4, 5, 1, 0 }, { 3, 2, 6, 7 } };
        Material Mat = { { Albedo.X, Albedo.Y, Albedo.Z }, 0.8f, 0.0f, { 0, 0, 0 } };
        for (auto& Face : Faces)
        {
            const F3 N = Norm(Cross(Wc[Face[1]] - Wc[Face[0]], Wc[Face[2]] - Wc[Face[0]]));
            R.Triangle(Wc[Face[0]], Wc[Face[1]], Wc[Face[2]], N, N, N, Mat, false, { 1, 1, 1 });
            R.Triangle(Wc[Face[0]], Wc[Face[2]], Wc[Face[3]], N, N, N, Mat, false, { 1, 1, 1 });
            R.ShadowTriangle(Wc[Face[0]], Wc[Face[1]], Wc[Face[2]]);
            R.ShadowTriangle(Wc[Face[0]], Wc[Face[2]], Wc[Face[3]]);
        }
    }

    // Lathe-profile traffic cone (base plate + frustum + tip), axis +Z local, posed by a Jolt body transform.
    void DrawCone(Renderer& R, F3 Centre, const double Quat[4])
    {
        const auto Xf = [&](F3 P) { return RotateD(Quat, P) + Centre; };
        const int Segments = 12;
        const struct { float Z, R; F3 Colour; } Profile[5] = {
            { -0.35f, 0.150f, { 0.95f, 0.35f, 0.05f } }, { -0.28f, 0.115f, { 0.95f, 0.35f, 0.05f } },
            { -0.05f, 0.085f, { 0.85f, 0.85f, 0.85f } }, { 0.12f, 0.055f, { 0.95f, 0.35f, 0.05f } },
            { 0.35f, 0.018f, { 0.95f, 0.35f, 0.05f } }
        };
        for (int Band = 0; Band < 4; ++Band)
        {
            Material Mat = { { Profile[Band].Colour.X, Profile[Band].Colour.Y, Profile[Band].Colour.Z }, 0.5f, 0.0f, { 0, 0, 0 } };
            for (int S = 0; S < Segments; ++S)
            {
                const float A0 = (float)S / Segments * 2.0f * (float)Pi, A1 = (float)(S + 1) / Segments * 2.0f * (float)Pi;
                const F3 P00 = Xf({ Profile[Band].R * std::cos(A0), Profile[Band].R * std::sin(A0), Profile[Band].Z });
                const F3 P10 = Xf({ Profile[Band].R * std::cos(A1), Profile[Band].R * std::sin(A1), Profile[Band].Z });
                const F3 P01 = Xf({ Profile[Band + 1].R * std::cos(A0), Profile[Band + 1].R * std::sin(A0), Profile[Band + 1].Z });
                const F3 P11 = Xf({ Profile[Band + 1].R * std::cos(A1), Profile[Band + 1].R * std::sin(A1), Profile[Band + 1].Z });
                const F3 N = Norm(Cross(P10 - P00, P01 - P00));
                R.Triangle(P00, P10, P11, N, N, N, Mat, false, { 1, 1, 1 });
                R.Triangle(P00, P11, P01, N, N, N, Mat, false, { 1, 1, 1 });
                R.ShadowTriangle(P00, P10, P11);
                R.ShadowTriangle(P00, P11, P01);
            }
        }
        const double Identity[4] = { 0, 0, 0, 1 };
        (void)Identity;
    }

    double HeadingOf(const VehicleSolver& Car)
    {
        const double* Q = Car.Orientation;
        return std::atan2(2.0 * (Q[0] * Q[1] + Q[2] * Q[3]), 1.0 - 2.0 * (Q[1] * Q[1] + Q[2] * Q[2]));
    }
    F3 CarForward(const VehicleSolver& Car)
    {
        return RotateD(Car.Orientation, F3{ 1, 0, 0 });
    }
}

int main(int Argc, char** Argv)
{
    if (Argc < 4)
    {
        std::printf("usage: SceneRender <hero|tyre-stills|gif-drive|gif-burnout|gif-jolt> <facetdir> <outdir>\n");
        return 2;
    }
    const std::string Mode = Argv[1], FacetDir = Argv[2], outdir = Argv[3];
    VehicleMeshes Meshes;
    if (!LoadVehicle(FacetDir, Meshes))
        return 1;
    const auto FramePath = [&](const std::string& Name) { return outdir + "/" + Name; };
    std::vector<F3> BodyWorld;
    WheelRenderState Wheels[4];
    const float NoHeat[4] = { 0, 0, 0, 0 };

    if (Mode == "hero" || Mode == "tyre-stills")
    {
        Renderer R;
        R.Init(1600, 900);
        VehicleSolver Car;
        const double Spawn[3] = { 0.0, 0.0, 0.55 };
        Car.Respawn(Spawn, -0.55);
        VehicleCommand Idle;
        for (int S = 0; S < 300; ++S) Car.Advance(Idle, 1.0 / 120.0);
        PoseVehicle(Car, Meshes, BodyWorld, Wheels);
        if (Mode == "hero")
        {
            R.Cam = LookAt(F3{ 5.6f, -5.4f, 1.55f }, F3{ 0.1f, 0.0f, 0.42f }, 42.0f, 1600.0f / 900.0f);
            R.Background(); R.GroundPlane();
            RenderVehicle(R, Meshes, BodyWorld, Wheels, false, NoHeat);
            R.WritePpm(FramePath("hero.ppm"));
            R.Cam = LookAt(F3{ -5.9f, -4.8f, 1.45f }, F3{ -0.2f, 0.0f, 0.42f }, 42.0f, 1600.0f / 900.0f);
            R.Background(); R.GroundPlane();
            RenderVehicle(R, Meshes, BodyWorld, Wheels, false, NoHeat);
            R.WritePpm(FramePath("hero-rear.ppm"));
            std::printf("hero stills written\n");
            return 0;
        }
        // Tyre stills: camera on the front-left corner. (a) nominal, (b) flat (thermal deflated to 30 kPa),
        // (c) XPBD lattice mode with the rubber hidden: rim + the actual lattice particles under load.
        double Hub[3], Quat[4];
        const auto RenderCorner = [&](const char* Name, bool HideRubber, bool Particles)
        {
            PoseVehicle(Car, Meshes, BodyWorld, Wheels);
            Car.QueryWheelPose(0, Hub, Quat);
            const F3 HubF = { (float)Hub[0], (float)Hub[1], (float)Hub[2] };
            const F3 Side = RotateD(Car.Orientation, F3{ 0.22f, 1.0f, 0.14f });
            R.Cam = LookAt(HubF + Side * 2.35f + F3{ 0, 0, 0.30f }, HubF + F3{ 0, 0, -0.08f }, 44.0f, 1600.0f / 900.0f);
            R.Background(); R.GroundPlane();
            RenderVehicle(R, Meshes, BodyWorld, Wheels, HideRubber, NoHeat);
            if (Particles)
            {
                // Lattice particles live in the steered (unspun) wheel frame.
                double SteerQ[4], Steered[4];
                AxisQuat(0, 0, 1, Car.Wheels[0].Steer, SteerQ);
                MulQuat(Car.Orientation, SteerQ, Steered);
                const TyreCarcassLattice& Lattice = Car.QueryCarcassLattice(1);
                for (int Index = 0; Index < TyreCarcassLattice::ParticleCount; ++Index)
                {
                    double P[3];
                    Lattice.QueryParticle(Index, P);
                    const F3 W = RotateD(Steered, F3{ (float)P[0], (float)P[1], (float)P[2] }) + HubF;
                    const bool Crown = std::fabs(P[1]) < 0.01;
                    R.Disc(W, 0.020f, Crown ? F3{ 0.15f, 0.65f, 1.4f } : F3{ 1.3f, 0.85f, 0.15f });
                }
            }
            R.WritePpm(FramePath(Name));
            std::printf("%s written (defl %.1f mm, P %.0f Pa)\n", Name, Car.Wheels[0].TyreDeflection * 1000.0, Car.Wheels[0].Pressure);
        };
        RenderCorner("tyre-nominal.ppm", false, false);
        const double FlatPressures[4] = { 30000.0, 30000.0, 30000.0, 30000.0 };
        Car.Thermal.Inflate(FlatPressures);
        for (int S = 0; S < 360; ++S) Car.Advance(Idle, 1.0 / 120.0);
        RenderCorner("tyre-flat.ppm", false, false);
        const double Nominal[4] = { 220000.0, 220000.0, 220000.0, 220000.0 };
        Car.Thermal.Inflate(Nominal);
        Car.RestoreTyres();
        Car.AssignCarcassModel(CarcassLatticeModel);
        Car.Respawn(Spawn, -0.55);
        for (int S = 0; S < 420; ++S) Car.Advance(Idle, 1.0 / 120.0);
        RenderCorner("tyre-lattice.ppm", true, true);
        // Demo projection and solid shapes both use the same spring loads. Show their nodal interpolation
        // at 30 kPa so the deformation is visible; the older lattice above is a separate force model.
        Car.Thermal.Inflate(FlatPressures);
        Car.RestoreTyres();
        Car.AssignCarcassModel(CarcassProjectionModel);
        Car.Respawn(Spawn, -0.55);
        for (int S = 0; S < 360; ++S) Car.Advance(Idle, 1.0 / 120.0);
        RenderCorner("tyre-demo-xpbd.ppm", false, false);
        Car.AssignCarcassModel(CarcassSolidModel);
        Car.Respawn(Spawn, -0.55);
        for (int S = 0; S < 360; ++S) Car.Advance(Idle, 1.0 / 120.0);
        RenderCorner("tyre-demo-solid.ppm", false, false);
        return 0;
    }

    if (Mode == "gif-drive" || Mode == "gif-burnout")
    {
        Renderer R;
        R.Init(1280, 720);
        VehicleSolver Car;
        const double Spawn[3] = { 0.0, 0.0, 0.55 };
        Car.Respawn(Spawn, 0.0);
        VehicleCommand Idle;
        for (int S = 0; S < 150; ++S) Car.Advance(Idle, 1.0 / 120.0);
        int Frame = 0;
        const auto Capture = [&](F3 CamPos, F3 Target, const float Heat[4])
        {
            PoseVehicle(Car, Meshes, BodyWorld, Wheels);
            R.Cam = LookAt(CamPos, Target, 50.0f, 1280.0f / 720.0f);
            R.Background(); R.GroundPlane();
            RenderVehicle(R, Meshes, BodyWorld, Wheels, false, Heat);
            char Name[64];
            std::snprintf(Name, sizeof Name, "frame-%04d.ppm", Frame++);
            R.WritePpm(FramePath(Name));
        };
        const auto Speed = [&]() { return std::sqrt(Car.LinearVelocity[0] * Car.LinearVelocity[0] + Car.LinearVelocity[1] * Car.LinearVelocity[1]); };
        if (Mode == "gif-drive")
        {
            F3 CamPos = { -8.0f, -7.0f, 3.0f };
            double SimTime = 0.0;
            for (int Step = 0; Step < 120 * 9; ++Step)
            {
                VehicleCommand Command;
                if (SimTime < 3.2)
                    Command.Throttle = 1.0;
                else if (SimTime < 7.4)
                {
                    Command.Throttle = Speed() < 15.0 ? 0.4 : 0.0;
                    Command.Brake = Speed() > 17.0 ? 0.35 : 0.0;
                    Command.Steer = std::fmax(-1.0, std::fmin(1.0,
                        0.35 * std::sin(2.0 * Pi * 0.35 * (SimTime - 3.2)) - 0.9 * HeadingOf(Car) - 0.015 * Car.Position[1]));
                }
                else
                    Command.Brake = 0.9;
                Car.Advance(Command, 1.0 / 120.0);
                SimTime += 1.0 / 120.0;
                if (Step % 6 == 0)
                {
                    const F3 Fwd = CarForward(Car);
                    const F3 CarPos = { (float)Car.Position[0], (float)Car.Position[1], (float)Car.Position[2] };
                    const F3 Desired = CarPos - Fwd * 8.5f + F3{ 0, 0, 2.7f } + RotateD(Car.Orientation, F3{ 0, -2.6f, 0 });
                    CamPos = Desired;   // rigid chase: a smoothed camera lags unboundedly at speed
                    if ((Step % 180) == 0) std::printf("t=%.1f pos=(%.1f, %.1f, %.2f) speed=%.1f fwdz=%.2f\n", SimTime, Car.Position[0], Car.Position[1], Car.Position[2], Speed(), (double)Fwd.Z);
                    Capture(CamPos, CarPos + Fwd * 3.0f + F3{ 0, 0, 0.3f }, NoHeat);
                }
            }
        }
        else
        {
            // Burnout close-up: camera tracks the rear-left corner; tread glows with thermal surface temperature.
            for (int Step = 0; Step < 120 * 6; ++Step)
            {
                const double T = Step / 120.0;
                VehicleCommand Command;
                if (T > 0.4 && T < 3.4) { Command.Throttle = 1.0; Command.Brake = 1.0; }
                else if (T >= 3.4) Command.Throttle = 1.0;
                Car.Advance(Command, 1.0 / 120.0);
                if (Step % 6 == 0)
                {
                    float Heat[4] = { 0, 0, 0, 0 };
                    for (int Wd = 0; Wd < 4; ++Wd)
                        Heat[Wd] = (float)std::fmax(0.0, std::fmin(1.0, (Car.Thermal.Wheels[Wd].Surface - 320.0) / 90.0));
                    double Hub[3], Quat[4];
                    Car.QueryWheelPose(2, Hub, Quat);
                    const F3 HubF = { (float)Hub[0], (float)Hub[1], (float)Hub[2] };
                    const F3 Offset = RotateD(Car.Orientation, F3{ -1.5f, 2.1f, 0.55f });
                    Capture(HubF + Offset, HubF + F3{ 0, 0, -0.10f }, Heat);
                }
            }
        }
        std::printf("%s: %d frames\n", Mode.c_str(), Frame);
        return 0;
    }

    if (Mode == "gif-jolt")
    {
        // The JoltScene.cpp exchange loop with per-frame cone poses for the render.
        Renderer R;
        R.Init(1280, 720);
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
        JPH::TempAllocatorImpl TempAllocator(32 * 1024 * 1024);
        JPH::JobSystemThreadPool JobSystem(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, 1);
        class BPL final : public JPH::BroadPhaseLayerInterface
        {
        public:
            JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
            JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer L) const override { return JPH::BroadPhaseLayer(L); }
        };
        class OVB final : public JPH::ObjectVsBroadPhaseLayerFilter
        {
        public:
            bool ShouldCollide(JPH::ObjectLayer L1, JPH::BroadPhaseLayer L2) const override { return L1 != 0 || L2 == JPH::BroadPhaseLayer(1); }
        };
        class OLP final : public JPH::ObjectLayerPairFilter
        {
        public:
            bool ShouldCollide(JPH::ObjectLayer L1, JPH::ObjectLayer L2) const override { return L1 != 0 || L2 == 1; }
        };
        BPL BPLayers; OVB ObjectVsBP; OLP ObjectPairs;
        JPH::PhysicsSystem Physics;
        Physics.Init(1024, 0, 1024, 1024, BPLayers, ObjectVsBP, ObjectPairs);
        Physics.SetGravity(JPH::Vec3(0.0f, 0.0f, -9.81f));
        JPH::BodyInterface& Bodies = Physics.GetBodyInterface();
        {
            JPH::BodyCreationSettings Ground(new JPH::BoxShape(JPH::Vec3(200, 200, 0.5f)), JPH::RVec3(0, 0, -0.5),
                                             JPH::Quat::sIdentity(), JPH::EMotionType::Static, 0);
            Bodies.CreateAndAddBody(Ground, JPH::EActivation::DontActivate);
            JPH::BodyCreationSettings Wall(new JPH::BoxShape(JPH::Vec3(0.5f, 8, 2)), JPH::RVec3(60, 0, 2),
                                           JPH::Quat::sIdentity(), JPH::EMotionType::Static, 0);
            Bodies.CreateAndAddBody(Wall, JPH::EActivation::DontActivate);
        }
        std::vector<JPH::BodyID> Cones;
        {
            JPH::RefConst<JPH::Shape> ConeShape = new JPH::CylinderShape(0.35f, 0.16f);
            const JPH::Quat Upright = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), 0.5f * (float)Pi);
            for (int Index = 0; Index < 10; ++Index)
            {
                JPH::BodyCreationSettings Settings(ConeShape, JPH::RVec3(14.0 + 4.0 * Index, (Index % 2 == 0) ? 0.85 : -0.85, 0.35),
                                                   Upright, JPH::EMotionType::Dynamic, 1);
                Settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
                Settings.mMassPropertiesOverride.mMass = 3.2f;
                Settings.mFriction = 0.6f;
                Settings.mRestitution = 0.25f;
                Cones.push_back(Bodies.CreateAndAddBody(Settings, JPH::EActivation::Activate));
            }
        }
        JPH::BodyID CarProxy;
        {
            JPH::Array<JPH::Vec3> Points;
            const float Hull[8][3] = { { -3, -1, -0.1f }, { -3, 1, -0.1f }, { 3, -1, -0.1f }, { 3, 1, -0.1f },
                                       { -2, -1, 1 }, { -2, 1, 1 }, { 2, -1, 1 }, { 2, 1, 1 } };
            for (auto& P : Hull) Points.push_back(JPH::Vec3(P[0], P[1], P[2]));
            JPH::ConvexHullShapeSettings HullSettings(Points);
            JPH::BodyCreationSettings Settings(HullSettings.Create().Get(), JPH::RVec3(0, 0, 0.55), JPH::Quat::sIdentity(),
                                               JPH::EMotionType::Dynamic, 1);
            Settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            Settings.mMassPropertiesOverride.mMass = 1300.0f;
            Settings.mGravityFactor = 0.0f;
            Settings.mLinearDamping = 0.0f;
            Settings.mAngularDamping = 0.0f;
            Settings.mFriction = 0.4f;
            Settings.mRestitution = 0.1f;
            CarProxy = Bodies.CreateAndAddBody(Settings, JPH::EActivation::Activate);
        }
        const auto JV = [](const double V[3]) { return JPH::Vec3((float)V[0], (float)V[1], (float)V[2]); };
        const auto JQ = [](const double Q[4]) { return JPH::Quat((float)Q[0], (float)Q[1], (float)Q[2], (float)Q[3]); };
        VehicleSolver Car;
        const double Spawn[3] = { 0.0, 0.0, 0.55 };
        Car.Respawn(Spawn, 0.0);
        const double Dt = 1.0 / 120.0;
        const auto Tick = [&](const VehicleCommand& Command)
        {
            Car.Advance(Command, Dt);
            Bodies.SetPositionAndRotation(CarProxy, JV(Car.Position), JQ(Car.Orientation), JPH::EActivation::Activate);
            Bodies.SetLinearAndAngularVelocity(CarProxy, JV(Car.LinearVelocity), JV(Car.AngularVelocity));
            const double Before[3] = { Car.LinearVelocity[0], Car.LinearVelocity[1], Car.LinearVelocity[2] };
            const double BeforeA[3] = { Car.AngularVelocity[0], Car.AngularVelocity[1], Car.AngularVelocity[2] };
            Physics.Update((float)Dt, 1, &TempAllocator, &JobSystem);
            const JPH::Vec3 After = Bodies.GetLinearVelocity(CarProxy);
            const JPH::Vec3 AfterA = Bodies.GetAngularVelocity(CarProxy);
            Car.LinearVelocity[0] += After.GetX() - Before[0];
            Car.LinearVelocity[1] += After.GetY() - Before[1];
            Car.LinearVelocity[2] += After.GetZ() - Before[2];
            Car.AngularVelocity[0] += AfterA.GetX() - BeforeA[0];
            Car.AngularVelocity[1] += AfterA.GetY() - BeforeA[1];
            Car.AngularVelocity[2] += AfterA.GetZ() - BeforeA[2];
        };
        VehicleCommand Idle;
        for (int S = 0; S < 120; ++S) Tick(Idle);
        int Frame = 0;
        F3 CamPos = { -6.0f, -11.0f, 5.0f };
        for (int Step = 0; Step < 120 * 8; ++Step)
        {
            const double Speed = std::sqrt(Car.LinearVelocity[0] * Car.LinearVelocity[0] + Car.LinearVelocity[1] * Car.LinearVelocity[1]);
            VehicleCommand Command;
            Command.Throttle = Speed < 12.0 ? 0.85 : 0.25;
            const double TargetY = 0.45 * std::sin(2.0 * Pi * Car.Position[0] / 16.0);
            Command.Steer = std::fmax(-1.0, std::fmin(1.0, -1.1 * HeadingOf(Car) + 0.30 * (TargetY - Car.Position[1])));
            if (Car.Position[0] > 54.0)
                Command.Brake = 1.0, Command.Throttle = 0.0;
            Tick(Command);
            if (Step % 6 == 0)
            {
                PoseVehicle(Car, Meshes, BodyWorld, Wheels);
                const F3 CarPos = { (float)Car.Position[0], (float)Car.Position[1], (float)Car.Position[2] };
                const F3 Desired = CarPos + F3{ -7.0f, -11.0f, 4.6f };
                CamPos = CamPos + (Desired - CamPos) * 0.08f;
                R.Cam = LookAt(CamPos, CarPos + F3{ 3.0f, 0, 0.2f }, 50.0f, 1280.0f / 720.0f);
                R.Background(); R.GroundPlane();
                const double Identity[4] = { 0, 0, 0, 1 };
                DrawBox(R, F3{ 60.0f, 0, 2.0f }, F3{ 0.5f, 8.0f, 2.0f }, Identity, F3{ 0.45f, 0.46f, 0.48f });
                RenderVehicle(R, Meshes, BodyWorld, Wheels, false, NoHeat);
                for (const JPH::BodyID& Cone : Cones)
                {
                    const JPH::RVec3 P = Bodies.GetPosition(Cone);
                    const JPH::Quat Q = Bodies.GetRotation(Cone);
                    const double Quat[4] = { Q.GetX(), Q.GetY(), Q.GetZ(), Q.GetW() };
                    DrawCone(R, F3{ (float)P.GetX(), (float)P.GetY(), (float)P.GetZ() }, Quat);
                }
                char Name[64];
                std::snprintf(Name, sizeof Name, "frame-%04d.ppm", Frame++);
                R.WritePpm(FramePath(Name));
            }
            if (Car.Position[0] > 56.0 && Speed < 0.3)
                break;
        }
        std::printf("gif-jolt: %d frames\n", Frame);
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
        return 0;
    }

    std::printf("unknown mode %s\n", Mode.c_str());
    return 2;
}
