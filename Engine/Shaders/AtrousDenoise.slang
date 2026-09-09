//============================================================================================================================================
// 📦 Engine/Shaders/AtrousDenoise.slang — R7: edge-avoiding à-trous wavelet filter over the accumulated radiance
//============================================================================================================================================
// One dispatch per wavelet level. Level i reads the previous level and writes the next, sampling a 5×5 neighbourhood
//    whose taps are spaced 2^i pixels apart. Five levels therefore reach a 81×81 pixel footprint for the cost of
//    5 × 25 taps instead of 6561, which is the entire point of the à-trous ("with holes") formulation.
//
// The filter is EDGE-AVOIDING: each tap is weighted by how much it agrees with the centre pixel, so the blur runs
//    along surfaces and stops at silhouettes. Three weights multiply together (Dammertz et al. 2010, and the
//    geometry/luminance terms from SVGF, Schied et al. 2017):
//
//      w_normal    = max(0, n·n_tap)^σn         — a tap on a differently-oriented surface contributes nothing
//      w_depth     = exp(-|z - z_tap| / (σz·|∇z| + ε))   — separates surfaces that overlap in screen space
//      w_luminance = exp(-|l - l_tap| / (σl·√(g·Var) + ε)) — preserves genuine lighting features
//
// The luminance term is what separates this from a bilateral blur: dividing by the estimated standard deviation
//    means a difference that is small RELATIVE TO THE NOISE is smoothed, while a difference much larger than the
//    noise is a real edge and is preserved. Where variance is high (few samples) the filter is permissive and
//    blurs hard; where the estimate has converged it barely touches the image. That is the property that lets the
//    denoiser fade out as the running mean converges rather than permanently softening a still frame.
//
// Variance is carried in the alpha channel and filtered alongside the colour with the SQUARE of the colour weights,
//    which is the correct propagation for a weighted mean of independent samples.
//
// NOTE: GLSL source lowered by glslc -fshader-stage=compute.

#version 460
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

//------------------------------------------------------------------------------------------------------------------------
//                                                       BINDINGS
//------------------------------------------------------------------------------------------------------------------------
// A private descriptor set. The ReSTIR kernel's set is full to its variable-count texture array, and a filter that
//    only needs three images has no business being bolted onto it.

layout(set = 0, binding = 0, rgba32f) uniform readonly  image2D SourceImage;    // xyz = radiance, w = variance
layout(set = 0, binding = 1, rgba32f) uniform writeonly image2D TargetImage;    // xyz = radiance, w = variance
layout(set = 0, binding = 2, rgba16f) uniform readonly  image2D SurfaceImage;   // xyz = normal, w = depth (≤ 0 = none)
layout(set = 0, binding = 3, rgba8)   uniform writeonly image2D OutputImage;   // tone-mapped presentation image (final level only)

layout(push_constant) uniform DenoiseConstants
{
    uvec2 Extent;             // [px]  image size
    uint  StepSize;           // [px]  tap spacing for this level: 1, 2, 4, 8, 16
    uint  Enabled;            // [-]   0 = straight copy, so the pass is an identity switch

    float NormalPower;        // [-]   σn — exponent on the normal agreement
    float DepthScale;         // [-]   σz — relative depth tolerance
    float LuminanceScale;     // [-]   σl — how many standard deviations count as "the same"
    float Exposure;           // [-]   applied by the final level's tone map

    uint  FinalLevel;         // [-]   1 = also tone-map into OutputImage
    // A7d. Must match the kernel's: with the denoiser on, THIS is the tone map the viewer sees, and a
    //    saturation applied in only one of the two would change colour when the denoiser was toggled.
    float ColourSaturation;   // [-]   1 in daylight, 0 under starlight
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       HELPERS
//------------------------------------------------------------------------------------------------------------------------

float Luminance(vec3 Colour)
{
    return dot(Colour, vec3(0.2126, 0.7152, 0.0722));
}

// Identical to ReSTIRViewport.slang's tone map — the two must agree, because with the denoiser off the kernel
//    writes the presentation image itself and toggling the filter must not change the grade.
float AcesFilm(float x)
{
    float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 ToneMap(vec3 Hdr)
{
    // A7d, identical to the kernel's: below about 3 cd/m² the eye loses colour, and rendering full saturation
    //    there turns a faint pre-dawn glow into a lurid orange band.
    const float Grey = dot(Hdr, vec3(0.2126, 0.7152, 0.0722));
    Hdr = mix(vec3(Grey), Hdr, clamp(ColourSaturation, 0.0, 1.0));
    Hdr *= Exposure;
    return pow(vec3(AcesFilm(Hdr.r), AcesFilm(Hdr.g), AcesFilm(Hdr.b)), vec3(1.0 / 2.2));
}

// The 5-tap B₃ spline kernel, separable: (1/16, 1/4, 3/8, 1/4, 1/16). The 2D weight is the outer product.
float KernelWeight(int Offset)
{
    const float Weights[5] = float[5](0.0625, 0.25, 0.375, 0.25, 0.0625);
    return Weights[Offset + 2];
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        KERNEL
//------------------------------------------------------------------------------------------------------------------------

void main()
{
    ivec2 Pixel = ivec2(gl_GlobalInvocationID.xy);
    if (Pixel.x >= int(Extent.x) || Pixel.y >= int(Extent.y)) return;

    vec4 Centre = imageLoad(SourceImage, Pixel);

    // Disabled, or a pixel with no surface (background): pass straight through. Filtering the background against
    //    geometry would drag scene colour into the void and produce a halo around every silhouette.
    vec4 CentreSurface = imageLoad(SurfaceImage, Pixel);
    if (Enabled == 0u || CentreSurface.w <= 0.0)
    {
        imageStore(TargetImage, Pixel, Centre);
        if (FinalLevel != 0u) imageStore(OutputImage, Pixel, vec4(ToneMap(Centre.rgb), 1.0));
        return;
    }

    vec3  CentreNormal    = CentreSurface.xyz;
    float CentreDepth     = CentreSurface.w;
    float CentreLuminance = Luminance(Centre.rgb);

    // The luminance denominator uses a 3×3 pre-filtered variance so a single bright fireflier cannot declare itself
    //    an edge and survive every level. ε keeps a fully converged (zero-variance) region from dividing by zero.
    float VarianceSum = 0.0;
    float VarianceWeight = 0.0;
    for (int Y = -1; Y <= 1; ++Y)
        for (int X = -1; X <= 1; ++X)
        {
            ivec2 Tap = clamp(Pixel + ivec2(X, Y), ivec2(0), ivec2(Extent) - 1);
            float W   = KernelWeight(X) * KernelWeight(Y);
            VarianceSum    += imageLoad(SourceImage, Tap).a * W;
            VarianceWeight += W;
        }
    float LocalVariance = VarianceWeight > 0.0 ? max(VarianceSum / VarianceWeight, 0.0) : 0.0;
    float LuminanceDenominator = LuminanceScale * sqrt(LocalVariance) + 1.0e-4;

    vec3  ColourSum   = vec3(0.0);
    float VarianceOut = 0.0;
    float WeightSum   = 0.0;

    int Step = int(StepSize);

    for (int Y = -2; Y <= 2; ++Y)
    {
        for (int X = -2; X <= 2; ++X)
        {
            ivec2 Offset = ivec2(X, Y) * Step;
            ivec2 Tap    = Pixel + Offset;

            // Taps outside the image are dropped rather than clamped: clamping would pile the edge pixel's value
            //    up against the border and produce a bright rim.
            if (Tap.x < 0 || Tap.y < 0 || Tap.x >= int(Extent.x) || Tap.y >= int(Extent.y)) continue;

            vec4 TapSurface = imageLoad(SurfaceImage, Tap);
            if (TapSurface.w <= 0.0) continue;                     // background never bleeds into a surface

            vec4  TapColour = imageLoad(SourceImage, Tap);
            float Base      = KernelWeight(X) * KernelWeight(Y);

            // ① Normal agreement.
            float NormalWeight = pow(max(dot(CentreNormal, TapSurface.xyz), 0.0), NormalPower);

            // ② Depth. Scaled by the centre depth so the tolerance is relative — a 5 cm step matters at 1 m and
            //    is irrelevant at 100 m — and by the tap distance, standing in for the depth gradient.
            float DepthDelta  = abs(CentreDepth - TapSurface.w);
            float DepthSpan   = DepthScale * CentreDepth * float(max(abs(X), abs(Y)) * Step) + 1.0e-3;
            float DepthWeight = exp(-DepthDelta / DepthSpan);

            // ③ Luminance, relative to the noise level.
            float LuminanceDelta  = abs(CentreLuminance - Luminance(TapColour.rgb));
            float LuminanceWeight = exp(-LuminanceDelta / LuminanceDenominator);

            float Weight = Base * NormalWeight * DepthWeight * LuminanceWeight;

            ColourSum   += TapColour.rgb * Weight;
            VarianceOut += TapColour.a   * Weight * Weight;        // variance of a weighted mean
            WeightSum   += Weight;
        }
    }

    // WeightSum is always ≥ the centre tap's own weight, so this cannot divide by zero; the guard is for the
    //    pathological case of a denormal kernel weight.
    vec4 Result = WeightSum > 1.0e-8
                ? vec4(ColourSum / WeightSum, VarianceOut / (WeightSum * WeightSum))
                : Centre;

    imageStore(TargetImage, Pixel, Result);

    // The last level is also the presentation write. Doing it here rather than in a sixth pass saves a full-screen
    //    round trip, and keeps the tone map the only place linear radiance becomes display values.
    if (FinalLevel != 0u) imageStore(OutputImage, Pixel, vec4(ToneMap(Result.rgb), 1.0));
}
