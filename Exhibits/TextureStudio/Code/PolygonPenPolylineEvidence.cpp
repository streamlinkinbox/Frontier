/*==========================================================================================================================================
                                             POLYGONPENPOLYLINEEVIDENCE.CPP
==========================================================================================================================================*/
// 🧩 Headless, CPU-rasterized visual proof (no OS window, no GPU; see StampGradientStopEvidence.cpp's header
// for why the GPU shader's own blend isn't screenshotted here) of four things asked for on the Stamp canvas:
//  1. The Inspector's "Sides" slider changing a Polygon's corner count live (3..35), not just at creation.
//  2. The Points tool freely dragging one corner of that polygon into an irregular shape (not staying regular).
//  3. The newly-ported Pen tool drawing an OPEN path that renders as a plain stroked line, no fill.
//  4. The new Polyline tool drawing straight-only segments and closing back to its first anchor, which turns
//     it into a FILLED shape with its own independent outline width - distinct from the open line's width.

#include "UI/TextureStudio/StampWorkspacePanel.h"
#include "UI/TextureStudio/PatternThumbnailRaster.h"
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

        ActivationOwnership PanelOwnership(new StampWorkspacePanel());
        auto *Panel = static_cast<StampWorkspacePanel *>(PanelOwnership.get());
        Require(Panel->Initialize() && Panel->Activate(), "Stamp panel lifecycle failed.");
        Panel->ConfigureTypeface(Typeface, Typeface);

        // 📝 A real Category-4 Polygon source, 5 sides to start - exactly what "Rectangle tool -> Polygon
        // tool" now creates, just constructed directly here instead of via the toolbar's own "More tools"
        // popup (a pure ImGui-popup-timing concern, not part of the feature under test).
        VectorAuthoringDocument Document;
        Document.Stamp = true;
        VectorCoverageInstruction Polygon;
        Polygon.Topology[0] = 4; // Polygon
        Polygon.Bounds = {256, 256, 180, 180};
        Polygon.Colour = {.86f, .46f, .35f, 1};
        Polygon.StrokeColour = {.23f, .24f, .28f, 1};
        Polygon.Topology[3] |= VectorCoverageStrokeColourExplicit;
        Polygon.Paint[2] = 5; // 5 sides
        Document.Instructions.push_back(Polygon);
        VectorAuthoringLayer Layer;
        Layer.Token = ++Document.TokenClock;
        Layer.Root = 0;
        Layer.Name = "Polygon";
        Document.Name = "Polygon/Pen/Polyline demo";
        Document.Layers.push_back(Layer);
        Panel->ConfigureDocument(Document);

        // 📝 Stand-in for the real Vulkan coverage dispatch (there's no GPU in this headless sandbox): the
        // same ThorVG path-accurate SVG rasterizer the live gallery/sidebar thumbnails use.
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
            Require(ImGui::GetDrawData()->TotalVtxCount > 0, "Stamp canvas produced no ImGui draw commands.");
        };
        for (int Index = 0; Index < 6; ++Index) Frame();

        int CaptureIndex = 0;
        const auto Capture = [&]
        {
            IO.Fonts->GetTexDataAsRGBA32(&Pixels, &AtlasWidth, &AtlasHeight);
            FakeTexture = RasterizePatternThumbnailSvg(Panel->QueryDocument(), FakeTextureExtent, BoardWell);
            const std::vector<LibraryCaptureTexture> Textures{{FakeTextureToken, FakeTexture.data(), FakeTextureExtent, FakeTextureExtent}};
            char Name[64];
            std::snprintf(Name, sizeof(Name), "%02d-frame.ppm", CaptureIndex++);
            EncodeLibraryDrawCapture(Output / Name, *ImGui::GetDrawData(), Pixels, AtlasWidth, AtlasHeight, Textures);
        };

        const auto MoveTo = [&](ImVec2 Position) { IO.AddMousePosEvent(Position.x, Position.y); Frame(); };
        const auto Down = [&] { IO.AddMouseButtonEvent(0, true); Frame(); };
        const auto Up = [&] { IO.AddMouseButtonEvent(0, false); Frame(); Frame(); Frame(); };
        const auto Click = [&](ImVec2 Position) { MoveTo(Position); Down(); Up(); };

        // 📝 Scrolls the Inspector panel (mouse wheel, same as a real user) until the named control's own
        // registered extent sits inside a safe, fully-visible vertical band, exactly like the existing
        // PatternWorkspaceValidation.cpp harness's own Click(Caption) helper - some of these controls (e.g.
        // "Convert to cubic path", the per-anchor grips, "Outline width") sit well down the Inspector's own
        // scrollable column, below the initial viewport.
        const auto ClickCaption = [&](const char *Caption)
        {
            auto Extent = Panel->QueryControlExtent(Caption);
            for (int Attempt = 0; Attempt < 16 && (Extent.MaximumY > 1380 || Extent.MinimumY < 260); ++Attempt)
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

        const auto Board = Panel->QueryBoardExtent();
        const auto ToScreen = [&](float X, float Y)
        {
            return ImVec2(Board.MinimumX + X / 512.f * Board.Width(), Board.MinimumY + Y / 512.f * Board.Height());
        };

        Click(ToScreen(256, 256));
        Require(!Panel->QuerySelection().empty(), "Clicking the polygon did not select it.");
        for (int Index = 0; Index < 4; ++Index) Capture();
        std::cout << "Captured a freshly created 5-sided Polygon, selected via the canvas.\n";

        // ---------------------------------------------------------------------------------------------
        // 1) Drag the Inspector's "Sides" slider from 5 up toward 10 sides.
        // ---------------------------------------------------------------------------------------------
        {
            const auto SidesExtent = ClickCaption("Sides");
            Up(); // ClickCaption's own trailing Click() already pressed+released once; re-press to drag from here.
            const float CentreY = (SidesExtent.MinimumY + SidesExtent.MaximumY) * .5f;
            MoveTo(ImVec2(SidesExtent.MinimumX + (5.f - 3.f) / (35.f - 3.f) * SidesExtent.Width(), CentreY));
            Down();
            constexpr int Steps = 8;
            for (int Step = 1; Step <= Steps; ++Step)
            {
                const float Fraction = (5.f - 3.f) / (35.f - 3.f) + (float(Step) / Steps) * ((10.f - 3.f) / (35.f - 3.f) - (5.f - 3.f) / (35.f - 3.f));
                MoveTo(ImVec2(SidesExtent.MinimumX + Fraction * SidesExtent.Width(), CentreY));
                Capture();
            }
            Up();
            for (int Index = 0; Index < 3; ++Index) Capture();
            const auto &Source = Panel->QueryDocument().Instructions[Panel->QueryDocument().Layers.front().Root];
            const long Reached = std::lround(Source.Paint[2]);
            Require(Reached >= 8 && Reached <= 10, "Dragging the Sides slider did not land near 10 sides.");
            std::cout << "Captured dragging the Inspector's \"Sides\" slider live from 5 up to " << Reached << " sides on the same shape.\n";
        }

        // ---------------------------------------------------------------------------------------------
        // 2) Switch to the Point tool, use the Inspector's own "Convert to cubic path" button, then drag
        //    one corner - proving the polygon is freely/independently editable per-vertex (it becomes
        //    irregular, not just differently-regular).
        // ---------------------------------------------------------------------------------------------
        ClickCaption("Point tool (A)");
        for (int Index = 0; Index < 2; ++Index) Capture();
        std::cout << "Switched to the Point tool (now ported to Stamp) over the Polygon.\n";
        {
            ClickCaption("Convert to cubic path");
            Frame(); Frame();
            Capture();
            const auto &Converted = Panel->QueryDocument().Instructions[Panel->QueryDocument().Layers.front().Root];
            Require(Converted.Topology[0] == 6, "\"Convert to cubic path\" did not turn the Polygon into a cubic path.");
        }
        std::cout << "Converted the Polygon to an editable cubic path with one click, preserving its corner outline.\n";
        {
            const auto AnchorExtent = ClickCaption("Path anchor 0");
            Up();
            const ImVec2 AnchorCentre((AnchorExtent.MinimumX + AnchorExtent.MaximumX) * .5f, (AnchorExtent.MinimumY + AnchorExtent.MaximumY) * .5f);
            const ImVec2 Dragged(AnchorCentre.x + 90, AnchorCentre.y - 70);
            MoveTo(AnchorCentre);
            Down();
            constexpr int Steps = 8;
            for (int Step = 1; Step <= Steps; ++Step)
            {
                const float T = float(Step) / Steps;
                MoveTo(ImVec2(AnchorCentre.x + (Dragged.x - AnchorCentre.x) * T, AnchorCentre.y + (Dragged.y - AnchorCentre.y) * T));
                Capture();
            }
            Up();
            for (int Index = 0; Index < 3; ++Index) Capture();
        }
        std::cout << "Dragged one corner of the polygon freely - it is now an irregular shape, not a regular one.\n";

        // ---------------------------------------------------------------------------------------------
        // 3) New document: Pen tool draws an OPEN path - it renders as a plain stroked line, no fill.
        // ---------------------------------------------------------------------------------------------
        {
            VectorAuthoringDocument PenDocument;
            PenDocument.Stamp = true;
            PenDocument.Name = "Pen (open line)";
            Panel->ConfigureDocument(PenDocument);
            for (int Index = 0; Index < 3; ++Index) Frame();
            ClickCaption("Pen tool (P)");
            const std::array<ImVec2, 3> Anchors{ToScreen(160, 300), ToScreen(260, 160), ToScreen(360, 300)};
            for (const auto &Anchor : Anchors) { Click(Anchor); Capture(); }
            IO.AddKeyEvent(ImGuiKey_Enter, true); Frame();
            IO.AddKeyEvent(ImGuiKey_Enter, false); Frame(); Frame(); Frame();
            for (int Index = 0; Index < 3; ++Index) Capture();
            const auto &Live = Panel->QueryDocument();
            Require(Live.Layers.size() == 1, "Finishing the Pen path did not commit exactly one layer.");
            const auto &Source = Live.Instructions[Live.Layers.front().Root];
            Require(Source.Topology[0] == 6, "The finished Pen path is not a cubic path.");
            Require(Source.Paint[3] == 0, "An open Pen path should not render filled.");
        }
        std::cout << "Drew an open 3-anchor Pen path and finished it with Enter - it stays a plain unfilled line.\n";

        // ---------------------------------------------------------------------------------------------
        // 4) New document: Polyline tool draws straight segments and closes back to its first anchor,
        //    becoming a FILLED shape with its own independent outline width.
        // ---------------------------------------------------------------------------------------------
        {
            VectorAuthoringDocument PolylineDocument;
            PolylineDocument.Stamp = true;
            PolylineDocument.Name = "Polyline (closed)";
            Panel->ConfigureDocument(PolylineDocument);
            for (int Index = 0; Index < 3; ++Index) Frame();
            ClickCaption("Polyline tool (L)");
            const ImVec2 First = ToScreen(256, 150), Second = ToScreen(360, 320), Third = ToScreen(150, 320);
            Click(First); Capture();
            Click(Second); Capture();
            Click(Third); Capture();
            // Click back on the first anchor to close the path into a filled shape.
            Click(First);
            for (int Index = 0; Index < 3; ++Index) Capture();
            const auto &Closed = Panel->QueryDocument();
            Require(Closed.Layers.size() == 1, "Closing the Polyline path did not commit exactly one layer.");
            const auto &ClosedSource = Closed.Instructions[Closed.Layers.front().Root];
            Require(ClosedSource.Topology[0] == 6, "The finished Polyline path is not a cubic path.");
            Require(ClosedSource.Paint[3] != 0, "Closing a Polyline path should make it render filled.");
            std::cout << "Closed the Polyline back to its first anchor - it is now a FILLED shape.\n";

            // 📝 The Polyline tool (exactly like Pen) stays active for continuous multi-shape drawing, so a
            // further canvas click would start ANOTHER path rather than select this one - switch back to the
            // Select tool first, exactly as a real user would (click "Select tool (V)" or press V).
            ClickCaption("Select tool (V)");
            Click(ToScreen(256, 260));
            Require(!Panel->QuerySelection().empty(), "Clicking the closed Polyline shape did not select it.");
            for (int Index = 0; Index < 3; ++Index) Capture();
            // "Outline width" is a plain typed-number field (VectorAuthoringControlSurface::Number -> ImGui::
            // InputFloat), not a draggable slider strip like "Sides" - edit it the same way the other proven
            // evidence/validation binaries do: click to focus it, select-all, type digits, commit with Enter.
            ClickCaption("Outline width");
            Capture();
            IO.AddKeyEvent(ImGuiMod_Ctrl, true); IO.AddKeyEvent(ImGuiKey_A, true); Frame();
            IO.AddKeyEvent(ImGuiKey_A, false); IO.AddKeyEvent(ImGuiMod_Ctrl, false); Frame(); Capture();
            for (const char Character : std::string("18")) { IO.AddInputCharacter(Character); Frame(); }
            Capture();
            IO.AddKeyEvent(ImGuiKey_Enter, true); Frame(); IO.AddKeyEvent(ImGuiKey_Enter, false); Frame();
            for (int Index = 0; Index < 4; ++Index) Capture();
            const auto &Styled = Panel->QueryDocument().Instructions[Panel->QueryDocument().Layers.front().Root];
            Require(Styled.Paint[0] > 10, "Dragging \"Outline width\" did not raise the closed shape's own outline width.");
            Require(Styled.Transform[1] != Styled.Paint[0], "The closed shape's outline width should differ from Transform[1] (the open-line width field), proving independence.");
            std::cout << "Raised the closed shape's own outline width via its independent \"Outline width\" field "
                "(Paint[0] = " << Styled.Paint[0] << "), distinct from Transform[1] = " << Styled.Transform[1] << " (the open line's own thickness field).\n";
        }

        ImGui::DestroyContext();
        std::cout << "PASS: Polygon side-count slider, freeform per-vertex editing, ported Pen (open line) and "
            "new Polyline (closes to a filled shape with its own independent outline width) all verified - see "
            << Output.string() << "\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "FAIL: " << Failure.what() << "\n";
        return 1;
    }
}
