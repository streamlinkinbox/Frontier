/*==========================================================================================================================================
                                                    FOG SPACE DEFINITION
==========================================================================================================================================*/
// 🧩 Analytic closed-form height fog, planet-curved aerial perspective, local 3D volumetric fog, and GPU dispatch.

#include "Engine/PlanetaryAtmosphere/FogSpace.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace Frontier
{

namespace
{

double ProfileIntegral(double Z0, double ΔZ, double T0, double T1, double ReferenceHeightM, double ScaleHeightM) noexcept
{
    if (T1 <= T0)
    {
        return 0.0;
    }
    const auto AboveIntegral = [&](double A, double B) {
        const double ZA = Z0 + ΔZ * A;
        if (std::abs(ΔZ) < 1e-12)
        {
            return std::exp(-std::max(ZA - ReferenceHeightM, 0.0) / ScaleHeightM) * (B - A);
        }
        const double ZB     = Z0 + ΔZ * B;
        const double Length = B - A;
        const double Ratio  = std::abs(ΔZ) * Length / ScaleHeightM;
        return std::exp(-(std::min(ZA, ZB) - ReferenceHeightM) / ScaleHeightM) * Length * (-std::expm1(-Ratio) / Ratio);
    };
    const double ZA = Z0 + ΔZ * T0;
    const double ZB = Z0 + ΔZ * T1;
    if (ZA <= ReferenceHeightM && ZB <= ReferenceHeightM)
    {
        return T1 - T0;
    }
    if (ZA >= ReferenceHeightM && ZB >= ReferenceHeightM)
    {
        return AboveIntegral(T0, T1);
    }
    const double TCross = T0 + (ReferenceHeightM - ZA) / ΔZ;
    if (ZA < ReferenceHeightM)
    {
        return (TCross - T0) + AboveIntegral(TCross, T1);
    }
    return AboveIntegral(T0, TCross) + (T1 - TCross);
}

double FogDot(SpatialVector A, SpatialVector B) noexcept
{
    return A.X * B.X + A.Y * B.Y + A.Z * B.Z;
}

double FogLen(SpatialVector Vector) noexcept
{
    return std::sqrt(FogDot(Vector, Vector));
}

SpatialVector FogSub(SpatialVector A, SpatialVector B) noexcept
{
    return {A.X - B.X, A.Y - B.Y, A.Z - B.Z};
}

SpatialVector FogCross(SpatialVector A, SpatialVector B) noexcept
{
    return {A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X};
}

double FogSmooth(double T) noexcept
{
    const double Clamped = std::clamp(T, 0.0, 1.0);
    return Clamped * Clamped * (3.0 - 2.0 * Clamped);
}

double FogHashLattice(int32_t X, int32_t Y, int32_t Z, uint64_t Seed) noexcept
{
    uint32_t WordX = static_cast<uint32_t>(X) * 1597334677u + static_cast<uint32_t>(Seed);
    uint32_t WordY = static_cast<uint32_t>(Y) * 3812015801u + static_cast<uint32_t>(Seed >> 32u);
    uint32_t WordZ = static_cast<uint32_t>(Z) * 2654435761u;
    uint32_t Mixed = WordX ^ WordY ^ WordZ;
    Mixed ^= Mixed >> 16u;
    Mixed *= 0x7FEB352Du;
    Mixed ^= Mixed >> 15u;
    Mixed *= 0x846CA68Bu;
    Mixed ^= Mixed >> 16u;
    return static_cast<double>(Mixed) * (1.0 / 4294967296.0);
}

double FogNoise3D(SpatialVector Point, uint64_t Seed) noexcept
{
    const int32_t X0    = static_cast<int32_t>(std::floor(Point.X));
    const int32_t Y0    = static_cast<int32_t>(std::floor(Point.Y));
    const int32_t Z0    = static_cast<int32_t>(std::floor(Point.Z));
    const double  Fx    = FogSmooth(Point.X - static_cast<double>(X0));
    const double  Fy    = FogSmooth(Point.Y - static_cast<double>(Y0));
    const double  Fz    = FogSmooth(Point.Z - static_cast<double>(Z0));
    const auto    Blend = [](double A, double B, double T) { return A + (B - A) * T; };
    const double  C00   = Blend(FogHashLattice(X0, Y0, Z0, Seed), FogHashLattice(X0 + 1, Y0, Z0, Seed), Fx);
    const double  C10   = Blend(FogHashLattice(X0, Y0 + 1, Z0, Seed), FogHashLattice(X0 + 1, Y0 + 1, Z0, Seed), Fx);
    const double  C01   = Blend(FogHashLattice(X0, Y0, Z0 + 1, Seed), FogHashLattice(X0 + 1, Y0, Z0 + 1, Seed), Fx);
    const double  C11   = Blend(FogHashLattice(X0, Y0 + 1, Z0 + 1, Seed), FogHashLattice(X0 + 1, Y0 + 1, Z0 + 1, Seed), Fx);
    return Blend(Blend(C00, C10, Fy), Blend(C01, C11, Fy), Fz);
}

double FogFbm3D(SpatialVector Point, uint64_t Seed) noexcept
{
    double Sum = 0.0, Amp = 0.5, Total = 0.0;
    for (uint32_t Oct = 0; Oct < 3; ++Oct)
    {
        Sum += Amp * FogNoise3D(Point, Seed + Oct * 97u);
        Total += Amp;
        Point = {Point.X * 2.05 + 5.3, Point.Y * 2.05 + 3.1, Point.Z * 2.05 + 7.9};
        Amp *= 0.5;
    }
    return Sum / Total;
}

double SampleIesRing(const PhotometricFogLuminaire& Luminaire, SpatialVector LightToPointUnit) noexcept
{
    const double AxisLen = FogLen(Luminaire.AxisUnit);
    if (AxisLen <= 1e-9)
    {
        return 1.0;
    }
    const SpatialVector Axis{Luminaire.AxisUnit.X / AxisLen, Luminaire.AxisUnit.Y / AxisLen, Luminaire.AxisUnit.Z / AxisLen};
    const double        cosθ = std::clamp(FogDot(Axis, LightToPointUnit), -1.0, 1.0);
    if (cosθ < Luminaire.OuterConeCos)
    {
        return 0.0;
    }
    const double ConeSmooth = (Luminaire.InnerConeCos > Luminaire.OuterConeCos)
                                  ? FogSmooth((cosθ - Luminaire.OuterConeCos) / (Luminaire.InnerConeCos - Luminaire.OuterConeCos))
                                  : 1.0;
    const double   RingCoord = std::clamp((1.0 - cosθ) * 0.5 * 7.0, 0.0, 7.0);
    const uint32_t I0        = std::min(6u, static_cast<uint32_t>(RingCoord));
    const double   Frac      = RingCoord - static_cast<double>(I0);
    const double   IesWeight = static_cast<double>(Luminaire.IesPolarWeights[I0]) * (1.0 - Frac) +
                               static_cast<double>(Luminaire.IesPolarWeights[I0 + 1]) * Frac;
    return ConeSmooth * std::max(0.0, IesWeight);
}

float ConvertFogFloat(double Quantity)
{
    if (!std::isfinite(Quantity) || std::abs(Quantity) > std::numeric_limits<float>::max())
    {
        throw std::runtime_error("Medium parameter is not representable as float32");
    }
    return static_cast<float>(Quantity);
}

struct PushHeader
{
    uint32_t Counts[4];
    float    Tuning[4];
};

} // namespace

double FogTransmittance(const FogParameters& Parameters,
                        SpatialVector        Origin,
                        SpatialVector        Direction,
                        double               DistanceM) noexcept
{
    if (DistanceM <= 0.0 || Parameters.ReferenceDensity <= 0.0)
    {
        return 1.0;
    }
    const double ScaleH = std::max(Parameters.FalloffHeightM, 1e-6);
    const double τ      = Parameters.ReferenceDensity *
                          ProfileIntegral(Origin.Z, Direction.Z, 0.0, DistanceM, Parameters.ReferenceHeightM, ScaleH);
    return std::exp(-std::max(τ, 0.0));
}

SpectrumRgb ApplyFog(const FogParameters& Parameters, SpectrumRgb Radiance, double Transmittance) noexcept
{
    const double T = std::clamp(Transmittance, 0.0, 1.0);
    return {Radiance.R * T + Parameters.Colour.R * (1.0 - T),
            Radiance.G * T + Parameters.Colour.G * (1.0 - T),
            Radiance.B * T + Parameters.Colour.B * (1.0 - T)};
}

FogRadianceIntegral IntegrateFogRadiance(
    const FogParameters&                             Medium,
    SpatialVector                                    Origin,
    SpatialVector                                    Direction,
    double                                           DistanceM,
    uint32_t                                         Steps,
    double                                           ScatteringAlbedo,
    const std::function<SpectrumRgb(SpatialVector)>& Incident)
{
    FogRadianceIntegral Result;
    if (DistanceM <= 0 || Steps == 0 || Medium.ReferenceDensity <= 0)
    {
        return Result;
    }
    const double StepM  = DistanceM / Steps;
    const double Albedo = std::clamp(ScatteringAlbedo, 0.0, 1.0);
    for (uint32_t Step = 0; Step < Steps; ++Step)
    {
        const double        EndT = FogTransmittance(Medium, Origin, Direction, StepM * (Step + 1));
        const SpatialVector At{Origin.X + Direction.X * StepM * (Step + 0.5),
                               Origin.Y + Direction.Y * StepM * (Step + 0.5),
                               Origin.Z + Direction.Z * StepM * (Step + 0.5)};
        const SpectrumRgb Light  = Incident(At);
        const double      Weight = (Result.Transmittance - EndT) * Albedo;
        Result.Radiance.R += Weight * Light.R;
        Result.Radiance.G += Weight * Light.G;
        Result.Radiance.B += Weight * Light.B;
        Result.Transmittance = EndT;
    }
    return Result;
}

double EvaluateLocalFogExtinction(const LocalFogVolumeProfile& Volume, SpatialVector PointMeters) noexcept
{
    if (Volume.ExtinctionPerM <= 0.0)
    {
        return 0.0;
    }
    const double Envelope = EvaluateVolumeEnvelope(PointMeters, Volume.BoxCenterM, Volume.BoxHalfExtentsM,
        Volume.BoundsShape, Volume.BoxEdgeFalloffM, Volume.VerticalAxis);
    if (Envelope <= 0) return 0;
    const std::array<double, 3> Point = {PointMeters.X, PointMeters.Y, PointMeters.Z};
    const std::array<double, 3> Centre = {Volume.BoxCenterM.X, Volume.BoxCenterM.Y, Volume.BoxCenterM.Z};
    const std::array<double, 3> Half = {Volume.BoxHalfExtentsM.X, Volume.BoxHalfExtentsM.Y, Volume.BoxHalfExtentsM.Z};
    const uint32_t Axis = std::min(Volume.VerticalAxis, 2u);
    const double Lower = Centre[Axis] - (Volume.BoundsShape == VolumeShape::Hemisphere ? 0 : Half[Axis]);
    const double HeightTerm = Volume.HeightFalloffM > 0 ?
        std::exp(-std::max(Point[Axis] - Lower, 0.0) / std::max(Volume.HeightFalloffM, 0.5)) : 1;

    double NoiseFactor = 1.0;
    if (Volume.NoiseModulation > 0.0)
    {
        const double Scale = std::max(Volume.NoiseScaleM, 0.5);
        const SpatialVector Advected{
            (PointMeters.X - (Volume.DisplacementReady ? Volume.WindDisplacementM.X : Volume.WindVelocityMps.X * Volume.WindTimeS)) / Scale,
            (PointMeters.Y - (Volume.DisplacementReady ? Volume.WindDisplacementM.Y : Volume.WindVelocityMps.Y * Volume.WindTimeS)) / Scale,
            (PointMeters.Z - (Volume.DisplacementReady ? Volume.WindDisplacementM.Z : Volume.WindVelocityMps.Z * Volume.WindTimeS)) / Scale};
        const double Sample = FogFbm3D(Advected, Volume.Seed);
        NoiseFactor         = std::max(0.0, (1.0 - Volume.NoiseModulation) + 1.85 * Volume.NoiseModulation * Sample);
    }
    return Volume.ExtinctionPerM * Envelope * HeightTerm * NoiseFactor;
}

FogRadianceIntegral EvaluateUnifiedVolumetricFog(
    const UnifiedVolumetricFogSpecification&                         Specification,
    SpatialVector                                                    OriginMeters,
    SpatialVector                                                    DirectionUnit,
    double                                                           SurfaceDistanceMeters,
    const std::function<bool(SpatialVector, SpatialVector, double)>& IsOccluded) noexcept
{
    FogRadianceIntegral Result;
    const double DirLen = FogLen(DirectionUnit);
    if (SurfaceDistanceMeters <= 0.0 || DirLen <= 1e-9)
    {
        return Result;
    }
    const SpatialVector Dir{DirectionUnit.X / DirLen, DirectionUnit.Y / DirLen, DirectionUnit.Z / DirLen};
    const double        MarchSpanM          = std::min(SurfaceDistanceMeters, std::max(1.0, Specification.MaxFogMarchDistanceM));
    const uint32_t      Steps               = std::max(1u, Specification.FroxelSteps);
    double              MediumTransmittance = 1.0;
    SpectrumRgb         MediumRadiance{};

    for (uint32_t Step = 0; Step < Steps; ++Step)
    {
        const double U0   = static_cast<double>(Step) / static_cast<double>(Steps);
        const double U1   = static_cast<double>(Step + 1) / static_cast<double>(Steps);
        const double T0   = MarchSpanM * (0.35 * U0 + 0.65 * U0 * U0);
        const double T1   = MarchSpanM * (0.35 * U1 + 0.65 * U1 * U1);
        const double Δt   = T1 - T0;
        const double MidT = 0.5 * (T0 + T1);
        const SpatialVector At{OriginMeters.X + Dir.X * MidT,
                               OriginMeters.Y + Dir.Y * MidT,
                               OriginMeters.Z + Dir.Z * MidT};

        const SpatialVector SegStart{OriginMeters.X + Dir.X * T0, OriginMeters.Y + Dir.Y * T0, OriginMeters.Z + Dir.Z * T0};
        const double HeightSegT = Specification.HeightFog.ReferenceDensity > 0.0
                                      ? FogTransmittance(Specification.HeightFog, SegStart, Dir, Δt)
                                      : 1.0;
        const double τHeight    = -std::log(std::max(HeightSegT, 1e-12));

        double      τLocal       = 0.0;
        double      LocalAlbedo  = Specification.HeightFogAlbedo;
        double      𝑔Local       = Specification.HeightFogPhaseG;
        SpectrumRgb LocalTint    = {1.0, 1.0, 1.0};
        double      PeakLocalExt = 0.0;

        for (const LocalFogVolumeProfile& Volume : Specification.LocalVolumes)
        {
            const double σExt = EvaluateLocalFogExtinction(Volume, At);
            if (σExt <= 0.0)
            {
                continue;
            }
            τLocal += σExt * Δt;
            if (σExt > PeakLocalExt)
            {
                PeakLocalExt = σExt;
                LocalAlbedo  = Volume.ScatteringAlbedo;
                𝑔Local       = Volume.PhaseAsymmetryG;
                LocalTint    = Volume.ScatteringTint;
            }
        }

        const double τSegment = τHeight + τLocal;
        if (τSegment <= 1e-9)
        {
            continue;
        }
        const double SegTrans   = std::exp(-τSegment);
        const double BlendLocal = τLocal / τSegment;
        const double EffAlbedo  = Specification.HeightFogAlbedo * (1.0 - BlendLocal) + LocalAlbedo * BlendLocal;
        const double 𝑔Effective = Specification.HeightFogPhaseG * (1.0 - BlendLocal) + 𝑔Local * BlendLocal;
        const SpectrumRgb EffTint{
            Specification.HeightFog.Colour.R * (1.0 - BlendLocal) + LocalTint.R * BlendLocal,
            Specification.HeightFog.Colour.G * (1.0 - BlendLocal) + LocalTint.G * BlendLocal,
            Specification.HeightFog.Colour.B * (1.0 - BlendLocal) + LocalTint.B * BlendLocal};

        SpectrumRgb Incident = Specification.SkyAmbientRadiance;

        for (const AtmosphereSolarSource& Sun : Specification.SolarSources)
        {
            const double SunLen = FogLen(Sun.Direction);
            if (SunLen <= 1e-9)
            {
                continue;
            }
            const SpatialVector SunDir{Sun.Direction.X / SunLen, Sun.Direction.Y / SunLen, Sun.Direction.Z / SunLen};
            if (SunDir.Z <= -0.02)
            {
                continue;
            }
            if (IsOccluded && IsOccluded(At, SunDir, 10000.0))
            {
                continue;
            }
            const double CloudTrans = Specification.CloudShadow ? Specification.CloudShadow->SampleTransmittance(At) : 1.0;
            if (CloudTrans <= 1e-4)
            {
                continue;
            }
            const double RadiusKm = Specification.Atmosphere.GroundRadiusKm +
                                    std::max(1e-3, Specification.ObserverAltitudeKm + 1e-3 * At.Z);
            const SpectrumRgb AtmosSunT = (Specification.Transmittance && Specification.Transmittance->Baked())
                                              ? Specification.Transmittance->Sample(Specification.Atmosphere, RadiusKm, SunDir.Z)
                                              : TransmittanceToTop(Specification.Atmosphere, RadiusKm, SunDir.Z, 16);
            const double Phase = HenyeyGreensteinPhase(FogDot(Dir, SunDir), 𝑔Effective);
            Incident.R += Sun.Irradiance.R * AtmosSunT.R * CloudTrans * Phase;
            Incident.G += Sun.Irradiance.G * AtmosSunT.G * CloudTrans * Phase;
            Incident.B += Sun.Irradiance.B * AtmosSunT.B * CloudTrans * Phase;
        }

        for (const PhotometricFogLuminaire& Luminaire : Specification.Luminaires)
        {
            const SpatialVector Delta = FogSub(Luminaire.PositionM, At);
            const double        Dist2 = FogDot(Delta, Delta);
            const double        Dist  = std::sqrt(Dist2);
            if (Dist <= 0.05 || Dist > Luminaire.RangeMeters)
            {
                continue;
            }
            const SpatialVector TowardLight{Delta.X / Dist, Delta.Y / Dist, Delta.Z / Dist};
            const SpatialVector LightToPt{-TowardLight.X, -TowardLight.Y, -TowardLight.Z};
            const double        Ies = SampleIesRing(Luminaire, LightToPt);
            if (Ies <= 1e-5)
            {
                continue;
            }
            if (IsOccluded && IsOccluded(At, TowardLight, Dist - 0.05))
            {
                continue;
            }
            const double RangeWindow = FogSmooth(1.0 - Dist / Luminaire.RangeMeters);
            const double Atten       = (Ies * RangeWindow) / std::max(0.25, Dist2);
            const double Phase       = HenyeyGreensteinPhase(FogDot(Dir, TowardLight), 𝑔Effective);
            Incident.R += Luminaire.PeakIntensity.R * Atten * Phase;
            Incident.G += Luminaire.PeakIntensity.G * Atten * Phase;
            Incident.B += Luminaire.PeakIntensity.B * Atten * Phase;
        }

        for (const LightningFogChannel& Bolt : Specification.LightningChannels)
        {
            const SpatialVector Seg  = FogSub(Bolt.SegmentEndM, Bolt.SegmentStartM);
            const double        Seg2 = std::max(FogDot(Seg, Seg), 1e-6);
            const double        Proj = std::clamp(FogDot(FogSub(At, Bolt.SegmentStartM), Seg) / Seg2, 0.0, 1.0);
            const SpatialVector Closest{Bolt.SegmentStartM.X + Seg.X * Proj,
                                        Bolt.SegmentStartM.Y + Seg.Y * Proj,
                                        Bolt.SegmentStartM.Z + Seg.Z * Proj};
            const SpatialVector Delta = FogSub(Closest, At);
            const double        Dist2 = FogDot(Delta, Delta);
            const double        Dist  = std::sqrt(Dist2);
            if (Dist > Bolt.ReachMeters)
            {
                continue;
            }
            const SpatialVector Toward = Dist > 1e-4 ? SpatialVector{Delta.X / Dist, Delta.Y / Dist, Delta.Z / Dist}
                                                     : SpatialVector{0.0, 0.0, 1.0};
            const double Phase   = HenyeyGreensteinPhase(FogDot(Dir, Toward), 0.35 * 𝑔Effective);
            const double Falloff = FogSmooth(1.0 - Dist / Bolt.ReachMeters) /
                                   std::max(Bolt.CoreRadiusM * Bolt.CoreRadiusM, Dist2);
            Incident.R += Bolt.LineRadiance.R * Falloff * Phase;
            Incident.G += Bolt.LineRadiance.G * Falloff * Phase;
            Incident.B += Bolt.LineRadiance.B * Falloff * Phase;
        }

        for (const RadiantFogFacet& Facet : Specification.RadiantFacets)
        {
            const SpatialVector Centroid{(Facet.CornerA.X + Facet.CornerB.X + Facet.CornerC.X) / 3.0,
                                         (Facet.CornerA.Y + Facet.CornerB.Y + Facet.CornerC.Y) / 3.0,
                                         (Facet.CornerA.Z + Facet.CornerB.Z + Facet.CornerC.Z) / 3.0};
            const SpatialVector Edge1       = FogSub(Facet.CornerB, Facet.CornerA);
            const SpatialVector Edge2       = FogSub(Facet.CornerC, Facet.CornerA);
            const SpatialVector NormalCross = FogCross(Edge1, Edge2);
            const double        DoubleArea  = FogLen(NormalCross);
            if (DoubleArea <= 1e-6)
            {
                continue;
            }
            const double        Area   = 0.5 * DoubleArea;
            const SpatialVector Normal = {NormalCross.X / DoubleArea, NormalCross.Y / DoubleArea, NormalCross.Z / DoubleArea};
            const SpatialVector Delta  = FogSub(Centroid, At);
            const double        Dist2  = FogDot(Delta, Delta);
            const double        Dist   = std::sqrt(Dist2);
            if (Dist <= 0.08)
            {
                continue;
            }
            const SpatialVector Toward{Delta.X / Dist, Delta.Y / Dist, Delta.Z / Dist};
            if (IsOccluded && IsOccluded(At, Toward, Dist - 0.06))
            {
                continue;
            }
            const double Geom  = std::abs(FogDot(Normal, Toward)) * Area / std::max(0.25, Dist2);
            const double Phase = HenyeyGreensteinPhase(FogDot(Dir, Toward), 𝑔Effective);
            Incident.R += Facet.SurfaceRadiance.R * Geom * Phase;
            Incident.G += Facet.SurfaceRadiance.G * Geom * Phase;
            Incident.B += Facet.SurfaceRadiance.B * Geom * Phase;
        }

        const double ScatterWeight = MediumTransmittance * (1.0 - SegTrans) * EffAlbedo;
        MediumRadiance.R += ScatterWeight * Incident.R * EffTint.R;
        MediumRadiance.G += ScatterWeight * Incident.G * EffTint.G;
        MediumRadiance.B += ScatterWeight * Incident.B * EffTint.B;
        MediumTransmittance *= SegTrans;
    }

    if (SurfaceDistanceMeters > MarchSpanM && Specification.HeightFog.ReferenceDensity > 0.0)
    {
        const SpatialVector TailOrigin{OriginMeters.X + Dir.X * MarchSpanM,
                                       OriginMeters.Y + Dir.Y * MarchSpanM,
                                       OriginMeters.Z + Dir.Z * MarchSpanM};
        const double TailT = FogTransmittance(Specification.HeightFog, TailOrigin, Dir, SurfaceDistanceMeters - MarchSpanM);
        MediumRadiance.R += MediumTransmittance * (1.0 - TailT) * Specification.HeightFog.Colour.R * Specification.SkyAmbientRadiance.R;
        MediumRadiance.G += MediumTransmittance * (1.0 - TailT) * Specification.HeightFog.Colour.G * Specification.SkyAmbientRadiance.G;
        MediumRadiance.B += MediumTransmittance * (1.0 - TailT) * Specification.HeightFog.Colour.B * Specification.SkyAmbientRadiance.B;
        MediumTransmittance *= TailT;
    }

    if (Specification.EnableAerialPerspective && !Specification.SolarSources.empty())
    {
        const AerialPerspectiveSample Aerial = EvaluateAerialPerspective(
            Specification.Atmosphere,
            Specification.Transmittance,
            Specification.MultiScatter,
            Specification.ObserverAltitudeKm,
            OriginMeters,
            Dir,
            SurfaceDistanceMeters,
            Specification.SolarSources,
            12);
        const double MeanAerialT = (Aerial.Transmittance.R + Aerial.Transmittance.G + Aerial.Transmittance.B) / 3.0;
        Result.Radiance          = {MediumRadiance.R * Aerial.Transmittance.R + Aerial.Inscatter.R * MediumTransmittance,
                                    MediumRadiance.G * Aerial.Transmittance.G + Aerial.Inscatter.G * MediumTransmittance,
                                    MediumRadiance.B * Aerial.Transmittance.B + Aerial.Inscatter.B * MediumTransmittance};
        Result.Transmittance     = MediumTransmittance * MeanAerialT;
    }
    else
    {
        Result.Radiance      = MediumRadiance;
        Result.Transmittance = MediumTransmittance;
    }

    return Result;
}

std::array<float, FogRadianceFloatCount> CompileFogRadiancePayload(
    const FogRadianceSpecification& Specification)
{
    const FogParameters& Medium = Specification.Medium;
    const std::array<float, FogRadianceFloatCount> Payload{
        ConvertFogFloat(Medium.ReferenceDensity),
        ConvertFogFloat(Medium.ReferenceHeightM),
        ConvertFogFloat(Medium.FalloffHeightM),
        ConvertFogFloat(Specification.ScatteringAlbedo),
        ConvertFogFloat(Specification.PhaseAsymmetry),
        ConvertFogFloat(Specification.MaximumViewDistanceM),
        static_cast<float>(Specification.ViewSamples),
        static_cast<float>(Specification.CloudShadowSamples),
        ConvertFogFloat(Specification.AmbientRadiance.R),
        ConvertFogFloat(Specification.AmbientRadiance.G),
        ConvertFogFloat(Specification.AmbientRadiance.B),
        Specification.GeometryShadowsEnabled ? 1.0f : 0.0f,
        Specification.CloudShadowsEnabled ? 1.0f : 0.0f,
        Specification.AnalyticLightsEnabled ? 1.0f : 0.0f,
        ConvertFogFloat(Specification.MaximumLightDistanceM),
        static_cast<float>(Specification.FacetSamples)};
    ValidateFogRadiancePayload(Payload.data(), Payload.size());
    return Payload;
}

void ValidateFogRadiancePayload(const float* Payload, std::size_t FloatCount)
{
    if (Payload == nullptr || FloatCount < FogRadianceFloatCount)
    {
        throw std::runtime_error("Fog input span is truncated");
    }
    for (std::size_t Index = 0; Index < FogRadianceFloatCount; ++Index)
    {
        if (!std::isfinite(Payload[Index]))
        {
            throw std::runtime_error("Fog input contains a nonfinite parameter");
        }
    }
    if (!(Payload[0] >= 0 && Payload[0] <= 1 && std::abs(Payload[1]) <= 10000 &&
          Payload[2] >= 1 && Payload[2] <= 10000 && Payload[3] >= 0 && Payload[3] <= 1 &&
          std::abs(Payload[4]) < 0.95f && Payload[5] > 0 && Payload[5] <= 6000 &&
          Payload[6] >= 1 && Payload[6] <= 128 && std::floor(Payload[6]) == Payload[6] &&
          Payload[7] >= 1 && Payload[7] <= 128 && std::floor(Payload[7]) == Payload[7] &&
          Payload[8] >= 0 && Payload[9] >= 0 && Payload[10] >= 0 &&
          (Payload[11] == 0 || Payload[11] == 1) && (Payload[12] == 0 || Payload[12] == 1) &&
          (Payload[13] == 0 || Payload[13] == 1) && Payload[14] > 0 && Payload[14] <= 100000 &&
          Payload[15] >= 0 && Payload[15] <= 64 && std::floor(Payload[15]) == Payload[15]))
    {
        throw std::runtime_error("Fog parameters exceed the bounded shader domain");
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                      GPU VOLUMETRIC FOG SPACE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

bool FogSpace::Initialize(VulkanComputeExchange& Exchange,
                          const std::string&     ShaderBinaryDirectory,
                          std::string*           OutDiagnostic)
{
    const std::array<VkDescriptorType, 8> Bindings{{
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         // Ensemble
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // TransLut
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // MultiLut
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // CloudShadowLut
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          // VisibilityProjection
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          // HdrFrame
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         // SceneShadowFrame
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // SceneShadowDepth
    }};

    if (!Exchange.LoadComputeProgram(ShaderBinaryDirectory + "/IntegrateFogSpace.spv",
                                     "IntegrateFogSpaceMain",
                                     Bindings,
                                     sizeof(PushHeader),
                                     Program_,
                                     OutDiagnostic))
    {
        return false;
    }
    const std::array<float, kFogSceneShadowFloat4Count * 4u> Absent{};
    return Exchange.UploadMemorySpan(AbsentSceneShadow_, Absent.data(), sizeof(Absent));
}

void FogSpace::Shutdown(VulkanComputeExchange& Exchange) noexcept
{
    Exchange.ReleaseMemorySpan(AbsentSceneShadow_);
    Exchange.ReleaseProgram(Program_);
}

bool FogSpace::Execute(VulkanComputeExchange&      Exchange,
                       const DeviceMemorySpan&     EnsembleSpan,
                       const AtmosphereProjection& AtmosphereLuts,
                       const VolumetricClouds&     Clouds,
                       const DeviceImageArraySlot& VisibilityImage,
                       DeviceImageArraySlot&       HdrFrameTarget,
                       uint32_t                    ViewportWidth,
                       uint32_t                    ViewportHeight,
                       const FogSceneShadow*       SceneShadow)
{
    const PushHeader Push{{ViewportWidth, ViewportHeight, 0u, 0u}, {static_cast<float>(MarchSamples_), 0.0f, 0.0f, 0.0f}};
    // 📝 Without scene cascades the shader sees control x = 0 and never samples the stand-in depth (the cloud shadow map).
    const bool Shadowed = SceneShadow != nullptr && SceneShadow->Frame != nullptr && SceneShadow->Depth != nullptr &&
                          SceneShadow->Frame->Handle != VK_NULL_HANDLE && SceneShadow->Depth->ArrayView != VK_NULL_HANDLE;
    const std::array<ComputeDescriptorArgument, 8> Args{{
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &EnsembleSpan, nullptr},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &AtmosphereLuts.TransmittanceArray()},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &AtmosphereLuts.MultiScatteringArray()},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, &Clouds.CloudShadowArray()},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &VisibilityImage},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &HdrFrameTarget},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Shadowed ? SceneShadow->Frame : &AbsentSceneShadow_, nullptr},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, nullptr, Shadowed ? SceneShadow->Depth : &Clouds.CloudShadowArray()},
    }};
    return Exchange.DispatchCompute(
        Program_, Args, &Push, sizeof(Push), (ViewportWidth + 7u) / 8u, (ViewportHeight + 7u) / 8u, 1u);
}

} // namespace Frontier
