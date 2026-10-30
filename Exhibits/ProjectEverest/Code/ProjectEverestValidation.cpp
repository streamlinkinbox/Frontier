/*==========================================================================================================================================
                                                    PROJECTEVERESTVALIDATION.CPP
==========================================================================================================================================*/
// Native draw-list capture of production browser, application selection, workspace and Control Centre.
#include "Exhibits/ControlCentre/Code/CpuRasterization.h"
#include "Engine/Interface/IconPresentation.h"
#include "UI/Editor/EditorWorkspace.h"
#include "UI/Editor/ProjectBrowserPanel.h"
#include <imgui_internal.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
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
            EditorWorkspace Workspace(*Centre);
            Workspace.ConfigureProject({"Project Everest", Selected.string(), "project-everest-ui-v1"});
            Workspace.ConfigureProject({"Project Everest", Selected.string(), "project-everest-ui-v1"});
            auto &Panels = Workspace.QueryPanels();

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
            Capture(Evidence / "Workspace.ppm");
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
        Ownership.reset();
        ImGui::DestroyContext();
        std::cout << "PASS: browser filtering, Everest selection, application launch/decode, consume-once handoff, "
                     "idempotent workspace setup, production LogStream registration/close/reopen, shared docking, "
                     "Control Centre and theme captures. Native C++ draw lists rasterized on CPU; no Vulkan scene test.\n";
        return 0;
    }
    catch (const std::exception &Error)
    {
        std::cerr << "FAIL: " << Error.what() << '\n';
        return 1;
    }
}
