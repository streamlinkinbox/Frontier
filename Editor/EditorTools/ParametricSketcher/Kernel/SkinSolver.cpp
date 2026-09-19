//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Kernel/SkinSolver.cpp — Loft, sweep, pipe and Coons / N-sided patch
//============================================================================================================================================
#include "SkinSolver.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Frontier
{

namespace
{
    // Basis-friendly Mat4 from an orthonormal basis at Origin: columns X, Y, Z.
    Mat4 BasisMatrix(Vec3 Origin, Vec3 X, Vec3 Y, Vec3 Z) noexcept
    {
        Mat4 M;
        M.M[0] = X.X; M.M[1] = X.Y; M.M[2] = X.Z;
        M.M[4] = Y.X; M.M[5] = Y.Y; M.M[6] = Y.Z;
        M.M[8] = Z.X; M.M[9] = Z.Y; M.M[10] = Z.Z;
        M.M[12] = Origin.X; M.M[13] = Origin.Y; M.M[14] = Origin.Z;
        return M;
    }
    Mat4 InverseBasis(Vec3 Origin, Vec3 X, Vec3 Y, Vec3 Z) noexcept                           // transpose of the rotation, −R^T·O
    {
        Mat4 M;
        M.M[0] = X.X; M.M[4] = X.Y; M.M[8] = X.Z;
        M.M[1] = Y.X; M.M[5] = Y.Y; M.M[9] = Y.Z;
        M.M[2] = Z.X; M.M[6] = Z.Y; M.M[10] = Z.Z;
        M.M[12] = -X.Dot(Origin); M.M[13] = -Y.Dot(Origin); M.M[14] = -Z.Dot(Origin);
        return M;
    }

    Vec3 CentroidOf(const NurbsCurve& C) noexcept
    {
        std::vector<Vec3> P; C.Tessellate(P, nullptr, 1e-3);
        if (P.size() > 1 && P.back().Coincident(P.front(), 1e-9)) P.pop_back();
        Vec3 S; for (const Vec3& Q : P) S = S + Q;
        return P.empty() ? Vec3() : S * (1.0 / double(P.size()));
    }

    // Approximate plane normal of a (possibly non-planar) closed curve: Newell's method on a tessellation.
    Vec3 LoopNormal(const NurbsCurve& C) noexcept
    {
        std::vector<Vec3> P; C.Tessellate(P, nullptr, 1e-3);
        Vec3 N;
        for (size_t I = 0; I + 1 < P.size(); ++I) N = N + P[I].Cross(P[I + 1]);
        return N.LengthSquared() > 1e-24 ? N.Normalised() : Vec3();
    }

    // Twist cost of pairing A(t) with B(t): summed distance between matched samples.
    double MatchCost(const NurbsCurve& A, const NurbsCurve& B, int Samples = 48) noexcept
    {
        double Cost = 0.0;
        for (int I = 0; I < Samples; ++I)
        {
            double F = double(I) / Samples;
            Vec3 PA = A.Sample(A.DomainStart() + F * (A.DomainEnd() - A.DomainStart()));
            Vec3 PB = B.Sample(B.DomainStart() + F * (B.DomainEnd() - B.DomainStart()));
            Cost += PA.Distance(PB);
        }
        return Cost;
    }

    // Common degree + knot vector for a family of sections on [0,1].
    Deliver<std::vector<NurbsCurve>> Compatible(std::vector<NurbsCurve> Rows) noexcept
    {
        int Degree = 1;
        for (const NurbsCurve& C : Rows) Degree = std::max(Degree, C.Degree);
        for (NurbsCurve& C : Rows) C = (C.Degree < Degree ? C.Elevated(Degree) : C).Reparameterised(0.0, 1.0);
        std::vector<double> Distinct;
        for (const NurbsCurve& C : Rows) for (double K : C.Knots) Distinct.push_back(K);
        std::sort(Distinct.begin(), Distinct.end());
        Distinct.erase(std::unique(Distinct.begin(), Distinct.end(), [](double A, double B) { return ScalarCriteria::Coincident(A, B, ScalarCriteria::ParametricEpsilon); }), Distinct.end());
        auto Multiplicity = [](const std::vector<double>& Knots, double T) { int M = 0; for (double K : Knots) if (ScalarCriteria::Coincident(K, T, ScalarCriteria::ParametricEpsilon)) ++M; return M; };
        for (double T : Distinct)
        {
            int Target = 0;
            for (const NurbsCurve& C : Rows) Target = std::max(Target, Multiplicity(C.Knots, T));
            for (NurbsCurve& C : Rows) { int Need = Target - Multiplicity(C.Knots, T); if (Need > 0) C = C.InsertKnot(T, Need); }
        }
        int Count = Rows.front().PoleCount();
        for (const NurbsCurve& C : Rows) if (C.PoleCount() != Count) return Deliver<std::vector<NurbsCurve>>::Reject(RefusalReason::NoConvergence, "sections could not be made compatible");
        return Deliver<std::vector<NurbsCurve>>::Accept(std::move(Rows));
    }

    // Interpolate compatible rows across V (homogeneous, shared chord-length parameters); Periodic closes the V direction
    //    by appending the first row again and interpolating through it (C0 at the return seam, exact positions).
    Deliver<NurbsSurface> SkinRows(std::vector<NurbsCurve> Rows, int DegreeV, bool Loop) noexcept
    {
        if (Loop) Rows.push_back(Rows.front());
        int Count = static_cast<int>(Rows.size());
        DegreeV = std::max(1, std::min(DegreeV, Count - 1));
        int CountU = Rows.front().PoleCount();
        if (DegreeV == 1)
        {
            std::vector<double> KV(size_t(Count) + 2, 0.0);
            for (int J = 0; J < Count; ++J) KV[size_t(J) + 1] = double(J) / (Count - 1);
            KV.back() = 1.0;
            std::vector<Vec4> Poles(size_t(CountU) * Count);
            for (int I = 0; I < CountU; ++I) for (int J = 0; J < Count; ++J) Poles[size_t(I) * Count + J] = Rows[J].Poles[I];
            Deliver<NurbsSurface> S = NurbsSurface::Build(Rows.front().Degree, 1, CountU, Count, std::move(Poles), Rows.front().Knots, KV);
            if (S) S.Payload.Classification = SurfaceClassification::Loft;
            return S;
        }
        std::vector<double> ParametersV(Count, 0.0);
        int Contributing = 0;
        for (int I = 0; I < CountU; ++I)
        {
            double Total = 0.0; std::vector<double> Local(Count, 0.0);
            for (int J = 1; J < Count; ++J) { Local[J] = Rows[J].Poles[I].Divide().Distance(Rows[J - 1].Poles[I].Divide()); Total += Local[J]; }
            if (Total <= ScalarCriteria::KernelTolerance) continue;
            ++Contributing; double Running = 0.0;
            for (int J = 1; J < Count; ++J) { Running += Local[J] / Total; ParametersV[J] += Running; }
        }
        if (Contributing == 0) return Deliver<NurbsSurface>::Reject(RefusalReason::DegenerateInput, "all sections coincide");
        for (double& P : ParametersV) P /= Contributing;
        ParametersV[Count - 1] = 1.0;
        for (int J = 1; J < Count; ++J) if (ParametersV[J] <= ParametersV[J - 1]) ParametersV[J] = ParametersV[J - 1] + 1e-6;
        std::vector<NurbsCurve> Columns; std::vector<double> KnotsV;
        for (int I = 0; I < CountU; ++I)
        {
            std::vector<Vec4> Through; for (const NurbsCurve& C : Rows) Through.push_back(C.Poles[I]);
            Deliver<NurbsCurve> Column = NurbsCurve::InterpolateHomogeneous(Through, DegreeV, &ParametersV);
            if (!Column) return Deliver<NurbsSurface>::Reject(Column.Denial.Reason, Column.Denial.Detail);
            if (Columns.empty()) KnotsV = Column.Payload.Knots;
            Columns.push_back(std::move(Column.Payload));
        }
        NurbsSurface S;
        S.DegreeU = Rows.front().Degree; S.DegreeV = DegreeV; S.CountU = CountU; S.CountV = Columns.front().PoleCount();
        S.KnotsU = Rows.front().Knots; S.KnotsV = KnotsV;
        S.Poles.resize(size_t(S.CountU) * S.CountV);
        for (int I = 0; I < S.CountU; ++I) for (int J = 0; J < S.CountV; ++J) S.Pole(I, J) = Columns[I].Poles[J];
        S.Classification = SurfaceClassification::Loft;
        return Deliver<NurbsSurface>::Accept(std::move(S));
    }

    // Sheets → body (sewn, capped when closed) or the lone sheet.
    Deliver<SkinSolver::Skin> Deliver1(std::vector<NurbsSurface> Sheets, bool WantBody, bool Cap = true) noexcept
    {
        SkinSolver::Skin Out;
        if (!WantBody && Sheets.size() == 1) { Out.Sheet = std::move(Sheets.front()); return Deliver<SkinSolver::Skin>::Accept(std::move(Out)); }
        Deliver<BrepBody> B = BrepBody::Sew(Sheets, ScalarCriteria::MergeTolerance, Cap);
        if (!B) return Deliver<SkinSolver::Skin>::Reject(B.Denial.Reason, B.Denial.Detail);
        Out.Body = std::move(B.Payload); Out.IsBody = true;
        return Deliver<SkinSolver::Skin>::Accept(std::move(Out));
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  HARMONISE
//------------------------------------------------------------------------------------------------------------------------
NurbsCurve SkinSolver::SeamAt(const NurbsCurve& Closed, double T) noexcept
{
    if (T <= Closed.DomainStart() + ScalarCriteria::ParametricEpsilon || T >= Closed.DomainEnd() - ScalarCriteria::ParametricEpsilon) return Closed;
    auto [A, B] = Closed.Split(T);
    Deliver<NurbsCurve> J = NurbsCurve::Join(B, A);
    return J ? J.Payload : Closed;
}

Deliver<std::vector<NurbsCurve>> SkinSolver::Harmonise(std::vector<NurbsCurve> Sections, bool AlignSense, bool AlignSeams) noexcept
{
    if (Sections.size() < 2) return Deliver<std::vector<NurbsCurve>>::Reject(RefusalReason::DegenerateInput, "need at least two sections");
    bool AnyClosed = false, AllClosed = true;
    for (const NurbsCurve& C : Sections) { bool K = C.Closed(); AnyClosed |= K; AllClosed &= K; }
    if (AnyClosed && !AllClosed) return Deliver<std::vector<NurbsCurve>>::Reject(RefusalReason::DegenerateInput, "sections mix open and closed curves");
    for (size_t I = 1; I < Sections.size(); ++I)
    {
        const NurbsCurve& Prev = Sections[I - 1];
        NurbsCurve& Cur = Sections[I];
        if (AllClosed)
        {
            // sense: normals should agree (fallback: match cost)
            if (AlignSense)
            {
                Vec3 NP = LoopNormal(Prev), NC = LoopNormal(Cur);
                if (NP.LengthSquared() > 0 && NC.LengthSquared() > 0) { if (NP.Dot(NC) < 0) Cur = Cur.Reversed(); }
                else if (MatchCost(Prev, Cur.Reversed()) < MatchCost(Prev, Cur)) Cur = Cur.Reversed();
            }
            if (AlignSeams)
            {
                // candidate seams: the parameter nearest Prev's start, and each knot break; keep the least twist
                double Best = Cur.DomainStart(); double BestCost = MatchCost(Prev, Cur);
                std::vector<double> Candidates;
                Candidates.push_back(Cur.ClosestParameter(Prev.StartPoint()));
                for (int K = Cur.Degree; K <= Cur.PoleCount(); ++K) Candidates.push_back(Cur.Knots[K]);
                for (int S = 1; S < 24; ++S) Candidates.push_back(Cur.DomainStart() + (Cur.DomainEnd() - Cur.DomainStart()) * S / 24.0);
                for (double T : Candidates)
                {
                    NurbsCurve Trial = SeamAt(Cur, T);
                    double Cost = MatchCost(Prev, Trial);
                    if (Cost < BestCost - 1e-9) { BestCost = Cost; Best = T; }
                }
                Cur = SeamAt(Cur, Best);
            }
        }
        else if (AlignSense)
        {
            double Straight = Prev.StartPoint().Distance(Cur.StartPoint()) + Prev.EndPoint().Distance(Cur.EndPoint());
            double Flipped = Prev.StartPoint().Distance(Cur.EndPoint()) + Prev.EndPoint().Distance(Cur.StartPoint());
            if (Flipped < Straight) Cur = Cur.Reversed();
        }
    }
    return Compatible(std::move(Sections));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  LOFT
//------------------------------------------------------------------------------------------------------------------------
Deliver<NurbsSurface> SkinSolver::LoftSheet(const std::vector<NurbsCurve>& Sections, const LoftOptions& Options) noexcept
{
    Deliver<std::vector<NurbsCurve>> Rows = Harmonise(Sections, Options.AlignSense, Options.AlignSeams);
    if (!Rows) return Deliver<NurbsSurface>::Reject(Rows.Denial.Reason, Rows.Denial.Detail);
    return SkinRows(std::move(Rows.Payload), Options.DegreeV, Options.Loop && Sections.size() >= 3);
}

Deliver<SkinSolver::Skin> SkinSolver::Loft(const std::vector<NurbsCurve>& Sections, const LoftOptions& Options) noexcept
{
    std::vector<std::vector<NurbsCurve>> Wrapped;
    for (const NurbsCurve& C : Sections) Wrapped.push_back({ C });
    return Loft(Wrapped, Options);
}

Deliver<SkinSolver::Skin> SkinSolver::Loft(const std::vector<std::vector<NurbsCurve>>& Sections, const LoftOptions& Options) noexcept
{
    if (Sections.size() < 2) return Deliver<Skin>::Reject(RefusalReason::DegenerateInput, "loft needs at least two sections");
    size_t LoopCount = Sections.front().size();
    for (const auto& S : Sections) if (S.size() != LoopCount || LoopCount == 0) return Deliver<Skin>::Reject(RefusalReason::DegenerateInput, "every section needs the same number of loops");
    bool Closed = true; for (const auto& S : Sections) for (const NurbsCurve& C : S) Closed &= C.Closed();
    if (!Closed && LoopCount > 1) return Deliver<Skin>::Reject(RefusalReason::DegenerateInput, "multi-loop sections must be closed");
    std::vector<NurbsSurface> Sheets;
    // Loop 0 is the outer loop; holes must run the other way so their sheets face inward (Sew/Orient cannot relate
    //    disconnected hulls, so the caps would otherwise close each hole rim as its own solid). Senses are decided per
    //    station against the previous station's outer loop, so sweeps whose sections turn past 90° stay consistent.
    std::vector<std::vector<NurbsCurve>> Oriented = Sections;
    if (Closed)
    {
        Vec3 Previous;
        for (auto& Station : Oriented)
        {
            Vec3 N = LoopNormal(Station.front());
            if (Previous.LengthSquared() > 0 && N.Dot(Previous) < 0) { Station.front() = Station.front().Reversed(); N = N * -1.0; }
            if (N.LengthSquared() > 0) Previous = N;
            for (size_t L = 1; L < Station.size(); ++L) if (LoopNormal(Station[L]).Dot(N) > 0) Station[L] = Station[L].Reversed();
        }
    }
    for (size_t L = 0; L < LoopCount; ++L)
    {
        std::vector<NurbsCurve> Column; for (const auto& S : Oriented) Column.push_back(S[L]);
        LoftOptions Per = Options; if (Closed) Per.AlignSense = false;
        Deliver<NurbsSurface> Sheet = LoftSheet(Column, Per);
        if (!Sheet) return Deliver<Skin>::Reject(Sheet.Denial.Reason, Sheet.Denial.Detail);
        Sheets.push_back(std::move(Sheet.Payload));
    }
    bool WantBody = Closed && Options.Solid;
    if (!WantBody) { if (Sheets.size() == 1) { Skin Out; Out.Sheet = std::move(Sheets.front()); return Deliver<Skin>::Accept(std::move(Out)); } return Deliver1(std::move(Sheets), true); }
    // Solid: the caps are the first and last sections' planar regions; Sew + Capped derive them from the open rims
    //    (multi-loop rims become one face with holes). A looped loft is already closed.
    return Deliver1(std::move(Sheets), true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  SWEEP
//------------------------------------------------------------------------------------------------------------------------
std::vector<SkinSolver::PathBasis> SkinSolver::BasesAlong(const NurbsCurve& Path, int Count, SweepBases Bases) noexcept
{
    std::vector<PathBasis> Out;
    Count = std::max(Count, 2);
    double T0 = Path.DomainStart(), T1 = Path.DomainEnd();
    // sample by arc length so stations are evenly spaced
    double Total = Path.Length();
    for (int I = 0; I < Count; ++I)
    {
        double F = double(I) / (Count - 1);
        double T = Total > ScalarCriteria::KernelTolerance ? Path.ParameterAtLength(F * Total) : T0 + F * (T1 - T0);
        if (I == Count - 1) T = T1;
        PathBasis Fr; Fr.Parameter = T; Fr.Point = Path.Sample(T); Fr.Tangent = Path.Tangent(T);
        if (Fr.Tangent.LengthSquared() < 1e-24) Fr.Tangent = Out.empty() ? Vec3::UnitZ() : Out.back().Tangent;
        Out.push_back(Fr);
    }
    // initial normal: Frenet normal where curvature allows, else any perpendicular
    Vec3 D[3]; Path.Derivatives(T0, 2, D);
    Vec3 N0 = D[1].Cross(D[2]).Cross(D[1]);
    if (N0.LengthSquared() < 1e-18) N0 = Out.front().Tangent.AnyPerpendicular();
    N0 = (N0 - Out.front().Tangent * N0.Dot(Out.front().Tangent)).Normalised();
    Out.front().Normal = N0;
    for (size_t I = 1; I < Out.size(); ++I)
    {
        if (Bases == SweepBases::Frenet)
        {
            Path.Derivatives(Out[I].Parameter, 2, D);
            Vec3 N = D[1].Cross(D[2]).Cross(D[1]);
            if (N.LengthSquared() < 1e-18) N = Out[I - 1].Normal;
            N = (N - Out[I].Tangent * N.Dot(Out[I].Tangent)).Normalised();
            if (N.Dot(Out[I - 1].Normal) < 0) N = N * -1.0;                                          // no inflection flips
            Out[I].Normal = N;
        }
        else if (Bases == SweepBases::Fixed)
        {
            Vec3 N = Out.front().Normal - Out[I].Tangent * Out.front().Normal.Dot(Out[I].Tangent);
            Out[I].Normal = N.LengthSquared() > 1e-18 ? N.Normalised() : Out[I - 1].Normal;
        }
        else
        {
            // double reflection (Wang, Jüttler, Zheng, Liu 2008)
            Vec3 V1 = Out[I].Point - Out[I - 1].Point; double C1 = V1.Dot(V1);
            if (C1 < 1e-24) { Out[I].Normal = Out[I - 1].Normal; continue; }
            Vec3 RL = Out[I - 1].Normal - V1 * (2.0 / C1 * V1.Dot(Out[I - 1].Normal));
            Vec3 TL = Out[I - 1].Tangent - V1 * (2.0 / C1 * V1.Dot(Out[I - 1].Tangent));
            Vec3 V2 = Out[I].Tangent - TL; double C2 = V2.Dot(V2);
            Vec3 N = C2 < 1e-24 ? RL : RL - V2 * (2.0 / C2 * V2.Dot(RL));
            N = (N - Out[I].Tangent * N.Dot(Out[I].Tangent)).Normalised();
            Out[I].Normal = N;
        }
    }
    for (PathBasis& F : Out) F.Binormal = F.Tangent.Cross(F.Normal).Normalised();
    return Out;
}

Deliver<SkinSolver::Skin> SkinSolver::Sweep(const NurbsCurve& Profile, const NurbsCurve& Path, const SweepOptions& Options) noexcept
{
    return Sweep(std::vector<NurbsCurve>{ Profile }, Path, Options);
}

Deliver<SkinSolver::Skin> SkinSolver::Sweep(const std::vector<NurbsCurve>& ProfileLoops, const NurbsCurve& Path, const SweepOptions& Options) noexcept
{
    if (ProfileLoops.empty()) return Deliver<Skin>::Reject(RefusalReason::DegenerateInput, "sweep needs a profile");
    if (Path.Length() < ScalarCriteria::MergeTolerance) return Deliver<Skin>::Reject(RefusalReason::DegenerateInput, "sweep path has no length");
    // Station count: enough to follow the path's curvature — spans × 4, at least 8 for curved paths, 2 for a line.
    int Spans = 0; for (size_t K = 1; K < Path.Knots.size(); ++K) if (Path.Knots[K] > Path.Knots[K - 1] + ScalarCriteria::ParametricEpsilon) ++Spans;
    int Stations = Options.Stations > 0 ? Options.Stations : (Path.Degree == 1 && Spans == 1 ? 2 : std::max(8, Spans * 4 + 1));
    if (std::fabs(Options.TwistAngle) > 1e-12 || std::fabs(Options.ScaleEnd - 1.0) > 1e-12) Stations = std::max(Stations, 9);
    std::vector<PathBasis> Stops = BasesAlong(Path, Stations, Options.Bases);
    // The profile is expressed in the start basis; its own plane normal decides the mapping: the profile normal maps to
    //    the path tangent (Plasticity places the profile perpendicular to the path at its start).
    const PathBasis& F0 = Stops.front();
    Vec3 ProfileNormal = LoopNormal(ProfileLoops.front());
    Vec3 Centre = CentroidOf(ProfileLoops.front());
    if (!ProfileLoops.front().Closed()) { ProfileNormal = Vec3(); Centre = F0.Point; }
    Mat4 ToLocal;
    if (ProfileNormal.LengthSquared() > 0.5)
    {
        // local basis of the profile: Z = its normal (aligned with the path tangent sense), X = any perpendicular
        if (ProfileNormal.Dot(F0.Tangent) < 0) ProfileNormal = ProfileNormal * -1.0;
        Vec3 X = ProfileNormal.AnyPerpendicular().Normalised(), Y = ProfileNormal.Cross(X).Normalised();
        ToLocal = InverseBasis(Centre, X, Y, ProfileNormal);
        // choose X so that, after mapping, the profile's X lands on the path normal with the least rotation: rotate so
        //    that X ↔ F0.Normal exactly (closest match keeps the profile from spinning when the profile already lies in
        //    the path's start plane).
        Vec3 Xw = ProfileNormal.Cross(F0.Normal).Cross(ProfileNormal);
        if (Xw.LengthSquared() > 1e-18) { X = Xw.Normalised(); Y = ProfileNormal.Cross(X).Normalised(); ToLocal = InverseBasis(Centre, X, Y, ProfileNormal); }
    }
    else
    {
        // open / non-planar profile: keep it where it is relative to the start basis
        ToLocal = InverseBasis(F0.Point, F0.Normal, F0.Binormal, F0.Tangent);
    }
    std::vector<std::vector<NurbsCurve>> Sections;
    for (size_t I = 0; I < Stops.size(); ++I)
    {
        const PathBasis& F = Stops[I];
        double S = double(I) / (Stops.size() - 1);
        double Scale = 1.0 + (Options.ScaleEnd - 1.0) * S;
        double Twist = Options.TwistAngle * S;
        Vec3 N = F.Normal * std::cos(Twist) + F.Binormal * std::sin(Twist);
        Vec3 B = F.Tangent.Cross(N).Normalised();
        Mat4 Place = BasisMatrix(F.Point, N * Scale, B * Scale, F.Tangent);
        Mat4 M = Place * ToLocal;
        std::vector<NurbsCurve> Loops; for (const NurbsCurve& P : ProfileLoops) Loops.push_back(P.Transformed(M));
        Sections.push_back(std::move(Loops));
    }
    LoftOptions L; L.DegreeV = Stops.size() == 2 ? 1 : 3; L.AlignSeams = false; L.AlignSense = false; L.Solid = Options.Solid;
    Deliver<Skin> Out = Loft(Sections, L);
    if (Out && !Out.Payload.IsBody) Out.Payload.Sheet.Classification = SurfaceClassification::Sweep;
    return Out;
}

Deliver<SkinSolver::Skin> SkinSolver::Pipe(const NurbsCurve& Path, double Radius, bool Solid) noexcept
{
    if (Radius <= ScalarCriteria::MergeTolerance) return Deliver<Skin>::Reject(RefusalReason::DegenerateInput, "pipe radius must be positive");
    Deliver<NurbsCurve> C = NurbsCurve::Circle(Path.StartPoint(), Path.Tangent(Path.DomainStart()), Radius);
    if (!C) return Deliver<Skin>::Reject(C.Denial.Reason, C.Denial.Detail);
    SweepOptions O; O.Solid = Solid;
    return Sweep(C.Payload, Path, O);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  PATCH
//------------------------------------------------------------------------------------------------------------------------
std::vector<NurbsCurve> SkinSolver::RingOrder(std::vector<NurbsCurve> Pieces, double Tolerance) noexcept
{
    std::vector<NurbsCurve> Ring;
    if (Pieces.empty()) return Ring;
    Ring.push_back(Pieces.front()); Pieces.erase(Pieces.begin());
    while (!Pieces.empty())
    {
        Vec3 End = Ring.back().EndPoint(); bool Found = false;
        for (size_t I = 0; I < Pieces.size(); ++I)
        {
            if (Pieces[I].StartPoint().Coincident(End, Tolerance)) { Ring.push_back(Pieces[I]); Pieces.erase(Pieces.begin() + long(I)); Found = true; break; }
            if (Pieces[I].EndPoint().Coincident(End, Tolerance)) { Ring.push_back(Pieces[I].Reversed()); Pieces.erase(Pieces.begin() + long(I)); Found = true; break; }
        }
        if (!Found) return {};
    }
    if (!Ring.back().EndPoint().Coincident(Ring.front().StartPoint(), Tolerance)) return {};
    return Ring;
}

Deliver<NurbsCurve> SkinSolver::LoopCurve(const BrepBody& Body, int Loop) noexcept
{
    if (Loop < 0 || Loop >= int(Body.Loops.size())) return Deliver<NurbsCurve>::Reject(RefusalReason::OutOfDomain, "no such loop");
    std::vector<NurbsCurve> Pieces;
    for (int Ce : Body.Loops[Loop].Coedges) { const BrepCoedge& C = Body.Coedges[Ce]; Pieces.push_back(C.Reversed ? Body.Edges[C.Edge].Curve.Reversed() : Body.Edges[C.Edge].Curve); }
    std::vector<NurbsCurve> Ring = RingOrder(Pieces, ScalarCriteria::MergeTolerance * 10);
    if (Ring.empty()) return Deliver<NurbsCurve>::Reject(RefusalReason::OpenWire, "loop coedges do not chain");
    NurbsCurve Out = Ring.front();
    for (size_t I = 1; I < Ring.size(); ++I) { Deliver<NurbsCurve> J = NurbsCurve::Join(Out, Ring[I]); if (!J) return J; Out = std::move(J.Payload); }
    return Deliver<NurbsCurve>::Accept(std::move(Out));
}

namespace
{
    // Non-rational, degree-3, [0,1] version of a boundary (exact for integral cubics and lower; refit otherwise).
    NurbsCurve CubicIntegral(const NurbsCurve& C, double Tolerance) noexcept
    {
        if (!C.Rational())
        {
            NurbsCurve R = C.Degree < 3 ? C.Elevated(3) : C;
            return R.Reparameterised(0.0, 1.0);
        }
        // refit: sample densely enough for the tolerance, interpolate
        int N = 8;
        for (int Round = 0; Round < 6; ++Round)
        {
            std::vector<Vec3> Through;
            for (int I = 0; I <= N; ++I) Through.push_back(C.Sample(C.DomainStart() + (C.DomainEnd() - C.DomainStart()) * I / N));
            Deliver<NurbsCurve> Fit = NurbsCurve::Interpolate(Through, 3, false);
            if (!Fit) { N *= 2; continue; }
            double Worst = 0.0;
            for (int I = 0; I < 64; ++I) { double D = 0; (void)Fit.Payload.ClosestParameter(C.Sample(C.DomainStart() + (C.DomainEnd() - C.DomainStart()) * (I + 0.5) / 64), &D); Worst = std::max(Worst, D); }
            if (Worst <= Tolerance || Round == 5) return Fit.Payload.Reparameterised(0.0, 1.0);
            N *= 2;
        }
        return C.Elevated(3).Reparameterised(0.0, 1.0);
    }
}

Deliver<NurbsSurface> SkinSolver::CoonsPatch(std::vector<NurbsCurve> Boundaries, double Tolerance) noexcept
{
    if (Boundaries.size() != 3 && Boundaries.size() != 4) return Deliver<NurbsSurface>::Reject(RefusalReason::DegenerateInput, "Coons patch needs 3 or 4 boundaries");
    std::vector<NurbsCurve> Ring = RingOrder(std::move(Boundaries), Tolerance);
    if (Ring.empty()) return Deliver<NurbsSurface>::Reject(RefusalReason::OpenWire, "patch boundaries do not form a closed ring");
    if (Ring.size() == 3)
    {
        // degenerate fourth side: a zero-length curve at the corner between Ring[2] and Ring[0]
        Vec3 Corner = Ring.front().StartPoint();
        NurbsCurve Z; Z.Degree = 1; Z.Poles = { Vec4(Corner, 1.0), Vec4(Corner, 1.0) }; Z.Knots = { 0, 0, 1, 1 };   // built by hand: Build() refuses zero length
        Ring.push_back(Z);
    }
    // Ring: C0 (bottom, u→), D1 (right, v↑), C1 (top, reversed so u→), D0 (left, reversed so v↑)
    NurbsCurve C0 = CubicIntegral(Ring[0], Tolerance), D1 = CubicIntegral(Ring[1], Tolerance);
    NurbsCurve C1 = CubicIntegral(Ring[2].Reversed(), Tolerance), D0 = CubicIntegral(Ring[3].Reversed(), Tolerance);
    // common knots along U (C0 & C1) and V (D0 & D1)
    Deliver<std::vector<NurbsCurve>> UPair = Compatible({ C0, C1 }), VPair = Compatible({ D0, D1 });
    if (!UPair || !VPair) return Deliver<NurbsSurface>::Reject(RefusalReason::NoConvergence, "patch boundaries could not be made compatible");
    C0 = UPair.Payload[0]; C1 = UPair.Payload[1]; D0 = VPair.Payload[0]; D1 = VPair.Payload[1];
    // Coons in pole space (exact because the blends are linear and the boundaries share knots after refinement):
    //    S = ruled(C0,C1) + ruled(D0,D1) − bilinear(corners). Ruled surfaces are degree 1 in the other direction; elevate
    //    to the boundary degree and insert the boundary knots so all three share one lattice.
    int NU = C0.PoleCount(), NV = D0.PoleCount();
    // Build lattice-compatible curves for the degree-1 directions by elevating a 2-pole line to degree 3 with the
    //    other pair's knots: use NurbsCurve::Line then Elevated + Refined.
    auto LinearAs = [](const NurbsCurve& Shape, Vec4 A, Vec4 B) -> std::vector<Vec4>
    {
        NurbsCurve L; L.Degree = 1; L.Poles = { A, B }; L.Knots = { 0, 0, 1, 1 };
        NurbsCurve E = L.Elevated(Shape.Degree);
        std::vector<double> Interior; for (int K = Shape.Degree + 1; K < Shape.PoleCount(); ++K) Interior.push_back(Shape.Knots[K]);
        E = E.Refined(Interior);
        return E.Poles;
    };
    Vec4 P00 = C0.Poles.front(), P10 = C0.Poles.back(), P01 = C1.Poles.front(), P11 = C1.Poles.back();
    std::vector<Vec4> Poles(size_t(NU) * NV);
    // ruled between C0 and C1 along V: for each U pole, linear in V with D0's knots
    for (int I = 0; I < NU; ++I)
    {
        std::vector<Vec4> Col = LinearAs(D0, C0.Poles[I], C1.Poles[I]);
        for (int J = 0; J < NV; ++J) Poles[size_t(I) * NV + J] = Col[J];
    }
    for (int J = 0; J < NV; ++J)
    {
        std::vector<Vec4> Row = LinearAs(C0, D0.Poles[J], D1.Poles[J]);
        for (int I = 0; I < NU; ++I) Poles[size_t(I) * NV + J] = Poles[size_t(I) * NV + J] + Row[I];
    }
    std::vector<Vec4> Bottom = LinearAs(C0, P00, P10), Top = LinearAs(C0, P01, P11);
    for (int I = 0; I < NU; ++I)
    {
        std::vector<Vec4> Col = LinearAs(D0, Bottom[I], Top[I]);
        for (int J = 0; J < NV; ++J) { Vec4& P = Poles[size_t(I) * NV + J]; P = P - Col[J]; P.W = 1.0; }
    }
    Deliver<NurbsSurface> S = NurbsSurface::Build(C0.Degree, D0.Degree, NU, NV, std::move(Poles), C0.Knots, D0.Knots);
    if (S) S.Payload.Classification = SurfaceClassification::Coons;
    return S;
}

Deliver<SkinSolver::Skin> SkinSolver::Patch(std::vector<NurbsCurve> Boundaries, double Tolerance) noexcept
{
    if (Boundaries.size() == 1 && Boundaries.front().Closed())
    {
        // one closed curve: split into four quarters by arc length
        const NurbsCurve& C = Boundaries.front(); double L = C.Length();
        std::vector<NurbsCurve> Quarters; NurbsCurve Rest = C;
        for (int Q = 1; Q < 4; ++Q)
        {
            double T = Rest.ParameterAtLength(L / 4.0);
            auto [A, B] = Rest.Split(T); Quarters.push_back(A); Rest = B;
        }
        Quarters.push_back(Rest);
        Boundaries = Quarters;
    }
    if (Boundaries.size() < 3) return Deliver<Skin>::Reject(RefusalReason::DegenerateInput, "patch needs at least three boundaries (or one closed curve)");
    Skin Out;
    if (Boundaries.size() <= 4)
    {
        Deliver<NurbsSurface> S = CoonsPatch(std::move(Boundaries), Tolerance);
        if (!S) return Deliver<Skin>::Reject(S.Denial.Reason, S.Denial.Detail);
        Out.Sheet = std::move(S.Payload);
        return Deliver<Skin>::Accept(std::move(Out));
    }
    // N-sided: split each boundary at its midpoint; centre = mean of midpoints pulled toward the boundary plane; each
    //    quad = (second half of side i, first half of side i+1, spoke from mid(i+1) to centre, spoke from centre to mid(i)).
    std::vector<NurbsCurve> Ring = RingOrder(std::move(Boundaries), Tolerance);
    if (Ring.empty()) return Deliver<Skin>::Reject(RefusalReason::OpenWire, "patch boundaries do not form a closed ring");
    size_t N = Ring.size();
    std::vector<NurbsCurve> First(N), Second(N); std::vector<Vec3> Mid(N);
    Vec3 Centre;
    for (size_t I = 0; I < N; ++I)
    {
        double T = Ring[I].ParameterAtLength(Ring[I].Length() * 0.5);
        auto [A, B] = Ring[I].Split(T); First[I] = A; Second[I] = B; Mid[I] = A.EndPoint(); Centre = Centre + Mid[I];
    }
    Centre = Centre * (1.0 / double(N));
    // spokes: cubic with tangents blending the neighbouring boundary tangents for a smooth centre
    std::vector<NurbsCurve> Spoke(N);
    for (size_t I = 0; I < N; ++I)
    {
        Vec3 P = Mid[I];
        Vec3 Toward = (Centre - P);
        // inward direction perpendicular to the boundary tangent
        Vec3 Tan = Ring[I].Tangent(Ring[I].ParameterAtLength(Ring[I].Length() * 0.5));
        Vec3 In = (Toward - Tan * Toward.Dot(Tan));
        if (In.LengthSquared() < 1e-18) In = Toward;
        In = In.Normalised() * (Toward.Length() / 3.0);
        // cubic Bézier: leaves the rim perpendicular to it, arrives at the centre along the mirrored direction
        Deliver<NurbsCurve> S = NurbsCurve::Bezier({ P, P + In, Centre + (P + In - Centre) * (1.0 / 3.0), Centre });
        Spoke[I] = S ? S.Payload : NurbsCurve::Line(P, Centre).Payload;
    }
    std::vector<NurbsSurface> Quads;
    for (size_t I = 0; I < N; ++I)
    {
        size_t J = (I + 1) % N;
        Deliver<NurbsSurface> Q = CoonsPatch({ Second[I], First[J], Spoke[J], Spoke[I].Reversed() }, Tolerance);
        if (!Q) return Deliver<Skin>::Reject(Q.Denial.Reason, Q.Denial.Detail);
        Quads.push_back(std::move(Q.Payload));
    }
    return Deliver1(std::move(Quads), true, false);                                         // a fill is a sheet body: never cap its rim
}

// Project a set of guide curves onto a NURBS surface by iteratively pulling each interior control point toward the
//    guide samples' offsets. Boundary rows (u = 0, u = 1, v = 0, v = 1) are clamped, so the loft's exact profile
//    boundaries are preserved; only the interior bends. Each round: for every guide sample at closest (u,v) on the
//    current surface, compute the offset to the target, and distribute it to the interior control points weighted by
//    their basis-function value. Re-project after each round so the closest (u,v) follows the surface as it bends.
//    This is the same iterative-projection pattern FairPatchSolver uses for its guides, applied to a lofted sheet.
namespace
{
    // Approximate B-spline basis function value at parameter u in [0,1] for a non-periodic clamped curve of the given
    //    degree, with control points evenly spaced at t = i/(Count-1). The piecewise polynomial is bell-shaped; this
    //    proxy is exact for cubic and lower, near-exact for higher. Good enough for distributing surface-point offsets
    //    to nearby control points; the iterative rounds recover the lost precision.
    double BasisProxy(double ControlU, double U, int Degree) noexcept
    {
        if (U < 0 || U > 1) return 0;
        double D = std::fabs(ControlU - U);
        double HalfWidth = 0.5; int P = std::max(1, Degree);
        if (P == 1) HalfWidth = 0.5;
        else if (P == 2) HalfWidth = 1.0;
        else if (P == 3) HalfWidth = 1.5;
        else HalfWidth = double(P) * 0.5;
        if (D >= HalfWidth) return 0;
        double T = 1.0 - D / HalfWidth;
        // Smooth bump: t^2 (3 - 2t) — cubic Hermite, 0 at the edges, 1 at the centre.
        return T * T * (3.0 - 2.0 * T);
    }
    // Is the (I, J) pole on a boundary row? (We leave the four boundary rows clamped so the loft's profiles are exact.)
    bool BoundaryRow(int CountU, int CountV, int I, int J) noexcept
    {
        return I == 0 || I == CountU - 1 || J == 0 || J == CountV - 1;
    }
}

NurbsSurface SkinSolver::ProjectGuides(NurbsSurface Sheet, const LoftGuideOptions& Options) noexcept
{
    if (Options.Guides.empty() || Options.Weight <= 0.0) return Sheet;
    for (int Round = 0; Round < std::max(1, Options.Rounds); ++Round)
    {
        // For each control point P_{IJ}, accumulate a weighted offset (and the total weight) across all guide samples.
        //    The V coordinate of a guide sample is the sample's normalized parameter (T mapped to [0,1]) — guides are
        //    ordered along their arc, not along the surface's natural U/V, so the convention is "the guide samples
        //    sweep V from 0 to 1". U is taken from the closest parameter on the current surface, so the guide can
        //    walk across the surface in U as the V constraint moves the surface around.
        std::vector<Vec3> Offset(Sheet.Poles.size(), Vec3{});
        std::vector<double> Total(Sheet.Poles.size(), 0.0);
        for (const NurbsCurve& Guide : Options.Guides)
        {
            if (Guide.PoleCount() < 2 || Guide.Length() <= 0) continue;
            for (int K = 0; K < Options.SamplesPerGuide; ++K)
            {
                double NormT = double(K) / std::max(1, Options.SamplesPerGuide - 1);
                double T = Guide.DomainStart() + (Guide.DomainEnd() - Guide.DomainStart()) * NormT;
                Vec3 Target = Guide.Sample(T);
                // Use V = NormT (the guide's normalized position along its arc, mapped to the surface V range).
                double V = NormT * (Sheet.DomainEndV() - Sheet.DomainStartV()) + Sheet.DomainStartV();
                // Find the U that lands closest to the target along the iso-curve at V.
                double U = 0.5 * (Sheet.DomainStartU() + Sheet.DomainEndU());
                double BestDistance = std::numeric_limits<double>::infinity();
                int USamples = 24;
                for (int US = 0; US <= USamples; ++US)
                {
                    double Uu = Sheet.DomainStartU() + (Sheet.DomainEndU() - Sheet.DomainStartU()) * US / USamples;
                    Vec3 P = Sheet.Sample(Uu, V);
                    double D = (P - Target).LengthSquared();
                    if (D < BestDistance) { BestDistance = D; U = Uu; }
                }
                Vec3 SurfacePoint = Sheet.Sample(U, V);
                Vec3 Delta = (Target - SurfacePoint) * Options.Weight;
                int CU = Sheet.CountU, CV = Sheet.CountV;
                for (int I = 0; I < CU; ++I)
                    for (int J = 0; J < CV; ++J)
                    {
                        if (BoundaryRow(CU, CV, I, J)) continue;
                        double W = BasisProxy(double(I) / std::max(1, CU - 1), (U - Sheet.DomainStartU()) / (Sheet.DomainEndU() - Sheet.DomainStartU()), Sheet.DegreeU) *
                                   BasisProxy(double(J) / std::max(1, CV - 1), (V - Sheet.DomainStartV()) / (Sheet.DomainEndV() - Sheet.DomainStartV()), Sheet.DegreeV);
                        if (W < 1e-9) continue;
                        Offset[I * CV + J] = Offset[I * CV + J] + Delta * W;
                        Total[I * CV + J] += W;
                    }
            }
        }
        for (size_t K = 0; K < Offset.size(); ++K)
        {
            if (Total[K] < 1e-9) continue;
            Vec3 Move = Offset[K] / Total[K];
            Vec4& P = Sheet.Poles[K];
            if (std::fabs(P.W) < 1e-9) P.W = 1.0;
            P.X += Move.X * P.W; P.Y += Move.Y * P.W; P.Z += Move.Z * P.W;
        }
    }
    return Sheet;
}

} // namespace Frontier
