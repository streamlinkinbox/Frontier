#ifndef FRONTIER_PROGRESSIVE_DENOISE_SHARED_H
#define FRONTIER_PROGRESSIVE_DENOISE_SHARED_H
// Shared C++/GLSL policy. Motion history is bounded to 32 previous samples, then
// ResolveSurface adds one. Keep young/reprojected pixels fully denoised; allow
// stationary valid history to progressively reveal the unfiltered running mean.
// NaN, negative and empty histories fail closed to full filtering.
float ProgressiveDenoiseStrength(float ValidSamples)
{
    return ValidSamples > 33.0 ? 33.0 / ValidSamples : 1.0;
}
#endif
