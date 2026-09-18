//============================================================================================================================================
// 📦 Project-Zero/Source/FlyThroughSolver.cpp — Unreal-Style Fly-Through Camera Navigation Implementation
//============================================================================================================================================

#include "FlyThroughSolver.h"
#include <cmath>
#include <algorithm>

namespace Frontier::ProjectZero {

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

FlyThroughSolver::FlyThroughSolver() noexcept
    : CameraProjection()
    , Config{
        2.5f,                   // Base flight speed 2.5 m/s
        3.0f,                   // 3x speed boost when holding Shift
        0.00125f,               // Mouse sensitivity 0.00125 rad/px (≈ 0.07°/px, Unreal-like default)
        0.5f,                   // Scroll speed increment 0.5 m/s per click
        12.0f                   // Acceleration damping rate
    }
    , CurrentVelocity{ 0.0f, 0.0f, 0.0f }
    , CurrentFlightSpeed(2.5f)
    , SteeringActive(false)
{
}

FlyThroughSolver::FlyThroughSolver(const FlyThroughConfiguration& InitialConfig) noexcept
    : CameraProjection()
    , Config(InitialConfig)
    , CurrentVelocity{ 0.0f, 0.0f, 0.0f }
    , CurrentFlightSpeed(InitialConfig.BaseFlightSpeed)
    , SteeringActive(false)
{
}

//------------------------------------------------------------------------------------------------------------------------
//                                                LOCOMOTION STEPPING
//------------------------------------------------------------------------------------------------------------------------

void FlyThroughSolver::AdvanceLocomotion(const InputExchange& Input, float Δτ) noexcept
{
    if (Δτ <= 0.0f)
    {
        return;
    }

    // 1. Mouse Scroll Wheel Flight Speed Adjustment
    float Scroll = Input.QueryMouseScrollDelta();
    if (std::abs(Scroll) > 1e-4f)
    {
        CurrentFlightSpeed = std::clamp(CurrentFlightSpeed + Scroll * Config.ScrollSpeedIncrement, 0.25f, 50.0f);
    }

    // 2. Right Mouse Button (RMB) Look-Around Steering
    SteeringActive = Input.IsMouseButtonPressed(Frontier::MouseButtonCategory::ButtonRight);
    if (SteeringActive)
    {
        Vector3 CursorDelta = Input.QueryCursorDelta();
        float NewYaw   = YawRadians + CursorDelta.x * Config.MouseSensitivity;
        float NewPitch = PitchRadians - CursorDelta.y * Config.MouseSensitivity * (Config.InvertPitch ? -1.0f : 1.0f);
        AssignOrientationEuler(NewPitch, NewYaw, 0.0f);
    }

    // 3. 6-DOF Directional Flight (WASD + Q/E)
    Vector3 DesiredDirection{ 0.0f, 0.0f, 0.0f };

    // Forward / Backward
    if (Input.IsKeyPressed(VirtualKeyCategory::KeyW)) DesiredDirection += ForwardVector;
    if (Input.IsKeyPressed(VirtualKeyCategory::KeyS)) DesiredDirection -= ForwardVector;

    // Strafe Right / Left
    if (Input.IsKeyPressed(VirtualKeyCategory::KeyD)) DesiredDirection += RightVector;
    if (Input.IsKeyPressed(VirtualKeyCategory::KeyA)) DesiredDirection -= RightVector;

    // Vertical Up (E) / Down (Q) — Strict +Z Up Axis
    if (Input.IsKeyPressed(VirtualKeyCategory::KeyE)) DesiredDirection += Vector3{ 0.0f, 0.0f, 1.0f };
    if (Input.IsKeyPressed(VirtualKeyCategory::KeyQ)) DesiredDirection -= Vector3{ 0.0f, 0.0f, 1.0f };

    float LengthSq = DesiredDirection.LengthSquared();
    if (LengthSq > 1e-6f)
    {
        DesiredDirection = DesiredDirection / std::sqrt(LengthSq);
    }

    // Speed boost multiplier when Shift is held
    float TargetSpeed = CurrentFlightSpeed;
    if (Input.IsKeyPressed(VirtualKeyCategory::KeyLeftShift))
    {
        TargetSpeed *= Config.BoostMultiplier;
    }

    Vector3 TargetVelocity = DesiredDirection * TargetSpeed;

    // Smooth exponential acceleration and braking damping
    float SmoothingAlpha = 1.0f - std::exp(-Config.AccelerationDamping * Δτ);
    CurrentVelocity += (TargetVelocity - CurrentVelocity) * SmoothingAlpha;

    // Apply movement integration
    SpatialLocation += CurrentVelocity * Δτ;
}

void FlyThroughSolver::AdvanceProjection(float Δτ) noexcept
{
    (void)Δτ;
}

void FlyThroughSolver::AssignFlightSpeed(float SpeedMetersPerSec) noexcept
{
    CurrentFlightSpeed = std::clamp(SpeedMetersPerSec, 0.1f, 100.0f);
}

} // namespace Frontier::ProjectZero
