//============================================================================================================================================
//                                                          FBXCODEC.H
//============================================================================================================================================
// 🧩 FBX → SceneStructure via ufbx (ContentInterchange). Same output shape as SceneCodec::Decode: one GeometryStructure
//    per mesh × material part (object space), one instance per node × part, MaterialDescriptor per ufbx_material
//    (MaterialCodec::DecodeFbx: Simple / Single class, Phong shininess → GGX roughness), textures registered by
//    absolute filename or embedded content, PlacementRecord per node (whole tree), CameraRecord per camera,
//    PunctualLuminaireRecord per light.
//
// Axis / units: ufbx is asked for right-handed Z-up metres (ufbx_load_opts.target_axes = ufbx_axes_right_handed_z_up,
//    target_unit_meters = 1, space_conversion = MODIFY_GEOMETRY) so no engine-side swap is needed — node_to_world
//    is already CLAUDE.md §7 world space. Faces are triangulated with ufbx_triangulate_face (n-gons welcome).

#pragma once

#include "SceneCodec.h"

namespace Frontier {

class FbxCodec
{
public:
    // Fills `Out` (cleared first) from a .fbx (binary or ASCII). Returns false and sets `Error` on failure; a
    //    non-empty `Error` on success is a warning line.
    [[nodiscard]] static bool Decode(const std::string& Path, SceneStructure& Out, TextureIndex* Textures, const SceneDecodeConfiguration& Config, std::string* Error) noexcept;
};

} // namespace Frontier
