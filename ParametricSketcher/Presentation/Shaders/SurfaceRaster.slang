//============================================================================================================================================
// 📦 ParametricSketcher/Presentation/Shaders/SurfaceRaster.slang — Shaded NURBS faces: hemispheric fill, key light, back-face tint
//============================================================================================================================================
// One vertex per tessellation sample (position, normal, uv). Fragment lighting is view-independent enough for a
//    modelling viewport: hemisphere ambient + one key light + a soft rim, and a deliberate warm-red tint on back
//    faces so an inverted winding is visible at a glance — that is the proof surface for the outward-normal contract.
//    Selection state brightens toward the selection orange; hover toward white.

#include "ShadingRecords.slang"

struct SurfaceVarying
{
    float4 Clip;                // clip-space position (SV_Position)
    float3 WorldPosition;
    float3 WorldNormal;
    float2 Parameter;           // (u, v) — used for iso-curve overlay
};

SurfaceVarying SurfaceVertex(ViewRecord View, DrawRecord Draw, float3 Position, float3 Normal, float2 Parameter)
{
    SurfaceVarying Out;
    float4 World      = mul(Draw.ModelWorld, float4(Position, 1.0));
    Out.WorldPosition = World.xyz;
    Out.WorldNormal   = normalize(mul(Draw.ModelWorld, float4(Normal, 0.0)).xyz);
    Out.Parameter     = Parameter;
    Out.Clip          = mul(View.ViewClip, World);
    return Out;
}

float4 SurfaceFragment(ViewRecord View, DrawRecord Draw, SurfaceVarying In, bool FrontFacing)
{
    float3 N = normalize(In.WorldNormal);
    float3 V = normalize(View.EyePosition.xyz - In.WorldPosition);
    if (View.EyePosition.w < 0.5) V = normalize(View.EyePosition.xyz);        // orthographic: EyePosition holds the view direction
    if (!FrontFacing) N = -N;

    float3 Key       = normalize(View.Illumination.xyz);
    float  Ambient   = View.Illumination.w;
    float  Hemi      = 0.5 + 0.5 * N.z;                                        // sky above (Z-up)
    float  Diffuse   = saturate(dot(N, Key));
    float3 H         = normalize(Key + V);
    float  Specular  = pow(saturate(dot(N, H)), 48.0) * 0.25;
    float  Rim       = pow(1.0 - saturate(dot(N, V)), 3.0) * 0.18;

    float3 Base = Draw.Tint.xyz;
    if (!FrontFacing) Base = lerp(Base, float3(0.85, 0.30, 0.25), 0.7);       // 🔴 back face → winding fault visible
    if (Draw.Selection.x > 1.5)      Base = lerp(Base, float3(1.00, 0.62, 0.15), 0.55);
    else if (Draw.Selection.x > 0.5) Base = lerp(Base, float3(1.0, 1.0, 1.0), 0.25);

    float3 Colour = Base * (Ambient * Hemi + Diffuse * 0.85) + float3(Specular + Rim, Specular + Rim, Specular + Rim);
    return float4(Colour, Draw.Tint.w);
}
