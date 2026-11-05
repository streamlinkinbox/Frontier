/*==========================================================================================================================================
                                                               SLIDERROW.CPP
==========================================================================================================================================*/
// 🧩 .co-slider: label, editable figure and unit over the accent track (ObservatorySlider.js, SliderControl.css).

#include "Engine/Interface/Controls/SliderRow.h"

#include "Engine/Interface/Controls/ControlColumn.h"
#include "Engine/Interface/Controls/GlyphRun.h"
#include "Engine/Interface/InspectorControls.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Frontier
{

namespace
{

// 📝 The digits SliderControl.js shows: the larger decimal count of Minimum and Step (SliderDigits).
[[nodiscard]] int ResolveSliderDigits(float Minimum, float Step) noexcept
{
    auto Places = [](float Quantity) {
        char Text[48]{};
        std::snprintf(Text, sizeof(Text), "%.6f", static_cast<double>(std::fabs(Quantity)));
        int Count = 0;
        const char* Point = std::strchr(Text, '.');
        if (Point == nullptr) return 0;
        int Last = 0;
        for (int Index = 1; Point[Index] != '\0'; ++Index)
        {
            if (Point[Index] != '0') Last = Index;
        }
        Count = Last;
        return Count;
    };
    return std::min(10, std::max(Places(Minimum), Places(Step)));
}

// 📝 SnapSlider: clamp onto Minimum + k·Step.
[[nodiscard]] float SnapSliderSetting(float Quantity, float Minimum, float Maximum, float Step) noexcept
{
    if (Step <= 0.0f) return std::clamp(Quantity, Minimum, Maximum);
    const float Snapped = Minimum + std::round((Quantity - Minimum) / Step) * Step;
    return std::clamp(Snapped, Minimum, Maximum);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    SLIDER ROW
//------------------------------------------------------------------------------------------------------------------------

bool SliderRow::Construct(ControlColumn& Column, const char* Id, const char* Label, float& Quantity, float Minimum, float Maximum, float Step,
                          const char* Unit, float DisplayScale, bool Enabled, const char* Tooltip)
{
    const ControlTheme&    Theme    = Column.QueryTheme();
    const EditorTypefaces& Faces    = Column.QueryFaces();
    ImDrawList* const      DrawList = Column.QueryDrawList();
    const float            Scale    = Column.QueryScale();
    const float            Width    = Column.QueryWidth();
    // 📝 .co-slider (narrow): border-top 1 px, padding 11 px 0 9 px; head (label 11 px, figure light 18 px + unit 9 px) 24 px,
    //    margin-bottom 10 px; the 14 px accent track filled to --slider-fill; a 17 px accent thumb in a 3 px ring.
    const float  S     = Scale;
    const bool Capsule = Theme.CapsuleSliders;
    const float PaddingTop = Capsule ? 0.0f : 12.0f;
    const float HeadGap    = Capsule ? 12.0f : 10.0f;
    const float TrackHeight = Capsule ? 22.0f : 14.0f;
    const float ThumbRadius = Capsule ? 10.0f : 8.5f;
    const float FigureSize  = Capsule ? 11.0f : 18.0f;
    const float UnitSize    = Capsule ? 10.0f : 9.0f;
    const ImVec2 Top   = Column.Space(PaddingTop + 24.0f + HeadGap + TrackHeight + (Capsule ? 0.0f : 9.0f), Theme.SliderMargin);
    const float  Fade  = Enabled ? 1.0f : 0.45f;
    bool         Moved = false;
    DrawList->AddLine(ImVec2(Top.x, Top.y + 0.5f * S), ImVec2(Top.x + Width, Top.y + 0.5f * S), Theme.SliderRule, S);

    const float HeadTop = Top.y + PaddingTop * S;
    (void)GlyphRun::Construct(Column, Faces.Regular, 11.0f * S, ImVec2(Top.x, GlyphRun::ResolveBaseline(HeadTop + 0.5f * (24.0f - 11.0f * 1.4f) * S, 11.0f * S, 1.4f)),
                            FadeColour(Theme.ControlLabel, Fade), Label);

    // 📝 The figure: SliderDigits of Minimum × scale and Step × scale; the unit right-aligned after a 4 px gap.
    const int   Digits     = ResolveSliderDigits(Minimum * DisplayScale, Step * DisplayScale);
    char        Figure[48] = {};
    std::snprintf(Figure, sizeof(Figure), "%.*f", Digits, static_cast<double>(Quantity * DisplayScale));
    const float UnitWide   = GlyphRun::Measure(Column, Faces.Regular, UnitSize * S, Unit);
    const float FigureWide = GlyphRun::Measure(Column, Faces.Light, FigureSize * S, Figure);
    const float PillRight  = Top.x + Width - 5.0f * S;
    const float UnitLeft   = PillRight - UnitWide;
    const float FigureLeft = UnitLeft - (Unit != nullptr && Unit[0] != '\0' ? 4.0f * S : 0.0f) - FigureWide;
    const float Baseline   = HeadTop + 0.5f * (24.0f * S - GlyphRun::EmPixelHeight * FigureSize * S) + GlyphRun::EmAscent * FigureSize * S;

    ImGui::PushID(Id);
    const ImGuiID FigureId = ImGui::GetID("##Figure");
    const ImVec2  FigureMin(PillRight - std::max(78.0f * S, UnitWide + FigureWide + 12.0f * S), HeadTop);
    const ImVec2  FigureMax(PillRight + 4.0f * S, HeadTop + 24.0f * S);
    if (Capsule) DrawList->AddRectFilled(FigureMin, FigureMax, Theme.SliderCapsule, 20.0f * S);
    if (!Enabled) ImGui::BeginDisabled();
    bool Typing = ImGui::TempInputIsActive(FigureId);
    if (!Typing)
    {
        ImGui::SetCursorScreenPos(FigureMin);
        if (ImGui::InvisibleButton("##Figure", ImVec2(FigureMax.x - FigureMin.x, FigureMax.y - FigureMin.y), ImGuiButtonFlags_EnableNav) && Enabled)
        {
            Typing = true;
        }
        ControlColumn::ConstructTooltip(Tooltip);
    }
    if (Typing && Enabled)
    {
        float Display        = Quantity * DisplayScale;
        char  Format[16]     = {};
        std::snprintf(Format, sizeof(Format), "%%.%df", Digits);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ResolveCssColour(0x00000000));
        ImGui::PushStyleColor(ImGuiCol_Text, Theme.SliderNumber);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 9.0f * S);
        if (ImGui::TempInputScalar(ImRect(FigureMin, FigureMax), FigureId, "##FigureEntry", ImGuiDataType_Float, &Display, Format, nullptr, nullptr))
        {
            if (std::isfinite(Display) && DisplayScale > 0.0f)
            {
                const float Next = SnapSliderSetting(Display / DisplayScale, Minimum, Maximum, Step);
                Moved = Next != Quantity;
                Quantity = Next;
            }
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        DrawList->AddRect(FigureMin, FigureMax, Theme.Accent, 9.0f * S, ImDrawFlags_None, S);
    }
    else
    {
        (void)GlyphRun::Construct(Column, Faces.Light, FigureSize * S, ImVec2(FigureLeft, Baseline), FadeColour(Theme.SliderNumber, Fade), Figure);
    }
    (void)GlyphRun::Construct(Column, Faces.Regular, UnitSize * S, ImVec2(UnitLeft, Baseline), FadeColour(Theme.SliderUnit, Fade), Unit);

    // 📝 The track: full width, 14 px; the thumb centre runs from 8.5 px to width − 8.5 px.
    const float  TrackTop = HeadTop + (24.0f + HeadGap) * S;
    const ImVec2 TrackMin(Top.x, TrackTop);
    const ImVec2 TrackMax(Top.x + Width, TrackTop + TrackHeight * S);
    ImGui::SetCursorScreenPos(ImVec2(TrackMin.x, TrackMin.y - 3.0f * S));
    ImGui::InvisibleButton("##Track", ImVec2(Width, (TrackHeight + 6.0f) * S), ImGuiButtonFlags_EnableNav);
    ControlColumn::ConstructTooltip(Tooltip);
    const float Travel = std::max(1.0f, Width - 2.0f * ThumbRadius * S);
    if (Enabled && ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left) && Maximum > Minimum)
    {
        const float Fraction = std::clamp((ImGui::GetIO().MousePos.x - (TrackMin.x + ThumbRadius * S)) / Travel, 0.0f, 1.0f);
        const float Snapped  = SnapSliderSetting(Minimum + Fraction * (Maximum - Minimum), Minimum, Maximum, Step);
        if (Snapped != Quantity)
        {
            Quantity = Snapped;
            Moved    = true;
        }
    }
    if (Enabled && ImGui::IsItemFocused())
    {
        float Next = Quantity;
        const float Increment = Step * (ImGui::GetIO().KeyShift ? 10.0f : 1.0f);
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow)) Next -= Increment;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow)) Next += Increment;
        if (ImGui::IsKeyPressed(ImGuiKey_Home)) Next = Minimum;
        if (ImGui::IsKeyPressed(ImGuiKey_End)) Next = Maximum;
        Next = SnapSliderSetting(Next, Minimum, Maximum, Step);
        if (Next != Quantity) { Quantity = Next; Moved = true; }
    }
    if (!Enabled) ImGui::EndDisabled();
    ImGui::PopID();

    const float Fill  = Maximum > Minimum ? std::clamp((Quantity - Minimum) / (Maximum - Minimum), 0.0f, 1.0f) : 0.0f;
    const float Split = TrackMin.x + Fill * Width;
    DrawList->AddRectFilled(TrackMin, TrackMax, FadeColour(Theme.SliderEmpty, Fade), 11.0f * S);
    if (Split > TrackMin.x + 0.5f)
    {
        DrawList->PushClipRect(TrackMin, ImVec2(Split, TrackMax.y), true);
        DrawList->AddRectFilled(TrackMin, TrackMax, FadeColour(Capsule ? ResolveCssColour(0x555650FF) : Theme.Accent, Fade), 11.0f * S);
        DrawList->PopClipRect();
    }
    const ImVec2 Thumb(TrackMin.x + ThumbRadius * S + Fill * Travel, TrackMin.y + 0.5f * TrackHeight * S);
    if (Capsule)
    {
        DrawList->AddCircleFilled(Thumb, ThumbRadius * S, FadeColour(ResolveCssColour(0xE2E4D8FF), Fade), 32);
        return Moved;
    }
    DrawList->AddCircleFilled(Thumb, 9.5f * S, FadeColour(Theme.ThumbHalo, Fade), 32);
    DrawList->AddCircleFilled(Thumb, ThumbRadius * S, FadeColour(Theme.ThumbRing, Fade), 32);
    DrawList->AddCircleFilled(Thumb, 5.5f * S, FadeColour(Theme.Accent, Fade), 32);
    return Moved;
}

bool SliderRow::ConstructCaptioned(ControlColumn& Column, const char* Id, const char* Label, float& Quantity, float Minimum, float Maximum, float Step,
                                   const char* Unit, const char* LowCaption, const char* HighCaption, float DisplayScale, bool Enabled, const char* Tooltip)
{
    const bool Moved = Construct(Column, Id, Label, Quantity, Minimum, Maximum, Step, Unit, DisplayScale, Enabled, Tooltip);
    if (LowCaption == nullptr && HighCaption == nullptr) return Moved;
    // 📝 .range-labels: 9 px, space-between, in the unit's grey; a 12 px line.
    const EditorTypefaces& Faces = Column.QueryFaces();
    const float            S     = Column.QueryScale();
    const ImVec2           Top   = Column.Space(12.0f, 0.0f, 3.0f);
    const ImU32            Ink   = FadeColour(Column.QueryTheme().SliderUnit, Enabled ? 1.0f : 0.45f);
    const ImVec2           Base(Top.x, Top.y + GlyphRun::EmAscent * 9.0f * S);
    if (LowCaption != nullptr) (void)GlyphRun::Construct(Column, Faces.Regular, 9.0f * S, Base, Ink, LowCaption);
    if (HighCaption != nullptr)
    {
        const float Wide = GlyphRun::Measure(Column, Faces.Regular, 9.0f * S, HighCaption);
        (void)GlyphRun::Construct(Column, Faces.Regular, 9.0f * S, ImVec2(Top.x + Column.QueryWidth() - Wide, Base.y), Ink, HighCaption);
    }
    return Moved;
}

bool SliderRow::ConstructInteger(ControlColumn& Column, const char* Id, const char* Label, uint32_t& Quantity, uint32_t Minimum,
                                 uint32_t Maximum, const char* Unit, bool Enabled, const char* Tooltip)
{
    float Setting = static_cast<float>(Quantity);
    if (!Construct(Column, Id, Label, Setting, static_cast<float>(Minimum), static_cast<float>(Maximum), 1.0f, Unit, 1.0f, Enabled, Tooltip))
    {
        return false;
    }
    const uint32_t Rounded = static_cast<uint32_t>(std::lround(std::clamp(Setting, static_cast<float>(Minimum), static_cast<float>(Maximum))));
    const bool     Moved   = Rounded != Quantity;
    Quantity               = Rounded;
    return Moved;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    DISTANCE ROW
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📝 Three significant figures (or the step, when coarser), so a logarithmic drag lands on readable distances.
[[nodiscard]] double SnapDistance(double Metres, double Minimum, double Maximum, double Step) noexcept
{
    const double Clamped = std::clamp(Metres, Minimum, Maximum);
    const double Quantum = std::max(Step, std::pow(10.0, std::floor(std::log10(std::max(Clamped, 1.0e-6))) - 2.0));
    return std::clamp(std::round(Clamped / Quantum) * Quantum, Minimum, Maximum);
}

[[nodiscard]] double ResolveDistanceShare(double Metres, double Minimum, double Maximum) noexcept
{
    if (!(Maximum > Minimum) || Minimum <= 0.0) return 0.0;
    return std::clamp(std::log(std::max(Metres, Minimum) / Minimum) / std::log(Maximum / Minimum), 0.0, 1.0);
}

} // namespace

void SliderRow::FormatDistance(double Metres, char* Text, uint32_t Capacity) noexcept
{
    const double Size = std::fabs(Metres);
    if (Size >= 1000.0)
        std::snprintf(Text, Capacity, Size >= 100000.0 ? "%.0f km" : (Size >= 10000.0 ? "%.1f km" : "%.2f km"), Metres / 1000.0);
    else
        std::snprintf(Text, Capacity, Size >= 100.0 ? "%.0f m" : (Size >= 10.0 ? "%.1f m" : "%.2f m"), Metres);
}

bool SliderRow::ConstructDistance(ControlColumn& Column, const char* Id, const char* Label, double& Metres, double Minimum, double Maximum,
                                  double Step, bool Enabled, const char* Tooltip)
{
    const ControlTheme&    Theme    = Column.QueryTheme();
    const EditorTypefaces& Faces    = Column.QueryFaces();
    ImDrawList* const      DrawList = Column.QueryDrawList();
    const float            S        = Column.QueryScale();
    const float            Width    = Column.QueryWidth();
    const bool  Capsule     = Theme.CapsuleSliders;
    const float PaddingTop  = Capsule ? 0.0f : 12.0f;
    const float HeadGap     = Capsule ? 12.0f : 10.0f;
    const float TrackHeight = Capsule ? 22.0f : 14.0f;
    const float ThumbRadius = Capsule ? 10.0f : 8.5f;
    const float FigureSize  = Capsule ? 11.0f : 18.0f;
    const float UnitSize    = Capsule ? 10.0f : 9.0f;
    const ImVec2 Top  = Column.Space(PaddingTop + 24.0f + HeadGap + TrackHeight + (Capsule ? 0.0f : 9.0f), Theme.SliderMargin);
    const float  Fade = Enabled ? 1.0f : 0.45f;
    bool         Moved = false;
    DrawList->AddLine(ImVec2(Top.x, Top.y + 0.5f * S), ImVec2(Top.x + Width, Top.y + 0.5f * S), Theme.SliderRule, S);

    const float HeadTop = Top.y + PaddingTop * S;
    (void)GlyphRun::Construct(Column, Faces.Regular, 11.0f * S, ImVec2(Top.x, GlyphRun::ResolveBaseline(HeadTop + 0.5f * (24.0f - 11.0f * 1.4f) * S, 11.0f * S, 1.4f)),
                              FadeColour(Theme.ControlLabel, Fade), Label);

    // 📝 The figure and its unit from one formatted distance ("850 m", "2.60 km").
    char Formatted[48] = {};
    FormatDistance(Metres, Formatted, sizeof(Formatted));
    char        Figure[48] = {};
    const char* Space      = std::strchr(Formatted, ' ');
    const char* Unit       = Space != nullptr ? Space + 1 : "";
    std::snprintf(Figure, sizeof(Figure), "%.*s", Space != nullptr ? static_cast<int>(Space - Formatted) : 47, Formatted);
    const bool  Kilometres = std::fabs(Metres) >= 1000.0;
    const float UnitWide   = GlyphRun::Measure(Column, Faces.Regular, UnitSize * S, Unit);
    const float FigureWide = GlyphRun::Measure(Column, Faces.Light, FigureSize * S, Figure);
    const float PillRight  = Top.x + Width - 5.0f * S;
    const float UnitLeft   = PillRight - UnitWide;
    const float FigureLeft = UnitLeft - 4.0f * S - FigureWide;
    const float Baseline   = HeadTop + 0.5f * (24.0f * S - GlyphRun::EmPixelHeight * FigureSize * S) + GlyphRun::EmAscent * FigureSize * S;

    ImGui::PushID(Id);
    const ImGuiID FigureId = ImGui::GetID("##Figure");
    const ImVec2  FigureMin(PillRight - std::max(78.0f * S, UnitWide + FigureWide + 12.0f * S), HeadTop);
    const ImVec2  FigureMax(PillRight + 4.0f * S, HeadTop + 24.0f * S);
    if (Capsule) DrawList->AddRectFilled(FigureMin, FigureMax, Theme.SliderCapsule, 20.0f * S);
    if (!Enabled) ImGui::BeginDisabled();
    bool Typing = ImGui::TempInputIsActive(FigureId);
    if (!Typing)
    {
        ImGui::SetCursorScreenPos(FigureMin);
        if (ImGui::InvisibleButton("##Figure", ImVec2(FigureMax.x - FigureMin.x, FigureMax.y - FigureMin.y), ImGuiButtonFlags_EnableNav) && Enabled)
            Typing = true;
        ControlColumn::ConstructTooltip(Tooltip);
    }
    if (Typing && Enabled)
    {
        // 📝 Typed in the unit shown: kilometres stay kilometres.
        double Display = Kilometres ? Metres / 1000.0 : Metres;
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ResolveCssColour(0x00000000));
        ImGui::PushStyleColor(ImGuiCol_Text, Theme.SliderNumber);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 9.0f * S);
        if (ImGui::TempInputScalar(ImRect(FigureMin, FigureMax), FigureId, "##FigureEntry", ImGuiDataType_Double, &Display, "%.3f", nullptr, nullptr))
        {
            if (std::isfinite(Display))
            {
                const double Next = SnapDistance(Kilometres ? Display * 1000.0 : Display, Minimum, Maximum, Step);
                Moved  = Next != Metres;
                Metres = Next;
            }
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        DrawList->AddRect(FigureMin, FigureMax, Theme.Accent, 9.0f * S, ImDrawFlags_None, S);
    }
    else
    {
        (void)GlyphRun::Construct(Column, Faces.Light, FigureSize * S, ImVec2(FigureLeft, Baseline), FadeColour(Theme.SliderNumber, Fade), Figure);
    }
    (void)GlyphRun::Construct(Column, Faces.Regular, UnitSize * S, ImVec2(UnitLeft, Baseline), FadeColour(Theme.SliderUnit, Fade), Unit);

    const float  TrackTop = HeadTop + (24.0f + HeadGap) * S;
    const ImVec2 TrackMin(Top.x, TrackTop);
    const ImVec2 TrackMax(Top.x + Width, TrackTop + TrackHeight * S);
    ImGui::SetCursorScreenPos(ImVec2(TrackMin.x, TrackMin.y - 3.0f * S));
    ImGui::InvisibleButton("##Track", ImVec2(Width, (TrackHeight + 6.0f) * S), ImGuiButtonFlags_EnableNav);
    ControlColumn::ConstructTooltip(Tooltip);
    const float Travel = std::max(1.0f, Width - 2.0f * ThumbRadius * S);
    if (Enabled && ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left) && Maximum > Minimum && Minimum > 0.0)
    {
        const double Share   = std::clamp(static_cast<double>((ImGui::GetIO().MousePos.x - (TrackMin.x + ThumbRadius * S)) / Travel), 0.0, 1.0);
        const double Snapped = SnapDistance(Minimum * std::pow(Maximum / Minimum, Share), Minimum, Maximum, Step);
        if (Snapped != Metres)
        {
            Metres = Snapped;
            Moved  = true;
        }
    }
    if (Enabled && ImGui::IsItemFocused())
    {
        // 📝 Arrow keys move 2 % of a decade (20 % with Shift).
        double Next = Metres;
        const double Factor = std::pow(10.0, ImGui::GetIO().KeyShift ? 0.2 : 0.02);
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow)) Next /= Factor;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow)) Next *= Factor;
        if (ImGui::IsKeyPressed(ImGuiKey_Home)) Next = Minimum;
        if (ImGui::IsKeyPressed(ImGuiKey_End)) Next = Maximum;
        Next = SnapDistance(Next, Minimum, Maximum, Step);
        if (Next != Metres) { Metres = Next; Moved = true; }
    }
    if (!Enabled) ImGui::EndDisabled();
    ImGui::PopID();

    const float Fill  = static_cast<float>(ResolveDistanceShare(Metres, Minimum, Maximum));
    const float Split = TrackMin.x + Fill * Width;
    DrawList->AddRectFilled(TrackMin, TrackMax, FadeColour(Theme.SliderEmpty, Fade), 11.0f * S);
    // 📝 Decade ticks under the track: the logarithmic scale is visible, not only felt.
    if (Minimum > 0.0 && Maximum > Minimum)
    {
        for (double Decade = std::pow(10.0, std::ceil(std::log10(Minimum))); Decade < Maximum; Decade *= 10.0)
        {
            const float X = TrackMin.x + ThumbRadius * S + static_cast<float>(ResolveDistanceShare(Decade, Minimum, Maximum)) * Travel;
            DrawList->AddLine(ImVec2(X, TrackMax.y + 2.0f * S), ImVec2(X, TrackMax.y + 5.0f * S), FadeColour(Theme.SliderUnit, 0.6f * Fade), S);
        }
    }
    if (Split > TrackMin.x + 0.5f)
    {
        DrawList->PushClipRect(TrackMin, ImVec2(Split, TrackMax.y), true);
        DrawList->AddRectFilled(TrackMin, TrackMax, FadeColour(Capsule ? ResolveCssColour(0x555650FF) : Theme.Accent, Fade), 11.0f * S);
        DrawList->PopClipRect();
    }
    const ImVec2 Thumb(TrackMin.x + ThumbRadius * S + Fill * Travel, TrackMin.y + 0.5f * TrackHeight * S);
    if (Capsule)
    {
        DrawList->AddCircleFilled(Thumb, ThumbRadius * S, FadeColour(ResolveCssColour(0xE2E4D8FF), Fade), 32);
        return Moved;
    }
    DrawList->AddCircleFilled(Thumb, 9.5f * S, FadeColour(Theme.ThumbHalo, Fade), 32);
    DrawList->AddCircleFilled(Thumb, ThumbRadius * S, FadeColour(Theme.ThumbRing, Fade), 32);
    DrawList->AddCircleFilled(Thumb, 5.5f * S, FadeColour(Theme.Accent, Fade), 32);
    return Moved;
}

} // namespace Frontier
