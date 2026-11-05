#pragma once
/*==========================================================================================================================================
                                                           DEPLOYABLEUNIT.H
==========================================================================================================================================*/
// 🧩 The pawn class: a unit that can be deployed into the world and driven by InputPackets — here, the
//    ControlVehicle. It owns its pose and its stand: how it spawns is its own function (Deploy, under the
//    session's SpawnPolicy), so it stands the same way regardless of the slope it is placed on. An Agent
//    (Scene/Agent.h) is the control stand that feeds it packets; the unit never knows who is driving.

#include "Frontier/Engine/Input/InputPacket.h"
#include "Frontier/Engine/Effects/VehicleInspection.h"
#include "Frontier/Engine/Scene/VehicleCabin.h"
#include "Frontier/Engine/Session/SpawnPolicy.h"

#include <array>
#include <cstdint>
#include <string>

namespace Frontier
{

enum class UnitCategory : uint8_t { Vehicle = 0, Character = 1, Spectator = 2 };

// 📝 Car-studio appearance carried with the unit (and across level transitions): what the configurator edits.
struct VehicleUnitConfiguration
{
    std::string PaintName = "Graphite";
    std::array<float, 3> PaintAlbedo = { 0.030f, 0.032f, 0.036f };
    std::string RimFinishName = "Satin Gunmetal";
    std::string WheelLightName = "Cyan";
    float RideHeightOffsetMetres = 0.0f;
};

class DeployableUnit
{
  public:
    DeployableUnit() = default;
    explicit DeployableUnit(UnitCategory InitialCategory) : Category(InitialCategory) {}

    [[nodiscard]] UnitCategory QueryCategory() const noexcept { return Category; }
    [[nodiscard]] const std::array<float, 3>& QueryPositionMetres() const noexcept { return PositionMetres; }
    [[nodiscard]] float QueryYawDegrees() const noexcept { return YawDegrees; }
    [[nodiscard]] float QueryPitchDegrees() const noexcept { return PitchDegrees; }
    [[nodiscard]] float QueryRollDegrees() const noexcept { return RollDegrees; }
    [[nodiscard]] float QueryForwardSpeedMps() const noexcept { return ForwardSpeedMps; }
    [[nodiscard]] float QueryOdometerMetres() const noexcept { return OdometerMetres; }
    [[nodiscard]] const SpawnPose& QuerySpawnStand() const noexcept { return SpawnStand; }
    [[nodiscard]] const SpawnPolicy& QuerySpawnPolicy() const noexcept { return SpawnRules; }
    [[nodiscard]] float QuerySecondsSinceSpawn() const noexcept { return SecondsSinceSpawn; }
    [[nodiscard]] bool IsHandbrakeLatched() const noexcept { return HandbrakeLatched; }
    [[nodiscard]] const VehicleUnitConfiguration& QueryVehicleConfiguration() const noexcept { return VehicleSpec; }
    [[nodiscard]] const InputPacket& QueryLatestInput() const noexcept { return LatestInput; }
    [[nodiscard]] const VehicleCabin& QueryCabin() const noexcept { return Cabin; }
    [[nodiscard]] VehicleCabin& AccessCabin() noexcept { return Cabin; }
    [[nodiscard]] const VehicleInspection& QueryInspection() const noexcept { return Inspection; }
    [[nodiscard]] VehicleInspection& AccessInspection() noexcept { return Inspection; }

    void AssignVehicleConfiguration(const VehicleUnitConfiguration& Spec) { VehicleSpec = Spec; }
    void AssignSpawnPolicy(const SpawnPolicy& Rules) noexcept { SpawnRules = Rules; }

    // 📝 The deployment function: resolves the stand under the spawn rules (footprint survey unless the rules
    //    trust flat ground) and places the unit on it, parked or live exactly as the rules say.
    void Deploy(const std::array<float, 3>& PositionMetres_, float YawDegrees_,
                const SpawnSurfaceSampler& Sampler) noexcept;

    // 📝 Stands the unit back at its spawn anchor (what RespawnPolicy calls when it allows a respawn).
    void ReturnToSpawnStand() noexcept;

    // 📝 Effective pedals: driver demand blended with the spawn brake rules, the parked handbrake latch
    //    included. The latch releases the first time the driver genuinely asks for drive.
    void ResolveEffectivePedals(const InputPacket& Packet, float& OutThrottle, float& OutBrake, float& OutHandbrake) noexcept;

    // 📝 One step of being driven. Kinematic vehicle stand-in (the physics vehicle replaces the integrator,
    //    not this contract): throttle/brake move it, steering turns it in proportion to travel.
    void ApplyInputPacket(const InputPacket& Packet, float StepSeconds) noexcept;

    void Place(const std::array<float, 3>& PositionMetres_, float YawDegrees_) noexcept;

  private:
    UnitCategory Category = UnitCategory::Vehicle;
    std::array<float, 3> PositionMetres = { 0.0f, 0.0f, 0.53f };
    float YawDegrees = 0.0f;
    float PitchDegrees = 0.0f;
    float RollDegrees = 0.0f;
    float ForwardSpeedMps = 0.0f;
    float OdometerMetres = 0.0f;

    SpawnPolicy SpawnRules{};
    SpawnPose SpawnStand{};
    float SecondsSinceSpawn = 0.0f;
    bool HandbrakeLatched = false;
    VehicleUnitConfiguration VehicleSpec{};
    VehicleCabin Cabin{};   // steering wheel + (empty) tablet, driven by the same packets
    VehicleInspection Inspection{};   // visual effect only (Solid/Ghost/Wireframe/X-ray Scan)
    InputPacket LatestInput{};
};

} // namespace Frontier
