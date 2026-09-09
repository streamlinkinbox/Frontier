//============================================================================================================================================
//                                                     MATERIALDESCRIPTOR.H
//============================================================================================================================================
// 🧩 Authoring record of one material: a list of slabs (each a complete OpenPBR Surface v1.1.1 parameter set plus the
//    `slate_` extensions) and a post-order list of operations that layer or mix them. This is what codecs read and
//    write; MaterialIndex flattens it to the GPU records at the configured slab limit.
//
// Units / conventions: metres, nits, linear Rec.709 primaries (MaterialSystemResearch-2026.md §7 decision 4). Parameter
//    names follow OpenPBR §5 verbatim (PascalCase) so the interchange mapping is one-to-one; defaults are the spec's.
//    Nothing in this record caps the slab count — the cap is `[render] slab_limit`, applied by MaterialIndex.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier {

static constexpr uint32_t kMaterialTextureNone = 0xFFFFFFFFu;

//------------------------------------------------------------------------------------------------------------------------
//                                                    TEXTURE REFERENCE
//------------------------------------------------------------------------------------------------------------------------
// One texturable channel: which resident texture (TextureIndex slot), which UV set, the KHR_texture_transform affine
//    and which component(s) feed the channel (glTF packs metalness in B and roughness in G of one image).

enum class TextureChannelSelection : uint8_t { Rgb = 0, R = 1, G = 2, B = 3, A = 4 };

struct TextureReference
{
    uint32_t                Texture   = kMaterialTextureNone;   // [idx] TextureIndex slot; kMaterialTextureNone = constant only
    uint8_t                 UvSet     = 0u;                     // [idx] TEXCOORD_n
    TextureChannelSelection Channel   = TextureChannelSelection::Rgb;
    float                   OffsetU   = 0.0f, OffsetV = 0.0f;   // [uv]  KHR_texture_transform
    float                   ScaleU    = 1.0f, ScaleV  = 1.0f;   // [-]
    float                   Rotation  = 0.0f;                   // [rad]
    float                   Scalar    = 1.0f;                   // [-]   normal-map scale / occlusion strength

    [[nodiscard]] bool IsBound() const noexcept { return Texture != kMaterialTextureNone; }
    [[nodiscard]] bool operator==(const TextureReference&) const noexcept = default;
};

// Texturable channels of one slab, in the order MaterialSlabRecord packs them (16 × uint16).
enum class MaterialTextureChannel : uint8_t
{
    BaseColor = 0, Metalness, SpecularRoughness, SpecularColor, GeometryNormal, GeometryCoatNormal, Emission, GeometryOpacity,
    Transmission, Subsurface, Coat, Fuzz, ThinFilm, Anisotropy, Occlusion, Mask,
    Count
};
static constexpr uint32_t kMaterialTextureChannelCount = static_cast<uint32_t>(MaterialTextureChannel::Count);

//------------------------------------------------------------------------------------------------------------------------
//                                                   MATERIAL SLAB DESCRIPTOR
//------------------------------------------------------------------------------------------------------------------------
// OpenPBR Surface §5 parameter reference, spec order, spec defaults. Colours are float[3] linear Rec.709.

struct MaterialSlabDescriptor
{
    // Base
    float BaseWeight                     = 1.0f;
    float BaseColor[3]                   = { 0.8f, 0.8f, 0.8f };
    float BaseMetalness                  = 0.0f;
    float BaseDiffuseRoughness           = 0.0f;                  // EON roughness
    // Specular
    float SpecularWeight                 = 1.0f;
    float SpecularColor[3]               = { 1.0f, 1.0f, 1.0f };  // F82-tint for metals, F0 scale for dielectrics
    float SpecularRoughness              = 0.3f;
    float SpecularRoughnessAnisotropy    = 0.0f;
    float SpecularIor                    = 1.5f;
    // Transmission
    float TransmissionWeight             = 0.0f;
    float TransmissionColor[3]           = { 1.0f, 1.0f, 1.0f };
    float TransmissionDepth              = 0.0f;                  // [m]
    float TransmissionScatter[3]         = { 0.0f, 0.0f, 0.0f };
    float TransmissionScatterAnisotropy  = 0.0f;
    float TransmissionDispersionScale    = 0.0f;
    float TransmissionDispersionAbbeNumber = 20.0f;
    // Subsurface
    float SubsurfaceWeight               = 0.0f;
    float SubsurfaceColor[3]             = { 0.8f, 0.8f, 0.8f };
    float SubsurfaceRadius               = 1.0f;                  // [m]
    float SubsurfaceRadiusScale[3]       = { 1.0f, 0.5f, 0.25f };
    float SubsurfaceScatterAnisotropy    = 0.0f;
    // Coat
    float CoatWeight                     = 0.0f;
    float CoatColor[3]                   = { 1.0f, 1.0f, 1.0f };
    float CoatRoughness                  = 0.0f;
    float CoatRoughnessAnisotropy        = 0.0f;
    float CoatIor                        = 1.6f;
    float CoatDarkening                  = 1.0f;
    // Fuzz
    float FuzzWeight                     = 0.0f;
    float FuzzColor[3]                   = { 1.0f, 1.0f, 1.0f };
    float FuzzRoughness                  = 0.5f;
    // Emission
    float EmissionLuminance              = 0.0f;                  // [nit]
    float EmissionColor[3]               = { 1.0f, 1.0f, 1.0f };
    // Thin film
    float ThinFilmWeight                 = 0.0f;
    float ThinFilmThickness              = 0.5f;                  // [µm]
    float ThinFilmIor                    = 1.4f;
    // Geometry
    float GeometryOpacity                = 1.0f;
    //    geometry_normal / geometry_tangent / geometry_coat_normal / geometry_coat_tangent are the texture channels below
    //    (constant vectors are the mesh frame by definition).

    // `slate_` extensions (MaterialSystemResearch-2026.md §7 decision 2) — serialised as glTF extras.slate_*
    float SlateHazinessWeight            = 0.0f;                  // second specular lobe weight
    float SlateHazinessRoughness         = 0.6f;                  // second specular lobe roughness
    float SlateGlintDensity              = 0.0f;                  // Deliot–Belcour flake density (0 = off)
    float SlateGlintUvScale              = 1.0f;
    // ── end of float prefix (58 floats; MaterialSlabRecord mirrors it byte for byte) ──
    bool  GeometryThinWalled             = false;

    TextureReference Textures[kMaterialTextureChannelCount];

    [[nodiscard]] TextureReference&       Texture(MaterialTextureChannel C)       noexcept { return Textures[static_cast<uint32_t>(C)]; }
    [[nodiscard]] const TextureReference& Texture(MaterialTextureChannel C) const noexcept { return Textures[static_cast<uint32_t>(C)]; }
    [[nodiscard]] bool operator==(const MaterialSlabDescriptor&) const noexcept = default;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    MATERIAL OPERATION
//------------------------------------------------------------------------------------------------------------------------
// Post-order expression over slabs. Operands index either a slab (bit 31 clear) or an earlier operation result
//    (bit 31 set). A descriptor with one slab and no operations is that slab; with N slabs and no operations the
//    slabs are an implicit VerticalLayer chain, Slabs[0] on top.

enum class MaterialOperationCategory : uint8_t
{
    VerticalLayer = 0,   // Left over Right (Left = top)
    HorizontalMix = 1,   // lerp(Right, Left, Weight | Mask texture)
    Weight        = 2,   // Left × Weight (base_weight scaling)
    Coverage      = 3    // Left with geometry_opacity × Weight
};

static constexpr uint32_t kMaterialOperandResultBit = 0x80000000u;

struct MaterialOperation
{
    MaterialOperationCategory Category = MaterialOperationCategory::VerticalLayer;
    uint32_t                  Left     = 0u;
    uint32_t                  Right    = 0u;                      // unused by Weight / Coverage
    float                     Weight   = 1.0f;                    // constant mix factor / weight
    TextureReference          Mask;                               // HorizontalMix: per-texel factor when bound

    [[nodiscard]] bool operator==(const MaterialOperation&) const noexcept = default;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    MATERIAL DESCRIPTOR
//------------------------------------------------------------------------------------------------------------------------

enum MaterialFlag : uint32_t
{
    MaterialFlagDoubleSided     = 1u << 0,
    MaterialFlagAlphaMask       = 1u << 1,   // geometry_opacity thresholded at AlphaCutoff
    MaterialFlagAlphaTranslucent= 1u << 2,   // geometry_opacity used as coverage
    MaterialFlagUnlit           = 1u << 3,   // KHR_materials_unlit: base colour is the radiance
    MaterialFlagThinWalled      = 1u << 4,
    MaterialFlagEmissive        = 1u << 5    // derived: any slab emission_luminance > 0
};

struct MaterialDescriptor
{
    std::string                         Name;
    std::vector<MaterialSlabDescriptor> Slabs;         // never empty after a codec; Slabs[0] is the top when implicit
    std::vector<MaterialOperation>      Operations;    // post-order; empty = implicit vertical chain
    uint32_t                            Flags       = 0u;
    float                               AlphaCutoff = 0.5f;

    [[nodiscard]] bool operator==(const MaterialDescriptor&) const noexcept = default;
};

} // namespace Frontier
