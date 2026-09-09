//============================================================================================================================================
//                                                         SCENECODEC.H
//============================================================================================================================================
// 🧩 glTF 2.0 ⇄ SceneStructure (ContentInterchange). Decode: cgltf → GeometryStructure per primitive, one instance per
//    node×primitive, MaterialDescriptor per material (MaterialCodec, all supported KHR extensions + extras), textures
//    registered in the scene's TextureIndex (URI or GLB buffer view), PlacementRecord per node (whole node tree, not
//    only mesh nodes), CameraRecord per camera, PunctualLuminaireRecord per KHR_lights_punctual light.
//    Encode: minimal embedded-buffer writer (positions/normals, indices, materials via MaterialCodec) used once to turn
//    the hard-coded Cornell box into Content/Scenes/CornellBox.gltf — output is byte-identical to the R2 writer.
//
// Axis convention: glTF is right-handed Y-up; the engine is right-handed Z-up (CLAUDE.md §7). The decode applies
//    ClipProjection::ConstructGltfToWorldProjection() to every node's world matrix, the encode applies the inverse to
//    every vertex, so a round trip is exact.

#pragma once

#include "../GeometricRaster/SceneStructure.h"
#include "TextureIndex.h"
#include <string>

namespace Frontier {

struct SceneDecodeConfiguration
{
    float    UniformScale     = 1.0f;    // [-] applied after the axis swap (Sponza's node already carries 0.008)
    float    EmissiveRadiance = 1.0f;    // [-] multiplier on emissiveFactor × emissiveStrength
    uint32_t SlabLimit        = 1u;      // [cnt] [render] slab_limit — MaterialIndex flatten cap
    bool     MergePrimitives  = false;   // [-] reserved: merge same-material primitives before clustering
};

struct SceneEncodeConfiguration
{
    std::string                 Name;                      // scene name; empty = "CornellBox"
    const std::vector<Vector3>* CornerNormals = nullptr;   // 3 per triangle, world space → smooth NORMAL
    bool                        WriteTexcoords = false;    // emit TEXCOORD_0 from TriangleIndex UVs
    // Object spans over Triangles, in append order. Null (or empty) keeps the R2 shape — one node, one mesh,
    //    one primitive per material, byte for byte. Set, each span becomes a named node + mesh, so the decode
    //    carries one placement per scene object for the outliner to walk.
    const std::vector<TriangleSpanRecord>* Spans = nullptr;
};

class SceneCodec
{
public:
    // Fills `Out` (cleared first) from a .gltf / .glb file. Textures are registered (not decoded) in `Textures` when
    //    given. Returns false and sets `Error` on failure; a non-empty `Error` on success is a warning line.
    [[nodiscard]] static bool Decode(const std::string& Path, SceneStructure& Out, TextureIndex* Textures, const SceneDecodeConfiguration& Config, std::string* Error) noexcept;

    // Writes a world-space triangle soup as an embedded-buffer .gltf. Without spans the shape is the R2 one
    //    (one node, one mesh, one primitive per material), reproduced byte for byte (flat normals, no TEXCOORD_0,
    //    node/mesh named "CornellBox"). With spans each span becomes a named node + mesh (emissive spans last).
    //    R4b: CornerNormals (3 per triangle, world space) switch to smooth shading, WriteTexcoords emits TriangleIndex UVs.
    [[nodiscard]] static bool Encode(const std::string& Path, const std::vector<TriangleIndex>& Triangles,
                                     const std::vector<MaterialDescriptor>& Materials, std::string* Error,
                                     const SceneEncodeConfiguration& Configuration = {}) noexcept;
};

} // namespace Frontier
