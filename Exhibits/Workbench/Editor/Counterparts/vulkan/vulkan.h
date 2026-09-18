// CPU-proof counterpart: the proof harness never touches the device, but RayTracingSolver.h reaches SwapchainExchange.h,
//    which names a VkPhysicalDevice in a signature. This is enough for the harness to compile without the SDK
//    (CLAUDE.md: the AI sandbox has no Vulkan; the engine build uses the real header).
#pragma once
#include <cstdint>
typedef struct VkPhysicalDevice_T* VkPhysicalDevice;
