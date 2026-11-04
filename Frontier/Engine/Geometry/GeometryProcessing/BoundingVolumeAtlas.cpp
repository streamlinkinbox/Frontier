/*==========================================================================================================================================
                                             BOUNDING VOLUME ATLAS DEFINITION
==========================================================================================================================================*/
// 🧩 16-bin Surface-Area-Heuristic (SAH) software BVH builder, ordered child slab CPU ray tracer, & Slang float4 packer.

#include "Engine/Geometry/GeometryProcessing/BoundingVolumeAtlas.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numeric>

namespace Frontier
{
namespace
{

constexpr uint32_t kInteriorFlagBit = 0x80000000u;
constexpr uint32_t kSahBinCount     = 16u;
constexpr uint32_t kMaxLeafTris     = 4u;

float SurfaceAreaBox(const std::array<float, 3>& Lo, const std::array<float, 3>& Hi) noexcept
{
    const float Dx = std::max(0.0f, Hi[0] - Lo[0]);
    const float Dy = std::max(0.0f, Hi[1] - Lo[1]);
    const float Dz = std::max(0.0f, Hi[2] - Lo[2]);
    return 2.0f * (Dx * Dy + Dy * Dz + Dz * Dx);
}

bool IntersectSlabAabb(const std::array<float, 3>& Lo,
                       const std::array<float, 3>& Hi,
                       const std::array<float, 3>& Origin,
                       const std::array<float, 3>& InvDir,
                       float                       MinT,
                       float                       MaxT,
                       float&                      OutEntryT) noexcept
{
    float T0 = MinT;
    float T1 = MaxT;
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        const float A = (Lo[Axis] - Origin[Axis]) * InvDir[Axis];
        const float B = (Hi[Axis] - Origin[Axis]) * InvDir[Axis];
        T0            = std::max(T0, std::min(A, B));
        T1            = std::min(T1, std::max(A, B));
        if (T1 < T0)
        {
            return false;
        }
    }
    OutEntryT = T0;
    return true;
}

bool IntersectRayTriangleMoller(const PackedRayTriangle&    Tri,
                                const std::array<float, 3>& Origin,
                                const std::array<float, 3>& Dir,
                                float                       MinT,
                                float                       MaxT,
                                float&                      OutT,
                                float&                      OutU,
                                float&                      OutV) noexcept
{
    const std::array<float, 3> Pvec = {
        Dir[1] * Tri.Edge2[2] - Dir[2] * Tri.Edge2[1],
        Dir[2] * Tri.Edge2[0] - Dir[0] * Tri.Edge2[2],
        Dir[0] * Tri.Edge2[1] - Dir[1] * Tri.Edge2[0],
    };
    const float Det = Tri.Edge1[0] * Pvec[0] + Tri.Edge1[1] * Pvec[1] + Tri.Edge1[2] * Pvec[2];
    if (std::abs(Det) <= 1.0e-9f)
    {
        return false;
    }
    const float                InvDet = 1.0f / Det;
    const std::array<float, 3> Tvec   = {
        Origin[0] - Tri.Vertex0[0],
        Origin[1] - Tri.Vertex0[1],
        Origin[2] - Tri.Vertex0[2],
    };
    const float U = (Tvec[0] * Pvec[0] + Tvec[1] * Pvec[1] + Tvec[2] * Pvec[2]) * InvDet;
    if (U < 0.0f || U > 1.0f)
    {
        return false;
    }
    const std::array<float, 3> Qvec = {
        Tvec[1] * Tri.Edge1[2] - Tvec[2] * Tri.Edge1[1],
        Tvec[2] * Tri.Edge1[0] - Tvec[0] * Tri.Edge1[2],
        Tvec[0] * Tri.Edge1[1] - Tvec[1] * Tri.Edge1[0],
    };
    const float V = (Dir[0] * Qvec[0] + Dir[1] * Qvec[1] + Dir[2] * Qvec[2]) * InvDet;
    if (V < 0.0f || (U + V) > 1.0f)
    {
        return false;
    }
    const float HitT = (Tri.Edge2[0] * Qvec[0] + Tri.Edge2[1] * Qvec[1] + Tri.Edge2[2] * Qvec[2]) * InvDet;
    if (HitT < MinT || HitT > MaxT)
    {
        return false;
    }
    OutT = HitT;
    OutU = U;
    OutV = V;
    return true;
}

} // namespace

uint32_t BoundingVolumeAtlas::BuildRecursive(std::vector<uint32_t>&             Indices,
                                             std::vector<std::array<float, 3>>& Centroids,
                                             std::vector<std::array<float, 3>>& TriBoundsMin,
                                             std::vector<std::array<float, 3>>& TriBoundsMax,
                                             uint32_t                           Begin,
                                             uint32_t                           Count)
{
    EnclosureCell Cell{};
    Cell.LowerBounds = {1.0e30f, 1.0e30f, 1.0e30f};
    Cell.UpperBounds = {-1.0e30f, -1.0e30f, -1.0e30f};

    std::array<float, 3> CentMin = {1.0e30f, 1.0e30f, 1.0e30f};
    std::array<float, 3> CentMax = {-1.0e30f, -1.0e30f, -1.0e30f};

    for (uint32_t I = Begin; I < Begin + Count; ++I)
    {
        const uint32_t TriIdx = Indices[I];
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            Cell.LowerBounds[Axis] = std::min(Cell.LowerBounds[Axis], TriBoundsMin[TriIdx][Axis] - 1.0e-4f);
            Cell.UpperBounds[Axis] = std::max(Cell.UpperBounds[Axis], TriBoundsMax[TriIdx][Axis] + 1.0e-4f);
            CentMin[Axis]          = std::min(CentMin[Axis], Centroids[TriIdx][Axis]);
            CentMax[Axis]          = std::max(CentMax[Axis], Centroids[TriIdx][Axis]);
        }
    }

    const uint32_t CellIndex = static_cast<uint32_t>(Cells_.size());
    Cells_.push_back(Cell);

    if (Count <= kMaxLeafTris)
    {
        Cells_[CellIndex].LeftOrFirst  = Begin;
        Cells_[CellIndex].RightOrCount = Count;
        return CellIndex;
    }

    const float ParentArea = std::max(SurfaceAreaBox(Cell.LowerBounds, Cell.UpperBounds), 1.0e-8f);
    float       BestCost   = static_cast<float>(Count);
    int         BestAxis   = -1;
    uint32_t    BestSplit  = 0u;

    struct SahBin
    {
        std::array<float, 3> Lo    = {1.0e30f, 1.0e30f, 1.0e30f};
        std::array<float, 3> Hi    = {-1.0e30f, -1.0e30f, -1.0e30f};
        uint32_t             Count = 0u;
    };

    for (int Axis = 0; Axis < 3; ++Axis)
    {
        const float Span = CentMax[Axis] - CentMin[Axis];
        if (Span <= 1.0e-6f)
        {
            continue;
        }
        std::array<SahBin, kSahBinCount> Bins{};
        const float Scale = static_cast<float>(kSahBinCount) / Span;
        for (uint32_t I = Begin; I < Begin + Count; ++I)
        {
            const uint32_t TriIdx = Indices[I];
            const uint32_t BinIdx = std::min(
                kSahBinCount - 1u,
                static_cast<uint32_t>(std::max(0.0f, (Centroids[TriIdx][Axis] - CentMin[Axis]) * Scale)));
            ++Bins[BinIdx].Count;
            for (int A = 0; A < 3; ++A)
            {
                Bins[BinIdx].Lo[A] = std::min(Bins[BinIdx].Lo[A], TriBoundsMin[TriIdx][A]);
                Bins[BinIdx].Hi[A] = std::max(Bins[BinIdx].Hi[A], TriBoundsMax[TriIdx][A]);
            }
        }

        std::array<float, kSahBinCount - 1u>    LeftArea{};
        std::array<uint32_t, kSahBinCount - 1u> LeftCount{};
        SahBin                                  RunLeft{};
        for (uint32_t S = 0u; S < kSahBinCount - 1u; ++S)
        {
            RunLeft.Count += Bins[S].Count;
            for (int A = 0; A < 3; ++A)
            {
                RunLeft.Lo[A] = std::min(RunLeft.Lo[A], Bins[S].Lo[A]);
                RunLeft.Hi[A] = std::max(RunLeft.Hi[A], Bins[S].Hi[A]);
            }
            LeftCount[S] = RunLeft.Count;
            LeftArea[S]  = (RunLeft.Count > 0u) ? SurfaceAreaBox(RunLeft.Lo, RunLeft.Hi) : 0.0f;
        }

        SahBin RunRight{};
        for (int S = static_cast<int>(kSahBinCount) - 2; S >= 0; --S)
        {
            const uint32_t RightIdx = static_cast<uint32_t>(S + 1);
            RunRight.Count += Bins[RightIdx].Count;
            for (int A = 0; A < 3; ++A)
            {
                RunRight.Lo[A] = std::min(RunRight.Lo[A], Bins[RightIdx].Lo[A]);
                RunRight.Hi[A] = std::max(RunRight.Hi[A], Bins[RightIdx].Hi[A]);
            }
            if (LeftCount[S] == 0u || RunRight.Count == 0u)
            {
                continue;
            }
            const float RightArea = SurfaceAreaBox(RunRight.Lo, RunRight.Hi);
            const float Cost      = 0.5f + (LeftArea[S] * static_cast<float>(LeftCount[S]) +
                                       RightArea * static_cast<float>(RunRight.Count)) /
                                          ParentArea;
            if (Cost < BestCost)
            {
                BestCost  = Cost;
                BestAxis  = Axis;
                BestSplit = static_cast<uint32_t>(S + 1);
            }
        }
    }

    uint32_t Mid = Begin + Count / 2u;
    if (BestAxis >= 0)
    {
        const float Span  = CentMax[BestAxis] - CentMin[BestAxis];
        const float Scale = static_cast<float>(kSahBinCount) / Span;
        auto        SplitIt = std::partition(
            Indices.begin() + Begin,
            Indices.begin() + Begin + Count,
            [&](uint32_t TriIdx) {
                const uint32_t BinIdx = std::min(
                    kSahBinCount - 1u,
                    static_cast<uint32_t>(std::max(0.0f, (Centroids[TriIdx][BestAxis] - CentMin[BestAxis]) * Scale)));
                return BinIdx < BestSplit;
            });
        Mid = static_cast<uint32_t>(SplitIt - Indices.begin());
        if (Mid == Begin || Mid == Begin + Count)
        {
            Mid = Begin + Count / 2u;
            std::nth_element(Indices.begin() + Begin,
                             Indices.begin() + Mid,
                             Indices.begin() + Begin + Count,
                             [&](uint32_t A, uint32_t B) {
                                 return Centroids[A][BestAxis] < Centroids[B][BestAxis];
                             });
        }
    }
    else
    {
        int WideAxis = 0;
        for (int A = 1; A < 3; ++A)
        {
            if ((CentMax[A] - CentMin[A]) > (CentMax[WideAxis] - CentMin[WideAxis]))
            {
                WideAxis = A;
            }
        }
        std::nth_element(Indices.begin() + Begin,
                         Indices.begin() + Mid,
                         Indices.begin() + Begin + Count,
                         [&](uint32_t A, uint32_t B) {
                             return Centroids[A][WideAxis] < Centroids[B][WideAxis];
                         });
    }

    const uint32_t LeftChild  = BuildRecursive(Indices, Centroids, TriBoundsMin, TriBoundsMax, Begin, Mid - Begin);
    const uint32_t RightChild = BuildRecursive(Indices, Centroids, TriBoundsMin, TriBoundsMax, Mid, Begin + Count - Mid);
    Cells_[CellIndex].LeftOrFirst  = LeftChild;
    Cells_[CellIndex].RightOrCount = RightChild | kInteriorFlagBit;
    return CellIndex;
}

bool BoundingVolumeAtlas::BuildFromFacetField(const FacetField&                   SourceFacets,
                                              const ScreenSpaceClusterRefinement* MaterialSource)
{
    Cells_.clear();
    Triangles_.clear();
    const size_t TriCount = SourceFacets.Corners.size() / 3u;
    if (TriCount == 0u)
    {
        return false;
    }

    std::vector<PackedRayTriangle>     RawTris;
    std::vector<std::array<float, 3>>  Centroids;
    std::vector<std::array<float, 3>>  BoundsMin;
    std::vector<std::array<float, 3>>  BoundsMax;
    RawTris.reserve(TriCount);
    Centroids.reserve(TriCount);
    BoundsMin.reserve(TriCount);
    BoundsMax.reserve(TriCount);

    for (size_t TriIdx = 0u; TriIdx < TriCount; ++TriIdx)
    {
        const FacetPoint3D& C0      = SourceFacets.Corners[TriIdx * 3u + 0u];
        const FacetPoint3D& C1      = SourceFacets.Corners[TriIdx * 3u + 1u];
        const FacetPoint3D& C2      = SourceFacets.Corners[TriIdx * 3u + 2u];
        const uint32_t      ClassId =
            (TriIdx < SourceFacets.Classification.size()) ? SourceFacets.Classification[TriIdx] : 0u;

        PackedRayTriangle Packed{};
        Packed.Vertex0 = {static_cast<float>(C0.X), static_cast<float>(C0.Y), static_cast<float>(C0.Z)};
        const std::array<float, 3> V1 = {static_cast<float>(C1.X), static_cast<float>(C1.Y), static_cast<float>(C1.Z)};
        const std::array<float, 3> V2 = {static_cast<float>(C2.X), static_cast<float>(C2.Y), static_cast<float>(C2.Z)};
        Packed.Edge1 = {V1[0] - Packed.Vertex0[0], V1[1] - Packed.Vertex0[1], V1[2] - Packed.Vertex0[2]};
        Packed.Edge2 = {V2[0] - Packed.Vertex0[0], V2[1] - Packed.Vertex0[1], V2[2] - Packed.Vertex0[2]};

        if (TriIdx * 3u + 2u < SourceFacets.CornerUVs.size())
        {
            Packed.Uv0U = static_cast<float>(SourceFacets.CornerUVs[TriIdx * 3u + 0u][0]);
            Packed.Uv0V = static_cast<float>(SourceFacets.CornerUVs[TriIdx * 3u + 0u][1]);
            Packed.Uv1  = {static_cast<float>(SourceFacets.CornerUVs[TriIdx * 3u + 1u][0]),
                           static_cast<float>(SourceFacets.CornerUVs[TriIdx * 3u + 1u][1])};
            Packed.Uv2  = {static_cast<float>(SourceFacets.CornerUVs[TriIdx * 3u + 2u][0]),
                           static_cast<float>(SourceFacets.CornerUVs[TriIdx * 3u + 2u][1])};
        }

        Packed.ClassificationId = ClassId;
        if (MaterialSource != nullptr)
        {
            const ClusterMaterialProfile& Mat = MaterialSource->FindMaterial(ClassId);
            Packed.TransmissiveGlass          = Mat.TransmissiveGlass;
            Packed.RefractiveIndex            = Mat.RefractiveIndexIor;
            Packed.AbsorptionPerM             = {Mat.SpectralAbsorptionPerMeter.X,
                                                 Mat.SpectralAbsorptionPerMeter.Y,
                                                 Mat.SpectralAbsorptionPerMeter.Z};
        }
        else if (ClassId == 7u || ClassId == 12u)
        {
            Packed.TransmissiveGlass = true;
            Packed.RefractiveIndex   = (ClassId == 12u) ? 1.58f : 1.52f;
        }

        RawTris.push_back(Packed);
        Centroids.push_back({
            (Packed.Vertex0[0] + V1[0] + V2[0]) * (1.0f / 3.0f),
            (Packed.Vertex0[1] + V1[1] + V2[1]) * (1.0f / 3.0f),
            (Packed.Vertex0[2] + V1[2] + V2[2]) * (1.0f / 3.0f),
        });
        BoundsMin.push_back({
            std::min({Packed.Vertex0[0], V1[0], V2[0]}),
            std::min({Packed.Vertex0[1], V1[1], V2[1]}),
            std::min({Packed.Vertex0[2], V1[2], V2[2]}),
        });
        BoundsMax.push_back({
            std::max({Packed.Vertex0[0], V1[0], V2[0]}),
            std::max({Packed.Vertex0[1], V1[1], V2[1]}),
            std::max({Packed.Vertex0[2], V1[2], V2[2]}),
        });
    }

    std::vector<uint32_t> Order(RawTris.size());
    std::iota(Order.begin(), Order.end(), 0u);
    Cells_.reserve(RawTris.size() * 2u);
    (void)BuildRecursive(Order, Centroids, BoundsMin, BoundsMax, 0u, static_cast<uint32_t>(Order.size()));

    Triangles_.resize(Order.size());
    for (size_t NewIdx = 0u; NewIdx < Order.size(); ++NewIdx)
    {
        Triangles_[NewIdx] = RawTris[Order[NewIdx]];
    }
    return !Cells_.empty();
}

bool BoundingVolumeAtlas::BuildFromRefinementCut(const ScreenSpaceClusterRefinement& Refinement,
                                                 const ClusterRuntimeFrameReport&    CutReport)
{
    FacetField CutFacets{};
    const auto& Sequence = Refinement.TierSequence();
    const auto& Records  = Refinement.ClusterRecords();

    auto AppendGlobals = [&](const std::vector<uint32_t>& Globals) {
        for (uint32_t Gid : Globals)
        {
            if (Gid >= Records.size())
            {
                continue;
            }
            const ClusterRefinementRecord& Rec  = Records[Gid];
            const ClusterTier&             Tier = Sequence.Tiers[Rec.TierIndex];
            const FacetCluster&            Fc   = Tier.Clusters[Rec.ClusterIndexInTier];
            for (uint32_t Tri : Fc.Triangles)
            {
                CutFacets.Corners.push_back(Tier.Complex.Positions[Tier.Complex.Triangles[Tri * 3u + 0u]]);
                CutFacets.Corners.push_back(Tier.Complex.Positions[Tier.Complex.Triangles[Tri * 3u + 1u]]);
                CutFacets.Corners.push_back(Tier.Complex.Positions[Tier.Complex.Triangles[Tri * 3u + 2u]]);
                CutFacets.Classification.push_back(Rec.ClassificationId);
                if (Tier.Complex.CarriesUVs && Tri * 3u + 2u < Tier.Complex.CornerUVs.size())
                {
                    CutFacets.CornerUVs.push_back(Tier.Complex.CornerUVs[Tri * 3u + 0u]);
                    CutFacets.CornerUVs.push_back(Tier.Complex.CornerUVs[Tri * 3u + 1u]);
                    CutFacets.CornerUVs.push_back(Tier.Complex.CornerUVs[Tri * 3u + 2u]);
                }
                else
                {
                    CutFacets.CornerUVs.push_back({0.0, 0.0});
                    CutFacets.CornerUVs.push_back({0.0, 0.0});
                    CutFacets.CornerUVs.push_back({0.0, 0.0});
                }
            }
        }
    };

    AppendGlobals(CutReport.SelectedOpaqueGlobals);
    AppendGlobals(CutReport.SelectedGlassGlobals);
    return BuildFromFacetField(CutFacets, &Refinement);
}

SoftwareRayHit BoundingVolumeAtlas::TraceClosestRay(std::array<float, 3> OriginMeters,
                                                    std::array<float, 3> DirectionUnit,
                                                    float                MinDistanceMeters,
                                                    float                MaxDistanceMeters,
                                                    bool                 IncludeGlass) const noexcept
{
    SoftwareRayHit Result{};
    if (Cells_.empty() || Triangles_.empty())
    {
        return Result;
    }

    const std::array<float, 3> InvDir = {
        1.0f / (std::abs(DirectionUnit[0]) > 1.0e-8f ? DirectionUnit[0] : (DirectionUnit[0] >= 0.0f ? 1.0e-8f : -1.0e-8f)),
        1.0f / (std::abs(DirectionUnit[1]) > 1.0e-8f ? DirectionUnit[1] : (DirectionUnit[1] >= 0.0f ? 1.0e-8f : -1.0e-8f)),
        1.0f / (std::abs(DirectionUnit[2]) > 1.0e-8f ? DirectionUnit[2] : (DirectionUnit[2] >= 0.0f ? 1.0e-8f : -1.0e-8f)),
    };

    float                 BestT   = MaxDistanceMeters;
    float                 BestU   = 0.0f;
    float                 BestV   = 0.0f;
    uint32_t              BestIdx = UINT32_MAX;
    std::array<uint32_t, 64> Stack{};
    uint32_t                 StackTop = 0u;
    float                    RootEntry = 0.0f;
    if (IntersectSlabAabb(Cells_[0].LowerBounds, Cells_[0].UpperBounds, OriginMeters, InvDir, MinDistanceMeters, BestT, RootEntry))
    {
        Stack[StackTop++] = 0u;
    }

    while (StackTop > 0u)
    {
        const EnclosureCell& Cell = Cells_[Stack[--StackTop]];
        if ((Cell.RightOrCount & kInteriorFlagBit) != 0u)
        {
            const uint32_t LeftIdx  = Cell.LeftOrFirst;
            const uint32_t RightIdx = Cell.RightOrCount & ~kInteriorFlagBit;
            float          EntryL = 0.0f, EntryR = 0.0f;
            const bool     HitL = IntersectSlabAabb(Cells_[LeftIdx].LowerBounds, Cells_[LeftIdx].UpperBounds, OriginMeters, InvDir, MinDistanceMeters, BestT, EntryL);
            const bool     HitR = IntersectSlabAabb(Cells_[RightIdx].LowerBounds, Cells_[RightIdx].UpperBounds, OriginMeters, InvDir, MinDistanceMeters, BestT, EntryR);
            if (HitL && HitR)
            {
                if (EntryL < EntryR)
                {
                    if (StackTop + 2u <= Stack.size())
                    {
                        Stack[StackTop++] = RightIdx;
                        Stack[StackTop++] = LeftIdx;
                    }
                }
                else
                {
                    if (StackTop + 2u <= Stack.size())
                    {
                        Stack[StackTop++] = LeftIdx;
                        Stack[StackTop++] = RightIdx;
                    }
                }
            }
            else if (HitL && StackTop + 1u <= Stack.size())
            {
                Stack[StackTop++] = LeftIdx;
            }
            else if (HitR && StackTop + 1u <= Stack.size())
            {
                Stack[StackTop++] = RightIdx;
            }
        }
        else
        {
            for (uint32_t Offset = 0u; Offset < Cell.RightOrCount; ++Offset)
            {
                const uint32_t            TriIdx = Cell.LeftOrFirst + Offset;
                const PackedRayTriangle&  Tri    = Triangles_[TriIdx];
                if (!IncludeGlass && Tri.TransmissiveGlass)
                {
                    continue;
                }
                float T = 0.0f, U = 0.0f, V = 0.0f;
                if (IntersectRayTriangleMoller(Tri, OriginMeters, DirectionUnit, MinDistanceMeters, BestT, T, U, V))
                {
                    BestT   = T;
                    BestU   = U;
                    BestV   = V;
                    BestIdx = TriIdx;
                }
            }
        }
    }

    if (BestIdx == UINT32_MAX)
    {
        return Result;
    }

    const PackedRayTriangle& HitTri = Triangles_[BestIdx];
    const float              W      = 1.0f - BestU - BestV;
    float                    Nx     = HitTri.Edge1[1] * HitTri.Edge2[2] - HitTri.Edge1[2] * HitTri.Edge2[1];
    float                    Ny     = HitTri.Edge1[2] * HitTri.Edge2[0] - HitTri.Edge1[0] * HitTri.Edge2[2];
    float                    Nz     = HitTri.Edge1[0] * HitTri.Edge2[1] - HitTri.Edge1[1] * HitTri.Edge2[0];
    const float              NLen   = std::sqrt(Nx * Nx + Ny * Ny + Nz * Nz);
    if (NLen > 1.0e-8f)
    {
        Nx /= NLen;
        Ny /= NLen;
        Nz /= NLen;
    }
    if (Nx * DirectionUnit[0] + Ny * DirectionUnit[1] + Nz * DirectionUnit[2] > 0.0f)
    {
        Nx = -Nx;
        Ny = -Ny;
        Nz = -Nz;
    }

    Result.Hit              = true;
    Result.DistanceMeters   = BestT;
    Result.TriangleIndex    = BestIdx;
    Result.ClassificationId = HitTri.ClassificationId;
    Result.WorldPosition    = {
        OriginMeters[0] + DirectionUnit[0] * BestT,
        OriginMeters[1] + DirectionUnit[1] * BestT,
        OriginMeters[2] + DirectionUnit[2] * BestT,
    };
    Result.GeometricNormal = {Nx, Ny, Nz};
    Result.TextureCoord    = {
        W * HitTri.Uv0U + BestU * HitTri.Uv1[0] + BestV * HitTri.Uv2[0],
        W * HitTri.Uv0V + BestU * HitTri.Uv1[1] + BestV * HitTri.Uv2[1],
    };
    return Result;
}

std::array<float, 3> BoundingVolumeAtlas::TraceShadowTransmittance(std::array<float, 3> OriginMeters,
                                                                   std::array<float, 3> DirectionUnit,
                                                                   float                MaxDistanceMeters) const noexcept
{
    if (Cells_.empty() || Triangles_.empty())
    {
        return {1.0f, 1.0f, 1.0f};
    }

    const std::array<float, 3> InvDir = {
        1.0f / (std::abs(DirectionUnit[0]) > 1.0e-8f ? DirectionUnit[0] : (DirectionUnit[0] >= 0.0f ? 1.0e-8f : -1.0e-8f)),
        1.0f / (std::abs(DirectionUnit[1]) > 1.0e-8f ? DirectionUnit[1] : (DirectionUnit[1] >= 0.0f ? 1.0e-8f : -1.0e-8f)),
        1.0f / (std::abs(DirectionUnit[2]) > 1.0e-8f ? DirectionUnit[2] : (DirectionUnit[2] >= 0.0f ? 1.0e-8f : -1.0e-8f)),
    };

    std::array<float, 3>     Trans{1.0f, 1.0f, 1.0f};
    std::array<uint32_t, 64> Stack{};
    uint32_t                 StackTop  = 0u;
    float                    RootEntry = 0.0f;
    if (IntersectSlabAabb(Cells_[0].LowerBounds, Cells_[0].UpperBounds, OriginMeters, InvDir, 1.5e-3f, MaxDistanceMeters, RootEntry))
    {
        Stack[StackTop++] = 0u;
    }

    while (StackTop > 0u)
    {
        const EnclosureCell& Cell = Cells_[Stack[--StackTop]];
        if ((Cell.RightOrCount & kInteriorFlagBit) != 0u)
        {
            const uint32_t LeftIdx  = Cell.LeftOrFirst;
            const uint32_t RightIdx = Cell.RightOrCount & ~kInteriorFlagBit;
            float          EntryL = 0.0f, EntryR = 0.0f;
            if (IntersectSlabAabb(Cells_[LeftIdx].LowerBounds, Cells_[LeftIdx].UpperBounds, OriginMeters, InvDir, 1.5e-3f, MaxDistanceMeters, EntryL) &&
                StackTop < Stack.size())
            {
                Stack[StackTop++] = LeftIdx;
            }
            if (IntersectSlabAabb(Cells_[RightIdx].LowerBounds, Cells_[RightIdx].UpperBounds, OriginMeters, InvDir, 1.5e-3f, MaxDistanceMeters, EntryR) &&
                StackTop < Stack.size())
            {
                Stack[StackTop++] = RightIdx;
            }
        }
        else
        {
            for (uint32_t Offset = 0u; Offset < Cell.RightOrCount; ++Offset)
            {
                const PackedRayTriangle& Tri = Triangles_[Cell.LeftOrFirst + Offset];
                float                    T = 0.0f, U = 0.0f, V = 0.0f;
                if (IntersectRayTriangleMoller(Tri, OriginMeters, DirectionUnit, 1.5e-3f, MaxDistanceMeters, T, U, V))
                {
                    if (!Tri.TransmissiveGlass)
                    {
                        return {0.0f, 0.0f, 0.0f};
                    }
                    const float F0 = std::pow((Tri.RefractiveIndex - 1.0f) / (Tri.RefractiveIndex + 1.0f), 2.0f);
                    const float Tr = (1.0f - F0);
                    Trans[0] *= Tr * std::exp(-Tri.AbsorptionPerM[0] * 0.015f);
                    Trans[1] *= Tr * std::exp(-Tri.AbsorptionPerM[1] * 0.015f);
                    Trans[2] *= Tr * std::exp(-Tri.AbsorptionPerM[2] * 0.015f);
                }
            }
        }
    }
    return Trans;
}

void BoundingVolumeAtlas::PackForGpu(std::vector<std::array<float, 4>>& OutCellFloat4Table,
                                     std::vector<std::array<float, 4>>& OutTriangleFloat4Table) const
{
    OutCellFloat4Table.clear();
    OutTriangleFloat4Table.clear();

    if (Cells_.empty() || Triangles_.empty())
    {
        OutCellFloat4Table.push_back({0.0f, 0.0f, 0.0f, 0.0f});
        OutCellFloat4Table.push_back({0.0f, 0.0f, 0.0f, 0.0f});
        OutTriangleFloat4Table.resize(5u, {0.0f, 0.0f, 0.0f, 0.0f});
        return;
    }

    OutCellFloat4Table.reserve(Cells_.size() * 2u);
    for (const EnclosureCell& C : Cells_)
    {
        OutCellFloat4Table.push_back({C.LowerBounds[0], C.LowerBounds[1], C.LowerBounds[2], std::bit_cast<float>(C.LeftOrFirst)});
        OutCellFloat4Table.push_back({C.UpperBounds[0], C.UpperBounds[1], C.UpperBounds[2], std::bit_cast<float>(C.RightOrCount)});
    }

    OutTriangleFloat4Table.reserve(Triangles_.size() * 5u);
    for (const PackedRayTriangle& T : Triangles_)
    {
        const uint32_t PackedClassAndGlass = (T.ClassificationId & 0x7FFFFFFFu) | (T.TransmissiveGlass ? 0x80000000u : 0u);
        OutTriangleFloat4Table.push_back({T.Vertex0[0], T.Vertex0[1], T.Vertex0[2], T.Uv0U});
        OutTriangleFloat4Table.push_back({T.Edge1[0], T.Edge1[1], T.Edge1[2], T.Uv0V});
        OutTriangleFloat4Table.push_back({T.Edge2[0], T.Edge2[1], T.Edge2[2], std::bit_cast<float>(PackedClassAndGlass)});
        OutTriangleFloat4Table.push_back({T.Uv1[0], T.Uv1[1], T.Uv2[0], T.Uv2[1]});
        OutTriangleFloat4Table.push_back({T.AbsorptionPerM[0], T.AbsorptionPerM[1], T.AbsorptionPerM[2], T.RefractiveIndex});
    }
}

} // namespace Frontier
