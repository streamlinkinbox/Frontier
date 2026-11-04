/*==========================================================================================================================================
                                                             SHADOWSECTION.CPP
==========================================================================================================================================*/
// 🧩 Sun & shadows: sun shadows, ray-traced shadows and the firefly clamp, the three measured shadow options, and one action that
//    applies the measured recommendation (Documentation/Plans/Ongoing/RayTracedLighting.md) or restores the demo.

#include "Engine/Interface/Inspectors/RenderSettings/RenderSettingsSections.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/InspectorControls.h"

#include <array>

namespace Frontier
{

bool ShadowSection::Construct(ControlColumn& Page, RenderPipelineConfiguration& P, const RenderSettingsReading& Reading)
{
    using namespace SectionRows;
    const bool Recommended = P.SolarDisc && P.StratifiedSun && P.PenumbraFilter && P.MirrorFilter;
    const bool Demo        = !P.SolarDisc && !P.StratifiedSun && !P.PenumbraFilter && !P.MirrorFilter && !P.MirrorTrace;
    BeginSection(Page, "Sun & shadows", "Sun", Reading.RayTraced && P.RayTracedShadows ? "BVH cone" : "Cascades",
                 "Sun visibility per render pixel: an exact BVH cone on the RTX path, cascaded shadow depth otherwise.");
    const char* Lighting = RenderAvailability::Lighting(P);
    const char* Shadows  = RenderAvailability::Shadows(P);
    const char* Rays     = RenderAvailability::ShadowRays(P);
    bool Changed = Switch(Page, "DirectShadow", "Sun shadows", P.DirectShadow, Lighting, "Ray-traced BVH cone or cascaded shadow depth");
    Changed |= Switch(Page, "RayTracedShadows", "Ray-traced shadows", P.RayTracedShadows, Shadows != nullptr ? Shadows : RenderAvailability::RayTracedOnly(P),
                      "Exact BVH visibility for the sun cone, street lamps and headlights");
    Changed |= Switch(Page, "FireflyFilter", "Firefly & boiling suppression", P.FireflyFilter, RenderAvailability::RayTracedOnly(P),
                      "Jacobian and luminance outlier clamp");

    SubHeading(Page, "Measured refinements");
    Changed |= Switch(Page, "SolarDisc", "Solar disc \xC2\xB7 recommended", P.SolarDisc, Shadows != nullptr ? Shadows : RenderAvailability::RayTracedOnly(P),
                      "The sun shaded as a disc, no solar disc in reflection rays: glint errors 572 to 56");
    Changed |= Switch(Page, "StratifiedSun", "Stratified sun \xC2\xB7 recommended", P.StratifiedSun, Rays, "Sun-disc samples stratified over 4 \xC3\x97 4 pixel blocks");
    Changed |= Switch(Page, "PenumbraFilter", "Penumbra filter \xC2\xB7 recommended", P.PenumbraFilter, Rays,
                      "Ray-traced sun verdicts filtered across their penumbra: errors 928 to 615, 30 % less flicker, +34 ms on SwiftShader");
    const std::array<ActionEntry, 2> Actions = {{
        {"ApplyRecommendation", "Apply the measured recommendation", !Recommended && Lighting == nullptr,
         "Solar disc, stratified sun, penumbra filter and mirror filter on"},
        {"RestoreDemo", "Restore the demo", !Demo, "Every measured option off: the VehicleHMI demo exactly"},
    }};
    ActionRowLook Look;
    Look.MarginTop    = 6;
    Look.MarginBottom = 14;
    switch (ActionRow::Construct(Page, Actions, Look))
    {
        case 0:
            P.SolarDisc = P.StratifiedSun = P.PenumbraFilter = P.MirrorFilter = true;
            Changed = true;
            break;
        case 1:
            P.SolarDisc = P.StratifiedSun = P.PenumbraFilter = P.MirrorFilter = P.MirrorTrace = false;
            Changed = true;
            break;
        default:
            break;
    }
    EndSection(Page);
    return Changed;
}

} // namespace Frontier

#endif
