//==============================================================================================================================================
//                                                           VOLUMERAYMARCH.H
//==============================================================================================================================================
// 📦 One definition of how a gas volume is lit and integrated along a ray — read by the CPU proofs and transcribed into GasVolumeRaymarch.slang.
//
// This header exists for the reason WindField.h and AtmosphereModel.h exist: the same arithmetic has to run
//    in a headless proof with no device, and in a compute shader on one, and those two cannot share a binary.
//    Writing it twice guarantees they drift. Writing it once and transcribing it means a disagreement is a
//    diff rather than a mystery, and the proof renders the picture the shader is supposed to produce.
//
// 📐 The integration is Beer–Lambert along the ray, in front-to-back order, with four lighting terms that the
//    browser simulator established and that are kept verbatim so a scene reads the same in both:
//
//    | Term               | What it is                                                                      |
//    |--------------------|---------------------------------------------------------------------------------|
//    | Extinction         | σ = smoke · DensityExtinction + temperature · 0.65; transmittance is exp(−σ Δ𝑠) |
//    | Direct scatter     | sunlight, attenuated by a shadow march, shaped by the phase function            |
//    | Ambient scatter    | sky colour, weighted by height so the underside of a plume is darker            |
//    | Internal emission  | blackbody from the temperature reading — the fire lighting its own smoke        |
//
// 💡 THE PHASE FUNCTION IS A BLEND OF TWO HENYEY–GREENSTEIN LOBES, NOT ONE.
//    A single forward lobe makes smoke look like glass: bright directly away from the sun and flat everywhere
//    else. Mixing a backward lobe at 𝑔 = −0.25 in at 28 % restores the silver lining that a real plume has when
//    it is lit from behind. The browser arrived at those two numbers by eye and they are transcribed, not
//    re-derived, because matching it is the point.
//
// ⚠️ OPTICAL DEPTH IS NORMALISED BY THE DIAGONAL OF THE DOMAIN, AND THAT IS NOT COSMETIC.
//    Without it a plume authored in a 2 m box becomes nearly transparent the moment dynamic bounds expand it,
//    because the same density is integrated over a longer ray. OpticalScale is what holds the appearance still
//    while the box grows, and removing it looks exactly like the smoke evaporating.
//
// Dependency-free and header-only. Nothing here allocates, and nothing here knows what a device is.

#pragma once

#include <cmath>
#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE LIGHTING SETTINGS
//------------------------------------------------------------------------------------------------------------------------

// Named as the browser simulator names them, so the eventual panel is a projection of this struct rather than
//    a translation of it. Defaults are its DEFAULT_PARAMS readings.
struct VolumeLighting
{
    float DensityExtinction  = 18.5f;   // [1/m]  - how fast smoke swallows light
    float SmokeAlbedo        = 0.22f;   // [-]    - 0.22 is soot; dust is above 0.5
    float ShadowDensity      = 11.5f;   // [1/m]  - extinction used by the shadow march only
    float FireIntensity      = 5.6f;    // [-]    - scale on the blackbody emission
    float TemperatureScale   = 1.15f;   // [-]    - maps the temperature reading onto the palette
    float InternalScattering = 2.5f;    // [-]    - how much the fire lights its own smoke
    float PhaseAnisotropy    = 0.38f;   // [-]    - 𝑔 of the forward lobe, in (−1, 1)
    float AmbientIntensity   = 0.35f;   // [-]    - sky contribution
    float SunIntensity       = 2.2f;    // [-]    - sun contribution
    float Exposure           = 1.25f;   // [-]    - applied after integration, before the transfer curve
    int32_t RaymarchSteps    = 96;      // [-]    - samples along the primary ray
    int32_t ShadowSteps      = 6;       // [-]    - samples along the ray to the sun, per primary sample
};


// One sample of the volume at a point, in the units the integration wants.
struct VolumeSample
{
    float Smoke       = 0.0f;   // [-] - normalised density
    float Temperature = 0.0f;   // [K] - above ambient
};


// Reads the volume at a normalised coordinate inside the unit cube. The context is whatever the caller bound;
//    the integration never inspects it. This is the seam that lets the proof march a CoarseGasField and the
//    engine march a device image without the integration knowing which.
using VolumeReading = VolumeSample (*)(const void* Context, const float Coordinate[3]);

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE PHASE FUNCTION
//------------------------------------------------------------------------------------------------------------------------

/// 📦 Henyey–Greenstein phase for one lobe.
/// in    CosineAngle   [-]  cosine between the view ray and the light
/// in    Asymmetry     [-]  𝑔; positive is forward scattering, negative backward
/// out   float         [-]  normalised so its integral over the sphere is one
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline float HenyeyGreenstein(float CosineAngle, float Asymmetry) noexcept
{
    constexpr float Pi = 3.14159265358979323846f;
    const float Squared = Asymmetry * Asymmetry;
    float Denominator = std::pow(1.0f + Squared - 2.0f * Asymmetry * CosineAngle, 1.5f);
    if (Denominator < 1.0e-4f) Denominator = 1.0e-4f;
    return (1.0f - Squared) / (4.0f * Pi * Denominator);
}


/// 📦 The two-lobe phase the browser settled on: a backward lobe at 𝑔 = −0.25 mixed into the authored forward
///    lobe at 28 %. See the file header for why one lobe is not enough.
/// in    CosineAngle   [-]  cosine between the view ray and the light
/// in    Asymmetry     [-]  the authored forward 𝑔
/// out   float         [-]  the blended phase
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline float DualHenyeyGreenstein(float CosineAngle, float Asymmetry) noexcept
{
    const float Backward = HenyeyGreenstein(CosineAngle, -0.25f);
    const float Forward  = HenyeyGreenstein(CosineAngle, Asymmetry);
    return Backward * (1.0f - 0.72f) + Forward * 0.72f;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   BLACKBODY EMISSION
//------------------------------------------------------------------------------------------------------------------------

/// 📦 Emitted colour for a temperature reading, on the browser's default palette: black through deep red and
///    orange into a white core.
/// in    Temperature        [K]  reading above ambient
/// in    TemperatureScale   [-]  the authored mapping onto the palette
/// out   OutColour          [-]  linear radiance, not yet exposed
/// note  clamped at 1.6 of the palette's range, which is where the browser's white core saturates; without the
///       clamp a hot voxel blows out to a flat white disc with no shape in it
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline void BlackbodyRadiance(float Temperature, float TemperatureScale, float OutColour[3]) noexcept
{
    OutColour[0] = OutColour[1] = OutColour[2] = 0.0f;
    float Mapped = Temperature * TemperatureScale * 0.28f;
    if (Mapped <= 0.01f) return;
    if (Mapped > 1.6f) Mapped = 1.6f;

    // Four stops, linearly interpolated, transcribed from evaluateBlackbodyPalette's palette 0.
    static constexpr float Stops[4][3] = { { 0.00f, 0.00f, 0.000f },
                                           { 0.55f, 0.03f, 0.005f },
                                           { 1.00f, 0.24f, 0.010f },
                                           { 1.00f, 0.86f, 0.520f } };
    const float Scaled = Mapped / 1.6f * 3.0f;
    uint32_t Low = static_cast<uint32_t>(Scaled);
    if (Low > 2u) Low = 2u;
    const float Fraction = Scaled - static_cast<float>(Low);
    for (int Channel = 0; Channel < 3; ++Channel)
        OutColour[Channel] = Stops[Low][Channel] * (1.0f - Fraction) + Stops[Low + 1u][Channel] * Fraction;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   RAY AGAINST THE BOX
//------------------------------------------------------------------------------------------------------------------------

/// 📦 The interval of a ray lying inside the unit cube, by the slab method.
/// in    Origin        [-]  ray origin in unit-cube coordinates
/// in    Direction     [-]  ray direction, normalised
/// out   OutEnter      [-]  distance at which the ray enters; clamped to 0 when the origin is inside
/// out   OutExit       [-]  distance at which it leaves
/// out   bool          [-]  false when the ray misses, in which case the two distances are untouched
/// err   an axis-parallel ray is handled without dividing by zero, because the infinities the division
///       produces compare correctly against the slab bounds
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline bool UnitCubeInterval(const float Origin[3], const float Direction[3],
                             float& OutEnter, float& OutExit) noexcept
{
    float Enter = 0.0f, Exit = 1.0e9f;
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        const float Inverse = 1.0f / (Direction[Axis] != 0.0f ? Direction[Axis] : 1.0e-9f);
        float Near = (0.0f - Origin[Axis]) * Inverse;
        float Far  = (1.0f - Origin[Axis]) * Inverse;
        if (Near > Far) { const float Swap = Near; Near = Far; Far = Swap; }
        if (Near > Enter) Enter = Near;
        if (Far  < Exit)  Exit  = Far;
    }
    if (Exit <= Enter) return false;
    OutEnter = Enter;
    OutExit  = Exit;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE INTEGRATION
//------------------------------------------------------------------------------------------------------------------------

// What one marched ray resolved to.
struct MarchedRay
{
    float Radiance[3]  = { 0.0f, 0.0f, 0.0f };   // [-] - linear, pre-exposure
    float Transmittance = 1.0f;                  // [-] - 1 is clear air, 0 is fully occluded
};


/// 📦 Fraction of the sun's light reaching a point inside the volume, by a short march toward it.
/// in    Reading      [-]  the volume
/// in    Context      [-]  passed through untouched
/// in    Coordinate   [-]  the point, in unit-cube coordinates
/// in    SunDirection [-]  unit vector toward the sun
/// in    Lighting     [-]  ShadowSteps and ShadowDensity are read
/// out   float        [-]  transmittance in [0, 1]
/// note  the march leaves the cube early rather than clamping, because a clamped sample repeated at the wall
///       darkens everything near the boundary by an amount that depends only on where the box happens to be
/// cost  🚩  ShadowSteps volume readings per call, and this is called once per primary sample
/// tag   api, nonallocating, nonthrowing
inline float SunTransmittance(VolumeReading Reading, const void* Context, const float Coordinate[3],
                              const float SunDirection[3], const VolumeLighting& Lighting) noexcept
{
    const int32_t Steps = Lighting.ShadowSteps > 0 ? Lighting.ShadowSteps : 1;
    const float StepSize = 0.6f / static_cast<float>(Steps);

    float Depth = 0.0f;
    for (int32_t Step = 0; Step < Steps; ++Step)
    {
        const float Distance = (static_cast<float>(Step) + 0.5f) * StepSize;
        const float Point[3] = { Coordinate[0] + SunDirection[0] * Distance,
                                 Coordinate[1] + SunDirection[1] * Distance,
                                 Coordinate[2] + SunDirection[2] * Distance };
        if (Point[0] < 0.0f || Point[0] > 1.0f) break;
        if (Point[1] < 0.0f || Point[1] > 1.0f) break;
        if (Point[2] < 0.0f || Point[2] > 1.0f) break;
        Depth += Reading(Context, Point).Smoke * Lighting.ShadowDensity * StepSize;
    }
    return std::exp(-Depth);
}


/// 📦 Marches one ray through the unit cube and returns what it saw.
/// in    Reading       [-]  the volume
/// in    Context       [-]  passed through untouched
/// in    Origin        [-]  ray origin in unit-cube coordinates
/// in    Direction     [-]  ray direction, normalised
/// in    SunDirection  [-]  unit vector toward the sun
/// in    Lighting      [-]  the authored lighting
/// in    DomainDiagonal [m] world diagonal of the domain, for the optical normalisation in the file header
/// out   MarchedRay    [-]  radiance and transmittance
/// err   a ray missing the cube returns clear air rather than refusing
/// post  Transmittance is in [0, 1] and never increases along the ray
/// note  ⚠️ the march stops early once transmittance falls below 0.003, which is invisible and saves most of
///       the work inside a dense plume. Removing the early exit does not change the picture, it only costs.
/// cost  🔴  up to RaymarchSteps × (1 + ShadowSteps) volume readings
/// tag   api, nonallocating, nonthrowing
inline MarchedRay MarchVolume(VolumeReading Reading, const void* Context, const float Origin[3],
                              const float Direction[3], const float SunDirection[3],
                              const VolumeLighting& Lighting, float DomainDiagonal) noexcept
{
    MarchedRay Result;
    float Enter = 0.0f, Exit = 0.0f;
    if (!UnitCubeInterval(Origin, Direction, Enter, Exit)) return Result;

    const int32_t Steps = Lighting.RaymarchSteps > 0 ? Lighting.RaymarchSteps : 1;
    const float StepSize = (Exit - Enter) / static_cast<float>(Steps);

    // See the file header: this is what keeps a plume's opacity still while dynamic bounds expand the box.
    const float OpticalScale = 1.75f / (DomainDiagonal > 1.0f ? DomainDiagonal : 1.0f);

    const float Cosine = Direction[0] * SunDirection[0] + Direction[1] * SunDirection[1]
                       + Direction[2] * SunDirection[2];
    const float Phase = DualHenyeyGreenstein(Cosine, Lighting.PhaseAnisotropy);

    const float Sunlight[3] = { 1.00f * Lighting.SunIntensity,
                                0.93f * Lighting.SunIntensity,
                                0.82f * Lighting.SunIntensity };
    const float Skylight[3] = { 0.30f * Lighting.AmbientIntensity,
                                0.38f * Lighting.AmbientIntensity,
                                0.52f * Lighting.AmbientIntensity };

    for (int32_t Step = 0; Step < Steps; ++Step)
    {
        if (Result.Transmittance < 0.003f) break;

        const float Distance = Enter + (static_cast<float>(Step) + 0.5f) * StepSize;
        const float Point[3] = { Origin[0] + Direction[0] * Distance,
                                 Origin[1] + Direction[1] * Distance,
                                 Origin[2] + Direction[2] * Distance };
        const VolumeSample Reading_ = Reading(Context, Point);
        if (Reading_.Smoke <= 0.004f && Reading_.Temperature <= 0.015f) continue;

        const float Interval = StepSize * OpticalScale;
        float Extinction = Reading_.Smoke * Lighting.DensityExtinction + Reading_.Temperature * 0.65f;
        if (Extinction < 0.001f) Extinction = 0.001f;
        const float StepTransmittance = std::exp(-Extinction * Interval);

        // The powder term: a plume is darker where it is dense, because light entering it is scattered away
        //    before it can leave. Without it smoke reads as uniformly bright paper.
        const float Powder = 1.0f - 0.45f * std::exp(-Reading_.Smoke * Lighting.DensityExtinction * 2.2f);
        const float Direct = SunTransmittance(Reading, Context, Point, SunDirection, Lighting)
                           * Phase * Powder * 2.4f;

        // Height weighting, so the underside of a plume is darker than its crown. Axis 2 is up.
        const float Height = 0.45f + (1.15f - 0.45f) * Point[2];

        float Emission[3];
        BlackbodyRadiance(Reading_.Temperature, Lighting.TemperatureScale, Emission);
        const float Internal = Lighting.InternalScattering * (0.45f + 0.55f * std::exp(-Reading_.Smoke * 0.9f));

        for (int Channel = 0; Channel < 3; ++Channel)
        {
            const float Scattered = (Sunlight[Channel] * Direct + Skylight[Channel] * Height)
                                  * Lighting.SmokeAlbedo * Reading_.Smoke;
            const float Emitted   = Emission[Channel] * Lighting.FireIntensity * Internal;
            Result.Radiance[Channel] += Result.Transmittance * (Scattered + Emitted) * Interval;
        }
        Result.Transmittance *= StepTransmittance;
    }
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE TRANSFER
//------------------------------------------------------------------------------------------------------------------------

/// 📦 Linear radiance into a displayable byte, through exposure and the sRGB transfer curve.
/// in    Radiance   [-]  linear, pre-exposure
/// in    Exposure   [-]  the authored exposure
/// out   uint8_t    [-]  0 to 255
/// note  tone mapping is Reinhard, matching the browser; a filmic curve would read differently and the point
///       of this port is that it does not
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline uint8_t TransferToDisplay(float Radiance, float Exposure) noexcept
{
    float Exposed = Radiance * Exposure;
    if (Exposed < 0.0f) Exposed = 0.0f;
    Exposed = Exposed / (1.0f + Exposed);
    const float Encoded = Exposed <= 0.0031308f ? Exposed * 12.92f
                                                : 1.055f * std::pow(Exposed, 1.0f / 2.4f) - 0.055f;
    const float Scaled = Encoded * 255.0f + 0.5f;
    if (Scaled <= 0.0f)   return 0u;
    if (Scaled >= 255.0f) return 255u;
    return static_cast<uint8_t>(Scaled);
}

}   // namespace Frontier
