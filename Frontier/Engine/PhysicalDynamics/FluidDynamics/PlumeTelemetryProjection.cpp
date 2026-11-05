/*==========================================================================================================================================
                                                   PLUMETELEMETRYPROJECTION.CPP
==========================================================================================================================================*/
// 🧩 See PlumeTelemetryProjection.h.

#include "PlumeTelemetryProjection.h"

namespace Frontier::FluidDynamics
{

using GlobalIllumination::ConstructAxisQuaternion;
using GlobalIllumination::MultiplyQuaternion;
using GlobalIllumination::RotateVector;

PlumeTelemetry ProjectPlumeTelemetry(const PlumeVehicleBody& Vehicle, const std::vector<PlumeWheelReading>& Readings,
                                     const std::vector<PlumeWheelThermal>& Thermal)
{
    PlumeTelemetry Telemetry;
    Telemetry.Position         = Vehicle.Position;
    Telemetry.Velocity         = Vehicle.LinearVelocity;
    Telemetry.AfterfireSeconds = 0.0;
    for (size_t Index = 0; Index < Readings.size(); ++Index)
    {
        const PlumeWheelReading& Reading = Readings[Index];
        const PlumeVehicleWheel& Wheel   = Vehicle.Wheels.at(Index);
        const PlumeWheelThermal& Heat    = Thermal.at(Index);
        const Vector3            Heading = RotateVector(MultiplyQuaternion(Vehicle.Orientation, ConstructAxisQuaternion({0.0, 0.0, 1.0}, Wheel.Steer)), {1.0, 0.0, 0.0});
        PlumeWheel               Result;
        Result.InContact       = Reading.ContactFraction > 0.5 && !Reading.OnDebris;
        // Bare rims have no tyre rubber to vaporise.
        Result.SlidingPower    = Reading.Shredded ? 0.0 : Reading.SlidingPower;
        Result.SlipSpeed       = Reading.SlipSpeed;
        Result.ContactPoint    = Reading.ContactPoint;
        Result.ContactVelocity = Reading.ContactVelocity;
        Result.HubPosition     = Reading.HubPosition;
        Result.Thermal         = {!Reading.Shredded && Heat.Ignited, Heat.CombustionPower, Heat.Surface};
        Result.Burst           = Heat.Burst;
        Result.Radius          = Wheel.Radius;
        Result.Longitudinal    = Heading;
        Result.SurfaceSpeed    = Wheel.Spin * Wheel.Radius;
        Telemetry.Wheels.push_back(Result);
    }
    return Telemetry;
}

} // namespace Frontier::FluidDynamics
