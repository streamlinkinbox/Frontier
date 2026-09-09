//============================================================================================================================================
// 📦 Engine/Shaders/ShadowResolve.slang — GI-off direct lighting: the shaded image, with tier-selected shadows
//============================================================================================================================================
// 🧩 The GPU no-ray path. SurfaceResolve has already turned the visibility buffer into a thin G-buffer (world
//    position + geometric normal per pixel); this pass spends it on direct-only lookdev PBR — Lambert plus a GGX
//    specular under the luminaire taps, a flat ambient fill, no bounces — and takes light visibility from the
//    shadow maps rasterised this frame by ShadowRaster.
//
//    This is what runs when Global Illumination is off. The ReSTIR kernel is not dispatched at all in that mode, so
//    nothing here traces a ray: the only occlusion test is the map lookup in ShadowSample.slang, whose technique
//    (hard · wide PCF · PCSS) is whichever one the active quality tier chose.
//
//    The shading maths deliberately mirrors VisibilityRaster::Shade on the CPU term for term — same ambient
//    constant, same GGX D/G/F, same Reinhard tone map — so the headless proof's sheets and the application's frame
//    are the same picture at the same settings, and a divergence is a bug in one of the two, not a design choice.
// NOTE: GLSL source lowered by glslc -fshader-stage=compute.

#version 460
#extension GL_GOOGLE_include_directive : require
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "Shaders/SceneRecords.slang"
#include "Shaders/ShadowSample.slang"

layout(std430, set = 0, binding = 1) readonly buffer InstanceExtent { InstanceRecord Instances[]; };
layout(std430, set = 0, binding = 5) readonly buffer MaterialExtent { MaterialRecord Materials[]; };

layout(set = 0, binding = 10, rgba32f) uniform readonly  image2D SurfaceImage;   // xyz = world position, w = visibility id
layout(set = 0, binding = 11, rgba16f) uniform readonly  image2D NormalImage;    // xyz = geometric normal, camera-facing
layout(set = 0, binding = 12, rgba8)   uniform writeonly image2D OutputImage;

const float kPi      = 3.14159265359;
const float kAmbient = 0.045;   // matches VisibilityRaster::kAmbient — the flat fill under the direct light
const vec3  kMiss    = vec3(0.30, 0.42, 0.63);   // matches the CPU path's background

void main()
{
    uvec2 Pixel = gl_GlobalInvocationID.xy;
    if (Pixel.x >= Extent.x || Pixel.y >= Extent.y) return;

    vec4 Surface    = imageLoad(SurfaceImage, ivec2(Pixel));
    uint Visibility = floatBitsToUint(Surface.w);
    if (Visibility == kVisibilityInvalid)
    {
        imageStore(OutputImage, ivec2(Pixel), vec4(pow(kMiss / (1.0 + kMiss), vec3(1.0 / 2.2)), 1.0));
        return;
    }

    vec3 Position = Surface.xyz;
    vec3 Normal   = normalize(imageLoad(NormalImage, ivec2(Pixel)).xyz);

    MaterialRecord M = Materials[Instances[VisibilityInstance(Visibility)].MaterialIndex];
    vec3  Albedo    = M.Albedo.xyz;
    float Roughness = M.Albedo.w;
    float Metalness = M.Emissive.w;

    // An emissive surface shows its own emission and is not lit further, exactly as on the CPU path.
    if (dot(M.Emissive.xyz, vec3(1.0)) > 0.0)
    {
        vec3 E = M.Emissive.xyz;
        imageStore(OutputImage, ivec2(Pixel), vec4(pow(E / (1.0 + E), vec3(1.0 / 2.2)), 1.0));
        return;
    }

    vec3 Accumulated = Albedo * (1.0 - Metalness) * kAmbient;

    vec3  ToEye = CameraOrigin.xyz - Position;
    float EyeLen = length(ToEye);
    if (EyeLen > 1e-6) ToEye /= EyeLen;
    float NdotV = dot(Normal, ToEye);
    if (NdotV <= 0.0) NdotV = 1e-4;

    float Alpha = max(Roughness * Roughness, 0.05);
    float A2    = Alpha * Alpha;
    float K     = (Alpha + 1.0) * (Alpha + 1.0) * 0.125;

    uint Taps = min(ShadowControl.z, kShadowMaximumTaps);
    for (uint Tap = 0u; Tap < Taps; ++Tap)
    {
        vec3  ToLight = ShadowTapOrigin[Tap].xyz - Position;
        float Distance = length(ToLight);
        if (Distance <= 1e-6) continue;
        ToLight /= Distance;

        float NdotL = dot(Normal, ToLight);
        if (NdotL <= 0.0) continue;

        // BOTH cosine rejects come before the shadow lookup. LdotL is the emitter's own facing test: a tap that
        //    points away from this surface cannot light it whatever the shadow map says. Testing it afterwards
        //    would spend a full PCSS kernel — up to 106 texel fetches at Reference — on a result thrown away on
        //    the next line. VisibilityRaster::Shade already rejects on both cosines before calling Shadow(), so
        //    this ordering is what keeps the GPU path faithful to the CPU one rather than merely cheaper.
        float LdotL = -dot(ShadowTapNormal[Tap].xyz, ToLight);
        if (LdotL <= 0.0) continue;

        float Lit = ShadowLitFraction(Position, Normal, NdotL, Tap);
        if (Lit <= 0.0) continue;

        // The tap's radiance and the solid angle it subtends are carried alongside the matrices; the CPU path
        //    folds the same NdotL · LdotL · Area / d² geometry term.
        vec3  Radiance = ShadowTapRadiance[Tap].xyz;
        float Weight   = ShadowTapRadiance[Tap].w;

        float Geometry = NdotL * LdotL * Weight * Lit / (Distance * Distance);

        vec3  Half     = normalize(ToLight + ToEye);
        float NdotH    = max(dot(Normal, Half), 0.0);
        float Denom    = NdotH * NdotH * (A2 - 1.0) + 1.0;
        float D        = A2 / (kPi * Denom * Denom);
        float G        = (NdotV / (NdotV * (1.0 - K) + K)) * (NdotL / (NdotL * (1.0 - K) + K));
        float Cos1     = 1.0 - NdotH;
        float Cos5     = Cos1 * Cos1 * Cos1 * Cos1 * Cos1;

        vec3 F0   = mix(vec3(0.04), Albedo, Metalness);
        vec3 F    = F0 + (vec3(1.0) - F0) * Cos5;
        vec3 Spec = D * G * F / (4.0 * NdotV * NdotL);
        vec3 Diff = Albedo * (1.0 - Metalness) / kPi;

        Accumulated += (Diff + Spec) * Radiance * Geometry;
    }

    vec3 Mapped = Accumulated / (1.0 + Accumulated);           // Reinhard, as on the CPU path
    imageStore(OutputImage, ivec2(Pixel), vec4(pow(max(Mapped, vec3(0.0)), vec3(1.0 / 2.2)), 1.0));
}
