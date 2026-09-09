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
layout(binding = 21) uniform sampler2D TransmittanceLut;  // A2: sun survival, (cos sun zenith, sqrt altitude)
layout(binding = 22) uniform sampler2D MultiScatterLut;   // A2: isotropic multiple-scattering transfer factor
layout(binding = 25) uniform sampler2D Textures[];   // R4a bindless table — ⚠️ moved 11 → 15 in R4b → 18 in R6 → 19 in R7a → 21 in R7 → 23 in A2 → 25 in A7: a variable-count binding must be the last one in the set

const float kTemporalNormalCos = 0.906308;   // cos(25°): temporal/spatial normal validation
const float kTemporalDepthTol  = 0.10;       // 10% relative depth validation
const uint  kTemporalMClamp    = 20u;        // prev M capped at 20× current M (ReSTIR M-clamp)
const uint  kSpatialTaps       = 4u;         // R6 row 3: 4-tap cross — the true spatial reuse (ExtraCandidateCount = extra same-pixel candidates)
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
    float Occlusion;        // baked AO — multiplies the debug ambient floor only
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
#include "AtmosphereScattering.slang"

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
    // A7 ⚠️ The sky parameters used to live here and filled the block to exactly 128 bytes — Vulkan's guaranteed
    //    minimum. The moon and the star field need ~20 bytes more, and growing past the guarantee would work on
    //    a card that offers 256 and fail on one that offers only 128, i.e. break on someone else's machine.
    //    They now live in the SkyRecord uniform buffer, which is also the shape a per-world sky needs (M1).
    // A7d. How much colour the eye still has at the adapted luminance: 1 in daylight, 0 under starlight.
    float   ColourSaturation;       // [-]   taken from one of the reserves, so the block is still 128 B
    uint    PushReserve1;
    uint    PushReserve2;
    uint    PushReserve3;
    uint    PushReserve4;
    uint    PushReserve5;
    uint    PushReserve6;
    uint    PushReserve7;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     SKY RECORD (A7)
//------------------------------------------------------------------------------------------------------------------------
// One uniform buffer describing the sky. A buffer rather than push constants because the block was full, and a
//    buffer rather than more push bytes because this is the shape a PER-WORLD sky needs: when portals arrive
//    this becomes an array indexed by world ordinal and nothing else about the kernel changes (CLAUDE.md §15b).

layout(binding = 24) uniform SkyRecord
{
    vec3    SunDirection;           // [-]   toward the sun, unit, world space (Z-up)
    float   SunIlluminance;         // [lx]  direct-beam illuminance above the atmosphere; 0 disables the sky

    vec3    MoonDirection;          // [-]   toward the moon, unit
    float   MoonPhase;              // [0..1] illuminated fraction; 0 new, 1 full

    float   CameraAltitude;         // [m]   observer height above the planet surface
    float   StarRotation;           // [rad] sidereal rotation of the star field
    float   StarBrightness;         // [cd/m²] 0 disables the stars
    float   MoonIlluminance;        // [lx]  sunlight reaching the moon; 0 disables it

    uint    SkyViewSteps;           // [cnt] quality tier: samples along the view ray
    uint    SkyLightSteps;          // [cnt] quality tier: 0 selects the tabulated path
    float   SkyTurbidity;           // [-]   aerosol load; 1 is the clear reference atmosphere (A7b)
    float   MoonAngularScale;       // [-]   1 is the true 0.259°; larger is an artistic moon (A7c)
};

const uint kFeatureGlobalIllumination = 1u;   // bit 0: trace the secondary bounce (section 4)
const uint kFeatureAntiAliasing       = 2u;   // bit 1: sub-pixel jitter — applied by the R2 raster/resolve, not here
const uint kFeatureAmbientFloor       = 4u;   // bit 2: add albedo × AmbientStrength (debug fill light; default OFF)
const uint kFeatureTemporalReuse      = 8u;   // bit 3: R6 row 2 — temporal reservoir reuse (back-projection + validation)
const uint kFeatureSpatialReuse       = 16u;  // bit 4: R6 row 3 — spatiotemporal neighbour reuse (pairwise MIS)
const uint kFeatureAliasPick          = 32u;  // bit 5: R6 row 3 — Walker-alias light pick (off = uniform, the R0 identity proof)
const uint kFeatureTemporalReprojection = 64u;  // bit 6: R7a — reproject the running mean through the R2 motion vectors
const uint kFeatureDenoise            = 128u; // bit 7: R7 — the à-trous filter runs, so the kernel defers the tone map to it
const uint kFeatureSkyLighting        = 256u; // bit 8: A5 — an escaped bounce ray gathers sky radiance (off = the pre-A5 image)
const uint kFeatureNightSky           = 512u; // bit 9: A7 — the moon disc and the star field (off = the pre-A7 image)

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

// Instance owning a flat primitive slot. Triangles[] is laid out per instance in FlatTriangleOffset order, so this
//    is a search for the last instance whose offset does not exceed the slot. Bounded by instance count.
uint InstanceOfPrimitive(uint flatPrimitive)
{
    uint found = 0u;
    for (uint i = 0u; i + 1u < uint(Instances.length()); ++i)
    {
        if (Instances[i + 1u].FlatTriangleOffset <= flatPrimitive) found = i + 1u;
        else break;
    }
    return found;
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
//                                          A2 — ATMOSPHERE TABLE SAMPLERS
//------------------------------------------------------------------------------------------------------------------------
// Declared here rather than beside the sky evaluators because A4's sun reads the same transmittance: the disc,
//    the sky and the sunlight landing on the ground must redden together, which only happens if all three
//    consult one table.

vec3 SampleTransmittance(float Altitude, float CosSunZenith)
{
    return texture(TransmittanceLut, TransmittanceParameterisation(Altitude, CosSunZenith)).rgb;
}

vec3 SampleMultiScatter(float Altitude, float CosSunZenith)
{
    // ⚠️ Its OWN parameterisation, not the transmittance one. The transmittance mapping cannot express a sun
    //    below the horizon, and a sun below the horizon is exactly when multiple scattering is what you see.
    return texture(MultiScatterLut, MultiScatterParameterisation(Altitude, CosSunZenith)).rgb;
}

//------------------------------------------------------------------------------------------------------------------------
//                                            A4 — THE SUN AS A LIGHT
//------------------------------------------------------------------------------------------------------------------------
// The sun has no triangle, so it is addressed by the index ONE PAST the last luminaire triangle. That keeps the
//    reservoir's existing uint light index working untouched, which matters because the temporal and spatial
//    reuse paths carry that index across frames and neighbours.
//
// 🔴 Every read of a light's emission or position now goes through these two accessors. The emission used to be
//    fetched inline at eight separate sites; adding the sun to each of those would have been eight chances to
//    forget one, and a missed site would have read a scene material for the sun — a plausible wrong colour
//    rather than an obvious failure.

bool IsSunLight(uint lightIndex) { return SunIlluminance > 0.0 && lightIndex >= LightTriangleCount; }

// How many light "slots" a pixel can choose from: the emissive triangles, plus the sun when it is above the
//    horizon. Below the horizon it is not a candidate at all, so no sample is ever wasted on a light that
//    cannot contribute.
uint LightSlotCount()
{
    return LightTriangleCount + ((SunIlluminance > 0.0 && SunDirection.z > -0.05) ? 1u : 0u);
}

// The sun's radiance, attenuated by the air it has already travelled through. This is the same transmittance
//    the sky march uses, so the disc, the sky and the light on the ground all redden together at sunset instead
//    of disagreeing.
vec3 SunRadiance()
{
    return SampleTransmittance(max(CameraAltitude, 1.0), SunDirection.z) * SunIlluminance;
}

vec3 LightEmission(uint lightIndex)
{
    if (IsSunLight(lightIndex)) return SunRadiance();
    return Materials[floatBitsToUint(Triangles[LightTriangle(lightIndex)].v0.w)].Emissive.xyz;
}

// A point on the sun's disc, expressed as somewhere far away along a jittered direction. Returning a POINT
//    rather than a direction is what lets the existing shadow-ray and reuse machinery work unchanged — and the
//    jitter across the 0.268° disc is exactly what produces a real penumbra instead of a hard edge.
vec3 SampleSunPoint(vec3 origin, float u1, float u2)
{
    // Uniform on the cap: the radius must go as √u or samples bunch at the centre and the penumbra is too sharp.
    const float CosMax = cos(kSunAngularRadius);
    const float CosTheta = 1.0 - u1 * (1.0 - CosMax);
    const float SinTheta = sqrt(max(0.0, 1.0 - CosTheta * CosTheta));
    const float Phi      = 6.28318530 * u2;

    // Any tangent frame will do; the disc is rotationally symmetric about the sun direction.
    vec3 Up      = abs(SunDirection.z) < 0.99 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 Tangent = normalize(cross(Up, SunDirection));
    vec3 Bitangent = cross(SunDirection, Tangent);

    const vec3 Direction = normalize(SunDirection * CosTheta
                                   + Tangent   * (SinTheta * cos(Phi))
                                   + Bitangent * (SinTheta * sin(Phi)));

    // Far enough to be effectively directional, near enough that the squared-distance falloff in PHatFull does
    //    not underflow to zero in a float.
    return origin + Direction * 1.0e7;
}

vec3 SampleLightPointFor(uint lightIndex, vec3 origin, float u1, float u2)
{
    if (IsSunLight(lightIndex)) return SampleSunPoint(origin, u1, u2);
    return SampleLightPoint(lightIndex, u1, u2);
}

// R6 row 3: light-triangle pick. Alias ON (F5, default): one Walker-alias probe — col uniform, accept on
//    Threshold else redirect to AliasSlot, source pdf = Probability[z] (power/total, SceneStructure.cpp).
//    Alias OFF: uniform over luminaires with p = 1/N — bit-for-bit the R0 estimator (identity proof).
//    Any positive pSource keeps RIS unbiased: the converged image is unchanged, only the noise differs.
uint PickLight(inout uint seed, out float pSource)
{
    const uint Slots = LightSlotCount();

    // A4. The sun occupies the slot one past the last triangle. It is picked with a fixed share rather than
    //    being folded into the alias table, because that table is built on the CPU from triangle power and the
    //    sun's contribution changes every frame as it moves — rebuilding the table per frame would cost more
    //    than the stratification is worth.
    //
    //    Half the samples when the sun is up. It is overwhelmingly the brightest light in an outdoor scene, so
    //    an even split with the luminaires is far closer to importance-proportional than any fixed small share,
    //    and RIS stays unbiased for ANY positive pSource — this choice only moves noise around.
    if (Slots > LightTriangleCount)
    {
        const float SunShare = 0.5;
        if (RandFloat(seed) < SunShare)
        {
            pSource = SunShare;
            return LightTriangleCount;
        }
        // Fall through to a triangle, whose probability is scaled by the remaining share.
        if ((FeatureFlags & kFeatureAliasPick) != 0u)
        {
            uint col = uint(RandFloat(seed) * float(LightTriangleCount));
            col = min(col, LightTriangleCount - 1u);
            uint z = RandFloat(seed) < Luminaires[col].Threshold ? col : Luminaires[col].AliasSlot;
            pSource = Luminaires[z].Probability * (1.0 - SunShare);
            return z;
        }
        pSource = (1.0 - SunShare) / float(LightTriangleCount);
        return min(uint(RandFloat(seed) * float(LightTriangleCount)), LightTriangleCount - 1u);
    }

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

// R6 row 2: full-BSDF target — f(wo,wi)·Le·cosθ/d². Any positive target keeps RIS unbiased (converged image
//    unchanged); the BSDF-weighted one is required for correct re-evaluation at foreign pixels on reuse.
float PHatFull(SurfaceFrame F, ShadingRecord m, ResolvedLayers L, vec3 wo, vec3 emit, vec3 toLight, float dist2)
{
    float dist = max(sqrt(dist2), 1e-9);
    vec3  lDir = toLight / dist;
    float cosT = max(0.0, dot(F.Geometric, lDir));
    if (cosT <= 0.0) return 0.0;
    vec3  wi = ToLocal(F, lDir);
    if (wi.z <= 0.0) return 0.0;
    return max(dot(EvaluateBsdf(m, L, wo, wi), emit), 0.0) * cosT / (dist2 + 0.001);
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
//                                            A2 — TABULATED ATMOSPHERE
//------------------------------------------------------------------------------------------------------------------------
// These live here rather than in AtmosphereScattering.slang because they sample the kernel's own descriptors,
//    and that header is deliberately free of bindings so the CPU proof can port it verbatim.

// The A3 march with its inner light march replaced by a texture fetch, plus the second-order term.
vec3 SkyRadianceTabulated(float CameraAltitude, vec3 ViewDirection, vec3 SunDirection, vec3 SunIlluminance, int ViewSteps)
{
    const vec3 Origin = vec3(0.0, 0.0, kPlanetRadius + max(CameraAltitude, 1.0));

    float Distance = RaySphereNearest(Origin, ViewDirection, kAtmosphereRadius);
    if (Distance < 0.0) return vec3(0.0);
    const float Ground = RaySphereNearest(Origin, ViewDirection, kPlanetRadius);
    if (Ground > 0.0) Distance = min(Distance, Ground);

    const float CosTheta      = dot(ViewDirection, SunDirection);
    const float PhaseRayleigh = RayleighPhase(CosTheta);
    const float PhaseMie      = MiePhase(CosTheta, kMieAsymmetry);

    const float Step = Distance / float(ViewSteps);
    vec3 Radiance      = vec3(0.0);
    vec3 Transmittance = vec3(1.0);

    const bool HitsGround = Ground > 0.0;   // A7c: the ray ends on the planet, not in space

    for (int i = 0; i < ViewSteps; ++i)
    {
        const vec3  Position = Origin + ViewDirection * (float(i) + 0.5) * Step;
        const float Radius   = length(Position);
        const float Altitude = Radius - kPlanetRadius;
        const vec3  Density  = AtmosphereDensity(Altitude);

        const vec3 Extinction = kRayleighScattering * Density.x
                              + vec3(MieExtinction()) * Density.y
                              + kOzoneAbsorption    * Density.z;
        const vec3 StepTransmittance = exp(-Extinction * Step);

        // The sun's zenith angle AT THIS SAMPLE, not at the camera. Using the camera's would make the whole ray
        //    share one lighting condition and flatten the terminator into a hard edge.
        const float CosSunZenith = dot(Position / Radius, SunDirection);
        const vec3  SunArriving  = SampleTransmittance(Altitude, CosSunZenith);

        const vec3 Phased    = kRayleighScattering * Density.x * PhaseRayleigh
                             + vec3(MieScattering()) * Density.y * PhaseMie;
        const vec3 Isotropic = (kRayleighScattering * Density.x + vec3(MieScattering()) * Density.y)
                             * SampleMultiScatter(Altitude, CosSunZenith);

        const vec3 Scattering = Phased * SunArriving + Isotropic;
        const vec3 Integrated = (Scattering - Scattering * StepTransmittance) / max(Extinction, vec3(1e-9));

        Radiance      += Transmittance * Integrated * SunIlluminance;
        Transmittance *= StepTransmittance;
    }

    // A7c — the ground. Before this, a ray passing below the horizon returned only the thin slice of air in
    //    front of it, which at eye height is nearly nothing: the world ended in a black void along the horizon
    //    line and the exposure meter read that void as most of the scene.
    if (HitsGround)
    {
        const vec3  Surface = Origin + ViewDirection * Distance;
        const vec3  Normal  = normalize(Surface);
        const float CosSunAtSurface = dot(Normal, SunDirection);

        // The sky's own light on the ground, which is what keeps the surface from switching off the moment the
        //    sun dips. The multiple-scattering table already holds exactly this quantity — the isotropic
        //    radiance arriving at an altitude for a sun angle — and a uniform hemisphere of radiance L delivers
        //    an irradiance of πL, so the π cancels against the one inside GroundReflection.
        const vec3 SkyIrradiance = SampleMultiScatter(0.0, CosSunAtSurface) * SunIlluminance * 3.14159265;

        Radiance += Transmittance * GroundReflection(Normal, SunDirection, SunIlluminance,
                                                     SampleTransmittance(0.0, CosSunAtSurface), SkyIrradiance);
    }
    return Radiance;
}

vec3 SunDiscTabulated(float CameraAltitude, vec3 ViewDirection, vec3 SunDirection, vec3 SunIlluminance)
{
    if (dot(ViewDirection, SunDirection) < cos(kSunAngularRadius)) return vec3(0.0);
    const float SolidAngle = 6.28318530 * (1.0 - cos(kSunAngularRadius));
    return SampleTransmittance(max(CameraAltitude, 1.0), SunDirection.z) * SunIlluminance / SolidAngle;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MAIN COMPUTE KERNEL
//------------------------------------------------------------------------------------------------------------------------

void main()
{
    uvec2 pixel = gl_GlobalInvocationID.xy;
    if (pixel.x >= ViewportWidth || pixel.y >= ViewportHeight) return;

    // A7b ⚠️ FIRST, before anything that can touch the atmosphere. Every Mie term in this invocation — the sky
    //    march, the sun disc, and the bounce ray's sky gather — reads this global, so it must be set before the
    //    first of them runs. Setting it later would leave whichever path ran first evaluating clear air.
    //    Clamped rather than trusted: a negative turbidity is negative extinction, i.e. an atmosphere that
    //    amplifies light, and the march would diverge into NaN rather than merely look wrong.
    gAerosolTurbidity = clamp(SkyTurbidity, 0.0, 16.0);

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
        // A3 — the ray-miss path. This used to return pure black; it now returns the radiance of the sky in the
        //    ray's direction, which is what turns the roof oculus from a hole into nothing into a window.
        //
        //    SunIlluminance of zero disables the sky entirely and restores the black background, so every image
        //    made before A3 can still be reproduced exactly. That is the identity switch for this phase.
        vec3 skyRadiance = vec3(0.0);
        if (SunIlluminance > 0.0)
        {
            // The primary ray direction has to be rebuilt here: the visibility raster only stores hits, so a
            //    miss carries no direction. Pixel centre, matching the raster's own convention.
            const vec2 uv  = (vec2(pixel) + 0.5) / vec2(float(ViewportWidth), float(ViewportHeight));
            const vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
            const vec3 rayDirection = normalize(CameraForward
                                              + CameraRight * (ndc.x * FieldOfViewTanHalf * AspectRatio)
                                              + CameraUp    * (ndc.y * FieldOfViewTanHalf));

            // A2 — the LUT path. The tables remove the inner light march, taking the per-pixel cost from
            //    ViewSteps × LightSteps down to ViewSteps, and add the multiple scattering that single
            //    scattering physically cannot produce. SkyLightSteps of 0 selects it; a non-zero value keeps
            //    the A3 march, so the two can still be compared directly on the same frame.
            if (SkyLightSteps == 0u)
                skyRadiance = SkyRadianceTabulated(CameraAltitude, rayDirection, SunDirection,
                                                   vec3(SunIlluminance), int(SkyViewSteps));
            else
                skyRadiance = SkyRadiance(CameraAltitude, rayDirection, SunDirection,
                                          vec3(SunIlluminance), int(SkyViewSteps), int(SkyLightSteps));

            skyRadiance += SunDiscTabulated(CameraAltitude, rayDirection, SunDirection, vec3(SunIlluminance));

            // A7 — the night sky. Both are ADDED to the sky radiance and then attenuated by the atmosphere
            //    between here and them, so they fade out at dawn exactly as the real ones do rather than being
            //    switched off by a threshold. That is the whole reason they are not a separate "night" branch.
            if ((FeatureFlags & kFeatureNightSky) != 0u)
            {
                const vec3 Attenuation = SampleTransmittance(max(CameraAltitude, 1.0), rayDirection.z);

                // Stars first: the moon draws over them, which is correct — it is nearer and opaque.
                skyRadiance += StarField(rayDirection, StarRotation, StarBrightness) * Attenuation;

                if (MoonIlluminance > 0.0)
                {
                    // A7c ⚠️ The true moon is 0.259° across, which at a 55° field of view is about ten pixels
                    //    — accurate, and far smaller than people expect, because the eye enlarges it near the
                    //    horizon and every photograph of it is taken with a long lens. The scale is an
                    //    admitted artistic control, so it is a multiplier on the real angle rather than a
                    //    replacement for it: 1 is always the truth, and the readout says so.
                    const float MoonRadius = kMoonAngularRadius * clamp(MoonAngularScale, 0.05, 40.0);

                    const float CosToMoon = dot(rayDirection, MoonDirection);
                    if (CosToMoon > cos(MoonRadius))
                    {
                        // Where on the disc this ray lands, in units of the moon's radius. Building a tangent
                        //    frame keeps the phase terminator oriented consistently as the moon moves, instead
                        //    of spinning with the camera.
                        const vec3 Up      = abs(MoonDirection.z) < 0.99 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
                        const vec3 Tangent = normalize(cross(Up, MoonDirection));
                        const vec3 Bitan   = cross(MoonDirection, Tangent);
                        const vec3 Offset  = rayDirection / max(CosToMoon, 1e-6) - MoonDirection;
                        const vec2 Disc    = vec2(dot(Offset, Tangent), dot(Offset, Bitan)) / MoonRadius;

                        const float Lit = MoonLitFraction(Disc, MoonPhase);
                        if (Lit > 0.0)
                        {
                            // Radiance, not illuminance: divided by the solid angle it subtends, so its
                            //    brightness does not change with render resolution.
                            //
                            // ⚠️ The TRUE solid angle, deliberately, not the scaled one. Dividing by the scaled
                            //    angle would conserve total flux, so an enlarged moon would dim as it grew and
                            //    a big one would read as a flat grey disc. Keeping the true angle keeps the
                            //    SURFACE BRIGHTNESS that of the real moon, which is what makes an enlarged one
                            //    still look like the moon.
                            const float SolidAngle = 6.28318530 * (1.0 - cos(kMoonAngularRadius));
                            skyRadiance += vec3(MoonIlluminance) * kMoonAlbedo * Lit
                                         / SolidAngle * Attenuation;
                        }
                    }
                }
            }
        }
        Resolve(pixel, skyRadiance);
        return;
    }

    GpuInstance primaryInstance = Instances[VisibilityInstance(visibility)];
    GpuMaterial mat = Materials[primaryInstance.MaterialIndex];

    vec3  hitPos     = surface.xyz;
    vec3  toCamera   = CameraOrigin - hitPos;
    float primaryT   = length(toCamera);
    vec3  viewDir    = toCamera / max(primaryT, 1e-6);
    float coneWidth  = primaryT * PixelSpreadAngle();             // ray-cone footprint at the primary hit [m]

    SurfaceFrame  frame;
    ShadingRecord shading = ResolveMaterial(mat, primaryInstance, VisibilityPrimitive(visibility), hitPos, viewDir, coneWidth, frame);

    // If we directly hit an emitter — return its radiance (slab emission × texture; the header's radiance drives the luminaire table)
    if (dot(frame.Emission, frame.Emission) > 0.0)
    {
        Resolve(pixel, frame.Emission);
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

    if (LightSlotCount() > 0u)   // A4: a scene lit only by the sun has no emissive triangle at all
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
            float  pSource;
            uint   li          = PickLight(seed, pSource);   // R6 row 3: alias (F5) or uniform (identity proof)
            float  u1          = RandFloat(seed);
            float  u2          = RandFloat(seed);
            vec3   lp          = SampleLightPointFor(li, hitPos, u1, u2);
            vec3   toLight     = lp - hitPos;

            // Get light material
            vec3 lEmissive = LightEmission(li);

            // Target function: full-BSDF unshadowed direct contribution (R6 row 2)
            float pHat = PHatFull(frame, shading, layers, woLocal, lEmissive, toLight, dot(toLight, toLight));
            float w    = pHat / pSource;   // p̂ / p_source — pSource is 1/N uniform or the alias Probability[z]

            ResampleCandidate(res, lp, li, vec2(u1, u2), w, seed);
        }

        // Compute unbiased weight W (R6: the chosen light comes from the reservoir — no nearest-emissive search)
        if (res.WeightSum > 0.0 && res.SampleCount > 0u)
        {
            vec3  toSel    = res.SelectedPoint - hitPos;

            vec3 selEmissive = LightEmission(res.SelectedLight);

            float pHatSel = PHatFull(frame, shading, layers, woLocal, selEmissive, toSel, dot(toSel, toSel));
            if (pHatSel > 0.0)
                res.UnbiasedWeight = res.WeightSum / (float(res.SampleCount) * pHatSel);

            // Extra same-pixel candidates (R6 row 3: renamed from SpatialPassCount): ExtraCandidateCount further light samples
            //    from THIS pixel, resampled with the same target function so the reservoir stays a valid RIS estimate.
            for (uint sp = 0u; sp < ExtraCandidateCount; ++sp)
            {
                float  npSource;
                uint   nli    = PickLight(seed, npSource);   // R6 row 3: same alias/uniform gate as the initial loop
                float  nu1    = RandFloat(seed);
                float  nu2    = RandFloat(seed);
                vec3   nlp    = SampleLightPointFor(nli, hitPos, nu1, nu2);
                vec3   nToL   = nlp - hitPos;
                vec3   nEmit  = LightEmission(nli);
                float  nPHat  = PHatFull(frame, shading, layers, woLocal, nEmit, nToL, dot(nToL, nToL));
                float  nW     = nPHat / npSource;   // p̂ / p_source, same as the initial loop
                ResampleCandidate(res, nlp, nli, vec2(nu1, nu2), nW, seed);
            }
            // Recompute W for the final selection (the selected point may have changed).
            vec3  emit2  = vec3(0.0);
            {
                vec3  toSel2   = res.SelectedPoint - hitPos;
                emit2          = LightEmission(res.SelectedLight);
                float pHat2 = PHatFull(frame, shading, layers, woLocal, emit2, toSel2, dot(toSel2, toSel2));
                res.UnbiasedWeight = pHat2 > 0.0 ? res.WeightSum / (float(res.SampleCount) * pHat2) : 0.0;
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
                        vec3 prevEmit = LightEmission(prev.Counts.y);
                        vec3 prevToL  = prev.Sample.xyz - hitPos;
                        float pPrev = PHatFull(frame, shading, layers, woLocal, prevEmit, prevToL, dot(prevToL, prevToL));
                        vec3  curToL  = res.SelectedPoint - hitPos;
                        float pCur  = PHatFull(frame, shading, layers, woLocal, emit2, curToL, dot(curToL, curToL));
                        float wCur  = pCur * res.UnbiasedWeight * float(res.SampleCount);
                        float wPrev = pPrev * prev.UvDepth.z * float(mPrevCapped);
                        float total = wCur + wPrev;
                        if (total > 0.0 && RandFloat(seed) * total <= wPrev)
                        {
                            res.SelectedPoint = prev.Sample.xyz;
                            res.SelectedLight = prev.Counts.y;
                            res.SelectedUv    = prev.UvDepth.xy;
                        }
                        res.SampleCount = res.SampleCount + mPrevCapped;
                        res.WeightSum   = total;
                        resAge = prev.Counts.w + 1u;
                        vec3  finEmit = LightEmission(res.SelectedLight);
                        vec3  finToL  = res.SelectedPoint - hitPos;
                        float pFin = PHatFull(frame, shading, layers, woLocal, finEmit, finToL, dot(finToL, finToL));
                        res.UnbiasedWeight = pFin > 0.0 ? total / (float(res.SampleCount) * pFin) : 0.0;
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

                for (uint tap = 0u; tap < kSpatialTaps; ++tap)
                {
                    // The cross keeps its 90° spacing — it is still four taps of equal reach, just aimed
                    //    differently each frame, so no direction is systematically favoured over time.
                    float  theta  = angle + float(tap) * 1.57079633;
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
                    vec3 neighEmit = LightEmission(neigh.Counts.y);
                    vec3 neighToL  = neigh.Sample.xyz - hitPos;
                    float pNeigh = PHatFull(frame, shading, layers, woLocal, neighEmit, neighToL, dot(neighToL, neighToL));
                    vec3  selfToL  = res.SelectedPoint - hitPos;
                    vec3  selfEmit = LightEmission(res.SelectedLight);
                    float pSelf = PHatFull(frame, shading, layers, woLocal, selfEmit, selfToL, dot(selfToL, selfToL));
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
                    }
                    res.SampleCount = res.SampleCount + mNeighCapped;
                    res.WeightSum   = nTotal;
                    resAge = takeAge;
                    vec3  mEmit = LightEmission(res.SelectedLight);
                    vec3  mToL  = res.SelectedPoint - hitPos;
                    float pM = PHatFull(frame, shading, layers, woLocal, mEmit, mToL, dot(mToL, mToL));
                    res.UnbiasedWeight = pM > 0.0 ? nTotal / (float(res.SampleCount) * pM) : 0.0;
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

            // Shade (R6: the stored selection carries its light — no search)
            if (res.Visible == 1u)
            {
                vec3 shadeDir = normalize(res.SelectedPoint - hitPos);
                float shadeCos = max(0.0, dot(hitNormal, shadeDir));

                vec3 chosenEmit = LightEmission(res.SelectedLight);

                float dist2 = dot(res.SelectedPoint - hitPos, res.SelectedPoint - hitPos);
                vec3  wiLocal = ToLocal(frame, shadeDir);
                vec3  f = wiLocal.z > 0.0 ? EvaluateBsdf(shading, layers, woLocal, wiLocal) : vec3(0.0);   // R4b: full lobe stack
                accumulatedRadiance += f * chosenEmit * shadeCos * res.UnbiasedWeight / (dist2 + 0.01);
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

            // A5 — the sky as an environment light. A bounce ray that escapes the scene used to contribute
            //    nothing, which is why the room stayed lit only by what it could see directly: a wall facing the
            //    oculus received no skylight at all, however bright the sky outside was.
            //
            //    This is the whole of the environment term. No separate environment sampler and no second alias
            //    table: the BSDF sample already chose a direction with the right distribution, so the escaped ray
            //    IS the environment sample, and multiplying by throughput is the correct estimator. An explicit
            //    environment sampler would add a second estimator to combine by MIS for no gain at this depth.
            if (!bounceHit.valid && dot(bounceDir, hitNormal) > 0.0 && SunIlluminance > 0.0
                && (FeatureFlags & kFeatureSkyLighting) != 0u)
            {
                accumulatedRadiance += throughput * SkyRadianceTabulated(
                    CameraAltitude, bounceDir, SunDirection, vec3(SunIlluminance), int(max(SkyViewSteps / 2u, 8u)));
            }
            else if (bounceHit.valid && dot(bounceDir, hitNormal) > 0.0)
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
                else if (LightSlotCount() > 0u)
                {
                    vec3  bHitPos    = bounceHit.pos;
                    vec3  bHitNormal = bFrame.Geometric;
                    vec3  bWo        = ToLocal(bFrame, -bounceDir);
                    if (bWo.z <= 0.0) bWo = vec3(bWo.xy, 1e-3);
                    ResolvedLayers bLayers = ResolveLayers(bShading, bWo);

                    // A4: the bounce samples the sun too, uniformly over all slots. The secondary bounce is a
                    //    single unweighted sample rather than a reservoir, so a uniform pick is the correct
                    //    estimator here — and outdoors the sun is what actually lights the second bounce.
                    const uint bSlots = LightSlotCount();
                    uint  bLi  = min(uint(RandFloat(seed) * float(bSlots)), bSlots - 1u);
                    vec3  bLp  = SampleLightPointFor(bLi, bHitPos, RandFloat(seed), RandFloat(seed));
                    vec3  bToL = bLp - bHitPos;
                    float bDist = length(bToL);
                    vec3  bLDir = bToL / bDist;
                    float bCos  = max(0.0, dot(bHitNormal, bLDir));
                    vec3  bWi   = ToLocal(bFrame, bLDir);

                    vec3 bLEmit = LightEmission(bLi);

                    if (bCos > 0.0 && bWi.z > 0.0 && !TraceShadow(bHitPos + bHitNormal * kEps, bLp))
                    {
                        float bDist2 = bDist * bDist;
                        vec3  bF = EvaluateBsdf(bShading, bLayers, bWo, bWi);
                        // ⚠️ Divide by the SLOT count, not the triangle count: the pick above chose from
                        //    bSlots, and dividing by a different number than you sampled from is a bias that
                        //    scales the whole second bounce.
                        accumulatedRadiance += throughput * bF * bLEmit * bCos / ((bDist2 + 0.01) * float(bSlots));
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

    // R7a: the primary hit's geometric normal and camera distance are what the next frame validates its
    //    back-projection against — the same pair R6 row 2 uses to validate reprojected reservoirs.
    ResolveSurface(pixel, accumulatedRadiance, hitNormal, primaryT);
}
