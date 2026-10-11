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
    float3 Key    = normalize(float3(-0.45f, 0.65f, 0.61f));
    float3 Fill   = normalize(float3(0.7f, -0.2f, 0.68f));
    float3 View   = float3(0.0f, 0.0f, 1.0f);
    float  NdotV  = saturate(N.z);
    float  Fresnel = pow(1.0f - NdotV, 4.0f);

    // Reflection vector for the studio environment (sphere map behaviour).
    float3 R = reflect(-View, N);
    float  Sky      = smoothstep(-0.6f, 1.0f, R.y) * 0.75f + 0.25f;                    // graded softbox above, never black
    float  Floor    = smoothstep(0.1f, -0.9f, R.y) * 0.5f;                            // gentle floor darkening
    float  Horizon  = exp(-R.y * R.y * 10.0f) * 0.5f;                                // soft horizon band
    float  Softbox  = pow(saturate(dot(R, Key)), 4.0f);                             // broad key reflection
    float  Spark    = pow(saturate(dot(R, Key)), 160.0f);                           // pin highlight
    float  FillBand = pow(saturate(dot(R, Fill)), 2.5f) * 0.30f;
    float  Side     = pow(saturate(dot(R, normalize(float3(0.9f, 0.1f, 0.3f)))), 3.0f) * 0.18f; // second softbox, camera right

    float3 Albedo; float Metal; float Rough; float3 EnvTint = float3(1.0f, 1.0f, 1.0f);
    if (Layer == 0u)      { Albedo = float3(0.62f, 0.66f, 0.72f); Metal = 1.0f; Rough = 0.35f; }              // steel
    else if (Layer == 1u) { Albedo = float3(0.92f, 0.94f, 0.97f); Metal = 1.0f; Rough = 0.05f; }              // chrome
    else if (Layer == 2u) { Albedo = float3(1.00f, 0.78f, 0.34f); Metal = 1.0f; Rough = 0.25f; EnvTint = float3(1.0f, 0.9f, 0.7f); } // gold
    else if (Layer == 3u) { Albedo = float3(0.95f, 0.55f, 0.40f); Metal = 1.0f; Rough = 0.30f; EnvTint = float3(1.0f, 0.85f, 0.75f); } // copper
    else if (Layer == 4u) { Albedo = float3(0.90f, 0.91f, 0.93f); Metal = 0.0f; Rough = 0.30f; }              // plastic white
    else if (Layer == 5u) { Albedo = float3(0.85f, 0.16f, 0.14f); Metal = 0.0f; Rough = 0.25f; }              // plastic red
    else if (Layer == 6u) { Albedo = float3(0.16f, 0.36f, 0.85f); Metal = 0.0f; Rough = 0.25f; }              // plastic blue
    else if (Layer == 7u) { Albedo = float3(0.70f, 0.62f, 0.55f); Metal = 0.0f; Rough = 0.85f; }              // clay
    else if (Layer == 8u) { Albedo = float3(0.93f, 0.90f, 0.95f); Metal = 0.3f; Rough = 0.15f; EnvTint = float3(0.95f, 0.9f, 1.05f); } // pearl
    else if (Layer == 9u) { Albedo = float3(0.10f, 0.11f, 0.12f); Metal = 0.6f; Rough = 0.40f; }              // carbon
    else if (Layer == 10u){ Albedo = float3(0.045f, 0.045f, 0.05f); Metal = 0.0f; Rough = 0.95f; }            // rubber (matte tyre)
    else if (Layer == 11u){ Albedo = float3(0.03f, 0.05f, 0.07f); Metal = 0.0f; Rough = 0.04f; }              // glass (dark tinted, glossy)
    else if (Layer == 12u){ Albedo = float3(0.95f, 0.96f, 1.00f); Metal = 0.0f; Rough = 0.20f; }              // headlight (emissive white)
    else                  { Albedo = float3(0.90f, 0.05f, 0.05f); Metal = 0.0f; Rough = 0.25f; }              // taillight (emissive red)

    // Environment radiance seen in the reflection, blurred by roughness.
    float  Blur = Rough * Rough;
    // Metals see a higher-contrast studio: sky vs. floor gradient, sharp softbox edges; dielectrics only borrow a fraction.
    float  SkyEdge   = 0.35f + 0.65f * smoothstep(-0.25f, 0.35f, R.y);                  // horizon for mirrors; floor stays a lit studio floor, not a void
    float  Contrast  = lerp(SkyEdge, Sky, Blur);
    float3 Env  = EnvTint * (0.18f + 0.62f * Contrast - 0.12f * lerp(Floor, 0.25f, Blur) + 0.30f * lerp(Horizon, 0.15f, Blur) + 0.75f * lerp(Softbox, 0.25f, Blur) + FillBand + Side);

    // Diffuse term for dielectrics (Lambert against the studio lights + hemisphere).
    float  Lambert = 0.28f + 0.55f * saturate(dot(N, Key)) + 0.20f * saturate(dot(N, Fill)) + 0.15f * saturate(N.y);
    float3 Diffuse = Albedo * Lambert;

    // Dielectric specular: sharp for low roughness, fresnel-boosted at grazing angles.
    float  Gloss    = lerp(220.0f, 12.0f, Rough);
    float  Spec     = pow(saturate(dot(R, Key)), Gloss) * lerp(1.0f, 0.25f, Rough) + Spark * (1.0f - Rough) * 0.6f;
    float3 Dielectric = Diffuse + (0.04f + 0.6f * Fresnel) * lerp(Env, float3(0.6f, 0.6f, 0.6f), 0.5f) * 1.6f + Spec * float3(1.0f, 1.0f, 1.0f);
    float3 Metallic   = Albedo * Env * 1.25f + Spark * (1.0f - Rough) * 1.1f * Albedo + Fresnel * 0.20f * Albedo;

    float3 Colour = lerp(Dielectric, Metallic, Metal);
    if (Layer == 9u) Colour += float3(1.0f, 1.0f, 1.0f) * (pow(saturate(dot(R, Key)), 40.0f) * 0.35f);            // carbon: clear-coat sheen
    // Emissive studios: add a self-lit floor so the light reads as glowing regardless of the studio reflection.
    if (Layer == 12u) Colour += float3(0.85f, 0.87f, 0.95f) * 0.9f;                                              // headlight glow
    if (Layer == 13u) Colour += float3(0.80f, 0.06f, 0.06f) * 0.9f;                                              // taillight glow
    return Colour;
}
