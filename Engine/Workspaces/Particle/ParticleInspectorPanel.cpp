/*============================================================================================================================================
                                                        PARTICLEINSPECTORPANEL.CPP
============================================================================================================================================*/
// 🧩 Implementation of ParticleInspectorPanel reusing ColourRampEditor, ScalarCurveEditor, ScalarSlider & DiagnosticInfographicCanvas

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Particle/ParticleInspectorPanel.h"

#include "Engine/Components/Controls/ColourRampEditor.h"
#include "Engine/Components/Controls/DropdownSelector.h"
#include "Engine/Components/Controls/ScalarCurveEditor.h"
#include "Engine/Components/Controls/ScalarSlider.h"
#include "Engine/Components/Diagnostics/DiagnosticInfographicCanvas.h"
#include "Engine/Components/VectorIcons/VectorIconRepository.h"
#include "Engine/Workspaces/Particle/ParticlePaneAppearance.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdio>

namespace Frontier::Workspaces
{

namespace
{

constexpr std::array<const char*, 4u> TabTitles {{
    "Overview",
    "Source",
    "Motion & FX",
    "Units & Record",
}};

constexpr std::array<const char*, 15u> ScalarChannelLabels {{
    "0 · ModeDefault",
    "1 · NormalizedLifetime",
    "2 · ParticleAgeSeconds",
    "3 · VelocitySpeed",
    "4 · VerticalVelocity",
    "5 · OriginAzimuthAngle",
    "6 · VelocityHeadingAngle",
    "7 · DistanceFromOrigin",
    "8 · WorldElevation",
    "9 · SpatialNoise3D",
    "10 · PerParticleRandomSeed",
    "11 · SpinOrientationAngle",
    "12 · DistanceToCamera",
    "13 · ParticleSizeMeters",
    "14 · AssignedLifespanSeconds",
}};

[[nodiscard]] uint32_t QueryBokehApertureCode(const ParticleSpace::ParticleRig& Rig) noexcept
{
    if (const auto* Sprite = std::get_if<ParticleSpace::SpriteBillboardCarrier>(&Rig.Carrier))
    {
        return std::min<uint32_t>(static_cast<uint32_t>(Sprite->BokehAperture), 3u);
    }
    return 0u;
}

void AssignBokehApertureCode(ParticleSpace::ParticleRig& Rig, uint32_t Code) noexcept
{
    if (auto* Sprite = std::get_if<ParticleSpace::SpriteBillboardCarrier>(&Rig.Carrier))
    {
        Sprite->BokehAperture = static_cast<ParticleSpace::BokehApertureCategory>(std::min<uint32_t>(Code, 3u));
    }
}

} // namespace

ParticleInspectorPanel::~ParticleInspectorPanel() noexcept
{
    WorkspaceAppearance::ReleasePanePointerOwnership(this);
}

void ParticleInspectorPanel::AttachSession(ParticleWorkspaceSession* TargetSession) noexcept
{
    Session = TargetSession;
}

void ParticleInspectorPanel::Render(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const PaneInputReading& Input,
    double)
{
    const auto Theme = ParticleAppearance::QueryThemeColors();
    Surface.FillRectangle(Bounds, Theme.PanelBg, 0.0f);

    if (Session == nullptr)
    {
        return;
    }

    const ControlPointer Ptr = Input.PrimaryPointer;

    if (!Ptr.Down)
    {
        DraggingRate     = false;
        DraggingSize     = false;
        DraggingStrength = false;
        DraggingRadius   = false;
    }

    ParticleSpace::ParticleRig* Rig   = Session->AccessSelectedRig();
    auto*                       Force = Session->AccessSelectedForce();

    // 1. Breadcrumb + Reset pill
    const char* Breadcrumb = (Session->SelectionTarget == ParticleSelectionTarget::ForceSource && Force != nullptr)
        ? "Inspector / Environment / Force Field"
        : "Inspector / Effects / Silk & Curl";
    Surface.Text({ Bounds.X + 16.0f, Bounds.Y + 14.0f }, 11.0f, Theme.TextMuted, Breadcrumb);

    const Rectangle2D ResetBtn{ Bounds.Right() - 80.0f, Bounds.Y + 9.0f, 64.0f, 24.0f };
    const bool HoverReset = Ptr.Enabled && ResetBtn.Contains({ Ptr.X, Ptr.Y });
    Surface.FillRectangle(ResetBtn, HoverReset ? Theme.HoverBg : Theme.CardBg, 12.0f);
    Surface.StrokeRectangle(ResetBtn, Theme.BorderSubtle, 12.0f, 1.0f);
    Surface.Text({ ResetBtn.X + 13.0f, ResetBtn.Y + 5.5f }, 10.5f, Theme.TextPrimary, "Reset");
    if (HoverReset && Ptr.Pressed)
    {
        Session->RestartSimulation();
    }

    // 2. Active entity header card
    const Rectangle2D HeaderCard{ Bounds.X + 16.0f, Bounds.Y + 42.0f, Bounds.Width - 32.0f, 64.0f };
    Surface.FillRectangle(HeaderCard, Theme.CardBg, 14.0f);
    Surface.StrokeRectangle(HeaderCard, Theme.BorderSubtle, 14.0f, 1.0f);

    const Rectangle2D IconTile{ HeaderCard.X + 12.0f, HeaderCard.Y + 13.0f, 38.0f, 38.0f };
    Surface.FillRectangle(IconTile, Theme.InnerTileBg, 10.0f);
    Surface.StrokeRectangle(IconTile, Theme.BorderSubtle, 10.0f, 1.0f);
    VectorIconRepository::ConstructControl(
        Surface,
        (Session->SelectionTarget == ParticleSelectionTarget::ForceSource) ? ControlIcon::ForceVortex : ControlIcon::ParticleSparkles,
        { IconTile.X + 10.0f, IconTile.Y + 10.0f, 18.0f,
          (Session->SelectionTarget == ParticleSelectionTarget::ForceSource) ? Theme.ForceAccent : Theme.ParticleAccent, 1.5f });

    const char* HeaderTitle = (Session->SelectionTarget == ParticleSelectionTarget::ForceSource && Force != nullptr)
        ? Force->Label.c_str()
        : (Rig != nullptr ? Rig->RigTitle.c_str() : "No Rig Selected");
    const char* HeaderSub = (Session->SelectionTarget == ParticleSelectionTarget::ForceSource && Force != nullptr)
        ? "FORCE · BOUNDED LATTICE"
        : "VFX · ADD";
    Surface.Text({ IconTile.Right() + 12.0f, HeaderCard.Y + 15.0f }, 14.0f, Theme.TextPrimary, HeaderTitle);
    Surface.Text({ IconTile.Right() + 12.0f, HeaderCard.Y + 35.0f }, 9.5f, Theme.TextMuted, HeaderSub);

    // Toggle switch on right of header card
    bool EntityEnabled = (Session->SelectionTarget == ParticleSelectionTarget::ForceSource && Force != nullptr)
        ? Force->Enabled
        : (Rig != nullptr ? Rig->Enabled : false);
    const Rectangle2D SwitchRect{ HeaderCard.Right() - 56.0f, HeaderCard.Y + 19.0f, 44.0f, 24.0f };
    Surface.FillRectangle(SwitchRect, EntityEnabled ? Theme.WarmCreamBg : Theme.InnerTileBg, 12.0f);
    Surface.FillCircle(
        { EntityEnabled ? SwitchRect.Right() - 12.0f : SwitchRect.X + 12.0f, SwitchRect.Center().Y },
        8.5f,
        EntityEnabled ? Theme.WarmCreamInk : Theme.TextMuted);
    if (Ptr.Enabled && Ptr.Pressed && SwitchRect.Contains({ Ptr.X, Ptr.Y }))
    {
        if (Session->SelectionTarget == ParticleSelectionTarget::ForceSource && Force != nullptr)
        {
            Force->Enabled = !Force->Enabled;
            Session->RecordRevisionStep("Toggle Force");
        }
        else if (Rig != nullptr)
        {
            Rig->Enabled = !Rig->Enabled;
            Session->RecordRevisionStep("Toggle Rig");
        }
    }

    // 3. 4-Tab Bar
    const Rectangle2D TabBar{ Bounds.X + 16.0f, HeaderCard.Bottom() + 10.0f, Bounds.Width - 32.0f, 34.0f };
    Surface.FillRectangle(TabBar, Theme.CardBg, 17.0f);
    Surface.StrokeRectangle(TabBar, Theme.BorderSubtle, 17.0f, 1.0f);

    const float TabW = (TabBar.Width - 8.0f) * 0.25f;
    for (uint32_t Idx = 0u; Idx < 4u; ++Idx)
    {
        const Rectangle2D Tab{ TabBar.X + 4.0f + static_cast<float>(Idx) * TabW, TabBar.Y + 4.0f, TabW, TabBar.Height - 8.0f };
        const bool Active = (static_cast<uint32_t>(Session->ActiveInspectorTab) == Idx);
        const bool Hover  = Ptr.Enabled && Tab.Contains({ Ptr.X, Ptr.Y });
        if (Active || Hover)
        {
            Surface.FillRectangle(Tab, Active ? Theme.InnerTileBg : Theme.HoverBg, 13.0f);
        }
        Surface.Text(
            { Tab.X + 10.0f, Tab.Y + 6.5f },
            10.2f,
            Active ? Theme.TextPrimary : Theme.TextMuted,
            TabTitles[Idx]);
        if (Hover && Ptr.Pressed)
        {
            Session->ActiveInspectorTab = static_cast<ParticleInspectorTab>(Idx);
        }
    }

    float CursorY = TabBar.Bottom() + 12.0f;

    // 4. Tab Content
    if (Session->ActiveInspectorTab == ParticleInspectorTab::Overview && Rig != nullptr)
    {
        // Capacity card
        const Rectangle2D CapCard{ Bounds.X + 16.0f, CursorY, Bounds.Width - 32.0f, 250.0f };
        Surface.FillRectangle(CapCard, Theme.CardBg, 14.0f);
        Surface.StrokeRectangle(CapCard, Theme.BorderSubtle, 14.0f, 1.0f);

        Surface.FillCircle({ CapCard.X + 22.0f, CapCard.Y + 20.0f }, 8.0f, Theme.EmeraldSoft);
        VectorIconRepository::ConstructControl(Surface, ControlIcon::CircleCheck, { CapCard.X + 16.0f, CapCard.Y + 14.0f, 12.0f, Theme.EmeraldInk, 1.6f });
        Surface.Text({ CapCard.X + 36.0f, CapCard.Y + 14.0f }, 11.0f, Theme.TextPrimary, "Within capacity");
        Surface.Text({ CapCard.Right() - 96.0f, CapCard.Y + 14.0f }, 9.5f, Theme.TextMuted, "01 / LIVE SOLVER");

        Surface.Text({ CapCard.X + 14.0f, CapCard.Y + 38.0f }, 10.5f, Theme.TextMuted, "Particles alive");

        const auto Metrics = Session->QueryMetrics();
        char AliveBuf[24]{};
        std::snprintf(AliveBuf, sizeof(AliveBuf), "%u", Metrics.AliveParticles > 0u ? Metrics.AliveParticles : Rig->Sizing.CapacitySlots);
        Surface.Text({ CapCard.X + 14.0f, CapCard.Y + 54.0f }, 26.0f, Theme.TextPrimary, AliveBuf);

        char RateBuf[32]{};
        std::snprintf(RateBuf, sizeof(RateBuf), "%.0f /s", static_cast<double>(Rig->Sizing.ReleaseRatePerSec));
        Surface.Text({ CapCard.Right() - 70.0f, CapCard.Y + 56.0f }, 14.0f, Theme.TextPrimary, RateBuf);
        char CapBuf[32]{};
        std::snprintf(CapBuf, sizeof(CapBuf), "of %u cap", Rig->Sizing.CapacitySlots);
        Surface.Text({ CapCard.Right() - 78.0f, CapCard.Y + 74.0f }, 9.5f, Theme.TextMuted, CapBuf);

        // DiagnosticInfographicCanvas inside Capacity card
        const Rectangle2D InfoBounds{ CapCard.X + 12.0f, CapCard.Y + 96.0f, CapCard.Width - 24.0f, 142.0f };
        DiagnosticInfographicSpec InfoSpec{};
        InfoSpec.Archetype          = InfographicArchetype::OpticalBokehAperture;
        InfoSpec.AnimationSeconds   = Session->PlaybackTuning.ElapsedSeconds;
        InfoSpec.ActiveApertureCode = QueryBokehApertureCode(*Rig);
        InfoSpec.PrimaryScalar      = 1.0f;
        const DiagnosticInfographicReading InfoRead = DiagnosticInfographicCanvas::Construct(
            Surface, InfoBounds, InfoSpec, Ptr, 1.0f);
        if (InfoRead.Modified)
        {
            AssignBokehApertureCode(*Rig, InfoRead.ChosenApertureCode);
            Session->RecordRevisionStep("Switch Bokeh Aperture");
        }
        CursorY = CapCard.Bottom() + 10.0f;

        // 3 Summary metric tiles (Shape, Manifold, Wind coupling highlighted warm cream!)
        const float TileGap = 8.0f;
        const float TileW   = (Bounds.Width - 32.0f - TileGap * 2.0f) / 3.0f;
        const Rectangle2D Tile0{ Bounds.X + 16.0f, CursorY, TileW, 88.0f };
        const Rectangle2D Tile1{ Tile0.Right() + TileGap, CursorY, TileW, 88.0f };
        const Rectangle2D Tile2{ Tile1.Right() + TileGap, CursorY, TileW, 88.0f };

        Surface.FillRectangle(Tile0, Theme.CardBg, 12.0f);
        Surface.StrokeRectangle(Tile0, Theme.BorderSubtle, 12.0f, 1.0f);
        Surface.Text({ Tile0.X + 12.0f, Tile0.Y + 12.0f }, 9.5f, Theme.TextMuted, "Shape");
        Surface.Text({ Tile0.X + 12.0f, Tile0.Y + 30.0f }, 13.0f, Theme.TextPrimary, "Bokeh");
        Surface.Text({ Tile0.X + 12.0f, Tile0.Y + 46.0f }, 13.0f, Theme.TextPrimary, "Bead");
        Surface.Text({ Tile0.X + 12.0f, Tile0.Bottom() - 18.0f }, 9.0f, Theme.TextFaint, "additive glow");

        Surface.FillRectangle(Tile1, Theme.CardBg, 12.0f);
        Surface.StrokeRectangle(Tile1, Theme.BorderSubtle, 12.0f, 1.0f);
        Surface.Text({ Tile1.X + 12.0f, Tile1.Y + 12.0f }, 9.5f, Theme.TextMuted, "Manifold");
        Surface.Text({ Tile1.X + 12.0f, Tile1.Y + 30.0f }, 13.0f, Theme.TextPrimary, "Curl");
        Surface.Text({ Tile1.X + 12.0f, Tile1.Y + 46.0f }, 13.0f, Theme.TextPrimary, "ribbon");
        char FlowSub[32]{};
        std::snprintf(FlowSub, sizeof(FlowSub), "%.1f m/s flow", static_cast<double>(Rig->Dynamics.Curl.DriveSpeed));
        Surface.Text({ Tile1.X + 12.0f, Tile1.Bottom() - 18.0f }, 9.0f, Theme.TextFaint, FlowSub);

        Surface.FillRectangle(Tile2, Theme.WarmCreamBg, 12.0f);
        Surface.Text({ Tile2.X + 12.0f, Tile2.Y + 12.0f }, 9.5f, Theme.WarmCreamInk, "Wind coupling");
        char WindVal[32]{};
        std::snprintf(WindVal, sizeof(WindVal), "%.2f /s", static_cast<double>(Rig->Dynamics.ForceLink.WindCouplingRate));
        Surface.Text({ Tile2.X + 12.0f, Tile2.Y + 34.0f }, 15.0f, Theme.WarmCreamInk, WindVal);
        Surface.Text({ Tile2.X + 12.0f, Tile2.Bottom() - 18.0f }, 9.0f, ColorRgba{ 78u, 78u, 68u, 255u }, "lattice coupling");
        CursorY += 98.0f;

        // QUICK CONTROLS 4-tile grid
        const Rectangle2D QuickCard{ Bounds.X + 16.0f, CursorY, Bounds.Width - 32.0f, 118.0f };
        Surface.FillRectangle(QuickCard, Theme.CardBg, 14.0f);
        Surface.StrokeRectangle(QuickCard, Theme.BorderSubtle, 14.0f, 1.0f);
        Surface.Text({ QuickCard.X + 14.0f, QuickCard.Y + 12.0f }, 10.0f, Theme.TextMuted, "QUICK CONTROLS");
        Surface.Text({ QuickCard.Right() - 88.0f, QuickCard.Y + 12.0f }, 9.5f, Theme.TextFaint, "SILK & CURL");

        const float QGap = 8.0f;
        const float QW   = (QuickCard.Width - 24.0f - QGap * 3.0f) * 0.25f;
        const float QY   = QuickCard.Y + 32.0f;
        const float QH   = 74.0f;

        struct QuickTileSpec
        {
            const char* Label;
            ControlIcon Icon;
            bool        On;
        };
        const bool IsAdditive = std::visit([](const auto& C) -> bool
        {
            if constexpr (requires { C.BlendEquation; })
            {
                return C.BlendEquation == ColourSpace::OpticalBlendEquation::Additive;
            }
            return true;
        }, Rig->Carrier);
        const std::array<QuickTileSpec, 4u> QuickTiles {{
            { "Visible",       ControlIcon::EyeVisible,       Rig->Enabled },
            { "Wind-driven",   ControlIcon::ForceWind,        Rig->Dynamics.Curl.Mode != ParticleSpace::CurlManifoldMode::Off },
            { "Additive glow", ControlIcon::ParticleSparkles, IsAdditive },
            { "Floor bounce",  ControlIcon::ForceAttract,     Rig->Dynamics.Ballistics.BounceRestitution > 0.01f },
        }};

        for (uint32_t Idx = 0u; Idx < 4u; ++Idx)
        {
            const Rectangle2D QT{ QuickCard.X + 12.0f + static_cast<float>(Idx) * (QW + QGap), QY, QW, QH };
            const bool On    = QuickTiles[Idx].On;
            const bool Hover = Ptr.Enabled && QT.Contains({ Ptr.X, Ptr.Y });
            Surface.FillRectangle(QT, On ? Theme.EmeraldSoft : (Hover ? Theme.HoverBg : Theme.InnerTileBg), 10.0f);
            Surface.StrokeRectangle(QT, On ? Theme.EmeraldInk : Theme.BorderSubtle, 10.0f, 1.0f);
            VectorIconRepository::ConstructControl(
                Surface, QuickTiles[Idx].Icon, { QT.Center().X - 7.0f, QT.Y + 11.0f, 14.0f, On ? Theme.EmeraldInk : Theme.TextMuted, 1.5f });
            const float LabelW = Surface.MeasureTextWidth(9.0f, QuickTiles[Idx].Label);
            Surface.Text({ QT.Center().X - LabelW * 0.5f, QT.Bottom() - 28.0f }, 9.0f, Theme.TextPrimary, QuickTiles[Idx].Label);
            const char* StateStr = On ? "ON" : "OFF";
            const float StateW = Surface.MeasureTextWidth(8.5f, StateStr);
            Surface.Text({ QT.Center().X - StateW * 0.5f, QT.Bottom() - 15.0f }, 8.5f, On ? Theme.EmeraldInk : Theme.TextFaint, StateStr);

            if (Hover && Ptr.Pressed)
            {
                if (Idx == 0u) Rig->Enabled = !Rig->Enabled;
                else if (Idx == 1u)
                {
                    Rig->Dynamics.Curl.Mode = (Rig->Dynamics.Curl.Mode == ParticleSpace::CurlManifoldMode::Off)
                        ? ParticleSpace::CurlManifoldMode::BokehStreamRibbons
                        : ParticleSpace::CurlManifoldMode::Off;
                }
                else if (Idx == 2u)
                {
                    std::visit([](auto& C)
                    {
                        if constexpr (requires { C.BlendEquation; })
                        {
                            C.BlendEquation = (C.BlendEquation == ColourSpace::OpticalBlendEquation::Additive)
                                ? ColourSpace::OpticalBlendEquation::PremultipliedAlpha
                                : ColourSpace::OpticalBlendEquation::Additive;
                        }
                    }, Rig->Carrier);
                }
                else if (Idx == 3u)
                {
                    Rig->Dynamics.Ballistics.BounceRestitution =
                        (Rig->Dynamics.Ballistics.BounceRestitution > 0.01f) ? 0.0f : 0.55f;
                }
                Session->RecordRevisionStep("Toggle Quick Control");
            }
        }
    }
    else if (Session->ActiveInspectorTab == ParticleInspectorTab::Source && Rig != nullptr)
    {
        // Release Rate + Particle Size sliders
        const Rectangle2D RateRow{ Bounds.X + 16.0f, CursorY, Bounds.Width - 32.0f, 32.0f };
        char RateBuf[24]{};
        std::snprintf(RateBuf, sizeof(RateBuf), "%.0f", static_cast<double>(Rig->Sizing.ReleaseRatePerSec));
        float NewRate = Rig->Sizing.ReleaseRatePerSec;
        const ControlContact RateContact = ScalarSlider::ConstructWithReadout(
            Surface, RateRow, RateBuf, "/s", 10.0f, 2500.0f, Rig->Sizing.ReleaseRatePerSec, DraggingRate, Ptr, NewRate);
        if (RateContact.Pressed) DraggingRate = true;
        if (DraggingRate) Rig->Sizing.ReleaseRatePerSec = NewRate;
        CursorY += 40.0f;

        const Rectangle2D SizeRow{ Bounds.X + 16.0f, CursorY, Bounds.Width - 32.0f, 32.0f };
        char SizeBuf[24]{};
        std::snprintf(SizeBuf, sizeof(SizeBuf), "%.3f", static_cast<double>(Rig->Sizing.SizeStartMeters));
        float NewSize = Rig->Sizing.SizeStartMeters;
        const ControlContact SizeContact = ScalarSlider::ConstructWithReadout(
            Surface, SizeRow, SizeBuf, "m", 0.005f, 0.25f, Rig->Sizing.SizeStartMeters, DraggingSize, Ptr, NewSize);
        if (SizeContact.Pressed) DraggingSize = true;
        if (DraggingSize) Rig->Sizing.SizeStartMeters = NewSize;
        CursorY += 44.0f;

        // Scalar Channel dropdown (15 HTML COLOR_BY_MODES)
        const uint32_t ChanIdx = std::min<uint32_t>(static_cast<uint32_t>(Rig->TintBinding.ScalarChannel), 14u);
        const Rectangle2D ChanBounds{ Bounds.X + 16.0f, CursorY, Bounds.Width - 32.0f, 30.0f };
        const ControlContact ChanBtn = DropdownSelector::ConstructButton(
            Surface, ChanBounds, ScalarChannelLabels[ChanIdx], ChannelMenuOpen, Ptr, 1.0f);
        if (ChanBtn.Pressed) ChannelMenuOpen = !ChannelMenuOpen;
        CursorY += 38.0f;

        // Reusable ColourRampEditor + Content Browser Drop Slot
        const Rectangle2D RampBounds{ Bounds.X + 16.0f, CursorY, Bounds.Width - 32.0f, 174.0f };
        const bool HighlightDrop = (BoundBrowserPanel != nullptr && BoundBrowserPanel->InspectReading().DraggingColourRampActive);
        if (ColourRampEditor::Construct(
                Surface,
                RampBounds,
                "Rig Colour Ramp & Thermal Driver",
                Rig->TintBinding.SourceProfile,
                Session->RampEditorSession,
                Ptr,
                HighlightDrop,
                1.0f))
        {
            Rig->RebuildBakedColourLut();
            Session->RecordRevisionStep("Edit Colour Ramp");
        }
        LastRampStripBounds = Session->RampEditorSession.LastStripBounds;

        if (BoundBrowserPanel != nullptr)
        {
            ColourRampEditor::RegisterDropSlot(
                *BoundBrowserPanel,
                "particle.rig.colour_ramp",
                "Active Particle Rig Colour Ramp",
                Session->RampEditorSession,
                &Rig->TintBinding.SourceProfile,
                [this](const ColourSpace::ColourSourceProfile& Profile, std::string_view Title)
                {
                    if (Session != nullptr)
                    {
                        Session->AssignActiveRigColourProfile(Profile, Title);
                    }
                });
        }

        if (ChannelMenuOpen)
        {
            uint32_t Chosen = ChanIdx;
            const ControlContact Sheet = DropdownSelector::ConstructOptionSheet(
                Surface, ChanBounds, ScalarChannelLabels.data(), 8u, ChanIdx, Ptr, Chosen, 1.0f);
            if (Sheet.Pressed)
            {
                Rig->TintBinding.ScalarChannel = static_cast<ParticleSpace::ParticleScalarChannel>(Chosen);
                ChannelMenuOpen                = false;
                Session->RecordRevisionStep("Change Scalar Channel");
            }
        }
    }
    else if (Session->ActiveInspectorTab == ParticleInspectorTab::MotionEffects)
    {
        if (Session->SelectionTarget == ParticleSelectionTarget::ForceSource && Force != nullptr)
        {
            const Rectangle2D StrRow{ Bounds.X + 16.0f, CursorY, Bounds.Width - 32.0f, 32.0f };
            char StrBuf[24]{};
            std::snprintf(StrBuf, sizeof(StrBuf), "%.1f", static_cast<double>(Force->Strength));
            float NewStr = Force->Strength;
            const ControlContact StrContact = ScalarSlider::ConstructWithReadout(
                Surface, StrRow, StrBuf, "m/s", 0.0f, 30.0f, Force->Strength, DraggingStrength, Ptr, NewStr);
            if (StrContact.Pressed) DraggingStrength = true;
            if (DraggingStrength) Force->Strength = NewStr;
            CursorY += 42.0f;

            const Rectangle2D RadRow{ Bounds.X + 16.0f, CursorY, Bounds.Width - 32.0f, 32.0f };
            char RadBuf[24]{};
            std::snprintf(RadBuf, sizeof(RadBuf), "%.1f", static_cast<double>(Force->RadiusMeters));
            float NewRad = Force->RadiusMeters;
            const ControlContact RadContact = ScalarSlider::ConstructWithReadout(
                Surface, RadRow, RadBuf, "m", 0.5f, 12.0f, Force->RadiusMeters, DraggingRadius, Ptr, NewRad);
            if (RadContact.Pressed) DraggingRadius = true;
            if (DraggingRadius) Force->RadiusMeters = NewRad;
            CursorY += 46.0f;
        }

        const Rectangle2D SizeCurveBounds{ Bounds.X + 16.0f, CursorY, Bounds.Width - 32.0f, 146.0f };
        (void)ScalarCurveEditor::Construct(
            Surface, SizeCurveBounds, "Size Over Life", Session->SizeOverLifeCurve, Session->SizeCurveSession, Ptr, Theme.ParticleAccent, 1.0f);
        CursorY += 156.0f;

        const Rectangle2D OpCurveBounds{ Bounds.X + 16.0f, CursorY, Bounds.Width - 32.0f, 146.0f };
        (void)ScalarCurveEditor::Construct(
            Surface, OpCurveBounds, "Opacity Over Life", Session->OpacityOverLifeCurve, Session->OpacityCurveSession, Ptr, Theme.CyanAccent, 1.0f);
    }
    else if (Session->ActiveInspectorTab == ParticleInspectorTab::UnitsRecord && Rig != nullptr)
    {
        const Rectangle2D RecCard{ Bounds.X + 16.0f, CursorY, Bounds.Width - 32.0f, 140.0f };
        Surface.FillRectangle(RecCard, Theme.CardBg, 14.0f);
        Surface.StrokeRectangle(RecCard, Theme.BorderSubtle, 14.0f, 1.0f);
        Surface.Text({ RecCard.X + 14.0f, RecCard.Y + 14.0f }, 12.0f, Theme.TextPrimary, "Revision & .ParticleSpace Codec");

        char RevText[64]{};
        std::snprintf(
            RevText,
            sizeof(RevText),
            "History steps: %u  |  Cursor: %u  |  Modified: %s",
            Session->InspectRevision().QueryLiveStepCount(),
            Session->InspectRevision().QueryCursorStepIndex(),
            Session->InspectRevision().QueryModifiedSinceSave() ? "Yes" : "Clean");
        Surface.Text({ RecCard.X + 14.0f, RecCard.Y + 40.0f }, 10.5f, Theme.TextMuted, RevText);

        const Rectangle2D UndoBtn{ RecCard.X + 14.0f, RecCard.Y + 72.0f, 84.0f, 30.0f };
        const Rectangle2D RedoBtn{ UndoBtn.Right() + 8.0f, RecCard.Y + 72.0f, 84.0f, 30.0f };
        Surface.FillRectangle(UndoBtn, Theme.InnerTileBg, 10.0f);
        Surface.StrokeRectangle(UndoBtn, Theme.BorderSubtle, 10.0f, 1.0f);
        Surface.Text({ UndoBtn.X + 24.0f, UndoBtn.Y + 8.0f }, 11.0f, Theme.TextPrimary, "Undo");
        if (Ptr.Enabled && Ptr.Pressed && UndoBtn.Contains({ Ptr.X, Ptr.Y }))
        {
            (void)Session->Undo();
        }

        Surface.FillRectangle(RedoBtn, Theme.InnerTileBg, 10.0f);
        Surface.StrokeRectangle(RedoBtn, Theme.BorderSubtle, 10.0f, 1.0f);
        Surface.Text({ RedoBtn.X + 24.0f, RedoBtn.Y + 8.0f }, 11.0f, Theme.TextPrimary, "Redo");
        if (Ptr.Enabled && Ptr.Pressed && RedoBtn.Contains({ Ptr.X, Ptr.Y }))
        {
            (void)Session->Redo();
        }
    }
}

void ParticleInspectorPanel::PresentInImGuiPane(double DeltaSeconds, float ScaleFactor)
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
