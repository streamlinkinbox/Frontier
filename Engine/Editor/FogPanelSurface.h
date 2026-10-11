//==============================================================================================================================================
//                                                              FOGPANELSURFACE.H
//==============================================================================================================================================
// 📦 Drawing surface for the native fog cards: the FogPanel.css and FogShapePanel.css palette, and the three visuals FogPanel.jsx,
//    HeightFogVisual.jsx and FogShapePanel.jsx paint into their SVG design spaces.

#pragma once

#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace Frontier::FogCards
{

constexpr float Pi = 3.14159265358979f;

//------------------------------------------------------------------------------------------------------------------------
//                                                  PALETTE AND GEOMETRY
//------------------------------------------------------------------------------------------------------------------------
// Read straight off FogPanel.css and FogShapePanel.css. The fog family is blue-grey where the light family is
//    green-tinted, so the two surfaces never share constants.

constexpr ImU32 ChamberFill  = IM_COL32( 25,  25,  25, 255);   // [-] .fog-shared-beam background #191919
constexpr ImU32 ChamberEdge  = IM_COL32(255, 255, 255,  18);   // [-] .fog-shared-beam border-top #ffffff12
constexpr ImU32 ChamberKey   = IM_COL32(119, 125, 120, 255);   // [-] .fog-shared-beam header #777d78
constexpr ImU32 ChamberLive  = IM_COL32(207, 211, 206, 255);   // [-] header strong #cfd3ce
constexpr ImU32 ChamberIdle  = IM_COL32(215, 154, 143, 255);   // [-] [data-enabled=false] strong #d79a8f
constexpr ImU32 ChamberSmall = IM_COL32(102, 109, 104, 255);   // [-] .fog-shared-beam > small #666d68
constexpr ImU32 ChamberText  = IM_COL32(147, 154, 149, 255);   // [-] .fog-shared-beam svg text #939a95
constexpr ImU32 ChamberRule  = IM_COL32(255, 255, 255,  16);   // [-] chamber gridline #ffffff10
constexpr ImU32 ChamberMark  = IM_COL32(237, 143, 143, 199);   // [-] 2 % marker #ed8f8f at .78
constexpr ImU32 ChamberLamp  = IM_COL32(255, 241, 202, 255);   // [-] emitter dot #fff1ca

constexpr ImU32 ProfileFill  = IM_COL32( 25,  25,  25, 255);   // [-] .height-density-visual > svg #191919
constexpr ImU32 ProfileEdge  = IM_COL32(255, 255, 255,  11);   // [-] svg border #ffffff0b
constexpr ImU32 ProfileKey   = IM_COL32(143, 150, 145, 255);   // [-] .height-density-status #8f9691
constexpr ImU32 ProfileLive  = IM_COL32( 81, 212, 135, 255);   // [-] status dot #51d487
constexpr ImU32 ProfileIdle  = IM_COL32(211, 113, 113, 255);   // [-] status dot disabled #d37171
constexpr ImU32 ProfileHead  = IM_COL32(201, 204, 199, 255);   // [-] status strong #c9ccc7
constexpr ImU32 ProfileValue = IM_COL32(226, 228, 223, 255);   // [-] .height-density-metric strong #e2e4df
constexpr ImU32 ProfileUnit  = IM_COL32(119, 125, 120, 255);   // [-] metric span and > p #777d78
constexpr ImU32 ProfileText  = IM_COL32(150, 157, 153, 255);   // [-] svg text #969d99
constexpr ImU32 ProfileRule  = IM_COL32(255, 255, 255,  18);   // [-] density gridline #ffffff12
constexpr ImU32 ProfileTick  = IM_COL32(255, 255, 255,  11);   // [-] altitude gridline #ffffff0b
constexpr ImU32 ProfileDrop  = IM_COL32(255, 255, 255, 107);   // [-] falloff rule #ffffff6b
constexpr ImU32 ProfileRing  = IM_COL32( 17,  19,  21, 255);   // [-] handle stroke #111315
constexpr ImU32 ReadingFill  = IM_COL32( 24,  25,  24, 255);   // [-] .height-density-readings span #181918
constexpr ImU32 ReadingKey   = IM_COL32(120, 126, 121, 255);   // [-] readings small #787e79
constexpr ImU32 ReadingValue = IM_COL32(215, 218, 213, 255);   // [-] readings b #d7dad5

constexpr ImU32 VolumeFill   = IM_COL32( 21,  27,  29, 255);   // [-] .fog-shape-preview background #151b1d
constexpr ImU32 VolumeEdge   = IM_COL32(255, 255, 255,  18);   // [-] preview border #ffffff12
constexpr ImU32 VolumeGlow   = IM_COL32(173, 203, 214, 255);   // [-] preview radial gradient #adcbd6
constexpr ImU32 VolumeFace   = IM_COL32(184, 217, 225,   7);   // [-] mesh face #b8d9e107
constexpr ImU32 VolumeWire   = IM_COL32(175, 206, 218, 255);   // [-] mesh edge #afceda
constexpr ImU32 VolumeStamp  = IM_COL32(129, 150, 159, 255);   // [-] preview caption #81969f
constexpr ImU32 ChoiceText   = IM_COL32(170, 184, 185, 255);   // [-] .fog-shape-choice #aab8b9
constexpr ImU32 EntryKey     = IM_COL32(158, 175, 178, 255);   // [-] .fog-dimensions label #9eafb2
constexpr ImU32 EntryFill    = IM_COL32( 17,  21,  22, 255);   // [-] .fog-dimensions input #111516
constexpr ImU32 EntryEdge    = IM_COL32(255, 255, 255,  12);   // [-] input border #ffffff0c
constexpr ImU32 EntryText    = IM_COL32(208, 223, 225, 255);   // [-] input colour #d0dfe1
constexpr ImU32 BoundsRule   = IM_COL32(255, 255, 255,  16);   // [-] .fog-derived-bounds border-top #ffffff10
constexpr ImU32 BoundsHead   = IM_COL32(160, 180, 184, 255);   // [-] .fog-derived-bounds h4 #a0b4b8
constexpr ImU32 BoundsKey    = IM_COL32(132, 150, 155, 255);   // [-] th:first-child #84969b
constexpr ImU32 BoundsValue  = IM_COL32(196, 217, 223, 255);   // [-] td #c4d9df
constexpr ImU32 NoteText     = IM_COL32(132, 151, 155, 255);   // [-] .fog-shape-card p #84979b
constexpr ImU32 RowLabel     = IM_COL32(153, 153, 153, 255);   // [-] .transform-table row label #999999

constexpr float BaselineShare = 0.792f;   // [-] SVG text y is the baseline; ImGui places the line box top

//------------------------------------------------------------------------------------------------------------------------
//                                                      DESIGN SPACE
//------------------------------------------------------------------------------------------------------------------------
// An SVG viewBox mapped onto a screen rectangle. Meet reproduces the default preserveAspectRatio
//    "xMidYMid meet" — uniform scale, letterboxed. Stretch reproduces preserveAspectRatio="none".

struct Frame
{
    ImDrawList* Draw   = nullptr;
    ImFont*     Face   = nullptr;
    ImVec2      Origin {};
    float       ScaleX = 1.0f;
    float       ScaleY = 1.0f;

    ImVec2 At(float X, float Y) const { return { Origin.x + X * ScaleX, Origin.y + Y * ScaleY }; }
    float  Span(float Length) const    { return Length * ScaleX; }
    float  Rise(float Length) const    { return Length * ScaleY; }
};

inline Frame Meet(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, ImVec2 Size, float Wide, float Tall)
{
    const float Share = std::min(Size.x / Wide, Size.y / Tall);
    return { Draw, Face, { Spot.x + (Size.x - Wide * Share) * 0.5f, Spot.y + (Size.y - Tall * Share) * 0.5f },
             Share, Share };
}

inline Frame Stretch(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, ImVec2 Size, float Wide, float Tall)
{
    return { Draw, Face, Spot, Size.x / Wide, Size.y / Tall };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

enum class Anchor { Start, Middle, End };

inline ImU32 Blend(ImU32 Colour, float Alpha)
{
    return (Colour & 0x00ffffff) | (static_cast<ImU32>(std::clamp(Alpha, 0.0f, 1.0f) * 255.0f + 0.5f) << 24);
}

inline ImU32 FromParts(const float Rgb[3], float Alpha)
{
    return IM_COL32(static_cast<int>(std::clamp(Rgb[0], 0.0f, 1.0f) * 255.0f + 0.5f),
                    static_cast<int>(std::clamp(Rgb[1], 0.0f, 1.0f) * 255.0f + 0.5f),
                    static_cast<int>(std::clamp(Rgb[2], 0.0f, 1.0f) * 255.0f + 0.5f),
                    static_cast<int>(std::clamp(Alpha,  0.0f, 1.0f) * 255.0f + 0.5f));
}

inline float Measured(ImFont* Face, float Size, const char* Body)
{
    return Face->CalcTextSizeA(Size, FLT_MAX, 0.0f, Body).x;
}

// SVG text: y is the baseline and text-anchor shifts the run, so both are undone before ImGui draws it.
inline void Inked(const Frame& F, float X, float Y, float Size, ImU32 Colour, const char* Body,
                  Anchor Side = Anchor::Start)
{
    const float Lifted = Size * F.ScaleY;
    ImVec2 Spot = F.At(X, Y);
    Spot.y -= Lifted * BaselineShare;
    if (Side != Anchor::Start)
    {
        const float Run = Measured(F.Face, Lifted, Body);
        Spot.x -= Side == Anchor::Middle ? Run * 0.5f : Run;
    }
    F.Draw->AddText(F.Face, Lifted, Spot, Colour, Body);
}

inline void Dashed(const Frame& F, ImVec2 From, ImVec2 To, ImU32 Colour, float On, float Off, float Thick = 1.0f)
{
    const ImVec2 A = F.At(From.x, From.y), B = F.At(To.x, To.y);
    const float  Dx = B.x - A.x, Dy = B.y - A.y, Reach = std::sqrt(Dx * Dx + Dy * Dy);
    if (Reach <= 0.0001f)
    {
        return;
    }
    const float Step = F.ScaleX;
    for (float Walked = 0.0f; Walked < Reach; Walked += (On + Off) * Step)
    {
        const float Stop = std::min(Walked + On * Step, Reach);
        F.Draw->AddLine({ A.x + Dx * Walked / Reach, A.y + Dy * Walked / Reach },
                        { A.x + Dx * Stop / Reach,   A.y + Dy * Stop / Reach }, Colour, Thick);
    }
}

// A vertical linearGradient over an already-emitted vertex range; ImGui's own shader keeps alpha, which
//    cannot fade a fill to nothing, so every channel is interpolated by hand.
inline void ShadeColumn(ImDrawList* Draw, int First, int Last, float TopY, float FootY, ImU32 Top, ImU32 Foot)
{
    const float Reach = std::max(0.0001f, FootY - TopY);
    for (int Index = First; Index < Last; ++Index)
    {
        ImDrawVert& Vertex = Draw->VtxBuffer[Index];
        const float Share  = std::clamp((Vertex.pos.y - TopY) / Reach, 0.0f, 1.0f);
        ImU32 Mixed = 0;
        for (int Channel = 0; Channel < 4; ++Channel)
        {
            const float A = static_cast<float>((Top  >> (Channel * 8)) & 0xff);
            const float B = static_cast<float>((Foot >> (Channel * 8)) & 0xff);
            Mixed |= static_cast<ImU32>(A + (B - A) * Share + 0.5f) << (Channel * 8);
        }
        Vertex.col = Mixed;
    }
}

// No createRadialGradient in ImGui: concentric discs of falling alpha stand in for the preview halo.
inline void Halo(const Frame& F, float X, float Y, float Rx, float Ry, ImU32 Colour, float Opacity, int Steps = 26)
{
    for (int Step = Steps; Step >= 1; --Step)
    {
        const float Share = static_cast<float>(Step) / static_cast<float>(Steps);
        F.Draw->AddEllipseFilled(F.At(X, Y), { F.Span(Rx) * Share, F.Rise(Ry) * Share },
                                 Blend(Colour, Opacity / static_cast<float>(Steps)), 0.0f, 72);
    }
}

inline void Fixed(char* Out, size_t Size, double Value, int Decimals)
{
    std::snprintf(Out, Size, "%.*f", Decimals, Value);
}

// toLocaleString("en-US") — thousands separators, and for the bounds table at most three decimals with
//    trailing zeros dropped.
inline void Grouped(char* Out, size_t Size, double Value, int Decimals)
{
    char Plain[64];
    std::snprintf(Plain, sizeof(Plain), "%.*f", Decimals, Value);
    if (Decimals > 0)
    {
        char* Dot = std::strchr(Plain, '.');
        if (Dot)
        {
            char* Tail = Plain + std::strlen(Plain) - 1;
            while (Tail > Dot && *Tail == '0')
            {
                *Tail-- = 0;
            }
            if (Tail == Dot)
            {
                *Tail = 0;
            }
        }
    }
    const char* Source = Plain;
    char        Sign   = 0;
    if (*Source == '-')
    {
        Sign = '-';
        ++Source;
    }
    const char* Dot   = std::strchr(Source, '.');
    const size_t Whole = Dot ? static_cast<size_t>(Dot - Source) : std::strlen(Source);
    size_t Written = 0;
    if (Sign && Written + 1 < Size)
    {
        Out[Written++] = Sign;
    }
    for (size_t Index = 0; Index < Whole && Written + 1 < Size; ++Index)
    {
        if (Index > 0 && (Whole - Index) % 3 == 0)
        {
            Out[Written++] = ',';
        }
        Out[Written++] = Source[Index];
    }
    for (const char* Rest = Source + Whole; Rest && *Rest && Written + 1 < Size; ++Rest)
    {
        Out[Written++] = *Rest;
    }
    Out[Written] = 0;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     LETTER SPACING
//------------------------------------------------------------------------------------------------------------------------

// CSS letter-spacing adds a fixed advance after every glyph; ImGui has no such notion, so the run is
//    walked one codepoint at a time.
inline float Tracked(const Frame& F, float X, float Y, float Size, ImU32 Colour, const char* Body, float Extra,
                     Anchor Side = Anchor::Start, bool Baseline = false)
{
    const float  Lifted = Size * F.ScaleY;
    const float  Step   = Extra * F.ScaleX;
    ImFontBaked* Baked  = F.Face->GetFontBaked(Lifted);
    float Run = 0.0f;
    for (const char* Walk = Body; *Walk;)
    {
        unsigned int Point = 0;
        Walk += ImTextCharFromUtf8(&Point, Walk, nullptr);
        Run += Baked->GetCharAdvance(static_cast<ImWchar>(Point)) + Step;
    }
    Run = std::max(0.0f, Run - Step);
    ImVec2 Spot = F.At(X, Y);
    if (Baseline)
    {
        Spot.y -= Lifted * BaselineShare;
    }
    if (Side != Anchor::Start)
    {
        Spot.x -= Side == Anchor::Middle ? Run * 0.5f : Run;
    }
    for (const char* Walk = Body; *Walk;)
    {
        const char*  Head  = Walk;
        unsigned int Point = 0;
        Walk += ImTextCharFromUtf8(&Point, Walk, nullptr);
        F.Draw->AddText(F.Face, Lifted, Spot, Colour, Head, Walk);
        Spot.x += Baked->GetCharAdvance(static_cast<ImWchar>(Point)) + Step;
    }
    return Run;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      BEAM CHAMBER
//------------------------------------------------------------------------------------------------------------------------
// FogPanel.jsx FogBeamChamber — one Medium-owned chamber shared by Height, Atmospheric and Local Fog.

enum class FogKind { Height, Aerial, Local };

struct FogValues
{
    FogKind Kind       = FogKind::Height;
    bool    Enabled    = false;
    float   Density    = 0.02f;
    float   Falloff    = 400.0f;
    float   SunScatter = 0.6f;
    float   Start      = 50.0f;
    float   MieBlend   = 0.4f;
    float   Coverage   = 0.55f;
    float   Anisotropy = 0.6f;
    float   Tint[3]    = { 1.0f, 1.0f, 1.0f };
};

// LiveGraph.jsx FogDensity with AuthoredPreview set, so a disabled medium still previews its authored curve.
inline float Extinction(const FogValues& Medium, float Altitude)
{
    if (Medium.Kind == FogKind::Height)
    {
        return Medium.Density * std::exp(-std::max(0.0f, Altitude) / std::max(10.0f, Medium.Falloff));
    }
    return Medium.Kind == FogKind::Aerial ? Medium.Density * 0.001f
                                          : Medium.Density * Medium.Coverage * 0.01f;
}

inline const char* SpreadName(FogKind Kind)
{
    return Kind == FogKind::Local ? "Anisotropy" : Kind == FogKind::Height ? "Sun Scatter" : "Mie Blend";
}

inline float Spread(const FogValues& Medium)
{
    return std::max(0.0f, Medium.Kind == FogKind::Local  ? Medium.Anisotropy
                        : Medium.Kind == FogKind::Height ? Medium.SunScatter
                                                         : Medium.MieBlend);
}

constexpr float ChamberBody = 102.0f;                                      // [px] .fog-shared-beam svg height
inline float    ChamberHeight() { return 13 + 10 + 3 + 10 + 4 + ChamberBody + 8; }

// The whole chamber, container chrome included. Spot is the top-left of the bordered box.
inline float PaintChamber(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide, const FogValues& Medium)
{
    const float Tall = ChamberHeight();
    Draw->AddRectFilled(Spot, { Spot.x + Wide, Spot.y + Tall }, ChamberFill, 12.0f);
    Draw->AddLine({ Spot.x + 12.0f, Spot.y + 0.5f }, { Spot.x + Wide - 12.0f, Spot.y + 0.5f }, ChamberEdge, 1.0f);

    const bool  Live  = Medium.Enabled;
    const float Share = Medium.Enabled ? 1.0f : 0.82f;
    Frame Flat{ Draw, Face, Spot, 1.0f, 1.0f };

    Tracked(Flat, 12.0f, 13.0f, 8.0f, ChamberKey, "BEAM CHAMBER", 1.0f);
    Tracked(Flat, Wide - 12.0f, 13.0f, 8.0f, Live ? ChamberLive : ChamberIdle, Live ? "LIVE" : "PREVIEW", 1.0f,
            Anchor::End);

    char Line[96];
    if (Medium.Kind == FogKind::Local)
    {
        std::snprintf(Line, sizeof(Line), "Interior 2 m layer");
    }
    else if (Medium.Kind == FogKind::Height)
    {
        std::snprintf(Line, sizeof(Line), "Height medium \u00b7 25 m layer");
    }
    else
    {
        char Metres[32];
        Grouped(Metres, sizeof(Metres), std::max(0.0f, Medium.Start), 3);
        std::snprintf(Line, sizeof(Line), "Distance haze \u00b7 start %s m", Metres);
    }
    Draw->AddText(Face, 8.0f, { Spot.x + 12.0f, Spot.y + 26.0f }, ChamberSmall, Line);

    //  The 300 x 102 viewBox, letterboxed exactly as the default preserveAspectRatio does ----------------
    const Frame F = Meet(Draw, Face, { Spot.x + 12.0f, Spot.y + 40.0f }, { Wide - 24.0f, ChamberBody }, 300.0f,
                         ChamberBody);
    const float Thin = std::max(1.0f, F.ScaleX);

    const float Probe   = Medium.Kind == FogKind::Height ? 25.0f : 2.0f;
    const float Density = std::max(0.0f, Extinction(Medium, Probe));
    const float Blur    = Spread(Medium);
    const float Reach   = Medium.Kind == FogKind::Height ? 120.0f : 400.0f;
    const ImU32 Tint    = Medium.Kind == FogKind::Height ? FromParts(Medium.Tint, 1.0f)
                                                         : IM_COL32(212, 227, 236, 255);
    auto Through = [&](float Distance) { return std::exp(-Density * std::max(0.0f, Distance)); };

    for (float Row : { 26.0f, 51.0f, 76.0f })
    {
        Dashed(F, { 12.0f, Row }, { 288.0f, Row }, ChamberRule, 2.0f, 5.0f, Thin);
    }
    for (int Index = 0; Index < 70; ++Index)
    {
        const float Part    = static_cast<float>(Index) / 69.0f;
        const float Shown   = 0.32f + 0.68f * std::pow(Through(Part * Reach), 0.15f);
        const float Half    = 3.0f + Part * Part * (11.0f + Blur * 24.0f);
        const float Opacity = Share * (0.1f + Blur * 0.2f) * Shown;
        F.Draw->AddRectFilled(F.At(12.0f + Part * 272.0f, 51.0f - Half),
                              F.At(12.0f + Part * 272.0f + 4.2f, 51.0f + Half), Blend(Tint, Opacity));
    }
    F.Draw->AddLine(F.At(12.0f, 51.0f), F.At(288.0f, 40.0f - Blur * 10.0f), Blend(Tint, Share * 0.42f), Thin);
    F.Draw->AddLine(F.At(12.0f, 51.0f), F.At(288.0f, 62.0f + Blur * 10.0f), Blend(Tint, Share * 0.42f), Thin);
    F.Draw->AddLine(F.At(12.0f, 51.0f), F.At(288.0f, 51.0f), Blend(Tint, Share * 0.82f), 1.6f * Thin);

    const bool  Clear = Density <= 0.0f;
    const float Range = Clear ? 0.0f : -std::log(0.02f) / Density;
    if (!Clear)
    {
        const float Mark = 12.0f + std::clamp(Range / Reach, 0.0f, 1.0f) * 276.0f;
        Dashed(F, { Mark, 18.0f }, { Mark, 84.0f }, ChamberMark, 3.0f, 3.0f, Thin);
    }
    F.Draw->AddCircleFilled(F.At(12.0f, 51.0f), F.Span(4.0f), Blend(ChamberLamp, Share), 24);

    char Body[96], Figure[48];
    Fixed(Figure, sizeof(Figure), Blur, 2);
    char Upper[32];
    std::snprintf(Upper, sizeof(Upper), "%s", SpreadName(Medium.Kind));
    for (char* Walk = Upper; *Walk; ++Walk)
    {
        *Walk = static_cast<char>(std::toupper(static_cast<unsigned char>(*Walk)));
    }
    std::snprintf(Body, sizeof(Body), "%s \u00b7 %s", Upper, Figure);
    Tracked(F, 10.0f, 13.0f, 8.0f, ChamberText, Body, 0.2f, Anchor::Start, true);

    if (Clear)
    {
        std::snprintf(Body, sizeof(Body), "2%% \u00b7 CLEAR");
    }
    else if (Range >= 1000.0f)
    {
        Fixed(Figure, sizeof(Figure), Range / 1000.0f, 1);
        std::snprintf(Body, sizeof(Body), "2%% \u00b7 %s km", Figure);
    }
    else
    {
        Fixed(Figure, sizeof(Figure), Range, 0);
        std::snprintf(Body, sizeof(Body), "2%% \u00b7 %s m", Figure);
    }
    Tracked(F, 290.0f, 13.0f, 8.0f, ChamberText, Body, 0.2f, Anchor::End, true);

    Fixed(Figure, sizeof(Figure), Through(Reach) * 100.0f, 0);
    char Metres[32];
    Grouped(Metres, sizeof(Metres), Reach, 0);
    std::snprintf(Body, sizeof(Body), "%s%% PHYSICAL AT %s m", Figure, Metres);
    Tracked(F, 290.0f, 96.0f, 8.0f, ChamberText, Body, 0.2f, Anchor::End, true);
    return Tall;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    DENSITY PROFILE
//------------------------------------------------------------------------------------------------------------------------
// HeightFogVisual.jsx — the authored Height Fog density profile, draggable in both axes at once.

constexpr float ProfileLeft   = 34.0f;    // [px] plot box inside the 300 x 210 viewBox
constexpr float ProfileRight  = 286.0f;
constexpr float ProfileTop    = 20.0f;
constexpr float ProfileBottom = 174.0f;
constexpr float ProfileBody   = 220.0f;   // [px] .height-density-visual > svg height
constexpr float ProfileCeil   = 0.2f;     // [1/m] the plotted density ceiling
constexpr float ProfileSky    = 3000.0f;  // [m] the plotted altitude ceiling

inline const char* ProfileNote()
{
    return "Drag the profile point to author the same Density and Falloff Height values used by Medium, "
           "Visibility and Beam Chamber.";
}

inline float Lines(ImFont* Face, float Size, float Wide, const char* Body)
{
    const float Stacked = Face->CalcTextSizeA(Size, FLT_MAX, Wide, Body).y;
    return std::max(1.0f, std::round(Stacked / Size)) * Size * 1.55f;
}

inline float ProfileHeight(ImFont* Face, float Wide)
{
    return 10 + 12 + 36 + 12 + ProfileBody + 12 + 51 + 12 + Lines(Face, 9.0f, Wide, ProfileNote());
}

// Spot is the top-left of the .height-density-visual grid. Dragging writes through the two out slots.
inline float PaintProfile(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide, const FogValues& Medium,
                          float* DensitySlot = nullptr, float* FalloffSlot = nullptr)
{
    const bool  Live    = Medium.Enabled;
    const float Density = std::max(0.0f, Medium.Density);
    const float Falloff = std::max(10.0f, Medium.Falloff);
    const ImU32 Tint    = FromParts(Medium.Tint, 1.0f);
    auto Curve = [&](float Altitude) { return Density * std::exp(-Altitude / Falloff); };

    Frame Flat{ Draw, Face, Spot, 1.0f, 1.0f };
    char  Body[128], Figure[48];

    //  .height-density-status ---------------------------------------------------------------------------
    Draw->AddCircleFilled({ Spot.x + 3.0f, Spot.y + 5.0f }, 3.0f, Live ? ProfileLive : ProfileIdle, 16);
    Tracked(Flat, 13.0f, 0.0f, 8.0f, ProfileKey, Live ? "LIVE DENSITY PROFILE" : "AUTHORED PREVIEW", 0.9f);
    Grouped(Figure, sizeof(Figure), Falloff, 0);
    std::snprintf(Body, sizeof(Body), "%s m falloff", Figure);
    Draw->AddText(Face, 8.0f, { Spot.x + Wide - Measured(Face, 8.0f, Body), Spot.y }, ProfileHead, Body);

    //  .height-density-metric ---------------------------------------------------------------------------
    const float MetricTop = Spot.y + 22.0f;
    Fixed(Body, sizeof(Body), Density, 4);
    const float Run = Tracked(Flat, 0.0f, MetricTop - Spot.y, 30.0f, ProfileValue, Body, -1.2f);
    Draw->AddText(Face, 10.0f, { Spot.x + Run + 8.0f, MetricTop + (30.0f - 10.0f) * BaselineShare },
                  ProfileUnit, "m\u207b\u00b9 datum density");

    //  The 300 x 210 viewBox, stretched because the element asks for preserveAspectRatio="none" ---------
    const ImVec2 Box{ Spot.x, MetricTop + 36.0f + 12.0f };
    Draw->AddRectFilled(Box, { Box.x + Wide, Box.y + ProfileBody }, ProfileFill, 12.0f);
    Draw->AddRect(Box, { Box.x + Wide, Box.y + ProfileBody }, ProfileEdge, 12.0f);
    const Frame F = Stretch(Draw, Face, Box, { Wide, ProfileBody }, 300.0f, 210.0f);
    const float Thin = std::max(1.0f, F.ScaleY);
    auto Across = [&](float Altitude)
    { return ProfileLeft + (Altitude / ProfileSky) * (ProfileRight - ProfileLeft); };
    auto Up = [&](float Value)
    { return ProfileBottom - (Value / ProfileCeil) * (ProfileBottom - ProfileTop); };

    for (float Value : { 0.0f, 0.05f, 0.1f, 0.15f, 0.2f })
    {
        const float Row = Up(Value);
        if (Value > 0.0f)
        {
            Dashed(F, { ProfileLeft, Row }, { ProfileRight, Row }, ProfileRule, 2.0f, 5.0f, Thin);
        }
        else
        {
            F.Draw->AddLine(F.At(ProfileLeft, Row), F.At(ProfileRight, Row), ProfileRule, Thin);
        }
        Fixed(Figure, sizeof(Figure), Value, Value > 0.0f ? 2 : 0);
        Tracked(F, ProfileLeft - 6.0f, Row + 3.0f, 8.0f, ProfileText, Figure, 0.25f, Anchor::End, true);
    }
    for (float Altitude : { 0.0f, 1000.0f, 2000.0f, 3000.0f })
    {
        const float Column = Across(Altitude);
        F.Draw->AddLine(F.At(Column, ProfileTop), F.At(Column, ProfileBottom), ProfileTick, Thin);
        if (Altitude > 0.0f)
        {
            Fixed(Figure, sizeof(Figure), Altitude / 1000.0f, 0);
            std::snprintf(Body, sizeof(Body), "%s km", Figure);
        }
        else
        {
            std::snprintf(Body, sizeof(Body), "0 m");
        }
        Tracked(F, Column, 193.0f, 8.0f, ProfileText, Body, 0.25f,
                Altitude == 0.0f ? Anchor::Start : Altitude >= ProfileSky ? Anchor::End : Anchor::Middle, true);
    }

    //  The filled area carries a vertical gradient over the bounding box of the filled path itself ------
    const int First = F.Draw->VtxBuffer.Size;
    for (int Index = 1; Index <= 60; ++Index)
    {
        const float A = static_cast<float>(Index - 1) * 50.0f, B = static_cast<float>(Index) * 50.0f;
        F.Draw->AddQuadFilled(F.At(Across(A), Up(Curve(A))), F.At(Across(B), Up(Curve(B))),
                              F.At(Across(B), ProfileBottom), F.At(Across(A), ProfileBottom), IM_COL32_WHITE);
    }
    ShadeColumn(F.Draw, First, F.Draw->VtxBuffer.Size, F.At(0.0f, Up(Density)).y, F.At(0.0f, ProfileBottom).y,
                Blend(Tint, Live ? 0.42f : 0.28f), Blend(Tint, 0.025f));
    for (int Index = 0; Index <= 60; ++Index)
    {
        const float Altitude = static_cast<float>(Index) * 50.0f;
        F.Draw->PathLineTo(F.At(Across(Altitude), Up(Curve(Altitude))));
    }
    F.Draw->PathStroke(Tint, 0, 1.7f * Thin);

    const float HandleX = Across(Falloff), HandleY = Up(Curve(Falloff));
    Dashed(F, { HandleX, ProfileTop }, { HandleX, ProfileBottom }, ProfileDrop, 4.0f, 4.0f, Thin);
    F.Draw->AddCircleFilled(F.At(HandleX, HandleY), 5.0f * F.ScaleY, Tint, 24);
    F.Draw->AddCircle(F.At(HandleX, HandleY), 5.0f * F.ScaleY, ProfileRing, 24, 2.0f * Thin);
    Fixed(Figure, sizeof(Figure), Falloff, 0);
    std::snprintf(Body, sizeof(Body), "37%% \u00b7 %s m", Figure);
    Tracked(F, std::min(ProfileRight - 4.0f, HandleX + 8.0f), std::max(14.0f, HandleY - 9.0f), 8.0f, ProfileText,
            Body, 0.25f, HandleX > 230.0f ? Anchor::End : Anchor::Start, true);
    Tracked(F, ProfileLeft, 207.0f, 8.0f, ProfileText, "DRAG POINT \u00b7 DENSITY / FALLOFF", 0.25f,
            Anchor::Start, true);

    //  onPointerDown / onPointerMove with pointer capture, which is an active InvisibleButton here ------
    if (DensitySlot || FalloffSlot)
    {
        ImGui::SetCursorScreenPos(Box);
        ImGui::InvisibleButton("##height-density", { Wide, ProfileBody });
        if (ImGui::IsItemActive())
        {
            const ImVec2 Mouse = ImGui::GetIO().MousePos;
            const float  Px    = (Mouse.x - Box.x) / std::max(1.0f, Wide) * 300.0f;
            const float  Py    = (Mouse.y - Box.y) / ProfileBody * 210.0f;
            if (FalloffSlot)
            {
                *FalloffSlot = std::round(10.0f + (std::clamp(Px, ProfileLeft, ProfileRight) - ProfileLeft) /
                                                      (ProfileRight - ProfileLeft) * 2990.0f);
            }
            if (DensitySlot)
            {
                const float Next = (ProfileBottom - std::clamp(Py, ProfileTop, ProfileBottom)) /
                                   (ProfileBottom - ProfileTop) * ProfileCeil;
                *DensitySlot = std::round(Next * 10000.0f) / 10000.0f;
            }
        }
    }

    //  .height-density-readings -------------------------------------------------------------------------
    const float ReadTop  = Box.y + ProfileBody + 12.0f;
    const float ReadWide = (Wide - 12.0f) * 0.5f;
    const char* Keys[2]  = { "AT FALLOFF HEIGHT", "AT 2\u00d7 FALLOFF" };
    const float Values[2] = { Curve(Falloff), Curve(Falloff * 2.0f) };
    for (int Tile = 0; Tile < 2; ++Tile)
    {
        const float Edge = Spot.x + static_cast<float>(Tile) * (ReadWide + 12.0f);
        Draw->AddRectFilled({ Edge, ReadTop }, { Edge + ReadWide, ReadTop + 51.0f }, ReadingFill, 10.0f);
        Draw->AddText(Face, 8.0f, { Edge + 11.0f, ReadTop + 10.0f }, ReadingKey, Keys[Tile]);
        Fixed(Figure, sizeof(Figure), Values[Tile], 4);
        std::snprintf(Body, sizeof(Body), "%s m\u207b\u00b9", Figure);
        Draw->AddText(Face, 13.0f, { Edge + 11.0f, ReadTop + 25.0f }, ReadingValue, Body);
    }

    const float NoteTop = ReadTop + 51.0f + 12.0f;
    Draw->AddText(Face, 9.0f, { Spot.x, NoteTop }, ProfileUnit, ProfileNote(), nullptr, Wide);
    return NoteTop + Lines(Face, 9.0f, Wide, ProfileNote()) - Spot.y;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      VOLUME SHAPE
//------------------------------------------------------------------------------------------------------------------------
// FogShape.js — the authored volume, its wireframe and the world bounds it derives.

enum class FogForm { Box, Sphere, Ellipsoid, Diamond, Cylinder, Cone, Prism, Custom };

constexpr int kMaxOutlinePoints = 32;

struct FogShape
{
    FogForm Form   = FogForm::Box;
    float   Wide   = 200.0f;
    float   Deep   = 200.0f;
    float   Tall   = 200.0f;
    float   Radius = 100.0f;
    int     Sides  = 6;
    float   Outline[kMaxOutlinePoints][2] = { { -100, -70 }, { 30, -100 }, { 100, -10 }, { 50, 100 }, { -80, 60 } };
    int     OutlineCount = 5;
};

inline const char* FormName(FogForm Form)
{
    switch (Form)
    {
        case FogForm::Box:       return "Box";
        case FogForm::Sphere:    return "Sphere";
        case FogForm::Ellipsoid: return "Ellipsoid";
        case FogForm::Diamond:   return "Diamond";
        case FogForm::Cylinder:  return "Cylinder";
        case FogForm::Cone:      return "Cone";
        case FogForm::Prism:     return "Prism";
        default:                 return "Custom";
    }
}

// The <option> labels; the four unlisted forms show their bare name, exactly as the lookup falls through.
inline const char* FormLabel(FogForm Form)
{
    switch (Form)
    {
        case FogForm::Box:     return "Box / rectangular volume";
        case FogForm::Diamond: return "Diamond / octahedron";
        case FogForm::Prism:   return "Regular polygon prism";
        case FogForm::Custom:  return "Custom polygon extrusion";
        default:               return FormName(Form);
    }
}

// ValidFogPolygon — ordered, no crossing edges, and an area worth extruding.
inline bool ValidOutline(const float Points[][2], int Count)
{
    auto Cross = [](const float* A, const float* B, const float* C)
    { return (B[0] - A[0]) * (C[1] - A[1]) - (B[1] - A[1]) * (C[0] - A[0]); };
    auto Over = [&](const float* A, const float* B, const float* C)
    {
        return std::fabs(Cross(A, B, C)) < 1e-7f && C[0] >= std::min(A[0], B[0]) && C[0] <= std::max(A[0], B[0]) &&
               C[1] >= std::min(A[1], B[1]) && C[1] <= std::max(A[1], B[1]);
    };
    float Area = 0.0f;
    for (int I = 0; I < Count; ++I)
    {
        const float* A = Points[I];
        const float* B = Points[(I + 1) % Count];
        Area += A[0] * B[1] - B[0] * A[1];
        if (std::hypot(A[0] - B[0], A[1] - B[1]) < 0.001f)
        {
            return false;
        }
        for (int J = I + 1; J < Count; ++J)
        {
            if (J == I + 1 || (I == 0 && J == Count - 1))
            {
                continue;
            }
            const float* C = Points[J];
            const float* D = Points[(J + 1) % Count];
            if ((Cross(A, B, C) * Cross(A, B, D) < 0.0f && Cross(C, D, A) * Cross(C, D, B) < 0.0f) ||
                Over(A, B, C) || Over(A, B, D) || Over(C, D, A) || Over(C, D, B))
            {
                return false;
            }
        }
    }
    return std::fabs(Area) > 0.001f;
}

struct ShapeMesh
{
    std::vector<std::array<float, 3>> Vertices;
    std::vector<std::array<int, 2>>   Edges;
    std::vector<std::vector<int>>     Faces;
    float Low[3]{};
    float High[3]{};
    float Size[3]{};
};

// FogGeometry — every branch in source order so the vertex indices match the browser one for one.
inline ShapeMesh ConstructMesh(const FogShape& Shape)
{
    ShapeMesh Mesh;
    auto Extrude = [&](const std::vector<std::array<float, 2>>& Ring)
    {
        const int Count = static_cast<int>(Ring.size());
        for (const auto& Point : Ring)
        {
            Mesh.Vertices.push_back({ Point[0], Point[1], -Shape.Tall / 2.0f });
        }
        for (const auto& Point : Ring)
        {
            Mesh.Vertices.push_back({ Point[0], Point[1], Shape.Tall / 2.0f });
        }
        for (int I = 0; I < Count; ++I)
        {
            const int J = (I + 1) % Count;
            Mesh.Edges.push_back({ I, J });
            Mesh.Edges.push_back({ I + Count, J + Count });
            Mesh.Edges.push_back({ I, I + Count });
            Mesh.Faces.push_back({ I, J, J + Count, I + Count });
        }
        std::vector<int> Floor, Roof;
        for (int I = 0; I < Count; ++I)
        {
            Floor.push_back(I);
            Roof.push_back(I + Count);
        }
        Mesh.Faces.push_back(Floor);
        Mesh.Faces.push_back(Roof);
    };

    if (Shape.Form == FogForm::Box)
    {
        Extrude({ { -Shape.Wide / 2.0f, -Shape.Deep / 2.0f },
                  {  Shape.Wide / 2.0f, -Shape.Deep / 2.0f },
                  {  Shape.Wide / 2.0f,  Shape.Deep / 2.0f },
                  { -Shape.Wide / 2.0f,  Shape.Deep / 2.0f } });
    }
    else if (Shape.Form == FogForm::Custom)
    {
        std::vector<std::array<float, 2>> Ring;
        for (int I = 0; I < Shape.OutlineCount; ++I)
        {
            Ring.push_back({ Shape.Outline[I][0], Shape.Outline[I][1] });
        }
        Extrude(Ring);
    }
    else if (Shape.Form == FogForm::Prism || Shape.Form == FogForm::Cylinder)
    {
        const int Count = Shape.Form == FogForm::Prism ? Shape.Sides : 48;
        std::vector<std::array<float, 2>> Ring;
        for (int I = 0; I < Count; ++I)
        {
            const float Angle = static_cast<float>(I) * 2.0f * Pi / static_cast<float>(Count);
            Ring.push_back({ Shape.Radius * std::cos(Angle), Shape.Radius * std::sin(Angle) });
        }
        Extrude(Ring);
    }
    else if (Shape.Form == FogForm::Cone)
    {
        for (int I = 0; I < 48; ++I)
        {
            const float Angle = static_cast<float>(I) * Pi / 24.0f;
            Mesh.Vertices.push_back({ Shape.Radius * std::cos(Angle), Shape.Radius * std::sin(Angle),
                                      -Shape.Tall / 2.0f });
        }
        Mesh.Vertices.push_back({ 0.0f, 0.0f, Shape.Tall / 2.0f });
        for (int I = 0; I < 48; ++I)
        {
            Mesh.Edges.push_back({ I, (I + 1) % 48 });
            if (I % 6 == 0)
            {
                Mesh.Edges.push_back({ I, 48 });
            }
            Mesh.Faces.push_back({ I, (I + 1) % 48, 48 });
        }
    }
    else if (Shape.Form == FogForm::Diamond)
    {
        Mesh.Vertices = { {  Shape.Wide / 2.0f, 0.0f, 0.0f }, { 0.0f,  Shape.Deep / 2.0f, 0.0f },
                          { -Shape.Wide / 2.0f, 0.0f, 0.0f }, { 0.0f, -Shape.Deep / 2.0f, 0.0f },
                          { 0.0f, 0.0f, Shape.Tall / 2.0f },  { 0.0f, 0.0f, -Shape.Tall / 2.0f } };
        for (int I = 0; I < 4; ++I)
        {
            Mesh.Edges.push_back({ I, (I + 1) % 4 });
            Mesh.Edges.push_back({ I, 4 });
            Mesh.Edges.push_back({ I, 5 });
            Mesh.Faces.push_back({ I, (I + 1) % 4, 4 });
            Mesh.Faces.push_back({ I, (I + 1) % 4, 5 });
        }
    }
    else
    {
        const bool  Round    = Shape.Form == FogForm::Sphere;
        const float Radii[3] = { Round ? Shape.Radius : Shape.Wide / 2.0f,
                                 Round ? Shape.Radius : Shape.Deep / 2.0f,
                                 Round ? Shape.Radius : Shape.Tall / 2.0f };
        for (int Latitude = 0; Latitude <= 12; ++Latitude)
        {
            for (int Longitude = 0; Longitude < 24; ++Longitude)
            {
                const float A = static_cast<float>(Latitude) * Pi / 12.0f;
                const float B = static_cast<float>(Longitude) * Pi / 12.0f;
                Mesh.Vertices.push_back({ Radii[0] * std::sin(A) * std::cos(B),
                                          Radii[1] * std::sin(A) * std::sin(B), Radii[2] * std::cos(A) });
                const int I = Latitude * 24 + Longitude;
                const int J = Latitude * 24 + (Longitude + 1) % 24;
                Mesh.Edges.push_back({ I, J });
                if (Latitude < 12)
                {
                    if (Longitude % 3 == 0)
                    {
                        Mesh.Edges.push_back({ I, I + 24 });
                    }
                    Mesh.Faces.push_back({ I, J, J + 24, I + 24 });
                }
            }
        }
    }

    for (int Axis = 0; Axis < 3; ++Axis)
    {
        float Least = FLT_MAX, Most = -FLT_MAX;
        for (const auto& Point : Mesh.Vertices)
        {
            Least = std::min(Least, Point[Axis]);
            Most  = std::max(Most, Point[Axis]);
        }
        Mesh.Low[Axis]  = Least;
        Mesh.High[Axis] = Most;
        Mesh.Size[Axis] = Most - Least;
    }
    return Mesh;
}

// Ear clipping, because AddConvexPolyFilled silently misdraws the concave caps a Custom outline extrudes.
inline void FillOutline(ImDrawList* Draw, const std::vector<ImVec2>& Ring, ImU32 Colour)
{
    const int Total = static_cast<int>(Ring.size());
    if (Total < 3)
    {
        return;
    }
    auto Cross = [](ImVec2 A, ImVec2 B, ImVec2 C)
    { return (B.x - A.x) * (C.y - A.y) - (B.y - A.y) * (C.x - A.x); };
    float Area = 0.0f;
    for (int I = 0; I < Total; ++I)
    {
        Area += Ring[I].x * Ring[(I + 1) % Total].y - Ring[(I + 1) % Total].x * Ring[I].y;
    }
    const float Facing = Area >= 0.0f ? 1.0f : -1.0f;
    std::vector<int> Rest(Total);
    for (int I = 0; I < Total; ++I)
    {
        Rest[I] = I;
    }
    for (int Guard = 0; static_cast<int>(Rest.size()) > 3 && Guard < Total * Total; ++Guard)
    {
        bool Clipped = false;
        for (int I = 0; I < static_cast<int>(Rest.size()); ++I)
        {
            const int Count = static_cast<int>(Rest.size());
            const int P = Rest[(I + Count - 1) % Count], Q = Rest[I], R = Rest[(I + 1) % Count];
            if (Cross(Ring[P], Ring[Q], Ring[R]) * Facing <= 0.0f)
            {
                continue;
            }
            bool Clean = true;
            for (int J : Rest)
            {
                if (J == P || J == Q || J == R)
                {
                    continue;
                }
                if (Cross(Ring[P], Ring[Q], Ring[J]) * Facing >= 0.0f &&
                    Cross(Ring[Q], Ring[R], Ring[J]) * Facing >= 0.0f &&
                    Cross(Ring[R], Ring[P], Ring[J]) * Facing >= 0.0f)
                {
                    Clean = false;
                    break;
                }
            }
            if (!Clean)
            {
                continue;
            }
            Draw->AddTriangleFilled(Ring[P], Ring[Q], Ring[R], Colour);
            Rest.erase(Rest.begin() + I);
            Clipped = true;
            break;
        }
        if (!Clipped)
        {
            break;
        }
    }
    if (Rest.size() == 3)
    {
        Draw->AddTriangleFilled(Ring[Rest[0]], Ring[Rest[1]], Ring[Rest[2]], Colour);
    }
}

inline const char* VolumeNote()
{
    return "Shape and dimensions determine the bounds\u2014not a fixed box. The preview fits its volume; centre "
           "changes update the world bounds. HTML authoring only; native fog masking is deferred.";
}

inline const char* OutlineNote()
{
    return "Ordered polygon, extruded along Z. Supports concave footprints without crossing edges; this is not an "
           "arbitrary mesh/SDF importer.";
}

// Row heights straight off FogShapePanel.css; the caller draws the card and its h3, this draws the body.
inline float VolumeHeight(ImFont* Face, float Wide, const FogShape& Shape)
{
    int Cells = 0;
    if (Shape.Form == FogForm::Sphere || Shape.Form == FogForm::Cylinder || Shape.Form == FogForm::Cone ||
        Shape.Form == FogForm::Prism)
    {
        Cells = 1;
    }
    else if (Shape.Form != FogForm::Custom)
    {
        Cells = 2;
    }
    Cells += Shape.Form == FogForm::Sphere ? 0 : 1;
    Cells += Shape.Form == FogForm::Prism ? 1 : 0;
    const int   Rows  = (Cells + 1) / 2;
    const float Grid  = Rows > 0 ? static_cast<float>(Rows) * 56.0f + static_cast<float>(Rows - 1) * 12.0f : 0.0f;
    const float Extra = Shape.Form == FogForm::Custom
                            ? 15.0f + 10.0f + 120.0f + 12.0f + 28.0f + 12.0f + Lines(Face, 11.0f, Wide, OutlineNote())
                            : 0.0f;
    return 15.0f + 10.0f + 26.0f + 18.0f + 12.0f + Wide * 250.0f / 320.0f + 20.0f + Grid + 24.0f + Extra + 24.0f +
           26.0f + 32.0f + 24.0f + 20.0f + 16.0f + 120.0f + 14.0f + Lines(Face, 11.0f, Wide, VolumeNote());
}

// Spot is the top-left of the card body. Cycle advances the <select> when the user clicks it.
inline float PaintVolume(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide, const FogShape& Shape,
                         const float Centre[3], FogShape* Cycle = nullptr)
{
    const ShapeMesh Mesh = ConstructMesh(Shape);
    Frame Flat{ Draw, Face, Spot, 1.0f, 1.0f };
    char  Body[192], Figure[64];
    float Y = Spot.y;

    //  .fog-shape-choice ---------------------------------------------------------------------------------
    Draw->AddText(Face, 12.0f, { Spot.x, Y }, ChoiceText, "Shape");
    Y += 15.0f + 10.0f;
    Draw->AddRectFilled({ Spot.x, Y }, { Spot.x + Wide, Y + 26.0f }, IM_COL32(0, 0, 0, 255), 4.0f);
    Draw->AddRect({ Spot.x, Y }, { Spot.x + Wide, Y + 26.0f }, IM_COL32(255, 255, 255, 13), 4.0f);
    Draw->AddText(Face, 12.0f, { Spot.x + 9.0f, Y + 6.0f }, ChoiceText, FormLabel(Shape.Form));
    Draw->AddTriangleFilled({ Spot.x + Wide - 20.0f, Y + 11.0f }, { Spot.x + Wide - 10.0f, Y + 11.0f },
                            { Spot.x + Wide - 15.0f, Y + 16.0f }, ChoiceText);
    if (Cycle)
    {
        ImGui::SetCursorScreenPos({ Spot.x, Y });
        if (ImGui::InvisibleButton("##fog-shape-choice", { Wide, 26.0f }))
        {
            Cycle->Form = static_cast<FogForm>((static_cast<int>(Shape.Form) + 1) % 8);
        }
    }
    Y += 26.0f + 18.0f;

    //  .fog-shape-preview — the 320 x 250 viewBox at width 100% and height auto -------------------------
    Y += 12.0f;
    const float Deep = Wide * 250.0f / 320.0f;
    const Frame F    = Meet(Draw, Face, { Spot.x, Y }, { Wide, Deep }, 320.0f, 250.0f);
    Draw->AddRectFilled(F.At(0.0f, 0.0f), F.At(320.0f, 250.0f), VolumeFill, 15.0f * F.ScaleX);
    Halo(F, 160.0f, 125.0f, 155.0f, 112.0f, VolumeGlow, 0.16f);

    std::vector<ImVec2> Flattened(Mesh.Vertices.size());
    float Least[2] = { FLT_MAX, FLT_MAX }, Most[2] = { -FLT_MAX, -FLT_MAX };
    for (size_t Index = 0; Index < Mesh.Vertices.size(); ++Index)
    {
        const auto& Point = Mesh.Vertices[Index];
        Flattened[Index]  = { (Point[0] - Point[1]) * 0.70710678f,
                              (Point[0] + Point[1]) / std::sqrt(6.0f) - Point[2] * std::sqrt(2.0f / 3.0f) };
        Least[0] = std::min(Least[0], Flattened[Index].x);
        Least[1] = std::min(Least[1], Flattened[Index].y);
        Most[0]  = std::max(Most[0], Flattened[Index].x);
        Most[1]  = std::max(Most[1], Flattened[Index].y);
    }
    const float Zoom = std::min(270.0f / std::max(0.001f, Most[0] - Least[0]),
                                190.0f / std::max(0.001f, Most[1] - Least[1]));
    std::vector<ImVec2> Shown(Flattened.size());
    for (size_t Index = 0; Index < Flattened.size(); ++Index)
    {
        Shown[Index] = F.At(160.0f + (Flattened[Index].x - (Least[0] + Most[0]) / 2.0f) * Zoom,
                            120.0f + (Flattened[Index].y - (Least[1] + Most[1]) / 2.0f) * Zoom);
    }
    for (const auto& Face3 : Mesh.Faces)
    {
        std::vector<ImVec2> Ring;
        Ring.reserve(Face3.size());
        for (int Index : Face3)
        {
            Ring.push_back(Shown[static_cast<size_t>(Index)]);
        }
        FillOutline(Draw, Ring, VolumeFace);
    }
    const float Fade = Shape.Form == FogForm::Sphere || Shape.Form == FogForm::Ellipsoid ? 0.4f : 0.65f;
    for (const auto& Line : Mesh.Edges)
    {
        Draw->AddLine(Shown[static_cast<size_t>(Line[0])], Shown[static_cast<size_t>(Line[1])],
                      Blend(VolumeWire, Fade), 0.65f * F.ScaleX);
    }
    std::snprintf(Body, sizeof(Body), "%s", FormName(Shape.Form));
    for (char* Walk = Body; *Walk; ++Walk)
    {
        *Walk = static_cast<char>(std::toupper(static_cast<unsigned char>(*Walk)));
    }
    std::strcat(Body, " \u00b7 Z UP \u00b7 FIT TO VOLUME");
    Inked(F, 14.0f, 232.0f, 9.0f, VolumeStamp, Body);
    Draw->AddRect({ Spot.x, Y }, { Spot.x + Wide, Y + Deep }, VolumeEdge, 15.0f);
    Y += Deep + 20.0f;

    //  .fog-dimensions — two equal columns ---------------------------------------------------------------
    struct Cell { const char* Name; float Value; int Decimals; };
    std::vector<Cell> Cells;
    if (Shape.Form == FogForm::Sphere || Shape.Form == FogForm::Cylinder || Shape.Form == FogForm::Cone ||
        Shape.Form == FogForm::Prism)
    {
        Cells.push_back({ "Radius (m)", Shape.Radius, 2 });
    }
    else if (Shape.Form != FogForm::Custom)
    {
        Cells.push_back({ "Width (m)", Shape.Wide, 2 });
        Cells.push_back({ "Depth (m)", Shape.Deep, 2 });
    }
    if (Shape.Form != FogForm::Sphere)
    {
        Cells.push_back({ "Height (m)", Shape.Tall, 2 });
    }
    if (Shape.Form == FogForm::Prism)
    {
        Cells.push_back({ "Sides", static_cast<float>(Shape.Sides), 0 });
    }
    const float Column = (Wide - 12.0f) * 0.5f;
    for (size_t Index = 0; Index < Cells.size(); ++Index)
    {
        const float Left = Spot.x + static_cast<float>(Index % 2) * (Column + 12.0f);
        const float Head = Y + static_cast<float>(Index / 2) * 68.0f;
        Draw->AddText(Face, 11.0f, { Left, Head }, EntryKey, Cells[Index].Name);
        Draw->AddRectFilled({ Left, Head + 21.0f }, { Left + Column, Head + 56.0f }, EntryFill, 9.0f);
        Draw->AddRect({ Left, Head + 21.0f }, { Left + Column, Head + 56.0f }, EntryEdge, 9.0f);
        Grouped(Figure, sizeof(Figure), Cells[Index].Value, Cells[Index].Decimals);
        Draw->AddText(Face, 11.0f, { Left + 10.0f, Head + 32.0f }, EntryText, Figure);
    }
    const int Rows = (static_cast<int>(Cells.size()) + 1) / 2;
    Y += Rows > 0 ? static_cast<float>(Rows) * 56.0f + static_cast<float>(Rows - 1) * 12.0f : 0.0f;
    Y += 24.0f;

    //  .fog-custom — only the Custom outline carries an editor ------------------------------------------
    if (Shape.Form == FogForm::Custom)
    {
        Draw->AddText(Face, 12.0f, { Spot.x, Y }, ChoiceText, "Footprint vertices \u00b7 X, Y metres");
        Y += 25.0f;
        Draw->AddRectFilled({ Spot.x, Y }, { Spot.x + Wide, Y + 120.0f }, EntryFill, 10.0f);
        Draw->AddRect({ Spot.x, Y }, { Spot.x + Wide, Y + 120.0f }, IM_COL32(255, 255, 255, 21), 10.0f);
        for (int Index = 0; Index < Shape.OutlineCount && Index < 8; ++Index)
        {
            char Across[32], Along[32];
            Grouped(Across, sizeof(Across), Shape.Outline[Index][0], 3);
            Grouped(Along, sizeof(Along), Shape.Outline[Index][1], 3);
            std::snprintf(Body, sizeof(Body), "%s, %s", Across, Along);
            Draw->AddText(Face, 11.0f, { Spot.x + 12.0f, Y + 12.0f + static_cast<float>(Index) * 14.0f },
                          IM_COL32(189, 207, 211, 255), Body);
        }
        Y += 120.0f + 12.0f;
        const float Run = Measured(Face, 11.0f, "Apply footprint") + 24.0f;
        Draw->AddRectFilled({ Spot.x, Y }, { Spot.x + Run, Y + 28.0f }, IM_COL32(34, 34, 34, 255), 16.0f);
        Draw->AddRect({ Spot.x, Y }, { Spot.x + Run, Y + 28.0f }, IM_COL32(255, 255, 255, 13), 16.0f);
        Draw->AddText(Face, 11.0f, { Spot.x + 12.0f, Y + 7.0f }, ChoiceText, "Apply footprint");
        Y += 28.0f + 12.0f;
        Draw->AddText(Face, 11.0f, { Spot.x, Y }, NoteText, OutlineNote(), nullptr, Wide);
        Y += Lines(Face, 11.0f, Wide, OutlineNote());
    }

    //  TransformPanel.jsx Compact with one Centre row, WORLD SPACE ---------------------------------------
    Y += 24.0f;
    Draw->AddText(Face, 14.0f, { Spot.x, Y }, IM_COL32(222, 222, 222, 255), "Transform");
    Inked(Flat, Wide, Y - Spot.y + 12.0f, 8.0f, BoundsKey, "WORLD SPACE", Anchor::End);
    Y += 26.0f;
    {
        const float Label  = 58.0f;
        const float Gutter = 20.0f;
        const float Pane   = (Wide - Label - Gutter - 8.0f) / 3.0f;
        Draw->AddText(Face, 10.0f, { Spot.x, Y + 8.0f }, RowLabel, "Centre");
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            const float Left = Spot.x + Label + static_cast<float>(Axis) * (Pane + 4.0f);
            Draw->AddRectFilled({ Left, Y }, { Left + Pane, Y + 26.0f }, IM_COL32(0, 0, 0, 255), 7.0f);
            Fixed(Figure, sizeof(Figure), Centre[Axis], 2);
            Inked(Flat, Left + Pane - 7.0f - Spot.x, Y + 17.0f - Spot.y, 11.0f, IM_COL32(201, 201, 201, 255),
                  Figure, Anchor::End);
        }
        Inked(Flat, Wide, Y + 17.0f - Spot.y, 9.0f, RowLabel, "m", Anchor::End);
        Y += 32.0f;
    }

    //  .fog-derived-bounds -------------------------------------------------------------------------------
    Y += 24.0f;
    Draw->AddLine({ Spot.x, Y }, { Spot.x + Wide, Y }, BoundsRule, 1.0f);
    Y += 20.0f;
    Draw->AddText(Face, 11.0f, { Spot.x, Y }, BoundsHead, "Calculated world bounds");
    Y += 16.0f;
    const float Quarter = Wide / 4.0f;
    const char* Heads[4] = { "Metres", "X", "Y", "Z" };
    for (int Index = 0; Index < 4; ++Index)
    {
        if (Index == 0)
        {
            Draw->AddText(Face, 10.0f, { Spot.x + 3.0f, Y + 9.0f }, BoundsKey, Heads[Index]);
        }
        else
        {
            Inked(Flat, static_cast<float>(Index + 1) * Quarter - 3.0f, Y + 9.0f + 10.0f * BaselineShare - Spot.y,
                  10.0f, BoundsKey, Heads[Index], Anchor::End);
        }
    }
    Y += 30.0f;
    for (int Row = 0; Row < 3; ++Row)
    {
        const char* Names[3] = { "Min", "Max", "Size" };
        Draw->AddText(Face, 10.0f, { Spot.x + 3.0f, Y + 9.0f }, BoundsKey, Names[Row]);
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            const float Value = Row == 0   ? Mesh.Low[Axis] + Centre[Axis]
                                : Row == 1 ? Mesh.High[Axis] + Centre[Axis]
                                           : Mesh.Size[Axis];
            Grouped(Figure, sizeof(Figure), std::fabs(Value) < 0.00005f ? 0.0f : Value, 3);
            Inked(Flat, static_cast<float>(Axis + 2) * Quarter - 3.0f, Y + 9.0f + 10.0f * BaselineShare - Spot.y,
                  10.0f, BoundsValue, Figure, Anchor::End);
        }
        Y += 30.0f;
    }

    Y += 14.0f;
    Draw->AddText(Face, 11.0f, { Spot.x, Y }, NoteText, VolumeNote(), nullptr, Wide);
    Y += Lines(Face, 11.0f, Wide, VolumeNote());
    return Y - Spot.y;
}

}   // namespace Frontier::FogCards
