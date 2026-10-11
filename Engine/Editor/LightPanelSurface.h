//==============================================================================================================================================
//                                                             LIGHTPANELSURFACE.H
//==============================================================================================================================================
// 📦 Drawing surface for the native light inspector: the LightPanel.css palette and the canvas primitives LightProjection.js paints with.

#pragma once

#include <imgui.h>
#include <imgui_internal.h>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Frontier::Lighting
{

constexpr float Pi = 3.14159265358979f;

//------------------------------------------------------------------------------------------------------------------------
//                                                  PALETTE AND GEOMETRY
//------------------------------------------------------------------------------------------------------------------------
// Every constant is read straight off LightPanel.css. The family is green-tinted; a neutral grey panel is
//    the InspectorDepot kit, which is not what ships.

constexpr ImU32 CardFill     = IM_COL32( 25,  25,  25, 255);   // [-] .lp-card background #191919
constexpr ImU32 CardEdge     = IM_COL32(255, 255, 255,  12);   // [-] .lp-card border #ffffff0c
constexpr ImU32 HeadText     = IM_COL32(222, 222, 222, 255);   // [-] .lp-card h3 #dedede
constexpr ImU32 HeadCaption  = IM_COL32(124, 136, 124, 255);   // [-] .lp-card header span #7c887c
constexpr ImU32 PrimaryText  = IM_COL32(237, 240, 231, 255);   // [-] .lp-primary #edf0e7
constexpr ImU32 PrimaryFract = IM_COL32(118, 128, 116, 255);   // [-] .lp-primary .lp-decimal #768074
constexpr ImU32 PrimaryUnit  = IM_COL32(169, 179, 164, 255);   // [-] .lp-primary small #a9b3a4
constexpr ImU32 CaptionText  = IM_COL32(152, 160, 145, 255);   // [-] .lp-primary-caption #98a091
constexpr ImU32 DescribeText = IM_COL32(177, 185, 168, 255);   // [-] .lp-descriptor #b1b9a8
constexpr ImU32 StudyText    = IM_COL32(130, 141, 126, 255);   // [-] .lp-study-note #828d7e
constexpr ImU32 TileFill     = IM_COL32( 32,  35,  31, 255);   // [-] .lp-readings .mp-pill #20231f
constexpr ImU32 TileValue    = IM_COL32(220, 228, 213, 255);   // [-] .lp-readings .v #dce4d5
constexpr ImU32 TileKey      = IM_COL32(155, 169, 147, 255);   // [-] .lp-readings .k #9ba993
constexpr ImU32 FieldLabel   = IM_COL32(153, 153, 153, 255);   // [-] .field > span #999999
constexpr ImU32 TrackLit     = IM_COL32( 69,  69,  69, 255);   // [-] range filled #454545
constexpr ImU32 TrackDim     = IM_COL32( 36,  36,  36, 255);   // [-] range unfilled #242424
constexpr ImU32 EntryFill    = IM_COL32(  0,   0,   0, 255);   // [-] .split-value input #000000
constexpr ImU32 EntryText    = IM_COL32(221, 221, 221, 255);   // [-] .lighting-panel input #dddddd
constexpr ImU32 ChoiceFill   = IM_COL32( 36,  36,  36, 255);   // [-] .lp-profiles button #242424
constexpr ImU32 ChoiceEdge   = IM_COL32(255, 255, 255,  10);   // [-] .lp-profiles button border #ffffff0a
constexpr ImU32 ChoiceText   = IM_COL32(144, 149, 144, 255);   // [-] .lp-profiles button #909590
constexpr ImU32 PickedFill   = IM_COL32( 54,  62,  55, 255);   // [-] button[aria-pressed] #363e37
constexpr ImU32 PickedEdge   = IM_COL32(120, 133, 120, 255);   // [-] button[aria-pressed] #788578
constexpr ImU32 PickedText   = IM_COL32(238, 240, 235, 255);   // [-] button[aria-pressed] #eef0eb
constexpr ImU32 NoteText     = IM_COL32(110, 117, 111, 255);   // [-] .lp-note #6e756f
constexpr ImU32 RowText      = IM_COL32(153, 153, 153, 255);   // [-] .lp-toggle-row #999999
constexpr ImU32 SwitchOff    = IM_COL32( 60,  60,  60, 255);   // [-] .lp-switch #3c3c3c
constexpr ImU32 SwitchOn     = IM_COL32( 50, 199,  99, 255);   // [-] .lp-switch[aria-checked] #32c763
constexpr ImU32 SwitchKnob   = IM_COL32(221, 221, 221, 255);   // [-] .lp-switch i #dddddd
constexpr ImU32 AxisText     = IM_COL32(201, 201, 201, 255);   // [-] .transform-table input #c9c9c9
constexpr ImU32 ResetFill    = IM_COL32( 34,  34,  34, 255);   // [-] .transform-resets button #222222
constexpr ImU32 ResetText    = IM_COL32(170, 170, 170, 255);   // [-] .transform-resets button #aaaaaa
constexpr ImU32 PlotText     = IM_COL32(146, 155, 143, 255);   // [-] ProjectResponse labels #929b8f
constexpr ImU32 PlotTick     = IM_COL32(255, 255, 255,  20);   // [-] ProjectResponse gridline #ffffff14
constexpr ImU32 PlotEdge     = IM_COL32(255, 255, 255,  68);   // [-] ProjectResponse 5 m rule #ffffff44
constexpr ImU32 PlotMark     = IM_COL32(238, 238, 234, 255);   // [-] ProjectResponse 5 m dot #eeeeea
constexpr ImU32 PlotFoot     = IM_COL32(165, 172, 161, 255);   // [-] ProjectResponse distance #a5aca1
constexpr ImU32 AxisFoot     = IM_COL32(130, 138, 130, 255);   // [-] ProjectResponse ends #828a82
constexpr ImU32 StudyInk     = IM_COL32(137, 145, 141, 255);   // [-] ProjectLight Text default #89918d
constexpr ImU32 StudyRule    = IM_COL32(255, 255, 255,  48);   // [-] ProjectLight Path default #ffffff30
constexpr ImU32 StudyRing    = IM_COL32(255, 255, 255,  32);   // [-] ProjectLight Circle default #ffffff20

constexpr float CardRadius  = 18;   // [px] .lp-card border-radius
constexpr float CardPadX    = 15;   // [px] .lp-card padding left and right
constexpr float CardPadY    = 17;   // [px] .lp-card padding top and bottom
constexpr float StackGap    = 12;   // [px] .lighting-panel gap
constexpr float HeadSkirt   = 15;   // [px] .lp-card > header margin-bottom
constexpr float WellRadius  = 6;    // [px] .lp-canvas border-radius
constexpr float StudyWide   = 380;  // [px] ProjectLight design width
constexpr float StudyHigh   = 260;  // [px] ProjectLight design height

// Canvas fillText sits on the baseline; ImGui::AddText hangs from the line box top.
constexpr float BaselineShare = 0.792f;

//------------------------------------------------------------------------------------------------------------------------
//                                                      COLOUR HELPERS
//------------------------------------------------------------------------------------------------------------------------

inline ImU32 Blend(ImU32 Colour, float Alpha)
{
    return (Colour & 0x00ffffffu) | (static_cast<ImU32>(ImClamp(Alpha, 0.0f, 1.0f) * 255.0f + 0.5f) << 24);
}

inline ImU32 FromBytes(const float Rgb[3], float Alpha)
{
    return IM_COL32(static_cast<int>(ImClamp(Rgb[0], 0.0f, 1.0f) * 255.0f + 0.5f),
                    static_cast<int>(ImClamp(Rgb[1], 0.0f, 1.0f) * 255.0f + 0.5f),
                    static_cast<int>(ImClamp(Rgb[2], 0.0f, 1.0f) * 255.0f + 0.5f),
                    static_cast<int>(ImClamp(Alpha,  0.0f, 1.0f) * 255.0f + 0.5f));
}

// kelvin → linear RGB, the Tanner Helland curve LightPanel.js carries verbatim
inline void Kelvin(float Degrees, float Out[3])
{
    const float Heat = Degrees / 100.0f;
    const float Red   = Heat <= 66.0f ? 255.0f : 329.6987f * std::pow(Heat - 60.0f, -0.1332f);
    const float Green = Heat <= 66.0f ? 99.4708f * std::log(Heat) - 161.1196f
                                      : 288.1222f * std::pow(Heat - 60.0f, -0.0755f);
    const float Blue  = Heat >= 66.0f ? 255.0f
                                      : (Heat <= 19.0f ? 0.0f : 138.5177f * std::log(Heat - 10.0f) - 305.0448f);
    Out[0] = ImClamp(Red,   0.0f, 255.0f) / 255.0f;
    Out[1] = ImClamp(Green, 0.0f, 255.0f) / 255.0f;
    Out[2] = ImClamp(Blue,  0.0f, 255.0f) / 255.0f;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CANVAS PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

// Canvas setLineDash([On, Off]) along one segment.
inline void DashedLine(ImDrawList* Draw, ImVec2 From, ImVec2 To, ImU32 Colour, float On, float Off, float Thick = 1.0f)
{
    const float Span = std::sqrt((To.x - From.x) * (To.x - From.x) + (To.y - From.y) * (To.y - From.y));
    if (Span <= 0.0001f)
    {
        return;
    }
    const ImVec2 Step{ (To.x - From.x) / Span, (To.y - From.y) / Span };
    for (float Walked = 0.0f; Walked < Span; Walked += On + Off)
    {
        const float Stop = ImMin(Walked + On, Span);
        Draw->AddLine({ From.x + Step.x * Walked, From.y + Step.y * Walked },
                      { From.x + Step.x * Stop,   From.y + Step.y * Stop }, Colour, Thick);
    }
}

// Canvas createLinearGradient down the y axis. ImGui only ships a KeepAlpha shader, which cannot fade out.
inline void ShadeDown(ImDrawList* Draw, int First, int Last, float TopY, float FootY, ImU32 Top, ImU32 Foot)
{
    const float Reach = ImMax(FootY - TopY, 0.0001f);
    for (ImDrawVert* Vertex = Draw->VtxBuffer.Data + First; Vertex < Draw->VtxBuffer.Data + Last; ++Vertex)
    {
        const float Share = ImClamp((Vertex->pos.y - TopY) / Reach, 0.0f, 1.0f);
        const int Red   = int(((Top >>  0) & 0xff) + (((Foot >>  0) & 0xff) - int((Top >>  0) & 0xff)) * Share);
        const int Green = int(((Top >>  8) & 0xff) + (((Foot >>  8) & 0xff) - int((Top >>  8) & 0xff)) * Share);
        const int Blue  = int(((Top >> 16) & 0xff) + (((Foot >> 16) & 0xff) - int((Top >> 16) & 0xff)) * Share);
        const int Alpha = int(((Top >> 24) & 0xff) + (((Foot >> 24) & 0xff) - int((Top >> 24) & 0xff)) * Share);
        Vertex->col = IM_COL32(Red, Green, Blue, Alpha);
    }
}

// Canvas createRadialGradient(0 → Radius) with Tint(Opacity) at the core and Tint(0) at the rim.
inline void Glow(ImDrawList* Draw, ImVec2 Centre, float Radius, const float Rgb[3], float Opacity, int Steps = 30)
{
    for (int Ring = Steps; Ring >= 1; --Ring)
    {
        const float Outer = Radius * (float(Ring) / float(Steps));
        const float Share = 1.0f - (float(Ring) / float(Steps));
        Draw->AddCircleFilled(Centre, Outer, FromBytes(Rgb, Opacity * Share / float(Steps) * 2.4f), 48);
    }
}

// Canvas shadowBlur around a capsule: concentric strokes fading outward.
inline void SoftEdge(ImDrawList* Draw, ImVec2 Head, ImVec2 Foot, float Radius, ImU32 Tinted, float Blur)
{
    for (int Ring = 6; Ring >= 1; --Ring)
    {
        const float Grow = Blur * (float(Ring) / 6.0f);
        Draw->AddRectFilled({ Head.x - Grow, Head.y - Grow }, { Foot.x + Grow, Foot.y + Grow },
                            Blend(Tinted, ((Tinted >> 24) & 0xff) / 255.0f * 0.10f * (1.0f - float(Ring) / 7.0f)),
                            Radius + Grow);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        LETTERING
//------------------------------------------------------------------------------------------------------------------------

enum class Anchor : unsigned { Left = 0, Centre, Right };

// Canvas fillText: baseline-anchored, with textAlign deciding the horizontal origin.
inline void Inked(ImDrawList* Draw, ImFont* Face, float Size, ImVec2 Spot, ImU32 Colour, const char* Body,
                  Anchor Align = Anchor::Left)
{
    const float Wide = Face->CalcTextSizeA(Size, FLT_MAX, 0.0f, Body).x;
    const float Left = Align == Anchor::Left ? Spot.x : (Align == Anchor::Centre ? Spot.x - Wide * 0.5f : Spot.x - Wide);
    Draw->AddText(Face, Size, { Left, Spot.y - Size * BaselineShare }, Colour, Body);
}

inline float Width(ImFont* Face, float Size, const char* Body)
{
    return Face->CalcTextSizeA(Size, FLT_MAX, 0.0f, Body).x;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      NUMBER FORMATS
//------------------------------------------------------------------------------------------------------------------------

inline void Fixed(char* Out, size_t Size, double Value, int Decimals)
{
    std::snprintf(Out, Size, "%.*f", Decimals, Value);
}

// Number.toLocaleString("en-US") — grouped thousands, the rail and the hero figure both rely on it.
inline void Grouped(char* Out, size_t Size, double Value, int Decimals)
{
    char Plain[64];
    std::snprintf(Plain, sizeof(Plain), "%.*f", Decimals, Value < 0 ? -Value : Value);
    char* Point = std::strchr(Plain, '.');
    const size_t Whole = Point ? size_t(Point - Plain) : std::strlen(Plain);
    size_t Cursor = 0;
    if (Value < 0 && Cursor + 1 < Size)
    {
        Out[Cursor++] = '-';
    }
    for (size_t Index = 0; Index < Whole && Cursor + 1 < Size; ++Index)
    {
        if (Index && (Whole - Index) % 3 == 0)
        {
            Out[Cursor++] = ',';
        }
        Out[Cursor++] = Plain[Index];
    }
    for (const char* Tail = Point; Tail && *Tail && Cursor + 1 < Size; ++Tail)
    {
        Out[Cursor++] = *Tail;
    }
    Out[Cursor] = '\0';
}

}   // namespace Frontier::Lighting
