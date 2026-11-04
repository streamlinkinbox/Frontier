/*==========================================================================================================================================
                                                              PORTALSPACE.H
==========================================================================================================================================*/
// 🧩 The portal apertures' world: paired one-sided rectangles, the transit transform between them, crossing detection and the
//    clipped clone of a body that straddles an aperture. A body crossing aperture A emerges from its peer B, scaled by the ratio
//    of their extents; while it straddles, only the entering half collides and the far side is a transparent clone. The math is
//    pure (no solver, no renderer) so the editor, the physics step and the validation share one implementation.

#pragma once

#include "Frontier/Engine/Scene/GeometryLayout.h"
#include "Frontier/Engine/Scene/PortalRecord.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 One aperture resolved into world space: its placement, front normal (local +Z turned into the world), and half extents.
struct PortalAperture
{
    uint64_t              Record = 0;                       // [-] the portal record's token value
    std::string           Name{};                           // [-] the record name (pairs match by name)
    std::string           LinkedName{};                     // [-] the peer's record name
    PlacementMatrix       Placement{};                      // [-] world from local (column-major)
    std::array<double, 3> Position{};                       // [m] the aperture centre
    std::array<double, 3> Normal{ 0.0, 0.0, 1.0 };          // [-] the front normal (local +Z in world)
    std::array<double, 3> Right{ 1.0, 0.0, 0.0 };           // [-] local +X in world
    std::array<double, 3> Up{ 0.0, 1.0, 0.0 };              // [-] local +Y in world
    double                HalfWidth  = 1.0;                 // [m]
    double                HalfHeight = 1.5;                 // [m]
};

// 📝 A body's pose and motion as the transit reads and writes them.
struct PortalBodyPose
{
    std::array<double, 3> Position{};                       // [m]
    std::array<double, 4> Orientation{ 0.0, 0.0, 0.0, 1.0 }; // [-] unit quaternion (x, y, z, w)
    std::array<double, 3> LinearVelocity{};                 // [m/s]
    std::array<double, 3> AngularVelocity{};                // [rad/s]
};

// 📝 The result of carrying a pose through a pair: the emerging pose, its velocities and the scale applied.
struct PortalTransit
{
    PortalBodyPose        Pose{};
    double                Scale = 1.0;                      // [-] the aperture ratio applied to offsets and velocities
};

// 📝 Whether a step crossed an aperture, and where.
struct PortalCrossing
{
    bool                  Crossed       = false;
    bool                  WithinAperture = false;           // [-] the crossing point lies inside the aperture rectangle
    std::array<double, 3> Point{};                          // [m] the plane crossing point
    double                SignedBefore  = 0.0;              // [m] signed distance before the step (+ front)
    double                SignedAfter   = 0.0;              // [m] signed distance after the step
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       PORTAL SPACE
//------------------------------------------------------------------------------------------------------------------------

class PortalSpace
{
  public:
    // 📝 Resolves the level's Portal records into apertures and pairs them by name (A links to B and B links to A).
    void Configure(const WorldSpace& World);

    [[nodiscard]] const std::vector<PortalAperture>& QueryApertures() const noexcept { return Apertures_; }
    [[nodiscard]] size_t QueryPairCount() const noexcept { return Pairs_.size(); }

    // 📝 The peer aperture of the given one, or null when unpaired.
    [[nodiscard]] const PortalAperture* QueryPeer(const PortalAperture& Aperture) const noexcept;

    // 📝 Carries a pose through From → To: position, orientation and both velocities, scaled by the aperture ratio.
    [[nodiscard]] PortalTransit ResolveTransit(const PortalBodyPose& Pose, const PortalAperture& From, const PortalAperture& To) const;

    // 📝 Whether the step from Before to After crossed the aperture's plane front-to-back, and whether the crossing is inside it.
    [[nodiscard]] PortalCrossing DetectCrossing(const std::array<double, 3>& Before, const std::array<double, 3>& After,
                                                const PortalAperture& Aperture) const;

    // 📝 The clone pose for a straddling body: the transit of its current pose, drawn clipped to the peer aperture (the far,
    //    transparent half). The entering half stays the real body on the near side.
    [[nodiscard]] PortalBodyPose ResolveClone(const PortalBodyPose& Pose, const PortalAperture& From, const PortalAperture& To) const;

  private:
    std::vector<PortalAperture>             Apertures_{};
    std::unordered_map<uint64_t, uint64_t>  Pairs_{};      // [-] aperture record token → peer record token
};

// 📝 The aperture ratio (To / From) by width, clamped to a sane range so a tiny aperture cannot blow a body up without bound.
[[nodiscard]] double ResolvePortalScale(const PortalAperture& From, const PortalAperture& To) noexcept;

} // namespace Frontier
