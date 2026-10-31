/*==========================================================================================================================================
                                             PATHREPLAYVARIANCEFILTER.SLANG
==========================================================================================================================================*/
// 🧩 Motion-aware variance-guided à-trous wavelet denoiser for replayed global illumination.

#include "PathReplayReservoir.slang"

//------------------------------------------------------------------------------------------------------------------------
//                                              DEMODULATION AND GUARDS
//------------------------------------------------------------------------------------------------------------------------

bool ReplayScatteringReceiver(SurfacePoint R)
{
    if (R.PositionHit.w == 0 || R.MotionPositionToken.w <= 0) return false;
    if ((Extent.Primary.w & 64u) != 0) return true;
    return max(R.AlbedoIor.x, max(R.AlbedoIor.y, R.AlbedoIor.z)) > 1e-4;
}

// 📝 Demodulate surface reflectance before spatial filtering so textured and coloured edges stay crisp.
float3 ReplayVarianceDemodulator(SurfacePoint R)
{
    bool AuthoredMaterials = (Extent.Primary.w & 134217728u) != 0;
    if (AuthoredMaterials && MaterialAddress(R) > 0 && MaterialTransport(R) == 2u)
        return float3(1.0);
    float3 Albedo = clamp(R.AlbedoIor.xyz, 0.0, 1.0);
    if ((Extent.Primary.w & 64u) == 0)
        return max(Albedo, float3(0.04));
    float Metallic = clamp(R.ViewMetallic.w, 0.0, 1.0);
    float SpecularFloor = 0.08 * clamp(R.ResidualSpecular.w, 0.0, 1.0);
    float3 Effective = Albedo * (1.0 - Metallic) + max(Albedo, float3(SpecularFloor)) * Metallic +
                       float3(SpecularFloor * (1.0 - Metallic));
    uint Address = MaterialAddress(R);
    if (AuthoredMaterials && Address > 0u)
    {
        uint Offset = Address - 1u;
        uint Count = min(uint(Angles[Offset]), 8u);
        for (uint Layer = 0u; Layer < Count; ++Layer)
        {
            uint P = Offset + 8u + Layer * 24u;
            if (uint(Angles[P + 12u]) == 1u)
                Effective = max(Effective, float3(0.04 * saturate(Angles[P + 6u])));
            else if (Angles[P + 19u] > 0.0)
            {
                float3 SheenRgb = saturate(float3(Angles[P + 16u], Angles[P + 17u], Angles[P + 18u]));
                Effective = max(Effective, SheenRgb * saturate(Angles[P + 19u]) * 0.35);
            }
        }
    }
    return max(Effective, float3(0.04));
}

bool ReplayVarianceHistoryValid(float4 OldIrradiance, float4 OldMoments)
{
    if (!all(isfinite(OldIrradiance)) || !all(isfinite(OldMoments))) return false;
    if (OldIrradiance.w <= 0.0 || OldIrradiance.w > 64.0) return false;
    if (any(OldIrradiance.xyz < 0.0) || any(OldIrradiance.xyz > 1e6)) return false;
    if (OldMoments.x < 0.0 || OldMoments.x > 1e6 || OldMoments.y < 0.0 || OldMoments.y > 1e12) return false;
    if (OldMoments.z < 0.0 || OldMoments.z > 1.0 || OldMoments.w < 0.0 || OldMoments.w > 1.0) return false;
    return true;
}

bool ReplayVarianceCompatibleTap(SurfacePoint R, SurfacePoint S, float PixelStep, out float PlaneRatio,
                                 out float NormalCosine)
{
    PlaneRatio = 0.0;
    NormalCosine = 0.0;
    if (!ReplayScatteringReceiver(S)) return false;
    bool AuthoredMaterials = (Extent.Primary.w & 134217728u) != 0;
    if (AuthoredMaterials && MaterialTransport(R) != MaterialTransport(S)) return false;
    float RLength = dot(R.NormalRoughness.xyz, R.NormalRoughness.xyz);
    float SLength = dot(S.NormalRoughness.xyz, S.NormalRoughness.xyz);
    if (RLength <= 1e-12 || SLength <= 1e-12) return false;
    float3 NormalR = R.NormalRoughness.xyz * rsqrt(RLength);
    float3 NormalS = S.NormalRoughness.xyz * rsqrt(SLength);
    NormalCosine = dot(NormalR, NormalS);
    if (NormalCosine < 0.35) return false;
    float Footstep = isfinite(R.MotionPixel.z) && R.MotionPixel.z > 0.0 ? clamp(R.MotionPixel.z, 0.02, 0.35) : 0.08;
    float Span = max(PixelStep, 1.0);
    float3 Delta = S.PositionHit.xyz - R.PositionHit.xyz;
    float ChordLimit = max(0.08, Footstep * Span * 1.35);
    if (dot(Delta, Delta) > ChordLimit * ChordLimit) return false;
    float PlaneTolerance = max(0.02, Footstep * Span * 0.22);
    float PlaneOffset = max(abs(dot(Delta, NormalR)), abs(dot(Delta, NormalS)));
    if (PlaneOffset > PlaneTolerance) return false;
    float RoughR = AuthoredMaterials ? ReplayEffectiveSpecularRoughness(R) : R.NormalRoughness.w;
    float RoughS = AuthoredMaterials ? ReplayEffectiveSpecularRoughness(S) : S.NormalRoughness.w;
    if ((Extent.Primary.w & 64u) != 0 &&
        (abs(RoughR - RoughS) > 0.25 || abs(R.ViewMetallic.w - S.ViewMetallic.w) > 0.25))
        return false;
    PlaneRatio = PlaneOffset / PlaneTolerance;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                            SUBPIXEL TEMPORAL GATHER
//------------------------------------------------------------------------------------------------------------------------

// 📝 Gather prior demodulated irradiance and moments before ResolveReplayReservoirs overwrites Slots 1 and 4.
[shader("compute")][numthreads(64, 1, 1)] void GatherReplayVarianceHistory(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x;
    if (Pixel >= Extent.Dimensions.x) return;
    SurfacePoint R = Receivers[Pixel];
    float4 GatheredIrradiance = 0;
    float4 GatheredMoments = 0;
    if ((Extent.Primary.w & 4194304u) == 0 && Extent.History.w == 0 && Extent.History.z != 0 &&
        ReplayScatteringReceiver(R) && R.MotionPixel.w != 0 && isfinite(R.MotionPixel.z) &&
        ReplayMotionConvertible(R.MotionPixel.xy))
    {
        float MotionNormLength = dot(R.MotionNormal.xyz, R.MotionNormal.xyz);
        if (MotionNormLength > 1e-12)
        {
            float3 MotionUnitNormal = R.MotionNormal.xyz * rsqrt(MotionNormLength);
            float Footstep = R.MotionPixel.z > 0.0 ? clamp(R.MotionPixel.z, 0.02, 0.40) : 0.08;
            float ChordLimit = max(0.04, Footstep * 1.30);
            float PlaneLimit = max(0.015, Footstep * 0.35);
            int2 Corner = int2(floor(R.MotionPixel.xy));
            float2 Fraction = clamp(R.MotionPixel.xy - float2(Corner), 0.0, 1.0);
            float3 IrradianceSum = 0;
            float LengthSum = 0;
            float2 MomentSum = 0;
            float WeightSum = 0;
            for (int OffsetY = 0; OffsetY <= 1; ++OffsetY)
                for (int OffsetX = 0; OffsetX <= 1; ++OffsetX)
                {
                    float Bilinear = (OffsetX == 0 ? (1.0 - Fraction.x) : Fraction.x) *
                                     (OffsetY == 0 ? (1.0 - Fraction.y) : Fraction.y);
                    if (Bilinear <= 1e-5) continue;
                    int2 P = Corner + int2(OffsetX, OffsetY);
                    if (any(P < 0) || any(P >= int2(Extent.History.xy))) continue;
                    uint Previous = uint(P.y) * Extent.History.x + uint(P.x);
                    float4 PriorPosition = OutputColour[ReplaySlot(1, Previous)];
                    float3 PriorNormal = OutputColour[ReplaySlot(4, Previous)].xyz;
                    float4 OldIrradiance = OutputColour[ReplaySlot(15, Previous)];
                    float4 OldMoments = OutputColour[ReplaySlot(16, Previous)];
                    float PriorNormLength = dot(PriorNormal, PriorNormal);
                    if (PriorPosition.w != R.MotionPositionToken.w || PriorNormLength <= 1e-12 ||
                        !ReplayVarianceHistoryValid(OldIrradiance, OldMoments))
                        continue;
                    float3 PriorUnitNormal = PriorNormal * rsqrt(PriorNormLength);
                    if (dot(PriorUnitNormal, MotionUnitNormal) < 0.95) continue;
                    float3 Delta = PriorPosition.xyz - R.MotionPositionToken.xyz;
                    if (dot(Delta, Delta) > ChordLimit * ChordLimit) continue;
                    float PlaneOffset = max(abs(dot(Delta, MotionUnitNormal)), abs(dot(Delta, PriorUnitNormal)));
                    if (PlaneOffset > PlaneLimit) continue;
                    float ReceiverRoughness = (Extent.Primary.w & 134217728u) != 0
                        ? ReplayEffectiveSpecularRoughness(R)
                        : clamp(R.NormalRoughness.w, 0.0, 1.0);
                    if (abs(OldMoments.z - ReceiverRoughness) > 0.15 ||
                        abs(OldMoments.w - clamp(R.ViewMetallic.w, 0.0, 1.0)) > 0.15)
                        continue;
                    IrradianceSum += Bilinear * OldIrradiance.xyz;
                    LengthSum += Bilinear * OldIrradiance.w;
                    MomentSum += Bilinear * OldMoments.xy;
                    WeightSum += Bilinear;
                }
            if (WeightSum > 1e-4)
            {
                float InverseWeight = 1.0 / WeightSum;
                GatheredIrradiance = float4(IrradianceSum * InverseWeight, LengthSum * InverseWeight);
                GatheredMoments = float4(MomentSum * InverseWeight, 1.0, 0.0);
                InterlockedAdd(Counters[14], 1);
            }
        }
    }
    OutputColour[ReplaySlot(17, Pixel)] = GatheredIrradiance;
    OutputColour[ReplaySlot(18, Pixel)] = GatheredMoments;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           TEMPORAL MOMENTS AND VARIANCE
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")][numthreads(64, 1, 1)] void IntegrateReplayVarianceMoments(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x;
    if (Pixel >= Extent.Dimensions.x) return;
    SurfacePoint R = Receivers[Pixel];
    float4 RawOutput = OutputColour[Pixel];
    if (!ReplayScatteringReceiver(R))
    {
        OutputColour[ReplaySlot(15, Pixel)] = 0;
        OutputColour[ReplaySlot(16, Pixel)] = 0;
        OutputColour[ReplaySlot(19, Pixel)] = float4(all(isfinite(RawOutput.xyz)) ? max(RawOutput.xyz, 0.0) : 0.0, 0.0);
        return;
    }
    float3 Emission = all(isfinite(R.ResidualSpecular.xyz)) ? max(R.ResidualSpecular.xyz, 0.0) : 0.0;
    float3 RawIndirect = all(isfinite(RawOutput.xyz)) ? max(RawOutput.xyz - Emission, 0.0) : 0.0;
    float3 CurrentIrradiance = RawIndirect / ReplayVarianceDemodulator(R);
    float CurrentLuminance = Power(CurrentIrradiance);

    // 📝 Spatial 3x3 luminance statistics supply disocclusion variance (Length < 4) and detect lighting shifts.
    int2 Centre = int2(Pixel % Extent.History.x, Pixel / Extent.History.x);
    float SpatialMean = 0.0;
    float SpatialSecond = 0.0;
    float SpatialCount = 0.0;
    for (int OffsetY = -1; OffsetY <= 1; ++OffsetY)
        for (int OffsetX = -1; OffsetX <= 1; ++OffsetX)
        {
            int2 Q = Centre + int2(OffsetX, OffsetY);
            if (any(Q < 0) || any(Q >= int2(Extent.History.xy))) continue;
            uint NeighbourIndex = uint(Q.y) * Extent.History.x + uint(Q.x);
            SurfacePoint S = Receivers[NeighbourIndex];
            float PlaneRatio = 0.0, NormalCosine = 0.0;
            if (OffsetX != 0 || OffsetY != 0)
            {
                if (!ReplayVarianceCompatibleTap(R, S, length(float2(OffsetX, OffsetY)), PlaneRatio, NormalCosine))
                    continue;
            }
            float4 NeighbourRaw = OutputColour[NeighbourIndex];
            float3 NeighbourEmission = all(isfinite(S.ResidualSpecular.xyz)) ? max(S.ResidualSpecular.xyz, 0.0) : 0.0;
            float3 NeighbourIndirect = all(isfinite(NeighbourRaw.xyz)) ? max(NeighbourRaw.xyz - NeighbourEmission, 0.0) : 0.0;
            float NeighbourLum = Power(NeighbourIndirect / ReplayVarianceDemodulator(S));
            SpatialMean += NeighbourLum;
            SpatialSecond += NeighbourLum * NeighbourLum;
            SpatialCount += 1.0;
        }
    SpatialMean /= max(SpatialCount, 1.0);
    SpatialSecond /= max(SpatialCount, 1.0);
    float SpatialVariance = max(0.0, SpatialSecond - SpatialMean * SpatialMean);

    float4 GatheredIrradiance = OutputColour[ReplaySlot(17, Pixel)];
    float4 GatheredMoments = OutputColour[ReplaySlot(18, Pixel)];
    bool AuthoredMaterials = (Extent.Primary.w & 134217728u) != 0;
    float Roughness = AuthoredMaterials ? clamp(ReplayEffectiveSpecularRoughness(R), 0.04, 1.0)
                                        : clamp(R.NormalRoughness.w, 0.05, 1.0);
    float Metallic = clamp(R.ViewMetallic.w, 0.0, 1.0);
    bool StationaryPixel = all(R.PositionHit.xyz == R.MotionPositionToken.xyz) &&
                           length(R.MotionPixel.xy - float2(Centre)) < 1e-3;
    float MaxHistory = ((Extent.Primary.w & 64u) != 0 && !StationaryPixel) ?
                       lerp(4.0, 12.0, Roughness * Roughness) : 16.0;
    if (AuthoredMaterials && !StationaryPixel && Roughness < 0.25)
    {
        float3 CurrentRefl = reflect(-Unit(R.ViewMetallic.xyz), Unit(R.NormalRoughness.xyz));
        float3 PriorView = Unit(CameraProjection(4).xyz - R.MotionPositionToken.xyz);
        float3 PriorRefl = reflect(-PriorView, Unit(R.MotionNormal.xyz));
        float LobeVariance = max(1e-4, 2.0 * Roughness * Roughness * Roughness * Roughness);
        float AngularWeight = exp(-max(0.0, 1.0 - dot(CurrentRefl, PriorRefl)) / LobeVariance);
        MaxHistory = max(2.0, lerp(2.0, MaxHistory, AngularWeight));
    }

    float3 IntegratedIrradiance = CurrentIrradiance;
    float MeanLuminance = CurrentLuminance;
    float SecondLuminance = CurrentLuminance * CurrentLuminance;
    float Length = 1.0;
    if (GatheredMoments.z > 0.5 &&
        ReplayVarianceHistoryValid(GatheredIrradiance, float4(GatheredMoments.xy, Roughness, Metallic)))
    {
        float PriorVariance = max(0.0, GatheredMoments.y - GatheredMoments.x * GatheredMoments.x);
        float NoiseScale = max(sqrt(SpatialVariance), sqrt(PriorVariance)) +
                           0.08 * max(1.0, max(SpatialMean, GatheredMoments.x));
        float ShiftRatio = SpatialCount >= 4.0 ? abs(SpatialMean - GatheredMoments.x) / NoiseScale : 0.0;
        float EffectiveLength = GatheredIrradiance.w;
        if (ShiftRatio > 3.5)
            EffectiveLength = max(0.5, EffectiveLength / (1.0 + 0.85 * (ShiftRatio - 3.5)));
        Length = min(EffectiveLength + 1.0, MaxHistory);
        float Alpha = 1.0 / Length;
        IntegratedIrradiance = lerp(GatheredIrradiance.xyz, CurrentIrradiance, Alpha);
        MeanLuminance = lerp(GatheredMoments.x, CurrentLuminance, Alpha);
        SecondLuminance = lerp(GatheredMoments.y, CurrentLuminance * CurrentLuminance, Alpha);
    }

    float TemporalVariance = max(0.0, SecondLuminance - MeanLuminance * MeanLuminance);
    float Variance = TemporalVariance / Length;
    if (Length < 4.0)
    {
        InterlockedAdd(Counters[15], 1);
        float Blend = clamp((Length - 1.0) / 3.0, 0.0, 1.0);
        Variance = lerp(SpatialVariance, max(TemporalVariance, 0.25 * SpatialVariance), Blend) / Length;
    }

    // 📝 Persist un-blurred temporal irradiance so spatial passes never diffuse progressively across frames.
    OutputColour[ReplaySlot(15, Pixel)] = float4(IntegratedIrradiance, Length);
    OutputColour[ReplaySlot(16, Pixel)] = float4(MeanLuminance, SecondLuminance, Roughness, Metallic);
    OutputColour[ReplaySlot(19, Pixel)] = float4(IntegratedIrradiance, Variance);
}

//------------------------------------------------------------------------------------------------------------------------
//                                           EDGE-STOPPING WAVELET FILTER
//------------------------------------------------------------------------------------------------------------------------

[shader("compute")][numthreads(64, 1, 1)] void FilterReplayVarianceWavelet(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x;
    if (Pixel >= Extent.Dimensions.x) return;
    uint Step = max(Extent.Dimensions.z, 1u);
    uint PassIndex = Extent.Dimensions.w;
    uint SourceSlot = PassIndex == 1u ? 20u : 19u;
    uint DestSlot = PassIndex == 0u ? 20u : 19u;
    SurfacePoint R = Receivers[Pixel];
    float4 CentreSample = OutputColour[ReplaySlot(SourceSlot, Pixel)];
    if (!ReplayScatteringReceiver(R))
    {
        if (PassIndex < 2u)
            OutputColour[ReplaySlot(DestSlot, Pixel)] = CentreSample;
        return;
    }
    bool AuthoredMaterials = (Extent.Primary.w & 134217728u) != 0;
    if (AuthoredMaterials && MaterialAddress(R) > 0 && MaterialTransport(R) == 2u)
    {
        if (PassIndex < 2u)
            OutputColour[ReplaySlot(DestSlot, Pixel)] = CentreSample;
        else
        {
            float3 GlazingEmission = all(isfinite(R.ResidualSpecular.xyz)) ? max(R.ResidualSpecular.xyz, 0.0) : 0.0;
            float GlazingAlpha = isfinite(OutputColour[Pixel].w) ? OutputColour[Pixel].w : 1.0;
            OutputColour[Pixel] = float4(max(CentreSample.xyz, 0.0) + GlazingEmission, GlazingAlpha);
        }
        return;
    }

    int2 Centre = int2(Pixel % Extent.History.x, Pixel / Extent.History.x);

    // 📝 Pre-filter Centre variance with a geometry-guarded 3x3 [1,2,1] kernel to suppress zero-variance pinholes.
    float FilteredVariance = 0.0;
    float VarianceWeightSum = 0.0;
    for (int VarY = -1; VarY <= 1; ++VarY)
        for (int VarX = -1; VarX <= 1; ++VarX)
        {
            int2 VPos = Centre + int2(VarX, VarY);
            if (any(VPos < 0) || any(VPos >= int2(Extent.History.xy))) continue;
            uint VIndex = uint(VPos.y) * Extent.History.x + uint(VPos.x);
            if (VarX != 0 || VarY != 0)
            {
                float PlaneRatio = 0.0, NormalCosine = 0.0;
                if (!ReplayVarianceCompatibleTap(R, Receivers[VIndex], length(float2(VarX, VarY)),
                                                 PlaneRatio, NormalCosine))
                    continue;
            }
            float KernelW = float((VarX == 0 ? 2 : 1) * (VarY == 0 ? 2 : 1));
            FilteredVariance += KernelW * max(OutputColour[ReplaySlot(SourceSlot, VIndex)].w, 0.0);
            VarianceWeightSum += KernelW;
        }
    FilteredVariance /= max(VarianceWeightSum, 1.0);

    bool RoughOpaque = (Extent.Primary.w & 64u) != 0;
    float Roughness = RoughOpaque
        ? (AuthoredMaterials ? clamp(ReplayEffectiveSpecularRoughness(R), 0.04, 1.0)
                             : clamp(R.NormalRoughness.w, 0.05, 1.0))
        : 1.0;
    float NormalPower = AuthoredMaterials && Roughness < 0.20
        ? lerp(384.0, 112.0, Roughness / 0.20)
        : lerp(128.0, 48.0, Roughness);
    float SigmaScale = AuthoredMaterials && Roughness < 0.20
        ? lerp(0.95, 2.30, Roughness / 0.20)
        : lerp(2.0, 3.5, Roughness);
    float ConeStepGate = (AuthoredMaterials && Step > 1u && Roughness < 0.20)
        ? exp(-float(Step - 1u) / max(0.25, Roughness * 14.0))
        : 1.0;
    float SigmaLuminance = SigmaScale * sqrt(max(0.0, FilteredVariance)) + 1e-4;
    float CentreLuminance = Power(CentreSample.xyz);

    float3 IrradianceSum = 0.0;
    float WeightSum = 0.0;
    float VarianceSum = 0.0;
    for (int TapY = -2; TapY <= 2; ++TapY)
        for (int TapX = -2; TapX <= 2; ++TapX)
        {
            float KernelWeight = float((TapX == 0 ? 6 : abs(TapX) == 1 ? 4 : 1) *
                                       (TapY == 0 ? 6 : abs(TapY) == 1 ? 4 : 1));
            if (TapX == 0 && TapY == 0)
            {
                IrradianceSum += KernelWeight * CentreSample.xyz;
                WeightSum += KernelWeight;
                VarianceSum += KernelWeight * KernelWeight * max(CentreSample.w, 0.0);
                continue;
            }
            int2 Offset = int2(TapX, TapY) * int(Step);
            int2 Q = Centre + Offset;
            if (any(Q < 0) || any(Q >= int2(Extent.History.xy))) continue;
            uint TapIndex = uint(Q.y) * Extent.History.x + uint(Q.x);
            SurfacePoint S = Receivers[TapIndex];
            float PlaneRatio = 0.0, NormalCosine = 0.0;
            if (!ReplayVarianceCompatibleTap(R, S, length(float2(Offset)), PlaneRatio, NormalCosine))
            {
                InterlockedAdd(Counters[17], 1);
                continue;
            }
            InterlockedAdd(Counters[16], 1);
            float4 TapSample = OutputColour[ReplaySlot(SourceSlot, TapIndex)];
            if (!all(isfinite(TapSample))) continue;
            float TapRoughness = AuthoredMaterials ? ReplayEffectiveSpecularRoughness(S) : S.NormalRoughness.w;
            float WeightPlane = exp(-2.0 * PlaneRatio);
            float WeightNormal = pow(max(NormalCosine, 0.0), NormalPower);
            float WeightLum = exp(-abs(Power(TapSample.xyz) - CentreLuminance) / SigmaLuminance);
            float WeightRough = RoughOpaque ? exp(-abs(Roughness - TapRoughness) / 0.15) : 1.0;
            float Weight = KernelWeight * ConeStepGate * WeightPlane * WeightNormal * WeightLum * WeightRough;
            IrradianceSum += Weight * max(TapSample.xyz, 0.0);
            WeightSum += Weight;
            VarianceSum += Weight * Weight * max(TapSample.w, 0.0);
        }

    float3 FilteredIrradiance = IrradianceSum / max(WeightSum, 1e-12);
    float PropagatedVariance = VarianceSum / max(WeightSum * WeightSum, 1e-12);
    if (PassIndex < 2u)
    {
        OutputColour[ReplaySlot(DestSlot, Pixel)] = float4(FilteredIrradiance, PropagatedVariance);
        return;
    }

    float3 Emission = all(isfinite(R.ResidualSpecular.xyz)) ? max(R.ResidualSpecular.xyz, 0.0) : 0.0;
    float Alpha = isfinite(OutputColour[Pixel].w) ? OutputColour[Pixel].w : 1.0;
    OutputColour[Pixel] = float4(FilteredIrradiance * ReplayVarianceDemodulator(R) + Emission, Alpha);
}
