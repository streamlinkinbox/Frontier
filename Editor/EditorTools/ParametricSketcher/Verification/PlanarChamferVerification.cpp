//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Verification/PlanarChamferVerification.cpp — Phase 34c exact planar chamfers by topology
//============================================================================================================================================
// The topological chamfer replaces one straight convex edge, or the whole rim of a planar face, by planar faces whose
// long sides are the set-back lines in the neighbouring faces; nothing is intersected numerically. The exact answers a
// modeller can check: a chamfered edge removes ½·d²·sin θ × (edge length at the wedge centroid), so a 120° prism edge
// loses ½·d²·sin 120°·L — the number the cutter route got wrong — and a cap edge whose end faces lean at 120° loses a
// little more than its length suggests; a bevelled cap of perimeter P and corner angles αᵢ loses P·d²/2 − Σcot(αᵢ/2)·d³/3.
#include "Kernel/ChamferSolver.h"
#include "Kernel/BlendSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

using namespace Frontier;

namespace
{

// Declared acceptance: every result is polyhedral and tessellates exactly, so volumes are compared at 1e-12 relative.
constexpr double ExactLimit = 1e-12;                                                    // [-]

[[nodiscard]] double Volume(const BrepBody& B) noexcept { return B.Validate().Volume; }

// Every face a plane through its poles (degree 1×1, four coplanar poles): the whole body is still polyhedral.
[[nodiscard]] bool Polyhedral(const BrepBody& B) noexcept
{
    for (const BrepFace& F : B.Faces)
    {
        const NurbsSurface& S = F.Surface;
        if (S.DegreeU != 1 || S.DegreeV != 1 || S.Poles.size() != 4) return false;
        const Vec3 O = S.Pole(0, 0).Divide(), U = S.Pole(1, 0).Divide() - O, V = S.Pole(0, 1).Divide() - O;
        if (std::fabs(U.Cross(V).Normalised().Dot(S.Pole(1, 1).Divide() - O)) > 1e-9) return false;
    }
    return true;
}

[[nodiscard]] bool EveryEdgeTwoCoedged(const BrepBody& B) noexcept
{
    for (const BrepEdge& E : B.Edges) if (E.Coedges.size() != 2) return false;
    return true;
}

[[nodiscard]] bool Topology(const BrepBody& B, int V, int E, int F) noexcept
{
    const BodyReport R = B.Validate();
    return R.Solid() && R.Hulls == 1 && R.Genus == 0 && R.Vertices == V && R.Edges == E && R.Faces == F;
}

// Faces whose outward normal is within tolerance of N.
[[nodiscard]] std::vector<int> FacesToward(const BrepBody& B, Vec3 N) noexcept
{
    std::vector<int> Out;
    for (size_t F = 0; F < B.Faces.size(); ++F)
        if (B.FaceNormal(int(F), 0.5, 0.5).Normalised().Dot(N.Normalised()) > 1.0 - 1e-9) Out.push_back(int(F));
    return Out;
}

[[nodiscard]] int EdgeBetween(const BrepBody& B, Vec3 P, Vec3 Q) noexcept
{
    for (size_t E = 0; E < B.Edges.size(); ++E)
    {
        const BrepEdge& Edge = B.Edges[E];
        if (Edge.VertexStart < 0 || Edge.VertexEnd < 0) continue;
        const Vec3 A = B.Vertices[Edge.VertexStart].Point, C = B.Vertices[Edge.VertexEnd].Point;
        if ((A.Distance(P) < 1e-9 && C.Distance(Q) < 1e-9) || (A.Distance(Q) < 1e-9 && C.Distance(P) < 1e-9)) return int(E);
    }
    return -1;
}

[[nodiscard]] std::vector<int> EdgesAtHeight(const BrepBody& B, double Z) noexcept
{
    std::vector<int> Out;
    for (size_t E = 0; E < B.Edges.size(); ++E)
    {
        const BrepEdge& Edge = B.Edges[E];
        if (std::fabs(B.Vertices[Edge.VertexStart].Point.Z - Z) < 1e-9 && std::fabs(B.Vertices[Edge.VertexEnd].Point.Z - Z) < 1e-9) Out.push_back(int(E));
    }
    return Out;
}

// Regular n-gon prism of circumradius R and height H about +Z, first vertex on +X.
[[nodiscard]] Deliver<BrepBody> Prism(int Sides, double R, double H) noexcept
{
    std::vector<Vec3> P;
    for (int I = 0; I < Sides; ++I) { const double A = ScalarCriteria::TwoPi * I / Sides; P.push_back({ R * std::cos(A), R * std::sin(A), 0.0 }); }
    Deliver<NurbsCurve> Poly = NurbsCurve::Polyline(P, true);
    if (!Poly) return Deliver<BrepBody>::Reject(Poly.Denial.Reason, Poly.Denial.Detail);
    return BrepBody::Extrude(Poly.Payload, { 0, 0, 1 }, H);
}

// Bevel of a right prism cap: perimeter P, interior angles Alpha, set-back d → P·d²/2 − Σ cot(αᵢ/2)·d³/3.
[[nodiscard]] double CapBevel(double Perimeter, const std::vector<double>& Alpha, double D) noexcept
{
    double Cot = 0.0;
    for (double A : Alpha) Cot += 1.0 / std::tan(0.5 * A);
    return Perimeter * D * D / 2.0 - Cot * D * D * D / 3.0;
}

} // namespace

int main()
{
    VerificationPanel Panel("SolidArc · Phase 34c · exact planar chamfers by topology — one edge of any dihedral angle, and a planar face's rim");
    const double D = 0.4;                                                               // [m] set-back throughout

    //------------------------------------------------------------------ box edges
    Panel.Section("A box edge: the chamfer face is exact, the neighbours re-trim, and every one of the twelve edges works");
    const BrepBody Box = BrepBody::Box({ 0, 0, 0 }, { 4, 3, 2 }).Payload;
    const int FrontTop = EdgeBetween(Box, { 0, 0, 2 }, { 4, 0, 2 });
    Deliver<BrepBody> One = ChamferSolver::ChamferEdge(Box, FrontTop, D);
    Panel.Expect("The front top edge (length 4) chamfers into a closed manifold genus-zero solid", One && One.Payload.Validate().Solid());
    if (One)
    {
        Panel.Expect("Topology is V10/E15/F7 — two vertices, three edges and one face more, every edge two-coedged", Topology(One.Payload, 10, 15, 7) && EveryEdgeTwoCoedged(One.Payload));
        Panel.Within("Volume is 24 − ½·d²·4 exactly (relative)", std::fabs(Volume(One.Payload) - (24.0 - 0.5 * D * D * 4.0)) / 24.0, ExactLimit);
        Panel.Expect("All seven faces are planes through their poles", Polyhedral(One.Payload));
        const std::vector<int> Bevel = FacesToward(One.Payload, { 0, -1, 1 });
        Panel.Expect("Exactly one face faces the bisector (0, −1, 1)/√2 — the chamfer plane", Bevel.size() == 1);
        if (Bevel.size() == 1)
        {
            const BrepBody::FaceTriangles T = One.Payload.TessellateFace(Bevel[0]);
            double Area = 0.0;
            for (size_t I = 0; I + 2 < T.Triangles.size(); I += 3)
                Area += 0.5 * (T.Positions[T.Triangles[I + 1]] - T.Positions[T.Triangles[I]]).Cross(T.Positions[T.Triangles[I + 2]] - T.Positions[T.Triangles[I]]).Length();
            Panel.Within("Its area is d·√2 × 4 (relative)", std::fabs(Area - D * std::sqrt(2.0) * 4.0) / (D * std::sqrt(2.0) * 4.0), 1e-9);
        }
        Panel.Expect("The source box is untouched (V8/E12/F6, volume 24)", Topology(Box, 8, 12, 6) && std::fabs(Volume(Box) - 24.0) < 1e-12);
    }
    {
        int Exact = 0;
        for (size_t E = 0; E < Box.Edges.size(); ++E)
        {
            const BrepEdge& Edge = Box.Edges[E];
            const double L = Box.Vertices[Edge.VertexStart].Point.Distance(Box.Vertices[Edge.VertexEnd].Point);
            Deliver<BrepBody> R = ChamferSolver::ChamferEdge(Box, int(E), D);
            if (R && Topology(R.Payload, 10, 15, 7) && Polyhedral(R.Payload) && std::fabs(Volume(R.Payload) - (24.0 - 0.5 * D * D * L)) / 24.0 < ExactLimit) ++Exact;
        }
        Panel.Equal("All twelve box edges chamfer exactly, whichever way their loops run", double(Exact), 12.0, 0.0);
    }
    Deliver<BrepBody> Twice = One ? ChamferSolver::ChamferEdge(One.Payload, EdgeBetween(One.Payload, { 0, 3, 2 }, { 4, 3, 2 }), D) : One;
    Panel.Expect("A second edge on the already-chamfered box chamfers too (the cutter route refused this)", Twice && Topology(Twice.Payload, 12, 18, 8));
    if (Twice) Panel.Within("Its volume is 24 − 2·½·d²·4 (relative)", std::fabs(Volume(Twice.Payload) - (24.0 - D * D * 4.0)) / 24.0, ExactLimit);

    //------------------------------------------------------------------ prism edges
    Panel.Section("A hexagonal prism: the 120° vertical edge the cutter route got wrong, and a cap edge with leaning end faces");
    Deliver<BrepBody> Hex = Prism(6, 2.0, 2.0);
    const double HexVolume = 6.0 * std::sqrt(3.0) * 2.0;
    Panel.Expect("A hexagonal prism of side 2 and height 2 is the fixture (V12/E18/F8, volume 12√3)", Hex && Topology(Hex.Payload, 12, 18, 8) && std::fabs(Volume(Hex.Payload) - HexVolume) < 1e-12);
    if (Hex)
    {
        const int Vertical = EdgeBetween(Hex.Payload, { 2, 0, 0 }, { 2, 0, 2 });
        Deliver<BrepBody> Side = ChamferSolver::ChamferEdge(Hex.Payload, Vertical, D);
        const double Wedge120 = 0.5 * D * D * std::sin(ScalarCriteria::Radians(120.0)) * 2.0;
        Panel.Expect("The vertical edge (dihedral 120°) chamfers into a genus-zero V14/E21/F9 solid", Side && Topology(Side.Payload, 14, 21, 9) && Polyhedral(Side.Payload));
        if (Side) Panel.Within("It removes ½·d²·sin 120° × 2 — the 120° wedge, not the 90° one (relative)", std::fabs(Volume(Side.Payload) - (HexVolume - Wedge120)) / HexVolume, ExactLimit);
        Panel.Note("Scratchpad/Phase34b/Repro_HexChamfer.arc expected 20.646046; measured %.9f", Side ? Volume(Side.Payload) : 0.0);

        const int CapEdge = EdgeBetween(Hex.Payload, { 2, 0, 2 }, { 1, std::sqrt(3.0), 2 });
        Deliver<BrepBody> Cap = ChamferSolver::ChamferEdge(Hex.Payload, CapEdge, D);
        // The end faces are the neighbouring side faces at 120°: at the wedge centroid (d/3 in from the edge) each end
        //    plane is cot 60°·d/3 further out, so the removed prism is ½·d² × (2 + 2·(d/3)·cot 60°).
        const double CapLength = 2.0 + 2.0 * (D / 3.0) / std::tan(ScalarCriteria::Radians(60.0));
        Panel.Expect("A cap edge (dihedral 90°, end faces leaning at 120°) chamfers into V14/E21/F9", Cap && Topology(Cap.Payload, 14, 21, 9) && Polyhedral(Cap.Payload));
        if (Cap) Panel.Within("It removes ½·d² × the edge length at the wedge centroid, 2 + 2·(d/3)·cot 60° (relative)", std::fabs(Volume(Cap.Payload) - (HexVolume - 0.5 * D * D * CapLength)) / HexVolume, ExactLimit);
        int Exact = 0;
        for (size_t E = 0; E < Hex.Payload.Edges.size(); ++E)
        {
            Deliver<BrepBody> R = ChamferSolver::ChamferEdge(Hex.Payload, int(E), D);
            if (R && Topology(R.Payload, 14, 21, 9) && Polyhedral(R.Payload)) ++Exact;
        }
        Panel.Equal("All eighteen prism edges chamfer into closed polyhedral V14/E21/F9 solids", double(Exact), 18.0, 0.0);
    }

    //------------------------------------------------------------------ rims
    Panel.Section("A planar face's rim: the bevel of a cap with mitred corners, by --face or by the complete edge set");
    Deliver<BrepBody> BoxBevel = ChamferSolver::ChamferFaceRim(Box, FacesToward(Box, { 0, 0, 1 }).front(), D);
    const double BoxCap = CapBevel(14.0, std::vector<double>(4, ScalarCriteria::HalfPi), D);
    Panel.Expect("The box top rim bevels into a genus-zero V12/E20/F10 solid", BoxBevel && Topology(BoxBevel.Payload, 12, 20, 10) && Polyhedral(BoxBevel.Payload) && EveryEdgeTwoCoedged(BoxBevel.Payload));
    if (BoxBevel)
    {
        Panel.Within("It removes P·d²/2 − 4·cot 45°·d³/3 with P = 14 (relative)", std::fabs(Volume(BoxBevel.Payload) - (24.0 - BoxCap)) / 24.0, ExactLimit);
        Panel.Expect("Four chamfer faces face the four bisectors (±1, 0, 1)/√2 and (0, ±1, 1)/√2",
                     FacesToward(BoxBevel.Payload, { 1, 0, 1 }).size() == 1 && FacesToward(BoxBevel.Payload, { -1, 0, 1 }).size() == 1 &&
                     FacesToward(BoxBevel.Payload, { 0, 1, 1 }).size() == 1 && FacesToward(BoxBevel.Payload, { 0, -1, 1 }).size() == 1);
        Panel.Expect("The top face is inset to (4 − 2d) × (3 − 2d) at the same height", [&]
        {
            const int Top = FacesToward(BoxBevel.Payload, { 0, 0, 1 }).front();
            Box3 Bounds; bool Any = false;
            for (int Loop : BoxBevel.Payload.Faces[Top].Loops)
                for (int C : BoxBevel.Payload.Loops[Loop].Coedges)
                {
                    const Vec3 P = BoxBevel.Payload.CoedgeStart(C);
                    if (!Any) { Bounds = Box3{ P, P }; Any = true; }
                    else { Bounds.Low = Vec3::Min(Bounds.Low, P); Bounds.High = Vec3::Max(Bounds.High, P); }
                }
            return Any && std::fabs(Bounds.High.X - Bounds.Low.X - (4.0 - 2.0 * D)) < 1e-12 && std::fabs(Bounds.High.Y - Bounds.Low.Y - (3.0 - 2.0 * D)) < 1e-12 &&
                std::fabs(Bounds.Low.Z - 2.0) < 1e-12 && std::fabs(Bounds.High.Z - 2.0) < 1e-12;
        }());
    }
    {
        int Exact = 0;
        for (size_t F = 0; F < Box.Faces.size(); ++F)
        {
            const Vec3 N = Box.FaceNormal(int(F), 0.5, 0.5);
            const double P = std::fabs(N.Z) > 0.5 ? 14.0 : std::fabs(N.Y) > 0.5 ? 12.0 : 10.0;
            Deliver<BrepBody> R = ChamferSolver::ChamferFaceRim(Box, int(F), D);
            if (R && Topology(R.Payload, 12, 20, 10) && std::fabs(Volume(R.Payload) - (24.0 - CapBevel(P, std::vector<double>(4, ScalarCriteria::HalfPi), D))) / 24.0 < ExactLimit) ++Exact;
        }
        Panel.Equal("All six box faces bevel exactly", double(Exact), 6.0, 0.0);
    }
    if (Hex)
    {
        const int TopFace = FacesToward(Hex.Payload, { 0, 0, 1 }).front();
        const std::vector<int> TopRim = EdgesAtHeight(Hex.Payload, 2.0);
        const double HexCap = CapBevel(12.0, std::vector<double>(6, ScalarCriteria::Radians(120.0)), D);
        Deliver<BrepBody> ByFace = ChamferSolver::ChamferFaceRim(Hex.Payload, TopFace, D);
        Deliver<BrepBody> BySet = ChamferSolver::ChamferEdges(Hex.Payload, TopRim, D);
        Panel.Expect("The hexagon cap bevels by face into a genus-zero V18/E30/F14 solid with six mitre edges", ByFace && Topology(ByFace.Payload, 18, 30, 14) && Polyhedral(ByFace.Payload));
        if (ByFace) Panel.Within("It removes 12·d²/2 − 6·cot 60°·d³/3 (relative)", std::fabs(Volume(ByFace.Payload) - (HexVolume - HexCap)) / HexVolume, ExactLimit);
        Panel.Expect("The same six edges given as a set dispatch to the rim route and give the same solid",
                     BySet && TopRim.size() == 6 && ChamferSolver::RimFace(Hex.Payload, TopRim) == TopFace && Topology(BySet.Payload, 18, 30, 14) &&
                     std::fabs(Volume(BySet.Payload) - Volume(ByFace.Payload)) < 1e-12);
        Deliver<BrepBody> Both = ByFace ? ChamferSolver::ChamferFaceRim(ByFace.Payload, FacesToward(ByFace.Payload, { 0, 0, -1 }).front(), D) : ByFace;
        Panel.Expect("Both caps bevel in turn: V24/E42/F20 with twice the removal", Both && Topology(Both.Payload, 24, 42, 20) && std::fabs(Volume(Both.Payload) - (HexVolume - 2.0 * HexCap)) / HexVolume < ExactLimit);
        const std::vector<int> SideFace = FacesToward(Hex.Payload, { std::cos(ScalarCriteria::Radians(30.0)), std::sin(ScalarCriteria::Radians(30.0)), 0 });
        Deliver<BrepBody> SideRim = SideFace.size() == 1 ? ChamferSolver::ChamferFaceRim(Hex.Payload, SideFace.front(), D)
                                                         : Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "no side face");
        Panel.Expect("A side face's rim refuses: its corners would need a vertex face (two chamfer planes cut one side edge at different points)",
                     SideFace.size() == 1 && !SideRim && std::string(SideRim.Denial.Detail).find("vertex face") != std::string::npos);
    }

    //------------------------------------------------------------------ refusals
    Panel.Section("Refusals leave the source untouched and say why");
    Panel.Expect("Zero set-back and an out-of-range edge refuse", !ChamferSolver::ChamferEdge(Box, FrontTop, 0.0) && !ChamferSolver::ChamferEdge(Box, 99, D));
    Panel.Expect("A set-back that reaches the far end of an adjacent edge refuses", !ChamferSolver::ChamferEdge(Box, FrontTop, 2.0) && !ChamferSolver::ChamferEdge(Box, FrontTop, 3.0));
    Panel.Expect("A set-back that would fold the inset rim over refuses", !ChamferSolver::ChamferFaceRim(Box, FacesToward(Box, { 0, 0, 1 }).front(), 1.6));
    Panel.Expect("Two arbitrary edges are neither one edge nor a rim and refuse", !ChamferSolver::ChamferEdges(Box, { 0, 5 }, D) && !ChamferSolver::ChamferEdges(Box, {}, D));
    {
        // An L-shaped block: the re-entrant vertical edge is concave — a chamfer there would add material.
        std::vector<Vec3> L = { { 0, 0, 0 }, { 4, 0, 0 }, { 4, 1, 0 }, { 1, 1, 0 }, { 1, 3, 0 }, { 0, 3, 0 } };
        Deliver<NurbsCurve> Outline = NurbsCurve::Polyline(L, true);
        Deliver<BrepBody> Angle = Outline ? BrepBody::Extrude(Outline.Payload, { 0, 0, 1 }, 2.0) : Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "outline");
        const int Concave = Angle ? EdgeBetween(Angle.Payload, { 1, 1, 0 }, { 1, 1, 2 }) : -1;
        const int Convex = Angle ? EdgeBetween(Angle.Payload, { 4, 1, 0 }, { 4, 1, 2 }) : -1;
        Deliver<BrepBody> R = Angle ? ChamferSolver::ChamferEdge(Angle.Payload, Concave, D) : Angle;
        Panel.Expect("The re-entrant edge of an L-block refuses as concave, while its convex neighbour chamfers",
                     Angle && Concave >= 0 && !R && std::string(R.Denial.Detail).find("concave") != std::string::npos &&
                     ChamferSolver::ChamferEdge(Angle.Payload, Convex, D));
        Panel.Note("%s", Angle && !R ? R.Denial.Detail : "no angle piece");
    }
    {
        Deliver<BrepBody> Cylinder = BrepBody::Cylinder({ 6, 0, 0 }, { 0, 0, 1 }, 1.0, 2.0);
        Panel.Expect("A cylinder's circular cap rim refuses the planar route (curved edge) and an open sheet refuses",
                     Cylinder && !ChamferSolver::ChamferEdge(Cylinder.Payload, 0, D) &&
                     !ChamferSolver::ChamferEdge(BrepBody::FromSurface(NurbsSurface::Plane({ 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, 1, 1).Payload), 0, D));
    }

    //------------------------------------------------------------------ console + proof
    Panel.Section("Console commit, live re-derivation, and visual proof");
    ConsoleHost CommandHost(SOLIDARC_PROOF_FOLDER, 1280, 800);
    if (Hex)
    {
        (void)CommandHost.Document().AddBody("Prism", Hex.Payload);
        const int Vertical = EdgeBetween(Hex.Payload, { 2, 0, 0 }, { 2, 0, 2 });
        const bool Accepted = CommandHost.Execute("chamfer Prism " + std::to_string(D) + " --edges=" + std::to_string(Vertical) + " --name=One");
        const SceneFigure* Committed = CommandHost.Document().Find("One");
        Panel.Expect("`chamfer <body> d --edges=i` commits the exact 120° chamfer through the planar route and consumes the source",
                     Accepted && Committed && Topology(Committed->Body, 14, 21, 9) && !CommandHost.Document().Find("Prism") &&
                     std::fabs(Volume(Committed->Body) - (HexVolume - 0.5 * D * D * std::sin(ScalarCriteria::Radians(120.0)) * 2.0)) / HexVolume < ExactLimit);
        (void)CommandHost.Document().AddBody("Cap", Hex.Payload);
        const int TopFace = FacesToward(Hex.Payload, { 0, 0, 1 }).front();
        const bool Bevelled = CommandHost.Execute("chamfer Cap " + std::to_string(D) + " --face=" + std::to_string(TopFace) + " --name=Bevel");
        const SceneFigure* Bevel = CommandHost.Document().Find("Bevel");
        Panel.Expect("`chamfer <body> d --face=i` commits the bevelled cap (V18/E30/F14)", Bevelled && Bevel && Topology(Bevel->Body, 18, 30, 14));
        Panel.Expect("`--edges=` with two arbitrary edges and `--edges=` together with `--face=` refuse at the console",
                     !CommandHost.Execute("chamfer Bevel 0.2 --edges=0,1") && !CommandHost.Execute("chamfer Bevel 0.2 --edges=0 --face=0"));
        (void)CommandHost.Document().AddBody("Native", BrepBody::Cylinder({ 6, 0, 0 }, { 0, 0, 1 }, 1.0, 2.0).Payload);
        Panel.Expect("A native cylinder cap rim still takes its own exact route through the same verb",
                     CommandHost.Execute("chamfer Native 0.2 --edges=0 --name=CapChamfer") && CommandHost.Document().Find("CapChamfer") &&
                     CommandHost.Document().Find("CapChamfer")->Body.Validate().Solid());
    }

    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase34c_PlanarChamfer.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost ProofHost(SOLIDARC_PROOF_FOLDER, 1280, 800);
    auto Reset = [&]() { return ProofHost.Execute("reset") && ProofHost.Execute("gizmo off") && ProofHost.Execute("show iso off"); };
    auto Add = [&](const char* Name, BrepBody Body, uint8_t Matcap) -> bool { ProofHost.Document().AddBody(Name, std::move(Body)).Matcap = Matcap; return true; };
    auto Tile = [&](int Index) { return ProofHost.Execute("view iso") && ProofHost.Execute("view orbit 20 -8") && ProofHost.Execute("view fit") && ProofHost.Execute("render sheet " + std::to_string(Index)); };
    Deliver<BrepBody> HexSide = Hex ? ChamferSolver::ChamferEdge(Hex.Payload, EdgeBetween(Hex.Payload, { 2, 0, 0 }, { 2, 0, 2 }), D) : Hex;
    Deliver<BrepBody> HexBevel = Hex ? ChamferSolver::ChamferFaceRim(Hex.Payload, FacesToward(Hex.Payload, { 0, 0, 1 }).front(), D) : Hex;
    Deliver<BrepBody> HexBoth = HexBevel ? ChamferSolver::ChamferFaceRim(HexBevel.Payload, FacesToward(HexBevel.Payload, { 0, 0, -1 }).front(), D) : HexBevel;
    Deliver<BrepBody> BoxAll = BoxBevel ? ChamferSolver::ChamferFaceRim(BoxBevel.Payload, FacesToward(BoxBevel.Payload, { 0, 0, -1 }).front(), D) : BoxBevel;
    const bool Rendered = One && Twice && HexSide && HexBevel && HexBoth && BoxBevel && BoxAll &&
        Reset() && Add("Box", Box, 0) && Add("One", One.Payload.Transformed(Mat4::Translation({ 6, 0, 0 })), 3) && Add("Twice", Twice.Payload.Transformed(Mat4::Translation({ 12, 0, 0 })), 3) && Tile(0) &&
        Reset() && Add("Hex", Hex.Payload, 0) && Add("HexSide", HexSide.Payload.Transformed(Mat4::Translation({ 6, 0, 0 })), 7) && Tile(1) &&
        Reset() && Add("HexBevel", HexBevel.Payload, 5) && Add("HexBoth", HexBoth.Payload.Transformed(Mat4::Translation({ 6, 0, 0 })), 5) && Tile(2) &&
        Reset() && Add("BoxBevel", BoxBevel.Payload, 2) && Add("BoxAll", BoxAll.Payload.Transformed(Mat4::Translation({ 6, 0, 0 })), 2) && Tile(3) &&
        ProofHost.Execute("render sheet finalize Phase34c_PlanarChamfer");
    Panel.Expect("C++ proof commands complete without refusal", Rendered);
    Panel.Expect("C++ contact-sheet proof is written and non-trivial", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);

    return Panel.Conclude();
}
