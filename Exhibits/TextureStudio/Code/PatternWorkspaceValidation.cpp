/*============================================================================================================================================
                                                      PATTERNWORKSPACEVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Drive the real native Pattern UI with pointer/keyboard input and capture its ImGui triangles with actual Vulkan canvas pixels.

#include "UI/TextureStudio/PatternWorkspacePanel.h"
#include "Engine/ContentInterchange/VectorAuthoringEditing.h"
#include "Engine/Interface/DropdownControls.h"
#include "Engine/Render/VectorAuthoringCoverageRendering.h"
#include "Engine/Render/VectorSelectionEvaluation.h"
#include "Engine/Render/VectorSurfaceCompilation.h"
#include "AtlasReadback.h"
#include "RasterizeLibraryEvidence.h"
#include <iostream>
#include <fstream>
#include <limits>
#include <imgui_internal.h>

int main(int Count, char **Arguments)
{
    using namespace Frontier;
    try
    {
        AssertRasterCondition(Count == 5 || (Count == 6 && (std::string_view(Arguments[5]) == "--serve" || std::string_view(Arguments[5]) == "--animate" || std::string_view(Arguments[5]) == "--animate-join" || std::string_view(Arguments[5]) == "--animate-endpoint-join" || std::string_view(Arguments[5]) == "--animate-clipboard" || std::string_view(Arguments[5]) == "--animate-segment" || std::string_view(Arguments[5]) == "--animate-multipoint" || std::string_view(Arguments[5]) == "--animate-placement" || std::string_view(Arguments[5]) == "--animate-continuation")), "Expected Vulkan driver, compute shader, DM Sans face and evidence directory.");
        const std::filesystem::path Destination(Arguments[4]);
        std::filesystem::create_directories(Destination);
        AtlasValidationDevice Device(Arguments[1], VK_API_VERSION_1_2);
        const auto Shader = DecodeShaderArchive(Arguments[2]);
        uint32_t BackendChecks = 0;
        {
            VectorAuthoringCoverageRendering Rendering;
            Rendering.Initialize(Device.QueryAccess(), RasterShader{Shader, "main"});
            const auto ConfirmBackend = [&](bool Condition, const char *Caption) { AssertRasterCondition(Condition, Caption); ++BackendChecks; };
            VectorCoverageProgram Program;
            Program.Instructions.emplace_back();
            Program.Instructions[0].Bounds = {256, 256, 160, 160};
            Program.Instructions[0].Colour = {1,0,0,1};
            Program.Instructions[0].StrokeColour = {0,0,1,1};
            Program.Instructions[0].Topology[3] |= VectorCoverageStrokeColourExplicit;
            Program.Instructions[0].Transform[1] = 12;
            uint64_t BackendSerial = 0;
            const auto CaptureBackend = [&](int Slot, uint64_t Completed)
            {
                return Device.Capture(Rendering.QueryImage(size_t(Slot)), {512, 512}, [&](VkCommandBuffer Commands)
                {
                    Rendering.ConstructCommands(Commands, Completed, BackendSerial + 1);
                }, [&]() { Rendering.ConfirmSubmission(); ++BackendSerial; });
            };
            int First = Rendering.SelectProgram(Program);
            auto Pixels = CaptureBackend(First, 0);
            ConfirmBackend(Pixels[(256 * 512 + 256) * 4 + 3] == 255, "Native residency did not dispatch first coverage image.");
            const size_t FillPixel = (256 * 512 + 256) * 4, StrokePixel = (256 * 512 + 174) * 4;
            ConfirmBackend(Pixels[FillPixel] > 240 && Pixels[FillPixel + 2] < 12 && Pixels[StrokePixel + 2] > 240 && Pixels[StrokePixel] < 12,
                "Fill and stroke did not retain independently authored GPU colours.");
            ConfirmBackend(Rendering.SelectProgram(Program) == First, "Cached native image changed slots.");
            (void)CaptureBackend(First, 0);
            ConfirmBackend(Rendering.QueryDispatchCount() == 1, "Cached native image was recomputed.");
            Program.Instructions[0].Transform[2] = .25f;
            int Second = Rendering.SelectProgram(Program);
            Pixels = CaptureBackend(Second, 0);
            ConfirmBackend(Second != First && Pixels[(256 * 512 + 256) * 4 + 3] >= 63 && Pixels[(256 * 512 + 256) * 4 + 3] <= 64, "Native residency overwrote a sampled image or lost new coverage.");
            Program.Instructions[0].Transform[2] = .5f;
            int Third = Rendering.SelectProgram(Program);
            Pixels = CaptureBackend(Third, 0);
            ConfirmBackend(Third != First && Third != Second, "Native residency failed to retain its third output slot.");
            Program.Instructions[0].Transform[2] = .75f;
            ConfirmBackend(Rendering.SelectProgram(Program) == Third, "Exhausted native residence did not keep the last valid image.");
            Pixels = CaptureBackend(Third, 4);
            ConfirmBackend(Pixels[(256 * 512 + 256) * 4 + 3] >= 127 && Pixels[(256 * 512 + 256) * 4 + 3] <= 128, "Deferred native edit overwrote an in-flight image.");
            int Retired = Rendering.SelectProgram(Program);
            Pixels = CaptureBackend(Retired, 5);
            ConfirmBackend(Rendering.QueryDispatchCount() == 4 && Pixels[(256 * 512 + 256) * 4 + 3] >= 191, "Retired native output did not receive the deferred edit.");
        }
        uint32_t ZoomResolutionChecks = 0;
        {
            // 📝 A zoomed canvas must re-bake at the resolution it is actually displayed at, not keep
            // sampling a fixed-size raster and stretch it — that stretch is what reads as pixelated.
            VectorAuthoringCoverageRendering Rendering;
            Rendering.Initialize(Device.QueryAccess(), RasterShader{Shader, "main"});
            const auto ConfirmZoom = [&](bool Condition, const char *Caption) { AssertRasterCondition(Condition, Caption); ++ZoomResolutionChecks; };
            VectorCoverageProgram Program;
            Program.Instructions.emplace_back();
            Program.Instructions[0].Bounds = {256, 256, 160, 160};
            uint64_t ZoomSerial = 0;
            const auto CaptureAt = [&](int Slot, uint64_t Completed, uint32_t Extent)
            {
                return Device.Capture(Rendering.QueryImage(size_t(Slot)), {Extent, Extent}, [&](VkCommandBuffer Commands)
                {
                    Rendering.ConstructCommands(Commands, Completed, ZoomSerial + 1);
                }, [&]() { Rendering.ConfirmSubmission(); ++ZoomSerial; });
            };
            // A low DisplayExtent hint (zoomed-out canvas) must cap new content below the full affordable tile.
            const int Slot = Rendering.SelectProgram(Program, 128);
            ConfirmZoom(Rendering.QuerySlotExtent(size_t(Slot)) == 128, "A zoomed-out DisplayExtent hint did not shrink a freshly authored document's bake tile.");
            auto SmallPixels = CaptureAt(Slot, 0, 128);
            ConfirmZoom(SmallPixels[(64 * 128 + 64) * 4 + 3] == 255, "Shrunk native bake lost the unchanged document's coverage.");
            const uint64_t DispatchesBeforeZoomIn = Rendering.QueryDispatchCount();
            // Zooming in on the same, unmodified content must re-dispatch into a sharper pooled tile.
            const int Sharpened = Rendering.SelectProgram(Program, 1024);
            ConfirmZoom(Sharpened == Slot, "A DisplayExtent-only change moved unchanged content to a different slot.");
            ConfirmZoom(Rendering.QuerySlotExtent(size_t(Sharpened)) == 1024, "Zooming in on unchanged content did not re-bake it to full resolution.");
            auto SharpPixels = CaptureAt(Sharpened, 1, 1024);
            ConfirmZoom(Rendering.QueryDispatchCount() == DispatchesBeforeZoomIn + 1, "A DisplayExtent-only tier change skipped its GPU re-dispatch.");
            ConfirmZoom(SharpPixels[(512 * 1024 + 512) * 4 + 3] == 255, "Full-resolution re-bake after zooming in lost the unchanged document's coverage.");
            // Zooming back out must drop the resolution again instead of permanently holding the sharper tile.
            const int Settled = Rendering.SelectProgram(Program, 128);
            ConfirmZoom(Settled == Slot && Rendering.QuerySlotExtent(size_t(Settled)) == 128, "Zooming back out did not relinquish the sharper pooled tile.");
        }
        uint32_t SourceChecks = 0;
        {
            VectorAuthoringDocument Document;
            VectorCoverageInstruction Primitive;
            Primitive.Bounds = {256, 256, 160, 160};
            const auto FirstToken = ConstructVectorAuthoringPrimitive(Document, Primitive, "Rectangle");
            const auto SecondToken = ConstructVectorAuthoringPrimitive(Document, Primitive, "Rectangle");
            Document.Layers[1].Root = Document.Layers[0].Root;
            const auto Original = Document;
            Document.Layers[0].SurfaceStructure = VectorAuthoringSurfaceStructure::Weave;
            Document.Layers[0].TextileGeometry = VectorAuthoringTextileGeometry::WeaveYarn;
            Document.Layers[0].SurfaceRelief = .35f;
            const std::string WeaveSource = EncodeVectorAuthoring(Document);
            AssertRasterCondition(DecodeVectorAuthoring(WeaveSource) == Document,
                "Weave yarn classification did not survive native source round trip."); ++SourceChecks;
            AssertRasterCondition(WeaveSource.find("weave_role = 'yarn'") != std::string::npos,
                "Native vector TOML omitted the web-compatible weave-yarn key."); ++SourceChecks;
            std::string WebSemanticSource = WeaveSource;
            for (const std::string MetadataLine : {"surface_structure = 'weave'\n", "textile_geometry = 'weave_yarn'\n"})
            {
                const auto MetadataStart = WebSemanticSource.find(MetadataLine);
                AssertRasterCondition(MetadataStart != std::string::npos, "Native textile metadata line is missing."); ++SourceChecks;
                WebSemanticSource.erase(MetadataStart, MetadataLine.size());
            }
            AssertRasterCondition(DecodeVectorAuthoring(WebSemanticSource) == Document,
                "Web weave-yarn semantics did not map into native vector authoring."); ++SourceChecks;
            constexpr std::array<const char *, 3> StitchSemantics{"holes", "thread", "highlight"};
            constexpr std::array<VectorAuthoringTextileGeometry, 3> StitchGeometry{
                VectorAuthoringTextileGeometry::NeedleEntry,
                VectorAuthoringTextileGeometry::StitchThread,
                VectorAuthoringTextileGeometry::UpperThreadPass};
            for (size_t Index = 0; Index < StitchSemantics.size(); ++Index)
            {
                std::string StitchSource = WebSemanticSource;
                const std::string WeaveEntry = "weave_role = 'yarn'";
                const auto WeaveStart = StitchSource.find(WeaveEntry);
                AssertRasterCondition(WeaveStart != std::string::npos, "Native weave semantic key is missing."); ++SourceChecks;
                StitchSource.replace(WeaveStart, WeaveEntry.size(), "stitch_role = '" + std::string(StitchSemantics[Index]) + "'");
                const auto ImportedStitch = DecodeVectorAuthoring(StitchSource);
                AssertRasterCondition(ImportedStitch.Layers[0].SurfaceStructure == VectorAuthoringSurfaceStructure::Stitch &&
                    ImportedStitch.Layers[0].TextileGeometry == StitchGeometry[Index],
                    "Web stitch semantic did not map into its native textile geometry."); ++SourceChecks;
            }
            Document.Layers[0].SurfaceStructure = VectorAuthoringSurfaceStructure::Stitch;
            bool MismatchedWeaveRefused = false;
            try { (void)EncodeVectorAuthoring(Document); } catch (const std::exception &) { MismatchedWeaveRefused = true; }
            AssertRasterCondition(MismatchedWeaveRefused, "Native vector source accepted weave yarn geometry classified as a stitch."); ++SourceChecks;
            Document.Layers[0].SurfaceStructure = VectorAuthoringSurfaceStructure::Weave;
            Document.Layers[0].SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Normal)].Enabled = false;
            bool MissingWeaveNormalRefused = false;
            try { (void)EncodeVectorAuthoring(Document); } catch (const std::exception &) { MissingWeaveNormalRefused = true; }
            AssertRasterCondition(MissingWeaveNormalRefused, "Native weave source accepted disabled Normal contribution."); ++SourceChecks;
            Document.Layers[0].SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Normal)].Enabled = true;
            Document.Layers[0].SurfaceStructure = VectorAuthoringSurfaceStructure::Freeform;
            Document.Layers[0].TextileGeometry = VectorAuthoringTextileGeometry::Unclassified;
            const std::array<uint32_t, 1> FirstSelection{FirstToken};
            VectorAuthoringSourcePatch Patch; Patch.TranslationX = 24; Patch.Colour = std::array<float, 4>{.2f, .8f, .5f, 1};
            Patch.StrokeColour = std::array<float, 4>{.1f, .2f, .9f, 1};
            TransformVectorAuthoringSelection(Document, FirstSelection, Patch);
            AssertRasterCondition(Document.Instructions[Document.Layers[1].Root] == Original.Instructions[0], "Shared source edit changed an unselected source."); ++SourceChecks;
            AssertRasterCondition(Document.Instructions[Document.Layers[0].Root].Bounds[0] == 280, "Source transform lost its translation."); ++SourceChecks;
            AssertRasterCondition(Document.Instructions[Document.Layers[0].Root].StrokeColour == *Patch.StrokeColour &&
                Document.Instructions[Document.Layers[1].Root].StrokeColour == Original.Instructions[0].StrokeColour,
                "Independent stroke colour did not follow only the selected shared source."); ++SourceChecks;
            AssertRasterCondition(DecodeVectorAuthoring(EncodeVectorAuthoring(Document)) == Document,
                "Independent fill/stroke colours did not survive source serialization."); ++SourceChecks;
            Document.Layers[0].Locked = true;
            const auto Protected = Document;
            bool Refused = false;
            try { TransformVectorAuthoringSelection(Document, FirstSelection, Patch); } catch (const std::exception &) { Refused = true; }
            AssertRasterCondition(Refused && Document == Protected, "Protected source edit was not atomic."); ++SourceChecks;
            Document.Layers[0].Locked = false;
            const std::array<uint32_t, 2> Both{FirstToken, SecondToken};
            const auto Combined = IntegrateVectorAuthoringBoolean(Document, Both, VectorCoverageCategory::Difference);
            AssertRasterCondition(Document.Layers.size() == 1 && Document.Layers[0].Token == Combined, "Native Boolean integration lost the lower identity."); ++SourceChecks;
            AssertRasterCondition(DecodeVectorAuthoring(EncodeVectorAuthoring(Document)) == Document, "Native Boolean edit did not persist source TOML."); ++SourceChecks;
            Document = Original;
            VectorCoverageInstruction Mask = Primitive, Operation;
            Mask.Colour = {.25f, .25f, .25f, 1};
            Document.Instructions.push_back(Mask);
            Operation.Topology = {25, Document.Layers[0].Root, uint32_t(Document.Instructions.size() - 1), 0};
            Document.Layers[0].Root = uint32_t(Document.Instructions.size());
            Document.Instructions.push_back(Operation);
            TransformVectorAuthoringSelection(Document, FirstSelection, Patch);
            const auto &Root = Document.Instructions[Document.Layers[0].Root];
            AssertRasterCondition(Document.Instructions[Root.Topology[2]].Colour == Mask.Colour, "Owner paint edit changed an expression mask's luminance."); ++SourceChecks;
            AssertRasterCondition(Document.Instructions[Root.Topology[2]].Bounds[0] == 280, "Expression mask did not follow source geometry."); ++SourceChecks;
            PartitionVectorAuthoringSelection(Document, FirstSelection);
            AssertRasterCondition(Document.Layers.size() == 1 && Document.Layers[0].Token == SecondToken, "Source deletion removed unselected artwork."); ++SourceChecks;
            AssertRasterCondition(Document.Instructions.size() == 1, "Deleted source expressions were not reclaimed."); ++SourceChecks;
        }
        VectorCoverageRasterization Raster;
        Raster.Initialize(Device.QueryAccess(), {Shader, "main"}, 512, 512);
        VectorSelectionEvaluation SelectionEvaluation;
        const auto SelectionShader = DecodeShaderArchive(std::filesystem::path(Arguments[2]).parent_path() / "VectorSelectionCompute.spv");
        SelectionEvaluation.Initialize(Device.QueryAccess(), RasterShader{SelectionShader, "main"});
        uint64_t Serial = 0;
        std::optional<VectorCoverageProgram> Requested, Displayed;
        uint32_t RequestedExtent = 0;
        std::vector<unsigned char> Coverage(512 * 512 * 4);
        ImGui::CreateContext();
        auto &Input = ImGui::GetIO();
        Input.IniFilename = nullptr;
        Input.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
        Input.DisplaySize = ImVec2(1440, 900);
        Input.DeltaTime = 1.f / 60;
        auto *Typeface = Input.Fonts->AddFontFromFileTTF(Arguments[3], 16);
        const auto LightFilename = std::filesystem::path(Arguments[3]).parent_path() / "DMSans-Light.ttf";
        auto *LightTypeface = Input.Fonts->AddFontFromFileTTF(LightFilename.string().c_str(), 16);
        AssertRasterCondition(Typeface, "Pinned reference typeface did not load.");
        unsigned char *Atlas = nullptr; int AtlasWidth = 0, AtlasHeight = 0;
        Input.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        Input.Fonts->TexDesiredFormat = ImTextureFormat_RGBA32;
        ActivationOwnership Ownership(new PatternWorkspacePanel());
        auto &Workspace = *static_cast<PatternWorkspacePanel *>(Ownership.get());
        AssertRasterCondition(Workspace.Initialize() && Workspace.Activate(), "Native Pattern lifecycle activation failed.");
        Workspace.ConfigureTypeface(Typeface, LightTypeface ? LightTypeface : Typeface);
        Workspace.QueryCoverage = [&](const VectorCoverageProgram &Program, uint32_t Extent) { Requested = Program; RequestedExtent = Extent; return uint64_t(2); };
        Workspace.QuerySelectionToken = [&](const VectorAuthoringDocument &Document, PlanePoint Point)
        {
            return SelectionEvaluation.QueryToken(ConstructVectorSelectionProgram(Document), {Point.X, Point.Y});
        };
        std::string SavedSource;
        Workspace.SaveSource = [&](std::string_view Source) { SavedSource = Source; return true; };
        uint32_t DockChecks = 0;
        {
            PanelDockspace Default, Alternate;
            const auto RegisterPanels = [&](PanelDockspace &Directory)
            {
                AssertRasterCondition(Directory.Register({"a", "Left", []() { ImGui::TextUnformatted("Stable source"); }}), "Native default panel registration failed.");
                AssertRasterCondition(Directory.Register({"b", "Right", []() { ImGui::TextUnformatted("Stable inspection"); }}), "Native second panel registration failed.");
                AssertRasterCondition(Directory.ConfigureColumns({{"a", .3f}, {"b", .7f}}), "Native default columns refused.");
            };
            RegisterPanels(Default);
            RegisterPanels(Alternate);
            AssertRasterCondition(Alternate.ConfigureDockspaceKey("Independent workspace"), "Workspace identity refused before seating.");
            for (int Index = 0; Index < 6; ++Index)
            {
                ImGui::NewFrame();
                if (Index % 2) Alternate.ConstructInterface(1);
                else Default.ConstructInterface(1);
                ImGui::Render();
            }
            AssertRasterCondition(Default.QueryDockspace() != Alternate.QueryDockspace(), "Independent workspaces share a dockspace identity."); ++DockChecks;
            AssertRasterCondition(ImGui::FindWindowByName("Left###FrontierPanel-a") != nullptr, "Default window identity changed during authoring extension."); ++DockChecks;
            const auto Layout = Default.EncodeLayout();
            std::string Diagnostic;
            AssertRasterCondition(PanelDockspace::ValidateLayout(Layout, Diagnostic), "Retained default workspace layout is no longer valid."); ++DockChecks;
            AssertRasterCondition(ImGui::DockBuilderGetNode(Default.QueryDockspace()) != nullptr, "Inactive default docking was destroyed during workspace switching."); ++DockChecks;
        }
        uint32_t DropdownChecks = 0;
        const auto ConfirmDropdown = [&](bool Condition, const char *Caption) { AssertRasterCondition(Condition, Caption); ++DropdownChecks; };
        for (float Scale : {1.f, 1.5f, 2.f})
        {
            const auto Style = ImGui::GetStyle();
            ImGui::NewFrame(); ImGui::Begin("Dropdown contract validation");
            ImGui::OpenPopup("Dropdown contract");
            if (DropdownControlSurface::BeginMenu("Dropdown contract", Scale))
            {
                ConfirmDropdown(std::abs(ImGui::GetStyle().PopupRounding - 8 * Scale) < .01f, "Shared dropdown rounding differs.");
                ConfirmDropdown(ImGui::GetStyle().WindowPadding.x == 12 * Scale && ImGui::GetStyle().WindowPadding.y == 10 * Scale,
                    "Shared dropdown padding differs.");
                ConfirmDropdown(ImGui::GetStyle().Colors[ImGuiCol_PopupBg].w == 1, "Dropdown background is not solid.");
                DropdownControlSurface::Option("Selected option", true, Scale);
                ConfirmDropdown(std::abs(ImGui::GetItemRectSize().y - 28 * Scale) < .01f, "Dropdown option height differs.");
                DropdownControlSurface::Option("Disabled option", false, Scale, false);
                ConfirmDropdown(ImGui::GetCurrentContext()->LastItemData.ItemFlags & ImGuiItemFlags_Disabled, "Dropdown disabled option lost input protection.");
                ImGui::CloseCurrentPopup(); DropdownControlSurface::EndMenu();
            }
            else ConfirmDropdown(false, "Shared dropdown did not open.");
            ImGui::End(); ImGui::Render();
            ConfirmDropdown(ImGui::GetStyle().PopupRounding == Style.PopupRounding
                && ImGui::GetStyle().WindowPadding.x == Style.WindowPadding.x
                && ImGui::GetStyle().Colors[ImGuiCol_PopupBg].w == Style.Colors[ImGuiCol_PopupBg].w,
                "Dropdown styles leaked to other controls.");
        }
        {
            const PlaneExtent Button{20, 20, 220, 50};
            const auto Menu = ControlKit::DropdownMenuExtent(Button, 3);
            ConfirmDropdown(Menu.Height() == 2 * ControlKitTokens::DropdownPaddingY
                + 3 * ControlKitTokens::DropdownRowHeight + 2 * ControlKitTokens::DropdownRowGap,
                "PixelSpace dropdown dimensions differ from the shared native dropdown geometry.");
            ImGui::NewFrame(); ImGui::Begin("PixelSpace dropdown contract");
            PixelSpace Surface; (void)Surface.Begin(SurfaceLayer::Window, 1440, 900, 1);
            const char *Options[]{"One", "Two", "Three"}; uint32_t Chosen = 0;
            ControlPointer Pointer;
            Pointer.X = Menu.MinimumX + ControlKitTokens::DropdownPaddingX + 5;
            Pointer.Y = Menu.MinimumY + ControlKitTokens::DropdownPaddingY
                + ControlKitTokens::DropdownRowHeight + ControlKitTokens::DropdownRowGap + 5;
            Pointer.Released = true;
            const auto Hit = ControlKit::DropdownMenu(Surface, Button, Options, 3, 0, Pointer, Chosen);
            ConfirmDropdown(Hit.Clicked && Chosen == 1, "Shared PixelSpace row hit geometry lost selection.");
            ImGui::End(); ImGui::Render();
        }
        uint32_t ContourChecks = 0;
        {
            ImGui::NewFrame();
            ImGui::Begin("Control contour validation");
            VectorAuthoringControlSurface Boundaries;
            for (float Scale : {1.f, 1.5f, 2.f})
            {
                Boundaries.Scale = Scale;
                Boundaries.BeginCard("Subtle card");
                AssertRasterCondition(std::abs(ImGui::GetStyle().ChildRounding - 8 * Scale) < .01f, "Native card radius is not subtly rounded."); ++ContourChecks;
                const auto MeasureTopInset = [&](const char *Caption, bool Capsule)
                {
                    const auto Origin = ImGui::GetCursorScreenPos();
                    auto *Ink = ImGui::GetWindowDrawList();
                    const int Start = Ink->VtxBuffer.Size;
                    Boundaries.Button(Caption, 200 * Scale, false, {}, Capsule);
                    float Inset = 200 * Scale;
                    for (int Index = Start; Index < Ink->VtxBuffer.Size; ++Index)
                        if (std::abs(Ink->VtxBuffer[Index].pos.y - Origin.y) < .7f * Scale)
                            Inset = std::min(Inset, Ink->VtxBuffer[Index].pos.x - Origin.x);
                    return Inset;
                };
                const float Rectangle = MeasureTopInset("Rectangle action", false);
                const float Pill = MeasureTopInset("Pill action", true);
                AssertRasterCondition(Pill > Rectangle + 2 * Scale, "Rectangular actions still have pill-shaped native triangle contours."); ++ContourChecks;
                Boundaries.EndCard();
            }
            ImGui::End();
            ImGui::Render();
        }
        // 📝 Honor each dynamic atlas texture instead of rebuilding the legacy atlas after UVs were recorded.
        const auto CaptureTextures = [&]()
        {
            std::vector<LibraryCaptureTexture> Textures{{2, Coverage.data(), 512, 512}};
            for (auto *Texture : Input.Fonts->TexList)
            {
                if (Texture->Status == ImTextureStatus_WantCreate || Texture->Status == ImTextureStatus_WantUpdates)
                {
                    Texture->SetTexID(ImTextureID(100 + Texture->UniqueID));
                    Texture->SetStatus(ImTextureStatus_OK);
                }
                if (Texture->Status == ImTextureStatus_Destroyed) continue;
                if (Texture->Status == ImTextureStatus_WantDestroy)
                {
                    Texture->SetTexID(ImTextureID_Invalid);
                    Texture->SetStatus(ImTextureStatus_Destroyed);
                    continue;
                }
                AssertRasterCondition(Texture->Format == ImTextureFormat_RGBA32 && Texture->Pixels,
                    "CPU capture requires live RGBA font texture pixels.");
                Textures.push_back({uint64_t(Texture->TexID), Texture->Pixels, Texture->Width, Texture->Height});
            }
            Atlas = Input.Fonts->TexData->Pixels;
            AtlasWidth = Input.Fonts->TexData->Width; AtlasHeight = Input.Fonts->TexData->Height;
            return Textures;
        };
        bool RecordAnimation = false;
        uint32_t AnimationOrdinal = 0;
        uint32_t Checks = 0;
        const auto Confirm = [&](bool Condition, const char *Caption) { AssertRasterCondition(Condition, Caption); ++Checks; };
        const auto Frame = [&](std::optional<uint64_t> CompletedOverride = std::nullopt)
        {
            ImGui::NewFrame();
            Workspace.ConstructInterface();
            ImGui::Render();
            const bool Changed = Requested && (!Displayed || Requested->Instructions != Displayed->Instructions || Requested->Segments != Displayed->Segments || Requested->Root != Displayed->Root);
            if (Changed) Raster.ConfigureRetiredProgram(*Requested, Serial);
            Coverage = Device.Capture(Raster.QueryImage(), Raster.QueryExtent(), [&](VkCommandBuffer Commands)
            {
                if (Changed) Confirm(Raster.ConstructCommands(Commands, Serial, Serial + 1), "Changed native canvas failed to dispatch.");
                SelectionEvaluation.ConstructCommands(Commands, CompletedOverride.value_or(Serial), Serial + 1);
            }, [&]()
            {
                if (Changed) Raster.ConfirmSubmission();
                SelectionEvaluation.ConfirmSubmission(); ++Serial;
            });
            if (Changed) Displayed = Requested;
            const auto Textures = CaptureTextures();
            if (RecordAnimation)
            {
                char Caption[48]; std::snprintf(Caption, sizeof(Caption), "Motion%05u.ppm", AnimationOrdinal++);
                EncodeLibraryDrawCapture(Destination / Caption, *ImGui::GetDrawData(), Atlas, AtlasWidth, AtlasHeight, Textures);
            }
        };
        const auto Capture = [&](const char *Caption)
        {
            const auto Textures = CaptureTextures();
            EncodeLibraryDrawCapture(Destination / (std::string(Caption) + ".ppm"), *ImGui::GetDrawData(), Atlas, AtlasWidth, AtlasHeight, Textures);
        };
        if (Count == 6 && std::string_view(Arguments[5]) == "--serve")
        {
            VectorAuthoringDocument Initial;
            CubicPath Contour;
            Contour.Closed = true;
            Contour.Anchors = {{{160, 160}, {160, 160}, {200, 120}},
                {{330, 180}, {290, 140}, {360, 220}}, {{260, 350}, {260, 350}, {260, 350}}};
            ConstructCubicPath(Initial, Contour, {}, "Editable cubic contour");
            Workspace.ConfigureDocument(Initial);
            for (int Index = 0; Index < 8; ++Index) Frame();
            Capture("Current");
            std::cout << "READY" << std::endl;
            const std::array<ImGuiKey, 18> Keys{ImGuiKey_None, ImGuiKey_V, ImGuiKey_A, ImGuiKey_P,
                ImGuiKey_R, ImGuiKey_O, ImGuiKey_Enter, ImGuiKey_Escape, ImGuiKey_Backspace, ImGuiKey_Delete,
                ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_Z,
                ImGuiKey_Y, ImGuiKey_S, ImGuiKey_Space};
            float X = 0, Y = 0, Wheel = 0;
            unsigned Buttons = 0, Key = 0, Character = 0;
            bool Ctrl = false, Shift = false, Alt = false, Pressed = false, Focused = true;
            while (std::cin >> X >> Y >> Buttons >> Wheel >> Ctrl >> Shift >> Alt >> Key >> Pressed >> Focused >> Character)
            {
                if (!std::isfinite(X) || !std::isfinite(Y) || !std::isfinite(Wheel) || Key >= Keys.size()
                    || Buttons > 7 || std::abs(X) > 10000 || std::abs(Y) > 10000 || std::abs(Wheel) > 20
                    || Character > 0x10ffff || (Character >= 0xd800 && Character <= 0xdfff)) break;
                Input.AddFocusEvent(Focused);
                Input.AddMousePosEvent(X, Y);
                for (unsigned Button = 0; Button < 3; ++Button) Input.AddMouseButtonEvent(int(Button), (Buttons & (1u << Button)) != 0);
                Input.AddMouseWheelEvent(0, Wheel);
                Input.AddKeyEvent(ImGuiMod_Ctrl, Ctrl);
                Input.AddKeyEvent(ImGuiMod_Shift, Shift);
                Input.AddKeyEvent(ImGuiMod_Alt, Alt);
                if (Key) Input.AddKeyEvent(Keys[Key], Pressed);
                if (Character) Input.AddInputCharacter(Character);
                for (int Index = 0; Index < 3; ++Index) Frame();
                Capture("Current");
                std::cout << "CAPTURE " << Serial << ' ' << Workspace.QueryRevision() << ' '
                    << Workspace.QueryDocument().Layers.size() << std::endl;
            }
            ImGui::DestroyContext();
            return 0;
        }
        const auto ClickPoint = [&](ImVec2 Position)
        {
            Input.AddMousePosEvent(Position.x, Position.y); Frame();
            Input.AddMouseButtonEvent(0, true); Frame();
            Input.AddMouseButtonEvent(0, false); Frame(); Frame(); Frame();
        };
        const auto Click = [&](const char *Caption)
        {
            auto Extent = Workspace.QueryControlExtent(Caption);
            for (int Attempt = 0; Attempt < 16 && (Extent.MaximumY > 890 || Extent.MinimumY < 80); ++Attempt)
            {
                const auto Panel = Workspace.QueryPanelExtent("Inspector");
                Input.AddMousePosEvent((Panel.MinimumX + Panel.MaximumX) * .5f, 500);
                Input.AddMouseWheelEvent(0, Extent.MaximumY > 890 ? -5 : 5);
                Frame(); Frame(); Frame();
                Extent = Workspace.QueryControlExtent(Caption);
            }
            Confirm(Extent.Width() > 0 && Extent.Height() > 0, Caption);
            ClickPoint(ImVec2((Extent.MinimumX + Extent.MaximumX) * .5f, (Extent.MinimumY + Extent.MaximumY) * .5f));
        };
        for (int Index = 0; Index < 8; ++Index) Frame();
        uint32_t SurfaceControlChecks = 0;
        const auto ConfirmSurfaceControl = [&](bool Condition, const char *Caption)
        {
            AssertRasterCondition(Condition, Caption);
            ++SurfaceControlChecks;
        };
        {
            VectorAuthoringDocument ChannelDocument;
            VectorCoverageInstruction Shape;
            Shape.Bounds = {180, 230, 90, 90};
            ConstructVectorAuthoringPrimitive(ChannelDocument, Shape, "Dielectric ground");
            Shape.Bounds = {340, 280, 72, 72};
            ConstructVectorAuthoringPrimitive(ChannelDocument, Shape, "Metal accent");
            Workspace.ConfigureDocument(ChannelDocument);
            Frame(); Frame();
            Click("Select:1");
            Click("Surface channels · 1 selected");
            const auto Revision = Workspace.QueryRevision();
            Click("Roughness");
            const auto Edited = Workspace.QueryDocument();
            ConfirmSurfaceControl(Edited.Layers[0].SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Roughness)].Enabled == false,
                "Native roughness toggle did not update the selected shape.");
            ConfirmSurfaceControl(Edited.Layers[1].SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Roughness)].Enabled,
                "Native roughness toggle contaminated an unselected shape.");
            ConfirmSurfaceControl(Workspace.QueryRevision() == Revision + 1,
                "One channel toggle did not create exactly one undoable document revision.");
            const auto Roughness = CompileVectorSurface(Edited, VectorAuthoringSurfaceChannel::Roughness);
            ConfirmSurfaceControl(Roughness.Contributions.size() == 2
                && !Roughness.Contributions[0].Channels[size_t(VectorAuthoringSurfaceChannel::Roughness)].Enabled
                && Roughness.Contributions[1].Channels[size_t(VectorAuthoringSurfaceChannel::Roughness)].Enabled,
                "Per-shape roughness enablement did not survive GPU program compilation.");
            Click("Undo");
            ConfirmSurfaceControl(Workspace.QueryDocument() == ChannelDocument,
                "Channel edit Undo did not restore both shapes and their channel settings.");
            Click("Redo");
            ConfirmSurfaceControl(Workspace.QueryDocument() == Edited,
                "Channel edit Redo changed per-shape contribution settings.");
            Click("Roughness strength");
            Input.AddKeyEvent(ImGuiMod_Ctrl, true); Input.AddKeyEvent(ImGuiKey_A, true); Frame();
            Input.AddKeyEvent(ImGuiKey_A, false); Input.AddKeyEvent(ImGuiMod_Ctrl, false); Frame();
            for (const char Character : std::string("0.25")) { Input.AddInputCharacter(Character); Frame(); }
            Input.AddKeyEvent(ImGuiKey_Enter, true); Frame(); Input.AddKeyEvent(ImGuiKey_Enter, false); Frame();
            const auto StrengthEdited = Workspace.QueryDocument();
            ConfirmSurfaceControl(std::abs(StrengthEdited.Layers[0].SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Roughness)].Strength - 0.25f) < 0.001f,
                "Native roughness strength control did not edit the selected shape.");
            ConfirmSurfaceControl(StrengthEdited.Layers[1].SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Roughness)].Strength == 1.f,
                "Native roughness strength edit changed an unselected shape.");
            const auto StrengthProgram = CompileVectorSurface(StrengthEdited, VectorAuthoringSurfaceChannel::Roughness);
            ConfirmSurfaceControl(StrengthProgram.Contributions.size() == 2 &&
                StrengthProgram.Contributions[0].Channels[size_t(VectorAuthoringSurfaceChannel::Roughness)].Strength == 0.25f &&
                StrengthProgram.Contributions[1].Channels[size_t(VectorAuthoringSurfaceChannel::Roughness)].Strength == 1.f,
                "Selected-shape roughness strength did not survive native GPU program compilation.");
            Click("Surface relief");
            Input.AddKeyEvent(ImGuiMod_Ctrl, true); Input.AddKeyEvent(ImGuiKey_A, true); Frame();
            Input.AddKeyEvent(ImGuiKey_A, false); Input.AddKeyEvent(ImGuiMod_Ctrl, false); Frame();
            for (const char Character : std::string("0.35")) { Input.AddInputCharacter(Character); Frame(); }
            Input.AddKeyEvent(ImGuiKey_Enter, true); Frame(); Input.AddKeyEvent(ImGuiKey_Enter, false); Frame();
            const auto ReliefEdited = Workspace.QueryDocument();
            ConfirmSurfaceControl(std::abs(ReliefEdited.Layers[0].SurfaceRelief - 0.35f) < 0.001f &&
                ReliefEdited.Layers[1].SurfaceRelief == 1.f,
                "Native surface relief edit changed the wrong selected-shape contribution.");
            const auto ReliefProgram = CompileVectorSurface(ReliefEdited, VectorAuthoringSurfaceChannel::Height);
            ConfirmSurfaceControl(ReliefProgram.Contributions.size() == 2 &&
                std::abs(ReliefProgram.Contributions[0].SurfaceRelief - 0.35f) < 0.001f &&
                ReliefProgram.Contributions[1].SurfaceRelief == 1.f,
                "Per-shape surface relief did not survive height program compilation.");
            Workspace.ConfigureDocument({});
            Frame(); Frame();
        }
        uint32_t PickingChecks = 0;
        const auto ConfirmPicking = [&](bool Condition, const char *Caption) { AssertRasterCondition(Condition, Caption); ++PickingChecks; };
        const auto EvaluatePoint = [&](const VectorAuthoringDocument &Document, std::array<float, 2> Point, uint32_t Expected)
        {
            const auto Program = ConstructVectorSelectionProgram(Document);
            auto Token = SelectionEvaluation.QueryToken(Program, Point);
            for (int Attempt = 0; !Token && Attempt < 6; ++Attempt)
            {
                Frame(); Token = SelectionEvaluation.QueryToken(Program, Point);
            }
            ConfirmPicking(Token && *Token == Expected, "GPU contour selection disagrees with expected visible source.");
            const auto Dispatches = SelectionEvaluation.QueryDispatchCount();
            ConfirmPicking(SelectionEvaluation.QueryToken(Program, Point) == Token, "Cached GPU selection lost its authenticated token.");
            Frame();
            ConfirmPicking(SelectionEvaluation.QueryDispatchCount() == Dispatches, "Identical point selection redispatched the shader.");
        };
        {
            VectorAuthoringDocument Document;
            VectorCoverageInstruction Shape;
            Shape.Bounds = {256, 256, 160, 160};
            const auto Lower = ConstructVectorAuthoringPrimitive(Document, Shape, "Lower rectangle");
            Shape.Topology[0] = 1;
            const auto Upper = ConstructVectorAuthoringPrimitive(Document, Shape, "Upper ellipse");
            EvaluatePoint(Document, {256, 256}, Upper);
            EvaluatePoint(Document, {330, 330}, Lower);
            EvaluatePoint(Document, {350, 350}, 0);
            // Fractional coordinates are evaluated directly, not rounded to a texture pixel centre.
            EvaluatePoint(Document, {256.125f, 256.875f}, Upper);
            Document.Layers.back().Locked = true; EvaluatePoint(Document, {256, 256}, Lower);
            Document.Layers.back().Locked = false; Document.Layers.back().Visible = false;
            EvaluatePoint(Document, {256, 256}, Lower);
            Document.Layers.back().Visible = true;
            const auto Group = ++Document.TokenClock;
            VectorAuthoringEnclosure Enclosure; Enclosure.Token = Group; Enclosure.Name = "Hidden enclosure"; Enclosure.FolderEnabled = true;
            Document.Enclosures.push_back(Enclosure);
            Document.Layers.back().EnclosureToken = Group;
            Document.Enclosures.back().VisibilityEnabled = false;
            EvaluatePoint(Document, {256, 256}, Lower);
            Document.Enclosures.back().VisibilityEnabled = true; Document.Enclosures.back().LockEnabled = true;
            EvaluatePoint(Document, {256, 256}, Lower);
            for (uint32_t Category = 0; Category < 6; ++Category)
            {
                VectorAuthoringDocument Primitive;
                Shape.Topology[0] = Category;
                const auto Source = ConstructVectorAuthoringPrimitive(Primitive, Shape, "Parametric motif");
                EvaluatePoint(Primitive, {256, 256}, Source);
                EvaluatePoint(Primitive, {350, 350}, 0);
            }
            VectorAuthoringDocument RotatedSource;
            Shape.Topology[0] = 0; Shape.Bounds = {256, 256, 160, 40}; Shape.Transform[0] = .785398163f;
            const auto RotatedToken = ConstructVectorAuthoringPrimitive(RotatedSource, Shape, "Rotated motif");
            EvaluatePoint(RotatedSource, {300, 300}, RotatedToken);
            EvaluatePoint(RotatedSource, {300, 212}, 0);
            VectorAuthoringDocument Stroke;
            Shape.Bounds = {256, 256, 160, 160}; Shape.Topology[0] = 1; Shape.Transform[0] = 0;
            Shape.Paint[3] = 0; Shape.Transform[1] = 8;
            const auto StrokeToken = ConstructVectorAuthoringPrimitive(Stroke, Shape, "Stroke-only ellipse");
            EvaluatePoint(Stroke, {256, 256}, 0);
            EvaluatePoint(Stroke, {337, 256}, StrokeToken);
            EvaluatePoint(Stroke, {344, 256}, 0);
            Stroke.Instructions[0].Transform[2] = 0; EvaluatePoint(Stroke, {337, 256}, 0);
            Shape.Paint[3] = 1; Shape.Transform[1] = 0;
            VectorAuthoringDocument Boolean;
            Shape.Topology[0] = 0; Shape.Bounds = {256, 256, 180, 180};
            const auto Underlay = ConstructVectorAuthoringPrimitive(Boolean, Shape, "Underlay");
            const auto Owner = ConstructVectorAuthoringPrimitive(Boolean, Shape, "Boolean ring");
            Shape.Topology[0] = 1; Shape.Bounds = {256, 256, 80, 80};
            Boolean.Instructions.push_back(Shape);
            VectorCoverageInstruction Operation; Operation.Topology = {17, 1, 2, 0};
            Boolean.Instructions.push_back(Operation); Boolean.Layers.back().Root = 3;
            EvaluatePoint(Boolean, {256, 256}, Underlay);
            EvaluatePoint(Boolean, {320, 256}, Owner);
            Boolean.Instructions[3].Topology[0] = 16; EvaluatePoint(Boolean, {256, 256}, Owner);
            Boolean.Instructions[3].Topology[0] = 18;
            EvaluatePoint(Boolean, {256, 256}, Owner); EvaluatePoint(Boolean, {320, 256}, Underlay);
            Boolean.Instructions[3].Topology[0] = 19;
            EvaluatePoint(Boolean, {256, 256}, Underlay); EvaluatePoint(Boolean, {320, 256}, Owner);
            VectorAuthoringDocument Masked;
            Shape.Topology[0] = 0; Shape.Bounds = {256, 256, 160, 160};
            const auto MaskOwner = ConstructVectorAuthoringPrimitive(Masked, Shape, "Masked motif");
            Shape.Topology[0] = 1; Shape.Bounds[2] = Shape.Bounds[3] = 80;
            Masked.Layers[0].Mask = Shape;
            EvaluatePoint(Masked, {256, 256}, MaskOwner); EvaluatePoint(Masked, {320, 256}, 0);
            Masked.Layers[0].MaskInverted = true;
            EvaluatePoint(Masked, {256, 256}, 0); EvaluatePoint(Masked, {320, 256}, MaskOwner);
            Masked.Layers[0].MaskEnabled = false; EvaluatePoint(Masked, {256, 256}, MaskOwner);
            Masked.Layers[0].MaskEnabled = true; Masked.Layers[0].MaskInverted = false;
            Masked.Layers[0].MaskLuminance = true; Masked.Layers[0].Mask->Colour = {0, 0, 0, 1};
            EvaluatePoint(Masked, {256, 256}, 0);
            Masked.Layers[0].Mask->Colour = {1, 1, 1, 1}; EvaluatePoint(Masked, {256, 256}, MaskOwner);
            VectorAuthoringDocument Cubic;
            VectorCoverageInstruction Contour;
            Contour.Bounds = {256, 256, 160, 160}; Contour.Topology = {6, 0, 8, 1};
            for (float Radius : {.5f, .2f})
            {
                const std::array<std::array<float, 2>, 4> Corners{{{-Radius, -Radius}, {Radius, -Radius}, {Radius, Radius}, {-Radius, Radius}}};
                for (size_t Index = 0; Index < 4; ++Index)
                {
                    const auto Start = Corners[Index], Finish = Corners[(Index + 1) % 4];
                    Cubic.Segments.push_back({{Start[0], Start[1], Start[0] + (Finish[0] - Start[0]) / 3, Start[1] + (Finish[1] - Start[1]) / 3},
                        {Start[0] + 2 * (Finish[0] - Start[0]) / 3, Start[1] + 2 * (Finish[1] - Start[1]) / 3, Finish[0], Finish[1]}});
                }
            }
            Cubic.Instructions = {Contour}; Cubic.TokenClock = 1;
            VectorAuthoringLayer CubicLayer; CubicLayer.Token = 1; CubicLayer.Name = "Cubic ring"; Cubic.Layers.push_back(CubicLayer);
            EvaluatePoint(Cubic, {256, 256}, 0); EvaluatePoint(Cubic, {320, 256}, 1);
            Cubic.Instructions[0].Topology[3] = 0; EvaluatePoint(Cubic, {256, 256}, 1);
            // All 32 token bits survive normalized RGBA8 transport, including its high alpha byte.
            Masked.Layers[0].Token = 0xfedcba98; Masked.TokenClock = 0xfedcba98;
            EvaluatePoint(Masked, {256, 256}, 0xfedcba98);
            VectorAuthoringDocument Empty;
            EvaluatePoint(Empty, {256, 256}, 0);
            VectorAuthoringDocument Capacity;
            Shape = {}; Shape.Bounds = {256, 256, 120, 120};
            ConstructVectorAuthoringPrimitive(Capacity, Shape, "Layer 1");
            for (uint32_t Token = 2; Token <= 64; ++Token)
            {
                auto Layer = Capacity.Layers.front(); Layer.Token = Token; Layer.Name = "Layer " + std::to_string(Token);
                Capacity.Layers.push_back(Layer);
            }
            Capacity.TokenClock = 64;
            EvaluatePoint(Capacity, {256, 256}, 64);
            Capacity.Instructions[0].Bounds = {-20, 256, 120, 120};
            EvaluatePoint(Capacity, {-20, 256}, 0);
            auto Invalid = ConstructVectorSelectionProgram(Capacity);
            Invalid.Candidates.back()[1] = Invalid.Candidates.front()[1];
            bool InvalidRefused = false;
            try { (void)SelectionEvaluation.QueryToken(Invalid, {256, 256}); } catch (const std::exception &) { InvalidRefused = true; }
            ConfirmPicking(InvalidRefused, "Duplicate GPU selection source tokens were accepted.");
            const auto Program = ConstructVectorSelectionProgram(Masked);
            const std::array<float, 2> Point{256.25f, 256.5f};
            ConfirmPicking(!SelectionEvaluation.QueryToken(Program, Point), "New GPU point returned a stale result.");
            Frame(); Frame(0);
            ConfirmPicking(!SelectionEvaluation.QueryToken(Program, Point), "Unretired GPU token was read on CPU.");
            Frame();
            ConfirmPicking(SelectionEvaluation.QueryToken(Program, Point) == std::optional<uint32_t>(0xfedcba98), "Retired GPU token was not made available.");
            auto Replacement = Program; Replacement.Coverage.Instructions[0].Transform[2] = 0;
            ConfirmPicking(!SelectionEvaluation.QueryToken(Program, {256, 256}), "Different GPU pointer reused an old cached request.");
            Frame();
            ConfirmPicking(!SelectionEvaluation.QueryToken(Replacement, {256, 256}), "Source mutation reused an in-flight token.");
            Frame();
            ConfirmPicking(!SelectionEvaluation.QueryToken(Replacement, {256, 256}), "Stale request completion was accepted for changed source.");
            Frame();
            ConfirmPicking(SelectionEvaluation.QueryToken(Replacement, {256, 256}) == std::optional<uint32_t>(0), "Changed source did not receive its own evaluated miss.");
            bool Refused = false;
            try { (void)SelectionEvaluation.QueryToken(Program, {std::numeric_limits<float>::infinity(), 0}); } catch (const std::exception &) { Refused = true; }
            ConfirmPicking(Refused, "Nonfinite GPU selection pointer was accepted.");
        }
        Confirm(Workspace.QueryDocument().Layers.empty(), "Idle native Pattern UI created artwork.");
        Click("Add pattern layer");
        Frame();
        Click("Rectangle");
        Confirm(Workspace.QueryDocument().Layers.size() == 1 && Workspace.QuerySelection().size() == 1, "Native layer menu did not add and select a rectangle.");
        for (int Index = 0; Index < 4; ++Index) Frame();
        Input.AddMousePosEvent(700, 25); Frame();
        Capture("NativePatternWorkspace");
        const auto Board = Workspace.QueryBoardExtent();
        Confirm(std::abs(Board.Width() - 360) < 3, "Native board width differs from the approved HTML reference.");
        // 📝 Resolution independence: the GPU bake must track the on-screen zoomed board, not stay
        // fixed while the displayed quad grows — otherwise zooming in stretches a fixed-size raster
        // and visibly pixelates instead of re-baking the vector content sharper.
        const uint32_t BaselineRequestedExtent = RequestedExtent;
        for (int Index = 0; Index < 5; ++Index) { Click("Zoom in"); Frame(); }
        const auto ZoomedBoard = Workspace.QueryBoardExtent();
        Confirm(ZoomedBoard.Width() > Board.Width() * 2.5f, "Repeated zoom-in did not enlarge the displayed board as expected.");
        Confirm(RequestedExtent > BaselineRequestedExtent + 200,
            "GPU coverage bake did not request a sharper resolution after the canvas zoomed in.");
        Click("Fit"); Frame();
        Confirm(std::abs(Workspace.QueryBoardExtent().Width() - Board.Width()) < 3 && RequestedExtent < ZoomedBoard.Width(),
            "GPU coverage bake stayed at the zoomed-in resolution after Fit returned the canvas to 100%.");
        const auto Viewport = Workspace.QueryPanelExtent("Viewport");
        const auto Inspector = Workspace.QueryPanelExtent("Inspector");
        const auto Layers = Workspace.QueryPanelExtent("Layers");
        Confirm(std::abs(Viewport.MinimumX - 280) < 5 && std::abs(Inspector.MinimumX - 1102) < 5, "Native three-column boundaries differ from the measured HTML.");
        Confirm(std::abs(Layers.MinimumY - 77) < 5, "Native library/layers split differs from the measured HTML.");
        const auto Original = Workspace.QueryDocument();
        const ImVec2 Centre((Board.MinimumX + Board.MaximumX) * .5f, (Board.MinimumY + Board.MaximumY) * .5f);
        Input.AddMousePosEvent(Centre.x, Centre.y); Frame();
        Input.AddMouseButtonEvent(0, true); Frame();
        for (int Index = 1; Index <= 5; ++Index) { Input.AddMousePosEvent(Centre.x + Index * 6, Centre.y + Index * 2); Frame(); }
        Input.AddMouseButtonEvent(0, false); Frame();
        Confirm(Workspace.QueryDocument() != Original, "Native pointer drag did not edit source geometry.");
        Click("Undo");
        Confirm(Workspace.QueryDocument() == Original, "One Undo did not restore the whole native drag gesture.");
        Click("Redo");
        Confirm(Workspace.QueryDocument() != Original, "Native drag redo did not restore geometry.");
        Click("Save document");
        Confirm(!Workspace.QueryModified() && DecodeVectorAuthoring(SavedSource) == Workspace.QueryDocument(), "Native save callback lost editable TOML source.");
        const auto Token = Workspace.QuerySelection().front();
        const auto Saved = Workspace.QueryDocument();
        Click(("Lock:" + std::to_string(Token)).c_str());
        const auto Locked = Workspace.QueryDocument();
        Input.AddKeyEvent(ImGuiKey_Delete, true); Frame(); Input.AddKeyEvent(ImGuiKey_Delete, false); Frame();
        Confirm(Workspace.QueryDocument() == Locked, "Native keyboard delete changed protected artwork.");
        Click(("Lock:" + std::to_string(Token)).c_str());
        Confirm(Workspace.QueryDocument() == Saved, "Unlock changed unrelated native source properties.");
        Click("Duplicate selection");
        Confirm(Workspace.QueryDocument().Layers.size() == 2, "Native duplicate action lost a source.");
        Click("Undo");
        Confirm(Workspace.QueryDocument().Layers.size() == 1, "Native duplicate Undo failed.");
        Click("Modifiers");
        for (int Index = 0; Index < 5; ++Index) Frame();
        Click("Add array modifier");
        Confirm(Workspace.QueryDocument().Layers.size() == 1, "live array modifier polluted the authored layer list with baked copies.");
        Confirm(!Workspace.QueryDocument().Layers.front().Modifiers.empty(), "Add array modifier did not attach a live stack entry.");
        Confirm(Frontier::ConstructEvaluatedVectorAuthoringDocument(Workspace.QueryDocument()).Layers.size() == 4,
            "Evaluated live array modifier did not expand to four procedural copies for rendering.");
        for (int Index = 0; Index < 5; ++Index) Frame();
        Capture("NativePatternArrayInspector");
        Click("Undo");
        Click("Add mask");
        Confirm(Workspace.QueryDocument().Layers.front().Mask.has_value(), "Native owned-mask action did not retain a descriptor.");
        for (int Index = 0; Index < 4; ++Index) Frame();
        Click("Edit mask");
        for (int Index = 0; Index < 4; ++Index) Frame();
        Capture("NativePatternOwnedMask");
        Click("Done editing mask");
        Click("Undo");
        Confirm(!Workspace.QueryDocument().Layers.front().Mask, "Native mask Undo failed.");
        const auto BeforePan = Workspace.QueryDocument();
        Input.AddMousePosEvent(Centre.x, Centre.y); Frame();
        Input.AddMouseButtonEvent(2, true); Frame();
        Input.AddMousePosEvent(Centre.x + 20, Centre.y + 10); Frame();
        Input.AddMouseButtonEvent(2, false); Frame();
        Confirm(Workspace.QueryDocument() == BeforePan, "Native view pan mutated the document.");
        Click("Fit");
        const auto Dispatches = Raster.QueryDispatchCount();
        for (int Index = 0; Index < 4; ++Index) Frame();
        Confirm(Raster.QueryDispatchCount() == Dispatches, "Idle native Pattern UI recomputed GPU coverage.");
        uint32_t GestureChecks = 0;
        const auto ConfirmGesture = [&](bool Condition, const char *Caption)
        {
            AssertRasterCondition(Condition, Caption); ++GestureChecks;
        };
        const auto Near = [](float First, float Second) { return std::abs(First - Second) < .02f; };
        const auto PointOnBoard = [&](float X, float Y)
        {
            const auto Extent = Workspace.QueryBoardExtent();
            return ImVec2(Extent.MinimumX + X / 512 * Extent.Width(), Extent.MinimumY + Y / 512 * Extent.Height());
        };
        const auto BeginDrag = [&](ImVec2 Start)
        {
            Input.AddMousePosEvent(Start.x, Start.y); Frame();
            Input.AddMouseButtonEvent(0, true); Frame();
        };
        const auto MoveDrag = [&](ImVec2 Finish)
        {
            Input.AddMousePosEvent(Finish.x, Finish.y); Frame(); Frame();
        };
        const auto EndDrag = [&]() { Input.AddMouseButtonEvent(0, false); Frame(); Frame(); };
        const auto Grip = [&](const char *Name)
        {
            const auto Extent = Workspace.QueryControlExtent(Name);
            ConfirmGesture(Extent.Width() > 0, "Native transform grip is unavailable.");
            return ImVec2((Extent.MinimumX + Extent.MaximumX) * .5f, (Extent.MinimumY + Extent.MaximumY) * .5f);
        };
        if (Count == 6 && std::string_view(Arguments[5]) == "--animate-join")
        {
            Workspace.ConfigureDocument({}); Frame(); Frame(); Click("Fit");
            Input.MouseDrawCursor = true; RecordAnimation = true;
            Click("Pen tool (P)");
            const auto TranslatePointer = [&](ImVec2 Start, ImVec2 Finish)
            {
                for (int Index = 1; Index <= 6; ++Index)
                {
                    const float Parameter = float(Index) / 6;
                    Input.AddMousePosEvent(Start.x + (Finish.x - Start.x) * Parameter, Start.y + (Finish.y - Start.y) * Parameter);
                    Frame();
                }
            };
            const auto AuthorAnchor = [&](float X, float Y, float TangentX, float TangentY)
            {
                const auto Start = PointOnBoard(X, Y), Finish = PointOnBoard(TangentX, TangentY);
                TranslatePointer(Input.MousePos, Start); BeginDrag(Start); TranslatePointer(Start, Finish); EndDrag();
            };
            const auto FinishOpen = [&]()
            {
                Input.AddKeyEvent(ImGuiKey_Enter, true); Frame(); Input.AddKeyEvent(ImGuiKey_Enter, false); Frame();
            };
            AuthorAnchor(100, 180, 150, 130); AuthorAnchor(220, 180, 220, 180); FinishOpen();
            AuthorAnchor(300, 300, 330, 270); AuthorAnchor(420, 300, 420, 300); FinishOpen();
            ConfirmGesture(Workspace.QueryDocument().Layers.size() == 2, "Animated joining did not author two native open paths.");
            Click("Select:1"); Input.AddKeyEvent(ImGuiMod_Shift, true); Frame(); Click("Select:2");
            Input.AddKeyEvent(ImGuiMod_Shift, false); Frame();
            const auto BeforeJoin = Workspace.QueryDocument();
            const auto Revision = Workspace.QueryRevision();
            Click("Join end to start");
            const auto Joined = Workspace.QueryDocument();
            ConfirmGesture(Joined.Layers.size() == 1 && Joined.Layers[0].Token == 1 && Workspace.QueryRevision() == Revision + 1,
                "Animated join did not retain one source identity in one history edit.");
            for (int Index = 0; Index < 5; ++Index) Frame();
            Click("Undo"); ConfirmGesture(Workspace.QueryDocument() == BeforeJoin, "Animated join Undo failed.");
            Click("Redo"); ConfirmGesture(Workspace.QueryDocument() == Joined, "Animated join Redo failed.");
            for (int Index = 0; Index < 6; ++Index) Frame();
            RecordAnimation = false; Capture("NativePatternJoining");
            std::ofstream Metadata(Destination / "MotionFrames.json");
            Metadata << "{\"frames\":" << AnimationOrdinal << ",\"width\":1440,\"height\":900,\"native_mouse_cursor\":true,\"native_pointer_input\":true,\"windows_executed\":false}\n";
            std::cout << AnimationOrdinal << " real native join-animation frames captured.\n";
            ImGui::DestroyContext();
            return 0;
        }
        if (Count == 6 && std::string_view(Arguments[5]) == "--animate")
        {
            Workspace.ConfigureDocument({}); Frame(); Frame(); Click("Fit");
            Input.MouseDrawCursor = true;
            RecordAnimation = true;
            Click("Pen tool (P)");
            const auto TranslatePointer = [&](ImVec2 Start, ImVec2 Finish)
            {
                for (int Index = 1; Index <= 6; ++Index)
                {
                    const float Parameter = float(Index) / 6;
                    Input.AddMousePosEvent(Start.x + (Finish.x - Start.x) * Parameter, Start.y + (Finish.y - Start.y) * Parameter);
                    Frame();
                }
            };
            const auto AuthorAnchor = [&](float X, float Y, float TangentX, float TangentY)
            {
                const auto Start = PointOnBoard(X, Y), Finish = PointOnBoard(TangentX, TangentY);
                TranslatePointer(Input.MousePos, Start);
                BeginDrag(Start);
                TranslatePointer(Start, Finish);
                EndDrag();
            };
            AuthorAnchor(160, 160, 200, 120);
            AuthorAnchor(330, 180, 370, 220);
            AuthorAnchor(260, 350, 260, 350);
            ClickPoint(Grip("Pen anchor 0"));
            for (int Index = 0; Index < 4; ++Index) Frame();
            Click("Point tool (A)"); ClickPoint(Grip("Path anchor 1"));
            const auto Start = Grip("Path anchor 1");
            BeginDrag(Start); TranslatePointer(Start, PointOnBoard(355, 210)); EndDrag();
            const auto BeforeSubdivision = Workspace.QueryDocument();
            Click("Subdivide following segment");
            const auto Subdivided = Workspace.QueryDocument();
            ConfirmGesture(DecodeCubicPath(Subdivided, Subdivided.Layers[0].Token).Anchors.size() == 4, "Animated native subdivision did not insert an anchor.");
            for (int Index = 0; Index < 4; ++Index) Frame();
            Click("Undo"); ConfirmGesture(Workspace.QueryDocument() == BeforeSubdivision, "Animated subdivision undo failed.");
            Click("Redo"); ConfirmGesture(Workspace.QueryDocument() == Subdivided, "Animated subdivision redo failed.");
            for (int Index = 0; Index < 5; ++Index) Frame();
            Click("Pen tool (P)");
            AuthorAnchor(90, 350, 120, 330);
            AuthorAnchor(200, 420, 200, 420);
            Input.AddKeyEvent(ImGuiKey_Enter, true); Frame(); Input.AddKeyEvent(ImGuiKey_Enter, false); Frame();
            Click("Point tool (A)"); Click("Resume from end");
            AuthorAnchor(370, 420, 400, 390);
            Input.AddKeyEvent(ImGuiKey_Enter, true); Frame(); Input.AddKeyEvent(ImGuiKey_Enter, false); Frame();
            const auto Continued = Workspace.QueryDocument();
            ConfirmGesture(DecodeCubicPath(Continued, Continued.Layers.back().Token).Anchors.size() == 3, "Animated continuation did not extend the retained layer.");
            Click("Undo"); Click("Redo");
            ConfirmGesture(Workspace.QueryDocument() == Continued, "Animated continuation history failed.");
            Click("Point tool (A)"); Click("Resume from start");
            AuthorAnchor(90, 260, 100, 235);
            Input.AddKeyEvent(ImGuiKey_Enter, true); Frame(); Input.AddKeyEvent(ImGuiKey_Enter, false); Frame();
            ConfirmGesture(DecodeCubicPath(Workspace.QueryDocument(), Continued.Layers.back().Token).Anchors.size() == 4, "Animated start continuation did not prepend an anchor.");
            for (int Index = 0; Index < 5; ++Index) Frame();
            RecordAnimation = false;
            Capture("NativePatternRefinement");
            std::ofstream Metadata(Destination / "MotionFrames.json");
            Metadata << "{\"frames\":" << AnimationOrdinal << ",\"width\":1440,\"height\":900,\"native_mouse_cursor\":true,\"native_pointer_input\":true,\"windows_executed\":false}\n";
            std::cout << AnimationOrdinal << " real native animation frames captured.\n";
            ImGui::DestroyContext();
            return 0;
        }
        VectorAuthoringDocument Fixture;
        Fixture.Name = "Selection gestures";
        VectorCoverageInstruction Rectangle;
        Rectangle.Bounds = {180, 256, 80, 60};
        Rectangle.Colour = {.85f, .3f, .2f, 1};
        const auto First = ConstructVectorAuthoringPrimitive(Fixture, Rectangle, "First motif");
        Rectangle.Bounds[0] = 330;
        Rectangle.Colour = {.3f, .7f, .4f, 1};
        const auto Second = ConstructVectorAuthoringPrimitive(Fixture, Rectangle, "Second motif");
        Rectangle.Bounds = {420, 170, 40, 40};
        ConstructVectorAuthoringPrimitive(Fixture, Rectangle, "Locked motif");
        Fixture.Layers.back().Locked = true;
        Rectangle.Bounds = {80, 170, 40, 40};
        ConstructVectorAuthoringPrimitive(Fixture, Rectangle, "Hidden motif");
        Fixture.Layers.back().Visible = false;
        Workspace.ConfigureDocument(Fixture);
        Click("Fit"); Frame(); Frame();
        const auto RevisionBeforeMarquee = Workspace.QueryRevision();
        BeginDrag(PointOnBoard(90, 190)); MoveDrag(PointOnBoard(390, 310));
        Capture("NativePatternMarquee");
        EndDrag();
        ConfirmGesture(Workspace.QuerySelection() == std::vector<uint32_t>({First, Second}), "Marquee did not intersect both editable motifs.");
        ConfirmGesture(Workspace.QueryDocument() == Fixture && Workspace.QueryRevision() == RevisionBeforeMarquee, "Marquee altered source or history.");
        for (const auto *Name : {"Resize:nw", "Resize:n", "Resize:ne", "Resize:w", "Resize:e", "Resize:sw", "Resize:s", "Resize:se", "Rotate selection"}) (void)Grip(Name);
        const auto OriginalBounds = QueryVectorAuthoringSelectionBounds(Fixture, Workspace.QuerySelection());
        const auto Resize = Grip("Resize:se");
        BeginDrag(Resize); MoveDrag(ImVec2(Resize.x + 35, Resize.y + 14)); EndDrag();
        const auto Resized = Workspace.QueryDocument();
        const auto ResizedBounds = QueryVectorAuthoringSelectionBounds(Resized, Workspace.QuerySelection());
        ConfirmGesture(ResizedBounds[2] > OriginalBounds[2] && ResizedBounds[3] > OriginalBounds[3], "Native corner grip failed to enlarge selection.");
        ConfirmGesture(Near(ResizedBounds[0] - ResizedBounds[2] * .5f, OriginalBounds[0] - OriginalBounds[2] * .5f) && Near(ResizedBounds[1] - ResizedBounds[3] * .5f, OriginalBounds[1] - OriginalBounds[3] * .5f), "Opposite resize corner moved.");
        ConfirmGesture(Resized.Instructions[Resized.Layers[2].Root] == Fixture.Instructions[Fixture.Layers[2].Root], "Resize changed an unselected locked source.");
        ConfirmGesture(Workspace.QueryRevision() == RevisionBeforeMarquee + 1, "Resize did not finalize exactly once.");
        const size_t EnlargedPixel = size_t(260 * 512 + 230) * 4;
        ConfirmGesture(Coverage[EnlargedPixel + 3] > 240 && Coverage[EnlargedPixel] > Coverage[EnlargedPixel + 1], "GPU resize pixels did not extend the red motif beyond its original bounds.");
        Capture("NativePatternResize");
        Click("Undo"); ConfirmGesture(Workspace.QueryDocument() == Fixture, "Resize Undo did not restore exact source.");
        Click("Redo"); ConfirmGesture(Workspace.QueryDocument() == Resized, "Resize Redo lost source geometry.");
        Click("Undo");
        const auto Rotate = Grip("Rotate selection");
        const auto Pivot = PointOnBoard(OriginalBounds[0], OriginalBounds[1]);
        BeginDrag(Rotate); MoveDrag(ImVec2(Pivot.x + (Pivot.y - Rotate.y), Pivot.y)); EndDrag();
        const auto Rotated = Workspace.QueryDocument();
        ConfirmGesture(Near(Rotated.Instructions[Rotated.Layers[0].Root].Transform[0], 3.14159265f * .5f), "Native rotation grip did not rotate by 90 degrees.");
        ConfirmGesture(Near(Rotated.Instructions[Rotated.Layers[0].Root].Bounds[0], OriginalBounds[0]), "Group rotation did not orbit the shared pivot.");
        const size_t RotatedPixel = size_t(181 * 512 + 255) * 4;
        ConfirmGesture(Coverage[RotatedPixel + 3] > 240 && Coverage[RotatedPixel] > Coverage[RotatedPixel + 1], "GPU rotation pixels did not reach the rotated motif centre.");
        ConfirmGesture(Coverage[size_t(256 * 512 + 180) * 4 + 3] == 0, "GPU rotation retained coverage at the old motif location.");
        Capture("NativePatternRotation");
        Click("Undo"); ConfirmGesture(Workspace.QueryDocument() == Fixture, "Rotation Undo did not restore source.");
        const auto CancelGrip = Grip("Resize:se");
        BeginDrag(CancelGrip); MoveDrag(ImVec2(CancelGrip.x + 50, CancelGrip.y + 30));
        Input.AddKeyEvent(ImGuiKey_Escape, true); Input.AddMouseButtonEvent(0, false); Frame(); Frame();
        Input.AddKeyEvent(ImGuiKey_Escape, false); Frame();
        ConfirmGesture(Workspace.QueryDocument() == Fixture && Workspace.QuerySelection().size() == 2, "Escape/button-up finalized a cancelled resize.");
        const auto FocusGrip = Grip("Resize:e");
        BeginDrag(FocusGrip); MoveDrag(ImVec2(FocusGrip.x + 20, FocusGrip.y));
        Input.AddFocusEvent(false); Frame(); Frame();
        ConfirmGesture(Workspace.QueryDocument() == Fixture, "Focus loss finalized a live resize.");
        Input.AddMouseButtonEvent(0, false); Input.AddFocusEvent(true); Frame(); Frame();
        // Reverse-direction marquee must exclude both hidden and locked layers.
        BeginDrag(PointOnBoard(470, 340)); MoveDrag(PointOnBoard(40, 120)); EndDrag();
        ConfirmGesture(Workspace.QuerySelection() == std::vector<uint32_t>({First, Second}), "Reverse marquee selected hidden/protected motifs.");
        ClickPoint(PointOnBoard(450, 330));
        ClickPoint(PointOnBoard(180, 256));
        ConfirmGesture(Workspace.QuerySelection() == std::vector<uint32_t>({First}), "Single canvas selection failed after marquee.");
        Input.AddKeyEvent(ImGuiMod_Shift, true); Frame();
        BeginDrag(PointOnBoard(280, 210)); MoveDrag(PointOnBoard(375, 300)); EndDrag();
        Input.AddKeyEvent(ImGuiMod_Shift, false); Frame();
        ConfirmGesture(Workspace.QuerySelection() == std::vector<uint32_t>({First, Second}), "Shift-marquee did not preserve the initial selection.");
        BeginDrag(PointOnBoard(470, 340)); MoveDrag(PointOnBoard(40, 120));
        Input.AddKeyEvent(ImGuiKey_Escape, true); Frame();
        Input.AddKeyEvent(ImGuiKey_Escape, false); EndDrag();
        ConfirmGesture(Workspace.QuerySelection() == std::vector<uint32_t>({First, Second}) && Workspace.QueryDocument() == Fixture, "Cancelled marquee lost initial selection or changed source.");
        // A single rotated primitive uses its own axes, not its world-axis bounding rectangle.
        VectorAuthoringDocument Oriented;
        Oriented.Name = "Rotated motif";
        Rectangle.Bounds = {256, 256, 120, 80}; Rectangle.Transform[0] = 3.14159265f * .25f;
        const auto OrientedToken = ConstructVectorAuthoringPrimitive(Oriented, Rectangle, "Oriented motif");
        Workspace.ConfigureDocument(Oriented); Frame(); Frame();
        ClickPoint(PointOnBoard(256, 256));
        ConfirmGesture(Workspace.QuerySelection() == std::vector<uint32_t>({OrientedToken}), "Oriented source selection failed.");
        const auto East = Grip("Resize:e");
        BeginDrag(East); MoveDrag(ImVec2(East.x + 21, East.y + 21)); EndDrag();
        const auto Extended = Workspace.QueryDocument();
        const auto &ExtendedSource = Extended.Instructions[Extended.Layers.front().Root];
        ConfirmGesture(ExtendedSource.Bounds[2] > 150 && Near(ExtendedSource.Bounds[3], 80), "Rotated local-axis edge resize sheared or failed.");
        ConfirmGesture(Near(ExtendedSource.Transform[0], Rectangle.Transform[0]), "Local resize changed source orientation.");
        Click("Undo");
        Input.AddKeyEvent(ImGuiMod_Alt, true); Input.AddKeyEvent(ImGuiMod_Shift, true); Frame();
        const auto CentredGrip = Grip("Resize:se");
        BeginDrag(CentredGrip); MoveDrag(ImVec2(CentredGrip.x, CentredGrip.y + 30)); EndDrag();
        Input.AddKeyEvent(ImGuiMod_Alt, false); Input.AddKeyEvent(ImGuiMod_Shift, false); Frame();
        const auto &CentredDocument = Workspace.QueryDocument();
        const auto &CentredSource = CentredDocument.Instructions[CentredDocument.Layers[0].Root];
        ConfirmGesture(Near(CentredSource.Bounds[0], 256) && Near(CentredSource.Bounds[1], 256), "Alt-resize moved the selection centre.");
        ConfirmGesture(Near(CentredSource.Bounds[2] / CentredSource.Bounds[3], 1.5f), "Shift-resize changed the original aspect ratio.");
        Click("Undo");
        const auto OutsideGrip = Grip("Resize:e");
        BeginDrag(OutsideGrip); MoveDrag(ImVec2(1090, OutsideGrip.y + 80)); EndDrag();
        ConfirmGesture(Workspace.QueryDocument() != Oriented, "Resize stopped when pointer left the board.");
        Click("Undo"); ConfirmGesture(Workspace.QueryDocument() == Oriented, "Outside-board resize Undo failed.");
        Click("Zoom in"); Frame();
        const auto ZoomGrip = Grip("Resize:e");
        BeginDrag(ZoomGrip); MoveDrag(ImVec2(ZoomGrip.x + 20, ZoomGrip.y + 20)); EndDrag();
        ConfirmGesture(Workspace.QueryDocument() != Oriented, "Zoomed transform grips stopped responding.");
        Click("Save document");
        ConfirmGesture(DecodeVectorAuthoring(SavedSource) == Workspace.QueryDocument(), "Transformed native source did not round-trip TOML.");
        ConfirmGesture(Workspace.QueryDiagnostic().empty(), "Native gesture left a diagnostic.");
        {
            VectorAuthoringDocument PickingDocument;
            PickingDocument.Name = "GPU contour selection";
            VectorCoverageInstruction Source;
            Source.Bounds = {256, 256, 180, 180}; Source.Colour = {.25f, .5f, .8f, 1};
            const auto Underlay = ConstructVectorAuthoringPrimitive(PickingDocument, Source, "Blue underlay");
            Source.Colour = {.85f, .3f, .2f, 1};
            const auto Ring = ConstructVectorAuthoringPrimitive(PickingDocument, Source, "Red Boolean ring");
            Source.Topology[0] = 1; Source.Bounds[2] = Source.Bounds[3] = 80;
            PickingDocument.Instructions.push_back(Source);
            VectorCoverageInstruction Difference; Difference.Topology = {17, 1, 2, 0};
            PickingDocument.Instructions.push_back(Difference); PickingDocument.Layers.back().Root = 3;
            Workspace.ConfigureDocument(PickingDocument); Click("Fit"); Frame(); Frame();
            ClickPoint(PointOnBoard(256, 256));
            ConfirmPicking(Workspace.QuerySelection() == std::vector<uint32_t>({Underlay}), "Native click did not select through a Boolean hole.");
            Capture("NativePatternContourPicking");
            ClickPoint(PointOnBoard(320, 256));
            ConfirmPicking(Workspace.QuerySelection() == std::vector<uint32_t>({Ring}), "Native click did not select the Boolean ring coverage.");
            ClickPoint(PointOnBoard(460, 410));
            ConfirmPicking(Workspace.QuerySelection().empty(), "Native evaluated miss did not clear selection.");
            Workspace.ConfigureDocument(PickingDocument); Frame(); Frame();
            const auto FastStart = PointOnBoard(257.8f, 257.3f), FastFinish = PointOnBoard(287.8f, 267.3f);
            BeginDrag(FastStart);
            Input.AddMousePosEvent(FastFinish.x, FastFinish.y); Input.AddMouseButtonEvent(0, false); Frame(0); Frame(0);
            Input.AddMousePosEvent(400, 100); Frame(); Frame(); Frame();
            const auto FastBoard = Workspace.QueryBoardExtent();
            const float ExpectedX = 256 + (std::floor(FastFinish.x) - std::floor(FastStart.x)) / FastBoard.Width() * 512;
            const float ExpectedY = 256 + (std::floor(FastFinish.y) - std::floor(FastStart.y)) / FastBoard.Height() * 512;
            ConfirmPicking(Near(Workspace.QueryDocument().Instructions[Workspace.QueryDocument().Layers[0].Root].Bounds[0], ExpectedX) &&
                Near(Workspace.QueryDocument().Instructions[Workspace.QueryDocument().Layers[0].Root].Bounds[1], ExpectedY),
                "Fast drag lost its release point while GPU selection was pending.");
            Click("Undo"); ConfirmPicking(Workspace.QueryDocument() == PickingDocument, "Fast pending drag did not remain one undoable edit.");
            Workspace.ConfigureDocument(PickingDocument); Frame(); Frame();
            auto Callback = Workspace.QuerySelectionToken;
            Workspace.QuerySelectionToken = {};
            ClickPoint(PointOnBoard(256, 256));
            ConfirmPicking(Workspace.QuerySelection().empty() && !Workspace.QueryDiagnostic().empty(), "Unavailable GPU picking fell back to bounding-box selection.");
            Workspace.QuerySelectionToken = Callback;
            Workspace.ConfigureDocument(PickingDocument); Frame(); Frame();
            BeginDrag(PointOnBoard(251.3f, 252.8f));
            Input.AddKeyEvent(ImGuiKey_Escape, true); Frame();
            Input.AddKeyEvent(ImGuiKey_Escape, false); EndDrag(); Frame(); Frame();
            ConfirmPicking(Workspace.QuerySelection().empty() && Workspace.QueryDocument() == PickingDocument, "Late GPU result resurrected a cancelled selection.");
            BeginDrag(PointOnBoard(253.1f, 254.2f));
            auto Replacement = PickingDocument; Replacement.Name = "Source replaced while selection pending";
            Workspace.ConfigureDocument(Replacement); EndDrag(); Frame(); Frame();
            ConfirmPicking(Workspace.QuerySelection().empty() && Workspace.QueryDocument() == Replacement, "Late GPU result changed a replacement document.");
        }
        const auto Key = [&](ImGuiKey Code)
        {
            Input.AddKeyEvent(Code, true); Frame();
            Input.AddKeyEvent(Code, false); Frame(); Frame();
        };
        {
            Workspace.ConfigureDocument({}); Frame(); Frame();
            const auto InspectMenu = [&](const char *Caption)
            {
                const auto &Popups = ImGui::GetCurrentContext()->OpenPopupStack;
                ConfirmDropdown(!Popups.empty() && Popups.back().Window, Caption);
                const auto *Popup = Popups.back().Window;
                ConfirmDropdown(Near(Popup->WindowRounding, 8) && Near(Popup->WindowPadding.x, 12)
                    && Near(Popup->WindowPadding.y, 10), "Native dropdown bypassed the shared rounded/padded surface.");
            };
            Click("Add pattern layer"); InspectMenu("Add motif dropdown absent."); Capture("NativeDropdownAddMotif");
            Key(ImGuiKey_Escape); ConfirmDropdown(ImGui::GetCurrentContext()->OpenPopupStack.empty(), "Escape did not dismiss add menu."); ConfirmDropdown(Workspace.QueryDocument().Layers.empty(), "Dismissed add dropdown changed source.");
            Click("More tools"); InspectMenu("More tools dropdown absent.");
            ConfirmDropdown(Workspace.QueryControlExtent("Automatic Pen curves").Width()
                >= Typeface->CalcTextSizeA(ImGui::GetCurrentContext()->OpenPopupStack.back().Window->FontRefSize, FLT_MAX, 0, "Automatic Pen curves").x + 46,
                "Action dropdown clipped its toggle captions.");
            Capture("NativeDropdownMoreTools"); Key(ImGuiKey_Escape);
            Click("Document actions"); InspectMenu("Document dropdown absent."); Capture("NativeDropdownDocument"); Key(ImGuiKey_Escape);
            Click("Add pattern layer"); Click("Rectangle");
            ConfirmDropdown(Workspace.QueryDocument().Layers.size() == 1, "Shared action row did not activate its native operation.");
            Click("Select:1");
            if (Workspace.QueryControlExtent("Array layout").Width() == 0) Click("Modifiers");
            Click("Array layout");
            InspectMenu("Array dropdown absent."); Capture("NativeDropdownArray");
            const auto BeforeDismiss = Workspace.QueryDocument();
            Key(ImGuiKey_Escape);
            ConfirmDropdown(Workspace.QueryDocument() == BeforeDismiss, "Dismissed selection dropdown changed array source.");
        }
        uint32_t EndpointChecks = 0;
        const auto ConfirmEndpoint = [&](bool Condition, const char *Caption) { AssertRasterCondition(Condition, Caption); ++EndpointChecks; };
        for (bool FromStart : {false, true})
        {
            VectorAuthoringDocument Source;
            CubicPath Path;
            Path.Anchors = {{{140, 160}, {140, 160}, {190, 110}}, {{300, 230}, {250, 280}, {300, 230}}};
            VectorCoverageInstruction Style;
            Style.Colour = {.2f, .5f, .9f, .6f}; Style.Transform[1] = 3;
            const auto Token = ConstructCubicPath(Source, Path, Style, "Independent endpoint source");
            Workspace.ConfigureDocument(Source); Frame(); Frame(); Click("Fit"); Click("Select:1"); Click("Pen tool (P)");
            const auto Revision = Workspace.QueryRevision();
            const auto Endpoint = Grip(FromStart ? "Resume start endpoint" : "Resume end endpoint");
            Input.AddKeyEvent(ImGuiMod_Ctrl, true); Frame(); ClickPoint(Endpoint);
            Input.AddKeyEvent(ImGuiMod_Ctrl, false); Frame();
            ConfirmEndpoint(Workspace.QueryDocument() == Source && Workspace.QueryRevision() == Revision,
                "Ctrl endpoint click edited retained source or finalized a single anchor.");
            ClickPoint(PointOnBoard(380, 350));
            const auto Preview = Workspace.QueryDocument();
            ConfirmEndpoint(Preview.Layers.size() == 2 && Preview.Layers[0] == Source.Layers[0],
                "Ctrl endpoint click resumed the original path instead of creating a separate layer.");
            ConfirmEndpoint(DecodeCubicPath(Preview, Token) == DecodeCubicPath(Source, Token),
                "Separate endpoint preview changed original controls.");
            const auto NewToken = Preview.Layers.back().Token;
            ConfirmEndpoint(NewToken != Token && DecodeCubicPath(Preview, NewToken).Anchors.size() == 2,
                "Separate endpoint path reused identity or duplicated an anchor.");
            ConfirmEndpoint(Workspace.QueryRevision() == Revision, "Separate endpoint preview changed history.");
            Capture("NativePatternIndependentEndpoint");
            Key(ImGuiKey_Enter); const auto Finalized = Workspace.QueryDocument();
            ConfirmEndpoint(Workspace.QueryRevision() == Revision + 1, "Separate endpoint path did not finalize once.");
            Click("Undo"); ConfirmEndpoint(Workspace.QueryDocument() == Source, "Separate endpoint Undo did not preserve the original path.");
            Click("Redo"); ConfirmEndpoint(Workspace.QueryDocument() == Finalized, "Separate endpoint Redo did not restore both paths.");
            Workspace.ConfigureDocument(Source); Frame(); Frame(); Click("Select:1");
            Input.AddKeyEvent(ImGuiMod_Ctrl, true); Frame(); ClickPoint(Grip(FromStart ? "Resume start endpoint" : "Resume end endpoint"));
            Input.AddKeyEvent(ImGuiMod_Ctrl, false); Frame(); ClickPoint(PointOnBoard(400, 320)); Key(ImGuiKey_Escape);
            ConfirmEndpoint(Workspace.QueryDocument() == Source, "Cancelled separate endpoint path changed retained source.");
            ClickPoint(Grip(FromStart ? "Resume start endpoint" : "Resume end endpoint"));
            ClickPoint(PointOnBoard(400, 320));
            ConfirmEndpoint(Workspace.QueryDocument().Layers.size() == 1
                && DecodeCubicPath(Workspace.QueryDocument(), Token).Anchors.size() == 3,
                "Normal endpoint click no longer resumed the existing path.");
            Key(ImGuiKey_Escape);
            ConfirmEndpoint(Workspace.QueryDocument() == Source, "Normal continuation cancellation changed retained source.");
        }
        uint32_t CornerChecks = 0;
        const auto ConfirmCorner = [&](bool Condition, const char *Caption) { AssertRasterCondition(Condition, Caption); ++CornerChecks; };
        uint32_t PlacementChecks = 0;
        const auto ConfirmPlacement = [&](bool Condition, const char *Caption) { AssertRasterCondition(Condition, Caption); ++PlacementChecks; };
        {
            const bool Movie = Count == 6 && std::string_view(Arguments[5]) == "--animate-continuation";
            const bool PlacementMovie = Count == 6 && std::string_view(Arguments[5]) == "--animate-placement";
            if (PlacementMovie) { Input.MouseDrawCursor = true; RecordAnimation = true; }
            Workspace.ConfigureDocument({}); Frame(); Frame(); Click("Fit"); Click("Pen tool (P)");
            ClickPoint(PointOnBoard(120, 180)); ClickPoint(PointOnBoard(240, 200));
            const auto Revision = Workspace.QueryRevision();
            BeginDrag(PointOnBoard(350, 250)); MoveDrag(PointOnBoard(380, 220));
            const auto Preview = Workspace.QueryDocument(); const auto Token = Preview.Layers.back().Token;
            const auto Original = DecodeCubicPath(Preview, Token);
            const auto Pixels = Coverage;
            Input.AddKeyEvent(ImGuiKey_Space, true); Frame();
            MoveDrag(PointOnBoard(410, 240));
            const auto Repositioned = DecodeCubicPath(Workspace.QueryDocument(), Token);
            const float ΔX = Repositioned.Anchors.back().Position[0] - Original.Anchors.back().Position[0];
            const float ΔY = Repositioned.Anchors.back().Position[1] - Original.Anchors.back().Position[1];
            ConfirmPlacement(ΔX > 25 && ΔY > 15, "Space placement did not reposition new anchor.");
            ConfirmPlacement(Near(Repositioned.Anchors.back().Outgoing[0] - Original.Anchors.back().Outgoing[0], ΔX)
                && Near(Repositioned.Anchors.back().Incoming[1] - Original.Anchors.back().Incoming[1], ΔY), "Space placement changed tangent offsets.");
            ConfirmPlacement(Repositioned.Anchors[0] == Original.Anchors[0] && Repositioned.Anchors[1] == Original.Anchors[1], "Space placement moved earlier anchors.");
            ConfirmPlacement(Workspace.QueryRevision() == Revision && Pixels != Coverage, "Space placement changed history or did not change Vulkan preview.");
            Capture("NativePatternPenPlacement");
            Input.AddKeyEvent(ImGuiKey_Space, false); Frame();
            MoveDrag(PointOnBoard(430, 230));
            const auto Reshaped = DecodeCubicPath(Workspace.QueryDocument(), Token);
            ConfirmPlacement(Near(Reshaped.Anchors.back().Position[0], Repositioned.Anchors.back().Position[0])
                && Near(Reshaped.Anchors.back().Position[1], Repositioned.Anchors.back().Position[1])
                && !Near(Reshaped.Anchors.back().Incoming[0], Repositioned.Anchors.back().Incoming[0]),
                "Releasing Space did not return to tangent placement at the translated anchor.");
            EndDrag(); Key(ImGuiKey_Enter); const auto Finished = Workspace.QueryDocument();
            ConfirmPlacement(Workspace.QueryRevision() == Revision + 1, "Repositioned Pen path did not finalize once.");
            Click("Undo"); ConfirmPlacement(Workspace.QueryDocument().Layers.empty(), "Repositioned Pen Undo failed.");
            Click("Redo"); ConfirmPlacement(Workspace.QueryDocument() == Finished, "Repositioned Pen Redo failed.");
            BeginDrag(Grip("Resume end endpoint"));
            const auto Retained = Workspace.QueryDocument();
            Input.AddKeyEvent(ImGuiKey_Space, true); Frame(); MoveDrag(PointOnBoard(460, 290));
            ConfirmPlacement(Workspace.QueryDocument() == Retained, "Space moved a retained endpoint before a new anchor was placed.");
            EndDrag(); Input.AddKeyEvent(ImGuiKey_Space, false); Frame();
            BeginDrag(PointOnBoard(440, 300)); Input.AddKeyEvent(ImGuiKey_Space, true); Frame(); MoveDrag(PointOnBoard(460, 320));
            Input.AddKeyEvent(ImGuiKey_Space, false); Frame(); EndDrag(); Key(ImGuiKey_Escape);
            ConfirmPlacement(Workspace.QueryDocument() == Retained, "Space placement continuation cancel changed retained source.");
            if (PlacementMovie)
            {
                RecordAnimation = false;
                std::ofstream Metadata(Destination / "MotionFrames.json");
                Metadata << "{\"frames\":" << AnimationOrdinal << ",\"width\":1440,\"height\":900,\"native_mouse_cursor\":true,\"native_pointer_input\":true,\"windows_executed\":false}\n";
                ImGui::DestroyContext(); return 0;
            }
            if (Movie) { Input.MouseDrawCursor = true; RecordAnimation = true; }
            for (bool DragOutgoing : {false, true})
            {
                Workspace.ConfigureDocument({}); Frame(); Frame(); Click("Fit"); Click("Pen tool (P)");
                ClickPoint(PointOnBoard(120, 180));
                BeginDrag(PointOnBoard(240, 200)); MoveDrag(PointOnBoard(270, 160)); EndDrag();
                const auto Before = Workspace.QueryDocument(); const auto PathToken = Before.Layers.back().Token;
                const auto PathBefore = DecodeCubicPath(Before, PathToken);
                const auto BeforeRevision = Workspace.QueryRevision();
                Input.AddKeyEvent(ImGuiMod_Alt, true); Frame();
                BeginDrag(Grip("Pen anchor 1"));
                if (DragOutgoing) MoveDrag(PointOnBoard(280, 250));
                const auto Board = Workspace.QueryBoardExtent();
                const std::array<float, 2> ExpectedOutgoing{(Input.MousePos.x - Board.MinimumX) / Board.Width() * 512,
                    (Input.MousePos.y - Board.MinimumY) / Board.Height() * 512};
                Input.AddKeyEvent(ImGuiMod_Alt, false); Frame(); EndDrag();
                ConfirmCorner(DecodeCubicPath(Workspace.QueryDocument(), PathToken).Anchors.size() == 2,
                    "Alt-click appended a duplicate pending anchor.");
                ClickPoint(PointOnBoard(360, 280));
                const auto Corner = DecodeCubicPath(Workspace.QueryDocument(), PathToken);
                ConfirmCorner(Near(Corner.Anchors[1].Incoming[0], PathBefore.Anchors[1].Incoming[0])
                    && Near(Corner.Anchors[1].Incoming[1], PathBefore.Anchors[1].Incoming[1]),
                    "Independent outgoing continuation changed the incoming segment.");
                ConfirmCorner(Near(Corner.Anchors[1].Outgoing[0], DragOutgoing ? ExpectedOutgoing[0] : Corner.Anchors[1].Position[0])
                    && Near(Corner.Anchors[1].Outgoing[1], DragOutgoing ? ExpectedOutgoing[1] : Corner.Anchors[1].Position[1]),
                    "Alt continuation did not retain the corner/independent outgoing control.");
                ConfirmCorner(Workspace.QueryRevision() == BeforeRevision, "Alt continuation finalized preview history.");
                Capture("NativePatternPenCorner");
                Key(ImGuiKey_Enter); const auto Finalized = Workspace.QueryDocument();
                ConfirmCorner(Workspace.QueryRevision() == BeforeRevision + 1, "Alt continuation did not finalize once.");
                Click("Undo"); ConfirmCorner(Workspace.QueryDocument().Layers.empty(), "Alt continuation Undo failed.");
                Click("Redo"); ConfirmCorner(Workspace.QueryDocument() == Finalized, "Alt continuation Redo failed.");
                ClickPoint(Grip("Resume end endpoint"));
                ClickPoint(PointOnBoard(440, 300));
                Input.AddKeyEvent(ImGuiMod_Alt, true); Frame(); ClickPoint(Grip("Pen anchor 3"));
                Input.AddKeyEvent(ImGuiMod_Alt, false); Frame(); Key(ImGuiKey_Escape);
                ConfirmCorner(Workspace.QueryDocument() == Finalized, "Cancelled Alt continuation changed retained source.");
            }
            Workspace.ConfigureDocument({}); Frame(); Frame(); Click("More tools"); Click("Automatic Pen curves");
            Click("Pen tool (P)"); ClickPoint(PointOnBoard(120, 180)); ClickPoint(PointOnBoard(240, 200));
            const auto Automatic = Workspace.QueryDocument();
            Input.AddKeyEvent(ImGuiMod_Alt, true); Frame(); ClickPoint(Grip("Pen anchor 1"));
            Input.AddKeyEvent(ImGuiMod_Alt, false); Frame();
            ConfirmCorner(Workspace.QueryDocument() == Automatic && !Workspace.QueryDiagnostic().empty(),
                "Automatic Pen corner conversion was not refused without changing the preview.");
            Key(ImGuiKey_Escape);
            if (Movie)
            {
                for (int Index = 0; Index < 5; ++Index) Frame();
                RecordAnimation = false;
                std::ofstream Metadata(Destination / "MotionFrames.json");
                Metadata << "{\"frames\":" << AnimationOrdinal << ",\"width\":1440,\"height\":900,\"native_mouse_cursor\":true,\"native_pointer_input\":true,\"windows_executed\":false}\n";
                std::cout << AnimationOrdinal << " native Pen-placement frames captured.\n";
                ImGui::DestroyContext(); return 0;
            }
        }
        uint32_t MultipointChecks = 0;
        const auto ConfirmMultipoint = [&](bool Condition, const char *Caption) { AssertRasterCondition(Condition, Caption); ++MultipointChecks; };
        {
            VectorAuthoringDocument Source;
            CubicPath Path;
            for (auto Position : {std::array<float, 2>{100, 140}, {210, 300}, {330, 160}, {410, 310}})
                Path.Anchors.push_back({Position, Position, Position});
            VectorCoverageInstruction Style; Style.Transform[1] = 4; Style.Colour = {.9f, .2f, .1f, 1};
            const auto Token = ConstructCubicPath(Source, Path, Style, "Automatic curves and multipoint");
            Workspace.ConfigureDocument(Source); Frame(); Frame(); Click("Fit"); Click("Select:1"); Click("Point tool (A)");
            const bool Movie = Count == 6 && std::string_view(Arguments[5]) == "--animate-multipoint";
            if (Movie) { Input.MouseDrawCursor = true; RecordAnimation = true; }
            ClickPoint(Grip("Path anchor 0"));
            Input.AddKeyEvent(ImGuiMod_Shift, true); Frame(); ClickPoint(Grip("Path anchor 1"));
            Input.AddKeyEvent(ImGuiMod_Shift, false); Frame();
            ConfirmMultipoint(Workspace.QueryAnchorSelection() == std::vector<size_t>{0, 1}, "Shift-click did not extend point selection.");
            const auto Before = Workspace.QueryDocument(); const auto Revision = Workspace.QueryRevision();
            const auto Start = Grip("Path anchor 1"); BeginDrag(Start);
            for (int Index = 1; Index <= 8; ++Index) MoveDrag(ImVec2(Start.x + 24 * float(Index) / 8, Start.y + 12 * float(Index) / 8));
            ConfirmMultipoint(Workspace.QueryRevision() == Revision && Workspace.QueryDocument() != Before, "Multipoint preview changed history or did not edit.");
            EndDrag(); const auto Moved = Workspace.QueryDocument(); const auto MovedPath = DecodeCubicPath(Moved, Token);
            ConfirmMultipoint(Workspace.QueryRevision() == Revision + 1, "Multipoint gesture was not one undo edit.");
            for (size_t Index = 0; Index < 2; ++Index)
                ConfirmMultipoint(Near(MovedPath.Anchors[Index].Position[0] - Path.Anchors[Index].Position[0], MovedPath.Anchors[0].Position[0] - Path.Anchors[0].Position[0]),
                    "Multipoint anchors moved by different displacements.");
            ConfirmMultipoint(MovedPath.Anchors[2] == Path.Anchors[2] && MovedPath.Anchors[3] == Path.Anchors[3], "Multipoint drag moved an unselected anchor.");
            Click("Undo"); ConfirmMultipoint(Workspace.QueryDocument() == Before, "Multipoint Undo failed.");
            Click("Redo"); ConfirmMultipoint(Workspace.QueryDocument() == Moved, "Multipoint Redo failed.");
            const auto Pixels = Coverage;
            Click("Automatic selected tangents"); const auto Automatic = Workspace.QueryDocument(); const auto AutomaticPath = DecodeCubicPath(Automatic, Token);
            ConfirmMultipoint(AutomaticPath.Anchors[0].Outgoing != MovedPath.Anchors[0].Outgoing && AutomaticPath.Anchors[1].Incoming != MovedPath.Anchors[1].Incoming,
                "Automatic selected tangents did not derive controls.");
            ConfirmMultipoint(AutomaticPath.Anchors[2] == MovedPath.Anchors[2] && AutomaticPath.Anchors[3] == MovedPath.Anchors[3], "Automatic tangents touched unselected anchors.");
            ConfirmMultipoint(Coverage != Pixels, "Automatic curves did not change Vulkan pixels.");
            Capture("NativePatternMultipoint");
            Click("Undo"); ConfirmMultipoint(Workspace.QueryDocument() == Moved, "Automatic curves Undo failed.");
            Click("Redo"); ConfirmMultipoint(Workspace.QueryDocument() == Automatic, "Automatic curves Redo failed.");
            Key(ImGuiKey_RightArrow); const auto Nudged = DecodeCubicPath(Workspace.QueryDocument(), Token);
            ConfirmMultipoint(Near(Nudged.Anchors[0].Position[0], AutomaticPath.Anchors[0].Position[0] + 1) && Near(Nudged.Anchors[1].Position[0], AutomaticPath.Anchors[1].Position[0] + 1),
                "Keyboard did not nudge all selected points.");
            ConfirmMultipoint(Nudged.Anchors[2] == AutomaticPath.Anchors[2], "Keyboard nudge touched unrelated point.");
            Click("Undo"); Key(ImGuiKey_Delete);
            ConfirmMultipoint(DecodeCubicPath(Workspace.QueryDocument(), Token).Anchors.size() == 2, "Batch Delete did not remove selected anchors.");
            Click("Undo"); ConfirmMultipoint(Workspace.QueryDocument() == Automatic && Workspace.QueryAnchorSelection().empty(), "Topology Undo did not restore source and invalidate stale point indices.");
            Click("Select all anchors"); const auto MinimumBefore = Workspace.QueryDocument(); Key(ImGuiKey_Delete);
            ConfirmMultipoint(Workspace.QueryDocument() == MinimumBefore && !Workspace.QueryDiagnostic().empty(), "Batch Delete below minimum was not refused atomically.");
            Click("Automatic selected tangents"); const auto AllAutomatic = Workspace.QueryDocument();
            ConfirmMultipoint(DecodeVectorAuthoring(EncodeVectorAuthoring(AllAutomatic)) == AllAutomatic, "Automatic/multipoint source TOML round trip failed.");
            BeginDrag(Grip("Path anchor 1")); MoveDrag(PointOnBoard(280, 320)); Key(ImGuiKey_Escape); EndDrag();
            ConfirmMultipoint(Workspace.QueryDocument() == AllAutomatic, "Multipoint Escape finalized preview.");
            if (Movie)
            {
                for (int Index = 0; Index < 5; ++Index) Frame();
                RecordAnimation = false;
                std::ofstream Metadata(Destination / "MotionFrames.json");
                Metadata << "{\"frames\":" << AnimationOrdinal << ",\"width\":1440,\"height\":900,\"native_mouse_cursor\":true,\"native_pointer_input\":true,\"windows_executed\":false}\n";
                std::cout << AnimationOrdinal << " native multipoint frames captured.\n";
                ImGui::DestroyContext(); return 0;
            }
            Workspace.ConfigureDocument({}); Frame(); Frame(); Click("More tools"); Click("Automatic Pen curves");
            Frame(); Frame(); Click("Pen tool (P)");
            ClickPoint(PointOnBoard(100, 140)); ClickPoint(PointOnBoard(210, 300)); ClickPoint(PointOnBoard(330, 160));
            ConfirmMultipoint(!Workspace.QueryDocument().Layers.empty(), "Automatic Pen preview is empty.");
            const auto PenPreview = DecodeCubicPath(Workspace.QueryDocument(), Workspace.QueryDocument().Layers[0].Token);
            ConfirmMultipoint(PenPreview.Anchors[1].Outgoing != PenPreview.Anchors[1].Position, "Automatic Pen preview did not derive tangents.");
            Key(ImGuiKey_Enter); const auto PenDocument = Workspace.QueryDocument();
            ConfirmMultipoint(DecodeCubicPath(PenDocument, PenDocument.Layers[0].Token) == PenPreview, "Automatic Pen finish changed preview geometry.");
            Click("Undo"); ConfirmMultipoint(Workspace.QueryDocument().Layers.empty(), "Automatic Pen did not finalize in one undo edit.");
        }
        uint32_t SegmentChecks = 0;
        const auto ConfirmSegment = [&](bool Condition, const char *Caption) { AssertRasterCondition(Condition, Caption); ++SegmentChecks; };
        {
            VectorAuthoringDocument Source;
            CubicPath Path;
            Path.Anchors = {{{100, 200}, {100, 200}, {200, 200}}, {{400, 200}, {300, 200}, {400, 200}}};
            VectorCoverageInstruction Style; Style.Paint[3] = 0; Style.Colour = {1, .15f, .1f, 1}; Style.Transform[1] = 4;
            const auto Token = ConstructCubicPath(Source, Path, Style, "Direct segment editing");
            Workspace.ConfigureDocument(Source); Frame(); Frame(); Click("Fit"); Click("Select:1"); Click("Point tool (A)");
            const bool Movie = Count == 6 && std::string_view(Arguments[5]) == "--animate-segment";
            if (Movie) { Input.MouseDrawCursor = true; RecordAnimation = true; }
            const auto Before = Workspace.QueryDocument(); const auto Revision = Workspace.QueryRevision();
            ClickPoint(PointOnBoard(250, 200));
            const auto Split = Workspace.QueryDocument();
            ConfirmSegment(DecodeCubicPath(Split, Token).Anchors.size() == 3 && Workspace.QueryRevision() == Revision + 1,
                "Direct curve click did not split in exactly one history edit.");
            ConfirmSegment(Split.Layers == Before.Layers, "Segment splitting changed layer identity.");
            ConfirmSegment(std::abs(DecodeCubicPath(Split, Token).Anchors[1].Position[0] - 250) < 2, "Pointer split missed its curve parameter.");
            Click("Undo"); ConfirmSegment(Workspace.QueryDocument() == Before, "Direct split Undo failed.");
            Click("Redo"); ConfirmSegment(Workspace.QueryDocument() == Split, "Direct split Redo failed.");
            Click("Undo");
            const auto Pixels = Coverage; const auto BendRevision = Workspace.QueryRevision();
            const auto Start = PointOnBoard(250, 200), Finish = PointOnBoard(250, 250);
            BeginDrag(Start);
            for (int Index = 1; Index <= 12; ++Index) MoveDrag(ImVec2(Start.x, Start.y + (Finish.y - Start.y) * float(Index) / 12));
            ConfirmSegment(Workspace.QueryRevision() == BendRevision && Workspace.QueryDocument() != Before, "Bend preview finalized history or did not change source.");
            EndDrag(); const auto Bent = Workspace.QueryDocument(); const auto BentPath = DecodeCubicPath(Bent, Token);
            ConfirmSegment(Workspace.QueryRevision() == BendRevision + 1 && BentPath.Anchors.size() == 2, "Bending subdivided or finalized more than once.");
            ConfirmSegment(BentPath.Anchors[0].Position == Path.Anchors[0].Position && BentPath.Anchors[1].Position == Path.Anchors[1].Position,
                "Direct bending moved an endpoint.");
            ConfirmSegment(Coverage != Pixels, "Direct bend did not change Vulkan canvas pixels.");
            ConfirmSegment(DecodeVectorAuthoring(EncodeVectorAuthoring(Bent)) == Bent, "Direct bend TOML round trip failed.");
            Capture("NativePatternSegmentEditing");
            Click("Undo"); ConfirmSegment(Workspace.QueryDocument() == Before, "Direct bend Undo failed.");
            Click("Redo"); ConfirmSegment(Workspace.QueryDocument() == Bent, "Direct bend Redo failed.");
            Click("Undo"); BeginDrag(Start); MoveDrag(Finish); Key(ImGuiKey_Escape); EndDrag();
            ConfirmSegment(Workspace.QueryDocument() == Before, "Escape finalized a direct bend.");
            if (Movie)
            {
                for (int Index = 0; Index < 6; ++Index) Frame();
                RecordAnimation = false;
                std::ofstream Metadata(Destination / "MotionFrames.json");
                Metadata << "{\"frames\":" << AnimationOrdinal << ",\"width\":1440,\"height\":900,\"native_mouse_cursor\":true,\"native_pointer_input\":true,\"windows_executed\":false}\n";
                std::cout << AnimationOrdinal << " native segment-editing frames captured.\n";
                ImGui::DestroyContext(); return 0;
            }
        }
        uint32_t PointChecks = 0;
        const auto ConfirmPoint = [&](bool Condition, const char *Caption) { AssertRasterCondition(Condition, Caption); ++PointChecks; };
        {
            VectorAuthoringDocument Points;
            CubicPath Path;
            Path.Closed = true;
            Path.Anchors = {{{160, 160}, {160, 160}, {160, 160}},
                {{320, 160}, {280, 140}, {360, 180}}, {{260, 340}, {260, 340}, {260, 340}}};
            const auto Token = ConstructCubicPath(Points, Path, {}, "Editable cubic contour");
            Workspace.ConfigureDocument(Points); Frame(); Frame();
            Key(ImGuiKey_V); ClickPoint(PointOnBoard(250, 240));
            ConfirmPoint(Workspace.QuerySelection() == std::vector<uint32_t>{Token}, "Point fixture selection failed.");
            Click("Point tool (A)");
            ClickPoint(Grip("Path anchor 1"));
            const auto Before = Workspace.QueryDocument();
            const auto Revision = Workspace.QueryRevision();
            ClickPoint(Grip("Path anchor 1"));
            ConfirmPoint(Workspace.QueryDocument() == Before && Workspace.QueryRevision() == Revision, "Point selection created history.");
            const auto PixelsBefore = Coverage;
            const auto Start = Grip("Path anchor 1");
            BeginDrag(Start); MoveDrag(ImVec2(Start.x + 24, Start.y + 16));
            ConfirmPoint(Workspace.QueryRevision() == Revision && Workspace.QueryDocument() != Before, "Point preview changed history or did not update geometry.");
            EndDrag();
            const auto Edited = Workspace.QueryDocument();
            ConfirmPoint(Workspace.QueryRevision() == Revision + 1, "Point drag did not create exactly one history operation.");
            const auto EditedPath = DecodeCubicPath(Edited, Token);
            const float ΔX = (std::floor(Start.x + 24) - std::floor(Start.x)) / Workspace.QueryBoardExtent().Width() * 512;
            const float ΔY = (std::floor(Start.y + 16) - std::floor(Start.y)) / Workspace.QueryBoardExtent().Height() * 512;
            ConfirmPoint(Near(EditedPath.Anchors[1].Position[0], 320 + ΔX) && Near(EditedPath.Anchors[1].Position[1], 160 + ΔY), "Point drag moved the anchor to an incorrect coordinate.");
            ConfirmPoint(Near(EditedPath.Anchors[1].Incoming[0], 280 + ΔX) && Near(EditedPath.Anchors[1].Outgoing[1], 180 + ΔY), "Point drag failed to translate retained controls.");
            ConfirmPoint(PixelsBefore != Coverage, "Point drag did not alter GPU coverage pixels.");
            Capture("NativePatternPointEditing");
            Click("Undo"); ConfirmPoint(Workspace.QueryDocument() == Before, "Point drag undo did not restore exact source.");
            Click("Redo"); ConfirmPoint(Workspace.QueryDocument() == Edited, "Point drag redo did not restore exact source.");
            const auto Tangent = Grip("Path outgoing 1");
            BeginDrag(Tangent); MoveDrag(ImVec2(Tangent.x + 12, Tangent.y - 18)); EndDrag();
            const auto Mirrored = DecodeCubicPath(Workspace.QueryDocument(), Token);
            for (size_t Axis = 0; Axis < 2; ++Axis)
                ConfirmPoint(Near(Mirrored.Anchors[1].Incoming[Axis] + Mirrored.Anchors[1].Outgoing[Axis], 2 * Mirrored.Anchors[1].Position[Axis]), "Native tangent drag did not mirror the opposite control.");
            const auto IndependentStart = Grip("Path outgoing 1");
            Input.AddKeyEvent(ImGuiMod_Alt, true); Frame();
            BeginDrag(IndependentStart); MoveDrag(ImVec2(IndependentStart.x + 10, IndependentStart.y + 10)); EndDrag();
            Input.AddKeyEvent(ImGuiMod_Alt, false); Frame();
            const auto Independent = DecodeCubicPath(Workspace.QueryDocument(), Token);
            ConfirmPoint(Independent.Anchors[1].Incoming == Mirrored.Anchors[1].Incoming, "Alt tangent drag moved the opposite control.");
            const auto FastStart = Grip("Path anchor 1");
            const auto BeforeFast = Workspace.QueryDocument();
            const auto RevisionBeforeFast = Workspace.QueryRevision();
            BeginDrag(FastStart);
            Input.AddMousePosEvent(FastStart.x + 14, FastStart.y + 8); Input.AddMouseButtonEvent(0, false); Frame(); Frame();
            ConfirmPoint(Workspace.QueryDocument() != BeforeFast && Workspace.QueryRevision() == RevisionBeforeFast + 1, "Fast release lost or duplicated a point edit.");
            Click("Undo"); ConfirmPoint(Workspace.QueryDocument() == BeforeFast, "Fast point release undo failed.");
            const auto Stable = Workspace.QueryDocument();
            const auto StableRevision = Workspace.QueryRevision();
            BeginDrag(Grip("Path anchor 1")); MoveDrag(PointOnBoard(400, 200));
            Input.AddKeyEvent(ImGuiKey_Escape, true); Frame();
            Input.AddKeyEvent(ImGuiKey_Escape, false); EndDrag();
            ConfirmPoint(Workspace.QueryDocument() == Stable && Workspace.QueryRevision() == StableRevision, "Escape finalized a point drag.");
            BeginDrag(Grip("Path anchor 1")); MoveDrag(PointOnBoard(390, 200));
            Input.AddFocusEvent(false); Frame();
            Input.AddMouseButtonEvent(0, false); Input.AddFocusEvent(true); Frame(); Frame();
            ConfirmPoint(Workspace.QueryDocument() == Stable, "Focus loss finalized a point drag.");
            Click("Corner active anchor");
            const auto Corner = DecodeCubicPath(Workspace.QueryDocument(), Token).Anchors[1];
            ConfirmPoint(Corner.Position == Corner.Incoming && Corner.Position == Corner.Outgoing, "Corner control failed.");
            Click("Smooth active anchor");
            ConfirmPoint(DecodeCubicPath(Workspace.QueryDocument(), Token).Anchors[1].Incoming != Corner.Incoming, "Smooth control failed.");
            VectorAuthoringDocument PrimitiveDocument;
            ConstructVectorAuthoringPrimitive(PrimitiveDocument, {}, "Convert rectangle");
            Workspace.ConfigureDocument(PrimitiveDocument); Frame(); Frame();
            Key(ImGuiKey_V); ClickPoint(PointOnBoard(256, 256)); Key(ImGuiKey_A);
            Click("Convert to cubic path");
            ConfirmPoint(Workspace.QueryDocument().Instructions[Workspace.QueryDocument().Layers[0].Root].Topology[0] == 6, "Explicit primitive conversion failed.");
            const auto Converted = Workspace.QueryDocument();
            const auto ConvertedToken = Converted.Layers[0].Token;
            ClickPoint(Grip("Path anchor 1")); Key(ImGuiKey_RightArrow);
            ConfirmPoint(Near(DecodeCubicPath(Workspace.QueryDocument(), ConvertedToken).Anchors[1].Position[0],
                DecodeCubicPath(Converted, ConvertedToken).Anchors[1].Position[0] + 1), "Point keyboard nudge moved the wrong geometry.");
            Click("Undo"); ConfirmPoint(Workspace.QueryDocument() == Converted, "Point nudge undo failed.");
            ClickPoint(Grip("Path anchor 1")); Key(ImGuiKey_Delete);
            ConfirmPoint(DecodeCubicPath(Workspace.QueryDocument(), ConvertedToken).Anchors.size() == 3, "Point Delete did not remove one anchor.");
            const auto ThreeAnchors = Workspace.QueryDocument();
            ClickPoint(Grip("Path anchor 0")); Key(ImGuiKey_Delete);
            ConfirmPoint(Workspace.QueryDocument() == ThreeAnchors && !Workspace.QueryDiagnostic().empty(), "Point Delete destroyed a minimum closed path.");
            Click("Undo"); ConfirmPoint(Workspace.QueryDocument() == Converted, "Anchor removal undo failed.");
            Click("Undo"); ConfirmPoint(Workspace.QueryDocument() == PrimitiveDocument, "Primitive conversion undo failed.");
            ConfirmPoint(Workspace.QueryDiagnostic().empty(), "Point editing left a diagnostic.");
        }
        uint32_t PenChecks = 0;
        const auto ConfirmPen = [&](bool Condition, const char *Caption) { AssertRasterCondition(Condition, Caption); ++PenChecks; };
        {
            VectorAuthoringDocument Empty;
            Workspace.ConfigureDocument(Empty); Frame(); Frame();
            Key(ImGuiKey_P);
            const auto Revision = Workspace.QueryRevision();
            BeginDrag(PointOnBoard(160, 160)); MoveDrag(PointOnBoard(190, 130)); EndDrag();
            ConfirmPen(Workspace.QueryDocument() == Empty && Workspace.QueryRevision() == Revision, "One pen anchor created retained source history.");
            ClickPoint(PointOnBoard(320, 160));
            ConfirmPen(Workspace.QueryDocument().Layers.size() == 1 && Workspace.QueryRevision() == Revision, "Pen preview failed or changed history.");
            ConfirmPen(Workspace.QueryDocument().Instructions[Workspace.QueryDocument().Layers[0].Root].Paint[3] == 0, "Open pen preview enabled fill.");
            BeginDrag(PointOnBoard(280, 320)); MoveDrag(PointOnBoard(310, 350)); EndDrag();
            Capture("NativePatternPenDraft");
            ClickPoint(Grip("Pen anchor 0"));
            ConfirmPen(Workspace.QueryDocument().Layers.size() == 1 && Workspace.QueryRevision() == Revision + 1, "Closed pen path did not finalize exactly once.");
            const auto Closed = Workspace.QueryDocument();
            const auto ClosedPath = DecodeCubicPath(Closed, Closed.Layers[0].Token);
            ConfirmPen(ClosedPath.Closed && ClosedPath.Anchors.size() == 3, "Pen closure lost source topology.");
            ConfirmPen(ClosedPath.Anchors[0].Outgoing[0] > ClosedPath.Anchors[0].Position[0]
                && ClosedPath.Anchors[0].Incoming[0] < ClosedPath.Anchors[0].Position[0], "Pen drag lost mirrored controls.");
            ConfirmPen(Coverage[size_t(230 * 512 + 250) * 4 + 3] > 240, "Closed pen path did not produce filled GPU coverage.");
            Capture("NativePatternPenEditing");
            Click("Undo"); ConfirmPen(Workspace.QueryDocument() == Empty, "Pen path undo did not restore the empty source.");
            Click("Redo"); ConfirmPen(Workspace.QueryDocument() == Closed, "Pen path redo changed retained geometry.");
            ConfirmPen(DecodeVectorAuthoring(EncodeVectorAuthoring(Closed)) == Closed, "Pen path TOML round trip failed.");
            Workspace.ConfigureDocument(Empty); Frame(); Frame();
            ClickPoint(PointOnBoard(150, 150)); ClickPoint(PointOnBoard(320, 300)); Key(ImGuiKey_Enter);
            const auto Open = Workspace.QueryDocument();
            ConfirmPen(Open.Layers.size() == 1 && !DecodeCubicPath(Open, Open.Layers[0].Token).Closed, "Enter did not finish an open pen path.");
            ConfirmPen(Open.Instructions[Open.Layers[0].Root].Paint[3] == 0, "Finished open pen path retained fill.");
            Workspace.ConfigureDocument(Empty); Frame(); Frame();
            const auto CancelRevision = Workspace.QueryRevision();
            ClickPoint(PointOnBoard(150, 150)); ClickPoint(PointOnBoard(310, 300)); Key(ImGuiKey_Escape);
            ConfirmPen(Workspace.QueryDocument() == Empty && Workspace.QueryRevision() == CancelRevision, "Escape finalized a pending pen path.");
            ClickPoint(PointOnBoard(150, 150)); ClickPoint(PointOnBoard(310, 300)); Key(ImGuiKey_Backspace);
            ConfirmPen(Workspace.QueryDocument() == Empty, "Pen Backspace did not remove the last pending segment.");
            Key(ImGuiKey_Enter);
            ConfirmPen(Workspace.QueryDocument() == Empty && !Workspace.QueryDiagnostic().empty(), "One-anchor pen finish was not refused.");
            Key(ImGuiKey_Escape);
            ClickPoint(PointOnBoard(150, 150)); ClickPoint(PointOnBoard(310, 300));
            Input.AddFocusEvent(false); Frame(); Input.AddFocusEvent(true); Frame(); Frame();
            ConfirmPen(Workspace.QueryDocument() == Empty, "Focus loss retained a live pen session.");
            ClickPoint(PointOnBoard(150, 150)); ClickPoint(PointOnBoard(310, 300));
            Click("Select tool (V)");
            ConfirmPen(Workspace.QueryDocument().Layers.size() == 1, "Tool switch silently dropped a valid pending pen path.");
            Workspace.ConfigureDocument(Empty); Frame(); Frame(); Key(ImGuiKey_P);
            ClickPoint(PointOnBoard(150, 150)); ClickPoint(PointOnBoard(310, 300));
            const auto BeforeSaveRevision = Workspace.QueryRevision();
            Input.AddKeyEvent(ImGuiMod_Ctrl, true); Frame(); Key(ImGuiKey_S);
            Input.AddKeyEvent(ImGuiMod_Ctrl, false); Frame();
            ConfirmPen(Workspace.QueryDocument().Layers.size() == 1 && Workspace.QueryRevision() == BeforeSaveRevision + 1
                && DecodeVectorAuthoring(SavedSource) == Workspace.QueryDocument(), "Ctrl+S did not finalize and serialize the pending pen path.");
            const auto SavedPen = Workspace.QueryDocument();
            ClickPoint(PointOnBoard(170, 150)); ClickPoint(PointOnBoard(310, 340));
            const auto BeforeCancelRevision = Workspace.QueryRevision();
            Input.AddKeyEvent(ImGuiMod_Ctrl, true); Frame(); Key(ImGuiKey_Z);
            Input.AddKeyEvent(ImGuiMod_Ctrl, false); Frame();
            ConfirmPen(Workspace.QueryDocument() == SavedPen && Workspace.QueryRevision() == BeforeCancelRevision, "Pending-path Ctrl+Z changed completed source history.");
            ClickPoint(PointOnBoard(170, 150)); ClickPoint(PointOnBoard(310, 340)); Key(ImGuiKey_A);
            ConfirmPen(Workspace.QueryDocument().Layers.size() == 2, "Point shortcut did not finish a valid pending pen path.");
            ConfirmPen(Workspace.QueryDiagnostic().empty(), "Pen editing left a stale diagnostic.");
        }
        uint32_t RefinementChecks = 0;
        const auto ConfirmRefinement = [&](bool Condition, const char *Caption) { AssertRasterCondition(Condition, Caption); ++RefinementChecks; };
        {
            VectorAuthoringDocument Source;
            CubicPath Path;
            Path.Closed = true;
            Path.Anchors = {{{160, 160}, {160, 160}, {200, 120}},
                {{330, 180}, {290, 140}, {360, 220}}, {{260, 350}, {260, 350}, {260, 350}}};
            const auto Token = ConstructCubicPath(Source, Path, {}, "Subdivision contour");
            Workspace.ConfigureDocument(Source); Frame(); Frame();
            Click("Select:1"); Key(ImGuiKey_A); ClickPoint(Grip("Path anchor 1"));
            const auto Before = Workspace.QueryDocument();
            const auto Revision = Workspace.QueryRevision();
            const auto Decoded = DecodeCubicPath(Before, Token);
            std::array<float, 2> Midpoint;
            for (size_t Axis = 0; Axis < 2; ++Axis)
                Midpoint[Axis] = .125f * Decoded.Anchors[1].Position[Axis] + .375f * Decoded.Anchors[1].Outgoing[Axis]
                    + .375f * Decoded.Anchors[2].Incoming[Axis] + .125f * Decoded.Anchors[2].Position[Axis];
            Click("Subdivide following segment");
            const auto Refined = Workspace.QueryDocument();
            const auto RefinedPath = DecodeCubicPath(Refined, Token);
            ConfirmRefinement(RefinedPath.Anchors.size() == 4 && Workspace.QueryRevision() == Revision + 1, "Native subdivision did not finalize one new anchor.");
            ConfirmRefinement(Near(RefinedPath.Anchors[2].Position[0], Midpoint[0]) && Near(RefinedPath.Anchors[2].Position[1], Midpoint[1]), "Native subdivision did not preserve the exact cubic midpoint.");
            ConfirmRefinement(DecodeVectorAuthoring(EncodeVectorAuthoring(Refined)) == Refined, "Native subdivision TOML round trip failed.");
            Capture("NativePatternRefinement");
            Click("Undo"); ConfirmRefinement(Workspace.QueryDocument() == Before, "Native subdivision Undo failed.");
            Click("Redo"); ConfirmRefinement(Workspace.QueryDocument() == Refined, "Native subdivision Redo failed.");
            Workspace.ConfigureDocument(Source); Frame(); Frame(); Click("Select:1");
            ClickPoint(Grip("Path anchor 2")); Click("Subdivide following segment");
            ConfirmRefinement(DecodeCubicPath(Workspace.QueryDocument(), Token).Anchors.size() == 4, "Native closing-segment subdivision failed.");
            Path.Closed = false;
            VectorAuthoringDocument Open;
            ConstructCubicPath(Open, Path, {}, "Open endpoint");
            Workspace.ConfigureDocument(Open); Frame(); Frame(); Click("Select:1"); ClickPoint(Grip("Path anchor 2"));
            const auto OpenRevision = Workspace.QueryRevision();
            Click("Subdivide following segment");
            ConfirmRefinement(Workspace.QueryDocument() == Open && Workspace.QueryRevision() == OpenRevision, "Open endpoint offered a nonexistent following segment.");
            Workspace.ConfigureDocument({}); Frame(); Frame(); Key(ImGuiKey_P);
            ClickPoint(PointOnBoard(160, 160));
            Input.AddKeyEvent(ImGuiMod_Shift, true); Frame();
            ClickPoint(PointOnBoard(330, 220)); ClickPoint(PointOnBoard(280, 340)); Key(ImGuiKey_Enter);
            Input.AddKeyEvent(ImGuiMod_Shift, false); Frame();
            const auto Constrained = DecodeCubicPath(Workspace.QueryDocument(), 1);
            ConfirmRefinement(Near(Constrained.Anchors[1].Position[1], Constrained.Anchors[0].Position[1]), "Shift pen placement did not constrain to a 45-degree direction.");
            ConfirmRefinement(Constrained.Anchors[1].Position == Constrained.Anchors[1].Incoming
                && Constrained.Anchors[1].Position == Constrained.Anchors[1].Outgoing, "Constrained click accidentally created tangent controls.");
            Workspace.ConfigureDocument({}); Frame(); Frame();
            Input.AddKeyEvent(ImGuiMod_Shift, true); Frame();
            BeginDrag(PointOnBoard(160, 160)); MoveDrag(PointOnBoard(210, 180)); EndDrag();
            Input.AddKeyEvent(ImGuiMod_Shift, false); Frame();
            ClickPoint(PointOnBoard(320, 300)); Key(ImGuiKey_Enter);
            const auto Tangent = DecodeCubicPath(Workspace.QueryDocument(), 1).Anchors[0];
            ConfirmRefinement(Near(Tangent.Position[1], Tangent.Outgoing[1]), "Shift pen tangent did not constrain its angle.");
            Key(ImGuiKey_A); ClickPoint(Grip("Path anchor 0"));
            Input.AddKeyEvent(ImGuiMod_Shift, true); Frame();
            BeginDrag(Grip("Path outgoing 0")); MoveDrag(PointOnBoard(230, 220)); EndDrag();
            Input.AddKeyEvent(ImGuiMod_Shift, false); Frame();
            const auto PointTangent = DecodeCubicPath(Workspace.QueryDocument(), 1).Anchors[0];
            ConfirmRefinement(Near(std::abs(PointTangent.Outgoing[0] - PointTangent.Position[0]),
                std::abs(PointTangent.Outgoing[1] - PointTangent.Position[1])), "Shift Point tangent did not constrain its angle.");
            ConfirmRefinement(Workspace.QueryDiagnostic().empty(), "Refinement left a diagnostic.");
        }
        uint32_t ContinuationChecks = 0;
        const auto ConfirmContinuation = [&](bool Condition, const char *Caption) { AssertRasterCondition(Condition, Caption); ++ContinuationChecks; };
        for (bool FromStart : {false, true})
        {
            VectorAuthoringDocument Source;
            CubicPath Path;
            Path.Anchors = {{{170, 180}, {170, 180}, {210, 140}},
                {{280, 310}, {240, 270}, {280, 310}}};
            VectorCoverageInstruction Style;
            Style.Transform[1] = 3;
            Style.Colour = {.8f, .3f, .2f, 1};
            const auto Token = ConstructCubicPath(Source, Path, Style, "Retained open contour");
            auto Alias = Source.Layers[0];
            Alias.Token = ++Source.TokenClock; Alias.Name = "Independent path alias";
            Alias.Visible = false;
            Alias.Mask = Source.Instructions[Alias.Root];
            Source.Layers.push_back(Alias);
            const auto AliasPath = DecodeCubicPath(Source, Alias.Token);
            Workspace.ConfigureDocument(Source); Frame(); Frame(); Click("Select:1"); Key(ImGuiKey_A);
            const auto Revision = Workspace.QueryRevision();
            Click(FromStart ? "Resume from start" : "Resume from end");
            Key(ImGuiKey_Enter);
            ConfirmContinuation(Workspace.QueryDocument() == Source && Workspace.QueryRevision() == Revision, "Resuming without extending created source history.");
            Key(ImGuiKey_P);
            const auto Endpoint = Grip(FromStart ? "Resume start endpoint" : "Resume end endpoint");
            BeginDrag(Endpoint); MoveDrag(ImVec2(Endpoint.x + 10, Endpoint.y - 8)); EndDrag();
            ConfirmContinuation(Workspace.QueryDocument() == Source, "Endpoint resume mutated an existing segment before extension.");
            ClickPoint(PointOnBoard(FromStart ? 90.f : 380.f, FromStart ? 110.f : 360.f));
            ConfirmContinuation(Workspace.QueryDocument().Layers.size() == Source.Layers.size() && Workspace.QueryRevision() == Revision, "Continuation duplicated the layer or changed preview history.");
            const auto Preview = Workspace.QueryDocument();
            const auto PreviewPath = DecodeCubicPath(Preview, Token);
            const auto Original = DecodeCubicPath(Source, Token);
            const size_t Offset = FromStart ? 1 : 0;
            for (size_t Index = 0; Index < Original.Anchors.size(); ++Index)
            {
                const auto &Anchor = PreviewPath.Anchors[Index + Offset];
                ConfirmContinuation(Near(Anchor.Position[0], Original.Anchors[Index].Position[0])
                    && Near(Anchor.Position[1], Original.Anchors[Index].Position[1]), "Continuation moved an original anchor.");
            }
            ConfirmContinuation(PreviewPath.Anchors[Offset].Outgoing == Original.Anchors[0].Outgoing
                && PreviewPath.Anchors[Offset + 1].Incoming == Original.Anchors[1].Incoming, "Continuation rewrote an original segment control.");
            Key(ImGuiKey_Enter);
            const auto Extended = Workspace.QueryDocument();
            const auto &Layer = Extended.Layers[0];
            ConfirmContinuation(Workspace.QueryRevision() == Revision + 1 && Layer.Token == Token && Layer.Name == Source.Layers[0].Name, "Continuation lost source identity or atomic history.");
            const auto &ExtendedStyle = Extended.Instructions[Layer.Root];
            ConfirmContinuation(ExtendedStyle.Colour == Style.Colour && ExtendedStyle.Transform[1] == Style.Transform[1], "Continuation lost retained stroke/paint.");
            ConfirmContinuation(DecodeCubicPath(Extended, Alias.Token) == AliasPath && Extended.Layers[1].Mask == Source.Layers[1].Mask, "Continuation mutated an independent alias or owned mask descriptor.");
            ConfirmContinuation(DecodeVectorAuthoring(EncodeVectorAuthoring(Extended)) == Extended, "Continuation TOML round trip failed.");
            Capture("NativePatternContinuation");
            Click("Undo"); ConfirmContinuation(Workspace.QueryDocument() == Source, "Continuation Undo did not restore exact source.");
            Click("Redo"); ConfirmContinuation(Workspace.QueryDocument() == Extended, "Continuation Redo changed source.");
            Workspace.ConfigureDocument(Source); Frame(); Frame(); Click("Select:1"); Key(ImGuiKey_P);
            ClickPoint(Grip(FromStart ? "Resume start endpoint" : "Resume end endpoint"));
            ClickPoint(PointOnBoard(400, 200)); Key(ImGuiKey_Backspace); Key(ImGuiKey_Backspace);
            ConfirmContinuation(Workspace.QueryDocument() == Source, "Continuation Backspace removed an original anchor.");
            ClickPoint(Grip(FromStart ? "Resume start endpoint" : "Resume end endpoint"));
            ClickPoint(PointOnBoard(400, 200)); Key(ImGuiKey_Escape);
            ConfirmContinuation(Workspace.QueryDocument() == Source, "Continuation Escape changed original source.");
            ConfirmContinuation(Workspace.QueryDiagnostic().empty(), "Continuation left a diagnostic.");
            ClickPoint(Grip(FromStart ? "Resume start endpoint" : "Resume end endpoint"));
            ClickPoint(Grip("Pen anchor 0"));
            ConfirmContinuation(Workspace.QueryDocument() == Source && !Workspace.QueryDiagnostic().empty(), "Two-anchor continuation incorrectly closed or duplicated its first endpoint.");
            ClickPoint(PointOnBoard(400, 200)); ClickPoint(Grip("Pen anchor 0"));
            ConfirmContinuation(DecodeCubicPath(Workspace.QueryDocument(), Token).Closed, "Continuing a path could not close it at the opposite endpoint.");
            Click("Undo"); ConfirmContinuation(Workspace.QueryDocument() == Source, "Continuation closure Undo failed.");
            Workspace.ConfigureDocument(Source); Frame(); Frame(); Click("Select:1"); Key(ImGuiKey_P);
            ClickPoint(Grip(FromStart ? "Resume start endpoint" : "Resume end endpoint"));
            ClickPoint(PointOnBoard(400, 200)); Input.AddFocusEvent(false); Frame();
            Input.AddFocusEvent(true); Frame(); Frame();
            ConfirmContinuation(Workspace.QueryDocument() == Source, "Focus loss finalized a continuation.");
            Source.Layers[0].Locked = true;
            Workspace.ConfigureDocument(Source); Frame(); Frame(); Click("Select:1"); Key(ImGuiKey_A);
            Click("Resume from end"); Key(ImGuiKey_Enter);
            ConfirmContinuation(Workspace.QueryDocument() == Source, "Protected source offered an active continuation operation.");
        }
        uint32_t ClipboardChecks = 0;
        const auto ConfirmClipboard = [&](bool Condition, const char *Caption)
        {
            AssertRasterCondition(Condition, Caption); ++ClipboardChecks;
        };
        {
            VectorAuthoringDocument Source;
            VectorCoverageInstruction Shape;
            Shape.Bounds = {150, 170, 80, 65}; Shape.Colour = {.3f, .65f, .85f, 1};
            ConstructVectorAuthoringPrimitive(Source, Shape, "Masked rectangle");
            Shape.Topology[0] = 1; Shape.Bounds = {290, 230, 85, 60}; Shape.Colour = {.8f, .4f, .25f, .6f};
            ConstructVectorAuthoringPrimitive(Source, Shape, "Translucent ellipse");
            CubicPath Curve; Curve.Anchors = {{{130, 330}, {130, 330}, {200, 260}}, {{330, 360}, {260, 410}, {330, 360}}};
            Shape.Paint[3] = 0; Shape.Transform[1] = 5; Shape.Colour = {.5f, .8f, .4f, 1};
            ConstructCubicPath(Source, Curve, Shape, "Editable curve");
            Shape = {}; Shape.Bounds = {420, 390, 45, 45};
            ConstructVectorAuthoringPrimitive(Source, Shape, "Protected original"); Source.Layers.back().Locked = true;
            Source.Layers[0].Mask = VectorCoverageInstruction{}; Source.Layers[0].Mask->Topology[0] = 1;
            Source.Layers[0].Mask->Bounds = {150, 170, 85, 70}; Source.Layers[0].SurfaceChannels[1].Strength = .4f;
            Source.Enclosures = {{5, 0, "Motif"}, {6, 5, "Nested motif"}}; Source.TokenClock = 6;
            Source.Layers[0].EnclosureToken = Source.Layers[1].EnclosureToken = 6;
            Workspace.ConfigureDocument(Source); Frame(); Frame(); Click("Fit"); Key(ImGuiKey_V);
            const auto Shortcut = [&](ImGuiKey KeyCode, bool Shift = false, bool Super = false)
            {
                Input.AddKeyEvent(Super ? ImGuiMod_Super : ImGuiMod_Ctrl, true);
                if (Shift) Input.AddKeyEvent(ImGuiMod_Shift, true);
                Frame(); Key(KeyCode);
                Input.AddKeyEvent(Super ? ImGuiMod_Super : ImGuiMod_Ctrl, false);
                if (Shift) Input.AddKeyEvent(ImGuiMod_Shift, false);
                Frame();
            };
            const auto Revision = Workspace.QueryRevision();
            Shortcut(ImGuiKey_V);
            ConfirmClipboard(Workspace.QueryDocument() == Source && Workspace.QueryRevision() == Revision,
                "Empty clipboard paste changed native source.");
            Shortcut(ImGuiKey_A);
            ConfirmClipboard(Workspace.QuerySelection() == std::vector<uint32_t>({1, 2, 3}), "Select all included protected sources or lost visible sources.");
            const bool Movie = Count == 6 && std::string_view(Arguments[5]) == "--animate-clipboard";
            if (Movie) { Input.MouseDrawCursor = true; RecordAnimation = true; }
            Click("Document actions"); Frame(); Capture("NativePatternClipboardMenu"); Click("Document Copy selection");
            ConfirmClipboard(Workspace.QueryDocument() == Source && Workspace.QueryRevision() == Revision, "Copy created a native history edit.");
            const auto Fragment = ExtractVectorAuthoringSelection(Source, Workspace.QuerySelection());
            auto Expected = Source; const auto Inserted = IntegrateVectorAuthoringSelection(Expected, Fragment);
            Shortcut(ImGuiKey_V);
            ConfirmClipboard(Workspace.QueryDocument() == Expected && Workspace.QuerySelection() == Inserted,
                "Native paste lost retained sources, group identities, mask or channel settings.");
            ConfirmClipboard(Workspace.QueryRevision() == Revision + 1, "Paste did not finalize exactly one history edit.");
            Capture("NativePatternClipboard");
            if (Movie) for (int Index = 0; Index < 12; ++Index) Frame();
            Input.AddKeyEvent(ImGuiMod_Shift, true); Frame(); Key(ImGuiKey_LeftBracket);
            Input.AddKeyEvent(ImGuiMod_Shift, false); Frame();
            auto Reordered = Expected; ReorderVectorAuthoringSelection(Reordered, Inserted, VectorSelectionOrder::Back);
            ConfirmClipboard(Workspace.QueryDocument() == Reordered, "Shift bracket did not send copied selection to the back.");
            Shortcut(ImGuiKey_X);
            auto Cut = Reordered; PartitionVectorAuthoringSelection(Cut, Inserted);
            ConfirmClipboard(Workspace.QueryDocument() == Cut && Workspace.QuerySelection().empty(), "Cut did not delete only selected copies.");
            Click("Undo");
            ConfirmClipboard(Workspace.QueryDocument() == Reordered && Workspace.QuerySelection() == Inserted,
                "Cut Undo did not restore source and selected identities.");
            if (Movie) for (int Index = 0; Index < 10; ++Index) Frame();
            Click("Redo"); ConfirmClipboard(Workspace.QueryDocument() == Cut, "Cut Redo changed source.");
            const auto CutFragment = ExtractVectorAuthoringSelection(Reordered, Inserted);
            auto Restored = Cut; const auto RestoredTokens = IntegrateVectorAuthoringSelection(Restored, CutFragment, {0, 0});
            Shortcut(ImGuiKey_V, true);
            ConfirmClipboard(Workspace.QueryDocument() == Restored && Workspace.QuerySelection() == RestoredTokens,
                "Paste in place did not retain the cut fragment's exact source coordinates.");
            ConfirmClipboard(DecodeVectorAuthoring(EncodeVectorAuthoring(Restored)) == Restored, "Clipboard native source lost TOML round trip.");
            if (Movie)
            {
                for (int Index = 0; Index < 15; ++Index) Frame();
                RecordAnimation = false;
                std::ofstream Metadata(Destination / "MotionFrames.json");
                Metadata << "{\"frames\":" << AnimationOrdinal << ",\"width\":1440,\"height\":900,\"native_mouse_cursor\":true,\"native_pointer_input\":true,\"windows_executed\":false}\n";
                std::cout << AnimationOrdinal << " real native clipboard frames captured.\n";
                ImGui::DestroyContext(); return 0;
            }
            Workspace.ConfigureDocument(Source); Frame(); Frame(); Click("Select:4");
            const auto ProtectedRevision = Workspace.QueryRevision();
            Shortcut(ImGuiKey_X); Shortcut(ImGuiKey_D);
            ConfirmClipboard(Workspace.QueryDocument() == Source && Workspace.QueryRevision() == ProtectedRevision,
                "Cut or duplicate changed a protected layer.");
            VectorAuthoringDocument Blank; Workspace.ConfigureDocument(Blank); Frame(); Frame();
            auto CrossDocument = Blank; const auto CrossTokens = IntegrateVectorAuthoringSelection(CrossDocument, CutFragment, {0, 0});
            Shortcut(ImGuiKey_V, true, true);
            ConfirmClipboard(Workspace.QueryDocument() == CrossDocument && Workspace.QuerySelection() == CrossTokens,
                "Session clipboard did not survive document switching or Super paste-in-place.");
            const auto BeforeDuplicate = Workspace.QueryDocument(); const auto DuplicateSelection = Workspace.QuerySelection();
            auto Duplicated = BeforeDuplicate;
            const auto DuplicateTokens = IntegrateVectorAuthoringSelection(Duplicated, ExtractVectorAuthoringSelection(BeforeDuplicate, DuplicateSelection));
            Shortcut(ImGuiKey_D, false, true);
            ConfirmClipboard(Workspace.QueryDocument() == Duplicated && Workspace.QuerySelection() == DuplicateTokens,
                "Super duplicate did not reuse the shared independent copy operation.");
            Click("Undo"); ConfirmClipboard(Workspace.QueryDocument() == BeforeDuplicate, "Duplicate Undo failed.");
            const auto BeforeNoOp = Workspace.QueryRevision();
            Click("Document actions"); Click("Document Bring to front");
            ConfirmClipboard(Workspace.QueryRevision() == BeforeNoOp, "Boundary stacking command created a no-op history edit.");
        }
        uint32_t EndpointJoiningChecks = 0;
        const auto ConfirmEndpointJoining = [&](bool Condition, const char *Caption)
        {
            AssertRasterCondition(Condition, Caption); ++EndpointJoiningChecks;
        };
        for (int Pair = 0; Pair < 4; ++Pair) for (bool Extended : {false, true})
        {
            VectorAuthoringDocument Source;
            CubicPath First, Second;
            First.Anchors = {{{100, 180}, {100, 180}, {140, 130}}, {{220, 180}, {180, 130}, {220, 180}}};
            Second.Anchors = {{{320, 300}, {320, 300}, {350, 350}}, {{430, 300}, {390, 350}, {430, 300}}};
            VectorCoverageInstruction Style; Style.Transform[1] = 4; Style.Paint[3] = 0; Style.Colour = {.3f, .7f, .9f, 1};
            const auto Retained = ConstructCubicPath(Source, First, Style, "Continue this path");
            const auto Consumed = ConstructCubicPath(Source, Second, Style, "Join this path");
            Workspace.ConfigureDocument(Source); Frame(); Frame(); Click("Fit"); Key(ImGuiKey_P);
            const bool Movie = Count == 6 && std::string_view(Arguments[5]) == "--animate-endpoint-join" && Pair == 0 && Extended;
            if (Movie) { Input.MouseDrawCursor = true; RecordAnimation = true; }
            const auto MovePointer = [&](ImVec2 Destination)
            {
                if (!Movie) return;
                const auto Origin = Input.MousePos;
                for (int Index = 1; Index <= 8; ++Index)
                {
                    const float Parameter = float(Index) / 8;
                    Input.AddMousePosEvent(Origin.x + (Destination.x - Origin.x) * Parameter,
                        Origin.y + (Destination.y - Origin.y) * Parameter);
                    Frame();
                }
            };
            Click("Select:1");
            const auto Revision = Workspace.QueryRevision();
            const auto Resume = Grip(Pair >= 2 ? "Resume start endpoint" : "Resume end endpoint");
            MovePointer(Resume); ClickPoint(Resume);
            ConfirmEndpointJoining(Workspace.QueryDocument() == Source && Workspace.QueryRevision() == Revision,
                "Endpoint join resume mutated source or history.");
            if (Extended)
            {
                const auto Extension = PointOnBoard(260, 250); MovePointer(Extension); ClickPoint(Extension);
            }
            const auto BeforeJoin = Workspace.QueryDocument();
            auto Expected = BeforeJoin;
            IntegrateCubicPaths(Expected, Retained, Consumed, Pair >= 2, Pair % 2 != 0);
            const auto Endpoint = Grip(Pair % 2 ? "Join end endpoint:2" : "Join start endpoint:2");
            MovePointer(Endpoint); ClickPoint(Endpoint);
            const auto Joined = Workspace.QueryDocument();
            ConfirmEndpointJoining(Joined == Expected, "Pen endpoint join differed from shared cubic operation.");
            ConfirmEndpointJoining(Workspace.QueryRevision() == Revision + 1 && Workspace.QuerySelection() == std::vector<uint32_t>{Retained},
                "Pen endpoint join did not finalize once with retained selection.");
            ConfirmEndpointJoining(DecodeVectorAuthoring(EncodeVectorAuthoring(Joined)) == Joined,
                "Pen endpoint join source did not round trip.");
            if (Pair == 0 && Extended) Capture("NativePatternEndpointJoin");
            if (Movie) for (int Index = 0; Index < 12; ++Index) Frame();
            Click("Undo");
            ConfirmEndpointJoining(Workspace.QueryDocument() == Source && Workspace.QuerySelection() == std::vector<uint32_t>{Retained},
                "Pen endpoint join Undo failed to restore original source and selection.");
            if (Movie) for (int Index = 0; Index < 8; ++Index) Frame();
            Click("Redo"); ConfirmEndpointJoining(Workspace.QueryDocument() == Joined, "Pen endpoint join Redo changed source.");
            Key(ImGuiKey_Enter);
            ConfirmEndpointJoining(Workspace.QueryDocument() == Joined && Workspace.QueryRevision() == Revision + 3,
                "Pen endpoint join left a pending continuation or duplicate history edit.");
            if (Movie)
            {
                for (int Index = 0; Index < 12; ++Index) Frame();
                RecordAnimation = false;
                std::ofstream Metadata(Destination / "MotionFrames.json");
                Metadata << "{\"frames\":" << AnimationOrdinal << ",\"width\":1440,\"height\":900,\"native_mouse_cursor\":true,\"native_pointer_input\":true,\"windows_executed\":false}\n";
                std::cout << AnimationOrdinal << " real native endpoint-join frames captured.\n";
                ImGui::DestroyContext(); return 0;
            }
            Workspace.ConfigureDocument(Source); Frame(); Frame(); Key(ImGuiKey_P); Click("Select:1");
            ClickPoint(Grip(Pair >= 2 ? "Resume start endpoint" : "Resume end endpoint"));
            const auto Target = Grip(Pair % 2 ? "Join end endpoint:2" : "Join start endpoint:2");
            const auto BypassRevision = Workspace.QueryRevision();
            Input.AddKeyEvent(ImGuiMod_Ctrl, true); Frame(); ClickPoint(Target);
            Input.AddKeyEvent(ImGuiMod_Ctrl, false); Frame();
            ConfirmEndpointJoining(Workspace.QueryDocument().Layers.size() == 2 && Workspace.QueryRevision() == BypassRevision,
                "Ctrl-click did not bypass endpoint join.");
            Key(ImGuiKey_Escape);
            ConfirmEndpointJoining(Workspace.QueryDocument() == Source, "Cancelled Ctrl-bypassed endpoint join changed retained source.");
        }
        for (int Exclusion = 0; Exclusion < 3; ++Exclusion)
        {
            VectorAuthoringDocument Source;
            CubicPath First, Second;
            First.Anchors = {{{100, 180}, {100, 180}, {140, 130}}, {{220, 180}, {180, 130}, {220, 180}}};
            Second.Anchors = {{{320, 300}, {320, 300}, {350, 350}}, {{430, 300}, {390, 350}, {430, 300}},
                {{380, 400}, {380, 400}, {380, 400}}};
            Second.Closed = Exclusion == 1;
            VectorCoverageInstruction Style; Style.Transform[1] = 4; Style.Paint[3] = 0;
            ConstructCubicPath(Source, First, Style, "Retained path");
            ConstructCubicPath(Source, Second, Style, "Excluded target");
            if (Exclusion == 0) Source.Layers[1].Visible = false;
            if (Exclusion == 2)
            {
                const auto Start = Source.Instructions[Source.Layers[1].Root].Topology[1];
                Source.Segments[Start + 1].StartControl[0] += .1f;
            }
            Workspace.ConfigureDocument(Source); Frame(); Frame(); Key(ImGuiKey_P); Click("Select:1");
            ClickPoint(Grip("Resume end endpoint"));
            ConfirmEndpointJoining(Workspace.QueryControlExtent("Join start endpoint:2").Width() == 0,
                "Hidden, closed or compound path exposed a join target.");
            ConfirmEndpointJoining(Workspace.QueryDocument() == Source, "Enumerating excluded join targets changed source.");
            Key(ImGuiKey_Escape);
        }
        {
            VectorAuthoringDocument Source;
            CubicPath First, Second;
            First.Anchors = {{{100, 180}, {100, 180}, {140, 130}}, {{220, 180}, {180, 130}, {220, 180}}};
            Second.Anchors = {{{220, 180}, {220, 180}, {320, 260}}, {{420, 300}, {380, 350}, {420, 300}}};
            VectorCoverageInstruction Style; Style.Transform[1] = 4; Style.Paint[3] = 0;
            ConstructCubicPath(Source, First, Style, "Coincident retained");
            ConstructCubicPath(Source, Second, Style, "Coincident target");
            auto Expected = Source; IntegrateCubicPaths(Expected, 1, 2);
            Workspace.ConfigureDocument(Source); Frame(); Frame(); Key(ImGuiKey_P); Click("Select:1");
            const auto Revision = Workspace.QueryRevision();
            ClickPoint(Grip("Resume end endpoint")); ClickPoint(Grip("Join start endpoint:2"));
            ConfirmEndpointJoining(Workspace.QueryDocument() == Expected && Workspace.QueryRevision() == Revision + 1,
                "Coincident endpoint click did not merge with the shared cubic operation.");
            ConfirmEndpointJoining(DecodeCubicPath(Workspace.QueryDocument(), 1).Anchors.size() == 3,
                "Coincident endpoint join duplicated its shared anchor.");
            Click("Undo"); ConfirmEndpointJoining(Workspace.QueryDocument() == Source, "Coincident endpoint join Undo changed source.");
        }
        for (int Refusal = 0; Refusal < 7; ++Refusal)
        {
            VectorAuthoringDocument Source;
            CubicPath First, Second;
            First.Anchors = {{{100, 180}, {100, 180}, {140, 130}}, {{220, 180}, {180, 130}, {220, 180}}};
            Second.Anchors = {{{320, 300}, {320, 300}, {350, 350}}, {{430, 300}, {390, 350}, {430, 300}}};
            VectorCoverageInstruction Style; Style.Transform[1] = 4; Style.Paint[3] = 0;
            ConstructCubicPath(Source, First, Style, "Retained path");
            const auto Consumed = ConstructCubicPath(Source, Second, Style, "Unavailable join");
            if (Refusal == 0) Source.Instructions[Source.Layers[1].Root].Colour[3] = .5f;
            if (Refusal == 1) Source.Instructions[Source.Layers[1].Root].Transform[3] = 1;
            if (Refusal == 2) Source.Layers[1].Mask = VectorCoverageInstruction{};
            if (Refusal == 3) Source.Layers[1].Locked = true;
            if (Refusal == 4) Source.Layers[1].SurfaceChannels[0].Strength = .5f;
            if (Refusal == 5)
            {
                ConstructCubicPath(Source, First, Style, "Intervening path");
                std::swap(Source.Layers[1], Source.Layers[2]);
            }
            if (Refusal == 6) Source.Instructions[Source.Layers[1].Root].Transform[1] = 9;
            Workspace.ConfigureDocument(Source); Frame(); Frame(); Key(ImGuiKey_P); Click("Select:1");
            ClickPoint(Grip("Resume end endpoint")); ClickPoint(PointOnBoard(260, 240));
            const auto Pending = Workspace.QueryDocument(); const auto Revision = Workspace.QueryRevision();
            ClickPoint(Grip(("Join start endpoint:" + std::to_string(Consumed)).c_str()));
            ConfirmEndpointJoining(Workspace.QueryDocument() == Pending && Workspace.QueryRevision() == Revision,
                "Refused endpoint join changed pending source or history.");
            ConfirmEndpointJoining(!Workspace.QueryDiagnostic().empty(), "Refused endpoint join omitted its diagnostic.");
            Key(ImGuiKey_Escape);
            ConfirmEndpointJoining(Workspace.QueryDocument() == Source, "Refused join Escape did not restore exact retained source.");
        }
        uint32_t JoiningChecks = 0;
        const auto ConfirmJoining = [&](bool Condition, const char *Caption) { AssertRasterCondition(Condition, Caption); ++JoiningChecks; };
        constexpr const char *JoinCaptions[]{"Join end to start", "Join end to end", "Join start to start", "Join start to end"};
        for (int Pair = 0; Pair < 4; ++Pair)
        {
            VectorAuthoringDocument Source;
            CubicPath First, Second;
            First.Anchors = {{{100, 180}, {100, 180}, {140, 130}}, {{220, 180}, {180, 130}, {220, 180}}};
            Second.Anchors = {{{300, 300}, {300, 300}, {340, 350}}, {{420, 300}, {380, 350}, {420, 300}}};
            VectorCoverageInstruction Style; Style.Transform[1] = 4; Style.Colour = {.8f, .3f, .2f, 1};
            const auto Retained = ConstructCubicPath(Source, First, Style, "Retained join contour");
            const auto Consumed = ConstructCubicPath(Source, Second, Style, "Consumed join contour");
            const auto SelectPair = [&]()
            {
                Click("Select:1"); Input.AddKeyEvent(ImGuiMod_Shift, true); Frame(); Click("Select:2");
                Input.AddKeyEvent(ImGuiMod_Shift, false); Frame();
            };
            Workspace.ConfigureDocument(Source); Frame(); Frame(); Key(ImGuiKey_V); SelectPair();
            const auto Revision = Workspace.QueryRevision();
            const auto A = Pair >= 2 ? First.Anchors.front().Position : First.Anchors.back().Position;
            const auto B = Pair % 2 ? Second.Anchors.back().Position : Second.Anchors.front().Position;
            const size_t Pixel = size_t(uint32_t((A[1] + B[1]) * .5f) * 512 + uint32_t((A[0] + B[0]) * .5f)) * 4;
            ConfirmJoining(Coverage[Pixel + 3] == 0, "Join bridge fixture already covers its gap.");
            Click(JoinCaptions[Pair]);
            const auto Joined = Workspace.QueryDocument();
            ConfirmJoining(Joined.Layers.size() == 1 && Joined.Layers[0].Token == Retained && Workspace.QueryRevision() == Revision + 1,
                "Native endpoint choice did not finalize one retained join operation.");
            ConfirmJoining(Workspace.QuerySelection() == std::vector<uint32_t>{Retained}, "Native join did not remove the consumed token from selection.");
            ConfirmJoining(DecodeCubicPath(Joined, Retained).Anchors.size() == 4, "Native bridge join lost an original anchor.");
            ConfirmJoining(Coverage[Pixel + 3] > 200, "Native join did not create actual Vulkan bridge pixels.");
            ConfirmJoining(DecodeVectorAuthoring(EncodeVectorAuthoring(Joined)) == Joined, "Joined native source did not round trip TOML.");
            Capture("NativePatternJoining");
            Click("Undo"); ConfirmJoining(Workspace.QueryDocument() == Source && Workspace.QuerySelection().size() == 2, "Native join Undo did not restore both sources/selection.");
            Click("Redo"); ConfirmJoining(Workspace.QueryDocument() == Joined, "Native join Redo changed source.");
            for (int Refusal = 0; Refusal < 3; ++Refusal)
            {
                auto Incompatible = Source;
                if (Refusal == 0) Incompatible.Instructions[Incompatible.Layers[1].Root].Colour[0] = .2f;
                if (Refusal == 1) Incompatible.Layers[1].Mask = Incompatible.Instructions[Incompatible.Layers[1].Root];
                if (Refusal == 2) Incompatible.Layers[1].Locked = true;
                Workspace.ConfigureDocument(Incompatible); Frame(); Frame(); SelectPair();
                const auto BeforeRefusal = Workspace.QueryRevision(); Click(JoinCaptions[Pair]);
                ConfirmJoining(Workspace.QueryDocument() == Incompatible && Workspace.QueryRevision() == BeforeRefusal, "Unavailable native join changed incompatible/protected source.");
            }
            ConfirmJoining(Workspace.QueryDiagnostic().empty(), "Native joining left a diagnostic.");
            (void)Consumed;
        }
        {
            VectorAuthoringDocument Source;
            CubicPath First, Second;
            First.Anchors = {{{100, 180}, {100, 180}, {140, 130}}, {{220, 180}, {180, 130}, {220, 180}}};
            Second.Anchors = {{{220, 180}, {220, 180}, {260, 230}}, {{420, 300}, {380, 350}, {420, 300}}};
            VectorCoverageInstruction Style; Style.Transform[1] = 4;
            ConstructCubicPath(Source, First, Style, "Coincident retained contour");
            ConstructCubicPath(Source, Second, Style, "Coincident consumed contour");
            Workspace.ConfigureDocument(Source); Frame(); Frame(); Key(ImGuiKey_V);
            Click("Select:1"); Input.AddKeyEvent(ImGuiMod_Shift, true); Frame(); Click("Select:2"); Input.AddKeyEvent(ImGuiMod_Shift, false); Frame();
            Click("Join end to start");
            const auto Joined = DecodeCubicPath(Workspace.QueryDocument(), 1);
            ConfirmJoining(Joined.Anchors.size() == 3 && Workspace.QueryDocument().Layers.size() == 1, "Native coincident join duplicated its shared endpoint.");
            ConfirmJoining(Joined.Anchors[1].Incoming == First.Anchors[1].Incoming && Joined.Anchors[1].Outgoing == Second.Anchors[0].Outgoing,
                "Native coincident join did not retain both meaningful tangent controls.");
            ConfirmJoining(Coverage[size_t(180 * 512 + 220) * 4 + 3] > 200, "Coincident native join lost Vulkan stroke coverage.");
            Click("Undo"); ConfirmJoining(Workspace.QueryDocument() == Source, "Coincident native join Undo failed.");
        }
        Checks += SurfaceControlChecks;
        std::ofstream Report(Destination / "NativePatternWorkspaceChecks.json");
        Report << "{\"result\":\"passed\",\"checks\":" << Checks << ",\"placement_checks\":" << PlacementChecks << ",\"corner_checks\":" << CornerChecks << ",\"endpoint_checks\":" << EndpointChecks << ",\"dropdown_checks\":" << DropdownChecks << ",\"multipoint_checks\":" << MultipointChecks << ",\"segment_checks\":" << SegmentChecks << ",\"clipboard_checks\":" << ClipboardChecks << ",\"endpoint_joining_checks\":" << EndpointJoiningChecks << ",\"joining_checks\":" << JoiningChecks << ",\"continuation_checks\":" << ContinuationChecks << ",\"refinement_checks\":" << RefinementChecks << ",\"contour_checks\":" << ContourChecks << ",\"surface_control_checks\":" << SurfaceControlChecks << ",\"pen_checks\":" << PenChecks << ",\"point_checks\":" << PointChecks << ",\"picking_checks\":" << PickingChecks << ",\"gesture_checks\":" << GestureChecks << ",\"source_checks\":" << SourceChecks << ",\"dock_checks\":" << DockChecks << ",\"backend_checks\":" << BackendChecks << ",\"zoom_resolution_checks\":" << ZoomResolutionChecks << ",\"gpu_dispatches\":" << Raster.QueryDispatchCount() << ",\"selection_dispatches\":" << SelectionEvaluation.QueryDispatchCount() << ",\"submission_serial\":" << Serial
               << ",\"width\":1440,\"height\":900,\"windows_executed\":false,\"full_parity_complete\":false,\"stamp_started\":false}\n";
        std::cout << Checks << " native Pattern UI/input/GPU checks passed.\n";
        ImGui::DestroyContext();
        return 0;
    }
    catch (const std::exception &Failure)
    {
        if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
        std::cerr << Failure.what() << '\n';
        return 1;
    }
}
