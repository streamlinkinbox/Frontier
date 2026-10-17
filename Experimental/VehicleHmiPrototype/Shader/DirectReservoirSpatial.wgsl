//============================================================================================================================================
//                                                        DIRECTRESERVOIRSPATIAL.WGSL
//============================================================================================================================================
// 🧩 ReSTIR DI, second pass: spatial reuse with pairwise MIS, the final shadow ray, and the shaded direct light split
//    into demodulated diffuse irradiance and specular radiance.

// 📝 Each pixel's temporal reservoir is the canonical sample; k neighbours inside a disk of trace pixels whose guide
//    is similar contribute theirs. Every sample is evaluated at the receiving surface (p̂_c) and at its own (p̂_i,
//    stored), the pairwise weights are formed, the survivor's visibility is traced and the result shaded with the
//    full layered reflectance. The reservoir written here is the next frame's temporal input.

//! include "DirectReservoir.wgsl"

@group(0) @binding(1) var CurrentGuide                            : texture_2d<f32>;
@group(0) @binding(2) var CurrentPositions                        : texture_2d<f32>;
@group(0) @binding(3) var<storage, read> TemporalReservoirs       : array<DirectRecord>;
@group(0) @binding(4) var<storage, read_write> FinalReservoirs    : array<DirectRecord>;
@group(0) @binding(5) var DiffuseTarget                           : texture_storage_2d<rgba16float, write>;
@group(0) @binding(6) var SpecularTarget                          : texture_storage_2d<rgba16float, write>;

const MaxNeighbours : u32 = 8u;

@compute @workgroup_size(8, 8)
fn ResampleDirectSpatial(@builtin(global_invocation_id) Invocation: vec3u)
{
    let TracePixel = Invocation.xy;
    let Extent = vec2u(Frame.TraceExtent);
    if (any(TracePixel >= Extent))
    {
        return;
    }
    let Index = TracePixel.y * Extent.x + TracePixel.x;
    let Primary = DecodePrimarySurface(textureLoad(CurrentGuide, vec2i(TracePixel), 0), textureLoad(CurrentPositions, vec2i(TracePixel), 0));
    if (!Primary.Valid)
    {
        FinalReservoirs[Index] = PackDirect(ConstructEmptyDirect());
        textureStore(DiffuseTarget, vec2i(TracePixel), vec4f(0.0));
        textureStore(SpecularTarget, vec2i(TracePixel), vec4f(0.0));
        return;
    }
    let Position = Primary.Position;
    let Normal = Primary.Normal;
    let Surface = ProjectSurfaceSample(FetchMaterial(Primary.Family));
    let View = normalize(Frame.CameraPosition - Position);
    var Seed = ConstructPixelSeed(TracePixel, Frame.FrameIndex, 37u);

    var Canonical = UnpackDirect(TemporalReservoirs[Index]);
    var Result = Canonical;
    if (FeatureEnabled(Frame, FeatureSpatialReuse) && Trace.DirectSpatialCount > 0u)
    {
        // gather similar neighbours first: the pairwise weights need their count
        var Neighbours: array<u32, MaxNeighbours>;
        var Count = 0u;
        var TallySum = Canonical.Tally;
        let Attempts = min(Trace.DirectSpatialCount, MaxNeighbours);
        for (var Attempt = 0u; Attempt < Attempts; Attempt++)
        {
            let Offset = ProjectUniformDisk(vec2f(AdvanceUniform(&Seed), AdvanceUniform(&Seed))) * Trace.DirectSpatialRadius;
            let Texel = vec2i(TracePixel) + vec2i(round(Offset));
            if (any(Texel < vec2i(0)) || any(Texel >= vec2i(Extent)) || all(Texel == vec2i(TracePixel)))
            {
                continue;
            }
            let Other = DecodePrimarySurface(textureLoad(CurrentGuide, Texel, 0), textureLoad(CurrentPositions, Texel, 0));
            if (!Other.Valid || !ResolveSimilar(Normal, Primary.ViewDepth, Other.Normal, Other.ViewDepth))
            {
                continue;
            }
            Neighbours[Count] = u32(Texel.y) * Extent.x + u32(Texel.x);
            TallySum += min(TemporalReservoirs[Neighbours[Count]].Tally, Trace.DirectTallyLimit);
            Count += 1u;
        }
        if (Count > 0u)
        {
            let Share = f32(Count);
            let CanonicalHere = Canonical.TargetPdf;
            var Combined = ConstructEmptyDirect();
            var MisCanonical = 0.0;
            let Pairwise = TraceFlag(TraceFlagPairwise);
            for (var Slot = 0u; Slot < Count; Slot++)
            {
                var Neighbour = UnpackDirect(TemporalReservoirs[Neighbours[Slot]]);
                Neighbour.Tally = min(Neighbour.Tally, Trace.DirectTallyLimit);
                let Other = DecodePrimarySurface(textureLoad(CurrentGuide, vec2i(i32(Neighbours[Slot] % Extent.x), i32(Neighbours[Slot] / Extent.x)), 0),
                                                 textureLoad(CurrentPositions, vec2i(i32(Neighbours[Slot] % Extent.x), i32(Neighbours[Slot] / Extent.x)), 0));
                let OtherSurface = ProjectSurfaceSample(FetchMaterial(Other.Family));
                let OtherView = normalize(Frame.CameraPosition - Other.Position);
                // the neighbour's sample here, the canonical sample there
                let NeighbourHere = select(0.0, ResolveDirectTarget(ReconstructDirectSample(Neighbour, Position, Normal), Surface, Normal, View), Neighbour.Weight > 0.0);
                let CanonicalThere = select(0.0, ResolveDirectTarget(ReconstructDirectSample(Canonical, Other.Position, Other.Normal), OtherSurface, Other.Normal, OtherView),
                                            CanonicalHere > 0.0);
                var MisNeighbour = 0.0;
                if (Pairwise)
                {
                    MisNeighbour = ResolvePairwiseNeighbour(Neighbour.Tally, Neighbour.TargetPdf, Canonical.Tally, NeighbourHere, Share);
                    MisCanonical += ResolvePairwiseCanonical(Neighbour.Tally, CanonicalThere, Canonical.Tally, CanonicalHere, Share) / Share;
                }
                else
                {
                    MisNeighbour = Neighbour.Tally / max(TallySum, 1.0);
                }
                StreamDirect(&Combined, Neighbour, MisNeighbour * NeighbourHere * Neighbour.Weight, Neighbour.Tally, NeighbourHere, AdvanceUniform(&Seed));
            }
            if (!Pairwise)
            {
                MisCanonical = Canonical.Tally / max(TallySum, 1.0);
            }
            StreamDirect(&Combined, Canonical, MisCanonical * CanonicalHere * Canonical.Weight, Canonical.Tally, CanonicalHere, AdvanceUniform(&Seed));
            FinaliseDirect(&Combined);
            Result = Combined;
        }
    }

    // final visibility and shading
    var Diffuse = vec3f(0.0);
    var Specular = vec3f(0.0);
    if (Result.Weight > 0.0)
    {
        let Chosen = ReconstructDirectSample(Result, Position, Normal);
        if (!Chosen.Valid || dot(Normal, Chosen.Direction) <= 0.0 || !ResolveLightVisible(Chosen, Position, Normal))
        {
            Result.Weight = 0.0;
        }
        else
        {
            var Split = EvaluateLightSplit(Chosen, Surface, Normal, View);
            var Scale = Result.Weight;
            if (Trace.FireflyClamp > 0.0)
            {
                let Luminance = ResolveSplitLuminance(Split, Surface) * Scale;
                Scale *= select(1.0, Trace.FireflyClamp / Luminance, Luminance > Trace.FireflyClamp);
            }
            // 📝 Diffuse leaves as irradiance (× π) so the display modulates it by ρ_d/π like the card path's.
            Diffuse = Split.Diffuse * (Pi * Scale);
            Specular = Split.Specular * Scale;
        }
    }
    FinalReservoirs[Index] = PackDirect(Result);
    textureStore(DiffuseTarget, vec2i(TracePixel), vec4f(Diffuse, 1.0));
    textureStore(SpecularTarget, vec2i(TracePixel), vec4f(Specular, 1.0));
}
