/*==========================================================================================================================================
                                                        ENVIRONMENTENCODING.CPP
==========================================================================================================================================*/
// 🧩 Refresh scalars 128..267 (EnvironmentSettings.js EncodeEnvironment), the six-body moon roster (CelestialAtlas.js) and the
//    low-precision orbital solver for planets and sidereal time (CelestialSolver.js), ported 1:1 from the VehicleHMI demo.

#include "EnvironmentEncoding.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace Frontier::GlobalIllumination
{

namespace
{

constexpr double Pi = 3.141592653589793;
constexpr double R  = Pi / 180.0;   // [rad/°] CelestialSolver.js R

// 📝 JS mod: ((A % B) + B) % B, % being fmod with the dividend's sign.
double Modulo(double A, double B = 360.0) noexcept
{
    return std::fmod(std::fmod(A, B) + B, B);
}

// 📝 Howard Hinnant's days_from_civil: whole days from 1970-01-01 to a proleptic Gregorian date.
int64_t CountCivilDays(int64_t Year, uint32_t Month, uint32_t Day) noexcept
{
    Year -= Month <= 2 ? 1 : 0;
    const int64_t  Era       = (Year >= 0 ? Year : Year - 399) / 400;
    const uint32_t YearOfEra = static_cast<uint32_t>(Year - Era * 400);
    const uint32_t DayOfYear = (153 * (Month + (Month > 2 ? -3 : 9)) + 2) / 5 + Day - 1;
    const uint32_t DayOfEra  = YearOfEra * 365 + YearOfEra / 4 - YearOfEra / 100 + DayOfYear;
    return Era * 146097 + static_cast<int64_t>(DayOfEra) - 719468;
}

// 📝 Date.parse(SkyDate + 'T00:00:00Z') for 'YYYY-MM-DD'; anything else falls back to Date.UTC(2026, 9, 20).
double ParseSkyDateMilliseconds(const std::string& Text) noexcept
{
    int Year = 0, Month = 0, Day = 0;
    char Trailing = 0;
    const bool Parsed = Text.size() == 10 && std::sscanf(Text.c_str(), "%4d-%2d-%2d%c", &Year, &Month, &Day, &Trailing) == 3 && Month >= 1 &&
                        Month <= 12 && Day >= 1 && Day <= 31;
    if (!Parsed) return static_cast<double>(CountCivilDays(2026, 10, 20)) * 86400000.0;
    return static_cast<double>(CountCivilDays(Year, static_cast<uint32_t>(Month), static_cast<uint32_t>(Day))) * 86400000.0;
}

// 📝 Keplerian ellipse → ecliptic rectangular position (Paul Schlyter's formulation, J2000 epoch).
Vector3 SolveOrbit(double N, double i, double w, double a, double e, double M) noexcept
{
    M = Modulo(M) * R;
    double E = M;
    for (int Iteration = 0; Iteration < 6; ++Iteration) E -= (E - e * std::sin(E) - M) / (1 - e * std::cos(E));
    const double X = a * (std::cos(E) - e), Y = a * std::sqrt(1 - e * e) * std::sin(E), V = std::atan2(Y, X), D = ComputeHypot(X, Y);
    N *= R;
    i *= R;
    w *= R;
    return {D * (std::cos(N) * std::cos(V + w) - std::sin(N) * std::sin(V + w) * std::cos(i)),
            D * (std::sin(N) * std::cos(V + w) + std::cos(N) * std::sin(V + w) * std::cos(i)), D * std::sin(V + w) * std::sin(i)};
}

// 📝 Ecliptic → equatorial → local horizontal; engine azimuth is counter-clockwise from +X east, Z up.
HorizontalPlacement ProjectHorizontal(const Vector3& P, double JulianDay, double Latitude, double Longitude) noexcept
{
    const double Ob = (23.4393 - 3.563e-7 * (JulianDay - 2451543.5)) * R;
    const double X = P[0], Y = P[1] * std::cos(Ob) - P[2] * std::sin(Ob), Z = P[1] * std::sin(Ob) + P[2] * std::cos(Ob);
    const double RA = std::atan2(Y, X), Dec = std::atan2(Z, ComputeHypot(X, Y));
    const double LST = Modulo(280.46061837 + 360.98564736629 * (JulianDay - 2451545) + Longitude) * R;
    const double H = LST - RA, L = Latitude * R;
    const double East  = -std::cos(Dec) * std::sin(H);
    const double North = std::sin(Dec) * std::cos(L) - std::cos(Dec) * std::cos(H) * std::sin(L);
    const double Up    = std::sin(Dec) * std::sin(L) + std::cos(Dec) * std::cos(H) * std::cos(L);
    HorizontalPlacement Placement;
    Placement.Direction = {East, North, Up};
    Placement.Azimuth   = std::atan2(North, East) / R;
    Placement.Elevation = std::asin(std::max(-1.0, std::min(1.0, Up))) / R;
    return Placement;
}

std::string KeyOf(std::initializer_list<double> Numbers)
{
    std::string Key;
    char        Text[40];
    for (double Number : Numbers)
    {
        std::snprintf(Text, sizeof(Text), "%.17g,", Number);
        Key += Text;
    }
    return Key;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                   MOON ROSTER
//------------------------------------------------------------------------------------------------------------------------

const std::array<MoonBodyProfile, 6>& QueryMoonBodies() noexcept
{
    static const std::array<MoonBodyProfile, 6> Bodies = {{
        {"Luna", 25.0, 6.7, 0.0, 1.0},
        {"Ember", 1.6, 25.0, 0.08, 1.0},
        {"Glacier", 1.2, 3.0, 0.05, 1.0},
        {"Sulfur", 1.1, 2.0, 0.0, 1.0},
        {"Shroud", 2.4, 27.0, 1.0, 1.0},
        {"Shard", 0.6, 0.0, 0.0, 0.9},
    }};
    return Bodies;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   CELESTIAL SOLVER
//------------------------------------------------------------------------------------------------------------------------

CelestialSolution SolveCelestial(const EnvironmentSettings& S)
{
    const std::string DateText = S.SkyDate.empty() ? std::string("2026-10-20") : S.SkyDate;
    const double JD = ParseSkyDateMilliseconds(DateText) / 86400000 + 2440587.5 + (S.SunTime - S.TimeZone) / 24;
    const double D  = JD - 2451543.5;
    const Vector3 Sun  = SolveOrbit(0, 0, 282.9404 + 4.70935e-5 * D, 1, 0.016709 - 1.151e-9 * D, 356.047 + 0.9856002585 * D);
    const Vector3 Moon = SolveOrbit(125.1228 - 0.0529538083 * D, 5.1454, 318.0634 + 0.1643573223 * D, 60.2666, .0549, 115.3654 + 13.0649929509 * D);
    const double  Lat = S.Latitude, Lon = S.Longitude;
    // 📝 Venus, Jupiter, Mars, Saturn: N, i, w, a, e, M.
    const double Bodies[4][6] = {
        {76.6799 + 2.4659e-5 * D, 3.3946 + 2.75e-8 * D, 54.891 + 1.38374e-5 * D, .72333, .006773 - 1.302e-9 * D, 48.0052 + 1.6021302244 * D},
        {100.4542 + 2.76854e-5 * D, 1.303 - 1.557e-7 * D, 273.8777 + 1.64505e-5 * D, 5.20256, .048498 + 4.469e-9 * D, 19.895 + .0830853001 * D},
        {49.5574 + 2.11081e-5 * D, 1.8497 - 1.78e-8 * D, 286.5016 + 2.92961e-5 * D, 1.523688, .093405 + 2.516e-9 * D, 18.6021 + .5240207766 * D},
        {113.6634 + 2.3898e-5 * D, 2.4886 - 1.081e-7 * D, 339.3939 + 2.97661e-5 * D, 9.55475, .055546 - 9.499e-9 * D, 316.967 + .0334442282 * D}};
    CelestialSolution Solution;
    for (int Planet = 0; Planet < 4; ++Planet)
    {
        const auto&   E = Bodies[Planet];
        const Vector3 H = SolveOrbit(E[0], E[1], E[2], E[3], E[4], E[5]);
        Solution.Planets[Planet] = ProjectHorizontal({H[0] + Sun[0], H[1] + Sun[1], H[2] + Sun[2]}, JD, Lat, Lon);
    }
    Solution.Sun       = ProjectHorizontal(Sun, JD, Lat, Lon);
    Solution.Moon      = ProjectHorizontal(Moon, JD, Lat, Lon);
    Solution.MoonPhase = Modulo(JD - 2451550.1, 29.530588853);
    Solution.JulianDay = JD;
    return Solution;
}

CelestialSolution AdvanceCelestial(EnvironmentSettings& S, double DeltaSeconds)
{
    if (S.DayAnimate)
    {
        S.SunTime         = Modulo(S.SunTime + DeltaSeconds * 24 / (std::max(.01, S.DayDuration) * 3600), 24);
        S.SunAutoPosition = true;
    }
    const CelestialSolution Solved = SolveCelestial(S);
    if (S.SunAutoPosition)
    {
        S.SunAzimuth   = Solved.Sun.Azimuth;
        S.SunElevation = Solved.Sun.Elevation;
    }
    auto FollowSky = [&Solved](MoonInstanceSettings& Moon) {
        if (!Moon.MoonFollowSky) return;
        Moon.MoonAzimuth   = Solved.Moon.Azimuth;
        Moon.MoonElevation = Solved.Moon.Elevation;
        Moon.MoonPhase     = Solved.MoonPhase;
    };
    FollowSky(S.PrimaryMoon);
    for (MoonInstanceSettings& Moon : S.MoonInstances) FollowSky(Moon);
    return Solved;
}

double ResolveSiderealAngle(double JulianDay, double Longitude) noexcept
{
    // 📝 IlluminationHost.js: ((280.46061837 + 360.98564736629·(JD − 2451545) + Longitude) % 360)·π/180 — a plain %, not mod.
    return std::fmod(280.46061837 + 360.98564736629 * (JulianDay - 2451545) + Longitude, 360.0) * Pi / 180;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   ENCODER
//------------------------------------------------------------------------------------------------------------------------

std::vector<float> EnvironmentEncoder::Encode(const EnvironmentSettings& S, const EnvironmentLight& Light)
{
    std::vector<double> Packed;
    Packed.reserve(140);
    auto Push = [&Packed](std::initializer_list<double> Numbers) { Packed.insert(Packed.end(), Numbers.begin(), Numbers.end()); };
    const MoonInstanceSettings& M0 = S.PrimaryMoon;
    const Vector3 Solar = Light.SolarIlluminance.value_or(Vector3{0.0, 0.0, 0.0});
    Push({Light.SunDirection[0], Light.SunDirection[1], Light.SunDirection[2], S.SunRadius * Pi / 180});
    Push({S.StarsEnabled ? S.StarBrightness : 0, S.StarLimit, S.StarSize, S.StarRotation * Pi / 180});
    Push({S.TwinkleDepth, S.TwinkleRate, S.MilkyWay, S.SunEnabled == false ? -18 : S.SunElevation});
    Push({std::cos(M0.MoonElevation * Pi / 180) * std::cos(M0.MoonAzimuth * Pi / 180), std::cos(M0.MoonElevation * Pi / 180) * std::sin(M0.MoonAzimuth * Pi / 180),
          std::sin(M0.MoonElevation * Pi / 180), M0.MoonSize * Pi / 360});
    Push({M0.MoonPhase / 29.53 * 2 * Pi, M0.MoonEnabled ? M0.MoonBrightness : 0, S.FogEnabled ? S.FogDensity : 0, S.FogHeight});
    Push({S.FogScatter, S.AerialEnabled ? S.AerialDensity : 0, S.AerialStart, S.SunTemperature});
    Push({S.FlareEnabled ? S.FlareIntensity : 0, S.FlareSpread, S.FlareGhosts, S.FlareGhostBrightness});
    Push({S.FlareHaloRadius, S.FlareHaloWidth, S.FlareHaloBrightness, S.FlareStreak});
    Push({S.FlareRotation * Pi / 180, S.FlareRays, S.FlareChromatic, S.FlareCustomLayers ? 1.0 : 0.0});
    Push({Solar[0], Solar[1], Solar[2], S.SunGlow});
    Packed[8] = S.TwinkleEnabled ? S.TwinkleDepth : 0;
    Push({S.FlareGhostSpacing, S.FlareGhostShape, S.FlareAnamorphic ? 1.0 : 0.0, S.FlareWarmStreak ? 1.0 : 0.0});
    Push({S.FlareBurst ? 1.0 : 0.0, S.FlareAperture, S.SunDiscEnabled ? 1.0 : 0.0, S.PlanetsEnabled ? S.PlanetBrightness : 0});
    const MoonInstanceSettings Absent{};
    for (uint32_t Instance = 0; Instance < 4; ++Instance)
    {
        const MoonInstanceSettings& M = Instance == 0 ? S.PrimaryMoon : (Instance - 1 < S.MoonInstances.size() ? S.MoonInstances[Instance - 1] : Absent);
        const MoonBodyProfile&      B = QueryMoonBodies()[std::min<uint32_t>(M.MoonBody, 5)];
        const double A = M.MoonAzimuth * Pi / 180, E = M.MoonElevation * Pi / 180;
        Push({std::cos(E) * std::cos(A), std::cos(E) * std::sin(A), std::sin(E), M.MoonRoll * Pi / 180});
        Push({M.MoonSize * Pi / 360, M.MoonEnabled ? M.MoonBrightness : 0, M.MoonPhase / 29.53 * 2 * Pi, M.MoonGlow});
        Push({M.MoonSpin * Pi / 180, (B.Tilt + M.MoonPitch) * Pi / 180, B.Haze, B.Gamma});
        const AtmosphereSettings& Air = S.Atmosphere;
        const std::string Key = KeyOf({A, E, Air.Rayleigh, Air.Mie, Air.Ozone, Air.RayleighHeight, Air.MieHeight});
        auto& Memo = MoonTransmission_[Instance];
        if (!Memo || Memo->Key != Key)
        {
            Memo = MoonTransmissionMemo{Key, ResolveSunTransmittance({std::cos(E) * std::cos(A), std::cos(E) * std::sin(A), std::sin(E)}, Air)};
        }
        Push({static_cast<double>(M.MoonBody), Memo->Transmittance[0], Memo->Transmittance[1], Memo->Transmittance[2]});
    }
    for (uint32_t Planet = 0; Planet < 4; ++Planet)
    {
        const Vector3 Direction = Planet < Light.Planets.size() ? Light.Planets[Planet] : Vector3{0.0, 0.0, -1.0};
        Push({Direction[0], Direction[1], Direction[2], 1.0});
    }
    Push({S.FogColour[0], S.FogColour[1], S.FogColour[2], S.AerialMie});
    Push({Light.PixelAngle.value_or(.0015), Light.Sidereal.value_or(0.0), S.Latitude * Pi / 180, S.StarHalo});
    Push({S.HorizonGlow, S.WhiteLine, S.LineAtCivilOnly ? 1.0 : 0.0, S.AtmosphereEnabled ? 1.0 : 0.0});
    std::vector<float> Scalars(Packed.size());
    for (size_t Index = 0; Index < Packed.size(); ++Index) Scalars[Index] = static_cast<float>(StoreSingle(Packed[Index]));
    return Scalars;
}

} // namespace Frontier::GlobalIllumination
