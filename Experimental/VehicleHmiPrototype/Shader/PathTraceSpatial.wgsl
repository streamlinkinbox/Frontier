//============================================================================================================================================
//                                                           PATHTRACESPATIAL.WGSL
//============================================================================================================================================
// 🧩 ReSTIR PT, second pass: spatial reuse of neighbouring paths through the hybrid shift with pairwise MIS, then the
//    resolve into demodulated indirect irradiance and specular radiance (joined by the direct specular).

// 📝 The pixel's temporal reservoir is canonical; up to PathSpatialCount similar neighbours in a disk lend their paths.
//    Each neighbour path is shifted here (forward, with visibility) and the canonical path shifted to the neighbour
//    (reverse, for the MIS weights); the survivor's stored Rest and first-bounce direction at this pixel resolve the
//    output without another ray. The reservoir written is the next frame's temporal input.

//! include "DirectReservoir.wgsl"
//! include "PathReservoirs.wgsl"

@group(0) @binding(1) var CurrentGuide                            : texture_2d<f32>;
@group(0) @binding(2) var CurrentPositions                        : texture_2d<f32>;
@group(0) @binding(3) var<storage, read> TemporalReservoirs       : array<PathRecord>;
@group(0) @binding(4) var<storage, read_write> FinalReservoirs    : array<PathRecord>;
@group(0) @binding(5) var DirectSpecular                          : texture_2d<f32>;
@group(0) @binding(6) var IndirectTarget                          : texture_storage_2d<rgba16float, write>;
@group(0) @binding(7) var SpecularTarget                          : texture_storage_2d<rgba16float, write>;

const MaxPathNeighbours : u32 = 4u;

@compute @workgroup_size(8, 8)
fn ResamplePathSpatial(@builtin(global_invocation_id) Invocation: vec3u)
{
    let TracePixel = Invocation.xy;
    let Extent = vec2u(Frame.TraceExtent);
    if (any(TracePixel >= Extent))
    {
        return;
    }
    let Index = TracePixel.y * Extent.x + TracePixel.x;
    let Primary = DecodePrimarySurface(textureLoad(CurrentGuide, vec2i(TracePixel), 0), textureLoad(CurrentPositions, vec2i(TracePixel), 0));
    let Specular0 = textureLoad(DirectSpecular, vec2i(TracePixel), 0).rgb;
    if (!Primary.Valid || !FeatureEnabled(Frame, FeatureIndirect))
    {
        FinalReservoirs[Index] = PackPath(ConstructEmptyPathReservoir());
        textureStore(IndirectTarget, vec2i(TracePixel), vec4f(0.0));
        textureStore(SpecularTarget, vec2i(TracePixel), vec4f(Specular0, 1.0));
        return;
    }
    let Vertex = ConstructPrimaryVertex(Primary.Position, Primary.Normal, Primary.Family);
    var Seed = ConstructPixelSeed(TracePixel, Frame.FrameIndex, 47u);

    var Canonical = UnpackPath(TemporalReservoirs[Index]);
    var Result = Canonical;
    if (FeatureEnabled(Frame, FeatureSpatialReuse) && Trace.PathSpatialCount > 0u)
    {
        var Neighbours: array<u32, MaxPathNeighbours>;
        var Count = 0u;
        var TallySum = Canonical.Tally;
        let Attempts = min(Trace.PathSpatialCount, MaxPathNeighbours);
        for (var Attempt = 0u; Attempt < Attempts; Attempt++)
        {
            let Offset = ProjectUniformDisk(vec2f(AdvanceUniform(&Seed), AdvanceUniform(&Seed))) * Trace.PathSpatialRadius;
            let Texel = vec2i(TracePixel) + vec2i(round(Offset));
            if (any(Texel < vec2i(0)) || any(Texel >= vec2i(Extent)) || all(Texel == vec2i(TracePixel)))
            {
                continue;
            }
            let Other = DecodePrimarySurface(textureLoad(CurrentGuide, Texel, 0), textureLoad(CurrentPositions, Texel, 0));
            if (!Other.Valid || !ResolveSimilar(Primary.Normal, Primary.ViewDepth, Other.Normal, Other.ViewDepth))
            {
                continue;
            }
            Neighbours[Count] = u32(Texel.y) * Extent.x + u32(Texel.x);
            TallySum += min(TemporalReservoirs[Neighbours[Count]].Tally, Trace.PathTallyLimit);
            Count += 1u;
        }
        if (Count > 0u)
        {
            let Share = f32(Count);
            let Pairwise = TraceFlag(TraceFlagPairwise);
            let CanonicalHere = Canonical.TargetPdf;
            var Combined = ConstructEmptyPathReservoir();
            var MisCanonical = 0.0;
            var Tally = Canonical.Tally;
            for (var Slot = 0u; Slot < Count; Slot++)
            {
                let Texel = vec2i(i32(Neighbours[Slot] % Extent.x), i32(Neighbours[Slot] / Extent.x));
                var Neighbour = UnpackPath(TemporalReservoirs[Neighbours[Slot]]);
                Neighbour.Tally = min(Neighbour.Tally, Trace.PathTallyLimit);
                Tally += Neighbour.Tally;
                let Other = DecodePrimarySurface(textureLoad(CurrentGuide, Texel, 0), textureLoad(CurrentPositions, Texel, 0));
                let OtherVertex = ConstructPrimaryVertex(Other.Position, Other.Normal, Other.Family);
                var Forward: ShiftResult;
                Forward.Valid = false;
                Forward.Target = 0.0;
                Forward.Rest = vec3f(0.0);
                Forward.Omega = vec3f(0.0, 0.0, 1.0);
                if (Neighbour.Weight > 0.0)
                {
                    Forward = EvaluateShift(Neighbour.Sample, Vertex, true);
                }
                var ReverseTarget = 0.0;
                if (Canonical.Weight > 0.0 && CanonicalHere > 0.0)
                {
                    ReverseTarget = EvaluateShift(Canonical.Sample, OtherVertex, TraceFlag(TraceFlagMisVisibility)).Target;
                }
                var MisNeighbour = 0.0;
                if (Pairwise)
                {
                    MisNeighbour = ResolvePairwiseNeighbour(Neighbour.Tally, Neighbour.TargetPdf, Canonical.Tally, Forward.Target, Share);
                    MisCanonical += ResolvePairwiseCanonical(Neighbour.Tally, ReverseTarget, Canonical.Tally, CanonicalHere, Share) / Share;
                }
                else
                {
                    MisNeighbour = Neighbour.Tally / max(TallySum, 1.0);
                }
                var Shifted = Neighbour.Sample;
                Shifted.Rest = Forward.Rest;
                Shifted.Omega = Forward.Omega;
                StreamPath(&Combined, Shifted, MisNeighbour * Forward.Target * Neighbour.Weight, Forward.Target, AdvanceUniform(&Seed));
            }
            if (!Pairwise)
            {
                MisCanonical = Canonical.Tally / max(TallySum, 1.0);
            }
            StreamPath(&Combined, Canonical.Sample, MisCanonical * CanonicalHere * Canonical.Weight, CanonicalHere, AdvanceUniform(&Seed));
            Combined.Tally = Tally;
            FinalisePath(&Combined);
            Result = Combined;
        }
    }

    var Indirect = vec3f(0.0);
    var Specular = Specular0;
    if (Result.Weight > 0.0)
    {
        let Split = ResolvePathSplit(Result, Vertex);
        Indirect = Split.Diffuse;
        Specular += Split.Specular;
    }
    FinalReservoirs[Index] = PackPath(Result);
    textureStore(IndirectTarget, vec2i(TracePixel), vec4f(Indirect, 1.0));
    textureStore(SpecularTarget, vec2i(TracePixel), vec4f(Specular, 1.0));
}
