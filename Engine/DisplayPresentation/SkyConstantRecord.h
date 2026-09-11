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
//        block size = 144 B
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

#include <cstddef>
#include <cstdint>

namespace Frontier {

// The sun's angular RADIUS, one definition for three consumers: the shader's disc draws this body, the
//    viewport's next-event estimator samples it, and the CPU raster draws it a third time (VisibilityRaster) —
//    a sun whose paths disagree on its size would be two suns. This MUST equal SkyRecords.slang's
//    kSunAngularRadius; the SkyKernelParityProof pins the shader's literal, this pins the host's.
inline constexpr float kSunAngularRadius = 0.53f * (3.14159265358979323846f / 180.0f) * 0.5f;

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
    uint32_t Control[4];        // x = view samples, y = light samples, z/w = unused
    float    Twilight[4];       // x = glow, y = line, z = 1 when the line is civil-only, w = unused
    float    SunDirect[4];      // xyz = panel direct-sun factor 0.11·gain·colour·T (kernel: ÷Ω, ×Ω back); w = unused
};

static_assert(sizeof(SkyConstantRecord) == 144u, "SkyConstants must match the shader's std140 block exactly");
static_assert(sizeof(SkyConstantRecord) % 16u == 0u, "std140 blocks are 16-B aligned");
static_assert(offsetof(SkyConstantRecord, SunRadiance) == 16u, "SkySunRadiance sits at offset 16");
static_assert(offsetof(SkyConstantRecord, Rayleigh)    == 32u, "SkyRayleigh sits at offset 32");
static_assert(offsetof(SkyConstantRecord, Mie)         == 48u, "SkyMie sits at offset 48");
static_assert(offsetof(SkyConstantRecord, Ozone)       == 64u, "SkyOzone sits at offset 64");
static_assert(offsetof(SkyConstantRecord, Planet)      == 80u, "SkyPlanet sits at offset 80");
static_assert(offsetof(SkyConstantRecord, Control)     == 96u, "SkyControl sits at offset 96");
static_assert(offsetof(SkyConstantRecord, Twilight)    == 112u, "SkyTwilight sits at offset 112");
static_assert(offsetof(SkyConstantRecord, SunDirect)   == 128u, "SkySunDirect sits at offset 128");

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

} // namespace Frontier
