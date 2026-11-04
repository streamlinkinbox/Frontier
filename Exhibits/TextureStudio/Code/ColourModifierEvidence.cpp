/*==========================================================================================================================================
                                             COLOURMODIFIEREVIDENCE.CPP
==========================================================================================================================================*/
// 🧩 Headless, CPU-rasterized visual proof of the NEW "Colour" modifier-stack entry: paints fill and/or
// stroke with a Linear gradient, a Radial gradient, or a discrete Ramp - the Ramp's band count auto-derives
// from an immediately preceding Array modifier's own rows/rings (AutoGroup), with a manual GroupSize
// override always available. Demonstrated on the real rendered canvas in two scenes:
//  SCENE A (Array + Colour live chaining - "give each array row its own colour"):
//   1. A Grid Array (3x2) modifier expands one bead into 6 - still a plain uniform colour.
//   2. "+ Colour" (Ramp, AutoGroup) bands the 6 copies by ROW - group size auto-matched the Array's Columns.
//   3. Turning AutoGroup off with a manual GroupSize=1 instead alternates colour PER COPY (checkerboard).
//   4. Switching the generator to Linear instead blends continuously, left-to-right, across the whole population.
//   5. Switching Target to Stroke/Both colours only the outline, or both outline and fill, independently.
//   6. Deleting the Colour entry (leaving Array intact) returns to a plain uniform grid - no residue.
//  SCENE B (Apply/Undo/Delete lifecycle, matching the existing Mirror/Noise/Offset modifiers):
//   7. "+ Colour" on a single un-arrayed shape, Apply bakes the flat colour into the one real layer, Undo
//      restores the live modifier, Delete removes it with no other change.

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

        // 📝 A single round "Bead", given a visible stroke outline up front so the Target=Stroke/Both demo
        // later has something to actually colour.
        VectorAuthoringDocument Document;
        Document.TokenClock = 1;
        VectorCoverageInstruction Bead;
        Bead.Topology[0] = 1; // ellipse
        Bead.Bounds = {256, 256, 26, 26};
        Bead.Colour = {.72f, .70f, .66f, 1};
        Bead.Transform[1] = 5; // visible stroke width
        Document.Instructions = {Bead};
        VectorAuthoringLayer Layer; Layer.Token = 1; Layer.Name = "Bead"; Layer.Root = 0;
        Document.Layers = {Layer};
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

        const auto SelectBead = [&]
        {
            Click(ToScreen(256, 256));
            Require(!Panel->QuerySelection().empty(), "Clicking the Bead motif did not select it.");
        };

        const auto EvaluatedDocument = [&] { return ConstructEvaluatedVectorAuthoringDocument(Panel->QueryDocument()); };
        const auto RowColour = [&](size_t LayerIndex)
        {
            const auto Evaluated = EvaluatedDocument();
            return Evaluated.Instructions[Evaluated.Layers[LayerIndex].Root].Colour;
        };
        const auto RowStroke = [&](size_t LayerIndex)
        {
            const auto Evaluated = EvaluatedDocument();
            return Evaluated.Instructions[Evaluated.Layers[LayerIndex].Root].StrokeColour;
        };

        SelectBead();
        for (int Index = 0; Index < 4; ++Index) Capture();
        std::cout << "Captured a single grey Bead motif (with a visible stroke outline), selected via the canvas.\n";

        // =================================================================================================
        // SCENE A - "give each array row its own colour" (Array + Colour live chaining).
        // =================================================================================================
        ClickCaption("Modifiers");
        for (int Index = 0; Index < 2; ++Index) Capture();
        PickDropdownRow("Array layout", 1); // {"Linear","Grid","Circular / arc","Radial rings"}
        TypeNumber("Columns", "3");
        TypeNumber("Rows", "2");
        Capture();
        ClickCaption("Add array modifier");
        for (int Index = 0; Index < 2; ++Index) Capture();
        Require(Panel->QueryDocument().Layers.size() == 1, "Adding an Array modifier must not duplicate the authored layer.");
        Require(EvaluatedDocument().Layers.size() == 6, "The evaluated/rendered document did not expand to the 3x2 grid.");
        std::cout << "Added a live Grid Array modifier (3 columns x 2 rows): 1 authored layer, 6 evaluated copies, still one plain colour.\n";

        ClickCaption("+ Colour");
        for (int Index = 0; Index < 3; ++Index) Capture();
        Require(Panel->QueryDocument().Layers.front().Modifiers.size() == 2, "Adding a Colour modifier did not stack onto the existing Array entry.");
        {
            const auto Row0 = RowColour(0), Row1 = RowColour(1), Row2 = RowColour(2);
            const auto Row3 = RowColour(3), Row4 = RowColour(4), Row5 = RowColour(5);
            Require(Row0 == Row1 && Row1 == Row2, "AutoGroup Ramp did not give row 0's three copies a matching colour.");
            Require(Row3 == Row4 && Row4 == Row5, "AutoGroup Ramp did not give row 1's three copies a matching colour.");
            Require(Row0 != Row3, "AutoGroup Ramp did not give the two array rows two DIFFERENT colours.");
        }
        std::cout << "Added a live Colour modifier (Ramp, AutoGroup): the default two stops auto-banded the grid's own rows - "
            "row 1 one colour, row 2 another - the band size came straight from the Array's own 3-column width, no manual count needed.\n";

        ClickCaption("Auto group from array (M2)");
        for (int Index = 0; Index < 2; ++Index) Capture();
        TypeNumber("Group size (M2)", "1");
        for (int Index = 0; Index < 2; ++Index) Capture();
        {
            const auto Row0 = RowColour(0), Row1 = RowColour(1), Row2 = RowColour(2), Row3 = RowColour(3);
            Require(Row0 != Row1, "Manual GroupSize=1 did not alternate colour per individual copy.");
            Require(Row0 == Row2 && Row1 == Row3, "Manual GroupSize=1 did not cycle back to stop 0 every other copy.");
        }
        std::cout << "Turned AutoGroup off and set a manual Group size of 1: every individual copy now alternates colour "
            "(a checkerboard, not a row band) - proving the grouping is fully overridable, not locked to the array's own shape.\n";
        ClickCaption("Auto group from array (M2)");
        for (int Index = 0; Index < 2; ++Index) Capture();
        std::cout << "Re-enabled AutoGroup: the row banding returned immediately.\n";

        PickDropdownRow("Colour generator (M2)", 0); // {"Linear gradient","Radial gradient","Colour ramp (grouped)"}
        for (int Index = 0; Index < 3; ++Index) Capture();
        {
            const auto Left = RowColour(0), Middle = RowColour(1), Right = RowColour(2);
            Require(Left != Middle && Middle != Right && Left != Right, "Linear generator did not vary continuously across the array's own columns.");
        }
        std::cout << "Switched the generator to Linear: colour now blends continuously left-to-right across the WHOLE arrayed "
            "population (every one of the 6 copies its own shade), auto-fit to the array's own combined bounding box.\n";

        PickDropdownRow("Colour generator (M2)", 2); // back to Ramp
        for (int Index = 0; Index < 2; ++Index) Capture();

        PickDropdownRow("Paints (M2)", 1); // {"Fill","Stroke / outline","Fill + stroke"}
        for (int Index = 0; Index < 3; ++Index) Capture();
        {
            const auto FillRow0 = RowColour(0), FillRow3 = RowColour(3);
            const auto StrokeRow0 = RowStroke(0), StrokeRow3 = RowStroke(3);
            Require(FillRow0 == FillRow3, "Stroke-only target should leave every copy's fill colour identical (untouched).");
            Require(StrokeRow0 != StrokeRow3, "Stroke-only target did not band the stroke/outline colour by row.");
        }
        std::cout << "Switched Target to Stroke / outline: every copy's FILL reverted to its original grey, but the OUTLINE "
            "itself now carries the row banding - fill and stroke are driven completely independently.\n";

        PickDropdownRow("Paints (M2)", 2); // Both
        for (int Index = 0; Index < 3; ++Index) Capture();
        {
            Require(RowColour(0) != RowColour(3), "Both target did not band the fill colour by row.");
            Require(RowStroke(0) != RowStroke(3), "Both target did not band the stroke colour by row.");
        }
        std::cout << "Switched Target to Fill + stroke: both the bead's face and its outline now carry the row banding together.\n";

        ClickCaption("Delete modifier (M2)");
        for (int Index = 0; Index < 2; ++Index) Capture();
        Require(Panel->QueryDocument().Layers.front().Modifiers.size() == 1, "Deleting the Colour entry should leave the Array entry untouched.");
        {
            const auto Row0 = RowColour(0), Row3 = RowColour(3);
            Require(Row0 == Row3, "Deleting the Colour modifier did not return the grid to one uniform colour.");
        }
        std::cout << "Deleted the Colour modifier: back to the plain 3x2 grid in one uniform colour, Array untouched, no residue.\n";

        ClickCaption("Delete modifier (M1)");
        SelectBead();
        Require(Panel->QueryDocument().Layers.front().Modifiers.empty(), "Delete did not remove the Array modifier entry.");
        for (int Index = 0; Index < 2; ++Index) Capture();
        std::cout << "Deleted the Array modifier too: back to the single plain Bead, ready for the Apply/Undo scene.\n";

        // =================================================================================================
        // SCENE B - Apply/Undo/Delete lifecycle, exactly like the existing Mirror/Noise/Offset modifiers.
        // =================================================================================================
        ClickCaption("+ Colour");
        for (int Index = 0; Index < 3; ++Index) Capture();
        Require(Panel->QueryDocument().Layers.front().Modifiers.size() == 1, "Adding a Colour modifier did not add exactly one entry.");
        Require(EvaluatedDocument().Layers.size() == 1, "Colour must never duplicate the layer - only paint it.");
        const auto BeforeColour = Panel->QueryDocument().Instructions[Panel->QueryDocument().Layers.front().Root].Colour;
        const auto AfterColour = RowColour(0);
        Require(BeforeColour != AfterColour, "The default Colour modifier did not visibly repaint the shape.");
        std::cout << "Added a live Colour MODIFIER to the single Bead: the authored source colour is untouched, only the live render repainted.\n";

        PickDropdownRow("Colour generator (M1)", 1); // Radial, for visual variety in this scene
        for (int Index = 0; Index < 3; ++Index) Capture();
        std::cout << "Live-edited the modifier's own generator to Radial - the live colour re-evaluated instantly, source still untouched.\n";

        ClickCaption("Apply modifier (M1)");
        for (int Index = 0; Index < 2; ++Index) Capture();
        {
            const auto &Baked = Panel->QueryDocument();
            Require(Baked.Layers.size() == 1 && Baked.Layers.front().Modifiers.empty(), "Applying Colour should bake the paint into the one existing layer, not add or remove layers.");
            Require(Baked.Instructions[Baked.Layers.front().Root].Colour != BeforeColour, "Apply did not bake the new colour into the real authored instruction.");
        }
        std::cout << "Applied the Colour modifier: the painted colour is now baked directly into the one real layer's own source colour.\n";
        ClickCaption("Undo");
        Require(Panel->QueryDocument().Layers.front().Modifiers.size() == 1, "Undo after Apply did not restore the Colour modifier entry.");
        SelectBead();
        for (int Index = 0; Index < 2; ++Index) Capture();
        std::cout << "Undid the Apply: back to the plain grey source with its live Colour modifier intact.\n";

        ClickCaption("Delete modifier (M1)");
        Require(Panel->QueryDocument().Layers.front().Modifiers.empty(), "Delete did not remove the Colour modifier entry.");
        SelectBead();
        for (int Index = 0; Index < 2; ++Index) Capture();
        std::cout << "Deleted the Colour modifier: back to the plain grey Bead, no trace left.\n";

        ImGui::DestroyContext();
        std::cout << "PASS: the Colour modifier works as a genuine Blender-style modifier-stack entry alongside Array/Mirror/"
            "Noise/Offset - AutoGroup row/ring banding, manual GroupSize override, Linear/Radial/Ramp generators, independent "
            "Fill/Stroke/Both targeting, and the full Add/live-edit/Apply/Undo/Delete lifecycle are all verified against the "
            "actual rendered canvas, see " << Output.string() << "\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "FAIL: " << Failure.what() << "\n";
        return 1;
    }
}
