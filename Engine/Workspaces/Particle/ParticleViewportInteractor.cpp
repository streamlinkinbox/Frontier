/*============================================================================================================================================
                                                   PARTICLEVIEWPORTINTERACTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of Z-Up spherical orbit/pan/dolly camera rig & perspective projection for ParticleViewportHost

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Particle/ParticleViewportInteractor.h"

#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <cmath>

namespace Frontier::Workspaces
{

namespace
{

[[nodiscard]] Mathematics::Axis3D NormalizeSafe(Mathematics::Axis3D Vec) noexcept
{
    const float LenSq = Vec.X * Vec.X + Vec.Y * Vec.Y + Vec.Z * Vec.Z;
    if (LenSq <= 1.0e-12f)
    {
        return { 0.0f, 0.0f, 1.0f };
    }
    const float Inv = 1.0f / std::sqrt(LenSq);
    return { Vec.X * Inv, Vec.Y * Inv, Vec.Z * Inv };
}

[[nodiscard]] Mathematics::Axis3D CrossVec(Mathematics::Axis3D A, Mathematics::Axis3D B) noexcept
{
    return {
        A.Y * B.Z - A.Z * B.Y,
        A.Z * B.X - A.X * B.Z,
        A.X * B.Y - A.Y * B.X,
    };
}

[[nodiscard]] float DotVec(Mathematics::Axis3D A, Mathematics::Axis3D B) noexcept
{
    return A.X * B.X + A.Y * B.Y + A.Z * B.Z;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                               PARTICLEVIEWPORTINTERACTOR
//------------------------------------------------------------------------------------------------------------------------

void ParticleViewportInteractor::ResetCamera() noexcept
{
    CameraRig       = ParticleCameraRig{};
    OrbitDragActive = false;
    PanDragActive   = false;
}

void ParticleViewportInteractor::ConfigureCamera(const ParticleCameraRig& Rig) noexcept
{
    CameraRig                = Rig;
    CameraRig.PitchRadians   = std::clamp(CameraRig.PitchRadians, -1.42f, 1.42f);
    CameraRig.DistanceMeters = std::clamp(CameraRig.DistanceMeters, 1.2f, 80.0f);
}

void ParticleViewportInteractor::FocusPoint(Mathematics::Axis3D TargetMeters, float DistanceMeters) noexcept
{
    CameraRig.TargetMeters   = TargetMeters;
    CameraRig.DistanceMeters = std::clamp(DistanceMeters, 1.2f, 80.0f);
}

Mathematics::Axis3D ParticleViewportInteractor::QueryCameraPosition() const noexcept
{
    const float CosPitch = std::cos(CameraRig.PitchRadians);
    const float SinPitch = std::sin(CameraRig.PitchRadians);
    const float CosYaw   = std::cos(CameraRig.YawRadians);
    const float SinYaw   = std::sin(CameraRig.YawRadians);
    return {
        CameraRig.TargetMeters.X + CameraRig.DistanceMeters * CosPitch * CosYaw,
        CameraRig.TargetMeters.Y + CameraRig.DistanceMeters * CosPitch * SinYaw,
        CameraRig.TargetMeters.Z + CameraRig.DistanceMeters * SinPitch,
    };
}

bool ParticleViewportInteractor::ApplyPointerInput(
    Rectangle2D             ViewportBounds,
    const PaneInputReading& Input) noexcept
{
    bool Changed = false;
    const Point2D Pointer{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool Inside = Input.PrimaryPointer.Enabled && ViewportBounds.Contains(Pointer);

    if (Inside && std::abs(Input.WheelDeltaY) > 1.0e-4f)
    {
        const float ZoomFactor   = std::pow(0.88f, Input.WheelDeltaY);
        CameraRig.DistanceMeters = std::clamp(CameraRig.DistanceMeters * ZoomFactor, 1.2f, 80.0f);
        Changed                  = true;
    }

    if (Inside && Input.PrimaryPointer.Pressed)
    {
        OrbitDragActive = !Input.ShiftHeld;
        PanDragActive   = Input.ShiftHeld;
        LastPointerPos  = Pointer;
    }

    if (Input.PrimaryPointer.Released || !Input.PrimaryPointer.Down)
    {
        OrbitDragActive = false;
        PanDragActive   = false;
    }

    if ((OrbitDragActive || PanDragActive) && Input.PrimaryPointer.Down)
    {
        const float Dx = Pointer.X - LastPointerPos.X;
        const float Dy = Pointer.Y - LastPointerPos.Y;
        LastPointerPos = Pointer;

        if (std::abs(Dx) > 0.01f || std::abs(Dy) > 0.01f)
        {
            if (OrbitDragActive)
            {
                CameraRig.YawRadians  -= Dx * 0.0065f;
                CameraRig.PitchRadians = std::clamp(CameraRig.PitchRadians + Dy * 0.0055f, -1.42f, 1.42f);
            }
            else if (PanDragActive)
            {
                const Mathematics::Axis3D Eye     = QueryCameraPosition();
                const Mathematics::Axis3D Forward = NormalizeSafe({
                    CameraRig.TargetMeters.X - Eye.X,
                    CameraRig.TargetMeters.Y - Eye.Y,
                    CameraRig.TargetMeters.Z - Eye.Z,
                });
                const Mathematics::Axis3D Right   = NormalizeSafe(CrossVec(Forward, { 0.0f, 0.0f, 1.0f }));
                const Mathematics::Axis3D Up      = NormalizeSafe(CrossVec(Right, Forward));
                const float PanScale              = CameraRig.DistanceMeters * 0.0018f;
                CameraRig.TargetMeters.X += (-Right.X * Dx + Up.X * Dy) * PanScale;
                CameraRig.TargetMeters.Y += (-Right.Y * Dx + Up.Y * Dy) * PanScale;
                CameraRig.TargetMeters.Z += (-Right.Z * Dx + Up.Z * Dy) * PanScale;
            }
            Changed = true;
        }
    }

    return Changed;
}

bool ParticleViewportInteractor::ProjectWorldPoint(
    Mathematics::Axis3D WorldPoint,
    Rectangle2D         ViewportBounds,
    Point2D&            OutScreenPoint,
    float&              OutDepthMeters) const noexcept
{
    if (ViewportBounds.Width <= 4.0f || ViewportBounds.Height <= 4.0f)
    {
        return false;
    }

    const Mathematics::Axis3D Eye     = QueryCameraPosition();
    const Mathematics::Axis3D Forward = NormalizeSafe({
        CameraRig.TargetMeters.X - Eye.X,
        CameraRig.TargetMeters.Y - Eye.Y,
        CameraRig.TargetMeters.Z - Eye.Z,
    });
    const Mathematics::Axis3D Right   = NormalizeSafe(CrossVec(Forward, { 0.0f, 0.0f, 1.0f }));
    const Mathematics::Axis3D Up      = NormalizeSafe(CrossVec(Right, Forward));

    const Mathematics::Axis3D Delta{
        WorldPoint.X - Eye.X,
        WorldPoint.Y - Eye.Y,
        WorldPoint.Z - Eye.Z,
    };

    const float Depth = DotVec(Delta, Forward);
    if (Depth <= 0.12f)
    {
        return false;
    }

    const float FocalPx = (ViewportBounds.Height * 0.5f) / std::tan(CameraRig.VerticalFovRadians * 0.5f);
    const float CamX    = DotVec(Delta, Right);
    const float CamY    = DotVec(Delta, Up);

    OutScreenPoint.X = ViewportBounds.X + ViewportBounds.Width * 0.5f + (CamX / Depth) * FocalPx;
    OutScreenPoint.Y = ViewportBounds.Y + ViewportBounds.Height * 0.5f - (CamY / Depth) * FocalPx;
    OutDepthMeters   = Depth;
    return true;
}

} // namespace Frontier::Workspaces
