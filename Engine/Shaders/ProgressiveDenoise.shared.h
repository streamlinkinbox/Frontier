#ifndef FRONTIER_PROGRESSIVE_DENOISE_SHARED_H
#define FRONTIER_PROGRESSIVE_DENOISE_SHARED_H
// Shared C++/GLSL policy. Motion history is bounded to 32 previous samples, then
// ResolveSurface adds one. Keep young/reprojected pixels fully denoised; allow
// stationary valid history to progressively reveal the unfiltered running mean.
// NaN, negative and empty histories fail closed to full filtering.
//
// PER LEVEL, not one weight for the whole chain. The five à-trous levels do not cost the same amount of
// detail: level 0 mixes the eight neighbours one pixel away, level 4 mixes neighbours THIRTY-TWO pixels away.
// At equal blend weight the wide levels are what makes a held frame look soft, and they are also the levels
// whose job finishes first — the variance of the mean falls as 1/n, so the low-frequency noise they exist to
// remove is gone long before the pixel-scale noise is. Raising the base fade to the power (1 + log2(step))
// retires them in that order: the same ordering SVGF derivatives use when they drop levels as history grows,
// expressed as a continuous weight instead of a level count so nothing pops on the frame a level is dropped.
//
// Below the bound every level keeps full strength, so a young or freshly disoccluded pixel is filtered exactly
// as before. This is a presentation policy, not a measured convergence criterion; what IS measured is its
// outcome — Tools/Build/CheckProgressiveDenoise.sh reports the surviving detail bias per sample count.
float ProgressiveDenoiseStrength(float ValidSamples, uint StepSize)
{
    // Negated so NaN takes the full-filtering branch.
    if (!(ValidSamples > 33.0)) return 1.0;
    float Base     = 33.0 / ValidSamples;
    float Strength = Base;
    for (uint Span = StepSize; Span > 1u; Span >>= 1u) Strength *= Base;   // exponent = 1 + log2(StepSize)
    return Strength;
}
#endif
