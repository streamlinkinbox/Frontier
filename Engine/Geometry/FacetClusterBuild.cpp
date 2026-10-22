/*==========================================================================================================================================
                                                        FACETCLUSTERBUILD.CPP
==========================================================================================================================================*/
// 🧩 Implementation of the facet weld, edge-incidence classification, scored cluster partition, cluster measurement and partition
//    validation declared in FacetClusterBuild.h.

#include "Engine/Geometry/FacetClusterBuild.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Frontier
{

//--------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL CONSTANTS
//--------------------------------------------------------------------------------------------------------------------------

// 📝 Reference scoring constants (GeometryHierarchy.js, BuildMeshlets). Changing any one changes the reference cluster counts.
static constexpr double SeamCrossPenalty    = 0.35;   // [-] - score penalty for joining only across a seam or crease
static constexpr double DistanceWeight      = 0.30;   // [-] - weight on centroid distance / expected radius
static constexpr double MinimumExpectedSpan = 1.0e-12; // [m] - floor for the expected cluster radius
static constexpr double UvMatchTolerance    = 1.0e-5; // [-] - UV wedge identity tolerance
static constexpr double ContainmentSlack    = 1.0e-9; // [m] - sphere/enclosure containment slack for round-off
static constexpr double PiConstant          = 3.14159265358979323846; // [-] - circle ratio for the expected-radius term

//--------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL VECTOR OPS
//--------------------------------------------------------------------------------------------------------------------------

static SpatialPosition Subtract(const SpatialPosition &A, const SpatialPosition &B)
{
    return { A.X - B.X, A.Y - B.Y, A.Z - B.Z };
}

static double Dot(const SpatialPosition &A, const SpatialPosition &B)
{
    return A.X * B.X + A.Y * B.Y + A.Z * B.Z;
}

static SpatialPosition Cross(const SpatialPosition &A, const SpatialPosition &B)
{
    return { A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X };
}

static double Length(const SpatialPosition &A)
{
    return std::hypot(A.X, A.Y, A.Z);
}

// 📝 The reference returns +Z for a zero-length axis rather than a zero vector, so a cone axis is always a usable direction.
static SpatialPosition Normalized(const SpatialPosition &A)
{
    const double Magnitude = Length(A);
    if (Magnitude > 0.0) return { A.X / Magnitude, A.Y / Magnitude, A.Z / Magnitude };
    return { 0.0, 0.0, 1.0 };
}

static SpatialPosition ReadPosition(const PolygonComplex &Complex, uint32_t Vertex)
{
    return Complex.Positions[Vertex];
}

static SpatialPosition TriangleNormal(const PolygonComplex &Complex, uint32_t Triangle)
{
    const SpatialPosition &A = Complex.Positions[Complex.Triangles[std::size_t(Triangle) * 3 + 0]];
    const SpatialPosition &B = Complex.Positions[Complex.Triangles[std::size_t(Triangle) * 3 + 1]];
    const SpatialPosition &C = Complex.Positions[Complex.Triangles[std::size_t(Triangle) * 3 + 2]];
    return Cross(Subtract(B, A), Subtract(C, A));
}

static SpatialPosition TriangleCentroid(const PolygonComplex &Complex, uint32_t Triangle)
{
    const SpatialPosition A = ReadPosition(Complex, Complex.Triangles[std::size_t(Triangle) * 3 + 0]);
    const SpatialPosition B = ReadPosition(Complex, Complex.Triangles[std::size_t(Triangle) * 3 + 1]);
    const SpatialPosition C = ReadPosition(Complex, Complex.Triangles[std::size_t(Triangle) * 3 + 2]);
    return { (A.X + B.X + C.X) / 3.0, (A.Y + B.Y + C.Y) / 3.0, (A.Z + B.Z + C.Z) / 3.0 };
}

//--------------------------------------------------------------------------------------------------------------------------
//                                                    EDGE INCIDENCE
//--------------------------------------------------------------------------------------------------------------------------

// 📝 One undirected welded edge and its incident facets. Built once, then shared by classification, partition and validation.
struct EdgeIncidence
{
    uint32_t              A = 0;              // [idx] - lower welded endpoint
    uint32_t              B = 0;              // [idx] - upper welded endpoint
    std::vector<uint32_t> Triangles;          // [idx] - incident facets
    bool                  Sharp   = false;    // [-]   - marked sharp, imported or by crease
    bool                  UvSeam  = false;    // [-]   - corner UVs disagree across the edge
    bool                  HasFirstUv = false; // [-]   - a first corner-UV pair has been recorded
    std::array<double, 2> FirstUvA{};         // [-]   - UV at the lower endpoint of the first incident corner
    std::array<double, 2> FirstUvB{};         // [-]   - UV at the upper endpoint of the first incident corner
};

struct IncidenceStructure
{
    std::unordered_map<uint64_t, EdgeIncidence> Edges;
    std::vector<std::vector<uint32_t>>          Adjacent;            // [idx] - per facet, ascending neighbour facets
    std::unordered_set<uint64_t>                FeatureSeamPairs;    // [-]   - facet pairs joined only across a seam or crease
    uint32_t                                    SharpEdgeCount    = 0;
    uint32_t                                    UvSeamEdgeCount   = 0;
    uint32_t                                    BoundaryEdgeCount = 0;
};

static uint64_t EdgeIdentity(uint32_t A, uint32_t B)
{
    if (A > B) std::swap(A, B);
    return (uint64_t(A) << 32) | uint64_t(B);
}

static uint64_t FacetPairIdentity(uint32_t A, uint32_t B)
{
    if (A > B) std::swap(A, B);
    return (uint64_t(A) << 32) | uint64_t(B);
}

static std::array<double, 2> ReadCornerUv(const PolygonComplex &Complex, uint32_t Triangle, uint32_t Corner)
{
    const std::size_t Offset = std::size_t(Triangle) * 3 + Corner;
    if (Offset >= Complex.CornerUVs.size()) return { 0.0, 0.0 };
    return Complex.CornerUVs[Offset];
}

static double UvDistance(const std::array<double, 2> &A, const std::array<double, 2> &B)
{
    return std::hypot(A[0] - B[0], A[1] - B[1]);
}

// Build the incidence structure. Marks authored sharp bits and UV disagreement; crease classification is applied by the caller
// because it depends on the requested angle.
static IncidenceStructure BuildIncidence(const PolygonComplex &Complex)
{
    IncidenceStructure Incidence;
    const uint32_t TriangleCount = uint32_t(Complex.Triangles.size() / 3);
    for (uint32_t Triangle = 0; Triangle < TriangleCount; ++Triangle)
    {
        for (uint32_t Corner = 0; Corner < 3; ++Corner)
        {
            const uint32_t V0 = Complex.Triangles[std::size_t(Triangle) * 3 + Corner];
            const uint32_t V1 = Complex.Triangles[std::size_t(Triangle) * 3 + (Corner + 1) % 3];
            EdgeIncidence &Edge = Incidence.Edges[EdgeIdentity(V0, V1)];
            Edge.A = std::min(V0, V1);
            Edge.B = std::max(V0, V1);
            Edge.Triangles.push_back(Triangle);
            if (std::size_t(Triangle) < Complex.SharpMask.size() && (Complex.SharpMask[Triangle] & (1u << Corner)) != 0)
                Edge.Sharp = true;
            if (Complex.HasUVs)
            {
                const uint32_t CornerA = V0 == Edge.A ? Corner : (Corner + 1) % 3;
                const uint32_t CornerB = V0 == Edge.A ? (Corner + 1) % 3 : Corner;
                const std::array<double, 2> UvA = ReadCornerUv(Complex, Triangle, CornerA);
                const std::array<double, 2> UvB = ReadCornerUv(Complex, Triangle, CornerB);
                if (!Edge.HasFirstUv)
                {
                    Edge.FirstUvA        = UvA;
                    Edge.FirstUvB        = UvB;
                    Edge.HasFirstUv      = true;
                }
                else if (UvDistance(UvA, Edge.FirstUvA) > UvMatchTolerance ||
                         UvDistance(UvB, Edge.FirstUvB) > UvMatchTolerance)
                    Edge.UvSeam = true;
            }
        }
    }
    Incidence.Adjacent.assign(TriangleCount, {});
    for (const auto &[Identity, Edge] : Incidence.Edges)
    {
        (void)Identity;
        if (Edge.Sharp) ++Incidence.SharpEdgeCount;
        if (Edge.UvSeam) ++Incidence.UvSeamEdgeCount;
        if (Edge.Triangles.size() == 1) ++Incidence.BoundaryEdgeCount;
        for (const uint32_t Triangle : Edge.Triangles)
            for (const uint32_t Other : Edge.Triangles)
                if (Other != Triangle)
                {
                    Incidence.Adjacent[Triangle].push_back(Other);
                    if (Edge.Sharp || Edge.UvSeam) Incidence.FeatureSeamPairs.insert(FacetPairIdentity(Triangle, Other));
                }
    }
    for (auto &List : Incidence.Adjacent) std::sort(List.begin(), List.end());
    return Incidence;
}

//--------------------------------------------------------------------------------------------------------------------------
//                                                    WELD
//--------------------------------------------------------------------------------------------------------------------------

// 📝 Weld key: exact coordinate bits, with -0.0 folded onto +0.0 exactly as the reference's (P + 0) decimal key does.
struct PositionKey
{
    uint64_t Bits[3] = { 0, 0, 0 };
    bool operator==(const PositionKey &Other) const
    {
        return Bits[0] == Other.Bits[0] && Bits[1] == Other.Bits[1] && Bits[2] == Other.Bits[2];
    }
};

struct PositionKeyHash
{
    std::size_t operator()(const PositionKey &Key) const
    {
        uint64_t Mixed = Key.Bits[0] * 0x9E3779B97F4A7C15ull;
        Mixed ^= Key.Bits[1] + 0x9E3779B97F4A7C15ull + (Mixed << 6) + (Mixed >> 2);
        Mixed ^= Key.Bits[2] + 0x9E3779B97F4A7C15ull + (Mixed << 6) + (Mixed >> 2);
        return std::size_t(Mixed ^ (Mixed >> 32));
    }
};

static uint64_t FoldedBits(double Component)
{
    return std::bit_cast<uint64_t>(Component + 0.0);
}

WeldOutcome WeldFacetField(const FacetField &Source, const WeldSpecification &Specification)
{
    WeldOutcome Outcome;
    PolygonComplex &Complex = Outcome.Complex;
    const std::size_t FacetCount = Source.Corners.size() / 3;
    Complex.HasUVs = Source.CornerUVs.size() >= FacetCount * 3;

    std::unordered_map<PositionKey, uint32_t, PositionKeyHash> Welded;
    Welded.reserve(FacetCount * 3);
    const auto Weld = [&](const SpatialPosition &Position) -> uint32_t
    {
        const PositionKey Key{ { FoldedBits(Position.X), FoldedBits(Position.Y), FoldedBits(Position.Z) } };
        const auto Found = Welded.find(Key);
        if (Found != Welded.end()) return Found->second;
        const uint32_t Index = uint32_t(Complex.Positions.size());
        Welded.emplace(Key, Index);
        Complex.Positions.push_back(Position);
        return Index;
    };

    Complex.Triangles.reserve(FacetCount * 3);
    Complex.CornerUVs.reserve(FacetCount * 3);
    Complex.Family.reserve(FacetCount);
    Complex.SharpMask.reserve(FacetCount);
    for (std::size_t Facet = 0; Facet < FacetCount; ++Facet)
    {
        for (uint32_t Corner = 0; Corner < 3; ++Corner)
        {
            Complex.Triangles.push_back(Weld(Source.Corners[Facet * 3 + Corner]));
            Complex.CornerUVs.push_back(Complex.HasUVs ? Source.CornerUVs[Facet * 3 + Corner]
                                                       : std::array<double, 2>{ 0.0, 0.0 });
        }
        Complex.Family.push_back(Facet < Source.Family.size() ? Source.Family[Facet] : 0u);
        Complex.SharpMask.push_back(Facet < Source.SharpMask.size() ? uint8_t(Source.SharpMask[Facet] & 7u) : uint8_t(0));
    }

    IncidenceStructure Incidence = BuildIncidence(Complex);

    // ② Optional dihedral crease classification, then propagate every sharp edge back into the per-facet masks.
    for (auto &[Identity, Edge] : Incidence.Edges)
    {
        (void)Identity;
        if (!Edge.Sharp && Specification.HasCreaseAngle && Edge.Triangles.size() == 2)
        {
            const SpatialPosition N0 = Normalized(TriangleNormal(Complex, Edge.Triangles[0]));
            const SpatialPosition N1 = Normalized(TriangleNormal(Complex, Edge.Triangles[1]));
            if (Dot(N0, N1) < Specification.CreaseCosine) Edge.Sharp = true;
        }
        if (!Edge.Sharp) continue;
        for (const uint32_t Triangle : Edge.Triangles)
            for (uint32_t Corner = 0; Corner < 3; ++Corner)
            {
                const uint32_t V0 = Complex.Triangles[std::size_t(Triangle) * 3 + Corner];
                const uint32_t V1 = Complex.Triangles[std::size_t(Triangle) * 3 + (Corner + 1) % 3];
                if (EdgeIdentity(V0, V1) == EdgeIdentity(Edge.A, Edge.B))
                    Complex.SharpMask[Triangle] |= uint8_t(1u << Corner);
            }
    }

    Outcome.SharpEdgeCount    = Incidence.SharpEdgeCount;
    Outcome.UvSeamEdgeCount   = Incidence.UvSeamEdgeCount;
    Outcome.BoundaryEdgeCount = Incidence.BoundaryEdgeCount;
    return Outcome;
}

//--------------------------------------------------------------------------------------------------------------------------
//                                                    PARTITION
//--------------------------------------------------------------------------------------------------------------------------

uint32_t QueryPartitionClass(uint32_t Family, const ClusterSpecification &Specification)
{
    return std::find(Specification.TransmissiveFamily.begin(), Specification.TransmissiveFamily.end(), Family) !=
                   Specification.TransmissiveFamily.end()
               ? 1u
               : 0u;
}

FacetCluster MeasureFacetCluster(const PolygonComplex &Complex, const std::vector<uint32_t> &Members,
                                 uint32_t PartitionKey)
{
    FacetCluster Cluster;
    Cluster.Triangles    = Members;
    Cluster.PartitionKey = PartitionKey;
    if (Members.empty()) return Cluster;

    std::array<double, 3> Minimum = { std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
                                      std::numeric_limits<double>::max() };
    std::array<double, 3> Maximum = { -std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(),
                                      -std::numeric_limits<double>::max() };
    SpatialPosition Sum{ 0.0, 0.0, 0.0 };
    SpatialPosition AxisSum{ 0.0, 0.0, 0.0 };
    std::vector<SpatialPosition> FacetNormals;
    std::size_t Samples = 0;
    for (const uint32_t Triangle : Members)
    {
        for (uint32_t Corner = 0; Corner < 3; ++Corner)
        {
            const SpatialPosition Position = ReadPosition(Complex, Complex.Triangles[std::size_t(Triangle) * 3 + Corner]);
            Minimum[0] = std::min(Minimum[0], Position.X);
            Minimum[1] = std::min(Minimum[1], Position.Y);
            Minimum[2] = std::min(Minimum[2], Position.Z);
            Maximum[0] = std::max(Maximum[0], Position.X);
            Maximum[1] = std::max(Maximum[1], Position.Y);
            Maximum[2] = std::max(Maximum[2], Position.Z);
            Sum.X += Position.X;
            Sum.Y += Position.Y;
            Sum.Z += Position.Z;
            ++Samples;
        }
        const SpatialPosition Normal = TriangleNormal(Complex, Triangle);
        FacetNormals.push_back(Normal);
        AxisSum.X += Normal.X;
        AxisSum.Y += Normal.Y;
        AxisSum.Z += Normal.Z;
    }
    const double Divisor = double(std::max<std::size_t>(1, Samples));
    Cluster.SphereCentre = { Sum.X / Divisor, Sum.Y / Divisor, Sum.Z / Divisor };

    double Radius = 0.0;
    for (const uint32_t Triangle : Members)
        for (uint32_t Corner = 0; Corner < 3; ++Corner)
        {
            const SpatialPosition Position = ReadPosition(Complex, Complex.Triangles[std::size_t(Triangle) * 3 + Corner]);
            Radius = std::max(Radius, Length(Subtract(Position, Cluster.SphereCentre)));
        }
    Cluster.SphereRadius = Radius;

    Cluster.ConeAxis = Normalized(AxisSum);
    double Cutoff = 1.0;
    for (const SpatialPosition &Normal : FacetNormals) Cutoff = std::min(Cutoff, Dot(Cluster.ConeAxis, Normalized(Normal)));
    Cluster.ConeCutoff = Cutoff;

    Cluster.Minimum = Minimum;
    Cluster.Maximum = Maximum;
    return Cluster;
}

std::vector<FacetCluster> PartitionFacetClusters(const PolygonComplex &Complex, const ClusterSpecification &Specification)
{
    const uint32_t TriangleCount = uint32_t(Complex.Triangles.size() / 3);
    std::vector<FacetCluster> Clusters;
    if (TriangleCount == 0) return Clusters;

    const IncidenceStructure Incidence = BuildIncidence(Complex);
    std::vector<uint32_t> PartitionKey(TriangleCount, 0u);
    for (uint32_t Triangle = 0; Triangle < TriangleCount; ++Triangle)
        PartitionKey[Triangle] = QueryPartitionClass(Triangle < Complex.Family.size() ? Complex.Family[Triangle] : 0u,
                                                    Specification);

    std::vector<SpatialPosition> Normals(TriangleCount);
    std::vector<SpatialPosition> Centroids(TriangleCount);
    for (uint32_t Triangle = 0; Triangle < TriangleCount; ++Triangle)
    {
        Normals[Triangle]   = Normalized(TriangleNormal(Complex, Triangle));
        Centroids[Triangle] = TriangleCentroid(Complex, Triangle);
    }

    // 📝 Scale-normalised distance term: mean centroid spacing of adjacent facets, scaled to an expected cluster radius.
    double SpacingTotal = 0.0;
    std::size_t SpacingPairs = 0;
    for (uint32_t Triangle = 0; Triangle < TriangleCount; ++Triangle)
        for (const uint32_t Other : Incidence.Adjacent[Triangle])
            if (Other > Triangle)
            {
                SpacingTotal += Length(Subtract(Centroids[Triangle], Centroids[Other]));
                ++SpacingPairs;
            }
    const double MeanSpacing = SpacingPairs != 0 ? SpacingTotal / double(SpacingPairs) : 1.0;
    const double ExpectedRadius = std::max(MeanSpacing * std::sqrt(double(Specification.TriangleLimit) / PiConstant),
                                          MinimumExpectedSpan);

    std::vector<int32_t> Assignment(TriangleCount, -1);
    for (uint32_t Seed = 0; Seed < TriangleCount; ++Seed)
    {
        if (Assignment[Seed] >= 0) continue;
        const uint32_t Identity = uint32_t(Clusters.size());
        const uint32_t Key      = PartitionKey[Seed];
        std::vector<uint32_t> Members;
        std::unordered_set<uint32_t> Vertices;
        Members.reserve(Specification.TriangleLimit);
        Vertices.reserve(Specification.VertexLimit);

        const auto CountNewVertices = [&](uint32_t Triangle) -> std::size_t
        {
            std::size_t Added = 0;
            for (uint32_t Corner = 0; Corner < 3; ++Corner)
                if (Vertices.find(Complex.Triangles[std::size_t(Triangle) * 3 + Corner]) == Vertices.end()) ++Added;
            return Added;
        };
        const auto Admit = [&](uint32_t Triangle)
        {
            Assignment[Triangle] = int32_t(Identity);
            Members.push_back(Triangle);
            for (uint32_t Corner = 0; Corner < 3; ++Corner)
                Vertices.insert(Complex.Triangles[std::size_t(Triangle) * 3 + Corner]);
        };

        Admit(Seed);
        SpatialPosition Cone = Normals[Seed];
        std::set<uint32_t> Frontier;
        for (const uint32_t Other : Incidence.Adjacent[Seed])
            if (Assignment[Other] < 0 && PartitionKey[Other] == Key) Frontier.insert(Other);

        while (Members.size() < Specification.TriangleLimit && !Frontier.empty())
        {
            int64_t Best = -1;
            double  BestScore = -std::numeric_limits<double>::infinity();
            for (const uint32_t Candidate : Frontier)
            {
                if (Assignment[Candidate] >= 0) continue;
                if (Vertices.size() + CountNewVertices(Candidate) > Specification.VertexLimit) continue;
                const double Alignment = Dot(Normals[Candidate], Cone);
                const double Distance  = Length(Subtract(Centroids[Candidate], Centroids[Seed]));

                // 📝 A candidate joined only across a seam or crease pays a penalty, so clusters follow UV charts and creases.
                bool SmoothConnection = false;
                for (const uint32_t Neighbour : Incidence.Adjacent[Candidate])
                    if (Assignment[Neighbour] == int32_t(Identity) &&
                        Incidence.FeatureSeamPairs.find(FacetPairIdentity(Candidate, Neighbour)) ==
                            Incidence.FeatureSeamPairs.end())
                    {
                        SmoothConnection = true;
                        break;
                    }
                const double SeamPenalty = SmoothConnection ? 0.0 : SeamCrossPenalty;
                const double Score = Alignment - DistanceWeight * (Distance / ExpectedRadius) - SeamPenalty;
                if (Score > BestScore || (Score == BestScore && (Best < 0 || Candidate < uint32_t(Best))))
                {
                    BestScore = Score;
                    Best      = int64_t(Candidate);
                }
            }
            if (Best < 0) break;
            const uint32_t Admitted = uint32_t(Best);
            Frontier.erase(Admitted);
            Admit(Admitted);
            Cone = Normalized({ Cone.X + Normals[Admitted].X, Cone.Y + Normals[Admitted].Y, Cone.Z + Normals[Admitted].Z });
            for (const uint32_t Other : Incidence.Adjacent[Admitted])
                if (Assignment[Other] < 0 && PartitionKey[Other] == Key) Frontier.insert(Other);
        }

        std::sort(Members.begin(), Members.end());
        Clusters.push_back(MeasureFacetCluster(Complex, Members, Key));
    }
    return Clusters;
}

//--------------------------------------------------------------------------------------------------------------------------
//                                                    VALIDATION
//--------------------------------------------------------------------------------------------------------------------------

std::vector<std::string> ValidateClusterBuild(const PolygonComplex &Complex, const std::vector<FacetCluster> &Clusters,
                                             const ClusterSpecification &Specification)
{
    std::vector<std::string> Violations;
    const uint32_t TriangleCount = uint32_t(Complex.Triangles.size() / 3);
    std::vector<int32_t> Owner(TriangleCount, -1);

    for (std::size_t Identity = 0; Identity < Clusters.size(); ++Identity)
    {
        const FacetCluster &Cluster = Clusters[Identity];
        const std::string Prefix = "cluster " + std::to_string(Identity) + ": ";
        if (Cluster.Triangles.empty()) Violations.push_back(Prefix + "no member facets");
        if (Cluster.Triangles.size() > Specification.TriangleLimit)
            Violations.push_back(Prefix + "facet cap exceeded (" + std::to_string(Cluster.Triangles.size()) + " > " +
                                 std::to_string(Specification.TriangleLimit) + ")");

        std::unordered_set<uint32_t> Vertices;
        for (std::size_t Member = 0; Member < Cluster.Triangles.size(); ++Member)
        {
            const uint32_t Triangle = Cluster.Triangles[Member];
            if (Triangle >= TriangleCount)
            {
                Violations.push_back(Prefix + "member facet index " + std::to_string(Triangle) + " is outside the complex");
                continue;
            }
            if (Member > 0 && Triangle <= Cluster.Triangles[Member - 1])
                Violations.push_back(Prefix + "member facets are not strictly ascending at " + std::to_string(Member));
            if (Owner[Triangle] >= 0)
                Violations.push_back(Prefix + "facet " + std::to_string(Triangle) + " is claimed twice (also cluster " +
                                     std::to_string(Owner[Triangle]) + ")");
            Owner[Triangle] = int32_t(Identity);
            for (uint32_t Corner = 0; Corner < 3; ++Corner)
                Vertices.insert(Complex.Triangles[std::size_t(Triangle) * 3 + Corner]);
            const uint32_t Class = QueryPartitionClass(Triangle < Complex.Family.size() ? Complex.Family[Triangle] : 0u,
                                                      Specification);
            if (Class != Cluster.PartitionKey)
                Violations.push_back(Prefix + "facet " + std::to_string(Triangle) + " is class " + std::to_string(Class) +
                                     " but the cluster is class " + std::to_string(Cluster.PartitionKey));
        }
        if (Vertices.size() > Specification.VertexLimit)
            Violations.push_back(Prefix + "welded-vertex cap exceeded (" + std::to_string(Vertices.size()) + " > " +
                                 std::to_string(Specification.VertexLimit) + ")");

        // 📝 Connectivity: every member must be reachable from the first through shared-edge neighbours inside the cluster.
        if (!Cluster.Triangles.empty())
        {
            std::unordered_set<uint32_t> Members(Cluster.Triangles.begin(), Cluster.Triangles.end());
            std::unordered_map<uint32_t, std::vector<uint32_t>> Adjacent;
            for (const uint32_t Triangle : Cluster.Triangles)
                for (uint32_t Corner = 0; Corner < 3; ++Corner)
                {
                    const uint32_t V0 = Complex.Triangles[std::size_t(Triangle) * 3 + Corner];
                    const uint32_t V1 = Complex.Triangles[std::size_t(Triangle) * 3 + (Corner + 1) % 3];
                    for (const uint32_t Other : Cluster.Triangles)
                    {
                        if (Other == Triangle) continue;
                        for (uint32_t Side = 0; Side < 3; ++Side)
                        {
                            const uint32_t W0 = Complex.Triangles[std::size_t(Other) * 3 + Side];
                            const uint32_t W1 = Complex.Triangles[std::size_t(Other) * 3 + (Side + 1) % 3];
                            if (EdgeIdentity(V0, V1) == EdgeIdentity(W0, W1)) Adjacent[Triangle].push_back(Other);
                        }
                    }
                }
            std::unordered_set<uint32_t> Reached;
            std::vector<uint32_t> Pending{ Cluster.Triangles.front() };
            Reached.insert(Cluster.Triangles.front());
            while (!Pending.empty())
            {
                const uint32_t Triangle = Pending.back();
                Pending.pop_back();
                for (const uint32_t Other : Adjacent[Triangle])
                    if (Reached.insert(Other).second) Pending.push_back(Other);
            }
            if (Reached.size() != Members.size())
                Violations.push_back(Prefix + "not connected (" + std::to_string(Reached.size()) + " of " +
                                     std::to_string(Members.size()) + " facets reachable)");
        }

        // 📝 Enclosure: sphere and box must contain every member corner, and the cone must be a usable direction.
        for (const uint32_t Triangle : Cluster.Triangles)
            for (uint32_t Corner = 0; Corner < 3; ++Corner)
            {
                const SpatialPosition Position = ReadPosition(Complex, Complex.Triangles[std::size_t(Triangle) * 3 + Corner]);
                const double Offset = Length(Subtract(Position, Cluster.SphereCentre));
                if (Offset > Cluster.SphereRadius + ContainmentSlack)
                    Violations.push_back(Prefix + "sphere containment violated by " + std::to_string(Offset - Cluster.SphereRadius) + " m");
                const std::array<double, 3> Coordinates{ Position.X, Position.Y, Position.Z };
                for (int Axis = 0; Axis < 3; ++Axis)
                    if (Coordinates[Axis] < Cluster.Minimum[Axis] - ContainmentSlack ||
                        Coordinates[Axis] > Cluster.Maximum[Axis] + ContainmentSlack)
                        Violations.push_back(Prefix + "enclosure box containment violated on axis " + std::to_string(Axis));
            }
        const double AxisLength = Length(Cluster.ConeAxis);
        if (!(AxisLength > 0.0))
            Violations.push_back(Prefix + "cone axis is not a direction");
        if (!(Cluster.ConeCutoff >= -1.0 - 1.0e-9 && Cluster.ConeCutoff <= 1.0 + 1.0e-9))
            Violations.push_back(Prefix + "cone cutoff " + std::to_string(Cluster.ConeCutoff) + " is outside [-1, 1]");
    }

    for (uint32_t Triangle = 0; Triangle < TriangleCount; ++Triangle)
        if (Owner[Triangle] < 0) Violations.push_back("facet " + std::to_string(Triangle) + " belongs to no cluster");
    return Violations;
}

}   // namespace Frontier
