//============================================================================================================================================
// 📦 Engine/Shaders/ShadowSample.slang — The three shadow techniques, sampled from the maps (include only)
//============================================================================================================================================
// 🧩 This is the GPU statement of the shadow ladder the quality tiers select between. It is the counterpart of
//    VisibilityRaster::Shadow on the CPU (Engine/GeometricRaster/VisibilityRaster.cpp) and the two are written to
//    give the same answer for the same inputs — the CPU one is what the headless proof gates, this one is what the
//    application runs.
//
//    kShadowFilterHard — one comparison against the map. Binary and aliased; the Minimal tier's shadow.
//
//    kShadowFilterPcf  — a box of comparisons of fixed radius. The mean is the lit fraction, so the edge becomes a
//       ramp. The ramp's WIDTH is constant, which is the giveaway: a real penumbra widens with the distance between
//       occluder and receiver, and this one does not. Cheap, stable, deliberately not physical.
//
//    kShadowFilterPcss — true percentage-closer soft shadows, in two steps:
//       ① Blocker search. Over a window scaled by the light's apparent size, average the depths of every texel
//          nearer to the light than the receiver. That mean occluder depth — not the nearest — is what the penumbra
//          relation wants, since overlapping occluders would otherwise over-soften.
//       ② Penumbra estimate by similar triangles:  w = (Receiver − Blocker) / Blocker × LightSize.
//          At contact the numerator vanishes and the kernel collapses to a hard test, so contact stays sharp; far
//          from the occluder the kernel opens and the shadow softens. That is contact hardening, and it is why this
//          sits at the top of the ladder.
//
// Depth convention: the maps hold ordinary depth (near→0, far→1); everything here converts to metres through
//    ShadowLinearDepth first, so biases and penumbrae are in world units and mean the same thing at every depth.

#ifndef FRONTIER_SHADOW_SAMPLE
#define FRONTIER_SHADOW_SAMPLE

#include "Shaders/ShadowRecords.slang"

// The maps: one 2D layer per light tap, sampled as plain depth (no comparison sampler — the filters need the value
//    itself for the blocker search, not a hardware-compared 0/1).
layout(set = 1, binding = 1) uniform sampler2DArray ShadowMaps;

// One texel's linear depth in metres; a point off the map reports "nothing occluding" so a receiver outside the
//    light's frustum is lit, which is the usual convention.
float ShadowTexelDepth(vec2 Texel, uint Tap)
{
    float Side = ShadowGeometry.x;
    if (Texel.x < 0.0 || Texel.y < 0.0 || Texel.x >= Side || Texel.y >= Side) return 1e30;
    float Stored = texelFetch(ShadowMaps, ivec3(ivec2(Texel), int(Tap)), 0).r;
    return ShadowLinearDepth(Stored, ShadowGeometry.y, ShadowGeometry.z);
}

// PCSS step one: the mean depth of the texels nearer than the receiver. Returns false when the window finds no
//    occluder at all, which means the receiver is fully lit and no kernel need be paid for.
bool ShadowBlockerDepth(vec2 Texel, float Receiver, float Bias, float Radius, uint Tap, out float Blocker)
{
    float Step = Radius <= 0.0 ? 1.0 : Radius * 2.0 / float(kShadowBlockerTaps - 1u);
    float Half = max(Radius, 0.0);
    float Sum = 0.0;
    uint  Count = 0u;
    for (uint Y = 0u; Y < kShadowBlockerTaps; ++Y)
    {
        for (uint X = 0u; X < kShadowBlockerTaps; ++X)
        {
            vec2  At = Texel - vec2(Half) + Step * vec2(float(X), float(Y));
            float D  = ShadowTexelDepth(At, Tap);
            if (D + Bias < Receiver) { Sum += D; ++Count; }
        }
    }
    Blocker = Count == 0u ? 0.0 : Sum / float(Count);
    return Count != 0u;
}

// The percentage-closer box shared by all three techniques: hard is this with one tap, PCF a fixed radius, PCSS a
//    penumbra-derived one. Returns the lit fraction in [0,1].
float ShadowFilterLit(vec2 Texel, float Receiver, float Bias, float Radius, uint Taps, uint Tap)
{
    if (Taps <= 1u || Radius <= 0.0)
        return ShadowTexelDepth(Texel, Tap) + Bias < Receiver ? 0.0 : 1.0;

    float Step = Radius * 2.0 / float(Taps - 1u);
    float Lit  = 0.0;
    for (uint Y = 0u; Y < Taps; ++Y)
    {
        for (uint X = 0u; X < Taps; ++X)
        {
            vec2 At = Texel - vec2(Radius) + Step * vec2(float(X), float(Y));
            if (!(ShadowTexelDepth(At, Tap) + Bias < Receiver)) Lit += 1.0;
        }
    }
    return Lit / float(Taps * Taps);
}

// The lit fraction at a world position for one light tap, under whichever technique the tier selected.
//    Normal is the geometric normal; the sample leaves from the surface nudged along it, because without that nudge
//    a grazing surface lands inside its own map texels and peppers itself with acne.
float ShadowLitFraction(vec3 World, vec3 Normal, float NdotL, uint Tap)
{
    const float kNormalOffset = 0.02;   // [m] along the geometric normal, ahead of the map test

    vec4 Clip = ShadowLightClip[Tap] * vec4(World + Normal * kNormalOffset, 1.0);
    if (Clip.w <= 0.0) return 1.0;                    // behind the light
    vec3 Ndc = Clip.xyz / Clip.w;
    vec2 Uv  = Ndc.xy * 0.5 + 0.5;
    if (any(lessThan(Uv, vec2(0.0))) || any(greaterThanEqual(Uv, vec2(1.0)))) return 1.0;   // outside: no information

    float Side     = ShadowGeometry.x;
    vec2  Texel    = Uv * Side - 0.5;
    float Receiver = ShadowLinearDepth(Ndc.z, ShadowGeometry.y, ShadowGeometry.z);
    float Bias     = ShadowGeometry.w * (1.0 + (1.0 - NdotL));   // slope-scaled, as on the CPU path

    uint Kind = ShadowControl.x;
    uint Taps = max(ShadowControl.y, 1u);

    if (Kind == kShadowFilterHard)
        return ShadowFilterLit(Texel, Receiver, Bias, 0.0, 1u, Tap);

    // PCF's radius is a fixed count of texels, so it needs no projection maths at all — it returns before the
    //    texels-per-metre conversion below, which only PCSS consumes.
    if (Kind == kShadowFilterPcf)
        return ShadowFilterLit(Texel, Receiver, Bias, float(Taps) * 0.5, Taps, Tap);

    // Texels per metre at the receiver's depth: the frustum is 2·z·tan(half) metres across Side texels.
    //
    // ⚠️ This used to recover the half-angle as 1.0 / ShadowLightClip[Tap][1][1], on the reasoning that a value read
    //    back from the matrix "cannot drift apart" from it. That is true of a bare PROJECTION matrix, where [1][1]
    //    is exactly ±1/tan(half). ShadowLightClip is Projection · View, so the element also carries the light's own
    //    basis — it is (−1/tanHalf)·Up.y — and the recovery is only right when the light happens to look along an
    //    axis that leaves Up.y = ∓1. Measured over six orientations (Scratchpad/ShadowMatrixProof.cpp) every one was
    //    wrong: a ceiling lamp aimed straight down recovered 1e6 instead of 2.14, because Up.y is 0 there and the
    //    clamp floors the divisor — a penumbra 466,000× too wide, i.e. PCSS destroyed in the most ordinary setup
    //    there is. The tangent is now carried explicitly beside the tap it belongs to.
    float TanHalf        = max(ShadowTapNormal[Tap].w, 1e-6);
    float TexelsPerMetre = Side / max(2.0 * Receiver * TanHalf, 1e-6);

    // PCSS.
    float LightSize    = ShadowTapOrigin[Tap].w;
    float SearchTexels = max(LightSize * TexelsPerMetre * 0.5, 1.0);
    float Blocker;
    if (!ShadowBlockerDepth(Texel, Receiver, Bias, SearchTexels, Tap, Blocker)) return 1.0;
    if (Blocker <= 0.0) return ShadowFilterLit(Texel, Receiver, Bias, 1.0, Taps, Tap);

    float Penumbra = (Receiver - Blocker) / Blocker * LightSize;
    float Radius   = max(Penumbra * TexelsPerMetre * 0.5, 0.5);

    return ShadowFilterLit(Texel, Receiver, Bias, Radius, Taps, Tap);
}

#endif // FRONTIER_SHADOW_SAMPLE
