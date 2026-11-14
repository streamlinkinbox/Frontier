/*============================================================================================================================================
                                                         PARTICLESOFTWAREEXECUTOR.H
============================================================================================================================================*/
// 🧩 Modular CPU simulation orchestrator binding ParticleDocument, ForceFieldAccumulator, SDF collider & all 16 dynamics solvers

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/DistanceFieldSpace/SignedDistanceVolume.h"
#include "Engine/ParticleSpace/Dynamics/BotanicalSway.h"
#include "Engine/ParticleSpace/Dynamics/CurlManifold.h"
#include "Engine/ParticleSpace/Dynamics/EffectorLattice.h"
#include "Engine/ParticleSpace/Dynamics/FerrofluidMeniscus.h"
#include "Engine/ParticleSpace/Dynamics/FlockBoids.h"
#include "Engine/ParticleSpace/Dynamics/ForceFieldCoupling.h"
#include "Engine/ParticleSpace/Dynamics/FractureFissure.h"
#include "Engine/ParticleSpace/Dynamics/InteractiveCollider.h"
#include "Engine/ParticleSpace/Dynamics/MolecularKinetics.h"
#include "Engine/ParticleSpace/Dynamics/PyroSpectacle.h"
#include "Engine/ParticleSpace/Dynamics/ShallowWaterSplash.h"
#include "Engine/ParticleSpace/Dynamics/ShieldBarrier.h"
#include "Engine/ParticleSpace/Dynamics/SpiderwebWeave.h"
#include "Engine/ParticleSpace/Dynamics/StrandConstraint.h"
#include "Engine/ParticleSpace/Dynamics/TransitionDerez.h"
#include "Engine/ParticleSpace/Origins/StructuralOrigins.h"
#include "Engine/ParticleSpace/ParticleBuffers.h"
#include "Engine/ParticleSpace/ParticleDocument.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldAccumulator.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct RigSimulationWorkspace
{
    uint32_t                            RigId               = 0u;   // [id]    - Linked ParticleRig::RigId
    std::vector<ParticleSlot>           Slots               {};     // [-]     - Live 80B ParticleSlot array
    std::vector<StrandVertex>           StrandVertices      {};     // [-]     - Live 16B strand vertex buffer
    std::vector<StrandVertex>           PrevStrandVertices  {};     // [-]     - Previous step strand positions (XPBD)
    std::vector<StructuralStrandAnchor> StrandAnchors       {};     // [-]     - Pinned root/tip strand topology
    SpiderwebSilkLattice                WebLattice          {};     // [-]     - Spiderweb rest/live positions & bonds
    float                               ReleaseAccumulator  = 0.0f; // [count] - Fractional release carryover
    float                               BurstCooldownSec    = 0.0f; // [s]     - Periodic burst timer
    uint32_t                            SpawnCursor         = 0u;   // [idx]   - Ring-buffer cursor for next release
    uint32_t                            AliveCount          = 0u;   // [count] - Active particle slots after step
    uint32_t                            ActiveStrandCount   = 0u;   // [count] - Active strand count after step
    Mathematics::Axis3D                 LatestMagnetMeters  {};     // [m]     - Evaluated ferrofluid magnet pose
    RallyVehiclePose                    LatestRallyPose     {};     // [-]     - Evaluated figure-8 rally pose
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        CLASSES
//------------------------------------------------------------------------------------------------------------------------

class ParticleSoftwareExecutor
{
public:
    void Initialize(const ParticleDocument& Document);
    void Reset(const ParticleDocument& Document);

    void Step(
        const ParticleDocument&                         Document,
        float                                           DeltaSeconds,
        const DistanceFieldSpace::SignedDistanceVolume* OptionalSdf = nullptr);

    void SyncForceSources(const ParticleDocument& Document);

    [[nodiscard]] const RigSimulationWorkspace*                  FindWorkspace(uint32_t RigId) const noexcept;
    [[nodiscard]] std::span<const RigSimulationWorkspace>        QueryWorkspaces() const noexcept;
    [[nodiscard]] ParticleTelemetryCounters                      QueryTelemetry() const noexcept;
    [[nodiscard]] double                                         QueryElapsedSeconds() const noexcept;
    [[nodiscard]] uint64_t                                       QueryStepIndex() const noexcept;
    [[nodiscard]] const SimulationSpace::ForceFields::ForceFieldAccumulator& QueryForceAccumulator() const noexcept;

private:
    void InitializeRigWorkspace(const ParticleRig& Rig, uint32_t GlobalSeed, RigSimulationWorkspace& OutWork);
    void SpawnSingleSlot(
        const ParticleRig&      Rig,
        uint32_t                SlotIndex,
        uint32_t                GlobalSeed,
        ParticleSlot&           OutSlot) const noexcept;

    std::vector<RigSimulationWorkspace>                 Workspaces       {};
    SimulationSpace::ForceFields::ForceFieldAccumulator ForceAccumulator {};
    ParticleTelemetryCounters                           Telemetry        {};
    double                                              ElapsedSeconds   = 0.0;
    uint64_t                                            StepIndex        = 0ull;
};

} // namespace Frontier::ParticleSpace
