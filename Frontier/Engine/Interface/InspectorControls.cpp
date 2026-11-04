/*==========================================================================================================================================
                                                       INSPECTORCONTROLS.CPP
==========================================================================================================================================*/
// 🧩 General-purpose Editor surface palette, typeface resolver, vector glyphs, and modular Inspector controls.

#include "Engine/Interface/InspectorControls.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/ControlKit.h"
#include "Engine/Interface/TypefaceRegistry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL UTILITIES
//------------------------------------------------------------------------------------------------------------------------

namespace
{

constexpr float π = 3.14159265f;

ImU32 QuadToColor32(ColorQuad Quad) noexcept
{
    return ImGui::GetColorU32(ImVec4(Quad.Red, Quad.Green, Quad.Blue, Quad.Alpha));
}

ImU32 BlendColor32(ImU32 Base, ImU32 Target, float Ratio) noexcept
{
    const float Clamped = std::clamp(Ratio, 0.0f, 1.0f);
    const ImVec4 A      = ImGui::ColorConvertU32ToFloat4(Base);
    const ImVec4 B      = ImGui::ColorConvertU32ToFloat4(Target);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(
        A.x + (B.x - A.x) * Clamped,
        A.y + (B.y - A.y) * Clamped,
        A.z + (B.z - A.z) * Clamped,
        A.w + (B.w - A.w) * Clamped));
}

int HexNibble(char Character) noexcept
{
    if (Character >= '0' && Character <= '9') return Character - '0';
    if (Character >= 'a' && Character <= 'f') return 10 + (Character - 'a');
    if (Character >= 'A' && Character <= 'F') return 10 + (Character - 'A');
    return -1;
}

float MeasureTextWidth(ImFont* Font, float Size, const char* Text) noexcept
{
    if (Font == nullptr || Text == nullptr || Text[0] == '\0')
    {
        return 0.0f;
    }
    return Font->CalcTextSizeA(Size, 1.0e9f, 0.0f, Text).x;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    SPECTRUM & TYPEFACES
//------------------------------------------------------------------------------------------------------------------------

ImU32 ParseHexSpectrum(std::string_view Hex, ImU32 Fallback) noexcept
{
    if (!Hex.empty() && Hex.front() == '#')
    {
        Hex.remove_prefix(1);
    }
    if (Hex.size() != 6 && Hex.size() != 8)
    {
        return Fallback;
    }
    uint32_t Channels[4] = {0, 0, 0, 255};
    const size_t Count   = Hex.size() / 2;
    for (size_t Index = 0; Index < Count; ++Index)
    {
        const int High = HexNibble(Hex[Index * 2]);
        const int Low  = HexNibble(Hex[Index * 2 + 1]);
        if (High < 0 || Low < 0)
        {
            return Fallback;
        }
        Channels[Index] = static_cast<uint32_t>((High << 4) | Low);
    }
    return IM_COL32(Channels[0], Channels[1], Channels[2], Channels[3]);
}

ImU32 ModulateSpectrumAlpha(ImU32 Colour, float AlphaMultiplier) noexcept
{
    ImVec4 Unpacked = ImGui::ColorConvertU32ToFloat4(Colour);
    Unpacked.w      = std::clamp(Unpacked.w * AlphaMultiplier, 0.0f, 1.0f);
    return ImGui::ColorConvertFloat4ToU32(Unpacked);
}

EditorTypefaces EditorTypefaces::Resolve() noexcept
{
    ImFont* Fallback = ImGui::GetFont();
    EditorTypefaces Result{Fallback, Fallback, Fallback, Fallback, Fallback, Fallback};

    if (const TypefaceRegistry* Active = TypefaceRegistry::QueryCurrent())
    {
        int PrimaryIdx = Active->ResolveFamilyIndex("DM Sans");
        if (PrimaryIdx < 0)
        {
            PrimaryIdx = Active->ResolveFamilyIndex("Inter");
        }
        if (PrimaryIdx >= 0)
        {
            const auto SelectWeight = [&](FontWeightCategory Weight) -> ImFont* {
                void* Handle = Active->QueryHandle(static_cast<uint32_t>(PrimaryIdx), Weight);
                return Handle != nullptr ? static_cast<ImFont*>(Handle) : Fallback;
            };
            Result.Light    = SelectWeight(FontWeightCategory::Light);
            Result.Regular  = SelectWeight(FontWeightCategory::Regular);
            Result.Medium   = SelectWeight(FontWeightCategory::Medium);
            Result.SemiBold = SelectWeight(FontWeightCategory::SemiBold);
            Result.Bold     = SelectWeight(FontWeightCategory::Bold);
        }

        const int MonoIdx = Active->ResolveFamilyIndex("JetBrains Mono");
        if (MonoIdx >= 0)
        {
            if (void* MonoFace = Active->QueryHandle(static_cast<uint32_t>(MonoIdx), FontWeightCategory::Regular))
            {
                Result.Monospace = static_cast<ImFont*>(MonoFace);
            }
        }
    }
    return Result;
}

InspectorPalette InspectorPalette::Resolve() noexcept
{
    const ControlKitPalette& Upstream = ControlKit::Palette();
    const float PanelLuma = 0.2126f * Upstream.Panel.Red + 0.7152f * Upstream.Panel.Green + 0.0722f * Upstream.Panel.Blue;

    InspectorPalette Out{};
    Out.DarkMode    = PanelLuma < 0.45f;
    Out.ThemeAccent = QuadToColor32(Upstream.Accent);
    Out.LiveGreen   = IM_COL32(0x3E, 0xC4, 0x6D, 0xFF);
    Out.WarmAmber   = IM_COL32(0xE8, 0xA8, 0x4A, 0xFF);
    Out.DangerRed   = IM_COL32(0xD9, 0x56, 0x56, 0xFF);

    if (Out.DarkMode && PanelLuma < 0.16f)
    {
        const ImU32 BasePanel = BlendColor32(IM_COL32(0x16, 0x16, 0x16, 0xFF), QuadToColor32(Upstream.Panel), 0.32f);
        Out.ViewportBg        = IM_COL32(0x0C, 0x0C, 0x0C, 0xFF);
        Out.ColumnBg          = BasePanel;
        Out.StatusBarBg       = BlendColor32(IM_COL32(0x12, 0x12, 0x12, 0xFF), BasePanel, 0.30f);
        Out.ViewportBarBg     = BlendColor32(IM_COL32(0x15, 0x15, 0x15, 0xFF), BasePanel, 0.30f);
        Out.InspectorTopBg    = BlendColor32(IM_COL32(0x1B, 0x1B, 0x1B, 0xFF), QuadToColor32(Upstream.Card), 0.28f);
        Out.InspectorBottomBg = BlendColor32(IM_COL32(0x14, 0x14, 0x14, 0xFF), BasePanel, 0.28f);
        Out.EdgeBorder        = IM_COL32(0x22, 0x22, 0x22, 0xFF);
        Out.CountCardBg       = IM_COL32(0x1E, 0x1E, 0x1E, 0xFF);
        Out.CountCardBorder   = IM_COL32(0x28, 0x28, 0x28, 0xFF);
        Out.SearchBg          = IM_COL32(0x1B, 0x1B, 0x1B, 0xFF);
        Out.SearchBorder      = IM_COL32(0x24, 0x24, 0x24, 0xFF);
        Out.RowHoverBg        = IM_COL32(0x1E, 0x1E, 0x1E, 0xFF);
        Out.RowSelectedTop    = IM_COL32(0x33, 0x33, 0x33, 0xFF);
        Out.RowSelectedBottom = IM_COL32(0x29, 0x29, 0x29, 0xFF);
        Out.RowSelectedBorder = IM_COL32(0x43, 0x43, 0x43, 0xFF);
        Out.InsetFieldBg      = IM_COL32(0x12, 0x12, 0x12, 0xFF);
        Out.InsetFieldBorder  = IM_COL32(0x28, 0x28, 0x28, 0xFF);
        Out.SliderTrackBg     = IM_COL32(0x25, 0x25, 0x25, 0xFF);
        Out.SliderFillBg      = BlendColor32(IM_COL32(0x78, 0x78, 0x78, 0xFF), Out.ThemeAccent, 0.35f);
        Out.SliderThumbBg     = IM_COL32(0xE8, 0xE8, 0xE8, 0xFF);
        Out.ToggleOffBg       = IM_COL32(0x27, 0x27, 0x27, 0xFF);
        Out.ToggleOnBg        = BlendColor32(IM_COL32(0xDC, 0xDC, 0xDC, 0xFF), Out.ThemeAccent, 0.25f);
        Out.SegmentedBg       = IM_COL32(0x13, 0x13, 0x13, 0xFF);
        Out.SegmentedActiveBg = IM_COL32(0x2C, 0x2C, 0x2C, 0xFF);
        Out.TextPrimary       = IM_COL32(0xEF, 0xEF, 0xEF, 0xFF);
        Out.TextSecondary     = IM_COL32(0xBC, 0xBC, 0xBC, 0xFF);
        Out.TextMuted         = IM_COL32(0x78, 0x78, 0x78, 0xFF);
        Out.TextFaint         = IM_COL32(0x5A, 0x5A, 0x5A, 0xFF);
    }
    else
    {
        const ImU32 Panel     = QuadToColor32(Upstream.Panel);
        const ImU32 Card      = QuadToColor32(Upstream.Card);
        const ImU32 Inset     = QuadToColor32(Upstream.Inset);
        const ImU32 Raised    = QuadToColor32(Upstream.Raised);
        const ImU32 Selected  = QuadToColor32(Upstream.Selected);
        const ImU32 Stroke    = QuadToColor32(Upstream.Stroke);
        const ImU32 Strong    = QuadToColor32(Upstream.StrokeStrong);
        Out.ViewportBg        = Inset;
        Out.ColumnBg          = Panel;
        Out.StatusBarBg       = BlendColor32(Panel, Inset, 0.45f);
        Out.ViewportBarBg     = Panel;
        Out.InspectorTopBg    = Card;
        Out.InspectorBottomBg = Panel;
        Out.EdgeBorder        = Stroke;
        Out.CountCardBg       = Card;
        Out.CountCardBorder   = Stroke;
        Out.SearchBg          = Inset;
        Out.SearchBorder      = Stroke;
        Out.RowHoverBg        = Raised;
        Out.RowSelectedTop    = Selected;
        Out.RowSelectedBottom = BlendColor32(Selected, Raised, 0.4f);
        Out.RowSelectedBorder = Strong;
        Out.InsetFieldBg      = Inset;
        Out.InsetFieldBorder  = Stroke;
        Out.SliderTrackBg     = Stroke;
        Out.SliderFillBg      = Out.ThemeAccent;
        Out.SliderThumbBg     = QuadToColor32(Upstream.Text);
        Out.ToggleOffBg       = Raised;
        Out.ToggleOnBg        = Out.ThemeAccent;
        Out.SegmentedBg       = Inset;
        Out.SegmentedActiveBg = Selected;
        Out.TextPrimary       = QuadToColor32(Upstream.Text);
        Out.TextSecondary     = BlendColor32(QuadToColor32(Upstream.Text), QuadToColor32(Upstream.TextDim), 0.35f);
        Out.TextMuted         = QuadToColor32(Upstream.TextDim);
        Out.TextFaint         = BlendColor32(QuadToColor32(Upstream.TextDim), Panel, 0.38f);
    }
    return Out;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    VECTOR GLYPHS
//------------------------------------------------------------------------------------------------------------------------

void ConstructEditorGlyph(ImDrawList*      DrawList,
                          ImVec2           Centre,
                          float            HalfExtent,
                          ImU32            Colour,
                          std::string_view Identifier,
                          float            StrokeThickness) noexcept
{
    const float R = HalfExtent;
    if (Identifier == "Sun")
    {
        DrawList->AddCircle(Centre, R * 0.46f, Colour, 16, StrokeThickness);
        for (int Ray = 0; Ray < 8; ++Ray)
        {
            const float θ = static_cast<float>(Ray) * (π * 0.25f);
            DrawList->AddLine(ImVec2(Centre.x + std::cos(θ) * R * 0.68f, Centre.y + std::sin(θ) * R * 0.68f),
                              ImVec2(Centre.x + std::cos(θ) * R, Centre.y + std::sin(θ) * R), Colour, StrokeThickness);
        }
    }
    else if (Identifier == "Moon")
    {
        DrawList->AddCircle(Centre, R * 0.78f, Colour, 18, StrokeThickness);
        DrawList->AddCircleFilled(ImVec2(Centre.x + R * 0.30f, Centre.y - R * 0.22f), R * 0.56f, IM_COL32(22, 22, 22, 230), 14);
    }
    else if (Identifier == "Planet")
    {
        DrawList->AddCircle(Centre, R * 0.50f, Colour, 18, StrokeThickness);
        DrawList->AddEllipse(Centre, ImVec2(R * 0.98f, R * 0.30f), Colour, -0.35f, 24, StrokeThickness);
    }
    else if (Identifier == "Cloud")
    {
        DrawList->AddRect(ImVec2(Centre.x - R * 0.85f, Centre.y - R * 0.18f),
                          ImVec2(Centre.x + R * 0.85f, Centre.y + R * 0.62f), Colour, R * 0.40f, 0, StrokeThickness);
        DrawList->AddCircle(ImVec2(Centre.x - R * 0.16f, Centre.y - R * 0.22f), R * 0.42f, Colour, 14, StrokeThickness);
    }
    else if (Identifier == "Globe")
    {
        DrawList->AddCircle(Centre, R * 0.85f, Colour, 20, StrokeThickness);
        DrawList->AddLine(ImVec2(Centre.x - R * 0.85f, Centre.y), ImVec2(Centre.x + R * 0.85f, Centre.y), Colour, StrokeThickness);
        DrawList->AddBezierCubic(ImVec2(Centre.x, Centre.y - R * 0.85f),
                                 ImVec2(Centre.x + R * 0.48f, Centre.y - R * 0.30f),
                                 ImVec2(Centre.x + R * 0.48f, Centre.y + R * 0.30f),
                                 ImVec2(Centre.x, Centre.y + R * 0.85f), Colour, StrokeThickness);
    }
    else if (Identifier == "Star" || Identifier == "Sparkles")
    {
        const ImVec2 Pts[8] = {
            {Centre.x, Centre.y - R}, {Centre.x + R * 0.28f, Centre.y - R * 0.28f},
            {Centre.x + R, Centre.y}, {Centre.x + R * 0.28f, Centre.y + R * 0.28f},
            {Centre.x, Centre.y + R}, {Centre.x - R * 0.28f, Centre.y + R * 0.28f},
            {Centre.x - R, Centre.y}, {Centre.x - R * 0.28f, Centre.y - R * 0.28f},
        };
        DrawList->AddPolyline(Pts, 8, Colour, ImDrawFlags_Closed, StrokeThickness);
    }
    else if (Identifier == "Wind")
    {
        DrawList->AddLine(ImVec2(Centre.x - R * 0.88f, Centre.y - R * 0.35f), ImVec2(Centre.x + R * 0.55f, Centre.y - R * 0.35f), Colour, StrokeThickness);
        DrawList->AddLine(ImVec2(Centre.x - R * 0.88f, Centre.y + 0.05f), ImVec2(Centre.x + R * 0.85f, Centre.y + 0.05f), Colour, StrokeThickness);
        DrawList->AddLine(ImVec2(Centre.x - R * 0.65f, Centre.y + R * 0.45f), ImVec2(Centre.x + R * 0.35f, Centre.y + R * 0.45f), Colour, StrokeThickness);
    }
    else if (Identifier == "Flame")
    {
        const ImVec2 Pts[5] = {
            {Centre.x, Centre.y - R * 0.92f}, {Centre.x + R * 0.68f, Centre.y + R * 0.08f},
            {Centre.x + R * 0.40f, Centre.y + R * 0.85f}, {Centre.x - R * 0.40f, Centre.y + R * 0.85f},
            {Centre.x - R * 0.68f, Centre.y + R * 0.08f},
        };
        DrawList->AddPolyline(Pts, 5, Colour, ImDrawFlags_Closed, StrokeThickness);
    }
    else if (Identifier == "Zap")
    {
        const ImVec2 Pts[4] = {
            {Centre.x + R * 0.15f, Centre.y - R * 0.92f}, {Centre.x - R * 0.55f, Centre.y + R * 0.05f},
            {Centre.x + R * 0.35f, Centre.y + R * 0.05f}, {Centre.x - R * 0.15f, Centre.y + R * 0.92f},
        };
        DrawList->AddPolyline(Pts, 4, Colour, ImDrawFlags_None, StrokeThickness * 1.15f);
    }
    else if (Identifier == "Layers")
    {
        const ImVec2 Top[4] = {
            {Centre.x, Centre.y - R * 0.78f}, {Centre.x + R * 0.88f, Centre.y - R * 0.25f},
            {Centre.x, Centre.y + R * 0.22f}, {Centre.x - R * 0.88f, Centre.y - R * 0.25f},
        };
        DrawList->AddPolyline(Top, 4, Colour, ImDrawFlags_Closed, StrokeThickness);
        DrawList->AddLine(ImVec2(Centre.x - R * 0.88f, Centre.y + R * 0.22f), ImVec2(Centre.x, Centre.y + R * 0.68f), Colour, StrokeThickness);
        DrawList->AddLine(ImVec2(Centre.x, Centre.y + R * 0.68f), ImVec2(Centre.x + R * 0.88f, Centre.y + R * 0.22f), Colour, StrokeThickness);
    }
    else if (Identifier == "Activity")
    {
        const ImVec2 Pts[6] = {
            {Centre.x - R * 0.92f, Centre.y}, {Centre.x - R * 0.42f, Centre.y},
            {Centre.x - R * 0.18f, Centre.y - R * 0.78f}, {Centre.x + R * 0.20f, Centre.y + R * 0.78f},
            {Centre.x + R * 0.46f, Centre.y}, {Centre.x + R * 0.92f, Centre.y},
        };
        DrawList->AddPolyline(Pts, 6, Colour, ImDrawFlags_None, StrokeThickness);
    }
    else if (Identifier == "Sliders")
    {
        for (int Row = -1; Row <= 1; ++Row)
        {
            const float Y = Centre.y + static_cast<float>(Row) * R * 0.56f;
            DrawList->AddLine(ImVec2(Centre.x - R * 0.85f, Y), ImVec2(Centre.x + R * 0.85f, Y), Colour, StrokeThickness);
            const float KnobX = Centre.x + (Row == 0 ? R * 0.35f : -R * 0.30f);
            DrawList->AddCircleFilled(ImVec2(KnobX, Y), 2.1f, Colour, 8);
        }
    }
    else if (Identifier == "GridLattice")
    {
        DrawList->AddRect(ImVec2(Centre.x - R * 0.82f, Centre.y - R * 0.82f),
                          ImVec2(Centre.x + R * 0.82f, Centre.y + R * 0.82f), Colour, 2.0f, 0, StrokeThickness);
        DrawList->AddLine(ImVec2(Centre.x, Centre.y - R * 0.82f), ImVec2(Centre.x, Centre.y + R * 0.82f), Colour, StrokeThickness);
        DrawList->AddLine(ImVec2(Centre.x - R * 0.82f, Centre.y), ImVec2(Centre.x + R * 0.82f, Centre.y), Colour, StrokeThickness);
    }
    else if (Identifier == "Hammer")
    {
        DrawList->AddLine(ImVec2(Centre.x - R * 0.72f, Centre.y + R * 0.72f),
                          ImVec2(Centre.x + R * 0.32f, Centre.y - R * 0.32f), Colour, StrokeThickness * 1.2f);
        DrawList->AddRect(ImVec2(Centre.x - R * 0.05f, Centre.y - R * 0.78f),
                          ImVec2(Centre.x + R * 0.78f, Centre.y - R * 0.12f), Colour, 1.5f, 0, StrokeThickness);
    }
    else if (Identifier == "Eye" || Identifier == "EyeOff")
    {
        DrawList->AddBezierCubic(ImVec2(Centre.x - R * 0.90f, Centre.y),
                                 ImVec2(Centre.x - R * 0.35f, Centre.y - R * 0.68f),
                                 ImVec2(Centre.x + R * 0.35f, Centre.y - R * 0.68f),
                                 ImVec2(Centre.x + R * 0.90f, Centre.y), Colour, StrokeThickness);
        DrawList->AddBezierCubic(ImVec2(Centre.x - R * 0.90f, Centre.y),
                                 ImVec2(Centre.x - R * 0.35f, Centre.y + R * 0.68f),
                                 ImVec2(Centre.x + R * 0.35f, Centre.y + R * 0.68f),
                                 ImVec2(Centre.x + R * 0.90f, Centre.y), Colour, StrokeThickness);
        DrawList->AddCircle(Centre, R * 0.26f, Colour, 10, StrokeThickness);
        if (Identifier == "EyeOff")
        {
            DrawList->AddLine(ImVec2(Centre.x - R * 0.82f, Centre.y + R * 0.72f),
                              ImVec2(Centre.x + R * 0.82f, Centre.y - R * 0.72f), Colour, StrokeThickness);
        }
    }
    else if (Identifier == "Search")
    {
        DrawList->AddCircle(ImVec2(Centre.x - R * 0.15f, Centre.y - R * 0.15f), R * 0.58f, Colour, 16, StrokeThickness);
        DrawList->AddLine(ImVec2(Centre.x + R * 0.28f, Centre.y + R * 0.28f),
                          ImVec2(Centre.x + R * 0.85f, Centre.y + R * 0.85f), Colour, StrokeThickness * 1.15f);
    }
    else if (Identifier == "Check")
    {
        const ImVec2 Pts[3] = {
            {Centre.x - R * 0.65f, Centre.y + R * 0.05f},
            {Centre.x - R * 0.15f, Centre.y + R * 0.55f},
            {Centre.x + R * 0.72f, Centre.y - R * 0.48f},
        };
        DrawList->AddPolyline(Pts, 3, Colour, ImDrawFlags_None, StrokeThickness * 1.25f);
    }
    else if (Identifier == "ChevronRight")
    {
        const ImVec2 Pts[3] = {
            {Centre.x - R * 0.28f, Centre.y - R * 0.58f},
            {Centre.x + R * 0.32f, Centre.y},
            {Centre.x - R * 0.28f, Centre.y + R * 0.58f},
        };
        DrawList->AddPolyline(Pts, 3, Colour, ImDrawFlags_None, StrokeThickness);
    }
    else if (Identifier == "ChevronDown")
    {
        const ImVec2 Pts[3] = {
            {Centre.x - R * 0.58f, Centre.y - R * 0.24f},
            {Centre.x, Centre.y + R * 0.34f},
            {Centre.x + R * 0.58f, Centre.y - R * 0.24f},
        };
        DrawList->AddPolyline(Pts, 3, Colour, ImDrawFlags_None, StrokeThickness);
    }
    else if (Identifier == "Trash")
    {
        DrawList->AddLine(ImVec2(Centre.x - R * 0.72f, Centre.y - R * 0.48f),
                          ImVec2(Centre.x + R * 0.72f, Centre.y - R * 0.48f), Colour, StrokeThickness);
        DrawList->AddRect(ImVec2(Centre.x - R * 0.52f, Centre.y - R * 0.45f),
                          ImVec2(Centre.x + R * 0.52f, Centre.y + R * 0.78f), Colour, 1.5f, 0, StrokeThickness);
    }
    else if (Identifier == "EditorLaunch")
    {
        DrawList->AddRect(ImVec2(Centre.x - R * 0.78f, Centre.y - R * 0.42f),
                          ImVec2(Centre.x + R * 0.42f, Centre.y + R * 0.78f), Colour, 2.0f, 0, StrokeThickness);
        DrawList->AddLine(ImVec2(Centre.x - R * 0.05f, Centre.y + R * 0.05f),
                          ImVec2(Centre.x + R * 0.82f, Centre.y - R * 0.82f), Colour, StrokeThickness);
        DrawList->AddLine(ImVec2(Centre.x + R * 0.30f, Centre.y - R * 0.82f),
                          ImVec2(Centre.x + R * 0.82f, Centre.y - R * 0.82f), Colour, StrokeThickness);
        DrawList->AddLine(ImVec2(Centre.x + R * 0.82f, Centre.y - R * 0.82f),
                          ImVec2(Centre.x + R * 0.82f, Centre.y - R * 0.30f), Colour, StrokeThickness);
    }
    else if (Identifier == "Droplets")
    {
        DrawList->AddCircle(ImVec2(Centre.x, Centre.y + R * 0.22f), R * 0.52f, Colour, 14, StrokeThickness);
        DrawList->AddLine(ImVec2(Centre.x - R * 0.44f, Centre.y - R * 0.05f), ImVec2(Centre.x, Centre.y - R * 0.82f), Colour, StrokeThickness);
        DrawList->AddLine(ImVec2(Centre.x + R * 0.44f, Centre.y - R * 0.05f), ImVec2(Centre.x, Centre.y - R * 0.82f), Colour, StrokeThickness);
    }
    else if (Identifier == "Scan")
    {
        const float C = R * 0.82f;
        const float S = R * 0.36f;
        DrawList->AddLine(ImVec2(Centre.x - C, Centre.y - C), ImVec2(Centre.x - S, Centre.y - C), Colour, StrokeThickness);
        DrawList->AddLine(ImVec2(Centre.x - C, Centre.y - C), ImVec2(Centre.x - C, Centre.y - S), Colour, StrokeThickness);
        DrawList->AddLine(ImVec2(Centre.x + C, Centre.y - C), ImVec2(Centre.x + S, Centre.y - C), Colour, StrokeThickness);
        DrawList->AddLine(ImVec2(Centre.x + C, Centre.y - C), ImVec2(Centre.x + C, Centre.y - S), Colour, StrokeThickness);
        DrawList->AddLine(ImVec2(Centre.x - C, Centre.y + C), ImVec2(Centre.x - S, Centre.y + C), Colour, StrokeThickness);
        DrawList->AddLine(ImVec2(Centre.x - C, Centre.y + C), ImVec2(Centre.x - C, Centre.y + S), Colour, StrokeThickness);
        DrawList->AddLine(ImVec2(Centre.x + C, Centre.y + C), ImVec2(Centre.x + S, Centre.y + C), Colour, StrokeThickness);
        DrawList->AddLine(ImVec2(Centre.x + C, Centre.y + C), ImVec2(Centre.x + C, Centre.y + S), Colour, StrokeThickness);
    }
    else if (Identifier == "Undo2")
    {
        DrawList->AddCircle(ImVec2(Centre.x + R * 0.10f, Centre.y + R * 0.12f), R * 0.62f, Colour, 16, StrokeThickness);
        DrawList->AddLine(ImVec2(Centre.x - R * 0.72f, Centre.y - R * 0.25f), ImVec2(Centre.x - R * 0.30f, Centre.y - R * 0.65f), Colour, StrokeThickness);
    }
    else
    {
        DrawList->AddRect(ImVec2(Centre.x - R * 0.75f, Centre.y - R * 0.75f),
                          ImVec2(Centre.x + R * 0.75f, Centre.y + R * 0.75f), Colour, 2.5f, 0, StrokeThickness);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    INSPECTOR CONTROLS
//------------------------------------------------------------------------------------------------------------------------

void InspectorControls::ConstructCrumbBar(const InspectorPalette& Palette,
                                          const EditorTypefaces&  Faces,
                                          const char*             GroupLabel)
{
    ImDrawList*  DrawList   = ImGui::GetWindowDrawList();
    const ImVec2 Cursor     = ImGui::GetCursorScreenPos();
    const float  AvailWidth = std::max(120.0f, ImGui::GetContentRegionAvail().x);
    const float  BarHeight  = 44.0f;
    const float  MidY       = Cursor.y + BarHeight * 0.5f;

    float LeftX = Cursor.x + 18.0f;
    DrawList->AddText(Faces.Regular, 11.5f, ImVec2(LeftX, MidY - 6.0f), Palette.TextMuted, "Inspector");
    LeftX += MeasureTextWidth(Faces.Regular, 11.5f, "Inspector") + 8.0f;
    if (GroupLabel != nullptr && GroupLabel[0] != '\0')
    {
        ConstructEditorGlyph(DrawList, ImVec2(LeftX + 3.0f, MidY), 4.0f, Palette.TextFaint, "ChevronRight", 1.3f);
        LeftX += 14.0f;
        DrawList->AddText(Faces.Medium, 11.5f, ImVec2(LeftX, MidY - 6.0f), Palette.TextSecondary, GroupLabel);
    }

    const char* SavedText = "Changes apply live";
    const float SavedW    = MeasureTextWidth(Faces.Regular, 10.5f, SavedText);
    const float RightX    = Cursor.x + AvailWidth - 18.0f - SavedW;
    ConstructEditorGlyph(DrawList, ImVec2(RightX - 10.0f, MidY), 4.5f, Palette.LiveGreen, "Check", 1.5f);
    DrawList->AddText(Faces.Regular, 10.5f, ImVec2(RightX, MidY - 5.5f), Palette.TextMuted, SavedText);
    DrawList->AddLine(ImVec2(Cursor.x, Cursor.y + BarHeight - 1.0f),
                      ImVec2(Cursor.x + AvailWidth, Cursor.y + BarHeight - 1.0f), Palette.EdgeBorder, 1.0f);

    ImGui::SetCursorScreenPos(ImVec2(Cursor.x, Cursor.y + BarHeight));
    ImGui::Dummy(ImVec2(AvailWidth, 0.0f));
}

bool InspectorControls::ConstructRecordHeader(const InspectorPalette& Palette,
                                              const EditorTypefaces&  Faces,
                                              const char*             EyebrowCategory,
                                              const char*             RecordName,
                                              const char*             EditorButtonLabel,
                                              bool                    ToggleSupported,
                                              bool&                   InOutEnabled)
{
    ImDrawList*  DrawList      = ImGui::GetWindowDrawList();
    const ImVec2 Origin        = ImGui::GetCursorScreenPos();
    const float  AvailW        = std::max(140.0f, ImGui::GetContentRegionAvail().x);
    bool         EditorClicked = false;

    DrawList->AddText(Faces.SemiBold, 9.5f, Origin, Palette.TextMuted, EyebrowCategory != nullptr ? EyebrowCategory : "");
    DrawList->AddText(Faces.Medium, 19.0f, ImVec2(Origin.x, Origin.y + 15.0f), Palette.TextPrimary,
                      RecordName != nullptr ? RecordName : "");

    float RightEdge = Origin.x + AvailW;

    if (ToggleSupported)
    {
        const char* PillText = InOutEnabled ? "Enabled" : "Disabled";
        const float TextW    = MeasureTextWidth(Faces.Medium, 11.0f, PillText);
        const float PillW    = TextW + 30.0f;
        const float PillH    = 28.0f;
        const ImVec2 PillMin(RightEdge - PillW, Origin.y + 8.0f);
        const ImVec2 PillMax(RightEdge, Origin.y + 8.0f + PillH);

        ImGui::SetCursorScreenPos(PillMin);
        if (ImGui::InvisibleButton("##RecordEnablePill", ImVec2(PillW, PillH)))
        {
            InOutEnabled = !InOutEnabled;
        }
        const bool Hovered = ImGui::IsItemHovered();
        DrawList->AddRectFilled(PillMin, PillMax, Hovered ? Palette.RowHoverBg : Palette.CountCardBg, 14.0f);
        DrawList->AddRect(PillMin, PillMax, Palette.InsetFieldBorder, 14.0f);
        DrawList->AddCircleFilled(ImVec2(PillMin.x + 12.0f, PillMin.y + PillH * 0.5f), 3.4f,
                                  InOutEnabled ? Palette.LiveGreen : Palette.TextFaint, 12);
        DrawList->AddText(Faces.Medium, 11.0f, ImVec2(PillMin.x + 21.0f, PillMin.y + (PillH - 11.0f) * 0.5f),
                          InOutEnabled ? Palette.TextPrimary : Palette.TextMuted, PillText);
        RightEdge = PillMin.x - 8.0f;
    }

    if (EditorButtonLabel != nullptr && EditorButtonLabel[0] != '\0')
    {
        const float TextW = MeasureTextWidth(Faces.Medium, 10.5f, EditorButtonLabel);
        const float BtnW  = TextW + 30.0f;
        const float BtnH  = 28.0f;
        const ImVec2 BtnMin(RightEdge - BtnW, Origin.y + 8.0f);
        const ImVec2 BtnMax(RightEdge, Origin.y + 8.0f + BtnH);

        ImGui::SetCursorScreenPos(BtnMin);
        if (ImGui::InvisibleButton("##LaunchRecordEditor", ImVec2(BtnW, BtnH)))
        {
            EditorClicked = true;
        }
        const bool Hovered = ImGui::IsItemHovered();
        DrawList->AddRectFilled(BtnMin, BtnMax, Hovered ? Palette.SegmentedActiveBg : Palette.CountCardBg, 8.0f);
        DrawList->AddRect(BtnMin, BtnMax, Hovered ? Palette.ThemeAccent : Palette.InsetFieldBorder, 8.0f);
        DrawList->AddText(Faces.Medium, 10.5f, ImVec2(BtnMin.x + 9.0f, BtnMin.y + (BtnH - 10.5f) * 0.5f),
                          Palette.TextPrimary, EditorButtonLabel);
        ConstructEditorGlyph(DrawList, ImVec2(BtnMax.x - 11.0f, BtnMin.y + BtnH * 0.5f),
                             4.8f, Palette.ThemeAccent, "EditorLaunch", 1.3f);
    }

    ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 44.0f));
    ImGui::Dummy(ImVec2(AvailW, 0.0f));
    return EditorClicked;
}

void InspectorControls::ConstructSectionDivider(const InspectorPalette& Palette,
                                                const EditorTypefaces&  Faces,
                                                const char*             UpperLabel,
                                                const char*             TrailingNote)
{
    ImDrawList*  DrawList = ImGui::GetWindowDrawList();
    const ImVec2 Origin   = ImGui::GetCursorScreenPos();
    const float  AvailW   = std::max(120.0f, ImGui::GetContentRegionAvail().x);
    const float  TopY     = Origin.y + 6.0f;

    DrawList->AddText(Faces.SemiBold, 10.0f, ImVec2(Origin.x, TopY), Palette.TextMuted, UpperLabel);
    if (TrailingNote != nullptr && TrailingNote[0] != '\0')
    {
        const float NoteW = MeasureTextWidth(Faces.Regular, 10.0f, TrailingNote);
        DrawList->AddText(Faces.Regular, 10.0f, ImVec2(Origin.x + AvailW - NoteW, TopY), Palette.TextFaint, TrailingNote);
    }
    DrawList->AddLine(ImVec2(Origin.x, TopY + 16.0f), ImVec2(Origin.x + AvailW, TopY + 16.0f), Palette.EdgeBorder, 1.0f);
    ImGui::SetCursorScreenPos(ImVec2(Origin.x, TopY + 23.0f));
    ImGui::Dummy(ImVec2(AvailW, 0.0f));
}

bool InspectorControls::ConstructSwitchGrid(const InspectorPalette&     Palette,
                                            const EditorTypefaces&      Faces,
                                            const char*                 IdPrefix,
                                            const InspectorSwitchEntry* Switches,
                                            size_t                      SwitchCount)
{
    if (Switches == nullptr || SwitchCount == 0)
    {
        return false;
    }

    ImDrawList*  DrawList = ImGui::GetWindowDrawList();
    const ImVec2 Origin   = ImGui::GetCursorScreenPos();
    const float  AvailW   = std::max(160.0f, ImGui::GetContentRegionAvail().x);
    const float  Gap      = 8.0f;
    const float  CardW    = (AvailW - Gap) * 0.5f;
    const float  CardH    = 44.0f;
    bool         Changed  = false;

    for (size_t Index = 0; Index < SwitchCount; ++Index)
    {
        const size_t Col = Index % 2;
        const size_t Row = Index / 2;
        const ImVec2 Min(Origin.x + static_cast<float>(Col) * (CardW + Gap),
                         Origin.y + static_cast<float>(Row) * (CardH + Gap));
        const ImVec2 Max(Min.x + CardW, Min.y + CardH);
        const bool   Active = Switches[Index].Target != nullptr && *Switches[Index].Target;

        char ButtonId[96] = {};
        std::snprintf(ButtonId, sizeof(ButtonId), "%s_Sw%zu", IdPrefix, Index);
        ImGui::SetCursorScreenPos(Min);
        if (ImGui::InvisibleButton(ButtonId, ImVec2(CardW, CardH)) && Switches[Index].Target != nullptr)
        {
            *Switches[Index].Target = !(*Switches[Index].Target);
            Changed                 = true;
        }
        const bool Hovered = ImGui::IsItemHovered();
        DrawList->AddRectFilled(Min, Max, Hovered ? Palette.RowHoverBg : Palette.CountCardBg, 10.0f);
        DrawList->AddRect(Min, Max, Palette.CountCardBorder, 10.0f);
        DrawList->AddText(Faces.Medium, 11.5f, ImVec2(Min.x + 11.0f, Min.y + 8.0f),
                          Active ? Palette.TextPrimary : Palette.TextSecondary, Switches[Index].Label);
        if (Switches[Index].SubNote != nullptr && Switches[Index].SubNote[0] != '\0')
        {
            DrawList->AddText(Faces.Regular, 9.5f, ImVec2(Min.x + 11.0f, Min.y + 24.0f),
                              Palette.TextFaint, Switches[Index].SubNote);
        }

        const float  SwW = 30.0f;
        const float  SwH = 17.0f;
        const ImVec2 SwMin(Max.x - 10.0f - SwW, Min.y + (CardH - SwH) * 0.5f);
        const ImVec2 SwMax(SwMin.x + SwW, SwMin.y + SwH);
        DrawList->AddRectFilled(SwMin, SwMax, Active ? Palette.ToggleOnBg : Palette.ToggleOffBg, SwH * 0.5f);
        const float KnobX = Active ? (SwMax.x - SwH * 0.5f) : (SwMin.x + SwH * 0.5f);
        DrawList->AddCircleFilled(ImVec2(KnobX, SwMin.y + SwH * 0.5f), 6.2f,
                                  Active ? Palette.ViewportBg : Palette.TextMuted, 14);
    }

    const size_t TotalRows = (SwitchCount + 1) / 2;
    ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + static_cast<float>(TotalRows) * (CardH + Gap) + 2.0f));
    ImGui::Dummy(ImVec2(AvailW, 0.0f));
    return Changed;
}

bool InspectorControls::ConstructSliderRow(const InspectorPalette& Palette,
                                           const EditorTypefaces&  Faces,
                                           const char*             Id,
                                           const char*             Label,
                                           float&                  InOutParameter,
                                           float                   Minimum,
                                           float                   Maximum,
                                           const char*             DisplayFormat)
{
    ImDrawList*  DrawList  = ImGui::GetWindowDrawList();
    const ImVec2 Origin    = ImGui::GetCursorScreenPos();
    const float  AvailW    = std::max(180.0f, ImGui::GetContentRegionAvail().x);
    const float  RowHeight = 32.0f;
    const float  LabelW    = std::min(104.0f, AvailW * 0.34f);
    const float  ReadoutW  = 64.0f;
    const float  TrackX    = Origin.x + LabelW + 8.0f;
    const float  TrackW    = std::max(44.0f, AvailW - LabelW - ReadoutW - 18.0f);
    const float  MidY      = Origin.y + RowHeight * 0.5f;

    DrawList->AddText(Faces.Regular, 11.5f, ImVec2(Origin.x, MidY - 6.0f), Palette.TextSecondary, Label);

    ImGui::SetCursorScreenPos(ImVec2(TrackX, Origin.y + 4.0f));
    ImGui::InvisibleButton(Id, ImVec2(TrackW, RowHeight - 8.0f));
    bool Changed = false;
    if (ImGui::IsItemActive())
    {
        const float Ratio     = std::clamp((ImGui::GetIO().MousePos.x - TrackX) / TrackW, 0.0f, 1.0f);
        const float Candidate = Minimum + Ratio * (Maximum - Minimum);
        if (std::fabs(Candidate - InOutParameter) > 1.0e-6f)
        {
            InOutParameter = Candidate;
            Changed        = true;
        }
    }
    const bool Hovered = ImGui::IsItemHovered() || ImGui::IsItemActive();

    const float Norm  = (Maximum > Minimum) ? std::clamp((InOutParameter - Minimum) / (Maximum - Minimum), 0.0f, 1.0f) : 0.0f;
    const float FillX = TrackX + Norm * TrackW;
    DrawList->AddRectFilled(ImVec2(TrackX, MidY - 2.0f), ImVec2(TrackX + TrackW, MidY + 2.0f), Palette.SliderTrackBg, 2.0f);
    if (Norm > 0.002f)
    {
        DrawList->AddRectFilled(ImVec2(TrackX, MidY - 2.0f), ImVec2(FillX, MidY + 2.0f), Palette.SliderFillBg, 2.0f);
    }
    DrawList->AddCircleFilled(ImVec2(FillX, MidY), Hovered ? 6.5f : 5.5f, Palette.SliderThumbBg, 16);

    const ImVec2 BoxMin(Origin.x + AvailW - ReadoutW, Origin.y + 3.0f);
    const ImVec2 BoxMax(Origin.x + AvailW, Origin.y + RowHeight - 3.0f);
    DrawList->AddRectFilled(BoxMin, BoxMax, Palette.InsetFieldBg, 6.0f);
    DrawList->AddRect(BoxMin, BoxMax, Palette.InsetFieldBorder, 6.0f);

    char Formatted[32] = {};
    std::snprintf(Formatted, sizeof(Formatted), DisplayFormat, static_cast<double>(InOutParameter));
    const float TextW = MeasureTextWidth(Faces.Monospace, 11.0f, Formatted);
    DrawList->AddText(Faces.Monospace, 11.0f,
                      ImVec2(BoxMax.x - 7.0f - TextW, BoxMin.y + (BoxMax.y - BoxMin.y - 11.0f) * 0.5f),
                      Palette.TextPrimary, Formatted);

    ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + RowHeight + 2.0f));
    ImGui::Dummy(ImVec2(AvailW, 0.0f));
    return Changed;
}

bool InspectorControls::ConstructIntegerSliderRow(const InspectorPalette& Palette,
                                                  const EditorTypefaces&  Faces,
                                                  const char*             Id,
                                                  const char*             Label,
                                                  uint32_t&               InOutParameter,
                                                  uint32_t                Minimum,
                                                  uint32_t                Maximum,
                                                  const char*             Suffix)
{
    float AsFloat = static_cast<float>(InOutParameter);
    char  Fmt[24] = {};
    std::snprintf(Fmt, sizeof(Fmt), "%%.0f%s", Suffix != nullptr ? Suffix : "");
    if (ConstructSliderRow(Palette, Faces, Id, Label, AsFloat,
                           static_cast<float>(Minimum), static_cast<float>(Maximum), Fmt))
    {
        InOutParameter = static_cast<uint32_t>(std::clamp(std::lround(AsFloat),
                                                          static_cast<long>(Minimum),
                                                          static_cast<long>(Maximum)));
        return true;
    }
    return false;
}

bool InspectorControls::ConstructSegmentedRow(const InspectorPalette& Palette,
                                              const EditorTypefaces&  Faces,
                                              const char*             Id,
                                              const char*             Label,
                                              int&                    InOutSelectedIndex,
                                              const char* const*      Options,
                                              int                     OptionCount)
{
    if (Options == nullptr || OptionCount <= 0)
    {
        return false;
    }

    ImDrawList*  DrawList = ImGui::GetWindowDrawList();
    const ImVec2 Origin   = ImGui::GetCursorScreenPos();
    const float  AvailW   = std::max(180.0f, ImGui::GetContentRegionAvail().x);
    const bool   HasLabel = (Label != nullptr && Label[0] != '\0');
    const float  LabelW   = HasLabel ? std::min(100.0f, AvailW * 0.33f) : 0.0f;
    const float  RowH     = 32.0f;
    const ImVec2 SegMin(Origin.x + (HasLabel ? LabelW + 8.0f : 0.0f), Origin.y + 1.0f);
    const ImVec2 SegMax(Origin.x + AvailW, Origin.y + RowH - 1.0f);

    if (HasLabel)
    {
        DrawList->AddText(Faces.Regular, 11.5f, ImVec2(Origin.x, Origin.y + 9.0f), Palette.TextSecondary, Label);
    }

    DrawList->AddRectFilled(SegMin, SegMax, Palette.SegmentedBg, 8.0f);
    DrawList->AddRect(SegMin, SegMax, Palette.InsetFieldBorder, 8.0f);

    const float InnerW  = (SegMax.x - SegMin.x) - 6.0f;
    const float CellW   = InnerW / static_cast<float>(OptionCount);
    bool        Changed = false;

    for (int Index = 0; Index < OptionCount; ++Index)
    {
        const ImVec2 CellMin(SegMin.x + 3.0f + static_cast<float>(Index) * CellW, SegMin.y + 3.0f);
        const ImVec2 CellMax(CellMin.x + CellW - 2.0f, SegMax.y - 3.0f);
        const bool   Active = (Index == InOutSelectedIndex);

        char BtnId[96] = {};
        std::snprintf(BtnId, sizeof(BtnId), "%s_Opt%d", Id, Index);
        ImGui::SetCursorScreenPos(CellMin);
        if (ImGui::InvisibleButton(BtnId, ImVec2(CellMax.x - CellMin.x, CellMax.y - CellMin.y)))
        {
            if (InOutSelectedIndex != Index)
            {
                InOutSelectedIndex = Index;
                Changed            = true;
            }
        }
        if (Active)
        {
            DrawList->AddRectFilled(CellMin, CellMax, Palette.SegmentedActiveBg, 6.0f);
        }
        const float TextW = MeasureTextWidth(Faces.Medium, 10.5f, Options[Index]);
        DrawList->AddText(Faces.Medium, 10.5f,
                          ImVec2(CellMin.x + (CellMax.x - CellMin.x - TextW) * 0.5f,
                                 CellMin.y + (CellMax.y - CellMin.y - 10.5f) * 0.5f),
                          Active ? Palette.TextPrimary : Palette.TextMuted, Options[Index]);
    }

    ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + RowH + 4.0f));
    ImGui::Dummy(ImVec2(AvailW, 0.0f));
    return Changed;
}

bool InspectorControls::ConstructColourRow(const InspectorPalette& Palette,
                                           const EditorTypefaces&  Faces,
                                           const char*             Id,
                                           const char*             Label,
                                           float                   InOutRgb[3])
{
    ImDrawList*  DrawList = ImGui::GetWindowDrawList();
    const ImVec2 Origin   = ImGui::GetCursorScreenPos();
    const float  AvailW   = std::max(180.0f, ImGui::GetContentRegionAvail().x);
    const float  RowH     = 32.0f;

    DrawList->AddText(Faces.Regular, 11.5f, ImVec2(Origin.x, Origin.y + 9.0f), Palette.TextSecondary, Label);

    static constexpr std::array<std::array<float, 3>, 6> Presets = {{
        {0.82f, 0.14f, 0.16f}, {0.91f, 0.83f, 0.29f}, {0.20f, 0.56f, 0.92f},
        {0.24f, 0.77f, 0.43f}, {0.88f, 0.89f, 0.91f}, {0.14f, 0.15f, 0.17f},
    }};

    bool        Changed = false;
    const float StartX  = Origin.x + AvailW - static_cast<float>(Presets.size()) * 24.0f;
    for (size_t Index = 0; Index < Presets.size(); ++Index)
    {
        const ImVec2 SwMin(StartX + static_cast<float>(Index) * 24.0f, Origin.y + 6.0f);
        const ImVec2 SwMax(SwMin.x + 20.0f, SwMin.y + 20.0f);
        char SwId[96] = {};
        std::snprintf(SwId, sizeof(SwId), "%s_Sw%zu", Id, Index);
        ImGui::SetCursorScreenPos(SwMin);
        if (ImGui::InvisibleButton(SwId, ImVec2(20.0f, 20.0f)))
        {
            InOutRgb[0] = Presets[Index][0];
            InOutRgb[1] = Presets[Index][1];
            InOutRgb[2] = Presets[Index][2];
            Changed     = true;
        }
        const ImU32 Fill = ImGui::ColorConvertFloat4ToU32(
            ImVec4(Presets[Index][0], Presets[Index][1], Presets[Index][2], 1.0f));
        DrawList->AddRectFilled(SwMin, SwMax, Fill, 5.0f);
        DrawList->AddRect(SwMin, SwMax, Palette.InsetFieldBorder, 5.0f);
    }

    ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + RowH + 2.0f));
    ImGui::Dummy(ImVec2(AvailW, 0.0f));
    return Changed;
}

void InspectorControls::ConstructReadoutStrip(const InspectorPalette&     Palette,
                                              const EditorTypefaces&      Faces,
                                              const InspectorReadoutCell* Cells,
                                              size_t                      CellCount)
{
    if (Cells == nullptr || CellCount == 0)
    {
        return;
    }

    ImDrawList*  DrawList = ImGui::GetWindowDrawList();
    const ImVec2 Origin   = ImGui::GetCursorScreenPos();
    const float  AvailW   = std::max(160.0f, ImGui::GetContentRegionAvail().x);
    const float  Gap      = 6.0f;
    const float  CellW    = (AvailW - Gap * static_cast<float>(CellCount - 1)) / static_cast<float>(CellCount);
    const float  CellH    = 46.0f;

    for (size_t Index = 0; Index < CellCount; ++Index)
    {
        const ImVec2 Min(Origin.x + static_cast<float>(Index) * (CellW + Gap), Origin.y);
        const ImVec2 Max(Min.x + CellW, Origin.y + CellH);
        DrawList->AddRectFilled(Min, Max, Palette.InsetFieldBg, 8.0f);
        DrawList->AddRect(Min, Max, Palette.InsetFieldBorder, 8.0f);
        DrawList->AddText(Faces.Regular, 9.5f, ImVec2(Min.x + 9.0f, Min.y + 7.0f), Palette.TextMuted, Cells[Index].Caption);
        DrawList->AddText(Faces.Monospace, 13.0f, ImVec2(Min.x + 9.0f, Min.y + 22.0f), Palette.TextPrimary, Cells[Index].Metric);
    }

    ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + CellH + 8.0f));
    ImGui::Dummy(ImVec2(AvailW, 0.0f));
}

bool InspectorControls::ConstructActionButton(const InspectorPalette& Palette,
                                              const EditorTypefaces&  Faces,
                                              const char*             Id,
                                              const char*             Label)
{
    ImDrawList*  DrawList = ImGui::GetWindowDrawList();
    const ImVec2 Origin   = ImGui::GetCursorScreenPos();
    const float  AvailW   = std::max(140.0f, ImGui::GetContentRegionAvail().x);
    const float  BtnH     = 32.0f;

    ImGui::SetCursorScreenPos(Origin);
    const bool Clicked = ImGui::InvisibleButton(Id, ImVec2(AvailW, BtnH));
    const bool Hovered = ImGui::IsItemHovered();

    DrawList->AddRectFilled(Origin, ImVec2(Origin.x + AvailW, Origin.y + BtnH),
                            Hovered ? Palette.SegmentedActiveBg : Palette.CountCardBg, 8.0f);
    DrawList->AddRect(Origin, ImVec2(Origin.x + AvailW, Origin.y + BtnH), Palette.InsetFieldBorder, 8.0f);
    const float TextW = MeasureTextWidth(Faces.Medium, 11.5f, Label);
    DrawList->AddText(Faces.Medium, 11.5f,
                      ImVec2(Origin.x + (AvailW - TextW) * 0.5f, Origin.y + (BtnH - 11.5f) * 0.5f),
                      Palette.TextPrimary, Label);

    ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + BtnH + 6.0f));
    ImGui::Dummy(ImVec2(AvailW, 0.0f));
    return Clicked;
}

bool InspectorControls::ConstructDangerButton(const InspectorPalette& Palette,
                                              const EditorTypefaces&  Faces,
                                              const char*             Id,
                                              const char*             Label)
{
    ImDrawList*  DrawList = ImGui::GetWindowDrawList();
    const ImVec2 Origin   = ImGui::GetCursorScreenPos();
    const float  AvailW   = std::max(140.0f, ImGui::GetContentRegionAvail().x);
    const float  BtnH     = 30.0f;

    ImGui::SetCursorScreenPos(Origin);
    const bool Clicked = ImGui::InvisibleButton(Id, ImVec2(AvailW, BtnH));
    const bool Hovered = ImGui::IsItemHovered();

    DrawList->AddRectFilled(Origin, ImVec2(Origin.x + AvailW, Origin.y + BtnH),
                            Hovered ? ModulateSpectrumAlpha(Palette.DangerRed, 0.22f) : Palette.InsetFieldBg, 8.0f);
    DrawList->AddRect(Origin, ImVec2(Origin.x + AvailW, Origin.y + BtnH),
                      ModulateSpectrumAlpha(Palette.DangerRed, Hovered ? 0.85f : 0.40f), 8.0f);
    const float TextW = MeasureTextWidth(Faces.Medium, 11.0f, Label);
    DrawList->AddText(Faces.Medium, 11.0f,
                      ImVec2(Origin.x + (AvailW - TextW) * 0.5f, Origin.y + (BtnH - 11.0f) * 0.5f),
                      Palette.DangerRed, Label);

    ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + BtnH + 6.0f));
    ImGui::Dummy(ImVec2(AvailW, 0.0f));
    return Clicked;
}

void InspectorControls::ConstructMutedNote(const InspectorPalette& Palette,
                                           const EditorTypefaces&  Faces,
                                           const char*             Text)
{
    ImGui::PushFont(Faces.Regular, 10.5f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Palette.TextMuted));
    ImGui::TextWrapped("%s", Text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Spacing();
}

void InspectorControls::ConstructFooter(const InspectorPalette& Palette,
                                        const EditorTypefaces&  Faces,
                                        const char*             RecordName,
                                        const char*             CategoryText)
{
    ImDrawList*  DrawList = ImGui::GetWindowDrawList();
    const ImVec2 Origin   = ImGui::GetCursorScreenPos();
    const float  AvailW   = std::max(140.0f, ImGui::GetContentRegionAvail().x);
    const float  TopY     = Origin.y + 10.0f;

    DrawList->AddLine(ImVec2(Origin.x, TopY), ImVec2(Origin.x + AvailW, TopY), Palette.EdgeBorder, 1.0f);
    DrawList->AddText(Faces.Regular, 10.0f, ImVec2(Origin.x, TopY + 9.0f), Palette.TextFaint, RecordName);
    const float CatW = MeasureTextWidth(Faces.Regular, 10.0f, CategoryText);
    DrawList->AddText(Faces.Regular, 10.0f, ImVec2(Origin.x + AvailW - CatW, TopY + 9.0f), Palette.TextFaint, CategoryText);

    ImGui::SetCursorScreenPos(ImVec2(Origin.x, TopY + 26.0f));
    ImGui::Dummy(ImVec2(AvailW, 0.0f));
}

} // namespace Frontier

#endif // FRONTIER_EDITOR && FRONTIER_WITH_INTERFACE_SDK
