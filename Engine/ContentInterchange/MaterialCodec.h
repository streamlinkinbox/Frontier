//============================================================================================================================================
//                                                        MATERIALCODEC.H
//============================================================================================================================================
// 🧩 Interchange material ⇄ MaterialDescriptor (CLAUDE.md role 2). glTF 2.0 core + KHR_materials_{emissive_strength, ior,
//    specular, clearcoat, sheen, transmission, volume, dispersion, iridescence, anisotropy, diffuse_transmission, unlit}
//    + KHR_texture_transform, and the Slate extras (`extras.slate_*` scalars, `extras.slate_slabs` full slab graph).
//    FBX (ufbx PBR maps) and OBJ (.mtl) mappings live here too so every codec produces the same descriptor.
//    Mapping table: MaterialSystemResearch-2026.md §5.
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
