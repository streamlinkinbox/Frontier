//============================================================================================================================================
//                                                      DRIVESCENEAUTHOR.H
//============================================================================================================================================
// 📦 The `--scene drive` level author — the drivable course + the real ControlVehicle + four procedural wheels,
//    built once into a world-space triangle soup and exported through SceneCodec::Encode, then imported like any glTF.
//    This is the exact export-once pattern ShowcaseStructure uses; only the contents differ.
//
//    INSTANCE-ORDER CONTRACT (the decode makes one instance per node×primitive, in span-append order):
//        span 0  "Vehicle body"  — paint + glass + plastic -> instance 0 (dynamic; VehicleInstanceSequence body)
//        span 1  "Wheel FL"      — rubber + hub + brake -> instance 1 (dynamic; wheel 0)
//        span 2  "Wheel FR"      — rubber + hub + brake -> instance 2 (dynamic; wheel 1)
//        span 3  "Wheel RL"      — rubber + hub + brake -> instance 3 (dynamic; wheel 2)
//        span 4  "Wheel RR"      — rubber + hub + brake -> instance 4 (dynamic; wheel 3)
//        span 5  "Course"        — 6 materials -> static instances
//    SceneCodec preserves these material partitions inside their spans; VehicleInstanceSequence still addresses the
//    body and four wheel spans by the same stable instance ordinal, not by a per-material instance count.
//
//    Object-local origins (so the live World = T(pose)·R(pose) places each part correctly, no rest subtraction):
//        • body  — ControlVehicleMesh shifted down by the geometry's CoM height, so local origin = centre of mass.
//        • wheel — cylinder centred at the origin, axle along local +Y (VehicleInstanceSequence spins about local Y).
//    The course is emitted in world space (it never moves). Body has NO collider — only the wheels meet the ground.
//
//    World space is RH Z-up, metres.

#pragma once

#include "../../../Engine/ContentInterchange/MaterialDescriptor.h"
#include "../../../Engine/DeviceExchange/SwapchainExchange.h"   // Vector3, TriangleIndex, TriangleSpanRecord
#include "../../../Engine/PhysicalDynamics/Vehicle/VehicleGeometry.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier {
namespace Drive {

// Advance this stamp and the versioned OpeningScene filename together when Construct() changes the scene.
// Existing scenes are never overwritten by a build, including authored copies of an earlier opening.
inline constexpr uint32_t kDriveSceneRevision = 4u;   // 4: ground-referenced opening body and wheel placement

// True when the file at Path was written by this revision (cheap header scan). Missing/older ⇒ false ⇒ re-export.
[[nodiscard]] bool DriveSceneMatchesRevision(const std::string& Path) noexcept;

class DriveSceneAuthor
{
public:
    // Fills the triangle soup: authors the 12-material course / ControlVehicle palette, then appends body, four
    //    wheels, and the course (in that span order — the instance-order contract above).
    // StaticPose = false (the default) emits all four wheels at the span origin, because the raster host
    // supplies each wheel span's world transform per frame from the suspension solver.
    //
    // StaticPose = true bakes each wheel at its rest hub instead.  A ray-traced host renders the level as ONE
    // static soup with no per-span instancing, so with the default the four wheels stack at the body origin
    // and vanish inside the shell -- which is exactly how the first ReSTIR sheets came out wheel-less.
    void Construct(bool StaticPose = false) noexcept;

    // Writes DriveCourse.gltf at Path (embedded buffer, smooth normals, texcoords, spans). Error gets the message.
    [[nodiscard]] bool Export(const std::string& Path, std::string* Error) const noexcept;

    [[nodiscard]] const std::vector<TriangleIndex>&      QueryTriangles()     const noexcept { return Triangles; }
    [[nodiscard]] const std::vector<Vector3>&            QueryCornerNormals() const noexcept { return CornerNormals; }
    [[nodiscard]] const std::vector<MaterialDescriptor>& QueryMaterials()     const noexcept { return Materials; }
    [[nodiscard]] const std::vector<TriangleSpanRecord>& QuerySpans()         const noexcept { return Spans; }

private:
    struct SpanScope
    {
        std::vector<TriangleSpanRecord>*  Spans     = nullptr;
        const std::vector<TriangleIndex>* Triangles = nullptr;
        uint32_t                          Span      = 0u;
        ~SpanScope() noexcept;
    };
    [[nodiscard]] SpanScope OpenSpan(const char* Name, bool Dynamic) noexcept;

    void AppendTriangle(const Vector3 P[3], const Vector3 N[3], const float Uv[3][2], uint32_t Material) noexcept;
    void AppendFace(const Vector3& A, const Vector3& B, const Vector3& C, uint32_t Material) noexcept;   // flat-shaded

    void AppendVehicleBody(float ComHeight, uint32_t Material) noexcept;
    void AppendWheel(const Vector3& LocalHub, float Radius, float HalfWidth, uint32_t Segments, uint32_t Material) noexcept;
    void AppendCourse() noexcept;

    void AuthorMaterials() noexcept;

    std::vector<TriangleIndex>      Triangles;
    std::vector<Vector3>            CornerNormals;
    std::vector<MaterialDescriptor> Materials;
    std::vector<TriangleSpanRecord> Spans;
};

} // namespace Drive
} // namespace Frontier
