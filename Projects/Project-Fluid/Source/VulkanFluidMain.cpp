#define GLFW_INCLUDE_NONE
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include "PbfFluid.h"
#include "SurfaceReconstruction.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace PF = Frontier::ProjectFluid;

namespace {
constexpr std::uint32_t InitialWidth = 1280;
constexpr std::uint32_t InitialHeight = 720;

[[noreturn]] void Fail(const std::string& message) { throw std::runtime_error(message); }
void VkCheck(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) Fail(std::string(operation) + " failed (VkResult " + std::to_string(result) + ")");
}

std::vector<std::uint32_t> ReadSpirv(const char* path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) Fail(std::string("Cannot open shader: ") + path);
    const auto bytes = stream.tellg();
    if (bytes <= 0 || (bytes % 4) != 0) Fail(std::string("Invalid SPIR-V: ") + path);
    std::vector<std::uint32_t> words(static_cast<std::size_t>(bytes) / 4);
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(words.data()), bytes);
    return words;
}

struct Buffer {
    VkBuffer Handle{VK_NULL_HANDLE};
    VkDeviceMemory Memory{VK_NULL_HANDLE};
    VkDeviceSize Size{};
    void* Mapped{};
};

struct alignas(16) Parameters {
    std::uint32_t ParticleCount{};
    std::uint32_t Width{};
    std::uint32_t Height{};
    std::uint32_t ObstacleEnabled{};
    float Time{};
    float ParticleRadius{0.079f};
    float BoundsX{1.95f};
    float BoundsZ{1.25f};
    float ColourR{0.36f};
    float ColourG{0.82f};
    float ColourB{0.86f};
    std::uint32_t Paused{};
    float Opacity{0.01f};
    float Roughness{0.09f};
    float Ior{1.333f};
    float MaterialIndex{};
    float AbsorptionR{0.85f};
    float AbsorptionG{0.20f};
    float AbsorptionB{0.12f};
    float OpticalPadding{};
};
struct alignas(16) GpuParticle {
    float X{},Y{},Z{},VolumeWeight{};
    float AxisAX{},AxisAY{},AxisAZ{},PaddingA{};
    float AxisBX{},AxisBY{},AxisBZ{},PaddingB{};
    float AxisCX{},AxisCY{},AxisCZ{},PaddingC{};
};
static_assert(sizeof(Parameters) == 80 && sizeof(GpuParticle) == 64);

class FluidApplication final {
public:
    ~FluidApplication() { Shutdown(); }

    void Run() {
        Initialize();
        auto previous = std::chrono::steady_clock::now();
        while (!glfwWindowShouldClose(Window_)) {
            glfwPollEvents();
            const auto now = std::chrono::steady_clock::now();
            const float dt = std::min(0.05f, std::chrono::duration<float>(now - previous).count());
            previous = now;
            // Mapped uniforms and mode data are single-buffered in this proof;
            // do not mutate them until the preceding dispatch has retired.
            vkWaitForFences(Device_, 1, &FrameFence_, VK_TRUE, UINT64_MAX);
            Update(dt);
            Draw();
        }
        vkDeviceWaitIdle(Device_);
    }

private:
    GLFWwindow* Window_{};
    VkInstance Instance_{VK_NULL_HANDLE};
    VkSurfaceKHR WindowSurface_{VK_NULL_HANDLE};
    VkPhysicalDevice Physical_{VK_NULL_HANDLE};
    VkDevice Device_{VK_NULL_HANDLE};
    VkQueue Queue_{VK_NULL_HANDLE};
    std::uint32_t QueueFamily_{};
    VkSwapchainKHR Swapchain_{VK_NULL_HANDLE};
    VkFormat SwapFormat_{VK_FORMAT_UNDEFINED};
    VkExtent2D Extent_{};
    std::vector<VkImage> Images_;
    VkCommandPool CommandPool_{VK_NULL_HANDLE};
    VkCommandBuffer Command_{VK_NULL_HANDLE};
    VkDescriptorSetLayout SetLayout_{VK_NULL_HANDLE};
    VkDescriptorPool DescriptorPool_{VK_NULL_HANDLE};
    VkDescriptorSet ClearSet_{VK_NULL_HANDLE};
    VkDescriptorSet SplatSet_{VK_NULL_HANDLE};
    VkDescriptorSet ResolveSet_{VK_NULL_HANDLE};
    VkPipelineLayout ClearLayout_{VK_NULL_HANDLE};
    VkPipelineLayout SplatLayout_{VK_NULL_HANDLE};
    VkPipelineLayout ResolveLayout_{VK_NULL_HANDLE};
    VkPipeline ClearPipeline_{VK_NULL_HANDLE};
    VkPipeline SplatPipeline_{VK_NULL_HANDLE};
    VkPipeline ResolvePipeline_{VK_NULL_HANDLE};
    VkSemaphore Acquired_{VK_NULL_HANDLE};
    VkSemaphore Rendered_{VK_NULL_HANDLE};
    VkFence FrameFence_{VK_NULL_HANDLE};
    Buffer ParticleBuffer_;
    Buffer DepthBuffer_;
    Buffer ThicknessBuffer_;
    Buffer PixelBuffer_;
    Buffer UniformBuffer_;
    PF::PbfFluid Fluid_;
    PF::SurfaceReconstruction Reconstruction_;
    Parameters Params_{};
    std::vector<GpuParticle> GpuParticles_;
    float StepAccumulator_{};
    bool PauseLatch_{false};
    bool ResetLatch_{false};
    bool StirLatch_{false};
    bool PourLatch_{false};
    bool Pouring_{true};
    std::uint64_t FrameNumber_{};

    std::uint32_t FindMemory(std::uint32_t mask, VkMemoryPropertyFlags wanted) const {
        VkPhysicalDeviceMemoryProperties properties{};
        vkGetPhysicalDeviceMemoryProperties(Physical_, &properties);
        for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i)
            if ((mask & (1u << i)) && (properties.memoryTypes[i].propertyFlags & wanted) == wanted) return i;
        Fail("No compatible Vulkan memory type");
    }

    Buffer MakeBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible) {
        Buffer buffer{};
        buffer.Size = size;
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = size;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkCheck(vkCreateBuffer(Device_, &info, nullptr, &buffer.Handle), "vkCreateBuffer");
        VkMemoryRequirements requirement{};
        vkGetBufferMemoryRequirements(Device_, buffer.Handle, &requirement);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirement.size;
        const VkMemoryPropertyFlags flags = hostVisible
            ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        allocation.memoryTypeIndex = FindMemory(requirement.memoryTypeBits, flags);
        VkCheck(vkAllocateMemory(Device_, &allocation, nullptr, &buffer.Memory), "vkAllocateMemory");
        VkCheck(vkBindBufferMemory(Device_, buffer.Handle, buffer.Memory, 0), "vkBindBufferMemory");
        if (hostVisible) VkCheck(vkMapMemory(Device_, buffer.Memory, 0, size, 0, &buffer.Mapped), "vkMapMemory");
        return buffer;
    }

    void DestroyBuffer(Buffer& buffer) {
        if (!Device_) return;
        if (buffer.Mapped) vkUnmapMemory(Device_, buffer.Memory);
        if (buffer.Handle) vkDestroyBuffer(Device_, buffer.Handle, nullptr);
        if (buffer.Memory) vkFreeMemory(Device_, buffer.Memory, nullptr);
        buffer = {};
    }

    void Initialize() {
        if (!glfwInit()) Fail("GLFW initialization failed");
        if (!glfwVulkanSupported()) Fail("No Vulkan loader/driver is available");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        Window_ = glfwCreateWindow(InitialWidth, InitialHeight, "Frontier — Project Fluid (Vulkan GPU + CPU mirror)", nullptr, nullptr);
        if (!Window_) Fail("Window creation failed");

        std::uint32_t extensionCount = 0;
        const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "Project Fluid";
        app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        app.pEngineName = "Frontier";
        app.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instanceInfo.pApplicationInfo = &app;
        instanceInfo.enabledExtensionCount = extensionCount;
        instanceInfo.ppEnabledExtensionNames = extensions;
        VkCheck(vkCreateInstance(&instanceInfo, nullptr, &Instance_), "vkCreateInstance");
        VkCheck(glfwCreateWindowSurface(Instance_, Window_, nullptr, &WindowSurface_), "glfwCreateWindowSurface");
        PickDevice();
        CreateSwapchain();
        CreateComputeState();

        Params_.Width = Extent_.width;
        Params_.Height = Extent_.height;
        Params_.ObstacleEnabled = 1;
        GpuParticles_.resize(PF::PbfFluid::MaxParticles);
        UploadParticles();
        std::cout << "Project Fluid / Flux controls: Space pause, R reset, S stir, P toggle pour, 1-4 materials, Esc quit\n";
    }

    void PickDevice() {
        std::uint32_t count = 0;
        vkEnumeratePhysicalDevices(Instance_, &count, nullptr);
        std::vector<VkPhysicalDevice> devices(count);
        vkEnumeratePhysicalDevices(Instance_, &count, devices.data());
        for (VkPhysicalDevice candidate : devices) {
            std::uint32_t familyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
            std::vector<VkQueueFamilyProperties> families(familyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
            for (std::uint32_t i = 0; i < familyCount; ++i) {
                VkBool32 present = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, WindowSurface_, &present);
                if (present && (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
                    Physical_ = candidate;
                    QueueFamily_ = i;
                    break;
                }
            }
            if (Physical_) break;
        }
        if (!Physical_) Fail("No Vulkan compute queue can present to this window");
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue.queueFamilyIndex = QueueFamily_;
        queue.queueCount = 1;
        queue.pQueuePriorities = &priority;
        const char* extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        VkDeviceCreateInfo device{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        device.queueCreateInfoCount = 1;
        device.pQueueCreateInfos = &queue;
        device.enabledExtensionCount = 1;
        device.ppEnabledExtensionNames = &extension;
        VkCheck(vkCreateDevice(Physical_, &device, nullptr, &Device_), "vkCreateDevice");
        vkGetDeviceQueue(Device_, QueueFamily_, 0, &Queue_);
    }

    void CreateSwapchain() {
        VkSurfaceCapabilitiesKHR capabilities{};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(Physical_, WindowSurface_, &capabilities);
        std::uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(Physical_, WindowSurface_, &formatCount, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(Physical_, WindowSurface_, &formatCount, formats.data());
        VkSurfaceFormatKHR selected = formats.front();
        for (const auto& format : formats)
            if (format.format == VK_FORMAT_B8G8R8A8_UNORM) selected = format;
        if (selected.format != VK_FORMAT_B8G8R8A8_UNORM && selected.format != VK_FORMAT_B8G8R8A8_SRGB)
            Fail("Project Fluid currently requires a BGRA8 swapchain");
        SwapFormat_ = selected.format;
        Extent_ = capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()
            ? capabilities.currentExtent : VkExtent2D{InitialWidth, InitialHeight};
        const std::uint32_t imageCount = std::clamp(capabilities.minImageCount + 1, capabilities.minImageCount,
            capabilities.maxImageCount ? capabilities.maxImageCount : capabilities.minImageCount + 1);
        VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        info.surface = WindowSurface_;
        info.minImageCount = imageCount;
        info.imageFormat = selected.format;
        info.imageColorSpace = selected.colorSpace;
        info.imageExtent = Extent_;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.preTransform = capabilities.currentTransform;
        info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        info.clipped = VK_TRUE;
        VkCheck(vkCreateSwapchainKHR(Device_, &info, nullptr, &Swapchain_), "vkCreateSwapchainKHR");
        std::uint32_t actual = 0;
        vkGetSwapchainImagesKHR(Device_, Swapchain_, &actual, nullptr);
        Images_.resize(actual);
        vkGetSwapchainImagesKHR(Device_, Swapchain_, &actual, Images_.data());
    }

    VkPipeline MakePipeline(const char* path, VkPipelineLayout layout) {
        const auto code = ReadSpirv(path);
        VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        moduleInfo.codeSize = code.size() * sizeof(std::uint32_t);
        moduleInfo.pCode = code.data();
        VkShaderModule module{};
        VkCheck(vkCreateShaderModule(Device_, &moduleInfo, nullptr, &module), "vkCreateShaderModule");
        VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stage.module = module;
        stage.pName = "main";
        VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pipelineInfo.stage = stage;
        pipelineInfo.layout = layout;
        VkPipeline pipeline{};
        VkCheck(vkCreateComputePipelines(Device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline), "vkCreateComputePipelines");
        vkDestroyShaderModule(Device_, module, nullptr);
        return pipeline;
    }

    void CreateComputeState() {
        ParticleBuffer_ = MakeBuffer(PF::PbfFluid::MaxParticles * sizeof(GpuParticle), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
        const VkDeviceSize pixelBytes = static_cast<VkDeviceSize>(Extent_.width) * Extent_.height * 4;
        DepthBuffer_ = MakeBuffer(pixelBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
        ThicknessBuffer_ = MakeBuffer(pixelBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
        PixelBuffer_ = MakeBuffer(pixelBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, false);
        UniformBuffer_ = MakeBuffer(sizeof(Parameters), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);

        std::array<VkDescriptorSetLayoutBinding, 5> bindings{};
        bindings[0] = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        bindings[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        bindings[2] = {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        bindings[3] = {3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        bindings[4] = {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        setInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
        setInfo.pBindings = bindings.data();
        VkCheck(vkCreateDescriptorSetLayout(Device_, &setInfo, nullptr, &SetLayout_), "vkCreateDescriptorSetLayout");

        VkPipelineLayoutCreateInfo surfaceLayout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        surfaceLayout.setLayoutCount = 1;
        surfaceLayout.pSetLayouts = &SetLayout_;
        VkCheck(vkCreatePipelineLayout(Device_, &surfaceLayout, nullptr, &ClearLayout_), "vkCreatePipelineLayout");
        VkCheck(vkCreatePipelineLayout(Device_, &surfaceLayout, nullptr, &SplatLayout_), "vkCreatePipelineLayout");
        VkCheck(vkCreatePipelineLayout(Device_, &surfaceLayout, nullptr, &ResolveLayout_), "vkCreatePipelineLayout");
        ClearPipeline_ = MakePipeline(PROJECT_FLUID_CLEAR_SPV, ClearLayout_);
        SplatPipeline_ = MakePipeline(PROJECT_FLUID_SPLAT_SPV, SplatLayout_);
        ResolvePipeline_ = MakePipeline(PROJECT_FLUID_RESOLVE_SPV, ResolveLayout_);

        std::array<VkDescriptorPoolSize, 2> sizes{{
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 12}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3}}};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = 3;
        pool.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
        pool.pPoolSizes = sizes.data();
        VkCheck(vkCreateDescriptorPool(Device_, &pool, nullptr, &DescriptorPool_), "vkCreateDescriptorPool");
        std::array<VkDescriptorSetLayout, 3> layouts{SetLayout_, SetLayout_, SetLayout_};
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = DescriptorPool_;
        allocate.descriptorSetCount = static_cast<std::uint32_t>(layouts.size());
        allocate.pSetLayouts = layouts.data();
        std::array<VkDescriptorSet, 3> sets{};
        VkCheck(vkAllocateDescriptorSets(Device_, &allocate, sets.data()), "vkAllocateDescriptorSets");
        ClearSet_ = sets[0]; SplatSet_ = sets[1]; ResolveSet_ = sets[2];
        WriteSet(ClearSet_); WriteSet(SplatSet_); WriteSet(ResolveSet_);

        VkCommandPoolCreateInfo commandPool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        commandPool.queueFamilyIndex = QueueFamily_;
        commandPool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VkCheck(vkCreateCommandPool(Device_, &commandPool, nullptr, &CommandPool_), "vkCreateCommandPool");
        VkCommandBufferAllocateInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        command.commandPool = CommandPool_;
        command.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        command.commandBufferCount = 1;
        VkCheck(vkAllocateCommandBuffers(Device_, &command, &Command_), "vkAllocateCommandBuffers");
        VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCreateSemaphore(Device_, &semaphore, nullptr, &Acquired_);
        vkCreateSemaphore(Device_, &semaphore, nullptr, &Rendered_);
        VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkCreateFence(Device_, &fence, nullptr, &FrameFence_);
    }

    void WriteSet(VkDescriptorSet set) {
        VkDescriptorBufferInfo particles{ParticleBuffer_.Handle, 0, ParticleBuffer_.Size};
        VkDescriptorBufferInfo pixels{PixelBuffer_.Handle, 0, PixelBuffer_.Size};
        VkDescriptorBufferInfo depths{DepthBuffer_.Handle, 0, DepthBuffer_.Size};
        VkDescriptorBufferInfo uniform{UniformBuffer_.Handle, 0, UniformBuffer_.Size};
        VkDescriptorBufferInfo thickness{ThicknessBuffer_.Handle, 0, ThicknessBuffer_.Size};
        std::array<VkDescriptorBufferInfo*, 5> infos{&particles, &pixels, &depths, &uniform, &thickness};
        std::array<VkWriteDescriptorSet, 5> writes{};
        for (std::uint32_t i = 0; i < writes.size(); ++i) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = set; writes[i].dstBinding = i; writes[i].descriptorCount = 1;
            writes[i].descriptorType = i == 3 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pBufferInfo = infos[i];
        }
        vkUpdateDescriptorSets(Device_, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    void UploadParticles() {
        const auto& positions = Fluid_.Positions();
        Reconstruction_.Update(positions);
        const auto& kernels = Reconstruction_.Kernels();
        for (std::size_t i = 0; i < positions.size(); ++i) {
            const auto& k = kernels[i];
            GpuParticles_[i] = {k.Centre.x,k.Centre.y,k.Centre.z,k.VolumeWeight,
                k.AxisA.x,k.AxisA.y,k.AxisA.z,0.0f,k.AxisB.x,k.AxisB.y,k.AxisB.z,0.0f,
                k.AxisC.x,k.AxisC.y,k.AxisC.z,0.0f};
        }
        std::memcpy(ParticleBuffer_.Mapped, GpuParticles_.data(), positions.size() * sizeof(GpuParticle));
        Params_.ParticleCount = static_cast<std::uint32_t>(positions.size());
        Params_.Time = Fluid_.Time();
        Params_.ObstacleEnabled = Fluid_.ObstacleEnabled() ? 1u : 0u;
        const PF::FluidMaterial& material = Fluid_.ActiveMaterial();
        Params_.ColourR = material.Colour.x; Params_.ColourG = material.Colour.y; Params_.ColourB = material.Colour.z;
        Params_.Opacity = material.Opacity; Params_.Roughness = material.Roughness; Params_.Ior = material.Ior;
        Params_.MaterialIndex = static_cast<float>(Fluid_.ActiveMaterialKey());
        Params_.AbsorptionR = material.Absorption.x; Params_.AbsorptionG = material.Absorption.y; Params_.AbsorptionB = material.Absorption.z;
    }

    bool Pressed(int key, bool& latch) {
        const bool down = glfwGetKey(Window_, key) == GLFW_PRESS;
        const bool edge = down && !latch;
        latch = down;
        return edge;
    }

    void Update(float dt) {
        if (glfwGetKey(Window_, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(Window_, GLFW_TRUE);
        if (Pressed(GLFW_KEY_SPACE, PauseLatch_)) Params_.Paused ^= 1u;
        if (Pressed(GLFW_KEY_R, ResetLatch_)) Fluid_.Reset();
        if (Pressed(GLFW_KEY_S, StirLatch_)) Fluid_.Stir();
        if (Pressed(GLFW_KEY_P, PourLatch_)) Pouring_ = !Pouring_;
        if (glfwGetKey(Window_, GLFW_KEY_1) == GLFW_PRESS) Fluid_.SetMaterial(PF::Material::Water);
        if (glfwGetKey(Window_, GLFW_KEY_2) == GLFW_PRESS) Fluid_.SetMaterial(PF::Material::Milk);
        if (glfwGetKey(Window_, GLFW_KEY_3) == GLFW_PRESS) Fluid_.SetMaterial(PF::Material::Honey);
        if (glfwGetKey(Window_, GLFW_KEY_4) == GLFW_PRESS) Fluid_.SetMaterial(PF::Material::Chocolate);
        if (!Params_.Paused) {
            constexpr float fixedDelta = 1.0f / 60.0f;
            StepAccumulator_ = std::min(StepAccumulator_ + dt, fixedDelta * 2.0f);
            int steps = 0;
            while (StepAccumulator_ >= fixedDelta && steps++ < 2) {
                if (Pouring_) Fluid_.Pour(fixedDelta);
                Fluid_.Step(fixedDelta);
                StepAccumulator_ -= fixedDelta;
            }
        }
        UploadParticles();
        std::memcpy(UniformBuffer_.Mapped, &Params_, sizeof(Params_));
        const auto& d = Fluid_.Diagnostics();
        std::string title = "Project Fluid | Flux PBF | " + std::string(Fluid_.ActiveMaterial().Name) +
            " | particles " + std::to_string(Fluid_.Positions().size()) +
            " | contacts " + std::to_string(d.SphereContacts) + (Pouring_ ? " | POUR" : " | FLOW PAUSED") + (Params_.Paused ? " | PAUSED" : "");
        glfwSetWindowTitle(Window_, title.c_str());
    }

    void Draw() {
        vkWaitForFences(Device_, 1, &FrameFence_, VK_TRUE, UINT64_MAX);
        vkResetFences(Device_, 1, &FrameFence_);
        std::uint32_t imageIndex = 0;
        VkResult acquired = vkAcquireNextImageKHR(Device_, Swapchain_, UINT64_MAX, Acquired_, VK_NULL_HANDLE, &imageIndex);
        if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) Fail("Swapchain acquisition failed");
        vkResetCommandBuffer(Command_, 0);
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        VkCheck(vkBeginCommandBuffer(Command_, &begin), "vkBeginCommandBuffer");
        vkCmdBindPipeline(Command_, VK_PIPELINE_BIND_POINT_COMPUTE, ClearPipeline_);
        vkCmdBindDescriptorSets(Command_, VK_PIPELINE_BIND_POINT_COMPUTE, ClearLayout_, 0, 1, &ClearSet_, 0, nullptr);
        vkCmdDispatch(Command_, (Extent_.width + 15) / 16, (Extent_.height + 15) / 16, 1);
        VkMemoryBarrier computeBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        computeBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        computeBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(Command_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                             1, &computeBarrier, 0, nullptr, 0, nullptr);
        vkCmdBindPipeline(Command_, VK_PIPELINE_BIND_POINT_COMPUTE, SplatPipeline_);
        vkCmdBindDescriptorSets(Command_, VK_PIPELINE_BIND_POINT_COMPUTE, SplatLayout_, 0, 1, &SplatSet_, 0, nullptr);
        vkCmdDispatch(Command_, (Params_.ParticleCount + 63u) / 64u, 1, 1);
        VkMemoryBarrier splatBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        splatBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        splatBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(Command_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                             1, &splatBarrier, 0, nullptr, 0, nullptr);
        vkCmdBindPipeline(Command_, VK_PIPELINE_BIND_POINT_COMPUTE, ResolvePipeline_);
        vkCmdBindDescriptorSets(Command_, VK_PIPELINE_BIND_POINT_COMPUTE, ResolveLayout_, 0, 1, &ResolveSet_, 0, nullptr);
        vkCmdDispatch(Command_, (Extent_.width + 15) / 16, (Extent_.height + 15) / 16, 1);
        VkBufferMemoryBarrier pixelBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        pixelBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        pixelBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        pixelBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        pixelBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        pixelBarrier.buffer = PixelBuffer_.Handle;
        pixelBarrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(Command_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             0, nullptr, 1, &pixelBarrier, 0, nullptr);
        VkImageMemoryBarrier toTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toTransfer.srcAccessMask = 0;
        toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        // We overwrite every pixel, so UNDEFINED may discard the acquired
        // image's previous presentation contents on every frame.
        toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = Images_[imageIndex];
        toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(Command_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &toTransfer);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {Extent_.width, Extent_.height, 1};
        vkCmdCopyBufferToImage(Command_, PixelBuffer_.Handle, Images_[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        VkImageMemoryBarrier toPresent = toTransfer;
        toPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toPresent.dstAccessMask = 0;
        toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkCmdPipelineBarrier(Command_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &toPresent);
        VkCheck(vkEndCommandBuffer(Command_), "vkEndCommandBuffer");
        const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.waitSemaphoreCount = 1; submit.pWaitSemaphores = &Acquired_; submit.pWaitDstStageMask = &waitStage;
        submit.commandBufferCount = 1; submit.pCommandBuffers = &Command_;
        submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &Rendered_;
        VkCheck(vkQueueSubmit(Queue_, 1, &submit, FrameFence_), "vkQueueSubmit");
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.waitSemaphoreCount = 1; present.pWaitSemaphores = &Rendered_;
        present.swapchainCount = 1; present.pSwapchains = &Swapchain_; present.pImageIndices = &imageIndex;
        VkCheck(vkQueuePresentKHR(Queue_, &present), "vkQueuePresentKHR");
        if ((++FrameNumber_ % 180u) == 0u) ReportDiagnostics();
    }

    void ReportDiagnostics() {
        const auto& d = Fluid_.Diagnostics();
        std::cout << "[Flux CPU simulation / Vulkan render] particles=" << Fluid_.Positions().size()
                  << " pressure=" << d.PressureIterations << " mean/peak compression="
                  << d.MeanCompression << "/" << d.PeakCompression
                  << " viscosity PCG=" << d.ViscosityIterations << " residual=" << d.ViscosityRelativeResidual
                  << " sphere/wall contacts=" << d.SphereContacts << "/" << d.WallContacts << '\n';
    }

    void Shutdown() {
        if (Device_) vkDeviceWaitIdle(Device_);
        if (Device_) {
            if (FrameFence_) vkDestroyFence(Device_, FrameFence_, nullptr);
            if (Acquired_) vkDestroySemaphore(Device_, Acquired_, nullptr);
            if (Rendered_) vkDestroySemaphore(Device_, Rendered_, nullptr);
            if (CommandPool_) vkDestroyCommandPool(Device_, CommandPool_, nullptr);
            if (ClearPipeline_) vkDestroyPipeline(Device_, ClearPipeline_, nullptr);
            if (SplatPipeline_) vkDestroyPipeline(Device_, SplatPipeline_, nullptr);
            if (ResolvePipeline_) vkDestroyPipeline(Device_, ResolvePipeline_, nullptr);
            if (ClearLayout_) vkDestroyPipelineLayout(Device_, ClearLayout_, nullptr);
            if (SplatLayout_) vkDestroyPipelineLayout(Device_, SplatLayout_, nullptr);
            if (ResolveLayout_) vkDestroyPipelineLayout(Device_, ResolveLayout_, nullptr);
            if (DescriptorPool_) vkDestroyDescriptorPool(Device_, DescriptorPool_, nullptr);
            if (SetLayout_) vkDestroyDescriptorSetLayout(Device_, SetLayout_, nullptr);
            DestroyBuffer(ParticleBuffer_); DestroyBuffer(DepthBuffer_); DestroyBuffer(ThicknessBuffer_); DestroyBuffer(PixelBuffer_); DestroyBuffer(UniformBuffer_);
            if (Swapchain_) vkDestroySwapchainKHR(Device_, Swapchain_, nullptr);
            vkDestroyDevice(Device_, nullptr);
        }
        if (WindowSurface_) vkDestroySurfaceKHR(Instance_, WindowSurface_, nullptr);
        if (Instance_) vkDestroyInstance(Instance_, nullptr);
        if (Window_) glfwDestroyWindow(Window_);
        glfwTerminate();
        Device_ = VK_NULL_HANDLE;
    }
};
} // namespace

int main() {
    try {
        FluidApplication application;
        application.Run();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Project Fluid: " << error.what() << '\n';
        return 1;
    }
}
