/*============================================================================================================================================
                                                          NULLPHYSICSADAPTER.CPP
============================================================================================================================================*/
// 🧩 Dependency-free adapter mechanics: fixed Δτ substeps, ground-plane support, and a momentum-conserving
//    proxy exchange (impulse j = (1+e)·μ·max(vrel·n, 0) along the line of centres) mirroring the Jolt proxy contract.

#include "Engine/Physics/Adapter/NullPhysicsAdapter.h"

#include <cmath>

namespace Frontier
{

namespace
{
    inline PhysicsBodyToken PackToken(uint32_t Index, uint32_t Generation)
    {
        return (static_cast<PhysicsBodyToken>(Generation) << 32) | static_cast<PhysicsBodyToken>(Index);
    }

    inline double ClampMagnitude(double V, double Lo, double Hi) noexcept { return V < Lo ? Lo : (V > Hi ? Hi : V); }

    // 🔍 Bounding sphere of the whole collider set (max over colliders of |offset| + local radius).
    double ResolveBoundRadius(const PhysicsBodyDescription& Description)
    {
        double Radius = 0.0;
        for (const PhysicsColliderDescription& C : Description.Colliders)
        {
            double Local = 0.0;
            switch (C.Shape)
            {
            case PhysicsColliderShape::BoundingSphere:
                Local = C.Radius;
                break;
            case PhysicsColliderShape::BoundBox:
                Local = std::sqrt(C.HalfExtents[0] * C.HalfExtents[0] + C.HalfExtents[1] * C.HalfExtents[1]
                                  + C.HalfExtents[2] * C.HalfExtents[2]);
                break;
            case PhysicsColliderShape::BoundingCapsule:
                Local = C.CapsuleHalfHeight + C.Radius;
                break;
            case PhysicsColliderShape::ConvexHull:
            {
                for (std::size_t P = 0; P + 2 < C.HullPositions.size(); P += 3)
                {
                    const double R = std::sqrt(C.HullPositions[P] * C.HullPositions[P]
                                               + C.HullPositions[P + 1] * C.HullPositions[P + 1]
                                               + C.HullPositions[P + 2] * C.HullPositions[P + 2]) + C.Radius;
                    Local = Local < R ? R : Local;
                }
                break;
            }
            }
            const double Off = std::sqrt(C.PositionOffset[0] * C.PositionOffset[0] + C.PositionOffset[1] * C.PositionOffset[1]
                                         + C.PositionOffset[2] * C.PositionOffset[2]);
            Radius = Radius < Off + Local ? Off + Local : Radius;
        }
        return Radius > 1.0e-6 ? Radius : 0.5;
    }
}

NullPhysicsAdapter::~NullPhysicsAdapter()
{
    Shutdown();
}

bool NullPhysicsAdapter::Initialize(const PhysicsWorldConfiguration& Configuration)
{
    World       = Configuration;
    Accumulator = 0.0;
    return true;
}

void NullPhysicsAdapter::Shutdown() noexcept
{
    Slots.assign(1, BodySlot{});
    Free.clear();
    Accumulator = 0.0;
}

PhysicsBodyToken NullPhysicsAdapter::CreateBody(const PhysicsBodyDescription& Description)
{
    BodySlot Slot;
    Slot.Live          = true;
    Slot.Motion        = Description.FractureBoundStatic ? PhysicsBodyMotion::Static : Description.Motion;
    Slot.FractureBound = Description.FractureBoundStatic;
    for (int Axis = 0; Axis < 3; ++Axis) Slot.Position[Axis]    = Description.Position[Axis];
    for (int Axis = 0; Axis < 4; ++Axis) Slot.Orientation[Axis] = Description.Orientation[Axis];
    for (int Axis = 0; Axis < 3; ++Axis) Slot.Linear[Axis]      = Description.LinearVelocity[Axis];
    for (int Axis = 0; Axis < 3; ++Axis) Slot.Angular[Axis]     = Description.AngularVelocity[Axis];
    Slot.Mass          = Description.MassKilograms > 1.0e-6 ? Description.MassKilograms : 1.0;
    Slot.InvMass       = Slot.Motion == PhysicsBodyMotion::Dynamic ? 1.0 / Slot.Mass : 0.0;
    Slot.Friction      = Description.Friction;
    Slot.Restitution   = Description.Restitution;
    Slot.LinearDamping = Description.LinearDamping;
    Slot.GravityShare  = Description.GravityShare;
    Slot.BoundRadius   = ResolveBoundRadius(Description);

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

void NullPhysicsAdapter::DestroyBody(PhysicsBodyToken Body)
{
    const uint32_t Index = static_cast<uint32_t>(Body & 0xFFFFFFFFu);
    if (Index == 0 || Index >= Slots.size()) return;
    if (PackToken(Index, Slots[Index].Generation) != Body) return;
    Slots[Index].Live = false;
    Free.push_back(Index);
}

size_t NullPhysicsAdapter::QueryBodyCount() const noexcept
{
    std::size_t Live = 0;
    for (const BodySlot& Slot : Slots)
        if (Slot.Live) ++Live;
    return Live;
}

void NullPhysicsAdapter::ConfigureBodyTransform(PhysicsBodyToken Body, const double Position[3], const double Orientation[4])
{
    const uint32_t Index = static_cast<uint32_t>(Body & 0xFFFFFFFFu);
    if (Index >= Slots.size() || !Slots[Index].Live) return;
    for (int Axis = 0; Axis < 3; ++Axis) Slots[Index].Position[Axis]    = Position[Axis];
    for (int Axis = 0; Axis < 4; ++Axis) Slots[Index].Orientation[Axis] = Orientation[Axis];
}

void NullPhysicsAdapter::ConfigureBodyVelocities(PhysicsBodyToken Body, const double Linear[3], const double Angular[3])
{
    const uint32_t Index = static_cast<uint32_t>(Body & 0xFFFFFFFFu);
    if (Index >= Slots.size() || !Slots[Index].Live) return;
    for (int Axis = 0; Axis < 3; ++Axis) Slots[Index].Linear[Axis]  = Linear[Axis];
    for (int Axis = 0; Axis < 3; ++Axis) Slots[Index].Angular[Axis] = Angular[Axis];
}

void NullPhysicsAdapter::QueryBodyTransform(PhysicsBodyToken Body, double OutPosition[3], double OutOrientation[4]) const
{
    const uint32_t Index = static_cast<uint32_t>(Body & 0xFFFFFFFFu);
    if (Index >= Slots.size() || !Slots[Index].Live) return;
    for (int Axis = 0; Axis < 3; ++Axis) OutPosition[Axis]    = Slots[Index].Position[Axis];
    for (int Axis = 0; Axis < 4; ++Axis) OutOrientation[Axis] = Slots[Index].Orientation[Axis];
}

void NullPhysicsAdapter::QueryBodyVelocities(PhysicsBodyToken Body, double OutLinear[3], double OutAngular[3]) const
{
    const uint32_t Index = static_cast<uint32_t>(Body & 0xFFFFFFFFu);
    if (Index >= Slots.size() || !Slots[Index].Live) return;
    for (int Axis = 0; Axis < 3; ++Axis) OutLinear[Axis]  = Slots[Index].Linear[Axis];
    for (int Axis = 0; Axis < 3; ++Axis) OutAngular[Axis] = Slots[Index].Angular[Axis];
}

void NullPhysicsAdapter::ApplyLinearImpulse(PhysicsBodyToken Body, const double Impulse[3])
{
    const uint32_t Index = static_cast<uint32_t>(Body & 0xFFFFFFFFu);
    if (Index >= Slots.size() || !Slots[Index].Live) return;
    BodySlot& Slot = Slots[Index];
    for (int Axis = 0; Axis < 3; ++Axis) Slot.Linear[Axis] += Impulse[Axis] * Slot.InvMass;
}

void NullPhysicsAdapter::ApplyImpulseAtPoint(PhysicsBodyToken Body, const double Impulse[3], const double Point[3])
{
    (void)Point;   // 📝 bound-sphere bodies carry no inertia tensor yet; the linear share is exact
    ApplyLinearImpulse(Body, Impulse);
}

bool NullPhysicsAdapter::CastRay(const double Origin[3], const double Direction[3], double Length, PhysicsRayHit& OutHit) const
{
    // 🔍 Analytic sphere cast per body: solve |O + tD − C| = r inside [0, Length].
    bool   Near       = false;
    double NearT      = Length;
    const BodySlot* NearSlot = nullptr;
    for (const BodySlot& Slot : Slots)
    {
        if (!Slot.Live) continue;
        const double Oc[3] = { Origin[0] - Slot.Position[0], Origin[1] - Slot.Position[1], Origin[2] - Slot.Position[2] };
        const double B = Oc[0] * Direction[0] + Oc[1] * Direction[1] + Oc[2] * Direction[2];
        const double C = Oc[0] * Oc[0] + Oc[1] * Oc[1] + Oc[2] * Oc[2] - Slot.BoundRadius * Slot.BoundRadius;
        const double Discriminant = B * B - C;
        if (Discriminant < 0.0) continue;
        const double T = -B - std::sqrt(Discriminant);
        if (T < 0.0 || T > NearT) continue;
        Near = true; NearT = T; NearSlot = &Slot;
    }
    if (!Near) { OutHit = PhysicsRayHit{}; return false; }
    OutHit.Hit      = true;
    OutHit.Distance = NearT;
    for (int Axis = 0; Axis < 3; ++Axis) OutHit.Position[Axis] = Origin[Axis] + Direction[Axis] * NearT;
    const double Inv = 1.0 / std::fmax(1.0e-9, NearSlot->BoundRadius);
    for (int Axis = 0; Axis < 3; ++Axis) OutHit.Normal[Axis] = (OutHit.Position[Axis] - NearSlot->Position[Axis]) * Inv;
    const std::ptrdiff_t SlotIndex = NearSlot - Slots.data();
    OutHit.Body = PackToken(static_cast<uint32_t>(SlotIndex), NearSlot->Generation);
    return true;
}

void NullPhysicsAdapter::Advance(double Seconds)
{
    Accumulator += Seconds;
    const double Δτ = World.SubstepSeconds > 1.0e-6 ? World.SubstepSeconds : 1.0 / 240.0;
    int Substeps = 0;
    while (Accumulator >= Δτ * (1.0 - 1.0e-9) && Substeps < World.MaxSubstepsPerStep)
    {
        Substep(Δτ);
        Accumulator -= Δτ;
        ++Substeps;
    }
    if (!World.DeterministicCycle && Substeps == World.MaxSubstepsPerStep)
        Accumulator = 0.0;   // ⚠️ spiral-of-death clamp: drop the tail when running free
}

double NullPhysicsAdapter::CollectProxyDeltaVelocity(PhysicsBodyToken Body, double OutDelta[3])
{
    const uint32_t Index = static_cast<uint32_t>(Body & 0xFFFFFFFFu);
    if (Index >= Slots.size() || !Slots[Index].Live)
    {
        for (int Axis = 0; Axis < 3; ++Axis) OutDelta[Axis] = 0.0;
        return 0.0;
    }
    BodySlot& Slot = Slots[Index];
    double Speed = 0.0;
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        OutDelta[Axis]  = Slot.ProxyDelta[Axis];
        Speed          += Slot.ProxyDelta[Axis] * Slot.ProxyDelta[Axis];
        Slot.ProxyDelta[Axis] = 0.0;
    }
    return std::sqrt(Speed);
}

void NullPhysicsAdapter::Substep(double Δτ)
{
    for (BodySlot& Slot : Slots)
    {
        if (!Slot.Live) continue;
        if (Slot.Motion == PhysicsBodyMotion::Dynamic)
        {
            for (int Axis = 0; Axis < 3; ++Axis) Slot.Linear[Axis] += World.Gravity[Axis] * Slot.GravityShare * Δτ;
            const double Damp = std::fmax(0.0, 1.0 - Slot.LinearDamping * Δτ);
            for (int Axis = 0; Axis < 3; ++Axis) Slot.Linear[Axis] *= Damp;
        }
        if (Slot.Motion != PhysicsBodyMotion::Static)
        {
            for (int Axis = 0; Axis < 3; ++Axis) Slot.Position[Axis] += Slot.Linear[Axis] * Δτ;
        }

        // 📝 Ground plane z = 0: rest contact through the bound sphere (normal impulse + Coulomb friction share).
        const double Floor = Slot.BoundRadius;
        if (Slot.Motion == PhysicsBodyMotion::Dynamic && Slot.Position[2] < Floor)
        {
            Slot.Position[2] = Floor;
            if (Slot.Linear[2] < 0.0) Slot.Linear[2] = -Slot.Linear[2] * Slot.Restitution;
            Slot.Linear[0] *= std::fmax(0.0, 1.0 - Slot.Friction * 0.5 * Δτ);
            Slot.Linear[1] *= std::fmax(0.0, 1.0 - Slot.Friction * 0.5 * Δτ);
        }
    }

    // Vehicle proxy exchange against the other bodies (momentum split along the line of centres).
    for (BodySlot& Slot : Slots)
    {
        if (!Slot.Live || Slot.Motion != PhysicsBodyMotion::Kinematic) continue;
        ResolveProxyContacts(Slot);
    }
}

void NullPhysicsAdapter::ResolveProxyContacts(BodySlot& Proxy)
{
    for (BodySlot& Other : Slots)
    {
        if (!Other.Live) continue;
        if (&Other == &Proxy) continue;
        const double Dx = Other.Position[0] - Proxy.Position[0];
        const double Dy = Other.Position[1] - Proxy.Position[1];
        const double Dz = Other.Position[2] - Proxy.Position[2];
        const double Radii = Proxy.BoundRadius + Other.BoundRadius;
        const double D2 = Dx * Dx + Dy * Dy + Dz * Dz;
        if (D2 >= Radii * Radii || D2 < 1.0e-12) continue;

        const double D = std::sqrt(D2);
        const double N[3] = { Dx / D, Dy / D, Dz / D };
        const double Vrel = (Proxy.Linear[0] - Other.Linear[0]) * N[0]
                          + (Proxy.Linear[1] - Other.Linear[1]) * N[1]
                          + (Proxy.Linear[2] - Other.Linear[2]) * N[2];
        if (Vrel <= 0.0) continue;

        const double e = ClampMagnitude(Other.Restitution, 0.0, 1.0);
        if (Other.Motion == PhysicsBodyMotion::Dynamic)
        {
            // 📝 Inelastic shove: the dynamic body leaves at proxy closing speed; the proxy pays the equal-opposite
            //    impulse, emerging in ProxyDelta for the host solver (cone drag-back).
            const double J = (1.0 + e) * Other.Mass * Vrel;
            Other.Linear[0] += J * Other.InvMass * N[0];
            Other.Linear[1] += J * Other.InvMass * N[1];
            Other.Linear[2] += J * Other.InvMass * N[2];
            const double ProxyTax = J / Proxy.Mass;
            Proxy.ProxyDelta[0] -= ProxyTax * N[0];
            Proxy.ProxyDelta[1] -= ProxyTax * N[1];
            Proxy.ProxyDelta[2] -= ProxyTax * N[2];
            const double Push = Radii - D;
            Other.Position[0] += N[0] * Push;
            Other.Position[1] += N[1] * Push;
            Other.Position[2] += N[2] * Push;
        }
        else
        {
            // Static wall: the proxy loses its inward closing share (1 + e)·v·n (the exchange record).
            const double Loss = (1.0 + e) * Vrel;
            Proxy.ProxyDelta[0] += Loss * N[0];
            Proxy.ProxyDelta[1] += Loss * N[1];
            Proxy.ProxyDelta[2] += Loss * N[2];
        }
    }
}

} // namespace Frontier
