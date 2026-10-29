/*==========================================================================================================================================
                                                       NIGHT SKY EXHIBIT
                                     ------------------------------------------------------------------
              Hipparcos star field, Milky Way band, Moon disc and field, single-scattering day and dusk sky, and
              a sun-disc lens flare with ghosts measured in the rendered image. CPU only, headless.
==========================================================================================================================================*/

// 🧩 Sky exhibit. Solar position and lunar phase come from CelestialModel; the scattering day and dusk sky
//    comes from AtmosphereModel (transmittance LUT, single + approximated multiple scattering); the sun disc,
//    ghost chain, starburst, anamorphic streak and halo come from LensFlareModel. Stars are the open
//    Hipparcos-derived catalogue in Data/Hipparcos8.csv (XHIP via d3-celestial, BSD-3). The lunar ephemeris
//    uses the Meeus periodic series with topocentric parallax; the lunar surface is the actual demo's
//    Luna texture in EngineContent/CelestialMaps (source and licence limits in MoonAlbedoProvenance.md), shaded with the
//    Lommel–Seeliger law in CelestialModel. Display mapping is the demonstration's own Narkowicz ACES
//    (DisplayPresent.wgsl ProjectFilmic) with the exact sRGB opto-electronic transfer.

#include "Engine/Rendering/AtmosphereModel.h"
#include "Engine/Rendering/CelestialModel.h"
#include "Engine/Rendering/LensFlareModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{

constexpr double Pi = 3.14159265358979323846;
constexpr double DegToRad = Pi / 180.0;
constexpr double ArcsecToRad = Pi / (180.0 * 3600.0);
constexpr double BenoniLatitudeDeg = -26.1885;   // Benoni, Gauteng, ZA
constexpr double BenoniLongitudeDeg = 28.3209;
// TT − UTC is 69.184 s for the 2026 exhibit dates (UTC≈UT1 here); callers outside that range
// must supply their own time-scale conversion to the production ephemeris API.
constexpr double ExhibitTtMinusUtcDays = 69.184 / 86400.0;
constexpr double SunAngularRadius = 0.2666 * DegToRad;
constexpr double StandardHalfTangent = 0.2679;   // tan(15 deg): 30 degree vertical field
constexpr double CatalogueEpochYearsToDate = 26.82;   // J2000.0 to 2026-10-26

struct SkyVector
{
    double X = 0.0, Y = 0.0, Z = 0.0;   // local frame: X east, Y north, Z up
};

SkyVector operator+(SkyVector A, SkyVector B) { return {A.X + B.X, A.Y + B.Y, A.Z + B.Z}; }
SkyVector operator-(SkyVector A, SkyVector B) { return {A.X - B.X, A.Y - B.Y, A.Z - B.Z}; }
SkyVector operator*(SkyVector A, double S) { return {A.X * S, A.Y * S, A.Z * S}; }
double Dot(SkyVector A, SkyVector B) { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
SkyVector Cross(SkyVector A, SkyVector B)
{
    return {A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X};
}
SkyVector Normalize(SkyVector A)
{
    const double Length = std::sqrt(Dot(A, A));
    return Length > 0.0 ? A * (1.0 / Length) : SkyVector{0.0, 0.0, 1.0};
}

struct Radiance
{
    double R = 0.0, G = 0.0, B = 0.0;
};
Radiance operator+(Radiance A, Radiance B) { return {A.R + B.R, A.G + B.G, A.B + B.B}; }
Radiance operator*(Radiance A, double S) { return {A.R * S, A.G * S, A.B * S}; }

double Clamp01(double X) { return std::clamp(X, 0.0, 1.0); }
double Smoothstep(double Edge0, double Edge1, double X)
{
    const double T = Clamp01((X - Edge0) / (Edge1 - Edge0));
    return T * T * (3.0 - 2.0 * T);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   TIME, FRAMES AND CATALOGUE
//------------------------------------------------------------------------------------------------------------------------

double LocalSiderealRad(double Julian)
{
    const double N = Julian - 2451545.0;
    double GmstHours = std::fmod(18.697374558 + 24.06570982441908 * N, 24.0);
    if (GmstHours < 0.0) GmstHours += 24.0;
    return (GmstHours * 15.0 + BenoniLongitudeDeg) * DegToRad;
}

// Equatorial (RA, Dec) to local ENU, same azimuth convention as CelestialModel's SunDirectionLocal.
SkyVector EquatorialToLocal(double RightAscensionRad, double DeclinationRad, double LocalSiderealRadians)
{
    const double Latitude = BenoniLatitudeDeg * DegToRad;
    const double HourAngle = LocalSiderealRadians - RightAscensionRad;
    const double Altitude = std::asin(std::clamp(std::sin(DeclinationRad) * std::sin(Latitude) +
                                                     std::cos(DeclinationRad) * std::cos(Latitude) * std::cos(HourAngle),
                                                 -1.0, 1.0));
    const double Azimuth = std::atan2(std::sin(HourAngle), std::cos(HourAngle) * std::sin(Latitude) -
                                                               std::tan(DeclinationRad) * std::cos(Latitude)) + Pi;
    return {std::cos(Altitude) * std::sin(Azimuth), std::cos(Altitude) * std::cos(Azimuth), std::sin(Altitude)};
}

void LocalToEquatorial(SkyVector V, double LocalSiderealRadians, double& RightAscensionRad, double& DeclinationRad)
{
    const double Latitude = BenoniLatitudeDeg * DegToRad;
    const double Altitude = std::asin(std::clamp(V.Z, -1.0, 1.0));
    const double AzimuthNorth = std::atan2(V.X, V.Y);
    DeclinationRad = std::asin(std::clamp(std::sin(Latitude) * std::sin(Altitude) +
                                              std::cos(Latitude) * std::cos(Altitude) * std::cos(AzimuthNorth),
                                          -1.0, 1.0));
    const double SinHour = -std::sin(AzimuthNorth) * std::cos(Altitude) * std::cos(Latitude);
    const double CosHour = std::sin(Altitude) - std::sin(Latitude) * std::sin(DeclinationRad);
    RightAscensionRad = LocalSiderealRadians - std::atan2(SinHour, CosHour);
}

struct EquatorialPoint
{
    double RightAscensionRad = 0.0;
    double DeclinationRad = 0.0;
};

EquatorialPoint SunEquatorial(double Julian)
{
    const double N = Julian - 2451545.0;
    const double MeanLongitude = (280.460 + 0.9856474 * N) * DegToRad;
    const double MeanAnomaly = (357.528 + 0.9856003 * N) * DegToRad;
    const double Lambda = MeanLongitude + (1.915 * std::sin(MeanAnomaly) + 0.020 * std::sin(2.0 * MeanAnomaly)) * DegToRad;
    const double Obliquity = (23.439 - 0.0000004 * N) * DegToRad;
    return {std::atan2(std::cos(Obliquity) * std::sin(Lambda), std::cos(Lambda)),
            std::asin(std::sin(Obliquity) * std::sin(Lambda))};
}

struct EclipticPoint
{
    double LongitudeRad = 0.0;
    double LatitudeRad = 0.0;
    double ArgumentLatitudeRad = 0.0;   // [rad] mean argument of latitude F, for libration
};

// Full chapter 47 periodic series in the production model; the exhibit's dates are all in 2026.
EclipticPoint MoonEcliptic(double Julian)
{
    const auto Moon = Frontier::MoonGeocentric(Julian + ExhibitTtMinusUtcDays);
    return {Moon.LongitudeRad, Moon.LatitudeRad, Moon.ArgumentLatitudeRad};
}

Frontier::LunarTopocentricPosition MoonAtBenoni(double Julian)
{
    return Frontier::MoonTopocentric(Frontier::MoonGeocentric(Julian + ExhibitTtMinusUtcDays),
                                     Julian, BenoniLatitudeDeg, BenoniLongitudeDeg, 0.0);
}

SkyVector MoonDirectionAtBenoni(double Julian)
{
    const auto Direction = MoonAtBenoni(Julian).Direction;
    return {Direction.X, Direction.Y, Direction.Z};
}

EquatorialPoint MoonEquatorial(double Julian)
{
    const EclipticPoint Moon = MoonEcliptic(Julian);
    const double D = Julian - 2451545.0;
    const double Obliquity = (23.439 - 0.0000004 * D) * DegToRad;
    const double Lambda = Moon.LongitudeRad;
    const double Beta = Moon.LatitudeRad;
    return {std::atan2(std::sin(Lambda) * std::cos(Obliquity) - std::tan(Beta) * std::sin(Obliquity), std::cos(Lambda)),
            std::asin(std::sin(Beta) * std::cos(Obliquity) + std::cos(Beta) * std::sin(Obliquity) * std::sin(Lambda))};
}

struct LunarLibration
{
    double SubObserverLongitudeRad = 0.0;   // [rad] selenographic longitude facing the observer
    double SubObserverLatitudeRad = 0.0;    // [rad] selenographic latitude facing the observer
};

// 📝 Optical libration, Meeus chapter 53 at the same truncated-series precision as the ephemeris:
//    W = λ − Ω, A = atan2(sinW·cosβ·cosI − sinβ·sinI, cosW·cosβ), l' = A − F,
//    b' = asin(−sinW·cosβ·sinI − sinβ·cosI), I = 1.54242°. Sign conventions settled numerically
//    against the independent pole-geometry latitude asin(Observer·Pole) before adoption
//    (agreement 0.3° at the exhibit epoch): sub-observer latitude = +b', longitude = −l'
//    (l' > 0 librates the mean centre toward Mare Crisium, so the observer faces west of it).
LunarLibration OpticalLibration(double Julian)
{
    const EclipticPoint Moon = MoonEcliptic(Julian);
    const double T = (Julian - 2451545.0) / 36525.0;
    const double Omega = (125.0445479 - 1934.1362891 * T) * DegToRad;
    const double Inclination = 1.54242 * DegToRad;
    const double W = Moon.LongitudeRad - Omega;
    const double A = std::atan2(std::sin(W) * std::cos(Moon.LatitudeRad) * std::cos(Inclination) -
                                    std::sin(Moon.LatitudeRad) * std::sin(Inclination),
                                std::cos(W) * std::cos(Moon.LatitudeRad));
    double Longitude = -(A - Moon.ArgumentLatitudeRad);
    while (Longitude > Pi) Longitude -= 2.0 * Pi;
    while (Longitude < -Pi) Longitude += 2.0 * Pi;
    const double Latitude = std::asin(-std::sin(W) * std::cos(Moon.LatitudeRad) * std::sin(Inclination) -
                                      std::sin(Moon.LatitudeRad) * std::cos(Inclination));
    return {Longitude, Latitude};
}

double GeometricIlluminatedFraction(EquatorialPoint Sun, EquatorialPoint Moon)
{
    const double CosElongation = std::sin(Sun.DeclinationRad) * std::sin(Moon.DeclinationRad) +
                                 std::cos(Sun.DeclinationRad) * std::cos(Moon.DeclinationRad) *
                                     std::cos(Sun.RightAscensionRad - Moon.RightAscensionRad);
    return 0.5 * (1.0 - CosElongation);
}

// Precession from J2000 to the observation date (IAU first-order: m = 46.124, n = 20.043 arcsec per year).
EquatorialPoint PrecessFromJ2000(double RightAscensionRad, double DeclinationRad, double Years)
{
    constexpr double M = 46.124, N = 20.043;
    const double DeltaRa = (M + N * std::sin(RightAscensionRad) * std::tan(DeclinationRad)) * Years * ArcsecToRad;
    const double DeltaDec = N * std::cos(RightAscensionRad) * Years * ArcsecToRad;
    return {RightAscensionRad + DeltaRa, DeclinationRad + DeltaDec};
}

struct CatalogueStar
{
    double RightAscensionRad = 0.0;   // J2000
    double DeclinationRad = 0.0;      // J2000
    double Magnitude = 0.0;
    double ColourBv = 0.0;
};

// Reads Data/Hipparcos8.csv (id,ra_deg,dec_deg,mag,bv). Returns an empty vector if the file is missing.
std::vector<CatalogueStar> LoadHipparcosCatalogue(const std::string& Path)
{
    std::vector<CatalogueStar> Stars;
    std::ifstream File(Path);
    std::string Line;
    while (std::getline(File, Line))
    {
        if (Line.empty() || Line[0] == '#' || Line.rfind("id,", 0) == 0) continue;
        std::stringstream Fields(Line);
        std::string Id, Ra, Dec, Mag, Bv;
        if (!std::getline(Fields, Id, ',') || !std::getline(Fields, Ra, ',') || !std::getline(Fields, Dec, ',') ||
            !std::getline(Fields, Mag, ',') || !std::getline(Fields, Bv, ','))
            continue;
        CatalogueStar Star;
        Star.RightAscensionRad = std::strtod(Ra.c_str(), nullptr) * DegToRad;
        Star.DeclinationRad = std::strtod(Dec.c_str(), nullptr) * DegToRad;
        Star.Magnitude = std::strtod(Mag.c_str(), nullptr);
        Star.ColourBv = std::strtod(Bv.c_str(), nullptr);
        Stars.push_back(Star);
    }
    return Stars;
}

// Blackbody-like colour from B-V (Ballesteros temperature, Tanner Helland blackbody fit), normalised to max 1.
Radiance StarColour(double Bv)
{
    const double Temperature = 4600.0 * (1.0 / (0.92 * Bv + 1.7) + 1.0 / (0.92 * Bv + 0.62));
    const double T = Temperature / 100.0;
    const double Red = T <= 66.0 ? 255.0 : 329.698727446 * std::pow(T - 60.0, -0.1332047592);
    const double Green = T <= 66.0 ? 99.4708025861 * std::log(T) - 161.1195681661
                                   : 288.1221695283 * std::pow(T - 60.0, -0.0755148492);
    const double Blue = T >= 66.0 ? 255.0 : (T <= 19.0 ? 0.0 : 138.5177312231 * std::log(T - 10.0) - 305.0447927307);
    Radiance Colour{std::clamp(Red, 0.0, 255.0) / 255.0, std::clamp(Green, 0.0, 255.0) / 255.0,
                    std::clamp(Blue, 0.0, 255.0) / 255.0};
    const double Peak = std::max({Colour.R, Colour.G, Colour.B, 1e-6});
    return Colour * (1.0 / Peak);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   MOON
//------------------------------------------------------------------------------------------------------------------------

struct MoonFrame
{
    SkyVector Right;
    SkyVector Up;
    SkyVector Observer;
    SkyVector TowardSun;
    double    PhaseAngleRad = 0.0;              // [rad] Sun–Moon–observer angle, 0 at full Moon
    Radiance  Tint{1.0, 1.0, 1.0};              // [-] atmospheric transmittance toward the Moon
    // 📝 Camera-to-lunar disc rotation: components of the projected lunar north axis in the
    //    (Right, Up) disc basis, plus the sub-observer point from optical libration.
    double    NorthRight = 0.0;                 // [-] Right · projected pole axis
    double    NorthUp = 1.0;                    // [-] Up · projected pole axis
    double    SubObserverLongitudeRad = 0.0;    // [rad]
    double    SubObserverLatitudeRad = 0.0;     // [rad]
    double    GeometricLatitudeRad = 0.0;       // [rad] asin(Observer·Pole), independent cross-check
};

MoonFrame MakeMoonFrame(SkyVector MoonDirection, SkyVector SunDirection, double Julian)
{
    MoonFrame Frame;
    Frame.Right = Normalize(Cross(MoonDirection, {0.0, 0.0, 1.0}));
    Frame.Up = Cross(Frame.Right, MoonDirection);
    Frame.Observer = MoonDirection * -1.0;
    Frame.TowardSun = SunDirection;
    Frame.PhaseAngleRad = std::acos(std::clamp(Dot(Frame.Observer, Frame.TowardSun), -1.0, 1.0));
    // Lunar north pole (IAU mean pole near J2000: RA 273.85°, Dec +66.54°) in the local frame,
    // projected into the disc plane. This is what orients the measured albedo correctly for a
    // Benoni observer — altitude-up alone would put lunar north up on the disc, which the real
    // southern-hemisphere sky never does at moonrise.
    const SkyVector Pole = EquatorialToLocal(273.85 * DegToRad, 66.54 * DegToRad, LocalSiderealRad(Julian));
    const SkyVector ProjectedPole = Normalize(Pole - Frame.Observer * Dot(Pole, Frame.Observer));
    Frame.NorthRight = Dot(Frame.Right, ProjectedPole);
    Frame.NorthUp = Dot(Frame.Up, ProjectedPole);
    Frame.GeometricLatitudeRad = std::asin(std::clamp(Dot(Frame.Observer, Pole), -1.0, 1.0));
    const LunarLibration Libration = OpticalLibration(Julian);
    Frame.SubObserverLongitudeRad = Libration.SubObserverLongitudeRad;
    Frame.SubObserverLatitudeRad = Libration.SubObserverLatitudeRad;
    return Frame;
}

double HashLattice(int X, int Y, int Z)
{
    uint32_t H = static_cast<uint32_t>(X) * 374761393u + static_cast<uint32_t>(Y) * 668265263u +
                 static_cast<uint32_t>(Z) * 2147483647u;
    H = (H ^ (H >> 13)) * 1274126177u;
    H ^= H >> 16;
    return static_cast<double>(H & 0xFFFFFFu) / 16777215.0;
}

double ValueNoise(SkyVector P)
{
    const double Fx = std::floor(P.X), Fy = std::floor(P.Y), Fz = std::floor(P.Z);
    const int Ix = static_cast<int>(Fx), Iy = static_cast<int>(Fy), Iz = static_cast<int>(Fz);
    const double Tx = Smoothstep(0.0, 1.0, P.X - Fx), Ty = Smoothstep(0.0, 1.0, P.Y - Fy), Tz = Smoothstep(0.0, 1.0, P.Z - Fz);
    double Value = 0.0;
    for (int Corner = 0; Corner < 8; ++Corner)
    {
        const int Dx = Corner & 1, Dy = (Corner >> 1) & 1, Dz = (Corner >> 2) & 1;
        const double Weight = (Dx ? Tx : 1.0 - Tx) * (Dy ? Ty : 1.0 - Ty) * (Dz ? Tz : 1.0 - Tz);
        Value += Weight * HashLattice(Ix + Dx, Iy + Dy, Iz + Dz);
    }
    return Value;
}

// Cosine of incidence at the disc sample (A, B) in units of the angular radius; negative off-disc.
// Kept as the geometric oracle for the raster lit-fraction check: lit where this is positive.
double MoonDiscLambert(const MoonFrame& Frame, double A, double B, SkyVector* NormalOut = nullptr)
{
    const double Radicand = 1.0 - A * A - B * B;
    if (Radicand < 0.0) return -1.0;
    const SkyVector Normal = Frame.Right * A + Frame.Up * B + Frame.Observer * std::sqrt(Radicand);
    if (NormalOut) *NormalOut = Normal;
    return Dot(Normal, Frame.TowardSun);
}

// 📝 Textured disc radiance: production Lommel–Seeliger shading over the loaded demo map
//    (μ₀ from the oracle above, μ = √(1−A²−B²) by the disc construction), earthshine on the dark
//    side, then the frame's atmospheric tint toward the Moon. Camera disc coordinates (A, B) are
//    rotated into the projected selenographic axes the production sampler contracts for:
//    A_moon = A·NorthUp − B·NorthRight (toward lunar east), B_moon = A·NorthRight + B·NorthUp.
Radiance MoonDiscRadiance(const Frontier::LunarAlbedoMap& Map, const MoonFrame& Frame, double A, double B)
{
    const double CosIncidence = MoonDiscLambert(Frame, A, B);
    const double CosEmission  = std::sqrt(std::max(1.0 - A * A - B * B, 0.0));
    const double MoonA = A * Frame.NorthUp - B * Frame.NorthRight;
    const double MoonB = A * Frame.NorthRight + B * Frame.NorthUp;
    const Frontier::SpectrumRgb Lit = Frontier::EvaluateLunarDiscRadiance(
        Map, MoonA, MoonB, Frame.SubObserverLongitudeRad, Frame.SubObserverLatitudeRad, CosIncidence,
        CosEmission, Frame.PhaseAngleRad, Frontier::SpectrumRgb{10.0, 10.0, 10.0}, 0.030);
    return Radiance{Lit.R * Frame.Tint.R, Lit.G * Frame.Tint.G, Lit.B * Frame.Tint.B};
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   CAMERA AND CANVAS
//------------------------------------------------------------------------------------------------------------------------

struct SkyCamera
{
    SkyVector Forward;
    SkyVector Right;
    SkyVector Up;
    double Tangent = StandardHalfTangent;
    int Width = 0;
    int Height = 0;
};

SkyCamera MakeSkyCamera(SkyVector Aim, int Width, int Height, double Tangent)
{
    SkyCamera Camera;
    Camera.Forward = Normalize(Aim);
    Camera.Right = Normalize(Cross(Camera.Forward, {0.0, 0.0, 1.0}));
    Camera.Up = Cross(Camera.Right, Camera.Forward);
    Camera.Tangent = Tangent;
    Camera.Width = Width;
    Camera.Height = Height;
    return Camera;
}

// Height-normalised screen coordinates (X right, Y up, units of half the frame height).
void ScreenCoordinates(const SkyCamera& Camera, SkyVector D, double& X, double& Y)
{
    const double Depth = Dot(D, Camera.Forward);
    X = Dot(D, Camera.Right) / Depth / Camera.Tangent;
    Y = Dot(D, Camera.Up) / Depth / Camera.Tangent;
}

SkyVector PixelDirection(const SkyCamera& Camera, double Px, double Py)
{
    const double X = (Px - Camera.Width * 0.5) / (Camera.Height * 0.5);
    const double Y = (Camera.Height * 0.5 - Py) / (Camera.Height * 0.5);
    return Normalize(Camera.Forward + Camera.Right * (X * Camera.Tangent) + Camera.Up * (Y * Camera.Tangent));
}

struct Canvas
{
    int Width = 0;
    int Height = 0;
    std::vector<Radiance> Pixels;
    Canvas(int W, int H) : Width(W), Height(H), Pixels(static_cast<size_t>(W) * H) {}
    Radiance& At(int X, int Y) { return Pixels[static_cast<size_t>(Y) * Width + X]; }
    const Radiance& At(int X, int Y) const { return Pixels[static_cast<size_t>(Y) * Width + X]; }
};

// 📝 Display mapping: Narkowicz ACES filmic (the demonstration's ProjectFilmic in DisplayPresent.wgsl,
//    ported coefficient for coefficient) followed by the exact sRGB transfer, standing in for the
//    demonstration's sRGB swapchain view.
double ProjectFilmic(double Radiance)
{
    constexpr double A = 2.51, B = 0.03, C = 2.43, D = 0.59, E = 0.14;
    const double   X = std::max(Radiance, 0.0);
    return Clamp01((X * (A * X + B)) / (X * (C * X + D) + E));
}

uint8_t ToByte(double Linear, double Exposure)
{
    const double Mapped = ProjectFilmic(Exposure * std::max(Linear, 0.0));
    const double Srgb   = Mapped <= 0.0031308 ? 12.92 * Mapped : 1.055 * std::pow(Mapped, 1.0 / 2.4) - 0.055;
    return static_cast<uint8_t>(std::lround(Clamp01(Srgb) * 255.0));
}

bool WritePpm(const std::string& Path, const Canvas& Image, double Exposure)
{
    FILE* File = std::fopen(Path.c_str(), "wb");
    if (!File) return false;
    std::fprintf(File, "P6\n%d %d\n255\n", Image.Width, Image.Height);
    for (const Radiance& Pixel : Image.Pixels)
    {
        const uint8_t Bytes[3] = {ToByte(Pixel.R, Exposure), ToByte(Pixel.G, Exposure), ToByte(Pixel.B, Exposure)};
        std::fwrite(Bytes, 1, 3, File);
    }
    std::fclose(File);
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   SKY MODELS
//------------------------------------------------------------------------------------------------------------------------

// Night factor from solar elevation: 0 at civil twilight and above, 1 from astronomical twilight down.
double NightFactor(double SunElevationRad)
{
    return Smoothstep(-6.0 * DegToRad, -15.0 * DegToRad, SunElevationRad);
}

constexpr double SolarIrradianceTop = 10.0;   // [-] top-of-atmosphere sun irradiance, exhibit radiance units

// Multiple-scattering boost factor for one sun position. MultipleScatteringApprox returns its
// SingleScattered input times a factor gathered at the observer (its Direction argument is unused),
// so the factor is evaluated once per sun position with a unit input and reused per pixel exactly.
Frontier::SpectrumRgb MultipleFactorFor(const Frontier::AtmosphereParameters& Atmosphere, SkyVector Sun)
{
    const double GroundKm = Atmosphere.GroundRadiusKm;
    return Frontier::MultipleScatteringApprox(Atmosphere, {0.0, 0.0, GroundKm + 0.001}, {0.0, 0.0, 1.0},
                                              {Sun.X, Sun.Y, Sun.Z}, Frontier::SpectrumRgb{1.0, 1.0, 1.0},
                                              Frontier::SpectrumRgb{SolarIrradianceTop, SolarIrradianceTop,
                                                                    SolarIrradianceTop});
}

// Scattered sky (single + factored multiple scattering) for a view direction (ENU) and sun (ENU).
Radiance ScatteredSky(const Frontier::AtmosphereParameters& Atmosphere, const Frontier::TransmittanceLut& Lut,
                      const Frontier::SpectrumRgb& MultipleFactor, SkyVector D, SkyVector Sun)
{
    const double GroundKm = Atmosphere.GroundRadiusKm;
    const Frontier::SpectrumRgb Single = Frontier::SingleScattering(
        Atmosphere, {0.0, 0.0, GroundKm + 0.001}, {D.X, D.Y, D.Z}, {Sun.X, Sun.Y, Sun.Z},
        Frontier::SpectrumRgb{SolarIrradianceTop, SolarIrradianceTop, SolarIrradianceTop}, &Lut, 24);
    return Radiance{Single.R * (1.0 + MultipleFactor.R), Single.G * (1.0 + MultipleFactor.G),
                    Single.B * (1.0 + MultipleFactor.B)};
}

// Night background: airglow and urban glow near the horizon, plus the Milky Way band from galactic latitude and
// a brighter bulge toward the galactic centre, with dust lanes from procedural noise.
Radiance NightBackground(SkyVector D, SkyVector GalacticPole, SkyVector GalacticCentre, double Night)
{
    const double Latitude = std::asin(std::clamp(Dot(D, GalacticPole), -1.0, 1.0));
    const double Separation = std::acos(std::clamp(Dot(D, GalacticCentre), -1.0, 1.0));
    const double Lane = Smoothstep(0.45, 0.75, ValueNoise(D * 9.0)) * std::exp(-std::pow(Latitude / 0.07, 2.0));
    const double Band = std::exp(-std::pow(Latitude / 0.12, 2.0)) * (0.30 + 0.70 * std::exp(-std::pow(Separation / 0.55, 2.0)));
    const double Clouds = 0.55 + 0.45 * ValueNoise(D * 6.0);
    const double Milky = 0.060 * Band * Clouds * (1.0 - 0.75 * Lane);
    const double Warm = std::exp(-std::pow(Separation / 0.8, 2.0));
    const Radiance Milk{0.80 + 0.2 * Warm, 0.86 + 0.06 * Warm, 1.0 - 0.22 * Warm};
    const double Altitude = std::asin(std::clamp(D.Z, -1.0, 1.0));
    const Radiance Glow{0.0016 + 0.010 * std::exp(-Altitude / 0.07), 0.0020 + 0.0075 * std::exp(-Altitude / 0.07),
                        0.0036 + 0.0045 * std::exp(-Altitude / 0.07)};
    return (Glow + Milk * Milky) * Night;
}

// Gaussian splat with peak from magnitude and colour from B-V; scaled to the image size.
struct StarSplat
{
    double Px = 0.0, Py = 0.0, Sigma = 1.0, Peak = 0.0;
    Radiance Colour{};
};

void SplatStars(Canvas& Image, const std::vector<StarSplat>& Splats, double Scale,
                const std::vector<double>* Occlusion = nullptr)
{
    for (const StarSplat& Splat : Splats)
    {
        const double Sigma = Splat.Sigma * Scale;
        const int Reach = static_cast<int>(std::ceil(3.0 * Sigma));
        const int CentreX = static_cast<int>(std::lround(Splat.Px));
        const int CentreY = static_cast<int>(std::lround(Splat.Py));
        for (int Y = std::max(0, CentreY - Reach); Y <= std::min(Image.Height - 1, CentreY + Reach); ++Y)
            for (int X = std::max(0, CentreX - Reach); X <= std::min(Image.Width - 1, CentreX + Reach); ++X)
            {
                const double Dx = X + 0.5 - Splat.Px, Dy = Y + 0.5 - Splat.Py;
                const double Weight = std::exp(-(Dx * Dx + Dy * Dy) / (2.0 * Sigma * Sigma));
                const double Visible = Occlusion == nullptr ? 1.0 :
                    1.0 - (*Occlusion)[static_cast<size_t>(Y)*Image.Width+X];
                Image.At(X, Y) = Image.At(X, Y) + Splat.Colour * (Splat.Peak * Weight * Visible);
            }
    }
}

std::vector<StarSplat> BuildStarSplats(const std::vector<CatalogueStar>& Catalogue, const SkyCamera& Camera,
                                       double LocalSidereal, double Night)
{
    std::vector<StarSplat> Splats;
    Splats.reserve(Catalogue.size());
    for (const CatalogueStar& Star : Catalogue)
    {
        const EquatorialPoint Date = PrecessFromJ2000(Star.RightAscensionRad, Star.DeclinationRad, CatalogueEpochYearsToDate);
        const SkyVector D = EquatorialToLocal(Date.RightAscensionRad, Date.DeclinationRad, LocalSidereal);
        if (D.Z < -0.02 || Dot(D, Camera.Forward) <= 0.0) continue;
        double Sx = 0.0, Sy = 0.0;
        ScreenCoordinates(Camera, D, Sx, Sy);
        const double AspectHalf = static_cast<double>(Camera.Width) / Camera.Height;
        if (std::fabs(Sx) > 1.05 * AspectHalf || std::fabs(Sy) > 1.05) continue;
        StarSplat Splat;
        Splat.Px = Sx * Camera.Height * 0.5 + Camera.Width * 0.5;
        Splat.Py = Camera.Height * 0.5 - Sy * Camera.Height * 0.5;
        Splat.Sigma = 0.70 + 0.30 * std::clamp(3.0 - Star.Magnitude, 0.0, 4.0);
        Splat.Peak = Night * 6.0 * std::pow(10.0, -0.4 * (Star.Magnitude + 1.0));
        Splat.Colour = StarColour(Star.ColourBv);
        Splats.push_back(Splat);
    }
    return Splats;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   SCENES
//------------------------------------------------------------------------------------------------------------------------

struct Counter
{
    int Passed = 0;
    int Failed = 0;
    void Check(const char* Name, bool Ok, double Value)
    {
        std::printf("%s %s = %.6g\n", Ok ? "PASS" : "FAIL", Name, Value);
        Ok ? ++Passed : ++Failed;
    }
};

struct SceneLight
{
    SkyVector Sun;
    SkyVector Moon;
    double SunElevationRad = 0.0;
    double Night = 1.0;
    Radiance MoonTint{1.0, 1.0, 1.0};   // [-] atmospheric transmittance toward the Moon
    double MoonAngularRadius = 0.0;     // [rad] apparent radius at this observer
};

// Night field with the Milky Way, the Hipparcos catalogue and the Moon disc.
Canvas RenderNightField(SkyVector Aim, int Width, int Height, double Tangent, double LocalSidereal,
                        double Julian, const SceneLight& Light, const std::vector<CatalogueStar>& Catalogue,
                        const Frontier::LunarAlbedoMap& MoonMap, uint32_t* StarCountOut)
{
    Canvas Image(Width, Height);
    const SkyCamera Camera = MakeSkyCamera(Aim, Width, Height, Tangent);
    const SkyVector GalacticPole = EquatorialToLocal(192.85948 * DegToRad, 27.12825 * DegToRad, LocalSidereal);
    const SkyVector GalacticCentre = EquatorialToLocal(266.40499 * DegToRad, -28.93618 * DegToRad, LocalSidereal);
    MoonFrame Frame = MakeMoonFrame(Light.Moon, Light.Sun, Julian);
    Frame.Tint = Light.MoonTint;
    const double Scale = static_cast<double>(Width) / 960.0;
    std::vector<double> MoonCoverage(static_cast<size_t>(Width)*Height, 0.0);

    constexpr int Samples = 2;   // 2x2 supersampling for disc edges and star cores
    for (int Y = 0; Y < Height; ++Y)
        for (int X = 0; X < Width; ++X)
        {
            Radiance Accumulated{};
            for (int Sy = 0; Sy < Samples; ++Sy)
                for (int Sx = 0; Sx < Samples; ++Sx)
                {
                    const SkyVector D = PixelDirection(Camera, X + (Sx + 0.5) / Samples, Y + (Sy + 0.5) / Samples);
                    Radiance Value = NightBackground(D, GalacticPole, GalacticCentre, Light.Night);
                    const double MoonCos = Dot(D, Light.Moon);
                    if (Light.MoonAngularRadius > 0.0 && MoonCos > std::cos(Light.MoonAngularRadius * 1.25))
                    {
                        const double A = Dot(D, Frame.Right) / std::sin(Light.MoonAngularRadius);
                        const double B = Dot(D, Frame.Up) / std::sin(Light.MoonAngularRadius);
                        if (A * A + B * B <= 1.0)
                        {
                            Value = MoonDiscRadiance(MoonMap, Frame, A, B);
                            MoonCoverage[static_cast<size_t>(Y)*Width+X] += 1.0/(Samples*Samples);
                        }
                    }
                    Accumulated = Accumulated + Value;
                }
            Image.At(X, Y) = Accumulated * (1.0 / (Samples * Samples));
        }

    const std::vector<StarSplat> Splats = BuildStarSplats(Catalogue, Camera, LocalSidereal, Light.Night);
    SplatStars(Image, Splats, Scale, &MoonCoverage);
    if (StarCountOut) *StarCountOut = static_cast<uint32_t>(Splats.size());
    return Image;
}

// Moon close-up: disc fills most of the frame, so the terminator and surface shading can be inspected.
// Disc radius is 0.42 of the frame half-height by construction (tangent = angular radius / 0.42).
Canvas RenderMoonCloseUp(SkyVector MoonDirection, SkyVector Sun, int Size, double Julian, double MoonAngularRadius,
                         const Frontier::LunarAlbedoMap& MoonMap, Radiance MoonTint, int* DiscPixelsOut)
{
    Canvas Image(Size, Size);
    const double Tangent = std::tan(MoonAngularRadius) / 0.42;
    const SkyCamera Camera = MakeSkyCamera(MoonDirection, Size, Size, Tangent);
    MoonFrame Frame = MakeMoonFrame(MoonDirection, Sun, Julian);
    Frame.Tint = MoonTint;
    for (int Y = 0; Y < Size; ++Y)
        for (int X = 0; X < Size; ++X)
        {
            const SkyVector D = PixelDirection(Camera, X + 0.5, Y + 0.5);
            const double A = Dot(D, Frame.Right) / std::sin(MoonAngularRadius);
            const double B = Dot(D, Frame.Up) / std::sin(MoonAngularRadius);
            Radiance Value{0.0012, 0.0015, 0.0028};
            if (A * A + B * B <= 1.0)
            {
                Value = MoonDiscRadiance(MoonMap, Frame, A, B);
                if (DiscPixelsOut) ++*DiscPixelsOut;
            }
            Image.At(X, Y) = Value;
        }
    return Image;
}

// Four phases of the cycle containing the date, each as a centred disc with the Sun's real lit side.
// No atmospheric tint here: the strip is a phase-geometry study, stated as such in the README.
Canvas RenderMoonPhases(double JulianDay, int TileSize, const Frontier::LunarAlbedoMap& MoonMap,
                        std::vector<double>& Geometric, std::vector<double>& Raster,
                        std::vector<double>& ModelFraction)
{
    constexpr int TileCount = 4;
    const double TileRadius = TileSize * 0.42;
    Canvas Image(TileSize * TileCount, TileSize);
    const double CycleStart = Frontier::NewMoonEpochJulian +
                              std::floor((JulianDay - Frontier::NewMoonEpochJulian) / Frontier::SynodicMonthDays) *
                                  Frontier::SynodicMonthDays;
    for (int Tile = 0; Tile < TileCount; ++Tile)
    {
        const double PhaseJulian = CycleStart + Frontier::SynodicMonthDays * Tile / 4.0;
        const double Lst = LocalSiderealRad(PhaseJulian);
        const SkyVector Moon = MoonDirectionAtBenoni(PhaseJulian);
        const SkyVector Sun = EquatorialToLocal(SunEquatorial(PhaseJulian).RightAscensionRad,
                                                SunEquatorial(PhaseJulian).DeclinationRad, Lst);
        const MoonFrame Frame = MakeMoonFrame(Moon, Sun, PhaseJulian);
        double Lit = 0.0, Samples = 0.0;
        for (int Y = 0; Y < TileSize; ++Y)
            for (int X = 0; X < TileSize; ++X)
            {
                const double A = (X + 0.5 - TileSize * 0.5) / TileRadius;
                const double B = (TileSize * 0.5 - (Y + 0.5)) / TileRadius;
                Radiance Value{0.0012, 0.0015, 0.0028};
                if (A * A + B * B <= 1.0)
                {
                    Value = MoonDiscRadiance(MoonMap, Frame, A, B);
                    Lit += MoonDiscLambert(Frame, A, B) > 0.0 ? 1.0 : 0.0;
                    Samples += 1.0;
                }
                Image.At(Tile * TileSize + X, Y) = Value;
            }
        Geometric.push_back(0.5 * (1.0 - std::clamp(Dot(Sun, Moon), -1.0, 1.0)));
        Raster.push_back(Lit / Samples);
        ModelFraction.push_back(Frontier::MoonPhase(PhaseJulian).Illumination);
    }
    return Image;
}

// 📝 Sun quantities shared by the flare scenes: transmittance toward the sun from the LUT at the
//    observer, disc-centre radiance (top-of-atmosphere irradiance spread over the limb-darkened
//    disc's solid angle), and the disc-integrated irradiance at the lens that scales every flare term.
struct SunRadianceSetup
{
    Frontier::SpectrumRgb CentreRadiance{};
    Frontier::SpectrumRgb DiscIrradiance{};
    Frontier::SpectrumRgb Tint{};
};

SunRadianceSetup SetupSunRadiance(const Frontier::AtmosphereParameters& Atmosphere,
                                  const Frontier::TransmittanceLut& Lut, SkyVector Sun,
                                  const Frontier::SunDiscProfile& DiscProfile)
{
    SunRadianceSetup Setup;
    Setup.Tint = Lut.Sample(Atmosphere, Atmosphere.GroundRadiusKm + 0.001, std::clamp(Sun.Z, -1.0, 1.0));
    const double CentreScale = SolarIrradianceTop / (Frontier::SunDiscSolidAngleSr(DiscProfile.AngularRadiusRad) *
                                                     Frontier::SunDiscMeanLimbFactor(DiscProfile));
    Setup.CentreRadiance = {CentreScale * Setup.Tint.R, CentreScale * Setup.Tint.G, CentreScale * Setup.Tint.B};
    Setup.DiscIrradiance = {SolarIrradianceTop * Setup.Tint.R, SolarIrradianceTop * Setup.Tint.G,
                            SolarIrradianceTop * Setup.Tint.B};
    return Setup;
}

// Sun disc and full lens flare on the scattering sky. Ghost positions come from LensFlareModel and
// are measured in the rendered image. OnlyGhost >= 0 keeps a single aperture ghost (and no other
// flare term), so its contribution can be isolated for the rendering-level centroid check.
Canvas RenderSunFlare(SkyVector Aim, SkyVector Sun, double Visibility, const Frontier::AtmosphereParameters& Atmosphere,
                      const Frontier::TransmittanceLut& Lut, std::array<Frontier::FlareGhost, 6>* GhostsOut,
                      int OnlyGhost = -1)
{
    const int Width = 1280, Height = 800;
    Canvas Image(Width, Height);
    const SkyCamera Camera = MakeSkyCamera(Aim, Width, Height, StandardHalfTangent);
    double SunX = 0.0, SunY = 0.0;
    ScreenCoordinates(Camera, Sun, SunX, SunY);
    const std::array<Frontier::FlareGhost, 6> Ghosts = Frontier::LayoutFlareGhosts(SunX, SunY, Visibility);
    if (GhostsOut) *GhostsOut = Ghosts;
    const Frontier::SpectrumRgb Multiple = MultipleFactorFor(Atmosphere, Sun);
    Frontier::SunDiscProfile DiscProfile;
    DiscProfile.EdgeBlurRad = 0.75 * StandardHalfTangent / (Height * 0.5);   // [rad] ~0.75 px σ
    const SunRadianceSetup SunSetup = SetupSunRadiance(Atmosphere, Lut, Sun, DiscProfile);
    Frontier::FlareOpticsProfile Optics;
    Optics.StreakStrength = 0.35;
    for (int Y = 0; Y < Height; ++Y)
        for (int X = 0; X < Width; ++X)
        {
            const SkyVector D = PixelDirection(Camera, X + 0.5, Y + 0.5);
            Radiance Value = ScatteredSky(Atmosphere, Lut, Multiple, D, Sun);
            const double Theta = std::acos(std::clamp(Dot(D, Sun), -1.0, 1.0));
            // 📝 OnlyGhost is the centroid-measurement mode: sky plus the one ghost, nothing else, so
            //    the residual against the visibility-0 frame isolates that ghost alone. (With the disc
            //    visibility-gated it would otherwise survive the subtraction and drag the centroid.)
            if (OnlyGhost < 0)
            {
                const Frontier::SpectrumRgb Disc = Frontier::EvaluateSunDiscRadiance(Theta, DiscProfile,
                                                                                     SunSetup.CentreRadiance);
                Value = Value + Radiance{Disc.R, Disc.G, Disc.B} * Visibility;
            }
            double Px = 0.0, Py = 0.0;
            ScreenCoordinates(Camera, D, Px, Py);
            const Frontier::SpectrumRgb Flare =
                OnlyGhost >= 0 ? Frontier::EvaluateFlareGhostRadiance(Px, Py, SunX, SunY, SunSetup.DiscIrradiance,
                                                                      Visibility, Optics, OnlyGhost)
                               : Frontier::EvaluateLensFlareRadiance(Px, Py, SunX, SunY, SunSetup.DiscIrradiance,
                                                                     Visibility, Optics);
            Value = Value + Radiance{Flare.R, Flare.G, Flare.B};
            Image.At(X, Y) = Value;
        }
    return Image;
}

// Telephoto sun: 3.2° vertical field so the limb darkening, blurred analytic limb, diffraction
// spikes and anamorphic streak can be inspected at disc scale (disc ≈ 120 px of 720).
// FilteredExposureOut receives the exposure that places disc centre at ACES input 0.6 — the
// solar-filter view in which the limb law itself is visible (sky falls to black, as it must:
// the disc outshines the sky by ~10⁵, which no single photographic exposure spans).
Canvas RenderSunDiscClose(SkyVector Sun, const Frontier::AtmosphereParameters& Atmosphere,
                          const Frontier::TransmittanceLut& Lut, double* FilteredExposureOut)
{
    const int Width = 960, Height = 720;
    const double Tangent = std::tan(1.6 * DegToRad);
    Canvas Image(Width, Height);
    const SkyVector East = Normalize(Cross(Sun, {0.0, 0.0, 1.0}));
    const SkyVector Up = Cross(East, Sun);
    const SkyVector Aim = Normalize(Sun - East * (0.16 * Tangent) - Up * (0.10 * Tangent));
    const SkyCamera Camera = MakeSkyCamera(Aim, Width, Height, Tangent);
    double SunX = 0.0, SunY = 0.0;
    ScreenCoordinates(Camera, Sun, SunX, SunY);
    const Frontier::SpectrumRgb Multiple = MultipleFactorFor(Atmosphere, Sun);
    Frontier::SunDiscProfile DiscProfile;
    DiscProfile.EdgeBlurRad = 0.9 * Tangent / (Height * 0.5);   // [rad] ~0.9 px σ at this field
    const SunRadianceSetup SunSetup = SetupSunRadiance(Atmosphere, Lut, Sun, DiscProfile);
    if (FilteredExposureOut)
    {
        // 📝 Centre at ACES input 0.6, not deep in the shoulder: limb darkening only spans a 2.6:1
        //    radiance range, and at 3.2 the filmic shoulder compresses it invisibly (measured
        //    profile 234 → 216 → 151 across the limb at centre 1.1; 0.6 reads at a glance).
        *FilteredExposureOut = 0.6 / std::max({SunSetup.CentreRadiance.R, SunSetup.CentreRadiance.G,
                                               SunSetup.CentreRadiance.B, 1e-9});
    }
    Frontier::FlareOpticsProfile Optics;
    Optics.GhostStrength = 0.35;   // ghosts crowd the centre at this field; spikes and streak lead
    for (int Y = 0; Y < Height; ++Y)
        for (int X = 0; X < Width; ++X)
        {
            const SkyVector D = PixelDirection(Camera, X + 0.5, Y + 0.5);
            Radiance Value = ScatteredSky(Atmosphere, Lut, Multiple, D, Sun);
            const double Theta = std::acos(std::clamp(Dot(D, Sun), -1.0, 1.0));
            const Frontier::SpectrumRgb Disc = Frontier::EvaluateSunDiscRadiance(Theta, DiscProfile,
                                                                                 SunSetup.CentreRadiance);
            Value = Value + Radiance{Disc.R, Disc.G, Disc.B};
            double Px = 0.0, Py = 0.0;
            ScreenCoordinates(Camera, D, Px, Py);
            const Frontier::SpectrumRgb Flare =
                Frontier::EvaluateLensFlareRadiance(Px, Py, SunX, SunY, SunSetup.DiscIrradiance, 1.0, Optics);
            Value = Value + Radiance{Flare.R, Flare.G, Flare.B};
            Image.At(X, Y) = Value;
        }
    return Image;
}

// Dusk sweep: sun elevation at a fixed azimuth, with the same scattering model and night fade.
Canvas RenderDuskFrame(double SunElevationRad, const Frontier::AtmosphereParameters& Atmosphere,
                       const Frontier::TransmittanceLut& Lut, const SkyVector& GalacticPole,
                       const SkyVector& GalacticCentre, const std::vector<CatalogueStar>& Catalogue, double LocalSidereal)
{
    constexpr int Width = 480, Height = 300;
    Canvas Image(Width, Height);
    const SkyVector Sun{std::cos(SunElevationRad) * std::sin(270.0 * DegToRad),
                        std::cos(SunElevationRad) * std::cos(270.0 * DegToRad), std::sin(SunElevationRad)};
    const SkyVector Aim{std::cos(15.0 * DegToRad) * std::sin(270.0 * DegToRad),
                        std::cos(15.0 * DegToRad) * std::cos(270.0 * DegToRad), std::sin(15.0 * DegToRad)};
    const SkyCamera Camera = MakeSkyCamera(Aim, Width, Height, 0.40);
    const double Night = NightFactor(SunElevationRad);
    const Frontier::SpectrumRgb Multiple = MultipleFactorFor(Atmosphere, Sun);
    for (int Y = 0; Y < Height; ++Y)
        for (int X = 0; X < Width; ++X)
        {
            const SkyVector D = PixelDirection(Camera, X + 0.5, Y + 0.5);
            Radiance Value{0.03, 0.025, 0.02};   // unlit ground below the horizon
            if (D.Z >= 0.0)
            {
                const double DaySky = 1.0 - Night;
                Value = ScatteredSky(Atmosphere, Lut, Multiple, D, Sun) * DaySky;
                Value = Value + NightBackground(D, GalacticPole, GalacticCentre, Night);
            }
            Image.At(X, Y) = Value;
        }
    std::vector<StarSplat> Splats = BuildStarSplats(Catalogue, Camera, LocalSidereal, Night);
    SplatStars(Image, Splats, 0.5);
    return Image;
}

} // namespace

int main(int ArgumentCount, char** Arguments)
{
    const std::string OutputPrefix = ArgumentCount > 1 ? Arguments[1] : "Capture/";
    const std::string CataloguePath = ArgumentCount > 2 ? Arguments[2] : "Data/Hipparcos8.csv";
    const std::string MoonMapPath = ArgumentCount > 3 ? Arguments[3] : "EngineContent/CelestialMaps/MoonAlbedo2048.ppm";
    Counter Check;

    const double NightJulian = Frontier::JulianDate(2026, 10, 26, 18.5);   // 20:30 SAST
    const double DayJulian = Frontier::JulianDate(2026, 10, 26, 14.5);     // 16:30 SAST
    const double NightLst = LocalSiderealRad(NightJulian);
    const double DayLst = LocalSiderealRad(DayJulian);

    // Catalogue.
    const std::vector<CatalogueStar> Catalogue = LoadHipparcosCatalogue(CataloguePath);
    Check.Check("Hipparcos catalogue loaded (stars)", Catalogue.size() > 40000, static_cast<double>(Catalogue.size()));

    // IEC sRGB decoding must happen before bilinear interpolation, matching rgba8unorm-srgb in the demo.
    {
        const std::string Path = OutputPrefix + "SrgbProbe.ppm";
        FILE* File = std::fopen(Path.c_str(), "wb");
        if (File == nullptr) return 2;
        std::fprintf(File, "P6\n4 2\n255\n");
        for (int Pixel = 0; Pixel < 8; ++Pixel)
        {
            const unsigned char Grey = Pixel % 4 < 2 ? 0 : 255;
            for (int Channel = 0; Channel < 3; ++Channel) std::fputc(Grey, File);
        }
        std::fclose(File);
        Frontier::LunarAlbedoMap Probe;
        const bool Loaded = Probe.LoadPpm(Path.c_str());
        const double Midpoint = Probe.Sample(0.0, 0.0).R;
        Check.Check("lunar interpolation occurs in linear space", Loaded && std::abs(Midpoint - 0.5) < 1e-14,
                    Midpoint);
        File = std::fopen(Path.c_str(), "wb");
        if (File == nullptr) return 2;
        std::fprintf(File, "P6\n4 2\n255\n");
        for (int Byte = 0; Byte < 24; ++Byte) std::fputc(128, File);
        std::fclose(File);
        const bool GreyLoaded = Probe.LoadPpm(Path.c_str());
        const double GreyLinear = Probe.Sample(0.0, 0.0).R;
        Check.Check("lunar sRGB mid-grey decodes to 0.21586", GreyLoaded && std::abs(GreyLinear - 0.2158605001) < 1e-10,
                    GreyLinear);
    }

    // Measured lunar albedo map: named-feature samples pin the selenographic orientation
    // (Tycho bright ray crater, Plato dark floor, Mare Crisium dark against its highlands).
    Frontier::LunarAlbedoMap MoonMap;
    const bool MoonMapLoaded = MoonMap.LoadPpm(MoonMapPath.c_str());
    Check.Check("lunar albedo map loaded (2048x1024)", MoonMapLoaded && MoonMap.Width() == 2048 && MoonMap.Height() == 1024,
                MoonMapLoaded ? static_cast<double>(MoonMap.Width()) : 0.0);
    const double TychoAlbedo = MoonMap.Sample(-11.0 * DegToRad, -43.0 * DegToRad).R;
    const double PlatoAlbedo = MoonMap.Sample(-9.0 * DegToRad, 51.0 * DegToRad).R;
    const double CrisiumAlbedo = MoonMap.Sample(59.0 * DegToRad, 17.0 * DegToRad).R;
    Check.Check("Tycho samples brighter than Plato (orientation)", TychoAlbedo > 2.0 * PlatoAlbedo,
                TychoAlbedo / std::max(PlatoAlbedo, 1e-9));
    Check.Check("Mare Crisium samples dark against Tycho", CrisiumAlbedo < 0.6 * TychoAlbedo, CrisiumAlbedo);
    std::printf("INFO moon map Tycho %.3f Plato %.3f Crisium %.3f\n", TychoAlbedo, PlatoAlbedo, CrisiumAlbedo);
    // Lommel–Seeliger flatness: with the featureless fallback (uniform albedo) a full Moon must be
    // exactly as bright at 0.7 radii as at centre — the property Lambert shading fails.
    const Frontier::LunarAlbedoMap UniformMap;
    const double FullCentre = Frontier::EvaluateLunarDiscRadiance(UniformMap, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 0.0,
                                                                  Frontier::SpectrumRgb{10.0, 10.0, 10.0}, 0.0).R;
    const double CosAt07 = std::sqrt(1.0 - 0.49);
    const double FullLimb = Frontier::EvaluateLunarDiscRadiance(UniformMap, 0.7, 0.0, 0.0, 0.0, CosAt07, CosAt07, 0.0,
                                                                Frontier::SpectrumRgb{10.0, 10.0, 10.0}, 0.0).R;
    Check.Check("full disc flat under Lommel-Seeliger", std::fabs(FullLimb / FullCentre - 1.0) < 1e-9,
                FullLimb / FullCentre);
    const double DarkSide = Frontier::EvaluateLunarDiscRadiance(UniformMap, 0.0, 0.0, 0.0, 0.0, -0.5, 1.0, Pi,
                                                                Frontier::SpectrumRgb{10.0, 10.0, 10.0}, 0.030).B;
    Check.Check("earthshine present and faint on dark side", DarkSide > 0.0 && DarkSide < 0.02 * FullCentre, DarkSide);
    const double Quarter = Frontier::EvaluateLunarDiscRadiance(UniformMap, 0.0, 0.0, 0.0, 0.0, 0.8, 0.8, 0.5 * Pi,
                                                               Frontier::SpectrumRgb{10.0, 10.0, 10.0}, 0.0).R;
    const double FullSame = Frontier::EvaluateLunarDiscRadiance(UniformMap, 0.0, 0.0, 0.0, 0.0, 0.8, 0.8, 0.0,
                                                                Frontier::SpectrumRgb{10.0, 10.0, 10.0}, 0.0).R;
    Check.Check("quarter phase dimming exp(-1.10 g)", std::fabs(Quarter / FullSame - std::exp(-1.10 * 0.5 * Pi)) < 0.01,
                Quarter / FullSame);

    // Sun: ENU sun matches CelestialModel at Benoni.
    const Frontier::SolarPosition CoreSun = Frontier::SunPosition(DayJulian, BenoniLatitudeDeg, BenoniLongitudeDeg);
    const SkyVector DaySun = EquatorialToLocal(SunEquatorial(DayJulian).RightAscensionRad,
                                               SunEquatorial(DayJulian).DeclinationRad, DayLst);
    const double SunError = std::fabs(std::asin(DaySun.Z) - CoreSun.ElevationRad) / DegToRad;
    Check.Check("sun elevation vs CelestialModel (deg)", SunError < 0.05, SunError);
    std::printf("INFO sun at 16:30 SAST: elevation %.3f deg, azimuth %.3f deg\n", CoreSun.ElevationRad / DegToRad,
                CoreSun.AzimuthRad / DegToRad);

    // Equatorial round trip.
    double RaBack = 0.0, DecBack = 0.0;
    LocalToEquatorial(EquatorialToLocal(1.2, -0.4, NightLst), NightLst, RaBack, DecBack);
    const double RoundTrip = std::fabs(std::remainder(RaBack - 1.2, 2.0 * Pi)) + std::fabs(DecBack + 0.4);
    Check.Check("equatorial round trip (rad)", RoundTrip < 1e-9, RoundTrip);

    // Atmosphere: LUT bake and sky colour sanity (zenith blue, forward scattering toward the sun).
    Frontier::AtmosphereParameters Atmosphere;
    // 📝 Demonstration parity: AtmosphereScattering.wgsl carries Mie extinction 4.40e-6/m in total
    //    (scattering 3.996e-6 + absorption 0.404e-6). This pins the production split to that total.
    Check.Check("Mie extinction total matches demonstration (1/km)",
                std::fabs(Atmosphere.MieScattering + Atmosphere.MieAbsorption - 4.40e-3) < 1e-9,
                Atmosphere.MieScattering + Atmosphere.MieAbsorption);
    Frontier::TransmittanceLut Lut;
    Check.Check("transmittance LUT baked", Lut.Bake(Atmosphere), 1.0);
    const Frontier::SpectrumRgb DayMultiple = MultipleFactorFor(Atmosphere, DaySun);
    const Radiance Zenith = ScatteredSky(Atmosphere, Lut, DayMultiple, {0.0, 0.0, 1.0}, DaySun);
    const SkyVector AwayFromSun = Normalize({-DaySun.X, -DaySun.Y, 0.5});   // antisolar, about 30 deg up
    const Radiance Toward = ScatteredSky(Atmosphere, Lut, DayMultiple, DaySun, DaySun);
    const Radiance Opposite = ScatteredSky(Atmosphere, Lut, DayMultiple, AwayFromSun, DaySun);
    std::printf("INFO zenith radiance %.4f %.4f %.4f, toward sun %.4f %.4f %.4f, multiple factor %.3f %.3f %.3f\n",
                Zenith.R, Zenith.G, Zenith.B, Toward.R, Toward.G, Toward.B, DayMultiple.R, DayMultiple.G, DayMultiple.B);
    Check.Check("zenith sky is blue (B over R)", Zenith.B > Zenith.R, Zenith.B / Zenith.R);
    Check.Check("sky brighter toward the sun than away", Toward.R + Toward.G + Toward.B > Opposite.R + Opposite.G + Opposite.B,
                (Toward.R + Toward.G + Toward.B) / (Opposite.R + Opposite.G + Opposite.B));

    // Moon: geometry, raster, and the mean-age comparison as information only.
    std::vector<double> Geometric, Raster, ModelFraction;
    const Canvas Phases = RenderMoonPhases(NightJulian, 300, MoonMap, Geometric, Raster, ModelFraction);
    double WorstRaster = 0.0, WorstModel = 0.0;
    for (size_t I = 0; I < Geometric.size(); ++I)
    {
        WorstRaster = std::max(WorstRaster, std::fabs(Raster[I] - Geometric[I]));
        WorstModel = std::max(WorstModel, std::fabs(Geometric[I] - ModelFraction[I]));
        std::printf("INFO phase %zu geometric %.4f raster %.4f mean-age %.4f\n", I, Geometric[I], Raster[I], ModelFraction[I]);
    }
    Check.Check("moon raster lit fraction vs geometry", WorstRaster < 0.01, WorstRaster);
    std::printf("INFO moon geometry vs mean-age model, worst gap %.4f (model difference, not a render check)\n", WorstModel);
    Check.Check("phase strip runs new (0) to full (2)", Geometric[0] < 0.1 && Geometric[2] > 0.9, Geometric[2]);

    const auto NightMoonPosition = MoonAtBenoni(NightJulian);
    const SkyVector NightMoon{NightMoonPosition.Direction.X, NightMoonPosition.Direction.Y, NightMoonPosition.Direction.Z};
    const SkyVector NightSun = EquatorialToLocal(SunEquatorial(NightJulian).RightAscensionRad,
                                                 SunEquatorial(NightJulian).DeclinationRad, NightLst);
    const Frontier::SpectrumRgb MoonTintRgb =
        Lut.Sample(Atmosphere, Atmosphere.GroundRadiusKm + 0.001, std::clamp(NightMoon.Z, -1.0, 1.0));
    const Radiance NightMoonTint{MoonTintRgb.R, MoonTintRgb.G, MoonTintRgb.B};
    const SceneLight NightLight{NightSun, NightMoon, std::asin(NightSun.Z), NightFactor(std::asin(NightSun.Z)),
                                NightMoonTint, NightMoonPosition.AngularRadiusRad};
    std::printf("INFO moon at 20:30 SAST: altitude %.2f deg, angular diameter %.1f arcsec, illuminated %.3f\n",
                std::asin(NightMoon.Z) / DegToRad, 2.0 * NightMoonPosition.AngularRadiusRad / ArcsecToRad,
                0.5 * (1.0 - Dot(NightSun, NightMoon)));
    std::printf("INFO geocentric phase fraction %.3f (topocentric differs slightly)\n",
                GeometricIlluminatedFraction(SunEquatorial(NightJulian), MoonEquatorial(NightJulian)));
    // Optical libration: Meeus latitude must agree with the independent pole-geometry latitude,
    // and the longitude must sit inside the physical optical-libration range (±7.9°).
    const MoonFrame LibrationFrame = MakeMoonFrame(NightMoon, NightSun, NightJulian);
    Check.Check("libration latitude vs pole geometry (deg)",
                std::fabs(LibrationFrame.SubObserverLatitudeRad - LibrationFrame.GeometricLatitudeRad) < 1.5 * DegToRad,
                (LibrationFrame.SubObserverLatitudeRad - LibrationFrame.GeometricLatitudeRad) / DegToRad);
    Check.Check("libration longitude within optical range (deg)",
                std::fabs(LibrationFrame.SubObserverLongitudeRad) < 8.5 * DegToRad,
                LibrationFrame.SubObserverLongitudeRad / DegToRad);
    std::printf("INFO libration sub-observer lon %.2f lat %.2f deg, pole position components (%.3f, %.3f)\n",
                LibrationFrame.SubObserverLongitudeRad / DegToRad, LibrationFrame.SubObserverLatitudeRad / DegToRad,
                LibrationFrame.NorthRight, LibrationFrame.NorthUp);

    // Moon close-up: check the disc area in pixels against pi r^2.
    const int CloseSize = 720;
    int DiscPixels = 0;
    const Canvas Close = RenderMoonCloseUp(NightMoon, NightSun, CloseSize, NightJulian,
                                           NightMoonPosition.AngularRadiusRad, MoonMap, NightMoonTint, &DiscPixels);
    const double ClosePixelRadius = 0.42 * CloseSize * 0.5;
    const double ExpectedDiscPixels = Pi * ClosePixelRadius * ClosePixelRadius;
    Check.Check("moon close-up disc area vs pi r^2", std::fabs(DiscPixels - ExpectedDiscPixels) < 0.03 * ExpectedDiscPixels,
                DiscPixels / ExpectedDiscPixels);

    // Night field: Milky Way aimed at the brightest band point above 25 degrees, and a field centred on the Moon.
    SkyVector BestAim{0.0, 1.0, 0.0};
    double BestScore = -1.0;
    for (double Azimuth = 0.0; Azimuth < 360.0; Azimuth += 2.0)
        for (double Altitude = 25.0; Altitude <= 45.0; Altitude += 2.0)
        {
            const double Az = Azimuth * DegToRad, Al = Altitude * DegToRad;
            const SkyVector Aim{std::cos(Al) * std::sin(Az), std::cos(Al) * std::cos(Az), std::sin(Al)};
            const SkyVector Pole = EquatorialToLocal(192.85948 * DegToRad, 27.12825 * DegToRad, NightLst);
            const double Latitude = std::asin(std::clamp(Dot(Aim, Pole), -1.0, 1.0));
            const double Score = std::exp(-std::pow(Latitude / 0.12, 2.0));
            if (Score > BestScore) { BestScore = Score; BestAim = Aim; }
        }
    uint32_t StarCountMilky = 0, StarCountMoon = 0;
    const Canvas MilkyWay = RenderNightField(BestAim, 1280, 800, StandardHalfTangent * 1.4, NightLst, NightJulian, NightLight, Catalogue,
                                             MoonMap, &StarCountMilky);
    Check.Check("night field stars drawn (Milky Way aim)", StarCountMilky > 3000, static_cast<double>(StarCountMilky));
    const Canvas MoonField = RenderNightField(NightMoon, 1280, 800, StandardHalfTangent * 1.8, NightLst, NightJulian, NightLight,
                                              Catalogue, MoonMap, &StarCountMoon);
    // Catalogue density depends on the Moon's pointing; this check only guards against an empty field.
    Check.Check("moon field stars drawn", StarCountMoon > 2000, static_cast<double>(StarCountMoon));
    Canvas OcclusionProbe(5, 5);
    std::vector<double> DiscMask(25, 0.0);
    DiscMask[12] = 1.0;
    SplatStars(OcclusionProbe, {{2.5, 2.5, 1.0, 1.0, {1,1,1}}}, 1.0, &DiscMask);
    Check.Check("Moon coverage masks background stars", OcclusionProbe.At(2,2).R == 0.0 &&
                OcclusionProbe.At(1,2).R > 0.0, OcclusionProbe.At(2,2).R);

    // Sun-disc lens flare: aim so the sun is right of and above centre, leaving ghosts on the opposite side.
    const SkyVector SunEast = Normalize(Cross(DaySun, {0.0, 0.0, 1.0}));
    const SkyVector SunUp = Cross(SunEast, DaySun);
    const SkyVector DayAim = Normalize(DaySun - SunEast * (0.55 * StandardHalfTangent) - SunUp * (0.25 * StandardHalfTangent));
    std::array<Frontier::FlareGhost, 6> Ghosts{};
    const Canvas Flare = RenderSunFlare(DayAim, DaySun, 1.0, Atmosphere, Lut, &Ghosts);
    std::array<Frontier::FlareGhost, 6> HiddenGhosts{};
    const Canvas FlareHidden = RenderSunFlare(DayAim, DaySun, 0.0, Atmosphere, Lut, &HiddenGhosts);
    double HiddenStrength = 0.0;
    for (const Frontier::FlareGhost& Ghost : HiddenGhosts) HiddenStrength = std::max(HiddenStrength, Ghost.Strength);
    Check.Check("zero visibility removes every ghost", HiddenStrength == 0.0, HiddenStrength);

    // Rendering-level ghost check: residual (flare minus no-flare) centroid near each LensFlareModel position.
    const SkyCamera FlareCamera = MakeSkyCamera(DayAim, 1280, 800, StandardHalfTangent);
    double SunX = 0.0, SunY = 0.0;
    ScreenCoordinates(FlareCamera, DaySun, SunX, SunY);
    double CollinearityError = 0.0;
    for (size_t I = 0; I < Ghosts.size(); ++I)
    {
        const double Cross2 = Ghosts[I].PositionX * SunY - Ghosts[I].PositionY * SunX;
        CollinearityError = std::max(CollinearityError, std::fabs(Cross2));
    }
    Check.Check("ghost centres collinear with sun and frame centre", CollinearityError < 1e-12, CollinearityError);
    double WorstCentroidPixels = 0.0;
    int MeasuredGhosts = 0;
    for (size_t I = 0; I < Ghosts.size(); ++I)
    {
        if (Ghosts[I].Strength <= 0.0) continue;
        // Ghost alone: render with only this ghost, subtract the flare-off frame, and take the intensity centroid.
        const Canvas Alone = RenderSunFlare(DayAim, DaySun, 1.0, Atmosphere, Lut, nullptr, static_cast<int>(I));
        // Height-normalised units to pixels: half-height is 400 px and the frame centre is (640, 400).
        const double ExpectedX = 640.0 + Ghosts[I].PositionX * 400.0;
        const double ExpectedY = 400.0 - Ghosts[I].PositionY * 400.0;
        double SumWeight = 0.0, SumX = 0.0, SumY = 0.0;
        for (int Y = 0; Y < 800; ++Y)
            for (int X = 0; X < 1280; ++X)
            {
                const double Weight = std::max(0.0, Alone.At(X, Y).R - FlareHidden.At(X, Y).R);
                SumWeight += Weight;
                SumX += Weight * (X + 0.5);
                SumY += Weight * (Y + 0.5);
            }
        if (SumWeight <= 0.0) continue;
        ++MeasuredGhosts;
        const double Error = std::hypot(SumX / SumWeight - ExpectedX, SumY / SumWeight - ExpectedY);
        WorstCentroidPixels = std::max(WorstCentroidPixels, Error);
        std::printf("INFO ghost %zu expected (%.1f, %.1f) px, isolated centroid error %.2f px\n", I, ExpectedX, ExpectedY, Error);
    }
    Check.Check("rendered ghost centroids within 2 px", MeasuredGhosts >= 5 && WorstCentroidPixels < 2.0, WorstCentroidPixels);

    // Independent physical identities, not a comparison against the implementation itself.
    {
        constexpr int Samples = 100000;
        double PhaseIntegral = 0.0;
        double RayleighDifference = 0.0;
        for (int Index = 0; Index < Samples; ++Index)
        {
            const double Cosine = -1.0 + 2.0 * (Index + 0.5) / Samples;
            PhaseIntegral += Frontier::CornetteShanksPhase(Cosine, 0.8) * 4.0 * Pi / Samples;
            RayleighDifference = std::max(RayleighDifference,
                std::abs(Frontier::CornetteShanksPhase(Cosine, 0.0) - Frontier::RayleighPhase(Cosine)));
        }
        Check.Check("Cornette-Shanks integrates to one", std::abs(PhaseIntegral - 1.0) < 1e-6, PhaseIntegral);
        Check.Check("zero-asymmetry aerosol equals Rayleigh", RayleighDifference < 1e-14, RayleighDifference);
        Frontier::FlareOpticsProfile Optics;
        Optics.StreakStrength = 1.0;
        Optics.StreakAngleRad = 0.0;
        const Frontier::SpectrumRgb Unit{1.0, 1.0, 1.0};
        const auto Warm = Frontier::EvaluateWarmStreak(0.4, 0.0, 0.0, 0.0, Unit, 1.0, Optics);
        const double Expected = 0.65 * std::exp(-0.2 * 3.0);
        Check.Check("warm streak matches full-demo formula and coordinate scale",
                    std::abs(Warm.R - Expected) < 1e-14 && std::abs(Warm.B / Warm.R - 0.55) < 1e-14, Warm.R);
        const auto Mixed = Frontier::EvaluateLensFlareRadiance(0.4, 0.0, 0.0, 0.0, Unit, 1.0, Optics);
        Optics.StreakStrength = 0.0;
        const auto Without = Frontier::EvaluateLensFlareRadiance(0.4, 0.0, 0.0, 0.0, Unit, 1.0, Optics);
        Check.Check("warm plus anamorphic independently additive", std::abs(Mixed.R - Without.R - Warm.R) < 1e-14,
                    Mixed.R - Without.R);
        Optics.StreakStrength = 1.0;
        Optics.StreakAngleRad = Pi * 0.5;
        const auto Rotated = Frontier::EvaluateWarmStreak(0.0, 0.4, 0.0, 0.0, Unit, 0.5, Optics);
        const auto Hidden = Frontier::EvaluateWarmStreak(0.0, 0.4, 0.0, 0.0, Unit, 0.0, Optics);
        Check.Check("warm streak rotation and partial occlusion", std::abs(Rotated.R / Warm.R - 0.5) < 1e-14,
                    Rotated.R / Warm.R);
        Check.Check("warm streak hidden exactly", Hidden.R == 0.0 && Hidden.G == 0.0 && Hidden.B == 0.0, Hidden.R);
    }

    // Sun disc: integrated flux against the analytic limb-darkened integral, limb ratio at the
    // geometric edge (½ from the Gaussian point-spread edge × 0.38 limb law), strict falloff.
    {
        Frontier::SunDiscProfile ProbeDisc;
        ProbeDisc.EdgeBlurRad = 1.0e-7;   // [rad] near-ideal edge for the flux integral
        constexpr int Rings = 720;
        double FluxNumeric = 0.0;
        for (int I = 0; I < Rings; ++I)
        {
            const double Theta = ProbeDisc.AngularRadiusRad * (I + 0.5) / Rings;
            FluxNumeric += Frontier::EvaluateSunDiscRadiance(Theta, ProbeDisc, Frontier::SpectrumRgb{1.0, 1.0, 1.0}).R *
                           2.0 * Pi * std::sin(Theta) * ProbeDisc.AngularRadiusRad / Rings;
        }
        const double FluxAnalytic = Frontier::SunDiscMeanLimbFactor(ProbeDisc) *
                                    Frontier::SunDiscSolidAngleSr(ProbeDisc.AngularRadiusRad);
        Check.Check("sun disc flux vs analytic limb integral", std::fabs(FluxNumeric / FluxAnalytic - 1.0) < 0.01,
                    FluxNumeric / FluxAnalytic);
        const double LimbRatio = Frontier::EvaluateSunDiscRadiance(ProbeDisc.AngularRadiusRad, ProbeDisc,
                                                                   Frontier::SpectrumRgb{1.0, 1.0, 1.0}).R;
        Check.Check("disc radiance at limb (half of 0.38 limb law)", std::fabs(LimbRatio - 0.19) < 0.01, LimbRatio);
        Frontier::SunDiscProfile BlurredDisc;
        BlurredDisc.EdgeBlurRad = 0.75 * StandardHalfTangent / 400.0;
        const double At09 = Frontier::EvaluateSunDiscRadiance(0.9 * BlurredDisc.AngularRadiusRad, BlurredDisc,
                                                              Frontier::SpectrumRgb{1.0, 1.0, 1.0}).R;
        const double At10 = Frontier::EvaluateSunDiscRadiance(1.0 * BlurredDisc.AngularRadiusRad, BlurredDisc,
                                                              Frontier::SpectrumRgb{1.0, 1.0, 1.0}).R;
        const double At11 = Frontier::EvaluateSunDiscRadiance(1.1 * BlurredDisc.AngularRadiusRad, BlurredDisc,
                                                              Frontier::SpectrumRgb{1.0, 1.0, 1.0}).R;
        const double At13 = Frontier::EvaluateSunDiscRadiance(1.3 * BlurredDisc.AngularRadiusRad, BlurredDisc,
                                                              Frontier::SpectrumRgb{1.0, 1.0, 1.0}).R;
        Check.Check("blurred limb falls off strictly", At09 > At10 && At10 > At11 && At11 > At13 && At13 > 0.0, At11);
    }

    // Flare optics: linearity in visibility, streak anisotropy, starburst spike count (6 blades → 6).
    {
        const Frontier::FlareOpticsProfile Optics;
        const Frontier::SpectrumRgb LensIrradiance{7.0, 6.8, 6.2};
        const double FullFlare = Frontier::EvaluateLensFlareRadiance(0.30, -0.20, SunX, SunY, LensIrradiance, 1.0,
                                                                     Optics).G;
        const double HalfFlare = Frontier::EvaluateLensFlareRadiance(0.30, -0.20, SunX, SunY, LensIrradiance, 0.5,
                                                                     Optics).G;
        Check.Check("flare linear in disc visibility", std::fabs(HalfFlare / FullFlare - 0.5) < 1e-9,
                    HalfFlare / FullFlare);
        const double Along = Frontier::EvaluateAnamorphicStreak(SunX + 0.45, SunY, SunX, SunY, LensIrradiance, 1.0,
                                                                Optics).B;
        const double Across = Frontier::EvaluateAnamorphicStreak(SunX, SunY + 0.45, SunX, SunY, LensIrradiance, 1.0,
                                                                 Optics).B;
        Check.Check("anamorphic streak anisotropic (along vs across)", Along > 0.0 && Across < 1e-6 * Along, Along);
        constexpr int CircleSamples = 720;
        double Circle[CircleSamples];
        double CirclePeak = 0.0;
        for (int I = 0; I < CircleSamples; ++I)
        {
            const double Phi = 2.0 * Pi * I / CircleSamples;
            Circle[I] = Frontier::EvaluateDiffractionStarburst(SunX + 0.10 * std::cos(Phi),
                                                               SunY + 0.10 * std::sin(Phi), SunX, SunY, LensIrradiance,
                                                               1.0, Optics).G;
            CirclePeak = std::max(CirclePeak, Circle[I]);
        }
        int SpikeCount = 0;
        for (int I = 0; I < CircleSamples; ++I)
        {
            const double Previous = Circle[(I + CircleSamples - 1) % CircleSamples];
            const double Next = Circle[(I + 1) % CircleSamples];
            if (Circle[I] > Previous && Circle[I] >= Next && Circle[I] > 0.25 * CirclePeak) ++SpikeCount;
        }
        Check.Check("starburst spikes from 6-blade aperture", SpikeCount == 6, static_cast<double>(SpikeCount));
        std::printf("INFO flare sample %.4f, streak along %.4f across %.3g, spikes %d\n", FullFlare, Along, Across,
                    SpikeCount);
    }

    // Telephoto sun for disc-scale inspection, plus a rendered-level limb check on its pixels:
    // disc centre sits at height-normalised (0.16, 0.10) by the aim construction, radius 60 px.
    double SunFilteredExposure = 0.0;
    const Canvas SunClose = RenderSunDiscClose(DaySun, Atmosphere, Lut, &SunFilteredExposure);
    {
        const int CentreX = 480 + static_cast<int>(0.16 * 360.0);
        const int CentreY = 360 - static_cast<int>(0.10 * 360.0);
        const double CentreLinear = SunClose.At(CentreX, CentreY).R;
        const double LimbLinear = SunClose.At(CentreX + 60, CentreY).R;
        const double OutsideLinear = SunClose.At(CentreX + 140, CentreY).R;
        Check.Check("rendered disc limb ratio at geometric edge",
                    LimbLinear / CentreLinear > 0.14 && LimbLinear / CentreLinear < 0.26, LimbLinear / CentreLinear);
        Check.Check("rendered disc dominates sky at 2.3 radii", CentreLinear > 50.0 * OutsideLinear,
                    CentreLinear / std::max(OutsideLinear, 1e-9));
        std::printf("INFO telephoto disc centre %.4g limb %.4g outside %.4g, filtered exposure %.3g\n", CentreLinear,
                    LimbLinear, OutsideLinear, SunFilteredExposure);
    }

    // Dusk sweep at a fixed azimuth: 14 deg above to 18 deg below the horizon.
    const SkyVector GalacticPole = EquatorialToLocal(192.85948 * DegToRad, 27.12825 * DegToRad, NightLst);
    const SkyVector GalacticCentre = EquatorialToLocal(266.40499 * DegToRad, -28.93618 * DegToRad, NightLst);
    const std::array<double, 6> DuskElevations{14.0, 2.0, -2.0, -6.0, -12.0, -18.0};
    std::vector<Canvas> Dusk;
    for (double Elevation : DuskElevations)
        Dusk.push_back(RenderDuskFrame(Elevation * DegToRad, Atmosphere, Lut, GalacticPole, GalacticCentre, Catalogue, NightLst));
    const Radiance Horizon = Dusk[2].At(240, 246);   // sample just above the horizon, toward the sun column
    Check.Check("sunset horizon is warm (R over B) at -2 deg", Horizon.R > Horizon.B, Horizon.R / Horizon.B);

    // 📝 Photographic exposures per scene class (ACES input scale), set from measured pixel
    //    radiances: the wide sun frame is exposed for the sky (1.15) as a real sun photograph is —
    //    at 2.8 the physical forward-scattering aureole (measured 0.45–0.60 toward the sun) clips
    //    the whole frame to white; the telephoto pair shows flare-in-sky at 0.5 and the limb law
    //    itself at solar-filter exposure; the dusk sweep keeps one honest exposure across tiles.
    const bool Wrote = WritePpm(OutputPrefix + "NightField.ppm", MilkyWay, 1.0) &&
                       WritePpm(OutputPrefix + "MoonField.ppm", MoonField, 1.0) &&
                       WritePpm(OutputPrefix + "MoonClose.ppm", Close, 0.42) &&
                       WritePpm(OutputPrefix + "MoonPhases.ppm", Phases, 0.5) &&
                       WritePpm(OutputPrefix + "SunFlare.ppm", Flare, 1.15) &&
                       WritePpm(OutputPrefix + "SunDiscClose.ppm", SunClose, 0.5) &&
                       WritePpm(OutputPrefix + "SunDiscFiltered.ppm", SunClose, SunFilteredExposure);
    bool WroteDusk = true;
    for (size_t I = 0; I < Dusk.size(); ++I)
        WroteDusk = WroteDusk && WritePpm(OutputPrefix + "Dusk" + std::to_string(I) + ".ppm", Dusk[I], 1.6);
    Check.Check("PPM outputs written", Wrote && WroteDusk, Wrote && WroteDusk ? 1.0 : 0.0);

    std::printf("SUMMARY passed %d failed %d\n", Check.Passed, Check.Failed);
    return Check.Failed == 0 ? 0 : 1;
}
