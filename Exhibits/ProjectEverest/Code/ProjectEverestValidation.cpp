/*==========================================================================================================================================
                                                    PROJECTEVERESTVALIDATION.CPP
==========================================================================================================================================*/
// Native draw-list capture of production browser, application selection, workspace and Control Centre.
#include "Exhibits/ControlCentre/Code/CpuRasterization.h"
#include "Engine/Interface/IconPresentation.h"
#include "UI/Editor/EditorWorkspace.h"
#include "UI/Editor/ProjectBrowserPanel.h"
#include <imgui_internal.h>
#include "Frontier/Engine/Scene/EnvironmentRecords.h"
#include "Frontier/Engine/Scene/RecordStore.h"
#include "Frontier/Engine/Scene/SceneDirectory.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
// An application-owned description demonstrates that the inspector has no hard-coded vehicle fields.
class VehicleSpecificationEntry final : public Frontier::RecordEntry
{
  public:
    double Wheelbase = 2.7;
    bool Enabled = true;
    VehicleSpecificationEntry() : RecordEntry(Frontier::RecordClassification::Vehicle)
    {
        Frontier::RecordEditorDescription Description;
        Description.StableId = "user-vehicle";
        Description.Group = "Vehicle";
        Description.DisplayName = "User vehicle specification";
        Description.Category = "Application-authored vehicle";
        Description.Enabled = &Enabled;
        Description.ScalarFields = {{"Wheelbase", "m", 1.0, 6.0, .01, 2, &Wheelbase}};
        RegisterEditorDescription(std::move(Description));
    }
};

class ConstructionFailureEntry final : public Frontier::RecordEntry
{
  public:
    ConstructionFailureEntry() : RecordEntry(Frontier::RecordClassification::Vehicle)
    {
        throw std::runtime_error("Intentional constructor failure");
    }
};

void Require(bool Condition, const char *Message)
{
    if (!Condition) throw std::runtime_error(Message);
}
void Capture(const std::filesystem::path &Path)
{
    const auto *Commands = ImGui::GetDrawData();
    std::vector<unsigned char> Pixels(1440 * 1000 * 3, 24);
    for (const auto *List : Commands->CmdLists)
        FrontierExhibit::RasterizeCommands(List, Pixels.data(), 1440, 1000,
            Commands->DisplayPos, Commands->FramebufferScale);
    std::ofstream Output(Path, std::ios::binary);
    Output << "P6\n1440 1000\n255\n";
    Output.write(reinterpret_cast<const char *>(Pixels.data()), Pixels.size());
    Require(Output.good(), "Capture write failed");
}
}

int main(int ArgumentCount, char **Arguments)
{
    using namespace Frontier;
    try
    {
        Require(ArgumentCount == 3, "Expected content and evidence directories");
        const auto Content = std::filesystem::absolute(Arguments[1]);
        const auto Evidence = std::filesystem::absolute(Arguments[2]);
        std::filesystem::create_directories(Evidence);
        std::filesystem::remove(Evidence / "Personal.projectspace");
        const auto InitialDirectoryCount = SceneDirectory::Instance().QueryCount();
        const auto InitialStoreCount = RecordStore::Instance().QueryCount();
        {
            SceneDirectoryPanel Projection;
            RecordToken Removed;
            {
                VehicleSpecificationEntry Vehicle;
                Removed = Vehicle.Token();
                Projection.Synchronize(SceneDirectory::Instance());
                Projection.Select(Removed);
                Projection.Synchronize(SceneDirectory::Instance());
                Require(Projection.QueryInspectorModel().ScalarFields.size() == 1,
                    "Application-authored inspector was not registered");
                Require(Projection.QueryInspectorModel().ScalarFields[0].Target == &Vehicle.Wheelbase,
                    "Application field target differs");
            }
            Projection.Synchronize(SceneDirectory::Instance());
            Require(!Projection.QueryInspectorModel().HasSelection && Projection.QueryInspectorModel().ScalarFields.empty(),
                "Externally destroyed record left inspector targets");
            Require(SceneDirectory::Instance().QueryEditorDescription(Removed) == nullptr,
                "Destroyed inspector description remained published");
            bool Threw = false;
            try { ConstructionFailureEntry Entry; }
            catch (const std::runtime_error &) { Threw = true; }
            Require(Threw && SceneDirectory::Instance().QueryCount() == InitialDirectoryCount &&
                RecordStore::Instance().QueryCount() == InitialStoreCount, "Constructor failure leaked registration");
        }
        ImGui::CreateContext();
        auto &IO = ImGui::GetIO();
        IO.DisplaySize = ImVec2(1440, 1000);
        IO.DeltaTime = 1.0f / 60;
        IO.IniFilename = nullptr;
        IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
        ImGui::GetPlatformIO().Renderer_TextureMaxWidth = 4096;
        ImGui::GetPlatformIO().Renderer_TextureMaxHeight = 4096;
        IconPresentation::Attach(Content / "Icons");
        ActivationOwnership Ownership(new ControlCentrePanel(Content / "FontArchives", Evidence / "Personal.projectspace"));
        auto *Centre = static_cast<ControlCentrePanel *>(Ownership.get());
        Require(Centre->Initialize() && Centre->Activate(), "Control Centre activation failed");
        {
            ProjectBrowserPanel Browser;
            auto FrameBrowser = [&]()
            {
                Centre->PrepareInterface();
                ImGui::NewFrame();
                Browser.ConfigurePalette(ControlKit::Palette());
                Browser.ConstructInterface();
                ImGui::Render();
                FrontierExhibit::AcknowledgeTextures();
            };
            const auto Titles = Browser.QueryFilteredTitles("project", "Everest", false);
            Require(Titles.size() == 1 && Titles[0] == "Project Everest", "Everest catalogue registration missing");
            FrameBrowser(); FrameBrowser(); FrameBrowser();
            Capture(Evidence / "Browser.ppm");
            Require(Browser.OpenSample("everest"), "Everest selection failed");
            Browser.DebugShow("everest", 0, 0, false, 100, "details", "dark", true, false);
            FrameBrowser(); FrameBrowser(); FrameBrowser();
            Capture(Evidence / "ApplicationSelection.ppm");
            IO.AddMousePosEvent(700, 524);
            FrameBrowser();
            IO.AddMouseButtonEvent(0, true);
            FrameBrowser();
            IO.AddMouseButtonEvent(0, false);
            FrameBrowser();
            const auto Selected = Browser.ConsumeLaunchPath();
            Require(Selected.filename() == "Everest.projectspace", "Wrong application launch path");
            Require(Browser.ConsumeLaunchPath().empty(), "Launch request was not consumed once");
            ImGui::ClosePopupsOverWindow(nullptr, false);
            const auto InitialCount = SceneDirectory::Instance().QueryCount();
            EditorWorkspace Workspace(*Centre);
            Workspace.ConfigureProject({"Project Everest", Selected.string(), "project-everest-ui-v1"});
            Workspace.ConfigureProject({"Project Everest", Selected.string(), "project-everest-ui-v1"});
            auto &Panels = Workspace.QueryPanels();
            auto *Scene = Workspace.QueryEverestScene();
            Require(Scene != nullptr, "Everest scene absent");
            auto &Projection = Scene->QueryPanel();
            Require(SceneDirectory::Instance().QueryCount() == InitialCount + 5, "Default registration count differs");
            Require(Projection.QueryInspectorModel().HasSelection, "Sun inspector absent");
            Require(Projection.QueryInspectorModel().ScalarFields.size() == 3, "Sun authored controls missing");
            const auto Added = Scene->ConstructSun();
            Require(SceneDirectory::Instance().QueryRegistered(Added), "Constructor did not register record");
            auto *Sun = dynamic_cast<SunRecord *>(RecordStore::Instance().Find(Added));
            Require(Sun != nullptr, "Token did not resolve to Sun");
            auto &Model = Projection.QueryInspectorModel();
            Require(Model.ScalarFields[0].Target == &Sun->Placement.ElevationDegrees, "Inspector copied rather than addressed live setting");
            *Model.ScalarFields[0].Target = 52.5;
            Require(Sun->Placement.ElevationDegrees == 52.5, "Authored inspector did not edit live setting");
            Require(Scene->EraseSelected(), "Selected scene deletion refused");
            Require(!Scene->EraseSelected(), "Repeated deletion was not safe");
            Require(RecordStore::Instance().Find(Added) == nullptr, "Deleted token remained live");
            Require(!Projection.QueryInspectorModel().HasSelection &&
                Projection.QueryInspectorModel().ScalarFields.empty(), "Inspector retained destroyed fields");
            Projection.Select(Added);
            Scene->Synchronize();
            Require(!Projection.QuerySelected().IsValid(), "Stale selection remained active");
            const auto Replacement = Scene->ConstructSun();
            Require(Replacement.Value != Added.Value, "Recreated record reused stale identity");
            Require(Scene->EraseSelected(), "Replacement teardown failed");
            Require(Projection.SelectByStableId("sun"), "Default Sun selection failed");
            Scene->Synchronize();


            auto FrameWorkspace = [&]()
            {
                Centre->PrepareInterface();
                (void)Workspace.ConfigurePlatform();
                ImGui::NewFrame();
                (void)Centre->Integrate(1.0 / 60);
                Workspace.ConstructInterface();
                Centre->ConstructInterface();
                ImGui::Render();
                FrontierExhibit::AcknowledgeTextures();
            };
            for (int Index = 0; Index < 10; ++Index) FrameWorkspace();
            Require(Panels.QueryOpen("LogStream"), "Production LogStream panel absent");
            Require(Panels.QueryOpen("Outliner") && Panels.QueryOpen("Inspector"), "Scene panels absent");
            // Real input: select the Fog row, then return to Sun and drag its elevation slider.
            const auto ClickAt = [&](float X, float Y)
            {
                IO.AddMousePosEvent(X, Y);
                FrameWorkspace();
                IO.AddMouseButtonEvent(0, true);
                FrameWorkspace();
                IO.AddMouseButtonEvent(0, false);
                FrameWorkspace();
            };
            ClickAt(100, 425);
            FrameWorkspace();
            Require(Projection.QueryInspectorModel().StableId == "fog", "Mouse selection did not select Fog");
            ClickAt(100, 311);
            FrameWorkspace();
            Require(Projection.QueryInspectorModel().StableId == "sun", "Mouse selection did not reselect Sun");
            const double ElevationBefore = *Projection.QueryInspectorModel().ScalarFields[0].Target;
            IO.AddMousePosEvent(1100, 353);
            FrameWorkspace();
            IO.AddMouseButtonEvent(0, true);
            FrameWorkspace();
            IO.AddMousePosEvent(1250, 353);
            FrameWorkspace();
            IO.AddMouseButtonEvent(0, false);
            FrameWorkspace();
            Require(*Projection.QueryInspectorModel().ScalarFields[0].Target != ElevationBefore,
                "Mouse slider edit did not change live Sun elevation");
            Capture(Evidence / "Workspace.ppm");
            ClickAt(338, 90);
            FrameWorkspace();
            Require(SceneDirectory::Instance().QueryCount() == InitialCount + 6,
                "Add Sun button did not construct a record");
            ClickAt(417, 90);
            FrameWorkspace();
            Require(SceneDirectory::Instance().QueryCount() == InitialCount + 5 &&
                !Projection.QueryInspectorModel().HasSelection, "Delete button retained a record or inspector");
            Capture(Evidence / "EmptyInspector.ppm");
            Projection.ConfigureTextFilter("fog");
            Require(Projection.QueryVisible().size() == 1, "Outliner text filter differs");
            Projection.ConfigureTextFilter("");
            Require(Projection.SelectByStableId("clouds"), "Cloud row selection failed");
            Scene->Synchronize();
            for (int Index = 0; Index < 3; ++Index) FrameWorkspace();
            Capture(Evidence / "CloudInspector.ppm");
            Require(Projection.SelectByStableId("sun"), "Sun reselect failed");
            Scene->Synchronize();
            Require(Panels.ConfigureOpen("LogStream", false), "LogStream close failed");
            FrameWorkspace();
            Require(Panels.ConfigureOpen("LogStream", true), "LogStream reopen failed");
            Centre->ActivatePage(AppearancePageCategory::Theme);
            for (int Index = 0; Index < 90; ++Index) FrameWorkspace();
            Capture(Evidence / "ControlCentre.ppm");
            IO.AddKeyEvent(ImGuiKey_Escape, true);
            FrameWorkspace();
            IO.AddKeyEvent(ImGuiKey_Escape, false);
            auto Preferences = Centre->QueryAppearance().QueryApplied();
            Preferences.Theme = ThemeCategory::Light;
            Centre->QueryAppearance().ConfigureDraft(Preferences);
            Centre->QueryAppearance().Apply();
            for (int Index = 0; Index < 90; ++Index) FrameWorkspace();
            Capture(Evidence / "LightTheme.ppm");
        }
        Require(SceneDirectory::Instance().QueryCount() == InitialDirectoryCount &&
            RecordStore::Instance().QueryCount() == InitialStoreCount, "Workspace teardown leaked scene records");
        Ownership.reset();
        ImGui::DestroyContext();
        std::cout << "PASS: browser filtering, Everest selection, application launch/decode, consume-once handoff, "
                     "idempotent workspace setup, production LogStream registration/close/reopen, shared docking, "
                     "Control Centre and theme captures; Outliner mouse selection, live Inspector mouse edit, authored vehicle "
                     "description, create/delete/recreate, constructor rollback and workspace teardown. "
                     "Native C++ draw lists rasterized on CPU; no Vulkan scene test.\n";
        return 0;
    }
    catch (const std::exception &Error)
    {
        std::cerr << "FAIL: " << Error.what() << '\n';
        return 1;
    }
}
