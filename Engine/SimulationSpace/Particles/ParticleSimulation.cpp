/*============================================================================================================================================
                                                         PARTICLESIMULATION.CPP
============================================================================================================================================*/
// 🧩 Composable ParticleSimulation tick execution, 32-byte telemetry summary, fluent specification & multi-simulation registry

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Particles/ParticleSimulation.h"
#include "Engine/SimulationSpace/ForceFields/FieldLatticeCascade.h"
#include "Engine/SimulationSpace/Particles/ParticleDomainSolvers.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace Frontier::SimulationSpace::Particles
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     INTERNAL ROUTINES
//------------------------------------------------------------------------------------------------------------------------

namespace
{

constexpr uint32_t RangeTileSlots = 256u; // [slots] - Contiguous 64B-aligned SoA tile granularity

[[nodiscard]] double QueryClockMicros() noexcept
{
    const auto Now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration<double, std::micro>(Now).count();
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      PARTICLE SIMULATION
//------------------------------------------------------------------------------------------------------------------------

ParticleSimulation::ParticleSimulation(std::string SimulationLabel)
    : Label(std::move(SimulationLabel))
    , Integrator(std::make_unique<SemiImplicitEulerIntegrator>())
    , RandomGenerator(1337u)
{
    Channels.ConfigureCapacity(256u, ChannelStoragePolicy::DenseSwapRemove);
}

void ParticleSimulation::ConfigureCapacity(uint32_t MaxCapacity, ChannelStoragePolicy Policy)
{
    Channels.ConfigureCapacity(std::max(1u, MaxCapacity), Policy);
}

void ParticleSimulation::AssignDeterministicSeed(uint32_t Seed) noexcept
{
    MasterSeed = (Seed != 0u) ? Seed : 1337u;
    RandomGenerator.AssignSeed(MasterSeed);
}

void ParticleSimulation::AttachVulkanExecutor(ParticleVulkanExecutor* Executor, bool SyncFromGpu) noexcept
{
    AttachedExecutor      = Executor;
    SyncChannelsFromGpu   = SyncFromGpu;
    GpuStateSynchronized  = false;
    LastTickExecutedOnGpu = false;
    LastUploadedSdf       = nullptr;
}

void ParticleSimulation::DetachVulkanExecutor() noexcept
{
    AttachedExecutor      = nullptr;
    GpuStateSynchronized  = false;
    LastTickExecutedOnGpu = false;
    LastUploadedSdf       = nullptr;
}

void ParticleSimulation::AttachSource(std::unique_ptr<ParticleSource> Source)
{
    if (!Source)
    {
        return;
    }
    Source->DeclareRequiredChannels(Channels);
    Sources.push_back(std::move(Source));
}

void ParticleSimulation::AttachMotionSolver(std::unique_ptr<ParticleMotionSolver> Solver)
{
    if (!Solver)
    {
        return;
    }
    Solver->DeclareRequiredChannels(Channels);
    MotionSolvers.push_back(std::move(Solver));
}

void ParticleSimulation::AttachCollider(std::unique_ptr<ParticleCollider> Collider)
{
    if (!Collider)
    {
        return;
    }
    Collider->DeclareRequiredChannels(Channels);
    Colliders.push_back(std::move(Collider));
}

void ParticleSimulation::AssignIntegrator(std::unique_ptr<ParticleIntegrator> StepIntegrator)
{
    if (!StepIntegrator)
    {
        return;
    }
    StepIntegrator->DeclareRequiredChannels(Channels);
    Integrator = std::move(StepIntegrator);
}

void ParticleSimulation::Tick(const SimulationTickInputs& Inputs)
{
    const double StartMicros = QueryClockMicros();
    const float  Dt          = std::clamp(Inputs.DeltaTimeSeconds, 0.0f, 0.1f);
    if (Dt <= 0.0f)
    {
        ComputeReadbackSummary(StartMicros);
        return;
    }

    SimulationTickInputs SafeInputs = Inputs;
    SafeInputs.DeltaTimeSeconds     = Dt;

    // 1. Release new particle slots via attached sources
    uint32_t ReleasedSlotsThisTick = 0u;
    for (std::unique_ptr<ParticleSource>& Source : Sources)
    {
        ReleasedSlotsThisTick += Source->ReleaseParticles(Channels, SafeInputs, RandomGenerator);
    }

    // 1B. Auto-delegate natively to Vulkan 1.3 Compute when eligible & attached
    LastTickExecutedOnGpu = false;
    if (AttachedExecutor != nullptr &&
        AttachedExecutor->QueryDeviceReady() &&
        RequestedTarget != ExecutionTarget::CpuParallel &&
        QueryGpuComputeEligible())
    {
        if (TryExecuteTickOnVulkanGpu(SafeInputs, ReleasedSlotsThisTick))
        {
            LastTickExecutedOnGpu = true;
            ReadbackSummary.RevisionCounter += 1u;
            return;
        }
    }

    const uint32_t ActiveCount = Channels.QueryActiveCount();
    if (ActiveCount > 0u)
    {
        // 2. Per-tick initialization (e.g. orbiting magnet pole, boids snapshot)
        for (std::unique_ptr<ParticleMotionSolver>& Solver : MotionSolvers)
        {
            Solver->InitializeTick(Channels, SafeInputs);
        }

        // 3. Evaluate modular motion solvers in contiguous SoA tiles
        for (uint32_t TileBegin = 0u; TileBegin < ActiveCount; TileBegin += RangeTileSlots)
        {
            const uint32_t TileEnd = std::min(ActiveCount, TileBegin + RangeTileSlots);
            for (const std::unique_ptr<ParticleMotionSolver>& Solver : MotionSolvers)
            {
                Solver->EvaluateRange(Channels, TileBegin, TileEnd, SafeInputs);
            }
        }

        // 4. Advance positions via active integrator
        if (Integrator)
        {
            for (uint32_t TileBegin = 0u; TileBegin < ActiveCount; TileBegin += RangeTileSlots)
            {
                const uint32_t TileEnd = std::min(ActiveCount, TileBegin + RangeTileSlots);
                Integrator->AdvanceRange(Channels, TileBegin, TileEnd, SafeInputs);
            }
        }

        // 5. Enforce modular colliders (GroundPlane, SphereContact, SignedDistanceVolume)
        for (uint32_t TileBegin = 0u; TileBegin < ActiveCount; TileBegin += RangeTileSlots)
        {
            const uint32_t TileEnd = std::min(ActiveCount, TileBegin + RangeTileSlots);
            for (const std::unique_ptr<ParticleCollider>& Collider : Colliders)
            {
                Collider->EnforceRange(Channels, TileBegin, TileEnd, SafeInputs);
            }
        }

        // 6. Post-range structural finalization (XPBD constraints, pyro sub-bursts)
        for (std::unique_ptr<ParticleMotionSolver>& Solver : MotionSolvers)
        {
            Solver->FinalizeTick(Channels, SafeInputs, RandomGenerator);
        }

        // 7. Advance lifespans & reclaim expired slots synchronously across ALL channels (including custom user types)
        ReclaimExpiredLifespans(Dt);
    }

    ReadbackSummary.RevisionCounter += 1u;
    ComputeReadbackSummary(StartMicros);
}

void ParticleSimulation::TriggerBurst(uint32_t OverrideCount) noexcept
{
    for (std::unique_ptr<ParticleSource>& Source : Sources)
    {
        Source->TriggerImmediateBurst(OverrideCount);
    }
}

void ParticleSimulation::Reset(uint32_t OverrideSeed) noexcept
{
    if (OverrideSeed != 0u)
    {
        MasterSeed = OverrideSeed;
    }
    RandomGenerator.AssignSeed(MasterSeed);
    Channels.ResetAllChannels();

    for (std::unique_ptr<ParticleSource>& Source : Sources)
    {
        Source->ResetSource();
    }
    for (std::unique_ptr<ParticleMotionSolver>& Solver : MotionSolvers)
    {
        Solver->ResetSolver();
    }

    GpuStateSynchronized  = false;
    LastTickExecutedOnGpu = false;
    ReadbackSummary       = ParticleReadbackSummary{};
}

bool ParticleSimulation::TryExecuteTickOnVulkanGpu(
    const SimulationTickInputs& SafeInputs,
    uint32_t                    ReleasedSlotsThisTick)
{
    if (AttachedExecutor == nullptr || !AttachedExecutor->QueryDeviceReady())
    {
        return false;
    }

    const uint32_t ActiveCount = Channels.QueryActiveCount();
    if (ActiveCount == 0u)
    {
        ReadbackSummary.AliveCount = 0u;
        return true;
    }

    // Upload CPU-released slots on first sync or whenever new slots were emitted into Channels
    if (!GpuStateSynchronized || ReleasedSlotsThisTick > 0u)
    {
        if (!AttachedExecutor->UploadChannelBank(Channels, LastGpuDiagnostic))
        {
            return false;
        }
        GpuStateSynchronized = true;
    }

    // Upload 3D SignedDistanceVolume once whenever attached or swapped
    if (SafeInputs.WorldDistanceField != nullptr && SafeInputs.WorldDistanceField != LastUploadedSdf)
    {
        if (!AttachedExecutor->UploadSignedDistanceVolume(*SafeInputs.WorldDistanceField, LastGpuDiagnostic))
        {
            return false;
        }
        LastUploadedSdf = SafeInputs.WorldDistanceField;
    }

    ParticleGpuStepRequest GpuReq{};
    GpuReq.Uniforms.DeltaTimeSec      = SafeInputs.DeltaTimeSeconds;
    GpuReq.Uniforms.TimeSeconds       = static_cast<float>(SafeInputs.TimeSeconds);
    GpuReq.Uniforms.ActiveCount       = ActiveCount;
    GpuReq.Uniforms.Capacity          = std::max(Channels.QueryCapacity(), ActiveCount);
    GpuReq.Uniforms.ReleaseStartSlot  = 0u;
    GpuReq.Uniforms.ReleaseCount      = 0u;
    GpuReq.Uniforms.MasterSeed        = MasterSeed ^ (ReadbackSummary.RevisionCounter * 747796405u + 2891336453u);
    GpuReq.Uniforms.GravityDragCurl   = { 0.0f, 0.0f, 0.0f, 0.0f };
    GpuReq.Uniforms.CurlTimingBounds  = { 0.85f, 0.6f, 0.0f, 0.35f };
    GpuReq.Uniforms.ForceCoupling     = { 0.0f, 0.0f, 0.0f, 0.2f };
    GpuReq.Uniforms.ColliderControl   = { 0u, 0u, 0u, ReadbackSummary.RevisionCounter };
    GpuReq.Uniforms.SizeSpinMetrics   = { 0.06f, 0.04f, 0.0f, 0.0f }; // SizeMul = 0 keeps authored Appearance when no AppearanceCurveSolver attached

    // Inspect attached motion solvers to pack exact GPU uniform contracts + specialized domain pass
    for (std::unique_ptr<ParticleMotionSolver>& SolverPtr : MotionSolvers)
    {
        if (const auto* Grav = dynamic_cast<const GravityBuoyancySolver*>(SolverPtr.get()))
        {
            const auto& P = Grav->QueryParameters();
            GpuReq.Uniforms.GravityDragCurl[0] = P.GravityScale;
            GpuReq.Uniforms.GravityDragCurl[1] = P.BuoyancyAccelMs2;
            GpuReq.Uniforms.GravityDragCurl[2] = P.DragCoefficient;
        }
        else if (const auto* Curl = dynamic_cast<const CurlTurbulenceSolver*>(SolverPtr.get()))
        {
            const auto& P = Curl->QueryParameters();
            GpuReq.Uniforms.GravityDragCurl[3]  = P.AmplitudeMs;
            GpuReq.Uniforms.CurlTimingBounds[0] = P.FrequencyPerM;
            GpuReq.Uniforms.CurlTimingBounds[1] = P.TimeScrollSpeed;
        }
        else if (const auto* ForceCoupler = dynamic_cast<const ForceFieldCouplingSolver*>(SolverPtr.get()))
        {
            const auto& P = ForceCoupler->QueryParameters();
            GpuReq.Uniforms.ForceCoupling[0] = P.CouplingGain;
            GpuReq.Uniforms.ForceCoupling[1] = P.AccelScale;
            GpuReq.Uniforms.ForceCoupling[2] = P.DragScale;
        }
        else if (const auto* Appear = dynamic_cast<const AppearanceCurveSolver*>(SolverPtr.get()))
        {
            const auto& P = Appear->QueryParameters();
            GpuReq.Uniforms.ColorStopA        = P.ColorA;
            GpuReq.Uniforms.ColorStopC        = P.ColorC;
            GpuReq.Uniforms.ColorStopB        = P.ColorB;
            GpuReq.Uniforms.SizeSpinMetrics   = { P.SizeStartMeters, P.SizeEndMeters, P.SizeMultiplier, 0.0f };
            GpuReq.Uniforms.ColorDriverNorm[0]= P.SpeedNormMaxMs;
            GpuReq.Uniforms.ColorDriverNorm[1]= P.HeightNormMaxM;
            GpuReq.Uniforms.ColorDriverNorm[2]= P.PhaseOffset;
            GpuReq.Uniforms.ColorDriverMode[0]= static_cast<uint32_t>(P.Driver);
            GpuReq.Uniforms.ColorDriverMode[1]= static_cast<uint32_t>(P.WrapMode);
        }
        else if (const auto* Strand = dynamic_cast<const XpbdStrandSolver*>(SolverPtr.get()))
        {
            const auto& P = Strand->QueryParameters();
            const float Stiff01 = std::clamp(P.ElasticStiffness * 0.45f, 0.15f, 0.96f);
            const float Cross01 = Stiff01 * 0.65f;
            uint32_t CrossBits = 0u;
            std::memcpy(&CrossBits, &Cross01, sizeof(uint32_t));
            GpuReq.Uniforms.ColorDriverNorm[3] = Stiff01;
            GpuReq.Uniforms.ColorDriverMode[3] = CrossBits;
            GpuReq.StrandRelaxIterations       = std::clamp(P.SubIterations, 1u, 12u);
        }
        else if (auto* Ferro = dynamic_cast<RosensweigFerrofluidSolver*>(SolverPtr.get()))
        {
            Ferro->InitializeTick(Channels, SafeInputs);
            const auto& P = Ferro->QueryParameters();
            const auto Pole = Ferro->QueryEvaluatedPoleMeters();
            GpuReq.DomainPass = SpecializedDomainPass::Ferrofluid;
            GpuReq.DomainUniforms.OriginScalarA    = { P.OriginMeters.X, P.OriginMeters.Y, P.OriginMeters.Z, P.MaxCuspHeightMeters };
            GpuReq.DomainUniforms.AuxVectorScalarB = { Pole.X, Pole.Y, Pole.Z, P.OrbitSpeedRadSec };
            GpuReq.DomainUniforms.ScalarPackC      = { P.CapillaryDamping, P.VesselRadiusMeters, 0.0f, 0.0f };
        }
        else if (const auto* Pyro = dynamic_cast<const PyroVolleyBurstSolver*>(SolverPtr.get()))
        {
            const auto& P = Pyro->QueryParameters();
            GpuReq.DomainPass = SpecializedDomainPass::PyroVolley;
            GpuReq.DomainUniforms.OriginScalarA    = { P.LaunchOriginMeters.X, P.LaunchOriginMeters.Y, P.LaunchOriginMeters.Z, P.AscentSpeedMs };
            GpuReq.DomainUniforms.AuxVectorScalarB = { 0.0f, 0.0f, 1.0f, P.BurstSpeedMs };
            GpuReq.DomainUniforms.ScalarPackC      = { static_cast<float>(P.StarsPerBurst), static_cast<float>(P.PatternCode), 0.0f, 0.0f };
        }
        else if (const auto* Boids = dynamic_cast<const BoidsFlockSolver*>(SolverPtr.get()))
        {
            const auto& P = Boids->QueryParameters();
            GpuReq.DomainPass = SpecializedDomainPass::BoidsFlock;
            GpuReq.DomainUniforms.OriginScalarA = { P.HomeOriginMeters.X, P.HomeOriginMeters.Y, P.HomeOriginMeters.Z, P.HomeRadiusMeters };
            GpuReq.DomainUniforms.ScalarPackC   = { P.PerceptionMeters, P.SeparationMeters, P.CruiseSpeedMs, P.MaxSteerAccelMs2 };
            GpuReq.DomainUniforms.DomainControl[2] = 0u;
            for (const DynamicSphereCollider& Sphere : SafeInputs.SphereColliders)
            {
                if (Sphere.Active)
                {
                    GpuReq.DomainUniforms.AuxVectorScalarB = { Sphere.CentreMeters.X, Sphere.CentreMeters.Y, Sphere.CentreMeters.Z, Sphere.RadiusMeters };
                    GpuReq.DomainUniforms.DomainControl[2] = 1u;
                    break;
                }
            }
        }
        else if (const auto* Frac = dynamic_cast<const FractureCrackSolver*>(SolverPtr.get()))
        {
            const auto& P = Frac->QueryParameters();
            GpuReq.DomainPass = SpecializedDomainPass::Fracture;
            GpuReq.DomainUniforms.OriginScalarA = { P.OriginMeters.X, P.OriginMeters.Y, P.OriginMeters.Z, P.SheetRadiusMeters };
            GpuReq.DomainUniforms.ScalarPackC   = { P.BranchDensity, P.IncisionDepthMeters, static_cast<float>(P.Pattern), 0.0f };
        }
        else if (const auto* MoGraph = dynamic_cast<const MoGraphEffectorSolver*>(SolverPtr.get()))
        {
            const auto& P = MoGraph->QueryParameters();
            GpuReq.DomainPass = SpecializedDomainPass::LatticeEffector;
            GpuReq.DomainUniforms.OriginScalarA    = { P.OriginMeters.X, P.OriginMeters.Y, P.OriginMeters.Z, P.ExtrusionMeters };
            GpuReq.DomainUniforms.ScalarPackC      = { P.FalloffMeters, P.WaveFrequencyHz, P.SpacingMeters, 0.0f };
            GpuReq.DomainUniforms.DomainControl[2] = P.EnableDerezLaunch ? 1u : 0u;
        }
        else if (auto* Shield = dynamic_cast<EnergyShieldRippleSolver*>(SolverPtr.get()))
        {
            Shield->InitializeTick(Channels, SafeInputs);
            const auto& P   = Shield->QueryParameters();
            const auto  Imp = Shield->QueryLastImpactPoint();
            GpuReq.DomainPass = SpecializedDomainPass::ShieldRipple;
            GpuReq.DomainUniforms.OriginScalarA    = { P.CentreMeters.X, P.CentreMeters.Y, P.CentreMeters.Z, P.RadiusMeters };
            GpuReq.DomainUniforms.AuxVectorScalarB = { Imp.X, Imp.Y, Imp.Z, P.RippleSpeedHz };
            GpuReq.DomainUniforms.ScalarPackC      = { P.RippleSpeedHz, 0.0f, 0.0f, 0.0f };
            GpuReq.DomainUniforms.TimingCounts[2]  = static_cast<float>(Shield->QueryLastImpactTimeSec());
            GpuReq.DomainUniforms.DomainControl[2] = 0u; // Mode 0 = Energy Shield Ripple
        }
        else if (const auto* Space = dynamic_cast<const SpaceRefractionSolver*>(SolverPtr.get()))
        {
            const auto& P = Space->QueryParameters();
            GpuReq.DomainPass = SpecializedDomainPass::SpaceRefraction;
            GpuReq.DomainUniforms.OriginScalarA    = { P.SingularityMeters.X, P.SingularityMeters.Y, P.SingularityMeters.Z, P.InnerRadiusMeters };
            GpuReq.DomainUniforms.ScalarPackC      = { P.OrbitalStrength, P.ThermalRiseMs, 0.0f, 0.0f };
            GpuReq.DomainUniforms.DomainControl[2] = 1u; // Mode 1 = Space Refraction
        }
    }

    // Inspect attached colliders
    for (const std::unique_ptr<ParticleCollider>& ColliderPtr : Colliders)
    {
        if (const auto* Ground = dynamic_cast<const GroundPlaneCollider*>(ColliderPtr.get()))
        {
            const auto& P = Ground->QueryParameters();
            GpuReq.Uniforms.ColliderControl[0]  = 1u;
            GpuReq.Uniforms.CurlTimingBounds[2] = P.FloorZMeters;
            GpuReq.Uniforms.CurlTimingBounds[3] = P.Restitution;
            GpuReq.Uniforms.ForceCoupling[3]    = P.SlidingFriction;
        }
        else if (dynamic_cast<const SphereContactCollider*>(ColliderPtr.get()) != nullptr)
        {
            for (const DynamicSphereCollider& Sphere : SafeInputs.SphereColliders)
            {
                if (Sphere.Active)
                {
                    GpuReq.Uniforms.ColliderControl[1] = 1u;
                    GpuReq.Uniforms.SphereCenterRadius = { Sphere.CentreMeters.X, Sphere.CentreMeters.Y, Sphere.CentreMeters.Z, Sphere.RadiusMeters };
                    GpuReq.Uniforms.SphereVelBounce    = { Sphere.VelocityMs.X, Sphere.VelocityMs.Y, Sphere.VelocityMs.Z, Sphere.Restitution };
                    break;
                }
            }
        }
        else if (const auto* SdfCol = dynamic_cast<const SignedDistanceVolumeCollider*>(ColliderPtr.get()))
        {
            if (SafeInputs.WorldDistanceField != nullptr)
            {
                const auto& P = SdfCol->QueryParameters();
                GpuReq.ApplySdfCollider              = true;
                GpuReq.DomainUniforms.TimingCounts[3]= P.Restitution;
                GpuReq.DomainUniforms.ScalarPackD[3] = P.SurfaceFriction;
            }
        }
    }

    // Pack ForceFieldAccumulator sources & waypoints into GPU SSBO buffers
    std::vector<ForceFields::ForceSourceShaderEntry> PackedSources{};
    std::vector<ForceFields::SpatialVector3>         PackedWaypoints{};
    if (SafeInputs.ForceAccumulator != nullptr)
    {
        ForceFields::FieldLatticeCascade Packer(8u);
        Packer.PackGpuSourceBuffers(*SafeInputs.ForceAccumulator, SafeInputs.TimeSeconds, PackedSources, PackedWaypoints);
        GpuReq.PackedForceSources   = PackedSources;
        GpuReq.PackedForceWaypoints = PackedWaypoints;
    }

    if (!AttachedExecutor->DispatchStep(GpuReq, ReadbackSummary, LastGpuDiagnostic))
    {
        return false;
    }

    // Mirror newly detonated GPU Pyro slots + channel updates when SyncChannelsFromGpu is active
    if (GpuReq.DomainPass == SpecializedDomainPass::PyroVolley)
    {
        const uint32_t PostPyroSlots = AttachedExecutor->QueryLastPyroAllocatedSlots();
        if (PostPyroSlots > Channels.QueryActiveCount())
        {
            uint32_t BeginSlot = 0u;
            (void)Channels.AcquireSlots(PostPyroSlots - Channels.QueryActiveCount(), BeginSlot);
        }
    }

    if (SyncChannelsFromGpu)
    {
        (void)AttachedExecutor->DownloadChannelBank(Channels, LastGpuDiagnostic);
        // Reclaim expired slots in DenseSwapRemove mode so continuous emitters never saturate capacity
        if (Channels.QueryStoragePolicy() == ChannelStoragePolicy::DenseSwapRemove)
        {
            auto Life = Channels.AccessChannel(ChannelLayouts::Lifespan);
            if (!Life.empty())
            {
                uint32_t Slot = 0u;
                bool ReclaimedAny = false;
                while (Slot < Channels.QueryActiveCount())
                {
                    if ((Life[Slot].LivenessMask & 1u) == 0u ||
                        (Life[Slot].MaxLifeSeconds > 0.0f && Life[Slot].AgeSeconds >= Life[Slot].MaxLifeSeconds))
                    {
                        Channels.ReclaimSlot(Slot);
                        ReclaimedAny = true;
                        continue;
                    }
                    ++Slot;
                }
                if (ReclaimedAny)
                {
                    GpuStateSynchronized = false; // Re-upload compacted slots next tick
                }
            }
        }
    }

    return true;
}

bool ParticleSimulation::QueryGpuComputeEligible() const noexcept
{
    if (!Channels.QueryChannelsGpuResident())
    {
        return false;
    }
    for (const std::unique_ptr<ParticleSource>& Source : Sources)
    {
        if (!Source->QueryGpuComputeSupported())
        {
            return false;
        }
    }
    for (const std::unique_ptr<ParticleMotionSolver>& Solver : MotionSolvers)
    {
        if (!Solver->QueryGpuComputeSupported())
        {
            return false;
        }
    }
    for (const std::unique_ptr<ParticleCollider>& Collider : Colliders)
    {
        if (!Collider->QueryGpuComputeSupported())
        {
            return false;
        }
    }
    if (Integrator && !Integrator->QueryGpuComputeSupported())
    {
        return false;
    }
    return true;
}

void ParticleSimulation::ReclaimExpiredLifespans(float DeltaTimeSeconds) noexcept
{
    auto Lifespans = Channels.AccessChannel(ChannelLayouts::Lifespan);
    if (Lifespans.empty())
    {
        return;
    }

    uint32_t Slot = 0u;
    while (Slot < Channels.QueryActiveCount())
    {
        LifespanChannel& Life = Lifespans[Slot];
        Life.AgeSeconds += DeltaTimeSeconds;
        if (Life.MaxLifeSeconds > 0.0f && Life.AgeSeconds >= Life.MaxLifeSeconds)
        {
            if (Channels.QueryStoragePolicy() == ChannelStoragePolicy::DenseSwapRemove)
            {
                Channels.ReclaimSlot(Slot);
                // Re-inspect same Slot because last active slot was swapped here
                continue;
            }
            Life.LivenessMask = 0u;
        }
        ++Slot;
    }
}

void ParticleSimulation::ComputeReadbackSummary(double TickStartMicros) noexcept
{
    const uint32_t Active = Channels.QueryActiveCount();
    auto Kin              = Channels.QueryChannel(ChannelLayouts::Kinematics);

    float PeakSpeedSq = 0.0f;
    float SumX        = 0.0f;
    float SumY        = 0.0f;
    float SumZ        = 0.0f;
    if (!Kin.empty() && Active > 0u)
    {
        for (uint32_t Slot = 0u; Slot < Active; ++Slot)
        {
            const KinematicsChannel& Body = Kin[Slot];
            const float SpeedSq           = Body.VelX * Body.VelX + Body.VelY * Body.VelY + Body.VelZ * Body.VelZ;
            if (std::isfinite(SpeedSq) && SpeedSq > PeakSpeedSq)
            {
                PeakSpeedSq = SpeedSq;
            }
            SumX += Body.PosX;
            SumY += Body.PosY;
            SumZ += Body.PosZ;
        }
    }

    const float InvActive = (Active > 0u) ? (1.0f / static_cast<float>(Active)) : 0.0f;
    const float CenterX   = SumX * InvActive;
    const float CenterY   = SumY * InvActive;
    const float CenterZ   = SumZ * InvActive;

    float MaxRadiusSq = 0.0f;
    if (!Kin.empty() && Active > 0u)
    {
        for (uint32_t Slot = 0u; Slot < Active; ++Slot)
        {
            const float Dx  = Kin[Slot].PosX - CenterX;
            const float Dy  = Kin[Slot].PosY - CenterY;
            const float Dz  = Kin[Slot].PosZ - CenterZ;
            const float RSq = Dx * Dx + Dy * Dy + Dz * Dz;
            if (std::isfinite(RSq) && RSq > MaxRadiusSq)
            {
                MaxRadiusSq = RSq;
            }
        }
    }

    ReadbackSummary.AliveCount         = Active;
    ReadbackSummary.PeakSpeedMs        = std::sqrt(PeakSpeedSq);
    ReadbackSummary.BoundsRadiusMeters = std::sqrt(MaxRadiusSq);
    ReadbackSummary.TickDurationUs     = static_cast<float>(std::max(0.0, QueryClockMicros() - TickStartMicros));
    ReadbackSummary.BoundsCenterX      = CenterX;
    ReadbackSummary.BoundsCenterY      = CenterY;
    ReadbackSummary.BoundsCenterZ      = CenterZ;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           FLUENT C++20 SIMULATION SPECIFICATION
//------------------------------------------------------------------------------------------------------------------------

ParticleSimulationSpecification::ParticleSimulationSpecification(std::string SimulationLabel)
    : AssembledSimulation(std::move(SimulationLabel))
{
}

ParticleSimulationSpecification& ParticleSimulationSpecification::ConfigureCapacity(
    uint32_t             MaxCapacity,
    ChannelStoragePolicy Policy)
{
    AssembledSimulation.ConfigureCapacity(MaxCapacity, Policy);
    return *this;
}

ParticleSimulationSpecification& ParticleSimulationSpecification::AssignDeterministicSeed(uint32_t Seed) noexcept
{
    AssembledSimulation.AssignDeterministicSeed(Seed);
    return *this;
}

ParticleSimulationSpecification& ParticleSimulationSpecification::AssignExecutionTarget(ExecutionTarget Target) noexcept
{
    AssembledSimulation.AssignExecutionTarget(Target);
    return *this;
}

ParticleSimulationSpecification& ParticleSimulationSpecification::AttachCustomSource(
    std::string                           SourceLabel,
    CustomParticleSource::ChannelCallback DeclareCallback,
    CustomParticleSource::ReleaseCallback ReleaseCallback)
{
    AssembledSimulation.AttachSource(
        std::make_unique<CustomParticleSource>(
            std::move(SourceLabel),
            std::move(DeclareCallback),
            std::move(ReleaseCallback)));
    return *this;
}

ParticleSimulationSpecification& ParticleSimulationSpecification::AttachCustomMotionSolver(
    std::string                            SolverLabel,
    CustomParticleSolver::ChannelCallback  DeclareCallback,
    CustomParticleSolver::EvaluateCallback EvaluateCallback)
{
    AssembledSimulation.AttachMotionSolver(
        std::make_unique<CustomParticleSolver>(
            std::move(SolverLabel),
            std::move(DeclareCallback),
            std::move(EvaluateCallback)));
    return *this;
}

ParticleSimulation ParticleSimulationSpecification::Construct()
{
    return std::move(AssembledSimulation);
}

//------------------------------------------------------------------------------------------------------------------------
//                                              PARTICLE SIMULATION REGISTRY
//------------------------------------------------------------------------------------------------------------------------

void ParticleSimulationRegistry::RegisterSimulation(uint32_t UniqueToken, ParticleSimulation Simulation)
{
    for (TokenizedEntry& Entry : Entries)
    {
        if (Entry.UniqueToken == UniqueToken)
        {
            Entry.Simulation = std::move(Simulation);
            return;
        }
    }
    Entries.push_back(TokenizedEntry{ UniqueToken, std::move(Simulation) });
}

bool ParticleSimulationRegistry::RemoveSimulation(uint32_t UniqueToken) noexcept
{
    for (std::size_t Index = 0u; Index < Entries.size(); ++Index)
    {
        if (Entries[Index].UniqueToken == UniqueToken)
        {
            Entries.erase(Entries.begin() + static_cast<std::ptrdiff_t>(Index));
            return true;
        }
    }
    return false;
}

void ParticleSimulationRegistry::ClearAllSimulations() noexcept
{
    Entries.clear();
}

void ParticleSimulationRegistry::TickAll(const SimulationTickInputs& Inputs)
{
    for (TokenizedEntry& Entry : Entries)
    {
        Entry.Simulation.Tick(Inputs);
    }
}

void ParticleSimulationRegistry::ResetAll() noexcept
{
    for (TokenizedEntry& Entry : Entries)
    {
        Entry.Simulation.Reset();
    }
}

void ParticleSimulationRegistry::TriggerBurst(uint32_t UniqueToken, uint32_t OverrideCount) noexcept
{
    if (ParticleSimulation* Sim = FindSimulation(UniqueToken))
    {
        Sim->TriggerBurst(OverrideCount);
    }
}

ParticleSimulation* ParticleSimulationRegistry::FindSimulation(uint32_t UniqueToken) noexcept
{
    for (TokenizedEntry& Entry : Entries)
    {
        if (Entry.UniqueToken == UniqueToken)
        {
            return &Entry.Simulation;
        }
    }
    return nullptr;
}

const ParticleSimulation* ParticleSimulationRegistry::FindSimulation(uint32_t UniqueToken) const noexcept
{
    for (const TokenizedEntry& Entry : Entries)
    {
        if (Entry.UniqueToken == UniqueToken)
        {
            return &Entry.Simulation;
        }
    }
    return nullptr;
}

uint32_t ParticleSimulationRegistry::QueryTotalAliveParticles() const noexcept
{
    uint32_t TotalAlive = 0u;
    for (const TokenizedEntry& Entry : Entries)
    {
        TotalAlive += Entry.Simulation.QueryActiveCount();
    }
    return TotalAlive;
}

} // namespace Frontier::SimulationSpace::Particles
