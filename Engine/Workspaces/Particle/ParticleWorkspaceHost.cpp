/*============================================================================================================================================
                                                        PARTICLEWORKSPACEHOST.CPP
============================================================================================================================================*/
// 🧩 Implementation of ParticleWorkspaceHost coordinating StandaloneWorkspace / FeatureSet panes, ContentBrowser Colour Ramps & LogStreamDrawer

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Particle/ParticleWorkspaceHost.h"

#include "Engine/Components/Diagnostics/LogStreamDrawer.h"
#include "Engine/Components/VectorIcons/VectorIconRepository.h"
#include "Engine/Interchange/GeometryInterchange.h"
#include "Engine/Workspaces/Particle/ParticlePaneAppearance.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace Frontier::Workspaces
{

namespace
{

[[nodiscard]] std::string NormalizeLowerExt(const std::filesystem::path& FilePath)
{
    std::string Ext = FilePath.extension().string();
    std::transform(Ext.begin(), Ext.end(), Ext.begin(), [](unsigned char Ch) { return static_cast<char>(std::tolower(Ch)); });
    return Ext;
}

} // namespace

ParticleWorkspaceHost::ParticleWorkspaceHost()
{
    Outliner.AttachSession(&Session);
    ViewportHost.AttachSession(&Session);
    Inspector.AttachSession(&Session);
}

void ParticleWorkspaceHost::ConfigureLaunchPolicy(const Configuration::EngineConfiguration& Config) noexcept
{
    ActiveEngineConfig = Config;
}

bool ParticleWorkspaceHost::AttachVulkanInterchange(
    GeometricRaster::VulkanInterchange* BoundDevice,
    const std::filesystem::path&        ShaderDirectory)
{
    return Session.AttachVulkanInterchange(BoundDevice, ShaderDirectory);
}

void ParticleWorkspaceHost::AttachContentBrowserPanel(ContentBrowserPanel* TargetPanel)
{
    BoundBrowserPanel = TargetPanel;
    Session.AttachContentBrowserPanel(TargetPanel);
    Inspector.AttachContentBrowserPanel(TargetPanel);
    if (TargetPanel == nullptr)
    {
        return;
    }

    TargetPanel->ConfigureWorkspaceDomain(WorkspaceCategory::Particle);
    (void)TargetPanel->ActivateCategory(ContentCategory::Colours);
    TargetPanel->ConfigureColourCollectionScope(ColourCollectionScope::Ramps);
    TargetPanel->ClearColourRampSlots();
    if (ParticleSpace::ParticleRig* Rig = Session.AccessSelectedRig())
    {
        TargetPanel->RegisterColourRampSlot(ColourRampSlotRegistration{
            "particle.rig.colour_ramp",
            "Active Particle Rig Colour Ramp",
            Inspector.QueryLastColourRampStripBounds(),
            &Rig->TintBinding.SourceProfile,
            [this](const ColourSpace::ColourSourceProfile& Profile, std::string_view Title)
            {
                Session.AssignActiveRigColourProfile(Profile, Title);
            },
            nullptr,
        });
    }
}

#if FRONTIER_EDITOR_ENABLED
void ParticleWorkspaceHost::AttachContentBrowser(Editor::Explorer::ContentBrowser* TargetBrowser)
{
    BoundBrowser = TargetBrowser;
    AttachContentBrowserPanel(TargetBrowser != nullptr ? &TargetBrowser->AccessPanel() : nullptr);
}
#endif

bool ParticleWorkspaceHost::IntakeExternalFileDrop(
    const std::filesystem::path& DroppedPath,
    std::string&                 OutDiagnostic)
{
    const std::string Ext = NormalizeLowerExt(DroppedPath);
    if (Ext == ".particlespace")
    {
        return Session.LoadParticleSpaceDocument(DroppedPath, OutDiagnostic);
    }
    if (Ext == ".toml" || Ext == ".json")
    {
        return Session.ImportExternalSnapshot(DroppedPath, OutDiagnostic);
    }
    if (Ext == ".obj" || Ext == ".stl" || Ext == ".gltf" || Ext == ".geometryspace")
    {
        GeometrySpace::ConstructedSampleAsset Imported{};
        if (!Interchange::GeometryInterchange::ImportAssetFromFile(DroppedPath, Imported, OutDiagnostic))
        {
            return false;
        }
        if (ParticleSpace::ParticleRig* Rig = Session.AccessSelectedRig())
        {
            ParticleSpace::SurfaceAnchorOriginSpec SurfaceSpec{};
            Rig->Origin         = SurfaceSpec;
            Rig->SampledAnchors = ParticleSpace::SampleSurfaceAnchors(SurfaceSpec, 512u, &Imported.Network);
            Session.RecordRevisionStep("Drop Custom Surface Mesh");
            Session.AppendLog(LogStreamSeverity::Success, "Rig", "Sampled dropped surface mesh " + DroppedPath.filename().string());
        }
        return true;
    }
    OutDiagnostic = "Unsupported ParticleWorkspace drop extension: " + Ext;
    return false;
}

void ParticleWorkspaceHost::RegisterDockPanes(Components::HeadlessWorkspaceDockContainer& DockContainer)
{
    using Components::DockPlacementDirection;
    using Components::PaneDirectiveMask;
    using Components::WorkspaceDockPaneDescriptor;

    RegisteredTabCount = 0u;

    (void)DockContainer.RegisterPane(WorkspaceDockPaneDescriptor{
        "Outliner",
        DockPlacementDirection::SplitLeft,
        0.20f,
        PaneDirectiveMask::StandardPaneDirectives,
        [this]() { Outliner.PresentInImGuiPane(1.0 / 60.0, 1.0f); },
    });

    (void)DockContainer.RegisterPane(WorkspaceDockPaneDescriptor{
        "Scene view",
        DockPlacementDirection::CenterTabGroup,
        0.54f,
        PaneDirectiveMask::CentralViewportDirectives,
        [this]() { ViewportHost.PresentInImGuiPane(1.0 / 60.0, 1.0f); },
    });
    ++RegisteredTabCount;

    if (ActiveEngineConfig.QueryLaunchMode() == Configuration::HostLaunchMode::FeatureSet)
    {
        const char* PeerWorkspaces[3] = { "Geometry", "Material", "Forces" };
        for (const char* PeerTitle : PeerWorkspaces)
        {
            (void)DockContainer.RegisterPane(WorkspaceDockPaneDescriptor{
                PeerTitle,
                DockPlacementDirection::CenterTabGroup,
                0.54f,
                PaneDirectiveMask::CentralViewportDirectives,
                [PeerTitle]() { ImGui::TextUnformatted(PeerTitle); },
            });
            ++RegisteredTabCount;
        }
    }

    (void)DockContainer.RegisterPane(WorkspaceDockPaneDescriptor{
        "Inspector",
        DockPlacementDirection::SplitRight,
        0.26f,
        PaneDirectiveMask::StandardPaneDirectives,
        [this]() { Inspector.PresentInImGuiPane(1.0 / 60.0, 1.0f); },
    });
}

void ParticleWorkspaceHost::RenderStudioTopStrip(PixelSpace& Surface, Rectangle2D Bounds, const ControlPointer& Pointer)
{
    const auto Theme = ParticleAppearance::QueryThemeColors();
    Surface.FillRectangle(Bounds, ColorRgba{ 10u, 11u, 14u, 255u }, 0.0f);
    Surface.StrokeLine({ Bounds.X, Bounds.Bottom() - 1.0f }, { Bounds.Right(), Bounds.Bottom() - 1.0f }, Theme.BorderSubtle, 1.0f);

    // Brand mark + PARTICLE STUDIO
    Surface.Text({ Bounds.X + 16.0f, Bounds.Y + 12.0f }, 16.0f, Theme.TextPrimary, "frontier.");
    Surface.Text({ Bounds.X + 94.0f, Bounds.Y + 15.0f }, 10.5f, Theme.TextMuted, "PARTICLE STUDIO");

    // Document tabs
    const Rectangle2D ActiveTab{ Bounds.X + 224.0f, Bounds.Y + 7.0f, 124.0f, 28.0f };
    Surface.FillRectangle(ActiveTab, Theme.InnerTileBg, 9.0f);
    Surface.StrokeRectangle(ActiveTab, Theme.BorderSubtle, 9.0f, 1.0f);
    Surface.Text({ ActiveTab.X + 14.0f, ActiveTab.Y + 7.5f }, 11.0f, Theme.TextPrimary, Session.StatusBanner.c_str());

    // Center mode segmented pills: Workspace | Assets | Log Stream
    const float CenterX = Bounds.Center().X - 150.0f;
    const Rectangle2D ModeWell{ CenterX, Bounds.Y + 6.0f, 300.0f, 30.0f };
    Surface.FillRectangle(ModeWell, Theme.CardBg, 15.0f);
    Surface.StrokeRectangle(ModeWell, Theme.BorderSubtle, 15.0f, 1.0f);

    const Rectangle2D WsPill{ ModeWell.X + 3.0f, ModeWell.Y + 3.0f, 96.0f, 24.0f };
    const Rectangle2D AssetsPill{ WsPill.Right() + 4.0f, ModeWell.Y + 3.0f, 88.0f, 24.0f };
    const Rectangle2D LogPill{ AssetsPill.Right() + 4.0f, ModeWell.Y + 3.0f, 102.0f, 24.0f };

    const bool WsActive = !Session.ShowAssetsBrowser && !Session.LogDrawerSpec.Expanded;
    Surface.FillRectangle(WsPill, WsActive ? Theme.HoverBg : ColorRgba{ 0u, 0u, 0u, 0u }, 12.0f);
    Surface.Text({ WsPill.X + 14.0f, WsPill.Y + 5.5f }, 11.0f, WsActive ? Theme.TextPrimary : Theme.TextMuted, "Workspace");
    if (Pointer.Enabled && Pointer.Pressed && WsPill.Contains({ Pointer.X, Pointer.Y }))
    {
        Session.ShowAssetsBrowser      = false;
        Session.LogDrawerSpec.Expanded = false;
    }

    Surface.FillRectangle(AssetsPill, Session.ShowAssetsBrowser ? Theme.HoverBg : ColorRgba{ 0u, 0u, 0u, 0u }, 12.0f);
    Surface.Text({ AssetsPill.X + 14.0f, AssetsPill.Y + 5.5f }, 11.0f, Session.ShowAssetsBrowser ? Theme.TextPrimary : Theme.TextMuted, "Assets 102");
    if (Pointer.Enabled && Pointer.Pressed && AssetsPill.Contains({ Pointer.X, Pointer.Y }))
    {
        Session.ShowAssetsBrowser = !Session.ShowAssetsBrowser;
    }

    char LogLbl[32]{};
    std::snprintf(LogLbl, sizeof(LogLbl), "Log Stream %zu", Session.LogEntries.size());
    Surface.FillRectangle(LogPill, Session.LogDrawerSpec.Expanded ? Theme.HoverBg : ColorRgba{ 0u, 0u, 0u, 0u }, 12.0f);
    Surface.Text({ LogPill.X + 10.0f, LogPill.Y + 5.5f }, 11.0f, Session.LogDrawerSpec.Expanded ? Theme.TextPrimary : Theme.TextMuted, LogLbl);
    if (Pointer.Enabled && Pointer.Pressed && LogPill.Contains({ Pointer.X, Pointer.Y }))
    {
        Session.LogDrawerSpec.Expanded = !Session.LogDrawerSpec.Expanded;
    }

    // Right status badge + Export TOML button
    const Rectangle2D StatusPill{ Bounds.Right() - 284.0f, Bounds.Y + 8.0f, 168.0f, 26.0f };
    Surface.FillRectangle(StatusPill, Session.UsingGpuExecutor() ? Theme.EmeraldSoft : Theme.AmberSoft, 13.0f);
    Surface.StrokeRectangle(StatusPill, Session.UsingGpuExecutor() ? Theme.EmeraldInk : Theme.AmberInk, 13.0f, 1.0f);
    Surface.FillCircle({ StatusPill.X + 12.0f, StatusPill.Center().Y }, 3.2f, Session.UsingGpuExecutor() ? Theme.EmeraldInk : Theme.AmberInk);
    Surface.Text(
        { StatusPill.X + 20.0f, StatusPill.Y + 6.5f },
        9.8f,
        Session.UsingGpuExecutor() ? Theme.EmeraldInk : Theme.AmberInk,
        Session.UsingGpuExecutor() ? "LIVE · VULKAN 1.3 GPU" : "PREVIEW · SOFTWARE 3D");

    const Rectangle2D ExportBtn{ Bounds.Right() - 104.0f, Bounds.Y + 7.0f, 90.0f, 28.0f };
    const bool HoverExport = Pointer.Enabled && ExportBtn.Contains({ Pointer.X, Pointer.Y });
    Surface.FillRectangle(ExportBtn, HoverExport ? Theme.HoverBg : Theme.InnerTileBg, 14.0f);
    Surface.StrokeRectangle(ExportBtn, Theme.BorderStrong, 14.0f, 1.0f);
    Surface.Text({ ExportBtn.X + 12.0f, ExportBtn.Y + 7.5f }, 10.5f, Theme.TextPrimary, "Export TOML");
}

void ParticleWorkspaceHost::RenderOverlayDrawers(PixelSpace& Surface, Rectangle2D ViewportBounds, const ControlPointer& Pointer)
{
    if (Session.LogDrawerSpec.Expanded)
    {
        const Rectangle2D DrawerBounds{
            ViewportBounds.X + 260.0f,
            ViewportBounds.Bottom() - 230.0f,
            std::max(380.0f, ViewportBounds.Width - 520.0f),
            196.0f,
        };
        LogStreamDrawer::Construct(
            Surface,
            DrawerBounds,
            Session.LogEntries,
            Session.LogDrawerSpec,
            Pointer,
            1.0f);
        if (Session.LogDrawerSpec.ClearClicked)
        {
            Session.LogEntries.clear();
        }
    }
}

} // namespace Frontier::Workspaces
