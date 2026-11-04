/*==========================================================================================================================================
                                                            DEVICEEXCHANGE.H
==========================================================================================================================================*/
// 🧩 Vulkan raster + compute device exchange with WebGPU ordering semantics, for the 1:1 VehicleHMI illumination port.

#pragma once

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>

#include "VulkanDeviceAccess.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace Frontier
{

// 📝 The demo (DeviceExchange.js / ShaderInterchange.js) speaks WebGPU. This exchange gives the C++ port the same
//    vocabulary on Vulkan so every host module ports statement for statement:
//    ① descriptor layouts are declared as ordered resource codes (slot = position in the list);
//    ② queue writes (WriteRegion / WriteTexture) land before the next submission, exactly like GPUQueue.writeBuffer;
//    ③ every dispatch, raster sequence and copy is separated from earlier work by a full memory dependency — the
//       conservative form of WebGPU's implicit usage-scope synchronisation;
//    ④ every image lives in VK_IMAGE_LAYOUT_GENERAL, and raster viewports are y-flipped (negative height) so clip
//       space keeps WebGPU's y-up convention, as Dawn's Vulkan backend does.

//------------------------------------------------------------------------------------------------------------------------
//                                                   RESOURCE CODES
//------------------------------------------------------------------------------------------------------------------------

enum class DescriptorCode : uint8_t
{
    Uniform,
    UniformDynamic,
    StorageRead,
    Storage,
    Texture2D,
    Texture2DUnfiltered,
    Texture2DLayered,
    Texture3D,
    TextureUint2D,
    TextureDepth2D,
    TextureDepth2DLayered,
    StorageTexture2D,
    StorageTexture3D,
    Sampler,
    SamplerUnfiltered,
    SamplerComparison
};

enum class TextureFormat : uint8_t
{
    Rgba16Float,
    Rg16Float,
    R16Float,
    R32Float,
    Rgba32Float,
    Rgba8Unorm,
    Rgba8UnormSrgb,
    R8Unorm,
    R32Uint,
    Rg32Uint,
    Depth32Float
};

enum class TextureDimension : uint8_t
{
    Planar,      // 2D (optionally layered)
    Volumetric   // 3D
};

enum class ViewDimension : uint8_t
{
    Planar,
    PlanarLayered,
    Volumetric
};

namespace TextureUsage
{
constexpr uint32_t CopySource      = 1u;
constexpr uint32_t CopyTarget      = 2u;
constexpr uint32_t Sampled         = 4u;
constexpr uint32_t Storage         = 8u;
constexpr uint32_t RenderAttachment = 16u;
} // namespace TextureUsage

[[nodiscard]] VkFormat TranslateTextureFormat(TextureFormat Format) noexcept;
[[nodiscard]] uint32_t QueryTexelBytes(TextureFormat Format) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                   RESOURCES
//------------------------------------------------------------------------------------------------------------------------

class DeviceExchange;

// 📝 A device memory region — the demo's "records" (GPUBuffer). Always device local, usable for every role.
struct DeviceRegion
{
    DeviceExchange* Exchange = nullptr;
    VkBuffer        Handle   = VK_NULL_HANDLE;
    VkDeviceMemory  Memory   = VK_NULL_HANDLE;
    VkDeviceSize    Capacity = 0;   // [B]
    std::string     Label{};

    ~DeviceRegion();
};

struct TextureSpecification
{
    std::string      Label{};
    uint32_t         Width     = 1;                         // [texel]
    uint32_t         Height    = 1;                         // [texel]
    uint32_t         DepthOrLayers = 1;                     // [texel] volume depth, or array layer count
    uint32_t         Levels    = 1;                         // [-] mip levels
    TextureDimension Dimension = TextureDimension::Planar;
    TextureFormat    Format    = TextureFormat::Rgba16Float;
    uint32_t         Usage     = TextureUsage::Sampled;
    std::vector<TextureFormat> ViewFormats{};               // [-] extra view formats (srgb ↔ unorm)
};

struct DeviceTexture
{
    DeviceExchange*      Exchange = nullptr;
    VkImage              Image    = VK_NULL_HANDLE;
    VkDeviceMemory       Memory   = VK_NULL_HANDLE;
    TextureSpecification Specification{};
    bool                 Owned    = true;   // [-] false: an image another exchange allocated (WrapTexture)

    ~DeviceTexture();
};

struct TextureViewSpecification
{
    ViewDimension Dimension   = ViewDimension::Planar;
    bool          DimensionSet = false;
    TextureFormat Format      = TextureFormat::Rgba16Float;
    bool          FormatSet   = false;
    uint32_t      BaseLayer   = 0;
    uint32_t      LayerCount  = 0;   // 0 = all remaining
    uint32_t      BaseLevel   = 0;
    uint32_t      LevelCount  = 0;   // 0 = all remaining
};

struct DeviceTextureView
{
    DeviceExchange*               Exchange = nullptr;
    VkImageView                   Handle   = VK_NULL_HANDLE;
    std::shared_ptr<DeviceTexture> Texture{};
    ViewDimension                 Dimension = ViewDimension::Planar;
    TextureFormat                 Format    = TextureFormat::Rgba16Float;
    uint32_t                      BaseLayer = 0;
    uint32_t                      LayerCount = 1;
    uint32_t                      BaseLevel = 0;
    uint32_t                      LevelCount = 1;

    ~DeviceTextureView();
};

enum class FilterMode : uint8_t { Nearest, Linear };
enum class AddressMode : uint8_t { ClampToEdge, Repeat, MirrorRepeat };
enum class CompareFunction : uint8_t { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always, Disabled };

struct SamplerSpecification
{
    FilterMode      MagFilter    = FilterMode::Nearest;
    FilterMode      MinFilter    = FilterMode::Nearest;
    FilterMode      MipmapFilter = FilterMode::Nearest;
    AddressMode     AddressU     = AddressMode::ClampToEdge;
    AddressMode     AddressV     = AddressMode::ClampToEdge;
    AddressMode     AddressW     = AddressMode::ClampToEdge;
    float           LodMinimum   = 0.0f;
    float           LodMaximum   = 32.0f;
    CompareFunction Compare      = CompareFunction::Disabled;
};

struct DeviceSampler
{
    DeviceExchange* Exchange = nullptr;
    VkSampler       Handle   = VK_NULL_HANDLE;

    ~DeviceSampler();
};

struct DescriptorSlot
{
    DescriptorCode Code          = DescriptorCode::Uniform;
    TextureFormat  StorageFormat = TextureFormat::Rgba16Float;   // storage textures only
};

struct DescriptorLayout
{
    DeviceExchange*             Exchange = nullptr;
    VkDescriptorSetLayout       Handle   = VK_NULL_HANDLE;
    std::vector<DescriptorSlot> Slots{};
    std::string                 Label{};

    ~DescriptorLayout();
};

// 📝 One bound resource of a descriptor group: a region (whole or a window), a texture view, or a sampler.
struct DescriptorResource
{
    std::shared_ptr<DeviceRegion>      Region{};
    VkDeviceSize                       Offset = 0;
    VkDeviceSize                       Size   = VK_WHOLE_SIZE;
    std::shared_ptr<DeviceTextureView> View{};
    std::shared_ptr<DeviceSampler>     Sampler{};

    DescriptorResource() = default;
    DescriptorResource(std::shared_ptr<DeviceRegion> RegionInstance) : Region(std::move(RegionInstance)) {}
    DescriptorResource(std::shared_ptr<DeviceRegion> RegionInstance, VkDeviceSize WindowOffset, VkDeviceSize WindowSize)
        : Region(std::move(RegionInstance)), Offset(WindowOffset), Size(WindowSize) {}
    DescriptorResource(std::shared_ptr<DeviceTextureView> ViewInstance) : View(std::move(ViewInstance)) {}
    DescriptorResource(std::shared_ptr<DeviceSampler> SamplerInstance) : Sampler(std::move(SamplerInstance)) {}
};

struct DescriptorGroup
{
    DeviceExchange*                   Exchange = nullptr;
    VkDescriptorSet                   Handle   = VK_NULL_HANDLE;
    std::shared_ptr<DescriptorLayout> Layout{};
    std::vector<DescriptorResource>   Resources{};   // [-] retained so the bound resources outlive the group
    std::string                       Label{};

    ~DescriptorGroup();
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   PROGRAMS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One specialization constant: the constant_id is the position in the list handed to the program constructor.
struct SpecializationConstant
{
    enum class Category : uint8_t { Unsigned, Signed, Float } Kind = Category::Unsigned;
    uint32_t Bits = 0;

    static SpecializationConstant FromUnsigned(uint32_t Scalar) { return {Category::Unsigned, Scalar}; }
    static SpecializationConstant FromFloat(float Scalar);
};

struct DeviceProgram
{
    DeviceExchange*                                Exchange = nullptr;
    VkPipeline                                     Pipeline = VK_NULL_HANDLE;
    VkPipelineLayout                               Layout   = VK_NULL_HANDLE;
    VkPipelineBindPoint                            BindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
    std::vector<std::shared_ptr<DescriptorLayout>> Layouts{};
    std::string                                    Label{};

    ~DeviceProgram();
};

enum class VertexFormat : uint8_t { Float32x2, Float32x3, Float32x4, Uint32, Uint32x2, Uint32x4 };

struct VertexAttribute
{
    uint32_t     Location = 0;
    uint32_t     Offset   = 0;   // [B]
    VertexFormat Format   = VertexFormat::Float32x3;
};

struct VertexLayout
{
    uint32_t                     Stride   = 0;       // [B]
    bool                         Instance = false;   // [-] step per instance instead of per vertex
    std::vector<VertexAttribute> Attributes{};
};

enum class BlendFactor : uint8_t { Zero, One, SourceAlpha, OneMinusSourceAlpha, DestinationAlpha, OneMinusDestinationAlpha,
                                   Source, OneMinusSource, Destination, OneMinusDestination };
enum class BlendOperation : uint8_t { Add, Subtract, ReverseSubtract, Minimum, Maximum };

struct BlendComponent
{
    BlendOperation Operation   = BlendOperation::Add;
    BlendFactor    Source      = BlendFactor::One;
    BlendFactor    Destination = BlendFactor::Zero;
};

struct ColorTarget
{
    TextureFormat  Format       = TextureFormat::Rgba16Float;
    bool           BlendEnabled = false;
    BlendComponent Color{};
    BlendComponent Alpha{};
    uint32_t       WriteMask    = 0xFu;
};

struct RasterProgramSpecification
{
    std::string                  Label{};
    std::string                  VertexSpirvPath{};
    std::string                  VertexEntry{};
    std::string                  FragmentSpirvPath{};   // empty = depth-only
    std::string                  FragmentEntry{};
    std::vector<VertexLayout>    VertexLayouts{};
    VkPrimitiveTopology          Topology      = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkCullModeFlags              CullMode      = VK_CULL_MODE_NONE;
    VkFrontFace                  FrontFace     = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    bool                         DepthEnabled  = false;
    TextureFormat                DepthFormat   = TextureFormat::Depth32Float;
    bool                         DepthWrite    = false;
    CompareFunction              DepthCompare  = CompareFunction::Always;
    float                        DepthBias     = 0.0f;
    float                        DepthBiasSlope = 0.0f;
    float                        DepthBiasClamp = 0.0f;
    bool                         UnclippedDepth = false;
    std::vector<ColorTarget>     Targets{};
    std::vector<std::shared_ptr<DescriptorLayout>> Layouts{};
    std::vector<SpecializationConstant> Specialization{};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   COMMAND SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

enum class LoadOperation : uint8_t { Load, Clear };

struct ColorAttachment
{
    std::shared_ptr<DeviceTextureView> View{};
    LoadOperation                      Load  = LoadOperation::Clear;
    std::array<float, 4>               Clear = {0.0f, 0.0f, 0.0f, 0.0f};
};

struct DepthAttachment
{
    std::shared_ptr<DeviceTextureView> View{};
    LoadOperation                      Load  = LoadOperation::Clear;
    float                              Clear = 0.0f;
};

class CommandSequence
{
  public:
    explicit CommandSequence(DeviceExchange& Exchange, VkCommandBuffer CommandStream);

    // Compute: a dispatch is ordered after all earlier work (WebGPU dispatch usage scopes).
    void BindProgram(const std::shared_ptr<DeviceProgram>& Program);
    void BindGroup(uint32_t Index, const std::shared_ptr<DescriptorGroup>& Group, std::span<const uint32_t> DynamicOffsets = {});
    void Dispatch(uint32_t GroupCountX, uint32_t GroupCountY = 1u, uint32_t GroupCountZ = 1u);

    // Raster: one dynamic-rendering scope; draws inside it are ordered by the rasterizer as in a WebGPU render pass.
    void BeginRaster(std::span<const ColorAttachment> Colors, const DepthAttachment* Depth, const std::string& Label = "");
    void BindVertexRegion(uint32_t Slot, const std::shared_ptr<DeviceRegion>& Region, VkDeviceSize Offset = 0);
    void BindIndexRegion(const std::shared_ptr<DeviceRegion>& Region, VkIndexType IndexType = VK_INDEX_TYPE_UINT32);
    void Draw(uint32_t VertexCount, uint32_t InstanceCount = 1u, uint32_t FirstVertex = 0u, uint32_t FirstInstance = 0u);
    void DrawIndexedIndirect(const std::shared_ptr<DeviceRegion>& Region, VkDeviceSize Offset);
    // 📝 Many commands in one call: the count read from CountRegion (drawIndirectCount), or a fixed series (multiDrawIndirect).
    void DrawIndexedIndirectCount(const std::shared_ptr<DeviceRegion>& Region, VkDeviceSize Offset, const std::shared_ptr<DeviceRegion>& CountRegion,
                                  VkDeviceSize CountOffset, uint32_t MaximumDraws, uint32_t Stride = 20);
    void DrawIndexedIndirectSeries(const std::shared_ptr<DeviceRegion>& Region, VkDeviceSize Offset, uint32_t DrawCount, uint32_t Stride = 20);
    void SetViewport(float X, float Y, float Width, float Height, float MinimumDepth = 0.0f, float MaximumDepth = 1.0f);
    void SetScissor(uint32_t X, uint32_t Y, uint32_t Width, uint32_t Height);
    void EndRaster();

    void CopyRegion(const std::shared_ptr<DeviceRegion>& Source, VkDeviceSize SourceOffset,
                    const std::shared_ptr<DeviceRegion>& Target, VkDeviceSize TargetOffset, VkDeviceSize ByteCount);
    void ClearTexture(const std::shared_ptr<DeviceTexture>& Texture, std::array<float, 4> Colour);

    [[nodiscard]] VkCommandBuffer Stream() const noexcept { return CommandStream_; }
    void OrderAfterEarlierWork();

  private:
    DeviceExchange& Exchange_;
    VkCommandBuffer CommandStream_ = VK_NULL_HANDLE;
    VkPipelineBindPoint BoundPoint_  = VK_PIPELINE_BIND_POINT_COMPUTE;
    VkPipelineLayout    BoundLayout_ = VK_NULL_HANDLE;
    bool            InsideRaster_  = false;
    uint32_t        RasterWidth_   = 0;
    uint32_t        RasterHeight_  = 0;
    std::vector<std::shared_ptr<void>> Retained_{};   // [-] resources referenced until the submission completes
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   DEVICE EXCHANGE
//------------------------------------------------------------------------------------------------------------------------

struct DeviceCapabilities
{
    bool DrawIndirectCount = false;   // [-] CommandSequence::DrawIndexedIndirectCount is available
    bool MultiDrawIndirect = false;   // [-] CommandSequence::DrawIndexedIndirectSeries is available
};
struct DeviceLimitsSummary
{
    uint32_t MaximumTexture3D        = 0;
    uint32_t MaximumTexture2D        = 0;
    uint32_t MaximumComputeGroups    = 0;
    uint32_t MinimumUniformAlignment = 256;
    uint32_t MinimumStorageAlignment = 256;
};

class DeviceExchange
{
  public:
    DeviceExchange();
    ~DeviceExchange();

    DeviceExchange(const DeviceExchange&)            = delete;
    DeviceExchange& operator=(const DeviceExchange&) = delete;

    // Loads the Vulkan loader (explicit path, FRONTIER_VULKAN_DRIVER, or the system libvulkan) and opens one queue.
    bool Initialize(const std::string& ExplicitDriverLibraryPath = "", std::string* OutDiagnostic = nullptr);
    // 📝 Borrows a Vulkan 1.3 device another owner created with dynamicRendering enabled (the editor's presentation device);
    //    Shutdown then leaves that device and instance alive.
    bool AttachDevice(const VulkanDeviceAccess& Access, std::string* OutDiagnostic = nullptr);
    void Shutdown() noexcept;
    // 📝 This exchange's own device as another exchange can borrow it (same queue, submissions ordered on it).
    [[nodiscard]] VulkanDeviceAccess QueryDeviceAccess() const noexcept;

    [[nodiscard]] bool                       Ready() const noexcept { return Device_ != VK_NULL_HANDLE; }
    [[nodiscard]] const std::string&         AdapterDescription() const noexcept { return AdapterDescription_; }
    [[nodiscard]] const DeviceLimitsSummary& Limits() const noexcept { return Limits_; }
    [[nodiscard]] const DeviceCapabilities&  Capabilities() const noexcept { return Capabilities_; }

    // Resources (the demo's ConstructRecords / ConstructRecordsFrom / ConstructTexture / ConstructSampler).
    std::shared_ptr<DeviceRegion>      ConstructRegion(VkDeviceSize ByteCount, const std::string& Label = "");
    std::shared_ptr<DeviceRegion>      ConstructRegionFrom(const void* Bytes, VkDeviceSize ByteCount, const std::string& Label = "");
    std::shared_ptr<DeviceTexture>     ConstructTexture(const TextureSpecification& Specification);
    // 📝 Wraps an image of the same device that another exchange allocated; it must already be in VK_IMAGE_LAYOUT_GENERAL.
    std::shared_ptr<DeviceTexture>     WrapTexture(VkImage Image, const TextureSpecification& Specification);
    std::shared_ptr<DeviceTextureView> ConstructView(const std::shared_ptr<DeviceTexture>& Texture,
                                                     const TextureViewSpecification& Specification = {});
    std::shared_ptr<DeviceSampler>     ConstructSampler(const SamplerSpecification& Specification);

    std::shared_ptr<DescriptorLayout> ConstructDescriptorLayout(std::span<const DescriptorSlot> Slots, const std::string& Label = "");
    std::shared_ptr<DescriptorGroup>  ConstructDescriptorGroup(const std::shared_ptr<DescriptorLayout>& Layout,
                                                               std::vector<DescriptorResource> Resources, const std::string& Label = "");

    std::shared_ptr<DeviceProgram> ConstructComputeProgram(const std::string& SpirvPath, const std::string& EntryPoint,
                                                           std::vector<std::shared_ptr<DescriptorLayout>> Layouts,
                                                           std::span<const SpecializationConstant> Specialization = {},
                                                           std::string* OutDiagnostic = nullptr);
    std::shared_ptr<DeviceProgram> ConstructRasterProgram(const RasterProgramSpecification& Specification,
                                                          std::string* OutDiagnostic = nullptr);

    // Queue writes: staged now, executed at the head of the next submission (GPUQueue.writeBuffer / writeTexture).
    void WriteRegion(const std::shared_ptr<DeviceRegion>& Region, VkDeviceSize Offset, const void* Bytes, VkDeviceSize ByteCount);
    void WriteTexture(const std::shared_ptr<DeviceTexture>& Texture, uint32_t Layer, const void* Bytes, VkDeviceSize ByteCount,
                      uint32_t Width, uint32_t Height, uint32_t Depth = 1u, uint32_t OriginX = 0u, uint32_t OriginY = 0u,
                      uint32_t OriginZ = 0u, uint32_t Level = 0u);

    // 🔍 Validation hook: sees every queue write (region label, offset, bytes) as it is staged.
    std::function<void(const std::string&, VkDeviceSize, const void*, VkDeviceSize)> WriteObserver{};

    // Recording + submission. Submit waits for completion (one submission in flight; WebGPU onSubmittedWorkDone).
    CommandSequence BeginSequence();
    bool            Submit(CommandSequence& Sequence, std::string* OutDiagnostic = nullptr);
    // 📝 Several sequences behind one set of staged writes — the work of one submission — each submitted and awaited on its
    //    own under its label, so a device fault names the sequence it struck.
    bool            SubmitSeries(std::span<CommandSequence* const> Series, std::span<const std::string> Labels,
                                 std::string* OutDiagnostic = nullptr);
    bool            FlushWrites(std::string* OutDiagnostic = nullptr);
    // 📝 Waits until the queue is idle (every submission, including a borrowing owner's), before replacing sampled targets.
    void            AwaitIdle() noexcept;
    // 📝 VK_SUCCESS while the queue answers; VK_ERROR_DEVICE_LOST once any owner of the device lost it.
    VkResult        ProbeDevice() noexcept;

    // 🔍 Names the work of the following submissions in failure reports ("distance volume 12 of 340, slab 3").
    void LabelSubmissions(std::string Label) { SubmissionLabel_ = std::move(Label); }
    // 🔍 Every rejected queue write or copy and every failed submission, oldest first (empty while healthy).
    [[nodiscard]] const std::vector<std::string>& Faults() const noexcept { return Faults_; }
    // 🔍 Sees each fault as it is recorded (the editor journals them).
    std::function<void(const std::string&)> FaultObserver{};
    // 🔍 Label and wall time of every awaited submission, from vkQueueSubmit to its fence (stage costs on any driver when each stage
    //    is a submission of its own, FRONTIER_ILLUMINATION_STAGED_SUBMISSIONS=1); unset = not measured.
    std::function<void(const std::string&, double)> SubmissionTiming{};
    [[nodiscard]] uint64_t CompletedSubmissions() const noexcept { return CompletedSubmissions_; }
    void RecordFault(const std::string& Fault);

    // Readback for validation captures.
    bool ReadRegion(const std::shared_ptr<DeviceRegion>& Region, VkDeviceSize Offset, VkDeviceSize ByteCount, std::vector<uint8_t>& OutBytes);
    bool ReadTexture(const std::shared_ptr<DeviceTexture>& Texture, uint32_t Layer, uint32_t Level, std::vector<uint8_t>& OutBytes);

    // Internal access for the resource destructors and the command sequence.
    void ReleaseRegion(DeviceRegion& Region) noexcept;
    void ReleaseTexture(DeviceTexture& Texture) noexcept;
    void ReleaseView(DeviceTextureView& View) noexcept;
    void ReleaseSampler(DeviceSampler& Sampler) noexcept;
    void ReleaseDescriptorLayout(DescriptorLayout& Layout) noexcept;
    void ReleaseDescriptorGroup(DescriptorGroup& Group) noexcept;
    void ReleaseProgram(DeviceProgram& Program) noexcept;

    struct ResolvedFunctions;
    [[nodiscard]] const ResolvedFunctions& Functions() const noexcept { return *Functions_; }

  private:
    bool LoadShaderUnit(const std::string& SpirvPath, VkShaderModule& OutUnit, std::string* OutDiagnostic);
    uint32_t DescribePhysicalDevice();
    bool CompleteDevice(std::string* OutDiagnostic);
    uint32_t SelectMemoryType(uint32_t TypeBits, VkMemoryPropertyFlags Required) const;
    bool EnsureStaging(VkDeviceSize ByteCount);
    void TransitionToGeneral(VkCommandBuffer Stream, DeviceTexture& Texture);
    bool ExecuteImmediately(const std::function<void(VkCommandBuffer)>& Record, std::string* OutFailure = nullptr);
    std::string DescribeSubmissionPoint() const;

    struct PendingRegionWrite
    {
        std::shared_ptr<DeviceRegion> Region{};
        VkDeviceSize                  TargetOffset  = 0;
        VkDeviceSize                  StagingOffset = 0;
        VkDeviceSize                  ByteCount     = 0;
    };
    struct PendingTextureWrite
    {
        std::shared_ptr<DeviceTexture> Texture{};
        VkDeviceSize                   StagingOffset = 0;
        VkBufferImageCopy              Copy{};
    };

    void*                            LibraryHandle_      = nullptr;
    std::unique_ptr<ResolvedFunctions>   Functions_{};
    VkInstance                       Instance_           = VK_NULL_HANDLE;
    VkPhysicalDevice                 PhysicalDevice_     = VK_NULL_HANDLE;
    VkDevice                         Device_             = VK_NULL_HANDLE;
    VkQueue                          Queue_              = VK_NULL_HANDLE;
    uint32_t                         QueueFamily_        = 0u;
    VkCommandPool                    CommandPool_        = VK_NULL_HANDLE;
    VkDescriptorPool                 DescriptorPool_     = VK_NULL_HANDLE;
    VkFence                          SubmitFence_        = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties MemoryProperties_{};
    DeviceLimitsSummary              Limits_{};
    DeviceCapabilities               Capabilities_{};
    std::string                      AdapterDescription_{};
    bool                             DeviceAttached_     = false;   // [-] the device belongs to another owner (AttachDevice)

    VkBuffer                         StagingHandle_      = VK_NULL_HANDLE;
    VkDeviceMemory                   StagingMemory_      = VK_NULL_HANDLE;
    VkDeviceSize                     StagingCapacity_    = 0;
    VkDeviceSize                     StagingCursor_      = 0;
    uint8_t*                         StagingBytes_       = nullptr;
    std::vector<PendingRegionWrite>  PendingRegions_{};
    std::vector<PendingTextureWrite> PendingTextures_{};
    std::vector<std::shared_ptr<DeviceTexture>> PendingTransitions_{};
    std::vector<VkCommandBuffer>     ActiveStreams_{};

    std::string                      SubmissionLabel_{};          // [-] the work the next submission carries
    std::string                      CompletedLabel_{};           // [-] the work of the last submission that completed
    uint64_t                         CompletedSubmissions_ = 0;   // [-] submissions whose fence signalled
    std::vector<std::string>         Faults_{};
};

} // namespace Frontier
