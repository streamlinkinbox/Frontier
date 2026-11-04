//============================================================================================================================================
//                                                         VARIANCEUPSAMPLE.SLANG
//============================================================================================================================================
// 🧩 Joint bilateral projection of a resampling-resolution signal onto every screen pixel, guided by depth and normal.

#pragma once
#include "IntrinsicParity.slang"
#include "HostConstants.slang"

#include "RefreshDescriptor.slang"
#include "PackingCodec.slang"

  [[vk::binding(1, 0)]] Texture2D<float4>      SourceSignal;
  [[vk::binding(2, 0)]] Texture2D<float4>       SourceGuide;
  [[vk::binding(3, 0)]] Texture2D<float4> SurfaceAttributes;
  [[vk::binding(4, 0)]] Texture2D<float>      SurfaceDepth;
  [[vk::binding(5, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4>      ScreenTarget;

 
[shader("compute")]
[numthreads(8, 8, 1)]
void ProjectScreenSignal(uint3 Invocation : SV_DispatchThreadID)
{
    let Pixel = int2(Invocation.xy);
    if (any(float2(Pixel) >= Refresh.ScreenExtent))
    {
        return;
    }
    let Depth = LoadTexel(SurfaceDepth, Pixel, 0);
    if (Depth <= 0.0)
    {
        ScreenTarget[Pixel] = float4(0.0);
        return;
    }
    let ViewDepth = ResolveViewDepth(ReconstructWorldPosition(float2(Pixel) + 0.5, Depth));
    let Normal = DecodeOctahedral(LoadTexel(SurfaceAttributes, Pixel, 0).xy);
    let Extent = int2(TextureExtent(SourceSignal));
    let Position = float2(Pixel) / float(Refresh.TraceScale);
    let Corner = int2(floor(Position));
    let Fraction = Position - floor(Position);
    var Sum = float3(0.0);
    var WeightSum = 0.0;
    var Nearest = float3(0.0);
    var NearestScore = 1.0e30;
    for (var Tap = 0u; Tap < 4u; Tap++)
    {
        let Offset = int2(ToSigned(Tap & 1u), ToSigned(Tap >> 1u));
        let Texel = clamp(Corner + Offset, int2(0), Extent - int2(1));
        let Guide = LoadTexel(SourceGuide, Texel, 0);
        if (Guide.w < 0.5)
        {
            continue;
        }
        let Signal = LoadTexel(SourceSignal, Texel, 0).rgb;
        let DepthDifference = abs(Guide.z - ViewDepth) / max(ViewDepth, 1.0e-3);
        let Facing = max(dot(DecodeOctahedral(Guide.xy), Normal), 0.0);
        let Bilinear = SelectBranch(1.0 - Fraction.x, Fraction.x, Offset.x == 1) * SelectBranch(1.0 - Fraction.y, Fraction.y, Offset.y == 1);
        let Weight = (Bilinear + 1.0e-3) * exp(-DepthDifference * 40.0) * pow(Facing, 16.0);
        Sum += Signal * Weight;
        WeightSum += Weight;
        let Score = DepthDifference + (1.0 - Facing);
        if (Score < NearestScore)
        {
            NearestScore = Score;
            Nearest = Signal;
        }
    }
    let Projected = SelectBranch(Nearest, Sum / WeightSum, WeightSum > 1.0e-4);
    ScreenTarget[Pixel] = float4(Projected, 1.0);
}
