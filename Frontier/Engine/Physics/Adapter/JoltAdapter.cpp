/*============================================================================================================================================
                                                             JOLTADAPTER.CPP
============================================================================================================================================*/
// 🧩 Jolt 5.2.0 adapter mechanics. Layers mirror the exhibit JoltScene: NonMoving statics, Moving dynamics/kinematics.
//    Advance slices host time into fixed Δτ substeps (one PhysicsSystem.Update per slice, single-worker job lane for
//    reproducible ordering); kinematic proxies accumulate their collision Δv per slice for the host vehicle solver.

#include "Engine/Physics/Adapter/JoltAdapter.h"

#include <cmath>

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

namespace Frontier
{

namespace
{
    // 🔍 Jolt global runtime with reference counting: allocator + factory + type registry are process-wide; several
    //    adapters may coexist (validation suites construct worlds sequentially).
    struct JoltStatics
    {
        int                                     LiveWorlds     = 0;
        std::unique_ptr<JPH::Factory>           Factory;
        std::unique_ptr<JPH::TempAllocatorImpl> TempAllocator;
        std::unique_ptr<JPH::JobSystemThreadPool> JobLane;
    };

    JoltStatics& Statics()
    {
        static JoltStatics G;
        return G;
    }

    namespace Layers { constexpr JPH::ObjectLayer NonMoving = 0; constexpr JPH::ObjectLayer Moving = 1; }
    namespace BroadPhaseLayers { constexpr JPH::BroadPhaseLayer NonMoving(0); constexpr JPH::BroadPhaseLayer Moving(1); }

    class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
    {
    public:
        JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
        JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer Layer) const override
        {
            return Layer == Layers::NonMoving ? BroadPhaseLayers::NonMoving : BroadPhaseLayers::Moving;
        }
    };

    class ObjectVsBPLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter
    {
    public:
        bool ShouldCollide(JPH::ObjectLayer Layer1, JPH::BroadPhaseLayer Layer2) const override
        {
            return Layer1 == Layers::NonMoving ? Layer2 == BroadPhaseLayers::Moving : true;
        }
    };

    class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter
    {
    public:
        bool ShouldCollide(JPH::ObjectLayer Layer1, JPH::ObjectLayer Layer2) const override
        {
            return Layer1 == Layers::NonMoving ? Layer2 == Layers::Moving : true;
        }
    };

    inline JPH::Vec3 V3(const double V[3]) { return JPH::Vec3((float)V[0], (float)V[1], (float)V[2]); }
    inline JPH::Quat Q4(const double Q[4]) { return JPH::Quat((float)Q[0], (float)Q[1], (float)Q[2], (float)Q[3]); }
    inline PhysicsBodyToken PackToken(uint32_t Index, uint32_t Generation)
    {
        return (static_cast<PhysicsBodyToken>(Generation) << 32) | static_cast<PhysicsBodyToken>(Index);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

struct JoltAdapter::Implementation
{
    BPLayerInterfaceImpl      BPLayers;
    ObjectVsBPLayerFilterImpl ObjectVsBP;
    ObjectLayerPairFilterImpl ObjectPairs;
    JPH::PhysicsSystem        World;
    PhysicsWorldConfiguration Configuration{};
};

static void AcquireJoltStatics()
{
    JoltStatics& G = Statics();
    if (G.LiveWorlds++ == 0)
    {
        JPH::RegisterDefaultAllocator();
        G.Factory = std::make_unique<JPH::Factory>();
        JPH::Factory::sInstance = G.Factory.get();
        JPH::RegisterTypes();
        G.TempAllocator = std::make_unique<JPH::TempAllocatorImpl>(32 * 1024 * 1024);
        G.JobLane       = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, 1);
    }
}

static void ReleaseJoltStatics()
{
    JoltStatics& G = Statics();
    if (--G.LiveWorlds == 0)
    {
        G.JobLane.reset();
        G.TempAllocator.reset();
        JPH::UnregisterTypes();
        JPH::Factory::sInstance = nullptr;
        G.Factory.reset();
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

JoltAdapter::~JoltAdapter()
{
    Shutdown();
}

bool JoltAdapter::Initialize(const PhysicsWorldConfiguration& Configuration)
{
    AcquireJoltStatics();
    Inner = std::make_unique<Implementation>();
    Inner->Configuration = Configuration;

    JPH::PhysicsSettings Settings;
    Settings.mNumVelocitySteps = Configuration.VelocitySolverIterations;
    Settings.mNumPositionSteps = Configuration.PositionSolverIterations;
    Inner->World.SetPhysicsSettings(Settings);

    Inner->World.Init(4096, 0, 2048, 4096, Inner->BPLayers, Inner->ObjectVsBP, Inner->ObjectPairs);
    Inner->World.SetGravity(V3(Configuration.Gravity));
    return true;
}

void JoltAdapter::Shutdown() noexcept
{
    if (!Inner) return;
    JPH::BodyInterface& Bodies = Inner->World.GetBodyInterface();
    for (BodySlot& Slot : Slots)
    {
        if (!Slot.Live) continue;
        const JPH::BodyID Id(Slot.JoltId);
        Bodies.RemoveBody(Id);
        Bodies.DestroyBody(Id);
        Slot.Live = false;
    }
    Free.clear();
    Accumulator = 0.0;
    Inner.reset();
    ReleaseJoltStatics();
}

const PhysicsWorldConfiguration& JoltAdapter::QueryWorldConfiguration() const noexcept
{
    static const PhysicsWorldConfiguration Empty{};
    return Inner ? Inner->Configuration : Empty;
}

bool JoltAdapter::QuerySlot(PhysicsBodyToken Body, uint32_t& OutIndex) const noexcept
{
    const uint32_t Index = static_cast<uint32_t>(Body & 0xFFFFFFFFu);
    if (Index == 0 || Index >= Slots.size()) return false;
    if (PackToken(Index, Slots[Index].Generation) != Body || !Slots[Index].Live) return false;
    OutIndex = Index;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     BODY MANAGEMENT
//------------------------------------------------------------------------------------------------------------------------

PhysicsBodyToken JoltAdapter::CreateBody(const PhysicsBodyDescription& Description)
{
    if (!Inner) return PhysicsBodyInvalid;

    JPH::ShapeSettings::ShapeResult ShapeResult;
    if (Description.Colliders.size() == 1)
    {
        const PhysicsColliderDescription& C = Description.Colliders.front();
        switch (C.Shape)
        {
        case PhysicsColliderShape::BoundBox:
            ShapeResult = JPH::BoxShapeSettings(V3(C.HalfExtents)).Create();
            break;
        case PhysicsColliderShape::BoundingSphere:
            ShapeResult = JPH::SphereShapeSettings((float)C.Radius).Create();
            break;
        case PhysicsColliderShape::BoundingCapsule:
            ShapeResult = JPH::CapsuleShapeSettings((float)C.CapsuleHalfHeight, (float)C.Radius).Create();
            break;
        case PhysicsColliderShape::ConvexHull:
        {
            JPH::Array<JPH::Vec3> Points;
            for (std::size_t P = 0; P + 2 < C.HullPositions.size(); P += 3)
                Points.push_back(JPH::Vec3((float)C.HullPositions[P], (float)C.HullPositions[P + 1], (float)C.HullPositions[P + 2]));
            JPH::ConvexHullShapeSettings HullSettings(Points);
            if (C.Radius > 0.0) HullSettings.mMaxConvexRadius = (float)C.Radius;
            else HullSettings.mMaxConvexRadius = 0.0f;
            ShapeResult = HullSettings.Create();
            break;
        }
        }
    }
    else if (Description.Colliders.size() > 1)
    {
        // 📝 Compound of simple bounds: each collider sits at its local offset (no rotations authored yet).
        JPH::StaticCompoundShapeSettings Compound;
        for (const PhysicsColliderDescription& C : Description.Colliders)
        {
            JPH::RefConst<JPH::ShapeSettings> Part;
            switch (C.Shape)
            {
            case PhysicsColliderShape::BoundBox:        Part = new JPH::BoxShapeSettings(V3(C.HalfExtents)); break;
            case PhysicsColliderShape::BoundingSphere:  Part = new JPH::SphereShapeSettings((float)C.Radius); break;
            case PhysicsColliderShape::BoundingCapsule: Part = new JPH::CapsuleShapeSettings((float)C.CapsuleHalfHeight, (float)C.Radius); break;
            case PhysicsColliderShape::ConvexHull:
            {
                JPH::Array<JPH::Vec3> Points;
                for (std::size_t P = 0; P + 2 < C.HullPositions.size(); P += 3)
                    Points.push_back(JPH::Vec3((float)C.HullPositions[P], (float)C.HullPositions[P + 1], (float)C.HullPositions[P + 2]));
                Part = new JPH::ConvexHullShapeSettings(Points);
                break;
            }
            }
            Compound.AddShape(V3(C.PositionOffset), JPH::Quat::sIdentity(), Part);
        }
        ShapeResult = Compound.Create();
    }
    if (Description.Colliders.empty() || ShapeResult.HasError()) return PhysicsBodyInvalid;

    const JPH::EMotionType Motion = Description.FractureBoundStatic        ? JPH::EMotionType::Static
                                  : Description.Motion == PhysicsBodyMotion::Static    ? JPH::EMotionType::Static
                                  : Description.Motion == PhysicsBodyMotion::Kinematic ? JPH::EMotionType::Kinematic
                                                                                       : JPH::EMotionType::Dynamic;
    const JPH::ObjectLayer Layer = Motion == JPH::EMotionType::Static ? Layers::NonMoving : Layers::Moving;

    JPH::BodyCreationSettings Settings(ShapeResult.Get(), JPH::RVec3(Description.Position[0], Description.Position[1], Description.Position[2]),
                                       Q4(Description.Orientation), Motion, Layer);
    Settings.mFriction       = (float)Description.Friction;
    Settings.mRestitution    = (float)Description.Restitution;
    Settings.mLinearDamping  = (float)Description.LinearDamping;
    Settings.mAngularDamping = (float)Description.AngularDamping;
    Settings.mGravityFactor  = (float)Description.GravityShare;
    if (Motion == JPH::EMotionType::Dynamic)
    {
        Settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        Settings.mMassPropertiesOverride.mMass = (float)Description.MassKilograms;
    }
    Settings.mAllowSleeping = Motion == JPH::EMotionType::Dynamic;

    JPH::BodyInterface& Bodies = Inner->World.GetBodyInterface();
    const JPH::BodyID Id = Bodies.CreateAndAddBody(Settings, Motion == JPH::EMotionType::Dynamic ? JPH::EActivation::Activate
                                                                                                 : JPH::EActivation::DontActivate);
    if (Id.IsInvalid()) return PhysicsBodyInvalid;
    if (Motion != JPH::EMotionType::Static)
        Bodies.SetLinearAndAngularVelocity(Id, V3(Description.LinearVelocity), V3(Description.AngularVelocity));

    BodySlot Slot;
    Slot.Live        = true;
    Slot.JoltId      = Id.GetIndexAndSequenceNumber();
    Slot.ProxyMarked = Description.Motion == PhysicsBodyMotion::Kinematic;

    uint32_t Index;
    if (!Free.empty())
    {
        Index = Free.back();
        Free.pop_back();
        Slot.Generation = Slots[Index].Generation + 1;
        Slots[Index]    = Slot;
    }
    else
    {
        Index = static_cast<uint32_t>(Slots.size());
        Slots.push_back(Slot);
    }
    return PackToken(Index, Slots[Index].Generation);
}

void JoltAdapter::DestroyBody(PhysicsBodyToken Body)
{
    uint32_t Index;
    if (!Inner || !QuerySlot(Body, Index)) return;
    JPH::BodyInterface& Bodies = Inner->World.GetBodyInterface();
    const JPH::BodyID Id(Slots[Index].JoltId);
    Bodies.RemoveBody(Id);
    Bodies.DestroyBody(Id);
    Slots[Index].Live = false;
    Free.push_back(Index);
}

size_t JoltAdapter::QueryBodyCount() const noexcept
{
    std::size_t Live = 0;
    for (const BodySlot& Slot : Slots)
        if (Slot.Live) ++Live;
    return Live;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       KINEMATICS
//------------------------------------------------------------------------------------------------------------------------

void JoltAdapter::ConfigureBodyTransform(PhysicsBodyToken Body, const double Position[3], const double Orientation[4])
{
    uint32_t Index;
    if (!Inner || !QuerySlot(Body, Index)) return;
    Inner->World.GetBodyInterface().SetPositionAndRotation(JPH::BodyID(Slots[Index].JoltId),
        JPH::RVec3(Position[0], Position[1], Position[2]), Q4(Orientation), JPH::EActivation::Activate);
}

void JoltAdapter::ConfigureBodyVelocities(PhysicsBodyToken Body, const double Linear[3], const double Angular[3])
{
    uint32_t Index;
    if (!Inner || !QuerySlot(Body, Index)) return;
    Inner->World.GetBodyInterface().SetLinearAndAngularVelocity(JPH::BodyID(Slots[Index].JoltId), V3(Linear), V3(Angular));
}

void JoltAdapter::QueryBodyTransform(PhysicsBodyToken Body, double OutPosition[3], double OutOrientation[4]) const
{
    uint32_t Index;
    if (!Inner || !QuerySlot(Body, Index)) return;
    const JPH::BodyInterface& Bodies = Inner->World.GetBodyInterface();
    const JPH::RVec3 P = Bodies.GetPosition(JPH::BodyID(Slots[Index].JoltId));
    const JPH::Quat  Q = Bodies.GetRotation(JPH::BodyID(Slots[Index].JoltId));
    OutPosition[0] = P.GetX(); OutPosition[1] = P.GetY(); OutPosition[2] = P.GetZ();
    OutOrientation[0] = Q.GetX(); OutOrientation[1] = Q.GetY(); OutOrientation[2] = Q.GetZ(); OutOrientation[3] = Q.GetW();
}

void JoltAdapter::QueryBodyVelocities(PhysicsBodyToken Body, double OutLinear[3], double OutAngular[3]) const
{
    uint32_t Index;
    if (!Inner || !QuerySlot(Body, Index)) return;
    const JPH::BodyInterface& Bodies = Inner->World.GetBodyInterface();
    const JPH::Vec3 L = Bodies.GetLinearVelocity(JPH::BodyID(Slots[Index].JoltId));
    const JPH::Vec3 A = Bodies.GetAngularVelocity(JPH::BodyID(Slots[Index].JoltId));
    OutLinear[0] = L.GetX(); OutLinear[1] = L.GetY(); OutLinear[2] = L.GetZ();
    OutAngular[0] = A.GetX(); OutAngular[1] = A.GetY(); OutAngular[2] = A.GetZ();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         FORCES
//------------------------------------------------------------------------------------------------------------------------

void JoltAdapter::ApplyLinearImpulse(PhysicsBodyToken Body, const double Impulse[3])
{
    uint32_t Index;
    if (!Inner || !QuerySlot(Body, Index)) return;
    Inner->World.GetBodyInterface().AddImpulse(JPH::BodyID(Slots[Index].JoltId), V3(Impulse));
}

void JoltAdapter::ApplyImpulseAtPoint(PhysicsBodyToken Body, const double Impulse[3], const double Point[3])
{
    uint32_t Index;
    if (!Inner || !QuerySlot(Body, Index)) return;
    Inner->World.GetBodyInterface().AddImpulse(JPH::BodyID(Slots[Index].JoltId), V3(Impulse),
        JPH::RVec3(Point[0], Point[1], Point[2]));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         QUERIES
//------------------------------------------------------------------------------------------------------------------------

bool JoltAdapter::CastRay(const double Origin[3], const double Direction[3], double Length, PhysicsRayHit& OutHit) const
{
    if (!Inner) return false;
    JPH::RRayCast Ray(JPH::RVec3(Origin[0], Origin[1], Origin[2]), V3(Direction) * (float)Length);
    JPH::RayCastResult Result;
    if (!Inner->World.GetNarrowPhaseQuery().CastRay(Ray, Result)) { OutHit = PhysicsRayHit{}; return false; }

    OutHit.Hit      = true;
    OutHit.Distance = Result.mFraction * Length;
    for (int Axis = 0; Axis < 3; ++Axis)
        OutHit.Position[Axis] = Origin[Axis] + Direction[Axis] * Length * Result.mFraction;
    JPH::Vec3 N(0.0f, 0.0f, 1.0f);
    {
        JPH::BodyLockRead Lock(Inner->World.GetBodyLockInterface(), Result.mBodyID);
        if (Lock.Succeeded())
            N = Lock.GetBody().GetWorldSpaceSurfaceNormal(Result.mSubShapeID2,
                JPH::RVec3(OutHit.Position[0], OutHit.Position[1], OutHit.Position[2]));
    }
    OutHit.Normal[0] = N.GetX(); OutHit.Normal[1] = N.GetY(); OutHit.Normal[2] = N.GetZ();
    for (std::size_t Index = 0; Index < Slots.size(); ++Index)
    {
        if (Slots[Index].Live && Slots[Index].JoltId == Result.mBodyID.GetIndexAndSequenceNumber())
        {
            OutHit.Body = PackToken(static_cast<uint32_t>(Index), Slots[Index].Generation);
            break;
        }
    }
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       STEPPING
//------------------------------------------------------------------------------------------------------------------------

void JoltAdapter::Advance(double Seconds)
{
    if (!Inner) return;
    Accumulator += Seconds;
    const double Δτ = Inner->Configuration.SubstepSeconds > 1.0e-6 ? Inner->Configuration.SubstepSeconds : 1.0 / 240.0;
    int Substeps = 0;
    while (Accumulator >= Δτ * (1.0 - 1.0e-9) && Substeps < Inner->Configuration.MaxSubstepsPerStep)
    {
        // Capture proxy velocities before the slice so the collision Δv is measurable afterwards.
        std::vector<double> BeforeLinear(Slots.size() * 3, 0.0);
        JPH::BodyInterface& Bodies = Inner->World.GetBodyInterface();
        for (std::size_t Index = 0; Index < Slots.size(); ++Index)
        {
            if (!Slots[Index].Live || !Slots[Index].ProxyMarked) continue;
            const JPH::Vec3 L = Bodies.GetLinearVelocity(JPH::BodyID(Slots[Index].JoltId));
            BeforeLinear[Index * 3 + 0] = L.GetX();
            BeforeLinear[Index * 3 + 1] = L.GetY();
            BeforeLinear[Index * 3 + 2] = L.GetZ();
        }

        Inner->World.Update((float)Δτ, 1, Statics().TempAllocator.get(), Statics().JobLane.get());

        for (std::size_t Index = 0; Index < Slots.size(); ++Index)
        {
            if (!Slots[Index].Live || !Slots[Index].ProxyMarked) continue;
            const JPH::Vec3 L = Bodies.GetLinearVelocity(JPH::BodyID(Slots[Index].JoltId));
            for (int Axis = 0; Axis < 3; ++Axis)
                Slots[Index].ProxyDelta[Axis] += static_cast<double>(L[Axis]) - BeforeLinear[Index * 3 + Axis];
        }

        Accumulator -= Δτ;
        ++Substeps;
    }
    if (!Inner->Configuration.DeterministicCycle && Substeps == Inner->Configuration.MaxSubstepsPerStep)
        Accumulator = 0.0;
}

double JoltAdapter::CollectProxyDeltaVelocity(PhysicsBodyToken Body, double OutDelta[3])
{
    uint32_t Index;
    if (!Inner || !QuerySlot(Body, Index))
    {
        for (int Axis = 0; Axis < 3; ++Axis) OutDelta[Axis] = 0.0;
        return 0.0;
    }
    BodySlot& Slot = Slots[Index];
    double Speed = 0.0;
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        OutDelta[Axis]      = Slot.ProxyDelta[Axis];
        Speed              += Slot.ProxyDelta[Axis] * Slot.ProxyDelta[Axis];
        Slot.ProxyDelta[Axis] = 0.0;
    }
    return std::sqrt(Speed);
}

} // namespace Frontier
