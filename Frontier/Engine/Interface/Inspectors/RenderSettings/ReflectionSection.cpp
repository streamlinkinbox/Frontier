/*==========================================================================================================================================
                                                           REFLECTIONSECTION.CPP
==========================================================================================================================================*/
// 🧩 Reflections: the glossy and mirror lobe, RTX reflection rays and their bounces, intensity and roughness cutoff, and the two
//    measured mirror options.

#include "Engine/Interface/Inspectors/RenderSettings/RenderSettingsSections.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/InspectorControls.h"

#include <cstdio>

namespace Frontier
{

bool ReflectionSection::Construct(ControlColumn& Page, RenderPipelineConfiguration& P, const RenderSettingsReading& Reading)
{
    using namespace SectionRows;
    char Status[48];
    if (!P.Reflection) std::snprintf(Status, sizeof(Status), "Off");
    else if (Reading.RayTraced && P.RayTracedReflections) std::snprintf(Status, sizeof(Status), "RTX \xC2\xB7 %dB", P.ReflectionBounces);
    else std::snprintf(Status, sizeof(Status), "Distance field");
    BeginSection(Page, "Reflections", "Aperture", Status,
                 "Traced at the trace resolution like the GI, then denoised by the glossy filter. They keep working with indirect GI off.");
    const char* Lighting    = RenderAvailability::Lighting(P);
    const char* Reflections = RenderAvailability::Reflections(P);
    const char* Rays        = RenderAvailability::ReflectionRays(P);
    bool Changed = Switch(Page, "Reflection", "Glossy & mirror reflections", P.Reflection, Lighting, "The reflection target and the exterior mirror glass");
    const char* RayToggle = Reflections != nullptr ? Reflections : RenderAvailability::RayTracedOnly(P);
    Changed |= Switch(Page, "RayTracedReflections", "RTX reflection rays", P.RayTracedReflections, RayToggle,
                      "Exact BVH triangle hits instead of distance-field sphere tracing");
    Changed |= Count(Page, "ReflectionBounces", "Reflection bounces", P.ReflectionBounces, 1, 4, "B", Rays);
    Changed |= Amount(Page, "ReflectionScale", "Reflection intensity", P.ReflectionScale, 0.0f, 3.0f, 0.05f, "\xC3\x97", Reflections);
    Changed |= Amount(Page, "ReflectionRoughness", "Roughness cutoff", P.ReflectionRoughness, 0.15f, 1.0f, 0.05f, "", Rays,
                      "Rougher surfaces take the diffuse irradiance instead (the distance-field path cuts off at 0.5)");

    SubHeading(Page, "Measured refinements");
    Changed |= Switch(Page, "MirrorFilter", "Mirror filter \xC2\xB7 recommended", P.MirrorFilter, Rays,
                      "Smooth surfaces take the unfiltered reflection: RMS error 2.70 to 1.58 for +13 ms (SwiftShader)");
    const char* Trace = Rays != nullptr ? Rays : (P.MirrorFilter ? nullptr : "Needs the mirror filter");
    Changed |= Switch(Page, "MirrorTrace", "Mirror trace \xC2\xB7 costly", P.MirrorTrace, Trace,
                      "With the mirror filter: smooth surfaces traced at screen resolution (RMS 1.36 for +561 ms on SwiftShader)");
    EndSection(Page);
    return Changed;
}

} // namespace Frontier

#endif
