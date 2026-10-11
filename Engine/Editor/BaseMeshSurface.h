//==============================================================================================================================================
//                                                           BASEMESHSURFACE.H
//==============================================================================================================================================
// 📦 The five base meshes the shipped editor opens with, and the analytical markers it draws for them.
//
//    Reference: Experimental/ProjectZeroEditor/index.html, the built 4.87 MB bundle. Three pieces of it
//    meet here and they are all keyed off one thing — the row's icon:
//
//      Editor.jsx InitialRows    the roster: cube, sphere, cylinder, torus, cone, seated under Showcase
//      FractureSpecification.js  Describe().Primitive = Subject.Icon.replace(/^editor-/, "")
//      CheckerViewport.jsx       Shape() branches on Subject.Icon to draw the placement marker
//
//    The engine had none of that. Every geometry row carried IconSymbol::EditorMesh, so a cube and a torus
//    were the same row with different text, and the fracture card had to guess a primitive from the label.
//    The five artworks were already in EngineContent/Icons and in the icon manifest; they had simply never
//    been registered in IconSymbols.inc, so nothing could ask for them. They are registered now.
//
//    The markers are drawn here rather than rasterised, because the reference draws them from inline SVG
//    at a stroke width of 1.4 in a 80 x 76 viewBox, not from the icon files. Translating the eleven path
//    commands by hand keeps the geometry checkable: the harness pins every vertex.

#pragma once

#include "WindInstrumentSurface.h"
#include <cstring>

namespace Frontier::BaseMesh
{

namespace Kit = Frontier::WindInstrument;

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE ROSTER
//------------------------------------------------------------------------------------------------------------------------
// Editor.jsx:48-59. Six fields per row: Id, Name, Panel, Icon, Parent, Descriptor. The panel is "geometry"
//    and the parent is "showcase" for all five, so only the varying three are carried.

enum class Solid { Cube, Sphere, Cylinder, Torus, Cone, Count };

struct Entry
{
    const char* Id;
    const char* Name;
    const char* Icon;
    const char* Descriptor;
};

inline constexpr Entry Roster[] =
{
    { "cube",     "Cube",     "editor-cube",     "Geometry" },
    { "sphere",   "Sphere",   "editor-sphere",   "Geometry" },
    { "cylinder", "Cylinder", "editor-cylinder", "Geometry" },
    { "torus",    "Torus",    "editor-torus",    "Geometry" },
    { "cone",     "Cone",     "editor-cone",     "Geometry" },
};

static_assert(sizeof(Roster) / sizeof(Roster[0]) == size_t(Solid::Count),
              "the roster and the kind enumeration are the same five rows");

inline const Entry& Of(Solid Which) { return Roster[size_t(Which)]; }

// The group the five hang from — Editor.jsx:47, a "group" row with the folder-scene icon.
inline constexpr const char* GroupId         = "showcase";
inline constexpr const char* GroupName       = "Showcase";
inline constexpr const char* GroupIcon       = "folder-scene";
inline constexpr const char* GroupDescriptor = "Scene collection";

//------------------------------------------------------------------------------------------------------------------------
//                                              DESCRIBE().PRIMITIVE, EXACTLY
//------------------------------------------------------------------------------------------------------------------------
// FractureSpecification.js:68 — `Subject.Icon?.replace(/^editor-/, "")`. A row whose icon does not start
//    with the prefix keeps its icon name whole, and a row with no icon has no primitive at all. Both of
//    those are reproduced; the old native code sniffed the row's *label* instead, which is a different
//    thing entirely and got "Cube 2" wrong.

inline const char* PrimitiveOfIcon(const char* Icon)
{
    if (Icon == nullptr) return nullptr;
    const char* const Prefix = "editor-";
    const size_t Length = std::strlen(Prefix);
    return std::strncmp(Icon, Prefix, Length) == 0 ? Icon + Length : Icon;
}

inline const char* PrimitiveOf(Solid Which) { return PrimitiveOfIcon(Of(Which).Icon); }

// FractureStructure.js:21 — the seven the fracture editor can partition. FracturePanel.jsx narrows that to
//    the four a scene object can be, which is what Fracture::Supported() carries.
inline bool Partitionable(const char* Primitive)
{
    if (Primitive == nullptr) return false;
    for (const char* One : { "cube", "sphere", "cylinder", "cone", "pane", "beam", "rock" })
        if (std::strcmp(Primitive, One) == 0) return true;
    return false;
}

inline Solid SolidOfPrimitive(const char* Primitive)
{
    if (Primitive != nullptr)
        for (size_t I = 0; I < size_t(Solid::Count); ++I)
            if (std::strcmp(Primitive, Roster[I].Id) == 0) return Solid(I);
    return Solid::Count;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   PALETTE AND GEOMETRY
//------------------------------------------------------------------------------------------------------------------------
// Editor.css .checker-pane and .preview-placement.

constexpr ImU32 CheckerLight = IM_COL32( 29,  29,  29, 255);  // [-] repeating-conic #1d1d1d
constexpr ImU32 CheckerDark  = IM_COL32( 23,  23,  23, 255);  // [-] repeating-conic #171717
constexpr ImU32 MarkerFill   = IM_COL32( 23,  23,  23, 117);  // [-] .preview-placement svg fill #17171775
constexpr ImU32 MarkerInk    = IM_COL32(180, 192, 206, 255);  // [-] .preview-placement colour #b4c0ce
constexpr ImU32 MarkerPicked = IM_COL32(255, 180,  84, 255);  // [-] .selected colour #ffb454
constexpr ImU32 LabelInk     = IM_COL32(188, 188, 188, 255);  // [-] .preview-placement span #bcbcbc
constexpr ImU32 LabelPicked  = IM_COL32(240, 206, 158, 255);  // [-] .selected span #f0ce9e
constexpr ImU32 PickedFill   = IM_COL32(255, 180,  84,   8);  // [-] .selected background #ffb45408
constexpr ImU32 PickedEdge   = IM_COL32(255, 180,  84, 136);  // [-] .selected border #ffb45488
constexpr ImU32 HoverFill    = IM_COL32(  0,   0,   0,  68);  // [-] :hover background #0004
constexpr ImU32 HoverEdge    = IM_COL32(255, 255, 255,  36);  // [-] :hover border #ffffff24
constexpr ImU32 CaptionWash  = IM_COL32( 17,  17,  17, 119);  // [-] .checker-caption background #1117
constexpr ImU32 CaptionInk   = IM_COL32( 98,  98,  98, 255);  // [-] .checker-caption colour #626262
constexpr ImU32 EmptyInk     = IM_COL32(115, 115, 115, 255);  // [-] .checker-empty colour #737373
constexpr ImU32 EmptySmall   = IM_COL32( 94,  94,  94, 255);  // [-] .checker-empty small #5e5e5e

constexpr float CheckerTile  = 48.0f;   // [px] the conic gradient's own 48 x 48 tile, four 24 px quadrants
constexpr float MarkerWide   = 76.0f;   // [px] .preview-placement svg width
constexpr float MarkerTall   = 72.0f;   // [px] .preview-placement svg height
constexpr float MarkerStroke =  1.4f;   // [px] its stroke-width
constexpr float BoxWide      = 80.0f;   // [-]  viewBox width
constexpr float BoxTall      = 76.0f;   // [-]  viewBox height
constexpr float PlaceMin     = 126.0f;  // [px] .preview-placement min-height
constexpr float PlacePadTop  = 14.0f;   // [px] its padding-top
constexpr float PlacePadX    =  8.0f;   // [px] padding left / right
constexpr float PlacePadFoot = 10.0f;   // [px] padding-bottom
constexpr float PlaceGap     =  8.0f;   // [px] the column gap between marker and label
constexpr float PlaceRound   = 10.0f;   // [px] border-radius
constexpr float LabelSize    = 11.0f;   // [px] span font-size
constexpr float LabelLead    =  1.4f;   // [-]  its line-height
constexpr float GridMin      = 110.0f;  // [px] grid auto-fill minmax(110px, 1fr)
constexpr float GridGap      = 14.0f;   // [px] its gap
constexpr float GridPadY     = 28.0f;   // [px] .checker-placements padding top / bottom
constexpr float GridPadX     = 22.0f;   // [px] padding left / right
constexpr float CaptionSize  =  8.0f;   // [px] .checker-caption font-size
constexpr float CaptionTrack =  1.0f;   // [px] its letter-spacing
constexpr float CaptionPadY  =  9.0f;   // [px] padding top / bottom
constexpr float CaptionPadX  = 12.0f;   // [px] padding left / right
constexpr float EmptySize    = 12.0f;   // [px] .checker-empty font-size
constexpr float EmptySmallSz = 10.0f;   // [px] .checker-empty small
constexpr float EmptyGap     =  8.0f;   // [px] its gap
constexpr float EmptyMin     = 160.0f;  // [px] its min-height

inline constexpr const char* Caption = "HTML PREVIEW \xc2\xb7 ANALYTICAL MARKERS";

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE MARKER ARTWORK
//------------------------------------------------------------------------------------------------------------------------
// CheckerViewport.jsx Shape(), transcribed command by command. Every shape is given in viewBox units and
//    placed by one lambda, so the harness can assert the vertices rather than diff a picture.
//
//    SVG fills an unclosed path as though it were closed, which is why the cone's two-segment polyline and
//    the cylinder's open outline both carry the translucent fill.

struct Mark { float X = 0.0f, Y = 0.0f; };

// A path collected in viewBox units: a run of points, flagged for whether it is filled and/or closed.
struct Trace
{
    Mark  Points[64];
    int   Count   = 0;
    bool  Filled  = false;
    bool  Closed  = false;
    bool  Dashes  = false;
    void Add(float X, float Y) { if (Count < 64) Points[Count++] = { X, Y }; }
};

struct Oval
{
    float CX = 0.0f, CY = 0.0f, RX = 0.0f, RY = 0.0f;
};

struct Artwork
{
    Trace Traces[4];
    int   TraceCount = 0;
    Oval  Ovals[3];
    int   OvalCount = 0;
    Trace& Open() { return Traces[TraceCount++]; }
};

// A cubic in viewBox units, flattened to the sixteen segments ImGui would use anyway. Keeping the sampling
//    here rather than in the painter means the harness measures the same curve that is drawn.
inline void Curve(Trace& Into, Mark A, Mark B, Mark C, Mark D, int Steps = 16)
{
    for (int I = 1; I <= Steps; ++I)
    {
        const float T = float(I) / float(Steps), U = 1.0f - T;
        Into.Add(U * U * U * A.X + 3 * U * U * T * B.X + 3 * U * T * T * C.X + T * T * T * D.X,
                 U * U * U * A.Y + 3 * U * U * T * B.Y + 3 * U * T * T * C.Y + T * T * T * D.Y);
    }
}

inline Artwork Shape(Solid Which)
{
    Artwork Art;
    switch (Which)
    {
    case Solid::Cube:
    {
        // "m40 7 27 15v31L40 69 13 53V22Z"
        Trace& Body = Art.Open();
        Body.Filled = Body.Closed = true;
        Body.Add(40, 7); Body.Add(67, 22); Body.Add(67, 53); Body.Add(40, 69); Body.Add(13, 53); Body.Add(13, 22);
        // "m13 22 27 16 27-16" — the top-face vee
        Trace& Vee = Art.Open();
        Vee.Add(13, 22); Vee.Add(40, 38); Vee.Add(67, 22);
        // "M40 38v31" and "M40 7v31" are one vertical run through the centre
        Trace& Spine = Art.Open();
        Spine.Add(40, 7); Spine.Add(40, 69);
        break;
    }
    case Solid::Sphere:
    {
        // <circle cx=40 cy=38 r=28/> <ellipse rx=12 ry=28/> <ellipse rx=28 ry=10/>
        Art.Ovals[Art.OvalCount++] = { 40, 38, 28, 28 };
        Art.Ovals[Art.OvalCount++] = { 40, 38, 12, 28 };
        Art.Ovals[Art.OvalCount++] = { 40, 38, 28, 10 };
        break;
    }
    case Solid::Cylinder:
    {
        Art.Ovals[Art.OvalCount++] = { 40, 18, 25, 10 };
        // "M15 18v39c0 14 50 14 50 0V18"
        Trace& Body = Art.Open();
        Body.Filled = true;
        Body.Add(15, 18); Body.Add(15, 57);
        Curve(Body, { 15, 57 }, { 15, 71 }, { 65, 71 }, { 65, 57 });
        Body.Add(65, 18);
        // "M15 57c0-14 50-14 50 0" with stroke-dasharray 3 4 — the hidden far rim
        Trace& Hidden = Art.Open();
        Hidden.Dashes = true;
        Hidden.Add(15, 57);
        Curve(Hidden, { 15, 57 }, { 15, 43 }, { 65, 43 }, { 65, 57 });
        break;
    }
    case Solid::Torus:
    {
        Art.Ovals[Art.OvalCount++] = { 40, 38, 30, 23 };
        Art.Ovals[Art.OvalCount++] = { 40, 38, 15, 10 };
        break;
    }
    case Solid::Cone:
    {
        // "M14 57 40 9l26 48" — filled as though closed, then the base ellipse over it
        Trace& Body = Art.Open();
        Body.Filled = true;
        Body.Add(14, 57); Body.Add(40, 9); Body.Add(66, 57);
        Art.Ovals[Art.OvalCount++] = { 40, 57, 26, 10 };
        break;
    }
    default: break;
    }
    return Art;
}

// The marker, drawn into a box of the caller's choosing with SVG `meet` semantics: the uniform scale is
//    the smaller of the two ratios, and the artwork is centred in whatever the other axis leaves over.
inline void PaintMarkerIn(ImDrawList* Draw, ImVec2 Spot, float BoxW, float BoxH, Solid Which, ImU32 Ink,
                          ImU32 Fill, float Stroke)
{
    const float Scale = ImMin(BoxW / BoxWide, BoxH / BoxTall);
    const ImVec2 Origin { Spot.x + (BoxW - BoxWide * Scale) * 0.5f,
                          Spot.y + (BoxH - BoxTall * Scale) * 0.5f };
    auto Place = [&](float X, float Y) { return ImVec2{ Origin.x + X * Scale, Origin.y + Y * Scale }; };
    const float Thick = Stroke * Scale;
    const ImU32 MarkerFill = Fill;

    const Artwork Art = Shape(Which);
    for (int O = 0; O < Art.OvalCount; ++O)
    {
        const Oval& One = Art.Ovals[O];
        Draw->AddEllipseFilled(Place(One.CX, One.CY), { One.RX * Scale, One.RY * Scale }, MarkerFill, 0.0f, 64);
        Draw->AddEllipse(Place(One.CX, One.CY), { One.RX * Scale, One.RY * Scale }, Ink, 0.0f, 64, Thick);
    }
    for (int T = 0; T < Art.TraceCount; ++T)
    {
        const Trace& One = Art.Traces[T];
        if (One.Count < 2) continue;
        if (One.Filled)
        {
            // Both filled traces here are convex in their own right; the cube's hexagon and the cone's
            //    triangle. AddConvexPolyFilled is safe for exactly that reason and no other.
            ImVec2 Hull[64];
            for (int I = 0; I < One.Count; ++I) Hull[I] = Place(One.Points[I].X, One.Points[I].Y);
            Draw->AddConvexPolyFilled(Hull, One.Count, MarkerFill);
        }
        for (int I = 0; I + 1 < One.Count; ++I)
        {
            const ImVec2 A = Place(One.Points[I].X, One.Points[I].Y);
            const ImVec2 B = Place(One.Points[I + 1].X, One.Points[I + 1].Y);
            if (One.Dashes) Kit::Dashed(Draw, A, B, Ink, 3.0f * Scale, 4.0f * Scale, Thick);
            else            Draw->AddLine(A, B, Ink, Thick);
        }
        if (One.Closed)
            Draw->AddLine(Place(One.Points[One.Count - 1].X, One.Points[One.Count - 1].Y),
                          Place(One.Points[0].X, One.Points[0].Y), Ink, Thick);
    }
}

// The shipped size: the viewBox is 80 x 76 and the CSS box is 76 x 72, so the scale is 0.95 either way —
//    the aspect ratios agree, so nothing is letterboxed.
inline void PaintMarker(ImDrawList* Draw, ImVec2 Spot, Solid Which, bool Picked)
{
    PaintMarkerIn(Draw, Spot, MarkerWide, MarkerTall, Which, Picked ? MarkerPicked : MarkerInk, MarkerFill,
                  MarkerStroke);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE PLACEMENT BUTTON
//------------------------------------------------------------------------------------------------------------------------

enum class Pointer { Idle, Hover };

// .preview-placement is a 126 px minimum column: 14 px of padding, the 72 px marker, an 8 px gap, the
//    label's line boxes, then 10 px. With a one-line label that is 14+72+8+15.4+10 = 119.4, under the
//    minimum, so the box stays 126 and the content sits at the top.
inline float PlacementHeight(int LabelLines)
{
    const float Used = PlacePadTop + MarkerTall + PlaceGap + float(LabelLines) * LabelSize * LabelLead + PlacePadFoot;
    return ImMax(Used, PlaceMin);
}

inline void PaintPlacement(ImDrawList* Draw, ImFont* Light, ImVec2 Spot, float Wide, float Tall,
                           Solid Which, const char* Name, bool Picked, Pointer State)
{
    if (Picked)
    {
        Draw->AddRectFilled(Spot, { Spot.x + Wide, Spot.y + Tall }, PickedFill, PlaceRound);
        Draw->AddRect(Spot, { Spot.x + Wide, Spot.y + Tall }, PickedEdge, PlaceRound);
    }
    else if (State == Pointer::Hover)
    {
        Draw->AddRectFilled(Spot, { Spot.x + Wide, Spot.y + Tall }, HoverFill, PlaceRound);
        Draw->AddRect(Spot, { Spot.x + Wide, Spot.y + Tall }, HoverEdge, PlaceRound);
    }
    // The flex column centres nothing horizontally — align-items defaults to stretch, and the svg has a
    //    fixed 76 px width, so it sits at the content box's left edge.
    PaintMarker(Draw, { Spot.x + PlacePadX, Spot.y + PlacePadTop }, Which, Picked);
    const float Line = LabelSize * LabelLead;
    Kit::Inked(Draw, Light, Spot.x + PlacePadX,
               Spot.y + PlacePadTop + MarkerTall + PlaceGap + (Line - Kit::Grind(LabelSize)) * 0.5f
                   + Kit::AscentShare * LabelSize,
               LabelSize, Picked ? LabelPicked : LabelInk, Name);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE CHECKER PANE
//------------------------------------------------------------------------------------------------------------------------

// repeating-conic-gradient(#1d1d1d 0% 25%, #171717 0% 50%) 0 0 / 48px 48px. A conic sweep starts at twelve
//    o'clock and runs clockwise, so the quadrants are light, dark, light, dark from the top-right round —
//    an ordinary 24 px checkerboard whose phase is set by the tile, not by the pane.
inline void PaintChecker(ImDrawList* Draw, ImVec2 Spot, float Wide, float Tall)
{
    Draw->AddRectFilled(Spot, { Spot.x + Wide, Spot.y + Tall }, CheckerDark);
    const float Half = CheckerTile * 0.5f;
    for (float Y = 0.0f; Y < Tall; Y += Half)
        for (float X = 0.0f; X < Wide; X += Half)
        {
            const int Column = int(X / Half), Row = int(Y / Half);
            if (((Column + Row) & 1) != 0) continue;   // the dark quadrants are already the base fill
            Draw->AddRectFilled({ Spot.x + X, Spot.y + Y },
                                { Spot.x + ImMin(X + Half, Wide), Spot.y + ImMin(Y + Half, Tall) }, CheckerLight);
        }
}

// auto-fill, minmax(110px, 1fr): as many 110 px columns as fit with 14 px gaps, then each stretched to
//    share what is left. The count is what the browser's grid algorithm gives.
inline int Columns(float Wide)
{
    const float Inner = Wide - GridPadX * 2.0f;
    if (Inner < GridMin) return 1;
    return ImMax(1, int((Inner + GridGap) / (GridMin + GridGap)));
}

struct Pane
{
    ImVec2 Spot {};
    float  Wide = 0.0f, Tall = 0.0f;
    int    Picked = -1;        // index into the visible roster, -1 for none
    int    Hovered = -1;
    int    Count = int(Solid::Count);
};

inline float CaptionHeight() { return CaptionPadY * 2.0f + Kit::Grind(CaptionSize); }

// The whole pane: checker, the placement grid, then the caption bar pinned to the bottom.
inline void PaintPane(ImDrawList* Draw, ImFont* Light, const Pane& P)
{
    PaintChecker(Draw, P.Spot, P.Wide, P.Tall);
    const float Bar = CaptionHeight();

    if (P.Count > 0)
    {
        const int   Across = Columns(P.Wide);
        const float Inner  = P.Wide - GridPadX * 2.0f;
        const float Column = (Inner - GridGap * float(Across - 1)) / float(Across);
        const float Tall   = PlacementHeight(1);
        for (int I = 0; I < P.Count && I < int(Solid::Count); ++I)
        {
            const int Row = I / Across, Slot = I % Across;
            const ImVec2 At { P.Spot.x + GridPadX + float(Slot) * (Column + GridGap),
                              P.Spot.y + GridPadY + float(Row) * (Tall + GridGap) };
            PaintPlacement(Draw, Light, At, Column, Tall, Solid(I), Of(Solid(I)).Name, I == P.Picked,
                           I == P.Hovered ? Pointer::Hover : Pointer::Idle);
        }
    }
    else
    {
        // .checker-empty — centred in the content box, which is the pane less the caption bar.
        const float Box = ImMax(P.Tall - Bar, EmptyMin);
        const float Stack = Kit::Grind(EmptySize) + EmptyGap + Kit::Grind(EmptySmallSz);
        float Y = P.Spot.y + (Box - Stack) * 0.5f;
        Kit::Inked(Draw, Light, P.Spot.x + P.Wide * 0.5f, Y + Kit::AscentShare * EmptySize, EmptySize,
                   EmptyInk, "No constructed entities", Kit::Anchor::Middle);
        Y += Kit::Grind(EmptySize) + EmptyGap;
        Kit::Inked(Draw, Light, P.Spot.x + P.Wide * 0.5f, Y + Kit::AscentShare * EmptySmallSz, EmptySmallSz,
                   EmptySmall, "Ctrl+A to construct", Kit::Anchor::Middle);
    }

    Draw->AddRectFilled({ P.Spot.x, P.Spot.y + P.Tall - Bar }, { P.Spot.x + P.Wide, P.Spot.y + P.Tall },
                        CaptionWash);
    Kit::Tracked(Draw, Light, P.Spot.x + CaptionPadX,
                 P.Spot.y + P.Tall - Bar + CaptionPadY, CaptionSize, CaptionInk, Caption, CaptionTrack);
}

}   // namespace Frontier::BaseMesh
