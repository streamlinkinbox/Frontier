#pragma once
/*==========================================================================================================================================
                                                             VEHICLECABIN.H
==========================================================================================================================================*/
// 🧩 The parts of the vehicle a driver touches, as the C++ vehicle carries them: a steering wheel that
//    follows the steer demand, and the dashboard tablet. The tablet is deliberately EMPTY for now — it
//    powers on and shows nothing; the HMI arrives in a later step and only has to fill this in.

#include "Frontier/Engine/Input/InputPacket.h"

#include <algorithm>

namespace Frontier
{

struct SteeringWheel
{
    float AngleDegrees = 0.0f;      // [-] current lock, turned by steer demand
    float MaximumLockDegrees = 540.0f;

    void ApplySteer(float SteerAxis) noexcept
    {
        AngleDegrees = std::clamp(SteerAxis, -1.0f, 1.0f) * MaximumLockDegrees;
    }
};

// 📝 The tablet, present and powered, showing nothing yet.
struct VehicleTablet
{
    enum class Content : uint8_t { Empty = 0 };

    bool Powered = true;
    Content Shown = Content::Empty;

    [[nodiscard]] bool IsEmpty() const noexcept { return !Powered || Shown == Content::Empty; }
};

struct VehicleCabin
{
    SteeringWheel Wheel{};
    VehicleTablet Tablet{};

    void ApplyInputPacket(const InputPacket& Packet) noexcept { Wheel.ApplySteer(Packet.SteerAxis); }
};

} // namespace Frontier
