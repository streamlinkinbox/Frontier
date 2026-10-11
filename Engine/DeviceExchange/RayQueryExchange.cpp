//============================================================================================================================================
//                                                          RAYQUERYEXCHANGE.CPP
//============================================================================================================================================
// 📦 Builds persistent triangle BLASes and refits a TLAS without modifying resources still read by earlier recordings.

#include "RayQueryExchange.h"
#include "../GeometricRaster/SceneStructure.h"
#include <algorithm>
#include <cstring>

namespace Frontier {

bool RayQueryExchange::Allocate(Extent& Storage, VkDeviceSize Bytes, VkBufferUsageFlags Usage, const void* Source)
{
    Storage.Bytes = std::max<VkDeviceSize>(Bytes, 16u);
    VkBufferCreateInfo Description{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    Description.size = Storage.Bytes;
    Description.usage = Usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    if (vkCreateBuffer(Device, &Description, nullptr, &Storage.Buffer) != VK_SUCCESS) return false;
    VkMemoryRequirements Requirements{};
    vkGetBufferMemoryRequirements(Device, Storage.Buffer, &Requirements);
    VkPhysicalDeviceMemoryProperties Properties{};
    vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &Properties);
    const VkMemoryPropertyFlags Requested = Source ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                                 : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    uint32_t Selected = UINT32_MAX;
    for (uint32_t Slot = 0u; Slot < Properties.memoryTypeCount; ++Slot)
        if ((Requirements.memoryTypeBits & (1u << Slot)) && (Properties.memoryTypes[Slot].propertyFlags & Requested) == Requested)
        { Selected = Slot; break; }
    if (Selected == UINT32_MAX) return false;
    VkMemoryAllocateFlagsInfo Address{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO };
    Address.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    VkMemoryAllocateInfo Allocation{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    Allocation.pNext = &Address;
    Allocation.allocationSize = Requirements.size;
    Allocation.memoryTypeIndex = Selected;
    if (vkAllocateMemory(Device, &Allocation, nullptr, &Storage.Memory) != VK_SUCCESS) return false;
    if (vkBindBufferMemory(Device, Storage.Buffer, Storage.Memory, 0u) != VK_SUCCESS) return false;
    if (Source)
    {
        void* Destination = nullptr;
        if (vkMapMemory(Device, Storage.Memory, 0u, Bytes, 0u, &Destination) != VK_SUCCESS) return false;
        std::memcpy(Destination, Source, static_cast<size_t>(Bytes));
        vkUnmapMemory(Device, Storage.Memory);
    }
    VkBufferDeviceAddressInfo Query{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
    Query.buffer = Storage.Buffer;
    Storage.Address = vkGetBufferDeviceAddress(Device, &Query);
    return Storage.Address != 0u;
}

void RayQueryExchange::Reclaim(Extent& Storage)
{
    if (Storage.Buffer) vkDestroyBuffer(Device, Storage.Buffer, nullptr);
    if (Storage.Memory) vkFreeMemory(Device, Storage.Memory, nullptr);
    Storage = {};
}

void RayQueryExchange::RetireScene()
{
    SceneReady = PlacementsPending = false;
    if (TopLevel) DestroyAcceleration(Device, TopLevel, nullptr);
    TopLevel = VK_NULL_HANDLE;
    for (auto Acceleration : BottomLevels)
        if (Acceleration) DestroyAcceleration(Device, Acceleration, nullptr);
    BottomLevels.clear();
    for (auto& Storage : BottomStorage) Reclaim(Storage);
    BottomStorage.clear();
    for (auto* Storage : { &Vertices, &Indices, &Placements, &TopStorage, &Scratch }) Reclaim(*Storage);
    PendingPlacements.clear();
}

void RayQueryExchange::Retire()
{
    if (!Device) return;
    // Caller retires after its device fence; construction also waits before replacing a resident scene.
    RetireScene();
    if (DescriptorPool) vkDestroyDescriptorPool(Device, DescriptorPool, nullptr);
    if (Layout) vkDestroyDescriptorSetLayout(Device, Layout, nullptr);
    if (Commands) vkDestroyCommandPool(Device, Commands, nullptr);
    DescriptorPool = VK_NULL_HANDLE;
    Layout = VK_NULL_HANDLE;
    Commands = VK_NULL_HANDLE;
    Descriptor = VK_NULL_HANDLE;
    Device = VK_NULL_HANDLE;
}

bool RayQueryExchange::Construct(VkDevice ActiveDevice, VkPhysicalDevice ActivePhysicalDevice, VkQueue ActiveQueue, uint32_t QueueFamily)
{
    Device = ActiveDevice;
    PhysicalDevice = ActivePhysicalDevice;
    Queue = ActiveQueue;
    BuildSizes = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(vkGetDeviceProcAddr(Device, "vkGetAccelerationStructureBuildSizesKHR"));
    CreateAcceleration = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(vkGetDeviceProcAddr(Device, "vkCreateAccelerationStructureKHR"));
    DestroyAcceleration = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(vkGetDeviceProcAddr(Device, "vkDestroyAccelerationStructureKHR"));
    BuildAcceleration = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(vkGetDeviceProcAddr(Device, "vkCmdBuildAccelerationStructuresKHR"));
    AccelerationAddress = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(vkGetDeviceProcAddr(Device, "vkGetAccelerationStructureDeviceAddressKHR"));
    if (!BuildSizes || !CreateAcceleration || !DestroyAcceleration || !BuildAcceleration || !AccelerationAddress) return false;
    VkPhysicalDeviceAccelerationStructurePropertiesKHR AccelerationProperties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR };
    VkPhysicalDeviceProperties2 Properties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &AccelerationProperties };
    vkGetPhysicalDeviceProperties2(PhysicalDevice, &Properties);
    ScratchAlignment = std::max<VkDeviceSize>(AccelerationProperties.minAccelerationStructureScratchOffsetAlignment, 1u);
    VkCommandPoolCreateInfo CommandDescription{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    CommandDescription.queueFamilyIndex = QueueFamily;
    if (vkCreateCommandPool(Device, &CommandDescription, nullptr, &Commands) != VK_SUCCESS) return false;
    VkDescriptorSetLayoutBinding DescriptorDescription{ 0u, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
    VkDescriptorSetLayoutCreateInfo LayoutDescription{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    LayoutDescription.bindingCount = 1u;
    LayoutDescription.pBindings = &DescriptorDescription;
    if (vkCreateDescriptorSetLayout(Device, &LayoutDescription, nullptr, &Layout) != VK_SUCCESS) return false;
    VkDescriptorPoolSize Size{ VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1u };
    VkDescriptorPoolCreateInfo PoolDescription{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    PoolDescription.maxSets = 1u;
    PoolDescription.poolSizeCount = 1u;
    PoolDescription.pPoolSizes = &Size;
    if (vkCreateDescriptorPool(Device, &PoolDescription, nullptr, &DescriptorPool) != VK_SUCCESS) return false;
    VkDescriptorSetAllocateInfo Allocation{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    Allocation.descriptorPool = DescriptorPool;
    Allocation.descriptorSetCount = 1u;
    Allocation.pSetLayouts = &Layout;
    return vkAllocateDescriptorSets(Device, &Allocation, &Descriptor) == VK_SUCCESS;
}

VkAccelerationStructureGeometryKHR RayQueryExchange::PlacementGeometry() const
{
    VkAccelerationStructureGeometryKHR Geometry{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
    Geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    Geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    Geometry.geometry.instances.data.deviceAddress = Placements.Address;
    return Geometry;
}

void RayQueryExchange::RecordTopLevel(VkCommandBuffer Command, bool Refit)
{
    const auto Geometry = PlacementGeometry();
    VkAccelerationStructureBuildGeometryInfoKHR Build{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
    Build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    Build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    Build.mode = Refit ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    Build.srcAccelerationStructure = Refit ? TopLevel : VK_NULL_HANDLE;
    Build.dstAccelerationStructure = TopLevel;
    Build.geometryCount = 1u;
    Build.pGeometries = &Geometry;
    Build.scratchData.deviceAddress = ScratchAddress;
    VkAccelerationStructureBuildRangeInfoKHR Range{ static_cast<uint32_t>(PendingPlacements.size()), 0u, 0u, 0u };
    const auto* Span = &Range;
    BuildAcceleration(Command, 1u, &Build, &Span);
    VkMemoryBarrier Visible{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    Visible.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    Visible.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(Command, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0u, 1u, &Visible, 0u, nullptr, 0u, nullptr);
}

bool RayQueryExchange::ConstructScene(const SceneStructure& Scene)
{
    if (!Device || !Descriptor || Scene.QueryInstances().empty()) return false;
    if (vkDeviceWaitIdle(Device) != VK_SUCCESS) return false;
    RetireScene();
    const auto& Rows = Scene.QueryInstances();
    const auto& Points = Scene.QueryVertices();
    const auto& Corners = Scene.QueryIndices();
    if (Rows.size() > 0xFFFFFFu || Points.empty() || Corners.empty()) return false;
    const VkBufferUsageFlags Input = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    if (!Allocate(Vertices, Points.size() * sizeof(VertexRecord), Input, Points.data()) ||
        !Allocate(Indices, Corners.size() * sizeof(uint32_t), Input, Corners.data())) return false;
    BottomLevels.resize(Rows.size(), VK_NULL_HANDLE);
    BottomStorage.resize(Rows.size());
    PendingPlacements.resize(Rows.size());
    std::vector<VkAccelerationStructureGeometryKHR> Geometry(Rows.size());
    std::vector<VkAccelerationStructureBuildGeometryInfoKHR> Builds(Rows.size());
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> Ranges(Rows.size());
    VkDeviceSize ScratchBytes = 0u;
    for (uint32_t Slot = 0u; Slot < Rows.size(); ++Slot)
    {
        const auto& Row = Rows[Slot];
        if (!Row.TriangleCount || Row.FirstIndex > Corners.size() || Row.TriangleCount > (Corners.size() - Row.FirstIndex) / 3u) return false;
        uint32_t LastVertex = 0u;
        for (uint32_t Index = 0u; Index < Row.TriangleCount * 3u; ++Index)
            LastVertex = std::max(LastVertex, Corners[Row.FirstIndex + Index]);
        if (Row.VertexOffset >= Points.size() || LastVertex >= Points.size() - Row.VertexOffset) return false;
        auto& Facets = Geometry[Slot];
        Facets.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        Facets.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        Facets.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        auto& Triangle = Facets.geometry.triangles;
        Triangle.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        Triangle.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        Triangle.vertexData.deviceAddress = Vertices.Address + Row.VertexOffset * sizeof(VertexRecord);
        Triangle.vertexStride = sizeof(VertexRecord);
        Triangle.maxVertex = LastVertex;
        Triangle.indexType = VK_INDEX_TYPE_UINT32;
        Triangle.indexData.deviceAddress = Indices.Address + Row.FirstIndex * sizeof(uint32_t);
        auto& Build = Builds[Slot];
        Build.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        Build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        Build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        Build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        Build.geometryCount = 1u;
        Build.pGeometries = &Facets;
        VkAccelerationStructureBuildSizesInfoKHR Sizes{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
        BuildSizes(Device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &Build, &Row.TriangleCount, &Sizes);
        if (!Allocate(BottomStorage[Slot], Sizes.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR)) return false;
        VkAccelerationStructureCreateInfoKHR Description{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
        Description.buffer = BottomStorage[Slot].Buffer;
        Description.size = Sizes.accelerationStructureSize;
        Description.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        if (CreateAcceleration(Device, &Description, nullptr, &BottomLevels[Slot]) != VK_SUCCESS) return false;
        Build.dstAccelerationStructure = BottomLevels[Slot];
        Ranges[Slot].primitiveCount = Row.TriangleCount;
        ScratchBytes = std::max(ScratchBytes, Sizes.buildScratchSize);
        VkAccelerationStructureDeviceAddressInfoKHR Address{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
        Address.accelerationStructure = BottomLevels[Slot];
        auto& Placement = PendingPlacements[Slot];
        Placement.instanceCustomIndex = Slot;
        Placement.mask = 0xFFu;
        Placement.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        Placement.accelerationStructureReference = AccelerationAddress(Device, &Address);
        for (uint32_t RowIndex = 0u; RowIndex < 3u; ++RowIndex)
            for (uint32_t Column = 0u; Column < 4u; ++Column)
                Placement.transform.matrix[RowIndex][Column] = Row.World[Column * 4u + RowIndex];
    }
    if (!Allocate(Placements, PendingPlacements.size() * sizeof(VkAccelerationStructureInstanceKHR),
                  Input | VK_BUFFER_USAGE_TRANSFER_DST_BIT, PendingPlacements.data())) return false;
    const auto TopGeometry = PlacementGeometry();
    VkAccelerationStructureBuildGeometryInfoKHR TopBuild{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
    TopBuild.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    TopBuild.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    TopBuild.geometryCount = 1u;
    TopBuild.pGeometries = &TopGeometry;
    const uint32_t Count = static_cast<uint32_t>(Rows.size());
    VkAccelerationStructureBuildSizesInfoKHR Sizes{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    BuildSizes(Device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &TopBuild, &Count, &Sizes);
    ScratchBytes = std::max({ ScratchBytes, Sizes.buildScratchSize, Sizes.updateScratchSize });
    if (!Allocate(TopStorage, Sizes.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR) ||
        !Allocate(Scratch, ScratchBytes + ScratchAlignment, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) return false;
    ScratchAddress = (Scratch.Address + ScratchAlignment - 1u) / ScratchAlignment * ScratchAlignment;
    VkAccelerationStructureCreateInfoKHR Description{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
    Description.buffer = TopStorage.Buffer;
    Description.size = Sizes.accelerationStructureSize;
    Description.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    if (CreateAcceleration(Device, &Description, nullptr, &TopLevel) != VK_SUCCESS) return false;
    VkCommandBufferAllocateInfo Allocation{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    Allocation.commandPool = Commands;
    Allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    Allocation.commandBufferCount = 1u;
    VkCommandBuffer Command = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(Device, &Allocation, &Command) != VK_SUCCESS) return false;
    VkCommandBufferBeginInfo Begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    Begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(Command, &Begin) != VK_SUCCESS) { vkFreeCommandBuffers(Device, Commands, 1u, &Command); return false; }
    for (uint32_t Slot = 0u; Slot < Rows.size(); ++Slot)
    {
        Builds[Slot].scratchData.deviceAddress = ScratchAddress;
        const auto* Range = &Ranges[Slot];
        BuildAcceleration(Command, 1u, &Builds[Slot], &Range);
        VkMemoryBarrier Serial{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        Serial.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        Serial.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        vkCmdPipelineBarrier(Command, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                             VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0u, 1u, &Serial, 0u, nullptr, 0u, nullptr);
    }
    RecordTopLevel(Command, false);
    const VkResult Sealed = vkEndCommandBuffer(Command);
    VkSubmitInfo Submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    Submit.commandBufferCount = 1u;
    Submit.pCommandBuffers = &Command;
    const bool Submitted = Sealed == VK_SUCCESS && vkQueueSubmit(Queue, 1u, &Submit, VK_NULL_HANDLE) == VK_SUCCESS;
    const bool Completed = Submitted && vkQueueWaitIdle(Queue) == VK_SUCCESS;
    vkFreeCommandBuffers(Device, Commands, 1u, &Command);
    if (!Completed) return false;
    VkWriteDescriptorSetAccelerationStructureKHR Acceleration{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
    Acceleration.accelerationStructureCount = 1u;
    Acceleration.pAccelerationStructures = &TopLevel;
    VkWriteDescriptorSet Write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    Write.pNext = &Acceleration;
    Write.dstSet = Descriptor;
    Write.descriptorCount = 1u;
    Write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    vkUpdateDescriptorSets(Device, 1u, &Write, 0u, nullptr);
    SceneReady = true;
    return true;
}

bool RayQueryExchange::RefreshPlacements(const InstanceRecord* Rows, uint32_t Count)
{
    if (!SceneReady || !Rows || Count != PendingPlacements.size()) return false;
    for (uint32_t Slot = 0u; Slot < Count; ++Slot)
        for (uint32_t Row = 0u; Row < 3u; ++Row)
            for (uint32_t Column = 0u; Column < 4u; ++Column)
                PendingPlacements[Slot].transform.matrix[Row][Column] = Rows[Slot].World[Column * 4u + Row];
    PlacementsPending = true;
    return true;
}

void RayQueryExchange::RecordRefit(VkCommandBuffer Command)
{
    if (!SceneReady || !PlacementsPending) return;
    VkMemoryBarrier Before{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    Before.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    Before.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    vkCmdPipelineBarrier(Command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         0u, 1u, &Before, 0u, nullptr, 0u, nullptr);
    const VkDeviceSize Bytes = PendingPlacements.size() * sizeof(VkAccelerationStructureInstanceKHR);
    const auto* Source = reinterpret_cast<const unsigned char*>(PendingPlacements.data());
    for (VkDeviceSize Offset = 0u; Offset < Bytes; Offset += 65536u)
        vkCmdUpdateBuffer(Command, Placements.Buffer, Offset, std::min<VkDeviceSize>(65536u, Bytes - Offset), Source + Offset);
    VkMemoryBarrier Uploaded{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    Uploaded.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    Uploaded.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(Command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         0u, 1u, &Uploaded, 0u, nullptr, 0u, nullptr);
    RecordTopLevel(Command, true);
    PlacementsPending = false;
}
}
