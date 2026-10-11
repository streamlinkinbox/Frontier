//============================================================================================================================================
//                                                           RAYQUERYEXCHANGE.H
//============================================================================================================================================
// 📦 Owns Vulkan triangle acceleration and queue-ordered placement refits for inline hardware ray queries.

#pragma once

#include <vulkan/vulkan.h>
#include <vector>

namespace Frontier {
class SceneStructure;
struct InstanceRecord;

class RayQueryExchange
{
public:
    bool Construct(VkDevice Device, VkPhysicalDevice PhysicalDevice, VkQueue Queue, uint32_t QueueFamily);
    bool ConstructScene(const SceneStructure& Scene);
    bool RefreshPlacements(const InstanceRecord* Placements, uint32_t Count);
    void RecordRefit(VkCommandBuffer Command);
    void Retire();
    [[nodiscard]] bool Ready() const noexcept { return SceneReady; }
    [[nodiscard]] VkDescriptorSetLayout DescriptorLayout() const noexcept { return Layout; }
    [[nodiscard]] VkDescriptorSet Descriptors() const noexcept { return Descriptor; }

private:
    struct Extent
    {
        VkBuffer Buffer = VK_NULL_HANDLE;
        VkDeviceMemory Memory = VK_NULL_HANDLE;
        VkDeviceAddress Address = 0u;
        VkDeviceSize Bytes = 0u;
    };
    bool Allocate(Extent& Storage, VkDeviceSize Bytes, VkBufferUsageFlags Usage, const void* Source = nullptr);
    void Reclaim(Extent& Storage);
    void RetireScene();
    VkAccelerationStructureGeometryKHR PlacementGeometry() const;
    void RecordTopLevel(VkCommandBuffer Command, bool Refit);

    VkDevice Device = VK_NULL_HANDLE;
    VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
    VkQueue Queue = VK_NULL_HANDLE;
    VkCommandPool Commands = VK_NULL_HANDLE;
    VkDescriptorSetLayout Layout = VK_NULL_HANDLE;
    VkDescriptorPool DescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet Descriptor = VK_NULL_HANDLE;
    VkAccelerationStructureKHR TopLevel = VK_NULL_HANDLE;
    std::vector<VkAccelerationStructureKHR> BottomLevels;
    std::vector<Extent> BottomStorage;
    Extent Vertices, Indices, Placements, TopStorage, Scratch;
    std::vector<VkAccelerationStructureInstanceKHR> PendingPlacements;
    VkDeviceSize ScratchAlignment = 256u;
    VkDeviceAddress ScratchAddress = 0u;
    bool SceneReady = false;
    bool PlacementsPending = false;
    PFN_vkGetAccelerationStructureBuildSizesKHR BuildSizes = nullptr;
    PFN_vkCreateAccelerationStructureKHR CreateAcceleration = nullptr;
    PFN_vkDestroyAccelerationStructureKHR DestroyAcceleration = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR BuildAcceleration = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR AccelerationAddress = nullptr;
};
}
