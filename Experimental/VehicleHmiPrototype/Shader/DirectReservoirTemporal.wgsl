//============================================================================================================================================
//                                                       DIRECTRESERVOIRTEMPORAL.WGSL
//============================================================================================================================================
// 🧩 ReSTIR DI, first pass: the primary surface's guide, the light candidates (and a BSDF-sampled one) resampled by
//    importance, the chosen one's visibility, then temporal reuse against the previous frame with pairwise MIS.

// 📝 Per trace pixel:
//    ① the primary surface (x, n, family) from the surface projection → this frame's guide and position textures;
//    ② N light candidates from the mixture and one BSDF-sampled candidate are streamed into a reservoir with the
//       balance-heuristic candidate weights w = p̂ / (N_l·p_l + N_b·p_b), every pdf in the candidate's own measure;
//    ③ the survivor's shadow ray; an occluded survivor keeps its tally but loses its weight (visibility reuse);
//    ④ the prior frame's reservoir at the motion-reprojected pixel, validated against the prior guide, joins by
//       pairwise MIS: both samples are evaluated at both surfaces, so a light that only one pixel can see is not
//       over-counted; M is clamped to the direct tally limit.

//! include "DirectReservoir.wgsl"

@group(0) @binding(1) var SurfaceAttributes                       : texture_2d<f32>;
@group(0) @binding(2) var SurfaceDepth                            : texture_depth_2d;
@group(0) @binding(3) var SurfaceMotion                           : texture_2d<f32>;
@group(0) @binding(4) var PreviousGuide                           : texture_2d<f32>;
@group(0) @binding(5) var PreviousPositions                       : texture_2d<f32>;
@group(0) @binding(6) var GuideTarget                             : texture_storage_2d<rgba16float, write>;
@group(0) @binding(7) var PositionTarget                          : texture_storage_2d<rgba32float, write>;
@group(0) @binding(8) var<storage, read> PreviousReservoirs       : array<DirectRecord>;
@group(0) @binding(9) var<storage, read_write> CurrentReservoirs  : array<DirectRecord>;

@compute @workgroup_size(8, 8)
fn ResampleDirectTemporal(@builtin(global_invocation_id) Invocation: vec3u)
{
    let TracePixel = Invocation.xy;
    let Extent = vec2u(Frame.TraceExtent);
    if (any(TracePixel >= Extent))
    {
        return;
    }
    let Index = TracePixel.y * Extent.x + TracePixel.x;
    let ScreenPixel = ResolveScreenPixel(TracePixel);
    let Depth = textureLoad(SurfaceDepth, ScreenPixel, 0);
    if (Depth <= 0.0)
    {
        textureStore(GuideTarget, vec2i(TracePixel), vec4f(0.0));
        textureStore(PositionTarget, vec2i(TracePixel), vec4f(0.0));
        CurrentReservoirs[Index] = PackDirect(ConstructEmptyDirect());
        return;
    }

    // ① primary surface
    let Attributes = textureLoad(SurfaceAttributes, ScreenPixel, 0);
    let Position = ReconstructWorldPosition(vec2f(ScreenPixel) + 0.5, Depth);
    let Normal = DecodeOctahedral(Attributes.xy);
    let Family = u32(Attributes.z + 0.5);
    let ViewDepth = ResolveViewDepth(Position);
    let ExpectedDepth = ResolvePriorViewDepth(Position);
    textureStore(GuideTarget, vec2i(TracePixel), vec4f(EncodeOctahedral(Normal), ViewDepth, 1.0 + ExpectedDepth));
    textureStore(PositionTarget, vec2i(TracePixel), vec4f(Position, f32(Family)));
    let Surface = ProjectSurfaceSample(FetchMaterial(Family));
    let View = normalize(Frame.CameraPosition - Position);
    let AllowSpecular = TraceFlag(TraceFlagReflection);
    var Seed = ConstructPixelSeed(TracePixel, Frame.FrameIndex, 31u);

    // ② candidates
    let LightCount = Trace.DirectCandidates;
    let BsdfCount = select(0u, 1u, TraceFlag(TraceFlagBsdfCandidate));
    var Reservoir = ConstructEmptyDirect();
    for (var Candidate = 0u; Candidate < LightCount; Candidate++)
    {
        let Random = vec3f(AdvanceUniform(&Seed), AdvanceUniform(&Seed), AdvanceUniform(&Seed));
        let Extra = AdvanceUniform(&Seed);
        let Sample = SampleLight(Position, Normal, Random, Extra);
        let Target = select(0.0, ResolveDirectTarget(Sample, Surface, Normal, View), Sample.Valid);
        var Weight = 0.0;
        if (Target > 0.0)
        {
            var BsdfPdf = 0.0;
            if (!Sample.Delta && BsdfCount > 0u)
            {
                BsdfPdf = PdfReflectance(Surface, Normal, View, Sample.Direction, AllowSpecular) * Sample.Geometry;
            }
            Weight = Target / (f32(LightCount) * Sample.Pdf + f32(BsdfCount) * BsdfPdf);
        }
        var Proposal = ConstructEmptyDirect();
        AssignDirectSample(&Proposal, Sample);
        StreamDirect(&Reservoir, Proposal, Weight, 1.0, Target, AdvanceUniform(&Seed));
    }
    if (BsdfCount > 0u)
    {
        let Random = vec3f(AdvanceUniform(&Seed), AdvanceUniform(&Seed), AdvanceUniform(&Seed));
        let Lobe = SampleReflectance(Surface, Normal, View, Random, AllowSpecular);
        var Weight = 0.0;
        var Target = 0.0;
        var Proposal = ConstructEmptyDirect();
        if (Lobe.Pdf > 0.0)
        {
            let Hit = TraceClosest(OffsetRayOrigin(Position, Normal), Lobe.Direction, DirectionalReach);
            var Sample: LightSample;
            var LightPdf = 0.0;
            var BsdfPdf = Lobe.Pdf;
            var Found = false;
            if (!Hit.Hit)
            {
                Sample = ConstructDirectionalSample(Lobe.Direction, Position, Normal, select(LightKindSky, LightKindSun, InsideSunDisc(Lobe.Direction)));
                LightPdf = Sample.Pdf;
                Found = Sample.Valid;
            }
            else if (ResolveLuminance(FetchMaterial(Hit.Family).Emission) > 0.0)
            {
                Sample = ConstructTrianglePoint(Hit.Triangle, Hit.Occupant, Hit.Barycentric, Position);
                LightPdf = ResolveHitLightPdf(Hit, Normal, Lobe.Direction);
                BsdfPdf = Lobe.Pdf * ResolveHitGeometry(Hit, Lobe.Direction);
                Found = Sample.Valid;
            }
            if (Found)
            {
                Target = ResolveDirectTarget(Sample, Surface, Normal, View);
                let Denominator = f32(LightCount) * LightPdf + f32(BsdfCount) * BsdfPdf;
                Weight = select(0.0, Target / Denominator, Target > 0.0 && Denominator > 0.0);
                AssignDirectSample(&Proposal, Sample);
            }
        }
        StreamDirect(&Reservoir, Proposal, Weight, 1.0, Target, AdvanceUniform(&Seed));
    }
    FinaliseDirect(&Reservoir);

    // ③ visibility of the survivor
    if (Reservoir.Weight > 0.0)
    {
        let Chosen = ReconstructDirectSample(Reservoir, Position, Normal);
        if (!Chosen.Valid || !ResolveLightVisible(Chosen, Position, Normal))
        {
            Reservoir.Weight = 0.0;
        }
    }

    // ④ temporal reuse
    if (FeatureEnabled(Frame, FeatureTemporalReuse) && !FeatureEnabled(Frame, FeatureTemporalRestart))
    {
        let Motion = textureLoad(SurfaceMotion, ScreenPixel, 0).xy;
        let PriorScreen = vec2f(ScreenPixel) + 0.5 - Motion;
        let PriorTrace = vec2i(floor((PriorScreen - 0.5) / f32(Frame.TraceScale) + 0.5));
        if (all(PriorTrace >= vec2i(0)) && all(PriorTrace < vec2i(Extent)))
        {
            let Prior = DecodePrimarySurface(textureLoad(PreviousGuide, PriorTrace, 0), textureLoad(PreviousPositions, PriorTrace, 0));
            if (Prior.Valid && dot(Prior.Normal, Normal) > 0.85 && ClassifyPriorDepth(Prior.ViewDepth, ExpectedDepth, ViewDepth))
            {
                var PriorReservoir = UnpackDirect(PreviousReservoirs[u32(PriorTrace.y) * Extent.x + u32(PriorTrace.x)]);
                PriorReservoir.Tally = min(PriorReservoir.Tally, Trace.DirectTallyLimit);
                if (PriorReservoir.Tally > 0.0)
                {
                    let PriorSurface = ProjectSurfaceSample(FetchMaterial(Prior.Family));
                    let PriorView = normalize(Frame.CameraPosition - Prior.Position);
                    // the prior sample at this surface, this sample at the prior surface
                    let PriorHere = ResolveDirectTarget(ReconstructDirectSample(PriorReservoir, Position, Normal), Surface, Normal, View);
                    let CurrentThere = select(0.0, ResolveDirectTarget(ReconstructDirectSample(Reservoir, Prior.Position, Prior.Normal),
                                                                        PriorSurface, Prior.Normal, PriorView), Reservoir.TargetPdf > 0.0);
                    let CurrentHere = Reservoir.TargetPdf;
                    let PriorThere = PriorReservoir.TargetPdf;
                    let TallyCurrent = Reservoir.Tally;
                    let TallyPrior = PriorReservoir.Tally;
                    var MisPrior = 0.0;
                    var MisCurrent = 1.0;
                    if (TraceFlag(TraceFlagPairwise))
                    {
                        MisPrior = ResolvePairwiseNeighbour(TallyPrior, PriorThere, TallyCurrent, PriorHere, 1.0);
                        MisCurrent = ResolvePairwiseCanonical(TallyPrior, CurrentThere, TallyCurrent, CurrentHere, 1.0);
                    }
                    else
                    {
                        MisPrior = TallyPrior / max(TallyPrior + TallyCurrent, 1.0);
                        MisCurrent = TallyCurrent / max(TallyPrior + TallyCurrent, 1.0);
                    }
                    var Combined = ConstructEmptyDirect();
                    StreamDirect(&Combined, Reservoir, MisCurrent * CurrentHere * Reservoir.Weight, TallyCurrent, CurrentHere, AdvanceUniform(&Seed));
                    StreamDirect(&Combined, PriorReservoir, MisPrior * PriorHere * PriorReservoir.Weight, TallyPrior, PriorHere, AdvanceUniform(&Seed));
                    FinaliseDirect(&Combined);
                    Reservoir = Combined;
                }
            }
        }
    }
    CurrentReservoirs[Index] = PackDirect(Reservoir);
}
