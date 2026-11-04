/*==========================================================================================================================================
                                                               PHYSICSSPACE.H
==========================================================================================================================================*/
// 🧩 PhysicsSpace, the world rigid bodies live in: boxes, spheres, hulls and fixed triangle sets that fall under gravity, collide,
//    come to rest and sleep, pushed by forces and asked where rays meet them. Every call goes through the space's PhysicsAdapter
//    (JoltAdapter today); the header holds no Jolt type, so editor, game, vehicle and test code include it freely.

#pragma once

#include "Frontier/Extensions/Physics/PhysicsAdapter.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace Frontier
{

class PhysicsSpace
{
  public:
    explicit PhysicsSpace(const PhysicsSpaceSettings& Settings = {});
    ~PhysicsSpace();

    PhysicsSpace(const PhysicsSpace&)            = delete;
    PhysicsSpace& operator=(const PhysicsSpace&) = delete;

    // 📝 The body's index in the space (0, 1, 2 …), or PhysicsNoBody when the space is full or the shape is degenerate.
    uint32_t AddBody(const PhysicsBodyDescriptor& Descriptor);
    // 📝 Arranges the broad phase once the bodies are in (stepping works without it, only slower at first).
    void Prepare();
    // 📝 One step of DeltaSeconds, split into CollisionSteps sub-steps (more for fast bodies).
    void Step(double DeltaSeconds, uint32_t CollisionSteps = 1);

    [[nodiscard]] PhysicsBodyReading ReadBody(uint32_t Index) const;
    void PlaceBody(uint32_t Index, const PhysicsBodyReading& Pose);
    void ApplyForce(uint32_t Index, const std::array<double, 3>& Force, const std::array<double, 3>& Point);
    void ApplyTorque(uint32_t Index, const std::array<double, 3>& Torque);
    [[nodiscard]] bool CastRay(const PhysicsRay& Ray, PhysicsRayHit& Hit) const;
    void AssignMass(uint32_t Index, double MassKilograms, const std::array<double, 3>& InertiaKgm2);
    void Dispatch(uint32_t Count, const std::function<void(uint32_t)>& Task);

    // 📝 The colliders' edges in world space at their current poses, for the viewport's collision wireframe: a box's twelve
    //    edges, a triangle set's facet edges, a hull's outline, a sphere's three great circles. Fixed and moving bodies alike.
    [[nodiscard]] std::vector<PhysicsWireSegment> ResolveWireframe() const;

    [[nodiscard]] const PhysicsBodyDescriptor& QueryDescriptor(uint32_t Index) const { return Descriptors[Index]; }
    [[nodiscard]] size_t                       QueryBodyCount() const noexcept { return Descriptors.size(); }
    [[nodiscard]] size_t                       QueryAwakeCount() const;
    [[nodiscard]] uint64_t                     QueryContactsAdded() const noexcept;
    [[nodiscard]] double                       QuerySimulatedSeconds() const noexcept { return SimulatedSeconds; }
    [[nodiscard]] uint32_t                     QueryWorkers() const noexcept;
    [[nodiscard]] const char*                  QuerySolverName() const noexcept;

  private:
    // 📝 The shape geometry kept for the collision wireframe (the descriptor drops its points and triangles once the solver holds
    //    them). One entry per body, in the same order as Descriptors.
    struct WireframeShape
    {
        PhysicsShape                      Shape = PhysicsShape::Box;
        std::array<double, 3>             HalfExtentMetres{};
        std::vector<std::array<float, 3>> Points{};
        std::vector<uint32_t>             Triangles{};
    };

    std::unique_ptr<PhysicsAdapter>    Adapter;
    std::vector<PhysicsBodyDescriptor> Descriptors;
    std::vector<WireframeShape>        WireframeShapes;
    double                             SimulatedSeconds = 0.0;
};

// 📝 The level's Euler convention (OrientationDegrees: R = Rz(γ) · Ry(β) · Rx(α)) to and from a unit quaternion (x, y, z, w).
[[nodiscard]] std::array<double, 4> ResolvePhysicsOrientation(const std::array<double, 3>& OrientationDegrees) noexcept;
[[nodiscard]] std::array<double, 3> ResolvePhysicsEulerDegrees(const std::array<double, 4>& Orientation) noexcept;
// 📝 A vector turned by a unit quaternion.
[[nodiscard]] std::array<double, 3> RotatePhysicsVector(const std::array<double, 4>& Orientation, const std::array<double, 3>& Vector) noexcept;

} // namespace Frontier
