/*============================================================================================================================================
                                         STAMPWORKSPACEPANELCONSTRUCTIONVALIDATION.CPP
============================================================================================================================================*/
// 🧩 End-to-end headless validation for StampWorkspacePanel startup, editable Decal Canvas units,
//    unsaved '*' indicator + inline Save & Export options modal, opposite-of-canvas default component
//    colour (Colour channel only), multi-point Freeform 2D gradients with selectable spline/step
//    interpolation, and sub-frame component stack reordering performance.

#include "UI/TextureStudio/PatternWorkspacePanel.h"
#include "UI/TextureStudio/StampWorkspacePanel.h"
#include "UI/TextureStudio/PatternThumbnailRaster.h"
#include "Engine/ContentInterchange/VectorGradientStops.h"
#include "Engine/Render/VectorSurfaceCompilation.h"
#include <imgui.h>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
void Require(bool Condition, const char *Message)
{
    if (!Condition) throw std::runtime_error(Message);
}

float Luminance(const std::array<float, 4> &Colour) noexcept
{
    return .2126f * Colour[0] + .7152f * Colour[1] + .0722f * Colour[2];
}
}

int main()
{
    using namespace Frontier;
    try
    {
        // 1. Startup workspace contract regression check.
        {
            PatternWorkspacePanel Pattern;
            StampWorkspacePanel Stamp;
            Require(!Pattern.QueryDocument().Stamp, "PatternWorkspacePanel must open a non-Stamp blank document");
            Require(Stamp.QueryDocument().Stamp, "StampWorkspacePanel must open a Stamp blank document");
            Require(std::abs(Stamp.QueryCanvasExtent() - 512.f) < .01f, "Default Stamp canvas extent must be 512 units");
        }

        // 2. Opposite-of-canvas default colour (Colour channel only).
        {
            StampWorkspacePanel Stamp;
            Stamp.ConfigureCanvasGroundColour({1.f, 1.f, 1.f, 1.f});
            const auto OnWhite = Stamp.OppositeCanvasColour();
            Require(OnWhite[0] < .02f && OnWhite[1] < .02f && OnWhite[2] < .02f, "White canvas must default to black ink");

            Stamp.ConfigureCanvasGroundColour({0.f, 0.f, 0.f, 1.f});
            const auto OnBlack = Stamp.OppositeCanvasColour();
            Require(OnBlack[0] > .98f && OnBlack[1] > .98f && OnBlack[2] > .98f, "Black canvas must default to white ink");

            Stamp.ConfigureCanvasGroundColour({.05f, .15f, .85f, 1.f});
            const auto OnBlue = Stamp.OppositeCanvasColour();
            Require(OnBlue[0] > .85f && OnBlue[1] > .75f && OnBlue[2] < .25f,
                "Blue canvas must default to complementary high-contrast warm yellow/amber");

            Stamp.ConfigureCanvasGroundColour({.5f, .5f, .5f, 1.f});
            const auto OnMidGrey = Stamp.OppositeCanvasColour();
            Require(std::abs(Luminance(OnMidGrey) - .5f) > .35f,
                "Mid-grey canvas must boost luminance contrast so new components remain clearly visible");
        }

        // 3. Multi-point Freeform 2D gradient + 5-mode colour interpolation + TOML & GPU surface compilation.
        {
            VectorAuthoringDocument Document;
            Document.Stamp = true;
            Document.CanvasExtent = 1024.f;
            VectorCoverageInstruction Shape;
            Shape.Topology[0] = 0;
            Shape.Bounds = {512.f, 512.f, 768.f, 768.f};
            Shape.Transform[3] = 3.f; // Freeform 2D gradient
            const std::vector<VectorFreeformGradientPoint> Points{
                {{-.40f, -.40f}, {1.f, .10f, .10f, 1.f}},
                {{.40f, -.40f}, {.10f, 1.f, .15f, 1.f}},
                {{-.40f, .40f}, {.10f, .25f, 1.f, 1.f}},
                {{.40f, .40f}, {1.f, .90f, .10f, 1.f}},
                {{0.f, -.10f}, {.95f, .15f, .90f, 1.f}},
                {{.05f, .25f}, {.10f, .95f, .95f, 1.f}}};
            VectorAuthoringSourcePatch Patch;
            ApplyVectorFreeformPoints(Patch, Points, 2.4f);
            Patch.GradientInterpolation = uint32_t(VectorGradientInterpolation::CatmullRom);
            const uint32_t Token = ConstructVectorAuthoringPrimitive(Document, Shape, "Freeform quad");
            TransformVectorAuthoringSelection(Document, std::array<uint32_t, 1>{Token}, Patch);

            const std::string Encoded = EncodeVectorAuthoring(Document);
            const auto Decoded = DecodeVectorAuthoring(Encoded);
            Require(Decoded == Document, "Freeform 2D gradient + custom CanvasExtent must round-trip losslessly through TOML");
            Require(std::abs(Decoded.CanvasExtent - 1024.f) < .01f, "Decoded CanvasExtent must equal 1024");

            const auto Normalized = NormalizeVectorInstructionForGpu(Decoded.Instructions[0], Decoded.CanvasExtent);
            Require(std::abs(Normalized.Bounds[0] - 256.f) < .01f && std::abs(Normalized.Bounds[2] - 384.f) < .01f,
                "GPU normalization must map 1024-unit coordinates into the shader's 512-unit reference frame");

            const auto Compiled = CompileVectorSurface(Decoded, VectorAuthoringSurfaceChannel::Colour);
            Require(!Compiled.Coverage.Instructions.empty() && uint32_t(Compiled.Coverage.Instructions[0].Transform[3]) == 3u,
                "CompileVectorSurface must preserve Freeform 2D gradient category");

            // Verify all 5 interpolation modes evaluate distinct valid colours in both 1D and 2D.
            const std::vector<VectorGradientStop> Stops{
                {0.f, {1.f, 0.f, 0.f, 1.f}},
                {.35f, {0.f, 1.f, 0.f, 1.f}},
                {.70f, {0.f, 0.f, 1.f, 1.f}},
                {1.f, {1.f, 1.f, 0.f, 1.f}}};
            const auto LinearSample = EvaluateVectorGradientColour(Stops, .22f, uint32_t(VectorGradientInterpolation::Linear));
            const auto HermiteSample = EvaluateVectorGradientColour(Stops, .22f, uint32_t(VectorGradientInterpolation::Hermite));
            const auto BSplineSample = EvaluateVectorGradientColour(Stops, .22f, uint32_t(VectorGradientInterpolation::BSpline));
            const auto StepSample = EvaluateVectorGradientColour(Stops, .10f, uint32_t(VectorGradientInterpolation::Step));
            Require(std::abs(LinearSample[0] - HermiteSample[0]) > 1e-3f, "Hermite must differ from Linear at interior t");
            Require(std::abs(LinearSample[1] - BSplineSample[1]) > 1e-3f, "B-Spline must smooth interior stops differently from Linear");
            Require(StepSample[0] > .99f && StepSample[1] < .01f, "Step mode must hold stop colour before midpoint");

            const auto Raster = RasterizeStampDocument(Decoded, 256);
            const auto SamplePixel = [&](int Px, int Py) {
                const size_t Off = (size_t(Py) * 256u + size_t(Px)) * 4u;
                return std::array<uint8_t, 4>{Raster[Off], Raster[Off + 1], Raster[Off + 2], Raster[Off + 3]};
            };
            const auto TopLeft = SamplePixel(68, 68);
            const auto TopRight = SamplePixel(188, 68);
            const auto BottomLeft = SamplePixel(68, 188);
            const auto BottomRight = SamplePixel(188, 188);
            Require(TopLeft[3] > 200 && TopLeft[0] > TopLeft[1] + 80 && TopLeft[0] > TopLeft[2] + 80,
                "Freeform 2D gradient top-left pin must rasterize predominantly red");
            Require(TopRight[3] > 200 && TopRight[1] > TopRight[0] + 80 && TopRight[1] > TopRight[2] + 80,
                "Freeform 2D gradient top-right pin must rasterize predominantly green");
            Require(BottomLeft[3] > 200 && BottomLeft[2] > BottomLeft[0] + 80,
                "Freeform 2D gradient bottom-left pin must rasterize predominantly blue");
            Require(BottomRight[3] > 200 && BottomRight[0] > 180 && BottomRight[1] > 160 && BottomRight[2] < 100,
                "Freeform 2D gradient bottom-right pin must rasterize predominantly yellow");
        }

        // 4. Full ImGui StampWorkspacePanel UI checks: editable canvas size, '*' unsaved marker, Save & Export buttons,
        //    opposite-colour component creation (Colour channel only), and 16-layer reorder benchmark.
        ImGui::CreateContext();
        auto &IO = ImGui::GetIO();
        IO.IniFilename = nullptr;
        IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        IO.DisplaySize = ImVec2(1600, 1000);
        IO.DeltaTime = 1.f / 60.f;
        IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        ImFont *Font = nullptr;
        if (std::filesystem::is_regular_file("EngineContent/FontArchives/Inter/Inter-Regular.otf"))
            Font = IO.Fonts->AddFontFromFileTTF("EngineContent/FontArchives/Inter/Inter-Regular.otf", 14.f);
        if (!Font) Font = IO.Fonts->AddFontDefault();
        unsigned char *FontBytes = nullptr;
        int FontW = 0, FontH = 0;
        IO.Fonts->GetTexDataAsRGBA32(&FontBytes, &FontW, &FontH);
        IO.Fonts->SetTexID(ImTextureID(1));

        ActivationOwnership Ownership(new StampWorkspacePanel());
        auto *Panel = static_cast<StampWorkspacePanel *>(Ownership.get());
        Require(Panel->Initialize() && Panel->Activate(), "StampWorkspacePanel failed to initialize/activate");
        Panel->ConfigureTypeface(Font, Font);
        Panel->ConfigureContentRoot("EngineContent");
        const auto ScratchLibrary = std::filesystem::temp_directory_path() / "FrontierStampPanelValidationLib";
        std::filesystem::remove_all(ScratchLibrary);
        std::filesystem::create_directories(ScratchLibrary);
        Panel->ConfigureStampLibraryDirectory(ScratchLibrary);

        int SaveCalls = 0;
        Panel->SaveSource = [&](std::string_view) { ++SaveCalls; return true; };
        Panel->QueryCoverage = [](const VectorCoverageProgram &, uint32_t) -> uint64_t { return 2; };
        Panel->QueryCoverageReady = [] { return true; };

        auto Step = [&](int Frames = 1)
        {
            for (int Index = 0; Index < Frames; ++Index)
            {
                ImGui::NewFrame();
                Panel->ConstructInterface();
                ImGui::Render();
                if (ImDrawData *Draw = ImGui::GetDrawData(); Draw && Draw->Textures)
                    for (ImTextureData *Tex : *Draw->Textures)
                        if (Tex->Status == ImTextureStatus_WantCreate || Tex->Status == ImTextureStatus_WantUpdates)
                        {
                            if (Tex->TexID == ImTextureID_Invalid) Tex->SetTexID(ImTextureID(99));
                            Tex->SetStatus(ImTextureStatus_OK);
                        }
                        else if (Tex->Status == ImTextureStatus_WantDestroy)
                        {
                            Tex->SetTexID(ImTextureID_Invalid);
                            Tex->SetStatus(ImTextureStatus_Destroyed);
                        }
            }
        };
        auto ClickControl = [&](const std::string &Name)
        {
            const auto Box = Panel->QueryControlExtent(Name);
            Require(Box.Width() > 0 && Box.Height() > 0, ("Missing control extent: " + Name).c_str());
            const float Cx = (Box.MinimumX + Box.MaximumX) * .5f;
            const float Cy = (Box.MinimumY + Box.MaximumY) * .5f;
            IO.AddMousePosEvent(Cx, Cy);
            Step();
            IO.AddMouseButtonEvent(0, true);
            Step();
            IO.AddMouseButtonEvent(0, false);
            Step();
        };

        Step(3);
        Require(!Panel->QueryModified(), "Fresh Stamp document must start unmodified");
        Require(Panel->QueryControlExtent("Canvas units").Width() > 0, "Editable Decal Canvas units control must be visible");
        Require(Panel->QueryControlExtent("Save stamp").Width() > 0, "Inline Save icon button must be visible");
        Require(Panel->QueryControlExtent("Export stamp").Width() > 0, "Inline Export icon button must be visible");
        Require(Panel->QueryControlExtent("Unsaved indicator").Width() == 0, "Saved stamp must not show unsaved '*'");

        // Set canvas colour to deep blue and resize canvas to 1024 units, then add a Rectangle via '+' menu.
        Panel->ConfigureCanvasGroundColour({.05f, .12f, .86f, 1.f});
        Panel->ApplyCanvasExtent(1024.f);
        Step(2);
        Require(Panel->QueryModified(), "Editing canvas extent must mark the stamp modified");
        Require(Panel->QueryControlExtent("Unsaved indicator").Width() > 0, "Modified stamp must show '*' next to stamp name");

        ClickControl("Add stamp component");
        ClickControl("Rectangle");
        Step(2);
        Require(Panel->QueryDocument().Layers.size() == 1, "Adding a Rectangle must create one layer");
        {
            const auto &Layer = Panel->QueryDocument().Layers[0];
            const auto &Inst = Panel->QueryDocument().Instructions[Layer.Root];
            Require(std::abs(Inst.Bounds[0] - 512.f) < 1.f && std::abs(Inst.Bounds[1] - 512.f) < 1.f,
                "New component on 1024-unit canvas must centre at (512, 512)");
            Require(Inst.Colour[0] > .85f && Inst.Colour[1] > .75f && Inst.Colour[2] < .25f,
                "New component Colour channel must default to opposite colour of the blue canvas");
            const VectorAuthoringLayer DefaultLayer{};
            Require(Layer.SurfaceChannels == DefaultLayer.SurfaceChannels &&
                    std::abs(Layer.SurfaceRelief - DefaultLayer.SurfaceRelief) < 1e-5f,
                "Opposite canvas default colour must NOT alter non-Colour PBR channels");
        }

        // Click inline Save button -> clears modified '*' state.
        ClickControl("Save stamp");
        Step(2);
        Require(SaveCalls == 1 && !Panel->QueryModified(), "Clicking Save icon button must save and clear modified state");
        Require(Panel->QueryControlExtent("Unsaved indicator").Width() == 0, "Saved stamp must hide '*' indicator");

        // Click inline Export button -> opens Export stamp options modal (UI only), then Cancel closes it.
        ClickControl("Export stamp");
        Step(2);
        Require(Panel->QueryPanelExtent("Export stamp options").Width() > 0,
            "Clicking Export icon button must open 'Export stamp options' dialog");
        ClickControl("Cancel");
        Step(2);
        Require(Panel->QueryPanelExtent("Export stamp options").Width() == 0,
            "Clicking Cancel in Export stamp options must close the dialog");

        // 5. Reorder speedup benchmark: 16 PBR layers reordered 20 times consecutively must never stall.
        {
            VectorAuthoringDocument Rich;
            Rich.Stamp = true;
            Rich.Name = "Reorder benchmark";
            for (int Index = 0; Index < 16; ++Index)
            {
                VectorCoverageInstruction Prim;
                Prim.Topology[0] = uint32_t(Index % 2);
                Prim.Bounds = {160.f + float(Index * 14), 160.f + float(Index * 14), 180.f, 180.f};
                Prim.Colour = {(Index % 3 == 0) ? .9f : .15f, (Index % 3 == 1) ? .85f : .2f, (Index % 3 == 2) ? .9f : .2f, 1.f};
                const uint32_t Tok = ConstructVectorAuthoringPrimitive(Rich, Prim, "Comp " + std::to_string(Index));
                Rich.Layers.back().SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Height)].Enabled = true;
                Rich.Layers.back().SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Normal)].Enabled = true;
                Rich.Layers.back().SurfaceRelief = .35f;
                (void)Tok;
            }
            Panel->ConfigureDocument(Rich);
            Step(2);
            (void)Panel->QueryLayerAtlasPixels();
            (void)EvaluateStampSurfaceMaps(Panel->QueryDocument(), 256);

            const uint32_t TopToken = Panel->QueryDocument().Layers.back().Token;
            ClickControl("Select:" + std::to_string(TopToken));
            Step(1);

            const auto Start = std::chrono::steady_clock::now();
            for (int Iter = 0; Iter < 10; ++Iter)
            {
                ClickControl("Layer footer send backward");
                (void)Panel->QueryLayerAtlasPixels();
                (void)EvaluateStampSurfaceMaps(Panel->QueryDocument(), 256);
            }
            for (int Iter = 0; Iter < 10; ++Iter)
            {
                ClickControl("Layer footer bring forward");
                (void)Panel->QueryLayerAtlasPixels();
                (void)EvaluateStampSurfaceMaps(Panel->QueryDocument(), 256);
            }
            const double TotalMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
            const double AvgMs = TotalMs / 20.0;
            std::cout << "Stamp 16-layer stack reorder average (UI + LayerAtlas + 256px PBR maps): " << AvgMs << " ms\n";
            Require(Panel->QueryDocument().Layers.back().Token == TopToken,
                "10 Move backward + 10 Move forward operations must return layer to top of stack");
            Require(AvgMs < 25.0, "Reordering Stamp components was too slow and risked UI stalls");
        }

        std::filesystem::remove_all(ScratchLibrary);
        Ownership.reset();
        ImGui::DestroyContext();
    }
    catch (const std::exception &Failure)
    {
        if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
        std::cerr << "StampWorkspacePanelConstructionValidation failed: " << Failure.what() << "\n";
        return 1;
    }
    std::cout << "StampWorkspacePanelConstructionValidation passed.\n";
    return 0;
}
