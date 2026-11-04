//============================================================================================================================================
//                                                         PENUMBRARESOLVE.SLANG
//============================================================================================================================================
// 🧩 The ray-traced sun's penumbra filtered across its own width: one shadow ray per pixel and refresh leaves a binary verdict, so a
//    penumbra a few pixels wide flickers; this averages the verdicts over a disc the size of that penumbra (RefinementPenumbraFilter).

#pragma once
#include "IntrinsicParity.slang"
#include "HostConstants.slang"

// 📝 The sun subtends a cone of half-angle θ ≈ 0.0047 rad, so a receiver d metres past its occluder lies in a penumbra of half-width
//    d·tan θ. ① A 5×5 search (stride growing with the screen height) finds the blocker travel the direct pass stored and exits where
//    every verdict agrees — most pixels. ② Half that half-width over the pixel footprint gives the filter radius, 0.75–8 px. ③ Sixteen taps
//    on a Vogel disc, weighted by the receiver plane and normal, average the verdicts; with RefinementStratifiedSun each 4×4 block
//    already covers the disc, so the average converges in one refresh. ④ The direct radiance changes by sun × (filtered − own verdict).

#include "RefreshDescriptor.slang"
#include "PackingCodec.slang"
#include "DiscrepancySequence.slang"

  [[vk::binding(1, 0)]] Texture2D<float4>        SurfaceAttributes;
  [[vk::binding(2, 0)]] Texture2D<float>             SurfaceDepth;
  [[vk::binding(3, 0)]] Texture2D<float4>          PenumbraSignal;
  [[vk::binding(4, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4>        DirectTarget;

static const float PenumbraTangent     = 0.0047;   // [-] tan θ of the sun's angular radius (RayIntersection.slang SolarAngularRadius)
static const uint  PenumbraTaps        = 16u;
// 📝 The filter disc's radius as a share of the penumbra half-width d·tan θ: 0.5 (a quarter of the penumbra) measured closer to the
//    converged shadow than 1.0, whose disc as wide as the penumbra blurs its profile (Exhibits/RayTracedLighting).
static const float PenumbraRadiusShare = 0.5;

[shader("compute")]
[numthreads(8, 8, 1)]
void ResolvePenumbra(uint3 Invocation : SV_DispatchThreadID)
{
    let Pixel = int2(Invocation.xy);
    if (any(float2(Pixel) >= Refresh.ScreenExtent))
    {
        return;
    }
    let Depth = LoadTexel(SurfaceDepth, Pixel, 0);
    if (Depth <= 0.0)
    {
        return;
    }
    let Limit = int2(Refresh.ScreenExtent) - int2(1);
    let Centre = LoadTexel(PenumbraSignal, Pixel, 0);
    let Own = SelectBranch(0.0, 1.0, Centre.a < 0.0);

    // ① blocker search
    let Stride = max(1, ToSigned(Refresh.ScreenExtent.y / 270.0 + 0.5));
    var Unoccluded = 0u;
    var Occluded = 0u;
    var BlockerTravel = 0.0;
    for (var Y = -2; Y <= 2; Y++)
    {
        for (var X = -2; X <= 2; X++)
        {
            let Tap = LoadTexel(PenumbraSignal, clamp(Pixel + int2(X, Y) * Stride, int2(0), Limit), 0).a;
            if (Tap < 0.0)
            {
                Unoccluded++;
            }
            else
            {
                Occluded++;
                BlockerTravel += Tap;
            }
        }
    }
    if (Unoccluded == 0u || Occluded == 0u)
    {
        return;
    }

    // ② penumbra half-width over the pixel footprint
    let Position = ReconstructWorldPosition(float2(Pixel) + 0.5, Depth);
    let Footprint = max(length(ReconstructWorldPosition(float2(Pixel) + float2(1.5, 0.5), Depth) - Position), 1.0e-4);
    let Radius = clamp(BlockerTravel / float(Occluded) * PenumbraTangent * PenumbraRadiusShare / Footprint, 0.75, 8.0);
    let Normal = DecodeOctahedral(LoadTexel(SurfaceAttributes, Pixel, 0).xy);
    let PlaneTolerance = 0.02 * ResolveViewDepth(Position) + 0.01;

    // ③ Vogel disc, rotated per pixel
    let Rotation = float(PermuteUnsigned(ToUnsigned(Pixel.x) * 73856093u ^ ToUnsigned(Pixel.y) * 19349663u) >> 8u) * (6.2831853 / 16777216.0);
    var Sum = Own;
    var WeightSum = 1.0;
    for (var Tap = 0u; Tap < PenumbraTaps; Tap++)
    {
        let Angle = float(Tap) * 2.3999632 + Rotation;
        let Offset = float2(cos(Angle), sin(Angle)) * (Radius * sqrt((float(Tap) + 0.5) / float(PenumbraTaps)));
        let TapPixel = clamp(Pixel + int2(round(Offset)), int2(0), Limit);
        let TapDepth = LoadTexel(SurfaceDepth, TapPixel, 0);
        if (TapDepth <= 0.0)
        {
            continue;
        }
        let TapPosition = ReconstructWorldPosition(float2(TapPixel) + 0.5, TapDepth);
        let TapNormal = DecodeOctahedral(LoadTexel(SurfaceAttributes, TapPixel, 0).xy);
        let Weight = exp(-abs(dot(TapPosition - Position, Normal)) / PlaneTolerance) * pow(max(dot(TapNormal, Normal), 0.0), 8.0);
        Sum += Weight * SelectBranch(0.0, 1.0, LoadTexel(PenumbraSignal, TapPixel, 0).a < 0.0);
        WeightSum += Weight;
    }

    // ④ the corrected direct radiance
    let Filtered = Sum / WeightSum;
    let Direct = DirectTarget[Pixel];
    DirectTarget[Pixel] = float4(Direct.rgb + Centre.rgb * (Filtered - Own), Direct.a);
}
