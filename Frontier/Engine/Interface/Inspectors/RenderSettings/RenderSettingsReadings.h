/*==========================================================================================================================================
                                                         RENDERSETTINGSREADINGS.H
==========================================================================================================================================*/
// 🧩 What the Render settings page derives from its record: the quality grades, the render and trace extents, the rays a refresh may
//    trace, the distance-field cascades, and why a setting does not apply under the others.

#pragma once

#include "Engine/Scene/EngineConfigurations.h"

#include <array>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    QUALITY GRADES
//------------------------------------------------------------------------------------------------------------------------

struct RenderQualityGrade
{
    const char* Name           = "";
    uint32_t    ClipResolution = 0;      // [voxels] distance clipmap edge
    double      FinestVoxel    = 0.0;    // [m]
    uint32_t    CardExtent     = 0;      // [texel] surface-card atlas edge
    uint32_t    ShadowExtent   = 0;      // [texel]
    uint32_t    Steps          = 0;      // [-] distance-field march steps
    double      RenderScale    = 1.0;    // [-]
    uint32_t    TileBudget     = 0;      // [-] static card tiles relit per refresh
    uint32_t    PixelLimit     = 0;      // [px] render pixel ceiling
};

// 📝 IlluminationConstants.cpp ResolveQualityGrade, field for field (the interface library does not link the renderer).
inline constexpr std::array<RenderQualityGrade, 3> RenderQualityGrades = {{
    {"Low", 64, 0.25, 1024, 1024, 64, 0.6, 384, 1280u * 720u},
    {"Medium", 96, 0.16, 2048, 2048, 80, 0.85, 1024, 1600u * 900u},
    {"High", 128, 0.125, 2048, 2048, 96, 1.0, 2048, 1920u * 1080u},
}};

//------------------------------------------------------------------------------------------------------------------------
//                                                    READING
//------------------------------------------------------------------------------------------------------------------------

struct RenderSettingsReading
{
    const RenderQualityGrade* Grade     = &RenderQualityGrades[2];
    bool                      Analytic  = false;   // [-] the analytic illumination is showing
    bool                      RayTraced = false;   // [-] the RTX path
    bool                      Measured  = false;   // [-] the extents are the viewport's, not derived
    std::array<uint32_t, 2>   Render    = {0, 0};  // [px]
    std::array<uint32_t, 2>   Trace     = {0, 0};  // [px] ReSTIR GI and reflection targets
    // 📝 Rays one refresh may trace at most (every path to its last bounce, every pixel reflective): ① sun visibility per render pixel,
    //    ② M paths × bounces per trace pixel with an NEE ray per vertex, ③ the visibility re-trace of reused samples, ④ reflection paths
    //    per trace pixel, ⑤ the mirror trace per render pixel. Zero on the GTX path and the analytic illumination.
    uint64_t SunRays        = 0;
    uint64_t PathRays       = 0;
    uint64_t VisibilityRays = 0;
    uint64_t ReflectionRays = 0;
    uint64_t MirrorRays     = 0;
    uint64_t TotalRays      = 0;
    std::array<double, 4> CascadeSpans{};          // [m] edge of each distance-field cascade
};

// 📝 The viewport's extents when it presents, else the demo's rule for a 1600 × 900 panel: render = panel × grade scale × viewport scale
//    under the grade's pixel ceiling, trace = ⌈render ÷ trace scale⌉.
[[nodiscard]] RenderSettingsReading ResolveRenderSettingsReading(const RenderPipelineConfiguration& Parameters);

// 📝 "1.2M", "640k", "980".
void FormatRayCount(uint64_t Rays, char* Text, size_t Capacity);

//------------------------------------------------------------------------------------------------------------------------
//                                                    AVAILABILITY
//------------------------------------------------------------------------------------------------------------------------

// 📝 Why a group of settings does not apply under the others (null when it does): the page fades such rows instead of hiding them.
namespace RenderAvailability
{
[[nodiscard]] const char* Lighting(const RenderPipelineConfiguration& Parameters) noexcept;     // the global illumination's settings
[[nodiscard]] const char* RayTracedOnly(const RenderPipelineConfiguration& Parameters) noexcept;
[[nodiscard]] const char* Indirect(const RenderPipelineConfiguration& Parameters) noexcept;     // the path and reuse settings
[[nodiscard]] const char* Reflections(const RenderPipelineConfiguration& Parameters) noexcept;
[[nodiscard]] const char* ReflectionRays(const RenderPipelineConfiguration& Parameters) noexcept;
[[nodiscard]] const char* Shadows(const RenderPipelineConfiguration& Parameters) noexcept;
[[nodiscard]] const char* ShadowRays(const RenderPipelineConfiguration& Parameters) noexcept;
[[nodiscard]] const char* Denoiser(const RenderPipelineConfiguration& Parameters) noexcept;
} // namespace RenderAvailability

} // namespace Frontier
