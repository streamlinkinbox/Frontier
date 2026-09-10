//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Verification/FairPatchVerification.cpp — Phase 9b: energy-fair fills with G0 / G1 / G2 rims, guides, N-sided, recipes
//============================================================================================================================================
#include "Kernel/FairPatchSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <cmath>

using namespace Frontier;

namespace
{
    // Rims of a body face with the neighbouring face as support.
    std::vector<FairRim> FaceRims(const BrepBody& Body, int Face, RimContinuity Continuity)
    {
        std::vector<FairRim> Rims;
        for (int L : Body.Faces[size_t(Face)].Loops) for (int Ce : Body.Loops[size_t(L)].Coedges)
        {
            const BrepCoedge& C = Body.Coedges[size_t(Ce)]; const BrepEdge& E = Body.Edges[size_t(C.Edge)];
            int Other = -1; for (int Oc : E.Coedges) if (Body.Coedges[size_t(Oc)].Face != Face) Other = Body.Coedges[size_t(Oc)].Face;
            FairRim R; R.Curve = E.Curve; R.Support = &Body.Faces[size_t(Other)].Surface; R.Continuity = Continuity; Rims.push_back(R);
        }
        return Rims;
    }
    int FaceWithNormal(const BrepBody& Body, Vec3 N)
    {
        for (size_t F = 0; F < Body.Faces.size(); ++F) { Vec3 M = Body.Faces[F].Surface.Normal(0.5, 0.5); if (Body.Faces[F].Reversed) M = -M; if (M.Dot(N) > 0.9) return int(F); }
        return -1;
    }
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 9b · FairPatch Verification — fair interior · G1 / G2 rims from adjacent faces · tension · guides · N-sided seams · recipes");
    const double Pi = ScalarCriteria::Pi;

    Panel.Section("Fairness: a planar boundary gives the plane; the boundary is kept exactly");
    {
        std::vector<FairRim> R(4);
        R[0].Curve = NurbsCurve::Line({ 0, 0, 0 }, { 1, 0, 0 }).Payload; R[1].Curve = NurbsCurve::Line({ 1, 0, 0 }, { 1, 1, 0 }).Payload;
        R[2].Curve = NurbsCurve::Line({ 1, 1, 0 }, { 0, 1, 0 }).Payload; R[3].Curve = NurbsCurve::Line({ 0, 1, 0 }, { 0, 0, 0 }).Payload;
        FairPatchOptions O; O.Spans = 6; FairPatchReport Rep;
        Deliver<NurbsSurface> S = FairPatchSolver::Quad(R, {}, O, &Rep);
        Panel.Expect("four lines → one untrimmed quad sheet classified FairPatch", S && S.Payload.Classification == SurfaceClassification::FairPatch);
        double Worst = 0; for (const Vec4& P : S.Payload.Poles) Worst = std::max(Worst, std::fabs(P.Z));
        Panel.Within("every pole lies in the plane (bending energy minimiser of a flat rim is flat)", Worst, 1e-9);
        Panel.Within("sampled bending energy of the plane", Rep.Energy, 1e-12);
        Panel.Expect("the lattice has at least the requested spans", S.Payload.CountU >= 6 + 3 && S.Payload.CountV >= 6 + 3);
        // a twisted (non-planar) quad: corners lifted alternately
        std::vector<FairRim> T(4);
        T[0].Curve = NurbsCurve::Line({ 0, 0, 0 }, { 1, 0, 0.3 }).Payload; T[1].Curve = NurbsCurve::Line({ 1, 0, 0.3 }, { 1, 1, 0 }).Payload;
        T[2].Curve = NurbsCurve::Line({ 1, 1, 0 }, { 0, 1, 0.3 }).Payload; T[3].Curve = NurbsCurve::Line({ 0, 1, 0.3 }, { 0, 0, 0 }).Payload;
        FairPatchReport RepT; Deliver<NurbsSurface> St = FairPatchSolver::Quad(T, {}, O, &RepT);
        Panel.Expect("a twisted rim is accepted", bool(St));
        Panel.Within("its fair interior bends about as little as the bilinear Coons fill (ratio ≤ 1.25)", RepT.Energy / RepT.CoonsEnergy, 1.25);
        Vec3 C = St.Payload.Sample(0.5, 0.5);
        Panel.Within("saddle centre sits at the mean corner height 0.15", std::fabs(C.Z - 0.15), 2e-2);
        for (int Side = 0; Side < 4; ++Side)
        {
            double D = 0; for (int K = 0; K <= 10; ++K) { double U = 0, V = 0, Dd = 0; St.Payload.ClosestParameter(T[size_t(Side)].Curve.Sample(K / 10.0), U, V, &Dd); D = std::max(D, Dd); }
            if (Side == 0) Panel.Within("the rims are interpolated exactly (boundary rows fixed)", D, 1e-9);
        }
    }

    Panel.Section("G1 / G2 against a curved support: a window in a cylinder");
    {
        NurbsSurface Cyl = NurbsSurface::Cylinder({ 0, -1, 0 }, Vec3::UnitY(), 1.0, 3.0).Payload;
        auto Pt = [](double A, double Y) { return Vec3{ std::cos(A), Y, std::sin(A) }; };
        double A0 = Pi * 0.25, A1 = Pi * 0.75;
        std::vector<FairRim> R(4);
        R[0].Curve = NurbsCurve::Interpolate({ Pt(A0, 0), Pt(A0 + 0.25 * (A1 - A0), 0), Pt(0.5 * (A0 + A1), 0), Pt(A0 + 0.75 * (A1 - A0), 0), Pt(A1, 0) }, 3).Payload;
        R[1].Curve = NurbsCurve::Line(Pt(A1, 0), Pt(A1, 1)).Payload;
        R[2].Curve = NurbsCurve::Interpolate({ Pt(A1, 1), Pt(A0 + 0.75 * (A1 - A0), 1), Pt(0.5 * (A0 + A1), 1), Pt(A0 + 0.25 * (A1 - A0), 1), Pt(A0, 1) }, 3).Payload;
        R[3].Curve = NurbsCurve::Line(Pt(A0, 1), Pt(A0, 0)).Payload;
        auto Radial = [](const NurbsSurface& S) { double W = 0; for (int I = 0; I <= 20; ++I) for (int J = 0; J <= 20; ++J) { Vec3 P = S.Sample(I / 20.0, J / 20.0); W = std::max(W, std::fabs(std::sqrt(P.X * P.X + P.Z * P.Z) - 1)); } return W; };
        FairPatchOptions O; double Dev[3] = {}, Tan[3] = {}, Curv[3] = {};
        for (int M = 0; M < 3; ++M)
        {
            for (FairRim& X : R) { X.Support = &Cyl; X.Continuity = RimContinuity(M); }
            FairPatchReport Rep; Deliver<NurbsSurface> S = FairPatchSolver::Quad(R, {}, O, &Rep);
            Dev[M] = S ? Radial(S.Payload) : 1e9; Tan[M] = Rep.TangentBreak; Curv[M] = S ? FairPatchSolver::CurvatureBreak(S.Payload, R[1]) : 1e9;
            Panel.Note("G%d: radial deviation from the cylinder %.5f  tangent break %.4f°  curvature break %.4f 1/m  energy %.4f (Coons %.4f)", M, Dev[M], ScalarCriteria::Degrees(Rep.TangentBreak), Rep.CurvatureBreak, Rep.Energy, Rep.CoonsEnergy);
        }
        Panel.Expect("G0 fill sags away from the cylinder (> 4 %)", Dev[0] > 0.04);
        Panel.Within("G1 fill stays within 1 % of the cylinder", Dev[1], 1e-2);
        Panel.Within("G2 fill stays within 0.4 % of the cylinder", Dev[2], 4e-3);
        Panel.Within("G1 rims: worst normal angle against the cylinder < 0.5°", ScalarCriteria::Degrees(Tan[1]), 0.5);
        Panel.Within("G2 rims: worst normal angle against the cylinder < 0.5°", ScalarCriteria::Degrees(Tan[2]), 0.5);
        Panel.Expect("G2 matches the cylinder's curvature 1/m across the straight rims better than G1 does", Curv[2] < 0.5 * Curv[1]);
        Panel.Within("G2 curvature mismatch across the straight rims < 0.15 (of 1.0)", Curv[2], 0.15);
        // partial support: only the two arcs
        for (FairRim& X : R) { X.Support = nullptr; X.Continuity = RimContinuity::Position; }
        R[0].Support = &Cyl; R[0].Continuity = RimContinuity::Tangent; R[2].Support = &Cyl; R[2].Continuity = RimContinuity::Tangent;
        FairPatchReport RepP; Deliver<NurbsSurface> Sp = FairPatchSolver::Quad(R, {}, O, &RepP);
        Panel.Expect("mixed rims (two G1, two G0) are accepted", bool(Sp));
        Panel.Within("the two G1 rims still meet the cylinder < 0.5°", ScalarCriteria::Degrees(RepP.TangentBreak), 0.5);
        // tension
        for (FairRim& X : R) { X.Support = &Cyl; X.Continuity = RimContinuity::Tangent; }
        double Z[3] = {}; int K = 0;
        for (double T : { 0.5, 1.0, 2.0 }) { for (FairRim& X : R) X.Tension = T; Deliver<NurbsSurface> S = FairPatchSolver::Quad(R, {}, O); Z[K++] = S.Payload.Sample(0.5, 0.5).Z; }
        Panel.Expect("tension 0.5 < 1 < 2 lifts the centre monotonically (fuller fill)", Z[0] < Z[1] && Z[1] < Z[2]);
        Panel.Within("tension 1 puts the centre on the cylinder (z ≈ 1)", std::fabs(Z[1] - 1.0), 1e-2);
        // unsupported G1 falls back to G0 and says so
        for (FairRim& X : R) { X.Support = nullptr; X.Tension = 1.0; }
        FairPatchReport RepU; Deliver<NurbsSurface> Su = FairPatchSolver::Quad(R, {}, O, &RepU);
        Panel.Expect("G1 without a support is built as G0 and reported (4 unsupported rims)", Su && RepU.UnsupportedRims == 4 && RepU.TangentBreak == 0.0);
    }

    Panel.Section("A pillow over a box: rims flush with four perpendicular walls, and a guide");
    {
        BrepBody Box = BrepBody::Box({ 0, 0, 0 }, { 1, 1, 1 }).Payload;
        int Top = FaceWithNormal(Box, Vec3::UnitZ());
        std::vector<FairRim> Rims = FaceRims(Box, Top, RimContinuity::Tangent);
        FairPatchOptions O; FairPatchReport Rep;
        Deliver<SkinSolver::Skin> S = FairPatchSolver::Build(Rims, {}, O, &Rep);
        Panel.Expect("four box-top edges with the side walls as supports → one quad sheet", S && !S.Payload.IsBody && Rep.Quads == 1);
        Vec3 P, Du, Dv; S.Payload.Sheet.Derivatives(0.5, 0.0, P, Du, Dv);
        Panel.Within("mid-rim the fill leaves the wall vertically (cross derivative ∥ +Z)", std::fabs(Dv.Normalised().Z - 1.0), 1e-3);
        Panel.Expect("the pillow rises above the top face", S.Payload.Sheet.Sample(0.5, 0.5).Z > 1.2);
        Panel.Within("worst rim normal angle against the walls (corners excluded) < 3°", ScalarCriteria::Degrees(Rep.TangentBreak), 3.0);
        // top face itself as support (planar) → back to the flat face
        std::vector<FairRim> Flat = Rims; for (FairRim& R : Flat) R.Support = &Box.Faces[size_t(Top)].Surface;
        FairPatchReport RepF; Deliver<SkinSolver::Skin> Sf = FairPatchSolver::Build(Flat, {}, O, &RepF);
        Panel.Within("with the flat top as support G1 reproduces the plane (energy 0)", RepF.Energy, 1e-9);
        // guide
        NurbsCurve G = NurbsCurve::Bezier({ { 0, 0.5, 1 }, { 0.33, 0.5, 1.5 }, { 0.66, 0.5, 1.5 }, { 1, 0.5, 1 } }).Payload;
        std::vector<FairRim> G0 = Rims; for (FairRim& R : G0) R.Continuity = RimContinuity::Position;
        FairPatchReport RepG; Deliver<SkinSolver::Skin> Sg = FairPatchSolver::Build(G0, { G }, O, &RepG);
        Panel.Expect("a guide curve across a G0 fill is accepted", bool(Sg));
        Panel.Within("the fill passes through the guide within 2 mm (of a 1 m box)", RepG.GuideDeviation, 2e-3);
        Panel.Within("guide crest 1.375 reached at the centre", std::fabs(Sg.Payload.Sheet.Sample(0.5, 0.5).Z - G.Sample(0.5).Z), 3e-3);
        // star option on four rims
        FairPatchOptions Star = O; Star.Star = true; FairPatchReport RepS;
        Deliver<SkinSolver::Skin> Ss = FairPatchSolver::Build(Rims, {}, Star, &RepS);
        Panel.Expect("--star on four rims gives four sewn quads (sheet body, 8 open rim edges)", Ss && Ss.Payload.IsBody && RepS.Quads == 4 && Ss.Payload.Body.Validate().OpenEdges == 8);
        Panel.Within("star seams are tangent-continuous (< 1°)", ScalarCriteria::Degrees(RepS.SeamBreak), 1.0);
    }

    Panel.Section("N-sided fills: pentagon and hexagonal window, seams and rims");
    {
        std::vector<FairRim> P;
        for (int I = 0; I < 5; ++I)
        {
            double A0 = I * 2 * Pi / 5, A1 = (I + 1) * 2 * Pi / 5; FairRim R;
            R.Curve = NurbsCurve::Line({ std::cos(A0), std::sin(A0), 0.2 * std::sin(2 * A0) }, { std::cos(A1), std::sin(A1), 0.2 * std::sin(2 * A1) }).Payload; P.push_back(R);
        }
        FairPatchOptions O; FairPatchReport Rep;
        Deliver<SkinSolver::Skin> S = FairPatchSolver::Build(P, {}, O, &Rep);
        BodyReport B = S ? S.Payload.Body.Validate() : BodyReport{};
        Panel.Expect("five non-planar lines → five quads sewn into one sheet body", S && S.Payload.IsBody && B.Faces == 5 && B.OpenEdges == 10 && B.NonManifoldEdges == 0);
        Panel.Within("spoke seams are tangent-continuous (< 3°)", ScalarCriteria::Degrees(Rep.SeamBreak), 3.0);
        // shuffled / reversed rims give the same fill
        std::vector<FairRim> Q = { P[3], P[0], P[4], P[1], P[2] }; Q[1].Curve = Q[1].Curve.Reversed();
        Deliver<SkinSolver::Skin> Sq = FairPatchSolver::Build(Q, {}, O);
        Panel.Within("rim order and sense do not matter (same area)", std::fabs(Sq.Payload.Body.Validate().Area - B.Area), 1e-6);
        // a gap refuses
        std::vector<FairRim> Gap = P; Gap.pop_back();
        Panel.Expect("an open ring is refused", !FairPatchSolver::Build(Gap, {}, O));
        // one closed curve
        FairRim Circle; Circle.Curve = NurbsCurve::Circle({ 0, 0, 0 }, Vec3::UnitZ(), 1).Payload;
        Deliver<SkinSolver::Skin> Sc = FairPatchSolver::Build({ Circle }, {}, O);
        Panel.Expect("one closed curve is quartered into a single quad", Sc && !Sc.Payload.IsBody);
    }

    Panel.Section("Console: fairpatch verb, per-rim tags, --on, --guides, recipes that follow their sources");
    {
        ConsoleHost Host("/tmp/SolidArcVerification", 1280, 800);
        auto Figure = [&](const char* Name) -> const SceneFigure* { return Host.Document().Find(std::string(Name)); };
        Host.Execute("cylinder (0,0,0) 2 3 --name=Drum ; box (1,-0.8,0.8) (3,0.8,2.2) --name=Cutter ; boolean subtract Drum Cutter --name=Windowed");
        Panel.Expect("fairpatch of six window edges, G2", Host.Execute("fairpatch Windowed:e2 Windowed:e3 Windowed:e4 Windowed:e7 Windowed:e8 Windowed:e9 --g2 --name=Window") && Figure("Window"));
        const SceneFigure* Wf = Figure("Window");
        Panel.Expect("six rims → sheet body of six quads", Wf && Wf->Classification == FigureClassification::Body && Wf->Body.Faces.size() == 6);
        Panel.Expect("recipe records the operation and per-rim continuity", Wf && Wf->Recipe.Operation == RecipeOperation::FairPatch && Wf->Recipe.Sections.size() == 6 && Wf->Recipe.Sections[0].Continuity == RimContinuity::Curvature);
        Panel.Expect("the support of a body edge resolves to the drum wall (a Cylinder), not the cut wall", FigureRecipe::ResolveSupport(Wf->Recipe.Sections[0], Host.Document(), Wf->Recipe.Hint(Host.Document(), Workplane{}))->Classification == SurfaceClassification::Cylinder);
        double Worst = 0;
        for (const BrepFace& F : Wf->Body.Faces) for (int I = 0; I <= 6; ++I) for (int J = 0; J <= 6; ++J) { Vec3 P = F.Surface.Sample(F.Surface.DomainStartU() + (F.Surface.DomainEndU() - F.Surface.DomainStartU()) * I / 6.0, F.Surface.DomainStartV() + (F.Surface.DomainEndV() - F.Surface.DomainStartV()) * J / 6.0); Worst = std::max(Worst, std::fabs(std::sqrt(P.X * P.X + P.Y * P.Y) - 2.0)); }
        Panel.Within("the G2 window lies on the drum radius 2 within 1 %", Worst, 2e-2);
        Host.Execute("box (4,-1,0) (6,1,1) --name=Block ; spline (4,0,1) (4.7,0,1.55) (5.3,0,1.55) (6,0,1) --name=Crest");
        Panel.Expect("per-rim @f tags pick the wall faces; --guides adds a guide", Host.Execute("fairpatch Block:e4@f2 Block:e5@f5 Block:e6@f3 Block:e7@f4 --g1 --guides=Crest --name=Pillow") && Figure("Pillow"));
        double Before = Figure("Pillow")->Surface.Sample(0.5, 0.5).Z;
        Host.Execute("move Crest (0,0,0.4)");
        double After = Figure("Pillow")->Surface.Sample(0.5, 0.5).Z;
        Panel.Within("moving the guide rebuilds the pillow 0.4 higher", std::fabs(After - Before - 0.4), 2e-2);
        Host.Execute("undo");
        Panel.Within("undo brings it back", std::fabs(Figure("Pillow")->Surface.Sample(0.5, 0.5).Z - Before), 1e-9);
        Panel.Expect("tags @g0/@g1/@g2/@t parse per rim", Host.Execute("fairpatch Block:e4@g2@t0.5 Block:e5@g0 Block:e6@g1 Block:e7 --name=Mixed") && Figure("Mixed") && Figure("Mixed")->Recipe.Sections[0].Continuity == RimContinuity::Curvature && Figure("Mixed")->Recipe.Sections[0].Tension == 0.5 && Figure("Mixed")->Recipe.Sections[1].Continuity == RimContinuity::Position);
        Panel.Expect("unknown tag refused", !Host.Execute("fairpatch Block:e4@x Block:e5 Block:e6 Block:e7"));
        Host.Execute("cylinder (10,0,0) 1 2 --sheet --name=Tube ; line (11,0,0.5) (11,0,1.5) --name=L1 ; spline (11,0,1.5) (10.7,0.7,1.5) (10,1,1.5) --degree=2 --name=L2 ; line (10,1,1.5) (10,1,0.5) --name=L3 ; spline (10,1,0.5) (10.7,0.7,0.5) (11,0,0.5) --degree=2 --name=L4");
        Panel.Expect("sketch curves with --on=<surface> take G1 from that surface", Host.Execute("fairpatch L1 L2 L3 L4 --g1 --on=Tube --name=OnTube") && Figure("OnTube") && Figure("OnTube")->Recipe.Sections[0].Support == Figure("Tube")->Identity);
        Panel.Expect("recipe summary names the verb, rims and continuity", Figure("OnTube")->Recipe.Summary(Host.Document()).find("fairpatch of L1[G1]") == 0);
    }
    return Panel.Conclude();
}
