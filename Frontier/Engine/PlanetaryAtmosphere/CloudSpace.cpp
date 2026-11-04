/*==========================================================================================================================================
                                                   CLOUD SPACE DEFINITION
==========================================================================================================================================*/
// 🧩 Magnitude-invariant uint32 lattice hash, C2 quintic fade, rotated 3D octaves, Worley erosion,
//    6 tropospheric profiles, Beers-Powder scattering, and wide-span orthographic shadow projection.

#include "Engine/PlanetaryAtmosphere/CloudSpace.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace Frontier
{

namespace
{

constexpr double π = 3.14159265358979323846;

SpatialVector Add(SpatialVector A, SpatialVector B) noexcept { return {A.X + B.X, A.Y + B.Y, A.Z + B.Z}; }
SpatialVector Mul(SpatialVector A, double Scalar) noexcept { return {A.X * Scalar, A.Y * Scalar, A.Z * Scalar}; }
double        Dot(SpatialVector A, SpatialVector B) noexcept { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }

// Magnitude-invariant uint32 bit-mix hash (matches SultanAladin/Frontier- CloudShadow.slang).
double HashLattice(int32_t X, int32_t Y, int32_t Z, uint64_t Seed) noexcept
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

// C2 Perlin quintic fade: 6t^5 - 15t^4 + 10t^3 (zero 1st and 2nd derivatives at cell boundaries).
double QuinticFade(double T) noexcept
{
    const double Clamped = std::clamp(T, 0.0, 1.0);
    return Clamped * Clamped * Clamped * (Clamped * (Clamped * 6.0 - 15.0) + 10.0);
}

double SmoothFade(double T) noexcept
{
    const double Clamped = std::clamp(T, 0.0, 1.0);
    return Clamped * Clamped * (3.0 - 2.0 * Clamped);
}

// Orthonormal 3D octave rotation so consecutive octave lattice planes never align.
SpatialVector RotateCloudOctave(SpatialVector Point) noexcept
{
    return {
         0.00 * Point.X + 0.80 * Point.Y + 0.60 * Point.Z,
        -0.80 * Point.X + 0.36 * Point.Y - 0.48 * Point.Z,
        -0.60 * Point.X - 0.48 * Point.Y + 0.64 * Point.Z,
    };
}

double ValueNoise3D(SpatialVector Point, uint64_t Seed) noexcept
{
    const int32_t X0 = static_cast<int32_t>(std::floor(Point.X));
    const int32_t Y0 = static_cast<int32_t>(std::floor(Point.Y));
    const int32_t Z0 = static_cast<int32_t>(std::floor(Point.Z));
    const double  Fx = QuinticFade(Point.X - static_cast<double>(X0));
    const double  Fy = QuinticFade(Point.Y - static_cast<double>(Y0));
    const double  Fz = QuinticFade(Point.Z - static_cast<double>(Z0));
    const auto    Blend = [](double A, double B, double T) noexcept { return A + (B - A) * T; };

    const double C00 = Blend(HashLattice(X0, Y0, Z0, Seed), HashLattice(X0 + 1, Y0, Z0, Seed), Fx);
    const double C10 = Blend(HashLattice(X0, Y0 + 1, Z0, Seed), HashLattice(X0 + 1, Y0 + 1, Z0, Seed), Fx);
    const double C01 = Blend(HashLattice(X0, Y0, Z0 + 1, Seed), HashLattice(X0 + 1, Y0, Z0 + 1, Seed), Fx);
    const double C11 = Blend(HashLattice(X0, Y0 + 1, Z0 + 1, Seed), HashLattice(X0 + 1, Y0 + 1, Z0 + 1, Seed), Fx);
    return Blend(Blend(C00, C10, Fy), Blend(C01, C11, Fy), Fz);
}

double RotatedOctaveFbm(SpatialVector Point, uint64_t Seed) noexcept
{
    double Sum       = 0.0;
    double Amplitude = 0.5;
    double Norm      = 0.0;
    for (uint32_t Octave = 0; Octave < 4; ++Octave)
    {
        Sum += Amplitude * ValueNoise3D(Point, Seed + Octave * 131u);
        Norm += Amplitude;
        const SpatialVector Rotated = RotateCloudOctave(Point);
        Point                       = {Rotated.X * 2.03 + 11.3, Rotated.Y * 2.03 + 7.7, Rotated.Z * 2.03 + 5.1};
        Amplitude *= 0.5;
    }
    return Sum / Norm;
}

double EvaluateTroposphericHeightEnvelope(TroposphericCloudProfile Profile, double HeightFraction) noexcept
{
    const double H = std::clamp(HeightFraction, 0.0, 1.0);
    switch (Profile)
    {
        case TroposphericCloudProfile::Stratus:
            return SmoothFade(H / 0.12) * SmoothFade((1.0 - H) / 0.25);
        case TroposphericCloudProfile::Stratocumulus:
            return SmoothFade(H / 0.16) * SmoothFade((1.0 - H) / 0.35);
        case TroposphericCloudProfile::Cumulus:
            return std::clamp(std::min(H * 4.2, 1.0) * (1.0 - std::max(H - 0.65, 0.0) / 0.35 * 0.85), 0.0, 1.0);
        case TroposphericCloudProfile::Cumulonimbus:
            return SmoothFade(H / 0.10) * (0.72 + 0.28 * SmoothFade((H - 0.55) / 0.30)) * SmoothFade((1.0 - H) / 0.08);
        case TroposphericCloudProfile::Altostratus:
            return SmoothFade(H / 0.22) * SmoothFade((1.0 - H) / 0.28);
        case TroposphericCloudProfile::Cirrus:
            return std::sin(π * H) * (0.65 + 0.35 * H);
    }
    return SmoothFade(H / 0.16) * SmoothFade((1.0 - H) / 0.30);
}

double WorleyCellular3D(SpatialVector Point, uint64_t Seed) noexcept
{
    const int32_t CellX = static_cast<int32_t>(std::floor(Point.X));
    const int32_t CellY = static_cast<int32_t>(std::floor(Point.Y));
    const int32_t CellZ = static_cast<int32_t>(std::floor(Point.Z));
    double        MinDist2 = 8.0;
    for (int32_t Dz = -1; Dz <= 1; ++Dz)
    {
        for (int32_t Dy = -1; Dy <= 1; ++Dy)
        {
            for (int32_t Dx = -1; Dx <= 1; ++Dx)
            {
                const int32_t Nx = CellX + Dx;
                const int32_t Ny = CellY + Dy;
                const int32_t Nz = CellZ + Dz;
                const double  Jx = static_cast<double>(Nx) + HashLattice(Nx, Ny, Nz, Seed ^ 0xA173ull);
                const double  Jy = static_cast<double>(Ny) + HashLattice(Nx, Ny, Nz, Seed ^ 0xB295ull);
                const double  Jz = static_cast<double>(Nz) + HashLattice(Nx, Ny, Nz, Seed ^ 0xC3D7ull);
                const double  Ox = Point.X - Jx;
                const double  Oy = Point.Y - Jy;
                const double  Oz = Point.Z - Jz;
                MinDist2         = std::min(MinDist2, Ox * Ox + Oy * Oy + Oz * Oz);
            }
        }
    }
    return std::clamp(1.0 - std::sqrt(MinDist2), 0.0, 1.0);
}

double WorleyFbm3D(SpatialVector Point, uint64_t Seed) noexcept
{
    const double W0 = WorleyCellular3D(Point, Seed);
    const SpatialVector R1 = RotateCloudOctave(Point);
    const double W1 = WorleyCellular3D({R1.X * 2.0 + 3.7, R1.Y * 2.0 + 1.9, R1.Z * 2.0 + 5.3}, Seed + 79u);
    const SpatialVector R2 = RotateCloudOctave(R1);
    const double W2 = WorleyCellular3D({R2.X * 4.0 + 7.1, R2.Y * 4.0 + 4.3, R2.Z * 4.0 + 2.9}, Seed + 193u);
    return 0.625 * W0 + 0.25 * W1 + 0.125 * W2;
}

bool IntersectRayBounds(SpatialVector Origin,
                        SpatialVector Direction,
                        SpatialVector LowerBound,
                        SpatialVector UpperBound,
                        double        MaxDist,
                        double&       OutEnter,
                        double&       OutExit) noexcept
{
    double       TMin    = 0.0;
    double       TMax    = MaxDist;
    const double Orig[3] = {Origin.X, Origin.Y, Origin.Z};
    const double Dir[3]  = {Direction.X, Direction.Y, Direction.Z};
    const double Lo[3]   = {LowerBound.X, LowerBound.Y, LowerBound.Z};
    const double Hi[3]   = {UpperBound.X, UpperBound.Y, UpperBound.Z};
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        if (std::abs(Dir[Axis]) < 1e-10)
        {
            if (Orig[Axis] < Lo[Axis] || Orig[Axis] > Hi[Axis])
            {
                return false;
            }
        }
        else
        {
            const double A = (Lo[Axis] - Orig[Axis]) / Dir[Axis];
            const double B = (Hi[Axis] - Orig[Axis]) / Dir[Axis];
            TMin           = std::max(TMin, std::min(A, B));
            TMax           = std::min(TMax, std::max(A, B));
            if (TMax <= TMin)
            {
                return false;
            }
        }
    }
    OutEnter = TMin;
    OutExit  = TMax;
    return TMax > TMin;
}

float ConvertRadianceFloat(double Quantity)
{
    if (!std::isfinite(Quantity) || std::abs(Quantity) > std::numeric_limits<float>::max())
    {
        throw std::runtime_error("Medium parameter is not representable as float32");
    }
    return static_cast<float>(Quantity);
}

} // namespace

double CloudDensity(const CloudParameters& Parameters, SpatialVector Point) noexcept
{
    if (Point.Z < Parameters.LowerHeightM || Point.Z > Parameters.TopHeightM ||
        Parameters.TopHeightM <= Parameters.LowerHeightM)
    {
        return 0.0;
    }
    if (Parameters.UniformDensityOverride >= 0.0)
    {
        return Parameters.UniformDensityOverride * Parameters.DensityScale;
    }
    const double HeightFraction = (Point.Z - Parameters.LowerHeightM) / (Parameters.TopHeightM - Parameters.LowerHeightM);
    const double Envelope = EvaluateTroposphericHeightEnvelope(TroposphericCloudProfile::Cumulus, HeightFraction);
    const SpatialVector SamplePoint{
        (Point.X + Parameters.WindXMps * Parameters.WindTimeS) / 780.0,
        (Point.Y + Parameters.WindYMps * Parameters.WindTimeS) / 780.0,
        Point.Z / 420.0};
    const double MacroNoise = RotatedOctaveFbm(SamplePoint, Parameters.Seed);
    const double Coverage   = std::clamp(Parameters.Coverage, 0.0, 1.0);
    const double Shaped     = std::clamp((MacroNoise - (1.0 - Coverage)) / std::max(Coverage, 1e-3), 0.0, 1.5);
    return Shaped * Envelope * Parameters.DensityScale;
}

CloudMarchResult MarchClouds(const CloudParameters& Parameters,
                             SpatialVector          Origin,
                             SpatialVector          Direction,
                             SpatialVector          SunDirection,
                             const SpectrumRgb&     SunRadiance,
                             double                 MaxDistanceM,
                             uint32_t               Steps,
                             uint32_t               SunSteps) noexcept
{
    CloudMarchResult Result;
    if (Steps == 0 || MaxDistanceM <= 0.0)
    {
        return Result;
    }
    double TEnter = 0.0;
    double TExit  = MaxDistanceM;
    if (std::abs(Direction.Z) < 1e-9)
    {
        if (Origin.Z < Parameters.LowerHeightM || Origin.Z > Parameters.TopHeightM)
        {
            return Result;
        }
    }
    else
    {
        const double TA = (Parameters.LowerHeightM - Origin.Z) / Direction.Z;
        const double TB = (Parameters.TopHeightM - Origin.Z) / Direction.Z;
        TEnter          = std::max(std::min(TA, TB), 0.0);
        TExit           = std::min(std::max(TA, TB), MaxDistanceM);
        if (TExit <= TEnter)
        {
            return Result;
        }
    }

    const double Δt    = (TExit - TEnter) / static_cast<double>(Steps);
    const double Phase = HenyeyGreensteinPhase(Dot(Direction, SunDirection), Parameters.PhaseG);
    double       Transmittance = 1.0;
    SpectrumRgb  Radiance{};

    for (uint32_t Step = 0; Step < Steps; ++Step)
    {
        const double        RayT    = TEnter + Δt * (static_cast<double>(Step) + 0.5);
        const SpatialVector At      = Add(Origin, Mul(Direction, RayT));
        const double        Density = CloudDensity(Parameters, At);
        if (Density <= 0.0)
        {
            continue;
        }
        const double σExtinction       = Parameters.ExtinctionPerM * Density;
        const double StepTransmittance = std::exp(-σExtinction * Δt);
        if (Transmittance < 1e-4)
        {
            Transmittance *= StepTransmittance;
            continue;
        }

        double τSolar = 0.0;
        if (SunSteps > 0)
        {
            const double ΔsSolar = 180.0;
            for (uint32_t Sub = 0; Sub < SunSteps; ++Sub)
            {
                const SpatialVector ShadowAt = Add(At, Mul(SunDirection, ΔsSolar * (static_cast<double>(Sub) + 0.5)));
                τSolar += Parameters.ExtinctionPerM * CloudDensity(Parameters, ShadowAt) * ΔsSolar;
                if (ShadowAt.Z > Parameters.TopHeightM && SunDirection.Z > 0.0)
                {
                    break;
                }
            }
        }
        const double SunTransmittance = std::exp(-τSolar);
        const double ScatterAmount    = Transmittance * (1.0 - StepTransmittance) * Phase * SunTransmittance;
        const double AmbientAmount    = Transmittance * (1.0 - StepTransmittance);
        Radiance.R += ScatterAmount * SunRadiance.R + AmbientAmount * Parameters.AmbientRadiance.R;
        Radiance.G += ScatterAmount * SunRadiance.G + AmbientAmount * Parameters.AmbientRadiance.G;
        Radiance.B += ScatterAmount * SunRadiance.B + AmbientAmount * Parameters.AmbientRadiance.B;
        Transmittance *= StepTransmittance;
    }
    Result.Radiance      = Radiance;
    Result.Transmittance = Transmittance;
    return Result;
}

double EvaluateCloudVolumeDensity(const CloudVolumeProfile& Volume, SpatialVector PointMeters) noexcept
{
    const CloudParameters& Parameters = Volume.Slab;
    double Envelope       = 1.0;
    double HeightFraction = 0.5;
    double ScaleMeters    = 780.0;

    if (Volume.Domain == CloudVolumeDomain::PlanetarySlab)
    {
        if (PointMeters.Z < Parameters.LowerHeightM || PointMeters.Z > Parameters.TopHeightM ||
            Parameters.TopHeightM <= Parameters.LowerHeightM)
        {
            return 0.0;
        }
        HeightFraction = (PointMeters.Z - Parameters.LowerHeightM) / (Parameters.TopHeightM - Parameters.LowerHeightM);
        Envelope       = EvaluateTroposphericHeightEnvelope(Volume.HeightProfile, HeightFraction);
    }
    else
    {
        const double Dx = std::abs(PointMeters.X - Volume.BoxCenterM.X);
        const double Dy = std::abs(PointMeters.Y - Volume.BoxCenterM.Y);
        const double Dz = std::abs(PointMeters.Z - Volume.BoxCenterM.Z);
        if (Dx > Volume.BoxHalfExtentsM.X || Dy > Volume.BoxHalfExtentsM.Y || Dz > Volume.BoxHalfExtentsM.Z)
        {
            return 0.0;
        }
        const double Soft = std::max(Volume.BoxEdgeFalloffM, 1.0);
        const double Fx   = SmoothFade((Volume.BoxHalfExtentsM.X - Dx) / Soft);
        const double Fy   = SmoothFade((Volume.BoxHalfExtentsM.Y - Dy) / Soft);
        const double Fz   = SmoothFade((Volume.BoxHalfExtentsM.Z - Dz) / Soft);
        Envelope          = Fx * Fy * Fz;
        HeightFraction    = std::clamp(
            0.5 + 0.5 * (PointMeters.Z - Volume.BoxCenterM.Z) / std::max(Volume.BoxHalfExtentsM.Z, 1.0), 0.0, 1.0);
        ScaleMeters = std::max(
            180.0, 0.85 * std::max({Volume.BoxHalfExtentsM.X, Volume.BoxHalfExtentsM.Y, Volume.BoxHalfExtentsM.Z}));
    }

    if (Parameters.UniformDensityOverride >= 0.0)
    {
        return Parameters.UniformDensityOverride * Parameters.DensityScale * Envelope;
    }

    const SpatialVector Advected{
        (PointMeters.X + Parameters.WindXMps * Parameters.WindTimeS) / ScaleMeters,
        (PointMeters.Y + Parameters.WindYMps * Parameters.WindTimeS) / ScaleMeters,
        PointMeters.Z / (0.55 * ScaleMeters)};
    double MacroShape = RotatedOctaveFbm(Advected, Parameters.Seed);
    if (Volume.WorleyErosionStrength > 0.0)
    {
        const double Worley = WorleyFbm3D({Advected.X * 2.2, Advected.Y * 2.2, Advected.Z * 2.2}, Parameters.Seed ^ 0x55AA11ull);
        const double Eroded = std::clamp(
            MacroShape - Volume.WorleyErosionStrength * (1.0 - Worley) * (0.35 + 0.65 * HeightFraction), 0.0, 1.0);
        MacroShape = Eroded / std::max(1.0 - 0.35 * Volume.WorleyErosionStrength, 0.25);
    }
    const double Coverage = std::clamp(Parameters.Coverage, 0.0, 1.0);
    const double Shaped   = std::clamp((MacroShape - (1.0 - Coverage)) / std::max(Coverage, 1e-3), 0.0, 1.5);
    return Shaped * Envelope * Parameters.DensityScale;
}

double EvaluateBeersPowderEnergy(double τOpticalDepth,
                                 double cosθScattering,
                                 double 𝑔Forward,
                                 double 𝑔Backscatter,
                                 double PowderStrength,
                                 double MultiScatterStrength) noexcept
{
    const double τ      = std::max(0.0, τOpticalDepth);
    const double Beer0  = std::exp(-τ);
    const double Powder = 1.0 - PowderStrength * std::exp(-2.0 * τ) * 0.55;
    const double Phase0 = 0.72 * HenyeyGreensteinPhase(cosθScattering, 𝑔Forward) +
                          0.28 * HenyeyGreensteinPhase(cosθScattering, 𝑔Backscatter);

    const double Beer1  = std::exp(-0.35 * τ);
    const double Phase1 = 0.55 * HenyeyGreensteinPhase(cosθScattering, 0.5 * 𝑔Forward) +
                          0.45 * (1.0 / (4.0 * π));
    return Beer0 * Powder * Phase0 + MultiScatterStrength * Beer1 * Phase1;
}

VolumetricCloudSample MarchCloudVolumes(std::span<const CloudVolumeProfile> Volumes,
                                        SpatialVector                       OriginMeters,
                                        SpatialVector                       DirectionUnit,
                                        std::span<const CloudSolarBeam>     SolarBeams,
                                        const SpectrumRgb&                  SkyAmbientRadiance,
                                        double                              MaxDistanceM,
                                        uint32_t                            Steps,
                                        uint32_t                            ShadowSteps) noexcept
{
    VolumetricCloudSample Result;
    if (Volumes.empty() || Steps == 0 || MaxDistanceM <= 0.0)
    {
        return Result;
    }

    double TGlobalEnter = MaxDistanceM;
    double TGlobalExit  = 0.0;
    bool   AnyHit       = false;
    for (const CloudVolumeProfile& Volume : Volumes)
    {
        double Enter = 0.0;
        double Exit  = 0.0;
        if (Volume.Domain == CloudVolumeDomain::PlanetarySlab)
        {
            if (std::abs(DirectionUnit.Z) < 1e-9)
            {
                if (OriginMeters.Z >= Volume.Slab.LowerHeightM && OriginMeters.Z <= Volume.Slab.TopHeightM)
                {
                    Enter  = 0.0;
                    Exit   = MaxDistanceM;
                    AnyHit = true;
                }
            }
            else
            {
                const double A = (Volume.Slab.LowerHeightM - OriginMeters.Z) / DirectionUnit.Z;
                const double B = (Volume.Slab.TopHeightM - OriginMeters.Z) / DirectionUnit.Z;
                Enter          = std::max(std::min(A, B), 0.0);
                Exit           = std::min(std::max(A, B), MaxDistanceM);
                if (Exit > Enter)
                {
                    AnyHit = true;
                }
            }
        }
        else
        {
            const SpatialVector Lower{Volume.BoxCenterM.X - Volume.BoxHalfExtentsM.X,
                                      Volume.BoxCenterM.Y - Volume.BoxHalfExtentsM.Y,
                                      Volume.BoxCenterM.Z - Volume.BoxHalfExtentsM.Z};
            const SpatialVector Upper{Volume.BoxCenterM.X + Volume.BoxHalfExtentsM.X,
                                      Volume.BoxCenterM.Y + Volume.BoxHalfExtentsM.Y,
                                      Volume.BoxCenterM.Z + Volume.BoxHalfExtentsM.Z};
            if (IntersectRayBounds(OriginMeters, DirectionUnit, Lower, Upper, MaxDistanceM, Enter, Exit))
            {
                AnyHit = true;
            }
        }
        if (Exit > Enter)
        {
            TGlobalEnter = std::min(TGlobalEnter, Enter);
            TGlobalExit  = std::max(TGlobalExit, Exit);
        }
    }

    if (!AnyHit || TGlobalExit <= TGlobalEnter)
    {
        return Result;
    }

    const double Δt            = (TGlobalExit - TGlobalEnter) / static_cast<double>(Steps);
    double       Transmittance = 1.0;
    double       WeightedDist  = 0.0;
    double       WeightSum     = 0.0;
    SpectrumRgb  Radiance{};

    for (uint32_t Step = 0; Step < Steps; ++Step)
    {
        const double        DistM = TGlobalEnter + Δt * (static_cast<double>(Step) + 0.5);
        const SpatialVector At    = Add(OriginMeters, Mul(DirectionUnit, DistM));

        double      σTotal       = 0.0;
        double      𝑔Forward     = 0.35;
        double      𝑔Back        = -0.25;
        double      Powder       = 0.85;
        double      MultiGain    = 0.55;
        SpectrumRgb LocalAmbient = SkyAmbientRadiance;
        double      Dominantσ    = 0.0;

        for (const CloudVolumeProfile& Volume : Volumes)
        {
            const double Density = EvaluateCloudVolumeDensity(Volume, At);
            if (Density <= 0.0)
            {
                continue;
            }
            const double σExt = Volume.Slab.ExtinctionPerM * Density;
            σTotal += σExt;
            if (σExt > Dominantσ)
            {
                Dominantσ    = σExt;
                𝑔Forward     = Volume.Slab.PhaseG;
                𝑔Back        = Volume.BackscatterPhaseG;
                Powder       = Volume.PowderStrength;
                MultiGain    = Volume.MultiScatterStrength;
                LocalAmbient = {SkyAmbientRadiance.R + Volume.Slab.AmbientRadiance.R,
                                SkyAmbientRadiance.G + Volume.Slab.AmbientRadiance.G,
                                SkyAmbientRadiance.B + Volume.Slab.AmbientRadiance.B};
            }
        }

        if (σTotal <= 1e-8)
        {
            continue;
        }

        const double StepT         = std::exp(-σTotal * Δt);
        const double OpacityWeight = Transmittance * (1.0 - StepT);
        WeightedDist += OpacityWeight * DistM;
        WeightSum += OpacityWeight;

        if (Transmittance < 1e-4)
        {
            Transmittance *= StepT;
            continue;
        }

        SpectrumRgb StepScatter = LocalAmbient;
        for (const CloudSolarBeam& Beam : SolarBeams)
        {
            const double BeamLen = std::sqrt(Dot(Beam.Direction, Beam.Direction));
            if (BeamLen <= 1e-9)
            {
                continue;
            }
            const SpatialVector SunDir = Mul(Beam.Direction, 1.0 / BeamLen);
            double              τSolar = 0.0;
            if (ShadowSteps > 0)
            {
                const double ΔsShadow = 140.0;
                for (uint32_t Sub = 0; Sub < ShadowSteps; ++Sub)
                {
                    const SpatialVector ShadowPos = Add(At, Mul(SunDir, ΔsShadow * (static_cast<double>(Sub) + 0.5)));
                    for (const CloudVolumeProfile& Volume : Volumes)
                    {
                        τSolar += Volume.Slab.ExtinctionPerM * EvaluateCloudVolumeDensity(Volume, ShadowPos) * ΔsShadow;
                    }
                }
            }
            const double Energy = EvaluateBeersPowderEnergy(
                τSolar, Dot(DirectionUnit, SunDir), 𝑔Forward, 𝑔Back, Powder, MultiGain);
            StepScatter.R += Beam.Radiance.R * Energy;
            StepScatter.G += Beam.Radiance.G * Energy;
            StepScatter.B += Beam.Radiance.B * Energy;
        }

        Radiance.R += OpacityWeight * StepScatter.R;
        Radiance.G += OpacityWeight * StepScatter.G;
        Radiance.B += OpacityWeight * StepScatter.B;
        Transmittance *= StepT;
    }

    Result.Radiance         = Radiance;
    Result.Transmittance    = Transmittance;
    Result.MeanHitDistanceM = WeightSum > 1e-6 ? (WeightedDist / WeightSum) : 0.0;
    return Result;
}

bool CloudShadowProjection::Bake(std::span<const CloudVolumeProfile> Volumes,
                                 SpatialVector                       AnchorMeters,
                                 SpatialVector                       SolarDirectionUnit,
                                 double                              HalfSpanMeters,
                                 uint32_t                            Resolution,
                                 uint32_t                            MarchSteps)
{
    const double SunLen = std::sqrt(Dot(SolarDirectionUnit, SolarDirectionUnit));
    if (Resolution < 2 || HalfSpanMeters <= 1.0 || MarchSteps == 0 || SunLen <= 1e-9)
    {
        return false;
    }
    Resolution_     = Resolution;
    HalfSpanM_      = HalfSpanMeters;
    AnchorM_        = AnchorMeters;
    SolarDirection_ = Mul(SolarDirectionUnit, 1.0 / SunLen);

    const SpatialVector RefAxis = std::abs(SolarDirection_.Z) < 0.95 ? SpatialVector{0.0, 0.0, 1.0} : SpatialVector{1.0, 0.0, 0.0};
    const SpatialVector CrossU{RefAxis.Y * SolarDirection_.Z - RefAxis.Z * SolarDirection_.Y,
                               RefAxis.Z * SolarDirection_.X - RefAxis.X * SolarDirection_.Z,
                               RefAxis.X * SolarDirection_.Y - RefAxis.Y * SolarDirection_.X};
    const double ULen = std::sqrt(Dot(CrossU, CrossU));
    OrthoAxisU_       = Mul(CrossU, 1.0 / std::max(ULen, 1e-12));
    OrthoAxisV_       = {SolarDirection_.Y * OrthoAxisU_.Z - SolarDirection_.Z * OrthoAxisU_.Y,
                         SolarDirection_.Z * OrthoAxisU_.X - SolarDirection_.X * OrthoAxisU_.Z,
                         SolarDirection_.X * OrthoAxisU_.Y - SolarDirection_.Y * OrthoAxisU_.X};

    OpticalDepthTexels_.assign(static_cast<size_t>(Resolution) * Resolution, 0.0f);
    if (Volumes.empty() || SolarDirection_.Z <= 0.02)
    {
        return true;
    }

    double MaxCloudTop = 2500.0;
    for (const CloudVolumeProfile& Volume : Volumes)
    {
        if (Volume.Domain == CloudVolumeDomain::PlanetarySlab)
        {
            MaxCloudTop = std::max(MaxCloudTop, Volume.Slab.TopHeightM + 100.0);
        }
        else
        {
            MaxCloudTop = std::max(MaxCloudTop, Volume.BoxCenterM.Z + Volume.BoxHalfExtentsM.Z + 100.0);
        }
    }
    const double RaySpanM = std::min(30000.0, MaxCloudTop / std::max(SolarDirection_.Z, 0.08));
    const double Δt       = RaySpanM / static_cast<double>(MarchSteps);

    for (uint32_t Row = 0; Row < Resolution; ++Row)
    {
        const double CoordV = ((static_cast<double>(Row) + 0.5) / static_cast<double>(Resolution) * 2.0 - 1.0) * HalfSpanMeters;
        for (uint32_t Col = 0; Col < Resolution; ++Col)
        {
            const double CoordU = ((static_cast<double>(Col) + 0.5) / static_cast<double>(Resolution) * 2.0 - 1.0) * HalfSpanMeters;
            const SpatialVector OriginPt = Add(AnchorMeters, Add(Mul(OrthoAxisU_, CoordU), Mul(OrthoAxisV_, CoordV)));

            double τAccum = 0.0;
            for (uint32_t Step = 0; Step < MarchSteps; ++Step)
            {
                const SpatialVector SamplePt = Add(OriginPt, Mul(SolarDirection_, Δt * (static_cast<double>(Step) + 0.5)));
                for (const CloudVolumeProfile& Volume : Volumes)
                {
                    τAccum += Volume.Slab.ExtinctionPerM * EvaluateCloudVolumeDensity(Volume, SamplePt) * Δt;
                }
            }
            OpticalDepthTexels_[static_cast<size_t>(Row) * Resolution + Col] = static_cast<float>(τAccum);
        }
    }
    return true;
}

double CloudShadowProjection::SampleOpticalDepth(SpatialVector WorldPointMeters) const noexcept
{
    if (OpticalDepthTexels_.empty() || Resolution_ < 2)
    {
        return 0.0;
    }
    const SpatialVector Rel{WorldPointMeters.X - AnchorM_.X,
                            WorldPointMeters.Y - AnchorM_.Y,
                            WorldPointMeters.Z - AnchorM_.Z};
    const double        AlongSun = Dot(Rel, SolarDirection_);
    const SpatialVector Projected{Rel.X - SolarDirection_.X * AlongSun,
                                  Rel.Y - SolarDirection_.Y * AlongSun,
                                  Rel.Z - SolarDirection_.Z * AlongSun};
    const double CoordU = (Dot(Projected, OrthoAxisU_) / HalfSpanM_) * 0.5 + 0.5;
    const double CoordV = (Dot(Projected, OrthoAxisV_) / HalfSpanM_) * 0.5 + 0.5;
    if (CoordU < 0.0 || CoordU > 1.0 || CoordV < 0.0 || CoordV > 1.0)
    {
        return 0.0;
    }

    const double   Fx = std::clamp(CoordU * static_cast<double>(Resolution_) - 0.5, 0.0, static_cast<double>(Resolution_ - 1));
    const double   Fy = std::clamp(CoordV * static_cast<double>(Resolution_) - 0.5, 0.0, static_cast<double>(Resolution_ - 1));
    const uint32_t X0 = static_cast<uint32_t>(Fx);
    const uint32_t Y0 = static_cast<uint32_t>(Fy);
    const uint32_t X1 = std::min(X0 + 1, Resolution_ - 1);
    const uint32_t Y1 = std::min(Y0 + 1, Resolution_ - 1);
    const double   Tx = Fx - static_cast<double>(X0);
    const double   Ty = Fy - static_cast<double>(Y0);

    const auto At = [&](uint32_t X, uint32_t Y) -> double {
        return static_cast<double>(OpticalDepthTexels_[static_cast<size_t>(Y) * Resolution_ + X]);
    };
    const double Top = At(X0, Y0) + (At(X1, Y0) - At(X0, Y0)) * Tx;
    const double Bot = At(X0, Y1) + (At(X1, Y1) - At(X0, Y1)) * Tx;
    return Top + (Bot - Top) * Ty;
}

double CloudShadowProjection::SampleTransmittance(SpatialVector WorldPointMeters) const noexcept
{
    return std::exp(-SampleOpticalDepth(WorldPointMeters));
}

//------------------------------------------------------------------------------------------------------------------------
//                                           CLOUD SHADER INPUT ENCODING
//------------------------------------------------------------------------------------------------------------------------

std::array<float, CloudRadianceFloatCount> CompileCloudRadiancePayload(
    const CloudRadianceSpecification& Specification)
{
    const CloudParameters& Medium = Specification.Medium;
    std::array<float, CloudRadianceFloatCount> Payload{
        ConvertRadianceFloat(Medium.LowerHeightM),
        ConvertRadianceFloat(Medium.TopHeightM),
        ConvertRadianceFloat(Medium.Coverage),
        ConvertRadianceFloat(Medium.DensityScale),
        ConvertRadianceFloat(Medium.ExtinctionPerM),
        ConvertRadianceFloat(Medium.PhaseG),
        ConvertRadianceFloat(Medium.WindTimeS),
        ConvertRadianceFloat(Medium.UniformDensityOverride),
        ConvertRadianceFloat(Medium.WindXMps),
        ConvertRadianceFloat(Medium.WindYMps),
        ConvertRadianceFloat(Specification.MaximumDistanceM),
        static_cast<float>(Specification.ViewSamples),
        ConvertRadianceFloat(Specification.SolarDirection.X),
        ConvertRadianceFloat(Specification.SolarDirection.Y),
        ConvertRadianceFloat(Specification.SolarDirection.Z),
        static_cast<float>(Specification.SolarShadowSamples),
        ConvertRadianceFloat(Specification.SolarRadiance.R),
        ConvertRadianceFloat(Specification.SolarRadiance.G),
        ConvertRadianceFloat(Specification.SolarRadiance.B),
        ConvertRadianceFloat(Specification.ObserverHeightM),
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        ConvertRadianceFloat(Medium.AmbientRadiance.R),
        ConvertRadianceFloat(Medium.AmbientRadiance.G),
        ConvertRadianceFloat(Medium.AmbientRadiance.B),
        0.0f};
    static_assert(sizeof(float) == sizeof(uint32_t));
    const uint32_t LowBits  = static_cast<uint32_t>(Medium.Seed);
    const uint32_t HighBits = static_cast<uint32_t>(Medium.Seed >> 32u);
    std::memcpy(&Payload[20], &LowBits, sizeof(LowBits));
    std::memcpy(&Payload[21], &HighBits, sizeof(HighBits));
    ValidateCloudRadiancePayload(Payload.data(), Payload.size());
    return Payload;
}

void ValidateCloudRadiancePayload(const float* Payload, std::size_t FloatCount)
{
    if (Payload == nullptr || FloatCount < CloudRadianceFloatCount)
    {
        throw std::runtime_error("Cloud input span is truncated");
    }
    for (std::size_t Index = 0; Index < CloudRadianceFloatCount; ++Index)
    {
        if (Index != 20 && Index != 21 && !std::isfinite(Payload[Index]))
        {
            throw std::runtime_error("Cloud input contains a nonfinite parameter");
        }
    }
    if (!(Payload[1] > Payload[0] && Payload[2] >= 0 && Payload[2] <= 1 &&
          Payload[3] >= 0 && Payload[3] <= 16 && Payload[4] >= 0 && Payload[4] <= 0.1f &&
          std::abs(Payload[5]) < 0.99f && std::abs(Payload[6]) <= 1000000.0f && Payload[7] >= -1.0f &&
          Payload[7] <= 16.0f && std::abs(Payload[8]) <= 100.0f && std::abs(Payload[9]) <= 100.0f &&
          Payload[10] >= 0.0f && Payload[10] <= 100000.0f && Payload[11] >= 0.0f && Payload[11] <= 128.0f &&
          std::floor(Payload[11]) == Payload[11] && Payload[15] >= 0.0f && Payload[15] <= 8.0f &&
          std::floor(Payload[15]) == Payload[15]))
    {
        throw std::runtime_error("Cloud parameters exceed the bounded shader domain");
    }
    const double DirectionLength = Payload[12] * Payload[12] + Payload[13] * Payload[13] + Payload[14] * Payload[14];
    if (std::abs(DirectionLength - 1.0) >= 0.001)
    {
        throw std::runtime_error("Cloud solar direction is not unit length");
    }
    for (std::size_t Index : {16u, 17u, 18u, 24u, 25u, 26u})
    {
        if (Payload[Index] < 0.0f)
        {
            throw std::runtime_error("Cloud incident radiance is negative");
        }
    }
}

} // namespace Frontier
