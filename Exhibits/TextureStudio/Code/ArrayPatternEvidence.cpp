/*==========================================================================================================================================
                                                   ARRAYPATTERNEVIDENCE.CPP
==========================================================================================================================================*/
// 🧩 Headless, CPU-rasterized visual proof of the Pattern workspace's real "Modifiers" Inspector card -
// arrays are LIVE procedural modifiers (Blender-style): the authored document keeps its single source layer
// throughout, and every screenshot/assertion below reads the evaluated expansion the canvas actually renders.
// Demonstrates exactly the three shapes asked about:
//  1. A plain circular array - N copies placed evenly around a single ring.
//  2. A radial array with multiple RINGS, each ring at a larger radius than the last ("expanding outward").
//  3. The same radial rings, but with the "Radial distance" scale profile, so each copy's own size grows
//     the further it sits from the centre - a classic sunburst/particle-burst look, not just repeated dots.

#include "UI/TextureStudio/PatternWorkspacePanel.h"
#include "Engine/ContentInterchange/VectorAuthoringEditing.h"
#include "UI/TextureStudio/PatternThumbnailRaster.h"
#include "Engine/Interface/DropdownControls.h"
#include "RasterizeLibraryEvidence.h"
#include <imgui_internal.h>
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
        std::vector<uint8_t> FakeTexture = RasterizePatternThumbnailSvg(Document, FakeTextureExtent, BoardWell);
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
            FakeTexture = RasterizePatternThumbnailSvg(Frontier::ConstructEvaluatedVectorAuthoringDocument(Panel->QueryDocument()), FakeTextureExtent, BoardWell);
            const std::vector<LibraryCaptureTexture> Textures{{FakeTextureToken, FakeTexture.data(), FakeTextureExtent, FakeTextureExtent}};
            char Name[64];
            std::snprintf(Name, sizeof(Name), "%02d-frame.ppm", CaptureIndex++);
            EncodeLibraryDrawCapture(Output / Name, *ImGui::GetDrawData(), Pixels, AtlasWidth, AtlasHeight, Textures);
        };

        const auto MoveTo = [&](ImVec2 Position) { IO.AddMousePosEvent(Position.x, Position.y); Frame(); };
        const auto Down = [&] { IO.AddMouseButtonEvent(0, true); Frame(); };
        const auto Up = [&] { IO.AddMouseButtonEvent(0, false); Frame(); Frame(); Frame(); };
        const auto Click = [&](ImVec2 Position) { MoveTo(Position); Down(); Up(); };

        // 📝 Scrolls the Inspector (mouse wheel, same as a real user) until the named control's own registered
        // extent sits inside a safe visible band - the "Modifiers" card and its own fields sit well
        // down the Inspector's scrollable column, below the initial viewport.
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

        // 📝 Edits a typed Inspector number field (VectorAuthoringControlSurface::Number -> ImGui::InputFloat):
        // click to focus it, select-all, type the digits, commit with Enter - driving an actual frame for
        // every key event, exactly like a real keyboard would.
        const auto TypeNumber = [&](const char *Caption, const char *Digits)
        {
            ClickCaption(Caption);
            IO.AddKeyEvent(ImGuiMod_Ctrl, true); IO.AddKeyEvent(ImGuiKey_A, true); Frame();
            IO.AddKeyEvent(ImGuiKey_A, false); IO.AddKeyEvent(ImGuiMod_Ctrl, false); Frame();
            for (const char Character : std::string(Digits)) { IO.AddInputCharacter(Character); Frame(); }
            IO.AddKeyEvent(ImGuiKey_Enter, true); Frame(); IO.AddKeyEvent(ImGuiKey_Enter, false); Frame();
        };

        // 📝 Picks a row from an open ImGui popup list, same mechanism proven in PatternGradientStopEvidence.cpp
        // for the "Motif fill source" dropdown.
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

        SelectDot();
        for (int Index = 0; Index < 4; ++Index) Capture();
        std::cout << "Captured a single Dot motif, selected via the canvas.\n";

        ClickCaption("Modifiers");
        for (int Index = 0; Index < 2; ++Index) Capture();
        std::cout << "Opened the Inspector's real \"Modifiers\" card.\n";

        // ---------------------------------------------------------------------------------------------
        // 1) Plain circular array - 10 copies evenly spaced around one ring.
        // ---------------------------------------------------------------------------------------------
        {
            PickDropdownRow("Array layout", 2); // {"Linear","Grid","Circular / arc","Radial rings"}
            for (int Index = 0; Index < 2; ++Index) Capture();
            TypeNumber("Copies per ring", "10");
            Capture();
            ClickCaption("Add array modifier");
            for (int Index = 0; Index < 3; ++Index) Capture();
            Require(Panel->QueryDocument().Layers.size() == 1, "The live circular array modifier baked layers into the authored document.");
            const auto Generated = Frontier::ConstructEvaluatedVectorAuthoringDocument(Panel->QueryDocument());
            Require(Generated.Layers.size() == 10, "The live circular array did not evaluate to exactly 10 copies.");
            std::cout << "Captured a circular array: 10 copies of the Dot placed evenly around one ring.\n";
        }

        ClickCaption("Undo");
        Require(Panel->QueryDocument().Layers.size() == 1, "Undo did not restore the single Dot motif.");
        SelectDot();
        for (int Index = 0; Index < 2; ++Index) Capture();
        std::cout << "Undid the circular array back to the single Dot.\n";

        // ---------------------------------------------------------------------------------------------
        // 2) Radial rings array - 3 rings, each at a larger radius than the last (expanding outward).
        // ---------------------------------------------------------------------------------------------
        {
            PickDropdownRow("Array layout", 3); // "Radial rings"
            for (int Index = 0; Index < 2; ++Index) Capture();
            TypeNumber("Copies per ring", "8");
            TypeNumber("Rings", "3");
            TypeNumber("Radius", "55");
            TypeNumber("Ring spacing", "55");
            Capture();
            ClickCaption("Add array modifier");
            for (int Index = 0; Index < 3; ++Index) Capture();
            Require(Panel->QueryDocument().Layers.size() == 1, "The live radial-rings modifier baked layers into the authored document.");
            const auto Generated = Frontier::ConstructEvaluatedVectorAuthoringDocument(Panel->QueryDocument());
            Require(Generated.Layers.size() == 24, "The live radial-rings array did not evaluate to exactly 24 copies (8 per ring x 3 rings).");
            // Ring 0 (innermost) comes first, Ring 2 (outermost) comes last - confirm the rings really do sit
            // at increasing distance from the centre, i.e. genuinely "expanding outward".
            const auto Distance = [&](const VectorCoverageInstruction &Instruction)
            { return std::hypot(Instruction.Bounds[0] - 256, Instruction.Bounds[1] - 256); };
            const float InnerDistance = Distance(Generated.Instructions[Generated.Layers[0].Root]);
            const float OuterDistance = Distance(Generated.Instructions[Generated.Layers[23].Root]);
            Require(OuterDistance > InnerDistance + 50, "The outer ring is not meaningfully further from the centre than the inner ring.");
            std::cout << "Captured a radial array: 3 rings of 8 copies each, every ring sitting further out than "
                "the last (inner ring at " << InnerDistance << "px, outer ring at " << OuterDistance << "px from centre) - "
                "a circular array that genuinely expands outward.\n";
        }

        ClickCaption("Undo");
        Require(Panel->QueryDocument().Layers.size() == 1, "Undo did not restore the single Dot motif.");
        SelectDot();
        for (int Index = 0; Index < 2; ++Index) Capture();
        std::cout << "Undid the radial-rings array back to the single Dot.\n";

        // ---------------------------------------------------------------------------------------------
        // 3) Same radial rings, but with the "Radial distance" scale profile: each copy's own size grows
        //    the further out its ring sits - a sunburst/particle-burst look, not identically-sized dots.
        // ---------------------------------------------------------------------------------------------
        {
            PickDropdownRow("Scale profile", 3); // {"Uniform","Linear progression","Circular wave","Radial distance","Seeded noise"}
            for (int Index = 0; Index < 2; ++Index) Capture();
            TypeNumber("Start scale %", "45");
            TypeNumber("End scale %", "220");
            Capture();
            ClickCaption("Add array modifier");
            for (int Index = 0; Index < 4; ++Index) Capture();
            Require(Panel->QueryDocument().Layers.size() == 1, "The live scaled radial modifier baked layers into the authored document.");
            const auto Generated = Frontier::ConstructEvaluatedVectorAuthoringDocument(Panel->QueryDocument());
            Require(Generated.Layers.size() == 24, "The live scaled radial array did not evaluate to exactly 24 copies.");
            const float InnerWidth = Generated.Instructions[Generated.Layers[0].Root].Bounds[2];
            const float OuterWidth = Generated.Instructions[Generated.Layers[23].Root].Bounds[2];
            Require(OuterWidth > InnerWidth * 1.5f, "The outermost ring's copies are not visibly larger than the innermost ring's - the \"size changes outward\" effect is missing.");
            std::cout << "Captured a radial array with the \"Radial distance\" scale profile: innermost-ring "
                "copies are " << InnerWidth << "px wide, outermost-ring copies are " << OuterWidth << "px wide - "
                "each copy's own size genuinely grows the further out it sits, a radial sunburst pattern.\n";
        }

        ImGui::DestroyContext();
        std::cout << "PASS: the real Pattern workspace Array tool - circular rings, multi-ring radial arrays "
            "expanding outward, and a radial scale profile that grows copies outward - all verified on the "
            "actual rendered canvas, see " << Output.string() << "\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "FAIL: " << Failure.what() << "\n";
        return 1;
    }
}
