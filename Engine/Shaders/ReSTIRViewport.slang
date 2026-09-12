//============================================================================================================================================
// 📦 Engine/Shaders/ReSTIRViewport.slang — Progressive path tracer with RIS direct lighting (interim kernel)
//============================================================================================================================================
// 🚧 HONEST STATUS (plan v2.1 R0): this is NOT ReSTIR as published. Per pixel it does
//       • primary visibility from the R2 visibility raster (SurfaceImage / NormalImage, no primary rays),
//       • shadow / bounce rays traverse the R3 CWBVH (TraversalCWBVH.slang, bindings 8-9) — O(log N), not O(N),
//       • ReSTIR-DI *initial candidates only*: M light samples resampled with a weighted reservoir (correct RIS),
//         with ExtraCandidateCount further same-pixel candidates folded in, plus temporal (R6 row 2) and
//         spatiotemporal neighbour reuse (R6 row 3),
//       • one BSDF-sampled bounce with next-event estimation — plain 1-bounce path tracing, no GI reservoir (R6),
//       • R4b: surfaces are shaded with the OpenPBR lobe set of MaterialEvaluation.slang (one resolved slab, interpolated
//         normals + normal maps from the VertexRecord buffer, ray-cone texture LOD, GGX energy / LTC sheen LUTs),
//       • a running-mean accumulation that restarts on camera motion; motion vectors exist (R2) but are consumed only
//         by the R6 temporal reservoir (row 2), no denoiser (R7).
//    It is kept as the reference image source until the staged frame graph replaces it.
// NOTE: GLSL source lowered to SPIR-V by the build scripts (glslc -fshader-stage=compute --target-env=vulkan1.2 -I Engine).
//       World space is right-handed Z-up (CLAUDE.md §7); the pixel→ray mapping (Shaders/RayGeneration.slang) is applied by
//       SurfaceResolve.slang — this kernel no longer generates camera rays.

#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require   // R4a: bindless Textures[nonuniformEXT(slot)]
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

//------------------------------------------------------------------------------------------------------------------------
//                                                    STORAGE IMAGE OUTPUT
//------------------------------------------------------------------------------------------------------------------------

layout(rgba8,   binding = 0) uniform writeonly image2D OutputImage;    // tone-mapped 8-bit presentation image
layout(rgba32f, binding = 3) uniform          image2D HistoryImage;   // linear HDR running mean, .a = sample count
layout(rgba32f, binding = 4) uniform readonly image2D SurfaceImage;   // R2 SurfaceResolve: xyz = world position, w = visibility id bits
layout(rgba16f, binding = 5) uniform readonly image2D NormalImage;    // R2 SurfaceResolve: xyz = camera-facing geometric normal

//------------------------------------------------------------------------------------------------------------------------
//                                                    SCENE GEOMETRY BINDING
//------------------------------------------------------------------------------------------------------------------------

struct GpuTriangle          // TriangleIndex (SwapchainExchange.h), R4a layout
{
    vec4 v0;            // xyz = vertex α, w = materialIndex (floatBitsToUint)
    vec4 v1;            // xyz = vertex β, w = γ.u
    vec4 v2;            // xyz = vertex γ, w = γ.v
    vec4 uv;            // xy = α.uv, zw = β.uv
};

struct GpuMaterial          // MaterialRecord (ContentInterchange/MaterialIndex.h, 64 B) — R4a header; mirrors SceneRecords.slang
{
    vec4  Albedo;           // xyz = flattened base colour, w = specular roughness
    vec4  Emissive;         // xyz = emission radiance [nit], w = metalness
    uvec4 Slabs;            // x = SlabOffset, y = SlabCount, z = Flags, w = Complexity
    uvec2 Textures;         // x = base colour slot, y = normal slot (0xFFFFFFFF = none)
    float AlphaCutoff;
    float NormalScale;
};

struct GpuMaterialSlab      // MaterialSlabRecord (288 B) — 18 vec4; field map in SceneRecords.slang. Read by R4b shading.
{
    vec4  Payload[15];
    uvec4 TextureSlots0;
    uvec4 TextureSlots1;
    uvec4 Runtime;
};

struct GpuInstance          // InstanceRecord (SceneStructure.h / SceneRecords.slang) — only MaterialIndex is read here
{
    mat4 World;
    mat4 PreviousWorld;
    uint VertexOffset;
    uint FirstIndex;
    uint TriangleCount;
    uint MaterialIndex;
    uint ClusterOffset;
    uint ClusterCount;
    uint Flags;
    uint FlatTriangleOffset;
};

struct GpuLuminaire         // LuminaireRecord (SceneStructure.h): emissive triangle + alias entry
{
    uint  TriangleSlot;     // index into Triangles[]
    uint  InstanceIndex;
    uint  PrimitiveIndex;
    uint  AliasSlot;
    float Threshold;
    float Area;
    float Probability;
    float Pad;
};

layout(std430, binding = 1) readonly buffer TriangleExtent  { GpuTriangle  Triangles[];  };
layout(std430, binding = 2) readonly buffer MaterialExtent  { GpuMaterial  Materials[];  };
layout(std430, binding = 6) readonly buffer InstanceExtent  { GpuInstance  Instances[];  };
layout(std430, binding = 7) readonly buffer LuminaireExtent { GpuLuminaire Luminaires[]; };
layout(std430, binding = 8) readonly buffer TraversalNodeExtent { vec4 CwbvhNodes[]; };   // R3: TraversalIndex node blob
layout(std430, binding = 9) readonly buffer TraversalLeafExtent { vec4 CwbvhTris[];  };   // R3: TraversalIndex triangle blob
layout(std430, binding = 10) readonly buffer MaterialSlabExtent { GpuMaterialSlab MaterialSlabs[]; };

struct GpuVertex            // VertexRecord (SceneRecords.slang, 64 B) — R4b: interpolated normals / tangents / UVs
{
    vec4 Position;
    vec4 Normal;            // xyz object space
    vec4 Tangent;           // xyz object space, w = bitangent sign
    vec4 Texcoord;          // xy
};
layout(std430, binding = 11) readonly buffer VertexExtent { GpuVertex Vertices[]; };   // R4b: shared with the R2 raster set
layout(std430, binding = 12) readonly buffer IndexExtent  { uint      Indices[];  };   // R4b
// 13 = EnergyLut, 14 = SheenLut (declared in MaterialEvaluation.slang)
layout(binding = 15) uniform sampler2D MotionImage;   // R6: R2 motion (CurrentUv − PreviousUv, [0,1] uv units) — texelFetch only
struct GpuReservoir   // 64 B stride — mirrors ReservoirBufferRecord in SwapchainExchange.cpp
{
    vec4  Sample;    // xyz = light sample point, w = WeightSum
    uvec4 Counts;    // x = M, y = SelectedLight, z = Visible, w = Age
    vec4  UvDepth;   // xy = SelectedUv, z = W, w = view depth at the build pixel [m]
    vec4  Normal;    // xyz = viewer-facing geometric normal at the build pixel, w = stride guard (ViewportWidth)
};
layout(std430, binding = 16) readonly buffer PrevReservoirExtent { GpuReservoir PrevReservoirs[]; };
layout(std430, binding = 17) writeonly buffer CurrReservoirExtent { GpuReservoir CurrReservoirs[]; };
layout(rgba16f, binding = 18) uniform image2D HistorySurfaceImage;   // R7a: xyz = normal, w = depth the history mean was shaded at (w ≤ 0 = no surface)
layout(rgba32f, binding = 19) uniform image2D MomentImage;           // R7: x = E[luma], y = E[luma²], reprojected with the mean
layout(rgba32f, binding = 20) uniform writeonly image2D DenoiseImage; // R7: xyz = linear radiance, w = variance — the à-trous input
// ── Bindings 21-24 are the Celestial sky, moons, star tables and post record, declared in
//    Shaders/SkyRecords.slang, Shaders/MoonRecords.slang and Shaders/PostRecords.slang below. Round 9 vacated
//    21-24 when the sun/sky system was removed (947b8b9); all four are now filled, each written at bring-up —
//    a declared-but-never-written descriptor is a validation error rather than free space.
//    ⚠️ The bindless table is declared BEFORE the includes: MoonAlong samples it, and SkyAlong calls MoonAlong,
//    so the table and the moons must both be visible where the sky is defined. 25 is still the highest binding
//    NUMBER in the set, which is what the variable-count rule constrains — source order is free.
layout(binding = 25) uniform sampler2D Textures[];   // R4a bindless table — ⚠️ moved 11 → 15 in R4b → 18 in R6 → 19 in R7a → 21 in R7 → 23 in A2 → 25 in A7: a variable-count binding must be the last one in the set
#include "Shaders/MoonRecords.slang"
#include "Shaders/PostRecords.slang"
#include "Shaders/SkyRecords.slang"

const float kTemporalNormalCos = 0.906308;   // cos(25°): temporal/spatial normal validation
const float kTemporalDepthTol  = 0.10;       // 10% relative depth validation
const uint  kTemporalMClamp    = 20u;        // prev M capped at 20× current M (ReSTIR M-clamp)
const uint  kSpatialTapCeiling = 4u;         // R6 row 3: the widest cross the kernel will walk; SpatialTapCount picks how many of them run.
                                             //    R10: the COUNT is now tier-keyed (Minimal 0 … Reference 4) because a hardcoded 4 made the
                                             //    cheapest tier spend two thirds of its lighting budget — 12 of 18 full-BSDF target-function
                                             //    evaluations per pixel — on spatial reuse. The ceiling stays 4 so the 90° tap spacing below is
                                             //    unchanged for any tier that asks for the full cross.
// ⚠️ The cross is ROTATED and its radius JITTERED per pixel per frame. A fixed offset set makes the reuse graph static:
//    pixel (x,y) could only ever exchange reservoirs with (x±r,y) and (x,y±r), which partitions the image into r²
//    residue classes that never share a sample. Each class converges to its own estimate and temporal accumulation
//    cements the difference instead of averaging it away, so a fixed r = 11 lattice stayed visible at any frame count.
//    Measured: 121 isolated pools versus 1 when jittered, and a 30 % class-to-class bias that never decayed.
const float kSpatialRadiusMinPx = 4.0;       // [px] at 1280 px width — scaled by RenderWidth/1280 for resolution independence
const float kSpatialRadiusMaxPx = 16.0;      // [px] the band's mean matches the old fixed radius, so reuse reach is unchanged

const uint kMaterialTextureNone = 0xFFFFFFFFu;

#include "MaterialEvaluation.slang"

//------------------------------------------------------------------------------------------------------------------------
//                                          R4b: MATERIAL RESOLVE (slab + textures → ShadingRecord)
//------------------------------------------------------------------------------------------------------------------------
// Barycentrics are rebuilt from the hit position against the flat triangle (one convention for the raster-fed primary
//    hit and the CWBVH bounce hit). Vertex normals / tangents / UVs come from the shared VertexRecord buffer (binding 11/12)
//    through the instance's index range — the same data the R2 raster consumes, so nothing is uploaded twice.
// Texture LOD: ray cones (Akenine-Möller et al. 2019/2021). Cone width w = t · pixelSpread (primary) + t₂ · pixelSpread
//    (bounce, ⚠️ no curvature term yet — flat-surface spread only); λ = ½ log2(texW·texH · uvArea / worldArea) + log2(w / |n·d|).
// ⚠️ Texture channel selection is not part of MaterialSlabRecord; the shader assumes the glTF packing the codec emits:
//    metalness = B and roughness = G of the metallicRoughness image, occlusion = R, opacity = A of base colour, others RGB.

vec3 Barycentric(vec3 p, vec3 a, vec3 b, vec3 c)
{
    vec3 e0 = b - a, e1 = c - a, e2 = p - a;
    float d00 = dot(e0, e0), d01 = dot(e0, e1), d11 = dot(e1, e1), d20 = dot(e2, e0), d21 = dot(e2, e1);
    float den = d00 * d11 - d01 * d01;
    if (abs(den) < 1e-20) return vec3(1.0, 0.0, 0.0);
    float v = (d11 * d20 - d01 * d21) / den;
    float w = (d00 * d21 - d01 * d20) / den;
    return vec3(1.0 - v - w, v, w);
}

// Texture slot of channel C (0-15) of a slab, 0xFFFF = none — mirrors MaterialSlabTexture in SceneRecords.slang.
uint SlabTexture(GpuMaterialSlab S, uint C)
{
    uint Word = C < 8u ? S.TextureSlots0[C / 2u] : S.TextureSlots1[(C - 8u) / 2u];
    return (Word >> ((C & 1u) * 16u)) & 0xFFFFu;
}
const uint kChannelBaseColor = 0u, kChannelMetalness = 1u, kChannelRoughness = 2u, kChannelSpecularColor = 3u, kChannelNormal = 4u,
           kChannelCoatNormal = 5u, kChannelEmission = 6u, kChannelOpacity = 7u, kChannelCoat = 10u, kChannelFuzz = 11u,
           kChannelThinFilm = 12u, kChannelOcclusion = 14u;

struct SurfaceFrame         // everything the kernel needs at a hit besides the ShadingRecord
{
    vec3  Normal;           // shading normal (interpolated + normal map), facing the viewer side
    vec3  Tangent;          // orthonormal frame axes for the local BSDF space
    vec3  Bitangent;
    vec3  Geometric;        // face normal facing the viewer side (ray offsets, shadow rays)
    vec3  Emission;         // slab emission × texture [nit]
    float Opacity;          // geometry_opacity × texture alpha
    float Occlusion;        // baked AO — multiplies the ambient fills (debug floor, moonlight), never the direct light
    float LodBias;          // ½ log2(uvArea / worldArea) + log2(coneWidth / |n·d|): add ½ log2(texW·texH) per texture
};

vec3 ToLocal(SurfaceFrame F, vec3 w)   { return vec3(dot(w, F.Tangent), dot(w, F.Bitangent), dot(w, F.Normal)); }
vec3 FromLocal(SurfaceFrame F, vec3 w) { return F.Tangent * w.x + F.Bitangent * w.y + F.Normal * w.z; }

vec4 SampleChannel(GpuMaterialSlab S, uint channel, vec2 uv, float lodBias, vec4 fallback)
{
    uint slot = SlabTexture(S, channel);
    if (slot == 0xFFFFu) return fallback;
    ivec2 size = textureSize(Textures[nonuniformEXT(slot)], 0);
    float lod = lodBias + 0.5 * log2(float(size.x * size.y));
    return textureLod(Textures[nonuniformEXT(slot)], uv, lod);
}

// Resolve the material at a hit. `viewDir` points from the hit towards the viewer / previous vertex; `coneWidth` is the
//    ray-cone footprint at the hit [m]. Fills the ShadingRecord from the first slab (Tier A: one resolved slab) or, when
//    the material has no slab, from the 64 B header (constant base colour / roughness / metalness).
ShadingRecord ResolveMaterial(GpuMaterial mat, GpuInstance instance, uint primitive, vec3 hitPos, vec3 viewDir, float coneWidth, out SurfaceFrame F)
{
    GpuTriangle tri = Triangles[instance.FlatTriangleOffset + primitive];
    vec3 bc = Barycentric(hitPos, tri.v0.xyz, tri.v1.xyz, tri.v2.xyz);
    vec3 e1 = tri.v1.xyz - tri.v0.xyz, e2 = tri.v2.xyz - tri.v0.xyz;
    vec3 faceCross = cross(e1, e2);
    float worldArea = 0.5 * length(faceCross);
    vec3 ng = worldArea > 0.0 ? faceCross / (2.0 * worldArea) : vec3(0.0, 0.0, 1.0);
    if (dot(ng, viewDir) < 0.0) ng = -ng;

    // Vertex attributes through the instance's index range (object space → world with the instance matrix).
    uint base = instance.FirstIndex + primitive * 3u;
    GpuVertex va = Vertices[Indices[base + 0u] + instance.VertexOffset];
    GpuVertex vb = Vertices[Indices[base + 1u] + instance.VertexOffset];
    GpuVertex vc = Vertices[Indices[base + 2u] + instance.VertexOffset];
    mat3 world = mat3(instance.World);   // ⚠️ normals use the linear part directly (rigid / uniform scale); non-uniform scale is not corrected
    vec2 uv  = bc.x * va.Texcoord.xy + bc.y * vb.Texcoord.xy + bc.z * vc.Texcoord.xy;
    vec3 nI  = world * (bc.x * va.Normal.xyz + bc.y * vb.Normal.xyz + bc.z * vc.Normal.xyz);
    vec4 tI  = bc.x * va.Tangent + bc.y * vb.Tangent + bc.z * vc.Tangent;
    vec3 n   = dot(nI, nI) > 1e-12 ? normalize(nI) : ng;
    if (dot(n, ng) < 0.0) n = -n;                                   // interpolated normal follows the viewer-facing side
    vec3 t;
    if (dot(tI.xyz, tI.xyz) > 1e-12) t = world * tI.xyz;
    else                                                            // no authored tangents: derive from the UV parametrisation of the face
    {
        vec2 duv1 = vb.Texcoord.xy - va.Texcoord.xy, duv2 = vc.Texcoord.xy - va.Texcoord.xy;
        float det = duv1.x * duv2.y - duv2.x * duv1.y;
        t = abs(det) > 1e-12 ? (e1 * duv2.y - e2 * duv1.y) / det : (abs(n.z) < 0.999 ? cross(vec3(0.0, 0.0, 1.0), n) : vec3(1.0, 0.0, 0.0));
        tI.w = 1.0;
    }
    t = t - n * dot(n, t);
    t = dot(t, t) > 1e-12 ? normalize(t) : normalize(abs(n.z) < 0.999 ? cross(vec3(0.0, 0.0, 1.0), n) : vec3(1.0, 0.0, 0.0));
    float handed = tI.w < 0.0 ? -1.0 : 1.0;
    vec3 b = cross(n, t) * handed;

    // Ray-cone LOD bias (texture-size term added per fetch).
    vec2 duvA = vb.Texcoord.xy - va.Texcoord.xy, duvB = vc.Texcoord.xy - va.Texcoord.xy;
    float uvArea = 0.5 * abs(duvA.x * duvB.y - duvB.x * duvA.y);
    F.LodBias = (uvArea > 0.0 && worldArea > 0.0) ? 0.5 * log2(uvArea / worldArea) + log2(max(coneWidth, 1e-6) / max(abs(dot(ng, viewDir)), 1e-3)) : 0.0;

    ShadingRecord m;
    m.BaseColor = mat.Albedo.xyz; m.Metalness = mat.Emissive.w; m.DiffuseRoughness = 0.0;
    m.SpecularWeight = 1.0; m.SpecularColor = vec3(1.0); m.SpecularRoughness = mat.Albedo.w; m.SpecularAnisotropy = 0.0; m.SpecularIor = 1.5;
    m.ThinFilmWeight = 0.0; m.ThinFilmThickness = 0.5; m.ThinFilmIor = 1.4;
    m.HazinessWeight = 0.0; m.HazinessRoughness = 0.5;
    m.CoatWeight = 0.0; m.CoatColor = vec3(1.0); m.CoatRoughness = 0.0; m.CoatIor = 1.6; m.CoatDarkening = 1.0;
    m.FuzzWeight = 0.0; m.FuzzColor = vec3(1.0); m.FuzzRoughness = 0.5;
    m.Emission = mat.Emissive.xyz;
    F.Emission = mat.Emissive.xyz; F.Opacity = 1.0; F.Occlusion = 1.0;

    if (mat.Slabs.y > 0u)
    {
        GpuMaterialSlab S = MaterialSlabs[mat.Slabs.x];   // Tier A: the bottom slab only (multi-slab stacks arrive with R5)
        vec4 P0 = S.Payload[0], P1 = S.Payload[1], P2 = S.Payload[2], P3 = S.Payload[3];
        vec4 P8 = S.Payload[8], P9 = S.Payload[9], P10 = S.Payload[10], P11 = S.Payload[11], P12 = S.Payload[12], P13 = S.Payload[13];
        float normalScale = uintBitsToFloat(S.Runtime.y), occlusionStrength = uintBitsToFloat(S.Runtime.z);

        vec4 baseTex = SampleChannel(S, kChannelBaseColor, uv, F.LodBias, vec4(1.0));
        m.BaseColor          = P0.x * P0.yzw * baseTex.xyz;
        m.Metalness          = P1.x * SampleChannel(S, kChannelMetalness, uv, F.LodBias, vec4(1.0)).z;   // glTF: B
        m.DiffuseRoughness   = P1.y;
        m.SpecularWeight     = P1.z;
        m.SpecularColor      = vec3(P1.w, P2.xy) * SampleChannel(S, kChannelSpecularColor, uv, F.LodBias, vec4(1.0)).xyz;
        m.SpecularRoughness  = P2.z * SampleChannel(S, kChannelRoughness, uv, F.LodBias, vec4(1.0)).y;   // glTF: G
        m.SpecularAnisotropy = P2.w;
        m.SpecularIor        = P3.x;
        m.CoatWeight         = P8.y * SampleChannel(S, kChannelCoat, uv, F.LodBias, vec4(1.0)).x;
        m.CoatColor          = vec3(P8.zw, P9.x);
        m.CoatRoughness      = P9.y;
        m.CoatIor            = P9.w;
        m.CoatDarkening      = P10.x;
        m.FuzzWeight         = P10.y;
        m.FuzzColor          = vec3(P10.zw, P11.x) * SampleChannel(S, kChannelFuzz, uv, F.LodBias, vec4(1.0)).xyz;
        m.FuzzRoughness      = P11.y;
        m.Emission           = P11.z * vec3(P11.w, P12.xy) * SampleChannel(S, kChannelEmission, uv, F.LodBias, vec4(1.0)).xyz;
        m.ThinFilmWeight     = P12.z * SampleChannel(S, kChannelThinFilm, uv, F.LodBias, vec4(1.0)).x;
        m.ThinFilmThickness  = P12.w;
        m.ThinFilmIor        = P13.x;
        m.HazinessWeight     = P13.z;
        m.HazinessRoughness  = P13.w;
        F.Emission  = m.Emission;
        F.Opacity   = P13.y * SampleChannel(S, kChannelOpacity, uv, F.LodBias, vec4(1.0)).w;   // glTF: A of base colour
        F.Occlusion = mix(1.0, SampleChannel(S, kChannelOcclusion, uv, F.LodBias, vec4(1.0)).x, occlusionStrength);

        uint normalSlot = SlabTexture(S, kChannelNormal);
        if (normalSlot != 0xFFFFu)
        {
            vec3 tn = SampleChannel(S, kChannelNormal, uv, F.LodBias, vec4(0.5, 0.5, 1.0, 1.0)).xyz * 2.0 - 1.0;
            tn.x *= normalScale; tn.y *= normalScale;
            vec3 nm = normalize(t * tn.x + b * tn.y + n * tn.z);
            if (dot(nm, ng) > 0.0) n = nm;                         // reject maps that push the normal below the surface
            t = normalize(t - n * dot(n, t)); b = cross(n, t) * handed;
        }
    }

    F.Normal = n; F.Tangent = t; F.Bitangent = b; F.Geometric = ng;
    return m;
}

#include "TraversalCWBVH.slang"

const uint kVisibilityInvalid = 0xFFFFFFFFu;
uint VisibilityInstance(uint Packed)  { return (Packed >> 14) & 0x3FFFFu; }
uint VisibilityPrimitive(uint Packed) { return Packed & 0x3FFFu; }

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUSH CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

layout(push_constant) uniform ReSTIRConstants
{
    vec3    CameraOrigin;           // [m]   camera world position
    float   FieldOfViewTanHalf;     // [-]   tan(FoV/2)
    vec3    CameraForward;          // [-]   forward unit vector
    float   AspectRatio;            // [-]   width / height
    vec3    CameraRight;            // [-]   right unit vector
    float   Exposure;               // [-]   ACES exposure
    vec3    CameraUp;               // [-]   up unit vector
    float   AmbientStrength;        // [-]   ambient fallback
    uint    ViewportWidth;          // [px]  render width
    uint    ViewportHeight;         // [px]  render height
    uint    FrameIndex;             // [cnt] temporal accumulation index
    uint    ExtraCandidateCount;  // [cnt] extra same-pixel RIS candidates (R6 row 3: renamed; true spatial reuse is the fixed kSpatialTaps cross)
    uint    SamplesPerPixel;        // [cnt] primary DI candidates per pixel
    uint    AlphaMaskedMaterialCount; // [cnt] R4b: materials flagged kMaterialFlagAlphaMask (0 ⇒ any-hit shadow rays) — was TriangleCount (unused here)
    uint    LightTriangleCount;     // [cnt] emissive triangles for DI sampling
    uint    FeatureFlags;           // [bit] kFeatureGlobalIllumination | kFeatureAntiAliasing
    // Push constants stay within 128 bytes, which is Vulkan’s GUARANTEED MINIMUM. Growing past it works on a
    //    card offering 256 and fails on one offering only the guarantee — a defect that appears on someone
    //    else’s machine. Anything that does not fit goes into a uniform buffer rather than growing this block.
    // A7d. How much colour the eye still has at the adapted luminance: 1 in daylight, 0 under starlight.
    float   ColourSaturation;       // [-]   taken from one of the reserves, so the block is still 128 B
    uint    SpatialTapCount;        // [cnt] R10: spatial-reuse neighbours per pixel, tier-keyed (0 = cross off)
    uint    PushReserve2;
    uint    PushReserve3;
    uint    PushReserve4;
    uint    PushReserve5;
    uint    PushReserve6;
    uint    PushReserve7;
};

const uint kFeatureGlobalIllumination = 1u;   // bit 0: trace the secondary bounce (section 4)
const uint kFeatureAntiAliasing       = 2u;   // bit 1: sub-pixel jitter — applied by the R2 raster/resolve, not here
const uint kFeatureAmbientFloor       = 4u;   // bit 2: add albedo × AmbientStrength (debug fill light; default OFF)
const uint kFeatureTemporalReuse      = 8u;   // bit 3: R6 row 2 — temporal reservoir reuse (back-projection + validation)
const uint kFeatureSpatialReuse       = 16u;  // bit 4: R6 row 3 — spatiotemporal neighbour reuse (pairwise MIS)
const uint kFeatureAliasPick          = 32u;  // bit 5: R6 row 3 — Walker-alias light pick (off = uniform, the R0 identity proof)
const uint kFeatureTemporalReprojection = 64u;  // bit 6: R7a — reproject the running mean through the R2 motion vectors
const uint kFeatureDenoise            = 128u; // bit 7: R7 — the à-trous filter runs, so the kernel defers the tone map to it

float PixelSpreadAngle() { return 2.0 * FieldOfViewTanHalf / float(ViewportHeight); }   // [rad] R4b ray-cone spread per pixel

//------------------------------------------------------------------------------------------------------------------------
//                                                    PCG RANDOM
//------------------------------------------------------------------------------------------------------------------------

uint PcgHash(uint v)
{
    uint state = v * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float RandFloat(inout uint seed)
{
    seed = PcgHash(seed);
    return float(seed) * (1.0 / 4294967296.0);
}

uint MakeSeed(uvec2 pixel, uint frame)
{
    return PcgHash(pixel.x + pixel.y * 7919u + frame * 1000003u);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    RAY / HIT STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

const uint kMaterialFlagAlphaMask = 2u;      // MaterialRecord.Flags bit (SceneRecords.slang)
const uint kAlphaMaskRetraceLimit = 4u;      // cut-out layers a shadow / bounce ray may pass through before giving up
struct Ray    { vec3 origin; vec3 dir; };
struct Hit    { bool valid; float t; vec3 pos; vec3 normal; uint matIdx; uint primitive; uint instance; };

const float kEps = 1e-4;
const float kInf = 1e30;

//------------------------------------------------------------------------------------------------------------------------
//                                                    SCENE TRAVERSAL
//------------------------------------------------------------------------------------------------------------------------

// R4b alpha mask: true when the hit on `primitive` is a cut-out texel (kMaterialFlagAlphaMask materials only). Opacity =
//    geometry_opacity × base-colour alpha at LOD 0 through the flat triangle's own UVs — no vertex fetch on this path.
bool AlphaMaskCutOut(uint primitive, vec3 hitPos)
{
    GpuTriangle tri = Triangles[primitive];
    GpuMaterial mat = Materials[floatBitsToUint(tri.v0.w)];
    if ((mat.Slabs.z & kMaterialFlagAlphaMask) == 0u) return false;
    float opacity = 1.0;
    if (mat.Slabs.y > 0u) opacity = MaterialSlabs[mat.Slabs.x].Payload[13].y;   // geometry_opacity
    if (mat.Textures.x != kMaterialTextureNone)
    {
        vec3 bc = Barycentric(hitPos, tri.v0.xyz, tri.v1.xyz, tri.v2.xyz);
        vec2 uv = bc.x * tri.uv.xy + bc.y * tri.uv.zw + bc.z * vec2(tri.v1.w, tri.v2.w);
        opacity *= textureLod(Textures[nonuniformEXT(mat.Textures.x)], uv, 0.0).a;
    }
    return opacity < mat.AlphaCutoff;
}

//------------------------------------------------------------------------------------------------------------------------
//                                        HIT IDENTITY  (D2 — one definition, one place)
//------------------------------------------------------------------------------------------------------------------------
// A hit is identified by the PAIR (instance, primitive). Today Triangles[] is one flat world-space array and the
//    traversal returns a flat slot, so the instance is recovered from the layout. Once instances carry transforms
//    (D3) the traversal will return the pair directly and only these two helpers change — every caller already
//    speaks in terms of the pair, so nothing downstream has to be revisited.
//
// ⚠️ Why this is worth a helper rather than an inline loop: the search below used to be copy-pasted at the bounce
//    site. Two copies of an identity rule is how (instance, primitive) silently disagree between the primary hit
//    and the bounce hit — and because ReSTIR reuses hits across frames, a disagreement decorrelates the reservoir
//    instead of crashing. One definition removes that whole class of bug.

// Instance owning a flat primitive slot. Triangles[] is laid out per instance in FlatTriangleOffset order —
//    SceneStructure::Finalise assigns each offset from the size of a growing vector, walking instances in order,
//    so the sequence is non-decreasing — and the answer is the LAST instance whose offset does not exceed the
//    slot. That is a sorted search, so it is done in log₂(N) steps rather than by walking.
//
//    Why "last" and not "first": an instance with no triangles repeats its predecessor's offset, and the walk it
//    replaces kept advancing over equal offsets, so it settled on the last of a run. This binary search converges
//    to the same element by moving `low` on every `<=`, which keeps empty instances resolving exactly as before
//    instead of handing the primitive to whichever duplicate the search happened to land on first.
//
//    Cornell has two instances, where this is a wash. It is O(instances) per ray hit that matters: every closest
//    hit on every candidate and bounce ray pays it, so on a real scene the walk is a per-ray tax proportional to
//    scene complexity.
uint InstanceOfPrimitive(uint flatPrimitive)
{
    uint low  = 0u;
    uint high = uint(Instances.length()) - 1u;      // Instances is never empty: a hit implies geometry
    while (low < high)
    {
        // Bias the midpoint UP so that `low = mid` always advances and the loop cannot stall on high == low + 1.
        uint mid = low + (high - low + 1u) / 2u;
        if (Instances[mid].FlatTriangleOffset <= flatPrimitive) low  = mid;
        else                                                    high = mid - 1u;
    }
    return low;
}

// Primitive index WITHIN its instance — what ResolveMaterial expects for vertex/uv lookup.
uint LocalPrimitive(uint flatPrimitive, uint instance)
{
    return flatPrimitive - Instances[instance].FlatTriangleOffset;
}

Hit TraceScene(Ray ray)
{
    // R3: closest hit through the CWBVH; the primitive index addresses the flat Triangles[] for material and normal.
    // R4b: a hit on a cut-out texel of an alpha-masked material is skipped and the ray re-traced from just past it.
    Hit closest;
    closest.valid = false;
    closest.t     = kInf;

    vec3 rD = vec3(1.0) / ray.dir;   // IEEE ±inf on axis-aligned rays is handled by the slab test exactly as in tinybvh
    vec3  origin = ray.origin;
    float tBase  = 0.0;
    uint  passes = AlphaMaskedMaterialCount == 0u ? 0u : kAlphaMaskRetraceLimit;
    for (uint pass = 0u; pass <= passes; ++pass)
    {
        TraversalHit h = TraverseClosest(origin, ray.dir, rD, kInf);
        if (h.primitive == 0xFFFFFFFFu || h.t <= kEps) return closest;
        vec3 pos = origin + h.t * ray.dir;
        if (passes != 0u && AlphaMaskCutOut(h.primitive, pos)) { origin = pos + ray.dir * kEps; tBase += h.t + kEps; continue; }

        GpuTriangle tri = Triangles[h.primitive];
        closest.valid  = true;
        closest.t      = tBase + h.t;
        closest.pos    = pos;
        closest.normal = normalize(cross(tri.v1.xyz - tri.v0.xyz, tri.v2.xyz - tri.v0.xyz));   // R4a: face normal from the edges (slot now carries UVs)
        closest.matIdx = floatBitsToUint(tri.v0.w);
        closest.primitive = h.primitive;
        closest.instance  = InstanceOfPrimitive(h.primitive);   // D2: identity is the (instance, primitive) pair
        return closest;
    }
    return closest;
}

bool TraceShadow(vec3 origin, vec3 target)
{
    vec3 dir = target - origin;
    float tMax = length(dir) - 2.0 * kEps;
    dir = normalize(dir);
    vec3 rD = vec3(1.0) / dir;
    if (AlphaMaskedMaterialCount == 0u)
        return TraverseOccluded(origin + dir * kEps, dir, rD, tMax);   // R3 fast path: any-hit, no material fetch (Cornell unchanged)

    // R4b: closest-hit walk that steps through cut-out texels (foliage cards); an opaque hit before the light occludes.
    vec3 o = origin + dir * kEps;
    for (uint pass = 0u; pass <= kAlphaMaskRetraceLimit; ++pass)
    {
        TraversalHit h = TraverseClosest(o, dir, rD, tMax);
        if (h.primitive == 0xFFFFFFFFu) return false;
        vec3 pos = o + h.t * dir;
        if (!AlphaMaskCutOut(h.primitive, pos)) return true;
        o = pos + dir * kEps; tMax -= h.t + kEps;
        if (tMax <= 0.0) return false;
    }
    return true;   // more cut-out layers than the limit: treat as occluded (conservative, never leaks light)
}

//------------------------------------------------------------------------------------------------------------------------
//                                          COSINE-WEIGHTED HEMISPHERE SAMPLING
//------------------------------------------------------------------------------------------------------------------------

vec3 SampleCosineHemisphere(vec3 normal, float u1, float u2)
{
    float r     = sqrt(u1);
    float theta = 6.28318530718 * u2;
    float x     = r * cos(theta);
    float y     = r * sin(theta);
    float z     = sqrt(max(0.0, 1.0 - u1));

    vec3 up      = abs(normal.y) < 0.999 ? vec3(0,1,0) : vec3(1,0,0);
    vec3 tangent = normalize(cross(up, normal));
    vec3 bitan   = cross(normal, tangent);

    return normalize(tangent * x + bitan * y + normal * z);
}

//------------------------------------------------------------------------------------------------------------------------
//                             LIGHT TRIANGLE AREA SAMPLING (uniform area pick)
//------------------------------------------------------------------------------------------------------------------------

// Light triangles come from the luminaire table (R2), which carries a Walker alias entry + area-power pdf per
//    luminaire — R6 row 3 picks through it (PickLight) for many-light scenes. Uniform stays behind the F5 toggle
//    for the R0 identity proof.
uint LightTriangle(uint lightIdx) { return Luminaires[lightIdx].TriangleSlot; }

vec3 SampleLightPoint(uint lightTriIdx, float u1, float u2)
{
    uint idx = LightTriangle(lightTriIdx);
    vec3 v0 = Triangles[idx].v0.xyz;
    vec3 v1 = Triangles[idx].v1.xyz;
    vec3 v2 = Triangles[idx].v2.xyz;
    float r1 = sqrt(u1);
    return (1.0 - r1) * v0 + r1 * (1.0 - u2) * v1 + r1 * u2 * v2;
}

//------------------------------------------------------------------------------------------------------------------------
//                                          LIGHT EMISSION + POSITION ACCESSORS
//------------------------------------------------------------------------------------------------------------------------
// Every read of a light's emission or position goes through these two accessors, so the shading, reservoir and
//    reuse paths cannot disagree about what a light is.

vec3 LightEmission(uint lightIndex)
{
    return Materials[floatBitsToUint(Triangles[LightTriangle(lightIndex)].v0.w)].Emissive.xyz;
}

vec3 SampleLightPointFor(uint lightIndex, float u1, float u2)
{
    return SampleLightPoint(lightIndex, u1, u2);
}

// Face normal of a light triangle, winding side — the emitter cosine (CPU parity: VisibilityRaster's LdotL).
//    A luminaire is single-sided: a sample seen from behind its face emits nothing.
vec3 LightNormal(uint lightIndex)
{
    uint idx = LightTriangle(lightIndex);
    vec3 e1 = Triangles[idx].v1.xyz - Triangles[idx].v0.xyz;
    vec3 e2 = Triangles[idx].v2.xyz - Triangles[idx].v0.xyz;
    vec3 n = cross(e1, e2);
    float len = length(n);
    return len > 0.0 ? n / len : vec3(0.0, 0.0, 1.0);
}

// ── The sun as a direct light ──────────────────────────────────────────────────────────────────────────
// The light set used to be emissive mesh triangles only: sunlight entered through the sky integral on escaped
//    rays plus the flat indoor fill, so no surface ever caught direct sun and nothing cast a sun shadow. The sun
//    now joins the reservoir as a first-class candidate — a distant disc sampled uniformly in solid angle,
//    shadowed by the same TraceShadow every mesh sample takes. It rides the SAME reservoir (not a side
//    estimator), so temporal/spatial reuse, M-clamp and W apply to sunlight unchanged — the reservoir algebra
//    only ever asks w = p̂/p, and both light kinds now answer it exactly.
const float kSunPickProbability = 0.5;   // when sun and lamps coexist, each candidate flips this coin
const uint  kSunLightIndex      = 0xFFFFFFFFu;   // SelectedLight sentinel: the sample is a sun direction, not a triangle
const float kSunShadowDistance  = 1.0e4;   // [m] the far point sun samples are stored as — past any scene, inside float range

// Solid angle of the solar disc. Half-angle form, not 2π(1−cosθ): at 0.0046 rad the subtraction cancels to a
//    1 % error in float, and this Ω divides every sun weight — the bias would land directly on the sunlight.
float SunSolidAngle()
{
    const float kPi = 3.14159265358979323846;
    float s = sin(kSunAngularRadius * 0.5);
    return 4.0 * kPi * s * s;
}

// Uniform direction within the sun's disc cone (pdf 1/Ω). The basis excludes the dominant axis, so it cannot
//    degenerate wherever the sun stands — zenith included.
vec3 SampleSunDirection(vec3 sunDir, float u1, float u2)
{
    float cosMax = cos(kSunAngularRadius);
    float cosT = mix(1.0, cosMax, u1);
    float sinT = sqrt(max(0.0, 1.0 - cosT * cosT));
    float phi = 6.28318530718 * u2;
    vec3 up = abs(sunDir.x) < 0.9 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
    vec3 t = normalize(cross(up, sunDir));
    vec3 b = cross(sunDir, t);
    return normalize(t * (sinT * cos(phi)) + b * (sinT * sin(phi)) + sunDir * cosT);
}

// Effective disc radiance: the record carries the panel's direct-sun factor Q (0.11·gain·colour·T), and the
//    estimator needs radiance — L·Ω must equal the panel's formula, so L = Q/Ω. One Ω, from the shared radius,
//    which is also why the sun's d² is 1: the far point is a shadow-ray target, not a distance.
vec3 SunEmission()
{
    return SkySunDirect.xyz / SunSolidAngle();
}

// R6 row 3: light-triangle pick. Alias ON (F5, default): one Walker-alias probe — col uniform, accept on
//    Threshold else redirect to AliasSlot, source pdf = Probability[z] (power/total, SceneStructure.cpp).
//    Alias OFF: uniform over luminaires with p = 1/N — bit-for-bit the R0 estimator (identity proof).
//    Any positive pSource keeps RIS unbiased: the converged image is unchanged, only the noise differs.
uint PickLight(inout uint seed, out float pSource)
{
    if ((FeatureFlags & kFeatureAliasPick) != 0u)
    {
        uint col = uint(RandFloat(seed) * float(LightTriangleCount));
        col = min(col, LightTriangleCount - 1u);
        uint z = RandFloat(seed) < Luminaires[col].Threshold ? col : Luminaires[col].AliasSlot;
        pSource = Luminaires[z].Probability;
        return z;
    }
    pSource = 1.0 / float(LightTriangleCount);
    return min(uint(RandFloat(seed) * float(LightTriangleCount)), LightTriangleCount - 1u);
}

//------------------------------------------------------------------------------------------------------------------------
//                                  RIS DIRECT LIGHTING — WEIGHTED RESERVOIR SAMPLING (initial candidates)
//------------------------------------------------------------------------------------------------------------------------

// R6 row 2: full-BSDF target — f(wo,wi)·Le·cosθ·cosφ/d², both cosines. The emitter cosine is the CPU raster's
//    LdotL: a lamp quad is single-sided, and without it the room's edges catch light the fixture never threw.
//    Any positive target keeps RIS unbiased (converged image unchanged); the BSDF-weighted one is required for
//    correct re-evaluation at foreign pixels on reuse.
float PHatFull(SurfaceFrame F, ShadingRecord m, ResolvedLayers L, vec3 wo, vec3 emit, vec3 toLight, float dist2, vec3 lightNormal)
{
    float dist = max(sqrt(dist2), 1e-9);
    vec3  lDir = toLight / dist;
    float cosT = max(0.0, dot(F.Geometric, lDir));
    if (cosT <= 0.0) return 0.0;
    float cosL = max(0.0, dot(lightNormal, -lDir));
    if (cosL <= 0.0) return 0.0;
    vec3  wi = ToLocal(F, lDir);
    if (wi.z <= 0.0) return 0.0;
    return max(dot(EvaluateBsdf(m, L, wo, wi), emit), 0.0) * cosT * cosL / (dist2 + 0.001);
}

// Sun target: f·L·cos — the sun is at infinity, so no d². L is the record's attenuated direct radiance, which
//    is zero at night and when the sun hides, so dead sun candidates carry p̂ = 0 and can never be selected.
float PHatSun(SurfaceFrame F, ShadingRecord m, ResolvedLayers L, vec3 wo, vec3 hitPos,
              vec3 sunEmit, vec3 sunDir)
{
    float cosT = max(0.0, dot(F.Geometric, sunDir));
    if (cosT <= 0.0) return 0.0;
    vec3 wi = ToLocal(F, sunDir);
    if (wi.z <= 0.0) return 0.0;
    // Cloud transmittance belongs in the RIS target, not only in the final shade. Otherwise reservoir reuse
    //    selects using an unshadowed p̂ but evaluates a cloud-shadowed contribution, which biases the sun.
    return max(dot(EvaluateBsdf(m, L, wo, wi), sunEmit), 0.0) * cosT * CloudSunTransmittance(hitPos, sunDir);
}

// p̂ of a STORED sample — the one question every merge asks, answered for both light kinds. A sun sample's
//    direction is rebuilt from its far point, exactly: the point is 1e4 m out, so the rebuild differs from the
//    sampled direction by ~1e-4 rad — inside the disc, where the pdf is still 1/Ω.
float PHatSelected(SurfaceFrame F, ShadingRecord m, ResolvedLayers L, vec3 wo, vec3 hitPos,
                  uint light, vec3 toLight)
{
    if (light == kSunLightIndex)
        return PHatSun(F, m, L, wo, hitPos, SunEmission(), normalize(toLight));
    return PHatFull(F, m, L, wo, LightEmission(light), toLight, dot(toLight, toLight), LightNormal(light));
}

struct Reservoir
{
    vec3  SelectedPoint;    // chosen light sample position
    float WeightSum;        // running sum of candidate weights (Σ p̂/p_source)
    uint  SampleCount;      // M: number of considered candidates
    float UnbiasedWeight;   // W: final unbiased contribution weight (WeightSum / (M · p̂(z)))
    uint  SelectedLight;    // R6: luminaire-table index of the chosen sample (kills the O(L) nearest-emissive searches)
    vec2  SelectedUv;       // R6: area-sample uv of the chosen sample on its triangle (p̂ re-evaluation on reuse)
    uint  Visible;          // R6: visibility of the chosen sample at the shading pixel (1 = lit; re-traced on reuse)
};

void ResampleCandidate(inout Reservoir res, vec3 point, uint light, vec2 uv, float weight, inout uint seed)
{
    res.WeightSum    += weight;
    res.SampleCount  += 1u;
    if (RandFloat(seed) * res.WeightSum <= weight)
    {
        res.SelectedPoint = point;
        res.SelectedLight = light;
        res.SelectedUv    = uv;
    }
}

// One direct-light candidate, sun or lamp — the initial loop and the extras loop both draw through here, so the
//    two cannot drift into different estimators. Weights answer w = p̂/p EXACTLY, both continuous pdfs included:
//    a mesh sample's pdf is p_coin·p_light·(1/area) and a sun sample's is p_coin·(1/Ω). The kernel used to divide
//    by the pick alone, which paid every lamp's contribution divided by its area (the Cornell lamp rendered 2.5x
//    too bright) — the CPU raster never had this bug (Tap.Weight carries the area), and the SunDirectProof pins
//    the corrected scale against quadrature. HitPos anchors the sun's far point; SunUp (any direct radiance)
//    decides whether the coin is flipped at all.
void DrawDirectCandidate(SurfaceFrame F, ShadingRecord m, ResolvedLayers L, vec3 wo, vec3 hitPos, bool sunUp,
                         out vec3 point, out uint light, out vec2 uv, out float weight, inout uint seed)
{
    bool sun = sunUp && (LightTriangleCount == 0u || RandFloat(seed) < kSunPickProbability);
    if (sun)
    {
        float u1 = RandFloat(seed);
        float u2 = RandFloat(seed);
        vec3 sunDir = SampleSunDirection(SkySunDirection.xyz, u1, u2);
        point  = hitPos + sunDir * kSunShadowDistance;
        light  = kSunLightIndex;
        uv     = vec2(u1, u2);
        float pHat = PHatSun(F, m, L, wo, hitPos, SunEmission(), sunDir);
        float pPick = LightTriangleCount > 0u ? kSunPickProbability : 1.0;
        weight = pHat * SunSolidAngle() / pPick;
    }
    else
    {
        float  pSource;
        uint   li = PickLight(seed, pSource);
        float  u1 = RandFloat(seed);
        float  u2 = RandFloat(seed);
        point = SampleLightPointFor(li, u1, u2);
        light = li;
        uv    = vec2(u1, u2);
        vec3 toLight = point - hitPos;
        float pHat = PHatFull(F, m, L, wo, LightEmission(li), toLight, dot(toLight, toLight), LightNormal(li));
        float pPick = sunUp ? 1.0 - kSunPickProbability : 1.0;
        weight = pHat * Luminaires[li].Area / (pSource * pPick);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                              ACES FILMIC TONE MAPPING
//------------------------------------------------------------------------------------------------------------------------

// Rec. 709 luma. Must match AtrousDenoise.slang exactly: the kernel measures the variance of THIS quantity and the
//    filter compares neighbours by it, so a mismatch would make the filter's noise estimate describe a different
//    signal from the one it is filtering.
float Luminance(vec3 colour)
{
    return dot(colour, vec3(0.2126, 0.7152, 0.0722));
}

float AcesFilm(float x)
{
    float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 ToneMap(vec3 hdr)
{
    // A7d ⚠️ Desaturate BEFORE the exposure and the curve, in linear radiance, because that is where the colour
    //    physically is. Doing it afterwards would mix display values, and a channel that had already clipped
    //    would drag the grey it is mixed toward.
    //
    //    Below about 3 cd/m² the cones give out before the rods do, so colour drains from what the eye sees and
    //    by 0.003 it is gone. Rendering full saturation down there is what turns a faint pre-dawn glow into a
    //    lurid orange band: at 15° below the horizon the glow is 0.078 cd/m² and almost entirely red, so red
    //    clips while blue stays black. Mixed to grey it reads as the pale band it actually is.
    const float Grey = dot(hdr, vec3(0.2126, 0.7152, 0.0722));
    hdr = mix(vec3(Grey), hdr, clamp(ColourSaturation, 0.0, 1.0));
    hdr *= Exposure;
    float r = AcesFilm(hdr.r);
    float g = AcesFilm(hdr.g);
    float b = AcesFilm(hdr.b);
    // gamma 2.2
    return pow(vec3(r, g, b), vec3(1.0 / 2.2));
}

//------------------------------------------------------------------------------------------------------------------------
//                                              TEMPORAL ACCUMULATION RESOLVE
//------------------------------------------------------------------------------------------------------------------------
// Running mean in linear radiance, REPROJECTED through the R2 motion vectors (R7a).
//
// Before R7a the mean was read from the pixel's own address and the whole history was thrown away the instant the
//    camera moved: the image went to one sample per pixel while moving and only resolved once the camera stopped.
//    The motion vectors needed to do better already existed — R2 writes them and R6 back-projects temporal
//    reservoirs through them — they simply were not consumed by the accumulator. This reads the history from where
//    the surface WAS, so a pan keeps its samples.
//
// Validation is deliberately the same rule R6 row 2 uses for reservoirs — 25° normal, 10 % relative depth — rather
//    than a second, subtly different one. A pixel that fails it is disocclusion: new geometry with no history, and
//    the honest answer there is to restart rather than smear a neighbour's colour across the silhouette.
//
// The history's alpha channel carries the sample count. Reprojection preserves it, so a surface tracked across a
//    pan keeps converging instead of restarting at n = 1; disocclusion resets it to 1. HistorySurfaceImage stores the
//    matching (normal, depth) so the next frame can validate against what this frame actually shaded.

const float kReprojectNormalCos = kTemporalNormalCos;   // 25°, identical to the reservoir rule
const float kReprojectDepthTol  = kTemporalDepthTol;    // 10 % relative

// Depth < 0 marks "no surface" (background); such a pixel never reprojects and never becomes a valid source.
void ResolveSurface(uvec2 pixel, vec3 radiance, vec3 normal, float depth)
{
    vec4  history   = vec4(0.0);
    float count     = 0.0;
    bool  resolved  = false;   // did we already establish where this pixel's history comes from?

    const bool reproject = depth > 0.0 && (FeatureFlags & kFeatureTemporalReprojection) != 0u;

    vec2 moments = vec2(0.0);   // R7: x = E[luma], y = E[luma²], carried alongside the mean

    if (FrameIndex > 0u && reproject)
    {
        vec2 extent = vec2(float(ViewportWidth), float(ViewportHeight));
        vec2 cuv    = (vec2(pixel) + 0.5) / extent;
        vec2 motion = texelFetch(MotionImage, ivec2(pixel), 0).rg;
        ivec2 prevPx = ivec2(floor((cuv - motion) * extent));

        if (all(greaterThanEqual(prevPx, ivec2(0))) && prevPx.x < int(ViewportWidth) && prevPx.y < int(ViewportHeight))
        {
            vec4 prevSurface = imageLoad(HistorySurfaceImage, prevPx);
            // prevSurface.w <= 0 means that pixel held no surface last frame, so there is nothing to inherit.
            if (prevSurface.w > 0.0
                && dot(normal, prevSurface.xyz) > kReprojectNormalCos
                && abs(depth - prevSurface.w) / max(depth, 1e-3) < kReprojectDepthTol)
            {
                history  = imageLoad(HistoryImage, prevPx);
                count    = history.a;
                moments  = imageLoad(MomentImage, prevPx).xy;   // the moments follow the same reprojection
                resolved = true;
            }
            // Failing validation is a real disocclusion: leave count at 0 so the mean restarts at n = 1 rather
            //    than smearing the surface that used to be behind this pixel.
            else resolved = true;
        }
        else resolved = true;   // back-projected off screen — also a disocclusion
    }

    if (FrameIndex > 0u && !resolved)
    {
        // Either reprojection is disabled, or this pixel has no surface to reproject through (background, direct
        //    emitters). Both read the pixel's own history, which is exactly the pre-R7a behaviour — so with the
        //    feature bit clear this function is byte-identical to the accumulator it replaced.
        history = imageLoad(HistoryImage, ivec2(pixel));
        count   = history.a;
        moments = imageLoad(MomentImage, ivec2(pixel)).xy;
    }

    // The lens's answer to the sun, added to the SAMPLE before the running mean reads it — every frame's sample
    //    carries exactly one flare, so the mean converges to E[radiance] + flare, bounded at any frame count.
    //    (It used to be added to the MEAN after the update, which stored the flare into the history and added a
    //    fresh one every frame — the flare grew as (n+1)/2, flooding the frame white within seconds and burying
    //    the disc. Analytic, so no variance is booked for it: a constant offset leaves E[x²]−E[x]² unchanged.)
    {
        vec2 extent = vec2(float(ViewportWidth), float(ViewportHeight));
        radiance += FlareAlong((vec2(pixel) + 0.5) / extent, extent.x / max(extent.y, 1.0));
    }

    count      = count + 1.0;
    vec3 mean  = history.rgb + (radiance - history.rgb) / count;

    // R7: the same incremental mean applied to the first two luminance moments. Variance is E[x²] − E[x]², which is
    //    the noise estimate the à-trous filter divides by; a pixel with many samples reports a small variance and
    //    is therefore barely filtered, which is how the denoiser fades out as the image converges.
    float luma = Luminance(radiance);
    moments   += (vec2(luma, luma * luma) - moments) / count;
    // Clamped at zero: with one sample the estimator is exactly zero and float error can push it slightly negative.
    float sampleVariance = max(moments.y - moments.x * moments.x, 0.0);

    // ⚠️ The image being filtered is the MEAN of n samples, not one sample, so the noise the filter must reason
    //    about is the variance OF THE MEAN — the sample variance divided by n. Handing it the raw sample variance
    //    reports a noise level that never falls as the estimate converges, so the filter stays maximally
    //    permissive forever and keeps blurring an image that no longer needs it.
    float variance = sampleVariance / count;

    // A single sample carries no information about its own spread. Reporting its luminance squared keeps the
    //    filter permissive on brand-new pixels (disocclusions), which is exactly where smoothing is needed most.
    if (count < 2.0) variance = luma * luma;

    imageStore(HistoryImage,     ivec2(pixel), vec4(mean, count));
    imageStore(HistorySurfaceImage, ivec2(pixel), vec4(normal, depth));
    imageStore(MomentImage,      ivec2(pixel), vec4(moments, 0.0, 0.0));

    // With the denoiser on, the filter owns the tone map: it needs LINEAR radiance, and tone mapping before a blur
    //    would average display-space values and darken every gradient. The final à-trous level writes OutputImage.
    imageStore(DenoiseImage, ivec2(pixel), vec4(mean, variance));
    if ((FeatureFlags & kFeatureDenoise) == 0u)
        imageStore(OutputImage, ivec2(pixel), vec4(ToneMap(mean), 1.0));
}

// Background and other surface-less writes: no normal, no depth, never a reprojection source.
void Resolve(uvec2 pixel, vec3 radiance)
{
    ResolveSurface(pixel, radiance, vec3(0.0, 0.0, 1.0), -1.0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MAIN COMPUTE KERNEL
//------------------------------------------------------------------------------------------------------------------------

void main()
{
    uvec2 pixel = gl_GlobalInvocationID.xy;
    if (pixel.x >= ViewportWidth || pixel.y >= ViewportHeight) return;

    uint seed = MakeSeed(pixel, FrameIndex);

    //------------------------------------------------------------------------------------------------------------------
    // 1 + 2. Primary visibility — read from the R2 visibility raster instead of tracing. SurfaceResolve intersected the
    //    pixel's camera ray (with the frame's raster jitter) with the rasterised triangle, so hitPos / hitNormal are
    //    what TraceScene used to return, already flipped to face the camera. ⚠️ Deviation from R0: anti-aliasing jitter
    //    is now one Halton offset per FRAME (shared by raster and resolve) instead of one PCG offset per pixel; the
    //    accumulated result converges to the same box-filtered image, only the per-frame noise pattern differs.
    //------------------------------------------------------------------------------------------------------------------
    vec4 surface    = imageLoad(SurfaceImage, ivec2(pixel));
    uint visibility = floatBitsToUint(surface.w);

    if (visibility == kVisibilityInvalid)
    {
        // A missed primary ray sees the sky. This used to contribute nothing, which was correct only while the
        //    renderer had no environment light — with a real atmosphere it is the background of every outdoor
        //    frame. SkyAlong returns black when the sky is disabled, so the old behaviour is still reachable.
        // The kernel reads pre-resolved surfaces and never builds a primary ray, so the direction for a MISSED
        //    pixel has to be reconstructed here from the same camera basis SurfaceResolve used.
        vec2  extent = vec2(float(ViewportWidth), float(ViewportHeight));
        vec2  ndc    = ((vec2(pixel) + 0.5) / extent) * 2.0 - 1.0;
        float aspect = extent.x / max(extent.y, 1.0);
        vec3  direction = normalize(CameraForward
                                  + CameraRight * (ndc.x * FieldOfViewTanHalf * aspect)
                                  - CameraUp    * (ndc.y * FieldOfViewTanHalf));
        // A missed ray crosses the whole rain column, so it earns the full bow (the large distance saturates
        //    the path fraction exactly as the model prescribes for the sky).
        Resolve(pixel, SkyAlong(direction, CameraOrigin) + RainbowAlong(direction, SkySunDirection.xyz, 1.0e6));
        return;
    }

    GpuInstance primaryInstance = Instances[VisibilityInstance(visibility)];
    GpuMaterial mat = Materials[primaryInstance.MaterialIndex];

    vec3  hitPos     = surface.xyz;
    vec3  toCamera   = CameraOrigin - hitPos;
    float primaryT   = length(toCamera);
    vec3  viewDir    = toCamera / max(primaryT, 1e-6);
    float coneWidth  = primaryT * PixelSpreadAngle();             // ray-cone footprint at the primary hit [m]
    bool  weatherEnabled = (SkyControl.z & (kCloudLayerFlag | kLocalCloudFlag | kLocalFogFlag)) != 0u;

    SurfaceFrame  frame;
    ShadingRecord shading = ResolveMaterial(mat, primaryInstance, VisibilityPrimitive(visibility), hitPos, viewDir, coneWidth, frame);

    // If we directly hit an emitter — return its radiance (slab emission × texture; the header's radiance drives the luminaire table).
    // Apply the camera segment before this early return as well: a cloud deck between the eye and an emissive sign is
    //    still in front of the sign, and must not disappear merely because the hit skipped surface lighting.
    if (dot(frame.Emission, frame.Emission) > 0.0)
    {
        vec3 emitterRadiance = frame.Emission;
        if (weatherEnabled)
        {
            vec3 weatherScatter;
            float weatherTransmittance;
            float cloudDay = smoothstep(-12.0, 0.0, SkySunDirection.w);
            CloudAlong(CameraOrigin, -viewDir, primaryT, SkySunDirection.xyz,
                       SkySunRadiance.xyz * cloudDay, SkySunDirect.xyz * 0.5,
                       weatherScatter, weatherTransmittance);
            emitterRadiance = emitterRadiance * weatherTransmittance + weatherScatter;
        }
        Resolve(pixel, emitterRadiance);
        return;
    }

    vec3 hitNormal = frame.Geometric;                             // shadow-ray offsets and the DI target function stay geometric
    vec3 woLocal   = ToLocal(frame, viewDir);
    if (woLocal.z <= 0.0) woLocal = vec3(woLocal.xy, 1e-3);      // grazing normal-mapped case: keep the BSDF defined
    ResolvedLayers layers = ResolveLayers(shading, woLocal);
    vec3 albedo    = shading.BaseColor;                           // debug ambient floor only

    vec3 accumulatedRadiance = vec3(0.0);

    //------------------------------------------------------------------------------------------------------------------
    // 3. ReSTIR direct lighting — initial candidates (+ extra same-pixel passes), R6 temporal merge, R6 row 3
    //    spatiotemporal neighbour taps, shade.
    //------------------------------------------------------------------------------------------------------------------

    // Sunlight counts as direct light: the block runs when EITHER light kind exists (a lamp-less outdoor scene
    //    used to skip it entirely and shade with skylight alone). The sun switch is the record's direct row, so
    //    night, a hidden sun and a disabled sky all take the lamps-only path with no flag to flip.
    bool sunUp = dot(SkySunDirect.xyz, vec3(1.0)) > 0.0;
    if (sunUp || LightTriangleCount > 0u)
    {
        // Initial reservoir from M = SamplesPerPixel candidates
        Reservoir res;
        res.SelectedPoint  = vec3(0.0);
        res.WeightSum      = 0.0;
        res.SampleCount    = 0u;
        res.UnbiasedWeight = 0.0;
        res.SelectedLight  = 0u;
        res.SelectedUv     = vec2(0.0);
        res.Visible        = 0u;

        for (uint s = 0u; s < SamplesPerPixel; ++s)
        {
            vec3  lp; uint li; vec2 luv; float w;
            DrawDirectCandidate(frame, shading, layers, woLocal, hitPos, sunUp, lp, li, luv, w, seed);
            ResampleCandidate(res, lp, li, luv, w, seed);
        }

        // Compute unbiased weight W (R6: the chosen light comes from the reservoir — no nearest-emissive search)
        if (res.WeightSum > 0.0 && res.SampleCount > 0u)
        {
            // p̂ OF THE CURRENT SELECTION, carried from here on.
            //
            //    Every later stage needs this same quantity — the target function evaluated at whichever sample
            //    the reservoir currently holds — and each used to re-derive it with a fresh PHatFull, which is a
            //    full OpenPBR EvaluateBsdf (GGX D and G2, Fresnel, EON diffuse, coat, sheen). But the selection
            //    only ever changes at a resample, and when it does the winner's p̂ was JUST computed as part of
            //    deciding it won. So the value can simply be carried: pSelected below is updated at each point
            //    the selection can change, and read everywhere the old code called PHatFull again.
            //
            //    The pre-R10 code also computed a pHatSel here purely to set UnbiasedWeight, which the block at
            //    the end of the extras loop then overwrote unconditionally — nothing read it in between. That
            //    evaluation was dead in every path and is simply gone.
            float pSelected = 0.0;

            // Extra same-pixel candidates (R6 row 3: renamed from SpatialPassCount): ExtraCandidateCount further light samples
            //    from THIS pixel, resampled with the same target function so the reservoir stays a valid RIS estimate.
            for (uint sp = 0u; sp < ExtraCandidateCount; ++sp)
            {
                vec3  nlp; uint nli; vec2 nuv; float nW;
                DrawDirectCandidate(frame, shading, layers, woLocal, hitPos, sunUp, nlp, nli, nuv, nW, seed);
                ResampleCandidate(res, nlp, nli, nuv, nW, seed);
            }
            // W for the final selection (the selected point may have changed in the loop above). This is the one
            //    evaluation that establishes pSelected; everything downstream carries it. PHatSelected answers for
            //    whichever light kind the reservoir holds — the old code's emit2 temp only knew mesh lights.
            {
                vec3 toSel2 = res.SelectedPoint - hitPos;
                pSelected   = PHatSelected(frame, shading, layers, woLocal, hitPos, res.SelectedLight, toSel2);
                res.UnbiasedWeight = pSelected > 0.0 ? res.WeightSum / (float(res.SampleCount) * pSelected) : 0.0;
            }

            // R6 row 2: temporal reuse — back-project by the R2 motion vector, validate the previous surface
            //    (25° normal, 10% depth, stride guard), pairwise-MIS merge with M-clamp, then re-trace visibility
            //    at the current pixel (standard ReSTIR — keeps W unbiased after reuse).
            uint resAge = 0u;
            if ((FeatureFlags & kFeatureTemporalReuse) != 0u && FrameIndex > 0u)
            {
                vec2 cuv = (vec2(pixel) + 0.5) / vec2(float(ViewportWidth), float(ViewportHeight));
                vec2 motion = texelFetch(MotionImage, ivec2(pixel), 0).rg;
                vec2 puv = cuv - motion;
                ivec2 prevPx = ivec2(floor(puv * vec2(float(ViewportWidth), float(ViewportHeight))));
                if (all(greaterThanEqual(prevPx, ivec2(0))) && prevPx.x < int(ViewportWidth) && prevPx.y < int(ViewportHeight))
                {
                    GpuReservoir prev = PrevReservoirs[prevPx.y * int(ViewportWidth) + prevPx.x];
                    uint prevM = prev.Counts.x;
                    bool valid = prevM > 0u
                        && prev.Normal.w == float(ViewportWidth)   // stride guard (render-scale change)
                        && dot(hitNormal, prev.Normal.xyz) > kTemporalNormalCos
                        && abs(primaryT - prev.UvDepth.w) / max(primaryT, 1e-3) < kTemporalDepthTol;
                    if (valid)
                    {
                        uint mPrevCapped = min(prevM, kTemporalMClamp * res.SampleCount);
                        vec3 prevToL  = prev.Sample.xyz - hitPos;
                        float pPrev = PHatSelected(frame, shading, layers, woLocal, hitPos, prev.Counts.y, prevToL);
                        // pCur is p̂ at the current selection, and res has not been touched since pSelected was
                        //    established — so it IS pSelected, not something to evaluate again.
                        float pCur  = pSelected;
                        float wCur  = pCur * res.UnbiasedWeight * float(res.SampleCount);
                        float wPrev = pPrev * prev.UvDepth.z * float(mPrevCapped);
                        float total = wCur + wPrev;
                        if (total > 0.0 && RandFloat(seed) * total <= wPrev)
                        {
                            res.SelectedPoint = prev.Sample.xyz;
                            res.SelectedLight = prev.Counts.y;
                            res.SelectedUv    = prev.UvDepth.xy;
                            pSelected         = pPrev;   // the winner's p̂, already in hand from the merge test
                        }
                        res.SampleCount = res.SampleCount + mPrevCapped;
                        res.WeightSum   = total;
                        resAge = prev.Counts.w + 1u;
                        // pFin is p̂ of whichever sample survived the merge, and both candidates' p̂ were computed
                        //    to decide it — so it is a selection between two known values, never a new evaluation.
                        res.UnbiasedWeight = pSelected > 0.0 ? total / (float(res.SampleCount) * pSelected) : 0.0;
                    }
                }
            }

            // R6 row 3: spatiotemporal neighbour reuse — K taps read the STABLE prev-frame buffer at neighbouring
            //    pixels of the current frame (same-frame Curr reads would race within the dispatch). Each valid tap
            //    merges by the same pairwise-MIS algebra as temporal: validation, per-tap M-clamp, winner age+1.
            //    Gated on kFeatureSpatialReuse and FrameIndex > 0 (no prev frame on the first accumulation frame).
            if ((FeatureFlags & kFeatureSpatialReuse) != 0u && FrameIndex > 0u)
            {
                // Rotation and radius are drawn from a SEPARATE stream, seeded from the pixel and frame, so the
                //    main `seed` sequence used by resampling is untouched — the R6 identity proofs still hold.
                uint  tapSeed = PcgHash(MakeSeed(pixel, FrameIndex) ^ 0x9E3779B9u);
                float scale   = float(ViewportWidth) / 1280.0;
                float angle   = RandFloat(tapSeed) * 6.28318531;
                float radius  = mix(kSpatialRadiusMinPx, kSpatialRadiusMaxPx, RandFloat(tapSeed)) * scale;

                uint spatialTaps = min(SpatialTapCount, kSpatialTapCeiling);
                for (uint tap = 0u; tap < spatialTaps; ++tap)
                {
                    // The cross keeps its 90° spacing — it is still four taps of equal reach, just aimed
                    //    differently each frame, so no direction is systematically favoured over time.
                    float  theta  = angle + float(tap) * (6.28318531 / float(spatialTaps));
                    ivec2  offset = ivec2(int(round(radius * cos(theta))), int(round(radius * sin(theta))));
                    if (offset.x == 0 && offset.y == 0) continue;   // a rounded-to-zero tap is the pixel itself
                    ivec2 npx = ivec2(pixel) + offset;
                    if (any(lessThan(npx, ivec2(0))) || npx.x >= int(ViewportWidth) || npx.y >= int(ViewportHeight)) continue;
                    GpuReservoir neigh = PrevReservoirs[npx.y * int(ViewportWidth) + npx.x];
                    uint neighM = neigh.Counts.x;
                    bool nValid = neighM > 0u
                        && neigh.Normal.w == float(ViewportWidth)   // stride guard (render-scale change)
                        && dot(hitNormal, neigh.Normal.xyz) > kTemporalNormalCos
                        && abs(primaryT - neigh.UvDepth.w) / max(primaryT, 1e-3) < kTemporalDepthTol;
                    if (!nValid) continue;
                    uint mNeighCapped = min(neighM, kTemporalMClamp * res.SampleCount);
                    vec3 neighToL  = neigh.Sample.xyz - hitPos;
                    float pNeigh = PHatSelected(frame, shading, layers, woLocal, hitPos, neigh.Counts.y, neighToL);
                    // Same identity as the temporal merge: p̂ at the current selection is the carried value.
                    float pSelf  = pSelected;
                    float wSelf  = pSelf * res.UnbiasedWeight * float(res.SampleCount);
                    float wNeigh = pNeigh * neigh.UvDepth.z * float(mNeighCapped);
                    float nTotal = wSelf + wNeigh;
                    uint  takeAge = resAge;
                    if (nTotal > 0.0 && RandFloat(seed) * nTotal <= wNeigh)
                    {
                        res.SelectedPoint = neigh.Sample.xyz;
                        res.SelectedLight = neigh.Counts.y;
                        res.SelectedUv    = neigh.UvDepth.xy;
                        takeAge = neigh.Counts.w + 1u;
                        pSelected         = pNeigh;   // the winner's p̂, already in hand
                    }
                    res.SampleCount = res.SampleCount + mNeighCapped;
                    res.WeightSum   = nTotal;
                    resAge = takeAge;
                    res.UnbiasedWeight = pSelected > 0.0 ? nTotal / (float(res.SampleCount) * pSelected) : 0.0;
                }
            }

            // Visibility re-traced at the current pixel; an occluded merged sample contributes nothing.
            res.Visible = TraceShadow(hitPos + hitNormal * kEps, res.SelectedPoint) ? 0u : 1u;
            if (res.Visible == 0u) res.UnbiasedWeight = 0.0;

            // Publish the final reservoir for next frame's temporal reuse (always — even occluded, M still counts).
            GpuReservoir outRes;
            outRes.Sample  = vec4(res.SelectedPoint, res.WeightSum);
            outRes.Counts  = uvec4(res.SampleCount, res.SelectedLight, res.Visible, resAge);
            outRes.UvDepth = vec4(res.SelectedUv, res.UnbiasedWeight, primaryT);
            outRes.Normal  = vec4(hitNormal, float(ViewportWidth));
            CurrReservoirs[pixel.y * ViewportWidth + pixel.x] = outRes;

            // Shade (R6: the stored selection carries its light — no search). The sun divides by nothing: it is at
            //    infinity, so its d² is 1 by definition and the stored far point's is meaningless. Mesh lights pay
            //    the emitter cosine the target pays (same quantity, same value — shade and target must agree).
            if (res.Visible == 1u)
            {
                vec3 shadeDir = normalize(res.SelectedPoint - hitPos);
                float shadeCos = max(0.0, dot(hitNormal, shadeDir));

                vec3  wiLocal = ToLocal(frame, shadeDir);
                vec3  f = wiLocal.z > 0.0 ? EvaluateBsdf(shading, layers, woLocal, wiLocal) : vec3(0.0);   // R4b: full lobe stack
                if (res.SelectedLight == kSunLightIndex)
                {
                    // The reservoir target carries this same visibility term (PHatSun), so the cloud does not
                    //    turn into a post-hoc multiplier that would bias temporal/spatial reuse.
                    accumulatedRadiance += f * SunEmission() * shadeCos * res.UnbiasedWeight
                                         * CloudSunTransmittance(hitPos, shadeDir);
                }
                else
                {
                    vec3 chosenEmit = LightEmission(res.SelectedLight);
                    float shadeCosL = max(0.0, dot(LightNormal(res.SelectedLight), -shadeDir));
                    float dist2 = dot(res.SelectedPoint - hitPos, res.SelectedPoint - hitPos);
                    accumulatedRadiance += f * chosenEmit * shadeCos * shadeCosL * res.UnbiasedWeight / (dist2 + 0.01);
                }
            }
        }
    }

    //------------------------------------------------------------------------------------------------------------------
    // 4. One-bounce indirect (plain path tracing with NEE, no GI reservoir); skipped when the GI feature is off
    //------------------------------------------------------------------------------------------------------------------
    if ((FeatureFlags & kFeatureGlobalIllumination) != 0u)
    {
        // R4b: BSDF-sampled bounce (EON CLTC / VNDF / LTC per lobe). ⚠️ For Cornell (specular_weight 0, r_d 0) the CLTC
        //    sample IS a cosine sample, but one extra random number is drawn for the lobe pick — the converged image is
        //    unchanged, the per-frame noise pattern is not.
        vec4 bounce = SampleBsdf(shading, layers, woLocal, vec3(RandFloat(seed), RandFloat(seed), RandFloat(seed)));
        if (bounce.w > 0.0)
        {
            vec3 bounceDir = FromLocal(frame, bounce.xyz);
            vec3 throughput = EvaluateBsdf(shading, layers, woLocal, bounce.xyz) * bounce.z / bounce.w;   // f · cosθ / pdf
            Ray  bounceRay;
            bounceRay.origin = hitPos + hitNormal * kEps;
            bounceRay.dir    = bounceDir;

            Hit bounceHit = TraceScene(bounceRay);

            // ⚠️ An escaped bounce ray now collects the sky, and this is the half that changes the LIGHTING
            //    rather than the background. In an outdoor scene the sky is the largest emitter present: it is
            //    what makes an upward-facing surface read cool and an unlit face take the colour of the air
            //    above it. Dropping it was correct when there was no environment light and is a missing light
            //    source now.
            //
            //    The throughput already carries the BSDF and the cosine from the bounce, so the sky enters
            //    exactly as any other emitter would — no separate ambient term, no second light path.
            if (!bounceHit.valid && dot(bounceDir, hitNormal) > 0.0)
            {
                accumulatedRadiance += throughput * SkyAlong(bounceDir, hitPos);
            }
            if (bounceHit.valid && dot(bounceDir, hitNormal) > 0.0)
            {
                GpuMaterial   bMat  = Materials[bounceHit.matIdx];
                // D2: TraceScene already resolved the instance — reuse it rather than repeating the search here.
                GpuInstance   bInstance = Instances[bounceHit.instance];
                SurfaceFrame  bFrame;
                float         bCone = coneWidth + bounceHit.t * PixelSpreadAngle();
                ShadingRecord bShading = ResolveMaterial(bMat, bInstance, LocalPrimitive(bounceHit.primitive, bounceHit.instance), bounceHit.pos, -bounceDir, bCone, bFrame);

                if (dot(bFrame.Emission, bFrame.Emission) > 0.0)
                {
                    accumulatedRadiance += throughput * bFrame.Emission;
                }
                else if (sunUp || LightTriangleCount > 0u)
                {
                    vec3  bHitPos    = bounceHit.pos;
                    vec3  bHitNormal = bFrame.Geometric;
                    vec3  bWo        = ToLocal(bFrame, -bounceDir);
                    if (bWo.z <= 0.0) bWo = vec3(bWo.xy, 1e-3);
                    ResolvedLayers bLayers = ResolveLayers(bShading, bWo);

                    // The bounce draws one light the same way the reservoir candidates do — sun or lamp by the same
                    //    coin — because a single unweighted sample is a one-candidate reservoir and the pdfs are the
                    //    same ones DrawDirectCandidate divides by. (The old mesh-only line divided by the slot count
                    //    but not the area: the same 1/A bias the reservoir had, fixed here too.)
                    bool sunBounce = sunUp && (LightTriangleCount == 0u || RandFloat(seed) < kSunPickProbability);
                    if (sunBounce)
                    {
                        vec3 bSunDir = SampleSunDirection(SkySunDirection.xyz, RandFloat(seed), RandFloat(seed));
                        float bCos = max(0.0, dot(bHitNormal, bSunDir));
                        vec3  bWi  = ToLocal(bFrame, bSunDir);
                        if (bCos > 0.0 && bWi.z > 0.0
                            && !TraceShadow(bHitPos + bHitNormal * kEps, bHitPos + bSunDir * kSunShadowDistance))
                        {
                            vec3  bF = EvaluateBsdf(bShading, bLayers, bWo, bWi);
                            float pPick = LightTriangleCount > 0u ? kSunPickProbability : 1.0;
                            // The bounced sun is still travelling through the same cloud field as primary NEE; scene
                            //    visibility alone only tests geometry and would leave indirect sunlight unshadowed by weather.
                            accumulatedRadiance += throughput * bF * SunEmission() * bCos * SunSolidAngle()
                                                 * CloudSunTransmittance(bHitPos, bSunDir) / pPick;
                        }
                    }
                    else
                    {
                        const uint bSlots = LightTriangleCount;
                        uint  bLi  = min(uint(RandFloat(seed) * float(bSlots)), bSlots - 1u);
                        vec3  bLp  = SampleLightPointFor(bLi, RandFloat(seed), RandFloat(seed));
                        vec3  bToL = bLp - bHitPos;
                        float bDist = length(bToL);
                        vec3  bLDir = bToL / bDist;
                        float bCos  = max(0.0, dot(bHitNormal, bLDir));
                        float bCosL = max(0.0, dot(LightNormal(bLi), -bLDir));
                        vec3  bWi   = ToLocal(bFrame, bLDir);

                        vec3 bLEmit = LightEmission(bLi);

                        if (bCos > 0.0 && bCosL > 0.0 && bWi.z > 0.0 && !TraceShadow(bHitPos + bHitNormal * kEps, bLp))
                        {
                            float bDist2 = bDist * bDist;
                            vec3  bF = EvaluateBsdf(bShading, bLayers, bWo, bWi);
                            // ⚠️ MULTIPLY by the slot count: the pick above chose one of bSlots uniformly (pdf
                            //    1/bSlots) and the point uniformly on its triangle (pdf 1/area), so the sample's
                            //    pdf is pMesh/(bSlots·area) and the estimate divides by the PDF — i.e. multiplies
                            //    by slots·area. The old line divided by bSlots (a defended-in-comment inversion),
                            //    which paid the whole second bounce divided by N² and forgot the area too.
                            float pMesh = sunUp ? 1.0 - kSunPickProbability : 1.0;
                            accumulatedRadiance += throughput * bF * bLEmit * bCos * bCosL
                                                 * Luminaires[bLi].Area * float(bSlots) / ((bDist2 + 0.01) * pMesh);
                        }
                    }
                }
            }
        }
    }

    //------------------------------------------------------------------------------------------------------------------
    // 5. Ambient floor (debug only — flag kFeatureAmbientFloor; a real GI pass makes it unnecessary) + tone map + write
    //------------------------------------------------------------------------------------------------------------------
    if ((FeatureFlags & kFeatureAmbientFloor) != 0u)
        accumulatedRadiance += albedo * frame.Occlusion * AmbientStrength;

    // Moonlight, and it is NOT debug-only. The reference panel adds its moon loop to every shaded surface
    //    unconditionally, and the CPU raster learned why: a scene lit only by the sky renders as black geometry
    //    against a correct sky unless the environment light reaches the surfaces too. Escaped bounce rays collect
    //    the discs through SkyAlong (the GI moonlight); this is the direct fill for the primary hit, so one
    //    bounce is enough for moonlight to read. Zero when no moon is up, by the count in the block.
    accumulatedRadiance += albedo * frame.Occlusion * MoonAmbient();
    // The bow in the air the view ray crossed: primary rays only (a rasterizer's placement — the escaped bounce
    //    is a transport path, not a view). The view direction runs camera-ward reversed, and the primary travel
    //    is the rain column — short rays earn no bow, which keeps the arc off nearby geometry.
    accumulatedRadiance += RainbowAlong(-viewDir, SkySunDirection.xyz, primaryT);

    // Weather is the final camera segment, so every surface contribution above — direct sun, reused ReSTIR light,
    //    escaped-bounce sky, ambient floor, moon fill and the bow — crosses the same cloud/fog layer exactly once.
    //    Misses take this composition inside SkyAlong; surfaces need the companion segment from eye to hit.
    if (weatherEnabled && primaryT > 0.0)
    {
        vec3 weatherScatter;
        float weatherTransmittance;
        float cloudDay = smoothstep(-12.0, 0.0, SkySunDirection.w);
        CloudAlong(CameraOrigin, -viewDir, primaryT, SkySunDirection.xyz,
                   SkySunRadiance.xyz * cloudDay, SkySunDirect.xyz * 0.5,
                   weatherScatter, weatherTransmittance);
        accumulatedRadiance = accumulatedRadiance * weatherTransmittance + weatherScatter;
    }

    // R7a: the primary hit's geometric normal and camera distance are what the next frame validates its
    //    back-projection against — the same pair R6 row 2 uses to validate reprojected reservoirs.
    ResolveSurface(pixel, accumulatedRadiance, hitNormal, primaryT);
}
