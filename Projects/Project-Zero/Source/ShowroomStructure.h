//============================================================================================================================================
//                                                      SHOWROOMSTRUCTURE.H
//============================================================================================================================================
// 🧩 Project-Zero's spatial-interface level (`--scene showroom`): the Cornell box widened and furnished, so a 3D
//    interface panel can be judged against saturated neighbours, a mirror, and real colour bleed.
//
//    The original CornellBox.gltf is deliberately left untouched — it is the bit-identity reference for the open GPU
//    verification, and a second level is cheaper than a disputed baseline. The showroom keeps Cornell's red and green
//    side walls (so colour bleed still reads the familiar way) and adds a plinth, a chrome sphere, a matte pillar, a
//    rough copper stand, a deep-blue floor inlay, an amber strip, and a dimmer rear luminaire for rim separation.
//
// Built once in world space (RH Z-up, metres) and exported through SceneCodec::Encode to
//    Content/Scenes/Showroom.gltf, after which the renderer only ever sees the file — the same discipline
//    ShaderBallStructure follows.

#pragma once

#include "../../../Engine/ContentInterchange/MaterialDescriptor.h"
#include "../../../Engine/DeviceExchange/SwapchainExchange.h"

#include <string>
#include <vector>

namespace Frontier {
namespace ProjectZero {

class ShowroomStructure
{
public:
    // Fills the world-space soup. Triangles carry UVs; CornerNormals holds three smooth normals per triangle.
    //    The emissive quads are appended last, the luminaire convention the Cornell box and shader ball share.
    //
    //    DropBodyCount > 0 appends that many spheres above the floor, each with its OWN material so the codec
    //    gives each one its OWN instance — the renderer moves instances, not triangles, so a body that shares a
    //    material with another body could not be moved independently. They are appended BEFORE the luminaires so
    //    the "emissive quads are last" convention still holds.
    void Construct(uint32_t DropBodyCount = 0u) noexcept;

    // Rest pose of drop body `Ordinal` as Construct placed it: where the renderer puts it before physics runs, and
    //    where Project-Physics should create the matching rigid body so the two agree on frame zero.
    [[nodiscard]] static Vector3 QueryDropOrigin(uint32_t Ordinal) noexcept;
    [[nodiscard]] static float   QueryDropRadius() noexcept { return 0.16f; }   // [m]

    // Instance ordinal of the first drop body. Everything below this index is static scenery.
    [[nodiscard]] uint32_t QueryFirstDropInstance() const noexcept { return FirstDropMaterial; }
    [[nodiscard]] uint32_t QueryDropCount()         const noexcept { return DropCount; }

    // Writes Showroom.gltf at Path. Error receives the codec message.
    [[nodiscard]] bool Export(const std::string& Path, std::string* Error) const noexcept;

    [[nodiscard]] const std::vector<TriangleIndex>&      QueryTriangles()     const noexcept { return Triangles; }
    [[nodiscard]] const std::vector<Vector3>&            QueryCornerNormals() const noexcept { return CornerNormals; }
    [[nodiscard]] const std::vector<MaterialDescriptor>& QueryMaterials()     const noexcept { return Materials; }

    // Where the trial interface hangs: centred on the rear wall, at eye height, tilted toward the camera.
    [[nodiscard]] static Vector3 QueryPanelOrigin() noexcept { return Vector3{ 0.0f, 1.55f, 1.32f }; }
    [[nodiscard]] static float   QueryPanelTilt()   noexcept { return -0.21f; }   // [rad] ≈ 12° face-up toward the eye

private:
    void AppendQuad(const Vector3& A, const Vector3& B, const Vector3& C, const Vector3& D, uint32_t Material, float UvScale) noexcept;
    void AppendBox(const Vector3& Minimum, const Vector3& Maximum, uint32_t Material) noexcept;
    void AppendSphere(const Vector3& Centre, float Radius, uint32_t Material, uint32_t Rings, uint32_t Segments) noexcept;
    void AppendTriangle(const Vector3 P[3], const Vector3 N[3], const float Uv[3][2], uint32_t Material) noexcept;

    std::vector<TriangleIndex>      Triangles;
    std::vector<Vector3>            CornerNormals;
    std::vector<MaterialDescriptor> Materials;

    uint32_t                        FirstDropMaterial = 0u;   // [idx] material/instance ordinal of drop body 0
    uint32_t                        DropCount         = 0u;   // [cnt]
};

} // namespace ProjectZero
} // namespace Frontier
