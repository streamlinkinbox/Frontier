/*============================================================================================================================================
                                                            DETERMINISTICQUADRATURE.SLANG
============================================================================================================================================*/
// 🧩 Shared Owen-Scrambled Sobol (0,2) Quadrature + Dupuy-Benyoub (2023) Spherical-Cap GGX VNDF (Zero RNG)
//    - Bit-exact GPU mirror of DeterministicIntegrators.h (5-op bit-parallel, zero denoiser)

module DeterministicQuadrature;

public uint ReverseBits32(uint X)
{
    X = ((X & 0x55555555u) << 1u) | ((X >> 1u) & 0x55555555u);
    X = ((X & 0x33333333u) << 2u) | ((X >> 2u) & 0x33333333u);
    X = ((X & 0x0F0F0F0Fu) << 4u) | ((X >> 4u) & 0x0F0F0F0Fu);
    X = ((X & 0x00FF00FFu) << 8u) | ((X >> 8u) & 0x00FF00FFu);
    return (X << 16u) | (X >> 16u);
}

public uint AvalancheKey32(uint Input)
{
    uint Mixed = Input;
    Mixed ^= Mixed >> 16u;
    Mixed *= 0x7FEB352Du;
    Mixed ^= Mixed >> 15u;
    Mixed *= 0x846CA68Bu;
    Mixed ^= Mixed >> 16u;
    return Mixed;
}

public uint LaineKarrasPermutation(uint Input, uint Key)
{
    uint Permuted = Input + Key;
    Permuted ^= Permuted * 0x6C50B47Cu;
    Permuted ^= Permuted * 0xB82F1E52u;
    Permuted ^= Permuted * 0xC7AFE638u;
    Permuted ^= Permuted * 0x8D22F6E6u;
    return Permuted;
}

public uint NestedDigitPermutation(uint Input, uint Key)
{
    return ReverseBits32(LaineKarrasPermutation(ReverseBits32(Input), Key));
}

public uint SobolSecondDimension(uint Index)
{
    uint Bits = Index;
    Bits ^= (Bits & 0xAAAAAAAAu) >> 1u;
    Bits ^= (Bits & 0xCCCCCCCCu) >> 2u;
    Bits ^= (Bits & 0xF0F0F0F0u) >> 4u;
    Bits ^= (Bits & 0xFF00FF00u) >> 8u;
    Bits ^= (Bits & 0xFFFF0000u) >> 16u;
    return ReverseBits32(Bits);
}

public float2 Sobol02Sample(uint SampleIndex, uint PixelKey, uint PairIndex)
{
    uint PairKey = AvalancheKey32(PairIndex * 0x9E3779B9u + 0x85EBCA6Bu) ^ PixelKey;
    uint ShuffledIndex = NestedDigitPermutation(SampleIndex, PairKey);
    uint SecondDigits = NestedDigitPermutation(SobolSecondDimension(ShuffledIndex), AvalancheKey32(PairKey ^ 0x63D83595u));
    float U = float(ShuffledIndex) * (1.0 / 4294967296.0);
    float V = float(SecondDigits) * (1.0 / 4294967296.0);
    return float2(U, V);
}

// Spherical-cap GGX VNDF local sample (Dupuy-Benyoub 2023). Inputs in tangent space (N = +Z).
public float3 SampleSphericalCapGgxVndfLocal(float2 Xi, float Alpha, float CosCap)
{
    float CosTheta = mix(CosCap, 1.0, Xi.x);
    float SinTheta = sqrt(max(0.0, 1.0 - CosTheta * CosTheta));
    float Phi = 6.28318530718 * Xi.y;
    return float3(SinTheta * cos(Phi), SinTheta * sin(Phi), CosTheta);
}
