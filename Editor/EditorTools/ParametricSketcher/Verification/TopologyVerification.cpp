//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Verification/TopologyVerification.cpp — Phase 6: B-rep construction, Euler counts, orientation, volumes, trimming
//============================================================================================================================================
#include "Kernel/TopologySpecification.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <cmath>

using namespace Frontier;

int main()
{
    VerificationPanel Panel("SolidArc · Phase 6 · Topology Verification — BrepBody · Sew / Capped / Orient · Euler · volumes · planar triangulation");
    const double Pi = ScalarCriteria::Pi;

    Panel.Section("Primitive solids: closed, manifold, oriented, Euler characteristic, analytic volume");
    {
        struct Case { const char* Name; Deliver<BrepBody> Body; int V, E, F, Genus; double Volume, Area; };
        Case Cases[] = {
            { "Box 2×1×1",       BrepBody::Box({ 0, 0, 0 }, { 2, 1, 1 }),                          8, 12, 6, 0, 2.0, 10.0 },
            { "Cylinder r1 h2",  BrepBody::Cylinder({ 0, 0, 0 }, Vec3::UnitZ(), 1, 2),             2, 3, 3, 0, 2 * Pi, 6 * Pi },
            { "Cone 1→0.5 h2",   BrepBody::Cone({ 0, 0, 0 }, Vec3::UnitZ(), 1, 0.5, 2),            2, 3, 3, 0, Pi * 2 / 3 * (1 + 0.5 + 0.25), Pi * 1.25 + Pi * 1.5 * std::sqrt(0.25 + 4) },
            { "Cone to apex",    BrepBody::Cone({ 0, 0, 0 }, Vec3::UnitZ(), 1, 0, 2),              2, 2, 2, 0, Pi * 2 / 3, Pi + Pi * std::sqrt(5.0) },
            { "Sphere r1",       BrepBody::Sphere({ 0, 0, 0 }, 1),                                  2, 1, 1, 0, 4.0 / 3.0 * Pi, 4 * Pi },
            { "Torus R2 r0.5",   BrepBody::Torus({ 0, 0, 0 }, Vec3::UnitZ(), 2, 0.5),               1, 2, 1, 1, 2 * Pi * Pi * 2 * 0.25, 4 * Pi * Pi * 2 * 0.5 },
        };
        for (Case& C : Cases)
        {
            Panel.Expect((std::string(C.Name) + " built").c_str(), bool(C.Body));
            if (!C.Body) continue;
            BodyReport R = C.Body.Payload.Validate();
            Panel.Expect((std::string(C.Name) + " closed manifold oriented").c_str(), R.Closed && R.Manifold && R.Oriented);
            Panel.Expect((std::string(C.Name) + " V/E/F = " + std::to_string(C.V) + "/" + std::to_string(C.E) + "/" + std::to_string(C.F)).c_str(), R.Vertices == C.V && R.Edges == C.E && R.Faces == C.F);
            Panel.Expect((std::string(C.Name) + " genus " + std::to_string(C.Genus)).c_str(), R.Genus == C.Genus);
            Panel.Within((std::string(C.Name) + " volume vs analytic (tessellated, rel)").c_str(), std::fabs(R.Volume - C.Volume) / C.Volume, 5e-3);
            Panel.Within((std::string(C.Name) + " area vs analytic (rel)").c_str(), std::fabs(R.Area - C.Area) / C.Area, 5e-3);
            Panel.Expect((std::string(C.Name) + " is a solid (positive volume)").c_str(), R.Solid());
        }
    }

    Panel.Section("Orientation: every interior edge has two coedges of opposite sense, all face normals point outward");
    {
        BrepBody Box = BrepBody::Box({ -1, -1, -1 }, { 1, 1, 1 }).Payload;
        bool Outward = true;
        for (size_t F = 0; F < Box.Faces.size(); ++F)
        {
            const NurbsSurface& S = Box.Faces[F].Surface;
            double U = 0.5 * (S.DomainStartU() + S.DomainEndU()), V = 0.5 * (S.DomainStartV() + S.DomainEndV());
            Vec3 P = S.Sample(U, V), N = Box.FaceNormal(int(F), U, V);
            if (N.Dot(P) < 0.99) Outward = false;
        }
        Panel.Expect("Box: all six face normals outward (cube centred at origin)", Outward);
        BrepBody Flipped = Box; for (size_t F = 0; F < Flipped.Faces.size(); ++F) Flipped.FlipFace(int(F));
        Panel.Expect("Flipping all faces keeps the edge pairing consistent but negates volume", Flipped.Validate().Oriented && Flipped.SignedVolume() < 0);
        Flipped.Orient();
        Panel.Expect("Orient() recovers positive volume", Flipped.SignedVolume() > 0);
        BrepBody One = Box; One.FlipFace(2);
        Panel.Expect("One flipped face is reported as misoriented edges", One.Validate().MisorientedEdges == 4);
        One.Orient();
        Panel.Expect("Orient() heals a single inverted face", One.Validate().MisorientedEdges == 0 && One.SignedVolume() > 0);
        BrepBody Mirror = Box.Transformed(Mat4::Scaling({ -1, 1, 1 }));
        Panel.Expect("Reflection re-orients the body (volume stays positive)", Mirror.Validate().Solid());
    }

    Panel.Section("Extrude and revolve through the generic sewer");
    {
        NurbsCurve Hex = NurbsCurve::Polygon(Workplane::XY(), { 0, 0 }, 1, 6, 0, true).Payload;
        Deliver<BrepBody> HexBody = BrepBody::Extrude(Hex, Vec3::UnitZ(), 2);
        BodyReport R = HexBody.Payload.Validate();
        Panel.Expect("Hexagon extrusion: 6 side faces + 2 caps, 12 vertices, 18 edges", R.Faces == 8 && R.Vertices == 12 && R.Edges == 18);
        Panel.Within("Hexagon prism volume = 3√3/2 · h", std::fabs(R.Volume - 3 * std::sqrt(3.0) / 2 * 2), 1e-9);
        Panel.Expect("Hexagon prism is a solid", R.Solid());
        Panel.Expect("SplitAtKinks: hexagon → 6 pieces", SplitAtKinks(Hex).size() == 6);
        NurbsCurve Slot = NurbsCurve::Slot(Workplane::XY(), { -1, 0 }, { 1, 0 }, 0.5).Payload;
        Deliver<BrepBody> SlotBody = BrepBody::Extrude(Slot, Vec3::UnitZ(), 1);
        R = SlotBody.Payload.Validate();
        Panel.Expect("Slot extrusion (tangent-continuous profile) stays one side face + 2 caps", R.Faces == 3 && R.Solid());
        Panel.Within("Slot prism volume = (2·2·0.5·2 + π·0.25)·1", std::fabs(R.Volume - (2.0 + Pi * 0.25)), 5e-3);
        NurbsCurve Circle = NurbsCurve::Circle({ 0, 0, 0 }, Vec3::UnitZ(), 1).Payload;
        Deliver<BrepBody> Down = BrepBody::Extrude(Circle, Vec3::UnitZ(), -1);
        Panel.Expect("Negative extrusion length still yields a positive solid", Down.Payload.Validate().Solid());
        NurbsCurve Profile = NurbsCurve::Polyline({ { 1, 0, 0 }, { 2, 0, 0 }, { 2, 0, 1 }, { 1, 0, 1 } }, true).Payload;
        Deliver<BrepBody> Ring = BrepBody::Revolve(Profile, { 0, 0, 0 }, Vec3::UnitZ(), 2 * Pi);
        R = Ring.Payload.Validate();
        Panel.Expect("Full revolution of a square → genus-1 solid, 4 faces", R.Genus == 1 && R.Faces == 4 && R.Solid());
        Panel.Within("Ring volume = π(2²−1²)·1", std::fabs(R.Volume - 3 * Pi) / (3 * Pi), 5e-3);
        Deliver<BrepBody> Half = BrepBody::Revolve(Profile, { 0, 0, 0 }, Vec3::UnitZ(), Pi);
        R = Half.Payload.Validate();
        Panel.Expect("Half revolution: 4 swept faces + 2 planar caps, genus 0", R.Faces == 6 && R.Genus == 0 && R.Solid());
        Panel.Within("Half ring volume = 1.5π", std::fabs(R.Volume - 1.5 * Pi) / (1.5 * Pi), 5e-3);
    }

    Panel.Section("Sheets, sewing and capping");
    {
        BrepBody Sheet = BrepBody::FromSurface(NurbsSurface::Cylinder({ 0, 0, 0 }, Vec3::UnitZ(), 1, 1).Payload);
        BodyReport R = Sheet.Validate();
        Panel.Expect("Open cylinder is a sheet with 2 open edges and one seam", Sheet.Classification() == BodyClassification::Sheet && R.OpenEdges == 2 && R.Edges == 3);
        Panel.Expect("OpenLoops finds both rims", Sheet.OpenLoops().size() == 2);
        int Caps = Sheet.Capped();
        Panel.Expect("Capped() adds two planar faces", Caps == 2 && Sheet.Faces.size() == 3);
        Sheet.Orient();
        Panel.Expect("Capped and oriented sheet becomes a solid", Sheet.Validate().Solid());
        // sew six independent planes → box (order and orientation of the inputs deliberately mixed)
        std::vector<NurbsSurface> Planes = {
            NurbsSurface::Plane({ 0, 0, 1 }, Vec3::UnitY(), Vec3::UnitX(), 1, 1).Payload,   // top, flipped axes
            NurbsSurface::Plane({ 0, 0, 0 }, Vec3::UnitX(), Vec3::UnitY(), 1, 1).Payload,
            NurbsSurface::Plane({ 0, 0, 0 }, Vec3::UnitZ(), Vec3::UnitX(), 1, 1).Payload,   // front, flipped
            NurbsSurface::Plane({ 0, 1, 0 }, Vec3::UnitX(), Vec3::UnitZ(), 1, 1).Payload,
            NurbsSurface::Plane({ 0, 0, 0 }, Vec3::UnitY(), Vec3::UnitZ(), 1, 1).Payload,
            NurbsSurface::Plane({ 1, 0, 0 }, Vec3::UnitZ(), Vec3::UnitY(), 1, 1).Payload }; // right, flipped
        Deliver<BrepBody> Sewn = BrepBody::Sew(Planes);
        R = Sewn.Payload.Validate();
        Panel.Expect("Sew of six arbitrarily oriented planes → unit cube solid", R.Solid() && R.Edges == 12 && R.Vertices == 8);
        Panel.Within("Sewn cube volume 1", std::fabs(R.Volume - 1.0), 1e-9);
        // five planes: open box, capped automatically
        Planes.pop_back();
        Deliver<BrepBody> Five = BrepBody::Sew(Planes);
        Panel.Expect("Five planes: missing side is capped, still a unit cube", Five.Payload.Validate().Solid() && std::fabs(Five.Payload.SignedVolume() - 1.0) < 1e-9 && Five.Payload.Faces.size() == 6);
    }

    Panel.Section("Planar triangulation with holes");
    {
        std::vector<Vec3> P = { { 0, 0, 0 }, { 4, 0, 0 }, { 4, 3, 0 }, { 0, 3, 0 },                          // outer CCW
                                { 1, 1, 0 }, { 1, 2, 0 }, { 2, 2, 0 }, { 2, 1, 0 } };                        // hole (CW)
        std::vector<uint32_t> Tri = TriangulatePlanarPolygon(P, { { 0, 1, 2, 3 }, { 4, 5, 6, 7 } }, Vec3::UnitZ());
        double Area = 0; bool AllCcw = true;
        for (size_t I = 0; I + 2 < Tri.size(); I += 3)
        {
            Vec3 A = P[Tri[I]], B = P[Tri[I + 1]], C = P[Tri[I + 2]];
            double S = (B - A).Cross(C - A).Z * 0.5; if (S < -1e-12) AllCcw = false; Area += S;
        }
        Panel.Expect("Rectangle with square hole: 8 triangles", Tri.size() == 24);
        Panel.Within("Area = 12 − 1", std::fabs(Area - 11.0), 1e-9);
        Panel.Expect("All triangles CCW about +Z", AllCcw);
        // concave outline
        std::vector<Vec3> L = { { 0, 0, 0 }, { 3, 0, 0 }, { 3, 1, 0 }, { 1, 1, 0 }, { 1, 3, 0 }, { 0, 3, 0 } };
        Tri = TriangulatePlanarPolygon(L, { { 0, 1, 2, 3, 4, 5 } }, Vec3::UnitZ());
        Area = 0; for (size_t I = 0; I + 2 < Tri.size(); I += 3) Area += (L[Tri[I + 1]] - L[Tri[I]]).Cross(L[Tri[I + 2]] - L[Tri[I]]).Z * 0.5;
        Panel.Within("L-shape area 5", std::fabs(Area - 5.0), 1e-9);
    }

    Panel.Section("Console: bodies in the document, face / edge selection through the pick plane");
    {
        ConsoleHost Host("/tmp/SolidArcVerification", 1280, 800);
        Host.Execute("box (0,0,0) 2 1.5 1 ; cylinder (4,0,0) 0.8 2 ; view iso ; view fit");
        SceneFigure* Box = Host.Document().Find(std::string("Box"));
        Panel.Expect("box verb creates a Body figure", Box && Box->Classification == FigureClassification::Body && Box->Body.Validate().Solid());
        Panel.Expect("cylinder verb now creates a solid body (use --sheet for the old surface)", Host.Document().Find(std::string("Cylinder"))->Classification == FigureClassification::Body);
        Host.Execute("cylinder (8,0,0) 0.8 2 --sheet");
        Panel.Expect("--sheet keeps the raw surface path", Host.Document().Find(std::string("Cylinder.2"))->Classification == FigureClassification::Surface);
        // top face of the box: project its centre and click in face mode
        double X = 0, Y = 0; (void)Host.Camera().WorldToPixel({ 1, 0.75, 1 }, 1280, 800, X, Y);
        char Line[160];
        Host.Execute("key 3");
        std::snprintf(Line, sizeof Line, "click %d %d", int(X), int(Y)); Host.Execute(Line);
        Box = Host.Document().Find(std::string("Box"));                                  // figure storage may have grown since
        Panel.Expect("Face mode click selects the top face (face 1 = +Z plane)", Box->SelectedFaces.size() == 1 && Box->Body.FaceNormal(Box->SelectedFaces[0], 0.5, 0.5).Z > 0.99);
        Host.Execute("key 2");
        Panel.Expect("Switching to edge mode drops the face selection", Box->SelectedFaces.empty());
        (void)Host.Camera().WorldToPixel({ 1, 0, 1 }, 1280, 800, X, Y);                 // midpoint of the front-top edge
        std::snprintf(Line, sizeof Line, "click %d %d", int(X), int(Y)); Host.Execute(Line);
        Panel.Expect("Edge mode click selects the front-top edge (length 2, z = 1)", Box->SelectedEdges.size() == 1 && std::fabs(Box->Body.Edges[Box->SelectedEdges[0]].Curve.Length() - 2.0) < 1e-9
                     && std::fabs(Box->Body.Edges[Box->SelectedEdges[0]].Curve.StartPoint().Z - 1.0) < 1e-9);
        Host.Execute("select faces Box all ; gizmo status");
        Panel.Within("Gizmo pivot for all faces = box centre", Host.Gizmo().CurrentPivot().Origin.Distance({ 1, 0.75, 0.5 }), 1e-9);
        Host.Execute("key 4 ; select Box ; move Box (0,0,2) ; undo");
        Box = Host.Document().Find(std::string("Box"));                                  // undo restores the document → re-resolve
        Panel.Within("Body move + undo round-trips through the undo timeline", Box->Bounds().Low.Z, 1e-12);
        Host.Execute("rect (0,0) (2,2) ; extrude Rectangle 1");
        SceneFigure* Ext = Host.Document().Find(std::string("Extrusion"));
        Panel.Expect("extrude of a closed profile yields a solid body with caps", Ext && Ext->Classification == FigureClassification::Body && Ext->Body.Faces.size() == 6 && Ext->Body.Validate().Solid());
        Host.Execute("render Proof_06b_Console");
    }

    return Panel.Conclude();
}
