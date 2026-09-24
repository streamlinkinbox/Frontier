//============================================================================================================================================
//                                                       SHADOWEXCHANGE.H
//============================================================================================================================================
// 🧩 The GI-off path's shadow stage: one depth-only map per light tap, rasterised from the tap point, then sampled
//    by ShadowResolve under whichever filter the active quality tier selected.
//
//    This header is the CPU statement of the block the shaders read. ShadowFrameConfiguration mirrors the
//    `ShadowConstants` uniform in Engine/Shaders/ShadowRecords.slang byte for byte (std140) — change both sides or
//    neither. Nothing Vulkan appears here; the handles live in the .cpp, as with VisibilityExchange.
//
// Frame order, when Global Illumination is OFF:
//    Cull → visibility raster → HiZ → SurfaceResolve → [ShadowRaster × taps] → ShadowResolve → present.
//    The ReSTIR kernel is not dispatched at all in that mode: light visibility comes from these maps, not from rays.
//
// The technique ladder (Minimal hard · Economy wide PCF · Standard and above PCSS) is chosen by FidelityCriteria in
//    DisplayPresentation/FidelityClassifier.h and passed in here; the map SIDE additionally honours the Control
//    Centre's shadow-resolution dropdown, which outranks the tier. See Shaders/ShadowSample.slang for the filters
//    themselves and GeometricRaster/VisibilityRaster.cpp for the CPU implementation the proof gates.

#pragma once

#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                    SHADOW FILTER
//------------------------------------------------------------------------------------------------------------------------
// Mirrors kShadowFilter* in Shaders/ShadowRecords.slang and ShadowTechniqueCategory in FidelityClassifier.h.

enum class ShadowFilterCategory : uint32_t
{
    Hard = 0,   // one comparison per tap
    Pcf  = 1,   // fixed-radius percentage-closer box
    Pcss = 2,   // blocker search + similar triangles: contact-hardening
    Count = 3
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  LIGHT TAP RECORD
//------------------------------------------------------------------------------------------------------------------------
// One stratified point on a luminaire. Mirrors VisibilityRaster::LightTap on the CPU path so both produce the same
//    lighting from the same scene.

struct ShadowLightTap
{
    float Origin[3]   = { 0.0f, 0.0f, 0.0f };   // [m]   the tap's world position
    float LightSize   = 0.0f;                   // [m]   the luminaire's extent, √Area — PCSS's LightSize term
    float Radiance[3] = { 0.0f, 0.0f, 0.0f };   // [nit] emitted radiance
    float Weight      = 0.0f;                   // [m²]  the emitter area this tap integrates
    float Normal[3]   = { 0.0f, 0.0f, 1.0f };   // [-]   the emitter's unit normal (the LdotL term)
    float TangentHalf = 0.0f;                   // [-]   tan(HalfAngle) of this tap's frustum — PCSS's texels-per-metre
                                                //       term. Filled in by the exchange from HalfAngle, not by the
                                                //       caller. It is carried explicitly because the shader used to
                                                //       recover it from the world→clip matrix, which is Projection ·
                                                //       View and so only yields the right angle for a light that
                                                //       happens to point down an axis — see ShadowSample.slang.
};

//------------------------------------------------------------------------------------------------------------------------
//                                              SHADOW FRAME CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

struct ShadowFrameConfiguration
{
    static constexpr uint32_t MaximumTaps = 4u;   // matches kShadowMaximumTaps / VisibilityRaster::kLightTaps

    ShadowLightTap       Taps[MaximumTaps]{};
    uint32_t             TapCount   = 0u;                            // [cnt] live taps this frame

    // ── The sun ──────────────────────────────────────────────────────────────────────────────────────────────
    // ⚠️ THE SUN IS A LIGHT LIKE ANY OTHER AND MUST CAST A SHADOW. The ReSTIR kernel has treated it as one since
    //    the sun landed as a direct light (kSunLightIndex / PHatSun): outdoors it is the largest emitter present.
    //    The GI-off shadow stage did not, because PlaceShadowTaps only ever walked the emissive MESH triangles —
    //    so with GI off an outdoor scene rasterised lamp shadows and no sun shadow at all, and a scene whose only
    //    light IS the sun (no emissive mesh) placed zero taps and skipped the stage entirely.
    //
    //    The sun occupies tap slot 0 when enabled, and the mesh emitters fill the slots after it. It is flagged
    //    directional: the resolve must not apply 1/d² or a point-light geometry term to a light at infinity.
    bool                 SunEnabled   = false;                       // [-]   the sun is above the horizon and lit
    float                SunDirection[3] = { 0.0f, 0.0f, 1.0f };     // [-]   unit vector pointing TOWARD the sun
    float                SunRadiance[3]  = { 0.0f, 0.0f, 0.0f };     // [nit] the record's attenuated direct sun
    float                SunAngularRadius = 0.00465f;                // [rad] the solar disc — PCSS's penumbra scale

    // ── The moon ─────────────────────────────────────────────────────────────────────────────────────────────
    // The same argument as the sun, one step down in magnitude. A moonlit night has REAL shadows — soft, blue and
    //    dim, but present — and before this the moon lit nothing at all on the GI-off path: MoonAmbient() exists
    //    but is only summed by the ReSTIR kernel, so with GI off a night frame fell to the flat kAmbient fill and
    //    the moon was a painted disc in the sky that cast no light and no shadow.
    //
    //    The moon takes tap slot 1 (after the sun) and is likewise directional. Sun and moon can both be up —
    //    daytime moons are ordinary — so the two are independent, not an either/or.
    bool                 MoonEnabled   = false;                      // [-]   a moon is up and contributing
    float                MoonDirection[3] = { 0.0f, 0.0f, 1.0f };    // [-]   unit vector pointing TOWARD the moon
    float                MoonRadiance[3]  = { 0.0f, 0.0f, 0.0f };    // [nit] brightness × phase × tint, from the roster
    float                MoonAngularRadius = 0.00465f;               // [rad] the lunar disc — PCSS's penumbra scale

    uint32_t             MapSide    = 512u;                          // [px]  shadow map side, tier or dropdown
    ShadowFilterCategory Filter     = ShadowFilterCategory::Pcss;    // [-]   which filter the tier selected
    uint32_t             FilterTaps = 5u;                            // [cnt] filter kernel side, in taps
    float                NearPlane  = 0.02f;                         // [m]   light frustum near
    float                FarPlane   = 100.0f;                        // [m]   light frustum far
    float                DepthBias  = 0.015f;                        // [m]   plus a slope term from NdotL
    float                HalfAngle  = 65.0f;                         // [deg] frustum half-angle about tap→centre
    float                Centre[3]  = { 0.0f, 0.0f, 0.0f };          // [m]   scene centre each tap aims at

    // Which tap slots are directional (bit K = tap K). Mirrors ShadowControl.w in ShadowRecords.slang.
    uint32_t             DirectionalMask = 0u;
};

// Bit of ShadowControl.w, per tap: this tap is a light at infinity (the sun), not a point on an emitter.
inline constexpr uint32_t kShadowTapDirectionalBit = 1u;

} // namespace Frontier
