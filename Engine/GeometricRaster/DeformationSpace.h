//============================================================================================================================================
//                                                             DEFORMATIONSPACE.H
//============================================================================================================================================
// 📦 Correctness-first closed-surface signed fields. CPU BVH reconstruction; no real-time update claim.

#pragma once

#include "DistanceFieldSpace.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <numeric>

namespace Frontier
{

class DeformationSpace
{
public:
    using Face = std::array<uint32_t, 3>;

    // Transactional: rejected snapshots leave the last valid field AND its revision unchanged. The caller must
    // stop publication on failure, not present that older field as the rejected snapshot's visibility.
    bool Update(const std::vector<Vector3>& Positions, const std::vector<Face>& Faces,
                uint32_t Resolution, float Padding, std::string& Refusal)
    {
        Refusal.clear();
        if (Positions.empty() || Faces.empty() || Resolution < 8u || Resolution > 256u ||
            !std::isfinite(Padding) || Padding <= 0.0f)
        {
            Refusal = "invalid signed-field geometry or grid specification";
            return false;
        }
        if (Positions.size() == Vertices.size() && Faces == Triangles && Resolution == GridResolution && Padding == GridPadding &&
            std::memcmp(Positions.data(), Vertices.data(), Positions.size() * sizeof(Vector3)) == 0) return true;

        DeformationSpace Candidate;
        Candidate.Vertices = Positions;
        Candidate.Triangles = Faces;
        Candidate.GridResolution = Resolution;
        Candidate.GridPadding = Padding;
        std::map<std::pair<uint32_t, uint32_t>, std::pair<uint32_t, int32_t>> Edges;
        for (const Vector3& Position : Positions)
            if (!std::isfinite(Position.x) || !std::isfinite(Position.y) || !std::isfinite(Position.z))
            {
                Refusal = "nonfinite signed-field vertex";
                return false;
            }
        for (const Face& Triangle : Faces)
        {
            for (uint32_t Index : Triangle)
                if (Index >= Positions.size()) { Refusal = "signed-field index outside vertex span"; return false; }
            const float AreaSquared = Cross(Positions[Triangle[1]] - Positions[Triangle[0]],
                                            Positions[Triangle[2]] - Positions[Triangle[0]]).LengthSquared();
            if (!std::isfinite(AreaSquared) || AreaSquared < 1.0e-18f)
            {
                Refusal = "degenerate signed-field triangle";
                return false;
            }
            for (uint32_t Corner = 0u; Corner < 3u; ++Corner)
            {
                const uint32_t A = Triangle[Corner], B = Triangle[(Corner + 1u) % 3u];
                auto& Edge = Edges[{std::min(A, B), std::max(A, B)}];
                ++Edge.first;
                Edge.second += A < B ? 1 : -1;
            }
        }
        for (const auto& Edge : Edges)
            if (Edge.second.first != 2u || Edge.second.second != 0)
            {
                Refusal = "signed field requires a closed consistently oriented indexed surface";
                return false;
            }
        Candidate.Order.resize(Faces.size());
        std::iota(Candidate.Order.begin(), Candidate.Order.end(), 0u);
        Candidate.Build(0u, static_cast<uint32_t>(Faces.size()));
        const Vector3 Margin{Padding, Padding, Padding};
        const Vector3 Minimum = Candidate.Branches[0].Minimum - Margin;
        const Vector3 Maximum = Candidate.Branches[0].Maximum + Margin;
        Candidate.Field = DistanceFieldSpace(Resolution, Resolution, Resolution, Minimum, Maximum);
        const Vector3 Spacing = Candidate.Field.GetVoxelSpacing();
        for (uint32_t Z = 0u; Z < Resolution; ++Z)
            for (uint32_t Y = 0u; Y < Resolution; ++Y)
                for (uint32_t X = 0u; X < Resolution; ++X)
                {
                    const Vector3 Point = Minimum + Spacing * Vector3{X + 0.5f, Y + 0.5f, Z + 0.5f};
                    Candidate.Field.SetVoxelSample(X, Y, Z, Candidate.Distance(Point));
                }
        Candidate.Revision = Revision + 1u;
        *this = std::move(Candidate);
        return true;
    }

    [[nodiscard]] const DistanceFieldSpace& QueryField() const noexcept { return Field; }
    [[nodiscard]] uint64_t QueryRevision() const noexcept { return Revision; }
    [[nodiscard]] const std::vector<Vector3>& QueryVertices() const noexcept { return Vertices; }
    [[nodiscard]] const std::vector<Face>& QueryTriangles() const noexcept { return Triangles; }

    // Reference query against the CURRENT triangles, independent of voxel interpolation.
    [[nodiscard]] float Distance(Vector3 Point) const
    {
        if (Branches.empty()) return std::numeric_limits<float>::infinity();
        float Squared = std::numeric_limits<float>::max();
        Nearest(0u, Point, Squared);
        const Vector3 Direction = Vector3{1.0f, 0.37139067f, 0.529113f}.Normalized();
        return std::sqrt(Squared) * ((Crossings(0u, Point, Direction) & 1u) ? -1.0f : 1.0f);
    }

    // Non-cryptographic content signature covers geometry, topology, grid and algorithm version. Cache consumers
    // must compare it before loading an SDF1 payload. A bare SDF1 file is NOT a self-validating deformation cache.
    [[nodiscard]] uint64_t Signature() const noexcept
    {
        uint64_t Hash = 14695981039346656037ull;
        auto Mix = [&](const void* Data, size_t Count)
        {
            const auto* Bytes = static_cast<const unsigned char*>(Data);
            for (size_t Index = 0u; Index < Count; ++Index) { Hash ^= Bytes[Index]; Hash *= 1099511628211ull; }
        };
        const uint32_t Algorithm = 1u;
        Mix(&Algorithm, sizeof(Algorithm)); Mix(&GridResolution, sizeof(GridResolution)); Mix(&GridPadding, sizeof(GridPadding));
        for (const Vector3& Vertex : Vertices) { Mix(&Vertex.x, sizeof(float)); Mix(&Vertex.y, sizeof(float)); Mix(&Vertex.z, sizeof(float)); }
        for (const Face& Triangle : Triangles) Mix(Triangle.data(), 3u * sizeof(uint32_t));
        return Hash;
    }

private:
    struct Branch
    {
        Vector3 Minimum{1.0e30f, 1.0e30f, 1.0e30f}, Maximum{-1.0e30f, -1.0e30f, -1.0e30f};
        uint32_t First = 0u, Count = 0u, Left = 0u, Right = 0u;
    };
    static float Dot(Vector3 A, Vector3 B) { return A.x * B.x + A.y * B.y + A.z * B.z; }
    static Vector3 Cross(Vector3 A, Vector3 B)
    {
        return {A.y * B.z - A.z * B.y, A.z * B.x - A.x * B.z, A.x * B.y - A.y * B.x};
    }
    static float Coordinate(Vector3 Point, uint32_t Axis) { return Axis == 0u ? Point.x : Axis == 1u ? Point.y : Point.z; }
    uint32_t Build(uint32_t First, uint32_t Count)
    {
        const uint32_t Slot = static_cast<uint32_t>(Branches.size());
        Branches.emplace_back();
        Branch Local;
        Local.First = First; Local.Count = Count;
        for (uint32_t Index = First; Index < First + Count; ++Index)
            for (uint32_t Corner : Triangles[Order[Index]])
            {
                const Vector3 Point = Vertices[Corner];
                Local.Minimum = {std::min(Local.Minimum.x, Point.x), std::min(Local.Minimum.y, Point.y), std::min(Local.Minimum.z, Point.z)};
                Local.Maximum = {std::max(Local.Maximum.x, Point.x), std::max(Local.Maximum.y, Point.y), std::max(Local.Maximum.z, Point.z)};
            }
        if (Count > 8u)
        {
            const Vector3 Extent = Local.Maximum - Local.Minimum;
            const uint32_t Axis = Extent.x > Extent.y ? (Extent.x > Extent.z ? 0u : 2u) : (Extent.y > Extent.z ? 1u : 2u);
            const uint32_t Middle = First + Count / 2u;
            auto Centre = [&](uint32_t Index)
            {
                const Face& Triangle = Triangles[Index];
                return Coordinate(Vertices[Triangle[0]] + Vertices[Triangle[1]] + Vertices[Triangle[2]], Axis);
            };
            std::nth_element(Order.begin() + First, Order.begin() + Middle, Order.begin() + First + Count,
                             [&](uint32_t A, uint32_t B) { return Centre(A) < Centre(B); });
            Local.Left = Build(First, Middle - First);
            Local.Right = Build(Middle, First + Count - Middle);
            Local.Count = 0u;
        }
        Branches[Slot] = Local;
        return Slot;
    }
    static float BoxDistance(const Branch& Node, Vector3 Point)
    {
        const Vector3 Delta{std::max({Node.Minimum.x - Point.x, 0.0f, Point.x - Node.Maximum.x}),
                            std::max({Node.Minimum.y - Point.y, 0.0f, Point.y - Node.Maximum.y}),
                            std::max({Node.Minimum.z - Point.z, 0.0f, Point.z - Node.Maximum.z})};
        return Delta.LengthSquared();
    }
    float TriangleDistance(Vector3 Point, const Face& Triangle) const
    {
        const Vector3 A = Vertices[Triangle[0]], B = Vertices[Triangle[1]], C = Vertices[Triangle[2]];
        const Vector3 Normal = Cross(B - A, C - A);
        const float Plane = Dot(Point - A, Normal);
        const Vector3 Projected = Point - Normal * (Plane / Normal.LengthSquared());
        if (Dot(Cross(B - A, Projected - A), Normal) >= 0.0f &&
            Dot(Cross(C - B, Projected - B), Normal) >= 0.0f && Dot(Cross(A - C, Projected - C), Normal) >= 0.0f)
            return Plane * Plane / Normal.LengthSquared();
        float Best = std::numeric_limits<float>::max();
        for (uint32_t Edge = 0u; Edge < 3u; ++Edge)
        {
            const Vector3 Start = Vertices[Triangle[Edge]], Delta = Vertices[Triangle[(Edge + 1u) % 3u]] - Start;
            const float Parameter = std::clamp(Dot(Point - Start, Delta) / Delta.LengthSquared(), 0.0f, 1.0f);
            Best = std::min(Best, (Point - Start - Delta * Parameter).LengthSquared());
        }
        return Best;
    }
    void Nearest(uint32_t Slot, Vector3 Point, float& Best) const
    {
        const Branch& Node = Branches[Slot];
        if (BoxDistance(Node, Point) > Best) return;
        if (Node.Count != 0u)
        {
            for (uint32_t Index = Node.First; Index < Node.First + Node.Count; ++Index)
                Best = std::min(Best, TriangleDistance(Point, Triangles[Order[Index]]));
            return;
        }
        const bool LeftFirst = BoxDistance(Branches[Node.Left], Point) < BoxDistance(Branches[Node.Right], Point);
        Nearest(LeftFirst ? Node.Left : Node.Right, Point, Best);
        Nearest(LeftFirst ? Node.Right : Node.Left, Point, Best);
    }
    uint32_t Crossings(uint32_t Slot, Vector3 Origin, Vector3 Direction) const
    {
        const Branch& Node = Branches[Slot];
        float Near = 0.0f, Far = 1.0e30f;
        for (uint32_t Axis = 0u; Axis < 3u; ++Axis)
        {
            const float A = (Coordinate(Node.Minimum, Axis) - Coordinate(Origin, Axis)) / Coordinate(Direction, Axis);
            const float B = (Coordinate(Node.Maximum, Axis) - Coordinate(Origin, Axis)) / Coordinate(Direction, Axis);
            Near = std::max(Near, std::min(A, B)); Far = std::min(Far, std::max(A, B));
        }
        if (Near > Far) return 0u;
        if (Node.Count == 0u) return Crossings(Node.Left, Origin, Direction) + Crossings(Node.Right, Origin, Direction);
        uint32_t Count = 0u;
        for (uint32_t Index = Node.First; Index < Node.First + Node.Count; ++Index)
        {
            const Face& Triangle = Triangles[Order[Index]];
            const Vector3 A = Vertices[Triangle[0]], Edge = Vertices[Triangle[1]] - A, Other = Vertices[Triangle[2]] - A;
            const Vector3 Perpendicular = Cross(Direction, Other);
            const float Determinant = Dot(Edge, Perpendicular);
            if (std::abs(Determinant) < 1.0e-10f) continue;
            const Vector3 Relative = Origin - A, Crossed = Cross(Relative, Edge);
            const float U = Dot(Relative, Perpendicular) / Determinant, V = Dot(Direction, Crossed) / Determinant;
            const float Travel = Dot(Other, Crossed) / Determinant;
            if (U >= 0.0f && V >= 0.0f && U + V <= 1.0f && Travel > 1.0e-7f) ++Count;
        }
        return Count;
    }
    std::vector<Vector3> Vertices;
    std::vector<Face> Triangles;
    std::vector<uint32_t> Order;
    std::vector<Branch> Branches;
    DistanceFieldSpace Field;
    uint32_t GridResolution = 0u;
    float GridPadding = 0.0f;
    uint64_t Revision = 0u;
};

} // namespace Frontier
