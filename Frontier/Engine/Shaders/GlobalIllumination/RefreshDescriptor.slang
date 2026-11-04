//============================================================================================================================================
//                                                        REFRESHDESCRIPTOR.SLANG
//============================================================================================================================================
// 🧩 The refresh profile at group 0, slot 0 — every program that reads the camera, the light or the toggles includes this.

#pragma once
#include "IntrinsicParity.slang"

#include "SceneLayouts.slang"

  [[vk::binding(0, 0)]] ConstantBuffer<RefreshConstants> Refresh;

// 📝 Reconstructs the world position of a screen pixel centre from reversed depth through the jittered inverse.
float3 ReconstructWorldPosition(float2 Pixel, float Depth)
{
    let Ndc = float2(Pixel.x * Refresh.InverseScreenExtent.x * 2.0 - 1.0, 1.0 - Pixel.y * Refresh.InverseScreenExtent.y * 2.0);
    let Clip = mul(float4(Ndc, Depth, 1.0), Refresh.InverseViewProjection);
    return Clip.xyz / Clip.w;
}

// 📝 Linear view distance along −Z of a world position; used by the similarity tests of every reprojection.
float ResolveViewDepth(float3 Position)
{
    return -(mul(float4(Position, 1.0), Refresh.View)).z;
}

// 📝 View distance the same world point had in the previous refresh: w of its previous unjittered clip position.
float ResolvePriorViewDepth(float3 Position)
{
    return (mul(float4(Position, 1.0), Refresh.PreviousUnjitteredViewProjection)).w;
}

// 📝 A prior guide depth agrees when it matches either where this point was last refresh (a static surface under a
//    moving camera) or where it is now (a surface riding with the camera, such as the chased vehicle).
bool ClassifyPriorDepth(float PriorDepth, float ExpectedDepth, float ViewDepth)
{
    return abs(PriorDepth - ExpectedDepth) < 0.08 * ExpectedDepth + 0.03 || abs(PriorDepth - ViewDepth) < 0.08 * ViewDepth + 0.03;
}
