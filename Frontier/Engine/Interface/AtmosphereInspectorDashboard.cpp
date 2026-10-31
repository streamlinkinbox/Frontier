/*====================================================================================================================================
                                                ATMOSPHEREINSPECTORDASHBOARD.CPP
====================================================================================================================================*/
// Native C++/ImGui port of VehicleHMI's AtmosphereInspectorDashboard.js and observatory styling.
// Model diagnostics use AtmosphereModel; renderer-only sky pixels remain an explicit empty state.

#include "Engine/Interface/AtmosphereInspectorDashboard.h"

#if defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/ControlKit.h"
#include "Engine/Rendering/AtmosphereModel.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <string>

namespace Frontier
{
namespace
{
constexpr double Pi = 3.14159265358979323846;
constexpr double EarthObserverAltitudeKm = 0.1;
int CapturePage = 0;

ImU32 RGB(unsigned Red, unsigned Green, unsigned Blue, unsigned Alpha = 255u) noexcept
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

double Snap(double Value, double Minimum, double Maximum, double Step) noexcept
{
    Value = std::clamp(Value, Minimum, Maximum);
    if (Step > 0.0)
        Value = Minimum + std::round((Value - Minimum) / Step) * Step;
    return std::clamp(Value, Minimum, Maximum);
}

struct Paint
{
    PixelSpace& Surface;
    bool SurfaceReady = false;
    ImDrawList* Ink = nullptr;
    ImVec2 Origin{};
    float Width = 0.0f;
    ImFont* Typeface = nullptr;

    [[nodiscard]] ImVec2 At(float X, float Y) const noexcept { return ImVec2(Origin.x + X, Origin.y + Y); }

    void Text(float X, float Y, const char* Value, float Size, ImU32 Colour, float Align = 0.0f) const
    {
        if (Value == nullptr || Value[0] == '\0') return;
        const float TextWidth = Typeface->CalcTextSizeA(Size, FLT_MAX, 0.0f, Value).x;
        if (Align == 0.5f) X -= TextWidth * 0.5f;
        else if (Align > 0.5f) X -= TextWidth;
        Ink->AddText(Typeface, Size, At(X, Y), Colour, Value);
    }

    void Wrap(float X, float Y, float WrapWidth, const char* Value, float Size = 10.0f,
              ImU32 Colour = Hex(0x96a18b)) const
    {
        if (Value != nullptr && Value[0] != '\0')
            Ink->AddText(Typeface, Size, At(X, Y), Colour, Value, nullptr, std::max(1.0f, WrapWidth));
    }
};

void DrawCard(const Paint& UI, float X, float Y, float Width, float Height,
              const char* Title, const char* Index = nullptr, ImU32 Fill = Hex(0x232720))
{
    const ImVec2 Minimum = UI.At(X, Y);
    const ImVec2 Maximum = UI.At(X + Width, Y + Height);
    ImGui::SetCursorScreenPos(Minimum);
    ImGui::Dummy(ImVec2(Width, Height));
    UI.Ink->AddRectFilled(Minimum, Maximum, Fill, 22.0f);
    UI.Ink->AddRect(Minimum, Maximum, Hex(0xd6e3a6, 12), 22.0f);
    UI.Text(X + 18.0f, Y + 17.0f, Title, 12.0f, Hex(0xdde5d0));
    if (Index != nullptr)
        UI.Text(X + Width - 18.0f, Y + 18.0f, Index, 8.0f, Hex(0x8c9a7d), 1.0f);
}

bool DrawButton(const Paint& UI, const char* Id, const char* Label, float X, float Y,
                float Width, float Height, bool Selected = false, bool Disabled = false)
{
    const ImVec2 Position = UI.At(X, Y);
    ImGui::SetCursorScreenPos(Position);
    if (Disabled) ImGui::BeginDisabled();
    const bool Pressed = ImGui::InvisibleButton(Id, ImVec2(Width, Height), ImGuiButtonFlags_EnableNav);
    const bool Hovered = ImGui::IsItemHovered();
    const bool Focused = ImGui::IsItemFocused();
    if (Disabled) ImGui::EndDisabled();
    UI.Ink->AddRectFilled(Position, UI.At(X + Width, Y + Height),
                          Selected ? Hex(0x343a2c) : (Hovered ? Hex(0x2a3125) : Hex(0x20251e)), Height * 0.5f);
    UI.Ink->AddRect(Position, UI.At(X + Width, Y + Height),
                    Selected ? Hex(0xd6e3a6, 44) : (Focused ? Hex(0xd6e3a6, 90) : Hex(0xd6e3a6, 16)), Height * 0.5f);
    UI.Text(X + Width * 0.5f, Y + (Height - 10.0f) * 0.5f, Label, 10.0f,
            Disabled ? Hex(0x666b5f) : (Selected ? Hex(0xe0e9c7) : Hex(0xb2bca6)), 0.5f);
    return Pressed && !Disabled;
}

float DrawTabs(const Paint& UI, int& Active, const char* const* Labels, int Count, float Y)
{
    constexpr float Height = 39.0f;
    const float Gap = 3.0f;
    const float InnerWidth = UI.Width - 8.0f - Gap * static_cast<float>(Count - 1);
    const float TabWidth = InnerWidth / static_cast<float>(Count);
    for (int Index = 0; Index < Count; ++Index)
    {
        const float X = 4.0f + static_cast<float>(Index) * (TabWidth + Gap);
        ImGui::PushID(Index);
        const bool Pressed = DrawButton(UI, "##atmosphere-tab", Labels[Index], X, Y, TabWidth, Height, Index == Active);
        if (Pressed) Active = Index;
        if (ImGui::IsItemFocused())
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Home)) Active = 0;
            else if (ImGui::IsKeyPressed(ImGuiKey_End)) Active = Count - 1;
            else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) Active = (Index + Count - 1) % Count;
            else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) Active = (Index + 1) % Count;
        }
        ImGui::PopID();
    }
    return Y + Height + 18.0f;
}

void DrawHeaderPower(const Paint& UI, bool& Enabled)
{
    const float Width = 90.0f;
    const float X = UI.Width - Width;
    const ImVec2 Position = UI.At(X, 21.0f);
    ImGui::SetCursorScreenPos(Position);
    const bool Pressed = ImGui::InvisibleButton("##atmosphere-power", ImVec2(Width, 29.0f), ImGuiButtonFlags_EnableNav);
    const bool Hovered = ImGui::IsItemHovered();
    UI.Ink->AddRectFilled(Position, UI.At(X + Width, 50.0f),
                          Enabled ? Hex(0xd6e3a6, Hovered ? 34 : 16) : Hex(0x20241e), 15.0f);
    UI.Ink->AddRect(Position, UI.At(X + Width, 50.0f), Enabled ? Hex(0xd6e3a6, 40) : Hex(0xd6e3a6, 16), 15.0f);
    UI.Text(X + Width * 0.5f, 29.0f, Enabled ? "● Live" : "○ Off", 10.0f,
            Enabled ? Hex(0xd6e3a6) : Hex(0xa3a99a), 0.5f);
    if (Pressed) Enabled = !Enabled;
}

template <class OnChange>
float DrawSlider(const Paint& UI, const char* Id, const char* Label, double& Value,
                 double Minimum, double Maximum, double Step, const char* Unit, double DisplayScale,
                 float X, float Y, float Width, const OnChange& Changed)
{
    constexpr float PillHeight = 25.0f;
    constexpr float NumberWidth = 42.0f;
    constexpr float TrackOffset = 42.0f;
    constexpr float TrackHeight = 24.0f;
    constexpr float TotalHeight = 71.0f;
    constexpr float CaptionSize = 10.0f;
    constexpr float ValueSize = 13.02f;
    constexpr float UnitSize = 9.114f;

    const bool Percentage = Unit != nullptr && std::strcmp(Unit, "%") == 0;
    const bool HasUnit = Unit != nullptr && Unit[0] != '\0' && std::strcmp(Unit, "-") != 0;
    const double Multiplier = Percentage && std::abs(DisplayScale - 1.0) < 1e-12
                                  ? 100.0
                                  : (std::abs(DisplayScale) > 1e-12 ? DisplayScale : 1.0);
    const double DisplayStep = Step * std::abs(Multiplier);
    const float UnitWidth = HasUnit ? UI.Typeface->CalcTextSizeA(UnitSize, FLT_MAX, 0.0f, Unit).x : 0.0f;
    const float PillWidth = std::min(Width, std::max(63.0f, 12.0f + NumberWidth +
                                                        (HasUnit ? 5.0f + UnitWidth : 0.0f)));
    const ImVec2 PillMinimum = UI.At(X + Width - PillWidth, Y);
    const ImVec2 PillMaximum = UI.At(X + Width, Y + PillHeight);
    const float InputWidth = std::min(NumberWidth, std::max(1.0f, PillWidth - 12.0f -
                                                              (HasUnit ? 5.0f + UnitWidth : 0.0f)));

    UI.Text(X, Y + 7.0f, Label, CaptionSize, Hex(0xa7acb5));
    UI.Ink->AddRectFilled(PillMinimum, PillMaximum, Hex(0x090909), 12.5f);
    UI.Ink->AddRect(PillMinimum, PillMaximum, Hex(0xffffff, 13), 12.5f);

    double DisplayValue = Value * Multiplier;
    char Format[16] = {};
    std::snprintf(Format, sizeof(Format), "%s", (Percentage || DisplayStep >= 1.0) ? "%.0f" : "%.6g");
    ImGui::PushID(Id);
    ImGui::SetCursorScreenPos(UI.At(X + Width - PillWidth + 6.0f, Y));
    ImGui::SetNextItemWidth(InputWidth);
    ImGui::PushFont(UI.Typeface, ValueSize);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 6.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Hex(0xcbd3dc)));
    const bool InputChanged = ImGui::InputDouble("##Position", &DisplayValue, 0.0, 0.0, Format);
    const bool ValueFocused = ImGui::IsItemFocused();
    if (InputChanged && std::isfinite(DisplayValue))
    {
        const double Next = Snap(DisplayValue / Multiplier, Minimum, Maximum, Step);
        if (std::abs(Next - Value) > 1e-12)
        {
            Value = Next;
            Changed(Value);
        }
    }
    else if (ValueFocused)
    {
        const int Direction = static_cast<int>(ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) -
                              static_cast<int>(ImGui::IsKeyPressed(ImGuiKey_DownArrow, true));
        if (Direction != 0)
        {
            const double Next = Snap(Value + Direction * Step, Minimum, Maximum, Step);
            if (std::abs(Next - Value) > 1e-12)
            {
                Value = Next;
                Changed(Value);
            }
        }
    }
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();
    ImGui::PopFont();
    if (HasUnit)
        UI.Ink->AddText(UI.Typeface, UnitSize, UI.At(X + Width - 6.0f - UnitWidth, Y + 8.0f), Hex(0x7f8f9a), Unit);

    const float TrackY = Y + TrackOffset;
    ImGui::SetCursorScreenPos(UI.At(X, TrackY));
    ImGui::InvisibleButton("##slider-track", ImVec2(Width, TrackHeight), ImGuiButtonFlags_EnableNav);
    const bool Hovered = ImGui::IsItemHovered();
    const bool Active = ImGui::IsItemActive();
    const bool TrackFocused = ImGui::IsItemFocused();
    if (UI.SurfaceReady)
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

        float Replacement = static_cast<float>(Value);
        const ControlHit Hit = ControlKit::Slider(
            UI.Surface, Spanning(X, TrackY, Width, TrackHeight), static_cast<float>(Minimum),
            static_cast<float>(Maximum), static_cast<float>(std::clamp(Value, Minimum, Maximum)), Active,
            Pointer, Replacement, ImGui::GetStyle().Alpha, TrackHeight, 22.0f);
        ControlKit::ConfigurePalette(Original);
        if (Hit.Dragging && Pointer.Down)
        {
            const double Next = Snap(static_cast<double>(Replacement), Minimum, Maximum, Step);
            if (std::abs(Next - Value) > 1e-12)
            {
                Value = Next;
                Changed(Value);
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
            const double Next = Snap(Value + Direction * Step, Minimum, Maximum, Step);
            if (std::abs(Next - Value) > 1e-12)
            {
                Value = Next;
                Changed(Value);
            }
        }
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(UI.At(X, Y + TotalHeight));
    return TotalHeight;
}

void DrawCoefficientKpi(const Paint& UI, float X, float Y, float Width,
                        const char* Label, double Value, const char* Unit, bool Accent = false)
{
    const ImU32 Fill = Accent ? Hex(0xd7e3ad) : Hex(0x252b21);
    const ImU32 Ink = Accent ? Hex(0x2d3823) : Hex(0xe0e9ce);
    char Number[32] = {};
    std::snprintf(Number, sizeof(Number), "%.4g", Value);
    UI.Ink->AddRectFilled(UI.At(X, Y), UI.At(X + Width, Y + 72.0f), Fill, 15.0f);
    UI.Text(X + 9.0f, Y + 10.0f, Label, 8.0f, Accent ? Hex(0x687951) : Hex(0x95a383));
    UI.Text(X + 9.0f, Y + 27.0f, Number, 18.0f, Ink);
    UI.Text(X + 9.0f, Y + 52.0f, Unit, 7.5f, Accent ? Hex(0x687951) : Hex(0x89977b));
}

void DrawColumnScan(const Paint& UI, const AtmosphereParameters& Parameters, double& ProbeAltitudeKm,
                    ImGuiStorage* State, ImGuiID ProbeStateId, float Y)
{
    const float CardHeight = 397.0f;
    DrawCard(UI, 0.0f, Y, UI.Width, CardHeight, "Atmospheric column", "01 / DENSITY SCAN", Hex(0x1b231d));
    const float Left = 14.0f;
    const float Top = Y + 49.0f;
    const float CanvasWidth = UI.Width - 28.0f;
    const float CanvasHeight = 244.0f;
    ImGui::SetCursorScreenPos(UI.At(Left, Top));
    ImGui::InvisibleButton("##atmosphere-column-probe", ImVec2(CanvasWidth, CanvasHeight), ImGuiButtonFlags_EnableNav);
    const bool Active = ImGui::IsItemActive();
    const bool Focused = ImGui::IsItemFocused();
    const float Scale = CanvasWidth / 640.0f;
    const float CentreX = Left + CanvasWidth * 0.5f;
    const float CentreY = Top + CanvasHeight * 0.89f;
    const float GroundRadius = CanvasWidth * 0.375f;
    const auto ProbeFromMouse = [&] {
        const ImVec2 Mouse = ImGui::GetIO().MousePos;
        const double Distance = std::hypot(Mouse.x - (UI.Origin.x + CentreX), Mouse.y - (UI.Origin.y + CentreY));
        const double RawKm = (Distance - GroundRadius) / std::max(4.0f * Scale, 1e-6f);
        return std::round(std::clamp(RawKm, 0.0, 30.0) * 2.0) * 0.5;
    };
    if (Active && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        ProbeAltitudeKm = ProbeFromMouse();
        State->SetFloat(ProbeStateId, static_cast<float>(ProbeAltitudeKm));
    }
    else if (Focused)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Home)) ProbeAltitudeKm = 0.0;
        else
        {
            const int Direction = static_cast<int>(ImGui::IsKeyPressed(ImGuiKey_UpArrow, true) ||
                                                   ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) -
                                  static_cast<int>(ImGui::IsKeyPressed(ImGuiKey_DownArrow, true) ||
                                                   ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true));
            ProbeAltitudeKm = std::clamp(ProbeAltitudeKm + Direction * 0.5, 0.0, 30.0);
        }
        State->SetFloat(ProbeStateId, static_cast<float>(ProbeAltitudeKm));
    }

    ImGui::PushClipRect(UI.At(Left, Top), UI.At(Left + CanvasWidth, Top + CanvasHeight), true);
    UI.Ink->AddRectFilled(UI.At(Left, Top), UI.At(Left + CanvasWidth, Top + CanvasHeight), Hex(0x161c17), 17.0f);
    for (double Km = 30.0; Km >= 0.0; Km -= 0.5)
    {
        const double Rayleigh = RayleighDensity(Parameters, Km);
        const double Mie = MieDensity(Parameters, Km);
        const double Ozone = OzoneDensity(Parameters, Km);
        const unsigned Alpha = static_cast<unsigned>(std::clamp(7.0 + Rayleigh * 40.0 + Mie * 22.0 + Ozone * 10.0, 0.0, 90.0));
        const float Radius = GroundRadius + static_cast<float>(Km) * 4.0f * Scale;
        UI.Ink->AddCircle(UI.At(CentreX, CentreY), Radius, Hex(0xadcfaf, Alpha), 64, std::max(0.65f, Scale));
    }
    for (double Km : {0.0, 5.0, 10.0, 20.0, 30.0})
    {
        const float Radius = GroundRadius + static_cast<float>(Km) * 4.0f * Scale;
        UI.Ink->AddCircle(UI.At(CentreX, CentreY), Radius, Hex(0xd9e4c1, 36), 64, 1.0f);
    }
    UI.Ink->AddCircleFilled(UI.At(CentreX, CentreY), GroundRadius, Hex(0x242b23), 64);
    UI.Ink->AddCircle(UI.At(CentreX, CentreY), GroundRadius, Hex(0xb4c599, 86), 64, 1.0f);
    for (float Radius : {GroundRadius * 0.26f, GroundRadius * 0.50f, GroundRadius * 0.75f})
    {
        UI.Ink->AddEllipse(UI.At(CentreX, CentreY), ImVec2(Radius, GroundRadius * 0.42f), Hex(0xacbaa3, 22), 0.0f, 48, 1.0f);
    }

    const float ProbeRadius = GroundRadius + static_cast<float>(ProbeAltitudeKm) * 4.0f * Scale;
    const float ProbeAngle = -1.17f;
    const float ProbeX = CentreX + std::cos(ProbeAngle) * ProbeRadius;
    const float ProbeY = CentreY + std::sin(ProbeAngle) * ProbeRadius;
    const ImVec2 ProbePoint = UI.At(ProbeX, ProbeY);
    UI.Ink->AddLine(ProbePoint, UI.At(Left + CanvasWidth - 48.0f, ProbeY - 26.0f), Hex(0xd4e99b, 138), 1.0f);
    UI.Ink->AddCircle(ProbePoint, 13.0f, Hex(0xd4e99b, 120), 32, 1.2f);
    UI.Ink->AddCircleFilled(ProbePoint, 4.0f, Hex(0xe3efc5), 20);
    UI.Ink->AddLine(ImVec2(ProbePoint.x - 18.0f, ProbePoint.y), ImVec2(ProbePoint.x + 18.0f, ProbePoint.y), Hex(0xd4e99b, 70));
    UI.Ink->AddLine(ImVec2(ProbePoint.x, ProbePoint.y - 18.0f), ImVec2(ProbePoint.x, ProbePoint.y + 18.0f), Hex(0xd4e99b, 70));
    UI.Text(Left + 12.0f, Top + CanvasHeight - 19.0f, "GROUND / Z = 0", 8.0f, Hex(0xb1baa5));

    const double MolecularDensity = RayleighDensity(Parameters, ProbeAltitudeKm) * 100.0;
    const double AerosolDensity = MieDensity(Parameters, ProbeAltitudeKm) * 100.0;
    char Relative[32] = {}, Aerosol[32] = {}, Probe[32] = {};
    std::snprintf(Relative, sizeof(Relative), "%.1f", MolecularDensity);
    std::snprintf(Aerosol, sizeof(Aerosol), "%.2f%%", AerosolDensity);
    std::snprintf(Probe, sizeof(Probe), "↗ %.1f km probe", ProbeAltitudeKm);
    UI.Text(Left + 14.0f, Top + 14.0f, "Relative molecular density", 8.5f, Hex(0xaab69c));
    UI.Text(Left + 12.0f, Top + 28.0f, Relative, 34.0f, Hex(0xe9efd9));
    UI.Text(Left + 14.0f, Top + 66.0f, "% of sea level", 8.0f, Hex(0x92a17f));
    const float TagWidth = 96.0f;
    const float TagX = Left + CanvasWidth - TagWidth - 10.0f;
    UI.Ink->AddRectFilled(UI.At(TagX, Top + 13.0f), UI.At(TagX + TagWidth, Top + 36.0f), Hex(0x2e3a26, 150), 12.0f);
    UI.Ink->AddRect(UI.At(TagX, Top + 13.0f), UI.At(TagX + TagWidth, Top + 36.0f), Hex(0xd6e3a6, 38), 12.0f);
    UI.Text(TagX + TagWidth * 0.5f, Top + 20.0f, Probe, 8.0f, Hex(0xd8e5bd), 0.5f);
    const float TileX = Left + CanvasWidth - 116.0f;
    const float TileY = Top + 153.0f;
    UI.Ink->AddRectFilled(UI.At(TileX, TileY), UI.At(TileX + 105.0f, TileY + 53.0f), Hex(0x252d22, 232), 12.0f);
    UI.Ink->AddRect(UI.At(TileX, TileY), UI.At(TileX + 105.0f, TileY + 53.0f), Hex(0xd6e3a6, 18), 12.0f);
    UI.Text(TileX + 10.0f, TileY + 9.0f, "Aerosol density", 7.5f, Hex(0xa8b398));
    UI.Text(TileX + 10.0f, TileY + 26.0f, Aerosol, 15.0f, Hex(0xdce8bf));
    ImGui::PopClipRect();

    auto Changed = [&ProbeAltitudeKm, State, ProbeStateId](double Value) {
        ProbeAltitudeKm = Value;
        State->SetFloat(ProbeStateId, static_cast<float>(ProbeAltitudeKm));
    };
    DrawSlider(UI, "atmosphere-probe-altitude", "Probe altitude", ProbeAltitudeKm, 0.0, 30.0, 0.5, "km", 1.0,
               18.0f, Y + 309.0f, UI.Width - 36.0f, Changed);
    UI.Wrap(18.0f, Y + 382.0f, UI.Width - 36.0f,
            "Exaggerated shell diagram · relative density, not a rendered planet.", 8.5f, Hex(0x89977c));
}

void DrawCoefficientKpis(const Paint& UI, const AtmosphereParameters& Parameters, float Y)
{
    const float Gap = 6.0f;
    const float TileWidth = (UI.Width - Gap * 2.0f) / 3.0f;
    const ImVec2 Start = UI.At(0.0f, Y);
    ImGui::SetCursorScreenPos(Start);
    ImGui::Dummy(ImVec2(UI.Width, 76.0f));
    DrawCoefficientKpi(UI, 0.0f, Y, TileWidth, "RAYLEIGH R", Parameters.RayleighScattering.R, "1/km");
    DrawCoefficientKpi(UI, TileWidth + Gap, Y, TileWidth, "MIE SCATTERING", Parameters.MieScattering, "1/km");
    DrawCoefficientKpi(UI, 2.0f * (TileWidth + Gap), Y, TileWidth, "OZONE R", Parameters.OzoneAbsorption.R, "1/km", true);
}

void DrawSkyCaptureEmptyState(const Paint& UI, float Y)
{
    constexpr float Height = 189.0f;
    DrawCard(UI, 0.0f, Y, UI.Width, Height, "Sky-view capture", "RENDERER / SCATTERING", Hex(0x1e241b));
    const float Left = 18.0f;
    const float Top = Y + 54.0f;
    const float Width = UI.Width - 36.0f;
    UI.Ink->AddRectFilled(UI.At(Left, Top), UI.At(Left + Width, Top + 92.0f), Hex(0x151a15), 13.0f);
    UI.Ink->AddRect(UI.At(Left, Top), UI.At(Left + Width, Top + 92.0f), Hex(0xd6e3a6, 12), 13.0f);
    UI.Text(Left + 14.0f, Top + 17.0f, "No baked image · analytic sky", 10.0f, Hex(0xc1d3a3));
    UI.Wrap(Left + 14.0f, Top + 42.0f, Width - 28.0f,
            "Bake the atmosphere to see actual renderer pixels here. No substitute illustration.",
            8.5f, Hex(0x89977c));
}

void DrawAtmosphereOverview(const Paint& UI, AtmosphereParameters& Parameters, float Y,
                            ImGuiStorage* State, ImGuiID ProbeStateId)
{
    double ProbeAltitudeKm = std::clamp(static_cast<double>(State->GetFloat(ProbeStateId, 8.0f)), 0.0, 30.0);
    DrawColumnScan(UI, Parameters, ProbeAltitudeKm, State, ProbeStateId, Y);
    const float KpiY = Y + 411.0f;
    DrawCoefficientKpis(UI, Parameters, KpiY);
    DrawSkyCaptureEmptyState(UI, KpiY + 87.0f);
}

void DrawScaleHeightChart(const Paint& UI, AtmosphereParameters& Parameters, ImGuiStorage* State,
                          ImGuiID SelectedId, float Y)
{
    constexpr float Height = 558.0f;
    DrawCard(UI, 0.0f, Y, UI.Width, Height, "How the air thins", "SCALE-HEIGHT COMPARATOR", Hex(0x20251e));
    const float Left = 18.0f;
    const float Top = Y + 57.0f;
    const float Width = UI.Width - 36.0f;
    const float ChartHeight = 246.0f;
    ImGui::SetCursorScreenPos(UI.At(Left, Top));
    ImGui::InvisibleButton("##scale-height-chart", ImVec2(Width, ChartHeight), ImGuiButtonFlags_EnableNav);
    const bool Active = ImGui::IsItemActive();
    const bool Focused = ImGui::IsItemFocused();
    int Selected = std::clamp(State->GetInt(SelectedId, 0), 0, 1);
    const float CentreLeft = Left + Width * 0.39f;
    const float CentreRight = Left + Width * 0.61f;
    const float Spread = Width * 0.34f;
    const float Bottom = Top + ChartHeight - 34.0f;
    const float GraphTop = Top + 18.0f;
    const float GraphHeight = Bottom - GraphTop;

    if (Active && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        const ImVec2 Mouse = ImGui::GetIO().MousePos;
        Selected = Mouse.x < UI.Origin.x + Left + Width * 0.5f ? 0 : 1;
        const double Altitude = std::clamp((Bottom - (Mouse.y - UI.Origin.y)) / GraphHeight * 20.0, 0.0, 20.0);
        if (Selected == 0)
            Parameters.RayleighScaleHeightKm = std::clamp(std::round(Altitude * 10.0) / 10.0, 1.0, 20.0);
        else
            Parameters.MieScaleHeightKm = std::clamp(std::round(Altitude * 10.0) / 10.0, 0.1, 5.0);
        State->SetInt(SelectedId, Selected);
    }
    else if (Focused)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) Selected = 0;
        else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) Selected = 1;
        else if (ImGui::IsKeyPressed(ImGuiKey_Home))
        {
            if (Selected == 0) Parameters.RayleighScaleHeightKm = 8.0;
            else Parameters.MieScaleHeightKm = 1.2;
        }
        else
        {
            const int Direction = static_cast<int>(ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) -
                                  static_cast<int>(ImGui::IsKeyPressed(ImGuiKey_DownArrow, true));
            if (Selected == 0)
                Parameters.RayleighScaleHeightKm = std::clamp(Parameters.RayleighScaleHeightKm + Direction * 0.1, 1.0, 20.0);
            else
                Parameters.MieScaleHeightKm = std::clamp(Parameters.MieScaleHeightKm + Direction * 0.1, 0.1, 5.0);
        }
        State->SetInt(SelectedId, Selected);
    }

    for (int Index = 0; Index <= 4; ++Index)
    {
        const float GridY = Bottom - static_cast<float>(Index) * GraphHeight / 4.0f;
        const float AltitudeKm = static_cast<float>(Index) * 5.0f;
        UI.Ink->AddLine(UI.At(Left + 4.0f, GridY), UI.At(Left + Width * 0.43f, GridY), Hex(0xb0c6aa, 20), 1.0f);
        UI.Ink->AddLine(UI.At(Left + Width * 0.57f, GridY), UI.At(Left + Width - 4.0f, GridY), Hex(0xb0c6aa, 20), 1.0f);
        char Label[12] = {};
        std::snprintf(Label, sizeof(Label), "%.0f", AltitudeKm);
        UI.Text(Left + Width * 0.5f, GridY - 4.0f, Label, 8.0f, Hex(0xa9b59b), 0.5f);
    }
    UI.Text(Left + Width * 0.5f, Top + 2.0f, "km", 8.0f, Hex(0x7c8c70), 0.5f);

    const auto DrawRibbon = [&](bool Rayleigh) {
        constexpr int Samples = 81;
        std::array<ImVec2, Samples> Points{};
        const float Centre = Rayleigh ? CentreLeft : CentreRight;
        const float Sign = Rayleigh ? -1.0f : 1.0f;
        for (int Index = 0; Index < Samples; ++Index)
        {
            const double AltitudeKm = static_cast<double>(Index) / static_cast<double>(Samples - 1) * 20.0;
            const double Density = Rayleigh ? RayleighDensity(Parameters, AltitudeKm)
                                            : MieDensity(Parameters, AltitudeKm);
            const float Py = Bottom - static_cast<float>(AltitudeKm / 20.0) * GraphHeight;
            Points[static_cast<size_t>(Index)] = UI.At(Centre + Sign * static_cast<float>(Density) * Spread, Py);
        }
        const ImU32 Colour = Rayleigh ? Hex(0xb8d3bd) : Hex(0xdecfa8);
        UI.Ink->AddPolyline(Points.data(), Samples, Colour, ImDrawFlags_None, 2.2f);
        UI.Ink->AddLine(UI.At(Centre, GraphTop), UI.At(Centre, Bottom), Hex(0xb0c6aa, 52), 1.0f);
        const double HeightKm = Rayleigh ? Parameters.RayleighScaleHeightKm : Parameters.MieScaleHeightKm;
        const float MarkerY = Bottom - static_cast<float>(HeightKm / 20.0) * GraphHeight;
        const float MarkerX = Centre + Sign * std::exp(-1.0f) * Spread;
        UI.Ink->AddLine(UI.At(Centre, MarkerY), UI.At(MarkerX, MarkerY), Colour, 1.0f);
        UI.Ink->AddCircle(UI.At(MarkerX, MarkerY), 7.0f, Colour, 24, 1.2f);
        UI.Ink->AddCircleFilled(UI.At(MarkerX, MarkerY), 2.3f, Colour, 16);
        char Value[24] = {};
        std::snprintf(Value, sizeof(Value), "%.1f km", HeightKm);
        UI.Text(Rayleigh ? Left + 4.0f : Left + Width - 4.0f, Y + 349.0f, Value, 15.0f, Colour, Rayleigh ? 0.0f : 1.0f);
    };
    DrawRibbon(true);
    DrawRibbon(false);
    UI.Text(Left + 5.0f, Y + 374.0f, "1/e HEIGHT", 7.5f, Hex(0x7f9071));

    auto Changed = [](double) {};
    DrawSlider(UI, "rayleigh-scale-height", "Rayleigh scale height", Parameters.RayleighScaleHeightKm,
               1.0, 20.0, 0.1, "km", 1.0, 18.0f, Y + 407.0f, UI.Width - 36.0f, Changed);
    DrawSlider(UI, "mie-scale-height", "Mie scale height", Parameters.MieScaleHeightKm,
               0.1, 5.0, 0.05, "km", 1.0, 18.0f, Y + 478.0f, UI.Width - 36.0f, Changed);
}

void DrawConstituentSliders(const Paint& UI, AtmosphereParameters& Parameters, float Y)
{
    constexpr float Height = 1005.0f;
    DrawCard(UI, 0.0f, Y, UI.Width, Height, "Constituents", "SEA-LEVEL COEFFICIENTS", Hex(0x20251e));
    float RowY = Y + 50.0f;
    const float X = 18.0f;
    const float Width = UI.Width - 36.0f;
    auto Changed = [](double) {};
    const auto Subheading = [&](const char* Label, const char* Detail) {
        UI.Text(X, RowY + 4.0f, Label, 10.0f, Hex(0xb8c9a7));
        UI.Text(X + Width, RowY + 5.0f, Detail, 7.5f, Hex(0x7f8e73), 1.0f);
        RowY += 24.0f;
    };

    Subheading("Rayleigh / molecules", "1/km");
    DrawSlider(UI, "rayleigh-r", "Rayleigh R", Parameters.RayleighScattering.R, 0.0, 0.05, 0.0001, "1/km", 1.0, X, RowY, Width, Changed); RowY += 71.0f;
    DrawSlider(UI, "rayleigh-g", "Rayleigh G", Parameters.RayleighScattering.G, 0.0, 0.05, 0.0001, "1/km", 1.0, X, RowY, Width, Changed); RowY += 71.0f;
    DrawSlider(UI, "rayleigh-b", "Rayleigh B", Parameters.RayleighScattering.B, 0.0, 0.05, 0.0001, "1/km", 1.0, X, RowY, Width, Changed); RowY += 79.0f;

    Subheading("Mie / aerosols", "1/km");
    DrawSlider(UI, "mie-scattering", "Scattering", Parameters.MieScattering, 0.0, 0.02, 0.0001, "1/km", 1.0, X, RowY, Width, Changed); RowY += 71.0f;
    DrawSlider(UI, "mie-absorption", "Absorption", Parameters.MieAbsorption, 0.0, 0.02, 0.0001, "1/km", 1.0, X, RowY, Width, Changed); RowY += 79.0f;

    Subheading("Ozone layer", "1/km · tent profile");
    DrawSlider(UI, "ozone-r", "Ozone R", Parameters.OzoneAbsorption.R, 0.0, 0.01, 0.00001, "1/km", 1.0, X, RowY, Width, Changed); RowY += 71.0f;
    DrawSlider(UI, "ozone-g", "Ozone G", Parameters.OzoneAbsorption.G, 0.0, 0.01, 0.00001, "1/km", 1.0, X, RowY, Width, Changed); RowY += 71.0f;
    DrawSlider(UI, "ozone-b", "Ozone B", Parameters.OzoneAbsorption.B, 0.0, 0.01, 0.00001, "1/km", 1.0, X, RowY, Width, Changed); RowY += 71.0f;
    DrawSlider(UI, "ozone-peak", "Peak altitude", Parameters.OzonePeakKm, 10.0, 50.0, 0.1, "km", 1.0, X, RowY, Width, Changed); RowY += 71.0f;
    DrawSlider(UI, "ozone-width", "Layer width", Parameters.OzoneWidthKm, 1.0, 30.0, 0.1, "km", 1.0, X, RowY, Width, Changed);
}

void DrawPhaseShape(const Paint& UI, AtmosphereParameters& Parameters, float Y)
{
    constexpr float Height = 455.0f;
    DrawCard(UI, 0.0f, Y, UI.Width, Height, "Where light scatters", "DIRECTIONAL RESPONSE", Hex(0x20251e));
    const float CentreX = UI.Width * 0.5f;
    const float CentreY = Y + 180.0f;
    const float Radius = std::min(UI.Width * 0.32f, 118.0f);
    for (float GridRadius : {Radius * 0.34f, Radius * 0.67f, Radius})
        UI.Ink->AddCircle(UI.At(CentreX, CentreY), GridRadius, Hex(0xaebc98, 26), 48, 1.0f);
    UI.Ink->AddLine(UI.At(18.0f, CentreY), UI.At(UI.Width - 18.0f, CentreY), Hex(0xaebc98, 46), 1.0f);
    constexpr int Samples = 361;
    std::array<double, Samples> Phase{};
    double Peak = 0.0;
    for (int Index = 0; Index < Samples; ++Index)
    {
        const double Angle = static_cast<double>(Index) / static_cast<double>(Samples - 1) * 2.0 * Pi;
        Phase[static_cast<size_t>(Index)] = CornetteShanksPhase(std::cos(Angle), Parameters.MiePhaseG);
        Peak = std::max(Peak, Phase[static_cast<size_t>(Index)]);
    }
    std::array<ImVec2, Samples> Points{};
    for (int Index = 0; Index < Samples; ++Index)
    {
        const double Angle = static_cast<double>(Index) / static_cast<double>(Samples - 1) * 2.0 * Pi;
        const float Relative = static_cast<float>(Peak > 0.0 ? std::sqrt(Phase[static_cast<size_t>(Index)] / Peak) : 0.0);
        Points[static_cast<size_t>(Index)] = UI.At(CentreX + std::cos(Angle) * Radius * Relative,
                                                   CentreY + std::sin(Angle) * Radius * Relative);
    }
    UI.Ink->AddPolyline(Points.data(), Samples, Hex(0xd6dcb0), ImDrawFlags_None, 2.2f);
    UI.Ink->AddCircleFilled(UI.At(CentreX, CentreY), 4.0f, Hex(0xe0e9c8), 16);
    UI.Ink->AddLine(UI.At(20.0f, CentreY), UI.At(CentreX - Radius - 12.0f, CentreY), Hex(0xbac5a7), 1.6f);
    UI.Ink->AddLine(UI.At(CentreX + Radius + 12.0f, CentreY), UI.At(UI.Width - 20.0f, CentreY), Hex(0xbac5a7), 1.6f);
    UI.Text(18.0f, Y + 333.0f, "INCIDENT LIGHT →", 8.0f, Hex(0x9aa98c));
    UI.Text(UI.Width - 18.0f, Y + 333.0f, "FORWARD →", 8.0f, Hex(0x9aa98c), 1.0f);
    UI.Wrap(18.0f, Y + 350.0f, UI.Width - 36.0f,
            "Normalised Cornette–Shanks phase reference · relative shape, not radiance.", 8.0f, Hex(0x89977c));

    auto Changed = [](double) {};
    DrawSlider(UI, "mie-phase-g", "Mie anisotropy", Parameters.MiePhaseG,
               -0.95, 0.95, 0.01, "g", 1.0, 18.0f, Y + 379.0f, UI.Width - 36.0f, Changed);
}

SpectrumRgb ZenithTransmittance(const AtmosphereParameters& Parameters) noexcept
{
    return TransmittanceToTop(Parameters, Parameters.GroundRadiusKm + EarthObserverAltitudeKm, 1.0, 64);
}

double NeutralLightTransmission(const SpectrumRgb& RGBValue) noexcept
{
    return RGBValue.R * 0.2126 + RGBValue.G * 0.7152 + RGBValue.B * 0.0722;
}

void DrawTransmissionDial(const Paint& UI, SpectrumRgb Transmission, bool Enabled, float Y)
{
    constexpr float Height = 435.0f;
    DrawCard(UI, 0.0f, Y, UI.Width, Height, "Light through air", "ZENITH / TRANSMISSION", Hex(0x20251e));
    UI.Text(18.0f, Y + 49.0f, Enabled ? "Zenith reference · observer at 100 m" : "Atmosphere off · reference only",
            8.5f, Hex(0x8d9c7d));
    const ImVec2 Centre(UI.Width * 0.5f, Y + 260.0f);
    const float MaximumRadius = std::min(UI.Width * 0.36f, 136.0f);
    const std::array<double, 3> Channels = {Transmission.R, Transmission.G, Transmission.B};
    const std::array<ImU32, 3> Colours = {Hex(0xd2a49b), Hex(0xb6cc9c), Hex(0xa4c6d7)};
    for (int Index = 0; Index < 3; ++Index)
    {
        const float Radius = MaximumRadius - static_cast<float>(Index) * 25.0f;
        std::array<ImVec2, 97> Track{}, Arc{};
        for (int Sample = 0; Sample < 97; ++Sample)
        {
            const double Angle = Pi + static_cast<double>(Sample) / 96.0 * Pi;
            Track[static_cast<size_t>(Sample)] = UI.At(Centre.x + std::cos(Angle) * Radius,
                                                        Centre.y + std::sin(Angle) * Radius);
            const double ArcAngle = Pi + static_cast<double>(Sample) / 96.0 * Pi * std::clamp(Channels[static_cast<size_t>(Index)], 0.0, 1.0);
            Arc[static_cast<size_t>(Sample)] = UI.At(Centre.x + std::cos(ArcAngle) * Radius,
                                                      Centre.y + std::sin(ArcAngle) * Radius);
        }
        UI.Ink->AddPolyline(Track.data(), static_cast<int>(Track.size()), Hex(0xc5cfb9, 18), ImDrawFlags_None, 7.0f);
        const int VisibleSamples = std::clamp(static_cast<int>(std::round(Channels[static_cast<size_t>(Index)] * 96.0)) + 1, 1, 97);
        if (VisibleSamples > 1)
            UI.Ink->AddPolyline(Arc.data(), VisibleSamples, Colours[static_cast<size_t>(Index)], ImDrawFlags_None, 7.0f);
        const int EndIndex = VisibleSamples - 1;
        UI.Ink->AddCircleFilled(Arc[static_cast<size_t>(EndIndex)], 3.5f, Colours[static_cast<size_t>(Index)], 16);
    }

    char Neutral[24] = {}, Red[24] = {}, Green[24] = {}, Blue[24] = {};
    std::snprintf(Neutral, sizeof(Neutral), "%.1f%%", NeutralLightTransmission(Transmission) * 100.0);
    std::snprintf(Red, sizeof(Red), "%.1f%%", Transmission.R * 100.0);
    std::snprintf(Green, sizeof(Green), "%.1f%%", Transmission.G * 100.0);
    std::snprintf(Blue, sizeof(Blue), "%.1f%%", Transmission.B * 100.0);
    UI.Text(Centre.x, Y + 190.0f, Neutral, 28.0f, Hex(0xe7eddc), 0.5f);
    UI.Text(Centre.x, Y + 222.0f, "neutral-light transmission", 8.0f, Hex(0x9eac90), 0.5f);
    UI.Text(30.0f, Y + 290.0f, "0%", 8.0f, Hex(0x7f8c73));
    UI.Text(UI.Width - 30.0f, Y + 290.0f, "100%", 8.0f, Hex(0x7f8c73), 1.0f);

    const float Gap = 0.0f;
    const float CellWidth = UI.Width / 3.0f;
    const float RowY = Y + 333.0f;
    UI.Ink->AddLine(UI.At(18.0f, RowY), UI.At(UI.Width - 18.0f, RowY), Hex(0xd6e3a6, 18), 1.0f);
    const char* Labels[] = {"RED", "GREEN", "BLUE"};
    const char* Values[] = {Red, Green, Blue};
    for (int Index = 0; Index < 3; ++Index)
    {
        const float X = Gap + CellWidth * static_cast<float>(Index);
        UI.Text(X + CellWidth * 0.5f, RowY + 13.0f, Labels[Index], 7.5f, Hex(0x95a383), 0.5f);
        UI.Text(X + CellWidth * 0.5f, RowY + 28.0f, Values[Index], 14.0f, Colours[static_cast<size_t>(Index)], 0.5f);
    }
    UI.Wrap(18.0f, Y + 385.0f, UI.Width - 36.0f,
            "Model transmittance at zenith; no live Sun direction or renderer sky lookup is connected here.",
            8.0f, Hex(0x89977c));
}

void DrawGroundAlbedo(const Paint& UI, AtmosphereParameters& Parameters, float Y)
{
    constexpr float Height = 151.0f;
    DrawCard(UI, 0.0f, Y, UI.Width, Height, "Ground reflectance", "SURFACE RESPONSE", Hex(0x20251e));
    auto Changed = [](double) {};
    DrawSlider(UI, "ground-albedo", "Ground albedo", Parameters.GroundAlbedo, 0.0, 1.0, 0.01, "-", 1.0,
               18.0f, Y + 56.0f, UI.Width - 36.0f, Changed);
}

void DrawBakingPage(const Paint& UI, float Y)
{
    constexpr float Height = 356.0f;
    DrawCard(UI, 0.0f, Y, UI.Width, Height, "Atmosphere bake", "RENDERER / SKY LOOKUP", Hex(0x20251e));
    const float StatusY = Y + 56.0f;
    UI.Ink->AddRectFilled(UI.At(18.0f, StatusY), UI.At(UI.Width - 18.0f, StatusY + 38.0f), Hex(0x171d14), 12.0f);
    UI.Text(30.0f, StatusY + 14.0f, "No baked image · analytic sky", 9.0f, Hex(0xb8c9a7));
    DrawButton(UI, "##bake-unavailable", "Bake atmosphere", 18.0f, Y + 108.0f,
               UI.Width - 36.0f, 34.0f, false, true);

    const float PreviewY = Y + 159.0f;
    UI.Ink->AddRectFilled(UI.At(18.0f, PreviewY), UI.At(UI.Width - 18.0f, PreviewY + 139.0f), Hex(0x111611), 15.0f);
    UI.Ink->AddRect(UI.At(18.0f, PreviewY), UI.At(UI.Width - 18.0f, PreviewY + 139.0f), Hex(0xd6e3a6, 15), 15.0f);
    UI.Wrap(34.0f, PreviewY + 53.0f, UI.Width - 68.0f,
            "Bake the atmosphere to see actual renderer pixels here. No substitute illustration.",
            9.0f, Hex(0x89977c));
    UI.Wrap(18.0f, Y + 312.0f, UI.Width - 36.0f,
            "AtmosphereModel provides analytic transmittance and scattering; no GPU bake request or HDR image is connected to this exhibit.",
            8.0f, Hex(0x89977c));
}

void DrawFooterAndSpacing(const Paint& UI, float Y)
{
    UI.Ink->AddLine(UI.At(0.0f, Y), UI.At(UI.Width, Y), Hex(0xd6e3a6, 28), 1.0f);
    UI.Text(0.0f, Y + 13.0f, "ATMOSPHERIC SCATTERING", 8.0f, Hex(0x7c8e69));
    UI.Text(UI.Width, Y + 13.0f, "Settings apply live ↗", 8.0f, Hex(0x7c8e69), 1.0f);
    ImGui::SetCursorScreenPos(UI.At(0.0f, Y + 30.0f));
    ImGui::Dummy(ImVec2(UI.Width, 24.0f));
}

} // namespace

#if defined(FRONTIER_WITH_WORKSPACE_CAPTURE)
void ConfigureAtmosphereInspectorPageForCapture(int Page) noexcept
{
    CapturePage = std::clamp(Page, 0, 3);
}
#endif

void ConstructAtmosphereInspectorDashboard(AtmosphereParameters& Parameters, bool& Enabled)
{
    ImGui::PushID(&Parameters);
    ImDrawList* Ink = ImGui::GetWindowDrawList();
    const ImVec2 WindowPosition = ImGui::GetWindowPos();
    const ImVec2 WindowSize = ImGui::GetWindowSize();
    Ink->AddRectFilled(WindowPosition, ImVec2(WindowPosition.x + WindowSize.x, WindowPosition.y + WindowSize.y), Hex(0x151815));
    const ImVec2 DisplaySize = ImGui::GetIO().DisplaySize;
    PixelSpace Surface;
    const bool SurfaceReady = Surface.Begin(SurfaceLayer::Window, DisplaySize.x, DisplaySize.y);
    const ImVec2 HostCursor = ImGui::GetCursorScreenPos();
    const float HostWidth = ImGui::GetContentRegionAvail().x;
    const ImVec2 Origin(HostCursor.x + 18.0f, HostCursor.y + 17.0f);
    const float Width = std::max(120.0f, HostWidth - 36.0f);
    Paint UI{Surface, SurfaceReady, Ink, Origin, Width, ImGui::GetFont()};

    for (float DotY = 5.0f; DotY < 850.0f; DotY += 13.0f)
        for (float DotX = 5.0f; DotX < Width; DotX += 13.0f)
            Ink->AddCircleFilled(UI.At(DotX, DotY), 0.65f, Hex(0xb2c594, 18), 4);

    UI.Text(0.0f, 0.0f, "ENVIRONMENT  /  03", 9.0f, Hex(0x8c9783));
    UI.Text(0.0f, 19.0f, "Atmosphere", 31.0f, Hex(0xe8eae0));
    DrawHeaderPower(UI, Enabled);
    UI.Text(0.0f, 58.0f, "Scattering, density & the visible sky.", 10.0f, Hex(0x9aa38f));

    static const char* Tabs[] = {"Overview", "Medium", "Light response", "Baking"};
    const ImGuiID PageId = ImGui::GetID("##atmosphere-dashboard-page");
    const ImGuiID ProbeStateId = ImGui::GetID("##atmosphere-probe-km");
    const ImGuiID SelectedStateId = ImGui::GetID("##atmosphere-selected-scale-height");
    ImGuiStorage* State = ImGui::GetStateStorage();
    int ActivePage = std::clamp(State->GetInt(PageId, CapturePage), 0, 3);
    const float ContentY = DrawTabs(UI, ActivePage, Tabs, 4, 82.0f);
    State->SetInt(PageId, ActivePage);

    float FooterY = ContentY;
    if (ActivePage == 0)
    {
        DrawAtmosphereOverview(UI, Parameters, ContentY, State, ProbeStateId);
        FooterY = ContentY + 710.0f;
    }
    else if (ActivePage == 1)
    {
        DrawScaleHeightChart(UI, Parameters, State, SelectedStateId, ContentY);
        DrawConstituentSliders(UI, Parameters, ContentY + 572.0f);
        DrawPhaseShape(UI, Parameters, ContentY + 1591.0f);
        FooterY = ContentY + 2064.0f;
    }
    else if (ActivePage == 2)
    {
        const SpectrumRgb Transmission = ZenithTransmittance(Parameters);
        DrawTransmissionDial(UI, Transmission, Enabled, ContentY);
        DrawGroundAlbedo(UI, Parameters, ContentY + 455.0f);
        FooterY = ContentY + 624.0f;
    }
    else
    {
        DrawBakingPage(UI, ContentY);
        FooterY = ContentY + 375.0f;
    }
    DrawFooterAndSpacing(UI, FooterY);
    ImGui::PopID();
}

} // namespace Frontier

#endif // FRONTIER_WITH_INTERFACE_SDK
