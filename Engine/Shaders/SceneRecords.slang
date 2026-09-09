//============================================================================================================================================
// 📦 Engine/Shaders/SceneRecords.slang — Resident-scene record layouts shared by the R2 passes (include only)
//============================================================================================================================================
// Mirrors Engine/GeometricRaster/SceneStructure.h byte for byte (std430) and the FrameConstants block written by
//    VisibilityExchange.cpp (std140). Change both sides or neither.
//
// Coordinates: world is right-handed Z-up (CLAUDE.md §7). ViewClip already contains the single Vulkan Y flip and the
//    reverse-Z infinite projection (near → depth 1, infinity → depth 0); see GeometricRaster/ClipProjection.h.

#ifndef FRONTIER_SCENE_RECORDS
#define FRONTIER_SCENE_RECORDS

//------------------------------------------------------------------------------------------------------------------------
//                                                     FRAME CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

layout(std140, set = 0, binding = 0) uniform FrameConstants
{
    mat4  ViewClip;             // world → clip, current frame, sub-pixel jitter folded in
    mat4  PreviousViewClip;     // world → clip, previous frame, unjittered (motion vectors)
    vec4  FrustumPlanes[5];     // world-space, normalised: left, right, bottom, top, near (infinite far → no far plane)
    vec4  CameraOrigin;         // xyz = eye, w = near distance
    vec4  CameraForward;        // xyz = forward, w = tan(FoV/2)
    vec4  CameraRight;          // xyz = right,   w = aspect
    vec4  CameraUp;             // xyz = up,      w = unused
    vec4  Jitter;               // xy = clip-space jitter added as clip.xy += Jitter.xy * clip.w; zw = pixel-space jitter [0,1)
    uvec4 Extent;               // x = render width, y = render height, z = HiZ mip count, w = cluster count
    uvec4 Control;              // x = cull phase (1 | 2), y = frame index, z = debug view, w = flags (bit 0: HiZ test on)
    vec4  Projection;           // x = P00, y = |P11| (sphere → screen bounds), zw = unused
};

const uint kControlHiZEnabled = 1u;

//------------------------------------------------------------------------------------------------------------------------
//                                                     SCENE RECORDS
//------------------------------------------------------------------------------------------------------------------------

struct VertexRecord            // 64 B — GeometryStructure.h
{
    vec4 Position;             // xyz, w = pad
    vec4 Normal;               // xyz, w = pad
    vec4 Tangent;              // xyz, w = sign
    vec4 Texcoord;             // xy = uv, zw = pad
};

struct InstanceRecord          // 160 B — SceneStructure.h
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

struct ClusterRecord           // 48 B — SceneStructure.h
{
    vec4 Sphere;               // xyz = object-space centre, w = radius
    vec4 Cone;                 // xyz = object-space axis,   w = cutoff (1.0 = never culled)
    uint InstanceIndex;
    uint FirstIndex;
    uint TriangleCount;
    uint FirstPrimitive;
};

struct MaterialRecord          // 64 B — ContentInterchange/MaterialIndex.h (R4a header; Tier A fast path)
{
    vec4  Albedo;              // xyz = flattened base colour (linear Rec.709), w = specular roughness
    vec4  Emissive;            // xyz = emission_luminance × emission_color [nit], w = metalness
    uvec4 Slabs;               // x = SlabOffset, y = SlabCount, z = Flags (kMaterialFlag*), w = Complexity
    uvec2 Textures;            // x = base colour slot, y = normal slot (0xFFFFFFFF = none)
    float AlphaCutoff;
    float NormalScale;
};

struct MaterialSlabRecord      // 288 B — ContentInterchange/MaterialIndex.h (OpenPBR §5 order + slate_ + 16 texture slots)
{
    vec4 Base;                 // base_weight, base_color.rgb
    vec4 BaseSpecular;         // base_metalness, base_diffuse_roughness, specular_weight, specular_color.r
    vec4 Specular;             // specular_color.gb, specular_roughness, specular_roughness_anisotropy
    vec4 Transmission;         // specular_ior, transmission_weight, transmission_color.rg
    vec4 Transmission2;        // transmission_color.b, transmission_depth, transmission_scatter.rg
    vec4 Transmission3;        // transmission_scatter.b, transmission_scatter_anisotropy, dispersion_scale, dispersion_abbe_number
    vec4 Subsurface;           // subsurface_weight, subsurface_color.rgb
    vec4 Subsurface2;          // subsurface_radius, subsurface_radius_scale.rgb
    vec4 Coat;                 // subsurface_scatter_anisotropy, coat_weight, coat_color.rg
    vec4 Coat2;                // coat_color.b, coat_roughness, coat_roughness_anisotropy, coat_ior
    vec4 Fuzz;                 // coat_darkening, fuzz_weight, fuzz_color.rg
    vec4 Emission;             // fuzz_color.b, fuzz_roughness, emission_luminance, emission_color.r
    vec4 ThinFilm;             // emission_color.gb, thin_film_weight, thin_film_thickness
    vec4 Geometry;             // thin_film_ior, geometry_opacity, slate_haziness_weight, slate_haziness_roughness
    vec4 Slate;                // slate_glint_density, slate_glint_uv_scale, uintBits(SlabFlags), uintBits(MaskTexturePacked)
    uvec4 TextureSlots0;       // 8 × uint16: base_color, metalness | specular_roughness, specular_color | geometry_normal, geometry_coat_normal | emission, geometry_opacity
    uvec4 TextureSlots1;       // 8 × uint16: transmission, subsurface | coat, fuzz | thin_film, anisotropy | occlusion, mask
    uvec4 Runtime;             // x = TextureUvSets (16 × 2 bits), y = floatBits(NormalScale), z = floatBits(OcclusionStrength), w = floatBits(MixWeight)
};

const uint kMaterialFlagDoubleSided  = 1u;
const uint kMaterialFlagAlphaMask    = 2u;
const uint kMaterialFlagAlphaBlend   = 4u;
const uint kMaterialFlagUnlit        = 8u;
const uint kMaterialFlagThinWalled   = 16u;
const uint kMaterialFlagEmissive     = 32u;
const uint kMaterialTextureNone      = 0xFFFFFFFFu;

// Texture slot of channel C (0-15) of a slab, 0xFFFF = none.
uint MaterialSlabTexture(MaterialSlabRecord S, uint C)
{
    uint Word = C < 8u ? S.TextureSlots0[C / 2u] : S.TextureSlots1[(C - 8u) / 2u];
    return (Word >> ((C & 1u) * 16u)) & 0xFFFFu;
}

struct DrawCommand             // VkDrawIndexedIndirectCommand
{
    uint IndexCount;
    uint InstanceCount;
    uint FirstIndex;
    int  VertexOffset;
    uint FirstInstance;        // = cluster index (read back through gl_InstanceIndex)
};

// Counters[0] = draw count (consumed by vkCmdDrawIndexedIndirectCount); the rest are telemetry.
const uint kCounterDrawCount     = 0u;
const uint kCounterClusterTotal  = 1u;
const uint kCounterFrustumPassed = 2u;
const uint kCounterConePassed    = 3u;
const uint kCounterHiZPassed     = 4u;
const uint kCounterTriangleDrawn = 5u;
const uint kCounterPhaseOneDrawn = 6u;
const uint kCounterDrawCountPhaseTwo = 7u;   // draw count of the phase-2 list (vkCmdDrawIndexedIndirectCount reads it at byte 28)
const uint kCounterCount         = 8u;

const uint kInstanceFlagDoubleSided = 1u;
const uint kInstanceFlagEmissive    = 2u;

//------------------------------------------------------------------------------------------------------------------------
//                                                  VISIBILITY IDENTIFIER
//------------------------------------------------------------------------------------------------------------------------
// GeometricRaster/VisibilityProjection.h: 18-bit instance token << 14 | 14-bit primitive token; 0xFFFFFFFF = nothing.

const uint kVisibilityInvalid = 0xFFFFFFFFu;

uint PackVisibility(uint InstanceToken, uint PrimitiveToken)
{
    return ((InstanceToken & 0x3FFFFu) << 14) | (PrimitiveToken & 0x3FFFu);
}

uint VisibilityInstance(uint Packed)  { return (Packed >> 14) & 0x3FFFFu; }
uint VisibilityPrimitive(uint Packed) { return Packed & 0x3FFFu; }

#endif // FRONTIER_SCENE_RECORDS
