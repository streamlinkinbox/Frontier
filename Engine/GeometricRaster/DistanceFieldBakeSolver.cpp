//============================================================================================================================================
//                                                 DISTANCEFIELDBAKESOLVER.CPP
//============================================================================================================================================
// 📦 Exact triangle mesh to 3D Signed Distance Field voxel generation solver with accelerated spatial binning.

#include "DistanceFieldBakeSolver.h"
#include "../ContentInterchange/ShaderBallGeometry.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <iostream>

#if defined(_OPENMP)
    #include <omp.h>
#endif

namespace Frontier {

namespace {

inline float Dot(const Vector3& A, const Vector3& B) noexcept
{
    return A.x * B.x + A.y * B.y + A.z * B.z;
}

inline Vector3 Cross(const Vector3& A, const Vector3& B) noexcept
{
    return {
        A.y * B.z - A.z * B.y,
        A.z * B.x - A.x * B.z,
        A.x * B.y - A.y * B.x
    };
}

// Exact point-to-triangle squared distance and closest point
float ComputePointTriangleDistanceSq(const Vector3& Point,
                                     const Vector3& VertA,
                                     const Vector3& VertB,
                                     const Vector3& VertC,
                                     Vector3& OutClosestPoint) noexcept
{
    const Vector3 EdgeAB = VertB - VertA;
    const Vector3 EdgeAC = VertC - VertA;
    const Vector3 DiffAP = Point - VertA;

    const float Dot0 = Dot(EdgeAB, DiffAP);
    const float Dot1 = Dot(EdgeAC, DiffAP);
    if (Dot0 <= 0.0f && Dot1 <= 0.0f)
    {
        OutClosestPoint = VertA;
        return (Point - VertA).LengthSquared();
    }

    const Vector3 DiffBP = Point - VertB;
    const float Dot2 = Dot(EdgeAB, DiffBP);
    const float Dot3 = Dot(EdgeAC, DiffBP);
    if (Dot2 >= 0.0f && Dot3 <= Dot2)
    {
        OutClosestPoint = VertB;
        return (Point - VertB).LengthSquared();
    }

    const float FactorVC = Dot0 * Dot3 - Dot2 * Dot1;
    if (FactorVC <= 0.0f && Dot0 >= 0.0f && Dot2 <= 0.0f)
    {
        const float V = Dot0 / (Dot0 - Dot2);
        OutClosestPoint = VertA + EdgeAB * V;
        return (Point - OutClosestPoint).LengthSquared();
    }

    const Vector3 DiffCP = Point - VertC;
    const float Dot4 = Dot(EdgeAB, DiffCP);
    const float Dot5 = Dot(EdgeAC, DiffCP);
    if (Dot5 >= 0.0f && Dot4 <= Dot5)
    {
        OutClosestPoint = VertC;
        return (Point - VertC).LengthSquared();
    }

    const float FactorVB = Dot4 * Dot1 - Dot0 * Dot5;
    if (FactorVB <= 0.0f && Dot1 >= 0.0f && Dot5 <= 0.0f)
    {
        const float W = Dot1 / (Dot1 - Dot5);
        OutClosestPoint = VertA + EdgeAC * W;
        return (Point - OutClosestPoint).LengthSquared();
    }

    const float FactorVA = Dot2 * Dot5 - Dot4 * Dot3;
    if (FactorVA <= 0.0f && (Dot3 - Dot2) >= 0.0f && (Dot4 - Dot5) >= 0.0f)
    {
        const float W = (Dot3 - Dot2) / ((Dot3 - Dot2) + (Dot4 - Dot5));
        OutClosestPoint = VertB + (VertC - VertB) * W;
        return (Point - OutClosestPoint).LengthSquared();
    }

    const float InverseDenom = 1.0f / (FactorVA + FactorVB + FactorVC);
    const float V = FactorVB * InverseDenom;
    const float W = FactorVC * InverseDenom;
    OutClosestPoint = VertA + EdgeAB * V + EdgeAC * W;
    return (Point - OutClosestPoint).LengthSquared();
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    SOLVER EXECUTION
//------------------------------------------------------------------------------------------------------------------------

bool DistanceFieldBakeSolver::Solve(const GeometryStructure& InputMesh,
                                    const DistanceFieldBakeSpecification& Specification,
                                    DistanceFieldSpace& OutDistanceField,
                                    std::string* OutError) noexcept
{
    const auto& Vertices = InputMesh.QueryVertices();
    const auto& Indices  = InputMesh.QueryIndices();

    const uint32_t VertexCount   = static_cast<uint32_t>(Vertices.size());
    const uint32_t IndexCount    = static_cast<uint32_t>(Indices.size());
    const uint32_t TriangleCount = IndexCount / 3u;

    if (VertexCount == 0u || TriangleCount == 0u)
    {
        if (OutError) *OutError = "input mesh contains no triangles";
        return false;
    }

    // Compute bounding box
    Vector3 BoundingMin = Vertices[0].SpatialLocation;
    Vector3 BoundingMax = Vertices[0].SpatialLocation;

    for (uint32_t V = 1u; V < VertexCount; ++V)
    {
        const Vector3 P = Vertices[V].SpatialLocation;
        BoundingMin.x = std::min(BoundingMin.x, P.x);
        BoundingMin.y = std::min(BoundingMin.y, P.y);
        BoundingMin.z = std::min(BoundingMin.z, P.z);
        BoundingMax.x = std::max(BoundingMax.x, P.x);
        BoundingMax.y = std::max(BoundingMax.y, P.y);
        BoundingMax.z = std::max(BoundingMax.z, P.z);
    }

    // Apply boundary padding
    const float Pad = Specification.BoundaryPadding;
    BoundingMin = BoundingMin - Vector3{ Pad, Pad, Pad };
    BoundingMax = BoundingMax + Vector3{ Pad, Pad, Pad };

    const Vector3 BoundingSpan = BoundingMax - BoundingMin;

    // Build spatial uniform grid acceleration
    const uint32_t Bins = std::max(8u, Specification.SpatialBinCount);
    const Vector3 CellExtent = {
        BoundingSpan.x / static_cast<float>(Bins),
        BoundingSpan.y / static_cast<float>(Bins),
        BoundingSpan.z / static_cast<float>(Bins)
    };

    std::vector<std::vector<uint32_t>> SpatialGrid(static_cast<size_t>(Bins) * Bins * Bins);

    for (uint32_t TriIndex = 0u; TriIndex < TriangleCount; ++TriIndex)
    {
        const Vector3 VertA = Vertices[Indices[TriIndex * 3u + 0u]].SpatialLocation;
        const Vector3 VertB = Vertices[Indices[TriIndex * 3u + 1u]].SpatialLocation;
        const Vector3 VertC = Vertices[Indices[TriIndex * 3u + 2u]].SpatialLocation;

        const Vector3 TriMin = {
            std::min({ VertA.x, VertB.x, VertC.x }),
            std::min({ VertA.y, VertB.y, VertC.y }),
            std::min({ VertA.z, VertB.z, VertC.z })
        };
        const Vector3 TriMax = {
            std::max({ VertA.x, VertB.x, VertC.x }),
            std::max({ VertA.y, VertB.y, VertC.y }),
            std::max({ VertA.z, VertB.z, VertC.z })
        };

        const int32_t I0 = std::clamp(static_cast<int32_t>((TriMin.x - BoundingMin.x) / CellExtent.x), 0, static_cast<int32_t>(Bins) - 1);
        const int32_t I1 = std::clamp(static_cast<int32_t>((TriMax.x - BoundingMin.x) / CellExtent.x), 0, static_cast<int32_t>(Bins) - 1);
        const int32_t J0 = std::clamp(static_cast<int32_t>((TriMin.y - BoundingMin.y) / CellExtent.y), 0, static_cast<int32_t>(Bins) - 1);
        const int32_t J1 = std::clamp(static_cast<int32_t>((TriMax.y - BoundingMin.y) / CellExtent.y), 0, static_cast<int32_t>(Bins) - 1);
        const int32_t K0 = std::clamp(static_cast<int32_t>((TriMin.z - BoundingMin.z) / CellExtent.z), 0, static_cast<int32_t>(Bins) - 1);
        const int32_t K1 = std::clamp(static_cast<int32_t>((TriMax.z - BoundingMin.z) / CellExtent.z), 0, static_cast<int32_t>(Bins) - 1);

        for (int32_t K = K0; K <= K1; ++K)
        {
            for (int32_t J = J0; J <= J1; ++J)
            {
                for (int32_t I = I0; I <= I1; ++I)
                {
                    const size_t LinearBin = static_cast<size_t>(K) * Bins * Bins + static_cast<size_t>(J) * Bins + static_cast<size_t>(I);
                    SpatialGrid[LinearBin].push_back(TriIndex);
                }
            }
        }
    }

    const uint32_t ResX = Specification.ResolutionX;
    const uint32_t ResY = Specification.ResolutionY;
    const uint32_t ResZ = Specification.ResolutionZ;

    OutDistanceField = DistanceFieldSpace(ResX, ResY, ResZ, BoundingMin, BoundingMax);

    // Parallel voxel distance solve
#if defined(_OPENMP)
    #pragma omp parallel for collapse(3) schedule(dynamic, 4)
#endif
    for (int32_t K = 0; K < static_cast<int32_t>(ResZ); ++K)
    {
        for (int32_t J = 0; J < static_cast<int32_t>(ResY); ++J)
        {
            for (int32_t I = 0; I < static_cast<int32_t>(ResX); ++I)
            {
                const float RatioX = (static_cast<float>(I) + 0.5f) / static_cast<float>(ResX);
                const float RatioY = (static_cast<float>(J) + 0.5f) / static_cast<float>(ResY);
                const float RatioZ = (static_cast<float>(K) + 0.5f) / static_cast<float>(ResZ);

                const Vector3 Point = {
                    BoundingMin.x + RatioX * BoundingSpan.x,
                    BoundingMin.y + RatioY * BoundingSpan.y,
                    BoundingMin.z + RatioZ * BoundingSpan.z
                };

                const int32_t CenterI = std::clamp(static_cast<int32_t>((Point.x - BoundingMin.x) / CellExtent.x), 0, static_cast<int32_t>(Bins) - 1);
                const int32_t CenterJ = std::clamp(static_cast<int32_t>((Point.y - BoundingMin.y) / CellExtent.y), 0, static_cast<int32_t>(Bins) - 1);
                const int32_t CenterK = std::clamp(static_cast<int32_t>((Point.z - BoundingMin.z) / CellExtent.z), 0, static_cast<int32_t>(Bins) - 1);

                float MinimumDistanceSq = 1e9f;
                Vector3 BestClosestPoint{};
                uint32_t BestTriangle = 0u;

                for (int32_t SearchRadius = 2; SearchRadius <= static_cast<int32_t>(Bins); SearchRadius += 2)
                {
                    const int32_t MinI = std::max(0, CenterI - SearchRadius);
                    const int32_t MaxI = std::min(static_cast<int32_t>(Bins) - 1, CenterI + SearchRadius);
                    const int32_t MinJ = std::max(0, CenterJ - SearchRadius);
                    const int32_t MaxJ = std::min(static_cast<int32_t>(Bins) - 1, CenterJ + SearchRadius);
                    const int32_t MinK = std::max(0, CenterK - SearchRadius);
                    const int32_t MaxK = std::min(static_cast<int32_t>(Bins) - 1, CenterK + SearchRadius);

                    for (int32_t ZK = MinK; ZK <= MaxK; ++ZK)
                    {
                        for (int32_t YJ = MinJ; YJ <= MaxJ; ++YJ)
                        {
                            for (int32_t XI = MinI; XI <= MaxI; ++XI)
                            {
                                const size_t LinearBin = static_cast<size_t>(ZK) * Bins * Bins + static_cast<size_t>(YJ) * Bins + static_cast<size_t>(XI);
                                const auto& BinTriangles = SpatialGrid[LinearBin];

                                for (uint32_t TriIdx : BinTriangles)
                                {
                                    Vector3 ClosestOnTri{};
                                    const float DistSq = ComputePointTriangleDistanceSq(Point,
                                        Vertices[Indices[TriIdx * 3u + 0u]].SpatialLocation,
                                        Vertices[Indices[TriIdx * 3u + 1u]].SpatialLocation,
                                        Vertices[Indices[TriIdx * 3u + 2u]].SpatialLocation,
                                        ClosestOnTri);

                                    if (DistSq < MinimumDistanceSq)
                                    {
                                        MinimumDistanceSq = DistSq;
                                        BestClosestPoint  = ClosestOnTri;
                                        BestTriangle      = TriIdx;
                                    }
                                }
                            }
                        }
                    }

                    const float SearchExtent = static_cast<float>(SearchRadius) * CellExtent.x;
                    if (MinimumDistanceSq < SearchExtent * SearchExtent) break;
                }

                const float UnsignedDist = std::sqrt(MinimumDistanceSq);

                // Sign determination: angle with triangle normal
                const Vector3 VertA = Vertices[Indices[BestTriangle * 3u + 0u]].SpatialLocation;
                const Vector3 VertB = Vertices[Indices[BestTriangle * 3u + 1u]].SpatialLocation;
                const Vector3 VertC = Vertices[Indices[BestTriangle * 3u + 2u]].SpatialLocation;
                const Vector3 TriNormal = Cross(VertB - VertA, VertC - VertA).Normalized();

                const Vector3 Difference = Point - BestClosestPoint;
                const float SignFactor = (Dot(Difference, TriNormal) >= 0.0f) ? 1.0f : -1.0f;

                OutDistanceField.SetVoxelSample(static_cast<uint32_t>(I),
                                                static_cast<uint32_t>(J),
                                                static_cast<uint32_t>(K),
                                                UnsignedDist * SignFactor);
            }
        }
    }

    return true;
}

bool DistanceFieldBakeSolver::BakeShaderBall(const std::string& MeshAssetPath,
                                             const std::string& OutputSdfPath,
                                             const DistanceFieldBakeSpecification& Specification,
                                             std::string* OutError) noexcept
{
    GeometryStructure ShaderBallMesh;
    std::string LoadError;
    if (!ShaderBallGeometry::LoadResolved(MeshAssetPath, ShaderBallMesh, nullptr, &LoadError))
    {
        if (OutError) *OutError = "failed to load shaderball: " + LoadError;
        return false;
    }

    DistanceFieldSpace BakedField;
    if (!Solve(ShaderBallMesh, Specification, BakedField, OutError))
    {
        return false;
    }

    if (!BakedField.SaveToFile(OutputSdfPath))
    {
        if (OutError) *OutError = "failed to write output distance field to " + OutputSdfPath;
        return false;
    }

    return true;
}

} // namespace Frontier
