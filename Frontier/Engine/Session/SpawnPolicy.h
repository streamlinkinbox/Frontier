#pragma once
/*==========================================================================================================================================
                                                              SPAWNPOLICY.H
==========================================================================================================================================*/
// 🧩 How a DeployableUnit is stood in the world and handed over: which surface rules place it (so a vehicle
//    spawns the same way on a flat studio pad, a kerb, a bank or a cross-slope — its own deployment function,
//    not the level's), which brakes rule the opening seconds (enabled, disabled for a hold, or parked on the
//    handbrake), and which wheels drive. A SessionPolicy carries one of these; RespawnPolicy decides whether
//    a unit comes back at all, this decides how it stands every time it does.

#include <array>
#include <cstdint>
#include <functional>

namespace Frontier
{

enum class SpawnSupportRule : uint8_t
{
    SurveySlope = 0,         // probe the ground under the whole footprint; rest on the surface's own pitch/roll
    LevelGround = 1,         // trust the authored pose as flat (studio pad, scripted stand)
    SuspendAboveSurface = 2  // start at a hover height above the surface and settle
};

enum class DrivenWheelRule : uint8_t { RearDriven = 0, FrontDriven = 1, AllDriven = 2 };

// 📝 The ground envelope one survey covers: four tyre patches rather than one point under the centre of mass.
struct SpawnFootprint
{
    float HalfLengthMetres = 1.70f;    // [m] half the wheelbase envelope
    float HalfWidthMetres = 1.05f;     // [m] half the track envelope
    float CentreHeightMetres = 0.53f;  // [m] centre of mass above the surface at rest
    float HoverMetres = 0.60f;         // [m] extra height a SuspendAboveSurface stand starts from
    float MaximumSlopeDegrees = 42.0f; // [°] steepest stand the survey accepts
};

struct SpawnSurfaceProbe
{
    std::array<float, 3> ContactPointMetres = { 0.0f, 0.0f, 0.0f };
    bool HasSurface = false;
};

// 📝 Asks the world where the ground is under a point (a physics ray in play, a flat plane in validation).
using SpawnSurfaceSampler = std::function<bool(const std::array<float, 3>& AbovePointMetres, SpawnSurfaceProbe& OutProbe)>;

// 📝 The stand the deployment function resolves: position, heading, and the surface's own pitch and roll.
struct SpawnPose
{
    std::array<float, 3> PositionMetres = { 0.0f, 0.0f, 0.0f };
    float YawDegrees = 0.0f;
    float PitchDegrees = 0.0f;
    float RollDegrees = 0.0f;
    bool Supported = false;   // false: no surface under the footprint — the authored pose was kept, flagged
};

struct SpawnPolicy
{
    SpawnSupportRule SupportRule = SpawnSupportRule::SurveySlope;
    DrivenWheelRule DrivenWheels = DrivenWheelRule::RearDriven;
    bool BrakesEnabledAtSpawn = true;     // false: neither pedal answers the driver until the hold expires
    bool HandbrakeEngagedAtSpawn = false; // parked: stays latched until the driver asks for drive
    float BrakeHoldSeconds = 0.0f;        // [s] how long spawn brake rules outrank the driver
    float ParkedPedalShare = 0.0f;        // [0..1] service-brake share applied while parked rules outrank
    SpawnFootprint Footprint{};

    [[nodiscard]] bool OutranksDriver(float SecondsSinceSpawn) const noexcept
    {
        return SecondsSinceSpawn < BrakeHoldSeconds || HandbrakeEngagedAtSpawn;
    }

    // 📝 Blends driver demand with the spawn brake rules. Brakes disabled at spawn: both pedals ignored while
    //    the hold runs. Parked: pedal share + latched handbrake until the hold expires.
    void ResolvePedalDemand(float SecondsSinceSpawn, float DriverBrake, float DriverHandbrake,
                            float& OutBrake, float& OutHandbrake) const noexcept;

    [[nodiscard]] static SpawnPolicy StudioParked();   // lobby: level pad, parked handbrake, wheels free
    [[nodiscard]] static SpawnPolicy GridRelease();    // proving grounds / race grid: surveyed stand, live handover
    [[nodiscard]] static SpawnPolicy SuspendedDrop();  // hover drop: brakes disabled for a short handover
};

// 📝 The vehicle's own deployment function: stands a unit at (PositionMetres, YawDegrees) under these rules,
//    regardless of slope. Pure geometry against the sampler — the same code path in play and in validation.
[[nodiscard]] SpawnPose ResolveSpawnPose(const std::array<float, 3>& PositionMetres, float YawDegrees,
                                         const SpawnPolicy& Policy, const SpawnSurfaceSampler& Sampler);

} // namespace Frontier
