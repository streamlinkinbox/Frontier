/*==========================================================================================================================================
                                                              PHYSICSADAPTER.H
==========================================================================================================================================*/
// 🧩 The PhysicsAdapter, the one doorway every world object takes into a rigid-body solver: bodies (boxes, spheres, convex hulls,
//    fixed triangle sets), forces and torques at points, placement, and ray queries against the bodies. The header holds no solver
//    type; JoltAdapter is the Jolt side. PhysicsSpace owns one adapter, so editor, game, vehicle and test code never see Jolt.

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         TYPES
//------------------------------------------------------------------------------------------------------------------------

enum class PhysicsShape : uint8_t
{
    Box,
    Sphere,
    Hull,        // the convex hull of Points
    Triangles,   // Points indexed three at a time by Triangles; fixed bodies only
};

enum class PhysicsMotion : uint8_t
{
    Fixed,    // never moves: the ground, the scene's colliders, fractured objects
    Moving,   // falls, collides and is pushed
};

inline constexpr uint32_t PhysicsNoBody = std::numeric_limits<uint32_t>::max();

// 📝 One body as it enters the space (z up, metres). Orientation is a unit quaternion (x, y, z, w). Points and the body's position
//    are in the body's own axes about its origin; with an authored inertia the origin is the centre of mass.
struct PhysicsBodyDescriptor
{
    uint64_t                         Record              = 0;                       // [-] the level record it stands for; 0: none
    PhysicsShape                     Shape               = PhysicsShape::Box;
    std::array<double, 3>            HalfExtentMetres    = { 0.5, 0.5, 0.5 };       // [m] box; a sphere's radius is [0]
    std::vector<std::array<float, 3>> Points{};                                     // [m] hull points or triangle corners
    std::vector<uint32_t>            Triangles{};                                   // [-] three corner indices per triangle
    std::array<double, 3>            PositionMetres      = { 0.0, 0.0, 0.0 };       // [m] the shape's origin
    std::array<double, 4>            Orientation         = { 0.0, 0.0, 0.0, 1.0 };  // [-]
    PhysicsMotion                    Motion              = PhysicsMotion::Moving;
    double                           MassKilograms       = 0.0;                     // [kg] 0: from the shape at 1000 kg/m³
    std::array<double, 3>            InertiaKgm2         = { 0.0, 0.0, 0.0 };       // [kg·m²] principal, about the origin; 0: shape
    double                           Friction            = 0.5;                     // [-]
    double                           Restitution         = 0.2;                     // [-]
    double                           LinearDamping       = 0.05;                    // [1/s]
    double                           AngularDamping      = 0.05;                    // [1/s]
    std::array<double, 3>            LinearVelocityMps   = { 0.0, 0.0, 0.0 };       // [m/s]
    std::array<double, 3>            AngularVelocityRads = { 0.0, 0.0, 0.0 };       // [rad/s]
    bool                             Sleeps              = true;                    // [-] false keeps a driven body awake
    bool                             Swept               = false;                   // [-] continuous collision for fast bodies
};

struct PhysicsBodyReading
{
    std::array<double, 3> PositionMetres      = { 0.0, 0.0, 0.0 };
    std::array<double, 4> Orientation         = { 0.0, 0.0, 0.0, 1.0 };
    std::array<double, 3> LinearVelocityMps   = { 0.0, 0.0, 0.0 };
    std::array<double, 3> AngularVelocityRads = { 0.0, 0.0, 0.0 };
    bool                  Awake               = false;
};

// 📝 A ray from Origin along the unit Direction for Length metres; IgnoredBody (a body index) is passed through, e.g. the chassis
//    whose wheels ask where the road is.
struct PhysicsRay
{
    std::array<double, 3> OriginMetres = { 0.0, 0.0, 0.0 };
    std::array<double, 3> Direction    = { 0.0, 0.0, -1.0 };
    double                LengthMetres = 1.0;
    uint32_t              IgnoredBody  = PhysicsNoBody;
};

struct PhysicsRayHit
{
    std::array<double, 3> PointMetres    = { 0.0, 0.0, 0.0 };
    std::array<double, 3> Normal         = { 0.0, 0.0, 1.0 };   // [-] the surface's outward normal at the point
    double                DistanceMetres = 0.0;
    uint32_t              Body           = PhysicsNoBody;
};

struct PhysicsSpaceSettings
{
    std::array<double, 3> GravityMps2   = { 0.0, 0.0, -9.81 };   // [m/s²] z up
    uint32_t              MaximumBodies = 8192;
    uint32_t              Workers       = 0;                     // [-] job threads; 0: the machine's choice (up to 4)
};

// 📝 One edge of a collider drawn in world space, for the viewport's collision wireframe. Body is the space index it belongs to;
//    Moving tells the overlay whether to tint it as a dynamic body or a fixed collider.
struct PhysicsWireSegment
{
    std::array<double, 3> Start{};    // [m]
    std::array<double, 3> End{};      // [m]
    uint32_t              Body   = 0; // [-] the space body index
    bool                  Moving = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PHYSICS ADAPTER
//------------------------------------------------------------------------------------------------------------------------

// 📝 Calls come from one thread at a time (the physics thread while it runs). Body indices are 0, 1, 2 … in the order added.
class PhysicsAdapter
{
  public:
    virtual ~PhysicsAdapter() = default;

    // 📝 The body's index, or PhysicsNoBody when the solver is full or the shape is degenerate.
    virtual uint32_t AddBody(const PhysicsBodyDescriptor& Descriptor) = 0;
    // 📝 Arranges the broad phase once the bodies are in (stepping works without it, only slower at first).
    virtual void Prepare() = 0;
    // 📝 One step of Seconds, split into CollisionSteps collision sub-steps. Forces added since the last step act during it.
    virtual void Step(double Seconds, uint32_t CollisionSteps) = 0;

    [[nodiscard]] virtual PhysicsBodyReading ReadBody(uint32_t Body) const = 0;
    // 📝 Moves a body to Pose (position, orientation and both velocities), waking it.
    virtual void PlaceBody(uint32_t Body, const PhysicsBodyReading& Pose) = 0;
    // 📝 A force [N] at a world point [m], held until the next step; the torque about the centre of mass follows from the point.
    virtual void ApplyForce(uint32_t Body, const std::array<double, 3>& Force, const std::array<double, 3>& Point) = 0;
    virtual void ApplyTorque(uint32_t Body, const std::array<double, 3>& Torque) = 0;
    // 📝 The nearest body the ray meets; false when it meets none.
    [[nodiscard]] virtual bool CastRay(const PhysicsRay& Ray, PhysicsRayHit& Hit) const = 0;
    // 📝 A moving body's new mass [kg] and principal inertia about its centre of mass [kg·m²] (fuel burning off a chassis).
    virtual void AssignMass(uint32_t Body, double MassKilograms, const std::array<double, 3>& InertiaKgm2) = 0;
    // 📝 Runs Task(0 … Count − 1) on the solver's job threads and waits; only between steps (e.g. the soft tyre carcasses).
    virtual void Dispatch(uint32_t Count, const std::function<void(uint32_t)>& Task) = 0;

    [[nodiscard]] virtual size_t   QueryAwakeCount() const          = 0;
    [[nodiscard]] virtual uint64_t QueryContactsAdded() const noexcept = 0;
    [[nodiscard]] virtual uint32_t QueryWorkers() const noexcept     = 0;
    [[nodiscard]] virtual const char* QuerySolverName() const noexcept = 0;
};

// 📝 The Jolt adapter (JoltAdapter.cpp): the only solver today.
[[nodiscard]] std::unique_ptr<PhysicsAdapter> ConstructJoltAdapter(const PhysicsSpaceSettings& Settings);

} // namespace Frontier
