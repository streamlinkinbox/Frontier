//============================================================================================================================================
// 📦 Frontier/PhysicalDynamics/XPBDTyreSolver.h — Soft-body (rigid-XPBD) tyre ring, coupled to a rigid hub
//============================================================================================================================================
//
//    The tyre is the one deformable element in the vehicle (the user's explicit call: tyres are XPBD soft bodies, everything
//    else stays rigid in Jolt). This follows Müller et al. 2020, "Detailed Rigid Body Simulation with XPBD" — the same paper
//    whose Fig. 1 RC car has compliant-constraint tyres rather than an FEM mesh. A ring of particles is held to the hub by
//    compliant radial "spokes" (carcass + inflation pressure) and to its neighbours by stiff "hoop" constraints (tread
//    band). Ground contact is an inequality constraint with Coulomb friction. Because XPBD is unconditionally stable it can
//    be sub-stepped hard — the vehicle thread drives it at hundreds of Hz inside one 60 Hz Jolt step.
//
//    Coupling: the hub is treated as kinematic here (its pose is supplied each Step by the Jolt wheel body). The reaction the
//    deformed carcass transmits back through the spokes is aggregated into NetForce()/NetTorque(), which the vehicle layer
//    feeds to RigidBodySolver::ApplyForceAtPoint / ApplyTorque on the hub. That one-way pose-in / force-out handshake keeps
//    the soft tyre and the rigid solver cleanly separated across the physics thread.
//
//    ── Phase status ──────────────────────────────────────────────────────────────────────────────────────────────────
//      Phase 0 (this file): a FUNCTIONAL skeleton — a correct XPBD substep loop (predict → project spoke/hoop/contact →
//        update velocity) over a single ring, with provisional, uncalibrated stiffness/mass. It compiles, runs, and produces
//        a plausible hub reaction, establishing the architecture and the GT↔PT force handshake.
//      Phase 2 (later): multi-ring carcass + sidewall, an inflation-pressure volume constraint, a proper tread-block
//        friction/relaxation-length model, thermal coupling, and calibration + cross-validation against the Pacejka
//        baseline before it replaces the analytic tyre. DO NOT treat Phase-0 outputs as physically calibrated.

#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "../DeviceExchange/OrientationClassifier.h"    // Frontier::Vector3, Frontier::Quaternion

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                  PARAMETERS
//------------------------------------------------------------------------------------------------------------------------

struct XPBDTyreParameters
{
    uint32_t RingSegments             = 24u;       // [-]   particles around the ring (≥ 6)
    float    Radius                   = 0.34f;     // [m]   unloaded tyre radius
    float    Width                    = 0.245f;    // [m]   section width (Phase 0: single ring; retained for Phase 2)
    float    TotalMass                = 12.0f;     // [kg]  carcass mass distributed over the ring particles
    float    CarcassComplianceRadial  = 2.0e-6f;   // [m/N] spoke compliance — lower ⇒ stiffer carcass / higher pressure
    float    TreadComplianceHoop      = 1.0e-7f;   // [m/N] neighbour compliance — the tread band's hoop stiffness
    float    ContactCompliance        = 0.0f;      // [m/N] ground contact compliance (0 = rigid contact)
    float    FrictionCoefficient      = 1.3f;      // [-]   Coulomb limit on tangential contact correction
    Vector3  SpinAxisLocal            = { 0.0f, 1.0f, 0.0f };  // [-] wheel spin axis in the hub's local frame (ring ⟂ this)
    Vector3  Gravity                  = { 0.0f, 0.0f, -9.81f };// [m/s²] world gravity (Frontier is +Z up)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    NODE
//------------------------------------------------------------------------------------------------------------------------

struct XPBDTyreNode
{
    Vector3 Position;        // [m]    current world position
    Vector3 Previous;        // [m]    position at the start of the current substep (for the XPBD velocity update)
    Vector3 Velocity;        // [m/s]  world velocity
    Vector3 RestOffsetLocal; // [m]    offset from hub centre at build time, in the hub's local frame (the spoke anchor)
    float   InverseMass = 0.0f;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 XPBD TYRE SOLVER
//------------------------------------------------------------------------------------------------------------------------

class XPBDTyreSolver
{
public:
    // Ground probe: given a world point, report the ground height (world +Z) beneath it and the surface normal.
    //    Returns false where there is no ground (the node is then left in free flight). The vehicle layer wires this to
    //    RigidBodySolver::CastRay / the heightfield so the tyre reads the same terrain as the rest of the sim.
    using GroundQuery = std::function<bool(const Vector3& WorldPoint, float& OutGroundHeightZ, Vector3& OutNormal)>;

    // Builds the ring around the given hub pose. Safe to call again to rebuild with new parameters.
    void Build(const XPBDTyreParameters& Parameters, const Vector3& HubPosition, const Quaternion& HubOrientation) noexcept;

    // Advances the tyre by dt using `substeps` XPBD substeps, with the hub held at the supplied kinematic pose.
    //    Populates NetForce()/NetTorque() with the reaction transmitted to the hub over this Step.
    void Step(float DeltaSeconds, uint32_t Substeps, const Vector3& HubPosition, const Quaternion& HubOrientation,
              const GroundQuery& Ground) noexcept;

    [[nodiscard]] bool                              IsBuilt()   const noexcept { return !Nodes.empty(); }
    [[nodiscard]] const std::vector<XPBDTyreNode>&  QueryNodes() const noexcept { return Nodes; }
    [[nodiscard]] const XPBDTyreParameters&         QueryParameters() const noexcept { return Parameters; }

    // Aggregate reaction the deformed carcass exerts on the hub over the last Step (hub-centre-referenced).
    [[nodiscard]] Vector3 NetForce()  const noexcept { return AccumulatedForce; }
    [[nodiscard]] Vector3 NetTorque() const noexcept { return AccumulatedTorque; }
    [[nodiscard]] uint32_t QueryContactCount() const noexcept { return LastContactCount; }

private:
    XPBDTyreParameters        Parameters;
    std::vector<XPBDTyreNode> Nodes;
    std::vector<float>        RestHoopLength;    // [m] neighbour rest lengths, indexed by node

    Vector3  AccumulatedForce{};
    Vector3  AccumulatedTorque{};
    uint32_t LastContactCount = 0u;
};

} // namespace Frontier
