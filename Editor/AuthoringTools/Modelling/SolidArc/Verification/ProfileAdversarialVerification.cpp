//============================================================================================================================================
// 📦 Verification/ProfileAdversarialVerification.cpp — Phase 23: adversarial planar NURBS contacts, crossings, offsets and booleans
//============================================================================================================================================
// This is deliberately a kernel-facing regression net.  It captures the distinctions a profile editor must retain:
// coincident portions are continua rather than invented point crossings; a tangent is a zero-area contact; and a
// self-crossing cannot be passed on as though it were a valid single boundary.  It also measures real free-form offset
// distance rather than merely checking that an offset curve was returned.
#include "Kernel/ProfileSolver.h"
#include "VerificationPanel.h"
#include <algorithm>
#include <cmath>
#include <vector>

using namespace Frontier;

namespace
{
[[nodiscard]] double OffsetDistanceError(const NurbsCurve& Source, const NurbsCurve& Offset, double Distance) noexcept
{
    double Worst = 0.0;
    for (int I = 0; I <= 240; ++I)
    {
        double T = ScalarCriteria::Lerp(Offset.DomainStart(), Offset.DomainEnd(), static_cast<double>(I) / 240.0);
        double Nearest = 0.0;
        (void)Source.ClosestParameter(Offset.Sample(T), &Nearest);
        Worst = std::max(Worst, std::fabs(Nearest - std::fabs(Distance)));
    }
    return Worst;
}

[[nodiscard]] bool Simple(const Profile& P) noexcept
{
    for (const ProfileLoop& L : P.Loops) if (!ProfileSolver::SelfIntersections(L.Curve).empty()) return false;
    return true;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 23 · Adversarial Profile Verification — coincident / tangent contacts, self-crossing splines, free-form offset + boolean");
    const Vec3 Z = Vec3::UnitZ();
    const double Pi = ScalarCriteria::Pi;
    const Workplane XY = Workplane::XY();
    auto Assemble = [&](const NurbsCurve& C) { return ProfileSolver::Assemble({ C }, Z); };

    Panel.Section("Coincident and overlapping contact semantics");
    {
        NurbsCurve Full = NurbsCurve::Line({ 0, 0, 0 }, { 4, 0, 0 }).Payload;
        NurbsCurve Nested = NurbsCurve::Line({ 1, 0, 0 }, { 3, 0, 0 }).Payload;
        Panel.Expect("Nested coincident line segment creates no fake isolated crossing", ProfileSolver::Intersect(Full, Nested).empty());
        Panel.Expect("Exactly coincident line creates no fake isolated crossing", ProfileSolver::Intersect(Full, Full).empty());

        NurbsCurve Square = NurbsCurve::Rectangle(XY, { 0, 0 }, { 4, 4 }).Payload;
        Deliver<Profile> A = Assemble(Square);
        Deliver<Profile> SameUnion = ProfileSolver::Combine(A.Payload, A.Payload, ProfileOperation::Union);
        Deliver<Profile> SameSubtract = ProfileSolver::Combine(A.Payload, A.Payload, ProfileOperation::Subtract);
        Deliver<Profile> SameIntersect = ProfileSolver::Combine(A.Payload, A.Payload, ProfileOperation::Intersect);
        Panel.Expect("Coincident profiles: union has one simple loop", SameUnion && SameUnion.Payload.Loops.size() == 1 && Simple(SameUnion.Payload));
        Panel.Within("Coincident profiles: union area is unchanged", SameUnion ? std::fabs(SameUnion.Payload.Area() - 16.0) : 1.0, 1e-12);
        Panel.Expect("Coincident profiles: subtract is empty", SameSubtract && SameSubtract.Payload.Loops.empty());
        Panel.Expect("Coincident profiles: intersect has one simple loop", SameIntersect && SameIntersect.Payload.Loops.size() == 1 && Simple(SameIntersect.Payload));
        Panel.Within("Coincident profiles: intersect area is unchanged", SameIntersect ? std::fabs(SameIntersect.Payload.Area() - 16.0) : 1.0, 1e-12);

        NurbsCurve Shared = NurbsCurve::Rectangle(XY, { 4, 0 }, { 7, 4 }).Payload;
        Deliver<Profile> SharedUnion = ProfileSolver::Combine(A.Payload, Assemble(Shared).Payload, ProfileOperation::Union);
        Panel.Expect("Overlapping boundary segment merges into one simple union loop", SharedUnion && SharedUnion.Payload.Loops.size() == 1 && Simple(SharedUnion.Payload));
        Panel.Within("Shared-edge union area is 28", SharedUnion ? std::fabs(SharedUnion.Payload.Area() - 28.0) : 1.0, 1e-12);
    }

    Panel.Section("Tangent and near-tangent contacts remain discrete and deterministic");
    {
        NurbsCurve Left = NurbsCurve::Circle({ 0, 0, 0 }, Z, 1.0).Payload;
        NurbsCurve Tangent = NurbsCurve::Circle({ 2, 0, 0 }, Z, 1.0).Payload;
        NurbsCurve Apart = NurbsCurve::Circle({ 2.0001, 0, 0 }, Z, 1.0).Payload;
        NurbsCurve Overlap = NurbsCurve::Circle({ 1.9999, 0, 0 }, Z, 1.0).Payload;
        std::vector<CurveCrossing> Contact = ProfileSolver::Intersect(Left, Tangent);
        Panel.Expect("External tangent circles return one tangent contact", Contact.size() == 1 && Contact.front().Tangent);
        Panel.Within("Tangent contact is at (1,0)", Contact.size() == 1 ? Contact.front().Point.Distance({ 1, 0, 0 }) : 1.0, 1e-12);
        Panel.Expect("A 0.0001 gap has no crossing", ProfileSolver::Intersect(Left, Apart).empty());
        std::vector<CurveCrossing> Crossings = ProfileSolver::Intersect(Left, Overlap);
        Panel.Expect("A 0.0001 overlap has two transverse crossings", Crossings.size() == 2 && !Crossings[0].Tangent && !Crossings[1].Tangent);
        Panel.Equal("Near-tangent crossing separation is resolved", Crossings.size() == 2 ? std::fabs(Crossings[0].Point.Y - Crossings[1].Point.Y) : 0.0, 0.01999975, 1e-8);

        Deliver<Profile> PL = Assemble(Left), PT = Assemble(Tangent);
        Deliver<Profile> U = ProfileSolver::Combine(PL.Payload, PT.Payload, ProfileOperation::Union);
        Deliver<Profile> I = ProfileSolver::Combine(PL.Payload, PT.Payload, ProfileOperation::Intersect);
        Deliver<Profile> D = ProfileSolver::Combine(PL.Payload, PT.Payload, ProfileOperation::Subtract);
        Panel.Expect("Tangent union keeps two simple components (not a self-touching loop)", U && U.Payload.Loops.size() == 2 && Simple(U.Payload));
        Panel.Within("Tangent union retains both disc areas", U ? std::fabs(U.Payload.Area() - 2.0 * Pi) : 1.0, 5e-5);
        Panel.Expect("Tangent intersect is empty: point contact has zero area", I && I.Payload.Loops.empty());
        Panel.Expect("Tangent subtract leaves one simple disc", D && D.Payload.Loops.size() == 1 && Simple(D.Payload));
        Panel.Within("Tangent subtract retains one disc area", D ? std::fabs(D.Payload.Area() - Pi) : 1.0, 2e-5);
    }

    Panel.Section("Closed and single-span self-intersecting cubic splines");
    {
        Deliver<NurbsCurve> Bow = NurbsCurve::Interpolate({ { -2, -2, 0 }, { 2, 2, 0 }, { -2, 2, 0 }, { 2, -2, 0 } }, 3, true);
        std::vector<CurveCrossing> BowCrossings = ProfileSolver::SelfIntersections(Bow.Payload);
        Panel.Expect("Closed interpolated bow-tie reports its one self-crossing", Bow && Bow.Payload.Closed() && BowCrossings.size() == 1);
        Panel.Within("Closed interpolated bow-tie crossing is reproduced by both parameters", BowCrossings.size() == 1 ? Bow.Payload.Sample(BowCrossings.front().ParameterA).Distance(Bow.Payload.Sample(BowCrossings.front().ParameterB)) : 1.0, 1e-10);
        Deliver<Profile> Invalid = Assemble(Bow.Payload);
        Panel.Expect("Assemble refuses a self-crossing spline as one material loop", !Invalid && Invalid.Denial.Reason == RefusalReason::SelfIntersecting);
        std::vector<PlanarCell> Lobes = ProfileSolver::Cells({ Bow.Payload }, Z);
        Panel.Expect("Arrangement decomposes the bow-tie into two bounded simple lobes", Lobes.size() == 2 && Lobes[0].Area > 0.0 && Lobes[1].Area > 0.0);

        Deliver<NurbsCurve> OneSpan = NurbsCurve::Bezier({ { 0, 0, 0 }, { 5.069385, 8.004659, 0 }, { -1.019467, 3.008169, 0 }, { 3, 0, 0 } });
        std::vector<CurveCrossing> OneSpanCrossings = ProfileSolver::SelfIntersections(OneSpan.Payload);
        Panel.Expect("Loop contained within one cubic Bézier span is detected", OneSpan && OneSpan.Payload.BezierSegments().size() == 1 && OneSpanCrossings.size() == 1);
        Panel.Within("Single-span crossing is reproduced by both parameters", OneSpanCrossings.size() == 1 ? OneSpan.Payload.Sample(OneSpanCrossings.front().ParameterA).Distance(OneSpan.Payload.Sample(OneSpanCrossings.front().ParameterB)) : 1.0, 1e-10);
        Deliver<NurbsCurve> ShiftedOneSpan = NurbsCurve::Bezier({ { 10000000, 0, 0 }, { 10000005.069385, 8.004659, 0 }, { 9999998.980533, 3.008169, 0 }, { 10000003, 0, 0 } });
        Panel.Expect("Single-span loop detection is invariant under large model translation", ShiftedOneSpan && ProfileSolver::SelfIntersections(ShiftedOneSpan.Payload).size() == 1);
    }

    Panel.Section("Free-form offsets: distance quality and invalid-input refusal");
    {
        Deliver<NurbsCurve> Gentle = NurbsCurve::Interpolate({ { -4, 0, 0 }, { -2, 2, 0 }, { 0, -1, 0 }, { 2, 2, 0 }, { 4, 0, 0 } }, 3, false);
        Deliver<NurbsCurve> LeftOffset = ProfileSolver::Offset(Gentle.Payload, 0.15, Z);
        Deliver<NurbsCurve> RightOffset = ProfileSolver::Offset(Gentle.Payload, -0.15, Z);
        Panel.Expect("Moderate free-form offsets construct as cubic curves", LeftOffset && RightOffset && LeftOffset.Payload.Degree == 3 && RightOffset.Payload.Degree == 3);
        Panel.Expect("Moderate free-form offsets remain simple", LeftOffset && RightOffset && ProfileSolver::SelfIntersections(LeftOffset.Payload).empty() && ProfileSolver::SelfIntersections(RightOffset.Payload).empty());
        Panel.Within("Positive free-form offset stays within 0.0015 of its 0.15 distance", LeftOffset ? OffsetDistanceError(Gentle.Payload, LeftOffset.Payload, 0.15) : 1.0, 1.5e-3);
        Panel.Within("Negative free-form offset stays within 0.0015 of its 0.15 distance", RightOffset ? OffsetDistanceError(Gentle.Payload, RightOffset.Payload, -0.15) : 1.0, 1.5e-3);

        Deliver<NurbsCurve> Ellipse = NurbsCurve::Ellipse({}, Z, Vec3::UnitX(), 3.0, 2.0);
        Deliver<NurbsCurve> EllipseOffset = ProfileSolver::Offset(Ellipse.Payload, 0.2, Z);
        Panel.Expect("Ellipse offset stays a closed free-form result, not osculating circular arcs", EllipseOffset && EllipseOffset.Payload.Closed() && EllipseOffset.Payload.Degree == 3 && !EllipseOffset.Payload.Rational());
        Panel.Within("Ellipse offset stays within 0.001 of its 0.2 normal distance", EllipseOffset ? OffsetDistanceError(Ellipse.Payload, EllipseOffset.Payload, 0.2) : 1.0, 1e-3);
        Panel.Expect("Ellipse offset remains simple", EllipseOffset && ProfileSolver::SelfIntersections(EllipseOffset.Payload).empty());

        Deliver<NurbsCurve> Bow = NurbsCurve::Interpolate({ { -2, -2, 0 }, { 2, 2, 0 }, { -2, 2, 0 }, { 2, -2, 0 } }, 3, true);
        Deliver<NurbsCurve> RefusedBow = ProfileSolver::Offset(Bow.Payload, 0.2, Z);
        Panel.Expect("Offset refuses a closed self-intersecting spline", !RefusedBow && RefusedBow.Denial.Reason == RefusalReason::SelfIntersecting);
        Deliver<NurbsCurve> Tight = NurbsCurve::Interpolate({ { -2, 0, 0 }, { -1, 3, 0 }, { 0, -3, 0 }, { 1, 3, 0 }, { 2, 0, 0 } }, 3, false);
        Deliver<NurbsCurve> RefusedCusp = ProfileSolver::Offset(Tight.Payload, 0.8, Z);
        Panel.Expect("Offset refuses a free-form curvature cusp before producing a folded curve", !RefusedCusp && RefusedCusp.Denial.Reason == RefusalReason::DegenerateInput);
        Deliver<NurbsCurve> OneSpan = NurbsCurve::Bezier({ { 0, 0, 0 }, { 5.069385, 8.004659, 0 }, { -1.019467, 3.008169, 0 }, { 3, 0, 0 } });
        Deliver<NurbsCurve> RefusedOneSpan = ProfileSolver::Offset(OneSpan.Payload, 0.1, Z);
        Panel.Expect("Offset also refuses a loop wholly inside one cubic span", !RefusedOneSpan && RefusedOneSpan.Denial.Reason == RefusalReason::SelfIntersecting);
    }

    Panel.Section("Free-form NURBS Boolean stress: overlapping cubic closed splines");
    {
        Deliver<NurbsCurve> ACurve = NurbsCurve::Interpolate({ { -2, 0, 0 }, { 0, 2, 0 }, { 2, 0, 0 }, { 0, -2, 0 } }, 3, true);
        Deliver<NurbsCurve> BCurve = NurbsCurve::Interpolate({ { -0.5, 0, 0 }, { 1.5, 2, 0 }, { 3.5, 0, 0 }, { 1.5, -2, 0 } }, 3, true);
        Deliver<Profile> A = Assemble(ACurve.Payload), B = Assemble(BCurve.Payload);
        Deliver<Profile> U = ProfileSolver::Combine(A.Payload, B.Payload, ProfileOperation::Union);
        Deliver<Profile> D = ProfileSolver::Combine(A.Payload, B.Payload, ProfileOperation::Subtract);
        Deliver<Profile> I = ProfileSolver::Combine(A.Payload, B.Payload, ProfileOperation::Intersect);
        Panel.Expect("Overlapping cubic closed splines produce one union, difference and common loop", U && D && I && U.Payload.Loops.size() == 1 && D.Payload.Loops.size() == 1 && I.Payload.Loops.size() == 1);
        Panel.Expect("All free-form Boolean result loops remain simple", U && D && I && Simple(U.Payload) && Simple(D.Payload) && Simple(I.Payload));
        Panel.Expect("Boolean result retains cubic non-rational curve pieces", U && !U.Payload.Loops.front().Curve.Rational() && U.Payload.Loops.front().Curve.Degree == 3);
        Panel.Within("Free-form Boolean area identity: union + common = A + B", U && I ? std::fabs(U.Payload.Area() + I.Payload.Area() - std::fabs(A.Payload.Area()) - std::fabs(B.Payload.Area())) : 1.0, 5e-5);
        Panel.Within("Free-form Boolean area identity: difference + common = A", D && I ? std::fabs(D.Payload.Area() + I.Payload.Area() - std::fabs(A.Payload.Area())) : 1.0, 5e-5);
    }

    return Panel.Conclude();
}
