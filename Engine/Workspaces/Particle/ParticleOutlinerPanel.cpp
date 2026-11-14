/*============================================================================================================================================
                                                       PARTICLEOUTLINERPANEL.CPP
============================================================================================================================================*/
// 🧩 Implementation of ParticleOutlinerPanel: census tiles, live search, 14 non-quarantined filters, coupled C++ ForceFields & Construct sheet

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Particle/ParticleOutlinerPanel.h"

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
    const ColorRgba Background{ 18u, 18u, 18u, 255u };
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

    // 1. Header Row: Outliner heading, scene subtitle, circular Compact toggle & warm-gold '+' Construct button
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
        FontWeightClass::Medium,
        { InnerX, CursorY },
        20.0f,
        ColorRgba{ 239u, 239u, 239u, 255u },
        "Outliner");

    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { InnerX, CursorY + 23.0f },
        std::max(40.0f, CompactBtnRect.X - InnerX - 8.0f),
        11.0f,
        ColorRgba{ 118u, 118u, 118u, 255u },
        "Force field & particle scene");

    // Circular Compact rows button (radius = 14px)
    const bool CompactHover = Input.PrimaryPointer.Enabled && CompactBtnRect.Contains(Cursor);
    Surface.FillRectangle(
        CompactBtnRect,
        CompactMode
            ? ColorRgba{ 255u, 255u, 255u, 24u }
            : (CompactHover ? ColorRgba{ 255u, 255u, 255u, 14u } : ColorRgba{ 255u, 255u, 255u, 8u }),
        14.0f);
    Surface.StrokeRectangle(
        CompactBtnRect,
        CompactMode ? ColorRgba{ 232u, 194u, 122u, 180u } : ColorRgba{ 255u, 255u, 255u, 22u },
        14.0f,
        1.0f);
    VectorIconRepository::ConstructControl(
        Surface,
        ControlIcon::LayersSlabs,
        {
            CompactBtnRect.X + 7.0f,
            CompactBtnRect.Y + 7.0f,
            14.0f,
            CompactMode ? ColorRgba{ 232u, 194u, 122u, 255u } : ColorRgba{ 188u, 188u, 188u, 255u },
            1.7f,
        });
    if (Input.PrimaryPointer.Enabled && Input.PrimaryPointer.Pressed && CompactHover && !ConstructSheetOpen)
    {
        CompactMode = !CompactMode;
    }

    // Circular warm-gold '+' Construct button (opens 93-preset + 19-ForceModel sheet)
    const bool AddHover = Input.PrimaryPointer.Enabled && AddBtnRect.Contains(Cursor);
    Surface.FillRectangle(
        AddBtnRect,
        ConstructSheetOpen
            ? ColorRgba{ 232u, 194u, 122u, 255u }
            : (AddHover ? ColorRgba{ 232u, 194u, 122u, 64u } : ColorRgba{ 232u, 194u, 122u, 36u }),
        14.0f);
    Surface.StrokeRectangle(
        AddBtnRect,
        ColorRgba{ 232u, 194u, 122u, 110u },
        14.0f,
        1.0f);
    VectorIconRepository::ConstructControl(
        Surface,
        ControlIcon::PlusAdd,
        {
            AddBtnRect.X + 7.0f,
            AddBtnRect.Y + 7.0f,
            14.0f,
            ConstructSheetOpen ? ColorRgba{ 18u, 18u, 18u, 255u } : ColorRgba{ 246u, 220u, 176u, 255u },
            2.1f,
        });
    if (Input.PrimaryPointer.Enabled && Input.PrimaryPointer.Pressed && AddHover)
    {
        ConstructSheetOpen = !ConstructSheetOpen;
        SheetScrollOffsetY = 0.0f;
    }

    CursorY += 44.0f;

    // 2. Project Zero / HTML 64px Census Tiles (Visible / Hidden with 22px symbol circle + 28px right count)
    const float StatGap = 8.0f;
    const float StatW   = (InnerW - StatGap) * 0.5f;
    const float StatH   = 64.0f;
    const Rectangle2D VisibleTile{ InnerX, CursorY, StatW, StatH };
    const Rectangle2D HiddenTile{ InnerX + StatW + StatGap, CursorY, StatW, StatH };

    const uint32_t VisibleCount = Session != nullptr ? Session->QueryVisibleRecordCount() : 0u;
    const uint32_t HiddenCount  = Session != nullptr ? Session->QueryHiddenRecordCount() : 0u;

    auto RenderStatTile = [&](Rectangle2D Rect, const char* Label, uint32_t Count, bool IsVisibleTile)
    {
        Surface.FillRectangle(Rect, ColorRgba{ 255u, 255u, 255u, 9u }, 18.0f);
        Surface.StrokeRectangle(Rect, ColorRgba{ 255u, 255u, 255u, 18u }, 18.0f, 1.0f);

        const Point2D SymCenter{ Rect.X + 24.0f, Rect.Y + 21.0f };
        if (IsVisibleTile)
        {
            Surface.FillCircle(SymCenter, 11.0f, ColorRgba{ 52u, 199u, 89u, 255u });
            const Point2D CheckPts[3] = {
                { SymCenter.X - 4.2f, SymCenter.Y + 0.3f },
                { SymCenter.X - 1.2f, SymCenter.Y + 3.4f },
                { SymCenter.X + 4.6f, SymCenter.Y - 3.2f },
            };
            Surface.StrokePolyline(CheckPts, ColorRgba{ 10u, 27u, 16u, 255u }, 1.9f, false);
        }
        else
        {
            Surface.FillCircle(SymCenter, 11.0f, ColorRgba{ 255u, 255u, 255u, 18u });
            VectorIconRepository::ConstructControl(
                Surface,
                ControlIcon::EyeHidden,
                { SymCenter.X - 6.0f, SymCenter.Y - 6.0f, 12.0f, ColorRgba{ 146u, 146u, 150u, 255u }, 1.5f });
        }

        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { Rect.X + 13.0f, Rect.Y + 39.0f },
            11.0f,
            ColorRgba{ 150u, 150u, 150u, 255u },
            Label);

        char NumBuf[16]{};
        std::snprintf(NumBuf, sizeof(NumBuf), "%u", Count);
        WorkspaceAppearance::ConstructRightAlignedText(
            Surface,
            FontWeightClass::Light,
            { Rect.Right() - 14.0f, Rect.Bottom() - 34.0f },
            28.0f,
            ColorRgba{ 237u, 237u, 237u, 255u },
            NumBuf,
            true);
    };

    RenderStatTile(VisibleTile, "Visible", VisibleCount, true);
    RenderStatTile(HiddenTile, "Hidden", HiddenCount, false);
    CursorY += StatH + 10.0f;

    // 3. Capsule Search Field (36px tall, 18px radius, Ctrl F keycap)
    if (Input.ShortcutFind && !ConstructSheetOpen)
    {
        SearchBuffer.Active = true;
    }
    if (SearchBuffer.Active)
    {
        WorkspaceAppearance::ForwardInputToTextBuffer(SearchBuffer, Input, Δt);
    }

    const Rectangle2D SearchRect{ InnerX, CursorY, InnerW, 36.0f };
    Surface.FillRectangle(SearchRect, ColorRgba{ 255u, 255u, 255u, 12u }, 18.0f);
    Surface.StrokeRectangle(
        SearchRect,
        SearchBuffer.Active ? ColorRgba{ 232u, 194u, 122u, 160u } : ColorRgba{ 255u, 255u, 255u, 18u },
        18.0f,
        1.0f);
    VectorIconRepository::ConstructControl(
        Surface,
        ControlIcon::SearchGlass,
        { SearchRect.X + 13.0f, SearchRect.Y + 11.0f, 14.0f, ColorRgba{ 124u, 124u, 124u, 255u }, 1.6f });

    const Rectangle2D TextFieldRect{ SearchRect.X + 33.0f, SearchRect.Y + 4.0f, std::max(40.0f, SearchRect.Width - 82.0f), 28.0f };
    if (SearchBuffer.Length == 0u && !SearchBuffer.Active)
    {
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Regular,
            { TextFieldRect.X, SearchRect.Y + 11.0f },
            TextFieldRect.Width,
            12.0f,
            ColorRgba{ 103u, 103u, 103u, 255u },
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

    const Rectangle2D KbdRect{ SearchRect.Right() - 48.0f, SearchRect.Y + 9.0f, 38.0f, 18.0f };
    Surface.FillRectangle(KbdRect, ColorRgba{ 21u, 21u, 21u, 255u }, 5.0f);
    Surface.StrokeRectangle(KbdRect, ColorRgba{ 255u, 255u, 255u, 20u }, 5.0f, 1.0f);
    const float KbdTextW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.0f, "Ctrl F", true);
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { KbdRect.X + (KbdRect.Width - KbdTextW) * 0.5f, KbdRect.Y + 4.5f },
        9.0f,
        ColorRgba{ 142u, 142u, 142u, 255u },
        "Ctrl F",
        true);

    CursorY += 44.0f;

    // 4. Capsule Category Filter Strip: All, Effects, 14 Active Non-Quarantined Categories, Forces (Zero Quarantine!)
    const float FilterH   = 22.0f;
    const float FilterGap = 5.0f;
    float FilterX         = InnerX;
    float FilterY         = CursorY;

    auto RenderFilterButton = [&](const char* Label, bool Active, ColorRgba /*Accent*/, auto&& SelectCallback)
    {
        const float TextW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.5f, Label);
        const float BtnW  = TextW + 17.0f;
        if (FilterX + BtnW > InnerX + InnerW + 0.5f && FilterX > InnerX + 1.0f)
        {
            FilterX = InnerX;
            FilterY += FilterH + FilterGap;
        }
        const Rectangle2D FilterRect{ FilterX, FilterY, BtnW, FilterH };
        const bool Hovered = PointerEnabled && FilterRect.Contains(Cursor);
        const ColorRgba Fill = Active
            ? ColorRgba{ 255u, 255u, 255u, 24u }
            : (Hovered ? ColorRgba{ 255u, 255u, 255u, 10u } : ColorRgba{ 255u, 255u, 255u, 0u });
        const ColorRgba Border = Active
            ? ColorRgba{ 255u, 255u, 255u, 42u }
            : ColorRgba{ 255u, 255u, 255u, 18u };
        const ColorRgba Ink = Active
            ? ColorRgba{ 242u, 242u, 242u, 255u }
            : (Hovered ? ColorRgba{ 226u, 226u, 226u, 255u } : ColorRgba{ 143u, 143u, 143u, 255u });

        if (Fill.Alpha > 0u)
        {
            Surface.FillRectangle(FilterRect, Fill, FilterH * 0.5f);
        }
        Surface.StrokeRectangle(FilterRect, Border, FilterH * 0.5f, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            Active ? FontWeightClass::Medium : FontWeightClass::Regular,
            { FilterRect.X + 8.5f, FilterRect.Y + 4.5f },
            10.5f,
            Ink,
            Label);
        if (PrimaryClick && Hovered)
        {
            SelectCallback();
        }
        FilterX += BtnW + FilterGap;
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

    for (const ParticleCategoryDescriptor& Cat : ParticlePresetRegistry::QueryActiveCategories())
    {
        const bool CatActive = (FilterMode == ParticleOutlinerFilterMode::SingleDomain && FilteredCategory == Cat.Category);
        RenderFilterButton(
            Cat.ShortChip,
            CatActive,
            Cat.AccentTint,
            [&, CategoryCode = Cat.Category]() { ConfigureFilterCategory(CategoryCode); });
    }

    RenderFilterButton(
        "Forces",
        FilterMode == ParticleOutlinerFilterMode::ForcesOnly,
        ColorRgba{ 143u, 211u, 196u, 255u },
        [&]() { ConfigureFilterForces(); });

    CursorY = FilterY + FilterH + 10.0f;

    // 5. Scrollable Outliner Tree Viewport
    const float FooterH = 30.0f;
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
    float RowY = TreeViewport.Y + 2.0f - ScrollOffsetY;
    const float RowH = CompactMode ? 28.0f : 38.0f;
    const std::string_view SearchText(SearchBuffer.Text, SearchBuffer.Length);

    auto RenderGroupHeader = [&](const char* Title, uint32_t Count, bool& Expanded)
    {
        const Rectangle2D HeaderRect{ InnerX, RowY, InnerW, 22.0f };
        const bool Hovered = PointerEnabled && TreeViewport.Contains(Cursor) && HeaderRect.Contains(Cursor);
        if (Hovered)
        {
            Surface.FillRectangle(HeaderRect, ColorRgba{ 255u, 255u, 255u, 8u }, 6.0f);
        }
        WorkspaceAppearance::ConstructChevronSymbol(
            Surface,
            { HeaderRect.X + 7.0f, HeaderRect.Y + 11.0f },
            7.0f,
            Expanded,
            ColorRgba{ 104u, 104u, 104u, 255u });
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Medium,
            { HeaderRect.X + 18.0f, HeaderRect.Y + 5.0f },
            9.5f,
            ColorRgba{ 118u, 118u, 118u, 255u },
            Title);
        char CountBuf[16]{};
        std::snprintf(CountBuf, sizeof(CountBuf), "%u", Count);
        WorkspaceAppearance::ConstructRightAlignedText(
            Surface,
            FontWeightClass::Regular,
            { HeaderRect.Right() - 6.0f, HeaderRect.Y + 5.0f },
            10.0f,
            ColorRgba{ 92u, 92u, 92u, 255u },
            CountBuf,
            true);
        if (PrimaryClick && Hovered)
        {
            Expanded = !Expanded;
        }
        RowY += 24.0f;
    };

    auto RenderSubgroupLabel = [&](const std::string& Caption)
    {
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Regular,
            { InnerX + 10.0f, RowY + 4.0f },
            InnerW - 14.0f,
            9.5f,
            ColorRgba{ 98u, 98u, 98u, 255u },
            Caption);
        RowY += 18.0f;
    };

    auto RenderTreeRow = [&](
        const std::string& Title,
        const std::string& Summary,
        ColorRgba          Accent,
        bool               Selected,
        bool               Visible,
        bool               Nested,
        uint32_t           ForceLawOrdinal,
        const char*        /*StatusTag*/,
        auto&&             SelectCallback,
        auto&&             VisibilityCallback)
    {
        if (!SearchText.empty()
            && !ContainsCaseInsensitive(Title, SearchText)
            && !ContainsCaseInsensitive(Summary, SearchText))
        {
            return;
        }

        const Rectangle2D RowRect{ InnerX, RowY, InnerW, RowH };
        const bool RowVisibleOnScreen = (RowRect.Bottom() >= TreeViewport.Y - 4.0f) && (RowRect.Y <= TreeViewport.Bottom() + 4.0f);

        if (RowVisibleOnScreen)
        {
            const bool RowHover = PointerEnabled && TreeViewport.Contains(Cursor) && RowRect.Contains(Cursor);

            // Borderless slim HTML tree row: transparent at rest, subtle wash on hover, crisp highlight on selection
            if (Selected)
            {
                Surface.FillRectangle(RowRect, ColorRgba{ 255u, 255u, 255u, 23u }, 11.0f);
                Surface.StrokeRectangle(RowRect, ColorRgba{ 255u, 255u, 255u, 36u }, 11.0f, 1.0f);
                // Exact HTML .tree-row.selected::before 2.5px vertical accent indicator
                Surface.FillRectangle(
                    { RowRect.X + 2.0f, RowRect.Y + 9.0f, 2.5f, std::max(10.0f, RowRect.Height - 18.0f) },
                    Accent,
                    1.5f);
            }
            else if (RowHover)
            {
                Surface.FillRectangle(RowRect, ColorRgba{ 255u, 255u, 255u, 10u }, 11.0f);
            }

            const float ContentAlpha = Visible ? 1.0f : 0.45f;
            const float LeftPad      = Nested ? 22.0f : 10.0f;

            // 24x24px soft-tinted 8px-radius icon well (reuses Frontier ForceLawIcon for C++ ForceFields)
            const float IconBoxSize = CompactMode ? 18.0f : 24.0f;
            const Rectangle2D IconBox{
                RowRect.X + LeftPad,
                RowRect.Y + (RowRect.Height - IconBoxSize) * 0.5f,
                IconBoxSize,
                IconBoxSize,
            };
            Surface.FillRectangle(IconBox, ColorRgba{ 255u, 255u, 255u, 11u }, 8.0f);
            if (ForceLawOrdinal < static_cast<uint32_t>(ForceModel::Count))
            {
                WorkspaceAppearance::ConstructForceLawIcon(
                    Surface,
                    ForceLawOrdinal,
                    { IconBox.X + 4.5f, IconBox.Y + 4.5f },
                    IconBoxSize - 9.0f,
                    Accent,
                    ContentAlpha);
            }
            else
            {
                Surface.FillCircle(
                    IconBox.Center(),
                    CompactMode ? 3.8f : 4.8f,
                    Accent.ScaledAlpha(ContentAlpha));
                Surface.StrokeCircle(
                    IconBox.Center(),
                    CompactMode ? 5.8f : 7.4f,
                    Accent.ScaledAlpha(0.35f * ContentAlpha),
                    1.0f);
            }

            // Right-hand eye toggle (shown on hover, selected, or when hidden in soft red #D96B6B) + live status dot
            const Rectangle2D EyeRect{ RowRect.Right() - 28.0f, RowRect.Y + (RowRect.Height - 24.0f) * 0.5f, 24.0f, 24.0f };
            const bool EyeHover = PointerEnabled && TreeViewport.Contains(Cursor) && EyeRect.Contains(Cursor);
            const bool ShowEye  = RowHover || Selected || !Visible;
            if (ShowEye)
            {
                if (EyeHover)
                {
                    Surface.FillRectangle(EyeRect, ColorRgba{ 255u, 255u, 255u, 16u }, 12.0f);
                }
                VectorIconRepository::ConstructControl(
                    Surface,
                    Visible ? ControlIcon::EyeVisible : ControlIcon::EyeHidden,
                    {
                        EyeRect.X + 5.5f,
                        EyeRect.Y + 5.5f,
                        13.0f,
                        Visible ? ColorRgba{ 130u, 130u, 134u, 255u } : ColorRgba{ 217u, 107u, 107u, 255u },
                        1.55f,
                    });
            }

            const Point2D LiveDotCenter{
                (ShowEye ? EyeRect.X - 6.0f : RowRect.Right() - 12.0f),
                RowRect.Y + RowRect.Height * 0.5f,
            };
            if (Visible)
            {
                Surface.FillCircle(LiveDotCenter, 5.2f, ColorRgba{ 62u, 196u, 109u, 36u });
                Surface.FillCircle(LiveDotCenter, 3.0f, ColorRgba{ 62u, 196u, 109u, 255u });
            }
            else
            {
                Surface.FillCircle(LiveDotCenter, 3.0f, ColorRgba{ 61u, 61u, 61u, 255u });
            }

            const float CopyX = IconBox.Right() + 9.0f;
            const float CopyW = std::max(30.0f, LiveDotCenter.X - 10.0f - CopyX);
            const ColorRgba TitleInk = Selected
                ? ColorRgba{ 255u, 255u, 255u, 255u }
                : ColorRgba{ 220u, 220u, 220u, static_cast<uint8_t>(255.0f * ContentAlpha) };
            const ColorRgba MetaInk  = ColorRgba{ 110u, 110u, 110u, static_cast<uint8_t>(255.0f * ContentAlpha) };

            if (CompactMode)
            {
                WorkspaceAppearance::ConstructTruncatedText(
                    Surface,
                    FontWeightClass::Regular,
                    { CopyX, RowRect.Y + 7.5f },
                    CopyW,
                    11.5f,
                    TitleInk,
                    Title);
            }
            else
            {
                WorkspaceAppearance::ConstructTruncatedText(
                    Surface,
                    Selected ? FontWeightClass::Medium : FontWeightClass::Regular,
                    { CopyX, RowRect.Y + 5.5f },
                    CopyW,
                    12.0f,
                    TitleInk,
                    Title);
                WorkspaceAppearance::ConstructTruncatedText(
                    Surface,
                    FontWeightClass::Regular,
                    { CopyX, RowRect.Y + 21.0f },
                    CopyW,
                    10.0f,
                    MetaInk,
                    Summary);
            }

            if (PrimaryClick && RowHover)
            {
                if (EyeHover && ShowEye)
                {
                    VisibilityCallback();
                }
                else
                {
                    SelectCallback();
                }
            }
        }

        RowY += RowH + 2.0f;
    };

    if (Session != nullptr)
    {
        // A. Coupled C++ ForceFields Environment Group
        const bool ShowEnvironment = (FilterMode == ParticleOutlinerFilterMode::AllEntries
                                      || FilterMode == ParticleOutlinerFilterMode::ForcesOnly);
        if (ShowEnvironment)
        {
            auto ForceSpan = Session->AccessForceSources();
            RenderGroupHeader("ENVIRONMENT · C++ FORCEFIELDS", static_cast<uint32_t>(ForceSpan.size() + 2u), EnvironmentExpanded);

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

            std::string EffectsHeading = "PARTICLE EFFECTS";
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

    // 7. Interactive '+' Construct Overlay Sheet (93 Non-Quarantined Presets + 19 C++ ForceModel Entries)
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

    // Top mode segmented tabs: Particle Presets (93) | Force Models (19)
    const char* PageLabels[2] = { "Presets (93)", "Force Models (19)" };
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

    // Preset category shelf strip when browsing presets (matches Outliner capsule grammar)
    if (ActiveConstructPage == ConstructSheetPage::ParticlePresets)
    {
        float CatX = InnerX;
        float CatY = Y;
        for (const ParticleCategoryDescriptor& Cat : ParticlePresetRegistry::QueryActiveCategories())
        {
            const float BtnW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 9.5f, Cat.ShortChip) + 12.0f;
            if (CatX + BtnW > InnerX + InnerW + 0.5f && CatX > InnerX + 1.0f)
            {
                CatX = InnerX;
                CatY += 22.0f;
            }
            const Rectangle2D CatBtn{ CatX, CatY, BtnW, 19.0f };
            const bool Active  = (ConstructCategory == Cat.Category);
            const bool Hovered = Input.PrimaryPointer.Enabled && CatBtn.Contains(Cursor);
            Surface.FillRectangle(
                CatBtn,
                Active
                    ? ColorRgba{ 232u, 194u, 122u, 36u }
                    : (Hovered ? ColorRgba{ 255u, 255u, 255u, 14u } : ColorRgba{ 255u, 255u, 255u, 6u }),
                9.5f);
            Surface.StrokeRectangle(
                CatBtn,
                Active ? ColorRgba{ 232u, 194u, 122u, 170u } : ColorRgba{ 255u, 255u, 255u, 18u },
                9.5f,
                1.0f);
            WorkspaceAppearance::ConstructWeightedText(
                Surface,
                Active ? FontWeightClass::Medium : FontWeightClass::Regular,
                { CatBtn.X + 6.0f, CatBtn.Y + 3.5f },
                9.5f,
                Active ? ColorRgba{ 246u, 224u, 180u, 255u } : ColorRgba{ 166u, 166u, 166u, 255u },
                Cat.ShortChip);
            if (Clicked && Hovered)
            {
                ConstructCategory  = Cat.Category;
                SheetScrollOffsetY = 0.0f;
            }
            CatX += BtnW + 4.0f;
        }
        Y = CatY + 26.0f;
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
            const Rectangle2D EntryRect{ ListViewport.X, EntryY, ListViewport.Width, 35.0f };
            const bool Hovered = Input.PrimaryPointer.Enabled && ListViewport.Contains(Cursor) && EntryRect.Contains(Cursor);
            Surface.FillRectangle(
                EntryRect,
                Hovered ? ColorRgba{ 255u, 255u, 255u, 18u } : ColorRgba{ 255u, 255u, 255u, 6u },
                10.0f);
            if (Hovered)
            {
                Surface.StrokeRectangle(EntryRect, ColorRgba{ 232u, 194u, 122u, 120u }, 10.0f, 1.0f);
            }

            const ColorRgba DotColor = (Preset.ColorA.Luminance() < 0.08f)
                ? ParticlePresetRegistry::QueryCategoryDescriptor(Preset.Category).AccentTint
                : Preset.ColorA;
            Surface.FillRectangle({ EntryRect.X + 6.0f, EntryRect.Y + 6.5f, 22.0f, 22.0f }, ColorRgba{ DotColor.Red, DotColor.Green, DotColor.Blue, 32u }, 7.0f);
            Surface.FillCircle({ EntryRect.X + 17.0f, EntryRect.Y + 17.5f }, 4.5f, DotColor);
            WorkspaceAppearance::ConstructTruncatedText(
                Surface,
                FontWeightClass::Regular,
                { EntryRect.X + 35.0f, EntryRect.Y + 5.0f },
                EntryRect.Width - 44.0f,
                11.0f,
                ColorRgba{ 236u, 236u, 236u, 255u },
                Preset.DisplayName);
            char MetaBuf[64]{};
            std::snprintf(MetaBuf, sizeof(MetaBuf), "%s · %u slots", Preset.Sublabel, Preset.Capacity);
            WorkspaceAppearance::ConstructTruncatedText(
                Surface,
                FontWeightClass::Regular,
                { EntryRect.X + 35.0f, EntryRect.Y + 19.0f },
                EntryRect.Width - 44.0f,
                9.5f,
                ColorRgba{ 126u, 126u, 126u, 255u },
                MetaBuf);

            if (Clicked && Hovered && Session != nullptr)
            {
                (void)Session->AddEffectFromPreset(Preset.Identifier);
                ConstructSheetOpen = false;
            }
            EntryY += 38.0f;
        }
    }
    else
    {
        for (uint32_t ModelOrd = 0u; ModelOrd < static_cast<uint32_t>(ForceModel::Count); ++ModelOrd)
        {
            const ForceModel Model = static_cast<ForceModel>(ModelOrd);
            const ColorRgba Tint = WorkspaceAppearance::QueryForceLawTint(ModelOrd);
            const Rectangle2D EntryRect{ ListViewport.X, EntryY, ListViewport.Width, 34.0f };
            const bool Hovered = Input.PrimaryPointer.Enabled && ListViewport.Contains(Cursor) && EntryRect.Contains(Cursor);
            Surface.FillRectangle(
                EntryRect,
                Hovered ? ColorRgba{ 255u, 255u, 255u, 18u } : ColorRgba{ 255u, 255u, 255u, 6u },
                10.0f);
            if (Hovered)
            {
                Surface.StrokeRectangle(EntryRect, ColorRgba{ Tint.Red, Tint.Green, Tint.Blue, 130u }, 10.0f, 1.0f);
            }

            WorkspaceAppearance::ConstructForceLawIcon(
                Surface,
                ModelOrd,
                { EntryRect.X + 8.0f, EntryRect.Y + 9.0f },
                16.0f,
                Tint);
            WorkspaceAppearance::ConstructTruncatedText(
                Surface,
                FontWeightClass::Medium,
                { EntryRect.X + 30.0f, EntryRect.Y + 8.5f },
                EntryRect.Width - 38.0f,
                11.2f,
                WorkspaceSurfaceColors::PrimaryInk,
                QueryForceModelDisplayName(Model));

            if (Clicked && Hovered && Session != nullptr)
            {
                (void)Session->AddForceSource(Model);
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
