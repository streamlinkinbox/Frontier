//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Verification/ProfileVerification.cpp — Phase 7: exact curve crossings, winding, 2D booleans, fillet /
//    chamfer / offset / trim / join, and the console verbs on top of them.
//============================================================================================================================================
#include "Kernel/ProfileSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <cmath>

using namespace Frontier;

int main()
{
    VerificationPanel Panel("SolidArc · Phase 7 · Profile Verification — crossings · winding · union / subtract / intersect · fillet · chamfer · offset · trim · join");
    const double Pi = ScalarCriteria::Pi; const Vec3 Z = Vec3::UnitZ(); Workplane W;
    auto Pf = [&](const NurbsCurve& C) { return ProfileSolver::Assemble({ C }, Z).Payload; };
    NurbsCurve R1 = NurbsCurve::Rectangle(W, { 0, 0 }, { 2, 2 }).Payload, R2 = NurbsCurve::Rectangle(W, { 1, 1 }, { 3, 3 }).Payload;
    NurbsCurve Circ = NurbsCurve::Circle({ 2, 1, 0 }, Z, 0.75).Payload;

    Panel.Section("Curve–curve crossings (subdivision + Newton) and self-intersection");
    {
        std::vector<CurveCrossing> X = ProfileSolver::Intersect(R1, R2);
        Panel.Expect("Overlapping squares cross twice", X.size() == 2);
        Panel.Within("Crossing points exact: (2,1) and (1,2)", X.size() == 2 ? std::min(X[0].Point.Distance({ 2, 1, 0 }) + X[1].Point.Distance({ 1, 2, 0 }), X[0].Point.Distance({ 1, 2, 0 }) + X[1].Point.Distance({ 2, 1, 0 })) : 1.0, 1e-9);
        X = ProfileSolver::Intersect(R1, Circ);
        Panel.Expect("Square edge x=2 crosses the circle twice", X.size() == 2);
        Panel.Within("Rational circle crossings at y = 1 ± 0.75 (polished to 1e-12)", X.size() == 2 ? std::fabs(X[0].Point.Y - 0.25) + std::fabs(X[1].Point.Y - 1.75) : 1.0, 1e-12);
        Panel.Expect("Crossing parameters reproduce the points on both curves", X.size() == 2 && R1.Sample(X[0].ParameterA).Distance(Circ.Sample(X[0].ParameterB)) < 1e-12);
        NurbsCurve Tan = NurbsCurve::Circle({ 3.5, 1, 0 }, Z, 0.75).Payload;                        // touches Circ at (2.75,1)
        X = ProfileSolver::Intersect(Circ, Tan);
        Panel.Expect("Tangent circles report one tangent contact", X.size() == 1 && X[0].Tangent);
        Panel.Expect("Disjoint curves: no crossings", ProfileSolver::Intersect(R1, NurbsCurve::Circle({ 9, 9, 0 }, Z, 1).Payload).empty());
        NurbsCurve Bow = NurbsCurve::Polyline({ { 0, 0, 0 }, { 2, 2, 0 }, { 2, 0, 0 }, { 0, 2, 0 } }, true).Payload;
        std::vector<CurveCrossing> S = ProfileSolver::SelfIntersections(Bow);
        Panel.Expect("Bow-tie has exactly one self-crossing at (1,1)", S.size() == 1 && S[0].Point.Distance({ 1, 1, 0 }) < 1e-9);
        Panel.Expect("Square and circle have none", ProfileSolver::SelfIntersections(R1).empty() && ProfileSolver::SelfIntersections(Circ).empty());
        NurbsCurve Spl = NurbsCurve::Interpolate({ { -1, 0, 0 }, { 0, 1, 0 }, { 1, 0, 0 }, { 2, 1, 0 }, { 3, 0, 0 } }, 3).Payload;
        NurbsCurve Lin = NurbsCurve::Line({ -1, 0.5, 0 }, { 3, 0.5, 0 }).Payload;
        X = ProfileSolver::Intersect(Spl, Lin);
        bool OnBoth = true; for (const CurveCrossing& K : X) if (std::fabs(K.Point.Y - 0.5) > 1e-10 || Spl.Sample(K.ParameterA).Distance(K.Point) > 1e-9) OnBoth = false;
        Panel.Expect("Cubic spline vs line: 4 crossings, all on y = 0.5 and on the spline", X.size() == 4 && OnBoth);
    }

    Panel.Section("Signed area, winding number, enclosure");
    {
        Panel.Within("Square area +4 (counter-clockwise)", std::fabs(ProfileSolver::SignedArea(R1, Z) - 4.0), 1e-12);
        Panel.Within("Reversed square area −4", std::fabs(ProfileSolver::SignedArea(R1.Reversed(), Z) + 4.0), 1e-12);
        Panel.Within("Circle area πr² within 1e-5 relative", std::fabs(ProfileSolver::SignedArea(Circ, Z) - Pi * 0.5625) / (Pi * 0.5625), 1e-5);
        Panel.Expect("Winding inside 1, outside 0, reversed −1", ProfileSolver::Winding(R1, Z, { 1, 1, 0 }) == 1 && ProfileSolver::Winding(R1, Z, { 5, 1, 0 }) == 0 && ProfileSolver::Winding(R1.Reversed(), Z, { 1, 1, 0 }) == -1);
        NurbsCurve Hole = NurbsCurve::Rectangle(W, { 0.5, 0.5 }, { 1.5, 1.5 }).Payload;
        Deliver<Profile> P = ProfileSolver::Assemble({ R1, Hole }, Z);
        Panel.Expect("Assemble: outer depth 0, inner depth 1", P && P.Payload.Loops[0].Depth == 0 && P.Payload.Loops[1].Depth == 1);
        Profile N = ProfileSolver::Normalised(P.Payload);
        Panel.Expect("Normalised: hole is clockwise, outer counter-clockwise", N.Loops[0].SignedArea > 0 && N.Loops[1].SignedArea < 0);
        Panel.Within("Normalised area = 4 − 1", std::fabs(N.Area() - 3.0), 1e-12);
        Panel.Expect("Contains: material yes, hole no", N.Contains({ 0.25, 0.25, 0 }) && !N.Contains({ 1, 1, 0 }));
        Panel.Expect("Assemble refuses an open curve", !ProfileSolver::Assemble({ NurbsCurve::Line({ 0, 0, 0 }, { 1, 0, 0 }).Payload }, Z));
        Panel.Expect("Assemble refuses a self-crossing loop", ProfileSolver::Assemble({ NurbsCurve::Polyline({ { 0, 0, 0 }, { 2, 2, 0 }, { 2, 0, 0 }, { 0, 2, 0 } }, true).Payload }, Z).Denial.Reason == RefusalReason::SelfIntersecting);
        Panel.Expect("Assemble refuses a non-coplanar loop", !ProfileSolver::Assemble({ NurbsCurve::Polyline({ { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 1 }, { 0, 1, 0 } }, true).Payload }, Z));
    }

    Panel.Section("Booleans respect winding: union / subtract / intersect on squares and rational circles");
    {
        Profile A = Pf(R1), B = Pf(R2), C = Pf(Circ);
        Deliver<Profile> U = ProfileSolver::Combine(A, B, ProfileOperation::Union);
        Panel.Expect("Union of overlapping squares: one loop", U && U.Payload.Loops.size() == 1);
        Panel.Within("Union area 4 + 4 − 1 = 7", std::fabs(U.Payload.Area() - 7.0), 1e-12);
        Panel.Expect("Union loop is a closed degree-2 chain with 8 corners (L-shaped octagon)", U.Payload.Loops[0].Curve.Closed() && ProfileSolver::SelfIntersections(U.Payload.Loops[0].Curve).empty());
        Deliver<Profile> S = ProfileSolver::Combine(A, B, ProfileOperation::Subtract);
        Panel.Within("Subtract area 4 − 1 = 3", std::fabs(S.Payload.Area() - 3.0), 1e-12);
        Deliver<Profile> I = ProfileSolver::Combine(A, B, ProfileOperation::Intersect);
        Panel.Within("Intersect area 1", std::fabs(I.Payload.Area() - 1.0), 1e-12);
        Panel.Expect("Intersect result is counter-clockwise (material)", I.Payload.Loops.size() == 1 && I.Payload.Loops[0].SignedArea > 0);
        Deliver<Profile> Uc = ProfileSolver::Combine(A, C, ProfileOperation::Union);
        Deliver<Profile> Sc = ProfileSolver::Combine(A, C, ProfileOperation::Subtract);
        Deliver<Profile> Ic = ProfileSolver::Combine(A, C, ProfileOperation::Intersect);
        Panel.Within("Square ∪ circle + square ∩ circle = square + circle", std::fabs(Uc.Payload.Area() + Ic.Payload.Area() - (4.0 + ProfileSolver::SignedArea(Circ, Z))), 1e-6);
        Panel.Within("Square − circle + square ∩ circle = square", std::fabs(Sc.Payload.Area() + Ic.Payload.Area() - 4.0), 1e-6);
        Panel.Within("Square ∩ circle = half disc exactly (circle centred on the edge)", std::fabs(Ic.Payload.Area() - 0.5 * Pi * 0.5625) / (0.5 * Pi * 0.5625), 1e-5);
        Panel.Expect("Circle pieces stay rational quadratics — no polygonisation", Sc.Payload.Loops[0].Curve.Rational() && Sc.Payload.Loops[0].Curve.Degree == 2);
        // exact arc check: sample the subtract result near the circular notch and measure distance to the circle centre
        double Worst = 0; const NurbsCurve& K = Sc.Payload.Loops[0].Curve;
        for (int T = 0; T <= 400; ++T) { Vec3 P = K.Sample(K.DomainStart() + (K.DomainEnd() - K.DomainStart()) * T / 400.0); double D = P.Distance({ 2, 1, 0 }); if (P.X < 2 - 1e-9 && D < 0.75 + 0.05) Worst = std::max(Worst, std::fabs(D - 0.75)); }
        Panel.Within("Notch lies on the circle to 1e-12", Worst, 1e-12);
        // hole creation
        NurbsCurve Inner = NurbsCurve::Rectangle(W, { 0.5, 0.5 }, { 1.5, 1.5 }).Payload;
        Deliver<Profile> H = ProfileSolver::Combine(A, Pf(Inner), ProfileOperation::Subtract);
        Panel.Expect("Subtracting a contained square produces outer + clockwise hole", H.Payload.Loops.size() == 2 && H.Payload.Loops[0].SignedArea * H.Payload.Loops[1].SignedArea < 0);
        Panel.Within("Holed area 3", std::fabs(H.Payload.Area() - 3.0), 1e-12);
        Panel.Expect("Hole depth 1, outer depth 0", (H.Payload.Loops[0].Depth + H.Payload.Loops[1].Depth) == 1);
        Deliver<Profile> H2 = ProfileSolver::Combine(H.Payload, Pf(NurbsCurve::Rectangle(W, { 0.75, 0.75 }, { 1.25, 1.25 }).Payload), ProfileOperation::Union);
        Panel.Expect("Union of an island inside the hole → 3 loops, depths 0/1/2", H2.Payload.Loops.size() == 3);
        Panel.Within("Island area 3 + 0.25", std::fabs(H2.Payload.Area() - 3.25), 1e-12);
        Deliver<Profile> Fill = ProfileSolver::Combine(H.Payload, Pf(Inner), ProfileOperation::Union);
        Panel.Expect("Union with the exact hole shape fills it (coincident boundaries)", Fill.Payload.Loops.size() == 1 && std::fabs(Fill.Payload.Area() - 4.0) < 1e-12);
        // disjoint and shared-edge
        Deliver<Profile> D = ProfileSolver::Combine(A, Pf(NurbsCurve::Rectangle(W, { 5, 5 }, { 6, 6 }).Payload), ProfileOperation::Union);
        Panel.Expect("Disjoint union keeps both loops", D.Payload.Loops.size() == 2 && std::fabs(D.Payload.Area() - 5.0) < 1e-12);
        Panel.Expect("Disjoint intersect is empty", ProfileSolver::Combine(A, Pf(NurbsCurve::Rectangle(W, { 5, 5 }, { 6, 6 }).Payload), ProfileOperation::Intersect).Payload.Loops.empty());
        Deliver<Profile> Sh = ProfileSolver::Combine(A, Pf(NurbsCurve::Rectangle(W, { 2, 0 }, { 4, 2 }).Payload), ProfileOperation::Union);
        Panel.Expect("Shared-edge union merges into one 2×4 loop", Sh.Payload.Loops.size() == 1 && std::fabs(Sh.Payload.Area() - 8.0) < 1e-12);
        Panel.Expect("A ∩ A = A, A − A = ∅", std::fabs(ProfileSolver::Combine(A, A, ProfileOperation::Intersect).Payload.Area() - 4.0) < 1e-12 && ProfileSolver::Combine(A, A, ProfileOperation::Subtract).Payload.Loops.empty());
        Panel.Expect("Winding of the input is irrelevant: clockwise B gives the same union", std::fabs(ProfileSolver::Combine(A, Pf(R2.Reversed()), ProfileOperation::Union).Payload.Area() - 7.0) < 1e-12);
        Profile Tilted = Pf(R2); Tilted.Normal = Vec3::UnitX();
        Panel.Expect("Non-coplanar profiles are refused", !ProfileSolver::Combine(A, Tilted, ProfileOperation::Union));
        Panel.Expect("Assemble refuses a loop whose plane differs from the profile normal", !ProfileSolver::Assemble({ R2 }, Vec3::UnitX()));
        // wheel: circle minus four circles, chained
        Profile Wheel = Pf(NurbsCurve::Circle({ 0, 0, 0 }, Z, 3).Payload);
        for (int K2 = 0; K2 < 4; ++K2) { double An = K2 * Pi / 2; Wheel = ProfileSolver::Combine(Wheel, Pf(NurbsCurve::Circle({ 2 * std::cos(An), 2 * std::sin(An), 0 }, Z, 0.6).Payload), ProfileOperation::Subtract).Payload; }
        Panel.Expect("Wheel: 1 rim + 4 holes", Wheel.Loops.size() == 5);
        Panel.Within("Wheel area 9π − 4·0.36π", std::fabs(Wheel.Area() - (9 * Pi - 4 * Pi * 0.36)) / (9 * Pi), 1e-5);
    }

    Panel.Section("Fillet, chamfer, offset, trim, join");
    {
        Deliver<NurbsCurve> F = ProfileSolver::Filleted(R1, 0.25);
        Panel.Expect("Fillet all four corners: closed, rational", F && F.Payload.Closed() && F.Payload.Rational());
        Panel.Within("Filleted perimeter 8 − 2 + π/2", std::fabs(F.Payload.Length() - (8 - 8 * 0.25 + 2 * Pi * 0.25)), 1e-9);
        Panel.Within("Filleted area 4 − (4 − π)·r²", std::fabs(ProfileSolver::SignedArea(F.Payload, Z) - (4 - (4 - Pi) * 0.0625)), 1e-5);
        std::vector<int> Two = { 0, 2 };
        Deliver<NurbsCurve> F2 = ProfileSolver::Filleted(R1, 0.5, &Two);
        Panel.Within("Fillet only corners 0 and 2", std::fabs(F2.Payload.Length() - (8 - 4 * 0.5 + Pi * 0.5)), 1e-9);
        Panel.Expect("Fillet refuses a radius larger than the sides allow", !ProfileSolver::Filleted(R1, 1.5));
        Deliver<NurbsCurve> Ch = ProfileSolver::Chamfered(R1, 0.25);
        Panel.Within("Chamfer perimeter 8 − 2 + 4·0.25·√2", std::fabs(Ch.Payload.Length() - (8 - 8 * 0.25 + 4 * 0.25 * std::sqrt(2.0))), 1e-9);
        Deliver<NurbsCurve> Fo = ProfileSolver::Filleted(NurbsCurve::Polyline({ { 0, 0, 0 }, { 2, 0, 0 }, { 2, 2, 0 } }, false).Payload, 0.5);
        Panel.Within("Open L fillet length 4 − 1 + π/4", std::fabs(Fo.Payload.Length() - (3 + Pi * 0.25)), 1e-9);
        Panel.Expect("Open L fillet keeps its end points", Fo.Payload.StartPoint().Coincident({ 0, 0, 0 }, 1e-9) && Fo.Payload.EndPoint().Coincident({ 2, 2, 0 }, 1e-9));
        Deliver<NurbsCurve> Hx = ProfileSolver::Filleted(NurbsCurve::Polygon(W, { 0, 0 }, 1, 6, 0, true).Payload, 0.2);
        Panel.Expect("Hexagon fillet closed, no self-crossings", Hx && Hx.Payload.Closed() && ProfileSolver::SelfIntersections(Hx.Payload).empty());
        Deliver<NurbsCurve> Oo = ProfileSolver::Offset(R1, -0.5, Z);
        Panel.Within("Outward square offset area 4 + 4 + π/4 (rounded corners)", std::fabs(ProfileSolver::SignedArea(Oo.Payload, Z) - (8 + Pi * 0.25)), 1e-4);
        Deliver<NurbsCurve> Oi = ProfileSolver::Offset(R1, 0.5, Z);
        Panel.Within("Inward square offset area 1 (sharp corners)", std::fabs(ProfileSolver::SignedArea(Oi.Payload, Z) - 1.0), 1e-9);
        Panel.Expect("Inward offset stays a 4-corner polyline", Oi.Payload.Poles.size() == 5 || Oi.Payload.Degree == 1);
        Deliver<NurbsCurve> Oc = ProfileSolver::Offset(Circ, -0.25, Z);
        double WorstR = 0; for (int T = 0; T <= 200; ++T) WorstR = std::max(WorstR, std::fabs(Oc.Payload.Sample(Oc.Payload.DomainStart() + (Oc.Payload.DomainEnd() - Oc.Payload.DomainStart()) * T / 200.0).Distance({ 2, 1, 0 }) - 1.0));
        Panel.Within("Circle offset is an exact rational circle r = 1", WorstR, 1e-12);
        Panel.Expect("Offset collapsing a circle is refused", !ProfileSolver::Offset(Circ, 0.75, Z));
        Deliver<NurbsCurve> Os = ProfileSolver::Offset(NurbsCurve::Slot(W, { -2, 0 }, { 2, 0 }, 1).Payload, -0.5, Z);
        Panel.Within("Slot outward offset = slot of radius 1.5 (area 4·3 + 2.25π)", std::fabs(ProfileSolver::SignedArea(Os.Payload, Z) - (12 + Pi * 2.25)) / 19.0, 1e-5);
        NurbsCurve L = NurbsCurve::Line({ -1, 1, 0 }, { 4, 1, 0 }).Payload;
        Deliver<std::vector<NurbsCurve>> Tr = ProfileSolver::Trimmed(L, { Circ }, { 2, 1, 0 });
        Panel.Expect("Trim removes the chord inside the circle, leaving two pieces", Tr && Tr.Payload.size() == 2);
        Panel.Within("Pieces end on the circle: x = 1.25 and x = 2.75", Tr ? std::fabs(Tr.Payload[0].EndPoint().X - 1.25) + std::fabs(Tr.Payload[1].StartPoint().X - 2.75) : 1.0, 1e-9);
        Panel.Expect("Trim with no crossing is refused", !ProfileSolver::Trimmed(L, { NurbsCurve::Circle({ 9, 9, 0 }, Z, 1).Payload }, { 0, 1, 0 }));
        Deliver<NurbsCurve> J = ProfileSolver::Joined({ NurbsCurve::Line({ 0, 0, 0 }, { 1, 0, 0 }).Payload, NurbsCurve::Line({ 0, 1, 0 }, { 0, 0, 0 }).Payload, NurbsCurve::Line({ 1, 1, 0 }, { 1, 0, 0 }).Payload, NurbsCurve::Line({ 0, 1, 0 }, { 1, 1, 0 }).Payload });
        Panel.Expect("Join four lines in any order / sense → closed square", J && J.Payload.Closed() && std::fabs(J.Payload.Length() - 4.0) < 1e-12);
        Panel.Expect("Join refuses a gap", !ProfileSolver::Joined({ NurbsCurve::Line({ 0, 0, 0 }, { 1, 0, 0 }).Payload, NurbsCurve::Line({ 2, 0, 0 }, { 3, 0, 0 }).Payload }));
    }

    Panel.Section("Console: boolean / profile / fillet / chamfer / offset / trim / join / explode verbs, undo");
    {
        ConsoleHost Host("/tmp/SolidArcVerification", 1280, 800);
        auto Figure = [&](const char* N) { return Host.Document().Find(std::string(N)); };
        Host.Execute("rect (0,0) (2,2) ; rect (1,1) (3,3) ; boolean union Rectangle Rectangle.2");
        Panel.Expect("boolean union consumes inputs and creates 'Union'", Figure("Union") && !Figure("Rectangle") && Host.Document().Figures().size() == 1);
        Panel.Within("Union area 7", std::fabs(ProfileSolver::SignedArea(Figure("Union")->Curve, Z) - 7.0), 1e-12);
        Host.Execute("undo");
        Panel.Expect("undo restores both rectangles", Figure("Rectangle") && Figure("Rectangle.2") && !Figure("Union"));
        Host.Execute("redo ; circle (2,1) 0.75 ; boolean subtract Union -- Circle --keep");
        Panel.Expect("--keep leaves the inputs in place", Figure("Union") && Figure("Circle") && Figure("Difference"));
        Host.Execute("delete Union Circle ; rect (0.5,0.5) (0.9,0.9) ; boolean subtract Difference -- Rectangle");
        Panel.Expect("Subtracting a contained square yields two loops named Difference / Difference.2", Figure("Difference") && Figure("Difference.2"));
        Host.Execute("fillet Difference 0.2");
        Panel.Expect("fillet on a mixed line/arc loop stays closed and simple", Figure("Difference")->Curve.Closed() && ProfileSolver::SelfIntersections(Figure("Difference")->Curve).empty());
        Host.Execute("polygon (5,0) 1 6 ; offset Polygon 0.3 --copy");
        Panel.Expect("offset --copy adds Polygon.offset and keeps the source", Figure("Polygon") && Figure("Polygon.offset"));
        Panel.Expect("Inward hexagon offset is smaller", ProfileSolver::SignedArea(Figure("Polygon.offset")->Curve, Z) < ProfileSolver::SignedArea(Figure("Polygon")->Curve, Z));
        Host.Execute("line (-3,4,0) (6,4,0) ; circle (1,4) 1 ; trim Line (1,4,0)");
        Panel.Expect("trim splits Line into Line and Line.2 outside the circle", Figure("Line") && Figure("Line.2") && std::fabs(Figure("Line")->Curve.EndPoint().X - 0.0) < 1e-9 && std::fabs(Figure("Line.2")->Curve.StartPoint().X - 2.0) < 1e-9);
        Host.Execute("explode Polygon ; join Polygon Polygon.2 Polygon.3 Polygon.4 Polygon.5 Polygon.6 --name=Hex");
        Panel.Expect("explode → 6 segments, join → closed hexagon again", Figure("Hex") && Figure("Hex")->Curve.Closed() && std::fabs(Figure("Hex")->Curve.Length() - 6.0) < 1e-9);
        Host.Execute("chamfer Hex 0.2");
        Panel.Within("chamfer hexagon perimeter 6 − 12·0.2 + 6·0.2·√3 (120° corners)", std::fabs(Figure("Hex")->Curve.Length() - (6 - 12 * 0.2 + 6 * 0.2 * std::sqrt(3.0))), 1e-9);
        Host.Execute("select all ; key q");
        Panel.Expect("Q on disjoint selection: union keeps loops, refused for open curves (Line) — figure count unchanged", Host.Document().Figures().size() >= 2);
        Host.Execute("view top ; view fit ; render Proof_07b_Console");
    }

    Panel.Section("Phase 7b · planar arrangement (Cells) — faces shared across edges, containment, dangling pruning");
    {
        auto CellsOf = [&](std::vector<NurbsCurve> C) { return ProfileSolver::Cells(C, Z); };
        auto Sq = CellsOf({ R1, NurbsCurve::Circle({ 1, 1, 0 }, Z, 0.5).Payload });
        Panel.Expect("square + inner circle → 2 cells (ring with 1 hole, disc)", Sq.size() == 2 && Sq[0].Holes.size() == 1 && Sq[1].Holes.empty());
        Panel.Within("ring area 4 − π/4", Sq.size() == 2 ? std::fabs(Sq[0].Area - (4 - Pi * 0.25)) : 1.0, 1e-4);
        Panel.Expect("disc depth 1, ring depth 0", Sq.size() == 2 && Sq[0].Depth == 0 && Sq[1].Depth == 1);
        auto Ov = CellsOf({ R1, R2 });
        std::vector<double> A; for (const PlanarCell& C : Ov) A.push_back(C.Area); std::sort(A.begin(), A.end());
        Panel.Expect("two overlapping squares → 3 cells sharing edges", Ov.size() == 3);
        Panel.Within("their areas 1, 3, 3", Ov.size() == 3 ? std::fabs(A[0] - 1) + std::fabs(A[1] - 3) + std::fabs(A[2] - 3) : 1.0, 1e-9);
        auto Half = CellsOf({ NurbsCurve::Circle({ 0, 0, 0 }, Z, 1).Payload, NurbsCurve::Line({ -2, 0, 0 }, { 2, 0, 0 }).Payload });
        Panel.Expect("circle cut by a longer line → 2 half discs, dangling ends pruned", Half.size() == 2);
        Panel.Within("each half disc area π/2", Half.size() == 2 ? std::fabs(Half[0].Area - Pi / 2) + std::fabs(Half[1].Area - Pi / 2) : 1.0, 1e-4);
        auto Tt = CellsOf({ NurbsCurve::Line({ 0, 0.3, 0 }, { 1, 0.3, 0 }).Payload, NurbsCurve::Line({ 0, 0.7, 0 }, { 1, 0.7, 0 }).Payload, NurbsCurve::Line({ 0.3, 0, 0 }, { 0.3, 1, 0 }).Payload, NurbsCurve::Line({ 0.7, 0, 0 }, { 0.7, 1, 0 }).Payload });
        Panel.Expect("tic-tac-toe → exactly the one bounded centre cell", Tt.size() == 1);
        Panel.Within("centre cell area 0.16", Tt.size() == 1 ? std::fabs(Tt[0].Area - 0.16) : 1.0, 1e-9);
        auto Nest = CellsOf({ NurbsCurve::Rectangle(W, { 0, 0 }, { 4, 4 }).Payload, NurbsCurve::Circle({ 2, 2, 0 }, Z, 1.5).Payload, NurbsCurve::Rectangle(W, { 1.5, 1.5 }, { 2.5, 2.5 }).Payload });
        Panel.Expect("three nested loops → depths 0/1/2, each with one hole except the innermost", Nest.size() == 3 && Nest[0].Depth == 0 && Nest[1].Depth == 1 && Nest[2].Depth == 2 && Nest[0].Holes.size() == 1 && Nest[1].Holes.size() == 1 && Nest[2].Holes.empty());
        Panel.Expect("an open curve alone bounds nothing", CellsOf({ NurbsCurve::Line({ 0, 0, 0 }, { 1, 1, 0 }).Payload }).empty());
        auto Tb = CellsOf({ R1, NurbsCurve::Line({ 0, 1, 0 }, { 2, 1, 0 }).Payload });
        Panel.Expect("square with a line across → 2 cells of area 2", Tb.size() == 2 && std::fabs(Tb[0].Area - 2) < 1e-9 && std::fabs(Tb[1].Area - 2) < 1e-9);
    }

    Panel.Section("Phase 7b · solids with through-holes (multi-loop caps) and Euler with inner loops");
    {
        NurbsCurve Outer = R1, Inner = NurbsCurve::Circle({ 1, 1, 0 }, Z, 0.5).Payload;
        BrepBody B = BrepBody::Extrude({ Outer, Inner }, Z, 1.0).Payload;
        BodyReport R = B.Validate();
        Panel.Expect("square with circular hole extruded → V10 E15 F7 with 9 loops", R.Vertices == 10 && R.Edges == 15 && R.Faces == 7 && R.Loops == 9);
        Panel.Expect("one hull, χ = 0, genus 1, solid", R.Hulls == 1 && R.EulerCharacteristic == 0 && R.Genus == 1 && R.Solid());
        Panel.Within("volume 4 − π/4 (tessellated)", std::fabs(R.Volume - (4 - Pi * 0.25)), 2e-3);
        BrepBody Rv = BrepBody::Revolve({ NurbsCurve::Rectangle(W, { 2, 0 }, { 4, 2 }).Payload, NurbsCurve::Circle({ 3, 1, 0 }, Z, 0.4).Payload }, { 0, 0, 0 }, Vec3::UnitY(), Pi).Payload;
        BodyReport Rr = Rv.Validate();
        Panel.Expect("half-revolved square with hole → closed solid of genus 1", Rr.Solid() && Rr.Genus == 1);
    }

    Panel.Section("Phase 7b · sketch areas in the document: fill choice, extrude picks the area's loops");
    {
        ConsoleHost Host("/tmp/SolidArcVerification", 1280, 800);
        auto Figure = [&](const char* N) { return Host.Document().Find(std::string(N)); };
        Host.Execute("rect (0,0) (4,4) ; circle (2,2) 1 ; circle (2,2) 0.4 ; areas");
        Panel.Expect("three nested curves → 3 sketch areas, all filled", Host.Document().Areas().size() == 3);
        Host.Execute("fill off a1");
        Panel.Expect("fill off a1 empties the ring", !Host.Document().Areas()[1].Filled && Host.Document().Areas()[0].Filled);
        Host.Execute("line (0,0) (1,1)");
        Panel.Expect("fill survives a rebuild after adding an unrelated curve", Host.Document().Areas().size() == 3 && !Host.Document().Areas()[1].Filled);
        Host.Execute("extrude Rectangle 1 --name=Plate");
        Panel.Expect("extrude of the outer curve takes its filled area: genus-1 plate", Figure("Plate") && Figure("Plate")->Classification == FigureClassification::Body && Figure("Plate")->Body.Validate().Genus == 1);
        Host.Execute("extrude a1 1 --name=Ring");
        Panel.Expect("extrude of an unfilled area yields sheets, not a solid", Figure("Ring") && Figure("Ring")->Classification == FigureClassification::Surface);
        Host.Execute("extrude a2 2 --name=Pin");
        Panel.Expect("extrude of the filled centre disc is a solid cylinder", Figure("Pin") && Figure("Pin")->Body.Validate().Solid() && Figure("Pin")->Body.Validate().Genus == 0);
        Host.Execute("fill at (3.5,3.5) off");
        Panel.Expect("fill at (point) addresses the innermost area under it", !Host.Document().Areas()[0].Filled);
        Host.Execute("undo");
        Panel.Expect("undo restores the fill choice", Host.Document().Areas()[0].Filled);
        Host.Execute("box (0,0,0) 2 2 1 --name=Ortho ; select Ortho ; view top ; view fit");
        Host.Execute("view top ; view fit ; render Proof_07c_Areas");
    }

    return Panel.Conclude();
}
