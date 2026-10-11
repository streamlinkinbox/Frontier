//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Verification/TweakVerification.cpp — Phase 34b face / edge / vertex tweaks on fixed topology
//============================================================================================================================================
// A tweak translates a set of vertices and re-fits only the geometry that touches them, so V/E/F never change and the
// questions a modeller asks about a box have exact answers: moving a face by any vector keeps every face planar (the
// volume follows Cavalieri), moving an edge keeps the faces along it planar and warps the end faces only when the vector
// runs along the edge, and moving a vertex warps the faces around it — refused by default, exact bilinear patches when
// warping is allowed, with the volume pinned by the trilinear hexahedron integral. Prisms cover trimmed n-gon caps.
#include "Kernel/TweakSolver.h"
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

// Declared acceptance. Planar results tessellate exactly, so their volumes are compared at 1e-9 relative. Warped
//    (bilinear) faces are tessellated to the kernel's sagitta with at most 32 subdivisions per span, which leaves the
//    lifted-corner box ≈ 4e-5 below its exact volume; they are compared at the kernel's VolumeTolerance.
constexpr double PlanarLimit = 1e-9;                                                    // [-]
constexpr double WarpLimit = 1e-3;                                                      // [-]

[[nodiscard]] BrepBody Box(Vec3 Low, Vec3 High) noexcept { return BrepBody::Box(Low, High).Payload; }

// Index of the box vertex nearest a corner, the face whose outward normal is N, and the edge between two corners.
[[nodiscard]] int VertexAt(const BrepBody& B, Vec3 P) noexcept
{
    int Best = -1; double BestDistance = 1e-6;
    for (size_t V = 0; V < B.Vertices.size(); ++V)
    {
        double D = B.Vertices[V].Point.Distance(P);
        if (D < BestDistance) { BestDistance = D; Best = int(V); }
    }
    return Best;
}

[[nodiscard]] int FaceToward(const BrepBody& B, Vec3 N) noexcept
{
    int Best = -1; double BestDot = 0.5;
    for (size_t F = 0; F < B.Faces.size(); ++F)
    {
        const NurbsSurface& S = B.Faces[F].Surface;
        double Dot = B.FaceNormal(int(F), 0.5 * (S.DomainStartU() + S.DomainEndU()), 0.5 * (S.DomainStartV() + S.DomainEndV())).Dot(N);
        if (Dot > BestDot) { BestDot = Dot; Best = int(F); }
    }
    return Best;
}

[[nodiscard]] int EdgeBetween(const BrepBody& B, Vec3 P, Vec3 Q) noexcept
{
    for (size_t E = 0; E < B.Edges.size(); ++E)
    {
        Vec3 A = B.Vertices[B.Edges[E].VertexStart].Point, Z = B.Vertices[B.Edges[E].VertexEnd].Point;
        if ((A.Distance(P) < 1e-9 && Z.Distance(Q) < 1e-9) || (A.Distance(Q) < 1e-9 && Z.Distance(P) < 1e-9)) return int(E);
    }
    return -1;
}

[[nodiscard]] int PlanarFaces(const BrepBody& B) noexcept
{
    int N = 0;
    for (const BrepFace& F : B.Faces) if (F.Surface.Classification == SurfaceClassification::Plane) ++N;
    return N;
}

// Faces that warped: the tweak tags a four-sided face Freeform only when its corners left their plane.
[[nodiscard]] int WarpedFaces(const BrepBody& B) noexcept
{
    int N = 0;
    for (const BrepFace& F : B.Faces) if (F.Surface.Classification == SurfaceClassification::Freeform) ++N;
    return N;
}

[[nodiscard]] bool SameTopology(const BrepBody& A, const BrepBody& B) noexcept
{
    if (A.Vertices.size() != B.Vertices.size() || A.Edges.size() != B.Edges.size() || A.Coedges.size() != B.Coedges.size() ||
        A.Loops.size() != B.Loops.size() || A.Faces.size() != B.Faces.size()) return false;
    for (size_t E = 0; E < A.Edges.size(); ++E)
        if (A.Edges[E].VertexStart != B.Edges[E].VertexStart || A.Edges[E].VertexEnd != B.Edges[E].VertexEnd || A.Edges[E].Coedges != B.Edges[E].Coedges) return false;
    for (size_t L = 0; L < A.Loops.size(); ++L) if (A.Loops[L].Coedges != B.Loops[L].Coedges || A.Loops[L].Face != B.Loops[L].Face) return false;
    return true;
}

// Exact volume of the trilinear hexahedron with corners C[i + 2j + 4k] at unit-cube corner (i, j, k). The Jacobian
//    determinant of the trilinear map is at most quadratic in each coordinate, so 2-point Gauss per axis is exact.
[[nodiscard]] double HexahedronVolume(const Vec3 C[8]) noexcept
{
    const double G[2] = { 0.5 - 0.5 / std::sqrt(3.0), 0.5 + 0.5 / std::sqrt(3.0) };
    double Volume = 0.0;
    for (double X : G) for (double Y : G) for (double Z : G)
    {
        Vec3 Dx{}, Dy{}, Dz{};
        for (int K = 0; K < 8; ++K)
        {
            const double I = (K & 1) ? 1.0 : 0.0, J = (K & 2) ? 1.0 : 0.0, L = (K & 4) ? 1.0 : 0.0;
            const double Wi = I ? X : 1.0 - X, Wj = J ? Y : 1.0 - Y, Wk = L ? Z : 1.0 - Z;
            const double Si = I ? 1.0 : -1.0, Sj = J ? 1.0 : -1.0, Sk = L ? 1.0 : -1.0;
            Dx = Dx + C[K] * (Si * Wj * Wk); Dy = Dy + C[K] * (Wi * Sj * Wk); Dz = Dz + C[K] * (Wi * Wj * Sk);
        }
        Volume += Dx.Dot(Dy.Cross(Dz)) * 0.125;                                         // Gauss weights ½ per axis
    }
    return Volume;
}

// Box corners in trilinear order for a box [0,L]×[0,W]×[0,H], with one corner displaced.
void BoxCorners(double L, double W, double H, Vec3 C[8]) noexcept
{
    for (int K = 0; K < 8; ++K) C[K] = { (K & 1) ? L : 0.0, (K & 2) ? W : 0.0, (K & 4) ? H : 0.0 };
}

} // namespace

int main()
{
    VerificationPanel Panel("SolidArc · Phase 34b · Tweak Verification — translate a face, an edge or a vertex of a solid on fixed topology");
    const double L = 4.0, W = 3.0, H = 2.0;
    const BrepBody Source = Box({ 0, 0, 0 }, { L, W, H });
    const int Top = FaceToward(Source, { 0, 0, 1 });
    const int FrontTopEdge = EdgeBetween(Source, { 0, 0, H }, { L, 0, H });
    const int Corner = VertexAt(Source, { L, W, H });

    //------------------------------------------------------------------ faces
    Panel.Section("Moving a face: along its normal and obliquely, every face stays planar and V/E/F never change");
    Deliver<BrepBody> Up = TweakSolver::TranslateFace(Source, Top, { 0, 0, 1.5 }, false);
    Panel.Expect("The top face moves 1.5 along its normal into a closed manifold solid", Up && Up.Payload.Validate().Solid());
    if (Up)
    {
        const BodyReport R = Up.Payload.Validate();
        Panel.Expect("Topology is untouched: V8/E12/F6 with the same edge–vertex incidences", SameTopology(Source, Up.Payload) && R.Vertices == 8 && R.Edges == 12 && R.Faces == 6);
        Panel.Within("Volume is exactly 4·3·3.5 = 42 (relative)", std::fabs(R.Volume - 42.0) / 42.0, PlanarLimit);
        Panel.Expect("All six faces remain tagged Plane", PlanarFaces(Up.Payload) == 6);
        Deliver<BrepBody> Pushed = BlendSolver::PushFace(Source, Top, 1.5);
        Panel.Expect("`push` reaches the same volume through a slab Boolean but keeps the extra faces; the tweak is the true face move",
                     Pushed && std::fabs(Pushed.Payload.Validate().Volume - 42.0) < 1e-6 && Pushed.Payload.Faces.size() > 6);
        Panel.Note("push topology V%zu/E%zu/F%zu · tweak V8/E12/F6", Pushed ? Pushed.Payload.Vertices.size() : 0, Pushed ? Pushed.Payload.Edges.size() : 0, Pushed ? Pushed.Payload.Faces.size() : 0);
    }
    Deliver<BrepBody> Sheared = TweakSolver::TranslateFace(Source, Top, { 1.0, 0.5, 0.0 }, false);
    Panel.Expect("The top face slides in its own plane by (1, 0.5, 0): the sides become parallelograms, still planar", Sheared && Sheared.Payload.Validate().Solid() && PlanarFaces(Sheared.Payload) == 6);
    if (Sheared) Panel.Within("A shear preserves the volume exactly (Cavalieri): 24 (relative)", std::fabs(Sheared.Payload.Validate().Volume - 24.0) / 24.0, PlanarLimit);
    Deliver<BrepBody> Oblique = TweakSolver::TranslateFace(Source, Top, { 1.0, 0.5, 1.5 }, false);
    Panel.Expect("An oblique face move (1, 0.5, 1.5) is accepted with all faces planar", Oblique && Oblique.Payload.Validate().Solid() && PlanarFaces(Oblique.Payload) == 6);
    if (Oblique) Panel.Within("Its volume is the sheared prism's 4·3·3.5 = 42 (relative)", std::fabs(Oblique.Payload.Validate().Volume - 42.0) / 42.0, PlanarLimit);
    Panel.Expect("Two successive tweaks compose (move up, then shear) into the same solid as one oblique move",
                 Up && [&] { Deliver<BrepBody> Two = TweakSolver::TranslateFace(Up.Payload, Top, { 1.0, 0.5, 0.0 }, false);
                             return Two && Oblique && std::fabs(Two.Payload.Validate().Volume - Oblique.Payload.Validate().Volume) < 1e-9 && SameTopology(Two.Payload, Oblique.Payload); }());

    //------------------------------------------------------------------ edges
    Panel.Section("Moving an edge: the two faces along it stay planar; the end faces warp only for a component along the edge");
    Deliver<BrepBody> Ridge = TweakSolver::TranslateEdge(Source, FrontTopEdge, { 0, 0, 1.0 }, false);
    Panel.Expect("Lifting the front top edge by 1 is accepted without warping (the end faces contain the direction)", Ridge && Ridge.Payload.Validate().Solid() && PlanarFaces(Ridge.Payload) == 6);
    if (Ridge)
    {
        Panel.Expect("Topology is untouched", SameTopology(Source, Ridge.Payload));
        Panel.Within("The top becomes a tilted plane: volume 4·3·(2 + 1/2) = 30 (relative)", std::fabs(Ridge.Payload.Validate().Volume - 30.0) / 30.0, PlanarLimit);
    }
    Deliver<BrepBody> Splayed = TweakSolver::TranslateEdge(Source, FrontTopEdge, { 0, -1.0, 0.5 }, false);
    Panel.Expect("A move perpendicular to the edge but out of the top plane (0, −1, 0.5) also keeps every face planar", Splayed && Splayed.Payload.Validate().Solid() && PlanarFaces(Splayed.Payload) == 6);
    if (Splayed)
    {
        // Cross-section trapezoid in the (y, z) plane: the front top corner moves from (0, 2) to (−1, 2.5); shoelace × length.
        const double Section = 0.5 * std::fabs((0.0 * 0.0 - W * 0.0) + (W * H - W * 0.0) + (W * 2.5 - (-1.0) * H) + ((-1.0) * 0.0 - 0.0 * 2.5));
        Panel.Within("Its volume is the section polygon's area × length (relative)", std::fabs(Splayed.Payload.Validate().Volume - Section * L) / (Section * L), PlanarLimit);
    }
    std::vector<int> Warped = TweakSolver::WarpedFaces(Source, TweakSolver::EdgeVertices(Source, FrontTopEdge), { 1.0, 0, 0 });
    Panel.Expect("A move along the edge names exactly the two end faces as the ones that would warp", Warped.size() == 2 &&
                 std::find(Warped.begin(), Warped.end(), FaceToward(Source, { -1, 0, 0 })) != Warped.end() && std::find(Warped.begin(), Warped.end(), FaceToward(Source, { 1, 0, 0 })) != Warped.end());
    Panel.Expect("…and the tweak refuses it unless warping is allowed", !TweakSolver::TranslateEdge(Source, FrontTopEdge, { 1.0, 0, 0 }, false));
    Deliver<BrepBody> Slid = TweakSolver::TranslateEdge(Source, FrontTopEdge, { 1.0, 0, 0 }, true);
    Panel.Expect("With warping allowed the end faces become bilinear and the solid stays closed", Slid && Slid.Payload.Validate().Solid() && PlanarFaces(Slid.Payload) == 4 && SameTopology(Source, Slid.Payload));
    if (Slid)
    {
        Vec3 C[8]; BoxCorners(L, W, H, C); C[4] = C[4] + Vec3{ 1, 0, 0 }; C[5] = C[5] + Vec3{ 1, 0, 0 };
        const double Exact = HexahedronVolume(C);
        Panel.Within("Its volume follows the trilinear hexahedron integral (relative)", std::fabs(Slid.Payload.Validate().Volume - Exact) / Exact, WarpLimit);
        Panel.Note("exact %.9f · measured %.9f", Exact, Slid.Payload.Validate().Volume);
    }

    //------------------------------------------------------------------ vertices
    Panel.Section("Moving a vertex: refused by default because faces would warp; exact bilinear faces when allowed");
    Warped = TweakSolver::WarpedFaces(Source, { Corner }, { 0, 0, 1.0 });
    Panel.Expect("Lifting a top corner straight up warps only the top face (the two vertical sides contain the direction)", Warped.size() == 1 && Warped[0] == Top);
    Panel.Expect("The default tweak refuses and leaves the source untouched", !TweakSolver::TranslateVertex(Source, Corner, { 0, 0, 1.0 }, false) && Source.Faces.size() == 6 && std::fabs(Source.Validate().Volume - 24.0) < 1e-12);
    Deliver<BrepBody> Lifted = TweakSolver::TranslateVertex(Source, Corner, { 0, 0, 1.0 }, true);
    Panel.Expect("With warping allowed the corner lifts into a closed manifold solid with one bilinear face", Lifted && Lifted.Payload.Validate().Solid() && PlanarFaces(Lifted.Payload) == 5 && SameTopology(Source, Lifted.Payload));
    if (Lifted)
    {
        Panel.Within("The volume is 24 + ∫∫ (x/4)(y/3) = 27 (relative)", std::fabs(Lifted.Payload.Validate().Volume - 27.0) / 27.0, WarpLimit);
        Panel.Expect("The bilinear top passes through all four of its corners", [&]
        {
            const NurbsSurface& S = Lifted.Payload.Faces[Top].Surface;
            Vec3 P[4] = { S.Sample(S.DomainStartU(), S.DomainStartV()), S.Sample(S.DomainEndU(), S.DomainStartV()), S.Sample(S.DomainStartU(), S.DomainEndV()), S.Sample(S.DomainEndU(), S.DomainEndV()) };
            int Hits = 0;
            for (const Vec3& Q : P) for (const BrepVertex& V : Lifted.Payload.Vertices) if (V.Point.Distance(Q) < 1e-12) { ++Hits; break; }
            return Hits == 4;
        }());
        Panel.Note("lifted corner: measured %.6f, exact 27 (tessellation floor at 32 spans)", Lifted.Payload.Validate().Volume);
    }
    Deliver<BrepBody> Pulled = TweakSolver::TranslateVertex(Source, Corner, { 0.5, 0.5, 1.0 }, true);
    Panel.Expect("An oblique corner pull warps all three faces around it and stays closed", Pulled && Pulled.Payload.Validate().Solid() && PlanarFaces(Pulled.Payload) == 3 && SameTopology(Source, Pulled.Payload));
    if (Pulled)
    {
        Vec3 C[8]; BoxCorners(L, W, H, C); C[7] = C[7] + Vec3{ 0.5, 0.5, 1.0 };
        const double Exact = HexahedronVolume(C);
        Panel.Within("Its volume follows the trilinear hexahedron integral (relative)", std::fabs(Pulled.Payload.Validate().Volume - Exact) / Exact, WarpLimit);
        Panel.Note("exact %.9f · measured %.9f", Exact, Pulled.Payload.Validate().Volume);
    }

    //------------------------------------------------------------------ prisms
    Panel.Section("Prisms: trimmed n-gon caps translate rigidly or refit in their plane; curved edges refuse");
    Deliver<NurbsCurve> Hexagon = NurbsCurve::Polyline({ { 2, 0, 0 }, { 1, std::sqrt(3.0), 0 }, { -1, std::sqrt(3.0), 0 }, { -2, 0, 0 }, { -1, -std::sqrt(3.0), 0 }, { 1, -std::sqrt(3.0), 0 } }, true);
    Deliver<BrepBody> Prism = Hexagon ? BrepBody::Extrude(Hexagon.Payload, { 0, 0, 1 }, 2.0) : Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "hexagon");
    Panel.Expect("A hexagonal prism (six natural quads, two trimmed hexagon caps) is the fixture", Prism && Prism.Payload.Validate().Solid() && Prism.Payload.Faces.size() == 8);
    if (Prism)
    {
        const BrepBody& P = Prism.Payload;
        const int Cap = FaceToward(P, { 0, 0, 1 });
        const double Base = 6.0 * std::sqrt(3.0);                                       // [m²] hexagon of side 2
        Deliver<BrepBody> Leaning = TweakSolver::TranslateFace(P, Cap, { 1.0, 0.4, 0.8 }, false);
        Panel.Expect("Moving the cap obliquely keeps the cap planar and turns the six sides into planar parallelograms", Leaning && Leaning.Payload.Validate().Solid() && PlanarFaces(Leaning.Payload) == 8 && SameTopology(P, Leaning.Payload));
        if (Leaning) Panel.Within("Its volume is base area × new height 2.8 (relative)", std::fabs(Leaning.Payload.Validate().Volume - Base * 2.8) / (Base * 2.8), PlanarLimit);
        const int Side = EdgeBetween(P, { 2, 0, 0 }, { 2, 0, 2 });
        Deliver<BrepBody> Bulged = TweakSolver::TranslateEdge(P, Side, { 0.6, 0, 0 }, false);
        Panel.Expect("Pushing one vertical edge outward refits both hexagon caps in their planes and keeps the two sides planar", Bulged && Bulged.Payload.Validate().Solid() && WarpedFaces(Bulged.Payload) == 0 && PlanarFaces(Bulged.Payload) == 4 && SameTopology(P, Bulged.Payload));
        if (Bulged) Panel.Within("Each cap grows by ½·0.6·2√3 = 0.6√3: volume (6√3 + 0.6√3)·2 (relative)", std::fabs(Bulged.Payload.Validate().Volume - (Base + 0.6 * std::sqrt(3.0)) * 2.0) / ((Base + 0.6 * std::sqrt(3.0)) * 2.0), PlanarLimit);
        Panel.Expect("Lifting one cap vertex out of the cap plane refuses: an n-gon cannot warp", !TweakSolver::TranslateVertex(P, VertexAt(P, { 2, 0, 2 }), { 0, 0, 0.5 }, true));
        Panel.Expect("Moving one cap vertex within the cap plane refits the cap and the two adjacent sides", [&]
        {
            Deliver<BrepBody> R = TweakSolver::TranslateVertex(P, VertexAt(P, { 2, 0, 2 }), { 0.3, 0, 0 }, true);
            return R && R.Payload.Validate().Solid() && SameTopology(P, R.Payload) && WarpedFaces(R.Payload) == 2 && std::fabs(R.Payload.Validate().Volume - (6.0 * std::sqrt(3.0) + 0.5 * 0.3 * std::sqrt(3.0)) * 2.0) > 0.0;
        }());
    }
    const BrepBody Drum = BrepBody::Cylinder({ 6, 0, 0 }, { 0, 0, 1 }, 1.0, 2.0).Payload;
    Panel.Expect("A native cylinder cap (closed circular rim) translates axially with exact cylinder topology", [&]
    {
        Deliver<BrepBody> R = TweakSolver::TranslateFace(Drum, FaceToward(Drum, { 0, 0, 1 }), { 0, 0, 1 }, true);
        return R && R.Payload.Validate().Solid() && R.Payload.Vertices.size() == 2 && R.Payload.Edges.size() == 3 && R.Payload.Faces.size() == 3 &&
               std::fabs(R.Payload.Validate().Volume - ScalarCriteria::Pi * 3.0) / (ScalarCriteria::Pi * 3.0) < 2e-3;
    }());
    Panel.Expect("A zero vector, a bad index and an open sheet refuse", !TweakSolver::TranslateFace(Source, Top, { 0, 0, 0 }, true) && !TweakSolver::TranslateVertex(Source, 99, { 0, 0, 1 }, true) &&
                 !TweakSolver::TranslateFace(BrepBody::FromSurface(NurbsSurface::Plane({ 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, 1, 1).Payload), 0, { 0, 0, 1 }, true));
    Panel.Expect("A move that would invert the solid refuses", !TweakSolver::TranslateFace(Source, Top, { 0, 0, -3.0 }, true));

    //------------------------------------------------------------------ console + proof
    Panel.Section("Console commit and visual proof");
    ConsoleHost CommandHost(SOLIDARC_PROOF_FOLDER, 1280, 800);
    (void)CommandHost.Document().AddBody("Block", Source);
    const bool Accepted = CommandHost.Execute("tweak Block (0,0,1.5) --face=" + std::to_string(Top) + " --name=Taller");
    const SceneFigure* Committed = CommandHost.Document().Find("Taller");
    Panel.Expect("`tweak <body> (dx,dy,dz) --face=i` commits the moved solid and consumes the source",
                 Accepted && Committed && Committed->Body.Faces.size() == 6 && std::fabs(Committed->Body.Validate().Volume - 42.0) < 1e-9 && !CommandHost.Document().Find("Block"));
    Panel.Expect("`tweak --vertex=i` without --warp refuses and names the warping face; with --warp it commits",
                 !CommandHost.Execute("tweak Taller (0,0,1) --vertex=" + std::to_string(Corner)) &&
                 CommandHost.Execute("tweak Taller (0,0,1) --vertex=" + std::to_string(Corner) + " --warp --name=Peaked") && CommandHost.Document().Find("Peaked"));
    Panel.Expect("Two selectors at once, a missing selector and a curve figure refuse at the console",
                 !CommandHost.Execute("tweak Peaked (1,0,0) --face=0 --edge=1") && !CommandHost.Execute("tweak Peaked (1,0,0)") &&
                 CommandHost.Execute("circle (0,0) 1 --name=Ring") && !CommandHost.Execute("tweak Ring (1,0,0) --vertex=0"));

    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Phase34b_Tweak.png";
    std::error_code Error;
    std::filesystem::remove(Proof, Error);
    ConsoleHost ProofHost(SOLIDARC_PROOF_FOLDER, 1280, 800);
    auto Reset = [&]() { return ProofHost.Execute("reset") && ProofHost.Execute("gizmo off") && ProofHost.Execute("show iso off"); };
    auto Add = [&](const char* Name, BrepBody Body, uint8_t Matcap) -> bool { ProofHost.Document().AddBody(Name, std::move(Body)).Matcap = Matcap; return true; };
    auto Frame = [&](int Tile) { return ProofHost.Execute("view iso") && ProofHost.Execute("view orbit 20 -8") && ProofHost.Execute("view fit") && ProofHost.Execute("render sheet " + std::to_string(Tile)); };
    Deliver<BrepBody> LeaningProof = Prism ? TweakSolver::TranslateFace(Prism.Payload, FaceToward(Prism.Payload, { 0, 0, 1 }), { 1.0, 0.4, 0.8 }, false) : Deliver<BrepBody>::Reject(RefusalReason::DegenerateInput, "prism");
    const bool Rendered = Up && Oblique && Ridge && Slid && Lifted && Pulled && Prism && LeaningProof &&
        Reset() && Add("Source", Source, 0) && Add("Oblique", Oblique.Payload.Transformed(Mat4::Translation({ 6, 0, 0 })), 3) && Frame(0) &&
        Reset() && Add("Ridge", Ridge.Payload, 1) && Add("Slid", Slid.Payload.Transformed(Mat4::Translation({ 6, 0, 0 })), 7) && Frame(1) &&
        Reset() && Add("Lifted", Lifted.Payload, 5) && Add("Pulled", Pulled.Payload.Transformed(Mat4::Translation({ 6, 0, 0 })), 4) && Frame(2) &&
        Reset() && Add("Prism", Prism.Payload, 2) && Add("Leaning", LeaningProof.Payload.Transformed(Mat4::Translation({ 6, 0, 0 })), 6) && Frame(3) &&
        ProofHost.Execute("render sheet finalize Phase34b_Tweak");
    Panel.Expect("C++ proof commands complete without refusal", Rendered);
    Panel.Expect("C++ contact-sheet proof is written and non-trivial", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);

    return Panel.Conclude();
}
