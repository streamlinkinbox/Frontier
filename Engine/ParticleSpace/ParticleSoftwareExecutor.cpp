/*============================================================================================================================================
                                                        PARTICLESOFTWAREEXECUTOR.CPP
============================================================================================================================================*/
// 🧩 Orchestration of modular release origins, 16 dynamics solvers, SimulationSpace::ForceFields & ParticleTintDriver

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/ParticleSoftwareExecutor.h"

#include "Engine/ColourSpace/ColourRampSampler.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/ParticleSpace/Dynamics/BallisticMotion.h"
#include "Engine/ParticleSpace/Origins/AnalyticOrigins.h"

#include <algorithm>
#include <cmath>
#include <variant>

namespace Frontier::ParticleSpace
{

namespace
{

[[nodiscard]] Mathematics::Axis3D ExtractOriginCentre(const ReleaseOriginSpec& Origin) noexcept
{
    return std::visit([](const auto& Spec) noexcept -> Mathematics::Axis3D
    {
        using SpecT = std::decay_t<decltype(Spec)>;
        if constexpr (std::is_same_v<SpecT, DualElectrodeOriginSpec>)
        {
            return Spec.CathodeMeters;
        }
        else
        {
            return Spec.OriginMeters;
        }
    }, Origin);
}

[[nodiscard]] bool HasStandaloneDirectSolver(const SimulationDynamicsProfile& Dyn) noexcept
{
    return (Dyn.Ferrofluid.FerroMode != 0u) ||
           (Dyn.Fracture.CrackMode != 0u) ||
           (Dyn.EffectorGrid.Active) ||
           (Dyn.Shield.Active) ||
           (Dyn.Pyro.BeamModeActive > 0.0f);
}

[[nodiscard]] bool IsBotanicalStrandMode(StrandDynamicsMode Mode) noexcept
{
    return Mode == StrandDynamicsMode::GrassWheat ||
           Mode == StrandDynamicsMode::TallReeds ||
           Mode == StrandDynamicsMode::BroadFern ||
           Mode == StrandDynamicsMode::CanopyTree ||
           Mode == StrandDynamicsMode::UnderwaterKelp ||
           Mode == StrandDynamicsMode::SeaAnemone;
}

} // namespace

void ParticleSoftwareExecutor::Initialize(const ParticleDocument& Document)
{
    ElapsedSeconds = 0.0;
    StepIndex      = 0ull;
    SyncForceSources(Document);

    Workspaces.clear();
    Workspaces.reserve(Document.Rigs.size());
    for (const ParticleRig& Rig : Document.Rigs)
    {
        RigSimulationWorkspace Work{};
        InitializeRigWorkspace(Rig, Document.RandomSeed, Work);
        Workspaces.push_back(std::move(Work));
    }

    Step(Document, 1.0f / 60.0f, nullptr);
}

void ParticleSoftwareExecutor::Reset(const ParticleDocument& Document)
{
    Initialize(Document);
}

void ParticleSoftwareExecutor::SyncForceSources(const ParticleDocument& Document)
{
    ForceAccumulator.AssignSources(Document.ForceSources);
}

void ParticleSoftwareExecutor::InitializeRigWorkspace(
    const ParticleRig&      Rig,
    uint32_t                GlobalSeed,
    RigSimulationWorkspace& OutWork)
{
    OutWork.RigId = Rig.RigId;
    const uint32_t Capacity = std::clamp(Rig.Sizing.CapacitySlots, 32u, 65536u);
    OutWork.Slots.assign(Capacity, ParticleSlot{});

    // Mark all ballistic slots inactive initially unless pre-populated below
    for (uint32_t I = 0u; I < Capacity; ++I)
    {
        OutWork.Slots[I].PositionAge[3]      = 1000.0f;
        OutWork.Slots[I].VelocityLifetime[3] = 1.0f;
        OutWork.Slots[I].Attributes[3]       = HashOriginScalar(I, GlobalSeed + Rig.RigId);
    }

    // Populate structural strands if this rig uses ClothGrid, BotanicalRoot, or CanopySkeleton
    uint32_t VertsPerStrand = 0u;
    if (const auto* ClothSpec = std::get_if<ClothGridOriginSpec>(&Rig.Origin))
    {
        OutWork.StrandAnchors = BuildClothGridAnchors(*ClothSpec);
        VertsPerStrand = std::clamp(ClothSpec->SegmentCount, 4u, MaxRibbonStrandVertices);
    }
    else if (const auto* BotSpec = std::get_if<BotanicalRootOriginSpec>(&Rig.Origin))
    {
        OutWork.StrandAnchors = BuildBotanicalAnchors(*BotSpec);
        VertsPerStrand = std::clamp(BotSpec->SegmentCount, 4u, MaxRibbonStrandVertices);
    }
    else if (const auto* CanopySpec = std::get_if<CanopySkeletonOriginSpec>(&Rig.Origin))
    {
        OutWork.StrandAnchors = BuildCanopySkeletonAnchors(*CanopySpec);
        VertsPerStrand = std::clamp(CanopySpec->SegmentCount, 4u, MaxRibbonStrandVertices);
    }
    else if (const auto* WebSpec = std::get_if<SpiderwebOriginSpec>(&Rig.Origin))
    {
        OutWork.WebLattice = InitializeSpiderwebLattice(*WebSpec);
        OutWork.ActiveStrandCount = static_cast<uint32_t>(OutWork.WebLattice.Bonds.size());
    }

    if (!OutWork.StrandAnchors.empty() && VertsPerStrand >= 2u)
    {
        OutWork.StrandVertices.resize(OutWork.StrandAnchors.size() * VertsPerStrand);
        OutWork.PrevStrandVertices.resize(OutWork.StrandVertices.size());
        for (std::size_t S = 0u; S < OutWork.StrandAnchors.size(); ++S)
        {
            const std::size_t VertexOffset = S * VertsPerStrand;
            InitializeStrandVertices(
                std::span<StrandVertex>(OutWork.StrandVertices.data() + VertexOffset, VertsPerStrand),
                std::span<StrandVertex>(OutWork.PrevStrandVertices.data() + VertexOffset, VertsPerStrand),
                OutWork.StrandAnchors[S],
                0.03f);
        }
        OutWork.ActiveStrandCount = static_cast<uint32_t>(OutWork.StrandAnchors.size());
    }

    // Pre-populate persistent population modes
    if (Rig.Dynamics.Boids.Active || Rig.Dynamics.Molecular.Active ||
        Rig.Dynamics.Curl.Mode != CurlManifoldMode::Off ||
        Rig.Dynamics.Transition.Mode != TransitionDerezMode::Off ||
        Rig.Dynamics.Botanical.SeedHeadMode != 0u)
    {
        for (uint32_t I = 0u; I < Capacity; ++I)
        {
            SpawnSingleSlot(Rig, I, GlobalSeed, OutWork.Slots[I]);
            if (Rig.Dynamics.Molecular.Active)
            {
                OutWork.Slots[I].PhaseTiming[2] = (HashOriginScalar(I, GlobalSeed + 17u) < Rig.Dynamics.Molecular.InitialFractionA) ? 0.0f : 1.0f;
            }
            if (Rig.Dynamics.Transition.Mode != TransitionDerezMode::Off || Rig.Dynamics.Botanical.SeedHeadMode != 0u)
            {
                OutWork.Slots[I].VelocityLifetime[3] = 120.0f;
            }
        }
    }
    else if (!HasStandaloneDirectSolver(Rig.Dynamics) && Rig.Dynamics.Pyro.FireworkMode != 0u)
    {
        const uint32_t BurstCount = std::min<uint32_t>(Capacity, static_cast<uint32_t>(Rig.FireworkTargets.Points.size()));
        for (uint32_t I = 0u; I < BurstCount; ++I)
        {
            const FireworkStarPoint& Star = Rig.FireworkTargets.Points[I];
            ParticleSlot& S = OutWork.Slots[I];
            S.PositionAge[0]      = Star.OffsetX * 0.25f;
            S.PositionAge[1]      = Star.OffsetY * 0.25f;
            S.PositionAge[2]      = 2.6f + Star.OffsetZ * 0.25f;
            S.PositionAge[3]      = 0.0f;
            S.VelocityLifetime[0] = Star.OffsetX * 4.5f;
            S.VelocityLifetime[1] = Star.OffsetY * 4.5f;
            S.VelocityLifetime[2] = Star.OffsetZ * 4.5f;
            S.VelocityLifetime[3] = std::max(1.2f, Rig.Sizing.LifetimeMaxSec);
            S.Attributes[0]       = Rig.Sizing.SizeStartMeters;
            S.ColourRgba[0]       = Star.Red;
            S.ColourRgba[1]       = Star.Green;
            S.ColourRgba[2]       = Star.Blue;
            S.ColourRgba[3]       = 1.0f;
        }
    }
}

void ParticleSoftwareExecutor::SpawnSingleSlot(
    const ParticleRig& Rig,
    uint32_t           SlotIndex,
    uint32_t           GlobalSeed,
    ParticleSlot&      OutSlot) const noexcept
{
    const uint32_t Capacity = std::max(1u, static_cast<uint32_t>(Rig.Sizing.CapacitySlots));
    const uint32_t Salt     = GlobalSeed ^ (static_cast<uint32_t>(StepIndex) * 2654435761u) ^ (Rig.RigId * 97u);

    OriginSamplePoint Pt{};
    if (!Rig.SampledAnchors.Anchors.empty() && std::holds_alternative<SurfaceAnchorOriginSpec>(Rig.Origin))
    {
        const auto& Anchors = Rig.SampledAnchors.Anchors;
        const auto& Pick    = Anchors[SlotIndex % Anchors.size()];
        Pt.PositionMeters   = Pick.PositionMeters;
        Pt.NormalAxis       = Pick.NormalAxis;
    }
    else
    {
        Pt = SampleAnalyticOrigin(Rig.Origin, SlotIndex, Capacity, Salt);
    }

    const float U0 = HashOriginScalar(SlotIndex, Salt + 11u);
    const float U1 = HashOriginScalar(SlotIndex, Salt + 29u);
    const float U2 = HashOriginScalar(SlotIndex, Salt + 53u);
    const float U3 = HashOriginScalar(SlotIndex, Salt + 79u);

    const float Speed = Rig.Dynamics.Ballistics.SpeedMin +
                        (Rig.Dynamics.Ballistics.SpeedMax - Rig.Dynamics.Ballistics.SpeedMin) * U0;

    Mathematics::Axis3D Dir = Rig.Dynamics.Ballistics.DirectionAxis;
    if (std::holds_alternative<SphereVolumeOriginSpec>(Rig.Origin))
    {
        Dir = Pt.NormalAxis;
    }
    const float Spread = Rig.Dynamics.Ballistics.SpreadRadians;
    const float Theta  = U1 * Constants::TauFloat;
    const float ConeR  = std::tan(std::clamp(Spread * 0.5f, 0.0f, 1.45f)) * std::sqrt(U2);
    float Vx = Dir.X + std::cos(Theta) * ConeR;
    float Vy = Dir.Y + std::sin(Theta) * ConeR;
    float Vz = Dir.Z + (U2 - 0.5f) * Spread * 0.5f;
    const float VLen = std::sqrt(std::max(1.0e-6f, Vx * Vx + Vy * Vy + Vz * Vz));
    Vx = (Vx / VLen) * Speed;
    Vy = (Vy / VLen) * Speed;
    Vz = (Vz / VLen) * Speed;

    OutSlot.PositionAge[0]      = Pt.PositionMeters.X;
    OutSlot.PositionAge[1]      = Pt.PositionMeters.Y;
    OutSlot.PositionAge[2]      = Pt.PositionMeters.Z;
    OutSlot.PositionAge[3]      = 0.0f;
    OutSlot.VelocityLifetime[0] = Vx;
    OutSlot.VelocityLifetime[1] = Vy;
    OutSlot.VelocityLifetime[2] = Vz;
    OutSlot.VelocityLifetime[3] = Rig.Sizing.LifetimeMinSec +
                                  (Rig.Sizing.LifetimeMaxSec - Rig.Sizing.LifetimeMinSec) * U3;
    OutSlot.Attributes[0]       = Rig.Sizing.SizeStartMeters;
    OutSlot.Attributes[1]       = U1 * Constants::TauFloat;
    OutSlot.Attributes[2]       = Rig.Sizing.AngularSpinRate * (U2 * 2.0f - 1.0f);
    OutSlot.Attributes[3]       = U3;
    OutSlot.PhaseTiming[0]      = 0.0f;
    OutSlot.PhaseTiming[1]      = 0.0f;
}

void ParticleSoftwareExecutor::Step(
    const ParticleDocument&                         Document,
    float                                           DeltaSeconds,
    const DistanceFieldSpace::SignedDistanceVolume* OptionalSdf)
{
    if (Document.PlaybackPaused || DeltaSeconds <= 0.0f)
    {
        return;
    }

    const float Dt = std::clamp(DeltaSeconds * Document.TimeScale, 1.0e-4f, 0.1f);
    ElapsedSeconds += static_cast<double>(Dt);
    ++StepIndex;
    const float ElapsedF = static_cast<float>(ElapsedSeconds);

    ForceAccumulator.StepAnimation(ElapsedSeconds, true);

    bool HasSolo = false;
    for (const ParticleRig& Rig : Document.Rigs)
    {
        if (Rig.Solo)
        {
            HasSolo = true;
            break;
        }
    }

    Telemetry = ParticleTelemetryCounters{};
    float MinX =  1.0e9f, MinY =  1.0e9f, MinZ =  1.0e9f;
    float MaxX = -1.0e9f, MaxY = -1.0e9f, MaxZ = -1.0e9f;

    const std::size_t RigCount = std::min(Document.Rigs.size(), Workspaces.size());
    for (std::size_t R = 0u; R < RigCount; ++R)
    {
        const ParticleRig&      Rig  = Document.Rigs[R];
        RigSimulationWorkspace& Work = Workspaces[R];
        if (!Rig.Enabled || Rig.Quarantined || (HasSolo && !Rig.Solo))
        {
            Work.AliveCount = 0u;
            continue;
        }

        const Mathematics::Axis3D Origin = ExtractOriginCentre(Rig.Origin);

        // 1. Strand / botanical / spiderweb structural updates
        if (!Work.StrandAnchors.empty())
        {
            const uint32_t VertsPerStrand = static_cast<uint32_t>(Work.StrandVertices.size() / Work.StrandAnchors.size());
            uint32_t IntactStrands = 0u;
            for (std::size_t S = 0u; S < Work.StrandAnchors.size(); ++S)
            {
                const std::size_t VertexOffset = S * VertsPerStrand;
                auto VertSpan = std::span<StrandVertex>(Work.StrandVertices.data() + VertexOffset, VertsPerStrand);
                auto PrevSpan = std::span<StrandVertex>(Work.PrevStrandVertices.data() + VertexOffset, VertsPerStrand);
                if (IsBotanicalStrandMode(Rig.Dynamics.Strands.Mode))
                {
                    StepBotanicalFoliageStrand(
                        VertSpan,
                        Rig.Dynamics.Botanical,
                        Rig.Dynamics.Strands,
                        Work.StrandAnchors[S],
                        Document.Collider,
                        Document.AmbientWindMps,
                        ElapsedF);
                    ++IntactStrands;
                }
                else
                {
                    if (StepStrandXpbd(
                            VertSpan,
                            PrevSpan,
                            Rig.Dynamics.Strands,
                            Work.StrandAnchors[S],
                            Document.Collider,
                            Document.AmbientWindMps,
                            Dt,
                            ElapsedF))
                    {
                        ++IntactStrands;
                    }
                }
            }
            Work.ActiveStrandCount = IntactStrands;
            Telemetry.ActiveStrandCount += IntactStrands;
        }
        else if (Rig.Dynamics.Spiderweb.WebMode != 0u)
        {
            StepSpiderwebWeave(
                Work.WebLattice,
                Rig.Dynamics.Spiderweb,
                Document.Collider,
                Document.AmbientWindMps,
                Dt,
                ElapsedF);
            Work.ActiveStrandCount = static_cast<uint32_t>(Work.WebLattice.Bonds.size());
            Telemetry.ActiveStrandCount += Work.ActiveStrandCount;
        }

        // 2. Direct full-buffer procedural solvers
        if (Rig.Dynamics.Ferrofluid.FerroMode != 0u)
        {
            Work.LatestMagnetMeters = EvaluateFerroMagnetPosition(
                Rig.Dynamics.Ferrofluid,
                Rig.Dynamics.EffectorGrid.PrimaryEffector.CentreMeters,
                Origin,
                ElapsedF);
            StepFerrofluidSpikes(Work.Slots, Rig.Dynamics.Ferrofluid, Origin, Work.LatestMagnetMeters, ElapsedF);
        }
        else if (Rig.Dynamics.Fracture.CrackMode != 0u)
        {
            StepFractureFissureSlabs(Work.Slots, Rig.Dynamics.Fracture, Origin, ElapsedF);
        }
        else if (Rig.Dynamics.WaterSplash.SplashMode != 0u)
        {
            Work.LatestRallyPose = EvaluateRallyVehiclePose(Rig.Dynamics.WaterSplash, ElapsedF);
            StepShallowWaterSplash(Work.Slots, Rig.Dynamics.WaterSplash, Dt, ElapsedF);
        }
        else if (Rig.Dynamics.EffectorGrid.Active)
        {
            StepEffectorLattice(Work.Slots, Rig.Dynamics.EffectorGrid, Origin, ElapsedF);
        }
        else if (Rig.Dynamics.Shield.Active)
        {
            StepShieldBarrier(Work.Slots, Rig.Dynamics.Shield, Document.Collider, Origin, ElapsedF);
        }
        else if (Rig.Dynamics.Pyro.FlareSalvoMode != 0u)
        {
            StepFlareSalvoEjection(Work.Slots, Rig.Dynamics.Pyro, Origin, Dt, ElapsedF);
        }
        else if (Rig.Dynamics.Pyro.BeamModeActive > 0.0f)
        {
            const Mathematics::Axis3D Target = (Document.Collider.BallRadiusMeters > 0.01f)
                                                   ? Document.Collider.BallPositionMeters
                                                   : Mathematics::Axis3D{ Origin.X + 4.0f, Origin.Y, Origin.Z + 1.5f };
            StepBeamFilament(Work.Slots, Rig.Dynamics.Pyro, Origin, Target, ElapsedF);
        }
        else
        {
            // Continuous spawn & periodic burst release
            const bool StructuralOnly =
                (Rig.Dynamics.Strands.Mode != StrandDynamicsMode::Off && Rig.Dynamics.Botanical.SeedHeadMode == 0u) ||
                (Rig.Dynamics.Spiderweb.WebMode != 0u);
            const bool PersistentPopulation = Rig.Dynamics.Boids.Active || Rig.Dynamics.Molecular.Active ||
                                              Rig.Dynamics.Curl.Mode != CurlManifoldMode::Off ||
                                              Rig.Dynamics.Transition.Mode != TransitionDerezMode::Off ||
                                              Rig.Dynamics.Botanical.SeedHeadMode != 0u ||
                                              StructuralOnly;
            if (!PersistentPopulation && !Work.Slots.empty())
            {
                Work.ReleaseAccumulator += Rig.Sizing.ReleaseRatePerSec * Dt;
                uint32_t ToSpawn = static_cast<uint32_t>(std::floor(Work.ReleaseAccumulator));
                Work.ReleaseAccumulator -= static_cast<float>(ToSpawn);

                if (Rig.Dynamics.Ballistics.BurstIntervalSec > 0.0f && Rig.Dynamics.Ballistics.BurstCount > 0u)
                {
                    Work.BurstCooldownSec -= Dt;
                    if (Work.BurstCooldownSec <= 0.0f)
                    {
                        ToSpawn += Rig.Dynamics.Ballistics.BurstCount;
                        Work.BurstCooldownSec = Rig.Dynamics.Ballistics.BurstIntervalSec;
                    }
                }

                ToSpawn = std::min<uint32_t>(ToSpawn, static_cast<uint32_t>(Work.Slots.size()));
                for (uint32_t K = 0u; K < ToSpawn; ++K)
                {
                    const uint32_t Idx = Work.SpawnCursor % static_cast<uint32_t>(Work.Slots.size());
                    ++Work.SpawnCursor;
                    SpawnSingleSlot(Rig, Idx, Document.RandomSeed, Work.Slots[Idx]);
                }
            }

            // Collective solvers
            if (Rig.Dynamics.Boids.Active)
            {
                StepFlockBoids(Work.Slots, Rig.Dynamics.Boids, Document.Collider, Origin, Dt, ElapsedF);
            }
            else if (Rig.Dynamics.Molecular.Active)
            {
                const bool Reactions = (Rig.Dynamics.Family == ParticleSimulationFamily::ChemicalReaction);
                StepMolecularKinetics(Work.Slots, Rig.Dynamics.Molecular, Origin, Reactions, Dt);
            }
            else if (Rig.Dynamics.Transition.Mode != TransitionDerezMode::Off)
            {
                StepTransitionDerez(Work.Slots, Rig.Dynamics.Transition, Origin, Dt, ElapsedF);
            }
            else
            {
                // Per-particle advection: CurlManifold, Firework star, Seed detachment, or Ballistics + ForceFields + Colliders
                for (uint32_t I = 0u; I < static_cast<uint32_t>(Work.Slots.size()); ++I)
                {
                    ParticleSlot& Slot = Work.Slots[I];
                    if (Slot.PositionAge[3] >= Slot.VelocityLifetime[3])
                    {
                        continue;
                    }

                    if (Rig.Dynamics.Botanical.SeedHeadMode != 0u)
                    {
                        EvaluateBotanicalSeedDetachment(Slot, Rig.Dynamics.Botanical, Document.AmbientWindMps, Dt, ElapsedF);
                    }
                    else if (Rig.Dynamics.Pyro.FireworkMode != 0u)
                    {
                        StepPyroFireworkStar(Slot, Rig.Dynamics.Pyro, Dt, ElapsedF);
                    }
                    else if (Rig.Dynamics.Curl.Mode != CurlManifoldMode::Off)
                    {
                        StepCurlManifoldParticle(Slot, Rig.Dynamics.Curl, Origin, Dt, ElapsedF);
                    }
                    else
                    {
                        StepBallisticParticle(Slot, Rig.Dynamics.Ballistics, Rig.Dynamics.LocalAttractor, Origin, Dt, ElapsedF, I);
                    }

                    ApplyForceFieldCoupling(
                        Slot,
                        Rig.Dynamics.ForceLink,
                        &ForceAccumulator,
                        Origin,
                        Document.AmbientWindMps,
                        Dt,
                        ElapsedSeconds);

                    ResolveParticleCollisions(
                        Slot,
                        Document.Collider,
                        Rig.Dynamics.Ballistics.BounceRestitution,
                        ElapsedF,
                        OptionalSdf);
                }
            }
        }

        // 3. Evaluate ParticleTintDriver & size interpolation + telemetry bounds across alive slots
        uint32_t RigAlive = 0u;
        for (uint32_t I = 0u; I < static_cast<uint32_t>(Work.Slots.size()); ++I)
        {
            ParticleSlot& Slot = Work.Slots[I];
            if (Slot.PositionAge[3] >= Slot.VelocityLifetime[3])
            {
                continue;
            }
            ++RigAlive;

            const float LifeNorm = std::clamp(Slot.PositionAge[3] / std::max(1.0e-4f, Slot.VelocityLifetime[3]), 0.0f, 1.0f);
            if (!HasStandaloneDirectSolver(Rig.Dynamics))
            {
                Slot.Attributes[0] = Rig.Sizing.SizeStartMeters + (Rig.Sizing.SizeEndMeters - Rig.Sizing.SizeStartMeters) * LifeNorm;
            }

            if (Rig.Dynamics.Pyro.FireworkMode == 0u)
            {
                ParticleChannelSampleInput TintIn{};
                TintIn.ModeDefaultCoordinate = (Slot.PhaseTiming[0] > 0.0f) ? Slot.PhaseTiming[0] : LifeNorm;
                TintIn.PositionX             = Slot.PositionAge[0];
                TintIn.PositionY             = Slot.PositionAge[1];
                TintIn.PositionZ             = Slot.PositionAge[2];
                TintIn.OriginX               = Origin.X;
                TintIn.OriginY               = Origin.Y;
                TintIn.OriginZ               = Origin.Z;
                TintIn.VelocityX             = Slot.VelocityLifetime[0];
                TintIn.VelocityY             = Slot.VelocityLifetime[1];
                TintIn.VelocityZ             = Slot.VelocityLifetime[2];
                TintIn.CameraX               = Document.ViewportCamera.EyePositionMeters.X;
                TintIn.CameraY               = Document.ViewportCamera.EyePositionMeters.Y;
                TintIn.CameraZ               = Document.ViewportCamera.EyePositionMeters.Z;
                TintIn.AgeSeconds            = Slot.PositionAge[3];
                TintIn.LifespanSeconds       = Slot.VelocityLifetime[3];
                TintIn.SizeMeters            = Slot.Attributes[0];
                TintIn.OrientationRadians    = Slot.Attributes[1];
                TintIn.RandomSeed            = Slot.Attributes[3];
                TintIn.ElapsedSeconds        = ElapsedF;
                TintIn.NoiseFrequency        = Rig.Sizing.TurbulenceFreq;

                const ColourSpace::LinearRgba Tint = EvaluateParticleTint(
                    Rig.TintBinding.SourceProfile,
                    Rig.TintBinding.ScalarChannel,
                    TintIn);
                Slot.ColourRgba[0] = Tint.Red;
                Slot.ColourRgba[1] = Tint.Green;
                Slot.ColourRgba[2] = Tint.Blue;
                Slot.ColourRgba[3] = Tint.Alpha * Rig.Sizing.OpacityPeak;
            }

            MinX = std::min(MinX, Slot.PositionAge[0]);
            MinY = std::min(MinY, Slot.PositionAge[1]);
            MinZ = std::min(MinZ, Slot.PositionAge[2]);
            MaxX = std::max(MaxX, Slot.PositionAge[0]);
            MaxY = std::max(MaxY, Slot.PositionAge[1]);
            MaxZ = std::max(MaxZ, Slot.PositionAge[2]);
        }

        Work.AliveCount = RigAlive;
        Telemetry.AliveCount += RigAlive;
    }

    if (Telemetry.AliveCount > 0u)
    {
        auto EncodeMm = [](float Meters) noexcept -> uint32_t
        {
            const double Mm = std::clamp(static_cast<double>(Meters) * 1000.0 + 1.0e6, 0.0, 2.0e9);
            return static_cast<uint32_t>(Mm);
        };
        Telemetry.BoundsMinFixedX = EncodeMm(MinX);
        Telemetry.BoundsMinFixedY = EncodeMm(MinY);
        Telemetry.BoundsMinFixedZ = EncodeMm(MinZ);
        Telemetry.BoundsMaxFixedX = EncodeMm(MaxX);
        Telemetry.BoundsMaxFixedY = EncodeMm(MaxY);
        Telemetry.BoundsMaxFixedZ = EncodeMm(MaxZ);
    }
}

const RigSimulationWorkspace* ParticleSoftwareExecutor::FindWorkspace(uint32_t RigId) const noexcept
{
    for (const RigSimulationWorkspace& W : Workspaces)
    {
        if (W.RigId == RigId)
        {
            return &W;
        }
    }
    return nullptr;
}

std::span<const RigSimulationWorkspace> ParticleSoftwareExecutor::QueryWorkspaces() const noexcept
{
    return Workspaces;
}

ParticleTelemetryCounters ParticleSoftwareExecutor::QueryTelemetry() const noexcept
{
    return Telemetry;
}

double ParticleSoftwareExecutor::QueryElapsedSeconds() const noexcept
{
    return ElapsedSeconds;
}

uint64_t ParticleSoftwareExecutor::QueryStepIndex() const noexcept
{
    return StepIndex;
}

const SimulationSpace::ForceFields::ForceFieldAccumulator& ParticleSoftwareExecutor::QueryForceAccumulator() const noexcept
{
    return ForceAccumulator;
}

} // namespace Frontier::ParticleSpace
