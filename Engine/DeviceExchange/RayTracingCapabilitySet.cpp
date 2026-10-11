//============================================================================================================================================
//                                                  RAYTRACINGCAPABILITYSET.CPP
//============================================================================================================================================
// 🧩 Extension-first ray-tracing capability probe — see RayTracingCapabilitySet.h.

#include "RayTracingCapabilitySet.h"

#include <cstring>
#include <vector>

namespace Frontier {

namespace {

bool HasExtension(const std::vector<VkExtensionProperties>& List, const char* Name) noexcept
{
    for (const VkExtensionProperties& E : List)
        if (std::strcmp(E.extensionName, Name) == 0) return true;
    return false;
}

} // namespace

RayTracingCapabilitySet RayTracingCapabilitySet::Probe(VkPhysicalDevice Device) noexcept
{
    RayTracingCapabilitySet Set{};
    if (Device == VK_NULL_HANDLE) return Set;

    // ① Extension list — the source of truth for whether a tier may even be attempted.
    uint32_t Count = 0u;
    vkEnumerateDeviceExtensionProperties(Device, nullptr, &Count, nullptr);
    std::vector<VkExtensionProperties> Extensions(Count);
    if (Count) vkEnumerateDeviceExtensionProperties(Device, nullptr, &Count, Extensions.data());

    Set.AccelerationStructureExtension  = HasExtension(Extensions, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
    Set.RayQueryExtension               = HasExtension(Extensions, VK_KHR_RAY_QUERY_EXTENSION_NAME);
    Set.RayTracingPipelineExtension     = HasExtension(Extensions, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
    Set.DeferredHostOperationsExtension = HasExtension(Extensions, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    Set.ShaderInt64Atomics              = HasExtension(Extensions, VK_KHR_SHADER_ATOMIC_INT64_EXTENSION_NAME);

    // ② Features — chain only the structs whose extensions exist (chaining an unknown struct is undefined on old loaders).
    VkPhysicalDeviceAccelerationStructureFeaturesKHR AsFeatures{};
    AsFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    VkPhysicalDeviceRayQueryFeaturesKHR RqFeatures{};
    RqFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR RpFeatures{};
    RpFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
    VkPhysicalDeviceVulkan12Features Features12{};
    Features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;

    VkPhysicalDeviceFeatures2 Features2{};
    Features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    void** Tail = &Features2.pNext;
    auto Chain = [&](void* Node, void** Next) { *Tail = Node; Tail = Next; };
    Chain(&Features12, &Features12.pNext);
    if (Set.AccelerationStructureExtension) Chain(&AsFeatures, &AsFeatures.pNext);
    if (Set.RayQueryExtension)              Chain(&RqFeatures, &RqFeatures.pNext);
    if (Set.RayTracingPipelineExtension)    Chain(&RpFeatures, &RpFeatures.pNext);
    vkGetPhysicalDeviceFeatures2(Device, &Features2);

    Set.BufferDeviceAddress          = Features12.bufferDeviceAddress == VK_TRUE;
    Set.DescriptorIndexing           = Features12.descriptorIndexing == VK_TRUE && Features12.runtimeDescriptorArray == VK_TRUE;
    Set.AccelerationStructureFeature = Set.AccelerationStructureExtension && AsFeatures.accelerationStructure == VK_TRUE;
    Set.RayQueryFeature              = Set.RayQueryExtension && RqFeatures.rayQuery == VK_TRUE;
    Set.RayTracingPipelineFeature    = Set.RayTracingPipelineExtension && RpFeatures.rayTracingPipeline == VK_TRUE;

    // ③ Properties / limits
    VkPhysicalDeviceAccelerationStructurePropertiesKHR AsProperties{};
    AsProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
    VkPhysicalDeviceSubgroupProperties SubgroupProperties{};
    SubgroupProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
    VkPhysicalDeviceDriverProperties DriverProperties{};
    DriverProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;

    VkPhysicalDeviceProperties2 Properties2{};
    Properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    Tail = &Properties2.pNext;
    Chain(&SubgroupProperties, &SubgroupProperties.pNext);
    Chain(&DriverProperties, &DriverProperties.pNext);
    if (Set.AccelerationStructureExtension) Chain(&AsProperties, &AsProperties.pNext);
    vkGetPhysicalDeviceProperties2(Device, &Properties2);

    Set.SubgroupSize                    = SubgroupProperties.subgroupSize;
    Set.MaxComputeWorkGroupInvocations  = Properties2.properties.limits.maxComputeWorkGroupInvocations;
    Set.MaxGeometryCount                = AsProperties.maxGeometryCount;
    Set.MaxInstanceCount                = AsProperties.maxInstanceCount;
    Set.DeviceName                      = Properties2.properties.deviceName;
    Set.DriverInfo                      = std::string(DriverProperties.driverName) + " " + DriverProperties.driverInfo;
    return Set;
}

RayTracingTierCategory RayTracingCapabilitySet::QuerySupportedTier() const noexcept
{
    // Each tier requires the full extension + feature set of the tier below plus its own.
    const bool AsReady = AccelerationStructureExtension && AccelerationStructureFeature
                      && DeferredHostOperationsExtension && BufferDeviceAddress;
    if (AsReady && RayQueryFeature && RayTracingPipelineFeature) return RayTracingTierCategory::Pipeline;
    if (AsReady && RayQueryFeature)                              return RayTracingTierCategory::RayQuery;
    return RayTracingTierCategory::Software;
}

RayTracingTierCategory RayTracingCapabilitySet::ResolveTier(RayTracingRequestCategory Request) const noexcept
{
    const RayTracingTierCategory Supported = QuerySupportedTier();
    RayTracingTierCategory Wanted = Supported;
    switch (Request)
    {
        case RayTracingRequestCategory::Software: Wanted = RayTracingTierCategory::Software; break;
        case RayTracingRequestCategory::RayQuery: Wanted = RayTracingTierCategory::RayQuery; break;
        case RayTracingRequestCategory::Pipeline: Wanted = RayTracingTierCategory::Pipeline; break;
        default: break;
    }
    // Never above what the device supports — downgrade silently here; the caller announces it.
    return static_cast<uint32_t>(Wanted) > static_cast<uint32_t>(Supported) ? Supported : Wanted;
}

const char* RayTracingCapabilitySet::TierName(RayTracingTierCategory Tier) noexcept
{
    switch (Tier)
    {
        case RayTracingTierCategory::Software: return "Software BVH";
        case RayTracingTierCategory::RayQuery: return "Ray Query";
        case RayTracingTierCategory::Pipeline: return "Ray Pipeline";
        default: return "Unknown";
    }
}

const char* RayTracingCapabilitySet::RequestName(RayTracingRequestCategory Request) noexcept
{
    switch (Request)
    {
        case RayTracingRequestCategory::Auto:     return "Auto";
        case RayTracingRequestCategory::Software: return "Software";
        case RayTracingRequestCategory::RayQuery: return "RayQuery";
        case RayTracingRequestCategory::Pipeline: return "Pipeline";
        default: return "Unknown";
    }
}

} // namespace Frontier
