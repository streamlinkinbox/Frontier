/*============================================================================================================================================
                                                       PARTICLEPRESETCOMPILER.CPP
============================================================================================================================================*/
// 🧩 Compiles all 93 non-quarantined presets and live ParticleEffectDescriptor configurations into modular C++20 ParticleSimulations

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Particle/ParticlePresetCompiler.h"

#include <algorithm>
#include <cmath>

namespace Frontier::Workspaces
{

using namespace Frontier::SimulationSpace::Particles;

//------------------------------------------------------------------------------------------------------------------------
//                                                     INTERNAL ROUTINES
//------------------------------------------------------------------------------------------------------------------------

namespace
{

[[nodiscard]] std::array<float, 4u> DecodeLinearRgba(ParticleRgba8 Color) noexcept
{
    return {{
        static_cast<float>(Color.Red) / 255.0f,
        static_cast<float>(Color.Green) / 255.0f,
        static_cast<float>(Color.Blue) / 255.0f,
        static_cast<float>(Color.Alpha) / 255.0f,
    }};
}

[[nodiscard]] VolumeShapeCategory ResolveReleaseVolume(ParticleReleaseVolume Volume) noexcept
{
    switch (Volume)
    {
        case ParticleReleaseVolume::PointOrigin:   return VolumeShapeCategory::PointOrigin;
        case ParticleReleaseVolume::SphereVolume:  return VolumeShapeCategory::SphereVolume;
        case ParticleReleaseVolume::DiscPlane:     return VolumeShapeCategory::DiscVolume;
        case ParticleReleaseVolume::BoxVolume:     return VolumeShapeCategory::BoxVolume;
        case ParticleReleaseVolume::SurfaceSample: return VolumeShapeCategory::BoxSurfaceLattice;
        default:                                               return VolumeShapeCategory::SphereVolume;
    }
}

[[nodiscard]] StrandLayoutPattern SelectStrandPattern(uint8_t Architecture, std::string_view Slug) noexcept
{
    if (Slug.find("web") != std::string_view::npos)
    {
        return StrandLayoutPattern::RadialWeb;
    }
    switch (Architecture)
    {
        case 1u:
        case 2u: return StrandLayoutPattern::HangingCurtain;
        case 3u: return StrandLayoutPattern::ClothBannerGrid;
        case 4u:
        case 5u:
        case 6u:
        case 8u:
        case 9u: return StrandLayoutPattern::UpwardFloraField;
        case 7u: return StrandLayoutPattern::CanopyTree;
        default: return StrandLayoutPattern::ClothBannerGrid;
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  PARTICLE PRESET COMPILER
//------------------------------------------------------------------------------------------------------------------------

std::optional<ParticleSimulation> ParticlePresetCompiler::CompilePresetSlug(
    std::string_view PresetSlug,
    uint32_t         DeterministicSeed)
{
    if (ParticlePresetRegistry::VerifySlugRetired(PresetSlug))
    {
        return std::nullopt;
    }

    const ParticlePresetDescriptor* Preset =
        ParticlePresetRegistry::FindPreset(PresetSlug);
    if (Preset == nullptr)
    {
        return std::nullopt;
    }

    ParticleEffectDescriptor Entry{};
    Entry.PresetIdentifier        = Preset->Identifier;
    Entry.Title                   = Preset->DisplayName;
    Entry.Sublabel                = Preset->Sublabel;
    Entry.Category                = Preset->Category;
    Entry.AuthoringDomain         = Preset->AuthoringDomain;
    Entry.Visible                 = true;
    Entry.ReleaseVolume           = Preset->ReleaseVolume;
    Entry.RadiusMeters            = Preset->RadiusMeters;
    Entry.Capacity                = std::max(32u, Preset->Capacity);
    Entry.BirthRatePerSec         = Preset->BirthRatePerSec;
    Entry.SpeedMinMs              = Preset->SpeedMinMs;
    Entry.SpeedMaxMs              = Preset->SpeedMaxMs;
    Entry.LifeMinSec              = Preset->LifeMinSec;
    Entry.LifeMaxSec              = Preset->LifeMaxSec;
    Entry.SizeStartMeters         = Preset->SizeStartMeters;
    Entry.SizeEndMeters           = Preset->SizeEndMeters;
    Entry.Silhouette              = Preset->Silhouette;
    Entry.BlendCategory           = Preset->BlendCategory;
    Entry.ColorA                  = Preset->ColorA;
    Entry.ColorB                  = Preset->ColorB;
    Entry.ColorC                  = Preset->ColorC;
    Entry.GravityMultiplier       = Preset->GravityMultiplier;
    Entry.DragCoefficient         = Preset->DragCoefficient;
    Entry.ForceCouplingGain       = 1.0f;
    Entry.CurlAmplitude           = 0.45f;
    Entry.CurlFrequency           = 0.85f;
    Entry.FloorRestitution        = 0.35f;
    Entry.RiggingArchitecture     = Preset->RiggingArchitecture;
    Entry.FerrofluidVessel        = Preset->FerrofluidVessel;
    Entry.FracturePattern         = Preset->FracturePattern;
    Entry.EffectorArchitecture    = Preset->EffectorArchitecture;
    Entry.FireworkPattern         = static_cast<FireworkPatternCategory>(
        std::min<uint8_t>(Preset->FireworkPattern, static_cast<uint8_t>(FireworkPatternCategory::GrandFinale)));
    Entry.DeterministicSeed       = DeterministicSeed;

    return CompileEffectDescriptor(Entry);
}

std::optional<ParticleSimulation> ParticlePresetCompiler::CompileEffectDescriptor(
    const ParticleEffectDescriptor& Effect)
{
    if (!Effect.PresetIdentifier.empty()
        && ParticlePresetRegistry::VerifySlugRetired(Effect.PresetIdentifier))
    {
        return std::nullopt;
    }

    const uint32_t SafeCapacity = std::clamp(Effect.Capacity, 32u, 65536u);
    const uint32_t Seed         = (Effect.DeterministicSeed != 0u) ? Effect.DeterministicSeed : 1337u;

    AppearanceCurveParameters CurveSettings{};
    CurveSettings.ColorA          = DecodeLinearRgba(Effect.ColorA);
    CurveSettings.ColorC          = DecodeLinearRgba(Effect.ColorC);
    CurveSettings.ColorB          = DecodeLinearRgba(Effect.ColorB);
    CurveSettings.SizeStartMeters = std::max(0.005f, Effect.SizeStartMeters);
    CurveSettings.SizeEndMeters   = std::max(0.002f, Effect.SizeEndMeters);
    CurveSettings.SizeMultiplier  = std::max(0.1f, Effect.SizeMultiplier);

    switch (Effect.AuthoringDomain)
    {
        case ParticleAuthoringDomain::RiggingAndFlora:
        {
            StrandGridParameters Grid{};
            Grid.Pattern            = SelectStrandPattern(Effect.RiggingArchitecture, Effect.PresetIdentifier);
            Grid.OriginMeters       = Effect.OriginMeters;
            Grid.StrandCount        = std::clamp(Effect.StrandRows, 4u, 64u);
            Grid.SegmentsPerStrand  = std::clamp(Effect.CurveSegments, 4u, 32u);
            Grid.WidthMeters        = std::max(1.25f, Effect.RadiusMeters * 4.0f);
            Grid.StrandLengthMeters = std::clamp(Effect.StrandHeightMeters, 0.4f, 6.0f);
            Grid.CatenarySagMeters  = Effect.CatenarySagMeters;

            if (Grid.Pattern == StrandLayoutPattern::ClothBannerGrid)
            {
                Grid.OriginMeters.X = (Effect.OriginMeters.X == 0.0f) ? -1.15f : Effect.OriginMeters.X;
                Grid.OriginMeters.Z = std::max(Effect.OriginMeters.Z, 1.45f);
            }
            else if (Grid.Pattern == StrandLayoutPattern::HangingCurtain || Grid.Pattern == StrandLayoutPattern::RadialWeb)
            {
                Grid.OriginMeters.Z = std::max(Effect.OriginMeters.Z, Grid.StrandLengthMeters + 0.25f);
            }

            const uint32_t Needed = std::max(SafeCapacity, Grid.StrandCount * Grid.SegmentsPerStrand);

            XpbdStrandSolverParameters Xpbd{};
            Xpbd.ElasticStiffness = Effect.ElasticStiffness;
            Xpbd.WaveSpeedHz      = Effect.WaveSpeedHz;
            Xpbd.TearSpeedMs      = Effect.TearSpeedMs;
            Xpbd.SubIterations    = 5u;

            return ParticleSimulationSpecification(Effect.Title)
                .ConfigureCapacity(Needed, ChannelStoragePolicy::FixedSlotOrder)
                .AssignDeterministicSeed(Seed)
                .AttachSource<StrandGridSource>(Grid)
                .AttachMotionSolver<GravityBuoyancySolver>(GravityBuoyancyParameters{
                    .GravityScale     = Effect.GravityMultiplier * 0.45f,
                    .BuoyancyAccelMs2 = 0.0f,
                    .DragCoefficient  = std::max(0.15f, Effect.DragCoefficient),
                })
                .AttachMotionSolver<ForceFieldCouplingSolver>(ForceFieldCouplingParameters{ .CouplingGain = Effect.ForceCouplingGain })
                .AttachMotionSolver<XpbdStrandSolver>(Xpbd)
                .AttachCollider<SphereContactCollider>(1.0f)
                .AttachCollider<GroundPlaneCollider>(GroundPlaneParameters{ .FloorZMeters = 0.0f, .Restitution = 0.15f })
                .AssignIntegrator<XpbdVerletIntegrator>()
                .Construct();
        }

        case ParticleAuthoringDomain::FerrofluidFlux:
        {
            FerrofluidParameters Ferro{};
            Ferro.OriginMeters        = Effect.OriginMeters;
            Ferro.VesselForm          = (Effect.FerrofluidVessel == 2u) ? 2u : 1u;
            Ferro.SpikeCount          = std::clamp(Effect.FerroSpikeCount, 32u, SafeCapacity);
            Ferro.OrbitSpeedRadSec    = Effect.MagnetOrbitSpeedRadS;
            Ferro.MaxCuspHeightMeters = std::clamp(Effect.MaxSpikeHeightMeters, 0.15f, 2.2f);
            Ferro.ManualMagnetPole    = Effect.ManualMagnetPole;

            return ParticleSimulationSpecification(Effect.Title)
                .ConfigureCapacity(std::max(SafeCapacity, Ferro.SpikeCount), ChannelStoragePolicy::FixedSlotOrder)
                .AssignDeterministicSeed(Seed)
                .AttachSource<FerrofluidSurfaceSource>(Ferro)
                .AttachMotionSolver<RosensweigFerrofluidSolver>(Ferro)
                .AttachCollider<SphereContactCollider>(0.65f)
                .AssignIntegrator<MidpointRk2Integrator>()
                .Construct();
        }

        case ParticleAuthoringDomain::FireworksSpectacle:
        {
            PyroVolleyParameters Pyro{};
            Pyro.LaunchOriginMeters = { Effect.OriginMeters.X, Effect.OriginMeters.Y, 0.25f };
            Pyro.PatternCode        = static_cast<uint32_t>(Effect.FireworkPattern);
            Pyro.SkyText            = Effect.FireworkSkyText.empty() ? "2026" : Effect.FireworkSkyText;
            Pyro.StarsPerBurst      = std::clamp(SafeCapacity / 8u, 36u, 140u);

            return ParticleSimulationSpecification(Effect.Title)
                .ConfigureCapacity(SafeCapacity, ChannelStoragePolicy::DenseSwapRemove)
                .AssignDeterministicSeed(Seed)
                .AttachSource<PyroVolleySource>(Pyro)
                .AttachMotionSolver<GravityBuoyancySolver>(GravityBuoyancyParameters{
                    .GravityScale     = std::max(0.25f, Effect.GravityMultiplier),
                    .BuoyancyAccelMs2 = 0.0f,
                    .DragCoefficient  = 0.22f,
                })
                .AttachMotionSolver<ForceFieldCouplingSolver>(ForceFieldCouplingParameters{ .CouplingGain = Effect.ForceCouplingGain * 0.45f })
                .AttachMotionSolver<PyroVolleyBurstSolver>(Pyro)
                .AttachCollider<GroundPlaneCollider>(GroundPlaneParameters{ .FloorZMeters = 0.0f, .Restitution = 0.2f })
                .AssignIntegrator<SemiImplicitEulerIntegrator>()
                .Construct();
        }

        case ParticleAuthoringDomain::BoidsFlock:
        {
            BoidsFlockParameters Flock{};
            Flock.HomeOriginMeters = { Effect.OriginMeters.X, Effect.OriginMeters.Y, std::max(1.8f, Effect.OriginMeters.Z) };
            Flock.PerceptionMeters = std::clamp(Effect.BoidsPerceptionMeters, 0.4f, 4.5f);
            Flock.CruiseSpeedMs    = std::clamp(Effect.BoidsCruiseSpeedMs, 0.8f, 9.0f);
            Flock.CreatureVariant  = Effect.BoidsCreatureVariant;
            Flock.FlockCount       = std::clamp(SafeCapacity, 24u, 360u);

            return ParticleSimulationSpecification(Effect.Title)
                .ConfigureCapacity(Flock.FlockCount, ChannelStoragePolicy::FixedSlotOrder)
                .AssignDeterministicSeed(Seed)
                .AttachSource<BoidsFlockSource>(Flock)
                .AttachMotionSolver<BoidsFlockSolver>(Flock)
                .AttachMotionSolver<ForceFieldCouplingSolver>(ForceFieldCouplingParameters{ .CouplingGain = Effect.ForceCouplingGain * 0.35f })
                .AttachCollider<GroundPlaneCollider>(GroundPlaneParameters{ .FloorZMeters = 0.25f, .Restitution = 0.4f })
                .AssignIntegrator<SemiImplicitEulerIntegrator>()
                .Construct();
        }

        case ParticleAuthoringDomain::FractureAndCracks:
        {
            FractureCrackParameters Frac{};
            Frac.OriginMeters        = { Effect.OriginMeters.X, Effect.OriginMeters.Y, 0.12f };
            Frac.IncisionDepthMeters = std::clamp(Effect.FractureDepthMeters, 0.05f, 1.5f);
            Frac.BranchDensity       = std::clamp(Effect.CrackBranchDensity, 0.3f, 3.0f);
            Frac.Pattern             = (Effect.FracturePattern == 2u) ? 2u : 1u;
            Frac.CellCount           = std::clamp(SafeCapacity, 64u, 512u);

            return ParticleSimulationSpecification(Effect.Title)
                .ConfigureCapacity(Frac.CellCount, ChannelStoragePolicy::FixedSlotOrder)
                .AssignDeterministicSeed(Seed)
                .AttachSource<FractureSurfaceSource>(Frac)
                .AttachMotionSolver<FractureCrackSolver>(Frac)
                .AttachMotionSolver<GravityBuoyancySolver>(GravityBuoyancyParameters{ .GravityScale = 1.0f, .DragCoefficient = 0.18f })
                .AttachCollider<GroundPlaneCollider>(GroundPlaneParameters{ .FloorZMeters = 0.0f, .Restitution = 0.3f })
                .AssignIntegrator<SemiImplicitEulerIntegrator>()
                .Construct();
        }

        case ParticleAuthoringDomain::MoGraphEffector:
        case ParticleAuthoringDomain::TransitionDerez:
        {
            const uint32_t Side = std::clamp(static_cast<uint32_t>(std::sqrt(static_cast<double>(SafeCapacity))), 6u, 28u);
            MoGraphEffectorParameters Grid{};
            Grid.OriginMeters      = Effect.OriginMeters;
            Grid.GridX             = Side;
            Grid.GridY             = Side;
            Grid.FalloffMeters     = std::clamp(Effect.EffectorFalloffMeters, 0.5f, 6.0f);
            Grid.ExtrusionMeters   = std::clamp(Effect.EffectorExtrusionMeters, 0.2f, 4.0f);
            Grid.EnableDerezLaunch = (Effect.AuthoringDomain == ParticleAuthoringDomain::TransitionDerez);

            return ParticleSimulationSpecification(Effect.Title)
                .ConfigureCapacity(Side * Side, ChannelStoragePolicy::FixedSlotOrder)
                .AssignDeterministicSeed(Seed)
                .AttachSource<LatticeGridSource>(Grid)
                .AttachMotionSolver<MoGraphEffectorSolver>(Grid)
                .AttachMotionSolver<ForceFieldCouplingSolver>(ForceFieldCouplingParameters{ .CouplingGain = Effect.ForceCouplingGain * 0.4f })
                .AttachCollider<SphereContactCollider>(0.85f)
                .AssignIntegrator<SemiImplicitEulerIntegrator>()
                .Construct();
        }

        case ParticleAuthoringDomain::EnergyShield:
        {
            EnergyShieldParameters Shield{};
            Shield.CentreMeters    = Effect.OriginMeters;
            Shield.RippleSpeedHz   = std::clamp(Effect.ShieldRippleSpeed, 0.5f, 8.0f);
            Shield.GeometryVariant = Effect.ShieldGeometry;
            Shield.CellCount       = std::clamp(SafeCapacity, 64u, 420u);

            return ParticleSimulationSpecification(Effect.Title)
                .ConfigureCapacity(Shield.CellCount, ChannelStoragePolicy::FixedSlotOrder)
                .AssignDeterministicSeed(Seed)
                .AttachSource<ShieldBarrierSource>(Shield)
                .AttachMotionSolver<EnergyShieldRippleSolver>(Shield)
                .AssignIntegrator<SemiImplicitEulerIntegrator>()
                .Construct();
        }

        case ParticleAuthoringDomain::SpaceDistortion:
        {
            VolumeSourceParameters Source{};
            Source.Shape             = VolumeShapeCategory::DiscVolume;
            Source.OriginMeters      = { Effect.OriginMeters.X, Effect.OriginMeters.Y, 1.6f };
            Source.RadiusMeters      = 1.85f;
            Source.BirthRatePerSec   = std::max(60.0f, Effect.BirthRatePerSec);
            Source.InitialBurstCount = std::min(SafeCapacity / 2u, 220u);
            Source.SpeedMinMs        = 1.2f;
            Source.SpeedMaxMs        = 2.6f;
            Source.LifeMinSec        = 2.5f;
            Source.LifeMaxSec        = 4.5f;

            const bool HeatShimmerPreset = (Effect.PresetIdentifier.find("heat") != std::string::npos
                                            || Effect.PresetIdentifier.find("shimmer") != std::string::npos);

            SpaceRefractionParameters Space{};
            Space.SingularityMeters = Source.OriginMeters;
            Space.OrbitalStrength   = HeatShimmerPreset ? 0.0f : 7.5f;
            Space.ThermalRiseMs     = HeatShimmerPreset ? 2.4f : 0.0f;

            return ParticleSimulationSpecification(Effect.Title)
                .ConfigureCapacity(SafeCapacity, ChannelStoragePolicy::DenseSwapRemove)
                .AssignDeterministicSeed(Seed)
                .AttachSource<VolumeParticleSource>(Source)
                .AttachMotionSolver<SpaceRefractionSolver>(Space)
                .AttachMotionSolver<ForceFieldCouplingSolver>(ForceFieldCouplingParameters{ .CouplingGain = Effect.ForceCouplingGain * 0.45f })
                .AttachMotionSolver<AppearanceCurveSolver>(CurveSettings)
                .AssignIntegrator<MidpointRk2Integrator>()
                .Construct();
        }

        case ParticleAuthoringDomain::GeneralParticle:
        default:
        {
            VolumeSourceParameters Source{};
            Source.Shape               = ResolveReleaseVolume(Effect.ReleaseVolume);
            Source.OriginMeters        = Effect.OriginMeters;
            Source.LaunchDirection     = Effect.LaunchDirection;
            Source.RadiusMeters        = std::max(0.05f, Effect.RadiusMeters);
            Source.ConeSpreadRad       = Effect.ConeSpreadRad;
            Source.BirthRatePerSec     = (Effect.BirthRatePerSec > 0.1f) ? Effect.BirthRatePerSec : 85.0f;
            Source.InitialBurstCount   = std::min(SafeCapacity / 3u, std::max(32u, Effect.BurstCount));
            Source.BurstCount          = std::max(16u, Effect.BurstCount);
            Source.BurstIntervalSec    = Effect.BurstIntervalSec;
            Source.SpeedMinMs          = std::max(0.1f, Effect.SpeedMinMs);
            Source.SpeedMaxMs          = std::max(Source.SpeedMinMs, Effect.SpeedMaxMs);
            Source.LifeMinSec          = std::max(0.25f, Effect.LifeMinSec);
            Source.LifeMaxSec          = std::max(Source.LifeMinSec, Effect.LifeMaxSec);
            Source.SizeStartMeters     = std::max(0.01f, Effect.SizeStartMeters);
            Source.SpinRateRadSec      = Effect.SpinRateRadSec;
            Source.AllocateOrientation = (Effect.Silhouette == ParticleVisualSilhouette::CubeShard
                                          || Effect.Silhouette == ParticleVisualSilhouette::GlassShard
                                          || Effect.Silhouette == ParticleVisualSilhouette::LeafOrPaper
                                          || Effect.Silhouette == ParticleVisualSilhouette::CoinDisc);

            ParticleSimulationSpecification Specification(Effect.Title);
            Specification
                .ConfigureCapacity(SafeCapacity, ChannelStoragePolicy::DenseSwapRemove)
                .AssignDeterministicSeed(Seed)
                .AttachSource<VolumeParticleSource>(Source)
                .AttachMotionSolver<GravityBuoyancySolver>(GravityBuoyancyParameters{
                    .GravityScale     = Effect.GravityMultiplier,
                    .BuoyancyAccelMs2 = (Effect.GravityMultiplier < 0.0f) ? (-Effect.GravityMultiplier * 4.5f) : 0.0f,
                    .DragCoefficient  = std::max(0.02f, Effect.DragCoefficient),
                })
                .AttachMotionSolver<ForceFieldCouplingSolver>(ForceFieldCouplingParameters{ .CouplingGain = Effect.ForceCouplingGain })
                .AttachMotionSolver<CurlTurbulenceSolver>(CurlTurbulenceParameters{
                    .AmplitudeMs   = Effect.CurlAmplitude,
                    .FrequencyPerM = Effect.CurlFrequency,
                })
                .AttachMotionSolver<AeroFlutterSolver>(AeroFlutterParameters{
                    .FlutterAmplitudeMs2 = (Effect.Category == Workspaces::ParticleEffectCategory::WindBlown) ? 1.45f : 0.22f,
                })
                .AttachMotionSolver<AppearanceCurveSolver>(CurveSettings)
                .AttachCollider<GroundPlaneCollider>(GroundPlaneParameters{
                    .FloorZMeters = 0.0f,
                    .Restitution  = std::clamp(Effect.FloorRestitution, 0.0f, 0.95f),
                })
                .AttachCollider<SignedDistanceVolumeCollider>()
                .AssignIntegrator<SemiImplicitEulerIntegrator>();

            if (Effect.BallInteractionEnabled)
            {
                Specification.AttachCollider<SphereContactCollider>(1.0f);
            }
            return Specification.Construct();
        }
    }
}

} // namespace Frontier::Workspaces
