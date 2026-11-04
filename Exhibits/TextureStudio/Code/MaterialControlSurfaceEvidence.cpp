/*==========================================================================================================================================
                                                       MATERIALCONTROLSURFACEEVIDENCE.CPP
==========================================================================================================================================*/
// 🧩 Capture actual C++ dropdowns, toggles and colour controls with Vulkan material pixels; no OS window is created.

#include "AtlasReadback.h"
#include <imgui_internal.h>
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
        IO.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
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
        uint64_t Serial = 18;
        uint64_t RenderingRevision = Panel->QueryLibrary().QueryRevision();
        const auto IntegrateInterface = [&]()
        {
            ImGui::NewFrame();
            Panel->ConstructInterface();
            ImGui::Render();
            if (RenderingRevision != Panel->QueryLibrary().QueryRevision())
            {
                Rendering.ConfigureSelection(Panel->QueryLibrary().QuerySelection(), *Panel->QueryLibrary().QueryApplied());
                ++Serial;
                PreviewPixels = Device.Capture(Rendering.QueryPreviewImage(), {512,512}, [&](VkCommandBuffer Commands)
                {
                    Rendering.ConstructCommands(Commands, Serial - 1, Serial);
                }, [&]() { Rendering.ConfirmSubmission(); });
                const auto Extent = Rendering.QueryAtlasExtent();
                AtlasPixels = Device.Capture(Rendering.QueryAtlasImage(), {Extent,Extent}, [](VkCommandBuffer) {}, []() {});
                RenderingRevision = Panel->QueryLibrary().QueryRevision();
            }
        };
        const auto Click = [&](PlaneExtent Extent)
        {
            AssertRasterCondition(Extent.Width() > 0 && Extent.Height() > 0, "Control has no extent.");
            IO.AddMousePosEvent((Extent.MinimumX + Extent.MaximumX) / 2, (Extent.MinimumY + Extent.MaximumY) / 2);
            IntegrateInterface();
            IO.AddMouseButtonEvent(0, true);
            IntegrateInterface();
            IO.AddMouseButtonEvent(0, false);
            IntegrateInterface();
            IntegrateInterface();
        };
        const auto CaptureControls = [&](const char *Filename)
        {
            IO.Fonts->GetTexDataAsRGBA32(&Font, &FontWidth, &FontHeight);
            auto CapturedTextures = Icons.QueryTextures();
            CapturedTextures.push_back({2, AtlasPixels.data(), Edge, Edge});
            CapturedTextures.push_back({3, PreviewPixels.data(), 512,512});
            EncodeLibraryDrawCapture(Destination / Filename, *ImGui::GetDrawData(), Font, FontWidth, FontHeight, CapturedTextures);
        };
        auto *Inspector = ImGui::FindWindowByName("Inspector###FrontierPanel-MaterialInspector");
        ImGuiWindow *Properties = nullptr;
        for (auto *Window : ImGui::GetCurrentContext()->Windows)
            if (Window->ParentWindow == Inspector && std::string(Window->Name).find("/Material properties_") != std::string::npos)
                Properties = Window;
        AssertRasterCondition(Properties != nullptr, "Property scroll region is absent.");
        const auto Toggle = Panel->QueryPaintControlExtent(0);
        ImGui::SetScrollY(Properties, Properties->Scroll.y + Toggle.MinimumY - Properties->InnerRect.Min.y - 80);
        IntegrateInterface();
        IntegrateInterface();
        CaptureControls("NativeFlakePalette.ppm");
        Click(Panel->QueryPaintControlExtent(1));
        IntegrateInterface();
        CaptureControls("NativeDropdown.ppm");
        Click(Panel->QueryMenuOptionExtent(1));
        const auto ModeExtent = Panel->QueryPaintControlExtent(1);
        size_t GradientVertices = 0;
        for (const auto &Vertex : Properties->DrawList->VtxBuffer)
            if (Vertex.pos.x > ModeExtent.MinimumX && Vertex.pos.x < ModeExtent.MaximumX &&
                Vertex.pos.y >= ModeExtent.MinimumY + 44 && Vertex.pos.y <= ModeExtent.MinimumY + 70)
                ++GradientVertices;
        AssertRasterCondition(GradientVertices >= 300, "Ramp gradient was not rasterized in its visible property scroll region.");
        CaptureControls("NativeFlakeRamp.ppm");
        Click(Panel->QueryPaintControlExtent(1));
        IntegrateInterface();
        Click(Panel->QueryMenuOptionExtent(2));
        AssertRasterCondition(Panel->QueryPaintColourCount() == 1, "Single mode still displays multiple boxes.");
        CaptureControls("NativeFlakeSingle.ppm");
        Click(Panel->QueryPaintControlExtent(2));
        IntegrateInterface();
        const auto &Popups = ImGui::GetCurrentContext()->OpenPopupStack;
        AssertRasterCondition(!Popups.empty() && Popups.back().Window, "Colour wheel is absent.");
        auto *Popup = Popups.back().Window;
        const auto BeforeColour = Panel->QueryLibrary().QueryApplied()->Scattering.Paint.Palette[0];
        const auto BeforePixels = PreviewPixels;
        const float Square = Popup->FontRefSize + ImGui::GetStyle().FramePadding.y * 2;
        const float WheelDiameter = 260 - Square - ImGui::GetStyle().ItemInnerSpacing.x;
        const auto Cursor = Popup->DC.CursorStartPos;
        const float WheelX = Cursor.x + (WheelDiameter + Square) * .5f + WheelDiameter * .46f;
        const float WheelY = Cursor.y + Popup->FontRefSize + 8 + WheelDiameter * .5f;
        Click({WheelX - 1, WheelY - 1, WheelX + 1, WheelY + 1});
        AssertRasterCondition(Panel->QueryLibrary().QueryApplied()->Scattering.Paint.Palette[0] != BeforeColour,
            "Colour wheel pointer input did not change the applied material.");
        AssertRasterCondition(PreviewPixels != BeforePixels, "Live colour edit did not change GPU preview pixels.");
        CaptureControls("NativeColourWheel.ppm");
        std::ofstream Report(Destination / "NativeControlSurfaceChecks.json");
        Report << "{\"result\":\"passed\",\"native_imgui_cpu_execution\":true,\"vulkan_material_execution\":true,"
            "\"windows_execution\":false,\"native_window_execution\":false,\"hardware_execution\":false,\"live_colour_gpu_pixels\":true,"
            "\"preview_parameter_allocations\":" << Rendering.QueryPreviewParameterAllocationCount() << ","
            "\"capture\":\"Actual C++ ImGui triangle rasterization plus real Vulkan readbacks; not a Windows screenshot\"}\n";
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
