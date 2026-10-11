//==============================================================================================================================================
//                                                          FRACTURECARDSURFACE.H
//==============================================================================================================================================
// 📦 FracturePanel.jsx: the per-object fracture card appended to the geometry inspector, and the shaded
//    solid it draws from FractureProjection.js. Nothing in Engine/ carried either before this.
//
//    The card is a disclosure in three stages. Closed it is a title and one switch; enabled it grows the
//    shaded diagram, the BAKE mode pair and a status line; in Baked mode it grows an SDF block whose own
//    switch grows a resolution field. Every stage is measured from Editor.css rather than guessed.
//
//    The diagram is a real Voronoi diagram: nine sites clipped against a 36-gon by half-planes, each cell
//    shaded from its own centroid and lifted onto a sphere. It is deterministic, so the harness checks the
//    cell count, the tessellation and the shading rather than eyeballing a picture.

#pragma once

#include "WindInstrumentSurface.h"
#include <vector>

namespace Frontier::Fracture
{

namespace Kit = Frontier::WindInstrument;

//------------------------------------------------------------------------------------------------------------------------
//                                                   PALETTE AND GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

constexpr ImU32 CardFill    = IM_COL32( 26,  26,  26, 255);  // [-] .generic-card background #1a1a1a
constexpr ImU32 CardEdge    = IM_COL32(255, 255, 255,  13);  // [-] .generic-card border #ffffff0d
constexpr ImU32 TitleInk    = IM_COL32(202, 202, 202, 255);  // [-] h3 #cacaca
constexpr ImU32 EyebrowInk  = IM_COL32(104, 104, 104, 255);  // [-] .fracture-card header small #686868
constexpr ImU32 HeadInk     = IM_COL32(137, 137, 137, 255);  // [-] .fracture-card h4 #898989
constexpr ImU32 SwitchInk   = IM_COL32(170, 170, 170, 255);  // [-] .switch-row #aaa
constexpr ImU32 NoteInk     = IM_COL32(118, 118, 118, 255);  // [-] .fracture-card p #767676
constexpr ImU32 FieldInk    = IM_COL32(145, 145, 145, 255);  // [-] .field > span #919191
constexpr ImU32 ButtonFill  = IM_COL32( 34,  34,  34, 255);  // [-] button background #222
constexpr ImU32 ButtonInk   = IM_COL32(240, 240, 240, 255);  // [-] body colour #f0f0f0, inherited
constexpr ImU32 TrackFill   = IM_COL32( 14,  14,  14, 255);  // [-] .fracture-mode background #0e0e0e
constexpr ImU32 TrackEdge   = IM_COL32(255, 255, 255,  11);  // [-] .fracture-mode border #ffffff0b
constexpr ImU32 PressedFill = IM_COL32( 52,  52,  52, 255);  // [-] .fracture-mode [aria-pressed] #343434
constexpr ImU32 PressedInk  = IM_COL32(238, 238, 238, 255);  // [-] and its colour #eee
constexpr ImU32 SwitchOff   = IM_COL32( 60,  60,  60, 255);  // [-] .toggle #3c3c3c
constexpr ImU32 SwitchOn    = IM_COL32( 52, 199,  89, 255);  // [-] .toggle.on #34c759
constexpr ImU32 Knob        = IM_COL32(238, 238, 238, 255);  // [-] .toggle i #eee
constexpr ImU32 Rule        = IM_COL32(255, 255, 255,  10);  // [-] .fracture-sdf border-top #ffffff0a
constexpr ImU32 SelectFill  = IM_COL32(  0,   0,   0, 255);  // [-] select background #000
constexpr ImU32 SelectInk   = IM_COL32(180, 188, 182, 255);  // [-] .fracture-sdf select #b4bcb6

constexpr float CardRound   = 22.0f;   // [px] .generic-card border-radius
constexpr float PadX        = 14.0f;   // [px] .generic-card padding left/right
constexpr float PadY        = 18.0f;   // [px] .generic-card padding top/bottom
constexpr float TitleSize   = 15.0f;   // [px] .fracture-card h3
constexpr float TitleDrop   =  5.0f;   // [px] h3 margin-bottom
constexpr float EyebrowSize =  9.0f;   // [px] header small
constexpr float EyebrowTrack=  1.2f;   // [px] its letter-spacing
constexpr float ExpandBox   = 30.0f;   // [px] .fracture-expand is 30 square
constexpr float SwitchSize  = 11.0f;   // [px] .switch-row font-size
constexpr float SwitchGap   = 20.0f;   // [px] .switch-row margin
constexpr float ToggleWide  = 44.0f;   // [px] .toggle width
constexpr float ToggleTall  = 24.0f;   // [px] .toggle height
constexpr float KnobBox     = 20.0f;   // [px] .toggle i
constexpr float DiagramTall =134.0f;   // [px] .fracture-diagram svg height
constexpr float DiagramBleed=  8.0f;   // [px] .fracture-diagram margin: 0 -8px
constexpr float DiagramDrop =  8.0f;   // [px] and its margin-bottom
constexpr float BakeSize    = 10.0f;   // [px] .fracture-card h4
constexpr float BakeTrack   =  1.3f;   // [px] its letter-spacing
constexpr float BakeLift    = 18.0f;   // [px] h4 margin-top
constexpr float BakeDrop    = 10.0f;   // [px] h4 margin-bottom
constexpr float ModePad     =  4.0f;   // [px] .fracture-mode padding and gap
constexpr float ButtonTall  = 28.0f;   // [px] button min-height
constexpr float ModeRound   =  8.0f;   // [px] .fracture-mode border-radius
constexpr float ModeSize    = 11.0f;   // [px] .fracture-mode button font-size
constexpr float NoteSize    = 10.0f;   // [px] .fracture-card p font-size
constexpr float NoteLead    =  1.7f;   // [-]  its line-height
constexpr float NoteGap     = 14.0f;   // [px] p margin
constexpr float OpenTall    = 10.0f + 10.0f + 28.0f - 28.0f;  // padding only; min-height carries the rest
constexpr float OpenPad     = 10.0f;   // [px] .fracture-open padding
constexpr float OpenGap     = 14.0f;   // [px] .fracture-open margin
constexpr float OpenRound   = 16.0f;   // [px] button border-radius
constexpr float SdfLift     = 16.0f;   // [px] .fracture-sdf margin-top
constexpr float SdfPad      =  4.0f;   // [px] .fracture-sdf padding-top
constexpr float FieldGap    = 20.0f;   // [px] .field margin
constexpr float FieldLabel  = 11.0f;   // [px] .field > span font-size
constexpr float FieldDrop   =  8.0f;   // [px] .field > span margin-bottom
constexpr float SelectTall  = 32.0f;   // [px] select height
constexpr float SelectRound =  4.0f;   // [px] .fracture-sdf select border-radius
constexpr float SelectSize  = 11.0f;   // [px] .fracture-sdf select font-size
constexpr float SelectPad   = 13.0f;   // [px] select padding-left

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE SETTINGS RECORD
//------------------------------------------------------------------------------------------------------------------------
// FractureSpecification.js owns these. Normalise() reproduces its clamps exactly, including the two keys
//    that are rounded to whole numbers and the enumerations that fall back rather than clamp.

enum class Mode { Dynamic, Baked };

struct Settings
{
    bool  Enabled       = false;
    Mode  Run           = Mode::Dynamic;
    bool  PieceSdf      = false;
    int   SdfResolution = 64;
    float Energy        = 2500.0f;
    int   Seed          = 42;
    int   Ceiling       = 48;
    float MinimumSize   = 0.045f;
    float X = 0.0f, Y = 0.0f, Z = 0.0f;
    bool  Baked         = false;   // a stored bake exists
    bool  Ready         = false;   // ... and its signature still matches the owner
};

inline Settings Normalise(Settings Input)
{
    Settings Out = Input;
    if (Out.SdfResolution != 32 && Out.SdfResolution != 64 && Out.SdfResolution != 128) Out.SdfResolution = 64;
    Out.Energy      = std::clamp(Out.Energy, 0.0f, 50000.0f);
    Out.Seed        = int(std::lround(std::clamp(float(Out.Seed), 1.0f, 999999.0f)));
    Out.Ceiling     = int(std::lround(std::clamp(float(Out.Ceiling), 2.0f, 160.0f)));
    Out.MinimumSize = std::clamp(Out.MinimumSize, 0.002f, 0.3f);
    Out.X = std::clamp(Out.X, -1000.0f, 1000.0f);
    Out.Y = std::clamp(Out.Y, -1000.0f, 1000.0f);
    Out.Z = std::clamp(Out.Z, -1000.0f, 1000.0f);
    return Out;
}

// Describe().Primitive comes from the outliner icon with its "editor-" prefix stripped. Only these four
//    carry a convex preview; a torus says so, anything else is simply pending.
inline bool Supported(const char* Primitive)
{
    return Primitive && (!std::strcmp(Primitive, "cube") || !std::strcmp(Primitive, "sphere")
                      || !std::strcmp(Primitive, "cylinder") || !std::strcmp(Primitive, "cone"));
}

//------------------------------------------------------------------------------------------------------------------------
//                                             THE SHADED SOLID (FRACTUREPROJECTION.JS)
//------------------------------------------------------------------------------------------------------------------------

struct Point { double X = 0.0, Y = 0.0; };

constexpr int GlyphSites   =  9;    // [-] the nine seeds
constexpr int GlyphRing    = 36;    // [-] the perimeter is a 36-gon
constexpr double GlyphSpan = 58.0;  // [-] of radius 58
constexpr float  GlyphWide = 280.0f, GlyphTall = 134.0f;   // [px] viewBox

using Polygon = std::vector<Point>;

// Sutherland-Hodgman against the half-plane p.d <= Distance.
inline Polygon Clip(const Polygon& Shape, Point Direction, double Distance)
{
    Polygon Out;
    const size_t Count = Shape.size();
    for (size_t I = 0; I < Count; ++I)
    {
        const Point& Here = Shape[I];
        const Point& Next = Shape[(I + 1) % Count];
        const double Offset     = Here.X * Direction.X + Here.Y * Direction.Y - Distance;
        const double NextOffset = Next.X * Direction.X + Next.Y * Direction.Y - Distance;
        if (Offset <= 0.0) Out.push_back(Here);
        if ((Offset < 0.0) != (NextOffset < 0.0))
        {
            const double Fraction = Offset / (Offset - NextOffset);
            Out.push_back({ Here.X + (Next.X - Here.X) * Fraction, Here.Y + (Next.Y - Here.Y) * Fraction });
        }
    }
    return Out;
}

// Each cell is the perimeter clipped by the perpendicular bisector against every other site.
inline std::vector<Polygon> Cells(const std::vector<Point>& Sites, const Polygon& Perimeter)
{
    std::vector<Polygon> Out;
    Out.reserve(Sites.size());
    for (size_t I = 0; I < Sites.size(); ++I)
    {
        Polygon Shape = Perimeter;
        for (size_t S = 0; S < Sites.size(); ++S)
        {
            if (S == I) continue;
            const Point Direction { Sites[S].X - Sites[I].X, Sites[S].Y - Sites[I].Y };
            const double Distance = (Sites[S].X * Sites[S].X + Sites[S].Y * Sites[S].Y
                                   - Sites[I].X * Sites[I].X - Sites[I].Y * Sites[I].Y) / 2.0;
            Shape = Clip(Shape, Direction, Distance);
        }
        Out.push_back(Shape);
    }
    return Out;
}

inline ImU32 Shade(double Tone, bool Green = false)
{
    const int Level = int(std::lround(std::max(35.0, std::min(210.0, Tone))));
    return Green ? IM_COL32(Level - 12, Level + 3, Level - 4, 255)
                 : IM_COL32(Level,      Level + 2, Level + 1, 255);
}

inline std::vector<Point> GlyphSeeds()
{
    return { { -9, -6 }, { 19, -22 }, { -32, -29 }, { 41, 5 }, { -34, 8 },
             { 8, 28 }, { -19, 37 }, { 39, 39 }, { 2, -46 } };
}

inline Polygon GlyphPerimeter()
{
    Polygon Ring;
    Ring.reserve(GlyphRing);
    for (int I = 0; I < GlyphRing; ++I)
    {
        const double Angle = I * 3.14159265358979323846 / 18.0;
        Ring.push_back({ std::cos(Angle) * GlyphSpan, std::sin(Angle) * GlyphSpan });
    }
    return Ring;
}

// The cells, in the painter's order the component sorts them into: by the first vertex's Y. Array.sort is
//    stable, so equal keys keep their seed order.
inline std::vector<Polygon> GlyphPieces()
{
    std::vector<Polygon> Pieces = Cells(GlyphSeeds(), GlyphPerimeter());
    std::stable_sort(Pieces.begin(), Pieces.end(), [](const Polygon& A, const Polygon& B)
    {
        const double First  = A.empty() ? 0.0 : A.front().Y;
        const double Second = B.empty() ? 0.0 : B.front().Y;
        return First < Second;
    });
    return Pieces;
}

inline Point Centroid(const Polygon& Shape)
{
    Point Sum;
    for (const Point& P : Shape) { Sum.X += P.X / double(Shape.size()); Sum.Y += P.Y / double(Shape.size()); }
    return Sum;
}

// The solid's projection: a flat scale plus a parallax from the cell's own centre, lifted by the height of
//    a sphere of the perimeter's radius.
inline Point SolidProject(Point P, Point Centre)
{
    const double Lift = std::sqrt(std::max(0.0, GlyphSpan * GlyphSpan - Centre.X * Centre.X - Centre.Y * Centre.Y));
    return { 140.0 + P.X * 1.05 + Centre.X * 0.18,
              61.0 + P.Y * 0.77 + Centre.Y * 0.16 - Lift * 0.12 };
}

// One shaded facet: the extruded side walls on the receding edges, a triangle fan to the interior, then the
//    hairline outline. Concave cells are common here, so nothing is handed to AddConvexPolyFilled.
inline void PaintFacet(ImDrawList* Draw, const Polygon& Shape, int Index, ImVec2 Origin, float Scale)
{
    if (Shape.size() < 3) return;
    const Point Centre = Centroid(Shape);
    std::vector<Point> Surface;
    Surface.reserve(Shape.size());
    for (const Point& P : Shape) Surface.push_back(SolidProject(P, Centre));
    const Point Interior = SolidProject(Centre, Centre);
    const double Tone = 133.0 - Centre.X * 0.36 - Centre.Y * 0.35;

    auto Place = [&](const Point& P) { return ImVec2{ Origin.x + float(P.X) * Scale, Origin.y + float(P.Y) * Scale }; };

    const size_t Count = Surface.size();
    for (size_t S = 0; S < Count; ++S)
    {
        const Point& Here = Surface[S];
        const Point& Next = Surface[(S + 1) % Count];
        if (Next.X < Here.X)
            Draw->AddQuadFilled(Place(Here), Place(Next),
                                Place({ Next.X + 1.0, Next.Y + 7.0 }), Place({ Here.X + 1.0, Here.Y + 7.0 }),
                                Shade(Tone * 0.42));
    }
    for (size_t S = 0; S < Count; ++S)
    {
        const Point& Here = Surface[S];
        const Point& Next = Surface[(S + 1) % Count];
        Draw->AddTriangleFilled(Place(Here), Place(Next), Place(Interior),
                                Shade(Tone + std::sin(double(S) * 2.3 + Index) * 15.0, Index % 7 == 0));
    }
    for (size_t S = 0; S < Count; ++S)
        Draw->AddLine(Place(Surface[S]), Place(Surface[(S + 1) % Count]),
                      IM_COL32(212, 223, 212, 48), std::max(0.6f * Scale, 0.5f));
}

// The whole diagram. The SVG is xMidYMid meet in a box of its own height, so it scales down only when the
//    column is narrower than 280 and is centred in whatever is left.
inline void PaintGlyph(ImDrawList* Draw, ImVec2 Spot, float Wide)
{
    const float Scale = std::min(Wide / GlyphWide, 1.0f);
    const ImVec2 Origin { Spot.x + (Wide - GlyphWide * Scale) * 0.5f,
                          Spot.y + (DiagramTall - GlyphTall * Scale) * 0.5f };
    auto Place = [&](float X, float Y) { return ImVec2{ Origin.x + X * Scale, Origin.y + Y * Scale }; };
    Draw->AddEllipseFilled(Place(140.0f, 113.0f), { 71.0f * Scale, 9.0f * Scale }, IM_COL32(0, 0, 0, 48));
    const std::vector<Polygon> Pieces = GlyphPieces();
    for (size_t I = 0; I < Pieces.size(); ++I) PaintFacet(Draw, Pieces[I], int(I), Origin, Scale);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE CARD'S OWN PARTS
//------------------------------------------------------------------------------------------------------------------------

// The baseline that centres one line of Size inside a box of BoxTall. Inked() takes a baseline; a CSS line
//    box is Grind(Size) tall and puts the baseline AscentShare of an em below its top.
inline float Baseline(float BoxTop, float BoxTall, float Size)
{
    return BoxTop + (BoxTall - Kit::Grind(Size)) * 0.5f + Kit::AscentShare * Size;
}

// A block formatting context just rich enough for this card: adjacent siblings collapse their touching
//    margins to the larger of the two, which is why BAKE sits 18 px under the diagram and not 26.
struct Flow
{
    float Y = 0.0f;
    float Pending = 0.0f;
    bool  Sealed = false;   // a border or padding has broken the collapse at this edge

    float Place(float TopMargin, float Height, float BottomMargin)
    {
        Y += Sealed ? TopMargin : std::max(Pending, TopMargin);
        Sealed = false;
        const float Top = Y;
        Y += Height;
        Pending = BottomMargin;
        return Top;
    }
    void Seal(float Thickness) { Y += Pending + Thickness; Pending = 0.0f; Sealed = true; }
    float Close() { return Y + Pending; }
};

inline void PaintToggle(ImDrawList* Draw, ImVec2 Spot, bool On)
{
    Draw->AddRectFilled(Spot, { Spot.x + ToggleWide, Spot.y + ToggleTall }, On ? SwitchOn : SwitchOff,
                        ToggleTall * 0.5f);
    const float Slide = On ? KnobBox : 0.0f;   // .toggle.on i { transform: translateX(20px) }
    Draw->AddCircleFilled({ Spot.x + 2.0f + Slide + KnobBox * 0.5f, Spot.y + 2.0f + KnobBox * 0.5f },
                          KnobBox * 0.5f, Knob, 32);
}

// .switch-row: the label left, the toggle hard right, both centred on the 24 px the toggle imposes.
inline void PaintSwitchRow(ImDrawList* Draw, ImFont* Light, ImVec2 Spot, float Wide, const char* Label, bool On)
{
    Kit::Inked(Draw, Light, Spot.x, Baseline(Spot.y, ToggleTall, SwitchSize), SwitchSize, SwitchInk, Label);
    PaintToggle(Draw, { Spot.x + Wide - ToggleWide, Spot.y }, On);
}

// .fracture-card p is 10px on a 1.7 line-height, which is not the font's own line box, so the flow is
//    measured here rather than with the kit's default leading.
inline float NoteLeading() { return NoteSize * NoteLead; }

inline int NoteLines(ImFont* Light, float Wide, const char* Body)
{
    Kit::TrackedLine Lines[12];
    return std::max(1, Kit::TrackedWrap(Light, NoteSize, Body, 0.0f, Wide, Lines, 12));
}

inline float NoteHeight(ImFont* Light, float Wide, const char* Body)
{
    return NoteLines(Light, Wide, Body) * NoteLeading();
}

inline void PaintNote(ImDrawList* Draw, ImFont* Light, ImVec2 Spot, float Wide, const char* Body)
{
    Kit::TrackedLine Lines[12];
    const int Count = std::max(1, Kit::TrackedWrap(Light, NoteSize, Body, 0.0f, Wide, Lines, 12));
    for (int I = 0; I < Count; ++I)
    {
        char Piece[256];
        const size_t Taken = std::min(sizeof(Piece) - 1, size_t(Lines[I].To - Lines[I].From));
        std::memcpy(Piece, Lines[I].From, Taken);
        Piece[Taken] = 0;
        // Each line box is NoteLeading tall with the text centred in it, which is what line-height does.
        Kit::Inked(Draw, Light, Spot.x, Baseline(Spot.y + I * NoteLeading(), NoteLeading(), NoteSize),
                   NoteSize, NoteInk, Piece);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE CARD
//------------------------------------------------------------------------------------------------------------------------

struct Owner
{
    const char* Name      = "Cube";
    const char* Primitive = "cube";
};

// The status paragraph the component assembles from the mode and the bake state.
inline void StatusLine(const Settings& S, char* Out, size_t Room)
{
    const char* Lead = S.Run == Mode::Dynamic ? "Generate fragments on demand."
                                              : "Reuse stored geometry for this object.";
    const char* Tail = "";
    if (S.Run == Mode::Baked)
        Tail = !S.Ready ? "Open the editor to bake or refresh geometry."
             : S.PieceSdf ? "Browser geometry ready. SDF pending." : "Browser bake ready.";
    std::snprintf(Out, Room, "%s %s", Lead, Tail);
    // The JSX always emits the separating space, even when the tail is empty.
    size_t Length = std::strlen(Out);
    while (Length > 0 && Out[Length - 1] == ' ') Out[--Length] = 0;
}

inline void UnsupportedLine(const Owner& Who, char* Out, size_t Room)
{
    std::snprintf(Out, Room, "Fracture settings are available. %s",
                  Who.Primitive && !std::strcmp(Who.Primitive, "torus")
                      ? "Concave preview needs decomposition." : "Source geometry preview is pending.");
}

// Every y the card needs, in card-local coordinates. Measure() and Paint() share it so a height can never
//    drift from what is drawn.
struct Plan
{
    float Inner = 0.0f;
    float HeadTop = 0.0f, HeadTall = 0.0f, TitleTop = 0.0f, EyebrowTop = 0.0f, ExpandTop = 0.0f;
    float EnableTop = 0.0f;
    float DiagramTop = 0.0f, BakeTop = 0.0f, ModeTop = 0.0f, StatusTop = 0.0f;
    float SdfTop = 0.0f, SdfSwitchTop = 0.0f, FieldTop = 0.0f, SelectTop = 0.0f, SdfNoteTop = 0.0f;
    float OpenTop = 0.0f, PendingTop = 0.0f;
    float Tall = 0.0f;
    bool  HasSdf = false, HasField = false, HasPending = false;
};

inline Plan Measure(ImFont* Light, ImFont* Regular, float Wide, const Settings& Raw, const Owner& Who)
{
    const Settings S = Normalise(Raw);
    Plan P;
    P.Inner = Wide - PadX * 2.0f;
    Flow F;
    F.Y = PadY;
    F.Sealed = true;   // the card's own padding breaks collapsing at the top edge

    const float LeftTall = Kit::Grind(TitleSize) + TitleDrop + Kit::Grind(EyebrowSize);
    P.HeadTall = std::max(ExpandBox, LeftTall);
    P.HeadTop = F.Place(0.0f, P.HeadTall, 0.0f);
    P.TitleTop = P.HeadTop + (P.HeadTall - LeftTall) * 0.5f;
    P.EyebrowTop = P.TitleTop + Kit::Grind(TitleSize) + TitleDrop;
    P.ExpandTop = P.HeadTop + (P.HeadTall - ExpandBox) * 0.5f;

    P.EnableTop = F.Place(SwitchGap, ToggleTall, SwitchGap);

    if (S.Enabled)
    {
        P.DiagramTop = F.Place(0.0f, DiagramTall, DiagramDrop);
        P.BakeTop    = F.Place(BakeLift, Kit::Grind(BakeSize), BakeDrop);
        P.ModeTop    = F.Place(0.0f, ModePad * 2.0f + ButtonTall + 2.0f, 0.0f);

        char Status[256];
        StatusLine(S, Status, sizeof(Status));
        P.StatusTop = F.Place(NoteGap, NoteHeight(Light, P.Inner, Status), NoteGap);

        if (S.Run == Mode::Baked)
        {
            P.HasSdf = true;
            F.Y += std::max(F.Pending, SdfLift);
            F.Pending = 0.0f;
            P.SdfTop = F.Y;
            F.Seal(1.0f + SdfPad);            // border-top then padding-top
            P.SdfSwitchTop = F.Place(SwitchGap, ToggleTall, SwitchGap);
            if (S.PieceSdf)
            {
                P.HasField = true;
                const float FieldTall = Kit::Grind(FieldLabel) + FieldDrop + SelectTall;
                P.FieldTop  = F.Place(FieldGap, FieldTall, FieldGap);
                P.SelectTop = P.FieldTop + Kit::Grind(FieldLabel) + FieldDrop;
                P.SdfNoteTop = F.Place(NoteGap,
                                       NoteHeight(Light, P.Inner, "SDF authoring setting \xc2\xb7 generation pending."),
                                       NoteGap);
            }
            // .fracture-sdf has no bottom border or padding, so the last child's margin collapses out of it.
        }

        const float OpenTall = std::max(ButtonTall, OpenPad * 2.0f + Kit::Grind(ModeSize));
        P.OpenTop = F.Place(OpenGap, OpenTall, OpenGap);

        if (!Supported(Who.Primitive))
        {
            P.HasPending = true;
            char Pending[192];
            UnsupportedLine(Who, Pending, sizeof(Pending));
            P.PendingTop = F.Place(NoteGap, NoteHeight(Light, P.Inner, Pending), NoteGap);
        }
    }
    P.Tall = F.Close() + PadY;
    (void)Regular;
    return P;
}

inline float CardHeight(ImFont* Light, ImFont* Regular, float Wide, const Settings& S, const Owner& Who)
{
    return Measure(Light, Regular, Wide, S, Who).Tall;
}

inline void PaintCard(ImDrawList* Draw, ImFont* Light, ImFont* Regular, ImVec2 Spot, float Wide,
                      const Settings& Raw, const Owner& Who, ImU32 Backdrop = IM_COL32(9, 9, 9, 255))
{
    const Settings S = Normalise(Raw);
    const Plan P = Measure(Light, Regular, Wide, S, Who);
    const float Left = Spot.x + PadX;

    Draw->AddRectFilled(Spot, { Spot.x + Wide, Spot.y + P.Tall }, CardFill, CardRound);
    Draw->AddRect(Spot, { Spot.x + Wide, Spot.y + P.Tall }, CardEdge, CardRound);

    // Header. h3 is the Regular face; everything else on this card is the Light body face.
    Kit::Boxed(Draw, Regular, Left, Spot.y + P.TitleTop, TitleSize, TitleInk, "Fracture");
    Kit::Tracked(Draw, Light, Left, Spot.y + P.EyebrowTop + Kit::AscentShare * EyebrowSize, EyebrowSize,
                 EyebrowInk, "PER-OBJECT GEOMETRY", EyebrowTrack);
    {
        const bool Live = S.Enabled;
        const ImVec2 At { Spot.x + Wide - PadX - ExpandBox, Spot.y + P.ExpandTop };
        const ImU32 Fill = Live ? ButtonFill : IM_COL32(34, 34, 34, 102);   // button:disabled { opacity:.4 }
        const ImU32 Edge = Live ? CardEdge : IM_COL32(255, 255, 255, 5);
        Draw->AddRectFilled(At, { At.x + ExpandBox, At.y + ExpandBox }, Fill, OpenRound);
        Draw->AddRect(At, { At.x + ExpandBox, At.y + ExpandBox }, Edge, OpenRound);
        Kit::Inked(Draw, Light, At.x + ExpandBox * 0.5f, Baseline(At.y, ExpandBox, 20.0f), 20.0f,
                   Live ? ButtonInk : IM_COL32(240, 240, 240, 102), "\xe2\x86\x97", Kit::Anchor::Middle);
    }

    PaintSwitchRow(Draw, Light, { Left, Spot.y + P.EnableTop }, P.Inner, "Enable fracture", S.Enabled);
    if (!S.Enabled) return;

    // The diagram bleeds 8 px into the card's padding on both sides.
    PaintGlyph(Draw, { Left - DiagramBleed, Spot.y + P.DiagramTop }, P.Inner + DiagramBleed * 2.0f);

    Kit::Tracked(Draw, Light, Left, Spot.y + P.BakeTop + Kit::AscentShare * BakeSize, BakeSize, HeadInk,
                 "BAKE", BakeTrack);

    // The mode pair: one inset track, two buttons sharing the width, the pressed one filled.
    {
        const float Top = Spot.y + P.ModeTop, Tall = ModePad * 2.0f + ButtonTall + 2.0f;
        Draw->AddRectFilled({ Left, Top }, { Left + P.Inner, Top + Tall }, TrackFill, ModeRound);
        Draw->AddRect({ Left, Top }, { Left + P.Inner, Top + Tall }, TrackEdge, ModeRound);
        const float Each = (P.Inner - 2.0f - ModePad * 3.0f) * 0.5f;
        const char* Names[2] = { "Dynamic", "Baked" };
        for (int I = 0; I < 2; ++I)
        {
            const float X = Left + 1.0f + ModePad + I * (Each + ModePad);
            const float Y = Top + 1.0f + ModePad;
            const bool On = (I == 0) == (S.Run == Mode::Dynamic);
            if (On) Draw->AddRectFilled({ X, Y }, { X + Each, Y + ButtonTall }, PressedFill, OpenRound);
            Kit::Inked(Draw, Light, X + Each * 0.5f, Baseline(Y, ButtonTall, ModeSize), ModeSize,
                       On ? PressedInk : ButtonInk, Names[I], Kit::Anchor::Middle);
        }
    }

    {
        char Status[256];
        StatusLine(S, Status, sizeof(Status));
        PaintNote(Draw, Light, { Left, Spot.y + P.StatusTop }, P.Inner, Status);
    }

    if (P.HasSdf)
    {
        Draw->AddLine({ Left, Spot.y + P.SdfTop }, { Left + P.Inner, Spot.y + P.SdfTop }, Rule, 1.0f);
        PaintSwitchRow(Draw, Light, { Left, Spot.y + P.SdfSwitchTop }, P.Inner, "Bake SDF per piece", S.PieceSdf);
        if (P.HasField)
        {
            Kit::Inked(Draw, Light, Left, Spot.y + P.FieldTop + Kit::AscentShare * FieldLabel, FieldLabel,
                       FieldInk, "Resolution per piece");
            const float Top = Spot.y + P.SelectTop;
            Draw->AddRectFilled({ Left, Top }, { Left + P.Inner, Top + SelectTall }, SelectFill, SelectRound);
            Draw->AddRect({ Left, Top }, { Left + P.Inner, Top + SelectTall }, CardEdge, SelectRound);
            char Choice[32];
            std::snprintf(Choice, sizeof(Choice), "%d\xc2\xb3 \xc2\xb7 R16F", S.SdfResolution);
            Kit::Inked(Draw, Light, Left + SelectPad, Baseline(Top, SelectTall, SelectSize), SelectSize,
                       SelectInk, Choice);
            // The native select arrow sits in the 30 px reserved on the right.
            const float AX = Left + P.Inner - 17.0f, AY = Top + SelectTall * 0.5f - 1.0f;
            Draw->AddTriangleFilled({ AX - 4.0f, AY - 1.5f }, { AX + 4.0f, AY - 1.5f }, { AX, AY + 3.5f },
                                    SelectInk);
            PaintNote(Draw, Light, { Left, Spot.y + P.SdfNoteTop }, P.Inner,
                      "SDF authoring setting \xc2\xb7 generation pending.");
        }
    }

    // The expand button: label left, arrow right, justify-content: space-between.
    {
        const float Top = Spot.y + P.OpenTop;
        const float Tall = std::max(ButtonTall, OpenPad * 2.0f + Kit::Grind(ModeSize));
        Draw->AddRectFilled({ Left, Top }, { Left + P.Inner, Top + Tall }, ButtonFill, OpenRound);
        Draw->AddRect({ Left, Top }, { Left + P.Inner, Top + Tall }, CardEdge, OpenRound);
        char Label[160];
        std::snprintf(Label, sizeof(Label), "Edit %s fracture", Who.Name);
        Kit::Inked(Draw, Light, Left + OpenPad, Baseline(Top, Tall, ModeSize), ModeSize, ButtonInk, Label);
        Kit::Inked(Draw, Light, Left + P.Inner - OpenPad, Baseline(Top, Tall, ModeSize), ModeSize, ButtonInk,
                   "\xe2\x86\x97", Kit::Anchor::End);
    }

    if (P.HasPending)
    {
        char Pending[192];
        UnsupportedLine(Who, Pending, sizeof(Pending));
        PaintNote(Draw, Light, { Left, Spot.y + P.PendingTop }, P.Inner, Pending);
    }
    (void)Backdrop;
}

} // namespace Frontier::Fracture
