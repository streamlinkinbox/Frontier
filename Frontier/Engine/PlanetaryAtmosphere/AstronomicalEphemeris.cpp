/*==========================================================================================================================================
                                               ASTRONOMICAL EPHEMERIS DEFINITION
==========================================================================================================================================*/

#include "Engine/PlanetaryAtmosphere/AstronomicalEphemeris.h"

#include "Engine/Foundation/DeterministicRandom.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <istream>
#include <sstream>
#include <string>

namespace Frontier
{
namespace
{

constexpr double π        = 3.14159265358979323846;
constexpr double DegToRad = π / 180.0;

double WrapDegrees(double Degrees) noexcept
{
    Degrees = std::fmod(Degrees, 360.0);
    return Degrees < 0.0 ? Degrees + 360.0 : Degrees;
}

double WrapPi(double Radians) noexcept
{
    while (Radians > π) Radians -= 2.0 * π;
    while (Radians < -π) Radians += 2.0 * π;
    return Radians;
}

// Meeus chapter 47 periodic coefficients and algorithm adapted from Sonia Keys' MIT-licensed
// github.com/soniakeys/meeus/v3/moonposition, revision bfbd9ac2c7f709c94f1dee190c633d24a723f628.
// Copyright 2013 Sonia Keys. Full license: Documentation/Notices/SoniaKeysMeeus.LICENSE.
struct LunarLongitudeTerm
{
    int D, M, Prime, F, Longitude, Distance;
};
struct LunarLatitudeTerm
{
    int D, M, Prime, F, Latitude;
};

constexpr LunarLongitudeTerm LunarLongitudeTerms[] = {
    {0, 0, 1, 0, 6288774, -20905355},  {2, 0, -1, 0, 1274027, -3699111}, {2, 0, 0, 0, 658314, -2955968},
    {0, 0, 2, 0, 213618, -569925},     {0, 1, 0, 0, -185116, 48888},     {0, 0, 0, 2, -114332, -3149},
    {2, 0, -2, 0, 58793, 246158},      {2, -1, -1, 0, 57066, -152138},   {2, 0, 1, 0, 53322, -170733},
    {2, -1, 0, 0, 45758, -204586},     {0, 1, -1, 0, -40923, -129620},   {1, 0, 0, 0, -34720, 108743},
    {0, 1, 1, 0, -30383, 104755},      {2, 0, 0, -2, 15327, 10321},      {0, 0, 1, 2, -12528, 0},
    {0, 0, 1, -2, 10980, 79661},       {4, 0, -1, 0, 10675, -34782},     {0, 0, 3, 0, 10034, -23210},
    {4, 0, -2, 0, 8548, -21636},       {2, 1, -1, 0, -7888, 24208},      {2, 1, 0, 0, -6766, 30824},
    {1, 0, -1, 0, -5163, -8379},       {1, 1, 0, 0, 4987, -16675},       {2, -1, 1, 0, 4036, -12831},
    {2, 0, 2, 0, 3994, -10445},        {4, 0, 0, 0, 3861, -11650},       {2, 0, -3, 0, 3665, 14403},
    {0, 1, -2, 0, -2689, -7003},       {2, 0, -1, 2, -2602, 0},          {2, -1, -2, 0, 2390, 10056},
    {1, 0, 1, 0, -2348, 6322},         {2, -2, 0, 0, 2236, -9884},       {0, 1, 2, 0, -2120, 5751},
    {0, 2, 0, 0, -2069, 0},            {2, -2, -1, 0, 2048, -4950},      {2, 0, 1, -2, -1773, 4130},
    {2, 0, 0, 2, -1595, 0},            {4, -1, -1, 0, 1215, -3958},      {0, 0, 2, 2, -1110, 0},
    {3, 0, -1, 0, -892, 3258},         {2, 1, 1, 0, -810, 2616},         {4, -1, -2, 0, 759, -1897},
    {0, 2, -1, 0, -713, -2117},        {2, 2, -1, 0, -700, 2354},        {2, 1, -2, 0, 691, 0},
    {2, -1, 0, -2, 596, 0},            {4, 0, 1, 0, 549, -1423},         {0, 0, 4, 0, 537, -1117},
    {4, -1, 0, 0, 520, -1571},         {1, 0, -2, 0, -487, -1739},       {2, 1, 0, -2, -399, 0},
    {0, 0, 2, -2, -381, -4421},        {1, 1, 1, 0, 351, 0},             {3, 0, -2, 0, -340, 0},
    {4, 0, -3, 0, 330, 0},             {2, -1, 2, 0, 327, 0},            {0, 2, 1, 0, -323, 1165},
    {1, 1, -1, 0, 299, 0},             {2, 0, 3, 0, 294, 0},             {2, 0, -1, -2, 0, 8752}};

constexpr LunarLatitudeTerm LunarLatitudeTerms[] = {
    {0, 0, 0, 1, 5128122},  {0, 0, 1, 1, 280602},   {0, 0, 1, -1, 277693},  {2, 0, 0, -1, 173237},
    {2, 0, -1, 1, 55413},   {2, 0, -1, -1, 46271},  {2, 0, 0, 1, 32573},    {0, 0, 2, 1, 17198},
    {2, 0, 1, -1, 9266},    {0, 0, 2, -1, 8822},    {2, -1, 0, -1, 8216},   {2, 0, -2, -1, 4324},
    {2, 0, 1, 1, 4200},     {2, 1, 0, -1, -3359},   {2, -1, -1, 1, 2463},   {2, -1, 0, 1, 2211},
    {2, -1, -1, -1, 2065},  {0, 1, -1, -1, -1870},  {4, 0, -1, -1, 1828},   {0, 1, 0, 1, -1794},
    {0, 0, 0, 3, -1749},    {0, 1, -1, 1, -1565},   {1, 0, 0, 1, -1491},    {0, 1, 1, 1, -1475},
    {0, 1, 1, -1, -1410},   {0, 1, 0, -1, -1344},   {1, 0, 0, -1, -1335},   {0, 0, 3, 1, 1107},
    {4, 0, 0, -1, 1021},    {4, 0, -1, 1, 833},     {0, 0, 1, -3, 777},     {4, 0, -2, 1, 671},
    {2, 0, 0, -3, 607},     {2, 0, 2, -1, 596},     {2, -1, 1, -1, 491},    {2, 0, -2, 1, -451},
    {0, 0, 3, -1, 439},     {2, 0, 2, 1, 422},      {2, 0, -3, -1, 421},    {2, 1, -1, 1, -366},
    {2, 1, 0, 1, -351},     {4, 0, 0, 1, 331},      {2, -1, 1, 1, 315},     {2, -2, 0, -1, 302},
    {0, 0, 1, 3, -283},     {2, 1, 1, -1, -229},    {1, 1, 0, -1, 223},     {1, 1, 0, 1, 223},
    {0, 1, -2, -1, -220},   {2, 1, -1, -1, -220},   {1, 0, 1, 1, -185},     {2, -1, -2, -1, 181},
    {0, 1, 2, 1, -177},     {4, 0, -2, -1, 176},    {4, -1, -1, -1, 166},   {1, 0, 1, -1, -164},
    {4, 0, 1, -1, 132},     {1, 0, -1, -1, -119},   {4, -1, 0, -1, 115},    {2, -2, 0, 1, 107}};

double Polynomial(double T, double A, double B, double C, double D, double E = 0.0) noexcept
{
    return ((((E * T + D) * T + C) * T + B) * T + A) * DegToRad;
}

double SolarEccentricityFactor(int SolarMultiplier, double Eccentricity) noexcept
{
    return SolarMultiplier == 0 ? 1.0 : (std::abs(SolarMultiplier) == 1 ? Eccentricity : Eccentricity * Eccentricity);
}

} // namespace

double JulianDate(int Year, unsigned Month, unsigned Day, double UtcHours) noexcept
{
    const int  A   = (14 - static_cast<int>(Month)) / 12;
    const int  Y   = Year + 4800 - A;
    const int  M   = static_cast<int>(Month) + 12 * A - 3;
    const long Jdn = static_cast<long>(Day) + (153L * M + 2) / 5 + 365L * Y + Y / 4 - Y / 100 + Y / 400 - 32045;
    return static_cast<double>(Jdn) + (UtcHours - 12.0) / 24.0;
}

SolarPosition SunPosition(double Julian, double LatitudeDeg, double LongitudeDeg) noexcept
{
    const double DaysSinceJ2000    = Julian - 2451545.0;
    const double MeanLongitude     = WrapDegrees(280.460 + 0.9856474 * DaysSinceJ2000);
    const double MeanAnomaly       = WrapDegrees(357.528 + 0.9856003 * DaysSinceJ2000) * DegToRad;
    const double λEcliptic         = (MeanLongitude + 1.915 * std::sin(MeanAnomaly) + 0.020 * std::sin(2.0 * MeanAnomaly)) * DegToRad;
    const double εObliquity        = (23.439 - 0.0000004 * DaysSinceJ2000) * DegToRad;
    const double δDeclination      = std::asin(std::sin(εObliquity) * std::sin(λEcliptic));
    const double αRightAscension   = std::atan2(std::cos(εObliquity) * std::sin(λEcliptic), std::cos(λEcliptic));

    double GmstHours = std::fmod(18.697374558 + 24.06570982441908 * DaysSinceJ2000, 24.0);
    if (GmstHours < 0.0) GmstHours += 24.0;
    const double LocalSiderealRad = (GmstHours * 15.0 + LongitudeDeg) * DegToRad;
    const double HourAngle        = WrapPi(LocalSiderealRad - αRightAscension);

    const double φLatitude    = LatitudeDeg * DegToRad;
    const double sinθElev     = std::sin(δDeclination) * std::sin(φLatitude) +
                                std::cos(δDeclination) * std::cos(φLatitude) * std::cos(HourAngle);
    const double θElevation   = std::asin(std::clamp(sinθElev, -1.0, 1.0));
    const double ϕAzimuth     = std::atan2(std::sin(HourAngle),
                                           std::cos(HourAngle) * std::sin(φLatitude) -
                                               std::tan(δDeclination) * std::cos(φLatitude)) + π;

    return {θElevation, std::fmod(ϕAzimuth, 2.0 * π), δDeclination};
}

SpatialVector SunDirectionLocal(const SolarPosition& Position) noexcept
{
    const double cosθ = std::cos(Position.ElevationRad);
    return {cosθ * std::sin(Position.AzimuthRad),
            cosθ * std::cos(Position.AzimuthRad),
            std::sin(Position.ElevationRad)};
}

DayPhase ClassifyDayPhase(double SolarElevationRad) noexcept
{
    const double ElevationDeg = SolarElevationRad / DegToRad;
    if (ElevationDeg > -0.833) return DayPhase::Day;
    if (ElevationDeg > -6.0) return DayPhase::CivilTwilight;
    if (ElevationDeg > -12.0) return DayPhase::NauticalTwilight;
    if (ElevationDeg > -18.0) return DayPhase::AstronomicalTwilight;
    return DayPhase::Night;
}

const char* DayPhaseName(DayPhase Phase) noexcept
{
    switch (Phase)
    {
        case DayPhase::Day: return "Day";
        case DayPhase::CivilTwilight: return "CivilTwilight";
        case DayPhase::NauticalTwilight: return "NauticalTwilight";
        case DayPhase::AstronomicalTwilight: return "AstronomicalTwilight";
        case DayPhase::Night: return "Night";
    }
    return "Night";
}

LunarGeocentricPosition MoonGeocentric(double JulianTt) noexcept
{
    if (!std::isfinite(JulianTt)) return {};
    const double CenturiesT = (JulianTt - 2451545.0) / 36525.0;
    if (!std::isfinite(CenturiesT) || std::abs(CenturiesT) > 5.0) return {};

    const double MeanL = Polynomial(CenturiesT, 218.3164477, 481267.88123421, -0.0015786, 1.0 / 538841.0, -1.0 / 65194000.0);
    const double ElongD = Polynomial(CenturiesT, 297.8501921, 445267.1114034, -0.0018819, 1.0 / 545868.0, -1.0 / 113065000.0);
    const double SolarM = Polynomial(CenturiesT, 357.5291092, 35999.0502909, -0.0001535, 1.0 / 24490000.0);
    const double LunarPrime = Polynomial(CenturiesT, 134.9633964, 477198.8675055, 0.0087414, 1.0 / 69699.0, -1.0 / 14712000.0);
    const double ArgF = Polynomial(CenturiesT, 93.272095, 483202.0175233, -0.0036539, -1.0 / 3526000.0, 1.0 / 863310000.0);
    const double VenusA1 = (119.75 + 131.849 * CenturiesT) * DegToRad;
    const double JupiterA2 = (53.09 + 479264.29 * CenturiesT) * DegToRad;
    const double FlatteningA3 = (313.45 + 481266.484 * CenturiesT) * DegToRad;
    const double Eccentricity = 1.0 - 0.002516 * CenturiesT - 0.0000074 * CenturiesT * CenturiesT;

    double Δλ = 3958.0 * std::sin(VenusA1) + 1962.0 * std::sin(MeanL - ArgF) + 318.0 * std::sin(JupiterA2);
    double Δr = 0.0;
    double Δβ = -2235.0 * std::sin(MeanL) + 382.0 * std::sin(FlatteningA3) + 175.0 * std::sin(VenusA1 - ArgF) +
                175.0 * std::sin(VenusA1 + ArgF) + 127.0 * std::sin(MeanL - LunarPrime) - 115.0 * std::sin(MeanL + LunarPrime);

    for (const auto& Term : LunarLongitudeTerms)
    {
        const double Argument = Term.D * ElongD + Term.M * SolarM + Term.Prime * LunarPrime + Term.F * ArgF;
        const double Factor   = SolarEccentricityFactor(Term.M, Eccentricity);
        Δλ += Term.Longitude * std::sin(Argument) * Factor;
        Δr += Term.Distance * std::cos(Argument) * Factor;
    }
    for (const auto& Term : LunarLatitudeTerms)
    {
        const double Argument = Term.D * ElongD + Term.M * SolarM + Term.Prime * LunarPrime + Term.F * ArgF;
        Δβ += Term.Latitude * std::sin(Argument) * SolarEccentricityFactor(Term.M, Eccentricity);
    }
    double λRad = std::fmod(MeanL + Δλ * 1e-6 * DegToRad, 2.0 * π);
    if (λRad < 0.0) λRad += 2.0 * π;
    return {λRad, Δβ * 1e-6 * DegToRad, 385000.56 + Δr * 1e-3, ArgF};
}

double MoonGeometricIllumination(const LunarGeocentricPosition& Moon, double JulianTt) noexcept
{
    if (!std::isfinite(JulianTt) || std::abs(JulianTt - 2451545.0) > 5.0 * 36525.0 ||
        !std::isfinite(Moon.LongitudeRad) || !std::isfinite(Moon.LatitudeRad) ||
        std::abs(Moon.LatitudeRad) > 0.5 * π || !std::isfinite(Moon.DistanceKm) || Moon.DistanceKm <= 6379.0)
    {
        return 0.0;
    }
    const double DaysN         = JulianTt - 2451545.0;
    const double Anomaly       = WrapDegrees(357.528 + 0.9856003 * DaysN) * DegToRad;
    const double λSolar        = (WrapDegrees(280.460 + 0.9856474 * DaysN) +
                                  1.915 * std::sin(Anomaly) + 0.020 * std::sin(2.0 * Anomaly)) * DegToRad;
    const double SolarDistKm   = 149597870.7 * (1.00014 - 0.01671 * std::cos(Anomaly) - 0.00014 * std::cos(2.0 * Anomaly));
    const double cosψElong     = std::clamp(std::cos(Moon.LatitudeRad) * std::cos(Moon.LongitudeRad - λSolar), -1.0, 1.0);
    const double SunMoonDistKm = std::sqrt(SolarDistKm * SolarDistKm + Moon.DistanceKm * Moon.DistanceKm -
                                           2.0 * SolarDistKm * Moon.DistanceKm * cosψElong);
    if (!std::isfinite(SunMoonDistKm) || SunMoonDistKm <= 0.0) return 0.0;
    const double cosPhase = (Moon.DistanceKm - SolarDistKm * cosψElong) / SunMoonDistKm;
    return 0.5 * (1.0 + std::clamp(cosPhase, -1.0, 1.0));
}

LunarTopocentricPosition MoonTopocentric(const LunarGeocentricPosition& Moon,
                                         double                         JulianUt1,
                                         double                         LatitudeDeg,
                                         double                         LongitudeDeg,
                                         double                         HeightMeters) noexcept
{
    if (!std::isfinite(Moon.LongitudeRad) || !std::isfinite(Moon.LatitudeRad) ||
        std::abs(Moon.LatitudeRad) > 0.5 * π || !std::isfinite(Moon.DistanceKm) ||
        Moon.DistanceKm <= 6379.0 || !std::isfinite(JulianUt1) || !std::isfinite(LatitudeDeg) ||
        std::abs(LatitudeDeg) > 90.0 || !std::isfinite(LongitudeDeg) || !std::isfinite(HeightMeters))
    {
        return {};
    }
    const double CenturiesT = (JulianUt1 - 2451545.0) / 36525.0;
    if (!std::isfinite(CenturiesT) || std::abs(CenturiesT) > 5.0) return {};

    const double εObliquity = (23.4392911111 - 0.0130041667 * CenturiesT - 0.0000001639 * CenturiesT * CenturiesT +
                               0.0000005036 * CenturiesT * CenturiesT * CenturiesT) * DegToRad;
    const double λ = Moon.LongitudeRad, β = Moon.LatitudeRad;
    const double αRightAscension = std::atan2(std::sin(λ) * std::cos(εObliquity) - std::tan(β) * std::sin(εObliquity), std::cos(λ));
    const double δDeclination    = std::asin(std::clamp(
        std::sin(β) * std::cos(εObliquity) + std::cos(β) * std::sin(εObliquity) * std::sin(λ), -1.0, 1.0));

    const double DaysN         = JulianUt1 - 2451545.0;
    const double SiderealHours = std::fmod(18.697374558 + 24.06570982441908 * DaysN, 24.0);
    const double HourAngle     = (SiderealHours * 15.0 + LongitudeDeg) * DegToRad - αRightAscension;
    const double φLatitude     = LatitudeDeg * DegToRad;
    const double cosδ          = std::cos(δDeclination);
    const double sinδ          = std::sin(δDeclination);

    const double GeoEast  = -cosδ * std::sin(HourAngle);
    const double GeoNorth = sinδ * std::cos(φLatitude) - cosδ * std::cos(HourAngle) * std::sin(φLatitude);
    const double GeoUp    = sinδ * std::sin(φLatitude) + cosδ * std::cos(HourAngle) * std::cos(φLatitude);

    constexpr double EarthRadiusKm = 6378.137, Flattening = 1.0 / 298.257223563;
    constexpr double EccentricitySq = Flattening * (2.0 - Flattening);
    const double     sinφ = std::sin(φLatitude), cosφ = std::cos(φLatitude);
    const double     PrimeVertical = EarthRadiusKm / std::sqrt(1.0 - EccentricitySq * sinφ * sinφ);
    const double     Equatorial    = (PrimeVertical + HeightMeters * 0.001) * cosφ;
    const double     Polar         = (PrimeVertical * (1.0 - EccentricitySq) + HeightMeters * 0.001) * sinφ;
    const double     ObserverNorth = -Equatorial * sinφ + Polar * cosφ;
    const double     ObserverUp    = Equatorial * cosφ + Polar * sinφ;

    const double TopoX      = Moon.DistanceKm * GeoEast;
    const double TopoY      = Moon.DistanceKm * GeoNorth - ObserverNorth;
    const double TopoZ      = Moon.DistanceKm * GeoUp - ObserverUp;
    const double DistanceKm = std::sqrt(TopoX * TopoX + TopoY * TopoY + TopoZ * TopoZ);
    if (!std::isfinite(DistanceKm) || DistanceKm <= 1737.4) return {};
    return {{TopoX / DistanceKm, TopoY / DistanceKm, TopoZ / DistanceKm}, DistanceKm, std::asin(1737.4 / DistanceKm)};
}

LunarPhase MoonPhase(double Julian) noexcept
{
    double AgeDays = std::fmod(Julian - NewMoonEpochJulian, SynodicMonthDays);
    if (AgeDays < 0.0) AgeDays += SynodicMonthDays;
    const double ψPhase = 2.0 * π * AgeDays / SynodicMonthDays;
    return {AgeDays, 0.5 * (1.0 - std::cos(ψPhase))};
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       LUNAR SURFACE
//------------------------------------------------------------------------------------------------------------------------

bool LunarAlbedoMap::LoadSrgbBytes(uint32_t Width, uint32_t Height, const uint8_t* SrgbRgbBytes)
{
    Texels_.clear();
    Width_ = Height_ = 0;
    if (Width < 4u || Height < 2u || SrgbRgbBytes == nullptr)
    {
        return false;
    }
    Width_  = Width;
    Height_ = Height;
    Texels_.resize(static_cast<size_t>(Width_) * Height_);
    const auto DecodeSrgb = [](uint8_t Byte) noexcept {
        const double Encoded = static_cast<double>(Byte) / 255.0;
        return Encoded <= 0.04045 ? Encoded / 12.92 : std::pow((Encoded + 0.055) / 1.055, 2.4);
    };
    for (size_t Index = 0; Index < Texels_.size(); ++Index)
    {
        Texels_[Index] = {DecodeSrgb(SrgbRgbBytes[3u * Index + 0u]),
                          DecodeSrgb(SrgbRgbBytes[3u * Index + 1u]),
                          DecodeSrgb(SrgbRgbBytes[3u * Index + 2u])};
    }
    return true;
}

bool LunarAlbedoMap::LoadPpm(const char* Path)
{
    Texels_.clear();
    Width_ = Height_ = 0;
    FILE* File = Path != nullptr ? std::fopen(Path, "rb") : nullptr;
    if (File == nullptr)
    {
        return false;
    }
    auto NextToken = [&File](char* TokenOut, size_t Capacity) -> bool {
        int Byte = std::fgetc(File);
        while (Byte != EOF)
        {
            if (Byte == '#')
            {
                while (Byte != EOF && Byte != '\n') Byte = std::fgetc(File);
            }
            else if (Byte == ' ' || Byte == '\t' || Byte == '\r' || Byte == '\n')
            {
                Byte = std::fgetc(File);
            }
            else
            {
                break;
            }
        }
        size_t Count = 0;
        while (Byte != EOF && Byte != ' ' && Byte != '\t' && Byte != '\r' && Byte != '\n' && Count + 1 < Capacity)
        {
            TokenOut[Count++] = static_cast<char>(Byte);
            Byte              = std::fgetc(File);
        }
        TokenOut[Count] = '\0';
        return Count > 0;
    };
    char   Token[32] = {};
    double Maximum   = 0.0;
    bool   HeaderOk  = NextToken(Token, sizeof Token) && std::strcmp(Token, "P6") == 0;
    if (HeaderOk && NextToken(Token, sizeof Token)) Width_ = static_cast<uint32_t>(std::strtoul(Token, nullptr, 10));
    if (HeaderOk && NextToken(Token, sizeof Token)) Height_ = static_cast<uint32_t>(std::strtoul(Token, nullptr, 10));
    if (HeaderOk && NextToken(Token, sizeof Token)) Maximum = std::strtod(Token, nullptr);
    HeaderOk = HeaderOk && Width_ >= 4 && Height_ >= 2 && Maximum >= 1.0 && Maximum <= 255.0;
    if (!HeaderOk)
    {
        std::fclose(File);
        Width_ = Height_ = 0;
        return false;
    }
    std::vector<unsigned char> Bytes(static_cast<size_t>(Width_) * Height_ * 3u);
    const size_t               ReadCount = std::fread(Bytes.data(), 1, Bytes.size(), File);
    std::fclose(File);
    if (ReadCount != Bytes.size())
    {
        Width_ = Height_ = 0;
        return false;
    }
    Texels_.resize(static_cast<size_t>(Width_) * Height_);
    const double Scale      = 1.0 / Maximum;
    const auto   DecodeSrgb = [Scale](unsigned char Byte) noexcept {
        const double Encoded = Byte * Scale;
        return Encoded <= 0.04045 ? Encoded / 12.92 : std::pow((Encoded + 0.055) / 1.055, 2.4);
    };
    for (size_t Index = 0; Index < Texels_.size(); ++Index)
    {
        Texels_[Index] = {DecodeSrgb(Bytes[3 * Index]), DecodeSrgb(Bytes[3 * Index + 1]), DecodeSrgb(Bytes[3 * Index + 2])};
    }
    return true;
}

SpectrumRgb LunarAlbedoMap::Sample(double λEastRad, double φLatitudeRad) const noexcept
{
    if (Texels_.empty())
    {
        return {0.55, 0.54, 0.52};
    }
    double TexCoordU = (λEastRad + π) / (2.0 * π);
    TexCoordU -= std::floor(TexCoordU);
    const double TexCoordV = std::clamp((0.5 * π - φLatitudeRad) / π, 0.0, 1.0);
    const double ScaledX   = TexCoordU * static_cast<double>(Width_) - 0.5;
    const double ScaledY   = TexCoordV * static_cast<double>(Height_) - 0.5;
    const long   X0        = static_cast<long>(std::floor(ScaledX));
    const long   Y0        = static_cast<long>(std::floor(ScaledY));
    const double FracX     = ScaledX - static_cast<double>(X0);
    const double FracY     = ScaledY - static_cast<double>(Y0);
    const auto   At        = [this](long Column, long Row) noexcept {
        const long WrappedCol = ((Column % static_cast<long>(Width_)) + static_cast<long>(Width_)) % static_cast<long>(Width_);
        const long ClampedRow = Row < 0 ? 0 : (Row >= static_cast<long>(Height_) ? static_cast<long>(Height_) - 1 : Row);
        return Texels_[static_cast<size_t>(ClampedRow) * Width_ + static_cast<size_t>(WrappedCol)];
    };
    const SpectrumRgb Row0{At(X0, Y0).R * (1.0 - FracX) + At(X0 + 1, Y0).R * FracX,
                           At(X0, Y0).G * (1.0 - FracX) + At(X0 + 1, Y0).G * FracX,
                           At(X0, Y0).B * (1.0 - FracX) + At(X0 + 1, Y0).B * FracX};
    const SpectrumRgb Row1{At(X0, Y0 + 1).R * (1.0 - FracX) + At(X0 + 1, Y0 + 1).R * FracX,
                           At(X0, Y0 + 1).G * (1.0 - FracX) + At(X0 + 1, Y0 + 1).G * FracX,
                           At(X0, Y0 + 1).B * (1.0 - FracX) + At(X0 + 1, Y0 + 1).B * FracX};
    return {Row0.R * (1.0 - FracY) + Row1.R * FracY,
            Row0.G * (1.0 - FracY) + Row1.G * FracY,
            Row0.B * (1.0 - FracY) + Row1.B * FracY};
}

SpectrumRgb EvaluateLunarDiscRadiance(const LunarAlbedoMap& Map,
                                      double                DiscA,
                                      double                DiscB,
                                      double                λSubObserverRad,
                                      double                φSubObserverRad,
                                      double                μ₀Incidence,
                                      double                μEmission,
                                      double                PhaseAngleRad,
                                      const SpectrumRgb&    SolarIrradiance,
                                      double                EarthshineStrength) noexcept
{
    const double ρSquared = DiscA * DiscA + DiscB * DiscB;
    if (ρSquared > 1.0)
    {
        return {};
    }
    const double      TowardObserver = std::sqrt(1.0 - ρSquared);
    const double      sinφ₀          = std::sin(φSubObserverRad);
    const double      cosφ₀          = std::cos(φSubObserverRad);
    const double      φRad           = std::asin(std::clamp(TowardObserver * sinφ₀ + DiscB * cosφ₀, -1.0, 1.0));
    const double      λRad           = λSubObserverRad + std::atan2(DiscA, TowardObserver * cosφ₀ - DiscB * sinφ₀);
    const SpectrumRgb Albedo         = Map.Sample(λRad, φRad);
    const double      μ₀             = μ₀Incidence > 0.0 ? μ₀Incidence : 0.0;
    const double      μ              = μEmission > 1.0e-4 ? μEmission : 1.0e-4;
    const double      LommelSeeliger = μ₀ > 0.0 ? 2.0 * μ₀ / (μ₀ + μ) : 0.0;
    const double      PhaseDimming   = std::exp(-1.10 * std::abs(PhaseAngleRad));
    const double      LitScale       = LommelSeeliger * PhaseDimming / π;
    return {Albedo.R * (SolarIrradiance.R * LitScale + EarthshineStrength * 0.55),
            Albedo.G * (SolarIrradiance.G * LitScale + EarthshineStrength * 0.65),
            Albedo.B * (SolarIrradiance.B * LitScale + EarthshineStrength * 1.00)};
}

//------------------------------------------------------------------------------------------------------------------------
//                                                            STARS
//------------------------------------------------------------------------------------------------------------------------

std::vector<StarRecord> DecodeHipparcosCatalogue(std::istream& Input,
                                                 double        JulianUt1,
                                                 double        LatitudeDeg,
                                                 double        LongitudeDeg)
{
    std::vector<StarRecord> Stars;
    if (!std::isfinite(JulianUt1) || !std::isfinite(LatitudeDeg) || !std::isfinite(LongitudeDeg) ||
        std::abs(LatitudeDeg) > 90.0)
    {
        return Stars;
    }
    const double     Years       = (JulianUt1 - 2451545.0) / 365.25;
    const double     φLatitude   = LatitudeDeg * DegToRad;
    const double     GmstHours   = std::fmod(18.697374558 + 24.06570982441908 * (JulianUt1 - 2451545.0), 24.0);
    const double     SiderealRad = (GmstHours * 15.0 + LongitudeDeg) * DegToRad;
    constexpr double ArcsecToRad = π / (180.0 * 3600.0);
    const double     PrecessM    = 46.124 * Years * ArcsecToRad;
    const double     PrecessN    = 20.043 * Years * ArcsecToRad;
    const double     cosM = std::cos(PrecessM), sinM = std::sin(PrecessM);
    const double     cosN = std::cos(PrecessN), sinN = std::sin(PrecessN);
    const double     cosSid = std::cos(SiderealRad), sinSid = std::sin(SiderealRad);
    const double     cosφ = std::cos(φLatitude), sinφ = std::sin(φLatitude);

    std::string Line;
    while (std::getline(Input, Line))
    {
        if (Line.empty() || Line[0] == '#' || Line.rfind("id,", 0) == 0) continue;
        std::stringstream Fields(Line);
        std::string       Id, RaText, DecText, MagnitudeText, ColourText;
        if (!std::getline(Fields, Id, ',') || !std::getline(Fields, RaText, ',') ||
            !std::getline(Fields, DecText, ',') || !std::getline(Fields, MagnitudeText, ',') ||
            !std::getline(Fields, ColourText, ','))
        {
            continue;
        }
        auto ParseNumber = [](const std::string& Text, double& Out) {
            char* End = nullptr;
            Out       = std::strtod(Text.c_str(), &End);
            if (End == Text.c_str()) return false;
            while (std::isspace(static_cast<unsigned char>(*End))) ++End;
            return *End == '\0' && std::isfinite(Out);
        };
        double RaDeg = 0, DecDeg = 0, Magnitude = 0, Colour = 0;
        if (!ParseNumber(RaText, RaDeg) || !ParseNumber(DecText, DecDeg) ||
            !ParseNumber(MagnitudeText, Magnitude) || !ParseNumber(ColourText, Colour) ||
            RaDeg < 0.0 || RaDeg >= 360.0 || std::abs(DecDeg) > 90.0 || Magnitude < -35.0 || Magnitude > 40.0)
        {
            continue;
        }
        const double αRad        = RaDeg * DegToRad;
        const double δRad        = DecDeg * DegToRad;
        const double UnitX       = std::cos(δRad) * std::cos(αRad);
        const double UnitY       = std::cos(δRad) * std::sin(αRad);
        const double UnitZ       = std::sin(δRad);
        const double RotatedX    = cosN * UnitX - sinN * UnitZ;
        const double RotatedZ    = sinN * UnitX + cosN * UnitZ;
        const double EquatorialX = cosM * RotatedX - sinM * UnitY;
        const double EquatorialY = sinM * RotatedX + cosM * UnitY;
        const double Meridian    = cosSid * EquatorialX + sinSid * EquatorialY;
        const SpatialVector Direction{-sinSid * EquatorialX + cosSid * EquatorialY,
                                      RotatedZ * cosφ - Meridian * sinφ,
                                      RotatedZ * sinφ + Meridian * cosφ};
        Stars.push_back({Direction, Magnitude, Colour});
    }
    return Stars;
}

std::vector<StarRecord> GenerateStarCatalogue(uint32_t Count, uint64_t Seed)
{
    std::vector<StarRecord> Stars;
    Stars.reserve(Count);
    DeterministicRandom Random(Seed, 0x57A2ull);
    for (uint32_t Index = 0; Index < Count; ++Index)
    {
        const double UnitZ = 2.0 * Random.NextUnit() - 1.0;
        const double ϕ     = 2.0 * π * Random.NextUnit();
        const double ρ     = std::sqrt(std::max(1.0 - UnitZ * UnitZ, 0.0));
        StarRecord   Star;
        Star.Direction   = {ρ * std::cos(ϕ), ρ * std::sin(ϕ), UnitZ};
        Star.Magnitude   = -1.0 + 7.0 * std::pow(Random.NextUnit(), 0.45);
        Star.ColourIndex = 2.0 * Random.NextUnit() - 1.0;
        Stars.push_back(Star);
    }
    return Stars;
}

} // namespace Frontier
