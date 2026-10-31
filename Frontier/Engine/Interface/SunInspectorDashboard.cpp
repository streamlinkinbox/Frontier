/*====================================================================================================================================
                                                    SUNINSPECTORDASHBOARD.CPP
====================================================================================================================================*/
// 🧩 Native ImGui/ControlKit port of VehicleHMI's SunInspectorDashboard.js.
//    Data remains on SunRecord; this presentation does not add a renderer or a second lighting model.

#include "Engine/Interface/SunInspectorDashboard.h"

#if defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/ControlKit.h"
#include "Engine/Rendering/CelestialModel.h"
#include "Engine/Scene/EnvironmentRecords.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>

namespace Frontier
{
namespace
{
constexpr double Pi = 3.14159265358979323846;
constexpr double DegreesToRadians = Pi / 180.0;
constexpr double RadiansToDegrees = 180.0 / Pi;
constexpr double PlanckC2 = 0.01438776877;

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
    Placement.LightingPreset = 0;
}

void SetTimedField(SunPlacement& Placement) noexcept
{
    Placement.AutoPosition = true;
    Placement.LightingPreset = 0;
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

template <typename OnChange>
bool DrawSlider(PixelSpace& Surface, bool SurfaceReady, const char* Id, const char* Label,
                double& Value, double Minimum, double Maximum, double DisplayStep,
                const char* Unit, int Digits, double DisplayScale, OnChange&& Changed)
{
    bool ValueChanged = false;
    ImGui::PushID(Id);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(Label);
    const float Available = ImGui::GetContentRegionAvail().x;
    ImGui::SameLine(std::max(0.0f, Available - 130.0f));
    ImGui::SetNextItemWidth(104.0f);
    double DisplayValue = Value * DisplayScale;
    const std::string Format = "%." + std::to_string(std::clamp(Digits, 0, 5)) + "f";
    if (ImGui::InputDouble("##value", &DisplayValue, DisplayStep, DisplayStep * 10.0, Format.c_str()))
    {
        const double UnderlyingStep = DisplayScale == 0.0 ? DisplayStep : DisplayStep / std::abs(DisplayScale);
        const double Next = Snap(DisplayValue / DisplayScale, Minimum, Maximum, UnderlyingStep);
        if (std::abs(Next - Value) > 1e-10)
        {
            Value = Next;
            Changed(Value);
            ValueChanged = true;
        }
    }
    if (Unit != nullptr && Unit[0] != '\0')
    {
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::TextDisabled("%s", Unit);
    }

    const ImVec2 TrackMinimum = ImGui::GetCursorScreenPos();
    const float TrackWidth = std::max(20.0f, ImGui::GetContentRegionAvail().x);
    ImGui::InvisibleButton("##slider-track", ImVec2(TrackWidth, 18.0f));
    const bool Hovered = ImGui::IsItemHovered();
    const bool Active = ImGui::IsItemActive();
    if (SurfaceReady)
    {
        const ImGuiIO& Io = ImGui::GetIO();
        ControlPointer Pointer;
        Pointer.X = Io.MousePos.x;
        Pointer.Y = Io.MousePos.y;
        Pointer.Down = Active && ImGui::IsMouseDown(ImGuiMouseButton_Left);
        Pointer.Pressed = ImGui::IsItemActivated();
        Pointer.Released = ImGui::IsItemDeactivated();
        Pointer.Enabled = Hovered || Active;

        float Output = static_cast<float>(Value);
        const ControlHit Hit = ControlKit::Slider(
            Surface, Spanning(TrackMinimum.x, TrackMinimum.y, TrackWidth, 18.0f),
            static_cast<float>(Minimum), static_cast<float>(Maximum), static_cast<float>(Value), Active,
            Pointer, Output, 1.0f, 4.0f, 11.0f);
        if (Hit.Dragging && Pointer.Down)
        {
            const double UnderlyingStep = DisplayScale == 0.0 ? DisplayStep : DisplayStep / std::abs(DisplayScale);
            const double Next = Snap(static_cast<double>(Output), Minimum, Maximum, UnderlyingStep);
            if (std::abs(Next - Value) > 1e-10)
            {
                Value = Next;
                Changed(Value);
                ValueChanged = true;
            }
        }
    }
    ImGui::Dummy(ImVec2(1.0f, 5.0f));
    ImGui::PopID();
    return ValueChanged;
}

void DrawSectionHeading(const char* Title, const char* Index)
{
    ImGui::Spacing();
    ImGui::TextDisabled("%s", Title);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", Index);
    ImGui::Separator();
}

void DrawToggleTiles(SunPlacement& Placement)
{
    const float Gap = 7.0f;
    const float Width = std::max(70.0f, (ImGui::GetContentRegionAvail().x - Gap * 2.0f) / 3.0f);
    struct Tile
    {
        const char* Label;
        const char* Mark;
        bool* Value;
    };
    Tile Tiles[] = {{"Sunlight", "SUN", &Placement.Enabled},
                    {"Sun disc", "DISC", &Placement.DiscEnabled},
                    {"Day cycle", "CYCLE", &Placement.DayAnimate}};
    for (int Index = 0; Index < 3; ++Index)
    {
        if (Index != 0) ImGui::SameLine(0.0f, Gap);
        ImGui::PushID(Index + 7300);
        const bool On = *Tiles[Index].Value;
        const ImVec4 ActiveBg(0.12f, 0.22f, 0.16f, 1.0f);
        const ImVec4 IdleBg(0.09f, 0.09f, 0.09f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, On ? ActiveBg : IdleBg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, On ? ImVec4(0.16f, 0.28f, 0.20f, 1.0f) : ImVec4(0.14f, 0.14f, 0.14f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ActiveBg);
        const std::string Caption = std::string(Tiles[Index].Mark) + "  " + Tiles[Index].Label + (On ? "  ON" : "  OFF");
        if (ImGui::Button(Caption.c_str(), ImVec2(Width, 46.0f)))
        {
            *Tiles[Index].Value = !On;
            if (Index == 2 && Placement.DayAnimate)
                Placement.AutoPosition = true;
        }
        ImGui::PopStyleColor(3);
        ImGui::PopID();
    }
    ImGui::Spacing();
}

void DrawVectorEditor(SunPlacement& Placement)
{
    DrawSectionHeading("Solar vector", "01 / DIRECTION");
    const float Width = std::max(220.0f, ImGui::GetContentRegionAvail().x);
    const float Height = std::clamp(Width * 0.62f, 178.0f, 268.0f);
    const ImVec2 TopLeft = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##sun-vector-diagram", ImVec2(Width, Height));
    const bool Active = ImGui::IsItemActive();
    const ImVec2 BottomRight(TopLeft.x + Width, TopLeft.y + Height);
    ImDrawList* Draw = ImGui::GetWindowDrawList();
    Draw->AddRectFilled(TopLeft, BottomRight, IM_COL32(18, 18, 18, 255), 12.0f);
    Draw->AddRect(TopLeft, BottomRight, IM_COL32(47, 47, 47, 255), 12.0f);
    const float Radius = std::min(Height * 0.42f, Width * 0.29f);
    const ImVec2 Centre(TopLeft.x + Width * 0.50f, TopLeft.y + Height * 0.52f);
    for (int Ring = 1; Ring <= 4; ++Ring)
    {
        const ImU32 Color = Ring == 2 ? IM_COL32(215, 194, 155, 88) : IM_COL32(215, 194, 155, 25);
        Draw->AddCircle(Centre, Radius * static_cast<float>(Ring) / 2.0f, Color, 48, 1.0f);
    }
    Draw->AddLine(ImVec2(Centre.x - Radius - 8.0f, Centre.y), ImVec2(Centre.x + Radius + 8.0f, Centre.y), IM_COL32(232, 199, 139, 48), 1.0f);
    Draw->AddLine(ImVec2(Centre.x, Centre.y - Radius - 8.0f), ImVec2(Centre.x, Centre.y + Radius + 8.0f), IM_COL32(232, 199, 139, 48), 1.0f);
    Draw->AddText(ImVec2(Centre.x + 5.0f, TopLeft.y + 7.0f), IM_COL32(170, 158, 136, 255), "+Y / N");
    Draw->AddText(ImVec2(TopLeft.x + Width - 44.0f, Centre.y + 5.0f), IM_COL32(170, 158, 136, 255), "+X / E");
    Draw->AddText(ImVec2(TopLeft.x + 8.0f, Centre.y + 5.0f), IM_COL32(170, 158, 136, 255), "W");
    Draw->AddText(ImVec2(Centre.x - 3.0f, BottomRight.y - 22.0f), IM_COL32(170, 158, 136, 255), "S");

    const ResolvedSun Current = ResolveSun(Placement);
    const double AzimuthRadians = Current.Azimuth * DegreesToRadians;
    const float Distance = static_cast<float>(Clamp(90.0 - Current.Elevation, 0.0, 180.0) / 180.0) * Radius;
    const ImVec2 SunPoint(Centre.x + std::cos(AzimuthRadians) * Distance,
                          Centre.y - std::sin(AzimuthRadians) * Distance);
    const bool Lit = Placement.Enabled && Current.Elevation >= 0.0;
    Draw->AddLine(Centre, SunPoint, Lit ? IM_COL32(232, 199, 139, 235) : IM_COL32(135, 147, 139, 220), 1.5f);
    Draw->AddCircleFilled(Centre, 3.8f, IM_COL32(217, 202, 176, 255));
    Draw->AddCircleFilled(SunPoint, 11.0f, Lit ? IM_COL32(232, 199, 139, 42) : IM_COL32(135, 147, 139, 42));
    Draw->AddCircle(SunPoint, 9.0f, Lit ? IM_COL32(232, 199, 139, 150) : IM_COL32(135, 147, 139, 150), 24, 1.0f);
    Draw->AddCircleFilled(SunPoint, 4.0f, Lit ? IM_COL32(240, 214, 159, 255) : IM_COL32(141, 155, 145, 255));
    Draw->AddText(ImVec2(TopLeft.x + 12.0f, BottomRight.y - 22.0f), IM_COL32(171, 157, 136, 255), "DRAG VECTOR");
    Draw->AddText(ImVec2(TopLeft.x + 12.0f, TopLeft.y + 12.0f), IM_COL32(171, 157, 136, 255),
                  Placement.AutoPosition ? "TIME LINKED" : "MANUAL");
    if (Active && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        const ImVec2 Mouse = ImGui::GetIO().MousePos;
        const double DX = Mouse.x - Centre.x;
        const double DY = Centre.y - Mouse.y;
        const double Length = std::hypot(DX, DY);
        if (Length > 1.0)
        {
            SetDirectionManual(Placement);
            Placement.AzimuthDegrees = Clamp(std::atan2(DY, DX) * RadiansToDegrees, -180.0, 180.0);
            Placement.ElevationDegrees = Clamp(90.0 - Length / Radius * 90.0, -90.0, 90.0);
        }
    }
    if (ImGui::IsItemFocused())
    {
        const double Nudge = ImGui::GetIO().KeyShift ? 10.0 : 1.0;
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
                (ImGui::IsKeyPressed(ImGuiKey_RightArrow) ? Nudge : -Nudge), -180.0, 180.0);
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow))
        {
            SetDirectionManual(Placement);
            Placement.ElevationDegrees = Clamp(Placement.ElevationDegrees +
                (ImGui::IsKeyPressed(ImGuiKey_UpArrow) ? Nudge : -Nudge), -90.0, 90.0);
        }
    }
    const std::string Coordinates = Number(Current.Direction.X, 2) + " / " + Number(Current.Direction.Y, 2) + " / " + Number(Current.Direction.Z, 2);
    ImGui::TextDisabled("WORLD XYZ · Z UP · %s", Coordinates.c_str());
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

SolarDaySummary ComputeSolarDay(const SunPlacement& Placement)
{
    SolarDaySummary Result;
    for (size_t Index = 0; Index < Result.Elevations.size(); ++Index)
    {
        const double Hour = static_cast<double>(Index) / 4.0;
        const double Julian = JulianDate(Placement.SkyYear, static_cast<unsigned>(Placement.SkyMonth),
                                         static_cast<unsigned>(Placement.SkyDay), Hour - Placement.TimeZoneHours);
        Result.Elevations[Index] = SunPosition(Julian, Placement.LatitudeDegrees, Placement.LongitudeDegrees).ElevationRad * RadiansToDegrees;
    }
    for (size_t Index = 1; Index < Result.Elevations.size(); ++Index)
    {
        const double A = Result.Elevations[Index - 1];
        const double B = Result.Elevations[Index];
        const double Step = 0.25;
        if (A >= 0.0 && B >= 0.0) Result.DayHours += Step;
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

void DrawDaylightChart(SunPlacement& Placement)
{
    DrawSectionHeading("Solar day", "03 / EPHEMERIS");
    const float Width = std::max(220.0f, ImGui::GetContentRegionAvail().x);
    const float Height = 186.0f;
    const ImVec2 TopLeft = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##solar-day-chart", ImVec2(Width, Height));
    const bool Active = ImGui::IsItemActive();
    const ImVec2 BottomRight(TopLeft.x + Width, TopLeft.y + Height);
    ImDrawList* Draw = ImGui::GetWindowDrawList();
    Draw->AddRectFilled(TopLeft, BottomRight, IM_COL32(18, 18, 18, 255), 12.0f);
    Draw->AddRect(TopLeft, BottomRight, IM_COL32(47, 47, 47, 255), 12.0f);
    const SolarDaySummary Data = ComputeSolarDay(Placement);
    const float Left = TopLeft.x + 36.0f;
    const float Right = BottomRight.x - 14.0f;
    const float Top = TopLeft.y + 17.0f;
    const float Bottom = BottomRight.y - 29.0f;
    const auto Y = [&](double Elevation) {
        return Bottom - static_cast<float>((Elevation + 20.0) / 110.0) * (Bottom - Top);
    };
    for (const double Degree : {-10.0, 0.0, 60.0})
    {
        const float YValue = Y(Degree);
        Draw->AddLine(ImVec2(Left, YValue), ImVec2(Right, YValue), Degree == 0.0 ? IM_COL32(232, 199, 139, 105) : IM_COL32(232, 199, 139, 28), 1.0f);
        char Label[12] = {};
        std::snprintf(Label, sizeof(Label), "%.0f°", Degree);
        Draw->AddText(ImVec2(TopLeft.x + 7.0f, YValue - 6.0f), IM_COL32(156, 148, 127, 255), Label);
    }
    std::array<ImVec2, 97> Points{};
    for (size_t Index = 0; Index < Points.size(); ++Index)
    {
        const float X = Left + (Right - Left) * static_cast<float>(Index) / 96.0f;
        Points[Index] = ImVec2(X, Y(Data.Elevations[Index]));
    }
    Draw->AddPolyline(Points.data(), static_cast<int>(Points.size()), IM_COL32(159, 153, 122, 175), ImDrawFlags_None, 1.8f);
    for (size_t Index = 0; Index + 1 < Points.size(); ++Index)
    {
        if (Data.Elevations[Index] >= 0.0 && Data.Elevations[Index + 1] >= 0.0)
            Draw->AddLine(Points[Index], Points[Index + 1], IM_COL32(239, 211, 151, 255), 2.4f);
    }
    const float CurrentX = Left + (Right - Left) * static_cast<float>(Clamp(Placement.LocalSolarTime / 24.0, 0.0, 1.0));
    Draw->AddLine(ImVec2(CurrentX, Top), ImVec2(CurrentX, Bottom), IM_COL32(232, 199, 139, 110), 1.0f);
    if (Placement.AutoPosition)
    {
        const ResolvedSun Current = ResolveSun(Placement);
        Draw->AddCircleFilled(ImVec2(CurrentX, Y(Current.Elevation)), 4.5f, IM_COL32(241, 214, 158, 255));
    }
    for (const int Hour : {0, 6, 12, 18, 24})
    {
        const float X = Left + (Right - Left) * static_cast<float>(Hour) / 24.0f;
        char Label[8] = {};
        std::snprintf(Label, sizeof(Label), "%02d:00", Hour);
        Draw->AddText(ImVec2(X - 16.0f, BottomRight.y - 20.0f), IM_COL32(163, 153, 130, 255), Label);
    }
    if (Active && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        const double Fraction = Clamp((ImGui::GetIO().MousePos.x - Left) / std::max(1.0f, Right - Left), 0.0, 1.0);
        Placement.LocalSolarTime = std::round(Fraction * 24.0 * 100.0) / 100.0;
        SetTimedField(Placement);
    }
    else if (ImGui::IsItemFocused())
    {
        const double Nudge = ImGui::GetIO().KeyShift ? 1.0 : 0.25;
        if (ImGui::IsKeyPressed(ImGuiKey_Home)) Placement.LocalSolarTime = 12.0;
        else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow))
            Placement.LocalSolarTime = std::fmod(Placement.LocalSolarTime - Nudge + 24.0, 24.0);
        else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow))
            Placement.LocalSolarTime = std::fmod(Placement.LocalSolarTime + Nudge, 24.0);
        SetTimedField(Placement);
    }
    ImGui::TextDisabled("Rise  %s     Set  %s     Above horizon  %.1f h",
                        Data.HasRise ? SolarClock(Data.Rise).c_str() : "—",
                        Data.HasSet ? SolarClock(Data.Set).c_str() : "—", Data.DayHours);
}

void DrawBlackbodySpectrum(const SunPlacement& Placement)
{
    DrawSectionHeading("Source spectrum", "06 / REFERENCE");
    const float Width = std::max(220.0f, ImGui::GetContentRegionAvail().x);
    const float Height = 132.0f;
    const ImVec2 TopLeft = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##sun-spectrum", ImVec2(Width, Height));
    const ImVec2 BottomRight(TopLeft.x + Width, TopLeft.y + Height);
    ImDrawList* Draw = ImGui::GetWindowDrawList();
    Draw->AddRectFilled(TopLeft, BottomRight, IM_COL32(18, 18, 18, 255), 10.0f);
    Draw->AddRect(TopLeft, BottomRight, IM_COL32(47, 47, 47, 255), 10.0f);
    for (int Grid = 1; Grid <= 3; ++Grid)
    {
        const float Y = TopLeft.y + 12.0f + static_cast<float>(Grid) * (Height - 32.0f) / 4.0f;
        Draw->AddLine(ImVec2(TopLeft.x + 18.0f, Y), ImVec2(BottomRight.x - 12.0f, Y), IM_COL32(234, 214, 173, 25), 1.0f);
    }
    constexpr int SampleCount = 65;
    std::array<double, SampleCount> Spectrum{};
    double Peak = 0.0;
    for (int Index = 0; Index < SampleCount; ++Index)
    {
        const double Wavelength = (380.0 + static_cast<double>(Index) / 64.0 * 400.0) * 1e-9;
        const double Exponent = PlanckC2 / (Wavelength * Placement.TemperatureKelvin);
        Spectrum[static_cast<size_t>(Index)] = 1.0 / (std::pow(Wavelength, 5.0) * std::expm1(Exponent));
        Peak = std::max(Peak, Spectrum[static_cast<size_t>(Index)]);
    }
    std::array<ImVec2, SampleCount> Points{};
    const float GraphTop = TopLeft.y + 11.0f;
    const float GraphBottom = BottomRight.y - 25.0f;
    const float GraphLeft = TopLeft.x + 17.0f;
    const float GraphRight = BottomRight.x - 12.0f;
    for (int Index = 0; Index < SampleCount; ++Index)
    {
        const float X = GraphLeft + (GraphRight - GraphLeft) * static_cast<float>(Index) / 64.0f;
        const float Y = GraphBottom - static_cast<float>(Spectrum[static_cast<size_t>(Index)] / Peak) * (GraphBottom - GraphTop);
        Points[static_cast<size_t>(Index)] = ImVec2(X, Y);
    }
    Draw->AddPolyline(Points.data(), SampleCount, IM_COL32(240, 221, 186, 245), ImDrawFlags_None, 2.0f);
    const double PeakNanometers = 2.897771955e6 / Placement.TemperatureKelvin;
    if (PeakNanometers >= 380.0 && PeakNanometers <= 780.0)
    {
        const float X = GraphLeft + static_cast<float>((PeakNanometers - 380.0) / 400.0) * (GraphRight - GraphLeft);
        Draw->AddLine(ImVec2(X, GraphTop), ImVec2(X, GraphBottom), IM_COL32(238, 214, 166, 135), 1.0f);
        Draw->AddCircleFilled(ImVec2(X, GraphTop + 5.0f), 3.0f, IM_COL32(240, 212, 157, 255));
    }
    ImGui::TextDisabled("380 nm  ·  visible band  ·  780 nm     Blackbody peak  %.0f nm", PeakNanometers);
}

void DrawDiscDiagram(SunPlacement& Placement)
{
    DrawSectionHeading("Apparent solar disc", "07 / ANGULAR SIZE");
    const float Width = std::max(220.0f, ImGui::GetContentRegionAvail().x);
    const float Height = 142.0f;
    const ImVec2 TopLeft = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##sun-disc-diagram", ImVec2(Width, Height));
    const bool Active = ImGui::IsItemActive();
    const ImVec2 Centre(TopLeft.x + Width * 0.5f, TopLeft.y + Height * 0.48f);
    const float BaseRadius = std::clamp(10.0f + static_cast<float>(Placement.RadiusDegrees) * 38.0f, 11.0f, 86.0f);
    ImDrawList* Draw = ImGui::GetWindowDrawList();
    Draw->AddRectFilled(TopLeft, ImVec2(TopLeft.x + Width, TopLeft.y + Height), IM_COL32(18, 18, 18, 255), 10.0f);
    Draw->AddRect(TopLeft, ImVec2(TopLeft.x + Width, TopLeft.y + Height), IM_COL32(47, 47, 47, 255), 10.0f);
    for (const float Ring : {30.0f, 54.0f, 72.0f})
        Draw->AddCircle(Centre, Ring, IM_COL32(232, 199, 139, 28), 48, 1.0f);
    Draw->AddLine(ImVec2(Centre.x - 92.0f, Centre.y), ImVec2(Centre.x + 92.0f, Centre.y), IM_COL32(232, 199, 139, 38), 1.0f);
    Draw->AddLine(ImVec2(Centre.x, Centre.y - 60.0f), ImVec2(Centre.x, Centre.y + 60.0f), IM_COL32(232, 199, 139, 38), 1.0f);
    const ImU32 Glow = IM_COL32(232, 199, 139, static_cast<int>(Clamp(Placement.Glow * 22.0, 0.0, 80.0)));
    Draw->AddCircleFilled(Centre, BaseRadius + 24.0f, Glow, 48);
    Draw->AddCircleFilled(Centre, BaseRadius, Placement.DiscEnabled ? IM_COL32(241, 215, 166, 255) : IM_COL32(107, 99, 81, 255), 48);
    Draw->AddLine(ImVec2(Centre.x - BaseRadius, Centre.y), ImVec2(Centre.x + BaseRadius, Centre.y), IM_COL32(255, 243, 207, 140), 1.0f);
    if (Active && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        Placement.RadiusDegrees = Clamp(Placement.RadiusDegrees + ImGui::GetIO().MouseDelta.x * 0.005, 0.05, 2.0);
        Placement.LightingPreset = 0;
    }
    else if (ImGui::IsItemFocused())
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Home)) Placement.RadiusDegrees = 0.27;
        else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow))
            Placement.RadiusDegrees = Clamp(Placement.RadiusDegrees - 0.01, 0.05, 2.0);
        else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow))
            Placement.RadiusDegrees = Clamp(Placement.RadiusDegrees + 0.01, 0.05, 2.0);
        Placement.LightingPreset = 0;
    }
    ImGui::TextDisabled("ANGULAR SIZE / EXAGGERATED · diameter %.2f°", Placement.RadiusDegrees * 2.0);
}

void ApplyPreset(SunPlacement& Placement, int Preset)
{
    Placement.LightingPreset = Preset;
    Placement.AutoPosition = false;
    Placement.DayAnimate = false;
    switch (Preset)
    {
        case 1: Placement.ElevationDegrees = 64.0; Placement.AzimuthDegrees = 145.0; Placement.SolarIlluminance = 12.0; break;
        case 2: Placement.ElevationDegrees = 10.0; Placement.AzimuthDegrees = 160.0; Placement.SolarIlluminance = 12.0; break;
        case 3: Placement.ElevationDegrees = 1.5; Placement.AzimuthDegrees = 172.0; Placement.SolarIlluminance = 12.0; break;
        case 4: Placement.ElevationDegrees = -14.0; Placement.AzimuthDegrees = 172.0; Placement.SolarIlluminance = 12.0; break;
        default: Placement.LightingPreset = 0; break;
    }
}

void DrawPresetButtons(SunPlacement& Placement)
{
    DrawSectionHeading("Lighting presets", "02 / LOOK");
    constexpr const char* Labels[] = {"Noon", "Golden", "Dusk", "Night"};
    const float ButtonWidth = std::max(52.0f, (ImGui::GetContentRegionAvail().x - 15.0f) / 4.0f);
    for (int Index = 0; Index < 4; ++Index)
    {
        if (Index != 0) ImGui::SameLine(0.0f, 5.0f);
        ImGui::PushID(7400 + Index);
        const bool Selected = Placement.LightingPreset == Index + 1;
        if (Selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.78f, 0.83f, 0.79f, 1.0f));
        if (ImGui::Button(Labels[Index], ImVec2(ButtonWidth, 29.0f))) ApplyPreset(Placement, Index + 1);
        if (Selected) ImGui::PopStyleColor();
        ImGui::PopID();
    }
}

void DrawOverview(PixelSpace& Surface, bool SurfaceReady, SunPlacement& Placement)
{
    DrawVectorEditor(Placement);
    DrawToggleTiles(Placement);
    const ResolvedSun Current = ResolveSun(Placement);
    ImGui::TextDisabled("AZIMUTH"); ImGui::SameLine(); ImGui::Text("%s°", Number(Current.Azimuth, 1).c_str());
    ImGui::SameLine(0.0f, 22.0f); ImGui::TextDisabled("SUNLIGHT"); ImGui::SameLine(); ImGui::Text("%s×", Number(Placement.SolarIlluminance, 1).c_str());
    ImGui::SameLine(0.0f, 22.0f); ImGui::TextDisabled("SOURCE"); ImGui::SameLine();
    ImGui::Text("%s", Placement.ColourSource == 0 ? (Number(Placement.TemperatureKelvin, 0) + " K").c_str() : "RGB");
    ImGui::SameLine(0.0f, 16.0f);
    if (ImGui::SmallButton(Placement.AutoPosition ? "Time linked ↗" : "Manual ↗"))
    {
        Placement.AutoPosition = !Placement.AutoPosition;
        if (!Placement.AutoPosition) Placement.DayAnimate = false;
    }
    DrawSectionHeading("Direction controls", "02 / AIM");
    DrawSlider(Surface, SurfaceReady, "sun-azimuth", "Azimuth", Placement.AzimuthDegrees, -180.0, 180.0, 0.1, "°", 1, 1.0,
               [&](double) { SetDirectionManual(Placement); });
    DrawSlider(Surface, SurfaceReady, "sun-elevation", "Elevation", Placement.ElevationDegrees, -90.0, 90.0, 0.1, "°", 1, 1.0,
               [&](double) { SetDirectionManual(Placement); });
    ImGui::TextWrapped("Inner horizon ring: 0°. Centre: zenith. Editing direction stops the day-cycle toggle and returns to manual placement.");
    DrawPresetButtons(Placement);
}

void DrawDaylight(PixelSpace& Surface, bool SurfaceReady, SunPlacement& Placement)
{
    const ResolvedSun Current = ResolveSun(Placement);
    ImGui::TextDisabled("LOCAL TIME · %s · %s", SolarClock(Placement.LocalSolarTime).c_str(),
                        Placement.AutoPosition ? "TIME LINKED" : "MANUAL DIRECTION");
    ImGui::Text("Solar position");
    ImGui::TextDisabled("Elevation %s°     Azimuth %s°", Number(Current.Elevation, 1).c_str(), Number(Current.Azimuth, 1).c_str());
    DrawDaylightChart(Placement);
    DrawSlider(Surface, SurfaceReady, "sun-time", "Local solar control time", Placement.LocalSolarTime,
               0.0, 24.0, 0.01, "h", 2, 1.0, [&](double) { SetTimedField(Placement); });
    DrawSlider(Surface, SurfaceReady, "sun-day-duration", "Full day duration", Placement.DayDurationHours,
               0.01, 24.0, 0.01, "real h", 2, 1.0, [&](double) { Placement.LightingPreset = 0; });
    DrawSectionHeading("Time & location", "04 / COORDINATES");
    ImGui::TextDisabled("Calendar date");
    int Date[3] = {Placement.SkyYear, Placement.SkyMonth, Placement.SkyDay};
    if (ImGui::InputInt3("##calendar-date", Date))
    {
        Placement.SkyYear = std::clamp(Date[0], 1600, 2600);
        Placement.SkyMonth = std::clamp(Date[1], 1, 12);
        Placement.SkyDay = std::clamp(Date[2], 1, 31);
        SetTimedField(Placement);
    }
    DrawSlider(Surface, SurfaceReady, "sun-latitude", "Latitude", Placement.LatitudeDegrees,
               -90.0, 90.0, 0.01, "°", 2, 1.0, [&](double) { SetTimedField(Placement); });
    DrawSlider(Surface, SurfaceReady, "sun-longitude", "Longitude", Placement.LongitudeDegrees,
               -180.0, 180.0, 0.01, "°", 2, 1.0, [&](double) { SetTimedField(Placement); });
    DrawSlider(Surface, SurfaceReady, "sun-time-zone", "UTC offset", Placement.TimeZoneHours,
               -12.0, 14.0, 0.5, "h", 1, 1.0, [&](double) { SetTimedField(Placement); });
    ImGui::TextWrapped("Date/location path uses the existing low-precision celestial solver. The day-cycle control is authoring state; runtime animation is future work.");
}

void DrawLightAndColour(PixelSpace& Surface, bool SurfaceReady, SunPlacement& Placement)
{
    DrawSectionHeading("Light source", "05 / RADIANCE");
    ImGui::TextDisabled("Sunlight intensity   %s×", Number(Placement.SolarIlluminance, 1).c_str());
    DrawSlider(Surface, SurfaceReady, "sun-illuminance", "Sunlight multiplier", Placement.SolarIlluminance,
               0.0, 60.0, 0.1, "×", 1, 1.0, [&](double) { Placement.LightingPreset = 0; });
    ImGui::TextDisabled("Colour source");
    if (ImGui::RadioButton("Temperature", Placement.ColourSource == 0)) Placement.ColourSource = 0;
    ImGui::SameLine();
    if (ImGui::RadioButton("RGB", Placement.ColourSource == 1)) Placement.ColourSource = 1;
    DrawSlider(Surface, SurfaceReady, "sun-temperature", "Colour temperature", Placement.TemperatureKelvin,
               2000.0, 10000.0, 50.0, "K", 0, 1.0, [&](double) { Placement.ColourSource = 0; });
    float Tint[3] = {static_cast<float>(Placement.Tint[0]), static_cast<float>(Placement.Tint[1]), static_cast<float>(Placement.Tint[2])};
    if (ImGui::ColorEdit3("Sun tint · linear", Tint, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs))
    {
        Placement.Tint = {Clamp(Tint[0], 0.0, 1.0), Clamp(Tint[1], 0.0, 1.0), Clamp(Tint[2], 0.0, 1.0)};
        Placement.ColourSource = 1;
    }
    DrawBlackbodySpectrum(Placement);
    ImGui::TextWrapped("Normalized blackbody reference only; intensity is an exhibit scale, not measured lux or irradiance. Renderer integration remains future work.");
}

void DrawDiscAndOptics(PixelSpace& Surface, bool SurfaceReady, SunPlacement& Placement)
{
    const double Diameter = Placement.RadiusDegrees * 2.0;
    ImGui::TextDisabled("ANGULAR DIAMETER   %s°", Number(Diameter, 2).c_str());
    DrawDiscDiagram(Placement);
    DrawSlider(Surface, SurfaceReady, "sun-radius", "Angular diameter", Placement.RadiusDegrees,
               0.05, 2.0, 0.01, "°", 2, 2.0, [&](double) { Placement.LightingPreset = 0; });
    DrawSlider(Surface, SurfaceReady, "sun-glow", "Optical aureole", Placement.Glow,
               0.0, 3.0, 0.01, "×", 2, 1.0, [&](double) { Placement.LightingPreset = 0; });
    ImGui::TextWrapped("Disc diagram is exaggerated for editing; the sky uses the authored angular radius. Lens flare is a separate optical component and is not connected to this UI slice.");
}

void ResetSun(SunPlacement& Placement)
{
    Placement = SunPlacement{};
}

} // namespace

void ConstructSunInspectorDashboard(SunPlacement& Placement)
{
    AdvanceSunPlacement(Placement, ImGui::GetIO().DeltaTime);
    ImGui::TextDisabled("ENVIRONMENT  /  02");
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - 72.0f));
    if (ImGui::SmallButton("Reset ↗")) ResetSun(Placement);
    ImGui::Text("Sun");
    ImGui::TextDisabled("Directional light / position & colour.");

    PixelSpace Surface;
    const ImVec2 Display = ImGui::GetIO().DisplaySize;
    const bool SurfaceReady = Surface.Begin(SurfaceLayer::Window, Display.x, Display.y);
    if (ImGui::BeginTabBar("##SunInspectorSections", ImGuiTabBarFlags_FittingPolicyScroll))
    {
        if (ImGui::BeginTabItem("Overview"))
        {
            DrawOverview(Surface, SurfaceReady, Placement);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Daylight"))
        {
            DrawDaylight(Surface, SurfaceReady, Placement);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Light & colour"))
        {
            DrawLightAndColour(Surface, SurfaceReady, Placement);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Disc & optics"))
        {
            DrawDiscAndOptics(Surface, SurfaceReady, Placement);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("DIRECTIONAL LIGHT / Z UP");
    ImGui::SameLine();
    ImGui::TextDisabled("Settings stay on Sun record · renderer integration is future work");
}

} // namespace Frontier

#endif // FRONTIER_WITH_INTERFACE_SDK
