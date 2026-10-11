//============================================================================================================================================
// 📦 Project-Zero/Source/CelestialSpecification.h — Celestial Scalar Algebra and Reference Parameter Declarations
//============================================================================================================================================
//
//    Every constant in this file is transcribed from the reference celestial console
//    (`https://sultanaladin.github.io/Frontier-/celestial/`, fragment program `#fs`, 497 lines) and from the
//    defaults its own `defaults()` routine produces. Nothing here is invented, rounded or "tuned to taste":
//    where the reference writes `.78`, this writes `0.78f`.
//
//    ⚠️ FRAME CONVENTION. The reference program is authored $+Y$ up (`dir.y` is altitude, azimuth 0 points at
//    $-Z$). Frontier is strictly $+Z$ up (CLAUDE.md §7). Rather than re-derive every trigonometric identity —
//    the surest way to introduce drift — the transcription keeps the reference frame verbatim and rotates only
//    at the seam, in `SkyFrameOf` / `WorldFrameOf` below. Inside the celestial translation units, "up" is $+Y$
//    exactly as the reference has it.
//
//============================================================================================================================================

#pragma once

#include "../../../DeviceExchange/OrientationClassifier.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Frontier::ProjectZero {

//------------------------------------------------------------------------------------------------------------------------
//                                            SCALAR AND SPECTRAL ALGEBRA
//------------------------------------------------------------------------------------------------------------------------
//    The shading language primitives the transcription leans on. Named so a reader can put the C++ and the
//    reference line side by side and see the same expression.

constexpr float kPi = 3.14159265f;                              // [-] the reference's own PI literal, to the digit
constexpr float kDegreesToRadians = kPi / 180.0f;               // [rad/deg] conversion

//    The reference splits its trigonometry across two machines with two different precisions, and the split is
//    load-bearing. Inside the fragment shader every angle is a float and PI is the truncated literal above.
//    On the panel side every angle is a JavaScript *double* built from the full `Math.PI`, and it is rounded to
//    float exactly once, when `gl.uniform*f` uploads it. Converting degrees in float instead reproduces the
//    wrong machine: for the sun direction it costs ~49 arcsec, about 5% of the solar radius, which visibly
//    walks the disc and the specular highlight. `PanelRadians` is that upload path — double throughout, one
//    rounding at the end — and must be used for any quantity the panel computes and uploads as a uniform.
constexpr double kPiPanel = 3.14159265358979323846;             // [-] JavaScript `Math.PI`
constexpr double kDegreesToRadiansPanel = kPiPanel / 180.0;     // [rad/deg] the panel's `D2R`

[[nodiscard]] inline float PanelRadians(float Degrees) noexcept
{
    return static_cast<float>(static_cast<double>(Degrees) * kDegreesToRadiansPanel);
}

[[nodiscard]] inline float Fract(float x) noexcept              { return x - std::floor(x); }
[[nodiscard]] inline float Clamp01(float x) noexcept            { return std::clamp(x, 0.0f, 1.0f); }
[[nodiscard]] inline float Mix(float a, float b, float t) noexcept { return a + (b - a) * t; }
//    GLSL `radians()`: float, truncated PI. Correct ONLY for conversions the fragment shader itself performs
//    (e.g. `airMassOf`). For anything the panel uploads as a uniform, use `PanelRadians` instead.
[[nodiscard]] inline float Radians(float Degrees) noexcept      { return Degrees * kDegreesToRadians; }
[[nodiscard]] inline float Degrees(float Rads) noexcept         { return Rads / kDegreesToRadians; }
[[nodiscard]] inline float Step(float Edge, float x) noexcept   { return x < Edge ? 0.0f : 1.0f; }

// GLSL smoothstep, including its edge-equal degeneracy guard.
[[nodiscard]] inline float SmoothStep(float Edge0, float Edge1, float x) noexcept
{
    const float Denominator = Edge1 - Edge0;
    if (std::abs(Denominator) < 1e-20f)
    {
        return x < Edge0 ? 0.0f : 1.0f;
    }
    const float t = Clamp01((x - Edge0) / Denominator);
    return t * t * (3.0f - 2.0f * t);
}

//    Component-wise spectral operations. `Vector3` already carries the arithmetic operators; these supply the
//    shading-language intrinsics the engine type does not declare.

[[nodiscard]] inline float Dot(const Vector3& a, const Vector3& b) noexcept
{
    return OrientationClassifier::DotProduct(a, b);
}

[[nodiscard]] inline Vector3 Cross(const Vector3& a, const Vector3& b) noexcept
{
    return OrientationClassifier::CrossProduct(a, b);
}

[[nodiscard]] inline Vector3 Negate(const Vector3& v) noexcept          { return Vector3{ -v.x, -v.y, -v.z }; }
[[nodiscard]] inline Vector3 Splat(float s) noexcept                    { return Vector3{ s, s, s }; }

[[nodiscard]] inline Vector3 Mix(const Vector3& a, const Vector3& b, float t) noexcept
{
    return Vector3{ Mix(a.x, b.x, t), Mix(a.y, b.y, t), Mix(a.z, b.z, t) };
}

[[nodiscard]] inline Vector3 Mix(const Vector3& a, const Vector3& b, const Vector3& t) noexcept
{
    return Vector3{ Mix(a.x, b.x, t.x), Mix(a.y, b.y, t.y), Mix(a.z, b.z, t.z) };
}

[[nodiscard]] inline Vector3 ExponentOf(const Vector3& v) noexcept
{
    return Vector3{ std::exp(v.x), std::exp(v.y), std::exp(v.z) };
}

[[nodiscard]] inline Vector3 PowerOf(const Vector3& v, float e) noexcept
{
    return Vector3{ std::pow(std::max(v.x, 0.0f), e), std::pow(std::max(v.y, 0.0f), e), std::pow(std::max(v.z, 0.0f), e) };
}

[[nodiscard]] inline Vector3 MaximumOf(const Vector3& a, const Vector3& b) noexcept
{
    return Vector3{ std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z) };
}

[[nodiscard]] inline Vector3 MaximumOf(const Vector3& a, float b) noexcept
{
    return Vector3{ std::max(a.x, b), std::max(a.y, b), std::max(a.z, b) };
}

[[nodiscard]] inline Vector3 ClampEach(const Vector3& v, float Low, float High) noexcept
{
    return Vector3{ std::clamp(v.x, Low, High), std::clamp(v.y, Low, High), std::clamp(v.z, Low, High) };
}

// Rec.709 relative luminance — the weights the reference uses for every sky-brightness decision.
[[nodiscard]] inline float LuminanceOf(const Vector3& c) noexcept
{
    return c.x * 0.2126f + c.y * 0.7152f + c.z * 0.0722f;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   FRAME ROTATION
//------------------------------------------------------------------------------------------------------------------------
//    Frontier world ($+X$ east, $+Y$ north, $+Z$ up) ↔ reference sky ($+X$ east, $+Y$ up, $-Z$ north).
//    A pure axis relabel: no scale, no handedness change, exactly invertible.

[[nodiscard]] inline Vector3 SkyFrameOf(const Vector3& World) noexcept
{
    return Vector3{ World.x, World.z, -World.y };
}

[[nodiscard]] inline Vector3 WorldFrameOf(const Vector3& Sky) noexcept
{
    return Vector3{ Sky.x, -Sky.z, Sky.y };
}

//------------------------------------------------------------------------------------------------------------------------
//                                              ENTITY PARAMETER RECORDS
//------------------------------------------------------------------------------------------------------------------------
//    One record per entity the reference panel exposes, carrying the panel's own default. The names follow the
//    panel's labels so the eventual inspector is a projection of this state rather than a translation of it.

// Atmosphere — panel entity `atmosphere`.
struct AtmosphereCriteria
{
    bool    Visible             = true;
    float   RayleighStrength    = 1.0f;                         // [-] `atm_rayleigh`
    float   MieStrength         = 1.0f;                         // [-] `atm_mie`
    float   MieAnisotropy       = 0.78f;                        // [-] `atm_mieG`
    float   OzoneStrength       = 1.2f;                         // [-] `atm_ozone`
    float   RayleighScaleHeight = 8000.0f;                      // [m]  `atm_hr`
    float   MieScaleHeight      = 1200.0f;                      // [m]  `atm_hm`
    float   PlanetRadius        = 6371.0f * 1000.0f;            // [m]  `atm_planetR` (panel is km)
    float   AtmosphereHeight    = 100.0f * 1000.0f;             // [m]  `atm_height`  (panel is km)
    float   GlowIntensity       = 1.0f;                         // [-] `dawn_int`
    float   LineIntensity       = 1.0f;                         // [-] `hl_int`
    bool    LineAtCivilOnly     = true;                         // [-] `hl_auto`
};

// Sun — panel entity `sun`.
struct SunCriteria
{
    bool    Visible             = true;
    float   LocalHours          = 6.4f;                         // [h]   `sun_time`
    float   LatitudeDegrees     = -26.0f;                       // [deg] `sun_lat`
    float   AzimuthOffsetDegrees= 0.0f;                         // [deg] `sun_az`
    float   ColourTemperature   = 5800.0f;                      // [K]   `sun_temp`
    float   Intensity           = 22.0f;                        // [-]   `sun_intensity`
    float   AngularDiameterDeg  = 0.53f;                        // [deg] `sun_ang`
    float   Softness            = 0.25f;                        // [-]   `sun_soft`
    float   DiscRadiance        = 12.0f;                        // [-]   `sun_disc`
};

// Sky — panel entity `sky`.
struct SkyCriteria
{
    bool    Visible             = true;
    Vector3 Tint                { 1.0f, 1.0f, 1.0f };           // [-] `sky_tint`, already through the panel's 1-(1-v)*.35
    float   Brightness          = 1.0f;                         // [-] `sky_bright`
    Vector3 GroundAlbedo        { 0.16862746f, 0.16078432f, 0.14117648f }; // [-] `sky_ground` #2b2924
    float   GroundBrightness    = 1.0f;                         // [-] `sky_groundBright`
};

// Stars — panel entity `stars`.
struct StarCriteria
{
    bool    Visible             = true;
    float   Density             = 1.0f;                         // [-] `st_density`
    float   Brightness          = 1.0f;                         // [-] `st_bright`
    float   Size                = 1.0f;                         // [-] `st_size`
    float   Glow                = 0.8f;                         // [-] `st_glow`
    float   Twinkle             = 0.5f;                         // [-] `st_twinkle`
    float   ColourStrength      = 1.0f;                         // [-] `st_color`
    float   ContrastLimit       = 1.5f;                         // [-] `st_limit`
    float   MilkyWay            = 1.0f;                         // [-] `st_milky`
    float   RotationDegrees     = 40.0f;                        // [deg] `st_rot`
    float   MilkyTiltDegrees    = -62.0f;                       // [deg] `st_tilt`
    float   Layers              = 3.0f;                         // [-] quality `High`
    float   Supersample         = 2.0f;                         // [-] quality `High`
};

// One moon roster slot — panel entity `moons`, atlas preset `luna`.
struct MoonCriteria
{
    bool    Visible             = true;
    bool    OppositeTheSun      = false;                        // `link`
    float   AzimuthDegrees      = 300.0f;
    float   ElevationDegrees    = 28.0f;
    float   AngularDiameterDeg  = 0.9f;                         // atlas `luna.size`
    float   Brightness          = 1.6f;
    float   Phase               = 0.12f;
    float   Glow                = 0.8f;
    float   SpinDegrees         = 0.0f;
    float   TiltDegrees         = 6.7f;                         // atlas `luna.tilt`
    float   Haze                = 0.0f;                         // atlas `luna.haze`
    float   Gamma               = 1.0f;                         // atlas `luna.gamma`
    Vector3 Tint                { 1.0f, 1.0f, 1.0f };
};

// Height Fog — panel entity `fog`. The live reference uploads `uFogOn = 0`; the transcription keeps that.
struct HeightFogCriteria
{
    bool    Visible             = false;                        // reference: `f1('uFogOn',0)`
    float   Density             = 0.011f;
    float   FalloffHeight       = 42.0f;                        // [m]
    Vector3 Tint                { 0.5607843f, 0.6431373f, 0.73333335f }; // #8fa4bb
    float   SunScatter          = 0.7f;
};

// Atmospheric (aerial perspective) Fog — panel entity `afog`.
struct AtmosphericFogCriteria
{
    bool    Visible             = true;
    float   Density             = 7.0f * 1e-5f;                 // panel `af_density` × 1e-5
    float   FalloffHeight       = 1200.0f;                      // [m]
    float   StartDistance       = 0.0f;                         // [m]
    float   MieShare            = 0.35f;
    float   Anisotropy          = 0.7f;
    float   SkyShare            = 1.0f;
    Vector3 Tint                { 1.0f, 1.0f, 1.0f };
};

// Local Volumetric Fog — panel entity `vfog`.
struct LocalFogCriteria
{
    bool    Visible             = true;
    float   Shape               = 1.0f;                         // 0 box · 1 ellipsoid · 2 sphere · 3 dome
    bool    Falloff             = true;                         // `vf_fall`
    Vector3 Placement           { 6.0f, 2.5f, -14.0f };         // [m] sky frame
    Vector3 HalfExtents         { 18.0f, 4.0f, 18.0f };         // [m]
    float   Softness            = 0.45f;
    float   Density             = 0.35f;
    float   NoiseStrength       = 0.75f;
    float   NoiseScale          = 5.0f;
    float   DriftSpeed          = 0.8f;
    Vector3 Albedo              { 0.8745098f, 0.9019608f, 0.93333334f }; // #dfe6ee
    float   Absorption          = 0.08f;
    float   Anisotropy          = 0.6f;
    bool    SelfShadow          = true;
    float   MarchSteps          = 28.0f;                        // quality `Balanced`
};

// Volumetric Clouds — panel entity `vclouds`, type `Cumulus`.
struct VolumetricCloudCriteria
{
    bool    Visible             = true;
    float   Variety             = 2.0f;                         // 0 stratus … 5 cirrus; `Cumulus`
    float   Coverage            = 0.45f;
    float   Density             = 1.0f;
    float   NoiseScale          = 1.4f;
    float   Detail              = 0.6f;
    float   Anvil               = 0.3f;
    float   BaseAltitude        = 1500.0f;                      // [m]
    float   Thickness           = 900.0f;                       // [m]
    float   DriftSpeed          = 6.0f;                         // [m/s]
    float   DriftDegrees        = 214.0f;                       // [deg]
    float   ForwardLobe         = 0.8f;                         // `cl_g1`
    float   BackwardLobe        = 0.3f;                         // `cl_g2`
    float   LobeMix             = 0.3f;
    float   Absorption          = 0.05f;
    float   AmbientShare        = 0.9f;
    float   Powder              = 0.6f;
    Vector3 Albedo              { 1.0f, 1.0f, 1.0f };
    float   MarchSteps          = 36.0f;                        // quality `Balanced`
    float   LightTaps           = 5.0f;                         // tier light taps
};

// Local Cloud — panel entity `lcloud`.
struct LocalCloudCriteria
{
    bool    Visible             = true;
    float   Shape               = 1.0f;                         // 0 box · 1 ellipsoid
    Vector3 Placement           { 40.0f, 120.0f, -160.0f };     // [m] sky frame
    Vector3 HalfExtents         { 90.0f, 35.0f, 70.0f };        // [m]
    float   Softness            = 0.5f;
    float   Variety             = 1.0f;                         // 0 stratiform · 1 cumuliform · 2 wispy
    float   Coverage            = 0.6f;
    float   Density             = 1.2f;
    float   NoiseScale          = 30.0f;
    float   Detail              = 0.55f;
    float   DriftSpeed          = 1.5f;
    float   MarchSteps          = 36.0f;                        // quality `Balanced`
};

// Cloud Layer (the thin analytic slab) — panel entity `clouds`. Reference uploads `uCloudOn = 0`.
struct CloudLayerCriteria
{
    bool    Visible             = false;                        // reference: `f1('uCloudOn',0)`
    float   Coverage            = 0.46f;
    float   Density             = 0.62f;
    float   Altitude            = 130.0f;                       // [m]
    float   NoiseScale          = 1.0f;
    float   Detail              = 0.55f;
    float   DriftSpeed          = 1.0f;
    Vector3 Tint                { 0.93333334f, 0.9529412f, 0.972549f };  // #eef3f8
    Vector3 Shade               { 0.36078432f, 0.41568628f, 0.4862745f };// #5c6a7c
};

// Wind — panel entity `wind`.
struct WindCriteria
{
    bool    Visible             = true;
    float   Speed               = 4.2f;                         // [m/s]
    float   BearingDegrees      = 214.0f;                       // [deg]
    float   Gust                = 0.25f;
    float   Turbulence          = 0.2f;
    float   Shear               = 0.6f;
    float   VeerDegreesPerKm    = 18.0f;
    bool    DrivesClouds        = true;
    bool    DrivesLocalCloud    = true;
    bool    DrivesLocalFog      = true;
    bool    DrivesPrecipitation = true;
};

// Precipitation — panel entity `precip`.
struct PrecipitationCriteria
{
    bool    Visible             = true;
    uint32_t Variety            = 0u;                           // 0 Rain · 1 Drizzle · 2 Hail · 3 Snow · 4 Sleet
    float   RateMillimetres     = 12.0f;                        // [mm/h]
    float   DensityScale        = 1.5f;
    float   SizeScale           = 1.0f;
    float   WindCoupling        = 0.35f;
    bool    FromClouds          = true;
    bool    Collide             = true;
    float   Restitution         = 0.35f;
    float   RestSeconds         = 1.6f;
    bool    Splash              = true;
    float   Accumulation        = 0.4f;
    float   Streak              = 0.7f;
    float   Opacity             = 0.85f;
    Vector3 Tint                { 0.85882354f, 0.91764706f, 0.99607843f }; // #dbeafe
    uint32_t Budget             = 25000u;                       // quality `High`
};

// Rainbow — panel entity `precip` § Rainbow.
struct RainbowCriteria
{
    bool    Visible             = true;
    float   Intensity           = 1.0f;
    float   Width               = 1.0f;
    float   Secondary           = 0.6f;
    float   Supernumerary       = 0.35f;
    bool    AlwaysOn            = false;                        // panel `rb_mode` = "From rain"
};

// Post / lens flare / tonemap — panel entity `post`.
struct PostCriteria
{
    bool    Visible             = true;
    float   ExposureStops       = 0.4f;                         // [EV] `pp_ev`
    uint32_t Tonemap            = 1u;                           // 0 none · 1 ACES · 2 Reinhard · 3 Filmic · 4 AgX
    float   Bloom               = 1.0f;
    float   Vignette            = 0.28f;
    float   Grain               = 0.1f;
    bool    FlareOn             = true;
    float   FlareVariety        = 0.0f;                         // 0 Cinematic · 1 Anamorphic · 2 Starburst · 3 Halo
    float   FlareIntensity      = 1.0f;
    float   Ghosts              = 5.0f;
    float   Halo                = 0.55f;
    float   Streak              = 0.8f;
    float   Chroma              = 0.65f;
};

// Point Light — panel entity `plight`. The reference holds it in `S.plP` rather than in a slider section,
// which is why it is easy to miss; it is uploaded every frame and it is on by default.
struct PointLightCriteria
{
    bool    Visible             = true;                         // `vis_plight`
    Vector3 Placement           { 6.0f, 2.2f, -4.0f };          // [m] sky frame, offset by the plane height
    Vector3 Colour              { 1.0f, 0.85098039f, 0.62745098f }; // #ffd9a0
    float   Intensity           = 14.0f;                        // [cd]
    float   Reach               = 26.0f;                        // [m] `distance`
    float   Decay               = 2.0f;                         // [-] inverse-power exponent
};

// Spot Light — panel entity `slight`. Likewise held in `S.slP`.
struct SpotLightCriteria
{
    bool    Visible             = true;                         // `vis_slight`
    Vector3 Placement           { -5.0f, 6.0f, -8.0f };         // [m] sky frame
    Vector3 Target              { 0.0f, 0.0f, -6.0f };          // [m] sky frame
    Vector3 Colour              { 0.90980392f, 0.94117647f, 1.0f }; // #e8f0ff
    float   Intensity           = 62.0f;                        // [cd]
    float   ConeDegrees         = 26.0f;                        // [deg] full cone; the shader uploads cos(half)
    float   Penumbra            = 0.42f;                        // [-] `uSLSoft`

    //    `uSLDir` — the shader is handed the normalised pointing vector, not the target.
    [[nodiscard]] Vector3 Direction() const noexcept
    {
        const Vector3 d = Target - Placement;
        const float   l = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        return l > 1e-6f ? Vector3{ d.x / l, d.y / l, d.z / l } : Vector3{ 0.0f, -1.0f, 0.0f };
    }

    //    `uSLCos` — the cosine of the HALF angle, which is what the cone test compares against.
    [[nodiscard]] float CosineHalfAngle() const noexcept
    {
        return std::cos(PanelRadians(ConeDegrees * 0.5f));                   // uSLCos = cos(angle/2*D2R)
    }
};

// Height Field — panel entity `terrain`, held in `S.terrP`. The live panel uploads `uTerrOn = 0`; the
// transcription keeps that default and keeps the sculpted field behind it, exactly as the reference does.
struct TerrainCriteria
{
    bool    Visible             = false;                        // reference: `f1('uTerrOn',0)`
    float   Size                = 180.0f;                       // [m] `terrP.size`
    float   Height              = 12.0f;                        // [m] `terrP.height`
    float   Frequency           = 1.2f;                         // [-] `terrP.frequency`
    float   Seed                = 417.0f;                       // [-] `terrP.seed`
    float   Roughness           = 0.88f;                        // [-] `terrP.roughness`
    bool    Wireframe           = false;                        // [-] `terrP.wireframe`
    Vector3 LowColour           { 0.14901961f, 0.21176471f, 0.15686275f }; // #263628
    Vector3 HighColour          { 0.53333333f, 0.56862745f, 0.48235294f }; // #88917b
    Vector3 Placement           { 0.0f, 0.0f, 0.0f };           // [m] sky frame
};

// Checker ground plane — panel entity `plane`.
struct GroundPlaneCriteria
{
    bool    Visible             = true;
    float   HalfSize            = 600.0f;                       // [m]
    float   Height              = 0.0f;                         // [m]
    float   CellSize            = 1.0f;                         // [m]
    bool    Graticule           = true;
    Vector3 TintA               { 0.9490196f, 0.9490196f, 0.9490196f };  // #f2f2f2
    Vector3 TintB               { 0.7882353f, 0.8f, 0.8235294f };        // #c9ccd2
    float   ShadowPresence      = 1.0f;                         // `uPresence.x`
    float   GlobalPresence      = 1.0f;                         // `uPresence.y`
};

// The observer — panel entity `camera`.
struct ObserverCriteria
{
    float   FieldOfViewDegrees  = 72.0f;                        // [deg] `cam_fov`
    float   Height              = 2.0f;                         // [m]   `cam_height`
    float   EastMetres          = 0.0f;                         // [m]   `cam_x`
    float   NorthMetres         = 0.0f;                         // [m]   `cam_z` (sky frame Z)
    float   YawDegrees          = 35.0f;                        // [deg] `cam_yaw`
    float   PitchDegrees        = -4.0f;                        // [deg] `cam_pitch`
    float   RollDegrees         = 0.0f;                         // [deg] `cam_roll`
};

//------------------------------------------------------------------------------------------------------------------------
//                                              THE WHOLE SKY, IN ONE RECORD
//------------------------------------------------------------------------------------------------------------------------

struct CelestialCriteria
{
    AtmosphereCriteria       Atmosphere;
    SunCriteria              Sun;
    SkyCriteria              Sky;
    StarCriteria             Stars;
    MoonCriteria             Moons[4];
    uint32_t                 MoonCount      = 1u;
    HeightFogCriteria        HeightFog;
    AtmosphericFogCriteria   AtmosphericFog;
    LocalFogCriteria         LocalFog;
    VolumetricCloudCriteria  VolumetricCloud;
    LocalCloudCriteria       LocalCloud;
    CloudLayerCriteria       CloudLayer;
    WindCriteria             Wind;
    PrecipitationCriteria    Precipitation;
    RainbowCriteria          Rainbow;
    PostCriteria             Post;
    GroundPlaneCriteria      GroundPlane;
    TerrainCriteria          Terrain;
    PointLightCriteria       PointLight;
    SpotLightCriteria        SpotLight;
    ObserverCriteria         Observer;
};

} // namespace Frontier::ProjectZero
