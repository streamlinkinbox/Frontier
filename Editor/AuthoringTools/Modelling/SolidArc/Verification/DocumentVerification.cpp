//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Verification/DocumentVerification.cpp — Phase 22: native .arc document persistence
//============================================================================================================================================
// A native .arc is a versioned construction journal, rather than a mesh export. These checks cover the properties that
// matter to a modelling document: semantic replay preserves exact B-rep geometry and associative recipes; names containing
// whitespace remain valid; a subsequent save leaves a recovery copy; and a malformed document cannot damage the model
// currently open in the host.
#include "Console/ConsoleHost.h"
#include "Document/UndoSequence.h"
#include "VerificationPanel.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace Frontier;

namespace
{
[[nodiscard]] const SceneFigure* Figure(const ConsoleHost& Host, const std::string& Name)
{
    for (const SceneFigure& F : Host.AllFigures()) if (F.Name == Name) return &F;
    return nullptr;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Phase 22 · Native .arc Document Verification — versioned command journal, atomic save/open, recovery");
    const std::filesystem::path Directory = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "DocumentVerification";
    const std::filesystem::path Stem = Directory / "mounting-bracket";
    const std::filesystem::path Document = Directory / "mounting-bracket.arc";
    const std::filesystem::path Backup = Directory / "mounting-bracket.arc.bak";
    const std::filesystem::path Invalid = Directory / "invalid.arc";
    std::error_code Error;
    std::filesystem::remove_all(Directory, Error);
    std::filesystem::create_directories(Directory, Error);

    ConsoleHost Host((Directory / "proofs").string(), 960, 640);
    Panel.Section("Construct a mixed 2D / 3D parametric document");
    Panel.Expect("custom workplane origin succeeds", Host.Execute("workplane xy --origin=(3,4,0)"));
    Panel.Expect("named workplane with spaces succeeds", Host.Execute("plane --name=\"Detail Plane\""));
    Panel.Expect("profile with a quoted name succeeds", Host.Execute("rect (0,0) (20,12) --name=\"Plate Profile\""));
    Panel.Expect("associative extrusion succeeds", Host.Execute("extrude \"Plate Profile\" 5 --name=\"Plate Body\""));
    Panel.Expect("independent B-rep box succeeds", Host.Execute("box (30,0,0) (42,12,8) --name=\"Base Plate\""));
    Panel.Expect("direct B-rep move succeeds", Host.Execute("move \"Base Plate\" (2,3,0)"));
    Panel.Expect("first guide line succeeds", Host.Execute("line (0,20) (10,20) --name=\"Guide A\""));
    Panel.Expect("second guide line succeeds", Host.Execute("line (0,20) (0,30) --name=\"Guide B\""));
    Panel.Expect("constraint graph entry succeeds", Host.Execute("constraint perpendicular \"Guide A\" \"Guide B\""));
    Panel.Expect("one saved constraint exists", Host.AllConstraints().size() == 1);

    const uint64_t BeforeFirstSave = UndoSequence::Fingerprint(Host.Document());
    const SceneFigure* SourceBefore = Figure(Host, "Plate Profile");
    const SceneFigure* BodyBefore = Figure(Host, "Plate Body");
    const SceneFigure* BaseBefore = Figure(Host, "Base Plate");
    Panel.Expect("quoted source figure exists", SourceBefore != nullptr);
    Panel.Expect("derived body exists", BodyBefore != nullptr && BodyBefore->Recipe.Live());
    Panel.Expect("moved B-rep exists", BaseBefore != nullptr && BaseBefore->Classification == FigureClassification::Body);
    const double BaseVolume = BaseBefore ? BaseBefore->Body.Validate().Volume : 0.0;

    Panel.Section("Save: extension normalization, version header and recovery backup");
    Panel.Expect("save accepts a stem and appends .arc", Host.Execute("save \"" + Stem.generic_string() + "\""));
    Panel.Expect("native .arc was written", std::filesystem::exists(Document));
    std::ifstream First(Document);
    std::string Header; std::getline(First, Header);
    First.close(); // Release the read handle before the Windows atomic replacement check.
    Panel.Expect("native document declares v1 header", Header == "# SolidArc native document v1");
    Panel.Expect("first save has no backup yet", !std::filesystem::exists(Backup));

    // A second save must preserve the prior version before atomically replacing it.
    Panel.Expect("post-save direct edit succeeds", Host.Execute("move \"Base Plate\" (1,0,0)"));
    const uint64_t BeforeOpen = UndoSequence::Fingerprint(Host.Document());
    Panel.Expect("save without a path reuses the active .arc document", Host.Execute("save"));
    Panel.Expect("second save refreshed a .bak recovery file", std::filesystem::exists(Backup));
    Panel.Expect("second model edit changed the saved model fingerprint", BeforeOpen != BeforeFirstSave);

    Panel.Section("Open: exact semantic replay and associativity");
    Panel.Expect("reset replaces the live scene before the test open", Host.Execute("reset"));
    Panel.Expect("reset removed all figures", Host.AllFigures().empty());
    Panel.Expect("open .arc succeeds", Host.Execute("open \"" + Document.generic_string() + "\""));
    Panel.Expect("open restores original figure count", Host.AllFigures().size() == 5);
    Panel.Expect("open restores the exact saved geometric document fingerprint", UndoSequence::Fingerprint(Host.Document()) == BeforeOpen);
    // The second save contains the one-unit move made after the first save; this check confirms both model revisions
    // retained their semantic structure rather than flattening the scene to display meshes.
    const SceneFigure* SourceAfter = Figure(Host, "Plate Profile");
    const SceneFigure* BodyAfter = Figure(Host, "Plate Body");
    const SceneFigure* BaseAfter = Figure(Host, "Base Plate");
    Panel.Expect("quoted names survive save/open", SourceAfter != nullptr && BodyAfter != nullptr && BaseAfter != nullptr);
    Panel.Expect("extrude recipe survives save/open", BodyAfter != nullptr && BodyAfter->Recipe.Live());
    Panel.Equal("direct B-rep volume survives save/open", BaseAfter ? BaseAfter->Body.Validate().Volume : 0.0, BaseVolume, 1e-8);
    Panel.Expect("constraint graph survives save/open", Host.AllConstraints().size() == 1);
    Panel.Expect("named workplane survives save/open", Host.Execute("workplane \"Detail Plane\""));

    // Changing a retained source must still rebuild the retained extrusion after reload; this distinguishes a native
    // parametric document from a flattened body archive.
    const double HeightBefore = BodyAfter ? BodyAfter->Bounds().Low.Z : 0.0;
    Panel.Expect("moving a loaded source replays its associative extrusion", Host.Execute("move \"Plate Profile\" (0,0,2)"));
    const SceneFigure* Regenerated = Figure(Host, "Plate Body");
    Panel.Expect("recipe is still live after source edit", Regenerated != nullptr && Regenerated->Recipe.Live());
    Panel.Within("reloaded extrusion followed its source by 2 mm", std::fabs((Regenerated ? Regenerated->Bounds().Low.Z : 0.0) - HeightBefore - 2.0), 1e-8);

    Panel.Section("Rejected open is transactional");
    {
        std::ofstream Out(Invalid, std::ios::binary | std::ios::trunc);
        Out << "# SolidArc native document v1\nunknown-command definitely-not-model-data\n";
    }
    const uint64_t BeforeBadOpen = UndoSequence::Fingerprint(Host.Document());
    const size_t FiguresBeforeBadOpen = Host.AllFigures().size();
    Panel.Expect("malformed native document is refused", !Host.Execute("open \"" + Invalid.generic_string() + "\""));
    Panel.Expect("failed open preserves the current geometry", UndoSequence::Fingerprint(Host.Document()) == BeforeBadOpen);
    Panel.Expect("failed open preserves the current figure count", Host.AllFigures().size() == FiguresBeforeBadOpen);
    Panel.Expect("wrong native extension is refused", !Host.Execute("open \"" + (Directory / "not-a-document.txt").generic_string() + "\""));

    Panel.Section("Exterior sheet sewing preserves intentional openings across document replay");
    Panel.Expect("clear previous model before sheet checks", Host.Execute("reset"));
    Panel.Expect("first open sheet", Host.Execute("plane (0,0,0) 1 1 --name=Skin_A"));
    Panel.Expect("second adjacent open sheet", Host.Execute("plane (1,0,0) 1 1 --name=Skin_B"));
    Panel.Expect("open sewing succeeds", Host.Execute("sew Skin_A Skin_B --open --name=Exterior"));
    const SceneFigure* Exterior = Figure(Host, "Exterior");
    Panel.Expect("sewing replaces source sheets with one body", Host.AllFigures().size() == 1 && Exterior != nullptr);
    const BodyReport SkinReport = Exterior ? Exterior->Body.Validate() : BodyReport{};
    Panel.Expect("no caps or thickness added", SkinReport.Faces == 2 && SkinReport.OpenEdges == 6 && !SkinReport.Closed);
    Panel.Expect("shared edge actually sewn", SkinReport.Edges == 7 && SkinReport.Hulls == 1);
    Panel.Expect("open skin is manifold and consistently oriented", SkinReport.Manifold && SkinReport.Oriented);
    const std::filesystem::path SkinDocument = Directory / "exterior.arc";
    const uint64_t SkinFingerprint = UndoSequence::Fingerprint(Host.Document());
    Panel.Expect("save open skin", Host.Execute("save \"" + SkinDocument.generic_string() + "\""));
    std::ifstream SkinStream(SkinDocument);
    const std::string SkinText((std::istreambuf_iterator<char>(SkinStream)), std::istreambuf_iterator<char>());
    SkinStream.close();
    Panel.Expect("saved open skins refuse rather than silently cap in older builds", SkinText.find("require open-sew") != std::string::npos);
    Panel.Expect("supported capability succeeds", Host.Execute("require open-sew"));
    Panel.Expect("unknown capability refuses", !Host.Execute("require unsupported-capability"));
    Panel.Expect("capability checks do not alter geometry", UndoSequence::Fingerprint(Host.Document()) == SkinFingerprint);
    Panel.Expect("clear open skin", Host.Execute("reset"));
    Panel.Expect("reload open skin", Host.Execute("open \"" + SkinDocument.generic_string() + "\""));
    Panel.Expect("open skin geometry survives replay", UndoSequence::Fingerprint(Host.Document()) == SkinFingerprint);
    Exterior = Figure(Host, "Exterior");
    Panel.Expect("replay still has six open edges", Exterior != nullptr && Exterior->Body.Validate().OpenEdges == 6);

    Panel.Section("Knot-aware exterior sewing and explicit surface knots");
    Panel.Expect("clear before knot checks", Host.Execute("reset"));
    Panel.Expect("non-uniform patch", Host.Execute("patch 3 2 (0,0,0) (0,1,0) (0.5,0,0) (0.5,1,0) (2,0,0) (2,1,0) --degree=1 --knots-u=0,0,0.25,1,1 --name=Long"));
    const SceneFigure* Long = Figure(Host, "Long");
    Panel.Expect("explicit knots affect the parameterisation", Long != nullptr && Long->Surface.Sample(0.25, 0).Distance(Vec3(0.5, 0, 0)) < 1e-10);
    Panel.Expect("first short side", Host.Execute("plane (0,1,0) 0.5 1 --name=Short_A"));
    Panel.Expect("second short side", Host.Execute("plane (0.5,1,0) 1.5 1 --name=Short_B"));
    const uint64_t BeforeKnots = UndoSequence::Fingerprint(Host.Document());
    Panel.Expect("bad knot count refused", !Host.Execute("patch 2 2 (0,0) (0,1) (1,0) (1,1) --degree=1 --knots-u=0,0,1"));
    Panel.Expect("non-finite knot refused", !Host.Execute("patch 2 2 (0,0) (0,1) (1,0) (1,1) --degree=1 --knots-u=0,0,nan,1"));
    Panel.Expect("zero knot domain refused", !Host.Execute("patch 2 2 (0,0) (0,1) (1,0) (1,1) --degree=1 --knots-u=0,0,0,0"));
    Panel.Expect("decreasing knots refused", !Host.Execute("patch 2 2 (0,0) (0,1) (1,0) (1,1) --degree=1 --knots-u=0,1,0,1"));
    Panel.Expect("bad patches leave geometry unchanged", UndoSequence::Fingerprint(Host.Document()) == BeforeKnots);
    Panel.Expect("long edge sews to short edges", Host.Execute("sew Long Short_A Short_B --open --knot-edges --name=Conforming"));
    const SceneFigure* Conforming = Figure(Host, "Conforming");
    const BodyReport ConformingReport = Conforming ? Conforming->Body.Validate() : BodyReport{};
    Panel.Expect("no T-junction cracks or caps", ConformingReport.Faces == 3 && ConformingReport.Edges == 11 && ConformingReport.OpenEdges == 8);
    Panel.Expect("conforming topology is connected and oriented", ConformingReport.Hulls == 1 && ConformingReport.Manifold && ConformingReport.Oriented);
    bool ClosedLoops = Conforming != nullptr;
    if (Conforming)
    {
        const BrepBody& Body = Conforming->Body;
        for (const BrepLoop& Loop : Body.Loops)
        {
            for (size_t I = 0; I < Loop.Coedges.size(); ++I)
            {
                const BrepCoedge& A = Body.Coedges[Loop.Coedges[I]];
                const BrepCoedge& B = Body.Coedges[Loop.Coedges[(I + 1) % Loop.Coedges.size()]];
                const BrepEdge& Ea = Body.Edges[A.Edge];
                const BrepEdge& Eb = Body.Edges[B.Edge];
                ClosedLoops = ClosedLoops && (A.Reversed ? Ea.VertexStart : Ea.VertexEnd) == (B.Reversed ? Eb.VertexEnd : Eb.VertexStart);
            }
        }
    }
    Panel.Expect("reversed sides keep coedges in loop order", ClosedLoops);
    const uint64_t ConformingFingerprint = UndoSequence::Fingerprint(Host.Document());
    const std::string ConformingPath = (Directory / "conforming.arc").generic_string();
    Panel.Expect("save knot-aware skin", Host.Execute("save \"" + ConformingPath + "\""));
    Panel.Expect("reopen knot-aware skin", Host.Execute("open \"" + ConformingPath + "\""));
    Panel.Expect("explicit knots and edge splits survive replay", UndoSequence::Fingerprint(Host.Document()) == ConformingFingerprint);

    Panel.Section("Unequal boundary segmentation without changing surface knots");
    Panel.Expect("clear before split-junction checks", Host.Execute("reset"));
    Panel.Expect("long edge with no interior knot", Host.Execute("plane (0,0,0) 2 1 --name=Long"));
    Panel.Expect("first unequal neighbour", Host.Execute("plane (0,1,0) 0.7 1 --name=Short_A"));
    Panel.Expect("second unequal neighbour", Host.Execute("plane (0.7,1,0) 1.3 1 --name=Short_B"));
    Panel.Expect("reconcile unequal edges", Host.Execute("sew Long Short_A Short_B --open --split-junctions --name=Reconciled"));
    const auto* Reconciled = Figure(Host, "Reconciled");
    const auto ReconciledReport = Reconciled ? Reconciled->Body.Validate() : BodyReport{};
    Panel.Expect("no cracks or artificial faces", ReconciledReport.Faces == 3 && ReconciledReport.Edges == 10 &&
        ReconciledReport.OpenEdges == 7 && ReconciledReport.Hulls == 1 && ReconciledReport.Oriented && ReconciledReport.Manifold);
    const uint64_t ReconciledFingerprint = UndoSequence::Fingerprint(Host.Document());
    const std::string ReconciledPath = (Directory / "reconciled.arc").generic_string();
    Panel.Expect("save unequal-edge skin", Host.Execute("save \"" + ReconciledPath + "\""));
    std::ifstream ReconciledFile(ReconciledPath);
    const std::string ReconciledText((std::istreambuf_iterator<char>(ReconciledFile)), std::istreambuf_iterator<char>());
    Panel.Expect("old builds must refuse boundary-split documents", ReconciledText.find("require boundary-splits") != std::string::npos);
    Panel.Expect("reopen unequal-edge skin", Host.Execute("open \"" + ReconciledPath + "\""));
    Panel.Expect("split-junction topology survives replay", ReconciledFingerprint == UndoSequence::Fingerprint(Host.Document()));
    const auto Curved = NurbsSurface::Patch(3, 1, 4, 2,
        { {0,0,0}, {0,1,0}, {0.6,0,0.2}, {0.6,1,0.2}, {1.4,0,0.2}, {1.4,1,0.2}, {2,0,0}, {2,1,0} });
    Panel.Expect("curved boundary fixture", bool(Curved));
    if (Curved)
    {
        const auto Neighbours = Curved.Payload.Transformed(Mat4::Translation({0,1,0})).SplitU(0.35);
        const auto Joined = BrepBody::Sew({Curved.Payload, Neighbours.first, Neighbours.second}, ScalarCriteria::MergeTolerance, false, false, true);
        const auto Report = Joined ? Joined.Payload.Validate() : BodyReport{};
        Panel.Expect("curved unequal boundaries sew without planar assumptions", Report.Faces == 3 && Report.Edges == 10 && Report.OpenEdges == 7 && Report.Hulls == 1 && Report.Oriented);
    }
    const auto CylinderSurface = NurbsSurface::Cylinder({}, Vec3::UnitZ(), 1, 2);
    const auto CylinderBefore = BrepBody::Sew({CylinderSurface.Payload}, ScalarCriteria::MergeTolerance, false);
    const auto CylinderAfter = BrepBody::Sew({CylinderSurface.Payload}, ScalarCriteria::MergeTolerance, false, false, true);
    Panel.Expect("closed circular edge domains are not discarded", CylinderBefore && CylinderAfter &&
        CylinderBefore.Payload.Validate().Edges == CylinderAfter.Payload.Validate().Edges &&
        CylinderBefore.Payload.Validate().OpenEdges == CylinderAfter.Payload.Validate().OpenEdges);

    Panel.Section("Explicit knots for exact editable feature chains");
    Panel.Expect("clear before curve knots", Host.Execute("reset"));
    Panel.Expect("explicit curve parameterization", Host.Execute("cpcurve (0,0,0) (1,0,0) (2,1,0) --degree=1 --knots=0,0,0.2,1,1 --name=ExactGuide"));
    const auto* ExactGuide = Figure(Host, "ExactGuide");
    Panel.Expect("explicit curve knots are used", ExactGuide && ExactGuide->Curve.Sample(.2).Distance({1,0,0}) < 1e-12);
    const auto CurveFingerprint = UndoSequence::Fingerprint(Host.Document());
    Panel.Expect("wrong curve knot count refused", !Host.Execute("cpcurve (0,0) (1,0) --degree=1 --knots=0,1"));
    Panel.Expect("decreasing curve knots refused", !Host.Execute("cpcurve (0,0) (1,0) --degree=1 --knots=0,1,0,1"));
    Panel.Expect("nonfinite curve knots refused", !Host.Execute("cpcurve (0,0) (1,0) --degree=1 --knots=0,0,nan,1"));
    Panel.Expect("ambiguous periodic custom knots refused", !Host.Execute("cpcurve (0,0) (1,0) (1,1) --degree=1 --periodic --knots=0,0,.5,1,1"));
    Panel.Expect("empty explicit curve domain refused", !Host.Execute("cpcurve (0,0) (1,0) --degree=1 --knots=0,0,0,0"));
    Panel.Expect("unclamped explicit curve refused", !Host.Execute("cpcurve (0,0) (1,0) --degree=1 --knots=-1,0,1,2"));
    Panel.Expect("discontinuous internal curve knot refused", !Host.Execute("cpcurve (0,0) (1,0) (1,1) (2,1) --degree=1 --knots=0,0,.5,.5,1,1"));
    Panel.Expect("invalid curves leave scene intact", CurveFingerprint == UndoSequence::Fingerprint(Host.Document()));
    const auto CurvePath = (Directory / "curve-knots.arc").generic_string();
    Panel.Expect("save explicit curve", Host.Execute("save \"" + CurvePath + "\""));
    std::ifstream CurveFile(CurvePath);
    const std::string CurveText((std::istreambuf_iterator<char>(CurveFile)), std::istreambuf_iterator<char>());
    Panel.Expect("old builds must refuse explicit curve knots", CurveText.find("require curve-knots") != std::string::npos);
    Panel.Expect("reopen explicit curve", Host.Execute("open \"" + CurvePath + "\""));
    Panel.Expect("curve parameters survive persistence", CurveFingerprint == UndoSequence::Fingerprint(Host.Document()));

    Panel.Section("Numerical snapshot retention is bounded for dense imported skins");
    SceneDocument Snapshot = Host.Document();
    UndoSequence Bounded;
    Bounded.CapGeometry(1);
    Bounded.Record(Snapshot, "first tint");
    Snapshot.Figures().front().Tint[0] = 0.13f;
    Panel.Expect("first bounded snapshot settles", Bounded.Settle(Snapshot));
    const uint64_t BeforeSecond = UndoSequence::Fingerprint(Snapshot);
    Bounded.Record(Snapshot, "second tint");
    Snapshot.Figures().front().Tint[0] = 0.24f;
    Panel.Expect("second bounded snapshot settles", Bounded.Settle(Snapshot));
    const uint64_t AfterSecond = UndoSequence::Fingerprint(Snapshot);
    Panel.Expect("old snapshots evicted but latest retained", Bounded.UndoEntries().size() == 1);
    Panel.Expect("geometry accounting includes B-rep payload", Bounded.RetainedGeometryBytes() == UndoSequence::GeometryBytes(Snapshot));
    Panel.Expect("latest undo remains usable above a tiny budget", Bounded.Undo(Snapshot) == "second tint");
    Panel.Expect("bounded undo restores exact document", UndoSequence::Fingerprint(Snapshot) == BeforeSecond);
    Panel.Expect("redo remains available", Bounded.CanRedo());
    Panel.Expect("bounded redo returns label", Bounded.Redo(Snapshot) == "second tint");
    Panel.Expect("bounded redo restores exact document", UndoSequence::Fingerprint(Snapshot) == AfterSecond);
    Bounded.Record(Snapshot, "unchanged");
    Panel.Expect("unchanged snapshots do not enter the history", !Bounded.Settle(Snapshot));
    Panel.Expect("unchanged operation retains latest undo", Bounded.UndoEntries().size() == 1);
    Bounded.Clear();
    Panel.Expect("clearing releases retained geometry accounting", Bounded.RetainedGeometryBytes() == 0 && !Bounded.CanUndo() && !Bounded.CanRedo());

    return Panel.Conclude();
}
