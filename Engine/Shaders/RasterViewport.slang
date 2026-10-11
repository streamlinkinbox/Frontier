//============================================================================================================================================
//                                                           RASTERVIEWPORT.SLANG
//============================================================================================================================================
// 📦 Deterministic visibility shading with OpenPBR materials, raster shadows and the shared celestial/weather resolve.

#version 460
#extension GL_GOOGLE_include_directive : require
#define FRONTIER_RASTER_ONLY
#include "ViewportIntegrator.slang"
#include "ShadowSample.slang"

void main()
{
    uvec2 Pixel = gl_GlobalInvocationID.xy;
    if (Pixel.x >= ViewportWidth || Pixel.y >= ViewportHeight) return;
    vec4 Surface = imageLoad(SurfaceImage, ivec2(Pixel));
    uint Visibility = floatBitsToUint(Surface.w);
    if (Visibility == kVisibilityInvalid)
    {
        vec2 Extent = vec2(float(ViewportWidth), float(ViewportHeight));
        vec2 Screen = ((vec2(Pixel) + 0.5) / Extent) * 2.0 - 1.0;
        vec3 Direction = normalize(CameraForward + CameraRight * (Screen.x * FieldOfViewTanHalf * Extent.x / Extent.y)
                                                - CameraUp * (Screen.y * FieldOfViewTanHalf));
        Resolve(Pixel, SkyAlong(Direction) + RainbowAlong(Direction, SkySunDirection.xyz, 1.0e6));
        return;
    }

    GpuInstance Placement = Instances[VisibilityInstance(Visibility)];
    GpuMaterial Material = Materials[Placement.MaterialIndex];
    vec3 Position = Surface.xyz;
    vec3 Sight = CameraOrigin - Position;
    float Distance = length(Sight);
    Sight /= max(Distance, 1e-6);
    SurfaceFrame Faced;
    ShadingRecord Shading = ResolveMaterial(Material, Placement, VisibilityPrimitive(Visibility), Position,
                                           Sight, Distance * PixelSpreadAngle(), Faced);
    if (MaterialSelection(Material) == kReflectanceUnlit)
    {
        Resolve(Pixel, Shading.BaseColor);
        return;
    }
    vec3 LocalSight = ToLocal(Faced, Sight);
    LocalSight.z = max(LocalSight.z, 1e-3);
    ResolvedLayers Layers = ResolveLayers(Shading, LocalSight);
    vec3 Radiance = Faced.Emission;
    for (uint Tap = 0u; Tap < min(ShadowControl.z, kShadowMaximumTaps); ++Tap)
    {
        bool Directional = (ShadowControl.w & (kShadowTapDirectionalBit << Tap)) != 0u;
        vec3 Light = Directional ? -ShadowTapNormal[Tap].xyz : ShadowTapOrigin[Tap].xyz - Position;
        float SquaredDistance = dot(Light, Light);
        Light = normalize(Light);
        vec3 LocalLight = ToLocal(Faced, Light);
        float EmitterCosine = Directional ? 1.0 : max(-dot(ShadowTapNormal[Tap].xyz, Light), 0.0);
        if (LocalLight.z <= 0.0 || EmitterCosine <= 0.0) continue;
        float VisibilityWeight = ShadowLitFraction(Position, Faced.Geometric, max(dot(Faced.Geometric, Light), 0.0), Tap);
        vec3 Incident = ShadowTapRadiance[Tap].xyz;
        if (!Directional) Incident *= ShadowTapRadiance[Tap].w * EmitterCosine / max(SquaredDistance, 1e-4);
        Radiance += EvaluateBsdf(Shading, Layers, LocalSight, LocalLight) * Incident * LocalLight.z * VisibilityWeight;
    }
    // Sky ambient is an explicit setting, not an indirect bounce or a reservoir sample.
    if (SkyAmbientEnabled != 0u)
        Radiance += AmbientResponse(Shading, Layers, LocalSight) * SkyAlong(Faced.Normal);
    uint Reflection = (FeatureFlags >> kReflectionModeShift) & kReflectionModeMask;
    if (Reflection == 1u)
    {
        vec3 F0 = mix(vec3(0.04), Shading.BaseColor, Shading.Metalness) * Shading.SpecularColor;
        float Grazing = pow(1.0 - clamp(LocalSight.z, 0.0, 1.0), 5.0);
        Radiance += (F0 + (vec3(1.0) - F0) * Grazing) * SkyAlong(reflect(-Sight, Faced.Normal));
    }
    ResolveSurface(Pixel, Radiance, Faced.Geometric, Distance, Visibility, Shading.BaseColor);
}
