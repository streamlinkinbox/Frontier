//============================================================================================================================================
//                                                        DRIVERINPUTEXCHANGE.H
//============================================================================================================================================
// 📦 The seam that crosses Frontier's device polling into the engine-agnostic DriverInputIntegrator.
//
//    The integrator does all the feel work (digital ramps, self-centring steer, dead-zones, analog shaping); this
//    exchange only forwards the raw button readings and edge-detects the discrete pulses (shift up/down, reset) so they
//    fire once per press, not every frame the key is held.
//
//    STANDARD KEYBOARD LAYOUT (the default the user asked for):
//        W throttle   S brake/reverse   A/D steer left/right   Space handbrake
//        Left-Shift up-shift   Left-Ctrl down-shift   R reset
//
//    Play mode only: the host polls this when the chase (player) camera is active and skips it when the editor fly
//    camera owns input, so W/A/S/D fly the editor camera instead of driving. A gamepad/wheel would instead push analog
//    axes into the same DriverInputIntegrator (ForwardThrottleAxis/ForwardSteerAxis…) — documented there.

#pragma once

#include "../../../Engine/DeviceExchange/InputExchange.h"
#include "../../../Engine/PhysicalDynamics/Vehicle/DriverInputIntegrator.h"

namespace Frontier {
namespace Drive {

class DriverInputExchange
{
public:
    // Forward this frame's device readings into `Integrator`, fire edge pulses, and return the resolved command.
    Frontier::Vehicle::DriverCommand Poll(const Frontier::InputExchange&           Device,
                                          Frontier::Vehicle::DriverInputIntegrator& Integrator,
                                          float                                     Δτ) noexcept
    {
        using Key = Frontier::VirtualKeyCategory;

        Integrator.ForwardThrottleKey  (Device.IsKeyPressed(Key::KeyW));
        Integrator.ForwardBrakeKey     (Device.IsKeyPressed(Key::KeyS));
        Integrator.ForwardSteerLeftKey (Device.IsKeyPressed(Key::KeyA));
        Integrator.ForwardSteerRightKey(Device.IsKeyPressed(Key::KeyD));
        Integrator.ForwardHandbrakeKey (Device.IsKeyPressed(Key::KeySpace));

        const bool UpShift   = Device.IsKeyPressed(Key::KeyLeftShift);
        const bool DownShift = Device.IsKeyPressed(Key::KeyLeftControl);
        const bool Reset     = Device.IsKeyPressed(Key::KeyR);
        if (UpShift   && !PreviousUpShift)   Integrator.ShiftUp();       // rising edge only
        if (DownShift && !PreviousDownShift) Integrator.ShiftDown();
        if (Reset     && !PreviousReset)     Integrator.RequestReset();
        PreviousUpShift = UpShift;
        PreviousDownShift = DownShift;
        PreviousReset = Reset;

        return Integrator.Advance(Δτ);
    }

private:
    bool PreviousUpShift   = false;
    bool PreviousDownShift = false;
    bool PreviousReset     = false;
};

} // namespace Drive
} // namespace Frontier
