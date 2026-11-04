/*==========================================================================================================================================
                                                        RENDERSETTINGSREADINGS.CPP
==========================================================================================================================================*/
// 🧩 What the Render settings page derives from its record: the quality grades, the render and trace extents, the rays a refresh may
//    trace, the distance-field cascades, and why a setting does not apply under the others.

#include "Engine/Interface/Inspectors/RenderSettings/RenderSettingsReadings.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    READING
//------------------------------------------------------------------------------------------------------------------------

RenderSettingsReading ResolveRenderSettingsReading(const RenderPipelineConfiguration& P)
{
    RenderSettingsReading Reading;
    Reading.Grade     = &RenderQualityGrades[static_cast<size_t>(std::clamp(P.QualityTier, 0, 2))];
    Reading.Analytic  = P.AnalyticIllumination;
    Reading.RayTraced = P.HardwareRayQueryTier && !P.AnalyticIllumination;
    const uint32_t TraceScale = static_cast<uint32_t>(std::clamp(P.TraceScale, 1, 4));
    if (P.Measured.Presenting && P.Measured.ScreenExtent[0] > 0)
    {
        Reading.Measured = true;
        Reading.Render   = P.Measured.ScreenExtent;
    }
    else
    {
        // 📝 SceneViewport::ResolveScreenExtent for a 1600 × 900 panel at density 1.
        double       Ratio  = Reading.Grade->RenderScale * std::clamp(static_cast<double>(P.RenderScale), 0.25, 2.0);
        const double Pixels = 1600.0 * 900.0 * Ratio * Ratio;
        if (Pixels > Reading.Grade->PixelLimit) Ratio *= std::sqrt(Reading.Grade->PixelLimit / Pixels);
        Reading.Render = {static_cast<uint32_t>(std::max(64.0, std::floor(1600.0 * Ratio))), static_cast<uint32_t>(std::max(64.0, std::floor(900.0 * Ratio)))};
    }
    Reading.Trace = {(Reading.Render[0] + TraceScale - 1) / TraceScale, (Reading.Render[1] + TraceScale - 1) / TraceScale};

    if (Reading.RayTraced)
    {
        const uint64_t RenderPixels = static_cast<uint64_t>(Reading.Render[0]) * Reading.Render[1];
        const uint64_t TracePixels  = static_cast<uint64_t>(Reading.Trace[0]) * Reading.Trace[1];
        const uint64_t Vertex       = P.MultiBounceNee ? 2u : 1u;
        Reading.SunRays             = P.DirectShadow && P.RayTracedShadows ? RenderPixels : 0u;
        Reading.PathRays            = P.GlobalIllumination ? TracePixels * static_cast<uint64_t>(P.InitialCandidates) * static_cast<uint64_t>(P.MaxBounces) * Vertex : 0u;
        Reading.VisibilityRays      = P.GlobalIllumination && P.VisibilityReuse ? TracePixels : 0u;
        const bool Reflective       = P.Reflection && P.RayTracedReflections;
        Reading.ReflectionRays      = Reflective ? TracePixels * static_cast<uint64_t>(P.ReflectionBounces) * 2u : 0u;
        Reading.MirrorRays          = Reflective && P.MirrorFilter && P.MirrorTrace ? RenderPixels : 0u;
        Reading.TotalRays = Reading.SunRays + Reading.PathRays + Reading.VisibilityRays + Reading.ReflectionRays + Reading.MirrorRays;
    }
    for (size_t Cascade = 0; Cascade < Reading.CascadeSpans.size(); ++Cascade)
        Reading.CascadeSpans[Cascade] = Reading.Grade->ClipResolution * Reading.Grade->FinestVoxel * static_cast<double>(1u << Cascade);
    return Reading;
}

void FormatRayCount(uint64_t Rays, char* Text, size_t Capacity)
{
    if (Rays >= 1000000u)   std::snprintf(Text, Capacity, "%.1fM", static_cast<double>(Rays) / 1.0e6);
    else if (Rays >= 1000u) std::snprintf(Text, Capacity, "%.0fk", static_cast<double>(Rays) / 1.0e3);
    else                    std::snprintf(Text, Capacity, "%llu", static_cast<unsigned long long>(Rays));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    AVAILABILITY
//------------------------------------------------------------------------------------------------------------------------

namespace RenderAvailability
{

const char* Lighting(const RenderPipelineConfiguration& P) noexcept
{
    return P.AnalyticIllumination ? "The analytic illumination is showing (Debug views)" : nullptr;
}

const char* RayTracedOnly(const RenderPipelineConfiguration& P) noexcept
{
    if (const char* Reason = Lighting(P)) return Reason;
    return P.HardwareRayQueryTier ? nullptr : "RTX only";
}

const char* Indirect(const RenderPipelineConfiguration& P) noexcept
{
    if (const char* Reason = Lighting(P)) return Reason;
    return P.GlobalIllumination ? nullptr : "Indirect global illumination is off";
}

const char* Reflections(const RenderPipelineConfiguration& P) noexcept
{
    if (const char* Reason = Lighting(P)) return Reason;
    return P.Reflection ? nullptr : "Reflections are off";
}

const char* ReflectionRays(const RenderPipelineConfiguration& P) noexcept
{
    if (const char* Reason = Reflections(P)) return Reason;
    if (!P.HardwareRayQueryTier) return "RTX only";
    return P.RayTracedReflections ? nullptr : "Needs RTX reflection rays";
}

const char* Shadows(const RenderPipelineConfiguration& P) noexcept
{
    if (const char* Reason = Lighting(P)) return Reason;
    return P.DirectShadow ? nullptr : "Sun shadows are off";
}

const char* ShadowRays(const RenderPipelineConfiguration& P) noexcept
{
    if (const char* Reason = Shadows(P)) return Reason;
    if (!P.HardwareRayQueryTier) return "RTX only";
    return P.RayTracedShadows ? nullptr : "Needs ray-traced shadows";
}

const char* Denoiser(const RenderPipelineConfiguration& P) noexcept
{
    if (const char* Reason = Lighting(P)) return Reason;
    return P.VarianceFilter ? nullptr : "The denoiser is off";
}

} // namespace RenderAvailability

} // namespace Frontier
