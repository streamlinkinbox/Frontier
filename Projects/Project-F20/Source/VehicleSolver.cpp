//============================================================================================================================================
// 📦 Project-F20/Source/VehicleSolver.cpp — Vehicle Dynamics Simulation Implementation
//============================================================================================================================================

#include "VehicleSolver.h"
#include <algorithm>
#include <cmath>

namespace Frontier::ProjectF20 {

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

VehicleSolver::VehicleSolver(VehicleConfiguration InitialConfig) noexcept
    : Config(InitialConfig)
    , Telemetry{}
{
    Telemetry.SpatialLocation            = Frontier::Vector3{ 0.0f, 0.5f, 0.0f };
    Telemetry.AngularOrientation         = Frontier::Quaternion::Identity();
    Telemetry.LinearVelocity             = Frontier::Vector3{ 0.0f, 0.0f, 0.0f };
    Telemetry.SpeedKilometersPerHour     = 0.0f;
    Telemetry.EngineRevolutionsPerMinute = 1000.0f;             // Idle RPM
    Telemetry.CurrentGear                = 1;
    Telemetry.ChassisDamageRatio         = 0.0f;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              VEHICLE INTEGRATION
//------------------------------------------------------------------------------------------------------------------------

void VehicleSolver::AdvanceVehicle(float SteeringRequest, float ThrottleRequest, float BrakeRequest, float Δτ) noexcept
{
    if (Δτ <= 0.0f)
    {
        return;
    }

    Telemetry.SteeringInput = std::clamp(SteeringRequest, -1.0f, 1.0f);
    Telemetry.ThrottleInput = std::clamp(ThrottleRequest, 0.0f, 1.0f);
    Telemetry.BrakeInput    = std::clamp(BrakeRequest, 0.0f, 1.0f);

    // Forward drive force
    float DriveForce = (Telemetry.ThrottleInput * Config.EnginePeakTorque * 3.5f) / 0.35f; // Gear ratio / wheel radius
    float BrakingForce = Telemetry.BrakeInput * (Config.BrakeTorque / 0.35f);

    float ForwardSpeed = Telemetry.LinearVelocity.z;
    float NetForce = DriveForce - BrakingForce - (0.5f * 1.225f * 0.30f * 2.2f * ForwardSpeed * ForwardSpeed); // Aero drag

    float Acceleration = NetForce / Config.ChassisMass;
    Telemetry.LinearVelocity.z += Acceleration * Δτ;
    if (Telemetry.LinearVelocity.z < 0.0f)
    {
        Telemetry.LinearVelocity.z = 0.0f;
    }

    // Steering yaw rotation
    float SteeringYaw = Telemetry.SteeringInput * Config.SteeringLockAngle * (ForwardSpeed / (ForwardSpeed + 5.0f));
    Telemetry.LinearVelocity.x += SteeringYaw * 5.0f * Δτ;
    Telemetry.LinearVelocity.x *= (1.0f - 5.0f * Δτ);           // Lateral tire resistance

    // Update position
    Telemetry.SpatialLocation += Telemetry.LinearVelocity * Δτ;

    // Telemetry updates
    Telemetry.SpeedKilometersPerHour = Telemetry.LinearVelocity.z * 3.6f;
    Telemetry.EngineRevolutionsPerMinute = 1000.0f + (Telemetry.SpeedKilometersPerHour / 280.0f) * 7500.0f;

    // Automatic transmission shift
    if (Telemetry.EngineRevolutionsPerMinute > 7800.0f && Telemetry.CurrentGear < 6)
    {
        Telemetry.CurrentGear++;
    }
    else if (Telemetry.EngineRevolutionsPerMinute < 3000.0f && Telemetry.CurrentGear > 1)
    {
        Telemetry.CurrentGear--;
    }
}

} // namespace Frontier::ProjectF20
