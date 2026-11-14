/*============================================================================================================================================
                                                       PARTICLEOUTLINERPANEL.CPP
============================================================================================================================================*/
// 🧩 Implementation of ParticleOutlinerPanel: census tiles, live search, 14 non-quarantined filters, coupled C++ ForceFields & Construct sheet

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Particles/ParticleOutlinerPanel.h"

#include "Engine/Components/Controls/CapsuleButton.h"
#include "Engine/Components/Controls/ControlTooltip.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>

namespace Frontier::Workspaces
{

namespace
{

using namespace SimulationSpace::ForceFields;

//------------------------------------------------------------------------------------------------------------------------
//                                                    LOCAL ROUTINES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] bool ContainsCaseInsensitive(std::string_view Haystack, std::string_view Needle) noexcept
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
        bool Matched = true;
        for (std::size_t Offset = 0u; Offset < Needle.size(); ++Offset)
        {
            const unsigned char Left  = static_cast<unsigned char>(Haystack[Start + Offset]);
            const unsigned char Right = static_cast<unsigned char>(Needle[Offset]);
            if (std::tolower(Left) != std::tolower(Right))
            {
                Matched = false;
                break;
            }
        }
        if (Matched)
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] const char* ResolveBeaufortShortName(uint32_t Scale) noexcept
{
    static constexpr std::array<const char*, 13u> Names = {{
        "Calm", "Light air", "Light breeze", "Gentle breeze", "Moderate breeze",
        "Fresh breeze", "Strong breeze", "Near gale", "Gale", "Strong gale",
        "Storm", "Violent storm", "Hurricane",
    }};
    return Names[Scale < Names.size() ? Scale : (Names.size() - 1u)];
}

[[nodiscard]] bool QueryWindStreamCategory(const ForceSourceRecordEntry& Entry) noexcept
{
    return Entry.Domain == ForceDomainCategory::FlowField
        || Entry.Model == ForceModel::LinearWind
        || Entry.Model == ForceModel::AtmosphericGust;
}

[[nodiscard]] std::string FormatForceRowSummary(const ForceSourceRecordEntry& Entry)
{
    char Buffer[96]{};
    const char* LawName = QueryForceModelDisplayName(Entry.Model);
    std::snprintf(
        Buffer,
        sizeof(Buffer),
        "%s · %.1f m/s · r=%.0fm",
        LawName,
        Entry.Strength,
        Entry.RadiusMeters);
    return std::string(Buffer);
}

[[nodiscard]] std::string FormatEffectRowSummary(const ParticleEffectRecordEntry& Entry)
{
    char Buffer[96]{};
    if (Entry.BirthRatePerSec > 0.05f)
    {
        std::snprintf(
            Buffer,
            sizeof(Buffer),
            "%s · %u slots · %.0f/s",
            Entry.Sublabel.c_str(),
            Entry.Capacity,
            Entry.BirthRatePerSec);
    }
    else
    {
        std::snprintf(
            Buffer,
            sizeof(Buffer),
            "%s · %u slots",
            Entry.Sublabel.c_str(),
            Entry.Capacity);
    }
    return std::string(Buffer);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                               LIFECYCLE & CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

ParticleOutlinerPanel::~ParticleOutlinerPanel() noexcept
{
    WorkspaceAppearance::ReleasePanePointerOwnership(this);
}

void ParticleOutlinerPanel::AttachSession(ParticleWorkspaceSession* TargetSession) noexcept
{
    Session            = TargetSession;
    ScrollOffsetY      = 0.0f;
    SheetScrollOffsetY = 0.0f;
    ConstructSheetOpen = false;
    SearchBuffer       = {};
}

void ParticleOutlinerPanel::ConfigureFilterAll() noexcept
{
    FilterMode    = ParticleOutlinerFilterMode::AllEntries;
    ScrollOffsetY = 0.0f;
}

void ParticleOutlinerPanel::ConfigureFilterEffects() noexcept
{
    FilterMode    = ParticleOutlinerFilterMode::EffectsOnly;
    ScrollOffsetY = 0.0f;
}

void ParticleOutlinerPanel::ConfigureFilterForces() noexcept
{
    FilterMode    = ParticleOutlinerFilterMode::ForcesOnly;
    ScrollOffsetY = 0.0f;
}

void ParticleOutlinerPanel::ConfigureFilterCategory(ParticleEffectCategory Category) noexcept
{
    FilterMode       = ParticleOutlinerFilterMode::SingleDomain;
    FilteredCategory = Category;
    ScrollOffsetY    = 0.0f;
}

void ParticleOutlinerPanel::ConfigureSearchQuery(std::string_view Query) noexcept
{
    std::string Copy(Query);
    SearchBuffer.Begin(Copy.c_str());
    SearchBuffer.Active = false;
    ScrollOffsetY       = 0.0f;
}

void ParticleOutlinerPanel::ConfigureCompactMode(bool Enabled) noexcept
{
    CompactMode = Enabled;
}

void ParticleOutlinerPanel::ConfigureScrollOffset(float OffsetY) noexcept
{
    ScrollOffsetY = std::max(0.0f, OffsetY);
}

void ParticleOutlinerPanel::OpenConstructSheet(ConstructSheetPage Page) noexcept
{
    ConstructSheetOpen  = true;
    ActiveConstructPage = Page;
    SheetScrollOffsetY  = 0.0f;
}

void ParticleOutlinerPanel::CloseConstructSheet() noexcept
{
    ConstructSheetOpen = false;
}

ParticleOutlinerFilterMode ParticleOutlinerPanel::QueryFilterMode() const noexcept
{
    return FilterMode;
}

ParticleEffectCategory ParticleOutlinerPanel::QueryFilteredCategory() const noexcept
{
    return FilteredCategory;
}

bool ParticleOutlinerPanel::QueryCompactMode() const noexcept
{
    return CompactMode;
}

bool ParticleOutlinerPanel::QueryConstructSheetOpen() const noexcept
{
    return ConstructSheetOpen;
}

float ParticleOutlinerPanel::QueryScrollOffset() const noexcept
{
    return ScrollOffsetY;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              PRIMARY OUTLINER RENDERER
//------------------------------------------------------------------------------------------------------------------------

void ParticleOutlinerPanel::Render(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const PaneInputReading& Input,
    double                  Δt)
{
    const ColorRgba Background = WorkspaceAppearance::QueryDockedPaneBackground();
    Surface.PushClip(Bounds);
    Surface.FillRectangle(Bounds, Background, 0.0f);

    const float PadX   = 14.0f;
    const float InnerX = Bounds.X + PadX;
    const float InnerW = std::max(120.0f, Bounds.Width - PadX * 2.0f);
    const Point2D Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool    PointerEnabled = Input.PrimaryPointer.Enabled && !ConstructSheetOpen;
    const bool    PrimaryClick   = PointerEnabled && Input.PrimaryPointer.Pressed;

    ControlPointer TreePointer = Input.PrimaryPointer;
    if (ConstructSheetOpen)
    {
        TreePointer.Enabled  = false;
        TreePointer.Pressed  = false;
        TreePointer.Down     = false;
        TreePointer.Released = false;
    }

    float CursorY = Bounds.Y + 12.0f;

    // 1. Header Row: Outliner heading, record census subtitle, Compact toggle, and '+' Construct button
    const float AddBtnSize     = 28.0f;
    const float CompactBtnSize = 28.0f;
    const Rectangle2D AddBtnRect{
        InnerX + InnerW - AddBtnSize,
        CursorY + 2.0f,
        AddBtnSize,
        AddBtnSize,
    };
    const Rectangle2D CompactBtnRect{
        AddBtnRect.X - CompactBtnSize - 6.0f,
        CursorY + 2.0f,
        CompactBtnSize,
        CompactBtnSize,
    };

    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::SemiBold,
        { InnerX, CursorY },
        17.0f,
        WorkspaceSurfaceColors::PrimaryInk,
        "Outliner");

    char SubtitleBuf[96]{};
    const uint32_t TotalRecords = Session != nullptr ? Session->QueryTotalRecordCount() : 0u;
    std::snprintf(SubtitleBuf, sizeof(SubtitleBuf), "Particle & force scene · %u records", TotalRecords);
    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { InnerX, CursorY + 21.0f },
        std::max(40.0f, CompactBtnRect.X - InnerX - 8.0f),
        11.0f,
        WorkspaceSurfaceColors::MutedInk,
        SubtitleBuf);

    // Compact rows button
    const bool CompactHover = Input.PrimaryPointer.Enabled && CompactBtnRect.Contains(Cursor);
    Surface.FillRectangle(
        CompactBtnRect,
        CompactMode
            ? ColorRgba{ 48u, 48u, 48u, 255u }
            : (CompactHover ? WorkspaceSurfaceColors::RowHover : WorkspaceSurfaceColors::ControlWell),
        7.0f);
    Surface.StrokeRectangle(
        CompactBtnRect,
        CompactMode ? WorkspaceSurfaceColors::SelectionAmber : WorkspaceSurfaceColors::ControlWellBorder,
        7.0f,
        1.0f);
    VectorIconRepository::ConstructControl(
        Surface,
        ControlIcon::LayersSlabs,
        {
            CompactBtnRect.X + 7.0f,
            CompactBtnRect.Y + 7.0f,
            14.0f,
            CompactMode ? WorkspaceSurfaceColors::SelectionAmber : WorkspaceSurfaceColors::SecondaryInk,
            1.7f,
        });
    if (Input.PrimaryPointer.Enabled && Input.PrimaryPointer.Pressed && CompactHover && !ConstructSheetOpen)
    {
        CompactMode = !CompactMode;
    }

    // Cream '+' Construct button (opens 93-preset + 19-ForceModel sheet)
    const bool AddHover = Input.PrimaryPointer.Enabled && AddBtnRect.Contains(Cursor);
    Surface.FillRectangle(
        AddBtnRect,
        ConstructSheetOpen
            ? WorkspaceSurfaceColors::SelectionAmber
            : (AddHover ? ColorRgba{ 240u, 240u, 236u, 255u } : WorkspaceSurfaceColors::ConstructCream),
        7.0f);
    VectorIconRepository::ConstructControl(
        Surface,
        ControlIcon::PlusAdd,
        {
            AddBtnRect.X + 7.0f,
            AddBtnRect.Y + 7.0f,
            14.0f,
            WorkspaceSurfaceColors::ConstructDarkInk,
            2.1f,
        });
    if (Input.PrimaryPointer.Enabled && Input.PrimaryPointer.Pressed && AddHover)
    {
        ConstructSheetOpen = !ConstructSheetOpen;
        SheetScrollOffsetY = 0.0f;
    }

    CursorY += 42.0f;

    // 2. Visible / Hidden Census Tiles
    const float StatGap = 8.0f;
    const float StatW   = (InnerW - StatGap) * 0.5f;
    const float StatH   = 34.0f;
    const Rectangle2D VisibleTile{ InnerX, CursorY, StatW, StatH };
    const Rectangle2D HiddenTile{ InnerX + StatW + StatGap, CursorY, StatW, StatH };

    const uint32_t VisibleCount = Session != nullptr ? Session->QueryVisibleRecordCount() : 0u;
    const uint32_t HiddenCount  = Session != nullptr ? Session->QueryHiddenRecordCount() : 0u;

    auto RenderStatTile = [&](Rectangle2D Rect, const char* Label, uint32_t Count, ColorRgba DotColor, bool ActiveFilter)
    {
        Surface.FillRectangle(Rect, WorkspaceSurfaceColors::ControlWell, 8.0f);
        Surface.StrokeRectangle(
            Rect,
            ActiveFilter ? WorkspaceSurfaceColors::SelectionAmber : WorkspaceSurfaceColors::ControlWellBorder,
            8.0f,
            1.0f);
        Surface.FillCircle({ Rect.X + 14.0f, Rect.Y + Rect.Height * 0.5f }, 4.0f, DotColor);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Medium,
            { Rect.X + 24.0f, Rect.Y + 10.0f },
            11.5f,
            WorkspaceSurfaceColors::SecondaryInk,
            Label);
        char NumBuf[16]{};
        std::snprintf(NumBuf, sizeof(NumBuf), "%u", Count);
        WorkspaceAppearance::ConstructRightAlignedText(
            Surface,
            FontWeightClass::SemiBold,
            { Rect.Right() - 10.0f, Rect.Y + 9.0f },
            13.0f,
            WorkspaceSurfaceColors::PrimaryInk,
            NumBuf,
            true);
    };

    RenderStatTile(VisibleTile, "Visible", VisibleCount, WorkspaceSurfaceColors::StatusGreen, false);
    RenderStatTile(HiddenTile, "Hidden", HiddenCount, WorkspaceSurfaceColors::FaintInk, false);
    CursorY += StatH + 10.0f;

    // 3. Search Field (with Ctrl+F shortcut focus)
    if (Input.ShortcutFind && !ConstructSheetOpen)
    {
        SearchBuffer.Active = true;
    }
    if (SearchBuffer.Active)
    {
        WorkspaceAppearance::ForwardInputToTextBuffer(SearchBuffer, Input, Δt);
    }

    const Rectangle2D SearchRect{ InnerX, CursorY, InnerW, 30.0f };
    Surface.FillRectangle(SearchRect, WorkspaceSurfaceColors::DeepRecess, 8.0f);
    Surface.StrokeRectangle(
        SearchRect,
        SearchBuffer.Active ? WorkspaceSurfaceColors::SelectionAmber : WorkspaceSurfaceColors::ControlWellBorder,
        8.0f,
        1.0f);
    VectorIconRepository::ConstructControl(
        Surface,
        ControlIcon::SearchGlass,
        { SearchRect.X + 9.0f, SearchRect.Y + 8.0f, 13.5f, WorkspaceSurfaceColors::MutedInk, 1.7f });

    const Rectangle2D TextFieldRect{ SearchRect.X + 26.0f, SearchRect.Y + 1.0f, std::max(40.0f, SearchRect.Width - 78.0f), 28.0f };
    if (SearchBuffer.Length == 0u && !SearchBuffer.Active)
    {
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Regular,
            { TextFieldRect.X + 2.0f, SearchRect.Y + 8.0f },
            TextFieldRect.Width,
            12.0f,
            WorkspaceSurfaceColors::FaintInk,
            "Search scene...");
    }
    else
    {
        (void)TextInputField::Construct(Surface, TextFieldRect, SearchBuffer, TreePointer, 12.0f);
    }

    if (PrimaryClick && SearchRect.Contains(Cursor))
    {
        SearchBuffer.Active = true;
    }
    else if (PrimaryClick && !SearchRect.Contains(Cursor))
    {
        SearchBuffer.Active = false;
    }

    // Trailing "Ctrl F" shortcut hint tag
    const Rectangle2D KbdRect{ SearchRect.Right() - 46.0f, SearchRect.Y + 6.0f, 40.0f, 18.0f };
    Surface.FillRectangle(KbdRect, WorkspaceSurfaceColors::ControlWell, 4.0f);
    Surface.StrokeRectangle(KbdRect, WorkspaceSurfaceColors::ControlWellBorder, 4.0f, 1.0f);
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Medium,
        { KbdRect.X + 5.0f, KbdRect.Y + 3.0f },
        9.5f,
        WorkspaceSurfaceColors::MutedInk,
        "Ctrl F",
        true);

    CursorY += 38.0f;

    // 4. Category Filter Strip: All, Effects, Forces + 14 Active Non-Quarantined Categories (Zero Quarantine!)
    const float ChipH   = 21.0f;
    const float ChipGap = 4.0f;
    float ChipX         = InnerX;
    float ChipY         = CursorY;

    auto RenderFilterButton = [&](const char* Label, bool Active, ColorRgba Accent, auto&& SelectCallback)
    {
        const float TextW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 10.5f, Label);
        const float BtnW  = TextW + 14.0f;
        if (ChipX + BtnW > InnerX + InnerW + 0.5f && ChipX > InnerX + 1.0f)
        {
            ChipX = InnerX;
            ChipY += ChipH + ChipGap;
        }
        const Rectangle2D ChipRect{ ChipX, ChipY, BtnW, ChipH };
        const bool Hovered = PointerEnabled && ChipRect.Contains(Cursor);
        const ColorRgba Fill = Active
            ? ColorRgba{ Accent.Red, Accent.Green, Accent.Blue, 36u }
            : (Hovered ? WorkspaceSurfaceColors::RowHover : WorkspaceSurfaceColors::ControlWell);
        const ColorRgba Border = Active ? Accent : WorkspaceSurfaceColors::ControlWellBorder;
        const ColorRgba Ink    = Active ? Accent : WorkspaceSurfaceColors::SecondaryInk;

        Surface.FillRectangle(ChipRect, Fill, 6.0f);
        Surface.StrokeRectangle(ChipRect, Border, 6.0f, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            Active ? FontWeightClass::SemiBold : FontWeightClass::Medium,
            { ChipRect.X + 7.0f, ChipRect.Y + 4.0f },
            10.5f,
            Ink,
            Label);
        if (PrimaryClick && Hovered)
        {
            SelectCallback();
        }
        ChipX += BtnW + ChipGap;
    };

    RenderFilterButton(
        "All",
        FilterMode == ParticleOutlinerFilterMode::AllEntries,
        WorkspaceSurfaceColors::SelectionAmber,
        [&]() { ConfigureFilterAll(); });
    RenderFilterButton(
        "Effects",
        FilterMode == ParticleOutlinerFilterMode::EffectsOnly,
        WorkspaceSurfaceColors::SelectionAmber,
        [&]() { ConfigureFilterEffects(); });
    RenderFilterButton(
        "Forces",
        FilterMode == ParticleOutlinerFilterMode::ForcesOnly,
        ColorRgba{ 143u, 211u, 196u, 255u },
        [&]() { ConfigureFilterForces(); });

    for (const ParticleCategoryDescriptor& Cat : ParticlePresetRegistry::QueryActiveCategories())
    {
        const bool CatActive = (FilterMode == ParticleOutlinerFilterMode::SingleDomain && FilteredCategory == Cat.Category);
        RenderFilterButton(
            Cat.ShortChip,
            CatActive,
            Cat.AccentTint,
            [&, CategoryCode = Cat.Category]() { ConfigureFilterCategory(CategoryCode); });
    }

    CursorY = ChipY + ChipH + 10.0f;

    // Subtle divider separating filters from scrollable tree
    Surface.FillRectangle({ Bounds.X, CursorY, Bounds.Width, 1.0f }, WorkspaceSurfaceColors::HairlineLight, 0.0f);
    CursorY += 6.0f;

    // 5. Scrollable Outliner Tree Viewport
    const float FooterH = 26.0f;
    const Rectangle2D TreeViewport{
        Bounds.X,
        CursorY,
        Bounds.Width,
        std::max(40.0f, Bounds.Bottom() - FooterH - CursorY),
    };

    if (PointerEnabled && TreeViewport.Contains(Cursor) && std::abs(Input.WheelDeltaY) > 1.0e-4f)
    {
        ScrollOffsetY -= Input.WheelDeltaY * 34.0f;
    }
    const float MaxScroll = std::max(0.0f, LastTreeContentH - TreeViewport.Height);
    ScrollOffsetY         = std::clamp(ScrollOffsetY, 0.0f, MaxScroll);

    Surface.PushClip(TreeViewport);
    float RowY = TreeViewport.Y + 4.0f - ScrollOffsetY;
    const float RowH = CompactMode ? 28.0f : 42.0f;
    const std::string_view SearchText(SearchBuffer.Text, SearchBuffer.Length);

    auto RenderGroupHeader = [&](const char* Title, uint32_t Count, bool& Expanded)
    {
        const Rectangle2D HeaderRect{ InnerX, RowY, InnerW, 24.0f };
        const bool Hovered = PointerEnabled && TreeViewport.Contains(Cursor) && HeaderRect.Contains(Cursor);
        if (Hovered)
        {
            Surface.FillRectangle(HeaderRect, WorkspaceSurfaceColors::RowHover, 5.0f);
        }
        WorkspaceAppearance::ConstructChevronSymbol(
            Surface,
            { HeaderRect.X + 8.0f, HeaderRect.Y + 12.0f },
            8.0f,
            Expanded,
            WorkspaceSurfaceColors::MutedInk);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::SemiBold,
            { HeaderRect.X + 20.0f, HeaderRect.Y + 5.0f },
            11.0f,
            WorkspaceSurfaceColors::SecondaryInk,
            Title);
        char CountBuf[16]{};
        std::snprintf(CountBuf, sizeof(CountBuf), "%u", Count);
        WorkspaceAppearance::ConstructRightAlignedText(
            Surface,
            FontWeightClass::Medium,
            { HeaderRect.Right() - 6.0f, HeaderRect.Y + 5.0f },
            10.5f,
            WorkspaceSurfaceColors::FaintInk,
            CountBuf,
            true);
        if (PrimaryClick && Hovered)
        {
            Expanded = !Expanded;
        }
        RowY += 26.0f;
    };

    auto RenderSubgroupLabel = [&](const std::string& Caption)
    {
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::SemiBold,
            { InnerX + 10.0f, RowY + 3.0f },
            InnerW - 14.0f,
            10.0f,
            WorkspaceSurfaceColors::FaintInk,
            Caption);
        RowY += 19.0f;
    };

    auto RenderTreeRow = [&](
        const std::string& Title,
        const std::string& Summary,
        ColorRgba          Accent,
        bool               Selected,
        bool               Visible,
        bool               Nested,
        uint32_t           ForceLawOrdinal,
        const char*        StatusTag,
        auto&&             SelectCallback,
        auto&&             VisibilityCallback)
    {
        if (!SearchText.empty()
            && !ContainsCaseInsensitive(Title, SearchText)
            && !ContainsCaseInsensitive(Summary, SearchText))
        {
            return;
        }

        const float Indent = Nested ? 10.0f : 0.0f;
        const Rectangle2D RowRect{ InnerX + Indent, RowY, InnerW - Indent, RowH };
        const bool RowVisibleOnScreen = (RowRect.Bottom() >= TreeViewport.Y - 4.0f) && (RowRect.Y <= TreeViewport.Bottom() + 4.0f);

        if (RowVisibleOnScreen)
        {
            const bool RowHover = PointerEnabled && TreeViewport.Contains(Cursor) && RowRect.Contains(Cursor);
            const ColorRgba RowFill = Selected
                ? WorkspaceSurfaceColors::RowSelected
                : (RowHover ? WorkspaceSurfaceColors::RowHover : ColorRgba{ 22u, 22u, 23u, 255u });

            Surface.FillRectangle(RowRect, RowFill, 7.0f);
            Surface.StrokeRectangle(
                RowRect,
                Selected ? WorkspaceSurfaceColors::RowSelectedBorder : WorkspaceSurfaceColors::HairlineLight,
                7.0f,
                1.0f);

            // 3px leading accent / selection rail
            Surface.FillRectangle(
                { RowRect.X, RowRect.Y + 4.0f, 3.0f, RowRect.Height - 8.0f },
                Selected ? WorkspaceSurfaceColors::SelectionAmber : Accent.ScaledAlpha(Visible ? 0.90f : 0.30f),
                1.5f);

            // Leading icon well (reuses Frontier ForceLawIcon when ForceLawOrdinal < 19)
            const float IconBoxSize = CompactMode ? 18.0f : 24.0f;
            const Rectangle2D IconBox{
                RowRect.X + 9.0f,
                RowRect.Y + (RowRect.Height - IconBoxSize) * 0.5f,
                IconBoxSize,
                IconBoxSize,
            };
            Surface.FillRectangle(IconBox, ColorRgba{ Accent.Red, Accent.Green, Accent.Blue, 28u }, 5.0f);
            if (ForceLawOrdinal < static_cast<uint32_t>(ForceModel::Count))
            {
                WorkspaceAppearance::ConstructForceLawIcon(
                    Surface,
                    ForceLawOrdinal,
                    { IconBox.X + 4.0f, IconBox.Y + 4.0f },
                    IconBoxSize - 8.0f,
                    Accent,
                    Visible ? 1.0f : 0.45f);
            }
            else
            {
                Surface.FillCircle(
                    IconBox.Center(),
                    CompactMode ? 4.0f : 5.2f,
                    Accent.ScaledAlpha(Visible ? 1.0f : 0.35f));
                Surface.StrokeCircle(
                    IconBox.Center(),
                    CompactMode ? 6.0f : 7.8f,
                    Accent.ScaledAlpha(Visible ? 0.40f : 0.15f),
                    1.0f);
            }

            // Right-hand visibility eye button & status dot
            const Rectangle2D EyeRect{ RowRect.Right() - 26.0f, RowRect.Y + (RowRect.Height - 22.0f) * 0.5f, 22.0f, 22.0f };
            const bool EyeHover = PointerEnabled && TreeViewport.Contains(Cursor) && EyeRect.Contains(Cursor);
            if (EyeHover)
            {
                Surface.FillRectangle(EyeRect, ColorRgba{ 255u, 255u, 255u, 18u }, 5.0f);
            }
            VectorIconRepository::ConstructControl(
                Surface,
                Visible ? ControlIcon::EyeVisible : ControlIcon::EyeHidden,
                {
                    EyeRect.X + 4.0f,
                    EyeRect.Y + 4.0f,
                    14.0f,
                    Visible ? WorkspaceSurfaceColors::SecondaryInk : WorkspaceSurfaceColors::FaintInk,
                    1.6f,
                });

            const Point2D LiveDotCenter{ EyeRect.X - 8.0f, RowRect.Y + RowRect.Height * 0.5f };
            Surface.FillCircle(
                LiveDotCenter,
                3.0f,
                Visible ? WorkspaceSurfaceColors::StatusGreen : WorkspaceSurfaceColors::FaintInk);

            float CopyRightEdge = LiveDotCenter.X - 10.0f;
            if (StatusTag != nullptr && StatusTag[0] != '\0')
            {
                const float TagW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 9.5f, StatusTag) + 10.0f;
                const Rectangle2D TagRect{
                    CopyRightEdge - TagW,
                    RowRect.Y + (RowRect.Height - 16.0f) * 0.5f,
                    TagW,
                    16.0f,
                };
                const ColorRgba TagTint = Visible
                    ? ColorRgba{ 88u, 186u, 140u, 36u }
                    : ColorRgba{ 120u, 120u, 126u, 28u };
                Surface.FillRectangle(TagRect, TagTint, 4.0f);
                WorkspaceAppearance::ConstructWeightedText(
                    Surface,
                    FontWeightClass::Medium,
                    { TagRect.X + 5.0f, TagRect.Y + 2.5f },
                    9.5f,
                    Visible ? ColorRgba{ 148u, 222u, 188u, 255u } : WorkspaceSurfaceColors::MutedInk,
                    StatusTag);
                CopyRightEdge = TagRect.X - 6.0f;
            }

            const float CopyX = IconBox.Right() + 9.0f;
            const float CopyW = std::max(30.0f, CopyRightEdge - CopyX);
            const ColorRgba TitleInk = Visible ? WorkspaceSurfaceColors::PrimaryInk : WorkspaceSurfaceColors::MutedInk;
            if (CompactMode)
            {
                WorkspaceAppearance::ConstructTruncatedText(
                    Surface,
                    Selected ? FontWeightClass::SemiBold : FontWeightClass::Medium,
                    { CopyX, RowRect.Y + 7.0f },
                    CopyW,
                    11.5f,
                    TitleInk,
                    Title);
            }
            else
            {
                WorkspaceAppearance::ConstructTruncatedText(
                    Surface,
                    Selected ? FontWeightClass::SemiBold : FontWeightClass::Medium,
                    { CopyX, RowRect.Y + 6.0f },
                    CopyW,
                    12.0f,
                    TitleInk,
                    Title);
                WorkspaceAppearance::ConstructTruncatedText(
                    Surface,
                    FontWeightClass::Regular,
                    { CopyX, RowRect.Y + 23.0f },
                    CopyW,
                    10.2f,
                    WorkspaceSurfaceColors::MutedInk,
                    Summary);
            }

            if (PrimaryClick && RowHover)
            {
                if (EyeHover)
                {
                    VisibilityCallback();
                }
                else
                {
                    SelectCallback();
                }
            }
        }

        RowY += RowH + 4.0f;
    };

    if (Session != nullptr)
    {
        // A. Coupled C++ ForceFields Environment Group
        const bool ShowEnvironment = (FilterMode == ParticleOutlinerFilterMode::AllEntries
                                      || FilterMode == ParticleOutlinerFilterMode::ForcesOnly);
        if (ShowEnvironment)
        {
            auto ForceSpan = Session->AccessForceSources();
            RenderGroupHeader("Environment (C++ ForceFields)", static_cast<uint32_t>(ForceSpan.size() + 2u), EnvironmentExpanded);

            if (EnvironmentExpanded)
            {
                char FieldMeta[96]{};
                const auto& Overlay = Session->QueryForceOverlay();
                std::snprintf(
                    FieldMeta,
                    sizeof(FieldMeta),
                    "%u bounded forces · %s",
                    static_cast<uint32_t>(ForceSpan.size()),
                    Overlay.Enabled ? "4-tier lattice active" : "overlay muted");

                RenderTreeRow(
                    "Force Field",
                    FieldMeta,
                    ColorRgba{ 143u, 211u, 196u, 255u },
                    Session->QuerySelectionDomain() == ParticleSelectionDomain::EnvironmentField,
                    Overlay.Enabled,
                    false,
                    static_cast<uint32_t>(ForceModel::CurlNoise),
                    "coupled",
                    [&]() { Session->SelectEnvironmentField(); },
                    [&]()
                    {
                        Session->AccessForceOverlay().Enabled = !Session->AccessForceOverlay().Enabled;
                        Session->IncrementRevision();
                    });

                const ForceDiagnosticsReading WindDiag = Session->QueryAmbientWindDiagnostics();
                const float WindSpeed                  = VectorLength(WindDiag.TotalForce.AdvectionVelocity);
                char WindMeta[96]{};
                std::snprintf(
                    WindMeta,
                    sizeof(WindMeta),
                    "%.1f m/s at %.0f° (%s)",
                    WindSpeed,
                    WindDiag.CompassAzimuthDeg,
                    ResolveBeaufortShortName(WindDiag.BeaufortScale));

                const bool AmbientEnabled = !ForceSpan.empty() ? ForceSpan.front().Enabled : false;
                RenderTreeRow(
                    "Ambient Wind",
                    WindMeta,
                    ColorRgba{ 159u, 199u, 232u, 255u },
                    Session->QuerySelectionDomain() == ParticleSelectionDomain::AmbientWind,
                    AmbientEnabled,
                    true,
                    static_cast<uint32_t>(ForceModel::LinearWind),
                    AmbientEnabled ? "live" : "off",
                    [&]() { Session->SelectAmbientWind(); },
                    [&]()
                    {
                        if (!ForceSpan.empty())
                        {
                            ForceSpan.front().Enabled = !ForceSpan.front().Enabled;
                            ForceSpan.front().Visible = ForceSpan.front().Enabled;
                            Session->SynchronizeForceFieldsAccumulator();
                            Session->IncrementRevision();
                        }
                    });

                // Split bounded ForceSources (excluding index 0 which is the pinned Ambient Wind row) into Stream Generators vs Accelerate/Damp Zones
                const uint32_t AmbientToken = !ForceSpan.empty() ? ForceSpan.front().UniqueToken : 0u;
                uint32_t StreamCount = 0u;
                uint32_t ZoneCount   = 0u;
                for (const ForceSourceRecordEntry& Source : ForceSpan)
                {
                    if (Source.UniqueToken == AmbientToken)
                    {
                        continue;
                    }
                    if (QueryWindStreamCategory(Source))
                    {
                        ++StreamCount;
                    }
                    else
                    {
                        ++ZoneCount;
                    }
                }

                if (StreamCount > 0u)
                {
                    RenderSubgroupLabel("Wind & Stream Generators (" + std::to_string(StreamCount) + ")");
                    for (ForceSourceRecordEntry& Source : ForceSpan)
                    {
                        if (Source.UniqueToken == AmbientToken || !QueryWindStreamCategory(Source))
                        {
                            continue;
                        }
                        const uint32_t LawOrd = static_cast<uint32_t>(Source.Model);
                        const bool Selected   = (Session->QuerySelectionDomain() == ParticleSelectionDomain::ForceSource
                                                 && Session->QuerySelectedToken() == Source.UniqueToken);
                        RenderTreeRow(
                            Source.Label,
                            FormatForceRowSummary(Source),
                            WorkspaceAppearance::QueryForceLawTint(LawOrd),
                            Selected,
                            Source.Enabled && Source.Visible,
                            true,
                            LawOrd,
                            Source.Enabled ? "active" : "off",
                            [&, Token = Source.UniqueToken]() { Session->SelectForceSource(Token); },
                            [&, Token = Source.UniqueToken]()
                            {
                                if (ForceSourceRecordEntry* Entry = Session->FindForceSource(Token))
                                {
                                    Entry->Enabled = !Entry->Enabled;
                                    Entry->Visible = Entry->Enabled;
                                    Session->SynchronizeForceFieldsAccumulator();
                                    Session->IncrementRevision();
                                }
                            });
                    }
                }

                if (ZoneCount > 0u)
                {
                    RenderSubgroupLabel("Accelerate & Damp Zones (" + std::to_string(ZoneCount) + ")");
                    for (ForceSourceRecordEntry& Source : ForceSpan)
                    {
                        if (Source.UniqueToken == AmbientToken || QueryWindStreamCategory(Source))
                        {
                            continue;
                        }
                        const uint32_t LawOrd = static_cast<uint32_t>(Source.Model);
                        const bool Selected   = (Session->QuerySelectionDomain() == ParticleSelectionDomain::ForceSource
                                                 && Session->QuerySelectedToken() == Source.UniqueToken);
                        RenderTreeRow(
                            Source.Label,
                            FormatForceRowSummary(Source),
                            WorkspaceAppearance::QueryForceLawTint(LawOrd),
                            Selected,
                            Source.Enabled && Source.Visible,
                            true,
                            LawOrd,
                            Source.Enabled ? "active" : "off",
                            [&, Token = Source.UniqueToken]() { Session->SelectForceSource(Token); },
                            [&, Token = Source.UniqueToken]()
                            {
                                if (ForceSourceRecordEntry* Entry = Session->FindForceSource(Token))
                                {
                                    Entry->Enabled = !Entry->Enabled;
                                    Entry->Visible = Entry->Enabled;
                                    Session->SynchronizeForceFieldsAccumulator();
                                    Session->IncrementRevision();
                                }
                            });
                    }
                }
            }

            RowY += 6.0f;
        }

        // B. Particle Effects Group (14 Active Non-Quarantined Categories)
        const bool ShowEffects = (FilterMode != ParticleOutlinerFilterMode::ForcesOnly);
        if (ShowEffects)
        {
            auto EffectsSpan = Session->AccessEffects();
            uint32_t MatchedEffectCount = 0u;
            for (const ParticleEffectRecordEntry& Effect : EffectsSpan)
            {
                if (FilterMode == ParticleOutlinerFilterMode::SingleDomain && Effect.Category != FilteredCategory)
                {
                    continue;
                }
                ++MatchedEffectCount;
            }

            std::string EffectsHeading = "Particle Effects";
            if (FilterMode == ParticleOutlinerFilterMode::SingleDomain)
            {
                EffectsHeading = std::string("VFX · ") + ParticlePresetRegistry::QueryCategoryDescriptor(FilteredCategory).FullTitle;
            }
            RenderGroupHeader(EffectsHeading.c_str(), MatchedEffectCount, EffectsExpanded);

            if (EffectsExpanded)
            {
                for (const ParticleCategoryDescriptor& Category : ParticlePresetRegistry::QueryActiveCategories())
                {
                    if (FilterMode == ParticleOutlinerFilterMode::SingleDomain && Category.Category != FilteredCategory)
                    {
                        continue;
                    }

                    uint32_t CatCount = 0u;
                    for (const ParticleEffectRecordEntry& Effect : EffectsSpan)
                    {
                        if (Effect.Category == Category.Category)
                        {
                            ++CatCount;
                        }
                    }
                    if (CatCount == 0u)
                    {
                        continue;
                    }

                    RenderSubgroupLabel(std::string(Category.FullTitle) + " (" + std::to_string(CatCount) + ")");
                    for (ParticleEffectRecordEntry& Effect : EffectsSpan)
                    {
                        if (Effect.Category != Category.Category)
                        {
                            continue;
                        }
                        const bool Selected = (Session->QuerySelectionDomain() == ParticleSelectionDomain::ParticleEffect
                                               && Session->QuerySelectedToken() == Effect.UniqueToken);
                        const ColorRgba Swatch = (Effect.ColorA.Luminance() < 0.08f) ? Category.AccentTint : Effect.ColorA;
                        RenderTreeRow(
                            Effect.Title,
                            FormatEffectRowSummary(Effect),
                            Swatch,
                            Selected,
                            Effect.Visible,
                            true,
                            UINT32_MAX,
                            nullptr,
                            [&, Token = Effect.UniqueToken]() { Session->SelectParticleEffect(Token); },
                            [&, Token = Effect.UniqueToken]()
                            {
                                if (ParticleEffectRecordEntry* Target = Session->AccessEffect(Token))
                                {
                                    Target->Visible = !Target->Visible;
                                    Session->IncrementRevision();
                                }
                            });
                    }
                }
            }
        }
    }

    LastTreeContentH = (RowY + ScrollOffsetY) - TreeViewport.Y + 8.0f;
    WorkspaceAppearance::ConstructVerticalScrollIndicator(Surface, TreeViewport, LastTreeContentH, ScrollOffsetY, Cursor);
    Surface.PopClip();

    // 6. Status Footer
    const Rectangle2D FooterRect{ Bounds.X, Bounds.Bottom() - FooterH, Bounds.Width, FooterH };
    Surface.FillRectangle(FooterRect, WorkspaceSurfaceColors::FooterBackground, 0.0f);
    Surface.FillRectangle({ FooterRect.X, FooterRect.Y, FooterRect.Width, 1.0f }, WorkspaceSurfaceColors::HairlineLight, 0.0f);
    Surface.FillCircle({ FooterRect.X + 14.0f, FooterRect.Y + FooterH * 0.5f }, 3.5f, WorkspaceSurfaceColors::StatusGreen);
    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Medium,
        { FooterRect.X + 24.0f, FooterRect.Y + 6.5f },
        FooterRect.Width - 34.0f,
        10.5f,
        WorkspaceSurfaceColors::MutedInk,
        "Frontier C++20 · ForceFields Coupled · 93 Active Presets");

    // 7. Interactive '+' Construct Overlay Sheet (93 Non-Quarantined Presets + 19 C++ ForceModel Laws)
    if (ConstructSheetOpen)
    {
        RenderConstructOverlay(Surface, Bounds, AddBtnRect, Input);
    }

    Surface.PopClip();
    (void)ControlTooltip::PresentHoveredControlTooltip(Surface, Bounds, Input.PrimaryPointer);
}

//------------------------------------------------------------------------------------------------------------------------
//                                           '+' CONSTRUCT OVERLAY SHEET
//------------------------------------------------------------------------------------------------------------------------

void ParticleOutlinerPanel::RenderConstructOverlay(
    PixelSpace&             Surface,
    Rectangle2D             PaneBounds,
    Rectangle2D             AnchorButton,
    const PaneInputReading& Input)
{
    const float SheetW = std::min(316.0f, PaneBounds.Width - 16.0f);
    const uint32_t VisibleEntryCount = (ActiveConstructPage == ConstructSheetPage::ParticlePresets)
        ? ParticlePresetRegistry::CountPresetsInCategory(ConstructCategory)
        : static_cast<uint32_t>(ForceModel::Count);
    const float HeaderAllowance = (ActiveConstructPage == ConstructSheetPage::ParticlePresets) ? 106.0f : 56.0f;
    const float RowPitch        = (ActiveConstructPage == ConstructSheetPage::ParticlePresets) ? 37.0f : 35.0f;
    const float DesiredH        = HeaderAllowance + static_cast<float>(VisibleEntryCount) * RowPitch + 12.0f;
    const float MaxSheetH       = std::min(430.0f, PaneBounds.Height - 76.0f);
    const float SheetH          = std::clamp(DesiredH, 168.0f, std::max(168.0f, MaxSheetH));
    const Rectangle2D SheetRect{
        std::clamp(AnchorButton.Right() - SheetW, PaneBounds.X + 8.0f, PaneBounds.Right() - SheetW - 8.0f),
        AnchorButton.Bottom() + 6.0f,
        SheetW,
        SheetH,
    };

    const Point2D Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool    Clicked = Input.PrimaryPointer.Enabled && Input.PrimaryPointer.Pressed;

    if (Input.EscapePressed || (Clicked && !SheetRect.Contains(Cursor) && !AnchorButton.Contains(Cursor)))
    {
        ConstructSheetOpen = false;
        return;
    }

    // Soft elevated drop surface
    Surface.FillRectangle(
        { SheetRect.X + 2.0f, SheetRect.Y + 4.0f, SheetRect.Width, SheetRect.Height },
        ColorRgba{ 0u, 0u, 0u, 150u },
        12.0f);
    Surface.FillRectangle(SheetRect, ColorRgba{ 18u, 19u, 22u, 250u }, 12.0f);
    Surface.StrokeRectangle(SheetRect, WorkspaceSurfaceColors::RowSelectedBorder, 12.0f, 1.0f);

    float Y = SheetRect.Y + 10.0f;
    const float InnerX = SheetRect.X + 10.0f;
    const float InnerW = SheetRect.Width - 20.0f;

    // Top mode segmented tabs: Particle Presets (93) | Force Laws (19)
    const char* PageLabels[2] = { "Presets (93)", "Force Laws (19)" };
    uint32_t SelectedPageIdx  = static_cast<uint32_t>(ActiveConstructPage);
    const ControlContact SegContact = CapsuleButton::ConstructSegmented(
        Surface,
        { InnerX, Y, InnerW, 28.0f },
        PageLabels,
        2u,
        SelectedPageIdx,
        Input.PrimaryPointer,
        SelectedPageIdx);
    if (SegContact.Clicked)
    {
        ActiveConstructPage = static_cast<ConstructSheetPage>(SelectedPageIdx);
        SheetScrollOffsetY  = 0.0f;
    }
    Y += 34.0f;

    // Preset category shelf strip when browsing presets
    if (ActiveConstructPage == ConstructSheetPage::ParticlePresets)
    {
        float CatX = InnerX;
        float CatY = Y;
        for (const ParticleCategoryDescriptor& Cat : ParticlePresetRegistry::QueryActiveCategories())
        {
            const float BtnW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 9.5f, Cat.ShortChip) + 10.0f;
            if (CatX + BtnW > InnerX + InnerW + 0.5f && CatX > InnerX + 1.0f)
            {
                CatX = InnerX;
                CatY += 20.0f;
            }
            const Rectangle2D CatBtn{ CatX, CatY, BtnW, 18.0f };
            const bool Active  = (ConstructCategory == Cat.Category);
            const bool Hovered = Input.PrimaryPointer.Enabled && CatBtn.Contains(Cursor);
            Surface.FillRectangle(
                CatBtn,
                Active
                    ? ColorRgba{ Cat.AccentTint.Red, Cat.AccentTint.Green, Cat.AccentTint.Blue, 42u }
                    : (Hovered ? WorkspaceSurfaceColors::RowHover : WorkspaceSurfaceColors::ControlWell),
                5.0f);
            Surface.StrokeRectangle(CatBtn, Active ? Cat.AccentTint : WorkspaceSurfaceColors::ControlWellBorder, 5.0f, 1.0f);
            WorkspaceAppearance::ConstructWeightedText(
                Surface,
                Active ? FontWeightClass::SemiBold : FontWeightClass::Regular,
                { CatBtn.X + 5.0f, CatBtn.Y + 3.0f },
                9.5f,
                Active ? Cat.AccentTint : WorkspaceSurfaceColors::SecondaryInk,
                Cat.ShortChip);
            if (Clicked && Hovered)
            {
                ConstructCategory  = Cat.Category;
                SheetScrollOffsetY = 0.0f;
            }
            CatX += BtnW + 3.0f;
        }
        Y = CatY + 24.0f;
    }

    const Rectangle2D ListViewport{ InnerX, Y, InnerW, std::max(60.0f, SheetRect.Bottom() - Y - 10.0f) };
    if (Input.PrimaryPointer.Enabled && ListViewport.Contains(Cursor) && std::abs(Input.WheelDeltaY) > 1.0e-4f)
    {
        SheetScrollOffsetY = std::max(0.0f, SheetScrollOffsetY - Input.WheelDeltaY * 28.0f);
    }

    Surface.PushClip(ListViewport);
    float EntryY = ListViewport.Y - SheetScrollOffsetY;

    if (ActiveConstructPage == ConstructSheetPage::ParticlePresets)
    {
        for (const ParticlePresetDescriptor& Preset : ParticlePresetRegistry::QueryActivePresets())
        {
            if (Preset.Category != ConstructCategory)
            {
                continue;
            }
            const Rectangle2D EntryRect{ ListViewport.X, EntryY, ListViewport.Width, 34.0f };
            const bool Hovered = Input.PrimaryPointer.Enabled && ListViewport.Contains(Cursor) && EntryRect.Contains(Cursor);
            Surface.FillRectangle(EntryRect, Hovered ? WorkspaceSurfaceColors::RowSelected : WorkspaceSurfaceColors::ControlWell, 6.0f);
            Surface.StrokeRectangle(EntryRect, Hovered ? WorkspaceSurfaceColors::SelectionAmber : WorkspaceSurfaceColors::HairlineLight, 6.0f, 1.0f);

            const ColorRgba DotColor = (Preset.ColorA.Luminance() < 0.08f)
                ? ParticlePresetRegistry::QueryCategoryDescriptor(Preset.Category).AccentTint
                : Preset.ColorA;
            Surface.FillCircle({ EntryRect.X + 12.0f, EntryRect.Y + 17.0f }, 4.5f, DotColor);
            WorkspaceAppearance::ConstructTruncatedText(
                Surface,
                FontWeightClass::Medium,
                { EntryRect.X + 24.0f, EntryRect.Y + 4.0f },
                EntryRect.Width - 32.0f,
                11.0f,
                WorkspaceSurfaceColors::PrimaryInk,
                Preset.DisplayName);
            char MetaBuf[64]{};
            std::snprintf(MetaBuf, sizeof(MetaBuf), "%s · %u slots", Preset.Sublabel, Preset.Capacity);
            WorkspaceAppearance::ConstructTruncatedText(
                Surface,
                FontWeightClass::Regular,
                { EntryRect.X + 24.0f, EntryRect.Y + 18.0f },
                EntryRect.Width - 32.0f,
                9.5f,
                WorkspaceSurfaceColors::MutedInk,
                MetaBuf);

            if (Clicked && Hovered && Session != nullptr)
            {
                (void)Session->AddEffectFromPreset(Preset.Identifier);
                ConstructSheetOpen = false;
            }
            EntryY += 37.0f;
        }
    }
    else
    {
        for (uint32_t LawOrd = 0u; LawOrd < static_cast<uint32_t>(ForceModel::Count); ++LawOrd)
        {
            const ForceModel Law = static_cast<ForceModel>(LawOrd);
            const ColorRgba Tint = WorkspaceAppearance::QueryForceLawTint(LawOrd);
            const Rectangle2D EntryRect{ ListViewport.X, EntryY, ListViewport.Width, 32.0f };
            const bool Hovered = Input.PrimaryPointer.Enabled && ListViewport.Contains(Cursor) && EntryRect.Contains(Cursor);
            Surface.FillRectangle(EntryRect, Hovered ? WorkspaceSurfaceColors::RowSelected : WorkspaceSurfaceColors::ControlWell, 6.0f);
            Surface.StrokeRectangle(EntryRect, Hovered ? Tint : WorkspaceSurfaceColors::HairlineLight, 6.0f, 1.0f);

            WorkspaceAppearance::ConstructForceLawIcon(
                Surface,
                LawOrd,
                { EntryRect.X + 8.0f, EntryRect.Y + 8.0f },
                16.0f,
                Tint);
            WorkspaceAppearance::ConstructTruncatedText(
                Surface,
                FontWeightClass::Medium,
                { EntryRect.X + 30.0f, EntryRect.Y + 8.5f },
                EntryRect.Width - 38.0f,
                11.2f,
                WorkspaceSurfaceColors::PrimaryInk,
                QueryForceModelDisplayName(Law));

            if (Clicked && Hovered && Session != nullptr)
            {
                (void)Session->AddForceSource(Law);
                ConstructSheetOpen = false;
            }
            EntryY += 35.0f;
        }
    }

    const float SheetContentH = (EntryY + SheetScrollOffsetY) - ListViewport.Y;
    SheetScrollOffsetY        = std::clamp(SheetScrollOffsetY, 0.0f, std::max(0.0f, SheetContentH - ListViewport.Height));
    WorkspaceAppearance::ConstructVerticalScrollIndicator(Surface, ListViewport, SheetContentH, SheetScrollOffsetY, Cursor);
    Surface.PopClip();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 IMGUI PANE ADAPTER
//------------------------------------------------------------------------------------------------------------------------

void ParticleOutlinerPanel::PresentInImGuiPane(double Δt, float ScaleFactor)
{
    const float  SafeScale = std::max(0.25f, ScaleFactor);
    const ImVec2 WinPos    = ImGui::GetWindowPos();
    const ImVec2 WinSize   = ImGui::GetWindowSize();
    const ImVec2 CursorPos = ImGui::GetCursorScreenPos();
    const float  TopY      = CursorPos.y - ImGui::GetStyle().WindowPadding.y;
    const float  PaneH     = std::max(1.0f, (WinPos.y + WinSize.y) - TopY);

    ImGui::InvisibleButton(
        "##ParticleOutlinerPaneCanvas",
        ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), std::max(1.0f, ImGui::GetContentRegionAvail().y)),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

    const Rectangle2D ScreenBounds{ WinPos.x, TopY, WinSize.x, PaneH };
    const Rectangle2D LogicalBounds{ 0.0f, 0.0f, WinSize.x / SafeScale, PaneH / SafeScale };

    PixelSpace       Surface(ImGui::GetWindowDrawList(), { ScreenBounds.X, ScreenBounds.Y }, SafeScale);
    PaneInputReading Input = WorkspaceAppearance::CaptureImGuiPaneInput(ScreenBounds, SafeScale);
    WorkspaceAppearance::EnforcePanePointerOwnership(this, Input);

    Render(Surface, LogicalBounds, Input, Δt);

    WorkspaceAppearance::UpdatePanePointerOwnership(this, ScreenBounds, SafeScale, Input, false);
}

} // namespace Frontier::Workspaces
