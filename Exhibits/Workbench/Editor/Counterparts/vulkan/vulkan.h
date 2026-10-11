// CPU-proof counterpart: the proof harness never touches the device, but RayTracingSolver.h reaches SwapchainExchange.h,
//    which names a VkPhysicalDevice in a signature. This is enough for the harness to compile without the SDK
//    (CLAUDE.md: the AI sandbox has no Vulkan; the engine build uses the real header).
#pragma once
#include <cstdint>

#define VK_NULL_HANDLE nullptr

using VkDeviceSize = uint64_t;

typedef struct VkPhysicalDevice_T*             VkPhysicalDevice;
typedef struct VkDevice_T*                     VkDevice;
typedef struct VkBuffer_T*                     VkBuffer;
typedef struct VkImage_T*                      VkImage;
typedef struct VkImageView_T*                  VkImageView;
typedef struct VkSampler_T*                    VkSampler;
typedef struct VkDescriptorPool_T*             VkDescriptorPool;
typedef struct VkDescriptorSetLayout_T*        VkDescriptorSetLayout;
typedef struct VkPipelineLayout_T*             VkPipelineLayout;
typedef struct VkPipeline_T*                   VkPipeline;
typedef struct VkDescriptorSet_T*              VkDescriptorSet;
typedef struct VkCommandBuffer_T*              VkCommandBuffer;
typedef struct VkDeviceMemory_T*               VkDeviceMemory;

struct VkPhysicalDeviceMemoryProperties {
    uint32_t memoryTypeCount;
    uint32_t memoryHeapCount;
};
