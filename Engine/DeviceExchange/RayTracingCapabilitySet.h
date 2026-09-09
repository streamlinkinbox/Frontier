//============================================================================================================================================
//                                                   RAYTRACINGCAPABILITYSET.H
//============================================================================================================================================
// 🧩 Probes a physical device for the Vulkan ray-tracing extension set and resolves the renderer tier (plan v2.1 §3.4).
//
//    Tiers:  Software  — compute traversal over Slate's own CWBVH (any Vulkan 1.2 device; the primary path for GTX-class)
//            RayQuery  — VK_KHR_acceleration_structure + VK_KHR_ray_query from the same compute shaders
//            Pipeline  — VK_KHR_ray_tracing_pipeline on top (later high tier)
//
//    ⚠️ Detection trusts the device EXTENSION list first. Early drivers advertised rayQuery = true in the feature struct on
//       Pascal parts and crashed at pipeline creation (Khronos Vulkan-Docs #1241); a feature flag without its extension is
//       therefore ignored. The configuration file may force a lower tier; a tier is never faked upward.

#pragma once

#include <cstdint>
#include <string>
#include <vulkan/vulkan.h>

namespace Frontier {

enum class RayTracingTierCategory : uint32_t
{
    Software = 0,   // compute traversal, no RT extensions required
    RayQuery = 1,   // hardware AS + ray queries in compute
    Pipeline = 2,   // + ray-tracing pipeline (SBT)
    Count    = 3
};

// Configuration request ([render] ray_tracing_tier). Auto = highest supported.
enum class RayTracingRequestCategory : uint32_t { Auto = 0, Software = 1, RayQuery = 2, Pipeline = 3, Count = 4 };

struct RayTracingCapabilitySet
{
    // ── Extensions present in vkEnumerateDeviceExtensionProperties ─────────────────────────────────────────────────
    bool AccelerationStructureExtension = false;   // VK_KHR_acceleration_structure
    bool RayQueryExtension              = false;   // VK_KHR_ray_query
    bool RayTracingPipelineExtension    = false;   // VK_KHR_ray_tracing_pipeline
    bool DeferredHostOperationsExtension= false;   // VK_KHR_deferred_host_operations (required by acceleration_structure)
    bool BufferDeviceAddress            = false;   // core 1.2 feature — needed by AS builds
    bool DescriptorIndexing             = false;   // core 1.2 feature — bindless materials (both tiers)
    bool ShaderInt64Atomics             = false;   // VK_KHR_shader_atomic_int64 (visibility-buffer compute raster later)

    // ── Features actually enabled-able (queried through the pNext chain, only meaningful when the extension exists) ─
    bool AccelerationStructureFeature   = false;
    bool RayQueryFeature                = false;
    bool RayTracingPipelineFeature      = false;

    // ── Limits ─────────────────────────────────────────────────────────────────────────────────────────────────────
    uint32_t SubgroupSize               = 0u;
    uint32_t MaxComputeWorkGroupInvocations = 0u;
    uint64_t MaxGeometryCount           = 0u;      // VkPhysicalDeviceAccelerationStructurePropertiesKHR
    uint64_t MaxInstanceCount           = 0u;

    std::string DeviceName;
    std::string DriverInfo;

    // Highest tier the device genuinely supports (extension AND feature for each level).
    [[nodiscard]] RayTracingTierCategory QuerySupportedTier() const noexcept;
    // Resolves the configured request against the supported tier; never returns a tier above Supported.
    [[nodiscard]] RayTracingTierCategory ResolveTier(RayTracingRequestCategory Request) const noexcept;

    // Fill from a physical device (instance must be ≥ 1.1 so vkGetPhysicalDeviceFeatures2 / Properties2 exist).
    static RayTracingCapabilitySet Probe(VkPhysicalDevice Device) noexcept;

    [[nodiscard]] static const char* TierName(RayTracingTierCategory Tier) noexcept;
    [[nodiscard]] static const char* RequestName(RayTracingRequestCategory Request) noexcept;
};

} // namespace Frontier
