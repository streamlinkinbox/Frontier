//============================================================================================================================================
// 📦 Engine/DisplayPresentation/SkyConstantRecord.h — the CPU mirror of Shaders/SkyRecords.slang's uniform block
//============================================================================================================================================
// The GPU seam for the Celestial sky. One struct, written by the host and read by ReSTIRViewport at binding 21.
//
// ⚠️ THIS MUST MATCH THE SHADER'S std140 LAYOUT EXACTLY, and the static_asserts below are what makes a mismatch a
//    compile error rather than a wrong picture. Verified against the compiled SPIR-V, which lays the block out as:
//
//        offset   0   SkySunDirection      vec4
//        offset  16   SkySunRadiance       vec4
//        offset  32   SkyRayleigh          vec4
//        offset  48   SkyMie               vec4
//        offset  64   SkyOzone             vec4
//        offset  80   SkyPlanet            vec4
//        offset  96   SkyControl           uvec4
//        offset 112   SkyTwilight          vec4
//        offset 128   SkySunDirect         vec4
//        offset 144   SkyCloudLayer        vec4
//        offset 160   SkyCloudShape        vec4
//        offset 176   SkyCloudWind         vec4
//        offset 192   SkyCloudAlbedo       vec4
//        offset 208   SkyCloudControl      uvec4
//        offset 224   SkyLocalCloudCentre  vec4
//        offset 240   SkyLocalCloudHalfSize vec4
//        offset 256   SkyLocalCloudParams  vec4
//        offset 272   SkyLocalFogCentre    vec4
//        offset 288   SkyLocalFogHalfSize  vec4
//        offset 304   SkyLocalFogParams     vec4
//        block size = 320 B
//
//    Every member is a four-component vector on purpose. std140 rounds a vec3 up to sixteen bytes anyway, so
//    packing scalars into the spare lanes costs nothing and keeps the block at whole rows — the alternative is a
//    layout where adding one float silently shifts everything after it.
//
// ⚠️ AND IT MUST NOT BE A SECOND COPY OF THE MEDIUM. The coefficients come from AtmosphereMedium; this only
//    reshapes them. A literal 5.8e-6 appearing here would be the GI-on and GI-off skies starting to drift, which
//    is the same failure ColourTransfer.h records for the tone map.

#pragma once

#include "AtmosphereModel.h"
#include "VolumetricMedia.h"

#include <cstddef>
#include <cstdint>

namespace Frontier {

// The sun's angular RADIUS, one definition for three consumers: the shader's disc draws this body, the
//    viewport's next-event estimator samples it, and the CPU raster draws it a third time (VisibilityRaster) —
//    a sun whose paths disagree on its size would be two suns. This MUST equal SkyRecords.slang's
//    kSunAngularRadius; the SkyKernelParityProof pins the shader's literal, this pins the host's.
inline constexpr float kSunAngularRadius = 0.53f * (3.14159265358979323846f / 180.0f) * 0.5f;

// Aureole compression: the knee [linear], shoulder slope and angular width [rad] of the soft shoulder SkyAlong
//    (both paths) eases the single-scatter peak through. MUST equal SkyRecords.slang's kAureoleKnee/Slope/Sigma;
//    the SkyKernelParityProof pins the three literals pairwise, like the sun's radius above.
inline constexpr float kAureoleKnee  = 1.0f;
inline constexpr float kAureoleSlope = 0.06f;
inline constexpr float kAureoleSigma = 5.0f * (3.14159265358979323846f / 180.0f);

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE RECORD
//------------------------------------------------------------------------------------------------------------------------

struct SkyConstantRecord
{
    float    SunDirection[4];   // xyz = unit toward the sun; w = elevation [deg], for the twilight window
    float    SunRadiance[4];    // xyz = colour x intensity; w = 0 disables the sky entirely
    float    Rayleigh[4];       // xyz = beta_R x strength [1/m]; w = Rayleigh scale height [m]
    float    Mie[4];            // x = beta_M x strength, y = Mie scale height [m], z = anisotropy g, w = unused
    float    Ozone[4];          // xyz = beta_O x strength [1/m]; w = unused
    float    Planet[4];         // x = planet radius [m], y = shell height [m], z = camera height [m], w = unused
    uint32_t Control[4];        // x = view samples, y = light samples, z = weather flags, w = cloud type
    float    Twilight[4];       // x = glow, y = line, z = 1 when the line is civil-only, w = unused
    float    SunDirect[4];      // xyz = panel direct-sun factor 0.11·gain·colour·T (kernel: ÷Ω, ×Ω back); w = unused

    // The weather is carried in the same permanent sky block. Keeping it beside the atmosphere is important: a
    // ReSTIR miss and a bounce miss must see the exact same cloud field, and a second weather descriptor would
    // make those paths race the per-frame sky update. The rows are deliberately vec4/uvec4-shaped for std140.
    float    CloudLayer[4];     // base, thickness, coverage, density [m, m, -, x]
    float    CloudShape[4];     // feature scale, ceiling, anvil, HG anisotropy
    float    CloudWind[4];      // speed [m/s], bearing [deg], shear [/km], veer [deg/km]
    float    CloudAlbedo[4];    // rgb albedo, w = cloud clock seconds
    uint32_t CloudControl[4];   // cloud steps, local steps, sun taps, reserved
    float    LocalCloudCentre[4]; // xyz centre, w unused
    float    LocalCloudHalfSize[4]; // xyz half-size, w unused
    float    LocalCloudParams[4]; // density, coverage, feature scale, HG anisotropy
    float    LocalFogCentre[4];
    float    LocalFogHalfSize[4];
    float    LocalFogParams[4];
};

static_assert(sizeof(SkyConstantRecord) == 320u, "SkyConstants must match the shader's std140 block exactly");
static_assert(sizeof(SkyConstantRecord) % 16u == 0u, "std140 blocks are 16-B aligned");
static_assert(offsetof(SkyConstantRecord, SunRadiance) == 16u, "SkySunRadiance sits at offset 16");
static_assert(offsetof(SkyConstantRecord, Rayleigh)    == 32u, "SkyRayleigh sits at offset 32");
static_assert(offsetof(SkyConstantRecord, Mie)         == 48u, "SkyMie sits at offset 48");
static_assert(offsetof(SkyConstantRecord, Ozone)       == 64u, "SkyOzone sits at offset 64");
static_assert(offsetof(SkyConstantRecord, Planet)      == 80u, "SkyPlanet sits at offset 80");
static_assert(offsetof(SkyConstantRecord, Control)     == 96u, "SkyControl sits at offset 96");
static_assert(offsetof(SkyConstantRecord, Twilight)       == 112u, "SkyTwilight sits at offset 112");
static_assert(offsetof(SkyConstantRecord, SunDirect)      == 128u, "SkySunDirect sits at offset 128");
static_assert(offsetof(SkyConstantRecord, CloudLayer)      == 144u, "SkyCloudLayer sits at offset 144");
static_assert(offsetof(SkyConstantRecord, CloudShape)      == 160u, "SkyCloudShape sits at offset 160");
static_assert(offsetof(SkyConstantRecord, CloudWind)       == 176u, "SkyCloudWind sits at offset 176");
static_assert(offsetof(SkyConstantRecord, CloudAlbedo)     == 192u, "SkyCloudAlbedo sits at offset 192");
static_assert(offsetof(SkyConstantRecord, CloudControl)    == 208u, "SkyCloudControl sits at offset 208");
static_assert(offsetof(SkyConstantRecord, LocalCloudCentre) == 224u, "SkyLocalCloudCentre sits at offset 224");
static_assert(offsetof(SkyConstantRecord, LocalCloudHalfSize) == 240u, "SkyLocalCloudHalfSize sits at offset 240");
static_assert(offsetof(SkyConstantRecord, LocalCloudParams) == 256u, "SkyLocalCloudParams sits at offset 256");
static_assert(offsetof(SkyConstantRecord, LocalFogCentre)  == 272u, "SkyLocalFogCentre sits at offset 272");
static_assert(offsetof(SkyConstantRecord, LocalFogHalfSize) == 288u, "SkyLocalFogHalfSize sits at offset 288");
static_assert(offsetof(SkyConstantRecord, LocalFogParams)  == 304u, "SkyLocalFogParams sits at offset 304");

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE PACKER
//------------------------------------------------------------------------------------------------------------------------

// Reshapes the model into the block. The ONLY place a celestial value becomes GPU bytes, so the kernel and the
//    CPU raster cannot be handed different skies.
inline SkyConstantRecord PackSkyConstants(const AtmosphereMedium& Medium, const AtmosphereLight& Light,
                                          const TwilightSettings& Twilight, float SunElevationDegrees,
                                          float CameraHeightMetres, uint32_t ViewSamples,
                                          uint32_t LightSamples, bool Enabled, float SunDirectGain = 1.0f) noexcept
{
    SkyConstantRecord R{};

    for (int C = 0; C < 3; ++C) R.SunDirection[C] = Light.Direction[C];
    R.SunDirection[3] = SunElevationDegrees;

    // A disabled sky is signalled by zero radiance rather than a flag, so the shader's early-out is a float
    //    compare it already has to do rather than an extra branch.
    const float Gain = Enabled ? Light.Intensity : 0.0f;
    for (int C = 0; C < 3; ++C) R.SunRadiance[C] = Light.Colour[C] * Gain;
    R.SunRadiance[3] = Enabled ? 1.0f : 0.0f;

    for (int C = 0; C < 3; ++C) R.Rayleigh[C] = Medium.RayleighScattering[C] * Medium.RayleighStrength;
    R.Rayleigh[3] = Medium.RayleighScaleHeight;

    R.Mie[0] = Medium.MieScattering * Medium.MieStrength;
    R.Mie[1] = Medium.MieScaleHeight;
    R.Mie[2] = Medium.MieAnisotropy;

    for (int C = 0; C < 3; ++C) R.Ozone[C] = Medium.OzoneAbsorption[C] * Medium.OzoneStrength;

    R.Planet[0] = Medium.PlanetRadius;
    R.Planet[1] = Medium.AtmosphereHeight;
    R.Planet[2] = CameraHeightMetres;

    R.Control[0] = ViewSamples  == 0u ? 1u : ViewSamples;
    R.Control[1] = LightSamples == 0u ? 1u : LightSamples;

    R.Twilight[0] = Twilight.GlowIntensity;
    R.Twilight[1] = Twilight.LineIntensity;
    R.Twilight[2] = Twilight.LineAtCivilOnly ? 1.0f : 0.0f;

    // The direct sun: the reference panel's direct-sun factor (0.11·colour·gain·transmittance), evaluated for
    //    one observer, not per ray — scene relief is metres against an 8 km scale height. The transmittance is
    //    marched by the same Integrate the raster calls: the view march only (the light march feeds the
    //    in-scatter this row discards, so it runs at 1 step). 0.11 is the panel's principal surface-lighting
    //    gain (CelestialPanel.html:1162,1182 — alb·(trans·colour·intensity·.11·ndl·sh+amb)): the panel multiplies
    //    it directly, while the kernel divides by the disc solid angle (SunEmission) and multiplies back in the
    //    estimator, so the converged NEE equals the panel's formula while sampling the real disc (soft shadows)
    //    and shadowing through the BVH (the panel's sh term). SunDirectGain is the panel's Direct slider.
    //    Below the horizon the planet shadows the sun — hard zero, not the short ground-segment transmittance
    //    the march would return. A hidden or disabled sun is zero through the same Gain as the sky's radiance,
    //    so the direct light and the skylight cannot disagree about whether the sun is up.
    //    R.SunDirect[3] stays 0: reserved, like Mie.w/Ozone.w/Planet.w/Twilight.w.
    constexpr float kPanelDirectSunGain = 0.11f;
    if (Gain > 0.0f && SunElevationDegrees > 0.0f && SunDirectGain > 0.0f)
    {
        const AtmosphereSample SunPath = AtmosphereModel::Integrate(Medium, Light, CameraHeightMetres,
            Light.Direction, R.Control[0], 1u);
        for (int C = 0; C < 3; ++C)
            R.SunDirect[C] = kPanelDirectSunGain * SunDirectGain * Light.Colour[C] * Gain * SunPath.Transmittance[C];
    }
    return R;
}

// Packs the weather consumed by the ReSTIR miss, bounce and direct-sun paths. The atmosphere and weather share a
// record so a live slider update cannot leave the sky on one frame and its cloud shadow on another. Disabled media
// are represented by flags, not by stale rows: the record is zero-filled by the caller and the shader's early-outs
// then cost no cloud samples.
inline void PackSkyVolumes(SkyConstantRecord& R, bool Enabled,
                           const CloudLayerSettings& Cloud, const LocalVolumeSettings& LocalCloud,
                           const LocalVolumeSettings& LocalFog, const WindSettings& Wind,
                           const VolumetricBudget& Budget, float CloudTime) noexcept
{
    constexpr uint32_t kCloudLayer       = 1u << 0u;
    constexpr uint32_t kLocalCloud       = 1u << 1u;
    constexpr uint32_t kLocalFog         = 1u << 2u;
    constexpr uint32_t kCloudFollowWind  = 1u << 3u;
    constexpr uint32_t kLocalCloudWind   = 1u << 4u;
    constexpr uint32_t kLocalFogWind     = 1u << 5u;

    const bool UseCloud = Enabled && Cloud.Enabled;
    const bool UseLocalCloud = Enabled && LocalCloud.Enabled;
    const bool UseLocalFog = Enabled && LocalFog.Enabled;
    R.Control[2] = (UseCloud ? kCloudLayer : 0u)
                 | (UseLocalCloud ? kLocalCloud : 0u)
                 | (UseLocalFog ? kLocalFog : 0u)
                 | (UseCloud && Cloud.FollowWind ? kCloudFollowWind : 0u)
                 | (UseLocalCloud && LocalCloud.FollowWind ? kLocalCloudWind : 0u)
                 | (UseLocalFog && LocalFog.FollowWind ? kLocalFogWind : 0u);
    R.Control[3] = UseCloud ? static_cast<uint32_t>(Cloud.Type) : 0u;

    const float Ceiling = Cloud.CeilingMetres > 0.0f ? Cloud.CeilingMetres : 0.0f;
    R.CloudLayer[0] = Cloud.Base;
    R.CloudLayer[1] = Cloud.Thickness;
    R.CloudLayer[2] = Cloud.Coverage;
    R.CloudLayer[3] = Cloud.Density;
    R.CloudShape[0] = Cloud.Scale;
    R.CloudShape[1] = Ceiling;
    R.CloudShape[2] = Cloud.Anvil;
    R.CloudShape[3] = Cloud.Anisotropy;
    R.CloudWind[0] = Wind.Speed;
    R.CloudWind[1] = Wind.Bearing;
    R.CloudWind[2] = Wind.Shear;
    R.CloudWind[3] = Wind.Veer;
    for (int C = 0; C < 3; ++C) R.CloudAlbedo[C] = Cloud.Albedo[C];
    R.CloudAlbedo[3] = CloudTime;
    R.CloudControl[0] = Budget.CloudSteps == 0u ? 1u : Budget.CloudSteps;
    R.CloudControl[1] = Budget.LocalSteps == 0u ? 1u : Budget.LocalSteps;
    R.CloudControl[2] = Budget.LightTaps == 0u ? 1u : Budget.LightTaps;

    for (int C = 0; C < 3; ++C)
    {
        R.LocalCloudCentre[C] = LocalCloud.Centre[C];
        R.LocalCloudHalfSize[C] = LocalCloud.HalfSize[C];
        R.LocalFogCentre[C] = LocalFog.Centre[C];
        R.LocalFogHalfSize[C] = LocalFog.HalfSize[C];
    }
    R.LocalCloudParams[0] = LocalCloud.Density;
    R.LocalCloudParams[1] = LocalCloud.Coverage;
    R.LocalCloudParams[2] = LocalCloud.Scale;
    R.LocalCloudParams[3] = LocalCloud.Anisotropy;
    R.LocalFogParams[0] = LocalFog.Density;
    R.LocalFogParams[1] = LocalFog.Coverage;
    R.LocalFogParams[2] = LocalFog.Scale;
    R.LocalFogParams[3] = LocalFog.Anisotropy;
}

} // namespace Frontier
