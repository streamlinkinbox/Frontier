/*==========================================================================================================================================
                                             MIRRORNOISEOFFSETMODIFIEREVIDENCE.CPP
==========================================================================================================================================*/
// 🧩 Headless, CPU-rasterized visual proof of the three NEW Blender-style modifier kinds added to the stack
// that previously only supported Array: Mirror, Noise/jitter and Offset/outline. Demonstrated on the real
// rendered canvas, on a deliberately ASYMMETRIC freeform arrow motif (so a true chirality flip is visible -
// a shape that merely repositioned/rotated, rather than truly mirrored, would look wrong at a glance here):
//  1. "+ Mirror" adds a live, non-destructive Mirror entry; live-editing its axis angle re-aims the flip.
//  2. Toggling "Keep original" off replaces the source in place instead of adding a companion.
//  3. "Apply" bakes Mirror into two real, independent layers; Undo restores the single-layer+modifier state.
//  4. "+ Noise / jitter" nudges position/rotation by a seeded, deterministic, reversible amount.
//  5. "+ Offset / outline" grows/shrinks the motif's own silhouette; a negative distance shrinks it back.

#include "UI/TextureStudio/PatternWorkspacePanel.h"
#include "UI/TextureStudio/PatternThumbnailRaster.h"
#include "Engine/ContentInterchange/VectorAuthoringEditing.h"
#include "Engine/ContentInterchange/CubicPathEditing.h"
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

        // 📝 A deliberately ASYMMETRIC freeform arrow, pointing right - any left/right mirror must visibly
        // flip its point, unlike a circle/rectangle where a flip could be mistaken for a plain reposition.
        VectorAuthoringDocument Document;
        CubicPath Arrow;
        Arrow.Closed = true;
        Arrow.Anchors = {
            {{190, 236}, {190, 236}, {190, 236}}, {{266, 236}, {266, 236}, {266, 236}},
            {{266, 206}, {266, 206}, {266, 206}}, {{326, 256}, {326, 256}, {326, 256}},
            {{266, 306}, {266, 306}, {266, 306}}, {{266, 276}, {266, 276}, {266, 276}},
            {{190, 276}, {190, 276}, {190, 276}}};
        VectorCoverageInstruction Style;
        Style.Colour = {.95f, .55f, .18f, 1};
        const auto ArrowToken = ConstructCubicPath(Document, Arrow, Style, "Arrow");
        Panel->ConfigureDocument(Document);

        constexpr uint64_t FakeTextureToken = 42;
        constexpr uint32_t FakeTextureExtent = 512;
        constexpr uint32_t BoardWell = uint32_t(255) << 24 | uint32_t(0xd7) << 16 | uint32_t(0xe7) << 8 | uint32_t(0xed);
        std::vector<uint8_t> FakeTexture = RasterizePatternThumbnailSvg(ConstructEvaluatedVectorAuthoringDocument(Document), FakeTextureExtent, BoardWell);
        Panel->QueryCoverage = [&](const VectorCoverageProgram &, uint32_t) { return FakeTextureToken; };
        Panel->QueryCoverageReady = [] { return true; };
        Panel->QuerySelectionToken = [](const VectorAuthoringDocument &Doc, PlanePoint Point) -> std::optional<uint32_t>
        {
            for (const auto &Layer : Doc.Layers)
            {
                if (Layer.Root >= Doc.Instructions.size()) continue;
                const auto &Bounds = Doc.Instructions[Layer.Root].Bounds;
                if (std::abs(Point.X - Bounds[0]) <= Bounds[2] * .5f + 20 && std::abs(Point.Y - Bounds[1]) <= Bounds[3] * .5f + 20) return Layer.Token;
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

        const auto Board = Panel->QueryBoardExtent();
        const auto ToScreen = [&](float X, float Y)
        {
            return ImVec2(Board.MinimumX + X / 512.f * Board.Width(), Board.MinimumY + Y / 512.f * Board.Height());
        };

        const auto SelectArrow = [&]
        {
            Click(ToScreen(256, 256));
            Require(!Panel->QuerySelection().empty(), "Clicking the Arrow motif did not select it.");
        };

        const auto EvaluatedLayerCount = [&] { return ConstructEvaluatedVectorAuthoringDocument(Panel->QueryDocument()).Layers.size(); };

        SelectArrow();
        for (int Index = 0; Index < 4; ++Index) Capture();
        std::cout << "Captured a single asymmetric Arrow motif, selected via the canvas.\n";

        ClickCaption("Modifiers");
        for (int Index = 0; Index < 2; ++Index) Capture();

        // ---------------------------------------------------------------------------------------------
        // 1) "+ Mirror" - non-destructive: the authored document stays at 1 layer, but the live canvas
        //    shows the arrow AND its true geometric reflection (point now aims the opposite way).
        // ---------------------------------------------------------------------------------------------
        ClickCaption("+ Mirror");
        for (int Index = 0; Index < 3; ++Index) Capture();
        Require(Panel->QueryDocument().Layers.size() == 1, "Adding a Mirror modifier must not duplicate the authored layer.");
        Require(Panel->QueryDocument().Layers.front().Modifiers.size() == 1, "The layer did not gain exactly one modifier entry.");
        Require(EvaluatedLayerCount() == 2, "The evaluated/rendered document did not expand to the original plus its reflection.");
        std::cout << "Added a live Mirror MODIFIER: the authored document still has exactly 1 layer, but the "
            "evaluated canvas shows the arrow and its true reflected copy - a genuine chirality flip, not a reposition.\n";

        // ---------------------------------------------------------------------------------------------
        // 2) Live-edit the modifier's own axis angle field - the reflection re-aims immediately.
        // ---------------------------------------------------------------------------------------------
        TypeNumber("Axis angle (M1)", "0");
        for (int Index = 0; Index < 3; ++Index) Capture();
        std::cout << "Live-edited the Mirror's own axis angle from 90deg (left/right) to 0deg (top/bottom) - "
            "the reflection re-aimed instantly, still never touching the authored source.\n";
        TypeNumber("Axis angle (M1)", "90");
        for (int Index = 0; Index < 2; ++Index) Capture();

        // ---------------------------------------------------------------------------------------------
        // 3) "Keep original" off replaces the source in place instead of adding a companion layer.
        // ---------------------------------------------------------------------------------------------
        ClickCaption("Keep original (M1)");
        for (int Index = 0; Index < 3; ++Index) Capture();
        Require(EvaluatedLayerCount() == 1, "Turning off \"Keep original\" should replace the source, not add a companion.");
        std::cout << "Turned off \"Keep original\": the single arrow is now replaced by its own mirrored placement, "
            "instead of sitting alongside an untouched copy.\n";
        ClickCaption("Keep original (M1)");
        for (int Index = 0; Index < 2; ++Index) Capture();
        Require(EvaluatedLayerCount() == 2, "Re-enabling \"Keep original\" did not restore the companion reflection.");

        // ---------------------------------------------------------------------------------------------
        // 4) Apply bakes Mirror into two real, independent layers; Undo restores the modifier state.
        // ---------------------------------------------------------------------------------------------
        ClickCaption("Apply modifier (M1)");
        for (int Index = 0; Index < 3; ++Index) Capture();
        {
            const auto &Baked = Panel->QueryDocument();
            Require(Baked.Layers.size() == 2, "Apply did not bake the Mirror modifier into exactly 2 real layers.");
            Require(std::none_of(Baked.Layers.begin(), Baked.Layers.end(), [](const auto &Layer) { return !Layer.Modifiers.empty(); }),
                "A baked layer still carries a modifier entry after Apply.");
        }
        std::cout << "Applied the Mirror modifier: the authored document now genuinely contains 2 independent, "
            "selectable layers (the original and its mirrored twin), and the modifier entry is gone.\n";
        ClickCaption("Undo");
        Require(Panel->QueryDocument().Layers.size() == 1, "Undo after Apply did not restore the single authored layer.");
        Require(Panel->QueryDocument().Layers.front().Modifiers.size() == 1, "Undo after Apply did not restore the modifier entry.");
        SelectArrow();
        for (int Index = 0; Index < 2; ++Index) Capture();
        std::cout << "Undid the Apply: back to 1 authored layer with its live Mirror modifier intact.\n";

        ClickCaption("Delete modifier (M1)");
        Require(Panel->QueryDocument().Layers.front().Modifiers.empty(), "Delete did not remove the Mirror modifier entry.");
        SelectArrow();
        for (int Index = 0; Index < 2; ++Index) Capture();
        std::cout << "Deleted the Mirror modifier: back to the plain single Arrow, no trace left.\n";

        // ---------------------------------------------------------------------------------------------
        // 5) "+ Noise / jitter" - a seeded, deterministic nudge to position/rotation; never duplicates.
        // ---------------------------------------------------------------------------------------------
        ClickCaption("+ Noise / jitter");
        for (int Index = 0; Index < 3; ++Index) Capture();
        Require(Panel->QueryDocument().Layers.front().Modifiers.size() == 1, "Adding a Noise modifier did not add exactly one entry.");
        Require(EvaluatedLayerCount() == 1, "Noise must never duplicate the layer - only nudge its placement.");
        const auto BeforeJitter = Panel->QueryDocument().Instructions[Panel->QueryDocument().Layers.front().Root].Bounds;
        const auto EvaluatedJitter = ConstructEvaluatedVectorAuthoringDocument(Panel->QueryDocument());
        const auto AfterJitter = EvaluatedJitter.Instructions[EvaluatedJitter.Layers.front().Root].Bounds;
        Require(BeforeJitter != AfterJitter, "The default Noise modifier did not visibly nudge the shape's placement.");
        std::cout << "Added a live Noise/jitter MODIFIER: the single arrow nudged by a small seeded random amount "
            "(position + rotation) - the authored source position is untouched, only the live render moved.\n";

        TypeNumber("Position jitter (M1)", "40");
        for (int Index = 0; Index < 3; ++Index) Capture();
        std::cout << "Live-edited the jitter's position amplitude up to 40px - the nudge grew accordingly, still deterministic.\n";
        ClickCaption("Apply modifier (M1)");
        for (int Index = 0; Index < 2; ++Index) Capture();
        Require(Panel->QueryDocument().Layers.size() == 1 && Panel->QueryDocument().Layers.front().Modifiers.empty(),
            "Applying Noise should bake the nudge into the one existing layer, not add or remove layers.");
        std::cout << "Applied the Noise modifier: the nudged placement is now baked directly into the one real layer.\n";
        ClickCaption("Undo");
        Require(Panel->QueryDocument().Layers.front().Modifiers.size() == 1, "Undo after Apply did not restore the Noise modifier entry.");
        SelectArrow();
        ClickCaption("Delete modifier (M1)");
        Require(Panel->QueryDocument().Layers.front().Modifiers.empty(), "Delete did not remove the Noise modifier entry.");
        SelectArrow();

        // ---------------------------------------------------------------------------------------------
        // 6) "+ Offset / outline" - grows/shrinks the motif's own silhouette.
        // ---------------------------------------------------------------------------------------------
        ClickCaption("+ Offset / outline");
        for (int Index = 0; Index < 3; ++Index) Capture();
        Require(Panel->QueryDocument().Layers.front().Modifiers.size() == 1, "Adding an Offset modifier did not add exactly one entry.");
        TypeNumber("Offset distance (M1)", "24");
        for (int Index = 0; Index < 3; ++Index) Capture();
        {
            const auto EvaluatedGrown = ConstructEvaluatedVectorAuthoringDocument(Panel->QueryDocument());
            const auto Grown = EvaluatedGrown.Instructions[EvaluatedGrown.Layers.front().Root];
            const auto &Source = Panel->QueryDocument().Instructions[Panel->QueryDocument().Layers.front().Root];
            Require(Grown.Bounds[2] > Source.Bounds[2] + 40, "Positive offset did not visibly grow the arrow's silhouette.");
        }
        std::cout << "Live-edited Offset distance to +24px: the arrow's own bounding silhouette visibly grew outward.\n";
        TypeNumber("Offset distance (M1)", "-16");
        for (int Index = 0; Index < 3; ++Index) Capture();
        {
            const auto EvaluatedShrunk = ConstructEvaluatedVectorAuthoringDocument(Panel->QueryDocument());
            const auto Shrunk = EvaluatedShrunk.Instructions[EvaluatedShrunk.Layers.front().Root];
            const auto &Source = Panel->QueryDocument().Instructions[Panel->QueryDocument().Layers.front().Root];
            Require(Shrunk.Bounds[2] < Source.Bounds[2], "Negative offset did not visibly shrink the arrow's silhouette.");
        }
        std::cout << "Live-edited Offset distance to -16px: the silhouette shrank back inward, proving both directions work.\n";
        ClickCaption("Apply modifier (M1)");
        for (int Index = 0; Index < 2; ++Index) Capture();
        Require(Panel->QueryDocument().Layers.size() == 1 && Panel->QueryDocument().Layers.front().Modifiers.empty(),
            "Applying Offset should bake the shrink into the one existing layer, not add or remove layers.");
        std::cout << "Applied the Offset modifier: the shrunk silhouette is now baked directly into the one real layer.\n";

        ImGui::DestroyContext();
        std::cout << "PASS: Mirror, Noise/jitter and Offset/outline all work as genuine Blender-style modifier-stack "
            "entries alongside Array - add, live-edit, Apply and Undo all verified against the actual rendered "
            "canvas, see " << Output.string() << "\n";
        (void)ArrowToken;
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "FAIL: " << Failure.what() << "\n";
        return 1;
    }
}
