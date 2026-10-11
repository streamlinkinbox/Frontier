//============================================================================================================================================
// 📦 Project-Zero/Source/CelestialIntegrator.cpp — The Reference Fragment Program, Line by Line
//============================================================================================================================================
//
//    Transcription rules held throughout, because they are what make the result checkable:
//      ① Every numeric literal is the reference's literal. No re-derivation, no "equivalent" algebra.
//      ② Loop counts, early-outs and clamps are kept — they are visible in the image, not just the cost.
//      ③ Where the reference relies on a GLSL intrinsic, the equivalent lives in CelestialSpecification.h and
//         is named after it, so the two texts can be read side by side.
//      ④ Reference line numbers are quoted in the section banners, against the 497-line `#fs` program.
//
//============================================================================================================================================

#include "CelestialIntegrator.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace Frontier::ProjectZero {

namespace {

//------------------------------------------------------------------------------------------------------------------------
//                                          HASH AND NOISE — reference §27-31
//------------------------------------------------------------------------------------------------------------------------
//    `hash33`, `hash13` and `vnoise`. The hashes are bit-exact ports: the same multipliers, the same fract
//    cascade. Everything textured — stars, clouds, fog, terrain, grain — stands on these three.

Vector3 Hash33(const Vector3& p0) noexcept
{
    Vector3 p{ Fract(p0.x * 0.1031f), Fract(p0.y * 0.1030f), Fract(p0.z * 0.0973f) };
    const float d = p.x * (p.y + 33.33f) + p.y * (p.x + 33.33f) + p.z * (p.z + 33.33f);
    p += Splat(d);
    return Vector3{ Fract((p.x + p.y) * p.z), Fract((p.x + p.x) * p.y), Fract((p.y + p.x) * p.x) };
}

float Hash13(const Vector3& p0) noexcept
{
    Vector3 p{ Fract(p0.x * 0.1031f), Fract(p0.y * 0.1031f), Fract(p0.z * 0.1031f) };
    const float d = p.x * (p.z + 31.32f) + p.y * (p.y + 31.32f) + p.z * (p.x + 31.32f);
    p += Splat(d);
    return Fract((p.x + p.y) * p.z);
}

float ValueNoise(const Vector3& p) noexcept
{
    const Vector3 i{ std::floor(p.x), std::floor(p.y), std::floor(p.z) };
    Vector3 f{ p.x - i.x, p.y - i.y, p.z - i.z };
    f = Vector3{ f.x * f.x * (3.0f - 2.0f * f.x), f.y * f.y * (3.0f - 2.0f * f.y), f.z * f.z * (3.0f - 2.0f * f.z) };

    const float c000 = Hash13(i);
    const float c100 = Hash13(i + Vector3{ 1.0f, 0.0f, 0.0f });
    const float c010 = Hash13(i + Vector3{ 0.0f, 1.0f, 0.0f });
    const float c110 = Hash13(i + Vector3{ 1.0f, 1.0f, 0.0f });
    const float c001 = Hash13(i + Vector3{ 0.0f, 0.0f, 1.0f });
    const float c101 = Hash13(i + Vector3{ 1.0f, 0.0f, 1.0f });
    const float c011 = Hash13(i + Vector3{ 0.0f, 1.0f, 1.0f });
    const float c111 = Hash13(i + Vector3{ 1.0f, 1.0f, 1.0f });

    return Mix(Mix(Mix(c000, c100, f.x), Mix(c010, c110, f.x), f.y),
               Mix(Mix(c001, c101, f.x), Mix(c011, c111, f.x), f.y), f.z);
}

// `rsi` — ray/sphere intersection about the origin. Returns (-1,-1) on a miss, as the reference does.
void RaySphere(const Vector3& Origin, const Vector3& Direction, float Radius, float& OutNear, float& OutFar) noexcept
{
    const float b = Dot(Origin, Direction);
    const float c = Dot(Origin, Origin) - Radius * Radius;
    float d = b * b - c;
    if (d < 0.0f)
    {
        OutNear = -1.0f;
        OutFar  = -1.0f;
        return;
    }
    d = std::sqrt(d);
    OutNear = -b - d;
    OutFar  = -b + d;
}

// `kelvin` — the star-field blackbody approximation (2000..12000 K). Distinct from the sun's `kelvinRGB`.
Vector3 KelvinStar(float t) noexcept
{
    const float x = Clamp01((t - 2000.0f) / 10000.0f);
    Vector3 c = Mix(Vector3{ 1.0f, 0.55f, 0.25f }, Vector3{ 1.0f, 0.93f, 0.86f }, SmoothStep(0.0f, 0.4f, x));
    return Mix(c, Vector3{ 0.72f, 0.82f, 1.0f }, SmoothStep(0.4f, 1.0f, x));
}

Vector3 RotateY(const Vector3& v, float a) noexcept
{
    const float c = std::cos(a), s = std::sin(a);
    return Vector3{ c * v.x + s * v.z, v.y, -s * v.x + c * v.z };
}

Vector3 RotateX(const Vector3& v, float a) noexcept
{
    const float c = std::cos(a), s = std::sin(a);
    return Vector3{ v.x, c * v.y - s * v.z, s * v.y + c * v.z };
}

// Octahedral direction ↔ unit square, the star grid's single chart.
void OctahedralEncode(const Vector3& n0, float& OutU, float& OutV) noexcept
{
    const float L = std::abs(n0.x) + std::abs(n0.y) + std::abs(n0.z);
    const Vector3 n = n0 / std::max(L, 1e-20f);
    float px = n.x, py = n.z;
    if (n.y < 0.0f)
    {
        const float ax = 1.0f - std::abs(py);
        const float ay = 1.0f - std::abs(px);
        px = ax * (px >= 0.0f ? 1.0f : -1.0f);
        py = ay * (py >= 0.0f ? 1.0f : -1.0f);
    }
    OutU = px * 0.5f + 0.5f;
    OutV = py * 0.5f + 0.5f;
}

Vector3 OctahedralDecode(float u, float v) noexcept
{
    const float fx = u * 2.0f - 1.0f;
    const float fy = v * 2.0f - 1.0f;
    Vector3 n{ fx, 1.0f - std::abs(fx) - std::abs(fy), fy };
    const float t = std::max(-n.y, 0.0f);
    n.x += n.x >= 0.0f ? -t : t;
    n.z += n.z >= 0.0f ? -t : t;
    return n.Normalized();
}

// `clHG` — the Henyey-Greenstein lobe the clouds and local media share.
float HenyeyGreenstein(float c, float g) noexcept
{
    const float d = 1.0f + g * g - 2.0f * g * c;
    return (1.0f - g * g) / (4.0f * 3.14159f * std::pow(std::max(d, 1e-6f), 1.5f));
}

// `wl2rgb` — wavelength (380..700 nm) to linear-ish RGB, for the rainbow's spectral sum.
Vector3 WavelengthColour(float w) noexcept
{
    Vector3 c{ 0.0f, 0.0f, 0.0f };
    if (w < 440.0f)      { c = Vector3{ -(w - 440.0f) / 60.0f, 0.0f, 1.0f }; }
    else if (w < 490.0f) { c = Vector3{ 0.0f, (w - 440.0f) / 50.0f, 1.0f }; }
    else if (w < 510.0f) { c = Vector3{ 0.0f, 1.0f, -(w - 510.0f) / 20.0f }; }
    else if (w < 580.0f) { c = Vector3{ (w - 510.0f) / 70.0f, 1.0f, 0.0f }; }
    else if (w < 645.0f) { c = Vector3{ 1.0f, -(w - 645.0f) / 65.0f, 0.0f }; }
    else                 { c = Vector3{ 1.0f, 0.0f, 0.0f }; }
    const float f = (w < 420.0f) ? 0.3f + 0.7f * (w - 380.0f) / 40.0f
                  : (w > 680.0f) ? 0.3f + 0.7f * (700.0f - w) / 20.0f
                  : 1.0f;
    return c * f;
}

// `hue` — the flare's chromatic ring.
Vector3 HueOf(float h) noexcept
{
    //    ⚠️ GLSL `mod(x,y)` is `x - y*floor(x/y)`, which is NOT `std::fmod`: for negative x the two differ
    //    in sign, and here that swings a channel by a full 1.0 (hue(-0.3) gives 0.200 under GLSL and 1.000
    //    under fmod). Both call sites currently pass `fract(...)`, so the argument lands in [0,1) and the
    //    difference is masked — but the kernel would be silently wrong the moment anything fed it a negative,
    //    which is exactly how this class of bug ships. Matching GLSL exactly costs nothing.
    auto Channel = [](float x)
    {
        const float m = x - 6.0f * std::floor(x / 6.0f);        // GLSL mod
        return std::clamp(std::abs(m - 3.0f) - 1.0f, 0.0f, 1.0f);
    };
    const float base = h * 6.0f;
    return Vector3{ Channel(base + 0.0f), Channel(base + 4.0f), Channel(base + 2.0f) };
}

// expHeightK: the exact integral of exp(-y/H) along a straight segment (Wenzel / Quilez). Shared by both
// analytic fogs, and exercised directly by the transliteration proof.
float HeightIntegralKernel(float hh, float a, float b) noexcept
{
    const float ya = std::max(0.0f, std::min(a, b));
    const float yb = std::max(0.0f, std::max(a, b));
    return (yb - ya) < 1e-3f ? std::exp(-ya / hh) : hh * (std::exp(-ya / hh) - std::exp(-yb / hh)) / (yb - ya);
}

// clHeightProfile: the per-variety vertical density envelope.
// 0 stratus · 1 stratocumulus · 2 cumulus · 3 cumulonimbus · 4 altostratus · 5 cirrus
float CloudHeightProfileKernel(float hn, float Variety, float Anvil) noexcept
{
    const float t = Variety;
    if (t < 0.5f)  { return SmoothStep(0.0f, 0.08f, hn) * (1.0f - SmoothStep(0.75f, 1.0f, hn)); }
    if (t < 1.5f)  { return SmoothStep(0.0f, 0.12f, hn) * (1.0f - SmoothStep(0.5f, 0.95f, hn)); }
    if (t < 2.5f)  { return SmoothStep(0.0f, 0.07f, hn) * (1.0f - SmoothStep(0.35f, 1.0f, hn)) * 1.15f; }
    if (t < 3.5f)  { return SmoothStep(0.0f, 0.05f, hn) * (1.0f - SmoothStep(0.85f, 1.0f, hn))
                          * Mix(1.0f, 1.6f, SmoothStep(0.7f, 1.0f, hn) * Anvil); }
    if (t < 4.5f)  { return SmoothStep(0.0f, 0.3f, hn) * (1.0f - SmoothStep(0.6f, 1.0f, hn)) * 0.7f; }
    return SmoothStep(0.0f, 0.4f, hn) * (1.0f - SmoothStep(0.5f, 1.0f, hn)) * 0.35f;
}

// The tonemap ladder: `aces`, Reinhard, `filmic` (Uncharted 2) and `agxish`.
Vector3 ToneAces(const Vector3& x) noexcept
{
    auto Curve = [](float v) { return std::clamp((v * (2.51f * v + 0.03f)) / (v * (2.43f * v + 0.59f) + 0.14f), 0.0f, 1.0f); };
    return Vector3{ Curve(x.x), Curve(x.y), Curve(x.z) };
}

Vector3 ToneUncharted(const Vector3& x) noexcept
{
    constexpr float A = 0.15f, B = 0.5f, C = 0.1f, D = 0.2f, E = 0.02f, F = 0.3f;
    auto Curve = [](float v) { return ((v * (A * v + C * B) + D * E) / (v * (A * v + B) + D * F)) - E / F; };
    return Vector3{ Curve(x.x), Curve(x.y), Curve(x.z) };
}

Vector3 ToneFilmic(const Vector3& c) noexcept
{
    const Vector3 num = ToneUncharted(c * 2.0f);
    const Vector3 den = ToneUncharted(Splat(11.2f));
    return Vector3{ num.x / den.x, num.y / den.y, num.z / den.z };
}

Vector3 ToneAgx(const Vector3& c0) noexcept
{
    const Vector3 c = MaximumOf(c0, 0.0f);
    auto Curve = [](float v)
    {
        const float x = Clamp01((std::log2(v + 1e-5f) + 12.47f) / 16.5f);
        return x * x * (3.0f - 2.0f * x);
    };
    return Vector3{ Curve(c.x), Curve(c.y), Curve(c.z) };
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                 MOON ALBEDO SURFACE
//------------------------------------------------------------------------------------------------------------------------

MoonAlbedoSurface MoonAlbedoSurface::LoadPortablePixmap(const char* Path) noexcept
{
    MoonAlbedoSurface Surface{};
    std::FILE* File = std::fopen(Path, "rb");
    if (File == nullptr)
    {
        return Surface;
    }

    //    P6 header: magic, width, height, maximum value — with '#' comments permitted between any two tokens.
    auto ReadToken = [&File](char* Out, size_t Size) -> bool
    {
        size_t n = 0u;
        int c = 0;
        while ((c = std::fgetc(File)) != EOF)
        {
            if (c == '#')
            {
                while ((c = std::fgetc(File)) != EOF && c != '\n') { }
                continue;
            }
            if (std::isspace(c) != 0)
            {
                if (n > 0u) { break; }
                continue;
            }
            if (n + 1u < Size) { Out[n++] = static_cast<char>(c); }
        }
        Out[n] = '\0';
        return n > 0u;
    };

    char Magic[8] = {}, WidthText[16] = {}, HeightText[16] = {}, MaximumText[16] = {};
    if (!ReadToken(Magic, sizeof(Magic)) || Magic[0] != 'P' || Magic[1] != '6'
        || !ReadToken(WidthText, sizeof(WidthText)) || !ReadToken(HeightText, sizeof(HeightText))
        || !ReadToken(MaximumText, sizeof(MaximumText)))
    {
        std::fclose(File);
        return Surface;
    }

    const long Width = std::strtol(WidthText, nullptr, 10);
    const long Height = std::strtol(HeightText, nullptr, 10);
    if (Width <= 0 || Height <= 0 || Width > 16384 || Height > 16384)
    {
        std::fclose(File);
        return Surface;
    }

    Surface.Width  = static_cast<uint32_t>(Width);
    Surface.Height = static_cast<uint32_t>(Height);
    Surface.Texels.resize(static_cast<size_t>(Width) * static_cast<size_t>(Height) * 3u);
    const size_t Read = std::fread(Surface.Texels.data(), 1u, Surface.Texels.size(), File);
    std::fclose(File);

    if (Read != Surface.Texels.size())
    {
        return MoonAlbedoSurface{};
    }
    return Surface;
}

Vector3 MoonAlbedoSurface::Sample(float u, float v) const noexcept
{
    if (!Populated())
    {
        return Vector3{ 0.62f, 0.62f, 0.62f };                  // a plausible lunar grey when the atlas is absent
    }
    const float wu = u - std::floor(u);                         // wrap in longitude, clamp in latitude
    const float wv = Clamp01(v);
    uint32_t x = static_cast<uint32_t>(wu * static_cast<float>(Width));
    uint32_t y = static_cast<uint32_t>(wv * static_cast<float>(Height));
    if (x >= Width)  { x = Width - 1u; }
    if (y >= Height) { y = Height - 1u; }
    const size_t o = (static_cast<size_t>(y) * Width + x) * 3u;
    constexpr float kInv = 1.0f / 255.0f;
    return Vector3{ static_cast<float>(Texels[o]) * kInv,
                    static_cast<float>(Texels[o + 1u]) * kInv,
                    static_cast<float>(Texels[o + 2u]) * kInv };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

CelestialIntegrator::CelestialIntegrator(const CelestialCriteria& Parameters) noexcept
    : Criteria(Parameters)
    , Solved{}
    , MoonSurfaces{}
{
    SolveFrame(0.0f);
}

void CelestialIntegrator::AssignMoonSurface(uint32_t Slot, MoonAlbedoSurface Surface) noexcept
{
    if (Slot < 4u)
    {
        MoonSurfaces[Slot] = std::move(Surface);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                     SOLAR EPHEMERIS AND COLOUR — reference `sunDirAt`, `kelvinRGB`
//------------------------------------------------------------------------------------------------------------------------

Vector3 CelestialIntegrator::SolveSunDirection(float LocalHours) const noexcept
{
    //    sunDirAt(t): lat = sun_lat, HA = (t-12)/24·2π, sinE = cos(lat)·cos(HA)
    //                 az  = atan2(sin HA, cos HA·sin lat) + π + sun_az
    //    `sunDirAt` lives on the panel, so it runs in double and rounds once on upload. See `PanelRadians`.
    const double Latitude = static_cast<double>(Criteria.Sun.LatitudeDegrees) * kDegreesToRadiansPanel;
    const double HourAngle = (static_cast<double>(LocalHours) - 12.0) / 24.0 * kPiPanel * 2.0;
    const double SinElevation = std::cos(Latitude) * std::cos(HourAngle);
    const double Elevation = std::asin(std::clamp(SinElevation, -1.0, 1.0));
    const double Azimuth = std::atan2(std::sin(HourAngle), std::cos(HourAngle) * std::sin(Latitude))
                         + kPiPanel + static_cast<double>(Criteria.Sun.AzimuthOffsetDegrees) * kDegreesToRadiansPanel;

    return Vector3{ static_cast<float>(std::sin(Azimuth) * std::cos(Elevation)),
                    static_cast<float>(std::sin(Elevation)),
                    static_cast<float>(-std::cos(Azimuth) * std::cos(Elevation)) };
}

Vector3 CelestialIntegrator::KelvinColour(float Temperature) noexcept
{
    //    kelvinRGB: Tanner Helland's piecewise fit, exactly as the panel writes it.
    const float k = Temperature / 100.0f;
    const float r = (k <= 66.0f) ? 255.0f : 329.7f * std::pow(k - 60.0f, -0.133f);
    const float g = (k <= 66.0f) ? 99.47f * std::log(k) - 161.1f : 288.1f * std::pow(k - 60.0f, -0.0755f);
    const float b = (k >= 66.0f) ? 255.0f : ((k <= 19.0f) ? 0.0f : 138.5f * std::log(k - 10.0f) - 305.0f);
    return Vector3{ std::clamp(r, 0.0f, 255.0f) / 255.0f,
                    std::clamp(g, 0.0f, 255.0f) / 255.0f,
                    std::clamp(b, 0.0f, 255.0f) / 255.0f };
}

float CelestialIntegrator::AirMassOf(float ElevationDegrees) noexcept
{
    //    airMassOf: Kasten-Young. Saturates at 40 below z = 96°.
    const float z = 90.0f - ElevationDegrees;
    if (z >= 96.0f)
    {
        return 40.0f;
    }
    return std::min(40.0f, 1.0f / (std::cos(Radians(z)) + 0.50572f * std::pow(96.07995f - z, -1.6364f)));
}

void CelestialIntegrator::SolveFrame(float TimeSeconds) noexcept
{
    Solved.TimeSeconds    = TimeSeconds;
    Solved.SunDirection   = SolveSunDirection(Criteria.Sun.LocalHours);
    Solved.SunColour      = KelvinColour(Criteria.Sun.ColourTemperature);
    Solved.SunElevationDeg= Degrees(std::asin(std::clamp(Solved.SunDirection.y, -1.0f, 1.0f)));
    Solved.SunAzimuthRad  = std::atan2(Solved.SunDirection.x, -Solved.SunDirection.z);

    //    The moon roster. `link` puts the body at the anti-solar point; otherwise azimuth/elevation, through
    //    the panel's own `dirFrom(e, a)`.
    for (uint32_t i = 0u; i < 4u; ++i)
    {
        const MoonCriteria& M = Criteria.Moons[i];
        if (M.OppositeTheSun)
        {
            Solved.MoonDirections[i] = Negate(Solved.SunDirection);
        }
        else
        {
            //    `dirFrom` is panel-side: double, rounded once on upload.
            const double e = static_cast<double>(M.ElevationDegrees) * kDegreesToRadiansPanel;
            const double a = static_cast<double>(M.AzimuthDegrees) * kDegreesToRadiansPanel;
            Solved.MoonDirections[i] = Vector3{ static_cast<float>(std::sin(a) * std::cos(e)),
                                                static_cast<float>(std::sin(e)),
                                                static_cast<float>(-std::cos(a) * std::cos(e)) };
        }
    }

    //    The sky-ambient probe. The reference renders a 4×1 target with `uProbe`: zenith, sun-side horizon and
    //    anti-sun horizon, weighted .4/.35/.25. Same three directions, same weights, same atmosphere code.
    const Vector3 Origin{ 0.0f, Criteria.Atmosphere.PlanetRadius + Criteria.Observer.Height, 0.0f };
    const Vector3 Horizontal = Vector3{ Solved.SunDirection.x + 1e-4f, 0.0f, Solved.SunDirection.z }.Normalized();
    const Vector3 Probes[3] = {
        Vector3{ 0.0f, 1.0f, 0.0f },
        (Horizontal + Vector3{ 0.0f, 0.08f, 0.0f }).Normalized(),
        (Negate(Horizontal) + Vector3{ 0.0f, 0.08f, 0.0f }).Normalized()
    };
    constexpr float Weights[3] = { 0.4f, 0.35f, 0.25f };

    Vector3 Ambient{ 0.0f, 0.0f, 0.0f };
    for (uint32_t i = 0u; i < 3u; ++i)
    {
        Vector3 Sky{}, Transmittance{};
        float Ground = 0.0f;
        IntegrateAtmosphere(Origin, Probes[i], Sky, Transmittance, Ground);
        Sky *= Criteria.Sky.Tint;
        Sky *= Criteria.Sky.Brightness * (Criteria.Sky.Visible ? 1.0f : 0.0f) * (Criteria.Atmosphere.Visible ? 1.0f : 0.0f);
        Ambient += Sky * Weights[i];
    }
    Solved.SkyAmbient = Ambient;

    //    `uStarCeil` — the brightest star the field can produce, in the star pass's own units.
    Solved.StarCeiling = 8.0f * 2.6e-3f * std::max(Criteria.Stars.Brightness, 1e-6f) * 3.2f * 1.6f;
}

void CelestialIntegrator::AdvanceWind(float DeltaSeconds) noexcept
{
    //    windStep: the integral of the ground-level base wind, and the gust phase.
    const float Speed = Criteria.Wind.Visible ? Criteria.Wind.Speed : 0.0f;
    const float Bearing = PanelRadians(Criteria.Wind.BearingDegrees);        // uWDir = wd_dir*D2R
    const float Gust = 1.0f + Criteria.Wind.Gust * (0.55f * std::sin(Solved.WindGustPhase)
                                                  + 0.30f * std::sin(Solved.WindGustPhase * 2.31f + 1.7f)
                                                  + 0.15f * std::sin(Solved.WindGustPhase * 4.7f + 0.4f));
    Solved.WindIntegral[0] += std::sin(Bearing) * Speed * Gust * DeltaSeconds;
    Solved.WindIntegral[1] += std::cos(Bearing) * Speed * Gust * DeltaSeconds;
    Solved.WindGustPhase   += DeltaSeconds * (0.35f + Criteria.Wind.Gust * 0.4f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                              WIND FIELD — reference §105-111
//------------------------------------------------------------------------------------------------------------------------

Vector3 CelestialIntegrator::WindDisplacement(const Vector3& Position) const noexcept
{
    //    windDisp: two trig calls and a multiply — cheap enough to sit inside a march, as the reference notes.
    const float Speed = Criteria.Wind.Visible ? Criteria.Wind.Speed : 0.0f;
    const float km = std::max(0.0f, Position.y) / 1000.0f;
    const float sp = Speed * (1.0f + Criteria.Wind.Shear * km);
    //    uWDir arrives from the panel (double); the veer term is shader-side float with the truncated PI.
    const float b  = PanelRadians(Criteria.Wind.BearingDegrees)
                   + Criteria.Wind.VeerDegreesPerKm * km * kPi / 180.0f;
    const float Magnitude = std::sqrt(std::sin(b) * sp * std::sin(b) * sp + std::cos(b) * sp * std::cos(b) * sp);
    const float f = Magnitude / std::max(1e-3f, Speed);
    return Vector3{ Solved.WindIntegral[0] * f, 0.0f, Solved.WindIntegral[1] * f };
}

Vector3 CelestialIntegrator::WindTurbulence(const Vector3& Position, float Time) const noexcept
{
    //    windTurb: the curl of a low-frequency noise — divergence-free swirl. Six noise evaluations.
    if (Criteria.Wind.Turbulence <= 0.0f || !Criteria.Wind.Visible)
    {
        return Vector3{ 0.0f, 0.0f, 0.0f };
    }
    const Vector3 q = Position * 0.02f + Vector3{ Time * 0.05f, 0.0f, Time * 0.03f };
    constexpr float e = 0.5f;
    const float n1 = ValueNoise(q + Vector3{ 0.0f, e, 0.0f }) - ValueNoise(q - Vector3{ 0.0f, e, 0.0f });
    const float n2 = ValueNoise(q + Vector3{ 0.0f, 0.0f, e }) - ValueNoise(q - Vector3{ 0.0f, 0.0f, e });
    const float n3 = ValueNoise(q + Vector3{ e, 0.0f, 0.0f }) - ValueNoise(q - Vector3{ e, 0.0f, 0.0f });
    return Vector3{ n1 - n2, n2 - n3, n3 - n1 } * (Criteria.Wind.Turbulence * Criteria.Wind.Speed * 0.9f);
}

Vector3 CelestialIntegrator::SwirlAt(const Vector3& Position) const noexcept
{
    //    windSwirl: the per-pixel displacement, evaluated once and reused — never inside the march loop.
    return WindTurbulence(Position, Solved.TimeSeconds) * 8.0f;
}

//------------------------------------------------------------------------------------------------------------------------
//                                             ATMOSPHERE — reference §78-101
//------------------------------------------------------------------------------------------------------------------------
//    Single-scattering Rayleigh + Mie + ozone, 20 view samples × 8 light samples, quadratic spacing so the
//    samples crowd near the camera. The ozone term rides the Rayleigh optical depth, as the reference has it.

void CelestialIntegrator::IntegrateAtmosphere(const Vector3& Origin, const Vector3& Direction,
                                              Vector3& OutSky, Vector3& OutTransmittance, float& OutGround) const noexcept
{
    OutSky           = Vector3{ 0.0f, 0.0f, 0.0f };
    OutTransmittance = Vector3{ 1.0f, 1.0f, 1.0f };
    OutGround        = 0.0f;

    const float PlanetRadius = Criteria.Atmosphere.PlanetRadius;
    const float Ra = PlanetRadius + Criteria.Atmosphere.AtmosphereHeight;

    float AtmoNear = 0.0f, AtmoFar = 0.0f;
    RaySphere(Origin, Direction, Ra, AtmoNear, AtmoFar);
    if (AtmoFar < 0.0f)
    {
        return;
    }

    float t0 = std::max(AtmoNear, 0.0f);
    float t1 = AtmoFar;

    float GroundNear = 0.0f, GroundFar = 0.0f;
    RaySphere(Origin, Direction, PlanetRadius, GroundNear, GroundFar);
    if (GroundNear > 0.0f)
    {
        t1 = GroundNear;
        OutGround = 1.0f;
    }

    constexpr int N = 20;
    constexpr int NL = 8;
    const float Length = t1 - t0;

    const Vector3 BetaRayleigh = Vector3{ 5.8e-6f, 13.5e-6f, 33.1e-6f } * Criteria.Atmosphere.RayleighStrength;
    const Vector3 BetaMie      = Splat(21e-6f) * Criteria.Atmosphere.MieStrength;
    const Vector3 BetaOzone    = Vector3{ 0.65e-6f, 1.881e-6f, 0.085e-6f } * Criteria.Atmosphere.OzoneStrength;

    Vector3 SumRayleigh{ 0.0f, 0.0f, 0.0f };
    Vector3 SumMie{ 0.0f, 0.0f, 0.0f };
    float DepthRayleigh = 0.0f, DepthMie = 0.0f;

    const float mu = Dot(Direction, Solved.SunDirection);
    const float g  = Criteria.Atmosphere.MieAnisotropy;
    const float PhaseRayleigh = 3.0f / (16.0f * kPi) * (1.0f + mu * mu);
    const float PhaseMie = 3.0f / (8.0f * kPi) * ((1.0f - g * g) * (1.0f + mu * mu))
                         / ((2.0f + g * g) * std::pow(std::max(1.0f + g * g - 2.0f * g * mu, 1e-6f), 1.5f));

    const float Hr = Criteria.Atmosphere.RayleighScaleHeight;
    const float Hm = Criteria.Atmosphere.MieScaleHeight;

    for (int i = 0; i < N; ++i)
    {
        float s0 = static_cast<float>(i) / static_cast<float>(N);
        float s1 = static_cast<float>(i + 1) / static_cast<float>(N);
        s0 *= s0;                                               // quadratic: dense near the camera
        s1 *= s1;

        const float ta = t0 + Length * s0;
        const float tb = t0 + Length * s1;
        const float Segment = tb - ta;
        const float tm = 0.5f * (ta + tb);

        const Vector3 p = Origin + Direction * tm;
        const float h = p.Length() - PlanetRadius;
        const float hr = std::exp(-h / Hr) * Segment;
        const float hm = std::exp(-h / Hm) * Segment;
        DepthRayleigh += hr;
        DepthMie      += hm;

        float LightNear = 0.0f, LightFar = 0.0f;
        RaySphere(p, Solved.SunDirection, Ra, LightNear, LightFar);
        const float LightLength = LightFar;

        float LightRayleigh = 0.0f, LightMie = 0.0f;
        bool Lit = true;
        for (int j = 0; j < NL; ++j)
        {
            float q0 = static_cast<float>(j) / static_cast<float>(NL);
            float q1 = static_cast<float>(j + 1) / static_cast<float>(NL);
            q0 *= q0;
            q1 *= q1;
            const float SegmentL = LightLength * (q1 - q0);
            const Vector3 q = p + Solved.SunDirection * (LightLength * 0.5f * (q0 + q1));
            const float hq = q.Length() - PlanetRadius;
            if (hq < 0.0f)
            {
                Lit = false;
                break;
            }
            LightRayleigh += std::exp(-hq / Hr) * SegmentL;
            LightMie      += std::exp(-hq / Hm) * SegmentL;
        }

        if (Lit)
        {
            const Vector3 Attenuation = ExponentOf(Negate(
                  BetaRayleigh * (DepthRayleigh + LightRayleigh)
                + BetaMie * 1.1f * (DepthMie + LightMie)
                + BetaOzone * (DepthRayleigh + LightRayleigh)));
            SumRayleigh += Attenuation * hr;
            SumMie      += Attenuation * hm;
        }
    }

    OutTransmittance = ExponentOf(Negate(BetaRayleigh * DepthRayleigh + BetaMie * 1.1f * DepthMie + BetaOzone * DepthRayleigh));
    OutSky = (SumRayleigh * BetaRayleigh * PhaseRayleigh + SumMie * BetaMie * PhaseMie)
           * Criteria.Sun.Intensity * Solved.SunColour;
}

//------------------------------------------------------------------------------------------------------------------------
//                                            TWILIGHT — reference `dawnGlow` §144-172
//------------------------------------------------------------------------------------------------------------------------
//    The band that the single-scattering integral cannot produce: the ground observer's dawn/dusk model, its
//    log-spaced colour ramp, the pre-sunrise white line and the cool earth-shadow dome fill.

Vector3 CelestialIntegrator::TwilightGlow(const Vector3& Direction, float ElevationDeg, float Facing) const noexcept
{
    const float Altitude = Degrees(std::asin(std::clamp(Direction.y, -1.0f, 1.0f)));
    if (Altitude < -2.0f)
    {
        return Vector3{ 0.0f, 0.0f, 0.0f };
    }
    const float AltitudePositive = std::max(Altitude, 0.0f);

    const float dAz = std::acos(std::clamp(Facing * 2.0f - 1.0f, -1.0f, 1.0f));
    const float az = std::exp(-std::pow(dAz / 0.95f, 2.0f));
    const float azWide = std::exp(-std::pow(dAz / 1.8f, 2.0f));
    const float tw = SmoothStep(-16.0f, -5.0f, ElevationDeg) * (1.0f - SmoothStep(0.5f, 6.0f, ElevationDeg));
    const float Depth = Clamp01(-ElevationDeg / 10.0f);

    const Vector3 c0{ 1.0f, 0.88f, 0.62f };
    const Vector3 c1{ 1.0f, 0.62f, 0.28f };
    const Vector3 c2{ 0.95f, 0.42f, 0.30f };
    const Vector3 c3{ 0.62f, 0.36f, 0.48f };
    const Vector3 c4{ 0.25f, 0.30f, 0.58f };

    const float u = std::log2(1.0f + AltitudePositive * 2.0f);
    Vector3 c = Mix(c0, c1, SmoothStep(0.0f, 1.6f, u));
    c = Mix(c, c2, SmoothStep(1.6f, 2.9f, u));
    c = Mix(c, c3, SmoothStep(2.9f, 4.0f, u));
    c = Mix(c, c4, SmoothStep(4.0f, 5.2f, u));
    c = Mix(c, Mix(c2, c4, 0.6f), Depth * 0.6f);

    const float H = Mix(1.9f, 3.8f, Depth);
    const float Envelope = std::exp(-AltitudePositive / H) * (1.0f - Depth * 0.35f);
    const float Rim = std::exp(-AltitudePositive / 0.45f) * (1.0f - Depth);

    Vector3 Glow = (c * (Envelope * 0.30f) + c0 * (Rim * 0.25f)) * ((az * 0.85f + azWide * 0.15f) * tw);

    //    The white line: only from about -5.5° to sunrise, then handed over to the disc.
    const float LineWindow = Mix(1.0f,
                                 SmoothStep(-5.5f, -2.5f, ElevationDeg) * (1.0f - SmoothStep(-0.6f, 0.3f, ElevationDeg)),
                                 Criteria.Atmosphere.LineAtCivilOnly ? 1.0f : 0.0f);
    const float LineAz = std::exp(-std::pow(dAz / 0.55f, 2.0f));
    const float Line = std::exp(-std::pow(Altitude / 0.11f, 2.0f)) * (0.7f + 0.3f * SmoothStep(-0.4f, 0.0f, Altitude));
    Glow += Vector3{ 1.0f, 0.98f, 0.92f } * (Line * LineAz * LineWindow * Criteria.Atmosphere.LineIntensity * 0.45f);

    //    The earth-shadow dome fill, strongest high up and opposite the sun.
    const float DomeWindow = SmoothStep(-16.0f, -8.0f, ElevationDeg) * (1.0f - SmoothStep(-2.0f, 4.0f, ElevationDeg));
    Glow += Vector3{ 0.10f, 0.15f, 0.30f } * (0.035f * DomeWindow * (1.0f - std::exp(-AltitudePositive / 6.0f)) * (1.0f - 0.5f * az));

    return Glow * Criteria.Atmosphere.GlowIntensity;
}

//------------------------------------------------------------------------------------------------------------------------
//                                            STAR FIELD — reference §44-77
//------------------------------------------------------------------------------------------------------------------------
//    The Milky Way band with its dust lanes, then up to four octahedral star layers with a 3×3 neighbour search
//    so no star is clipped at a cell edge. Flat-topped discs with a half-pixel edge, a halo, and diffraction
//    spikes on the brightest first-layer stars.

Vector3 CelestialIntegrator::StarField(const Vector3& Direction, float PixelAngle, float AirMass) const noexcept
{
    const StarCriteria& St = Criteria.Stars;
    //    uStarRot / uMilkyTilt are panel uploads: st_rot*D2R and st_tilt*D2R in double. The cell grid is a
    //    `floor()`, so a single ulp here can select a different star entirely — this must be the panel path.
    const Vector3 sd = RotateX(RotateY(Direction, PanelRadians(St.RotationDegrees)),
                               PanelRadians(St.MilkyTiltDegrees));
    Vector3 col{ 0.0f, 0.0f, 0.0f };

    //    The galaxy: a smooth band, 3-octave noise, dust lanes darkening the core.
    const float Band = std::exp(-std::pow(sd.y / 0.15f, 2.0f));
    const float n = ValueNoise(sd * 6.0f) * 0.5f + ValueNoise(sd * 13.0f + Splat(3.1f)) * 0.3f
                  + ValueNoise(sd * 29.0f + Splat(7.3f)) * 0.2f;
    const float Dust = SmoothStep(0.35f, 0.7f, ValueNoise(sd * 9.0f + Vector3{ 5.2f, 1.1f, 8.8f }))
                     * std::exp(-std::pow(sd.y / 0.06f, 2.0f)) * 0.8f;
    const float MilkyWay = Band * (0.35f + 0.65f * n) * (1.0f - Dust) * St.MilkyWay;
    col += Mix(Vector3{ 0.55f, 0.62f, 0.9f }, Vector3{ 0.95f, 0.85f, 0.7f }, Dust * 0.6f) * (MilkyWay * 3.2e-4f);

    //    The point stars.
    float ou = 0.0f, ov = 0.0f;
    OctahedralEncode(sd, ou, ov);

    for (int o = 0; o < 4; ++o)
    {
        if (static_cast<float>(o) >= St.Layers)
        {
            break;
        }
        const float Frequency = (o == 0) ? 18.0f : (o == 1) ? 46.0f : (o == 2) ? 110.0f : 230.0f;
        const float Density   = (o == 0) ? 0.28f : (o == 1) ? 0.42f : (o == 2) ? 0.6f : 0.7f;
        const float LayerGain = (o == 0) ? 1.0f : (o == 1) ? 0.4f : (o == 2) ? 0.15f : 0.06f;

        const float gx = ou * Frequency;
        const float gy = ov * Frequency;
        const float bx = std::floor(gx);
        const float by = std::floor(gy);

        for (int j = -1; j <= 1; ++j)
        {
            for (int i = -1; i <= 1; ++i)
            {
                const float cx = bx + static_cast<float>(i);
                const float cy = by + static_cast<float>(j);
                const Vector3 h = Hash33(Vector3{ cx, cy, static_cast<float>(o) * 17.0f });
                if (h.x < 1.0f - Density * St.Density)
                {
                    continue;
                }

                const float su = (cx + 0.5f + (h.y - 0.5f) * 0.9f) / Frequency;
                const float sv = (cy + 0.5f + (h.z - 0.5f) * 0.9f) / Frequency;
                const Vector3 StarDirection = OctahedralDecode(Clamp01(su), Clamp01(sv));
                const float Angle = std::acos(std::clamp(Dot(sd, StarDirection), -1.0f, 1.0f));

                float Bright = (std::pow(Hash13(Vector3{ cx, cy, 3.3f + static_cast<float>(o) }), 8.0f) * 8.0f + 0.15f) * LayerGain;
                Bright *= Mix(1.0f, Band * 2.2f + 0.35f, St.MilkyWay * 0.6f);

                const float Phase = Hash13(Vector3{ cx, cy, 5.7f }) * 6.283f;
                const float Twinkle = 1.0f - St.Twinkle * (0.3f + 0.5f * Clamp01(AirMass / 6.0f))
                                    * (0.5f + 0.5f * std::sin(Solved.TimeSeconds * (3.0f + Hash13(Vector3{ cx, cy, 2.2f }) * 8.0f) + Phase));

                const float Temperature = Mix(2800.0f, 11000.0f, std::pow(Hash13(Vector3{ cx, cy, 9.1f }), 1.6f));
                const Vector3 StarColour = Mix(Vector3{ 1.0f, 1.0f, 1.0f }, KelvinStar(Temperature), St.ColourStrength);

                //    Never smaller than a pixel: every star lands squarely on at least one pixel.
                const float Size = std::max(St.Size * (0.0006f + Bright * 0.00028f), PixelAngle * 0.9f);
                float Core = (1.0f - SmoothStep(Size * 0.55f, Size, Angle)) * (1.0f + 0.6f * Step(2.5f, Bright));
                Core *= 3.2f;                                   // energy normalisation for the small footprint

                const float Halo = std::exp(-std::pow(Angle / (Size * (2.0f + 6.0f * St.Glow)), 1.5f))
                                 * 0.045f * St.Glow * (Bright / 8.0f + 0.03f);

                float Spikes = 0.0f;
                if (o == 0 && Bright > 2.5f)
                {
                    const Vector3 t1 = Cross(StarDirection, Vector3{ 0.0f, 1.0f, 0.0f } + Vector3{ 1e-3f, 0.0f, 0.0f }).Normalized();
                    const Vector3 t2 = Cross(StarDirection, t1);
                    const Vector3 Delta = sd - StarDirection;
                    const float qx = Dot(Delta, t1);
                    const float qy = Dot(Delta, t2);
                    const float w = std::max(PixelAngle * 0.6f, Size * 0.35f);
                    Spikes = (std::exp(-std::abs(qx) / w * 0.5f) * std::exp(-std::abs(qy) * 70.0f)
                            + std::exp(-std::abs(qy) / w * 0.5f) * std::exp(-std::abs(qx) * 70.0f))
                           * 0.3f * St.Glow * (Bright / 8.0f);
                }

                col += StarColour * ((Core + Halo + Spikes) * Bright * Twinkle * 2.6e-3f);
            }
        }
    }

    return col * St.Brightness;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              MOONS — reference §113-141
//------------------------------------------------------------------------------------------------------------------------
//    Textured UV spheres: the local disc frame, axial tilt then spin into equirectangular UV, phase lighting
//    with a haze wrap term, limb darkening, an atmospheric rim and the outer glow.

Vector3 CelestialIntegrator::MoonDiscs(const Vector3& Direction, const Vector3& Transmittance) const noexcept
{
    Vector3 col{ 0.0f, 0.0f, 0.0f };

    for (uint32_t i = 0u; i < 4u; ++i)
    {
        if (i >= Criteria.MoonCount)
        {
            break;
        }
        const MoonCriteria& M = Criteria.Moons[i];
        if (!M.Visible)
        {
            continue;
        }

        const Vector3 md = Solved.MoonDirections[i];
        const float AngularRadius = PanelRadians(M.AngularDiameterDeg) / 2.0f;   // uMoonP.x = m.size*D2R/2
        const float Brightness = M.Brightness;
        const float Phase = M.Phase;
        const float GlowAmount = M.Glow;
        const float Spin = PanelRadians(M.SpinDegrees);                          // uMoonSurf.x = m.spin*D2R
        const float Tilt = PanelRadians(M.TiltDegrees);                          // uMoonSurf.y = m.tilt*D2R
        const float Haze = M.Haze;
        const float Gamma = M.Gamma;

        const float a = std::acos(std::clamp(Dot(Direction, md), -1.0f, 1.0f));
        const Vector3 u = Cross(md, Vector3{ 0.0f, 1.0f, 0.0f }).Normalized();
        const Vector3 v = Cross(u, md);

        if (a < AngularRadius * 1.06f)
        {
            const float SinAng = std::sin(AngularRadius);
            const float lx = Dot(Direction, u) / std::max(SinAng, 1e-9f);
            const float ly = Dot(Direction, v) / std::max(SinAng, 1e-9f);
            const float r2 = lx * lx + ly * ly;
            const float z = std::sqrt(std::max(0.0f, 1.0f - r2));

            const Vector3 n{ lx, ly, z };
            const float ct = std::cos(Tilt), st = std::sin(Tilt);
            const Vector3 nt{ n.x, n.y * ct - n.z * st, n.y * st + n.z * ct };

            const float Longitude = std::atan2(nt.x, nt.z) + Spin;
            const float Latitude  = std::asin(std::clamp(nt.y, -1.0f, 1.0f));
            const float uu = Fract(Longitude / (2.0f * kPi) + 0.5f);
            const float vv = 0.5f - Latitude / kPi;

            Vector3 Albedo = MoonSurfaces[i].Sample(uu, vv);
            Albedo = PowerOf(Albedo, Gamma) * M.Tint;

            const float ph = Phase * 2.0f * kPi;
            const Vector3 Lit{ std::sin(ph), 0.0f, std::cos(ph) };
            const float Wrap = Haze * 0.3f;
            const float NdotL = std::max((Dot(n, Lit) + Wrap) / (1.0f + Wrap), 0.0f);
            const float EdgeWidth = Mix(0.975f, 0.88f, Haze);
            const float Edge = 1.0f - SmoothStep(AngularRadius * EdgeWidth, AngularRadius * (1.0f + Haze * 0.06f), a);
            const float Limb = Mix(1.0f, 0.5f, std::pow(1.0f - z, 2.0f) * std::max(Haze, 0.35f));

            col += Albedo * (Brightness * (std::pow(NdotL, 0.8f) * Limb + 0.012f) * Edge);

            const float RimA = SmoothStep(AngularRadius * 0.8f, AngularRadius, a)
                             * (1.0f - SmoothStep(AngularRadius, AngularRadius * 1.06f, a)) * Haze;
            col += M.Tint * (RimA * Brightness * 0.35f * (0.3f + 0.7f * NdotL));
        }

        const float Lum = 0.4f + 0.6f * (0.5f + 0.5f * std::cos(Phase * 2.0f * kPi));
        const float Glow = GlowAmount * Brightness
                         * (std::exp(-a / (AngularRadius * 1.4f)) * 0.09f + std::exp(-a * 3.5f) * 0.005f) * Lum;
        col += M.Tint * Glow;
    }

    return col * Transmittance;
}

//------------------------------------------------------------------------------------------------------------------------
//                                     VOLUMETRIC CLOUDS — reference §228-268
//------------------------------------------------------------------------------------------------------------------------

float CelestialIntegrator::CloudDensity(const Vector3& p, float Lod) const noexcept
{
    const VolumetricCloudCriteria& C = Criteria.VolumetricCloud;
    const float PlanetRadius = Criteria.Atmosphere.PlanetRadius;

    //    clAlt: altitude above the sphere, in the local frame whose origin is the camera's nadir at sea level.
    const float Altitude = Vector3{ p.x, p.y + PlanetRadius, p.z }.Length() - PlanetRadius;
    const float hn = Clamp01((Altitude - C.BaseAltitude) / std::max(1.0f, C.Thickness));

    //    clHeightProfile: the per-variety vertical density envelope, in `CloudHeightProfileKernel`.
    const float Profile = CloudHeightProfileKernel(hn, C.Variety, C.Anvil);

    if (Profile <= 0.0f)
    {
        return 0.0f;
    }

    //    Wind advection, and the shear that leans the column.
    float wdx = 0.0f, wdz = 0.0f;
    if (Criteria.Wind.DrivesClouds && Criteria.Wind.Visible)
    {
        const Vector3 d = WindDisplacement(Vector3{ p.x, C.BaseAltitude + hn * C.Thickness, p.z });
        wdx = d.x * 0.8f;
        wdz = d.z * 0.8f;
    }
    else
    {
        const float w = C.DriftSpeed * Solved.TimeSeconds * 0.8f;
        wdx = std::sin(PanelRadians(C.DriftDegrees)) * w;                    // uCLWindDir = cl_winddir*D2R
        wdz = std::cos(PanelRadians(C.DriftDegrees)) * w;
    }

    Vector3 q = p;
    q.x += wdx;
    q.z += wdz;
    const float WindLength = std::sqrt(wdx * wdx + wdz * wdz);
    const float Lean = hn * C.Thickness * 0.35f / std::max(1e-3f, WindLength + 1e-3f) * (C.Variety > 2.5f ? C.Anvil : 0.2f);
    q.x += Lean * wdx;
    q.z += Lean * wdz;

    const float sc = 1.0f / (C.NoiseScale * 900.0f);
    const Vector3 s = q * sc;

    float Shape = ValueNoise(s) * 0.5f
                + ValueNoise(s * 2.02f + Vector3{ 3.1f, 1.7f, 9.2f }) * 0.25f
                + ValueNoise(s * 4.1f + Vector3{ 7.7f, 2.2f, 1.1f }) * 0.125f
                + ValueNoise(s * 8.3f + Vector3{ 1.3f, 8.8f, 4.4f }) * 0.0625f;
    Shape /= 0.9375f;

    const float CirrusStreak = (C.Variety > 4.5f) ? ValueNoise(Vector3{ s.x * 0.25f, s.y * 4.0f, s.z * 6.0f }) : 0.0f;
    Shape = Mix(Shape, Shape * 0.6f + CirrusStreak * 0.5f, Step(4.5f, C.Variety));

    const float Coverage = C.Coverage;
    const float Base = Clamp01((Shape - (1.0f - Coverage)) / std::max(1e-3f, Coverage)) * Profile;

    if (Base <= 0.0f || Lod > 0.5f)
    {
        return Base * C.Density;
    }

    const float Detail = ValueNoise(q * (sc * 9.0f) + Splat(Solved.TimeSeconds * 0.02f)) * 0.6f
                       + ValueNoise(q * (sc * 19.0f) + Splat(5.0f)) * 0.4f;
    const float Erode = Mix(Detail, 1.0f - Detail, Clamp01(hn * 3.0f)) * C.Detail * 0.45f;
    return Clamp01(Base - Erode * (1.0f - Base)) * C.Density;
}

float CelestialIntegrator::CloudLightDepth(const Vector3& p) const noexcept
{
    //    clLight: growing-spaced taps toward the sun; the far ones drop to the coarse density.
    const VolumetricCloudCriteria& C = Criteria.VolumetricCloud;
    float od = 0.0f;
    const float st = C.Thickness * 0.12f;
    for (int i = 1; i <= 5; ++i)
    {
        if (static_cast<float>(i) > C.LightTaps)
        {
            break;
        }
        const float d = st * static_cast<float>(i) * static_cast<float>(i) * 0.35f;
        od += CloudDensity(p + Solved.SunDirection * d, static_cast<float>(i) > 2.0f ? 1.0f : 0.0f) * d * 0.8f;
    }
    return od;
}

void CelestialIntegrator::CloudMarch(const Vector3& Origin, const Vector3& Direction, const Vector3& Sky,
                                     const Vector3& Transmittance, float MaximumDistance,
                                     uint32_t PixelX, uint32_t PixelY,
                                     Vector3& OutScatter, float& OutCoverage) const noexcept
{
    OutScatter  = Vector3{ 0.0f, 0.0f, 0.0f };
    OutCoverage = 0.0f;

    const VolumetricCloudCriteria& C = Criteria.VolumetricCloud;
    if (!C.Visible || C.Coverage <= 0.001f || C.Density <= 0.001f)
    {
        return;
    }

    const float PlanetRadius = Criteria.Atmosphere.PlanetRadius;
    const Vector3 po = Origin + Vector3{ 0.0f, PlanetRadius, 0.0f };
    const float R0 = PlanetRadius + C.BaseAltitude;
    const float R1 = R0 + C.Thickness;
    const float h = po.Length() - PlanetRadius;

    float i0n = 0.0f, i0f = 0.0f, i1n = 0.0f, i1f = 0.0f, ign = 0.0f, igf = 0.0f;
    RaySphere(po, Direction, R0, i0n, i0f);
    RaySphere(po, Direction, R1, i1n, i1f);
    RaySphere(po, Direction, PlanetRadius, ign, igf);

    float t0 = 0.0f, t1 = 0.0f;
    if (h < C.BaseAltitude)
    {
        if (i1f < 0.0f) { return; }
        t0 = std::max(i0f, 0.0f);
        t1 = i1f;
    }
    else if (h > C.BaseAltitude + C.Thickness)
    {
        if (i1n < 0.0f) { return; }
        t0 = i1n;
        t1 = (i0n > 0.0f) ? i0n : i1f;
        if (ign > 0.0f && ign < t0) { return; }
    }
    else
    {
        t0 = 0.0f;
        t1 = (i0n > 0.0f) ? i0n : i1f;
    }

    if (ign > 0.0f) { t1 = std::min(t1, ign); }
    t1 = std::min(t1, MaximumDistance);
    const float Far = (h > C.BaseAltitude + C.Thickness) ? std::max(60000.0f, C.Thickness * 40.0f) : C.Thickness * 14.0f;
    t1 = std::min(t1, t0 + Far);
    if (t1 <= t0) { return; }

    const float N = C.MarchSteps;
    const float dt = (t1 - t0) / N;
    //    The reference jitters with `hash13(vec3(gl_FragCoord.xy, fract(uTime*3.)))` — same hash, same inputs.
    float t = t0 + dt * Hash13(Vector3{ static_cast<float>(PixelX) + 0.5f, static_cast<float>(PixelY) + 0.5f,
                                        Fract(Solved.TimeSeconds * 3.0f) });

    const float LodFar = SmoothStep(20000.0f, 200000.0f, t0);
    const float CosSun = Dot(Direction, Solved.SunDirection);
    const float Phase = Mix(HenyeyGreenstein(CosSun, C.ForwardLobe), HenyeyGreenstein(CosSun, -C.BackwardLobe), C.LobeMix) * 4.0f * 3.14159f;
    const float SunUp = Clamp01(Solved.SunDirection.y * 4.0f + 0.15f);
    const Vector3 SunLight = Transmittance * Solved.SunColour * (Criteria.Sun.Intensity * 0.02f * SunUp);
    const Vector3 Ambient = Sky * C.AmbientShare;

    Vector3 Accumulated{ 0.0f, 0.0f, 0.0f };
    float T = 1.0f;
    const float SigmaAbsorb = C.Absorption;

    for (int i = 0; i < 64; ++i)
    {
        if (static_cast<float>(i) >= N || T < 0.015f)
        {
            break;
        }
        const Vector3 p = Origin + Direction * t;
        const float Rho = CloudDensity(p, LodFar);

        if (Rho > 1e-3f)
        {
            const float od = Mix(CloudLightDepth(p), Rho * C.Thickness * 0.3f, LodFar);
            const float Altitude = Vector3{ p.x, p.y + PlanetRadius, p.z }.Length() - PlanetRadius;
            const float hn = Clamp01((Altitude - C.BaseAltitude) / std::max(1.0f, C.Thickness));

            //    Multi-scatter: three octaves with attenuated extinction and boosted contribution.
            float MultiScatter = 0.0f;
            float a = 1.0f, b = 1.0f;
            for (int o = 0; o < 3; ++o)
            {
                MultiScatter += b * std::exp(-od * a * (1.0f + SigmaAbsorb));
                a *= 0.5f;
                b *= 0.55f;
            }

            const float Powder = 1.0f - C.Powder * std::exp(-Rho * dt * 2.0f * (1.0f + SigmaAbsorb))
                                      * (1.0f - 0.5f * Clamp01(CosSun));
            const Vector3 Li = SunLight * (Phase * MultiScatter * Powder) + Ambient * Mix(0.35f, 1.0f, hn);

            const float SigmaT = Rho * (1.0f + SigmaAbsorb) * 0.06f;
            const float Ts = std::exp(-SigmaT * dt);
            const Vector3 Scatter = Li * C.Albedo * (Rho * 0.06f);

            Accumulated += (Scatter - Scatter * Ts) * (T / std::max(SigmaT, 1e-6f));
            T *= Ts;
        }
        t += dt;
    }

    //    Aerial perspective, only when looking through air below the layer.
    const float Above = Step(C.BaseAltitude + C.Thickness, h);
    const float Aerial = (1.0f - std::exp(-t0 * 3e-5f)) * (1.0f - Above);
    Accumulated = Mix(Accumulated, Sky * ((1.0f - T) * 0.9f), Aerial * 0.6f);

    OutScatter  = Accumulated;
    OutCoverage = 1.0f - T;
}

//------------------------------------------------------------------------------------------------------------------------
//                                    CLOUD LAYER SLAB — reference `cloudLayer` §316-325
//------------------------------------------------------------------------------------------------------------------------

namespace {

float CloudNoise2(float px, float py) noexcept
{
    //    cnoise2: the 2-D value noise the thin slab uses, built on the same hash13.
    const float ix = std::floor(px), iy = std::floor(py);
    float fx = px - ix, fy = py - iy;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    const float a = Hash13(Vector3{ ix, iy, 7.0f });
    const float b = Hash13(Vector3{ ix + 1.0f, iy, 7.0f });
    const float c = Hash13(Vector3{ ix, iy + 1.0f, 7.0f });
    const float d = Hash13(Vector3{ ix + 1.0f, iy + 1.0f, 7.0f });
    return Mix(Mix(a, b, fx), Mix(c, d, fx), fy);
}

} // namespace

Vector3 CelestialIntegrator::CloudSlab(const Vector3& Direction, const Vector3& Sky, const Vector3& Transmittance,
                                       const ObserverFrame& Observer, float& OutCoverage) const noexcept
{
    OutCoverage = 0.0f;
    const CloudLayerCriteria& C = Criteria.CloudLayer;
    if (!C.Visible || Direction.y < 0.015f)
    {
        return Vector3{ 0.0f, 0.0f, 0.0f };
    }
    const float dz = C.Altitude - Observer.Height;
    if (dz <= 1.0f)
    {
        return Vector3{ 0.0f, 0.0f, 0.0f };
    }

    const float t = dz / Direction.y;
    const float uvx = (Observer.Position.x + Direction.x * t) * 0.0009f * C.NoiseScale + Solved.TimeSeconds * 0.0025f * C.DriftSpeed;
    const float uvy = (Observer.Position.z + Direction.z * t) * 0.0009f * C.NoiseScale;

    float n = CloudNoise2(uvx, uvy) * 0.55f
            + CloudNoise2(uvx * 2.4f + 9.0f, uvy * 2.4f + 9.0f) * 0.3f
            + CloudNoise2(uvx * 5.7f - 3.0f, uvy * 5.7f - 3.0f) * (0.07f + C.Detail * 0.08f);
    n /= (0.92f + C.Detail * 0.08f);

    const float Threshold = 1.0f - C.Coverage * 0.78f;
    const float Body = Clamp01((n - Threshold) / std::max(1e-3f, 1.0f - Threshold));
    if (Body <= 0.0f)
    {
        return Vector3{ 0.0f, 0.0f, 0.0f };
    }

    float Alpha = (0.16f + Body * 0.72f) * C.Density;
    Alpha *= SmoothStep(0.015f, 0.08f, Direction.y);

    const float SunUp = Clamp01(Solved.SunDirection.y * 3.0f + 0.2f);
    Vector3 Lit = Mix(C.Shade, C.Tint, Body * 0.55f + 0.2f)
                * (Transmittance * Solved.SunColour * (SunUp * 0.9f) + Sky * 0.9f)
                * (Criteria.Sky.Brightness * 0.6f);
    const float Silver = std::pow(std::max(Dot(Direction, Solved.SunDirection), 0.0f), 24.0f) * 0.5f;
    Lit += Transmittance * Solved.SunColour * (Silver * Criteria.Sun.Intensity * 0.02f * (1.0f - Body));

    OutCoverage = Alpha;
    return Lit * Alpha;
}

//------------------------------------------------------------------------------------------------------------------------
//                              LOCAL VOLUMETRICS — reference `vfDensity` / `lcDensity` / `marchLocal`
//------------------------------------------------------------------------------------------------------------------------

namespace {

Vector3 LocalFogHalfExtents(const LocalFogCriteria& F) noexcept
{
    //    vfHalf: the effective half-extents per shape.
    if (F.Shape < 1.5f) { return F.HalfExtents; }
    if (F.Shape < 2.5f) { return Splat(F.HalfExtents.x); }
    return Vector3{ F.HalfExtents.x, F.HalfExtents.y * 0.5f, F.HalfExtents.x };
}

Vector3 LocalFogCentre(const LocalFogCriteria& F) noexcept
{
    return F.Shape > 2.5f ? F.Placement + Vector3{ 0.0f, F.HalfExtents.y * 0.5f, 0.0f } : F.Placement;
}

float LocalFogShapeMask(const LocalFogCriteria& F, const Vector3& q) noexcept
{
    float m = 0.0f;
    if (F.Shape < 0.5f)
    {
        const Vector3 a{ 1.0f - std::abs(q.x), 1.0f - std::abs(q.y), 1.0f - std::abs(q.z) };
        m = std::min(std::min(a.x, a.y), a.z);
    }
    else if (F.Shape < 1.5f) { m = 1.0f - q.Length(); }
    else if (F.Shape < 2.5f) { m = 1.0f - q.Length(); }
    else
    {
        const Vector3 e{ F.HalfExtents.x, F.HalfExtents.y, F.HalfExtents.x };
        const Vector3 w = q * LocalFogHalfExtents(F);
        const Vector3 qq{ w.x / e.x, std::max(w.y + e.y * 0.5f, 0.0f) / e.y, w.z / e.x };
        m = std::min(1.0f - qq.Length(), (w.y + e.y * 0.5f) / e.y * 4.0f);
    }

    if (F.Falloff)
    {
        return std::pow(SmoothStep(0.0f, 1.0f, Clamp01(m)), Mix(0.35f, 3.0f, F.Softness));
    }
    return SmoothStep(0.0f, std::max(0.02f, F.Softness * 0.5f), m);
}

// The reference's `vfBox` / `lcBox`: slab intersection of an axis-aligned volume.
void VolumeInterval(const Vector3& Origin, const Vector3& Direction, const Vector3& Centre, const Vector3& HalfSize,
                    float& OutNear, float& OutFar) noexcept
{
    auto SignOf = [](float v) { return v > 0.0f ? 1.0f : (v < 0.0f ? -1.0f : 0.0f); };
    const Vector3 m{ 1.0f / (Direction.x + 1e-6f * SignOf(Direction.x) + 1e-9f),
                     1.0f / (Direction.y + 1e-6f * SignOf(Direction.y) + 1e-9f),
                     1.0f / (Direction.z + 1e-6f * SignOf(Direction.z) + 1e-9f) };
    const Vector3 n = m * (Origin - Centre);
    const Vector3 k{ std::abs(m.x) * HalfSize.x, std::abs(m.y) * HalfSize.y, std::abs(m.z) * HalfSize.z };
    const Vector3 t1 = Negate(n) - k;
    const Vector3 t2 = Negate(n) + k;
    const float tn = std::max(std::max(t1.x, t1.y), t1.z);
    const float tf = std::min(std::min(t2.x, t2.y), t2.z);
    if (tn > tf || tf < 0.0f)
    {
        OutNear = -1.0f;
        OutFar  = -1.0f;
        return;
    }
    OutNear = std::max(tn, 0.0f);
    OutFar  = tf;
}

} // namespace

float CelestialIntegrator::LocalFogDensity(const Vector3& p) const noexcept
{
    const LocalFogCriteria& F = Criteria.LocalFog;
    const Vector3 Half = LocalFogHalfExtents(F);
    const Vector3 Centre = LocalFogCentre(F);
    const Vector3 q{ (p.x - Centre.x) / Half.x, (p.y - Centre.y) / Half.y, (p.z - Centre.z) / Half.z };
    const float Mask = LocalFogShapeMask(F, q);
    if (Mask <= 0.0f)
    {
        return 0.0f;
    }

    Vector3 w{ 0.0f, 0.0f, 0.0f };
    if (Criteria.Wind.DrivesLocalFog && Criteria.Wind.Visible)
    {
        w = (WindDisplacement(p) + SwirlAt(p)) * (0.35f / std::max(0.5f, F.NoiseScale));
    }
    else
    {
        w = Vector3{ Solved.TimeSeconds * F.DriftSpeed * 0.35f,
                     Solved.TimeSeconds * F.DriftSpeed * 0.05f,
                     Solved.TimeSeconds * F.DriftSpeed * 0.2f };
    }
    const Vector3 s = p / std::max(0.5f, F.NoiseScale) + w;

    const float n = ValueNoise(s) * 0.55f + ValueNoise(s * 2.3f + Splat(1.7f)) * 0.3f + ValueNoise(s * 5.1f + Splat(4.2f)) * 0.15f;
    const float Heterogeneity = Mix(1.0f, SmoothStep(0.28f, 0.85f, n) * 1.6f, F.NoiseStrength);
    const float Gravity = 1.0f - 0.35f * Clamp01(q.y * 0.5f + 0.5f);
    return F.Density * Mask * Heterogeneity * Gravity;
}

float CelestialIntegrator::LocalCloudDensity(const Vector3& p, float Lod) const noexcept
{
    const LocalCloudCriteria& C = Criteria.LocalCloud;
    const Vector3 q{ (p.x - C.Placement.x) / C.HalfExtents.x,
                     (p.y - C.Placement.y) / C.HalfExtents.y,
                     (p.z - C.Placement.z) / C.HalfExtents.z };

    float m = 0.0f;
    if (C.Shape < 0.5f)
    {
        const Vector3 a{ 1.0f - std::abs(q.x), 1.0f - std::abs(q.y), 1.0f - std::abs(q.z) };
        m = std::min(std::min(a.x, a.y), a.z);
    }
    else
    {
        m = 1.0f - q.Length();
    }
    const float Mask = SmoothStep(0.0f, std::max(0.05f, C.Softness), m);
    if (Mask <= 0.0f)
    {
        return 0.0f;
    }

    const float hn = Clamp01(q.y * 0.5f + 0.5f);
    const float Profile = (C.Variety < 0.5f) ? SmoothStep(0.0f, 0.1f, hn) * (1.0f - SmoothStep(0.75f, 1.0f, hn))
                        : (C.Variety < 1.5f) ? SmoothStep(0.0f, 0.08f, hn) * (1.0f - SmoothStep(0.4f, 1.0f, hn)) * 1.15f
                        :                      SmoothStep(0.0f, 0.3f, hn) * (1.0f - SmoothStep(0.6f, 1.0f, hn)) * 0.6f;

    Vector3 Offset{ 0.0f, 0.0f, 0.0f };
    if (Criteria.Wind.DrivesLocalCloud && Criteria.Wind.Visible)
    {
        Offset = WindDisplacement(p) * 0.6f + SwirlAt(p) * 0.6f;
    }
    else
    {
        Offset = Vector3{ Solved.TimeSeconds * C.DriftSpeed * 0.6f, 0.0f, Solved.TimeSeconds * C.DriftSpeed * 0.2f };
    }
    const Vector3 s = (p + Offset) / std::max(1.0f, C.NoiseScale);

    float Shape = ValueNoise(s) * 0.5f
                + ValueNoise(s * 2.02f + Vector3{ 3.1f, 1.7f, 9.2f }) * 0.25f
                + ValueNoise(s * 4.1f + Vector3{ 7.7f, 2.2f, 1.1f }) * 0.125f
                + ValueNoise(s * 8.3f + Vector3{ 1.3f, 8.8f, 4.4f }) * 0.0625f;
    Shape /= 0.9375f;

    const float Base = Clamp01((Shape - (1.0f - C.Coverage)) / std::max(1e-3f, C.Coverage)) * Profile * Mask;
    if (Base <= 0.0f || Lod > 0.5f)
    {
        return Base * C.Density;
    }

    const float Detail = ValueNoise(s * 9.0f) * 0.6f + ValueNoise(s * 19.0f + Splat(5.0f)) * 0.4f;
    const float Erode = Mix(Detail, 1.0f - Detail, Clamp01(hn * 3.0f)) * C.Detail * 0.45f;
    return Clamp01(Base - Erode * (1.0f - Base)) * C.Density;
}

float CelestialIntegrator::UnifiedShadow(const Vector3& p, float StepLength) const noexcept
{
    //    uniShadow: four taps toward the sun, accumulating the optical depth of BOTH local media, so fog
    //    shadows cloud and cloud shadows fog without a second march.
    float od = 0.0f;
    for (int i = 1; i <= 4; ++i)
    {
        const Vector3 q = p + Solved.SunDirection * (StepLength * static_cast<float>(i) * 0.5f);
        const float FogTerm = Criteria.LocalFog.Visible
                            ? LocalFogDensity(q) * (1.0f + Criteria.LocalFog.Absorption) : 0.0f;
        const float CloudTerm = Criteria.LocalCloud.Visible
                              ? LocalCloudDensity(q, 1.0f) * (1.0f + Criteria.VolumetricCloud.Absorption) * 0.06f : 0.0f;
        od += (FogTerm + CloudTerm) * StepLength * 0.5f;
    }
    return std::exp(-od);
}

void CelestialIntegrator::MarchLocalVolumes(const Vector3& Origin, const Vector3& Direction, float MaximumDistance,
                                            const Vector3& Sky, const Vector3& HorizonGlow,
                                            const Vector3& Transmittance, uint32_t PixelX, uint32_t PixelY,
                                            Vector3& OutScatter, float& OutTransmittance) const noexcept
{
    OutScatter       = Vector3{ 0.0f, 0.0f, 0.0f };
    OutTransmittance = 1.0f;

    const LocalFogCriteria& F = Criteria.LocalFog;
    const LocalCloudCriteria& C = Criteria.LocalCloud;

    float bfNear = -1.0f, bfFar = -1.0f, bcNear = -1.0f, bcFar = -1.0f;
    if (F.Visible)
    {
        VolumeInterval(Origin, Direction, LocalFogCentre(F), LocalFogHalfExtents(F), bfNear, bfFar);
    }
    if (C.Visible)
    {
        VolumeInterval(Origin, Direction, C.Placement, C.HalfExtents, bcNear, bcFar);
        if (bcNear >= 0.0f)
        {
            const Vector3 po = Origin + Vector3{ 0.0f, Criteria.Atmosphere.PlanetRadius, 0.0f };
            float ign = 0.0f, igf = 0.0f;
            RaySphere(po, Direction, Criteria.Atmosphere.PlanetRadius, ign, igf);
            if (ign > 0.0f && ign < bcNear)
            {
                bcNear = -1.0f;
                bcFar  = -1.0f;
            }
        }
    }

    const bool HasFog   = bfNear >= 0.0f && std::min(bfFar, MaximumDistance) > bfNear;
    const bool HasCloud = bcNear >= 0.0f && std::min(bcFar, MaximumDistance) > bcNear;
    if (!HasFog && !HasCloud)
    {
        return;
    }

    float t0 = 1e9f, t1 = 0.0f;
    if (HasFog)   { t0 = std::min(t0, bfNear); t1 = std::max(t1, bfFar); }
    if (HasCloud) { t0 = std::min(t0, bcNear); t1 = std::max(t1, bcFar); }
    t1 = std::min(t1, MaximumDistance);

    const float N = std::max(HasFog ? F.MarchSteps : 0.0f, HasCloud ? C.MarchSteps : 0.0f);
    if (N <= 0.0f || t1 <= t0)
    {
        return;
    }
    const float dt = (t1 - t0) / N;
    float t = t0 + dt * Hash13(Vector3{ static_cast<float>(PixelX) + 0.5f, static_cast<float>(PixelY) + 0.5f,
                                        Fract(Solved.TimeSeconds * 7.0f) });

    const float CosSun = std::clamp(Dot(Direction, Solved.SunDirection), -1.0f, 1.0f);
    const float gF = F.Anisotropy;
    const float PhaseFog = (1.0f - gF * gF) / (4.0f * 3.14159f * std::pow(std::max(1.0f + gF * gF - 2.0f * gF * CosSun, 1e-6f), 1.5f)) * 4.0f * 3.14159f;
    const float PhaseCloud = Mix(HenyeyGreenstein(CosSun, Criteria.VolumetricCloud.ForwardLobe),
                                 HenyeyGreenstein(CosSun, -Criteria.VolumetricCloud.BackwardLobe),
                                 Criteria.VolumetricCloud.LobeMix) * 4.0f * 3.14159f;

    const float SunUpFog = std::max(Solved.SunDirection.y + 0.05f, 0.0f);
    const float SunUpCloud = Clamp01(Solved.SunDirection.y * 4.0f + 0.15f);
    const Vector3 SunBase = Transmittance * Solved.SunColour * (Criteria.Sun.Intensity * 0.02f);
    const Vector3 AmbientFog = Sky * 0.75f + HorizonGlow * 0.15f;
    const Vector3 AmbientCloud = Sky * Criteria.VolumetricCloud.AmbientShare;

    const Vector3 FogHalf = LocalFogHalfExtents(F);
    const float StepFog = std::max(FogHalf.x, std::max(FogHalf.y, FogHalf.z)) * 0.5f;
    const float StepCloud = std::max(C.HalfExtents.x, std::max(C.HalfExtents.y, C.HalfExtents.z)) * 0.25f;
    const float ShadowStep = (HasFog && HasCloud) ? std::min(StepFog, StepCloud) : (HasFog ? StepFog : StepCloud);

    Vector3 Accumulated{ 0.0f, 0.0f, 0.0f };
    float T = 1.0f;

    for (int i = 0; i < 64; ++i)
    {
        if (static_cast<float>(i) >= N || T < 0.01f)
        {
            break;
        }
        const Vector3 p = Origin + Direction * t;

        const float RhoFog = (HasFog && t >= bfNear && t <= bfFar) ? LocalFogDensity(p) : 0.0f;
        const float RhoCloud = (HasCloud && t >= bcNear && t <= bcFar) ? LocalCloudDensity(p, 0.0f) : 0.0f;
        const float SigmaFog = RhoFog * (1.0f + F.Absorption);
        const float SigmaCloud = RhoCloud * (1.0f + Criteria.VolumetricCloud.Absorption) * 0.06f;
        const float SigmaTotal = SigmaFog + SigmaCloud;

        if (SigmaTotal > 1e-5f)
        {
            const float Shadow = (F.SelfShadow || RhoCloud > 0.0f) ? UnifiedShadow(p, ShadowStep) : 1.0f;
            Vector3 Source{ 0.0f, 0.0f, 0.0f };

            if (RhoFog > 0.0f)
            {
                Vector3 Li = AmbientFog + SunBase * (SunUpFog * PhaseFog * Shadow);

                //    The two local lights in-scatter into the fog, each through its own HG lobe evaluated
                //    against the light direction rather than the sun's. This is what makes a lamp inside a
                //    fog bank show a visible cone instead of merely brightening the surface under it.
                if (Criteria.PointLight.Visible)
                {
                    const PointLightCriteria& PL = Criteria.PointLight;
                    const Vector3 ToLight = PL.Placement - p;
                    const float d = std::sqrt(Dot(ToLight, ToLight));
                    const Vector3 L = ToLight / std::max(d, 1e-3f);
                    const float ph = (1.0f - gF * gF)
                                   / (4.0f * 3.14159f * std::pow(std::max(1.0f + gF * gF - 2.0f * gF * Dot(Direction, L), 1e-6f), 1.5f));
                    Li += PL.Colour * (PL.Intensity / std::pow(std::max(d, 1.0f), PL.Decay)
                                     * (1.0f - SmoothStep(PL.Reach * 0.7f, PL.Reach, d))
                                     * ph * 4.0f * 3.14159f * 0.02f);
                }
                if (Criteria.SpotLight.Visible)
                {
                    const SpotLightCriteria& SL = Criteria.SpotLight;
                    const Vector3 ToLight = SL.Placement - p;
                    const float d = std::sqrt(Dot(ToLight, ToLight));
                    const Vector3 L = ToLight / std::max(d, 1e-3f);
                    const float CosTheta = Dot(Negate(L), SL.Direction());
                    const float CosHalf = SL.CosineHalfAngle();
                    const float Cone = SmoothStep(CosHalf, Mix(CosHalf, 1.0f, SL.Penumbra * 0.9f) + 1e-4f, CosTheta);
                    const float ph = (1.0f - gF * gF)
                                   / (4.0f * 3.14159f * std::pow(std::max(1.0f + gF * gF - 2.0f * gF * Dot(Direction, L), 1e-6f), 1.5f));
                    Li += SL.Colour * ((SL.Intensity / std::max(d * d, 1.0f)) * Cone * ph * 4.0f * 3.14159f * 0.02f);
                }

                Source += Li * F.Albedo * RhoFog;
            }
            if (RhoCloud > 0.0f)
            {
                const float hn = Clamp01((p.y - C.Placement.y) / std::max(1.0f, C.HalfExtents.y) * 0.5f + 0.5f);
                const float Powder = 1.0f - Criteria.VolumetricCloud.Powder
                                   * std::exp(-RhoCloud * dt * 2.0f * (1.0f + Criteria.VolumetricCloud.Absorption))
                                   * (1.0f - 0.5f * Clamp01(CosSun));
                const float MultiScatter = Shadow + 0.55f * std::pow(Shadow, 0.5f) + 0.3f * std::pow(Shadow, 0.25f);
                const Vector3 Li = SunBase * (SunUpCloud * PhaseCloud * MultiScatter * Powder)
                                 + AmbientCloud * Mix(0.35f, 1.0f, hn);
                Source += Li * Criteria.VolumetricCloud.Albedo * (RhoCloud * 0.06f);
            }

            const float Ts = std::exp(-SigmaTotal * dt);
            Accumulated += (Source - Source * Ts) * (T / std::max(SigmaTotal, 1e-6f));
            T *= Ts;
        }
        t += dt;
    }

    OutScatter       = Accumulated;
    OutTransmittance = T;
}

//------------------------------------------------------------------------------------------------------------------------
//                                 ANALYTIC MEDIA — reference `expHeightK` / `fogT` / `applyMedia`
//------------------------------------------------------------------------------------------------------------------------

Vector3 CelestialIntegrator::ApplyMedia(const Vector3& Radiance, const Vector3& Direction, float Distance,
                                        const Vector3& SkyAmbient, const Vector3& HorizonGlow,
                                        const Vector3& Transmittance, float ObserverHeight) const noexcept
{
    const bool HasHeightFog = Criteria.HeightFog.Visible && ObserverHeight < 3000.0f;
    const bool HasAerialFog = Criteria.AtmosphericFog.Visible && ObserverHeight < 6000.0f;
    if (!HasHeightFog && !HasAerialFog)
    {
        return Radiance;
    }

    const float y0 = ObserverHeight;
    const float y1 = ObserverHeight + Direction.y * Distance;

    //    expHeightK, hoisted to file scope as `HeightIntegralKernel` so the transliteration proof exercises
    //    this exact definition rather than a restatement of it.
    auto HeightKernel = HeightIntegralKernel;

    const float CosSun = std::clamp(Dot(Direction, Solved.SunDirection), -1.0f, 1.0f);
    const Vector3 SunLight = Transmittance * Solved.SunColour * (Criteria.Sun.Intensity * 0.02f);

    float Tf = 1.0f;
    Vector3 Lf{ 0.0f, 0.0f, 0.0f };
    if (HasHeightFog)
    {
        const float od = Criteria.HeightFog.Density * Distance
                       * HeightKernel(std::max(1.0f, Criteria.HeightFog.FalloffHeight), y0, y1);
        Tf = std::exp(-od * od);                                // the reference's exp² of distance
        constexpr float g = 0.55f;
        const float hg = (1.0f - g * g) / (4.0f * 3.14159f * std::pow(std::max(1.0f + g * g - 2.0f * g * CosSun, 1e-6f), 1.5f));
        Lf = Criteria.HeightFog.Tint * (SkyAmbient * 0.9f + HorizonGlow * 0.35f)
           + SunLight * (Criteria.HeightFog.SunScatter * hg * std::max(Solved.SunDirection.y + 0.1f, 0.0f));
    }

    Vector3 Ta{ 1.0f, 1.0f, 1.0f };
    Vector3 La{ 0.0f, 0.0f, 0.0f };
    if (HasAerialFog)
    {
        const AtmosphericFogCriteria& A = Criteria.AtmosphericFog;
        const float od = A.Density * std::max(0.0f, Distance - A.StartDistance)
                       * HeightKernel(std::max(1.0f, A.FalloffHeight), y0, y1);
        if (od > 1e-6f)
        {
            const Vector3 Beta = Mix(Vector3{ 5.8e-6f, 13.5e-6f, 33.1e-6f } / 13.5e-6f, Splat(1.0f), A.MieShare);
            Ta = ExponentOf(Negate(Beta * od));
            const float g = A.Anisotropy;
            const float hg = (1.0f - g * g) / (4.0f * 3.14159f * std::pow(std::max(1.0f + g * g - 2.0f * g * CosSun, 1e-6f), 1.5f));
            La = ((SkyAmbient * 0.9f + HorizonGlow * 0.25f) * A.SkyShare
                 + SunLight * (hg * 4.0f * 3.14159f * std::max(Solved.SunDirection.y + 0.08f, 0.0f) * Mix(1.0f, 0.35f, A.MieShare)))
               * A.Tint;
        }
    }

    //    Each medium's in-scatter is attenuated by the other's transmittance, proportionally.
    const Vector3 T = Ta * Tf;
    const Vector3 Lin = La * (1.0f - Ta.x) * Mix(1.0f, Tf, 0.5f)
                      + Lf * ((1.0f - Tf) ) * Mix(Splat(1.0f), Ta, 0.5f);
    return Radiance * T + Lin;
}

//------------------------------------------------------------------------------------------------------------------------
//                                         RAINBOW — reference `bowAngle` / `rainbow`
//------------------------------------------------------------------------------------------------------------------------

float CelestialIntegrator::BowAngle(float WavelengthNm, float Order) noexcept
{
    //    Cauchy dispersion for water, then Descartes' minimum deviation. n ≈ 1.331 @700nm, 1.343 @400nm.
    const float n = 1.3245f + 3000.0f / (WavelengthNm * WavelengthNm);
    const float k = Order;
    const float CosI = std::sqrt((n * n - 1.0f) / (k * k + 2.0f * k));
    const float i = std::acos(std::clamp(CosI, -1.0f, 1.0f));
    const float r = std::asin(std::clamp(std::sin(i) / n, -1.0f, 1.0f));
    const float Deviation = 2.0f * i - 2.0f * (k + 1.0f) * r + k * 3.14159265f;
    return (Order < 1.5f) ? (3.14159265f - Deviation) : (Deviation - 3.14159265f);
}

Vector3 CelestialIntegrator::Rainbow(const Vector3& Direction, float RainVisibility, float SkyLuminance) const noexcept
{
    const RainbowCriteria& R = Criteria.Rainbow;
    if (!R.Visible)
    {
        return Vector3{ 0.0f, 0.0f, 0.0f };
    }
    const float SunUp = SmoothStep(-2.0f, 3.0f, Degrees(std::asin(std::clamp(Solved.SunDirection.y, -1.0f, 1.0f))));
    if (SunUp <= 0.0f)
    {
        return Vector3{ 0.0f, 0.0f, 0.0f };
    }

    const Vector3 Anti = Negate(Solved.SunDirection);
    const float Angle = std::acos(std::clamp(Dot(Direction, Anti), -1.0f, 1.0f));
    if (Angle > 1.0f || Angle < 0.6f)                           // the 34°..57° window
    {
        return Vector3{ 0.0f, 0.0f, 0.0f };
    }

    const float w = R.Width;
    Vector3 col{ 0.0f, 0.0f, 0.0f };
    float Sum = 0.0f;
    for (int i = 0; i < 14; ++i)
    {
        const float wl = 380.0f + static_cast<float>(i) / 13.0f * 320.0f;
        const Vector3 c = WavelengthColour(wl);
        const float a1 = BowAngle(wl, 1.0f);
        const float d1 = (Angle - a1) / (0.0035f * w);
        col += c * std::exp(-d1 * d1);
        const float a2 = BowAngle(wl, 2.0f);
        const float d2 = (Angle - a2) / (0.006f * w);
        col += c * (std::exp(-d2 * d2) * 0.42f * R.Secondary);
        Sum += 1.0f;
    }
    col = col / (Sum * 0.42f);

    //    Alexander's dark band between the bows, and the brighter inside of the primary.
    const float Inner = 1.0f - SmoothStep(BowAngle(560.0f, 1.0f) - 0.02f, BowAngle(560.0f, 1.0f), Angle);
    const float Alexander = SmoothStep(BowAngle(400.0f, 1.0f), BowAngle(400.0f, 1.0f) + 0.02f, Angle)
                          * (1.0f - SmoothStep(BowAngle(700.0f, 2.0f) - 0.02f, BowAngle(700.0f, 2.0f), Angle));
    const Vector3 SkyGlow = Splat(0.012f) * (Inner * R.Intensity);
    col = col * R.Intensity + SkyGlow;
    col *= (1.0f - Alexander * 0.18f);

    //    Supernumeraries just inside the primary — interference, faint.
    const float Primary = BowAngle(560.0f, 1.0f);
    const float Super = R.Supernumerary * std::exp(-std::pow((Primary - Angle) / 0.02f, 2.0f))
                      * (0.5f + 0.5f * std::cos((Primary - Angle) * 900.0f)) * (Angle < Primary ? 1.0f : 0.0f);
    col += Vector3{ 0.5f, 0.6f, 1.0f } * (Super * 0.25f * R.Intensity);

    const float BelowHorizon = SmoothStep(0.8f, 0.3f, Direction.y);
    return col * (SunUp * RainVisibility * BelowHorizon * std::max(SkyLuminance * 2.2f, 0.02f) * 1.6f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                          LENS FLARE — reference `lensFlare`
//------------------------------------------------------------------------------------------------------------------------

Vector3 CelestialIntegrator::LensFlare(float u, float v, float SunU, float SunV, float Visibility) const noexcept
{
    const PostCriteria& P = Criteria.Post;
    Vector3 f{ 0.0f, 0.0f, 0.0f };
    const float mx = u - SunU;
    const float my = v - SunV;
    const float Variety = P.FlareVariety;

    const float WeightGhost  = (Variety == 0.0f || Variety == 2.0f) ? 1.0f : 0.0f;
    const float WeightHalo   = (Variety == 0.0f || Variety == 3.0f) ? 1.0f : (Variety == 2.0f ? 0.5f : 0.0f);
    const float WeightStreak = (Variety == 1.0f) ? 2.2f : (Variety == 0.0f ? 1.0f : 0.35f);
    const float WeightBurst  = (Variety == 2.0f) ? 1.0f : (Variety == 0.0f ? 0.35f : 0.0f);

    for (int i = 0; i < 8; ++i)
    {
        if (static_cast<float>(i) >= P.Ghosts)
        {
            break;
        }
        const float fi = static_cast<float>(i);
        const float k = -1.35f + fi * 0.42f;
        const float px = SunU * k;
        const float py = SunV * k;
        const float r = 0.035f + 0.07f * Fract(fi * 0.618f + 0.31f);
        const float dd = std::sqrt((u - px) * (u - px) + (v - py) * (v - py));
        float g = SmoothStep(r, r * 0.35f, dd) * 0.9f + SmoothStep(r * 1.6f, r, dd) * 0.25f;
        g *= (0.06f + 0.06f * Fract(fi * 0.37f));
        f += Mix(Splat(1.0f), HueOf(Fract(fi * 0.23f + 0.5f)), P.Chroma) * (g * WeightGhost);
    }

    const float hx = u - SunU * 0.25f;
    const float hy = v - SunV * 0.25f;
    const float hr = std::sqrt(hx * hx + hy * hy);
    const float RingDistance = std::abs(hr - P.Halo);
    const float Halo = SmoothStep(0.045f, 0.0f, RingDistance) * 0.09f;
    const float RingAngle = std::atan2(hy, hx);
    f += Mix(Splat(1.0f), HueOf(Fract(RingAngle / 6.283f + RingDistance * 8.0f)), P.Chroma * 0.8f) * (Halo * WeightHalo);

    const float Streak = std::exp(-std::abs(my) * 95.0f) * std::exp(-std::abs(mx) * 2.2f) * 0.55f;
    f += Mix(Splat(1.0f), Vector3{ 0.45f, 0.65f, 1.0f }, P.Chroma) * (Streak * P.Streak * WeightStreak);

    const float a = std::atan2(my, mx);
    const float Length = std::sqrt(mx * mx + my * my);
    const float Burst = std::pow(std::abs(std::sin(a * 4.0f + 0.3f)), 24.0f) * std::exp(-Length * 3.5f) * 0.35f
                      + std::pow(std::abs(std::sin(a * 7.0f)), 40.0f) * std::exp(-Length * 6.0f) * 0.25f;
    f += Vector3{ 1.0f, 0.95f, 0.85f } * (Burst * WeightBurst);
    f += Vector3{ 1.0f, 0.9f, 0.8f } * (std::exp(-Length * 1.6f) * 0.04f);

    return f * (Visibility * P.FlareIntensity);
}

//------------------------------------------------------------------------------------------------------------------------
//                               LOCAL LIGHTS ON A SURFACE — reference `main()` plane branch
//------------------------------------------------------------------------------------------------------------------------
//    The two photometric lights the panel holds in `S.plP` / `S.slP`. The reference uploads them every frame
//    and both default to ON, so any surface the port shades has to answer to them:
//
//        point : uPLCol * uPLInt / max(d,1)^uPLDecay * (1 - smoothstep(reach*.7, reach, d)) * max(L.y,0) * .02
//        spot  : uSLCol * uSLInt / max(d²,1) * cone(penumbra) * max(L.y,0) * .02
//
//    `max(L.y, 0)` is the reference's own stand-in for N·L on a level plane; the general form below uses the
//    surface normal where one is available, which is the same expression when the normal is +Y.

Vector3 CelestialIntegrator::LocalLightsOnSurface(const Vector3& Position, const Vector3& Albedo) const noexcept
{
    Vector3 Radiance{ 0.0f, 0.0f, 0.0f };

    if (Criteria.PointLight.Visible)
    {
        const PointLightCriteria& P = Criteria.PointLight;
        const Vector3 ToLight = P.Placement - Position;
        const float d = std::sqrt(Dot(ToLight, ToLight));
        const Vector3 L = ToLight / std::max(d, 1e-3f);
        const float Falloff = P.Intensity / std::pow(std::max(d, 1.0f), P.Decay)
                            * (1.0f - SmoothStep(P.Reach * 0.7f, P.Reach, d));
        Radiance += Albedo * P.Colour * (Falloff * std::max(L.y, 0.0f) * 0.02f);
    }

    if (Criteria.SpotLight.Visible)
    {
        const SpotLightCriteria& S = Criteria.SpotLight;
        const Vector3 ToLight = S.Placement - Position;
        const float d = std::sqrt(Dot(ToLight, ToLight));
        const Vector3 L = ToLight / std::max(d, 1e-3f);
        const float CosTheta = Dot(Negate(L), S.Direction());
        const float CosHalf = S.CosineHalfAngle();
        const float Cone = SmoothStep(CosHalf, Mix(CosHalf, 1.0f, S.Penumbra * 0.9f) + 1e-4f, CosTheta);
        Radiance += Albedo * S.Colour * ((S.Intensity / std::max(d * d, 1.0f)) * Cone * std::max(L.y, 0.0f) * 0.02f);
    }

    return Radiance;
}

//------------------------------------------------------------------------------------------------------------------------
//                                     HEIGHT FIELD — reference `tfield` / `terrH` / `terrHit`
//------------------------------------------------------------------------------------------------------------------------
//    The sculpted terrain: three sinusoids in the patch's normalised coordinates, sphere-traced with a growing
//    step and refined by linear interpolation across the sign change, then differenced for the normal.

float CelestialIntegrator::TerrainField(float qx, float qz) const noexcept
{
    const TerrainCriteria& T = Criteria.Terrain;
    const float f = T.Frequency;
    const float s = T.Seed;
    return Clamp01(0.5f + 0.23f * std::sin((qx * 5.3f + s * 0.013f) * f)
                        + 0.16f * std::sin((qz * 7.1f - s * 0.019f) * f)
                        + 0.09f * std::sin((qx + qz) * 15.7f * f));
}

float CelestialIntegrator::TerrainHeightAt(float x, float z) const noexcept
{
    const TerrainCriteria& T = Criteria.Terrain;
    const float qx = (x - T.Placement.x) / T.Size + 0.5f;
    const float qz = (z - T.Placement.z) / T.Size + 0.5f;
    return T.Placement.y + TerrainField(qx, qz) * T.Height;
}

bool CelestialIntegrator::TerrainIntersect(const Vector3& Origin, const Vector3& Direction,
                                           float& OutDistance, Vector3& OutNormal) const noexcept
{
    OutDistance = -1.0f;
    const TerrainCriteria& T = Criteria.Terrain;
    if (!T.Visible)
    {
        return false;
    }

    const float Half = T.Size * 0.5f;
    float t = 0.0f;
    float dt = std::max(0.5f, T.Size / 160.0f);
    float PreviousDelta = 0.0f;
    bool Hit = false;

    for (int i = 0; i < 160; ++i)
    {
        const Vector3 p = Origin + Direction * t;
        const float lx = std::abs(p.x - T.Placement.x);
        const float lz = std::abs(p.z - T.Placement.z);
        if (lx > Half || lz > Half)
        {
            //    Outside the patch: give up only once the ray is climbing away above the highest ground.
            if (t > 0.0f && p.y > T.Placement.y + T.Height && Direction.y >= 0.0f) { break; }
        }
        else
        {
            const float Delta = p.y - TerrainHeightAt(p.x, p.z);
            if (Delta < 0.0f)
            {
                t -= dt * Delta / (Delta - PreviousDelta + 1e-5f);
                Hit = true;
                break;
            }
            PreviousDelta = Delta;
        }
        t += dt;
        dt *= 1.03f;
        if (t > T.Size * 4.0f) { break; }
    }

    if (!Hit)
    {
        return false;
    }

    const Vector3 p = Origin + Direction * t;
    const float e = T.Size / 256.0f;
    OutNormal = Vector3{ TerrainHeightAt(p.x - e, p.z) - TerrainHeightAt(p.x + e, p.z),
                         2.0f * e,
                         TerrainHeightAt(p.x, p.z - e) - TerrainHeightAt(p.x, p.z + e) }.Normalized();
    OutDistance = t;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                       THE GROUND UNDER THE SKY — reference `main()` terrain + checker-plane branches
//------------------------------------------------------------------------------------------------------------------------
//    The reference resolves the world before the sky: the sculpted field first, the checker plane second and
//    only if it is nearer. Both are shaded against the same sun, the same hemispherical ambient and the same
//    two local lights, then faded into the sky by the distance fog `1-exp(-t*7e-5)` raised to 1.6.
//
//    The checker is filtered rather than point-sampled. A screen-space derivative is not available on the CPU,
//    so the caller supplies the ray's footprint — the same quantity `fwidth` estimates — and the reference's
//    box-filtered checker integral is evaluated with it. Point-sampling would alias the plane into moiré at
//    grazing angles, which is the artefact `fwidth` exists to prevent.

CelestialIntegrator::GroundSample CelestialIntegrator::SampleGround(const Vector3& Direction,
                                                                    const ObserverFrame& Observer,
                                                                    float PixelFootprint) const noexcept
{
    GroundSample Result{};

    Vector3 SkyRadiance{}, Transmittance{};
    float Ground = 0.0f;
    IntegrateAtmosphere(Vector3{ 0.0f, Criteria.Atmosphere.PlanetRadius + Observer.Height, 0.0f },
                        Direction, SkyRadiance, Transmittance, Ground);
    SkyRadiance *= Criteria.Sky.Tint;
    SkyRadiance *= Criteria.Sky.Brightness * (Criteria.Sky.Visible ? 1.0f : 0.0f)
                 * (Criteria.Atmosphere.Visible ? 1.0f : 0.0f);
    if (!Criteria.Atmosphere.Visible)
    {
        Transmittance = Vector3{ 1.0f, 1.0f, 1.0f };
    }

    const float ElevationDeg = Solved.SunElevationDeg;
    const float hxz = std::sqrt(Direction.x * Direction.x + Direction.z * Direction.z) + 1e-5f;
    const float sxz = std::sqrt(Solved.SunDirection.x * Solved.SunDirection.x
                              + Solved.SunDirection.z * Solved.SunDirection.z) + 1e-5f;
    const float Facing = 0.5f + 0.5f * ((Direction.x / hxz) * (Solved.SunDirection.x / sxz)
                                      + (Direction.z / hxz) * (Solved.SunDirection.z / sxz));
    const float HdrScale = Criteria.Sun.Intensity / 22.0f * Criteria.Sky.Brightness
                         * (Criteria.Sky.Visible ? 1.0f : 0.0f);
    const float GroundObserver = 1.0f - SmoothStep(1500.0f, 12000.0f, Observer.Height);
    const Vector3 HorizonGlow = TwilightGlow(Vector3{ Direction.x, 0.0f, Direction.z }.Normalized() + Splat(1e-5f),
                                             ElevationDeg, Facing) * (HdrScale * GroundObserver);

    float PlaneDistance = 1e9f;
    Vector3 PlaneRadiance{ 0.0f, 0.0f, 0.0f };
    Vector3 PlaneNormal{ 0.0f, 1.0f, 0.0f };
    bool    PlaneHit = false;

    //    ── the sculpted height field ────────────────────────────────────────────────────────────────────────
    {
        float tHit = 0.0f;
        Vector3 n{ 0.0f, 1.0f, 0.0f };
        if (Observer.Height < 5000.0f && TerrainIntersect(Observer.Position, Direction, tHit, n))
        {
            const TerrainCriteria& T = Criteria.Terrain;
            const Vector3 hp = Observer.Position + Direction * tHit;
            const float hn = Clamp01((hp.y - T.Placement.y) / std::max(0.01f, T.Height));
            const Vector3 Albedo = Mix(T.LowColour, T.HighColour, hn);
            const float NdotL = std::max(Dot(n, Solved.SunDirection), 0.0f);
            const float Shadow = 1.0f - Criteria.GroundPlane.ShadowPresence
                               * (1.0f - SmoothStep(-1.0f, 3.0f, ElevationDeg)) * 0.35f * NdotL;

            const Vector3 Half = (Solved.SunDirection - Direction).Normalized();
            const Vector3 Specular = Transmittance * Solved.SunColour * (Criteria.Sun.Intensity * 0.02f)
                                   * (std::pow(std::max(Dot(n, Half), 0.0f), Mix(64.0f, 4.0f, T.Roughness))
                                      * (1.0f - T.Roughness));

            const Vector3 Ambient = (SkyRadiance * 0.42f + HorizonGlow * 0.08f) * (0.5f + 0.5f * n.y);
            PlaneRadiance = Albedo * (Transmittance * Solved.SunColour * (Criteria.Sun.Intensity * 0.11f * NdotL * Shadow)
                                      + Ambient) + Specular;

            if (T.Wireframe)
            {
                //    The reference draws a 32×32 wire over the patch, thresholded through `fwidth`.
                const float qx = (hp.x - T.Placement.x) / T.Size * 32.0f;
                const float qz = (hp.z - T.Placement.z) / T.Size * 32.0f;
                const float w = std::max(PixelFootprint * tHit / T.Size * 32.0f, 1e-4f);
                const float gx = std::abs(Fract(qx - 0.5f) - 0.5f) / w;
                const float gz = std::abs(Fract(qz - 0.5f) - 0.5f) / w;
                const float Line = 1.0f - std::min(std::min(gx, gz), 1.0f);
                PlaneRadiance = Mix(PlaneRadiance, Splat(0.9f), Line * 0.6f);
            }

            float Fog = 1.0f - std::exp(-tHit * 7e-5f);
            Fog = std::pow(Clamp01(Fog), 1.6f);
            PlaneRadiance = Mix(PlaneRadiance, SkyRadiance * 0.8f + HorizonGlow * 0.6f, Fog);
            PlaneDistance = tHit;
            PlaneNormal = n;
            PlaneHit = true;
        }
    }

    //    ── the checker plane ────────────────────────────────────────────────────────────────────────────────
    const GroundPlaneCriteria& G = Criteria.GroundPlane;
    if (G.Visible && Direction.y < -1e-4f && Observer.Height < 2000.0f)
    {
        const float t = (G.Height - Observer.Position.y) / Direction.y;
        if (t > 0.0f && t < PlaneDistance)
        {
            const Vector3 hp = Observer.Position + Direction * t;
            //    `step(max(|x|,|z|), uPlaneSize)` — 1 inside the checkered extent, 0 on the bare plane beyond
            //    it. Note the argument order: the reference's edge is the RADIUS and its x is the extent.
            const float InsideChecker = Step(std::max(std::abs(hp.x), std::abs(hp.z)), G.HalfSize);

            //    The reference's analytically box-filtered checker: the integral of the square wave over the
            //    footprint, which is what keeps a receding plane from aliasing.
            const float qx = hp.x / G.CellSize;
            const float qz = hp.z / G.CellSize;
            const float Footprint = std::max(PixelFootprint * t / G.CellSize, 1e-5f);
            const float wx = Footprint * 1.5f + 1e-4f;
            const float wz = Footprint * 1.5f + 1e-4f;
            auto Triangle = [](float v) { return std::abs(Fract(v * 0.5f) - 0.5f); };
            const float ix = 2.0f * (Triangle(qx - 0.5f * wx) - Triangle(qx + 0.5f * wx)) / wx;
            const float iz = 2.0f * (Triangle(qz - 0.5f * wz) - Triangle(qz + 0.5f * wz)) / wz;
            const float Checker = 0.5f - 0.5f * ix * iz;

            Vector3 Albedo = Mix(G.TintA, Mix(G.TintA, G.TintB, Checker), InsideChecker);

            if (G.Graticule)
            {
                //    Grid every cell, bold every ten, then the two coloured axis lines.
                const float g1x = std::abs(Fract(qx - 0.5f) - 0.5f) / (Footprint + 1e-4f);
                const float g1z = std::abs(Fract(qz - 0.5f) - 0.5f) / (Footprint + 1e-4f);
                const float Line = 1.0f - std::min(std::min(g1x, g1z), 1.0f);
                const float g10x = std::abs(Fract(qx / 10.0f - 0.5f) - 0.5f) / (Footprint * 0.1f + 1e-4f);
                const float g10z = std::abs(Fract(qz / 10.0f - 0.5f) - 0.5f) / (Footprint * 0.1f + 1e-4f);
                const float Line10 = 1.0f - std::min(std::min(g10x, g10z), 1.0f);
                Albedo = Mix(Albedo, Albedo * 0.55f, Line * 0.6f * InsideChecker);
                Albedo = Mix(Albedo, Vector3{ 0.35f, 0.4f, 0.5f }, Line10 * 0.8f * InsideChecker);

                const float FootprintMetres = std::max(PixelFootprint * t, 1e-5f);
                const float ax = 1.0f - std::min(std::abs(hp.z) / (FootprintMetres * 1.5f + 1e-4f), 1.0f);
                const float az = 1.0f - std::min(std::abs(hp.x) / (FootprintMetres * 1.5f + 1e-4f), 1.0f);
                Albedo = Mix(Albedo, Vector3{ 0.95f, 0.25f, 0.3f }, ax);
                Albedo = Mix(Albedo, Vector3{ 0.25f, 0.55f, 0.95f }, az);
            }

            const float NdotL = std::max(Solved.SunDirection.y, 0.0f);
            Vector3 SunLight = Transmittance * Solved.SunColour * (Criteria.Sun.Intensity * 0.11f * NdotL);

            //    Hemispherical ambient, with the reference's tiny floor so the plane never goes fully black.
            Vector3 Ambient = (SkyRadiance * 0.42f + HorizonGlow * 0.08f
                             + Vector3{ 0.0015f, 0.002f, 0.004f } * Criteria.Sky.Brightness)
                            * Mix(0.55f, 1.0f, G.GlobalPresence);
            Ambient += G.TintA * (G.GlobalPresence * 0.5f * 0.35f) * SunLight;

            const float Shadow = 1.0f - G.ShadowPresence * (1.0f - SmoothStep(-1.0f, 3.0f, ElevationDeg)) * 0.35f * NdotL;
            SunLight *= Shadow;

            //    Moonlight on the ground — the reference sums every visible moon.
            for (uint32_t k = 0u; k < Criteria.MoonCount && k < 4u; ++k)
            {
                if (!Criteria.Moons[k].Visible) { continue; }
                Ambient += Criteria.Moons[k].Tint * (Criteria.Moons[k].Brightness
                         * std::max(Solved.MoonDirections[k].y, 0.0f) * 0.0025f
                         * (0.5f + 0.5f * std::cos(Criteria.Moons[k].Phase * 6.2832f)));
            }

            PlaneRadiance = Albedo * (SunLight + Ambient);
            PlaneRadiance += LocalLightsOnSurface(hp, Albedo);

            float Fog = 1.0f - std::exp(-t * 7e-5f);
            Fog = std::pow(Clamp01(Fog), 1.6f);
            PlaneRadiance = Mix(PlaneRadiance, SkyRadiance * 0.8f + HorizonGlow * 0.6f, Fog);

            PlaneDistance = t;
            PlaneNormal = Vector3{ 0.0f, 1.0f, 0.0f };
            PlaneHit = true;
        }
    }

    Result.Hit      = PlaneHit;
    Result.Distance = PlaneDistance;
    Result.Radiance = PlaneRadiance;
    Result.Normal   = PlaneNormal;
    return Result;
}

Vector3 CelestialIntegrator::CompositeGround(const Vector3& Radiance, const Vector3& Direction, float Distance,
                                             const ObserverFrame& Observer,
                                             uint32_t PixelX, uint32_t PixelY) const noexcept
{
    //    `col = applyMedia(planeCol, ...)` then `col = col*lv.a + lv.rgb` — the reference's order exactly.
    return ApplyAerialPerspective(Radiance, Direction, Distance, Observer, PixelX, PixelY);
}

//------------------------------------------------------------------------------------------------------------------------
//                                  LIGHT GLYPHS — reference `main()` light-source glyph block
//------------------------------------------------------------------------------------------------------------------------
//    A small emissive bloom where each local light sits, occluded by the ground when the ground is nearer than
//    the light. Without it, a light that is on contributes illumination but is itself invisible.

Vector3 CelestialIntegrator::AddLightGlyphs(const Vector3& Radiance, const Vector3& Direction,
                                            const ObserverFrame& Observer,
                                            bool GroundHit, float GroundDistance) const noexcept
{
    Vector3 col = Radiance;

    if (Criteria.PointLight.Visible)
    {
        const Vector3 v = Criteria.PointLight.Placement - Observer.Position;
        const float dl = std::sqrt(Dot(v, v));
        const float c = Dot(v / std::max(dl, 1e-6f), Direction);
        const float Angle = std::acos(std::clamp(c, -1.0f, 1.0f));
        const float rr = 0.02f / std::max(1.0f, dl * 0.05f) * (1.0f + Criteria.PointLight.Intensity * 0.02f);
        const float Bloom = std::exp(-Angle * Angle / (rr * rr * 0.25f)) * 0.9f + std::exp(-Angle / rr * 0.6f) * 0.05f;
        const float Occluded = (GroundHit && GroundDistance < dl) ? 0.0f : 1.0f;
        col += Criteria.PointLight.Colour * (Occluded * Bloom * Criteria.PointLight.Intensity * 0.02f);
    }

    if (Criteria.SpotLight.Visible)
    {
        const Vector3 v = Criteria.SpotLight.Placement - Observer.Position;
        const float dl = std::sqrt(Dot(v, v));
        const float c = Dot(v / std::max(dl, 1e-6f), Direction);
        const float Angle = std::acos(std::clamp(c, -1.0f, 1.0f));
        const float rr = 0.02f / std::max(1.0f, dl * 0.05f) * (1.0f + Criteria.SpotLight.Intensity * 0.005f);
        const float Bloom = std::exp(-Angle * Angle / (rr * rr * 0.25f)) * 0.9f + std::exp(-Angle / rr * 0.6f) * 0.05f;
        const float Occluded = (GroundHit && GroundDistance < dl) ? 0.0f : 1.0f;
        col += Criteria.SpotLight.Colour * (Occluded * Bloom * Criteria.SpotLight.Intensity * 0.008f);
    }

    return col;
}

//------------------------------------------------------------------------------------------------------------------------
//                                   THE SKY ALONG A DIRECTION — reference `main()` else-branch
//------------------------------------------------------------------------------------------------------------------------

Vector3 CelestialIntegrator::SampleSkyRadiance(const Vector3& Direction, const ObserverFrame& Observer,
                                               float PixelAngle, uint32_t PixelX, uint32_t PixelY) const noexcept
{
    const Vector3 Origin{ 0.0f, Criteria.Atmosphere.PlanetRadius + Observer.Height, 0.0f };

    Vector3 Sky{}, Transmittance{};
    float Ground = 0.0f;
    IntegrateAtmosphere(Origin, Direction, Sky, Transmittance, Ground);
    Sky *= Criteria.Sky.Tint;
    Sky *= Criteria.Sky.Brightness * (Criteria.Sky.Visible ? 1.0f : 0.0f) * (Criteria.Atmosphere.Visible ? 1.0f : 0.0f);
    if (!Criteria.Atmosphere.Visible)
    {
        Transmittance = Vector3{ 1.0f, 1.0f, 1.0f };
    }

    const float ElevationDeg = Solved.SunElevationDeg;

    //    The horizontal facing term the twilight band is keyed on.
    const float hxz = std::sqrt(Direction.x * Direction.x + Direction.z * Direction.z) + 1e-5f;
    const float sxz = std::sqrt(Solved.SunDirection.x * Solved.SunDirection.x + Solved.SunDirection.z * Solved.SunDirection.z) + 1e-5f;
    const float Facing = 0.5f + 0.5f * ((Direction.x / hxz) * (Solved.SunDirection.x / sxz)
                                      + (Direction.z / hxz) * (Solved.SunDirection.z / sxz));

    const float HdrScale = Criteria.Sun.Intensity / 22.0f * Criteria.Sky.Brightness * (Criteria.Sky.Visible ? 1.0f : 0.0f);
    const float GroundObserver = 1.0f - SmoothStep(1500.0f, 12000.0f, Observer.Height);

    const Vector3 Glow = TwilightGlow(Direction, ElevationDeg, Facing) * (HdrScale * GroundObserver);
    const Vector3 HorizonGlow = TwilightGlow(Vector3{ Direction.x, 0.0f, Direction.z }.Normalized() + Splat(1e-5f),
                                             ElevationDeg, Facing) * (HdrScale * GroundObserver);

    const float HorizonDip = -std::acos(std::clamp(Criteria.Atmosphere.PlanetRadius
                                                 / (Criteria.Atmosphere.PlanetRadius + Observer.Height), 0.0f, 1.0f));

    Vector3 col = Sky + Glow;
    const float SkyLuminance = LuminanceOf(Sky);

    //    Horizon extinction, following the true (dipped) horizon.
    const float Extinction = SmoothStep(HorizonDip - 0.01f, HorizonDip + 0.12f * GroundObserver + 0.002f, Direction.y);

    //    Stars, gated on the sky's own luminance: skipped wholesale when even the brightest star cannot win.
    if (Criteria.Stars.Visible && Solved.StarCeiling > std::max(SkyLuminance, 1e-7f) * Criteria.Stars.ContrastLimit * 0.6f)
    {
        const float AirMass = AirMassOf(Degrees(std::asin(std::clamp(Direction.y, -1.0f, 1.0f))));
        Vector3 StarRadiance{ 0.0f, 0.0f, 0.0f };

        const float NeedsSupersample = Step(std::max(SkyLuminance, 1e-7f) * Criteria.Stars.ContrastLimit * 8.0f, Solved.StarCeiling);
        if (Criteria.Stars.Supersample > 1.5f && NeedsSupersample > 0.5f)
        {
            //    2×2 rotated-grid supersample so sub-pixel stars resolve without blur.
            Vector3 Sum{ 0.0f, 0.0f, 0.0f };
            const float Offsets[4][2] = { { -0.375f, -0.125f }, { 0.125f, -0.375f }, { 0.375f, 0.125f }, { -0.125f, 0.375f } };
            for (int q = 0; q < 4; ++q)
            {
                const Vector3 d2 = (Direction + (Observer.Right * Offsets[q][0] + Observer.Upward * Offsets[q][1]) * PixelAngle).Normalized();
                Sum += StarField(d2, PixelAngle * 0.7f, AirMass);
            }
            StarRadiance = Sum * 0.25f * Transmittance;
        }
        else
        {
            StarRadiance = StarField(Direction, PixelAngle, AirMass) * Transmittance;
        }

        //    Contrast-limited visibility: a star is seen only if it beats the sky around it.
        const float SkyFloor = std::max(SkyLuminance, 1e-7f);
        const float Threshold = SkyFloor * Criteria.Stars.ContrastLimit;
        const float l = LuminanceOf(StarRadiance);
        const float Visibility = SmoothStep(Threshold * 0.6f, Threshold * 2.5f, l + 1e-9f);
        col += StarRadiance * (Visibility * Extinction);
    }

    col += MoonDiscs(Direction, Transmittance);

    //    The thin analytic slab, then the marched layer, then the local volumes — the reference's order.
    {
        float Coverage = 0.0f;
        const Vector3 Slab = CloudSlab(Direction, Sky, Transmittance, Observer, Coverage);
        col = col * (1.0f - Coverage) + Slab;
    }
    {
        Vector3 Scatter{};
        float Coverage = 0.0f;
        CloudMarch(Observer.Position, Direction, Solved.SkyAmbient, Transmittance, 1e6f, PixelX, PixelY, Scatter, Coverage);
        col = col * (1.0f - Coverage) + Scatter;
    }
    {
        Vector3 Scatter{};
        float T = 1.0f;
        MarchLocalVolumes(Observer.Position, Direction, 1e6f, Solved.SkyAmbient, HorizonGlow, Transmittance, PixelX, PixelY, Scatter, T);
        col = col * T + Scatter;
    }

    //    The solar disc: limb darkening, the air-mass reddening, and the capped aureole.
    if (Criteria.Sun.Visible)
    {
        const float SunAngularRadius = PanelRadians(Criteria.Sun.AngularDiameterDeg) / 2.0f;  // uSunAng
        const float Angle = std::acos(std::clamp(Dot(Direction, Solved.SunDirection), -1.0f, 1.0f));
        const float Soft = Mix(1.0f, 2.2f, 1.0f - SmoothStep(0.0f, 4.0f, ElevationDeg));
        const float Disc = 1.0f - SmoothStep(SunAngularRadius * (1.0f - Criteria.Sun.Softness * 0.9f * Soft), SunAngularRadius, Angle);
        const float Limb = Mix(1.0f, 0.55f, SmoothStep(0.0f, SunAngularRadius, Angle));

        const Vector3 ExtinctionSpectral = ExponentOf(Negate(
              (Vector3{ 5.8e-6f, 13.5e-6f, 33.1e-6f } * (Criteria.Atmosphere.RayleighStrength * Criteria.Atmosphere.RayleighScaleHeight)
             + Splat(21e-6f) * (1.1f * Criteria.Atmosphere.MieStrength * Criteria.Atmosphere.MieScaleHeight))
            * AirMassOf(ElevationDeg)));
        const Vector3 SunColour = Solved.SunColour * MaximumOf(ExtinctionSpectral, Transmittance * Transmittance);

        col += SunColour * (Disc * Limb * Criteria.Sun.Intensity * Criteria.Sun.DiscRadiance
                          * Mix(0.35f, 1.0f, SmoothStep(-1.0f, 8.0f, ElevationDeg)));

        const float Aureole = std::exp(-Angle * 40.0f) * 0.35f + std::exp(-Angle * 9.0f) * 0.03f + std::exp(-Angle * 2.5f) * 0.004f;
        col += SunColour * (Aureole * Criteria.Post.Bloom * Criteria.Sun.Intensity * 0.6f);
    }

    //    One shared path length through both analytic media for sky rays.
    {
        const float hRef = std::max(Criteria.HeightFog.FalloffHeight * 4.0f, Criteria.AtmosphericFog.FalloffHeight * 5.0f);
        const float dS = (Direction.y > 0.002f) ? std::min(60000.0f, hRef / Direction.y) : 60000.0f;
        col = ApplyMedia(col, Direction, dS, Solved.SkyAmbient, HorizonGlow, Transmittance, Observer.Height);
    }

    return col;
}

Vector3 CelestialIntegrator::ApplyAerialPerspective(const Vector3& SurfaceRadiance, const Vector3& Direction,
                                                    float Distance, const ObserverFrame& Observer,
                                                    uint32_t PixelX, uint32_t PixelY) const noexcept
{
    const Vector3 Origin{ 0.0f, Criteria.Atmosphere.PlanetRadius + Observer.Height, 0.0f };
    Vector3 Sky{}, Transmittance{};
    float Ground = 0.0f;
    IntegrateAtmosphere(Origin, Direction, Sky, Transmittance, Ground);
    Sky *= Criteria.Sky.Tint;
    Sky *= Criteria.Sky.Brightness * (Criteria.Sky.Visible ? 1.0f : 0.0f) * (Criteria.Atmosphere.Visible ? 1.0f : 0.0f);

    const float hxz = std::sqrt(Direction.x * Direction.x + Direction.z * Direction.z) + 1e-5f;
    const float sxz = std::sqrt(Solved.SunDirection.x * Solved.SunDirection.x + Solved.SunDirection.z * Solved.SunDirection.z) + 1e-5f;
    const float Facing = 0.5f + 0.5f * ((Direction.x / hxz) * (Solved.SunDirection.x / sxz)
                                      + (Direction.z / hxz) * (Solved.SunDirection.z / sxz));
    const float HdrScale = Criteria.Sun.Intensity / 22.0f * Criteria.Sky.Brightness * (Criteria.Sky.Visible ? 1.0f : 0.0f);
    const float GroundObserver = 1.0f - SmoothStep(1500.0f, 12000.0f, Observer.Height);
    const Vector3 HorizonGlow = TwilightGlow(Vector3{ Direction.x, 0.0f, Direction.z }.Normalized() + Splat(1e-5f),
                                             Solved.SunElevationDeg, Facing) * (HdrScale * GroundObserver);

    Vector3 col = ApplyMedia(SurfaceRadiance, Direction, Distance, Solved.SkyAmbient, HorizonGlow, Transmittance, Observer.Height);

    Vector3 Scatter{};
    float T = 1.0f;
    MarchLocalVolumes(Observer.Position, Direction, Distance, Solved.SkyAmbient, HorizonGlow, Transmittance, PixelX, PixelY, Scatter, T);
    return col * T + Scatter;
}

Vector3 CelestialIntegrator::AddRainbow(const Vector3& Radiance, const Vector3& Direction, float SkyMask) const noexcept
{
    //    The bow lives in the rain column against the sky; the ground occludes it.
    const PrecipitationCriteria& P = Criteria.Precipitation;
    const bool RainOn = P.Visible && Criteria.VolumetricCloud.Visible && P.RateMillimetres > 0.0f
                      && (P.Variety == 0u || P.Variety == 1u || P.Variety == 4u);
    const float AltitudeFade = 1.0f - std::min(1.0f, std::max(0.0f,
                                    (Criteria.Observer.Height - Criteria.VolumetricCloud.BaseAltitude)
                                    / std::max(200.0f, Criteria.VolumetricCloud.Thickness)));
    const float RainVisibility = (Criteria.Rainbow.AlwaysOn ? 1.0f
                                : (RainOn ? std::min(1.0f, 0.6f + P.RateMillimetres / 25.0f) : 0.0f)) * AltitudeFade;

    const float HorizonLuminance = LuminanceOf(Solved.SkyAmbient);
    const float Mask = SkyMask * SmoothStep(-0.005f, 0.02f, Direction.y);

    Vector3 Sky{}, Transmittance{};
    float Ground = 0.0f;
    const Vector3 Origin{ 0.0f, Criteria.Atmosphere.PlanetRadius + Criteria.Observer.Height, 0.0f };
    IntegrateAtmosphere(Origin, Direction, Sky, Transmittance, Ground);

    return Radiance + Rainbow(Direction, RainVisibility, HorizonLuminance) * Solved.SunColour * Transmittance * Mask;
}

Vector3 CelestialIntegrator::AddLensFlare(const Vector3& Radiance, float ScreenU, float ScreenV,
                                          const ObserverFrame& Observer) const noexcept
{
    if (!Criteria.Post.Visible || !Criteria.Post.FlareOn || !Criteria.Sun.Visible)
    {
        return Radiance;
    }

    //    The sun's screen position, as the reference computes it on the CPU.
    const float cx = Dot(Solved.SunDirection, Observer.Right);
    const float cy = Dot(Solved.SunDirection, Observer.Upward);
    const float cz = Dot(Solved.SunDirection, Observer.Forward);
    const float InFront = cz > 0.02f ? 1.0f : 0.0f;
    const float SunU = InFront > 0.5f ? cx / cz / Observer.TangentHalf : 99.0f;
    const float SunV = InFront > 0.5f ? cy / cz / Observer.TangentHalf : 99.0f;

    const float ElevationDeg = Solved.SunElevationDeg;
    const float SunUp = SmoothStep(-6.0f, 4.0f, ElevationDeg);
    const float InFrame = 1.0f - SmoothStep(1.25f, 2.2f, std::sqrt(SunU * SunU + SunV * SunV));
    const float Visibility = InFront * SunUp * InFrame * Step(0.0f, ElevationDeg + 0.5f);

    return Radiance + LensFlare(ScreenU, ScreenV, SunU, SunV, Visibility)
                    * (Criteria.Sun.Intensity * 0.09f) * Solved.SunColour;
}

Vector3 CelestialIntegrator::ResolveDisplay(const Vector3& Radiance, float ScreenU, float ScreenV,
                                            uint32_t PixelX, uint32_t PixelY) const noexcept
{
    const PostCriteria& P = Criteria.Post;

    //    The reference's automatic exposure ramp, then the manual EV.
    const float ElevationDeg = Solved.SunElevationDeg;
    const float AutoEv = -0.35f * SmoothStep(-8.0f, -1.0f, ElevationDeg)
                       - 1.00f * SmoothStep(-1.0f, 6.0f, ElevationDeg)
                       - 0.60f * SmoothStep(6.0f, 30.0f, ElevationDeg);
    Vector3 col = Radiance * std::exp2((P.Visible ? P.ExposureStops : 0.0f) + AutoEv);

    const uint32_t Tonemap = P.Visible ? P.Tonemap : 1u;
    if (Tonemap == 1u)      { col = ToneAces(col); }
    else if (Tonemap == 2u) { col = Vector3{ col.x / (1.0f + col.x), col.y / (1.0f + col.y), col.z / (1.0f + col.z) }; }
    else if (Tonemap == 3u) { col = ToneFilmic(col); }
    else if (Tonemap == 4u) { col = ToneAgx(col); }
    else                    { col = ClampEach(col, 0.0f, 1.0f); }

    const float Vignette = P.Visible ? P.Vignette : 0.0f;
    const float r = std::sqrt(ScreenU * ScreenU + ScreenV * ScreenV) * 0.9f;
    col *= std::clamp(1.0f - Vignette * std::pow(r, 2.2f), 0.0f, 1.0f);

    col = PowerOf(MaximumOf(col, 0.0f), 1.0f / 2.2f);

    const float Grain = P.Visible ? P.Grain : 0.0f;
    const float Noise = (Hash13(Vector3{ static_cast<float>(PixelX) + 0.5f, static_cast<float>(PixelY) + 0.5f,
                                         Fract(Solved.TimeSeconds) * 100.0f }) - 0.5f) * Grain * 0.12f;
    return col + Splat(Noise);
}

//------------------------------------------------------------------------------------------------------------------------
//                                          THE SKY AS A LIGHT, FOR ReSTIR
//------------------------------------------------------------------------------------------------------------------------

Vector3 CelestialIntegrator::SampleSunIrradiance() const noexcept
{
    //    The sun's spectral irradiance after the atmosphere, matching the term the reference's surfaces use:
    //    `trans * uSunColor * uSunIntensity * .11 * ndl` for the plane. Here without the cosine, which the
    //    shading point supplies.
    if (!Criteria.Sun.Visible || Solved.SunDirection.y <= 0.0f)
    {
        return Vector3{ 0.0f, 0.0f, 0.0f };
    }
    const Vector3 Origin{ 0.0f, Criteria.Atmosphere.PlanetRadius + Criteria.Observer.Height, 0.0f };
    Vector3 Sky{}, Transmittance{};
    float Ground = 0.0f;
    IntegrateAtmosphere(Origin, Solved.SunDirection, Sky, Transmittance, Ground);
    return Transmittance * Solved.SunColour * (Criteria.Sun.Intensity * 0.11f);
}

Vector3 CelestialIntegrator::SampleEnvironmentRadiance(const Vector3& WorldDirection, const ObserverFrame& Observer) const noexcept
{
    //    The environment term a bounce ray sees. Cheap by construction: the atmosphere plus the twilight band,
    //    without the star/moon/cloud passes, which contribute negligibly to a diffuse bounce but cost the most.
    const Vector3 Direction = SkyFrameOf(WorldDirection);
    const Vector3 Origin{ 0.0f, Criteria.Atmosphere.PlanetRadius + Observer.Height, 0.0f };

    Vector3 Sky{}, Transmittance{};
    float Ground = 0.0f;
    IntegrateAtmosphere(Origin, Direction, Sky, Transmittance, Ground);
    Sky *= Criteria.Sky.Tint;
    Sky *= Criteria.Sky.Brightness * (Criteria.Sky.Visible ? 1.0f : 0.0f) * (Criteria.Atmosphere.Visible ? 1.0f : 0.0f);

    //    ⚠️ The checker plane is a real surface, and for a bounce ray leaving a room that stands ON it, it is
    //    the dominant one: every downward direction in the hemisphere lands there. Returning sky radiance for
    //    those directions would light the room from below with the wrong colour and the wrong intensity, and
    //    omitting it would drop the ground bounce entirely. Shaded here with the plane's own albedo, sun term
    //    and sky ambient — the same quantities `SampleGround` uses, without its filtered checker or grid,
    //    which are texture detail a diffuse bounce cannot resolve anyway.
    const GroundPlaneCriteria& G = Criteria.GroundPlane;
    if (G.Visible && Direction.y < -1e-4f && Observer.Height < 2000.0f)
    {
        const float t = (G.Height - Observer.Position.y) / Direction.y;
        if (t > 0.0f)
        {
            const Vector3 hp = Observer.Position + Direction * t;
            //    The mean of the two tiles: a diffuse bounce integrates over many cells at once.
            const Vector3 Albedo = (std::max(std::abs(hp.x), std::abs(hp.z)) <= G.HalfSize)
                                 ? (G.TintA + G.TintB) * 0.5f
                                 : G.TintA;
            const float ndl = std::max(Solved.SunDirection.y, 0.0f);
            const Vector3 SunLight = Transmittance * Solved.SunColour * (Criteria.Sun.Intensity * 0.11f * ndl);
            const Vector3 Ambient = Sky * 0.42f + Vector3{ 0.0015f, 0.002f, 0.004f } * Criteria.Sky.Brightness;
            Vector3 Radiance = Albedo * (SunLight + Ambient);
            Radiance += LocalLightsOnSurface(hp, Albedo);
            float Fog = 1.0f - std::exp(-t * 7e-5f);
            Fog = std::pow(Clamp01(Fog), 1.6f);
            return Mix(Radiance, Sky * 0.8f, Fog);
        }
    }

    if (Ground > 0.5f)
    {
        //    Below the horizon the bounce sees lit ground, not sky.
        const float ndl = std::max(Solved.SunDirection.y, 0.0f);
        const Vector3 SunLight = Transmittance * Solved.SunColour * (Criteria.Sun.Intensity * 0.09f * ndl);
        return Criteria.Sky.GroundAlbedo * (SunLight + Sky * 0.35f) * Criteria.Sky.GroundBrightness;
    }

    const float hxz = std::sqrt(Direction.x * Direction.x + Direction.z * Direction.z) + 1e-5f;
    const float sxz = std::sqrt(Solved.SunDirection.x * Solved.SunDirection.x + Solved.SunDirection.z * Solved.SunDirection.z) + 1e-5f;
    const float Facing = 0.5f + 0.5f * ((Direction.x / hxz) * (Solved.SunDirection.x / sxz)
                                      + (Direction.z / hxz) * (Solved.SunDirection.z / sxz));
    const float HdrScale = Criteria.Sun.Intensity / 22.0f * Criteria.Sky.Brightness * (Criteria.Sky.Visible ? 1.0f : 0.0f);
    Vector3 Radiance = Sky + TwilightGlow(Direction, Solved.SunElevationDeg, Facing) * HdrScale;

    //    ⚠️ The moons belong here, not only in the beauty pass. Without them this function returns ~0 for the
    //    whole night hemisphere, so a moonlit room receives no indirect light at all and every night frame is
    //    lit solely by whatever lamp happens to be in the scene. The moon is a genuine illuminant — full
    //    moonlight is about 0.25 lux at the ground — and a bounce ray that escapes toward it has to see it.
    //
    //    The point stars are deliberately NOT included: their integrated contribution to a diffuse bounce is
    //    negligible, while the 3x3 octahedral neighbour search is the most expensive thing in the integrator.
    //    The Milky Way band is folded in through `MoonDiscs`' own ambient floor instead.
    if (Criteria.MoonCount > 0u)
    {
        Radiance += MoonDiscs(Direction, Transmittance);
    }
    return Radiance;
}

//------------------------------------------------------------------------------------------------------------------------
//                          TEST ACCESS — thunks onto the shared kernels, for the transliteration proof
//------------------------------------------------------------------------------------------------------------------------
//    Each forwards to the one true definition above. Nothing is restated here, so the proof cannot pass by
//    agreeing with a copy that has itself drifted from the shipping path.

Vector3 CelestialIntegrator::KernelKelvinStar(float t) noexcept                 { return KelvinStar(t); }
float   CelestialIntegrator::KernelHash13(const Vector3& p) noexcept            { return Hash13(p); }
Vector3 CelestialIntegrator::KernelHash33(const Vector3& p) noexcept            { return Hash33(p); }
float   CelestialIntegrator::KernelValueNoise(const Vector3& p) noexcept        { return ValueNoise(p); }
Vector3 CelestialIntegrator::KernelHue(float h) noexcept                        { return HueOf(h); }
Vector3 CelestialIntegrator::KernelRotateY(const Vector3& v, float a) noexcept  { return RotateY(v, a); }
Vector3 CelestialIntegrator::KernelRotateX(const Vector3& v, float a) noexcept  { return RotateX(v, a); }
Vector3 CelestialIntegrator::KernelOctahedralDecode(float u, float v) noexcept  { return OctahedralDecode(u, v); }
float   CelestialIntegrator::KernelHenyeyGreenstein(float c, float g) noexcept  { return HenyeyGreenstein(c, g); }
float   CelestialIntegrator::KernelCloudNoise2(float px, float py) noexcept     { return CloudNoise2(px, py); }

float CelestialIntegrator::KernelHeightIntegral(float FalloffHeight, float y0, float y1) noexcept
{
    return HeightIntegralKernel(FalloffHeight, y0, y1);
}

float CelestialIntegrator::KernelCloudHeightProfile(float hn, float Variety, float Anvil) noexcept
{
    return CloudHeightProfileKernel(hn, Variety, Anvil);
}

float CelestialIntegrator::TerrainFieldValue(float qx, float qz) const noexcept
{
    return TerrainField(qx, qz);
}

Vector3 CelestialIntegrator::StarFieldValue(const Vector3& Direction, float PixelAngle, float AirMass) const noexcept
{
    return StarField(Direction, PixelAngle, AirMass);
}

} // namespace Frontier::ProjectZero
