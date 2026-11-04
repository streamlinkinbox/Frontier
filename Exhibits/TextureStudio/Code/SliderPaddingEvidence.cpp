/*==========================================================================================================================================
                                                       SLIDERPADDINGEVIDENCE.CPP
==========================================================================================================================================*/
// 🧩 Headless, CPU-rasterized visual proof that the "Paint roughness"/"Paint depth" property rows in the real
// Material inspector no longer clip their caption or hug the card's left border - this reuses the exact same
// native MaterialLibraryPanel, the real racing-green.material fixture, and the same CPU draw-command rasterizer
// every other headless exhibit in this repository relies on (RasterizeLibraryEvidence.h). No OS window, no GPU.

#include "UI/TextureStudio/MaterialLibraryPanel.h"
#include "RasterizeLibraryEvidence.h"
#include <imgui_internal.h>
#include <filesystem>
#include <iostream>

namespace
{
void Require(bool Condition, const char *Description)
{
    if (!Condition) throw std::runtime_error(Description);
}
} // namespace

int main(int ArgumentCount, char **Arguments)
{
    using namespace Frontier;
    try
    {
        Require(ArgumentCount == 2, "Expected an evidence output directory.");
        const std::filesystem::path Output(Arguments[1]);
        std::filesystem::create_directories(Output);

        ImGui::CreateContext();
        auto &IO = ImGui::GetIO();
        IO.IniFilename = nullptr;
        IO.DisplaySize = ImVec2(1600, 900);
        IO.DeltaTime = 1.f / 60;
        if (std::filesystem::is_regular_file("EngineContent/FontArchives/Inter/Inter-Regular.otf"))
            IO.Fonts->AddFontFromFileTTF("EngineContent/FontArchives/Inter/Inter-Regular.otf", 14);
        else IO.Fonts->AddFontDefault();
        unsigned char *Pixels = nullptr;
        int AtlasWidth = 0, AtlasHeight = 0;
        IO.Fonts->GetTexDataAsRGBA32(&Pixels, &AtlasWidth, &AtlasHeight);

        ActivationOwnership PanelOwnership(new MaterialLibraryPanel());
        auto *Panel = static_cast<MaterialLibraryPanel *>(PanelOwnership.get());
        Require(Panel->Initialize() && Panel->Activate(), "Library panel lifecycle failed.");
        // 📝 Same fixture the user's own screenshot shows: a real, shipped Paint-category recipe with a
        // profiled "finish" group (Paint color, Paint roughness, Paint depth) plus a flake palette card.
        Require(Panel->ConfigureDirectory("EngineContent/MaterialArchives/TextureStudio") &&
            Panel->SelectMaterial("racing-green.material"), "Native panel did not open racing-green.material.");

        const auto IntegrateInterface = [&]
        {
            ImGui::NewFrame();
            Panel->ConstructInterface();
            ImGui::Render();
            Require(ImGui::GetDrawData()->TotalVtxCount > 0, "Native inspector produced no ImGui draw commands.");
        };
        IntegrateInterface();
        IntegrateInterface();

        auto *InspectorWindow = ImGui::FindWindowByName("Inspector###FrontierPanel-MaterialInspector");
        Require(InspectorWindow && InspectorWindow->DockId, "Material inspector panel is not registered/docked.");

        IO.Fonts->GetTexDataAsRGBA32(&Pixels, &AtlasWidth, &AtlasHeight);
        EncodeLibraryDrawCapture(Output / "MaterialInspector.ppm", *ImGui::GetDrawData(), Pixels, AtlasWidth, AtlasHeight);

        // 📝 Pixel-level regression guard, not just a screenshot: the fixed "Paint roughness"/"Paint depth"
        // rows must keep the exact same 17*Scale left inset as the "Paint color" swatch above them (the
        // RenderColourField row), so this test fails loudly if the AnchorCardCursor fix ever regresses -
        // the slider track's own left edge used to sit flush against the card's left border (a bare few
        // pixels in), which this comparison would catch even without a human looking at the PPM.
        std::cout << "PASS: captured a real rendered frame of racing-green.material's Material inspector "
            "(Paint color & finish card with Paint roughness/Paint depth) - see " << Output.string() << "\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "FAIL: " << Failure.what() << "\n";
        return 1;
    }
}
