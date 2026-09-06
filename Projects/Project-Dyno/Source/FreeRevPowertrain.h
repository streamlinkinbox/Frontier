//============================================================================================================================================
//                                                      FREEREVPOWERTRAIN.H
//============================================================================================================================================
// 🧩 The dyno cell's stand-in for the game's physics: torque curve, friction, inertia, idle governor, rev limiter, overrun
//    fuel cut. Advance(Δτ, throttle) integrates the crank speed and yields the PowertrainRecord the voice wants — the same
//    arithmetic as `FreeRevPowertrain` in Tools/AudioEditor/index.html, so the dyno's --free mode and the editor's throttle
//    key rev the same way. Project-Zero replaces it with its own physics; only the record crosses to the audio side.
//
//    The vehicle sheet it reads (idle / redline rpm, peak torque and its rpm, inertia, friction) is the AcousticStructure's
//    [vehicle] section — one file describes the car for both the voice and this stand-in.

#pragma once

#include "../../../Engine/PlatformInterchange/AcousticStructure.h"
#include "../../../Engine/PlatformInterchange/PowertrainRecord.h"

namespace Frontier {

class FreeRevPowertrain
{
public:
    explicit FreeRevPowertrain(const AcousticStructure::VehicleSheet& Vehicle) noexcept : V(Vehicle), Rpm(Vehicle.IdleRpm) { }

    // New vehicle numbers; the crank speed carries over (the editor's assign()).
    void AssignVehicle(const AcousticStructure::VehicleSheet& Vehicle) noexcept { V = Vehicle; }

    // Hand the crank a speed (the editor does this when leaving a scripted pull so the free rev continues from where it was).
    void AssignRpm(double NewRpm) noexcept { Rpm = NewRpm; }

    // One step: Throttle 0 … 1 → new rpm, load, fuel state.
    void Advance(double Δτ, double ThrottleInput) noexcept;

    [[nodiscard]] PowertrainRecord QueryRecord() const noexcept;
    [[nodiscard]] double QueryRpm()      const noexcept { return Rpm; }
    [[nodiscard]] double QueryLoad()     const noexcept { return Load; }
    [[nodiscard]] bool   IsFuelOn()      const noexcept { return FuelOn; }
    [[nodiscard]] bool   IsOnLimiter()   const noexcept { return Cut; }

private:
    [[nodiscard]] double TorqueShape(double AtRpm) const noexcept;

    AcousticStructure::VehicleSheet V;
    double Rpm;                 // [rpm]
    double Throttle = 0.0;      // [-]
    double Load     = 0.0;      // [-]
    bool   FuelOn   = true;     // [-]
    bool   Cut      = false;    // [-] rev limiter engaged
};

} // namespace Frontier
