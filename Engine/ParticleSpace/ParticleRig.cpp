/*============================================================================================================================================
                                                              PARTICLERIG.CPP
============================================================================================================================================*/
// 🧩 ParticleRig cache reconstruction (64-entry ColourRampLut64, SurfaceAnchorSet, FireworkShellSample) & 352B GPU uniform packing

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/ParticleRig.h"

#include "Engine/ColourSpace/ColourRampSampler.h"
#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace Frontier::ParticleSpace
{

namespace
{

[[nodiscard]] Mathematics::Axis3D ExtractOriginAnchorPosition(const ReleaseOriginSpec& Spec) noexcept
{
    return std::visit(
        [](const auto& VariantEntry) noexcept -> Mathematics::Axis3D
        {
            using T = std::decay_t<decltype(VariantEntry)>;
            if constexpr (std::is_same_v<T, DualElectrodeOriginSpec>)
            {
                return {
                    0.5f * (VariantEntry.CathodeMeters.X + VariantEntry.AnodeMeters.X),
                    0.5f * (VariantEntry.CathodeMeters.Y + VariantEntry.AnodeMeters.Y),
                    0.5f * (VariantEntry.CathodeMeters.Z + VariantEntry.AnodeMeters.Z),
                };
            }
            else
            {
                return VariantEntry.OriginMeters;
            }
        },
        Spec);
}

[[nodiscard]] float ExtractAnalyticOriginRadius(const ReleaseOriginSpec& Spec) noexcept
{
    return std::visit(
        [](const auto& VariantEntry) noexcept -> float
        {
            using T = std::decay_t<decltype(VariantEntry)>;
            if constexpr (std::is_same_v<T, SphereVolumeOriginSpec> || std::is_same_v<T, DiscOriginSpec> ||
                          std::is_same_v<T, SpiderwebOriginSpec>)
            {
                return VariantEntry.RadiusMeters;
            }
            else if constexpr (std::is_same_v<T, SurfaceLatticeOriginSpec>)
            {
                return VariantEntry.ExtentMeters;
            }
            else if constexpr (std::is_same_v<T, SurfaceAnchorOriginSpec>)
            {
                return VariantEntry.ScaleMeters;
            }
            else if constexpr (std::is_same_v<T, FireworkPatternOriginSpec>)
            {
                return VariantEntry.PatternScale;
            }
            else if constexpr (std::is_same_v<T, BoxVolumeOriginSpec>)
            {
                return std::max({ VariantEntry.HalfExtentsMeters.X, VariantEntry.HalfExtentsMeters.Y, VariantEntry.HalfExtentsMeters.Z });
            }
            else
            {
                return 0.5f;
            }
        },
        Spec);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  PUBLIC MEMBER FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void ParticleRig::RebuildBakedColourLut() noexcept
{
    BakedColourLut = ColourSpace::PackColourRampLut64(TintBinding.SourceProfile);
}

void ParticleRig::RebuildOriginCaches(uint32_t /*Seed*/)
{
    if (const auto* SurfaceOrigin = std::get_if<SurfaceAnchorOriginSpec>(&Origin))
    {
        SampledAnchors = SampleSurfaceAnchors(
            *SurfaceOrigin,
            std::max(1u, Sizing.CapacitySlots),
            nullptr,
            Mathematics::WorldCoordinateBasis::RightHandedZUp);
    }
    else if (BackingSurface.FinishStyle != BackingFinishCategory::ParticlesOnlyHidden)
    {
        SurfaceAnchorOriginSpec BackingSpec{};
        BackingSpec.SurfacePreset    = BackingSurface.SurfacePreset;
        BackingSpec.SampleMode       = BackingSurface.SampleMode;
        BackingSpec.EulerRotationDeg = BackingSurface.EulerDegrees;
        SampledAnchors               = SampleSurfaceAnchors(
            BackingSpec,
            2048u,
            nullptr,
            Mathematics::WorldCoordinateBasis::RightHandedZUp);
    }

    if (const auto* FireworkOrigin = std::get_if<FireworkPatternOriginSpec>(&Origin))
    {
        FireworkTargets = SampleFireworkShell(
            FireworkOrigin->Shape,
            FireworkOrigin->SkyText,
            std::min(2048u, std::max(128u, Sizing.CapacitySlots)),
            0,
            Mathematics::WorldCoordinateBasis::RightHandedZUp);
    }
}

RigParticleUniform ParticleRig::PackGpuUniform(uint32_t SlotOffset, uint32_t SlotCount) const noexcept
{
    RigParticleUniform Out{};

    const Mathematics::Axis3D AnchorPos = ExtractOriginAnchorPosition(Origin);
    Mathematics::Axis3D       Dir       = Dynamics.Ballistics.DirectionAxis;
    const float DirLen = std::sqrt(Dir.X * Dir.X + Dir.Y * Dir.Y + Dir.Z * Dir.Z);
    if (DirLen > 1.0e-5f)
    {
        Dir.X /= DirLen;
        Dir.Y /= DirLen;
        Dir.Z /= DirLen;
    }
    else
    {
        Dir = { 0.0f, 0.0f, 1.0f };
    }

    ColourSpace::SpatialColourSampleInput LowIn{};
    LowIn.NormalizedCoordinate = 0.0f;
    ColourSpace::SpatialColourSampleInput MidIn{};
    MidIn.NormalizedCoordinate = 0.5f;
    ColourSpace::SpatialColourSampleInput HighIn{};
    HighIn.NormalizedCoordinate = 1.0f;

    const ColourSpace::LinearRgba PrimaryTint   = ColourSpace::SampleColourSource(TintBinding.SourceProfile, LowIn);
    const ColourSpace::LinearRgba MidTint       = ColourSpace::SampleColourSource(TintBinding.SourceProfile, MidIn);
    const ColourSpace::LinearRgba SecondaryTint = ColourSpace::SampleColourSource(TintBinding.SourceProfile, HighIn);

    const float LifeMin = std::max(0.05f, Sizing.LifetimeMinSec);
    const float LifeMax = std::max(LifeMin, Sizing.LifetimeMaxSec);

    // Slot 0: OriginRadius = xyz origin [m], w radius [m]
    Out.OriginRadius[0] = AnchorPos.X;
    Out.OriginRadius[1] = AnchorPos.Y;
    Out.OriginRadius[2] = AnchorPos.Z;
    Out.OriginRadius[3] = ExtractAnalyticOriginRadius(Origin);

    // Slot 1: DirectionSpread = xyz unit direction, w spread [rad]
    Out.DirectionSpread[0] = Dir.X;
    Out.DirectionSpread[1] = Dir.Y;
    Out.DirectionSpread[2] = Dir.Z;
    Out.DirectionSpread[3] = Dynamics.Ballistics.SpreadRadians;

    // Slot 2: SpeedDragGravity = x speedMin, y speedMax, z drag [1/s], w gravity [g]
    Out.SpeedDragGravity[0] = Dynamics.Ballistics.SpeedMin;
    Out.SpeedDragGravity[1] = Dynamics.Ballistics.SpeedMax;
    Out.SpeedDragGravity[2] = Dynamics.Ballistics.DragPerSecond;
    Out.SpeedDragGravity[3] = Dynamics.Ballistics.GravityFactor;

    // Slot 3: LifetimeSize = x lifeMin, y lifeMax, z sizeStart [m], w sizeEnd [m]
    Out.LifetimeSize[0] = LifeMin;
    Out.LifetimeSize[1] = LifeMax;
    Out.LifetimeSize[2] = Sizing.SizeStartMeters;
    Out.LifetimeSize[3] = Sizing.SizeEndMeters;

    // Slot 4..6: Primary, Secondary, Midpoint linear RGBA
    Out.PrimaryColour[0]   = PrimaryTint.Red;
    Out.PrimaryColour[1]   = PrimaryTint.Green;
    Out.PrimaryColour[2]   = PrimaryTint.Blue;
    Out.PrimaryColour[3]   = PrimaryTint.Alpha;
    Out.SecondaryColour[0] = SecondaryTint.Red;
    Out.SecondaryColour[1] = SecondaryTint.Green;
    Out.SecondaryColour[2] = SecondaryTint.Blue;
    Out.SecondaryColour[3] = SecondaryTint.Alpha;
    Out.MidpointColour[0]  = MidTint.Red;
    Out.MidpointColour[1]  = MidTint.Green;
    Out.MidpointColour[2]  = MidTint.Blue;
    Out.MidpointColour[3]  = MidTint.Alpha;

    // Slot 7: SimulationStep = x dt, y time, z frame, w simulation family
    Out.SimulationStep[3] = static_cast<float>(Dynamics.Family);

    // Slot 8: FluidResponse = x windCoupling [1/s], y bounce, z buoyancy [m/s^2], w flutter
    Out.FluidResponse[0] = Dynamics.ForceLink.WindCouplingRate;
    Out.FluidResponse[1] = Dynamics.Ballistics.BounceRestitution;
    Out.FluidResponse[2] = Dynamics.Ballistics.BuoyancyAccel;
    Out.FluidResponse[3] = Dynamics.Ballistics.FlutterAmp;

    // Slot 9: ReleaseBudget = x capacity, y originShape, z fractionA, w releaseCountThisStep
    Out.ReleaseBudget[0] = static_cast<float>(SlotCount);
    Out.ReleaseBudget[1] = static_cast<float>(QueryReleaseOriginCategory(Origin));
    Out.ReleaseBudget[2] = Dynamics.Molecular.InitialFractionA;
    Out.ReleaseBudget[3] = static_cast<float>(SlotOffset);

    // Slot 10: BoxExtentsHead = xyz box half extents [m], w head index
    if (const auto* Box = std::get_if<BoxVolumeOriginSpec>(&Origin))
    {
        Out.BoxExtentsHead[0] = Box->HalfExtentsMeters.X;
        Out.BoxExtentsHead[1] = Box->HalfExtentsMeters.Y;
        Out.BoxExtentsHead[2] = Box->HalfExtentsMeters.Z;
    }

    // Slot 11: MolecularTerms = x temperature, y epsilon/curlFreq, z sigma/curlAmp, w reactRate/curlRate
    if (Dynamics.Curl.Mode != CurlManifoldMode::Off)
    {
        Out.MolecularTerms[1] = Dynamics.Curl.SpatialFreq;
        Out.MolecularTerms[2] = Dynamics.Curl.DriveSpeed;
        Out.MolecularTerms[3] = Dynamics.Curl.EvolutionRate;
    }
    else
    {
        Out.MolecularTerms[0] = Dynamics.Molecular.Temperature;
        Out.MolecularTerms[1] = Dynamics.Molecular.Epsilon;
        Out.MolecularTerms[2] = Dynamics.Molecular.Sigma;
        Out.MolecularTerms[3] = Dynamics.Molecular.ReactionRate;
    }

    // Slot 12: GridLatticeTerms
    Out.GridLatticeTerms[1] = static_cast<float>(Dynamics.EffectorGrid.Columns);
    Out.GridLatticeTerms[2] = static_cast<float>(Dynamics.EffectorGrid.Rows);
    Out.GridLatticeTerms[3] = Dynamics.Molecular.DissociationRate;

    // Slot 13: ReactionDamping = x reactRadius, y damping, z shellRadius, w streakThinness
    Out.ReactionDamping[0] = Dynamics.Molecular.ReactionRadius;
    Out.ReactionDamping[1] = Dynamics.Molecular.LangevinDamping;
    Out.ReactionDamping[2] = Dynamics.Curl.ShellRadius;
    Out.ReactionDamping[3] = 1.0f;

    // Slot 14 & 18: CarrierDispatch & StrandAndBlend
    float CutoutShape   = 0.0f;
    float LeafSub       = 0.0f;
    float HeadStyle     = 0.0f;
    float HeadScale     = 1.0f;
    float Thinness      = 1.0f;
    float BlendEquation = static_cast<float>(TintBinding.SourceProfile.BlendEquation);
    float StrandVerts   = static_cast<float>(Dynamics.Strands.SegmentCount);

    if (const auto* Billboard = std::get_if<SpriteBillboardCarrier>(&Carrier))
    {
        CutoutShape   = static_cast<float>(Billboard->CutoutShape);
        LeafSub       = static_cast<float>(Billboard->LeafStyle);
        HeadStyle     = static_cast<float>(Billboard->BokehAperture);
        HeadScale     = Billboard->HeadScale;
        Thinness      = Billboard->StreakThinness;
        BlendEquation = static_cast<float>(Billboard->BlendEquation);
    }
    else if (const auto* Ribbon = std::get_if<RibbonStrandCarrier>(&Carrier))
    {
        Thinness      = Ribbon->ThicknessScale;
        StrandVerts   = static_cast<float>(Ribbon->SegmentCount);
        BlendEquation = static_cast<float>(Ribbon->BlendEquation);
    }
    else if (const auto* Solid = std::get_if<OrientedSolidCarrier>(&Carrier))
    {
        CutoutShape = (Solid->GeometryKind == SolidGeometryCategory::MetallicCoin)        ? 5.0f
                    : (Solid->GeometryKind == SolidGeometryCategory::GlassShardCrystal)   ? 18.0f
                                                                                          : 4.0f;
        BlendEquation = static_cast<float>(Solid->BlendEquation);
    }
    else if (const auto* Winged = std::get_if<WingedCreatureCarrier>(&Carrier))
    {
        CutoutShape   = 13.0f;
        BlendEquation = static_cast<float>(Winged->BlendEquation);
    }
    else if (const auto* Beam = std::get_if<DielectricBeamCarrier>(&Carrier))
    {
        BlendEquation = static_cast<float>(Beam->BlendEquation);
    }

    Out.ReactionDamping[3] = Thinness;
    Out.CarrierDispatch[2] = CutoutShape;
    Out.CarrierDispatch[3] = LeafSub;

    // Slot 15: ScaleAndModes = x sizeScale, y curlMode, z webMode, w dynMode
    Out.ScaleAndModes[0] = Sizing.OpacityPeak;
    Out.ScaleAndModes[1] = static_cast<float>(Dynamics.Curl.Mode);
    Out.ScaleAndModes[2] = static_cast<float>(Dynamics.Spiderweb.WebMode);
    Out.ScaleAndModes[3] = static_cast<float>(Dynamics.Strands.Mode);

    // Slot 16: ColourRangeMeta = x stopCount, y scalarChannel, z rangeMin, w rangeMax
    Out.ColourRangeMeta[0] = static_cast<float>(ColourSpace::ExtractOrderedStops(TintBinding.SourceProfile.Source).size());
    Out.ColourRangeMeta[1] = static_cast<float>(TintBinding.ScalarChannel);
    Out.ColourRangeMeta[2] = TintBinding.SourceProfile.Range.RangeMin;
    Out.ColourRangeMeta[3] = TintBinding.SourceProfile.Range.RangeMax;

    // Slot 17: ColourWrapMeta = x repeatCount, y wrapMode, z phaseOffset, w blendCurve
    Out.ColourWrapMeta[0] = TintBinding.SourceProfile.Range.RepeatCount;
    Out.ColourWrapMeta[1] = static_cast<float>(TintBinding.SourceProfile.Range.WrapBehaviour);
    Out.ColourWrapMeta[2] = TintBinding.SourceProfile.Range.PhaseOffset;
    Out.ColourWrapMeta[3] = static_cast<float>(TintBinding.SourceProfile.Range.BlendCurve);

    // Slot 18: StrandAndBlend = x strandVertices, y headScale, z headStyle, w blendEquation
    Out.StrandAndBlend[0] = StrandVerts;
    Out.StrandAndBlend[1] = HeadScale;
    Out.StrandAndBlend[2] = HeadStyle;
    Out.StrandAndBlend[3] = BlendEquation;

    // Slot 19 & 20: EffectorPrimary & EffectorSecondary
    Out.EffectorPrimary[0]   = Dynamics.EffectorGrid.PrimaryEffector.CentreMeters.X;
    Out.EffectorPrimary[1]   = Dynamics.EffectorGrid.PrimaryEffector.CentreMeters.Y;
    Out.EffectorPrimary[2]   = Dynamics.EffectorGrid.PrimaryEffector.CentreMeters.Z;
    Out.EffectorPrimary[3]   = Dynamics.EffectorGrid.PrimaryEffector.SizeMeters;
    Out.EffectorSecondary[0] = Dynamics.EffectorGrid.SecondaryEffector.CentreMeters.X;
    Out.EffectorSecondary[1] = Dynamics.EffectorGrid.SecondaryEffector.CentreMeters.Y;
    Out.EffectorSecondary[2] = Dynamics.EffectorGrid.SecondaryEffector.CentreMeters.Z;
    Out.EffectorSecondary[3] = Dynamics.EffectorGrid.SecondaryEffector.SizeMeters;

    // Slot 21: EffectorShapes
    Out.EffectorShapes[0] = static_cast<float>(Dynamics.EffectorGrid.PrimaryEffector.Shape);
    Out.EffectorShapes[1] = static_cast<float>(Dynamics.EffectorGrid.SecondaryEffector.Shape);
    Out.EffectorShapes[2] = static_cast<float>(Dynamics.Shield.Form);
    Out.EffectorShapes[3] = static_cast<float>(Dynamics.Shield.MotifBitmask);

    return Out;
}

} // namespace Frontier::ParticleSpace
