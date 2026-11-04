/*==========================================================================================================================================
                                                        NESTINGCONSTRUCTION.CPP
==========================================================================================================================================*/
// 🧩 GeometryHierarchy.js statement for statement: weld, edge incidence, cluster partition, quadric simplification, nesting
//    build, validation and tier collection. JavaScript Map/Set iteration order is kept wherever it reaches a result.
#include "NestingConstruction.h"
#include <algorithm>
#include <atomic>
#include <bit>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>
namespace Frontier::VirtualizedGeometry
{
namespace
{
constexpr uint32_t DefaultTriangleLimit     = 128;
constexpr double   DefaultSharpNormalCosine = 0x1.6a09e667f3bcdp-1;   // [-] Math.cos((45 * Math.PI) / 180) in V8, bit for bit
constexpr double   UvMatchTolerance         = 1e-5;                   // [-] UV wedge identity tolerance
constexpr double   CirclePi                 = 3.141592653589793;      // [-] Math.PI

[[nodiscard]] Point2 ReadCornerUv(const std::vector<double>& CornerUvs, uint32_t Triangle, uint32_t Corner) noexcept
{
    const size_t Base = static_cast<size_t>(Triangle) * 6 + Corner * 2;
    return {CornerUvs[Base], CornerUvs[Base + 1]};
}
[[nodiscard]] Point3 ReadPosition(const std::vector<double>& Positions, uint32_t Vertex) noexcept
{
    return {Positions[static_cast<size_t>(Vertex) * 3], Positions[static_cast<size_t>(Vertex) * 3 + 1],
            Positions[static_cast<size_t>(Vertex) * 3 + 2]};
}
template <typename Index>
[[nodiscard]] Point3 TriangleNormal(const std::vector<double>& Positions, const std::vector<Index>& Triangles, uint32_t Triangle) noexcept
{
    const Point3 A = ReadPosition(Positions, static_cast<uint32_t>(Triangles[Triangle * 3]));
    const Point3 B = ReadPosition(Positions, static_cast<uint32_t>(Triangles[Triangle * 3 + 1]));
    const Point3 C = ReadPosition(Positions, static_cast<uint32_t>(Triangles[Triangle * 3 + 2]));
    return Cross(Subtract(B, A), Subtract(C, A));   // 📝 unnormalized: length is twice the area
}
[[nodiscard]] std::array<Point3, 2> OrthogonalBasis(const Point3& Tangent) noexcept
{
    const Point3 Axis = std::fabs(Tangent[0]) < 0.8 ? Point3{1, 0, 0} : Point3{0, 1, 0};
    const Point3 U    = Normalize(Cross(Tangent, Axis));
    const Point3 V    = Normalize(Cross(Tangent, U));
    return {U, V};
}

// 📝 The JavaScript weld key: (P + 0) per axis, so -0 and +0 weld; exact bits otherwise (shortest round-trip strings are
//    unique per double).
struct PositionKey
{
    std::array<uint64_t, 3> Bits{};
    bool operator==(const PositionKey&) const = default;
};
struct PositionKeyHash
{
    size_t operator()(const PositionKey& Key) const noexcept
    {
        uint64_t Hash = 1469598103934665603ull;
        for (uint64_t Word : Key.Bits) Hash = (Hash ^ Word) * 1099511628211ull ^ (Word >> 29);
        return static_cast<size_t>(Hash);
    }
};
[[nodiscard]] uint64_t CanonicalBits(double Value) noexcept
{
    Value = Value + 0.0;
    if (std::isnan(Value)) return 0x7ff8000000000000ull;
    return std::bit_cast<uint64_t>(Value);
}
[[nodiscard]] PositionKey KeyOf(double X, double Y, double Z) noexcept { return {{CanonicalBits(X), CanonicalBits(Y), CanonicalBits(Z)}}; }

//------------------------------------------------------------------------------------------------------------------------
//                                                     EDGE INCIDENCE
//------------------------------------------------------------------------------------------------------------------------

struct IncidentEdge
{
    uint32_t              A = 0, B = 0;
    std::vector<uint32_t> Triangles{};
    bool                  Sharp = false, UvSeam = false, UvAssigned = false;
    Point2                UvA{}, UvB{};
};
// 📝 Every undirected edge lists its triangles once, in the insertion order of the demo's Map, and classifies UV seams and
//    marked sharp edges.
struct EdgeIncidence
{
    std::vector<IncidentEdge>              Edges{};
    std::unordered_map<uint64_t, uint32_t> EdgeSlot{};
    std::vector<std::vector<uint32_t>>     Adjacent{};
    std::unordered_set<uint64_t>           FeatureSeamBetween{};
    uint64_t                               VertexCount = 0, TriangleCount = 0;
    uint32_t                               SharpEdgeCount = 0, UvSeamEdgeCount = 0;
    [[nodiscard]] uint64_t PairKey(uint64_t T0, uint64_t T1) const noexcept
    {
        return T0 < T1 ? T0 * TriangleCount + T1 : T1 * TriangleCount + T0;
    }
    [[nodiscard]] const IncidentEdge* Find(uint64_t A, uint64_t B) const
    {
        const uint64_t Key = A < B ? A * VertexCount + B : B * VertexCount + A;
        const auto     Hit = EdgeSlot.find(Key);
        return Hit == EdgeSlot.end() ? nullptr : &Edges[Hit->second];
    }
};
template <typename Index>
[[nodiscard]] EdgeIncidence ConstructEdgeIncidence(const std::vector<Index>& Triangles, uint32_t VertexCount,
                                                   const std::vector<double>* CornerUvs = nullptr,
                                                   const std::vector<uint8_t>* SharpMask = nullptr)
{
    EdgeIncidence Incidence;
    Incidence.VertexCount   = VertexCount;
    Incidence.TriangleCount = Triangles.size() / 3;
    const uint32_t TriangleCount = static_cast<uint32_t>(Incidence.TriangleCount);
    Incidence.EdgeSlot.reserve(static_cast<size_t>(TriangleCount) * 2);
    for (uint32_t Triangle = 0; Triangle < TriangleCount; ++Triangle)
        for (uint32_t Corner = 0; Corner < 3; ++Corner)
        {
            const uint32_t V0 = static_cast<uint32_t>(Triangles[Triangle * 3 + Corner]);
            const uint32_t V1 = static_cast<uint32_t>(Triangles[Triangle * 3 + (Corner + 1) % 3]);
            const uint32_t A = std::min(V0, V1), B = std::max(V0, V1);
            const uint64_t Identity = static_cast<uint64_t>(A) * VertexCount + B;
            auto [Slot, Fresh] = Incidence.EdgeSlot.try_emplace(Identity, static_cast<uint32_t>(Incidence.Edges.size()));
            if (Fresh)
            {
                IncidentEdge Entry;
                Entry.A = A;
                Entry.B = B;
                Incidence.Edges.push_back(std::move(Entry));
            }
            IncidentEdge& Entry = Incidence.Edges[Slot->second];
            Entry.Triangles.push_back(Triangle);
            if (SharpMask && ((*SharpMask)[Triangle] & (1u << Corner)) != 0) Entry.Sharp = true;
            if (CornerUvs)
            {
                const uint32_t CornerA = V0 == A ? Corner : (Corner + 1) % 3;
                const uint32_t CornerB = V0 == A ? (Corner + 1) % 3 : Corner;
                const Point2   UvA     = ReadCornerUv(*CornerUvs, Triangle, CornerA);
                const Point2   UvB     = ReadCornerUv(*CornerUvs, Triangle, CornerB);
                if (!Entry.UvAssigned)
                {
                    Entry.UvAssigned = true;
                    Entry.UvA        = UvA;
                    Entry.UvB        = UvB;
                }
                else if (UvDistance(UvA, Entry.UvA) > UvMatchTolerance || UvDistance(UvB, Entry.UvB) > UvMatchTolerance)
                    Entry.UvSeam = true;
            }
        }
    Incidence.Adjacent.assign(TriangleCount, {});
    for (const IncidentEdge& Entry : Incidence.Edges)
    {
        if (Entry.Sharp) ++Incidence.SharpEdgeCount;
        if (Entry.UvSeam) ++Incidence.UvSeamEdgeCount;
        for (uint32_t Triangle : Entry.Triangles)
            for (uint32_t Other : Entry.Triangles)
                if (Other != Triangle)
                {
                    Incidence.Adjacent[Triangle].push_back(Other);
                    if (Entry.Sharp || Entry.UvSeam) Incidence.FeatureSeamBetween.insert(Incidence.PairKey(Triangle, Other));
                }
    }
    for (std::vector<uint32_t>& List : Incidence.Adjacent) std::sort(List.begin(), List.end());
    return Incidence;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     CLUSTER PARTITION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Greedy adjacency expansion: a seed grows across shared edges, scoring candidates by normal coherence with the running
//    cone, scale-normalised centroid distance and a soft penalty for crossing UV seams or marked sharp edges. Candidates tie
//    on the lowest index, so the frontier's iteration order never reaches the result.
[[nodiscard]] std::vector<std::vector<uint32_t>> PartitionClusters(const TierGeometry& Geometry, const std::vector<uint64_t>& PartitionKeys,
                                                                   uint32_t TriangleLimit, uint32_t VertexLimit)
{
    const std::vector<double>&   Positions     = Geometry.Positions;
    const std::vector<uint32_t>& Triangles     = Geometry.Triangles;
    const uint32_t               TriangleCount = Geometry.TriangleCount();
    const EdgeIncidence Incidence = ConstructEdgeIncidence(Triangles, Geometry.VertexCount(), &Geometry.CornerUvs, &Geometry.SharpMask);
    std::vector<Point3> Normals(TriangleCount), Centroids(TriangleCount);
    for (uint32_t Triangle = 0; Triangle < TriangleCount; ++Triangle)
    {
        Normals[Triangle] = Normalize(TriangleNormal(Positions, Triangles, Triangle));
        const Point3 A = ReadPosition(Positions, Triangles[Triangle * 3]);
        const Point3 B = ReadPosition(Positions, Triangles[Triangle * 3 + 1]);
        const Point3 C = ReadPosition(Positions, Triangles[Triangle * 3 + 2]);
        Centroids[Triangle] = {(A[0] + B[0] + C[0]) / 3, (A[1] + B[1] + C[1]) / 3, (A[2] + B[2] + C[2]) / 3};
    }
    std::vector<int32_t> Assigned(TriangleCount, -1);
    std::vector<std::vector<uint32_t>> Clusters;
    double   SpacingTotal = 0.0;
    uint64_t SpacingPairs = 0;
    for (uint32_t Triangle = 0; Triangle < TriangleCount; ++Triangle)
        for (uint32_t Other : Incidence.Adjacent[Triangle])
            if (Other > Triangle)
            {
                SpacingTotal += Length(Subtract(Centroids[Triangle], Centroids[Other]));
                ++SpacingPairs;
            }
    const double MeanSpacing    = SpacingPairs ? SpacingTotal / static_cast<double>(SpacingPairs) : 1.0;
    const double ExpectedRadius = JsMax(MeanSpacing * std::sqrt(static_cast<double>(TriangleLimit) / CirclePi), 1e-12);
    std::vector<int32_t> VertexStamp(Geometry.VertexCount(), -1), FrontierStamp(TriangleCount, -1);
    for (uint32_t Seed = 0; Seed < TriangleCount; ++Seed)
    {
        if (Assigned[Seed] >= 0) continue;
        const int32_t  Identity = static_cast<int32_t>(Clusters.size());
        const uint64_t Key      = PartitionKeys[Seed];
        std::vector<uint32_t> Members;
        uint32_t VertexTotal = 0;
        auto AddedVertices = [&](uint32_t Triangle) {
            uint32_t Added = 0;
            for (uint32_t Corner = 0; Corner < 3; ++Corner)
                if (VertexStamp[Triangles[Triangle * 3 + Corner]] != Identity) ++Added;
            return Added;
        };
        auto Admit = [&](uint32_t Triangle) {
            Assigned[Triangle] = Identity;
            Members.push_back(Triangle);
            for (uint32_t Corner = 0; Corner < 3; ++Corner)
            {
                const uint32_t Vertex = Triangles[Triangle * 3 + Corner];
                if (VertexStamp[Vertex] != Identity)
                {
                    VertexStamp[Vertex] = Identity;
                    ++VertexTotal;
                }
            }
        };
        Admit(Seed);
        Point3 Cone = Normals[Seed];
        std::vector<uint32_t> Frontier;
        auto Enter = [&](uint32_t Triangle) {
            if (FrontierStamp[Triangle] == Identity) return;
            FrontierStamp[Triangle] = Identity;
            Frontier.push_back(Triangle);
        };
        for (uint32_t Other : Incidence.Adjacent[Seed])
            if (Assigned[Other] < 0 && PartitionKeys[Other] == Key) Enter(Other);
        while (Members.size() < TriangleLimit && !Frontier.empty())
        {
            int64_t Best      = -1;
            double  BestScore = -Unbounded;
            for (uint32_t Candidate : Frontier)
            {
                if (Assigned[Candidate] >= 0) continue;
                if (VertexTotal + AddedVertices(Candidate) > VertexLimit) continue;
                const double Alignment = Dot(Normals[Candidate], Cone);
                const double Distance  = Length(Subtract(Centroids[Candidate], Centroids[Seed]));
                bool SmoothConnection = false;
                for (uint32_t Neighbour : Incidence.Adjacent[Candidate])
                    if (Assigned[Neighbour] == Identity && !Incidence.FeatureSeamBetween.contains(Incidence.PairKey(Candidate, Neighbour)))
                    {
                        SmoothConnection = true;
                        break;
                    }
                const double SeamPenalty = SmoothConnection ? 0.0 : 0.35;
                const double Score       = Alignment - 0.3 * (Distance / ExpectedRadius) - SeamPenalty;
                if (Score > BestScore || (Score == BestScore && static_cast<int64_t>(Candidate) < Best))
                {
                    BestScore = Score;
                    Best      = Candidate;
                }
            }
            if (Best < 0) break;
            const uint32_t Chosen = static_cast<uint32_t>(Best);
            Frontier.erase(std::find(Frontier.begin(), Frontier.end(), Chosen));
            FrontierStamp[Chosen] = -2;
            Admit(Chosen);
            Cone = Normalize({Cone[0] + Normals[Chosen][0], Cone[1] + Normals[Chosen][1], Cone[2] + Normals[Chosen][2]});
            for (uint32_t Other : Incidence.Adjacent[Chosen])
                if (Assigned[Other] < 0 && PartitionKeys[Other] == Key)
                {
                    // 📝 Set.add after Set.delete re-enters a triangle; FrontierStamp -2 marks only admitted ones.
                    if (FrontierStamp[Other] != Identity)
                    {
                        FrontierStamp[Other] = Identity;
                        Frontier.push_back(Other);
                    }
                }
        }
        std::sort(Members.begin(), Members.end());
        Clusters.push_back(std::move(Members));
    }
    return Clusters;
}

struct ClusterMeasure
{
    Point3        Minimum{}, Maximum{};
    NestingSphere Sphere{};
    NestingCone   Cone{};
};
[[nodiscard]] ClusterMeasure MeasureCluster(const TierGeometry& Geometry, const std::vector<uint32_t>& Members, double Error)
{
    const std::vector<double>&   Positions = Geometry.Positions;
    const std::vector<uint32_t>& Triangles = Geometry.Triangles;
    Point3   Minimum{Unbounded, Unbounded, Unbounded}, Maximum{-Unbounded, -Unbounded, -Unbounded}, Center{0, 0, 0};
    uint32_t Samples = 0;
    Point3   Axis{0, 0, 0};
    std::vector<Point3> Normals;
    Normals.reserve(Members.size());
    for (uint32_t Triangle : Members)
    {
        for (uint32_t Corner = 0; Corner < 3; ++Corner)
        {
            const Point3 P = ReadPosition(Positions, Triangles[Triangle * 3 + Corner]);
            for (int A = 0; A < 3; ++A)
            {
                Minimum[A] = JsMin(Minimum[A], P[A]);
                Maximum[A] = JsMax(Maximum[A], P[A]);
                Center[A] += P[A];
            }
            ++Samples;
        }
        const Point3 Normal = TriangleNormal(Positions, Triangles, Triangle);
        Normals.push_back(Normal);
        Axis = {Axis[0] + Normal[0], Axis[1] + Normal[1], Axis[2] + Normal[2]};
    }
    for (int A = 0; A < 3; ++A) Center[A] /= static_cast<double>(std::max<uint32_t>(1, Samples));
    double Radius = 0.0;
    for (uint32_t Triangle : Members)
        for (uint32_t Corner = 0; Corner < 3; ++Corner)
            Radius = JsMax(Radius, Length(Subtract(ReadPosition(Positions, Triangles[Triangle * 3 + Corner]), Center)));
    Axis = Normalize(Axis);
    double Cutoff = 1.0;
    for (const Point3& Normal : Normals) Cutoff = JsMin(Cutoff, Dot(Axis, Normalize(Normal)));
    ClusterMeasure Measure;
    for (int A = 0; A < 3; ++A)
    {
        Measure.Minimum[A] = Minimum[A] - Error;
        Measure.Maximum[A] = Maximum[A] + Error;
    }
    Measure.Sphere = {Center, Radius + Error};
    Measure.Cone   = {Axis, Cutoff};
    return Measure;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     QUADRIC SIMPLIFICATION
//------------------------------------------------------------------------------------------------------------------------

using Quadric = std::array<double, 10>;

[[nodiscard]] Quadric QuadricFromPlane(const Point3& Normal, double D, double Weight) noexcept
{
    const double A = Normal[0], B = Normal[1], C = Normal[2];
    return {Weight * A * A, Weight * A * B, Weight * A * C, Weight * A * D, Weight * B * B,
            Weight * B * C, Weight * B * D, Weight * C * C, Weight * C * D, Weight * D * D};
}
[[nodiscard]] double QuadricError(const Quadric& Q, const Point3& P) noexcept
{
    const double X = P[0], Y = P[1], Z = P[2];
    return Q[0] * X * X + 2 * Q[1] * X * Y + 2 * Q[2] * X * Z + 2 * Q[3] * X +
           Q[4] * Y * Y + 2 * Q[5] * Y * Z + 2 * Q[6] * Y + Q[7] * Z * Z + 2 * Q[8] * Z + Q[9];
}
struct QuadricChoice
{
    Point3 Position{};
    double Error = 0.0;
};
[[nodiscard]] double Determinant3(const std::array<std::array<double, 3>, 3>& R) noexcept
{
    return R[0][0] * (R[1][1] * R[2][2] - R[1][2] * R[2][1]) -
           R[0][1] * (R[1][0] * R[2][2] - R[1][2] * R[2][0]) +
           R[0][2] * (R[1][0] * R[2][1] - R[1][1] * R[2][0]);
}
[[nodiscard]] QuadricChoice QuadricOptimum(const Quadric& Q, std::vector<Point3> Fallbacks)
{
    const std::array<std::array<double, 3>, 3> M = {{{Q[0], Q[1], Q[2]}, {Q[1], Q[4], Q[5]}, {Q[2], Q[5], Q[7]}}};
    const Point3 V = {-Q[3], -Q[6], -Q[8]};
    const double Det = Determinant3(M);
    if (std::fabs(Det) > 1e-12)
    {
        auto Replace = [&](int Column) {
            std::array<std::array<double, 3>, 3> R = M;
            for (int Row = 0; Row < 3; ++Row) R[Row][Column] = V[Row];
            return Determinant3(R);
        };
        const Point3 Optimum = {Replace(0) / Det, Replace(1) / Det, Replace(2) / Det};
        if (std::isfinite(Optimum[0]) && std::isfinite(Optimum[1]) && std::isfinite(Optimum[2]) && Fallbacks.size() >= 2)
        {
            const double EdgeLength = Length(Subtract(Fallbacks[0], Fallbacks[1]));
            const Point3 Midpoint   = {(Fallbacks[0][0] + Fallbacks[1][0]) / 2, (Fallbacks[0][1] + Fallbacks[1][1]) / 2,
                                       (Fallbacks[0][2] + Fallbacks[1][2]) / 2};
            if (Length(Subtract(Optimum, Midpoint)) <= EdgeLength * 0.5) Fallbacks.push_back(Optimum);
        }
    }
    Point3 Best      = Fallbacks[0];
    double BestError = Unbounded;
    for (const Point3& Candidate : Fallbacks)
    {
        const double Error = QuadricError(Q, Candidate);
        if (Error < BestError)
        {
            BestError = Error;
            Best      = Candidate;
        }
    }
    return {Best, JsMax(0.0, BestError)};
}

// 📝 Per-vertex triangle sets of the demo (new Set() per vertex): insertion order, add-if-absent, clear.
class OrderedTriangleSets
{
  public:
    explicit OrderedTriangleSets(uint32_t VertexCount) : Order_(VertexCount) {}
    void Add(uint32_t Vertex, uint32_t Triangle)
    {
        if (Members_.insert(Key(Vertex, Triangle)).second) Order_[Vertex].push_back(Triangle);
    }
    void Clear(uint32_t Vertex)
    {
        for (uint32_t Triangle : Order_[Vertex]) Members_.erase(Key(Vertex, Triangle));
        Order_[Vertex].clear();
    }
    [[nodiscard]] const std::vector<uint32_t>& operator[](uint32_t Vertex) const noexcept { return Order_[Vertex]; }
  private:
    [[nodiscard]] static uint64_t Key(uint32_t Vertex, uint32_t Triangle) noexcept { return (static_cast<uint64_t>(Vertex) << 32) | Triangle; }
    std::vector<std::vector<uint32_t>> Order_;
    std::unordered_set<uint64_t>       Members_{};
};

// 📝 Small insertion-ordered unique list (the demo's short-lived Sets).
template <typename Value> void AppendUnique(std::vector<Value>& List, const Value& Entry)
{
    if (std::find(List.begin(), List.end(), Entry) == List.end()) List.push_back(Entry);
}

struct SimplifiedGroup : TierGeometry
{
    double   Deviation   = 0.0;
    uint32_t LockedCount = 0;
};

// 📝 SimplifyGroup: one group's geometry, respecting locked borders (group boundary, open border, non-manifold edge, family
//    border), marked sharp edges and UV seams (line quadrics, feature-corner pinning, collapse along features, synchronized
//    multi-chart UV interpolation and 2D UV flip rejection).
class GroupSimplification
{
  public:
    GroupSimplification(const TierGeometry& Local, uint32_t TargetTriangles, const NestingOptions& Options,
                        const std::vector<uint8_t>* OpenBorder = nullptr)
        : Options_(Options), OpenBorder_(OpenBorder), VertexCount_(Local.VertexCount()), TriangleCount_(Local.TriangleCount()), Target_(TargetTriangles), Families_(Local.Families),
          LivePositions_(Local.Positions), LiveTriangles_(Local.Triangles.begin(), Local.Triangles.end()), LiveCornerUvs_(Local.CornerUvs),
          LiveSharpMask_(Local.SharpMask), VertexTriangles_(VertexCount_)
    {
        if (LiveCornerUvs_.size() != static_cast<size_t>(TriangleCount_) * 6) LiveCornerUvs_.assign(static_cast<size_t>(TriangleCount_) * 6, 0.0);
        if (LiveSharpMask_.size() != TriangleCount_) LiveSharpMask_.assign(TriangleCount_, 0);
        Edges_ = ConstructEdgeIncidence(Local.Triangles, VertexCount_, &LiveCornerUvs_, &LiveSharpMask_);
    }
    [[nodiscard]] SimplifiedGroup Run();

  private:
    struct NeighbourFeature
    {
        uint32_t Other  = 0;
        bool     Sharp  = false, UvSeam = false;
        Point2   UvV{}, UvO{};
        uint32_t Family = 0;   // [-] family of the first triangle on the edge (I5 class borders)
        uint32_t Count  = 0;   // [-] live triangles on the edge (I4 open borders)
    };
    struct VertexFeatures
    {
        bool     IsFeature = false, IsCollapsibleFeature = false, IsFeatureCorner = false;
        uint32_t SharpDegree = 0, SeamDegree = 0, FeatureDegree = 0;
        std::vector<Point2>           Wedges{};
        std::vector<NeighbourFeature> Neighbours{};
        [[nodiscard]] const NeighbourFeature* Find(uint32_t Other) const
        {
            for (const NeighbourFeature& Entry : Neighbours)
                if (Entry.Other == Other) return &Entry;
            return nullptr;
        }
    };
    struct CollapseCandidate
    {
        uint32_t A = 0, B = 0;
        Point3   Position{};
        bool     PinA = false, PinB = false;
        double   Cost = 0.0, Deviation = 0.0;
        int32_t  VersionA = 0, VersionB = 0;
    };
    struct UvUpdate
    {
        uint32_t Triangle = 0, Corner = 0;
        Point2   Uv{};
    };

    [[nodiscard]] uint32_t Resolve(uint32_t Vertex) const noexcept
    {
        while (Remap_[Vertex] != static_cast<int32_t>(Vertex)) Vertex = static_cast<uint32_t>(Remap_[Vertex]);
        return Vertex;
    }
    [[nodiscard]] std::array<uint32_t, 3> ResolvedCorners(uint32_t Triangle) const noexcept
    {
        return {Resolve(static_cast<uint32_t>(LiveTriangles_[Triangle * 3])), Resolve(static_cast<uint32_t>(LiveTriangles_[Triangle * 3 + 1])),
                Resolve(static_cast<uint32_t>(LiveTriangles_[Triangle * 3 + 2]))};
    }
    [[nodiscard]] static int IndexOf(const std::array<uint32_t, 3>& Corners, uint32_t Vertex) noexcept
    {
        for (int Corner = 0; Corner < 3; ++Corner)
            if (Corners[Corner] == Vertex) return Corner;
        return -1;
    }
    [[nodiscard]] Point3 LivePosition(uint32_t Vertex) const noexcept { return ReadPosition(LivePositions_, Vertex); }
    [[nodiscard]] VertexFeatures AnalyzeVertexFeatures(uint32_t Vertex) const;
    [[nodiscard]] std::optional<CollapseCandidate> Evaluate(uint32_t A, uint32_t B) const;
    [[nodiscard]] bool Flips(uint32_t A, uint32_t B, const Point3& Position) const;
    [[nodiscard]] std::optional<std::vector<UvUpdate>> ComputeCornerUvUpdates(uint32_t Keeper, uint32_t Removed, const Point3& Position) const;
    void HeapPush(CollapseCandidate Item);
    [[nodiscard]] CollapseCandidate HeapPop();

    const NestingOptions&       Options_;
    const std::vector<uint8_t>* OpenBorder_ = nullptr;   // [-] I4: bit e marks an edge on the surface's own open border
    uint32_t                    VertexCount_ = 0, TriangleCount_ = 0, Target_ = 0;
    const std::vector<uint32_t>& Families_;
    std::vector<double>         LivePositions_;
    std::vector<int32_t>        LiveTriangles_;
    std::vector<double>         LiveCornerUvs_;
    std::vector<uint8_t>        LiveSharpMask_;
    EdgeIncidence               Edges_{};
    std::vector<uint8_t>        Locked_{}, Alive_{};
    std::vector<Quadric>        Quadrics_{};
    std::vector<int32_t>        Remap_{}, Versions_{};
    OrderedTriangleSets         VertexTriangles_;
    std::vector<CollapseCandidate> Heap_{};
};

GroupSimplification::VertexFeatures GroupSimplification::AnalyzeVertexFeatures(uint32_t Vertex) const
{
    VertexFeatures Features;
    for (uint32_t Triangle : VertexTriangles_[Vertex])
    {
        if (!Alive_[Triangle]) continue;
        const std::array<uint32_t, 3> Corners = ResolvedCorners(Triangle);
        const int LocalCorner = IndexOf(Corners, Vertex);
        if (LocalCorner < 0) continue;
        const Point2 Uv = ReadCornerUv(LiveCornerUvs_, Triangle, static_cast<uint32_t>(LocalCorner));
        bool Known = false;
        for (const Point2& Wedge : Features.Wedges)
            if (UvDistance(Wedge, Uv) <= UvMatchTolerance)
            {
                Known = true;
                break;
            }
        if (!Known) Features.Wedges.push_back(Uv);
        for (uint32_t Edge = 0; Edge < 3; ++Edge)
        {
            const uint32_t V0 = Corners[Edge], V1 = Corners[(Edge + 1) % 3];
            if (V0 != Vertex && V1 != Vertex) continue;
            const uint32_t Other   = V0 == Vertex ? V1 : V0;
            const uint32_t CornerV = V0 == Vertex ? Edge : (Edge + 1) % 3;
            const uint32_t CornerO = V0 == Vertex ? (Edge + 1) % 3 : Edge;
            const Point2   UvV     = ReadCornerUv(LiveCornerUvs_, Triangle, CornerV);
            const Point2   UvO     = ReadCornerUv(LiveCornerUvs_, Triangle, CornerO);
            NeighbourFeature* Entry = nullptr;
            for (NeighbourFeature& Candidate : Features.Neighbours)
                if (Candidate.Other == Other)
                {
                    Entry = &Candidate;
                    break;
                }
            if (!Entry)
            {
                Features.Neighbours.push_back({Other, false, false, UvV, UvO, Families_[Triangle], 0});
                Entry = &Features.Neighbours.back();
            }
            else
            {
                if (UvDistance(UvV, Entry->UvV) > UvMatchTolerance || UvDistance(UvO, Entry->UvO) > UvMatchTolerance) Entry->UvSeam = true;
                // 📝 I5: a family border inside the group is a feature curve, collapsible along itself like a marked sharp edge.
                if (Options_.ClassBorderCollapse && Families_[Triangle] != Entry->Family) Entry->Sharp = true;
            }
            if ((LiveSharpMask_[Triangle] & (1u << Edge)) != 0) Entry->Sharp = true;
            ++Entry->Count;
        }
    }
    // 📝 I4: an edge with one live triangle and an unlocked end lies on the surface's own open border (group borders lock both
    //    ends), a feature curve its vertices may slide along instead of a locked fence that fans needles into the interior.
    if (Options_.NeedleGuard && OpenBorder_)
        for (NeighbourFeature& Entry : Features.Neighbours)
            if (Entry.Count == 1 && !(Locked_[Vertex] && Locked_[Entry.Other])) Entry.Sharp = true;
    for (const NeighbourFeature& Entry : Features.Neighbours)
    {
        if (Entry.Sharp) ++Features.SharpDegree;
        if (Entry.UvSeam) ++Features.SeamDegree;
        if (Entry.Sharp || Entry.UvSeam) ++Features.FeatureDegree;
    }
    Features.IsFeature            = Features.FeatureDegree > 0 || Features.Wedges.size() > 1;
    Features.IsCollapsibleFeature = Features.IsFeature && Features.FeatureDegree == 2 && (Features.SharpDegree == 0 || Features.SharpDegree == 2) &&
                                    (Features.SeamDegree == 0 || Features.SeamDegree == 2) && Features.Wedges.size() <= 2;
    Features.IsFeatureCorner      = Features.IsFeature && !Features.IsCollapsibleFeature;
    return Features;
}

std::optional<GroupSimplification::CollapseCandidate> GroupSimplification::Evaluate(uint32_t A, uint32_t B) const
{
    A = Resolve(A);
    B = Resolve(B);
    if (A == B) return std::nullopt;
    if (Locked_[A] && Locked_[B]) return std::nullopt;
    const VertexFeatures FA = AnalyzeVertexFeatures(A), FB = AnalyzeVertexFeatures(B);
    const NeighbourFeature* EdgeFeature = FA.Find(B);
    if (!EdgeFeature) return std::nullopt;
    const bool EdgeIsFeature = EdgeFeature->Sharp || EdgeFeature->UvSeam;
    // 1) Never collapse a non-feature chord between two feature vertices.
    if (FA.IsFeature && FB.IsFeature && !EdgeIsFeature) return std::nullopt;
    const bool PinA = Locked_[A] || FA.IsFeatureCorner || (FA.IsFeature && !FB.IsFeature);
    const bool PinB = Locked_[B] || FB.IsFeatureCorner || (FB.IsFeature && !FA.IsFeature);
    if (PinA && PinB) return std::nullopt;
    // 2) A smooth interior vertex may only collapse into a UV-seam vertex along a feature edge.
    if (!EdgeIsFeature && (FA.SeamDegree > 0 || FB.SeamDegree > 0)) return std::nullopt;

    Quadric Sum;
    for (int C = 0; C < 10; ++C) Sum[C] = Quadrics_[A][C] + Quadrics_[B][C];
    const Point3 PA = LivePosition(A), PB = LivePosition(B);
    const Point3 Midpoint = {(PA[0] + PB[0]) / 2, (PA[1] + PB[1]) / 2, (PA[2] + PB[2]) / 2};
    Point3 Position;
    double Error = 0.0;
    if (PinA || PinB)
    {
        Position = PinA ? PA : PB;
        Error    = JsMax(0.0, QuadricError(Sum, Position));
    }
    else if (EdgeIsFeature)
    {
        // Along a feature edge, restrict candidates to the segment [PA, PB] so parameter t in [0, 1] is exact.
        Point3 Best = Midpoint;
        double BestError = Unbounded;
        for (const Point3& Candidate : {PA, PB, Midpoint})
        {
            const double E = QuadricError(Sum, Candidate);
            if (E < BestError)
            {
                BestError = E;
                Best      = Candidate;
            }
        }
        Position = Best;
        Error    = JsMax(0.0, BestError);
    }
    else
    {
        const QuadricChoice Choice = QuadricOptimum(Sum, {PA, PB, Midpoint});
        Position = Choice.Position;
        Error    = Choice.Error;
    }
    const double Deviation = JsMax(Length(Subtract(Position, PA)), Length(Subtract(Position, PB)));
    // 📝 Edge-length regularization: on ruled or planar patches the plane quadric cost is near zero along straight
    //    generators, so without a length tie-breaker the heap collapses whole columns into needles first.
    const double Trace = JsMax(Sum[0] + Sum[4] + Sum[7], 1e-6);
    double       Cost  = Error + 0.02 * Deviation * Deviation * Trace;
    if (Options_.NeedleGuard)
    {
        // 📝 I4: the plane quadrics are blind to triangle shape, so flat runs (terrain benches, caps) collapse into needles; the worst
        //    longest² / (2·area) the collapse would leave around A and B adds a cost in the units of the length term above.
        double WorstAspect = 0.0;
        for (uint32_t Vertex : {A, B})
            for (uint32_t Triangle : VertexTriangles_[Vertex])
            {
                if (!Alive_[Triangle]) continue;
                const std::array<uint32_t, 3> Corners = ResolvedCorners(Triangle);
                if (IndexOf(Corners, A) >= 0 && IndexOf(Corners, B) >= 0) continue;
                std::array<Point3, 3> Moved;
                for (int Corner = 0; Corner < 3; ++Corner)
                    Moved[Corner] = (Corners[Corner] == A || Corners[Corner] == B) ? Position : LivePosition(Corners[Corner]);
                const double Longest    = JsMax(JsMax(Length(Subtract(Moved[1], Moved[0])), Length(Subtract(Moved[2], Moved[1]))),
                                                Length(Subtract(Moved[0], Moved[2])));
                const double DoubleArea = Length(Cross(Subtract(Moved[1], Moved[0]), Subtract(Moved[2], Moved[0])));
                WorstAspect = JsMax(WorstAspect, DoubleArea > 0 ? Longest * Longest / DoubleArea : 1e6);
            }
        const double EdgeLength = Length(Subtract(PA, PB));
        Cost += 0.02 * EdgeLength * EdgeLength * Trace * JsMax(0.0, WorstAspect - 3.0);
    }
    return CollapseCandidate{A, B, Position, PinA, PinB, Cost, Deviation, Versions_[A], Versions_[B]};
}

void GroupSimplification::HeapPush(CollapseCandidate Item)
{
    Heap_.push_back(Item);
    size_t Index = Heap_.size() - 1;
    while (Index > 0)
    {
        const size_t Upper = (Index - 1) >> 1;
        if (Heap_[Upper].Cost <= Heap_[Index].Cost) break;
        std::swap(Heap_[Upper], Heap_[Index]);
        Index = Upper;
    }
}

GroupSimplification::CollapseCandidate GroupSimplification::HeapPop()
{
    const CollapseCandidate Top  = Heap_[0];
    const CollapseCandidate Last = Heap_.back();
    Heap_.pop_back();
    if (!Heap_.empty())
    {
        Heap_[0] = Last;
        size_t Index = 0;
        while (true)
        {
            size_t Smallest = Index;
            const size_t Left = Index * 2 + 1, Right = Left + 1;
            if (Left < Heap_.size() && Heap_[Left].Cost < Heap_[Smallest].Cost) Smallest = Left;
            if (Right < Heap_.size() && Heap_[Right].Cost < Heap_[Smallest].Cost) Smallest = Right;
            if (Smallest == Index) break;
            std::swap(Heap_[Smallest], Heap_[Index]);
            Index = Smallest;
        }
    }
    return Top;
}

bool GroupSimplification::Flips(uint32_t A, uint32_t B, const Point3& Position) const
{
    for (uint32_t Triangle : VertexTriangles_[A])
    {
        if (!Alive_[Triangle]) continue;
        const std::array<uint32_t, 3> Corners = ResolvedCorners(Triangle);
        if (IndexOf(Corners, B) >= 0) continue;
        std::array<Point3, 3> Points, Current;
        for (int Corner = 0; Corner < 3; ++Corner)
        {
            Current[Corner] = LivePosition(Corners[Corner]);
            Points[Corner]  = Corners[Corner] == A ? Position : Current[Corner];
        }
        const Point3 Before = Cross(Subtract(Current[1], Current[0]), Subtract(Current[2], Current[0]));
        const Point3 After  = Cross(Subtract(Points[1], Points[0]), Subtract(Points[2], Points[0]));
        if (Dot(Before, After) <= 0) return true;
    }
    return false;
}

// 📝 Chart-coherent corner UVs for every surviving triangle around the collapse; rejects any collapse that would mix UV charts
//    or invert a 2D UV triangle.
std::optional<std::vector<GroupSimplification::UvUpdate>> GroupSimplification::ComputeCornerUvUpdates(uint32_t Keeper, uint32_t Removed,
                                                                                                      const Point3& Position) const
{
    const Point3 PK = LivePosition(Keeper), PR = LivePosition(Removed);
    const Point3 Span = Subtract(PR, PK);
    const double SpanSq = Dot(Span, Span);
    const double T = SpanSq > 1e-20 ? JsMax(0.0, JsMin(1.0, Dot(Subtract(Position, PK), Span) / SpanSq)) : 0.0;
    struct ChartBlend
    {
        Point2 UvK{}, UvR{}, UvNew{};
    };
    std::vector<ChartBlend> Charts;
    for (uint32_t Triangle : VertexTriangles_[Removed])
    {
        if (!Alive_[Triangle]) continue;
        const std::array<uint32_t, 3> Corners = ResolvedCorners(Triangle);
        const int CK = IndexOf(Corners, Keeper), CR = IndexOf(Corners, Removed);
        if (CK < 0 || CR < 0) continue;
        const Point2 UvK = ReadCornerUv(LiveCornerUvs_, Triangle, static_cast<uint32_t>(CK));
        const Point2 UvR = ReadCornerUv(LiveCornerUvs_, Triangle, static_cast<uint32_t>(CR));
        Charts.push_back({UvK, UvR, {UvK[0] * (1 - T) + UvR[0] * T, UvK[1] * (1 - T) + UvR[1] * T}});
    }
    if (Charts.empty()) return std::nullopt;
    std::vector<uint32_t> Affected = VertexTriangles_[Removed];
    for (uint32_t Triangle : VertexTriangles_[Keeper]) AppendUnique(Affected, Triangle);
    std::vector<UvUpdate> Updates;
    for (uint32_t Triangle : Affected)
    {
        if (!Alive_[Triangle]) continue;
        const std::array<uint32_t, 3> Corners = ResolvedCorners(Triangle);
        const int CK = IndexOf(Corners, Keeper), CR = IndexOf(Corners, Removed);
        if (CK >= 0 && CR >= 0) continue;   // dies in collapse
        const int TargetCorner = CK >= 0 ? CK : CR;
        if (TargetCorner < 0) continue;
        const Point2 CurrentUv = ReadCornerUv(LiveCornerUvs_, Triangle, static_cast<uint32_t>(TargetCorner));
        std::optional<Point2> Replacement;
        if (CK >= 0 && T == 0)
            Replacement = CurrentUv;
        else
        {
            for (const ChartBlend& Chart : Charts)
            {
                const Point2& Reference = CK >= 0 ? Chart.UvK : Chart.UvR;
                if (UvDistance(CurrentUv, Reference) <= UvMatchTolerance)
                {
                    Replacement = Chart.UvNew;
                    break;
                }
            }
            if (!Replacement && Charts.size() == 1) Replacement = Charts[0].UvNew;
        }
        if (!Replacement) return std::nullopt;
        // Guard against 2D UV triangle inversion inside a chart.
        std::array<Point2, 3> Before, After;
        for (uint32_t Corner = 0; Corner < 3; ++Corner)
        {
            Before[Corner] = ReadCornerUv(LiveCornerUvs_, Triangle, Corner);
            After[Corner]  = static_cast<int>(Corner) == TargetCorner ? *Replacement : Before[Corner];
        }
        const double AreaBefore = UvSignedArea2(Before[0], Before[1], Before[2]);
        const double AreaAfter  = UvSignedArea2(After[0], After[1], After[2]);
        if (std::fabs(AreaBefore) > 1e-10 && AreaBefore * AreaAfter <= 1e-12) return std::nullopt;
        Updates.push_back({Triangle, static_cast<uint32_t>(TargetCorner), *Replacement});
    }
    return Updates;
}

SimplifiedGroup GroupSimplification::Run()
{
    Locked_.assign(VertexCount_, 0);
    auto OnOpenBorder = [&](const IncidentEdge& Entry) {
        if (!Options_.NeedleGuard || !OpenBorder_ || Entry.Triangles.size() != 1) return false;
        const uint32_t Triangle = Entry.Triangles[0];
        for (uint32_t Corner = 0; Corner < 3; ++Corner)
        {
            const uint32_t V0 = static_cast<uint32_t>(LiveTriangles_[Triangle * 3 + Corner]);
            const uint32_t V1 = static_cast<uint32_t>(LiveTriangles_[Triangle * 3 + (Corner + 1) % 3]);
            if ((V0 == Entry.A && V1 == Entry.B) || (V0 == Entry.B && V1 == Entry.A)) return ((*OpenBorder_)[Triangle] & (1u << Corner)) != 0;
        }
        return false;
    };
    for (const IncidentEdge& Entry : Edges_.Edges)
    {
        const bool BorderEdge   = Entry.Triangles.size() != 2 && !OnOpenBorder(Entry);
        bool       FamilyBorder = false;
        if (Entry.Triangles.size() >= 2)
            for (uint32_t Triangle : Entry.Triangles)
                if (Families_[Triangle] != Families_[Entry.Triangles[0]]) FamilyBorder = true;
        if (BorderEdge || (FamilyBorder && !Options_.ClassBorderCollapse))
        {
            Locked_[Entry.A] = 1;
            Locked_[Entry.B] = 1;
        }
        if (Entry.Sharp)
            for (uint32_t Triangle : Entry.Triangles)
                for (uint32_t Corner = 0; Corner < 3; ++Corner)
                {
                    const uint32_t V0 = static_cast<uint32_t>(LiveTriangles_[Triangle * 3 + Corner]);
                    const uint32_t V1 = static_cast<uint32_t>(LiveTriangles_[Triangle * 3 + (Corner + 1) % 3]);
                    if ((V0 == Entry.A && V1 == Entry.B) || (V0 == Entry.B && V1 == Entry.A)) LiveSharpMask_[Triangle] |= static_cast<uint8_t>(1u << Corner);
                }
    }

    Quadrics_.assign(VertexCount_, Quadric{});
    for (uint32_t Triangle = 0; Triangle < TriangleCount_; ++Triangle)
    {
        const Point3 A    = LivePosition(static_cast<uint32_t>(LiveTriangles_[Triangle * 3]));
        const Point3 Raw  = TriangleNormal(LivePositions_, LiveTriangles_, Triangle);
        const double Area = Length(Raw) / 2;
        if (!(Area > 0)) continue;
        const Point3 Normal = Normalize(Raw);
        // Weight surface plane quadric slightly higher where UV area is dense relative to 3D area.
        const Point2 U0 = ReadCornerUv(LiveCornerUvs_, Triangle, 0);
        const Point2 U1 = ReadCornerUv(LiveCornerUvs_, Triangle, 1);
        const Point2 U2 = ReadCornerUv(LiveCornerUvs_, Triangle, 2);
        const double UvArea = std::fabs(UvSignedArea2(U0, U1, U2)) * 0.5;
        const double Weight = Area * (1 + JsMin(2.0, UvArea / JsMax(Area, 1e-8)));
        const Quadric Plane = QuadricFromPlane(Normal, -Dot(Normal, A), Weight);
        for (uint32_t Corner = 0; Corner < 3; ++Corner)
        {
            Quadric& Target = Quadrics_[static_cast<uint32_t>(LiveTriangles_[Triangle * 3 + Corner])];
            for (int C = 0; C < 10; ++C) Target[C] += Plane[C];
        }
    }
    // Feature-line alignment quadrics for marked sharp edges and UV seams: two orthogonal planes intersecting along the 3D edge
    // line penalize bending a crease or seam while letting collinear runs collapse freely.
    for (const IncidentEdge& Entry : Edges_.Edges)
    {
        const bool ClassBorder = (Options_.ClassBorderCollapse && Entry.Triangles.size() == 2 &&
                                  Families_[Entry.Triangles[0]] != Families_[Entry.Triangles[1]]) || OnOpenBorder(Entry);
        if (!Entry.Sharp && !Entry.UvSeam && !ClassBorder) continue;
        const Point3 PA = LivePosition(Entry.A), PB = LivePosition(Entry.B);
        const Point3 Span = Subtract(PB, PA);
        const double EdgeLength = Length(Span);
        if (!(EdgeLength > 1e-12)) continue;
        const Point3 Tangent = {Span[0] / EdgeLength, Span[1] / EdgeLength, Span[2] / EdgeLength};
        const auto [U, V] = OrthogonalBasis(Tangent);
        const double FeatureWeight = (Entry.Sharp || ClassBorder ? 6.0 : 4.0) * EdgeLength * EdgeLength;
        const Quadric Plane1 = QuadricFromPlane(U, -Dot(U, PA), FeatureWeight);
        const Quadric Plane2 = QuadricFromPlane(V, -Dot(V, PA), FeatureWeight);
        for (uint32_t Endpoint : {Entry.A, Entry.B})
            for (int C = 0; C < 10; ++C) Quadrics_[Endpoint][C] += Plane1[C] + Plane2[C];
    }

    Alive_.assign(TriangleCount_, 1);
    for (uint32_t Triangle = 0; Triangle < TriangleCount_; ++Triangle)
        for (uint32_t Corner = 0; Corner < 3; ++Corner) VertexTriangles_.Add(static_cast<uint32_t>(LiveTriangles_[Triangle * 3 + Corner]), Triangle);
    Remap_.resize(VertexCount_);
    for (uint32_t Vertex = 0; Vertex < VertexCount_; ++Vertex) Remap_[Vertex] = static_cast<int32_t>(Vertex);
    Versions_.assign(VertexCount_, 0);

    std::vector<std::array<uint32_t, 2>> Candidates;
    Candidates.reserve(Edges_.Edges.size());
    for (const IncidentEdge& Entry : Edges_.Edges) Candidates.push_back({Entry.A, Entry.B});
    std::stable_sort(Candidates.begin(), Candidates.end());
    for (const auto& [A, B] : Candidates)
        if (std::optional<CollapseCandidate> Item = Evaluate(A, B)) HeapPush(*Item);

    uint32_t Remaining    = TriangleCount_;
    double   MaxDeviation = 0.0;
    const uint64_t SharpStride = VertexCount_;
    auto SharpKey = [SharpStride](uint64_t U, uint64_t V) { return U < V ? U * SharpStride + V : V * SharpStride + U; };
    while (Remaining > Target_ && !Heap_.empty())
    {
        const CollapseCandidate Item = HeapPop();
        const uint32_t A = Resolve(Item.A), B = Resolve(Item.B);
        if (A == B) continue;
        if (A != Item.A || B != Item.B || Versions_[A] != Item.VersionA || Versions_[B] != Item.VersionB)
        {
            if (std::optional<CollapseCandidate> Fresh = Evaluate(A, B)) HeapPush(*Fresh);
            continue;
        }
        const VertexFeatures FA = AnalyzeVertexFeatures(A), FB = AnalyzeVertexFeatures(B);
        const NeighbourFeature* EdgeFeature = FA.Find(B);
        if (!EdgeFeature || (FA.IsFeature && FB.IsFeature && !(EdgeFeature->Sharp || EdgeFeature->UvSeam))) continue;
        const bool PinA = Locked_[A] || FA.IsFeatureCorner || (FA.IsFeature && !FB.IsFeature);
        const bool PinB = Locked_[B] || FB.IsFeatureCorner || (FB.IsFeature && !FA.IsFeature);
        if (PinA && PinB) continue;
        if (PinA != Item.PinA || PinB != Item.PinB)
        {
            if (std::optional<CollapseCandidate> Fresh = Evaluate(A, B)) HeapPush(*Fresh);
            continue;
        }
        const uint32_t Keeper  = PinA ? A : PinB ? B : A;
        const uint32_t Removed = Keeper == A ? B : A;
        if (Flips(Removed, Keeper, Item.Position) || Flips(Keeper, Removed, Item.Position)) continue;
        const std::optional<std::vector<UvUpdate>> UvUpdates = ComputeCornerUvUpdates(Keeper, Removed, Item.Position);
        if (!UvUpdates) continue;

        std::vector<uint32_t> DyingVertices = {Keeper, Removed};
        bool Orphans = false;
        for (uint32_t Triangle : VertexTriangles_[Removed])
        {
            if (!Alive_[Triangle]) continue;
            const std::array<uint32_t, 3> Corners = ResolvedCorners(Triangle);
            if (IndexOf(Corners, Keeper) < 0) continue;
            for (uint32_t Vertex : Corners) AppendUnique(DyingVertices, Vertex);
        }
        double DyingDeviation = 0.0;
        for (uint32_t Vertex : DyingVertices)
        {
            if (Vertex == Removed) continue;
            bool Survives = false;
            for (uint32_t Triangle : VertexTriangles_[Vertex])
            {
                if (!Alive_[Triangle]) continue;
                const std::array<uint32_t, 3> Corners = ResolvedCorners(Triangle);
                if (!(IndexOf(Corners, Keeper) >= 0 && IndexOf(Corners, Removed) >= 0))
                {
                    Survives = true;
                    break;
                }
            }
            if (Survives) continue;
            if (Vertex == Keeper || Locked_[Vertex])
            {
                Orphans = true;
                break;
            }
            DyingDeviation = JsMax(DyingDeviation, Length(Subtract(LivePosition(Vertex), Item.Position)));
        }
        if (Orphans) continue;

        // Record sharp edges among the affected triangles before remapping Removed -> Keeper.
        std::vector<uint32_t> Affected = VertexTriangles_[Removed];
        for (uint32_t Triangle : VertexTriangles_[Keeper]) AppendUnique(Affected, Triangle);
        std::unordered_set<uint64_t> SharpPairs;
        for (uint32_t Triangle : Affected)
        {
            if (!Alive_[Triangle]) continue;
            const std::array<uint32_t, 3> Corners = ResolvedCorners(Triangle);
            for (uint32_t Edge = 0; Edge < 3; ++Edge)
                if ((LiveSharpMask_[Triangle] & (1u << Edge)) != 0)
                {
                    const uint32_t U = Corners[Edge] == Removed ? Keeper : Corners[Edge];
                    const uint32_t V = Corners[(Edge + 1) % 3] == Removed ? Keeper : Corners[(Edge + 1) % 3];
                    if (U != V) SharpPairs.insert(SharpKey(U, V));
                }
        }

        // Collapse Removed into Keeper at the evaluated position and apply the UV updates.
        for (int Axis = 0; Axis < 3; ++Axis) LivePositions_[static_cast<size_t>(Keeper) * 3 + Axis] = Item.Position[Axis];
        for (int C = 0; C < 10; ++C) Quadrics_[Keeper][C] += Quadrics_[Removed][C];
        for (const UvUpdate& Update : *UvUpdates)
        {
            const size_t Base = static_cast<size_t>(Update.Triangle) * 6 + Update.Corner * 2;
            LiveCornerUvs_[Base]     = Update.Uv[0];
            LiveCornerUvs_[Base + 1] = Update.Uv[1];
        }
        Remap_[Removed] = static_cast<int32_t>(Keeper);
        MaxDeviation    = JsMax(JsMax(MaxDeviation, Item.Deviation), DyingDeviation);
        for (uint32_t Triangle : Affected)
        {
            if (!Alive_[Triangle]) continue;
            const std::array<uint32_t, 3> Corners = ResolvedCorners(Triangle);
            if (Corners[0] == Corners[1] || Corners[1] == Corners[2] || Corners[0] == Corners[2])
            {
                Alive_[Triangle] = 0;
                --Remaining;
                continue;
            }
            uint8_t Mask = 0;
            for (uint32_t Corner = 0; Corner < 3; ++Corner)
            {
                LiveTriangles_[Triangle * 3 + Corner] = static_cast<int32_t>(Corners[Corner]);
                VertexTriangles_.Add(Corners[Corner], Triangle);
                if (SharpPairs.contains(SharpKey(Corners[Corner], Corners[(Corner + 1) % 3]))) Mask |= static_cast<uint8_t>(1u << Corner);
            }
            LiveSharpMask_[Triangle] = Mask;
        }
        VertexTriangles_.Clear(Removed);
        ++Versions_[Keeper];
        std::vector<uint32_t> Neighbours;
        for (uint32_t Triangle : VertexTriangles_[Keeper])
            for (uint32_t Corner = 0; Corner < 3; ++Corner)
            {
                const uint32_t Vertex = Resolve(static_cast<uint32_t>(LiveTriangles_[Triangle * 3 + Corner]));
                if (Vertex != Keeper) AppendUnique(Neighbours, Vertex);
            }
        for (uint32_t Vertex : Neighbours)
            if (std::optional<CollapseCandidate> Fresh = Evaluate(Keeper, Vertex)) HeapPush(*Fresh);
    }

    // Compact surviving geometry with per-corner UVs and sharp-edge masks.
    SimplifiedGroup Out;
    std::unordered_map<uint32_t, uint32_t> Compact;
    auto EmitVertex = [&](uint32_t Vertex) {
        Vertex = Resolve(Vertex);
        auto [Slot, Fresh] = Compact.try_emplace(Vertex, Out.VertexCount());
        if (Fresh)
            for (int Axis = 0; Axis < 3; ++Axis) Out.Positions.push_back(LivePositions_[static_cast<size_t>(Vertex) * 3 + Axis]);
        return Slot->second;
    };
    for (uint32_t Triangle = 0; Triangle < TriangleCount_; ++Triangle)
    {
        if (!Alive_[Triangle]) continue;
        for (uint32_t Corner = 0; Corner < 3; ++Corner)
        {
            Out.Triangles.push_back(EmitVertex(static_cast<uint32_t>(LiveTriangles_[Triangle * 3 + Corner])));
            Out.CornerUvs.push_back(LiveCornerUvs_[static_cast<size_t>(Triangle) * 6 + Corner * 2]);
            Out.CornerUvs.push_back(LiveCornerUvs_[static_cast<size_t>(Triangle) * 6 + Corner * 2 + 1]);
        }
        Out.Families.push_back(Families_[Triangle]);
        Out.SharpMask.push_back(LiveSharpMask_[Triangle]);
    }
    Out.Deviation = MaxDeviation;
    for (uint8_t Value : Locked_) Out.LockedCount += Value;
    return Out;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     NESTING CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] std::vector<std::vector<uint32_t>> GroupClusters(const TierGeometry& Geometry, const std::vector<std::vector<uint32_t>>& Clusters,
                                                               uint32_t GroupLimit)
{
    const uint32_t TriangleCount = Geometry.TriangleCount();
    std::vector<int32_t> ClusterOf(TriangleCount, -1);
    for (uint32_t Identity = 0; Identity < Clusters.size(); ++Identity)
        for (uint32_t Triangle : Clusters[Identity]) ClusterOf[Triangle] = static_cast<int32_t>(Identity);
    const EdgeIncidence Incidence = ConstructEdgeIncidence(Geometry.Triangles, Geometry.VertexCount());
    std::vector<std::vector<uint32_t>> ClusterAdjacent(Clusters.size());
    for (uint32_t Triangle = 0; Triangle < TriangleCount; ++Triangle)
        for (uint32_t Other : Incidence.Adjacent[Triangle])
            if (ClusterOf[Other] != ClusterOf[Triangle]) ClusterAdjacent[ClusterOf[Triangle]].push_back(static_cast<uint32_t>(ClusterOf[Other]));
    for (std::vector<uint32_t>& List : ClusterAdjacent)
    {
        std::sort(List.begin(), List.end());
        List.erase(std::unique(List.begin(), List.end()), List.end());
    }
    std::vector<int32_t> Assigned(Clusters.size(), -1);
    std::vector<std::vector<uint32_t>> Groups;
    for (uint32_t Seed = 0; Seed < Clusters.size(); ++Seed)
    {
        if (Assigned[Seed] >= 0) continue;
        const int32_t Identity = static_cast<int32_t>(Groups.size());
        std::vector<uint32_t> Members = {Seed};
        Assigned[Seed] = Identity;
        std::vector<uint32_t> Frontier = ClusterAdjacent[Seed];
        while (Members.size() < GroupLimit && !Frontier.empty())
        {
            const uint32_t Next = Frontier.front();
            Frontier.erase(Frontier.begin());
            if (Assigned[Next] >= 0) continue;
            Assigned[Next] = Identity;
            Members.push_back(Next);
            for (uint32_t Other : ClusterAdjacent[Next])
                if (Assigned[Other] < 0 && std::find(Frontier.begin(), Frontier.end(), Other) == Frontier.end()) Frontier.push_back(Other);
            std::sort(Frontier.begin(), Frontier.end());
        }
        std::sort(Members.begin(), Members.end());
        Groups.push_back(std::move(Members));
    }
    const size_t MinimumGroup = std::max<uint32_t>(2, GroupLimit >> 2);
    bool Repaired = true;
    while (Repaired)
    {
        Repaired = false;
        for (uint32_t Group = 0; Group < Groups.size(); ++Group)
        {
            if (Groups[Group].empty() || Groups[Group].size() >= MinimumGroup) continue;
            std::vector<std::pair<uint32_t, uint32_t>> Neighbours;   // (group, shared adjacencies), sorted by group below
            for (uint32_t Member : Groups[Group])
                for (uint32_t Other : ClusterAdjacent[Member])
                {
                    const uint32_t OtherGroup = static_cast<uint32_t>(Assigned[Other]);
                    if (OtherGroup != Group && !Groups[OtherGroup].empty())
                    {
                        auto Hit = std::find_if(Neighbours.begin(), Neighbours.end(), [&](const auto& Entry) { return Entry.first == OtherGroup; });
                        if (Hit == Neighbours.end()) Neighbours.push_back({OtherGroup, 1});
                        else ++Hit->second;
                    }
                }
            std::sort(Neighbours.begin(), Neighbours.end());
            int64_t Target = -1;
            double  TargetSize = Unbounded;
            int64_t TargetShared = -1;
            for (const auto& [OtherGroup, Shared] : Neighbours)
            {
                const size_t Size = Groups[OtherGroup].size();
                if (Size + Groups[Group].size() > static_cast<size_t>(GroupLimit) * 2) continue;
                if (static_cast<double>(Size) < TargetSize || (static_cast<double>(Size) == TargetSize && static_cast<int64_t>(Shared) > TargetShared))
                {
                    Target       = OtherGroup;
                    TargetSize   = static_cast<double>(Size);
                    TargetShared = Shared;
                }
            }
            if (Target < 0) continue;
            for (uint32_t Member : Groups[Group])
            {
                Assigned[Member] = static_cast<int32_t>(Target);
                Groups[Target].push_back(Member);
            }
            Groups[Group].clear();
            Repaired = true;
        }
    }
    std::vector<std::vector<uint32_t>> Kept;
    for (std::vector<uint32_t>& Members : Groups)
        if (!Members.empty())
        {
            std::sort(Members.begin(), Members.end());
            Kept.push_back(std::move(Members));
        }
    return Kept;
}

[[nodiscard]] TierGeometry ExtractGroupGeometry(const TierGeometry& Geometry, const std::vector<uint32_t>& TriangleList)
{
    TierGeometry Local;
    std::unordered_map<uint32_t, uint32_t> Slots;
    auto Emit = [&](uint32_t Vertex) {
        auto [Slot, Fresh] = Slots.try_emplace(Vertex, Local.VertexCount());
        if (Fresh)
            for (int Axis = 0; Axis < 3; ++Axis) Local.Positions.push_back(Geometry.Positions[static_cast<size_t>(Vertex) * 3 + Axis]);
        return Slot->second;
    };
    for (uint32_t Triangle : TriangleList)
    {
        for (uint32_t Corner = 0; Corner < 3; ++Corner)
        {
            Local.Triangles.push_back(Emit(Geometry.Triangles[Triangle * 3 + Corner]));
            Local.CornerUvs.push_back(Geometry.CornerUvs[static_cast<size_t>(Triangle) * 6 + Corner * 2]);
            Local.CornerUvs.push_back(Geometry.CornerUvs[static_cast<size_t>(Triangle) * 6 + Corner * 2 + 1]);
        }
        Local.Families.push_back(Geometry.Families[Triangle]);
        Local.SharpMask.push_back(Geometry.SharpMask[Triangle]);
    }
    return Local;
}

struct WeldedTier : TierGeometry
{
    std::vector<uint64_t> PartitionKeys{};
    std::vector<uint32_t> GroupOf{};
};
// 📝 Exact-position weld for concatenated group output; triangle order is preserved so finer-cluster attribution via GroupOf
//    stays aligned, and corner UVs and sharp masks travel with their surviving triangles.
[[nodiscard]] WeldedTier WeldTierGeometry(const TierGeometry& Concatenated, const std::vector<uint64_t>& PartitionKeys,
                                          const std::vector<uint32_t>& GroupOf)
{
    WeldedTier Out;
    std::unordered_map<PositionKey, uint32_t, PositionKeyHash> Welded;
    std::vector<uint32_t> Remap(Concatenated.VertexCount());
    for (uint32_t Vertex = 0; Vertex < Concatenated.VertexCount(); ++Vertex)
    {
        const Point3 P = Concatenated.ReadPosition(Vertex);
        auto [Slot, Fresh] = Welded.try_emplace(KeyOf(P[0], P[1], P[2]), Out.VertexCount());
        if (Fresh) Out.Positions.insert(Out.Positions.end(), P.begin(), P.end());
        Remap[Vertex] = Slot->second;
    }
    for (uint32_t Triangle = 0; Triangle < Concatenated.TriangleCount(); ++Triangle)
    {
        const std::array<uint32_t, 3> Corners = {Remap[Concatenated.Triangles[Triangle * 3]], Remap[Concatenated.Triangles[Triangle * 3 + 1]],
                                                 Remap[Concatenated.Triangles[Triangle * 3 + 2]]};
        if (Corners[0] == Corners[1] || Corners[1] == Corners[2] || Corners[0] == Corners[2]) continue;
        Out.Triangles.insert(Out.Triangles.end(), Corners.begin(), Corners.end());
        Out.Families.push_back(Concatenated.Families[Triangle]);
        Out.PartitionKeys.push_back(PartitionKeys[Triangle]);
        Out.GroupOf.push_back(GroupOf[Triangle]);
        for (uint32_t Value = 0; Value < 6; ++Value) Out.CornerUvs.push_back(Concatenated.CornerUvs[static_cast<size_t>(Triangle) * 6 + Value]);
        Out.SharpMask.push_back(Concatenated.SharpMask[Triangle]);
    }
    return Out;
}

// 📝 Closest point on a triangle by Voronoi region (Ericson, Real-Time Collision Detection §5.1.5), squared distance.
[[nodiscard]] double SquaredTriangleDistance(const Point3& P, const Point3& A, const Point3& B, const Point3& C) noexcept
{
    const Point3 AB = Subtract(B, A), AC = Subtract(C, A), AP = Subtract(P, A);
    const double D1 = Dot(AB, AP), D2 = Dot(AC, AP);
    Point3 Q = A;
    if (!(D1 <= 0 && D2 <= 0))
    {
        const Point3 BP = Subtract(P, B), CP = Subtract(P, C);
        const double D3 = Dot(AB, BP), D4 = Dot(AC, BP), D5 = Dot(AB, CP), D6 = Dot(AC, CP);
        const double VC = D1 * D4 - D3 * D2, VB = D5 * D2 - D1 * D6, VA = D3 * D6 - D5 * D4;
        auto Along = [](const Point3& Origin, const Point3& Span, double T) {
            return Point3{Origin[0] + Span[0] * T, Origin[1] + Span[1] * T, Origin[2] + Span[2] * T};
        };
        if (D3 >= 0 && D4 <= D3) Q = B;
        else if (VC <= 0 && D1 >= 0 && D3 <= 0) Q = Along(A, AB, D1 / (D1 - D3));
        else if (D6 >= 0 && D5 <= D6) Q = C;
        else if (VB <= 0 && D2 >= 0 && D6 <= 0) Q = Along(A, AC, D2 / (D2 - D6));
        else if (VA <= 0 && (D4 - D3) >= 0 && (D5 - D6) >= 0) Q = Along(B, Subtract(C, B), (D4 - D3) / ((D4 - D3) + (D5 - D6)));
        else
        {
            const double Scale = 1 / (VA + VB + VC);
            Q = Along(Along(A, AB, VB * Scale), AC, VC * Scale);
        }
    }
    const Point3 Offset = Subtract(P, Q);
    return Dot(Offset, Offset);
}

using TriangleCorners = std::array<Point3, 3>;

// 📝 Uniform grid over triangle boxes for nearest-triangle queries: a query widens ring by ring until no unvisited cell can hold a
//    nearer triangle. Ties keep the first triangle met, so queries are deterministic.
class TriangleProximity
{
  public:
    explicit TriangleProximity(const std::vector<TriangleCorners>& Facets) : Facets_(Facets)
    {
        Minimum_ = {Unbounded, Unbounded, Unbounded};
        Point3 Maximum{-Unbounded, -Unbounded, -Unbounded};
        for (const TriangleCorners& T : Facets_)
            for (const Point3& P : T)
                for (int A = 0; A < 3; ++A)
                {
                    Minimum_[A] = std::min(Minimum_[A], P[A]);
                    Maximum[A]  = std::max(Maximum[A], P[A]);
                }
        const double Extent = std::max({Maximum[0] - Minimum_[0], Maximum[1] - Minimum_[1], Maximum[2] - Minimum_[2], 1e-9});
        Cell_ = Extent / (std::cbrt(static_cast<double>(std::max<size_t>(1, Facets_.size()))) * 1.5 + 1);
        for (int A = 0; A < 3; ++A) Count_[A] = std::max(1, static_cast<int>(std::ceil((Maximum[A] - Minimum_[A]) / Cell_)) + 1);
        Bins_.assign(static_cast<size_t>(Count_[0]) * Count_[1] * Count_[2], {});
        for (uint32_t Index = 0; Index < Facets_.size(); ++Index)
        {
            std::array<int, 3> Low{}, High{};
            for (int A = 0; A < 3; ++A)
            {
                Low[A]  = Clamp(static_cast<int>((std::min({Facets_[Index][0][A], Facets_[Index][1][A], Facets_[Index][2][A]}) - Minimum_[A]) / Cell_), A);
                High[A] = Clamp(static_cast<int>((std::max({Facets_[Index][0][A], Facets_[Index][1][A], Facets_[Index][2][A]}) - Minimum_[A]) / Cell_), A);
            }
            for (int X = Low[0]; X <= High[0]; ++X)
                for (int Y = Low[1]; Y <= High[1]; ++Y)
                    for (int Z = Low[2]; Z <= High[2]; ++Z) Bins_[Bin(X, Y, Z)].push_back(Index);
        }
    }
    // 📝 Squared distance to the nearest triangle and its index.
    [[nodiscard]] std::pair<double, uint32_t> Nearest(const Point3& P) const
    {
        std::array<int, 3> Home{};
        double Outside = 0.0;
        for (int A = 0; A < 3; ++A)
        {
            Home[A] = Clamp(static_cast<int>(std::floor((P[A] - Minimum_[A]) / Cell_)), A);
            Outside = std::max({Outside, Minimum_[A] - P[A], P[A] - (Minimum_[A] + Count_[A] * Cell_)});
        }
        double   Best = Unbounded;
        uint32_t Hit  = 0;
        const int Rings = std::max({Count_[0], Count_[1], Count_[2]});
        for (int Ring = 0; Ring <= Rings; ++Ring)
        {
            const double Reach = std::max(0.0, (Ring - 1) * Cell_ + Outside);
            if (Reach * Reach > Best) break;
            for (int X = std::max(0, Home[0] - Ring); X <= std::min(Count_[0] - 1, Home[0] + Ring); ++X)
                for (int Y = std::max(0, Home[1] - Ring); Y <= std::min(Count_[1] - 1, Home[1] + Ring); ++Y)
                    for (int Z = std::max(0, Home[2] - Ring); Z <= std::min(Count_[2] - 1, Home[2] + Ring); ++Z)
                    {
                        if (std::max({std::abs(X - Home[0]), std::abs(Y - Home[1]), std::abs(Z - Home[2])}) != Ring) continue;
                        for (uint32_t Index : Bins_[Bin(X, Y, Z)])
                        {
                            const double Distance = SquaredTriangleDistance(P, Facets_[Index][0], Facets_[Index][1], Facets_[Index][2]);
                            if (Distance < Best || (Distance == Best && Index < Hit))
                            {
                                Best = Distance;
                                Hit  = Index;
                            }
                        }
                    }
        }
        return {Best, Hit};
    }
  private:
    [[nodiscard]] int Clamp(int Value, int Axis) const noexcept { return std::clamp(Value, 0, Count_[Axis] - 1); }
    [[nodiscard]] size_t Bin(int X, int Y, int Z) const noexcept { return (static_cast<size_t>(X) * Count_[1] + Y) * Count_[2] + Z; }
    const std::vector<TriangleCorners>& Facets_;
    Point3                              Minimum_{};
    double                              Cell_ = 1.0;
    std::array<int, 3>                  Count_{1, 1, 1};
    std::vector<std::vector<uint32_t>>  Bins_{};
};

[[nodiscard]] TriangleCorners ReadTriangle(const TierGeometry& Geometry, uint32_t Triangle)
{
    return {Geometry.ReadPosition(Geometry.Triangles[Triangle * 3]), Geometry.ReadPosition(Geometry.Triangles[Triangle * 3 + 1]),
            Geometry.ReadPosition(Geometry.Triangles[Triangle * 3 + 2])};
}
[[nodiscard]] Point3 Centroid(const TriangleCorners& T) noexcept
{
    return {(T[0][0] + T[1][0] + T[2][0]) / 3, (T[0][1] + T[1][1] + T[2][1]) / 3, (T[0][2] + T[1][2] + T[2][2]) / 3};
}

// 📝 I3: two-sided deviation between a simplified group and the source leaf triangles it stands in for, sampled at the vertices,
//    edge midpoints and centroids of the simplified triangles and at the vertices and centroids of the leaves.
[[nodiscard]] double MeasureGroupDeviation(const TierGeometry& Simplified, const TierGeometry& Leaf, const std::vector<uint32_t>& LeafTriangles)
{
    std::vector<TriangleCorners> Coarse, Source;
    for (uint32_t Triangle = 0; Triangle < Simplified.TriangleCount(); ++Triangle) Coarse.push_back(ReadTriangle(Simplified, Triangle));
    for (uint32_t Triangle : LeafTriangles) Source.push_back(ReadTriangle(Leaf, Triangle));
    if (Coarse.empty() || Source.empty()) return 0.0;
    const TriangleProximity CoarseGrid(Coarse), SourceGrid(Source);
    auto Midpoint = [](const Point3& A, const Point3& B) { return Point3{(A[0] + B[0]) / 2, (A[1] + B[1]) / 2, (A[2] + B[2]) / 2}; };
    double Worst = 0.0;
    for (const TriangleCorners& T : Coarse)
        for (const Point3& Sample : {T[0], T[1], T[2], Midpoint(T[0], T[1]), Midpoint(T[1], T[2]), Midpoint(T[2], T[0]), Centroid(T)})
            Worst = std::max(Worst, SourceGrid.Nearest(Sample).first);
    for (const TriangleCorners& T : Source)
        for (const Point3& Sample : {T[0], T[1], T[2], Centroid(T)}) Worst = std::max(Worst, CoarseGrid.Nearest(Sample).first);
    return std::sqrt(Worst);
}

// 📝 I2: Function(Index) for every index on up to Threads workers; the first exception is rethrown after every worker joined.
template <typename Function> void ForEachParallel(uint32_t Count, uint32_t Threads, const Function& Work)
{
    const uint32_t Workers = std::min<uint32_t>(Threads == 0 ? std::max(1u, std::thread::hardware_concurrency()) : Threads, Count);
    if (Workers <= 1)
    {
        for (uint32_t Index = 0; Index < Count; ++Index) Work(Index);
        return;
    }
    std::atomic<uint32_t> Cursor{0};
    std::atomic<bool>     Failed{false};
    std::exception_ptr    Failure;
    std::vector<std::thread> Pool;
    for (uint32_t Worker = 0; Worker < Workers; ++Worker)
        Pool.emplace_back([&]() {
            for (uint32_t Index = Cursor++; Index < Count && !Failed; Index = Cursor++)
            {
                try
                {
                    Work(Index);
                }
                catch (...)
                {
                    if (!Failed.exchange(true)) Failure = std::current_exception();
                }
            }
        });
    for (std::thread& Thread : Pool) Thread.join();
    if (Failure) std::rethrow_exception(Failure);
}

[[nodiscard]] std::string PositionKeyText(const Point3& P)
{
    std::string Text(24, '\0');
    const PositionKey Key = KeyOf(P[0], P[1], P[2]);
    std::memcpy(Text.data(), Key.Bits.data(), 24);
    return Text;
}
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     WELD
//------------------------------------------------------------------------------------------------------------------------

// 📝 Soup corners weld on exact 3D position bits so connectivity across UV seams and hard edges survives. Explicit sharp edges,
//    authored corner-normal discontinuities and an optional dihedral crease angle all mark sharp edges.
WeldedSoup WeldFacetSoup(const SurfaceSoup& Source, const NestingOptions& Options)
{
    WeldedSoup Out;
    std::vector<double> CornerNormals;
    std::unordered_map<PositionKey, uint32_t, PositionKeyHash> Welded;
    bool HasCornerNormals = false;
    auto Weld = [&](const Point3& P) {
        auto [Slot, Fresh] = Welded.try_emplace(KeyOf(P[0], P[1], P[2]), Out.VertexCount());
        if (Fresh) Out.Positions.insert(Out.Positions.end(), P.begin(), P.end());
        return Slot->second;
    };
    for (const SoupFacet& Facet : Source.Facets)
    {
        if (Facet.CarriesUv) Out.HasUvs = true;
        if (Facet.CarriesNormals) HasCornerNormals = true;
        for (uint32_t Corner = 0; Corner < 3; ++Corner)
        {
            Out.Triangles.push_back(Weld(Facet.Corners[Corner]));
            Out.CornerUvs.push_back(Facet.CarriesUv ? Facet.CornerUv[Corner][0] : 0.0);
            Out.CornerUvs.push_back(Facet.CarriesUv ? Facet.CornerUv[Corner][1] : 0.0);
            for (int Axis = 0; Axis < 3; ++Axis) CornerNormals.push_back(Facet.CarriesNormals ? Facet.Normals[Corner][Axis] : 0.0);
        }
        Out.SharpMask.push_back(static_cast<uint8_t>(Facet.SharpEdges & 7u));
        Out.Families.push_back(Facet.Family);
    }
    const uint64_t VertexCount = Out.VertexCount();
    std::unordered_set<uint64_t> ExplicitSharp;
    auto EdgeIdentity = [VertexCount](uint64_t A, uint64_t B) { return A < B ? A * VertexCount + B : B * VertexCount + A; };
    for (const auto& Pair : Source.SharpVertexPairs)
        if (Pair[0] != Pair[1]) ExplicitSharp.insert(EdgeIdentity(Pair[0], Pair[1]));
    for (const auto& Pair : Source.SharpPointPairs)
    {
        const auto A = Welded.find(KeyOf(Pair[0][0], Pair[0][1], Pair[0][2]));
        const auto B = Welded.find(KeyOf(Pair[1][0], Pair[1][1], Pair[1][2]));
        if (A != Welded.end() && B != Welded.end() && A->second != B->second) ExplicitSharp.insert(EdgeIdentity(A->second, B->second));
    }

    // Propagate explicit sharp edges, authored normal discontinuities and optional dihedral crease angles.
    const std::optional<double> CreaseCosine =
        Options.SharpAngleDegrees && std::isfinite(*Options.SharpAngleDegrees) ? std::optional<double>(std::cos((*Options.SharpAngleDegrees * CirclePi) / 180))
                                                                               : std::nullopt;
    const double NormalCosine = Options.SharpNormalCosine.value_or(DefaultSharpNormalCosine);
    EdgeIncidence Incidence = ConstructEdgeIncidence(Out.Triangles, Out.VertexCount(), &Out.CornerUvs, &Out.SharpMask);
    const std::vector<uint32_t>& Triangles = Out.Triangles;
    for (IncidentEdge& Entry : Incidence.Edges)
    {
        bool IsSharp = Entry.Sharp || ExplicitSharp.contains(EdgeIdentity(Entry.A, Entry.B));
        if (!IsSharp && CreaseCosine && Entry.Triangles.size() == 2)
        {
            const Point3 N0 = Normalize(TriangleNormal(Out.Positions, Triangles, Entry.Triangles[0]));
            const Point3 N1 = Normalize(TriangleNormal(Out.Positions, Triangles, Entry.Triangles[1]));
            if (Dot(N0, N1) < *CreaseCosine) IsSharp = true;
        }
        if (!IsSharp && HasCornerNormals && Entry.Triangles.size() == 2)
        {
            const uint32_t T0 = Entry.Triangles[0], T1 = Entry.Triangles[1];
            for (uint32_t SharedVertex : {Entry.A, Entry.B})
            {
                int C0 = -1, C1 = -1;
                for (int C = 0; C < 3; ++C)
                {
                    if (Triangles[T0 * 3 + C] == SharedVertex) C0 = C;
                    if (Triangles[T1 * 3 + C] == SharedVertex) C1 = C;
                }
                if (C0 >= 0 && C1 >= 0)
                {
                    const size_t Base0 = (static_cast<size_t>(T0) * 3 + C0) * 3, Base1 = (static_cast<size_t>(T1) * 3 + C1) * 3;
                    const Point3 N0 = {CornerNormals[Base0], CornerNormals[Base0 + 1], CornerNormals[Base0 + 2]};
                    const Point3 N1 = {CornerNormals[Base1], CornerNormals[Base1 + 1], CornerNormals[Base1 + 2]};
                    if (Length(N0) > 1e-6 && Length(N1) > 1e-6 && Dot(Normalize(N0), Normalize(N1)) < NormalCosine)
                    {
                        IsSharp = true;
                        break;
                    }
                }
            }
        }
        if (IsSharp)
        {
            Entry.Sharp = true;
            for (uint32_t Triangle : Entry.Triangles)
                for (uint32_t Corner = 0; Corner < 3; ++Corner)
                {
                    const uint32_t V0 = Triangles[Triangle * 3 + Corner], V1 = Triangles[Triangle * 3 + (Corner + 1) % 3];
                    if ((V0 == Entry.A && V1 == Entry.B) || (V0 == Entry.B && V1 == Entry.A)) Out.SharpMask[Triangle] |= static_cast<uint8_t>(1u << Corner);
                }
        }
    }
    return Out;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     NESTING
//------------------------------------------------------------------------------------------------------------------------

GeometryNesting ConstructGeometryNesting(const SurfaceSoup& Source, const NestingOptions& Options)
{
    const uint32_t TriangleLimit = Options.TriangleLimit, VertexLimit = Options.VertexLimit, GroupLimit = Options.GroupLimit;
    const double   TargetRatio   = Options.TargetRatio;
    if (!(TriangleLimit >= 1) || !(VertexLimit >= 3) || !(GroupLimit >= 1) || !(TargetRatio > 0 && TargetRatio < 1))
        throw std::invalid_argument("Geometry nesting limits are invalid");
    auto ClassOf = [&](uint32_t Family) -> uint32_t {
        return std::find(Options.TransmissiveFamilies.begin(), Options.TransmissiveFamilies.end(), Family) != Options.TransmissiveFamilies.end() ? 1u : 0u;
    };

    WeldedSoup Geometry = WeldFacetSoup(Source, Options);
    GeometryNesting Nesting;
    Nesting.SourceTriangles = Geometry.TriangleCount();
    Nesting.WeldedVertices  = Geometry.VertexCount();
    Nesting.HasUvs          = Geometry.HasUvs;
    std::vector<NestedCluster>& Clusters = Nesting.Clusters;

    // 📝 Partition keys: "class:<c>" on the welded soup, "group:<g>:class:<c>" afterwards; only equality matters.
    TierGeometry          TierSource = static_cast<const TierGeometry&>(Geometry);
    std::vector<uint64_t> PartitionKeys(TierSource.TriangleCount());
    for (uint32_t Triangle = 0; Triangle < PartitionKeys.size(); ++Triangle) PartitionKeys[Triangle] = ClassOf(TierSource.Families[Triangle]);
    struct FinerGroups
    {
        std::vector<std::vector<uint32_t>> Groups{};
        std::vector<uint32_t>              GroupOf{};
        std::vector<double>                GroupDeviation{};
        std::vector<std::vector<uint32_t>> Leaves{};   // [-] I3: source leaf triangles per group, ascending
    };
    std::optional<FinerGroups> Finer;
    std::vector<std::vector<uint32_t>> ClusterLeaves;   // [-] I3: source leaf triangles each cluster stands in for
    for (uint32_t Tier = 0; Tier < Options.TierLimit; ++Tier)
    {
        const std::vector<std::vector<uint32_t>> TierClusters = PartitionClusters(TierSource, PartitionKeys, TriangleLimit, VertexLimit);
        const EdgeIncidence TierIncidence = ConstructEdgeIncidence(TierSource.Triangles, TierSource.VertexCount(), &TierSource.CornerUvs, &TierSource.SharpMask);
        const uint32_t FirstCluster = static_cast<uint32_t>(Clusters.size());
        std::vector<uint32_t> TierClusterIdentities;
        for (const std::vector<uint32_t>& Members : TierClusters)
        {
            std::vector<uint32_t> FinerClusters;
            if (Finer)
            {
                std::vector<uint32_t> SourceGroups;
                for (uint32_t Triangle : Members) AppendUnique(SourceGroups, Finer->GroupOf[Triangle]);
                for (uint32_t Group : SourceGroups) FinerClusters.insert(FinerClusters.end(), Finer->Groups[Group].begin(), Finer->Groups[Group].end());
            }
            double FinerError = 0.0;
            for (uint32_t Child : FinerClusters) FinerError = JsMax(FinerError, Clusters[Child].Error);
            const double GroupStep = Finer ? JsMax(Finer->GroupDeviation[Finer->GroupOf[Members[0]]], 1e-5) : 0.0;
            // 📝 I3: the group's measured deviation from the source, kept monotone by the finer errors (plus the demo's minimum step),
            //    instead of a bound that sums every tier's own deviation.
            const double Error = Tier == 0                   ? 0.0
                                 : Options.MeasuredDeviation ? JsMax(FinerError + 1e-5, Finer->GroupDeviation[Finer->GroupOf[Members[0]]])
                                                             : FinerError + GroupStep;
            if (Options.MeasuredDeviation) ClusterLeaves.push_back(Finer ? std::vector<uint32_t>{} : Members);
            ClusterMeasure Measured = MeasureCluster(TierSource, Members, Error);
            const Point3        TightMinimum = Measured.Minimum, TightMaximum = Measured.Maximum;
            const NestingSphere TightSphere  = Measured.Sphere;
            // Union over finer clusters so coarser bounds conservatively contain every cluster they stand in for.
            for (uint32_t Child : FinerClusters)
                for (int Axis = 0; Axis < 3; ++Axis)
                {
                    Measured.Minimum[Axis] = JsMin(Measured.Minimum[Axis], Clusters[Child].Minimum[Axis]);
                    Measured.Maximum[Axis] = JsMax(Measured.Maximum[Axis], Clusters[Child].Maximum[Axis]);
                }
            if (!FinerClusters.empty())
            {
                const Point3 Center = {(Measured.Minimum[0] + Measured.Maximum[0]) / 2, (Measured.Minimum[1] + Measured.Maximum[1]) / 2,
                                       (Measured.Minimum[2] + Measured.Maximum[2]) / 2};
                double Radius = Length(Subtract(Measured.Sphere.Center, Center)) + Measured.Sphere.Radius;
                double Cutoff = Measured.Cone.Cutoff;
                for (uint32_t Child : FinerClusters)
                {
                    const NestedCluster& Node = Clusters[Child];
                    Radius = JsMax(Radius, Length(Subtract(Node.Sphere.Center, Center)) + Node.Sphere.Radius);
                    const double Alignment = JsMax(-1.0, JsMin(1.0, Dot(Measured.Cone.Axis, Node.Cone.Axis)));
                    const double Worst = Alignment * Node.Cone.Cutoff -
                                         std::sqrt(JsMax(0.0, 1 - Node.Cone.Cutoff * Node.Cone.Cutoff)) * std::sqrt(JsMax(0.0, 1 - Alignment * Alignment));
                    Cutoff = JsMin(Cutoff, Worst);
                }
                Measured.Sphere      = {Center, Radius};
                Measured.Cone.Cutoff = Cutoff;
            }
            uint32_t ClassMask = 0, SharpEdgesInCluster = 0;
            for (uint32_t Triangle : Members)
            {
                ClassMask |= 1u << ClassOf(TierSource.Families[Triangle]);
                const uint32_t Mask = TierSource.SharpMask[Triangle];
                SharpEdgesInCluster += (Mask & 1u) + ((Mask >> 1) & 1u) + ((Mask >> 2) & 1u);
            }
            NestedCluster Node;
            Node.Identity      = static_cast<uint32_t>(Clusters.size());
            Node.Tier          = Tier;
            std::sort(FinerClusters.begin(), FinerClusters.end());
            Node.Finer         = std::move(FinerClusters);
            Node.FirstTriangle = Members.empty() ? 0 : Members[0];
            Node.Triangles     = Members;
            Node.TriangleCount = static_cast<uint32_t>(Members.size());
            Node.Minimum       = Measured.Minimum;
            Node.Maximum       = Measured.Maximum;
            Node.Sphere        = Measured.Sphere;
            Node.Cone          = Measured.Cone;
            Node.TightMinimum  = TightMinimum;
            Node.TightMaximum  = TightMaximum;
            Node.TightSphere   = TightSphere;
            Node.Error         = Error;
            Node.CoarserSphere = Measured.Sphere;
            Node.ClassMask     = ClassMask;
            Node.SharpEdgesInCluster = SharpEdgesInCluster;
            TierClusterIdentities.push_back(Node.Identity);
            Clusters.push_back(std::move(Node));
        }
        // Link coarser clusters back to their finer simplification groups so every cluster in a group shares the exact same
        // coarser error and enclosing coarser sphere for crack-free runtime cuts.
        if (Finer)
        {
            std::vector<std::vector<uint32_t>> CoarserByGroup(Finer->Groups.size());
            for (uint32_t CoarserIdentity : TierClusterIdentities)
            {
                const uint32_t SourceGroup = Finer->GroupOf[Clusters[CoarserIdentity].FirstTriangle];
                if (SourceGroup < CoarserByGroup.size()) CoarserByGroup[SourceGroup].push_back(CoarserIdentity);
            }
            for (uint32_t GroupIndex = 0; GroupIndex < Finer->Groups.size(); ++GroupIndex)
            {
                const std::vector<uint32_t>& GroupMembers  = Finer->Groups[GroupIndex];
                const std::vector<uint32_t>& GroupCoarser  = CoarserByGroup[GroupIndex];
                if (GroupCoarser.empty()) continue;
                Point3 GMin{Unbounded, Unbounded, Unbounded}, GMax{-Unbounded, -Unbounded, -Unbounded};
                std::vector<uint32_t> Everyone = GroupMembers;
                Everyone.insert(Everyone.end(), GroupCoarser.begin(), GroupCoarser.end());
                for (uint32_t Identity : Everyone)
                    for (int A = 0; A < 3; ++A)
                    {
                        GMin[A] = JsMin(GMin[A], Clusters[Identity].Minimum[A]);
                        GMax[A] = JsMax(GMax[A], Clusters[Identity].Maximum[A]);
                    }
                double GroupCoarserError = 0.0;
                for (uint32_t Identity : GroupCoarser) GroupCoarserError = JsMax(GroupCoarserError, Clusters[Identity].Error);
                const Point3 Center = {(GMin[0] + GMax[0]) / 2, (GMin[1] + GMax[1]) / 2, (GMin[2] + GMax[2]) / 2};
                double Radius = 0.0;
                for (uint32_t Identity : Everyone)
                    Radius = JsMax(Radius, Length(Subtract(Clusters[Identity].Sphere.Center, Center)) + Clusters[Identity].Sphere.Radius);
                const NestingSphere Shared = {Center, Radius};
                for (uint32_t Identity : GroupCoarser) Clusters[Identity].Sphere = Shared;
                for (uint32_t Identity : GroupMembers)
                {
                    NestedCluster& Member = Clusters[Identity];
                    Member.GroupIdentity = static_cast<int32_t>(GroupIndex);
                    Member.Coarser       = GroupCoarser;
                    Member.CoarserError  = JsMax(GroupCoarserError, Member.Error + 1e-5);
                    Member.CoarserSphere = Shared;
                }
            }
        }
        if (Options.MeasuredDeviation && Finer)
        {
            // 📝 I3: every source leaf of a finer group belongs to the coarser cluster of that group nearest to it, so a cluster's
            //    leaves are its own region and a coarser group's measured deviation compares like with like.
            std::vector<std::vector<uint32_t>> CoarserOfGroup(Finer->Groups.size());
            for (uint32_t Identity : TierClusterIdentities)
            {
                const uint32_t SourceGroup = Finer->GroupOf[Clusters[Identity].FirstTriangle];
                if (SourceGroup < CoarserOfGroup.size()) CoarserOfGroup[SourceGroup].push_back(Identity);
            }
            ForEachParallel(static_cast<uint32_t>(Finer->Groups.size()), Options.Threads, [&](uint32_t GroupIndex) {
                std::vector<TriangleCorners> Facets;
                std::vector<uint32_t>        Owner;
                for (uint32_t Identity : CoarserOfGroup[GroupIndex])
                    for (uint32_t Triangle : Clusters[Identity].Triangles)
                    {
                        Facets.push_back(ReadTriangle(TierSource, Triangle));
                        Owner.push_back(Identity);
                    }
                if (Facets.empty()) return;
                const TriangleProximity Grid(Facets);
                for (uint32_t Leaf : Finer->Leaves[GroupIndex])
                    ClusterLeaves[Owner[Grid.Nearest(Centroid(ReadTriangle(Nesting.Tiers[0], Leaf))).second]].push_back(Leaf);
            });
        }
        NestingTier TierEntry;
        static_cast<TierGeometry&>(TierEntry) = TierSource;
        TierEntry.Tier            = Tier;
        TierEntry.Clusters        = TierClusterIdentities;
        TierEntry.SharpEdgeCount  = TierIncidence.SharpEdgeCount;
        TierEntry.UvSeamEdgeCount = TierIncidence.UvSeamEdgeCount;
        Nesting.Tiers.push_back(std::move(TierEntry));
        if (TierSource.TriangleCount() <= TriangleLimit) break;
        // 📝 I4: edges with one triangle in the whole tier are the surface's own open border (not a group border).
        std::vector<uint8_t> TierOpenBorder;
        if (Options.NeedleGuard)
        {
            TierOpenBorder.assign(TierSource.TriangleCount(), 0);
            for (const IncidentEdge& Entry : TierIncidence.Edges)
            {
                if (Entry.Triangles.size() != 1) continue;
                const uint32_t Triangle = Entry.Triangles[0];
                for (uint32_t Corner = 0; Corner < 3; ++Corner)
                {
                    const uint32_t V0 = TierSource.Triangles[Triangle * 3 + Corner], V1 = TierSource.Triangles[Triangle * 3 + (Corner + 1) % 3];
                    if ((V0 == Entry.A && V1 == Entry.B) || (V0 == Entry.B && V1 == Entry.A)) TierOpenBorder[Triangle] |= static_cast<uint8_t>(1u << Corner);
                }
            }
        }
        const uint32_t EffectiveGroupLimit = static_cast<uint32_t>(std::min<double>(static_cast<double>(GroupLimit) * std::ldexp(1.0, static_cast<int>(Tier)), 32.0));
        const std::vector<std::vector<uint32_t>> Groups = GroupClusters(TierSource, TierClusters, EffectiveGroupLimit);
        TierGeometry Concatenated;
        std::vector<uint64_t> CoarserKeys;
        FinerGroups Next;
        bool Progress = false;
        // 📝 I2: groups simplify independently; results merge in group order, so any thread count gives the serial build's bits.
        std::vector<SimplifiedGroup> Results(Groups.size());
        std::vector<uint32_t>        LocalTriangles(Groups.size());
        std::vector<std::vector<uint32_t>> GroupLeaves(Options.MeasuredDeviation ? Groups.size() : 0);
        auto SimplifyOne = [&](uint32_t GroupIdentity) {
            std::vector<uint32_t> TriangleList;
            for (uint32_t Cluster : Groups[GroupIdentity])
                TriangleList.insert(TriangleList.end(), TierClusters[Cluster].begin(), TierClusters[Cluster].end());
            std::sort(TriangleList.begin(), TriangleList.end());
            const TierGeometry Local = ExtractGroupGeometry(TierSource, TriangleList);
            const uint32_t Target = static_cast<uint32_t>(JsMax(1.0, std::ceil(static_cast<double>(Local.TriangleCount()) * TargetRatio)));
            std::vector<uint8_t> LocalOpenBorder;
            if (Options.NeedleGuard)
                for (uint32_t Triangle : TriangleList) LocalOpenBorder.push_back(TierOpenBorder[Triangle]);
            Results[GroupIdentity] = GroupSimplification(Local, Target, Options, Options.NeedleGuard ? &LocalOpenBorder : nullptr).Run();
            LocalTriangles[GroupIdentity] = static_cast<uint32_t>(Local.Triangles.size());
            if (Options.MeasuredDeviation)
            {
                std::vector<uint32_t>& Leaves = GroupLeaves[GroupIdentity];
                for (uint32_t Cluster : Groups[GroupIdentity])
                {
                    const std::vector<uint32_t>& Own = ClusterLeaves[FirstCluster + Cluster];
                    Leaves.insert(Leaves.end(), Own.begin(), Own.end());
                }
                std::sort(Leaves.begin(), Leaves.end());
                Leaves.erase(std::unique(Leaves.begin(), Leaves.end()), Leaves.end());
                Results[GroupIdentity].Deviation = MeasureGroupDeviation(Results[GroupIdentity], Nesting.Tiers[0], Leaves);
            }
        };
        ForEachParallel(static_cast<uint32_t>(Groups.size()), Options.Threads, SimplifyOne);
        for (uint32_t GroupIdentity = 0; GroupIdentity < Groups.size(); ++GroupIdentity)
        {
            const std::vector<uint32_t>& Members = Groups[GroupIdentity];
            const SimplifiedGroup& Simplified = Results[GroupIdentity];
            if (Simplified.Triangles.size() < LocalTriangles[GroupIdentity]) Progress = true;
            const uint32_t Base = Concatenated.VertexCount();
            Concatenated.Positions.insert(Concatenated.Positions.end(), Simplified.Positions.begin(), Simplified.Positions.end());
            for (uint32_t Index : Simplified.Triangles) Concatenated.Triangles.push_back(Index + Base);
            Concatenated.CornerUvs.insert(Concatenated.CornerUvs.end(), Simplified.CornerUvs.begin(), Simplified.CornerUvs.end());
            Concatenated.SharpMask.insert(Concatenated.SharpMask.end(), Simplified.SharpMask.begin(), Simplified.SharpMask.end());
            for (uint32_t Family : Simplified.Families)
            {
                Concatenated.Families.push_back(Family);
                CoarserKeys.push_back(((static_cast<uint64_t>(GroupIdentity) + 1) << 1) | ClassOf(Family));
                Next.GroupOf.push_back(GroupIdentity);
            }
            Next.GroupDeviation.push_back(Simplified.Deviation);
            std::vector<uint32_t> GroupMembers;
            for (uint32_t Cluster : Members) GroupMembers.push_back(FirstCluster + Cluster);
            Next.Groups.push_back(std::move(GroupMembers));
        }
        if (Options.MeasuredDeviation) Next.Leaves = std::move(GroupLeaves);
        if (!Progress) break;
        WeldedTier Welded = WeldTierGeometry(Concatenated, CoarserKeys, Next.GroupOf);
        Next.GroupOf  = std::move(Welded.GroupOf);
        PartitionKeys = std::move(Welded.PartitionKeys);
        TierSource    = static_cast<TierGeometry&&>(std::move(Welded));
        Finer         = std::move(Next);
    }
    return Nesting;
}

NestingOptions RefinedNestingOptions()
{
    NestingOptions Options;
    Options.MeasuredDeviation   = true;
    Options.NeedleGuard         = true;
    Options.ClassBorderCollapse = true;
    Options.Threads             = 0;
    return Options;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     VALIDATION
//------------------------------------------------------------------------------------------------------------------------

NestingValidation ValidateGeometryNesting(const SurfaceSoup& Source, const GeometryNesting& Nesting, const NestingOptions& Options)
{
    NestingValidation Checks;
    auto Confirm = [&](bool Condition, const char* Caption) {
        if (!Condition)
        {
            Checks.Passed = false;
            Checks.Failures.emplace_back(Caption);
        }
    };
    Confirm(Nesting.SourceTriangles == Source.Facets.size(), "source triangle count changed during weld");
    const NestingTier& Leaf = Nesting.Tiers[0];
    std::vector<int32_t> Covered(Leaf.TriangleCount(), 0);
    for (uint32_t Identity : Leaf.Clusters)
    {
        const NestedCluster& Node = Nesting.Clusters[Identity];
        Confirm(Node.TriangleCount <= Options.TriangleLimit, "leaf cluster exceeds the triangle limit");
        std::unordered_set<uint32_t> Vertices;
        for (uint32_t Triangle : Node.Triangles)
        {
            ++Covered[Triangle];
            for (uint32_t Corner = 0; Corner < 3; ++Corner) Vertices.insert(Leaf.Triangles[Triangle * 3 + Corner]);
        }
        Confirm(Vertices.size() <= Options.VertexLimit, "leaf cluster exceeds the vertex limit");
        Confirm(Node.Error == 0, "leaf cluster carries simplification error");
    }
    Confirm(std::all_of(Covered.begin(), Covered.end(), [](int32_t Count) { return Count == 1; }),
            "leaf clusters do not cover every source triangle exactly once");

    // Source UV bounds: every tier must stay inside the source UV domain.
    Point2 UvMin = {Unbounded, Unbounded}, UvMax = {-Unbounded, -Unbounded};
    for (size_t Index = 0; Index + 1 < Leaf.CornerUvs.size(); Index += 2)
    {
        UvMin[0] = JsMin(UvMin[0], Leaf.CornerUvs[Index]);
        UvMin[1] = JsMin(UvMin[1], Leaf.CornerUvs[Index + 1]);
        UvMax[0] = JsMax(UvMax[0], Leaf.CornerUvs[Index]);
        UvMax[1] = JsMax(UvMax[1], Leaf.CornerUvs[Index + 1]);
    }
    for (const NestingTier& Tier : Nesting.Tiers)
    {
        Confirm(Tier.CornerUvs.size() == static_cast<size_t>(Tier.TriangleCount()) * 6, "tier corner UVs disagree with the triangle count");
        for (size_t Index = 0; Index + 1 < Tier.CornerUvs.size(); Index += 2)
        {
            const double U = Tier.CornerUvs[Index], V = Tier.CornerUvs[Index + 1];
            Confirm(std::isfinite(U) && std::isfinite(V), "non-finite UV coordinate in nesting tier");
            Confirm(U >= UvMin[0] - 1e-5 && U <= UvMax[0] + 1e-5 && V >= UvMin[1] - 1e-5 && V <= UvMax[1] + 1e-5,
                    "interpolated UV escaped the source UV domain");
        }
        Confirm(Tier.SharpMask.size() == Tier.TriangleCount(), "tier sharp mask disagrees with the triangle count");
        if (Tier.Tier == 0) continue;
        std::unordered_set<std::string> CoarserPositions;
        for (uint32_t Vertex = 0; Vertex < Tier.VertexCount(); ++Vertex) CoarserPositions.insert(PositionKeyText(Tier.ReadPosition(Vertex)));
        const NestingTier& FinerTier = Nesting.Tiers[Tier.Tier - 1];
        const EdgeIncidence FinerEdges = ConstructEdgeIncidence(FinerTier.Triangles, FinerTier.VertexCount(), &FinerTier.CornerUvs, &FinerTier.SharpMask);
        for (uint32_t Identity : Tier.Clusters)
        {
            const NestedCluster& Node = Nesting.Clusters[Identity];
            Confirm(!Node.Finer.empty(), "coarser cluster has no finer clusters");
            double FinerError = -Unbounded;
            for (uint32_t Child : Node.Finer) FinerError = JsMax(FinerError, Nesting.Clusters[Child].Error);
            Confirm(Node.Error >= FinerError, "cluster error is not monotonic toward the coarsest tier");
            std::unordered_set<uint32_t> UnionTriangles;
            for (uint32_t Child : Node.Finer)
            {
                const NestedCluster& ChildNode = Nesting.Clusters[Child];
                Confirm(ChildNode.CoarserError >= ChildNode.Error, "finer cluster's coarser error is below its own error");
                for (int A = 0; A < 3; ++A)
                {
                    Confirm(Node.Minimum[A] <= ChildNode.Minimum[A] + 1e-9, "coarser bounds do not contain finer bounds");
                    Confirm(Node.Maximum[A] >= ChildNode.Maximum[A] - 1e-9, "coarser bounds do not contain finer bounds");
                }
                UnionTriangles.insert(ChildNode.Triangles.begin(), ChildNode.Triangles.end());
            }
            for (const IncidentEdge& Entry : FinerEdges.Edges)
            {
                size_t Inside = 0;
                for (uint32_t Triangle : Entry.Triangles) Inside += UnionTriangles.contains(Triangle) ? 1 : 0;
                if (!Inside || (Inside == Entry.Triangles.size() && Entry.Triangles.size() == 2)) continue;
                // 📝 I4: the surface's own open border has no neighbour to crack against; with the needle guard it may slide.
                if (Options.NeedleGuard && Entry.Triangles.size() == 1) continue;
                for (uint32_t Vertex : {Entry.A, Entry.B})
                    Confirm(CoarserPositions.contains(PositionKeyText(FinerTier.ReadPosition(Vertex))),
                            "shared border vertex moved during simplification (crack)");
            }
        }
    }
    for (const NestingTier& Tier : Nesting.Tiers)
    {
        NestingValidation::TierSummary Summary;
        Summary.Tier       = Tier.Tier;
        Summary.Clusters   = static_cast<uint32_t>(Tier.Clusters.size());
        Summary.Triangles  = Tier.TriangleCount();
        Summary.Vertices   = Tier.VertexCount();
        Summary.SharpEdges = Tier.SharpEdgeCount;
        Summary.UvSeams    = Tier.UvSeamEdgeCount;
        for (uint32_t Identity : Tier.Clusters) Summary.MaximumError = JsMax(Summary.MaximumError, Nesting.Clusters[Identity].Error);
        Checks.Tiers.push_back(Summary);
    }
    return Checks;
}

TierSoup CollectNestingTier(const GeometryNesting& Nesting, uint32_t Tier)
{
    if (Tier >= Nesting.Tiers.size()) throw std::out_of_range("Nesting tier does not exist: " + std::to_string(Tier));
    const NestingTier& Entry = Nesting.Tiers[Tier];
    const EdgeIncidence Incidence = ConstructEdgeIncidence(Entry.Triangles, Entry.VertexCount(), &Entry.CornerUvs, &Entry.SharpMask);
    TierSoup Soup;
    for (uint32_t Identity : Entry.Clusters)
    {
        const NestedCluster& Node = Nesting.Clusters[Identity];
        for (uint32_t Triangle : Node.Triangles)
        {
            uint8_t SeamBits = 0;
            for (uint32_t Corner = 0; Corner < 3; ++Corner)
            {
                const uint32_t Vertex     = Entry.Triangles[Triangle * 3 + Corner];
                const uint32_t NextVertex = Entry.Triangles[Triangle * 3 + (Corner + 1) % 3];
                const Point3   P          = Entry.ReadPosition(Vertex);
                Soup.Positions.insert(Soup.Positions.end(), P.begin(), P.end());
                Soup.CornerUvs.push_back(Entry.CornerUvs[static_cast<size_t>(Triangle) * 6 + Corner * 2]);
                Soup.CornerUvs.push_back(Entry.CornerUvs[static_cast<size_t>(Triangle) * 6 + Corner * 2 + 1]);
                const IncidentEdge* Edge = Incidence.Find(Vertex, NextVertex);
                if (Edge && Edge->UvSeam) SeamBits |= static_cast<uint8_t>(1u << Corner);
            }
            Soup.SharpMask.push_back(Entry.SharpMask[Triangle]);
            Soup.UvSeamMask.push_back(SeamBits);
            Soup.Families.push_back(Entry.Families[Triangle]);
            Soup.Owners.push_back(Identity);
            Soup.Errors.push_back(Node.Error);
        }
    }
    Soup.TriangleCount = static_cast<uint32_t>(Soup.Families.size());
    return Soup;
}
} // namespace Frontier::VirtualizedGeometry
