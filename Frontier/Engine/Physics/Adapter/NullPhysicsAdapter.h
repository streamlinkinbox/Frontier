/*============================================================================================================================================
                                                           NULLPHYSICSADAPTER.H
============================================================================================================================================*/
// 🧩 Dependency-free PhysicsAdapter: fixed-Δτ semi-implicit Euler world with a ground plane, sphere/box bound
//    colliders, analytic ray casts and momentum-exchanging vehicle proxies. The deterministic headless stand-in
//    when Jolt is unavailable; JoltAdapter remains the physical reference for collision behaviour.

#pragma once

#include "Engine/Physics/Adapter/PhysicsAdapter.h"

#include <vector>

namespace Frontier
{

class NullPhysicsAdapter final : public PhysicsAdapter
{
public:
    NullPhysicsAdapter() = default;
    ~NullPhysicsAdapter() override;

    bool               Initialize(const PhysicsWorldConfiguration& Configuration) override;
    void               Shutdown() noexcept override;
    [[nodiscard]] const char* QueryAdapterName() const noexcept override { return "NullPhysicsAdapter"; }
    [[nodiscard]] const PhysicsWorldConfiguration& QueryWorldConfiguration() const noexcept override { return World; }

    PhysicsBodyToken CreateBody(const PhysicsBodyDescription& Description) override;
    void             DestroyBody(PhysicsBodyToken Body) override;
    [[nodiscard]] size_t QueryBodyCount() const noexcept override;

    void ConfigureBodyTransform(PhysicsBodyToken Body, const double Position[3], const double Orientation[4]) override;
    void ConfigureBodyVelocities(PhysicsBodyToken Body, const double Linear[3], const double Angular[3]) override;
    void QueryBodyTransform(PhysicsBodyToken Body, double OutPosition[3], double OutOrientation[4]) const override;
    void QueryBodyVelocities(PhysicsBodyToken Body, double OutLinear[3], double OutAngular[3]) const override;

    void ApplyLinearImpulse(PhysicsBodyToken Body, const double Impulse[3]) override;
    void ApplyImpulseAtPoint(PhysicsBodyToken Body, const double Impulse[3], const double Point[3]) override;

    bool CastRay(const double Origin[3], const double Direction[3], double Length, PhysicsRayHit& OutHit) const override;

    void Advance(double Seconds) override;
    double CollectProxyDeltaVelocity(PhysicsBodyToken Body, double OutDelta[3]) override;

private:
    // 📝 One slot per created body; index + generation form the public token (slot 0 stays reserved/invalid).
    struct BodySlot
    {
        bool              Live          = false;   // [-]
        uint32_t          Generation    = 1;       // [-]
        PhysicsBodyMotion Motion        = PhysicsBodyMotion::Static;
        bool              FractureBound = false;   // [-] static fracture bound collider
        double            Position[3]   = { 0.0, 0.0, 0.0 };
        double            Orientation[4]= { 0.0, 0.0, 0.0, 1.0 };
        double            Linear[3]     = { 0.0, 0.0, 0.0 };
        double            Angular[3]    = { 0.0, 0.0, 0.0 };
        double            Mass          = 1.0;     // [kg]
        double            InvMass       = 1.0;     // [1/kg]
        double            Friction      = 0.6;
        double            Restitution   = 0.4;
        double            LinearDamping = 0.05;    // [1/s]
        double            GravityShare  = 1.0;     // [-]
        double            BoundRadius   = 0.5;     // [m] sphere bound of all colliders (impulse exchange)
        double            ProxyDelta[3] = { 0.0, 0.0, 0.0 };  // [m/s] accumulated proxy velocity exchange
    };

    void Substep(double Δτ);
    void ResolveProxyContacts(BodySlot& Proxy);

    PhysicsWorldConfiguration World{};
    std::vector<BodySlot>   Slots{ BodySlot{} };   // slot 0 = reserved invalid token
    std::vector<uint32_t>   Free;                  // recycled slot indices
    double                  Accumulator = 0.0;     // [s] unconsumed host time
};

} // namespace Frontier
