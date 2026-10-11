//==============================================================================================================================================
//                                                             WINDPANELSURFACE.H
//==============================================================================================================================================
// 📦 Drawing surface for the native wind binding card: the Editor.css and WindPanel.css palette, the WindSpecification.js authoring
//    model, and the animated XZ flow field WindPanel.jsx paints into its canvas.

#pragma once

#include <imgui.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Frontier::WindCards
{

constexpr float Pi = 3.14159265358979f;

//------------------------------------------------------------------------------------------------------------------------
//                                                  PALETTE AND GEOMETRY
//------------------------------------------------------------------------------------------------------------------------
// Read straight off Editor.css (.property-card, button, select) and WindPanel.css (.wind-visual, .wind-legend,
//    .wind-binding). The wind family is teal-to-gold where fog is blue-grey, so the surfaces share no constants.

constexpr ImU32 CardTop     = IM_COL32( 37,  37,  37, 255);   // [-] .property-card gradient 135deg start #252525
constexpr ImU32 CardFoot    = IM_COL32( 32,  32,  32, 255);   // [-] .property-card gradient end #202020
constexpr ImU32 CardEdge    = IM_COL32( 52,  52,  52, 255);   // [-] .property-card border #343434
constexpr ImU32 CardTitle   = IM_COL32(202, 202, 202, 255);   // [-] .property-card h3 #cacaca
constexpr ImU32 CardNote    = IM_COL32(145, 145, 145, 255);   // [-] .property-card > p #919191
constexpr ImU32 BodyText    = IM_COL32(240, 240, 240, 255);   // [-] body colour #f0f0f0
constexpr ImU32 SelectFill  = IM_COL32(  0,   0,   0, 255);   // [-] select background #000
constexpr ImU32 SelectEdge  = IM_COL32(255, 255, 255,  13);   // [-] select border #ffffff0d
constexpr ImU32 SelectText  = IM_COL32(204, 204, 204, 255);   // [-] select colour #ccc
constexpr ImU32 ButtonFill  = IM_COL32( 34,  34,  34, 255);   // [-] button background #222
constexpr ImU32 ButtonEdge  = IM_COL32(255, 255, 255,  13);   // [-] button border #ffffff0d
constexpr ImU32 CanvasFill  = IM_COL32( 17,  27,  32, 255);   // [-] canvas clear #111b20
constexpr ImU32 CanvasEdge  = IM_COL32(255, 255, 255,  18);   // [-] .wind-visual canvas border #ffffff12
constexpr ImU32 CanvasGrid  = IM_COL32(255, 255, 255,   9);   // [-] 32 px lattice #ffffff09
constexpr ImU32 CanvasStamp = IM_COL32(212, 229, 223, 255);   // [-] in-canvas label #d4e5df
constexpr ImU32 LegendText  = IM_COL32(146, 169, 159, 255);   // [-] .wind-legend #92a99f
constexpr ImU32 LegendFrom  = IM_COL32( 48, 110, 124, 255);   // [-] .wind-legend i stop 0 #306e7c
constexpr ImU32 LegendMid   = IM_COL32( 88, 165, 110, 255);   // [-] .wind-legend i stop 1 #58a56e
constexpr ImU32 LegendTo    = IM_COL32(211, 181, 101, 255);   // [-] .wind-legend i stop 2 #d3b565
constexpr ImU32 CaptionText = IM_COL32(131, 148, 142, 255);   // [-] .wind-visual > small #83948e

constexpr float CardPadX    = 24.0f;   // [px] .property-card padding left / right
constexpr float CardPadTop  = 23.0f;   // [px] .property-card padding-top
constexpr float CardPadFoot = 22.0f;   // [px] .property-card padding-bottom
constexpr float CardRound   = 22.0f;   // [px] .property-card border-radius
constexpr float TitleSize   = 12.0f;   // [px] h3 font-size
constexpr float TitleLine   = 16.0f;   // [px] h3 line-height
constexpr float TitleDrop   = 28.0f;   // [px] h3 margin-bottom
constexpr float BodySize    = 13.0f;   // [px] body font-size
constexpr float NoteSize    = 11.0f;   // [px] .property-card > p font-size
constexpr float SelectTall  = 32.0f;   // [px] select height
constexpr float SelectRound = 16.0f;   // [px] select border-radius
constexpr float SelectPadX  = 13.0f;   // [px] select padding-left
constexpr float SelectLift  = 10.0f;   // [px] .wind-binding select margin-top
constexpr float SelectDrop  = 18.0f;   // [px] .wind-binding select margin-bottom
constexpr float WideButPad  = 12.0f;   // [px] .wind-wide-button padding
constexpr float WideButDrop = 14.0f;   // [px] .wind-binding > .wind-wide-button margin-bottom
constexpr float CanvasTall  = 180.0f;  // [px] .wind-binding canvas height
constexpr float CanvasRound = 14.0f;   // [px] .wind-visual canvas border-radius
constexpr float LegendSize  = 10.0f;   // [px] .wind-legend font-size
constexpr float LegendLift  = 12.0f;   // [px] .wind-legend margin-top
constexpr float LegendDrop  =  6.0f;   // [px] .wind-legend margin-bottom
constexpr float LegendGap   = 10.0f;   // [px] .wind-legend gap
constexpr float LegendBar   =  4.0f;   // [px] .wind-legend i height
constexpr float CaptionSize = 10.0f;   // [px] .wind-visual > small font-size

// ImGui bakes a face so that ascent - descent equals the requested size; CSS sizes the em box instead. For
//    DM Sans (hhea ascent 992, descent -310, gap 0 per 1000 em) the two differ by exactly 1.302, so a CSS
//    pixel size is ground up by that factor before it is handed to ImGui. The same number is the font's
//    `normal` line-height, which is why a baked line of text is also exactly one CSS line box tall.
constexpr float EmScale       = 1.302f;   // [-] (992 + 310) / 1000, DM Sans hhea
constexpr float AscentShare   = 0.992f;   // [-] 992 / 1000, the CSS baseline drop inside a line box
constexpr float BaselineShare = AscentShare;

inline float Grind(float Size) { return Size * EmScale; }

//------------------------------------------------------------------------------------------------------------------------
//                                                       PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

enum class Anchor { Start, Middle, End };

inline ImU32 Blend(ImU32 Colour, float Alpha)
{
    return (Colour & 0x00ffffff) | (static_cast<ImU32>(std::clamp(Alpha, 0.0f, 1.0f) * 255.0f + 0.5f) << 24);
}

inline float Measured(ImFont* Face, float Size, const char* Body)
{
    return Face->CalcTextSizeA(Grind(Size), FLT_MAX, 0.0f, Body).x;
}

// Canvas fillText anchors on the baseline; ImGui anchors on the line box top, so the run is lifted back.
inline void Inked(ImDrawList* Draw, ImFont* Face, float X, float Y, float Size, ImU32 Colour, const char* Body,
                  Anchor Side = Anchor::Start)
{
    ImVec2 Spot { X, Y - Size * BaselineShare };
    if (Side != Anchor::Start)
    {
        const float Run = Measured(Face, Size, Body);
        Spot.x -= Side == Anchor::Middle ? Run * 0.5f : Run;
    }
    Draw->AddText(Face, Grind(Size), Spot, Colour, Body);
}

// Line box text: X,Y is the top-left corner, as the CSS box model places it.
inline void Boxed(ImDrawList* Draw, ImFont* Face, float X, float Y, float Size, ImU32 Colour, const char* Body)
{
    Draw->AddText(Face, Grind(Size), { X, Y }, Colour, Body);
}

inline float Stacked(ImFont* Face, float Size, float Wide, const char* Body)
{
    return Face->CalcTextSizeA(Grind(Size), FLT_MAX, Wide, Body).y;
}

inline void Flowed(ImDrawList* Draw, ImFont* Face, float X, float Y, float Size, float Wide, ImU32 Colour,
                   const char* Body)
{
    Draw->AddText(Face, Grind(Size), { X, Y }, Colour, Body, nullptr, Wide);
}

// A canvas with border-radius clips its own painting; an ImGui clip rectangle cannot, so the four corner
//    notches are painted back over in the backdrop colour. Each notch is star-shaped about its square corner,
//    so a triangle fan fills it exactly where AddConvexPolyFilled would not.
inline void RoundNotch(ImDrawList* Draw, ImVec2 Min, ImVec2 Max, float Radius, ImU32 Backdrop)
{
    struct NotchCorner { ImVec2 Pivot; ImVec2 Centre; float From; float To; };
    const NotchCorner Set[4] = {
        { { Min.x, Min.y }, { Min.x + Radius, Min.y + Radius }, Pi,          Pi * 1.5f },
        { { Max.x, Min.y }, { Max.x - Radius, Min.y + Radius }, Pi * 1.5f,   Pi * 2.0f },
        { { Max.x, Max.y }, { Max.x - Radius, Max.y - Radius }, 0.0f,        Pi * 0.5f },
        { { Min.x, Max.y }, { Min.x + Radius, Max.y - Radius }, Pi * 0.5f,   Pi        },
    };
    constexpr int Steps = 14;
    for (const NotchCorner& Corner : Set)
    {
        ImVec2 Previous {};
        for (int I = 0; I <= Steps; ++I)
        {
            const float Angle = Corner.From + (Corner.To - Corner.From) * (static_cast<float>(I) / Steps);
            const ImVec2 Point { Corner.Centre.x + std::cos(Angle) * Radius,
                                 Corner.Centre.y + std::sin(Angle) * Radius };
            if (I) Draw->AddTriangleFilled(Corner.Pivot, Previous, Point, Backdrop);
            Previous = Point;
        }
    }
}

// CSS hsl(): the browser palette for the flow field is authored in hue-saturation-lightness.
inline ImU32 Hsla(float Hue, float Saturation, float Lightness, float Alpha)
{
    const float C = (1.0f - std::fabs(2.0f * Lightness - 1.0f)) * Saturation;
    const float Sector = std::fmod(std::fmod(Hue, 360.0f) + 360.0f, 360.0f) / 60.0f;
    const float Second = C * (1.0f - std::fabs(std::fmod(Sector, 2.0f) - 1.0f));
    float R = 0, G = 0, B = 0;
    if      (Sector < 1) { R = C; G = Second; }
    else if (Sector < 2) { R = Second; G = C; }
    else if (Sector < 3) { G = C; B = Second; }
    else if (Sector < 4) { G = Second; B = C; }
    else if (Sector < 5) { R = Second; B = C; }
    else                 { R = C; B = Second; }
    const float Lift = Lightness - C * 0.5f;
    return IM_COL32(static_cast<int>(std::clamp(R + Lift, 0.0f, 1.0f) * 255.0f + 0.5f),
                    static_cast<int>(std::clamp(G + Lift, 0.0f, 1.0f) * 255.0f + 0.5f),
                    static_cast<int>(std::clamp(B + Lift, 0.0f, 1.0f) * 255.0f + 0.5f),
                    static_cast<int>(std::clamp(Alpha,    0.0f, 1.0f) * 255.0f + 0.5f));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   AUTHORING MODEL
//------------------------------------------------------------------------------------------------------------------------
// A direct reading of WindSpecification.js: a bounded XZ slice carrying up to 64 components, summed linearly.
//    This is the browser authoring model, deliberately separate from the native WindField atmospheric solver.

enum class WindKind { Directional, Gust, Tornado, Radial };

constexpr int WindKindCount = 4;

inline const char* KindName(WindKind Kind)
{
    switch (Kind)
    {
        case WindKind::Directional: return "Directional";
        case WindKind::Gust:        return "Gust";
        case WindKind::Tornado:     return "Tornado";
        case WindKind::Radial:      return "Radial";
    }
    return "Directional";
}

struct WindPart
{
    char     Name[84] { "Wind" };
    WindKind Kind      = WindKind::Directional;
    bool     Enabled   = true;
    float    X         = 0.0f;      // [m] centre on the slice X axis
    float    Z         = 0.0f;      // [m] centre on the slice Z axis
    float    Radius    = 260.0f;    // [m] falloff radius
    float    Strength  = 7.0f;      // [m/s] peak speed
    float    Bearing   = 90.0f;     // [deg] compass bearing the component blows toward
    float    Frequency = 0.3f;      // [Hz] gust pulse rate
};

constexpr int WindPartLimit = 64;

struct WindComposite
{
    float    Wide = 1000.0f;        // [m] slice width,  ResolveWind clamp 100..10000
    float    Deep = 1000.0f;        // [m] slice depth,  ResolveWind clamp 100..10000
    WindPart Parts[WindPartLimit] {};
    int      Count = 0;
};

inline float Clamped(float Value, float Low, float High, float Fallback)
{
    return std::isfinite(Value) ? std::max(Low, std::min(High, Value)) : Fallback;
}

inline void Adopt(WindComposite& Field, const WindPart& Part)
{
    if (Field.Count >= WindPartLimit) return;
    WindPart& Slot = Field.Parts[Field.Count++];
    Slot = Part;
    Slot.X         = Clamped(Part.X,        -Field.Wide * 0.5f, Field.Wide * 0.5f, 0.0f);
    Slot.Z         = Clamped(Part.Z,        -Field.Deep * 0.5f, Field.Deep * 0.5f, 0.0f);
    Slot.Radius    = Clamped(Part.Radius,    10.0f, 10000.0f, 260.0f);
    Slot.Strength  = Clamped(Part.Strength,   0.0f,   100.0f,   7.0f);
    Slot.Bearing   = Clamped(Part.Bearing,    0.0f,   360.0f,  90.0f);
    Slot.Frequency = Clamped(Part.Frequency,  0.0f,     5.0f,   0.3f);
}

// ResolveWind's fallback pair when the subject carries no authored WindField.
inline WindComposite Resolve(float Speed, float Bearing, float Gust)
{
    WindComposite Field;
    WindPart Prevailing;
    std::snprintf(Prevailing.Name, sizeof(Prevailing.Name), "%s", "Prevailing wind");
    Prevailing.Kind     = WindKind::Directional;
    Prevailing.Strength = Speed;
    Prevailing.Bearing  = Bearing;
    Adopt(Field, Prevailing);

    WindPart Passing;
    std::snprintf(Passing.Name, sizeof(Passing.Name), "%s", "Passing gust");
    Passing.Kind      = WindKind::Gust;
    Passing.Strength  = Speed * Gust;
    Passing.Radius    = 450.0f;
    Passing.Bearing   = Bearing;
    Passing.Frequency = 0.3f;
    Adopt(Field, Passing);
    return Field;
}

// EvaluateWind: linear superposition of every enabled component, in metres per second.
inline void Evaluate(const WindComposite& Field, float X, float Z, float Time, float Out[2])
{
    float VX = 0.0f, VZ = 0.0f;
    for (int I = 0; I < Field.Count; ++I)
    {
        const WindPart& Part = Field.Parts[I];
        if (!Part.Enabled || Part.Strength == 0.0f) continue;
        const float DX = X - Part.X, DZ = Z - Part.Z;
        const float Distance = std::sqrt(DX * DX + DZ * DZ);
        const float Reach    = Distance / Part.Radius;
        const float Shoulder = std::max(0.0f, 1.0f - Reach * Reach);
        const float Falloff  = Shoulder * Shoulder;
        const float Angle    = Part.Bearing * Pi / 180.0f;
        if (Part.Kind == WindKind::Directional || Part.Kind == WindKind::Gust)
        {
            const float Amount = Part.Strength
                               * (Part.Kind == WindKind::Gust
                                      ? Falloff * (0.65f + 0.35f * std::sin(Time * 2.0f * Pi * Part.Frequency))
                                      : 1.0f);
            VX += std::sin(Angle) * Amount;
            VZ -= std::cos(Angle) * Amount;
        }
        else if (Distance > 1e-6f)
        {
            const float Amount = Part.Strength * Falloff * std::min(1.0f, Reach * 3.0f);
            if (Part.Kind == WindKind::Tornado)
            {
                VX += ((-DZ - 0.18f * DX) / Distance) * Amount;
                VZ += (( DX - 0.18f * DZ) / Distance) * Amount;
            }
            else
            {
                VX += (DX / Distance) * Amount;
                VZ += (DZ / Distance) * Amount;
            }
        }
    }
    Out[0] = VX;
    Out[1] = VZ;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     FLOW FIELD
//------------------------------------------------------------------------------------------------------------------------
// WindCanvas paints four layers into one 2D context: a smoothed 64x48 speed field, a 32 px lattice, optional
//    vector arrows, and 190 fading flow strokes advected through the composite field.

constexpr int MoteCount = 190;
constexpr int HeatWide  = 64;
constexpr int HeatTall  = 48;

struct WindMotes
{
    float X[MoteCount] {};
    float Y[MoteCount] {};
    float Time  = 0.0f;
    bool  Ready = false;
};

// The browser seeds the motes from two coprime strides, so the pattern is deterministic rather than random.
inline void Seed(WindMotes& Motes)
{
    for (int I = 0; I < MoteCount; ++I)
    {
        Motes.X[I] = static_cast<float>((I * 73) % 151) / 151.0f;
        Motes.Y[I] = static_cast<float>((I * 43) % 149) / 149.0f;
    }
    Motes.Time  = 0.0f;
    Motes.Ready = true;
}

inline ImU32 SpeedInk(float Speed, float Alpha)
{
    const float Share = std::min(1.0f, Speed / 30.0f);
    return Hsla(190.0f - Share * 150.0f, 0.48f, 0.32f + Share * 0.28f, Alpha);
}

struct CanvasSettings
{
    bool Active    = true;
    bool Playing   = true;
    bool Vectors   = false;
    bool Gradient  = true;
    bool Particles = true;
};

// Motes drift along the field they sit in and wrap at the slice edges. Split out of the paint so a caller can
//    wind the clock forward without drawing, which is what a deterministic capture needs.
inline void Advect(const WindComposite& Field, float Wide, float Tall, WindMotes& Motes, float Step,
                   bool Active, bool Playing)
{
    if (!Motes.Ready) Seed(Motes);
    if (!Playing || !Active) return;
    for (int I = 0; I < MoteCount; ++I)
    {
        float Velocity[2];
        Evaluate(Field, (Motes.X[I] - 0.5f) * Field.Wide, (Motes.Y[I] - 0.5f) * Field.Deep, Motes.Time, Velocity);
        Motes.X[I] = std::fmod(std::fmod(Motes.X[I] + Velocity[0] * Step * 8.0f / Field.Wide, 1.0f) + 1.0f, 1.0f);
        Motes.Y[I] = std::fmod(std::fmod(Motes.Y[I] + Velocity[1] * Step * 8.0f / Field.Deep, 1.0f) + 1.0f, 1.0f);
    }
    (void)Wide;
    (void)Tall;
}

// One whole frame of motion, matching what PaintCanvas applies as it draws.
inline void Advance(const WindComposite& Field, float Wide, float Tall, WindMotes& Motes, float Delta,
                    bool Active, bool Playing)
{
    if (!Motes.Ready) Seed(Motes);
    const float Step = std::min(0.05f, Delta);
    if (Playing && Active) Motes.Time += Step;
    Advect(Field, Wide, Tall, Motes, Step, Active, Playing);
}

// Spot is the canvas top-left in screen pixels. Delta advances the clock exactly as the 32 ms frame gate does.
inline void PaintCanvas(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide, float Tall,
                        const WindComposite& Field, const CanvasSettings& Look, WindMotes& Motes, float Delta,
                        ImU32 Backdrop = CardTop)
{
    if (!Motes.Ready) Seed(Motes);
    const float Step = std::min(0.05f, Delta);
    if (Look.Playing && Look.Active) Motes.Time += Step;
    const float Time = Motes.Time;

    Draw->AddRectFilled(Spot, { Spot.x + Wide, Spot.y + Tall }, CanvasFill, CanvasRound);
    Draw->PushClipRect(Spot, { Spot.x + Wide, Spot.y + Tall }, true);

    auto Sample = [&](float X, float Y, float Out[2])
    {
        if (!Look.Active) { Out[0] = 0.0f; Out[1] = 0.0f; return; }
        Evaluate(Field, (X / Wide - 0.5f) * Field.Wide, (Y / Tall - 0.5f) * Field.Deep, Time, Out);
    };

    // A 64x48 speed field drawn back at canvas size with imageSmoothingEnabled, at globalAlpha .5. The smoothing
    //    is bilinear between texel centres, which is what a four-corner gradient quad reproduces exactly.
    if (Look.Gradient)
    {
        static ImU32 Heat[HeatTall][HeatWide];
        for (int Y = 0; Y < HeatTall; ++Y)
            for (int X = 0; X < HeatWide; ++X)
            {
                float Velocity[2];
                Sample((static_cast<float>(X) + 0.5f) / HeatWide * Wide,
                       (static_cast<float>(Y) + 0.5f) / HeatTall * Tall, Velocity);
                Heat[Y][X] = SpeedInk(std::sqrt(Velocity[0] * Velocity[0] + Velocity[1] * Velocity[1]), 0.5f);
            }
        const float CellW = Wide / HeatWide, CellH = Tall / HeatTall;
        for (int Y = -1; Y < HeatTall; ++Y)
            for (int X = -1; X < HeatWide; ++X)
            {
                const int LowX = std::clamp(X, 0, HeatWide - 1), HighX = std::clamp(X + 1, 0, HeatWide - 1);
                const int LowY = std::clamp(Y, 0, HeatTall - 1), HighY = std::clamp(Y + 1, 0, HeatTall - 1);
                const float Left  = Spot.x + (static_cast<float>(X) + 0.5f) * CellW;
                const float Right = Spot.x + (static_cast<float>(X) + 1.5f) * CellW;
                const float Top   = Spot.y + (static_cast<float>(Y) + 0.5f) * CellH;
                const float Foot  = Spot.y + (static_cast<float>(Y) + 1.5f) * CellH;
                Draw->AddRectFilledMultiColor({ std::max(Left, Spot.x), std::max(Top, Spot.y) },
                                              { std::min(Right, Spot.x + Wide), std::min(Foot, Spot.y + Tall) },
                                              Heat[LowY][LowX],  Heat[LowY][HighX],
                                              Heat[HighY][HighX], Heat[HighY][LowX]);
            }
    }

    for (float X = 0; X < Wide; X += 32.0f)
        Draw->AddLine({ Spot.x + X, Spot.y }, { Spot.x + X, Spot.y + Tall }, CanvasGrid, 1.0f);
    for (float Y = 0; Y < Tall; Y += 32.0f)
        Draw->AddLine({ Spot.x, Spot.y + Y }, { Spot.x + Wide, Spot.y + Y }, CanvasGrid, 1.0f);

    if (Look.Vectors)
        for (float Y = 20.0f; Y < Tall; Y += 32.0f)
            for (float X = 20.0f; X < Wide; X += 32.0f)
            {
                float Velocity[2];
                Sample(X, Y, Velocity);
                const float Speed = std::sqrt(Velocity[0] * Velocity[0] + Velocity[1] * Velocity[1]);
                if (Speed < 0.01f) continue;
                const float Angle  = std::atan2(Velocity[1] * Tall / Field.Deep, Velocity[0] * Wide / Field.Wide);
                const float Length = std::min(24.0f, 5.0f + Speed * 0.7f);
                const float Cos = std::cos(Angle), Sin = std::sin(Angle);
                auto Place = [&](float LX, float LY) {
                    return ImVec2 { Spot.x + X + LX * Cos - LY * Sin, Spot.y + Y + LX * Sin + LY * Cos };
                };
                const ImU32 Ink = SpeedInk(Speed, 0.9f);
                Draw->AddLine(Place(-Length * 0.5f, 0.0f), Place(Length * 0.5f, 0.0f), Ink, 1.2f);
                Draw->AddLine(Place(Length * 0.5f, 0.0f), Place(Length * 0.5f - 4.0f, -3.0f), Ink, 1.2f);
                Draw->AddLine(Place(Length * 0.5f, 0.0f), Place(Length * 0.5f - 4.0f,  3.0f), Ink, 1.2f);
            }

    if (Look.Particles)
        for (int I = 0; I < MoteCount; ++I)
        {
            const float X = Motes.X[I] * Wide, Y = Motes.Y[I] * Tall;
            float Velocity[2];
            Sample(X, Y, Velocity);
            const float Length = std::sqrt(Velocity[0] * Velocity[0] + Velocity[1] * Velocity[1]);
            if (Length < 0.01f) continue;

            // Fading flow strokes: the trail walks five steps upstream, then a tail-to-head gradient fades it out.
            const float Stroke = 7.0f + std::min(1.0f, Length / 30.0f) * 34.0f;
            ImVec2 Trail[6];
            int    Walked = 1;
            Trail[0] = { X, Y };
            float TailX = X, TailY = Y;
            for (int Walk = 0; Walk < 5; ++Walk)
            {
                float Local[2];
                Sample(TailX, TailY, Local);
                const float ScreenX = Local[0] * Wide / Field.Wide, ScreenY = Local[1] * Tall / Field.Deep;
                const float ScreenSpeed = std::sqrt(ScreenX * ScreenX + ScreenY * ScreenY);
                if (ScreenSpeed < 0.0001f) break;
                TailX -= ScreenX / ScreenSpeed * Stroke / 5.0f;
                TailY -= ScreenY / ScreenSpeed * Stroke / 5.0f;
                Trail[Walked++] = { TailX, TailY };
            }
            if (Walked < 2) continue;

            const float AxisX = X - TailX, AxisY = Y - TailY;
            const float AxisLength = AxisX * AxisX + AxisY * AxisY;
            auto Share = [&](ImVec2 Point)
            {
                if (AxisLength <= 0.0f) return 1.0f;
                return std::clamp(((Point.x - TailX) * AxisX + (Point.y - TailY) * AxisY) / AxisLength, 0.0f, 1.0f);
            };
            const float Thick = 0.9f + std::min(1.0f, Length / 30.0f) * 1.3f;
            // ImGui cannot fade a stroke's alpha along its length, so each leg is cut into four graded pieces.
            for (int Leg = 0; Leg + 1 < Walked; ++Leg)
            {
                const ImVec2 From = Trail[Leg], To = Trail[Leg + 1];
                constexpr int Pieces = 4;
                for (int Piece = 0; Piece < Pieces; ++Piece)
                {
                    const float A = static_cast<float>(Piece) / Pieces, B = static_cast<float>(Piece + 1) / Pieces;
                    const ImVec2 Head { From.x + (To.x - From.x) * A, From.y + (To.y - From.y) * A };
                    const ImVec2 Foot { From.x + (To.x - From.x) * B, From.y + (To.y - From.y) * B };
                    const float  Mid  = Share({ (Head.x + Foot.x) * 0.5f, (Head.y + Foot.y) * 0.5f });
                    Draw->AddLine({ Spot.x + Head.x, Spot.y + Head.y }, { Spot.x + Foot.x, Spot.y + Foot.y },
                                  SpeedInk(Length, Mid * 0.9f), Thick);
                }
            }
        }

    if (Look.Particles) Advect(Field, Wide, Tall, Motes, Step, Look.Active, Look.Playing);

    Inked(Draw, Face, Spot.x + 14.0f, Spot.y + 20.0f, 11.0f, CanvasStamp,
          Look.Active ? "XZ \xc2\xb7 COMBINED FIELD" : "FIELD DISABLED");
    Draw->PopClipRect();
    RoundNotch(Draw, Spot, { Spot.x + Wide, Spot.y + Tall }, CanvasRound, Backdrop);
    Draw->AddRect(Spot, { Spot.x + Wide, Spot.y + Tall }, CanvasEdge, CanvasRound, 0, 1.0f);
}

// The .wind-visual wrapper: the canvas, the speed legend, and the caption beneath it.
inline float VisualHeight(float CanvasHeight)
{
    return CanvasHeight + LegendLift + LegendSize + LegendDrop + CaptionSize;
}

inline void PaintVisual(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide, float CanvasHeight,
                        const WindComposite& Field, const CanvasSettings& Look, WindMotes& Motes, float Delta,
                        ImU32 Backdrop = CardTop)
{
    PaintCanvas(Draw, Face, Spot, Wide, CanvasHeight, Field, Look, Motes, Delta, Backdrop);

    const float LegendY = Spot.y + CanvasHeight + LegendLift;
    const char* Low = "0 m/s", *High = "30+ m/s";
    const float LowRun = Measured(Face, LegendSize, Low), HighRun = Measured(Face, LegendSize, High);
    Boxed(Draw, Face, Spot.x, LegendY, LegendSize, LegendText, Low);
    Boxed(Draw, Face, Spot.x + Wide - HighRun, LegendY, LegendSize, LegendText, High);

    const float BarLeft  = Spot.x + LowRun + LegendGap;
    const float BarRight = Spot.x + Wide - HighRun - LegendGap;
    const float BarTop   = LegendY + (LegendSize - LegendBar) * 0.5f;
    if (BarRight > BarLeft)
    {
        // linear-gradient(90deg, #306e7c, #58a56e, #d3b565): two halves, each a flat horizontal ramp.
        const float Middle = (BarLeft + BarRight) * 0.5f;
        Draw->AddRectFilledMultiColor({ BarLeft, BarTop }, { Middle, BarTop + LegendBar },
                                      LegendFrom, LegendMid, LegendMid, LegendFrom);
        Draw->AddRectFilledMultiColor({ Middle, BarTop }, { BarRight, BarTop + LegendBar },
                                      LegendMid, LegendTo, LegendTo, LegendMid);
    }

    Boxed(Draw, Face, Spot.x, LegendY + LegendSize + LegendDrop, CaptionSize, CaptionText,
          "Fading flow lines \xc2\xb7 composite speed colours \xc2\xb7 8\xc3\x97 time \xc2\xb7 horizontal XZ slice");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   WIND BINDING CARD
//------------------------------------------------------------------------------------------------------------------------
// WindPanel.jsx WindBinding: the field select, the editor shortcut, the live preview and the binding note. Used by
//    every subject that advects through a shared field, which is why fog and clouds both mount it.

struct BindingValues
{
    const char* FieldName   = nullptr;   // the resolved field, or nullptr for none / missing
    bool        Assigned    = false;     // a field id is stored, whether or not it still resolves
    bool        Following   = true;      // Follow Wind
    bool        FieldHidden = false;     // the bound field is hidden in the outliner
};

inline const char* SelectCaption(const BindingValues& Values)
{
    if (Values.FieldName) return Values.FieldName;
    return Values.Assigned ? "Missing field \xc2\xb7 choose another" : "None \xc2\xb7 still air";
}

inline float WideButtonHeight(float Size)
{
    return std::max(28.0f, std::round(Size * 1.2f) + WideButPad * 2.0f);
}

// The closing paragraph, written once so the measured height and the painted run can never drift apart.
inline void BindingNote(const BindingValues& Values, char* Out, size_t Size)
{
    if (!Values.FieldName)
    {
        std::snprintf(Out, Size, "%s", Values.Assigned ? "The assigned field was removed. Select another field."
                                                       : "No wind field assigned.");
        return;
    }
    char Lead[160];
    if (Values.Following) std::snprintf(Lead, sizeof(Lead), "Using all components of %s.", Values.FieldName);
    else                  std::snprintf(Lead, sizeof(Lead), "%s", "Follow Wind is off; field selection is retained.");
    std::snprintf(Out, Size, "%s HTML binding preview; no native cloud advection.", Lead);
}

// The card body: everything from the field label down to the closing note, measured on its own so a panel that
//    already draws the card chrome can mount just the contents.
inline float BindingBodyHeight(ImFont* Face, float Inner, const BindingValues& Values)
{
    float Height = std::round(BodySize * 1.2f) + SelectLift + SelectTall + SelectDrop;
    char Note[224];
    BindingNote(Values, Note, sizeof(Note));
    if (Values.FieldName)
        Height += WideButtonHeight(BodySize) + WideButDrop + VisualHeight(CanvasTall);
    return Height + Stacked(Face, NoteSize, Inner, Note);
}

inline float BindingHeight(ImFont* Face, float Wide, const BindingValues& Values)
{
    return CardPadTop + TitleLine + TitleDrop
         + BindingBodyHeight(Face, Wide - CardPadX * 2.0f, Values) + CardPadFoot;
}

struct BindingHits
{
    ImVec4 Select {};   // the field select box, in screen pixels
    ImVec4 Edit   {};   // the "Edit wind field" button, empty when no field resolves
};

// Spot is the body's top-left, inside whatever card chrome the panel already draws. Returns the hit rectangles
//    so the caller can mount the interactions it owns.
inline BindingHits PaintBindingBody(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Inner,
                                    const BindingValues& Values, const WindComposite& Field,
                                    WindMotes& Motes, float Delta, ImU32 Backdrop = CardTop)
{
    BindingHits Hits;
    const float Left = Spot.x;
    float Y = Spot.y;
    Boxed(Draw, Face, Left, Y, BodySize, BodyText, "Wind field");
    Y += std::round(BodySize * 1.2f) + SelectLift;

    Draw->AddRectFilled({ Left, Y }, { Left + Inner, Y + SelectTall }, SelectFill, SelectRound);
    Draw->AddRect({ Left, Y }, { Left + Inner, Y + SelectTall }, SelectEdge, SelectRound, 0, 1.0f);
    char Caption[128];
    std::snprintf(Caption, sizeof(Caption), "%s%s", SelectCaption(Values),
                  Values.FieldName && Values.FieldHidden ? " (hidden)" : "");
    Inked(Draw, Face, Left + SelectPadX, Y + SelectTall * 0.5f + BodySize * 0.36f, BodySize, SelectText, Caption);
    const float Chevron = Left + Inner - 17.0f, ChevronY = Y + SelectTall * 0.5f - 1.0f;
    Draw->AddLine({ Chevron - 4.0f, ChevronY }, { Chevron, ChevronY + 4.0f }, SelectText, 1.3f);
    Draw->AddLine({ Chevron, ChevronY + 4.0f }, { Chevron + 4.0f, ChevronY }, SelectText, 1.3f);
    Hits.Select = { Left, Y, Left + Inner, Y + SelectTall };
    Y += SelectTall + SelectDrop;

    if (Values.FieldName)
    {
        const float ButtonTall = WideButtonHeight(BodySize);
        Draw->AddRectFilled({ Left, Y }, { Left + Inner, Y + ButtonTall }, ButtonFill, SelectRound);
        Draw->AddRect({ Left, Y }, { Left + Inner, Y + ButtonTall }, ButtonEdge, SelectRound, 0, 1.0f);
        const char* Label = "Edit wind field \xe2\x86\x97";
        Inked(Draw, Face, Left + Inner * 0.5f, Y + ButtonTall * 0.5f + BodySize * 0.36f, BodySize, BodyText,
              Label, Anchor::Middle);
        Hits.Edit = { Left, Y, Left + Inner, Y + ButtonTall };
        Y += ButtonTall + WideButDrop;

        CanvasSettings Look;
        Look.Active = Values.Following && !Values.FieldHidden;
        PaintVisual(Draw, Face, { Left, Y }, Inner, CanvasTall, Field, Look, Motes, Delta, Backdrop);
        Y += VisualHeight(CanvasTall);
    }

    char Note[224];
    BindingNote(Values, Note, sizeof(Note));
    Flowed(Draw, Face, Left, Y, NoteSize, Inner, CardNote, Note);
    return Hits;
}

// The whole card, chrome included, for a panel that does not draw its own.
inline BindingHits PaintBinding(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide,
                                const BindingValues& Values, const WindComposite& Field,
                                WindMotes& Motes, float Delta)
{
    const float Foot = Spot.y + BindingHeight(Face, Wide, Values);
    // .property-card: linear-gradient(135deg, #252525, #202020) runs corner to corner, so the off-diagonal
    //    corners both carry the midpoint colour.
    const ImU32 Middle = IM_COL32(34, 34, 34, 255);
    Draw->AddRectFilledMultiColor(Spot, { Spot.x + Wide, Foot }, CardTop, Middle, CardFoot, Middle);
    Draw->AddRect(Spot, { Spot.x + Wide, Foot }, CardEdge, CardRound, 0, 1.0f);
    Boxed(Draw, Face, Spot.x + CardPadX, Spot.y + CardPadTop, TitleSize, CardTitle, "Wind binding");
    return PaintBindingBody(Draw, Face,
                            { Spot.x + CardPadX, Spot.y + CardPadTop + TitleLine + TitleDrop },
                            Wide - CardPadX * 2.0f, Values, Field, Motes, Delta, Middle);
}

} // namespace Frontier::WindCards
