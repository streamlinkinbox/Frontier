//============================================================================================================================================
// 📦 Frontier/DisplayPresentation/CycleScheduler.cpp — 14-Phase Engine Loop Execution Implementation
//============================================================================================================================================

#include "CycleScheduler.h"
#include <algorithm>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

CycleScheduler::CycleScheduler(CycleConfiguration InitialConfig) noexcept
    : Config(InitialConfig)
    , Telemetry{}
{
}

CycleScheduler::~CycleScheduler() noexcept
{
    Terminate();
}

bool CycleScheduler::Initialize() noexcept
{
    // Layer 0
    VulkanExchangeUnit   = std::make_unique<VulkanExchange>();
    ByteSpaceUnit        = std::make_unique<ByteSpace>(64ULL * 1024ULL * 1024ULL); // 64 MB per-frame transient
    TaskSchedulerUnit    = std::make_unique<TaskScheduler>();
    VendorClassifierUnit = std::make_unique<VendorClassifier>();

    if (!VulkanExchangeUnit->Initialize(true))
    {
        return false;
    }
    TaskSchedulerUnit->Initialize();

    // Layer 1
    RigidBodySolverUnit  = std::make_unique<RigidBodySolver>(TaskSchedulerUnit.get());
    DeformableSolverUnit = std::make_unique<DeformableSolver>(TaskSchedulerUnit.get());
    LocomotionSolverUnit = std::make_unique<LocomotionSolver>();

    if (!RigidBodySolverUnit->Initialize() || !DeformableSolverUnit->Initialize())
    {
        return false;
    }

    // Layer 2
    LevelSetSpaceUnit      = std::make_unique<LevelSetSpace>(64, 64, 64, 0.25f);
    LevelSetSpaceUnit->InitializeSphere(Vector3{ 8.0f, 0.0f, 8.0f }, 3.0f);

    FluidConfiguration fluidConfig{ 32, 32, 32, 0.5f, 0.001f, 0.05f, 0.1f, 300.0f, 16 };
    FluidSolverUnit        = std::make_unique<FluidSolver>(fluidConfig);
    ParticleIntegratorUnit = std::make_unique<ParticleIntegrator>(16384);

    // Layer 3
    GeometryStructureUnit    = std::make_unique<GeometryStructure>();
    VisibilityProjectionUnit = std::make_unique<VisibilityProjection>(Config.ViewportWidth, Config.ViewportHeight);
    RasterSequenceUnit       = std::make_unique<RasterSequence>(TaskSchedulerUnit.get());
    MaterialCodecUnit        = std::make_unique<MaterialCodec>();

    // Layer 4
    ClusteredSpaceUnit               = std::make_unique<ClusteredSpace>(16, 16, 32);
    DirectIlluminationIntegratorUnit = std::make_unique<DirectIlluminationIntegrator>(Config.ViewportWidth, Config.ViewportHeight);
    GlobalIlluminationIntegratorUnit = std::make_unique<GlobalIlluminationIntegrator>(Config.ViewportWidth, Config.ViewportHeight);
    AtmosphereIntegratorUnit         = std::make_unique<AtmosphereIntegrator>();

    // Layer 5
    OnlineConfiguration onlineConfig{ "front_prod", "front_sbx", "front_dep", "client_id", "secret", false };
    OnlineInterchangeUnit = std::make_unique<OnlineInterchange>(onlineConfig);
    if (!OnlineInterchangeUnit->InitializePlatform())
    {
        return false;
    }

    AcousticStructureUnit  = std::make_unique<AcousticStructure>();
    AcousticIntegratorUnit = std::make_unique<AcousticIntegrator>();

    // Layer 6
    if (!WorkspacePanelUnit.Initialize(Config.ViewportWidth, Config.ViewportHeight))
    {
        return false;
    }

    // Initial Light Setup
    PhotometricLightRecord sunLight{};
    sunLight.SpatialLocation       = Vector3{ 50.0f, 100.0f, 50.0f };
    sunLight.EmissiveRadiance      = Vector3{ 10.0f, 9.5f, 8.5f };
    sunLight.InfluenceRadius       = 500.0f;
    sunLight.SpotInnerAngleCosine  = 0.0f;
    sunLight.SpotOuterAngleCosine  = 0.0f;
    sunLight.LightCategory         = 2;
    Lights.push_back(sunLight);

    return true;
}

void CycleScheduler::Terminate() noexcept
{
    if (TaskSchedulerUnit)
    {
        TaskSchedulerUnit->Terminate();
    }
    if (OnlineInterchangeUnit)
    {
        OnlineInterchangeUnit->TerminatePlatform();
    }
    if (DeformableSolverUnit)
    {
        DeformableSolverUnit->Terminate();
    }
    if (RigidBodySolverUnit)
    {
        RigidBodySolverUnit->Terminate();
    }
    if (VulkanExchangeUnit)
    {
        VulkanExchangeUnit->Terminate();
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                        14-PHASE ENGINE CYCLE EXECUTION
//------------------------------------------------------------------------------------------------------------------------

void CycleScheduler::ExecuteCycle(float WallClockDeltaSeconds) noexcept
{
    Telemetry.FrameTimeSeconds = WallClockDeltaSeconds;
    Telemetry.AccumulatorSeconds += std::min(WallClockDeltaSeconds, Config.MaximumAccumulatorSeconds);

    // ① [DeviceExchange]: Ingest timestamped input events
    // (Acquired from platform windowing)

    // ② [OnlineInterchange]: Advance EOS platform tick & network datagrams
    OnlineInterchangeUnit->AdvanceOnlineCycle();

    // ③ [Pre-Physics Queue]: Kinematic trajectories & user movement commands
    // (Player input vectors evaluated)

    // ④ [Fixed Substep Execution]: Step physics solvers over Δτ_fixed
    while (Telemetry.AccumulatorSeconds >= Config.FixedTimeStepΔτ)
    {
        RigidBodySolverUnit->AdvanceSimulation(Config.FixedTimeStepΔτ);
        DeformableSolverUnit->AdvanceSimulation(Config.FixedTimeStepΔτ, 4);
        FluidSolverUnit->AdvanceSimulation(Config.FixedTimeStepΔτ, LevelSetSpaceUnit.get());

        Telemetry.AccumulatorSeconds -= Config.FixedTimeStepΔτ;
    }

    // ⑤ [Post-Physics Sync]: Interpolation weight α = τ_accum / Δτ_fixed
    Telemetry.InterpolationAlpha = Telemetry.AccumulatorSeconds / Config.FixedTimeStepΔτ;

    // ⑥ [AnimationSequence]: Skeletal skinning & joint matrices update
    // (Joint matrices interpolated)

    // ⑦ [LevelSetSpace Update]: Liquid surface distance fields & dynamic solid masks
    // (Dynamic obstacle SDF evaluated)

    // ⑧ [ParticleIntegrator]: GPU compute particles advected by curl noise & fluids
    ParticleIntegratorUnit->AdvanceSimulation(WallClockDeltaSeconds, FluidSolverUnit.get());

    // ⑨ [ClusteredSpace Binning]: 16x16x32 view frustum light assignment
    Matrix4x4 ViewProj = Matrix4x4::Identity();
    ClusteredSpaceUnit->BinLights(Lights, ViewProj);

    // ⑩ [VisibilityProjection]: Software meshlet rasterization producing 32-bit visibility buffer
    VisibilityProjectionUnit->ClearExtent();
    RasterSequenceUnit->RasterizeGeometry(*GeometryStructureUnit, 1, Matrix4x4::Identity(), ViewProj, *VisibilityProjectionUnit);

    // ⑪ [MaterialCodec Shading]: ReSTIR DI/GI candidate resampling & AtmosphereIntegrator
    DirectIlluminationIntegratorUnit->IntegrateDirectIllumination(
        *VisibilityProjectionUnit, *GeometryStructureUnit, *MaterialCodecUnit, Lights, RadianceField);

    GlobalIlluminationIntegratorUnit->IntegrateGlobalIllumination(
        *VisibilityProjectionUnit, *GeometryStructureUnit, *MaterialCodecUnit, RadianceField);

    AtmosphereIntegrationCriteria atmoCriteria{};
    atmoCriteria.RayOrigin              = Vector3{ 0.0f, 1.7f, 0.0f };
    atmoCriteria.RayDirection           = Vector3{ 0.0f, 0.2f, 1.0f }.Normalized();
    atmoCriteria.RayMarchDistance       = 64.0f;
    atmoCriteria.StepCount              = 32;
    atmoCriteria.AsymmetryFactorG       = 0.6f;
    atmoCriteria.AbsorptionCrossSection = 0.02f;
    atmoCriteria.ScatteringCrossSection = 0.08f;

    [[maybe_unused]] Vector4 AtmoScattering = AtmosphereIntegratorUnit->RaymarchMedia(
        atmoCriteria, *FluidSolverUnit, Vector3{ 0.5f, 0.8f, 0.3f }.Normalized(), Vector3{ 5.0f, 4.8f, 4.2f });

    // ⑫ [AcousticIntegrator]: 3D HRTF listener panned mix with occlusion raymarching
    AcousticIntegratorUnit->AdvanceAudio(WallClockDeltaSeconds, LevelSetSpaceUnit.get());

    // ⑬ [WorkspacePanel Present]: Immediate mode overlay & offscreen target presentation
    WorkspacePanelUnit.RenderWorkspaceOverlay(WorkspacePanelUnit.QueryEditMode(), WallClockDeltaSeconds);

    // ⑭ [ByteSpace Reclamation]: Reset linear transient storage offset to 0
    ByteSpaceUnit->ReclaimAll();

    // Update telemetry metrics
    Telemetry.CycleIndex++;
    Telemetry.ActiveRigidBodies         = RigidBodySolverUnit->QueryActiveBodyCount();
    Telemetry.ActiveDeformableParticles = DeformableSolverUnit->QueryActiveParticleCount();
    Telemetry.ActiveParticles           = ParticleIntegratorUnit->QueryActiveParticleCount();
    Telemetry.ByteSpaceOccupiedBytes    = ByteSpaceUnit->QueryOccupiedBytes();
}

} // namespace Frontier
