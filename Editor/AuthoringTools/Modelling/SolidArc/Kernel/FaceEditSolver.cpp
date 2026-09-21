//=============================================================================================================================================
// SolidArc · conservative face-edit routes (Phase 36a)
//=============================================================================================================================================
#include "FaceEditSolver.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace Frontier
{
namespace
{
constexpr double UnitTolerance = 1e-6;

struct BoxFrame
{
    Vec3 Low{};
    Vec3 High{};
};

struct FaceFrame
{
    BoxFrame Box;
    int Axis = -1;                                                                    // world axis normal to the face
    int Sign = 0;                                                                     // outward normal sign
};

[[nodiscard]] double Component(Vec3 P, int Axis) noexcept
{
    return Axis == 0 ? P.X : Axis == 1 ? P.Y : P.Z;
}

[[nodiscard]] Vec3 AxisVector(int Axis, double Sign = 1.0) noexcept
{
    return Axis == 0 ? Vec3(Sign, 0, 0) : Axis == 1 ? Vec3(0, Sign, 0) : Vec3(0, 0, Sign);
}

[[nodiscard]] bool Close(double A, double B, double Tolerance) noexcept
{
    return std::fabs(A - B) <= Tolerance * std::max(1.0, std::max(std::fabs(A), std::fabs(B)));
}

[[nodiscard]] bool ClosePoint(Vec3 A, Vec3 B, double Tolerance) noexcept
{
    return A.Distance(B) <= Tolerance * std::max(1.0, std::max(A.Length(), B.Length()));
}

[[nodiscard]] Deliver<NurbsSurface> Quad(Vec3 P00, Vec3 P10, Vec3 P01, Vec3 P11) noexcept
{
    // A degree-one tensor patch is an exact plane for a parallelogram and an exact bilinear patch for
    // a drafted quadrilateral. It is preferable to silently fitting a plane: the four rim points are
    // retained exactly and the topology builder still has natural four-edge boundaries.
    return NurbsSurface::Patch(1, 1, 2, 2, { P00, P01, P10, P11 });
}

[[nodiscard]] Deliver<BrepBody> SewNatural(const std::vector<NurbsSurface>& Surfaces, bool RequireSolid) noexcept
{
    Deliver<BrepBody> Sewn = BrepBody::Sew(Surfaces, ScalarCriteria::MergeTolerance, false);
    if (!Sewn) return Sewn;
    BrepBody Result = std::move(Sewn.Payload);
    const BodyReport R = Result.Validate();
    if (R.NonManifoldEdges != 0 || R.MisorientedEdges != 0 || R.Faces == 0)
    {
        if (R.NonManifoldEdges != 0) return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "face edit produced non-manifold topology");
        if (R.MisorientedEdges != 0) return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "face edit produced misoriented topology");
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "face edit produced no faces");
    }
    if (RequireSolid && !R.Solid())
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "face edit did not close a positive-volume solid");
    return Deliver<BrepBody>::Accept(std::move(Result));
}

[[nodiscard]] bool ReadBox(const BrepBody& Source, BoxFrame& Out) noexcept
{
    const BodyReport R = Source.Validate();
    if (!R.Solid() || R.Vertices != 8 || R.Edges != 12 || R.Faces != 6 || R.Hulls != 1)
        return false;
    Out.Low = Source.Bounds().Low;
    Out.High = Source.Bounds().High;
    const Vec3 D = Out.High - Out.Low;
    if (D.X <= ScalarCriteria::MergeTolerance || D.Y <= ScalarCriteria::MergeTolerance || D.Z <= ScalarCriteria::MergeTolerance)
        return false;
    for (const BrepFace& F : Source.Faces)
        if (!F.Natural || F.Surface.Classification != SurfaceClassification::Plane || F.Loops.size() != 1)
            return false;
    return true;
}

[[nodiscard]] bool ReadFace(const BrepBody& Source, int Face, FaceFrame& Out) noexcept
{
    if (Face < 0 || Face >= static_cast<int>(Source.Faces.size())) return false;
    if (!ReadBox(Source, Out.Box)) return false;
    const BrepFace& F = Source.Faces[Face];
    const Vec3 N = Source.FaceNormal(Face, 0.5 * (F.Surface.DomainStartU() + F.Surface.DomainEndU()),
                                      0.5 * (F.Surface.DomainStartV() + F.Surface.DomainEndV()));
    const double A[] = { std::fabs(N.X), std::fabs(N.Y), std::fabs(N.Z) };
    Out.Axis = static_cast<int>(std::max_element(std::begin(A), std::end(A)) - std::begin(A));
    if (A[Out.Axis] < 1.0 - UnitTolerance) return false;
    Out.Sign = Component(N, Out.Axis) >= 0.0 ? 1 : -1;
    const Box3 FB = F.Surface.Bounds();
    const double PlaneCoordinate = Out.Sign > 0 ? Component(Out.Box.High, Out.Axis) : Component(Out.Box.Low, Out.Axis);
    if (!Close(Component(FB.Low, Out.Axis), PlaneCoordinate, ScalarCriteria::MergeTolerance) ||
        !Close(Component(FB.High, Out.Axis), PlaneCoordinate, ScalarCriteria::MergeTolerance)) return false;
    return true;
}

[[nodiscard]] std::vector<NurbsSurface> PrismSurfaces(Vec3 Low, Vec3 High, Vec3 TopLow, Vec3 TopHigh) noexcept
{
    // Lower and upper rectangles share X/Y bounds. TopLow/TopHigh may differ from Low/High in
    // one coordinate, which is the exact drafted-prism route.
    const Vec3 L00{ Low.X, Low.Y, Low.Z }, L10{ Low.X, High.Y, Low.Z };
    const Vec3 L01{ High.X, Low.Y, Low.Z }, L11{ High.X, High.Y, Low.Z };
    const Vec3 U00{ TopLow.X, TopLow.Y, TopLow.Z }, U10{ TopLow.X, TopHigh.Y, TopLow.Z };
    const Vec3 U01{ TopHigh.X, TopLow.Y, TopHigh.Z }, U11{ TopHigh.X, TopHigh.Y, TopHigh.Z };
    std::vector<NurbsSurface> Faces;
    auto Add = [&](Deliver<NurbsSurface> S) { if (S) Faces.push_back(std::move(S.Payload)); };
    // For an ordinary box TopLow/TopHigh are (low.x,low.y,high.z)/(high.x,high.y,high.z).
    Add(Quad(L00, L01, L10, L11));                                                // z = low
    Add(Quad(U00, U01, U10, U11));                                                // z = high / drafted top
    Add(Quad(L00, U00, L01, U01));                                                // y = low
    Add(Quad(L10, L11, U10, U11));                                                // y = high
    Add(Quad(L00, L10, U00, U10));                                                // x = low
    Add(Quad(L01, U01, L11, U11));                                                // x = high / drafted side
    return Faces;
}

[[nodiscard]] Deliver<BrepBody> BuildDraftedPrism(const BoxFrame& B, int Axis, int Sign, double Delta) noexcept
{
    if (Axis == 2) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "draft currently supports vertical side faces (+/-X or +/-Y), not a cap");
    Vec3 TopLow{ B.Low.X, B.Low.Y, B.High.Z }, TopHigh{ B.High.X, B.High.Y, B.High.Z };
    if (Axis == 0)
    {
        if (Sign > 0) TopHigh.X += Delta; else TopLow.X -= Delta;
    }
    else
    {
        if (Sign > 0) TopHigh.Y += Delta; else TopLow.Y -= Delta;
    }
    const double WidthX = TopHigh.X - TopLow.X, WidthY = TopHigh.Y - TopLow.Y;
    if (WidthX <= ScalarCriteria::MergeTolerance || WidthY <= ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "draft collapses the upper section");
    return SewNatural(PrismSurfaces(B.Low, B.High, TopLow, TopHigh), true);
}

[[nodiscard]] bool BoundaryCurveMatches(const NurbsCurve& A, const NurbsCurve& B, double Tolerance) noexcept
{
    const bool SameSense = ClosePoint(A.StartPoint(), B.StartPoint(), Tolerance) && ClosePoint(A.EndPoint(), B.EndPoint(), Tolerance);
    const bool OppSense = ClosePoint(A.StartPoint(), B.EndPoint(), Tolerance) && ClosePoint(A.EndPoint(), B.StartPoint(), Tolerance);
    if (!SameSense && !OppSense) return false;
    for (int I = 1; I < 6; ++I)
    {
        const double T = A.DomainStart() + (A.DomainEnd() - A.DomainStart()) * I / 6.0;
        double Distance = 0.0;
        (void)B.ClosestParameter(A.Sample(T), &Distance);
        if (Distance > Tolerance * 10.0) return false;
    }
    return true;
}

[[nodiscard]] std::vector<NurbsCurve> NaturalBoundaryEdges(const BrepBody& Body, int Face) noexcept
{
    std::vector<NurbsCurve> Out;
    if (Face < 0 || Face >= static_cast<int>(Body.Faces.size())) return Out;
    for (int L : Body.Faces[Face].Loops)
        for (int C : Body.Loops[L].Coedges) Out.push_back(Body.CoedgeCurve(C));
    return Out;
}

[[nodiscard]] bool SameRim(const BrepBody& Source, int Face, const NurbsSurface& Replacement, double Tolerance) noexcept
{
    const std::vector<NurbsCurve> SourceEdges = NaturalBoundaryEdges(Source, Face);
    const BrepBody ReplacementBody = BrepBody::FromSurface(Replacement);
    const std::vector<NurbsCurve> ReplacementEdges = NaturalBoundaryEdges(ReplacementBody, 0);
    if (SourceEdges.size() != 4 || ReplacementEdges.size() != 4) return false;
    for (const NurbsCurve& A : SourceEdges)
    {
        bool Found = false;
        for (const NurbsCurve& B : ReplacementEdges) if (BoundaryCurveMatches(A, B, Tolerance)) { Found = true; break; }
        if (!Found) return false;
    }
    return true;
}

[[nodiscard]] Deliver<BrepBody> ShellByExtrudedU(const BoxFrame& B, int Axis, int Sign, double T) noexcept
{
    // Offset a rectangular prism in a local 2D cross-section and extrude it along the
    // remaining box axis. This is an exact closed U-profile: the selected face is the opening,
    // the inner floor/walls are real B-rep faces, and no Boolean or zero-area rim is involved.
    Vec3 N = AxisVector(Axis, static_cast<double>(Sign));
    int UAxis = 0, VAxis = 1;
    if (Axis == 0) { UAxis = 1; VAxis = 2; }
    if (Axis == 1) { UAxis = 0; VAxis = 2; }
    if (Axis == 2) { UAxis = 0; VAxis = 1; }
    Vec3 U = AxisVector(UAxis), V = AxisVector(VAxis);
    const double Q0 = Sign > 0 ? Component(B.Low, Axis) : -Component(B.High, Axis);
    const double Q1 = Sign > 0 ? Component(B.High, Axis) : -Component(B.Low, Axis);
    const double U0 = Component(B.Low, UAxis), U1 = Component(B.High, UAxis);
    const double V0 = Component(B.Low, VAxis), V1 = Component(B.High, VAxis);
    const double qi0 = Q0 + T, qi1 = Q1 - T, ui0 = U0 + T, ui1 = U1 - T;
    auto P = [&](double Q, double X, double Along) { return N * Q + U * X + V * Along; };
    // The profile walks outer bottom, outer opening edge, inset inner wall, inner floor,
    // then back out around the opposite wall. The small diagonal transitions are the exact
    // mitered material at the open rim, so the route remains manifold at all four corners.
    std::vector<Vec3> Points = {
        P(Q0, U0, V0), P(Q0, U1, V0), P(Q1, U1, V0),
        P(qi1, ui1, V0), P(qi0, ui1, V0), P(qi0, ui0, V0),
        P(qi1, ui0, V0), P(Q1, U0, V0) };
    Deliver<NurbsCurve> Profile = NurbsCurve::Polyline(Points, true);
    if (!Profile) return Deliver<BrepBody>::Reject(Profile.Denial.Reason, Profile.Denial.Detail);
    return BrepBody::Extrude(Profile.Payload, V, V1 - V0);
}

} // namespace

bool FaceEditSolver::IsCanonicalBox(const BrepBody& Source) noexcept
{
    BoxFrame B;
    return ReadBox(Source, B);
}

Deliver<BrepBody> FaceEditSolver::Heal(const BrepBody& Source, double Tolerance) noexcept
{
    if (Tolerance <= 0.0) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "healing tolerance must be positive");
    if (Source.Faces.empty()) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "cannot heal an empty body");
    std::vector<NurbsSurface> Surfaces;
    Surfaces.reserve(Source.Faces.size());
    bool HasTrimmedFace = false;
    for (const BrepFace& F : Source.Faces)
    {
        HasTrimmedFace = HasTrimmedFace || !F.Natural;
        const Box3 B = F.Surface.Bounds();
        if (B.Empty() || B.Diagonal() <= Tolerance) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "sliver face detected");
        if (F.Natural) Surfaces.push_back(F.Surface);
    }
    // Capped extrusions and shells legitimately contain trimmed planar caps. Rebuilding those
    // surfaces from their support would discard their loops, so validate and return an immutable
    // copy instead of pretending a support-only re-sew is a healing operation.
    if (HasTrimmedFace)
    {
        const BodyReport Existing = Source.Validate();
        for (const BrepEdge& E : Source.Edges)
            if (E.Curve.Length() <= Tolerance) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "sliver edge detected; healing refused to collapse it");
        if (Existing.NonManifoldEdges != 0 || Existing.MisorientedEdges != 0)
            return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "healing found non-manifold or misoriented trimmed topology");
        return Deliver<BrepBody>::Accept(Source);
    }
    Deliver<BrepBody> Result = BrepBody::Sew(Surfaces, Tolerance, false);
    if (!Result) return Result;
    const BodyReport R = Result.Payload.Validate();
    // Re-sewing is the only safe sliver cleanup in this kernel: it removes orphaned duplicate
    // boundaries while preserving every source surface. A tiny edge is rejected, never collapsed.
    for (const BrepEdge& E : Result.Payload.Edges)
        if (E.Curve.Length() <= Tolerance) return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "sliver edge detected; healing refused to collapse it");
    if (R.NonManifoldEdges != 0 || R.MisorientedEdges != 0)
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "healing left non-manifold or misoriented topology");
    return Result;
}

Deliver<BrepBody> FaceEditSolver::RemoveSlivers(const BrepBody& Source, double Tolerance) noexcept
{
    return Heal(Source, Tolerance);
}

Deliver<BrepBody> FaceEditSolver::OffsetFace(const BrepBody& Source, int Face, double Distance) noexcept
{
    FaceFrame F;
    if (!ReadFace(Source, Face, F)) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "exact face offset currently requires a canonical axis-aligned box face");
    if (!std::isfinite(Distance) || std::fabs(Distance) <= ScalarCriteria::KernelTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "face offset distance is zero or non-finite");
    BoxFrame B = F.Box;
    if (F.Axis == 0) { if (F.Sign > 0) B.High.X += Distance; else B.Low.X -= Distance; }
    if (F.Axis == 1) { if (F.Sign > 0) B.High.Y += Distance; else B.Low.Y -= Distance; }
    if (F.Axis == 2) { if (F.Sign > 0) B.High.Z += Distance; else B.Low.Z -= Distance; }
    if (B.High.X - B.Low.X <= ScalarCriteria::MergeTolerance || B.High.Y - B.Low.Y <= ScalarCriteria::MergeTolerance || B.High.Z - B.Low.Z <= ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "face offset would invert or collapse the box");
    return BrepBody::Box(B.Low, B.High);
}

Deliver<BrepBody> FaceEditSolver::ExtendFace(const BrepBody& Source, int Face, double Distance) noexcept
{
    FaceFrame F;
    if (!ReadFace(Source, Face, F)) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "exact face extension currently requires a canonical axis-aligned box face");
    if (!std::isfinite(Distance) || Distance <= ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "face extension distance must be positive");
    BoxFrame B = F.Box;
    if (F.Axis != 0) { B.Low.X -= Distance; B.High.X += Distance; }
    if (F.Axis != 1) { B.Low.Y -= Distance; B.High.Y += Distance; }
    if (F.Axis != 2) { B.Low.Z -= Distance; B.High.Z += Distance; }
    return BrepBody::Box(B.Low, B.High);
}

Deliver<BrepBody> FaceEditSolver::TrimFace(const BrepBody& Source, int Face, double Distance) noexcept
{
    FaceFrame F;
    if (!ReadFace(Source, Face, F)) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "exact face trim currently requires a canonical axis-aligned box face");
    if (!std::isfinite(Distance) || Distance <= ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "face trim distance must be positive");
    BoxFrame B = F.Box;
    if (F.Axis != 0) { B.Low.X += Distance; B.High.X -= Distance; }
    if (F.Axis != 1) { B.Low.Y += Distance; B.High.Y -= Distance; }
    if (F.Axis != 2) { B.Low.Z += Distance; B.High.Z -= Distance; }
    if (B.High.X - B.Low.X <= ScalarCriteria::MergeTolerance || B.High.Y - B.Low.Y <= ScalarCriteria::MergeTolerance || B.High.Z - B.Low.Z <= ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "face trim would collapse a neighbouring span");
    return BrepBody::Box(B.Low, B.High);
}

Deliver<BrepBody> FaceEditSolver::Draft(const BrepBody& Source, int Face, double AngleRadians) noexcept
{
    FaceFrame F;
    if (!ReadFace(Source, Face, F)) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "exact draft currently requires a canonical axis-aligned box face");
    if (!std::isfinite(AngleRadians) || std::fabs(AngleRadians) >= ScalarCriteria::HalfPi - ScalarCriteria::AngularTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "draft angle must be finite and strictly below 90 degrees");
    const double Delta = std::tan(AngleRadians) * (F.Box.High.Z - F.Box.Low.Z);
    if (!std::isfinite(Delta) || std::fabs(Delta) <= ScalarCriteria::KernelTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "draft produces no measurable change");
    return BuildDraftedPrism(F.Box, F.Axis, F.Sign, Delta);
}

Deliver<BrepBody> FaceEditSolver::DeleteFace(const BrepBody& Source, int Face) noexcept
{
    BoxFrame B;
    if (!ReadBox(Source, B) || Face < 0 || Face >= static_cast<int>(Source.Faces.size()))
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "face deletion currently requires a canonical axis-aligned box");
    std::vector<NurbsSurface> Surfaces;
    for (int I = 0; I < static_cast<int>(Source.Faces.size()); ++I) if (I != Face) Surfaces.push_back(Source.Faces[I].Surface);
    return SewNatural(Surfaces, false);
}

Deliver<BrepBody> FaceEditSolver::ReplaceFace(const BrepBody& Source, int Face, const NurbsSurface& Replacement) noexcept
{
    BoxFrame B;
    if (!ReadBox(Source, B) || Face < 0 || Face >= static_cast<int>(Source.Faces.size()))
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "face replacement currently requires a canonical axis-aligned box");
    const Refusal ReplacementError = Replacement.Validate();
    if (ReplacementError) return Deliver<BrepBody>::Reject(ReplacementError.Reason, ReplacementError.Detail);
    if (!SameRim(Source, Face, Replacement, ScalarCriteria::MergeTolerance))
        return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "replacement face rim is not identical; adjacent faces were left untouched");
    std::vector<NurbsSurface> Surfaces;
    for (int I = 0; I < static_cast<int>(Source.Faces.size()); ++I) Surfaces.push_back(I == Face ? Replacement : Source.Faces[I].Surface);
    Deliver<BrepBody> Result = SewNatural(Surfaces, true);
    if (!Result) return Result;
    if (Result.Payload.Faces.size() != Source.Faces.size())
        return Deliver<BrepBody>::Reject(RefusalReason::NonManifold, "replacement changed face count during sewing");
    return Result;
}

Deliver<BrepBody> FaceEditSolver::Shell(const BrepBody& Source, int Face, double Thickness) noexcept
{
    FaceFrame F;
    if (!ReadFace(Source, Face, F)) return Deliver<BrepBody>::Reject(RefusalReason::Unsupported, "shell/thicken currently requires a canonical axis-aligned box");
    if (!std::isfinite(Thickness) || Thickness <= ScalarCriteria::MergeTolerance)
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "shell thickness must be positive");
    const Vec3 D = F.Box.High - F.Box.Low;
    if (Thickness * 2.0 >= std::min(D.X, std::min(D.Y, D.Z)))
        return Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "shell thickness leaves no positive inner cavity");
    return ShellByExtrudedU(F.Box, F.Axis, F.Sign, Thickness);
}

} // namespace Frontier
