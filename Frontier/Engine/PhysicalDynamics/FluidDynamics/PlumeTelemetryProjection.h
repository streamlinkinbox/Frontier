/*==========================================================================================================================================
                                                    PLUMETELEMETRYPROJECTION.H
==========================================================================================================================================*/
// 🧩 The vehicle's tyre readings and tread heat as the plume's sources read them: contact, sliding power, the wheel's heading and its surface
//    speed. A port of SmokeSequence.js ProjectPlumeTelemetry.

#pragma once

#include "PlumeSourceProjection.h"

#include <vector>

namespace Frontier::FluidDynamics
{

using GlobalIllumination::Vector4;

// 📝 What the vehicle model says about its body and each wheel.
struct PlumeVehicleWheel
{
    double Steer  = 0.0;   // [rad]
    double Radius = 0.0;   // [m]
    double Spin   = 0.0;   // [rad/s]
};

struct PlumeVehicleBody
{
    Vector3                        Position{};          // [m]
    Vector3                        LinearVelocity{};    // [m/s]
    Vector4                        Orientation{0.0, 0.0, 0.0, 1.0};   // [-] x, y, z, w
    std::vector<PlumeVehicleWheel> Wheels;
};

// 📝 One wheel's contact reading (the tyre model's).
struct PlumeWheelReading
{
    double  ContactFraction = 0.0;
    bool    OnDebris        = false;
    bool    Shredded        = false;
    double  SlidingPower    = 0.0;   // [W]
    double  SlipSpeed       = 0.0;   // [m/s]
    Vector3 ContactPoint{};          // [m]
    Vector3 ContactVelocity{};       // [m/s]
    Vector3 HubPosition{};           // [m]
};

// 📝 One wheel's tread heat (the thermal model's).
struct PlumeWheelThermal
{
    bool   Ignited         = false;
    double CombustionPower = 0.0;   // [W]
    double Surface         = 0.0;   // [K]
    bool   Burst           = false;
};

[[nodiscard]] PlumeTelemetry ProjectPlumeTelemetry(const PlumeVehicleBody& Vehicle, const std::vector<PlumeWheelReading>& Readings,
                                                   const std::vector<PlumeWheelThermal>& Thermal);

} // namespace Frontier::FluidDynamics
