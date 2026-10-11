//============================================================================================================================================
// 📦 Frontier/PhysicalDynamics/RigidBodySolver.cpp — Jolt Rigid-Body World Implementation (the only translation unit that sees JPH::)
//============================================================================================================================================

#include "RigidBodySolver.h"

// Jolt.h must precede every other Jolt header; the library derives its feature/ISA defines from the compiler flags, so this
//    translation unit has to be compiled with the same -m<isa> / NDEBUG set as libJolt (see Tools/Build/BuildJolt.*).
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/PlaneShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>

// Phase 0 (vehicle) additions: scene queries, per-point forces, the strut spring constraint, and terrain heightfields.
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollector.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Body/BodyFilter.h>   // BodyFilter/IgnoreSingleBodyFilter live under Body/, not Collision/ (upstream Jolt layout)
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyLockMulti.h>
#include <Jolt/Physics/Constraints/Constraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/SpringSettings.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <thread>

JPH_SUPPRESS_WARNINGS

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                          PROCESS-WIDE JOLT REGISTRATION
//------------------------------------------------------------------------------------------------------------------------
// Jolt's factory and type registry are global; several solvers may coexist (editor preview + game world), so the first
//    Bring() registers and the last Retire() unregisters.

namespace {

std::mutex   RegistrationMutex;
uint32_t     RegistrationCount = 0u;

void JoltTrace(const char* Format, ...)
{
    va_list Arguments;
    va_start(Arguments, Format);
    char Line[1024];
    std::vsnprintf(Line, sizeof(Line), Format, Arguments);
    va_end(Arguments);
    std::fprintf(stderr, "[Jolt] %s\n", Line);
}

#ifdef JPH_ENABLE_ASSERTS
bool JoltAssertFailed(const char* Expression, const char* Message, const char* File, JPH::uint Line)
{
    std::fprintf(stderr, "[Jolt] %s:%u: (%s) %s\n", File, Line, Expression, Message != nullptr ? Message : "");
    return true;   // break into the debugger
}
#endif

bool RegisterJolt() noexcept
{
    std::lock_guard<std::mutex> Guard(RegistrationMutex);
    if (RegistrationCount++ > 0u) return true;

    JPH::RegisterDefaultAllocator();
    JPH::Trace = JoltTrace;
    JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = JoltAssertFailed;)
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();   // aborts with a trace when libJolt and this TU were compiled with different defines
    return JPH::Factory::sInstance != nullptr;
}

void UnregisterJolt() noexcept
{
    std::lock_guard<std::mutex> Guard(RegistrationMutex);
    if (RegistrationCount == 0u || --RegistrationCount > 0u) return;

    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  LAYER SCHEME
//------------------------------------------------------------------------------------------------------------------------
// Two object layers, two broad-phase trees: static geometry never has to be re-inserted when dynamic bodies move.

namespace ObjectLayers
{
    constexpr JPH::ObjectLayer NonMoving = 0;
    constexpr JPH::ObjectLayer Moving    = 1;
    constexpr JPH::uint        Count     = 2;
}

namespace BroadPhaseLayers
{
    constexpr JPH::BroadPhaseLayer NonMoving(0);
    constexpr JPH::BroadPhaseLayer Moving(1);
    constexpr JPH::uint            Count = 2;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 TYPE CONVERSION
//------------------------------------------------------------------------------------------------------------------------

inline JPH::Vec3    ToJolt(const Vector3& V) noexcept        { return JPH::Vec3(V.x, V.y, V.z); }
inline JPH::RVec3   ToJoltReal(const Vector3& V) noexcept    { return JPH::RVec3(JPH::Real(V.x), JPH::Real(V.y), JPH::Real(V.z)); }
inline JPH::Quat    ToJolt(const Quaternion& Q) noexcept     { const JPH::Quat J(Q.x, Q.y, Q.z, Q.w); return J.IsNormalized() ? J : J.Normalized(); }
inline Vector3      FromJolt(JPH::Vec3Arg V) noexcept        { return Vector3{ V.GetX(), V.GetY(), V.GetZ() }; }
#ifdef JPH_DOUBLE_PRECISION
inline Vector3      FromJolt(JPH::RVec3Arg V) noexcept       { return Vector3{ float(V.GetX()), float(V.GetY()), float(V.GetZ()) }; }
#endif
inline Quaternion   FromJolt(JPH::QuatArg Q) noexcept        { return Quaternion{ Q.GetX(), Q.GetY(), Q.GetZ(), Q.GetW() }; }

inline JPH::EMotionType ToJolt(RigidBodyMotionCategory M) noexcept
{
    switch (M)
    {
        case RigidBodyMotionCategory::Static:    return JPH::EMotionType::Static;
        case RigidBodyMotionCategory::Kinematic: return JPH::EMotionType::Kinematic;
        default:                                 return JPH::EMotionType::Dynamic;
    }
}

// Frontier is +Z up; Jolt's capsule and cylinder run along their local +Y, so those two are wrapped in a +90° rotation about X
//    (local Y → local Z). Returns a null ref with an explanation when the description is unusable.
JPH::ShapeRefC BuildShape(const CollisionShapeDescription& D, std::string& Refusal) noexcept
{
    constexpr float MinimumSize = 1.0e-4f;   // [m] below this Jolt's convex radius logic degenerates
    JPH::ShapeSettings::ShapeResult Result;
    switch (D.Category)
    {
        case CollisionShapeCategory::Plane:
        {
            const Vector3 N = D.PlaneNormal.Normalized();
            if (N.LengthSquared() < 0.5f) { Refusal = "plane normal is zero"; return nullptr; }
            JPH::PlaneShapeSettings S(JPH::Plane(ToJolt(N), -D.PlaneOffset), nullptr, std::max(1.0f, D.PlaneHalfExtent));
            S.SetEmbedded();
            Result = S.Create();
            break;
        }
        case CollisionShapeCategory::Box:
        {
            if (std::min({ D.HalfExtents.x, D.HalfExtents.y, D.HalfExtents.z }) < MinimumSize) { Refusal = "box half extents must be positive"; return nullptr; }
            const float ConvexRadius = std::min(JPH::cDefaultConvexRadius, 0.5f * std::min({ D.HalfExtents.x, D.HalfExtents.y, D.HalfExtents.z }));
            JPH::BoxShapeSettings S(ToJolt(D.HalfExtents), ConvexRadius);
            S.SetEmbedded();
            Result = S.Create();
            break;
        }
        case CollisionShapeCategory::Sphere:
        {
            if (D.Radius < MinimumSize) { Refusal = "sphere radius must be positive"; return nullptr; }
            JPH::SphereShapeSettings S(D.Radius);
            S.SetEmbedded();
            Result = S.Create();
            break;
        }
        case CollisionShapeCategory::Capsule:
        case CollisionShapeCategory::Cylinder:
        {
            if (D.Radius < MinimumSize || D.HalfHeight < MinimumSize) { Refusal = "capsule/cylinder radius and half height must be positive"; return nullptr; }
            JPH::Ref<JPH::ShapeSettings> Axial;
            if (D.Category == CollisionShapeCategory::Capsule)
                Axial = new JPH::CapsuleShapeSettings(D.HalfHeight, D.Radius);
            else
                Axial = new JPH::CylinderShapeSettings(D.HalfHeight, D.Radius, std::min(JPH::cDefaultConvexRadius, 0.5f * std::min(D.Radius, D.HalfHeight)));
            JPH::RotatedTranslatedShapeSettings S(JPH::Vec3::sZero(), JPH::Quat::sRotation(JPH::Vec3::sAxisX(), 0.5f * JPH::JPH_PI), Axial);
            S.SetEmbedded();
            Result = S.Create();
            break;
        }
        default:
            Refusal = "unknown collision shape category";
            return nullptr;
    }
    if (Result.HasError()) { Refusal = Result.GetError().c_str(); return nullptr; }
    return Result.Get();
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    JOLT WORLD
//------------------------------------------------------------------------------------------------------------------------

struct RigidBodySolver::JoltWorld
{
    struct BodyRecord
    {
        JPH::BodyID             Body;                               // [-] Jolt identity (index + sequence number)
        std::string             Name;                               // [-] diagnostic label
        RigidBodyMotionCategory Motion = RigidBodyMotionCategory::Dynamic;
        CollisionShapeCategory  Shape  = CollisionShapeCategory::Box;
        bool                    Alive  = false;
    };

    // The object-vs-broad-phase table SNAPSHOTS the two tables below in its constructor, so they must be fully populated
    //    before it is built — hence a nested aggregate that is a member declared ahead of it (members initialise in order).
    struct LayerScheme
    {
        JPH::BroadPhaseLayerInterfaceTable      BroadPhaseTable;
        JPH::ObjectLayerPairFilterTable         PairTable;

        LayerScheme()
            : BroadPhaseTable(ObjectLayers::Count, BroadPhaseLayers::Count)
            , PairTable(ObjectLayers::Count)
        {
            BroadPhaseTable.MapObjectToBroadPhaseLayer(ObjectLayers::NonMoving, BroadPhaseLayers::NonMoving);
            BroadPhaseTable.MapObjectToBroadPhaseLayer(ObjectLayers::Moving,    BroadPhaseLayers::Moving);
            PairTable.EnableCollision(ObjectLayers::Moving, ObjectLayers::Moving);
            PairTable.EnableCollision(ObjectLayers::Moving, ObjectLayers::NonMoving);   // static ↔ static never collides
        }
    };

    JPH::TempAllocatorImpl                      TemporaryAllocator;
    JPH::JobSystemThreadPool                    Jobs;
    LayerScheme                                 Layers;
    JPH::ObjectVsBroadPhaseLayerFilterTable     ObjectVsBroadPhaseTable;
    JPH::PhysicsSystem                          Physics;
    std::vector<BodyRecord>                     Records;            // [-] indexed by RigidBodyIdentity; creation order
    std::vector<uint32_t>                       FreeSlots;          // [-] recycled record indices

    // Phase 0 (vehicle): constraints live alongside bodies, same slot-recycling scheme, indexed by ConstraintIdentity.
    struct ConstraintRecord
    {
        JPH::Ref<JPH::Constraint>   Constraint;                     // [-] owning reference; the world also holds one
        bool                        Alive = false;
    };
    std::vector<ConstraintRecord>               Constraints;
    std::vector<uint32_t>                       FreeConstraintSlots;

    // hit BodyID → RigidBodyIdentity, via the slot we stashed in the body's user data at CreateBody().
    [[nodiscard]] RigidBodyIdentity IdentityOf(JPH::BodyID Id) noexcept
    {
        if (Id.IsInvalid()) return InvalidRigidBody;
        const uint32_t Slot = static_cast<uint32_t>(Physics.GetBodyInterface().GetUserData(Id));
        if (Slot < Records.size() && Records[Slot].Alive && Records[Slot].Body == Id) return Slot;
        return InvalidRigidBody;
    }

    // Jolt's own sample default is hardware_concurrency() - 1, which assumes the physics world owns the machine.
    //    It does not: a frame also carries the render thread and miniaudio's realtime callback, and the callback
    //    must never be preempted — a missed audio deadline is an audible click, whereas a slightly slower physics
    //    step is invisible. On a 4-thread host the old default asked for 3 workers, giving 1 + 3 + 1 = 5 runnable
    //    threads on 4 lanes. Measured effect of that oversubscription on a comparable workload: rebuild time
    //    roughly doubles (Scratchpad/TlasContentionBenchmark.cpp).
    //
    //    So: leave two lanes free (render + audio) and never ask for more than 4 workers, since Jolt scales poorly
    //    past that for the body counts this engine targets.
    [[nodiscard]] static int ResolveWorkerThreads() noexcept
    {
        const int Lanes = static_cast<int>(std::thread::hardware_concurrency());
        if (Lanes <= 0) return 1;                       // unknown topology → stay single-threaded
        return std::clamp(Lanes - 2, 1, 4);             // ≤3 lanes → 1 worker; 4 → 2; 8 → 4 (capped)
    }

    explicit JoltWorld(const RigidBodyConfiguration& C)
        : TemporaryAllocator(static_cast<JPH::uint>(std::max<uint32_t>(C.TemporaryAllocationBytes, 1u * 1024u * 1024u)))
        , Jobs(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
               C.WorkerThreads > 0u ? static_cast<int>(C.WorkerThreads)
                                    : ResolveWorkerThreads())
        , Layers()
        , ObjectVsBroadPhaseTable(Layers.BroadPhaseTable, BroadPhaseLayers::Count, Layers.PairTable, ObjectLayers::Count)
    {
        Physics.Init(C.MaxBodies, 0u, C.MaxBodyPairs, C.MaxContactConstraints, Layers.BroadPhaseTable, ObjectVsBroadPhaseTable, Layers.PairTable);
        Physics.SetGravity(ToJolt(C.Gravity));

        JPH::PhysicsSettings Settings = Physics.GetPhysicsSettings();
        Settings.mSpeculativeContactDistance = std::max(0.0f, C.SpeculativeContactDistance);
        Settings.mPenetrationSlop            = std::max(0.0f, C.PenetrationSlop);
        Settings.mTimeBeforeSleep            = std::max(0.0f, C.TimeBeforeSleepSeconds);
        Physics.SetPhysicsSettings(Settings);
    }

    [[nodiscard]] const BodyRecord* Find(RigidBodyIdentity Identity) const noexcept
    {
        if (Identity >= Records.size() || !Records[Identity].Alive) return nullptr;
        return &Records[Identity];
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

RigidBodySolver::RigidBodySolver() noexcept = default;

RigidBodySolver::~RigidBodySolver() noexcept
{
    Retire();
}

bool RigidBodySolver::Bring(const RigidBodyConfiguration& Configuration) noexcept
{
    if (Ready) { LastRefusal = "Bring() called twice without Retire()"; return false; }
    if (!(Configuration.FixedStepSeconds > 0.0f) || Configuration.MaxBodies == 0u)
    {
        LastRefusal = "FixedStepSeconds must be positive and MaxBodies non-zero";
        return false;
    }
    if (!RegisterJolt()) { LastRefusal = "Jolt type registration failed"; return false; }

    Config      = Configuration;
    Accumulator = 0.0f;
    Metrics     = RigidBodyMetrics{};
    World       = std::make_unique<JoltWorld>(Config);
    Ready       = true;
    LastRefusal.clear();
    return true;
}

void RigidBodySolver::Retire() noexcept
{
    if (!World) return;
    DestroyAllBodies();
    World.reset();          // PhysicsSystem, job threads and scratch go down before the global registry
    Ready = false;
    UnregisterJolt();
}

const char* RigidBodySolver::QueryBackendVersion() noexcept
{
    static char Version[32] = {};
    if (Version[0] == '\0') std::snprintf(Version, sizeof(Version), "Jolt %d.%d.%d", JPH_VERSION_MAJOR, JPH_VERSION_MINOR, JPH_VERSION_PATCH);
    return Version;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      BODIES
//------------------------------------------------------------------------------------------------------------------------

RigidBodyIdentity RigidBodySolver::CreateBody(const RigidBodyDescription& D) noexcept
{
    if (!Ready) { LastRefusal = "solver not ready"; return InvalidRigidBody; }
    if (D.Shape.Category == CollisionShapeCategory::Plane && D.Motion != RigidBodyMotionCategory::Static)
    {
        LastRefusal = "a plane can only be a static body";
        return InvalidRigidBody;
    }

    const JPH::ShapeRefC Shape = BuildShape(D.Shape, LastRefusal);
    if (Shape == nullptr) return InvalidRigidBody;

    const bool Moving = D.Motion != RigidBodyMotionCategory::Static;
    JPH::BodyCreationSettings Settings(Shape, ToJoltReal(D.Position), ToJolt(D.Orientation), ToJolt(D.Motion),
                                       Moving ? ObjectLayers::Moving : ObjectLayers::NonMoving);
    Settings.mLinearVelocity  = ToJolt(D.LinearVelocity);
    Settings.mAngularVelocity = ToJolt(D.AngularVelocity);
    Settings.mFriction        = std::max(0.0f, D.Friction);
    Settings.mRestitution     = std::clamp(D.Restitution, 0.0f, 1.0f);
    Settings.mLinearDamping   = std::max(0.0f, D.LinearDamping);
    Settings.mAngularDamping  = std::max(0.0f, D.AngularDamping);
    Settings.mGravityFactor   = D.GravityFactor;
    Settings.mAllowSleeping   = D.AllowSleeping;
    Settings.mMotionQuality   = D.ContinuousCollision ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
    if (D.Motion == RigidBodyMotionCategory::Dynamic && D.MassKilograms > 0.0f)
    {
        Settings.mOverrideMassProperties       = JPH::EOverrideMassProperties::CalculateInertia;   // inertia from the shape, scaled to the given mass
        Settings.mMassPropertiesOverride.mMass = D.MassKilograms;
    }

    JPH::BodyInterface& Bodies = World->Physics.GetBodyInterface();
    const JPH::BodyID Body = Bodies.CreateAndAddBody(Settings, Moving ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
    if (Body.IsInvalid())
    {
        LastRefusal = "Jolt refused the body (MaxBodies reached?)";
        return InvalidRigidBody;
    }

    uint32_t Slot;
    if (!World->FreeSlots.empty()) { Slot = World->FreeSlots.back(); World->FreeSlots.pop_back(); }
    else                           { Slot = static_cast<uint32_t>(World->Records.size()); World->Records.emplace_back(); }

    JoltWorld::BodyRecord& R = World->Records[Slot];
    R.Body   = Body;
    R.Name   = D.Name;
    R.Motion = D.Motion;
    R.Shape  = D.Shape.Category;
    R.Alive  = true;
    Bodies.SetUserData(Body, Slot);

    Metrics.BodyCount = World->Physics.GetNumBodies();
    return Slot;
}

void RigidBodySolver::DestroyBody(RigidBodyIdentity Identity) noexcept
{
    if (!Ready) return;
    const JoltWorld::BodyRecord* R = World->Find(Identity);
    if (R == nullptr) return;

    JPH::BodyInterface& Bodies = World->Physics.GetBodyInterface();
    if (Bodies.IsAdded(R->Body)) Bodies.RemoveBody(R->Body);
    Bodies.DestroyBody(R->Body);

    World->Records[Identity] = JoltWorld::BodyRecord{};
    World->FreeSlots.push_back(Identity);
    Metrics.BodyCount = World->Physics.GetNumBodies();
}

void RigidBodySolver::DestroyAllBodies() noexcept
{
    if (!World) return;

    // Constraints reference bodies, so they must leave the world before the bodies they link are destroyed.
    for (JoltWorld::ConstraintRecord& C : World->Constraints)
    {
        if (!C.Alive) continue;
        if (C.Constraint != nullptr) World->Physics.RemoveConstraint(C.Constraint.GetPtr());
        C = JoltWorld::ConstraintRecord{};
    }
    World->Constraints.clear();
    World->FreeConstraintSlots.clear();

    JPH::BodyInterface& Bodies = World->Physics.GetBodyInterface();
    for (const JoltWorld::BodyRecord& R : World->Records)
    {
        if (!R.Alive) continue;
        if (Bodies.IsAdded(R.Body)) Bodies.RemoveBody(R.Body);
        Bodies.DestroyBody(R.Body);
    }
    World->Records.clear();
    World->FreeSlots.clear();
    Metrics.BodyCount = World->Physics.GetNumBodies();
}

void RigidBodySolver::OptimizeBroadPhase() noexcept
{
    if (Ready) World->Physics.OptimizeBroadPhase();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       TICK
//------------------------------------------------------------------------------------------------------------------------

void RigidBodySolver::Advance(float Δτ) noexcept
{
    Metrics.StepsLastAdvance = 0u;
    if (!Ready || !(Δτ > 0.0f)) return;

    Accumulator += Δτ;

    // Spiral-of-death guard: a hitch longer than MaxStepsPerAdvance steps is dropped rather than replayed.
    const float BacklogLimit = Config.FixedStepSeconds * static_cast<float>(std::max(1u, Config.MaxStepsPerAdvance));
    if (Accumulator > BacklogLimit)
    {
        Metrics.DroppedSeconds += Accumulator - BacklogLimit;
        Accumulator = BacklogLimit;
    }

    using Clock = std::chrono::steady_clock;
    while (Accumulator >= Config.FixedStepSeconds)
    {
        const auto Start = Clock::now();
        const JPH::EPhysicsUpdateError Error = World->Physics.Update(Config.FixedStepSeconds,
                                                                     static_cast<int>(std::max(1u, Config.CollisionStepsPerUpdate)),
                                                                     &World->TemporaryAllocator, &World->Jobs);
        Metrics.LastStepMilliseconds = std::chrono::duration<float, std::milli>(Clock::now() - Start).count();
        if (Error != JPH::EPhysicsUpdateError::None)
        {
            char Line[128];
            std::snprintf(Line, sizeof(Line), "physics update error 0x%X (raise MaxBodyPairs / MaxContactConstraints / TemporaryAllocationBytes)",
                          static_cast<unsigned>(Error));
            LastRefusal = Line;
        }
        Accumulator -= Config.FixedStepSeconds;
        ++Metrics.StepCount;
        ++Metrics.StepsLastAdvance;
    }

    Metrics.AccumulatorSeconds = Accumulator;
    Metrics.BodyCount          = World->Physics.GetNumBodies();
    Metrics.ActiveBodyCount    = World->Physics.GetNumActiveBodies(JPH::EBodyType::RigidBody);
}

float RigidBodySolver::QueryInterpolationAlpha() const noexcept
{
    return Config.FixedStepSeconds > 0.0f ? std::clamp(Accumulator / Config.FixedStepSeconds, 0.0f, 1.0f) : 0.0f;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERACTION
//------------------------------------------------------------------------------------------------------------------------

void RigidBodySolver::ApplyImpulse(RigidBodyIdentity Identity, const Vector3& Impulse) noexcept
{
    if (!Ready) return;
    if (const JoltWorld::BodyRecord* R = World->Find(Identity); R != nullptr && R->Motion == RigidBodyMotionCategory::Dynamic)
        World->Physics.GetBodyInterface().AddImpulse(R->Body, ToJolt(Impulse));
}

void RigidBodySolver::AssignLinearVelocity(RigidBodyIdentity Identity, const Vector3& Velocity) noexcept
{
    if (!Ready) return;
    if (const JoltWorld::BodyRecord* R = World->Find(Identity); R != nullptr && R->Motion != RigidBodyMotionCategory::Static)
        World->Physics.GetBodyInterface().SetLinearVelocity(R->Body, ToJolt(Velocity));
}

void RigidBodySolver::Teleport(RigidBodyIdentity Identity, const Vector3& Position, const Quaternion& Orientation) noexcept
{
    if (!Ready) return;
    if (const JoltWorld::BodyRecord* R = World->Find(Identity); R != nullptr)
    {
        JPH::BodyInterface& Bodies = World->Physics.GetBodyInterface();
        Bodies.SetPositionAndRotation(R->Body, ToJoltReal(Position), ToJolt(Orientation),
                                      R->Motion == RigidBodyMotionCategory::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
        if (R->Motion == RigidBodyMotionCategory::Dynamic)
        {
            Bodies.SetLinearAndAngularVelocity(R->Body, JPH::Vec3::sZero(), JPH::Vec3::sZero());
        }
    }
}

void RigidBodySolver::MoveKinematic(RigidBodyIdentity Identity, const Vector3& Position, const Quaternion& Orientation, float Δτ) noexcept
{
    if (!Ready || !(Δτ > 0.0f)) return;
    if (const JoltWorld::BodyRecord* R = World->Find(Identity); R != nullptr && R->Motion == RigidBodyMotionCategory::Kinematic)
        World->Physics.GetBodyInterface().MoveKinematic(R->Body, ToJoltReal(Position), ToJolt(Orientation), Δτ);
}

void RigidBodySolver::Activate(RigidBodyIdentity Identity) noexcept
{
    if (!Ready) return;
    if (const JoltWorld::BodyRecord* R = World->Find(Identity); R != nullptr && R->Motion != RigidBodyMotionCategory::Static)
        World->Physics.GetBodyInterface().ActivateBody(R->Body);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      READBACK
//------------------------------------------------------------------------------------------------------------------------

bool RigidBodySolver::QueryPose(RigidBodyIdentity Identity, RigidBodyPose& Pose) const noexcept
{
    if (!Ready) return false;
    const JoltWorld::BodyRecord* R = World->Find(Identity);
    if (R == nullptr) return false;

    const JPH::BodyInterface& Bodies = World->Physics.GetBodyInterface();
    JPH::RVec3 Position; JPH::Quat Orientation;
    Bodies.GetPositionAndRotation(R->Body, Position, Orientation);

    Pose.Identity        = Identity;
    Pose.Position        = FromJolt(Position);
    Pose.Orientation     = FromJolt(Orientation);
    Pose.LinearVelocity  = R->Motion == RigidBodyMotionCategory::Static ? Vector3{} : FromJolt(Bodies.GetLinearVelocity(R->Body));
    Pose.AngularVelocity = R->Motion == RigidBodyMotionCategory::Static ? Vector3{} : FromJolt(Bodies.GetAngularVelocity(R->Body));
    Pose.Motion          = R->Motion;
    Pose.Shape           = R->Shape;
    Pose.Active          = Bodies.IsActive(R->Body);
    return true;
}

void RigidBodySolver::QueryPoses(std::vector<RigidBodyPose>& Poses) const noexcept
{
    Poses.clear();
    if (!Ready) return;
    Poses.reserve(World->Records.size());
    for (uint32_t Index = 0; Index < World->Records.size(); ++Index)
    {
        if (!World->Records[Index].Alive) continue;
        RigidBodyPose Pose;
        if (QueryPose(Index, Pose)) Poses.push_back(Pose);
    }
}

const std::string& RigidBodySolver::QueryName(RigidBodyIdentity Identity) const noexcept
{
    static const std::string Empty;
    if (!Ready) return Empty;
    const JoltWorld::BodyRecord* R = World->Find(Identity);
    return R != nullptr ? R->Name : Empty;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      PHASE 0 — EXPLICIT STEP (physics thread)
//------------------------------------------------------------------------------------------------------------------------

bool RigidBodySolver::StepOnce() noexcept
{
    if (!Ready) return false;

    using Clock = std::chrono::steady_clock;
    const auto Start = Clock::now();
    const JPH::EPhysicsUpdateError Error = World->Physics.Update(Config.FixedStepSeconds,
                                                                static_cast<int>(std::max(1u, Config.CollisionStepsPerUpdate)),
                                                                &World->TemporaryAllocator, &World->Jobs);
    Metrics.LastStepMilliseconds = std::chrono::duration<float, std::milli>(Clock::now() - Start).count();
    if (Error != JPH::EPhysicsUpdateError::None)
    {
        char Line[128];
        std::snprintf(Line, sizeof(Line), "physics update error 0x%X (raise MaxBodyPairs / MaxContactConstraints / TemporaryAllocationBytes)",
                      static_cast<unsigned>(Error));
        LastRefusal = Line;
    }
    ++Metrics.StepCount;
    Metrics.StepsLastAdvance   = 1u;
    Metrics.AccumulatorSeconds = Accumulator;
    Metrics.BodyCount          = World->Physics.GetNumBodies();
    Metrics.ActiveBodyCount    = World->Physics.GetNumActiveBodies(JPH::EBodyType::RigidBody);
    return Error == JPH::EPhysicsUpdateError::None;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      PHASE 0 — PER-POINT FORCES (suspension + tyre)
//------------------------------------------------------------------------------------------------------------------------

void RigidBodySolver::ApplyForce(RigidBodyIdentity Identity, const Vector3& ForceNewtons) noexcept
{
    if (!Ready) return;
    if (const JoltWorld::BodyRecord* R = World->Find(Identity); R != nullptr && R->Motion == RigidBodyMotionCategory::Dynamic)
        World->Physics.GetBodyInterface().AddForce(R->Body, ToJolt(ForceNewtons));
}

void RigidBodySolver::ApplyForceAtPoint(RigidBodyIdentity Identity, const Vector3& ForceNewtons, const Vector3& WorldPoint) noexcept
{
    if (!Ready) return;
    if (const JoltWorld::BodyRecord* R = World->Find(Identity); R != nullptr && R->Motion == RigidBodyMotionCategory::Dynamic)
        World->Physics.GetBodyInterface().AddForce(R->Body, ToJolt(ForceNewtons), ToJoltReal(WorldPoint));
}

void RigidBodySolver::ApplyTorque(RigidBodyIdentity Identity, const Vector3& TorqueNewtonMetres) noexcept
{
    if (!Ready) return;
    if (const JoltWorld::BodyRecord* R = World->Find(Identity); R != nullptr && R->Motion == RigidBodyMotionCategory::Dynamic)
        World->Physics.GetBodyInterface().AddTorque(R->Body, ToJolt(TorqueNewtonMetres));
}

void RigidBodySolver::ApplyAngularImpulse(RigidBodyIdentity Identity, const Vector3& AngularImpulse) noexcept
{
    if (!Ready) return;
    if (const JoltWorld::BodyRecord* R = World->Find(Identity); R != nullptr && R->Motion == RigidBodyMotionCategory::Dynamic)
        World->Physics.GetBodyInterface().AddAngularImpulse(R->Body, ToJolt(AngularImpulse));
}

//------------------------------------------------------------------------------------------------------------------------
//                                      PHASE 0 — BODY QUERIES (load, moment arm, slip)
//------------------------------------------------------------------------------------------------------------------------

float RigidBodySolver::QueryBodyMass(RigidBodyIdentity Identity) const noexcept
{
    if (!Ready) return 0.0f;
    const JoltWorld::BodyRecord* R = World->Find(Identity);
    if (R == nullptr || R->Motion == RigidBodyMotionCategory::Static) return 0.0f;

    JPH::BodyLockRead Lock(World->Physics.GetBodyLockInterface(), R->Body);
    if (!Lock.Succeeded()) return 0.0f;
    const JPH::MotionProperties* Motion = Lock.GetBody().GetMotionProperties();
    if (Motion == nullptr) return 0.0f;
    const float InverseMass = Motion->GetInverseMass();
    return InverseMass > 0.0f ? 1.0f / InverseMass : 0.0f;
}

Vector3 RigidBodySolver::QueryCenterOfMass(RigidBodyIdentity Identity) const noexcept
{
    if (!Ready) return Vector3{};
    const JoltWorld::BodyRecord* R = World->Find(Identity);
    if (R == nullptr) return Vector3{};
    return FromJolt(World->Physics.GetBodyInterface().GetCenterOfMassPosition(R->Body));
}

Vector3 RigidBodySolver::QueryPointVelocity(RigidBodyIdentity Identity, const Vector3& WorldPoint) const noexcept
{
    if (!Ready) return Vector3{};
    const JoltWorld::BodyRecord* R = World->Find(Identity);
    if (R == nullptr || R->Motion == RigidBodyMotionCategory::Static) return Vector3{};
    return FromJolt(World->Physics.GetBodyInterface().GetPointVelocity(R->Body, ToJoltReal(WorldPoint)));
}

//------------------------------------------------------------------------------------------------------------------------
//                                      PHASE 0 — SCENE QUERIES (probe / footprint)
//------------------------------------------------------------------------------------------------------------------------

SceneCastResult RigidBodySolver::CastRay(const Vector3& Origin, const Vector3& Direction, float MaxDistance,
                                         RigidBodyIdentity Ignore) const noexcept
{
    SceneCastResult Out;
    if (!Ready || !(MaxDistance > 0.0f)) return Out;

    const float Length = std::sqrt(Direction.x * Direction.x + Direction.y * Direction.y + Direction.z * Direction.z);
    if (!(Length > 0.0f)) return Out;
    const Vector3 Unit{ Direction.x / Length, Direction.y / Length, Direction.z / Length };

    // Jolt encodes the ray length in the (un-normalised) direction vector.
    const JPH::RRayCast Ray{ ToJoltReal(Origin), ToJolt(Vector3{ Unit.x * MaxDistance, Unit.y * MaxDistance, Unit.z * MaxDistance }) };

    JPH::BodyID IgnoreId;
    if (const JoltWorld::BodyRecord* Skip = World->Find(Ignore); Skip != nullptr) IgnoreId = Skip->Body;
    const JPH::IgnoreSingleBodyFilter BodyFilter(IgnoreId);

    JPH::RayCastResult Hit;
    if (!World->Physics.GetNarrowPhaseQuery().CastRay(Ray, Hit, {}, {}, BodyFilter)) return Out;

    const JPH::RVec3 Point = Ray.GetPointOnRay(Hit.mFraction);
    Out.Hit      = true;
    Out.Fraction = Hit.mFraction;
    Out.Distance = Hit.mFraction * MaxDistance;
    Out.Position = FromJolt(Point);
    Out.Body     = World->IdentityOf(Hit.mBodyID);

    JPH::BodyLockRead Lock(World->Physics.GetBodyLockInterface(), Hit.mBodyID);
    if (Lock.Succeeded())
        Out.Normal = FromJolt(Lock.GetBody().GetWorldSpaceSurfaceNormal(Hit.mSubShapeID2, Point));
    return Out;
}

SceneCastResult RigidBodySolver::CastSphere(float Radius, const Vector3& From, const Vector3& Sweep,
                                            RigidBodyIdentity Ignore) const noexcept
{
    SceneCastResult Out;
    if (!Ready || !(Radius > 0.0f)) return Out;

    JPH::SphereShapeSettings Settings(Radius);
    JPH::ShapeSettings::ShapeResult Result = Settings.Create();
    if (Result.HasError()) return Out;

    JPH::BodyID IgnoreId;
    if (const JoltWorld::BodyRecord* Skip = World->Find(Ignore); Skip != nullptr) IgnoreId = Skip->Body;

    const JPH::Quat Orientation = JPH::Quat::sIdentity();
    return SweepShapeImpl(Result.Get().GetPtr(), &Orientation, From, Sweep, IgnoreId.GetIndexAndSequenceNumber());
}

SceneCastResult RigidBodySolver::CastCylinder(float Radius, float HalfHeight, const Quaternion& Orientation,
                                              const Vector3& From, const Vector3& Sweep,
                                              RigidBodyIdentity Ignore) const noexcept
{
    SceneCastResult Out;
    if (!Ready || !(Radius > 0.0f) || !(HalfHeight > 0.0f)) return Out;

    // Frontier's cylinder axis is local +Z; a bare Jolt cylinder is +Y, so pre-rotate the requested orientation the same
    //    way BuildShape() does, then compose with the caller's world orientation.
    const JPH::Quat AxisAlign = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), 0.5f * JPH::JPH_PI);
    JPH::CylinderShapeSettings Settings(HalfHeight, Radius, std::min(JPH::cDefaultConvexRadius, 0.5f * std::min(Radius, HalfHeight)));
    JPH::ShapeSettings::ShapeResult Result = Settings.Create();
    if (Result.HasError()) return Out;

    JPH::BodyID IgnoreId;
    if (const JoltWorld::BodyRecord* Skip = World->Find(Ignore); Skip != nullptr) IgnoreId = Skip->Body;

    const JPH::Quat WorldOrientation = ToJolt(Orientation) * AxisAlign;
    return SweepShapeImpl(Result.Get().GetPtr(), &WorldOrientation, From, Sweep, IgnoreId.GetIndexAndSequenceNumber());
}

SceneCastResult RigidBodySolver::SweepShapeImpl(const void* ShapePtr, const void* OrientationPtr,
                                                const Vector3& From, const Vector3& Sweep, uint32_t IgnoreRaw) const noexcept
{
    SceneCastResult Out;
    const JPH::Shape* Shape = static_cast<const JPH::Shape*>(ShapePtr);
    const JPH::Quat   Orientation = *static_cast<const JPH::Quat*>(OrientationPtr);
    JPH::BodyID       IgnoreId(IgnoreRaw);

    const float Length = std::sqrt(Sweep.x * Sweep.x + Sweep.y * Sweep.y + Sweep.z * Sweep.z);
    if (!(Length > 0.0f)) return Out;

    const JPH::RMat44 Start = JPH::RMat44::sRotationTranslation(Orientation, ToJoltReal(From));
    JPH::RShapeCast   Cast(Shape, JPH::Vec3::sReplicate(1.0f), Start, ToJolt(Sweep));

    JPH::ShapeCastSettings Settings;
    Settings.mUseShrunkenShapeAndConvexRadius = true;
    Settings.mReturnDeepestPoint              = false;

    const JPH::IgnoreSingleBodyFilter BodyFilter(IgnoreId);
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> Collector;
    World->Physics.GetNarrowPhaseQuery().CastShape(Cast, Settings, ToJoltReal(From), Collector, {}, {}, BodyFilter);
    if (!Collector.HadHit()) return Out;

    const JPH::ShapeCastResult& Hit = Collector.mHit;
    Out.Hit      = true;
    Out.Fraction = Hit.mFraction;
    Out.Distance = Hit.mFraction * Length;
    Out.Position = FromJolt(ToJoltReal(From) + Hit.mContactPointOn2);
    Out.Normal   = FromJolt(-Hit.mPenetrationAxis.Normalized());        // penetration axis points 2→1; flip for the surface normal
    Out.Body     = World->IdentityOf(Hit.mBodyID2);
    return Out;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      PHASE 0 — HEIGHTFIELD TERRAIN
//------------------------------------------------------------------------------------------------------------------------

RigidBodyIdentity RigidBodySolver::CreateHeightfieldBody(const HeightfieldDescription& D) noexcept
{
    if (!Ready) { LastRefusal = "solver not ready"; return InvalidRigidBody; }
    if (D.Samples == nullptr || D.SampleCount == 0u || (D.SampleCount % 8u) != 0u)
    {
        LastRefusal = "heightfield needs a non-null sample grid whose side is a non-zero multiple of 8";
        return InvalidRigidBody;
    }

    // Jolt lays the grid in its local X/Z plane with height along +Y. Scale maps a sample to local metres.
    JPH::HeightFieldShapeSettings Field(D.Samples, JPH::Vec3::sZero(),
                                        JPH::Vec3(D.SpacingX, D.HeightScale, D.SpacingY), D.SampleCount);
    JPH::ShapeSettings::ShapeResult FieldResult = Field.Create();
    if (FieldResult.HasError()) { LastRefusal = std::string("heightfield: ") + FieldResult.GetError().c_str(); return InvalidRigidBody; }

    // Rotate local +Y (height) onto world +Z (+90° about X). After the wrap: col→world +X, row→world −Y, height→world +Z.
    JPH::RotatedTranslatedShapeSettings Wrap(JPH::Vec3::sZero(), JPH::Quat::sRotation(JPH::Vec3::sAxisX(), 0.5f * JPH::JPH_PI),
                                             FieldResult.Get());
    JPH::ShapeSettings::ShapeResult WrapResult = Wrap.Create();
    if (WrapResult.HasError()) { LastRefusal = std::string("heightfield wrap: ") + WrapResult.GetError().c_str(); return InvalidRigidBody; }

    JPH::BodyCreationSettings Settings(WrapResult.Get(), ToJoltReal(D.Origin), JPH::Quat::sIdentity(),
                                       JPH::EMotionType::Static, ObjectLayers::NonMoving);
    Settings.mFriction    = std::max(0.0f, D.Friction);
    Settings.mRestitution = std::clamp(D.Restitution, 0.0f, 1.0f);

    JPH::BodyInterface& Bodies = World->Physics.GetBodyInterface();
    const JPH::BodyID Body = Bodies.CreateAndAddBody(Settings, JPH::EActivation::DontActivate);
    if (Body.IsInvalid()) { LastRefusal = "Jolt refused the heightfield body (MaxBodies reached?)"; return InvalidRigidBody; }

    uint32_t Slot;
    if (!World->FreeSlots.empty()) { Slot = World->FreeSlots.back(); World->FreeSlots.pop_back(); }
    else                           { Slot = static_cast<uint32_t>(World->Records.size()); World->Records.emplace_back(); }

    JoltWorld::BodyRecord& R = World->Records[Slot];
    R.Body   = Body;
    R.Name   = D.Name;
    R.Motion = RigidBodyMotionCategory::Static;
    R.Shape  = CollisionShapeCategory::Heightfield;
    R.Alive  = true;
    Bodies.SetUserData(Body, Slot);

    Metrics.BodyCount = World->Physics.GetNumBodies();
    return Slot;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      PHASE 0 — STRUT SPRING CONSTRAINT
//------------------------------------------------------------------------------------------------------------------------

ConstraintIdentity RigidBodySolver::CreateDistanceSpring(RigidBodyIdentity BodyA, const Vector3& WorldAnchorA,
                                                         RigidBodyIdentity BodyB, const Vector3& WorldAnchorB,
                                                         float MinDistance, float MaxDistance,
                                                         float FrequencyHz, float DampingRatio) noexcept
{
    if (!Ready) { LastRefusal = "solver not ready"; return InvalidConstraint; }
    const JoltWorld::BodyRecord* A = World->Find(BodyA);
    const JoltWorld::BodyRecord* B = World->Find(BodyB);
    if (A == nullptr || B == nullptr) { LastRefusal = "distance spring: unknown body"; return InvalidConstraint; }

    JPH::DistanceConstraintSettings Settings;
    Settings.mSpace       = JPH::EConstraintSpace::WorldSpace;
    Settings.mPoint1      = ToJoltReal(WorldAnchorA);
    Settings.mPoint2      = ToJoltReal(WorldAnchorB);
    Settings.mMinDistance = MinDistance;
    Settings.mMaxDistance = MaxDistance;
    if (FrequencyHz > 0.0f)
    {
        Settings.mLimitsSpringSettings.mMode      = JPH::ESpringMode::FrequencyAndDamping;
        Settings.mLimitsSpringSettings.mFrequency = FrequencyHz;
        Settings.mLimitsSpringSettings.mDamping   = DampingRatio;
    }

    // Constraint creation needs the live Body objects; take both locks (setup path, single-threaded).
    const JPH::BodyID Pair[2] = { A->Body, B->Body };
    JPH::BodyLockMultiWrite Locks(World->Physics.GetBodyLockInterface(), Pair, 2);
    JPH::Body* LiveA = Locks.GetBody(0);
    JPH::Body* LiveB = Locks.GetBody(1);
    if (LiveA == nullptr || LiveB == nullptr) { LastRefusal = "distance spring: body lock failed"; return InvalidConstraint; }

    JPH::Constraint* Created = Settings.Create(*LiveA, *LiveB);
    if (Created == nullptr) { LastRefusal = "distance spring: Jolt refused the constraint"; return InvalidConstraint; }
    World->Physics.AddConstraint(Created);

    uint32_t Slot;
    if (!World->FreeConstraintSlots.empty()) { Slot = World->FreeConstraintSlots.back(); World->FreeConstraintSlots.pop_back(); }
    else                                     { Slot = static_cast<uint32_t>(World->Constraints.size()); World->Constraints.emplace_back(); }
    World->Constraints[Slot].Constraint = Created;   // Ref adopts; the world holds a second reference
    World->Constraints[Slot].Alive      = true;
    return Slot;
}

void RigidBodySolver::DestroyConstraint(ConstraintIdentity Identity) noexcept
{
    if (!Ready || Identity >= World->Constraints.size() || !World->Constraints[Identity].Alive) return;
    JoltWorld::ConstraintRecord& C = World->Constraints[Identity];
    if (C.Constraint != nullptr) World->Physics.RemoveConstraint(C.Constraint.GetPtr());
    C = JoltWorld::ConstraintRecord{};
    World->FreeConstraintSlots.push_back(Identity);
}

} // namespace Frontier
