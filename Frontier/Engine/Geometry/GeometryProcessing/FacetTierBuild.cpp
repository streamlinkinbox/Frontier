/*====================================================================================================================================
                                                        FACETTIERBUILD.CPP
====================================================================================================================================*/
// 🧩 Implementation of the facet weld, edge-incidence classification, cluster partition and validation declared in FacetTierBuild.h.

#include "Engine/Geometry/GeometryProcessing/FacetTierBuild.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

static FacetPoint3D Subtract(const FacetPoint3D& A, const FacetPoint3D& B) { return { A.X - B.X, A.Y - B.Y, A.Z - B.Z }; }
static double Dot(const FacetPoint3D& A, const FacetPoint3D& B) { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
static FacetPoint3D Cross(const FacetPoint3D& A, const FacetPoint3D& B)
{
    return { A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X };
}
static double Length(const FacetPoint3D& A) { return std::sqrt(Dot(A, A)); }
static FacetPoint3D Normalized(const FacetPoint3D& A)
{
    const double L = Length(A);
    return L > 1.0e-12 ? FacetPoint3D{ A.X / L, A.Y / L, A.Z / L } : FacetPoint3D{ 0.0, 0.0, 0.0 };
}

static FacetPoint3D TriangleNormal(const PolygonComplex& Complex, uint32_t Triangle)
{
    const FacetPoint3D& A = Complex.Positions[Complex.Triangles[Triangle * 3 + 0]];
    const FacetPoint3D& B = Complex.Positions[Complex.Triangles[Triangle * 3 + 1]];
    const FacetPoint3D& C = Complex.Positions[Complex.Triangles[Triangle * 3 + 2]];
    return Cross(Subtract(B, A), Subtract(C, A));
}

// 📝 Undirected edge key over welded vertex indices.
static uint64_t EdgeKey(uint32_t A, uint32_t B)
{
    if (A > B) std::swap(A, B);
    return (uint64_t(A) << 32) | uint64_t(B);
}

struct EdgeIncidence
{
    uint32_t              A = 0, B = 0;        // [idx] - welded endpoints, A < B
    std::vector<uint32_t> Triangles;           // [idx] - incident triangles
    bool                  Sharp = false;       // [-] - any incident facet mask marked this edge
    bool                  UvSeam = false;      // [-] - corner UVs disagree across the edge
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

WeldOutcome WeldFacetField(const FacetField& Source, const WeldSpecification& Specification)
{
    WeldOutcome Outcome;
    PolygonComplex& Complex = Outcome.Complex;
    Complex.CarriesUVs = !Source.CornerUVs.empty();
    const size_t FacetCount = Source.Classification.size();

    // ① Weld corners by exact position (string-exact in the JS reference; tuple hash here, same semantics).
    struct PositionHash
    {
        size_t operator()(const std::array<uint64_t, 3>& Key) const
        {
            size_t H = Key[0] * 0x9E3779B97F4A7C15ull;
            H ^= Key[1] * 0xC2B2AE3D27D4EB4Full + (H << 6) + (H >> 2);
            H ^= Key[2] * 0x165667B19E3779F9ull + (H << 6) + (H >> 2);
            return H;
        }
    };
    std::unordered_map<std::array<uint64_t, 3>, uint32_t, PositionHash> Welded;
    auto Weld = [&](const FacetPoint3D& P) -> uint32_t
    {
        // 📝 +0.0 folds -0.0 onto +0.0 exactly as the JS reference's (P + 0) key does.
        const std::array<uint64_t, 3> Key = { std::bit_cast<uint64_t>(P.X + 0.0),
                                              std::bit_cast<uint64_t>(P.Y + 0.0),
                                              std::bit_cast<uint64_t>(P.Z + 0.0) };
        const auto Found = Welded.find(Key);
        if (Found != Welded.end()) return Found->second;
        const uint32_t Index = uint32_t(Complex.Positions.size());
        Welded.emplace(Key, Index);
        Complex.Positions.push_back(P);
        return Index;
    };

    Complex.Triangles.reserve(FacetCount * 3);
    Complex.CornerUVs.reserve(FacetCount * 3);
    for (size_t Facet = 0; Facet < FacetCount; ++Facet)
    {
        for (uint32_t Corner = 0; Corner < 3; ++Corner)
        {
            Complex.Triangles.push_back(Weld(Source.Corners[Facet * 3 + Corner]));
            Complex.CornerUVs.push_back(Complex.CarriesUVs ? Source.CornerUVs[Facet * 3 + Corner]
                                                       : std::array<double, 2>{ 0.0, 0.0 });
        }
        Complex.Classification.push_back(Source.Classification[Facet]);
        Complex.SharpMask.push_back(Facet < Source.SharpMask.size() ? uint8_t(Source.SharpMask[Facet] & 7) : uint8_t(0));
    }

    // ② Edge incidence: incident triangles, authored-mask sharpness, UV disagreement across the edge.
    std::unordered_map<uint64_t, EdgeIncidence> Edges;
    const uint32_t TriangleCount = uint32_t(FacetCount);
    for (uint32_t Triangle = 0; Triangle < TriangleCount; ++Triangle)
    {
        for (uint32_t Corner = 0; Corner < 3; ++Corner)
        {
            const uint32_t A = Complex.Triangles[Triangle * 3 + Corner];
            const uint32_t B = Complex.Triangles[Triangle * 3 + (Corner + 1) % 3];
            EdgeIncidence& Edge = Edges[EdgeKey(A, B)];
            Edge.A = std::min(A, B);
            Edge.B = std::max(A, B);
            Edge.Triangles.push_back(Triangle);
            if (Complex.SharpMask[Triangle] & (1u << Corner)) Edge.Sharp = true;
        }
    }
    if (Complex.CarriesUVs)
    {
        for (auto& [Key, Edge] : Edges)
        {
            if (Edge.Triangles.size() != 2) continue;
            for (const uint32_t Endpoint : { Edge.A, Edge.B })
            {
                std::array<double, 2> Uv[2];
                for (int Side = 0; Side < 2; ++Side)
                {
                    const uint32_t Triangle = Edge.Triangles[Side];
                    for (uint32_t Corner = 0; Corner < 3; ++Corner)
                        if (Complex.Triangles[Triangle * 3 + Corner] == Endpoint)
                            Uv[Side] = Complex.CornerUVs[Triangle * 3 + Corner];
                }
                if (Uv[0] != Uv[1]) Edge.UvSeam = true;
            }
        }
    }

    // ③ Crease-angle sharpness, then propagate every sharp edge back into per-triangle masks.
    for (auto& [Key, Edge] : Edges)
    {
        if (!Edge.Sharp && Specification.UseCreaseAngle && Edge.Triangles.size() == 2)
        {
            const FacetPoint3D N0 = Normalized(TriangleNormal(Complex, Edge.Triangles[0]));
            const FacetPoint3D N1 = Normalized(TriangleNormal(Complex, Edge.Triangles[1]));
            if (Dot(N0, N1) < Specification.CreaseCosine) Edge.Sharp = true;
        }
        if (Edge.Sharp)
        {
            ++Outcome.SharpEdgeCount;
            for (const uint32_t Triangle : Edge.Triangles)
                for (uint32_t Corner = 0; Corner < 3; ++Corner)
                {
                    const uint32_t A = Complex.Triangles[Triangle * 3 + Corner];
                    const uint32_t B = Complex.Triangles[Triangle * 3 + (Corner + 1) % 3];
                    if ((A == Edge.A && B == Edge.B) || (A == Edge.B && B == Edge.A))
                        Complex.SharpMask[Triangle] |= uint8_t(1u << Corner);
                }
        }
        if (Edge.UvSeam) ++Outcome.UvSeamEdgeCount;
        if (Edge.Triangles.size() == 1) ++Outcome.BoundaryEdgeCount;
    }
    return Outcome;
}

std::vector<FacetCluster> PartitionFacetClusters(const PolygonComplex& Complex, uint32_t TriangleLimit)
{
    const uint32_t TriangleCount = uint32_t(Complex.Triangles.size() / 3);

    // ① Triangle adjacency across coincident welded edges.
    std::unordered_map<uint64_t, std::vector<uint32_t>> EdgeTriangles;
    for (uint32_t Triangle = 0; Triangle < TriangleCount; ++Triangle)
        for (uint32_t Corner = 0; Corner < 3; ++Corner)
            EdgeTriangles[EdgeKey(Complex.Triangles[Triangle * 3 + Corner],
                                  Complex.Triangles[Triangle * 3 + (Corner + 1) % 3])].push_back(Triangle);
    std::vector<std::vector<uint32_t>> Adjacency(TriangleCount);
    for (const auto& [Key, Members] : EdgeTriangles)
        if (Members.size() == 2)
        {
            Adjacency[Members[0]].push_back(Members[1]);
            Adjacency[Members[1]].push_back(Members[0]);
        }

    // ② Greedy connected growth: seed at the lowest unassigned triangle, absorb same-classification
    //    neighbours until the cap; repeat until every triangle is claimed exactly once.
    std::vector<char> Assigned(TriangleCount, 0);
    std::vector<FacetCluster> Clusters;
    for (uint32_t Seed = 0; Seed < TriangleCount; ++Seed)
    {
        if (Assigned[Seed]) continue;
        FacetCluster Cluster;
        std::queue<uint32_t> Frontier;   // per-cluster: a coincident queue would absorb disconnected leftovers
        Cluster.Classification = Complex.Classification[Seed];
        std::unordered_set<uint32_t> InFrontier{ Seed };
        Frontier.push(Seed);
        while (!Frontier.empty() && Cluster.Triangles.size() < TriangleLimit)
        {
            const uint32_t Triangle = Frontier.front();
            Frontier.pop();
            if (Assigned[Triangle]) continue;
            Assigned[Triangle] = 1;
            Cluster.Triangles.push_back(Triangle);
            for (const uint32_t Neighbour : Adjacency[Triangle])
            {
                if (Assigned[Neighbour] || InFrontier.count(Neighbour)) continue;
                if (Complex.Classification[Neighbour] != Cluster.Classification) continue;
                InFrontier.insert(Neighbour);
                Frontier.push(Neighbour);
            }
        }
        // 📝 Bounds sphere (Ritter expansion) and normal cone over member face normals.
        FacetPoint3D Centre{ 0.0, 0.0, 0.0 };
        for (const uint32_t Triangle : Cluster.Triangles)
            for (uint32_t Corner = 0; Corner < 3; ++Corner)
            {
                const FacetPoint3D& P = Complex.Positions[Complex.Triangles[Triangle * 3 + Corner]];
                Centre.X += P.X; Centre.Y += P.Y; Centre.Z += P.Z;
            }
        const double CornerCount = double(Cluster.Triangles.size() * 3);
        Centre = { Centre.X / CornerCount, Centre.Y / CornerCount, Centre.Z / CornerCount };
        double Radius = 0.0;
        for (const uint32_t Triangle : Cluster.Triangles)
            for (uint32_t Corner = 0; Corner < 3; ++Corner)
                Radius = std::max(Radius, Length(Subtract(Complex.Positions[Complex.Triangles[Triangle * 3 + Corner]], Centre)));
        FacetPoint3D Axis{ 0.0, 0.0, 0.0 };
        for (const uint32_t Triangle : Cluster.Triangles)
        {
            const FacetPoint3D N = Normalized(TriangleNormal(Complex, Triangle));
            Axis.X += N.X; Axis.Y += N.Y; Axis.Z += N.Z;
        }
        Axis = Normalized(Axis);
        double Cutoff = 1.0;
        for (const uint32_t Triangle : Cluster.Triangles)
            Cutoff = std::min(Cutoff, Dot(Normalized(TriangleNormal(Complex, Triangle)), Axis));
        Cluster.SphereCentre = Centre;
        Cluster.SphereRadius = Radius;
        Cluster.ConeAxis     = Axis;
        Cluster.ConeCutoff   = Cutoff;
        Clusters.push_back(std::move(Cluster));
    }
    return Clusters;
}

std::vector<std::string> ValidateClusterBuild(const PolygonComplex& Complex,
                                              const std::vector<FacetCluster>& Clusters,
                                              uint32_t TriangleLimit)
{
    std::vector<std::string> Problems;
    const uint32_t TriangleCount = uint32_t(Complex.Triangles.size() / 3);
    std::vector<uint32_t> Coverage(TriangleCount, 0);
    for (size_t Index = 0; Index < Clusters.size(); ++Index)
    {
        const FacetCluster& Cluster = Clusters[Index];
        if (Cluster.Triangles.empty()) Problems.push_back("cluster " + std::to_string(Index) + " is empty");
        if (Cluster.Triangles.size() > TriangleLimit)
            Problems.push_back("cluster " + std::to_string(Index) + " exceeds the triangle limit");
        std::unordered_set<uint32_t> Members(Cluster.Triangles.begin(), Cluster.Triangles.end());
        for (const uint32_t Triangle : Cluster.Triangles)
        {
            if (Triangle >= TriangleCount) { Problems.push_back("cluster references an out-of-range triangle"); continue; }
            ++Coverage[Triangle];
            if (Complex.Classification[Triangle] != Cluster.Classification)
                Problems.push_back("cluster " + std::to_string(Index) + " mixes classifications");
            for (uint32_t Corner = 0; Corner < 3; ++Corner)
            {
                const FacetPoint3D& P = Complex.Positions[Complex.Triangles[Triangle * 3 + Corner]];
                const double D = Length(Subtract(P, Cluster.SphereCentre));
                if (D > Cluster.SphereRadius + 1.0e-9)
                    Problems.push_back("cluster " + std::to_string(Index) + " bounds sphere does not contain a corner");
            }
        }
        // 📝 Connectivity: BFS within the member set over coincident edges must reach every member.
        if (Cluster.Triangles.size() > 1)
        {
            std::unordered_map<uint64_t, std::vector<uint32_t>> LocalEdges;
            for (const uint32_t Triangle : Cluster.Triangles)
                for (uint32_t Corner = 0; Corner < 3; ++Corner)
                    LocalEdges[EdgeKey(Complex.Triangles[Triangle * 3 + Corner],
                                       Complex.Triangles[Triangle * 3 + (Corner + 1) % 3])].push_back(Triangle);
            std::unordered_set<uint32_t> Reached{ Cluster.Triangles.front() };
            std::queue<uint32_t> Frontier;
            Frontier.push(Cluster.Triangles.front());
            while (!Frontier.empty())
            {
                const uint32_t Triangle = Frontier.front();
                Frontier.pop();
                for (uint32_t Corner = 0; Corner < 3; ++Corner)
                    for (const uint32_t Other : LocalEdges[EdgeKey(Complex.Triangles[Triangle * 3 + Corner],
                                                                   Complex.Triangles[Triangle * 3 + (Corner + 1) % 3])])
                        if (Members.count(Other) && !Reached.count(Other))
                        {
                            Reached.insert(Other);
                            Frontier.push(Other);
                        }
            }
            if (Reached.size() != Members.size())
                Problems.push_back("cluster " + std::to_string(Index) + " is not edge-connected");
        }
    }
    for (uint32_t Triangle = 0; Triangle < TriangleCount; ++Triangle)
        if (Coverage[Triangle] != 1)
            Problems.push_back("triangle " + std::to_string(Triangle) + " covered " +
                               std::to_string(Coverage[Triangle]) + " times");
    return Problems;
}

}   // namespace Frontier
