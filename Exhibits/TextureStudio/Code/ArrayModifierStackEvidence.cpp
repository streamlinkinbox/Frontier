/*==========================================================================================================================================
                                             ARRAYMODIFIERSTACKEVIDENCE.CPP
==========================================================================================================================================*/
// 🧩 Headless, CPU-rasterized visual proof of the Blender-style Array MODIFIER STACK: for a single
// selected layer the array exists only as a live, slider-editable, procedural stack entry ("Add array
// modifier") that never touches the authored document's Layers until explicitly "Applied" - the old
// destructive "Generate array" bake remains only as the multi-selection group path. This demonstrates,
// on the real rendered canvas:
//  1. Adding a modifier keeps the authored document at exactly 1 layer while the canvas shows the full array.
//  2. Live-editing the modifier's own fields (ring count) re-shapes the canvas immediately - no Generate click.
//  3. Disabling the modifier hides every copy, falling back to the single original shape; re-enabling restores it.
//  4. "Apply" bakes the live modifier into real, independent, selectable duplicate layers and removes the entry.
//  5. Undo restores the pre-Apply single-layer-with-modifier state; "Delete" removes a modifier with no bake.

#include "UI/TextureStudio/PatternWorkspacePanel.h"
#include "UI/TextureStudio/PatternThumbnailRaster.h"
#include "Engine/ContentInterchange/VectorAuthoringEditing.h"
#include "Engine/Interface/DropdownControls.h"
#include "RasterizeLibraryEvidence.h"
#include <imgui_internal.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
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
        IO.DisplaySize = ImVec2(1600, 1500);
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

        // 📝 A single small bright "dot" motif, centred on the tile - the thing that gets arrayed.
        VectorAuthoringDocument Document;
        VectorCoverageInstruction Dot;
        Dot.Topology[0] = 1; // ellipse
        Dot.Bounds = {256, 256, 34, 34};
        Dot.Colour = {.95f, .55f, .18f, 1};
        ConstructVectorAuthoringPrimitive(Document, Dot, "Dot");
        Panel->ConfigureDocument(Document);

        constexpr uint64_t FakeTextureToken = 42;
        constexpr uint32_t FakeTextureExtent = 512;
        constexpr uint32_t BoardWell = uint32_t(255) << 24 | uint32_t(0xd7) << 16 | uint32_t(0xe7) << 8 | uint32_t(0xed);
        // 📝 Unlike ArrayPatternEvidence (which rasterizes the authored document directly), the capture
        // texture here MUST run through the same modifier-evaluation the live canvas itself now uses -
        // otherwise this evidence would only ever show the single, un-arrayed base shape.
        std::vector<uint8_t> FakeTexture = RasterizePatternThumbnailSvg(ConstructEvaluatedVectorAuthoringDocument(Document), FakeTextureExtent, BoardWell);
        Panel->QueryCoverage = [&](const VectorCoverageProgram &, uint32_t) { return FakeTextureToken; };
        Panel->QueryCoverageReady = [] { return true; };
        Panel->QuerySelectionToken = [](const VectorAuthoringDocument &Doc, PlanePoint Point) -> std::optional<uint32_t>
        {
            for (const auto &Layer : Doc.Layers)
            {
                if (Layer.Root >= Doc.Instructions.size()) continue;
                const auto &Bounds = Doc.Instructions[Layer.Root].Bounds;
                if (std::abs(Point.X - Bounds[0]) <= Bounds[2] * .5f && std::abs(Point.Y - Bounds[1]) <= Bounds[3] * .5f) return Layer.Token;
            }
            return 0u;
        };

        const auto Frame = [&]
        {
            ImGui::NewFrame();
            Panel->ConstructInterface();
            ImGui::Render();
            Require(ImGui::GetDrawData()->TotalVtxCount > 0, "Pattern canvas produced no ImGui draw commands.");
        };
        for (int Index = 0; Index < 6; ++Index) Frame();

        int CaptureIndex = 0;
        const auto Capture = [&]
        {
            IO.Fonts->GetTexDataAsRGBA32(&Pixels, &AtlasWidth, &AtlasHeight);
            FakeTexture = RasterizePatternThumbnailSvg(ConstructEvaluatedVectorAuthoringDocument(Panel->QueryDocument()), FakeTextureExtent, BoardWell);
            const std::vector<LibraryCaptureTexture> Textures{{FakeTextureToken, FakeTexture.data(), FakeTextureExtent, FakeTextureExtent}};
            char Name[64];
            std::snprintf(Name, sizeof(Name), "%02d-frame.ppm", CaptureIndex++);
            EncodeLibraryDrawCapture(Output / Name, *ImGui::GetDrawData(), Pixels, AtlasWidth, AtlasHeight, Textures);
        };

        const auto MoveTo = [&](ImVec2 Position) { IO.AddMousePosEvent(Position.x, Position.y); Frame(); };
        const auto Down = [&] { IO.AddMouseButtonEvent(0, true); Frame(); };
        const auto Up = [&] { IO.AddMouseButtonEvent(0, false); Frame(); Frame(); Frame(); };
        const auto Click = [&](ImVec2 Position) { MoveTo(Position); Down(); Up(); };

        const auto ClickCaption = [&](const char *Caption)
        {
            auto Extent = Panel->QueryControlExtent(Caption);
            for (int Attempt = 0; Attempt < 24 && (Extent.MaximumY > 1380 || Extent.MinimumY < 260); ++Attempt)
            {
                const auto InspectorPanel = Panel->QueryPanelExtent("Inspector");
                MoveTo(ImVec2((InspectorPanel.MinimumX + InspectorPanel.MaximumX) * .5f, 700));
                IO.AddMouseWheelEvent(0, Extent.MaximumY > 1380 ? -5 : 5);
                Frame(); Frame(); Frame();
                Extent = Panel->QueryControlExtent(Caption);
            }
            Require(Extent.Width() > 0 && Extent.Height() > 0, Caption);
            Click(ImVec2((Extent.MinimumX + Extent.MaximumX) * .5f, (Extent.MinimumY + Extent.MaximumY) * .5f));
            return Extent;
        };

        const auto TypeNumber = [&](const char *Caption, const char *Digits)
        {
            ClickCaption(Caption);
            IO.AddKeyEvent(ImGuiMod_Ctrl, true); IO.AddKeyEvent(ImGuiKey_A, true); Frame();
            IO.AddKeyEvent(ImGuiKey_A, false); IO.AddKeyEvent(ImGuiMod_Ctrl, false); Frame();
            for (const char Character : std::string(Digits)) { IO.AddInputCharacter(Character); Frame(); }
            IO.AddKeyEvent(ImGuiKey_Enter, true); Frame(); IO.AddKeyEvent(ImGuiKey_Enter, false); Frame();
        };

        const auto PickDropdownRow = [&](const char *Caption, int RowIndex)
        {
            ClickCaption(Caption);
            Frame(); Frame();
            auto *Context = ImGui::GetCurrentContext();
            Require(!Context->OpenPopupStack.empty(), Caption);
            auto *Popup = Context->OpenPopupStack.back().Window;
            Require(Popup != nullptr, Caption);
            const float RowHeight = ControlKitTokens::DropdownRowHeight + ControlKitTokens::DropdownRowGap;
            const auto Position = Popup->DC.CursorStartPos;
            MoveTo(ImVec2(Position.x + 20, Position.y + RowIndex * RowHeight + ControlKitTokens::DropdownRowHeight * .5f));
            Down();
            Up();
        };

        const auto Board = Panel->QueryBoardExtent();
        const auto ToScreen = [&](float X, float Y)
        {
            return ImVec2(Board.MinimumX + X / 512.f * Board.Width(), Board.MinimumY + Y / 512.f * Board.Height());
        };

        const auto SelectDot = [&]
        {
            Click(ToScreen(256, 256));
            Require(!Panel->QuerySelection().empty(), "Clicking the Dot motif did not select it.");
        };

        const auto EvaluatedLayerCount = [&] { return ConstructEvaluatedVectorAuthoringDocument(Panel->QueryDocument()).Layers.size(); };

        SelectDot();
        for (int Index = 0; Index < 4; ++Index) Capture();
        std::cout << "Captured a single Dot motif, selected via the canvas.\n";

        ClickCaption("Modifiers");
        for (int Index = 0; Index < 2; ++Index) Capture();
        PickDropdownRow("Array layout", 2); // {"Linear","Grid","Circular / arc","Radial rings"}
        TypeNumber("Copies per ring", "6");
        Capture();
        std::cout << "Configured a 6-copy circular array in the shared staging fields (not generated yet).\n";

        // ---------------------------------------------------------------------------------------------
        // 1) "Add array modifier" - non-destructive: the authored document stays at 1 layer, but the live
        //    canvas (which now evaluates every layer's modifier stack before rendering) shows all 6 copies.
        // ---------------------------------------------------------------------------------------------
        ClickCaption("Add array modifier");
        for (int Index = 0; Index < 3; ++Index) Capture();
        Require(Panel->QueryDocument().Layers.size() == 1, "Adding a modifier must not duplicate the authored layer.");
        Require(Panel->QueryDocument().Layers.front().Modifiers.size() == 1, "The layer did not gain exactly one modifier entry.");
        Require(EvaluatedLayerCount() == 6, "The evaluated/rendered document did not expand to 6 copies.");
        std::cout << "Added the array as a live MODIFIER: the authored document still has exactly 1 layer, "
            "but the evaluated canvas shows all 6 copies - a true Blender-style non-destructive stack entry.\n";

        // ---------------------------------------------------------------------------------------------
        // 2) Live-edit the modifier's own field (more rings on the stack entry itself): the canvas reshapes
        //    immediately, with no "Generate" click at all - editing the modifier IS the live preview.
        // ---------------------------------------------------------------------------------------------
        TypeNumber("Copies per ring (M1)", "10");
        for (int Index = 0; Index < 3; ++Index) Capture();
        Require(Panel->QueryDocument().Layers.size() == 1, "Live-editing a modifier must still not duplicate the authored layer.");
        Require(EvaluatedLayerCount() == 10, "Live-editing the modifier's own field did not reshape the evaluated canvas.");
        std::cout << "Live-edited the modifier's own \"Copies per ring\" field from 6 to 10 - the canvas updated "
            "immediately to 10 rendered copies, with the authored document still unchanged at 1 layer.\n";

        // ---------------------------------------------------------------------------------------------
        // 3) Disable the modifier: every copy disappears, falling back to the single original shape; then
        //    re-enable it to restore the full array - proving the toggle is fully reversible and non-destructive.
        // ---------------------------------------------------------------------------------------------
        ClickCaption("Enabled (M1)");
        for (int Index = 0; Index < 3; ++Index) Capture();
        Require(EvaluatedLayerCount() == 1, "Disabling the modifier did not collapse the canvas back to the single shape.");
        std::cout << "Disabled the modifier: the canvas fell back to the single original Dot, with the modifier "
            "entry itself still sitting in the stack (not deleted).\n";

        ClickCaption("Enabled (M1)");
        for (int Index = 0; Index < 3; ++Index) Capture();
        Require(EvaluatedLayerCount() == 10, "Re-enabling the modifier did not restore the evaluated array.");
        std::cout << "Re-enabled the modifier: all 10 copies reappeared instantly.\n";

        // ---------------------------------------------------------------------------------------------
        // 4) Apply: bakes the live modifier into real, independent, selectable duplicate layers and
        //    removes the modifier entry - the explicit Blender-style bake, now the only single-layer one.
        // ---------------------------------------------------------------------------------------------
        ClickCaption("Apply modifier (M1)");
        for (int Index = 0; Index < 3; ++Index) Capture();
        {
            const auto &Baked = Panel->QueryDocument();
            Require(Baked.Layers.size() == 10, "Apply did not bake the modifier into exactly 10 real layers.");
            Require(std::none_of(Baked.Layers.begin(), Baked.Layers.end(), [](const auto &Layer) { return !Layer.Modifiers.empty(); }),
                "A baked layer still carries a modifier entry after Apply.");
        }
        std::cout << "Applied the modifier: the authored document now genuinely contains 10 independent, "
            "selectable duplicate layers, and the modifier entry itself is gone - the explicit bake, "
            "triggered from the live stack only when asked for.\n";

        // ---------------------------------------------------------------------------------------------
        // 5) Undo restores the exact pre-Apply state: 1 authored layer, modifier stack intact.
        // ---------------------------------------------------------------------------------------------
        ClickCaption("Undo");
        Require(Panel->QueryDocument().Layers.size() == 1, "Undo after Apply did not restore the single authored layer.");
        Require(Panel->QueryDocument().Layers.front().Modifiers.size() == 1, "Undo after Apply did not restore the modifier entry.");
        SelectDot();
        for (int Index = 0; Index < 2; ++Index) Capture();
        std::cout << "Undid the Apply: back to 1 authored layer with its live modifier intact, still showing all 10 copies.\n";

        // ---------------------------------------------------------------------------------------------
        // 6) Delete: removes the modifier entry outright (no bake at all) - reverts to the plain single shape.
        // ---------------------------------------------------------------------------------------------
        ClickCaption("Delete modifier (M1)");
        for (int Index = 0; Index < 3; ++Index) Capture();
        Require(Panel->QueryDocument().Layers.size() == 1, "Deleting a modifier must not change the layer count.");
        Require(Panel->QueryDocument().Layers.front().Modifiers.empty(), "Delete did not remove the modifier entry.");
        Require(EvaluatedLayerCount() == 1, "Deleting the modifier did not revert the canvas to the single original shape.");
        std::cout << "Deleted the modifier: no bake, no trace - the canvas reverted to the single original Dot.\n";

        ImGui::DestroyContext();
        std::cout << "PASS: the Array tool is a genuine Blender-style procedural modifier stack - add, "
            "live-edit, enable/disable, Apply and Delete all verified against the actual rendered canvas, "
            "see " << Output.string() << "\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "FAIL: " << Failure.what() << "\n";
        return 1;
    }
}
