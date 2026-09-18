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
//         and this is where the two convergence faults lived. Both are fixed and both are measured in the CPU
//         mirror (Exhibits/Workbench/Materials/RunRestirConvergence.sh, sheet RestirConvergenceSheet.png):
//           · the next frame's history takes the TEMPORAL reservoir only — spatial reuse is a shading-time
//             refinement, so a frame's spatial merges cannot inflate the M the temporal cap reasons about (the
//             research rule: don't feed the spatial result back into temporal unless starved);
//           · a spatial tap's M cap reads the receiver's PRE-merge sample count, not a count the tap loop is
//             itself growing (tap 2 could previously add up to 20x what tap 1 had just added).
//         Before: mean M 1 879 / max 4 436 at frame 16, mean 843 633 by frame 20, and 2 or 4 taps were WORSE than
//         0 taps at every budget (240x135, 4 candidates — N=32/64/128: 3 251/3 777/4 660 with 4 taps against
//         3 489/2 601/1 953 with none). After: 4 taps < 2 taps < 0 taps everywhere (7 462/7 163/7 599 against
//         7 843/7 623/8 082) with M bounded (41.7 mean / 64 max at frame 16, 50.4 / 84 at frame 32).
//       • the indirect half's own pool — ReSTIR GI-style reuse of the FIRST-BOUNCE VERTEX's NEE stratum — landed as
//         kFeatureGiReuse (bit 8, ON by default; the Control Centre's "Indirect reuse (GI pool)" checkbox and
//         ReSTIRIntegratorConfiguration::GlobalIlluminationReuse turn it off for the A/B). A second 80 B/px
//         reservoir pair (bindings 25/26 — same GpuReservoir, D10 widened it from 64 B, so the two pools share the
//         stride and the Identity tail) holds light samples DRAWN AT THE VERTEX; temporal reuse reprojects by the
//         pixel's motion and validates on the PRIMARY surface (25° / 10 % / stride guard), and spatial reuse runs the
//         same cross as the direct pool with the neighbour's reservoir re-evaluated at THIS pixel's vertex. (No
//         vertex-proximity test: the transferred quantity is a light SAMPLE — a world point on an emitter — so the
//         receiver's own target re-evaluation plus its re-traced shadow ray is the test. Measured and rejected in the
//         mirror; see the section 4b comment.) The vertex needs no buffer: this kernel is one dispatch, so the receiver's vertex
//         is in registers where the DI block built it. The pool REPLACES that vertex's single-sample NEE (never adds
//         to it — the giPoolOwns branch in the one-bounce block); a dielectric, SSS or interior vertex keeps the single-sample
//         arm, because a refraction site is not a NEE site. Measured in the CPU mirror before the port, where the
//         pool is `--restir-no-gi-reuse`'s on-arm: RMSE 7 387 vs 7 508 at N=32, 7 223 vs 7 295 at N=64, 7 632 vs
//         7 680 at N=128, pool shaded on ~16 % of surface pixels (the share whose own BSDF sample hits geometry).
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

struct GpuMaterialSlab      // MaterialSlabRecord (304 B) — 19 vec4; field map in SceneRecords.slang. Read by R4b shading.
{
    vec4  Payload[15];
    uvec4 TextureSlots0;
    uvec4 TextureSlots1;
    uvec4 Runtime;
    vec4  Slate2;           // M2: x = anisotropy_rotation [rad], y = direct_f0_weight (M6, 0), zw = 0
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
#include "TraversalRecords.slang"   // D6/D7: the two-level traversal's device records (must precede the buffers below)
layout(std430, binding = 8) readonly buffer TraversalNodeExtent { vec4 CwbvhNodes[]; };   // R3: shared CWBVH node blob (all BLASes)
layout(std430, binding = 9) readonly buffer TraversalLeafExtent { vec4 CwbvhTris[];  };   // R3: shared CWBVH triangle blob
// D6/D7 — the two-level set. Present in every dispatch; whether they are WALKED depends on TlasInstanceCount, which the
//    host sets to the resident instance count (0 = the scene is one world-space blob, ie every scene before D6). The
//    bindings are written unconditionally by SwapchainExchange (empty buffers until UploadInstances), so a scene
//    without instances reads nothing and the single-blob arithmetic is what it always was.
layout(std430, binding = 27) readonly buffer TraversalTlasNodeExtent      { TraversalTlasNode      TlasNodes[];      };
layout(std430, binding = 28) readonly buffer TraversalTlasPrimitiveExtent { uint                   TlasPrimitives[]; };
layout(std430, binding = 29) readonly buffer TraversalInstanceExtent      { TraversalTlasInstance TlasInstances[];  };
layout(std430, binding = 30) readonly buffer TraversalBlasPlacementExtent { TraversalBlasPlacement BlasPlacements[]; };
#define FRONTIER_TRAVERSAL_INSTANCES   // the four buffers above are declared: TraversalCWBVH.slang's two-level walkers compile
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
struct GpuReservoir   // 80 B stride (D10) — mirrors ReservoirBufferRecord in SwapchainExchange.cpp
{
    vec4  Sample;    // xyz = light sample point, w = WeightSum
    uvec4 Counts;    // x = M, y = SelectedLight, z = Visible, w = Age
    vec4  UvDepth;   // xy = SelectedUv, z = W, w = view depth at the build pixel [m]
    vec4  Normal;    // xyz = viewer-facing geometric normal at the build pixel, w = stride guard (ViewportWidth)
    uvec4 Identity;  // D10: x = the packed (instance, primitive) this reservoir was built on; yzw reserved
};
layout(std430, binding = 16) readonly buffer PrevReservoirExtent { GpuReservoir PrevReservoirs[]; };
layout(std430, binding = 17) writeonly buffer CurrReservoirExtent { GpuReservoir CurrReservoirs[]; };
// The indirect pool's own pair (kFeatureGiReuse). Same 80 B record (D10; 64 B before the Identity tail) and the same
//    field meanings, but the sample inside lives
//    on the FIRST-BOUNCE VERTEX rather than on the primary surface: Sample.xyz is a light point sampled from that
//    vertex (for the sun, a far point along a cone-sampled direction from it), UvDepth.xy its uv, and the record's
//    Normal/primaryT are the PRIMARY pixel's validation pair — the same pair the direct pool validates, because the
//    pool is reprojected by the pixel's motion vector whatever point the light sample came from. The vertex itself
//    needs no buffer: this kernel is one dispatch, so the receiver's vertex is in registers where it was built.
layout(std430, binding = 25) readonly buffer GiPrevReservoirExtent { GpuReservoir GiPrevReservoirs[]; };
layout(std430, binding = 26) writeonly buffer GiCurrReservoirExtent { GpuReservoir GiCurrReservoirs[]; };
layout(rgba16f, binding = 18) uniform image2D HistorySurfaceImage;   // R7a: xyz = normal, w = depth the history mean was shaded at (w ≤ 0 = no surface)
layout(rgba32f, binding = 19) uniform image2D MomentImage;           // R7: x = E[luma], y = E[luma²], reprojected with the mean
layout(rgba32f, binding = 20) uniform writeonly image2D DenoiseImage; // R7: xyz = linear radiance, w = variance — the à-trous input
// ── Bindings 21-24 are the Celestial sky, moons, star tables and post record, declared in
//    Shaders/SkyRecords.slang, Shaders/MoonRecords.slang and Shaders/PostRecords.slang below. Round 9 vacated
//    21-24 when the sun/sky system was removed (947b8b9); all four are now filled, each written at bring-up —
//    a declared-but-never-written descriptor is a validation error rather than free space.
//    ⚠️ The bindless table is declared BEFORE the includes: MoonAlong samples it, and SkyAlong calls MoonAlong,
//    so the table and the moons must both be visible where the sky is defined. 27 is still the highest binding
//    NUMBER in the set, which is what the variable-count rule constrains — source order is free.
layout(binding = 31) uniform sampler2D Textures[];   // R4a bindless table — ⚠️ moved 11 → 15 → 18 → 19 → 21 → 23 → 25 → 27 → 31 (the GI reservoir pair took 25/26, D6/D7's four instance buffers took 27-30): a variable-count binding must be the last one in the set
#include "Shaders/MoonRecords.slang"
#include "Shaders/PostRecords.slang"
#include "Shaders/SkyRecords.slang"
#include "Shaders/CloudShadow.slang"

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
// M3: kChannel* / kReflectance* / ReflectanceConsumes live in MaterialEvaluation.slang now (single source of truth,
// CPU-tested 1:1); visible here through #include "MaterialEvaluation.slang".
uint MaterialSelection(GpuMaterial mat) { return (mat.Slabs.z >> 8u) & 0xFu; }

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
    bool  ThinWalled;       // K4: SlabFlags bit0 (P14.z) — foil (thin-wall compound) vs solid (tracer walks the medium)
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

// M1: the selection gate — skips the texture op entirely when the channel is dead (explicit LOD keeps this legal
// in non-uniform control flow). `live` is per material (constant weight / selection), never per texel.
vec4 SampleChannelGated(GpuMaterialSlab S, uint channel, vec2 uv, float lodBias, vec4 fallback, bool live)
{
    if (!live) return fallback;
    return SampleChannel(S, channel, uv, lodBias, fallback);
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
    m.SpecularWeight = 1.0; m.SpecularColor = vec3(1.0); m.SpecularRoughness = mat.Albedo.w; m.SpecularAnisotropy = 0.0; m.AnisotropyAngle = 0.0; m.SpecularIor = 1.5;
    m.ThinFilmWeight = 0.0; m.ThinFilmThickness = 0.5; m.ThinFilmIor = 1.4;
    m.HazinessWeight = 0.0; m.HazinessRoughness = 0.5;
    m.CoatWeight = 0.0; m.CoatColor = vec3(1.0); m.CoatRoughness = 0.0; m.CoatAnisotropy = 0.0; m.CoatIor = 1.6; m.CoatDarkening = 1.0;
    m.CoatTangent = vec3(1.0, 0.0, 0.0); m.CoatNormal = vec3(0.0, 0.0, 1.0);   // M2: identity coat frame
    m.FuzzWeight = 0.0; m.FuzzColor = vec3(1.0); m.FuzzRoughness = 0.5;
    m.Emission = mat.Emissive.xyz;
    m.Selection = MaterialSelection(mat);   // M3: per-selection shading path (covers the header path too)
    m.SssWeight = 0.0; m.SssColor = vec3(1.0); m.SssRadius = 0.0; m.SssRadiusScale = vec3(0.0); m.SssThickness = 0.0;   // M5: SSS off (branch-gated, never read at 0; slab resolve in the kernel milestone)
    m.TransmissionWeight = 0.0; m.TransmissionColor = vec3(1.0); m.TransmissionDepth = 0.0; m.TransmissionThickness = 0.0;   // K2: was UNSET (garbage TransmitMix → random T-branch). Opaque default; the slab resolve below reads P3.yzw/P4 (resident — no CPU change was needed)
    F.Emission = mat.Emissive.xyz; F.Opacity = 1.0; F.Occlusion = 1.0; F.ThinWalled = true;   // K4: header path (no slab) is always foil — TransmitMix = 0 there anyway, so the flag is inert

    if (mat.Slabs.y > 0u)
    {
        GpuMaterialSlab S = MaterialSlabs[mat.Slabs.x];   // Tier A: the FIRST slab (top-most after flatten; multi-slab stacks arrive with R5)
        uint selection = MaterialSelection(mat);   // M1: Sultan-18 §9 gate — unread channels are not sampled
        vec4 P0 = S.Payload[0], P1 = S.Payload[1], P2 = S.Payload[2], P3 = S.Payload[3];
        vec4 P4 = S.Payload[4], P6 = S.Payload[6], P7 = S.Payload[7];   // K2: P5 (transmission scatter/dispersion) is v1-out — surface BTDF only, never declared
        vec4 P8 = S.Payload[8], P9 = S.Payload[9], P10 = S.Payload[10], P11 = S.Payload[11], P12 = S.Payload[12], P13 = S.Payload[13];
        vec4 P14 = S.Payload[14];   // K4: .z = SlabFlags bit0 thin-walled (floats 56–59 = glint pair + flags + mask)
        float normalScale = uintBitsToFloat(S.Runtime.y), occlusionStrength = uintBitsToFloat(S.Runtime.z);

        vec4 baseTex = SampleChannel(S, kChannelBaseColor, uv, F.LodBias, vec4(1.0));   // never gated: Unlit reads it
        m.BaseColor          = P0.x * P0.yzw * baseTex.xyz;
        m.Metalness          = P1.x * SampleChannelGated(S, kChannelMetalness, uv, F.LodBias, vec4(1.0),
                               P1.x > 0.0 || ReflectanceConsumes(selection, kChannelMetalness)).z;   // glTF: B
        m.DiffuseRoughness   = P1.y;
        m.SpecularWeight     = P1.z;
        m.SpecularColor      = vec3(P1.w, P2.xy) * SampleChannelGated(S, kChannelSpecularColor, uv, F.LodBias, vec4(1.0),
                               P1.z > 0.0 || ReflectanceConsumes(selection, kChannelSpecularColor)).xyz;
        m.SpecularRoughness  = P2.z * SampleChannelGated(S, kChannelRoughness, uv, F.LodBias, vec4(1.0),
                               (P1.z > 0.0 && P2.z > 0.0) || ReflectanceConsumes(selection, kChannelRoughness)).y;   // glTF: G
        // M2: anisotropy — direction from RG (tangent space), strength scale from B (KHR_materials_anisotropy);
        // the slab rotation composes additively. Fallback = +tangent, strength 1 (constant path).
        vec4 anisoTex = SampleChannelGated(S, kChannelAnisotropy, uv, F.LodBias, vec4(1.0, 0.5, 1.0, 1.0),
                            P2.w != 0.0 || ReflectanceConsumes(selection, kChannelAnisotropy));
        m.SpecularAnisotropy = P2.w * anisoTex.z;
        vec2 anisoDir = anisoTex.xy * 2.0 - 1.0;
        m.AnisotropyAngle = dot(anisoDir, anisoDir) > 1e-12 ? atan(anisoDir.y, anisoDir.x) + S.Slate2.x : S.Slate2.x;
        m.SpecularIor        = P3.x;
        // K2: M4 slab resolve — the payload already carries the volume pair (resident, no CPU change); the kernel
        // just never read it. Weight × kChannelTransmission.x (KHR: R = transmissionFactor; unbound → ×1.0).
        // Thickness is NOT in the slab (OpenPBR §5 has none): foil Beer = 1 (documented limit); solid traverses
        // true geometry (K4) and treats 0 as the no-exit fallback.
        m.TransmissionWeight = P3.y * SampleChannelGated(S, kChannelTransmission, uv, F.LodBias, vec4(1.0),
                               P3.y > 0.0 || ReflectanceConsumes(selection, kChannelTransmission)).x;
        m.TransmissionColor  = vec3(P3.z, P3.w, P4.x);
        m.TransmissionDepth  = P4.y;
        // K2: M5 slab resolve — weight × kChannelSubsurface.x (no KHR standard; .x by the weight-texture
        // convention; unbound → ×1.0). RadiusScale .yzw; P8.x (scatter anisotropy g) has no ShadingRecord field —
        // M5 v1 wrap doesn't consume g (v2 dipole: CPU-side first, kernel field then). Thickness is tracer-side
        // (K5 sets it per hit; the 0 default = unset).
        m.SssWeight          = P6.x * SampleChannelGated(S, kChannelSubsurface, uv, F.LodBias, vec4(1.0),
                               P6.x > 0.0 || ReflectanceConsumes(selection, kChannelSubsurface)).x;
        m.SssColor           = vec3(P6.y, P6.z, P6.w);
        m.SssRadius          = P7.x;
        m.SssRadiusScale     = vec3(P7.y, P7.z, P7.w);
        m.CoatWeight         = P8.y * SampleChannelGated(S, kChannelCoat, uv, F.LodBias, vec4(1.0),
                               P8.y > 0.0 || ReflectanceConsumes(selection, kChannelCoat)).x;
        m.CoatColor          = vec3(P8.zw, P9.x);
        m.CoatRoughness      = P9.y;
        m.CoatAnisotropy     = P9.z;
        m.CoatIor            = P9.w;
        m.CoatDarkening      = P10.x;
        m.FuzzWeight         = P10.y;
        m.FuzzColor          = vec3(P10.zw, P11.x) * SampleChannelGated(S, kChannelFuzz, uv, F.LodBias, vec4(1.0),
                               P10.y > 0.0 || ReflectanceConsumes(selection, kChannelFuzz)).xyz;
        m.FuzzRoughness      = P11.y;
        m.Emission           = P11.z * vec3(P11.w, P12.xy) * SampleChannel(S, kChannelEmission, uv, F.LodBias, vec4(1.0)).xyz;   // never gated: short-circuit + cutout read it
        m.ThinFilmWeight     = P12.z * SampleChannelGated(S, kChannelThinFilm, uv, F.LodBias, vec4(1.0),
                               P12.z > 0.0 || ReflectanceConsumes(selection, kChannelThinFilm)).x;
        m.ThinFilmThickness  = P12.w;
        m.ThinFilmIor        = P13.x;
        m.HazinessWeight     = P13.z;
        m.HazinessRoughness  = P13.w;
        F.ThinWalled = (floatBitsToUint(P14.z) & 1u) != 0u;   // K4: SlabFlags bit0 (exact bit-cast — the record is a memcpy mirror)
        F.Emission  = m.Emission;
        F.Opacity   = P13.y * SampleChannel(S, kChannelOpacity, uv, F.LodBias, vec4(1.0)).w;   // glTF: A of base colour
        F.Occlusion = mix(1.0, SampleChannelGated(S, kChannelOcclusion, uv, F.LodBias, vec4(1.0),
                               ReflectanceConsumes(selection, kChannelOcclusion)).x, occlusionStrength);

        uint normalSlot = SlabTexture(S, kChannelNormal);
        if (normalSlot != 0xFFFFu && ReflectanceConsumes(selection, kChannelNormal))
        {
            vec3 tn = SampleChannel(S, kChannelNormal, uv, F.LodBias, vec4(0.5, 0.5, 1.0, 1.0)).xyz * 2.0 - 1.0;
            tn.x *= normalScale; tn.y *= normalScale;
            vec3 nm = normalize(t * tn.x + b * tn.y + n * tn.z);
            if (dot(nm, ng) > 0.0) n = nm;                         // reject maps that push the normal below the surface
            t = normalize(t - n * dot(n, t)); b = cross(n, t) * handed;
        }

        // M2: independent coat frame (clearcoat_normal_texture), derived from the POST-normal-map base frame — the coat
        // follows the base surface plus its own perturbation — then expressed in local (base-frame) coords for the lobes.
        if (ReflectanceConsumes(selection, kChannelCoatNormal) && SlabTexture(S, kChannelCoatNormal) != 0xFFFFu)
        {
            vec3 cn = SampleChannel(S, kChannelCoatNormal, uv, F.LodBias, vec4(0.5, 0.5, 1.0, 1.0)).xyz * 2.0 - 1.0;
            vec3 nCw = normalize(t * cn.x + b * cn.y + n * cn.z);
            if (dot(nCw, ng) > 0.0)
            {
                vec3 tCw = t - nCw * dot(nCw, t);
                tCw = dot(tCw, tCw) > 1e-12 ? normalize(tCw)
                                            : normalize(abs(nCw.z) < 0.999 ? cross(vec3(0.0, 0.0, 1.0), nCw) : vec3(1.0, 0.0, 0.0));
                m.CoatTangent = vec3(dot(tCw, t), dot(tCw, b), dot(tCw, n));
                m.CoatNormal  = vec3(dot(nCw, t), dot(nCw, b), dot(nCw, n));
            }
        }
    }

    F.Normal = n; F.Tangent = t; F.Bitangent = b; F.Geometric = ng;
    return m;
}

#include "TraversalCWBVH.slang"

const uint kVisibilityInvalid = 0xFFFFFFFFu;
uint VisibilityInstance(uint Packed)  { return (Packed >> 14) & 0x3FFFFu; }
uint VisibilityPrimitive(uint Packed) { return Packed & 0x3FFFu; }

// ⚠️ Where the history's identity lives, and why it is not a new binding. Bindings 0–31 of this set are all taken
//    (21-24 are the sky/moons/stars/post records, 25/26 the GI reservoir pair, 27-30 D6/D7's instance buffers, 31 the
//    bindless table, which must stay the highest number because it is variable-count). Adding a 33rd descriptor for four
//    bytes per pixel would be a pipeline change for a channel pair that is already allocated, already reprojected with
//    the mean, and already reserved for growth: MomentImage's z/w, which held 0.0 and nothing else.
//
//    The packing is exact, not approximate: each half is a 16-bit integer, and every integer below 2^24 is exact in a
//    float32, so the decoded identity compares with `==` like the integer it is. There is no tolerance here and there
//    must not be one — an identity that matches "closely" is a different object.
float IdentityLow(uint Identity)   { return float(Identity & 0xFFFFu); }
float IdentityHigh(uint Identity)  { return float(Identity >> 16u); }
uint  DecodeIdentity(vec4 Moments) { return uint(Moments.z) | (uint(Moments.w) << 16u); }

// D10: the identity a pixel with no surface carries. Spelled once, so the resolve and the background path cannot
//    disagree about "no surface".
const uint kIdentityNone = 0xFFFFFFFFu;

// ⚠️ The validation compares the INSTANCE — the object — and deliberately not the primitive.
//
//    The raster carries (instance, primitive) packed, and the primitive is what the history's identity was first built
//    from. Measured on a still scene with a still camera, that restarted ~13 % of the image every frame: the jitter is
//    one Halton offset per frame, and on a densely tessellated object (a swatch sphere is ~1500 triangles for ~10
//    pixels) the neighbouring triangle is far smaller than a pixel, so the primitive changes under a jitter while the
//    surface does not. An identity that fine does not measure "the same thing", it measures the tessellation.
//
//    The object is the right granularity, and it is the one this feature exists for: it survives the motion of a moving
//    object (so the object keeps its own history as it moves — the point of D10), it differs between two objects that
//    exchange places (the ghost the rule catches), and it is the same number the raster, the instance buffers and the
//    acceleration structure all agree on. Same object, different face is the normal/depth test's job, not this one's.
uint IdentityObject(uint Packed) { return Packed >> 14u; }   // 18 bits — VisibilityInstance's own shift

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
    uint    TlasInstanceCount;  // [cnt] D6/D7: resident top-level instances (0 ⇒ single world-space blob, the pre-D6 path)
                                 //       — the last reserve slot, so the block is still 128 B and no other field moved
};

const uint kFeatureGlobalIllumination = 1u;   // bit 0: trace the secondary bounce (section 4)
const uint kFeatureAntiAliasing       = 2u;   // bit 1: sub-pixel jitter — applied by the R2 raster/resolve, not here
const uint kFeatureAmbientFloor       = 4u;   // bit 2: add albedo × AmbientStrength (debug fill light; default OFF)
const uint kFeatureTemporalReuse      = 8u;   // bit 3: R6 row 2 — temporal reservoir reuse (back-projection + validation)
const uint kFeatureSpatialReuse       = 16u;  // bit 4: R6 row 3 — spatiotemporal neighbour reuse (pairwise MIS)
const uint kFeatureAliasPick          = 32u;  // bit 5: R6 row 3 — Walker-alias light pick (off = uniform, the R0 identity proof)
const uint kFeatureTemporalReprojection = 64u;  // bit 6: R7a — reproject the running mean through the R2 motion vectors
const uint kFeatureDenoise            = 128u; // bit 7: R7 — the à-trous filter runs, so the kernel defers the tone map to it
const uint kFeatureGiReuse            = 256u; // bit 8: the indirect half's pool — ReSTIR GI-style reuse of the first-bounce
// D10: bit 9 — validate a reprojected history against the IDENTITY of the surface, not only its normal and depth.
//    ⚠️ Off by default, and byte-identical when off: with the bit clear the resolve reads, validates and stores exactly
//    what it did before this existed, which is what makes the exhibit's A/B a comparison rather than a rewrite.
const uint kFeatureTemporalIdentity   = 512u; // bit 9: D10 — identity-based validation of the reprojected history
                                              //    vertex's NEE stratum, temporally and over the same kSpatialTapCount cross.
                                              //    ON by default (the host sets it); clearing the bit restores the
                                              //    single-sample NEE arm, which is the A/B the CPU mirror measures.

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

// D6/D7 — the two-level arm of the trace, factored out so the alpha-mask retrace loop below is shared. `hit` is local
//    to the instance that was found; `instance` is the top-level row, which the caller resolves to the flat triangle
//    index through FirstTriangle. Both arms fill the same four values, so the caller cannot tell them apart.
struct TraversalResult
{
    float t;
    uint  primitive;   // [-] LOCAL triangle index inside the instance
    uint  instance;    // [idx] top-level instance, 0xFFFFFFFF on a miss
    bool  valid;
};

TraversalResult TraceTraversal(vec3 origin, vec3 dir, vec3 rD, float tmax)
{
    TraversalResult result;
    result.t = kInf; result.primitive = 0xFFFFFFFFu; result.instance = 0xFFFFFFFFu; result.valid = false;

    if (TlasInstanceCount == 0u)   // pre-D6 path: one world-space blob, primitive is already the flat index
    {
        TraversalHit h = TraverseClosest(origin, dir, rD, tmax);
        if (h.primitive == 0xFFFFFFFFu) return result;
        result.t = h.t; result.primitive = h.primitive; result.instance = 0u; result.valid = true;
        return result;
    }

    InstanceTraversalHit ih = TraverseInstancesClosest(origin, dir, rD, tmax);
    if (ih.instance == 0xFFFFFFFFu) return result;
    result.t = ih.hit.t; result.primitive = ih.hit.primitive; result.instance = ih.instance; result.valid = true;
    return result;
}

bool TraceTraversalOccluded(vec3 origin, vec3 dir, vec3 rD, float tmax)
{
    return TlasInstanceCount == 0u ? TraverseOccluded(origin, dir, rD, tmax)
                                   : TraverseInstancesOccluded(origin, dir, rD, tmax);
}

// D6 — the flat index a traversal result resolves to: the instance's own triangles are contiguous in Triangles[], so the
//    local primitive plus the instance's FirstTriangle is the world-space index the material / uv / normal lookups use.
uint FlatPrimitiveOf(TraversalResult result)
{
    return TlasInstanceCount == 0u ? result.primitive : TlasInstances[result.instance].FirstTriangle + result.primitive;
}

Hit TraceScene(Ray ray)
{
    // R3: closest hit through the CWBVH; the primitive index addresses the flat Triangles[] for material and normal.
    // D6/D7: with instances resident the walk is two-level and the triangle read is the instance's OBJECT-space copy,
    //    so its face normal is taken to world space by the instance's inverse transpose.
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
        TraversalResult r = TraceTraversal(origin, ray.dir, rD, kInf);
        if (!r.valid || r.t <= kEps) return closest;
        // `flat` is a GLSL keyword (interpolation qualifier), so this identifier is `flatIndex`: the repo's fallback
        //    toolchain (glslc) rejects the reserved word, and both compilers are expected to accept this file.
        const uint flatIndex = FlatPrimitiveOf(r);
        vec3 pos = origin + r.t * ray.dir;
        if (passes != 0u && AlphaMaskCutOut(flatIndex, pos)) { origin = pos + ray.dir * kEps; tBase += r.t + kEps; continue; }

        GpuTriangle tri = Triangles[flatIndex];
        vec3 objectNormal = normalize(cross(tri.v1.xyz - tri.v0.xyz, tri.v2.xyz - tri.v0.xyz));   // R4a: face normal from the edges (slot now carries UVs)
        closest.valid  = true;
        closest.t      = tBase + r.t;
        closest.pos    = pos;
        // ⚠️ The instance case is the inverse transpose: a rotation/scale appears in the object-space triangle, and a
        //    face normal carried out by the forward matrix would tilt the wrong way under a non-uniform instance
        //    scale. With no instances resident this is the same expression the pre-D6 kernel used.
        closest.normal = TlasInstanceCount == 0u ? objectNormal
                        : normalize(InstanceNormalToWorld(TlasInstances[r.instance], objectNormal));
        closest.matIdx = floatBitsToUint(tri.v0.w);
        closest.primitive = flatIndex;
        closest.instance  = InstanceOfPrimitive(flatIndex);   // D2: identity is the (instance, primitive) pair
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
        return TraceTraversalOccluded(origin + dir * kEps, dir, rD, tMax);   // R3 fast path: any-hit, no material fetch (Cornell unchanged)

    // R4b: closest-hit walk that steps through cut-out texels (foliage cards); an opaque hit before the light occludes.
    //    D6/D7: the same walk over the two-level traversal when instances are resident (TraceTraversal picks the arm).
    vec3 o = origin + dir * kEps;
    for (uint pass = 0u; pass <= kAlphaMaskRetraceLimit; ++pass)
    {
        TraversalResult r = TraceTraversal(o, dir, rD, tMax);
        if (!r.valid) return false;
        vec3 pos = o + r.t * dir;
        if (!AlphaMaskCutOut(FlatPrimitiveOf(r), pos)) return true;
        o = pos + dir * kEps; tMax -= r.t + kEps;
        if (tMax <= 0.0) return false;
    }
    return true;   // more cut-out layers than the limit: treat as occluded (conservative, never leaks light)
}

// K5: the SSS geometric chord (CPU SssChord mirror) — an inward raycast from just below the surface along −N; t is
// the first exit the double-sided traversal catches (backface for convex balls — the MT has no det-sign cull, so
// backfaces hit). Miss ⟹ t = 1e30 (Beer 0): the ray crossed the whole volume. THE OPEN-PLANE RULE: an open sheet
// has no exit — its chord misses, Beer = 0, the SSS arm goes dark. Correct: there is no volume behind an open
// plane to backlight through. The chord runs along −N (shading normal), not the view ray, matching the wrap's
// view-independence; concave partial-chords fold into t as medium (v1 honest approximation, CPU-mirrored).
float SssChord(vec3 P, vec3 N)
{
    Ray chordRay;
    chordRay.origin = P - N * 3e-4;   // pure ray offset below the surface (M4b-analog: never lands on-surface)
    chordRay.dir = -N;
    Hit h = TraceScene(chordRay);
    return h.valid ? h.t : 1e30;
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

// Indirect-contribution bound. Applied to every term the BOUNCE adds to the image (its NEE arms and the GI pool's
//    shade), never to the primary hit's direct lighting — clamping direct light would visibly darken a surface that
//    legitimately faces a bright luminaire, whereas an indirect sample this large is always a sampling artefact.
const float kIndirectFireflyBound = 64.0;

vec3 BoundIndirect(vec3 contribution)
{
    // Scale the whole colour by its peak rather than clamping per channel: a per-channel min() would shift the hue
    //    of any sample that crosses the bound in one channel only, turning a white firefly into a coloured one.
    float peak = max(contribution.r, max(contribution.g, contribution.b));
    return peak > kIndirectFireflyBound ? contribution * (kIndirectFireflyBound / peak) : contribution;
}

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
float PHatSun(SurfaceFrame F, ShadingRecord m, ResolvedLayers L, vec3 wo, vec3 sunEmit, vec3 sunDir)
{
    float cosT = max(0.0, dot(F.Geometric, sunDir));
    if (cosT <= 0.0) return 0.0;
    vec3 wi = ToLocal(F, sunDir);
    if (wi.z <= 0.0) return 0.0;
    return max(dot(EvaluateBsdf(m, L, wo, wi), sunEmit), 0.0) * cosT;
}

// p̂ of a STORED sample — the one question every merge asks, answered for both light kinds. A sun sample's
//    direction is rebuilt from its far point, exactly: the point is 1e4 m out, so the rebuild differs from the
//    sampled direction by ~1e-4 rad — inside the disc, where the pdf is still 1/Ω.
float PHatSelected(SurfaceFrame F, ShadingRecord m, ResolvedLayers L, vec3 wo, uint light, vec3 toLight, vec3 hitPos)
{
    if (light == kSunLightIndex)
    {
        vec3 sunDir = normalize(toLight);
        return PHatSun(F, m, L, wo, SunEmission() * CloudShadowTransmittance(hitPos, sunDir), sunDir);
    }
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
        float pHat = PHatSun(F, m, L, wo, SunEmission() * CloudShadowTransmittance(hitPos, sunDir), sunDir);
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

// ⚠️ D10: normal + depth agree for TWO DIFFERENT OBJECTS more often than it is comfortable to admit — two objects
//    exchanging places, a tumble that brings a different face to the same pixel at the same distance, an object leaving
//    a wall while another arrives to take its place. All of those reproject cleanly under the 25°/10 % rule and inherit
//    a history that was shaded from something else, which is ghosting: a mean that averages two different materials'
//    radiance and takes hundreds of frames to forget. The normal/depth rule is not wrong, it is incomplete — the third
//    thing a history has to remember about the surface it came from is WHICH surface it was.
//
// The identity is the packed (instance, primitive) the visibility raster hands this shader, so it is not a new fact
//    about the scene. A mismatch is a disocclusion in the only sense an accumulator cares about — the surface here is
//    not the surface there — and the honest answer is the one a failed depth test already gets: restart at n = 1.
//
// Depth < 0 marks "no surface" (background); such a pixel never reprojects and never becomes a valid source.
// `identity` is 0xFFFFFFFF for a pixel with no surface.
void ResolveSurface(uvec2 pixel, vec3 radiance, vec3 normal, float depth, uint identity)
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
            // The moments were already being read here for the reprojected mean; D10 makes that same load carry the
            //    identity too, so the feature costs no additional fetch — and when it is off, the z/w decode is simply
            //    never looked at.
            const vec4 prevMoments = imageLoad(MomentImage, prevPx);
            bool identityOk = true;
            if ((FeatureFlags & kFeatureTemporalIdentity) != 0u)
                identityOk = IdentityObject(DecodeIdentity(prevMoments)) == IdentityObject(identity);
            // prevSurface.w <= 0 means that pixel held no surface last frame, so there is nothing to inherit.
            if (prevSurface.w > 0.0
                && identityOk
                && dot(normal, prevSurface.xyz) > kReprojectNormalCos
                && abs(depth - prevSurface.w) / max(depth, 1e-3) < kReprojectDepthTol)
            {
                history  = imageLoad(HistoryImage, prevPx);
                count    = history.a;
                moments  = prevMoments.xy;   // the moments follow the same reprojection
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
    // D10: WHICH surface this mean was shaded from, in the moment image's reserved z/w. Stored on every frame whether
    //    the feature is on or not — it is one channel write, and a history written while the feature was off must not
    //    make the first frame with it on restart every pixel in the image.
    imageStore(MomentImage,      ivec2(pixel), vec4(moments, IdentityLow(identity), IdentityHigh(identity)));

    // With the denoiser on, the filter owns the tone map: it needs LINEAR radiance, and tone mapping before a blur
    //    would average display-space values and darken every gradient. The final à-trous level writes OutputImage.
    imageStore(DenoiseImage, ivec2(pixel), vec4(mean, variance));
    if ((FeatureFlags & kFeatureDenoise) == 0u)
        imageStore(OutputImage, ivec2(pixel), vec4(ToneMap(mean), 1.0));
}

// Background and other surface-less writes: no normal, no depth, never a reprojection source.
void Resolve(uvec2 pixel, vec3 radiance)
{
    ResolveSurface(pixel, radiance, vec3(0.0, 0.0, 1.0), -1.0, kIdentityNone);
}
//------------------------------------------------------------------------------------------------------------------------
// 4b. The indirect half's pool — ReSTIR GI-style reuse of the first-bounce vertex's NEE stratum (kFeatureGiReuse)
//------------------------------------------------------------------------------------------------------------------------
// The direct pool's samples live on the PRIMARY surface; these live on the FIRST-BOUNCE vertex. Everything else is the
//    same machinery: RIS candidates drawn at the vertex, a temporal merge against the previous frame's GI reservoir
//    (reprojected by the pixel's motion vector, validated by the same 25 deg / 10 % pair against the pixel's own
//    PRIMARY depth and normal — carried in the record — because the reprojection is the pixel's motion), the same
//    kSpatialTapCount cross, each tap's M capped against the receiver's PRE-merge count, and one visibility re-trace at
//    the vertex before the shade. The history carries the TEMPORAL reservoir only, exactly as for the direct pool:
//    spatial reuse stays a shading-time refinement, so it cannot inflate the M the next frame's temporal cap reasons
//    about (that fault is measured in the CPU mirror at 843 633 mean M by frame 20).
//
//    Why a light sample can cross vertices at all: the transferred quantity is a WORLD POINT on a light, not a
//    direction. The receiver re-evaluates its own target function against that point (PHatSelected at ITS vertex) and
//    re-traces the shadow ray — the same validity test the direct pool's taps use. A stricter "the two vertices must be
//    nearly the same place" rule was tried in the CPU mirror and rejected: each frame redraws its own primary BSDF
//    direction, so the vertices are different points almost always, and the test killed 6 989 of 20 831 temporal
//    attempts while buying nothing.
//
//    Glass, SSS and interior vertices are excluded and keep the single-sample NEE: the candidate drawer has no
//    below-stratum, and a refraction site is not a NEE site. The pool REPLACES the vertex's own NEE, so the caller
//    takes one or the other, never both — and the deeper bounces continue from the same vertex direction as before.
//
//    CPU mirror (the proof harness, line for line): Projects/Project-Zero/Host/MaterialLevelViewport.cpp —
//    `RestirGiTemporalReservoir` + `RestirGiSpatialShade`, with `--restir-no-gi-reuse` as the A/B. Measured there at
//    240x135 / 4 candidates / 2 taps against a 512-spp reference: 7 387.34 vs 7 508.13 (32 frames), 7 223.23 vs
//    7 294.72 (64), 7 631.62 vs 7 680.03 (128); the pool exists on ~16 % of surface pixels (the share whose own
//    primary BSDF sample hits geometry rather than the sky), shaded M ~10, ~25 % of selections occluded.
//
//    Writes GiCurrReservoirs[pixel] unconditionally (zeros when the vertex is ineligible) so a stale entry cannot be
//    reused across a frame in which this pixel had no vertex.
vec3 GiPoolShade(SurfaceFrame F, ShadingRecord m, ResolvedLayers L, vec3 wo, vec3 hitPos,
                 vec3 primaryNormal, float primaryT, uint primaryIdentity, uvec2 pixel, bool sunUp, inout uint seed)
{
    Reservoir ris;
    ris.SelectedPoint  = vec3(0.0);
    ris.SelectedLight  = kSunLightIndex;
    ris.SelectedUv     = vec2(0.0);
    ris.WeightSum      = 0.0;
    ris.SampleCount    = 0u;
    ris.UnbiasedWeight = 0.0;

    // 1. Candidates at the vertex. Same drawer, same pdfs — the light-pick coin, the sun's cone sample and the
    //    triangle-uniform point are the ones the shading estimate divides by (shade and target must agree).
    for (uint s = 0u; s < SamplesPerPixel; ++s)
    {
        vec3  lp; uint li; vec2 uv; float w;
        DrawDirectCandidate(F, m, L, wo, hitPos, sunUp, lp, li, uv, w, seed);
        ResampleCandidate(ris, lp, li, uv, w, seed);
    }
    // The tier's extra same-pixel candidates count for the indirect pool too: it is the same dial, and leaving them to
    //    the direct pool alone would make the tiers mean two different things.
    for (uint e = 0u; e < ExtraCandidateCount; ++e)
    {
        vec3  lp; uint li; vec2 uv; float w;
        DrawDirectCandidate(F, m, L, wo, hitPos, sunUp, lp, li, uv, w, seed);
        ResampleCandidate(ris, lp, li, uv, w, seed);
    }

    float pSelected = 0.0;
    if (ris.WeightSum > 0.0 && ris.SampleCount > 0u)
    {
        pSelected = PHatSelected(F, m, L, wo, ris.SelectedLight, ris.SelectedPoint - hitPos, hitPos);
        ris.UnbiasedWeight = pSelected > 0.0 ? ris.WeightSum / (float(ris.SampleCount) * pSelected) : 0.0;
    }

    // 2. Temporal reuse from the previous frame's pool.
    uint giAge = 0u;
    if ((FeatureFlags & kFeatureTemporalReuse) != 0u && FrameIndex > 0u)
    {
        vec2 cuv = (vec2(pixel) + 0.5) / vec2(float(ViewportWidth), float(ViewportHeight));
        vec2 motion = texelFetch(MotionImage, ivec2(pixel), 0).rg;
        vec2 puv = cuv - motion;
        ivec2 prevPx = ivec2(floor(puv * vec2(float(ViewportWidth), float(ViewportHeight))));
        if (all(greaterThanEqual(prevPx, ivec2(0))) && prevPx.x < int(ViewportWidth) && prevPx.y < int(ViewportHeight))
        {
            GpuReservoir prev = GiPrevReservoirs[prevPx.y * int(ViewportWidth) + prevPx.x];
            uint prevM = prev.Counts.x;
            bool valid = prevM > 0u
                && prev.Normal.w == float(ViewportWidth)   // stride guard (render-scale change)
                && dot(primaryNormal, prev.Normal.xyz) > kTemporalNormalCos
                && abs(primaryT - prev.UvDepth.w) / max(primaryT, 1e-3) < kTemporalDepthTol
                // D10: the same surface. It matters more here than in the direct pool: the pooled sample is a light
                //    point chosen at the FIRST-BOUNCE VERTEX, so inheriting one across a surface change inherits an
                //    occlusion test and a BRDF evaluated somewhere else — which reads as a reflection of an object
                //    that is no longer there, and clears only as 1/n.
                && ((FeatureFlags & kFeatureTemporalIdentity) == 0u
                    || IdentityObject(prev.Identity.x) == IdentityObject(primaryIdentity));
            if (valid)
            {
                uint mPrevCapped = min(prevM, kTemporalMClamp * ris.SampleCount);
                float pPrev = PHatSelected(F, m, L, wo, prev.Counts.y, prev.Sample.xyz - hitPos, hitPos);
                float wCur  = pSelected * ris.UnbiasedWeight * float(ris.SampleCount);
                float wPrev = pPrev * prev.UvDepth.z * float(mPrevCapped);
                float total = wCur + wPrev;
                if (total > 0.0 && RandFloat(seed) * total <= wPrev)
                {
                    ris.SelectedPoint = prev.Sample.xyz;
                    ris.SelectedLight = prev.Counts.y;
                    ris.SelectedUv    = prev.UvDepth.xy;
                    pSelected         = pPrev;
                }
                ris.SampleCount = ris.SampleCount + mPrevCapped;
                ris.WeightSum   = total;
                giAge = prev.Counts.w + 1u;
                ris.UnbiasedWeight = pSelected > 0.0 ? total / (float(ris.SampleCount) * pSelected) : 0.0;
            }
        }
    }

    // ⚠️ Publish the TEMPORAL reservoir before the taps touch it — the direct pool's rule, and the same measured fault
    //    if broken: spatial merges feeding the next frame's temporal cap compounded M to 843 633 by frame 20 (mirror).
    {
        GpuReservoir hist;
        hist.Sample  = vec4(ris.SelectedPoint, ris.WeightSum);
        hist.Counts  = uvec4(ris.SampleCount, ris.SelectedLight, 1u, giAge);
        hist.UvDepth = vec4(ris.SelectedUv, ris.UnbiasedWeight, primaryT);
        hist.Normal   = vec4(primaryNormal, float(ViewportWidth));
        hist.Identity = uvec4(primaryIdentity, 0u, 0u, 0u);   // D10: which surface this reservoir belongs to
        GiCurrReservoirs[pixel.y * ViewportWidth + pixel.x] = hist;
    }

    // 3. Spatial taps — the same cross, read from the PREVIOUS frame's pool (a same-frame neighbour read would race
    //    inside a dispatch), each neighbour's M capped against this pixel's pre-merge count.
    if ((FeatureFlags & kFeatureSpatialReuse) != 0u && FrameIndex > 0u)
    {
        uint  tapSeed = PcgHash(MakeSeed(pixel, FrameIndex) ^ 0x85EBCA6Bu);
        float scale   = float(ViewportWidth) / 1280.0;
        float angle   = RandFloat(tapSeed) * 6.28318531;
        float radius  = mix(kSpatialRadiusMinPx, kSpatialRadiusMaxPx, RandFloat(tapSeed)) * scale;
        uint  spatialTaps = min(SpatialTapCount, kSpatialTapCeiling);
        uint  capReference = max(ris.SampleCount, 1u);
        for (uint tap = 0u; tap < spatialTaps; ++tap)
        {
            float theta  = angle + float(tap) * (6.28318531 / float(spatialTaps));
            ivec2 offset = ivec2(int(round(radius * cos(theta))), int(round(radius * sin(theta))));
            if (offset.x == 0 && offset.y == 0) continue;
            ivec2 npx = ivec2(pixel) + offset;
            if (any(lessThan(npx, ivec2(0))) || npx.x >= int(ViewportWidth) || npx.y >= int(ViewportHeight)) continue;
            GpuReservoir neigh = GiPrevReservoirs[npx.y * int(ViewportWidth) + npx.x];
            uint neighM = neigh.Counts.x;
            bool nValid = neighM > 0u
                && neigh.Normal.w == float(ViewportWidth)
                && dot(primaryNormal, neigh.Normal.xyz) > kTemporalNormalCos
                && abs(primaryT - neigh.UvDepth.w) / max(primaryT, 1e-3) < kTemporalDepthTol;
            if (!nValid) continue;
            uint mNeighCapped = min(neighM, kTemporalMClamp * capReference);
            float pNeigh = PHatSelected(F, m, L, wo, neigh.Counts.y, neigh.Sample.xyz - hitPos, hitPos);
            float wSelf  = pSelected * ris.UnbiasedWeight * float(ris.SampleCount);
            float wNeigh = pNeigh * neigh.UvDepth.z * float(mNeighCapped);
            float total  = wSelf + wNeigh;
            uint  takeAge = giAge;
            if (total > 0.0 && RandFloat(seed) * total <= wNeigh)
            {
                ris.SelectedPoint = neigh.Sample.xyz;
                ris.SelectedLight = neigh.Counts.y;
                ris.SelectedUv    = neigh.UvDepth.xy;
                takeAge   = neigh.Counts.w + 1u;
                pSelected = pNeigh;
            }
            ris.SampleCount = ris.SampleCount + mNeighCapped;
            ris.WeightSum   = total;
            giAge = takeAge;
            ris.UnbiasedWeight = pSelected > 0.0 ? total / (float(ris.SampleCount) * pSelected) : 0.0;
        }
    }

    // 4. Visibility re-traced at the vertex for whichever sample the merges selected — an occluded selection
    //    contributes nothing this frame (the direct pool's rule at the primary).
    bool blocked = ris.SelectedLight == kSunLightIndex
        ? TraceShadow(hitPos + F.Geometric * kEps, ris.SelectedPoint)
        : TraceShadow(hitPos + F.Geometric * kEps, ris.SelectedPoint - normalize(ris.SelectedPoint - hitPos) * 1e-3);
    if (blocked || ris.SampleCount == 0u) return vec3(0.0);

    // 5. Shade at the vertex. The sun divides by nothing; a mesh light pays the emitter cosine the target pays.
    vec3 shadeDir = normalize(ris.SelectedPoint - hitPos);
    float shadeCos = max(0.0, dot(F.Geometric, shadeDir));
    vec3 wiLocal = ToLocal(F, shadeDir);
    vec3 f = wiLocal.z > 0.0 ? EvaluateBsdf(m, L, wo, wiLocal) : vec3(0.0);
    if (ris.SelectedLight == kSunLightIndex)
        return f * SunEmission() * shadeCos * ris.UnbiasedWeight;
    vec3 chosenEmit = LightEmission(ris.SelectedLight);
    float shadeCosL = max(0.0, dot(LightNormal(ris.SelectedLight), -shadeDir));
    float dist2 = dot(ris.SelectedPoint - hitPos, ris.SelectedPoint - hitPos);
    return f * chosenEmit * shadeCos * shadeCosL * ris.UnbiasedWeight / (dist2 + 0.01);
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    MAIN COMPUTE KERNEL
//------------------------------------------------------------------------------------------------------------------------

void main()
{
    uvec2 pixel = gl_GlobalInvocationID.xy;
    if (pixel.x >= ViewportWidth || pixel.y >= ViewportHeight) return;

    // The indirect pool's slot for this pixel is cleared up front (kFeatureGiReuse): every early return below — sky,
    //    an emissive hit, an unlit material, the rain column's own early out — must not leave a stale entry for a
    //    neighbour's tap to reuse next frame. The pool overwrites this slot when it runs.
    if ((FeatureFlags & kFeatureGiReuse) != 0u)
    {
        GpuReservoir giClear;
        giClear.Sample  = vec4(0.0);
        giClear.Counts  = uvec4(0u);
        giClear.UvDepth = vec4(0.0);
        giClear.Normal   = vec4(0.0, 0.0, 1.0, float(ViewportWidth));
        // M = 0 is what makes this slot invalid; the identity still says "no surface" rather than "instance 0,
        //    primitive 0", which is a real surface and would read as a match if a decode ever looked at it.
        giClear.Identity = uvec4(kIdentityNone, 0u, 0u, 0u);
        GiCurrReservoirs[pixel.y * ViewportWidth + pixel.x] = giClear;
    }

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
        Resolve(pixel, SkyAlong(direction) + RainbowAlong(direction, SkySunDirection.xyz, 1.0e6));
        return;
    }

    // D10: the identity every temporal validation in this pixel compares against — the raster's packed
    //    (instance, primitive), which is the same number the visibility buffer already carries. Nothing new is derived.
    const uint primaryIdentity = visibility;

    GpuInstance primaryInstance = Instances[VisibilityInstance(visibility)];
    GpuMaterial mat = Materials[primaryInstance.MaterialIndex];

    vec3  hitPos     = surface.xyz;
    vec3  toCamera   = CameraOrigin - hitPos;
    float primaryT   = length(toCamera);
    vec3  viewDir    = toCamera / max(primaryT, 1e-6);
    float coneWidth  = primaryT * PixelSpreadAngle();             // ray-cone footprint at the primary hit [m]

    SurfaceFrame  frame;
    ShadingRecord shading = ResolveMaterial(mat, primaryInstance, VisibilityPrimitive(visibility), hitPos, viewDir, coneWidth, frame);

    // If we directly hit an emitter — return its radiance (slab emission × texture; the header's radiance drives the luminaire table).
    // EmissiveOnly selections always land here: their derivation guarantees luminance > 0 with nothing reflective.
    if (dot(frame.Emission, frame.Emission) > 0.0)
    {
        Resolve(pixel, frame.Emission);
        return;
    }

    // M1: Unlit (KHR_materials_unlit) — base colour is the radiance, no lighting evaluated.
    if (MaterialSelection(mat) == kReflectanceUnlit)
    {
        Resolve(pixel, shading.BaseColor);
        return;
    }

    vec3 hitNormal = frame.Geometric;                             // shadow-ray offsets and the DI target function stay geometric
    vec3 woLocal   = ToLocal(frame, viewDir);
    if (woLocal.z <= 0.0) woLocal = vec3(woLocal.xy, 1e-3);      // grazing normal-mapped case: keep the BSDF defined
    // K5: per-hit SSS chord (CPU mirror — before the resolve like the CPU, though the resolve never reads thickness;
    // the wrap's Beer needs the true exit. 0 default = unset; 1e30 = missed/open = Beer 0. SSS-off mats skip the
    // raycast entirely, so opaque/SSS-off panels keep their bytes exactly.)
    if (shading.SssWeight > 0.0) shading.SssThickness = SssChord(hitPos, frame.Normal);
    ResolvedLayers layers = ResolveLayers(shading, woLocal);
    // K4: solid entry — the primary ray comes from air (IncidentIor = 1.0 default), so the entry interface is the
    // bare single step, not the foil double-refraction (CPU mirror: solidHit ⟹ SolidInterface, IncidentIor = 1.0
    // outside). Inert for DI (above-horizon only — the T arm never fires there) and for opaque/foil.
    bool solidPrimary = !frame.ThinWalled && layers.TransmitMix > 0.0;
    if (solidPrimary) layers.SolidInterface = true;
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
                pSelected   = PHatSelected(frame, shading, layers, woLocal, res.SelectedLight, toSel2, hitPos);
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
                        && abs(primaryT - prev.UvDepth.w) / max(primaryT, 1e-3) < kTemporalDepthTol
                        // D10: and the same surface. Two objects exchanging places pass the normal and the depth test
                        //    at both pixels; without this the reservoir carries the other object's light sample, so the
                        //    pixel is lit by a sample chosen for geometry that is no longer there.
                        && ((FeatureFlags & kFeatureTemporalIdentity) == 0u
                            || IdentityObject(prev.Identity.x) == IdentityObject(primaryIdentity));
                    if (valid)
                    {
                        uint mPrevCapped = min(prevM, kTemporalMClamp * res.SampleCount);
                        vec3 prevToL  = prev.Sample.xyz - hitPos;
                        float pPrev = PHatSelected(frame, shading, layers, woLocal, prev.Counts.y, prevToL, hitPos);
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

            // Visibility re-traced at the current pixel for the TEMPORAL reservoir: the history's W has to be an
            //    honest weight, and the two passes are two dispatches — the spatial pass cannot validate a value it
            //    is not allowed to write back. The spatial pass re-traces for whichever sample it ends up shading.
            {
                bool histBlocked = res.SelectedLight == kSunLightIndex
                    ? TraceShadow(hitPos + hitNormal * kEps, res.SelectedPoint)
                    : TraceShadow(hitPos + hitNormal * kEps, res.SelectedPoint - normalize(res.SelectedPoint - hitPos) * 1e-3);
                res.Visible = histBlocked ? 0u : 1u;
                if (res.Visible == 0u) res.UnbiasedWeight = 0.0;
            }

            // The temporal reservoir is what the history takes (see the publish block at the end of the DI block):
            //    snapshot the four quantities the reservoir is made of before the spatial taps touch them.
            vec3     histPoint     = res.SelectedPoint;
            vec2     histUv        = res.SelectedUv;   // vec2: UvDepth packs (uv, W, view depth) as one vec4 below
            uint     histLight     = res.SelectedLight;
            uint     histCount     = res.SampleCount;
            uint     histVisible   = res.Visible;
            uint     histAge       = resAge;
            float    histWeightSum = res.WeightSum;
            float    histW         = res.UnbiasedWeight;

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

                // ⚠️ The cap reference is the receiver's PRE-merge sample count (`histCount`, captured above). The
                //    old form capped against `res.SampleCount` while the loop added to it, so tap 2 could add up to
                //    20x what tap 1 had just added — compounding inside a single frame, and (with the spatial result
                //    in the history) compounding again across frames. Measured in the CPU mirror, where the same fix
                //    lands: mean M at frame 16 fell from 1879 (max 4436) to 41.6 (max 64).
                uint capReference = max(histCount, 1u);
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
                    uint mNeighCapped = min(neighM, kTemporalMClamp * capReference);
                    vec3 neighToL  = neigh.Sample.xyz - hitPos;
                    float pNeigh = PHatSelected(frame, shading, layers, woLocal, neigh.Counts.y, neighToL, hitPos);
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

            // Visibility re-traced a second time, now for the sample the SPATIAL merge selected: the merged
            //    selection may differ from the temporal one, so the shaded integral cannot inherit its test.
            res.Visible = TraceShadow(hitPos + hitNormal * kEps, res.SelectedPoint) ? 0u : 1u;
            if (res.Visible == 0u) res.UnbiasedWeight = 0.0;

            // Publish the reservoir for next frame's temporal reuse (always — even occluded, M still counts).
            //
            //    ⚠️ THIS MUST BE THE TEMPORAL RESERVOIR, NOT THE POST-SPATIAL ONE. Spatial reuse is a shading-time
            //    refinement: folding it into the history makes every frame's spatial merges raise the M that the
            //    next frame's temporal cap reasons about, so M compounds through both passes instead of growing
            //    once per frame. That feedback loop is what broke convergence — with it, `taps 0` converged to
            //    1952 RMSE by 128 frames while 2 taps stalled at 4437 and 4 at 4660, mean M reached 843 633 at
            //    frame 20, and the CPU mirror reproduced all of it line for line. With the split — and the per-tap
            //    cap below taken against the pre-merge count — more taps now strictly help at every budget
            //    (240x135, 4 candidates: 32/64/128 frames, taps 4 = 7462/7163/7599 vs taps 0 = 7843/7623/8082).
            //    (Kajiya's ReSTIR notes state the same rule: don't feed the spatial result back into temporal.)
            GpuReservoir histRes;
            histRes.Sample  = vec4(histPoint, histWeightSum);
            histRes.Counts  = uvec4(histCount, histLight, histVisible, histAge);
            histRes.UvDepth = vec4(histUv, histW, primaryT);
            histRes.Normal   = vec4(hitNormal, float(ViewportWidth));
            histRes.Identity = uvec4(primaryIdentity, 0u, 0u, 0u);   // D10: which surface this reservoir belongs to
            CurrReservoirs[pixel.y * ViewportWidth + pixel.x] = histRes;

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
                    accumulatedRadiance += f * SunEmission() * shadeCos * res.UnbiasedWeight;
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
    // K5: SSS below-stratum NEE (CPU DirectMISsss mirror) — the ONLY direct term the wrap can see. The reservoir
    // above owns wi.z ≥ 0 (partition — no double count) and the BSDF never lands below for SSS mats (TransmitMix =
    // 0 keeps the opaque-below rule, pdf exactly 0 there), so without this stratum SSS-direct would be exactly 0.
    // Single-strategy, NO shadow ray — the chord Beer replaces the visibility test: light behind the surface shines
    // THROUGH, attenuated, not blocked. Sun + lamps by the same coin the bounce NEE uses (same pdfs, same forms).
    //------------------------------------------------------------------------------------------------------------------
    if (layers.SssMix > 0.0 && (sunUp || LightTriangleCount > 0u))
    {
        bool sunBelow = sunUp && (LightTriangleCount == 0u || RandFloat(seed) < kSunPickProbability);
        if (sunBelow)
        {
            vec3 sSunDir = SampleSunDirection(SkySunDirection.xyz, RandFloat(seed), RandFloat(seed));
            float sCos = -dot(hitNormal, sSunDir);   // world-below cosine (the measure's |cos|)
            vec3  sWi  = ToLocal(frame, sSunDir);
            if (sCos > 0.0 && sWi.z < 0.0)
            {
                vec3  sF = EvaluateBsdf(shading, layers, woLocal, sWi);   // below-branch: SSS (+ T when mixed)
                float pPick = LightTriangleCount > 0u ? kSunPickProbability : 1.0;
                accumulatedRadiance += sF * SunEmission() * sCos * SunSolidAngle() / pPick;
            }
        }
        else
        {
            const uint sSlots = LightTriangleCount;
            uint  sLi  = min(uint(RandFloat(seed) * float(sSlots)), sSlots - 1u);
            vec3  sLp  = SampleLightPointFor(sLi, RandFloat(seed), RandFloat(seed));
            vec3  sToL = sLp - hitPos;
            float sDist = length(sToL);
            vec3  sLDir = sToL / sDist;
            float sCos  = -dot(hitNormal, sLDir);
            float sCosL = dot(LightNormal(sLi), -sLDir);
            vec3  sWi   = ToLocal(frame, sLDir);
            // Grazing-epsilon on cosL (CPU mirror — the MIS-less stratum NEEDS it: W = 1 can't bound the 1/Pl blowup
            // the above-stratum's MIS absorbs).
            if (sCos > 0.0 && sCosL > 1e-3 && sWi.z < 0.0)
            {
                float sDist2 = sDist * sDist;
                vec3  sF = EvaluateBsdf(shading, layers, woLocal, sWi);
                float pMesh = sunUp ? 1.0 - kSunPickProbability : 1.0;
                accumulatedRadiance += sF * LightEmission(sLi) * sCos * sCosL
                                     * Luminaires[sLi].Area * float(sSlots) / ((sDist2 + 0.01) * pMesh);
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
        vec4 bounce = SampleBsdf(shading, layers, woLocal, vec4(RandFloat(seed), RandFloat(seed), RandFloat(seed), RandFloat(seed)));   // K1: u.w seeds the M4 R/T branch — vec3 never compiled
        // M5 v2: dipole-virtuals die here — a below-bounce from a pure-SSS mat is a BSDF-sampled backlight
        // direction, not a transport vertex, and the K5 light-sampled below-stratum already estimates that same
        // integral every primary. Using one estimator and dropping the other is unbiased (just no variance
        // reduction); collecting both without MIS would double-count. The CPU exhibit runs both strata WITH MIS
        // (furnace-⑩) — the kernel follows when it grows MIS (M9+). One wasted sample, no ray traced.
        if (bounce.w > 0.0 && !(bounce.z < 0.0 && layers.TransmitMix <= 0.0 && layers.SssMix > 0.0))
        {
            vec3 bounceDir = FromLocal(frame, bounce.xyz);
            // K4: medium entry — below-horizon exit from a solid primary crosses into the glass (CPU mirror: wi.z < 0
            // + solidHit, EON/coat-below included since f = f_T > 0 there). Foil crossings stay outside (no inside).
            bool enteredSolid = solidPrimary && bounce.z < 0.0;
            vec3 throughput = EvaluateBsdf(shading, layers, woLocal, bounce.xyz) * abs(bounce.z) / bounce.w;   // K3: f · |cosθ| / pdf — the full-sphere T measure is symmetric (R: wi.z > 0, bit-identical)
            throughput = min(throughput, vec3(8.0));   // K4: firefly clamp (CPU mirror — softboxes through smooth glass)
            // ⚠️ The throughput clamp above does NOT bound what the bounce's NEE adds. Those estimators divide by
            //    (dist² + 0.01) and multiply by the emitter area and the slot count, so a bounce landing a few
            //    centimetres from a luminaire — or one whose cosL grazes zero on the light's own plane — returns a
            //    value orders of magnitude above the local mean. One such sample per few thousand frames is a
            //    firefly: a single pixel that flares white and then decays as the running mean dilutes it. Sunset is
            //    where it showed, because that is when the sun stops dominating and the mesh-light arm starts
            //    winning the coin, so this indirect path carries the image.
            //
            //    The bound is on the CONTRIBUTION, not the estimator, and it is generous (well above any legitimate
            //    indirect sample at these exposures) so converged energy is untouched — it only removes the tail
            //    that the running mean cannot average away in a viewer's lifetime.
            Ray  bounceRay;
            // K4: solid segments take a pure ray offset (CPU mirror — the normal term lands on-surface at near-normal
            // exits and re-hits the exit face); foil/R keep the K3 ray-side normal offset.
            bounceRay.origin = enteredSolid ? hitPos + bounceDir * 3e-4 : hitPos + hitNormal * (dot(bounceDir, hitNormal) > 0.0 ? kEps : -kEps);
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
            // K3: no above-horizon gate — below-horizon w > 0 bounces exist only when TransmitMix > 0 (opaque kills
            // them in SampleBsdf), so opaque is untouched and T-paths collect the environment / endpoints below.
            if (!bounceHit.valid)
            {
                // K4: entered solid but never exited (open mesh / numeric leak — CPU mirror): nominal-Beer fallback
                // over the slab thickness, then out. Thickness is 0 (not in the slab) so this is ×1 today — live
                // the day the slab carries it. Closed meshes never land here.
                if (enteredSolid) throughput *= exp(-layers.TransmissionSigma * shading.TransmissionThickness);
                accumulatedRadiance += throughput * SkyAlong(bounceDir);
            }
            if (bounceHit.valid)
            {
                // K4: Beer over the true interior segment just travelled (CPU mirror — EntrySigma captured at entry,
                // here the primary resolve's sigma; the segment is entry→endpoint for exits AND embedded hits).
                if (enteredSolid) throughput *= exp(-layers.TransmissionSigma * bounceHit.t);
                GpuMaterial   bMat  = Materials[bounceHit.matIdx];
                // D2: TraceScene already resolved the instance — reuse it rather than repeating the search here.
                GpuInstance   bInstance = Instances[bounceHit.instance];
                SurfaceFrame  bFrame;
                float         bCone = coneWidth + bounceHit.t * PixelSpreadAngle();
                ShadingRecord bShading = ResolveMaterial(bMat, bInstance, LocalPrimitive(bounceHit.primitive, bounceHit.instance), bounceHit.pos, -bounceDir, bCone, bFrame);
                // K4: medium context at the endpoint (CPU mirror). A same-material hit from inside is the exit
                // backface (glass→air override below); a DIFFERENT transmissive object embedded in the glass
                // shades R-only (v1: no nested dielectrics — and the ray stays inside).
                bool bFromInside = enteredSolid && bounceHit.matIdx == primaryInstance.MaterialIndex;
                if (enteredSolid && !bFromInside && bShading.TransmissionWeight > 0.0) bShading.TransmissionWeight = 0.0;
                // K5: endpoint chord (CPU mirror — interior-SSS is future work, and NEE skips inside anyway).
                if (!bFromInside && bShading.SssWeight > 0.0) bShading.SssThickness = SssChord(bounceHit.pos, bFrame.Normal);

                // M1: an unlit bounce endpoint contributes its base colour exactly like an emitter (constant radiance).
                vec3 bUnlit = MaterialSelection(bMat) == kReflectanceUnlit ? bShading.BaseColor : vec3(0.0);
                if (dot(bFrame.Emission, bFrame.Emission) > 0.0 || dot(bUnlit, bUnlit) > 0.0)
                {
                    accumulatedRadiance += throughput * (bFrame.Emission + bUnlit);
                }
                // K4: interior endpoints skip NEE (CPU mirror) — shadow rays treat glass as opaque, so every NEE
                // ray from inside would block at the exit wall; refracted NEE is future work. Emitters above still
                // collect (with the interior Beer already in the throughput).
                else if ((sunUp || LightTriangleCount > 0u) && !bFromInside)
                {
                    vec3  bHitPos    = bounceHit.pos;
                    vec3  bHitNormal = bFrame.Geometric;
                    vec3  bWo        = ToLocal(bFrame, -bounceDir);
                    if (bWo.z <= 0.0) bWo = vec3(bWo.xy, 1e-3);
                    ResolvedLayers bLayers = ResolveLayers(bShading, bWo);
                    // K4: exit interface — the transmitted side is air (IncidentIor rides the medium stack: the
                    // primary glass's eta captured at entry; CPU mirror).
                    if (bFromInside) { bLayers.SolidInterface = true; bLayers.IncidentIor = layers.SpecularEta; }

                    // ── 4b. The indirect pool owns this vertex's NEE when it can (kFeatureGiReuse): it REPLACES the
                    //    single sample below, never adds to it, and the deeper bounces carry on from the same
                    //    direction afterwards. A dielectric vertex, an SSS vertex and an interior vertex keep the
                    //    single-sample arm — the candidate drawer has no below-stratum and a refraction site is not a
                    //    NEE site. (Mirror: RestirGiTemporalReservoir's eligibility gate, and the same partition.)
                    bool giPoolOwns = (FeatureFlags & kFeatureGiReuse) != 0u
                        && !enteredSolid
                        && bShading.TransmissionWeight <= 0.0
                        && bShading.SssWeight <= 0.0;
                    if (giPoolOwns)
                        accumulatedRadiance += BoundIndirect(throughput * GiPoolShade(bFrame, bShading, bLayers, bWo, bHitPos,
                                                                        hitNormal, primaryT, primaryIdentity, pixel, sunUp, seed));
                    else if (true)
                    {
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
                            accumulatedRadiance += BoundIndirect(throughput * bF * SunEmission() * bCos * SunSolidAngle() / pPick);
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
                            accumulatedRadiance += BoundIndirect(throughput * bF * bLEmit * bCos * bCosL
                                                 * Luminaires[bLi].Area * float(bSlots) / ((bDist2 + 0.01) * pMesh));
                        }
                    }
                    }
                    // K5: endpoint below-stratum (the same SSS mirror in the bounce frame; inside the !bFromInside
                    // gate — interior endpoints skip both strata. v-prefix: the bounce frame's below-stratum draws).
                    if (bLayers.SssMix > 0.0)
                    {
                        bool sunBelowV = sunUp && (LightTriangleCount == 0u || RandFloat(seed) < kSunPickProbability);
                        if (sunBelowV)
                        {
                            vec3 vSunDir = SampleSunDirection(SkySunDirection.xyz, RandFloat(seed), RandFloat(seed));
                            float vCos = -dot(bHitNormal, vSunDir);
                            vec3  vWi  = ToLocal(bFrame, vSunDir);
                            if (vCos > 0.0 && vWi.z < 0.0)
                            {
                                vec3  vF = EvaluateBsdf(bShading, bLayers, bWo, vWi);
                                float vPick = LightTriangleCount > 0u ? kSunPickProbability : 1.0;
                                accumulatedRadiance += BoundIndirect(throughput * vF * SunEmission() * vCos * SunSolidAngle() / vPick);
                            }
                        }
                        else
                        {
                            const uint vSlots = LightTriangleCount;
                            uint  vLi  = min(uint(RandFloat(seed) * float(vSlots)), vSlots - 1u);
                            vec3  vLp  = SampleLightPointFor(vLi, RandFloat(seed), RandFloat(seed));
                            vec3  vToL = vLp - bHitPos;
                            float vDist = length(vToL);
                            vec3  vLDir = vToL / vDist;
                            float vCos  = -dot(bHitNormal, vLDir);
                            float vCosL = dot(LightNormal(vLi), -vLDir);
                            vec3  vWi   = ToLocal(bFrame, vLDir);
                            if (vCos > 0.0 && vCosL > 1e-3 && vWi.z < 0.0)
                            {
                                float vDist2 = vDist * vDist;
                                vec3  vF = EvaluateBsdf(bShading, bLayers, bWo, vWi);
                                float vMesh = sunUp ? 1.0 - kSunPickProbability : 1.0;
                                accumulatedRadiance += BoundIndirect(throughput * vF * LightEmission(vLi) * vCos * vCosL
                                                     * Luminaires[vLi].Area * float(vSlots) / ((vDist2 + 0.01) * vMesh));
                            }
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

    // R7a: the primary hit's geometric normal and camera distance are what the next frame validates its
    //    back-projection against — the same pair R6 row 2 uses to validate reprojected reservoirs.
    // D10: and WHICH hit it was — the raster's packed (instance, primitive), already in hand here and the only
    //    additional fact the accumulator needs to tell "this surface moved" from "a different one is here now".
    ResolveSurface(pixel, accumulatedRadiance, hitNormal, primaryT, visibility);
}
