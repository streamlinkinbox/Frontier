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
    uint32_t             MapSide    = 512u;                          // [px]  shadow map side, tier or dropdown
    ShadowFilterCategory Filter     = ShadowFilterCategory::Pcss;    // [-]   which filter the tier selected
    uint32_t             FilterTaps = 5u;                            // [cnt] filter kernel side, in taps
    float                NearPlane  = 0.02f;                         // [m]   light frustum near
    float                FarPlane   = 100.0f;                        // [m]   light frustum far
    float                DepthBias  = 0.015f;                        // [m]   plus a slope term from NdotL
    float                HalfAngle  = 65.0f;                         // [deg] frustum half-angle about tap→centre
    float                Centre[3]  = { 0.0f, 0.0f, 0.0f };          // [m]   scene centre each tap aims at
};

} // namespace Frontier
