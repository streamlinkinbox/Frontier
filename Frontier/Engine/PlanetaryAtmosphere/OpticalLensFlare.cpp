/*==========================================================================================================================================
                                               OPTICAL LENS FLARE DEFINITION
==========================================================================================================================================*/
// 🧩 Solar limb-darkening pixel filter, aperture-ghost chain, diffraction spikes, streaks, halo, and GPU sequence.

#include "Engine/PlanetaryAtmosphere/OpticalLensFlare.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace Frontier
{
namespace
{

constexpr double π = 3.14159265358979323846;

double ClampVisibility(double Visibility) noexcept
{
    return std::clamp(Visibility, 0.0, 1.0);
}

SpectrumRgb ScaleRgb(const SpectrumRgb& Colour, double Scale) noexcept
{
    return {Colour.R * Scale, Colour.G * Scale, Colour.B * Scale};
}

SpectrumRgb AddRgb(SpectrumRgb A, SpectrumRgb B) noexcept
{
    return {A.R + B.R, A.G + B.G, A.B + B.B};
}

double ApertureEdgeRadius(double ϕAngle, double Radius, uint32_t Blades, double RotationRad) noexcept
{
    const double Count = static_cast<double>(Blades < 3 ? 3 : Blades);
    const double Local = std::remainder(ϕAngle - RotationRad, 2.0 * π / Count);
    return Radius * std::cos(π / Count) / std::cos(Local);
}

double SoftInside(double ρRadius, double Edge, double Softness) noexcept
{
    const double T = (Edge - ρRadius) / (Softness > 0.0 ? Softness : 1.0e-9);
    const double C = std::clamp(T, 0.0, 1.0);
    return C * C * (3.0 - 2.0 * C);
}

struct PushHeader
{
    uint32_t Counts[4];
    float    Tuning[4];
};

} // namespace

SpectrumRgb EvaluateFlareGhostRadiance(double                    PixelX,
                                       double                    PixelY,
                                       double                    SunX,
                                       double                    SunY,
                                       const SpectrumRgb&        DiscIrradiance,
                                       double                    Visibility,
                                       const FlareOpticsProfile& Profile,
                                       int                       OnlyGhost) noexcept
{
    const double Gain = ClampVisibility(Visibility) * Profile.GhostStrength;
    if (Gain <= 0.0)
    {
        return {};
    }
    SpectrumRgb                     Out{};
    const std::array<FlareGhost, 6> Ghosts = LayoutFlareGhosts(SunX, SunY, 1.0);
    for (uint32_t Index = 0; Index < Ghosts.size(); ++Index)
    {
        if (OnlyGhost >= 0 && static_cast<int>(Index) != OnlyGhost)
        {
            continue;
        }
        const FlareGhost&           Ghost      = Ghosts[Index];
        const FlareGhostAppearance& Appearance = DefaultFlareGhostAppearances[Index];
        const double                Dx         = PixelX - Ghost.PositionX;
        const double                Dy         = PixelY - Ghost.PositionY;
        const double                ρ          = std::sqrt(Dx * Dx + Dy * Dy);
        if (ρ > Ghost.RadiusNdc * 1.15)
        {
            continue;
        }
        const double ϕ               = std::atan2(Dy, Dx);
        const double Softness        = 0.10 * Ghost.RadiusNdc;
        const double ChannelScale[3] = {1.012, 1.0, 0.988};
        const double TintChannel[3]  = {Appearance.Tint.R, Appearance.Tint.G, Appearance.Tint.B};
        const double FluxChannel[3]  = {DiscIrradiance.R, DiscIrradiance.G, DiscIrradiance.B};
        double       Contribution[3] = {0.0, 0.0, 0.0};
        for (uint32_t Channel = 0; Channel < 3; ++Channel)
        {
            const double Edge   = ApertureEdgeRadius(ϕ, Ghost.RadiusNdc * ChannelScale[Channel], Profile.ApertureBlades, Profile.ApertureRotationRad);
            const double Inside = SoftInside(ρ, Edge, Softness);
            double       Shape  = 0.0;
            if (Appearance.RingFraction > 0.0)
            {
                const double RingRadius = Appearance.RingFraction * Edge;
                const double RingWidth  = 0.10 * Ghost.RadiusNdc;
                const double RingDelta  = (ρ - RingRadius) / RingWidth;
                Shape                   = std::exp(-RingDelta * RingDelta) + 0.14 * Inside;
            }
            else
            {
                const double Rim = ρ / (Ghost.RadiusNdc > 0.0 ? Ghost.RadiusNdc : 1.0);
                Shape            = Inside * (0.55 + 0.45 * Rim * Rim);
            }
            Contribution[Channel] = 0.16 * Ghost.Strength * TintChannel[Channel] * FluxChannel[Channel] * Shape;
        }
        Out = AddRgb(Out, {Contribution[0], Contribution[1], Contribution[2]});
    }
    return ScaleRgb(Out, Gain);
}

SpectrumRgb EvaluateDiffractionStarburst(double                    PixelX,
                                         double                    PixelY,
                                         double                    SunX,
                                         double                    SunY,
                                         const SpectrumRgb&        DiscIrradiance,
                                         double                    Visibility,
                                         const FlareOpticsProfile& Profile) noexcept
{
    const double Gain = ClampVisibility(Visibility) * Profile.StarburstStrength;
    if (Gain <= 0.0 || Profile.StarburstLengthNdc <= 0.0)
    {
        return {};
    }
    const double   Dx            = PixelX - SunX;
    const double   Dy            = PixelY - SunY;
    const double   ρ             = std::sqrt(Dx * Dx + Dy * Dy);
    const uint32_t Spikes        = (Profile.ApertureBlades % 2u == 0u) ? Profile.ApertureBlades : 2u * Profile.ApertureBlades;
    const double   ϕ             = std::atan2(Dy, Dx) - Profile.ApertureRotationRad;
    const double   FluxChannel[3] = {DiscIrradiance.R, DiscIrradiance.G, DiscIrradiance.B};
    double         Contribution[3] = {0.0, 0.0, 0.0};
    const double   Radial = std::pow(1.0 / (1.0 + ρ / (0.012 + 0.05 * Profile.StarburstLengthNdc)), 1.7) *
                            std::exp(-ρ / Profile.StarburstLengthNdc);
    for (uint32_t Channel = 0; Channel < 3; ++Channel)
    {
        const double σPhi    = 0.020 * (Channel == 0 ? 1.10 : (Channel == 2 ? 0.92 : 1.0));
        double       Angular = 0.0;
        for (uint32_t Spike = 0; Spike < Spikes; ++Spike)
        {
            double Δϕ = std::remainder(ϕ - 2.0 * π * static_cast<double>(Spike) / static_cast<double>(Spikes), 2.0 * π);
            Δϕ /= σPhi;
            Angular += std::exp(-Δϕ * Δϕ);
        }
        Contribution[Channel] = FluxChannel[Channel] * Angular * Radial;
    }
    return ScaleRgb({Contribution[0], Contribution[1], Contribution[2]}, 0.85 * Gain);
}

SpectrumRgb EvaluateAnamorphicStreak(double                    PixelX,
                                     double                    PixelY,
                                     double                    SunX,
                                     double                    SunY,
                                     const SpectrumRgb&        DiscIrradiance,
                                     double                    Visibility,
                                     const FlareOpticsProfile& Profile) noexcept
{
    const double Gain = ClampVisibility(Visibility) * Profile.AnamorphicStrength;
    if (Gain <= 0.0 || Profile.AnamorphicLengthNdc <= 0.0 || Profile.AnamorphicThicknessNdc <= 0.0)
    {
        return {};
    }
    const double cosα  = std::cos(Profile.AnamorphicAngleRad);
    const double sinα  = std::sin(Profile.AnamorphicAngleRad);
    const double Dx    = PixelX - SunX;
    const double Dy    = PixelY - SunY;
    const double Along = Dx * cosα + Dy * sinα;
    const double Cross = -Dx * sinα + Dy * cosα;
    const double Wide  = std::exp(-(Cross / Profile.AnamorphicThicknessNdc) * (Cross / Profile.AnamorphicThicknessNdc)) *
                         std::exp(-std::abs(Along) / Profile.AnamorphicLengthNdc);
    const double Core  = std::exp(-(Cross / (0.35 * Profile.AnamorphicThicknessNdc)) *
                                  (Cross / (0.35 * Profile.AnamorphicThicknessNdc))) *
                         std::exp(-std::abs(Along) / (0.42 * Profile.AnamorphicLengthNdc));
    const double Shape = Wide + 0.85 * Core;
    return {DiscIrradiance.R * 0.34 * Shape * 0.55 * Gain,
            DiscIrradiance.G * 0.54 * Shape * 0.55 * Gain,
            DiscIrradiance.B * 1.00 * Shape * 0.55 * Gain};
}

SpectrumRgb EvaluateWarmStreak(double                    PixelX,
                               double                    PixelY,
                               double                    SunX,
                               double                    SunY,
                               const SpectrumRgb&        DiscIrradiance,
                               double                    Visibility,
                               const FlareOpticsProfile& Profile) noexcept
{
    const double Gain = ClampVisibility(Visibility) * Profile.StreakStrength;
    if (Gain <= 0.0) return {};
    const double Spread  = std::fmax(0.25, std::fmin(2.0, Profile.StreakSpread));
    const double cosα    = std::cos(Profile.StreakAngleRad);
    const double sinα    = std::sin(Profile.StreakAngleRad);
    const double OffsetX = 0.5 * (PixelX - SunX);
    const double OffsetY = 0.5 * (PixelY - SunY);
    const double Along   = OffsetX * cosα + OffsetY * sinα;
    const double Across  = -OffsetX * sinα + OffsetY * cosα;
    const double Shape   = 0.65 * Gain * std::exp(-(160.0 * std::abs(Across) + 3.0 * std::abs(Along)) / Spread);
    return {DiscIrradiance.R * Shape, DiscIrradiance.G * Shape * 0.8, DiscIrradiance.B * Shape * 0.55};
}

SpectrumRgb EvaluateLensFlareRadiance(double                    PixelX,
                                      double                    PixelY,
                                      double                    SunX,
                                      double                    SunY,
                                      const SpectrumRgb&        DiscIrradiance,
                                      double                    Visibility,
                                      const FlareOpticsProfile& Profile) noexcept
{
    const double ClampedV = ClampVisibility(Visibility);
    if (ClampedV <= 0.0)
    {
        return {};
    }
    SpectrumRgb Out = EvaluateFlareGhostRadiance(PixelX, PixelY, SunX, SunY, DiscIrradiance, ClampedV, Profile);
    Out             = AddRgb(Out, EvaluateDiffractionStarburst(PixelX, PixelY, SunX, SunY, DiscIrradiance, ClampedV, Profile));
    Out             = AddRgb(Out, EvaluateAnamorphicStreak(PixelX, PixelY, SunX, SunY, DiscIrradiance, ClampedV, Profile));
    Out             = AddRgb(Out, EvaluateWarmStreak(PixelX, PixelY, SunX, SunY, DiscIrradiance, ClampedV, Profile));

    const double Dx   = PixelX - SunX;
    const double Dy   = PixelY - SunY;
    const double ρ    = std::sqrt(Dx * Dx + Dy * Dy);
    const double Glow = Profile.GlowStrength *
                        (0.50 * std::exp(-(ρ / 0.035) * (ρ / 0.035)) + 0.025 * std::exp(-(ρ / 0.30) * (ρ / 0.30)));
    Out = AddRgb(Out, ScaleRgb(DiscIrradiance, Glow * ClampedV));

    const double Veil = Profile.VeilingStrength * (0.004 + 0.008 * std::exp(-(ρ / 1.2) * (ρ / 1.2)));
    Out               = AddRgb(Out, ScaleRgb(DiscIrradiance, Veil * ClampedV));

    const double SunDistance = std::sqrt(SunX * SunX + SunY * SunY);
    if (Profile.HaloStrength > 0.0 && SunDistance > 1.0e-6)
    {
        const double Hx              = PixelX + SunX;
        const double Hy              = PixelY + SunY;
        const double ρHalo           = std::sqrt(Hx * Hx + Hy * Hy);
        const double FluxChannel[3]  = {DiscIrradiance.R, DiscIrradiance.G, DiscIrradiance.B};
        const double RadiusOffset[3] = {0.004, 0.0, -0.004};
        double       Halo[3]         = {0.0, 0.0, 0.0};
        for (uint32_t Channel = 0; Channel < 3; ++Channel)
        {
            const double Δ = (ρHalo - SunDistance - RadiusOffset[Channel]) / 0.030;
            Halo[Channel]  = FluxChannel[Channel] * std::exp(-Δ * Δ);
        }
        Out = AddRgb(Out, ScaleRgb({Halo[0], Halo[1], Halo[2]}, 0.035 * Profile.HaloStrength * ClampedV));
    }
    return Out;
}

//------------------------------------------------------------------------------------------------------------------------
//                                               SOLAR DISC PIXEL FILTER
//------------------------------------------------------------------------------------------------------------------------

SolarDiscPixelFilter::SolarDiscPixelFilter(SunDiscProfile Profile) noexcept
    : DiscProfile_(Profile)
{
    ProfileValid_ = std::isfinite(Profile.AngularRadiusRad) && Profile.AngularRadiusRad > 0.0 &&
                    Profile.AngularRadiusRad < 0.5 * π && std::isfinite(Profile.EdgeBlurRad) &&
                    Profile.EdgeBlurRad >= 0.0 && Profile.EdgeBlurRad < 0.25 * π &&
                    std::isfinite(Profile.LimbLinear) && std::isfinite(Profile.LimbQuadratic) &&
                    Profile.LimbLinear >= 0.0 && Profile.LimbQuadratic >= 0.0 &&
                    Profile.LimbLinear + Profile.LimbQuadratic <= 1.0;
    if (!ProfileValid_)
    {
        return;
    }
    double Sum = 0.0;
    for (int Ring = 0; Ring < Rings; ++Ring)
    {
        const double AreaFraction = (Ring + 0.5) / Rings;
        const double Radius       = Profile.AngularRadiusRad * std::sqrt(AreaFraction);
        const double μ            = std::sqrt(1.0 - AreaFraction);
        const double Limb         = std::max(
            0.0, 1.0 - Profile.LimbLinear * (1.0 - μ) - Profile.LimbQuadratic * (1.0 - μ) * (1.0 - μ));
        for (int Sector = 0; Sector < Sectors; ++Sector)
        {
            const double       ϕ      = 2.0 * π * (Sector + 0.5 * (Ring & 1)) / Sectors;
            SolarSourceSample& Sample = SourceSamples_[Ring * Sectors + Sector];
            Sample                    = {Radius * std::cos(ϕ), Radius * std::sin(ϕ), Limb};
            Sum += Limb;
        }
    }
    if (Sum > 0.0)
    {
        for (SolarSourceSample& Sample : SourceSamples_)
        {
            Sample.Weight /= Sum;
        }
    }
    const double σ      = Profile.EdgeBlurRad > 0.0 ? Profile.EdgeBlurRad : 1e-9;
    const double Radius = Profile.AngularRadiusRad;
    if (Radius > 0.0)
    {
        constexpr int Steps    = 256;
        double        Integral = 0.0;
        for (int Part = 0; Part < 2; ++Part)
        {
            const double Start = Part == 0 ? 0.0 : Radius;
            const double End   = Part == 0 ? Radius : Radius + 8.0 * σ;
            const double Width = (End - Start) / Steps;
            for (int Step = 0; Step < Steps; ++Step)
            {
                const double θ        = Start + (Step + 0.5) * Width;
                const double Radiance = EvaluateSunDiscRadiance(θ, Profile, {1, 1, 1}).R;
                Integral += Radiance * 2.0 * π * std::sin(θ) * Width;
            }
        }
        const double Target = SunDiscSolidAngleSr(Radius) * SunDiscMeanLimbFactor(Profile);
        if (Integral > 0.0 && Target > 0.0)
        {
            ResolvedCorrection_ = Target / Integral;
        }
    }
}

SpectrumRgb SolarDiscPixelFilter::Evaluate(double             Dx,
                                           double             Dy,
                                           double             PixelWidth,
                                           double             PixelHeight,
                                           const SpectrumRgb& Irradiance) const noexcept
{
    const double Radius = DiscProfile_.AngularRadiusRad;
    if (!ProfileValid_ || !std::isfinite(Dx) || !std::isfinite(Dy) || !std::isfinite(PixelWidth) ||
        !std::isfinite(PixelHeight) || PixelWidth <= 0.0 || PixelHeight <= 0.0 ||
        !std::isfinite(Irradiance.R) || !std::isfinite(Irradiance.G) || !std::isfinite(Irradiance.B) ||
        Irradiance.R < 0.0 || Irradiance.G < 0.0 || Irradiance.B < 0.0)
    {
        return {};
    }
    const double RadiusPixels  = Radius / std::max(PixelWidth, PixelHeight);
    const double Blend         = std::clamp((6.0 - RadiusPixels) / 3.0, 0.0, 1.0);
    const double SmallWeight   = Blend * Blend * (3.0 - 2.0 * Blend);
    const double σResolved     = DiscProfile_.EdgeBlurRad > 0.0 ? DiscProfile_.EdgeBlurRad : 1e-9;
    const double σSmall        = std::max(σResolved, 0.20 * std::min(PixelWidth, PixelHeight));
    const double HalfDiagonal  = 0.5 * std::hypot(PixelWidth, PixelHeight);
    if (std::hypot(Dx, Dy) > Radius + HalfDiagonal + 6.0 * std::max(σSmall, σResolved))
    {
        return {};
    }
    const double Mean       = SunDiscMeanLimbFactor(DiscProfile_);
    const double SolidAngle = SunDiscSolidAngleSr(Radius);
    if (!(Mean > 0.0 && SolidAngle > 0.0))
    {
        return {};
    }
    const double PixelArea = PixelWidth * PixelHeight;
    if (!std::isfinite(PixelArea) || PixelArea < 1e-300)
    {
        return {};
    }
    double Scalar = 0.0;
    if (SmallWeight > 0.0)
    {
        constexpr double RootTwo = 1.4142135623730951;
        const double     Scale   = 1.0 / (RootTwo * σSmall);
        double           Mass    = 0.0;
        for (const SolarSourceSample& Sample : SourceSamples_)
        {
            const double X0    = (Dx - 0.5 * PixelWidth - Sample.X) * Scale;
            const double X1    = (Dx + 0.5 * PixelWidth - Sample.X) * Scale;
            const double Y0    = (Dy - 0.5 * PixelHeight - Sample.Y) * Scale;
            const double Y1    = (Dy + 0.5 * PixelHeight - Sample.Y) * Scale;
            const double XMass = 0.5 * (std::erf(X1) - std::erf(X0));
            const double YMass = 0.5 * (std::erf(Y1) - std::erf(Y0));
            Mass += Sample.Weight * XMass * YMass;
        }
        Scalar = SmallWeight * Mass / PixelArea;
    }
    if (SmallWeight < 1.0)
    {
        SunDiscProfile Profile = DiscProfile_;
        Profile.EdgeBlurRad    = σResolved;
        constexpr int Grid     = 4;
        double        Average  = 0.0;
        for (int Row = 0; Row < Grid; ++Row)
        {
            for (int Col = 0; Col < Grid; ++Col)
            {
                const double Xp = Dx + ((Col + 0.5) / Grid - 0.5) * PixelWidth;
                const double Yp = Dy + ((Row + 0.5) / Grid - 0.5) * PixelHeight;
                Average += EvaluateSunDiscRadiance(std::hypot(Xp, Yp), Profile, {1, 1, 1}).R;
            }
        }
        Scalar += (1.0 - SmallWeight) * Average * ResolvedCorrection_ / (Grid * Grid * SolidAngle * Mean);
    }
    return {Irradiance.R * Scalar, Irradiance.G * Scalar, Irradiance.B * Scalar};
}

//------------------------------------------------------------------------------------------------------------------------
//                                             SOLAR OPTICS SHADER PAYLOAD
//------------------------------------------------------------------------------------------------------------------------

double CalibrateSolarBlur(const SolarSensorCalibration& Calibration)
{
    const double Focal = Calibration.FocalLengthMm;
    const double Pitch = Calibration.PixelPitchMicrometres;
    const double Width = Calibration.GaussianFwhmPixels;
    if (!std::isfinite(Focal) || !std::isfinite(Pitch) || !std::isfinite(Width) || Focal <= 0 || Pitch <= 0 || Width < 0)
    {
        throw std::invalid_argument("Invalid solar sensor calibration");
    }
    const double Fwhm = Width * Pitch * 0.001 / Focal;
    if (!std::isfinite(Fwhm) || Fwhm > 0.1)
    {
        throw std::invalid_argument("Solar sensor calibration exceeds the small-angle domain");
    }
    return Fwhm / (2.0 * std::sqrt(2.0 * std::log(2.0)));
}

std::vector<float> CompileSolarOpticsPayload(const SolarOpticsParameters& Parameters)
{
    const auto& Disc  = Parameters.Disc;
    const auto& Flare = Parameters.Flare;
    const std::array<double, 27> Fields{
        Parameters.SunDirection.X, Parameters.SunDirection.Y, Parameters.SunDirection.Z,
        Parameters.DiscIrradiance.R, Parameters.DiscIrradiance.G, Parameters.DiscIrradiance.B,
        Disc.AngularRadiusRad, Disc.LimbLinear, Disc.LimbQuadratic, Disc.EdgeBlurRad,
        Flare.ApertureRotationRad, Flare.GhostStrength, Flare.StarburstStrength, Flare.StarburstLengthNdc,
        Flare.AnamorphicStrength, Flare.AnamorphicAngleRad, Flare.AnamorphicLengthNdc, Flare.AnamorphicThicknessNdc,
        Flare.StreakStrength, Flare.StreakAngleRad, Flare.StreakSpread, Flare.GlowStrength, Flare.HaloStrength,
        Flare.VeilingStrength, Parameters.CloudTransmission, Parameters.PlanetLimbZ, static_cast<double>(Flare.ApertureBlades)};
    for (double Value : Fields)
    {
        if (!std::isfinite(Value) || std::abs(Value) > 1e20)
        {
            throw std::invalid_argument("Solar parameters must be finite and bounded");
        }
    }
    const double DirectionLength = std::sqrt(Parameters.SunDirection.X * Parameters.SunDirection.X +
                                             Parameters.SunDirection.Y * Parameters.SunDirection.Y +
                                             Parameters.SunDirection.Z * Parameters.SunDirection.Z);
    if (!(DirectionLength > 1e-10) || Disc.AngularRadiusRad < 1e-7 || Disc.AngularRadiusRad >= 0.5 * π ||
        Disc.EdgeBlurRad < 0 || Disc.EdgeBlurRad >= 0.25 * π || Disc.LimbLinear < 0 || Disc.LimbQuadratic < 0 ||
        Disc.LimbLinear + Disc.LimbQuadratic > 1 || Parameters.DiscIrradiance.R < 0 || Parameters.DiscIrradiance.G < 0 ||
        Parameters.DiscIrradiance.B < 0 || Parameters.CloudTransmission < 0 || Parameters.CloudTransmission > 1 ||
        Parameters.PlanetLimbZ < -1 || Parameters.PlanetLimbZ > 1 || Flare.ApertureBlades < 3 || Flare.ApertureBlades > 24 ||
        Flare.GhostStrength < 0 || Flare.StarburstStrength < 0 || Flare.StarburstLengthNdc < 0 || Flare.AnamorphicStrength < 0 ||
        Flare.AnamorphicLengthNdc < 0 || Flare.AnamorphicThicknessNdc < 0 || Flare.StreakStrength < 0 || Flare.StreakSpread < 0 ||
        Flare.GlowStrength < 0 || Flare.HaloStrength < 0 || Flare.VeilingStrength < 0)
    {
        throw std::invalid_argument("Solar optics parameter domain rejected");
    }
    const double   σ        = Disc.EdgeBlurRad > 0 ? Disc.EdgeBlurRad : 1e-9;
    double         Integral = 0.0;
    SunDiscProfile Resolved = Disc;
    Resolved.EdgeBlurRad    = σ;
    for (uint32_t Part = 0; Part < 2; ++Part)
    {
        const double Start = Part == 0 ? 0.0 : Disc.AngularRadiusRad;
        const double End   = Part == 0 ? Disc.AngularRadiusRad : Disc.AngularRadiusRad + 8.0 * σ;
        const double Step  = (End - Start) / 256.0;
        for (uint32_t Index = 0; Index < 256; ++Index)
        {
            const double θ = Start + (Index + 0.5) * Step;
            Integral += EvaluateSunDiscRadiance(θ, Resolved, {1, 1, 1}).R * 2.0 * π * std::sin(θ) * Step;
        }
    }
    const double HalfSine   = std::sin(0.5 * Disc.AngularRadiusRad);
    const double Target     = 4.0 * π * HalfSine * HalfSine * SunDiscMeanLimbFactor(Disc);
    const double Correction = Integral > 0.0 ? Target / Integral : 1.0;

    const std::array<double, SolarOpticsHeaderFloats> Header{
        Parameters.SunDirection.X / DirectionLength, Parameters.SunDirection.Y / DirectionLength, Parameters.SunDirection.Z / DirectionLength,
        Disc.AngularRadiusRad, Parameters.DiscIrradiance.R, Parameters.DiscIrradiance.G, Parameters.DiscIrradiance.B, σ,
        Disc.LimbLinear, Disc.LimbQuadratic, Correction, Parameters.DiscEnabled ? 1.0 : 0.0,
        static_cast<double>(Flare.ApertureBlades), Flare.ApertureRotationRad, Flare.GhostStrength, Flare.StarburstStrength,
        Flare.StarburstLengthNdc, Flare.AnamorphicStrength, Flare.AnamorphicAngleRad, Flare.AnamorphicLengthNdc,
        Flare.AnamorphicThicknessNdc, Flare.StreakStrength, Flare.StreakAngleRad, Flare.StreakSpread,
        Flare.GlowStrength, Flare.HaloStrength, Flare.VeilingStrength, Parameters.CloudTransmission, Parameters.PlanetLimbZ, 0, 0, 0};

    std::vector<float> Payload(SolarOpticsPayloadFloats, 0.0f);
    for (size_t Index = 0; Index < Header.size(); ++Index)
    {
        Payload[Index] = static_cast<float>(Header[Index]);
    }
    double                                  WeightSum = 0.0;
    std::array<double, SolarSourceCount>    Weights{};
    for (uint32_t Ring = 0; Ring < SolarSourceRings; ++Ring)
    {
        const double Fraction = (Ring + 0.5) / SolarSourceRings;
        const double Radius   = Disc.AngularRadiusRad * std::sqrt(Fraction);
        const double μ        = std::sqrt(1.0 - Fraction);
        const double Weight   = 1.0 - Disc.LimbLinear * (1.0 - μ) - Disc.LimbQuadratic * (1.0 - μ) * (1.0 - μ);
        for (uint32_t Sector = 0; Sector < SolarSourceSectors; ++Sector)
        {
            const uint32_t Index                             = Ring * SolarSourceSectors + Sector;
            const double   ϕ                                 = 2.0 * π * (Sector + 0.5 * (Ring & 1u)) / SolarSourceSectors;
            Payload[SolarOpticsHeaderFloats + 4 * Index]     = static_cast<float>(Radius * std::cos(ϕ));
            Payload[SolarOpticsHeaderFloats + 4 * Index + 1] = static_cast<float>(Radius * std::sin(ϕ));
            Weights[Index]                                   = Weight;
            WeightSum += Weight;
        }
    }
    for (uint32_t Index = 0; Index < SolarSourceCount; ++Index)
    {
        Payload[SolarOpticsHeaderFloats + 4 * Index + 2] = static_cast<float>(Weights[Index] / WeightSum);
    }
    return Payload;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      GPU OPTICAL LENS FLARE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

bool OpticalLensFlare::Initialize(VulkanComputeExchange& Exchange,
                                  const std::string&     ShaderBinaryDirectory,
                                  std::string*           OutDiagnostic)
{
    const std::array<VkDescriptorType, 5> Bindings{{
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // Ensemble
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  // VisibilityProjection
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  // CloudWorkArray (Layer 1.w = Cloud transmittance)
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  // FlareVisibility (N x 1 x 1 RGBA16F)
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  // HdrFrame
    }};

    if (!Exchange.LoadComputeProgram(ShaderBinaryDirectory + "/EvaluateFlareVisibility.spv",
                                     "EvaluateFlareVisibilityMain",
                                     Bindings,
                                     sizeof(PushHeader),
                                     VisibilityProgram_,
                                     OutDiagnostic) ||
        !Exchange.LoadComputeProgram(ShaderBinaryDirectory + "/BlendFlareAperture.spv",
                                     "BlendFlareApertureMain",
                                     Bindings,
                                     sizeof(PushHeader),
                                     ApertureProgram_,
                                     OutDiagnostic))
    {
        return false;
    }

    return Exchange.AllocateRgba16fArray(16u, 1u, 1u, FlareVisibilityImage_);
}

void OpticalLensFlare::Shutdown(VulkanComputeExchange& Exchange) noexcept
{
    Exchange.ReleaseImage(FlareVisibilityImage_);
    Exchange.ReleaseProgram(ApertureProgram_);
    Exchange.ReleaseProgram(VisibilityProgram_);
}

bool OpticalLensFlare::Execute(VulkanComputeExchange&      Exchange,
                               const DeviceMemorySpan&     EnsembleSpan,
                               const DeviceImageArraySlot& VisibilityImage,
                               const VolumetricClouds&     Clouds,
                               DeviceImageArraySlot&       HdrFrameTarget,
                               uint32_t                    FlareSourceCount,
                               uint32_t                    ViewportWidth,
                               uint32_t                    ViewportHeight)
{
    if (FlareSourceCount == 0u)
    {
        return true;
    }
    if (FlareVisibilityImage_.Width < FlareSourceCount)
    {
        if (!Exchange.AllocateRgba16fArray(std::max(16u, FlareSourceCount), 1u, 1u, FlareVisibilityImage_))
        {
            return false;
        }
    }

    const PushHeader Push{{ViewportWidth, ViewportHeight, FlareSourceCount, 0u}, {0.0f, 0.0f, 0.0f, 0.0f}};
    const std::array<ComputeDescriptorArgument, 5> Args{{
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &EnsembleSpan, nullptr},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &VisibilityImage},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &Clouds.CloudWorkArray()},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &FlareVisibilityImage_},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, nullptr, &HdrFrameTarget},
    }};

    if (!Exchange.DispatchCompute(VisibilityProgram_, Args, &Push, sizeof(Push), FlareSourceCount, 1u, 1u))
    {
        return false;
    }

    return Exchange.DispatchCompute(
        ApertureProgram_, Args, &Push, sizeof(Push), (ViewportWidth + 7u) / 8u, (ViewportHeight + 7u) / 8u, 1u);
}

bool OpticalLensFlare::ReadbackVisibilityValues(VulkanComputeExchange&             Exchange,
                                                uint32_t                           FlareSourceCount,
                                                std::vector<std::array<float, 4>>& OutVisibilities)
{
    OutVisibilities.clear();
    if (FlareSourceCount == 0u)
    {
        return true;
    }
    std::vector<float> Rgba;
    if (!Exchange.DownloadRgba16fLayer(FlareVisibilityImage_, 0u, Rgba))
    {
        return false;
    }
    const uint32_t Count = std::min<uint32_t>(FlareSourceCount, FlareVisibilityImage_.Width);
    OutVisibilities.resize(Count);
    for (uint32_t Index = 0u; Index < Count; ++Index)
    {
        OutVisibilities[Index] = {Rgba[Index * 4u + 0u], Rgba[Index * 4u + 1u], Rgba[Index * 4u + 2u], Rgba[Index * 4u + 3u]};
    }
    return true;
}

} // namespace Frontier
