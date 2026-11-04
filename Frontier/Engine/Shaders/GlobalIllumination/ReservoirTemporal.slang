//============================================================================================================================================
//                                                        RESERVOIRTEMPORAL.SLANG
//============================================================================================================================================
// 🧩 ReSTIR GI initial candidate and temporal reuse: a cosine ray through the scene distance field, then the prior reservoir.

#pragma once
#include "IntrinsicParity.slang"
#include "HostConstants.slang"

// 📝 Per resampling pixel:
//    ① the primary surface (x_v, n_v) is read from the surface projection and written to this refresh's guide;
//    ② one cosine-weighted ray is traced through the ONE scene distance field; its hit radiance is read from the
//       surface cards (or the sky on an escape) — this is the candidate, with source pdf cosθ/π;
//    ③ the prior refresh's reservoir is found by motion reprojection and validated against the prior guide; its
//       reconnection vertex is carried with its occupant's motion, its radiance re-read from this refresh's cards, and
//       (on a rotating quarter of pixels) its visibility re-traced, so a vehicle driving between x_v and x_s darkens
//       the reuse instead of leaving a ghost; M is clamped to TemporalTallyLimit;
//    ④ W = Σw / (M · p̂(y)) is stored for the spatial dispatch and for the next refresh.

#include "CardSampling.slang"
#include "ReservoirLayouts.slang"
#include "DiscrepancySequence.slang"
#include "RayIntersection.slang"

  [[vk::binding(1, 0)]] Texture2D<float4>                   SurfaceAttributes;
  [[vk::binding(2, 0)]] Texture2D<float>                        SurfaceDepth;
  [[vk::binding(3, 0)]] Texture2D<float4>                       SurfaceMotion;
  [[vk::binding(4, 0)]] Texture2D<float4>                       PreviousGuide;
  [[vk::binding(5, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4>                         GuideTarget;
  [[vk::binding(6, 0)]] StructuredBuffer<PackedReservoir>   PreviousReservoirs;
  [[vk::binding(7, 0)]] RWStructuredBuffer<PackedReservoir> CurrentReservoirs;

int2 ResolveScreenPixel(uint2 TracePixel)
{
    return min(int2(TracePixel * Refresh.TraceScale), int2(Refresh.ScreenExtent) - int2(1));
}

// 📝 Carries a reconnection vertex that lies on a moving occupant from where it was last refresh to where it is now.
ReservoirSample CarryReservoirSample(ReservoirSample Sample)
{
    var Carried = Sample;
    if (Sample.Identity < ArrayCount(Occupants))
    {
        let Occupant = Occupants[Sample.Identity];
        if ((Occupant.Participation & ParticipationDynamic) != 0u)
        {
            let Local = mul(float4(Sample.Position, 1.0), Occupant.PreviousWorldToLocal);
            Carried.Position = (mul(Local, Occupant.LocalToWorld)).xyz;
            let LocalNormal = (mul(float4(Sample.Normal, 0.0), Occupant.PreviousWorldToLocal)).xyz;
            Carried.Normal = normalize((mul(float4(LocalNormal, 0.0), Occupant.LocalToWorld)).xyz);
        }
    }
    return Carried;
}

float3 RefreshSampleRadiance(ReservoirSample Sample)
{
    if (Sample.Identity == SkyIdentity)
    {
        return SampleSkyRadiance(-Sample.Normal);
    }
    if (FeatureEnabled(Refresh, FeatureRayTracing))
    {
        return Sample.Radiance;
    }
    let Level = ResolveFinestClip(Sample.Position, 1.0);
    let Card = SampleCardRadiance(Sample.Position, Sample.Normal, Sample.Identity, ResolveCardTolerance(Level));
    return SelectBranch(Sample.Radiance, Card.rgb, Card.w > 0.0);
}

 
[shader("compute")]
[numthreads(8, 8, 1)]
void ResampleTemporal(uint3 Invocation : SV_DispatchThreadID)
{
    let TracePixel = Invocation.xy;
    let Extent = uint2(Refresh.TraceExtent);
    if (any(TracePixel >= Extent))
    {
        return;
    }
    let Index = TracePixel.y * Extent.x + TracePixel.x;
    let ScreenPixel = ResolveScreenPixel(TracePixel);
    let Depth = LoadTexel(SurfaceDepth, ScreenPixel, 0);
    if (Depth <= 0.0)
    {
        GuideTarget[int2(TracePixel)] = float4(0.0);
        CurrentReservoirs[Index] = PackReservoir(ConstructEmptyReservoir());
        return;
    }

    // ① primary surface
    let Position = ReconstructWorldPosition(float2(ScreenPixel) + 0.5, Depth);
    let Normal = DecodeOctahedral(LoadTexel(SurfaceAttributes, ScreenPixel, 0).xy);
    let ViewDepth = ResolveViewDepth(Position);
    let ExpectedDepth = ResolvePriorViewDepth(Position);
    // 📝 Guide: octahedral normal, view depth, and 1 + the depth this point had last refresh (w ≥ 1 marks valid).
    GuideTarget[int2(TracePixel)] = float4(EncodeOctahedral(Normal), ViewDepth, 1.0 + ExpectedDepth);
    var Seed = ConstructPixelSeed(TracePixel, Refresh.RefreshIndex, 11u);

    // ② initial candidate generation (RIS over M_init multi-bounce paths in RTX mode, or scene distance field in GTX mode)
    let Bias = ResolveSurfaceBias(Position);
    let Basis = ConstructTangentBasis(Normal);
    let UseRayTrace = FeatureEnabled(Refresh, FeatureRayTracing);
    let InitialCount = SelectBranch(1u, ResolveInitialCandidates(Refresh), UseRayTrace);
    var Current = ConstructEmptyReservoir();

    for (var CandIdx = 0u; CandIdx < InitialCount; CandIdx += 1u)
    {
        let Pair = ProjectTemporalPair(TracePixel, Refresh.RefreshIndex + CandIdx * 37u, 3u + CandIdx * 2u);
        let Direction = mul(ProjectCosineHemisphere(Pair), Basis);
        var Candidate : ReservoirSample = {};
        if (UseRayTrace)
        {
            Candidate = TraceMultiBouncePath(Position + Normal * RayEpsilon, Direction, Seed);
        }
        else
        {
            let Hit = TraverseSceneDistance(Position + Normal * Bias, Direction, 0.5 * Bias, 300.0, 0.0);
            if (Hit.Hit)
            {
                Candidate.Position = Hit.Position;
                Candidate.Normal   = Hit.Normal;
                Candidate.Identity = Hit.Identity;
                Candidate.Radiance = ResolveMarchRadiance(Hit, Direction);
            }
            else
            {
                Candidate.Position = Position + Direction * SkyReach;
                Candidate.Normal   = -Direction;
                Candidate.Identity = SkyIdentity;
                Candidate.Radiance = SampleSkyRadiance(Direction);
            }
        }
        let Cosine = max(dot(Normal, Direction), 0.0);
        let SourcePdf = Cosine * InversePi;
        let InitialWeight = SelectBranch(0.0, ResolveLuminance(Candidate.Radiance) * Cosine / SourcePdf, SourcePdf > 1.0e-6);
        MergeReservoir(Current, Candidate, InitialWeight, 1.0, AdvanceUniform(Seed));
    }

    if (InitialCount > 1u && Current.Tally > 0.0)
    {
        let InitTarget = ResolveReservoirTarget(Current.Sample, Position, Normal);
        let InitW = SelectBranch(0.0, Current.WeightSum / (Current.Tally * InitTarget), InitTarget > 0.0);
        Current.WeightSum = InitTarget * InitW;
        Current.Weight    = InitW;
        Current.Tally     = 1.0;
    }

    // ③ temporal reuse
    if (FeatureEnabled(Refresh, FeatureTemporalReuse))
    {
        let Motion = LoadTexel(SurfaceMotion, ScreenPixel, 0).xy;
        let PriorScreen = float2(ScreenPixel) + 0.5 - Motion;
        let PriorTrace = int2(floor((PriorScreen - 0.5) / float(Refresh.TraceScale) + 0.5));
        if (all(PriorTrace >= int2(0)) && all(PriorTrace < int2(Extent)))
        {
            let PriorGuide = LoadTexel(PreviousGuide, PriorTrace, 0);
            let PriorNormal = DecodeOctahedral(PriorGuide.xy);
            let Similar = PriorGuide.w > 0.5 && dot(PriorNormal, Normal) > 0.85
                        && ClassifyPriorDepth(PriorGuide.z, ExpectedDepth, ViewDepth);
            if (Similar)
            {
                var Prior = UnpackReservoir(PreviousReservoirs[ToUnsigned(PriorTrace.y) * Extent.x + ToUnsigned(PriorTrace.x)]);
                if (Prior.Tally > 0.0)
                {
                    var Reused = CarryReservoirSample(Prior.Sample);
                    if (FeatureEnabled(Refresh, FeatureRadianceRefresh))
                    {
                        Reused.Radiance = RefreshSampleRadiance(Reused);
                    }
                    var Target = ResolveReservoirTarget(Reused, Position, Normal);
                    let Validate = ((TracePixel.x + TracePixel.y * 3u + Refresh.RefreshIndex) & 3u) == 0u;
                    if (FeatureEnabled(Refresh, FeatureVisibilityReuse) && Validate && Target > 0.0)
                    {
                        let Toward = Reused.Position - Position;
                        let Length = length(Toward);
                        var Visible = 1.0;
                        if (UseRayTrace)
                        {
                            let Reach = SelectBranch(Length - 3.0 * RayEpsilon, 400.0, Reused.Identity == SkyIdentity);
                            Visible   = TraceSceneVisibility(Position + Normal * RayEpsilon, Toward / max(Length, 1.0e-4), RayEpsilon, max(Reach, 0.0));
                        }
                        else
                        {
                            let Reach = SelectBranch(Length - 2.0 * ResolveSurfaceBias(Reused.Position), 300.0, Reused.Identity == SkyIdentity);
                            Visible   = TraverseSceneVisibility(Position + Normal * Bias, Toward / max(Length, 1.0e-4), 0.5 * Bias, max(Reach, 0.0));
                        }
                        Target *= Visible;
                    }
                    let Tally = min(Prior.Tally, Refresh.TemporalTallyLimit);
                    MergeReservoir(Current, Reused, Target * Prior.Weight * Tally, Tally, AdvanceUniform(Seed));
                }
            }
        }
    }

    // ④ unbiased contribution weight
    let Selected = ResolveReservoirTarget(Current.Sample, Position, Normal);
    Current.Weight = SelectBranch(0.0, Current.WeightSum / (Current.Tally * Selected), Selected > 0.0 && Current.Tally > 0.0);
    CurrentReservoirs[Index] = PackReservoir(Current);
}

// 📝 Beyond the demo: step ① alone, for a refresh whose indirect illumination is off while its reflections are on. The glossy
//    variance filter reads this refresh's guide (normal, depth, prior depth) to accept its own history and to stop its wavelet
//    and projection at edges; without the resampling nothing wrote it, every texel read as "no surface" (w < 0.5) and the
//    reflections vanished. With indirect illumination on the resampling writes the guide and this entry point never runs.
[shader("compute")]
[numthreads(8, 8, 1)]
void ProjectResamplingGuide(uint3 Invocation : SV_DispatchThreadID)
{
    let TracePixel = Invocation.xy;
    if (any(TracePixel >= uint2(Refresh.TraceExtent)))
    {
        return;
    }
    let ScreenPixel = ResolveScreenPixel(TracePixel);
    let Depth = LoadTexel(SurfaceDepth, ScreenPixel, 0);
    if (Depth <= 0.0)
    {
        GuideTarget[int2(TracePixel)] = float4(0.0);
        return;
    }
    let Position = ReconstructWorldPosition(float2(ScreenPixel) + 0.5, Depth);
    let Normal = DecodeOctahedral(LoadTexel(SurfaceAttributes, ScreenPixel, 0).xy);
    GuideTarget[int2(TracePixel)] = float4(EncodeOctahedral(Normal), ResolveViewDepth(Position), 1.0 + ResolvePriorViewDepth(Position));
}
