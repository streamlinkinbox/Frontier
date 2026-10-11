//============================================================================================================================================
// 📦 Verification/BlendVerification.cpp — chamfer / fillet / push checked against closed-form volumes, not against pictures
//============================================================================================================================================
// Every case asserts three things: the result is a closed, manifold, consistently wound solid (χ=2); its volume matches
// the analytic local wedge; and its feature topology is intentional.  Ordinary exterior blends remove material, while
// a re-entrant/root blend fills its small void wedge (and therefore adds material).
#include "Kernel/BlendSolver.h"
#include <cmath>
#include <cstdio>
#include <string>

using namespace Frontier;

namespace
{
    int Failures = 0;

    void Check(const char* What, bool Pass, const char* Detail = "")
    {
        if (!Pass) { ++Failures; std::printf("  FAIL  %s %s\n", What, Detail); }
        else std::printf("  ok    %s\n", What);
    }

    void CheckSolid(const char* What, const Deliver<BrepBody>& Result, double Expected, double Tolerance)
    {
        if (!Result) { ++Failures; std::printf("  FAIL  %s — refused: %s\n", What, Result.Denial.Detail); return; }
        BodyReport R = Result.Payload.Validate();
        if (!R.Solid())
        {
            ++Failures;
            std::printf("  FAIL  %s — not a solid (closed %d manifold %d oriented %d open %d χ=%d)\n",
                        What, R.Closed, R.Manifold, R.Oriented, R.OpenEdges, R.EulerCharacteristic);
            return;
        }
        double Error = std::fabs(R.Volume - Expected);
        if (Error > Tolerance)
        {
            ++Failures;
            std::printf("  FAIL  %s — volume %.5f, expected %.5f (error %.3e > %.3e)\n", What, R.Volume, Expected, Error, Tolerance);
            return;
        }
        std::printf("  ok    %-34s volume %12.5f  expected %12.5f  error %.2e\n", What, R.Volume, Expected, Error);
    }

    // First edge of a body whose two endpoints both sit at the given height.
    int EdgeAtHeight(const BrepBody& Body, double Height)
    {
        for (size_t E = 0; E < Body.Edges.size(); ++E)
        {
            const BrepEdge& Edge = Body.Edges[E];
            if (Edge.Coedges.size() != 2 || Edge.VertexStart < 0 || Edge.VertexEnd < 0) continue;
            if (std::fabs(Body.Vertices[Edge.VertexStart].Point.Z - Height) < 1e-9 &&
                std::fabs(Body.Vertices[Edge.VertexEnd].Point.Z - Height) < 1e-9) return (int)E;
        }
        return -1;
    }

    bool IsReflexHandleRoot(const BrepBody& Body, const BrepEdge& Edge)
    {
        if (Edge.VertexStart < 0 || Edge.VertexEnd < 0) return false;
        Vec3 A = Body.Vertices[Edge.VertexStart].Point, B = Body.Vertices[Edge.VertexEnd].Point;
        return std::fabs(std::fabs(A.X) - 10.0) < 1e-9 && std::fabs(std::fabs(B.X) - 10.0) < 1e-9 &&
               std::fabs(A.Y - 17.3205080757) < 1e-8 && std::fabs(B.Y - 17.3205080757) < 1e-8 &&
               std::fabs(std::fabs(A.Z - B.Z) - 10.0) < 1e-8;
    }

    int ExactArcEdges(const BrepBody& Body, double Radius)
    {
        int Count = 0;
        for (const BrepEdge& Edge : Body.Edges)
        {
            // Natural boundary extraction retains the Arc classification but intentionally drops analytic display
            // metadata. Curvature remains the geometric source of truth for the exact rational conic.
            double Middle = 0.5 * (Edge.Curve.DomainStart() + Edge.Curve.DomainEnd());
            if (Edge.Curve.Classification == CurveClassification::Arc &&
                std::fabs(Edge.Curve.Curvature(Middle) - 1.0 / Radius) < 1e-8) ++Count;
        }
        return Count;
    }
}

int main()
{
    std::printf("BlendVerification\n");

    // ---- 1. A box's vertical edge: the reference case, exact in closed form ------------------------------------
    {
        Deliver<BrepBody> Box = BrepBody::Box({ 0, 0, 0 }, { 20, 20, 20 });
        double Base = Box.Payload.Validate().Volume;
        EdgeCornerFrame F; std::string Why;
        Check("box edge 0 yields a corner frame", BlendSolver::Frame(Box.Payload, 0, F, Why), Why.c_str());
        Check("box corner is a right dihedral", std::fabs(F.Dihedral - ScalarCriteria::Pi * 0.5) < 1e-9);

        CheckSolid("box chamfer s=3", BlendSolver::ChamferEdge(Box.Payload, 0, 3.0), Base - BlendSolver::ChamferRemoval(F, 3.0), 1e-6);
        CheckSolid("box fillet  R=3", BlendSolver::FilletEdge(Box.Payload, 0, 3.0),  Base - BlendSolver::FilletRemoval(F, 3.0), 0.1);

        // A fillet keeps more material than the chamfer at the same tangent set-back.
        Deliver<BrepBody> Flat = BlendSolver::ChamferEdge(Box.Payload, 0, BlendSolver::TangentSetBack(F, 3.0));
        Deliver<BrepBody> Roll = BlendSolver::FilletEdge(Box.Payload, 0, 3.0);
        Check("fillet leaves more material than the equivalent chamfer",
              Flat && Roll && Roll.Payload.Validate().Volume > Flat.Payload.Validate().Volume);
    }

    // ---- 2. A pentagonal prism's top edge: the adjacent faces meet at 90° but the ends are mitred ---------------
    {
        Workplane Plane;
        Deliver<NurbsCurve> Profile = NurbsCurve::Polygon(Plane, { 0, 0 }, 20, 5, 0.0, true);
        Deliver<BrepBody> Prism = BrepBody::Extrude(Profile.Payload, { 0, 0, 1 }, 12.0);
        double Base = Prism.Payload.Validate().Volume;
        int Top = EdgeAtHeight(Prism.Payload, 12.0);
        Check("pentagon prism has a top edge", Top >= 0);

        EdgeCornerFrame F; std::string Why;
        Check("pentagon top edge yields a corner frame", BlendSolver::Frame(Prism.Payload, Top, F, Why), Why.c_str());

        // The chamfer is a prism cut and matches the closed form to tessellation accuracy.
        CheckSolid("pentagon top chamfer s=3", BlendSolver::ChamferEdge(Prism.Payload, Top, 3.0), Base - BlendSolver::ChamferRemoval(F, 3.0), 5.0);

        // The fillet's roll is exact, but this edge's ends are mitred: the roll is closed there by a straight chord
        //    rather than the true ellipse section, so the body is a valid solid that under-fills by ~0.4%. Asserted
        //    as a bound, so a regression past it fails rather than passing quietly.
        Deliver<BrepBody> Roll = BlendSolver::FilletEdge(Prism.Payload, Top, 3.0);
        if (!Roll) { ++Failures; std::printf("  FAIL  pentagon top fillet — refused: %s\n", Roll.Denial.Detail); }
        else
        {
            BodyReport R = Roll.Payload.Validate();
            double Ideal = Base - BlendSolver::FilletRemoval(F, 3.0);
            double Relative = std::fabs(R.Volume - Ideal) / Ideal;
            Check("pentagon top fillet is a closed solid", R.Solid());
            Check("pentagon top fillet is within 1% of the ideal roll (mitred ends)", Relative < 0.01);
            std::printf("        volume %.5f  ideal %.5f  shortfall %.3f%% (chord-flat mitre ends)\n", R.Volume, Ideal, Relative * 100.0);
        }
    }

    // ---- 3. Face push, and a blend on the pushed result ---------------------------------------------------------
    {
        Workplane Plane;
        Deliver<NurbsCurve> Profile = NurbsCurve::Polygon(Plane, { 0, 0 }, 20, 6, 0.0, true);
        Deliver<BrepBody> Prism = BrepBody::Extrude(Profile.Payload, { 0, 0, 1 }, 10.0);
        double Base = Prism.Payload.Validate().Volume;

        // Face 1 of the hexagonal prism is a 20 x 10 wall, so pushing it 26 adds exactly 20*10*26.
        // PushFace pulls the tool outline in by ~1e-6 of the body so the boolean sees a transversal contact rather
        //    than coincident faces, which it refuses. That shows up as a relative volume deficit of the same order.
        Deliver<BrepBody> Pushed = BlendSolver::PushFace(Prism.Payload, 1, 26.0);
        double Ideal = Base + 20.0 * 10.0 * 26.0;
        CheckSolid("hex prism push face 1 by 26", Pushed, Ideal, Ideal * 1e-5);

        if (Pushed)
        {
            // The vertical edge at the tip of the new arm — a corner made by the push, blended afterwards.
            int Tip = -1;
            for (size_t E = 0; E < Pushed.Payload.Edges.size(); ++E)
            {
                const BrepEdge& Edge = Pushed.Payload.Edges[E];
                if (Edge.Coedges.size() != 2 || Edge.VertexStart < 0) continue;
                Vec3 A = Pushed.Payload.Vertices[Edge.VertexStart].Point, B = Pushed.Payload.Vertices[Edge.VertexEnd].Point;
                if (A.Y > 43.0 && B.Y > 43.0 && std::fabs(A.Z - B.Z) > 9.0) { Tip = (int)E; break; }
            }
            Check("pushed arm has a blendable tip edge", Tip >= 0);
            if (Tip >= 0)
            {
                double Volume = Pushed.Payload.Validate().Volume;
                EdgeCornerFrame F; std::string Why;
                Check("tip edge yields a corner frame", BlendSolver::Frame(Pushed.Payload, Tip, F, Why), Why.c_str());
                CheckSolid("pushed arm chamfer s=4", BlendSolver::ChamferEdge(Pushed.Payload, Tip, 4.0), Volume - BlendSolver::ChamferRemoval(F, 4.0), 1e-3);

                Deliver<BrepBody> Roll = BlendSolver::FilletEdge(Pushed.Payload, Tip, 4.0);
                if (!Roll) { ++Failures; std::printf("  FAIL  pushed arm fillet — refused: %s\n", Roll.Denial.Detail); }
                else
                {
                    BodyReport R = Roll.Payload.Validate();
                    Check("pushed arm fillet is a closed solid", R.Solid());
                    Check("pushed arm fillet within 1% of the ideal roll", std::fabs(R.Volume - (Volume - BlendSolver::FilletRemoval(F, 4.0))) / Volume < 0.01);
                }
            }
        }
    }

    // ---- 4. Reflex handle root: the re-entrant 210° material edge where the pushed handle meets the head --------
    //    Unlike an exterior edge, a blend at this root fills the small void wedge.  The checks deliberately verify
    //    the clean rebuilt topology and the exact circular boundary; `Solid()` alone formerly let a visibly pinched
    //    Boolean result pass here.  This is the edge rendered by Phase21_Blends.arc section 4.
    {
        Workplane Plane;
        Deliver<NurbsCurve> Profile = NurbsCurve::Polygon(Plane, { 0, 0 }, 20, 6, 0.0, true);
        Deliver<BrepBody> Prism = BrepBody::Extrude(Profile.Payload, { 0, 0, 1 }, 10.0);
        Deliver<BrepBody> Spanner = BlendSolver::PushFace(Prism.Payload, 1, 26.0);
        int Root = -1;
        for (size_t E = 0; E < Spanner.Payload.Edges.size(); ++E)
            if (IsReflexHandleRoot(Spanner.Payload, Spanner.Payload.Edges[E])) { Root = (int)E; break; }
        Check("pushed spanner has its re-entrant handle-root edge", Root >= 0);
        if (Root >= 0)
        {
            EdgeCornerFrame F; std::string Why;
            Check("handle-root edge yields a blend frame", BlendSolver::Frame(Spanner.Payload, Root, F, Why), Why.c_str());
            double Base = Spanner.Payload.Validate().Volume;
            Deliver<BrepBody> Flat = BlendSolver::ChamferEdge(Spanner.Payload, Root, 4.0);
            CheckSolid("re-entrant root chamfer fills its 210-degree wedge", Flat,
                       Base + BlendSolver::ChamferRemoval(F, 4.0), 1e-6);
            Check("re-entrant chamfer is one clean added side face",
                  Flat && Flat.Payload.Faces.size() == 11 && Flat.Payload.Edges.size() == 27);

            Deliver<BrepBody> Roll = BlendSolver::FilletEdge(Spanner.Payload, Root, 4.0);
            // Validate() integrates the curved face through its display tessellation; 0.002 mm³ is 1.3e-7 of this
            // body and guards the analytic circular-wedge result without pretending that a tessellated integral is
            // symbolic arithmetic.
            CheckSolid("re-entrant root fillet fills its tangent circular wedge", Roll,
                       Base + BlendSolver::FilletRemoval(F, 4.0), 2e-3);
            Check("re-entrant fillet has one cylindrical root face and two exact R4 cap arcs",
                  Roll && Roll.Payload.Faces.size() == 11 && Roll.Payload.Edges.size() == 27 && ExactArcEdges(Roll.Payload, 4.0) == 2);
        }
    }

    // ---- 5. Refusals are reasoned, not silent -------------------------------------------------------------------
    {
        Deliver<BrepBody> Box = BrepBody::Box({ 0, 0, 0 }, { 20, 20, 20 });
        Check("chamfer refuses a zero set-back",  !BlendSolver::ChamferEdge(Box.Payload, 0, 0.0));
        Check("fillet refuses a negative radius", !BlendSolver::FilletEdge(Box.Payload, 0, -1.0));
        Check("blend refuses an out-of-range edge", !BlendSolver::ChamferEdge(Box.Payload, 999, 1.0));
        Check("push refuses a zero distance",     !BlendSolver::PushFace(Box.Payload, 0, 0.0));
        Check("push refuses an out-of-range face", !BlendSolver::PushFace(Box.Payload, 99, 1.0));
    }

    // ---- 6. Every edge of the pushed spanner: a blend must either work or refuse, never return a broken body ----
    //    This is the sweep that caught the real defect: a single fixed cutter size refused 14 of 30 edges and
    //    over-cut others by 1107 mm3 (the cut plane running past the edge's ends into the next face).
    {
        Workplane Plane;
        Deliver<NurbsCurve> Profile = NurbsCurve::Polygon(Plane, { 0, 0 }, 20, 6, 0.0, true);
        Deliver<BrepBody> Prism = BrepBody::Extrude(Profile.Payload, { 0, 0, 1 }, 10.0);
        Deliver<BrepBody> Spanner = BlendSolver::PushFace(Prism.Payload, 1, 26.0);
        double Base = Spanner.Payload.Validate().Volume;

        int Blendable = 0, Chamfered = 0, Rolled = 0, Broken = 0, Exact = 0, KernelExact = 0;
        double WorstChamfer = 0.0;
        for (size_t E = 0; E < Spanner.Payload.Edges.size(); ++E)
        {
            EdgeCornerFrame F; std::string Why;
            if (!BlendSolver::Frame(Spanner.Payload, (int)E, F, Why)) continue;
            ++Blendable;

            Deliver<BrepBody> Flat = BlendSolver::ChamferEdge(Spanner.Payload, (int)E, 3.0);
            if (Flat)
            {
                if (!Flat.Payload.Validate().Solid()) ++Broken;
                else
                {
                    ++Chamfered;
                    // The one 210° handle root is a reflex profile blend: it fills, rather than removes, the
                    // local wedge.  Keep it in the sweep so a future Boolean fallback cannot quietly regress it.
                    bool Reflex = IsReflexHandleRoot(Spanner.Payload, Spanner.Payload.Edges[E]);
                    double Expected = Base + (Reflex ? 1.0 : -1.0) * BlendSolver::ChamferRemoval(F, 3.0);
                    double Error = std::fabs(Flat.Payload.Validate().Volume - Expected);
                    if (Error < 1e-6) ++Exact;
                    // The Boolean's tessellated volume integral has a low-micron numerical floor after a direct face
                    // push.  This is still 1.5e-10 of the body, not the old "near exact" 1% fallback.
                    if (Error < 3e-6) ++KernelExact;
                    if (Error > WorstChamfer) WorstChamfer = Error;
                }
            }
            Deliver<BrepBody> Roll = BlendSolver::FilletEdge(Spanner.Payload, (int)E, 3.0);
            if (Roll) { if (!Roll.Payload.Validate().Solid()) ++Broken; else ++Rolled; }
        }
        std::printf("        spanner sweep: %d physical blendable edges, %d chamfered (%d exact, %d kernel-precise), %d filleted, worst chamfer error %.4f\n",
                    Blendable, Chamfered, Exact, KernelExact, Rolled, WorstChamfer);
        Check("no blend ever returns a broken (non-solid) body", Broken == 0);
        // A full planar face push has 26 boundary edges; the two root joins that lie in the top and bottom planes are
        // tangent seams rather than corners, so Frame correctly excludes them.  Every actual corner must blend.
        Check("all physical spanner corners chamfer", Chamfered == Blendable);
        Check("all physical spanner corners fillet",  Rolled == Blendable);
        Check("the full-face push leaves 24 physical blend corners", Blendable == 24);
        Check("every physical spanner chamfer reaches the numerical integration floor", KernelExact == Blendable);
        Check("no accepted spanner chamfer exceeds 3 cubic microns of wedge error", WorstChamfer < 3e-6);
    }

    std::printf(Failures ? "\nBlendVerification: %d FAILED\n" : "\nBlendVerification: all checks passed\n", Failures);
    return Failures ? 1 : 0;
}
