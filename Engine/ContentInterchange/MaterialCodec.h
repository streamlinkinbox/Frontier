//============================================================================================================================================
//                                                        MATERIALCODEC.H
//============================================================================================================================================
// 🧩 Interchange material ⇄ MaterialDescriptor (CLAUDE.md role 2). glTF 2.0 core + KHR_materials_{emissive_strength, ior,
//    specular, clearcoat, sheen, transmission, volume, dispersion, iridescence, anisotropy, diffuse_transmission, unlit}
//    + KHR_texture_transform, and the Slate extras (`extras.slate_*` scalars, `extras.slate_slabs` full slab graph).
//    FBX (ufbx PBR maps) and OBJ (.mtl) mappings live here too so every codec produces the same descriptor.
//    Mapping table: MaterialSystemResearch-2026.md §5 (pending in-tree; interim: MaterialCodecProof.cpp header).
//
//    M6 DROP INVENTORY — sources with no slab carrier, decoded past and never re-encoded (each locked by a proof
//    assert in MaterialCodecProof.cpp, so a future channel addition flips the assert instead of silently changing art):
//      glTF KHR_materials_specular.specularTexture (factor .A) · KHR_materials_volume.thickness_texture (G/B — the
//        thickness_factor scalar survives as MaterialDescriptor::VolumeThickness) ·
//        KHR_materials_iridescence.iridescence_thickness_texture · KHR_materials_clearcoat.clearcoat_roughness_texture ·
//        KHR_materials_sheen.sheen_roughness_texture · KHR_materials_diffuse_transmission.diffuse_transmission_color_texture.
//      FBX: transmission_extra_roughness · subsurface_tint_color/type · matte_factor/color · indirect_diffuse/specular ·
//        coat_rotation (specular_rotation IS mapped: Standard Surface turns × 2π → SlateAnisotropyRotation [rad]).
//      OBJ: MapNs (shininess maps are white = smooth — binding one to roughness would invert it); MapD rides the opacity
//        slot as BLEND coverage. Transparency (d, Tr folded by fast_obj) maps to transmission_weight, never to
//        opacity/coverage (M6 plan correction — .mtl has no cutout concept).
//
// Texture slots: codecs pass a resolver that turns a glTF texture (image URI / sampler) into a TextureIndex slot, so
//    this file never touches image files.

#pragma once

#include "MaterialDescriptor.h"
#include <functional>
#include <string>

struct cgltf_material;
struct cgltf_texture_view;
struct ufbx_material;

namespace Frontier {

// Returns the TextureIndex slot for a glTF texture (by index into cgltf_data::textures) or kMaterialTextureNone.
//    `Linear` tells the resolver whether the image holds data (normals, roughness) or colour (sRGB-encoded).
using GltfTextureResolver = std::function<uint32_t(const cgltf_texture_view& View, bool Linear)>;
// Same for ufbx textures (pointer identity) and OBJ texture paths.
using FbxTextureResolver  = std::function<uint32_t(const void* UfbxTexture, bool Linear)>;
using PathTextureResolver = std::function<uint32_t(const std::string& Path, bool Linear)>;

struct MaterialDecodeConfiguration
{
    float EmissiveRadiance = 1.0f;   // [-] multiplier on emission (matches SceneDecodeConfiguration::EmissiveRadiance)
};

class MaterialCodec
{
public:
    // glTF → descriptor. Null material = the fallback (OpenPBR defaults, base 0.8, roughness 0.5 as R2 did).
    [[nodiscard]] static MaterialDescriptor DecodeGltf(const cgltf_material* Material, const MaterialDecodeConfiguration& Config, const GltfTextureResolver& Resolve) noexcept;

    // descriptor → one glTF material JSON object (no trailing comma, no surrounding array). Single-slab materials
    //    emit plain glTF + extensions; multi-slab or slate_ parameters add `extras`. `ExtensionsUsed` receives the
    //    extension names the object needs (caller merges them into `extensionsUsed`). Texture slots are emitted
    //    through `TextureIndexOf` (returns glTF texture index or -1).
    [[nodiscard]] static std::string EncodeGltf(const MaterialDescriptor& Descriptor, std::vector<std::string>& ExtensionsUsed,
                                                const std::function<int(uint32_t TextureSlot)>& TextureIndexOf) noexcept;

    // Parse the `extras.slate_slabs` JSON (as written by EncodeGltf) back into Slabs/Operations. Returns false when
    //    the extras carry no Slate block (the descriptor is left as decoded from core glTF).
    [[nodiscard]] static bool DecodeSlateExtras(const char* ExtrasJson, MaterialDescriptor& InOut) noexcept;

    // FBX (ufbx) → descriptor, Simple / Single class.
    [[nodiscard]] static MaterialDescriptor DecodeFbx(const ufbx_material* Material, const MaterialDecodeConfiguration& Config, const FbxTextureResolver& Resolve) noexcept;

    // Wavefront .mtl → descriptor (Kd/Ks/Ns/Ni/d/Ke/map_*), Simple class.
    struct ObjMaterialSource
    {
        std::string Name;
        float Kd[3] = { 0.8f, 0.8f, 0.8f }, Ks[3] = { 0.0f, 0.0f, 0.0f }, Ke[3] = { 0.0f, 0.0f, 0.0f }, Tf[3] = { 1.0f, 1.0f, 1.0f };
        float Ns = 0.0f, Ni = 1.5f, d = 1.0f;
        int   Illum = 2;
        std::string MapKd, MapKs, MapKe, MapD, MapBump, MapNs;
    };
    [[nodiscard]] static MaterialDescriptor DecodeObj(const ObjMaterialSource& Source, const MaterialDecodeConfiguration& Config, const PathTextureResolver& Resolve) noexcept;

    // Phong shininess → GGX roughness (Blinn–Phong exponent lobe-width match), shared by FBX legacy and OBJ.
    [[nodiscard]] static float RoughnessFromShininess(float Shininess) noexcept;
};

} // namespace Frontier
