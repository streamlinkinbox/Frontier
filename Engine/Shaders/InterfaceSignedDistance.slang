//============================================================================================================================================
//                                                  INTERFACESIGNEDDISTANCE.SLANG
//============================================================================================================================================
// 🧩 Every shape the spatial interface can draw, as a signed distance in the figure's own local plane (metres,
//    +X right, +Y up, origin at the centre). Include-only; also compiled as C++ by Scratchpad/InterfaceRasterTest.cpp
//    (FRONTIER_CPU_PORT) so the raster proof runs on exactly this text — the R4b discipline.
//
// Why distances rather than geometry: a dial with 60 ticks, a redline band and a needle is ~30 ALU ops on a quad,
//    against thousands of triangles rebuilt whenever a value moves. It is also resolution-independent — the shapes
//    stay sharp at any distance, tilt or display density, which a rasterised vector image cannot do.
//
// Convention: distance < 0 inside, > 0 outside, in local metres. Coverage is taken from the distance with a
//    screen-space derivative so the edge is one pixel wide however the plane is transformed.

#ifndef FRONTIER_INTERFACE_SIGNED_DISTANCE
#define FRONTIER_INTERFACE_SIGNED_DISTANCE

//------------------------------------------------------------------------------------------------------------------------
//                                                     CATEGORIES
//------------------------------------------------------------------------------------------------------------------------
// Mirrors InterfaceCategory in Engine/SpatialInterface/InterfaceSpecification.h.

const uint kCategorySurface     = 0u;
const uint kCategoryArc         = 1u;
const uint kCategoryTickRing    = 2u;
const uint kCategoryNeedle      = 3u;
const uint kCategorySegmentCell = 4u;
const uint kCategoryLamp        = 5u;
const uint kCategoryGlyph       = 6u;   // P1: one stroke-font character; ScalarAlpha = ASCII code

const float kInterfacePi    = 3.14159265358979;
const float kInterfaceTau   = 6.28318530717959;
const float kInterfaceEpsilon = 1.0e-6;

//------------------------------------------------------------------------------------------------------------------------
//                                                  ROUNDED RECTANGLE
//------------------------------------------------------------------------------------------------------------------------
// The workhorse: cards, buttons, bar troughs, toggle beds, and the rounded-corner mask that replaces stencil (⑥).

float DistanceRoundedRectangle(vec2 Point, vec2 HalfExtent, float Radius)
{
    vec2 Inner = vec2(max(HalfExtent.x - Radius, 0.0), max(HalfExtent.y - Radius, 0.0));
    vec2 Delta = vec2(abs(Point.x) - Inner.x, abs(Point.y) - Inner.y);
    vec2 Outer = vec2(max(Delta.x, 0.0), max(Delta.y, 0.0));
    return length(Outer) + min(max(Delta.x, Delta.y), 0.0) - Radius;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        ARC
//------------------------------------------------------------------------------------------------------------------------
// Annular sector from Start, sweeping Sweep radians counter-clockwise, of the given Radius and Thickness. Fill is the
//    fraction of the sweep actually drawn (0 = empty meter, 1 = full) — that single scalar animates the whole meter.
//
// Angles are measured from +X counter-clockwise, so a dial that opens downward uses Start ≈ -220° and Sweep ≈ 260°.

float DistanceArc(vec2 Point, float Start, float Sweep, float Fill, float Radius, float Thickness)
{
    float Extent = Sweep * clamp(Fill, 0.0, 1.0);
    float Radial = abs(length(Point) - Radius) - Thickness * 0.5;

    if (abs(Extent) < kInterfaceEpsilon) return 1.0e9;

    // Angle of the sample relative to the arc start, wrapped into [0, τ).
    float Angle   = atan(Point.y, Point.x);
    float Offset  = Angle - Start;
    Offset        = Offset - kInterfaceTau * floor(Offset / kInterfaceTau);

    float Span    = abs(Extent);
    if (Extent < 0.0) Offset = kInterfaceTau - Offset;

    if (Offset <= Span) return Radial;   // inside the angular span: the annulus distance is the answer

    // Beyond the span: distance to the nearer end cap, so the arc terminates in a clean round end.
    float Nearest = (Offset - Span) < (kInterfaceTau - Offset) ? Span : 0.0;
    float CapAngle = Start + (Extent < 0.0 ? -Nearest : Nearest);
    vec2  Cap      = vec2(cos(CapAngle) * Radius, sin(CapAngle) * Radius);
    return length(Point - Cap) - Thickness * 0.5;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      TICK RING
//------------------------------------------------------------------------------------------------------------------------
// Count radial marks spread across Sweep radians from Start, each Thickness wide and Length long, drawn inward from
//    Radius. Repetition happens in angle space — no per-mark geometry exists anywhere.

float DistanceTickRing(vec2 Point, float Start, float Sweep, float Count, float Radius, float Thickness, float Length)
{
    if (Count < 1.0) return 1.0e9;

    float Distance = length(Point);
    if (Distance < kInterfaceEpsilon) return 1.0e9;

    float Angle  = atan(Point.y, Point.x);
    float Offset = Angle - Start;
    Offset       = Offset - kInterfaceTau * floor(Offset / kInterfaceTau);

    float Span = abs(Sweep);
    if (Offset > Span + kInterfaceEpsilon) return 1.0e9;   // outside the marked sector

    // Snap to the nearest mark, then measure in that mark's own frame.
    float Step    = Span / max(Count - 1.0, 1.0);
    float Index   = floor(Offset / Step + 0.5);
    float Snapped = Start + clamp(Index, 0.0, Count - 1.0) * Step * (Sweep < 0.0 ? -1.0 : 1.0);

    vec2  Axis    = vec2(cos(Snapped), sin(Snapped));
    vec2  Normal  = vec2(-Axis.y, Axis.x);
    float Along   = Point.x * Axis.x   + Point.y * Axis.y;
    float Across  = Point.x * Normal.x + Point.y * Normal.y;

    float Radially = abs(Along - (Radius - Length * 0.5)) - Length * 0.5;
    float Laterally = abs(Across) - Thickness * 0.5;

    vec2 Outer = vec2(max(Radially, 0.0), max(Laterally, 0.0));
    return length(Outer) + min(max(Radially, Laterally), 0.0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        NEEDLE
//------------------------------------------------------------------------------------------------------------------------
// A tapered pointer: Root wide at the hub, Waist wide at the tip, Length long, pointing at Angle. This is the figure
//    a spring drives — the whole animation is one float, and nothing is re-tessellated when it moves.

float DistanceNeedle(vec2 Point, float Angle, float Length, float Root, float Waist)
{
    vec2  Axis   = vec2(cos(Angle), sin(Angle));
    vec2  Normal = vec2(-Axis.y, Axis.x);

    float Along  = Point.x * Axis.x   + Point.y * Axis.y;
    float Across = Point.x * Normal.x + Point.y * Normal.y;

    float Travel = clamp(Along / max(Length, kInterfaceEpsilon), 0.0, 1.0);
    float Width  = mix(Root, Waist, Travel) * 0.5;

    float Beyond = max(Along - Length, -Along);   // past the tip, or behind the hub
    float Lateral = abs(Across) - Width;

    vec2 Outer = vec2(max(Beyond, 0.0), max(Lateral, 0.0));
    return length(Outer) + min(max(Beyond, Lateral), 0.0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SEVEN-SEGMENT CELL
//------------------------------------------------------------------------------------------------------------------------
// One digit, drawn from the seven classic bars. P0 uses this instead of a glyph sheet so no typography work leaks
//    into the spike; P1 replaces readouts that need real text with distance-field glyphs.
//
// Bar order: 0 top, 1 upper-right, 2 lower-right, 3 bottom, 4 lower-left, 5 upper-left, 6 middle.

float SegmentBar(vec2 Point, vec2 Centre, vec2 HalfExtent, float Thickness)
{
    vec2 Local = vec2(Point.x - Centre.x, Point.y - Centre.y);
    return DistanceRoundedRectangle(Local, HalfExtent, Thickness * 0.5);
}

float DistanceSegmentDigit(vec2 Point, uint Digit, vec2 Cell, float Thickness)
{
    // Bit per bar, indexed by digit. 10 = blank, 11 = minus.
    uint Lit = 0u;
    if      (Digit == 0u)  Lit = 0x3Fu;   // 0111111
    else if (Digit == 1u)  Lit = 0x06u;
    else if (Digit == 2u)  Lit = 0x5Bu;
    else if (Digit == 3u)  Lit = 0x4Fu;
    else if (Digit == 4u)  Lit = 0x66u;
    else if (Digit == 5u)  Lit = 0x6Du;
    else if (Digit == 6u)  Lit = 0x7Du;
    else if (Digit == 7u)  Lit = 0x07u;
    else if (Digit == 8u)  Lit = 0x7Fu;
    else if (Digit == 9u)  Lit = 0x6Fu;
    else if (Digit == 11u) Lit = 0x40u;   // minus = middle bar only
    else                   Lit = 0x00u;   // blank

    if (Lit == 0u) return 1.0e9;

    float HalfW = Cell.x;
    float HalfH = Cell.y;
    float Inset = Thickness * 0.75;                 // keeps adjacent bars from merging at the corners
    float ArmH  = max(HalfH * 0.5 - Inset, Thickness);
    float ArmW  = max(HalfW - Inset, Thickness);

    float Best = 1.0e9;

    if ((Lit & 0x01u) != 0u) Best = min(Best, SegmentBar(Point, vec2(0.0,  HalfH),          vec2(ArmW, Thickness * 0.5), Thickness));
    if ((Lit & 0x02u) != 0u) Best = min(Best, SegmentBar(Point, vec2( HalfW,  HalfH * 0.5), vec2(Thickness * 0.5, ArmH), Thickness));
    if ((Lit & 0x04u) != 0u) Best = min(Best, SegmentBar(Point, vec2( HalfW, -HalfH * 0.5), vec2(Thickness * 0.5, ArmH), Thickness));
    if ((Lit & 0x08u) != 0u) Best = min(Best, SegmentBar(Point, vec2(0.0, -HalfH),          vec2(ArmW, Thickness * 0.5), Thickness));
    if ((Lit & 0x10u) != 0u) Best = min(Best, SegmentBar(Point, vec2(-HalfW, -HalfH * 0.5), vec2(Thickness * 0.5, ArmH), Thickness));
    if ((Lit & 0x20u) != 0u) Best = min(Best, SegmentBar(Point, vec2(-HalfW,  HalfH * 0.5), vec2(Thickness * 0.5, ArmH), Thickness));
    if ((Lit & 0x40u) != 0u) Best = min(Best, SegmentBar(Point, vec2(0.0, 0.0),             vec2(ArmW, Thickness * 0.5), Thickness));

    return Best;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         LAMP
//------------------------------------------------------------------------------------------------------------------------

float DistanceLamp(vec2 Point, float Radius)
{
    return length(Point) - Radius;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    STROKE GLYPH
//------------------------------------------------------------------------------------------------------------------------
// P1 — real text, as an analytic stroke font rather than a sampled distance sheet.
//
// Why strokes and not a baked glyph sheet: this exchange binds no texture at all, and adding one would mean a
//    sampler binding, an offline bake, and a texture lifetime to manage — for a panel that draws labels and units,
//    not paragraphs. A stroke glyph is evaluated exactly like Needle and Arc already are, so it needs no new
//    binding, no upload, and stays resolution-independent at any camera distance or tilt. The trade is real and
//    worth stating: this suits labels, units and readouts. It is not a body-text engine and cannot render a font
//    you supply.
//
// Each glyph is a short list of strokes in a 0..1 em box, walked as capsule distances. The box is the standard
//    typographic cell: x right, y up, origin bottom-left, cap height 1.0.

// Distance to a capsule (a thick line segment) from A to B. The workhorse: every glyph is a few of these.
float StrokeSegment(vec2 Point, vec2 A, vec2 B, float HalfThickness)
{
    vec2  Span  = B - A;
    vec2  Local = Point - A;
    float Along = clamp(dot(Local, Span) / max(dot(Span, Span), kInterfaceEpsilon), 0.0, 1.0);
    return length(Local - Span * Along) - HalfThickness;
}

// Distance to a circular arc centred at C, swept from Start through Sweep radians at Radius.
float StrokeArc(vec2 Point, vec2 C, float Radius, float Start, float Sweep, float HalfThickness)
{
    vec2  Local = Point - C;
    float Angle = atan(Local.y, Local.x);

    // Fold the angle into [Start, Start + Sweep]; outside it the nearest point is an endcap.
    float Delta = Angle - Start;
    Delta -= 6.28318530718 * floor(Delta / 6.28318530718);      // → [0, 2π)
    if (Delta <= abs(Sweep) || Sweep >= 6.28318530718)
        return abs(length(Local) - Radius) - HalfThickness;

    vec2 CapA = C + vec2(cos(Start), sin(Start)) * Radius;
    vec2 CapB = C + vec2(cos(Start + Sweep), sin(Start + Sweep)) * Radius;
    return min(length(Point - CapA), length(Point - CapB)) - HalfThickness;
}

// One glyph, in the unit em box. Code is ASCII. Returns a large value for anything unmapped, so an unsupported
//    character renders as nothing rather than as a wrong glyph — a missing label is obvious, a wrong one is not.
float DistanceStrokeGlyph(vec2 P, uint Code, float HalfThickness)
{
    const float T = HalfThickness;
    float D = 1.0e9;

    // Digits as strokes, in the same em box as the letters.
    //
    //    An earlier version forwarded these to DistanceSegmentDigit, reasoning that a readout and a label should
    //    agree on shape. That was wrong and the proof sheet showed it: that function takes a half extent in the
    //    SAME units as its thickness, so a 0.06 stroke inside a 0.5 cell collapsed every digit into a stack of
    //    merged dashes. Seven-segment bars are sized for a large dedicated cell; a text digit needs to sit on the
    //    same baseline and weight as the letters beside it, which is a different shape problem.
    if (Code >= 48u && Code <= 57u)
    {
        uint Digit = Code - 48u;
        // Shared geometry: a rounded lozenge, with the digit-specific strokes layered on.
        const float B = 0.06, Tp = 0.94;   // baseline and cap height inside the em box
        if (Digit == 0u) {
            D = min(D, StrokeArc(P, vec2(0.5,0.72), 0.30, 0.0, 3.14159, T));
            D = min(D, StrokeArc(P, vec2(0.5,0.28), 0.30, 3.14159, 3.14159, T));
            D = min(D, StrokeSegment(P, vec2(0.20,0.28), vec2(0.20,0.72), T));
            D = min(D, StrokeSegment(P, vec2(0.80,0.28), vec2(0.80,0.72), T)); }
        else if (Digit == 1u) {
            D = min(D, StrokeSegment(P, vec2(0.50,B), vec2(0.50,Tp), T));
            D = min(D, StrokeSegment(P, vec2(0.30,0.78), vec2(0.50,Tp), T));
            D = min(D, StrokeSegment(P, vec2(0.28,B), vec2(0.72,B), T)); }
        else if (Digit == 2u) {
            D = min(D, StrokeArc(P, vec2(0.5,0.70), 0.30, 0.0, 3.4, T));
            D = min(D, StrokeSegment(P, vec2(0.78,0.62), vec2(0.20,B), T));
            D = min(D, StrokeSegment(P, vec2(0.20,B), vec2(0.82,B), T)); }
        else if (Digit == 3u) {
            D = min(D, StrokeArc(P, vec2(0.5,0.72), 0.26, -1.2, 3.9, T));
            D = min(D, StrokeArc(P, vec2(0.5,0.28), 0.26, -1.9, 3.9, T)); }
        else if (Digit == 4u) {
            D = min(D, StrokeSegment(P, vec2(0.68,B), vec2(0.68,Tp), T));
            D = min(D, StrokeSegment(P, vec2(0.68,Tp), vec2(0.14,0.34), T));
            D = min(D, StrokeSegment(P, vec2(0.14,0.34), vec2(0.90,0.34), T)); }
        else if (Digit == 5u) {
            D = min(D, StrokeSegment(P, vec2(0.80,Tp), vec2(0.24,Tp), T));
            D = min(D, StrokeSegment(P, vec2(0.24,Tp), vec2(0.22,0.56), T));
            D = min(D, StrokeArc(P, vec2(0.5,0.32), 0.29, -1.9, 3.6, T)); }
        else if (Digit == 6u) {
            D = min(D, StrokeArc(P, vec2(0.5,0.30), 0.30, 0.0, 6.28319, T));
            D = min(D, StrokeArc(P, vec2(0.62,0.66), 0.36, 1.7, 1.4, T)); }
        else if (Digit == 7u) {
            D = min(D, StrokeSegment(P, vec2(0.16,Tp), vec2(0.86,Tp), T));
            D = min(D, StrokeSegment(P, vec2(0.86,Tp), vec2(0.38,B), T)); }
        else if (Digit == 8u) {
            D = min(D, StrokeArc(P, vec2(0.5,0.71), 0.25, 0.0, 6.28319, T));
            D = min(D, StrokeArc(P, vec2(0.5,0.28), 0.29, 0.0, 6.28319, T)); }
        else {
            D = min(D, StrokeArc(P, vec2(0.5,0.70), 0.30, 0.0, 6.28319, T));
            D = min(D, StrokeArc(P, vec2(0.38,0.34), 0.36, 4.6, 1.4, T)); }
        return D;
    }

    if (Code == 32u) return 1.0e9;                                            // space

    // Uppercase A–Z. Lowercase folds to uppercase: a stroke font with one case reads cleanly on a cluster and
    //    halves the glyph table, which matters because every branch below costs shader instructions.
    uint C = Code;
    if (C >= 97u && C <= 122u) C -= 32u;

    if (C == 65u) {                                                           // A
        D = min(D, StrokeSegment(P, vec2(0.05,0.0), vec2(0.5,1.0), T));
        D = min(D, StrokeSegment(P, vec2(0.95,0.0), vec2(0.5,1.0), T));
        D = min(D, StrokeSegment(P, vec2(0.22,0.38), vec2(0.78,0.38), T)); }
    else if (C == 66u) {                                                      // B
        D = min(D, StrokeSegment(P, vec2(0.15,0.0), vec2(0.15,1.0), T));
        D = min(D, StrokeSegment(P, vec2(0.15,1.0), vec2(0.55,1.0), T));
        D = min(D, StrokeSegment(P, vec2(0.15,0.5), vec2(0.55,0.5), T));
        D = min(D, StrokeSegment(P, vec2(0.15,0.0), vec2(0.55,0.0), T));
        D = min(D, StrokeArc(P, vec2(0.55,0.75), 0.25, -1.5708, 3.14159, T));
        D = min(D, StrokeArc(P, vec2(0.55,0.25), 0.25, -1.5708, 3.14159, T)); }
    else if (C == 67u) {                                                      // C
        D = min(D, StrokeArc(P, vec2(0.5,0.5), 0.45, 0.7854, 4.7124, T)); }
    else if (C == 68u) {                                                      // D
        D = min(D, StrokeSegment(P, vec2(0.15,0.0), vec2(0.15,1.0), T));
        D = min(D, StrokeSegment(P, vec2(0.15,1.0), vec2(0.45,1.0), T));
        D = min(D, StrokeSegment(P, vec2(0.15,0.0), vec2(0.45,0.0), T));
        D = min(D, StrokeArc(P, vec2(0.45,0.5), 0.5, -1.5708, 3.14159, T)); }
    else if (C == 69u) {                                                      // E
        D = min(D, StrokeSegment(P, vec2(0.18,0.0), vec2(0.18,1.0), T));
        D = min(D, StrokeSegment(P, vec2(0.18,1.0), vec2(0.82,1.0), T));
        D = min(D, StrokeSegment(P, vec2(0.18,0.5), vec2(0.70,0.5), T));
        D = min(D, StrokeSegment(P, vec2(0.18,0.0), vec2(0.82,0.0), T)); }
    else if (C == 70u) {                                                      // F
        D = min(D, StrokeSegment(P, vec2(0.18,0.0), vec2(0.18,1.0), T));
        D = min(D, StrokeSegment(P, vec2(0.18,1.0), vec2(0.82,1.0), T));
        D = min(D, StrokeSegment(P, vec2(0.18,0.5), vec2(0.70,0.5), T)); }
    else if (C == 71u) {                                                      // G
        D = min(D, StrokeArc(P, vec2(0.5,0.5), 0.45, 0.7854, 4.7124, T));
        D = min(D, StrokeSegment(P, vec2(0.95,0.5), vec2(0.60,0.5), T));
        D = min(D, StrokeSegment(P, vec2(0.95,0.5), vec2(0.95,0.18), T)); }
    else if (C == 72u) {                                                      // H
        D = min(D, StrokeSegment(P, vec2(0.15,0.0), vec2(0.15,1.0), T));
        D = min(D, StrokeSegment(P, vec2(0.85,0.0), vec2(0.85,1.0), T));
        D = min(D, StrokeSegment(P, vec2(0.15,0.5), vec2(0.85,0.5), T)); }
    else if (C == 73u) {                                                      // I
        D = min(D, StrokeSegment(P, vec2(0.5,0.0), vec2(0.5,1.0), T)); }
    else if (C == 74u) {                                                      // J
        D = min(D, StrokeSegment(P, vec2(0.75,1.0), vec2(0.75,0.28), T));
        D = min(D, StrokeArc(P, vec2(0.45,0.28), 0.30, 3.14159, 1.5708, T)); }
    else if (C == 75u) {                                                      // K
        D = min(D, StrokeSegment(P, vec2(0.18,0.0), vec2(0.18,1.0), T));
        D = min(D, StrokeSegment(P, vec2(0.18,0.42), vec2(0.82,1.0), T));
        D = min(D, StrokeSegment(P, vec2(0.18,0.42), vec2(0.82,0.0), T)); }
    else if (C == 76u) {                                                      // L
        D = min(D, StrokeSegment(P, vec2(0.20,0.0), vec2(0.20,1.0), T));
        D = min(D, StrokeSegment(P, vec2(0.20,0.0), vec2(0.80,0.0), T)); }
    else if (C == 77u) {                                                      // M
        D = min(D, StrokeSegment(P, vec2(0.10,0.0), vec2(0.10,1.0), T));
        D = min(D, StrokeSegment(P, vec2(0.90,0.0), vec2(0.90,1.0), T));
        D = min(D, StrokeSegment(P, vec2(0.10,1.0), vec2(0.50,0.42), T));
        D = min(D, StrokeSegment(P, vec2(0.90,1.0), vec2(0.50,0.42), T)); }
    else if (C == 78u) {                                                      // N
        D = min(D, StrokeSegment(P, vec2(0.15,0.0), vec2(0.15,1.0), T));
        D = min(D, StrokeSegment(P, vec2(0.85,0.0), vec2(0.85,1.0), T));
        D = min(D, StrokeSegment(P, vec2(0.15,1.0), vec2(0.85,0.0), T)); }
    else if (C == 79u) {                                                      // O
        D = min(D, StrokeArc(P, vec2(0.5,0.5), 0.45, 0.0, 6.28319, T)); }
    else if (C == 80u) {                                                      // P
        D = min(D, StrokeSegment(P, vec2(0.18,0.0), vec2(0.18,1.0), T));
        D = min(D, StrokeSegment(P, vec2(0.18,1.0), vec2(0.55,1.0), T));
        D = min(D, StrokeSegment(P, vec2(0.18,0.5), vec2(0.55,0.5), T));
        D = min(D, StrokeArc(P, vec2(0.55,0.75), 0.25, -1.5708, 3.14159, T)); }
    else if (C == 82u) {                                                      // R
        D = min(D, StrokeSegment(P, vec2(0.18,0.0), vec2(0.18,1.0), T));
        D = min(D, StrokeSegment(P, vec2(0.18,1.0), vec2(0.55,1.0), T));
        D = min(D, StrokeSegment(P, vec2(0.18,0.5), vec2(0.55,0.5), T));
        D = min(D, StrokeArc(P, vec2(0.55,0.75), 0.25, -1.5708, 3.14159, T));
        D = min(D, StrokeSegment(P, vec2(0.50,0.5), vec2(0.85,0.0), T)); }
    else if (C == 83u) {                                                      // S
        // Upper bowl opens right, lower bowl opens left, joined by a diagonal. Two mirrored arcs alone left a
        //    gap at the waist -- visible on the proof sheet as a broken glyph -- so the joint is explicit.
        D = min(D, StrokeArc(P, vec2(0.5,0.74), 0.28, 0.0, 3.6, T));
        D = min(D, StrokeArc(P, vec2(0.5,0.28), 0.28, 3.14159, 3.6, T));
        D = min(D, StrokeSegment(P, vec2(0.28,0.62), vec2(0.72,0.40), T)); }
    else if (C == 84u) {                                                      // T
        D = min(D, StrokeSegment(P, vec2(0.5,0.0), vec2(0.5,1.0), T));
        D = min(D, StrokeSegment(P, vec2(0.10,1.0), vec2(0.90,1.0), T)); }
    else if (C == 85u) {                                                      // U
        D = min(D, StrokeSegment(P, vec2(0.15,1.0), vec2(0.15,0.30), T));
        D = min(D, StrokeSegment(P, vec2(0.85,1.0), vec2(0.85,0.30), T));
        D = min(D, StrokeArc(P, vec2(0.5,0.30), 0.35, 3.14159, 3.14159, T)); }
    else if (C == 86u) {                                                      // V
        D = min(D, StrokeSegment(P, vec2(0.10,1.0), vec2(0.5,0.0), T));
        D = min(D, StrokeSegment(P, vec2(0.90,1.0), vec2(0.5,0.0), T)); }
    else if (C == 87u) {                                                      // W
        D = min(D, StrokeSegment(P, vec2(0.05,1.0), vec2(0.28,0.0), T));
        D = min(D, StrokeSegment(P, vec2(0.28,0.0), vec2(0.5,0.62), T));
        D = min(D, StrokeSegment(P, vec2(0.72,0.0), vec2(0.5,0.62), T));
        D = min(D, StrokeSegment(P, vec2(0.95,1.0), vec2(0.72,0.0), T)); }
    else if (C == 88u) {                                                      // X
        D = min(D, StrokeSegment(P, vec2(0.12,0.0), vec2(0.88,1.0), T));
        D = min(D, StrokeSegment(P, vec2(0.88,0.0), vec2(0.12,1.0), T)); }
    else if (C == 89u) {                                                      // Y
        D = min(D, StrokeSegment(P, vec2(0.12,1.0), vec2(0.5,0.5), T));
        D = min(D, StrokeSegment(P, vec2(0.88,1.0), vec2(0.5,0.5), T));
        D = min(D, StrokeSegment(P, vec2(0.5,0.5), vec2(0.5,0.0), T)); }
    else if (C == 90u) {                                                      // Z
        D = min(D, StrokeSegment(P, vec2(0.12,1.0), vec2(0.88,1.0), T));
        D = min(D, StrokeSegment(P, vec2(0.88,1.0), vec2(0.12,0.0), T));
        D = min(D, StrokeSegment(P, vec2(0.12,0.0), vec2(0.88,0.0), T)); }
    else if (C == 81u) {                                                      // Q
        D = min(D, StrokeArc(P, vec2(0.5,0.5), 0.45, 0.0, 6.28319, T));
        // The tail must CROSS the ring to read as a Q; ending inside it just looks like a blemished O.
        D = min(D, StrokeSegment(P, vec2(0.58,0.26), vec2(0.94,0.02), T)); }
    // Punctuation and units — the characters a cluster actually needs beside a number.
    else if (C == 46u) {                                                      // .
        D = min(D, StrokeSegment(P, vec2(0.5,0.04), vec2(0.5,0.04), T * 1.4)); }
    else if (C == 44u) {                                                      // ,
        D = min(D, StrokeSegment(P, vec2(0.52,0.08), vec2(0.40,-0.10), T)); }
    else if (C == 58u) {                                                      // :
        D = min(D, StrokeSegment(P, vec2(0.5,0.68), vec2(0.5,0.68), T * 1.4));
        D = min(D, StrokeSegment(P, vec2(0.5,0.24), vec2(0.5,0.24), T * 1.4)); }
    else if (C == 45u) {                                                      // -
        D = min(D, StrokeSegment(P, vec2(0.20,0.5), vec2(0.80,0.5), T)); }
    else if (C == 43u) {                                                      // +
        D = min(D, StrokeSegment(P, vec2(0.20,0.5), vec2(0.80,0.5), T));
        D = min(D, StrokeSegment(P, vec2(0.5,0.20), vec2(0.5,0.80), T)); }
    else if (C == 47u) {                                                      // /
        D = min(D, StrokeSegment(P, vec2(0.15,0.0), vec2(0.85,1.0), T)); }
    else if (C == 37u) {                                                      // %
        D = min(D, StrokeSegment(P, vec2(0.15,0.0), vec2(0.85,1.0), T));
        D = min(D, StrokeArc(P, vec2(0.26,0.80), 0.16, 0.0, 6.28319, T * 0.8));
        D = min(D, StrokeArc(P, vec2(0.74,0.20), 0.16, 0.0, 6.28319, T * 0.8)); }
    else if (C == 176u) {                                                     // ° degree
        D = min(D, StrokeArc(P, vec2(0.5,0.82), 0.16, 0.0, 6.28319, T * 0.8)); }

    return D;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       COVERAGE
//------------------------------------------------------------------------------------------------------------------------
// Distance → alpha. PixelWidth is one screen pixel expressed in local metres (the fragment stage takes it from the
//    derivative of the local coordinate), so the edge stays exactly one pixel wide at any distance or tilt. This is
//    the property a pre-rasterised vector image loses the moment the camera moves.

float CoverageFromDistance(float Distance, float PixelWidth)
{
    float Width = max(PixelWidth, kInterfaceEpsilon);
    return clamp(0.5 - Distance / Width, 0.0, 1.0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     CLIP MASK  (⑥)
//------------------------------------------------------------------------------------------------------------------------
// The shader-clip tier: a rounded rectangle in figure-local space, evaluated in the same pass as everything else.
//    P0 writes it wide open; P2 animates it for masked wipes and scroll clipping under arbitrary transforms, where a
//    hardware scissor cannot reach.

float ClipCoverage(vec2 Point, vec4 ClipExtent, float ClipRadius, float PixelWidth)
{
    vec2 Centre     = vec2((ClipExtent.x + ClipExtent.z) * 0.5, (ClipExtent.y + ClipExtent.w) * 0.5);
    vec2 HalfExtent = vec2((ClipExtent.z - ClipExtent.x) * 0.5, (ClipExtent.w - ClipExtent.y) * 0.5);

    if (HalfExtent.x <= 0.0 || HalfExtent.y <= 0.0) return 0.0;

    float Distance = DistanceRoundedRectangle(vec2(Point.x - Centre.x, Point.y - Centre.y), HalfExtent, ClipRadius);
    return CoverageFromDistance(Distance, PixelWidth);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   FIGURE DISTANCE
//------------------------------------------------------------------------------------------------------------------------
// One switch over the category, reading the two scalars the instance slot carries. Adding a shape is a branch here —
//    never a new draw, never a new state object.
//
//    Surface      ScalarAlpha unused                    ScalarBeta unused
//    Arc          fill fraction 0..1                    thickness [m]
//    TickRing     unused                                mark count
//    Needle       angle fraction 0..1 across the sweep  hub radius [m]
//    SegmentCell  digit 0..9 (10 blank, 11 minus)       bar thickness [m]
//    Lamp         luminance 0..1                        unused
//
// ArcStart / ArcSweep are shared constants rather than per-figure fields so the 96-byte slot stays fixed; a dial
//    that needs a different opening is composed from a rotated figure, which costs nothing.

const float kArcStart = -3.6651914;   // [rad] -210° — the classic instrument opening, measured from +X
const float kArcSweep =  4.1887902;   // [rad]  240°

float DistanceFigure(uint Category, vec2 Point, vec2 HalfExtent, float CornerRadius, float ScalarAlpha, float ScalarBeta)
{
    if (Category == kCategorySurface)
    {
        return DistanceRoundedRectangle(Point, HalfExtent, CornerRadius);
    }
    if (Category == kCategoryArc)
    {
        float Radius = min(HalfExtent.x, HalfExtent.y) - ScalarBeta * 0.5;
        return DistanceArc(Point, kArcStart, kArcSweep, ScalarAlpha, max(Radius, kInterfaceEpsilon), ScalarBeta);
    }
    if (Category == kCategoryTickRing)
    {
        float Radius = min(HalfExtent.x, HalfExtent.y);
        float Length = Radius * 0.18;
        float Width  = Radius * 0.028;
        return DistanceTickRing(Point, kArcStart, kArcSweep, ScalarBeta, Radius, Width, Length);
    }
    if (Category == kCategoryNeedle)
    {
        float Length = min(HalfExtent.x, HalfExtent.y);
        float Angle  = kArcStart + kArcSweep * clamp(ScalarAlpha, 0.0, 1.0);
        float Pointer = DistanceNeedle(Point, Angle, Length, Length * 0.075, Length * 0.018);
        float Hub     = DistanceLamp(Point, max(ScalarBeta, Length * 0.06));
        return min(Pointer, Hub);
    }
    if (Category == kCategorySegmentCell)
    {
        uint Digit = uint(clamp(ScalarAlpha, 0.0, 11.0));
        return DistanceSegmentDigit(Point, Digit, HalfExtent, max(ScalarBeta, kInterfaceEpsilon));
    }
    if (Category == kCategoryLamp)
    {
        return DistanceLamp(Point, min(HalfExtent.x, HalfExtent.y));
    }
    if (Category == kCategoryGlyph)
    {
        // Point arrives centred on the figure; the glyph table is authored in a 0..1 em box with the origin at
        //    bottom-left, so map into that box before evaluating and scale the result back to local metres.
        vec2  Box   = max(HalfExtent, vec2(kInterfaceEpsilon));
        vec2  Unit  = vec2(Point.x / (2.0 * Box.x) + 0.5, Point.y / (2.0 * Box.y) + 0.5);
        float Scale = min(2.0 * Box.x, 2.0 * Box.y);
        float Half  = max(ScalarBeta, kInterfaceEpsilon) / max(Scale, kInterfaceEpsilon);
        return DistanceStrokeGlyph(Unit, uint(clamp(ScalarAlpha, 0.0, 255.0)), Half) * Scale;
    }

    return 1.0e9;
}

#endif // FRONTIER_INTERFACE_SIGNED_DISTANCE
