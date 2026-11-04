//============================================================================================================================================
//                                                       DISCREPANCYSEQUENCE.SLANG
//============================================================================================================================================
// 🧩 Per-pixel sample streams — a PCG hash for decorrelation and the R2 additive recurrence for temporal stratification.

#pragma once
#include "IntrinsicParity.slang"

// 📝 PCG-RXS-M-XS 32-bit permutation (O'Neill 2014), used as an integer hash.
uint PermuteUnsigned(uint Input)
{
    let Advanced = Input * 747796405u + 2891336453u;
    let Word = ((Advanced >> ((Advanced >> 28u) + 4u)) ^ Advanced) * 277803737u;
    return (Word >> 22u) ^ Word;
}

uint ConstructPixelSeed(uint2 Pixel, uint RefreshIndex, uint Stream)
{
    return PermuteUnsigned(Pixel.x + PermuteUnsigned(Pixel.y + PermuteUnsigned(RefreshIndex + PermuteUnsigned(Stream))));
}

float AdvanceUniform(inout uint Seed)
{
    Seed = PermuteUnsigned(Seed);
    return float(Seed >> 8u) * (1.0 / 16777216.0);
}

// 📝 R2 sequence (Roberts 2018): x_n = frac(s + n·(1/φ₂, 1/φ₂²)), φ₂ the plastic constant. A per-pixel hashed start
//    turns it into a Cranley–Patterson rotated stream, so each pixel walks a stratified sequence over refreshes.
float2 ProjectTemporalPair(uint2 Pixel, uint RefreshIndex, uint Stream)
{
    let Start = float2(float(PermuteUnsigned((Pixel.x * 73856093u) ^ (Pixel.y * 19349663u) ^ (Stream * 83492791u)) >> 8u),
                      float(PermuteUnsigned((Pixel.y * 73856093u) ^ (Pixel.x * 19349663u) ^ (Stream * 2654435761u)) >> 8u))
                * (1.0 / 16777216.0);
    let Step = float2(0.7548776662466927, 0.5698402909980532);
    return frac(Start + float(RefreshIndex % 65536u) * Step);
}

// 📝 Cosine-weighted hemisphere direction about +Z (Malley's method), pdf = cosθ / π.
float3 ProjectCosineHemisphere(float2 Pair)
{
    let Radius = sqrt(Pair.x);
    let φ = 2.0 * 3.14159265358979 * Pair.y;
    return float3(Radius * cos(φ), Radius * sin(φ), sqrt(max(0.0, 1.0 - Pair.x)));
}

// 📝 Uniform disk sample (concentric would be lower distortion; uniform suffices for reuse offsets).
float2 ProjectUniformDisk(float2 Pair)
{
    let Radius = sqrt(Pair.x);
    let φ = 2.0 * 3.14159265358979 * Pair.y;
    return float2(cos(φ), sin(φ)) * Radius;
}
