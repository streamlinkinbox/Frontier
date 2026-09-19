//============================================================================================================================================
// 📦 Frontier/DisplayPresentation/CycleScheduler.h — 14-Phase Deterministic Dual-Clock Execution Scheduler
//============================================================================================================================================

#pragma once

#include "../DeviceExchange/VulkanExchange.h"
#include "../DeviceExchange/ByteSpace.h"
#include "../DeviceExchange/TaskScheduler.h"
#include "../DeviceExchange/VendorClassifier.h"
#include "../PhysicalDynamics/RigidBodySolver.h"
#include "../PhysicalDynamics/DeformableSolver.h"
#include "../PhysicalDynamics/LocomotionSolver.h"
#include "../VolumetricDynamics/LevelSetSpace.h"
#include "../VolumetricDynamics/FluidSolver.h"
#include "../VolumetricDynamics/ParticleIntegrator.h"
#include "../GeometricRaster/GeometryStructure.h"
#include "../GeometricRaster/VisibilityProjection.h"
#include "../GeometricRaster/RasterSequence.h"
#include "../GeometricRaster/MaterialCodec.h"
#include "../PhotometricIllumination/ClusteredSpace.h"
#include "../PhotometricIllumination/DirectIlluminationIntegrator.h"
#include "../PhotometricIllumination/GlobalIlluminationIntegrator.h"
#include "../PhotometricIllumination/AtmosphereIntegrator.h"
#include "../PlatformInterchange/OnlineInterchange.h"
#include "../PlatformInterchange/AcousticStructure.h"
#include "../PlatformInterchange/AcousticIntegrator.h"
#include "WorkspacePanel.h"
#include <memory>
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                CYCLE CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

struct CycleConfiguration
{
    float                   FixedTimeStepΔτ;                    // [s] fixed simulation timestep Δτ (e.g. 1/60s)
    float                   MaximumAccumulatorSeconds;          // [s] maximum accumulated physics debt
    uint32_t                ViewportWidth;                      // [px] target presentation width
    uint32_t                ViewportHeight;                     // [px] target presentation height
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 CYCLE TELEMETRY
//------------------------------------------------------------------------------------------------------------------------

struct CycleTelemetry
{
    uint64_t                CycleIndex;                         // [counter] total rendered frame cycles
    float                   FrameTimeSeconds;                   // [s] wall-clock delta time
    float                   AccumulatorSeconds;                 // [s] remaining unconsumed physics debt
    float                   InterpolationAlpha;                 // [0..1] render interpolation factor α
    size_t                  ActiveRigidBodies;                  // [count] active rigid bodies
    size_t                  ActiveDeformableParticles;          // [count] active softbody particles
    size_t                  ActiveParticles;                    // [count] active compute particles
    size_t                  ByteSpaceOccupiedBytes;             // [B] linear storage bytes used
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   CYCLE SCHEDULER
//------------------------------------------------------------------------------------------------------------------------

class CycleScheduler
{
public:
    explicit CycleScheduler(CycleConfiguration InitialConfig) noexcept;
    ~CycleScheduler() noexcept;

    CycleScheduler(const CycleScheduler&) = delete;
    CycleScheduler& operator=(const CycleScheduler&) = delete;

    [[nodiscard]] bool      Initialize() noexcept;
    void                    Terminate() noexcept;

    void                    ExecuteCycle(float WallClockDeltaSeconds) noexcept;

    [[nodiscard]] const CycleTelemetry& QueryTelemetry() const noexcept { return Telemetry; }
    [[nodiscard]] WorkspacePanel&       QueryWorkspacePanel() noexcept { return WorkspacePanelUnit; }

    // Single unified conversion operator for telemetry
    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

private:
    CycleConfiguration      Config;                             // [config] loop parameters
    CycleTelemetry          Telemetry;                          // [telemetry] per-cycle profiling metrics

    // Layer 0: Device Exchange
    std::unique_ptr<VulkanExchange>       VulkanExchangeUnit;
    std::unique_ptr<ByteSpace>            ByteSpaceUnit;
    std::unique_ptr<TaskScheduler>        TaskSchedulerUnit;
    std::unique_ptr<VendorClassifier>     VendorClassifierUnit;

    // Layer 1: Physical Dynamics
    std::unique_ptr<RigidBodySolver>      RigidBodySolverUnit;
    std::unique_ptr<DeformableSolver>     DeformableSolverUnit;
    std::unique_ptr<LocomotionSolver>     LocomotionSolverUnit;

    // Layer 2: Volumetric Dynamics
    std::unique_ptr<LevelSetSpace>        LevelSetSpaceUnit;
    std::unique_ptr<FluidSolver>          FluidSolverUnit;
    std::unique_ptr<ParticleIntegrator>   ParticleIntegratorUnit;

    // Layer 3: Geometric Raster
    std::unique_ptr<GeometryStructure>    GeometryStructureUnit;
    std::unique_ptr<VisibilityProjection> VisibilityProjectionUnit;
    std::unique_ptr<RasterSequence>       RasterSequenceUnit;
    std::unique_ptr<MaterialCodec>        MaterialCodecUnit;

    // Layer 4: Photometric Illumination
    std::unique_ptr<ClusteredSpace>                ClusteredSpaceUnit;
    std::unique_ptr<DirectIlluminationIntegrator>  DirectIlluminationIntegratorUnit;
    std::unique_ptr<GlobalIlluminationIntegrator>  GlobalIlluminationIntegratorUnit;
    std::unique_ptr<AtmosphereIntegrator>          AtmosphereIntegratorUnit;

    // Layer 5: Platform Interchange
    std::unique_ptr<OnlineInterchange>    OnlineInterchangeUnit;
    std::unique_ptr<AcousticStructure>    AcousticStructureUnit;
    std::unique_ptr<AcousticIntegrator>   AcousticIntegratorUnit;

    // Layer 6: Presentation
    WorkspacePanel                        WorkspacePanelUnit;

    // Frame Storage
    std::vector<PhotometricLightRecord>   Lights;
    std::vector<Vector4>                  RadianceField;
};

template<>
inline CycleTelemetry CycleScheduler::Convert<CycleTelemetry>() const noexcept
{
    return Telemetry;
}

template<>
inline uint64_t CycleScheduler::Convert<uint64_t>() const noexcept
{
    return Telemetry.CycleIndex;
}

} // namespace Frontier
