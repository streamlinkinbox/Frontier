/*============================================================================================================================================
                                                         PARTICLEVIEWPORTHOST.CPP
============================================================================================================================================*/
// 🖥️ Implementation of ParticleViewportHost matching HTML Particle Studio center viewport, telemetry HUD, tool rail & playback timeline

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Particle/ParticleViewportHost.h"

#include "Engine/Components/Controls/DropdownSelector.h"
#include "Engine/Components/Controls/SimulationPlaybackControls.h"
#include "Engine/Components/Controls/SimulationTimeRuler.h"
#include "Engine/Components/VectorIcons/VectorIconRepository.h"
#include "Engine/Workspaces/Particle/ParticlePaneAppearance.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace Frontier::Workspaces
{

namespace
{

constexpr std::array<const char*, 4u> OverlayLabels {{
    "Clean viewport (no overlay)",
    "Velocity arrows",
    "Force vectors",
    "Origin anchors",
}};

constexpr std::array<const char*, 3u> ProjectionLabels {{
    "Perspective orbit",
    "Orthographic top",
    "Front elevation",
}};

} // namespace

ParticleViewportHost::~ParticleViewportHost() noexcept
{
    WorkspaceAppearance::ReleasePanePointerOwnership(this);
    ViewportTexture.DestroyPixels();
}

void ParticleViewportHost::AttachSession(ParticleWorkspaceSession* TargetSession) noexcept
{
    Session = TargetSession;
    Interactor.AttachSession(TargetSession);
}

void ParticleViewportHost::SyncViewportTexture()
{
    if (Session == nullptr)
    {
        return;
    }
    const std::span<const uint8_t> Pixels = Session->InspectViewportRgba8();
    const uint32_t W = Session->QueryViewportWidthPx();
    const uint32_t H = Session->QueryViewportHeightPx();
    if (W == 0u || H == 0u || Pixels.size() < static_cast<std::size_t>(W) * H * 4u)
    {
        return;
    }

    const bool NeedsCreate =
        TextureWidth != W ||
        TextureHeight != H ||
        ViewportTexture.Pixels == nullptr;
    if (NeedsCreate)
    {
        ViewportTexture.DestroyPixels();
        ViewportTexture.SetStatus(ImTextureStatus_Destroyed);
        ViewportTexture.Create(ImTextureFormat_RGBA32, static_cast<int>(W), static_cast<int>(H));
        TextureWidth  = W;
        TextureHeight = H;
    }

    if (ViewportTexture.Pixels != nullptr)
    {
        std::memcpy(ViewportTexture.Pixels, Pixels.data(), static_cast<std::size_t>(W) * H * 4u);
        if (!NeedsCreate && ViewportTexture.Status == ImTextureStatus_OK)
        {
            ViewportTexture.UpdateRect = { 0u, 0u, static_cast<unsigned short>(W), static_cast<unsigned short>(H) };
            ViewportTexture.SetStatus(ImTextureStatus_WantUpdates);
        }
    }
}

void ParticleViewportHost::Render(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const PaneInputReading& Input,
    double                  DeltaSeconds)
{
    const auto Theme = ParticleAppearance::QueryThemeColors();
    Surface.FillRectangle(Bounds, Theme.CanvasBg, 0.0f);

    if (Session == nullptr)
    {
        return;
    }

    const ControlPointer Ptr = Input.PrimaryPointer;

    // 1. Top viewport toolbar (42 px)
    const Rectangle2D TopBar{ Bounds.X + 8.0f, Bounds.Y + 6.0f, Bounds.Width - 16.0f, 38.0f };
    Surface.FillRectangle(TopBar, Theme.PanelBg, 10.0f);
    Surface.StrokeRectangle(TopBar, Theme.BorderSubtle, 10.0f, 1.0f);

    VectorIconRepository::ConstructControl(Surface, ControlIcon::ParticleSparkles, { TopBar.X + 12.0f, TopBar.Y + 11.0f, 16.0f, Theme.TextPrimary, 1.5f });
    Surface.Text({ TopBar.X + 34.0f, TopBar.Y + 12.0f }, 12.0f, Theme.TextPrimary, "Scene view");

    // Perspective orbit dropdown
    const Rectangle2D ProjBtn{ TopBar.X + 118.0f, TopBar.Y + 5.0f, 146.0f, 28.0f };
    const ControlContact ProjContact = DropdownSelector::ConstructButton(Surface, ProjBtn, ProjectionLabels[0], ProjectionMenuOpen, Ptr, 1.0f);
    if (ProjContact.Pressed)
    {
        ProjectionMenuOpen = !ProjectionMenuOpen;
    }

    // Overlay mode dropdown
    const uint32_t OverlayIdx = static_cast<uint32_t>(Session->OverlayMode);
    const Rectangle2D OverlayBtn{ ProjBtn.Right() + 8.0f, TopBar.Y + 5.0f, 206.0f, 28.0f };
    const ControlContact OverlayContact = DropdownSelector::ConstructButton(
        Surface, OverlayBtn, OverlayLabels[std::min<uint32_t>(OverlayIdx, 3u)], OverlayMenuOpen, Ptr, 1.0f);
    if (OverlayContact.Pressed)
    {
        OverlayMenuOpen = !OverlayMenuOpen;
    }

    // Throw Ball · T pill
    const Rectangle2D ThrowBtn{ OverlayBtn.Right() + 10.0f, TopBar.Y + 5.0f, 98.0f, 28.0f };
    const bool HoverThrow = Ptr.Enabled && ThrowBtn.Contains({ Ptr.X, Ptr.Y });
    Surface.FillRectangle(ThrowBtn, HoverThrow ? Theme.HoverBg : Theme.InnerTileBg, 14.0f);
    Surface.StrokeRectangle(ThrowBtn, Theme.BorderSubtle, 14.0f, 1.0f);
    Surface.Text({ ThrowBtn.X + 12.0f, ThrowBtn.Y + 7.5f }, 10.5f, Theme.TextPrimary, "Throw Ball · T");
    if (HoverThrow && Ptr.Pressed)
    {
        Session->ThrowInteractiveBall();
    }

    // Field · V pill
    const Rectangle2D FieldBtn{ ThrowBtn.Right() + 6.0f, TopBar.Y + 5.0f, 74.0f, 28.0f };
    const bool FieldActive = (Session->OverlayMode == ParticleOverlayMode::ForceVectors);
    const bool HoverField  = Ptr.Enabled && FieldBtn.Contains({ Ptr.X, Ptr.Y });
    Surface.FillRectangle(FieldBtn, FieldActive ? Theme.EmeraldSoft : (HoverField ? Theme.HoverBg : Theme.InnerTileBg), 14.0f);
    Surface.StrokeRectangle(FieldBtn, FieldActive ? Theme.EmeraldInk : Theme.BorderSubtle, 14.0f, 1.0f);
    Surface.Text({ FieldBtn.X + 12.0f, FieldBtn.Y + 7.5f }, 10.5f, FieldActive ? Theme.EmeraldInk : Theme.TextPrimary, "Field · V");
    if (HoverField && Ptr.Pressed)
    {
        Session->OverlayMode = FieldActive ? ParticleOverlayMode::CleanViewport : ParticleOverlayMode::ForceVectors;
    }

    // 2. Central 3D Canvas bounds (above playback & ruler strips)
    const float BottomDeckH = 104.0f;
    const Rectangle2D CanvasBounds{
        Bounds.X + 8.0f,
        TopBar.Bottom() + 6.0f,
        Bounds.Width - 16.0f,
        std::max(140.0f, Bounds.Height - 50.0f - BottomDeckH),
    };

    // Step simulation & upload/sync rendered Vulkan RGBA8 target
    Session->StepSimulation(
        static_cast<float>(DeltaSeconds > 0.0 ? DeltaSeconds : (1.0 / 60.0)),
        static_cast<uint32_t>(std::max(256.0f, CanvasBounds.Width)),
        static_cast<uint32_t>(std::max(256.0f, CanvasBounds.Height)));
    SyncViewportTexture();

    Surface.FillRectangle(CanvasBounds, Theme.CanvasBg, 12.0f);
    if (ViewportTexture.Pixels != nullptr)
    {
        Surface.FillTexturedRectangle(CanvasBounds, &ViewportTexture, { 0.0f, 0.0f }, { 1.0f, 1.0f }, 12.0f);
    }
    Surface.StrokeRectangle(CanvasBounds, Theme.BorderSubtle, 12.0f, 1.0f);

    // 3. Top-inside Live Telemetry HUD Strip
    const float HudY = CanvasBounds.Y + 10.0f;
    float HudX = CanvasBounds.X + 12.0f;

    const Rectangle2D LivePill{ HudX, HudY, 64.0f, 24.0f };
    Surface.FillRectangle(LivePill, ColorRgba{ 16u, 22u, 20u, 220u }, 12.0f);
    Surface.StrokeRectangle(LivePill, Theme.EmeraldInk, 12.0f, 1.0f);
    Surface.FillCircle({ LivePill.X + 12.0f, LivePill.Y + 12.0f }, 3.2f, Theme.EmeraldInk);
    Surface.Text({ LivePill.X + 22.0f, LivePill.Y + 6.0f }, 10.0f, Theme.EmeraldInk, "LIVE");
    HudX = LivePill.Right() + 8.0f;

    const auto* ActiveRig = Session->InspectSelectedRig();
    const char* RigName = ActiveRig ? ActiveRig->RigTitle.c_str() : "Particle Rig";
    const float NameW   = std::min(220.0f, static_cast<float>(std::strlen(RigName)) * 6.5f + 22.0f);
    const Rectangle2D NamePill{ HudX, HudY, NameW, 24.0f };
    Surface.FillRectangle(NamePill, ColorRgba{ 22u, 24u, 32u, 224u }, 12.0f);
    Surface.StrokeRectangle(NamePill, Theme.BorderSubtle, 12.0f, 1.0f);
    Surface.Text({ NamePill.X + 11.0f, NamePill.Y + 6.0f }, 10.5f, Theme.TextPrimary, RigName);
    HudX = NamePill.Right() + 8.0f;

    Surface.Text(
        { HudX, HudY + 6.0f },
        10.0f,
        Theme.TextMuted,
        Session->UsingGpuExecutor() ? "VFX · Vulkan 1.3" : "VFX · Software 3D");

    // Right-aligned live particle & timing telemetry pills
    const auto Metrics = Session->QueryMetrics();
    const uint32_t TotalSlots = Session->InspectDocument().QueryTotalAllocatedSlots();
    char CountText[48]{};
    std::snprintf(CountText, sizeof(CountText), "PARTICLES %u / %u", Metrics.AliveParticles, TotalSlots);
    const Rectangle2D CountPill{ CanvasBounds.Right() - 340.0f, HudY, 152.0f, 24.0f };
    Surface.FillRectangle(CountPill, ColorRgba{ 18u, 20u, 27u, 224u }, 12.0f);
    Surface.Text({ CountPill.X + 10.0f, CountPill.Y + 6.0f }, 9.8f, Theme.TextPrimary, CountText);

    const Rectangle2D RatePill{ CountPill.Right() + 6.0f, HudY, 76.0f, 24.0f };
    Surface.FillRectangle(RatePill, ColorRgba{ 18u, 20u, 27u, 224u }, 12.0f);
    Surface.Text({ RatePill.X + 10.0f, RatePill.Y + 6.0f }, 9.8f, Theme.TextPrimary, "RATE 60 fps");

    char MsText[32]{};
    std::snprintf(MsText, sizeof(MsText), "%s %.2f ms", Session->UsingGpuExecutor() ? "GPU" : "CPU", static_cast<double>(Session->StepDurationMs));
    const Rectangle2D MsPill{ RatePill.Right() + 6.0f, HudY, 88.0f, 24.0f };
    Surface.FillRectangle(MsPill, ColorRgba{ 18u, 20u, 27u, 224u }, 12.0f);
    Surface.Text({ MsPill.X + 9.0f, MsPill.Y + 6.0f }, 9.5f, Theme.TextPrimary, MsText);

    // 4. Left Floating Tool Rail inside Canvas
    const Rectangle2D ToolRail{ CanvasBounds.X + 12.0f, CanvasBounds.Y + 54.0f, 34.0f, 166.0f };
    Surface.FillRectangle(ToolRail, ColorRgba{ 18u, 20u, 26u, 232u }, 17.0f);
    Surface.StrokeRectangle(ToolRail, Theme.BorderSubtle, 17.0f, 1.0f);

    const std::array<ControlIcon, 5u> RailIcons {{
        ControlIcon::VectorPointer,
        ControlIcon::ParticleOrigin,
        ControlIcon::ForceMagnetic,
        ControlIcon::ForceWind,
        ControlIcon::ParticleRestart,
    }};
    for (uint32_t Idx = 0u; Idx < RailIcons.size(); ++Idx)
    {
        const Rectangle2D Slot{ ToolRail.X + 3.0f, ToolRail.Y + 6.0f + static_cast<float>(Idx) * 31.0f, 28.0f, 28.0f };
        const bool Selected = (static_cast<uint32_t>(Session->ActiveTool) == Idx);
        const bool Hover    = Ptr.Enabled && Slot.Contains({ Ptr.X, Ptr.Y });
        if (Selected || Hover)
        {
            Surface.FillRectangle(Slot, Selected ? Theme.HoverBg : Theme.InnerTileBg, 14.0f);
        }
        VectorIconRepository::ConstructControl(
            Surface,
            RailIcons[Idx],
            { Slot.X + 6.0f, Slot.Y + 6.0f, 16.0f, Selected ? Theme.WarmCreamBg : Theme.TextMuted, 1.5f });
        if (Hover && Ptr.Pressed)
        {
            if (Idx == 4u)
            {
                Session->CameraYawRad         = 0.45f;
                Session->CameraPitchRad       = 0.30f;
                Session->CameraDistanceMeters = 6.8f;
            }
            else
            {
                Session->ActiveTool = static_cast<ParticleViewportTool>(Idx);
            }
        }
    }

    // 5. Optional Force Vector Overlay inside Canvas
    if (Session->OverlayMode == ParticleOverlayMode::ForceVectors)
    {
        for (const auto& Force : Session->InspectDocument().ForceSources)
        {
            if (!Force.Enabled) continue;
            const float Fx = CanvasBounds.Center().X + Force.CentreMeters.X * 68.0f;
            const float Fy = CanvasBounds.Y + CanvasBounds.Height * 0.62f - Force.CentreMeters.Z * 68.0f;
            Surface.StrokeCircle({ Fx, Fy }, 12.0f, Theme.ForceAccent, 1.4f);
            Surface.StrokeLine({ Fx, Fy }, { Fx + 22.0f, Fy - 4.0f }, Theme.ForceAccent, 1.8f);
        }
    }

    // 6. Bottom-left XYZ Axis Triad & Bottom Hint Pill
    const Point2D TriadOrigin{ CanvasBounds.X + 34.0f, CanvasBounds.Bottom() - 34.0f };
    Surface.StrokeLine(TriadOrigin, { TriadOrigin.X, TriadOrigin.Y - 22.0f }, ColorRgba{ 112u, 224u, 134u, 255u }, 1.8f);
    Surface.StrokeLine(TriadOrigin, { TriadOrigin.X + 20.0f, TriadOrigin.Y + 10.0f }, ColorRgba{ 238u, 108u, 96u, 255u }, 1.8f);
    Surface.StrokeLine(TriadOrigin, { TriadOrigin.X - 18.0f, TriadOrigin.Y + 11.0f }, ColorRgba{ 98u, 164u, 245u, 255u }, 1.8f);
    Surface.Text({ TriadOrigin.X - 3.0f, TriadOrigin.Y - 34.0f }, 9.0f, ColorRgba{ 112u, 224u, 134u, 255u }, "Z");

    const float HintW = std::min(CanvasBounds.Width - 120.0f, 444.0f);
    const Rectangle2D HintPill{ CanvasBounds.Center().X - HintW * 0.5f, CanvasBounds.Bottom() - 30.0f, HintW, 22.0f };
    Surface.FillRectangle(HintPill, ColorRgba{ 18u, 20u, 26u, 220u }, 11.0f);
    Surface.Text(
        { HintPill.X + 14.0f, HintPill.Y + 5.0f },
        9.5f,
        Theme.TextMuted,
        "Drag handles (Shift: Y) · G handles · Drop OBJ/STL · T throw ball · Space pause · V field");

    // Process orbit / handle dragging inside canvas
    Interactor.ProcessInput(CanvasBounds, Input);

    // 7. Bottom Deck: SimulationPlaybackControls + SimulationTimeRuler
    const Rectangle2D PlaybackBounds{ Bounds.X + 8.0f, CanvasBounds.Bottom() + 6.0f, Bounds.Width - 16.0f, 42.0f };
    const SimulationPlaybackReading PbReading = SimulationPlaybackControls::Construct(
        Surface, PlaybackBounds, Session->PlaybackTuning, Ptr, 1.0f);
    if (PbReading.TriggeredAction == SimulationPlaybackAction::Restart)
    {
        Session->RestartSimulation();
    }
    else if (PbReading.TriggeredAction == SimulationPlaybackAction::SingleStep)
    {
        Session->TriggerSingleStep(
            static_cast<uint32_t>(std::max(256.0f, CanvasBounds.Width)),
            static_cast<uint32_t>(std::max(256.0f, CanvasBounds.Height)));
    }
    else if (PbReading.TriggeredAction == SimulationPlaybackAction::TriggerBurst)
    {
        Session->TriggerBurst();
    }

    const Rectangle2D RulerBounds{ Bounds.X + 8.0f, PlaybackBounds.Bottom() + 4.0f, Bounds.Width - 16.0f, 46.0f };
    const SimulationRulerReading RulerReading = SimulationTimeRuler::Construct(
        Surface, RulerBounds, Session->RulerSpec, Ptr, 1.0f);
    if (RulerReading.Modified)
    {
        Session->PlaybackTuning.ElapsedSeconds = RulerReading.ScrubbedSeconds;
        Session->PlaybackTuning.LiveMode       = false;
    }

    // Open dropdown menus above viewport
    if (OverlayMenuOpen)
    {
        uint32_t Chosen = std::min<uint32_t>(OverlayIdx, 3u);
        const ControlContact Sheet = DropdownSelector::ConstructOptionSheet(
            Surface, OverlayBtn, OverlayLabels.data(), 4u, Chosen, Ptr, Chosen, 1.0f);
        if (Sheet.Pressed)
        {
            Session->OverlayMode = static_cast<ParticleOverlayMode>(Chosen);
            OverlayMenuOpen      = false;
        }
    }
    if (ProjectionMenuOpen)
    {
        uint32_t Chosen = 0u;
        const ControlContact Sheet = DropdownSelector::ConstructOptionSheet(
            Surface, ProjBtn, ProjectionLabels.data(), 3u, 0u, Ptr, Chosen, 1.0f);
        if (Sheet.Pressed)
        {
            if (Chosen == 1u)
            {
                Session->CameraPitchRad = 1.30f;
                Session->CameraYawRad   = 0.0f;
            }
            else if (Chosen == 2u)
            {
                Session->CameraPitchRad = 0.05f;
                Session->CameraYawRad   = 0.0f;
            }
            else
            {
                Session->CameraYawRad   = 0.45f;
                Session->CameraPitchRad = 0.30f;
            }
            ProjectionMenuOpen = false;
        }
    }
}

void ParticleViewportHost::PresentInImGuiPane(double DeltaSeconds, float ScaleFactor)
{
    const float  SafeScale = std::max(0.25f, ScaleFactor);
    const ImVec2 Pos       = ImGui::GetWindowPos();
    const ImVec2 Size      = ImGui::GetWindowSize();
    const float  TopY      = ImGui::GetCursorScreenPos().y - ImGui::GetStyle().WindowPadding.y;
    const float  PaneH     = std::max(1.0f, (Pos.y + Size.y) - TopY);
    PaneInputReading Input = WorkspaceAppearance::CaptureImGuiPaneInput({ Pos.x, TopY, Size.x, PaneH }, SafeScale);
    WorkspaceAppearance::EnforcePanePointerOwnership(this, Input);
    PixelSpace Surface(ImGui::GetWindowDrawList(), { Pos.x, TopY }, SafeScale);
    Render(Surface, { 0.0f, 0.0f, Size.x / SafeScale, PaneH / SafeScale }, Input, DeltaSeconds);
}

} // namespace Frontier::Workspaces
