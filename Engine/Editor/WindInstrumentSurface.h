//==============================================================================================================================================
//                                                          WINDINSTRUMENTSURFACE.H
//==============================================================================================================================================
// 📦 Drawing surface for the shipped wind instrument cards: the InspectorDepot/styles.css palette and the anemometer trace
//    InspectorDepot/panels/wind.js paints, as rewritten at build time by InstrumentSpecification.js RecolourInstrument.

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

namespace Frontier::WindInstrument
{

constexpr float Pi = 3.14159265358979f;

//------------------------------------------------------------------------------------------------------------------------
//                                                  PALETTE AND GEOMETRY
//------------------------------------------------------------------------------------------------------------------------
// InspectorDepot/styles.css custom properties. This kit is neutral grey where the main editor's light family is
//    green-tinted, so none of these constants are shared with LightPanelSurface.h or WindPanelSurface.h.

constexpr ImU32 CardFill    = IM_COL32( 26,  26,  26, 255);   // [-] --inset #1a1a1a
constexpr ImU32 FieldFill   = IM_COL32(  0,   0,   0, 255);   // [-] --field #000000
constexpr ImU32 Stroke      = IM_COL32(255, 255, 255,  13);   // [-] --stroke rgba(255,255,255,.05)
constexpr ImU32 TextFull    = IM_COL32(240, 240, 240, 255);   // [-] --text #f0f0f0
constexpr ImU32 TextDim     = IM_COL32(136, 136, 136, 255);   // [-] --text-dim #888888
constexpr ImU32 TextFaint   = IM_COL32( 92,  92,  92, 255);   // [-] --text-faint #5c5c5c
constexpr ImU32 TraceRule   = IM_COL32(255, 255, 255,  15);   // [-] gridline rgba(255,255,255,.06)
constexpr ImU32 TraceLabel  = IM_COL32(255, 255, 255,  66);   // [-] axis label rgba(255,255,255,.26)
constexpr ImU32 TraceMean   = IM_COL32(255, 255, 255,  71);   // [-] mean rule rgba(255,255,255,.28)
constexpr ImU32 TraceLine   = IM_COL32(210, 210, 210,  92);   // [-] recoloured trace rgba(210,210,210,.36)
constexpr ImU32 TraceCrest  = IM_COL32(242, 242, 242, 242);   // [-] above-mean run rgba(242,242,242,.95)
constexpr ImU32 TracePeak   = IM_COL32(238, 238, 238, 255);   // [-] peak dot #eeeeee
constexpr ImU32 TraceTrough = IM_COL32(214, 154,  84, 255);   // [-] trough dot #d69a54
constexpr ImU32 TraceHead   = IM_COL32(255, 255, 255, 255);   // [-] live dot #fff
constexpr ImU32 WashTop     = IM_COL32(224, 224, 224,  26);   // [-] under-trace wash rgba(224,224,224,.10)
constexpr ImU32 WashFoot    = IM_COL32(224, 224, 224,   3);   // [-] under-trace wash rgba(224,224,224,.01)

constexpr float CardRound  = 18.0f;   // [px] --r-inset
constexpr float CardPadX   = 14.0f;   // [px] .pcard padding left / right
constexpr float CardPadTop = 12.0f;   // [px] .pcard padding-top
constexpr float MetricFoot =  8.0f;   // [px] .mp-metric padding-bottom
constexpr float HeadDrop   =  8.0f;   // [px] .mp-chead margin-bottom
constexpr float HeadTitle  = 15.0f;   // [px] .mp-chead .t font-size
constexpr float HeadSub    =  9.5f;   // [px] .mp-chead .s font-size
constexpr float HeadGap    =  1.0f;   // [px] .mp-chead .l gap
constexpr float NumSize    = 46.0f;   // [px] .mp-num .i and .d font-size
constexpr float NumUnit    = 12.0f;   // [px] .mp-num .u font-size
constexpr float NumLift    =  2.0f;   // [px] .mp-num margin-top
constexpr float NumDrop    =  6.0f;   // [px] .mp-num margin-bottom
constexpr float KeySize    =  9.5f;   // [px] .mp-k font-size
constexpr float KeyValue   = 11.0f;   // [px] .mp-target .v font-size
constexpr float ChartLift  =  2.0f;   // [px] .mp-chartwrap margin-top
constexpr float ChartBleed =  4.0f;   // [px] .mp-chartwrap negative side margin
constexpr float ChartBody  = 112.0f;  // [px] trace height, 170 when the card is pulled tall
constexpr float ChartTall  = 170.0f;  // [px] .tall trace height
constexpr float SpecGap    =  6.0f;   // [px] .mp-spec gap
constexpr float SpecPadX   = 10.0f;   // [px] .mp-spec > div padding left / right
constexpr float SpecPadY   =  7.0f;   // [px] .mp-spec > div padding top / bottom
constexpr float SpecRound  = 14.0f;   // [px] .mp-spec > div border-radius
constexpr float SpecKey    =  8.5f;   // [px] .mp-spec .k font-size
constexpr float SpecValue  = 13.0f;   // [px] .mp-spec b font-size

// ImGui bakes a face so that ascent - descent equals the requested size; CSS sizes the em box instead. For
//    DM Sans (hhea ascent 992, descent -310, gap 0 per 1000 em) the two differ by exactly 1.302, so a CSS
//    pixel size is ground up by that factor before it is handed to ImGui. The same number is the font's
//    `normal` line-height, which is why a baked line is also exactly one CSS line box tall.
constexpr float EmScale       = 1.302f;   // [-] (992 + 310) / 1000, DM Sans hhea
constexpr float AscentShare   = 0.992f;   // [-] 992 / 1000, the baseline drop inside a line box
// Canvas text is baseline-anchored; ImGui places the line box top.
constexpr float BaselineShare = AscentShare;

inline float Grind(float Size) { return Size * EmScale; }

//------------------------------------------------------------------------------------------------------------------------
//                                                       PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

enum class Anchor { Start, Middle, End };

inline float Measured(ImFont* Face, float Size, const char* Body)
{
    return Face->CalcTextSizeA(Grind(Size), FLT_MAX, 0.0f, Body).x;
}

inline void Boxed(ImDrawList* Draw, ImFont* Face, float X, float Y, float Size, ImU32 Colour, const char* Body)
{
    Draw->AddText(Face, Grind(Size), { X, Y }, Colour, Body);
}

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

// CSS adds letter-spacing after every glyph, the last one included, so the run is that much wider than the
//    sum of its advances.
// .mp-chead .l is a flex column, so the title and the subtitle each occupy a full line box.
inline float HeadHeight() { return Grind(HeadTitle) + HeadGap + Grind(HeadSub) + HeadDrop; }

inline float TrackedWidth(ImFont* Face, float Size, const char* Body, float Extra)
{
    ImFontBaked* Baked = Face->GetFontBaked(Grind(Size));
    float Run = 0.0f;
    for (const char* Scan = Body; *Scan;)
    {
        unsigned Point = 0;
        const int Used = ImTextCharFromUtf8(&Point, Scan, nullptr);
        if (Used <= 0) break;
        Run += Baked->GetCharAdvance(static_cast<ImWchar>(Point)) + Extra;
        Scan += Used;
    }
    return Run;
}

// CSS letter-spacing: the browser adds the gap after every glyph, so the run is drawn a codepoint at a time.
inline float Tracked(ImDrawList* Draw, ImFont* Face, float X, float Y, float Size, ImU32 Colour,
                     const char* Body, float Extra, bool Paint = true)
{
    float Pen = X;
    for (const char* Scan = Body; *Scan;)
    {
        unsigned Point = 0;
        const int Used = ImTextCharFromUtf8(&Point, Scan, nullptr);
        if (Used <= 0) break;
        if (Paint) Draw->AddText(Face, Grind(Size), { Pen, Y }, Colour, Scan, Scan + Used);
        ImFontBaked* Baked = Face->GetFontBaked(Grind(Size));
        Pen += Baked->GetCharAdvance(static_cast<ImWchar>(Point)) + Extra;
        Scan += Used;
    }
    return Pen - X;
}

// ImGui's own wrapping does not know about letter-spacing, so the break points are found with the tracked
//    advance instead. Greedy, on spaces, exactly as a browser breaks a run of plain words.
struct TrackedLine { const char* From; const char* To; };

inline int TrackedWrap(ImFont* Face, float Size, const char* Body, float Extra, float Wide,
                       TrackedLine* Lines, int Limit)
{
    int Count = 0;
    const char* Start = Body;
    while (*Start && Count < Limit)
    {
        const char* Best = nullptr;
        for (const char* Scan = Start; ; ++Scan)
        {
            const bool Stop = *Scan == 0;
            if (*Scan == ' ' || Stop)
            {
                char Piece[256];
                const size_t Taken = std::min(sizeof(Piece) - 1, size_t(Scan - Start));
                std::memcpy(Piece, Start, Taken);
                Piece[Taken] = 0;
                if (TrackedWidth(Face, Size, Piece, Extra) <= Wide || !Best) Best = Scan;
                else break;
                if (Stop) break;
            }
        }
        Lines[Count++] = { Start, Best };
        Start = Best;
        while (*Start == ' ') ++Start;
    }
    return Count;
}

inline float TrackedFlowHeight(ImFont* Face, float Size, const char* Body, float Extra, float Wide)
{
    TrackedLine Lines[12];
    const int Count = TrackedWrap(Face, Size, Body, Extra, Wide, Lines, 12);
    return std::max(1, Count) * Grind(Size);
}

// Y is the top of the first line box.
inline float PaintTrackedFlow(ImDrawList* Draw, ImFont* Face, float X, float Y, float Size, ImU32 Colour,
                              const char* Body, float Extra, float Wide)
{
    TrackedLine Lines[12];
    const int Count = TrackedWrap(Face, Size, Body, Extra, Wide, Lines, 12);
    for (int I = 0; I < Count; ++I)
    {
        char Piece[256];
        const size_t Taken = std::min(sizeof(Piece) - 1, size_t(Lines[I].To - Lines[I].From));
        std::memcpy(Piece, Lines[I].From, Taken);
        Piece[Taken] = 0;
        Tracked(Draw, Face, X, Y + I * Grind(Size), Size, Colour, Piece, Extra);
    }
    return std::max(1, Count) * Grind(Size);
}

inline void Dashed(ImDrawList* Draw, ImVec2 From, ImVec2 To, ImU32 Colour, float On, float Off, float Thick)
{
    const float Run = std::sqrt((To.x - From.x) * (To.x - From.x) + (To.y - From.y) * (To.y - From.y));
    if (Run <= 0.0f) return;
    const float StepX = (To.x - From.x) / Run, StepY = (To.y - From.y) / Run;
    for (float Walk = 0.0f; Walk < Run; Walk += On + Off)
    {
        const float End = std::min(Walk + On, Run);
        Draw->AddLine({ From.x + StepX * Walk, From.y + StepY * Walk },
                      { From.x + StepX * End,  From.y + StepY * End }, Colour, Thick);
    }
}

// icons.js P.arrowout on its 24 unit grid: the diagonal, then the corner elbow it points into.
inline void PaintArrowOut(ImDrawList* Draw, ImVec2 Spot, float Size, ImU32 Colour)
{
    const float Unit = Size / 24.0f, Thick = 1.75f * Unit;
    auto At = [&](float X, float Y) { return ImVec2{ Spot.x + X * Unit, Spot.y + Y * Unit }; };
    Draw->AddLine(At(8.5f, 15.5f), At(15.5f, 8.5f), Colour, Thick);
    Draw->PathLineTo(At(9.5f, 8.5f));
    Draw->PathLineTo(At(15.5f, 8.5f));
    Draw->PathLineTo(At(15.5f, 14.5f));
    Draw->PathStroke(Colour, 0, Thick);
}

inline void Upper(char* Out, size_t Size, const char* Body)
{
    size_t I = 0;
    for (; Body[I] && I + 1 < Size; ++I) Out[I] = static_cast<char>(std::toupper(static_cast<unsigned char>(Body[I])));
    Out[I] = '\0';
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    WIND ARITHMETIC
//------------------------------------------------------------------------------------------------------------------------
// The Beaufort bands, the land descriptions and the sixteen-point compass, read straight off wind.js.

struct BeaufortBand { float Limit; int Force; const char* Name; };

constexpr BeaufortBand Beaufort[11] = {
    {  0.5f,  0, "Calm"            }, {  1.5f,  1, "Light air"      }, {  3.3f,  2, "Light breeze" },
    {  5.5f,  3, "Gentle breeze"   }, {  7.9f,  4, "Moderate breeze"}, { 10.7f,  5, "Fresh breeze" },
    { 13.8f,  6, "Strong breeze"   }, { 17.1f,  7, "Near gale"      }, { 20.7f,  8, "Gale"         },
    { 24.4f,  9, "Strong gale"     }, { 28.4f, 10, "Storm"          },
};

inline BeaufortBand Force(float Speed)
{
    for (const BeaufortBand& Band : Beaufort) if (Speed < Band.Limit) return Band;
    return { FLT_MAX, 11, "Violent storm" };
}

inline const char* LandSign(float Speed)
{
    static const struct { float Limit; const char* Body; } Land[11] = {
        {  0.5f, "smoke rises straight up" }, {  1.5f, "smoke drifts" }, {  3.3f, "leaves rustle" },
        {  5.5f, "flags stir, leaves move" }, {  7.9f, "dust lifts, small branches move" },
        { 10.7f, "small trees sway" }, { 13.8f, "large branches move" }, { 17.1f, "whole trees in motion" },
        { 20.7f, "twigs break off" }, { 24.4f, "slates lift" }, { 28.4f, "trees uprooted" },
    };
    for (const auto& Entry : Land) if (Speed < Entry.Limit) return Entry.Body;
    return "structural damage";
}

inline const char* Compass(float Degrees)
{
    static const char* Points[16] = { "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
                                      "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW" };
    const float Wrapped = std::fmod(std::fmod(Degrees, 360.0f) + 360.0f, 360.0f);
    return Points[static_cast<int>(std::lround(Wrapped / 22.5f)) % 16];
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE ANEMOMETER
//------------------------------------------------------------------------------------------------------------------------
// Sixty seconds at four samples a second. The trace is prefilled so it never looks switched off.

constexpr int TraceLength = 240;

struct WindDraft
{
    float Speed      = 4.2f;    // [m/s] P.speed
    float Direction  = 214.0f;  // [deg] P.direction
    float Gust       = 0.3f;    // [0..1] P.gust
    float Turbulence = 0.24f;   // [0..1] P.turbulence
};

struct TraceLog
{
    float Sample[TraceLength] {};
    float Phase   = 0.0f;
    float Spare   = 0.0f;   // the sub-quarter-second remainder
    float Instant = 0.0f;
    bool  Ready   = false;
};

inline float GustAt(const WindDraft& Draft, float Phase)
{
    const float Swell = std::sin(Phase * 0.9f) * 0.6f
                      + std::sin(Phase * 2.3f + 1.7f) * 0.3f
                      + std::sin(Phase * 5.1f) * 0.1f;
    const float Grain = (std::sin(Phase * 17.3f) + std::sin(Phase * 29.7f + 2.1f)) * 0.5f;
    return 1.0f + Draft.Gust * (Swell * 0.55f) + Draft.Turbulence * Grain * 0.18f;
}

inline float Rate(const WindDraft& Draft) { return 0.4f + Draft.Turbulence * 2.2f; }

inline void Prefill(TraceLog& Log, const WindDraft& Draft)
{
    const float Pace = Rate(Draft);
    for (int I = 0; I < TraceLength; ++I)
        Log.Sample[I] = std::max(0.0f, Draft.Speed * GustAt(Draft, Log.Phase - (TraceLength - I) * 0.25f * Pace));
    Log.Instant = std::max(0.0f, Draft.Speed * GustAt(Draft, Log.Phase));
    Log.Ready = true;
}

inline void Advance(TraceLog& Log, const WindDraft& Draft, float Delta)
{
    if (!Log.Ready) Prefill(Log, Draft);
    Log.Phase += Delta * Rate(Draft);
    Log.Instant = std::max(0.0f, Draft.Speed * GustAt(Draft, Log.Phase));
    Log.Spare += Delta;
    while (Log.Spare >= 0.25f)
    {
        Log.Spare -= 0.25f;
        for (int I = 0; I + 1 < TraceLength; ++I) Log.Sample[I] = Log.Sample[I + 1];
        Log.Sample[TraceLength - 1] = Log.Instant;
    }
}

inline float Crest(const TraceLog& Log)
{
    float High = Log.Sample[0];
    for (float Value : Log.Sample) High = std::max(High, Value);
    return High;
}

inline float Lull(const TraceLog& Log)
{
    float Low = Log.Sample[0];
    for (float Value : Log.Sample) Low = std::min(Low, Value);
    return Low;
}

// The trace panel only, drawn into the .mp-chartwrap box. R/L/T/B are wind.js's own inset names.
inline void PaintTrace(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide, float Tall,
                       const TraceLog& Log, const WindDraft& Draft)
{
    constexpr float InsetR = 30.0f, InsetL = 2.0f, InsetT = 8.0f, InsetB = 15.0f;
    const float High = Crest(Log);
    const float Top  = std::max(2.0f, std::max(High, Draft.Speed) * 1.18f);
    auto PX = [&](float Share) { return Spot.x + InsetL + Share * (Wide - InsetL - InsetR); };
    auto PY = [&](float Value) { return Spot.y + InsetT + (1.0f - Value / Top) * (Tall - InsetT - InsetB); };

    char Label[32];
    for (float Share : { 0.25f, 0.5f, 0.75f, 1.0f })
    {
        const float Line = PY(Top * Share);
        Dashed(Draw, { PX(0.0f), Line }, { PX(1.0f), Line }, TraceRule, 2.0f, 5.0f, 1.0f);
        std::snprintf(Label, sizeof(Label), Top > 12.0f ? "%.0f" : "%.1f", double(Top * Share));
        Inked(Draw, Face, Spot.x + Wide - InsetR + 6.0f, Line + 3.0f, 9.0f, TraceLabel, Label);
    }

    // RecolourInstrument drops the green gust band for a neutral wash under the trace itself.
    const float WashTopY = Spot.y + InsetT, WashFootY = Spot.y + Tall - InsetB;
    const float Base = PY(0.0f);
    auto Wash = [&](float AtY)
    {
        const float Share = std::clamp((AtY - WashTopY) / std::max(1.0f, WashFootY - WashTopY), 0.0f, 1.0f);
        return IM_COL32(224, 224, 224, int(26.0f + (3.0f - 26.0f) * Share + 0.5f));
    };
    constexpr int WashBands = 10;   // a flat quad cannot carry a gradient, so each column is banded down its height
    for (int I = 0; I + 1 < TraceLength; ++I)
    {
        const float LeftX  = PX(float(I)     / (TraceLength - 1));
        const float RightX = PX(float(I + 1) / (TraceLength - 1));
        const float LeftY  = PY(Log.Sample[I]), RightY = PY(Log.Sample[I + 1]);
        for (int Band = 0; Band < WashBands; ++Band)
        {
            const float From = float(Band) / WashBands, To = float(Band + 1) / WashBands;
            const float LeftFrom  = LeftY  + (Base - LeftY)  * From, LeftTo  = LeftY  + (Base - LeftY)  * To;
            const float RightFrom = RightY + (Base - RightY) * From, RightTo = RightY + (Base - RightY) * To;
            Draw->AddQuadFilled({ LeftX, LeftFrom }, { RightX, RightFrom }, { RightX, RightTo }, { LeftX, LeftTo },
                                Wash((LeftFrom + LeftTo + RightFrom + RightTo) * 0.25f));
        }
    }

    const float MeanY = PY(Draft.Speed);
    Dashed(Draw, { PX(0.0f), MeanY }, { PX(1.0f), MeanY }, TraceMean, 4.0f, 4.0f, 1.0f);

    for (int I = 0; I + 1 < TraceLength; ++I)
        Draw->AddLine({ PX(float(I) / (TraceLength - 1)), PY(Log.Sample[I]) },
                      { PX(float(I + 1) / (TraceLength - 1)), PY(Log.Sample[I + 1]) }, TraceLine, 1.2f);

    // Only the runs that actually sit above the mean are brightened; no second invented trace.
    for (int I = 1; I < TraceLength; ++I)
    {
        if (Log.Sample[I] <= Draft.Speed || Log.Sample[I - 1] <= Draft.Speed) continue;
        Draw->AddLine({ PX(float(I - 1) / (TraceLength - 1)), PY(Log.Sample[I - 1]) },
                      { PX(float(I) / (TraceLength - 1)), PY(Log.Sample[I]) }, TraceCrest, 1.2f);
    }

    int PeakAt = 0, TroughAt = 0;
    for (int I = 0; I < TraceLength; ++I)
    {
        if (Log.Sample[I] > Log.Sample[PeakAt])   PeakAt = I;
        if (Log.Sample[I] < Log.Sample[TroughAt]) TroughAt = I;
    }
    Draw->AddCircleFilled({ PX(float(PeakAt) / (TraceLength - 1)), PY(Log.Sample[PeakAt]) }, 2.3f, TracePeak, 20);
    Draw->AddCircleFilled({ PX(float(TroughAt) / (TraceLength - 1)), PY(Log.Sample[TroughAt]) }, 2.3f, TraceTrough, 20);
    Draw->AddCircleFilled({ PX(1.0f), PY(Log.Sample[TraceLength - 1]) }, 2.6f, TraceHead, 20);

    const float Foot = Spot.y + Tall - 3.0f;
    Inked(Draw, Face, PX(0.0f), Foot, 9.0f, TraceLabel, "\xe2\x88\x92""60 s", Anchor::Start);
    Inked(Draw, Face, PX(0.5f), Foot, 9.0f, TraceLabel, "\xe2\x88\x92""30 s", Anchor::Middle);
    Inked(Draw, Face, PX(1.0f), Foot, 9.0f, TraceLabel, "now", Anchor::End);
}

inline float AnemometerHeight(bool Tall)
{
    return CardPadTop
         + HeadHeight()
         + NumLift + NumSize * 0.94f + NumDrop
         + KeySize
         + ChartLift + (Tall ? ChartTall : ChartBody)
         + SpecGap + (SpecPadY * 2 + SpecKey + 1 + SpecValue) * 2 + SpecGap
         + MetricFoot;
}

// Spot is the card's top-left. Wide is the full card width.
inline void PaintAnemometer(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide,
                            const TraceLog& Log, const WindDraft& Draft, bool Tall = false)
{
    const float Inner = Wide - CardPadX * 2.0f;
    const float Left  = Spot.x + CardPadX;
    const float Foot  = Spot.y + AnemometerHeight(Tall);
    Draw->AddRectFilled(Spot, { Spot.x + Wide, Foot }, CardFill, CardRound);

    float Y = Spot.y + CardPadTop;
    Boxed(Draw, Face, Left, Y, HeadTitle, TextFull, "Anemometer");
    char Sub[32];
    Upper(Sub, sizeof(Sub), "Last 60 seconds");
    Tracked(Draw, Face, Left, Y + Grind(HeadTitle) + HeadGap, HeadSub, TextFaint, Sub, 0.9f);
    // .mp-x, the taller-trace button: a 24 px square with a transparent ground until hover, so only
    //    the 12 px arrowout icon shows. icons.js draws it on a 24 unit grid at stroke-width 1.75.
    PaintArrowOut(Draw, { Spot.x + Wide - CardPadX - 24.0f + 6.0f, Y + 6.0f }, 12.0f, TextDim);
    Y += HeadHeight() + NumLift;

    char Whole[16], Fraction[8];
    std::snprintf(Whole, sizeof(Whole), "%d", int(std::floor(Log.Instant)));
    std::snprintf(Fraction, sizeof(Fraction), ".%d", int(std::lround(Log.Instant * 10.0f)) % 10);
    const float WholeRun    = Tracked(Draw, Face, Left, Y, NumSize, TextFull, Whole, -2.4f);
    const float FractionRun = Tracked(Draw, Face, Left + WholeRun, Y, NumSize, TextFaint, Fraction, -2.4f);
    Boxed(Draw, Face, Left + WholeRun + FractionRun + 7.0f, Y + NumSize * 0.94f - NumUnit - 2.0f,
          NumUnit, TextDim, "m/s");
    Y += NumSize * 0.94f + NumDrop;

    char Key[8];
    Upper(Key, sizeof(Key), "Mean");
    const float KeyRun = Tracked(Draw, Face, Left, Y, KeySize, TextFaint, Key, 1.3f);
    const BeaufortBand Band = Force(Draft.Speed);
    char Named[40];
    std::snprintf(Named, sizeof(Named), "%s", Band.Name);
    for (char* Scan = Named; *Scan; ++Scan) *Scan = static_cast<char>(std::tolower(static_cast<unsigned char>(*Scan)));
    char Mean[96];
    std::snprintf(Mean, sizeof(Mean), "%.1f m/s \xc2\xb7 %s, force %d", double(Draft.Speed), Named, Band.Force);
    Boxed(Draw, Face, Left + KeyRun + 6.0f, Y + KeySize - KeyValue, KeyValue, TextDim, Mean);
    Y += KeySize + ChartLift;

    PaintTrace(Draw, Face, { Left - ChartBleed, Y }, Inner + ChartBleed * 2.0f,
               Tall ? ChartTall : ChartBody, Log, Draft);
    Y += (Tall ? ChartTall : ChartBody) + SpecGap;

    const float High = Crest(Log), Low = Lull(Log);
    const float Pressure = 0.5f * 1.225f * Draft.Speed * Draft.Speed;
    char Values[4][48];
    std::snprintf(Values[0], sizeof(Values[0]), "%.2f\xc3\x97", double(High / std::max(Draft.Speed, 0.1f)));
    std::snprintf(Values[1], sizeof(Values[1]), "%.1f m/s", double(High - Low));
    std::snprintf(Values[2], sizeof(Values[2]), Pressure < 10.0f ? "%.1f Pa" : "%.0f Pa", double(Pressure));
    std::snprintf(Values[3], sizeof(Values[3]), "%.0f km/h \xc2\xb7 %.1f kn",
                  double(Draft.Speed * 3.6f), double(Draft.Speed * 1.944f));
    const char* Keys[4] = { "gust factor", "spread", "pressure", "also" };

    const float TileW = (Inner - SpecGap) * 0.5f;
    const float TileH = SpecPadY * 2 + SpecKey + 1 + SpecValue;
    for (int I = 0; I < 4; ++I)
    {
        const float TileX = Left + (I % 2) * (TileW + SpecGap);
        const float TileY = Y + (I / 2) * (TileH + SpecGap);
        Draw->AddRectFilled({ TileX, TileY }, { TileX + TileW, TileY + TileH }, FieldFill, SpecRound);
        Draw->AddRect({ TileX, TileY }, { TileX + TileW, TileY + TileH }, Stroke, SpecRound, 0, 1.0f);
        char Caption[32];
        Upper(Caption, sizeof(Caption), Keys[I]);
        Tracked(Draw, Face, TileX + SpecPadX, TileY + SpecPadY, SpecKey, TextFaint, Caption, 1.1f);
        Tracked(Draw, Face, TileX + SpecPadX, TileY + SpecPadY + SpecKey + 1.0f, SpecValue, TextFull,
                Values[I], -0.2f);
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                  THE SHARED CONTROLS
//------------------------------------------------------------------------------------------------------------------------
// controls.js stepper and tape. The tape is the panel's workhorse: a forty-division ruler lit up to the
//    current value, named marks along the foot, and a pennant marker.

struct Mark { float At; const char* Label; };

constexpr float StepBox   = 20.0f;   // [px] .step button
constexpr float StepRound =  7.0f;   // [px] .step button border-radius
constexpr float StepField = 42.0f;   // [px] .step input width
constexpr ImU32 Raised    = IM_COL32(34, 34, 34, 255);   // [-] --raised #222222

inline float StepperWidth(ImFont* Face, const char* Unit)
{
    const float UnitRun = Unit && *Unit ? Measured(Face, 9.0f, Unit) + 3.0f : 0.0f;
    return StepBox + 4.0f + (StepField + UnitRun + 16.0f + 2.0f) + 4.0f + StepBox;
}

inline void PaintStepper(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Value, int Decimals, const char* Unit)
{
    const float UnitRun = Unit && *Unit ? Measured(Face, 9.0f, Unit) + 3.0f : 0.0f;
    const float FieldW  = StepField + UnitRun + 16.0f + 2.0f;
    auto Button = [&](float X, const char* Sign)
    {
        Draw->AddRectFilled({ X, Spot.y }, { X + StepBox, Spot.y + StepBox }, Raised, StepRound);
        Draw->AddRect({ X, Spot.y }, { X + StepBox, Spot.y + StepBox }, Stroke, StepRound, 0, 1.0f);
        Inked(Draw, Face, X + StepBox * 0.5f, Spot.y + StepBox * 0.5f + 4.0f, 12.0f, TextDim, Sign, Anchor::Middle);
    };
    Button(Spot.x, "\xe2\x88\x92");
    const float FieldX = Spot.x + StepBox + 4.0f;
    const float FieldY = Spot.y + (StepBox - 21.0f) * 0.5f;
    Draw->AddRectFilled({ FieldX, FieldY }, { FieldX + FieldW, FieldY + 21.0f }, FieldFill, 9.0f);
    Draw->AddRect({ FieldX, FieldY }, { FieldX + FieldW, FieldY + 21.0f }, Stroke, 9.0f, 0, 1.0f);
    char Body[32];
    std::snprintf(Body, sizeof(Body), "%.*f", Decimals, double(Value));
    const float Baseline = FieldY + 3.0f + 12.5f;
    Inked(Draw, Face, FieldX + 8.0f + StepField, Baseline, 12.5f, TextFull, Body, Anchor::End);
    if (UnitRun > 0.0f) Inked(Draw, Face, FieldX + 8.0f + StepField + 3.0f, Baseline, 9.0f, TextDim, Unit);
    Button(FieldX + FieldW + 4.0f, "+");
}

constexpr float TapeBody = 30.0f;   // [px] tape({ height })
constexpr float TapeLead = 10.0f;   // [px] .tape margin-top
constexpr float TapeFoot = 12.0f;   // [px] .tape margin-bottom

inline float TapeHeight() { return TapeLead + StepBox + 1.0f + TapeBody + TapeFoot; }

inline void PaintTape(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide, const char* Label,
                      float Value, float Low, float High, int Decimals, const char* Unit,
                      const Mark* Marks, int MarkCount)
{
    const float HeadY = Spot.y + TapeLead;
    char Key[40];
    Upper(Key, sizeof(Key), Label);
    Tracked(Draw, Face, Spot.x, HeadY + (StepBox - 9.5f) * 0.5f, 9.5f, TextFaint, Key, 1.3f);
    PaintStepper(Draw, Face, { Spot.x + Wide - StepperWidth(Face, Unit), HeadY }, Value, Decimals, Unit);

    const float Top  = HeadY + StepBox + 1.0f;
    const float Pad  = 8.0f, Span = Wide - Pad * 2.0f, Base = Top + TapeBody - 11.0f;
    const float Share = std::clamp((Value - Low) / std::max(1e-6f, High - Low), 0.0f, 1.0f);
    for (int I = 0; I <= 40; ++I)
    {
        const float At = float(I) / 40.0f;
        const bool  Major = I % 10 == 0;
        const bool  Lit = At <= Share;
        const ImU32 Ink = Major ? IM_COL32(255, 255, 255, 77)
                                : IM_COL32(255, 255, 255, Lit ? 56 : 23);
        const float Long = Major ? 9.0f : (I % 5 == 0 ? 6.0f : 4.0f);
        const float X = Spot.x + Pad + At * Span;
        Draw->AddLine({ X, Base - Long }, { X, Base }, Ink, 1.0f);
    }
    Draw->AddLine({ Spot.x + Pad, Base + 0.5f }, { Spot.x + Pad + Span, Base + 0.5f },
                  IM_COL32(255, 255, 255, 26), 1.0f);

    // A label that would collide with one already placed is dropped, exactly as the canvas does.
    float Taken[8][2];
    int   TakenCount = 0;
    for (int I = 0; I < MarkCount && TakenCount < 8; ++I)
    {
        const float X = Spot.x + Pad + Marks[I].At * Span;
        const float Run = Measured(Face, 8.0f, Marks[I].Label);
        const float X0 = Marks[I].At <= 0.0f ? X : Marks[I].At >= 1.0f ? X - Run : X - Run * 0.5f;
        const float X1 = X0 + Run;
        bool Clash = false;
        for (int J = 0; J < TakenCount; ++J)
            if (X0 < Taken[J][1] + 5.0f && X1 > Taken[J][0] - 5.0f) Clash = true;
        if (Clash) continue;
        Taken[TakenCount][0] = X0;
        Taken[TakenCount][1] = X1;
        ++TakenCount;
        Boxed(Draw, Face, X0, Top + TapeBody - 1.0f - 8.0f, 8.0f, IM_COL32(255, 255, 255, 71), Marks[I].Label);
    }

    const float X = Spot.x + Pad + Share * Span;
    Draw->AddTriangleFilled({ X, Base - 13.0f }, { X + 4.0f, Base - 19.0f }, { X - 4.0f, Base - 19.0f }, TraceHead);
    Draw->AddLine({ X, Base - 12.0f }, { X, Base }, IM_COL32(255, 255, 255, 217), 1.4f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE HERO
//------------------------------------------------------------------------------------------------------------------------
// A hundred and ninety motes streaming on a curl field. Drag out from the middle and the angle is the
//    bearing, the distance the speed.

constexpr int   MoteCount = 190;
constexpr float HeroTall  = 168.0f;   // [px] the hero canvas, 138 when the panel is compact
constexpr float HeroShort = 138.0f;

struct Mote { float X, Y, Life, Seed, Dx, Dy; };
struct Sky  { Mote Motes[MoteCount] {}; bool Ready = false; };

inline float Curl(float X, float Y, float Phase)
{
    return std::sin(X * 7.1f + Phase * 0.7f) * std::cos(Y * 6.3f - Phase * 0.5f)
         + 0.5f * std::sin(X * 13.7f - Phase * 1.1f) * std::cos(Y * 11.3f + Phase * 0.9f);
}

inline void SkyPrime(Sky& Air)
{
    for (int I = 0; I < MoteCount; ++I)
        Air.Motes[I] = { float((I * 977) % 1000) / 1000.0f, float((I * 613) % 1000) / 1000.0f,
                         float((I * 37) % 100) / 100.0f, float((I * 131) % 1000) / 1000.0f, 1.0f, 0.0f };
    Air.Ready = true;
}

// Meteorological convention: the bearing is where the wind comes FROM, so it blows the other way.
inline void Heading(const WindDraft& Draft, float& UX, float& UY)
{
    const float Angle = (Draft.Direction + 180.0f - 90.0f) * Pi / 180.0f;
    UX = std::cos(Angle);
    UY = std::sin(Angle);
}

inline void SkyAdvance(Sky& Air, const WindDraft& Draft, const TraceLog& Log,
                       float Wide, float Tall, float Delta)
{
    if (!Air.Ready) SkyPrime(Air);
    float UX, UY;
    Heading(Draft, UX, UY);
    const float Turb = Draft.Turbulence;
    const float Norm = std::min(1.0f, Draft.Speed / 30.0f);
    const float Gust = GustAt(Draft, Log.Phase);
    const float Step = (0.03f + Norm * 0.55f) * Gust * Delta;
    for (Mote& Speck : Air.Motes)
    {
        const float Twist = Curl(Speck.X, Speck.Y, Log.Phase) * Turb * 0.9f;
        const float Cos = std::cos(Twist * 0.9f), Sin = std::sin(Twist * 0.9f);
        Speck.Dx = UX * Cos - UY * Sin;
        Speck.Dy = UX * Sin + UY * Cos;
        const float Pace = Step * (0.65f + Speck.Seed * 0.7f);
        Speck.X += Speck.Dx * Pace;
        Speck.Y += Speck.Dy * Pace * (Wide / Tall);
        Speck.Life -= Delta * (0.25f + Norm * 0.5f);
        if (Speck.Life <= 0.0f || Speck.X < -0.05f || Speck.X > 1.05f || Speck.Y < -0.05f || Speck.Y > 1.05f)
        {
            Speck.X = UX > 0.0f ? -0.02f - Speck.Seed * 0.1f
                    : UX < 0.0f ?  1.02f + Speck.Seed * 0.1f : Speck.Seed;
            if (std::fabs(UX) < 0.35f) { Speck.X = Speck.Seed; Speck.Y = UY > 0.0f ? -0.02f : 1.02f; }
            else                        Speck.Y = std::fmod(Speck.Seed * 7919.0f, 1000.0f) / 1000.0f;
            Speck.Life = 0.6f + Speck.Seed * 0.9f;
        }
    }
}

// An ImGui clip rectangle cannot be rounded, so a canvas with a border-radius paints over its own
//    corners. Repaint the four notches in whatever sits behind the card.
inline void CornerNotch(ImDrawList* Draw, ImVec2 Square, ImVec2 Heart, float Radius, float From, ImU32 Backdrop)
{
    Draw->PathLineTo(Square);
    for (int I = 0; I <= 12; ++I)
        Draw->PathLineTo({ Heart.x + std::cos(From + Pi * 0.5f * I / 12.0f) * Radius,
                           Heart.y + std::sin(From + Pi * 0.5f * I / 12.0f) * Radius });
    Draw->PathFillConvex(Backdrop);
}

inline void PaintHero(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide, float Tall,
                      const Sky& Air, const TraceLog& Log, const WindDraft& Draft,
                      ImU32 Backdrop = IM_COL32(5, 5, 5, 255))
{
    Draw->PushClipRect(Spot, { Spot.x + Wide, Spot.y + Tall }, true);
    // The sky itself, a short vertical ramp.
    constexpr int Ramp = 24;
    for (int I = 0; I < Ramp; ++I)
    {
        const float A = float(I) / Ramp, B = float(I + 1) / Ramp;
        const float Mid = (A + B) * 0.5f;
        const ImU32 Ink = IM_COL32(int(10 + (13 - 10) * Mid + 0.5f), int(16 + (26 - 16) * Mid + 0.5f),
                                   int(20 + (27 - 20) * Mid + 0.5f), 255);
        Draw->AddRectFilled({ Spot.x, Spot.y + Tall * A }, { Spot.x + Wide, Spot.y + Tall * B + 1.0f }, Ink);
    }
    for (float X = 0.0f; X < Wide; X += 26.0f)
        Draw->AddLine({ Spot.x + X, Spot.y }, { Spot.x + X, Spot.y + Tall }, IM_COL32(255, 255, 255, 9), 1.0f);
    for (float Y = 0.0f; Y < Tall; Y += 26.0f)
        Draw->AddLine({ Spot.x, Spot.y + Y }, { Spot.x + Wide, Spot.y + Y }, IM_COL32(255, 255, 255, 9), 1.0f);

    const float Norm = std::min(1.0f, Draft.Speed / 30.0f);
    const float Gust = GustAt(Draft, Log.Phase);
    const float Thick = 0.9f + Norm * 1.3f;
    for (const Mote& Speck : Air.Motes)
    {
        const float Long = (7.0f + Norm * 34.0f) * (0.5f + Speck.Seed);
        const float X = Spot.x + Speck.X * Wide, Y = Spot.y + Speck.Y * Tall;
        const float Alpha = std::min(0.8f, (0.3f + Norm * 0.45f)
                                           * std::min(1.0f, Speck.Life * 2.2f) * Gust);
        // ImGui cannot fade a stroke along its length, so the streak is laid down in graded pieces.
        constexpr int Pieces = 8;
        for (int I = 0; I < Pieces; ++I)
        {
            const float A = float(I) / Pieces, B = float(I + 1) / Pieces;
            const int Shade = int(Alpha * ((A + B) * 0.5f) * 255.0f + 0.5f);
            if (Shade <= 0) continue;
            Draw->AddLine({ X - Speck.Dx * Long * (1.0f - A), Y - Speck.Dy * Long * (1.0f - A) },
                          { X - Speck.Dx * Long * (1.0f - B), Y - Speck.Dy * Long * (1.0f - B) },
                          IM_COL32(137, 224, 196, Shade), Thick);
        }
    }

    float UX, UY;
    Heading(Draft, UX, UY);
    const float CX = Spot.x + Wide * 0.5f, CY = Spot.y + Tall * 0.5f;
    const float Reach = std::min(Wide, Tall) * 0.34f;
    const float Long  = Reach * (0.25f + Norm * 0.75f);
    for (int I = 0; I < 72; ++I)   // a 2/4 dashed rim
    {
        if (I % 2) continue;
        const float A = float(I) / 72.0f * 2.0f * Pi, B = float(I + 1) / 72.0f * 2.0f * Pi;
        Draw->AddLine({ CX + std::cos(A) * Reach, CY + std::sin(A) * Reach },
                      { CX + std::cos(B) * Reach, CY + std::sin(B) * Reach }, IM_COL32(255, 255, 255, 26), 1.0f);
    }
    Draw->AddLine({ CX, CY }, { CX + UX * Long, CY + UY * Long }, IM_COL32(255, 255, 255, 217), 2.0f);
    const float HX = CX + UX * Long, HY = CY + UY * Long;
    Draw->AddTriangleFilled({ HX + UX * 7.0f, HY + UY * 7.0f },
                            { HX - UY * 4.5f - UX * 2.0f, HY + UX * 4.5f - UY * 2.0f },
                            { HX + UY * 4.5f - UX * 2.0f, HY - UX * 4.5f - UY * 2.0f }, TraceHead);
    Draw->AddCircleFilled({ CX, CY }, 2.5f, IM_COL32(255, 255, 255, 140), 18);

    const float Face8 = (Draft.Direction - 90.0f) * Pi / 180.0f;
    Inked(Draw, Face, CX + std::cos(Face8) * (Reach + 11.0f), CY + std::sin(Face8) * (Reach + 11.0f) + 3.0f,
          8.0f, IM_COL32(255, 255, 255, 107), Compass(Draft.Direction), Anchor::Middle);
    const char* Cardinals[4] = { "N", "E", "S", "W" };
    for (int I = 0; I < 4; ++I)
    {
        const float Angle = (I * 90.0f - 90.0f) * Pi / 180.0f;
        Inked(Draw, Face, CX + std::cos(Angle) * (Reach + 11.0f), CY + std::sin(Angle) * (Reach + 11.0f) + 3.0f,
              8.0f, IM_COL32(255, 255, 255, 46), Cardinals[I], Anchor::Middle);
    }

    // The caption, over a ramp into near-black so the words stay legible whatever streams beneath.
    const BeaufortBand Sign = Force(Draft.Speed);
    char Flank[24];
    std::snprintf(Flank, sizeof(Flank), "force %d", Sign.Force);
    char Line[128], Spoken[128];
    std::snprintf(Line, sizeof(Line), "%.1f m/s from %s %d\xc2\xb0 \xc2\xb7 %s", double(Draft.Speed),
                  Compass(Draft.Direction), int(std::lround(Draft.Direction)), LandSign(Draft.Speed));
    Upper(Spoken, sizeof(Spoken), Line);
    // .mp-cap: a flex row with a 10 px gap. .r takes its own width, .l has min-width 0 and takes the rest,
    //    so a long sub wraps inside the left column and the whole band grows upward from the card's foot.
    const float CapFlank = Measured(Face, 10.5f, Flank);
    const float CapLeft  = std::max(1.0f, Wide - 12.0f * 2.0f - CapFlank - 10.0f);
    const float CapSub   = TrackedFlowHeight(Face, 10.0f, Spoken, 0.6f, CapLeft);
    const float CapTall  = 8.0f + Grind(12.5f) + 2.0f + CapSub + 9.0f;
    const float CapTop   = Spot.y + Tall - CapTall;
    for (int I = 0; I < 16; ++I)
    {
        const float A = float(I) / 16.0f, B = float(I + 1) / 16.0f;
        Draw->AddRectFilled({ Spot.x, CapTop + CapTall * A }, { Spot.x + Wide, CapTop + CapTall * B + 1.0f },
                            IM_COL32(5, 7, 15, int(224.0f * (A + B) * 0.5f + 0.5f)));
    }
    Boxed(Draw, Face, Spot.x + 12.0f, CapTop + 8.0f, 12.5f, TextFull, Sign.Name);
    PaintTrackedFlow(Draw, Face, Spot.x + 12.0f, CapTop + 8.0f + Grind(12.5f) + 2.0f, 10.0f, TextFaint,
                     Spoken, 0.6f, CapLeft);
    // align-items: flex-end — the right column sits on the same bottom edge as the left.
    Boxed(Draw, Face, Spot.x + Wide - 12.0f - CapFlank, CapTop + CapTall - 9.0f - Grind(10.5f), 10.5f,
          TextDim, Flank);
    Draw->PopClipRect();
    const float R = CardRound;
    const float X1 = Spot.x + Wide, Y1 = Spot.y + Tall;
    CornerNotch(Draw, Spot,        { Spot.x + R, Spot.y + R }, R,  Pi,          Backdrop);
    CornerNotch(Draw, { X1, Spot.y }, { X1 - R, Spot.y + R },  R, -Pi * 0.5f,   Backdrop);
    CornerNotch(Draw, { X1, Y1 },     { X1 - R, Y1 - R },      R,  0.0f,        Backdrop);
    CornerNotch(Draw, { Spot.x, Y1 }, { Spot.x + R, Y1 - R },  R,  Pi * 0.5f,   Backdrop);
    Draw->AddRect(Spot, { X1, Y1 }, IM_COL32(46, 46, 46, 255), R, 0, 1.0f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 THE RAIL AND THE DUO
//------------------------------------------------------------------------------------------------------------------------

constexpr float PillTall = 7.0f + 14.0f + 1.0f + 9.0f + 8.0f + 2.0f;
constexpr ImU32 PanelFill = IM_COL32(18, 18, 18, 255);   // [-] --panel #121212

// Four pills, the last a quarter wider: .mp-rail is repeat(3,1fr) 1.25fr.
inline void PaintRail(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide,
                      const TraceLog& Log, const WindDraft& Draft)
{
    const float Gap = 5.0f, Share = (Wide - Gap * 3.0f) / 4.25f;
    const BeaufortBand Band = Force(Draft.Speed);
    char Value[4][24], Unit[4][12];
    std::snprintf(Value[0], sizeof(Value[0]), "%.1f", double(Draft.Speed));      std::snprintf(Unit[0], 12, "m/s");
    std::snprintf(Value[1], sizeof(Value[1]), "%.1f", double(Crest(Log)));       std::snprintf(Unit[1], 12, "m/s");
    std::snprintf(Value[2], sizeof(Value[2]), "%s", Compass(Draft.Direction));
    std::snprintf(Unit[2], 12, "%d\xc2\xb0", int(std::lround(Draft.Direction)));
    std::snprintf(Value[3], sizeof(Value[3]), "%d", Band.Force);                 std::snprintf(Unit[3], 12, "bf");
    const char* Keys[4] = { "Mean", "Gust", "From", "Force" };

    float X = Spot.x;
    for (int I = 0; I < 4; ++I)
    {
        const float Run = I == 3 ? Share * 1.25f : Share;
        Draw->AddRectFilled({ X, Spot.y }, { X + Run, Spot.y + PillTall }, PanelFill, 999.0f);
        Draw->AddRect({ X, Spot.y }, { X + Run, Spot.y + PillTall }, Stroke, 999.0f, 0, 1.0f);
        const float Lead = Tracked(Draw, Face, X + 9.0f, Spot.y + 7.0f, 14.0f, TextFull, Value[I], -0.3f);
        Tracked(Draw, Face, X + 9.0f + Lead + 2.0f, Spot.y + 7.0f + 14.0f - 9.5f, 9.5f, TextDim, Unit[I], 0.4f);
        char Key[16];
        Upper(Key, sizeof(Key), Keys[I]);
        Tracked(Draw, Face, X + 9.0f, Spot.y + 7.0f + 14.0f + 1.0f, 9.0f, TextFaint, Key, 1.1f);
        X += Run + Gap;
    }
}

// .mp-stat is a grid, not a stack: `grid-template-columns: 1fr auto` with `align-items: end`, the icon
//    spanning both columns above. The number takes its own width and the label takes what is left, so a long
//    label wraps inside its column rather than running under the number — and the card grows when it does.
constexpr float StatPadX    = 13.0f;   // [px] .mp-stat padding-left / right
constexpr float StatPadTop  = 11.0f;   // [px] .mp-stat padding-top
constexpr float StatPadFoot = 10.0f;   // [px] .mp-stat padding-bottom
constexpr float StatRowGap  =  2.0f;   // [px] .mp-stat row gap
constexpr float StatColGap  =  8.0f;   // [px] .mp-stat column gap
constexpr float StatIcon    = 19.0f;   // [px] .mp-stat .i
constexpr float StatIconPad =  3.5f;   // [px] the 12 px glyph centred in it
constexpr float StatLabel   = 11.0f;   // [px] .mp-stat .l font-size
constexpr float StatNumber  = 25.0f;   // [px] .mp-stat .n font-size, line-height 1
constexpr float StatTrack   = -1.4f;   // [px] .mp-stat .n letter-spacing
constexpr float StatUnit    = 11.0f;   // [px] .mp-stat .n em font-size
constexpr float StatUnitGap =  2.0f;   // [px] .mp-stat .n em margin-left

using StatGlyph = void (*)(ImDrawList*, ImVec2, float, ImU32);

// The auto column: the tracked number, then the inline unit that shares its baseline.
inline float StatValueWidth(ImFont* Face, const char* Body, const char* Unit)
{
    float Run = TrackedWidth(Face, StatNumber, Body, StatTrack);
    if (Unit && *Unit) Run += StatUnitGap + Measured(Face, StatUnit, Unit);
    return Run;
}

inline float StatLabelColumn(ImFont* Face, float Wide, const char* Body, const char* Unit)
{
    return std::max(1.0f, Wide - StatPadX * 2.0f - StatValueWidth(Face, Body, Unit) - StatColGap);
}

inline float StatHeight(ImFont* Face, float Wide, const char* Label, const char* Body, const char* Unit)
{
    const float Column = StatLabelColumn(Face, Wide, Body, Unit);
    const float Block  = std::max(Grind(StatLabel),
                                  Face->CalcTextSizeA(Grind(StatLabel), FLT_MAX, Column, Label).y);
    return StatPadTop + StatIcon + StatRowGap + std::max(Block, StatNumber) + StatPadFoot;
}

// A one-line label, which is what every card using the constant form carries.
constexpr float StatTall = StatPadTop + StatIcon + StatRowGap + StatNumber + StatPadFoot;

// Row is the height the grid row forces on the card; a .mp-duo cell stretches to its taller sibling. The
//    slack is shared between the two auto rows, so the icon drops by half of it and the label and number,
//    being bottom aligned, land on the card's own padding edge.
inline void PaintStat(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide, const char* Label,
                      const char* Body, const char* Unit, bool Down, StatGlyph Glyph, float Row = 0.0f)
{
    const float Natural = StatHeight(Face, Wide, Label, Body, Unit);
    const float Tall    = std::max(Row, Natural);
    const float Slack   = (Tall - Natural) * 0.5f;
    Draw->AddRectFilled(Spot, { Spot.x + Wide, Spot.y + Tall }, CardFill, CardRound);

    const ImU32 Ink    = Down ? IM_COL32(239, 68, 68, 255) : IM_COL32( 34, 197,  94, 255);
    const ImU32 Ground = Down ? IM_COL32(239, 68, 68,  36) : IM_COL32( 34, 197,  94,  33);
    const float IconTop = Spot.y + StatPadTop + Slack;
    Draw->AddRectFilled({ Spot.x + StatPadX, IconTop },
                        { Spot.x + StatPadX + StatIcon, IconTop + StatIcon }, Ground, 7.0f);
    if (Glyph) Glyph(Draw, { Spot.x + StatPadX + StatIconPad, IconTop + StatIconPad }, 12.0f, Ink);

    // align-items: end — both boxes sit on the bottom edge of the second row.
    const float Foot   = Spot.y + Tall - StatPadFoot;
    const float Column = StatLabelColumn(Face, Wide, Body, Unit);
    const float Block  = std::max(Grind(StatLabel),
                                  Face->CalcTextSizeA(Grind(StatLabel), FLT_MAX, Column, Label).y);
    Draw->AddText(Face, Grind(StatLabel), { Spot.x + StatPadX, Foot - Block }, TextDim, Label, nullptr,
                  Column);

    // line-height: 1 on a 1.302 em face, so the half leading is negative and the baseline sits 21.025 down.
    const float NumberTop = Foot - StatNumber + (StatNumber - Grind(StatNumber)) * 0.5f;
    const float Base      = NumberTop + StatNumber * AscentShare;
    const float Value     = StatValueWidth(Face, Body, Unit);
    const float Left      = Spot.x + Wide - StatPadX - Value;
    Tracked(Draw, Face, Left, NumberTop, StatNumber, TextFull, Body, StatTrack);
    if (Unit && *Unit)
        Boxed(Draw, Face, Spot.x + Wide - StatPadX - Measured(Face, StatUnit, Unit),
              Base - StatUnit * AscentShare, StatUnit, TextDim, Unit);
}

// icons.js P.wind and P.alert on their 24 unit grid.
inline void PaintWindGlyph(ImDrawList* Draw, ImVec2 Spot, float Size, ImU32 Colour)
{
    const float U = Size / 24.0f, Thick = 1.75f * U;
    auto At = [&](float X, float Y) { return ImVec2{ Spot.x + X * U, Spot.y + Y * U }; };
    Draw->AddLine(At(3, 8), At(12, 8), Colour, Thick);
    Draw->PathArcTo(At(12, 5), 3.0f * U, 0.5f * Pi, -0.5f * Pi, 12);
    Draw->PathStroke(Colour, 0, Thick);
    Draw->AddLine(At(3, 13), At(16, 13), Colour, Thick);
    Draw->PathArcTo(At(16, 16), 3.0f * U, -0.5f * Pi, 0.5f * Pi, 12);
    Draw->PathStroke(Colour, 0, Thick);
    Draw->AddLine(At(3, 18), At(10, 18), Colour, Thick);
}

inline void PaintAlertGlyph(ImDrawList* Draw, ImVec2 Spot, float Size, ImU32 Colour)
{
    const float U = Size / 24.0f, Thick = 1.75f * U;
    auto At = [&](float X, float Y) { return ImVec2{ Spot.x + X * U, Spot.y + Y * U }; };
    Draw->AddTriangle(At(12, 3), At(2, 20), At(22, 20), Colour, Thick);
    Draw->AddLine(At(12, 10), At(12, 14), Colour, Thick);
    Draw->AddLine(At(12, 17.2f), At(12, 17.6f), Colour, Thick);
}

inline float DuoHeight(ImFont* Face, float Wide, const TraceLog& Log)
{
    const float Half = (Wide - 10.0f) * 0.5f;
    char High[16], Low[16];
    std::snprintf(High, sizeof(High), "%.1f", double(Crest(Log)));
    std::snprintf(Low,  sizeof(Low),  "%.1f", double(Lull(Log)));
    return std::max(StatHeight(Face, Half, "Gusting to", High, "m/s"),
                    StatHeight(Face, Half, "Lulling to", Low,  "m/s"));
}

inline void PaintDuo(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide, const TraceLog& Log)
{
    const float Gap = 10.0f, Half = (Wide - Gap) * 0.5f;
    const float High = Crest(Log), Low = Lull(Log);
    const bool  Alarmed = High > 17.0f;
    for (int I = 0; I < 2; ++I)
    {
        char Body[16];
        std::snprintf(Body, sizeof(Body), "%.1f", double(I == 0 ? High : Low));
        const bool Down = I == 0 && Alarmed;
        PaintStat(Draw, Face, { Spot.x + I * (Half + Gap), Spot.y }, Half,
                  I == 0 ? "Gusting to" : "Lulling to", Body, "m/s", Down,
                  Down ? PaintAlertGlyph : PaintWindGlyph, DuoHeight(Face, Wide, Log));
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                            THE BEAUFORT SCALE AND THE REST
//------------------------------------------------------------------------------------------------------------------------

constexpr float ScaleTall = 44.0f;

// .wf-meter: a header row holding the key and the stepper, then the scale under it.
constexpr float MeterLift  = 4.0f;    // [px] the stepper's inset from the meter's top
constexpr float MeterKey   = 8.5f;    // [px] .mp-k font-size
constexpr float MeterGap   = 2.0f;    // [px] between the header row and the scale
inline float MeterHeadTall() { return MeterLift + StepBox + MeterGap; }
inline float MeterTallness() { return MeterHeadTall() + ScaleTall + 2.0f; }

inline void PaintScale(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide, const WindDraft& Draft)
{
    const float Pad = 8.0f, Span = Wide - Pad * 2.0f, Y = Spot.y + 10.0f, Deep = 14.0f, Ceiling = 30.0f;
    auto PX = [&](float Value) { return Spot.x + Pad + std::clamp(Value / Ceiling, 0.0f, 1.0f) * Span; };

    float Previous = 0.0f;
    for (int I = 0; I <= 11; ++I)
    {
        const float Limit = I < 11 ? Beaufort[I].Limit : 30.0f;
        const int   Band  = I < 11 ? Beaufort[I].Force : 11;
        const float X0 = PX(Previous), X1 = PX(Limit);
        const float Heat = float(Band) / 11.0f;
        // 85% opaque over the meter's black ground.
        const ImU32 Ink = IM_COL32(int(std::lround(60 + Heat * 195) * 0.85f),
                                   int(std::lround(200 - Heat * 120) * 0.85f),
                                   int(std::lround(180 - Heat * 120) * 0.85f), 255);
        Draw->AddRectFilled({ X0, Y }, { X0 + std::max(1.0f, X1 - X0 - 1.0f), Y + Deep }, Ink);
        if (Band % 2 == 0 && X1 - X0 > 9.0f)
        {
            char Number[4];
            std::snprintf(Number, sizeof(Number), "%d", Band);
            Inked(Draw, Face, (X0 + X1) * 0.5f, Y + Deep - 4.0f, 7.5f, IM_COL32(0, 0, 0, 140), Number,
                  Anchor::Middle);
        }
        Previous = Limit;
    }

    const struct { float At; const char* Label; } Ticks[4] = { { 0, "0" }, { 10, "10" }, { 20, "20" }, { 30, "30 m/s" } };
    for (const auto& Tick : Ticks)
    {
        const float X = PX(Tick.At);
        Draw->AddLine({ X, Y + Deep + 1.0f }, { X, Y + Deep + 5.0f }, IM_COL32(255, 255, 255, 46), 1.0f);
        Inked(Draw, Face, X, Spot.y + ScaleTall - 3.0f, 8.0f, IM_COL32(255, 255, 255, 77), Tick.Label,
              Tick.At <= 0.0f ? Anchor::Start : Tick.At >= 30.0f ? Anchor::End : Anchor::Middle);
    }

    const float MX = PX(Draft.Speed);
    Draw->AddTriangleFilled({ MX, Y - 1.0f }, { MX + 4.0f, Y - 7.0f }, { MX - 4.0f, Y - 7.0f }, TraceHead);
    Draw->AddLine({ MX, Y }, { MX, Y + Deep }, IM_COL32(255, 255, 255, 242), 1.4f);
}

// The shared card head: title, subtitle, and whatever the body needs beneath.

inline void PaintHead(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, const char* Title, const char* Under)
{
    Boxed(Draw, Face, Spot.x, Spot.y, HeadTitle, TextFull, Title);
    char Shown[64];
    Upper(Shown, sizeof(Shown), Under);
    Tracked(Draw, Face, Spot.x, Spot.y + Grind(HeadTitle) + HeadGap, HeadSub, TextFaint, Shown, 0.9f);
}

inline float BeaufortHeight()
{
    return CardPadTop + HeadHeight()
         + 2.0f + MeterTallness() + 10.0f
         + TapeHeight()
         + 10.5f + 14.0f;
}

inline void PaintBeaufortCard(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide, const WindDraft& Draft)
{
    const float Inner = Wide - CardPadX * 2.0f, Left = Spot.x + CardPadX;
    Draw->AddRectFilled(Spot, { Spot.x + Wide, Spot.y + BeaufortHeight() }, CardFill, CardRound);
    float Y = Spot.y + CardPadTop;
    PaintHead(Draw, Face, { Left, Y }, "Beaufort", "Force \xc2\xb7 bearing");
    Y += HeadHeight() + 2.0f;

    const float MeterTall = MeterTallness();
    Draw->AddRectFilled({ Left, Y }, { Left + Inner, Y + MeterTall }, FieldFill, CardRound);
    Draw->AddRect({ Left, Y }, { Left + Inner, Y + MeterTall }, Stroke, CardRound, 0, 1.0f);
    char Key[20];
    Upper(Key, sizeof(Key), "wind speed");
    Tracked(Draw, Face, Left + 9.0f, Y + MeterLift + (StepBox - Grind(MeterKey)) * 0.5f, MeterKey,
            TextFaint, Key, 1.1f);
    PaintStepper(Draw, Face, { Left + Inner - 9.0f - StepperWidth(Face, "m/s"), Y + MeterLift },
                 Draft.Speed, 1, "m/s");
    PaintScale(Draw, Face, { Left, Y + MeterHeadTall() }, Inner, Draft);
    Y += MeterTall + 10.0f;

    static const Mark Rose[5] = { { 0.0f, "N" }, { 0.25f, "E" }, { 0.5f, "S" }, { 0.75f, "W" }, { 1.0f, "N" } };
    PaintTape(Draw, Face, { Left, Y }, Inner, "Coming from", Draft.Direction, 0.0f, 360.0f, 0, "\xc2\xb0", Rose, 5);
    Y += TapeHeight();

    const BeaufortBand Band = Force(Draft.Speed);
    char Named[40];
    std::snprintf(Named, sizeof(Named), "%s", Band.Name);
    for (char* Scan = Named; *Scan; ++Scan) *Scan = static_cast<char>(std::tolower(static_cast<unsigned char>(*Scan)));
    char Note[160];
    std::snprintf(Note, sizeof(Note), "Force %d \xc2\xb7 %s \xe2\x80\x94 %s.", Band.Force, Named, LandSign(Draft.Speed));
    Tracked(Draw, Face, Left + 2.0f, Y, 10.5f, TextFaint, Note, 0.2f);
}

inline float SteadinessHeight()
{
    return CardPadTop + HeadHeight() + TapeHeight() * 2.0f
         + (SpecPadY * 2 + SpecKey + 1 + SpecValue) + 14.0f;
}

inline void PaintSteadiness(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide, const WindDraft& Draft)
{
    const float Inner = Wide - CardPadX * 2.0f, Left = Spot.x + CardPadX;
    Draw->AddRectFilled(Spot, { Spot.x + Wide, Spot.y + SteadinessHeight() }, CardFill, CardRound);
    float Y = Spot.y + CardPadTop;
    PaintHead(Draw, Face, { Left, Y }, "Steadiness", "Gust \xc2\xb7 turbulence");
    Y += HeadHeight();

    static const Mark GustMarks[3] = { { 0.0f, "STEADY" }, { 0.3f, "BREEZY" }, { 1.0f, "SQUALLY" } };
    static const Mark TurbMarks[3] = { { 0.0f, "LAMINAR" }, { 0.24f, "OPEN AIR" }, { 1.0f, "ROTOR" } };
    PaintTape(Draw, Face, { Left, Y }, Inner, "Gustiness", Draft.Gust, 0, 1, 2, "", GustMarks, 3);
    Y += TapeHeight();
    PaintTape(Draw, Face, { Left, Y }, Inner, "Turbulence", Draft.Turbulence, 0, 1, 2, "", TurbMarks, 3);
    Y += TapeHeight();

    char Values[2][24];
    std::snprintf(Values[0], sizeof(Values[0]), "%.0f%%", double(Draft.Turbulence * 100.0f));
    std::snprintf(Values[1], sizeof(Values[1]), "%.2f\xc3\x97", double(1.0f + Draft.Gust * 0.6f));
    const char* Keys[2] = { "turbulence intensity", "gust to mean" };
    const float TileW = (Inner - SpecGap) * 0.5f, TileH = SpecPadY * 2 + SpecKey + 1 + SpecValue;
    for (int I = 0; I < 2; ++I)
    {
        const float X = Left + I * (TileW + SpecGap);
        Draw->AddRectFilled({ X, Y }, { X + TileW, Y + TileH }, FieldFill, SpecRound);
        Draw->AddRect({ X, Y }, { X + TileW, Y + TileH }, Stroke, SpecRound, 0, 1.0f);
        char Caption[40];
        Upper(Caption, sizeof(Caption), Keys[I]);
        Tracked(Draw, Face, X + SpecPadX, Y + SpecPadY, SpecKey, TextFaint, Caption, 1.1f);
        Tracked(Draw, Face, X + SpecPadX, Y + SpecPadY + SpecKey + 1.0f, SpecValue, TextFull, Values[I], -0.2f);
    }
}

// The things in the scene that follow this field. Each is a pill that can be cut loose.
struct Follower { const char* Name; bool Linked; };

constexpr float TagTall = 4.0f + 8.5f + 5.0f;

// .mpanel .mp-note, 10 px with .2 px tracking and 2 px of padding either side.
constexpr const char* DrivingNote =
    "Clouds and water can each be cut loose from the field \xe2\x80\x94 switch one off and it keeps its "
    "own drift.";

inline float DrivingHeight(ImFont* Face, float Wide, const Follower* Flock, int Count)
{
    const float Inner = Wide - CardPadX * 2.0f;
    float X = 0.0f, Rows = 1.0f;
    for (int I = 0; I < Count; ++I)
    {
        char Shown[40];
        Upper(Shown, sizeof(Shown), Flock[I].Name);
        const float Run = Tracked(nullptr, Face, 0, 0, 8.5f, 0, Shown, 1.2f, false) + 18.0f;
        if (X > 0.0f && X + Run > Inner) { Rows += 1.0f; X = 0.0f; }
        X += Run + 5.0f;
    }
    return CardPadTop + HeadHeight() + 8.0f + Rows * TagTall + (Rows - 1.0f) * 5.0f + 2.0f
         + 8.0f + TrackedFlowHeight(Face, 10.0f, DrivingNote, 0.2f, Inner - 2.0f) + 14.0f;
}

inline void PaintDriving(ImDrawList* Draw, ImFont* Face, ImVec2 Spot, float Wide,
                         const Follower* Flock, int Count)
{
    const float Inner = Wide - CardPadX * 2.0f, Left = Spot.x + CardPadX;
    Draw->AddRectFilled(Spot, { Spot.x + Wide, Spot.y + DrivingHeight(Face, Wide, Flock, Count) },
                        CardFill, CardRound);
    float Y = Spot.y + CardPadTop;
    PaintHead(Draw, Face, { Left, Y }, "Driving", "Everything that follows this field");
    Y += HeadHeight() + 8.0f;

    float X = Left;
    for (int I = 0; I < Count; ++I)
    {
        char Shown[40];
        Upper(Shown, sizeof(Shown), Flock[I].Name);
        const float Run = Tracked(Draw, Face, 0, 0, 8.5f, 0, Shown, 1.2f, false) + 18.0f;
        if (X > Left && X + Run > Left + Inner) { X = Left; Y += TagTall + 5.0f; }
        const bool On = Flock[I].Linked;
        if (On) Draw->AddRectFilled({ X, Y }, { X + Run, Y + TagTall }, CardFill, 999.0f);
        Draw->AddRect({ X, Y }, { X + Run, Y + TagTall },
                      On ? IM_COL32(46, 46, 46, 255) : Stroke, 999.0f, 0, 1.0f);
        Tracked(Draw, Face, X + 9.0f, Y + 4.0f, 8.5f, On ? TextFull : TextFaint, Shown, 1.2f);
        X += Run + 5.0f;
    }
    Y += TagTall + 2.0f + 8.0f;
    PaintTrackedFlow(Draw, Face, Left + 2.0f, Y, 10.0f, TextFaint, DrivingNote, 0.2f, Inner - 2.0f);
}

} // namespace Frontier::WindInstrument
