//============================================================================================================================================
//                                                        SURFACEOFFSETSOLVER.CPP
//============================================================================================================================================
// 📦 Surface projection, parallel-transport marching and adaptive cubic offset interpolation.

#include "SurfaceOffsetSolver.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <functional>

namespace Frontier
{
namespace
{
struct SurfaceProjection
{
    const BrepBody& Body;
    std::vector<Box3> Bounds;
    std::vector<std::vector<int>> Neighbours;
    std::vector<std::vector<int>> FaceVertices;
    std::vector<std::array<Vec3, 81>> Seeds;

    explicit SurfaceProjection(const BrepBody& Skin) : Body(Skin)
    {
        Neighbours.resize(Body.Faces.size());
        FaceVertices.resize(Body.Faces.size());
        for (const auto& Edge : Body.Edges) for (int C : Edge.Coedges)
        {
            FaceVertices[Body.Coedges[C].Face].push_back(Edge.VertexStart);
            FaceVertices[Body.Coedges[C].Face].push_back(Edge.VertexEnd);
        }
        for (const auto& Edge : Body.Edges) for (int A : Edge.Coedges) for (int B : Edge.Coedges)
            Neighbours[Body.Coedges[A].Face].push_back(Body.Coedges[B].Face);
        for (const auto& Face : Body.Faces)
        {
            Bounds.push_back(Face.Surface.Bounds());
            std::array<Vec3, 81> Positions;
            for (int U = 0; U <= 8; ++U) for (int V = 0; V <= 8; ++V)
                Positions[U * 9 + V] = Face.Surface.Sample(
                    Face.Surface.DomainStartU() + (Face.Surface.DomainEndU() - Face.Surface.DomainStartU()) * U / 8,
                    Face.Surface.DomainStartV() + (Face.Surface.DomainEndV() - Face.Surface.DomainStartV()) * V / 8);
            Seeds.push_back(Positions);
        }
    }

    double Refine(const NurbsSurface& Surface, Vec3 Target, double& U, double& V) const
    {
        double Distance = Surface.Sample(U, V).Distance(Target);
        for (int Iteration = 0; Iteration < 60 && Distance > 1e-10; ++Iteration)
        {
            Vec3 Position, Along, Across; Surface.Derivatives(U, V, Position, Along, Across);
            const Vec3 Difference = Target - Position;
            const double A = Along.Dot(Along), B = Along.Dot(Across), C = Across.Dot(Across), Determinant = A * C - B * B;
            if (std::fabs(Determinant) < 1e-28) break;
            const double StepAlong = (Difference.Dot(Along) * C - Difference.Dot(Across) * B) / Determinant;
            const double StepAcross = (Difference.Dot(Across) * A - Difference.Dot(Along) * B) / Determinant;
            bool Improved = false;
            for (double Scale = 1; Scale >= 1.0 / 1024; Scale *= 0.5)
            {
                const double NextU = ScalarCriteria::Clamp(U + Scale * StepAlong, Surface.DomainStartU(), Surface.DomainEndU());
                const double NextV = ScalarCriteria::Clamp(V + Scale * StepAcross, Surface.DomainStartV(), Surface.DomainEndV());
                const double NextDistance = Surface.Sample(NextU, NextV).Distance(Target);
                if (NextDistance < Distance)
                {
                    Improved = true; U = NextU; V = NextV; Distance = NextDistance; break;
                }
            }
            if (!Improved) break;
        }
        return Distance;
    }

    Vec3 Project(Vec3 Point, int& FaceIndex, double& Along, double& Across, double& Distance) const
    {
        const int Hint = FaceIndex;
        Distance = ScalarCriteria::Infinity; FaceIndex = -1;
        Vec3 Result;
        if (Hint >= 0 && Hint < static_cast<int>(Body.Faces.size()))
        {
            Distance = Refine(Body.Faces[Hint].Surface, Point, Along, Across);
            FaceIndex = Hint; Result = Body.Faces[Hint].Surface.Sample(Along, Across);
        }
        // 📝 Use the warm face's upper bound before sorting; the accepted candidate order is unchanged.
        std::vector<std::pair<double, int>> Candidates;
        for (size_t Index = 0; Index < Bounds.size(); ++Index)
        {
            const auto& BoundsOfFace = Bounds[Index];
            const Vec3 Closest = Vec3::Max(BoundsOfFace.Low, Vec3::Min(BoundsOfFace.High, Point));
            const double LowerDistance = Closest.Distance(Point);
            if (LowerDistance <= Distance + 1e-9) Candidates.emplace_back(LowerDistance, static_cast<int>(Index));
        }
        std::sort(Candidates.begin(), Candidates.end());
        for (const auto& [LowerDistance, Index] : Candidates)
        {
            if (LowerDistance > Distance + 1e-9) break;
            std::array<std::pair<double, int>, 81> Ranked;
            for (int Seed = 0; Seed < 81; ++Seed) Ranked[Seed] = { Seeds[Index][Seed].Distance(Point), Seed };
            std::partial_sort(Ranked.begin(), Ranked.begin() + 3, Ranked.end());
            for (int Slot = 0; Slot < 3; ++Slot)
            {
                const auto& Surface = Body.Faces[Index].Surface;
                double U = Surface.DomainStartU() + (Surface.DomainEndU() - Surface.DomainStartU()) * (Ranked[Slot].second / 9) / 8;
                double V = Surface.DomainStartV() + (Surface.DomainEndV() - Surface.DomainStartV()) * (Ranked[Slot].second % 9) / 8;
                const double Error = Refine(Body.Faces[Index].Surface, Point, U, V);
                if (Error < Distance)
                {
                    Distance = Error; FaceIndex = Index; Along = U; Across = V;
                    Result = Body.Faces[Index].Surface.Sample(U, V);
                }
            }
        }
        return Result;
    }
};
Vec3 Transport(Vec3 Tangent, Vec3 Before, Vec3 After)
{
    const Vec3 Cross = Before.Cross(After);
    const double Cosine = Before.Dot(After), Sine = Cross.Length();
    if (Sine < 1e-12) return (Tangent - After * Tangent.Dot(After)).Normalised();
    const Vec3 Axis = Cross / Sine;
    return (Tangent * Cosine + Axis.Cross(Tangent) * Sine + Axis * (Axis.Dot(Tangent) * (1 - Cosine))).Normalised();
}

Deliver<Vec3> March(
    const SurfaceProjection& Projection,
    Vec3                     Start,
    Vec3                     Tangent,
    double                   Width,
    int                      Steps,
    double&                  Length)
{
    using Out = Deliver<Vec3>;
    Length = 0;
    int Face = -1; double U = 0, V = 0, Error = 0;
    Vec3 Point = Projection.Project(Start, Face, U, V, Error);
    if (Face < 0) return Out::Reject(RefusalReason::DegenerateInput, "source has no support");
    auto Normal = [&](int Index, double Along, double Across)
    {
        return Projection.Body.Faces[Index].Surface.Normal(Along, Across) * (Projection.Body.Faces[Index].Reversed ? -1.0 : 1.0);
    };
    Vec3 N = Normal(Face, U, V);
    Vec3 Direction = N.Cross(Tangent).Normalised() * (Width < 0 ? -1.0 : 1.0);
    const double Step = std::fabs(Width) / Steps;
    for (int Index = 0; Index < Steps; ++Index)
    {
        const int PreviousFace = Face;
        Vec3 Next;
        double Advance = Step;
        for (int Pass = 0; Pass < 3; ++Pass)
        {
            Next = Projection.Project(Point + Direction * Advance, Face, U, V, Error);
            const double Travel = Next.Distance(Point);
            if (Face < 0 || !std::isfinite(Travel) || Travel < Step * 0.45 || Error > Step * 0.75)
                return Out::Reject(RefusalReason::DegenerateInput, "offset reaches a support boundary, fold or gap");
            Advance *= Step / Travel;
        }
        const auto& Adjacent = Projection.Neighbours[PreviousFace];
        if (Face != PreviousFace && std::find(Adjacent.begin(), Adjacent.end(), Face) == Adjacent.end())
        {
            bool ThroughVertex = false;
            for (int Vertex : Projection.FaceVertices[PreviousFace])
            {
                const auto& Other = Projection.FaceVertices[Face];
                if (Vertex < 0 || std::find(Other.begin(), Other.end(), Vertex) == Other.end()) continue;
                const Vec3 Corner = Projection.Body.Vertices[Vertex].Point;
                const Vec3 Chord = Next - Point;
                const double T = ScalarCriteria::Clamp((Corner - Point).Dot(Chord) / Chord.LengthSquared(), 0.0, 1.0);
                ThroughVertex |= Corner.Distance(Point + Chord * T) <= Step * .05 + ScalarCriteria::MergeTolerance;
            }
            if (!ThroughVertex) return Out::Reject(RefusalReason::DegenerateInput, "offset cannot jump between nonadjacent support faces");
        }
        int MidFace = Face; double MidU = U, MidV = V, MidError = 0;
        const Vec3 Mid = Projection.Project((Point + Next) * .5, MidFace, MidU, MidV, MidError);
        Length += Point.Distance(Mid) + Mid.Distance(Next);
        const Vec3 NextNormal = Normal(Face, U, V);
        if (N.Dot(NextNormal) < 0.3)
            return Out::Reject(RefusalReason::DegenerateInput, "offset crosses an unsupported sharp normal reversal");
        Direction = Transport(Direction, N, NextNormal);
        Point = Next;
        N = NextNormal;
    }
    return Out::Accept(Point);
}


struct SurfaceDistance
{
    double Length = 0;
    Vec3 Direction;
};

// 📝 Bounded local distance estimate: projected chord, on-skin path relaxation and per-query refinement.
// 📝 This is not a certified globally shortest path, especially at crease networks or cut loci.
SurfaceDistance LocalDistance(
    const SurfaceProjection& Projection,
    Vec3                     A,
    Vec3                     B,
    int                      Count)
{
    std::vector<Vec3> Points;
    int Face = -1; double U = 0, V = 0, Error = 0;
    for (int I = 0; I <= Count; ++I)
        Points.push_back(Projection.Project(A + (B - A) * (double(I) / Count), Face, U, V, Error));
    for (int Round = 0; Round < 6; ++Round)
    {
        auto Next = Points;
        for (int I = 1; I < Count; ++I)
            Next[I] = Projection.Project((Points[I - 1] + Points[I + 1]) * .5, Face, U, V, Error);
        Points = std::move(Next);
    }
    SurfaceDistance Result;
    for (int I = 1; I <= Count; ++I) Result.Length += Points[I].Distance(Points[I - 1]);
    Result.Direction = (Points.back() - Points[Points.size() - 2]).Normalised();
    return Result;
}

Deliver<Vec3> CornerDistancePoint(
    const SurfaceProjection& Projection,
    const NurbsCurve&        Source,
    Vec3                     Candidate,
    double                   First,
    double                   Last,
    double                   Width,
    double                   Tolerance,
    double&                  Refinement,
    double&                  Residual)
{
    using Out = Deliver<Vec3>;
    int Face = -1; double U = 0, V = 0, Error = 0;
    Candidate = Projection.Project(Candidate, Face, U, V, Error);
    auto Nearest = [&](int Steps)
    {
        double L = First, R = Last;
        const double Ratio = (std::sqrt(5.0) - 1) * .5;
        double A = R - Ratio * (R - L), B = L + Ratio * (R - L);
        auto Left = LocalDistance(Projection, Source.Sample(A), Candidate, Steps);
        auto Right = LocalDistance(Projection, Source.Sample(B), Candidate, Steps);
        for (int I = 0; I < 24; ++I)
        {
            if (Left.Length < Right.Length)
            {
                R = B; B = A; Right = Left; A = R - Ratio * (R - L);
                Left = LocalDistance(Projection, Source.Sample(A), Candidate, Steps);
            }
            else
            {
                L = A; A = B; Left = Right; B = L + Ratio * (R - L);
                Right = LocalDistance(Projection, Source.Sample(B), Candidate, Steps);
            }
        }
        auto Best = Left.Length < Right.Length ? Left : Right;
        for (double Knot : Source.Knots)
        {
            if (Knot <= L || Knot >= R) continue;
            auto AtCorner = LocalDistance(Projection, Source.Sample(Knot), Candidate, Steps);
            if (AtCorner.Length < Best.Length) Best = AtCorner;
        }
        return Best;
    };
    for (int Round = 0; Round < 12; ++Round)
    {
        auto Distance = Nearest(16);
        if (!std::isfinite(Distance.Length) || Distance.Direction.LengthSquared() < .9)
            return Out::Reject(RefusalReason::DegenerateInput, "corner surface-distance solve is singular");
        if (std::fabs(Distance.Length - Width) < Tolerance * .1)
        {
            const auto Fine = Nearest(32);
            if (std::fabs(Fine.Length - Distance.Length) > Tolerance * .2)
                return Out::Reject(RefusalReason::DegenerateInput, "corner surface-distance refinement did not converge");
            if (std::fabs(Fine.Length - Width) < Tolerance * .2)
            {
                Refinement = std::max(Refinement, std::fabs(Fine.Length - Distance.Length));
                Residual = std::max(Residual, std::fabs(Fine.Length - Width));
                return Out::Accept(Candidate);
            }
            Distance = Fine;
        }
        Candidate = Projection.Project(Candidate + Distance.Direction * (Width - Distance.Length), Face, U, V, Error);
        if (Error > Tolerance)
            return Out::Reject(RefusalReason::DegenerateInput, "corner surface-distance correction leaves support");
    }
    return Out::Reject(RefusalReason::DegenerateInput, "corner surface-distance correction did not converge");
}

}

Deliver<NurbsCurve> SurfaceOffsetSolver::Construct(
    const NurbsCurve&           Source,
    const BrepBody&             Support,
    const SurfaceOffsetOptions& Options,
    SurfaceOffsetReport*        Report) noexcept
{
    using Out = Deliver<NurbsCurve>;
    if (Support.Faces.empty() || Source.Validate() || !std::isfinite(Options.Distance) || !std::isfinite(Options.Tolerance) ||
        std::fabs(Options.Distance) < 1e-6 || std::fabs(Options.Distance) > 1 || Options.Tolerance < 1e-7 ||
        Options.Tolerance > 0.001 || Options.Samples < 16 || Options.Samples > 2048)
        return Out::Reject(RefusalReason::DegenerateInput, "invalid surface offset inputs or numerical limits");
    auto FinitePole = [](Vec4 P) { return std::isfinite(P.X) && std::isfinite(P.Y) && std::isfinite(P.Z) && std::isfinite(P.W) && P.W > 0; };
    for (Vec4 P : Source.Poles) if (!FinitePole(P)) return Out::Reject(RefusalReason::DegenerateInput, "source has invalid poles");
    for (double K : Source.Knots) if (!std::isfinite(K)) return Out::Reject(RefusalReason::DegenerateInput, "source has invalid knots");
    for (const auto& Face : Support.Faces)
    {
        if (!Face.Natural || Face.Surface.Validate())
            return Out::Reject(RefusalReason::DegenerateInput, "surface offsets require valid natural, untrimmed support faces");
        for (Vec4 P : Face.Surface.Poles) if (!FinitePole(P)) return Out::Reject(RefusalReason::DegenerateInput, "support has invalid poles");
        for (double K : Face.Surface.KnotsU) if (!std::isfinite(K)) return Out::Reject(RefusalReason::DegenerateInput, "support has invalid knots");
        for (double K : Face.Surface.KnotsV) if (!std::isfinite(K)) return Out::Reject(RefusalReason::DegenerateInput, "support has invalid knots");
    }
    for (const auto& C : Support.Coedges)
        if (C.Face < 0 || C.Face >= static_cast<int>(Support.Faces.size()))
            return Out::Reject(RefusalReason::DegenerateInput, "support has invalid face incidence");
    for (const auto& Edge : Support.Edges)
    {
        if (Edge.VertexStart < 0 || Edge.VertexEnd < 0 || Edge.VertexStart >= static_cast<int>(Support.Vertices.size()) || Edge.VertexEnd >= static_cast<int>(Support.Vertices.size()))
            return Out::Reject(RefusalReason::DegenerateInput, "support has invalid edge vertices");
        for (int C : Edge.Coedges) if (C < 0 || C >= static_cast<int>(Support.Coedges.size()))
            return Out::Reject(RefusalReason::DegenerateInput, "support has invalid edge incidence");
    }
    if (Support.Validate().Hulls != 1)
        return Out::Reject(RefusalReason::DegenerateInput, "surface offset support must be one edge-connected skin");
    SurfaceProjection Projection(Support);
    SurfaceOffsetReport Metrics;
    std::map<double, Vec3> Cache;
    struct CornerInterval
    {
        double First, Last;
        Vec3 A, B;
    };
    std::vector<CornerInterval> Corners;
    const double Begin = Source.DomainStart(), Span = Source.DomainEnd() - Begin;
    auto Station = [&](double Parameter) -> Deliver<Vec3>
    {
        for (const auto& Corner : Corners) if (Parameter > Corner.First && Parameter < Corner.Last)
        {
            const auto Existing = Cache.find(Parameter);
            if (Existing != Cache.end()) return Deliver<Vec3>::Accept(Existing->second);
            const double T = (Parameter - Corner.First) / (Corner.Last - Corner.First);
            const Vec3 A = Corner.A - Source.Sample(Begin + Span * Corner.First);
            const Vec3 B = Corner.B - Source.Sample(Begin + Span * Corner.Last);
            const Vec3 Guess = Source.Sample(Begin + Span * Parameter) + (A * (1 - T) + B * T).Normalised() * std::fabs(Options.Distance);
            auto Result = CornerDistancePoint(Projection, Source, Guess,
                Begin + Span * Corner.First, Begin + Span * Corner.Last, std::fabs(Options.Distance), Options.Tolerance, Metrics.CornerRefinement, Metrics.CornerResidual);
            if (Result) Cache[Parameter] = Result.Payload;
            return Result;
        }
        const auto Found = Cache.find(Parameter);
        if (Found != Cache.end()) return Deliver<Vec3>::Accept(Found->second);
        const Vec3 P = Source.Sample(Begin + Span * Parameter), T = Source.Tangent(Begin + Span * Parameter);
        int Face = -1; double U = 0, V = 0, Error = 0;
        const Vec3 OnSkin = Projection.Project(P, Face, U, V, Error);
        Metrics.SourceDistance = std::max(Metrics.SourceDistance, Error);
        if (Face < 0 || Error > Options.Tolerance || T.LengthSquared() < 0.9)
            return Deliver<Vec3>::Reject(RefusalReason::DegenerateInput, "source must lie on the support within tolerance with a regular tangent");
        double CoarseLength = 0;
        auto Coarse = March(Projection, OnSkin, T, Options.Distance, 16, CoarseLength);
        for (int Steps = 32; Steps <= 512; Steps *= 2)
        {
            double FineLength = 0;
            auto Fine = March(Projection, OnSkin, T, Options.Distance, Steps, FineLength);
            const double Difference = Fine && Coarse ? Fine.Payload.Distance(Coarse.Payload) : ScalarCriteria::Infinity;
            const double WidthError = std::fabs(FineLength - std::fabs(Options.Distance));
            if (Difference <= Options.Tolerance * 0.2 && WidthError <= Options.Tolerance * .2)
            {
                Metrics.MarchRefinement = std::max(Metrics.MarchRefinement, Difference);
                Metrics.PathLengthError = std::max(Metrics.PathLengthError, WidthError);
                Cache[Parameter] = Fine.Payload;
                return Fine;
            }
            Coarse = std::move(Fine);
        }
        return Deliver<Vec3>::Reject(RefusalReason::DegenerateInput, "surface offset march did not converge; no result accepted");
    };
    // 📝 Offset branches at inward C0 corners meet after clipping, not by interpolating across a discontinuity.
    struct Section
    {
        double First = 0, Last = 1;
        Vec3 FirstPoint, LastPoint;
        bool FirstFixed = false, LastFixed = false;
    };
    std::vector<double> Breaks{0};
    for (double Knot : Source.Knots)
    {
        const double T = (Knot - Begin) / Span;
        if (T > 0 && T < 1 && T != Breaks.back()) Breaks.push_back(T);
    }
    Breaks.push_back(1);
    std::vector<Section> Sections;
    for (size_t I = 1; I < Breaks.size(); ++I)
    {
        Section S; S.First = Breaks[I - 1]; S.Last = Breaks[I]; Sections.push_back(S);
    }
    for (size_t I = 1; I + 1 < Breaks.size(); ++I)
    {
        const double T = Breaks[I];
        const double Probe = std::min({1e-8, (T - Breaks[I - 1]) * .001, (Breaks[I + 1] - T) * .001});
        const double Left = T - Probe, Right = T + Probe;
        auto A = Station(Left), B = Station(Right);
        if (!A || !B) return Out::Reject(RefusalReason::DegenerateInput, "offset corner has no valid surface branch");
        if (A.Payload.Distance(B.Payload) <= Options.Tolerance * .1) continue;
        double L = Left, R = Right;
        bool Met = false;
        for (int Iteration = 0; Iteration < 20; ++Iteration)
        {
            A = Station(L); B = Station(R);
            if (!A || !B) break;
            const Vec3 Delta = B.Payload - A.Payload;
            if (Delta.Length() <= Options.Tolerance * .4) { Met = true; break; }
            const double H = std::min({1e-6, (L - Sections[I - 1].First) * .1, (Sections[I].Last - R) * .1});
            if (H <= 1e-12) break;
            auto Before = Station(L - H), After = Station(R + H);
            if (!Before || !After) break;
            const Vec3 DL = (A.Payload - Before.Payload) / H, DR = (After.Payload - B.Payload) / H;
            const double AA = DL.Dot(DL), BB = DL.Dot(DR), CC = DR.Dot(DR), Det = AA * CC - BB * BB;
            if (Det < 1e-20) break;
            const double StepL = (Delta.Dot(DL) * CC - Delta.Dot(DR) * BB) / Det;
            const double StepR = (Delta.Dot(DL) * BB - Delta.Dot(DR) * AA) / Det;
            L = ScalarCriteria::Clamp(L + StepL, Sections[I - 1].First, Left);
            R = ScalarCriteria::Clamp(R + StepR, Right, Sections[I].Last);
        }
        int FaceL = -1, FaceR = -1; double UL = 0, VL = 0, UR = 0, VR = 0, ProjectionError = 0;
        Projection.Project(Source.Sample(Begin + Span * Left), FaceL, UL, VL, ProjectionError);
        Projection.Project(Source.Sample(Begin + Span * Right), FaceR, UR, VR, ProjectionError);
        const Vec3 NL = Support.Faces[FaceL].Surface.Normal(UL, VL) * (Support.Faces[FaceL].Reversed ? -1.0 : 1.0);
        const Vec3 NR = Support.Faces[FaceR].Surface.Normal(UR, VR) * (Support.Faces[FaceR].Reversed ? -1.0 : 1.0);
        if (!Met || NL.Distance(NR) > 1e-4)
        {
            const double Half = std::min((T - Breaks[I - 1]) * .3, (Breaks[I + 1] - T) * .3);
            L = T - Half; R = T + Half;
            A = Station(L); B = Station(R);
            if (!A || !B) return Out::Reject(RefusalReason::DegenerateInput, "corner offset has no bounding branches");
            if (!Corners.empty() && Corners.back().Last >= L)
                return Out::Reject(RefusalReason::DegenerateInput, "offset corner neighbourhoods overlap");
            Corners.push_back({L, R, A.Payload, B.Payload});
            auto It = Cache.upper_bound(L);
            while (It != Cache.end() && It->first < R) It = Cache.erase(It);
            continue;
        }
        const Vec3 Join = (A.Payload + B.Payload) * .5;
        Sections[I - 1].Last = L; Sections[I - 1].LastPoint = Join; Sections[I - 1].LastFixed = true;
        Sections[I].First = R; Sections[I].FirstPoint = Join; Sections[I].FirstFixed = true;
    }
    std::vector<Vec4> Poles;
    std::vector<double> Knots(4, 0);
    double TotalSpan = 0;
    for (const auto& S : Sections) TotalSpan += S.Last - S.First;
    double Accumulated = 0;
    auto Pole = [&](Vec3 P) { Poles.emplace_back(P.X, P.Y, P.Z, 1); };
    std::function<Refusal(double, double, Vec3, Vec3, double, double, int)> Fit;
    Fit = [&](double First, double Last, Vec3 P0, Vec3 P3, double K0, double K1, int Depth) -> Refusal
    {
        auto A = Station(First + (Last - First) / 3), B = Station(First + (Last - First) * 2 / 3);
        if (!A) return A.Denial;
        if (!B) return B.Denial;
        const Vec3 U = A.Payload * 27 - P0 * 8 - P3, V = B.Payload * 27 - P0 - P3 * 8;
        const Vec3 P1 = (U * 2 - V) / 18, P2 = (V * 2 - U) / 18;
        double Maximum = 0;
        Vec3 Middle;
        for (double T : {1.0 / 6, .5, 5.0 / 6})
        {
            auto Exact = Station(First + (Last - First) * T);
            if (!Exact) return Exact.Denial;
            if (T == .5) Middle = Exact.Payload;
            const double S = 1 - T;
            const Vec3 Approximate = P0 * (S * S * S) + P1 * (3 * S * S * T) + P2 * (3 * S * T * T) + P3 * (T * T * T);
            Maximum = std::max(Maximum, Approximate.Distance(Exact.Payload));
        }
        if (Maximum > Options.Tolerance * .5)
        {
            if (Depth >= 16 || Metrics.Intervals > 8192)
                return {RefusalReason::DegenerateInput, "surface offset interpolation did not converge; no result accepted"};
            const double Mid = (First + Last) * .5, KM = (K0 + K1) * .5;
            auto Failure = Fit(First, Mid, P0, Middle, K0, KM, Depth + 1);
            if (Failure) return Failure;
            return Fit(Mid, Last, Middle, P3, KM, K1, Depth + 1);
        }
        if ((P3 - P0).Dot(Source.Sample(Begin + Span * Last) - Source.Sample(Begin + Span * First)) <= 0)
            return {RefusalReason::DegenerateInput, "offset develops a cusp or reverses source direction"};
        Metrics.InterpolationError = std::max(Metrics.InterpolationError, Maximum);
        ++Metrics.Intervals;
        if (Poles.empty())
        {
            Pole(P0); Metrics.Sources.push_back(Source.Sample(Begin + Span * First)); Metrics.Stations.push_back(P0);
            Metrics.Parameters.push_back(K0);
        }
        Pole(P1); Pole(P2); Pole(P3);
        Knots.insert(Knots.end(), 3, K1);
        Metrics.Sources.push_back(Source.Sample(Begin + Span * Last)); Metrics.Stations.push_back(P3);
        Metrics.Parameters.push_back(K1);
        return {};
    };
    for (const auto& S : Sections)
    {
        if (S.Last <= S.First) return Out::Reject(RefusalReason::DegenerateInput, "offset corners consume a source span");
        const int Count = std::max(1, int(std::ceil((S.Last - S.First) * Options.Samples)));
        for (int I = 0; I < Count; ++I)
        {
            const double First = S.First + (S.Last - S.First) * I / Count;
            const double Last = S.First + (S.Last - S.First) * (I + 1) / Count;
            const double Probe = std::min(1e-8, (Last - First) * .001);
            bool FirstCorner = false, LastCorner = false;
            for (const auto& Corner : Corners)
            {
                FirstCorner |= First > Corner.First && First < Corner.Last;
                LastCorner |= Last > Corner.First && Last < Corner.Last;
            }
            auto A = Station(I == 0 && First > 0 && !FirstCorner ? First + Probe : First);
            auto B = Station(I + 1 == Count && Last < 1 && !LastCorner ? Last - Probe : Last);
            if (!A) return Out::Reject(A.Denial.Reason, A.Denial.Detail);
            if (!B) return Out::Reject(B.Denial.Reason, B.Denial.Detail);
            const double K0 = (Accumulated + First - S.First) / TotalSpan, K1 = (Accumulated + Last - S.First) / TotalSpan;
            auto Failure = Fit(First, Last, I == 0 && S.FirstFixed ? S.FirstPoint : A.Payload,
                I + 1 == Count && S.LastFixed ? S.LastPoint : B.Payload, K0, K1, 0);
            if (Failure) return Out::Reject(Failure.Reason, Failure.Detail);
        }
        Accumulated += S.Last - S.First;
    }
    Knots.push_back(Knots.back());
    // 📝 Sampled nonlocal proximity guard; not a continuous intersection or global cut-locus certificate.
    for (size_t A = 0; A < Metrics.Stations.size(); ++A) for (size_t B = A + 4; B < Metrics.Stations.size(); ++B)
    {
        if (Source.Closed() && Metrics.Stations.size() - (B - A) < 4) continue;
        if (Metrics.Stations[A].Distance(Metrics.Stations[B]) < Options.Tolerance * 2)
            return Out::Reject(RefusalReason::SelfIntersecting, "offset approaches a nonlocal branch");
    }
    if (Source.Closed() && Metrics.Stations.front().Distance(Metrics.Stations.back()) > Options.Tolerance * .5)
        return Out::Reject(RefusalReason::DegenerateInput, "closed source requires an unsupported seam corner join");
    auto Result = NurbsCurve::Build(3, std::move(Poles), std::move(Knots));
    Metrics.CornerIntervals = static_cast<int>(Corners.size());
    if (Report) *Report = std::move(Metrics);
    return Result;
}
}
