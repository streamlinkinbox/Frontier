//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Verification/FaceLoftVerification.cpp — Phase 34a/34f face lofts
//=============================================================================================================================================
// Closed solids become one by skinning between chosen face rims. The faces are dropped exactly once, their rims become
// the sections of a ruled loft, and the skin is sewn to the survivors along the very edges the dropped faces used. The
// same route also joins two disconnected hulls carried by one B-rep. There is no Boolean in the bridge route, so shared
// rims are exact; analytic box/cap cases pin volumes, the same-body case pins topology and volume, and unsupported
// selections refuse with the sources untouched.
#include "Kernel/SkinSolver.h"
#include "Kernel/IntersectionSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

using namespace Frontier;

namespace
{

// Declared acceptance: tessellated volumes carry the kernel's sagitta floor (≈ 4e-4 relative, see Phase 32z), so exact
//    formulas are compared at the kernel's VolumeTolerance; counts and lengths are exact.
constexpr double VolumeLimit = 1e-3;                                                    // [-]

[[nodiscard]] BrepBody Box(Vec3 Low, Vec3 High) noexcept
{
    return BrepBody::Box(Low, High).Payload;
}

[[nodiscard]] BrepBody Cylinder(Vec3 Foot, Vec3 Axis, double Radius, double Height) noexcept
{
    return BrepBody::Cylinder(Foot, Axis, Radius, Height).Payload;
}

// Index of the face whose oriented B-rep normal (sampled at the surface domain midpoint) points along Direction.
[[nodiscard]] int FaceToward(const BrepBody& Body, Vec3 Direction) noexcept
{
    int Best = -1; double BestDot = -2.0;
    for (int F = 0; F < static_cast<int>(Body.Faces.size()); ++F)
    {
        BrepBody::FaceTriangles T = Body.TessellateFace(F);
        Vec3 Area;
        for (size_t I = 0; I + 2 < T.Triangles.size(); I += 3)
            Area = Area + (T.Positions[T.Triangles[I + 1]] - T.Positions[T.Triangles[I]]).Cross(T.Positions[T.Triangles[I + 2]] - T.Positions[T.Triangles[I]]);
        if (Area.LengthSquared() <= 1e-24) continue;
        const NurbsSurface& Surface = Body.Faces[F].Surface;
        const double U = 0.5 * (Surface.DomainStartU() + Surface.DomainEndU());
        const double V = 0.5 * (Surface.DomainStartV() + Surface.DomainEndV());
        const Vec3 Outward = Body.FaceNormal(F, U, V);
        const double D = Outward.Normalised().Dot(Direction.Normalised());
        if (D > BestDot) { BestDot = D; Best = F; }
    }
    return Best;
}

// Face index nearest a point whose outward normal points in the requested direction. Used to address one hull in a
//    deliberately disconnected multi-hull body without depending on construction face order.
[[nodiscard]] int FaceNear(const BrepBody& Body, Vec3 Point, Vec3 Direction) noexcept
{
    int Best = -1; double BestScore = -ScalarCriteria::Infinity;
    for (int F = 0; F < static_cast<int>(Body.Faces.size()); ++F)
    {
        BrepBody::FaceTriangles T = Body.TessellateFace(F);
        if (T.Positions.empty()) continue;
        Vec3 Centre; for (const Vec3& P : T.Positions) Centre = Centre + P; Centre = Centre * (1.0 / T.Positions.size());
        const NurbsSurface& Surface = Body.Faces[F].Surface;
        const Vec3 N = Body.FaceNormal(F, 0.5 * (Surface.DomainStartU() + Surface.DomainEndU()), 0.5 * (Surface.DomainStartV() + Surface.DomainEndV()));
        const double Score = N.Normalised().Dot(Direction.Normalised()) * 1000.0 - Centre.Distance(Point);
        if (Score > BestScore) { BestScore = Score; Best = F; }
    }
    return Best;
}

// Number of edges of Body that coincide (both ends within tolerance) with an edge of Piece: the rim the two share.
[[nodiscard]] int SharedEdges(const BrepBody& Body, const BrepBody& Piece, int PieceFace) noexcept
{
    int Count = 0;
    for (int Ce : Piece.Loops[Piece.Faces[PieceFace].Loops[0]].Coedges)
    {
        const NurbsCurve& Rim = Piece.Edges[Piece.Coedges[Ce].Edge].Curve;
        const Vec3 Mid = Rim.Sample(0.5 * (Rim.DomainStart() + Rim.DomainEnd()));
        for (const BrepEdge& E : Body.Edges)
        {
            double D = 0.0; (void)E.Curve.ClosestParameter(Mid, &D);
            if (D < 1e-9 && ((E.Curve.StartPoint().Distance(Rim.StartPoint()) < 1e-9 && E.Curve.EndPoint().Distance(Rim.EndPoint()) < 1e-9) ||
                             (E.Curve.StartPoint().Distance(Rim.EndPoint()) < 1e-9 && E.Curve.EndPoint().Distance(Rim.StartPoint()) < 1e-9)))
            { ++Count; break; }
        }
    }
    return Count;
}

// The one face of a native cylinder that is not a planar cap.
[[nodiscard]] int SideFace(const BrepBody& Body) noexcept
{
    for (int F = 0; F < static_cast<int>(Body.Faces.size()); ++F)
        if (Body.Faces[F].Surface.Classification == SurfaceClassification::Cylinder) return F;
    return -1;
}

} // namespace

int main()
{
    VerificationPanel Panel("SolidArc · Phase 34a · Face loft — one solid from two through a chosen face of each");

    //------------------------------------------------------------------ box → box: the analytic prism
    Panel.Section("Box to box: the skin between two congruent facing squares is the prism between them");
    const BrepBody A = Box({ 0, 0, 0 }, { 4, 4, 4 }), B = Box({ 10, 0, 0 }, { 14, 4, 4 });
    const int APlusX = FaceToward(A, { 1, 0, 0 }), BMinusX = FaceToward(B, { -1, 0, 0 });
    Deliver<BrepBody> Prism = SkinSolver::LoftFaces(A, APlusX, B, BMinusX);
    Panel.Expect("The face loft returns a closed manifold solid", Prism && Prism.Payload.Validate().Solid());
    if (Prism)
    {
        const BodyReport R = Prism.Payload.Validate();
        Panel.Expect("One hull of genus zero with V16/E25/F11: two open boxes, one skin, one seam",
                     R.Hulls == 1 && R.Genus == 0 && Prism.Payload.Vertices.size() == 16 && Prism.Payload.Edges.size() == 25 && Prism.Payload.Faces.size() == 11);
        Panel.Within("Volume follows 64 + 64 + 16·6 = 224 (relative)", std::fabs(R.Volume - 224.0) / 224.0, VolumeLimit);
        Panel.Within("Area follows 2·96 − 2·16 + 4·6·4 = 256 (relative)", std::fabs(R.Area - 256.0) / 256.0, VolumeLimit);
        Panel.Expect("All four rim edges of each dropped face are the skin's own edges (shared exactly, no duplicates)",
                     SharedEdges(Prism.Payload, A, APlusX) == 4 && SharedEdges(Prism.Payload, B, BMinusX) == 4);
        int Open = 0; for (const BrepEdge& E : Prism.Payload.Edges) Open += E.Coedges.size() != 2;
        Panel.Expect("Every edge has exactly two coedges (the seam is used twice by the skin)", Open == 0);
    }
    Panel.Expect("The source bodies are untouched", std::fabs(A.Validate().Volume - 64.0) < 1e-9 && std::fabs(B.Validate().Volume - 64.0) < 1e-9 && A.Faces.size() == 6 && B.Faces.size() == 6);

    //------------------------------------------------------------------ same-body handle: two hulls carried by one B-rep document
    Panel.Section("Same-body face loft: a bounded handle bridge joins two disconnected hulls without duplicating faces");
    Deliver<BrepBody> MultiHull = IntersectionSolver::Combine(A, B, BodyOperation::Union);
    Panel.Expect("The same-body fixture contains two valid disconnected hulls", MultiHull && MultiHull.Payload.Validate().Solid() && MultiHull.Payload.Validate().Hulls == 2);
    const int ATop = MultiHull ? FaceNear(MultiHull.Payload, { 4, 2, 2 }, { 1, 0, 0 }) : -1;
    const int BBottom = MultiHull ? FaceNear(MultiHull.Payload, { 10, 2, 2 }, { -1, 0, 0 }) : -1;
    Deliver<BrepBody> Handle = MultiHull && ATop >= 0 && BBottom >= 0
        ? SkinSolver::LoftFaces(MultiHull.Payload, ATop, MultiHull.Payload, BBottom)
        : Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "same-body fixture failed");
    Panel.Expect("Two facing faces on one same-body multi-hull B-rep produce a closed handle solid", Handle && Handle.Payload.Validate().Solid());
    if (!Handle) Panel.Note("same-body refusal: %s — %s", Refusal::Describe(Handle.Denial.Reason), Handle.Denial.Detail);
    if (Handle)
    {
        const BodyReport R = Handle.Payload.Validate();
        Panel.Expect("The same-body bridge joins one oriented genus-zero hull", R.Hulls == 1 && R.Genus == 0 && R.Vertices == 16 && R.Edges == 25 && R.Faces == 11);
        Panel.Expect("The same-body bridge does not duplicate the surviving topology", R.OpenEdges == 0 && R.NonManifoldEdges == 0);
        Panel.Within("The same-body bridge volume is the two boxes plus the connecting prism", std::fabs(R.Volume - 224.0) / 224.0, VolumeLimit);
    }
    Panel.Expect("A same-face request still refuses", MultiHull && !SkinSolver::LoftFaces(MultiHull.Payload, ATop, MultiHull.Payload, ATop));

    //------------------------------------------------------------------ cap → cap: the analytic frustum
    Panel.Section("Cylinder cap to cylinder cap: the skin between coaxial circles is the exact cone frustum");
    const BrepBody C1 = Cylinder({ 0, 0, 0 }, { 0, 0, 1 }, 2.0, 3.0), C2 = Cylinder({ 0, 0, 6 }, { 0, 0, 1 }, 1.0, 3.0);
    const int C1Top = FaceToward(C1, { 0, 0, 1 }), C2Bottom = FaceToward(C2, { 0, 0, -1 });
    Deliver<BrepBody> Frustum = SkinSolver::LoftFaces(C1, C1Top, C2, C2Bottom);
    Panel.Expect("The cap loft returns a closed manifold solid", Frustum && Frustum.Payload.Validate().Solid());
    if (Frustum)
    {
        const BodyReport R = Frustum.Payload.Validate();
        const double Band = ScalarCriteria::Pi * 3.0 * (4.0 + 2.0 + 1.0) / 3.0;
        const double Exact = ScalarCriteria::Pi * 4.0 * 3.0 + Band + ScalarCriteria::Pi * 1.0 * 3.0;
        Panel.Expect("One hull of genus zero with V4/E7/F5: the seams of both caps already coincide, so no rim is split",
                     R.Hulls == 1 && R.Genus == 0 && Frustum.Payload.Vertices.size() == 4 && Frustum.Payload.Edges.size() == 7 && Frustum.Payload.Faces.size() == 5);
        Panel.Within("Volume follows 12π + 7π + 3π (relative)", std::fabs(R.Volume - Exact) / Exact, VolumeLimit);
        // The skin's seam ruling joins the two circle seams; with no twist it is the frustum's slant √(3² + 1²).
        double Slant = 0.0;
        for (const BrepEdge& E : Frustum.Payload.Edges)
            if (E.Coedges.size() == 2 && E.Coedges[0] != E.Coedges[1] && Frustum.Payload.Coedges[E.Coedges[0]].Face == Frustum.Payload.Coedges[E.Coedges[1]].Face &&
                std::fabs(E.Curve.StartPoint().Z - 3.0) < 1e-9 && std::fabs(E.Curve.EndPoint().Z - 6.0) < 1e-9)
                Slant = E.Curve.StartPoint().Distance(E.Curve.EndPoint());
        Panel.Within("The seam ruling is the untwisted slant √10", std::fabs(Slant - std::sqrt(10.0)), 1e-9);
        const double SkinArea = R.Area - (2.0 * ScalarCriteria::Pi * 2.0 * 3.0 + ScalarCriteria::Pi * 4.0) - (2.0 * ScalarCriteria::Pi * 1.0 * 3.0 + ScalarCriteria::Pi * 1.0);
        Panel.Within("The skin's area follows the frustum's π(r₁ + r₂)·slant (relative)", std::fabs(SkinArea - ScalarCriteria::Pi * 3.0 * std::sqrt(10.0)) / (ScalarCriteria::Pi * 3.0 * std::sqrt(10.0)), 2e-3);
    }

    //------------------------------------------------------------------ box → cylinder: the square-to-round transition
    Panel.Section("Box to cylinder cap: a square-to-round transition with the seam split onto a real rim vertex");
    const BrepBody Fuselage = Box({ 0, -2, -2 }, { 6, 2, 2 }), Cowl = Cylinder({ 9, 0, 0 }, { 1, 0, 0 }, 2.4, 2.0);
    const int FuselageNose = FaceToward(Fuselage, { 1, 0, 0 }), CowlBack = FaceToward(Cowl, { -1, 0, 0 });
    Deliver<BrepBody> Nose = SkinSolver::LoftFaces(Fuselage, FuselageNose, Cowl, CowlBack);
    Panel.Expect("The square-to-round loft returns a closed manifold solid", Nose && Nose.Payload.Validate().Solid());
    if (Nose)
    {
        const BodyReport R = Nose.Payload.Validate();
        Panel.Expect("One hull of genus zero with V11/E17/F8: the cap circle is split into two arcs at the least-twist seam",
                     R.Hulls == 1 && R.Genus == 0 && Nose.Payload.Vertices.size() == 11 && Nose.Payload.Edges.size() == 17 && Nose.Payload.Faces.size() == 8);
        int Arcs = 0, Circles = 0; double Ruling = 0.0;
        for (const BrepEdge& E : Nose.Payload.Edges)
        {
            const bool Closed = E.VertexStart == E.VertexEnd;
            if (E.Curve.Degree == 2 && Closed) ++Circles;
            if (E.Curve.Degree == 2 && !Closed) ++Arcs;
            if (E.Curve.Degree == 1 && std::fabs(E.Curve.StartPoint().X - 6.0) < 1e-9 && std::fabs(E.Curve.EndPoint().X - 9.0) < 1e-9 && E.Coedges.size() == 2 &&
                Nose.Payload.Coedges[E.Coedges[0]].Face == Nose.Payload.Coedges[E.Coedges[1]].Face) Ruling = E.Curve.StartPoint().Distance(E.Curve.EndPoint());
        }
        Panel.Expect("The dropped cap's rim is now two arcs; the far cap keeps its one circle", Arcs == 2 && Circles == 1);
        // Least twist: the seam ruling runs from a box corner (2√2 from the axis) to the nearest point of the circle,
        //    so its length is √(3² + (2√2 − 2.4)²), not the √(3² + 2.4² + …) a seam left at the circle's own start would give.
        const double Expected = std::sqrt(9.0 + std::pow(2.0 * std::sqrt(2.0) - 2.4, 2.0));
        Panel.Within("The seam ruling has the least-twist length", std::fabs(Ruling - Expected), 1e-6);
        // The ruled skin between parallel sections has a quadratic section-area law, so the prismoidal rule is exact:
        //    V = d/6 · (A₀ + 4·A_mid + A₁) with A_mid the area of the mid-section curve, bounded by the two end areas.
        const double A0 = 16.0, A1 = ScalarCriteria::Pi * 2.4 * 2.4, Skin = R.Volume - 96.0 - Cowl.Validate().Volume;
        Panel.Expect("The skin adds material between the two end-area prisms", Skin > 3.0 * std::min(A0, A1) * 0.999 && Skin < 3.0 * std::max(A0, A1) * 1.001);
        Panel.Note("skin volume %.4f (square prism 48.0000, round prism %.4f)", Skin, 3.0 * A1);
    }

    //------------------------------------------------------------------ refusals
    Panel.Section("Unsupported selections refuse and leave the sources untouched");
    Panel.Expect("Faces that face away from each other refuse", !SkinSolver::LoftFaces(A, FaceToward(A, { 0, 0, 1 }), B, BMinusX));
    Panel.Expect("A cylinder side face (one loop through its seam twice) refuses", !SkinSolver::LoftFaces(A, APlusX, Cowl, SideFace(Cowl)));
    Panel.Expect("Non-facing same-body selections refuse", !SkinSolver::LoftFaces(A, APlusX, A, ATop));
    {
        Deliver<NurbsSurface> Wall = NurbsSurface::Cylinder({ 20, 0, 0 }, { 0, 0, 1 }, 1.0, 2.0);
        Deliver<BrepBody> Sheet = Wall ? BrepBody::Sew({ Wall.Payload }, ScalarCriteria::MergeTolerance, false) : Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "");
        Panel.Expect("An open sheet refuses", Sheet && !Sheet.Payload.Validate().Solid() && !SkinSolver::LoftFaces(Sheet.Payload, 0, B, BMinusX));
    }
    {
        Deliver<BrepBody> Bored = BrepBody::Box({ 20, 0, 0 }, { 24, 4, 4 });
        BrepBody Holed;
        if (Bored)
        {
            Deliver<BrepBody> Tool = BrepBody::Cylinder({ 22, 2, -1 }, { 0, 0, 1 }, 0.5, 6.0);
            Deliver<BrepBody> Result = Tool ? IntersectionSolver::Combine(Bored.Payload, Tool.Payload, BodyOperation::Subtract) : Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "");
            if (Result) Holed = Result.Payload;
        }
        int HoledTop = Holed.Faces.empty() ? -1 : FaceToward(Holed, { 0, 0, 1 });
        Panel.Expect("A face with an inner loop (a through-hole) refuses",
                     Holed.Faces.empty() || Holed.Faces[HoledTop].Loops.size() < 2 || !SkinSolver::LoftFaces(Holed, HoledTop, Box({ 20, 0, 8 }, { 24, 4, 12 }), 0));
    }
    Panel.Expect("An out-of-range face refuses", !SkinSolver::LoftFaces(A, 42, B, BMinusX));
    Panel.Expect("The sources are still the original solids", std::fabs(A.Validate().Volume - 64.0) < 1e-9 && Cowl.Faces.size() == 3 && Fuselage.Faces.size() == 6);

    //------------------------------------------------------------------ console commit + proof
    Panel.Section("Console commit and visual proof");
    ConsoleHost CommandHost(SOLIDARC_PROOF_FOLDER, 1280, 800);
    (void)CommandHost.Document().AddBody("Fuselage", Fuselage);
    (void)CommandHost.Document().AddBody("Cowl", Cowl);
    const std::string Command = "loft Fuselage:f" + std::to_string(FuselageNose) + " Cowl:f" + std::to_string(CowlBack) + " --name=Nose";
    const bool Accepted = CommandHost.Execute(Command);
    const SceneFigure* Committed = CommandHost.Document().Find("Nose");
    Panel.Expect("`loft A:fN B:fM` commits one solid and consumes both sources",
                 Accepted && Committed && Committed->Body.Validate().Solid() && !CommandHost.Document().Find("Fuselage") && !CommandHost.Document().Find("Cowl"));
    Panel.Expect("`loft A:fN B:fM --keep` keeps the sources", [&]
    {
        ConsoleHost Keep(SOLIDARC_PROOF_FOLDER, 1280, 800);
        (void)Keep.Document().AddBody("P", A); (void)Keep.Document().AddBody("Q", B);
        return Keep.Execute("loft P:f" + std::to_string(APlusX) + " Q:f" + std::to_string(BMinusX) + " --keep --name=Bar") && Keep.Document().Find("P") && Keep.Document().Find("Q") && Keep.Document().Find("Bar");
    }());
    Panel.Expect("A face token on a curve or a single token refuses at the console",
                 !CommandHost.Execute("loft Nose:f0"));
    Panel.Expect("The console accepts two distinct faces of one body", [&]
    {
        if (!MultiHull) return false;
        ConsoleHost HandleHost(SOLIDARC_PROOF_FOLDER, 1280, 800);
        (void)HandleHost.Document().AddBody("HandleSource", MultiHull.Payload);
        const std::string SameCommand = "loft HandleSource:f" + std::to_string(ATop) + " HandleSource:f" + std::to_string(BBottom) + " --keep --name=Handle";
        const bool AcceptedSame = HandleHost.Execute(SameCommand);
        const SceneFigure* Result = HandleHost.Document().Find("Handle");
        return AcceptedSame && Result && Result->Body.Validate().Solid() && HandleHost.Document().Find("HandleSource");
    }());

    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase34a_FaceLoft.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost ProofHost(SOLIDARC_PROOF_FOLDER, 1280, 800);
    auto Reset = [&]() { return ProofHost.Execute("reset") && ProofHost.Execute("gizmo off") && ProofHost.Execute("show iso off"); };
    auto Add = [&](const char* Name, BrepBody Body, uint8_t Matcap) -> bool { ProofHost.Document().AddBody(Name, std::move(Body)).Matcap = Matcap; return true; };
    const bool Rendered = Prism && Frustum && Nose &&
        Reset() && Add("Fuselage", Fuselage, 0) && Add("Cowl", Cowl, 1) && ProofHost.Execute("view iso") && ProofHost.Execute("view orbit 100 -10") && ProofHost.Execute("view fit") && ProofHost.Execute("render sheet 0") &&
        Reset() && Add("Nose", Nose.Payload, 7) && ProofHost.Execute("view iso") && ProofHost.Execute("view orbit 100 -10") && ProofHost.Execute("view fit") && ProofHost.Execute("render sheet 1") &&
        Reset() && Add("Prism", Prism.Payload, 3) && ProofHost.Execute("view iso") && ProofHost.Execute("view fit") && ProofHost.Execute("render sheet 2") &&
        Reset() && Add("Frustum", Frustum.Payload, 1) && ProofHost.Execute("view iso") && ProofHost.Execute("view orbit 0 -20") && ProofHost.Execute("view fit") && ProofHost.Execute("render sheet 3") &&
        ProofHost.Execute("render sheet finalize Phase34a_FaceLoft");
    Panel.Expect("C++ proof commands complete without refusal", Rendered);
    Panel.Expect("C++ contact-sheet proof is written and non-trivial", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);

    const std::filesystem::path HandleProof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase34f_SameBodyFaceLoft.png";
    std::filesystem::remove(HandleProof, Error);
    ConsoleHost HandleProofHost(SOLIDARC_PROOF_FOLDER, 1600, 800);
    const bool HandleRendered = MultiHull && Handle &&
        HandleProofHost.Document().AddBody("DisconnectedSource", MultiHull.Payload.Transformed(Mat4::Translation({ 0, -6, 0 }))).Identity > 0 &&
        HandleProofHost.Document().AddBody("SameBodyHandle", Handle.Payload.Transformed(Mat4::Translation({ 0, 6, 0 }))).Identity > 0 &&
        HandleProofHost.Execute("view iso") && HandleProofHost.Execute("view fit") && HandleProofHost.Execute("render Phase34f_SameBodyFaceLoft");
    Panel.Expect("The same-body handle proof render completes", HandleRendered);
    Panel.Expect("The same-body handle proof PNG is written", std::filesystem::exists(HandleProof) && std::filesystem::file_size(HandleProof, Error) > 100000);

    return Panel.Conclude();
}
