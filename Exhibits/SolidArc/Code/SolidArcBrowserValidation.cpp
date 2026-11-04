/*====================================================================================================================================
                                                   SOLIDARCBROWSERVALIDATION.CPP
====================================================================================================================================*/
// 🧩 S5 evidence: the Frontier project browser lists, renders, refuses and hands off .solidarc documents (offscreen Vulkan)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

// 📝 volk first (SolidArcDevice): the upstream raster headers then see VK_NO_PROTOTYPES-compatible declarations.
#include "UI/SolidArc/Viewport/SolidArcDevice.h"
#include "UI/SolidArc/Viewport/SolidArcPortableImage.h"

#include "Engine/Interface/ControlKit.h"
#include "Engine/Render/BrowserPreviewRendering.h"
#include "UI/Editor/ControlCentrePanel.h"
#include "UI/Editor/ProjectBrowserPanel.h"
#include "UI/SolidArc/Browser/SolidArcBrowserLink.h"
#include "UI/SolidArc/Browser/SolidArcBrowserPreview.h"
#include "UI/SolidArc/Document/SolidArcDocumentCodec.h"
#include "UI/SolidArc/Document/SolidArcSpecimen.h"

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace Frontier::SolidArc;
namespace GP = Frontier::GeometryProcessing;

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct ValidationSettings
{
    std::string DriverPath = {};                                                             // [-]  - Vulkan ICD (else SOLIDARC_VULKAN_DRIVER)
    std::string Exhibits   = "Exhibits/SolidArc/Results/Browser";                             // [-]  - Output folder
    std::string Shaders    = "EngineContent/ShaderArchives/Surface";                          // [-]  - BrowserPreview / BrowserDrafting SPIR-V
    std::string Workspace  = {};                                                             // [-]  - Executable started by "Open in SolidArc"
    int         Width      = 1600;                                                           // [px] - Frame width
    int         Height     = 960;                                                            // [px] - Frame height
};

struct ValidationRow
{
    std::string Expectation = {};                                                            // [-] - What must hold
    std::string Observed    = {};                                                            // [-] - What was measured
    bool        Passed      = false;                                                         // [-] - Verdict
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

ValidationSettings ReadArguments(int Count, char** Arguments)
{
    ValidationSettings Settings;
    if (const char* Driver = std::getenv("SOLIDARC_VULKAN_DRIVER")) Settings.DriverPath = Driver;
    for (int Index = 1; Index + 1 < Count; Index += 2)
    {
        const std::string Flag  = Arguments[Index];
        const std::string Value = Arguments[Index + 1];
        if (Flag == "--vulkan-driver") Settings.DriverPath = Value;
        else if (Flag == "--exhibits") Settings.Exhibits = Value;
        else if (Flag == "--shaders") Settings.Shaders = Value;
        else if (Flag == "--workspace") Settings.Workspace = Value;
        else if (Flag == "--width") Settings.Width = std::atoi(Value.c_str());
        else if (Flag == "--height") Settings.Height = std::atoi(Value.c_str());
    }
    return Settings;
}

// Bracket specimen plus two separate bodies: a multi-body document with distinguishable reference surfaces.
SolidArcDocument BuildAssemblyStudy()
{
    SolidArcDocument Document = BuildSolidArcSpecimen();
    Document.Title            = "Assembly study";
    SolidArcConstructRequest Torus;
    Torus.Kind        = SolidArcConstructKind::Torus;
    Torus.Combination = GP::FeatureCombination::Separate;
    Torus.Position    = { 6.0, 1.25, 0.4 };
    Torus.Major       = 1.1;
    Torus.Minor       = 0.35;
    (void)ConstructSolidArcPrimitive(Document, Torus);
    SolidArcConstructRequest Post;
    Post.Kind        = SolidArcConstructKind::Cylinder;
    Post.Combination = GP::FeatureCombination::Separate;
    Post.Position    = { -1.6, 1.25, 0.0 };
    Post.Radius      = 0.45;
    Post.Height      = 2.2;
    (void)ConstructSolidArcPrimitive(Document, Post);
    return Document;
}

std::string Utf8(const std::filesystem::path& Path)
{
    const std::u8string Encoded = Path.u8string();
    return std::string(Encoded.begin(), Encoded.end());
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                          MAIN
//------------------------------------------------------------------------------------------------------------------------

int main(int Count, char** Arguments)
{
    const ValidationSettings Settings = ReadArguments(Count, Arguments);
    const std::filesystem::path Exhibits = Settings.Exhibits;
    const std::filesystem::path Folder   = Exhibits / "Documents";
    std::filesystem::create_directories(Folder);
    std::vector<ValidationRow> Rows;
    const auto Expect = [&](const std::string& Expectation, const std::string& Observed, bool Passed)
    {
        Rows.push_back({ Expectation, Observed, Passed });
        std::printf("%s %s — %s\n", Passed ? "🟢" : "🔴", Expectation.c_str(), Observed.c_str());
    };

    // ① documents on disk: two readable, one damaged
    const std::filesystem::path Bracket  = Folder / "Bracket.solidarc";
    const std::filesystem::path Assembly = Folder / "Assembly study.solidarc";
    const std::filesystem::path Damaged  = Folder / "Damaged.solidarc";
    {
        SolidArcDocument Specimen = BuildSolidArcSpecimen();
        Specimen.Title            = "Bracket";
        Expect("Bracket.solidarc saved", Utf8(Bracket), SaveSolidArcDocument(Specimen, Utf8(Bracket)));
        Expect("Assembly study.solidarc saved", Utf8(Assembly), SaveSolidArcDocument(BuildAssemblyStudy(), Utf8(Assembly)));
        std::ofstream(Damaged) << "[document\ntitle = 'cut short";
    }

    // ② headless device + ImGui backend + upstream appearance (fonts, palette) exactly like the Frontier host
    SolidArcDevice Device;
    std::string    Refusal;
    if (!Device.Open(SolidArcDeviceSettings{ .DriverPath = Settings.DriverPath }, Refusal))
    {
        std::fprintf(stderr, "no Vulkan device: %s\n", Refusal.c_str());
        return 77;
    }
    const uint32_t Width  = uint32_t(Settings.Width);
    const uint32_t Height = uint32_t(Settings.Height);
    SolidArcImage  Target = Device.CreateImage(Width, Height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                               VK_IMAGE_ASPECT_COLOR_BIT);
    SolidArcBuffer Readback = Device.CreateBuffer(VkDeviceSize(Width) * Height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& Io    = ImGui::GetIO();
    Io.DisplaySize = ImVec2(float(Width), float(Height));
    Io.DeltaTime   = 1.0f / 60.0f;
    Io.IniFilename = nullptr;
    Frontier::ControlCentrePanel Appearance("EngineContent/FontArchives", std::filesystem::path{});
    const bool AppearanceActive = Appearance.Initialize() && Appearance.Activate();
    Expect("Upstream appearance activates (fonts + theme)", Appearance.QueryDiagnostic().empty() ? "active" : Appearance.QueryDiagnostic(), AppearanceActive);

    VkFormat                  TargetFormat = VK_FORMAT_R8G8B8A8_UNORM;
    ImGui_ImplVulkan_InitInfo Setup{};
    Setup.ApiVersion          = Device.ApiVersion;
    Setup.Instance            = Device.Instance;
    Setup.PhysicalDevice      = Device.Physical;
    Setup.Device              = Device.Device;
    Setup.QueueFamily         = Device.QueueFamily;
    Setup.Queue               = Device.Queue;
    Setup.DescriptorPoolSize  = 256;                                                         // font + thumbnails + live view
    Setup.MinImageCount       = 2;
    Setup.ImageCount          = 2;
    Setup.UseDynamicRendering = true;
    Setup.PipelineInfoMain.PipelineRenderingCreateInfo                         = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    Setup.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount    = 1;
    Setup.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &TargetFormat;
    if (!ImGui_ImplVulkan_Init(&Setup))
    {
        std::fprintf(stderr, "ImGui Vulkan backend refused to initialise\n");
        return 1;
    }

    // ③ the browser with the upstream reference preview renderer and the SolidArc link
    uint32_t FamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(Device.Physical, &FamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> Families(FamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(Device.Physical, &FamilyCount, Families.data());
    Frontier::VulkanRasterAccess Access;
    Access.Device            = Device.Device;
    Access.PhysicalDevice    = Device.Physical;
    Access.ResolveDevice     = vkGetDeviceProcAddr;
    Access.QueryMemory       = vkGetPhysicalDeviceMemoryProperties;
    Access.QueryFormat       = vkGetPhysicalDeviceFormatProperties;
    Access.QueryLimits       = vkGetPhysicalDeviceProperties;
    Access.QueueCapabilities = Families[Device.QueueFamily].queueFlags;
    Frontier::BrowserPreviewRendering Preview;
    Preview.Initialize(Access, Settings.Shaders);

    Frontier::ProjectBrowserPanel             Browser;
    std::map<VkImageView, VkDescriptorSet>    Descriptors;
    const auto Describe = [&](VkImageView View) -> VkDescriptorSet
    {
        if (!View) return VK_NULL_HANDLE;
        const auto Cached = Descriptors.find(View);
        if (Cached != Descriptors.end()) return Cached->second;
        return Descriptors.emplace(View, ImGui_ImplVulkan_AddTexture(View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)).first->second;
    };
    std::vector<std::string> Failures;
    Browser.QueryConfiguredThumbnail = [&](const std::string& Key, const Frontier::BrowserPreviewConfiguration& Configuration)
    {
        Frontier::BrowserPreviewImage Image;
        try
        {
            Preview.RequestThumbnail(Key, Configuration);
            if (Preview.QueryThumbnail(Key).Ready) Image.Texture = uint64_t(Describe(Preview.QueryThumbnailView(Key)));
        }
        catch (const std::exception& Failure) { Failures.push_back(std::string("thumbnail: ") + Failure.what()); }
        return Image;
    };
    // 📝 Sample tiles exactly like the Frontier host: "token:revision:view" resolved against the reference catalogue.
    const std::vector<Frontier::BrowserCatalogueEntry> Catalogue = Frontier::ConstructBrowserCatalogue();
    Browser.QueryThumbnailImage = [&](const std::string& Key)
    {
        Frontier::BrowserPreviewImage Image;
        const size_t ViewCut     = Key.rfind(':');
        const size_t RevisionCut = ViewCut == std::string::npos ? std::string::npos : Key.rfind(':', ViewCut - 1);
        if (RevisionCut == std::string::npos) return Image;
        const std::string Token = Key.substr(0, RevisionCut);
        for (const Frontier::BrowserCatalogueEntry& Entry : Catalogue)
            if (Entry.Token == Token)
            {
                const size_t Revision = std::stoul(Key.substr(RevisionCut + 1, ViewCut - RevisionCut - 1));
                const size_t View     = std::stoul(Key.substr(ViewCut + 1));
                return Browser.QueryConfiguredThumbnail(Key, Frontier::ConfigureBrowserPreview(Entry, View, Revision));
            }
        return Image;
    };
    VkImageView LastLive = VK_NULL_HANDLE;
    Browser.QueryLiveImage = [&]()
    {
        Frontier::BrowserPreviewImage Image;
        if (Preview.QueryLiveReady()) LastLive = Preview.QueryLiveView();
        const VkImageView Current = Preview.QueryLiveView();
        if (Current && Current == LastLive) Image.Texture = uint64_t(Describe(Current));
        return Image;
    };
    Browser.ConfigureLivePreview = [&](const Frontier::BrowserPreviewConfiguration& Configuration, const Frontier::BrowserPreviewCamera& Camera, int Channel,
                                       int LiveWidth, int LiveHeight)
    {
        try { Preview.ConfigureLive(Configuration, Camera, Channel, LiveWidth, LiveHeight); }
        catch (const std::exception& Failure) { Failures.push_back(std::string("live: ") + Failure.what()); }
    };
    Browser.CaptureLiveThumbnail = [&](const std::string& Key, const Frontier::BrowserPreviewConfiguration& Configuration,
                                       const Frontier::BrowserPreviewCamera& Camera, int Channel)
    {
        try { Preview.CaptureThumbnail(Key, Configuration, Camera, Channel); }
        catch (const std::exception& Failure) { Failures.push_back(std::string("capture: ") + Failure.what()); }
    };
    const std::filesystem::path HandOff = Exhibits / "HandOff";
    std::filesystem::remove_all(HandOff);
    SolidArcBrowserLinkSettings Link;
    Link.Workspace = Settings.Workspace.empty() ? LocateSolidArcWorkspace() : std::filesystem::path(Settings.Workspace);
    Link.Arguments = { "--exhibits", Utf8(HandOff), "--width", std::to_string(Width), "--height", std::to_string(Height) };
    ConnectSolidArcBrowser(Browser, Link);

    // ④ frame loop: preview commands + browser draw lists into one offscreen frame
    uint64_t   Serial = 1;
    const auto Frames = [&](int Amount, const std::string& Capture)
    {
        for (int Frame = 0; Frame < Amount; ++Frame)
        {
            const bool Last = Frame + 1 == Amount && !Capture.empty();
            Appearance.PrepareInterface();
            Browser.ConfigurePalette(Frontier::ControlKit::Palette());
            ImGui_ImplVulkan_NewFrame();
            ImGui::NewFrame();
            Browser.ConstructInterface();
            ImGui::Render();
            Device.SubmitImmediately([&](VkCommandBuffer Commands)
            {
                Preview.ConstructCommands(Commands, Serial - 1, Serial);
                TransitionSolidArcImage(Commands, Target.Image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                VkRenderingAttachmentInfo Colour{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
                Colour.imageView        = Target.View;
                Colour.imageLayout      = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                Colour.loadOp           = VK_ATTACHMENT_LOAD_OP_CLEAR;
                Colour.storeOp          = VK_ATTACHMENT_STORE_OP_STORE;
                Colour.clearValue.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
                VkRenderingInfo Rendering{ VK_STRUCTURE_TYPE_RENDERING_INFO };
                Rendering.renderArea           = { { 0, 0 }, { Width, Height } };
                Rendering.layerCount           = 1;
                Rendering.colorAttachmentCount = 1;
                Rendering.pColorAttachments    = &Colour;
                vkCmdBeginRendering(Commands, &Rendering);
                ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), Commands);
                vkCmdEndRendering(Commands);
                TransitionSolidArcImage(Commands, Target.Image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
                if (Last)
                {
                    VkBufferImageCopy Whole{};
                    Whole.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
                    Whole.imageExtent      = { Width, Height, 1 };
                    vkCmdCopyImageToBuffer(Commands, Target.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, Readback.Buffer, 1, &Whole);
                }
            });
            Preview.ConfirmSubmission();
            ++Serial;
            if (Last)
            {
                const uint32_t* Pixels = static_cast<const uint32_t*>(Readback.Mapped);
                WriteSolidArcPng(Utf8(Exhibits / (Capture + ".png")), Width, Height, Pixels);
                std::printf("capture %-20s %s\n", Capture.c_str(), DigestSolidArcPixels(Pixels, size_t(Width) * Height).c_str());
            }
        }
    };

    // ⑤ attach (the native picker's batch path): classification, decode, refusal
    const size_t Attached = Browser.AttachPaths({ Bracket, Assembly, Damaged }, false);
    Expect("Three .solidarc files attach as CAD entries", std::to_string(Attached) + " attached", Attached == 3);
    std::map<std::string, std::string> Tokens;
    for (int Index = 0; Index < 3; ++Index)
    {
        const std::string Token = "local-" + std::to_string(Index) + "-" + std::to_string(Index);
        if (const Frontier::BrowserCatalogueEntry* Entry = Browser.QueryEntry(Token)) Tokens[Entry->Title] = Token;
    }
    const auto* BracketEntry  = Browser.QueryEntry(Tokens["Bracket"]);
    const auto* AssemblyEntry = Browser.QueryEntry(Tokens["Assembly study"]);
    const auto* DamagedEntry  = Browser.QueryEntry(Tokens["Damaged"]);
    Expect("Entries carry the CAD category and the SolidArc workspace label",
           BracketEntry ? Frontier::BrowserWorkspaceLabel(BracketEntry->Category) + " · " + Frontier::BrowserLaunchTarget(BracketEntry->Category) : "missing",
           BracketEntry && BracketEntry->Category == Frontier::BrowserFileCategory::Cad && AssemblyEntry && DamagedEntry);
    const auto Titles = Browser.QueryFilteredTitles("cad", "", false);
    std::string Listed;
    for (const auto& Title : Titles) Listed += (Listed.empty() ? "" : ", ") + Title;
    Expect("CAD filter lists the attached documents first", Listed,
           Titles.size() >= 3 && Titles[0] == "Damaged" && Titles[1] == "Assembly study" && Titles[2] == "Bracket");
    Expect("Bracket decodes to real geometry", BracketEntry ? BracketEntry->NativeSummary : "missing",
           BracketEntry && BracketEntry->Geometry && BracketEntry->Geometry->size() % 30 == 0);
    Expect("Assembly study decodes to three bodies", AssemblyEntry ? AssemblyEntry->NativeSummary : "missing",
           AssemblyEntry && AssemblyEntry->Geometry && AssemblyEntry->NativeSummary.rfind("3 bodies", 0) == 0);
    Expect("Damaged document is refused, not substituted", DamagedEntry ? DamagedEntry->NativeRefusal : "missing",
           DamagedEntry && !DamagedEntry->Geometry && !DamagedEntry->NativeRefusal.empty());

    // ⑥ library: thumbnails come from the reference renderer fed with the document triangles
    Frames(12, "BrowserLibrary");
    const std::string BracketKey  = Frontier::BrowserSnapshotKey(Tokens["Bracket"], 0, 0);
    const std::string AssemblyKey = Frontier::BrowserSnapshotKey(Tokens["Assembly study"], 0, 0);
    Expect("Library thumbnails rendered for both readable documents", BracketKey + " · " + AssemblyKey,
           Preview.QueryThumbnail(BracketKey).Ready && Preview.QueryThumbnail(AssemblyKey).Ready);

    // ⑦ reader: live reference view (CAD contours channel) of the multi-body document
    Browser.DebugShow(Tokens["Assembly study"], 0, 0, false, 100, "details", "oled", false, false);
    Frames(12, "BrowserReader");
    Expect("Reader live view rendered from the document", Preview.QueryLiveReady() ? "live view ready" : "not ready", Preview.QueryLiveReady());

    Browser.DebugShow(Tokens["Damaged"], 0, 0, false, 100, "details", "oled", false, false);
    Frames(6, "BrowserRefusal");

    // 📝 Reference: the unchanged sample launch flow (preview dialog, no process started).
    Browser.DebugShow("mount", 0, 0, false, 100, "details", "oled", true, false);
    Frames(6, "BrowserSampleLaunch");
    Expect("Sample entries keep the preview-only launch dialog", Browser.QueryLaunchOutcome().empty() ? "no hand-off" : Browser.QueryLaunchOutcome(),
           Browser.QueryLaunchOutcome().empty());
    Browser.DebugShow("", 0, 0, false, 100, "details", "oled", false, false);
    Frames(2, "");

    // ⑧ hand-off: "Open in SolidArc" starts the workspace with --open <document>
    Browser.DebugShow(Tokens["Bracket"], 0, 0, false, 100, "details", "oled", false, false);
    Frames(4, "");
    Browser.OpenActiveFile();
    Expect("Open in SolidArc starts the workspace process", Browser.QueryLaunchOutcome(),
           Browser.QueryLaunchOutcome() == "SolidArc workspace started with this document.");
    Frames(6, "BrowserHandOff");
    const std::filesystem::path Opened = HandOff / "Workspace.png";
    const auto                  Waited = std::chrono::steady_clock::now();
    while (!std::filesystem::exists(Opened) && std::chrono::steady_clock::now() - Waited < std::chrono::seconds(240))
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    std::this_thread::sleep_for(std::chrono::seconds(2));                                   // The JSON beside the PNG completes the capture
    std::string Described;
    if (std::ifstream Json{ HandOff / "Workspace.json" }) Described.assign(std::istreambuf_iterator<char>(Json), std::istreambuf_iterator<char>());
    const bool OpenedCondition = std::filesystem::exists(Opened) && Described.find("Bracket") != std::string::npos;
    Expect("The started workspace opened Bracket.solidarc", OpenedCondition ? Utf8(Opened) : "no workspace capture", OpenedCondition);

    // ⑨ New dialog: the three choices, then New › SolidArc model creates, previews and hands over an empty model
    const std::pair<Frontier::BrowserCreation, const char*> Choices[] = {
        { Frontier::BrowserCreation::GameProject, "BrowserNewProject" },
        { Frontier::BrowserCreation::SolidArcDocument, "BrowserNewModel" },
        { Frontier::BrowserCreation::TextureLibrary, "BrowserNewLibrary" },
    };
    for (const auto& [Choice, Capture] : Choices)
    {
        Browser.ChooseCreation(Choice);
        Browser.DebugShow("", 0, 0, false, 100, "details", "oled", false, true);
        Frames(4, Capture);
    }
    Browser.ChooseCreation(Frontier::BrowserCreation::GameProject);
    Browser.DebugShow("", 0, 0, false, 100, "details", "oled", false, false);
    Frames(2, "");
    std::error_code Cleared;
    std::filesystem::remove(HandOff / "Workspace.png", Cleared);
    std::filesystem::remove(HandOff / "Workspace.json", Cleared);
    const std::filesystem::path Fresh = Exhibits / "New model.solidarc";
    std::filesystem::remove(Fresh, Cleared);
    const bool Created = Browser.CreateDocument(Frontier::BrowserCreation::SolidArcDocument, Exhibits / "New model", "");
    Expect("New › SolidArc model writes an empty model and starts the workspace", Created ? Utf8(Fresh) : Browser.QueryDiagnostic(),
           Created && std::filesystem::exists(Fresh));
    const auto* FreshEntry = Browser.QueryEntry(Browser.QueryActiveToken());
    Expect("The new model previews as the bare stage", FreshEntry ? FreshEntry->NativeSummary : "missing",
           FreshEntry && FreshEntry->Geometry && FreshEntry->NativeRefusal.empty() && FreshEntry->NativeSummary.rfind("0 bodies", 0) == 0);
    Frames(12, "BrowserNewModelReader");
    const auto FreshWaited = std::chrono::steady_clock::now();
    while (!std::filesystem::exists(Opened) && std::chrono::steady_clock::now() - FreshWaited < std::chrono::seconds(240))
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::string FreshDescribed;
    if (std::ifstream Json{ HandOff / "Workspace.json" }) FreshDescribed.assign(std::istreambuf_iterator<char>(Json), std::istreambuf_iterator<char>());
    const bool FreshOpened = std::filesystem::exists(Opened) && FreshDescribed.find("New model") != std::string::npos;
    Expect("The started workspace opened the new model", FreshOpened ? Utf8(Opened) : "no workspace capture", FreshOpened);

    std::string Unexpected;
    for (const auto& Failure : Failures) Unexpected += (Unexpected.empty() ? "" : "; ") + Failure;
    Expect("Preview renderer accepted every configuration", Failures.empty() ? "no refusals" : Unexpected, Failures.empty());

    // ⑩ report
    int Failed = 0;
    std::ostringstream Report;
    Report << "| Expectation | Observed | Result |\n|-------------|----------|--------|\n";
    for (const ValidationRow& Row : Rows)
    {
        Report << "| " << Row.Expectation << " | " << Row.Observed << " | " << (Row.Passed ? "🟢" : "🔴") << " |\n";
        Failed += Row.Passed ? 0 : 1;
    }
    std::ofstream(Exhibits / "BrowserValidation.md") << Report.str();

    vkDeviceWaitIdle(Device.Device);
    for (const auto& Entry : Descriptors) ImGui_ImplVulkan_RemoveTexture(Entry.second);
    Preview.Finalize();
    ImGui_ImplVulkan_Shutdown();
    ImGui::DestroyContext();
    Device.DestroyBuffer(Readback);
    Device.DestroyImage(Target);
    std::printf("%d of %zu expectations failed\n", Failed, Rows.size());
    return Failed == 0 ? 0 : 1;
}
