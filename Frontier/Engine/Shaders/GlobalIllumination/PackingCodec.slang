//============================================================================================================================================
//                                                           PACKINGCODEC.SLANG
//============================================================================================================================================
// 🧩 Octahedral unit-vector encoding and half-precision packing for reservoirs and surface attributes.

#pragma once
#include "IntrinsicParity.slang"

// 📝 Octahedral projection (Cigolle et al. 2014): the unit sphere folded onto the [−1, 1]² square.
float2 EncodeOctahedral(float3 Normal)
{
    let Projected = Normal.xy / (abs(Normal.x) + abs(Normal.y) + abs(Normal.z));
    if (Normal.z >= 0.0)
    {
        return Projected;
    }
    let Folded = (1.0 - abs(Projected.yx)) * SelectBranch(float2(-1.0), float2(1.0), Projected >= float2(0.0));
    return Folded;
}

float3 DecodeOctahedral(float2 Encoded)
{
    var Normal = float3(Encoded.x, Encoded.y, 1.0 - abs(Encoded.x) - abs(Encoded.y));
    let Fold = max(-Normal.z, 0.0);
    Normal.x += SelectBranch(Fold, -Fold, Normal.x >= 0.0);
    Normal.y += SelectBranch(Fold, -Fold, Normal.y >= 0.0);
    return normalize(Normal);
}

uint PackOctahedral(float3 Normal)
{
    return PackSnormPair(EncodeOctahedral(Normal));
}

float3 UnpackOctahedral(uint Packed)
{
    return DecodeOctahedral(UnpackSnormPair(Packed));
}

uint2 PackHalfTriple(float3 Triple, float Fourth)
{
    return uint2(PackHalfPair(Triple.xy), PackHalfPair(float2(Triple.z, Fourth)));
}

float4 UnpackHalfTriple(uint2 Packed)
{
    return float4(UnpackHalfPair(Packed.x), UnpackHalfPair(Packed.y));
}
