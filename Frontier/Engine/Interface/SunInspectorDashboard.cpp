/*====================================================================================================================================
                                                    SUNINSPECTORDASHBOARD.CPP
====================================================================================================================================*/
// Native ImGui port of VehicleHMI's SunInspectorDashboard.js and SunInspector.css.
// Keep its four pages, instrument order, labels, diagrams, and control grouping; only the widget backend is native.

#include "Engine/Interface/SunInspectorDashboard.h"

#if defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/ControlKit.h"
#include "Engine/Interface/VehicleHmiGlyphs.h"
#include "Engine/Rendering/CelestialModel.h"
#include "Engine/Scene/EnvironmentRecords.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace Frontier
{
namespace
{
constexpr double Pi = 3.14159265358979323846;
constexpr double DegreesToRadians = Pi / 180.0;
constexpr double RadiansToDegrees = 180.0 / Pi;
constexpr double PlanckC2 = 0.01438776877;
int CapturePage = 0;

constexpr ImU32 RGB(unsigned Red, unsigned Green, unsigned Blue, unsigned Alpha = 255u) noexcept
{
    return IM_COL32(Red, Green, Blue, Alpha);
}

ImU32 Hex(unsigned Value, unsigned Alpha = 255u) noexcept
{
    return RGB((Value >> 16u) & 0xffu, (Value >> 8u) & 0xffu, Value & 0xffu, Alpha);
}

ColorQuad Quad(ImU32 Colour) noexcept
{
    const ImVec4 Value = ImGui::ColorConvertU32ToFloat4(Colour);
    return {Value.x, Value.y, Value.z, Value.w};
}

struct ResolvedSun
{
    double Elevation = 0.0;
    double Azimuth = 145.0; // VehicleHMI convention: counterclockwise from +X east
    SpatialVector Direction{1.0, 0.0, 0.0};
};

ResolvedSun ResolveSun(const SunPlacement& Placement, double HourOverride = -1.0) noexcept
{
    if (!Placement.AutoPosition && HourOverride < 0.0)
    {
        const double Elevation = Placement.ElevationDegrees * DegreesToRadians;
        const double Azimuth = Placement.AzimuthDegrees * DegreesToRadians;
        const double CosElevation = std::cos(Elevation);
        return {Placement.ElevationDegrees, Placement.AzimuthDegrees,
                {CosElevation * std::cos(Azimuth), CosElevation * std::sin(Azimuth), std::sin(Elevation)}};
    }

    const double LocalHour = HourOverride >= 0.0 ? HourOverride : Placement.LocalSolarTime;
    const double Julian = JulianDate(Placement.SkyYear, static_cast<unsigned>(Placement.SkyMonth),
                                     static_cast<unsigned>(Placement.SkyDay), LocalHour - Placement.TimeZoneHours);
    const SolarPosition Position = SunPosition(Julian, Placement.LatitudeDegrees, Placement.LongitudeDegrees);
    const double NativeAzimuth = Position.AzimuthRad * RadiansToDegrees;
    double HmiAzimuth = std::fmod(90.0 - NativeAzimuth, 360.0);
    if (HmiAzimuth > 180.0) HmiAzimuth -= 360.0;
    if (HmiAzimuth < -180.0) HmiAzimuth += 360.0;
    return {Position.ElevationRad * RadiansToDegrees, HmiAzimuth, SunDirectionLocal(Position)};
}

double Clamp(double Value, double Minimum, double Maximum) noexcept
{
    return std::clamp(Value, Minimum, Maximum);
}

double Snap(double Value, double Minimum, double Maximum, double Step) noexcept
{
    Value = Clamp(Value, Minimum, Maximum);
    if (Step > 0.0)
        Value = Minimum + std::round((Value - Minimum) / Step) * Step;
    return Clamp(Value, Minimum, Maximum);
}

std::string Number(double Value, int Digits = 1)
{
    char Buffer[48] = {};
    std::snprintf(Buffer, sizeof(Buffer), "%.*f", std::clamp(Digits, 0, 5), Value);
    return Buffer;
}

std::string SolarClock(double Hour)
{
    const int Minutes = (static_cast<int>(std::llround(Hour * 60.0)) % 1440 + 1440) % 1440;
    char Buffer[8] = {};
    std::snprintf(Buffer, sizeof(Buffer), "%02d:%02d", Minutes / 60, Minutes % 60);
    return Buffer;
}

void SetDirectionManual(SunPlacement& Placement) noexcept
{
    Placement.AutoPosition = false;
    Placement.DayAnimate = false;
}

void SetTimedField(SunPlacement& Placement) noexcept
{
    Placement.AutoPosition = true;
}

void AdvanceSunPlacement(SunPlacement& Placement, double DeltaSeconds) noexcept
{
    if (Placement.DayAnimate)
    {
        Placement.LocalSolarTime = std::fmod(Placement.LocalSolarTime +
            std::max(0.0, DeltaSeconds) * 24.0 / (std::max(0.01, Placement.DayDurationHours) * 3600.0), 24.0);
        if (Placement.LocalSolarTime < 0.0) Placement.LocalSolarTime += 24.0;
        Placement.AutoPosition = true;
    }
    if (Placement.AutoPosition)
    {
        const ResolvedSun Solved = ResolveSun(Placement);
        Placement.ElevationDegrees = Solved.Elevation;
        Placement.AzimuthDegrees = Solved.Azimuth;
    }
}

ImFont* Font() noexcept
{
    return ImGui::GetFont();
}

float TextWidth(float Size, const char* Text) noexcept
{
    return Font()->CalcTextSizeA(Size, FLT_MAX, 0.0f, Text != nullptr ? Text : "").x;
}

void Text(ImDrawList* Ink, float X, float Y, const char* Value, float Size, ImU32 Colour,
          float Align = 0.0f) noexcept
{
    if (Value == nullptr || Value[0] == '\0') return;
    const float Width = TextWidth(Size, Value);
    if (Align == 0.5f) X -= Width * 0.5f;
    else if (Align > 0.5f) X -= Width;
    Ink->AddText(Font(), Size, ImVec2(X, Y), Colour, Value);
}

std::vector<std::string> WrapText(const char* Value, float Width, float Size)
{
    std::vector<std::string> Lines;
    if (Value == nullptr || Value[0] == '\0') return Lines;
    Width = std::max(1.0f, Width);
    std::string Line;
    std::string Word;
    const auto CommitWord = [&] {
        if (Word.empty()) return;
        const std::string Candidate = Line.empty() ? Word : Line + " " + Word;
        if (!Line.empty() && TextWidth(Size, Candidate.c_str()) > Width)
        {
            Lines.push_back(std::move(Line));
            Line = Word;
        }
        else
            Line = Candidate;
        Word.clear();
    };
    for (const char* Cursor = Value; *Cursor != '\0'; ++Cursor)
    {
        if (*Cursor == ' ' || *Cursor == '\n')
        {
            CommitWord();
            if (*Cursor == '\n' && !Line.empty())
            {
                Lines.push_back(std::move(Line));
                Line.clear();
            }
        }
        else
            Word.push_back(*Cursor);
    }
    CommitWord();
    if (!Line.empty()) Lines.push_back(std::move(Line));
    return Lines;
}

float WrappedHeight(const char* Value, float Width, float Size) noexcept
{
    const std::vector<std::string> Lines = WrapText(Value, Width, Size);
    return static_cast<float>(Lines.size()) * Size * 1.6f;
}

float Caption(ImDrawList* Ink, ImVec2 At, float Width, const char* Value)
{
    constexpr float Size = 10.0f;
    constexpr float LineHeight = Size * 1.6f;
    const ImU32 Colour = Hex(0xa3977e);
    const std::vector<std::string> Lines = WrapText(Value, Width, Size);
    for (size_t Index = 0; Index < Lines.size(); ++Index)
        Ink->AddText(Font(), Size, ImVec2(At.x, At.y + static_cast<float>(Index) * LineHeight), Colour,
                     Lines[Index].c_str());
    return static_cast<float>(Lines.size()) * LineHeight;
}

void DashedLine(ImDrawList* Ink, ImVec2 A, ImVec2 B, ImU32 Colour, float Width,
                float On, float Off) noexcept
{
    const float DX = B.x - A.x;
    const float DY = B.y - A.y;
    const float Length = std::hypot(DX, DY);
    if (Length <= 0.01f) return;
    float Position = 0.0f;
    while (Position < Length)
    {
        const float End = std::min(Length, Position + On);
        Ink->AddLine(ImVec2(A.x + DX * Position / Length, A.y + DY * Position / Length),
                     ImVec2(A.x + DX * End / Length, A.y + DY * End / Length), Colour, Width);
        Position = End + Off;
    }
}

void Glow(ImDrawList* Ink, ImVec2 Centre, float Radius, unsigned Red, unsigned Green, unsigned Blue,
          float Opacity) noexcept
{
    if (Radius <= 0.0f || Opacity <= 0.0f) return;
    for (int Ring = 24; Ring >= 1; --Ring)
    {
        const float Fraction = static_cast<float>(Ring) / 24.0f;
        const float Fade = 1.0f - Fraction;
        const int A = static_cast<int>(Opacity * Fade * 255.0f + 0.5f);
        Ink->AddCircleFilled(Centre, Radius * Fraction, RGB(Red, Green, Blue, static_cast<unsigned>(A)), 48);
    }
}

void Polyline(ImDrawList* Ink, const std::vector<ImVec2>& Points, ImU32 Colour, float Width) noexcept
{
    if (Points.size() >= 2)
        Ink->AddPolyline(Points.data(), static_cast<int>(Points.size()), Colour, ImDrawFlags_None, Width);
}

struct Card
{
    ImVec2 Position{};
    float Width = 0.0f;
    float Height = 0.0f;
    float Padding = 18.0f;
    bool HasId = true;
};

enum class CardStyle
{
    Standard,
    Direction,
    Aim,
    Ephemeris,
    Coordinates,
    Radiance,
    Spectrum,
    Disc,
    Subcomponent
};

Card BeginCard(const char* Id, const char* Title, const char* Index, float Height, float AvailableWidth,
               CardStyle Style, ImDrawList* Ink)
{
    const ImVec2 Start = ImGui::GetCursorScreenPos();
    const float Width = std::max(120.0f, AvailableWidth);
    Card Result{Start, Width, Height, Style == CardStyle::Radiance ? 0.0f : 18.0f, true};
    ImGui::PushID(Id);
    ImGui::Dummy(ImVec2(Width, Height));

    const ImVec2 Maximum(Start.x + Width, Start.y + Height);
    switch (Style)
    {
        case CardStyle::Direction:
            Ink->AddRectFilled(Start, Maximum, Hex(0x23241e), 24.0f);
            Glow(Ink, ImVec2(Start.x + Width * 0.55f, Start.y + Height * 0.60f), Width * 0.38f, 106, 96, 51, 0.156f);
            break;
        case CardStyle::Ephemeris:
            Ink->AddRectFilled(Start, Maximum, Hex(0x22241f), 24.0f);
            break;
        case CardStyle::Coordinates:
            Ink->AddRectFilled(Start, Maximum, Hex(0x20221e), 24.0f);
            break;
        case CardStyle::Radiance:
        case CardStyle::Aim:
            break;
        case CardStyle::Spectrum:
            Ink->AddRectFilled(Start, Maximum, Hex(0x25251f), 23.0f);
            Glow(Ink, ImVec2(Start.x + Width * 0.5f, Start.y + Height * 0.8f), Width * 0.35f, 137, 113, 73, 0.10f);
            break;
        case CardStyle::Disc:
            Ink->AddRectFilled(Start, Maximum, Hex(0x24251f), 24.0f);
            Glow(Ink, ImVec2(Start.x + Width * 0.5f, Start.y + Height * 0.45f), Width * 0.30f, 134, 119, 67, 0.12f);
            break;
        case CardStyle::Subcomponent:
            Ink->AddRectFilled(Start, Maximum, Hex(0x191917, 0), 18.0f);
            Ink->AddRect(Start, Maximum, Hex(0xffffff, 21), 18.0f);
            break;
        case CardStyle::Standard:
        default:
            Ink->AddRectFilled(Start, Maximum, Hex(0x25241f), 23.0f);
            Ink->AddRect(Start, Maximum, Hex(0xffffff, 10), 23.0f);
            break;
    }

    if (Style == CardStyle::Aim)
    {
        Ink->AddLine(ImVec2(Start.x, Start.y), ImVec2(Start.x + Width, Start.y), Hex(0xffffff, 19), 1.0f);
    }

    const float HeadingY = Start.y + (Style == CardStyle::Direction ? 17.0f : Result.Padding);
    Text(Ink, Start.x + Result.Padding, HeadingY, Title, 12.0f, Hex(0xe0e3d8));
    if (Index != nullptr)
    {
        Text(Ink, Start.x + Width - Result.Padding, HeadingY + 2.0f, Index, 8.0f, Hex(0xa0967d), 1.0f);
    }
    return Result;
}

void EndCard(const Card& Value)
{
    ImGui::SetCursorScreenPos(ImVec2(Value.Position.x, Value.Position.y + Value.Height + 14.0f));
    if (Value.HasId) ImGui::PopID();
}

void Readout(ImDrawList* Ink, float X, float Y, float Width, const char* Label,
             const char* Value, const char* Unit, float ValueSize = 31.0f,
             bool Compact = false, ImU32 LabelInk = 0, ImU32 ValueInk = 0, ImU32 UnitInk = 0,
             float UnitSize = 0.0f, bool UnitBelow = false)
{
    const ImU32 LabelColour = LabelInk != 0 ? LabelInk : Hex(0xad9f83);
    const ImU32 ValueColour = ValueInk != 0 ? ValueInk : Hex(0xe8e7df);
    const ImU32 UnitColour = UnitInk != 0 ? UnitInk : Hex(0xa09478);
    const float LabelSize = Compact ? 8.0f : 9.0f;
    const float ValueY = Y + LabelSize * 1.4f + 5.0f;
    Text(Ink, X, Y, Label, LabelSize, LabelColour);
    Text(Ink, X, ValueY, Value, ValueSize, ValueColour);
    if (Unit != nullptr && Unit[0] != '\0')
    {
        const float ResolvedUnitSize = UnitSize > 0.0f ? UnitSize : (Compact ? 8.0f : 10.0f);
        const bool PlaceBelow = UnitBelow || TextWidth(ValueSize, Value) + TextWidth(ResolvedUnitSize, Unit) + 4.0f > Width;
        const float UnitX = PlaceBelow ? X : X + TextWidth(ValueSize, Value) + 4.0f;
        const float UnitY = PlaceBelow ? ValueY + ValueSize * 1.25f
                                       : ValueY + (ValueSize - ResolvedUnitSize) * 0.75f;
        Text(Ink, UnitX, UnitY, Unit, ResolvedUnitSize, UnitColour);
    }
}

bool DrawButton(ImDrawList* Ink, const char* Id, const char* Label, ImVec2 Position, ImVec2 Size,
                ImU32 Fill, ImU32 Border, ImU32 TextColour, float Radius, float FontSize,
                bool Disabled = false, ImU32 HoverFill = 0, ImU32 HoverBorder = 0)
{
    ImGui::SetCursorScreenPos(Position);
    if (Disabled) ImGui::BeginDisabled();
    const bool Pressed = ImGui::InvisibleButton(Id, Size);
    const bool Hovered = ImGui::IsItemHovered();
    if (Disabled) ImGui::EndDisabled();

    const ImU32 Surface = Hovered && HoverFill != 0 ? HoverFill : Fill;
    Ink->AddRectFilled(Position, ImVec2(Position.x + Size.x, Position.y + Size.y), Surface, Radius);
    const ImU32 Outline = Hovered && HoverBorder != 0 ? HoverBorder : Border;
    if ((Outline & IM_COL32_A_MASK) != 0)
        Ink->AddRect(Position, ImVec2(Position.x + Size.x, Position.y + Size.y), Outline, Radius);
    Text(Ink, Position.x + Size.x * 0.5f,
         Position.y + (Size.y - FontSize) * 0.5f, Label, FontSize, Disabled ? Hex(0x77766f) : TextColour, 0.5f);
    return Pressed && !Disabled;
}

void DrawTag(ImDrawList* Ink, const char* Label, ImVec2 Position, ImVec2 Size)
{
    Ink->AddRectFilled(Position, ImVec2(Position.x + Size.x, Position.y + Size.y), Hex(0xe8c78b, 12), Size.y * 0.5f);
    Ink->AddRect(Position, ImVec2(Position.x + Size.x, Position.y + Size.y), Hex(0xe8c78b, 40), Size.y * 0.5f);
    Text(Ink, Position.x + Size.x * 0.5f, Position.y + (Size.y - 9.0f) * 0.5f, Label, 9.0f, Hex(0xe8c78b), 0.5f);
}

float DrawSlider(PixelSpace& Surface, bool SurfaceReady, ImDrawList* Ink, const char* Id,
                 const char* Label, double& Value, double Minimum, double Maximum, double DisplayStep,
                 const char* Unit, int Digits, double DisplayScale, float X, float Y, float Width,
                 const auto& OnChange)
{
    // Match Texture Studio's MaterialLibraryPanel::ConstructProperty: 25px value pill,
    // track at +42px, and the shared ControlKit slider's 24px rail / 22px thumb.
    constexpr float PillHeight = 25.0f;
    constexpr float NumberWidth = 42.0f;
    constexpr float TrackOffset = 42.0f;
    constexpr float TrackHeight = 24.0f;
    constexpr float TotalHeight = 71.0f;
    constexpr float CaptionSize = 10.0f;
    constexpr float ValueSize = 10.0f * 1.302f;
    constexpr float UnitSize = 7.0f * 1.302f;
    (void)Digits; // MaterialLibraryPanel uses %.6g for fractional fields and %.0f for integral fields.

    const bool Percentage = Unit != nullptr && std::strcmp(Unit, "%") == 0;
    const bool HasUnit = Unit != nullptr && Unit[0] != '\0' && std::strcmp(Unit, "-") != 0;
    const double Multiplier = Percentage && std::abs(DisplayScale - 1.0) < 1e-12
                                  ? 100.0
                                  : (std::abs(DisplayScale) > 1e-12 ? DisplayScale : 1.0);
    const float UnitWidth = HasUnit ? Font()->CalcTextSizeA(UnitSize, FLT_MAX, 0.0f, Unit).x : 0.0f;
    const float PillWidth = std::min(Width, std::max(63.0f, 12.0f + NumberWidth +
                                                        (HasUnit ? 5.0f + UnitWidth : 0.0f)));
    const ImVec2 PillMinimum(X + Width - PillWidth, Y);
    const ImVec2 PillMaximum(PillMinimum.x + PillWidth, Y + PillHeight);
    const float InputWidth = std::min(NumberWidth, std::max(1.0f, PillWidth - 12.0f -
                                                              (HasUnit ? 5.0f + UnitWidth : 0.0f)));

    Text(Ink, X, Y + 7.0f, Label, CaptionSize, Hex(0xa7acb5));
    Ink->AddRectFilled(PillMinimum, PillMaximum, Hex(0x090909), 12.5f);
    Ink->AddRect(PillMinimum, PillMaximum, Hex(0xffffff, 13), 12.5f);

    const double BoundedMultiplier = std::abs(Multiplier) > 1e-12 ? Multiplier : 1.0;
    const double DisplayIncrement = DisplayStep * std::abs(BoundedMultiplier);
    double DisplayValue = Value * BoundedMultiplier;
    char Format[16] = {};
    std::snprintf(Format, sizeof(Format), "%s", (Percentage || DisplayIncrement >= 1.0) ? "%.0f" : "%.6g");
    ImGui::PushID(Id);
    ImGui::SetCursorScreenPos(ImVec2(PillMinimum.x + 6.0f, Y));
    ImGui::SetNextItemWidth(InputWidth);
    ImGui::PushFont(Font(), ValueSize);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 6.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Hex(0xcbd3dc)));
    const bool InputChanged = ImGui::InputDouble("##Position", &DisplayValue, 0.0, 0.0, Format);
    const bool ValueFocused = ImGui::IsItemFocused();
    if (InputChanged && std::isfinite(DisplayValue))
    {
        const double Next = Snap(DisplayValue / BoundedMultiplier, Minimum, Maximum, DisplayStep);
        if (std::abs(Next - Value) > 1e-10)
        {
            Value = Next;
            OnChange(Value);
        }
    }
    else if (ValueFocused)
    {
        const int Direction = static_cast<int>(ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) -
                              static_cast<int>(ImGui::IsKeyPressed(ImGuiKey_DownArrow, true));
        if (Direction != 0)
        {
            const double Step = DisplayStep > 0.0 ? DisplayStep : 0.0;
            const double Next = Snap(Value + Direction * Step, Minimum, Maximum, DisplayStep);
            if (std::abs(Next - Value) > 1e-10)
            {
                Value = Next;
                OnChange(Value);
            }
        }
    }
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();
    ImGui::PopFont();

    if (HasUnit)
        Ink->AddText(Font(), UnitSize, ImVec2(PillMaximum.x - 6.0f - UnitWidth, Y + 8.0f),
                     Hex(0x7f8f9a), Unit);

    const float TrackY = Y + TrackOffset;
    ImGui::SetCursorScreenPos(ImVec2(X, TrackY));
    ImGui::InvisibleButton("##slider-track", ImVec2(Width, TrackHeight), ImGuiButtonFlags_EnableNav);
    const bool Hovered = ImGui::IsItemHovered();
    const bool Active = ImGui::IsItemActive();
    const bool TrackFocused = ImGui::IsItemFocused();
    if (SurfaceReady)
    {
        const ControlKitPalette Original = ControlKit::Palette();
        ControlKitPalette Palette = Original;
        Palette.SliderTrack = Quad(Hex(0x171717));
        Palette.SliderFill = Quad(Hex(0x454545));
        Palette.SliderThumb = Quad(Hex(0xd7d7ce));
        ControlKit::ConfigurePalette(Palette);

        const ImGuiIO& Io = ImGui::GetIO();
        ControlPointer Pointer;
        Pointer.X = Io.MousePos.x;
        Pointer.Y = Io.MousePos.y;
        Pointer.Down = Active && ImGui::IsMouseDown(ImGuiMouseButton_Left);
        Pointer.Pressed = ImGui::IsItemActivated();
        Pointer.Released = ImGui::IsItemDeactivated();
        Pointer.Enabled = Hovered || Active;

        const double SliderPosition = std::clamp(Value, Minimum, Maximum);
        float Replacement = static_cast<float>(SliderPosition);
        const ControlHit Hit = ControlKit::Slider(
            Surface, Spanning(X, TrackY, Width, TrackHeight), static_cast<float>(Minimum),
            static_cast<float>(Maximum), static_cast<float>(SliderPosition), Active, Pointer, Replacement,
            ImGui::GetStyle().Alpha, TrackHeight, 22.0f);
        ControlKit::ConfigurePalette(Original);
        if (Hit.Dragging && Pointer.Down)
        {
            const double Next = Snap(static_cast<double>(Replacement), Minimum, Maximum, DisplayStep);
            if (std::abs(Next - Value) > 1e-10)
            {
                Value = Next;
                OnChange(Value);
            }
        }
    }
    if (TrackFocused && !Active)
    {
        const int Direction = static_cast<int>(ImGui::IsKeyPressed(ImGuiKey_RightArrow, true) ||
                                               ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) -
                              static_cast<int>(ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true) ||
                                               ImGui::IsKeyPressed(ImGuiKey_DownArrow, true));
        if (Direction != 0)
        {
            const double Step = DisplayStep > 0.0 ? DisplayStep : 0.0;
            const double Next = Snap(Value + Direction * Step, Minimum, Maximum, DisplayStep);
            if (std::abs(Next - Value) > 1e-10)
            {
                Value = Next;
                OnChange(Value);
            }
        }
    }

    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(X, Y + TotalHeight));
    return TotalHeight;
}

struct SolarDaySummary
{
    std::array<double, 97> Elevations{};
    double Rise = 0.0;
    double Set = 0.0;
    double DayHours = 0.0;
    bool HasRise = false;
    bool HasSet = false;
};

SolarDaySummary ComputeSolarDay(const SunPlacement& Placement) noexcept
{
    SolarDaySummary Result;
    for (size_t Index = 0; Index < Result.Elevations.size(); ++Index)
    {
        const ResolvedSun Current = ResolveSun(Placement, static_cast<double>(Index) / 4.0);
        Result.Elevations[Index] = Current.Elevation;
    }
    for (size_t Index = 1; Index < Result.Elevations.size(); ++Index)
    {
        const double A = Result.Elevations[Index - 1];
        const double B = Result.Elevations[Index];
        constexpr double Step = 0.25;
        if (A >= 0.0 && B >= 0.0)
            Result.DayHours += Step;
        else if ((A < 0.0) != (B < 0.0))
        {
            const double Fraction = -A / (B - A);
            const double Hour = (static_cast<double>(Index - 1) / 4.0) + Fraction * Step;
            if (A < 0.0)
            {
                if (!Result.HasRise) { Result.Rise = Hour; Result.HasRise = true; }
                Result.DayHours += (1.0 - Fraction) * Step;
            }
            else
            {
                if (!Result.HasSet) { Result.Set = Hour; Result.HasSet = true; }
                Result.DayHours += Fraction * Step;
            }
        }
    }
    return Result;
}

double SolarSpectrumValue(double WavelengthNanometres, double Temperature) noexcept
{
    const double Wavelength = WavelengthNanometres * 1e-9;
    return 1.0 / (std::pow(Wavelength, 5.0) * std::expm1(PlanckC2 / (Wavelength * Temperature)));
}

ImVec2 DiagramPoint(ImVec2 Origin, float Scale, float X, float Y) noexcept
{
    return ImVec2(Origin.x + X * Scale, Origin.y + Y * Scale);
}

void DiagramLine(ImDrawList* Ink, ImVec2 Origin, float Scale, ImVec2 A, ImVec2 B,
                 ImU32 Colour, float Width = 1.0f)
{
    Ink->AddLine(DiagramPoint(Origin, Scale, A.x, A.y), DiagramPoint(Origin, Scale, B.x, B.y), Colour, Width * Scale);
}

void DiagramText(ImDrawList* Ink, ImVec2 Origin, float Scale, float X, float Y, const char* Value,
                 ImU32 Colour, float Size, float Align = 0.5f)
{
    Text(Ink, Origin.x + X * Scale, Origin.y + (Y - Size) * Scale, Value, Size * Scale, Colour, Align);
}

void DrawVectorDiagram(ImDrawList* Ink, ImVec2 Origin, float Width, const SunPlacement& Placement)
{
    constexpr float BaseWidth = 640.0f;
    const float Scale = Width / BaseWidth;
    const ImVec2 O = DiagramPoint(Origin, Scale, 320.0f, 210.0f);
    for (const float Radius : {45.0f, 90.0f, 135.0f, 180.0f})
        Ink->AddCircle(O, Radius * Scale, Radius == 90.0f ? Hex(0xd9c29b, 112) : Hex(0xd9c29b, 28), 64, Scale);
    for (int Index = 0; Index < 24; ++Index)
    {
        const double Angle = static_cast<double>(Index) / 24.0 * 2.0 * Pi;
        DiagramLine(Ink, Origin, Scale,
            {320.0f + static_cast<float>(std::cos(Angle)) * 174.0f, 210.0f + static_cast<float>(std::sin(Angle)) * 174.0f},
            {320.0f + static_cast<float>(std::cos(Angle)) * 180.0f, 210.0f + static_cast<float>(std::sin(Angle)) * 180.0f},
            Hex(0xe8c78b, 85), 1.0f);
    }
    DashedLine(Ink, DiagramPoint(Origin, Scale, 120.0f, 210.0f), DiagramPoint(Origin, Scale, 520.0f, 210.0f), Hex(0xe8c78b, 32), Scale, 2.0f * Scale, 6.0f * Scale);
    DashedLine(Ink, DiagramPoint(Origin, Scale, 320.0f, 15.0f), DiagramPoint(Origin, Scale, 320.0f, 405.0f), Hex(0xe8c78b, 32), Scale, 2.0f * Scale, 6.0f * Scale);

    const double Azimuth = Placement.AzimuthDegrees * DegreesToRadians;
    const float Radius = static_cast<float>(90.0 - Placement.ElevationDegrees);
    const ImVec2 Q = DiagramPoint(Origin, Scale,
        320.0f + static_cast<float>(std::cos(Azimuth)) * Radius,
        210.0f - static_cast<float>(std::sin(Azimuth)) * Radius);
    const bool Lit = Placement.Enabled && Placement.ElevationDegrees >= 0.0;
    Ink->AddLine(O, Q, Lit ? Hex(0xe8c78b) : Hex(0x87938b), 1.5f * Scale);
    Ink->AddCircleFilled(O, 4.0f * Scale, Hex(0xd9cab0), 24);
    Ink->AddCircleFilled(Q, 20.0f * Scale, Lit ? Hex(0xe8c78b, 22) : Hex(0x87938b, 22), 48);
    Ink->AddCircle(Q, 20.0f * Scale, Lit ? Hex(0xe8c78b, 112) : Hex(0x87938b, 112), 48, Scale);
    if (Lit) Glow(Ink, Q, 28.0f * Scale, 232, 199, 139, 0.2f);
    Ink->AddCircleFilled(Q, 6.0f * Scale, Lit ? Hex(0xf0d69f) : Hex(0x8d9b91), 32);
    DiagramText(Ink, Origin, Scale, 320.0f, 18.0f, "+Y / N", Hex(0xa49886), 16.0f);
    DiagramText(Ink, Origin, Scale, 532.0f, 216.0f, "+X / E", Hex(0xa49886), 16.0f, 0.0f);
    DiagramText(Ink, Origin, Scale, 108.0f, 216.0f, "W", Hex(0xa49886), 16.0f, 1.0f);
    DiagramText(Ink, Origin, Scale, 320.0f, 408.0f, "S", Hex(0xa49886), 16.0f);
    DiagramText(Ink, Origin, Scale, 419.0f, 194.0f, "0° HORIZON", Hex(0xab9d88), 13.0f, 0.0f);
    DiagramText(Ink, Origin, Scale, 24.0f, 389.0f, "DRAG VECTOR", Hex(0xab9d88), 14.0f, 0.0f);
}

void DrawDaylightDiagram(ImDrawList* Ink, ImVec2 Origin, float Width, const SunPlacement& Placement,
                         const SolarDaySummary& Data)
{
    constexpr float BaseWidth = 640.0f;
    const float Scale = Width / BaseWidth;
    const auto Y = [](double Elevation) { return 160.0f - static_cast<float>(Elevation / 90.0) * 110.0f; };
    for (const double Elevation : {-60.0, 0.0, 60.0})
    {
        const float YValue = Y(Elevation);
        const ImU32 Colour = Elevation == 0.0 ? Hex(0xe3ca99, 92) : Hex(0xe3ca99, 26);
        DashedLine(Ink, DiagramPoint(Origin, Scale, 42.0f, YValue), DiagramPoint(Origin, Scale, 606.0f, YValue), Colour,
                   Scale, 3.0f * Scale, 6.0f * Scale);
        char Label[16] = {};
        std::snprintf(Label, sizeof(Label), "%.0f°", Elevation);
        DiagramText(Ink, Origin, Scale, 32.0f, YValue + 5.0f, Label, Hex(0x9c947f), 15.0f, 1.0f);
    }

    std::vector<ImVec2> Points;
    Points.reserve(Data.Elevations.size());
    for (size_t Index = 0; Index < Data.Elevations.size(); ++Index)
    {
        const float Hour = static_cast<float>(Index) / 4.0f;
        Points.push_back(DiagramPoint(Origin, Scale, 42.0f + Hour / 24.0f * 564.0f, Y(Data.Elevations[Index])));
    }
    Polyline(Ink, Points, Hex(0x9f997a, 153), 1.6f * Scale);
    std::vector<ImVec2> Segment;
    for (size_t Index = 0; Index < Data.Elevations.size(); ++Index)
    {
        if (Data.Elevations[Index] >= 0.0)
            Segment.push_back(Points[Index]);
        else
        {
            if (Segment.size() > 1) Polyline(Ink, Segment, Hex(0xefd397), 2.6f * Scale);
            Segment.clear();
        }
    }
    if (Segment.size() > 1) Polyline(Ink, Segment, Hex(0xefd397), 2.6f * Scale);

    const float X = 42.0f + static_cast<float>(Placement.LocalSolarTime / 24.0) * 564.0f;
    DashedLine(Ink, DiagramPoint(Origin, Scale, X, 30.0f), DiagramPoint(Origin, Scale, X, 280.0f), Hex(0xe8c78b, 102), Scale,
               3.0f * Scale, 5.0f * Scale);
    if (Placement.AutoPosition)
    {
        const ResolvedSun Current = ResolveSun(Placement);
        Ink->AddCircleFilled(DiagramPoint(Origin, Scale, X, Y(Current.Elevation)), 6.0f * Scale, Hex(0xf1d69e), 32);
    }
    for (const int Hour : {0, 6, 12, 18, 24})
    {
        char Label[8] = {};
        std::snprintf(Label, sizeof(Label), "%02d:00", Hour);
        DiagramText(Ink, Origin, Scale, 42.0f + static_cast<float>(Hour) / 24.0f * 564.0f, 304.0f,
                    Label, Hex(0xa39982), 15.0f);
    }
}

void FillQuadGradient(ImDrawList* Ink, const ImVec2 Points[4], const ImU32 Colours[4])
{
    const ImVec2 Uv = ImGui::GetFontTexUvWhitePixel();
    Ink->PrimReserve(6, 4);
    const unsigned Base = Ink->_VtxCurrentIdx;
    for (const unsigned Index : {0u, 1u, 2u, 0u, 2u, 3u}) Ink->PrimWriteIdx(static_cast<ImDrawIdx>(Base + Index));
    for (unsigned Index = 0; Index < 4; ++Index) Ink->PrimWriteVtx(Points[Index], Uv, Colours[Index]);
}

void DrawSpectrumDiagram(ImDrawList* Ink, ImVec2 Origin, float Width, double Temperature)
{
    constexpr float BaseWidth = 640.0f;
    const float Scale = Width / BaseWidth;
    for (const float Y : {28.0f, 80.0f, 132.0f, 184.0f})
        DashedLine(Ink, DiagramPoint(Origin, Scale, 32.0f, Y), DiagramPoint(Origin, Scale, 608.0f, Y), Hex(0xead6ad, 26),
                   Scale, 2.0f * Scale, 6.0f * Scale);

    constexpr int SampleCount = 65;
    std::array<double, SampleCount> Values{};
    double Peak = 0.0;
    for (int Index = 0; Index < SampleCount; ++Index)
    {
        const double Wavelength = 380.0 + static_cast<double>(Index) / 64.0 * 400.0;
        Values[static_cast<size_t>(Index)] = SolarSpectrumValue(Wavelength, Temperature);
        Peak = std::max(Peak, Values[static_cast<size_t>(Index)]);
    }
    std::array<ImVec2, SampleCount> Points{};
    for (int Index = 0; Index < SampleCount; ++Index)
    {
        const double Wavelength = 380.0 + static_cast<double>(Index) / 64.0 * 400.0;
        const float X = 32.0f + static_cast<float>((Wavelength - 380.0) / 400.0) * 576.0f;
        const float Y = 204.0f - static_cast<float>(Values[static_cast<size_t>(Index)] / Peak) * 165.0f;
        Points[static_cast<size_t>(Index)] = DiagramPoint(Origin, Scale, X, Y);
    }

    constexpr std::array<ImU32, 5> Stops = {RGB(143, 129, 181), RGB(130, 167, 213), RGB(145, 189, 172),
                                            RGB(214, 199, 126), RGB(202, 138, 118)};
    for (int Index = 1; Index < SampleCount; ++Index)
    {
        const float F0 = static_cast<float>(Index - 1) / 64.0f;
        const float F1 = static_cast<float>(Index) / 64.0f;
        const auto ColourAt = [&](float F) {
            const float Scaled = F * 4.0f;
            const size_t Segment = std::min(static_cast<size_t>(Scaled), Stops.size() - 2);
            const float T = Scaled - static_cast<float>(Segment);
            const ImVec4 A = ImGui::ColorConvertU32ToFloat4(Stops[Segment]);
            const ImVec4 B = ImGui::ColorConvertU32ToFloat4(Stops[Segment + 1]);
            return RGB(static_cast<unsigned>((A.x + (B.x - A.x) * T) * 255.0f),
                       static_cast<unsigned>((A.y + (B.y - A.y) * T) * 255.0f),
                       static_cast<unsigned>((A.z + (B.z - A.z) * T) * 255.0f), 56u);
        };
        const ImVec2 P0 = Points[static_cast<size_t>(Index - 1)];
        const ImVec2 P1 = Points[static_cast<size_t>(Index)];
        const ImVec2 B0 = DiagramPoint(Origin, Scale, 32.0f + F0 * 576.0f, 204.0f);
        const ImVec2 B1 = DiagramPoint(Origin, Scale, 32.0f + F1 * 576.0f, 204.0f);
        const ImVec2 QuadPoints[4] = {P0, P1, B1, B0};
        const ImU32 QuadColours[4] = {ColourAt(F0), ColourAt(F1), RGB(0, 0, 0, 0), RGB(0, 0, 0, 0)};
        FillQuadGradient(Ink, QuadPoints, QuadColours);
    }
    Polyline(Ink, std::vector<ImVec2>(Points.begin(), Points.end()), Hex(0xf0ddba), 2.5f * Scale);

    const double PeakNanometres = 2897771.955 / Temperature;
    if (PeakNanometres >= 380.0 && PeakNanometres <= 780.0)
    {
        const float X = 32.0f + static_cast<float>((PeakNanometres - 380.0) / 400.0) * 576.0f;
        DashedLine(Ink, DiagramPoint(Origin, Scale, X, 28.0f), DiagramPoint(Origin, Scale, X, 204.0f), Hex(0xeed6a6, 112),
                   Scale, 3.0f * Scale, 6.0f * Scale);
        Ink->AddCircleFilled(DiagramPoint(Origin, Scale, X, 39.0f), 5.0f * Scale, Hex(0xf0d49d), 24);
        DiagramText(Ink, Origin, Scale, X, 19.0f, "λ MAX", Hex(0xc3b596), 13.0f);
    }

    for (int Index = 0; Index < 4; ++Index)
    {
        const float X0 = 32.0f + static_cast<float>(Index) / 4.0f * 576.0f;
        const float X1 = 32.0f + static_cast<float>(Index + 1) / 4.0f * 576.0f;
        Ink->AddRectFilledMultiColor(DiagramPoint(Origin, Scale, X0, 218.0f), DiagramPoint(Origin, Scale, X1, 225.0f),
                                     Stops[static_cast<size_t>(Index)], Stops[static_cast<size_t>(Index + 1)],
                                     Stops[static_cast<size_t>(Index + 1)], Stops[static_cast<size_t>(Index)]);
    }
    Ink->AddRect(DiagramPoint(Origin, Scale, 32.0f, 218.0f), DiagramPoint(Origin, Scale, 608.0f, 225.0f),
                 Hex(0xffffff, 18), 3.5f * Scale);
    for (const int Wavelength : {380, 480, 580, 680, 780})
    {
        char Label[8] = {};
        std::snprintf(Label, sizeof(Label), "%d", Wavelength);
        DiagramText(Ink, Origin, Scale, 32.0f + (static_cast<float>(Wavelength) - 380.0f) / 400.0f * 576.0f,
                    252.0f, Label, Hex(0xac9f86), 14.0f);
    }
    DiagramText(Ink, Origin, Scale, 320.0f, 277.0f, "VISIBLE WAVELENGTH / nm", Hex(0x877e6a), 13.0f);
}

void DrawDiscDiagram(ImDrawList* Ink, ImVec2 Origin, float Width, const SunPlacement& Placement)
{
    constexpr float BaseWidth = 640.0f;
    const float Scale = Width / BaseWidth;
    const ImVec2 Centre = DiagramPoint(Origin, Scale, 320.0f, 130.0f);
    for (const float Radius : {45.0f, 85.0f, 118.0f})
        Ink->AddCircle(Centre, Radius * Scale, Hex(0xe8c78b, 34), 64, Scale);
    DashedLine(Ink, DiagramPoint(Origin, Scale, 180.0f, 130.0f), DiagramPoint(Origin, Scale, 460.0f, 130.0f),
               Hex(0xe8c78b, 42), Scale, 2.0f * Scale, 5.0f * Scale);
    DashedLine(Ink, DiagramPoint(Origin, Scale, 320.0f, 5.0f), DiagramPoint(Origin, Scale, 320.0f, 255.0f),
               Hex(0xe8c78b, 42), Scale, 2.0f * Scale, 5.0f * Scale);
    const float Radius = 12.0f + static_cast<float>(Placement.RadiusDegrees / 2.0) * 76.0f;
    Glow(Ink, Centre, (Radius + 42.0f) * Scale, 232, 199, 139, 0.12f * static_cast<float>(Placement.Glow));
    Ink->AddCircleFilled(Centre, Radius * Scale, Placement.DiscEnabled ? Hex(0xf1d7a6) : Hex(0x776e5b), 64);
    DiagramLine(Ink, Origin, Scale, {320.0f - Radius, 130.0f}, {320.0f + Radius, 130.0f}, Hex(0xfff3cf, 102), 1.0f);
    DiagramText(Ink, Origin, Scale, 24.0f, 261.0f, "ANGULAR SIZE / EXAGGERATED", Hex(0xac9e83), 14.0f, 0.0f);
}

template <typename Changed>
void DrawToggleTile(ImDrawList* Ink, const char* Id, const char* Label, const char* Glyph,
                    bool& On, ImVec2 Position, float Width, Changed&& ChangedState)
{
    constexpr float Height = 106.0f;
    ImGui::SetCursorScreenPos(Position);
    const bool Clicked = ImGui::InvisibleButton(Id, ImVec2(Width, Height));
    const bool Hovered = ImGui::IsItemHovered();
    if (Clicked)
    {
        On = !On;
        ChangedState();
    }
    const ImU32 Background = Hex(0x202020);
    const ImU32 Border = On ? Hex(0x323232) : Hex(0x493232);
    Ink->AddRectFilled(Position, ImVec2(Position.x + Width, Position.y + Height),
                        Hovered ? Hex(0x303030) : Background, 16.0f);
    Ink->AddRect(Position, ImVec2(Position.x + Width, Position.y + Height), Hovered ? Hex(0x777777) : Border, 16.0f);
    const ImU32 SymbolBack = On ? Hex(0x69c884) : Hex(0xcc7673);
    const ImU32 SymbolInk = On ? Hex(0x16321e) : Hex(0x371d1c);
    const ImVec2 Centre(Position.x + Width * 0.5f, Position.y + 29.0f);
    Ink->AddCircleFilled(Centre, 15.5f, SymbolBack, 32);
    if (std::strcmp(Glyph, "NativeOrbit") == 0)
    {
        // VehicleHMI EditorIcons.js: 24-unit arc, two orbit points, and a 3-unit centre body.
        constexpr float IconScale = 18.0f / 24.0f;
        const float OrbitRadius = 10.0f * IconScale;
        const float Start = -static_cast<float>(Pi) * 0.25f;
        const float End = Start + static_cast<float>(Pi) * 5.0f / 3.0f;
        Ink->PathArcTo(Centre, OrbitRadius, Start, End, 48);
        Ink->PathStroke(SymbolInk, ImDrawFlags_None, 1.2f);
        const ImVec2 StartPoint(Centre.x + std::cos(Start) * OrbitRadius, Centre.y + std::sin(Start) * OrbitRadius);
        const ImVec2 EndPoint(Centre.x + std::cos(End) * OrbitRadius, Centre.y + std::sin(End) * OrbitRadius);
        Ink->AddCircleFilled(StartPoint, 0.6f, SymbolInk, 12);
        Ink->AddCircleFilled(EndPoint, 0.6f, SymbolInk, 12);
        Ink->AddCircleFilled(ImVec2(Centre.x + 7.0f * IconScale, Centre.y - 7.0f * IconScale),
                             2.0f * IconScale, SymbolInk, 16);
        Ink->AddCircleFilled(ImVec2(Centre.x - 7.0f * IconScale, Centre.y + 7.0f * IconScale),
                             2.0f * IconScale, SymbolInk, 16);
        Ink->AddCircleFilled(Centre, 3.0f * IconScale, SymbolInk, 20);
    }
    else
        DrawVehicleHmiGlyph(Ink, Glyph, Centre, 18.0f, SymbolInk, 1.5f);
    Text(Ink, Centre.x, Position.y + 55.0f, Label, 10.0f, Hex(0xc8c8c8), 0.5f);
    Text(Ink, Centre.x, Position.y + 77.0f, On ? "ON" : "OFF", 8.0f,
         On ? Hex(0x69c884) : Hex(0xcc7673), 0.5f);
}

bool ValidDate(int Year, int Month, int Day) noexcept
{
    if (Year < 1600 || Year > 2600 || Month < 1 || Month > 12) return false;
    constexpr int Days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int Maximum = Days[Month - 1];
    if (Month == 2 && (Year % 400 == 0 || (Year % 4 == 0 && Year % 100 != 0))) Maximum = 29;
    return Day >= 1 && Day <= Maximum;
}

void DrawDateInput(ImDrawList* Ink, const char* Id, const char* Label, int& Year, int& Month, int& Day,
                   float X, float Y, float Width, SunPlacement& Placement)
{
    Text(Ink, X, Y + 7.0f, Label, 10.0f, Hex(0xb8ad96));
    const float FieldWidth = std::min(160.0f, Width * 0.58f);
    const float FieldX = X + Width - FieldWidth;
    Ink->AddRectFilled(ImVec2(FieldX, Y), ImVec2(FieldX + FieldWidth, Y + 28.0f), Hex(0x171610), 9.0f);
    Ink->AddRect(ImVec2(FieldX, Y), ImVec2(FieldX + FieldWidth, Y + 28.0f), Hex(0xe8c78b, 38), 9.0f);

    static char DateBuffer[16] = {};
    if (DateBuffer[0] == '\0') std::snprintf(DateBuffer, sizeof(DateBuffer), "%04d-%02d-%02d", Year, Month, Day);
    ImGui::PushID(Id);
    ImGui::SetCursorScreenPos(ImVec2(FieldX + 7.0f, Y + 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Hex(0xe7d8b9)));
    ImGui::SetNextItemWidth(FieldWidth - 14.0f);
    if (ImGui::InputText("##date", DateBuffer, sizeof(DateBuffer)))
    {
        int NewYear = 0, NewMonth = 0, NewDay = 0;
        if (std::sscanf(DateBuffer, "%d-%d-%d", &NewYear, &NewMonth, &NewDay) == 3 && ValidDate(NewYear, NewMonth, NewDay))
        {
            Year = NewYear;
            Month = NewMonth;
            Day = NewDay;
            Placement.AutoPosition = true;
        }
    }
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();
    ImGui::PopID();
}

void DrawOverview(PixelSpace& Surface, bool SurfaceReady, SunPlacement& Placement, ImDrawList* Ink, float Width)
{
    const float InnerWidth = std::max(1.0f, Width - 36.0f);
    const float VectorChartHeight = InnerWidth * 420.0f / 640.0f;
    const float VectorChartY = 47.0f;
    const float FooterY = VectorChartY + VectorChartHeight;
    const float VectorHeight = FooterY + 25.0f + 18.0f;
    Card VectorCard = BeginCard("sun-vector", "Solar vector", "01 / DIRECTION", VectorHeight, Width, CardStyle::Direction, Ink);
    const ImVec2 ChartOrigin(VectorCard.Position.x + 18.0f, VectorCard.Position.y + VectorChartY);
    DrawVectorDiagram(Ink, ChartOrigin, InnerWidth, Placement);
    ImGui::SetCursorScreenPos(ChartOrigin);
    ImGui::InvisibleButton("##sun-vector-editor", ImVec2(InnerWidth, VectorChartHeight), ImGuiButtonFlags_AllowOverlap);
    const bool VectorActive = ImGui::IsItemActive();
    const bool VectorFocused = ImGui::IsItemFocused();
    if (VectorActive && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        const float Scale = InnerWidth / 640.0f;
        const double DX = (ImGui::GetIO().MousePos.x - ChartOrigin.x) / Scale - 320.0;
        const double DY = 210.0 - (ImGui::GetIO().MousePos.y - ChartOrigin.y) / Scale;
        const double Radius = std::hypot(DX, DY);
        SetDirectionManual(Placement);
        if (Radius > 1.0) Placement.AzimuthDegrees = Clamp(std::atan2(DY, DX) * RadiansToDegrees, -180.0, 180.0);
        Placement.ElevationDegrees = Clamp(90.0 - std::min(180.0, Radius), -90.0, 90.0);
    }
    else if (VectorFocused)
    {
        const double Step = ImGui::GetIO().KeyShift ? 10.0 : 1.0;
        if (ImGui::IsKeyPressed(ImGuiKey_Home))
        {
            SetDirectionManual(Placement);
            Placement.AzimuthDegrees = 145.0;
            Placement.ElevationDegrees = 38.0;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_RightArrow))
        {
            SetDirectionManual(Placement);
            Placement.AzimuthDegrees = Clamp(Placement.AzimuthDegrees +
                (ImGui::IsKeyPressed(ImGuiKey_RightArrow) ? Step : -Step), -180.0, 180.0);
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow))
        {
            SetDirectionManual(Placement);
            Placement.ElevationDegrees = Clamp(Placement.ElevationDegrees +
                (ImGui::IsKeyPressed(ImGuiKey_UpArrow) ? Step : -Step), -90.0, 90.0);
        }
    }

    const std::string Elevation = Number(Placement.ElevationDegrees, 1);
    Readout(Ink, VectorCard.Position.x + 18.0f, VectorCard.Position.y + 49.0f,
            InnerWidth - 100.0f, "Elevation", Elevation.c_str(), "°", 37.0f, false, 0, 0, 0, 17.0f);
    const bool ModeClicked = DrawButton(Ink, "##sun-mode", Placement.AutoPosition ? "Time linked ↗" : "Manual ↗",
               ImVec2(VectorCard.Position.x + VectorCard.Width - 18.0f - 116.0f, VectorCard.Position.y + 49.0f),
               ImVec2(116.0f, 29.0f), Hex(0x303128), Hex(0xe3d2a6, 28), Hex(0xe4dbbd), 20.0f, 9.0f,
               false, Hex(0x303030), Hex(0x777777));
    if (ModeClicked)
    {
        Placement.AutoPosition = !Placement.AutoPosition;
        if (!Placement.AutoPosition) Placement.DayAnimate = false;
    }

    const ImVec2 FooterMin(VectorCard.Position.x + 18.0f, ChartOrigin.y + VectorChartHeight + 11.0f);
    Ink->AddLine(FooterMin, ImVec2(VectorCard.Position.x + VectorCard.Width - 18.0f, FooterMin.y), Hex(0xffffff, 18), 1.0f);
    Text(Ink, FooterMin.x, FooterMin.y + 12.0f, "WORLD XYZ · Z UP", 9.0f, Hex(0xad9f83));
    const double Az = Placement.AzimuthDegrees * DegreesToRadians;
    const double El = Placement.ElevationDegrees * DegreesToRadians;
    char XYZ[64] = {};
    std::snprintf(XYZ, sizeof(XYZ), "%.2f / %.2f / %.2f", std::cos(El) * std::cos(Az),
                  std::cos(El) * std::sin(Az), std::sin(El));
    Text(Ink, VectorCard.Position.x + VectorCard.Width - 18.0f, FooterMin.y + 12.0f, XYZ, 9.0f, Hex(0xad9f83), 1.0f);
    EndCard(VectorCard);

    const ImVec2 MetricsStart = ImGui::GetCursorScreenPos();
    constexpr float MetricsHeight = 84.0f;
    ImGui::Dummy(ImVec2(Width, MetricsHeight));
    const float Gap = 7.0f;
    const float CellWidth = (Width - Gap * 2.0f) / 3.0f;
    const std::string TempSource = Placement.ColourSource == 1 ? "RGB" : Number(Placement.TemperatureKelvin, 0) + " K";
    const bool KpiUnitsBelow = Width < 520.0f;
    for (int Index = 0; Index < 3; ++Index)
    {
        const float X = MetricsStart.x + static_cast<float>(Index) * (CellWidth + Gap);
        const float Y = MetricsStart.y;
        const ImU32 Fill = Index == 2 ? Hex(0xd7dbb8) : Hex(0x242622);
        Ink->AddRectFilled(ImVec2(X, Y), ImVec2(X + CellWidth, Y + MetricsHeight), Fill, 15.0f);
        if (Index == 0)
        {
            const std::string Value = Number(Placement.AzimuthDegrees, 1);
            Readout(Ink, X + 10.0f, Y + 13.0f, CellWidth - 20.0f, "Azimuth", Value.c_str(), "° / +X east",
                    23.0f, true, 0, 0, 0, 8.0f, KpiUnitsBelow);
        }
        else if (Index == 1)
            Readout(Ink, X + 10.0f, Y + 13.0f, CellWidth - 20.0f, "Source tint", TempSource.c_str(), "source setting",
                    23.0f, true, 0, 0, 0, 8.0f, KpiUnitsBelow);
        else
            Readout(Ink, X + 10.0f, Y + 13.0f, CellWidth - 20.0f, "Sunlight", Number(Placement.SolarIlluminance, 1).c_str(),
                    "× multiplier", 23.0f, true, Hex(0x626b51), Hex(0x292e20), Hex(0x626b51), 8.0f, KpiUnitsBelow);
    }
    ImGui::SetCursorScreenPos(ImVec2(MetricsStart.x, MetricsStart.y + MetricsHeight + 16.0f));

    const float TileGap = 9.0f;
    const bool TwoColumns = Width < 370.0f;
    const int Columns = TwoColumns ? 2 : 3;
    const float TileWidth = (Width - TileGap * static_cast<float>(Columns - 1)) / static_cast<float>(Columns);
    const int Rows = (3 + Columns - 1) / Columns;
    const float TilesY = ImGui::GetCursorScreenPos().y;
    const float TilesHeight = static_cast<float>(Rows) * 106.0f + static_cast<float>(Rows - 1) * TileGap;
    ImGui::Dummy(ImVec2(Width, TilesHeight));
    const ImVec2 TilePositions[] = {
        ImVec2(MetricsStart.x, TilesY),
        ImVec2(MetricsStart.x + TileWidth + TileGap, TilesY),
        ImVec2(MetricsStart.x + static_cast<float>(2 % Columns) * (TileWidth + TileGap),
               TilesY + static_cast<float>(2 / Columns) * (106.0f + TileGap))};
    ImGui::PushID("sun-quick");
    DrawToggleTile(Ink, "##sunlight", "Sunlight", "Sun", Placement.Enabled, TilePositions[0], TileWidth, [] {});
    DrawToggleTile(Ink, "##sun-disc", "Sun disc", "Sun", Placement.DiscEnabled, TilePositions[1], TileWidth, [] {});
    DrawToggleTile(Ink, "##day-cycle", "Day cycle", "NativeOrbit", Placement.DayAnimate, TilePositions[2], TileWidth,
                   [&] { if (Placement.DayAnimate) Placement.AutoPosition = true; });
    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(MetricsStart.x, TilesY + TilesHeight + 18.0f));

    const float AimStartY = ImGui::GetCursorScreenPos().y;
    const float CaptionTextHeight = WrappedHeight(
        "Inner horizon ring: 0°. Centre: zenith. Outer region: below horizon. Editing direction stops the day cycle and switches to manual.",
        Width, 10.0f);
    const float AimHeight = 232.0f + CaptionTextHeight;
    Card Aim = BeginCard("sun-aim", "Direction controls", "02 / AIM", AimHeight, Width, CardStyle::Aim, Ink);
    float Y = AimStartY + 48.0f;
    DrawSlider(Surface, SurfaceReady, Ink, "sun-azimuth", "Azimuth", Placement.AzimuthDegrees,
               -180.0, 180.0, 0.1, "°", 1, 1.0, Aim.Position.x, Y, Width,
               [&](double) { SetDirectionManual(Placement); });
    Y += 74.0f;
    DrawSlider(Surface, SurfaceReady, Ink, "sun-elevation", "Elevation", Placement.ElevationDegrees,
               -90.0, 90.0, 0.1, "°", 1, 1.0, Aim.Position.x, Y, Width,
               [&](double) { SetDirectionManual(Placement); });
    Y += 74.0f;
    Caption(Ink, ImVec2(Aim.Position.x, Y + 6.0f), Width,
        "Inner horizon ring: 0°. Centre: zenith. Outer region: below horizon. Editing direction stops the day cycle and switches to manual.");
    EndCard(Aim);

    const ImVec2 PresetsStart = ImGui::GetCursorScreenPos();
    const float PresetsHeight = 34.0f;
    ImGui::Dummy(ImVec2(Width, PresetsHeight));
    const float ButtonGap = 6.0f;
    const float ButtonWidth = (Width - ButtonGap * 3.0f) / 4.0f;
    constexpr const char* Names[] = {"Noon", "Golden", "Dusk", "Night"};
    for (int Index = 0; Index < 4; ++Index)
    {
        const ImVec2 Position(PresetsStart.x + static_cast<float>(Index) * (ButtonWidth + ButtonGap), PresetsStart.y);
        const bool Selected = Placement.LightingPreset == Index + 1;
        ImGui::PushID(800 + Index);
        const bool Clicked = DrawButton(Ink, "##preset", Names[Index], Position, ImVec2(ButtonWidth, PresetsHeight),
            Selected ? Hex(0x4e3e23) : Hex(0x25231c), Selected ? Hex(0xe8c78b, 68) : Hex(0xe8c78b, 18),
            Selected ? Hex(0xf4d6a0) : Hex(0xbbae91), 15.0f, 10.0f, false, Hex(0x303030), Hex(0x777777));
        ImGui::PopID();
        if (Clicked)
        {
            Placement.AutoPosition = false;
            Placement.DayAnimate = false;
            Placement.LightingPreset = Index + 1;
            Placement.EmissionScale = 1.0;
            switch (Index)
            {
                case 0: Placement.ElevationDegrees = 64.0; Placement.AzimuthDegrees = 145.0; Placement.SolarIlluminance = 12.0; break;
                case 1: Placement.ElevationDegrees = 10.0; Placement.AzimuthDegrees = 160.0; Placement.SolarIlluminance = 12.0; break;
                case 2: Placement.ElevationDegrees = 1.5; Placement.AzimuthDegrees = 172.0; Placement.SolarIlluminance = 12.0; break;
                case 3: Placement.ElevationDegrees = -14.0; Placement.AzimuthDegrees = 172.0; Placement.SolarIlluminance = 12.0; break;
            }
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(PresetsStart.x, PresetsStart.y + PresetsHeight + 12.0f));
}

void DrawDaylight(PixelSpace& Surface, bool SurfaceReady, SunPlacement& Placement, ImDrawList* Ink, float Width)
{
    const float InnerWidth = std::max(1.0f, Width - 36.0f);
    const float ChartHeight = InnerWidth * 320.0f / 640.0f;
    const float ChartY = 139.0f;
    const float MetricsY = ChartY + ChartHeight + 12.0f;
    const float CaptionY = MetricsY + 93.0f;
    const char* DayDescription =
        "Date/location solver · geometric 0° crossings, without refraction or terrain. This path is a reference while direction is manual, not recorded telemetry.";
    const float DescriptionHeight = WrappedHeight(DayDescription, InnerWidth, 10.0f);
    const float FirstSliderY = CaptionY + DescriptionHeight + 13.0f;
    const float DayCardHeight = FirstSliderY + 2.0f * 74.0f + 18.0f;
    Card Day = BeginCard("solar-day", "Solar day", "03 / EPHEMERIS", DayCardHeight, Width, CardStyle::Ephemeris, Ink);

    const ImVec2 LocalTimePosition(Day.Position.x + 18.0f, Day.Position.y + 48.0f);
    const std::string LocalTime = SolarClock(Placement.LocalSolarTime);
    Readout(Ink, LocalTimePosition.x, LocalTimePosition.y, InnerWidth - 110.0f, "Local time", LocalTime.c_str(), "", 49.0f);
    const char* Status = Placement.DayAnimate ? "Cycle running" : Placement.AutoPosition ? "Time linked" : "Manual direction";
    const float StatusWidth = TextWidth(9.0f, Status) + 22.0f;
    DrawTag(Ink, Status, ImVec2(Day.Position.x + Day.Width - 18.0f - StatusWidth, Day.Position.y + 74.0f),
            ImVec2(StatusWidth, 26.0f));

    const SolarDaySummary Data = ComputeSolarDay(Placement);
    const ImVec2 ChartOrigin(Day.Position.x + 18.0f, Day.Position.y + ChartY);
    DrawDaylightDiagram(Ink, ChartOrigin, InnerWidth, Placement, Data);
    ImGui::SetCursorScreenPos(ImVec2(ChartOrigin.x, ChartOrigin.y));
    ImGui::InvisibleButton("##solar-day-chart", ImVec2(InnerWidth, ChartHeight));
    const bool ChartActive = ImGui::IsItemActive();
    const bool ChartFocused = ImGui::IsItemFocused();
    if (ChartActive && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        const float Fraction = std::clamp((ImGui::GetIO().MousePos.x - ChartOrigin.x - 42.0f * InnerWidth / 640.0f) /
            (564.0f * InnerWidth / 640.0f), 0.0f, 1.0f);
        Placement.LocalSolarTime = static_cast<double>(Fraction) * 24.0;
        SetTimedField(Placement);
    }
    else if (ChartFocused)
    {
        const double Step = ImGui::GetIO().KeyShift ? 2.5 : 0.25;
        if (ImGui::IsKeyPressed(ImGuiKey_Home)) Placement.LocalSolarTime = 12.0;
        else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow))
            Placement.LocalSolarTime = std::fmod(Placement.LocalSolarTime - Step + 24.0, 24.0);
        else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow))
            Placement.LocalSolarTime = std::fmod(Placement.LocalSolarTime + Step, 24.0);
        SetTimedField(Placement);
    }

    const ImVec2 MetricsPosition(Day.Position.x + 18.0f, Day.Position.y + MetricsY);
    Ink->AddLine(MetricsPosition, ImVec2(MetricsPosition.x + InnerWidth, MetricsPosition.y), Hex(0xe8c78b, 18), 1.0f);
    const float MetricGap = 10.0f;
    const float MetricWidth = (InnerWidth - MetricGap * 2.0f) / 3.0f;
    const std::string Rise = Data.HasRise ? SolarClock(Data.Rise) : "—";
    const std::string Set = Data.HasSet ? SolarClock(Data.Set) : "—";
    const std::string Above = Number(Data.DayHours, 1);
    Readout(Ink, MetricsPosition.x, MetricsPosition.y + 13.0f, MetricWidth, "Horizon rise", Rise.c_str(), "", 22.0f);
    Readout(Ink, MetricsPosition.x + MetricWidth + MetricGap, MetricsPosition.y + 13.0f, MetricWidth, "Horizon set", Set.c_str(), "", 22.0f);
    Readout(Ink, MetricsPosition.x + (MetricWidth + MetricGap) * 2.0f, MetricsPosition.y + 13.0f, MetricWidth,
            "Above horizon", Above.c_str(), "h", 22.0f, false, 0, 0, 0, 9.0f, true);
    Caption(Ink, ImVec2(Day.Position.x + 18.0f, Day.Position.y + CaptionY), InnerWidth, DayDescription);

    DrawSlider(Surface, SurfaceReady, Ink, "sun-local-time", "Local solar control time", Placement.LocalSolarTime,
        0.0, 24.0, 0.01, "h", 2, 1.0, Day.Position.x + 18.0f, Day.Position.y + FirstSliderY, InnerWidth,
        [&](double) { SetTimedField(Placement); });
    DrawSlider(Surface, SurfaceReady, Ink, "sun-day-duration", "Full day duration", Placement.DayDurationHours,
        0.01, 24.0, 0.01, "real h", 2, 1.0, Day.Position.x + 18.0f, Day.Position.y + FirstSliderY + 74.0f, InnerWidth,
        [&](double) {});
    EndCard(Day);

    const float DateY = 60.0f;
    const float FirstCoordinateSlider = 99.0f;
    const char* LocationDescription =
        "Changing time, date or location links the Sun to the existing celestial solver. The day-cycle switch stays in the Overview quick toggles.";
    const float LocationCaptionHeight = WrappedHeight(LocationDescription, InnerWidth, 10.0f);
    const float LocationHeight = FirstCoordinateSlider + 3.0f * 74.0f + 6.0f + LocationCaptionHeight + 13.0f + 18.0f;
    Card Location = BeginCard("sun-coordinates", "Time & location", "04 / COORDINATES", LocationHeight,
                              Width, CardStyle::Coordinates, Ink);
    DrawDateInput(Ink, "sun-calendar-date", "Calendar date", Placement.SkyYear, Placement.SkyMonth, Placement.SkyDay,
                  Location.Position.x + 18.0f, Location.Position.y + DateY, InnerWidth, Placement);
    const float CoordinateX = Location.Position.x + 18.0f;
    const float CoordinateY = Location.Position.y + FirstCoordinateSlider;
    DrawSlider(Surface, SurfaceReady, Ink, "sun-latitude", "Latitude", Placement.LatitudeDegrees,
        -90.0, 90.0, 0.01, "°", 2, 1.0, CoordinateX, CoordinateY, InnerWidth,
        [&](double) { SetTimedField(Placement); });
    DrawSlider(Surface, SurfaceReady, Ink, "sun-longitude", "Longitude", Placement.LongitudeDegrees,
        -180.0, 180.0, 0.01, "°", 2, 1.0, CoordinateX, CoordinateY + 74.0f, InnerWidth,
        [&](double) { SetTimedField(Placement); });
    DrawSlider(Surface, SurfaceReady, Ink, "sun-time-zone", "UTC offset", Placement.TimeZoneHours,
        -12.0, 14.0, 0.5, "h", 1, 1.0, CoordinateX, CoordinateY + 148.0f, InnerWidth,
        [&](double) { SetTimedField(Placement); });
    Caption(Ink, ImVec2(CoordinateX, CoordinateY + 3.0f * 74.0f + 6.0f), InnerWidth, LocationDescription);
    EndCard(Location);
}

void DrawColourSelector(ImDrawList* Ink, SunPlacement& Placement, float X, float Y, float Width)
{
    Text(Ink, X, Y, "Colour source", 10.0f, Hex(0xb3a58a));
    const float ComboWidth = Width;
    ImGui::SetCursorScreenPos(ImVec2(X, Y + 25.0f));
    ImGui::PushID("sun-colour-source");
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 9.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(Hex(0x211e17)));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImGui::ColorConvertU32ToFloat4(Hex(0x2c2921)));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImGui::ColorConvertU32ToFloat4(Hex(0x2c2921)));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(Hex(0xe8c78b, 34)));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Hex(0xe9d7b6)));
    ImGui::SetNextItemWidth(ComboWidth);
    const char* Preview = Placement.ColourSource == 1 ? "RGB" : "Temperature";
    if (ImGui::BeginCombo("##source", Preview))
    {
        const char* Options[] = {"Temperature", "RGB"};
        for (int Index = 0; Index < 2; ++Index)
        {
            const bool Selected = (Placement.ColourSource == Index);
            if (ImGui::Selectable(Options[Index], Selected)) Placement.ColourSource = Index;
            if (Selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(3);
    ImGui::PopID();
}

void DrawTintPicker(ImDrawList* Ink, SunPlacement& Placement, float X, float Y, float Width)
{
    Text(Ink, X, Y + 10.0f, "Sun Tint", 10.0f, Hex(0xb8ad96));
    const float PillWidth = std::min(145.0f, Width);
    const ImVec2 Position(X + Width - PillWidth, Y);
    const ImVec2 Size(PillWidth, 34.0f);
    ImGui::SetCursorScreenPos(Position);
    const bool Clicked = ImGui::InvisibleButton("##sun-tint-pill", Size);
    const bool Hovered = ImGui::IsItemHovered();
    Ink->AddRectFilled(Position, ImVec2(Position.x + Size.x, Position.y + Size.y),
                        Hovered ? Hex(0xe8c78b, 24) : Hex(0xe8c78b, 12), 17.0f);
    Ink->AddRect(Position, ImVec2(Position.x + Size.x, Position.y + Size.y), Hex(0xe8c78b, 48), 17.0f);

    char HexValue[8] = {};
    unsigned Channels[3] = {};
    for (int Index = 0; Index < 3; ++Index)
    {
        const double Linear = Clamp(Placement.Tint[static_cast<size_t>(Index)], 0.0, 1.0);
        Channels[static_cast<size_t>(Index)] = static_cast<unsigned>(std::round(std::pow(Linear, 1.0 / 2.2) * 255.0));
    }
    std::snprintf(HexValue, sizeof(HexValue), "#%02X%02X%02X", Channels[0], Channels[1], Channels[2]);
    Ink->AddCircleFilled(ImVec2(Position.x + 18.0f, Position.y + 17.0f), 11.0f, Hex(0xffffff, 10), 24);
    Ink->AddCircleFilled(ImVec2(Position.x + 18.0f, Position.y + 17.0f), 8.0f,
                         RGB(Channels[0], Channels[1], Channels[2]), 24);
    Text(Ink, Position.x + 34.0f, Position.y + 10.0f, HexValue, 11.0f, Hex(0xe5d8ba));
    Text(Ink, Position.x + Size.x - 16.0f, Position.y + 7.0f, "↗", 12.0f, Hex(0xe5d8ba), 0.5f);
    if (Clicked) ImGui::OpenPopup("##sun-tint-popup");

    ImGui::SetNextWindowSize(ImVec2(260.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopup("##sun-tint-popup"))
    {
        float Display[3] = {static_cast<float>(std::pow(Clamp(Placement.Tint[0], 0.0, 1.0), 1.0 / 2.2)),
                            static_cast<float>(std::pow(Clamp(Placement.Tint[1], 0.0, 1.0), 1.0 / 2.2)),
                            static_cast<float>(std::pow(Clamp(Placement.Tint[2], 0.0, 1.0), 1.0 / 2.2))};
        if (ImGui::ColorPicker3("##tint", Display, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview))
        {
            Placement.Tint = {std::pow(static_cast<double>(Display[0]), 2.2),
                              std::pow(static_cast<double>(Display[1]), 2.2),
                              std::pow(static_cast<double>(Display[2]), 2.2)};
            Placement.ColourSource = 1;
        }
        ImGui::EndPopup();
    }
}

void DrawLightAndColour(PixelSpace& Surface, bool SurfaceReady, SunPlacement& Placement, ImDrawList* Ink, float Width)
{
    const float InnerWidth = std::max(1.0f, Width - 36.0f);
    const char* LightCaption =
        "Exhibit intensity multiplier, not a measured lux or irradiance value. Atmospheric transmission is applied by the renderer.";
    const char* SpectrumCaption =
        "Normalised thermal reference, not measured radiance. The renderer uses its existing RGB approximation; RGB mode uses your custom tint. The blackbody peak may fall outside the visible window.";
    const float LightCaptionHeight = WrappedHeight(LightCaption, Width, 10.0f);
    const float LightHeight = 379.0f + LightCaptionHeight;
    Card Light = BeginCard("sun-light-source", "Light source", "05 / RADIANCE", LightHeight, Width, CardStyle::Radiance, Ink);
    const float X = Light.Position.x;
    const float ContentY = Light.Position.y;
    Readout(Ink, X, ContentY + 30.0f, Width, "Sunlight intensity", Number(Placement.SolarIlluminance, 1).c_str(), "×", 31.0f);
    const float FirstSliderY = ContentY + 86.0f;
    DrawSlider(Surface, SurfaceReady, Ink, "sun-illuminance", "Sunlight multiplier", Placement.SolarIlluminance,
        0.0, 60.0, 0.1, "×", 1, 1.0, X, FirstSliderY, Width,
        [&](double) {});
    const float CaptionY = FirstSliderY + 74.0f + 6.0f;
    Caption(Ink, ImVec2(X, CaptionY), Width, LightCaption);
    const float SelectorY = CaptionY + LightCaptionHeight + 13.0f;
    DrawColourSelector(Ink, Placement, X, SelectorY, Width);
    DrawTintPicker(Ink, Placement, X, SelectorY + 81.0f, Width);
    DrawSlider(Surface, SurfaceReady, Ink, "sun-temperature", "Colour temperature", Placement.TemperatureKelvin,
        2000.0, 10000.0, 50.0, "K", 0, 1.0, X, SelectorY + 126.0f, Width,
        [&](double) { Placement.ColourSource = 0; });
    EndCard(Light);

    const float SpectrumChartHeight = InnerWidth * 280.0f / 640.0f;
    const char* SpectrumCaptionFull = SpectrumCaption;
    const float SpectrumStatGap = 12.0f;
    const float SpectrumStatWidth = (InnerWidth - SpectrumStatGap) * 0.5f;
    const float SpectrumCaptionHeight = WrappedHeight(SpectrumCaptionFull, SpectrumStatWidth, 10.0f);
    const float SpectrumCardHeight = 277.0f + SpectrumChartHeight + SpectrumCaptionHeight;
    Card Spectrum = BeginCard("sun-spectrum", "Source spectrum", "06 / REFERENCE", SpectrumCardHeight,
                              Width, CardStyle::Spectrum, Ink);
    const float SpectrumX = Spectrum.Position.x + 18.0f;
    const float SpectrumInnerWidth = Spectrum.Width - 36.0f;
    const float HeroY = Spectrum.Position.y + 58.0f;
    Readout(Ink, SpectrumX, HeroY, SpectrumInnerWidth * 0.52f, "Thermal reference",
        Number(Placement.TemperatureKelvin, 0).c_str(), "K", 43.0f);
    const char* Mode = Placement.ColourSource == 1 ? "RGB source · reference only" : "Temperature source";
    const float ModeWidth = std::min(SpectrumInnerWidth * 0.45f, TextWidth(9.0f, Mode) + 22.0f);
    DrawTag(Ink, Mode, ImVec2(Spectrum.Position.x + Spectrum.Width - 18.0f - ModeWidth, HeroY + 23.0f),
            ImVec2(ModeWidth, 25.0f));

    const float SpectrumY = HeroY + 93.0f;
    const ImVec2 SpectrumOrigin(SpectrumX, SpectrumY);
    DrawSpectrumDiagram(Ink, SpectrumOrigin, SpectrumInnerWidth, Placement.TemperatureKelvin);
    const float StatsY = SpectrumY + SpectrumChartHeight + 12.0f;
    Ink->AddLine(ImVec2(SpectrumX, StatsY), ImVec2(SpectrumX + SpectrumInnerWidth, StatsY), Hex(0xe8c78b, 25), 1.0f);
    const float StatGap = 12.0f;
    const float StatWidth = (SpectrumInnerWidth - StatGap) / 2.0f;
    const double PeakNanometres = 2897771.955 / Placement.TemperatureKelvin;
    const std::string PeakText = Number(PeakNanometres, 0);
    Readout(Ink, SpectrumX, StatsY + 17.0f, StatWidth, "Blackbody peak", PeakText.c_str(), "nm", 24.0f);
    Readout(Ink, SpectrumX + StatWidth + StatGap, StatsY + 17.0f, StatWidth, "Visible window", "380–780", "nm", 24.0f);
    Caption(Ink, ImVec2(SpectrumX, StatsY + 83.0f), StatWidth, SpectrumCaptionFull);
    EndCard(Spectrum);
}

void DrawDiscAndOptics(PixelSpace& Surface, bool SurfaceReady, SunPlacement& Placement, ImDrawList* Ink, float Width,
                       const std::function<void()>& OpenLensFlare)
{
    const float InnerWidth = std::max(1.0f, Width - 36.0f);
    const float ChartHeight = InnerWidth * 280.0f / 640.0f;
    const char* DiscCaption =
        "Disc diagram is exaggerated for editing; the sky uses the actual angular diameter. The Sun disc quick toggle remains in Overview.";
    const float DiscCaptionHeight = WrappedHeight(DiscCaption, InnerWidth, 10.0f);
    const float FirstSliderY = 128.0f + ChartHeight;
    const float DiscHeight = FirstSliderY + 2.0f * 74.0f + 6.0f + DiscCaptionHeight + 13.0f + 18.0f;
    Card Disc = BeginCard("sun-angular-size", "Apparent solar disc", "07 / ANGULAR SIZE", DiscHeight,
                          Width, CardStyle::Disc, Ink);
    const std::string Diameter = Number(Placement.RadiusDegrees * 2.0, 2);
    Readout(Ink, Disc.Position.x + 18.0f, Disc.Position.y + 48.0f, InnerWidth,
            "Angular diameter", Diameter.c_str(), "°", 31.0f);
    const ImVec2 ChartOrigin(Disc.Position.x + 18.0f, Disc.Position.y + 116.0f);
    DrawDiscDiagram(Ink, ChartOrigin, InnerWidth, Placement);
    ImGui::SetCursorScreenPos(ChartOrigin);
    ImGui::InvisibleButton("##solar-disc-diagram", ImVec2(InnerWidth, ChartHeight));
    const bool Active = ImGui::IsItemActive();
    const bool Focused = ImGui::IsItemFocused();
    if (Active && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        Placement.RadiusDegrees = Clamp(Placement.RadiusDegrees + ImGui::GetIO().MouseDelta.x * 0.005, 0.05, 2.0);
    }
    else if (Focused)
    {
        const double Step = ImGui::GetIO().KeyShift ? 10.0 : 1.0;
        if (ImGui::IsKeyPressed(ImGuiKey_Home)) Placement.RadiusDegrees = 0.27;
        else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow))
            Placement.RadiusDegrees = Clamp(Placement.RadiusDegrees - 0.01 * Step, 0.05, 2.0);
        else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow))
            Placement.RadiusDegrees = Clamp(Placement.RadiusDegrees + 0.01 * Step, 0.05, 2.0);
    }

    DrawSlider(Surface, SurfaceReady, Ink, "sun-radius", "Angular diameter", Placement.RadiusDegrees,
        0.05, 2.0, 0.01, "°", 2, 2.0, Disc.Position.x + 18.0f, Disc.Position.y + FirstSliderY, InnerWidth,
        [&](double) {});
    DrawSlider(Surface, SurfaceReady, Ink, "sun-glow", "Optical aureole", Placement.Glow,
        0.0, 3.0, 0.01, "×", 2, 1.0, Disc.Position.x + 18.0f, Disc.Position.y + FirstSliderY + 74.0f, InnerWidth,
        [&](double) {});
    Caption(Ink, ImVec2(Disc.Position.x + 18.0f, Disc.Position.y + FirstSliderY + 148.0f + 6.0f),
            InnerWidth, DiscCaption);
    EndCard(Disc);

    const char* OpticsCaption = "Lens flare is a separate optical subcomponent. Opening it does not change sunlight or exposure.";
    const float OpticsCaptionHeight = WrappedHeight(OpticsCaption, Width - 36.0f, 10.0f);
    const float OpticsHeight = 139.0f + OpticsCaptionHeight;
    Card Optics = BeginCard("sun-connected-optics", "Connected optics", "08 / SUBCOMPONENT", OpticsHeight,
                            Width, CardStyle::Subcomponent, Ink);
    Caption(Ink, ImVec2(Optics.Position.x + 18.0f, Optics.Position.y + 54.0f), Width - 36.0f, OpticsCaption);
    const float ButtonY = Optics.Position.y + 67.0f + OpticsCaptionHeight;
    const bool OpenedLensFlare = DrawButton(Ink, "##open-lens-flare", "Open Lens Flare   ↗",
        ImVec2(Optics.Position.x + 18.0f, ButtonY), ImVec2(Width - 36.0f, 42.0f),
        Hex(0xe8c78b, 12), Hex(0xe8c78b, 34), Hex(0xe8c78b), 18.0f, 10.0f, false,
        Hex(0xe8c78b, 32), Hex(0xe8c78b, 85));
    if (OpenedLensFlare && OpenLensFlare) OpenLensFlare();
    EndCard(Optics);
}

void DrawHeader(ImDrawList* Ink, SunPlacement& Placement, float& Width)
{
    const ImVec2 Origin = ImGui::GetCursorScreenPos();
    Width = std::min(Width, ImGui::GetContentRegionAvail().x);
    constexpr float Height = 90.0f;
    ImGui::Dummy(ImVec2(Width, Height));
    Text(Ink, Origin.x, Origin.y, "ENVIRONMENT  /  02", 9.0f, Hex(0xa09680));
    Text(Ink, Origin.x, Origin.y + 31.0f, "Sun", 31.0f, Hex(0xe8e7df));
    Text(Ink, Origin.x, Origin.y + 74.0f, "Directional light / position & colour.", 11.0f, Hex(0xa19786));
    if (DrawButton(Ink, "##reset-sun", "Reset ↗",
        ImVec2(Origin.x + Width - 77.0f, Origin.y + 34.0f), ImVec2(77.0f, 28.0f),
        Hex(0xe8c78b, 11), Hex(0xe8c78b, 34), Hex(0xd5bf95), 22.0f, 9.0f, false,
        Hex(0xe8c78b, 28)))
    {
        Placement.ElevationDegrees = 38.0;
        Placement.AzimuthDegrees = 145.0;
        Placement.SolarIlluminance = 12.0;
        Placement.TemperatureKelvin = 5778.0;
        Placement.RadiusDegrees = 0.27;
        Placement.Glow = 1.0;
        Placement.DayAnimate = false;
        Placement.AutoPosition = false;
        Placement.ColourSource = 0;
    }
    ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + Height + 20.0f));
}

int DrawTabs(ImDrawList* Ink, float Width)
{
    static constexpr const char* Labels[] = {"Overview", "Daylight", "Light & colour", "Disc & optics"};
    const ImGuiID StateId = ImGui::GetID("##vehicle-hmi-sun-page");
    int& ActivePage = *ImGui::GetStateStorage()->GetIntRef(StateId, CapturePage);
    ActivePage = std::clamp(ActivePage, 0, 3);
    const ImVec2 Start = ImGui::GetCursorScreenPos();
    constexpr float Gap = 3.0f;
    constexpr float Height = 33.0f;
    const float TabWidth = (Width - Gap * 3.0f) / 4.0f;
    ImGui::Dummy(ImVec2(Width, Height));
    for (int Index = 0; Index < 4; ++Index)
    {
        const ImVec2 Position(Start.x + static_cast<float>(Index) * (TabWidth + Gap), Start.y);
        ImGui::PushID(Index + 9500);
        const bool Clicked = DrawButton(Ink, "##tab", Labels[Index], Position, ImVec2(TabWidth, Height),
            Index == ActivePage ? Hex(0x343329) : Hex(0x191917, 0),
            Index == ActivePage ? Hex(0xe3d2a6, 0) : Hex(0xffffff, 0),
            Index == ActivePage ? Hex(0xe7dfc8) : Hex(0xa79d87), 24.0f, 10.0f, false, Hex(0x352f24));
        const bool Focused = ImGui::IsItemFocused();
        ImGui::PopID();
        if (Clicked) ActivePage = Index;
        if (Focused)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Home)) ActivePage = 0;
            else if (ImGui::IsKeyPressed(ImGuiKey_End)) ActivePage = 3;
            else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) ActivePage = (ActivePage + 3) % 4;
            else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) ActivePage = (ActivePage + 1) % 4;
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(Start.x, Start.y + Height + 20.0f));
    return ActivePage;
}

void DrawFooter(ImDrawList* Ink, float Width, float AdditionalTopGap)
{
    const ImVec2 Cursor = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(Cursor.x, Cursor.y + AdditionalTopGap));
    const ImVec2 Position = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(Width, 32.0f));
    Ink->AddLine(Position, ImVec2(Position.x + Width, Position.y), Hex(0xe8c78b, 18), 1.0f);
    Text(Ink, Position.x + 2.0f, Position.y + 16.0f, "DIRECTIONAL LIGHT / Z UP", 8.0f, Hex(0xa29473));
    Text(Ink, Position.x + Width - 2.0f, Position.y + 16.0f, "Settings apply live ↗", 8.0f, Hex(0xa29473), 1.0f);
}

} // namespace

#if defined(FRONTIER_WITH_WORKSPACE_CAPTURE)
void ConfigureSunInspectorPageForCapture(int Page) noexcept
{
    CapturePage = std::clamp(Page, 0, 3);
}
#endif

void ConstructSunInspectorDashboard(SunPlacement& Placement, const std::function<void()>& OpenLensFlare)
{
    AdvanceSunPlacement(Placement, ImGui::GetIO().DeltaTime);
    ImDrawList* Ink = ImGui::GetWindowDrawList();
    const ImVec2 WindowPosition = ImGui::GetWindowPos();
    const ImVec2 WindowSize = ImGui::GetWindowSize();
    Ink->AddRectFilled(WindowPosition, ImVec2(WindowPosition.x + WindowSize.x, WindowPosition.y + WindowSize.y), Hex(0x191917));

    PixelSpace Surface;
    const ImVec2 Display = ImGui::GetIO().DisplaySize;
    const bool SurfaceReady = Surface.Begin(SurfaceLayer::Window, Display.x, Display.y);

    const ImVec2 HostCursor = ImGui::GetCursorScreenPos();
    const float HostWidth = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorScreenPos(ImVec2(HostCursor.x + 18.0f, HostCursor.y + 22.0f));
    float Width = std::max(120.0f, HostWidth - 36.0f);
    DrawHeader(Ink, Placement, Width);
    const int ActivePage = DrawTabs(Ink, Width);
    switch (ActivePage)
    {
        case 0: DrawOverview(Surface, SurfaceReady, Placement, Ink, Width); break;
        case 1: DrawDaylight(Surface, SurfaceReady, Placement, Ink, Width); break;
        case 2: DrawLightAndColour(Surface, SurfaceReady, Placement, Ink, Width); break;
        case 3: DrawDiscAndOptics(Surface, SurfaceReady, Placement, Ink, Width, OpenLensFlare); break;
        default: DrawOverview(Surface, SurfaceReady, Placement, Ink, Width); break;
    }
    // Match CSS margin collapse: 17px footer gap minus Overview's 12px presets or the other pages' 14px card margin.
    DrawFooter(Ink, Width, ActivePage == 0 ? 5.0f : 3.0f);
}

} // namespace Frontier

#endif // FRONTIER_WITH_INTERFACE_SDK
