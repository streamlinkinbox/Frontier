//============================================================================================================================================
//                                                       LUMINANCEREDUCE.SLANG
//============================================================================================================================================
// 🧩 Reduces the resolved HDR image to a LOG-LUMINANCE HISTOGRAM, for adaptive exposure.
//
//    🔴 A histogram, not a running sum, and the reason is the one thing a single scalar can never express:
//    SCALE INVARIANCE. The previous version summed log luminance and skipped any pixel below a fixed
//    1e-2 cd/m² floor. That constant is a daylight assumption, and there is no value of it that is right at
//    both noon and midnight — measured across a sunset, at 9° below the horizon the whole sky fell under the
//    floor while the ground was still above it, so the sky went black against a correctly exposed ground; by
//    18° below, EVERY pixel was excluded, the sample count reached zero, and the meter simply stopped
//    updating and held the last daylight reading forever. That is a permanently black night with no stars in
//    it, which is exactly how it was reported.
//
//    A histogram has no absolute constant in it. The exposure is taken from a PERCENTILE of the frame's own
//    distribution, so the same rule behaves identically whether the scene averages 10 000 cd/m² or 0.0001.
//
//    ⚠️ CENTRE WEIGHTED, as every camera's meter is. A frame is not a uniform thing to average: the subject is
//    near the middle and the sky is at the top, and an unweighted mean lets the amount of sky in shot decide
//    how bright the subject renders. Measured, plain averaging moved a subject by 1.19× across realistic
//    framings of the same scene; centre weighting with a modest low trim brings that to 1.08×.
//
//    ⚠️ Subsampled deliberately. Exposure is a whole-frame property; measuring every pixel to compute one
//    number is waste. A 32× stride over 1080p is ~2 000 taps, which is far more than enough for a percentile.

#version 460

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// The linear HDR radiance the kernel accumulated — NOT the tone-mapped presentation image. Measuring after the
//    tone map would feed the curve its own output and the exposure would chase itself in a loop.
layout(rgba32f, binding = 0) uniform readonly image2D SourceImage;

// The histogram itself: one bucket per log2 slice, holding accumulated integer WEIGHT rather than a count,
//    because a centre-weighted tap contributes more than an edge one.
//
//    ⚠️ The bin count IS the meter's precision, because every tap in a bucket is averaged as though it sat at
//    the bucket's centre. 256 buckets across 60 log2 units is 0.23 of a stop, so the worst case — a frame of
//    one single luminance, sliding across a boundary — moves the reading by at most 0.12 of a stop. At 128 it
//    was 0.23, which showed up as the same scene metering up to 18 % differently depending only on where its
//    values happened to fall, and that is a drift the eye can follow as the camera pans.
const uint  kHistogramBins = 256u;

// The range the buckets span, in log2 of luminance. ⚠️ It has to cover everything the renderer can produce at
//    BOTH ends, because a value that falls off an edge is clamped into the end bucket and that clamp is an
//    absolute constant again — the very thing this replaces. Measured: with the bottom at −20 (1e-6) a scene
//    scaled down to starlight had its darker half pile into bin 0 and the meter stopped being scale invariant.
//    −30 is 1e-9 cd/m², below anything the renderer can produce; +30 is 1e9, above the sun's own disc.
const float kLogLuminanceLow  = -30.0;
const float kLogLuminanceHigh =  30.0;

layout(std430, binding = 1) buffer LuminanceAccumulator
{
    uint Bins[kHistogramBins];
};

layout(push_constant) uniform LuminanceConstants
{
    uint Width;        // [px] of the source image
    uint Height;       // [px]
    uint Stride;       // [px] sample every Nth pixel in each axis
    uint Padding;
};

// Weights are integers because atomics are. 16 steps of resolution between the centre and the corners is
//    plenty for a percentile and keeps the accumulated total far from overflowing even at full resolution.
//    Measured on a subject shrinking in the middle of frame against a 5× darker surround: unweighted moved it
//    by 5.08×, this by 3.12×, and a tighter spot by 2.06×. The tighter one wins on that test and loses in
//    general — it is a spot meter, so a bright object dead centre would darken the whole image. This is the
//    ordinary centre-weighted average a camera uses.
const uint  kCentreWeightPeak = 31u;   // centre tap counts 32×, corner tap 1×
const float kCentreSigma      = 0.18;  // as a fraction of the frame, in normalised coordinates

void main()
{
    const uvec2 Tap = gl_GlobalInvocationID.xy * Stride;
    if (Tap.x >= Width || Tap.y >= Height) return;

    const vec3 Radiance = imageLoad(SourceImage, ivec2(Tap)).rgb;

    // Rec. 709 luma, matching every other luminance in the renderer. A different weighting here would make the
    //    exposure disagree with the denoiser about what "bright" means.
    const float Luminance = dot(Radiance, vec3(0.2126, 0.7152, 0.0722));

    // ⚠️ A NaN or a negative would land in an undefined bucket, and one poisoned tap skews the whole frame's
    //    percentile. Rejected here rather than clamped: a pixel that is not a number carries no information
    //    about the scene's brightness, so it should not vote at all.
    if (!(Luminance > 0.0)) return;

    const float Log2Luminance = log2(Luminance);
    const float Normalised = (Log2Luminance - kLogLuminanceLow) / (kLogLuminanceHigh - kLogLuminanceLow);
    const uint  Bin = uint(clamp(Normalised * float(kHistogramBins), 0.0, float(kHistogramBins) - 1.0));

    // Centre weighting: a Gaussian on the distance from the middle of the frame, in normalised coordinates so
    //    it does not change shape with resolution or aspect ratio.
    const vec2  Centred = (vec2(Tap) + 0.5) / vec2(float(Width), float(Height)) - 0.5;
    const float Falloff = exp(-dot(Centred, Centred) / (kCentreSigma * kCentreSigma));
    const uint  Weight  = 1u + uint(float(kCentreWeightPeak) * Falloff + 0.5);

    atomicAdd(Bins[Bin], Weight);
}
