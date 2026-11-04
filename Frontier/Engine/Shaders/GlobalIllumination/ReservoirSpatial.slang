//============================================================================================================================================
//                                                         RESERVOIRSPATIAL.SLANG
//============================================================================================================================================
// 🧩 ReSTIR GI spatial reuse with the reconnection-shift Jacobian, 1/Z bias correction, a visibility test, and the resolve.

#pragma once
#include "IntrinsicParity.slang"
#include "HostConstants.slang"

// 📝 Per resampling pixel r with primary surface (x_v, n_v):
//    ① its own temporal reservoir seeds the spatial reservoir;
//    ② SpatialCount nearby reservoirs q are proposed in a disk; a proposal is refused unless its surface agrees in
//       normal and depth. Its sample y = (x_s, n_s, L) is reconnected to x_v, so its weight carries the solid-angle
//       Jacobian |∂ω_r/∂ω_q| = (cos φ_r / cos φ_q)·(‖x_q − x_s‖² / ‖x_v − x_s‖²); escaped samples are directional
//       and carry 1. Extreme Jacobians are refused rather than clamped;
//    ③ Z sums the tallies of every domain whose own surface sees the chosen y above its horizon, so W = Σw/(Z·p̂(y));
//    ④ an adopted y is traced for visibility from x_v and, if occluded, the pixel falls back to its own reservoir;
//    ⑤ resolve: E = L(y) · max(0, n_v·ω_y) · W, the demodulated irradiance the variance integrator receives.

#include "CardSampling.slang"
#include "ReservoirLayouts.slang"
#include "DiscrepancySequence.slang"
#include "RayIntersection.slang"

  [[vk::binding(1, 0)]] Texture2D<float4>                            Guide;
  [[vk::binding(2, 0)]] Texture2D<float>                     SurfaceDepth;
  [[vk::binding(3, 0)]] StructuredBuffer<PackedReservoir> CurrentReservoirs;
  [[vk::binding(4, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4>                 IrradianceTarget;

static const uint ProposalCapacity = 8u;

float3 ResolveTracePosition(int2 TracePixel)
{
    let ScreenPixel = min(TracePixel * ToSigned(Refresh.TraceScale), int2(Refresh.ScreenExtent) - int2(1));
    return ReconstructWorldPosition(float2(ScreenPixel) + 0.5, LoadTexel(SurfaceDepth, ScreenPixel, 0));
}

float ResolveReconnectionJacobian(ReservoirSample Sample, float3 Receiver, float3 Donor)
{
    if (Sample.Identity == SkyIdentity)
    {
        return 1.0;
    }
    let ToDonor = Donor - Sample.Position;
    let ToReceiver = Receiver - Sample.Position;
    let DonorSquared = max(dot(ToDonor, ToDonor), 1.0e-6);
    let ReceiverSquared = max(dot(ToReceiver, ToReceiver), 1.0e-6);
    let CosineDonor = abs(dot(Sample.Normal, ToDonor)) * rsqrt(DonorSquared);
    let CosineReceiver = abs(dot(Sample.Normal, ToReceiver)) * rsqrt(ReceiverSquared);
    return (CosineReceiver / max(CosineDonor, 1.0e-4)) * (DonorSquared / ReceiverSquared);
}

 
[shader("compute")]
[numthreads(8, 8, 1)]
void ResampleSpatial(uint3 Invocation : SV_DispatchThreadID)
{
    let TracePixel = int2(Invocation.xy);
    let Extent = int2(Refresh.TraceExtent);
    if (any(TracePixel >= Extent))
    {
        return;
    }
    let GuideTexel = LoadTexel(Guide, TracePixel, 0);
    if (GuideTexel.w < 0.5)
    {
        IrradianceTarget[TracePixel] = float4(0.0);
        return;
    }
    let Normal = DecodeOctahedral(GuideTexel.xy);
    let ViewDepth = GuideTexel.z;
    let Position = ResolveTracePosition(TracePixel);
    var Seed = ConstructPixelSeed(uint2(TracePixel), Refresh.RefreshIndex, 23u);

    // ① own reservoir
    let Own = UnpackReservoir(CurrentReservoirs[ToUnsigned(TracePixel.y * Extent.x + TracePixel.x)]);
    var Spatial = ConstructEmptyReservoir();
    MergeReservoir(Spatial, Own.Sample, ResolveReservoirTarget(Own.Sample, Position, Normal) * Own.Weight * Own.Tally, Own.Tally,
                   AdvanceUniform(Seed));

    // ② proposals
    var DonorPositions : float3[8] = {};
    var DonorNormals : float3[8] = {};
    var DonorTallies : float[8] = {};
    var DonorCount = 0u;
    if (FeatureEnabled(Refresh, FeatureSpatialReuse))
    {
        let Count = min(Refresh.SpatialCount, ProposalCapacity);
        for (var Proposal = 0u; Proposal < Count; Proposal++)
        {
            let Offset = ProjectUniformDisk(float2(AdvanceUniform(Seed), AdvanceUniform(Seed))) * Refresh.SpatialRadius;
            let Donor = clamp(TracePixel + int2(RoundEven(Offset)), int2(0), Extent - int2(1));
            if (all(Donor == TracePixel))
            {
                continue;
            }
            let DonorGuide = LoadTexel(Guide, Donor, 0);
            let DonorNormal = DecodeOctahedral(DonorGuide.xy);
            if (DonorGuide.w < 0.5 || dot(DonorNormal, Normal) < 0.9 || abs(DonorGuide.z - ViewDepth) > 0.1 * ViewDepth)
            {
                continue;
            }
            let Reservoir = UnpackReservoir(CurrentReservoirs[ToUnsigned(Donor.y * Extent.x + Donor.x)]);
            if (Reservoir.Tally <= 0.0 || Reservoir.Weight <= 0.0)
            {
                continue;
            }
            let DonorPosition = ResolveTracePosition(Donor);
            if (abs(dot(DonorPosition - Position, Normal)) > 0.08 * max(ViewDepth, 0.5))
            {
                continue;
            }
            let Jacobian = ResolveReconnectionJacobian(Reservoir.Sample, Position, DonorPosition);
            let MaxJac = SelectBranch(8.0, 5.0, FeatureEnabled(Refresh, FeatureFireflyFilter));
            if (Jacobian > MaxJac || Jacobian < (1.0 / MaxJac) || Jacobian != Jacobian)
            {
                continue;
            }
            let Target = ResolveReservoirTarget(Reservoir.Sample, Position, Normal);
            if (MergeReservoir(Spatial, Reservoir.Sample, Target * Reservoir.Weight * Reservoir.Tally * Jacobian, Reservoir.Tally,
                               AdvanceUniform(Seed)))
            {
                Spatial.Adopted = true;
            }
            DonorPositions[DonorCount] = DonorPosition;
            DonorNormals[DonorCount]   = DonorNormal;
            DonorTallies[DonorCount]   = Reservoir.Tally;
            DonorCount++;
        }
    }

    // ④ visibility of an adopted sample from this surface
    if (Spatial.Adopted && FeatureEnabled(Refresh, FeatureVisibilityReuse))
    {
        let Toward = Spatial.Sample.Position - Position;
        let Length = length(Toward);
        var Visible = 1.0;
        if (FeatureEnabled(Refresh, FeatureRayTracing))
        {
            let Reach = SelectBranch(Length - 3.0 * RayEpsilon, 400.0, Spatial.Sample.Identity == SkyIdentity);
            Visible   = TraceSceneVisibility(Position + Normal * RayEpsilon, Toward / max(Length, 1.0e-4), RayEpsilon, max(Reach, 0.0));
        }
        else
        {
            let Bias = ResolveSurfaceBias(Position);
            let Reach = SelectBranch(Length - 2.0 * ResolveSurfaceBias(Spatial.Sample.Position), 300.0, Spatial.Sample.Identity == SkyIdentity);
            Visible   = TraverseSceneVisibility(Position + Normal * Bias, Toward / max(Length, 1.0e-4), 0.5 * Bias, max(Reach, 0.0));
        }
        if (Visible < 0.5)
        {
            Spatial = Own;
            Spatial.WeightSum = ResolveReservoirTarget(Own.Sample, Position, Normal) * Own.Weight * Own.Tally;
            Spatial.Adopted = false;
            DonorCount = 0u;
        }
    }

    // ③ 1/Z bias correction over the domains that could have produced y
    let Chosen = Spatial.Sample;
    var Z = SelectBranch(0.0, Own.Tally, ResolveReservoirTarget(Chosen, Position, Normal) > 0.0);
    for (var Donor = 0u; Donor < DonorCount; Donor++)
    {
        if (ResolveReservoirTarget(Chosen, DonorPositions[Donor], DonorNormals[Donor]) > 0.0)
        {
            Z += DonorTallies[Donor];
        }
    }
    let Target = ResolveReservoirTarget(Chosen, Position, Normal);
    let Weight = SelectBranch(0.0, Spatial.WeightSum / (Z * Target), Target > 0.0 && Z > 0.0);

    // ⑤ resolve the demodulated irradiance
    let Cosine = max(dot(Normal, ResolveSampleDirection(Chosen, Position)), 0.0);
    var Irradiance = Chosen.Radiance * Cosine * Weight;
    let Luminance = ResolveLuminance(Irradiance);
    let ClampScale = SelectBranch(40.0, 22.0, FeatureEnabled(Refresh, FeatureFireflyFilter));
    let Limit = ClampScale * max(ResolveLuminance(Refresh.SunIlluminance), 0.5);
    if (Luminance > Limit)
    {
        Irradiance *= Limit / Luminance;
    }
    IrradianceTarget[TracePixel] = float4(Irradiance, 1.0);
}
