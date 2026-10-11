//==============================================================================================================================================
//                                                           WINDEDITORSURFACE.H
//==============================================================================================================================================
// 📦 The WindEditor dialog: the modal that "Open WindEditor · place and combine components" raises in
//    WindPanel.jsx:503. Chrome, component list, XZ placement map and property grid. The evaluator, the mote
//    field and the combined-field canvas are the kit in WindPanelSurface.h, which this dialog embeds.
//
//    Everything here is measured off WindPanel.css and Editor.css. Two faces are needed because the editor
//    body is DM Sans Light (body font-weight 300) while .wind-section-head h2/h3 and .wind-dimensions h3 are
//    font-weight 400.

#pragma once

#include "WindPanelSurface.h"
#include <imgui_internal.h>   // ImTextCharFromUtf8, for letter-spacing and the stretched SVG text

namespace Frontier::WindEditor
{

namespace Kit = Frontier::WindCards;

using Kit::Anchor;
using Kit::Blend;
using Kit::Boxed;
using Kit::Flowed;
using Kit::Grind;
using Kit::Inked;
using Kit::Measured;
using Kit::Stacked;

//------------------------------------------------------------------------------------------------------------------------
//                                                   PALETTE AND GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

constexpr ImU32 Backdrop     = IM_COL32(  0,   0,   0, 187);   // [-] .wind-editor-backdrop #000b
constexpr ImU32 ShellFill    = IM_COL32( 17,  19,  19, 255);   // [-] .wind-editor #111313
constexpr ImU32 ShellEdge    = IM_COL32(255, 255, 255,  32);   // [-] .wind-editor border #ffffff20
constexpr ImU32 ShellText    = IM_COL32(211, 217, 212, 255);   // [-] .wind-editor colour #d3d9d4
constexpr ImU32 Rule         = IM_COL32(255, 255, 255,  18);   // [-] header / footer rule #ffffff12
constexpr ImU32 AsideRule    = IM_COL32(255, 255, 255,  16);   // [-] .wind-component-list border-right #ffffff10
constexpr ImU32 ButtonFill   = IM_COL32( 35,  39,  37, 255);   // [-] .wind-editor button #232725
constexpr ImU32 ButtonEdge   = IM_COL32(255, 255, 255,  16);   // [-] button border #ffffff10
constexpr ImU32 ButtonText   = IM_COL32(215, 222, 215, 255);   // [-] button colour #d7ded7
constexpr ImU32 FieldFill    = IM_COL32( 12,  16,  14, 255);   // [-] input / select background #0c100e
constexpr ImU32 FieldEdge    = IM_COL32(255, 255, 255,  24);   // [-] input / select border #ffffff18
constexpr ImU32 LabelText    = IM_COL32(149, 163, 151, 255);   // [-] .wind-editor label #95a397
constexpr ImU32 EyebrowText  = IM_COL32(133, 153, 138, 255);   // [-] .wind-editor .eyebrow #85998a
constexpr ImU32 HeadNote     = IM_COL32(137, 149, 143, 255);   // [-] .wind-section-head > span / small #89958f
constexpr ImU32 HeadingInk   = IM_COL32(202, 202, 202, 255);   // [-] Editor.css h3 #cacaca
constexpr ImU32 PartFill     = IM_COL32( 26,  31,  28, 255);   // [-] .wind-parts > div #1a1f1c
constexpr ImU32 PartPicked   = IM_COL32( 41,  42,  35, 255);   // [-] .wind-parts > div.selected #292a23
constexpr ImU32 PartEdge     = IM_COL32(191, 169, 130, 102);   // [-] .selected border-color #bfa98266
constexpr ImU32 PartSmall    = IM_COL32(151, 162, 143, 255);   // [-] .wind-parts small #97a28f
constexpr ImU32 ProseText    = IM_COL32(131, 149, 138, 255);   // [-] .wind-editor p #83958a
constexpr ImU32 PropsFill    = IM_COL32( 25,  30,  26, 255);   // [-] .wind-component-properties #191e1a
constexpr ImU32 PropsEdge    = IM_COL32(255, 255, 255,  12);   // [-] .wind-component-properties border #ffffff0c
constexpr ImU32 FooterText   = IM_COL32(137, 152, 139, 255);   // [-] .wind-editor > footer #89988b
constexpr ImU32 LiveDot      = IM_COL32( 52, 199,  89, 255);   // [-] Editor.css .green #34c759
constexpr ImU32 DeadDot      = IM_COL32(198, 115, 112, 255);   // [-] Editor.css .red #c67370
constexpr ImU32 SwitchTint   = IM_COL32(169, 192, 171, 255);   // [-] .wind-view-switches accent-color #a9c0ab
constexpr ImU32 SwitchMark   = IM_COL32( 17,  19,  19, 255);   // [-] the tick the accent colour is cut from

constexpr ImU32 MapFill      = IM_COL32( 21,  25,  27, 255);   // [-] placement rect #15191b
constexpr ImU32 MapGrid      = IM_COL32(255, 255, 255,   9);   // [-] 21 x 21 lattice #ffffff09
constexpr ImU32 MapAxis      = IM_COL32(255, 255, 255,  34);   // [-] centre cross #ffffff22
constexpr ImU32 MapEdge      = IM_COL32(255, 255, 255,  20);   // [-] .wind-placement border #ffffff14
constexpr ImU32 HandleFill   = IM_COL32( 34,  45,  45, 255);   // [-] handle circle #222d2d
constexpr ImU32 HandleEdge   = IM_COL32(126, 172, 163, 255);   // [-] handle stroke #7eaca3
constexpr ImU32 HandlePick   = IM_COL32(224, 197, 147, 255);   // [-] selected handle stroke #e0c593
constexpr ImU32 HandleNumber = IM_COL32(232, 225, 211, 255);   // [-] handle number #e8e1d3
constexpr ImU32 HandleName   = IM_COL32(142, 170, 164, 255);   // [-] handle caption #8eaaa4
constexpr ImU32 ReachFill    = IM_COL32(212, 190, 139,  14);   // [-] selected radius fill #d4be8b0e
constexpr ImU32 ReachFillOff = IM_COL32(132, 200, 189,   8);   // [-] radius fill #84c8bd08
constexpr ImU32 ReachEdge    = IM_COL32(212, 190, 139, 136);   // [-] selected radius stroke #d4be8b88
constexpr ImU32 ReachEdgeOff = IM_COL32(132, 200, 189,  51);   // [-] radius stroke #84c8bd33
constexpr ImU32 MapLegend    = IM_COL32(156, 170, 165, 255);   // [-] corner legends #9caaa5

constexpr float BackPad      = 24.0f;   // [px] .wind-editor-backdrop padding
constexpr float ShellWide    = 1600.0f; // [px] .wind-editor width min(1600px, 100%)
constexpr float ShellTall    = 950.0f;  // [px] .wind-editor height min(950px, 100%)
constexpr float ShellRound   = 20.0f;   // [px] .wind-editor border-radius
constexpr float HeadPadX     = 26.0f;   // [px] .wind-editor-header padding
constexpr float HeadPadY     = 22.0f;
constexpr float HeadGap      = 12.0f;   // [px] .wind-editor-header gap
constexpr float ButtonTall   = 32.0f;   // [px] .wind-editor button min-height
constexpr float ButtonPadX   = 12.0f;   // [px] .wind-editor button padding-left / right
constexpr float ButtonPadY   =  7.0f;   // [px] .wind-editor button padding-top / bottom
constexpr float ButtonRound  =  9.0f;   // [px] .wind-editor button border-radius
constexpr float CloseWide    = 36.0f;   // [px] .wind-close width
constexpr float CloseSize    = 22.0f;   // [px] .wind-close font-size
constexpr float FieldRound   =  8.0f;   // [px] input / select border-radius
constexpr float FieldPad     =  9.0f;   // [px] input / select padding
constexpr float HeadPickWide = 200.0f;  // [px] .wind-editor-header > select width
constexpr float AsideWide    = 235.0f;  // [px] .wind-editor-body first track
constexpr float AsidePad     = 20.0f;   // [px] .wind-component-list padding
constexpr float EnableWide   = 30.0f;   // [px] .wind-parts .wind-enable width
constexpr float PartGap      =  6.0f;   // [px] .wind-parts button gap
constexpr float PartDrop     =  7.0f;   // [px] .wind-parts > div margin-bottom
constexpr float PartRound    = 10.0f;   // [px] .wind-parts > div border-radius
constexpr float AddGap       =  6.0f;   // [px] .wind-add-buttons gap
constexpr float MainPad      = 22.0f;   // [px] .wind-editor-main padding
constexpr float PairGap      = 22.0f;   // [px] .wind-view-pair gap
constexpr float PairHeadTall = 32.0f;   // [px] .wind-view-pair .wind-section-head height
constexpr float PairHeadDrop = 14.0f;   // [px] .wind-view-pair .wind-section-head margin-bottom
constexpr float ViewTall     = 330.0f;  // [px] .wind-placement and .wind-view-pair canvas height
constexpr float MapRound     = 14.0f;   // [px] .wind-placement border-radius
constexpr float SwitchLift   = 12.0f;   // [px] .wind-view-switches margin-top
constexpr float SwitchGap    = 14.0f;   // [px] .wind-view-switches gap
constexpr float SwitchBox    = 13.0f;   // [px] the user agent checkbox
constexpr float SwitchPadL   =  4.0f;   // [px] user agent margin-left
constexpr float SwitchPadR   =  3.0f;   // [px] user agent margin-right
constexpr float SwitchPadY   =  3.0f;   // [px] user agent margin-top / bottom
constexpr float SwitchLabel  =  6.0f;   // [px] .wind-view-switches label gap
constexpr float PropsLift    = 24.0f;   // [px] .wind-component-properties margin-top
constexpr float PropsPad     = 20.0f;   // [px] .wind-component-properties padding
constexpr float PropsRound   = 14.0f;   // [px] .wind-component-properties border-radius
constexpr float PropsHeadDrop= 18.0f;   // [px] .wind-component-properties .wind-section-head margin-bottom
constexpr float GridGap      = 14.0f;   // [px] .wind-property-grid gap
constexpr float FootPadX     = 24.0f;   // [px] .wind-editor > footer padding
constexpr float FootPadY     = 14.0f;
constexpr float FootGap      =  8.0f;   // [px] footer span:first-child gap
constexpr float DotSize      =  7.0f;   // [px] Editor.css .green / .red
constexpr float LabelGap     =  8.0f;   // [px] .wind-editor label gap
constexpr float NameDrop     = 24.0f;   // [px] .wind-name margin-bottom
constexpr float SectionDrop  = 12.0f;   // [px] .wind-section-head margin
constexpr float AddLift      = 14.0f;   // [px] .wind-add-buttons margin-top
constexpr float AddDrop      = 20.0f;   // [px] .wind-add-buttons margin-bottom
constexpr float BoundsLift   = 24.0f;   // [px] .wind-dimensions margin-top
constexpr float BoundsGap    = 12.0f;   // [px] .wind-dimensions label margin-top
constexpr float ProseMargin  = 14.0f;   // [px] Editor.css p margin
constexpr float ProseLine    =  1.6f;   // [-]  .wind-editor p line-height

constexpr float BodySize     = 12.0f;   // [px] .wind-editor button / input / select font-size
constexpr float ShellSize    = 13.0f;   // [px] body font-size, the inherited strut
constexpr float LabelSize    = 11.0f;   // [px] .wind-editor label
constexpr float HeadingSize  = 16.0f;   // [px] .wind-section-head h2 / h3
constexpr float BoundsSize   = 12.0f;   // [px] .wind-dimensions h3
constexpr float TitleSize    = 26.0f;   // [px] .wind-editor-header h1
constexpr float TitleLift    =  5.0f;   // [px] .wind-editor-header h1 margin-top
constexpr float EyebrowSize  =  9.0f;   // [px] .eyebrow
constexpr float EyebrowTrack =  1.4f;   // [px] .eyebrow letter-spacing
constexpr float NoteSize     = 10.0f;   // [px] .wind-section-head > small
constexpr float StampSize    =  8.0f;   // [px] .wind-view-pair .wind-section-head > span
constexpr float PartLeadSize =  9.0f;   // [px] .wind-parts small
constexpr float AddSize      = 11.0f;   // [px] .wind-add-buttons button
constexpr float AddPadY      =  8.0f;   // [px] .wind-add-buttons button padding-top / bottom
constexpr float ProseSize    = 11.0f;   // [px] .wind-editor p
constexpr float FooterSize   = 10.0f;   // [px] .wind-editor > footer

// A line box is exactly one em-scale tall, and the baseline sits an ascent below its top.
inline float LineBox(float Size)   { return Size * Kit::EmScale; }
inline float Baseline(float Size)  { return Size * Kit::AscentShare; }

// Heights that fall out of the box model rather than out of a stylesheet.
constexpr float FieldTall  = FieldPad * 2.0f + 2.0f + BodySize * Kit::EmScale;          // input / select
constexpr float LabelBlock = LabelSize * Kit::EmScale + LabelGap + FieldTall;           // label column
constexpr float PartTall   = 2.0f + ButtonPadY * 2.0f + PartLeadSize * Kit::EmScale
                           + PartGap + BodySize * Kit::EmScale;                          // .wind-parts > div
constexpr float AddTall    = AddPadY * 2.0f + 2.0f + AddSize * Kit::EmScale > ButtonTall
                           ? AddPadY * 2.0f + 2.0f + AddSize * Kit::EmScale : ButtonTall;
constexpr float HeadTall   = HeadPadY * 2.0f + 1.0f
                           + ShellSize * Kit::EmScale + TitleLift + TitleSize * Kit::EmScale;
constexpr float FootTall   = FootPadY * 2.0f + 1.0f + FooterSize * Kit::EmScale;
constexpr float SwitchTall = SwitchBox + SwitchPadY * 2.0f;
constexpr float ProseTall  = ProseMargin * 2.0f + ProseSize * ProseLine;                 // one-line p block
constexpr float ViewTallAll= PairHeadTall + PairHeadDrop + ViewTall;

//------------------------------------------------------------------------------------------------------------------------
//                                                       PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

// letter-spacing: the tracking is added after every glyph, the browser included.
inline float Tracked(ImDrawList* Draw, ImFont* Face, float X, float Y, float Size, ImU32 Colour,
                     const char* Body, float Extra, bool Paint = true)
{
    const float Ground = Grind(Size);
    float Pen = X;
    for (const char* Scan = Body; *Scan;)
    {
        unsigned Point = 0;
        const int Used = ImTextCharFromUtf8(&Point, Scan, nullptr);
        if (Used <= 0) break;
        if (Paint) Draw->AddText(Face, Ground, { Pen, Y }, Colour, Scan, Scan + Used);
        Pen += Face->GetFontBaked(Ground)->GetCharAdvance(static_cast<ImWchar>(Point)) + Extra;
        Scan += Used;
    }
    return Pen - X;
}

// An SVG under preserveAspectRatio="none" stretches text with the rest of the user space. The glyphs are
//    baked at the vertical scale and the pen is walked at the horizontal one, which places every glyph where
//    the browser puts it; only the outlines themselves are not widened.
inline void Stretched(ImDrawList* Draw, ImFont* Face, float X, float Y, float Size, ImU32 Colour,
                      const char* Body, float ScaleX, float ScaleY, Anchor Side = Anchor::Start)
{
    const float Ground = Grind(Size * ScaleY);
    const float Widen  = ScaleX / ScaleY;
    float Pen = X;
    if (Side != Anchor::Start)
    {
        const float Run = Face->CalcTextSizeA(Ground, FLT_MAX, 0.0f, Body).x * Widen;
        Pen -= Side == Anchor::Middle ? Run * 0.5f : Run;
    }
    const float Top = Y - Baseline(Size * ScaleY);
    for (const char* Scan = Body; *Scan;)
    {
        unsigned Point = 0;
        const int Used = ImTextCharFromUtf8(&Point, Scan, nullptr);
        if (Used <= 0) break;
        Draw->AddText(Face, Ground, { Pen, Top }, Colour, Scan, Scan + Used);
        Pen += Face->GetFontBaked(Ground)->GetCharAdvance(static_cast<ImWchar>(Point)) * Widen;
        Scan += Used;
    }
}

inline void Dashed(ImDrawList* Draw, ImVec2 From, ImVec2 To, ImU32 Colour, float On, float Off, float Thick)
{
    const float Run = std::sqrt((To.x - From.x) * (To.x - From.x) + (To.y - From.y) * (To.y - From.y));
    if (Run <= 0.0f) return;
    const float StepX = (To.x - From.x) / Run, StepY = (To.y - From.y) / Run;
    for (float Walk = 0.0f; Walk < Run; Walk += On + Off)
    {
        const float Stop = std::min(Walk + On, Run);
        Draw->AddLine({ From.x + StepX * Walk, From.y + StepY * Walk },
                      { From.x + StepX * Stop, From.y + StepY * Stop }, Colour, Thick);
    }
}

// stroke-dasharray walks the arc length, so the dash pattern is laid down segment by segment.
inline void DashedEllipse(ImDrawList* Draw, ImVec2 Heart, float RadiusX, float RadiusY, ImU32 Colour,
                          float On, float Off, float Thick)
{
    constexpr int Steps = 160;
    float Left = On;
    bool  Ink  = true;
    ImVec2 Last { Heart.x + RadiusX, Heart.y };
    for (int I = 1; I <= Steps; ++I)
    {
        const float Angle = 2.0f * Kit::Pi * I / Steps;
        const ImVec2 Spot { Heart.x + std::cos(Angle) * RadiusX, Heart.y + std::sin(Angle) * RadiusY };
        float Leg = std::sqrt((Spot.x - Last.x) * (Spot.x - Last.x) + (Spot.y - Last.y) * (Spot.y - Last.y));
        ImVec2 Walk = Last;
        while (Leg > 0.0f)
        {
            const float Bite = std::min(Leg, Left);
            const float Share = Bite / std::max(Leg, 1e-6f);
            const ImVec2 Next { Walk.x + (Spot.x - Walk.x) * Share, Walk.y + (Spot.y - Walk.y) * Share };
            if (Ink) Draw->AddLine(Walk, Next, Colour, Thick);
            Walk = Next;
            Leg -= Bite;
            Left -= Bite;
            if (Left <= 1e-6f) { Ink = !Ink; Left = Ink ? On : Off; }
        }
        Last = Spot;
    }
}

inline void PaintDot(ImDrawList* Draw, ImVec2 Spot, bool Live)
{
    Draw->AddCircleFilled({ Spot.x + DotSize * 0.5f, Spot.y + DotSize * 0.5f }, DotSize * 0.5f,
                          Live ? LiveDot : DeadDot, 20);
}

inline float ButtonWidth(ImFont* Face, const char* Body, float Size = BodySize)
{
    return Measured(Face, Size, Body) + ButtonPadX * 2.0f + 2.0f;
}

// A button centres its content in whatever min-height wins.
inline void PaintButton(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide, const char* Body,
                        float Size = BodySize, float Tall = ButtonTall)
{
    Draw->AddRectFilled(Spot, { Spot.x + Wide, Spot.y + Tall }, ButtonFill, ButtonRound);
    Draw->AddRect(Spot, { Spot.x + Wide, Spot.y + Tall }, ButtonEdge, ButtonRound, 0, 1.0f);
    Boxed(Draw, Face, Spot.x + (Wide - Measured(Face, Size, Body)) * 0.5f,
          Spot.y + (Tall - LineBox(Size)) * 0.5f, Size, ButtonText, Body);
}

inline void PaintField(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide, const char* Body,
                       bool Chevron = false)
{
    Draw->AddRectFilled(Spot, { Spot.x + Wide, Spot.y + FieldTall }, FieldFill, FieldRound);
    Draw->AddRect(Spot, { Spot.x + Wide, Spot.y + FieldTall }, FieldEdge, FieldRound, 0, 1.0f);
    Draw->PushClipRect({ Spot.x + 1.0f, Spot.y + 1.0f },
                       { Spot.x + Wide - (Chevron ? 18.0f : 1.0f), Spot.y + FieldTall - 1.0f }, true);
    Boxed(Draw, Face, Spot.x + 1.0f + FieldPad, Spot.y + 1.0f + FieldPad, BodySize, ButtonText, Body);
    Draw->PopClipRect();
    if (Chevron)   // the user agent select arrow
    {
        const float CX = Spot.x + Wide - 13.0f, CY = Spot.y + FieldTall * 0.5f - 1.5f;
        Draw->AddLine({ CX - 3.5f, CY }, { CX, CY + 3.5f }, LabelText, 1.4f);
        Draw->AddLine({ CX, CY + 3.5f }, { CX + 3.5f, CY }, LabelText, 1.4f);
    }
}

inline void PaintLabelled(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide, const char* Caption,
                          const char* Body, bool Select = false)
{
    Boxed(Draw, Face, Spot.x, Spot.y, LabelSize, LabelText, Caption);
    PaintField(Draw, Face, { Spot.x, Spot.y + LineBox(LabelSize) + LabelGap }, Wide, Body, Select);
}

// .wind-section-head is a centred flex row: a heading on the left, a note pushed to the far right.
inline void PaintSectionHead(ImDrawList* Draw, ImFont* Light, ImFont* Regular, ImVec2 Spot, float Wide,
                             float Tall, const char* Heading, ImU32 HeadingColour, const char* Note,
                             float NotePoints)
{
    Boxed(Draw, Regular, Spot.x, Spot.y + (Tall - LineBox(HeadingSize)) * 0.5f, HeadingSize, HeadingColour,
          Heading);
    if (Note && *Note)
        Boxed(Draw, Light, Spot.x + Wide - Measured(Light, NotePoints, Note),
              Spot.y + (Tall - LineBox(NotePoints)) * 0.5f, NotePoints, HeadNote, Note);
}

// Editor.css p: 14 px margins and a 1.6 line box. ImGui stacks wrapped lines one em-scale apart, so the
//    extra leading is added by walking the lines by hand.
inline float ProseHeight(ImFont* Face, float Wide, const char* Body)
{
    const float Lines = std::round(Stacked(Face, ProseSize, Wide, Body) / LineBox(ProseSize));
    return ProseMargin * 2.0f + std::max(1.0f, Lines) * ProseSize * ProseLine;
}

inline void PaintProse(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide, const char* Body)
{
    const float Step = ProseSize * ProseLine;
    const float HalfLead = (Step - LineBox(ProseSize)) * 0.5f;
    float Y = Spot.y + ProseMargin;
    const char* Scan = Body;
    while (*Scan)
    {
        const char* Stop = nullptr;
        const char* Next = Face->CalcWordWrapPosition(Grind(ProseSize), Scan, Scan + std::strlen(Scan), Wide);
        Stop = (Next && Next > Scan) ? Next : Scan + std::strlen(Scan);
        Draw->AddText(Face, Grind(ProseSize), { Spot.x, Y + HalfLead }, ProseText, Scan, Stop);
        Y += Step;
        Scan = Stop;
        while (*Scan == ' ') ++Scan;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE PLACEMENT MAP
//------------------------------------------------------------------------------------------------------------------------
// <svg viewBox="0 0 600 400" preserveAspectRatio="none">: the two axes carry different scales, so a circle
//    is drawn as an ellipse and a stroke is thicker across the stretched axis.

inline void PaintPlacement(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide, float Tall,
                           const Kit::WindComposite& Field, int Selection)
{
    Draw->AddRectFilled(Spot, { Spot.x + Wide, Spot.y + Tall }, MapFill, MapRound);
    // The border is painted last; the user space is the content box inside it.
    const ImVec2 Inner { Spot.x + 1.0f, Spot.y + 1.0f };
    const float  Run = Wide - 2.0f, Drop = Tall - 2.0f;
    const float  ScaleX = Run / 600.0f, ScaleY = Drop / 400.0f;
    auto At = [&](float X, float Y) { return ImVec2{ Inner.x + X * ScaleX, Inner.y + Y * ScaleY }; };

    Draw->PushClipRect(Spot, { Spot.x + Wide, Spot.y + Tall }, true);
    Draw->AddRectFilled(At(0, 0), At(600, 400), MapFill);
    for (int I = 0; I < 21; ++I)
    {
        Draw->AddLine(At(I * 30.0f, 0), At(I * 30.0f, 400), MapGrid, ScaleX);
        Draw->AddLine(At(0, I * 20.0f), At(600, I * 20.0f), MapGrid, ScaleY);
    }
    Dashed(Draw, At(300, 0), At(300, 400), MapAxis, 4.0f * ScaleY, 5.0f * ScaleY, ScaleX);
    Dashed(Draw, At(0, 200), At(600, 200), MapAxis, 4.0f * ScaleX, 5.0f * ScaleX, ScaleY);

    for (int I = 0; I < Field.Count; ++I)
    {
        const Kit::WindPart& Part = Field.Parts[I];
        const bool Global   = Part.Kind == Kit::WindKind::Directional;
        const bool Selected = I == Selection;
        const float X = Global ? 30.0f : (Part.X / Field.Wide + 0.5f) * 600.0f;
        const float Z = Global ? 52.0f + I * 34.0f : (Part.Z / Field.Deep + 0.5f) * 400.0f;
        // opacity on the <g> multiplies into every child it holds
        const float Fade = Part.Enabled ? 1.0f : 0.4f;
        auto Fogged = [&](ImU32 Colour)
        {
            return Blend(Colour, ((Colour >> IM_COL32_A_SHIFT) & 255) / 255.0f * Fade);
        };

        if (!Global)
        {
            const float ReachX = Part.Radius / Field.Wide * 600.0f * ScaleX;
            const float ReachY = Part.Radius / Field.Deep * 400.0f * ScaleY;
            const ImVec2 Heart = At(X, Z);
            Draw->AddEllipseFilled(Heart, { ReachX, ReachY },
                                   Fogged(Selected ? ReachFill : ReachFillOff), 0.0f, 64);
            DashedEllipse(Draw, Heart, ReachX, ReachY, Fogged(Selected ? ReachEdge : ReachEdgeOff),
                          5.0f * ScaleX, 5.0f * ScaleX, 1.0f);
        }
        // r is an SVG length under a non-uniform transform, so the handle is an ellipse on screen.
        const float Radius = Selected ? 16.0f : 13.0f;
        Draw->AddEllipseFilled(At(X, Z), { Radius * ScaleX, Radius * ScaleY }, Fogged(HandleFill), 0.0f, 48);
        Draw->AddEllipse(At(X, Z), { Radius * ScaleX, Radius * ScaleY },
                         Fogged(Selected ? HandlePick : HandleEdge), 0.0f, 48, 1.0f);
        char Number[8];
        std::snprintf(Number, sizeof(Number), "%d", I + 1);
        const ImVec2 Seat = At(X, Z + 4.0f);
        Stretched(Draw, Face, Seat.x, Seat.y, 11.0f, Fogged(HandleNumber), Number, ScaleX, ScaleY,
                  Anchor::Middle);
        char Caption[112];
        std::snprintf(Caption, sizeof(Caption), "%s%s", Part.Name, Global ? " (global)" : "");
        const ImVec2 Tag = At(X + 22.0f, Z - 12.0f);
        Stretched(Draw, Face, Tag.x, Tag.y, 11.0f, Fogged(Selected ? HandlePick : HandleName), Caption,
                  ScaleX, ScaleY);
    }

    const ImVec2 Near = At(12, 20), Far = At(470, 384);
    Stretched(Draw, Face, Near.x, Near.y, 10.0f, MapLegend, "\xe2\x88\x92X / \xe2\x88\x92Z \xc2\xb7 METRES",
              ScaleX, ScaleY);
    Stretched(Draw, Face, Far.x, Far.y, 10.0f, MapLegend, "+X / +Z", ScaleX, ScaleY);
    Draw->PopClipRect();
    Kit::RoundNotch(Draw, Spot, { Spot.x + Wide, Spot.y + Tall }, MapRound, ShellFill);
    Draw->AddRect(Spot, { Spot.x + Wide, Spot.y + Tall }, MapEdge, MapRound, 0, 1.0f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE DIALOG
//------------------------------------------------------------------------------------------------------------------------

constexpr int FieldNameLimit = 84;

struct EditorState
{
    char FieldName[FieldNameLimit] { "Wind field" };
    int  Selection = 0;        // index into Field.Parts; -1 when nothing is selected
    bool Playing   = true;
    bool Vectors   = false;
    bool Gradient  = true;
    bool Particles = true;
    bool Hidden    = false;    // the subject hides the field, so the evaluator is shorted to zero
};

inline const char* KindNote(Kit::WindKind Kind)
{
    switch (Kind)
    {
        case Kit::WindKind::Tornado:
            return "Tangential vortex with inward pull; radius controls the local influence. This is a "
                   "horizontal slice, not a vertical tornado simulation.";
        case Kit::WindKind::Gust:
            return "Local directional pulse with smooth radial falloff. Frequency drives its time variation.";
        case Kit::WindKind::Radial:
            return "Local outward flow with smooth radial falloff.";
        default:
            return "Uniform background flow across the field. Bearing is the direction of travel: "
                   "0\xc2\xb0 north, 90\xc2\xb0 east.";
    }
}

// How many cells the property grid shows: name, type, strength, x, z, radius, then the steered extras.
inline int PropertyCount(Kit::WindKind Kind)
{
    return 6 + ((Kind == Kit::WindKind::Directional || Kind == Kit::WindKind::Gust) ? 1 : 0)
             + (Kind == Kit::WindKind::Gust ? 1 : 0);
}

inline float PropertiesHeight(ImFont* Face, float Wide, const Kit::WindComposite& Field, int Selection)
{
    const bool Picked = Selection >= 0 && Selection < Field.Count;
    float Tall = 2.0f + PropsPad * 2.0f + (Picked ? PairHeadTall : LineBox(HeadingSize)) + PropsHeadDrop;
    if (!Picked) return Tall - PropsHeadDrop;
    const int Rows = (PropertyCount(Field.Parts[Selection].Kind) + 3) / 4;
    Tall += Rows * LabelBlock + (Rows - 1) * GridGap;
    return Tall + ProseHeight(Face, Wide - PropsPad * 2.0f - 2.0f, KindNote(Field.Parts[Selection].Kind));
}

// Spot is the dialog shell's top-left corner, already inside the backdrop's 24 px inset.
inline void PaintDialog(ImDrawList* Draw, ImFont* Light, ImFont* Regular, ImVec2 Spot, float Wide, float Tall,
                        const Kit::WindComposite& Field, const EditorState& State,
                        Kit::WindMotes& Motes, float Delta)
{
    Draw->AddRectFilled(Spot, { Spot.x + Wide, Spot.y + Tall }, ShellFill, ShellRound);
    Draw->PushClipRect(Spot, { Spot.x + Wide, Spot.y + Tall }, true);

    //--- header ------------------------------------------------------------------------------------------
    const float HeadTop = Spot.y + HeadPadY;
    Tracked(Draw, Light, Spot.x + HeadPadX,
            HeadTop + Baseline(ShellSize) - Baseline(EyebrowSize), EyebrowSize, EyebrowText,
            "ENVIRONMENT / FIELD AUTHORING", EyebrowTrack);
    Boxed(Draw, Light, Spot.x + HeadPadX, HeadTop + LineBox(ShellSize) + TitleLift, TitleSize, ShellText,
          "WindEditor");

    const float RowY = Spot.y + (HeadTall - 1.0f - ButtonTall) * 0.5f;
    float Right = Spot.x + Wide - HeadPadX - CloseWide;
    Draw->AddRectFilled({ Right, RowY }, { Right + CloseWide, RowY + ButtonTall }, ButtonFill, ButtonRound);
    Draw->AddRect({ Right, RowY }, { Right + CloseWide, RowY + ButtonTall }, ButtonEdge, ButtonRound, 0, 1.0f);
    Boxed(Draw, Light, Right + (CloseWide - Measured(Light, CloseSize, "\xc3\x97")) * 0.5f,
          RowY + (ButtonTall - LineBox(CloseSize)) * 0.5f, CloseSize, ButtonText, "\xc3\x97");

    const char* PlayLabel = State.Playing ? "Pause preview" : "Resume preview";
    const float PlayWide  = ButtonWidth(Light, PlayLabel);
    Right -= HeadGap + PlayWide;
    PaintButton(Draw, Light, { Right, RowY }, PlayWide, PlayLabel);

    const float NewWide = ButtonWidth(Light, "+ Wind field");
    Right -= HeadGap + NewWide;
    PaintButton(Draw, Light, { Right, RowY }, NewWide, "+ Wind field");

    Right -= HeadGap + HeadPickWide;
    PaintField(Draw, Light, { Right, Spot.y + (HeadTall - 1.0f - FieldTall) * 0.5f }, HeadPickWide,
               State.FieldName, true);
    Draw->AddLine({ Spot.x, Spot.y + HeadTall - 0.5f }, { Spot.x + Wide, Spot.y + HeadTall - 0.5f }, Rule, 1.0f);

    //--- footer ------------------------------------------------------------------------------------------
    const float FootTop = Spot.y + Tall - FootTall;
    Draw->AddLine({ Spot.x, FootTop + 0.5f }, { Spot.x + Wide, FootTop + 0.5f }, Rule, 1.0f);
    const float FootLine = FootTop + 1.0f + FootPadY;
    PaintDot(Draw, { Spot.x + FootPadX, FootLine + (LineBox(FooterSize) - DotSize) * 0.5f }, !State.Hidden);
    Boxed(Draw, Light, Spot.x + FootPadX + DotSize + FootGap, FootLine, FooterSize, FooterText,
          State.Hidden ? "Field hidden \xc2\xb7 zero output" : "All enabled components evaluated together");
    const char* Pending = "Saved with scene \xc2\xb7 HTML evaluator / native wind and cloud simulation pending";
    Boxed(Draw, Light, Spot.x + Wide - FootPadX - Measured(Light, FooterSize, Pending), FootLine, FooterSize,
          FooterText, Pending);

    //--- the component list --------------------------------------------------------------------------------
    const float BodyTop = Spot.y + HeadTall;
    Draw->AddLine({ Spot.x + AsideWide - 0.5f, BodyTop }, { Spot.x + AsideWide - 0.5f, FootTop },
                  AsideRule, 1.0f);
    Draw->PushClipRect({ Spot.x, BodyTop }, { Spot.x + AsideWide - 1.0f, FootTop }, true);
    const float AsideX = Spot.x + AsidePad, AsideRun = AsideWide - 1.0f - AsidePad * 2.0f;
    float Y = BodyTop + AsidePad;

    PaintLabelled(Draw, Light, { AsideX, Y }, AsideRun, "Field name", State.FieldName);
    Y += LabelBlock + NameDrop;   // .wind-name 24 collapses over .wind-section-head 12

    char Tally[16];
    std::snprintf(Tally, sizeof(Tally), "%d / 64", Field.Count);
    PaintSectionHead(Draw, Light, Regular, { AsideX, Y }, AsideRun, LineBox(HeadingSize), "Components",
                     HeadingInk, Tally, NoteSize);
    Y += LineBox(HeadingSize) + std::max(SectionDrop, AddLift);

    const char* Kinds[Kit::WindKindCount] = { "Directional", "Gust", "Tornado", "Radial" };
    const float AddWide = (AsideRun - AddGap) * 0.5f;
    for (int I = 0; I < Kit::WindKindCount; ++I)
    {
        char Body[24];
        std::snprintf(Body, sizeof(Body), "+ %s", Kinds[I]);
        PaintButton(Draw, Light, { AsideX + (I % 2) * (AddWide + AddGap), Y + (I / 2) * (AddTall + AddGap) },
                    AddWide, Body, AddSize, AddTall);
    }
    Y += AddTall * 2.0f + AddGap + AddDrop;

    for (int I = 0; I < Field.Count; ++I)
    {
        const Kit::WindPart& Part = Field.Parts[I];
        const bool Selected = I == State.Selection;
        Draw->AddRectFilled({ AsideX, Y }, { AsideX + AsideRun, Y + PartTall },
                            Selected ? PartPicked : PartFill, PartRound);
        if (Selected)
            Draw->AddRect({ AsideX, Y }, { AsideX + AsideRun, Y + PartTall }, PartEdge, PartRound, 0, 1.0f);
        PaintDot(Draw, { AsideX + 1.0f + (EnableWide - DotSize) * 0.5f, Y + (PartTall - DotSize) * 0.5f },
                 Part.Enabled);
        const float TextX = AsideX + 1.0f + EnableWide + ButtonPadX;
        char Lead[40];
        std::snprintf(Lead, sizeof(Lead), "%02d / %s", I + 1, Kit::KindName(Part.Kind));
        Boxed(Draw, Light, TextX, Y + 1.0f + ButtonPadY, PartLeadSize, PartSmall, Lead);
        Draw->PushClipRect({ TextX, Y }, { AsideX + AsideRun - ButtonPadX, Y + PartTall }, true);
        Boxed(Draw, Light, TextX, Y + 1.0f + ButtonPadY + LineBox(PartLeadSize) + PartGap, BodySize,
              ButtonText, Part.Name);
        Draw->PopClipRect();
        Y += PartTall + PartDrop;
    }
    Y += BoundsLift - (Field.Count ? PartDrop : 0.0f);

    Boxed(Draw, Regular, AsideX, Y, BoundsSize, HeadingInk, "Preview bounds \xc2\xb7 m");
    Y += LineBox(BoundsSize);
    char Bound[16];
    std::snprintf(Bound, sizeof(Bound), "%.0f", double(Field.Wide));
    PaintLabelled(Draw, Light, { AsideX, Y + BoundsGap }, AsideRun, "Width", Bound);
    std::snprintf(Bound, sizeof(Bound), "%.0f", double(Field.Deep));
    PaintLabelled(Draw, Light, { AsideX, Y + BoundsGap * 2.0f + LabelBlock }, AsideRun, "Depth", Bound);
    Draw->PopClipRect();

    //--- the two views -------------------------------------------------------------------------------------
    const float MainX   = Spot.x + AsideWide + MainPad;
    const float MainRun = Wide - AsideWide - MainPad * 2.0f;
    const float Half    = (MainRun - PairGap) * 0.5f;
    float MainY = BodyTop + MainPad;
    Draw->PushClipRect({ Spot.x + AsideWide, BodyTop }, { Spot.x + Wide, FootTop }, true);

    PaintSectionHead(Draw, Light, Regular, { MainX, MainY }, Half, PairHeadTall, "Place components",
                     ShellText, "01 / XZ PLANE", StampSize);
    PaintSectionHead(Draw, Light, Regular, { MainX + Half + PairGap, MainY }, Half, PairHeadTall,
                     "Combined vector field", ShellText, "02 / EVALUATED SUM", StampSize);
    MainY += PairHeadTall + PairHeadDrop;

    PaintPlacement(Draw, Light, { MainX, MainY }, Half, ViewTall, Field, State.Selection);
    Kit::CanvasSettings Look;
    Look.Active    = !State.Hidden;
    Look.Playing   = State.Playing;
    Look.Vectors   = State.Vectors;
    Look.Gradient  = State.Gradient;
    Look.Particles = State.Particles;
    Kit::PaintCanvas(Draw, Light, { MainX + Half + PairGap, MainY }, Half, ViewTall, Field, Look, Motes,
                     State.Playing ? Delta : 0.0f, ShellFill);

    const char* Hint = "Drag numbered handles \xc2\xb7 arrow keys move 10 m \xc2\xb7 directional flow is global.";
    PaintProse(Draw, Light, { MainX, MainY + ViewTall }, Half, Hint);

    // .wind-view-switches: a wrapped row of user agent checkboxes.
    const struct { const char* Label; bool On; } Switches[3] = {
        { "Vectors", State.Vectors }, { "Speed gradient", State.Gradient }, { "Particles", State.Particles } };
    float SwitchX = MainX + Half + PairGap;
    const float SwitchY = MainY + ViewTall + SwitchLift;
    for (const auto& Flip : Switches)
    {
        const ImVec2 Box { SwitchX + SwitchPadL, SwitchY + SwitchPadY };
        Draw->AddRectFilled(Box, { Box.x + SwitchBox, Box.y + SwitchBox }, Flip.On ? SwitchTint : FieldFill,
                            2.0f);
        if (!Flip.On) Draw->AddRect(Box, { Box.x + SwitchBox, Box.y + SwitchBox }, FieldEdge, 2.0f, 0, 1.0f);
        else
        {
            Draw->AddLine({ Box.x + 2.6f, Box.y + 6.6f }, { Box.x + 5.2f, Box.y + 9.6f }, SwitchMark, 1.9f);
            Draw->AddLine({ Box.x + 5.2f, Box.y + 9.6f }, { Box.x + 10.3f, Box.y + 3.4f }, SwitchMark, 1.9f);
        }
        const float TextX = Box.x + SwitchBox + SwitchPadR + SwitchLabel;
        Boxed(Draw, Light, TextX, SwitchY + (SwitchTall - LineBox(LabelSize)) * 0.5f, LabelSize, LabelText,
              Flip.Label);
        SwitchX = TextX + Measured(Light, LabelSize, Flip.Label) + SwitchGap;
    }

    //--- the selected component ------------------------------------------------------------------------------
    const bool  Picked = State.Selection >= 0 && State.Selection < Field.Count;
    const float PropsY = MainY + ViewTall + std::max(ProseTall, SwitchLift + SwitchTall) + PropsLift;
    const float PropsTall = PropertiesHeight(Light, MainRun, Field, State.Selection);
    Draw->AddRectFilled({ MainX, PropsY }, { MainX + MainRun, PropsY + PropsTall }, PropsFill, PropsRound);
    Draw->AddRect({ MainX, PropsY }, { MainX + MainRun, PropsY + PropsTall }, PropsEdge, PropsRound, 0, 1.0f);

    const float PropsX   = MainX + 1.0f + PropsPad;
    const float PropsRun = MainRun - 2.0f - PropsPad * 2.0f;
    float Head = PropsY + 1.0f + PropsPad;
    const float HeadBox = Picked ? PairHeadTall : LineBox(HeadingSize);
    Boxed(Draw, Regular, PropsX, Head + (HeadBox - LineBox(HeadingSize)) * 0.5f, HeadingSize, ShellText,
          Picked ? "Selected component" : "Select or add a component");
    if (Picked)
    {
        const float RemoveWide = ButtonWidth(Light, "Remove component");
        PaintButton(Draw, Light, { PropsX + PropsRun - RemoveWide, Head }, RemoveWide, "Remove component");
    }
    Head += HeadBox + PropsHeadDrop;

    if (Picked)
    {
        const Kit::WindPart& Part = Field.Parts[State.Selection];
        const float Cell = (PropsRun - GridGap * 3.0f) / 4.0f;
        char Strength[16], Along[16], Across[16], Reach[16], Point[16], Beat[16];
        std::snprintf(Strength, sizeof(Strength), "%g", double(Part.Strength));
        std::snprintf(Along,    sizeof(Along),    "%g", double(Part.X));
        std::snprintf(Across,   sizeof(Across),   "%g", double(Part.Z));
        std::snprintf(Reach,    sizeof(Reach),    "%g", double(Part.Radius));
        std::snprintf(Point,    sizeof(Point),    "%g", double(Part.Bearing));
        std::snprintf(Beat,     sizeof(Beat),     "%g", double(Part.Frequency));

        struct Entry { const char* Caption; const char* Body; bool Select; };
        Entry Grid[8] = {
            { "Name",           Part.Name,                 false },
            { "Type",           Kit::KindName(Part.Kind),  true  },
            { "Strength (m/s)", Strength,                  false },
            { "X (m)",          Along,                     false },
            { "Z (m)",          Across,                    false },
            { "Radius (m)",     Reach,                     false },
            { nullptr, nullptr, false }, { nullptr, nullptr, false },
        };
        int Used = 6;
        if (Part.Kind == Kit::WindKind::Directional || Part.Kind == Kit::WindKind::Gust)
            Grid[Used++] = { "Bearing (\xc2\xb0)", Point, false };
        if (Part.Kind == Kit::WindKind::Gust) Grid[Used++] = { "Frequency (Hz)", Beat, false };

        for (int I = 0; I < Used; ++I)
            PaintLabelled(Draw, Light, { PropsX + (I % 4) * (Cell + GridGap),
                                         Head + (I / 4) * (LabelBlock + GridGap) },
                          Cell, Grid[I].Caption, Grid[I].Body, Grid[I].Select);

        const int Rows = (Used + 3) / 4;
        PaintProse(Draw, Light, { PropsX, Head + Rows * LabelBlock + (Rows - 1) * GridGap }, PropsRun,
                   KindNote(Part.Kind));
    }

    Draw->PopClipRect();
    Draw->PopClipRect();
    Kit::RoundNotch(Draw, Spot, { Spot.x + Wide, Spot.y + Tall }, ShellRound, Backdrop);
    Draw->AddRect(Spot, { Spot.x + Wide, Spot.y + Tall }, ShellEdge, ShellRound, 0, 1.0f);
}

} // namespace Frontier::WindEditor
