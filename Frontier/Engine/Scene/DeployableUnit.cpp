/*==========================================================================================================================================
                                                          DEPLOYABLEUNIT.CPP
==========================================================================================================================================*/

#include "Frontier/Engine/Scene/DeployableUnit.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{
constexpr float DegreesPerRadian = 57.29577951308232f;
constexpr float MaximumForwardMps = 55.0f;
constexpr float MaximumReverseMps = 12.0f;
constexpr float ThrottleAcceleration = 7.5f;  // [m/s²] at full demand
constexpr float BrakeDeceleration = 11.0f;    // [m/s²]
constexpr float RollingDrag = 0.35f;          // [m/s²]
}

void DeployableUnit::Deploy(const std::array<float, 3>& PositionMetres_, float YawDegrees_,
                            const SpawnSurfaceSampler& Sampler) noexcept
{
    SpawnStand = ResolveSpawnPose(PositionMetres_, YawDegrees_, SpawnRules, Sampler);
    PositionMetres = SpawnStand.PositionMetres;
    YawDegrees = SpawnStand.YawDegrees;
    PitchDegrees = SpawnStand.PitchDegrees;
    RollDegrees = SpawnStand.RollDegrees;
    ForwardSpeedMps = 0.0f;
    SecondsSinceSpawn = 0.0f;
    HandbrakeLatched = SpawnRules.HandbrakeEngagedAtSpawn && SpawnRules.BrakesEnabledAtSpawn;
}

void DeployableUnit::ReturnToSpawnStand() noexcept
{
    PositionMetres = SpawnStand.PositionMetres;
    YawDegrees = SpawnStand.YawDegrees;
    PitchDegrees = SpawnStand.PitchDegrees;
    RollDegrees = SpawnStand.RollDegrees;
    ForwardSpeedMps = 0.0f;
    SecondsSinceSpawn = 0.0f;
    HandbrakeLatched = SpawnRules.HandbrakeEngagedAtSpawn && SpawnRules.BrakesEnabledAtSpawn;
}

void DeployableUnit::ResolveEffectivePedals(const InputPacket& Packet, float& OutThrottle, float& OutBrake, float& OutHandbrake) noexcept
{
    OutThrottle = std::clamp(Packet.ThrottleAxis, 0.0f, 1.0f);
    SpawnRules.ResolvePedalDemand(SecondsSinceSpawn, Packet.BrakeAxis, Packet.HandbrakeAxis, OutBrake, OutHandbrake);

    // 📝 Parked in the studio: the latch holds until the driver asks for drive, then it is gone for good
    //    (re-deploying re-latches). Asking means real throttle with no service brake.
    if (HandbrakeLatched)
    {
        if (OutThrottle > 0.25f && Packet.BrakeAxis < 0.05f)
            HandbrakeLatched = false;
        else
            OutHandbrake = 1.0f;
    }
}

void DeployableUnit::ApplyInputPacket(const InputPacket& Packet, float StepSeconds) noexcept
{
    LatestInput = Packet;
    Cabin.ApplyInputPacket(Packet);
    if (StepSeconds <= 0.0f)
        return;
    SecondsSinceSpawn += StepSeconds;
    Inspection.Advance(StepSeconds);

    float Throttle = 0.0f, Brake = 0.0f, Handbrake = 0.0f;
    ResolveEffectivePedals(Packet, Throttle, Brake, Handbrake);

    float Acceleration = Throttle * ThrottleAcceleration - Brake * BrakeDeceleration;
    if (ForwardSpeedMps > 0.0f)
        Acceleration -= RollingDrag;
    else if (ForwardSpeedMps < 0.0f)
        Acceleration += RollingDrag;
    if (Handbrake > 0.5f)
        Acceleration = -ForwardSpeedMps / std::max(StepSeconds, 1e-4f) * 0.9f;   // the lock wins

    // Reverse is the brake pedal at a standstill, as in the prototype's drive model.
    if (Packet.HasAction(InputActionReverse) && ForwardSpeedMps <= 0.05f && Brake > 0.05f)
        Acceleration = -Brake * 4.0f;

    ForwardSpeedMps = std::clamp(ForwardSpeedMps + Acceleration * StepSeconds, -MaximumReverseMps, MaximumForwardMps);
    if (Handbrake > 0.5f && std::fabs(ForwardSpeedMps) < 0.05f)
        ForwardSpeedMps = 0.0f;

    const float Steer = std::clamp(Packet.SteerAxis, -1.0f, 1.0f);
    const float Travel = ForwardSpeedMps * StepSeconds;
    YawDegrees += Steer * (Travel * 14.0f);   // [°] bicycle-stand-in: heading follows travel and steer demand
    OdometerMetres += std::fabs(Travel);

    const float Yaw = YawDegrees / DegreesPerRadian;
    PositionMetres[0] += std::cos(Yaw) * Travel;
    PositionMetres[1] += std::sin(Yaw) * Travel;
}

void DeployableUnit::Place(const std::array<float, 3>& PositionMetres_, float YawDegrees_) noexcept
{
    PositionMetres = PositionMetres_;
    YawDegrees = YawDegrees_;
}

} // namespace Frontier
