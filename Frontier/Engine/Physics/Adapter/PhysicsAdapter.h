/*============================================================================================================================================
                                                             PHYSICSADAPTER.H
============================================================================================================================================*/
// 🧩 Backend-neutral rigid-world interface: bodies with simple bound colliders, impulses, queries and fixed stepping —
//    implemented by JoltAdapter (Jolt Physics 5.2.0) and NullPhysicsAdapter (dependency-free fallback). Fracture pieces
//    register as STATIC objects with simple bounds; no fracture dynamics are wired yet.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       TOKENS & TYPES
//------------------------------------------------------------------------------------------------------------------------

// 🔍 Stale-detecting body identity issued by an adapter (index + generation packed; 0 = invalid).
using PhysicsBodyToken = uint64_t;
inline constexpr PhysicsBodyToken PhysicsBodyInvalid = 0;

// 📝 Motion category. Kinematic bodies are driven by the host (vehicle chassis proxy); Dynamic bodies integrate under
//    forces; Static bodies never move (walls, ground, fracture pieces with bound colliders).
enum class PhysicsBodyMotion : uint8_t
{
    Static    = 0,
    Kinematic = 1,
    Dynamic   = 2
};

// 📝 Simple bound colliders only — fracture chunks take their enclosing box until fracture dynamics arrive.
enum class PhysicsColliderShape : uint8_t
{
    BoundBox        = 0,   // [-] axis-aligned half extents (body-local)
    BoundingSphere  = 1,   // [-] sphere bound
    BoundingCapsule = 2,   // [-] capsule along +Y (Jolt's capsule axis)
    ConvexHull      = 3    // [-] point-cloud hull (vehicle proxy uses the demo's fallback hull points)
};

struct PhysicsColliderDescription
{
    PhysicsColliderShape Shape = PhysicsColliderShape::BoundBox; // [-]
    double PositionOffset[3]    = { 0.0, 0.0, 0.0 };  // [m] body-local
    double HalfExtents[3]       = { 0.5, 0.5, 0.5 };  // [m] BoundBox
    double Radius               = 0.5;                // [m] sphere/capsule/hull margin
    double CapsuleHalfHeight    = 0.25;               // [m] cylindrical half height
    std::vector<double> HullPositions;                // [m] x,y,z triples for ConvexHull
};

struct PhysicsBodyDescription
{
    PhysicsBodyMotion Motion                       = PhysicsBodyMotion::Static; // [-]
    double            Position[3]                  = { 0.0, 0.0, 0.0 };         // [m] world
    double            Orientation[4]               = { 0.0, 0.0, 0.0, 1.0 };    // [-] quaternion x,y,z,w
    double            LinearVelocity[3]            = { 0.0, 0.0, 0.0 };         // [m/s]
    double            AngularVelocity[3]           = { 0.0, 0.0, 0.0 };         // [rad/s]
    double            MassKilograms                = 1.0;                       // [kg] dynamic bodies
    double            Friction                     = 0.6;                       // [-]
    double            Restitution                  = 0.4;                       // [-]
    double            LinearDamping                = 0.05;                      // [1/s]
    double            AngularDamping               = 0.05;                      // [1/s]
    double            GravityShare                 = 1.0;                       // [-] 0 = levitating proxy
    bool              FractureBoundStatic          = false;                     // [-] fracture piece: static bounds-only
    std::vector<PhysicsColliderDescription> Colliders;
};

// 📝 World tuning fixed at Initialize; substeps are the adapter's internal integration slices per Advance call.
struct PhysicsWorldConfiguration
{
    double Gravity[3]                = { 0.0, 0.0, -9.81 }; // [m/s²]
    double SubstepSeconds            = 1.0 / 240.0;         // [s] internal integration slice Δτ
    int    MaxSubstepsPerStep        = 8;                   // [-] clamp for spiral-of-death protection
    int    VelocitySolverIterations  = 8;                   // [-]
    int    PositionSolverIterations  = 2;                   // [-]
    bool   DeterministicCycle        = true;                // [-] consume whole accumulated time each Advance
};

// 🔍 Ray cast outcome.
struct PhysicsRayHit
{
    bool             Hit          = false;                 // [-]
    PhysicsBodyToken Body         = PhysicsBodyInvalid;    // [-]
    double           Distance     = 0.0;                   // [m]
    double           Position[3]  = { 0.0, 0.0, 0.0 };     // [m] world
    double           Normal[3]    = { 0.0, 0.0, 1.0 };     // [-] surface unit normal
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PHYSICS ADAPTER INTERFACE
//------------------------------------------------------------------------------------------------------------------------

class PhysicsAdapter
{
public:
    virtual ~PhysicsAdapter() = default;

    virtual bool               Initialize(const PhysicsWorldConfiguration& Configuration) = 0;
    virtual void               Shutdown() noexcept = 0;
    [[nodiscard]] virtual const char* QueryAdapterName() const noexcept = 0;
    [[nodiscard]] virtual const PhysicsWorldConfiguration& QueryWorldConfiguration() const noexcept = 0;

    // Bodies.
    virtual PhysicsBodyToken CreateBody(const PhysicsBodyDescription& Description) = 0;
    virtual void             DestroyBody(PhysicsBodyToken Body) = 0;
    [[nodiscard]] virtual size_t QueryBodyCount() const noexcept = 0;

    // Kinematics.
    virtual void ConfigureBodyTransform(PhysicsBodyToken Body, const double Position[3], const double Orientation[4]) = 0;
    virtual void ConfigureBodyVelocities(PhysicsBodyToken Body, const double Linear[3], const double Angular[3]) = 0;
    virtual void QueryBodyTransform(PhysicsBodyToken Body, double OutPosition[3], double OutOrientation[4]) const = 0;
    virtual void QueryBodyVelocities(PhysicsBodyToken Body, double OutLinear[3], double OutAngular[3]) const = 0;

    // Forces.
    virtual void ApplyLinearImpulse(PhysicsBodyToken Body, const double Impulse[3]) = 0;
    virtual void ApplyImpulseAtPoint(PhysicsBodyToken Body, const double Impulse[3], const double Point[3]) = 0;

    // Queries.
    virtual bool CastRay(const double Origin[3], const double Direction[3], double Length, PhysicsRayHit& OutHit) const = 0;

    // 📝 Advance the accumulated host time in fixed Δτ substeps (see PhysicsWorldConfiguration).
    virtual void Advance(double Seconds) = 0;

    // 📝 Vehicle proxy exchange: the collision-induced linear Δv [m/s] accumulated for a kinematic proxy (cone hits,
    //    wall impacts) since the previous Collect; the host solver applies it and the accumulator resets. Returns
    //    |Δv| for assertions.
    virtual double CollectProxyDeltaVelocity(PhysicsBodyToken Body, double OutDelta[3]) = 0;
};

} // namespace Frontier
