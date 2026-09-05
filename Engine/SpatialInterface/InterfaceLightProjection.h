//============================================================================================================================================
//                                                  INTERFACELIGHTPROJECTION.H
//============================================================================================================================================
// 🧩 The panel as a light source. Until now the interface has been an overlay: composited over the resolved image,
//    contributing nothing to the room. A real display lights what is near it and shows up in anything reflective,
//    and the chrome sphere sitting under the showroom panel makes its absence obvious.
//
//    This projects the composed figures onto a single emissive PROXY QUAD registered into the scene before the
//    acceleration structure is built. The renderer then treats it like any other emitter — no new light path, no
//    shader branch in the integrator, no change to the luminaire format. The panel lights the plinth and appears
//    in the sphere because it is, as far as the tracer is concerned, an ordinary glowing rectangle.
//
//    Tiers (References/InterfaceLightContribution-Plan.md, resolved earlier):
//        Off    no proxy at all — overlay only, exactly today's behaviour, zero cost
//        Low    proxy in the light list, radiance = the area-weighted average of the lit figures   ← default
//        High   same geometry and same luminaire; the hit shader evaluates the figures instead of the average
//        Ultra  one luminaire per emissive figure, so a single lit button casts its own shaped pool
//
//    Off/Low are implemented here. High and Ultra need shader-side work in the hit path and are deliberately
//    reported as unavailable rather than silently falling back to Low — a tier that quietly does something else
//    is worse than one that says it is not there.
//
//    Engine ⇄ project seam: this knows figures have colour, area and an emissive weight. It does not know which
//    figure is a warning lamp. The project picks the tier and owns the panel's placement.

#pragma once

#include "InterfaceStructure.h"
#include "InterfaceSequence.h"

#include <cstdint>

namespace Frontier {

class SceneStructure;

//------------------------------------------------------------------------------------------------------------------------
//                                                    FIDELITY TIER
//------------------------------------------------------------------------------------------------------------------------

enum class InterfaceFidelityTier : uint32_t
{
    Off   = 0u,
    Low   = 1u,
    High  = 2u,
    Ultra = 3u
};

[[nodiscard]] const char* InterfaceFidelityTierName(InterfaceFidelityTier Tier) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                  PANEL RADIANCE
//------------------------------------------------------------------------------------------------------------------------
// What the panel emits, averaged over its face. Computed from the figures so the light follows the interface:
//    a panel showing a red warning screen casts red light without anyone maintaining a second colour.

struct PanelRadiance
{
    float Red   = 0.0f;    // [W/sr/m²] relative
    float Green = 0.0f;
    float Blue  = 0.0f;
    float LitArea   = 0.0f;   // [m²] summed area of the emissive figures
    float PanelArea = 0.0f;   // [m²] area of the proxy quad
    uint32_t Contributors = 0u;   // [cnt] figures that carried any emission

    // Fraction of the panel that is actually lit. A mostly dark panel with one bright lamp should not light the
    //    room as if the whole face were glowing, so the proxy's radiance is scaled by this.
    // Clamped to 1: interface figures OVERLAP (a knob on its bed, a fill in its trough), so the summed lit area
    //    genuinely exceeds the panel face — measured 122% on the trial panel. Unclamped, a busy panel would light
    //    the room more than a solid white one, which is the wrong way round.
    [[nodiscard]] float Coverage() const noexcept
    {
        if (PanelArea <= 0.0f) return 0.0f;
        const float Fraction = LitArea / PanelArea;
        return Fraction < 1.0f ? Fraction : 1.0f;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 PANEL PROXY REQUEST
//------------------------------------------------------------------------------------------------------------------------

struct PanelProxyRequest
{
    InterfaceFidelityTier Tier = InterfaceFidelityTier::Low;

    // The panel's face in world space: centre, and the two half-axes spanning it. Taken from the placement the
    //    project already gives the interface, so the proxy cannot drift from the drawn panel.
    float CentreX = 0.0f, CentreY = 0.0f, CentreZ = 0.0f;          // [m]
    float RightX  = 1.0f, RightY  = 0.0f, RightZ  = 0.0f;          // [m] half-axis along the panel's local +X
    float UpX     = 0.0f, UpY     = 0.0f, UpZ     = 1.0f;          // [m] half-axis along the panel's local +Y

    // Scales the emitted radiance. A display that looks correct on screen is usually far too bright as a light
    //    source, because the overlay is composited at full intensity while a real emitter is measured in nits.
    float Gain = 1.0f;      // [-]
};

//------------------------------------------------------------------------------------------------------------------------
//                                               INTERFACE LIGHT PROJECTION
//------------------------------------------------------------------------------------------------------------------------

class InterfaceLightProjection
{
public:
    // Area-weighted average emission of the composed figures. Uses the SAME resolved tint the renderer draws, so
    //    the light and the image cannot disagree about the panel's colour.
    [[nodiscard]] static PanelRadiance MeasureRadiance(const InterfaceStructure& Structure,
                                                       const InterfaceSequence& Composition,
                                                       float PanelArea) noexcept;

    // Registers the proxy quad and its emissive material into the scene. Must be called BEFORE
    //    SceneStructure::Finalise, which is what flattens triangles and builds the luminaire table — a proxy
    //    added afterwards would be geometry the light sampler never sees.
    //
    //    Returns the instance ordinal, or 0xFFFFFFFF for Off, an unimplemented tier, or a degenerate quad.
    static uint32_t ComposeProxy(SceneStructure& Scene, const PanelProxyRequest& Request,
                                 const PanelRadiance& Radiance) noexcept;

    // Whether a tier does anything today. High and Ultra are specified and not yet built; a caller should report
    //    that rather than assume it got what it asked for.
    [[nodiscard]] static bool IsTierAvailable(InterfaceFidelityTier Tier) noexcept
    {
        return Tier == InterfaceFidelityTier::Off || Tier == InterfaceFidelityTier::Low;
    }
};

} // namespace Frontier
