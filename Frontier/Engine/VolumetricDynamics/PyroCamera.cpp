/*==========================================================================================================================================
                                                            PYROCAMERA.CPP
==========================================================================================================================================*/
// 🧩 The orbit camera of the Fluid volume (PyroCamera.h). Moved out of PyroVolumeSolver.cpp unchanged, with the three helpers it used.

#include "Frontier/Engine/VolumetricDynamics/PyroCamera.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{

constexpr float PiVal   = 3.14159265358979323846f;
constexpr float Deg2Rad = PiVal / 180.0f;

std::array<float, 3> Normalize3(const std::array<float, 3>& V) noexcept
{
    const float L = std::sqrt(V[0] * V[0] + V[1] * V[1] + V[2] * V[2]);
    if (L < 1.0e-6f)
        return { 0.0f, 1.0f, 0.0f };
    return { V[0] / L, V[1] / L, V[2] / L };
}

std::array<float, 3> Cross3(const std::array<float, 3>& A, const std::array<float, 3>& B) noexcept
{
    return {
        A[1] * B[2] - A[2] * B[1],
        A[2] * B[0] - A[0] * B[2],
        A[0] * B[1] - A[1] * B[0],
    };
}

} // namespace

//----------------------------------------------------------------------------------------------------------------------
//                                                PYRO ORBIT CAMERA
//----------------------------------------------------------------------------------------------------------------------

void PyroOrbitCamera::Reset() noexcept
{
    Theta      = 0.62f;
    Phi        = 1.18f;
    Distance   = 3.15f;
    Target     = { 0.0f, 0.35f, 0.0f };
    FovDegrees = 46.0f;
}

void PyroOrbitCamera::ApplyPresetView(int ViewOrdinal) noexcept
{
    if (ViewOrdinal == 1)      { Theta = 0.0f;           Phi = PiVal * 0.48f; Distance = 3.2f; } // Front
    else if (ViewOrdinal == 2) { Theta = PiVal * 0.5f;   Phi = PiVal * 0.48f; Distance = 3.2f; } // Side
    else if (ViewOrdinal == 3) { Theta = 0.0f;           Phi = 0.14f;         Distance = 3.5f; } // Top
    else                       { Reset(); }
}

void PyroOrbitCamera::OrbitByPixels(float DeltaX, float DeltaY) noexcept
{
    Theta -= DeltaX * 0.0065f;
    Phi    = std::clamp(Phi - DeltaY * 0.0065f, 0.08f, PiVal - 0.08f);
}

void PyroOrbitCamera::PanByPixels(float DeltaX, float DeltaY) noexcept
{
    const PyroCameraFrame Frame = ComputeFrame();
    const float Scale           = Distance * 0.0014f;
    for (int C = 0; C < 3; ++C)
        Target[static_cast<size_t>(C)] += (-DeltaX * Frame.Right[static_cast<size_t>(C)] + DeltaY * Frame.Up[static_cast<size_t>(C)]) * Scale;
}

void PyroOrbitCamera::ZoomByWheel(float WheelSteps) noexcept
{
    Distance = std::clamp(Distance * std::pow(0.88f, WheelSteps), 0.85f, 12.0f);
}

void PyroOrbitCamera::AdvanceTurntable(float DtSeconds) noexcept
{
    if (AutoRotate)
        Theta += DtSeconds * 0.38f;
}

PyroCameraFrame PyroOrbitCamera::ComputeFrame(float AspectOverride) const noexcept
{
    PyroCameraFrame Frame{};
    Frame.Aspect     = (AspectOverride > 0.05f) ? AspectOverride : Aspect;
    Frame.FovRadians = FovDegrees * Deg2Rad;

    const float SinPhi = std::sin(Phi);
    Frame.Position = {
        Target[0] + Distance * SinPhi * std::sin(Theta),
        Target[1] + Distance * std::cos(Phi),
        Target[2] + Distance * SinPhi * std::cos(Theta),
    };
    Frame.Forward = Normalize3({
        Target[0] - Frame.Position[0],
        Target[1] - Frame.Position[1],
        Target[2] - Frame.Position[2],
    });
    Frame.Right = Normalize3(Cross3(Frame.Forward, { 0.0f, 1.0f, 0.0f }));
    Frame.Up    = Normalize3(Cross3(Frame.Right, Frame.Forward));
    return Frame;
}

bool PyroOrbitCamera::RaycastBrushPoint(float NdcX, float NdcY, std::array<float, 3>& OutWorldPoint) const noexcept
{
    const PyroCameraFrame F = ComputeFrame();
    const float TanHalf     = std::tan(F.FovRadians * 0.5f);
    const std::array<float, 3> Dir = Normalize3({
        F.Forward[0] + NdcX * F.Aspect * TanHalf * F.Right[0] + NdcY * TanHalf * F.Up[0],
        F.Forward[1] + NdcX * F.Aspect * TanHalf * F.Right[1] + NdcY * TanHalf * F.Up[1],
        F.Forward[2] + NdcX * F.Aspect * TanHalf * F.Right[2] + NdcY * TanHalf * F.Up[2],
    });
    // Intersect camera-facing plane passing through Target
    const float Denom = Dir[0] * F.Forward[0] + Dir[1] * F.Forward[1] + Dir[2] * F.Forward[2];
    if (std::fabs(Denom) < 1.0e-4f)
        return false;
    const float Numer = (Target[0] - F.Position[0]) * F.Forward[0]
                      + (Target[1] - F.Position[1]) * F.Forward[1]
                      + (Target[2] - F.Position[2]) * F.Forward[2];
    const float T = Numer / Denom;
    if (T <= 0.05f)
        return false;
    OutWorldPoint = { F.Position[0] + Dir[0] * T, F.Position[1] + Dir[1] * T, F.Position[2] + Dir[2] * T };
    return true;
}

} // namespace Frontier
