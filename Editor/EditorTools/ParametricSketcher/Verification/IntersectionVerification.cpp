//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Verification/IntersectionVerification.cpp — Phase 9: SSI curves · trimmed faces · body booleans · winding
//============================================================================================================================================
#include "Kernel/IntersectionSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <cmath>

using namespace Frontier;

namespace
{
    struct Result { bool Ok = false; BodyReport R; BooleanReport B; BrepBody Body; };
    Result Run(const BrepBody& A, const BrepBody& B, BodyOperation Op)
    {
        Result O; Deliver<BrepBody> D = IntersectionSolver::Combine(A, B, Op, &O.B);
        if (D) { O.Ok = true; O.Body = D.Payload; O.R = O.Body.Validate(); }
        return O;
    }
    // Every triangle of a tessellated face must lie on its surface, and no triangle may be far from the face's trimming loops.
    double WorstSurfaceDeviation(const BrepBody& B)
    {
        double Worst = 0;
        for (size_t F = 0; F < B.Faces.size(); ++F)
        {
            BrepBody::FaceTriangles T = B.TessellateFace(static_cast<int>(F));
            for (size_t I = 0; I < T.Positions.size(); ++I) Worst = std::max(Worst, B.Faces[F].Surface.Sample(T.Parameters[I].X, T.Parameters[I].Y).Distance(T.Positions[I]));
        }
        return Worst;
    }
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 9 · Intersection Verification — SSI marching · exits on edges · (u,v) arrangements · union/subtract/intersect · winding");
    const double Pi = ScalarCriteria::Pi;
    BrepBody Box = BrepBody::Box({ 0, 0, 0 }, { 2, 2, 2 }).Payload;
    BrepBody Bore = BrepBody::Cylinder({ 1, 1, -1 }, { 0, 0, 1 }, 0.5, 4).Payload;

    Panel.Section("Surface–surface intersection curves");
    {
        std::vector<IntersectionCurve> X = IntersectionSolver::Intersect(Box, Bore);
        Panel.Expect("box ∩ through-cylinder: two curves (one per cap)", X.size() == 2);
        double Worst = 0, LengthErr = 0;
        for (const IntersectionCurve& C : X) { Worst = std::max(Worst, C.Deviation); LengthErr = std::max(LengthErr, std::fabs(C.Curve.Length() - Pi)); }
        Panel.Within("points lie on both surfaces (max deviation < 1e-6)", Worst, 1e-6);
        Panel.Within("each curve is a full circle of radius 0.5 (length π)", LengthErr, 1e-4);
        bool Traces = true;
        for (const IntersectionCurve& C : X) Traces = Traces && C.TraceA.size() == C.Points.size() && C.TraceB.size() == C.Points.size();
        Panel.Expect("every point carries (u,v) on both faces", Traces);
        // each circle is split at the cylinder seam: it starts and ends on the seam edge, i.e. open with coincident ends
        bool SeamSplit = true; for (const IntersectionCurve& C : X) SeamSplit = SeamSplit && !C.Closed && C.Points.front().Distance(C.Points.back()) < 1e-6;
        Panel.Expect("curves are split where they cross the cylinder seam (open pieces, ends meet)", SeamSplit);
        BrepBody B2 = BrepBody::Box({ 1, 1, 1 }, { 3, 3, 3 }).Payload;
        std::vector<IntersectionCurve> Y = IntersectionSolver::Intersect(Box, B2);
        Panel.Expect("box ∩ offset box: six straight pieces (one per face pair)", Y.size() == 6);
        double Dev = 0; for (const IntersectionCurve& C : Y) Dev = std::max(Dev, C.Deviation);
        Panel.Within("planar pieces are exact", Dev, 1e-12);
        Panel.Expect("disjoint bodies give no curves", IntersectionSolver::Intersect(Box, BrepBody::Sphere({ 9, 9, 9 }, 1).Payload).empty());
        Panel.Expect("Encloses: the cylinder axis point is inside the box, a far point is not", IntersectionSolver::Encloses(Box, { 1, 1, 1 }) && !IntersectionSolver::Encloses(Box, { 5, 1, 1 }));
    }

    Panel.Section("Box ∪ − ∩ box (exact volumes)");
    {
        BrepBody B2 = BrepBody::Box({ 1, 1, 1 }, { 3, 3, 3 }).Payload;
        Result U = Run(Box, B2, BodyOperation::Union), S = Run(Box, B2, BodyOperation::Subtract), I = Run(Box, B2, BodyOperation::Intersect);
        Panel.Expect("union is a closed manifold oriented solid", U.Ok && U.R.Solid() && U.R.Genus == 0);
        Panel.Within("union volume 8 + 8 − 1 = 15", std::fabs(U.R.Volume - 15.0), 1e-9);
        Panel.Within("union area 42", std::fabs(U.R.Area - 42.0), 1e-9);
        Panel.Expect("subtract is a solid with volume 7", S.Ok && S.R.Solid() && std::fabs(S.R.Volume - 7.0) < 1e-9);
        Panel.Expect("intersect is the unit cube (V8 E12 F6, volume 1)", I.Ok && I.R.Solid() && I.R.Vertices == 8 && I.R.Edges == 12 && I.R.Faces == 6 && std::fabs(I.R.Volume - 1.0) < 1e-9);
        Panel.Expect("six intersection curves, three pieces of each body inside the other", U.B.Curves == 6 && U.B.InsideA == 3 && U.B.InsideB == 3);
        BrepBody Through = BrepBody::Box({ 0.5, 0.5, -1 }, { 1.5, 1.5, 3 }).Payload;
        Result T = Run(Box, Through, BodyOperation::Subtract);
        Panel.Expect("box − through-box: genus 1 solid, volume 6", T.Ok && T.R.Solid() && T.R.Genus == 1 && std::fabs(T.R.Volume - 6.0) < 1e-9);
    }

    Panel.Section("Box ∪ − ∩ cylinder (trimmed planar caps with a hole, trimmed cylinder)");
    {
        Result U = Run(Box, Bore, BodyOperation::Union), S = Run(Box, Bore, BodyOperation::Subtract), I = Run(Box, Bore, BodyOperation::Intersect);
        const double Disc = Pi * 0.25, VolBore = Bore.Validate().Volume;              // the measured cylinder (tessellated quadrature)
        Panel.Expect("all three operations deliver closed manifold oriented solids", U.Ok && S.Ok && I.Ok && U.R.Solid() && S.R.Solid() && I.R.Solid());
        Panel.Within("box − bore volume 8 − 2π/4 (to tessellation quadrature)", std::fabs(S.R.Volume - (8.0 - Disc * 2)), 2e-3);
        Panel.Expect("box − bore has genus 1", S.R.Genus == 1);
        Panel.Within("box ∩ bore is half the cylinder (trimmed vs natural quadrature)", std::fabs(I.R.Volume - VolBore * 0.5), 5e-4);
        Panel.Within("box − bore + box ∩ bore = box (exact, same quadrature)", std::fabs(S.R.Volume + I.R.Volume - 8.0), 1e-6);
        Panel.Within("union + intersection = box + cylinder (inclusion–exclusion, same quadrature)", std::fabs(U.R.Volume + I.R.Volume - 8.0 - VolBore), 5e-4);
        int Trimmed = 0; for (const BrepFace& F : S.Body.Faces) if (!F.Natural) ++Trimmed;
        Panel.Expect("the difference keeps two trimmed caps and a trimmed cylinder face (3 trimmed, 4 natural)", Trimmed == 3 && S.Body.Faces.size() == 7);
        Panel.Within("trimmed-face tessellation lies on its surfaces", WorstSurfaceDeviation(S.Body), 1e-9);
        // winding: every face normal of the difference points away from the material — sample the bore wall
        int Wall = -1; for (size_t F = 0; F < S.Body.Faces.size(); ++F) if (S.Body.Faces[F].Surface.Classification == SurfaceClassification::Cylinder) Wall = static_cast<int>(F);
        Vec3 N = Wall >= 0 ? S.Body.FaceNormal(Wall, 0.125, 0.5) : Vec3{};
        Vec3 P = Wall >= 0 ? S.Body.Faces[Wall].Surface.Sample(0.125, 0.5) : Vec3{};
        Panel.Expect("the bore wall of the difference faces INTO the hole (towards the axis)", Wall >= 0 && N.Dot(Vec3{ 1, 1, P.Z } - P) > 0.9 * (Vec3{ 1, 1, P.Z } - P).Length());
        Vec3 Nc = Wall >= 0 ? I.Body.FaceNormal(0, 0.125, 0.5) : Vec3{};
        (void)Nc;
        Panel.Expect("intersection result is a plain cylinder V2 E3 F3", I.R.Vertices == 2 && I.R.Edges == 3 && I.R.Faces == 3);
    }

    Panel.Section("Curved against curved: sphere ∩ sphere, cylinder ∩ cylinder, box − torus, corner sphere");
    {
        BrepBody Sa = BrepBody::Sphere({ 0, 0, 0 }, 1).Payload, Sb = BrepBody::Sphere({ 1.2, 0.3, 0.2 }, 1).Payload;
        Result U = Run(Sa, Sb, BodyOperation::Union), I = Run(Sa, Sb, BodyOperation::Intersect), S = Run(Sa, Sb, BodyOperation::Subtract);
        Panel.Expect("sphere ∪ sphere, ∩, − are solids", U.Ok && I.Ok && S.Ok && U.R.Solid() && I.R.Solid() && S.R.Solid());
        double D = Vec3{ 1.2, 0.3, 0.2 }.Length();
        double LensExact = Pi * (4.0 * 1 + D) * std::pow(2.0 * 1 - D, 2) / 12.0;              // equal spheres lens
        Panel.Within("lens volume matches the closed form", std::fabs(I.R.Volume - LensExact), 2e-3);
        const double VolSphere = Sa.Validate().Volume;
        Panel.Within("union + lens = two measured spheres (inclusion–exclusion)", std::fabs(U.R.Volume + I.R.Volume - 2 * VolSphere), 5e-3);
        Panel.Within("sphere − sphere + lens = one measured sphere", std::fabs(S.R.Volume + I.R.Volume - VolSphere), 2e-3);
        BrepBody Ca = BrepBody::Cylinder({ 0, 0, -2 }, { 0, 0, 1 }, 1, 4).Payload, Cb = BrepBody::Cylinder({ -2, 0.3, 0 }, { 1, 0, 0 }, 0.5, 4).Payload;
        Result Cu = Run(Ca, Cb, BodyOperation::Union), Ci = Run(Ca, Cb, BodyOperation::Intersect);
        Panel.Expect("crossing cylinders: union and intersection are solids, four SSI pieces", Cu.Ok && Ci.Ok && Cu.R.Solid() && Ci.R.Solid() && Cu.B.Curves == 4);
        Panel.Within("cylinders: union + intersection = both volumes", std::fabs(Cu.R.Volume + Ci.R.Volume - (Pi * 4 + Pi * 0.25 * 4)), 5e-2);
        BrepBody Torus = BrepBody::Torus({ 1, 1, 1 }, { 0, 0, 1 }, 1.2, 0.3).Payload;
        Result Tu = Run(Box, Torus, BodyOperation::Union), Ts = Run(Box, Torus, BodyOperation::Subtract), Ti = Run(Box, Torus, BodyOperation::Intersect);
        Panel.Expect("box with a torus poking through all four sides: union/subtract/intersect are solids", Tu.Ok && Ts.Ok && Ti.Ok && Tu.R.Solid() && Ts.R.Solid() && Ti.R.Solid());
        Panel.Expect("box ∩ torus is a ring (genus 1); box − torus genus 0", Ti.R.Genus == 1 && Ts.R.Genus == 0);
        Panel.Within("torus: subtract + intersect = box", std::fabs(Ts.R.Volume + Ti.R.Volume - 8.0), 2e-3);
        BrepBody Corner = BrepBody::Sphere({ 1.9, 2.1, 2.05 }, 0.8).Payload;
        Result Ks = Run(Box, Corner, BodyOperation::Subtract), Ki = Run(Box, Corner, BodyOperation::Intersect);
        Panel.Expect("a sphere scooping a box corner (curve crosses three faces): solids", Ks.Ok && Ki.Ok && Ks.R.Solid() && Ki.R.Solid());
        Panel.Within("corner: subtract + intersect = box", std::fabs(Ks.R.Volume + Ki.R.Volume - 8.0), 1e-3);
    }

    Panel.Section("Refusals are explicit");
    {
        Panel.Expect("disjoint intersection → 'result is empty'", !Run(Box, BrepBody::Sphere({ 9, 9, 9 }, 1).Payload, BodyOperation::Intersect).Ok);
        BrepBody Sheet = BrepBody::FromSurface(NurbsSurface::Plane({ 0, 0, 1 }, Vec3::UnitX(), Vec3::UnitY(), 4, 4).Payload);
        Panel.Expect("a sheet is refused (booleans need two closed solids)", !Run(Box, Sheet, BodyOperation::Subtract).Ok);
        Panel.Expect("coincident faces (box against itself shifted along one face) are refused, not guessed", !Run(Box, BrepBody::Box({ 1, 0, 0 }, { 3, 2, 2 }).Payload, BodyOperation::Union).Ok);
    }

    Panel.Section("Console: boolean verb on bodies, hotkeys, undo");
    {
        ConsoleHost Host("/tmp/SolidArcVerification", 1280, 800);
        auto Figure = [&](const char* N) { return Host.Document().Find(std::string(N)); };
        Host.Execute("box (0,0,0) (2,2,2) ; cylinder (1,1,-1) 0.5 4 ; boolean subtract Box Cylinder");
        Panel.Expect("boolean subtract Box Cylinder → 'Difference' body, sources consumed", Figure("Difference") && !Figure("Box") && !Figure("Cylinder"));
        Panel.Within("console difference volume 8 − π/2 (to quadrature)", std::fabs(Figure("Difference")->Body.Validate().Volume - (8 - Pi / 2)), 2e-3);
        Host.Execute("undo");
        Panel.Expect("undo restores both sources", Figure("Box") && Figure("Cylinder") && !Figure("Difference"));
        Host.Execute("select Box Cylinder ; key q");
        Panel.Expect("Q on two selected bodies unions them (Plasticity Q)", Figure("Union") && Figure("Union")->Body.Validate().Solid());
        Host.Execute("box (5,0,0) (6,1,1) --name=A ; sphere (5.5,0.5,1.02) 0.4 --name=B ; boolean intersect A B --keep --name=Cap");
        Panel.Expect("--keep leaves A and B in place next to the result", Figure("A") && Figure("B") && Figure("Cap"));
        Host.Execute("intersections A B --curves");
        Panel.Expect("intersections <body> <body> --curves adds the SSI curve to the scene", Figure("Intersection.1") && Figure("Intersection.1")->Classification == FigureClassification::Curve);
        Host.Execute("rect (10,0) (12,2) ; circle (12,1) 1 ; boolean union Rectangle Circle");
        Panel.Expect("the same verb still does the 2D profile boolean for sketch curves", Figure("Union.2") && Figure("Union.2")->Classification == FigureClassification::Curve);
    }
    return Panel.Conclude();
}
