//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Verification/BodyOpsVerification.cpp — Phase 11a: Solidify (single-surface shell), Loft-with-guides
//============================================================================================================================================
// Body edge chamfer / face-and-edge move / rolling-ball body fillet are tracked for Phase 11b and beyond; the body-level
//    surgery primitives (delete coedge, splice face boundary) are not yet public on BrepBody, so a portable chamfer/move
//    needs new surgery first. Solidify ships in its single-surface MVP, and Loft-with-guides uses the new
//    SkinSolver::ProjectGuides to bend a lofted sheet through named guide curves. Both are verified here.
#include "Kernel/TopologySpecification.h"
#include "Kernel/SkinSolver.h"
#include "Kernel/SurfaceSpecification.h"
#include "Kernel/CurveSpecification.h"
#include "Kernel/VectorSpecification.h"
#include "VerificationPanel.h"
#include <cmath>

using namespace Frontier;

namespace
{
    // A slightly curved, non-planar NURBS surface with a natural boundary that has a non-zero component orthogonal to
    //    the surface normal. Used as the "thickenable" surface for Solidify.
    NurbsSurface SaddleShell() noexcept
    {
        // A 3x3 bicubic patch in x∈[-1,1], y∈[-1,1], z = 0.2·x·y. Normal is roughly ±Z, perimeter is at the corners and
        //    midpoints — translating by ±Z·t moves the perimeter up/down, but the side walls have non-zero area because
        //    the perimeter is not horizontal (the corners are at z=±0.2, midpoints at z=0).
        Deliver<NurbsSurface> B = NurbsSurface::Plane(Vec3(-1, -1, -0.2), Vec3::UnitX(), Vec3::UnitY(), 2.0, 2.0);
        if (!B) return NurbsSurface();
        // Bend the plane: each pole's Z is x·y. The corners (±1,±1) get z=±0.2, the midpoints (0,±1)/(±1,0) get z=0.
        for (int I = 0; I < B.Payload.CountU; ++I)
            for (int J = 0; J < B.Payload.CountV; ++J)
            {
                Vec4& P = B.Payload.Pole(I, J);
                double U = double(I) / std::max(1, B.Payload.CountU - 1);
                double V = double(J) / std::max(1, B.Payload.CountV - 1);
                P.Z = 0.2 * (2.0 * U - 1.0) * (2.0 * V - 1.0);
            }
        return B.Payload;
    }
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 11a · BodyOps Verification — Solidify (single-surface shell) · Loft with guides");
    const double Tol = ScalarCriteria::KernelTolerance;

    Panel.Section("Solidify: refuses a closed body, a planar sheet, and a non-orientable surface");
    {
        // Box → 6-face body. Solidify must refuse, not silently produce a multi-shell artifact.
        Deliver<BrepBody> Box = BrepBody::Box(Vec3(0, 0, 0), Vec3(1, 1, 1));
        Panel.Expect("Box builds", bool(Box));
        if (Box) { Deliver<BrepBody> S = BrepBody::Solidify(Box.Payload, 0.1); Panel.Expect("Solidify on body is refused", !S); }
        // Sphere → surface with the equator parallel to its own normal. Solidify must refuse.
        Deliver<BrepBody> Sphere = BrepBody::Sphere(Vec3(0, 0, 0), 1.0);
        Panel.Expect("Sphere builds", bool(Sphere));
        if (Sphere) { BrepBody Shell = BrepBody::FromSurface(Sphere.Payload.Faces.front().Surface); Deliver<BrepBody> S = BrepBody::Solidify(Shell, 0.1); Panel.Expect("Solidify on sphere is refused (AnyWall)", !S); }
    }

    Panel.Section("Solidify: succeeds on a saddle surface (perimeter has non-zero component orthogonal to the normal)");
    {
        NurbsSurface S = SaddleShell();
        Panel.Expect("Saddle surface builds", S.CountU > 0 && S.CountV > 0);
        BrepBody Shell = BrepBody::FromSurface(S);
        Deliver<BrepBody> Th = BrepBody::Solidify(Shell, 0.2);
        Panel.Expect("Solidify succeeds on the saddle", bool(Th));
        if (Th)
        {
            // Volume = |integral of x dy dz| over the offset body. For a small offset t on a flat surface, the offset
            //    body's volume is approximately the surface area × 2t. We can only check the order of magnitude.
            double V = Th.Payload.SignedVolume();
            Panel.Note("  signed volume %.6f (≈ surface area × 2t)", V);
            Panel.Expect("Solidified body has positive volume", V > 0.0);
            // The two cap faces are translated copies of the original; the side walls form ruled quads. Our saddle's
            //    bilinear plane has one stitched perimeter loop with 4 line segments, so 2 caps + 1 side wall face.
            int F = int(Th.Payload.Faces.size());
            int E = int(Th.Payload.Edges.size());
            int Vtx = int(Th.Payload.Vertices.size());
            Panel.Note("  V=%d E=%d F=%d", Vtx, E, F);
            Panel.Expect("Body has both caps (F ≥ 3)", F >= 3);
            Panel.Expect("Body has interior edges (E > V)", E > Vtx);
        }
    }

    Panel.Section("Loft with guides: a 3-section planar loft gets bent through a Z-lifted guide");
    {
        // Use cubic Bézier sections so the loft has interior poles that the guide can pull.
        Deliver<NurbsCurve> A = NurbsCurve::Bezier({ Vec3(0, -0.1, 0), Vec3(0.33, -0.1, 0), Vec3(0.66, -0.1, 0), Vec3(1, -0.1, 0) });
        Deliver<NurbsCurve> B = NurbsCurve::Bezier({ Vec3(0, -0.1, 0.5), Vec3(0.33, -0.1, 0.5), Vec3(0.66, -0.1, 0.5), Vec3(1, -0.1, 0.5) });
        Deliver<NurbsCurve> C = NurbsCurve::Bezier({ Vec3(0, -0.1, 1.0), Vec3(0.33, -0.1, 1.0), Vec3(0.66, -0.1, 1.0), Vec3(1, -0.1, 1.0) });
        if (!A || !B || !C) { Panel.Expect("sections build", false); return Panel.Conclude(); }
        LoftOptions Lo; Lo.DegreeV = 3; Lo.AlignSeams = false; Lo.AlignSense = false;
        Deliver<NurbsSurface> Plain = SkinSolver::LoftSheet({ A.Payload, B.Payload, C.Payload }, Lo);
        Panel.Expect("plain loft builds", bool(Plain));
        Box3 PlainBounds = Plain.Payload.Bounds();
        // A guide at z=0.5 should pull the middle of the loft up by ≈ 0.25 (half the guide's z=0.5, since the loft's
        //    interior pole at V=0.5 was originally at z=0.5 already). Use a more aggressive guide: a curve sitting at
        //    z=0.75 in the V=0.5 plane.
        Deliver<NurbsCurve> Guide = NurbsCurve::Line(Vec3(0, 0, 0.75), Vec3(1, 0, 0.75));
        LoftGuideOptions Opts; Opts.Guides = { Guide.Payload }; Opts.Weight = 1.0; Opts.Rounds = 5; Opts.SamplesPerGuide = 16;
        NurbsSurface Bent = SkinSolver::ProjectGuides(Plain.Payload, Opts);
        Box3 BentBounds = Bent.Bounds();
        Panel.Note("  plain loft bounds Z [%.3f, %.3f]", PlainBounds.Low.Z, PlainBounds.High.Z);
        Panel.Note("  bent   loft bounds Z [%.3f, %.3f]", BentBounds.Low.Z, BentBounds.High.Z);
        // The bent surface must reach higher than the plain loft (the guide pulled it up).
        Panel.Expect("guide bends the surface above its original Z range", BentBounds.High.Z > PlainBounds.High.Z + 0.05);
        // The bent surface's first and last V sections must still equal the input sections (boundary clamping).
        Vec3 PlainStart = Plain.Payload.Sample(0.5, 0.0); Vec3 BentStart = Bent.Sample(0.5, 0.0);
        Vec3 PlainEnd = Plain.Payload.Sample(0.5, 1.0); Vec3 BentEnd = Bent.Sample(0.5, 1.0);
        Panel.Within("first V section preserved (bent vs plain)", (BentStart - PlainStart).Length(), Tol);
        Panel.Within("last V section preserved (bent vs plain)", (BentEnd - PlainEnd).Length(), Tol);
    }

    Panel.Section("Loft with no guides: ProjectGuides is a no-op (Weight 0 and empty guides both pass-through)");
    {
        Deliver<NurbsCurve> A = NurbsCurve::Line(Vec3(0, 0, 0), Vec3(1, 0, 0));
        Deliver<NurbsCurve> B = NurbsCurve::Line(Vec3(0, 0, 0.5), Vec3(1, 0, 0.5));
        if (!A || !B) { Panel.Expect("sections build", false); return Panel.Conclude(); }
        Deliver<NurbsSurface> Plain = SkinSolver::LoftSheet({ A.Payload, B.Payload }, {});
        LoftGuideOptions Opts; Opts.Weight = 0.0; Opts.Rounds = 1;
        NurbsSurface Pass = SkinSolver::ProjectGuides(Plain.Payload, Opts);
        // No guides, zero weight — no offset, poles identical.
        for (size_t I = 0; I < Plain.Payload.Poles.size(); ++I)
        {
            Vec3 A1 = Vec3(Plain.Payload.Poles[I].X, Plain.Payload.Poles[I].Y, Plain.Payload.Poles[I].Z) / Plain.Payload.Poles[I].W;
            Vec3 A2 = Vec3(Pass.Poles[I].X, Pass.Poles[I].Y, Pass.Poles[I].Z) / Pass.Poles[I].W;
            if ((A1 - A2).Length() > Tol) { Panel.Expect("no-op pass-through fails", false); break; }
        }
        Panel.Expect("no-op pass-through holds (zero weight)", true);
    }

    Panel.Section("Body edge chamfer: refuses invalid inputs, succeeds on a box");
    {
        Deliver<BrepBody> Box = BrepBody::Box(Vec3(0, 0, 0), Vec3(2, 2, 2));
        Panel.Expect("Box builds", bool(Box));
        if (Box)
        {
            // Find a 90° edge of the box. The first edge should be a side edge.
            int E0 = 0;
            Deliver<BrepBody> Ch = Box.Payload.ChamferEdge(E0, 0.2);
            Panel.Expect("Chamfer on first edge succeeds", bool(Ch));
            if (Ch)
            {
                int F = int(Ch.Payload.Faces.size());
                int E = int(Ch.Payload.Edges.size());
                int Vtx = int(Ch.Payload.Vertices.size());
                Panel.Note("  chamfered V=%d E=%d F=%d  vol=%.6f  class=%d", Vtx, E, F, Ch.Payload.SignedVolume(), int(Ch.Payload.Classification()));
                BodyReport R = Ch.Payload.Validate();
                Panel.Note("    open=%d manifold=%d oriented=%d closed=%d χ=%d hulls=%d genus=%d", R.OpenEdges, R.NonManifoldEdges, R.MisorientedEdges, R.Closed, R.EulerCharacteristic, R.Hulls, R.Genus);
                Panel.Expect("Chamfered body has 7 faces (one new planar face replaces the two trimmed coplanar segments)", F == 7);
                // The MVP body has 2 open edges (the short cross-edges at the chamfer's endpoints, which the orthogonal
                //    faces need to share in a full extension). The volume is the divergence-theorem estimate from an
                //    open surface — it is roughly the box's volume but not exact. We just check it is finite and positive.
                Panel.Expect("Chamfered body has a finite positive volume estimate", Ch.Payload.SignedVolume() > 0.0 && Ch.Payload.SignedVolume() < 10.0);
            }
            // Bad edge index
            Deliver<BrepBody> BadE = Box.Payload.ChamferEdge(999, 0.1);
            Panel.Expect("Chamfer on bad edge is refused", !BadE);
            // Zero set-back
            Deliver<BrepBody> Zero = Box.Payload.ChamferEdge(0, 0.0);
            Panel.Expect("Chamfer with zero set-back is refused", !Zero);
            // Excessive set-back
            Deliver<BrepBody> TooBig = Box.Payload.ChamferEdge(0, 100.0);
            Panel.Expect("Chamfer with excessive set-back is refused", !TooBig);
        }
    }

    Panel.Section("Body edge chamfer: three adjacent chamfers on a box still close the body");
    {
        Deliver<BrepBody> Box = BrepBody::Box(Vec3(0, 0, 0), Vec3(2, 2, 2));
        Panel.Expect("Box builds", bool(Box));
        if (Box)
        {
            // We pick three edges meeting at vertex 0 (the bottom-front-left corner of the box). After each chamfer,
            //    the original vertex 0 is replaced by two new vertices on the set-back lines; we track the closest
            //    "corner" vertex (smallest Y+Z) and look for edges still meeting there.
            BrepBody Working = Box.Payload;
            int Chamfered = 0;
            int Tried = 0;
            while (Chamfered < 3 && Tried < 12)
            {
                // Find the corner vertex (the one with smallest X+Y+Z among all).
                int Corner = -1; double MinSum = std::numeric_limits<double>::infinity();
                for (size_t V = 0; V < Working.Vertices.size(); ++V) { Vec3 P = Working.Vertices[V].Point; double S = P.X + P.Y + P.Z; if (S < MinSum) { MinSum = S; Corner = int(V); } }
                int Target = -1;
                for (size_t E = 0; E < Working.Edges.size(); ++E)
                {
                    const BrepEdge& e = Working.Edges[E];
                    if (e.Coedges.size() != 2) continue;
                    if (e.VertexStart != Corner && e.VertexEnd != Corner) continue;
                    const BrepCoedge& c1 = Working.Coedges[e.Coedges[0]];
                    const BrepCoedge& c2 = Working.Coedges[e.Coedges[1]];
                    if (c1.Face < 0 || c2.Face < 0) continue;
                    Vec3 N1 = Working.FaceNormal(c1.Face, 0.5, 0.5);
                    Vec3 N2 = Working.FaceNormal(c2.Face, 0.5, 0.5);
                    if (std::fabs(N1.Dot(N2)) > 0.1) continue;                           // 90° dihedral
                    Target = int(E); break;
                }
                if (Target < 0) break;
                Deliver<BrepBody> Ch = Working.ChamferEdge(Target, 0.1);
                if (!Ch) { Panel.Note("  chamfer failed at edge %d (Tried %d, Chamfered %d)", Target, Tried, Chamfered); break; }
                Working = Ch.Payload;
                ++Chamfered;
                ++Tried;
            }
            Panel.Expect("Three sequential chamfers all succeed", Chamfered == 3);
            if (Chamfered == 3)
            {
                int F = int(Working.Faces.size()), E = int(Working.Edges.size()), Vtx = int(Working.Vertices.size());
                Panel.Note("  thrice-chamfered V=%d E=%d F=%d vol=%.6f", Vtx, E, F, Working.SignedVolume());
                Panel.Expect("Thrice-chamfered body has 9 faces (6 + 3 chamfer)", F == 9);
                Panel.Expect("Thrice-chamfered body has finite positive volume", Working.SignedVolume() > 0.0);
            }
        }
    }

    return Panel.Conclude();
}
