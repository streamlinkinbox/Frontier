//============================================================================================================================================
//                                                       DEPLOYMENTPOINT.H
//============================================================================================================================================
// 📦 Project-owned deployment actors transfer player data and a requested pose to a successful spawn.

#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <utility>

namespace Frontier
{
struct DeploymentPlayerRecord
{
    uint64_t                           Identifier = 1u;
    uint32_t                           Team = 0u;
    std::string                        DisplayName = "Player 1";
    std::map<std::string, std::string>  Properties; // [-] project data, not authentication credentials
};

struct DeploymentPoint
{
    std::string                 Name = "DeploymentPoint";
    std::string                 Archetype = "ControlVehicle";
    DeploymentPlayerRecord      Player;
    std::array<float, 3>        Position{0, 0, 0}; // [m] world space; the spawn adapter fits terrain
    std::array<float, 4>        Rotation{0, 0, 0, 1}; // [-] XYZW quaternion
    bool                        Enabled = true;
    bool                        DeleteAfterSpawn = false; // [-] false permits subsequent respawn requests
};

struct DeploymentReading
{
    uint64_t        PointIdentifier = 0u;
    uint64_t        EntityIdentifier = 0u;
    DeploymentPoint Specification; // [-] owned copy survives removal of the source actor
};

/// 📦 Simulation-thread ownership of deployment actors, independent of vehicle/character construction.
/// err   failed, disabled or reentrant spawns never consume the actor or its player data
class DeploymentSequence
{
public:
    static bool Validate(const DeploymentPoint& Point) noexcept
    {
        if (Point.Name.empty() || Point.Archetype.empty() || Point.Player.Identifier == 0u) return false;
        for (float Coordinate : Point.Position) if (!std::isfinite(Coordinate)) return false;
        double Length = 0.0;
        for (float Component : Point.Rotation)
        {
            if (!std::isfinite(Component)) return false;
            Length += double(Component) * Component;
        }
        return Length > 1e-12 && std::isfinite(Length);
    }

    uint64_t Register(DeploymentPoint Point)
    {
        if (!Validate(Point) || NextIdentifier == std::numeric_limits<uint64_t>::max()) return 0u;
        double Length = 0.0;
        for (float Component : Point.Rotation) Length += double(Component) * Component;
        for (float& Component : Point.Rotation) Component = float(Component / std::sqrt(Length));
        const uint64_t Identifier = NextIdentifier++;
        Points.emplace(Identifier, OccupancyRecord{std::move(Point), false});
        return Identifier;
    }

    [[nodiscard]] std::optional<DeploymentPoint> Query(uint64_t Identifier) const
    {
        const auto Found = Points.find(Identifier);
        return Found == Points.end() ? std::nullopt : std::optional<DeploymentPoint>(Found->second.Specification);
    }

    [[nodiscard]] size_t QueryCount() const noexcept { return Points.size(); }

    bool Retire(uint64_t Identifier)
    {
        const auto Found = Points.find(Identifier);
        if (Found == Points.end() || Found->second.Spawning) return false;
        Points.erase(Found);
        return true;
    }

    /// 📦 The project callback constructs its entity and returns a nonzero identifier only on success.
    /// out   caller-owned player/pose receipt; the project owns the spawned entity, never this sequence
    template<class Constructor>
    std::optional<DeploymentReading> Deploy(uint64_t Identifier, Constructor&& Construct)
    {
        auto Found = Points.find(Identifier);
        if (Found == Points.end() || Found->second.Spawning || !Found->second.Specification.Enabled) return std::nullopt;
        DeploymentReading Reading{Identifier, 0u, Found->second.Specification};
        Found->second.Spawning = true;
        try
        {
            Reading.EntityIdentifier = Construct(Reading.Specification);
        }
        catch (...)
        {
            Found->second.Spawning = false;
            throw;
        }
        Found->second.Spawning = false;
        if (Reading.EntityIdentifier == 0u) return std::nullopt;
        if (Reading.Specification.DeleteAfterSpawn) Points.erase(Found);
        return Reading;
    }

private:
    struct OccupancyRecord
    {
        DeploymentPoint Specification;
        bool            Spawning = false;
    };
    std::map<uint64_t, OccupancyRecord> Points;
    uint64_t                           NextIdentifier = 1u;
};
}
