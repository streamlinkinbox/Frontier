//============================================================================================================================================
// 📦 Project-Zero/Source/RayTracingSolver.h — Triangle Geometry Ray Intersection and Analytical Scene Solver
//============================================================================================================================================

#pragma once

#include "TracingIndex.h"
#include <vector>

namespace Frontier::ProjectZero {

//------------------------------------------------------------------------------------------------------------------------
//                                               RAY TRACING SOLVER
//------------------------------------------------------------------------------------------------------------------------

class RayTracingSolver
{
public:
    RayTracingSolver() noexcept;
    ~RayTracingSolver() noexcept = default;

    void                    ConstructCornellBoxScene() noexcept;

    // An OPEN scene, for judging the sky. The Cornell box shows the atmosphere through a 13° oculus at 12.9°
    //    elevation, which is enough to prove the light arrives but far too little to judge a sunset, a moon or a
    //    star field. This is ground and horizon with nothing overhead.
    void                    ConstructOutdoorScene() noexcept;
    void                    AppendTriangle(const Vector3& v0, const Vector3& v1, const Vector3& v2, uint32_t MaterialIdx) noexcept;
    void                    AppendQuad(const Vector3& v0, const Vector3& v1, const Vector3& v2, const Vector3& v3, uint32_t MaterialIdx) noexcept;
    void                    AppendBox(const Vector3& Center, const Vector3& Extents, float RotationDegrees, uint32_t MaterialIdx) noexcept;

    // ── Parametric primitives ────────────────────────────────────────────────────────────────────────────────────
    // Tessellated on the CPU into the same triangle list as everything else: the ray tracer has one geometry
    //    kind, so a sphere is a sphere only in how its vertices are generated.
    //
    //    Segment counts are arguments rather than constants because triangle budget is the scarce resource on a
    //    GTX 1650 SUPER, and a torus at the same tessellation as a sphere costs far more.

    // UV sphere. Rings run pole to pole, Segments run around the equator. The poles are fans rather than
    //    degenerate quads, which keeps zero-area triangles out of the BVH.
    void                    AppendSphere(const Vector3& Center, float Radius, uint32_t Segments, uint32_t Rings, uint32_t MaterialIdx) noexcept;

    // Cone standing on its base, apex at +Z. Radius 0 at the apex would collapse the side quads, so the side is
    //    a triangle fan to the apex and the base is a fan to its own centre.
    void                    AppendCone(const Vector3& BaseCentre, float Radius, float Height, uint32_t Segments, uint32_t MaterialIdx) noexcept;

    // Torus in the XY plane, MajorRadius to the tube centre, MinorRadius the tube itself. Fully closed, so it
    //    needs no caps.
    void                    AppendTorus(const Vector3& Center, float MajorRadius, float MinorRadius, uint32_t MajorSegments, uint32_t MinorSegments, uint32_t MaterialIdx) noexcept;

    // An axis-aligned horizontal plate at Z with a circular hole cut out of it. Used for the roof aperture: a
    //    ring of quads between the hole and the outer rectangle, so the opening is genuinely open rather than
    //    being a dark texture on a solid ceiling.
    void                    AppendPlateWithCircularHole(float MinimumX, float MinimumY, float MaximumX, float MaximumY,
                                                        float Z, const Vector3& HoleCentre, float HoleRadius,
                                                        uint32_t Segments, bool FaceDown, uint32_t MaterialIdx) noexcept;

    [[nodiscard]] HitIntersection EvaluateIntersection(const RayStructure& Ray) const noexcept;
    [[nodiscard]] bool            EvaluateOcclusion(const Vector3& PointA, const Vector3& PointB) const noexcept;

    [[nodiscard]] const std::vector<TriangleGeometry>&   QueryTriangles() const noexcept { return Triangles; }
    [[nodiscard]] const std::vector<AnalyticalMaterial>& QueryMaterials() const noexcept { return Materials; }

    // Single unified conversion operator for total triangle count
    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

private:
    std::vector<TriangleGeometry>   Triangles;                  // [primitives] scene triangle geometry
    std::vector<AnalyticalMaterial> Materials;                  // [materials] scene photometric materials
};

template<>
inline size_t RayTracingSolver::Convert<size_t>() const noexcept
{
    return Triangles.size();
}

template<>
inline uint32_t RayTracingSolver::Convert<uint32_t>() const noexcept
{
    return static_cast<uint32_t>(Triangles.size());
}

} // namespace Frontier::ProjectZero
