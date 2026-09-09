//============================================================================================================================================
//                                                     SHADERBALLSTRUCTURE.H
//============================================================================================================================================
// 🧩 R4b material test level (`--scene shaderball`): a procedural grid of UV spheres on a matte plane under one area
//    luminaire, one sphere per lobe of the resolved slab — dielectric roughness ramp, metals (F82 tint), thin film, coat,
//    fuzz, haziness, EON diffuse roughness, emission, alpha-masked card. Built once in world space (RH Z-up, metres),
//    exported through SceneCodec::Encode (smooth normals + TEXCOORD_0) to Content/Scenes/ShaderBall.gltf and thereafter
//    loaded like any other level, so the file — not this code — is what the renderer sees.

#pragma once

#include "MaterialDescriptor.h"
#include "../DeviceExchange/SwapchainExchange.h"
#include <string>
#include <vector>

namespace Frontier {

class ShaderBallStructure
{
public:
    // Fills the world-space soup. Triangles carry UVs; CornerNormals holds 3 smooth normals per triangle (TriangleIndex
    //    has no room for them). The emissive quad is appended last (luminaire convention shared with the Cornell box).
    void Construct() noexcept;

    // Writes ShaderBall.gltf at Path (SceneCodec::Encode with smooth normals and texcoords). Error receives the codec message.
    [[nodiscard]] bool Export(const std::string& Path, std::string* Error) const noexcept;

    [[nodiscard]] const std::vector<TriangleIndex>&      QueryTriangles()     const noexcept { return Triangles; }
    [[nodiscard]] const std::vector<Vector3>&            QueryCornerNormals() const noexcept { return CornerNormals; }
    [[nodiscard]] const std::vector<MaterialDescriptor>& QueryMaterials()     const noexcept { return Materials; }

private:
    void AppendSphere(const Vector3& Centre, float Radius, uint32_t Material, uint32_t Rings, uint32_t Segments) noexcept;
    void AppendQuad(const Vector3& A, const Vector3& B, const Vector3& C, const Vector3& D, uint32_t Material, float UvScale) noexcept;
    void AppendTriangle(const Vector3 P[3], const Vector3 N[3], const float Uv[3][2], uint32_t Material) noexcept;

    std::vector<TriangleIndex>      Triangles;
    std::vector<Vector3>            CornerNormals;
    std::vector<MaterialDescriptor> Materials;
};

} // namespace Frontier
