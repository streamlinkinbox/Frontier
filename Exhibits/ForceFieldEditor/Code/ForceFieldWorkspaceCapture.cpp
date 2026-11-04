/*==========================================================================================================================================
                                                    FORCEFIELDWORKSPACECAPTURE.CPP
==========================================================================================================================================*/
// 🧩 The Force Field in the Everest workspace, captured offscreen: the real EverestWorkspace with its Field view (the slice
//    from 100 m to 100 km, HUD, minimap, timeline lanes), the slice over the live Scene Viewport (own device, read back), and
//    the Force Field, Wind and force inspector pages section by section. Every capture checks what it shows.

#include "Engine/Interface/ConstructPanel.h"
#include "Engine/Interface/ForceFieldVisualization/ForceFieldVisualization.h"
#include "Engine/Interface/IconPresentation.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Interface/SceneDirectoryPanel.h"
#include "Engine/PhysicalDynamics/ForceSourceCatalogue.h"
#include "Engine/Scene/SceneDirectory.h"
#include "EverestSceneViewport.h"
#include "EverestWorkspace.h"
#include "Exhibits/ControlCentre/Code/CpuRasterization.h"
#include "UI/Editor/ControlCentrePanel.h"

#include <imgui.h>
#include <stb_image_write.h>

#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>
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

} // namespace

int main(int ArgumentCount, char** Arguments)
{
    using namespace Frontier;
    try
    {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        Require(ArgumentCount >= 5, "usage: ForceFieldWorkspaceCapture <EngineContent> <Projects/Everest> <ShaderRoot> <OutDir> [Only]");
        const std::filesystem::path ContentDir  = std::filesystem::absolute(Arguments[1]);
        const std::filesystem::path EverestRoot = std::filesystem::absolute(Arguments[2]);
        const std::filesystem::path ShaderRoot  = std::filesystem::absolute(Arguments[3]);
        const std::filesystem::path OutputDir   = std::filesystem::absolute(Arguments[4]);
        const std::string           Only        = ArgumentCount > 5 ? Arguments[5] : "";
        const auto                  Wanted      = [&](const std::string& Name) { return Only.empty() || Name.find(Only) != std::string::npos; };
        std::filesystem::create_directories(OutputDir);

        ImGui::CreateContext();
        ImGuiIO& Io    = ImGui::GetIO();
        Io.DisplaySize = ImVec2(static_cast<float>(CaptureWidth), static_cast<float>(CaptureHeight));
        Io.DeltaTime   = 1.0f / 30.0f;
        Io.IniFilename = nullptr;
        Io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;   // the dockspace's, before the first refresh
        Io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
        ImGui::GetPlatformIO().Renderer_TextureMaxWidth  = 4096;
        ImGui::GetPlatformIO().Renderer_TextureMaxHeight = 4096;
        IconPresentation::Attach(ContentDir / "Icons");
        const std::filesystem::path Preferences = OutputDir / "Personal.projectspace";
        std::filesystem::remove(Preferences);
        ActivationOwnership Ownership(new ControlCentrePanel(ContentDir / "FontArchives", Preferences, EverestRoot / "Everest.projectspace"));
        auto* Centre = static_cast<ControlCentrePanel*>(Ownership.get());
        Require(Centre->Initialize() && Centre->Activate(), "ControlCentrePanel initialization failed");

        Everest::EverestWorkspace     Workspace(*Centre, EverestRoot);
        Everest::EverestSceneViewport Bridge(EverestRoot, ShaderRoot, nullptr);
        std::vector<uint8_t>          ViewportPixels;
        std::array<uint32_t, 2>       ViewportExtent = {0, 0};
        std::string                   LastStatus;
        Workspace.PresentSceneViewport = [&](const Everest::SceneViewportRequest& Request) {
            const auto                          Start   = std::chrono::steady_clock::now();
            const Everest::SceneViewportRefresh Outcome = Bridge.Render(Request, Workspace.QuerySceneWorld(), Workspace.QueryElapsedSeconds());
            Everest::SceneViewportImage         Image;
            Image.Status  = Outcome.Status;
            Image.Readout = Outcome.Readout;
            LastStatus    = Outcome.Status;
            if (Outcome.View != VK_NULL_HANDLE && Bridge.AccessViewport()->ReadPresented(ViewportPixels))
            {
                ViewportExtent = Outcome.Extent;
                Image.Texture  = ViewportTag;
            }
            std::printf("  viewport image %u x %u in %.1f s - %s\n", Outcome.Extent[0], Outcome.Extent[1],
                        std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count(), Outcome.Status.c_str());
            return Image;
        };

        SceneWorld& World  = Workspace.QuerySceneWorld();
        auto*       Forces = dynamic_cast<ForceFieldRecord*>(World.FindFirstByType("ForceField"));
        Require(Forces != nullptr, "the level has no Force Field record");
        ForceFieldConfiguration& Field = Forces->AccessConfiguration();
        ForceSourceRecord*       Tornado = nullptr;
        int                      SourceCount = 0;
        for (const SceneRecordBase* Entry : World.FindAllByType("ForceSource"))
        {
            auto* Source = dynamic_cast<ForceSourceRecord*>(World.FindRecord(Entry->QueryToken()));
            ++SourceCount;
            if (Source != nullptr && Source->QueryRecordName() == "Tornado") Tornado = Source;
        }
        Require(Tornado != nullptr, "the level has no Tornado force");
        std::printf("[ForceFieldWorkspaceCapture] %d forces; the timeline holds at 18.25 s (the blast wave's window is open)\n", SourceCount);

        // 📝 The timeline holds still so every capture shows the same instant; the Tornado is selected (its outline lights,
        //    its page fills the Inspector).
        Field.TimelinePlaying = false;
        Field.TimelineSeconds = 18.25;
        Workspace.QueryOutliner().Select(Tornado->QueryToken());

        ImVec2     Pointer(-FLT_MAX, -FLT_MAX);
        const auto Tick = [&](int Count) {
            for (int Index = 0; Index < Count; ++Index)
            {
                Io.MousePos = Pointer;
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
        // 📝 A view settles when its lattice is in: refresh, wait for the sampling, refresh again (the tracers move between).
        const auto Settle = [&]() {
            for (int Round = 0; Round < 4; ++Round)
            {
                Tick(1);
                if (Workspace.AccessFieldViewport().Slice) Workspace.AccessFieldViewport().Slice->SettleSampling();
                std::this_thread::sleep_for(std::chrono::milliseconds(60));
            }
            Tick(2);
        };
        const auto WriteCapture = [&](const std::string& Name) {
            const ImDrawData*          DrawData = ImGui::GetDrawData();
            std::vector<unsigned char> Pixels(static_cast<size_t>(CaptureWidth) * CaptureHeight * 3u, 14u);
            const std::function<FrontierExhibit::Image(ImTextureID)> Resolve = [&](ImTextureID Texture) {
                return Texture == ViewportTag
                           ? FrontierExhibit::Image{ViewportPixels.data(), static_cast<int>(ViewportExtent[0]), static_cast<int>(ViewportExtent[1]), 4}
                           : FrontierExhibit::Image{};
            };
            for (const ImDrawList* List : DrawData->CmdLists)
                FrontierExhibit::RasterizeCommands(List, Pixels.data(), CaptureWidth, CaptureHeight, DrawData->DisplayPos, DrawData->FramebufferScale, nullptr,
                                                   &Resolve);
            const std::filesystem::path Target = OutputDir / (Name + ".png");
            Require(stbi_write_png(Target.string().c_str(), CaptureWidth, CaptureHeight, 3, Pixels.data(), CaptureWidth * 3) != 0, "PNG write failed");
            std::printf("[ForceFieldWorkspaceCapture] %s\n", Target.string().c_str());
        };
        // 📝 What a Field capture must show: a sampled lattice with a non-zero peak in the shown contribution.
        const auto Report = [&](const std::string& Name) {
            const FieldStatistics& Statistics = Workspace.AccessFieldViewport().Slice->QueryStatistics();
            char                   Cell[32], Span[32];
            ForceFieldVisualization::FormatDistance(Statistics.CellSpan, Cell, sizeof(Cell));
            ForceFieldVisualization::FormatDistance(Workspace.AccessFieldViewport().Slice->View.Span, Span, sizeof(Span));
            std::printf("  %s: span %s, %u x %u = %u samples in %.1f ms, cell %s, peak %.3f %s, %zu outlines\n", Name.c_str(), Span, Statistics.Columns,
                        Statistics.Rows, Statistics.Samples, Statistics.Milliseconds, Cell, Statistics.Peak,
                        Workspace.AccessFieldViewport().Slice->QueryUnit(), Workspace.AccessFieldViewport().Slice->Options.Sources.size());
            Require(Statistics.Samples > 100 && Statistics.Peak > 0.0, Name + ": the slice sampled nothing");
        };
        const auto ShowField = [&](ForceFieldProjection Projection, ForceContribution Contribution, ForceFieldQuantity Quantity, double Span, double CentreX,
                                   double CentreZ) {
            Workspace.AccessFieldViewport().Shown = true;
            Field.Projection                      = Projection;
            Field.ViewContribution                = Contribution;
            Field.Quantity                        = Quantity;
            Field.AdvanceRevision();
            Tick(1);
            Require(Workspace.AccessFieldViewport().Slice != nullptr, "the Field view made no slice");
            ForceFieldVisualization& Slice = *Workspace.AccessFieldViewport().Slice;
            Slice.View.Span                = Span;
            Slice.View.CentreX             = CentreX;
            Slice.View.CentreZ             = CentreZ;
            Slice.Invalidate();
        };
        // 📝 The pointer over a point of the slice (field axes), offset so the reading card sits beside it.
        const auto PointAt = [&](double X, double Z, float Offset) {
            ImVec2 Screen;
            if (Workspace.AccessFieldViewport().Slice && Workspace.AccessFieldViewport().Slice->Project(X, Field.SliceHeightMetres, Z, Screen))
                Pointer = ImVec2(Screen.x + Offset, Screen.y + Offset);
        };

        int Captured = 0;
        // ① The 6 km map (the editor's opening view): every always-on flow source, the cascades and the pointer's reading.
        if (Wanted("FieldMap6km"))
        {
            ShowField(ForceFieldProjection::Map, ForceContribution::Flow, ForceFieldQuantity::Speed, 6000.0, 0.0, 0.0);
            Settle();
            PointAt(0.0, 220.0, 34.0f);
            Settle();
            Require(Workspace.AccessFieldViewport().Hovering, "FieldMap6km: the pointer reads nothing");
            Report("FieldMap6km");
            WriteCapture("FieldMap6km");
            ++Captured;
            Pointer = ImVec2(-FLT_MAX, -FLT_MAX);
        }
        // ② 100 km: the mesocyclone, the squall line and the storm outflow's reach; cells grow to keep the sample count.
        if (Wanted("FieldMap100km"))
        {
            ShowField(ForceFieldProjection::Map, ForceContribution::Flow, ForceFieldQuantity::Speed, 100000.0, 0.0, 0.0);
            Settle();
            Report("FieldMap100km");
            Require(Workspace.AccessFieldViewport().Slice->QueryStatistics().CellSpan >= 256.0, "FieldMap100km: the cells did not grow with the span");
            WriteCapture("FieldMap100km");
            ++Captured;
        }
        // ③ The Tornado in the oblique view: its column standing on the slice, the inflow spiral.
        if (Wanted("FieldTornadoOblique"))
        {
            ShowField(ForceFieldProjection::Oblique, ForceContribution::Flow, ForceFieldQuantity::Speed, 1600.0, 0.0, 220.0);
            Settle();
            Report("FieldTornadoOblique");
            WriteCapture("FieldTornadoOblique");
            ++Captured;
        }
        // ④ Accelerate: the localized forces (attractor, repulsor, gravity) the Flow view dims.
        if (Wanted("FieldAccelerate"))
        {
            ShowField(ForceFieldProjection::Map, ForceContribution::Accelerate, ForceFieldQuantity::Speed, 2400.0, 0.0, 100.0);
            Settle();
            PointAt(520.0, 380.0, 30.0f);
            Settle();
            Report("FieldAccelerate");
            WriteCapture("FieldAccelerate");
            ++Captured;
            Pointer = ImVec2(-FLT_MAX, -FLT_MAX);
        }
        // ⑤ Vorticity (signed): the tornado and the mesocyclone's rotation, the jet's shear.
        if (Wanted("FieldVorticity"))
        {
            ShowField(ForceFieldProjection::Map, ForceContribution::Flow, ForceFieldQuantity::Vorticity, 3000.0, 0.0, 200.0);
            Settle();
            Report("FieldVorticity");
            WriteCapture("FieldVorticity");
            ++Captured;
        }
        // ⑥ The slice over the rendered scene (the Force Field's Scene overlay) in the scene camera's perspective.
        if (Wanted("FieldSceneOverlay"))
        {
            Workspace.AccessFieldViewport().Shown = false;
            Field.ViewContribution                = ForceContribution::Flow;
            Field.Quantity                        = ForceFieldQuantity::Speed;
            Field.SceneOverlay                    = true;
            Field.AdvanceRevision();
            for (int Round = 0; Round < 8; ++Round)
            {
                Tick(1);
                if (Workspace.AccessFieldViewport().Overlay) Workspace.AccessFieldViewport().Overlay->SettleSampling();
            }
            Require(!ViewportPixels.empty(), "no scene viewport image was presented: " + LastStatus);
            Require(Workspace.AccessFieldViewport().Overlay != nullptr, "the Scene overlay made no slice");
            const FieldStatistics& Statistics = Workspace.AccessFieldViewport().Overlay->QueryStatistics();
            std::printf("  FieldSceneOverlay: %u x %u = %u samples in %.1f ms, peak %.3f m/s\n", Statistics.Columns, Statistics.Rows, Statistics.Samples,
                        Statistics.Milliseconds, Statistics.Peak);
            Require(Statistics.Samples > 100 && Statistics.Peak > 0.0, "FieldSceneOverlay: the slice sampled nothing");
            WriteCapture("FieldSceneOverlay");
            ++Captured;
            Field.SceneOverlay = false;
            Field.AdvanceRevision();
        }

        // 📝 The pages, section by section, each in a column tall enough for the whole section (the workspace's column shows
        //    only its top); the readings are the ones the refreshes above published.
        if (Wanted("Page"))
        {
            Workspace.AccessFieldViewport().Shown = true;
            Field.ViewContribution                = ForceContribution::Flow;
            Field.Quantity                        = ForceFieldQuantity::Speed;
            Field.AdvanceRevision();
            Settle();
            const std::pair<const char*, RecordToken> Pages[] = {{"ForceSourcePage", Tornado->QueryToken()},
                                                                 {"ForceFieldPage", Forces->QueryToken()},
                                                                 {"WindPage", World.FindFirstByType("Wind")->QueryToken()}};
            constexpr int ColumnWidth = 470, ColumnHeight = 2900;
            for (const auto& [Name, Token] : Pages)
            {
                Workspace.QueryOutliner().Select(Token);
                Tick(2);
                RecordInspector* Page = InspectorRuntime::Instance().FindInspector(Token);
                Require(Page != nullptr, std::string(Name) + ": no inspector");
                const int Sections = std::max(1, Page->QuerySectionCount());
                for (int Section = 0; Section < Sections; ++Section)
                {
                    Page->ConfigureSection(Section);
                    for (int Settled = 0; Settled < 3; ++Settled)
                    {
                        Io.DisplaySize = ImVec2(static_cast<float>(ColumnWidth), static_cast<float>(ColumnHeight));
                        Io.MousePos    = ImVec2(-FLT_MAX, -FLT_MAX);
                        Centre->PrepareInterface();
                        ImGui::NewFrame();
                        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
                        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(ColumnWidth), static_cast<float>(ColumnHeight)));
                        ImGui::Begin("##InspectorSectionCapture", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
                        bool DeleteRequested = false;
                        InspectorRuntime::Instance().InspectRecord(Token, DeleteRequested);
                        ImGui::End();
                        ImGui::Render();
                        FrontierExhibit::AcknowledgeTextures();
                    }
                    const ImDrawData*          DrawData = ImGui::GetDrawData();
                    std::vector<unsigned char> Pixels(static_cast<size_t>(ColumnWidth) * ColumnHeight * 3u, 14u);
                    for (const ImDrawList* List : DrawData->CmdLists)
                        FrontierExhibit::RasterizeCommands(List, Pixels.data(), ColumnWidth, ColumnHeight, DrawData->DisplayPos, DrawData->FramebufferScale,
                                                           nullptr, nullptr);
                    const int  Reference  = ColumnHeight - 12;
                    const auto RowDiffers = [&](int Row) {
                        for (int Column = 24 * 3; Column < (ColumnWidth - 24) * 3; ++Column)
                            if (std::abs(int(Pixels[static_cast<size_t>(Row) * ColumnWidth * 3 + Column]) -
                                         int(Pixels[static_cast<size_t>(Reference) * ColumnWidth * 3 + Column])) > 6)
                                return true;
                        return false;
                    };
                    int Used = Reference;
                    while (Used > 0 && !RowDiffers(Used)) --Used;
                    Used = std::min(ColumnHeight, Used + 24);
                    const std::filesystem::path Target = OutputDir / (std::string(Name) + "-Section-" + std::to_string(Section) + ".png");
                    Require(stbi_write_png(Target.string().c_str(), ColumnWidth, Used, 3, Pixels.data(), ColumnWidth * 3) != 0, "PNG write failed");
                    std::printf("[ForceFieldWorkspaceCapture] %s (%d px)\n", Target.string().c_str(), Used);
                    ++Captured;
                }
                Io.DisplaySize = ImVec2(static_cast<float>(CaptureWidth), static_cast<float>(CaptureHeight));
            }
        }
        // ⑧ The workspace links (checked, not captured): the Outliner lists every force, the Construct dialog's Forces group holds
        //    the sixteen types, the dialog's order and the pages' actions add forces where the view is, select and show.
        if (Wanted("Workspace"))
        {
            const auto ForceTokens = [&]() {
                std::vector<uint64_t> Tokens;
                for (const SceneRecordBase* Entry : World.FindAllByType("ForceSource")) Tokens.push_back(Entry->QueryToken().Value);
                return Tokens;
            };
            const auto Selected = [&]() { return dynamic_cast<ForceSourceRecord*>(World.FindRecord(Workspace.QueryOutliner().QuerySelected())); };
            Tick(1);
            int Listed = 0;
            for (const SceneDirectoryRow& Row : Workspace.QueryOutliner().QueryRows())
                for (const uint64_t Token : ForceTokens()) Listed += Row.Token.Value == Token ? 1 : 0;
            Require(Listed == SourceCount, "Workspace: the Outliner lists " + std::to_string(Listed) + " of " + std::to_string(SourceCount) + " forces");
            int Tiles = 0;
            for (const ConstructCatalogueEntry& Entry : Workspace.QueryConstructDialog().QueryCatalogue())
            {
                Tiles += Entry.Group == "Forces" ? 1 : 0;
                Require(Entry.Identifier != "ForceSource", "Workspace: the bare ForceSource type is constructible");
            }
            Require(Tiles == 16, "Workspace: the Forces group holds " + std::to_string(Tiles) + " types");

            // The page's "add-force:<type>" with the Field view showing: under the Force Field, at the view's centre.
            Workspace.AccessFieldViewport().Shown = true;
            Tick(1);
            Require(Workspace.AccessFieldViewport().Slice != nullptr, "Workspace: the Field view made no slice");
            Workspace.AccessFieldViewport().Slice->View.CentreX = 300.0;
            Workspace.AccessFieldViewport().Slice->View.CentreZ = -400.0;
            InspectorRuntime::Instance().RequestWorkspaceAction("add-force:attractor");
            Tick(1);
            ForceSourceRecord* Added = Selected();
            Require(Added != nullptr && Added->QueryConfiguration().Category == ForceSourceCategory::Attractor, "Workspace: add-force made no attractor");
            Require(Added->QueryRecordName() == "Attractor 2", "Workspace: the new attractor is named '" + Added->QueryRecordName() + "'");
            Require(SceneDirectory::Instance().QueryParent(Added->QueryToken()).Value == Forces->QueryToken().Value,
                    "Workspace: the new attractor is not under the Force Field");
            Require(std::abs(Added->QueryConfiguration().CentreMetres[0] - 300.0) < 1e-6 && std::abs(Added->QueryConfiguration().CentreMetres[1] - 400.0) < 1e-6,
                    "Workspace: the new attractor is not at the view's centre");
            Require(static_cast<int>(ForceTokens().size()) == SourceCount + 1, "Workspace: the level does not hold the new force");

            // The Construct dialog's order with the Field view hidden: the type's defaults on the ground where the camera looks.
            Workspace.AccessFieldViewport().Shown = false;
            ConstructOrder Order;
            for (const ConstructCatalogueEntry& Entry : Workspace.QueryConstructDialog().QueryCatalogue())
                if (Entry.Identifier == "ForceSource:thermal_updraft") Order.Entry = Entry;
            Require(!Order.Entry.Identifier.empty(), "Workspace: the Forces group has no thermal");
            Order.Name = Order.Entry.Caption;
            Workspace.ServiceConstructOrder(Order);
            Tick(1);
            ForceSourceRecord* Ordered = Selected();
            Require(Ordered != nullptr && Ordered->QueryConfiguration().Category == ForceSourceCategory::ThermalUpdraft, "Workspace: the order made no thermal");
            Require(Ordered->QueryConfiguration().Strength == ConstructForceSource(ForceSourceCategory::ThermalUpdraft).Strength,
                    "Workspace: the thermal does not have its type's defaults");

            // select, field-view, field-span and the Construct dialog on its Forces group.
            InspectorRuntime::Instance().RequestWorkspaceAction("select:" + std::to_string(Tornado->QueryToken().Value));
            Tick(1);
            Require(Workspace.QueryOutliner().QuerySelected().Value == Tornado->QueryToken().Value, "Workspace: select did not select the Tornado");
            InspectorRuntime::Instance().RequestWorkspaceAction("field-span:1000");
            Tick(1);
            Require(Workspace.AccessFieldViewport().Shown && std::abs(Workspace.AccessFieldViewport().Slice->View.Span - 1000.0) < 1e-6,
                    "Workspace: field-span did not show 1 km");
            Settle();
            Require(InspectorRuntime::Instance().QueryEnvironmentAccess().FieldReadings.Measured, "Workspace: the page reads no measurement");
            Require(InspectorRuntime::Instance().QueryEnvironmentAccess().ForceSources.size() == static_cast<size_t>(SourceCount + 2),
                    "Workspace: the page does not list every force");
            InspectorRuntime::Instance().RequestWorkspaceAction("field-view:off");
            Tick(1);
            Require(!Workspace.AccessFieldViewport().Shown, "Workspace: field-view:off left the Field view");
            InspectorRuntime::Instance().RequestWorkspaceAction("add-force");
            Tick(1);
            Require(Workspace.QueryConstructDialog().QueryOpen(), "Workspace: add-force did not open the Construct dialog");
            Workspace.QueryConstructDialog().Close();
            Tick(1);
            std::printf("[ForceFieldWorkspaceCapture] Workspace: %d forces listed, 16 force types to construct; add-force, the dialog's order, select, "
                        "field-span, field-view and the dialog's Forces group all answered\n",
                        Listed);
            for (const RecordToken Token : {Added->QueryToken(), Ordered->QueryToken()}) (void)World.DestroyRecord(Token);
            ++Captured;
        }
        Require(Captured > 0, "nothing matched " + Only);
        std::printf("[ForceFieldWorkspaceCapture] %d captures\n", Captured);

        Workspace.PresentSceneViewport = nullptr;
        Ownership.reset();
        FrontierExhibit::AcknowledgeTextures();
        ImGui::GetIO().BackendFlags = 0;
        ImGui::DestroyContext();
        return 0;
    }
    catch (const std::exception& Failure)
    {
        std::fprintf(stderr, "[ForceFieldWorkspaceCapture] FAILED: %s\n", Failure.what());
        return 1;
    }
}
