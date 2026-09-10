//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Presentation/Shaders/MatcapStudio.slang — Procedural matcaps: one function sample per fragment or pre-render to a layer
//============================================================================================================================================
// A matcap is a sphere photographed under a studio: colour = f(view-space normal). Plasticity assigns one per whole;
//    that costs nothing extra on the GPU because the per-draw record carries the layer index and the lookup is a single
//    Texture2DArray sample. The software raster pre-render each studio into a 128×128 layer once (MatcapStudio.cpp) and
//    samples bilinearly, so per-whole matcaps are as cheap as one shared one. Studios are analytic so the same code
//    can also run un-pre-render on a GPU (slower, but identical pixels).
//
//    Layers: 0 Steel · 1 Chrome · 2 Gold · 3 Copper · 4 Plastic white · 5 Plastic red · 6 Plastic blue · 7 Clay · 8 Pearl · 9 Carbon

#include "ShadingRecords.slang"

// Analytic studio: a bright overhead softbox, a cooler fill from the lower left, a dark floor reflection band and a
//    sharp key highlight. Returns linear radiance for a unit view-space normal N (z toward the viewer).
float3 MatcapStudioSample(uint Layer, float3 N)
{
    float3 Key    = normalize(float3(-0.45, 0.65, 0.61));
    float3 Fill   = normalize(float3(0.7, -0.2, 0.68));
    float3 View   = float3(0.0, 0.0, 1.0);
    float  NdotV  = saturate(N.z);
    float  Fresnel = pow(1.0 - NdotV, 4.0);

    // Reflection vector for the studio environment (sphere map behaviour).
    float3 R = reflect(-View, N);
    float  Sky      = smoothstep(-0.6, 1.0, R.y) * 0.75 + 0.25;                    // graded softbox above, never black
    float  Floor    = smoothstep(0.1, -0.9, R.y) * 0.5;                            // gentle floor darkening
    float  Horizon  = exp(-R.y * R.y * 10.0) * 0.5;                                // soft horizon band
    float  Softbox  = pow(saturate(dot(R, Key)), 4.0);                             // broad key reflection
    float  Spark    = pow(saturate(dot(R, Key)), 160.0);                           // pin highlight
    float  FillBand = pow(saturate(dot(R, Fill)), 2.5) * 0.30;
    float  Side     = pow(saturate(dot(R, normalize(float3(0.9, 0.1, 0.3)))), 3.0) * 0.18; // second softbox, camera right

    float3 Albedo; float Metal; float Rough; float3 EnvTint = float3(1.0, 1.0, 1.0);
    if (Layer == 0u)      { Albedo = float3(0.62, 0.66, 0.72); Metal = 1.0; Rough = 0.35; }              // steel
    else if (Layer == 1u) { Albedo = float3(0.92, 0.94, 0.97); Metal = 1.0; Rough = 0.05; }              // chrome
    else if (Layer == 2u) { Albedo = float3(1.00, 0.78, 0.34); Metal = 1.0; Rough = 0.25; EnvTint = float3(1.0, 0.9, 0.7); } // gold
    else if (Layer == 3u) { Albedo = float3(0.95, 0.55, 0.40); Metal = 1.0; Rough = 0.30; EnvTint = float3(1.0, 0.85, 0.75); } // copper
    else if (Layer == 4u) { Albedo = float3(0.90, 0.91, 0.93); Metal = 0.0; Rough = 0.30; }              // plastic white
    else if (Layer == 5u) { Albedo = float3(0.85, 0.16, 0.14); Metal = 0.0; Rough = 0.25; }              // plastic red
    else if (Layer == 6u) { Albedo = float3(0.16, 0.36, 0.85); Metal = 0.0; Rough = 0.25; }              // plastic blue
    else if (Layer == 7u) { Albedo = float3(0.70, 0.62, 0.55); Metal = 0.0; Rough = 0.85; }              // clay
    else if (Layer == 8u) { Albedo = float3(0.93, 0.90, 0.95); Metal = 0.3; Rough = 0.15; EnvTint = float3(0.95, 0.9, 1.05); } // pearl
    else                  { Albedo = float3(0.10, 0.11, 0.12); Metal = 0.6; Rough = 0.40; }              // carbon

    // Environment radiance seen in the reflection, blurred by roughness.
    float  Blur = Rough * Rough;
    // Metals see a higher-contrast studio: sky vs. floor gradient, sharp softbox edges; dielectrics only borrow a fraction.
    float  SkyEdge   = 0.35 + 0.65 * smoothstep(-0.25, 0.35, R.y);                  // horizon for mirrors; floor stays a lit studio floor, not a void
    float  Contrast  = lerp(SkyEdge, Sky, Blur);
    float3 Env  = EnvTint * (0.18 + 0.62 * Contrast - 0.12 * lerp(Floor, 0.25, Blur) + 0.30 * lerp(Horizon, 0.15, Blur) + 0.75 * lerp(Softbox, 0.25, Blur) + FillBand + Side);

    // Diffuse term for dielectrics (Lambert against the studio lights + hemisphere).
    float  Lambert = 0.28 + 0.55 * saturate(dot(N, Key)) + 0.20 * saturate(dot(N, Fill)) + 0.15 * saturate(N.y);
    float3 Diffuse = Albedo * Lambert;

    // Dielectric specular: sharp for low roughness, fresnel-boosted at grazing angles.
    float  Gloss    = lerp(220.0, 12.0, Rough);
    float  Spec     = pow(saturate(dot(R, Key)), Gloss) * lerp(1.0, 0.25, Rough) + Spark * (1.0 - Rough) * 0.6;
    float3 Dielectric = Diffuse + (0.04 + 0.6 * Fresnel) * lerp(Env, float3(0.6, 0.6, 0.6), 0.5) * 1.6 + Spec * float3(1.0, 1.0, 1.0);
    float3 Metallic   = Albedo * Env * 1.25 + Spark * (1.0 - Rough) * 1.1 * Albedo + Fresnel * 0.20 * Albedo;

    float3 Colour = lerp(Dielectric, Metallic, Metal);
    if (Layer == 9u) Colour += float3(1.0, 1.0, 1.0) * (pow(saturate(dot(R, Key)), 40.0) * 0.35);            // carbon: clear-coat sheen
    return Colour;
}
