//============================================================================================================================================
// 📦 Project-Zero/Source/SkyFogIntegrator.cpp — Celestial Sky And Fog Volume Integrator Implementation Over Slang
//============================================================================================================================================

#include "SkyFogIntegrator.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "../../../Engine/DisplayPresentation/VolumetricMedia.h"
#include "../../../Engine/DisplayPresentation/CloudShadowStaging.h"
#include "../Shaders/SlangInterchange.h"
#include "../Shaders/SkySpecification.slang"
#include "../Shaders/FogSpecification.slang"
// The prelude above defines Slang keyword macros (`in`, `uniform`); undefine
// them here so nothing below this line can be macro-rewritten.
#undef in
#undef uniform

namespace Frontier::ProjectZero {

namespace {

//------------------------------------------------------------------------------------------------------------------------
//                                                  RENDER CORE STATE
//------------------------------------------------------------------------------------------------------------------------

// The shipped .slang core is compiled here as C++ (single translation unit,
// so its file-scope routines keep one definition). The courtyard owns the
// single atmosphere; the header never names .slang types.
SkyConfiguration    CoreSky{};                                  // [-] panel-default sky parameters
FogConfiguration    CoreFog{};                                  // [-] active fog scenario configuration
SkyFogIntegrator::FogScenario CoreScenario = SkyFogIntegrator::FogScenario::Morning;

float3              CoreSunRadiance{};                          // [lux] trans * color * intensity, shared

float3 MoonRadianceValue() noexcept
{
    return float3(0.19f, 0.24f, 0.32f);                         // [lux] cool showcase moonlight, dimmed x0.45
}

void FogLightPick(float3& LightDir, float3& LightRadiance) noexcept
{
    float sunLum = CoreSunRadiance.x * 0.212671f + CoreSunRadiance.y * 0.715160f + CoreSunRadiance.z * 0.072169f;
    float3 moon = MoonRadianceValue();
    float moonLum = moon.x * 0.212671f + moon.y * 0.715160f + moon.z * 0.072169f;
    bool sunWins = sunLum >= moonLum;
    LightDir = sunWins ? CoreSky.sunDir : CoreSky.moonDir;
    LightRadiance = sunWins ? CoreSunRadiance : moon;
}

float3 ToFloat3(const Vector3& Source) noexcept
{
    return float3(Source.x, Source.y, Source.z);
}

Vector3 ToVector3(const float3& Source) noexcept
{
    return Vector3{ Source.x, Source.y, Source.z };
}

//------------------------------------------------------------------------------------------------------------------------
//                                               SOLAR EPHEMERIS COPY
//------------------------------------------------------------------------------------------------------------------------

// Verbatim host-side copy of the harness solar solver (Frontier repo,
// Projects/Project-Zero/Host/SunPosition.h): the panel's own sunDirAt plus
// the Tanner Helland kelvin fit, in double precision. Solved in the Y-up
// render frame; the world swizzle below rotates it into Z-up.

struct SolarState
{
    double DirX, DirY, DirZ;                                    // [-] unit sun vector, Y-up render frame
    double ElevationDeg;                                        // [deg] sun elevation above horizon
    double ColorR, ColorG, ColorB;                              // [0..1] kelvin-fitted sun tint
};

SolarState SolveSolarState(double LocalHours, double LatitudeDeg, double AzimuthOffsetDeg, double ColourTemperatureK) noexcept
{
    constexpr double Pi = 3.141592653589793;
    constexpr double Deg2Rad = Pi / 180.0;
    const double Lat = LatitudeDeg * Deg2Rad;
    const double HourAngle = (LocalHours - 12.0) / 24.0 * Pi * 2.0;
    const double SinElev = std::cos(Lat) * std::cos(HourAngle);
    const double Elev = std::asin(std::clamp(SinElev, -1.0, 1.0));
    const double Azim = std::atan2(std::sin(HourAngle), std::cos(HourAngle) * std::sin(Lat)) + Pi + AzimuthOffsetDeg * Deg2Rad;
    SolarState Sun{};
    Sun.DirX = std::sin(Azim) * std::cos(Elev);
    Sun.DirY = std::sin(Elev);
    Sun.DirZ = -std::cos(Azim) * std::cos(Elev);
    Sun.ElevationDeg = Elev / Deg2Rad;

    const double Kelvin = ColourTemperatureK / 100.0;
    double Red = (Kelvin <= 66.0) ? 255.0 : 329.7 * std::pow(Kelvin - 60.0, -0.133);
    double Green = (Kelvin <= 66.0) ? 99.47 * std::log(Kelvin) - 161.1 : 288.1 * std::pow(Kelvin - 60.0, -0.0755);
    double Blue = (Kelvin >= 66.0) ? 255.0 : ((Kelvin <= 19.0) ? 0.0 : 138.5 * std::log(Kelvin - 10.0) - 305.0);
    Sun.ColorR = std::clamp(Red, 0.0, 255.0) / 255.0;
    Sun.ColorG = std::clamp(Green, 0.0, 255.0) / 255.0;
    Sun.ColorB = std::clamp(Blue, 0.0, 255.0) / 255.0;
    return Sun;
}

//------------------------------------------------------------------------------------------------------------------------
//                                               PANEL DEFAULT COPY
//------------------------------------------------------------------------------------------------------------------------

// Verbatim host-side copy of the panel defaults routine (Frontier repo,
// Projects/Project-Zero/Host/CelHost.h): every SkyConfiguration field the
// reference defaults() assigns, so the courtyard sky cannot drift from the
// gated mirror.

SkyConfiguration MakeSkyDefaults(const SolarState& Sun) noexcept
{
    SkyConfiguration p;
    p.sunDir = float3(float(Sun.DirX), float(Sun.DirY), float(Sun.DirZ));
    p.sunColor = float3(float(Sun.ColorR), float(Sun.ColorG), float(Sun.ColorB));
    p.sunElevationDeg = float(Sun.ElevationDeg);
    p.sunIntensity = 22.0f;
    p.sunAngularRadius = 0.53f * 3.14159265f / 180.0f / 2.0f;
    p.sunSoftness = 0.25f;
    p.sunDiscBoost = 12.0f;
    p.rayleigh = 1.0f;
    p.mie = 1.0f;
    p.mieG = 0.78f;
    p.ozone = 1.2f;
    p.planetRadius = 6371000.0f;
    p.atmoHeight = 100000.0f;
    p.rayleighH = 8000.0f;
    p.mieH = 1200.0f;
    p.skyTint = float3(1.0f, 1.0f, 1.0f);
    p.skyBright = 1.0f;
    p.groundAlbedo = float3(0.16862746f, 0.16078432f, 0.14117648f);
    p.groundBright = 1.0f;
    p.dawnIntensity = 1.0f;
    p.lineIntensity = 1.0f;
    p.lineAuto = 1u;
    p.exposureStops = 0.4f;
    p.tonemap = 1u;
    p.bloom = 1.0f;
    p.vignette = 0.28f;
    p.grain = 0.0f;
    p.camHeight = 2.0f;
    p.timeSeconds = 0.5f;
    p.fogOn = 0u;
    p.fogDensity = 0.011f;
    p.fogHeight = 42.0f;
    p.fogScatter = 0.7f;
    p.fogColor = float3(0.5607843f, 0.6431373f, 0.73333335f);
    p.afOn = 1u;
    p.afDensity = 7.0f * 1e-5f;
    p.afHeight = 1200.0f;
    p.afStart = 0.0f;
    p.afMie = 0.35f;
    p.afG = 0.7f;
    p.afSky = 1.0f;
    p.afTint = float3(1.0f, 1.0f, 1.0f);
    p.skyAmb = SkyAmbientCompute(p);
    p.starsOn = 1u;
    p.starDensity = 1.0f;
    p.starBright = 1.0f;
    p.starSize = 1.0f;
    p.starGlow = 0.8f;
    p.starTwinkle = 0.5f;
    p.starColor = 1.0f;
    p.starLimit = 1.5f;
    p.starMilky = 1.0f;
    p.starRot = 40.0f * 3.14159265f / 180.0f;
    p.starTilt = -62.0f * 3.14159265f / 180.0f;
    p.starLayers = 3.0f;
    p.starAA = 1.0f;
    p.starCeil = 8.0f * 2.6e-3f * (p.starBright != 0.0f ? p.starBright : 1.0f) * 3.2f * 1.6f;
    p.pixAngle = 0.0f;
    // Showcase sky: full moon, broken cirrus, boosted stars (artistic
    // license for the group photo; the mirror gates keep these off).
    p.moonOn = 1u;
    p.moonDir = float3(0.2506f, 0.3509f, -0.9023f);
    p.moonBright = 1.2f;   // Dimmed from 3.0: full-moon disc should read, not glare.
    p.cloudOn = 1u;
    p.cloudCoverage = 0.28f;
    p.cloudScale = 2.0f;
    p.starBright = 8.0f;
    p.starCeil = 8.0f * 2.6e-3f * p.starBright * 3.2f * 1.6f;
    return p;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 FOG SCENARIO COPY
//------------------------------------------------------------------------------------------------------------------------

// Verbatim copy of the harness fog scenarios (Frontier repo,
// Projects/Project-Zero/Host/FogViewport.cpp): the same global veil plus
// the morning ground-mist box and the backlit local sphere, all placed in
// the Y-up render frame.

FogConfiguration MakeFogScenario(SkyFogIntegrator::FogScenario Scenario, const float3& Ambient) noexcept
{
    FogConfiguration c = FogConfigurationMake(0.0006f, 0.15f, 0.0f, float3(0.75f, 0.78f, 0.82f), 0.55f, Ambient, 0.0f, 12u, 400.0f);
    if (Scenario == SkyFogIntegrator::FogScenario::Morning)
    {
        c.globalDensity = 0.004f;
        c.globalHeightFalloff = 0.25f;
        c.volumeCount = 1u;
        c.volumes[0] = FogVolumeMake(FogShapeBox, float3(0.0f, 1.2f, 10.0f), float3(45.0f, 1.5f, 35.0f), 0.010f, 0.35f,
            float3(0.80f, 0.82f, 0.85f), 0.55f, float3(0.0f, 0.0f, 0.0f), 0.25f, 0.15f);
    }
    else if (Scenario == SkyFogIntegrator::FogScenario::Backlit)
    {
        c.globalDensity = 0.0015f;
        c.volumeCount = 1u;
        c.volumes[0] = FogVolumeMake(FogShapeSphere, float3(10.0f, 3.0f, 5.1f), float3(5.0f, 5.0f, 5.0f), 0.030f, 0.10f,
            float3(0.85f, 0.82f, 0.78f), 0.70f, float3(0.0f, 0.0f, 0.0f), 0.5f, 0.20f);
    }
    return c;
}

//------------------------------------------------------------------------------------------------------------------------
//                                          REAL CLOUD SHADOW STATE
//------------------------------------------------------------------------------------------------------------------------

// The showcase marches the transplanted volumetric field itself (SunTransmittanceAt), never a texture. Staging
//    mirrors CelestialSequence::Prepare() exactly — the calibrated broken-cumulus values, swept three times
//    against the march (Coverage 0.34, not the struct default 0.55: the fbm piles mid-range, so 0.55 whites the
//    sky). Drift folds once per frame (SampleStep at mid-slab x time x 0.8, CloudDensity's own factors) exactly
//    the way the GPU host folds it for the kernel — same fold, same field, same taps.
CloudLayerSettings      CloudShadowLayer{};                     // [-] staged deck (Enabled = master switch)
WindSettings            CloudShadowWind{};                      // [-] staged breeze (7 m/s, bearing 250)
float                   CloudShadowTimeSec = 0.0f;              // [s] showcase instant (GPU: LocalHours x 3600)
float                   CloudShadowDriftX = 0.0f;               // [m] folded advection, X
float                   CloudShadowDriftY = 0.0f;               // [m] folded advection, Y

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

SkyFogIntegrator::SkyFogIntegrator() noexcept
{
    // Safe-by-default showcase light: sunset over evening ground mist.
    AssignSunHour(17.93);
    AssignFogScenario(FogScenario::Morning);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 ASSIGNMENT QUERIES
//------------------------------------------------------------------------------------------------------------------------

void SkyFogIntegrator::AssignSunHour(double LocalHours) noexcept
{
    // Johannesburg latitude with the showcase azimuth staging (-60 deg puts
    // the extinct sunset behind the default camera so the field catches a
    // warm key while the northern sky goes twilight behind the full moon;
    // the solar algorithm itself is the untouched gated copy).
    const SolarState Sun = SolveSolarState(LocalHours, -26.0, -60.0, 5800.0);
    CoreSky = MakeSkyDefaults(Sun);
    const float3 SkyOrigin = float3(0.0f, CoreSky.planetRadius + 2.0f, 0.0f);
    const AtmosphereStructure SunPath = AtmosphereCompute(SkyOrigin, CoreSky.sunDir, CoreSky);
    CoreSunRadiance = SunPath.trans * CoreSky.sunColor * CoreSky.sunIntensity;
    if (CoreSky.sunDir.y <= 0.0f)
    {
        // Below-horizon sun carries no direct light: without this clamp
        // the unextinguished disc value (intensity ~22) would flood night
        // scenes with daylight. The moon wins the fog-light pick instead.
        CoreSunRadiance = float3(0.0f, 0.0f, 0.0f);
    }
    CoreFog = MakeFogScenario(CoreScenario, SkyAmbientCompute(CoreSky));
}

void SkyFogIntegrator::AssignFogScenario(FogScenario Scenario) noexcept
{
    CoreScenario = Scenario;
    CoreFog = MakeFogScenario(CoreScenario, SkyAmbientCompute(CoreSky));
}

void SkyFogIntegrator::AssignCloudShadow(bool Enabled, float CloudTimeSeconds, uint32_t CloudType, float Coverage, float Scale, float Base, float Thickness, float Density) noexcept
{
    // Prepare()'s staging, restated (Engine/DisplayPresentation defaults would white the sky — see above).
    CloudShadowLayer = CloudLayerSettings{};
    CloudShadowLayer.Enabled = Enabled;
    CloudShadowLayer.Type = static_cast<CloudTypeCategory>(CloudType > 5u ? 2u : CloudType);
    CloudShadowLayer.Base = Base;
    CloudShadowLayer.Thickness = Thickness;
    CloudShadowLayer.Coverage = Coverage;
    CloudShadowLayer.Density = Density;
    CloudShadowLayer.Scale = Scale;
    CloudShadowLayer.Anvil = kCloudShadowShowcaseDiorama.Anvil;
    CloudShadowLayer.CeilingMetres = kCloudShadowShowcaseDiorama.CeilingMetres;
    CloudShadowWind = WindSettings{};
    CloudShadowWind.Speed = kCloudShadowShowcaseDiorama.WindSpeed;
    CloudShadowWind.Bearing = kCloudShadowShowcaseDiorama.WindBearing;
    CloudShadowTimeSec = CloudTimeSeconds;
    // Fold the drift exactly as the GPU pack does: mid-slab wind, CloudDensity's x0.8, same parentheses
    //    (Drift *= Time * 0.8 associates differently — the split proof caught it).
    float SlabBase = 0.0f, SlabTop = 0.0f;
    CloudShadowDriftX = 0.0f;
    CloudShadowDriftY = 0.0f;
    if (Enabled && VolumetricMedia::SlabExtent(CloudShadowLayer, SlabBase, SlabTop))
    {
        float Flow[3] = { 0.0f, 0.0f, 0.0f };
        WindField::SampleStep(CloudShadowWind, (SlabBase + SlabTop) * 0.5f, Flow);
        CloudShadowDriftX = Flow[0] * (CloudShadowTimeSec * 0.8f);
        CloudShadowDriftY = Flow[1] * (CloudShadowTimeSec * 0.8f);
    }
    std::printf("[Project-Zero] CloudShadow: %s, type %u, coverage %.2f, t = %.1f s, drift = (%.1f, %.1f).\n",
                Enabled ? "ON (marched)" : "OFF", CloudType > 5u ? 2u : CloudType, Coverage, CloudTimeSeconds,
                CloudShadowDriftX, CloudShadowDriftY);
}

float SkyFogIntegrator::QueryCloudShadow(const Vector3& WorldPos, const Vector3& SunWorldDir) const noexcept
{
    if (!CloudShadowLayer.Enabled)
    {
        return 1.0f;
    }
    const float P[3] = { WorldPos.x, WorldPos.y, WorldPos.z };
    const float S[3] = { SunWorldDir.x, SunWorldDir.y, SunWorldDir.z };
    return VolumetricMedia::SunTransmittanceAt(CloudShadowLayer, CloudShadowDriftX, CloudShadowDriftY, P, S);
}

Vector3 SkyFogIntegrator::QuerySunDirectionRender() const noexcept
{
    return ToVector3(CoreSky.sunDir);
}

Vector3 SkyFogIntegrator::QuerySunDirectionWorld() const noexcept
{
    return WorldFromRender(ToVector3(CoreSky.sunDir));
}

Vector3 SkyFogIntegrator::QuerySunRadiance() const noexcept
{
    return ToVector3(CoreSunRadiance);
}

Vector3 SkyFogIntegrator::QueryMoonDirectionRender() const noexcept
{
    return ToVector3(CoreSky.moonDir);
}

Vector3 SkyFogIntegrator::QueryMoonDirectionWorld() const noexcept
{
    return WorldFromRender(ToVector3(CoreSky.moonDir));
}

Vector3 SkyFogIntegrator::QueryMoonRadiance() const noexcept
{
    return ToVector3(MoonRadianceValue());
}

Vector3 SkyFogIntegrator::QuerySunFlareTint() const noexcept
{
    // Reference `AddLensFlare` output scale (unattenuated kelvin colour).
    return ToVector3(CoreSky.sunColor * (CoreSky.sunIntensity * 0.09f));
}

Vector3 SkyFogIntegrator::QueryFogLightDirection() const noexcept
{
    // The march scatters from one luminaire: the brighter of the setting
    // sun and the showcase moon, compared by luminance.
    float sunLum = CoreSunRadiance.x * 0.212671f + CoreSunRadiance.y * 0.715160f + CoreSunRadiance.z * 0.072169f;
    float3 moon = MoonRadianceValue();
    float moonLum = moon.x * 0.212671f + moon.y * 0.715160f + moon.z * 0.072169f;
    return (sunLum >= moonLum) ? ToVector3(CoreSky.sunDir) : ToVector3(CoreSky.moonDir);
}

Vector3 SkyFogIntegrator::QueryFogLightRadiance() const noexcept
{
    float sunLum = CoreSunRadiance.x * 0.212671f + CoreSunRadiance.y * 0.715160f + CoreSunRadiance.z * 0.072169f;
    float3 moon = MoonRadianceValue();
    float moonLum = moon.x * 0.212671f + moon.y * 0.715160f + moon.z * 0.072169f;
    return (sunLum >= moonLum) ? ToVector3(CoreSunRadiance) : ToVector3(moon);
}

Vector3 SkyFogIntegrator::QueryAmbientRadiance() const noexcept
{
    // The sky mean overestimates Lambert irradiance: the bright horizon
    // band counts fully in the mean but feeds surfaces at grazing angles,
    // so the surface fill is rescaled (the sky pixels themselves, and the
    // fog's own ambient source, keep the unscaled mirror value).
    const Vector3 Ambient = ToVector3(SkyAmbientCompute(CoreSky));
    const Vector3 Moon = ToVector3(MoonRadianceValue());
    return Vector3{ Ambient.x * 0.50f + Moon.x * 0.50f, Ambient.y * 0.50f + Moon.y * 0.50f, Ambient.z * 0.50f + Moon.z * 0.50f };
}

float SkyFogIntegrator::QueryFogFarDistance() const noexcept
{
    return CoreFog.maxMarchDistance;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              RADIANCE INTEGRATION
//------------------------------------------------------------------------------------------------------------------------

Vector3 SkyFogIntegrator::ComputeSkyRadiance(const Vector3& RenderDir) const noexcept
{
    return ToVector3(SkyRadianceCompute(ToFloat3(RenderDir), CoreSky));
}

Vector3 SkyFogIntegrator::ApplyPanelPost(const Vector3& LinearColor, uint32_t PixelX, uint32_t PixelY, uint32_t ImageWidth, uint32_t ImageHeight) const noexcept
{
    const float2 Uv = ViewportUVCompute(uint(PixelX), uint(PixelY), uint(ImageWidth), uint(ImageHeight));
    return ToVector3(SkyPostApply(ToFloat3(LinearColor), Uv.x, Uv.y, float(ImageWidth), float(ImageHeight), uint(PixelX), uint(PixelY), CoreSky));
}

Vector3 SkyFogIntegrator::MarchFog(const Vector3& RenderOrigin, const Vector3& RenderDir, float TMin, float TMax, float LightVisibility, float& Transmittance) const noexcept
{
    float3 LightDir{};
    float3 LightRadiance{};
    FogLightPick(LightDir, LightRadiance);
    const FogMarchStructure March = FogMarchVolumes(ToFloat3(RenderOrigin), ToFloat3(RenderDir), TMin, TMax, LightDir, LightRadiance, LightVisibility, CoreFog);
    Transmittance = March.transmittance;
    return ToVector3(March.scatter);
}

Vector3 SkyFogIntegrator::ApplyAerialPerspective(const Vector3& SurfaceColor, const Vector3& RenderOrigin, const Vector3& RenderDir, float Distance) const noexcept
{
    const float Facing = SunFacingCompute(ToFloat3(RenderDir), CoreSky.sunDir);
    const float3 HorizonGlow = DawnGlowCompute(ToFloat3(RenderDir), CoreSky.sunElevationDeg, Facing, CoreSky);
    return ToVector3(FogAtmosphereApply(ToFloat3(SurfaceColor), ToFloat3(RenderOrigin), ToFloat3(RenderDir), Distance, HorizonGlow, CoreSky));
}

//------------------------------------------------------------------------------------------------------------------------
//                                               FRAME CONVERSION
//------------------------------------------------------------------------------------------------------------------------

Vector3 SkyFogIntegrator::RenderFromWorld(const Vector3& World) noexcept
{
    // Proper rotation (determinant +1): X stays east, world up becomes
    // render up, world north becomes render negative-Z.
    return Vector3{ World.x, World.z, -World.y };
}

Vector3 SkyFogIntegrator::WorldFromRender(const Vector3& Render) noexcept
{
    return Vector3{ Render.x, -Render.z, Render.y };
}

} // namespace Frontier::ProjectZero
