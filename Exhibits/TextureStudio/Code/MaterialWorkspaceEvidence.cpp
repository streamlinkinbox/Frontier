/*==========================================================================================================================================
                                                       MATERIALWORKSPACEEVIDENCE.CPP
==========================================================================================================================================*/
// 🧩 Compose actual native ImGui commands with actual Vulkan atlas/preview readbacks for review; no OS window is created.

#include "AtlasReadback.h"
#include "RasterizeLibraryEvidence.h"
#include "WorkspaceIconEvidence.h"
#include "UI/TextureStudio/MaterialLibraryPanel.h"
#include "Engine/Render/MaterialLibraryRendering.h"

int main(int Count, char **Arguments)
{
    using namespace Frontier;
    try
    {
        AssertRasterCondition(Count == 5, "Expected driver, shaders, library and destination.");
        AtlasValidationDevice Device(Arguments[1], VK_API_VERSION_1_2);
        MaterialLibraryRendering Rendering;
        Rendering.Initialize(Device.QueryAccess(), Arguments[2]);
        Rendering.ConfigureEnvironments("EngineContent/EnvironmentArchives");
        struct RenderingRetirement
        {
            RasterValidationDevice &Device;
            ~RenderingRetirement() { (void)Device.DeviceWaitIdle(Device.Device); }
        } Retirement{Device};
        ImGui::CreateContext();
        auto &IO = ImGui::GetIO();
        IO.IniFilename = nullptr;
        IO.DisplaySize = ImVec2(1440, 1000);
        IO.DeltaTime = 1.f / 60;
        if (std::filesystem::is_regular_file("EngineContent/FontArchives/DMSans/DMSans-Regular.ttf"))
            IO.Fonts->AddFontFromFileTTF("EngineContent/FontArchives/DMSans/DMSans-Regular.ttf", 14);
        else IO.Fonts->AddFontDefault();
        auto *LightTypeface = std::filesystem::is_regular_file("EngineContent/FontArchives/DMSans/DMSans-Light.ttf") ?
            IO.Fonts->AddFontFromFileTTF("EngineContent/FontArchives/DMSans/DMSans-Light.ttf", 14) : nullptr;
        unsigned char *Font = nullptr;
        int FontWidth = 0, FontHeight = 0;
        IO.Fonts->GetTexDataAsRGBA32(&Font, &FontWidth, &FontHeight);
        IO.Fonts->SetTexID(ImTextureID(1));
        ActivationOwnership Ownership(new MaterialLibraryPanel());
        auto *Panel = static_cast<MaterialLibraryPanel *>(Ownership.get());
        AssertRasterCondition(Panel->Initialize() && Panel->Activate() && Panel->ConfigureDirectory(Arguments[3]) &&
            Panel->SelectMaterial("racing-green.material"), "Material evidence library did not initialize.");
        WorkspaceIconEvidence Icons;
        Icons.Configure(*Panel);
        Panel->ConfigureTypeface(IO.Fonts->Fonts[0], LightTypeface);
        Panel->RequestThumbnails = [&](const auto &Paths) { Rendering.RequestLibrary(Panel->QueryLibrary().QueryDirectory(), Paths); };
        Panel->QueryThumbnail = [&](const auto &Path)
        {
            const auto Slot = Rendering.QueryThumbnail(Path);
            const auto &Uv = Slot.Uv;
            return MaterialPreviewImage{Slot.Ready ? 2ull : 0ull, Uv.MinimumU, Uv.MinimumV, Uv.MaximumU, Uv.MaximumV};
        };
        Panel->ConfigurePreview = [&](const auto &Configuration) { Rendering.ConfigurePreview(Configuration); };
        Panel->QueryEnvironmentNames = [&]() { return Rendering.QueryEnvironmentNames(); };
        Panel->QueryPreview = [&]() { return MaterialPreviewImage{Rendering.QueryPreviewReady() ? 3ull : 0ull}; };
        Rendering.RequestLibrary(Panel->QueryLibrary().QueryDirectory(), {});
        Rendering.ConfigureSelection(Panel->QueryLibrary().QuerySelection(), *Panel->QueryLibrary().QueryApplied());
        std::vector<unsigned char> AtlasPixels, PreviewPixels;
        for (uint64_t Serial = 1; Serial <= 18; ++Serial)
        {
            ImGui::NewFrame();
            Panel->ConstructInterface();
            ImGui::Render();
            const bool Preview = !Rendering.QueryPreviewReady();
            const uint32_t Extent = Preview ? 512 : Rendering.QueryAtlasExtent();
            auto Pixels = Device.Capture(Preview ? Rendering.QueryPreviewImage() : Rendering.QueryAtlasImage(), {Extent, Extent},
                [&](VkCommandBuffer Commands) { Rendering.ConstructCommands(Commands, Serial - 1, Serial); },
                [&]() { Rendering.ConfirmSubmission(); });
            if (Preview) PreviewPixels = std::move(Pixels);
            else AtlasPixels = std::move(Pixels);
        }
        AssertRasterCondition(Rendering.QueryBakeCount() >= 8 && Rendering.QueryPreviewReady(), "Visible HTML library thumbnails were not baked.");
        const int Edge = int(Rendering.QueryAtlasExtent());
        auto Textures = Icons.QueryTextures();
        Textures.push_back({2, AtlasPixels.data(), Edge, Edge});
        Textures.push_back({3, PreviewPixels.data(), 512, 512});
        const std::filesystem::path Destination(Arguments[4]);
        std::filesystem::create_directories(Destination);
        IO.Fonts->GetTexDataAsRGBA32(&Font, &FontWidth, &FontHeight);
        EncodeLibraryDrawCapture(Destination / "MaterialWorkspace.ppm", *ImGui::GetDrawData(), Font, FontWidth, FontHeight, Textures);
        std::ofstream Report(Destination / "MaterialWorkspaceChecks.json");
        Report << "{\"result\":\"passed\",\"native_imgui_cpu_execution\":true,\"vulkan_material_execution\":true,"
            "\"native_window_execution\":false,\"windows_execution\":false,\"width\":1440,\"height\":1000,\"atlas_images\":1,\"baked_thumbnails\":" << Rendering.QueryBakeCount() << ","
            "\"capture\":\"CPU ImGui triangle composition using real Vulkan atlas and preview readbacks; not a Windows screenshot\"}\n";
        Ownership.reset();
        ImGui::DestroyContext();
        std::cout << "PASS: docked native controls composed with real Vulkan material atlas and preview pixels\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
        std::cerr << Failure.what() << '\n';
        return 1;
    }
}
