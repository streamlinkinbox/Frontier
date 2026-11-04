/*==========================================================================================================================================
                                                   STAMPGRADIENTSTOPEVIDENCE.CPP
==========================================================================================================================================*/
// 🧩 Headless, CPU-rasterized visual proof for the new interactive gradient colour-ramp editor on the real,
// native StampWorkspacePanel (no reimplementation of the canvas) and rasterized by the same CPU draw-command
// rasterizer every other headless exhibit in this repository relies on (RasterizeLibraryEvidence.h). No OS
// window, no GPU - the GPU shader's own multi-stop colour blend (VectorCoverage.slang) is a separate, already
// landed CPU-mirrored piece of code and cannot be compiled or screenshotted in this sandbox (no slangc); what
// this exhibit proves is everything that actually runs on the CPU and drives the ImGui chrome:
//   1. Selecting a linear-gradient-filled shape draws its colour axis plus one marker per stop directly on
//      the shape, on the canvas itself (not just an Inspector-only ramp bar).
//   2. Clicking the axis line (away from any marker) inserts a new stop there, blended from its neighbours.
//   3. Dragging a marker retimes that stop's position along the axis.
//   4. The gradient's own axis is a pair of freely-draggable A/B point handles - not a fixed angle against
//      the shape's bounding box - so the ramp's start and end can be placed anywhere relative to the shape
//      (e.g. corner to corner, or rotated to run top-to-bottom), independently of where any colour stop sits
//      along that axis.
//   5. The Inspector's own ramp editor (preview bar, add/remove buttons) reflects the same live stop list and
//      can add/remove stops independently of the canvas.
//   6. Switching the fill source's paint category re-seeds the axis to a sensible default for that category.
//   7. Undo walks every one of the above edits back off the same shared history the rest of Stamp already uses.

#include "UI/TextureStudio/StampWorkspacePanel.h"
#include "UI/TextureStudio/PatternThumbnailRaster.h"
#include "Engine/ContentInterchange/VectorAuthoringSelectionEditing.h"
#include "Engine/ContentInterchange/VectorGradientStops.h"
#include "Engine/Interface/DropdownControls.h"
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

        VectorAuthoringDocument Document;
        Document.Stamp = true;
        VectorCoverageInstruction Primitive;
        Primitive.Topology[0] = 0;
        Primitive.Bounds = {256, 256, 320, 180};
        Primitive.Transform[3] = 1; // linear gradient
        Primitive.Colour = {.85f, .2f, .25f, 1};
        Primitive.ColourEnd = {.2f, .35f, .9f, 1};
        ConstructVectorAuthoringPrimitive(Document, Primitive, "Gradient rectangle");
        Panel->ConfigureDocument(Document);

        // 📝 Stand-in for the real Vulkan coverage dispatch (there's no GPU in this headless sandbox): the
        // SAME ThorVG path-accurate SVG rasterizer the live gallery/sidebar thumbnails use, now extended to
        // emit real linear/radial gradient fills (previously solid-colour-only) - so this evidence shows the
        // actual blended gradient colours painted onto the shape, re-rasterized from the panel's live document
        // every Capture() below so edits (new stops, dragged stops, axis drags, category switches, undo) all
        // show their real, current colours rather than a flat placeholder.
        constexpr uint64_t FakeTextureToken = 42;
        constexpr uint32_t FakeTextureExtent = 512;
        constexpr uint32_t BoardWell = uint32_t(255) << 24 | uint32_t(0xd7) << 16 | uint32_t(0xe7) << 8 | uint32_t(0xed); // matches the board's own 0xede7d7 paper colour.
        std::vector<uint8_t> FakeTexture = RasterizePatternThumbnailSvg(Document, FakeTextureExtent, BoardWell);
        Panel->QueryCoverage = [&](const VectorCoverageProgram &, uint32_t) { return FakeTextureToken; };
        Panel->QueryCoverageReady = [] { return true; };

        // 📝 Stands in for the real GPU contour hit-test: the point is inside the one shape's axis-aligned
        // bounds -> its token; otherwise an explicit 0 ("definitely no hit", not "GPU still pending").
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

        const auto Capture = [&](const char *Name)
        {
            IO.Fonts->GetTexDataAsRGBA32(&Pixels, &AtlasWidth, &AtlasHeight);
            // 📝 Re-rasterize from the panel's *live* document every frame, so the captured texture always
            // shows the gradient's current stops/axis/colours, not a stale snapshot from before the edit.
            FakeTexture = RasterizePatternThumbnailSvg(Panel->QueryDocument(), FakeTextureExtent, BoardWell);
            const std::vector<LibraryCaptureTexture> Textures{{FakeTextureToken, FakeTexture.data(), FakeTextureExtent, FakeTextureExtent}};
            EncodeLibraryDrawCapture(Output / (std::string(Name) + ".ppm"), *ImGui::GetDrawData(), Pixels, AtlasWidth, AtlasHeight, Textures);
        };

        const auto MoveTo = [&](ImVec2 Position) { IO.AddMousePosEvent(Position.x, Position.y); Frame(); };
        const auto Down = [&] { IO.AddMouseButtonEvent(0, true); Frame(); };
        const auto Up = [&] { IO.AddMouseButtonEvent(0, false); Frame(); Frame(); Frame(); };
        const auto Click = [&](ImVec2 Position) { MoveTo(Position); Down(); Up(); };
        const auto Click2 = [&](const char *Caption)
        {
            const auto Extent = Panel->QueryControlExtent(Caption);
            Require(Extent.Width() > 0 && Extent.Height() > 0, Caption);
            Click(ImVec2((Extent.MinimumX + Extent.MaximumX) * .5f, (Extent.MinimumY + Extent.MaximumY) * .5f));
        };

        // Select the shape with a plain click at its centre (document space, 0..512 board, mapped through
        // the real board extent so this lines up with whatever zoom/pan the panel opened at).
        const auto Board = Panel->QueryBoardExtent();
        const auto ToScreen = [&](float X, float Y)
        {
            return ImVec2(Board.MinimumX + X / 512.f * Board.Width(), Board.MinimumY + Y / 512.f * Board.Height());
        };
        Click(ToScreen(256, 256));
        Require(!Panel->QuerySelection().empty(), "Clicking the gradient shape did not select it.");
        Capture("01-selected-gradient-markers");
        std::cout << "Captured 01: a selected linear-gradient rectangle shows its axis line and two colour-stop "
            "markers drawn directly on the shape.\n";

        // Click the axis line itself, roughly a third of the way along it (away from either end marker), to
        // insert a brand-new stop there.
        const auto StopExtent = [&](int Index) { return Panel->QueryControlExtent("Gradient stop:" + std::to_string(Index)); };
        const auto Stop0 = StopExtent(0), Stop1 = StopExtent(1);
        Require(Stop0.Width() > 0 && Stop1.Width() > 0, "Expected two gradient-stop hit boxes after selecting the shape.");
        const ImVec2 Marker0((Stop0.MinimumX + Stop0.MaximumX) * .5f, (Stop0.MinimumY + Stop0.MaximumY) * .5f);
        const ImVec2 Marker1((Stop1.MinimumX + Stop1.MaximumX) * .5f, (Stop1.MinimumY + Stop1.MaximumY) * .5f);
        const ImVec2 LineThird(Marker0.x + (Marker1.x - Marker0.x) * .33f, Marker0.y + (Marker1.y - Marker0.y) * .33f);
        Click(LineThird);
        {
            const auto &Source = Panel->QueryDocument().Instructions[Panel->QueryDocument().Layers.front().Root];
            Require(QueryVectorGradientStops(Source).size() == 3, "Clicking the gradient axis line did not insert a third stop.");
        }
        Capture("02-click-line-added-stop");
        std::cout << "Captured 02: clicking the gradient axis line (away from the existing markers) inserted a "
            "third colour stop there, blended from its two neighbours.\n";

        // Drag the newly-inserted stop (packed into slot 1, the new ascending-sorted middle position)
        // further along the axis. A drag clamps strictly between its immediate neighbours rather than
        // re-sorting the stop list, so slot 1 keeps naming the same stop throughout the drag.
        const auto StopMiddle = StopExtent(1);
        Require(StopMiddle.Width() > 0, "Expected the newly-inserted stop's hit box in slot 1.");
        const ImVec2 MarkerMiddle((StopMiddle.MinimumX + StopMiddle.MaximumX) * .5f, (StopMiddle.MinimumY + StopMiddle.MaximumY) * .5f);
        const ImVec2 DragTarget(Marker0.x + (Marker1.x - Marker0.x) * .7f, Marker0.y + (Marker1.y - Marker0.y) * .7f);
        MoveTo(MarkerMiddle);
        Down();
        MoveTo(ImVec2(MarkerMiddle.x + (DragTarget.x - MarkerMiddle.x) * .5f, MarkerMiddle.y + (DragTarget.y - MarkerMiddle.y) * .5f));
        MoveTo(DragTarget);
        Up();
        {
            const auto &Source = Panel->QueryDocument().Instructions[Panel->QueryDocument().Layers.front().Root];
            const auto Stops = QueryVectorGradientStops(Source);
            Require(Stops.size() == 3 && Stops[1].Position > .55f, "Dragging the inserted gradient stop did not retime it further along the axis.");
        }
        Capture("03-dragged-stop-marker");
        std::cout << "Captured 03: dragging that marker along the axis retimed its stop position (now further "
            "toward the end of the ramp) without disturbing the other two stops.\n";

        // 📝 The axis itself is two freely-draggable square handles (A at Fraction 0, B at Fraction 1) -
        // dragging one moves where the ramp starts or ends in the shape without touching any stop's own
        // Fraction along it. Drag B from the shape's right edge up to its top edge, turning what was a
        // left-to-right ramp into a diagonal one, exactly like dragging a gradient tool's line endpoint in
        // Figma/Illustrator.
        std::array<float, 4> AxisBeforeDrag;
        {
            const auto &Source = Panel->QueryDocument().Instructions[Panel->QueryDocument().Layers.front().Root];
            AxisBeforeDrag = Source.GradientAxis;
        }
        const auto AxisExtentB = Panel->QueryControlExtent("Gradient axis:1");
        Require(AxisExtentB.Width() > 0, "Expected the gradient's B-endpoint axis handle hit box.");
        const ImVec2 AxisHandleB((AxisExtentB.MinimumX + AxisExtentB.MaximumX) * .5f, (AxisExtentB.MinimumY + AxisExtentB.MaximumY) * .5f);
        MoveTo(AxisHandleB);
        Down();
        MoveTo(ToScreen(256, 150));
        MoveTo(ToScreen(256, 166));
        Up();
        {
            const auto &Source = Panel->QueryDocument().Instructions[Panel->QueryDocument().Layers.front().Root];
            const auto Stops = QueryVectorGradientStops(Source);
            Require(Source.GradientAxis[0] == AxisBeforeDrag[0] && Source.GradientAxis[1] == AxisBeforeDrag[1],
                "Dragging the B axis handle must not move the A endpoint.");
            Require(std::abs(Source.GradientAxis[2] - AxisBeforeDrag[2]) > 1e-3f || std::abs(Source.GradientAxis[3] - AxisBeforeDrag[3]) > 1e-3f,
                "Dragging the B axis handle did not move the gradient's own end point.");
            Require(Stops.size() == 3 && std::abs(Stops[1].Position - .7f) < .05f,
                "Dragging the axis endpoint must not retime any colour stop's own Fraction along it.");
        }
        Capture("04-dragged-axis-endpoint-b");
        std::cout << "Captured 04: dragging the gradient's own B endpoint handle (a square marker, distinct "
            "from the round colour-stop markers) repositioned where the ramp ends - independently of the three "
            "colour stops' own positions along it - exactly like dragging a gradient tool's endpoint handle in "
            "other design applications.\n";

        // The Inspector's own ramp editor: its "Add stop"/"Remove stop" buttons operate on the same live list.
        Click2("Add stop");
        {
            const auto &Source = Panel->QueryDocument().Instructions[Panel->QueryDocument().Layers.front().Root];
            Require(QueryVectorGradientStops(Source).size() == 4, "The Inspector's \"Add stop\" button did not add a fourth stop.");
        }
        Capture("05-inspector-add-stop");
        std::cout << "Captured 05: the Inspector ramp editor's own \"Add stop\" button (independent of the canvas) "
            "added a fourth stop, shown both in its preview bar and as a new marker on the shape.\n";

        Click2("Remove stop");
        {
            const auto &Source = Panel->QueryDocument().Instructions[Panel->QueryDocument().Layers.front().Root];
            Require(QueryVectorGradientStops(Source).size() == 3, "The Inspector's \"Remove stop\" button did not remove the selected stop.");
        }
        Capture("06-inspector-remove-stop");
        std::cout << "Captured 06: the Inspector's \"Remove stop\" button removed the selected stop back down to "
            "three, again reflected on both the preview bar and the canvas markers.\n";

        // Undo should walk every one of the above edits (insert-by-click, drag-stop, drag-axis-endpoint,
        // Inspector add, Inspector remove - five in total) back off the same shared history Stamp already
        // uses for every other authoring edit (translate/resize/rotate/etc), including the axis move.
        for (int Index = 0; Index < 5; ++Index) Click2("Undo");
        {
            const auto &Source = Panel->QueryDocument().Instructions[Panel->QueryDocument().Layers.front().Root];
            Require(QueryVectorGradientStops(Source).size() == 2, "Undoing all five gradient edits did not return to the original two-stop ramp.");
            Require(Source.GradientAxis == AxisBeforeDrag, "Undoing the axis-endpoint drag did not restore the gradient's original axis.");
        }
        Capture("07-undo-back-to-two-stops");
        std::cout << "Captured 07: five presses of the real \"Undo\" button walked every gradient edit - including "
            "the axis-endpoint drag - back off, restoring the original plain two-stop ramp on its original axis.\n";

        // 📝 Switching the fill source's own paint category (linear -> radial) re-seeds the axis to a
        // sensible default for that category, rather than carrying over whatever the other mode's handles
        // were last dragged to. Drives the real dropdown exactly like a user would: click it open, then
        // click the "Radial gradient" row inside the popup it raises.
        Click2("Component fill source");
        Frame(); Frame();
        {
            auto *Context = ImGui::GetCurrentContext();
            Require(!Context->OpenPopupStack.empty(), "\"Component fill source\" did not open a popup.");
            auto *Popup = Context->OpenPopupStack.back().Window;
            Require(Popup != nullptr, "\"Component fill source\" popup has no window.");
            constexpr int RadialRowIndex = 2; // {"Solid colour", "Linear gradient", "Radial gradient"}
            const float RowHeight = ControlKitTokens::DropdownRowHeight + ControlKitTokens::DropdownRowGap;
            const auto Position = Popup->DC.CursorStartPos;
            MoveTo(ImVec2(Position.x + 20, Position.y + RadialRowIndex * RowHeight + ControlKitTokens::DropdownRowHeight * .5f));
            Down();
            Up();
        }
        {
            const auto &Source = Panel->QueryDocument().Instructions[Panel->QueryDocument().Layers.front().Root];
            Require(uint32_t(Source.Transform[3]) == 2, "Choosing \"Radial gradient\" from the dropdown did not switch the fill's own paint category.");
            Require(Source.GradientAxis == std::array<float, 4>{0.f, 0.f, .5f, 0.f}, "Switching to a radial fill did not re-seed the axis to its own category default.");
        }
        Capture("08-category-switch-reseeds-axis");
        std::cout << "Captured 08: switching the fill source from linear to radial gradient re-seeded the "
            "gradient's own A/B axis to that category's own sensible default (centred, rather than carrying "
            "over the linear ramp's left-to-right handle positions).\n";

        ImGui::DestroyContext();
        std::cout << "PASS: captured the real Stamp canvas's interactive gradient colour-ramp editor - on-canvas "
            "markers, click-to-add, drag-to-reposition, freeform A/B axis-endpoint dragging, Inspector add/remove, "
            "category-switch axis re-seeding, and undo - see " << Output.string() << "\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "FAIL: " << Failure.what() << "\n";
        return 1;
    }
}
