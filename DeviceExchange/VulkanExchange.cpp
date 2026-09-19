//============================================================================================================================================
// 📦 Frontier/DeviceExchange/VulkanExchange.cpp — Vulkan Device Communication Implementation
//============================================================================================================================================

#include "VulkanExchange.h"

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

VulkanExchange::VulkanExchange() noexcept
    : DeviceToken{}
    , ActiveSlotCount(3)
    , InitializedCondition(false)
{
}

VulkanExchange::~VulkanExchange() noexcept
{
    Terminate();
}

bool VulkanExchange::Initialize(bool EnableValidation) noexcept
{
    (void)EnableValidation;
    DeviceToken.DeviceIdentifier       = 0x10DE0001ULL;         // Representative GPU ID
    DeviceToken.GraphicsQueueIndex     = 0;
    DeviceToken.ComputeQueueIndex      = 1;
    DeviceToken.TransferQueueIndex     = 2;
    DeviceToken.RayTracingCapable      = true;
    DeviceToken.MeshletClusterCapable  = true;
    InitializedCondition               = true;
    return true;
}

void VulkanExchange::Terminate() noexcept
{
    if (InitializedCondition)
    {
        SynchronizeDevice();
        InitializedCondition = false;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 COMMAND SLOT OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

VulkanDeviceToken VulkanExchange::QueryDevice() const noexcept
{
    return DeviceToken;
}

CommandRecordSlot VulkanExchange::AcquireSlot(uint32_t DesiredSlotIndex) noexcept
{
    CommandRecordSlot Slot{};
    Slot.SlotIndex           = DesiredSlotIndex % ActiveSlotCount;
    Slot.FenceToken          = 0x1000ULL + Slot.SlotIndex;
    Slot.SemaphoreToken      = 0x2000ULL + Slot.SlotIndex;
    Slot.InFlightCondition   = false;
    return Slot;
}

void VulkanExchange::SubmitSlot(CommandRecordSlot Slot) noexcept
{
    (void)Slot;
}

void VulkanExchange::SynchronizeDevice() noexcept
{
}

} // namespace Frontier
