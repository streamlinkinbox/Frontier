/*==========================================================================================================================================
                                                              BAKEFIELD.CPP
==========================================================================================================================================*/
// 🧩 Flatten the bake BVH and low-surface coverage into the shared GPU layout, evaluate it on the CPU as the
//    reference for BakeFieldCompute, and encode evaluated fields into the 27 mesh maps.

#include "Engine/ContentInterchange/BakeField.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Frontier
{
namespace
{
// 📝 Vector arithmetic mirrors BakeMesh.cpp's translation-unit-local helpers operation-for-operation; the twin
// evaluation below is only a valid reference for the GPU shader (and for RunMeshBake) while these stay identical.
void Cross(const float A[3], const float B[3], float Out[3]) noexcept
{
    Out[0] = A[1] * B[2] - A[2] * B[1];
    Out[1] = A[2] * B[0] - A[0] * B[2];
    Out[2] = A[0] * B[1] - A[1] * B[0];
}
float Dot(const float A[3], const float B[3]) noexcept { return A[0] * B[0] + A[1] * B[1] + A[2] * B[2]; }
void Normalize(float V[3]) noexcept
{
    const float Length = std::sqrt(Dot(V, V));
    if (Length > 1e-20f) { V[0] /= Length; V[1] /= Length; V[2] /= Length; }
}
void Transform(const std::array<float, 16> &Matrix, const float In[3], float Out[3]) noexcept
{
    for (int Row = 0; Row < 3; ++Row)
        Out[Row] = Matrix[Row * 4 + 0] * In[0] + Matrix[Row * 4 + 1] * In[1] + Matrix[Row * 4 + 2] * In[2] + Matrix[Row * 4 + 3];
}
uint8_t ToByte(float Value) noexcept { return uint8_t(std::max(0.f, std::min(1.f, Value)) * 255.f + 0.5f); }

//------------------------------------------------------------------------------------------------------------------------
//                                                        FLAT BVH CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Staging triangle identical in contents to BakeTriangleBvh's private Triangle; slots are emitted from it
// in traversal order once the extent build has settled the ordering.
struct StagingTriangle
{
    float    A[3], B[3], C[3];
    float    NormalA[3], NormalB[3], NormalC[3];
    uint32_t PartIndex;
};

// 📝 The same bounded median-split build as BakeTriangleBvh::BuildRange (same bounds walk, same axis choice,
// same centroid sort, same Count/2 split), writing BakeExtentSlots directly. The split always halves by count,
// so extent depth stays under 64 for any triangle count that fits in memory — the traversal stack bound the
// CPU twin and BakeFieldCompute share.
uint32_t BuildExtentRange(const std::vector<StagingTriangle> &Triangles, std::vector<uint32_t> &Order,
    std::vector<BakeExtentSlot> &Extents, uint32_t Start, uint32_t Count)
{
    BakeExtentSlot Extent{};
    Extent.Lower[0] = Extent.Lower[1] = Extent.Lower[2] = 3.402823e38f;
    Extent.Upper[0] = Extent.Upper[1] = Extent.Upper[2] = -3.402823e38f;
    Extent.Lower[3] = Extent.Upper[3] = 0.f;
    for (uint32_t Index = 0; Index < Count; ++Index)
    {
        const auto &Triangle = Triangles[Order[Start + Index]];
        for (const float *Vertex : {Triangle.A, Triangle.B, Triangle.C})
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                Extent.Lower[Axis] = std::min(Extent.Lower[Axis], Vertex[Axis]);
                Extent.Upper[Axis] = std::max(Extent.Upper[Axis], Vertex[Axis]);
            }
    }
    const uint32_t ExtentIndex = uint32_t(Extents.size());
    Extents.push_back(Extent);
    constexpr uint32_t LeafTriangles = 4;
    if (Count <= LeafTriangles)
    {
        Extents[ExtentIndex].Index[0] = Start;
        Extents[ExtentIndex].Index[1] = Count;
        Extents[ExtentIndex].Index[2] = 1;
        Extents[ExtentIndex].Index[3] = 0;
        return ExtentIndex;
    }
    const float Span[3]{Extents[ExtentIndex].Upper[0] - Extents[ExtentIndex].Lower[0],
        Extents[ExtentIndex].Upper[1] - Extents[ExtentIndex].Lower[1], Extents[ExtentIndex].Upper[2] - Extents[ExtentIndex].Lower[2]};
    const int Axis = Span[0] > Span[1] ? (Span[0] > Span[2] ? 0 : 2) : (Span[1] > Span[2] ? 1 : 2);
    std::sort(Order.begin() + Start, Order.begin() + Start + Count, [&](uint32_t A, uint32_t B)
    {
        const auto Centroid = [&](uint32_t TriangleIndex)
        {
            const auto &Triangle = Triangles[TriangleIndex];
            return (Triangle.A[Axis] + Triangle.B[Axis] + Triangle.C[Axis]) / 3.f;
        };
        return Centroid(A) < Centroid(B);
    });
    const uint32_t Mid = Count / 2;
    const uint32_t Left = BuildExtentRange(Triangles, Order, Extents, Start, Mid);
    const uint32_t Right = BuildExtentRange(Triangles, Order, Extents, Start + Mid, Count - Mid);
    Extents[ExtentIndex].Index[0] = Left;
    Extents[ExtentIndex].Index[1] = Right;
    Extents[ExtentIndex].Index[2] = 0;
    Extents[ExtentIndex].Index[3] = 0;
    return ExtentIndex;
}

BakeFieldTriangles BuildFieldTriangles(const BakeMeshSet &High)
{
    BakeFieldTriangles Result;
    // 📝 Bounds and epsilon exactly as RunMeshBake derives them: every high vertex (indexed or not), raw
    // positions with no part transform, epsilon = max(diagonal * 1e-5, 1e-7).
    float BoundsMinimum[3]{3.402823e38f, 3.402823e38f, 3.402823e38f}, BoundsMaximum[3]{-3.402823e38f, -3.402823e38f, -3.402823e38f};
    for (const auto &Part : High.Parts)
        for (const auto &Vertex : Part.Vertices)
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                BoundsMinimum[Axis] = std::min(BoundsMinimum[Axis], Vertex.Position[Axis]);
                BoundsMaximum[Axis] = std::max(BoundsMaximum[Axis], Vertex.Position[Axis]);
            }
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        Result.BoundsMinimum[Axis] = BoundsMinimum[Axis];
        Result.Diagonal[Axis] = BoundsMaximum[Axis] - BoundsMinimum[Axis];
    }
    Result.Epsilon = std::max(std::sqrt(Dot(Result.Diagonal, Result.Diagonal)) * 1e-5f, 1e-7f);

    // 📝 Triangle gathering identical to BakeTriangleBvh::Build (part order, index order, raw positions).
    std::vector<StagingTriangle> Staging;
    for (uint32_t PartIndex = 0; PartIndex < High.Parts.size(); ++PartIndex)
    {
        const auto &Part = High.Parts[PartIndex];
        for (size_t Index = 0; Index + 2 < Part.Indices.size(); Index += 3)
        {
            const auto &VA = Part.Vertices[Part.Indices[Index + 0]];
            const auto &VB = Part.Vertices[Part.Indices[Index + 1]];
            const auto &VC = Part.Vertices[Part.Indices[Index + 2]];
            StagingTriangle Triangle{};
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                Triangle.A[Axis] = VA.Position[Axis];
                Triangle.B[Axis] = VB.Position[Axis];
                Triangle.C[Axis] = VC.Position[Axis];
                Triangle.NormalA[Axis] = VA.Normal[Axis];
                Triangle.NormalB[Axis] = VB.Normal[Axis];
                Triangle.NormalC[Axis] = VC.Normal[Axis];
            }
            Triangle.PartIndex = PartIndex;
            Staging.push_back(Triangle);
        }
    }
    std::vector<uint32_t> Order(Staging.size());
    for (uint32_t Index = 0; Index < Order.size(); ++Index) Order[Index] = Index;
    if (!Staging.empty()) BuildExtentRange(Staging, Order, Result.Extents, 0, uint32_t(Order.size()));

    // 📝 Emit slots in traversal order so a leaf's [Index[0], Index[0] + Index[1]) range addresses this array
    // directly, with no order indirection buffer on the GPU.
    Result.Triangles.reserve(Staging.size());
    for (const uint32_t Source : Order)
    {
        const auto &Triangle = Staging[Source];
        BakeTriangleSlot Slot{};
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            Slot.A[Axis] = Triangle.A[Axis];
            Slot.B[Axis] = Triangle.B[Axis];
            Slot.C[Axis] = Triangle.C[Axis];
            Slot.NormalA[Axis] = Triangle.NormalA[Axis];
            Slot.NormalB[Axis] = Triangle.NormalB[Axis];
            Slot.NormalC[Axis] = Triangle.NormalC[Axis];
        }
        Slot.A[3] = float(Triangle.PartIndex);
        Slot.B[3] = Slot.C[3] = Slot.NormalA[3] = Slot.NormalB[3] = Slot.NormalC[3] = 0.f;
        Result.Triangles.push_back(Slot);
    }
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        UV COVERAGE RASTERIZATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Rasterizes one target part's UV layout into per-texel low surfaces. Triangles are visited in index order
// and only still-uncovered texels are filled, with the same barycentric test and tolerance RunMeshBake applies
// texel-by-texel — so the winning triangle per texel is the same one the legacy scan finds, at bounding-box
// cost instead of texels x triangles cost.
void RasterizeTarget(const BakePart &Part, uint32_t Resolution, bool NeedWireframe, const BakeFieldTriangles &Triangles,
    BakeFieldTarget &Target)
{
    const size_t TexelCount = size_t(Resolution) * Resolution;
    Target.Surfaces.assign(TexelCount, BakeTexelSurface{{0, 0, 0, 0}, {0, 0, 1, 0}});
    BakeEncodeContext &Encode = Target.Encode;
    Encode.Name = Part.Name;
    Encode.Resolution = Resolution;
    Encode.ObjectToWorld = Part.ObjectToWorld;
    Encode.ChartIds.assign(TexelCount, 0);
    if (NeedWireframe) Encode.Wireframe.assign(TexelCount, BakeWireframeTexel{});
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        Encode.BoundsMinimum[Axis] = Triangles.BoundsMinimum[Axis];
        Encode.Diagonal[Axis] = Triangles.Diagonal[Axis];
    }
    uint32_t ChartMaximum = 0;
    for (const auto &Vertex : Part.Vertices) ChartMaximum = std::max(ChartMaximum, Vertex.ChartId);
    Encode.Charts = ChartMaximum;

    for (size_t Index = 0; Index + 2 < Part.Indices.size(); Index += 3)
    {
        const auto &A = Part.Vertices[Part.Indices[Index + 0]];
        const auto &B = Part.Vertices[Part.Indices[Index + 1]];
        const auto &C = Part.Vertices[Part.Indices[Index + 2]];
        const float MinimumU = std::min({A.Uv[0], B.Uv[0], C.Uv[0]}), MaximumU = std::max({A.Uv[0], B.Uv[0], C.Uv[0]});
        const float MinimumV = std::min({A.Uv[1], B.Uv[1], C.Uv[1]}), MaximumV = std::max({A.Uv[1], B.Uv[1], C.Uv[1]});
        // 📝 Texel range covering the UV bounds, widened by one texel on every side so the legacy -1e-4
        // barycentric tolerance (which accepts texels a hair outside the triangle) sees the same candidates.
        const int32_t X0 = std::max(0, int32_t(std::floor(MinimumU * float(Resolution) - 0.5f)) - 1);
        const int32_t X1 = std::min(int32_t(Resolution) - 1, int32_t(std::ceil(MaximumU * float(Resolution) - 0.5f)) + 1);
        const int32_t Y0 = std::max(0, int32_t(std::floor((1.f - MaximumV) * float(Resolution) - 0.5f)) - 1);
        const int32_t Y1 = std::min(int32_t(Resolution) - 1, int32_t(std::ceil((1.f - MinimumV) * float(Resolution) - 0.5f)) + 1);
        const float Denominator = (B.Uv[1] - C.Uv[1]) * (A.Uv[0] - C.Uv[0]) + (C.Uv[0] - B.Uv[0]) * (A.Uv[1] - C.Uv[1]);
        if (std::fabs(Denominator) < 1e-12f) continue;
        for (int32_t Y = Y0; Y <= Y1; ++Y)
            for (int32_t X = X0; X <= X1; ++X)
            {
                const size_t Texel = size_t(Y) * Resolution + size_t(X);
                if (Encode.ChartIds[Texel] != 0) continue;
                const float PixelU = (float(X) + 0.5f) / float(Resolution);
                const float PixelV = 1.f - (float(Y) + 0.5f) / float(Resolution); // texture v grows upward
                const float WeightA = ((B.Uv[1] - C.Uv[1]) * (PixelU - C.Uv[0]) + (C.Uv[0] - B.Uv[0]) * (PixelV - C.Uv[1])) / Denominator;
                const float WeightB = ((C.Uv[1] - A.Uv[1]) * (PixelU - C.Uv[0]) + (A.Uv[0] - C.Uv[0]) * (PixelV - C.Uv[1])) / Denominator;
                const float WeightC = 1.f - WeightA - WeightB;
                if (WeightA < -1e-4f || WeightB < -1e-4f || WeightC < -1e-4f) continue;
                float Position[3], Normal[3];
                for (int Axis = 0; Axis < 3; ++Axis)
                    Position[Axis] = WeightA * A.Position[Axis] + WeightB * B.Position[Axis] + WeightC * C.Position[Axis];
                for (int Axis = 0; Axis < 3; ++Axis)
                    Normal[Axis] = WeightA * A.Normal[Axis] + WeightB * B.Normal[Axis] + WeightC * C.Normal[Axis];
                Normalize(Normal);
                float WorldPosition[3];
                Transform(Part.ObjectToWorld, Position, WorldPosition);
                // 📝 RunMeshBake normalizes the interpolated normal twice (once in its barycentric lerp, once
                // after copying it as the world normal) and never transforms it by ObjectToWorld; both are
                // preserved verbatim so stored surfaces match the legacy bake bit-for-bit.
                float WorldNormal[3]{Normal[0], Normal[1], Normal[2]};
                Normalize(WorldNormal);
                BakeTexelSurface &Surface = Target.Surfaces[Texel];
                Surface.WorldPosition[0] = WorldPosition[0];
                Surface.WorldPosition[1] = WorldPosition[1];
                Surface.WorldPosition[2] = WorldPosition[2];
                Surface.WorldPosition[3] = 0.f;
                Surface.WorldNormal[0] = WorldNormal[0];
                Surface.WorldNormal[1] = WorldNormal[1];
                Surface.WorldNormal[2] = WorldNormal[2];
                Surface.WorldNormal[3] = 1.f;
                Encode.ChartIds[Texel] = A.ChartId + 1;
                if (NeedWireframe)
                {
                    BakeWireframeTexel &Wireframe = Encode.Wireframe[Texel];
                    Wireframe.Weights[0] = WeightA;
                    Wireframe.Weights[1] = WeightB;
                    Wireframe.Weights[2] = WeightC;
                    Wireframe.WinningUv[0] = A.Uv[0];
                    Wireframe.WinningUv[1] = A.Uv[1];
                    Wireframe.WinningUv[2] = B.Uv[0];
                    Wireframe.WinningUv[3] = B.Uv[1];
                    Wireframe.WinningUv[4] = C.Uv[0];
                    Wireframe.WinningUv[5] = C.Uv[1];
                }
            }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        FLAT BVH QUERIES (CPU TWIN)
//------------------------------------------------------------------------------------------------------------------------

// 📝 The queries below are BakeMesh.cpp's BakeTriangleBvh::CastRay/ClosestPoint re-expressed over the flat
// slots with a fixed 64-deep explicit stack — the exact form BakeFieldCompute runs, including child push
// order, tie handling and tolerances. Depth 64 is unreachable: the build halves triangle count per extent.
constexpr uint32_t FlatStackDepth = 64;

bool RayBox(const float Origin[3], const float InvDirection[3], const float Minimum[3], const float Maximum[3], float MinDistance,
    float MaxDistance) noexcept
{
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        float T0 = (Minimum[Axis] - Origin[Axis]) * InvDirection[Axis];
        float T1 = (Maximum[Axis] - Origin[Axis]) * InvDirection[Axis];
        if (T0 > T1) std::swap(T0, T1);
        MinDistance = std::max(MinDistance, T0);
        MaxDistance = std::min(MaxDistance, T1);
        if (MaxDistance < MinDistance) return false;
    }
    return true;
}
// 📝 Möller–Trumbore ray-triangle intersection, identical to BakeMesh.cpp's (slot rows are float4, xyz leads).
bool RayTriangle(const float Origin[3], const float Direction[3], const float A[3], const float B[3], const float C[3],
    float MinDistance, float MaxDistance, float &OutDistance, float OutBarycentric[3]) noexcept
{
    const float Edge1[3]{B[0] - A[0], B[1] - A[1], B[2] - A[2]};
    const float Edge2[3]{C[0] - A[0], C[1] - A[1], C[2] - A[2]};
    float PVector[3];
    Cross(Direction, Edge2, PVector);
    const float Determinant = Dot(Edge1, PVector);
    if (std::fabs(Determinant) < 1e-12f) return false;
    const float InverseDeterminant = 1.f / Determinant;
    const float TVector[3]{Origin[0] - A[0], Origin[1] - A[1], Origin[2] - A[2]};
    const float U = Dot(TVector, PVector) * InverseDeterminant;
    if (U < -1e-5f || U > 1.f + 1e-5f) return false;
    float QVector[3];
    Cross(TVector, Edge1, QVector);
    const float V = Dot(Direction, QVector) * InverseDeterminant;
    if (V < -1e-5f || U + V > 1.f + 1e-5f) return false;
    const float Distance = Dot(Edge2, QVector) * InverseDeterminant;
    if (Distance < MinDistance || Distance > MaxDistance) return false;
    OutDistance = Distance;
    OutBarycentric[0] = 1.f - U - V;
    OutBarycentric[1] = U;
    OutBarycentric[2] = V;
    return true;
}

struct FlatHit
{
    bool     Found = false;
    float    Distance = 0;
    float    Normal[3]{0, 0, 0};
    float    Position[3]{0, 0, 0};
    uint32_t PartIndex = 0;
};

FlatHit CastRayFlat(const std::vector<BakeTriangleSlot> &Triangles, const std::vector<BakeExtentSlot> &Extents,
    const float Origin[3], const float Direction[3], float MinDistance, float MaxDistance)
{
    FlatHit Result;
    if (Extents.empty()) return Result;
    const float InvDirection[3]{1.f / Direction[0], 1.f / Direction[1], 1.f / Direction[2]};
    float BestDistance = MaxDistance;
    uint32_t Stack[FlatStackDepth];
    uint32_t Top = 0;
    Stack[Top++] = 0;
    while (Top > 0)
    {
        const BakeExtentSlot &Extent = Extents[Stack[--Top]];
        if (!RayBox(Origin, InvDirection, Extent.Lower, Extent.Upper, MinDistance, BestDistance)) continue;
        if (Extent.Index[2] != 0)
        {
            for (uint32_t Index = 0; Index < Extent.Index[1]; ++Index)
            {
                const BakeTriangleSlot &Triangle = Triangles[Extent.Index[0] + Index];
                float Distance, Barycentric[3];
                if (RayTriangle(Origin, Direction, Triangle.A, Triangle.B, Triangle.C, MinDistance, BestDistance, Distance,
                        Barycentric))
                {
                    BestDistance = Distance;
                    Result.Found = true;
                    Result.Distance = Distance;
                    Result.PartIndex = uint32_t(Triangle.A[3]);
                    for (int Axis = 0; Axis < 3; ++Axis)
                    {
                        Result.Position[Axis] = Origin[Axis] + Direction[Axis] * Distance;
                        Result.Normal[Axis] = Barycentric[0] * Triangle.NormalA[Axis] + Barycentric[1] * Triangle.NormalB[Axis] +
                            Barycentric[2] * Triangle.NormalC[Axis];
                    }
                    Normalize(Result.Normal);
                }
            }
        }
        else
        {
            if (Top < FlatStackDepth) Stack[Top++] = Extent.Index[0];
            if (Top < FlatStackDepth) Stack[Top++] = Extent.Index[1];
        }
    }
    return Result;
}

float BoxDistanceSquared(const float Point[3], const float Minimum[3], const float Maximum[3]) noexcept
{
    float Sum = 0;
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        const float Value = Point[Axis];
        if (Value < Minimum[Axis]) Sum += (Minimum[Axis] - Value) * (Minimum[Axis] - Value);
        else if (Value > Maximum[Axis]) Sum += (Value - Maximum[Axis]) * (Value - Maximum[Axis]);
    }
    return Sum;
}
// 📝 Ericson 5.1.5 nearest point on triangle, identical to BakeMesh.cpp's region walk.
void ClosestPointOnTriangle(const float Point[3], const float A[3], const float B[3], const float C[3], float Out[3]) noexcept
{
    const float Ab[3]{B[0] - A[0], B[1] - A[1], B[2] - A[2]};
    const float Ac[3]{C[0] - A[0], C[1] - A[1], C[2] - A[2]};
    const float Ap[3]{Point[0] - A[0], Point[1] - A[1], Point[2] - A[2]};
    const float D1 = Dot(Ab, Ap), D2 = Dot(Ac, Ap);
    if (D1 <= 0 && D2 <= 0) { Out[0] = A[0]; Out[1] = A[1]; Out[2] = A[2]; return; }
    const float Bp[3]{Point[0] - B[0], Point[1] - B[1], Point[2] - B[2]};
    const float D3 = Dot(Ab, Bp), D4 = Dot(Ac, Bp);
    if (D3 >= 0 && D4 <= D3) { Out[0] = B[0]; Out[1] = B[1]; Out[2] = B[2]; return; }
    const float Vc = D1 * D4 - D3 * D2;
    if (Vc <= 0 && D1 >= 0 && D3 <= 0)
    {
        const float V = D1 / (D1 - D3);
        for (int Axis = 0; Axis < 3; ++Axis) Out[Axis] = A[Axis] + Ab[Axis] * V;
        return;
    }
    const float Cp[3]{Point[0] - C[0], Point[1] - C[1], Point[2] - C[2]};
    const float D5 = Dot(Ab, Cp), D6 = Dot(Ac, Cp);
    if (D6 >= 0 && D5 <= D6) { Out[0] = C[0]; Out[1] = C[1]; Out[2] = C[2]; return; }
    const float Vb = D5 * D2 - D1 * D6;
    if (Vb <= 0 && D2 >= 0 && D6 <= 0)
    {
        const float W = D2 / (D2 - D6);
        for (int Axis = 0; Axis < 3; ++Axis) Out[Axis] = A[Axis] + Ac[Axis] * W;
        return;
    }
    const float Va = D3 * D6 - D5 * D4;
    if (Va <= 0 && (D4 - D3) >= 0 && (D5 - D6) >= 0)
    {
        const float W = (D4 - D3) / ((D4 - D3) + (D5 - D6));
        for (int Axis = 0; Axis < 3; ++Axis) Out[Axis] = B[Axis] + (C[Axis] - B[Axis]) * W;
        return;
    }
    const float Denominator = 1.f / (Va + Vb + Vc);
    const float V = Vb * Denominator, W = Vc * Denominator;
    for (int Axis = 0; Axis < 3; ++Axis) Out[Axis] = A[Axis] + Ab[Axis] * V + Ac[Axis] * W;
}
void PlaneBarycentric(const float Point[3], const float A[3], const float B[3], const float C[3], float Weights[3]) noexcept
{
    const float V0[3]{B[0] - A[0], B[1] - A[1], B[2] - A[2]};
    const float V1[3]{C[0] - A[0], C[1] - A[1], C[2] - A[2]};
    const float V2[3]{Point[0] - A[0], Point[1] - A[1], Point[2] - A[2]};
    const float D00 = Dot(V0, V0), D01 = Dot(V0, V1), D11 = Dot(V1, V1), D20 = Dot(V2, V0), D21 = Dot(V2, V1);
    const float Denominator = D00 * D11 - D01 * D01;
    if (std::fabs(Denominator) < 1e-20f) { Weights[0] = 1; Weights[1] = Weights[2] = 0; return; }
    const float V = (D11 * D20 - D01 * D21) / Denominator;
    const float W = (D00 * D21 - D01 * D20) / Denominator;
    Weights[0] = 1.f - V - W;
    Weights[1] = V;
    Weights[2] = W;
}

struct FlatClosest
{
    bool     Found = false;
    float    Distance = 0;
    float    Point[3]{0, 0, 0};
    float    Normal[3]{0, 0, 0};
    uint32_t PartIndex = 0;
};

FlatClosest ClosestPointFlat(const std::vector<BakeTriangleSlot> &Triangles, const std::vector<BakeExtentSlot> &Extents,
    const float Point[3], float MaxDistance)
{
    FlatClosest Result;
    if (Extents.empty()) return Result;
    float BestDistanceSquared = MaxDistance * MaxDistance;
    uint32_t Stack[FlatStackDepth];
    uint32_t Top = 0;
    Stack[Top++] = 0;
    while (Top > 0)
    {
        const BakeExtentSlot &Extent = Extents[Stack[--Top]];
        if (BoxDistanceSquared(Point, Extent.Lower, Extent.Upper) > BestDistanceSquared) continue;
        if (Extent.Index[2] != 0)
        {
            for (uint32_t Index = 0; Index < Extent.Index[1]; ++Index)
            {
                const BakeTriangleSlot &Triangle = Triangles[Extent.Index[0] + Index];
                float Closest[3];
                ClosestPointOnTriangle(Point, Triangle.A, Triangle.B, Triangle.C, Closest);
                const float Delta[3]{Closest[0] - Point[0], Closest[1] - Point[1], Closest[2] - Point[2]};
                const float DistanceSquared = Dot(Delta, Delta);
                if (DistanceSquared < BestDistanceSquared)
                {
                    BestDistanceSquared = DistanceSquared;
                    Result.Found = true;
                    Result.Distance = std::sqrt(DistanceSquared);
                    Result.PartIndex = uint32_t(Triangle.A[3]);
                    for (int Axis = 0; Axis < 3; ++Axis) Result.Point[Axis] = Closest[Axis];
                    float Weights[3];
                    PlaneBarycentric(Closest, Triangle.A, Triangle.B, Triangle.C, Weights);
                    for (int Axis = 0; Axis < 3; ++Axis)
                        Result.Normal[Axis] = Weights[0] * Triangle.NormalA[Axis] + Weights[1] * Triangle.NormalB[Axis] +
                            Weights[2] * Triangle.NormalC[Axis];
                    Normalize(Result.Normal);
                }
            }
        }
        else
        {
            const float LeftDistance = BoxDistanceSquared(Point, Extents[Extent.Index[0]].Lower, Extents[Extent.Index[0]].Upper);
            const float RightDistance = BoxDistanceSquared(Point, Extents[Extent.Index[1]].Lower, Extents[Extent.Index[1]].Upper);
            // 📝 Farther sub-extent pushed first so the nearer is visited first, as BakeMesh.cpp's ClosestPoint.
            if (LeftDistance <= RightDistance)
            {
                if (Top < FlatStackDepth) Stack[Top++] = Extent.Index[1];
                if (Top < FlatStackDepth) Stack[Top++] = Extent.Index[0];
            }
            else
            {
                if (Top < FlatStackDepth) Stack[Top++] = Extent.Index[0];
                if (Top < FlatStackDepth) Stack[Top++] = Extent.Index[1];
            }
        }
    }
    return Result;
}

// 📝 1:1 with BakeMesh.cpp's Hemisphere: deterministic tangent frame around a hit normal.
void Hemisphere(const float Normal[3], float Tangent[3], float Bitangent[3]) noexcept
{
    const float ZReference[3]{0, 0, 1}, YReference[3]{0, 1, 0};
    Cross(std::fabs(Normal[2]) < 0.9f ? ZReference : YReference, Normal, Tangent);
    Normalize(Tangent);
    Cross(Normal, Tangent, Bitangent);
}
// 📝 1:1 with BakeMesh.cpp's AverageNormal over the flat queries.
void AverageNormalFlat(const std::vector<BakeTriangleSlot> &Triangles, const std::vector<BakeExtentSlot> &Extents,
    const float Point[3], const float Normal[3], const float Facing[3], float Radius, uint32_t Samples, float Epsilon,
    float Out[3]) noexcept
{
    if (Radius <= Epsilon) { Out[0] = Normal[0]; Out[1] = Normal[1]; Out[2] = Normal[2]; return; }
    float Tangent[3], Bitangent[3];
    Hemisphere(Normal, Tangent, Bitangent);
    float Sum[3]{Normal[0] * 2.f, Normal[1] * 2.f, Normal[2] * 2.f};
    float Weight = 2.f;
    const float SearchRadius = Radius * 1.8f;
    for (uint32_t K = 0; K < Samples; ++K)
    {
        const float Angle = (float(K) / float(std::max(1u, Samples))) * 6.283185307179586f;
        const float Query[3]{Point[0] + Tangent[0] * std::cos(Angle) * Radius + Bitangent[0] * std::sin(Angle) * Radius - Normal[0] * Radius * 0.3f,
            Point[1] + Tangent[1] * std::cos(Angle) * Radius + Bitangent[1] * std::sin(Angle) * Radius - Normal[1] * Radius * 0.3f,
            Point[2] + Tangent[2] * std::cos(Angle) * Radius + Bitangent[2] * std::sin(Angle) * Radius - Normal[2] * Radius * 0.3f};
        const auto Near = ClosestPointFlat(Triangles, Extents, Query, SearchRadius);
        if (!Near.Found || Near.Distance > SearchRadius) continue;
        if (Dot(Near.Normal, Normal) < -0.1f || Dot(Near.Normal, Facing) < -1e-6f) continue;
        const float SampleWeight = 1.f - std::clamp(Near.Distance / SearchRadius, 0.f, 1.f);
        Sum[0] += Near.Normal[0] * SampleWeight; Sum[1] += Near.Normal[1] * SampleWeight; Sum[2] += Near.Normal[2] * SampleWeight;
        Weight += SampleWeight;
    }
    Out[0] = Sum[0] / Weight; Out[1] = Sum[1] / Weight; Out[2] = Sum[2] / Weight;
    Normalize(Out);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        PER-TEXEL FIELD EVALUATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 One texel of RunMeshBake's shared-query body (primary ray, hemisphere sampling, curvature probes,
// thickness ray, bevel average) writing BakeTexelField lanes instead of locals. BakeFieldCompute mirrors this
// function statement-for-statement; keep the two in lockstep.
BakeTexelField EvaluateTexel(const std::vector<BakeTriangleSlot> &Triangles, const std::vector<BakeExtentSlot> &Extents,
    const BakeFieldUniforms &Uniforms, const BakeTexelSurface &Surface)
{
    BakeTexelField Field{};
    const float WorldPosition[3]{Surface.WorldPosition[0], Surface.WorldPosition[1], Surface.WorldPosition[2]};
    const float WorldNormal[3]{Surface.WorldNormal[0], Surface.WorldNormal[1], Surface.WorldNormal[2]};
    const bool Covered = Surface.WorldNormal[3] > 0.5f;

    bool HasHit = false;
    FlatHit Hit;
    if (Covered)
    {
        const float EnvelopeLength = Uniforms.Back + Uniforms.Front;
        const float RayOrigin[3]{WorldPosition[0] - WorldNormal[0] * Uniforms.Back, WorldPosition[1] - WorldNormal[1] * Uniforms.Back,
            WorldPosition[2] - WorldNormal[2] * Uniforms.Back};
        Hit = CastRayFlat(Triangles, Extents, RayOrigin, WorldNormal, 0.f, EnvelopeLength);
        HasHit = Hit.Found;
    }
    // 📝 "NH"/"Q" as in RunMeshBake: hit values on a hit, low-surface values on a miss.
    const float *NH = HasHit ? Hit.Normal : WorldNormal;
    const float *Q = HasHit ? Hit.Position : WorldPosition;

    Field.HitPosition[0] = Hit.Position[0];
    Field.HitPosition[1] = Hit.Position[1];
    Field.HitPosition[2] = Hit.Position[2];
    Field.HitPosition[3] = HasHit ? 1.f : 0.f;
    Field.HitNormal[0] = Hit.Normal[0];
    Field.HitNormal[1] = Hit.Normal[1];
    Field.HitNormal[2] = Hit.Normal[2];
    Field.HitNormal[3] = float(Hit.PartIndex);
    Field.Bent[0] = NH[0];
    Field.Bent[1] = NH[1];
    Field.Bent[2] = NH[2];
    Field.Bent[3] = 1.f; // Visibility until hemisphere sampling says otherwise
    Field.Bevel[0] = NH[0];
    Field.Bevel[1] = NH[1];
    Field.Bevel[2] = NH[2];
    Field.Bevel[3] = 0.f;
    Field.Scalars[0] = 1.f; // SecondaryVisibility
    Field.Scalars[1] = 0.f; // SignedCurvature
    Field.Scalars[2] = 0.f; // ThicknessDistance
    Field.Scalars[3] = 0.f; // ThicknessFound

    const float Epsilon = Uniforms.Epsilon;
    if (HasHit && (Uniforms.NeedMask & BakeFieldNeedHemisphere))
    {
        float Tangent[3], Bitangent[3];
        Hemisphere(Hit.Normal, Tangent, Bitangent);
        const float Origin[3]{Hit.Position[0] + Hit.Normal[0] * Epsilon * 3, Hit.Position[1] + Hit.Normal[1] * Epsilon * 3,
            Hit.Position[2] + Hit.Normal[2] * Epsilon * 3};
        const uint32_t Samples = std::max(1u, Uniforms.Samples);
        const float SampleDistance = (Uniforms.NeedMask & BakeFieldNeedSecondary)
            ? std::max(Uniforms.AoDistance, Uniforms.SecondaryAoDistance) : Uniforms.AoDistance;
        uint32_t Blocked = 0, SecondaryBlocked = 0;
        float BentSum[3]{0, 0, 0};
        const float Seed = std::fmod(Hit.Position[0] * 0.754877666f + Hit.Position[1] * 0.569840296f +
            Hit.Position[2] * 0.438579021f, 1.f);
        for (uint32_t S = 0; S < Samples; ++S)
        {
            const float U = (float(S) + 0.5f) / float(Samples);
            const float Z = std::sqrt(std::max(0.f, 1.f - U)), R = std::sqrt(U);
            const float Angle = float(S) * 2.399963229728653f + Seed * 6.283185307179586f;
            const float Direction[3]{Hit.Normal[0] * Z + Tangent[0] * std::cos(Angle) * R + Bitangent[0] * std::sin(Angle) * R,
                Hit.Normal[1] * Z + Tangent[1] * std::cos(Angle) * R + Bitangent[1] * std::sin(Angle) * R,
                Hit.Normal[2] * Z + Tangent[2] * std::cos(Angle) * R + Bitangent[2] * std::sin(Angle) * R};
            const auto Obstruction = CastRayFlat(Triangles, Extents, Origin, Direction, Epsilon, SampleDistance);
            const bool Occluded = Obstruction.Found && Obstruction.Distance <= Uniforms.AoDistance;
            if (Occluded) ++Blocked;
            else { BentSum[0] += Direction[0]; BentSum[1] += Direction[1]; BentSum[2] += Direction[2]; }
            if (Obstruction.Found && Obstruction.Distance <= Uniforms.SecondaryAoDistance) ++SecondaryBlocked;
        }
        Field.Bent[3] = 1.f - float(Blocked) / float(Samples);
        Field.Scalars[0] = 1.f - float(SecondaryBlocked) / float(Samples);
        float Bent[3]{NH[0], NH[1], NH[2]};
        if (Dot(BentSum, BentSum) < 1e-12f) { Bent[0] = Hit.Normal[0]; Bent[1] = Hit.Normal[1]; Bent[2] = Hit.Normal[2]; }
        else { Bent[0] = BentSum[0]; Bent[1] = BentSum[1]; Bent[2] = BentSum[2]; Normalize(Bent); }
        Field.Bent[0] = Bent[0];
        Field.Bent[1] = Bent[1];
        Field.Bent[2] = Bent[2];
    }
    if (HasHit && (Uniforms.NeedMask & BakeFieldNeedCurvature))
    {
        float Tangent[3], Bitangent[3];
        Hemisphere(Hit.Normal, Tangent, Bitangent);
        float Sum = 0.f;
        uint32_t Count = 0;
        for (int S = 0; S < 4; ++S)
        {
            const float Angle = float(S) * 1.5707963267948966f;
            const float Direction[3]{Tangent[0] * std::cos(Angle) + Bitangent[0] * std::sin(Angle),
                Tangent[1] * std::cos(Angle) + Bitangent[1] * std::sin(Angle), Tangent[2] * std::cos(Angle) + Bitangent[2] * std::sin(Angle)};
            const float Query[3]{Hit.Position[0] + Direction[0] * Uniforms.CurvatureRadius,
                Hit.Position[1] + Direction[1] * Uniforms.CurvatureRadius, Hit.Position[2] + Direction[2] * Uniforms.CurvatureRadius};
            const auto Near = ClosestPointFlat(Triangles, Extents, Query, Uniforms.CurvatureRadius * 2.f);
            if (Near.Found)
            {
                const float Delta[3]{Near.Normal[0] - Hit.Normal[0], Near.Normal[1] - Hit.Normal[1], Near.Normal[2] - Hit.Normal[2]};
                Sum += Dot(Delta, Direction);
                ++Count;
            }
        }
        Field.Scalars[1] = Count ? Sum / float(Count) : 0.f;
    }
    if (HasHit && (Uniforms.NeedMask & BakeFieldNeedThickness))
    {
        const float InwardOrigin[3]{Q[0] - Hit.Normal[0] * Epsilon * 3, Q[1] - Hit.Normal[1] * Epsilon * 3, Q[2] - Hit.Normal[2] * Epsilon * 3};
        const float InwardDirection[3]{-Hit.Normal[0], -Hit.Normal[1], -Hit.Normal[2]};
        const auto Inward = CastRayFlat(Triangles, Extents, InwardOrigin, InwardDirection, Epsilon, 3.402823e38f);
        Field.Scalars[2] = Inward.Found ? Inward.Distance : 0.f;
        Field.Scalars[3] = Inward.Found ? 1.f : 0.f;
    }
    if (HasHit && (Uniforms.NeedMask & BakeFieldNeedBevel))
    {
        float Averaged[3];
        AverageNormalFlat(Triangles, Extents, Q, Hit.Normal, WorldNormal, Uniforms.BevelRadius,
            std::max(1u, Uniforms.BevelSamples), Epsilon, Averaged);
        const float InverseStrength = 1.f - std::clamp(Uniforms.BevelStrength, 0.f, 1.f);
        float BevelNormal[3]{Averaged[0] * Uniforms.BevelStrength + Hit.Normal[0] * InverseStrength,
            Averaged[1] * Uniforms.BevelStrength + Hit.Normal[1] * InverseStrength,
            Averaged[2] * Uniforms.BevelStrength + Hit.Normal[2] * InverseStrength};
        Normalize(BevelNormal);
        Field.Bevel[0] = BevelNormal[0];
        Field.Bevel[1] = BevelNormal[1];
        Field.Bevel[2] = BevelNormal[2];
        Field.Bevel[3] = std::clamp((1.f - Dot(Hit.Normal, BevelNormal)) * 8.f, 0.f, 1.f);
    }
    return Field;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        ENCODER ARITHMETIC (LEGACY MIRRORS)
//------------------------------------------------------------------------------------------------------------------------

void TangentBasis(const float Normal[3], float Tangent[3], float Bitangent[3]) noexcept
{
    const float UpReference[3]{0, 1, 0}, SideReference[3]{1, 0, 0};
    const bool UseUp = std::fabs(Normal[1]) < 0.99f;
    Cross(UseUp ? UpReference : SideReference, Normal, Tangent);
    Normalize(Tangent);
    Cross(Normal, Tangent, Bitangent);
    Normalize(Bitangent);
}
// 📝 Plain-transpose local normal, 1:1 with BakeMesh.cpp's TransformLocalNormal (exact for rotation-only transforms).
void TransformLocalNormal(const std::array<float, 16> &ObjectToWorld, const float World[3], float Out[3]) noexcept
{
    for (int Row = 0; Row < 3; ++Row)
        Out[Row] = ObjectToWorld[0 * 4 + Row] * World[0] + ObjectToWorld[1 * 4 + Row] * World[1] + ObjectToWorld[2 * 4 + Row] * World[2];
    Normalize(Out);
}
void UvIslandColor(uint32_t Id, uint8_t Out[3]) noexcept
{
    const uint32_t Code = (Id * 0x9e3779u) & 0xffffffu;
    Out[0] = uint8_t(Code & 255);
    Out[1] = uint8_t((Code >> 8) & 255);
    Out[2] = uint8_t((Code >> 16) & 255);
}
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                        REQUEST CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

BakeFieldRequest ConstructBakeFieldRequest(const std::vector<const BakePart *> &Targets, const BakeMeshSet &High,
    const BakeSettings &Settings)
{
    BakeFieldRequest Request;
    Request.Resolution = std::max(8u, std::min(BakeFieldMaximumResolution, Settings.Resolution));
    Request.Triangles = BuildFieldTriangles(High);

    // 📝 Need gating identical to RunMeshBake's per-channel analysis, settled once here so the GPU dispatch,
    // the CPU twin and the encoder all agree on which query groups a channel set pays for.
    const auto Needs = [&](const char *Key)
    {
        return std::find(Settings.Channels.begin(), Settings.Channels.end(), std::string(Key)) != Settings.Channels.end() &&
            IsMeshMapImplemented(Key);
    };
    const bool NeedOcclusion = Needs("occlusion");
    const bool NeedCurvature = Needs("curvature");
    const bool NeedOcclusionSecondary = Needs("occlusion-secondary");
    const bool NeedBentNormal = Needs("bent-normal"), NeedBentWorldNormal = Needs("bent-world-normal"), NeedBentObjectNormal = Needs("bent-object-normal");
    const bool NeedDust = Needs("dust"), NeedDirt = Needs("dirt"), NeedEdgeWear = Needs("edge-wear");
    const bool NeedConvexity = Needs("convexity"), NeedCavity = Needs("cavity");
    const bool NeedThickness = Needs("thickness");
    const bool NeedBevelNormal = Needs("bevel-normal"), NeedBevelMask = Needs("bevel-mask");
    const bool NeedHemisphereSampling = NeedOcclusion || NeedOcclusionSecondary || NeedBentNormal || NeedBentWorldNormal || NeedBentObjectNormal ||
        NeedDust || NeedDirt || NeedEdgeWear;
    const bool NeedCurvatureSampling = NeedCurvature || NeedConvexity || NeedCavity || NeedDirt || NeedEdgeWear;

    BakeFieldUniforms &Uniforms = Request.Uniforms;
    Uniforms.TexelCount = Request.Resolution * Request.Resolution;
    Uniforms.Samples = Settings.Samples;
    Uniforms.BevelSamples = Settings.BevelSamples;
    Uniforms.Front = Settings.Front;
    Uniforms.Back = Settings.Back;
    Uniforms.AoDistance = Settings.AoDistance;
    Uniforms.SecondaryAoDistance = Settings.SecondaryAoDistance;
    Uniforms.CurvatureRadius = Settings.CurvatureRadius;
    Uniforms.BevelRadius = Settings.BevelRadius;
    Uniforms.BevelStrength = Settings.BevelStrength;
    Uniforms.Epsilon = Request.Triangles.Epsilon;
    Uniforms.NeedMask = (NeedHemisphereSampling ? BakeFieldNeedHemisphere : 0u) |
        (NeedOcclusionSecondary ? BakeFieldNeedSecondary : 0u) | (NeedCurvatureSampling ? BakeFieldNeedCurvature : 0u) |
        (NeedThickness ? BakeFieldNeedThickness : 0u) | ((NeedBevelNormal || NeedBevelMask) ? BakeFieldNeedBevel : 0u);

    const bool NeedWireframe = Needs("wireframe");
    Request.Targets.resize(Targets.size());
    for (size_t Index = 0; Index < Targets.size(); ++Index)
        RasterizeTarget(*Targets[Index], Request.Resolution, NeedWireframe, Request.Triangles, Request.Targets[Index]);
    return Request;
}

void EvaluateBakeField(const BakeFieldRequest &Request, size_t TargetIndex, std::vector<BakeTexelField> &Fields,
    const std::function<void(const BakeProgress &)> &OnProgress)
{
    const BakeFieldTarget &Target = Request.Targets.at(TargetIndex);
    const uint32_t Resolution = Request.Resolution;
    Fields.resize(Target.Surfaces.size());
    const uint64_t TotalTexels = uint64_t(Resolution) * Resolution;
    uint64_t Done = 0;
    for (uint32_t Y = 0; Y < Resolution; ++Y)
    {
        for (uint32_t X = 0; X < Resolution; ++X)
        {
            const size_t Texel = size_t(Y) * Resolution + X;
            Fields[Texel] = EvaluateTexel(Request.Triangles.Triangles, Request.Triangles.Extents, Request.Uniforms, Target.Surfaces[Texel]);
            ++Done;
        }
        if (OnProgress)
            OnProgress(BakeProgress{Target.Encode.Name, uint32_t(TargetIndex), uint32_t(Request.Targets.size()), Done, TotalTexels});
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        MAP ENCODING
//------------------------------------------------------------------------------------------------------------------------

std::vector<BakePartResult> EncodeBakeResults(const BakeFieldRequest &Request,
    const std::vector<std::vector<BakeTexelField>> &Fields, const BakeSettings &Settings)
{
    std::vector<std::string> Channels;
    for (const auto &Key : Settings.Channels)
        if (IsMeshMapImplemented(Key)) Channels.push_back(Key);
    std::vector<BakePartResult> Results;
    if (Request.Targets.empty() || Channels.empty()) return Results;
    if (Fields.size() != Request.Targets.size())
        throw std::runtime_error("Bake field encoding received a field set that does not match its request targets.");

    const uint32_t Resolution = Request.Resolution;
    const float Epsilon = Request.Triangles.Epsilon;
    const bool NeedThickness = std::find(Channels.begin(), Channels.end(), std::string("thickness")) != Channels.end();
    const float MaxFrontBack = std::max(1e-6f, std::max(Settings.Front, Settings.Back));

    for (size_t TargetIndex = 0; TargetIndex < Request.Targets.size(); ++TargetIndex)
    {
        const BakeFieldTarget &Target = Request.Targets[TargetIndex];
        const BakeEncodeContext &Encode = Target.Encode;
        if (Fields[TargetIndex].size() != Target.Surfaces.size() || Encode.ChartIds.size() != Target.Surfaces.size())
            throw std::runtime_error("Bake field encoding received mismatched per-texel arrays for a target.");
        BakePartResult Result;
        Result.Name = Encode.Name;
        Result.Resolution = Resolution;
        for (const auto &Key : Channels) Result.Maps.push_back(BakeMapImage{Key, std::vector<uint8_t>(size_t(Resolution) * Resolution * 4, 0)});
        Result.Stats.Charts = Encode.Charts;

        for (uint32_t Y = 0; Y < Resolution; ++Y)
            for (uint32_t X = 0; X < Resolution; ++X)
            {
                const size_t Texel = size_t(Y) * Resolution + X;
                const size_t Pixel = Texel * 4;
                const float PixelU = (float(X) + 0.5f) / float(Resolution);
                const float PixelV = 1.f - (float(Y) + 0.5f) / float(Resolution); // texture v grows upward
                const BakeTexelSurface &Surface = Target.Surfaces[Texel];
                const BakeTexelField &Field = Fields[TargetIndex][Texel];
                const float WorldPosition[3]{Surface.WorldPosition[0], Surface.WorldPosition[1], Surface.WorldPosition[2]};
                const float WorldNormal[3]{Surface.WorldNormal[0], Surface.WorldNormal[1], Surface.WorldNormal[2]};
                const bool Covered = Encode.ChartIds[Texel] != 0;
                const uint32_t ChartId = Covered ? Encode.ChartIds[Texel] - 1 : 0;
                const bool HasHit = Field.HitPosition[3] > 0.5f;
                if (Covered) { if (HasHit) ++Result.Stats.Hits; else ++Result.Stats.Misses; }
                const float HitNormal[3]{Field.HitNormal[0], Field.HitNormal[1], Field.HitNormal[2]};
                const float HitPosition[3]{Field.HitPosition[0], Field.HitPosition[1], Field.HitPosition[2]};
                const float *NH = HasHit ? HitNormal : WorldNormal;
                const float *Q = HasHit ? HitPosition : WorldPosition;
                const float Bent[3]{Field.Bent[0], Field.Bent[1], Field.Bent[2]};
                const float Visibility = Field.Bent[3], SecondaryVisibility = Field.Scalars[0];
                const float SignedCurvature = Field.Scalars[1];
                const float Convexity = std::clamp(SignedCurvature * Settings.MaskStrength, 0.f, 1.f);
                const float Cavity = std::clamp(-SignedCurvature * Settings.MaskStrength, 0.f, 1.f);
                float ThicknessValue = 0.f;
                if (HasHit && NeedThickness)
                    ThicknessValue = Field.Scalars[3] > 0.5f
                        ? std::clamp((Field.Scalars[2] + Epsilon * 3) / std::max(1e-6f, Settings.ThicknessRange), 0.f, 1.f) : 1.f;
                const float BevelNormal[3]{Field.Bevel[0], Field.Bevel[1], Field.Bevel[2]};
                const float BevelMaskValue = Field.Bevel[3];

                // 📝 Shared tangent-space encoder, 1:1 with RunMeshBake's (green flip applies to tangent
                // outputs only, built from the low surface's own frame).
                const auto EncodeTangentSpace = [&](const float Direction[3], uint8_t Out[3])
                {
                    float Tangent[3], Bitangent[3];
                    TangentBasis(WorldNormal, Tangent, Bitangent);
                    float Tx = Dot(Direction, Tangent), Ty = Dot(Direction, Bitangent), Tz = Dot(Direction, WorldNormal);
                    if (!Settings.NormalPlusY) Ty = -Ty;
                    Out[0] = ToByte(Tx * 0.5f + 0.5f);
                    Out[1] = ToByte(Ty * 0.5f + 0.5f);
                    Out[2] = ToByte(Tz * 0.5f + 0.5f);
                };

                for (auto &Map : Result.Maps)
                {
                    uint8_t Out[4]{0, 0, 0, 0};
                    if (Map.Key == "coverage")
                    {
                        Out[0] = Out[1] = Out[2] = Covered ? 255 : 0;
                        Out[3] = Covered ? 255 : 0;
                    }
                    else if (Map.Key == "occlusion")
                    {
                        Out[0] = Out[1] = Out[2] = ToByte(Visibility);
                        Out[3] = 255;
                    }
                    else if (Map.Key == "occlusion-secondary")
                    {
                        Out[0] = Out[1] = Out[2] = ToByte(SecondaryVisibility);
                        Out[3] = 255;
                    }
                    else if (Map.Key == "curvature")
                    {
                        Out[0] = Out[1] = Out[2] = ToByte(0.5f + SignedCurvature * 0.5f);
                        Out[3] = 255;
                    }
                    else if (Map.Key == "convexity") { Out[0] = Out[1] = Out[2] = ToByte(Convexity); Out[3] = 255; }
                    else if (Map.Key == "cavity") { Out[0] = Out[1] = Out[2] = ToByte(Cavity); Out[3] = 255; }
                    else if (HasHit && Map.Key == "alpha") { Out[0] = Out[1] = Out[2] = Out[3] = 255; }
                    else if (HasHit && Map.Key == "world-normal")
                    {
                        float Signed = Settings.NormalPlusY ? HitNormal[1] : -HitNormal[1];
                        Out[0] = ToByte(HitNormal[0] * 0.5f + 0.5f);
                        Out[1] = ToByte(Signed * 0.5f + 0.5f);
                        Out[2] = ToByte(HitNormal[2] * 0.5f + 0.5f);
                        Out[3] = 255;
                    }
                    else if (HasHit && Map.Key == "normal") { EncodeTangentSpace(HitNormal, Out); Out[3] = 255; } // tangent normal
                    else if (Map.Key == "object-normal")
                    {
                        float LocalNormal[3];
                        TransformLocalNormal(Encode.ObjectToWorld, NH, LocalNormal);
                        Out[0] = ToByte(LocalNormal[0] * 0.5f + 0.5f);
                        Out[1] = ToByte(LocalNormal[1] * 0.5f + 0.5f);
                        Out[2] = ToByte(LocalNormal[2] * 0.5f + 0.5f);
                        Out[3] = 255;
                    }
                    else if (Map.Key == "bent-normal") { EncodeTangentSpace(Bent, Out); Out[3] = 255; }
                    else if (Map.Key == "bent-world-normal")
                    {
                        Out[0] = ToByte(Bent[0] * 0.5f + 0.5f); Out[1] = ToByte(Bent[1] * 0.5f + 0.5f); Out[2] = ToByte(Bent[2] * 0.5f + 0.5f);
                        Out[3] = 255;
                    }
                    else if (Map.Key == "bent-object-normal")
                    {
                        float LocalBent[3];
                        TransformLocalNormal(Encode.ObjectToWorld, Bent, LocalBent);
                        Out[0] = ToByte(LocalBent[0] * 0.5f + 0.5f); Out[1] = ToByte(LocalBent[1] * 0.5f + 0.5f); Out[2] = ToByte(LocalBent[2] * 0.5f + 0.5f);
                        Out[3] = 255;
                    }
                    else if (Map.Key == "bevel-normal") { EncodeTangentSpace(BevelNormal, Out); Out[3] = 255; }
                    else if (Map.Key == "bevel-mask") { Out[0] = Out[1] = Out[2] = ToByte(BevelMaskValue); Out[3] = 255; }
                    else if (Map.Key == "thickness") { Out[0] = Out[1] = Out[2] = ToByte(ThicknessValue); Out[3] = 255; }
                    else if (Map.Key == "height")
                    {
                        const float Delta[3]{Q[0] - WorldPosition[0], Q[1] - WorldPosition[1], Q[2] - WorldPosition[2]};
                        const float Signed = HasHit ? Dot(Delta, WorldNormal) : 0.f;
                        Out[0] = Out[1] = Out[2] = ToByte(0.5f + Signed / (2.f * MaxFrontBack));
                        Out[3] = 255;
                    }
                    else if (Map.Key == "vector-displacement")
                    {
                        const float Delta[3]{Q[0] - WorldPosition[0], Q[1] - WorldPosition[1], Q[2] - WorldPosition[2]};
                        Out[0] = ToByte(0.5f + Delta[0] / (2.f * MaxFrontBack));
                        Out[1] = ToByte(0.5f + Delta[1] / (2.f * MaxFrontBack));
                        Out[2] = ToByte(0.5f + Delta[2] / (2.f * MaxFrontBack));
                        Out[3] = 255;
                    }
                    else if (Map.Key == "position")
                    {
                        for (int Axis = 0; Axis < 3; ++Axis)
                            Out[Axis] = ToByte(Encode.Diagonal[Axis] > 1e-9f ? (Q[Axis] - Encode.BoundsMinimum[Axis]) / Encode.Diagonal[Axis] : 0.5f);
                        Out[3] = 255;
                    }
                    else if (Map.Key == "id")
                    {
                        const uint32_t Id = HasHit ? uint32_t(Field.HitNormal[3]) + 1 : 0;
                        Out[0] = uint8_t(Id & 255); Out[1] = uint8_t((Id >> 8) & 255); Out[2] = uint8_t((Id >> 16) & 255);
                        Out[3] = 255;
                    }
                    else if (Map.Key == "uv-island" && Covered) { UvIslandColor(ChartId, Out); Out[3] = 255; }
                    else if (Map.Key == "uv" && Covered)
                    {
                        Out[0] = ToByte(PixelU); Out[1] = ToByte(PixelV); Out[2] = 255; Out[3] = 255;
                    }
                    else if (Map.Key == "wireframe" && Covered && !Encode.Wireframe.empty())
                    {
                        const BakeWireframeTexel &Wireframe = Encode.Wireframe[Texel];
                        const float Ua[2]{Wireframe.WinningUv[0] * Resolution, Wireframe.WinningUv[1] * Resolution};
                        const float Ub[2]{Wireframe.WinningUv[2] * Resolution, Wireframe.WinningUv[3] * Resolution};
                        const float Uc[2]{Wireframe.WinningUv[4] * Resolution, Wireframe.WinningUv[5] * Resolution};
                        const float Area = std::fabs((Ub[0] - Ua[0]) * (Uc[1] - Ua[1]) - (Ub[1] - Ua[1]) * (Uc[0] - Ua[0]));
                        const auto EdgeLength = [](const float P0[2], const float P1[2]) { return std::max(1e-9f, std::sqrt((P0[0] - P1[0]) * (P0[0] - P1[0]) + (P0[1] - P1[1]) * (P0[1] - P1[1]))); };
                        const float DistanceA = std::fabs(Wireframe.Weights[0]) * Area / EdgeLength(Ub, Uc);
                        const float DistanceB = std::fabs(Wireframe.Weights[1]) * Area / EdgeLength(Uc, Ua);
                        const float DistanceC = std::fabs(Wireframe.Weights[2]) * Area / EdgeLength(Ua, Ub);
                        const float Distance = std::min({DistanceA, DistanceB, DistanceC});
                        Out[0] = Out[1] = Out[2] = ToByte(Settings.WireWidth + 0.5f - Distance);
                        Out[3] = 255;
                    }
                    else if (Map.Key == "base-color" && Covered)
                    {
                        // 📝 The geometry-only data model never carries a per-vertex color (no built-in demo
                        // mesh defines one), so this is always the documented fallback, not a placeholder.
                        Out[0] = Out[1] = Out[2] = Out[3] = 255;
                    }
                    else if (Map.Key == "dust")
                    {
                        if (HasHit)
                        {
                            float DustUp[3]{0, 0, 0};
                            if (Settings.DustAxis.size() == 2)
                            {
                                const int Axis = Settings.DustAxis[1] == 'X' ? 0 : Settings.DustAxis[1] == 'Y' ? 1 : Settings.DustAxis[1] == 'Z' ? 2 : 1;
                                DustUp[Axis] = Settings.DustAxis[0] == '-' ? -1.f : 1.f;
                            }
                            const float Facing = std::max(0.f, Dot(NH, DustUp));
                            const float Value = std::pow(Facing, std::max(1e-3f, Settings.DustFalloff)) * (0.65f + 0.35f * (1.f - Visibility));
                            Out[0] = Out[1] = Out[2] = ToByte(Value);
                        }
                        Out[3] = 255;
                    }
                    else if (Map.Key == "dirt")
                    {
                        if (HasHit) Out[0] = Out[1] = Out[2] = ToByte(std::clamp((1.f - Visibility) * Settings.MaskStrength + Cavity * 0.5f, 0.f, 1.f));
                        Out[3] = 255;
                    }
                    else if (Map.Key == "edge-wear")
                    {
                        if (HasHit) Out[0] = Out[1] = Out[2] = ToByte(Convexity * Visibility);
                        Out[3] = 255;
                    }
                    Map.Rgba[Pixel + 0] = Out[0];
                    Map.Rgba[Pixel + 1] = Out[1];
                    Map.Rgba[Pixel + 2] = Out[2];
                    Map.Rgba[Pixel + 3] = Out[3];
                }
            }
        Results.push_back(std::move(Result));
    }
    return Results;
}

std::vector<BakePartResult> RunBakeFieldEvaluation(const std::vector<const BakePart *> &Targets, const BakeMeshSet &High,
    const BakeSettings &Settings, const std::function<void(const BakeProgress &)> &OnProgress)
{
    // 📝 Same early-outs as RunMeshBake: nothing to do without targets or implemented channels.
    bool AnyImplemented = false;
    for (const auto &Key : Settings.Channels)
        AnyImplemented = AnyImplemented || IsMeshMapImplemented(Key);
    if (Targets.empty() || !AnyImplemented) return {};
    const BakeFieldRequest Request = ConstructBakeFieldRequest(Targets, High, Settings);
    std::vector<std::vector<BakeTexelField>> Fields(Request.Targets.size());
    for (size_t Index = 0; Index < Request.Targets.size(); ++Index)
        EvaluateBakeField(Request, Index, Fields[Index], OnProgress);
    return EncodeBakeResults(Request, Fields, Settings);
}
} // namespace Frontier
