/*============================================================================================================================================
                                                           LOGSTREAMDRAWER.CPP
============================================================================================================================================*/
// 📜 Implementation of LogStreamDrawer providing severity badges, category filter pills, and diagnostic log tail rendering

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Diagnostics/LogStreamDrawer.h"

#include <algorithm>
#include <array>

namespace Frontier::LogStreamDrawer
{

namespace
{

[[nodiscard]] bool MatchesFilter(const LogStreamEntry& Entry, LogStreamFilter Filter) noexcept
{
    switch (Filter)
    {
    case LogStreamFilter::All:     return true;
    case LogStreamFilter::Compute: return Entry.CategoryTag == "Compute";
    case LogStreamFilter::Codec:   return Entry.CategoryTag == "Codec";
    case LogStreamFilter::Force:   return Entry.CategoryTag == "Force";
    case LogStreamFilter::Rig:     return Entry.CategoryTag == "Rig";
    }
    return true;
}

[[nodiscard]] ColorRgba SeverityInk(LogStreamSeverity Sev) noexcept
{
    switch (Sev)
    {
    case LogStreamSeverity::Notice:  return { 116u, 178u, 255u, 255u };
    case LogStreamSeverity::Success: return { 66u, 214u, 144u, 255u };
    case LogStreamSeverity::Warning: return { 236u, 172u, 64u, 255u };
    case LogStreamSeverity::Error:   return { 240u, 96u, 96u, 255u };
    }
    return { 180u, 184u, 196u, 255u };
}

} // namespace

void Construct(
    PixelSpace&                     Surface,
    Rectangle2D                     Bounds,
    std::span<const LogStreamEntry> Entries,
    LogStreamDrawerSpec&            Spec,
    const ControlPointer&           Pointer,
    float                           Opacity) noexcept
{
    Spec.ClearClicked = false;
    if (!Spec.Expanded)
    {
        return;
    }

    Surface.FillRectangle(Bounds, ColorRgba{ 16u, 17u, 22u, 252u }.ScaledAlpha(Opacity), 12.0f);
    Surface.StrokeRectangle(Bounds, ColorRgba{ 54u, 58u, 72u, 240u }.ScaledAlpha(Opacity), 12.0f, 1.0f);

    Surface.Text(
        { Bounds.X + 14.0f, Bounds.Y + 11.0f },
        12.0f,
        ColorRgba{ 230u, 234u, 242u, 255u }.ScaledAlpha(Opacity),
        "Diagnostic Log Stream");

    constexpr std::array<const char*, 5u> FilterNames {{ "All", "Compute", "Codec", "Force", "Rig" }};
    float ChipX = Bounds.X + 178.0f;
    for (uint32_t Idx = 0u; Idx < FilterNames.size(); ++Idx)
    {
        const Rectangle2D Chip{ ChipX, Bounds.Y + 7.0f, 64.0f, 22.0f };
        const bool Active = (static_cast<uint32_t>(Spec.ActiveFilter) == Idx);
        const bool Hover  = Pointer.Enabled && Chip.Contains({ Pointer.X, Pointer.Y });
        Surface.FillRectangle(
            Chip,
            (Active ? ColorRgba{ 48u, 58u, 78u, 255u } : (Hover ? ColorRgba{ 36u, 39u, 48u, 255u } : ColorRgba{ 25u, 27u, 34u, 255u })).ScaledAlpha(Opacity),
            11.0f);
        Surface.Text(
            { Chip.X + 12.0f, Chip.Y + 5.0f },
            10.5f,
            (Active ? ColorRgba{ 240u, 244u, 252u, 255u } : ColorRgba{ 156u, 162u, 176u, 255u }).ScaledAlpha(Opacity),
            FilterNames[Idx]);
        if (Hover && Pointer.Pressed)
        {
            Spec.ActiveFilter = static_cast<LogStreamFilter>(Idx);
        }
        ChipX += 70.0f;
    }

    const Rectangle2D ClearBtn{ Bounds.Right() - 78.0f, Bounds.Y + 7.0f, 64.0f, 22.0f };
    const bool HoverClear = Pointer.Enabled && ClearBtn.Contains({ Pointer.X, Pointer.Y });
    Surface.FillRectangle(
        ClearBtn,
        (HoverClear ? ColorRgba{ 58u, 38u, 42u, 255u } : ColorRgba{ 32u, 34u, 42u, 255u }).ScaledAlpha(Opacity),
        11.0f);
    Surface.Text(
        { ClearBtn.X + 16.0f, ClearBtn.Y + 5.0f },
        10.5f,
        ColorRgba{ 220u, 224u, 232u, 255u }.ScaledAlpha(Opacity),
        "Clear");
    if (HoverClear && Pointer.Pressed)
    {
        Spec.ClearClicked = true;
    }

    float RowY = Bounds.Y + 38.0f;
    const float MaxY = Bounds.Bottom() - 12.0f;
    for (auto It = Entries.rbegin(); It != Entries.rend(); ++It)
    {
        if (RowY + 18.0f > MaxY)
        {
            break;
        }
        if (!MatchesFilter(*It, Spec.ActiveFilter))
        {
            continue;
        }
        const ColorRgba DotInk = SeverityInk(It->Severity).ScaledAlpha(Opacity);
        Surface.FillCircle({ Bounds.X + 18.0f, RowY + 7.0f }, 3.5f, DotInk);
        Surface.Text({ Bounds.X + 30.0f, RowY + 1.0f }, 10.5f, ColorRgba{ 126u, 132u, 146u, 255u }.ScaledAlpha(Opacity), It->TimestampText.c_str());
        Surface.Text({ Bounds.X + 96.0f, RowY + 1.0f }, 10.5f, DotInk, It->CategoryTag.c_str());
        Surface.Text({ Bounds.X + 160.0f, RowY + 1.0f }, 10.5f, ColorRgba{ 214u, 218u, 228u, 255u }.ScaledAlpha(Opacity), It->Message.c_str());
        RowY += 20.0f;
    }
}

} // namespace Frontier::LogStreamDrawer
