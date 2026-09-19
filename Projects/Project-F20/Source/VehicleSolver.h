//============================================================================================================================================
// 📦 Project-F20/Source/VehicleSolver.h — High-Performance Vehicle Dynamics and Tire Friction Solver
//============================================================================================================================================

#pragma once

#include "VehicleStructure.h"
#include "../../../PhysicalDynamics/RigidBodySolver.h"
#include "../../../PhysicalDynamics/DeformableSolver.h"

namespace Frontier::ProjectF20 {

//------------------------------------------------------------------------------------------------------------------------
//                                                    VEHICLE SOLVER
//------------------------------------------------------------------------------------------------------------------------

class VehicleSolver
{
public:
    explicit VehicleSolver(VehicleConfiguration InitialConfig) noexcept;
    ~VehicleSolver() noexcept = default;

    void                    AdvanceVehicle(float SteeringRequest, float ThrottleRequest, float BrakeRequest, float Δτ) noexcept;

    [[nodiscard]] const VehicleTelemetry& QueryTelemetry() const noexcept { return Telemetry; }

    // Single unified conversion operator for speed in km/h
    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

private:
    VehicleConfiguration    Config;                             // [config] physical car parameters
    VehicleTelemetry        Telemetry;                          // [telemetry] dynamic vehicle states
};

template<>
inline float VehicleSolver::Convert<float>() const noexcept
{
    return Telemetry.SpeedKilometersPerHour;
}

template<>
inline VehicleTelemetry VehicleSolver::Convert<VehicleTelemetry>() const noexcept
{
    return Telemetry;
}

} // namespace Frontier::ProjectF20
