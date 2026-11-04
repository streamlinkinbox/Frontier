/*============================================================================================================================================
                                                           VECTORCOVERAGE.SLANG
============================================================================================================================================*/
// 🧩 GPU contour winding, primitive coverage, gradients, Boolean evaluation, masking and ordered transparent artwork.

struct VectorCoverageInstruction
{
    uint4 Topology;
    float4 Bounds;
    float4 Transform;
    float4 Colour;
    float4 ColourEnd;
    float4 StrokeColour;
    float4 Paint;
    // 🧩 Mirror Engine/Render/VectorCoverage.h's VectorCoverageInstruction exactly: up to 4 gradient
    // stops (Colour/ColourEnd/GradientStopColour2/GradientStopColour3), with GradientStopPositions
    // giving each slot's Fraction in strictly ascending order starting at slot 0; -1 marks a slot (and
    // everything after it) unused. See EvaluateGradientColour below.
    float4 GradientStopPositions;
    float4 GradientStopColour2;
    float4 GradientStopColour3;
    // 🧩 (AxisStartX, AxisStartY, AxisEndX, AxisEndY) in the shape's local, unrotated, bounds-normalized
    // frame - see the field comment on Engine/Render/VectorCoverage.h's VectorCoverageInstruction.
    float4 GradientAxis;
    float4 ShapeParameters;
    float4 StrokeColourEnd;
    float4 StrokeGradientStopPositions;
    float4 StrokeGradientStopColour2;
    float4 StrokeGradientStopColour3;
    float4 StrokeGradientAxis;
    float4 FreeformPointPositions01;
    float4 FreeformPointPositions23;
    float4 FreeformPointPositions45;
    float4 FreeformPointPositions67;
    float4 FreeformPointColour4;
    float4 FreeformPointColour5;
    float4 FreeformPointColour6;
    float4 FreeformPointColour7;
    float4 FreeformParameters;
};
struct VectorCubicSegment { float4 StartControl; float4 ControlEnd; };
// 📝 Mirror VectorCoverageInstructionCapacity/VectorCoverageExtensionCapacity in VectorCoverage.h exactly
// (this header cannot be included from Slang, so these two constants must stay hand-synchronized).
static const uint InstructionCapacity = 256;
struct TileDirectoryEntry { uint Offset; uint Count; };
struct CoverageProjection { uint Width; uint Height; uint Count; uint Root; uint TileSize; uint TilesPerRow; };
[[vk::push_constant]] ConstantBuffer<CoverageProjection> Projection;
[[vk::binding(0, 0)]] StructuredBuffer<VectorCoverageInstruction> Instructions;
[[vk::binding(1, 0)]] [[vk::image_format("rgba8")]] RWTexture2D<float4> Coverage;
[[vk::binding(2, 0)]] StructuredBuffer<VectorCubicSegment> Segments;
// 📝 CPU-binned per-tile leaf culling (Engine/Render/VectorCoverageTileBinning.h): every leaf instruction's
// padded footprint is computed on the CPU once per upload and bucketed into Width x Height's pixel-space
// tile grid, so a pixel only walks the (usually small) subset of leaves whose footprint can actually reach
// its own tile instead of every leaf in the whole document -- the O(pixels x shapes x segments) wall this
// engine's vector rasterizer used to hit before "hundreds of shapes." Boolean/mask instructions are cheap
// (reading two already-evaluated slots) and are therefore always evaluated unconditionally below; only the
// expensive analytic distance work for a leaf is gated on tile membership.
[[vk::binding(3, 0)]] StructuredBuffer<TileDirectoryEntry> TileDirectory;
[[vk::binding(4, 0)]] StructuredBuffer<uint> TileIndices;
bool VectorLeafVisible(uint Index, float2 Point)
{
    float2 PixelPosition = clamp(Point / 512.0 * float2(Projection.Width, Projection.Height),
        0.0, float2(Projection.Width, Projection.Height) - 1.0);
    uint TilesPerColumn = max((Projection.Height + Projection.TileSize - 1) / Projection.TileSize, 1);
    uint TileX = min(uint(PixelPosition.x) / Projection.TileSize, max(Projection.TilesPerRow, 1) - 1);
    uint TileY = min(uint(PixelPosition.y) / Projection.TileSize, TilesPerColumn - 1);
    TileDirectoryEntry Entry = TileDirectory[TileY * Projection.TilesPerRow + TileX];
    for (uint Scan = 0; Scan < Entry.Count; ++Scan)
        if (TileIndices[Entry.Offset + Scan] == Index) return true;
    return false;
}

float SegmentDistance(float2 Point, float2 Start, float2 End)
{
    float2 Direction = End - Start;
    return length(Point - Start - Direction * saturate(dot(Point - Start, Direction) / max(dot(Direction, Direction), 1e-20)));
}
float2 CubicPoint(VectorCubicSegment Segment, float Parameter)
{
    float2 First = lerp(Segment.StartControl.xy, Segment.StartControl.zw, Parameter);
    float2 Second = lerp(Segment.StartControl.zw, Segment.ControlEnd.xy, Parameter);
    float2 Third = lerp(Segment.ControlEnd.xy, Segment.ControlEnd.zw, Parameter);
    return lerp(lerp(First, Second, Parameter), lerp(Second, Third, Parameter), Parameter);
}
float CubicWinding(VectorCubicSegment Segment, float2 Point)
{
    // 📝 Partition at analytic Y extrema, then solve each monotonic crossing on the GPU.
    float4 Heights = float4(Segment.StartControl.y, Segment.StartControl.w, Segment.ControlEnd.y, Segment.ControlEnd.w);
    float A = -Heights.x + 3 * Heights.y - 3 * Heights.z + Heights.w;
    float B = 3 * Heights.x - 6 * Heights.y + 3 * Heights.z;
    float C = -3 * Heights.x + 3 * Heights.y;
    float Cuts[4]; Cuts[0] = 0; uint Count = 1;
    if (abs(A) > 1e-8)
    {
        float Discriminant = B * B - 3 * A * C;
        if (Discriminant > 0)
        {
            float First = (-B - sqrt(Discriminant)) / (3 * A);
            float Second = (-B + sqrt(Discriminant)) / (3 * A);
            if (First > Second) { float Exchange = First; First = Second; Second = Exchange; }
            if (First > 0 && First < 1) Cuts[Count++] = First;
            if (Second > 0 && Second < 1) Cuts[Count++] = Second;
        }
    }
    else if (abs(B) > 1e-8)
    {
        float Critical = -C / (2 * B);
        if (Critical > 0 && Critical < 1) Cuts[Count++] = Critical;
    }
    Cuts[Count++] = 1;
    float Winding = 0;
    for (uint Interval = 1; Interval < Count; ++Interval)
    {
        float Lower = Cuts[Interval - 1], Upper = Cuts[Interval];
        float StartY = CubicPoint(Segment, Lower).y, EndY = CubicPoint(Segment, Upper).y;
        bool Ascending = EndY > StartY;
        if (!(Point.y >= min(StartY, EndY) && Point.y < max(StartY, EndY))) continue;
        for (uint Iteration = 0; Iteration < 24; ++Iteration)
        {
            float Centre = (Lower + Upper) * .5;
            if ((CubicPoint(Segment, Centre).y < Point.y) == Ascending) Lower = Centre; else Upper = Centre;
        }
        if (CubicPoint(Segment, (Lower + Upper) * .5).x > Point.x) Winding += Ascending ? 1 : -1;
    }
    return Winding;
}
float ShapeDistance(VectorCoverageInstruction Instruction, float2 Local)
{
    uint Category = Instruction.Topology.x;
    float2 HalfExtent = Instruction.Bounds.zw * .5;
    if (Category == 0)
    {
        float Radius = min(Instruction.ShapeParameters.x, min(HalfExtent.x, HalfExtent.y));
        float2 Outside = abs(Local) - HalfExtent + Radius;
        return length(max(Outside, 0)) + min(max(Outside.x, Outside.y), 0) - Radius;
    }
    if (Category == 1) return (length(Local / HalfExtent) - 1) * min(HalfExtent.x, HalfExtent.y);
    if (Category == 6)
    {
        // 📝 Unpainted construction contours evaluate no fill/stroke response.
        if (Instruction.Paint.w == 0 && Instruction.Transform.y <= 0) return 1e10;
        float Antialias = 512.0 / min(Projection.Width, Projection.Height);
        // 📝 Closed+filled contours read their independent outline width from Paint.x (mirror
        // QueryVectorCoverageStrokeWidth in VectorCoverage.h); open lines read Transform.y. The knee
        // must cover the wider band or cage rejects would cap Distance inside the outline's reach.
        float StrokeWidth = Instruction.Paint.w != 0 ? Instruction.Paint.x : Instruction.Transform.y;
        float Knee = max(StrokeWidth, 0) * .5 + Antialias * .5;
        float Winding = 0, Distance = 1e10;
        for (uint Curve = 0; Curve < Instruction.Topology.z; ++Curve)
        {
            VectorCubicSegment Segment = Segments[Instruction.Topology.y + Curve];
            Segment.StartControl *= float4(Instruction.Bounds.zw, Instruction.Bounds.zw);
            Segment.ControlEnd *= float4(Instruction.Bounds.zw, Instruction.Bounds.zw);
            // 📝 Control-cage rejects: rows outside the convex-hull cage cross nothing (exact zero
            // winding), and cages beyond the capped best minimum cannot move the saturated fill/stroke
            // response. Both skips are output-identical; they only elide provably dead curve work.
            float2 CageMinimum = min(min(Segment.StartControl.xy, Segment.StartControl.zw),
                                     min(Segment.ControlEnd.xy, Segment.ControlEnd.zw));
            float2 CageMaximum = max(max(Segment.StartControl.xy, Segment.StartControl.zw),
                                     max(Segment.ControlEnd.xy, Segment.ControlEnd.zw));
            if (Local.y >= CageMinimum.y && Local.y < CageMaximum.y)
                Winding += CubicWinding(Segment, Local);
            float2 Outside = max(max(CageMinimum - Local, Local - CageMaximum), 0);
            if (length(Outside) > min(Distance, Knee) + 1e-3) continue;
            // 📝 Wang's formula (mirror EstimateCubicFlattenSegments in VectorCoverage.h exactly): small/flat
            // ornamental curves collapse to as few as 2 chords instead of the former fixed 32, while sharp or
            // large curves still subdivide enough to stay within a quarter-pixel of the analytic curve. The
            // winding above stays exact regardless (it solves the cubic analytically, never flattens), so
            // this only ever changes antialiasing/stroke-edge smoothness, never fill/hole correctness.
            float2 SecondDifferenceA = Segment.StartControl.xy - 2 * Segment.StartControl.zw + Segment.ControlEnd.xy;
            float2 SecondDifferenceB = Segment.StartControl.zw - 2 * Segment.ControlEnd.xy + Segment.ControlEnd.zw;
            float ControlDeviation = max(length(SecondDifferenceA), length(SecondDifferenceB));
            float FlattenTolerance = max(Antialias * .25, 1e-4);
            uint SegmentCount = uint(clamp(ceil(sqrt(ControlDeviation * 3.0 / (4.0 * FlattenTolerance))), 2.0, 32.0));
            float2 Start = Segment.StartControl.xy;
            for (uint Interval = 1; Interval <= SegmentCount; ++Interval)
            {
                float2 End = CubicPoint(Segment, Interval / float(SegmentCount));
                Distance = min(Distance, SegmentDistance(Local, Start, End)); Start = End;
            }
        }
        bool Interior = (Instruction.Topology.w & 1) != 0 ? (uint(abs(Winding)) % 2 != 0) : abs(Winding) > .5;
        return Interior ? -Distance : Distance;
    }
    uint Count = Category == 2 ? 4 : Category == 3 ? 3 : uint(Instruction.Paint.z);
    if (Category == 5) Count *= 2;
    float Distance = 1e10; bool Inside = false;
    float2 Previous = 0;
    for (uint Corner = 0; Corner <= Count; ++Corner)
    {
        float Angle = -1.57079632679 + 6.28318530718 * (Corner % Count) / Count;
        float Radius = Category == 5 && Corner % 2 == 1 ? Instruction.Paint.y : 1;
        float2 Current = float2(cos(Angle), sin(Angle)) * HalfExtent * Radius;
        if (Corner > 0)
        {
            Distance = min(Distance, SegmentDistance(Local, Previous, Current));
            if ((Previous.y > Local.y) != (Current.y > Local.y) && Local.x < (Current.x - Previous.x) * (Local.y - Previous.y) / (Current.y - Previous.y) + Previous.x) Inside = !Inside;
        }
        Previous = Current;
    }
    return Inside ? -Distance : Distance;
}
// 📝 Piecewise-lerps across whichever pair of active stops bracket Fraction. Mirrors the packed-slot
// contract documented on VectorCoverageInstruction/GradientStopPositions in Engine/Render/VectorCoverage.h
// exactly (ascending positions, contiguous from slot 0, -1 sentinel ends the active run) — a plain 2-stop
// gradient (GradientStopPositions = {0,1,-1,-1}) collapses straight back to the original single lerp.
float4 InterpolateSplineSegment(float4 C0, float4 C1, float4 C2, float4 C3, float Local, uint Mode)
{
    if (Mode == 4) return Local < 0.5 ? C1 : C2;
    if (Mode == 1) return lerp(C1, C2, Local * Local * (3.0 - 2.0 * Local));
    if (Mode == 2)
    {
        float Local2 = Local * Local, Local3 = Local2 * Local;
        return clamp(0.5 * ((2.0 * C1) + (-C0 + C2) * Local + (2.0 * C0 - 5.0 * C1 + 4.0 * C2 - C3) * Local2 +
            (-C0 + 3.0 * C1 - 3.0 * C2 + C3) * Local3), 0.0, 1.0);
    }
    if (Mode == 3)
    {
        float Inv = 1.0 - Local, Local2 = Local * Local, Local3 = Local2 * Local;
        return clamp(((Inv * Inv * Inv) * C0 + (3.0 * Local3 - 6.0 * Local2 + 4.0) * C1 +
            (-3.0 * Local3 + 3.0 * Local2 + 3.0 * Local + 1.0) * C2 + Local3 * C3) / 6.0, 0.0, 1.0);
    }
    return lerp(C1, C2, Local);
}
float4 EvaluateGradientColour(VectorCoverageInstruction Instruction, float Fraction, uint Mode)
{
    float4 Colours[4] = {Instruction.Colour, Instruction.ColourEnd, Instruction.GradientStopColour2, Instruction.GradientStopColour3};
    float Positions[4] = {Instruction.GradientStopPositions.x, Instruction.GradientStopPositions.y, Instruction.GradientStopPositions.z, Instruction.GradientStopPositions.w};
    uint Count = Positions[1] < 0 ? 1 : Positions[2] < 0 ? 2 : Positions[3] < 0 ? 3 : 4;
    if (Count <= 1) return Colours[0];
    if (Fraction <= Positions[0])
        return Mode == 3 ? InterpolateSplineSegment(Colours[0], Colours[0], Colours[1], Colours[min(2u, Count - 1)], 0.0, 3) : Colours[0];
    if (Fraction >= Positions[Count - 1])
    {
        uint Last = Count - 2;
        return Mode == 3 ? InterpolateSplineSegment(Colours[Last > 0 ? Last - 1 : 0], Colours[Last], Colours[Last + 1], Colours[Last + 1], 1.0, 3) : Colours[Count - 1];
    }
    for (uint Stop = 0; Stop < Count - 1; ++Stop)
    {
        if (Fraction > Positions[Stop + 1]) continue;
        float Span = max(Positions[Stop + 1] - Positions[Stop], 1e-5);
        float Local = saturate((Fraction - Positions[Stop]) / Span);
        float4 C0 = Colours[Stop > 0 ? Stop - 1 : 0];
        float4 C1 = Colours[Stop];
        float4 C2 = Colours[Stop + 1];
        float4 C3 = Colours[Stop + 2 < Count ? Stop + 2 : Count - 1];
        return InterpolateSplineSegment(C0, C1, C2, C3, Local, Mode);
    }
    return Colours[Count - 1];
}
float4 EvaluateFreeformGradient2D(VectorCoverageInstruction Instruction, float2 LocalNorm, uint Mode)
{
    uint PointCount = clamp(uint(Instruction.FreeformParameters.x + 0.5), 2u, 8u);
    float Sharpness = clamp(Instruction.FreeformParameters.y, 0.25, 8.0);
    float2 Points[8] = {
        Instruction.FreeformPointPositions01.xy, Instruction.FreeformPointPositions01.zw,
        Instruction.FreeformPointPositions23.xy, Instruction.FreeformPointPositions23.zw,
        Instruction.FreeformPointPositions45.xy, Instruction.FreeformPointPositions45.zw,
        Instruction.FreeformPointPositions67.xy, Instruction.FreeformPointPositions67.zw};
    float4 Colours[8] = {
        Instruction.Colour, Instruction.ColourEnd, Instruction.GradientStopColour2, Instruction.GradientStopColour3,
        Instruction.FreeformPointColour4, Instruction.FreeformPointColour5, Instruction.FreeformPointColour6, Instruction.FreeformPointColour7};
    if (Mode == 4)
    {
        uint Nearest = 0;
        float BestDistance = 1e20;
        for (uint Index = 0; Index < PointCount; ++Index)
        {
            float Distance = length(LocalNorm - Points[Index]);
            if (Distance < BestDistance) { BestDistance = Distance; Nearest = Index; }
        }
        return Colours[Nearest];
    }
    float Weights[8];
    float WeightSum = 0.0;
    for (uint Index = 0; Index < PointCount; ++Index)
    {
        float Distance = length(LocalNorm - Points[Index]);
        if (Mode != 3 && Distance < 1e-4) return Colours[Index];
        float Weight = 0.0;
        if (Mode == 3)
        {
            float Regularized = sqrt(Distance * Distance + 0.04 * 0.04);
            Weight = 1.0 / pow(Regularized, max(Sharpness * 0.85, 0.35));
        }
        else if (Mode == 0)
            Weight = 1.0 / pow(max(Distance, 1e-4), max(Sharpness * 0.65, 0.35));
        else if (Mode == 2)
            Weight = 1.0 / pow(max(Distance, 1e-4), max(Sharpness * 1.25, 0.5));
        else
            Weight = 1.0 / pow(max(Distance, 1e-4), Sharpness);
        Weights[Index] = Weight;
        WeightSum += Weight;
    }
    if (WeightSum <= 1e-12) return Colours[0];
    float4 Accum = 0.0;
    float ShapedSum = 0.0;
    for (uint Index = 0; Index < PointCount; ++Index)
    {
        float Normalized = Weights[Index] / WeightSum;
        float Shaped = Normalized;
        if (Mode == 1) Shaped = Normalized * Normalized * (3.0 - 2.0 * Normalized);
        else if (Mode == 2) Shaped = clamp(Normalized * (0.35 + Normalized * (1.95 - 1.3 * Normalized)), 0.0, 1.0);
        else if (Mode == 3)
        {
            float Inv = 1.0 - Normalized;
            Shaped = ((1.0 - Inv * Inv * Inv) + Normalized * Normalized * (3.0 - 2.0 * Normalized)) * 0.5;
        }
        Accum += Colours[Index] * Shaped;
        ShapedSum += Shaped;
    }
    return ShapedSum > 1e-12 ? clamp(Accum / ShapedSum, 0.0, 1.0) : Colours[0];
}
void EvaluateVectorPoint(float2 Point, float Antialias, out float4 Evaluations[InstructionCapacity])
{
    for (uint Index = 0; Index < Projection.Count; ++Index)
    {
        VectorCoverageInstruction Instruction = Instructions[Index];
        uint Category = Instruction.Topology.x;
        float4 Result = 0;
        if (Category <= 6 && VectorLeafVisible(Index, Point))
        {
            float2 Translation = Point - Instruction.Bounds.xy;
            float Sine = sin(Instruction.Transform.x), Cosine = cos(Instruction.Transform.x);
            float2 Local = float2(Cosine * Translation.x + Sine * Translation.y, -Sine * Translation.x + Cosine * Translation.y);
            float Distance = ShapeDistance(Instruction, Local);
            float Fill = Instruction.Paint.w != 0 ? saturate(.5 - Distance / Antialias) : 0;
            // 📝 Closed+filled cubic contours (category 6) read their independent outline width from
            // Paint.x; everything else reads Transform.y (mirror QueryVectorCoverageStrokeWidth).
            float StrokeWidth = (Category == 6 && Instruction.Paint.w != 0) ? Instruction.Paint.x : Instruction.Transform.y;
            float Stroke = StrokeWidth > 0 ? saturate(.5 - (abs(Distance) - StrokeWidth * .5) / Antialias) : 0;
            // 📝 GradientAxis.xy/zw are the ramp's own two control points (see the struct field comment
            // above) in this same LocalNorm frame - linear projects LocalNorm onto that A->B segment;
            // radial reads xy as the centre and zw as a point on the circle's edge (so its distance from
            // the centre doubles as the radius). Replaces the old fixed-angle, bounds-cornered formula.
            float2 LocalNorm = Local / Instruction.Bounds.zw;
            uint FillMode = uint(clamp(Instruction.ShapeParameters.z + 0.5, 0.0, 4.0));
            float4 FillColour = Instruction.Colour;
            if (Instruction.Transform.w == 3)
                FillColour = EvaluateFreeformGradient2D(Instruction, LocalNorm, FillMode);
            else if (Instruction.Transform.w != 0)
            {
                float2 AxisStart = Instruction.GradientAxis.xy, AxisEnd = Instruction.GradientAxis.zw;
                float2 AxisVector = AxisEnd - AxisStart;
                float Fraction = Instruction.Transform.w == 1 ? saturate(dot(LocalNorm - AxisStart, AxisVector) / max(dot(AxisVector, AxisVector), 1e-6))
                    : saturate(length(LocalNorm - AxisStart) / max(length(AxisVector), 1e-4));
                FillColour = EvaluateGradientColour(Instruction, Fraction, FillMode);
            }
            float4 StrokeColour = (Instruction.Topology.w & 0x80000000) != 0 ? Instruction.StrokeColour : Instruction.Colour;
            if (Instruction.ShapeParameters.y != 0)
            {
                float2 StrokeStart = Instruction.StrokeGradientAxis.xy;
                float2 StrokeVector = Instruction.StrokeGradientAxis.zw - StrokeStart;
                float StrokeFraction = Instruction.ShapeParameters.y == 1
                    ? saturate(dot(LocalNorm - StrokeStart, StrokeVector) / max(dot(StrokeVector, StrokeVector), 1e-6))
                    : saturate(length(LocalNorm - StrokeStart) / max(length(StrokeVector), 1e-4));
                VectorCoverageInstruction StrokePaint = Instruction;
                StrokePaint.Colour = StrokeColour;
                StrokePaint.ColourEnd = Instruction.StrokeColourEnd;
                StrokePaint.GradientStopPositions = Instruction.StrokeGradientStopPositions;
                StrokePaint.GradientStopColour2 = Instruction.StrokeGradientStopColour2;
                StrokePaint.GradientStopColour3 = Instruction.StrokeGradientStopColour3;
                uint StrokeMode = uint(clamp(Instruction.ShapeParameters.w + 0.5, 0.0, 4.0));
                StrokeColour = EvaluateGradientColour(StrokePaint, StrokeFraction, StrokeMode);
            }
            float FillAlpha = Fill * FillColour.a * Instruction.Transform.z;
            float StrokeAlpha = Stroke * StrokeColour.a * Instruction.Transform.z;
            Result.a = StrokeAlpha + FillAlpha * (1 - StrokeAlpha);
            Result.rgb = Result.a > 0
                ? (StrokeColour.rgb * StrokeAlpha + FillColour.rgb * FillAlpha * (1 - StrokeAlpha)) / Result.a
                : 0;
        }
        else if (Category > 6)
        {
            float4 Lower = Evaluations[Instruction.Topology.y], Upper = Evaluations[Instruction.Topology.z];
            Result = Lower;
            if (Category == 16) Result.a = max(Lower.a, Upper.a);
            if (Category == 17) Result.a = min(Lower.a, 1 - Upper.a);
            if (Category == 18) Result.a = min(Lower.a, Upper.a);
            if (Category == 19) Result.a = max(min(Lower.a, 1 - Upper.a), min(Upper.a, 1 - Lower.a));
            if (Category == 24 || Category == 25)
            {
                float Mask = Upper.a * (Category == 25 ? dot(Upper.rgb, float3(.2126, .7152, .0722)) : 1);
                if (Instruction.Topology.w != 0) Mask = 1 - Mask;
                Result.a = Lower.a * Mask * Instruction.Transform.z;
            }
            if (Category == 29) Result.a = Lower.a * Instruction.Transform.z;
            if (Category == 28)
            {
                Result.a = Upper.a + Lower.a * (1 - Upper.a);
                Result.rgb = Result.a > 0 ? (Upper.rgb * Upper.a + Lower.rgb * Lower.a * (1 - Upper.a)) / Result.a : 0;
            }
        }
        Evaluations[Index] = Result;
    }
}
[shader("compute")]
[numthreads(8, 8, 1)]
void VectorCoverageCompute(uint3 Pixel : SV_DispatchThreadID)
{
    if (Pixel.x >= Projection.Width || Pixel.y >= Projection.Height) return;
    float2 Point = (float2(Pixel.xy) + .5) / float2(Projection.Width, Projection.Height) * 512;
    float4 Evaluations[InstructionCapacity];
    EvaluateVectorPoint(Point, 512.0 / min(Projection.Width, Projection.Height), Evaluations);
    Coverage[Pixel.xy] = Evaluations[Projection.Root];
}
[shader("compute")]
[numthreads(8, 8, 1)]
void VectorSelectionCompute(uint3 Pixel : SV_DispatchThreadID)
{
    if (Pixel.x != 0 || Pixel.y != 0) return;
    float4 Evaluations[InstructionCapacity];
    float2 Point = Instructions[InstructionCapacity].Bounds.xy;
    if (any(Point < 0) || any(Point > 512))
    {
        Coverage[uint2(0, 0)] = 0; return;
    }
    EvaluateVectorPoint(Point, 1, Evaluations);
    uint Token = 0;
    for (uint Index = 0; Index < Projection.Root; ++Index)
    {
        uint4 Candidate = Instructions[InstructionCapacity + Index].Topology;
        // 📝 Match nonzero coverage at the authored tile's antialias footprint; keep holes and masks on the GPU.
        if (Evaluations[Candidate.x].a >= (1.0 / 255.0)) Token = Candidate.y;
    }
    Coverage[uint2(0, 0)] = float4(Token & 255, (Token >> 8) & 255, (Token >> 16) & 255, Token >> 24) / 255.0;
}
