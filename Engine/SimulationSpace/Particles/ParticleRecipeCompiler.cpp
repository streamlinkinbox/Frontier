/*============================================================================================================================================
                                                       PARTICLERECIPECOMPILER.CPP
============================================================================================================================================*/
// 🧩 Compiles all 93 non-quarantined presets and live ParticleEffectRecordEntry configurations into modular C++20 ParticleSimulations

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Particles/ParticleRecipeCompiler.h"

#include <algorithm>
#include <cmath>

namespace Frontier::SimulationSpace::Particles
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

[[nodiscard]] std::array<float, 4u> ConvertSrgbToLinearRgba(ColorRgba Color) noexcept
{
    return {{
        static_cast<float>(Color.Red) / 255.0f,
        static_cast<float>(Color.Green) / 255.0f,
        static_cast<float>(Color.Blue) / 255.0f,
        static_cast<float>(Color.Alpha) / 255.0f,
    }};
}

[[nodiscard]] VolumeShapeCategory ConvertReleaseVolume(Workspaces::ParticleReleaseVolume Volume) noexcept
{
    switch (Volume)
    {
        case Workspaces::ParticleReleaseVolume::PointOrigin:   return VolumeShapeCategory::PointOrigin;
        case Workspaces::ParticleReleaseVolume::SphereVolume:  return VolumeShapeCategory::SphereVolume;
        case Workspaces::ParticleReleaseVolume::DiscPlane:     return VolumeShapeCategory::DiscVolume;
        case Workspaces::ParticleReleaseVolume::BoxVolume:     return VolumeShapeCategory::BoxVolume;
        case Workspaces::ParticleReleaseVolume::SurfaceSample: return VolumeShapeCategory::BoxSurfaceLattice;
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
        case 3u: return StrandLayoutPattern::ClothFlagGrid;
        case 4u:
        case 5u:
        case 6u:
        case 8u:
        case 9u: return StrandLayoutPattern::UpwardFloraField;
        case 7u: return StrandLayoutPattern::CanopyTree;
        default: return StrandLayoutPattern::ClothFlagGrid;
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  PARTICLE RECIPE COMPILER
//------------------------------------------------------------------------------------------------------------------------

std::optional<ParticleSimulation> ParticleRecipeCompiler::ConstructFromPresetSlug(
    std::string_view PresetSlug,
    uint32_t         DeterministicSeed)
{
    if (Workspaces::ParticlePresetRegistry::VerifySlugExcludedFromRegistry(PresetSlug))
    {
        return std::nullopt;
    }

    const Workspaces::ParticlePresetDescriptor* Preset =
        Workspaces::ParticlePresetRegistry::FindPresetByIdentifier(PresetSlug);
    if (Preset == nullptr)
    {
        return std::nullopt;
    }

    Workspaces::ParticleEffectRecordEntry Entry{};
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
    Entry.FireworkPattern         = static_cast<Workspaces::FireworkPatternCategory>(
        std::min<uint8_t>(Preset->FireworkPattern, static_cast<uint8_t>(Workspaces::FireworkPatternCategory::GrandFinale)));
    Entry.DeterministicSeed       = DeterministicSeed;

    return ConstructFromRecordEntry(Entry);
}

std::optional<ParticleSimulation> ParticleRecipeCompiler::ConstructFromRecordEntry(
    const Workspaces::ParticleEffectRecordEntry& Effect)
{
    if (!Effect.PresetIdentifier.empty()
        && Workspaces::ParticlePresetRegistry::VerifySlugExcludedFromRegistry(Effect.PresetIdentifier))
    {
        return std::nullopt;
    }

    const uint32_t SafeCapacity = std::clamp(Effect.Capacity, 32u, 65536u);
    const uint32_t Seed         = (Effect.DeterministicSeed != 0u) ? Effect.DeterministicSeed : 1337u;

    ColorAndSizeCurveParameters CurveParams{};
    CurveParams.ColorA          = ConvertSrgbToLinearRgba(Effect.ColorA);
    CurveParams.ColorC          = ConvertSrgbToLinearRgba(Effect.ColorC);
    CurveParams.ColorB          = ConvertSrgbToLinearRgba(Effect.ColorB);
    CurveParams.SizeStartMeters = std::max(0.005f, Effect.SizeStartMeters);
    CurveParams.SizeEndMeters   = std::max(0.002f, Effect.SizeEndMeters);
    CurveParams.SizeMultiplier  = std::max(0.1f, Effect.SizeMultiplier);

    switch (Effect.AuthoringDomain)
    {
        case Workspaces::ParticleAuthoringDomain::RiggingAndFlora:
        {
            StrandGridParameters Grid{};
            Grid.Pattern            = SelectStrandPattern(Effect.RiggingArchitecture, Effect.PresetIdentifier);
            Grid.OriginMeters       = Effect.OriginMeters;
            Grid.StrandCount        = std::clamp(Effect.StrandRows, 4u, 64u);
            Grid.SegmentsPerStrand  = std::clamp(Effect.CurveSegments, 4u, 32u);
            Grid.WidthMeters        = std::max(1.25f, Effect.RadiusMeters * 4.0f);
            Grid.StrandLengthMeters = std::clamp(Effect.StrandHeightMeters, 0.4f, 6.0f);
            Grid.CatenarySagMeters  = Effect.CatenarySagMeters;

            if (Grid.Pattern == StrandLayoutPattern::ClothFlagGrid)
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

            return ParticleSimulationBuilder(Effect.Title)
                .ConfigureCapacity(Needed, ChannelStoragePolicy::FixedSlotOrder)
                .WithDeterministicSeed(Seed)
                .WithReleaseRule<StrandGridReleaseRule>(Grid)
                .WithMotionLaw<GravityAndBuoyancyLaw>(GravityAndBuoyancyParameters{
                    .GravityScale     = Effect.GravityMultiplier * 0.45f,
                    .BuoyancyAccelMs2 = 0.0f,
                    .DragCoefficient  = std::max(0.15f, Effect.DragCoefficient),
                })
                .WithMotionLaw<ForceFieldCouplingLaw>(ForceFieldCouplingParameters{ .CouplingGain = Effect.ForceCouplingGain })
                .WithMotionLaw<XpbdStrandSolverLaw>(Xpbd)
                .WithCollider<DynamicSphereColliderRule>(1.0f)
                .WithCollider<GroundPlaneCollider>(GroundPlaneParameters{ .FloorZMeters = 0.0f, .Restitution = 0.15f })
                .WithIntegrator<XpbdVerletIntegrator>()
                .Construct();
        }

        case Workspaces::ParticleAuthoringDomain::FerrofluidFlux:
        {
            FerrofluidParameters Ferro{};
            Ferro.OriginMeters        = Effect.OriginMeters;
            Ferro.VesselForm          = (Effect.FerrofluidVessel == 2u) ? 2u : 1u;
            Ferro.SpikeCount          = std::clamp(Effect.FerroSpikeCount, 32u, SafeCapacity);
            Ferro.OrbitSpeedRadSec    = Effect.MagnetOrbitSpeedRadS;
            Ferro.MaxCuspHeightMeters = std::clamp(Effect.MaxSpikeHeightMeters, 0.15f, 2.2f);
            Ferro.ManualMagnetPole    = Effect.ManualMagnetPole;

            return ParticleSimulationBuilder(Effect.Title)
                .ConfigureCapacity(std::max(SafeCapacity, Ferro.SpikeCount), ChannelStoragePolicy::FixedSlotOrder)
                .WithDeterministicSeed(Seed)
                .WithReleaseRule<FerrofluidSurfaceReleaseRule>(Ferro)
                .WithMotionLaw<RosensweigFerrofluidLaw>(Ferro)
                .WithCollider<DynamicSphereColliderRule>(0.65f)
                .WithIntegrator<MidpointRk2Integrator>()
                .Construct();
        }

        case Workspaces::ParticleAuthoringDomain::FireworksSpectacle:
        {
            PyroVolleyParameters Pyro{};
            Pyro.LaunchOriginMeters = { Effect.OriginMeters.X, Effect.OriginMeters.Y, 0.25f };
            Pyro.PatternCode        = static_cast<uint32_t>(Effect.FireworkPattern);
            Pyro.SkyText            = Effect.FireworkSkyText.empty() ? "2026" : Effect.FireworkSkyText;
            Pyro.StarsPerBurst      = std::clamp(SafeCapacity / 8u, 36u, 140u);

            return ParticleSimulationBuilder(Effect.Title)
                .ConfigureCapacity(SafeCapacity, ChannelStoragePolicy::DenseSwapRemove)
                .WithDeterministicSeed(Seed)
                .WithReleaseRule<PyroVolleyReleaseRule>(Pyro)
                .WithMotionLaw<GravityAndBuoyancyLaw>(GravityAndBuoyancyParameters{
                    .GravityScale     = std::max(0.25f, Effect.GravityMultiplier),
                    .BuoyancyAccelMs2 = 0.0f,
                    .DragCoefficient  = 0.22f,
                })
                .WithMotionLaw<ForceFieldCouplingLaw>(ForceFieldCouplingParameters{ .CouplingGain = Effect.ForceCouplingGain * 0.45f })
                .WithMotionLaw<PyroVolleyBurstLaw>(Pyro)
                .WithCollider<GroundPlaneCollider>(GroundPlaneParameters{ .FloorZMeters = 0.0f, .Restitution = 0.2f })
                .WithIntegrator<SemiImplicitEulerIntegrator>()
                .Construct();
        }

        case Workspaces::ParticleAuthoringDomain::BoidsFlock:
        {
            BoidsFlockParameters Flock{};
            Flock.HomeOriginMeters = { Effect.OriginMeters.X, Effect.OriginMeters.Y, std::max(1.8f, Effect.OriginMeters.Z) };
            Flock.PerceptionMeters = std::clamp(Effect.BoidsPerceptionMeters, 0.4f, 4.5f);
            Flock.CruiseSpeedMs    = std::clamp(Effect.BoidsCruiseSpeedMs, 0.8f, 9.0f);
            Flock.CreatureVariant  = Effect.BoidsCreatureVariant;
            Flock.FlockCount       = std::clamp(SafeCapacity, 24u, 360u);

            return ParticleSimulationBuilder(Effect.Title)
                .ConfigureCapacity(Flock.FlockCount, ChannelStoragePolicy::FixedSlotOrder)
                .WithDeterministicSeed(Seed)
                .WithReleaseRule<BoidsFlockReleaseRule>(Flock)
                .WithMotionLaw<BoidsFlockLaw>(Flock)
                .WithMotionLaw<ForceFieldCouplingLaw>(ForceFieldCouplingParameters{ .CouplingGain = Effect.ForceCouplingGain * 0.35f })
                .WithCollider<GroundPlaneCollider>(GroundPlaneParameters{ .FloorZMeters = 0.25f, .Restitution = 0.4f })
                .WithIntegrator<SemiImplicitEulerIntegrator>()
                .Construct();
        }

        case Workspaces::ParticleAuthoringDomain::FractureAndCracks:
        {
            FractureCrackParameters Frac{};
            Frac.OriginMeters        = { Effect.OriginMeters.X, Effect.OriginMeters.Y, 0.12f };
            Frac.IncisionDepthMeters = std::clamp(Effect.FractureDepthMeters, 0.05f, 1.5f);
            Frac.BranchDensity       = std::clamp(Effect.CrackBranchDensity, 0.3f, 3.0f);
            Frac.Pattern             = (Effect.FracturePattern == 2u) ? 2u : 1u;
            Frac.CellCount           = std::clamp(SafeCapacity, 64u, 512u);

            return ParticleSimulationBuilder(Effect.Title)
                .ConfigureCapacity(Frac.CellCount, ChannelStoragePolicy::FixedSlotOrder)
                .WithDeterministicSeed(Seed)
                .WithReleaseRule<FractureSurfaceReleaseRule>(Frac)
                .WithMotionLaw<FractureCrackLaw>(Frac)
                .WithMotionLaw<GravityAndBuoyancyLaw>(GravityAndBuoyancyParameters{ .GravityScale = 1.0f, .DragCoefficient = 0.18f })
                .WithCollider<GroundPlaneCollider>(GroundPlaneParameters{ .FloorZMeters = 0.0f, .Restitution = 0.3f })
                .WithIntegrator<SemiImplicitEulerIntegrator>()
                .Construct();
        }

        case Workspaces::ParticleAuthoringDomain::MoGraphEffector:
        case Workspaces::ParticleAuthoringDomain::TransitionDerez:
        {
            const uint32_t Side = std::clamp(static_cast<uint32_t>(std::sqrt(static_cast<double>(SafeCapacity))), 6u, 28u);
            MoGraphEffectorParameters Grid{};
            Grid.OriginMeters      = Effect.OriginMeters;
            Grid.GridX             = Side;
            Grid.GridY             = Side;
            Grid.FalloffMeters     = std::clamp(Effect.EffectorFalloffMeters, 0.5f, 6.0f);
            Grid.ExtrusionMeters   = std::clamp(Effect.EffectorExtrusionMeters, 0.2f, 4.0f);
            Grid.EnableDerezLaunch = (Effect.AuthoringDomain == Workspaces::ParticleAuthoringDomain::TransitionDerez);

            return ParticleSimulationBuilder(Effect.Title)
                .ConfigureCapacity(Side * Side, ChannelStoragePolicy::FixedSlotOrder)
                .WithDeterministicSeed(Seed)
                .WithReleaseRule<LatticeGridReleaseRule>(Grid)
                .WithMotionLaw<MoGraphEffectorLaw>(Grid)
                .WithMotionLaw<ForceFieldCouplingLaw>(ForceFieldCouplingParameters{ .CouplingGain = Effect.ForceCouplingGain * 0.4f })
                .WithCollider<DynamicSphereColliderRule>(0.85f)
                .WithIntegrator<SemiImplicitEulerIntegrator>()
                .Construct();
        }

        case Workspaces::ParticleAuthoringDomain::EnergyShield:
        {
            EnergyShieldParameters Shield{};
            Shield.CentreMeters    = Effect.OriginMeters;
            Shield.RippleSpeedHz   = std::clamp(Effect.ShieldRippleSpeed, 0.5f, 8.0f);
            Shield.GeometryVariant = Effect.ShieldGeometry;
            Shield.CellCount       = std::clamp(SafeCapacity, 64u, 420u);

            return ParticleSimulationBuilder(Effect.Title)
                .ConfigureCapacity(Shield.CellCount, ChannelStoragePolicy::FixedSlotOrder)
                .WithDeterministicSeed(Seed)
                .WithReleaseRule<ShieldBarrierReleaseRule>(Shield)
                .WithMotionLaw<EnergyShieldRippleLaw>(Shield)
                .WithIntegrator<SemiImplicitEulerIntegrator>()
                .Construct();
        }

        case Workspaces::ParticleAuthoringDomain::SpaceDistortion:
        {
            VolumeReleaseParameters Rel{};
            Rel.Shape             = VolumeShapeCategory::DiscVolume;
            Rel.OriginMeters      = { Effect.OriginMeters.X, Effect.OriginMeters.Y, 1.6f };
            Rel.RadiusMeters      = 1.85f;
            Rel.BirthRatePerSec   = std::max(60.0f, Effect.BirthRatePerSec);
            Rel.InitialBurstCount = std::min(SafeCapacity / 2u, 220u);
            Rel.SpeedMinMs        = 1.2f;
            Rel.SpeedMaxMs        = 2.6f;
            Rel.LifeMinSec        = 2.5f;
            Rel.LifeMaxSec        = 4.5f;

            const bool IsHeatShimmer = (Effect.PresetIdentifier.find("heat") != std::string::npos
                                        || Effect.PresetIdentifier.find("shimmer") != std::string::npos);

            SpaceRefractionParameters Space{};
            Space.SingularityMeters = Rel.OriginMeters;
            Space.OrbitalStrength   = IsHeatShimmer ? 0.0f : 7.5f;
            Space.ThermalRiseMs     = IsHeatShimmer ? 2.4f : 0.0f;

            return ParticleSimulationBuilder(Effect.Title)
                .ConfigureCapacity(SafeCapacity, ChannelStoragePolicy::DenseSwapRemove)
                .WithDeterministicSeed(Seed)
                .WithReleaseRule<VolumeReleaseRule>(Rel)
                .WithMotionLaw<SpaceRefractionLaw>(Space)
                .WithMotionLaw<ForceFieldCouplingLaw>(ForceFieldCouplingParameters{ .CouplingGain = Effect.ForceCouplingGain * 0.45f })
                .WithMotionLaw<ColorAndSizeCurveLaw>(CurveParams)
                .WithIntegrator<MidpointRk2Integrator>()
                .Construct();
        }

        case Workspaces::ParticleAuthoringDomain::GeneralParticle:
        default:
        {
            VolumeReleaseParameters Rel{};
            Rel.Shape               = ConvertReleaseVolume(Effect.ReleaseVolume);
            Rel.OriginMeters        = Effect.OriginMeters;
            Rel.LaunchDirection     = Effect.LaunchDirection;
            Rel.RadiusMeters        = std::max(0.05f, Effect.RadiusMeters);
            Rel.ConeSpreadRad       = Effect.ConeSpreadRad;
            Rel.BirthRatePerSec     = (Effect.BirthRatePerSec > 0.1f) ? Effect.BirthRatePerSec : 85.0f;
            Rel.InitialBurstCount   = std::min(SafeCapacity / 3u, std::max(32u, Effect.BurstCount));
            Rel.BurstCount          = std::max(16u, Effect.BurstCount);
            Rel.BurstIntervalSec    = Effect.BurstIntervalSec;
            Rel.SpeedMinMs          = std::max(0.1f, Effect.SpeedMinMs);
            Rel.SpeedMaxMs          = std::max(Rel.SpeedMinMs, Effect.SpeedMaxMs);
            Rel.LifeMinSec          = std::max(0.25f, Effect.LifeMinSec);
            Rel.LifeMaxSec          = std::max(Rel.LifeMinSec, Effect.LifeMaxSec);
            Rel.SizeStartMeters     = std::max(0.01f, Effect.SizeStartMeters);
            Rel.SpinRateRadSec      = Effect.SpinRateRadSec;
            Rel.AllocateOrientation = (Effect.Silhouette == Workspaces::ParticleVisualSilhouette::ChipCube
                                       || Effect.Silhouette == Workspaces::ParticleVisualSilhouette::GlassShard
                                       || Effect.Silhouette == Workspaces::ParticleVisualSilhouette::LeafOrPaper
                                       || Effect.Silhouette == Workspaces::ParticleVisualSilhouette::CoinDisc);

            ParticleSimulationBuilder Builder(Effect.Title);
            Builder
                .ConfigureCapacity(SafeCapacity, ChannelStoragePolicy::DenseSwapRemove)
                .WithDeterministicSeed(Seed)
                .WithReleaseRule<VolumeReleaseRule>(Rel)
                .WithMotionLaw<GravityAndBuoyancyLaw>(GravityAndBuoyancyParameters{
                    .GravityScale     = Effect.GravityMultiplier,
                    .BuoyancyAccelMs2 = (Effect.GravityMultiplier < 0.0f) ? (-Effect.GravityMultiplier * 4.5f) : 0.0f,
                    .DragCoefficient  = std::max(0.02f, Effect.DragCoefficient),
                })
                .WithMotionLaw<ForceFieldCouplingLaw>(ForceFieldCouplingParameters{ .CouplingGain = Effect.ForceCouplingGain })
                .WithMotionLaw<LocalCurlTurbulenceLaw>(LocalCurlTurbulenceParameters{
                    .AmplitudeMs   = Effect.CurlAmplitude,
                    .FrequencyPerM = Effect.CurlFrequency,
                })
                .WithMotionLaw<FlutterAndTumbleLaw>(FlutterAndTumbleParameters{
                    .FlutterAmplitudeMs2 = (Effect.Category == Workspaces::ParticleEffectCategory::WindBlown) ? 1.45f : 0.22f,
                })
                .WithMotionLaw<ColorAndSizeCurveLaw>(CurveParams)
                .WithCollider<GroundPlaneCollider>(GroundPlaneParameters{
                    .FloorZMeters = 0.0f,
                    .Restitution  = std::clamp(Effect.FloorRestitution, 0.0f, 0.95f),
                })
                .WithCollider<SignedDistanceVolumeCollider>()
                .WithIntegrator<SemiImplicitEulerIntegrator>();

            if (Effect.BallInteractionEnabled)
            {
                Builder.WithCollider<DynamicSphereColliderRule>(1.0f);
            }
            return Builder.Construct();
        }
    }
}

} // namespace Frontier::SimulationSpace::Particles
