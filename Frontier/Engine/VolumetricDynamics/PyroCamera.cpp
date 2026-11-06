/*==========================================================================================================================================
                                                            PYROCAMERA.CPP
==========================================================================================================================================*/
// 🧩 The orbit camera of the Fluid volume (PyroCamera.h): Experimental/Fluid/src/camera.js and BoundsProjection.js, statement for statement.

#include "Frontier/Engine/VolumetricDynamics/PyroCamera.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{

constexpr float PiVal   = 3.14159265358979323846f;
constexpr float Deg2Rad = PiVal / 180.0f;

std::array<float, 3> Cross3(const std::array<float, 3>& A, const std::array<float, 3>& B) noexcept
{
    return {
        A[1] * B[2] - A[2] * B[1],
        A[2] * B[0] - A[0] * B[2],
        A[0] * B[1] - A[1] * B[0],
    };
}

// [unit] - camera.js: a vector over its length, or over 1 when it has none (Math.hypot(...) || 1)
std::array<float, 3> Unit3(const std::array<float, 3>& V) noexcept
{
    float L = std::sqrt(V[0] * V[0] + V[1] * V[1] + V[2] * V[2]);
    if (L == 0.0f) L = 1.0f;
    return { V[0] / L, V[1] / L, V[2] / L };
}

} // namespace

//----------------------------------------------------------------------------------------------------------------------
//                                                PYRO ORBIT CAMERA
//----------------------------------------------------------------------------------------------------------------------

void PyroOrbitCamera::Reset() noexcept
{
    TargetTheta    = 0.62f;
    TargetPhi      = 1.26f;
    TargetDistance = 3.35f;
    TargetCentre   = { 0.0f, 0.22f, 0.0f };
}

void PyroOrbitCamera::Snap() noexcept
{
    Theta    = TargetTheta;
    Phi      = TargetPhi;
    Distance = TargetDistance;
    Target   = TargetCentre;
}

void PyroOrbitCamera::FocusVolume(float Width, float Height) noexcept
{
    Reset();
    TargetDistance = std::min(9.5f, std::max(Width, Height) * 2.3f);
}

void PyroOrbitCamera::ApplyPresetView(int ViewOrdinal, float Width, float Height) noexcept
{
    if (ViewOrdinal <= 0 || ViewOrdinal > 3)
    {
        FocusVolume(Width, Height);
        return;
    }
    TargetTheta = ViewOrdinal == 2 ? PiVal / 2.0f : 0.0f;
    TargetPhi   = ViewOrdinal == 3 ? 0.03f : PiVal / 2.0f;
}

void PyroOrbitCamera::OrbitByPixels(float DeltaX, float DeltaY) noexcept
{
    TargetTheta -= DeltaX * 0.0075f;
    TargetPhi    = std::max(0.12f, std::min(PiVal - 0.08f, TargetPhi - DeltaY * 0.0075f));
}

void PyroOrbitCamera::PanByPixels(float DeltaX, float DeltaY) noexcept
{
    const PyroCameraBasis Basis = ComputeBasis();
    const float           Scale = TargetDistance * 0.0014f;
    for (size_t Axis = 0; Axis < 3; ++Axis)
    {
        TargetCentre[Axis] -= Basis.Right[Axis] * DeltaX * Scale;
        TargetCentre[Axis] += Basis.Up[Axis] * DeltaY * Scale;
    }
    TargetCentre[1] = std::max(-0.55f, std::min(2.2f, TargetCentre[1]));
}

void PyroOrbitCamera::ZoomByWheel(float DeltaPixels) noexcept
{
    TargetDistance = std::max(0.85f, std::min(9.5f, TargetDistance * (1.0f + DeltaPixels * 0.001f)));
}

void PyroOrbitCamera::AdvanceTurntable(float DtSeconds) noexcept
{
    TargetTheta += std::min(DtSeconds, 0.05f) * 0.35f;
}

void PyroOrbitCamera::Advance(float DtSeconds) noexcept
{
    const float Ease = std::min(1.0f, DtSeconds * 16.0f);
    Theta    += (TargetTheta - Theta) * Ease;
    Phi      += (TargetPhi - Phi) * Ease;
    Distance += (TargetDistance - Distance) * Ease;
    for (size_t Axis = 0; Axis < 3; ++Axis) Target[Axis] += (TargetCentre[Axis] - Target[Axis]) * Ease;
}

PyroCameraBasis PyroOrbitCamera::ComputeBasis(float AspectOverride) const noexcept
{
    PyroCameraBasis Basis{};
    Basis.Aspect     = (AspectOverride > 0.05f) ? AspectOverride : Aspect;
    Basis.FovRadians = FovDegrees * Deg2Rad;
    Basis.Near       = Near;

    const float SinPhi = std::sin(Phi);
    Basis.Position = {
        Target[0] + Distance * SinPhi * std::sin(Theta),
        Target[1] + Distance * std::cos(Phi),
        Target[2] + Distance * SinPhi * std::cos(Theta),
    };
    Basis.Forward = Unit3({ Target[0] - Basis.Position[0], Target[1] - Basis.Position[1], Target[2] - Basis.Position[2] });
    // [unit] - right = forward x worldUp, up = right x forward, each over its own length (camera.js update)
    Basis.Right = Unit3(Cross3(Basis.Forward, { 0.0f, 1.0f, 0.0f }));
    Basis.Up    = Cross3(Basis.Right, Basis.Forward);
    return Basis;
}

bool PyroOrbitCamera::RaycastVolumeUvw(float NdcX, float NdcY, const std::array<float, 3>& BoxMin, const std::array<float, 3>& BoxMax,
                                       std::array<float, 3>& OutUvw) const noexcept
{
    const PyroCameraBasis F       = ComputeBasis();
    const float           TanHalf = std::tan(F.FovRadians * 0.5f);
    const float           Px      = NdcX * F.Aspect * TanHalf;
    const float           Py      = NdcY * TanHalf;
    const std::array<float, 3> Dir = Unit3({ F.Forward[0] + Px * F.Right[0] + Py * F.Up[0], F.Forward[1] + Px * F.Right[1] + Py * F.Up[1],
                                             F.Forward[2] + Px * F.Right[2] + Py * F.Up[2] });

    float TMin = -INFINITY;
    float TMax = INFINITY;
    for (size_t Axis = 0; Axis < 3; ++Axis)
    {
        const float InverseDirection = 1.0f / (std::fabs(Dir[Axis]) < 1.0e-6f ? 1.0e-6f : Dir[Axis]);
        float       T0               = (BoxMin[Axis] - F.Position[Axis]) * InverseDirection;
        float       T1               = (BoxMax[Axis] - F.Position[Axis]) * InverseDirection;
        if (T0 > T1) std::swap(T0, T1);
        TMin = std::max(TMin, T0);
        TMax = std::min(TMax, T1);
    }
    if (TMax < std::max(TMin, 0.0f)) return false;

    const float TEnter = std::max(TMin, 0.0f);
    const float THit   = TEnter + (TMax - TEnter) * 0.45f;
    const std::array<float, 3> World = { F.Position[0] + Dir[0] * THit, F.Position[1] + Dir[1] * THit, F.Position[2] + Dir[2] * THit };
    OutUvw = {
        std::max(0.06f, std::min(0.94f, (World[0] - BoxMin[0]) / (BoxMax[0] - BoxMin[0]))),
        std::max(0.05f, std::min(0.94f, (World[1] - BoxMin[1]) / (BoxMax[1] - BoxMin[1]))),
        std::max(0.06f, std::min(0.94f, (World[2] - BoxMin[2]) / (BoxMax[2] - BoxMin[2]))),
    };
    return true;
}

//----------------------------------------------------------------------------------------------------------------------
//                                                  PROJECTED BOUNDS
//----------------------------------------------------------------------------------------------------------------------

void ProjectBounds(const PyroCameraBasis& Basis, const std::array<float, 3>& BoxMin, const std::array<float, 3>& BoxMax, float Width, float Height,
                   std::vector<std::array<float, 4>>& OutSegments)
{
    OutSegments.clear();
    const auto Dot = [](const std::array<float, 3>& Left, const std::array<float, 3>& Right) { return Left[0] * Right[0] + Left[1] * Right[1] + Left[2] * Right[2]; };
    std::array<std::array<float, 3>, 8> Corners{};
    for (size_t Index = 0; Index < 8; ++Index)
    {
        std::array<float, 3> Offset{};
        for (size_t Axis = 0; Axis < 3; ++Axis) Offset[Axis] = (((Index >> Axis) & 1u) ? BoxMax[Axis] : BoxMin[Axis]) - Basis.Position[Axis];
        Corners[Index] = { Dot(Offset, Basis.Right), Dot(Offset, Basis.Up), Dot(Offset, Basis.Forward) };
    }
    const float NearPlane = std::max(Basis.Near, 0.001f);
    const float Tangent   = std::tan(Basis.FovRadians / 2.0f);
    const auto  Project   = [&](const std::array<float, 3>& Point) {
        return std::array<float, 2>{ (0.5f + (Point[0] / (Point[2] * Tangent * Basis.Aspect)) * 0.5f) * Width, (0.5f - (Point[1] / (Point[2] * Tangent)) * 0.5f) * Height };
    };
    for (size_t Index = 0; Index < 8; ++Index)
    {
        for (size_t Axis = 0; Axis < 3; ++Axis)
        {
            if ((Index >> Axis) & 1u) continue;
            std::array<float, 3> Start = Corners[Index];
            std::array<float, 3> End   = Corners[Index | (1u << Axis)];
            if (Start[2] < NearPlane && End[2] < NearPlane) continue;
            if (Start[2] < NearPlane || End[2] < NearPlane)
            {
                const float Fraction = (NearPlane - Start[2]) / (End[2] - Start[2]);
                std::array<float, 3> Clipped{};
                for (size_t Coordinate = 0; Coordinate < 3; ++Coordinate) Clipped[Coordinate] = Start[Coordinate] + Fraction * (End[Coordinate] - Start[Coordinate]);
                if (Start[2] < NearPlane) Start = Clipped;
                else End = Clipped;
            }
            const std::array<float, 2> A = Project(Start);
            const std::array<float, 2> B = Project(End);
            const std::array<float, 4> Segment = { A[0], A[1], B[0], B[1] };
            if (std::isfinite(Segment[0]) && std::isfinite(Segment[1]) && std::isfinite(Segment[2]) && std::isfinite(Segment[3])) OutSegments.push_back(Segment);
        }
    }
}

} // namespace Frontier
