/*==========================================================================================================================================
                                                   EVERESTINTERFACEVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 End-to-end validation + 1920x1080 CPU-rasterized PNG Exhibits for ProjectGenerator C++ scaffolding, .levelspace containers,
//    composable physics/thermal/chemical inspectors, generic DiagramControls, standalone <Domain>Editors, and EverestWorkspace.

#include "Engine/ContentInterchange/ProjectGenerator.h"
#include "Engine/ContentInterchange/ProjectSpaceCodec.h"
#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Interface/IconPresentation.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Scene/RecordStore.h"
#include "Engine/Scene/SceneDirectory.h"
#include "EverestWorkspace.h"
#include "Exhibits/ControlCentre/Code/CpuRasterization.h"
#include "UI/Editor/ControlCentrePanel.h"
#include "UI/Editor/ProjectBrowserPanel.h"

#include <imgui.h>
#include <stb_image_write.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static_assert(FRONTIER_EDITOR == 1, "EverestInterfaceValidation must compile with FRONTIER_EDITOR == 1.");

namespace
{

constexpr int CaptureWidth  = 1920;
constexpr int CaptureHeight = 1080;

void Require(bool Condition, const char* Message)
{
    if (!Condition)
    {
        throw std::runtime_error(Message);
    }
}

void WritePngCapture(const std::filesystem::path& OutputPath)
{
    const ImDrawData* DrawData = ImGui::GetDrawData();
    Require(DrawData != nullptr && DrawData->CmdListsCount > 0, "Expected non-empty ImDrawData for PNG capture.");

    std::vector<unsigned char> Pixels(static_cast<size_t>(CaptureWidth) * static_cast<size_t>(CaptureHeight) * 3u, 14u);
    for (const ImDrawList* CmdList : DrawData->CmdLists)
    {
        FrontierExhibit::RasterizeCommands(
            CmdList, Pixels.data(), CaptureWidth, CaptureHeight, DrawData->DisplayPos, DrawData->FramebufferScale);
    }

    if (OutputPath.has_parent_path())
    {
        std::filesystem::create_directories(OutputPath.parent_path());
    }
    const int Written = stbi_write_png(
        OutputPath.string().c_str(), CaptureWidth, CaptureHeight, 3, Pixels.data(), CaptureWidth * 3);
    Require(Written != 0, "stbi_write_png failed to write exhibit PNG.");
}

} // namespace

int main(int ArgumentCount, char** Arguments)
{
    using namespace Frontier;

    try
    {
        Require(ArgumentCount == 4, "Expected EngineContent, Projects/Everest, and OutputDirectory arguments.");
        const std::filesystem::path ContentDir  = std::filesystem::absolute(Arguments[1]);
        const std::filesystem::path EverestRoot = std::filesystem::absolute(Arguments[2]);
        const std::filesystem::path OutputDir   = std::filesystem::absolute(Arguments[3]);
        const std::filesystem::path ProjectFile = EverestRoot / "Everest.projectspace";

        Require(std::filesystem::exists(ProjectFile), "Projects/Everest/Everest.projectspace not found.");
        std::filesystem::create_directories(OutputDir);

        // -------------------------------------------------------------------------------------------------------------
        // 0. Verify ProjectBrowserPanel::CreateProject auto-generates C++ classes + CMakeLists.txt + Default.levelspace
        // -------------------------------------------------------------------------------------------------------------
        {
            const std::filesystem::path GeneratedDir  = OutputDir / "GeneratedKineticLab";
            const std::filesystem::path GeneratedProj = GeneratedDir / "KineticLab.projectspace";
            std::filesystem::remove_all(GeneratedDir);

            ProjectBrowserPanel GeneratorBrowser;
            Require(GeneratorBrowser.CreateProject(GeneratedProj, "Kinetic Lab"),
                    "ProjectBrowserPanel::CreateProject failed for Kinetic Lab.");
            Require(std::filesystem::exists(GeneratedDir / "Source" / "KineticLabApplication.h"),
                    "Auto-generated KineticLabApplication.h missing.");
            Require(std::filesystem::exists(GeneratedDir / "Source" / "KineticLabApplication.cpp"),
                    "Auto-generated KineticLabApplication.cpp missing.");
            Require(std::filesystem::exists(GeneratedDir / "Source" / "Main.cpp"),
                    "Auto-generated Main.cpp missing.");
            Require(std::filesystem::exists(GeneratedDir / "CMakeLists.txt"),
                    "Auto-generated CMakeLists.txt missing.");
            Require(std::filesystem::exists(GeneratedDir / "Content" / "Levels" / "Default.levelspace"),
                    "Auto-generated Content/Levels/Default.levelspace missing.");

            SceneWorld GeneratedWorld;
            std::string LoadErr;
            Require(GeneratedWorld.LoadStartupLevelFromProject(GeneratedProj, &LoadErr),
                    ("Generated Default.levelspace failed to load: " + LoadErr).c_str());
            Require(GeneratedWorld.QueryRecordCount() == 6,
                    "Generated Default.levelspace expected 6 default starter records.");
        }

        // -------------------------------------------------------------------------------------------------------------
        // 1. Initialize ImGui + ControlCentrePanel + Project Browser -> Launch Everest.projectspace
        // -------------------------------------------------------------------------------------------------------------
        ImGui::CreateContext();
        ImGuiIO& Io    = ImGui::GetIO();
        Io.DisplaySize = ImVec2(static_cast<float>(CaptureWidth), static_cast<float>(CaptureHeight));
        Io.DeltaTime   = 1.0f / 60.0f;
        Io.IniFilename = nullptr;
        Io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        Io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
        ImGui::GetPlatformIO().Renderer_TextureMaxWidth  = 4096;
        ImGui::GetPlatformIO().Renderer_TextureMaxHeight = 4096;

        IconPresentation::Attach(ContentDir / "Icons");

        const std::filesystem::path PersonalPrefs = OutputDir / "Personal.projectspace";
        std::filesystem::remove(PersonalPrefs);

        ActivationOwnership Ownership(new ControlCentrePanel(ContentDir / "FontArchives", PersonalPrefs, ProjectFile));
        auto*               Centre = static_cast<ControlCentrePanel*>(Ownership.get());
        Require(Centre->Initialize() && Centre->Activate(), "ControlCentrePanel initialization failed.");

        std::filesystem::path LaunchedPath;
        {
            ProjectBrowserPanel Browser;
            Require(Browser.AttachPaths({ProjectFile}, true) == 1, "ProjectBrowserPanel refused Everest.projectspace.");
            Require(Browser.SelectProject(ProjectFile), "ProjectBrowserPanel failed to select Everest.projectspace.");

            for (int FrameIdx = 0; FrameIdx < 4; ++FrameIdx)
            {
                Centre->PrepareInterface();
                ImGui::NewFrame();
                (void)Centre->Integrate(1.0 / 60.0);
                Browser.ConfigurePalette(ControlKit::Palette());
                Browser.ConstructInterface();
                Centre->ConstructInterface();
                ImGui::Render();
                FrontierExhibit::AcknowledgeTextures();
            }
            WritePngCapture(OutputDir / "01-ProjectBrowserEverest.png");

            Require(Browser.RequestOpenProject(), "ProjectBrowserPanel::RequestOpenProject failed.");
            LaunchedPath = Browser.ConsumeLaunchPath();
            Require(LaunchedPath == ProjectFile, "Launched project path mismatch.");
        }

        // -------------------------------------------------------------------------------------------------------------
        // 2. Open EverestWorkspace and verify composable multi-inspector bindings
        // -------------------------------------------------------------------------------------------------------------
        Everest::EverestWorkspace Workspace(*Centre, LaunchedPath.parent_path());
        SceneWorld&               World = Workspace.QuerySceneWorld();
        Require(World.LoadLevelSpace(EverestRoot.parent_path().parent_path() / "Exhibits/EverestInterface/Content/Simulation.levelspace"), "Simulation exhibit fixture failed to load");
        Require(World.QueryRecordCount() == 26, "Expected 26 records in the Simulation fixture (one Render settings record).");
        // 📝 Each planet of the sky is its own Scene Directory entry (Venus, Jupiter, Mars, Saturn), beside the starfield and moons.
        Require(World.FindAllByType("Planet").size() == 4 && World.FindFirstByType("Stars") && World.FindFirstByType("Moons"),
                "Expected four Planet entries beside the Stars and Moons entries.");

        auto* OriginRec   = World.FindFirstByType("SpatialNode");
        auto* RigidRec    = World.FindFirstByType("RigidBody");
        auto* ThermalRec  = World.FindFirstByType("ThermalBody");
        auto* ChemRec     = World.FindFirstByType("ChemicalVessel");
        auto* OscRec      = World.FindFirstByType("HarmonicOscillator");
        auto* FractureRec = World.FindFirstByType("Fracture");
        Require(OriginRec && RigidRec && ThermalRec && ChemRec && OscRec && FractureRec,
                "Expected composable records missing from Everest SceneWorld.");

        Require(InspectorRuntime::Instance().QueryInspectorCount(OriginRec->QueryToken()) == 1,
                "SpatialNodeRecord must attach exactly 1 inspector (SpatialInspector).");
        Require(InspectorRuntime::Instance().QueryInspectorCount(RigidRec->QueryToken()) == 3,
                "RigidBodyRecord must attach 3 modular inspectors (Spatial + Physics + SurfaceOptics).");
        Require(InspectorRuntime::Instance().QueryInspectorCount(ThermalRec->QueryToken()) == 4,
                "ThermalBodyRecord must attach 4 modular inspectors (Spatial + Physics + Thermal + SurfaceOptics).");
        Require(InspectorRuntime::Instance().QueryInspectorCount(ChemRec->QueryToken()) == 3,
                "ChemicalVesselRecord must attach 3 modular inspectors (Spatial + Thermal + Chemical).");
        Require(InspectorRuntime::Instance().QueryInspectorCount(OscRec->QueryToken()) == 3,
                "HarmonicOscillatorRecord must attach 3 modular inspectors (Spatial + Physics + HarmonicOscillator).");
        Require(InspectorRuntime::Instance().QueryHasEditor(FractureRec->QueryToken()),
                "FractureRecord must expose standalone FractureEditor.");

        const auto RenderFrames = [&](int Count) {
            for (int FrameIdx = 0; FrameIdx < Count; ++FrameIdx)
            {
                Centre->PrepareInterface();
                ImGui::NewFrame();
                (void)Centre->Integrate(1.0 / 60.0);
                (void)Workspace.ConfigurePlatform();
                Workspace.ConstructInterface(1.0 / 60.0);
                Centre->ConstructInterface();
                ImGui::Render();
                FrontierExhibit::AcknowledgeTextures();
            }
        };

        // Exhibit 02: Default Solar selection + clean analytical viewport + Outliner .levelspace + LogStream
        RenderFrames(6);
        WritePngCapture(OutputDir / "02-EverestSolarWorkspace.png");

        // Exhibit 03: Select ThermalBodyRecord (Spatial + Physics/Mass + Thermal spectrum + SurfaceOptics)
        Workspace.QueryOutliner().Select(ThermalRec->QueryToken());
        RenderFrames(4);
        WritePngCapture(OutputDir / "03-EverestThermalPhysicsBody.png");

        // Exhibit 04: Select ChemicalVesselRecord (Spatial + Thermal + Chemical Arrhenius curve)
        Workspace.QueryOutliner().Select(ChemRec->QueryToken());
        RenderFrames(4);
        WritePngCapture(OutputDir / "04-EverestChemicalKinetics.png");

        // Exhibit 07: Select the Render settings record (RenderPipeline): the one inspector for the whole renderer.
        // Numbered 07 so the existing 01-06 capture names stay stable.
        auto* PipelineRec = World.FindFirstByType("RenderPipeline");
        Require(PipelineRec != nullptr, "Expected RenderPipeline record in Everest SceneWorld.");
        Workspace.QueryOutliner().Select(PipelineRec->QueryToken());
        RenderFrames(4);
        WritePngCapture(OutputDir / "07-EverestRenderPipelineInspector.png");

        // Exhibit 08: the demo's environment Inspector pages, one capture per section tab (InspectorPages ported 1:1).
        int PageCaptures = 0;
        for (const char* Type : {"Solar", "Atmosphere", "Clouds", "Wind", "ForceField", "Fog", "AerialFog", "Lightning", "Stars", "Moons", "OpticalFlare"})
        {
            SceneRecordBase* PageEntry = World.FindFirstByType(Type);
            Require(PageEntry != nullptr, "Expected every environment page's record in Everest SceneWorld.");
            RecordInspector* Page = InspectorRuntime::Instance().FindInspector(PageEntry->QueryToken());
            Require(Page != nullptr && Page->QueryOccupiesColumn(), "An environment Inspector page must occupy the Inspector column.");
            Workspace.QueryOutliner().Select(PageEntry->QueryToken());
            const int Sections = std::max(1, Page->QuerySectionCount());
            for (int Section = 0; Section < Sections; ++Section)
            {
                Page->ConfigureSection(Section);
                RenderFrames(4);
                WritePngCapture(OutputDir / ("08-InspectorPage-" + std::string(Type) + "-" + std::to_string(Section + 1) + ".png"));
                ++PageCaptures;
            }
            Page->ConfigureSection(0);
        }

        // Exhibit 05: Open standalone FractureEditor modal
        Workspace.QueryOutliner().Select(FractureRec->QueryToken());
        InspectorRuntime::Instance().RequestEditorOpen(FractureRec->QueryToken());
        RenderFrames(4);
        WritePngCapture(OutputDir / "05-EverestFractureEditor.png");
        InspectorRuntime::Instance().CloseActiveEditor();

        // Exhibit 06: Dynamic scene mutation via Outliner + ControlCentrePanel Appearance & Settings modal
        Workspace.QueryOutliner().RequestConstructRecord("ChemicalVessel");
        RenderFrames(2);
        Require(World.QueryRecordCount() == 27, "Dynamic RequestConstructRecord('ChemicalVessel') failed.");
        const RecordToken AddedToken = Workspace.QueryOutliner().QuerySelected();
        Workspace.QueryOutliner().RequestDeleteRecord(AddedToken);
        RenderFrames(2);
        Require(World.QueryRecordCount() == 26, "Dynamic RequestDeleteRecord failed.");

        Centre->ActivateSettingsPage(ControlCentrePageCategory::Appearance);
        RenderFrames(36);
        WritePngCapture(OutputDir / "06-EverestControlCentre.png");

        // Exhibit 06b: the workspace has no top toolbar; its Projects and Save level actions are on the Quick Controls page
        Require(Centre->QueryQuickActionCount() == 3, "EverestWorkspace must place Projects and Save level on Quick Controls.");
        Centre->ActivateSettingsPage(ControlCentrePageCategory::QuickControls);
        RenderFrames(36);
        WritePngCapture(OutputDir / "06b-EverestQuickControls.png");

        Ownership.reset();
        FrontierExhibit::AcknowledgeTextures();
        ImGui::GetIO().BackendFlags = 0;
        ImGui::DestroyContext();

        std::cout << "[EverestInterfaceValidation] Passed (ProjectGenerator C++ scaffolding, .levelspace, composable inspectors, 8 PNG exhibits and "
                  << PageCaptures << " environment page captures verified).\n";
        return 0;
    }
    catch (const std::exception& Error)
    {
        std::cerr << "[EverestInterfaceValidation] FAILED: " << Error.what() << "\n";
        return 1;
    }
}
