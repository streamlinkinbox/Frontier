//============================================================================================================================================
//                                                   SURFELGIRESOLVE.SLANG
//============================================================================================================================================
// 📦 GTX path, pass 2 of 2 — per-pixel resolve. Reads the visibility raster's G-buffer (SurfaceImage = world position,
//    NormalImage = normal — the SAME pair SurfaceResolve.slang produces for the ReSTIR kernel), evaluates the material,
//    and composes the final pixel from three separable terms:
//        direct  = sun NEE, shadow-ray tested        (always — sharp)
//        indirect= surfel-field diffuse gather        (only when Global Illumination is ON)
//        reflect = OFF / SKY / RAYTRACED              (the Reflections quick-tile mode)
//    then tone-maps to OutputImage. Materials are evaluated in ALL modes here, not only the raytraced kernel — which is
//    the correctness requirement: a surface looks the same material whether it is lit by ReSTIR, by surfels, or flat.
//
//    This one shader covers TWO of the three render modes (the third is the raytraced ReSTIR kernel, unchanged):
//        • Raytracing OFF + GI ON  → surfel GI      : kFeatureGlobalIllumination set, surfel gather runs.
//        • Raytracing OFF + GI OFF → plain raster   : flag clear, indirect = sky ambient only (never black), no rays.
//    The Reflections mode is independent (Off/Sky/Raytraced); the host clamps it so Raytraced reflections only appear
//    when the raytracing budget is actually available (see PortingGuide.md mode matrix + the fall-back guard).
//
//    THE MATERIAL IS DECODED, NOT APPROXIMATED.  SurfaceImage.w carries the visibility id — the same packed
//    instance/primitive pair ReSTIRViewport.slang's primary hit unpacks — so this pass resolves the real triangle
//    through SceneMaterialResolve.slang and evaluates MaterialEvaluation.slang's full lobe stack.
//
//    It used to read an rgba8 albedo image and a metal/rough aux and shade `albedo·(1−metal)/π` plus a fixed
//    F0 = 0.04. That is a different material model from the one the raytraced path evaluates for the same pixel:
//    no coat, no transmission, no anisotropy, no sheen, no thin film, no F82 metal tint, no EON diffuse
//    roughness — and base colour quantised to 8 bits on the way through. A viewer toggling "Raytracing" was
//    therefore comparing two BSDFs as well as two transports, which makes the comparison useless for judging
//    either. Transport, visibility and sampling may differ between the paths. The BSDF may not.
//
//    NOTE: GLSL source lowered by the build scripts (glslang/glslc -fshader-stage=compute --target-env=vulkan1.2).

#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

//------------------------------------------------------------------------------------------------------------------------
//                                                    IMAGES / G-BUFFER
//------------------------------------------------------------------------------------------------------------------------
layout(rgba8,   binding = 0) uniform writeonly image2D OutputImage;   // tone-mapped presentation
layout(rgba32f, binding = 1) uniform readonly  image2D SurfaceImage;  // xyz = world position, w = visibility id bits
layout(rgba16f, binding = 2) uniform readonly  image2D NormalImage;   // xyz = geometric normal
// 📝 There is no sky CUBE anywhere in this engine. This pass used to declare `layout(binding = 5) samplerCube
//    SkyCube` and sample it in three places, while SurfelGIStage::CreatePipelines listed bindings 0–4 and 8–12
//    and nothing else — a shader that reads a descriptor absent from its pipeline layout is an invalid pipeline,
//    so vkCreateComputePipelines rejected it and the stage fell back every time it was asked for.
//    The kernel's own sky is SkyRecords.slang's SkyAlong, which needs a nine-vec4 atmosphere block this pass has
//    no room for in a 96-byte push range. Until that block is shared, the sky here is the analytic two-lobe
//    gradient below, built only from push constants that already exist.
//    ⚠️ SEAM: this is NOT the kernel's atmosphere. Background and reflection-miss pixels will differ between the
//    surfel path and the raytraced path. Surface shading — the thing the paths are compared on — does not.

//------------------------------------------------------------------------------------------------------------------------
//                                                    SURFEL FIELD (read-only here)
//------------------------------------------------------------------------------------------------------------------------
struct GpuSurfel
{
    vec4 PositionRadius;
    vec4 NormalAge;
    vec4 Albedo;
    vec4 Irradiance;
    vec4 IrradianceNew;
};
layout(std430, binding = 10) readonly buffer SurfelExtent          { GpuSurfel Surfels[];      };
layout(std430, binding = 11) readonly buffer SurfelGridHeadExtent  { int  SurfelGridHead[]; };
layout(std430, binding = 12) readonly buffer SurfelGridNextExtent  { int  SurfelGridNext[]; };

// Shared CWBVH — direct shadow ray + raytraced reflection ray.
layout(std430, binding = 8) readonly buffer TraversalNodeExtent { vec4 CwbvhNodes[]; };
layout(std430, binding = 9) readonly buffer TraversalLeafExtent { vec4 CwbvhTris[];  };
uint TlasInstanceCount = 0u;
#include "TraversalCWBVH.slang"

//------------------------------------------------------------------------------------------------------------------------
//                                              THE SCENE, AS THE ReSTIR KERNEL SEES IT
//------------------------------------------------------------------------------------------------------------------------
#include "SceneShadingRecords.slang"

// 📝 13 and 14 are NOT free: MaterialEvaluation.slang claims them for EnergyLut and SheenLut, in every pass that
//    includes it. These start at 15 to leave that pair alone.
layout(std430, binding = 15) readonly buffer TriangleExtent     { GpuTriangle     Triangles[];     };
layout(std430, binding = 16) readonly buffer MaterialExtent     { GpuMaterial     Materials[];     };
layout(std430, binding = 17) readonly buffer InstanceExtent     { GpuInstance     Instances[];     };
layout(std430, binding = 18) readonly buffer MaterialSlabExtent { GpuMaterialSlab MaterialSlabs[]; };
layout(std430, binding = 19) readonly buffer VertexExtent       { GpuVertex       Vertices[];      };
layout(std430, binding = 20) readonly buffer IndexExtent        { uint            Indices[];       };

const uint kMaterialTextureNone = 0xFFFFFFFFu;
#define FRONTIER_AUTOMOTIVE_SHOWCASE 1
#include "MaterialEvaluation.slang"
layout(binding = 31) uniform sampler2D Textures[];   // bindless table, as in ReSTIRViewport (last binding in the set)
#include "SceneMaterialResolve.slang"

// The visibility id packing SurfaceResolve.slang writes into SurfaceImage.w — identical to ReSTIRViewport's.
const uint kVisibilityInvalid = 0xFFFFFFFFu;
uint VisibilityInstance(uint Packed)  { return (Packed >> 14) & 0x3FFFFu; }
uint VisibilityPrimitive(uint Packed) { return Packed & 0x3FFFu; }

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUSH CONSTANTS
//------------------------------------------------------------------------------------------------------------------------
layout(push_constant) uniform ResolveConstants
{
    vec4  SunDirectionRadiance;   // xyz = to-sun (unit), w = radiance
    vec4  SunColour;              // xyz = sun colour
    vec4  SkyAmbient;             // xyz = sky ambient (plain-raster diffuse fill), w = exposure
    vec4  GridOriginCell;         // xyz = grid origin, w = cell size
    vec4  CameraEye;              // xyz = eye, w unused
    uvec4 Counts;                 // x = surfel count, y = grid hash size, z = feature flags, w = reflection mode
} PC;

// Feature flags — MUST match Engine/Shaders/ReSTIRViewport.slang so a single ControlCentreSettings drives both paths.
const uint kFeatureGlobalIllumination = 1u;    // bit 0: surfel diffuse gather runs
// Reflection mode (Counts.w): 0 = Off, 1 = Sky, 2 = Raytraced.
const uint kReflectOff = 0u, kReflectSky = 1u, kReflectRaytraced = 2u;

// Horizon → zenith gradient about the sky ambient, plus a broad solar aureole. Roughness widens the lobe, which
//    is what a prefiltered cube would have done for a glossy reflection.
vec3 SkyAlongApprox(vec3 Direction, float Roughness)
{
    const vec3  up      = vec3(0.0, 0.0, 1.0);                       // world is RH Z-up, metres
    const float height  = clamp(dot(Direction, up) * 0.5 + 0.5, 0.0, 1.0);
    const vec3  zenith  = PC.SkyAmbient.xyz;
    const vec3  horizon = PC.SkyAmbient.xyz * vec3(1.35, 1.22, 1.05);  // Rayleigh reddens toward the horizon
    vec3 dome = mix(horizon, zenith, height * height);
    const float sunCos  = clamp(dot(Direction, normalize(PC.SunDirectionRadiance.xyz)), 0.0, 1.0);
    const float sharp   = mix(2048.0, 8.0, clamp(Roughness, 0.0, 1.0));
    dome += PC.SunColour.xyz * PC.SunDirectionRadiance.w * pow(sunCos, sharp) * 0.02;
    return dome;
}

ivec3 CellOf(vec3 p){ vec3 q = (p - PC.GridOriginCell.xyz) / PC.GridOriginCell.w; return ivec3(floor(q)); }
uint  CellHash(ivec3 c){ uint h = uint(c.x+1024)*73856093u ^ uint(c.y+1024)*19349663u ^ uint(c.z+1024)*83492791u; return h % PC.Counts.y; }

vec3 GatherField(vec3 P, vec3 N)
{
    vec3 acc = vec3(0.0); float wsum = 0.0;
    ivec3 base = CellOf(P);
    for (int dz=-1; dz<=1; ++dz) for (int dy=-1; dy<=1; ++dy) for (int dx=-1; dx<=1; ++dx)
    {
        uint cell = CellHash(base + ivec3(dx,dy,dz));
        int idx = SurfelGridHead[cell] - 1; int guard = 0;
        while (idx >= 0 && guard < 256)
        {
            GpuSurfel s = Surfels[idx];
            vec3 d = P - s.PositionRadius.xyz; float dist = length(d); float r = s.PositionRadius.w;
            if (s.Albedo.w > 0.5 && dist < r)
            {
                float wn = dot(N, s.NormalAge.xyz); float planar = abs(dot(d, s.NormalAge.xyz));
                if (wn > 0.0 && planar < r*0.5) { float wd = 1.0 - dist/r; wd*=wd; float w = wn*wd; acc += s.Irradiance.xyz*w; wsum += w; }
            }
            idx = SurfelGridNext[idx] - 1; ++guard;
        }
    }
    return wsum > 1e-4 ? acc / wsum : vec3(0.0);
}

vec3 SunDirect(vec3 P, vec3 N)
{
    vec3 L = normalize(PC.SunDirectionRadiance.xyz);
    float ndl = max(dot(N, L), 0.0);
    if (ndl <= 0.0) return vec3(0.0);
    vec3 rD = 1.0 / L;
    bool shadowed = TraverseOccluded(P + N*1e-3, L, rD, 1e30);   // single-blob world; two-level path: TraverseInstancesOccluded
    return shadowed ? vec3(0.0) : PC.SunColour.xyz * (PC.SunDirectionRadiance.w * ndl);
}

// Schlick Fresnel for the specular/reflection weight.
vec3 Fresnel(vec3 f0, float cosTheta){ float m = clamp(1.0 - cosTheta, 0.0, 1.0); float m2 = m*m; return f0 + (vec3(1.0) - f0) * (m2*m2*m); }

// Shade a ray hit: the real triangle, the real slab, the engine's BSDF. Shared by the reflection ray here and
//    by SurfelIrradianceUpdate.slang's bounce — the two must agree about what a surface does with light.
vec3 ShadeHit(vec3 hp, uint flatPrim, vec3 wo)
{
    GpuInstance  inst = Instances[0];                               // single-blob world
    GpuMaterial  mat  = Materials[TriangleMaterial(flatPrim)];
    SurfaceFrame F;
    ShadingRecord m = ResolveMaterial(mat, inst, LocalPrimitive(flatPrim, 0u), hp, wo, 0.0, F);
    vec3 woLoc = ToLocal(F, wo);
    if (woLoc.z <= 0.0) return F.Emission;
    ResolvedLayers layers = ResolveLayers(m, woLoc);

    vec3 Lo = F.Emission;
    vec3 L = normalize(PC.SunDirectionRadiance.xyz);
    vec3 wiLoc = ToLocal(F, L);
    if (wiLoc.z > 0.0 && !TraverseOccluded(hp + F.Geometric*1e-3, L, 1.0/L, 1e30))
        Lo += EvaluateBsdf(m, layers, woLoc, wiLoc) * (PC.SunColour.xyz * PC.SunDirectionRadiance.w) * wiLoc.z;
    Lo += AmbientResponse(m, layers, woLoc) * (kInvPi * GatherField(hp, F.Normal));
    return Lo;
}

vec3 Reflection(vec3 P, vec3 N, vec3 V, float roughness, uint mode)
{
    if (mode == kReflectOff) return vec3(0.0);
    vec3 R = reflect(-V, N);
    if (mode == kReflectSky)
    {
        // reflect the sky dome only — no scene rays. Roughness widens toward the ambient (cheap gloss).
        return SkyAlongApprox(R, roughness);
    }
    // kReflectRaytraced — one reflection ray against the CWBVH; miss → sky, hit → the hit's own material.
    vec3 rD = 1.0 / R;
    TraversalHit h = TraverseClosest(P + N*1e-3, R, rD, 1e30);
    if (h.t >= 1e29) return SkyAlongApprox(R, 0.0);
    return ShadeHit(P + R * h.t, h.primitive, -R);
}

void main()
{
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(SurfaceImage);
    if (pixel.x >= size.x || pixel.y >= size.y) return;

    vec4 surf = imageLoad(SurfaceImage, pixel);
    vec3 P = surf.xyz;
    uint visibility = floatBitsToUint(surf.w);
    // A miss (no geometry) shows the sky directly.
    if (visibility == kVisibilityInvalid)
    {
        vec3 V = normalize(P - PC.CameraEye.xyz);
        vec3 sky = SkyAlongApprox(V, 0.0);
        imageStore(OutputImage, pixel, vec4(pow(sky/(sky+vec3(1.0)), vec3(1.0/2.2)), 1.0));
        return;
    }

    // ── the receiving material, decoded ─────────────────────────────────────────────────────────────────────
    // Same unpack, same instance row, same ResolveMaterial call as ReSTIRViewport.slang's primary hit. The
    //    shading frame comes back from the decode, so the normal map and the tangent basis are the raytraced
    //    path's, not a geometric normal read out of an rgba16f image.
    GpuInstance  inst = Instances[VisibilityInstance(visibility)];
    GpuMaterial  mat  = Materials[inst.MaterialIndex];
    vec3  V         = normalize(PC.CameraEye.xyz - P);
    float coneWidth = length(PC.CameraEye.xyz - P) * 0.0015;   // ≈ one pixel's spread at the hit [m]
    SurfaceFrame F;
    ShadingRecord m = ResolveMaterial(mat, inst, VisibilityPrimitive(visibility), P, V, coneWidth, F);
    vec3 N = F.Normal;

    uint flags = PC.Counts.z;
    uint reflMode = PC.Counts.w;

    // An emitter is its own radiance, as in the raytraced kernel.
    if (dot(F.Emission, F.Emission) > 0.0)
    {
        vec3 e = F.Emission * PC.SkyAmbient.w;
        imageStore(OutputImage, pixel, vec4(pow(e/(e+vec3(1.0)), vec3(1.0/2.2)), 1.0));
        return;
    }

    vec3 woLoc = ToLocal(F, V);
    if (woLoc.z <= 0.0) { imageStore(OutputImage, pixel, vec4(0.0, 0.0, 0.0, 1.0)); return; }
    ResolvedLayers layers = ResolveLayers(m, woLoc);

    // --- direct (always, sharp) — the full lobe stack, not a Lambert ---
    vec3 col = vec3(0.0);
    {
        vec3 L = normalize(PC.SunDirectionRadiance.xyz);
        vec3 wiLoc = ToLocal(F, L);
        if (wiLoc.z > 0.0 && !TraverseOccluded(P + F.Geometric*1e-3, L, 1.0/L, 1e30))
            col += EvaluateBsdf(m, layers, woLoc, wiLoc) * (PC.SunColour.xyz * PC.SunDirectionRadiance.w) * wiLoc.z;
    }

    // --- indirect diffuse: an irradiance through this material's hemispherical response ---
    vec3 response = AmbientResponse(m, layers, woLoc);
    if ((flags & kFeatureGlobalIllumination) != 0u)
        col += response * (kInvPi * GatherField(P, N));        // surfel GI
    else
        col += response * PC.SkyAmbient.xyz;                   // plain raster: sky ambient fill (never black)

    // --- reflections (Off / Sky / Raytraced) ---
    // Fresnel from the resolved slab's own F0 — metal tint, coat IOR and all — rather than a hard 0.04.
    if (reflMode != kReflectOff)
    {
        vec3 f0 = mix(vec3(0.04), m.BaseColor, m.Metalness) * m.SpecularColor;
        vec3 Fr = Fresnel(f0, max(woLoc.z, 0.0));
        col += Fr * Reflection(P, N, V, m.SpecularRoughness, reflMode);
    }

    // exposure + Reinhard + gamma
    col *= PC.SkyAmbient.w;
    vec3 mapped = pow(col / (col + vec3(1.0)), vec3(1.0/2.2));
    imageStore(OutputImage, pixel, vec4(mapped, 1.0));
}
