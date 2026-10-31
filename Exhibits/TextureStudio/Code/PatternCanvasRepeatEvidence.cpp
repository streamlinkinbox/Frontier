/*==========================================================================================================================================
                                                      PATTERNCANVASREPEATEVIDENCE.CPP
==========================================================================================================================================*/
// 🧩 Headless, CPU-rasterized visual proof for the Pattern-canvas-specific parts of the same batch of
// fixes as StampCanvasGridEvidence.cpp, driven through the real native PatternWorkspacePanel with genuine
// mouse clicks (no reimplementation of the canvas):
//   1. The standalone "Seamless repeat preview" panel that used to sit below the canvas is gone.
//   2. Its job is now a "Preview seamless repeat" toggle in the real "Pattern more tools" dropdown that
//      tiles the same baked texture directly across the canvas itself.
//   3. The canvas chrome matches the app's neutral dark-charcoal theme instead of a hard-coded green tint.

#include "UI/TextureStudio/PatternWorkspacePanel.h"
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

        ActivationOwnership PanelOwnership(new PatternWorkspacePanel());
        auto *Panel = static_cast<PatternWorkspacePanel *>(PanelOwnership.get());
        Require(Panel->Initialize() && Panel->Activate(), "Pattern panel lifecycle failed.");
        Panel->ConfigureTypeface(Typeface, Typeface);

        // 📝 A small, visually asymmetric fake "bake" (two-tone, not a flat fill) so a tiled repeat is
        // unmistakably different from a single centred draw in the captured frame.
        constexpr uint64_t FakeTextureToken = 42;
        constexpr int FakeTextureExtent = 64;
        std::vector<unsigned char> FakeTexture(size_t(FakeTextureExtent) * FakeTextureExtent * 4);
        for (int Y = 0; Y < FakeTextureExtent; ++Y)
            for (int X = 0; X < FakeTextureExtent; ++X)
            {
                const size_t Address = (size_t(Y) * FakeTextureExtent + X) * 4;
                const bool Quadrant = (X < FakeTextureExtent / 2) != (Y < FakeTextureExtent / 2);
                FakeTexture[Address + 0] = Quadrant ? 224 : 70;
                FakeTexture[Address + 1] = Quadrant ? 120 : 150;
                FakeTexture[Address + 2] = Quadrant ? 40 : 210;
                FakeTexture[Address + 3] = 255;
            }
        Panel->QueryCoverage = [&](const VectorCoverageProgram &, uint32_t) { return FakeTextureToken; };
        Panel->QueryCoverageReady = [] { return true; };

        VectorAuthoringDocument Document;
        VectorCoverageInstruction Primitive;
        Primitive.Bounds = {256, 256, 200, 200};
        ConstructVectorAuthoringPrimitive(Document, Primitive, "Evidence motif");
        Panel->ConfigureDocument(Document);

        const auto Frame = [&]
        {
            ImGui::NewFrame();
            Panel->ConstructInterface();
            ImGui::Render();
            Require(ImGui::GetDrawData()->TotalVtxCount > 0, "Pattern canvas produced no ImGui draw commands.");
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

        Capture("01-single-tile");

        // Turn on the real "Preview seamless repeat" toggle via the real "Pattern more tools" dropdown:
        // the motif should now tile directly across the canvas instead of drawing once, centred.
        Click("More tools");
        Click("Preview seamless repeat");
        IO.AddKeyEvent(ImGuiKey_Escape, true); Frame();
        IO.AddKeyEvent(ImGuiKey_Escape, false); Frame(); Frame();
        Capture("02-tiled-in-canvas");

        ImGui::DestroyContext();
        std::cout << "PASS: captured the real Pattern canvas with a single centred bake, then the real "
            "\"Preview seamless repeat\" toggle tiling the same bake directly across the canvas - see "
            << Output.string() << "\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "FAIL: " << Failure.what() << "\n";
        return 1;
    }
}
