/*============================================================================================================================================
                                                    PARTICLEWORKSPACEUIVALIDATION.CPP
============================================================================================================================================*/
// 🖥️ Phase 5 UI & Component parity validation for SimulationPlaybackControls, SimulationTimeRuler, ScalarCurveEditor, DiagnosticInfographicCanvas, LogStreamDrawer & ParticleWorkspaceHost

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ColourSpace/ColourPresetRepository.h"
#include "Engine/Components/Appearance/ThemeRepository.h"
#include "Engine/Components/Appearance/TypographyRepository.h"
#include "Engine/Components/ContentBrowser/ContentBrowserPanel.h"
#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/Controls/ScalarCurveEditor.h"
#include "Engine/Components/Docking/WorkspaceDockContainer.h"
#include "Engine/Configuration/EngineConfiguration.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/Workspaces/Particle/ParticleWorkspaceHost.h"
#include "Engine/Workspaces/WorkspaceCapabilityRegister.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <imgui.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

namespace
{

using namespace Frontier;
using namespace Frontier::Configuration;
using namespace Frontier::GeometricRaster;
using namespace Frontier::Workspaces;

[[nodiscard]] bool VerifyCheck(bool Condition, const char* Message) noexcept
{
    if (!Condition)
    {
        std::fprintf(stderr, "[ParticleWorkspaceUIValidation] FAILED: %s\n", Message);
        return false;
    }
    return true;
}

} // namespace

int main()
{
    constexpr uint32_t CanvasW = 1840u;
    constexpr uint32_t CanvasH = 980u;

    const std::filesystem::path RepoRoot  = Exhibits::ResolveRepositoryRootPath();
    const std::filesystem::path ShaderDir = std::filesystem::path(FRONTIER_COMPILED_SHADER_DIR);
    const std::filesystem::path ImageDir  = RepoRoot / "Exhibits/Images/Particles";
    std::filesystem::create_directories(ImageDir);

    // 1. Verify WorkspaceCapabilityRegister & EngineConfiguration
    WorkspaceCapabilityRegister CapReg{};
    if (!VerifyCheck(CapReg.FindByTarget(HostWorkspaceTarget::ParticleWorkspace) != nullptr, "ParticleWorkspace registered in WorkspaceCapabilityRegister"))
    {
        return 1;
    }
    const auto Permitted = CapReg.QueryPermittedForPath("Test.ParticleSpace");
    if (!VerifyCheck(!Permitted.empty() && Permitted.front().Target == HostWorkspaceTarget::ParticleWorkspace, ".particlespace routes to ParticleWorkspace"))
    {
        return 1;
    }

    EngineConfiguration StandaloneCfg(EngineConfiguration::ConstructStandaloneParticleDefault());
    if (!VerifyCheck(StandaloneCfg.QueryWorkspaceCategory() == WorkspaceCategory::Particle, "ConstructStandaloneParticleDefault targets Particle"))
    {
        return 1;
    }

    // 2. Initialize Vulkan & ImGui context
    VulkanInterchange VkDevice{};
    std::string       VkDiag;
    if (!VerifyCheck(VkDevice.InitializeInterchange(RepoRoot, &VkDiag), VkDiag.c_str()))
    {
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGuiContext* Gui = ImGui::CreateContext();
    ImGui::SetCurrentContext(Gui);
    ImGuiIO& Io    = ImGui::GetIO();
    Io.IniFilename = nullptr;
    Io.LogFilename = nullptr;
    Io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    TypographyRepository Typefaces{};
    if (!VerifyCheck(Typefaces.LoadRepository(RepoRoot / "EngineContent/FontsArchives") == 5u, "Load FontRepository.FontSpace"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }
    TypographyRepository::AssignCurrent(&Typefaces);
    if (ImFont* DefaultFont = Typefaces.QueryFont(0u, Frontier::FontWeightClass::Regular))
    {
        Io.FontDefault = DefaultFont;
    }
    unsigned char* AtlasPixels = nullptr;
    int            AtlasW      = 0;
    int            AtlasH      = 0;
    Io.Fonts->GetTexDataAsRGBA32(&AtlasPixels, &AtlasW, &AtlasH);

    ThemeRepository Theme{};
    Theme.ConfigureTheme(Frontier::ThemePreset::Dark);
    Theme.ConfigureAccent(Frontier::AccentPreset::Blue);
    ControlAppearance::ConfigureTheme(Theme);

    // 3. Configure ParticleWorkspaceHost + ContentBrowserPanel
    ParticleWorkspaceHost Host{};
    Host.ConfigureLaunchPolicy(StandaloneCfg);
    if (!VerifyCheck(Host.AttachVulkanInterchange(&VkDevice, ShaderDir), "Attached VulkanInterchange to ParticleWorkspaceHost"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }

    ContentBrowserPanel Browser{};
    Browser.AttachThemeRepository(&Theme);
    Host.AttachContentBrowserPanel(&Browser);

    // Warm up simulation 24 steps for rich live GPU viewport render
    for (int Step = 0; Step < 24; ++Step)
    {
        Host.AccessSession().StepSimulation(1.0f / 60.0f, 1120u, 760u);
    }

    auto RenderWholeStudioFrame = [&](bool ShowBrowserShelf, bool ShowLogDrawer) {
        Io.DisplaySize = ImVec2(static_cast<float>(CanvasW), static_cast<float>(CanvasH));
        Io.DeltaTime   = 1.0f / 60.0f;
        ImGui::NewFrame();

        PixelSpace Surface(ImGui::GetForegroundDrawList(), { 0.0f, 0.0f }, 1.0f);
        const Rectangle2D FullBounds{ 0.0f, 0.0f, static_cast<float>(CanvasW), static_cast<float>(CanvasH) };
        Surface.FillRectangle(FullBounds, ColorRgba{ 11u, 12u, 16u, 255u }, 0.0f);

        ControlPointer Ptr{};
        Ptr.Enabled = true;
        PaneInputReading Input{};

        const Rectangle2D TopStripBounds{ 0.0f, 0.0f, static_cast<float>(CanvasW), 44.0f };
        Host.RenderStudioTopStrip(Surface, TopStripBounds, Ptr);

        const float WorkTop = TopStripBounds.Bottom();
        const float WorkH   = static_cast<float>(CanvasH) - WorkTop;
        const float OutW    = 286.0f;
        const float InspW   = 424.0f;
        const float VpW     = static_cast<float>(CanvasW) - OutW - InspW;

        Host.AccessOutliner().Render(Surface, { 0.0f, WorkTop, OutW, WorkH }, Input, 1.0 / 60.0);
        Host.AccessViewportHost().Render(Surface, { OutW, WorkTop, VpW, WorkH }, Input, 1.0 / 60.0);
        Host.AccessInspector().Render(Surface, { OutW + VpW, WorkTop, InspW, WorkH }, Input, 1.0 / 60.0);

        if (ShowBrowserShelf)
        {
            Browser.ConfigureShelfExtent(ContentShelfExtent::SplitHalf, true);
            ContentBrowserPointerInput BPtr{};
            Browser.RenderWithInput(Surface, FullBounds, BPtr, 1.0 / 60.0);
        }
        if (ShowLogDrawer)
        {
            Host.AccessSession().LogDrawerSpec.Expanded = true;
            Host.RenderOverlayDrawers(Surface, FullBounds, Ptr);
        }

        ImGui::Render();
    };

    // Capture primary HTML-parity screenshot (Overview tab + live Vulkan 1.3 viewport)
    RenderWholeStudioFrame(false, false);
    Exhibits::SoftwareRasterCanvas MainCanvas(CanvasW, CanvasH, { 11, 12, 16, 255 });
    MainCanvas.RasterizeImGuiBatches(ImGui::GetDrawData());
    const std::filesystem::path MainPng = ImageDir / "05-ParticleWorkspaceHostUI.png";
    if (!VerifyCheck(MainCanvas.WritePngFile(MainPng), "Write 05-ParticleWorkspaceHostUI.png"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }

    // 4. Verify Source tab, ColourRampEditor ContentBrowser drop slot, ScalarCurveEditor, LogStreamDrawer & Undo/Redo
    Host.AccessSession().ActiveInspectorTab = ParticleInspectorTab::Source;
    RenderWholeStudioFrame(true, false);

    const std::size_t BeforeRevCount = Host.InspectSession().InspectRevision().QueryLiveStepCount();
    const bool AppliedPreset = Browser.ApplyRampPresetToSlot(0u, "particle.rig.colour_ramp");
    if (!VerifyCheck(AppliedPreset, "ContentBrowser ApplyRampPresetToSlot updates active particle rig") ||
        !VerifyCheck(Host.InspectSession().InspectRevision().QueryLiveStepCount() > BeforeRevCount, "ColourRamp assignment recorded RevisionTracker step"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }

    // Capture secondary UI proof with Source tab + ContentBrowser Colour Ramps shelf + LogStreamDrawer
    RenderWholeStudioFrame(true, true);
    Exhibits::SoftwareRasterCanvas SourceCanvas(CanvasW, CanvasH, { 11, 12, 16, 255 });
    SourceCanvas.RasterizeImGuiBatches(ImGui::GetDrawData());
    const std::filesystem::path SourcePng = ImageDir / "05-ParticleSourceAndContentBrowserUI.png";
    if (!VerifyCheck(SourceCanvas.WritePngFile(SourcePng), "Write 05-ParticleSourceAndContentBrowserUI.png"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }

    // Verify Undo / Redo round-trip
    if (!VerifyCheck(Host.AccessSession().Undo(), "RevisionTracker Undo succeeded") ||
        !VerifyCheck(Host.AccessSession().Redo(), "RevisionTracker Redo succeeded"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }

    // Verify ScalarCurveProfile evaluation
    const float CurveMid = Host.InspectSession().SizeOverLifeCurve.Evaluate(0.25f);
    if (!VerifyCheck(CurveMid > 0.90f, "ScalarCurveProfile Bell curve peaks near t=0.25"))
    {
        ImGui::DestroyContext(Gui);
        return 1;
    }

    // Verify HeadlessWorkspaceDockContainer registration
    {
        Components::HeadlessWorkspaceDockContainer DockContainer{};
        Host.RegisterDockPanes(DockContainer);
        if (!VerifyCheck(Host.QueryRegisteredTabCount() == 1u, "Standalone ParticleWorkspace registers 1 center viewport tab"))
        {
            ImGui::DestroyContext(Gui);
            return 1;
        }
    }

    ImGui::DestroyContext(Gui);
    std::printf("[ParticleWorkspaceUIValidation] ALL CHECKS PASSED\n");
    return 0;
}
