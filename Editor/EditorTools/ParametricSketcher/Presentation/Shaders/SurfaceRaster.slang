//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Presentation/Shaders/SurfaceRaster.slang — Shaded NURBS faces: hemispheric fill, key light, back-face tint
//============================================================================================================================================
// One vertex per tessellation sample (position, normal, uv). Fragment lighting is view-independent enough for a
//    modelling viewport: hemisphere ambient + one key light + a soft rim, and a deliberate warm-red tint on back
//    faces so an inverted winding is visible at a glance — that is the proof surface for the outward-normal rule.
//    Selection brightens toward the selection orange; hover toward white.

#include "ShadingRecords.slang"
#include "MatcapStudio.slang"

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
    float4 World      = mul(Draw.LocalWorld, float4(Position, 1.0));
    Out.WorldPosition = World.xyz;
    Out.WorldNormal   = normalize(mul(Draw.LocalWorld, float4(Normal, 0.0)).xyz);
    Out.Parameter     = Parameter;
    Out.Clip          = mul(View.ViewClip, World);
    return Out;
}

// The software raster passes a bilinear-pre-render layer sampler; the Vulkan path binds a Texture2DArray. Both resolve
//    to the same studio function, so the interface is a plain function pointer-free call: MatcapLookup(Layer, N).
float3 MatcapLookup(uint Layer, float3 ViewNormal);

float4 SurfaceFragment(ViewRecord View, DrawRecord Draw, SurfaceVarying In, bool FrontFacing)
{
    float3 N = normalize(In.WorldNormal);
    float3 V = normalize(View.EyePosition.xyz - In.WorldPosition);
    if (View.EyePosition.w < 0.5) V = normalize(View.EyePosition.xyz);        // orthographic: EyePosition holds the view direction
    if (!FrontFacing) N = -N;

    float3 Albedo = Draw.Tint.xyz;
    float3 Colour;
    uint   Mode = uint(Draw.Surface.y + 0.5);
    if (Mode == 2u)
    {
        // Matcap: view-space normal from the camera basis (right, up, toward-viewer) encoded in ViewClip's rows.
        // Column-major camera→world: column 0 = right, column 1 = up, column 2 = back (toward the viewer).
        float3 Right = normalize(float3(View.ViewWorld.m[0], View.ViewWorld.m[1], View.ViewWorld.m[2]));
        float3 Up    = normalize(float3(View.ViewWorld.m[4], View.ViewWorld.m[5], View.ViewWorld.m[6]));
        float3 Vn    = float3(dot(N, Right), dot(N, Up), saturate(dot(N, V)));
        Vn = normalize(Vn);
        Colour = MatcapLookup(uint(Draw.Surface.x + 0.5), Vn);
        // Tint modulates only the dielectric/neutral studios lightly so per-figure colour still reads.
        Colour = Colour * lerp(float3(1.0, 1.0, 1.0), Albedo / max(max(Albedo.x, Albedo.y), max(Albedo.z, 1e-3)), 0.20);
    }
    else if (Mode == 1u)
    {
        // Plastic: Blinn-Phong with a fresnel rim, hemisphere fill.
        float3 Key      = normalize(View.Illumination.xyz);
        float  Ambient  = View.Illumination.w;
        float  Hemi     = 0.5 + 0.5 * N.z;
        float  Diffuse  = saturate(dot(N, Key));
        float3 H        = normalize(Key + V);
        float  Specular = pow(saturate(dot(N, H)), 96.0) * 0.6;
        float  Fresnel  = pow(1.0 - saturate(dot(N, V)), 4.0) * 0.35;
        Colour = Albedo * (Ambient * Hemi + Diffuse * 0.8) + float3(Specular + Fresnel, Specular + Fresnel, Specular + Fresnel);
    }
    else
    {
        float3 Key     = normalize(View.Illumination.xyz);
        float  Hemi    = 0.5 + 0.5 * N.z;
        Colour = Albedo * (View.Illumination.w * Hemi + saturate(dot(N, Key)) * 0.85);
    }

    if (!FrontFacing) Colour = lerp(Colour, float3(0.85, 0.30, 0.25), 0.6);   // 🔴 back face → winding fault visible
    if (Draw.Selection.x > 1.5)      Colour = lerp(Colour, float3(1.00, 0.62, 0.15), 0.55);
    else if (Draw.Selection.x > 0.5) Colour = lerp(Colour, float3(1.0, 1.0, 1.0), 0.25);
    Colour += Draw.Surface.z * Albedo;                                           // emissive boost (gizmo hover)
    return float4(Colour, Draw.Tint.w);
}
