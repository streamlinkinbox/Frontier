/*==========================================================================================================================================
                                                 GRADIENTAXISFREEFORMEVIDENCE.CPP
==========================================================================================================================================*/
// 🧩 Headless, CPU-rasterized visual proof of the exact scenario the gradient axis redesign was asked for:
// on a real text shape (an actual glyph-outline layer, not a stand-in rectangle), the gradient's own A and B
// endpoints are two independently-draggable point handles that can be placed anywhere relative to the shape -
// e.g. left-to-right across the text ("start to end"), or dragged to run top-to-bottom instead - rather than
// a single angle computed against the shape's bounding box. No OS window, no GPU; see
// StampGradientStopEvidence.cpp's header for why the GPU shader's own blend isn't screenshotted here.

#include "UI/TextureStudio/StampWorkspacePanel.h"
#include "UI/TextureStudio/PatternThumbnailRaster.h"
#include "Engine/ContentInterchange/VectorAuthoringSelectionEditing.h"
#include "Engine/ContentInterchange/StampTextOutline.h"
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

        // 📝 A real glyph-outline shape - the same AppendStampTextOutline used by the live text tool, not a
        // rectangle standing in for one - so "drag the gradient across this text" is exactly what it says.
        VectorAuthoringDocument Document;
        Document.Stamp = true;
        const uint32_t TextRoot = AppendStampTextOutline(Document, "GRADIENT", 220, {.85f, .2f, .25f, 1},
            "EngineContent/FontArchives/DMSans/DMSans-Regular.ttf", 0);
        // 📝 AppendStampTextOutline centres the glyph outline on its own (0,0) - recentre it onto the
        // document's own 512x512 canvas, exactly like CommitTextComposer does for a freshly composed box.
        Document.Instructions[TextRoot].Bounds[0] = 256.f;
        Document.Instructions[TextRoot].Bounds[1] = 256.f;
        Document.Instructions[TextRoot].Transform[3] = 1; // linear gradient
        Document.Instructions[TextRoot].ColourEnd = {.2f, .35f, .9f, 1};
        VectorAuthoringLayer Layer;
        Layer.Token = ++Document.TokenClock;
        Layer.Root = TextRoot;
        Layer.Name = "GRADIENT";
        Layer.ComponentKind = VectorAuthoringComponentKind::Text;
        Layer.SourceMarkup = "GRADIENT";
        Layer.TextPointSize = 220;
        Document.Name = "Freeform axis demo";
        Document.Layers.push_back(Layer);
        Panel->ConfigureDocument(Document);

        // 📝 Stand-in for the real Vulkan coverage dispatch (there's no GPU in this headless sandbox): the
        // SAME ThorVG path-accurate SVG rasterizer the live gallery/sidebar thumbnails use, now extended to
        // emit real linear/radial gradient fills (previously solid-colour-only) - so this evidence shows the
        // actual blended gradient colours painted onto the real glyph-outline geometry, not a flat placeholder.
        // Recomputed every Capture() below so the texture always reflects the gradient axis's current, live
        // position as it's being dragged - not a one-shot snapshot taken before the gesture starts.
        constexpr uint64_t FakeTextureToken = 42;
        constexpr uint32_t FakeTextureExtent = 512;
        constexpr uint32_t BoardWell = uint32_t(255) << 24 | uint32_t(0xd7) << 16 | uint32_t(0xe7) << 8 | uint32_t(0xed); // matches the board's own 0xede7d7 paper colour.
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
            // 📝 Re-rasterize from the panel's *live* document every frame, so the captured texture shows the
            // gradient's actual current axis/colours mid-drag, not a stale snapshot from before the gesture.
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

        const auto Board = Panel->QueryBoardExtent();
        const auto ToScreen = [&](float X, float Y)
        {
            return ImVec2(Board.MinimumX + X / 512.f * Board.Width(), Board.MinimumY + Y / 512.f * Board.Height());
        };
        const auto &TextBounds = Document.Instructions[TextRoot].Bounds;
        Click(ToScreen(TextBounds[0], TextBounds[1]));
        Require(!Panel->QuerySelection().empty(), "Clicking the text shape did not select it.");
        for (int Index = 0; Index < 4; ++Index) Capture(); // Hold a beat on the opening frame.
        std::cout << "Captured the selected text shape's gradient axis running left-to-right across it (A at "
            "its start, B at its end) - the ramp's own default orientation.\n";

        const auto AxisExtentA = Panel->QueryControlExtent("Gradient axis:0");
        const auto AxisExtentB = Panel->QueryControlExtent("Gradient axis:1");
        Require(AxisExtentA.Width() > 0 && AxisExtentB.Width() > 0, "Expected both gradient axis handle hit boxes on the text shape.");
        const ImVec2 HandleA((AxisExtentA.MinimumX + AxisExtentA.MaximumX) * .5f, (AxisExtentA.MinimumY + AxisExtentA.MaximumY) * .5f);
        const ImVec2 HandleB((AxisExtentB.MinimumX + AxisExtentB.MaximumX) * .5f, (AxisExtentB.MinimumY + AxisExtentB.MaximumY) * .5f);

        // 📝 Drag B (currently at the text's right/"end") up to above the text ("top"), in several small
        // steps so the resulting GIF shows the line visibly swinging from horizontal to vertical, exactly
        // the "place A/B at start/end, or rotate to top/bottom" behaviour that was asked for.
        const ImVec2 TargetTop = ToScreen(TextBounds[0], TextBounds[1] - TextBounds[3] * .65f);
        const ImVec2 TargetBottom = ToScreen(TextBounds[0], TextBounds[1] + TextBounds[3] * .65f);
        constexpr int Steps = 10;
        MoveTo(HandleB);
        Down();
        for (int Step = 1; Step <= Steps; ++Step)
        {
            const float T = float(Step) / Steps;
            MoveTo(ImVec2(HandleB.x + (TargetTop.x - HandleB.x) * T, HandleB.y + (TargetTop.y - HandleB.y) * T));
            Capture();
        }
        Up();
        Capture();
        {
            const auto &Source = Panel->QueryDocument().Instructions[Panel->QueryDocument().Layers.front().Root];
            Require(std::abs(Source.GradientAxis[3]) > .2f, "Dragging the B handle toward the text's top did not rotate the gradient axis.");
        }
        std::cout << "Captured dragging the B handle from the text's right/\"end\" up to above it - the "
            "gradient's own end point, independent of the shape's bounding box.\n";

        // Now drag A (still at the text's left/"start") down below the text, finishing with the axis running
        // cleanly top-to-bottom through the text instead of left-to-right - A and B fully swapped roles from
        // where they started, each placed freely by the user exactly like a Figma/Illustrator gradient line.
        MoveTo(HandleA);
        Down();
        for (int Step = 1; Step <= Steps; ++Step)
        {
            const float T = float(Step) / Steps;
            MoveTo(ImVec2(HandleA.x + (TargetBottom.x - HandleA.x) * T, HandleA.y + (TargetBottom.y - HandleA.y) * T));
            Capture();
        }
        Up();
        for (int Index = 0; Index < 6; ++Index) Capture(); // Hold a beat on the finished top-to-bottom axis.
        {
            const auto &Source = Panel->QueryDocument().Instructions[Panel->QueryDocument().Layers.front().Root];
            Require(std::abs(Source.GradientAxis[1]) > .2f, "Dragging the A handle toward the text's bottom did not rotate the gradient axis.");
            Require(std::abs(Source.GradientAxis[0]) < .1f && std::abs(Source.GradientAxis[2]) < .1f,
                "The finished axis should run essentially straight up-and-down through the text, not off to one side.");
        }
        std::cout << "Captured dragging the A handle from the text's left/\"start\" down below it, finishing "
            "with the ramp running top-to-bottom through the text - a full 90-degree reorientation, placed "
            "entirely by the two draggable endpoint handles.\n";

        ImGui::DestroyContext();
        std::cout << "PASS: captured freeform A/B gradient-axis placement on a real text shape - left-to-right "
            "by default, dragged to top-to-bottom - see " << Output.string() << "\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "FAIL: " << Failure.what() << "\n";
        return 1;
    }
}
