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
    void                    AppendTriangle(const Vector3& v0, const Vector3& v1, const Vector3& v2, uint32_t MaterialIdx) noexcept;
    void                    AppendQuad(const Vector3& v0, const Vector3& v1, const Vector3& v2, const Vector3& v3, uint32_t MaterialIdx) noexcept;
    void                    AppendBox(const Vector3& Center, const Vector3& Extents, float RotationDegrees, uint32_t MaterialIdx) noexcept;

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
