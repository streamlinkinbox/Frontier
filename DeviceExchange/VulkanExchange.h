//============================================================================================================================================
// 📦 Frontier/DeviceExchange/VulkanExchange.h — Vulkan Device Communication and Hardware Transport
//============================================================================================================================================

#pragma once

#include <cstdint>
#include <cstddef>
#include <string_view>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                    DEVICE TOKEN
//------------------------------------------------------------------------------------------------------------------------

struct VulkanDeviceToken
{
    uint64_t                DeviceIdentifier;                   // [token] unique physical device identifier
    uint32_t                ComputeQueueIndex;                  // [index] hardware compute queue index
    uint32_t                TransferQueueIndex;                 // [index] asynchronous copy queue index
    uint32_t                GraphicsQueueIndex;                 // [index] rasterization queue index
    bool                    RayTracingCapable;                  // [bool] hardware ray query support
    bool                    MeshletClusterCapable;              // [bool] hardware task/mesh shader support
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 COMMAND RECORD SLOT
//------------------------------------------------------------------------------------------------------------------------

struct CommandRecordSlot
{
    uint64_t                FenceToken;                         // [token] synchronization fence handle
    uint64_t                SemaphoreToken;                     // [token] completion semaphore handle
    uint32_t                SlotIndex;                          // [index] command recording slot index
    bool                    InFlightCondition;                  // [bool] true when submitted to hardware queue
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  VULKAN EXCHANGE
//------------------------------------------------------------------------------------------------------------------------

class VulkanExchange
{
public:
    VulkanExchange() noexcept;
    ~VulkanExchange() noexcept;

    VulkanExchange(const VulkanExchange&) = delete;
    VulkanExchange& operator=(const VulkanExchange&) = delete;

    [[nodiscard]] bool      Initialize(bool EnableValidation) noexcept;
    void                    Terminate() noexcept;

    [[nodiscard]] VulkanDeviceToken QueryDevice() const noexcept;
    [[nodiscard]] CommandRecordSlot AcquireSlot(uint32_t DesiredSlotIndex) noexcept;
    void                    SubmitSlot(CommandRecordSlot Slot) noexcept;
    void                    SynchronizeDevice() noexcept;

    // Single unified conversion operator for hardware querying
    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

private:
    VulkanDeviceToken       DeviceToken;                        // [struct] physical device attributes
    uint32_t                ActiveSlotCount;                    // [count] command slots in ring
    bool                    InitializedCondition;               // [bool] initialization status
};

template<>
inline VulkanDeviceToken VulkanExchange::Convert<VulkanDeviceToken>() const noexcept
{
    return DeviceToken;
}

template<>
inline uint64_t VulkanExchange::Convert<uint64_t>() const noexcept
{
    return DeviceToken.DeviceIdentifier;
}

} // namespace Frontier
