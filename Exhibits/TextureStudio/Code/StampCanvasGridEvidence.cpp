/*==========================================================================================================================================
                                                       STAMPCANVASGRIDEVIDENCE.CPP
==========================================================================================================================================*/
// 🧩 Headless, CPU-rasterized visual proof for three real Stamp-canvas fixes, all driven through the real
// native StampWorkspacePanel with genuine mouse clicks on the real "Zoom in" and "Show grid" controls (no
// reimplementation of the canvas) and rasterized by the same CPU draw-command rasterizer every other
// headless exhibit in this repository relies on (RasterizeLibraryEvidence.h). No OS window, no GPU:
//   1. The ruler/grid step now adapts to zoom, so "Show grid" still shows lines once zoomed to 1000% —
//      previously a fixed 64-unit stride meant the whole grid could land completely outside the visible,
//      panned board at high zoom, reading as "the grid disappeared and the shape draws over it".
//   2. The grid is still drawn after (on top of) the baked shape texture, so shapes can never obscure it.
//   3. The canvas chrome (outer board well, header/ruler strips) now reads ControlKit::Palette() instead
//      of a hard-coded green tint, so it matches the app's actual neutral dark-charcoal theme.

#include "UI/TextureStudio/StampWorkspacePanel.h"
#include "Engine/ContentInterchange/VectorAuthoringSelectionEditing.h"
#include "RasterizeLibraryEvidence.h"
#include <imgui_internal.h>
#include <filesystem>
#include <iostream>
#include <vector>

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
        // 📝 Mirrors PatternWorkspaceValidation's real headless setup: the native Pattern/Stamp panels'
        // own PanelDockspace rendering needs docking actually enabled (not just the vendored patches
        // compiled in), and the texture atlas path needs RendererHasTextures/TexDesiredFormat set before
        // the first NewFrame, or the very first ConstructInterface() call segfaults inside docking/texture
        // setup that assumes a real backend configured those.
        IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
        IO.DisplaySize = ImVec2(1600, 900);
        IO.DeltaTime = 1.f / 60;
        IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        IO.Fonts->TexDesiredFormat = ImTextureFormat_RGBA32;
        ImFont *Typeface = nullptr;
        if (std::filesystem::is_regular_file("EngineContent/FontArchives/Inter/Inter-Regular.otf"))
            Typeface = IO.Fonts->AddFontFromFileTTF("EngineContent/FontArchives/Inter/Inter-Regular.otf", 14);
        if (!Typeface) Typeface = IO.Fonts->AddFontDefault();
        unsigned char *Pixels = nullptr;
        int AtlasWidth = 0, AtlasHeight = 0;
        IO.Fonts->GetTexDataAsRGBA32(&Pixels, &AtlasWidth, &AtlasHeight);

        ActivationOwnership PanelOwnership(new StampWorkspacePanel());
        auto *Panel = static_cast<StampWorkspacePanel *>(PanelOwnership.get());
        Require(Panel->Initialize() && Panel->Activate(), "Stamp panel lifecycle failed.");
        Panel->ConfigureTypeface(Typeface, Typeface);

        // 📝 A distinctive solid-orange fake "bake": QueryCoverage would normally hand back a real GPU
        // texture handle, but the CPU rasterizer below can sample any RGBA buffer registered under the
        // same token, so this stands in for one without needing a GPU/Vulkan device.
        constexpr uint64_t FakeTextureToken = 42;
        constexpr int FakeTextureExtent = 64;
        std::vector<unsigned char> FakeTexture(size_t(FakeTextureExtent) * FakeTextureExtent * 4);
        for (size_t Index = 0; Index < FakeTexture.size(); Index += 4)
        {
            FakeTexture[Index + 0] = 224; FakeTexture[Index + 1] = 120; FakeTexture[Index + 2] = 40; FakeTexture[Index + 3] = 255;
        }
        Panel->QueryCoverage = [&](const VectorCoverageProgram &, uint32_t) { return FakeTextureToken; };
        Panel->QueryCoverageReady = [] { return true; };

        VectorAuthoringDocument Document;
        Document.Stamp = true;
        VectorCoverageInstruction Primitive;
        Primitive.Bounds = {256, 256, 200, 200};
        ConstructVectorAuthoringPrimitive(Document, Primitive, "Evidence rectangle");
        Panel->ConfigureDocument(Document);

        const auto Frame = [&]
        {
            ImGui::NewFrame();
            Panel->ConstructInterface();
            ImGui::Render();
            Require(ImGui::GetDrawData()->TotalVtxCount > 0, "Stamp canvas produced no ImGui draw commands.");
        };
        for (int Index = 0; Index < 6; ++Index) Frame();

        const auto ClickPoint = [&](ImVec2 Position)
        {
            IO.AddMousePosEvent(Position.x, Position.y); Frame();
            IO.AddMouseButtonEvent(0, true); Frame();
            IO.AddMouseButtonEvent(0, false); Frame(); Frame(); Frame();
        };
        const auto Click = [&](const char *Caption)
        {
            const auto Extent = Panel->QueryControlExtent(Caption);
            Require(Extent.Width() > 0 && Extent.Height() > 0, Caption);
            ClickPoint(ImVec2((Extent.MinimumX + Extent.MaximumX) * .5f, (Extent.MinimumY + Extent.MaximumY) * .5f));
        };
        const auto Capture = [&](const char *Name)
        {
            IO.Fonts->GetTexDataAsRGBA32(&Pixels, &AtlasWidth, &AtlasHeight);
            const std::vector<LibraryCaptureTexture> Textures{{FakeTextureToken, FakeTexture.data(), FakeTextureExtent, FakeTextureExtent}};
            EncodeLibraryDrawCapture(Output / (std::string(Name) + ".ppm"), *ImGui::GetDrawData(), Pixels, AtlasWidth, AtlasHeight, Textures);
        };

        Capture("01-initial-100pct");

        // Turn on the real "Show grid" toggle via the real "Stamp more tools" dropdown.
        Click("More tools");
        Click("Show grid");
        IO.AddKeyEvent(ImGuiKey_Escape, true); Frame();
        IO.AddKeyEvent(ImGuiKey_Escape, false); Frame(); Frame();
        Capture("02-grid-on-100pct");

        // Zoom all the way in via the real "Zoom in" button: 1.25x per click, 1.0 -> 10.0 (1000%, the
        // documented ceiling) in well under 12 clicks, each clamped by ApplyWorkspaceZoomStep.
        for (int Index = 0; Index < 12; ++Index) Click("Zoom in");
        Capture("03-grid-on-1000pct");

        ImGui::DestroyContext();
        std::cout << "PASS: captured the real Stamp canvas at 100% and 1000% zoom with \"Show grid\" enabled "
            "over a baked shape - see " << Output.string() << "\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "FAIL: " << Failure.what() << "\n";
        return 1;
    }
}
