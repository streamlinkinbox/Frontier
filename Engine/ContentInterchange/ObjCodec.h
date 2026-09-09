//============================================================================================================================================
//                                                          OBJCODEC.H
//============================================================================================================================================
// 🧩 Wavefront OBJ (+ .mtl) → SceneStructure via fast_obj (ContentInterchange). One GeometryStructure per
//    object/group × material (flat-shaded when the file carries no normals), one instance each under a placement
//    named after the group. MaterialDescriptor per .mtl material (MaterialCodec::DecodeObj: Kd/Ks/Ns/Ni/d/Ke + maps,
//    Simple class). OBJ has no cameras, lights or transforms — everything is a root placement with identity.
//
// Axis / units: OBJ has no declared convention; the de-facto exporters write right-handed Y-up, so the glTF swap
//    (x, y, z) → (x, −z, y) is applied via ConstructGltfToWorldProjection() and `UniformScale` handles units.

#pragma once

#include "SceneCodec.h"

namespace Frontier {

class ObjCodec
{
public:
    [[nodiscard]] static bool Decode(const std::string& Path, SceneStructure& Out, TextureIndex* Textures, const SceneDecodeConfiguration& Config, std::string* Error) noexcept;
};

} // namespace Frontier
