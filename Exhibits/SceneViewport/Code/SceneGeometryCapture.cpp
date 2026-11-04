/*==========================================================================================================================================
                                                         SCENEGEOMETRYCAPTURE.CPP
==========================================================================================================================================*/
// 🧩 The level's scene objects in the real Everest workspace, offscreen: the Outliner with the geometry groups, the geometry
//    inspector page of chosen records, and the viewport before and after edits made through the records.

#include "Engine/Interface/IconPresentation.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "EverestSceneViewport.h"
#include "EverestWorkspace.h"
#include "Exhibits/ControlCentre/Code/CpuRasterization.h"
#include "UI/Editor/ControlCentrePanel.h"

#include <imgui.h>
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

constexpr int      CaptureWidth  = 1920;
constexpr int      CaptureHeight = 1080;
constexpr uint64_t ViewportTag   = 0x5CE7E5u;   // [-] ImTextureID the capture resolves to the read-back image

void Require(bool Condition, const std::string& Message)
{
    if (!Condition) throw std::runtime_error(Message);
}

Frontier::GeometryRecord* FindGeometry(Frontier::SceneWorld& World, const std::string& Name)
{
    for (const Frontier::SceneRecordBase* Entry : World.FindAllByType("Geometry"))
        if (Entry->QueryRecordName() == Name) return dynamic_cast<Frontier::GeometryRecord*>(World.FindRecord(Entry->QueryToken()));
    throw std::runtime_error("no Geometry record named " + Name);
}

std::string Slug(const std::string& Name)
{
    std::string Text;
    for (const char Letter : Name)
        if (std::isalnum(static_cast<unsigned char>(Letter))) Text += Letter;
    return Text;
}

} // namespace

int main(int ArgumentCount, char** Arguments)
{
    using namespace Frontier;
    try
    {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        Require(ArgumentCount >= 5, "usage: SceneGeometryCapture <EngineContent> <Projects/Everest> <ShaderRoot> <OutDir> [GI refreshes] "
                                    "[gi|analytic|distance|virtualized] [refreshes after the edits]");
        const std::filesystem::path ContentDir  = std::filesystem::absolute(Arguments[1]);
        const std::filesystem::path EverestRoot = std::filesystem::absolute(Arguments[2]);
        const std::filesystem::path ShaderRoot  = std::filesystem::absolute(Arguments[3]);
        const std::filesystem::path OutputDir   = std::filesystem::absolute(Arguments[4]);
        const int                   Refreshes   = ArgumentCount > 5 ? std::atoi(Arguments[5]) : 6;
        // 📝 gi: the RTX path tracer (default); analytic: debug view 15; distance: the GTX distance-field GI; virtualized: RTX with
        //    the virtualized geometry clusters.
        const std::string           Mode         = ArgumentCount > 6 ? std::string(Arguments[6]) : std::string("gi");
        const bool                  AnalyticOnly = Mode == "analytic";
        const int                   AfterEdits   = ArgumentCount > 7 ? std::atoi(Arguments[7]) : std::max(2, Refreshes / 2);
        Require(Mode == "gi" || Mode == "analytic" || Mode == "distance" || Mode == "virtualized", "unknown mode " + Mode);
        std::filesystem::create_directories(OutputDir);

        ImGui::CreateContext();
        ImGuiIO& Io    = ImGui::GetIO();
        Io.DisplaySize = ImVec2(static_cast<float>(CaptureWidth), static_cast<float>(CaptureHeight));
        Io.DeltaTime   = 1.0f / 30.0f;
        Io.IniFilename = nullptr;
        Io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
        ImGui::GetPlatformIO().Renderer_TextureMaxWidth  = 4096;
        ImGui::GetPlatformIO().Renderer_TextureMaxHeight = 4096;
        IconPresentation::Attach(ContentDir / "Icons");
        const std::filesystem::path Preferences = OutputDir / "Personal.projectspace";
        std::filesystem::remove(Preferences);
        ActivationOwnership Ownership(new ControlCentrePanel(ContentDir / "FontArchives", Preferences, EverestRoot / "Everest.projectspace"));
        auto* Centre = static_cast<ControlCentrePanel*>(Ownership.get());
        Require(Centre->Initialize() && Centre->Activate(), "ControlCentrePanel initialization failed");

        // 📝 The bridge the editor uses, on a device of its own; the presented image comes back to the CPU.
        Everest::EverestWorkspace     Workspace(*Centre, EverestRoot);
        Everest::EverestSceneViewport Bridge(EverestRoot, ShaderRoot, nullptr);
        std::vector<uint8_t>          ViewportPixels;
        std::array<uint32_t, 2>       ViewportExtent = {0, 0};
        std::string                   LastStatus;
        bool                          Frozen = false;   // [-] interface-only ticks: the last image stays, nothing renders
        Everest::SceneViewportImage   LastImage;
        Workspace.PresentSceneViewport = [&](const Everest::SceneViewportRequest& Request) {
            if (Frozen && LastImage.Texture != 0) return LastImage;
            const auto Start = std::chrono::steady_clock::now();
            const Everest::SceneViewportRefresh Outcome = Bridge.Render(Request, Workspace.QuerySceneWorld(), Workspace.QueryElapsedSeconds());
            Everest::SceneViewportImage Image;
            Image.Status  = Outcome.Status;
            Image.Readout = Outcome.Readout;
            LastStatus    = Outcome.Status;
            if (Outcome.View != VK_NULL_HANDLE && Bridge.AccessViewport()->ReadPresented(ViewportPixels))
            {
                ViewportExtent = Outcome.Extent;
                Image.Texture  = ViewportTag;
            }
            std::printf("  viewport %u x %u in %.1f s — %s\n", Outcome.Extent[0], Outcome.Extent[1],
                        std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count(), Outcome.Status.c_str());
            LastImage = Image;
            return Image;
        };
        SceneWorld& World    = Workspace.QuerySceneWorld();
        auto*       Pipeline = dynamic_cast<RenderPipelineRecord*>(World.FindFirstByType("RenderPipeline"));
        Require(Pipeline != nullptr, "no Render settings record");
        Pipeline->AccessConfiguration().AnalyticIllumination = AnalyticOnly;
        Pipeline->AccessConfiguration().HardwareRayQueryTier = Mode != "distance";
        Pipeline->AccessConfiguration().VirtualizedGeometry  = Mode == "virtualized";
        Pipeline->AccessConfiguration().AdvanceRevision();

        const auto Tick = [&](int Count) {
            for (int Index = 0; Index < Count; ++Index)
            {
                Centre->PrepareInterface();
                ImGui::NewFrame();
                (void)Centre->Integrate(1.0 / 30.0);
                (void)Workspace.ConfigurePlatform();
                Workspace.ConstructInterface(1.0 / 30.0);
                Centre->ConstructInterface();
                ImGui::Render();
                FrontierExhibit::AcknowledgeTextures();
            }
        };
        const auto WriteWorkspace = [&](const std::filesystem::path& Target) {
            const ImDrawData*          DrawData = ImGui::GetDrawData();
            std::vector<unsigned char> Pixels(static_cast<size_t>(CaptureWidth) * CaptureHeight * 3u, 14u);
            const std::function<FrontierExhibit::Image(ImTextureID)> Resolve = [&](ImTextureID Texture) {
                return Texture == ViewportTag ? FrontierExhibit::Image{ViewportPixels.data(), static_cast<int>(ViewportExtent[0]), static_cast<int>(ViewportExtent[1]), 4}
                                              : FrontierExhibit::Image{};
            };
            for (const ImDrawList* List : DrawData->CmdLists)
                FrontierExhibit::RasterizeCommands(List, Pixels.data(), CaptureWidth, CaptureHeight, DrawData->DisplayPos, DrawData->FramebufferScale, nullptr,
                                                   &Resolve);
            Require(stbi_write_png(Target.string().c_str(), CaptureWidth, CaptureHeight, 3, Pixels.data(), CaptureWidth * 3) != 0, "PNG write failed");
            std::printf("[SceneGeometryCapture] %s\n", Target.string().c_str());
        };
        const auto WriteViewport = [&](const std::filesystem::path& Target) {
            Require(!ViewportPixels.empty(), "no viewport image: " + LastStatus);
            Require(stbi_write_png(Target.string().c_str(), static_cast<int>(ViewportExtent[0]), static_cast<int>(ViewportExtent[1]), 4, ViewportPixels.data(),
                                   static_cast<int>(ViewportExtent[0]) * 4) != 0,
                    "PNG write failed");
            std::printf("[SceneGeometryCapture] %s (%u x %u)\n", Target.string().c_str(), ViewportExtent[0], ViewportExtent[1]);
            return ViewportPixels;
        };
        // 📝 One record's page in a column tall enough for all of it, cropped under its last row (as SceneViewportWorkspaceCapture).
        const auto WritePage = [&](const std::string& Name) {
            const RecordToken Token = FindGeometry(World, Name)->QueryToken();
            Workspace.QueryOutliner().Select(Token);
            constexpr int ColumnWidth = 470, ColumnHeight = 3200;
            for (int Settle = 0; Settle < 3; ++Settle)
            {
                Io.DisplaySize = ImVec2(static_cast<float>(ColumnWidth), static_cast<float>(ColumnHeight));
                Centre->PrepareInterface();
                ImGui::NewFrame();
                ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
                ImGui::SetNextWindowSize(ImVec2(static_cast<float>(ColumnWidth), static_cast<float>(ColumnHeight)));
                ImGui::Begin("##GeometryPageCapture", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
                bool DeleteRequested = false;
                InspectorRuntime::Instance().InspectRecord(Token, DeleteRequested);
                ImGui::End();
                ImGui::Render();
                FrontierExhibit::AcknowledgeTextures();
            }
            Io.DisplaySize = ImVec2(static_cast<float>(CaptureWidth), static_cast<float>(CaptureHeight));
            const ImDrawData*          DrawData = ImGui::GetDrawData();
            std::vector<unsigned char> Pixels(static_cast<size_t>(ColumnWidth) * ColumnHeight * 3u, 14u);
            for (const ImDrawList* List : DrawData->CmdLists)
                FrontierExhibit::RasterizeCommands(List, Pixels.data(), ColumnWidth, ColumnHeight, DrawData->DisplayPos, DrawData->FramebufferScale, nullptr, nullptr);
            const int  Reference  = ColumnHeight - 12;
            const auto RowDiffers = [&](int Row) {
                for (int Column = 24 * 3; Column < (ColumnWidth - 24) * 3; ++Column)
                    if (std::abs(int(Pixels[static_cast<size_t>(Row) * ColumnWidth * 3 + Column]) - int(Pixels[static_cast<size_t>(Reference) * ColumnWidth * 3 + Column])) > 6)
                        return true;
                return false;
            };
            int Used = Reference;
            while (Used > 0 && !RowDiffers(Used)) --Used;
            Used = std::min(ColumnHeight, Used + 24);
            const std::filesystem::path Target = OutputDir / ("Inspector-" + Slug(Name) + ".png");
            Require(stbi_write_png(Target.string().c_str(), ColumnWidth, Used, 3, Pixels.data(), ColumnWidth * 3) != 0, "PNG write failed");
            std::printf("[SceneGeometryCapture] %s (%d px)\n", Target.string().c_str(), Used);
        };

        // ① the workspace with an array object selected, after the refreshes
        Workspace.QueryOutliner().Select(FindGeometry(World, "Boundary west")->QueryToken());
        Tick(Refreshes);
        Require(!ViewportPixels.empty(), "no scene viewport image was presented: " + LastStatus);
        WriteWorkspace(OutputDir / "Workspace.png");
        const std::vector<uint8_t> Before = WriteViewport(OutputDir / "Viewport-Before.png");

        // ② the geometry inspector page of five kinds of object
        for (const char* Name : {"Boundary west", "Skidpad ring", "Street lamps", "Wooden crates west", "Control tower"}) WritePage(Name);

        // ③ edits through the records and the editor's tools: move and turn the control tower and stop its shadow, re-lay the
        //    burnout tyre wall, lengthen the tunnel past the archive's eight segments (the reserve lays out the rest), hide the
        //    street lamps, remove the jump ramp, duplicate the control tower, construct three engine shapes (a red cube of its own)
        auto& Tower = FindGeometry(World, "Control tower")->AccessConfiguration();
        Tower.Spatial.PositionMetres[1] += 9.0f;
        Tower.Spatial.OrientationDegrees[2] = 30.0f;
        Tower.CastShadows                   = false;
        Tower.AdvanceRevision();
        auto& Tyres = FindGeometry(World, "Burnout tyre wall")->AccessConfiguration();
        Tyres.Modifiers.front().ConstantMetres[0] = 1.6f;
        Tyres.Modifiers.front().Count             = 8;
        Tyres.AdvanceRevision();
        auto& Tunnel = FindGeometry(World, "Tunnel")->AccessConfiguration();
        Tunnel.Modifiers.front().Count = 12;
        Tunnel.AdvanceRevision();
        auto& Lamps = FindGeometry(World, "Street lamps")->AccessConfiguration();
        Lamps.Visible = false;
        Lamps.AdvanceRevision();
        Require(World.DestroyRecord(FindGeometry(World, "Jump ramp")->QueryToken()), "the jump ramp was not removed");
        const RecordToken TowerCopy = Workspace.DuplicateGeometryRecord(FindGeometry(World, "Control tower")->QueryToken());
        Require(TowerCopy.IsValid(), "the control tower did not duplicate");
        auto& Copy = dynamic_cast<GeometryRecord*>(World.FindRecord(TowerCopy))->AccessConfiguration();
        Copy.Spatial.PositionMetres[0] = Tower.Spatial.PositionMetres[0] + 16.0f;
        Copy.Spatial.PositionMetres[1] = Tower.Spatial.PositionMetres[1];
        Copy.AdvanceRevision();
        const auto Construct = [&](const char* Shape, float Size, float X, float Y, std::optional<std::array<float, 3>> Albedo) {
            const auto& Catalogue = Workspace.QueryConstructDialog().QueryCatalogue();
            const auto  Entry     = std::find_if(Catalogue.begin(), Catalogue.end(), [&](const ConstructCatalogueEntry& Candidate) {
                return Candidate.Shape && Candidate.Identifier == Shape;
            });
            Require(Entry != Catalogue.end(), std::string("no engine shape ") + Shape + " in the Construct catalogue");
            ConstructOrder Order;
            Order.Entry    = *Entry;
            Order.Name     = Entry->Caption;
            Order.Size     = Size;
            Order.Albedo   = Albedo.value_or(Entry->Albedo);
            Order.Surfaced = Albedo.has_value();
            Workspace.ServiceConstructOrder(Order);
            auto& Made = dynamic_cast<GeometryRecord*>(World.FindRecord(Workspace.QueryOutliner().QuerySelected()))->AccessConfiguration();
            Made.Spatial.PositionMetres[0]     = X;
            Made.Spatial.PositionMetres[1]     = Y;
            Made.Spatial.OrientationDegrees[2] = 20.0f;
            Made.AdvanceRevision();
            return Workspace.QueryOutliner().QuerySelected();
        };
        const RecordToken Cube  = Construct("Cube", 3.0f, -44.0f, -21.0f, std::array<float, 3>{0.62f, 0.05f, 0.04f});
        const RecordToken Ball  = Construct("Sphere", 2.0f, -39.0f, -24.0f, std::nullopt);
        const RecordToken Torus = Construct("Torus", 3.0f, -48.0f, -26.0f, std::nullopt);
        // 📝 The cube selected: its move handles over the viewport.
        Workspace.QueryOutliner().Select(Cube);
        Tick(AfterEdits);
        WriteWorkspace(OutputDir / "Workspace-Edited.png");
        const std::vector<uint8_t> After = WriteViewport(OutputDir / "Viewport-After.png");
        // 📝 The Construct dialog (Ctrl+A) over the workspace, on interface-only ticks.
        Frozen = true;
        Workspace.QueryConstructDialog().Open();
        Tick(2);
        WriteWorkspace(OutputDir / "Workspace-Construct.png");
        Workspace.QueryConstructDialog().Close();
        Tick(1);
        Frozen = false;
        WritePage("Burnout tyre wall");
        WritePage("Tunnel");
        WritePage("Cube");

        // 📝 What the edits resolved to.
        const auto Resolved = [&](RecordToken Token) { return dynamic_cast<GeometryRecord*>(World.FindRecord(Token))->QueryConfiguration().Resolved; };
        const auto TunnelResolved = FindGeometry(World, "Tunnel")->QueryConfiguration().Resolved;
        std::printf("[SceneGeometryCapture] tunnel %u of %u copies shown, %u from the reserve (%u reserve occupants free)\n", TunnelResolved.Shown,
                    TunnelResolved.Copies, TunnelResolved.Reserved, TunnelResolved.ReserveFree);
        Require(TunnelResolved.Copies == 12 && TunnelResolved.Shown == 12 && TunnelResolved.Reserved == 4, "the tunnel did not lay out 12 copies");
        const auto CopyResolved = Resolved(TowerCopy);
        std::printf("[SceneGeometryCapture] control tower copy: bound %d, %u parts, %u shown\n", CopyResolved.Bound, CopyResolved.Parts, CopyResolved.Shown);
        Require(CopyResolved.Bound && CopyResolved.Parts == 3 && CopyResolved.Shown == 1, "the control tower copy is not shown");
        for (const auto& [Token, Label] : {std::pair{Cube, "cube"}, std::pair{Ball, "sphere"}, std::pair{Torus, "torus"}})
        {
            const auto Shape = Resolved(Token);
            std::printf("[SceneGeometryCapture] %s: bound %d, shown %u, own surfaces %d, %u triangles\n", Label, Shape.Bound, Shape.Shown, Shape.OwnSurfaces,
                        Shape.Facets);
            Require(Shape.Bound && Shape.Shown == 1, std::string("the ") + Label + " is not shown");
        }
        Require(Resolved(Cube).OwnSurfaces, "the cube has no surface of its own");
        for (const SceneRecordBase* Entry : World.FindAllByType("Geometry")) Require(Entry->QueryRecordName() != "Jump ramp", "the jump ramp is still a record");

        // 📝 How much of the viewport the edits changed.
        size_t Changed = 0, Pixels = 0;
        double Sum     = 0.0;
        for (size_t Offset = 0; Offset + 3 < std::min(Before.size(), After.size()); Offset += 4)
        {
            const int Delta = std::abs(int(Before[Offset]) - int(After[Offset])) + std::abs(int(Before[Offset + 1]) - int(After[Offset + 1])) +
                              std::abs(int(Before[Offset + 2]) - int(After[Offset + 2]));
            Changed += Delta > 24 ? 1 : 0;
            Sum += Delta / 3.0;
            ++Pixels;
        }
        std::printf("[SceneGeometryCapture] edits changed %.1f %% of the viewport's pixels (mean |delta| %.2f levels)\n", 100.0 * Changed / std::max<size_t>(1, Pixels),
                    Sum / std::max<size_t>(1, Pixels));

        Workspace.PresentSceneViewport = nullptr;
        Ownership.reset();
        FrontierExhibit::AcknowledgeTextures();
        ImGui::GetIO().BackendFlags = 0;
        ImGui::DestroyContext();
        return 0;
    }
    catch (const std::exception& Failure)
    {
        std::fprintf(stderr, "[SceneGeometryCapture] FAILED: %s\n", Failure.what());
        return 1;
    }
}
