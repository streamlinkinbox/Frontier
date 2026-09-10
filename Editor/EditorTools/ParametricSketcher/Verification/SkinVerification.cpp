//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Verification/SkinVerification.cpp — Phase 8: loft · sweep · pipe · Coons / N-sided patch · derived figures
//============================================================================================================================================
#include "Kernel/SkinSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <cmath>

using namespace Frontier;

int main()
{
    VerificationPanel Panel("SolidArc · Phase 8 · Skin Verification — loft (box→circle, holes, loop) · sweep frames · pipe · Coons · N-sided · recipes follow their sources");
    const double Pi = ScalarCriteria::Pi; const Vec3 Z = Vec3::UnitZ(); Workplane W;
    auto Report = [](const Deliver<SkinSolver::Skin>& S) { return S && S.Payload.IsBody ? S.Payload.Body.Validate() : BodyReport{}; };

    Panel.Section("Loft: harmonised sections (sense, seam, degree, knots) and closed-section solids");
    {
        NurbsCurve Sq = NurbsCurve::Rectangle(W, { -1, -1 }, { 1, 1 }).Payload, Ci = NurbsCurve::Circle({ 0, 0, 2 }, Z, 1).Payload;
        LoftOptions L;
        auto A = SkinSolver::Loft({ Sq, Ci }, L); BodyReport R = Report(A);
        Panel.Expect("square → circle lofts to a closed solid (V2 E3 F3, χ 2)", A && R.Solid() && R.Faces == 3 && R.EulerCharacteristic == 2);
        Panel.Within("volume between square (4) and disc (π): 7.17 for the ruled two-section loft", std::fabs(R.Volume - 7.1702), 2e-3);
        auto B = SkinSolver::Loft({ Sq, Ci.Reversed() }, L); BodyReport RB = Report(B);
        Panel.Within("a reversed circle gives the same solid (sense aligned)", std::fabs(RB.Volume - R.Volume), 1e-6);
        NurbsCurve Rot = SkinSolver::SeamAt(Ci, 0.6);
        Panel.Expect("SeamAt keeps the circle closed and exact", Rot.Closed() && std::fabs(Rot.Length() - 2 * Pi) < 1e-9 && Rot.StartPoint().Coincident(Ci.Sample(0.6), 1e-9));
        auto Cc = SkinSolver::Loft({ Sq, Rot }, L); BodyReport RC = Report(Cc);
        Panel.Within("a circle with its seam moved gives the same solid (seam aligned to least twist)", std::fabs(RC.Volume - R.Volume), 1e-3);
        auto Three = SkinSolver::Loft({ Sq, Ci, NurbsCurve::Circle({ 0, 0, 4 }, Z, 0.5).Payload }, L);
        Panel.Expect("three sections → cubic-across solid", Three && Report(Three).Solid() && Three.Payload.Body.Faces[0].Surface.DegreeV == 2);
        LoftOptions Sheet = L; Sheet.Solid = false;
        auto Sh = SkinSolver::Loft({ Sq, Ci }, Sheet);
        Panel.Expect("--sheet leaves an open sheet", Sh && !Sh.Payload.IsBody && Sh.Payload.Sheet.Classification == SurfaceClassification::Loft);
        auto Open = SkinSolver::Loft({ NurbsCurve::Line({ 0, 0, 0 }, { 2, 0, 0 }).Payload, NurbsCurve::Arc({ 1, 0, 1 }, Vec3::UnitY(), 1, 0, Pi).Payload }, L);
        Panel.Expect("open sections give a sheet, degree raised to the arc's 2", Open && !Open.Payload.IsBody && Open.Payload.Sheet.DegreeU == 2);
        auto Mixed = SkinSolver::Loft({ Sq, NurbsCurve::Line({ 0, 0, 2 }, { 1, 0, 2 }).Payload }, L);
        Panel.Expect("mixing open and closed sections is refused", !Mixed);
    }

    Panel.Section("Loft: through-holes and loop lofts");
    {
        NurbsCurve Sq = NurbsCurve::Rectangle(W, { -1, -1 }, { 1, 1 }).Payload, Ci = NurbsCurve::Circle({ 0, 0, 2 }, Z, 1).Payload;
        NurbsCurve H1 = NurbsCurve::Circle({ 0, 0, 0 }, Z, 0.4).Payload, H2 = NurbsCurve::Circle({ 0, 0, 2 }, Z, 0.3).Payload;
        LoftOptions L;
        auto Holed = SkinSolver::Loft({ { Sq, H1 }, { Ci, H2 } }, L); BodyReport R = Report(Holed);
        Panel.Expect("square-with-hole → circle-with-hole: genus-1 solid, one hull", Holed && R.Solid() && R.Genus == 1 && R.Hulls == 1);
        auto Plain = SkinSolver::Loft({ Sq, Ci }, L);
        Panel.Expect("holed volume is smaller than the plain loft", R.Volume < Report(Plain).Volume - 0.5);
        std::vector<NurbsCurve> Ring;
        for (int I = 0; I < 4; ++I) { double A = I * Pi / 2; Ring.push_back(NurbsCurve::Circle({ 3 * std::cos(A), 3 * std::sin(A), 0 }, { -std::sin(A), std::cos(A), 0 }, 0.5).Payload); }
        LoftOptions Loop; Loop.Loop = true;
        auto T = SkinSolver::Loft(Ring, Loop); BodyReport RT = Report(T);
        Panel.Expect("--loop through 4 circles round a ring → torus-like genus-1 solid", T && RT.Solid() && RT.Genus == 1);
        Panel.Within("its volume approximates the torus 2π²·3·0.25 (cubic through 4 stations)", std::fabs(RT.Volume - 2 * Pi * Pi * 3 * 0.25) / (2 * Pi * Pi * 0.75), 0.03);
    }

    Panel.Section("Sweep and pipe: rotation-minimising bases, exact profile carriage");
    {
        NurbsCurve Path = NurbsCurve::Arc({ 0, 0, 0 }, Vec3::UnitY(), 3, 0, Pi / 2).Payload;
        auto P = SkinSolver::Pipe(Path, 0.5, true); BodyReport R = Report(P);
        Panel.Expect("pipe r 0.5 along a quarter circle is a solid", P && R.Solid());
        Panel.Within("its volume is the quarter torus π·0.25·(2π·3)/4", std::fabs(R.Volume - Pi * 0.25 * 2 * Pi * 3 / 4) / 3.7, 2e-3);
        NurbsCurve Prof = NurbsCurve::Rectangle(W, { -0.3, -0.3 }, { 0.3, 0.3 }).Payload, Line = NurbsCurve::Line({ 0, 0, 0 }, { 0, 0, 3 }).Payload;
        SweepOptions O;
        auto S = SkinSolver::Sweep(Prof, Line, O); BodyReport RS = Report(S);
        Panel.Within("square swept along a line = prism 0.36·3", std::fabs(RS.Volume - 1.08), 1e-9);
        Panel.Expect("two stations along a line → linear across (degree 1)", S && S.Payload.Body.Faces[0].Surface.DegreeV == 1);
        SweepOptions Tw = O; Tw.TwistAngle = Pi / 2;
        auto St = SkinSolver::Sweep(Prof, Line, Tw); BodyReport RTw = Report(St);
        Panel.Expect("twisted sweep stays solid, volume within 1% of the prism", St && RTw.Solid() && std::fabs(RTw.Volume - 1.08) < 0.011);
        SweepOptions Sc = O; Sc.ScaleEnd = 0.5;
        auto Ss = SkinSolver::Sweep(Prof, Line, Sc); BodyReport RSc = Report(Ss);
        Panel.Within("scaled sweep (1 → 0.5) is a frustum: 3·0.36·(1+0.5+0.25)/3", std::fabs(RSc.Volume - 0.36 * 1.75), 5e-3);
        std::vector<SkinSolver::PathBasis> F = SkinSolver::BasesAlong(NurbsCurve::Interpolate({ { 2, 0, 0 }, { 0, 2, 1 }, { -2, 0, 2 }, { 0, -2, 3 }, { 2, 0, 4 } }, 3, false).Payload, 33, SweepBases::RotationMinimising);
        double WorstDot = 0, WorstStep = 0;
        for (size_t I = 0; I < F.size(); ++I) { WorstDot = std::max(WorstDot, std::fabs(F[I].Normal.Dot(F[I].Tangent))); if (I) WorstStep = std::max(WorstStep, std::acos(std::min(1.0, F[I].Normal.Dot(F[I - 1].Normal)))); }
        Panel.Within("RMF normals stay perpendicular to the tangent", WorstDot, 1e-9);
        Panel.Expect("RMF normals turn smoothly (< 20° per station on a helix)", WorstStep < ScalarCriteria::Radians(20));
        auto Helix = SkinSolver::Pipe(NurbsCurve::Interpolate({ { 2, 0, 0 }, { 0, 2, 1 }, { -2, 0, 2 }, { 0, -2, 3 }, { 2, 0, 4 } }, 3, false).Payload, 0.3, true);
        Panel.Expect("pipe along a 3D spline is a closed solid", Helix && Report(Helix).Solid());
    }

    Panel.Section("Coons patch and N-sided fill");
    {
        std::vector<NurbsCurve> Bd = { NurbsCurve::Line({ 0, 0, 0 }, { 2, 0, 0 }).Payload, NurbsCurve::ArcThreePoints({ 2, 0, 0 }, { 2.3, 1, 0.5 }, { 2, 2, 0 }).Payload,
                                       NurbsCurve::Line({ 2, 2, 0 }, { 0, 2, 0 }).Payload, NurbsCurve::ArcThreePoints({ 0, 2, 0 }, { -0.3, 1, 0.5 }, { 0, 0, 0 }).Payload };
        auto C = SkinSolver::CoonsPatch(Bd);
        Panel.Expect("four boundaries (two lines, two arcs) → Coons sheet", bool(C));
        double Worst = 0;
        if (C) for (int I = 0; I <= 20; ++I) { double V = I / 20.0; double D = 0; (void)Bd[1].ClosestParameter(C.Payload.Sample(1, V), &D); Worst = std::max(Worst, D); (void)Bd[3].ClosestParameter(C.Payload.Sample(0, V), &D); Worst = std::max(Worst, D); }
        Panel.Within("sheet edges lie on the rational arcs within the refit tolerance", Worst, 1e-6);
        Panel.Within("centre of the patch is the average of the arc bulges (z = 0.5)", C ? std::fabs(C.Payload.Sample(0.5, 0.5).Z - 0.5) : 1, 1e-9);
        // any order and sense
        auto C2 = SkinSolver::CoonsPatch({ Bd[2], Bd[0].Reversed(), Bd[3], Bd[1] });
        Panel.Expect("boundaries in any order / sense are rung into the same patch", C2 && std::fabs(C2.Payload.Sample(0.5, 0.5).Z - 0.5) < 1e-9);
        auto Tri = SkinSolver::Patch({ NurbsCurve::Line({ 0, 0, 0 }, { 2, 0, 0 }).Payload, NurbsCurve::Line({ 2, 0, 0 }, { 1, 2, 0.5 }).Payload, NurbsCurve::Line({ 1, 2, 0.5 }, { 0, 0, 0 }).Payload });
        Panel.Expect("three boundaries → degenerate-corner Coons sheet", Tri && !Tri.Payload.IsBody);
        std::vector<NurbsCurve> Pent;
        for (int I = 0; I < 5; ++I) { double A = I * 2 * Pi / 5, B = (I + 1) * 2 * Pi / 5; Pent.push_back(NurbsCurve::Line({ 2 * std::cos(A), 2 * std::sin(A), 0.3 * std::sin(3 * A) }, { 2 * std::cos(B), 2 * std::sin(B), 0.3 * std::sin(3 * B) }).Payload); }
        auto P5 = SkinSolver::Patch(Pent); BodyReport R5 = Report(P5);
        Panel.Expect("five boundaries → five Coons quads sewn about the centre (5 faces, one open rim)", P5 && P5.Payload.IsBody && R5.Faces == 5 && R5.Hulls == 1);
        Panel.Expect("interior spokes are shared (5 interior edges with two users, 10 rim edges)", P5 && R5.Edges == 15 && R5.OpenEdges == 10);
        auto Disc = SkinSolver::Patch({ NurbsCurve::Circle({ 0, 0, 0 }, Z, 1).Payload });
        Panel.Expect("one closed curve is quartered and patched", Disc && !Disc.Payload.IsBody);
        Panel.Within("the circle patch is flat and its centre is the circle centre", Disc ? Disc.Payload.Sheet.Sample(0.5, 0.5).Length() : 1, 1e-6);
        auto Gap = SkinSolver::Patch({ Bd[0], Bd[1], Bd[2] });
        Panel.Expect("boundaries that do not close are refused", !Gap);
    }

    Panel.Section("Kernel fix: Split at an existing full-multiplicity knot");
    {
        NurbsCurve C = NurbsCurve::Circle({ 0, 0, 0 }, Z, 1).Payload;
        auto [A, B] = C.Split(0.25);
        Panel.Expect("circle split at its quarter knot gives a 3-pole arc and a 7-pole remainder", A.PoleCount() == 3 && B.PoleCount() == 7);
        Panel.Within("both pieces keep their lengths (π/2 and 3π/2)", std::fabs(A.Length() - Pi / 2) + std::fabs(B.Length() - 1.5 * Pi), 1e-9);
    }

    Panel.Section("Console: derived figures follow their sources (recipes), sources stay, undo works");
    {
        ConsoleHost Host("/tmp/SolidArcVerification", 1280, 800);
        auto Figure = [&](const char* N) { return Host.Document().Find(std::string(N)); };
        Host.Execute("rect (-1,-1) (1,1) ; circle (0,0,2) 1 ; loft Rectangle Circle");
        Panel.Expect("loft creates a solid and leaves both sketch curves in place", Figure("Loft") && Figure("Loft")->Classification == FigureClassification::Body && Figure("Rectangle") && Figure("Circle"));
        Panel.Expect("the loft carries a recipe naming its sources", Figure("Loft")->Recipe.Operation == RecipeOperation::Loft && Figure("Loft")->Recipe.Sections.size() == 2);
        double V0 = Figure("Loft")->Body.Validate().Volume;
        Host.Execute("move Circle (0,0,1)");
        Panel.Expect("moving the circle regenerates the loft (taller → larger volume)", Figure("Loft")->Body.Validate().Volume > V0 + 1.0 && std::fabs(Figure("Loft")->Body.Bounds().High.Z - 3.0) < 1e-9);
        Host.Execute("key 1 ; select poles Circle 1 2 3 ; key g ; type 0.5,0,0 ; key return");
        Panel.Expect("pulling three circle poles (edit mode G) regenerates the loft again (top rim follows the bulged circle)", Figure("Loft")->Body.Bounds().High.X > 1.15 && Figure("Loft")->Body.Validate().Solid());
        Host.Execute("undo ; key 4");
        Panel.Expect("undo restores the loft with the circle", std::fabs(Figure("Loft")->Body.Bounds().High.X - 1.0) < 1e-6);
        Host.Execute("circle (0,0,4) 0.5 --name=Top ; loft Rectangle Circle Top --name=Tower ; delete Top");
        Panel.Expect("deleting a source leaves the last geometry and a complaint", Figure("Tower") && !Figure("Tower")->Recipe.Complaint.empty() && Figure("Tower")->Classification == FigureClassification::Body);
        Host.Execute("undo");
        Panel.Expect("undo brings the source back and clears the complaint", Figure("Top") && Figure("Tower")->Recipe.Complaint.empty());
        Host.Execute("recipe bake Tower ; move Circle (0,0,-1)");
        Panel.Expect("a baked figure no longer follows its sources", !Figure("Tower")->Recipe.Live() && std::fabs(Figure("Tower")->Body.Bounds().High.Z - 4.0) < 1e-9);
        Host.Execute("box (5,0,0) 2 2 1 ; circle (6,1,3) 0.6 --name=Ring ; sweep Ring Box:e8 --name=Tube ; pipe Box:e4 0.2 --name=Rail");
        Panel.Expect("sweep and pipe accept body edges (Box:eN) as path", Figure("Tube") && Figure("Rail") && Figure("Rail")->Classification == FigureClassification::Body);
        Host.Execute("move Box (0,0,2)");
        Panel.Expect("moving the box regenerates the rail along its edge", std::fabs(Figure("Rail")->Body.Bounds().Low.Z - (3.0 - 0.2)) < 1e-6);
        Host.Execute("rect (10,0) (14,4) --name=Plate ; circle (12,2) 1 --name=Hole ; circle (12,2,3) 0.8 --name=Lid ; circle (12,2,3) 0.3 --name=LidHole ; areas ; loft a0 Lid");
        Panel.Expect("lofting an area with a hole to a single circle is refused (loop counts differ)", !Figure("Loft.2"));
        Host.Execute("line (0,0,8) (2,0,8) --name=B1 ; line (2,0,8) (2,2,9) --name=B2 ; line (2,2,9) (0,2,8) --name=B3 ; line (0,2,8) (0,0,8) --name=B4 ; fillpatch B1 B2 B3 B4 --name=Skin");
        Panel.Expect("fillpatch over four lines → Coons sheet", Figure("Skin") && Figure("Skin")->Classification == FigureClassification::Surface);
        Host.Execute("move B2 (0,0,1)");
        Panel.Expect("moving one boundary without its neighbours breaks the ring → complaint, geometry kept", Figure("Skin") && !Figure("Skin")->Recipe.Complaint.empty());
        Host.Execute("undo ; select B2 B3 ; select poles B2 1 ; select poles B3 0 --add ; key g ; type 0,0,1 ; key return");
        Panel.Expect("moving the shared corner pole on both curves keeps the ring closed → patch regenerates (corner z = 10)", Figure("Skin") && Figure("Skin")->Recipe.Complaint.empty() && std::fabs(Figure("Skin")->Surface.Bounds().High.Z - 10.0) < 1e-6);
        Host.Execute("view iso ; view fit ; render Proof_08_Console");
    }

    return Panel.Conclude();
}
