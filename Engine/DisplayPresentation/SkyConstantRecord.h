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
//        block size = 128 B
//
//    Every member is a four-component vector on purpose. std140 rounds a vec3 up to sixteen bytes anyway, so
//    packing scalars into the spare lanes costs nothing and keeps the block at eight rows — the alternative is a
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
};

static_assert(sizeof(SkyConstantRecord) == 128u, "SkyConstants must match the shader's std140 block exactly");
static_assert(sizeof(SkyConstantRecord) % 16u == 0u, "std140 blocks are 16-B aligned");
static_assert(offsetof(SkyConstantRecord, SunRadiance) == 16u, "SkySunRadiance sits at offset 16");
static_assert(offsetof(SkyConstantRecord, Rayleigh)    == 32u, "SkyRayleigh sits at offset 32");
static_assert(offsetof(SkyConstantRecord, Mie)         == 48u, "SkyMie sits at offset 48");
static_assert(offsetof(SkyConstantRecord, Ozone)       == 64u, "SkyOzone sits at offset 64");
static_assert(offsetof(SkyConstantRecord, Planet)      == 80u, "SkyPlanet sits at offset 80");
static_assert(offsetof(SkyConstantRecord, Control)     == 96u, "SkyControl sits at offset 96");
static_assert(offsetof(SkyConstantRecord, Twilight)    == 112u, "SkyTwilight sits at offset 112");

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE PACKER
//------------------------------------------------------------------------------------------------------------------------

// Reshapes the model into the block. The ONLY place a celestial value becomes GPU bytes, so the kernel and the
//    CPU raster cannot be handed different skies.
inline SkyConstantRecord PackSkyConstants(const AtmosphereMedium& Medium, const AtmosphereLight& Light,
                                          const TwilightSettings& Twilight, float SunElevationDegrees,
                                          float CameraHeightMetres, uint32_t ViewSamples,
                                          uint32_t LightSamples, bool Enabled) noexcept
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
    return R;
}

} // namespace Frontier
