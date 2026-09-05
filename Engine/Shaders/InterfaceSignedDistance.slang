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

    return 1.0e9;
}

#endif // FRONTIER_INTERFACE_SIGNED_DISTANCE
