/*==========================================================================================================================================
                                                     WORKSPACEVIEWPORTRENDERING.H
==========================================================================================================================================*/
// 🧩 Unified 3D viewport camera/scattering staging and perspective-correct PBR/Flakes/Clearcoat/IBL CPU twin shared by Material,
// Texture Paint, and Baking workspaces.

#pragma once
#include "AutomotivePaint.h"
#include "MaterialPreviewConfiguration.h"
#include "MaterialPreviewGeometry.h"
#include "SurfaceMaterial.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <vector>

namespace Frontier
{
struct WorkspaceViewportImage
{
    uint64_t Texture = 0;
    float MinimumU = 0.f;
    float MinimumV = 0.f;
    float MaximumU = 1.f;
    float MaximumV = 1.f;
};

struct WorkspaceViewportOrbitCamera
{
    std::array<float, 3> Pivot{0.f, 0.f, 0.f};
    float Radius = .85f;
    float FrameRatio = 5.8f / 1.35f;
    float Yaw = .48f;
    float Pitch = .16f;
    float Zoom = 1.f;
    float PanX = 0.f;
    float PanY = 0.f;
    float VerticalFieldDegrees = 34.f;
    float Exposure = 1.f;
    float LightIntensity = 1.f;
    uint32_t Environment = 0;

    bool operator==(const WorkspaceViewportOrbitCamera &) const = default;
};

struct WorkspaceViewportCpuFrame
{
    uint32_t Width = 0;
    uint32_t Height = 0;
    std::vector<uint8_t> Rgba;
};

struct WorkspaceSurfaceViewportRequest
{
    const SurfaceGeometry *Geometry = nullptr;
    uint32_t FirstIndex = 0;
    uint32_t IndexCount = 0;
    const CompiledSurfaceMaterial *Material = nullptr;
    MaterialPreviewProjection Camera{};
    std::array<float, 3> LightDirection{.3906f, .7330f, .5569f};
    float LightIntensity = 2.25f;
    float EnvironmentStrength = 1.f;
    std::array<float, 3> EnvironmentUp{0.f, 1.f, 0.f};
    float Exposure = 1.f;
    std::array<float, 3> SurfaceTint{1.f, 1.f, 1.f};
    uint32_t PreviewShapeOrdinal = 0; // 0 = arbitrary mesh, 1..5 = MaterialPreviewShape + 1
    const MaterialTexturePyramid *EnvironmentEquirectPyramid = nullptr;
};

namespace WorkspaceViewportDetail
{
inline std::array<float, 3> Normalize3(std::array<float, 3> Vector)
{
    const float MaxComp = std::max({std::abs(Vector[0]), std::abs(Vector[1]), std::abs(Vector[2])});
    if (MaxComp <= 1e-20f) return {0.f, 0.f, 1.f};
    Vector[0] /= MaxComp;
    Vector[1] /= MaxComp;
    Vector[2] /= MaxComp;
    const float InvLen = 1.f / std::sqrt(Vector[0] * Vector[0] + Vector[1] * Vector[1] + Vector[2] * Vector[2]);
    return {Vector[0] * InvLen, Vector[1] * InvLen, Vector[2] * InvLen};
}

inline float Dot3(const std::array<float, 3> &Left, const std::array<float, 3> &Right)
{
    return Left[0] * Right[0] + Left[1] * Right[1] + Left[2] * Right[2];
}

inline std::array<float, 3> Cross3(const std::array<float, 3> &Left, const std::array<float, 3> &Right)
{
    return {
        Left[1] * Right[2] - Left[2] * Right[1],
        Left[2] * Right[0] - Left[0] * Right[2],
        Left[0] * Right[1] - Left[1] * Right[0]};
}

inline float Saturate(float Value)
{
    return std::clamp(Value, 0.f, 1.f);
}

inline float Smoothstep(float Edge0, float Edge1, float Value)
{
    if ( std::abs(Edge1 - Edge0) <= 1e-7f) return Value >= Edge1 ? 1.f : 0.f;
    const float T = Saturate((Value - Edge0) / (Edge1 - Edge0));
    return T * T * (3.f - 2.f * T);
}

inline float DecodeSrgbByte(uint8_t Byte)
{
    const float Encoded = float(Byte) * (1.f / 255.f);
    return Encoded <= .04045f ? Encoded / 12.92f : std::pow((Encoded + .055f) / 1.055f, 2.4f);
}

// Direct port of AutomotivePaint.slang HashPaintCell / EvaluatePaintNoise / EvaluatePaintCell / SelectPaintTint / EvaluatePaintSurface.
inline float HashPaintCellCpu(const std::array<float, 3> &Position)
{
    const auto Frac = [](float Value) { return Value - std::floor(Value); };
    std::array<float, 3> Pos{Frac(Position[0] * .1031f), Frac(Position[1] * .1031f), Frac(Position[2] * .1031f)};
    const float DotTerm = Pos[0] * (Pos[1] + 33.33f) + Pos[1] * (Pos[2] + 33.33f) + Pos[2] * (Pos[0] + 33.33f);
    Pos[0] += DotTerm; Pos[1] += DotTerm; Pos[2] += DotTerm;
    return Frac((Pos[0] + Pos[1]) * Pos[2]);
}

inline float EvaluatePaintNoiseCpu(const std::array<float, 3> &Position)
{
    const float Cx = std::floor(Position[0]), Cy = std::floor(Position[1]), Cz = std::floor(Position[2]);
    float Bx = Position[0] - Cx, By = Position[1] - Cy, Bz = Position[2] - Cz;
    Bx = Bx * Bx * (3.f - 2.f * Bx);
    By = By * By * (3.f - 2.f * By);
    Bz = Bz * Bz * (3.f - 2.f * Bz);
    const float N000 = HashPaintCellCpu({Cx, Cy, Cz});
    const float N100 = HashPaintCellCpu({Cx + 1.f, Cy, Cz});
    const float N010 = HashPaintCellCpu({Cx, Cy + 1.f, Cz});
    const float N110 = HashPaintCellCpu({Cx + 1.f, Cy + 1.f, Cz});
    const float N001 = HashPaintCellCpu({Cx, Cy, Cz + 1.f});
    const float N101 = HashPaintCellCpu({Cx + 1.f, Cy, Cz + 1.f});
    const float N011 = HashPaintCellCpu({Cx, Cy + 1.f, Cz + 1.f});
    const float N111 = HashPaintCellCpu({Cx + 1.f, Cy + 1.f, Cz + 1.f});
    return std::lerp(
        std::lerp(std::lerp(N000, N100, Bx), std::lerp(N010, N110, Bx), By),
        std::lerp(std::lerp(N001, N101, Bx), std::lerp(N011, N111, Bx), By), Bz);
}

inline std::array<float, 4> EvaluatePaintCellCpu(const std::array<float, 3> &Position, float Seed)
{
    const float CellX = std::floor(Position[0]), CellY = std::floor(Position[1]), CellZ = std::floor(Position[2]);
    const float LocalX = Position[0] - CellX, LocalY = Position[1] - CellY, LocalZ = Position[2] - CellZ;
    std::array<float, 3> Nearest{0.f, 0.f, 0.f};
    float First = 1e8f, Second = 1e8f;
    for (int Dz = -1; Dz <= 1; ++Dz)
        for (int Dy = -1; Dy <= 1; ++Dy)
            for (int Dx = -1; Dx <= 1; ++Dx)
            {
                const float Ox = float(Dx), Oy = float(Dy), Oz = float(Dz);
                const std::array<float, 3> Id{CellX + Ox, CellY + Oy, CellZ + Oz};
                const float Sx = .08f + .84f * HashPaintCellCpu({Id[0] + Seed, Id[1] + Seed, Id[2] + Seed});
                const float Sy = .08f + .84f * HashPaintCellCpu({Id[0] + Seed + 17.31f, Id[1] + Seed + 17.31f, Id[2] + Seed + 17.31f});
                const float Sz = .08f + .84f * HashPaintCellCpu({Id[0] + Seed + 43.97f, Id[1] + Seed + 43.97f, Id[2] + Seed + 43.97f});
                const float DelX = Ox + Sx - LocalX, DelY = Oy + Sy - LocalY, DelZ = Oz + Sz - LocalZ;
                const float Dist = DelX * DelX + DelY * DelY + DelZ * DelZ;
                if (Dist < First) { Second = First; First = Dist; Nearest = Id; }
                else if (Dist < Second) Second = Dist;
            }
    return {
        std::max(0.f, std::sqrt(Second) - std::sqrt(First)),
        HashPaintCellCpu({Nearest[0] + Seed + 61.7f, Nearest[1] + Seed + 61.7f, Nearest[2] + Seed + 61.7f}),
        HashPaintCellCpu({Nearest[0] + Seed + 127.3f, Nearest[1] + Seed + 127.3f, Nearest[2] + Seed + 127.3f}),
        HashPaintCellCpu({Nearest[0] + Seed + 223.1f, Nearest[1] + Seed + 223.1f, Nearest[2] + Seed + 223.1f})};
}

inline std::array<float, 3> SelectPaintTintCpu(const AutomotivePaint &Paint, float Position)
{
    Position = Saturate(Position);
    const uint32_t Count = std::max(1u, std::min(12u, Paint.Configuration[3]));
    if (Paint.Configuration[2] == 2) return {Paint.Palette[0][0], Paint.Palette[0][1], Paint.Palette[0][2]};
    if (Paint.Configuration[2] == 0)
    {
        const uint32_t Slot = std::min(Count - 1, uint32_t(Position * float(Count)));
        return {Paint.Palette[Slot][0], Paint.Palette[Slot][1], Paint.Palette[Slot][2]};
    }
    std::array<float, 3> Spectrum{Paint.Palette[0][0], Paint.Palette[0][1], Paint.Palette[0][2]};
    for (uint32_t Index = 1; Index < Count; ++Index)
    {
        const float Denom = std::max(.0001f, Paint.Palette[Index][3] - Paint.Palette[Index - 1][3]);
        const float Blend = Saturate((Position - Paint.Palette[Index - 1][3]) / Denom);
        for (size_t Comp = 0; Comp < 3; ++Comp)
            Spectrum[Comp] = std::lerp(Spectrum[Comp], Paint.Palette[Index][Comp], Blend);
    }
    return Spectrum;
}

struct PaintSurfaceCpu
{
    std::array<float, 3> Albedo{0.f, 0.f, 0.f};
    float Roughness = .5f;
    float Metallic = 0.f;
    std::array<float, 2> Slope{0.f, 0.f};
    float Coverage = 0.f;
};

// Direct CPU twin of EvaluatePaintSurface in Engine/Render/Shader/AutomotivePaint.slang.
inline PaintSurfaceCpu EvaluatePaintSurfaceCpu(const AutomotivePaint &Paint,
    const std::array<float, 3> &Position,
    const std::array<float, 3> &BaseAlbedo, float BaseRoughness, float BaseMetallic,
    float PixelFootprintCells)
{
    PaintSurfaceCpu Output{BaseAlbedo, BaseRoughness, BaseMetallic, {0.f, 0.f}, 0.f};
    if (Paint.Configuration[0] == 0 || Paint.Distribution[0] <= 0.f) return Output;

    const float BinderAtten = std::exp(-Paint.Distribution[3] * .28f);
    const std::array<float, 3> Base{
        BaseAlbedo[0] * BinderAtten, BaseAlbedo[1] * BinderAtten, BaseAlbedo[2] * BinderAtten};
    std::array<float, 3> Blended = Base;
    float Mask = 0.f, Random = 0.f;
    const float Frequency = std::min(1e7f, 52000.f / std::max(.001f, Paint.Distribution[1]) * Paint.Distribution[2]);
    const std::array<float, 3> Domain{
        Position[0] * Frequency, Position[1] * Frequency, Position[2] * Frequency};
    const float Detail = 1.f - Smoothstep(.55f, 1.6f, PixelFootprintCells);
    const std::array<float, 3> Warp{
        EvaluatePaintNoiseCpu({Domain[0] * .31f, Domain[1] * .31f, Domain[2] * .31f}) - .5f,
        EvaluatePaintNoiseCpu({Domain[0] * .31f + 11.7f, Domain[1] * .31f + 11.7f, Domain[2] * .31f + 11.7f}) - .5f,
        EvaluatePaintNoiseCpu({Domain[0] * .31f + 37.4f, Domain[1] * .31f + 37.4f, Domain[2] * .31f + 37.4f}) - .5f};

    const uint32_t Layers = std::clamp(Paint.Configuration[1], 1u, 4u);
    for (uint32_t Layer = 0; Layer < Layers; ++Layer)
    {
        const float Level = float(Layer);
        const float Depth = float(Layers - 1 - Layer) / std::max(float(Layers - 1), 1.f);
        const float ScaleL = .71f + Level * .37f;
        const std::array<float, 3> PreRot{
            Domain[0] * ScaleL + Warp[0] * .7f + Level * 27.17f,
            Domain[1] * ScaleL + Warp[1] * .7f + Level * 51.73f,
            Domain[2] * ScaleL + Warp[2] * .7f + Level * 19.43f};
        const std::array<float, 3> Point{
            .80f * PreRot[0] - .60f * PreRot[1],
            .36f * PreRot[0] + .48f * PreRot[1] + .80f * PreRot[2],
            -.48f * PreRot[0] - .64f * PreRot[1] + .60f * PreRot[2]};
        const auto Cell = EvaluatePaintCellCpu(Point, Level * 97.3f + 4.1f);
        const float Antialias = std::max(PixelFootprintCells * .025f, .009f);
        const float Inset = .018f + Cell[3] * .07f;
        const float Opacity = Smoothstep(Inset - Antialias, Inset + Antialias, Cell[0]) *
            (Cell[1] >= 1.f - Paint.Distribution[0] * .55f ? 1.f : 0.f);
        const auto RawTint = SelectPaintTintCpu(Paint, Cell[2]);
        const float BurialBlend = Depth * Paint.Relief[1] * .72f;
        for (size_t Comp = 0; Comp < 3; ++Comp)
            Blended[Comp] = std::lerp(Blended[Comp], std::lerp(RawTint[Comp], Base[Comp], BurialBlend), Opacity);
        Random = std::lerp(Random, Cell[3], Opacity);
        Mask = Opacity + Mask * (1.f - Opacity);
    }
    const float MeanMask = 1.f - std::pow(1.f - Paint.Distribution[0] * .31f, float(Layers));
    for (size_t Comp = 0; Comp < 3; ++Comp)
    {
        const float FarFlake = std::lerp(Paint.AverageTint[Comp], Base[Comp], Paint.Relief[1] * .26f);
        Output.Albedo[Comp] = std::lerp(std::lerp(Base[Comp], FarFlake, MeanMask), Blended[Comp], Detail);
    }
    Output.Coverage = std::lerp(MeanMask, Mask, Detail);
    const float ResolvedRandom = std::lerp(.5f, Random, Detail);
    Output.Roughness = std::clamp(std::lerp(BaseRoughness,
        std::lerp(Paint.Finish[0], Paint.Finish[1], ResolvedRandom), Output.Coverage), .045f, 1.f);
    Output.Metallic = std::lerp(BaseMetallic,
        std::lerp(Paint.Finish[2], Paint.Finish[3], ResolvedRandom), Output.Coverage);
    const float Angle = ResolvedRandom * 6.2831853f;
    const float Tilt = Paint.Relief[0] * Output.Coverage * Detail;
    Output.Slope = {std::cos(Angle) * Tilt, std::sin(Angle) * Tilt};
    return Output;
}

inline std::array<float, 4> SamplePyramidBilinear(const MaterialTexturePyramid &Pyramid, float U, float V, uint32_t LayerSlice)
{
    if (Pyramid.Levels.empty()) return {1.f, 1.f, 1.f, 1.f};
    const auto &Base = Pyramid.Levels[0];
    if (Base.Width == 0 || Base.Height == 0) return {1.f, 1.f, 1.f, 1.f};
    U = U - std::floor(U);
    V = V - std::floor(V);
    const uint32_t Layers = std::max(1u, Pyramid.Layers);
    const uint32_t Slice = std::min(LayerSlice, Layers - 1);
    const float Fx = U * float(Base.Width) - .5f;
    const float Fy = V * float(Base.Height) - .5f;
    const int X0 = (int(std::floor(Fx)) % int(Base.Width) + int(Base.Width)) % int(Base.Width);
    const int Y0 = (int(std::floor(Fy)) % int(Base.Height) + int(Base.Height)) % int(Base.Height);
    const int X1 = (X0 + 1) % int(Base.Width);
    const int Y1 = (Y0 + 1) % int(Base.Height);
    const float Tx = Fx - std::floor(Fx);
    const float Ty = Fy - std::floor(Fy);

    const auto Fetch = [&](int X, int Y) -> std::array<float, 4>
    {
        const size_t SlicePixels = size_t(Base.Width) * Base.Height;
        const size_t PixelIndex = (size_t(Slice) * SlicePixels + size_t(Y) * Base.Width + size_t(X)) * 4;
        if (!Pyramid.FloatRgba.empty())
        {
            if (PixelIndex + 3 >= Pyramid.FloatRgba.size()) return {1.f, 1.f, 1.f, 0.f};
            return {Pyramid.FloatRgba[PixelIndex + 0], Pyramid.FloatRgba[PixelIndex + 1],
                Pyramid.FloatRgba[PixelIndex + 2], Pyramid.FloatRgba[PixelIndex + 3]};
        }
        if (PixelIndex + 3 >= Pyramid.Rgba.size()) return {1.f, 1.f, 1.f, 1.f};
        const bool IsSrgb = Pyramid.Category == MaterialTextureCategory::Albedo ||
            Pyramid.Category == MaterialTextureCategory::Radiance;
        std::array<float, 4> Out{};
        for (size_t Comp = 0; Comp < 3; ++Comp)
            Out[Comp] = IsSrgb ? DecodeSrgbByte(Pyramid.Rgba[PixelIndex + Comp]) :
                float(Pyramid.Rgba[PixelIndex + Comp]) * (1.f / 255.f);
        Out[3] = float(Pyramid.Rgba[PixelIndex + 3]) * (1.f / 255.f);
        return Out;
    };

    const auto P00 = Fetch(X0, Y0), P10 = Fetch(X1, Y0), P01 = Fetch(X0, Y1), P11 = Fetch(X1, Y1);
    std::array<float, 4> Result{};
    for (size_t Comp = 0; Comp < 4; ++Comp)
        Result[Comp] = std::lerp(std::lerp(P00[Comp], P10[Comp], Tx), std::lerp(P01[Comp], P11[Comp], Tx), Ty);
    return Result;
}

inline void ConstructContinuousBasis3(const std::array<float, 3> &N,
    std::array<float, 3> &Tangent, std::array<float, 3> &Bitangent) noexcept
{
    const float SignZ = N[2] >= 0.f ? 1.f : -1.f;
    const float A = -1.f / (SignZ + N[2]);
    const float B = N[0] * N[1] * A;
    Tangent = {1.f + SignZ * N[0] * N[0] * A, SignZ * B, -SignZ * N[0]};
    Bitangent = {B, SignZ + N[1] * N[1] * A, -N[1]};
}

inline std::array<float, 3> SampleEquirectPyramidLevel(const MaterialTexturePyramid &Pyramid,
    const std::array<float, 3> &WorldDir, float MipLevel)
{
    if (Pyramid.Levels.empty()) return {0.f, 0.f, 0.f};
    const float Azimuth = std::atan2(WorldDir[0], WorldDir[2]);
    const float U = Azimuth * (0.5f / std::numbers::pi_v<float>) + 0.5f;
    const float V = std::acos(std::clamp(WorldDir[1], -1.f, 1.f)) / std::numbers::pi_v<float>;
    const float MaxMip = float(Pyramid.Levels.size() - 1);
    const float ClampedMip = std::clamp(MipLevel, 0.f, MaxMip);
    const size_t Level0 = size_t(std::floor(ClampedMip));
    const size_t Level1 = std::min(Level0 + 1, Pyramid.Levels.size() - 1);
    const float Frac = ClampedMip - float(Level0);

    const auto SampleAtLevel = [&](size_t LevelIdx) -> std::array<float, 3>
    {
        const auto &Lv = Pyramid.Levels[LevelIdx];
        if (Lv.Width == 0 || Lv.Height == 0) return {0.f, 0.f, 0.f};
        const float Fx = (U - std::floor(U)) * float(Lv.Width) - .5f;
        const float Fy = std::clamp(V, 0.f, 1.f) * float(Lv.Height) - .5f;
        const int X0 = (int(std::floor(Fx)) % int(Lv.Width) + int(Lv.Width)) % int(Lv.Width);
        const int X1 = (X0 + 1) % int(Lv.Width);
        const int Y0 = std::clamp(int(std::floor(Fy)), 0, int(Lv.Height) - 1);
        const int Y1 = std::clamp(Y0 + 1, 0, int(Lv.Height) - 1);
        const float Tx = Fx - std::floor(Fx);
        const float Ty = Fy - std::floor(Fy);
        const auto FetchRgb = [&](int X, int Y) -> std::array<float, 3>
        {
            const size_t ByteOffset = Lv.Offset + (size_t(Y) * Lv.Width + size_t(X)) * 4;
            if (ByteOffset + 2 >= Pyramid.Rgba.size()) return {0.f, 0.f, 0.f};
            return {
                DecodeSrgbByte(Pyramid.Rgba[ByteOffset + 0]),
                DecodeSrgbByte(Pyramid.Rgba[ByteOffset + 1]),
                DecodeSrgbByte(Pyramid.Rgba[ByteOffset + 2])};
        };
        const auto P00 = FetchRgb(X0, Y0), P10 = FetchRgb(X1, Y0), P01 = FetchRgb(X0, Y1), P11 = FetchRgb(X1, Y1);
        return {
            std::lerp(std::lerp(P00[0], P10[0], Tx), std::lerp(P01[0], P11[0], Tx), Ty),
            std::lerp(std::lerp(P00[1], P10[1], Tx), std::lerp(P01[1], P11[1], Tx), Ty),
            std::lerp(std::lerp(P00[2], P10[2], Tx), std::lerp(P01[2], P11[2], Tx), Ty)};
    };

    const auto C0 = SampleAtLevel(Level0);
    if (Level0 == Level1 || Frac <= 1e-4f) return C0;
    const auto C1 = SampleAtLevel(Level1);
    return {std::lerp(C0[0], C1[0], Frac), std::lerp(C0[1], C1[1], Frac), std::lerp(C0[2], C1[2], Frac)};
}

// Studio environment radiance model matching MaterialLibraryRendering::SynthesizeFallbackEnvironment
// so highlights, softboxes, and horizon bounce match the GPU studio rig across all 3 workspaces.
inline std::array<float, 3> SampleStudioEnvironmentCpu(const std::array<float, 3> &Direction,
    const std::array<float, 3> &EnvironmentUp, const std::array<float, 3> &KeyLight, float Roughness, float Strength)
{
    if (Strength <= 0.f) return {0.f, 0.f, 0.f};
    const float Elevation = Saturate(.5f + .5f * Dot3(Direction, EnvironmentUp));
    std::array<float, 3> Sky{
        std::lerp(.075f, .34f, Elevation),
        std::lerp(.078f, .36f, Elevation),
        std::lerp(.086f, .40f, Elevation)};
    // Key studio softbox + cool rim fill panel, broadened smoothly by surface roughness.
    const float KeyAlign = std::max(0.f, Dot3(Direction, KeyLight));
    const float BroadExp = std::lerp(42.f, 2.2f, Saturate(Roughness * 1.25f));
    const float SoftboxGain = std::pow(KeyAlign, BroadExp) * std::lerp(3.2f, .55f, Saturate(Roughness));
    const std::array<float, 3> FillDir = Normalize3({-KeyLight[0] * .75f, KeyLight[1] * .35f + .25f, -KeyLight[2] * .55f});
    const float FillAlign = std::max(0.f, Dot3(Direction, FillDir));
    const float FillGain = std::pow(FillAlign, std::lerp(18.f, 1.8f, Saturate(Roughness))) * std::lerp(.95f, .28f, Saturate(Roughness));
    return {
        (Sky[0] + SoftboxGain * 1.04f + FillGain * .78f) * Strength,
        (Sky[1] + SoftboxGain * 1.00f + FillGain * .86f) * Strength,
        (Sky[2] + SoftboxGain * .94f + FillGain * .98f) * Strength};
}

inline uint8_t EncodeDisplayByte(float LinearRadiance, float Exposure)
{
    const float Exposed = std::max(0.f, LinearRadiance * Exposure);
    const float Aces = Saturate(Exposed * (2.51f * Exposed + .03f) / (Exposed * (2.43f * Exposed + .59f) + .14f));
    const float Srgb = Aces <= .0031308f ? 12.92f * Aces : 1.055f * std::pow(Aces, 1.f / 2.4f) - .055f;
    return uint8_t(std::lround(Saturate(Srgb) * 255.f));
}
} // namespace WorkspaceViewportDetail

inline MaterialPreviewProjection ConstructWorkspaceOrbitProjection(const WorkspaceViewportOrbitCamera &Orbit, float AspectRatio = 1.f)
{
    float Pivot[3]{Orbit.Pivot[0], Orbit.Pivot[1], Orbit.Pivot[2]};
    return ConstructOrbitProjection(Orbit.Yaw, Orbit.Pitch, Orbit.Zoom, Orbit.PanX, Orbit.PanY,
        std::max(.05f, Orbit.Radius), Pivot, Orbit.FrameRatio, AspectRatio, Orbit.VerticalFieldDegrees);
}

inline std::array<float, 3> ConstructWorkspaceOrbitLightDirection(const WorkspaceViewportOrbitCamera &Orbit,
    std::array<float, 3> WorldLight = {.42f, .72f, .55f})
{
    const auto NormWorld = WorkspaceViewportDetail::Normalize3(WorldLight);
    const auto &View = ConstructWorkspaceOrbitProjection(Orbit, 1.f).ViewFromSurface;
    return WorkspaceViewportDetail::Normalize3({
        View[0] * NormWorld[0] + View[4] * NormWorld[1] + View[8] * NormWorld[2],
        View[1] * NormWorld[0] + View[5] * NormWorld[1] + View[9] * NormWorld[2],
        View[2] * NormWorld[0] + View[6] * NormWorld[1] + View[10] * NormWorld[2]});
}

inline std::array<float, 3> ConstructWorkspaceOrbitEnvironmentUp(const WorkspaceViewportOrbitCamera &Orbit)
{
    const auto &ExactView = ConstructWorkspaceOrbitProjection(Orbit, 1.f).ViewFromSurface;
    return WorkspaceViewportDetail::Normalize3({ExactView[4], ExactView[5], ExactView[6]});
}

// Ensures unit normals, bounded UVs, and Gram-Schmidt normal-orthogonal unit tangents with +/-1 handedness
// so arbitrary imported meshes satisfy ValidateSurfaceGeometry for MaterialSurface.slang GPU & CPU rendering.
inline SurfaceGeometry SanitizeMaterialSurfaceGeometry(SurfaceGeometry Mesh)
{
    using namespace WorkspaceViewportDetail;
    Mesh.MaterialCoordinatesEnabled = true;
    std::vector<std::array<double, 3>> AccumTan(Mesh.Corners.size(), {0., 0., 0.});
    std::vector<std::array<double, 3>> AccumBitan(Mesh.Corners.size(), {0., 0., 0.});
    for (size_t Tri = 0; Tri + 2 < Mesh.TriangleCorners.size(); Tri += 3)
    {
        const uint32_t I0 = Mesh.TriangleCorners[Tri + 0];
        const uint32_t I1 = Mesh.TriangleCorners[Tri + 1];
        const uint32_t I2 = Mesh.TriangleCorners[Tri + 2];
        if (I0 >= Mesh.Corners.size() || I1 >= Mesh.Corners.size() || I2 >= Mesh.Corners.size()) continue;
        const auto &C0 = Mesh.Corners[I0], &C1 = Mesh.Corners[I1], &C2 = Mesh.Corners[I2];
        const double Dx1 = double(C1.Position[0]) - C0.Position[0];
        const double Dy1 = double(C1.Position[1]) - C0.Position[1];
        const double Dz1 = double(C1.Position[2]) - C0.Position[2];
        const double Dx2 = double(C2.Position[0]) - C0.Position[0];
        const double Dy2 = double(C2.Position[1]) - C0.Position[1];
        const double Dz2 = double(C2.Position[2]) - C0.Position[2];
        const double Du1 = double(C1.Coordinates[0]) - C0.Coordinates[0];
        const double Dv1 = double(C1.Coordinates[1]) - C0.Coordinates[1];
        const double Du2 = double(C2.Coordinates[0]) - C0.Coordinates[0];
        const double Dv2 = double(C2.Coordinates[1]) - C0.Coordinates[1];
        const double Det = Du1 * Dv2 - Du2 * Dv1;
        if (std::abs(Det) <= 1e-10) continue;
        const double InvDet = 1. / Det;
        const std::array<double, 3> TriTan{
            (Dv2 * Dx1 - Dv1 * Dx2) * InvDet,
            (Dv2 * Dy1 - Dv1 * Dy2) * InvDet,
            (Dv2 * Dz1 - Dv1 * Dz2) * InvDet};
        const std::array<double, 3> TriBitan{
            (Du1 * Dx2 - Du2 * Dx1) * InvDet,
            (Du1 * Dy2 - Du2 * Dy1) * InvDet,
            (Du1 * Dz2 - Du2 * Dz1) * InvDet};
        for (uint32_t Idx : {I0, I1, I2})
            for (size_t Axis = 0; Axis < 3; ++Axis)
            {
                AccumTan[Idx][Axis] += TriTan[Axis];
                AccumBitan[Idx][Axis] += TriBitan[Axis];
            }
    }
    for (size_t Index = 0; Index < Mesh.Corners.size(); ++Index)
    {
        auto &Corner = Mesh.Corners[Index];
        for (float &Coord : Corner.Position) if (!std::isfinite(Coord)) Coord = 0.f;
        for (float &Uv : Corner.Coordinates) Uv = std::isfinite(Uv) ? std::clamp(Uv, -512.f, 512.f) : 0.f;
        Corner.Normal = Normalize3(Corner.Normal);

        std::array<double, 3> N{Corner.Normal[0], Corner.Normal[1], Corner.Normal[2]};
        const double NLen = std::sqrt(N[0] * N[0] + N[1] * N[1] + N[2] * N[2]);
        if (NLen > 1e-12) for (double &Comp : N) Comp /= NLen;
        else N = {0., 0., 1.};
        Corner.Normal = {float(N[0]), float(N[1]), float(N[2])};

        std::array<double, 3> T{Corner.Tangent[0], Corner.Tangent[1], Corner.Tangent[2]};
        const double ExistingLen = std::sqrt(T[0] * T[0] + T[1] * T[1] + T[2] * T[2]);
        const double AccumLen = std::sqrt(
            AccumTan[Index][0] * AccumTan[Index][0] +
            AccumTan[Index][1] * AccumTan[Index][1] +
            AccumTan[Index][2] * AccumTan[Index][2]);
        if (!std::isfinite(ExistingLen) || ExistingLen < 1e-5)
            T = AccumLen > 1e-8 ? AccumTan[Index] :
                (std::abs(N[1]) < .9 ? std::array<double, 3>{0., 1., 0.} : std::array<double, 3>{1., 0., 0.});

        const double Proj = N[0] * T[0] + N[1] * T[1] + N[2] * T[2];
        T = {T[0] - N[0] * Proj, T[1] - N[1] * Proj, T[2] - N[2] * Proj};
        double TLen = std::sqrt(T[0] * T[0] + T[1] * T[1] + T[2] * T[2]);
        if (TLen < 1e-8)
        {
            const std::array<double, 3> Ref = std::abs(N[1]) < .9 ?
                std::array<double, 3>{0., 1., 0.} : std::array<double, 3>{1., 0., 0.};
            T = {
                Ref[1] * N[2] - Ref[2] * N[1],
                Ref[2] * N[0] - Ref[0] * N[2],
                Ref[0] * N[1] - Ref[1] * N[0]};
            TLen = std::sqrt(T[0] * T[0] + T[1] * T[1] + T[2] * T[2]);
        }
        for (double &Comp : T) Comp /= std::max(1e-12, TLen);
        // Second Gram-Schmidt pass in float precision so ValidateSurfaceGeometry (< 1e-3) always holds.
        std::array<float, 3> Tf{float(T[0]), float(T[1]), float(T[2])};
        const float ProjF = Dot3(Corner.Normal, Tf);
        Tf = Normalize3({Tf[0] - Corner.Normal[0] * ProjF, Tf[1] - Corner.Normal[1] * ProjF, Tf[2] - Corner.Normal[2] * ProjF});

        float Sign = Corner.Tangent[3] < 0.f ? -1.f : 1.f;
        if (AccumLen > 1e-8)
        {
            const auto CrossNT = Cross3(Corner.Normal, Tf);
            const double Handed = CrossNT[0] * AccumBitan[Index][0] + CrossNT[1] * AccumBitan[Index][1] + CrossNT[2] * AccumBitan[Index][2];
            Sign = Handed < 0. ? -1.f : 1.f;
        }
        Corner.Tangent = {Tf[0], Tf[1], Tf[2], Sign};
    }
    return Mesh;
}

// Perspective-correct Z-buffered PBR/Flakes/Clearcoat/Sheen/AuthoringMaps/Studio-IBL/ACES CPU twin of MaterialSurface.slang.
inline WorkspaceViewportCpuFrame RenderWorkspaceSurfaceViewportCpu(const WorkspaceSurfaceViewportRequest &Request,
    uint32_t Width, uint32_t Height)
{
    using namespace WorkspaceViewportDetail;
    WorkspaceViewportCpuFrame Frame{Width, Height, std::vector<uint8_t>(size_t(Width) * Height * 4, 0)};
    if (!Request.Geometry || !Request.Material || Width == 0 || Height == 0) return Frame;
    const auto &Geometry = *Request.Geometry;
    if (Geometry.Corners.empty() || Geometry.TriangleCorners.size() < 3) return Frame;

    const uint32_t TotalIndices = uint32_t(Geometry.TriangleCorners.size());
    const uint32_t BeginIndex = std::min(Request.FirstIndex, TotalIndices);
    const uint32_t CountIndices = Request.IndexCount == 0 ? (TotalIndices - BeginIndex) :
        std::min(Request.IndexCount, TotalIndices - BeginIndex);
    if (CountIndices < 3) return Frame;

    const auto &Clip = Request.Camera.ClipFromSurface;
    const auto &View = Request.Camera.ViewFromSurface;
    const float ViewDet =
        View[0] * (View[5] * View[10] - View[9] * View[6]) -
        View[4] * (View[1] * View[10] - View[9] * View[2]) +
        View[8] * (View[1] * View[6] - View[5] * View[2]);
    const float HandednessFlip = ViewDet < 0.f ? -1.f : 1.f;

    struct TransformedVertex
    {
        float ScreenX = 0.f, ScreenY = 0.f, InvW = 0.f, Depth = 1.f;
        bool ClipValid = false;
        std::array<float, 3> ViewPos{};
        std::array<float, 3> ViewNormal{};
        std::array<float, 4> ViewTangent{};
        std::array<float, 2> Uv{};
        std::array<float, 3> MaterialPos{};
        uint32_t SubjectRole = 0;
    };

    const auto &MatScattering = Request.Material->Scattering;
    std::vector<uint8_t> Touched(Geometry.Corners.size(), 0);
    for (uint32_t Idx = BeginIndex; Idx < BeginIndex + CountIndices; ++Idx)
    {
        const uint32_t CornerIndex = Geometry.TriangleCorners[Idx];
        if (CornerIndex < Touched.size()) Touched[CornerIndex] = 1;
    }

    std::vector<TransformedVertex> Vertices(Geometry.Corners.size());
    for (size_t Index = 0; Index < Geometry.Corners.size(); ++Index)
    {
        if (!Touched[Index]) continue;
        const auto &Corner = Geometry.Corners[Index];
        auto &Out = Vertices[Index];
        const float Px = Corner.Position[0], Py = Corner.Position[1], Pz = Corner.Position[2];
        const float Cx = Clip[0] * Px + Clip[4] * Py + Clip[8] * Pz + Clip[12];
        const float Cy = Clip[1] * Px + Clip[5] * Py + Clip[9] * Pz + Clip[13];
        const float Cz = Clip[2] * Px + Clip[6] * Py + Clip[10] * Pz + Clip[14];
        const float Cw = Clip[3] * Px + Clip[7] * Py + Clip[11] * Pz + Clip[15];
        if (Cw <= 1e-4f) continue;
        Out.InvW = 1.f / Cw;
        const float NdcX = Cx * Out.InvW;
        const float NdcY = Cy * Out.InvW;
        const float NdcZ = Cz * Out.InvW;
        if (NdcZ < -.25f || NdcZ > 1.25f) continue;
        Out.ClipValid = true;
        // ConstructOrbitProjection & ConstructMaterialPreviewProjection produce Vulkan Y-down clip space:
        // NdcY = -1 is top of screen, +1 is bottom of screen.
        Out.ScreenX = (NdcX * .5f + .5f) * float(Width);
        Out.ScreenY = (NdcY * .5f + .5f) * float(Height);
        Out.Depth = NdcZ;
        Out.ViewPos = {
            View[0] * Px + View[4] * Py + View[8] * Pz + View[12],
            View[1] * Px + View[5] * Py + View[9] * Pz + View[13],
            View[2] * Px + View[6] * Py + View[10] * Pz + View[14]};
        Out.ViewNormal = Normalize3({
            View[0] * Corner.Normal[0] + View[4] * Corner.Normal[1] + View[8] * Corner.Normal[2],
            View[1] * Corner.Normal[0] + View[5] * Corner.Normal[1] + View[9] * Corner.Normal[2],
            View[2] * Corner.Normal[0] + View[6] * Corner.Normal[1] + View[10] * Corner.Normal[2]});
        const float Tx = View[0] * Corner.Tangent[0] + View[4] * Corner.Tangent[1] + View[8] * Corner.Tangent[2];
        const float Ty = View[1] * Corner.Tangent[0] + View[5] * Corner.Tangent[1] + View[9] * Corner.Tangent[2];
        const float Tz = View[2] * Corner.Tangent[0] + View[6] * Corner.Tangent[1] + View[10] * Corner.Tangent[2];
        const float TLenSq = Tx * Tx + Ty * Ty + Tz * Tz;
        if (TLenSq > 1e-8f)
        {
            const float InvTLen = 1.f / std::sqrt(TLenSq);
            Out.ViewTangent = {Tx * InvTLen, Ty * InvTLen, Tz * InvTLen, (Corner.Tangent[3] < 0.f ? -1.f : 1.f) * HandednessFlip};
        }
        else
        {
            const std::array<float, 3> UpRef = std::abs(Out.ViewNormal[1]) < .92f ?
                std::array<float, 3>{0.f, 1.f, 0.f} : std::array<float, 3>{1.f, 0.f, 0.f};
            const auto AutoTan = Normalize3(Cross3(UpRef, Out.ViewNormal));
            Out.ViewTangent = {AutoTan[0], AutoTan[1], AutoTan[2], 1.f};
        }
        Out.Uv = {
            Corner.Coordinates[0] * MatScattering.UvScaleOffset[0] + MatScattering.UvScaleOffset[2],
            Corner.Coordinates[1] * MatScattering.UvScaleOffset[1] + MatScattering.UvScaleOffset[3]};
        Out.SubjectRole = (Request.PreviewShapeOrdinal == 1 || Request.PreviewShapeOrdinal == 5) &&
            Corner.Coordinates[0] < 0.f ? uint32_t(-Corner.Coordinates[0]) : 0u;
        Out.MaterialPos = Corner.Position;
        if (Request.PreviewShapeOrdinal > 1)
            Out.MaterialPos[0] -= 16.f * float(Request.PreviewShapeOrdinal - 1);
        if (Request.PreviewShapeOrdinal == 1 && Out.SubjectRole == 0)
        {
            const float Angle = Corner.Coordinates[1] * std::numbers::pi_v<float>;
            const float Longitude = Corner.Coordinates[0] * 2.f * std::numbers::pi_v<float>;
            const float BallHeight = 1.43f * std::cos(Angle);
            const auto Groove = [BallHeight](float Centre, float GrooveWidth, float GrooveDepth) {
                return GrooveDepth * std::exp(-std::pow(std::abs((BallHeight - Centre) / GrooveWidth), 6.f));
            };
            const float BallRadius = std::max(.001f, 1.43f * std::sin(Angle) -
                Groove(.69f, .021f, .032f) - Groove(.6f, .014f, .023f) -
                Groove(-.81f, .018f, .028f) - Groove(-.89f, .018f, .028f) - Groove(-.97f, .018f, .028f));
            Out.MaterialPos = {BallRadius * std::sin(Longitude), BallHeight, BallRadius * std::cos(Longitude)};
        }
    }

    std::vector<float> DepthBuffer(size_t(Width) * Height, std::numeric_limits<float>::infinity());
    const auto LightDir = Normalize3(Request.LightDirection);
    const auto EnvUp = Normalize3(Request.EnvironmentUp);
    const float FlakeFreq = std::min(1e7f, 52000.f / std::max(.001f, MatScattering.Paint.Distribution[1]) * MatScattering.Paint.Distribution[2]);

    for (uint32_t Idx = BeginIndex; Idx + 2 < BeginIndex + CountIndices; Idx += 3)
    {
        const uint32_t I0 = Geometry.TriangleCorners[Idx + 0];
        const uint32_t I1 = Geometry.TriangleCorners[Idx + 1];
        const uint32_t I2 = Geometry.TriangleCorners[Idx + 2];
        if (I0 >= Vertices.size() || I1 >= Vertices.size() || I2 >= Vertices.size()) continue;
        const auto &V0 = Vertices[I0], &V1 = Vertices[I1], &V2 = Vertices[I2];
        if (!V0.ClipValid || !V1.ClipValid || !V2.ClipValid) continue;

        const float Area = (V1.ScreenX - V0.ScreenX) * (V2.ScreenY - V0.ScreenY) -
                           (V1.ScreenY - V0.ScreenY) * (V2.ScreenX - V0.ScreenX);
        if (std::abs(Area) <= 1e-4f) continue;
        const float InvArea = 1.f / Area;

        const int MinX = std::max(0, int(std::floor(std::min({V0.ScreenX, V1.ScreenX, V2.ScreenX}))));
        const int MaxX = std::min(int(Width) - 1, int(std::ceil(std::max({V0.ScreenX, V1.ScreenX, V2.ScreenX}))));
        const int MinY = std::max(0, int(std::floor(std::min({V0.ScreenY, V1.ScreenY, V2.ScreenY}))));
        const int MaxY = std::min(int(Height) - 1, int(std::ceil(std::max({V0.ScreenY, V1.ScreenY, V2.ScreenY}))));
        if (MinX > MaxX || MinY > MaxY) continue;

        // Approximate screen-space footprint in Voronoi cells for antialiased distance fade.
        const float EdgeMat01 = std::sqrt(
            (V1.MaterialPos[0] - V0.MaterialPos[0]) * (V1.MaterialPos[0] - V0.MaterialPos[0]) +
            (V1.MaterialPos[1] - V0.MaterialPos[1]) * (V1.MaterialPos[1] - V0.MaterialPos[1]) +
            (V1.MaterialPos[2] - V0.MaterialPos[2]) * (V1.MaterialPos[2] - V0.MaterialPos[2]));
        const float EdgePix01 = std::max(1.f, std::hypot(V1.ScreenX - V0.ScreenX, V1.ScreenY - V0.ScreenY));
        const float FootprintCells = (EdgeMat01 / EdgePix01) * FlakeFreq;

        for (int Py = MinY; Py <= MaxY; ++Py)
        {
            const float SampleY = float(Py) + .5f;
            for (int Px = MinX; Px <= MaxX; ++Px)
            {
                const float SampleX = float(Px) + .5f;
                const float W0 = ((V1.ScreenX - SampleX) * (V2.ScreenY - SampleY) -
                                  (V1.ScreenY - SampleY) * (V2.ScreenX - SampleX)) * InvArea;
                const float W1 = ((V2.ScreenX - SampleX) * (V0.ScreenY - SampleY) -
                                  (V2.ScreenY - SampleY) * (V0.ScreenX - SampleX)) * InvArea;
                const float W2 = 1.f - W0 - W1;
                if (W0 < -1e-5f || W1 < -1e-5f || W2 < -1e-5f) continue;

                const float Depth = W0 * V0.Depth + W1 * V1.Depth + W2 * V2.Depth;
                const size_t PixelOffset = size_t(Py) * Width + size_t(Px);
                if (Depth >= DepthBuffer[PixelOffset]) continue;
                DepthBuffer[PixelOffset] = Depth;

                // Perspective-correct interpolation weights.
                const float Persp0 = W0 * V0.InvW;
                const float Persp1 = W1 * V1.InvW;
                const float Persp2 = W2 * V2.InvW;
                const float InvPerspSum = 1.f / std::max(1e-12f, Persp0 + Persp1 + Persp2);
                const float B0 = Persp0 * InvPerspSum;
                const float B1 = Persp1 * InvPerspSum;
                const float B2 = Persp2 * InvPerspSum;

                const std::array<float, 3> ViewPosition{
                    B0 * V0.ViewPos[0] + B1 * V1.ViewPos[0] + B2 * V2.ViewPos[0],
                    B0 * V0.ViewPos[1] + B1 * V1.ViewPos[1] + B2 * V2.ViewPos[1],
                    B0 * V0.ViewPos[2] + B1 * V1.ViewPos[2] + B2 * V2.ViewPos[2]};
                std::array<float, 3> GeometricNormal = Normalize3({
                    B0 * V0.ViewNormal[0] + B1 * V1.ViewNormal[0] + B2 * V2.ViewNormal[0],
                    B0 * V0.ViewNormal[1] + B1 * V1.ViewNormal[1] + B2 * V2.ViewNormal[1],
                    B0 * V0.ViewNormal[2] + B1 * V1.ViewNormal[2] + B2 * V2.ViewNormal[2]});
                const std::array<float, 3> ViewDir = Normalize3({-ViewPosition[0], -ViewPosition[1], -ViewPosition[2]});
                if (Dot3(GeometricNormal, ViewDir) < 0.f && V0.SubjectRole == 0)
                    GeometricNormal = {-GeometricNormal[0], -GeometricNormal[1], -GeometricNormal[2]};

                const std::array<float, 3> InterpTanRaw{
                    B0 * V0.ViewTangent[0] + B1 * V1.ViewTangent[0] + B2 * V2.ViewTangent[0],
                    B0 * V0.ViewTangent[1] + B1 * V1.ViewTangent[1] + B2 * V2.ViewTangent[1],
                    B0 * V0.ViewTangent[2] + B1 * V1.ViewTangent[2] + B2 * V2.ViewTangent[2]};
                const float NdotT = Dot3(GeometricNormal, InterpTanRaw);
                const std::array<float, 3> Tangent = Normalize3({
                    InterpTanRaw[0] - GeometricNormal[0] * NdotT,
                    InterpTanRaw[1] - GeometricNormal[1] * NdotT,
                    InterpTanRaw[2] - GeometricNormal[2] * NdotT});
                const float TanSign = V0.ViewTangent[3] < 0.f ? -1.f : 1.f;

                const std::array<float, 2> BaseUv{
                    B0 * V0.Uv[0] + B1 * V1.Uv[0] + B2 * V2.Uv[0],
                    B0 * V0.Uv[1] + B1 * V1.Uv[1] + B2 * V2.Uv[1]};
                const std::array<float, 3> MaterialPosition{
                    B0 * V0.MaterialPos[0] + B1 * V1.MaterialPos[0] + B2 * V2.MaterialPos[0],
                    B0 * V0.MaterialPos[1] + B1 * V1.MaterialPos[1] + B2 * V2.MaterialPos[1],
                    B0 * V0.MaterialPos[2] + B1 * V1.MaterialPos[2] + B2 * V2.MaterialPos[2]};
                const uint32_t Role = V0.SubjectRole;

                PreviewScattering LocalIllum = MatScattering.Illumination;
                if (Role != 0)
                {
                    LocalIllum.Configuration[0] = Role == 1 ? 2u : 1u;
                    const std::array<float, 3> PedestalTint =
                        Role == 1 ? std::array<float, 3>{.015209f, .015996f, .019382f} :
                        Role == 2 ? std::array<float, 3>{.006512f, .006995f, .008568f} :
                        Role == 4 ? std::array<float, 3>{.008023f, .009721f, .013702f} :
                                    std::array<float, 3>{.135633f, .144128f, .165132f};
                    const float PedestalRough = Role == 1 ? .5f : Role == 2 ? .35f : Role == 4 ? .4f : .32f;
                    const float PedestalMetal = Role == 1 ? .5f : Role == 2 ? .6f : Role == 4 ? .4f : .85f;
                    LocalIllum.Strata[0] = {};
                    LocalIllum.Strata[0].AlbedoRoughness = {PedestalTint[0], PedestalTint[1], PedestalTint[2], PedestalRough};
                    LocalIllum.Strata[0].Optics = {PedestalMetal, 1.5f, 1.f, 0.f};
                    if (Role == 1)
                    {
                        LocalIllum.Strata[1] = {};
                        LocalIllum.Strata[1].AlbedoRoughness = {1.f, 1.f, 1.f, .1f};
                        LocalIllum.Strata[1].Optics = {0.f, 1.5f, .1f, 0.f};
                        LocalIllum.Strata[1].Classification[0] = 1u;
                    }
                }

                const bool PaintEnabled = MatScattering.Paint.Configuration[0] != 0 && Role == 0;
                PaintSurfaceCpu Paint{};
                std::array<float, 3> DirectAccum{0.f, 0.f, 0.f};
                std::array<float, 3> EnvAccum{0.f, 0.f, 0.f};
                const uint32_t StrataCount = std::min(8u, LocalIllum.Configuration[0]);

                for (uint32_t StratumIndex = 0; StratumIndex < StrataCount; ++StratumIndex)
                {
                    auto Stratum = LocalIllum.Strata[StratumIndex];
                    const auto &TexSel = MatScattering.TextureUse[StratumIndex];
                    const uint32_t Mask = Role == 0 ? TexSel[0] : 0u;
                    const float CoordU = BaseUv[0] * MatScattering.StratumUv[StratumIndex][0] + MatScattering.StratumUv[StratumIndex][2];
                    const float CoordV = BaseUv[1] * MatScattering.StratumUv[StratumIndex][1] + MatScattering.StratumUv[StratumIndex][3];

                    if ((Mask & 1u) != 0)
                    {
                        const auto AlbedoTex = SamplePyramidBilinear(Request.Material->Textures[0], CoordU, CoordV, TexSel[1]);
                        for (size_t Comp = 0; Comp < 3; ++Comp) Stratum.AlbedoRoughness[Comp] *= AlbedoTex[Comp];
                    }
                    if (StratumIndex == 0 && Role == 0)
                        for (size_t Comp = 0; Comp < 3; ++Comp) Stratum.AlbedoRoughness[Comp] *= Request.SurfaceTint[Comp];

                    float Occlusion = 1.f;
                    if ((Mask & 2u) != 0)
                    {
                        const auto OrmTex = SamplePyramidBilinear(Request.Material->Textures[1], CoordU, CoordV, TexSel[2]);
                        Occlusion = OrmTex[0];
                        Stratum.AlbedoRoughness[3] = std::clamp(Stratum.AlbedoRoughness[3] * OrmTex[1], .045f, 1.f);
                        if (Stratum.Classification[0] == 0) Stratum.Optics[0] *= OrmTex[2];
                    }

                    std::array<float, 3> Normal = GeometricNormal;
                    if (PaintEnabled)
                    {
                        if (StratumIndex == 0)
                        {
                            Paint = EvaluatePaintSurfaceCpu(MatScattering.Paint, MaterialPosition,
                                {Stratum.AlbedoRoughness[0], Stratum.AlbedoRoughness[1], Stratum.AlbedoRoughness[2]},
                                Stratum.AlbedoRoughness[3], Stratum.Optics[0], FootprintCells);
                            Stratum.AlbedoRoughness[0] = Paint.Albedo[0];
                            Stratum.AlbedoRoughness[1] = Paint.Albedo[1];
                            Stratum.AlbedoRoughness[2] = Paint.Albedo[2];
                            Stratum.AlbedoRoughness[3] = Paint.Roughness;
                            Stratum.Optics[0] = Paint.Metallic;
                        }
                        if (Stratum.Classification[0] == 0 && (std::abs(Paint.Slope[0]) > 1e-5f || std::abs(Paint.Slope[1]) > 1e-5f))
                        {
                            std::array<float, 3> FlakeTan{}, FlakeBitan{};
                            ConstructContinuousBasis3(Normal, FlakeTan, FlakeBitan);
                            Normal = Normalize3({
                                Normal[0] + FlakeTan[0] * Paint.Slope[0] + FlakeBitan[0] * Paint.Slope[1],
                                Normal[1] + FlakeTan[1] * Paint.Slope[0] + FlakeBitan[1] * Paint.Slope[1],
                                Normal[2] + FlakeTan[2] * Paint.Slope[0] + FlakeBitan[2] * Paint.Slope[1]});
                        }
                    }

                    const float AuthoringEnabled = (Role == 0 && Stratum.Classification[0] == 0) ? 1.f : 0.f;
                    std::array<float, 4> AuthoredNormal{0.5f, 0.5f, 1.f, 0.f};
                    std::array<float, 4> AuthoredHeight{1.f, 0.f, 0.f, 0.f};
                    if (AuthoringEnabled > 0.f)
                    {
                        const auto AuthoredColour = SamplePyramidBilinear(Request.Material->AuthoringMaps[0], CoordU, CoordV, 0);
                        AuthoredNormal = SamplePyramidBilinear(Request.Material->AuthoringMaps[1], CoordU, CoordV, 0);
                        AuthoredHeight = SamplePyramidBilinear(Request.Material->AuthoringMaps[2], CoordU, CoordV, 0);
                        const auto AuthoredRoughness = SamplePyramidBilinear(Request.Material->AuthoringMaps[3], CoordU, CoordV, 0);
                        const auto AuthoredMetalness = SamplePyramidBilinear(Request.Material->AuthoringMaps[4], CoordU, CoordV, 0);
                        const auto AuthoredOcclusion = SamplePyramidBilinear(Request.Material->AuthoringMaps[5], CoordU, CoordV, 0);
                        const float ColA = Saturate(AuthoredColour[3]);
                        for (size_t Comp = 0; Comp < 3; ++Comp)
                            Stratum.AlbedoRoughness[Comp] = std::lerp(Stratum.AlbedoRoughness[Comp], AuthoredColour[Comp], ColA);
                        Stratum.AlbedoRoughness[3] = std::clamp(
                            std::lerp(Stratum.AlbedoRoughness[3], AuthoredRoughness[0], Saturate(AuthoredRoughness[3])), .045f, 1.f);
                        Stratum.Optics[0] = std::lerp(Stratum.Optics[0], AuthoredMetalness[0], Saturate(AuthoredMetalness[3]));
                        Occlusion *= std::lerp(1.f, AuthoredOcclusion[0], Saturate(AuthoredOcclusion[3]));
                    }

                    std::array<float, 3> TangentNormal{0.f, 0.f, 1.f};
                    if ((Mask & 4u) != 0)
                    {
                        const auto NormTex = SamplePyramidBilinear(Request.Material->Textures[2], CoordU, CoordV, TexSel[3]);
                        TangentNormal = {NormTex[0] * 2.f - 1.f, NormTex[1] * 2.f - 1.f, NormTex[2] * 2.f - 1.f};
                    }
                    if (AuthoredHeight[3] > 0.f && AuthoringEnabled > 0.f)
                    {
                        constexpr float Delta = 1.f / 256.f;
                        const float HLeft = SamplePyramidBilinear(Request.Material->AuthoringMaps[2], CoordU - Delta, CoordV, 0)[0];
                        const float HRight = SamplePyramidBilinear(Request.Material->AuthoringMaps[2], CoordU + Delta, CoordV, 0)[0];
                        const float HUp = SamplePyramidBilinear(Request.Material->AuthoringMaps[2], CoordU, CoordV - Delta, 0)[0];
                        const float HDown = SamplePyramidBilinear(Request.Material->AuthoringMaps[2], CoordU, CoordV + Delta, 0)[0];
                        const float HStrength = MatScattering.StratumNormal[StratumIndex][1];
                        const auto HeightNormal = Normalize3({-(HRight - HLeft) * HStrength, -(HDown - HUp) * HStrength, 1.f});
                        const auto Combined = Normalize3({
                            TangentNormal[0] + HeightNormal[0],
                            TangentNormal[1] + HeightNormal[1],
                            TangentNormal[2] + HeightNormal[2] - 1.f});
                        const float Ha = Saturate(AuthoredHeight[3]);
                        TangentNormal = Normalize3({
                            std::lerp(TangentNormal[0], Combined[0], Ha),
                            std::lerp(TangentNormal[1], Combined[1], Ha),
                            std::lerp(TangentNormal[2], Combined[2], Ha)});
                    }
                    if ((Mask & 4u) != 0 || (AuthoredNormal[3] > 0.f && AuthoringEnabled > 0.f) || (AuthoredHeight[3] > 0.f && AuthoringEnabled > 0.f))
                    {
                        if (AuthoredNormal[3] > 0.f && AuthoringEnabled > 0.f)
                        {
                            const float Na = Saturate(AuthoredNormal[3]);
                            TangentNormal = Normalize3({
                                std::lerp(TangentNormal[0], AuthoredNormal[0] * 2.f - 1.f, Na),
                                std::lerp(TangentNormal[1], AuthoredNormal[1] * 2.f - 1.f, Na),
                                std::lerp(TangentNormal[2], AuthoredNormal[2] * 2.f - 1.f, Na)});
                        }
                        const float Strength = MatScattering.RadianceNormal[3] * MatScattering.StratumNormal[StratumIndex][0];
                        TangentNormal[0] *= Strength;
                        TangentNormal[1] *= Strength;
                        TangentNormal = Normalize3(TangentNormal);
                        if (PaintEnabled && StratumIndex == 0 && AuthoredNormal[3] <= 0.f && AuthoredHeight[3] <= 0.f)
                        {
                            const float Suppress = Saturate(Paint.Coverage * .85f);
                            TangentNormal = Normalize3({
                                std::lerp(TangentNormal[0], 0.f, Suppress),
                                std::lerp(TangentNormal[1], 0.f, Suppress),
                                std::lerp(TangentNormal[2], 1.f, Suppress)});
                        }
                        const float FrameDot = Dot3(Normal, Tangent);
                        const auto FrameTangent = Normalize3({
                            Tangent[0] - Normal[0] * FrameDot,
                            Tangent[1] - Normal[1] * FrameDot,
                            Tangent[2] - Normal[2] * FrameDot});
                        const auto RawFrameBitan = Cross3(Normal, FrameTangent);
                        const std::array<float, 3> FrameBitangent{
                            RawFrameBitan[0] * TanSign, RawFrameBitan[1] * TanSign, RawFrameBitan[2] * TanSign};
                        Normal = Normalize3({
                            FrameTangent[0] * TangentNormal[0] + FrameBitangent[0] * TangentNormal[1] + Normal[0] * TangentNormal[2],
                            FrameTangent[1] * TangentNormal[0] + FrameBitangent[1] * TangentNormal[1] + Normal[1] * TangentNormal[2],
                            FrameTangent[2] * TangentNormal[0] + FrameBitangent[2] * TangentNormal[1] + Normal[2] * TangentNormal[2]});
                    }

                    // Direct GGX + Schlick Fresnel + Charlie Sheen + Dielectric Coating evaluation.
                    const std::array<float, 3> Albedo{Stratum.AlbedoRoughness[0], Stratum.AlbedoRoughness[1], Stratum.AlbedoRoughness[2]};
                    const float Roughness = std::clamp(Stratum.AlbedoRoughness[3], .045f, 1.f);
                    const float Metallic = Saturate(Stratum.Optics[0]);
                    const float Ior = std::max(1.01f, Stratum.Optics[1]);
                    const float Coverage = Saturate(Stratum.Optics[2]);
                    const float SpecLevel = Role == 0 ? MatScattering.DirectionalOptics[StratumIndex][3] : .5f;
                    const float BaseR = ((Ior - 1.f) / (Ior + 1.f)) * ((Ior - 1.f) / (Ior + 1.f));
                    const float DielR = BaseR * SpecLevel * 2.f;
                    const std::array<float, 3> F0{
                        std::lerp(DielR, Albedo[0], Metallic),
                        std::lerp(DielR, Albedo[1], Metallic),
                        std::lerp(DielR, Albedo[2], Metallic)};

                    const float NdotV = std::max(.001f, Saturate(Dot3(Normal, ViewDir)));
                    const float NdotL = Saturate(Dot3(Normal, LightDir));
                    std::array<float, 3> DirectEval{0.f, 0.f, 0.f};
                    if (NdotL > 0.f)
                    {
                        const auto Half = Normalize3({ViewDir[0] + LightDir[0], ViewDir[1] + LightDir[1], ViewDir[2] + LightDir[2]});
                        const float NdotH = Saturate(Dot3(Normal, Half));
                        const float VdotH = Saturate(Dot3(ViewDir, Half));
                        const float Alpha = Roughness * Roughness;
                        const float AlphaSq = Alpha * Alpha;
                        const float Denom = NdotH * NdotH * (AlphaSq - 1.f) + 1.f;
                        const float Dist = AlphaSq / (std::numbers::pi_v<float> * Denom * Denom);
                        const float Vis = .5f / std::max(1e-6f,
                            NdotL * std::sqrt(NdotV * NdotV * (1.f - AlphaSq) + AlphaSq) +
                            NdotV * std::sqrt(NdotL * NdotL * (1.f - AlphaSq) + AlphaSq));
                        const float Pow5 = std::pow(1.f - VdotH, 5.f);
                        const std::array<float, 3> Fresnel{
                            F0[0] + (1.f - F0[0]) * Pow5,
                            F0[1] + (1.f - F0[1]) * Pow5,
                            F0[2] + (1.f - F0[2]) * Pow5};
                        for (size_t Comp = 0; Comp < 3; ++Comp)
                        {
                            DirectEval[Comp] = Dist * Vis * Fresnel[Comp];
                            if (Stratum.Classification[0] == 0)
                                DirectEval[Comp] += (1.f - Fresnel[Comp]) * (1.f - Metallic) * Albedo[Comp] / std::numbers::pi_v<float>;
                        }
                        if (Stratum.Classification[0] == 0 && Stratum.Sheen[3] > 0.f)
                        {
                            const float SheenRough = std::max(Stratum.SheenFinish[0], .045f);
                            const float InvAlpha = 1.f / std::max(SheenRough * SheenRough, 1e-3f);
                            const float SinSq = std::max(1.f - NdotH * NdotH, .0078125f);
                            const float Charlie = (2.f + InvAlpha) * std::pow(SinSq, InvAlpha * .5f) / (2.f * std::numbers::pi_v<float>);
                            const float Ashikhmin = 1.f / std::max(1e-4f, 4.f * (NdotL + NdotV - NdotL * NdotV));
                            for (size_t Comp = 0; Comp < 3; ++Comp)
                                DirectEval[Comp] += Stratum.Sheen[Comp] * Stratum.Sheen[3] * Charlie * Ashikhmin;
                        }
                        if (Stratum.Classification[0] == 1)
                        {
                            const float Entrance = BaseR + (1.f - BaseR) * std::pow(1.f - NdotL, 5.f);
                            const float Exit = BaseR + (1.f - BaseR) * std::pow(1.f - NdotV, 5.f);
                            const float PathLen = 1.f / std::max(.001f, NdotV) + 1.f / std::max(.001f, NdotL);
                            for (size_t Comp = 0; Comp < 3; ++Comp)
                            {
                                const float Absorp = std::exp(-Stratum.AbsorptionThickness[Comp] * Stratum.AbsorptionThickness[3] * PathLen);
                                DirectEval[Comp] += DirectAccum[Comp] * (1.f - Entrance) * (1.f - Exit) * Absorp;
                            }
                        }
                        for (size_t Comp = 0; Comp < 3; ++Comp) DirectEval[Comp] *= NdotL;
                    }
                    for (size_t Comp = 0; Comp < 3; ++Comp)
                        DirectAccum[Comp] = std::lerp(DirectAccum[Comp], DirectEval[Comp], Coverage);

                    // Studio IBL integration (diffuse irradiance + roughness-prefiltered specular reflection).
                    const float ViewPow5 = std::pow(1.f - NdotV, 5.f);
                    const std::array<float, 3> EnvFresnel{
                        F0[0] + (1.f - F0[0]) * ViewPow5,
                        F0[1] + (1.f - F0[1]) * ViewPow5,
                        F0[2] + (1.f - F0[2]) * ViewPow5};
                    const std::array<float, 3> ReflectDir = Normalize3({
                        2.f * Dot3(Normal, ViewDir) * Normal[0] - ViewDir[0],
                        2.f * Dot3(Normal, ViewDir) * Normal[1] - ViewDir[1],
                        2.f * Dot3(Normal, ViewDir) * Normal[2] - ViewDir[2]});
                    const float Broaden = Saturate(Roughness * Roughness * 1.35f);
                    const auto LobeDir = Normalize3({
                        std::lerp(ReflectDir[0], Normal[0], Broaden),
                        std::lerp(ReflectDir[1], Normal[1], Broaden),
                        std::lerp(ReflectDir[2], Normal[2], Broaden)});
                    std::array<float, 3> SpecEnv{};
                    std::array<float, 3> DiffEnv{};
                    if (Request.EnvironmentEquirectPyramid && !Request.EnvironmentEquirectPyramid->Levels.empty())
                    {
                        const auto &Vm = Request.Camera.ViewFromSurface;
                        const auto ViewToWorld = [&](const std::array<float, 3> &D) -> std::array<float, 3>
                        {
                            // ViewFromSurface is column-major 3x3 orthogonal rotation in [0..2], [4..6], [8..10];
                            // its transpose maps view-space directions back to world space.
                            return Normalize3({
                                Vm[0] * D[0] + Vm[1] * D[1] + Vm[2] * D[2],
                                Vm[4] * D[0] + Vm[5] * D[1] + Vm[6] * D[2],
                                Vm[8] * D[0] + Vm[9] * D[1] + Vm[10] * D[2]});
                        };
                        const auto SpecRaw = SampleEquirectPyramidLevel(
                            *Request.EnvironmentEquirectPyramid, ViewToWorld(LobeDir), 1.25f + Roughness * 5.25f);
                        const auto DiffRaw = SampleEquirectPyramidLevel(
                            *Request.EnvironmentEquirectPyramid, ViewToWorld(Normal), 5.75f);
                        for (size_t Comp = 0; Comp < 3; ++Comp)
                        {
                            SpecEnv[Comp] = SpecRaw[Comp] * Request.EnvironmentStrength;
                            DiffEnv[Comp] = DiffRaw[Comp] * Request.EnvironmentStrength;
                        }
                    }
                    else
                    {
                        SpecEnv = SampleStudioEnvironmentCpu(LobeDir, EnvUp, LightDir, Roughness, Request.EnvironmentStrength);
                        DiffEnv = SampleStudioEnvironmentCpu(Normal, EnvUp, LightDir, 1.f, Request.EnvironmentStrength * .85f);
                    }
                    std::array<float, 3> EnvEval{};
                    for (size_t Comp = 0; Comp < 3; ++Comp)
                    {
                        EnvEval[Comp] = EnvFresnel[Comp] * SpecEnv[Comp] * std::lerp(1.f, Occlusion, .5f);
                        if (Stratum.Classification[0] == 0)
                            EnvEval[Comp] += Albedo[Comp] * (1.f - Metallic) * (1.f - EnvFresnel[Comp]) * DiffEnv[Comp] * Occlusion;
                        else
                        {
                            const float Exit = BaseR + (1.f - BaseR) * ViewPow5;
                            EnvEval[Comp] += EnvAccum[Comp] * (1.f - Exit);
                        }
                        EnvAccum[Comp] = std::lerp(EnvAccum[Comp], EnvEval[Comp], Coverage);
                    }
                }

                std::array<float, 3> Luminous{1.f, 1.f, 1.f};
                if (Role == 0 && (MatScattering.TextureUse[0][0] & 8u) != 0)
                {
                    const auto &BaseXf = MatScattering.StratumUv[0];
                    const float EmisU = BaseUv[0] * BaseXf[0] + BaseXf[2];
                    const float EmisV = BaseUv[1] * BaseXf[1] + BaseXf[3];
                    const auto EmisSample = SamplePyramidBilinear(Request.Material->Textures[3], EmisU, EmisV, 0);
                    Luminous = std::array<float, 3>{EmisSample[0], EmisSample[1], EmisSample[2]};
                }
                const std::array<float, 3> Radiance{
                    DirectAccum[0] * Request.LightIntensity + EnvAccum[0] + (Role == 0 ? MatScattering.RadianceNormal[0] * Luminous[0] : 0.f),
                    DirectAccum[1] * Request.LightIntensity + EnvAccum[1] + (Role == 0 ? MatScattering.RadianceNormal[1] * Luminous[1] : 0.f),
                    DirectAccum[2] * Request.LightIntensity + EnvAccum[2] + (Role == 0 ? MatScattering.RadianceNormal[2] * Luminous[2] : 0.f)};

                uint8_t *Dst = &Frame.Rgba[PixelOffset * 4];
                Dst[0] = EncodeDisplayByte(Radiance[0], Request.Exposure);
                Dst[1] = EncodeDisplayByte(Radiance[1], Request.Exposure);
                Dst[2] = EncodeDisplayByte(Radiance[2], Request.Exposure);
                Dst[3] = 255u;
            }
        }
    }
    return Frame;
}
} // namespace Frontier
