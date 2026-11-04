/*============================================================================================================================================
                                                              JOLTADAPTER.H
============================================================================================================================================*/
// 🧩 Jolt Physics 5.2.0 backend for PhysicsAdapter: bodies with simple bound colliders, impulses, ray casts and
//    fixed-Δτ substepped Advance; kinematic vehicle proxies exchange collision Δv with the loose-body world exactly
//    like the VehiclePhysics exhibit's JoltScene (the car solver stays outside Jolt).

#pragma once

#include "Engine/Physics/Adapter/PhysicsAdapter.h"

#include <memory>
#include <vector>

namespace Frontier
{

class JoltAdapter final : public PhysicsAdapter
{
public:
    JoltAdapter() = default;
    ~JoltAdapter() override;

    JoltAdapter(const JoltAdapter&)            = delete;
    JoltAdapter& operator=(const JoltAdapter&) = delete;

    bool               Initialize(const PhysicsWorldConfiguration& Configuration) override;
    void               Shutdown() noexcept override;
    [[nodiscard]] const char* QueryAdapterName() const noexcept override { return "JoltAdapter 5.2.0"; }
    [[nodiscard]] const PhysicsWorldConfiguration& QueryWorldConfiguration() const noexcept override;

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
    [[nodiscard]] bool QuerySlot(PhysicsBodyToken Body, uint32_t& OutIndex) const noexcept;

    struct BodySlot
    {
        bool       Live          = false;      // [-]
        uint32_t   Generation    = 1;          // [-]
        uint32_t   JoltId        = 0xFFFFFFFFu;// [-] packed JPH::BodyID
        bool       ProxyMarked   = false;      // [-] kinematic vehicle proxy: velocities driven by the host
        double     ProxyDelta[3] = { 0.0, 0.0, 0.0 };  // [m/s] accumulated collision Δv
    };

    struct Implementation;                     // 📝 Jolt headers stay out of this public header
    std::unique_ptr<Implementation> Inner;
    std::vector<BodySlot> Slots{ BodySlot{} }; // slot 0 reserved/invalid
    std::vector<uint32_t> Free;
    double Accumulator = 0.0;                  // [s]
};

} // namespace Frontier
