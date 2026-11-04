/*====================================================================================================================================
                                                       SOLIDARCAPPLICATION.CPP
====================================================================================================================================*/
// 🧩 Windowed SolidArc: GLFW window (no client API) + Vulkan swapchain, same interface and frame as the capture

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometryProcessing/Diagnostics/OperationTrace.h"
#include "Engine/GeometryProcessing/Exchange/ExchangePath.h"
#include "UI/SolidArc/Document/SolidArcDocumentCodec.h"
#include "UI/SolidArc/Document/SolidArcSpecimen.h"
#include "UI/SolidArc/Interface/SolidArcFrame.h"
#include "UI/SolidArc/Interface/SolidArcInterface.h"
#include "UI/SolidArc/Interface/SolidArcLaunch.h"
#include "UI/SolidArc/Interface/SolidArcOptions.h"
#include "UI/SolidArc/Viewport/SolidArcDevice.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#endif

using namespace Frontier::SolidArc;

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct HostSwapchain
{
    VkSwapchainKHR           Swapchain = VK_NULL_HANDLE;                                     // [-]  - Presentation chain
    VkFormat                 Format    = VK_FORMAT_UNDEFINED;                                // [-]  - Image format (UNORM, like the capture)
    VkExtent2D               Extent    = {};                                                 // [px] - Image size
    std::vector<VkImage>     Images    = {};                                                 // [-]  - Owned by the swapchain
    std::vector<VkImageView> Views     = {};                                                 // [-]  - One per image
    std::vector<VkSemaphore> Finished  = {};                                                 // [-]  - Render → present, one per image
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    SWAPCHAIN
//------------------------------------------------------------------------------------------------------------------------

void ReleaseSwapchain(const SolidArcDevice& Device, HostSwapchain& Chain)
{
    for (VkImageView View : Chain.Views) vkDestroyImageView(Device.Device, View, nullptr);
    for (VkSemaphore Semaphore : Chain.Finished) vkDestroySemaphore(Device.Device, Semaphore, nullptr);
    Chain.Views.clear();
    Chain.Finished.clear();
    Chain.Images.clear();
}

bool BuildSwapchain(const SolidArcDevice& Device, VkSurfaceKHR Surface, GLFWwindow* Window, HostSwapchain& Chain)
{
    VkSurfaceCapabilitiesKHR Capabilities{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(Device.Physical, Surface, &Capabilities);
    uint32_t FormatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(Device.Physical, Surface, &FormatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> Formats(FormatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(Device.Physical, Surface, &FormatCount, Formats.data());
    if (Formats.empty()) return false;
    VkSurfaceFormatKHR Chosen = Formats.front();
    for (const VkSurfaceFormatKHR& Candidate : Formats)
        if ((Candidate.format == VK_FORMAT_B8G8R8A8_UNORM || Candidate.format == VK_FORMAT_R8G8B8A8_UNORM) && Candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            Chosen = Candidate;

    int FramebufferWidth = 0, FramebufferHeight = 0;
    glfwGetFramebufferSize(Window, &FramebufferWidth, &FramebufferHeight);
    VkExtent2D Extent = Capabilities.currentExtent;
    if (Extent.width == UINT32_MAX)
    {
        Extent.width  = std::clamp(uint32_t(FramebufferWidth), Capabilities.minImageExtent.width, Capabilities.maxImageExtent.width);
        Extent.height = std::clamp(uint32_t(FramebufferHeight), Capabilities.minImageExtent.height, Capabilities.maxImageExtent.height);
    }
    if (Extent.width == 0 || Extent.height == 0) return false;
    uint32_t ImageCount = Capabilities.minImageCount + 1;
    if (Capabilities.maxImageCount > 0) ImageCount = std::min(ImageCount, Capabilities.maxImageCount);

    VkSwapchainCreateInfoKHR Request{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    Request.surface          = Surface;
    Request.minImageCount    = ImageCount;
    Request.imageFormat      = Chosen.format;
    Request.imageColorSpace  = Chosen.colorSpace;
    Request.imageExtent      = Extent;
    Request.imageArrayLayers = 1;
    Request.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    Request.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    Request.preTransform     = Capabilities.currentTransform;
    Request.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    Request.presentMode      = VK_PRESENT_MODE_FIFO_KHR;
    Request.clipped          = VK_TRUE;
    Request.oldSwapchain     = Chain.Swapchain;
    VkSwapchainKHR Created   = VK_NULL_HANDLE;
    if (vkCreateSwapchainKHR(Device.Device, &Request, nullptr, &Created) != VK_SUCCESS) return false;
    ReleaseSwapchain(Device, Chain);
    if (Chain.Swapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(Device.Device, Chain.Swapchain, nullptr);
    Chain.Swapchain = Created;
    Chain.Format    = Chosen.format;
    Chain.Extent    = Extent;

    uint32_t Count = 0;
    vkGetSwapchainImagesKHR(Device.Device, Chain.Swapchain, &Count, nullptr);
    Chain.Images.resize(Count);
    vkGetSwapchainImagesKHR(Device.Device, Chain.Swapchain, &Count, Chain.Images.data());
    for (VkImage Image : Chain.Images)
    {
        VkImageViewCreateInfo ViewRequest{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        ViewRequest.image            = Image;
        ViewRequest.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        ViewRequest.format           = Chain.Format;
        ViewRequest.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VkImageView View = VK_NULL_HANDLE;
        vkCreateImageView(Device.Device, &ViewRequest, nullptr, &View);
        Chain.Views.push_back(View);
        VkSemaphoreCreateInfo SemaphoreRequest{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VkSemaphore Semaphore = VK_NULL_HANDLE;
        vkCreateSemaphore(Device.Device, &SemaphoreRequest, nullptr, &Semaphore);
        Chain.Finished.push_back(Semaphore);
    }
    return true;
}

// 📝 The Windows executable is a GUI program (no console), so a start-up failure is also shown in a message box.
void ReportStartupFailure(const std::string& Message)
{
    std::fprintf(stderr, "%s\n", Message.c_str());
#if defined(_WIN32)
    const std::wstring Wide = Frontier::GeometryProcessing::ResolveUtf8Path(Message).wstring();
    MessageBoxW(nullptr, Wide.c_str(), L"SolidArc could not start", MB_OK | MB_ICONERROR);
#endif
}

void ReportGlfwFailure(int Code, const char* Description)
{
    std::fprintf(stderr, "GLFW %d: %s\n", Code, Description);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    LAUNCH CONTEXT
//------------------------------------------------------------------------------------------------------------------------

// 📝 The command line as UTF-8 words. On Windows main() receives the words in the active code page, so a document in
//    "C:\Users\Zoë" handed over by the Frontier browser (CreateProcessW) would arrive damaged: the wide command line is
//    split again with CommandLineToArgvW and converted.
std::vector<std::string> CollectArguments(int Count, char** Arguments)
{
    std::vector<std::string> Words;
#if defined(_WIN32)
    int             WideCount = 0;
    wchar_t** const WideWords = CommandLineToArgvW(GetCommandLineW(), &WideCount);
    if (WideWords)
    {
        for (int Index = 0; Index < WideCount; ++Index)
            Words.push_back(Frontier::GeometryProcessing::DescribeUtf8Path(std::filesystem::path(WideWords[Index])));
        LocalFree(WideWords);
        return Words;
    }
#endif
    for (int Index = 0; Index < Count; ++Index) Words.emplace_back(Arguments[Index]);
    return Words;
}

std::filesystem::path ResolveExecutableFolder()
{
#if defined(_WIN32)
    std::wstring Buffer(32768, L'\0');
    const DWORD  Length = GetModuleFileNameW(nullptr, Buffer.data(), DWORD(Buffer.size()));
    if (Length == 0 || Length >= Buffer.size()) return {};
    Buffer.resize(Length);
    return std::filesystem::path(Buffer).parent_path();
#elif defined(__linux__)
    std::error_code             Failure;
    const std::filesystem::path Executable = std::filesystem::read_symlink("/proc/self/exe", Failure);
    return Failure ? std::filesystem::path() : Executable.parent_path();
#else
    return {};
#endif
}

// 📝 Started from Explorer, a shortcut or the Frontier browser, the working directory is arbitrary. An installed package
//    keeps SolidArcWorkspace.toml beside the executable: when the working directory has none, the executable's folder
//    becomes the working directory, so the relative paths of the options (autosave, Save, Import curves) stay in the package.
void AdoptPackageFolder(const std::string& OptionsPath)
{
    std::error_code Failure;
    if (std::filesystem::exists(Frontier::GeometryProcessing::ResolveUtf8Path(OptionsPath), Failure)) return;
    const std::filesystem::path Folder = ResolveExecutableFolder();
    if (!Folder.empty() && std::filesystem::exists(Folder / "SolidArcWorkspace.toml", Failure))
        std::filesystem::current_path(Folder, Failure);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

int main(int Count, char** Arguments)
{
    using Frontier::GeometryProcessing::DescribeUtf8Path;
    using Frontier::GeometryProcessing::ResolveUtf8Path;
    // 📝 `--open <document>` (the Frontier browser) and a bare document word (Windows "Open with", a file association, a drop
    //    on the icon) both open the document; see SolidArcLaunch.h.
    const Frontier::SolidArc::SolidArcLaunchRequest Launch = Frontier::SolidArc::ResolveSolidArcLaunch(CollectArguments(Count, Arguments));
    const std::string OptionsPath = Launch.OptionsPath;
    std::string       OpenPath    = Launch.OpenPath;
    std::string       CurvesPath  = Launch.CurvesPath;
    const bool        Specimen    = Launch.Specimen;
    // 📝 Paths given on the command line are fixed against the launch folder before the package folder is adopted.
    std::error_code AbsoluteFailure;
    if (!OpenPath.empty()) OpenPath = DescribeUtf8Path(std::filesystem::absolute(ResolveUtf8Path(OpenPath), AbsoluteFailure));
    if (!CurvesPath.empty()) CurvesPath = DescribeUtf8Path(std::filesystem::absolute(ResolveUtf8Path(CurvesPath), AbsoluteFailure));
    AdoptPackageFolder(OptionsPath);
#ifdef FRONTIER_DEVELOPMENT_TRACE
    // 📝 Development builds write SolidArcTrace.log beside the executable (the previous run is kept as
    //    SolidArcTrace.previous.log): every command, replay step, kernel operation and status line, flushed per line.
    {
        const std::filesystem::path Folder    = ResolveExecutableFolder();
        const std::filesystem::path TracePath = (Folder.empty() ? std::filesystem::current_path(AbsoluteFailure) : Folder) / "SolidArcTrace.log";
        const bool                  Opened    = Frontier::GeometryProcessing::OpenOperationTrace(TracePath);
        FRONTIER_TRACE("Session", std::string("SolidArc started · trace ") + (Opened ? DescribeUtf8Path(TracePath) : std::string("in memory only (file refused)")));
    }
#endif
    SolidArcOptions Options = LoadSolidArcOptions(OptionsPath);
    if (!CurvesPath.empty()) Options.CurvesPath = CurvesPath;                                // Import curves… starts here

    // ① window without a client API (Vulkan presents), instance extensions from GLFW
    glfwSetErrorCallback(ReportGlfwFailure);
    if (!glfwInit())
    {
        ReportStartupFailure("GLFW found no window system (on a headless host, SolidArcCapture renders offscreen).");
        return 77;
    }
    if (!glfwVulkanSupported())
    {
        ReportStartupFailure("No Vulkan loader was found (vulkan-1.dll comes with the GPU driver).");
        glfwTerminate();
        return 77;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* Window = glfwCreateWindow(Options.WindowWidth, Options.WindowHeight, "SolidArc", nullptr, nullptr);
    if (!Window)
    {
        ReportStartupFailure("The SolidArc window could not be created (GLFW).");
        glfwTerminate();
        return 1;
    }
    uint32_t                 ExtensionCount = 0;
    const char**             Extensions     = glfwGetRequiredInstanceExtensions(&ExtensionCount);
    SolidArcDeviceSettings   DeviceSettings{ .DriverPath = Options.DriverPath, .PresentationEnabled = true, .ValidationEnabled = Options.ValidationEnabled };
    for (uint32_t Index = 0; Index < ExtensionCount; ++Index) DeviceSettings.InstanceExtensions.emplace_back(Extensions[Index]);
    SolidArcDevice Device;
    std::string    Refusal;
    if (!Device.Open(DeviceSettings, Refusal))
    {
        ReportStartupFailure("No usable Vulkan device: " + Refusal);
        return 77;
    }
    VkSurfaceKHR Surface = VK_NULL_HANDLE;
    if (glfwCreateWindowSurface(Device.Instance, Window, nullptr, &Surface) != VK_SUCCESS)
    {
        ReportStartupFailure("The Vulkan window surface was refused.");
        return 1;
    }
    VkBool32 PresentCondition = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(Device.Physical, Device.QueueFamily, Surface, &PresentCondition);
    if (!PresentCondition)
    {
        ReportStartupFailure("The graphics queue cannot present to this window.");
        return 1;
    }
    HostSwapchain Chain;
    if (!BuildSwapchain(Device, Surface, Window, Chain))
    {
        ReportStartupFailure("The Vulkan swapchain could not be built for this window.");
        return 1;
    }

    // ② ImGui (GLFW input + Vulkan renderer) and the workspace
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ConfigureSolidArcInterface(Options);
    ImGui_ImplGlfw_InitForVulkan(Window, true);
    SolidArcWorkspaceFrame Frame;
    if (!Frame.Create(Device, Chain.Format, uint32_t(Chain.Images.size()), Refusal))
    {
        ReportStartupFailure("The workspace frame was refused: " + Refusal);
        return 1;
    }
    SolidArcWorkspaceState State;
    InitialiseSolidArcWorkspace(State, Options);
    State.DeviceLabel = Device.Describe() + (Frame.Raster.SampleCount() > 1 ? " · " + std::to_string(Frame.Raster.SampleCount()) + "× MSAA" : std::string(" · no MSAA"));
    if (Specimen)
    {
        State.WelcomeOpen = false;
        State.Document    = BuildSolidArcSpecimen();
        RefreshSolidArcScene(State, false);
        FitSolidArcCamera(State.Camera, State.Scene);
    }
    if (!OpenPath.empty())
    {
        const SolidArcDocumentFile File = LoadSolidArcDocumentFile(OpenPath);
        if (File.Refusal.empty())
        {
            State.WelcomeOpen       = false;
            State.Document          = CreateSolidArcDocument(File.Title);
            State.Document.Tree     = File.Tree;
            State.Document.Captions = File.Captions;
            State.Document.ImagePlanes = File.ImagePlanes;
            State.Document.Appearance  = File.Appearance;
            State.Document.DrawingSheet = File.DrawingSheet;
            State.Document.Dimensions   = File.Dimensions;
            RegenerateSolidArcDocument(State.Document, true);
            RefreshSolidArcScene(State, false);
            FitSolidArcCamera(State.Camera, State.Scene);
            State.DocumentPath = OpenPath;
            State.Status       = "Opened " + OpenPath;
        }
        else
        {
            // 📝 A refused document must not look like an empty one: the status line and the journal say why.
            State.Status = "Open refused: " + File.Refusal;
            State.Journal.push_back(State.Status);
        }
    }

    ConfigureSolidArcStartupViews(State);

    // ③ one frame in flight: fence → conclude previous → interface → acquire → record → submit → present
    VkCommandBufferAllocateInfo Allocation{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    Allocation.commandPool        = Device.CommandPool;
    Allocation.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    Allocation.commandBufferCount = 1;
    VkCommandBuffer Commands = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(Device.Device, &Allocation, &Commands);
    VkSemaphoreCreateInfo SemaphoreRequest{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkSemaphore Acquired = VK_NULL_HANDLE;
    vkCreateSemaphore(Device.Device, &SemaphoreRequest, nullptr, &Acquired);
    VkFenceCreateInfo FenceRequest{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    FenceRequest.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkFence InFlight = VK_NULL_HANDLE;
    vkCreateFence(Device.Device, &FenceRequest, nullptr, &InFlight);

    bool RebuildCondition = false;
    auto Previous         = std::chrono::steady_clock::now();
    bool FirstFrame       = true;
    while (!glfwWindowShouldClose(Window))
    {
        glfwPollEvents();
        int FramebufferWidth = 0, FramebufferHeight = 0;
        glfwGetFramebufferSize(Window, &FramebufferWidth, &FramebufferHeight);
        if (FramebufferWidth == 0 || FramebufferHeight == 0)
        {
            glfwWaitEvents();
            continue;
        }
        if (RebuildCondition || uint32_t(FramebufferWidth) != Chain.Extent.width || uint32_t(FramebufferHeight) != Chain.Extent.height)
        {
            vkDeviceWaitIdle(Device.Device);
            if (!BuildSwapchain(Device, Surface, Window, Chain)) continue;
            RebuildCondition = false;
        }

        const auto Waiting = std::chrono::steady_clock::now();
        vkWaitForFences(Device.Device, 1, &InFlight, VK_TRUE, UINT64_MAX);
        State.Profile.GpuWaitMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Waiting).count();
        if (!FirstFrame) Frame.Conclude(State);
        FirstFrame = false;
        const auto Now   = std::chrono::steady_clock::now();
        const double Delta = std::chrono::duration<double>(Now - Previous).count();
        Previous         = Now;

        Frame.PrepareViewport(State);
        State.Profile.UploadMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Now).count();
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        DrawSolidArcInterface(State, Frame.ViewportTexture(), Delta);
        ImGui::Render();

        uint32_t ImageIndex = 0;
        const VkResult Acquisition = vkAcquireNextImageKHR(Device.Device, Chain.Swapchain, UINT64_MAX, Acquired, VK_NULL_HANDLE, &ImageIndex);
        if (Acquisition == VK_ERROR_OUT_OF_DATE_KHR)
        {
            RebuildCondition = true;
            FirstFrame       = true;
            continue;
        }
        vkResetFences(Device.Device, 1, &InFlight);
        vkResetCommandBuffer(Commands, 0);
        VkCommandBufferBeginInfo Begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        Begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(Commands, &Begin);
        const SolidArcFrameTarget Target{ Chain.Images[ImageIndex], Chain.Views[ImageIndex], Chain.Extent.width, Chain.Extent.height, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR };
        Frame.Record(Commands, State, ImGui::GetDrawData(), Target, false);
        vkEndCommandBuffer(Commands);

        const VkPipelineStageFlags WaitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo Submission{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        Submission.waitSemaphoreCount   = 1;
        Submission.pWaitSemaphores      = &Acquired;
        Submission.pWaitDstStageMask    = &WaitStage;
        Submission.commandBufferCount   = 1;
        Submission.pCommandBuffers      = &Commands;
        Submission.signalSemaphoreCount = 1;
        Submission.pSignalSemaphores    = &Chain.Finished[ImageIndex];
        vkQueueSubmit(Device.Queue, 1, &Submission, InFlight);

        VkPresentInfoKHR Presentation{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        Presentation.waitSemaphoreCount = 1;
        Presentation.pWaitSemaphores    = &Chain.Finished[ImageIndex];
        Presentation.swapchainCount     = 1;
        Presentation.pSwapchains        = &Chain.Swapchain;
        Presentation.pImageIndices      = &ImageIndex;
        const VkResult Presented = vkQueuePresentKHR(Device.Queue, &Presentation);
        if (Presented == VK_ERROR_OUT_OF_DATE_KHR || Presented == VK_SUBOPTIMAL_KHR) RebuildCondition = true;
        State.FrameMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Now).count();
        ConcludeSolidArcFrame(State);                                                         // Slow frames → SolidArcTrace.log
    }

    // ④ flush a pending autosave, then release in reverse order
    vkDeviceWaitIdle(Device.Device);
    if (State.AutosaveTimer >= 0.0) SaveSolidArcDocument(State.Document, Options.RecoveryPath);
    vkDestroyFence(Device.Device, InFlight, nullptr);
    vkDestroySemaphore(Device.Device, Acquired, nullptr);
    vkFreeCommandBuffers(Device.Device, Device.CommandPool, 1, &Commands);
    Frame.Destroy();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    ReleaseSwapchain(Device, Chain);
    vkDestroySwapchainKHR(Device.Device, Chain.Swapchain, nullptr);
    vkDestroySurfaceKHR(Device.Instance, Surface, nullptr);
    Device.Close();
    glfwDestroyWindow(Window);
    FRONTIER_TRACE("Session", "SolidArc closed");
    Frontier::GeometryProcessing::CloseOperationTrace();
    glfwTerminate();
    return 0;
}
