/*====================================================================================================================================
                                                       QUADRICTIERBUILD.CPP
====================================================================================================================================*/
// 🧩 Implementation of the quadric simplification, cluster grouping and tier sequence declared in QuadricTierBuild.h.

#include "Engine/Geometry/GeometryProcessing/QuadricTierBuild.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace Frontier
{
namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    VECTOR HELPERS
//------------------------------------------------------------------------------------------------------------------------

double Dot(const FacetPoint3D& A, const FacetPoint3D& B) { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
FacetPoint3D Subtract(const FacetPoint3D& A, const FacetPoint3D& B) { return { A.X - B.X, A.Y - B.Y, A.Z - B.Z }; }
FacetPoint3D Add(const FacetPoint3D& A, const FacetPoint3D& B) { return { A.X + B.X, A.Y + B.Y, A.Z + B.Z }; }
FacetPoint3D Scale(const FacetPoint3D& A, double Factor) { return { A.X * Factor, A.Y * Factor, A.Z * Factor }; }
FacetPoint3D Cross(const FacetPoint3D& A, const FacetPoint3D& B)
{
    return { A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X };
}
double Length(const FacetPoint3D& A) { return std::sqrt(Dot(A, A)); }
double Distance(const FacetPoint3D& A, const FacetPoint3D& B) { return Length(Subtract(A, B)); }
FacetPoint3D Normalized(const FacetPoint3D& A)
{
    const double L = Length(A);
    return L > 1.0e-15 ? FacetPoint3D{ A.X / L, A.Y / L, A.Z / L } : FacetPoint3D{ 0.0, 0.0, 0.0 };
}

uint64_t EdgeKey(uint32_t A, uint32_t B)
{
    if (A > B) std::swap(A, B);
    return (uint64_t(A) << 32) | uint64_t(B);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    QUADRIC DEVIATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Symmetric 4×4 deviation quadric stored by its ten distinct coefficients; v = (x, y, z, 1).
struct Quadric
{
    double XX = 0.0, XY = 0.0, XZ = 0.0, XW = 0.0;
    double YY = 0.0, YZ = 0.0, YW = 0.0;
    double ZZ = 0.0, ZW = 0.0;
    double WW = 0.0;

    static Quadric FromPlane(const FacetPoint3D& Normal, double Offset)
    {
        Quadric Q;
        Q.XX = Normal.X * Normal.X; Q.XY = Normal.X * Normal.Y; Q.XZ = Normal.X * Normal.Z;
        Q.XW = Offset * Normal.X;
        Q.YY = Normal.Y * Normal.Y; Q.YZ = Normal.Y * Normal.Z; Q.YW = Offset * Normal.Y;
        Q.ZZ = Normal.Z * Normal.Z; Q.ZW = Offset * Normal.Z;
        Q.WW = Offset * Offset;
        return Q;
    }

    Quadric& operator+=(const Quadric& Other)
    {
        XX += Other.XX; XY += Other.XY; XZ += Other.XZ; XW += Other.XW;
        YY += Other.YY; YZ += Other.YZ; YW += Other.YW;
        ZZ += Other.ZZ; ZW += Other.ZW; WW += Other.WW;
        return *this;
    }

    double Evaluate(const FacetPoint3D& P) const
    {
        return XX * P.X * P.X + 2.0 * XY * P.X * P.Y + 2.0 * XZ * P.X * P.Z + 2.0 * XW * P.X
             + YY * P.Y * P.Y + 2.0 * YZ * P.Y * P.Z + 2.0 * YW * P.Y
             + ZZ * P.Z * P.Z + 2.0 * ZW * P.Z
             + WW;
    }

    // 📝 Minimiser of the quadratic form; false when the 3×3 block is singular.
    bool Minimiser(FacetPoint3D& Out) const
    {
        const double Det = XX * (YY * ZZ - YZ * YZ) - XY * (XY * ZZ - YZ * XZ) + XZ * (XY * YZ - YY * XZ);
        const double Scale = std::max({ std::abs(XX), std::abs(YY), std::abs(ZZ), 1.0e-30 });
        if (std::abs(Det) <= 1.0e-12 * Scale * Scale * Scale) return false;
        const double IX = -XW, IY = -YW, IZ = -ZW;
        const double RX = IX * (YY * ZZ - YZ * YZ) - XY * (IY * ZZ - YZ * IZ) + XZ * (IY * YZ - YY * IZ);
        const double RY = XX * (IY * ZZ - YZ * IZ) - IX * (XY * ZZ - YZ * XZ) + XZ * (XY * IZ - IY * XZ);
        const double RZ = XX * (YY * IZ - IY * YZ) - XY * (XY * IZ - IY * XZ) + IX * (XY * YZ - YY * XZ);
        Out = { RX / Det, RY / Det, RZ / Det };
        return true;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                COLLAPSE STATE (MUTABLE COMPLEX)
//------------------------------------------------------------------------------------------------------------------------

class CollapseWorkspace
{
  public:
    CollapseWorkspace(const PolygonComplex& Source, const SimplificationSpecification& Specification,
                  const std::vector<uint8_t>& LockedVertices)
        : Settings(Specification)
    {
        Positions = Source.Positions;
        CarriesUV = Source.CarriesUVs;
        TriangleCount = uint32_t(Source.Triangles.size() / 3);
        Triangles.resize(TriangleCount);
        CornerUV.resize(TriangleCount);
        Classification = Source.Classification;
        SharpMask = Source.SharpMask;
        TriangleAlive.assign(TriangleCount, 1);
        VertexAlive.assign(Positions.size(), 1);
        VertexLocked.assign(Positions.size(), 0);
        VertexBorder.assign(Positions.size(), 0);
        VertexUV.assign(Positions.size(), { 0.0, 0.0 });
        VertexUVKnown.assign(Positions.size(), 0);
        Incidence.assign(Positions.size(), {});
        VertexQuadric.assign(Positions.size(), {});
        VertexDeviation.assign(Positions.size(), 0.0);

        for (uint32_t Vertex = 0; Vertex < uint32_t(Positions.size()); ++Vertex)
            if (Vertex < LockedVertices.size() && LockedVertices[Vertex]) VertexLocked[Vertex] = 1;


        std::unordered_map<uint64_t, uint32_t> EdgeUse;
        for (uint32_t Triangle = 0; Triangle < TriangleCount; ++Triangle)
        {
            for (uint32_t Corner = 0; Corner < 3; ++Corner)
            {
                Triangles[Triangle][Corner] = Source.Triangles[Triangle * 3 + Corner];
                CornerUV[Triangle][Corner] = Source.CornerUVs.empty()
                    ? std::array<double, 2>{ 0.0, 0.0 } : Source.CornerUVs[Triangle * 3 + Corner];
                Incidence[Triangles[Triangle][Corner]].push_back(Triangle);
                const uint32_t Vertex = Triangles[Triangle][Corner];
                if (!VertexUVKnown[Vertex])
                {
                    VertexUV[Vertex] = CornerUV[Triangle][Corner];
                    VertexUVKnown[Vertex] = 1;
                }
                else if (VertexUV[Vertex] != CornerUV[Triangle][Corner])
                    VertexLocked[Vertex] = 1;   // 📝 seam vertex: two corner UVs on one welded position

            }
            // 📝 Open-border vertices are pinned: a border vertex that slides inward tears the silhouette.
            for (uint32_t Corner = 0; Corner < 3; ++Corner)
            {
                const uint32_t C0 = Triangles[Triangle][Corner];
                const uint32_t C1 = Triangles[Triangle][(Corner + 1) % 3];
                ++EdgeUse[EdgeKey(C0, C1)];
            }
        }
        // 📝 Open-border vertices may decimate along the border but never collapse into the interior:
        //    a border vertex merging with an interior one either tears the rim or drags it off its plane.
        for (const auto& [Key, Use] : EdgeUse)
            if (Use == 1)
            {
                VertexBorder[uint32_t(Key >> 32)] = 1;
                VertexBorder[uint32_t(Key & 0xFFFFFFFFu)] = 1;
            }
        for (uint32_t Triangle = 0; Triangle < TriangleCount; ++Triangle)
        {
            // 📝 Plane quadrics accumulate onto every corner vertex.
            const FacetPoint3D Normal = TriangleNormal(Triangle);
            const double  Norm   = Length(Normal);
            if (Norm <= 1.0e-15) continue;
            const Quadric Plane = Quadric::FromPlane(
                Scale(Normal, 1.0 / Norm),
                -Dot(Normalized(Normal), Positions[Triangles[Triangle][0]]));
            for (uint32_t Corner = 0; Corner < 3; ++Corner) VertexQuadric[Triangles[Triangle][Corner]] += Plane;
        }
    }

    FacetPoint3D TriangleNormal(uint32_t Triangle) const
    {
        const FacetPoint3D& A = Positions[Triangles[Triangle][0]];
        const FacetPoint3D& B = Positions[Triangles[Triangle][1]];
        const FacetPoint3D& C = Positions[Triangles[Triangle][2]];
        return Cross(Subtract(B, A), Subtract(C, A));
    }

    // 📝 Compact live incidence for one vertex; dead triangles are dropped in place.
    void GatherIncidence(uint32_t Vertex, std::vector<uint32_t>& Out) const
    {
        Out.clear();
        for (const uint32_t Triangle : Incidence[Vertex])
            if (TriangleAlive[Triangle] && Contains(Triangle, Vertex)) Out.push_back(Triangle);
    }

    bool Contains(uint32_t Triangle, uint32_t Vertex) const
    {
        return Triangles[Triangle][0] == Vertex || Triangles[Triangle][1] == Vertex || Triangles[Triangle][2] == Vertex;
    }

    uint32_t LiveTriangleCount() const
    {
        return uint32_t(std::count(TriangleAlive.begin(), TriangleAlive.end(), uint8_t(1)));
    }

    // 📝 Incident triangles of an edge; also reports whether a coincident triangle marks it sharp.
    void QueryEdge(uint32_t A, uint32_t B, std::vector<uint32_t>& Incident, bool& Sharp) const
    {
        Incident.clear();
        Sharp = false;
        for (const uint32_t Triangle : Incidence[A])
        {
            if (!TriangleAlive[Triangle] || !Contains(Triangle, B)) continue;
            // 📝 Incidence lists accumulate duplicates across merges; an edge must count each facet once or a
            //    manifold edge reads as non-manifold and the simplifier deadlocks.
            if (std::find(Incident.begin(), Incident.end(), Triangle) != Incident.end()) continue;
            Incident.push_back(Triangle);
            for (uint32_t Corner = 0; Corner < 3; ++Corner)
            {
                const uint32_t C0 = Triangles[Triangle][Corner];
                const uint32_t C1 = Triangles[Triangle][(Corner + 1) % 3];
                if ((C0 == A && C1 == B) || (C0 == B && C1 == A))
                    if (SharpMask[Triangle] & uint8_t(1u << Corner)) Sharp = true;
            }
        }
    }

    // 📝 Collapse target: the quadric minimiser, falling back to midpoint then endpoints; the cheapest wins.
    FacetPoint3D ChooseCollapsePosition(uint32_t A, uint32_t B, double& Cost) const
    {
        Quadric Combined = VertexQuadric[A];
        Combined += VertexQuadric[B];
        FacetPoint3D Chosen = Scale(Add(Positions[A], Positions[B]), 0.5);
        if (VertexBorder[A] && VertexBorder[B])
        {
            // 📝 Border-to-border collapse keeps the midpoint: the rim stays exactly on its plane or edge line.
            Cost = Combined.Evaluate(Chosen);
            if (Settings.UvPenalty > 0.0 && (VertexUVKnown[A] || VertexUVKnown[B]))
            {
                const double DU = VertexUV[A][0] - VertexUV[B][0];
                const double DV = VertexUV[A][1] - VertexUV[B][1];
                Cost += Settings.UvPenalty * (DU * DU + DV * DV);
            }
            return Chosen;
        }
        double  Best   = Combined.Evaluate(Chosen);
        FacetPoint3D Minimiser;
        if (Combined.Minimiser(Minimiser))
        {
            const double Candidate = Combined.Evaluate(Minimiser);
            if (std::isfinite(Candidate) && Candidate < Best) { Best = Candidate; Chosen = Minimiser; }
        }
        const double AtA = Combined.Evaluate(Positions[A]);
        if (AtA < Best) { Best = AtA; Chosen = Positions[A]; }
        const double AtB = Combined.Evaluate(Positions[B]);
        if (AtB < Best) { Best = AtB; Chosen = Positions[B]; }
        Cost = std::max(0.0, Best);
        if (Settings.UvPenalty > 0.0 && (VertexUVKnown[A] || VertexUVKnown[B]))
        {
            const double DU = VertexUV[A][0] - VertexUV[B][0];
            const double DV = VertexUV[A][1] - VertexUV[B][1];
            Cost += Settings.UvPenalty * (DU * DU + DV * DV);
        }
        return Chosen;
    }

    // 📝 Full legality test plus application. Returns false (and changes nothing) when the collapse would fold a
    //    facet, duplicate a facet, split a sharp edge or a UV seam, or open a border.
    bool ApplyCollapse(uint32_t A, uint32_t B, const FacetPoint3D& Position, std::vector<uint32_t>& Incident,
                       std::vector<uint32_t>& Affected)
    {
        if (A == B || !VertexAlive[A] || !VertexAlive[B]) return false;
        if (VertexLocked[A] || VertexLocked[B]) return false;
        if (VertexBorder[A] != VertexBorder[B]) return false;
        bool Sharp = false;
        QueryEdge(A, B, Incident, Sharp);
        if (Incident.empty() || Incident.size() > 2) return false;
        if (Sharp) return false;
        if (Classification[Incident[0]] != Classification[Incident[1 < Incident.size() ? 1 : 0]]) return false;

        // 📝 Affected set: every live triangle touching either endpoint.
        Affected.clear();
        for (const uint32_t Triangle : Incidence[A]) if (TriangleAlive[Triangle] && Contains(Triangle, A)) Affected.push_back(Triangle);
        for (const uint32_t Triangle : Incidence[B])
            if (TriangleAlive[Triangle] && Contains(Triangle, B) &&
                std::find(Affected.begin(), Affected.end(), Triangle) == Affected.end())
                Affected.push_back(Triangle);

        // 📝 Runaway guard: an ill-conditioned quadric can place the minimiser far outside the edge it collapses.
        const double Span = std::max(Distance(Positions[A], Positions[B]), 1.0e-12);
        if (Distance(Position, Positions[A]) > 4.0 * Span && Distance(Position, Positions[B]) > 4.0 * Span) return false;

        // 📝 Fold test: every surviving facet keeps its facing; degenerate results are refused.
        std::vector<std::array<uint32_t, 3>> Remapped;
        std::vector<std::array<uint32_t, 3>> Originals;
        for (const uint32_t Triangle : Affected)
        {
            const FacetPoint3D OldNormal = TriangleNormal(Triangle);
            const double  OldLength = Length(OldNormal);
            if (OldLength <= 1.0e-15) return false;
            std::array<uint32_t, 3> Next = Triangles[Triangle];
            for (uint32_t& Corner : Next) if (Corner == B) Corner = A;
            if (Next[0] == Next[1] || Next[1] == Next[2] || Next[2] == Next[0]) continue;   // collapses away
            FacetPoint3D P0 = Positions[Next[0]], P1 = Positions[Next[1]], P2 = Positions[Next[2]];
            if (Next[0] == A) P0 = Position;
            if (Next[1] == A) P1 = Position;
            if (Next[2] == A) P2 = Position;
            const FacetPoint3D NewNormal = Cross(Subtract(P1, P0), Subtract(P2, P0));
            const double  NewLength = Length(NewNormal);
            if (NewLength <= 1.0e-15) return false;
            if (Dot(Scale(OldNormal, 1.0 / OldLength), Scale(NewNormal, 1.0 / NewLength)) <= Settings.NormalCosineThreshold)
                return false;
            if (CarriesUV)
            {
                double Low = 1.0e30, High = -1.0e30;
                for (uint32_t Corner = 0; Corner < 3; ++Corner)
                {
                    const uint32_t Vertex = Next[Corner];
                    double U = CornerUV[Triangle][Corner][0];
                    if (Vertex == A && VertexUVKnown[A]) U = VertexUV[A][0];
                    Low = std::min(Low, U);
                    High = std::max(High, U);
                }
                // 📝 A facet that spans the U wrap would stretch the chart right across the meridian.
                if (High - Low > 0.5) return false;
            }
            Remapped.push_back(Next);
            Originals.push_back({ Next[0], Next[1], Next[2] });
        }

        // 📝 Duplicate test: no two surviving affected facets may share the same corner set.
        for (size_t I = 0; I < Remapped.size(); ++I)
            for (size_t J = I + 1; J < Remapped.size(); ++J)
            {
                std::array<uint32_t, 3> L = Remapped[I], R = Remapped[J];
                std::sort(L.begin(), L.end());
                std::sort(R.begin(), R.end());
                if (L == R) return false;
            }

        // ① remove the facets that collapsed onto a line
        for (const uint32_t Triangle : Affected)
        {
            std::array<uint32_t, 3> Next = Triangles[Triangle];
            for (uint32_t& Corner : Next) if (Corner == B) Corner = A;
            if (Next[0] == Next[1] || Next[1] == Next[2] || Next[2] == Next[0]) TriangleAlive[Triangle] = 0;
        }
        // ② remap the survivors. Every corner that pointed at B now points at A and carries A's corner UV:
        //    leaving B's UV behind would make the merged vertex two-valued and pin it in the next pass.
        for (const uint32_t Triangle : Affected)
        {
            if (!TriangleAlive[Triangle]) continue;
            for (uint32_t Corner = 0; Corner < 3; ++Corner)
                if (Triangles[Triangle][Corner] == B)
                {
                    Triangles[Triangle][Corner] = A;
                    if (VertexUVKnown[A]) CornerUV[Triangle][Corner] = VertexUV[A];
                }
            Incidence[A].push_back(Triangle);
        }
        // ③ merge vertex state (a UV-seam vertex stays pinned; see the constructor)
        VertexQuadric[A] += VertexQuadric[B];
        VertexDeviation[A] = std::max(VertexDeviation[A] + Distance(Position, Positions[A]),
                                      VertexDeviation[B] + Distance(Position, Positions[B]));
        if (!VertexUVKnown[A] && VertexUVKnown[B]) { VertexUV[A] = VertexUV[B]; VertexUVKnown[A] = 1; }
        Positions[A] = Position;
        VertexAlive[B] = 0;
        for (const uint32_t Triangle : Incidence[B]) if (TriangleAlive[Triangle]) Incidence[A].push_back(Triangle);
        Incidence[B].clear();
        return true;
    }

    std::vector<FacetPoint3D>                Positions;
    std::vector<std::array<uint32_t, 3>> Triangles;
    std::vector<std::array<std::array<double, 2>, 3>> CornerUV;
    std::vector<uint32_t>               Classification;
    std::vector<uint8_t>                SharpMask;
    std::vector<uint8_t>                TriangleAlive;
    std::vector<uint8_t>                VertexAlive;
    std::vector<uint8_t>                VertexLocked;
    std::vector<uint8_t>                VertexBorder;
    std::vector<Quadric>                VertexQuadric;
    std::vector<double>                 VertexDeviation;
    std::vector<std::array<double, 2>>  VertexUV;
    std::vector<uint8_t>                VertexUVKnown;
    std::vector<std::vector<uint32_t>>  Incidence;
    uint32_t                            TriangleCount = 0;
    bool                                CarriesUV = false;
    SimplificationSpecification         Settings;
};

struct EdgeCandidate
{
    double   Cost = 0.0;
    uint32_t A = 0, B = 0;
};

struct EdgeCandidateGreater
{
    bool operator()(const EdgeCandidate& First, const EdgeCandidate& Second) const { return First.Cost > Second.Cost; }
};

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                 PUBLIC — SIMPLIFICATION
//------------------------------------------------------------------------------------------------------------------------

SimplificationOutcome SimplifyPolygonComplex(const PolygonComplex& Source,
                                              const SimplificationSpecification& Specification,
                                              const std::vector<uint8_t>& LockedVertices)
{
    SimplificationOutcome Outcome;
    if (Source.Triangles.empty()) return Outcome;
    CollapseWorkspace Workspace(Source, Specification, LockedVertices);
    const uint32_t SourceCount = Workspace.TriangleCount;
    uint32_t Target = Specification.TargetTriangles;
    if (Target == 0)
        Target = uint32_t(std::max(1.0, std::floor(double(SourceCount) * std::clamp(Specification.TargetFraction, 0.05, 1.0))));
    Target = std::min(Target, SourceCount);

    std::priority_queue<EdgeCandidate, std::vector<EdgeCandidate>, EdgeCandidateGreater> Queue;
    auto PushVertex = [&](uint32_t Vertex)
    {
        std::vector<uint32_t> Incident;
        Workspace.GatherIncidence(Vertex, Incident);
        std::vector<uint32_t> Neighbours;
        for (const uint32_t Triangle : Incident)
            for (uint32_t Corner = 0; Corner < 3; ++Corner)
                if (Workspace.Triangles[Triangle][Corner] != Vertex) Neighbours.push_back(Workspace.Triangles[Triangle][Corner]);
        std::sort(Neighbours.begin(), Neighbours.end());
        Neighbours.erase(std::unique(Neighbours.begin(), Neighbours.end()), Neighbours.end());
        for (const uint32_t Neighbour : Neighbours)
        {
            double Cost = 0.0;
            Workspace.ChooseCollapsePosition(Vertex, Neighbour, Cost);
            Queue.push({ Cost, std::min(Vertex, Neighbour), std::max(Vertex, Neighbour) });
        }
    };
    for (uint32_t Vertex = 0; Vertex < uint32_t(Workspace.Positions.size()); ++Vertex) PushVertex(Vertex);

    uint32_t Live = Workspace.LiveTriangleCount();
    std::vector<uint32_t> Incident, Affected;
    uint32_t Guard = SourceCount * 64u + 1024u;
    while (Live > Target && !Queue.empty() && Guard-- > 0)
    {
        const EdgeCandidate Entry = Queue.top();
        Queue.pop();
        if (!Workspace.VertexAlive[Entry.A] || !Workspace.VertexAlive[Entry.B]) continue;
        double Cost = 0.0;
        const FacetPoint3D Position = Workspace.ChooseCollapsePosition(Entry.A, Entry.B, Cost);
        if (Cost > Entry.Cost + 1.0e-9 * (1.0 + std::abs(Cost)))          // stale entry: a fresher one exists
        {
            Queue.push({ Cost, Entry.A, Entry.B });
            continue;
        }
        const uint32_t Before = Workspace.LiveTriangleCount();
        if (Workspace.ApplyCollapse(Entry.A, Entry.B, Position, Incident, Affected))
        {
            ++Outcome.CollapseCount;
            Live = Workspace.LiveTriangleCount();
            PushVertex(Entry.A);
        }
        else
        {
            ++Outcome.BlockedCollapses;
        }
        (void)Before;
    }

    // 📝 Compact the survivors, keeping corner UVs and the inherited deviation.
    std::vector<uint32_t> Remap(Workspace.Positions.size(), std::numeric_limits<uint32_t>::max());
    PolygonComplex& Result = Outcome.Complex;
    Result.CarriesUVs = Source.CarriesUVs;
    for (uint32_t Triangle = 0; Triangle < Workspace.TriangleCount; ++Triangle)
    {
        if (!Workspace.TriangleAlive[Triangle]) continue;
        for (uint32_t Corner = 0; Corner < 3; ++Corner)
        {
            const uint32_t Vertex = Workspace.Triangles[Triangle][Corner];
            if (Remap[Vertex] == std::numeric_limits<uint32_t>::max())
            {
                Remap[Vertex] = uint32_t(Result.Positions.size());
                Result.Positions.push_back(Workspace.Positions[Vertex]);
                Outcome.VertexDeviation.push_back(Workspace.VertexDeviation[Vertex]);
            }
            Result.Triangles.push_back(Remap[Vertex]);
            Result.CornerUVs.push_back(Workspace.CornerUV[Triangle][Corner]);
        }
        Result.Classification.push_back(Workspace.Classification[Triangle]);
        Result.SharpMask.push_back(Workspace.SharpMask[Triangle]);
    }
    Outcome.TriangleDeviation.assign(Result.Triangles.size() / 3, 0.0);
    for (size_t Triangle = 0; Triangle < Outcome.TriangleDeviation.size(); ++Triangle)
    {
        double Deviation = 0.0;
        for (uint32_t Corner = 0; Corner < 3; ++Corner)
            Deviation = std::max(Deviation, Outcome.VertexDeviation[Result.Triangles[Triangle * 3 + Corner]]);
        Outcome.TriangleDeviation[Triangle] = Deviation;
    }
    return Outcome;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              INTERNAL — TIER CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📝 Re-derive sharp corner bits from edge incidence (authored masks do not survive remapping across tiers).
void ClassifyEdges(PolygonComplex& Complex, bool UseCreaseAngle, double CreaseCosine)
{
    const uint32_t TriangleCount = uint32_t(Complex.Triangles.size() / 3);
    Complex.SharpMask.assign(TriangleCount, 0);
    std::unordered_map<uint64_t, std::vector<std::pair<uint32_t, uint32_t>>> Edges;   // edge → (triangle, corner)
    for (uint32_t Triangle = 0; Triangle < TriangleCount; ++Triangle)
        for (uint32_t Corner = 0; Corner < 3; ++Corner)
            Edges[EdgeKey(Complex.Triangles[Triangle * 3 + Corner], Complex.Triangles[Triangle * 3 + (Corner + 1) % 3])]
                .emplace_back(Triangle, Corner);

    auto FaceNormal = [&](uint32_t Triangle) -> FacetPoint3D
    {
        const FacetPoint3D& A = Complex.Positions[Complex.Triangles[Triangle * 3 + 0]];
        const FacetPoint3D& B = Complex.Positions[Complex.Triangles[Triangle * 3 + 1]];
        const FacetPoint3D& C = Complex.Positions[Complex.Triangles[Triangle * 3 + 2]];
        return Normalized(Cross(Subtract(B, A), Subtract(C, A)));
    };
    for (const auto& [Key, Members] : Edges)
    {
        bool Sharp = Members.size() != 2;   // open borders stay locked
        if (!Sharp && UseCreaseAngle && Members.size() == 2)
            Sharp = Dot(FaceNormal(Members[0].first), FaceNormal(Members[1].first)) < CreaseCosine;
        if (!Sharp) continue;
        for (const auto& [Triangle, Corner] : Members) Complex.SharpMask[Triangle] |= uint8_t(1u << Corner);
    }
}

// 📝 Greedy adjacent grouping of clusters: same classification only, bounded by the tier's group limit.
//    Adjacency is true welded-vertex sharing, so every group is one edge-connected patch.
std::vector<std::vector<uint32_t>> GroupClusters(const PolygonComplex& Complex, const std::vector<FacetCluster>& Clusters,
                                                 uint32_t GroupLimit)
{
    const uint32_t ClusterCount = uint32_t(Clusters.size());
    std::vector<std::vector<uint32_t>> Adjacency(ClusterCount);
    std::unordered_map<uint32_t, std::vector<uint32_t>> Owners;   // vertex → clusters claiming it
    auto Link = [&](uint32_t First, uint32_t Second)
    {
        if (First == Second) return;
        if (Clusters[First].Classification != Clusters[Second].Classification) return;
        if (std::find(Adjacency[First].begin(), Adjacency[First].end(), Second) == Adjacency[First].end())
            Adjacency[First].push_back(Second);
        if (std::find(Adjacency[Second].begin(), Adjacency[Second].end(), First) == Adjacency[Second].end())
            Adjacency[Second].push_back(First);
    };
    for (uint32_t Cluster = 0; Cluster < ClusterCount; ++Cluster)
        for (const uint32_t Triangle : Clusters[Cluster].Triangles)
            for (uint32_t Corner = 0; Corner < 3; ++Corner)
            {
                const uint32_t Vertex = Complex.Triangles[Triangle * 3 + Corner];
                std::vector<uint32_t>& Claims = Owners[Vertex];
                for (const uint32_t Other : Claims) Link(Cluster, Other);
                if (std::find(Claims.begin(), Claims.end(), Cluster) == Claims.end()) Claims.push_back(Cluster);
            }
    std::vector<uint32_t> GroupOf(ClusterCount, std::numeric_limits<uint32_t>::max());
    std::vector<std::vector<uint32_t>> Groups;
    for (uint32_t Seed = 0; Seed < ClusterCount; ++Seed)
    {
        if (GroupOf[Seed] != std::numeric_limits<uint32_t>::max()) continue;
        const uint32_t Group = uint32_t(Groups.size());
        Groups.push_back({});
        GroupOf[Seed] = Group;
        Groups.back().push_back(Seed);
        for (size_t Cursor = 0; Cursor < Groups.back().size() && Groups.back().size() < GroupLimit; ++Cursor)
            for (const uint32_t Neighbour : Adjacency[Groups.back()[Cursor]])
            {
                if (Groups.back().size() >= GroupLimit) break;
                if (GroupOf[Neighbour] != std::numeric_limits<uint32_t>::max()) continue;
                GroupOf[Neighbour] = Group;
                Groups.back().push_back(Neighbour);
            }
    }
    // 📝 A lone cluster is folded into a touching group so every tier actually coarsens.
    for (size_t Group = 0; Group < Groups.size(); ++Group)
    {
        if (Groups[Group].size() != 1) continue;
        const uint32_t Member = Groups[Group][0];
        for (const uint32_t Neighbour : Adjacency[Member])
            if (Groups[GroupOf[Neighbour]].size() < GroupLimit)
            {
                Groups[GroupOf[Neighbour]].push_back(Member);
                GroupOf[Member] = GroupOf[Neighbour];
                Groups[Group].clear();
                break;
            }
    }
    Groups.erase(std::remove_if(Groups.begin(), Groups.end(), [](const auto& Group) { return Group.empty(); }), Groups.end());
    return Groups;
}

// 📝 Extract the facets of a cluster group, locking every vertex also used outside the group so borders never open.
PolygonComplex ExtractGroupGeometry(const PolygonComplex& Complex, const std::vector<FacetCluster>& Clusters,
                                    const std::vector<uint32_t>& Group, std::vector<uint8_t>& LockedVertices,
                                    std::vector<uint32_t>& GroupTriangles)
{
    std::unordered_set<uint32_t> Member;
    for (const uint32_t Cluster : Group) Member.insert(Clusters[Cluster].Triangles.begin(), Clusters[Cluster].Triangles.end());
    std::unordered_set<uint32_t> Members;
    GroupTriangles.assign(Member.begin(), Member.end());
    std::sort(GroupTriangles.begin(), GroupTriangles.end());

    std::unordered_map<uint32_t, uint32_t> UsedInside;
    for (const uint32_t Triangle : GroupTriangles)
        for (uint32_t Corner = 0; Corner < 3; ++Corner) ++UsedInside[Complex.Triangles[Triangle * 3 + Corner]];

    PolygonComplex Extracted;
    Extracted.CarriesUVs = Complex.CarriesUVs;
    std::unordered_map<uint32_t, uint32_t> Remap;
    LockedVertices.clear();
    for (const uint32_t Triangle : GroupTriangles)
        for (uint32_t Corner = 0; Corner < 3; ++Corner)
        {
            const uint32_t Vertex = Complex.Triangles[Triangle * 3 + Corner];
            if (Remap.find(Vertex) == Remap.end())
            {
                Remap.emplace(Vertex, uint32_t(Extracted.Positions.size()));
                Extracted.Positions.push_back(Complex.Positions[Vertex]);
                LockedVertices.push_back(0);
            }
        }
    // A vertex is locked when any other facet of the complex uses it.
    std::unordered_map<uint32_t, uint32_t> GlobalUse;
    for (size_t Triangle = 0; Triangle < Complex.Triangles.size() / 3; ++Triangle)
        for (uint32_t Corner = 0; Corner < 3; ++Corner) ++GlobalUse[Complex.Triangles[Triangle * 3 + Corner]];
    for (const auto& [Vertex, Local] : Remap)
        if (GlobalUse[Vertex] > UsedInside[Vertex]) LockedVertices[Local] = 1;

    for (const uint32_t Triangle : GroupTriangles)
    {
        for (uint32_t Corner = 0; Corner < 3; ++Corner)
        {
            Extracted.Triangles.push_back(Remap[Complex.Triangles[Triangle * 3 + Corner]]);
            Extracted.CornerUVs.push_back(Complex.CarriesUVs ? Complex.CornerUVs[Triangle * 3 + Corner]
                                                         : std::array<double, 2>{ 0.0, 0.0 });
        }
        Extracted.Classification.push_back(Complex.Classification[Triangle]);
        Extracted.SharpMask.push_back(Complex.SharpMask[Triangle]);
    }
    return Extracted;
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                 PUBLIC — TIER SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

ClusterTierSequence BuildClusterTierSequence(const PolygonComplex& Source, const TierBuildSpecification& Specification)
{
    ClusterTierSequence Sequence;
    Sequence.SourceTriangleCount = uint32_t(Source.Triangles.size() / 3);
    if (Sequence.SourceTriangleCount == 0) return Sequence;

    ClusterTier Leaf;
    Leaf.DepthTier = 0;
    Leaf.Complex = Source;
    Leaf.Clusters = PartitionFacetClusters(Source, Specification.TriangleLimit);
    Leaf.VertexDeviation.assign(Source.Positions.size(), 0.0);
    Leaf.ClusterDeviation.assign(Leaf.Clusters.size(), 0.0);
    Leaf.EnclosedClusters.resize(Leaf.Clusters.size());
    Sequence.Tiers.push_back(std::move(Leaf));

    std::vector<uint32_t> TriangleGroup(Sequence.SourceTriangleCount, 0);   // group id per facet of the current tier
    uint32_t DepthTier = 0;
    while (uint32_t(Sequence.Tiers.back().Complex.Triangles.size() / 3) > Specification.TriangleLimit &&
           DepthTier + 1 < Specification.MaximumTiers)
    {
        const uint32_t FinerTriangleCount = uint32_t(Sequence.Tiers.back().Complex.Triangles.size() / 3);
        const ClusterTier& Finer = Sequence.Tiers.back();
        const uint32_t EffectiveGroupLimit = std::max(Specification.GroupLimit,
            std::min(Specification.GroupLimitCeiling, Specification.GroupLimit * (1u << std::min(DepthTier, 8u))));
        const auto Groups = GroupClusters(Finer.Complex, Finer.Clusters, EffectiveGroupLimit);

        // 📝 Group id per finer cluster, used to derive the enclosure links after the coarser partition exists.
        std::vector<uint32_t> FineClusterGroup(Finer.Clusters.size(), 0);
        for (uint32_t Group = 0; Group < uint32_t(Groups.size()); ++Group)
            for (const uint32_t Cluster : Groups[Group]) FineClusterGroup[Cluster] = Group;

        PolygonComplex Coarse;
        Coarse.CarriesUVs = Finer.Complex.CarriesUVs;
        std::vector<double> VertexDeviation;
        std::vector<double> TriangleDeviation;
        std::vector<uint32_t> CoarseTriangleGroup;
        // 📝 Exact-position weld across groups: border vertices are locked, so their copies are bit-identical.
        struct PositionKey
        {
            size_t operator()(const std::array<uint64_t, 3>& Key) const
            {
                size_t H = Key[0] * 0x9E3779B97F4A7C15ull;
                H ^= Key[1] * 0xC2B2AE3D27D4EB4Full + (H << 6) + (H >> 2);
                H ^= Key[2] * 0x165667B19E3779F9ull + (H << 6) + (H >> 2);
                return H;
            }
        };
        std::unordered_map<std::array<uint64_t, 3>, uint32_t, PositionKey> WeldMap;
        auto WeldVertex = [&](const FacetPoint3D& P, double Deviation) -> uint32_t
        {
            const std::array<uint64_t, 3> Key = { std::bit_cast<uint64_t>(P.X + 0.0), std::bit_cast<uint64_t>(P.Y + 0.0),
                                                  std::bit_cast<uint64_t>(P.Z + 0.0) };
            const auto Found = WeldMap.find(Key);
            if (Found != WeldMap.end())
            {
                VertexDeviation[Found->second] = std::max(VertexDeviation[Found->second], Deviation);
                return Found->second;
            }
            const uint32_t Index = uint32_t(Coarse.Positions.size());
            WeldMap.emplace(Key, Index);
            Coarse.Positions.push_back(P);
            VertexDeviation.push_back(Deviation);
            return Index;
        };

        for (uint32_t Group = 0; Group < uint32_t(Groups.size()); ++Group)
        {
            std::vector<uint8_t> Locked;
            std::vector<uint32_t> GroupTriangles;
            const PolygonComplex Extracted = ExtractGroupGeometry(Finer.Complex, Finer.Clusters, Groups[Group], Locked, GroupTriangles);
            const uint32_t FacetCount = uint32_t(GroupTriangles.size());
            if (FacetCount == 0) continue;

            SimplificationSpecification Simplification;
            Simplification.UvPenalty = Specification.UvPenalty;
            Simplification.TargetTriangles = FacetCount > 1
                ? std::max(1u, uint32_t(std::floor(double(FacetCount) * std::clamp(Specification.CoarseningFraction, 0.05, 0.95))))
                : FacetCount;
            SimplificationOutcome Simplified;
            if (FacetCount > 1)
                Simplified = SimplifyPolygonComplex(Extracted, Simplification, Locked);
            else
            {
                Simplified.Complex = Extracted;
                Simplified.VertexDeviation.assign(Extracted.Positions.size(), 0.0);
                Simplified.TriangleDeviation.assign(1, 0.0);
            }

            for (uint32_t Triangle = 0; Triangle < uint32_t(Simplified.Complex.Triangles.size() / 3); ++Triangle)
            {
                for (uint32_t Corner = 0; Corner < 3; ++Corner)
                {
                    const uint32_t Local = Simplified.Complex.Triangles[Triangle * 3 + Corner];
                    Coarse.Triangles.push_back(WeldVertex(Simplified.Complex.Positions[Local], Simplified.VertexDeviation[Local]));
                    Coarse.CornerUVs.push_back(Simplified.Complex.CarriesUVs ? Simplified.Complex.CornerUVs[Triangle * 3 + Corner]
                                                                        : std::array<double, 2>{ 0.0, 0.0 });
                }
                Coarse.Classification.push_back(Simplified.Complex.Classification[Triangle]);
                Coarse.SharpMask.push_back(Simplified.Complex.SharpMask[Triangle]);
                CoarseTriangleGroup.push_back(Group);
                TriangleDeviation.push_back(Simplified.TriangleDeviation.empty() ? 0.0 : Simplified.TriangleDeviation[Triangle]);
            }
        }

        // 📝 A tier that cannot remove a facet ends the sequence: the remaining geometry is fully pinned.
        if (Coarse.Triangles.size() / 3 >= FinerTriangleCount) break;
        // 📝 Rebuild edge classification for the coarser level, then partition it.
        ClassifyEdges(Coarse, Specification.UseCreaseAngle, Specification.CreaseCosine);
        ClusterTier Tier;
        Tier.DepthTier = DepthTier + 1;
        Tier.Complex = std::move(Coarse);
        Tier.VertexDeviation = std::move(VertexDeviation);
        Tier.Clusters = PartitionFacetClusters(Tier.Complex, Specification.TriangleLimit);
        Tier.ClusterDeviation.assign(Tier.Clusters.size(), 0.0);
        Tier.EnclosedClusters.assign(Tier.Clusters.size(), {});
        for (uint32_t Cluster = 0; Cluster < uint32_t(Tier.Clusters.size()); ++Cluster)
        {
            std::unordered_set<uint32_t> Groups;
            double Deviation = 0.0;
            for (const uint32_t Triangle : Tier.Clusters[Cluster].Triangles)
            {
                Groups.insert(CoarseTriangleGroup[Triangle]);
                Deviation = std::max(Deviation, TriangleDeviation[Triangle]);
            }
            // 📝 Enclosure: every finer cluster of a touched group is enclosed by this cluster.
            std::unordered_set<uint32_t> Enclosed;
            for (uint32_t FineCluster = 0; FineCluster < uint32_t(Finer.Clusters.size()); ++FineCluster)
                if (Groups.count(FineClusterGroup[FineCluster])) Enclosed.insert(FineCluster);
            Tier.EnclosedClusters[Cluster].assign(Enclosed.begin(), Enclosed.end());
            std::sort(Tier.EnclosedClusters[Cluster].begin(), Tier.EnclosedClusters[Cluster].end());
            // 📝 Monotonic deviation: a coarser cluster can never claim less error than what it swallows.
            for (const uint32_t FineCluster : Tier.EnclosedClusters[Cluster])
                Deviation = std::max(Deviation, Finer.ClusterDeviation[FineCluster]);
            Tier.ClusterDeviation[Cluster] = Deviation;
        }
        // 📝 Bounds enclosure: a coarser sphere must contain every sphere it encloses, because the finer sphere
        //    is fitted to facets the collapse has since removed.
        for (uint32_t Cluster = 0; Cluster < uint32_t(Tier.Clusters.size()); ++Cluster)
            for (const uint32_t Fine : Tier.EnclosedClusters[Cluster])
            {
                const FacetCluster& FinerCluster = Finer.Clusters[Fine];
                const double Reach = Distance(Tier.Clusters[Cluster].SphereCentre, FinerCluster.SphereCentre)
                                   + FinerCluster.SphereRadius;
                Tier.Clusters[Cluster].SphereRadius = std::max(Tier.Clusters[Cluster].SphereRadius, Reach);
            }
        Tier.MaximumDeviation = *std::max_element(Tier.ClusterDeviation.begin(), Tier.ClusterDeviation.end());

        Sequence.Tiers.push_back(std::move(Tier));
        TriangleGroup = CoarseTriangleGroup;
        ++DepthTier;
    }
    return Sequence;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  PUBLIC — VALIDATION
//------------------------------------------------------------------------------------------------------------------------

std::vector<std::string> ValidateTierSequence(const ClusterTierSequence& Sequence, const TierBuildSpecification& Specification)
{
    std::vector<std::string> Problems;
    if (Sequence.Tiers.empty()) { Problems.push_back("sequence is empty"); return Problems; }
    if (Sequence.Tiers.front().Clusters.empty()) { Problems.push_back("leaf tier has no clusters"); return Problems; }

    uint32_t PriorTriangleCount = std::numeric_limits<uint32_t>::max();
    for (size_t Tier = 0; Tier < Sequence.Tiers.size(); ++Tier)
    {
        const ClusterTier& Current = Sequence.Tiers[Tier];
        const std::string Prefix = "tier " + std::to_string(Tier) + ": ";
        const auto Local = ValidateClusterBuild(Current.Complex, Current.Clusters, Specification.TriangleLimit);
        for (const auto& Problem : Local) Problems.push_back(Prefix + Problem);
        if (Current.Clusters.size() != Current.ClusterDeviation.size() ||
            Current.Clusters.size() != Current.EnclosedClusters.size())
            Problems.push_back(Prefix + "cluster and deviation arrays are misaligned");
        const uint32_t TriangleCount = uint32_t(Current.Complex.Triangles.size() / 3);
        if (TriangleCount >= PriorTriangleCount && Tier > 0)
            Problems.push_back(Prefix + "triangle count did not decrease from the finer tier");
        PriorTriangleCount = TriangleCount;
        if (Current.VertexDeviation.size() != Current.Complex.Positions.size())
            Problems.push_back(Prefix + "vertex deviation array is misaligned");
    }
    // 📝 The root is the fixed point of coarsening, not necessarily a single cluster: an open mesh with a pinned
    //    border settles at a few clusters, each still inside the cap checked above.

    // 📝 Enclosure: spheres nest, deviation never shrinks, and every finer cluster has a coarser home.
    for (size_t Tier = 0; Tier + 1 < Sequence.Tiers.size(); ++Tier)
    {
        const ClusterTier& Finer = Sequence.Tiers[Tier];
        const ClusterTier& Coarser = Sequence.Tiers[Tier + 1];
        std::vector<uint32_t> EnclosureCount(Finer.Clusters.size(), 0);
        for (uint32_t Cluster = 0; Cluster < uint32_t(Coarser.Clusters.size()); ++Cluster)
        {
            for (const uint32_t Fine : Coarser.EnclosedClusters[Cluster])
            {
                if (Fine >= Finer.Clusters.size())
                {
                    Problems.push_back("tier " + std::to_string(Tier + 1) + ": enclosure index out of range");
                    continue;
                }
                ++EnclosureCount[Fine];
                if (Coarser.ClusterDeviation[Cluster] + 1.0e-9 < Finer.ClusterDeviation[Fine])
                    Problems.push_back("tier " + std::to_string(Tier + 1) + ": coarser deviation is below its enclosed cluster");
                const double Reach = Distance(Coarser.Clusters[Cluster].SphereCentre, Finer.Clusters[Fine].SphereCentre);
                if (Reach + Finer.Clusters[Fine].SphereRadius > Coarser.Clusters[Cluster].SphereRadius + 1.0e-6)
                    Problems.push_back("tier " + std::to_string(Tier + 1) + ": coarser bounds sphere does not enclose its finer cluster");
            }
        }
        for (size_t Fine = 0; Fine < Finer.Clusters.size(); ++Fine)
            if (EnclosureCount[Fine] == 0)
                Problems.push_back("tier " + std::to_string(Tier) + ": cluster " + std::to_string(Fine) + " has no coarser home");
    }

    // 📝 Deviation is monotone across the whole sequence.
    for (size_t Tier = 1; Tier < Sequence.Tiers.size(); ++Tier)
        if (Sequence.Tiers[Tier].MaximumDeviation + 1.0e-9 < Sequence.Tiers[Tier - 1].MaximumDeviation)
            Problems.push_back("tier " + std::to_string(Tier) + ": tier deviation fell below the finer tier");
    return Problems;
}

}   // namespace Frontier
