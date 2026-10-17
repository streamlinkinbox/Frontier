//============================================================================================================================================
//                                                          PATHTRACETEMPORAL.WGSL
//============================================================================================================================================
// 🧩 ReSTIR PT, first pass: one path per pixel streamed into a reservoir, then temporal reuse of the previous frame's
//    path through the hybrid shift with pairwise MIS.

// 📝 Per trace pixel: the primary vertex comes from the guide and position textures the direct pass wrote this frame;
//    TracePath fills a fresh reservoir; the prior pixel (motion-reprojected, validated against the prior guide) lends
//    its path, carried with its occupant if it moved: the prior path is shifted here (forward, with visibility), the
//    new path is shifted to the prior surface (reverse, for its MIS weight), and the two are combined with the
//    pairwise weights; M is clamped to the path tally limit.

//! include "DirectReservoir.wgsl"
//! include "PathReservoirs.wgsl"

@group(0) @binding(1) var CurrentGuide                            : texture_2d<f32>;
@group(0) @binding(2) var CurrentPositions                        : texture_2d<f32>;
@group(0) @binding(3) var SurfaceMotion                           : texture_2d<f32>;
@group(0) @binding(4) var PreviousGuide                           : texture_2d<f32>;
@group(0) @binding(5) var PreviousPositions                       : texture_2d<f32>;
@group(0) @binding(6) var<storage, read> PreviousReservoirs       : array<PathRecord>;
@group(0) @binding(7) var<storage, read_write> CurrentReservoirs  : array<PathRecord>;

@compute @workgroup_size(8, 8)
fn ResamplePathTemporal(@builtin(global_invocation_id) Invocation: vec3u)
{
    let TracePixel = Invocation.xy;
    let Extent = vec2u(Frame.TraceExtent);
    if (any(TracePixel >= Extent))
    {
        return;
    }
    let Index = TracePixel.y * Extent.x + TracePixel.x;
    let Primary = DecodePrimarySurface(textureLoad(CurrentGuide, vec2i(TracePixel), 0), textureLoad(CurrentPositions, vec2i(TracePixel), 0));
    if (!Primary.Valid || !FeatureEnabled(Frame, FeatureIndirect))
    {
        CurrentReservoirs[Index] = PackPath(ConstructEmptyPathReservoir());
        return;
    }
    let Vertex = ConstructPrimaryVertex(Primary.Position, Primary.Normal, Primary.Family);
    let Seed = ConstructPixelSeed(TracePixel, Frame.FrameIndex, 41u);
    var Stream = ConstructPixelSeed(TracePixel, Frame.FrameIndex, 43u);

    var Reservoir = ConstructEmptyPathReservoir();
    TracePath(Vertex, Seed, &Reservoir, &Stream);

    if (FeatureEnabled(Frame, FeatureTemporalReuse) && !FeatureEnabled(Frame, FeatureTemporalRestart))
    {
        let ScreenPixel = ResolveScreenPixel(TracePixel);
        let Motion = textureLoad(SurfaceMotion, ScreenPixel, 0).xy;
        let PriorScreen = vec2f(ScreenPixel) + 0.5 - Motion;
        let PriorTrace = vec2i(floor((PriorScreen - 0.5) / f32(Frame.TraceScale) + 0.5));
        if (all(PriorTrace >= vec2i(0)) && all(PriorTrace < vec2i(Extent)))
        {
            let Prior = DecodePrimarySurface(textureLoad(PreviousGuide, PriorTrace, 0), textureLoad(PreviousPositions, PriorTrace, 0));
            if (Prior.Valid && dot(Prior.Normal, Primary.Normal) > 0.85 && ClassifyPriorDepth(Prior.ViewDepth, Primary.ExpectedDepth, Primary.ViewDepth))
            {
                var PriorReservoir = UnpackPath(PreviousReservoirs[u32(PriorTrace.y) * Extent.x + u32(PriorTrace.x)]);
                PriorReservoir.Tally = min(PriorReservoir.Tally, Trace.PathTallyLimit);
                if (PriorReservoir.Tally > 0.0)
                {
                    let PriorSample = CarryPathSample(PriorReservoir.Sample);
                    let PriorVertex = ConstructPrimaryVertex(Prior.Position, Prior.Normal, Prior.Family);
                    var Forward: ShiftResult;
                    Forward.Valid = false;
                    Forward.Target = 0.0;
                    if (PriorReservoir.Weight > 0.0)
                    {
                        Forward = EvaluateShift(PriorSample, Vertex, true);
                    }
                    var ReverseTarget = 0.0;
                    if (Reservoir.Weight > 0.0 && Reservoir.TargetPdf > 0.0)
                    {
                        ReverseTarget = EvaluateShift(Reservoir.Sample, PriorVertex, TraceFlag(TraceFlagMisVisibility)).Target;
                    }
                    let TallyCurrent = Reservoir.Tally;
                    let TallyPrior = PriorReservoir.Tally;
                    let CurrentHere = Reservoir.TargetPdf;
                    let PriorHere = Forward.Target;
                    var MisPrior = 0.0;
                    var MisCurrent = 1.0;
                    if (TraceFlag(TraceFlagPairwise))
                    {
                        MisPrior = ResolvePairwiseNeighbour(TallyPrior, PriorReservoir.TargetPdf, TallyCurrent, PriorHere, 1.0);
                        MisCurrent = ResolvePairwiseCanonical(TallyPrior, ReverseTarget, TallyCurrent, CurrentHere, 1.0);
                    }
                    else
                    {
                        MisPrior = TallyPrior / max(TallyPrior + TallyCurrent, 1.0);
                        MisCurrent = TallyCurrent / max(TallyPrior + TallyCurrent, 1.0);
                    }
                    var Shifted = PriorSample;
                    Shifted.Rest = Forward.Rest;
                    Shifted.Omega = Forward.Omega;
                    var Combined = ConstructEmptyPathReservoir();
                    StreamPath(&Combined, Reservoir.Sample, MisCurrent * CurrentHere * Reservoir.Weight, CurrentHere, AdvanceUniform(&Stream));
                    StreamPath(&Combined, Shifted, MisPrior * PriorHere * PriorReservoir.Weight, PriorHere, AdvanceUniform(&Stream));
                    Combined.Tally = TallyCurrent + TallyPrior;
                    FinalisePath(&Combined);
                    Reservoir = Combined;
                }
            }
        }
    }
    CurrentReservoirs[Index] = PackPath(Reservoir);
}
