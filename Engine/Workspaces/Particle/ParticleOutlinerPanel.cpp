/*============================================================================================================================================
                                                         PARTICLEOUTLINERPANEL.CPP
============================================================================================================================================*/
// 🧩 Implementation of ParticleOutlinerPanel matching HTML Particle Studio Outliner hierarchy, census tiles & category chips

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Particle/ParticleOutlinerPanel.h"

#include "Engine/Components/Controls/DropdownSelector.h"
#include "Engine/Components/VectorIcons/VectorIconRepository.h"
#include "Engine/Workspaces/Particle/ParticlePaneAppearance.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace Frontier::Workspaces
{

namespace
{

[[nodiscard]] bool ContainsIgnoreCase(std::string_view Haystack, std::string_view Needle) noexcept
{
    if (Needle.empty())
    {
        return true;
    }
    if (Haystack.size() < Needle.size())
    {
        return false;
    }
    for (std::size_t Start = 0u; Start + Needle.size() <= Haystack.size(); ++Start)
    {
        bool Match = true;
        for (std::size_t I = 0u; I < Needle.size(); ++I)
        {
            if (std::tolower(static_cast<unsigned char>(Haystack[Start + I])) !=
                std::tolower(static_cast<unsigned char>(Needle[I])))
            {
                Match = false;
                break;
            }
        }
        if (Match)
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] ControlIcon QueryForceIcon(SimulationSpace::ForceFields::ForceModel Model) noexcept
{
    using SimulationSpace::ForceFields::ForceModel;
    switch (Model)
    {
    case ForceModel::RotationalColumn: return ControlIcon::ForceVortex;
    case ForceModel::Attractor:        return ControlIcon::ForceAttract;
    case ForceModel::Repulsor:         return ControlIcon::ForceRepel;
    case ForceModel::MagneticDipole:   return ControlIcon::ForceMagnetic;
    default:                           return ControlIcon::ForceWind;
    }
}

[[nodiscard]] const char* QueryForceSubtitle(const SimulationSpace::ForceFields::ForceSourceRecordEntry& Src) noexcept
{
    using SimulationSpace::ForceFields::ForceModel;
    switch (Src.Model)
    {
    case ForceModel::LinearWind:       return "Wind Generator";
    case ForceModel::RotationalColumn: return "Tornado / Vortex";
    case ForceModel::Attractor:        return "Attractor Zone";
    case ForceModel::Repulsor:         return "Repulsor Pulse";
    case ForceModel::MagneticDipole:   return "Magnetic Dipole";
    default:                           return "Bounded Vector Field";
    }
}

} // namespace

ParticleOutlinerPanel::~ParticleOutlinerPanel() noexcept
{
    WorkspaceAppearance::ReleasePanePointerOwnership(this);
}

void ParticleOutlinerPanel::AttachSession(ParticleWorkspaceSession* TargetSession) noexcept
{
    Session = TargetSession;
}

void ParticleOutlinerPanel::Render(
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

    ParticleSpace::ParticleDocument& Doc = Session->AccessDocument();

    // Header row: Outliner + Pause/Add buttons
    Surface.Text({ Bounds.X + 16.0f, Bounds.Y + 14.0f }, 17.5f, Theme.TextPrimary, "Outliner");

    const Rectangle2D PauseBtn{ Bounds.Right() - 72.0f, Bounds.Y + 12.0f, 26.0f, 26.0f };
    const Rectangle2D AddBtn{ Bounds.Right() - 40.0f, Bounds.Y + 12.0f, 26.0f, 26.0f };
    Surface.FillRectangle(PauseBtn, Theme.InnerTileBg, 13.0f);
    Surface.StrokeRectangle(PauseBtn, Theme.BorderSubtle, 13.0f, 1.0f);
    VectorIconRepository::ConstructControl(
        Surface,
        Session->PlaybackTuning.Playing ? ControlIcon::ParticlePause : ControlIcon::ParticlePlay,
        { PauseBtn.X + 6.0f, PauseBtn.Y + 6.0f, 14.0f, Theme.TextPrimary, 1.6f });
    if (Ptr.Enabled && Ptr.Pressed && PauseBtn.Contains({ Ptr.X, Ptr.Y }))
    {
        Session->PlaybackTuning.Playing = !Session->PlaybackTuning.Playing;
    }

    Surface.FillRectangle(AddBtn, Theme.InnerTileBg, 13.0f);
    Surface.StrokeRectangle(AddBtn, Theme.BorderSubtle, 13.0f, 1.0f);
    VectorIconRepository::ConstructControl(
        Surface,
        ControlIcon::PlusAdd,
        { AddBtn.X + 6.0f, AddBtn.Y + 6.0f, 14.0f, Theme.TextPrimary, 1.6f });
    if (Ptr.Enabled && Ptr.Pressed && AddBtn.Contains({ Ptr.X, Ptr.Y }))
    {
        AddMenuOpen = !AddMenuOpen;
    }

    char Subtitle[64]{};
    const std::size_t TotalEntries = Doc.Rigs.size() + Doc.ForceSources.size();
    std::snprintf(Subtitle, sizeof(Subtitle), "Particle & force scene · %zu items", TotalEntries);
    Surface.Text({ Bounds.X + 16.0f, Bounds.Y + 38.0f }, 11.0f, Theme.TextMuted, Subtitle);

    // Visible / Hidden Census KPI cards
    uint32_t VisibleCount = 0u;
    uint32_t HiddenCount  = 0u;
    for (const ParticleSpace::ParticleRig& Rig : Doc.Rigs)
    {
        if (Rig.Enabled) ++VisibleCount;
        else ++HiddenCount;
    }
    for (const auto& Force : Doc.ForceSources)
    {
        if (Force.Enabled) ++VisibleCount;
        else ++HiddenCount;
    }

    const float KpiW = (Bounds.Width - 40.0f) * 0.5f;
    const Rectangle2D VisibleTile{ Bounds.X + 16.0f, Bounds.Y + 58.0f, KpiW, 56.0f };
    const Rectangle2D HiddenTile{ VisibleTile.Right() + 8.0f, Bounds.Y + 58.0f, KpiW, 56.0f };

    auto DrawKpi = [&](Rectangle2D Tile, ControlIcon Icon, ColorRgba IconBg, ColorRgba IconInk, const char* Label, uint32_t Count)
    {
        Surface.FillRectangle(Tile, Theme.CardBg, 12.0f);
        Surface.StrokeRectangle(Tile, Theme.BorderSubtle, 12.0f, 1.0f);
        Surface.FillCircle({ Tile.X + 20.0f, Tile.Y + 20.0f }, 10.0f, IconBg);
        VectorIconRepository::ConstructControl(Surface, Icon, { Tile.X + 13.0f, Tile.Y + 13.0f, 14.0f, IconInk, 1.6f });
        Surface.Text({ Tile.X + 12.0f, Tile.Bottom() - 18.0f }, 10.5f, Theme.TextMuted, Label);

        char NumBuf[16]{};
        std::snprintf(NumBuf, sizeof(NumBuf), "%u", Count);
        Surface.Text({ Tile.Right() - 28.0f, Tile.Y + 18.0f }, 20.0f, Theme.TextPrimary, NumBuf);
    };

    DrawKpi(VisibleTile, ControlIcon::CircleCheck, Theme.EmeraldSoft, Theme.EmeraldInk, "Visible", VisibleCount);
    DrawKpi(HiddenTile, ControlIcon::EyeHidden, Theme.InnerTileBg, Theme.TextMuted, "Hidden", HiddenCount);

    // Search input
    const Rectangle2D SearchBounds{ Bounds.X + 16.0f, Bounds.Y + 124.0f, Bounds.Width - 32.0f, 30.0f };
    Surface.FillRectangle(SearchBounds, Theme.CardBg, 10.0f);
    Surface.StrokeRectangle(SearchBounds, Theme.BorderSubtle, 10.0f, 1.0f);
    VectorIconRepository::ConstructControl(Surface, ControlIcon::SearchGlass, { SearchBounds.X + 10.0f, SearchBounds.Y + 8.0f, 14.0f, Theme.TextMuted, 1.5f });
    Surface.Text(
        { SearchBounds.X + 30.0f, SearchBounds.Y + 8.5f },
        11.5f,
        SearchText.empty() ? Theme.TextFaint : Theme.TextPrimary,
        SearchText.empty() ? "Search scene..." : SearchText.c_str());
    Surface.Text({ SearchBounds.Right() - 42.0f, SearchBounds.Y + 9.0f }, 10.0f, Theme.TextFaint, "Ctrl F");

    // Category filter chips matching HTML Studio
    struct ChipDef
    {
        OutlinerFilterCategory Cat;
        const char*            Label;
    };
    constexpr std::array<ChipDef, 18u> Chips {{
        { OutlinerFilterCategory::All,        "All" },
        { OutlinerFilterCategory::Effects,    "Effects" },
        { OutlinerFilterCategory::MoGraph,    "MoGraph" },
        { OutlinerFilterCategory::Cloth,      "Cloth" },
        { OutlinerFilterCategory::Flora,      "Flora" },
        { OutlinerFilterCategory::Ferro,      "Ferro" },
        { OutlinerFilterCategory::Fracture,   "Fracture" },
        { OutlinerFilterCategory::Curl,       "Curl" },
        { OutlinerFilterCategory::Sparks,     "Sparks" },
        { OutlinerFilterCategory::Pyro,       "Pyro" },
        { OutlinerFilterCategory::Derez,      "Derez" },
        { OutlinerFilterCategory::Lens,       "Lens" },
        { OutlinerFilterCategory::Shield,     "Shield" },
        { OutlinerFilterCategory::Sky,        "Sky" },
        { OutlinerFilterCategory::Wind,       "Wind" },
        { OutlinerFilterCategory::Weather,    "Weather" },
        { OutlinerFilterCategory::Quarantine, "Quarantine" },
        { OutlinerFilterCategory::Forces,     "Forces" },
    }};

    float ChipX = Bounds.X + 16.0f;
    float ChipY = Bounds.Y + 164.0f;
    for (const ChipDef& Def : Chips)
    {
        const float W = static_cast<float>(std::strlen(Def.Label)) * 6.4f + 18.0f;
        if (ChipX + W > Bounds.Right() - 14.0f)
        {
            ChipX = Bounds.X + 16.0f;
            ChipY += 24.0f;
        }
        const Rectangle2D ChipRect{ ChipX, ChipY, W, 20.0f };
        const bool Active = (ActiveFilter == Def.Cat);
        const bool Hover  = Ptr.Enabled && ChipRect.Contains({ Ptr.X, Ptr.Y });
        Surface.FillRectangle(ChipRect, Active ? Theme.WarmCreamBg : (Hover ? Theme.HoverBg : Theme.CardBg), 10.0f);
        Surface.StrokeRectangle(ChipRect, Active ? Theme.WarmCreamBg : Theme.BorderSubtle, 10.0f, 1.0f);
        Surface.Text(
            { ChipRect.X + 9.0f, ChipRect.Y + 4.0f },
            10.0f,
            Active ? Theme.WarmCreamInk : Theme.TextMuted,
            Def.Label);
        if (Hover && Ptr.Pressed)
        {
            ActiveFilter = Def.Cat;
        }
        ChipX += W + 5.0f;
    }

    float CursorY = ChipY + 34.0f;

    // ENVIRONMENT section (Force Sources + Ambient Wind)
    if (ActiveFilter == OutlinerFilterCategory::All || ActiveFilter == OutlinerFilterCategory::Forces)
    {
        Surface.Text({ Bounds.X + 16.0f, CursorY }, 10.0f, Theme.TextFaint, "ENVIRONMENT");
        char EnvCount[16]{};
        std::snprintf(EnvCount, sizeof(EnvCount), "%zu", Doc.ForceSources.size() + 1u);
        Surface.Text({ Bounds.Right() - 26.0f, CursorY }, 10.0f, Theme.TextFaint, EnvCount);
        CursorY += 18.0f;

        // Ambient wind row
        const Rectangle2D WindRow{ Bounds.X + 14.0f, CursorY, Bounds.Width - 28.0f, 40.0f };
        Surface.FillRectangle(WindRow, Theme.CardBg, 9.0f);
        VectorIconRepository::ConstructControl(Surface, ControlIcon::ForceWind, { WindRow.X + 10.0f, WindRow.Y + 12.0f, 16.0f, Theme.CyanAccent, 1.5f });
        Surface.Text({ WindRow.X + 34.0f, WindRow.Y + 7.0f }, 11.5f, Theme.TextPrimary, "Ambient Wind");
        char WindSub[48]{};
        std::snprintf(WindSub, sizeof(WindSub), "%.1f m/s · global breeze", static_cast<double>(Doc.AmbientWindMps.X));
        Surface.Text({ WindRow.X + 34.0f, WindRow.Y + 22.0f }, 9.5f, Theme.TextMuted, WindSub);
        Surface.FillCircle({ WindRow.Right() - 12.0f, WindRow.Y + 20.0f }, 3.5f, Theme.EmeraldInk);
        CursorY += 46.0f;

        for (std::size_t Idx = 0u; Idx < Doc.ForceSources.size(); ++Idx)
        {
            if (CursorY + 44.0f > Bounds.Bottom() - 180.0f)
            {
                break;
            }
            auto& Force = Doc.ForceSources[Idx];
            if (!ContainsIgnoreCase(Force.Label, SearchText))
            {
                continue;
            }
            const bool Selected = (Session->SelectionTarget == ParticleSelectionTarget::ForceSource &&
                                   Session->SelectedForceIndex == Idx);
            const Rectangle2D RowRect{ Bounds.X + 14.0f, CursorY, Bounds.Width - 28.0f, 42.0f };
            const bool Hover = Ptr.Enabled && RowRect.Contains({ Ptr.X, Ptr.Y });
            Surface.FillRectangle(RowRect, Selected ? Theme.HoverBg : (Hover ? Theme.InnerTileBg : Theme.CardBg), 9.0f);
            if (Selected)
            {
                Surface.StrokeRectangle(RowRect, Theme.ForceAccent, 9.0f, 1.2f);
            }

            VectorIconRepository::ConstructControl(
                Surface,
                QueryForceIcon(Force.Model),
                { RowRect.X + 10.0f, RowRect.Y + 13.0f, 16.0f, Theme.ForceAccent, 1.5f });
            Surface.Text({ RowRect.X + 34.0f, RowRect.Y + 7.0f }, 11.5f, Theme.TextPrimary, Force.Label.c_str());

            char Sub[64]{};
            std::snprintf(Sub, sizeof(Sub), "%s · %.1f m/s", QueryForceSubtitle(Force), static_cast<double>(Force.Strength));
            Surface.Text({ RowRect.X + 34.0f, RowRect.Y + 23.0f }, 9.5f, Theme.TextMuted, Sub);

            // ACTIVE / WAITING pill
            const Rectangle2D Badge{ RowRect.Right() - 74.0f, RowRect.Y + 12.0f, 48.0f, 17.0f };
            Surface.FillRectangle(Badge, Force.Enabled ? Theme.EmeraldSoft : Theme.AmberSoft, 5.0f);
            Surface.Text(
                { Badge.X + 7.0f, Badge.Y + 3.0f },
                8.5f,
                Force.Enabled ? Theme.EmeraldInk : Theme.AmberInk,
                Force.Enabled ? "ACTIVE" : "PAUSED");

            const Point2D DotCenter{ RowRect.Right() - 12.0f, RowRect.Y + 21.0f };
            Surface.FillCircle(DotCenter, 3.8f, Force.Enabled ? Theme.EmeraldInk : Theme.TextFaint);

            if (Hover && Ptr.Pressed)
            {
                const Rectangle2D DotHit{ DotCenter.X - 10.0f, DotCenter.Y - 10.0f, 20.0f, 20.0f };
                if (DotHit.Contains({ Ptr.X, Ptr.Y }))
                {
                    Force.Enabled = !Force.Enabled;
                    Session->RecordRevisionStep("Toggle Force Source");
                }
                else
                {
                    Session->SelectForceSource(Idx);
                }
            }
            CursorY += 46.0f;
        }
        CursorY += 8.0f;
    }

    // PARTICLE EFFECTS section
    if (ActiveFilter != OutlinerFilterCategory::Forces)
    {
        Surface.Text({ Bounds.X + 16.0f, CursorY }, 10.0f, Theme.TextFaint, "PARTICLE EFFECTS");
        char RigCountBuf[16]{};
        std::snprintf(RigCountBuf, sizeof(RigCountBuf), "%zu", Doc.Rigs.size());
        Surface.Text({ Bounds.Right() - 26.0f, CursorY }, 10.0f, Theme.TextFaint, RigCountBuf);
        CursorY += 18.0f;

        for (ParticleSpace::ParticleRig& Rig : Doc.Rigs)
        {
            if (CursorY + 46.0f > Bounds.Bottom() - 8.0f)
            {
                break;
            }
            if (!ContainsIgnoreCase(Rig.RigTitle, SearchText))
            {
                continue;
            }
            const bool Selected = (Session->SelectionTarget == ParticleSelectionTarget::Rig &&
                                   Session->SelectedRigId == Rig.RigId);
            const Rectangle2D RowRect{ Bounds.X + 14.0f, CursorY, Bounds.Width - 28.0f, 44.0f };
            const bool Hover = Ptr.Enabled && RowRect.Contains({ Ptr.X, Ptr.Y });
            Surface.FillRectangle(RowRect, Selected ? Theme.HoverBg : (Hover ? Theme.InnerTileBg : Theme.CardBg), 10.0f);
            Surface.StrokeRectangle(RowRect, Selected ? Theme.ParticleAccent : Theme.BorderSubtle, 10.0f, Selected ? 1.2f : 1.0f);

            VectorIconRepository::ConstructControl(
                Surface,
                ControlIcon::ParticleSparkles,
                { RowRect.X + 10.0f, RowRect.Y + 14.0f, 16.0f, Theme.ParticleAccent, 1.5f });
            Surface.Text({ RowRect.X + 34.0f, RowRect.Y + 8.0f }, 11.5f, Theme.TextPrimary, Rig.RigTitle.c_str());

            char RigSub[64]{};
            std::snprintf(RigSub, sizeof(RigSub), "%u slots · %.0f/s", Rig.Sizing.CapacitySlots, static_cast<double>(Rig.Sizing.ReleaseRatePerSec));
            Surface.Text({ RowRect.X + 34.0f, RowRect.Y + 24.0f }, 9.5f, Theme.TextMuted, RigSub);

            const Point2D DotCenter{ RowRect.Right() - 12.0f, RowRect.Y + 22.0f };
            Surface.FillCircle(DotCenter, 4.0f, Rig.Enabled ? Theme.EmeraldInk : Theme.TextFaint);

            if (Hover && Ptr.Pressed)
            {
                const Rectangle2D DotHit{ DotCenter.X - 10.0f, DotCenter.Y - 10.0f, 20.0f, 20.0f };
                if (DotHit.Contains({ Ptr.X, Ptr.Y }))
                {
                    Rig.Enabled = !Rig.Enabled;
                    Session->RecordRevisionStep("Toggle Rig Visibility");
                }
                else
                {
                    Session->SelectRig(Rig.RigId);
                }
            }
            CursorY += 48.0f;
        }
    }

    // Optional '+' Add Rig / Force sheet
    if (AddMenuOpen)
    {
        const std::array<DropdownDirectiveEntry, 3u> Entries {{
            { "Add Ballistic Sparks Rig", "", { 255u, 138u, 76u, 255u }, ControlIcon::ParticleSparkles, false, false, false, false },
            { "Add Vortex Force Source",  "", {  70u, 210u, 103u, 255u }, ControlIcon::ForceVortex,     false, false, false, false },
            { "Add Wind Force Source",    "", {  92u, 198u, 242u, 255u }, ControlIcon::ForceWind,       false, false, false, false },
        }};
        const Rectangle2D SheetBounds = DropdownSelector::QueryDirectiveSheetBounds(
            { AddBtn.Right() - 210.0f, AddBtn.Bottom() + 4.0f }, 210.0f, Entries);
        int32_t Chosen = -1;
        (void)DropdownSelector::ConstructDirectiveSheet(Surface, SheetBounds, Entries, Ptr, Chosen, 1.0f);
        if (Chosen == 0)
        {
            ParticleSpace::ParticleRig NewRig{};
            NewRig.RigTitle = "Added Sparks Rig";
            const uint32_t NewId = Doc.AddRig(std::move(NewRig));
            Session->SelectRig(NewId);
            Session->RecordRevisionStep("Add Rig");
            AddMenuOpen = false;
        }
        else if (Chosen == 1 || Chosen == 2)
        {
            SimulationSpace::ForceFields::ForceSourceRecordEntry NewForce{};
            NewForce.Label    = (Chosen == 1) ? "Added Vortex" : "Added Wind";
            NewForce.Model    = (Chosen == 1) ? SimulationSpace::ForceFields::ForceModel::RotationalColumn : SimulationSpace::ForceFields::ForceModel::LinearWind;
            NewForce.Strength = 10.0f;
            NewForce.RadiusMeters = 3.0f;
            Doc.ForceSources.push_back(NewForce);
            Session->SelectForceSource(Doc.ForceSources.size() - 1u);
            Session->RecordRevisionStep("Add Force Source");
            AddMenuOpen = false;
        }
    }
}

void ParticleOutlinerPanel::PresentInImGuiPane(double DeltaSeconds, float ScaleFactor)
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
