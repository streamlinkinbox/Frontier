/*==========================================================================================================================================
                                                             SPAWNPOLICY.CPP
==========================================================================================================================================*/

#include "Frontier/Engine/Session/SpawnPolicy.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{
constexpr float DegreesPerRadian = 57.29577951308232f;
}

void SpawnPolicy::ResolvePedalDemand(float SecondsSinceSpawn, float DriverBrake, float DriverHandbrake,
                                     float& OutBrake, float& OutHandbrake) const noexcept
{
    DriverBrake = std::clamp(DriverBrake, 0.0f, 1.0f);
    DriverHandbrake = std::clamp(DriverHandbrake, 0.0f, 1.0f);
    if (!BrakesEnabledAtSpawn)
    {
        // 📝 Brakes disabled at the start: nothing answers until the hold expires, then the driver is live.
        const bool Held = SecondsSinceSpawn < BrakeHoldSeconds;
        OutBrake = Held ? 0.0f : DriverBrake;
        OutHandbrake = Held ? 0.0f : DriverHandbrake;
        return;
    }
    if (SecondsSinceSpawn < BrakeHoldSeconds)
    {
        OutBrake = std::clamp(ParkedPedalShare, 0.0f, 1.0f);
        OutHandbrake = HandbrakeEngagedAtSpawn ? 1.0f : DriverHandbrake;
        return;
    }
    OutBrake = DriverBrake;
    OutHandbrake = DriverHandbrake;
}

SpawnPolicy SpawnPolicy::StudioParked()
{
    SpawnPolicy Policy{};
    Policy.SupportRule = SpawnSupportRule::LevelGround;
    Policy.BrakesEnabledAtSpawn = true;
    Policy.HandbrakeEngagedAtSpawn = true;
    Policy.BrakeHoldSeconds = 0.0f;   // the handbrake latch itself is the rule (DeployableUnit releases it on drive demand)
    Policy.ParkedPedalShare = 0.0f;   // wheels stay free so a turntable turn is honest
    return Policy;
}

SpawnPolicy SpawnPolicy::GridRelease()
{
    SpawnPolicy Policy{};
    Policy.SupportRule = SpawnSupportRule::SurveySlope;
    Policy.BrakesEnabledAtSpawn = true;
    Policy.HandbrakeEngagedAtSpawn = false;
    Policy.BrakeHoldSeconds = 0.0f;
    return Policy;
}

SpawnPolicy SpawnPolicy::SuspendedDrop()
{
    SpawnPolicy Policy{};
    Policy.SupportRule = SpawnSupportRule::SuspendAboveSurface;
    Policy.BrakesEnabledAtSpawn = false;   // nothing to brake against with the wheels in the air
    Policy.HandbrakeEngagedAtSpawn = true;
    Policy.BrakeHoldSeconds = 0.75f;
    return Policy;
}

SpawnPose ResolveSpawnPose(const std::array<float, 3>& PositionMetres, float YawDegrees,
                           const SpawnPolicy& Policy, const SpawnSurfaceSampler& Sampler)
{
    SpawnPose Pose{};
    Pose.PositionMetres = PositionMetres;
    Pose.YawDegrees = YawDegrees;

    const SpawnFootprint& Print = Policy.Footprint;
    if (Policy.SupportRule == SpawnSupportRule::LevelGround || !Sampler)
    {
        Pose.PositionMetres[2] += Print.CentreHeightMetres;
        Pose.Supported = true;
        return Pose;
    }

    // Probe the four tyre patches in the unit's own heading frame.
    const float Yaw = YawDegrees / DegreesPerRadian;
    const float Forward[2] = { std::cos(Yaw), std::sin(Yaw) };
    const float Left[2] = { -std::sin(Yaw), std::cos(Yaw) };
    const float Local[4][2] = { { 1.0f, 1.0f }, { 1.0f, -1.0f }, { -1.0f, 1.0f }, { -1.0f, -1.0f } };
    float Heights[4] = {};
    for (int Corner = 0; Corner < 4; ++Corner)
    {
        const std::array<float, 3> Above = {
            PositionMetres[0] + Forward[0] * Local[Corner][0] * Print.HalfLengthMetres + Left[0] * Local[Corner][1] * Print.HalfWidthMetres,
            PositionMetres[1] + Forward[1] * Local[Corner][0] * Print.HalfLengthMetres + Left[1] * Local[Corner][1] * Print.HalfWidthMetres,
            PositionMetres[2] + 6.0f };
        SpawnSurfaceProbe Probe{};
        if (!Sampler(Above, Probe) || !Probe.HasSurface)
        {
            // No ground under the footprint: keep the authored height, stand level, and say so.
            Pose.PositionMetres[2] += Print.CentreHeightMetres;
            Pose.Supported = false;
            return Pose;
        }
        Heights[Corner] = Probe.ContactPointMetres[2];
    }

    const float FrontHeight = 0.5f * (Heights[0] + Heights[1]);
    const float RearHeight = 0.5f * (Heights[2] + Heights[3]);
    const float LeftHeight = 0.5f * (Heights[0] + Heights[2]);
    const float RightHeight = 0.5f * (Heights[1] + Heights[3]);
    Pose.PitchDegrees = std::atan2(FrontHeight - RearHeight, 2.0f * Print.HalfLengthMetres) * DegreesPerRadian;
    Pose.RollDegrees = std::atan2(LeftHeight - RightHeight, 2.0f * Print.HalfWidthMetres) * DegreesPerRadian;

    const float Slope = std::max(std::fabs(Pose.PitchDegrees), std::fabs(Pose.RollDegrees));
    if (Slope > Print.MaximumSlopeDegrees)
    {
        Pose.PitchDegrees = 0.0f;
        Pose.RollDegrees = 0.0f;
        Pose.Supported = false;
    }
    else
        Pose.Supported = true;

    const float CentreHeight = 0.25f * (Heights[0] + Heights[1] + Heights[2] + Heights[3]);
    Pose.PositionMetres[2] = CentreHeight + Print.CentreHeightMetres
        + (Policy.SupportRule == SpawnSupportRule::SuspendAboveSurface ? Print.HoverMetres : 0.0f);
    return Pose;
}

} // namespace Frontier
