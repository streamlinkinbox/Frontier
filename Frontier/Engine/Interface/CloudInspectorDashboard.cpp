/*====================================================================================================================================
                                                    CLOUDINSPECTORDASHBOARD.CPP
====================================================================================================================================*/
// Native C++/ImGui port of VehicleHMI's CloudInspectorDashboard.js and CloudInspector.css.
// The renderer-only controls stay visibly unavailable until a cloud renderer supplies them; no preview data is fabricated.

#include "Engine/Interface/CloudInspectorDashboard.h"

#if defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/ControlKit.h"
#include "Engine/Rendering/CloudField.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cfloat>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>

namespace Frontier
{
namespace
{
constexpr int AtlasColumns = 56;
constexpr int AtlasRows = 26;
constexpr double AtlasHalfWidthM = 6000.0;
constexpr double AtlasHalfHeightM = 3000.0;
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
              ImU32 Colour = Hex(0x8f9a88)) const
    {
        if (Value != nullptr && Value[0] != '\0')
            Ink->AddText(Typeface, Size, At(X, Y), Colour, Value, nullptr, std::max(1.0f, WrapWidth));
    }
};

void DrawCard(const Paint& UI, float X, float Y, float Width, float Height,
              const char* Title, const char* Index, ImU32 Fill = Hex(0x1d231f))
{
    const ImVec2 Minimum = UI.At(X, Y);
    const ImVec2 Maximum = UI.At(X + Width, Y + Height);
    ImGui::SetCursorScreenPos(Minimum);
    ImGui::Dummy(ImVec2(Width, Height));
    UI.Ink->AddRectFilled(Minimum, Maximum, Fill, 22.0f);
    UI.Ink->AddRect(Minimum, Maximum, Hex(0xffffff, 10), 22.0f);
    UI.Text(X + 18.0f, Y + 17.0f, Title, 12.0f, Hex(0xdce1d4));
    UI.Text(X + Width - 18.0f, Y + 18.0f, Index, 8.0f, Hex(0x7f8d78), 1.0f);
}

bool DrawButton(const Paint& UI, const char* Id, const char* Label, float X, float Y,
                float Width, float Height, bool Selected = false)
{
    const ImVec2 Position = UI.At(X, Y);
    ImGui::SetCursorScreenPos(Position);
    const bool Pressed = ImGui::InvisibleButton(Id, ImVec2(Width, Height), ImGuiButtonFlags_EnableNav);
    const bool Hovered = ImGui::IsItemHovered();
    const bool Focused = ImGui::IsItemFocused();
    const ImU32 Fill = Selected ? Hex(0x30382d) : (Hovered ? Hex(0x2a3328) : Hex(0x20251f));
    UI.Ink->AddRectFilled(Position, UI.At(X + Width, Y + Height), Fill, Height * 0.5f);
    UI.Ink->AddRect(Position, UI.At(X + Width, Y + Height),
                    Selected ? Hex(0xd5e6a5, 34) : (Focused ? Hex(0xd5e6a5, 90) : Hex(0xffffff, 10)), Height * 0.5f);
    UI.Text(X + Width * 0.5f, Y + (Height - 10.0f) * 0.5f, Label, 10.0f,
            Selected ? Hex(0xe0e9cd) : Hex(0xa9b29f), 0.5f);
    return Pressed;
}

float DrawTabs(const Paint& UI, int& Active, const char* const* Labels, int Count, float Y)
{
    constexpr float Height = 39.0f;
    UI.Ink->AddRectFilled(UI.At(0.0f, Y), UI.At(UI.Width, Y + Height), Hex(0x1c201c), Height * 0.5f);
    UI.Ink->AddRect(UI.At(0.0f, Y), UI.At(UI.Width, Y + Height), Hex(0xffffff, 10), Height * 0.5f);
    const float Gap = 3.0f;
    const float InnerWidth = UI.Width - 8.0f - Gap * static_cast<float>(Count - 1);
    const float TabWidth = InnerWidth / static_cast<float>(Count);
    for (int Index = 0; Index < Count; ++Index)
    {
        const float X = 4.0f + static_cast<float>(Index) * (TabWidth + Gap);
        ImGui::PushID(Index);
        const bool Pressed = DrawButton(UI, "##cloud-tab", Labels[Index], X, Y + 4.0f, TabWidth, Height - 8.0f,
                                        Index == Active);
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

bool DrawPower(const Paint& UI, bool& Enabled, float Y)
{
    const float Width = 90.0f;
    const float X = UI.Width - Width;
    const ImVec2 Position = UI.At(X, Y);
    ImGui::SetCursorScreenPos(Position);
    const bool Pressed = ImGui::InvisibleButton("##cloud-power", ImVec2(Width, 29.0f), ImGuiButtonFlags_EnableNav);
    const bool Hovered = ImGui::IsItemHovered();
    UI.Ink->AddRectFilled(Position, UI.At(X + Width, Y + 29.0f),
                          Enabled ? Hex(0xd5e6a5, Hovered ? 34 : 20) : Hex(0x22251f), 15.0f);
    UI.Ink->AddRect(Position, UI.At(X + Width, Y + 29.0f),
                    Enabled ? Hex(0x566248, 120) : Hex(0x3c4137), 15.0f);
    UI.Text(X + Width * 0.5f, Y + 9.0f, Enabled ? "● Live" : "○ Off", 10.0f,
            Enabled ? Hex(0xd5e6a5) : Hex(0xa3a69d), 0.5f);
    if (Pressed) Enabled = !Enabled;
    return Pressed;
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
        if (std::abs(Next - Value) > 1e-10)
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
            if (std::abs(Next - Value) > 1e-10)
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
            if (std::abs(Next - Value) > 1e-10)
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
            if (std::abs(Next - Value) > 1e-10)
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

struct CloudAtlasCache
{
    const CloudParameters* Owner = nullptr;
    std::array<double, 8> Key{};
    uint64_t Seed = 0;
    bool Valid = false;
    std::array<float, AtlasColumns * AtlasRows> Density{};
};

void RefreshCloudAtlas(CloudAtlasCache& Cache, const CloudParameters& Parameters)
{
    const std::array<double, 8> Key = {Parameters.Coverage, Parameters.DensityScale, Parameters.BaseHeightM,
        Parameters.TopHeightM, Parameters.WindTimeS, Parameters.WindXMps, Parameters.WindYMps,
        Parameters.UniformDensityOverride};
    if (Cache.Valid && Cache.Owner == &Parameters && Cache.Key == Key && Cache.Seed == Parameters.Seed)
        return;

    Cache.Owner = &Parameters;
    Cache.Key = Key;
    Cache.Seed = Parameters.Seed;
    Cache.Valid = true;
    const double Altitude = Parameters.BaseHeightM + (Parameters.TopHeightM - Parameters.BaseHeightM) * 0.56;
    for (int Y = 0; Y < AtlasRows; ++Y)
    {
        const double WorldY = ((static_cast<double>(Y) + 0.5) / AtlasRows * 2.0 - 1.0) * AtlasHalfHeightM;
        for (int X = 0; X < AtlasColumns; ++X)
        {
            const double WorldX = ((static_cast<double>(X) + 0.5) / AtlasColumns * 2.0 - 1.0) * AtlasHalfWidthM;
            Cache.Density[static_cast<size_t>(Y * AtlasColumns + X)] =
                static_cast<float>(CloudDensity(Parameters, {WorldX, WorldY, Altitude}));
        }
    }
}

void DrawDensityAtlas(const Paint& UI, CloudParameters& Parameters, float Y)
{
    constexpr float Height = 342.0f;
    DrawCard(UI, 0.0f, Y, UI.Width, Height, "Density atlas", "01 / FIELD", Hex(0x181e1b));
    static CloudAtlasCache Atlas;
    RefreshCloudAtlas(Atlas, Parameters);

    char Coverage[32] = {};
    std::snprintf(Coverage, sizeof(Coverage), "%.0f", std::clamp(Parameters.Coverage, 0.0, 1.0) * 100.0);
    UI.Text(18.0f, Y + 51.0f, "Coverage setting", 9.0f, Hex(0x929b8b));
    UI.Text(17.0f, Y + 65.0f, Coverage, 43.0f, Hex(0xe4eadc));
    UI.Text(84.0f, Y + 84.0f, "%", 17.0f, Hex(0x909985));
    const float TagX = UI.Width - 118.0f;
    UI.Ink->AddRectFilled(UI.At(TagX, Y + 65.0f), UI.At(TagX + 100.0f, Y + 90.0f), Hex(0xd5e6a5, 10), 13.0f);
    UI.Ink->AddRect(UI.At(TagX, Y + 65.0f), UI.At(TagX + 100.0f, Y + 90.0f), Hex(0xd5e6a5, 26), 13.0f);
    UI.Text(TagX + 50.0f, Y + 73.0f, "SEEDED FIELD", 8.0f, Hex(0xccd9b7), 0.5f);

    const float Left = 18.0f;
    const float Top = Y + 119.0f;
    const float CanvasWidth = UI.Width - 36.0f;
    const float CanvasHeight = 170.0f;
    UI.Ink->AddRectFilled(UI.At(Left, Top), UI.At(Left + CanvasWidth, Top + CanvasHeight), Hex(0x0e1412), 13.0f);
    const float CellWidth = CanvasWidth / static_cast<float>(AtlasColumns);
    const float CellHeight = CanvasHeight / static_cast<float>(AtlasRows);
    for (int Row = 0; Row < AtlasRows; ++Row)
    {
        for (int Column = 0; Column < AtlasColumns; ++Column)
        {
            const float Density = Atlas.Density[static_cast<size_t>(Row * AtlasColumns + Column)];
            if (Density <= 0.0f) continue;
            const unsigned Alpha = static_cast<unsigned>(std::clamp(18.0f + Density * 135.0f, 0.0f, 185.0f));
            UI.Ink->AddRectFilled(UI.At(Left + Column * CellWidth, Top + Row * CellHeight),
                                  UI.At(Left + (Column + 1) * CellWidth + 0.35f,
                                        Top + (Row + 1) * CellHeight + 0.35f),
                                  RGB(199, 221, 150, Alpha));
        }
    }
    for (int Column = 1; Column < 7; ++Column)
    {
        const float GridX = Left + CanvasWidth * static_cast<float>(Column) / 7.0f;
        UI.Ink->AddLine(UI.At(GridX, Top + 6.0f), UI.At(GridX, Top + CanvasHeight - 6.0f), Hex(0xbbd7c5, 20), 1.0f);
    }
    for (int Row = 1; Row < 4; ++Row)
    {
        const float GridY = Top + CanvasHeight * static_cast<float>(Row) / 4.0f;
        UI.Ink->AddLine(UI.At(Left + 6.0f, GridY), UI.At(Left + CanvasWidth - 6.0f, GridY), Hex(0xbbd7c5, 20), 1.0f);
    }
    UI.Text(Left + 8.0f, Top + CanvasHeight - 19.0f, "XY / WORLD FIELD", 8.0f, Hex(0xd8e4db));
    UI.Text(Left + CanvasWidth - 8.0f, Top + CanvasHeight - 19.0f, "DENSITY →", 8.0f, Hex(0xd8e4db), 1.0f);
    UI.Ink->AddLine(UI.At(18.0f, Y + 310.0f), UI.At(UI.Width - 18.0f, Y + 310.0f), Hex(0xffffff, 12), 1.0f);
    UI.Text(18.0f, Y + 321.0f, "Static density diagnostic · not a lit sky render", 8.0f, Hex(0x8e9a8a));
}

void DrawKpi(const Paint& UI, float X, float Y, float Width, const char* Label,
             const char* Value, const char* Unit, bool Accent = false)
{
    const ImU32 Fill = Accent ? Hex(0xd5e6a5) : Hex(0x20251f);
    const ImU32 Ink = Accent ? Hex(0x222c19) : Hex(0xe0e9d6);
    UI.Ink->AddRectFilled(UI.At(X, Y), UI.At(X + Width, Y + 78.0f), Fill, 16.0f);
    UI.Ink->AddRect(UI.At(X, Y), UI.At(X + Width, Y + 78.0f), Hex(0xffffff, 8), 16.0f);
    UI.Text(X + 10.0f, Y + 11.0f, Label, 8.0f, Accent ? Hex(0x576443) : Hex(0x929b8b));
    UI.Text(X + 10.0f, Y + 29.0f, Value, 21.0f, Ink);
    UI.Text(X + 10.0f, Y + 57.0f, Unit, 7.5f, Accent ? Hex(0x576443) : Hex(0x909985));
}

void DrawKpis(const Paint& UI, const CloudParameters& Parameters, float Y)
{
    const float Gap = 7.0f;
    const float TileWidth = (UI.Width - Gap * 2.0f) / 3.0f;
    char Base[24] = {}, Top[24] = {};
    std::snprintf(Base, sizeof(Base), "%.2f", Parameters.BaseHeightM / 1000.0);
    std::snprintf(Top, sizeof(Top), "%.2f", Parameters.TopHeightM / 1000.0);
    const ImVec2 Start = UI.At(0.0f, Y);
    ImGui::SetCursorScreenPos(Start);
    ImGui::Dummy(ImVec2(UI.Width, 82.0f));
    DrawKpi(UI, 0.0f, Y, TileWidth, "LAYER BASE", Base, "km");
    DrawKpi(UI, TileWidth + Gap, Y, TileWidth, "EFFECTIVE TOP", Top, "km");
    DrawKpi(UI, 2.0f * (TileWidth + Gap), Y, TileWidth, "GPU CLOUD PASS", "—", "not connected", true);
}

void DrawMixEditor(const Paint& UI, CloudParameters& Parameters, float Y)
{
    constexpr float Height = 438.0f;
    DrawCard(UI, 0.0f, Y, UI.Width, Height, "Shape the field", "02 / MIX");
    UI.Wrap(18.0f, Y + 47.0f, UI.Width - 36.0f,
            "Coverage × density · drag the point or use arrow keys", 9.0f, Hex(0x8f9a88));

    const float Left = 18.0f;
    const float Top = Y + 78.0f;
    const float PlotWidth = UI.Width - 36.0f;
    const float PlotHeight = 154.0f;
    ImGui::SetCursorScreenPos(UI.At(Left, Top));
    ImGui::InvisibleButton("##cloud-mix-plot", ImVec2(PlotWidth, PlotHeight), ImGuiButtonFlags_EnableNav);
    const bool Active = ImGui::IsItemActive();
    const bool Focused = ImGui::IsItemFocused();
    if (Active && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        const ImVec2 Mouse = ImGui::GetIO().MousePos;
        Parameters.Coverage = std::clamp((Mouse.x - (UI.Origin.x + Left)) / PlotWidth, 0.0f, 1.0f);
        Parameters.DensityScale = std::clamp(4.0 * (1.0 - (Mouse.y - (UI.Origin.y + Top)) / PlotHeight), 0.0, 4.0);
    }
    else if (Focused)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Home))
        {
            Parameters.Coverage = 0.55;
            Parameters.DensityScale = 1.0;
        }
        else
        {
            const int Horizontal = static_cast<int>(ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) -
                                   static_cast<int>(ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true));
            const int Vertical = static_cast<int>(ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) -
                                 static_cast<int>(ImGui::IsKeyPressed(ImGuiKey_DownArrow, true));
            Parameters.Coverage = std::clamp(Parameters.Coverage + Horizontal * 0.01, 0.0, 1.0);
            Parameters.DensityScale = std::clamp(Parameters.DensityScale + Vertical * 0.05, 0.0, 4.0);
        }
    }

    const ImVec2 PlotMin = UI.At(Left, Top);
    const ImVec2 PlotMax = UI.At(Left + PlotWidth, Top + PlotHeight);
    UI.Ink->AddRectFilled(PlotMin, PlotMax, Hex(0x151a16), 12.0f);
    for (int Row = 0; Row < 5; ++Row)
    {
        const float GridY = Top + 16.0f + static_cast<float>(Row) * 28.0f;
        UI.Ink->AddLine(UI.At(Left + 10.0f, GridY), UI.At(Left + PlotWidth - 10.0f, GridY), Hex(0xaab6a1, 28), 1.0f);
    }
    for (int Column = 0; Column < 7; ++Column)
    {
        const float GridX = Left + 10.0f + static_cast<float>(Column) * (PlotWidth - 20.0f) / 6.0f;
        UI.Ink->AddLine(UI.At(GridX, Top + 12.0f), UI.At(GridX, Top + PlotHeight - 18.0f), Hex(0xaab6a1, 20), 1.0f);
    }
    const float PointX = Left + Parameters.Coverage * PlotWidth;
    const float PointY = Top + PlotHeight - Parameters.DensityScale / 4.0f * PlotHeight;
    UI.Ink->AddRectFilled(UI.At(Left + 10.0f, PointY), UI.At(PointX, Top + PlotHeight - 16.0f), Hex(0xc7dd96, 24), 3.0f);
    UI.Ink->AddLine(UI.At(Left + 10.0f, PointY), UI.At(PointX, PointY), Hex(0xcedeaa, 128), 1.0f);
    UI.Ink->AddLine(UI.At(PointX, PointY), UI.At(PointX, Top + PlotHeight - 16.0f), Hex(0xcedeaa, 128), 1.0f);
    UI.Ink->AddCircle(UI.At(PointX, PointY), 13.0f, Hex(0xcce1a2, 140), 32, 1.4f);
    UI.Ink->AddCircleFilled(UI.At(PointX, PointY), 5.0f, Hex(0xd6e7ae), 24);
    UI.Text(Left + 9.0f, Top + PlotHeight - 12.0f, "CLEAR", 8.0f, Hex(0x8d9a8c));
    UI.Text(Left + PlotWidth - 9.0f, Top + PlotHeight - 12.0f, "OVERCAST", 8.0f, Hex(0x8d9a8c), 1.0f);
    UI.Text(Left + 9.0f, Top + 8.0f, "DENSER ↑", 8.0f, Hex(0x8d9a8c));

    auto Changed = [](double) {};
    DrawSlider(UI, "cloud-coverage", "Coverage", Parameters.Coverage, 0.0, 1.0, 0.01, "%", 100.0,
               18.0f, Y + 253.0f, UI.Width - 36.0f, Changed);
    DrawSlider(UI, "cloud-density-scale", "Density multiplier", Parameters.DensityScale, 0.0, 4.0, 0.05, "×", 1.0,
               18.0f, Y + 324.0f, UI.Width - 36.0f, Changed);
}

void DrawCloudProfile(const Paint& UI, CloudParameters& Parameters, float Y)
{
    constexpr float Height = 427.0f;
    DrawCard(UI, 0.0f, Y, UI.Width, Height, "Layer anatomy", "03 / PROFILE");
    const float Left = 54.0f;
    const float Top = Y + 67.0f;
    const float PlotWidth = UI.Width - 78.0f;
    const float PlotHeight = 151.0f;
    for (int Line = 0; Line < 5; ++Line)
    {
        const float GridY = Top + static_cast<float>(Line) * PlotHeight / 4.0f;
        UI.Ink->AddLine(UI.At(Left, GridY), UI.At(Left + PlotWidth, GridY), Hex(0xd3dfc2, 32), 1.0f);
    }
    UI.Text(Left, Top + PlotHeight + 19.0f, "NORMALISED DENSITY PROFILE · X/Y = 0", 8.0f, Hex(0x96a18f));
    char TopLabel[32] = {};
    std::snprintf(TopLabel, sizeof(TopLabel), "%.2f km", Parameters.TopHeightM / 1000.0);
    UI.Text(UI.Width - 18.0f, Top + 2.0f, TopLabel, 9.0f, Hex(0xd4e5a8), 1.0f);
    UI.Text(Left, Top + PlotHeight - 13.0f, "BASE", 7.0f, Hex(0x89978b));

    std::array<double, 65> Samples{};
    double Peak = 0.0;
    const double Span = std::max(1.0, Parameters.TopHeightM - Parameters.BaseHeightM);
    for (size_t Index = 0; Index < Samples.size(); ++Index)
    {
        const double Fraction = 1.0 - static_cast<double>(Index) / static_cast<double>(Samples.size() - 1);
        const double Altitude = Parameters.BaseHeightM + Span * Fraction;
        Samples[Index] = CloudDensity(Parameters, {0.0, 0.0, Altitude});
        Peak = std::max(Peak, Samples[Index]);
    }
    std::array<ImVec2, 65> Points{};
    for (size_t Index = 0; Index < Points.size(); ++Index)
    {
        const float Fraction = static_cast<float>(Peak > 1e-12 ? Samples[Index] / Peak : 0.0);
        Points[Index] = UI.At(Left + Fraction * PlotWidth,
                              Top + static_cast<float>(Index) / static_cast<float>(Points.size() - 1) * PlotHeight);
    }
    UI.Ink->AddPolyline(Points.data(), static_cast<int>(Points.size()), Hex(0xd4e5a8), ImDrawFlags_None, 2.2f);

    auto Changed = [](double) {};
    const float ControlX = 18.0f;
    const float ControlWidth = UI.Width - 36.0f;
    const double MaxBase = std::max(0.0, std::min(20000.0, Parameters.TopHeightM - 100.0));
    DrawSlider(UI, "cloud-base-height", "Base altitude", Parameters.BaseHeightM, 0.0, MaxBase, 10.0, "m", 1.0,
               ControlX, Y + 254.0f, ControlWidth, Changed);
    double Thickness = std::max(100.0, Parameters.TopHeightM - Parameters.BaseHeightM);
    const double MaxThickness = std::max(100.0, 30000.0 - Parameters.BaseHeightM);
    const auto SetThickness = [&Parameters](double Value) {
        Parameters.TopHeightM = std::clamp(Parameters.BaseHeightM + Value, Parameters.BaseHeightM + 100.0, 30000.0);
    };
    DrawSlider(UI, "cloud-layer-thickness", "Layer thickness", Thickness, 100.0, MaxThickness, 10.0, "m", 1.0,
               ControlX, Y + 325.0f, ControlWidth, SetThickness);
}

void DrawCloudLightResponse(const Paint& UI, CloudParameters& Parameters, float Y)
{
    constexpr float Height = 229.0f;
    DrawCard(UI, 0.0f, Y, UI.Width, Height, "Light response", "04 / SCATTER");
    auto Changed = [](double) {};
    DrawSlider(UI, "cloud-phase-g", "Forward scattering", Parameters.PhaseG, -0.8, 0.8, 0.01, "g", 1.0,
               18.0f, Y + 53.0f, UI.Width - 36.0f, Changed);
    DrawSlider(UI, "cloud-extinction", "Extinction", Parameters.ExtinctionPerM, 0.0, 0.05, 0.0005, "1/m", 1.0,
               18.0f, Y + 124.0f, UI.Width - 36.0f, Changed);
}

void DrawUnavailableCard(const Paint& UI, float Y, const char* Title, const char* Index,
                         const char* Status, const char* Description, float Height = 198.0f)
{
    DrawCard(UI, 0.0f, Y, UI.Width, Height, Title, Index, Hex(0x1b201b));
    UI.Text(18.0f, Y + 62.0f, Status, 14.0f, Hex(0xd5e6a5));
    UI.Wrap(18.0f, Y + 91.0f, UI.Width - 36.0f, Description, 10.0f, Hex(0x929d8b));
}

void DrawWindPage(const Paint& UI, CloudParameters& Parameters, float Y)
{
    constexpr float Height = 330.0f;
    DrawCard(UI, 0.0f, Y, UI.Width, Height, "Wind advection", "07 / FORCEFIELD");
    auto Changed = [](double) {};
    DrawSlider(UI, "cloud-wind-x", "Wind X", Parameters.WindXMps, -40.0, 40.0, 0.1, "m/s", 1.0,
               18.0f, Y + 54.0f, UI.Width - 36.0f, Changed);
    DrawSlider(UI, "cloud-wind-y", "Wind Y", Parameters.WindYMps, -40.0, 40.0, 0.1, "m/s", 1.0,
               18.0f, Y + 125.0f, UI.Width - 36.0f, Changed);
    DrawSlider(UI, "cloud-wind-time", "Wind time", Parameters.WindTimeS, 0.0, 3600.0, 1.0, "s", 1.0,
               18.0f, Y + 196.0f, UI.Width - 36.0f, Changed);
    UI.Wrap(18.0f, Y + 274.0f, UI.Width - 36.0f,
            "This record stores its own X/Y advection vector. Shared weather controls belong to the separate Wind record.",
            9.0f, Hex(0x8f9a88));
}

void DrawFooter(const Paint& UI, float Y)
{
    UI.Ink->AddLine(UI.At(0.0f, Y), UI.At(UI.Width, Y), Hex(0xd5e6a5, 24), 1.0f);
    UI.Text(0.0f, Y + 13.0f, "VOLUMETRIC CLOUD FIELD", 8.0f, Hex(0x87947f));
    UI.Text(UI.Width, Y + 13.0f, "Settings apply live ↗", 8.0f, Hex(0x87947f), 1.0f);
    ImGui::SetCursorScreenPos(UI.At(0.0f, Y + 30.0f));
    ImGui::Dummy(ImVec2(UI.Width, 24.0f));
}

} // namespace

#if defined(FRONTIER_WITH_WORKSPACE_CAPTURE)
void ConfigureCloudInspectorPageForCapture(int Page) noexcept
{
    CapturePage = std::clamp(Page, 0, 3);
}
#endif

void ConstructCloudInspectorDashboard(CloudParameters& Parameters, bool& Enabled)
{
    ImGui::PushID(&Parameters);
    ImDrawList* Ink = ImGui::GetWindowDrawList();
    const ImVec2 WindowPosition = ImGui::GetWindowPos();
    const ImVec2 WindowSize = ImGui::GetWindowSize();
    Ink->AddRectFilled(WindowPosition, ImVec2(WindowPosition.x + WindowSize.x, WindowPosition.y + WindowSize.y), Hex(0x111412));

    PixelSpace Surface;
    const ImVec2 DisplaySize = ImGui::GetIO().DisplaySize;
    const bool SurfaceReady = Surface.Begin(SurfaceLayer::Window, DisplaySize.x, DisplaySize.y);
    const ImVec2 HostCursor = ImGui::GetCursorScreenPos();
    const float HostWidth = ImGui::GetContentRegionAvail().x;
    const ImVec2 Origin(HostCursor.x + 18.0f, HostCursor.y + 18.0f);
    const float Width = std::max(120.0f, HostWidth - 36.0f);
    Paint UI{Surface, SurfaceReady, Ink, Origin, Width, ImGui::GetFont()};

    UI.Text(0.0f, 0.0f, "ENVIRONMENT  /  01", 9.0f, Hex(0x8e988c));
    UI.Text(0.0f, 22.0f, "Clouds", 34.0f, Hex(0xe7eadf));
    DrawPower(UI, Enabled, 25.0f);
    UI.Text(0.0f, 64.0f, "Density field / shape & illumination.", 10.0f, Hex(0x929b8c));

    static const char* Tabs[] = {"Overview", "Formation", "Rendering", "Weather"};
    const ImGuiID PageId = ImGui::GetID("##cloud-dashboard-page");
    ImGuiStorage* State = ImGui::GetStateStorage();
    int ActivePage = std::clamp(State->GetInt(PageId, CapturePage), 0, 3);
    const float ContentY = DrawTabs(UI, ActivePage, Tabs, 4, 88.0f);
    State->SetInt(PageId, ActivePage);

    float FooterY = ContentY;
    if (ActivePage == 0)
    {
        DrawDensityAtlas(UI, Parameters, ContentY);
        const float KpiY = ContentY + 354.0f;
        DrawKpis(UI, Parameters, KpiY);
        DrawMixEditor(UI, Parameters, KpiY + 94.0f);
        const float LinkY = KpiY + 94.0f + 450.0f;
        if (DrawButton(UI, "##cloud-formation-link", "Formation & altitude   ↗", 0.0f, LinkY, Width, 38.0f))
            ActivePage = 1;
        FooterY = LinkY + 55.0f;
    }
    else if (ActivePage == 1)
    {
        DrawCloudProfile(UI, Parameters, ContentY);
        DrawCloudLightResponse(UI, Parameters, ContentY + 439.0f);
        FooterY = ContentY + 686.0f;
    }
    else if (ActivePage == 2)
    {
        DrawUnavailableCard(UI, ContentY, "Render footprint", "05 / BUDGET", "Renderer controls unavailable",
            "This native record has no render-resolution or march-budget fields, and no GPU cloud pass is connected to this exhibit.");
        DrawUnavailableCard(UI, ContentY + 211.0f, "Ground shadow field", "06 / OCCLUSION", "No projected shadow field",
            "The current CloudField model exposes density and self-shadowed transport only; it does not provide a separate ground-shadow field.");
        FooterY = ContentY + 432.0f;
    }
    else
    {
        DrawWindPage(UI, Parameters, ContentY);
        FooterY = ContentY + 349.0f;
    }
    State->SetInt(PageId, ActivePage);
    DrawFooter(UI, FooterY);
    ImGui::PopID();
}

} // namespace Frontier

#endif // FRONTIER_WITH_INTERFACE_SDK
