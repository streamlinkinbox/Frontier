/*==========================================================================================================================================
                                                  ASTRONOMICAL EPHEMERIS
                                     ------------------------------------------------------------------
                 Solar ephemeris, twilight phases, Meeus chapter 47 lunar geocentric/topocentric solver,
                  2K selenographic reflectance sampling, Lommel-Seeliger disc radiance, and Hipparcos stars.
==========================================================================================================================================*/
// 🧩 Astronomical ephemeris & lunar disc projection: NOAA/Meeus solar series, Sonia Keys MIT Meeus lunar
//    periodic series, equirectangular sRGB-to-linear reflectance decoding, Lommel-Seeliger 2μ₀/(μ₀+μ)
//    lunar surface radiance, projected-area antialiasing, and Hipparcos J2000 star catalogue decoding.

#pragma once

#include "Engine/PlanetaryAtmosphere/AtmosphereScattering.h"
#include "Engine/Rendering/ClusterNesting.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iosfwd>
#include <vector>

namespace Frontier
{

[[nodiscard]] double JulianDate(int Year, unsigned Month, unsigned Day, double UtcHours) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                            SUN
//------------------------------------------------------------------------------------------------------------------------

struct SolarPosition
{
    double ElevationRad   = 0.0;   // [rad] above local tangent plane (negative below)
    double AzimuthRad     = 0.0;   // [rad] clockwise from north
    double DeclinationRad = 0.0;   // [rad]
};

[[nodiscard]] SolarPosition SunPosition(double Julian, double LatitudeDeg, double LongitudeDeg) noexcept;
[[nodiscard]] SpatialVector SunDirectionLocal(const SolarPosition& Position) noexcept;

enum class DayPhase : uint8_t
{
    Day,
    CivilTwilight,
    NauticalTwilight,
    AstronomicalTwilight,
    Night
};

[[nodiscard]] DayPhase    ClassifyDayPhase(double SolarElevationRad) noexcept;
[[nodiscard]] const char* DayPhaseName(DayPhase Phase) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                            MOON
//------------------------------------------------------------------------------------------------------------------------

struct LunarPhase
{
    double AgeDays      = 0.0;
    double Illumination = 0.0;
};

inline constexpr double SynodicMonthDays   = 29.530588853;
inline constexpr double NewMoonEpochJulian = 2451550.26;

[[nodiscard]] LunarPhase MoonPhase(double Julian) noexcept;

struct LunarGeocentricPosition
{
    double LongitudeRad        = 0.0;
    double LatitudeRad         = 0.0;
    double DistanceKm          = 0.0;
    double ArgumentLatitudeRad = 0.0;
};

[[nodiscard]] LunarGeocentricPosition MoonGeocentric(double JulianTt) noexcept;
[[nodiscard]] double MoonGeometricIllumination(const LunarGeocentricPosition& Moon, double JulianTt) noexcept;

struct LunarTopocentricPosition
{
    SpatialVector Direction{};
    double        DistanceKm       = 0.0;
    double        AngularRadiusRad = 0.0;
};

[[nodiscard]] LunarTopocentricPosition MoonTopocentric(const LunarGeocentricPosition& Moon,
                                                       double                         JulianUt1,
                                                       double                         LatitudeDeg,
                                                       double                         LongitudeDeg,
                                                       double                         HeightMeters = 0.0) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                       LUNAR SURFACE
//------------------------------------------------------------------------------------------------------------------------

class LunarAlbedoMap
{
  public:
    bool LoadPpm(const char* Path);
    bool LoadSrgbBytes(uint32_t Width, uint32_t Height, const uint8_t* SrgbRgbBytes);

    [[nodiscard]] SpectrumRgb Sample(double λEastRad, double φLatitudeRad) const noexcept;
    [[nodiscard]] bool        Loaded() const noexcept { return !Texels_.empty(); }
    [[nodiscard]] uint32_t    Width() const noexcept { return Width_; }
    [[nodiscard]] uint32_t    Height() const noexcept { return Height_; }

  private:
    std::vector<SpectrumRgb> Texels_{};
    uint32_t                 Width_  = 0;
    uint32_t                 Height_ = 0;
};

[[nodiscard]] SpectrumRgb EvaluateLunarDiscRadiance(const LunarAlbedoMap& Map,
                                                    double                DiscA,
                                                    double                DiscB,
                                                    double                λSubObserverRad,
                                                    double                φSubObserverRad,
                                                    double                μ₀Incidence,
                                                    double                μEmission,
                                                    double                PhaseAngleRad,
                                                    const SpectrumRgb&    SolarIrradiance,
                                                    double                EarthshineStrength) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                  LUNAR DISC PROJECTION
//------------------------------------------------------------------------------------------------------------------------

struct LunarProjectionProfile
{
    const LunarAlbedoMap* Albedo                  = nullptr;
    SpatialVector         Direction               = {0.0, 0.0, 1.0};
    SpatialVector         North                   = {0.0, 1.0, 0.0};
    double                AngularRadiusRad        = 0.004596;
    double                FullMoonFluxRatio       = 2.5e-6;
    double                EarthshineStrength      = 0.00018;
    double                SubObserverLongitudeRad = 0.0;
    double                SubObserverLatitudeRad  = 0.0;
};

struct LunarPixel
{
    SpectrumRgb Radiance{};
    double      Coverage = 0.0;
};

class LunarDiscProjection
{
  public:
    LunarDiscProjection(const LunarProjectionProfile& Profile,
                        SpatialVector                 SunDirection,
                        SpectrumRgb                   SunIrradiance) noexcept
        : Profile_(Profile), SunIrradiance_(SunIrradiance)
    {
        constexpr double π               = 3.14159265358979323846;
        const double     DirectionLength = Length(Profile.Direction);
        const double     SunLength       = Length(SunDirection);
        if (!Profile.Albedo || !Profile.Albedo->Loaded() || !std::isfinite(DirectionLength) || DirectionLength < 1e-12 ||
            !std::isfinite(SunLength) || SunLength < 1e-12 || !std::isfinite(Profile.AngularRadiusRad) ||
            Profile.AngularRadiusRad <= 0.0 || Profile.AngularRadiusRad >= 0.35 ||
            !std::isfinite(Profile.FullMoonFluxRatio) || Profile.FullMoonFluxRatio <= 0.0 ||
            !std::isfinite(Profile.EarthshineStrength) || Profile.EarthshineStrength < 0.0 ||
            !std::isfinite(Profile.SubObserverLongitudeRad) || !std::isfinite(Profile.SubObserverLatitudeRad) ||
            !std::isfinite(SunIrradiance.R) || !std::isfinite(SunIrradiance.G) || !std::isfinite(SunIrradiance.B) ||
            SunIrradiance.R < 0.0 || SunIrradiance.G < 0.0 || SunIrradiance.B < 0.0)
        {
            return;
        }
        Direction_          = Mul(Profile.Direction, 1.0 / DirectionLength);
        SunDirection_       = Mul(SunDirection, 1.0 / SunLength);
        Observer_           = Mul(Direction_, -1.0);
        SpatialVector North = Sub(Profile.North, Mul(Direction_, Dot(Profile.North, Direction_)));
        if (!std::isfinite(Length(North)) || Length(North) < 1e-12)
        {
            const SpatialVector Fallback = std::abs(Direction_.Z) < 0.9 ? SpatialVector{0, 0, 1} : SpatialVector{0, 1, 0};
            North                        = Sub(Fallback, Mul(Direction_, Dot(Fallback, Direction_)));
        }
        North_         = Mul(North, 1.0 / Length(North));
        East_          = Cross(Direction_, North_);
        SinRadius_     = std::sin(Profile.AngularRadiusRad);
        ProjectedArea_ = π * SinRadius_ * SinRadius_;
        PhaseAngle_    = std::acos(std::clamp(Dot(Observer_, SunDirection_), -1.0, 1.0));

        double FullMean = 0.0;
        size_t Index    = 0;
        for (uint32_t Ring = 0; Ring < Rings; ++Ring)
        {
            const double ρ = std::sqrt((Ring + 0.5) / Rings);
            const double μ = std::sqrt(1.0 - ρ * ρ);
            for (uint32_t Sector = 0; Sector < Sectors; ++Sector)
            {
                const double ϕ   = 2.0 * π * (Sector + 0.5 * (Ring & 1)) / Sectors;
                Source_[Index].A = ρ * std::cos(ϕ);
                Source_[Index].B = ρ * std::sin(ϕ);
                const auto Full  = EvaluateLunarDiscRadiance(*Profile.Albedo,
                                                            Source_[Index].A,
                                                            Source_[Index].B,
                                                            Profile.SubObserverLongitudeRad,
                                                            Profile.SubObserverLatitudeRad,
                                                            μ,
                                                            μ,
                                                            0.0,
                                                            {1, 1, 1},
                                                            0.0);
                FullMean += Luminance(Full);
                ++Index;
            }
        }
        FullMean /= SourceCount;
        if (!std::isfinite(FullMean) || FullMean <= 1e-15)
        {
            return;
        }
        constexpr double MeanRadius    = 0.004596;
        const double     ReferenceArea = π * std::sin(MeanRadius) * std::sin(MeanRadius);
        Gain_                          = Profile.FullMoonFluxRatio / (ReferenceArea * FullMean);
        Valid_                         = true;
        for (auto& Sample : Source_)
        {
            Sample.Radiance = RadianceAt(Sample.A, Sample.B);
            Irradiance_.R += Sample.Radiance.R * ProjectedArea_ / SourceCount;
            Irradiance_.G += Sample.Radiance.G * ProjectedArea_ / SourceCount;
            Irradiance_.B += Sample.Radiance.B * ProjectedArea_ / SourceCount;
        }
    }

    [[nodiscard]] bool          Valid() const noexcept { return Valid_; }
    [[nodiscard]] SpatialVector Direction() const noexcept { return Direction_; }
    [[nodiscard]] SpectrumRgb   Irradiance() const noexcept { return Irradiance_; }

    [[nodiscard]] LunarPixel Evaluate(double        Dx,
                                      double        Dy,
                                      double        PixelWidth,
                                      double        PixelHeight,
                                      SpatialVector TangentRight,
                                      SpatialVector TangentUp) const noexcept
    {
        LunarPixel Result;
        if (!Valid_ || PixelWidth <= 0.0 || PixelHeight <= 0.0)
        {
            return Result;
        }
        const double EastX          = Dot(East_, TangentRight);
        const double EastY          = Dot(East_, TangentUp);
        const double NorthX         = Dot(North_, TangentRight);
        const double NorthY         = Dot(North_, TangentUp);
        const double RadiusPixels   = SinRadius_ / std::min(PixelWidth, PixelHeight);
        const double Blend          = std::clamp((RadiusPixels - 3.0) / 3.0, 0.0, 1.0);
        const double ResolvedWeight = Blend * Blend * (3.0 - 2.0 * Blend);
        if (ResolvedWeight < 1.0)
        {
            const double σX            = 0.55 * PixelWidth;
            const double σY            = 0.55 * PixelHeight;
            const double Root2         = std::sqrt(2.0);
            const double AreaPerSample = ProjectedArea_ / SourceCount;
            for (const auto& Sample : Source_)
            {
                const double SampleX = SinRadius_ * (Sample.A * EastX + Sample.B * NorthX);
                const double SampleY = SinRadius_ * (Sample.A * EastY + Sample.B * NorthY);
                const double WeightX = 0.5 * (std::erf((Dx + 0.5 * PixelWidth - SampleX) / (Root2 * σX)) -
                                              std::erf((Dx - 0.5 * PixelWidth - SampleX) / (Root2 * σX)));
                const double WeightY = 0.5 * (std::erf((Dy + 0.5 * PixelHeight - SampleY) / (Root2 * σY)) -
                                              std::erf((Dy - 0.5 * PixelHeight - SampleY) / (Root2 * σY)));
                const double Weight  = (1.0 - ResolvedWeight) * AreaPerSample * WeightX * WeightY / (PixelWidth * PixelHeight);
                Result.Radiance.R += Sample.Radiance.R * Weight;
                Result.Radiance.G += Sample.Radiance.G * Weight;
                Result.Radiance.B += Sample.Radiance.B * Weight;
                Result.Coverage += Weight;
            }
        }
        if (ResolvedWeight > 0.0)
        {
            constexpr int Grid = 4;
            for (int Row = 0; Row < Grid; ++Row)
            {
                for (int Col = 0; Col < Grid; ++Col)
                {
                    const double Qx = Dx + ((Col + 0.5) / Grid - 0.5) * PixelWidth;
                    const double Qy = Dy + ((Row + 0.5) / Grid - 0.5) * PixelHeight;
                    const double A  = (Qx * EastX + Qy * EastY) / SinRadius_;
                    const double B  = (Qx * NorthX + Qy * NorthY) / SinRadius_;
                    if (A * A + B * B > 1.0)
                    {
                        continue;
                    }
                    const auto   SampleColor = RadianceAt(A, B);
                    const double Weight      = ResolvedWeight / (Grid * Grid);
                    Result.Radiance.R += SampleColor.R * Weight;
                    Result.Radiance.G += SampleColor.G * Weight;
                    Result.Radiance.B += SampleColor.B * Weight;
                    Result.Coverage += Weight;
                }
            }
        }
        Result.Coverage = std::clamp(Result.Coverage, 0.0, 1.0);
        return Result;
    }

  private:
    static constexpr uint32_t Rings = 32, Sectors = 48, SourceCount = Rings * Sectors;
    struct SourceSample
    {
        double      A = 0;
        double      B = 0;
        SpectrumRgb Radiance{};
    };
    static double        Dot(SpatialVector A, SpatialVector B) noexcept { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
    static double        Length(SpatialVector A) noexcept { return std::sqrt(Dot(A, A)); }
    static SpatialVector Mul(SpatialVector A, double Scalar) noexcept { return {A.X * Scalar, A.Y * Scalar, A.Z * Scalar}; }
    static SpatialVector Add(SpatialVector A, SpatialVector B) noexcept { return {A.X + B.X, A.Y + B.Y, A.Z + B.Z}; }
    static SpatialVector Sub(SpatialVector A, SpatialVector B) noexcept { return {A.X - B.X, A.Y - B.Y, A.Z - B.Z}; }
    static SpatialVector Cross(SpatialVector A, SpatialVector B) noexcept
    {
        return {A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X};
    }
    static double Luminance(SpectrumRgb Color) noexcept { return 0.2126 * Color.R + 0.7152 * Color.G + 0.0722 * Color.B; }

    [[nodiscard]] SpectrumRgb RadianceAt(double A, double B) const noexcept
    {
        const double        μ      = std::sqrt(std::max(0.0, 1.0 - A * A - B * B));
        const SpatialVector Normal = Add(Add(Mul(East_, A), Mul(North_, B)), Mul(Observer_, μ));
        const auto          Color  = EvaluateLunarDiscRadiance(*Profile_.Albedo,
                                                     A,
                                                     B,
                                                     Profile_.SubObserverLongitudeRad,
                                                     Profile_.SubObserverLatitudeRad,
                                                     std::max(0.0, Dot(Normal, SunDirection_)),
                                                     μ,
                                                     PhaseAngle_,
                                                     {1, 1, 1},
                                                     Profile_.EarthshineStrength);
        return {Color.R * Gain_ * SunIrradiance_.R,
                Color.G * Gain_ * SunIrradiance_.G,
                Color.B * Gain_ * SunIrradiance_.B};
    }

    LunarProjectionProfile                Profile_;
    SpectrumRgb                           SunIrradiance_{}, Irradiance_{};
    SpatialVector                         Direction_{}, SunDirection_{}, Observer_{}, East_{}, North_{};
    double                                SinRadius_ = 0, ProjectedArea_ = 0, PhaseAngle_ = 0, Gain_ = 0;
    bool                                  Valid_ = false;
    std::array<SourceSample, SourceCount> Source_{};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                            STARS
//------------------------------------------------------------------------------------------------------------------------

struct StarRecord
{
    SpatialVector Direction{};
    double        Magnitude   = 0.0;
    double        ColourIndex = 0.0;
};

[[nodiscard]] std::vector<StarRecord> DecodeHipparcosCatalogue(std::istream& Input,
                                                               double        JulianUt1,
                                                               double        LatitudeDeg,
                                                               double        LongitudeDeg);

[[nodiscard]] std::vector<StarRecord> GenerateStarCatalogue(uint32_t Count, uint64_t Seed);

} // namespace Frontier
