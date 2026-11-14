/*============================================================================================================================================
                                                       SURFACEANCHORSAMPLER.CPP
============================================================================================================================================*/
// 🧩 Implementation of 17 procedural surfaces (0..16), PolygonNetwork conversion & area-weighted anchor sampling

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Origins/SurfaceAnchorSampler.h"

#include "Engine/Constants/MathConstants.h"
#include "Engine/ParticleSpace/Origins/AnalyticOrigins.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <utility>

namespace Frontier::ParticleSpace
{

namespace
{

using RawTriangle = std::array<Mathematics::Axis3D, 3u>;

[[nodiscard]] Mathematics::Axis3D ConvertYUpToBasis(
    Mathematics::Axis3D               VecYUp,
    Mathematics::WorldCoordinateBasis TargetBasis) noexcept
{
    return Mathematics::ConvertVectorBetweenBases(
        VecYUp,
        Mathematics::WorldCoordinateBasis::RightHandedYUp,
        TargetBasis);
}

void AppendQuad(
    std::vector<RawTriangle>& OutTris,
    Mathematics::Axis3D       A,
    Mathematics::Axis3D       B,
    Mathematics::Axis3D       C,
    Mathematics::Axis3D       D)
{
    OutTris.push_back({ A, B, C });
    OutTris.push_back({ A, C, D });
}

void AppendBox(
    std::vector<RawTriangle>& OutTris,
    float                     Cx,
    float                     Cy,
    float                     Cz,
    float                     Hx,
    float                     Hy,
    float                     Hz)
{
    auto Pt = [&](float Sx, float Sy, float Sz) noexcept -> Mathematics::Axis3D {
        return { Cx + Sx * Hx, Cy + Sy * Hy, Cz + Sz * Hz };
    };
    AppendQuad(OutTris, Pt(-1.0f, -1.0f,  1.0f), Pt( 1.0f, -1.0f,  1.0f), Pt( 1.0f,  1.0f,  1.0f), Pt(-1.0f,  1.0f,  1.0f));
    AppendQuad(OutTris, Pt( 1.0f, -1.0f, -1.0f), Pt(-1.0f, -1.0f, -1.0f), Pt(-1.0f,  1.0f, -1.0f), Pt( 1.0f,  1.0f, -1.0f));
    AppendQuad(OutTris, Pt( 1.0f, -1.0f,  1.0f), Pt( 1.0f, -1.0f, -1.0f), Pt( 1.0f,  1.0f, -1.0f), Pt( 1.0f,  1.0f,  1.0f));
    AppendQuad(OutTris, Pt(-1.0f, -1.0f, -1.0f), Pt(-1.0f, -1.0f,  1.0f), Pt(-1.0f,  1.0f,  1.0f), Pt(-1.0f,  1.0f, -1.0f));
    AppendQuad(OutTris, Pt(-1.0f,  1.0f,  1.0f), Pt( 1.0f,  1.0f,  1.0f), Pt( 1.0f,  1.0f, -1.0f), Pt(-1.0f,  1.0f, -1.0f));
    AppendQuad(OutTris, Pt(-1.0f, -1.0f, -1.0f), Pt( 1.0f, -1.0f, -1.0f), Pt( 1.0f, -1.0f,  1.0f), Pt(-1.0f, -1.0f,  1.0f));
}

void AppendLathe(
    std::vector<RawTriangle>&                     OutTris,
    std::span<const std::pair<float, float>>      Profile,
    uint32_t                                      Segs)
{
    for (std::size_t I = 0u; I + 1u < Profile.size(); ++I)
    {
        const float R0 = Profile[I].first;
        const float Y0 = Profile[I].second;
        const float R1 = Profile[I + 1u].first;
        const float Y1 = Profile[I + 1u].second;
        for (uint32_t S = 0u; S < Segs; ++S)
        {
            const float A0 = (static_cast<float>(S) / static_cast<float>(Segs)) * Constants::TauFloat;
            const float A1 = (static_cast<float>(S + 1u) / static_cast<float>(Segs)) * Constants::TauFloat;
            const Mathematics::Axis3D P00 { std::cos(A0) * R0, Y0, std::sin(A0) * R0 };
            const Mathematics::Axis3D P10 { std::cos(A1) * R0, Y0, std::sin(A1) * R0 };
            const Mathematics::Axis3D P11 { std::cos(A1) * R1, Y1, std::sin(A1) * R1 };
            const Mathematics::Axis3D P01 { std::cos(A0) * R1, Y1, std::sin(A0) * R1 };
            if (R0 > 1.0e-4f && R1 > 1.0e-4f)
            {
                AppendQuad(OutTris, P00, P01, P11, P10);
            }
            else if (R0 <= 1.0e-4f && R1 > 1.0e-4f)
            {
                OutTris.push_back({ P00, P01, P11 });
            }
            else if (R0 > 1.0e-4f && R1 <= 1.0e-4f)
            {
                OutTris.push_back({ P00, P01, P10 });
            }
        }
    }
}

void AppendTubeAlongCurve(
    std::vector<RawTriangle>&                               OutTris,
    const std::function<Mathematics::Axis3D(float)>&        EvalPt,
    uint32_t                                                Steps,
    uint32_t                                                Sides,
    const std::function<std::pair<float, float>(float)>&    EvalRad)
{
    std::vector<std::vector<Mathematics::Axis3D>> Rings(Steps + 1u);
    for (uint32_t I = 0u; I <= Steps; ++I)
    {
        const float U = static_cast<float>(I) / static_cast<float>(Steps);
        const Mathematics::Axis3D C     = EvalPt(U);
        const Mathematics::Axis3D CNext = EvalPt(std::min(1.0f, U + 0.004f));
        const Mathematics::Axis3D CPrev = EvalPt(std::max(0.0f, U - 0.004f));
        const Mathematics::Axis3D Tang  = Mathematics::NormalizeAxis(CNext - CPrev, { 1.0f, 0.0f, 0.0f });
        const Mathematics::Axis3D UpRef = (std::abs(Tang.Y) < 0.9f) ? Mathematics::Axis3D{ 0.0f, 1.0f, 0.0f } : Mathematics::Axis3D{ 1.0f, 0.0f, 0.0f };
        const Mathematics::Axis3D UAx   = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Tang, UpRef), { 1.0f, 0.0f, 0.0f });
        const Mathematics::Axis3D VAx   = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(UAx, Tang), { 0.0f, 1.0f, 0.0f });
        const auto [Rx, Ry] = EvalRad(U);
        Rings[I].resize(Sides);
        for (uint32_t S = 0u; S < Sides; ++S)
        {
            const float Phi = (static_cast<float>(S) / static_cast<float>(Sides)) * Constants::TauFloat;
            const float Cp  = std::cos(Phi);
            const float Sp  = std::sin(Phi);
            Rings[I][S] = {
                C.X + UAx.X * (Rx * Cp) + VAx.X * (Ry * Sp),
                C.Y + UAx.Y * (Rx * Cp) + VAx.Y * (Ry * Sp),
                C.Z + UAx.Z * (Rx * Cp) + VAx.Z * (Ry * Sp),
            };
        }
    }
    for (uint32_t I = 0u; I < Steps; ++I)
    {
        for (uint32_t S = 0u; S < Sides; ++S)
        {
            const uint32_t Sn = (S + 1u) % Sides;
            AppendQuad(OutTris, Rings[I][S], Rings[I][Sn], Rings[I + 1u][Sn], Rings[I + 1u][S]);
        }
    }
}

void AppendExtrudedPolygon(
    std::vector<RawTriangle>&                     OutTris,
    std::span<const std::pair<float, float>>      Poly2D,
    float                                         HalfDepthZ)
{
    const std::size_t N = Poly2D.size();
    if (N < 3u)
    {
        return;
    }
    float Cx = 0.0f;
    float Cy = 0.0f;
    for (const auto& Pt : Poly2D)
    {
        Cx += Pt.first;
        Cy += Pt.second;
    }
    Cx /= static_cast<float>(N);
    Cy /= static_cast<float>(N);
    const Mathematics::Axis3D Cf { Cx, Cy,  HalfDepthZ };
    const Mathematics::Axis3D Cb { Cx, Cy, -HalfDepthZ };
    for (std::size_t I = 0u; I < N; ++I)
    {
        const auto& A = Poly2D[I];
        const auto& B = Poly2D[(I + 1u) % N];
        const Mathematics::Axis3D Af { A.first, A.second,  HalfDepthZ };
        const Mathematics::Axis3D Bf { B.first, B.second,  HalfDepthZ };
        const Mathematics::Axis3D Ab { A.first, A.second, -HalfDepthZ };
        const Mathematics::Axis3D Bb { B.first, B.second, -HalfDepthZ };
        OutTris.push_back({ Cf, Af, Bf });
        OutTris.push_back({ Cb, Bb, Ab });
        AppendQuad(OutTris, Af, Ab, Bb, Bf);
    }
}

[[nodiscard]] SurfaceBackingGeometry FinalizeRawTriangles(
    std::span<const RawTriangle>      RawTris,
    Mathematics::WorldCoordinateBasis TargetBasis,
    bool                              AutoNormalize = true)
{
    SurfaceBackingGeometry Result {};
    if (RawTris.empty())
    {
        return Result;
    }

    float MinX = std::numeric_limits<float>::max();
    float MinY = std::numeric_limits<float>::max();
    float MinZ = std::numeric_limits<float>::max();
    float MaxX = std::numeric_limits<float>::lowest();
    float MaxY = std::numeric_limits<float>::lowest();
    float MaxZ = std::numeric_limits<float>::lowest();

    for (const auto& Tri : RawTris)
    {
        for (const auto& V : Tri)
        {
            const Mathematics::Axis3D P = ConvertYUpToBasis(V, TargetBasis);
            MinX = std::min(MinX, P.X); MaxX = std::max(MaxX, P.X);
            MinY = std::min(MinY, P.Y); MaxY = std::max(MaxY, P.Y);
            MinZ = std::min(MinZ, P.Z); MaxZ = std::max(MaxZ, P.Z);
        }
    }

    const float Cx       = (MinX + MaxX) * 0.5f;
    const float Cy       = (MinY + MaxY) * 0.5f;
    const float Cz       = (MinZ + MaxZ) * 0.5f;
    const float MaxSpan  = std::max({ 1.0e-5f, MaxX - MinX, MaxY - MinY, MaxZ - MinZ });
    const float InvScale = AutoNormalize ? (2.0f / MaxSpan) : 1.0f;

    Result.Triangles.reserve(RawTris.size());
    for (const auto& Raw : RawTris)
    {
        const Mathematics::Axis3D Pa = ConvertYUpToBasis(Raw[0], TargetBasis);
        const Mathematics::Axis3D Pb = ConvertYUpToBasis(Raw[1], TargetBasis);
        const Mathematics::Axis3D Pc = ConvertYUpToBasis(Raw[2], TargetBasis);
        const Mathematics::Axis3D A { (Pa.X - Cx) * InvScale, (Pa.Y - Cy) * InvScale, (Pa.Z - Cz) * InvScale };
        const Mathematics::Axis3D B { (Pb.X - Cx) * InvScale, (Pb.Y - Cy) * InvScale, (Pb.Z - Cz) * InvScale };
        const Mathematics::Axis3D C { (Pc.X - Cx) * InvScale, (Pc.Y - Cy) * InvScale, (Pc.Z - Cz) * InvScale };
        const Mathematics::Axis3D E1    = B - A;
        const Mathematics::Axis3D E2    = C - A;
        const Mathematics::Axis3D E3    = C - B;
        const Mathematics::Axis3D Cross = Mathematics::EvaluateCross(E1, E2);
        const float               Len   = Mathematics::EvaluateLength(Cross);
        const float               Area  = 0.5f * Len;
        if (Area < 1.0e-7f)
        {
            continue;
        }
        const Mathematics::Axis3D Normal = Cross * (1.0f / Len);
        const float Perim = Mathematics::EvaluateLength(E1) + Mathematics::EvaluateLength(E2) + Mathematics::EvaluateLength(E3);
        const float EdgeW = Area * (0.4f + 0.6f * std::min(3.0f, Perim / std::max(0.05f, std::sqrt(Area) * 4.0f)));
        Result.TotalArea += Area;
        Result.Triangles.push_back({ A, B, C, Normal, Area, EdgeW });
    }

    Result.BoundsMin = { (MinX - Cx) * InvScale, (MinY - Cy) * InvScale, (MinZ - Cz) * InvScale };
    Result.BoundsMax = { (MaxX - Cx) * InvScale, (MaxY - Cy) * InvScale, (MaxZ - Cz) * InvScale };
    return Result;
}

void BuildUtahTeapotRaw(std::vector<RawTriangle>& Tris)
{
    static constexpr std::array<std::pair<float, float>, 15u> Body = {{
        { 0.0f, -0.58f }, { 0.46f, -0.58f }, { 0.58f, -0.45f }, { 0.76f, -0.18f }, { 0.78f,  0.08f },
        { 0.64f, 0.34f }, { 0.46f,  0.48f }, { 0.50f,  0.54f }, { 0.38f,  0.55f }, { 0.36f,  0.62f },
        { 0.14f, 0.69f }, { 0.06f,  0.72f }, { 0.11f,  0.80f }, { 0.08f,  0.87f }, { 0.00f,  0.89f },
    }};
    AppendLathe(Tris, Body, 26u);
    AppendTubeAlongCurve(
        Tris,
        [](float U) noexcept -> Mathematics::Axis3D {
            const float X = 0.64f + U * 0.58f + std::sin(U * Constants::PiFloat) * 0.08f;
            const float Y = -0.22f + U * 0.78f - std::sin(U * Constants::PiFloat * 1.15f) * 0.14f;
            return { X, Y, 0.0f };
        },
        14u,
        10u,
        [](float U) noexcept {
            const float R = 0.16f * (1.0f - U * 0.56f);
            return std::pair<float, float>{ R, R };
        });
    AppendTubeAlongCurve(
        Tris,
        [](float U) noexcept -> Mathematics::Axis3D {
            const float A = -Constants::PiFloat * 0.52f + U * Constants::PiFloat * 1.18f;
            return { -0.64f - std::cos(A) * 0.44f, 0.06f + std::sin(A) * 0.42f, 0.0f };
        },
        16u,
        10u,
        [](float) noexcept { return std::pair<float, float>{ 0.072f, 0.052f }; });
}

void BuildSportsCarRaw(std::vector<RawTriangle>& Tris)
{
    struct Station { float X, YBot, YTop, YCab, WBot, WTop, WCab; };
    static constexpr std::array<Station, 7u> Stations = {{
        { -1.04f, -0.20f, 0.16f, 0.18f, 0.44f, 0.40f, 0.34f },
        { -0.82f, -0.22f, 0.20f, 0.28f, 0.49f, 0.44f, 0.34f },
        { -0.45f, -0.22f, 0.21f, 0.44f, 0.48f, 0.44f, 0.32f },
        {  0.02f, -0.22f, 0.20f, 0.45f, 0.47f, 0.43f, 0.31f },
        {  0.42f, -0.22f, 0.18f, 0.22f, 0.48f, 0.43f, 0.34f },
        {  0.80f, -0.21f, 0.12f, 0.13f, 0.47f, 0.41f, 0.34f },
        {  1.06f, -0.18f, 0.02f, 0.03f, 0.40f, 0.35f, 0.28f },
    }};
    std::array<std::array<Mathematics::Axis3D, 6u>, 7u> Rings {};
    for (std::size_t I = 0u; I < Stations.size(); ++I)
    {
        const auto& S = Stations[I];
        Rings[I] = {{
            { S.X, S.YBot, -S.WBot }, { S.X, S.YTop, -S.WTop }, { S.X, S.YCab, -S.WCab },
            { S.X, S.YCab,  S.WCab }, { S.X, S.YTop,  S.WTop }, { S.X, S.YBot,  S.WBot },
        }};
    }
    for (std::size_t I = 0u; I + 1u < Rings.size(); ++I)
    {
        for (std::size_t K = 0u; K < 6u; ++K)
        {
            const std::size_t Kn = (K + 1u) % 6u;
            AppendQuad(Tris, Rings[I][K], Rings[I][Kn], Rings[I + 1u][Kn], Rings[I + 1u][K]);
        }
    }
    AppendBox(Tris, -0.96f, 0.30f,  0.00f, 0.11f, 0.025f, 0.44f);
    AppendBox(Tris, -0.94f, 0.22f, -0.24f, 0.04f, 0.070f, 0.02f);
    AppendBox(Tris, -0.94f, 0.22f,  0.24f, 0.04f, 0.070f, 0.02f);

    static constexpr std::array<std::pair<float, float>, 4u> Wheels = {{
        { -0.64f, -0.46f }, { -0.64f, 0.46f }, { 0.64f, -0.46f }, { 0.64f, 0.46f },
    }};
    for (const auto& [Wx, Wz] : Wheels)
    {
        constexpr uint32_t Segs = 14u;
        constexpr float    Wr   = 0.20f;
        constexpr float    Wh   = 0.08f;
        for (uint32_t S = 0u; S < Segs; ++S)
        {
            const float A0 = (static_cast<float>(S) / static_cast<float>(Segs)) * Constants::TauFloat;
            const float A1 = (static_cast<float>(S + 1u) / static_cast<float>(Segs)) * Constants::TauFloat;
            const Mathematics::Axis3D P0i { Wx + std::cos(A0) * Wr, -0.18f + std::sin(A0) * Wr, Wz - Wh };
            const Mathematics::Axis3D P1i { Wx + std::cos(A1) * Wr, -0.18f + std::sin(A1) * Wr, Wz - Wh };
            const Mathematics::Axis3D P0o { Wx + std::cos(A0) * Wr, -0.18f + std::sin(A0) * Wr, Wz + Wh };
            const Mathematics::Axis3D P1o { Wx + std::cos(A1) * Wr, -0.18f + std::sin(A1) * Wr, Wz + Wh };
            AppendQuad(Tris, P0i, P0o, P1o, P1i);
            Tris.push_back(RawTriangle{{ { Wx, -0.18f, Wz + Wh }, P0o, P1o }});
            Tris.push_back(RawTriangle{{ { Wx, -0.18f, Wz - Wh }, P1i, P0i }});
        }
    }
}

void BuildSculptedBustRaw(std::vector<RawTriangle>& Tris)
{
    constexpr uint32_t Rings = 22u;
    constexpr uint32_t Segs  = 26u;
    std::vector<std::vector<Mathematics::Axis3D>> Grid(Rings + 1u, std::vector<Mathematics::Axis3D>(Segs));
    for (uint32_t R = 0u; R <= Rings; ++R)
    {
        const float V = static_cast<float>(R) / static_cast<float>(Rings);
        const float Y = -0.95f + V * 1.92f;
        for (uint32_t S = 0u; S < Segs; ++S)
        {
            const float Phi = (static_cast<float>(S) / static_cast<float>(Segs)) * Constants::TauFloat;
            const float C   = std::cos(Phi);
            const float Sn  = std::sin(Phi);
            float Rx = 0.42f;
            float Rz = 0.42f;
            if (V < 0.24f)
            {
                const float T = V / 0.24f;
                Rx = 0.78f - T * 0.46f;
                Rz = 0.38f - T * 0.12f;
            }
            else if (V < 0.38f)
            {
                const float T = (V - 0.24f) / 0.14f;
                Rx = 0.25f + T * 0.04f;
                Rz = 0.24f + T * 0.05f;
            }
            else
            {
                const float H       = (V - 0.38f) / 0.62f;
                const float Profile = std::sin(std::pow(H, 0.85f) * Constants::PiFloat);
                Rx = 0.46f * Profile * (H < 0.32f ? (0.78f + H * 0.7f) : 1.0f);
                Rz = 0.54f * Profile;
                if (Sn > 0.65f && H > 0.26f && H < 0.56f)
                {
                    const float NoseW = std::pow(std::max(0.0f, (Sn - 0.65f) / 0.35f), 2.2f);
                    const float NoseH = std::sin(((H - 0.26f) / 0.30f) * Constants::PiFloat);
                    Rz += 0.12f * NoseW * NoseH;
                }
                if (Sn > 0.35f && H > 0.46f && H < 0.62f)
                {
                    const float Brow = std::sin(((H - 0.46f) / 0.16f) * Constants::PiFloat);
                    Rz += 0.045f * Brow * std::max(0.0f, Sn - 0.35f);
                }
                if (Sn > 0.40f && H < 0.18f)
                {
                    Rz += 0.06f * (1.0f - H / 0.18f) * Sn;
                }
            }
            Grid[R][S] = { C * Rx, Y, Sn * Rz };
        }
    }
    for (uint32_t R = 0u; R < Rings; ++R)
    {
        for (uint32_t S = 0u; S < Segs; ++S)
        {
            const uint32_t Sn = (S + 1u) % Segs;
            AppendQuad(Tris, Grid[R][S], Grid[R + 1u][S], Grid[R + 1u][Sn], Grid[R][Sn]);
        }
    }
    const Mathematics::Axis3D BaseC { 0.0f, -0.95f, 0.0f };
    for (uint32_t S = 0u; S < Segs; ++S)
    {
        Tris.push_back({ BaseC, Grid[0][(S + 1u) % Segs], Grid[0][S] });
    }
}

void BuildTorusKnotRaw(std::vector<RawTriangle>& Tris)
{
    AppendTubeAlongCurve(
        Tris,
        [](float U) noexcept -> Mathematics::Axis3D {
            const float T = U * Constants::TauFloat;
            const float R = 0.62f + 0.26f * std::cos(3.0f * T);
            return { R * std::cos(2.0f * T), 0.32f * std::sin(3.0f * T), R * std::sin(2.0f * T) };
        },
        64u,
        12u,
        [](float) noexcept { return std::pair<float, float>{ 0.165f, 0.165f }; });
}

void BuildGearRaw(std::vector<RawTriangle>& Tris)
{
    std::vector<std::pair<float, float>> Poly {};
    constexpr uint32_t Teeth = 12u;
    const float Step = Constants::TauFloat / static_cast<float>(Teeth);
    for (uint32_t I = 0u; I < Teeth; ++I)
    {
        const float ToothOrigin = static_cast<float>(I) * Step;
        const std::array<std::pair<float, float>, 4u> Pts = {{
            { 0.68f, ToothOrigin + Step * 0.06f },
            { 0.96f, ToothOrigin + Step * 0.18f },
            { 0.96f, ToothOrigin + Step * 0.42f },
            { 0.68f, ToothOrigin + Step * 0.54f },
        }};
        for (const auto& [R, A] : Pts)
        {
            Poly.emplace_back(std::cos(A) * R, std::sin(A) * R);
        }
    }
    AppendExtrudedPolygon(Tris, Poly, 0.20f);

    std::vector<std::pair<float, float>> Hub {};
    for (uint32_t I = 0u; I < 16u; ++I)
    {
        const float A = (static_cast<float>(I) / 16.0f) * Constants::TauFloat;
        Hub.emplace_back(std::cos(A) * 0.34f, std::sin(A) * 0.34f);
    }
    AppendExtrudedPolygon(Tris, Hub, 0.32f);
}

void BuildCrystalClusterRaw(std::vector<RawTriangle>& Tris)
{
    struct Shard { float Ox, Oz, TiltX, TiltZ, R, H; };
    static constexpr std::array<Shard, 5u> Shards = {{
        {  0.00f,  0.00f,  0.04f, -0.02f, 0.30f, 1.55f },
        { -0.34f,  0.16f, -0.34f,  0.16f, 0.22f, 1.15f },
        {  0.36f, -0.14f,  0.32f, -0.18f, 0.24f, 1.22f },
        {  0.12f,  0.36f,  0.12f,  0.36f, 0.19f, 0.96f },
        { -0.22f, -0.32f, -0.22f, -0.32f, 0.18f, 0.92f },
    }};
    for (const auto& Sh : Shards)
    {
        constexpr uint32_t Sides = 6u;
        constexpr float    BaseY = -0.65f;
        const float        TopY  = BaseY + Sh.H * 0.72f;
        const float        TipY  = BaseY + Sh.H;
        std::array<Mathematics::Axis3D, Sides> Bot {};
        std::array<Mathematics::Axis3D, Sides> Top {};
        for (uint32_t S = 0u; S < Sides; ++S)
        {
            const float A  = (static_cast<float>(S) / static_cast<float>(Sides)) * Constants::TauFloat;
            const float Lx = std::cos(A) * Sh.R;
            const float Lz = std::sin(A) * Sh.R;
            Bot[S] = { Sh.Ox + Lx, BaseY, Sh.Oz + Lz };
            Top[S] = { Sh.Ox + Lx * 0.84f + Sh.TiltX * Sh.H * 0.72f, TopY, Sh.Oz + Lz * 0.84f + Sh.TiltZ * Sh.H * 0.72f };
        }
        const Mathematics::Axis3D Tip  { Sh.Ox + Sh.TiltX * Sh.H, TipY, Sh.Oz + Sh.TiltZ * Sh.H };
        const Mathematics::Axis3D BotC { Sh.Ox, BaseY, Sh.Oz };
        for (uint32_t S = 0u; S < Sides; ++S)
        {
            const uint32_t Sn = (S + 1u) % Sides;
            AppendQuad(Tris, Bot[S], Top[S], Top[Sn], Bot[Sn]);
            Tris.push_back({ Top[S], Tip, Top[Sn] });
            Tris.push_back({ BotC, Bot[Sn], Bot[S] });
        }
    }
}

void BuildArchwayRaw(std::vector<RawTriangle>& Tris)
{
    AppendBox(Tris, -0.62f, -0.35f, 0.0f, 0.22f, 0.65f, 0.28f);
    AppendBox(Tris,  0.62f, -0.35f, 0.0f, 0.22f, 0.65f, 0.28f);
    AppendBox(Tris, -0.62f, -0.94f, 0.0f, 0.28f, 0.08f, 0.34f);
    AppendBox(Tris,  0.62f, -0.94f, 0.0f, 0.28f, 0.08f, 0.34f);
    constexpr uint32_t Segs = 16u;
    constexpr float    Ri   = 0.40f;
    constexpr float    Ro   = 0.84f;
    constexpr float    Hz   = 0.28f;
    for (uint32_t I = 0u; I < Segs; ++I)
    {
        const float A0 = (static_cast<float>(I) / static_cast<float>(Segs)) * Constants::PiFloat;
        const float A1 = (static_cast<float>(I + 1u) / static_cast<float>(Segs)) * Constants::PiFloat;
        const Mathematics::Axis3D Pi0f { std::cos(A0) * Ri, 0.30f + std::sin(A0) * Ri,  Hz };
        const Mathematics::Axis3D Pi1f { std::cos(A1) * Ri, 0.30f + std::sin(A1) * Ri,  Hz };
        const Mathematics::Axis3D Po0f { std::cos(A0) * Ro, 0.30f + std::sin(A0) * Ro,  Hz };
        const Mathematics::Axis3D Po1f { std::cos(A1) * Ro, 0.30f + std::sin(A1) * Ro,  Hz };
        const Mathematics::Axis3D Pi0b { Pi0f.X, Pi0f.Y, -Hz };
        const Mathematics::Axis3D Pi1b { Pi1f.X, Pi1f.Y, -Hz };
        const Mathematics::Axis3D Po0b { Po0f.X, Po0f.Y, -Hz };
        const Mathematics::Axis3D Po1b { Po1f.X, Po1f.Y, -Hz };
        AppendQuad(Tris, Pi0f, Po0f, Po1f, Pi1f);
        AppendQuad(Tris, Po0b, Pi0b, Pi1b, Po1b);
        AppendQuad(Tris, Po0f, Po0b, Po1b, Po1f);
        AppendQuad(Tris, Pi0b, Pi0f, Pi1f, Pi1b);
    }
}

void BuildBevelCubeRaw(std::vector<RawTriangle>& Tris)
{
    constexpr uint32_t Sub = 6u;
    struct FaceSpec { Mathematics::Axis3D N, U, V; };
    static constexpr std::array<FaceSpec, 6u> Faces = {{
        { {  1.0f,  0.0f,  0.0f }, {  0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f,  1.0f } },
        { { -1.0f,  0.0f,  0.0f }, {  0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } },
        { {  0.0f,  1.0f,  0.0f }, {  1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } },
        { {  0.0f, -1.0f,  0.0f }, {  1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f,  1.0f } },
        { {  0.0f,  0.0f,  1.0f }, {  1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f,  0.0f } },
        { {  0.0f,  0.0f, -1.0f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f,  0.0f } },
    }};
    auto Bevel = [](float X, float Y, float Z) noexcept -> Mathematics::Axis3D {
        constexpr float P = 4.5f;
        const float Den = std::pow(
            std::pow(std::abs(X), P) + std::pow(std::abs(Y), P) + std::pow(std::abs(Z), P),
            1.0f / P);
        const float SafeDen = (Den > 1.0e-5f) ? Den : 1.0f;
        return { X / SafeDen, Y / SafeDen, Z / SafeDen };
    };
    for (const auto& F : Faces)
    {
        std::array<std::array<Mathematics::Axis3D, Sub + 1u>, Sub + 1u> Grid {};
        for (uint32_t I = 0u; I <= Sub; ++I)
        {
            const float Tu = (static_cast<float>(I) / static_cast<float>(Sub)) * 2.0f - 1.0f;
            for (uint32_t J = 0u; J <= Sub; ++J)
            {
                const float Tv = (static_cast<float>(J) / static_cast<float>(Sub)) * 2.0f - 1.0f;
                Grid[I][J] = Bevel(
                    F.N.X + F.U.X * Tu + F.V.X * Tv,
                    F.N.Y + F.U.Y * Tu + F.V.Y * Tv,
                    F.N.Z + F.U.Z * Tu + F.V.Z * Tv);
            }
        }
        for (uint32_t I = 0u; I < Sub; ++I)
        {
            for (uint32_t J = 0u; J < Sub; ++J)
            {
                AppendQuad(Tris, Grid[I][J], Grid[I + 1u][J], Grid[I + 1u][J + 1u], Grid[I][J + 1u]);
            }
        }
    }
}

void BuildConeSpireRaw(std::vector<RawTriangle>& Tris)
{
    constexpr uint32_t Segs  = 20u;
    constexpr uint32_t Rings = 6u;
    const Mathematics::Axis3D Tip   { 0.0f,  1.00f, 0.0f };
    const Mathematics::Axis3D BaseC { 0.0f, -0.85f, 0.0f };
    std::array<std::array<Mathematics::Axis3D, Segs>, Rings + 1u> Grid {};
    for (uint32_t R = 0u; R <= Rings; ++R)
    {
        const float T   = static_cast<float>(R) / static_cast<float>(Rings);
        const float Rad = (1.0f - T) * 0.85f;
        const float Y   = -0.85f + T * 1.85f;
        for (uint32_t S = 0u; S < Segs; ++S)
        {
            const float A = (static_cast<float>(S) / static_cast<float>(Segs)) * Constants::TauFloat;
            Grid[R][S] = { std::cos(A) * Rad, Y, std::sin(A) * Rad };
        }
    }
    for (uint32_t R = 0u; R + 1u < Rings; ++R)
    {
        for (uint32_t S = 0u; S < Segs; ++S)
        {
            const uint32_t Sn = (S + 1u) % Segs;
            AppendQuad(Tris, Grid[R][S], Grid[R + 1u][S], Grid[R + 1u][Sn], Grid[R][Sn]);
        }
    }
    for (uint32_t S = 0u; S < Segs; ++S)
    {
        const uint32_t Sn = (S + 1u) % Segs;
        Tris.push_back({ Grid[Rings - 1u][S], Tip, Grid[Rings - 1u][Sn] });
        Tris.push_back({ BaseC, Grid[0][Sn], Grid[0][S] });
    }
}

void BuildStarPolyhedronRaw(std::vector<RawTriangle>& Tris)
{
    static constexpr std::array<Mathematics::Axis3D, 6u> Core = {{
        {  0.52f,  0.00f,  0.00f }, { -0.52f,  0.00f,  0.00f },
        {  0.00f,  0.52f,  0.00f }, {  0.00f, -0.52f,  0.00f },
        {  0.00f,  0.00f,  0.52f }, {  0.00f,  0.00f, -0.52f },
    }};
    static constexpr std::array<std::array<uint32_t, 3u>, 8u> OctFaces = {{
        { 0u, 2u, 4u }, { 4u, 2u, 1u }, { 1u, 2u, 5u }, { 5u, 2u, 0u },
        { 4u, 3u, 0u }, { 1u, 3u, 4u }, { 5u, 3u, 1u }, { 0u, 3u, 5u },
    }};
    for (const auto& F : OctFaces)
    {
        const Mathematics::Axis3D& A = Core[F[0]];
        const Mathematics::Axis3D& B = Core[F[1]];
        const Mathematics::Axis3D& C = Core[F[2]];
        const Mathematics::Axis3D Mid { (A.X + B.X + C.X) / 3.0f, (A.Y + B.Y + C.Y) / 3.0f, (A.Z + B.Z + C.Z) / 3.0f };
        const Mathematics::Axis3D N   = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(B - A, C - A), { 0.0f, 1.0f, 0.0f });
        const Mathematics::Axis3D Apex { Mid.X + N.X * 0.85f, Mid.Y + N.Y * 0.85f, Mid.Z + N.Z * 0.85f };
        Tris.push_back({ A, B, Apex });
        Tris.push_back({ B, C, Apex });
        Tris.push_back({ C, A, Apex });
    }
}

void BuildSphereRaw(std::vector<RawTriangle>& Tris)
{
    constexpr uint32_t Rings = 16u;
    constexpr uint32_t Segs  = 24u;
    std::vector<std::vector<Mathematics::Axis3D>> Grid(Rings + 1u, std::vector<Mathematics::Axis3D>(Segs));
    for (uint32_t R = 0u; R <= Rings; ++R)
    {
        const float Phi = (static_cast<float>(R) / static_cast<float>(Rings)) * Constants::PiFloat;
        const float Y   = std::cos(Phi);
        const float Rad = std::sin(Phi);
        for (uint32_t S = 0u; S < Segs; ++S)
        {
            const float Theta = (static_cast<float>(S) / static_cast<float>(Segs)) * Constants::TauFloat;
            Grid[R][S] = { std::cos(Theta) * Rad, Y, std::sin(Theta) * Rad };
        }
    }
    for (uint32_t R = 0u; R < Rings; ++R)
    {
        for (uint32_t S = 0u; S < Segs; ++S)
        {
            const uint32_t Sn = (S + 1u) % Segs;
            AppendQuad(Tris, Grid[R][S], Grid[R + 1u][S], Grid[R + 1u][Sn], Grid[R][Sn]);
        }
    }
}

void BuildTorusRaw(std::vector<RawTriangle>& Tris)
{
    AppendTubeAlongCurve(
        Tris,
        [](float U) noexcept -> Mathematics::Axis3D {
            const float A = U * Constants::TauFloat;
            return { 0.72f * std::cos(A), 0.0f, 0.72f * std::sin(A) };
        },
        32u,
        14u,
        [](float) noexcept { return std::pair<float, float>{ 0.28f, 0.28f }; });
}

void BuildHandAndOrbRaw(std::vector<RawTriangle>& Tris)
{
    // Sculpted palm + 5 curved digits cradling a floating orb above the palm
    AppendBox(Tris, 0.0f, -0.35f, 0.0f, 0.42f, 0.14f, 0.36f);
    for (uint32_t Finger = 0u; Finger < 4u; ++Finger)
    {
        const float XOff = -0.30f + static_cast<float>(Finger) * 0.20f;
        AppendTubeAlongCurve(
            Tris,
            [XOff](float U) noexcept -> Mathematics::Axis3D {
                const float Angle = 0.2f + U * 1.15f;
                return { XOff, -0.25f + std::sin(Angle) * 0.58f, 0.30f + std::cos(Angle) * 0.22f - 0.18f };
            },
            8u,
            8u,
            [](float U) noexcept {
                const float R = 0.068f * (1.0f - 0.3f * U);
                return std::pair<float, float>{ R, R };
            });
    }
    AppendTubeAlongCurve(
        Tris,
        [](float U) noexcept -> Mathematics::Axis3D {
            return { -0.44f - U * 0.20f, -0.25f + U * 0.40f, -0.15f + U * 0.22f };
        },
        6u,
        8u,
        [](float) noexcept { return std::pair<float, float>{ 0.072f, 0.072f }; });
    // Floating chrome orb above palm
    std::vector<RawTriangle> Orb {};
    BuildSphereRaw(Orb);
    for (auto& Tri : Orb)
    {
        for (auto& V : Tri)
        {
            V = { V.X * 0.34f, V.Y * 0.34f + 0.38f, V.Z * 0.34f };
        }
        Tris.push_back(Tri);
    }
}

void BuildPlaneQuadRaw(std::vector<RawTriangle>& Tris, bool WallXY)
{
    if (WallXY)
    {
        AppendQuad(Tris, { -1.0f, -1.0f, 0.0f }, { 1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f }, { -1.0f, 1.0f, 0.0f });
    }
    else
    {
        AppendQuad(Tris, { -1.0f, 0.0f, -1.0f }, { -1.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, -1.0f });
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                 PUBLIC SURFACE SAMPLER
//------------------------------------------------------------------------------------------------------------------------

Mathematics::Axis3D RotateEulerXyzDegrees(
    Mathematics::Axis3D Point,
    Mathematics::Axis3D EulerDegrees) noexcept
{
    const float Rx = EulerDegrees.X * Constants::DegToRadFloat;
    const float Ry = EulerDegrees.Y * Constants::DegToRadFloat;
    const float Rz = EulerDegrees.Z * Constants::DegToRadFloat;

    float X = Point.X;
    float Y = Point.Y;
    float Z = Point.Z;

    if (std::abs(Rx) > 1.0e-6f)
    {
        const float C = std::cos(Rx);
        const float S = std::sin(Rx);
        const float Ny = Y * C - Z * S;
        const float Nz = Y * S + Z * C;
        Y = Ny;
        Z = Nz;
    }
    if (std::abs(Ry) > 1.0e-6f)
    {
        const float C = std::cos(Ry);
        const float S = std::sin(Ry);
        const float Nx = X * C + Z * S;
        const float Nz = -X * S + Z * C;
        X = Nx;
        Z = Nz;
    }
    if (std::abs(Rz) > 1.0e-6f)
    {
        const float C = std::cos(Rz);
        const float S = std::sin(Rz);
        const float Nx = X * C - Y * S;
        const float Ny = X * S + Y * C;
        X = Nx;
        Y = Ny;
    }
    return { X, Y, Z };
}

SurfaceBackingGeometry ConstructProceduralSurfaceGeometry(
    SurfacePresetCategory             Preset,
    Mathematics::WorldCoordinateBasis TargetBasis)
{
    std::vector<RawTriangle> RawTris {};
    switch (Preset)
    {
    case SurfacePresetCategory::FloorMatrixXZ:           BuildPlaneQuadRaw(RawTris, false); break;
    case SurfacePresetCategory::WallMatrixXY:            BuildPlaneQuadRaw(RawTris, true);  break;
    case SurfacePresetCategory::ObsidianSphere:          BuildSphereRaw(RawTris);           break;
    case SurfacePresetCategory::ObsidianHandChromeBall:  BuildHandAndOrbRaw(RawTris);       break;
    case SurfacePresetCategory::TorusRing:               BuildTorusRaw(RawTris);            break;
    case SurfacePresetCategory::CubicVolume:             AppendBox(RawTris, 0.0f, 0.0f, 0.0f, 0.85f, 0.85f, 0.85f); break;
    case SurfacePresetCategory::UtahTeapot:              BuildUtahTeapotRaw(RawTris);       break;
    case SurfacePresetCategory::SportsCarSilhouette:     BuildSportsCarRaw(RawTris);        break;
    case SurfacePresetCategory::SculptedHeadBust:        BuildSculptedBustRaw(RawTris);     break;
    case SurfacePresetCategory::TorusKnot32:             BuildTorusKnotRaw(RawTris);        break;
    case SurfacePresetCategory::PrecisionGearCog:        BuildGearRaw(RawTris);             break;
    case SurfacePresetCategory::QuartzCrystalCluster:    BuildCrystalClusterRaw(RawTris);   break;
    case SurfacePresetCategory::ArchitecturalArchway:    BuildArchwayRaw(RawTris);          break;
    case SurfacePresetCategory::BevelledMonolithCube:    BuildBevelCubeRaw(RawTris);        break;
    case SurfacePresetCategory::FacetedSpireCone:        BuildConeSpireRaw(RawTris);        break;
    case SurfacePresetCategory::StellatedStarPolyhedron: BuildStarPolyhedronRaw(RawTris);   break;
    case SurfacePresetCategory::CustomPolygonNetwork:
    default:
        BuildUtahTeapotRaw(RawTris);
        break;
    }
    return FinalizeRawTriangles(RawTris, TargetBasis, true);
}

SurfaceBackingGeometry ExtractSurfaceGeometryFromPolygonNetwork(
    const GeometrySpace::PolygonNetwork& Network,
    Mathematics::WorldCoordinateBasis    TargetBasis)
{
    const auto& Vertices = Network.Vertices;
    const auto& Edges    = Network.Edges;
    const auto& Faces    = Network.Faces;
    std::vector<RawTriangle> RawTris {};
    RawTris.reserve(Faces.size() * 2u);

    for (const auto& FaceEntry : Faces)
    {
        if (FaceEntry.BoundaryLoopEdgeIndex >= Edges.size() || FaceEntry.CornerCount < 3u)
        {
            continue;
        }
        std::vector<uint32_t> LoopVerts;
        LoopVerts.reserve(FaceEntry.CornerCount);
        uint32_t EdgeIdx = FaceEntry.BoundaryLoopEdgeIndex;
        for (uint16_t Step = 0u; Step < FaceEntry.CornerCount && EdgeIdx < Edges.size(); ++Step)
        {
            LoopVerts.push_back(Edges[EdgeIdx].OriginVertexIndex);
            EdgeIdx = Edges[EdgeIdx].NextEdgeIndex;
            if (EdgeIdx == FaceEntry.BoundaryLoopEdgeIndex)
            {
                break;
            }
        }
        if (LoopVerts.size() < 3u || LoopVerts[0] >= Vertices.size())
        {
            continue;
        }
        const auto& P0 = Vertices[LoopVerts[0]].Axes;
        for (std::size_t K = 1u; K + 1u < LoopVerts.size(); ++K)
        {
            const uint32_t I1 = LoopVerts[K];
            const uint32_t I2 = LoopVerts[K + 1u];
            if (I1 < Vertices.size() && I2 < Vertices.size())
            {
                const auto& P1 = Vertices[I1].Axes;
                const auto& P2 = Vertices[I2].Axes;
                // PolygonNetwork is authored in Frontier +Z Up coordinates; convert back to YUp so FinalizeRawTriangles normalizes uniformly
                RawTris.push_back(RawTriangle{{
                    Mathematics::ConvertVectorBetweenBases(P0, Mathematics::WorldCoordinateBasis::RightHandedZUp, Mathematics::WorldCoordinateBasis::RightHandedYUp),
                    Mathematics::ConvertVectorBetweenBases(P1, Mathematics::WorldCoordinateBasis::RightHandedZUp, Mathematics::WorldCoordinateBasis::RightHandedYUp),
                    Mathematics::ConvertVectorBetweenBases(P2, Mathematics::WorldCoordinateBasis::RightHandedZUp, Mathematics::WorldCoordinateBasis::RightHandedYUp),
                }});
            }
        }
    }

    if (RawTris.empty())
    {
        return ConstructProceduralSurfaceGeometry(SurfacePresetCategory::UtahTeapot, TargetBasis);
    }
    return FinalizeRawTriangles(RawTris, TargetBasis, true);
}

SurfaceAnchorSet SampleSurfaceAnchors(
    const SurfaceAnchorOriginSpec&       Spec,
    uint32_t                             AnchorCapacity,
    const GeometrySpace::PolygonNetwork* OptionalCustomNetwork,
    Mathematics::WorldCoordinateBasis    TargetBasis)
{
    SurfaceAnchorSet Output {};
    if (Spec.SurfacePreset == SurfacePresetCategory::CustomPolygonNetwork && OptionalCustomNetwork != nullptr)
    {
        Output.BackingGeometry = ExtractSurfaceGeometryFromPolygonNetwork(*OptionalCustomNetwork, TargetBasis);
    }
    else
    {
        Output.BackingGeometry = ConstructProceduralSurfaceGeometry(Spec.SurfacePreset, TargetBasis);
    }

    const auto& Tris = Output.BackingGeometry.Triangles;
    const uint32_t Cap = std::max(1u, AnchorCapacity);
    if (Tris.empty())
    {
        return Output;
    }

    std::vector<double> Cdf(Tris.size(), 0.0);
    double TotalWeight = 0.0;
    for (std::size_t I = 0u; I < Tris.size(); ++I)
    {
        const double Weight = (Spec.SampleMode == SurfaceSampleMode::RidgeCreaseAccent)
                                  ? static_cast<double>(Tris[I].EdgeWeight)
                                  : static_cast<double>(Tris[I].Area);
        TotalWeight += Weight;
        Cdf[I] = TotalWeight;
    }
    const double InvTotal = (TotalWeight > 1.0e-9) ? (1.0 / TotalWeight) : 1.0;
    for (double& Entry : Cdf)
    {
        Entry *= InvTotal;
    }

    const float Pitch = std::max(
        0.03f,
        std::sqrt(std::max(0.2f, Output.BackingGeometry.TotalArea) / static_cast<float>(std::max(16u, Cap))) * 1.15f);

    Output.Anchors.resize(Cap);
    Output.PackedSnapSlots.resize(Cap);

    for (uint32_t I = 0u; I < Cap; ++I)
    {
        const double TargetU = (static_cast<double>(I) + static_cast<double>(HashOriginScalar(I, 11u)) * 0.92) / static_cast<double>(Cap);
        std::size_t Lo = 0u;
        std::size_t Hi = Tris.size() - 1u;
        while (Lo < Hi)
        {
            const std::size_t Mid = (Lo + Hi) >> 1u;
            if (Cdf[Mid] < TargetU)
            {
                Lo = Mid + 1u;
            }
            else
            {
                Hi = Mid;
            }
        }
        const auto& Tri = Tris[Lo];
        const float R1  = std::sqrt(HashOriginScalar(I, 23u));
        const float R2  = HashOriginScalar(I, 37u);
        const float Wa  = 1.0f - R1;
        const float Wb  = R1 * (1.0f - R2);
        const float Wc  = R1 * R2;

        Mathematics::Axis3D LocalPos {
            Tri.VertexA.X * Wa + Tri.VertexB.X * Wb + Tri.VertexC.X * Wc,
            Tri.VertexA.Y * Wa + Tri.VertexB.Y * Wb + Tri.VertexC.Y * Wc,
            Tri.VertexA.Z * Wa + Tri.VertexB.Z * Wb + Tri.VertexC.Z * Wc,
        };
        if (Spec.SampleMode == SurfaceSampleMode::SurfaceInteriorVolume && (I % 3u) == 0u)
        {
            const float Depth = 0.25f + 0.68f * std::cbrt(HashOriginScalar(I, 53u));
            LocalPos = LocalPos * Depth;
        }

        const Mathematics::Axis3D RotPos  = RotateEulerXyzDegrees(LocalPos, Spec.EulerRotationDeg) * Spec.ScaleMeters;
        const Mathematics::Axis3D RotNorm = Mathematics::NormalizeAxis(
            RotateEulerXyzDegrees(Tri.Normal, Spec.EulerRotationDeg),
            { 0.0f, 0.0f, 1.0f });
        const float LocalElev = (TargetBasis == Mathematics::WorldCoordinateBasis::RightHandedYUp) ? RotPos.Y : RotPos.Z;
        const Mathematics::Axis3D WorldPos = Spec.OriginMeters + RotPos;

        Output.Anchors[I] = { WorldPos, RotNorm, LocalElev, Pitch };

        ParticleSlot& Snap = Output.PackedSnapSlots[I];
        Snap.PositionAge[0]      = WorldPos.X;
        Snap.PositionAge[1]      = WorldPos.Y;
        Snap.PositionAge[2]      = WorldPos.Z;
        Snap.PositionAge[3]      = LocalElev;
        Snap.PhaseTiming[0]      = RotNorm.X;
        Snap.PhaseTiming[1]      = RotNorm.Y;
        Snap.PhaseTiming[2]      = RotNorm.Z;
        Snap.PhaseTiming[3]      = Pitch;
    }

    return Output;
}

} // namespace Frontier::ParticleSpace
