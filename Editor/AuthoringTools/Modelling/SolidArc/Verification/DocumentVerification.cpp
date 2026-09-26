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
    const std::filesystem::path Directory = "/tmp/SolidArcDocumentVerification";
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
    Panel.Expect("save accepts a stem and appends .arc", Host.Execute("save " + Stem.string()));
    Panel.Expect("native .arc was written", std::filesystem::exists(Document));
    std::ifstream First(Document);
    std::string Header; std::getline(First, Header);
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
    Panel.Expect("open .arc succeeds", Host.Execute("open " + Document.string()));
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
    Panel.Expect("malformed native document is refused", !Host.Execute("open " + Invalid.string()));
    Panel.Expect("failed open preserves the current geometry", UndoSequence::Fingerprint(Host.Document()) == BeforeBadOpen);
    Panel.Expect("failed open preserves the current figure count", Host.AllFigures().size() == FiguresBeforeBadOpen);
    Panel.Expect("wrong native extension is refused", !Host.Execute("open " + (Directory / "not-a-document.txt").string()));

    return Panel.Conclude();
}
