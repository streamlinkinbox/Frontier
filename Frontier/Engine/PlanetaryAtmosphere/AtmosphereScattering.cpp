/*==========================================================================================================================================
                                               ATMOSPHERE SCATTERING DEFINITION
==========================================================================================================================================*/
// 🧩 Physical Rayleigh/Mie/Ozone scattering, multi-sun LUTs, aerial perspective, and 6-point Gauss-Legendre NEE.

#include "Engine/PlanetaryAtmosphere/AtmosphereScattering.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Frontier
{
namespace
{

constexpr double π = 3.14159265358979323846;

SpatialVector Add(SpatialVector A, SpatialVector B) noexcept { return {A.X + B.X, A.Y + B.Y, A.Z + B.Z}; }
SpatialVector Mul(SpatialVector A, double Scalar) noexcept { return {A.X * Scalar, A.Y * Scalar, A.Z * Scalar}; }
double        Dot(SpatialVector A, SpatialVector B) noexcept { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
double        Length(SpatialVector A) noexcept { return std::sqrt(Dot(A, A)); }

SpatialVector Cross(SpatialVector A, SpatialVector B) noexcept
{
    return {A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X};
}

SpatialVector NormalizeChecked(SpatialVector Vector)
{
    const double Norm = Length(Vector);
    if (!std::isfinite(Norm) || Norm <= 1e-9)
    {
        throw std::invalid_argument("Atmosphere/solar vector must be finite and non-zero");
    }
    return {Vector.X / Norm, Vector.Y / Norm, Vector.Z / Norm};
}

void ConstructOrthonormalAxes(SpatialVector Axis, SpatialVector& Tangent, SpatialVector& Bitangent) noexcept
{
    const SpatialVector Reference = std::abs(Axis.Z) < 0.999 ? SpatialVector{0.0, 0.0, 1.0} : SpatialVector{1.0, 0.0, 0.0};
    Tangent                       = Cross(Reference, Axis);
    const double TangentNorm      = Length(Tangent);
    Tangent   = TangentNorm > 1e-12 ? SpatialVector{Tangent.X / TangentNorm, Tangent.Y / TangentNorm, Tangent.Z / TangentNorm}
                                    : SpatialVector{1.0, 0.0, 0.0};
    Bitangent = Cross(Axis, Tangent);
}

void MapToCanonicalPlanetSpace(SpatialVector  LocalUp,
                               SpatialVector  Direction,
                               SpatialVector  SolarDirection,
                               double         RadiusKm,
                               SpatialVector& CanonicalPositionKm,
                               SpatialVector& CanonicalDirection,
                               SpatialVector& CanonicalSolarDirection) noexcept
{
    SpatialVector East{}, North{};
    ConstructOrthonormalAxes(LocalUp, East, North);
    CanonicalPositionKm     = {0.0, RadiusKm, 0.0};
    CanonicalDirection      = {Dot(Direction, East), Dot(Direction, LocalUp), Dot(Direction, North)};
    CanonicalSolarDirection = {Dot(SolarDirection, East), Dot(SolarDirection, LocalUp), Dot(SolarDirection, North)};
}

SpectrumRgb ExpNeg(const SpectrumRgb& τ) noexcept
{
    return {std::exp(-τ.R), std::exp(-τ.G), std::exp(-τ.B)};
}

SpectrumRgb AddRgb(SpectrumRgb A, SpectrumRgb B) noexcept { return {A.R + B.R, A.G + B.G, A.B + B.B}; }
SpectrumRgb MulRgb(SpectrumRgb A, double Scalar) noexcept { return {A.R * Scalar, A.G * Scalar, A.B * Scalar}; }
SpectrumRgb MulRgb(SpectrumRgb A, SpectrumRgb B) noexcept { return {A.R * B.R, A.G * B.G, A.B * B.B}; }

double SegmentMean(double σΔs, double τSegment, double TransmittanceSegment) noexcept
{
    return (τSegment > 1.0e-6) ? (σΔs * (1.0 - TransmittanceSegment) / τSegment) : σΔs;
}

double RaySphereAhead(SpatialVector PositionKm, SpatialVector Direction, double RadiusKm) noexcept
{
    const double HalfB = Dot(PositionKm, Direction);
    const double TermC = Dot(PositionKm, PositionKm) - RadiusKm * RadiusKm;
    const double Δ     = HalfB * HalfB - TermC;
    if (Δ < 0.0)
    {
        return -1.0;
    }
    const double Root = std::sqrt(Δ);
    const double Near = -HalfB - Root;
    return Near >= 0.0 ? Near : (-HalfB + Root >= 0.0 ? -HalfB + Root : -1.0);
}

SpectrumRgb OpticalDepthBetween(const AtmosphereParameters& Parameters,
                                SpatialVector               PointAKm,
                                SpatialVector               PointBKm,
                                uint32_t                    Steps) noexcept
{
    const SpatialVector SpanKm{PointBKm.X - PointAKm.X, PointBKm.Y - PointAKm.Y, PointBKm.Z - PointAKm.Z};
    const double        LengthKm = Length(SpanKm);
    if (LengthKm <= 0.0 || Steps == 0)
    {
        return {};
    }
    double       τRayleigh = 0.0, τMie = 0.0, τOzone = 0.0;
    const double Δs       = LengthKm / static_cast<double>(Steps);
    double       PriorR   = 0.0, PriorM = 0.0, PriorO = 0.0;
    for (uint32_t StepIndex = 0; StepIndex <= Steps; ++StepIndex)
    {
        const double        Fraction   = static_cast<double>(StepIndex) / static_cast<double>(Steps);
        const SpatialVector SampleKm{PointAKm.X + SpanKm.X * Fraction,
                                     PointAKm.Y + SpanKm.Y * Fraction,
                                     PointAKm.Z + SpanKm.Z * Fraction};
        const double        AltitudeKm = std::max(Length(SampleKm) - Parameters.GroundRadiusKm, 0.0);
        const double        ρRayleigh  = RayleighDensity(Parameters, AltitudeKm);
        const double        ρMie       = MieDensity(Parameters, AltitudeKm);
        const double        ρOzone     = OzoneDensity(Parameters, AltitudeKm);
        if (StepIndex > 0)
        {
            τRayleigh += 0.5 * (PriorR + ρRayleigh) * Δs;
            τMie      += 0.5 * (PriorM + ρMie) * Δs;
            τOzone    += 0.5 * (PriorO + ρOzone) * Δs;
        }
        PriorR = ρRayleigh;
        PriorM = ρMie;
        PriorO = ρOzone;
    }
    const double σMieExtinction = Parameters.MieScattering + Parameters.MieAbsorption;
    return {Parameters.RayleighScattering.R * τRayleigh + σMieExtinction * τMie + Parameters.OzoneAbsorption.R * τOzone,
            Parameters.RayleighScattering.G * τRayleigh + σMieExtinction * τMie + Parameters.OzoneAbsorption.G * τOzone,
            Parameters.RayleighScattering.B * τRayleigh + σMieExtinction * τMie + Parameters.OzoneAbsorption.B * τOzone};
}

SpectrumRgb WarpedOpticalDepth6Pt(const AtmosphereParameters& Parameters, SpatialVector StartKm, SpatialVector EndKm) noexcept
{
    const SpatialVector SpanKm{EndKm.X - StartKm.X, EndKm.Y - StartKm.Y, EndKm.Z - StartKm.Z};
    const double        LengthKm = Length(SpanKm);
    if (LengthKm <= 0.0)
    {
        return {};
    }
    static constexpr double Nodes[6]   = {0.03376524, 0.16939531, 0.38069041, 0.61930959, 0.83060469, 0.96623476};
    static constexpr double Weights[6] = {0.08566225, 0.18038079, 0.23395697, 0.23395697, 0.18038079, 0.08566225};
    double                  τRayleigh = 0.0, τMie = 0.0, τOzone = 0.0;
    for (int Tap = 0; Tap < 6; ++Tap)
    {
        const double        Fraction = Nodes[Tap] * Nodes[Tap];
        const double        Δs       = (2.0 * Nodes[Tap] * Weights[Tap]) * LengthKm;
        const SpatialVector SampleKm{StartKm.X + SpanKm.X * Fraction,
                                     StartKm.Y + SpanKm.Y * Fraction,
                                     StartKm.Z + SpanKm.Z * Fraction};
        const double        AltitudeKm = std::max(Length(SampleKm) - Parameters.GroundRadiusKm, 0.0);
        τRayleigh += RayleighDensity(Parameters, AltitudeKm) * Δs;
        τMie      += MieDensity(Parameters, AltitudeKm) * Δs;
        τOzone    += OzoneDensity(Parameters, AltitudeKm) * Δs;
    }
    const double σMieExtinction = Parameters.MieScattering + Parameters.MieAbsorption;
    return {Parameters.RayleighScattering.R * τRayleigh + σMieExtinction * τMie + Parameters.OzoneAbsorption.R * τOzone,
            Parameters.RayleighScattering.G * τRayleigh + σMieExtinction * τMie + Parameters.OzoneAbsorption.G * τOzone,
            Parameters.RayleighScattering.B * τRayleigh + σMieExtinction * τMie + Parameters.OzoneAbsorption.B * τOzone};
}

SpectrumRgb WarpedTransmittanceToTop6Pt(const AtmosphereParameters& Parameters, double RadiusKm, double μ) noexcept
{
    const double        Clampedμ = std::clamp(μ, -1.0, 1.0);
    const double        sinθ     = std::sqrt(std::max(1.0 - Clampedμ * Clampedμ, 0.0));
    const SpatialVector PositionKm{0.0, RadiusKm, 0.0};
    const SpatialVector Direction{0.0, Clampedμ, sinθ};
    const double        ToGround = RaySphereAhead(PositionKm, Direction, Parameters.GroundRadiusKm);
    const double        ToTop    = RaySphereAhead(PositionKm, Direction, Parameters.TopRadiusKm);
    double              LengthKm = ToTop;
    if (ToGround > 1e-4 && (LengthKm < 0.0 || ToGround < LengthKm))
    {
        LengthKm = ToGround;
    }
    if (LengthKm < 0.0)
    {
        return {1.0, 1.0, 1.0};
    }
    const SpatialVector EndKm = Add(PositionKm, Mul(Direction, LengthKm));
    return ExpNeg(WarpedOpticalDepth6Pt(Parameters, PositionKm, EndKm));
}

double AltitudeToTexCoordV(const AtmosphereParameters& Parameters, double RadiusKm) noexcept
{
    const double NormalizedAltitude =
        std::clamp((RadiusKm - Parameters.GroundRadiusKm) / (Parameters.TopRadiusKm - Parameters.GroundRadiusKm), 0.0, 1.0);
    return std::sqrt(NormalizedAltitude);
}
double TexCoordVToRadiusKm(const AtmosphereParameters& Parameters, double TexCoordV) noexcept
{
    return Parameters.GroundRadiusKm + TexCoordV * TexCoordV * (Parameters.TopRadiusKm - Parameters.GroundRadiusKm);
}
double ZenithCosineToTexCoordU(double μ) noexcept
{
    return 0.5 + 0.5 * std::copysign(std::pow(std::abs(std::clamp(μ, -1.0, 1.0)), 0.35), μ);
}
double TexCoordUToZenithCosine(double TexCoordU) noexcept
{
    const double SignedU = 2.0 * std::clamp(TexCoordU, 0.0, 1.0) - 1.0;
    return std::copysign(std::pow(std::abs(SignedU), 1.0 / 0.35), SignedU);
}

double SmoothStepScalar(double Edge0, double Edge1, double Scalar) noexcept
{
    if (Edge1 <= Edge0)
    {
        return Scalar >= Edge1 ? 1.0 : 0.0;
    }
    const double T = std::clamp((Scalar - Edge0) / (Edge1 - Edge0), 0.0, 1.0);
    return T * T * (3.0 - 2.0 * T);
}

double EvaluateCloudPattern(double TangentialU, double TangentialV, double Coverage) noexcept
{
    if (Coverage <= 0.0)
    {
        return 0.0;
    }
    const double WaveA     = 0.5 + 0.5 * std::sin(TangentialU * 2.10 + 0.45) * std::cos(TangentialV * 1.85 - 0.30);
    const double WaveB     = 0.5 + 0.5 * std::sin((TangentialU * 0.72 - TangentialV * 0.68) * 4.30 + 1.15);
    const double Envelope  = 0.65 * WaveA + 0.35 * WaveB;
    const double Threshold = 1.0 - std::clamp(Coverage, 0.0, 1.0);
    return SmoothStepScalar(Threshold, std::min(1.0, Threshold + 0.28), Envelope);
}

void RequireDomain(bool Condition, const char* Message)
{
    if (!Condition)
    {
        throw std::invalid_argument(Message);
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    MEDIUM & PHASE LAWS
//------------------------------------------------------------------------------------------------------------------------

double RayleighDensity(const AtmosphereParameters& Parameters, double AltitudeKm) noexcept
{
    return std::exp(-std::max(AltitudeKm, 0.0) / Parameters.RayleighScaleHeightKm);
}
double MieDensity(const AtmosphereParameters& Parameters, double AltitudeKm) noexcept
{
    return std::exp(-std::max(AltitudeKm, 0.0) / Parameters.MieScaleHeightKm);
}
double OzoneDensity(const AtmosphereParameters& Parameters, double AltitudeKm) noexcept
{
    return std::max(1.0 - std::abs(AltitudeKm - Parameters.OzonePeakKm) / Parameters.OzoneWidthKm, 0.0);
}

double RayleighPhase(double cosθ) noexcept
{
    return 3.0 / (16.0 * π) * (1.0 + cosθ * cosθ);
}
double HenyeyGreensteinPhase(double cosθ, double 𝑔) noexcept
{
    const double Denominator = 1.0 + 𝑔 * 𝑔 - 2.0 * 𝑔 * cosθ;
    return (1.0 - 𝑔 * 𝑔) / (4.0 * π * Denominator * std::sqrt(std::max(Denominator, 1e-12)));
}

double CornetteShanksPhase(double cosθ, double 𝑔) noexcept
{
    const double ClampedCos = std::clamp(cosθ, -1.0, 1.0);
    const double Asymmetry  = std::clamp(𝑔, -0.999, 0.999);
    return HenyeyGreensteinPhase(ClampedCos, Asymmetry) *
           1.5 * (1.0 + ClampedCos * ClampedCos) / (2.0 + Asymmetry * Asymmetry);
}

SpectrumRgb TransmittanceBetween(const AtmosphereParameters& Parameters,
                                 SpatialVector               PointAKm,
                                 SpatialVector               PointBKm,
                                 uint32_t                    Steps) noexcept
{
    return ExpNeg(OpticalDepthBetween(Parameters, PointAKm, PointBKm, Steps));
}

SpectrumRgb TransmittanceToTop(const AtmosphereParameters& Parameters,
                               double                      RadiusKm,
                               double                      μ,
                               uint32_t                    Steps) noexcept
{
    const double        sinθ = std::sqrt(std::max(1.0 - μ * μ, 0.0));
    const SpatialVector PositionKm{0.0, RadiusKm, 0.0};
    const SpatialVector Direction{0.0, μ, sinθ};

    const double ToGround = RaySphereAhead(PositionKm, Direction, Parameters.GroundRadiusKm);
    const double ToTop    = RaySphereAhead(PositionKm, Direction, Parameters.TopRadiusKm);
    double       LengthKm = ToTop;
    if (ToGround > 1e-4 && (LengthKm < 0.0 || ToGround < LengthKm))
    {
        LengthKm = ToGround;
    }
    if (LengthKm < 0.0)
    {
        return {1.0, 1.0, 1.0};
    }
    const SpatialVector EndKm = Add(PositionKm, Mul(Direction, LengthKm));
    return ExpNeg(OpticalDepthBetween(Parameters, PositionKm, EndKm, Steps));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TRANSMITTANCE LUT
//------------------------------------------------------------------------------------------------------------------------

bool TransmittanceLut::Bake(const AtmosphereParameters& Parameters,
                            uint32_t                    WidthMu,
                            uint32_t                    HeightAltitude,
                            uint32_t                    IntegrationSteps)
{
    Texels_.clear();
    if (WidthMu < 2 || HeightAltitude < 2)
    {
        WidthMu_ = HeightAltitude_ = 0;
        return false;
    }
    WidthMu_        = WidthMu;
    HeightAltitude_ = HeightAltitude;
    Texels_.resize(static_cast<size_t>(WidthMu) * HeightAltitude);
    for (uint32_t Row = 0; Row < HeightAltitude; ++Row)
    {
        const double TexCoordV = static_cast<double>(Row) / static_cast<double>(HeightAltitude - 1);
        const double RadiusKm  = TexCoordVToRadiusKm(Parameters, TexCoordV);
        for (uint32_t Col = 0; Col < WidthMu; ++Col)
        {
            const double TexCoordU = static_cast<double>(Col) / static_cast<double>(WidthMu - 1);
            const double μ         = TexCoordUToZenithCosine(TexCoordU);
            Texels_[static_cast<size_t>(Row) * WidthMu + Col] =
                TransmittanceToTop(Parameters, RadiusKm, μ, IntegrationSteps);
        }
    }
    return true;
}

SpectrumRgb TransmittanceLut::Sample(const AtmosphereParameters& Parameters, double RadiusKm, double μ) const noexcept
{
    if (Texels_.empty())
    {
        return TransmittanceToTop(Parameters, RadiusKm, μ);
    }
    const double   ScaledU = ZenithCosineToTexCoordU(μ) * static_cast<double>(WidthMu_ - 1);
    const double   ScaledV = AltitudeToTexCoordV(Parameters, RadiusKm) * static_cast<double>(HeightAltitude_ - 1);
    const uint32_t X0      = std::min(static_cast<uint32_t>(ScaledU), WidthMu_ - 2);
    const uint32_t Y0      = std::min(static_cast<uint32_t>(ScaledV), HeightAltitude_ - 2);
    const double   FracX   = ScaledU - X0;
    const double   FracY   = ScaledV - Y0;
    const auto     At      = [&](uint32_t X, uint32_t Y) { return Texels_[static_cast<size_t>(Y) * WidthMu_ + X]; };
    const SpectrumRgb Row0 = AddRgb(MulRgb(At(X0, Y0), 1.0 - FracX), MulRgb(At(X0 + 1, Y0), FracX));
    const SpectrumRgb Row1 = AddRgb(MulRgb(At(X0, Y0 + 1), 1.0 - FracX), MulRgb(At(X0 + 1, Y0 + 1), FracX));
    return AddRgb(MulRgb(Row0, 1.0 - FracY), MulRgb(Row1, FracY));
}

//------------------------------------------------------------------------------------------------------------------------
//                                        SINGLE & MULTIPLE SCATTERING INTEGRATORS
//------------------------------------------------------------------------------------------------------------------------

SpectrumRgb SingleScattering(const AtmosphereParameters& Parameters,
                             SpatialVector               PositionKm,
                             SpatialVector               Direction,
                             SpatialVector               SunDirection,
                             const SpectrumRgb&          SunIrradiance,
                             const TransmittanceLut*     Lut,
                             uint32_t                    Steps) noexcept
{
    const double ToGround = RaySphereAhead(PositionKm, Direction, Parameters.GroundRadiusKm);
    const double ToTop    = RaySphereAhead(PositionKm, Direction, Parameters.TopRadiusKm);
    double       LengthKm = ToTop;
    if (ToGround > 1e-4 && (LengthKm < 0.0 || ToGround < LengthKm))
    {
        LengthKm = ToGround;
    }
    if (LengthKm <= 0.0 || Steps == 0)
    {
        return {};
    }
    const double cosθ          = Dot(Direction, SunDirection);
    const double PhaseRayleigh = RayleighPhase(cosθ);
    const double PhaseMie      = CornetteShanksPhase(cosθ, Parameters.MiePhaseG);
    const double Δs            = LengthKm / static_cast<double>(Steps);

    SpectrumRgb Radiance{};
    SpectrumRgb ViewTransmittance{1.0, 1.0, 1.0};
    for (uint32_t StepIndex = 0; StepIndex < Steps; ++StepIndex)
    {
        const double        StartS = Δs * static_cast<double>(StepIndex);
        const double        EndS   = Δs * static_cast<double>(StepIndex + 1);
        const SpatialVector Point0 = Add(PositionKm, Mul(Direction, StartS));
        const SpatialVector Point1 = Add(PositionKm, Mul(Direction, EndS));
        const SpectrumRgb   Δτ     = OpticalDepthBetween(Parameters, Point0, Point1, 1);
        const SpectrumRgb   SegT   = ExpNeg(Δτ);

        const SpatialVector MidKm       = Add(PositionKm, Mul(Direction, 0.5 * (StartS + EndS)));
        const double        MidRadiusKm = Length(MidKm);
        const double        AltitudeKm  = std::max(MidRadiusKm - Parameters.GroundRadiusKm, 0.0);
        const double        μSolar      = Dot(Mul(MidKm, 1.0 / std::max(MidRadiusKm, 1e-9)), SunDirection);
        const double        SunGround   = RaySphereAhead(MidKm, SunDirection, Parameters.GroundRadiusKm);
        if (SunGround < 0.0 || SunGround <= 1e-4)
        {
            const SpectrumRgb SolarT    = Lut != nullptr ? Lut->Sample(Parameters, MidRadiusKm, μSolar)
                                                         : TransmittanceToTop(Parameters, MidRadiusKm, μSolar, 24);
            const double      ρRayleigh = RayleighDensity(Parameters, AltitudeKm);
            const double      ρMie      = MieDensity(Parameters, AltitudeKm);
            const auto        RatioMean = [](double τComponent, double τTotal, double SegmentTransmittance) {
                return τTotal <= 1e-12 ? 0.0 : (τComponent / τTotal) * (1.0 - SegmentTransmittance);
            };
            const double σRayleighR = Parameters.RayleighScattering.R * ρRayleigh * Δs;
            const double σRayleighG = Parameters.RayleighScattering.G * ρRayleigh * Δs;
            const double σRayleighB = Parameters.RayleighScattering.B * ρRayleigh * Δs;
            const double σMieStep   = Parameters.MieScattering * ρMie * Δs;
            const SpectrumRgb Scatter{
                PhaseRayleigh * RatioMean(σRayleighR, Δτ.R, SegT.R) + PhaseMie * RatioMean(σMieStep, Δτ.R, SegT.R),
                PhaseRayleigh * RatioMean(σRayleighG, Δτ.G, SegT.G) + PhaseMie * RatioMean(σMieStep, Δτ.G, SegT.G),
                PhaseRayleigh * RatioMean(σRayleighB, Δτ.B, SegT.B) + PhaseMie * RatioMean(σMieStep, Δτ.B, SegT.B)};
            Radiance = AddRgb(Radiance, MulRgb(MulRgb(MulRgb(ViewTransmittance, SolarT), SunIrradiance), Scatter));
        }
        ViewTransmittance = MulRgb(ViewTransmittance, SegT);
    }
    return Radiance;
}

SpectrumRgb MultipleScatteringApprox(const AtmosphereParameters& Parameters,
                                     SpatialVector               PositionKm,
                                     SpatialVector               Direction,
                                     SpatialVector               SunDirection,
                                     const SpectrumRgb&          SingleScattered,
                                     const SpectrumRgb&          SunIrradiance) noexcept
{
    (void)Direction;
    (void)SunIrradiance;
    const double        RadiusKm = Length(PositionKm);
    const SpatialVector ZenithUp = Mul(PositionKm, 1.0 / std::max(RadiusKm, 1e-9));
    double              SumR = 0.0, SumG = 0.0, SumB = 0.0;
    for (uint32_t Ring = 0; Ring < 4; ++Ring)
    {
        const double      μ    = -0.75 + 0.5 * static_cast<double>(Ring);
        const SpectrumRgb TopT = TransmittanceToTop(Parameters, RadiusKm, μ, 24);
        SumR += TopT.R;
        SumG += TopT.G;
        SumB += TopT.B;
    }
    const SpectrumRgb MeanT{SumR * 0.25, SumG * 0.25, SumB * 0.25};
    const double      μSolar      = Dot(ZenithUp, SunDirection);
    const double      SunGround   = RaySphereAhead(PositionKm, SunDirection, Parameters.GroundRadiusKm);
    const bool        SunVisible  = SunGround < 0.0 || SunGround <= 1e-4;
    const SpectrumRgb SolarT      = SunVisible ? TransmittanceToTop(Parameters, RadiusKm, μSolar, 24) : SpectrumRgb{};
    const double      AltitudeKm  = std::max(RadiusKm - Parameters.GroundRadiusKm, 0.0);
    const double      AirAlbedo   = std::clamp(0.6 * RayleighDensity(Parameters, AltitudeKm) + 0.1, 0.0, 0.9);
    const auto        SeriesBoost = [](double MeanTrans, double SunTrans, double ReturnAlbedo, double GroundAlbedo) {
        const double ψ = std::clamp(MeanTrans * ReturnAlbedo * 0.35 + GroundAlbedo * 0.06, 0.0, 0.55);
        return std::clamp(ψ / (1.0 - ψ), 0.0, 1.2) * SunTrans;
    };
    return MulRgb(SingleScattered,
                  {SeriesBoost(MeanT.R, SolarT.R, AirAlbedo, Parameters.GroundAlbedo),
                   SeriesBoost(MeanT.G, SolarT.G, AirAlbedo, Parameters.GroundAlbedo),
                   SeriesBoost(MeanT.B, SolarT.B, AirAlbedo, Parameters.GroundAlbedo)});
}

//------------------------------------------------------------------------------------------------------------------------
//                                      MULTI-SCATTERING, SKY-VIEW & AERIAL PERSPECTIVE
//------------------------------------------------------------------------------------------------------------------------

bool MultiScatteringLut::Bake(const AtmosphereParameters& Parameters,
                              const TransmittanceLut&     Transmittance,
                              uint32_t                    WidthSunCos,
                              uint32_t                    HeightAltitude,
                              uint32_t                    DirectionCount)
{
    if (WidthSunCos < 2 || HeightAltitude < 2 || DirectionCount == 0 || Parameters.TopRadiusKm <= Parameters.GroundRadiusKm)
    {
        return false;
    }
    WidthSunCos_    = WidthSunCos;
    HeightAltitude_ = HeightAltitude;
    Texels_.assign(static_cast<size_t>(WidthSunCos) * HeightAltitude, SpectrumRgb{});

    const double       SpanKm       = Parameters.TopRadiusKm - Parameters.GroundRadiusKm;
    const uint32_t     AzimuthRings = std::max(4u, DirectionCount);
    constexpr uint32_t MarchSteps   = 16;

    for (uint32_t Row = 0; Row < HeightAltitude; ++Row)
    {
        const double        TexCoordV = (static_cast<double>(Row) + 0.5) / static_cast<double>(HeightAltitude);
        const double        RadiusKm  = Parameters.GroundRadiusKm + std::clamp(TexCoordV * TexCoordV * SpanKm, 1e-3, SpanKm - 1e-3);
        const SpatialVector PositionKm{0.0, RadiusKm, 0.0};

        for (uint32_t Col = 0; Col < WidthSunCos; ++Col)
        {
            const double        TexCoordU  = (static_cast<double>(Col) + 0.5) / static_cast<double>(WidthSunCos);
            const double        μSolar     = std::clamp(2.0 * TexCoordU - 1.0, -1.0, 1.0);
            const double        sinθSolar  = std::sqrt(std::max(0.0, 1.0 - μSolar * μSolar));
            const SpatialVector SolarDirection{sinθSolar, μSolar, 0.0};

            SpectrumRgb  SecondOrderLuminance{};
            SpectrumRgb  TransferFraction{};
            const double ΔΩ             = (4.0 * π) / static_cast<double>(AzimuthRings);
            const double IsotropicPhase = 1.0 / (4.0 * π);

            for (uint32_t RayIndex = 0; RayIndex < AzimuthRings; ++RayIndex)
            {
                const double        μ      = -1.0 + 2.0 * (static_cast<double>(RayIndex) + 0.5) / static_cast<double>(AzimuthRings);
                const double        sinθ   = std::sqrt(std::max(0.0, 1.0 - μ * μ));
                const double        ϕ      = 2.399963229728653 * static_cast<double>(RayIndex);
                const SpatialVector RayDirection{sinθ * std::cos(ϕ), μ, sinθ * std::sin(ϕ)};

                const double ToGround   = RaySphereAhead(PositionKm, RayDirection, Parameters.GroundRadiusKm);
                const double ToTop      = RaySphereAhead(PositionKm, RayDirection, Parameters.TopRadiusKm);
                double       RayLength  = ToTop;
                const bool   HitsGround = (ToGround > 0.0 && (RayLength < 0.0 || ToGround < RayLength));
                if (HitsGround)
                {
                    RayLength = ToGround;
                }
                if (RayLength <= 0.0)
                {
                    continue;
                }

                const double Δs = RayLength / static_cast<double>(MarchSteps);
                SpectrumRgb  ViewTransmittance{1.0, 1.0, 1.0};
                SpectrumRgb  RayInscatter{};
                SpectrumRgb  RayTransfer{};

                for (uint32_t Step = 0; Step < MarchSteps; ++Step)
                {
                    const SpatialVector Point0 = Add(PositionKm, Mul(RayDirection, Δs * static_cast<double>(Step)));
                    const SpatialVector Point1 = Add(PositionKm, Mul(RayDirection, Δs * static_cast<double>(Step + 1)));
                    const SpatialVector MidKm  = Mul(Add(Point0, Point1), 0.5);
                    const SpectrumRgb   Δτ     = OpticalDepthBetween(Parameters, Point0, Point1, 2);
                    const SpectrumRgb   SegT   = ExpNeg(Δτ);

                    const double      MidRadiusKm = Length(MidKm);
                    const double      AltitudeKm  = std::max(MidRadiusKm - Parameters.GroundRadiusKm, 0.0);
                    const double      ρRayleigh   = RayleighDensity(Parameters, AltitudeKm);
                    const double      ρMie        = MieDensity(Parameters, AltitudeKm);
                    const SpectrumRgb σScatter{
                        Parameters.RayleighScattering.R * ρRayleigh + Parameters.MieScattering * ρMie,
                        Parameters.RayleighScattering.G * ρRayleigh + Parameters.MieScattering * ρMie,
                        Parameters.RayleighScattering.B * ρRayleigh + Parameters.MieScattering * ρMie};
                    const SpectrumRgb ScatterIntegral{
                        SegmentMean(σScatter.R * Δs, Δτ.R, SegT.R),
                        SegmentMean(σScatter.G * Δs, Δτ.G, SegT.G),
                        SegmentMean(σScatter.B * Δs, Δτ.B, SegT.B)};

                    RayTransfer = AddRgb(RayTransfer, MulRgb(ViewTransmittance, MulRgb(ScatterIntegral, IsotropicPhase)));

                    const double SunGround = RaySphereAhead(MidKm, SolarDirection, Parameters.GroundRadiusKm);
                    if (SunGround < 0.0 || SunGround <= 1e-4)
                    {
                        const double      μLocalSun = Dot(Mul(MidKm, 1.0 / std::max(MidRadiusKm, 1e-9)), SolarDirection);
                        const SpectrumRgb SolarT    = Transmittance.Baked()
                                                          ? Transmittance.Sample(Parameters, MidRadiusKm, μLocalSun)
                                                          : TransmittanceToTop(Parameters, MidRadiusKm, μLocalSun, 16);
                        const double      cosγ      = Dot(RayDirection, SolarDirection);
                        const double      PhaseR    = RayleighPhase(cosγ);
                        const double      PhaseM    = CornetteShanksPhase(cosγ, Parameters.MiePhaseG);
                        const SpectrumRgb PhaseWeighted{
                            SegmentMean((Parameters.RayleighScattering.R * ρRayleigh * PhaseR +
                                         Parameters.MieScattering * ρMie * PhaseM) * Δs,
                                        Δτ.R, SegT.R),
                            SegmentMean((Parameters.RayleighScattering.G * ρRayleigh * PhaseR +
                                         Parameters.MieScattering * ρMie * PhaseM) * Δs,
                                        Δτ.G, SegT.G),
                            SegmentMean((Parameters.RayleighScattering.B * ρRayleigh * PhaseR +
                                         Parameters.MieScattering * ρMie * PhaseM) * Δs,
                                        Δτ.B, SegT.B)};
                        RayInscatter = AddRgb(RayInscatter, MulRgb(MulRgb(ViewTransmittance, SolarT), PhaseWeighted));
                    }
                    ViewTransmittance = MulRgb(ViewTransmittance, SegT);
                }

                if (HitsGround && Parameters.GroundAlbedo > 0.0)
                {
                    const SpatialVector GroundKm     = Add(PositionKm, Mul(RayDirection, RayLength));
                    const SpatialVector GroundNormal = Mul(GroundKm, 1.0 / std::max(Length(GroundKm), 1e-9));
                    const double        μGroundSun   = Dot(GroundNormal, SolarDirection);
                    if (μGroundSun > 0.0)
                    {
                        const SpectrumRgb GroundSunT = Transmittance.Baked()
                                                           ? Transmittance.Sample(Parameters, Parameters.GroundRadiusKm + 1e-3, μGroundSun)
                                                           : TransmittanceToTop(Parameters, Parameters.GroundRadiusKm + 1e-3, μGroundSun, 16);
                        const double LambertBrdf = (Parameters.GroundAlbedo / π) * μGroundSun;
                        RayInscatter             = AddRgb(RayInscatter, MulRgb(MulRgb(ViewTransmittance, GroundSunT), LambertBrdf));
                    }
                }

                SecondOrderLuminance = AddRgb(SecondOrderLuminance, MulRgb(RayInscatter, ΔΩ * IsotropicPhase));
                TransferFraction     = AddRgb(TransferFraction, MulRgb(RayTransfer, ΔΩ));
            }

            const auto SolveGeometricSeries = [](double SecondOrder, double Transfer) {
                return std::max(0.0, SecondOrder / std::max(1.0 - std::clamp(Transfer, 0.0, 0.92), 0.08));
            };
            Texels_[static_cast<size_t>(Row) * WidthSunCos + Col] = {
                SolveGeometricSeries(SecondOrderLuminance.R, TransferFraction.R),
                SolveGeometricSeries(SecondOrderLuminance.G, TransferFraction.G),
                SolveGeometricSeries(SecondOrderLuminance.B, TransferFraction.B)};
        }
    }
    return true;
}

SpectrumRgb MultiScatteringLut::Sample(const AtmosphereParameters& Parameters, double RadiusKm, double μSolar) const noexcept
{
    if (Texels_.empty())
    {
        return {};
    }
    const double   SpanKm            = std::max(Parameters.TopRadiusKm - Parameters.GroundRadiusKm, 1e-6);
    const double   NormalizedHeight  = std::clamp((RadiusKm - Parameters.GroundRadiusKm) / SpanKm, 0.0, 1.0);
    const double   TexCoordV         = std::sqrt(NormalizedHeight);
    const double   TexCoordU         = std::clamp(0.5 + 0.5 * μSolar, 0.0, 1.0);
    const double   ScaledX           = TexCoordU * static_cast<double>(WidthSunCos_ - 1);
    const double   ScaledY           = TexCoordV * static_cast<double>(HeightAltitude_ - 1);
    const uint32_t X0                = static_cast<uint32_t>(ScaledX);
    const uint32_t Y0                = static_cast<uint32_t>(ScaledY);
    const uint32_t X1                = std::min(X0 + 1, WidthSunCos_ - 1);
    const uint32_t Y1                = std::min(Y0 + 1, HeightAltitude_ - 1);
    const double   FracX             = ScaledX - static_cast<double>(X0);
    const double   FracY             = ScaledY - static_cast<double>(Y0);

    const auto At = [&](uint32_t X, uint32_t Y) -> const SpectrumRgb& {
        return Texels_[static_cast<size_t>(Y) * WidthSunCos_ + X];
    };
    const auto Lerp = [](const SpectrumRgb& A, const SpectrumRgb& B, double T) -> SpectrumRgb {
        return {A.R + (B.R - A.R) * T, A.G + (B.G - A.G) * T, A.B + (B.B - A.B) * T};
    };
    return Lerp(Lerp(At(X0, Y0), At(X1, Y0), FracX), Lerp(At(X0, Y1), At(X1, Y1), FracX), FracY);
}

SpectrumRgb EvaluateTwilightArchRadiance(SpatialVector      LocalDirectionUnit,
                                         SpatialVector      LocalSunDirectionUnit,
                                         const SpectrumRgb& SunIrradiance,
                                         double             TwilightStrength) noexcept
{
    if (TwilightStrength <= 0.0)
    {
        return {};
    }
    const double ViewNorm = Length(LocalDirectionUnit);
    const double SunNorm  = Length(LocalSunDirectionUnit);
    if (ViewNorm <= 1e-9 || SunNorm <= 1e-9)
    {
        return {};
    }
    const SpatialVector ViewUnit  = Mul(LocalDirectionUnit, 1.0 / ViewNorm);
    const SpatialVector SolarUnit = Mul(LocalSunDirectionUnit, 1.0 / SunNorm);

    const double SolarElevationDeg = std::asin(std::clamp(SolarUnit.Z, -1.0, 1.0)) * (180.0 / π);
    if (SolarElevationDeg > 6.5 || SolarElevationDeg < -18.5)
    {
        return {};
    }
    const double RisingWindow  = std::clamp((SolarElevationDeg + 18.0) / 8.0, 0.0, 1.0);
    const double SettingWindow = std::clamp((6.0 - SolarElevationDeg) / 7.5, 0.0, 1.0);
    const double TwilightGate  = RisingWindow * SettingWindow * TwilightStrength;
    if (TwilightGate <= 1e-5 || std::max(ViewUnit.Z, -0.04) < -0.03)
    {
        return {};
    }

    const double LimbBand        = std::exp(-std::pow(std::max(ViewUnit.Z, 0.0) / 0.19, 1.45));
    const double AzimuthAlignment =
        (ViewUnit.X * SolarUnit.X + ViewUnit.Y * SolarUnit.Y) /
        std::max(1e-4, std::hypot(ViewUnit.X, ViewUnit.Y) * std::hypot(SolarUnit.X, SolarUnit.Y));
    const double SolarAzimuthPeak = std::pow(0.5 + 0.5 * std::clamp(AzimuthAlignment, -1.0, 1.0), 2.8);
    const double AntiSolarBand    = std::pow(0.5 - 0.5 * std::clamp(AzimuthAlignment, -1.0, 1.0), 2.2) *
                                    std::exp(-std::pow((ViewUnit.Z - 0.11) / 0.12, 2.0));

    const SpectrumRgb WarmArch{0.022 * SolarAzimuthPeak * LimbBand,
                               0.0085 * SolarAzimuthPeak * LimbBand,
                               0.0024 * SolarAzimuthPeak * LimbBand};
    const SpectrumRgb BeltOfVenus{0.0042 * AntiSolarBand, 0.0026 * AntiSolarBand, 0.0058 * AntiSolarBand};
    return MulRgb(SunIrradiance, MulRgb(AddRgb(WarmArch, BeltOfVenus), TwilightGate));
}

bool SkyViewLut::Bake(const AtmosphereParameters&            Parameters,
                      const TransmittanceLut&                Transmittance,
                      const MultiScatteringLut&              MultiScatter,
                      double                                 ObserverAltitudeKm,
                      std::span<const AtmosphereSolarSource> Suns,
                      uint32_t                               WidthAzimuth,
                      uint32_t                               HeightElevation,
                      uint32_t                               MarchSteps)
{
    if (WidthAzimuth < 4 || HeightElevation < 4 || MarchSteps == 0 || Parameters.TopRadiusKm <= Parameters.GroundRadiusKm)
    {
        return false;
    }
    WidthAzimuth_    = WidthAzimuth;
    HeightElevation_ = HeightElevation;
    Texels_.assign(static_cast<size_t>(WidthAzimuth) * HeightElevation, SpectrumRgb{});

    const double        SafeAltKm = std::clamp(ObserverAltitudeKm, 1e-3, (Parameters.TopRadiusKm - Parameters.GroundRadiusKm) - 1e-3);
    const double        RadiusKm  = Parameters.GroundRadiusKm + SafeAltKm;
    const SpatialVector CanonicalPositionKm{0.0, RadiusKm, 0.0};

    for (uint32_t Row = 0; Row < HeightElevation; ++Row)
    {
        const double TexCoordV = (static_cast<double>(Row) + 0.5) / static_cast<double>(HeightElevation);
        const double SignedV   = 2.0 * TexCoordV - 1.0;
        const double θElev     = (SignedV >= 0.0 ? 1.0 : -1.0) * (SignedV * SignedV) * (0.5 * π);
        const double sinθ      = std::sin(θElev);
        const double cosθ      = std::cos(θElev);

        for (uint32_t Col = 0; Col < WidthAzimuth; ++Col)
        {
            const double        ϕAzimuth = 2.0 * π * (static_cast<double>(Col) + 0.5) / static_cast<double>(WidthAzimuth);
            const SpatialVector LocalDirection{cosθ * std::sin(ϕAzimuth), cosθ * std::cos(ϕAzimuth), sinθ};
            const SpatialVector CanonicalDirection{LocalDirection.X, LocalDirection.Z, LocalDirection.Y};

            SpectrumRgb AccumulatedSky{};
            for (const AtmosphereSolarSource& Sun : Suns)
            {
                const double SunNorm = Length(Sun.Direction);
                if (SunNorm <= 1e-9)
                {
                    continue;
                }
                const SpatialVector LocalSun     = Mul(Sun.Direction, 1.0 / SunNorm);
                const SpatialVector CanonicalSun = {LocalSun.X, LocalSun.Z, LocalSun.Y};

                const SpectrumRgb Single = SingleScattering(
                    Parameters, CanonicalPositionKm, CanonicalDirection, CanonicalSun, Sun.Irradiance,
                    Transmittance.Baked() ? &Transmittance : nullptr, MarchSteps);

                SpectrumRgb Multiple{};
                if (MultiScatter.Baked())
                {
                    const SpectrumRgb Ψ          = MultiScatter.Sample(Parameters, RadiusKm, LocalSun.Z);
                    const double      ρRayleigh  = RayleighDensity(Parameters, SafeAltKm);
                    const double      ρMie       = MieDensity(Parameters, SafeAltKm);
                    const double      PathFactor = std::min(35.0, 8.0 / std::max(0.18, std::abs(LocalDirection.Z) + 0.08));
                    const SpectrumRgb ScatterColumn{
                        (Parameters.RayleighScattering.R * ρRayleigh + Parameters.MieScattering * ρMie) * PathFactor,
                        (Parameters.RayleighScattering.G * ρRayleigh + Parameters.MieScattering * ρMie) * PathFactor,
                        (Parameters.RayleighScattering.B * ρRayleigh + Parameters.MieScattering * ρMie) * PathFactor};
                    Multiple = MulRgb(MulRgb(Ψ, ScatterColumn), Sun.Irradiance);
                }
                else
                {
                    Multiple = MultipleScatteringApprox(
                        Parameters, CanonicalPositionKm, CanonicalDirection, CanonicalSun, Single, Sun.Irradiance);
                }

                const SpectrumRgb Twilight =
                    EvaluateTwilightArchRadiance(LocalDirection, LocalSun, Sun.Irradiance, Sun.TwilightStrength);
                AccumulatedSky = AddRgb(AccumulatedSky, AddRgb(AddRgb(Single, Multiple), Twilight));
            }
            Texels_[static_cast<size_t>(Row) * WidthAzimuth + Col] = AccumulatedSky;
        }
    }
    return true;
}

SpectrumRgb SkyViewLut::Sample(SpatialVector LocalDirectionUnit) const noexcept
{
    if (Texels_.empty())
    {
        return {};
    }
    const double Norm = Length(LocalDirectionUnit);
    if (Norm <= 1e-9)
    {
        return {};
    }
    const SpatialVector Direction = Mul(LocalDirectionUnit, 1.0 / Norm);
    const double        θElev     = std::asin(std::clamp(Direction.Z, -1.0, 1.0));
    double              ϕAzimuth  = std::atan2(Direction.X, Direction.Y);
    if (ϕAzimuth < 0.0)
    {
        ϕAzimuth += 2.0 * π;
    }

    const double NormalizedElev = θElev / (0.5 * π);
    const double TexCoordV      = std::clamp(
        0.5 + 0.5 * (NormalizedElev >= 0.0 ? 1.0 : -1.0) * std::sqrt(std::abs(NormalizedElev)), 0.0, 1.0);
    const double TexCoordU      = std::clamp(ϕAzimuth / (2.0 * π), 0.0, 1.0);

    const double   ScaledX = TexCoordU * static_cast<double>(WidthAzimuth_);
    const double   ScaledY = std::clamp(TexCoordV * static_cast<double>(HeightElevation_) - 0.5,
                                        0.0, static_cast<double>(HeightElevation_ - 1));
    const uint32_t X0      = static_cast<uint32_t>(std::floor(ScaledX)) % WidthAzimuth_;
    const uint32_t X1      = (X0 + 1) % WidthAzimuth_;
    const uint32_t Y0      = static_cast<uint32_t>(ScaledY);
    const uint32_t Y1      = std::min(Y0 + 1, HeightElevation_ - 1);
    const double   FracX   = ScaledX - std::floor(ScaledX);
    const double   FracY   = ScaledY - static_cast<double>(Y0);

    const auto At = [&](uint32_t X, uint32_t Y) -> const SpectrumRgb& {
        return Texels_[static_cast<size_t>(Y) * WidthAzimuth_ + X];
    };
    const auto Lerp = [](const SpectrumRgb& A, const SpectrumRgb& B, double T) -> SpectrumRgb {
        return {A.R + (B.R - A.R) * T, A.G + (B.G - A.G) * T, A.B + (B.B - A.B) * T};
    };
    return Lerp(Lerp(At(X0, Y0), At(X1, Y0), FracX), Lerp(At(X0, Y1), At(X1, Y1), FracX), FracY);
}

AerialPerspectiveSample EvaluateAerialPerspective(
    const AtmosphereParameters&            Parameters,
    const TransmittanceLut*                Transmittance,
    const MultiScatteringLut*              MultiScatter,
    double                                 ObserverAltitudeKm,
    SpatialVector                          OriginLocalMeters,
    SpatialVector                          DirectionLocalUnit,
    double                                 SurfaceDistanceMeters,
    std::span<const AtmosphereSolarSource> Suns,
    uint32_t                               Steps) noexcept
{
    AerialPerspectiveSample Result;
    const double            DirNorm = Length(DirectionLocalUnit);
    if (SurfaceDistanceMeters <= 0.0 || Steps == 0 || DirNorm <= 1e-9)
    {
        return Result;
    }
    const SpatialVector LocalDir     = Mul(DirectionLocalUnit, 1.0 / DirNorm);
    const SpatialVector CanonicalDir = {LocalDir.X, LocalDir.Z, LocalDir.Y};
    const SpatialVector StartKm{
        1e-3 * OriginLocalMeters.X,
        Parameters.GroundRadiusKm + std::max(1e-3, ObserverAltitudeKm + 1e-3 * OriginLocalMeters.Z),
        1e-3 * OriginLocalMeters.Y};
    const double LengthKm = 1e-3 * SurfaceDistanceMeters;
    const double Δs       = LengthKm / static_cast<double>(Steps);

    SpectrumRgb ViewTransmittance{1.0, 1.0, 1.0};
    SpectrumRgb Inscatter{};

    for (uint32_t Step = 0; Step < Steps; ++Step)
    {
        const SpatialVector Point0 = Add(StartKm, Mul(CanonicalDir, Δs * static_cast<double>(Step)));
        const SpatialVector Point1 = Add(StartKm, Mul(CanonicalDir, Δs * static_cast<double>(Step + 1)));
        const SpatialVector MidKm  = Mul(Add(Point0, Point1), 0.5);
        const SpectrumRgb   Δτ     = OpticalDepthBetween(Parameters, Point0, Point1, 2);
        const SpectrumRgb   SegT   = ExpNeg(Δτ);

        const double MidRadiusKm = Length(MidKm);
        const double AltitudeKm  = std::max(MidRadiusKm - Parameters.GroundRadiusKm, 0.0);
        const double ρRayleigh   = RayleighDensity(Parameters, AltitudeKm);
        const double ρMie        = MieDensity(Parameters, AltitudeKm);

        for (const AtmosphereSolarSource& Sun : Suns)
        {
            const double SunNorm = Length(Sun.Direction);
            if (SunNorm <= 1e-9)
            {
                continue;
            }
            const SpatialVector LocalSun     = Mul(Sun.Direction, 1.0 / SunNorm);
            const SpatialVector CanonicalSun = {LocalSun.X, LocalSun.Z, LocalSun.Y};
            const double        SunGround    = RaySphereAhead(MidKm, CanonicalSun, Parameters.GroundRadiusKm);
            if (SunGround >= 0.0 && SunGround > 1e-4)
            {
                continue;
            }
            const double      μSolar = Dot(Mul(MidKm, 1.0 / std::max(MidRadiusKm, 1e-9)), CanonicalSun);
            const SpectrumRgb SolarT = (Transmittance && Transmittance->Baked())
                                           ? Transmittance->Sample(Parameters, MidRadiusKm, μSolar)
                                           : TransmittanceToTop(Parameters, MidRadiusKm, μSolar, 16);
            const double      cosγ   = Dot(CanonicalDir, CanonicalSun);
            const double      PhaseR = RayleighPhase(cosγ);
            const double      PhaseM = CornetteShanksPhase(cosγ, Parameters.MiePhaseG);
            const SpectrumRgb Scatter{
                PhaseR * SegmentMean(Parameters.RayleighScattering.R * ρRayleigh * Δs, Δτ.R, SegT.R) +
                    PhaseM * SegmentMean(Parameters.MieScattering * ρMie * Δs, Δτ.R, SegT.R),
                PhaseR * SegmentMean(Parameters.RayleighScattering.G * ρRayleigh * Δs, Δτ.G, SegT.G) +
                    PhaseM * SegmentMean(Parameters.MieScattering * ρMie * Δs, Δτ.G, SegT.G),
                PhaseR * SegmentMean(Parameters.RayleighScattering.B * ρRayleigh * Δs, Δτ.B, SegT.B) +
                    PhaseM * SegmentMean(Parameters.MieScattering * ρMie * Δs, Δτ.B, SegT.B)};

            SpectrumRgb DirectTerm = MulRgb(MulRgb(SolarT, Sun.Irradiance), Scatter);
            if (MultiScatter && MultiScatter->Baked())
            {
                const SpectrumRgb Ψ = MultiScatter->Sample(Parameters, MidRadiusKm, μSolar);
                const SpectrumRgb MultiStep{
                    (Parameters.RayleighScattering.R * ρRayleigh + Parameters.MieScattering * ρMie) * Δs * Ψ.R * Sun.Irradiance.R,
                    (Parameters.RayleighScattering.G * ρRayleigh + Parameters.MieScattering * ρMie) * Δs * Ψ.G * Sun.Irradiance.G,
                    (Parameters.RayleighScattering.B * ρRayleigh + Parameters.MieScattering * ρMie) * Δs * Ψ.B * Sun.Irradiance.B};
                DirectTerm = AddRgb(DirectTerm, MultiStep);
            }
            Inscatter = AddRgb(Inscatter, MulRgb(ViewTransmittance, DirectTerm));
        }
        ViewTransmittance = MulRgb(ViewTransmittance, SegT);
    }

    Result.Inscatter     = Inscatter;
    Result.Transmittance = ViewTransmittance;
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                          COUPLED SOLAR/SKY PAYLOAD & 6-POINT GAUSS-LEGENDRE QUADRATURE
//------------------------------------------------------------------------------------------------------------------------

std::array<float, SolarSkyPayloadFloats> CompileSolarSkyPayload(const SolarSkySpecification& Specification,
                                                                std::size_t                  SourceIndex)
{
    RequireDomain(!Specification.SolarSources.empty() && SourceIndex < Specification.SolarSources.size(),
                  "Solar/sky specification must contain the requested solar source index");
    const SolarSkySourceProfile& Source  = Specification.SolarSources[SourceIndex];
    const SpatialVector          SunUnit = NormalizeChecked(Source.SolarDirection);
    const SpatialVector          LocalUp = NormalizeChecked(Specification.LocalUpDirection);

    RequireDomain(std::isfinite(Source.SolarAngularRadius) && Source.SolarAngularRadius >= 5e-4 && Source.SolarAngularRadius <= 0.35,
                  "Solar angular radius lies outside [0.0005, 0.35] radians");
    RequireDomain(std::isfinite(Specification.ObserverAltitudeKm) && Specification.ObserverAltitudeKm >= 0.0 &&
                      Specification.ObserverAltitudeKm <= 120.0,
                  "Observer altitude lies outside [0, 120] km");
    RequireDomain(std::isfinite(Source.SolarIrradiance.R) && Source.SolarIrradiance.R >= 0.0 &&
                      std::isfinite(Source.SolarIrradiance.G) && Source.SolarIrradiance.G >= 0.0 &&
                      std::isfinite(Source.SolarIrradiance.B) && Source.SolarIrradiance.B >= 0.0,
                  "Solar top-of-atmosphere irradiance must be finite and non-negative");
    RequireDomain(std::isfinite(Source.LimbDarkeningU1) && std::isfinite(Source.LimbDarkeningU2) && Source.LimbDarkeningU1 >= 0.0 &&
                      Source.LimbDarkeningU2 >= 0.0 && Source.LimbDarkeningU1 + Source.LimbDarkeningU2 <= 0.98,
                  "Solar limb-darkening coefficients violate positivity on the disc limb");
    RequireDomain(std::isfinite(Specification.MultipleScatterScale) && Specification.MultipleScatterScale >= 0.0 &&
                      Specification.MultipleScatterScale <= 4.0,
                  "Multiple-scattering scale lies outside [0, 4]");
    RequireDomain(std::isfinite(Specification.CloudBottomKm) && std::isfinite(Specification.CloudTopKm) &&
                      Specification.CloudBottomKm >= 0.05 && Specification.CloudTopKm > Specification.CloudBottomKm &&
                      Specification.CloudTopKm <= 50.0,
                  "Cloud slab altitude bounds are invalid");
    RequireDomain(std::isfinite(Specification.CloudExtinctionKm) && Specification.CloudExtinctionKm >= 0.0 &&
                      Specification.CloudExtinctionKm <= 50.0 && std::isfinite(Specification.CloudCoverage) &&
                      Specification.CloudCoverage >= 0.0 && Specification.CloudCoverage <= 1.0,
                  "Cloud slab optical parameters are invalid");

    std::array<float, SolarSkyPayloadFloats> Payload{};
    Payload[0]  = static_cast<float>(SunUnit.X);
    Payload[1]  = static_cast<float>(SunUnit.Y);
    Payload[2]  = static_cast<float>(SunUnit.Z);
    Payload[3]  = static_cast<float>(Source.SolarAngularRadius);
    Payload[4]  = static_cast<float>(LocalUp.X);
    Payload[5]  = static_cast<float>(LocalUp.Y);
    Payload[6]  = static_cast<float>(LocalUp.Z);
    Payload[7]  = static_cast<float>(Specification.ObserverAltitudeKm);
    Payload[8]  = static_cast<float>(Source.SolarIrradiance.R);
    Payload[9]  = static_cast<float>(Source.SolarIrradiance.G);
    Payload[10] = static_cast<float>(Source.SolarIrradiance.B);
    Payload[11] = static_cast<float>(Specification.MultipleScatterScale);
    Payload[12] = static_cast<float>(Source.LimbDarkeningU1);
    Payload[13] = static_cast<float>(Source.LimbDarkeningU2);
    Payload[14] = static_cast<float>(std::clamp(Specification.Atmosphere.GroundAlbedo, 0.0, 1.0));
    Payload[15] = static_cast<float>(std::clamp(Specification.Atmosphere.MiePhaseG, -0.99, 0.99));
    Payload[16] = static_cast<float>(Specification.CloudBottomKm);
    Payload[17] = static_cast<float>(Specification.CloudTopKm);
    Payload[18] = static_cast<float>(Specification.CloudExtinctionKm);
    Payload[19] = static_cast<float>(Specification.CloudCoverage);
    return Payload;
}

void ValidateSolarSkyPayload(std::span<const float> Payload)
{
    RequireDomain(Payload.size() >= SolarSkyPayloadFloats, "Solar/sky payload requires twenty floats");
    for (std::uint32_t Index = 0; Index < SolarSkyPayloadFloats; ++Index)
    {
        RequireDomain(std::isfinite(Payload[Index]), "Solar/sky payload contains non-finite float");
    }
    const double SunNorm = std::sqrt(double(Payload[0]) * Payload[0] + double(Payload[1]) * Payload[1] + double(Payload[2]) * Payload[2]);
    const double UpNorm  = std::sqrt(double(Payload[4]) * Payload[4] + double(Payload[5]) * Payload[5] + double(Payload[6]) * Payload[6]);
    RequireDomain(std::abs(SunNorm - 1.0) <= 1e-3 && std::abs(UpNorm - 1.0) <= 1e-3, "Solar/sky unit axes are not normalized");
    RequireDomain(Payload[3] >= 5e-4f && Payload[3] <= 0.35f, "Solar/sky angular radius is out of bounds");
    RequireDomain(Payload[7] >= 0.0f && Payload[7] <= 120.0f, "Solar/sky observer altitude is out of bounds");
    RequireDomain(Payload[8] >= 0.0f && Payload[9] >= 0.0f && Payload[10] >= 0.0f && Payload[11] >= 0.0f && Payload[11] <= 4.0f,
                  "Solar/sky irradiance or multiple-scatter scale is out of bounds");
    RequireDomain(Payload[12] >= 0.0f && Payload[13] >= 0.0f && (Payload[12] + Payload[13]) <= 0.985f &&
                      Payload[14] >= 0.0f && Payload[14] <= 1.0f && std::abs(Payload[15]) < 0.995f,
                  "Solar/sky limb-darkening or phase parameters are out of bounds");
    RequireDomain(Payload[16] >= 0.05f && Payload[17] > Payload[16] && Payload[17] <= 50.0f &&
                      Payload[18] >= 0.0f && Payload[18] <= 50.0f && Payload[19] >= 0.0f && Payload[19] <= 1.0f,
                  "Solar/sky cloud slab parameters are out of bounds");
}

double EvaluateSolarSkyCloudTransmittance(const SolarSkySpecification& Specification,
                                          SpatialVector                OriginMeters,
                                          SpatialVector                Direction) noexcept
{
    if (Specification.CloudExtinctionKm <= 0.0 || Specification.CloudCoverage <= 0.0)
    {
        return 1.0;
    }
    const double UpNorm  = Length(Specification.LocalUpDirection);
    const double DirNorm = Length(Direction);
    if (UpNorm <= 1e-9 || DirNorm <= 1e-9)
    {
        return 1.0;
    }
    const SpatialVector LocalUp = Mul(Specification.LocalUpDirection, 1.0 / UpNorm);
    const SpatialVector UnitDir = Mul(Direction, 1.0 / DirNorm);
    const double        μUp     = Dot(UnitDir, LocalUp);
    if (μUp <= 1e-3)
    {
        return 1.0;
    }

    const double OriginAltKm = Specification.ObserverAltitudeKm + 1e-3 * Dot(OriginMeters, LocalUp);
    if (OriginAltKm >= Specification.CloudTopKm)
    {
        return 1.0;
    }
    const double EntryAltKm  = std::max(Specification.CloudBottomKm, OriginAltKm);
    const double EntryDistKm = (EntryAltKm - OriginAltKm) / μUp;
    const double ExitDistKm  = (Specification.CloudTopKm - OriginAltKm) / μUp;
    const double SegmentKm   = std::max(0.0, ExitDistKm - EntryDistKm);
    if (SegmentKm <= 0.0)
    {
        return 1.0;
    }

    SpatialVector East{}, North{};
    ConstructOrthonormalAxes(LocalUp, East, North);
    const SpatialVector OriginKm{1e-3 * OriginMeters.X, 1e-3 * OriginMeters.Y, 1e-3 * OriginMeters.Z};

    static constexpr double Nodes[3]   = {0.11270167, 0.50000000, 0.88729833};
    static constexpr double Weights[3] = {0.27777778, 0.44444444, 0.27777778};
    double                  τCloud     = 0.0;
    for (int Tap = 0; Tap < 3; ++Tap)
    {
        const double        DistKm   = EntryDistKm + Nodes[Tap] * SegmentKm;
        const SpatialVector SampleKm = Add(OriginKm, Mul(UnitDir, DistKm));
        const double        AltKm    = OriginAltKm + DistKm * μUp;
        const double        HeightT  = std::clamp(
            (AltKm - Specification.CloudBottomKm) / std::max(Specification.CloudTopKm - Specification.CloudBottomKm, 1e-4), 0.0, 1.0);
        const double VerticalEnvelope = 4.0 * HeightT * (1.0 - HeightT);
        const double TangentialField  = EvaluateCloudPattern(Dot(SampleKm, East), Dot(SampleKm, North), Specification.CloudCoverage);
        τCloud += Specification.CloudExtinctionKm * VerticalEnvelope * TangentialField * (SegmentKm * Weights[Tap]);
    }
    return std::exp(-τCloud);
}

SpectrumRgb EvaluateSolarSkyOracleContinuum(const SolarSkySpecification& Specification,
                                            SpatialVector                OriginMeters,
                                            SpatialVector                Direction,
                                            std::uint32_t                Steps) noexcept
{
    const double UpNorm  = Length(Specification.LocalUpDirection);
    const double DirNorm = Length(Direction);
    if (UpNorm <= 1e-9 || DirNorm <= 1e-9 || Specification.SolarSources.empty())
    {
        return {};
    }
    const SpatialVector LocalUp    = Mul(Specification.LocalUpDirection, 1.0 / UpNorm);
    const SpatialVector UnitDir    = Mul(Direction, 1.0 / DirNorm);
    const double        LocalAltKm = std::max(0.0, Specification.ObserverAltitudeKm + 1e-3 * Dot(OriginMeters, LocalUp));
    const double        RadiusKm   = Specification.Atmosphere.GroundRadiusKm + std::max(LocalAltKm, 1e-3);
    const double        CloudT     = EvaluateSolarSkyCloudTransmittance(Specification, OriginMeters, UnitDir);

    TransmittanceLut HighPrecisionTrans{};
    (void)HighPrecisionTrans.Bake(Specification.Atmosphere, 256u, 64u, std::max(Steps, 96u));

    SpectrumRgb Total{};
    for (const SolarSkySourceProfile& Source : Specification.SolarSources)
    {
        const double SunNorm = Length(Source.SolarDirection);
        if (SunNorm <= 1e-9)
        {
            continue;
        }
        const SpatialVector UnitSun = Mul(Source.SolarDirection, 1.0 / SunNorm);
        SpatialVector       CanonicalObs{}, CanonicalDir{}, CanonicalSun{};
        MapToCanonicalPlanetSpace(LocalUp, UnitDir, UnitSun, RadiusKm, CanonicalObs, CanonicalDir, CanonicalSun);

        const SpectrumRgb Single = SingleScattering(
            Specification.Atmosphere, CanonicalObs, CanonicalDir, CanonicalSun, Source.SolarIrradiance, &HighPrecisionTrans, Steps);
        const SpectrumRgb Multiple = MultipleScatteringApprox(
            Specification.Atmosphere, CanonicalObs, CanonicalDir, CanonicalSun, Single, Source.SolarIrradiance);

        const double      CloudSunlit    = std::max(0.0, Dot(UnitSun, LocalUp));
        const double      SunGround      = RaySphereAhead(CanonicalObs, CanonicalSun, Specification.Atmosphere.GroundRadiusKm);
        const SpectrumRgb SunTop         = (SunGround < 0.0 || SunGround <= 1e-4)
                                               ? TransmittanceToTop(Specification.Atmosphere, RadiusKm, Dot(UnitSun, LocalUp), 64u)
                                               : SpectrumRgb{};
        const double      CloudInscatter = (1.0 - CloudT) * (0.18 + 0.52 * CloudSunlit) / π;

        const SpectrumRgb ClearSky{Single.R + Specification.MultipleScatterScale * Multiple.R,
                                   Single.G + Specification.MultipleScatterScale * Multiple.G,
                                   Single.B + Specification.MultipleScatterScale * Multiple.B};
        Total.R += ClearSky.R * CloudT + Source.SolarIrradiance.R * SunTop.R * CloudInscatter;
        Total.G += ClearSky.G * CloudT + Source.SolarIrradiance.G * SunTop.G * CloudInscatter;
        Total.B += ClearSky.B * CloudT + Source.SolarIrradiance.B * SunTop.B * CloudInscatter;
    }
    return Total;
}

SpectrumRgb EvaluateSolarSkyGaussLegendre6Pt(const SolarSkySpecification& Specification,
                                             SpatialVector                OriginMeters,
                                             SpatialVector                Direction) noexcept
{
    const double UpNorm  = Length(Specification.LocalUpDirection);
    const double DirNorm = Length(Direction);
    if (UpNorm <= 1e-9 || DirNorm <= 1e-9 || Specification.SolarSources.empty())
    {
        return {};
    }
    const AtmosphereParameters& Parameters = Specification.Atmosphere;
    const SpatialVector         LocalUp    = Mul(Specification.LocalUpDirection, 1.0 / UpNorm);
    const SpatialVector         UnitDir    = Mul(Direction, 1.0 / DirNorm);
    const double                LocalAltKm = std::max(0.0, Specification.ObserverAltitudeKm + 1e-3 * Dot(OriginMeters, LocalUp));
    const double                RadiusKm   = Parameters.GroundRadiusKm + std::max(LocalAltKm, 1e-3);
    const double                CloudT     = EvaluateSolarSkyCloudTransmittance(Specification, OriginMeters, UnitDir);

    TransmittanceLut TransLut{};
    (void)TransLut.Bake(Parameters, kTransmittanceLutWidth, kTransmittanceLutHeight, 48u);

    SpectrumRgb Total{};
    for (const SolarSkySourceProfile& Source : Specification.SolarSources)
    {
        const double SunNorm = Length(Source.SolarDirection);
        if (SunNorm <= 1e-9)
        {
            continue;
        }
        const SpatialVector UnitSun = Mul(Source.SolarDirection, 1.0 / SunNorm);
        SpatialVector       CanonicalPos{}, CanonicalDir{}, CanonicalSun{};
        MapToCanonicalPlanetSpace(LocalUp, UnitDir, UnitSun, RadiusKm, CanonicalPos, CanonicalDir, CanonicalSun);

        const double ToGround = RaySphereAhead(CanonicalPos, CanonicalDir, Parameters.GroundRadiusKm);
        const double ToTop    = RaySphereAhead(CanonicalPos, CanonicalDir, Parameters.TopRadiusKm);
        double       LengthKm = ToTop;
        if (ToGround > 1e-4 && (LengthKm < 0.0 || ToGround < LengthKm))
        {
            LengthKm = ToGround;
        }

        SpectrumRgb Single{};
        if (LengthKm > 0.0)
        {
            const double cosγ          = Dot(CanonicalDir, CanonicalSun);
            const double PhaseRayleigh = RayleighPhase(cosγ);
            const double PhaseMie      = CornetteShanksPhase(cosγ, Parameters.MiePhaseG);
            SpectrumRgb  ViewT{1.0, 1.0, 1.0};

            double LitStartKm       = 0.0;
            const auto IsSunBlocked = [&](double DistKm) noexcept -> bool {
                const SpatialVector Pt = Add(CanonicalPos, Mul(CanonicalDir, DistKm));
                return RaySphereAhead(Pt, CanonicalSun, Parameters.GroundRadiusKm) > 1e-4;
            };
            if (IsSunBlocked(0.0))
            {
                if (IsSunBlocked(LengthKm))
                {
                    LitStartKm = LengthKm;
                }
                else
                {
                    double LowerKm = 0.0, UpperKm = LengthKm;
                    for (int Bisect = 0; Bisect < 12; ++Bisect)
                    {
                        const double MidKm = 0.5 * (LowerKm + UpperKm);
                        if (IsSunBlocked(MidKm))
                        {
                            LowerKm = MidKm;
                        }
                        else
                        {
                            UpperKm = MidKm;
                        }
                    }
                    LitStartKm = UpperKm;
                    ViewT      = ExpNeg(WarpedOpticalDepth6Pt(Parameters, CanonicalPos, Add(CanonicalPos, Mul(CanonicalDir, LitStartKm))));
                }
            }

            const double ActiveSpanKm = std::max(0.0, LengthKm - LitStartKm);
            if (ActiveSpanKm > 1e-5)
            {
                static constexpr double Nodes[6]   = {0.03376524, 0.16939531, 0.38069041, 0.61930959, 0.83060469, 0.96623476};
                static constexpr double Weights[6] = {0.08566225, 0.18038079, 0.23395697, 0.23395697, 0.18038079, 0.08566225};
                SpatialVector           PriorPt    = Add(CanonicalPos, Mul(CanonicalDir, LitStartKm));
                const double            WarpPower  = (LitStartKm > 0.5) ? 1.0 : 2.22;

                for (int Tap = 0; Tap < 6; ++Tap)
                {
                    const double        NodeU   = Nodes[Tap];
                    const double        WarpedU = std::pow(NodeU, WarpPower);
                    const double        DistKm  = LitStartKm + ActiveSpanKm * WarpedU;
                    const double        Δs      = ActiveSpanKm * (WarpPower * std::pow(NodeU, WarpPower - 1.0) * Weights[Tap]);
                    const SpatialVector SamplePt = Add(CanonicalPos, Mul(CanonicalDir, DistKm));
                    const SpectrumRgb   SegT    = ExpNeg(WarpedOpticalDepth6Pt(Parameters, PriorPt, SamplePt));
                    ViewT                       = MulRgb(ViewT, SegT);
                    PriorPt                     = SamplePt;

                    const double SunGround = RaySphereAhead(SamplePt, CanonicalSun, Parameters.GroundRadiusKm);
                    if (SunGround < 0.0 || SunGround <= 1e-4)
                    {
                        const double      𝑟         = Length(SamplePt);
                        const double      AltitudeKm = std::max(𝑟 - Parameters.GroundRadiusKm, 0.0);
                        const double      μSolar     = Dot(Mul(SamplePt, 1.0 / 𝑟), CanonicalSun);
                        const SpectrumRgb SolarT     = TransLut.Sample(Parameters, 𝑟, μSolar);
                        const double      ρRayleigh  = RayleighDensity(Parameters, AltitudeKm);
                        const double      ρMie       = MieDensity(Parameters, AltitudeKm);
                        const SpectrumRgb σPhase{
                            (PhaseRayleigh * Parameters.RayleighScattering.R * ρRayleigh + PhaseMie * Parameters.MieScattering * ρMie) * Δs,
                            (PhaseRayleigh * Parameters.RayleighScattering.G * ρRayleigh + PhaseMie * Parameters.MieScattering * ρMie) * Δs,
                            (PhaseRayleigh * Parameters.RayleighScattering.B * ρRayleigh + PhaseMie * Parameters.MieScattering * ρMie) * Δs};

                        Single = AddRgb(Single, MulRgb(MulRgb(MulRgb(ViewT, SolarT), Source.SolarIrradiance), σPhase));
                    }
                }
            }
        }

        const SpectrumRgb Multiple  = MultipleScatteringApprox(
            Parameters, CanonicalPos, CanonicalDir, CanonicalSun, Single, Source.SolarIrradiance);
        const double      ObsGround = RaySphereAhead(CanonicalPos, CanonicalSun, Parameters.GroundRadiusKm);
        const SpectrumRgb SunObsT   = (ObsGround < 0.0 || ObsGround <= 1e-4)
                                          ? WarpedTransmittanceToTop6Pt(Parameters, RadiusKm, CanonicalSun.Y)
                                          : SpectrumRgb{};
        const SpectrumRgb ClearSky{
            Single.R + Specification.MultipleScatterScale * Multiple.R,
            Single.G + Specification.MultipleScatterScale * Multiple.G,
            Single.B + Specification.MultipleScatterScale * Multiple.B};

        const double CloudSunlit    = std::max(0.0, Dot(UnitSun, LocalUp));
        const double CloudInscatter = (1.0 - CloudT) * (0.18 + 0.52 * CloudSunlit) / π;
        Total.R += std::max(0.0, ClearSky.R * CloudT + Source.SolarIrradiance.R * SunObsT.R * CloudInscatter);
        Total.G += std::max(0.0, ClearSky.G * CloudT + Source.SolarIrradiance.G * SunObsT.G * CloudInscatter);
        Total.B += std::max(0.0, ClearSky.B * CloudT + Source.SolarIrradiance.B * SunObsT.B * CloudInscatter);
    }
    return Total;
}

SpectrumRgb EvaluateSolarSkyDiscRadiance(const SolarSkySpecification& Specification,
                                         SpatialVector                OriginMeters,
                                         SpatialVector                Direction,
                                         double*                      OutPrimarySolidAnglePdf,
                                         std::uint32_t                Steps) noexcept
{
    if (OutPrimarySolidAnglePdf)
    {
        *OutPrimarySolidAnglePdf = 0.0;
    }
    const double UpNorm  = Length(Specification.LocalUpDirection);
    const double DirNorm = Length(Direction);
    if (UpNorm <= 1e-9 || DirNorm <= 1e-9 || Specification.SolarSources.empty())
    {
        return {};
    }
    const SpatialVector LocalUp    = Mul(Specification.LocalUpDirection, 1.0 / UpNorm);
    const SpatialVector UnitDir    = Mul(Direction, 1.0 / DirNorm);
    const double        μUp        = Dot(UnitDir, LocalUp);
    if (μUp <= -0.02)
    {
        return {};
    }
    const double      LocalAltKm = std::max(0.0, Specification.ObserverAltitudeKm + 1e-3 * Dot(OriginMeters, LocalUp));
    const double      RadiusKm   = Specification.Atmosphere.GroundRadiusKm + std::max(LocalAltKm, 1e-3);
    const SpectrumRgb AtmosT     = TransmittanceToTop(Specification.Atmosphere, RadiusKm, μUp, Steps);
    const double      CloudT     = EvaluateSolarSkyCloudTransmittance(Specification, OriginMeters, UnitDir);

    SpectrumRgb Total{};
    for (std::size_t Index = 0; Index < Specification.SolarSources.size(); ++Index)
    {
        const SolarSkySourceProfile& Source  = Specification.SolarSources[Index];
        const double                 SunNorm = Length(Source.SolarDirection);
        if (SunNorm <= 1e-9)
        {
            continue;
        }
        const SpatialVector UnitSun      = Mul(Source.SolarDirection, 1.0 / SunNorm);
        const double        cosθEdge     = std::cos(Source.SolarAngularRadius);
        const double        cosθ         = Dot(UnitDir, UnitSun);
        const double        OneMinusEdge = std::max(1.0 - cosθEdge, 1e-12);
        const double        ΩDisc        = 2.0 * π * OneMinusEdge;
        if (cosθ < cosθEdge)
        {
            continue;
        }
        if (Index == 0u && OutPrimarySolidAnglePdf)
        {
            *OutPrimarySolidAnglePdf = 1.0 / ΩDisc;
        }
        const double CapFraction = std::clamp((cosθ - cosθEdge) / OneMinusEdge, 0.0, 1.0);
        const double μLimb       = std::sqrt(CapFraction);
        const double OneMinusμ   = 1.0 - μLimb;
        const double LimbProfile =
            std::max(0.0, 1.0 - Source.LimbDarkeningU1 * OneMinusμ - Source.LimbDarkeningU2 * OneMinusμ * OneMinusμ);
        const double MeanLimb    = std::max(1e-6, 1.0 - Source.LimbDarkeningU1 / 3.0 - Source.LimbDarkeningU2 / 6.0);
        const double ScaleFactor = ((LimbProfile / MeanLimb) / ΩDisc) * CloudT;

        Total.R += Source.SolarIrradiance.R * AtmosT.R * ScaleFactor;
        Total.G += Source.SolarIrradiance.G * AtmosT.G * ScaleFactor;
        Total.B += Source.SolarIrradiance.B * AtmosT.B * ScaleFactor;
    }
    return Total;
}

SpectrumRgb EvaluateSolarSkyTotalRadiance(const SolarSkySpecification& Specification,
                                          SpatialVector                OriginMeters,
                                          SpatialVector                Direction) noexcept
{
    return AddRgb(EvaluateSolarSkyGaussLegendre6Pt(Specification, OriginMeters, Direction),
                  EvaluateSolarSkyDiscRadiance(Specification, OriginMeters, Direction, nullptr, 32u));
}

SpectrumRgb IntegrateSolarSkyDiscIrradiance(const SolarSkySpecification& Specification,
                                            SpatialVector                OriginMeters,
                                            SpatialVector                SurfaceNormal,
                                            std::uint32_t                RadialRings,
                                            std::uint32_t                AzimuthSectors) noexcept
{
    const double NormLen = Length(SurfaceNormal);
    if (NormLen <= 1e-9 || RadialRings == 0 || AzimuthSectors == 0)
    {
        return {};
    }
    const SpatialVector Normal = Mul(SurfaceNormal, 1.0 / NormLen);

    SpectrumRgb Sum{};
    for (const SolarSkySourceProfile& Source : Specification.SolarSources)
    {
        const double SunNorm = Length(Source.SolarDirection);
        if (SunNorm <= 1e-9)
        {
            continue;
        }
        const SpatialVector UnitSun = Mul(Source.SolarDirection, 1.0 / SunNorm);
        SpatialVector       Tangent{}, Bitangent{};
        ConstructOrthonormalAxes(UnitSun, Tangent, Bitangent);

        const double cosθEdge     = std::cos(Source.SolarAngularRadius);
        const double OneMinusEdge = std::max(1.0 - cosθEdge, 1e-12);
        const double ΔΩ           = (2.0 * π * OneMinusEdge) / static_cast<double>(RadialRings * AzimuthSectors);

        SolarSkySpecification SingleSpec = Specification;
        SingleSpec.SolarSources          = {Source};

        for (std::uint32_t Ring = 0; Ring < RadialRings; ++Ring)
        {
            const double CapU = (static_cast<double>(Ring) + 0.5) / static_cast<double>(RadialRings);
            const double cosθ = cosθEdge + CapU * OneMinusEdge;
            const double sinθ = std::sqrt(std::max(0.0, 1.0 - cosθ * cosθ));
            for (std::uint32_t Sector = 0; Sector < AzimuthSectors; ++Sector)
            {
                const double        ϕ     = 2.0 * π * (static_cast<double>(Sector) + 0.5) / static_cast<double>(AzimuthSectors);
                const double        cosϕ  = std::cos(ϕ);
                const double        sinϕ  = std::sin(ϕ);
                const SpatialVector ωRay{UnitSun.X * cosθ + (Tangent.X * cosϕ + Bitangent.X * sinϕ) * sinθ,
                                         UnitSun.Y * cosθ + (Tangent.Y * cosϕ + Bitangent.Y * sinϕ) * sinθ,
                                         UnitSun.Z * cosθ + (Tangent.Z * cosϕ + Bitangent.Z * sinϕ) * sinθ};
                const double        μSurface = std::max(0.0, Dot(Normal, ωRay));
                if (μSurface <= 0.0)
                {
                    continue;
                }
                const SpectrumRgb Disc = EvaluateSolarSkyDiscRadiance(SingleSpec, OriginMeters, ωRay, nullptr, 48u);
                Sum                    = AddRgb(Sum, MulRgb(Disc, μSurface * ΔΩ));
            }
        }
    }
    return Sum;
}

} // namespace Frontier
