/*==========================================================================================================================================
                                                              BAKEMESH.CPP
==========================================================================================================================================*/
#include "Engine/ContentInterchange/BakeMesh.h"
#include "Engine/ContentInterchange/TeapotArchiveData.inl"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Frontier
{
namespace
{
constexpr float kPi = 3.14159265358979323846f;

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

//------------------------------------------------------------------------------------------------------------------------
//                                                     CUBE / SPHERE DEMO MESHES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Port of bakeMeshes.js packedBox(): a 6-face box, each face its own UV island tiled 3 columns x 2 rows
// with a 0.06-unit gutter, matching the web reference's layout and chart ids exactly.
BakePart BuildPackedBox(const char *Name)
{
    BakePart Part;
    Part.Name = Name;
    struct Face { float Normal[3]; float U[3]; float V[3]; };
    const Face Faces[6] = {
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},   // +X
        {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},   // -X
        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},   // +Y
        {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},   // -Y
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},    // +Z
        {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},  // -Z
    };
    for (uint32_t Face = 0; Face < 6; ++Face)
    {
        const auto &F = Faces[Face];
        const uint32_t Base = uint32_t(Part.Vertices.size());
        const uint32_t Column = Face % 3, Row = Face / 3;
        for (uint32_t Corner = 0; Corner < 4; ++Corner)
        {
            const float Su = (Corner == 1 || Corner == 2) ? 1.f : -1.f;
            const float Sv = (Corner == 2 || Corner == 3) ? 1.f : -1.f;
            BakeVertex Vertex;
            for (int Axis = 0; Axis < 3; ++Axis)
                Vertex.Position[Axis] = F.Normal[Axis] + Su * F.U[Axis] + Sv * F.V[Axis];
            Vertex.Normal[0] = F.Normal[0]; Vertex.Normal[1] = F.Normal[1]; Vertex.Normal[2] = F.Normal[2];
            const float Uu = (Su + 1.f) * 0.5f, Vv = (Sv + 1.f) * 0.5f;
            Vertex.Uv[0] = (Column + 0.06f + Uu * 0.88f) / 3.f;
            Vertex.Uv[1] = (Row + 0.06f + Vv * 0.88f) / 2.f;
            Vertex.ChartId = Face + 1;
            Part.Vertices.push_back(Vertex);
        }
        Part.Indices.insert(Part.Indices.end(), {Base, Base + 1, Base + 2, Base, Base + 2, Base + 3});
    }
    return Part;
}

// 📝 Port of three.js SphereGeometry(radius, widthSegments, heightSegments) position/normal/uv generation,
// with the low target's pole-clamped UV fix from bakeMeshes.js createDemoBakeAssets.
BakePart BuildSphere(const char *Name, float Radius, uint32_t WidthSegments, uint32_t HeightSegments, bool ClampUv)
{
    BakePart Part;
    Part.Name = Name;
    std::vector<std::vector<uint32_t>> Grid(HeightSegments + 1, std::vector<uint32_t>(WidthSegments + 1));
    for (uint32_t Y = 0; Y <= HeightSegments; ++Y)
    {
        const float V = float(Y) / float(HeightSegments);
        float UOffset = 0.f;
        if (Y == 0) UOffset = 0.5f / float(WidthSegments);
        else if (Y == HeightSegments) UOffset = -0.5f / float(WidthSegments);
        for (uint32_t X = 0; X <= WidthSegments; ++X)
        {
            const float U = float(X) / float(WidthSegments);
            BakeVertex Vertex;
            Vertex.Position[0] = -Radius * std::cos(U * 2.f * kPi) * std::sin(V * kPi);
            Vertex.Position[1] = Radius * std::cos(V * kPi);
            Vertex.Position[2] = Radius * std::sin(U * 2.f * kPi) * std::sin(V * kPi);
            float N[3]{Vertex.Position[0], Vertex.Position[1], Vertex.Position[2]};
            Normalize(N);
            Vertex.Normal[0] = N[0]; Vertex.Normal[1] = N[1]; Vertex.Normal[2] = N[2];
            float Uv0 = U + UOffset;
            if (ClampUv) Uv0 = std::max(0.f, std::min(1.f, Uv0));
            Vertex.Uv[0] = Uv0; Vertex.Uv[1] = 1.f - V;
            Vertex.ChartId = 1;
            Grid[Y][X] = uint32_t(Part.Vertices.size());
            Part.Vertices.push_back(Vertex);
        }
    }
    for (uint32_t Y = 0; Y < HeightSegments; ++Y)
        for (uint32_t X = 0; X < WidthSegments; ++X)
        {
            const uint32_t A = Grid[Y][X + 1], B = Grid[Y][X], C = Grid[Y + 1][X], D = Grid[Y + 1][X + 1];
            if (Y != 0) Part.Indices.insert(Part.Indices.end(), {A, B, D});
            if (Y != HeightSegments - 1) Part.Indices.insert(Part.Indices.end(), {B, C, D});
        }
    return Part;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     TEAPOT DEMO MESH
//------------------------------------------------------------------------------------------------------------------------

// 📝 Faithful re-derivation of three.js TeapotGeometry's bicubic Bezier evaluation (see
// Engine/ContentInterchange/TeapotArchiveData.inl for provenance of the patch data). The matrix-form "M*G*M"
// evaluation that file performs is mathematically identical to summing the standard cubic Bernstein basis over
// each patch's 4x4 control grid, which is what this function does directly.
BakePart BuildTeapot(const char *Name, float Size, uint32_t Segments)
{
    Segments = std::max(2u, Segments);
    constexpr float BlinnScale = 1.3f;
    const float MaxHeight = 3.15f;
    const float MaxHeight2 = MaxHeight / 2.f;
    const float TrueSize = Size / MaxHeight2;
    const uint32_t VertPerRow = Segments + 1;

    const auto Bernstein = [](float U, float Out[4]) noexcept
    {
        const float Inverse = 1.f - U;
        Out[0] = Inverse * Inverse * Inverse;
        Out[1] = 3.f * U * Inverse * Inverse;
        Out[2] = 3.f * U * U * Inverse;
        Out[3] = U * U * U;
    };
    const auto BernsteinDerivative = [](float U, float Out[4]) noexcept
    {
        const float Inverse = 1.f - U;
        Out[0] = -3.f * Inverse * Inverse;
        Out[1] = 3.f * Inverse * (Inverse - 2.f * U);
        Out[2] = 3.f * U * (2.f * Inverse - U);
        Out[3] = 3.f * U * U;
    };
    const auto ControlPoint = [](uint32_t PatchIndex, uint32_t Row, uint32_t Column, float Out[3]) noexcept
    {
        const uint32_t Vertex = TeapotPatchIndices[PatchIndex * 16 + Row * 4 + Column];
        Out[0] = TeapotControlPoints[Vertex * 3 + 0];
        Out[1] = TeapotControlPoints[Vertex * 3 + 1];
        Out[2] = TeapotControlPoints[Vertex * 3 + 2];
    };

    BakePart Part;
    Part.Name = Name;
    for (uint32_t Patch = 0; Patch < 32; ++Patch)
    {
        const uint32_t Base = uint32_t(Part.Vertices.size());
        for (uint32_t Ss = 0; Ss <= Segments; ++Ss)
        {
            const float S = float(Ss) / float(Segments);
            float Bs[4], Dbs[4];
            Bernstein(S, Bs);
            BernsteinDerivative(S, Dbs);
            for (uint32_t Ts = 0; Ts <= Segments; ++Ts)
            {
                const float T = float(Ts) / float(Segments);
                float Bt[4], Dbt[4];
                Bernstein(T, Bt);
                BernsteinDerivative(T, Dbt);
                float Position[3]{0, 0, 0}, SDir[3]{0, 0, 0}, TDir[3]{0, 0, 0};
                for (uint32_t Row = 0; Row < 4; ++Row)
                    for (uint32_t Column = 0; Column < 4; ++Column)
                    {
                        float Control[3];
                        ControlPoint(Patch, Row, Column, Control);
                        const float Weight = Bs[Row] * Bt[Column];
                        const float DWeightS = Dbs[Row] * Bt[Column];
                        const float DWeightT = Bs[Row] * Dbt[Column];
                        for (int Axis = 0; Axis < 3; ++Axis)
                        {
                            Position[Axis] += Weight * Control[Axis];
                            SDir[Axis] += DWeightS * Control[Axis];
                            TDir[Axis] += DWeightT * Control[Axis];
                        }
                    }
                float Normal[3];
                Cross(TDir, SDir, Normal);
                Normalize(Normal);
                float NormalOut[3];
                if (Position[0] == 0.f && Position[1] == 0.f)
                    { NormalOut[0] = 0.f; NormalOut[1] = Position[2] > MaxHeight2 ? 1.f : -1.f; NormalOut[2] = 0.f; }
                else
                    { NormalOut[0] = Normal[0]; NormalOut[1] = Normal[2]; NormalOut[2] = -Normal[1]; }

                BakeVertex Vertex;
                Vertex.Position[0] = TrueSize * Position[0];
                Vertex.Position[1] = TrueSize * (Position[2] - MaxHeight2);
                Vertex.Position[2] = -TrueSize * Position[1];
                Vertex.Normal[0] = NormalOut[0]; Vertex.Normal[1] = NormalOut[1]; Vertex.Normal[2] = NormalOut[2];
                // 📝 packTeapotUVs (bakeMeshes.js): tile each of the 32 patches into an 8x4 grid, inset by a
                // small gutter, and record its 1-based chart id — this is the low mesh's non-overlapping layout.
                constexpr float Gutter = 0.045f;
                const uint32_t Column = Patch % 8, Row = Patch / 8;
                Vertex.Uv[0] = (Column + Gutter + (1.f - T) * (1.f - 2.f * Gutter)) / 8.f;
                Vertex.Uv[1] = (Row + Gutter + (1.f - S) * (1.f - 2.f * Gutter)) / 4.f;
                Vertex.ChartId = Patch + 1;
                Part.Vertices.push_back(Vertex);
            }
        }
        for (uint32_t Ss = 0; Ss < Segments; ++Ss)
            for (uint32_t Ts = 0; Ts < Segments; ++Ts)
            {
                const uint32_t V1 = Base + Ss * VertPerRow + Ts, V2 = V1 + 1, V3 = V2 + VertPerRow, V4 = V1 + VertPerRow;
                const auto SamePosition = [&](uint32_t A, uint32_t B)
                {
                    return Part.Vertices[A].Position[0] == Part.Vertices[B].Position[0] &&
                        Part.Vertices[A].Position[1] == Part.Vertices[B].Position[1] &&
                        Part.Vertices[A].Position[2] == Part.Vertices[B].Position[2];
                };
                const auto Degenerate = [&](uint32_t X, uint32_t Y, uint32_t Z)
                { return SamePosition(X, Y) || SamePosition(X, Z) || SamePosition(Y, Z); };
                if (!Degenerate(V1, V2, V3)) Part.Indices.insert(Part.Indices.end(), {V1, V2, V3});
                if (!Degenerate(V1, V3, V4)) Part.Indices.insert(Part.Indices.end(), {V1, V3, V4});
            }
    }
    return Part;
}
} // namespace

DemoBakeAssets BuildDemoBakeAssets(const std::string &Shape)
{
    DemoBakeAssets Assets;
    if (Shape == "Cube")
    {
        Assets.Low.Parts.push_back(BuildPackedBox("Cube"));
        Assets.High.Parts.push_back(BuildPackedBox("Cube"));
    }
    else if (Shape == "Sphere")
    {
        Assets.Low.Parts.push_back(BuildSphere("Sphere", 1.05f, 16, 12, true));
        Assets.High.Parts.push_back(BuildSphere("Sphere", 1.05f, 64, 48, false));
    }
    else
    {
        Assets.Low.Parts.push_back(BuildTeapot("Teapot", 1.05f, 8));
        Assets.High.Parts.push_back(BuildTeapot("Teapot", 1.05f, 24));
    }
    Assets.Low.Label = Shape + " study";
    Assets.High.Label = Shape + " study";
    Assets.Low.Source = Assets.High.Source = "Built-in geometry";
    return Assets;
}

MeshSummary SummarizeMesh(const BakeMeshSet &Set)
{
    MeshSummary Summary;
    Summary.Objects = uint32_t(Set.Parts.size());
    for (const auto &Part : Set.Parts)
    {
        Summary.Triangles += Part.TriangleCount();
        Summary.Vertices += Part.Vertices.size();
    }
    return Summary;
}

std::string MeshSummaryText(const BakeMeshSet &Set)
{
    const auto Summary = SummarizeMesh(Set);
    return std::to_string(Summary.Objects) + " objects, " + std::to_string(Summary.Triangles) + " triangles";
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        TRIANGLE BVH
//------------------------------------------------------------------------------------------------------------------------

void BakeTriangleBvh::Build(const BakeMeshSet &High)
{
    Triangles.clear();
    Nodes.clear();
    Order.clear();
    for (uint32_t PartIndex = 0; PartIndex < High.Parts.size(); ++PartIndex)
    {
        const auto &Part = High.Parts[PartIndex];
        for (size_t Index = 0; Index + 2 < Part.Indices.size(); Index += 3)
        {
            const auto &VA = Part.Vertices[Part.Indices[Index + 0]];
            const auto &VB = Part.Vertices[Part.Indices[Index + 1]];
            const auto &VC = Part.Vertices[Part.Indices[Index + 2]];
            Triangle Triangle_;
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                Triangle_.A[Axis] = VA.Position[Axis];
                Triangle_.B[Axis] = VB.Position[Axis];
                Triangle_.C[Axis] = VC.Position[Axis];
                Triangle_.NormalA[Axis] = VA.Normal[Axis];
                Triangle_.NormalB[Axis] = VB.Normal[Axis];
                Triangle_.NormalC[Axis] = VC.Normal[Axis];
            }
            Triangle_.PartIndex = PartIndex;
            Triangles.push_back(Triangle_);
        }
    }
    Order.resize(Triangles.size());
    for (uint32_t Index = 0; Index < Order.size(); ++Index) Order[Index] = Index;
    if (!Triangles.empty()) BuildRange(0, uint32_t(Order.size()));
}

int32_t BakeTriangleBvh::BuildRange(uint32_t Start, uint32_t Count)
{
    Node Node_;
    Node_.Minimum[0] = Node_.Minimum[1] = Node_.Minimum[2] = 3.402823e38f;
    Node_.Maximum[0] = Node_.Maximum[1] = Node_.Maximum[2] = -3.402823e38f;
    for (uint32_t Index = 0; Index < Count; ++Index)
    {
        const auto &Triangle_ = Triangles[Order[Start + Index]];
        for (const float *Vertex : {Triangle_.A, Triangle_.B, Triangle_.C})
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                Node_.Minimum[Axis] = std::min(Node_.Minimum[Axis], Vertex[Axis]);
                Node_.Maximum[Axis] = std::max(Node_.Maximum[Axis], Vertex[Axis]);
            }
    }
    const int32_t NodeIndex = int32_t(Nodes.size());
    Nodes.push_back(Node_);
    constexpr uint32_t LeafTriangles = 4;
    if (Count <= LeafTriangles)
    {
        Nodes[NodeIndex].Start = Start;
        Nodes[NodeIndex].Count = Count;
        return NodeIndex;
    }
    const float Extent[3]{Nodes[NodeIndex].Maximum[0] - Nodes[NodeIndex].Minimum[0],
        Nodes[NodeIndex].Maximum[1] - Nodes[NodeIndex].Minimum[1], Nodes[NodeIndex].Maximum[2] - Nodes[NodeIndex].Minimum[2]};
    const int Axis = Extent[0] > Extent[1] ? (Extent[0] > Extent[2] ? 0 : 2) : (Extent[1] > Extent[2] ? 1 : 2);
    std::sort(Order.begin() + Start, Order.begin() + Start + Count, [&](uint32_t A, uint32_t B)
    {
        const auto Centroid = [&](uint32_t TriangleIndex)
        {
            const auto &Triangle_ = Triangles[TriangleIndex];
            return (Triangle_.A[Axis] + Triangle_.B[Axis] + Triangle_.C[Axis]) / 3.f;
        };
        return Centroid(A) < Centroid(B);
    });
    const uint32_t Mid = Count / 2;
    const int32_t Left = BuildRange(Start, Mid);
    const int32_t Right = BuildRange(Start + Mid, Count - Mid);
    Nodes[NodeIndex].Left = Left;
    Nodes[NodeIndex].Right = Right;
    return NodeIndex;
}

namespace
{
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
// 📝 Möller–Trumbore ray-triangle intersection, returns the barycentric-interpolated normal on hit.
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
} // namespace

BakeTriangleBvh::Hit BakeTriangleBvh::CastRay(const float Origin[3], const float Direction[3], float MinDistance,
    float MaxDistance) const
{
    Hit Result;
    if (Nodes.empty()) return Result;
    const float InvDirection[3]{1.f / Direction[0], 1.f / Direction[1], 1.f / Direction[2]};
    float BestDistance = MaxDistance;
    std::vector<int32_t> Stack{0};
    while (!Stack.empty())
    {
        const int32_t NodeIndex = Stack.back();
        Stack.pop_back();
        const auto &Node_ = Nodes[NodeIndex];
        if (!RayBox(Origin, InvDirection, Node_.Minimum, Node_.Maximum, MinDistance, BestDistance)) continue;
        if (Node_.Left < 0)
        {
            for (uint32_t Index = 0; Index < Node_.Count; ++Index)
            {
                const auto &Triangle_ = Triangles[Order[Node_.Start + Index]];
                float Distance, Barycentric[3];
                if (RayTriangle(Origin, Direction, Triangle_.A, Triangle_.B, Triangle_.C, MinDistance, BestDistance, Distance,
                        Barycentric))
                {
                    BestDistance = Distance;
                    Result.Found = true;
                    Result.Distance = Distance;
                    Result.PartIndex = Triangle_.PartIndex;
                    for (int Axis = 0; Axis < 3; ++Axis)
                    {
                        Result.Position[Axis] = Origin[Axis] + Direction[Axis] * Distance;
                        Result.Normal[Axis] = Barycentric[0] * Triangle_.NormalA[Axis] + Barycentric[1] * Triangle_.NormalB[Axis] +
                            Barycentric[2] * Triangle_.NormalC[Axis];
                    }
                    Normalize(Result.Normal);
                }
            }
        }
        else
        {
            Stack.push_back(Node_.Left);
            Stack.push_back(Node_.Right);
        }
    }
    return Result;
}

namespace
{
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
// 📝 Ericson, "Real-Time Collision Detection" 5.1.5: nearest point on triangle ABC to Point, handling the
// three vertex, three edge and one face Voronoi regions explicitly (no iteration, no degenerate branches).
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
// 📝 Barycentric weights of a point already known to lie in triangle ABC's plane (the result of
// ClosestPointOnTriangle above) — used to interpolate a smooth normal at the nearest point, rather than
// taking one vertex's/the face's flat normal.
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
} // namespace

BakeTriangleBvh::ClosestPointResult BakeTriangleBvh::ClosestPoint(const float Point[3], float MaxDistance) const
{
    ClosestPointResult Result;
    if (Nodes.empty()) return Result;
    float BestDistanceSquared = MaxDistance * MaxDistance;
    // 📝 Explicit stack, nearer child pushed last (visited first): a plain two-child BVH needs no priority
    // queue to get effective nearest-point pruning, just child-order-by-box-distance on each descent.
    std::vector<int32_t> Stack{0};
    while (!Stack.empty())
    {
        const int32_t NodeIndex = Stack.back();
        Stack.pop_back();
        const auto &Node_ = Nodes[NodeIndex];
        if (BoxDistanceSquared(Point, Node_.Minimum, Node_.Maximum) > BestDistanceSquared) continue;
        if (Node_.Left < 0)
        {
            for (uint32_t Index = 0; Index < Node_.Count; ++Index)
            {
                const auto &Triangle_ = Triangles[Order[Node_.Start + Index]];
                float Closest[3];
                ClosestPointOnTriangle(Point, Triangle_.A, Triangle_.B, Triangle_.C, Closest);
                const float Delta[3]{Closest[0] - Point[0], Closest[1] - Point[1], Closest[2] - Point[2]};
                const float DistanceSquared = Dot(Delta, Delta);
                if (DistanceSquared < BestDistanceSquared)
                {
                    BestDistanceSquared = DistanceSquared;
                    Result.Found = true;
                    Result.Distance = std::sqrt(DistanceSquared);
                    Result.PartIndex = Triangle_.PartIndex;
                    for (int Axis = 0; Axis < 3; ++Axis) Result.Point[Axis] = Closest[Axis];
                    float Weights[3];
                    PlaneBarycentric(Closest, Triangle_.A, Triangle_.B, Triangle_.C, Weights);
                    for (int Axis = 0; Axis < 3; ++Axis)
                        Result.Normal[Axis] = Weights[0] * Triangle_.NormalA[Axis] + Weights[1] * Triangle_.NormalB[Axis] +
                            Weights[2] * Triangle_.NormalC[Axis];
                    Normalize(Result.Normal);
                }
            }
        }
        else
        {
            const float LeftDistance = BoxDistanceSquared(Point, Nodes[Node_.Left].Minimum, Nodes[Node_.Left].Maximum);
            const float RightDistance = BoxDistanceSquared(Point, Nodes[Node_.Right].Minimum, Nodes[Node_.Right].Maximum);
            // 📝 Push the farther child first so the nearer one is popped (visited) first, tightening
            // BestDistanceSquared before the farther subtree is even tested against the box prune above.
            if (LeftDistance <= RightDistance) { Stack.push_back(Node_.Right); Stack.push_back(Node_.Left); }
            else { Stack.push_back(Node_.Left); Stack.push_back(Node_.Right); }
        }
    }
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        MESH MAP EVALUATION
//------------------------------------------------------------------------------------------------------------------------

const std::vector<std::string> &ImplementedMeshMapKeys()
{
    // 📝 All 27 keys from meshMaps.js's MESH_MAPS now have a real evaluator below - see RunMeshBake's per-texel
    // loop. "base-color" is included honestly: the geometry-only data model never carries vertex color (none
    // of the built-in demo meshes or the importer contract define one), so it always resolves to the exact
    // documented fallback ("White if no vertex color exists"), not a fabricated value.
    static const std::vector<std::string> Keys{
        "normal", "world-normal", "object-normal", "bent-normal", "bent-world-normal", "bent-object-normal", "bevel-normal",
        "occlusion", "occlusion-secondary", "curvature", "convexity", "cavity", "thickness", "height", "vector-displacement",
        "position", "id", "uv-island", "uv", "wireframe", "coverage", "alpha", "bevel-mask", "dust", "dirt", "edge-wear", "base-color"};
    return Keys;
}
bool IsMeshMapImplemented(const std::string &Key)
{
    const auto &Keys = ImplementedMeshMapKeys();
    return std::find(Keys.begin(), Keys.end(), Key) != Keys.end();
}

namespace
{
void Transform(const std::array<float, 16> &Matrix, const float In[3], float Out[3]) noexcept
{
    for (int Row = 0; Row < 3; ++Row)
        Out[Row] = Matrix[Row * 4 + 0] * In[0] + Matrix[Row * 4 + 1] * In[1] + Matrix[Row * 4 + 2] * In[2] + Matrix[Row * 4 + 3];
}
uint8_t ToByte(float Value) noexcept { return uint8_t(std::max(0.f, std::min(1.f, Value)) * 255.f + 0.5f); }

// 📝 Barycentric interpolation across a low-mesh triangle for position/normal/uv, used both to rasterize UV
// space and to build the per-texel tangent frame for the tangent-normal map.
struct Interpolated
{
    float Position[3];
    float Normal[3];
};
// 📝 WeightA/WeightB/WeightC are the barycentric weights of A/B/C directly (summing to ~1), not the
// "W=1-u-v" convention — callers must supply all three explicitly to avoid swapping vertices.
Interpolated BarycentricLerp(const BakeVertex &A, const BakeVertex &B, const BakeVertex &C, float WeightA, float WeightB,
    float WeightC) noexcept
{
    Interpolated Out{};
    for (int Axis = 0; Axis < 3; ++Axis)
        Out.Position[Axis] = WeightA * A.Position[Axis] + WeightB * B.Position[Axis] + WeightC * C.Position[Axis];
    for (int Axis = 0; Axis < 3; ++Axis)
        Out.Normal[Axis] = WeightA * A.Normal[Axis] + WeightB * B.Normal[Axis] + WeightC * C.Normal[Axis];
    Normalize(Out.Normal);
    return Out;
}
void TangentBasis(const float Normal[3], float Tangent[3], float Bitangent[3]) noexcept
{
    const float UpReference[3]{0, 1, 0}, SideReference[3]{1, 0, 0};
    const bool UseUp = std::fabs(Normal[1]) < 0.99f;
    Cross(UseUp ? UpReference : SideReference, Normal, Tangent);
    Normalize(Tangent);
    Cross(Normal, Tangent, Bitangent);
    Normalize(Bitangent);
}
// 📝 1:1 port of meshBakeCore.js's hemisphere(normal): an arbitrary (but deterministic) tangent frame
// around a world-space hit normal, used to orient occlusion's cosine-weighted samples and curvature's
// four cardinal tangent-plane probes.
void Hemisphere(const float Normal[3], float Tangent[3], float Bitangent[3]) noexcept
{
    const float ZReference[3]{0, 0, 1}, YReference[3]{0, 1, 0};
    Cross(std::fabs(Normal[2]) < 0.9f ? ZReference : YReference, Normal, Tangent);
    Normalize(Tangent);
    Cross(Normal, Tangent, Bitangent);
}
// 📝 1:1 with meshBakeCore.js's `localNormal`: the low target's original local axes, undone from its imported
// world transform by the PLAIN transpose of the upper 3x3 (not a true inverse-transpose) - matches the web
// reference's own normal-matrix shortcut exactly, which is only exact for orthonormal (rotation-only, no
// non-uniform scale) object-to-world matrices, same as every built-in demo part's transform.
void TransformLocalNormal(const std::array<float, 16> &ObjectToWorld, const float World[3], float Out[3]) noexcept
{
    for (int Row = 0; Row < 3; ++Row)
        Out[Row] = ObjectToWorld[0 * 4 + Row] * World[0] + ObjectToWorld[1 * 4 + Row] * World[1] + ObjectToWorld[2 * 4 + Row] * World[2];
    Normalize(Out);
}
// 📝 1:1 with meshBakeCore.js's uvIslandColor: a bijective odd multiplication mod 2^24 so chart colors are
// deterministic and distinct for every supported chart count; zero stays reserved for "outside".
void UvIslandColor(uint32_t Id, uint8_t Out[3]) noexcept
{
    const uint32_t Code = (Id * 0x9e3779u) & 0xffffffu;
    Out[0] = uint8_t(Code & 255);
    Out[1] = uint8_t((Code >> 8) & 255);
    Out[2] = uint8_t((Code >> 16) & 255);
}
// 📝 1:1 with meshBakeCore.js's averageNormal: a local high-normal average around Point within Radius, biased
// slightly inward (-Radius*0.3 along Normal) so the sample ring follows rounded geometry rather than floating
// above it, weighted by distance falloff and rejecting samples that face sharply away from Normal/Facing -
// the geometry-only "rounded edge" approximation behind bevel-normal/bevel-mask. Zero radius is a bypass.
void AverageNormal(const BakeTriangleBvh &Bvh, const float Point[3], const float Normal[3], const float Facing[3], float Radius,
    uint32_t Samples, float Epsilon, float Out[3]) noexcept
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
        const auto Near = Bvh.ClosestPoint(Query, SearchRadius);
        if (!Near.Found || Near.Distance > SearchRadius) continue;
        if (Dot(Near.Normal, Normal) < -0.1f || Dot(Near.Normal, Facing) < -1e-6f) continue;
        const float SampleWeight = 1.f - std::clamp(Near.Distance / SearchRadius, 0.f, 1.f);
        Sum[0] += Near.Normal[0] * SampleWeight; Sum[1] += Near.Normal[1] * SampleWeight; Sum[2] += Near.Normal[2] * SampleWeight;
        Weight += SampleWeight;
    }
    Out[0] = Sum[0] / Weight; Out[1] = Sum[1] / Weight; Out[2] = Sum[2] / Weight;
    Normalize(Out);
}
} // namespace

BakeMapOverrides CaptureBakeMapOverrides(const BakeSettings &Settings)
{
    return {Settings.AoDistance, Settings.SecondaryAoDistance, Settings.ThicknessRange, Settings.CurvatureRadius,
        Settings.Samples, Settings.BevelRadius, Settings.BevelStrength, Settings.BevelSamples, Settings.DustAxis,
        Settings.DustFalloff, Settings.WireWidth, Settings.MaskStrength};
}
void ApplyBakeMapOverrides(BakeSettings &Settings, const BakeMapOverrides &Value)
{
    Settings.AoDistance=Value.AoDistance; Settings.SecondaryAoDistance=Value.SecondaryAoDistance;
    Settings.ThicknessRange=Value.ThicknessRange; Settings.CurvatureRadius=Value.CurvatureRadius; Settings.Samples=Value.Samples;
    Settings.BevelRadius=Value.BevelRadius; Settings.BevelStrength=Value.BevelStrength; Settings.BevelSamples=Value.BevelSamples;
    Settings.DustAxis=Value.DustAxis; Settings.DustFalloff=Value.DustFalloff; Settings.WireWidth=Value.WireWidth; Settings.MaskStrength=Value.MaskStrength;
}

void ApplyBakeMapOutput(uint8_t Colour[4], const BakeMapOutputSettings &Properties) noexcept
{
    if (!Colour[3]) return;
    if (Properties.SwapGreenBlue) std::swap(Colour[1], Colour[2]);
    if (Properties.FlipGreen) Colour[1] = uint8_t(255 - Colour[1]);
    const float Gain = std::isfinite(Properties.Gain) ? std::clamp(Properties.Gain, 0.f, 4.f) : 1.f;
    for (int Channel=0; Channel<3; ++Channel)
    {
        const float Value = std::clamp(float(Colour[Channel])*Gain, 0.f, 255.f);
        Colour[Channel] = uint8_t(Value + .5f);
        if (Properties.InvertRgb) Colour[Channel] = uint8_t(255 - Colour[Channel]);
    }
}

std::vector<BakePartResult> RunMeshBake(const std::vector<const BakePart *> &Targets, const BakeMeshSet &High,
    const BakeSettings &Settings, const std::function<void(const BakeProgress &)> &OnProgress)
{
    std::vector<std::string> Channels;
    for (const auto &Key : Settings.Channels)
        if (IsMeshMapImplemented(Key)) Channels.push_back(Key);
    std::vector<BakePartResult> Results;
    if (Targets.empty() || Channels.empty()) return Results;

    BakeTriangleBvh Bvh;
    Bvh.Build(High);
    const uint32_t Resolution = std::max(8u, std::min(1024u, Settings.Resolution));
    const auto Needs = [&](const char *Key) { return std::find(Channels.begin(), Channels.end(), std::string(Key)) != Channels.end(); };
    const bool NeedOcclusion = Needs("occlusion");
    const bool NeedCurvature = Needs("curvature");
    const bool NeedOcclusionSecondary = Needs("occlusion-secondary");
    const bool NeedBentNormal = Needs("bent-normal"), NeedBentWorldNormal = Needs("bent-world-normal"), NeedBentObjectNormal = Needs("bent-object-normal");
    const bool NeedDust = Needs("dust"), NeedDirt = Needs("dirt"), NeedEdgeWear = Needs("edge-wear");
    const bool NeedConvexity = Needs("convexity"), NeedCavity = Needs("cavity");
    const bool NeedThickness = Needs("thickness");
    const bool NeedBevelNormal = Needs("bevel-normal"), NeedBevelMask = Needs("bevel-mask");
    // 📝 Hemisphere sampling (occlusion + bent-* normals) and the curvature probes are each one shared pass
    // per texel - every map that reads their result just consumes Visibility/Bent/SignedCurvature below,
    // matching meshBakeCore.js's own `if (maps.occlusion || maps["bent-normal"] || ...)` gating exactly.
    const bool NeedHemisphereSampling = NeedOcclusion || NeedOcclusionSecondary || NeedBentNormal || NeedBentWorldNormal || NeedBentObjectNormal ||
        NeedDust || NeedDirt || NeedEdgeWear;
    const bool NeedCurvatureSampling = NeedCurvature || NeedConvexity || NeedCavity || NeedDirt || NeedEdgeWear;

    // 📝 epsilon = max(highMeshBoundingBoxDiagonal * 1e-5, 1e-7) — meshBakeCore.js's self-intersection
    // bias for occlusion/thickness ray origins, scaled to the actual high mesh's size instead of a fixed
    // constant that would be wrong for a thumbnail-sized demo mesh or a building-sized imported one.
    float BoundsMinimum[3]{3.402823e38f, 3.402823e38f, 3.402823e38f}, BoundsMaximum[3]{-3.402823e38f, -3.402823e38f, -3.402823e38f};
    for (const auto &Part : High.Parts)
        for (const auto &Vertex : Part.Vertices)
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                BoundsMinimum[Axis] = std::min(BoundsMinimum[Axis], Vertex.Position[Axis]);
                BoundsMaximum[Axis] = std::max(BoundsMaximum[Axis], Vertex.Position[Axis]);
            }
    const float Diagonal[3]{BoundsMaximum[0] - BoundsMinimum[0], BoundsMaximum[1] - BoundsMinimum[1], BoundsMaximum[2] - BoundsMinimum[2]};
    const float Epsilon = std::max(std::sqrt(Dot(Diagonal, Diagonal)) * 1e-5f, 1e-7f);

    for (uint32_t TargetIndex = 0; TargetIndex < Targets.size(); ++TargetIndex)
    {
        const BakePart &Part = *Targets[TargetIndex];
        BakePartResult Result;
        Result.Name = Part.Name;
        Result.Resolution = Resolution;
        for (const auto &Key : Channels) Result.Maps.push_back(BakeMapImage{Key, std::vector<uint8_t>(size_t(Resolution) * Resolution * 4, 0)});

        uint32_t ChartMaximum = 0;
        for (const auto &Vertex : Part.Vertices) ChartMaximum = std::max(ChartMaximum, Vertex.ChartId);
        Result.Stats.Charts = ChartMaximum;

        const uint64_t TotalTexels = uint64_t(Resolution) * Resolution;
        uint64_t Done = 0;
        for (uint32_t Y = 0; Y < Resolution; ++Y)
        {
            for (uint32_t X = 0; X < Resolution; ++X)
            {
                const size_t Pixel = (size_t(Y) * Resolution + X) * 4;
                const float PixelU = (float(X) + 0.5f) / float(Resolution);
                const float PixelV = 1.f - (float(Y) + 0.5f) / float(Resolution); // texture v grows upward

                bool Covered = false;
                Interpolated Sample{};
                // 📝 Retained for wireframe (texture-space distance to the winning triangle's nearest edge)
                // and uv-island (its chart id) - both are pure low-mesh quantities, not high-surface hits.
                const BakeVertex *WinningA = nullptr, *WinningB = nullptr, *WinningC = nullptr;
                float WinningWeightA = 0.f, WinningWeightB = 0.f, WinningWeightC = 0.f;
                for (size_t Index = 0; Index + 2 < Part.Indices.size() && !Covered; Index += 3)
                {
                    const auto &A = Part.Vertices[Part.Indices[Index + 0]];
                    const auto &B = Part.Vertices[Part.Indices[Index + 1]];
                    const auto &C = Part.Vertices[Part.Indices[Index + 2]];
                    const float Denominator = (B.Uv[1] - C.Uv[1]) * (A.Uv[0] - C.Uv[0]) + (C.Uv[0] - B.Uv[0]) * (A.Uv[1] - C.Uv[1]);
                    if (std::fabs(Denominator) < 1e-12f) continue;
                    // 📝 Standard barycentric weights: WeightA is A's weight, WeightB is B's, WeightC is C's.
                    const float WeightA = ((B.Uv[1] - C.Uv[1]) * (PixelU - C.Uv[0]) + (C.Uv[0] - B.Uv[0]) * (PixelV - C.Uv[1])) / Denominator;
                    const float WeightB = ((C.Uv[1] - A.Uv[1]) * (PixelU - C.Uv[0]) + (A.Uv[0] - C.Uv[0]) * (PixelV - C.Uv[1])) / Denominator;
                    const float WeightC = 1.f - WeightA - WeightB;
                    if (WeightA < -1e-4f || WeightB < -1e-4f || WeightC < -1e-4f) continue;
                    Sample = BarycentricLerp(A, B, C, WeightA, WeightB, WeightC);
                    WinningA = &A; WinningB = &B; WinningC = &C;
                    WinningWeightA = WeightA; WinningWeightB = WeightB; WinningWeightC = WeightC;
                    Covered = true;
                }

                // 📝 One shared high-surface hit per texel: every map below (tangent/world normal, alpha,
                // occlusion, curvature) reads the same projected point/normal, instead of each map key
                // re-casting its own identical ray as the previous single-map-at-a-time version did.
                bool HasHit = false;
                BakeTriangleBvh::Hit Hit;
                float WorldNormal[3]{0, 0, 1};
                float WorldPosition[3]{0, 0, 0};
                if (Covered)
                {
                    Transform(Part.ObjectToWorld, Sample.Position, WorldPosition);
                    for (int Axis = 0; Axis < 3; ++Axis) WorldNormal[Axis] = Sample.Normal[Axis];
                    Normalize(WorldNormal);
                    // 📝 "Ray front/back limits act as a uniform projection envelope" (baking-studio.md): a
                    // single ray spans from Back behind the low surface to Front in front of it along the
                    // normal, taking the nearest high-surface hit anywhere in that envelope. This also makes
                    // an identical low==high demo mesh self-intersect correctly (hit at t ≈ Back), instead of
                    // two directionally-exclusive half-searches that can straddle a flat surface and miss it.
                    const float EnvelopeLength = Settings.Back + Settings.Front;
                    float RayOrigin[3]{WorldPosition[0] - WorldNormal[0] * Settings.Back, WorldPosition[1] - WorldNormal[1] * Settings.Back,
                        WorldPosition[2] - WorldNormal[2] * Settings.Back};
                    if (Settings.ProjectionRay) Hit = Bvh.CastRay(RayOrigin, WorldNormal, 0.f, EnvelopeLength);
                    else
                    {
                        // Nearest-point projection uses the larger front/back radius, then enforces the
                        // signed front/back envelope along the low surface normal.
                        const auto Near = Bvh.ClosestPoint(WorldPosition, std::max(Settings.Front, Settings.Back));
                        if (Near.Found)
                        {
                            const float Offset[3]{Near.Point[0]-WorldPosition[0], Near.Point[1]-WorldPosition[1], Near.Point[2]-WorldPosition[2]};
                            const float NormalOffset = Dot(Offset, WorldNormal);
                            if (NormalOffset >= -Settings.Back && NormalOffset <= Settings.Front)
                            {
                                Hit.Found = true; Hit.Distance = Near.Distance; Hit.PartIndex = Near.PartIndex;
                                for (int Axis=0; Axis<3; ++Axis) { Hit.Position[Axis] = Near.Point[Axis]; Hit.Normal[Axis] = Near.Normal[Axis]; }
                            }
                        }
                    }
                    HasHit = Hit.Found;
                    if (HasHit) ++Result.Stats.Hits; else ++Result.Stats.Misses;
                }

                // 📝 "NH"/"Q" mirror meshBakeCore.js's own names: the resolved high-surface normal/position on
                // a hit, falling back to the low mesh's own interpolated normal/position on a miss (its
                // documented "neutral low normals/zero displacement" fallback) - every map below reads these,
                // not Hit.Normal/Hit.Position directly, so a miss still produces the spec's fallback pixel
                // instead of a transparent hole.
                const float *NH = HasHit ? Hit.Normal : WorldNormal;
                const float *Q = HasHit ? Hit.Position : WorldPosition;

                // 📝 Ambient occlusion + bent normals + secondary AO: one shared cosine-weighted, golden-angle
                // hemisphere sampler around NH (1:1 port of meshBakeCore.js's combined occlusion/bent-normal
                // loop) - a deterministic, world-position-seeded rotation avoids UV-chart-boundary noise. Bent
                // accumulates every unblocked sample direction; occlusion-secondary reuses the same samples
                // against its own distance instead of re-sampling.
                float Visibility = 1.f, SecondaryVisibility = 1.f;
                float Bent[3]{NH[0], NH[1], NH[2]};
                if (HasHit && (NeedHemisphereSampling || NeedCurvatureSampling))
                {
                    float Tangent[3], Bitangent[3];
                    Hemisphere(Hit.Normal, Tangent, Bitangent);
                    if (NeedHemisphereSampling)
                    {
                        const float Origin[3]{Hit.Position[0] + Hit.Normal[0] * Epsilon * 3, Hit.Position[1] + Hit.Normal[1] * Epsilon * 3,
                            Hit.Position[2] + Hit.Normal[2] * Epsilon * 3};
                        const uint32_t Samples = std::max(1u, Settings.Samples);
                        const float SampleDistance = NeedOcclusionSecondary ? std::max(Settings.AoDistance, Settings.SecondaryAoDistance) : Settings.AoDistance;
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
                            const auto Obstruction = Bvh.CastRay(Origin, Direction, Epsilon, SampleDistance);
                            const bool Occluded = Obstruction.Found && Obstruction.Distance <= Settings.AoDistance;
                            if (Occluded) ++Blocked;
                            else { BentSum[0] += Direction[0]; BentSum[1] += Direction[1]; BentSum[2] += Direction[2]; }
                            if (Obstruction.Found && Obstruction.Distance <= Settings.SecondaryAoDistance) ++SecondaryBlocked;
                        }
                        Visibility = 1.f - float(Blocked) / float(Samples);
                        SecondaryVisibility = 1.f - float(SecondaryBlocked) / float(Samples);
                        if (Dot(BentSum, BentSum) < 1e-12f) { Bent[0] = Hit.Normal[0]; Bent[1] = Hit.Normal[1]; Bent[2] = Hit.Normal[2]; }
                        else { Bent[0] = BentSum[0]; Bent[1] = BentSum[1]; Bent[2] = BentSum[2]; Normalize(Bent); }
                    }
                }
                // 📝 Curvature: four cardinal tangent-plane probes find the nearest high-surface point in
                // each direction; the signed divergence of that neighbour's normal from the centre normal,
                // projected back onto the probe direction, is meshBakeCore.js's discrete curvature estimate
                // (positive = convex, negative = concave), remapped from [-1,1] into the [0,1] map range.
                float SignedCurvature = 0.f;
                if (HasHit && NeedCurvatureSampling)
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
                        const float Query[3]{Hit.Position[0] + Direction[0] * Settings.CurvatureRadius,
                            Hit.Position[1] + Direction[1] * Settings.CurvatureRadius, Hit.Position[2] + Direction[2] * Settings.CurvatureRadius};
                        const auto Near = Bvh.ClosestPoint(Query, Settings.CurvatureRadius * 2.f);
                        if (Near.Found)
                        {
                            const float Delta[3]{Near.Normal[0] - Hit.Normal[0], Near.Normal[1] - Hit.Normal[1], Near.Normal[2] - Hit.Normal[2]};
                            Sum += Dot(Delta, Direction);
                            ++Count;
                        }
                    }
                    SignedCurvature = Count ? Sum / float(Count) : 0.f;
                }

                // 📝 Convexity/cavity isolate the signed curvature's positive/negative half into independent
                // bright-edge masks, each bounded by mask-strength - 1:1 with meshBakeCore.js's `convexity =
                // clamp(signedCurvature * maskStrength)`, `cavity = clamp(-signedCurvature * maskStrength)`.
                const float Convexity = std::clamp(SignedCurvature * Settings.MaskStrength, 0.f, 1.f);
                const float Cavity = std::clamp(-SignedCurvature * Settings.MaskStrength, 0.f, 1.f);

                // 📝 Thickness: an inward ray (opposite NH) from just past the surface to the nearest backside
                // hit - "ray searches beyond that display range, so farther surfaces saturate white instead of
                // becoming false holes"; an open mesh with no inward hit also reads white (thick/no hole), not
                // black (a false thin spot).
                float ThicknessValue = 0.f;
                if (HasHit && NeedThickness)
                {
                    const float InwardOrigin[3]{Q[0] - Hit.Normal[0] * Epsilon * 3, Q[1] - Hit.Normal[1] * Epsilon * 3, Q[2] - Hit.Normal[2] * Epsilon * 3};
                    const float InwardDirection[3]{-Hit.Normal[0], -Hit.Normal[1], -Hit.Normal[2]};
                    const auto Inward = Bvh.CastRay(InwardOrigin, InwardDirection, Epsilon, 3.402823e38f);
                    ThicknessValue = Inward.Found ? std::clamp((Inward.Distance + Epsilon * 3) / std::max(1e-6f, Settings.ThicknessRange), 0.f, 1.f) : 1.f;
                }

                // 📝 Bevel normal/mask: a local high-normal average within BevelRadius, blended with NH by
                // BevelStrength - "changes shading only"; the mask isolates how much that averaging actually
                // bent the normal (meshBakeCore.js's `clamp((1 - NH.dot(averaged)) * 8)`).
                float BevelNormal[3]{NH[0], NH[1], NH[2]};
                float BevelMaskValue = 0.f;
                if (HasHit && (NeedBevelNormal || NeedBevelMask))
                {
                    float Averaged[3];
                    AverageNormal(Bvh, Q, Hit.Normal, WorldNormal, Settings.BevelRadius, std::max(1u, Settings.BevelSamples), Epsilon, Averaged);
                    const float InverseStrength = 1.f - std::clamp(Settings.BevelStrength, 0.f, 1.f);
                    BevelNormal[0] = Averaged[0] * Settings.BevelStrength + Hit.Normal[0] * InverseStrength;
                    BevelNormal[1] = Averaged[1] * Settings.BevelStrength + Hit.Normal[1] * InverseStrength;
                    BevelNormal[2] = Averaged[2] * Settings.BevelStrength + Hit.Normal[2] * InverseStrength;
                    Normalize(BevelNormal);
                    BevelMaskValue = std::clamp((1.f - Dot(Hit.Normal, BevelNormal)) * 8.f, 0.f, 1.f);
                }

                // 📝 Shared tangent-space encoder for normal/bent-normal/bevel-normal: project Direction onto
                // the low mesh's own tangent frame (built from WorldNormal, not the hit normal) - the only
                // three outputs the NormalPlusY flip applies to (baking-studio.md: "flips green for tangent,
                // bent-tangent and bevel-normal outputs only. Object/world vector normals are not altered").
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
                const float MaxFrontBack = std::max(1e-6f, std::max(Settings.Front, Settings.Back));

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
                        float Signed = Settings.NormalPlusY ? Hit.Normal[1] : -Hit.Normal[1];
                        Out[0] = ToByte(Hit.Normal[0] * 0.5f + 0.5f);
                        Out[1] = ToByte(Signed * 0.5f + 0.5f);
                        Out[2] = ToByte(Hit.Normal[2] * 0.5f + 0.5f);
                        Out[3] = 255;
                    }
                    else if (HasHit && Map.Key == "normal") { EncodeTangentSpace(Hit.Normal, Out); Out[3] = 255; } // tangent normal
                    else if (Map.Key == "object-normal")
                    {
                        float LocalNormal[3];
                        TransformLocalNormal(Part.ObjectToWorld, NH, LocalNormal);
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
                        TransformLocalNormal(Part.ObjectToWorld, Bent, LocalBent);
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
                            Out[Axis] = ToByte(Diagonal[Axis] > 1e-9f ? (Q[Axis] - BoundsMinimum[Axis]) / Diagonal[Axis] : 0.5f);
                        Out[3] = 255;
                    }
                    else if (Map.Key == "id")
                    {
                        const uint32_t Id = HasHit ? Hit.PartIndex + 1 : 0;
                        Out[0] = uint8_t(Id & 255); Out[1] = uint8_t((Id >> 8) & 255); Out[2] = uint8_t((Id >> 16) & 255);
                        Out[3] = 255;
                    }
                    else if (Map.Key == "uv-island" && Covered) { UvIslandColor(WinningA->ChartId, Out); Out[3] = 255; }
                    else if (Map.Key == "uv" && Covered)
                    {
                        Out[0] = ToByte(PixelU); Out[1] = ToByte(PixelV); Out[2] = 255; Out[3] = 255;
                    }
                    else if (Map.Key == "wireframe" && Covered && WinningA)
                    {
                        const float Ua[2]{WinningA->Uv[0] * Resolution, WinningA->Uv[1] * Resolution};
                        const float Ub[2]{WinningB->Uv[0] * Resolution, WinningB->Uv[1] * Resolution};
                        const float Uc[2]{WinningC->Uv[0] * Resolution, WinningC->Uv[1] * Resolution};
                        const float Area = std::fabs((Ub[0] - Ua[0]) * (Uc[1] - Ua[1]) - (Ub[1] - Ua[1]) * (Uc[0] - Ua[0]));
                        const auto EdgeLength = [](const float P0[2], const float P1[2]) { return std::max(1e-9f, std::sqrt((P0[0] - P1[0]) * (P0[0] - P1[0]) + (P0[1] - P1[1]) * (P0[1] - P1[1]))); };
                        const float DistanceA = std::fabs(WinningWeightA) * Area / EdgeLength(Ub, Uc);
                        const float DistanceB = std::fabs(WinningWeightB) * Area / EdgeLength(Uc, Ua);
                        const float DistanceC = std::fabs(WinningWeightC) * Area / EdgeLength(Ua, Ub);
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
                    if (!Settings.MapOutput.empty())
                    {
                        const auto Found = Settings.MapOutput.find(Map.Key);
                        if (Found != Settings.MapOutput.end()) ApplyBakeMapOutput(Out, Found->second);
                    }
                    Map.Rgba[Pixel + 0] = Out[0];
                    Map.Rgba[Pixel + 1] = Out[1];
                    Map.Rgba[Pixel + 2] = Out[2];
                    Map.Rgba[Pixel + 3] = Out[3];
                }
                ++Done;
            }
            if (OnProgress)
                OnProgress(BakeProgress{Part.Name, TargetIndex, uint32_t(Targets.size()), Done, TotalTexels});
        }
        Results.push_back(std::move(Result));
    }
    return Results;
}
} // namespace Frontier
