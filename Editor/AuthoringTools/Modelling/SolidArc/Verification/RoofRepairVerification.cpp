//============================================================================================================================================
//                                                      ROOFREPAIRVERIFICATION.CPP
//============================================================================================================================================
// Native original/repaired document comparison. The source remains an open exterior skin, not a manufactured solid.
#include "Console/ConsoleHost.h"
#include "Document/UndoSequence.h"
#include "VerificationPanel.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>

using namespace Frontier;

namespace
{
    std::vector<std::string> FaceNames(const std::string& Path)
    {
        std::ifstream Input(Path);
        std::vector<std::string> Names;
        for (std::string Line; std::getline(Input, Line);)
        {
            if (!Line.starts_with("sew ")) continue;
            Names.clear();
            std::istringstream Tokens(Line.substr(4));
            for (std::string Token; Tokens >> Token;) if (!Token.starts_with("--")) Names.push_back(Token);
        }
        return Names;
    }

    bool SameSurface(const NurbsSurface& First, const NurbsSurface& Second)
    {
        if (First.DegreeU != Second.DegreeU || First.DegreeV != Second.DegreeV || First.CountU != Second.CountU ||
            First.CountV != Second.CountV || First.KnotsU != Second.KnotsU || First.KnotsV != Second.KnotsV) return false;
        for (size_t Index = 0; Index < First.Poles.size(); ++Index)
        {
            const Vec4& A = First.Poles[Index]; const Vec4& B = Second.Poles[Index];
            if (A.X != B.X || A.Y != B.Y || A.Z != B.Z || A.W != B.W) return false;
        }
        return true;
    }
}

int main(int Count, char** Arguments)
{
    if (Count != 3 && Count != 4) return 2;
    VerificationPanel Panel("SolidArc · first orange roof repair");
    const std::filesystem::path Destination = Count == 4 ? Arguments[3] : SOLIDARC_PROOF_FOLDER "/RoofRepairVerification";
    std::filesystem::create_directories(Destination);
    ConsoleHost Original(Destination.string(), 1600, 1000), Repaired(Destination.string(), 1600, 1000);
    Panel.Expect("original opens", Original.Execute(std::string("open \"") + Arguments[1] + "\""));
    Panel.Expect("repaired opens", Repaired.Execute(std::string("open \"") + Arguments[2] + "\""));
    const auto* Before = Original.Document().Find("Liger_Main_Body");
    const auto* After = Repaired.Document().Find("Liger_Main_Body");
    if (!Before || !After) return 2;
    const BodyReport OldReport = Before->Body.Validate(), NewReport = After->Body.Validate();
    std::cout << "REPAIR_TOPOLOGY faces=" << NewReport.Faces << " edges=" << NewReport.Edges << " vertices=" << NewReport.Vertices
              << " open=" << NewReport.OpenEdges << " nonmanifold=" << NewReport.NonManifoldEdges
              << " misoriented=" << NewReport.MisorientedEdges << " hulls=" << NewReport.Hulls << '\n';
    Panel.Expect("twenty fewer real faces, not hidden boundary edges", OldReport.Faces == 1094 && NewReport.Faces == 1074);
    Panel.Expect("no new open edges", NewReport.OpenEdges <= OldReport.OpenEdges);
    Panel.Expect("one connected, oriented, manifold skin", NewReport.Hulls == 1 && NewReport.NonManifoldEdges == 0 && NewReport.MisorientedEdges == 0);
    Panel.Expect("open source remains open", !NewReport.Closed);
    const auto OldNames = FaceNames(Arguments[1]), NewNames = FaceNames(Arguments[2]);
    if (OldNames.size() != Before->Body.Faces.size() || NewNames.size() != After->Body.Faces.size()) return 2;
    std::map<std::string, size_t> Lookup;
    for (size_t Index = 0; Index < OldNames.size(); ++Index) Lookup[OldNames[Index]] = Index;
    std::set<int> Changed;
    for (int Index : {80,82,102,158,160,162,182,188,222,224,310,632,680,682,1038,1040,1116})
        { Changed.insert(Index); Changed.insert(Index + 1); }
    size_t Unchanged = 0;
    bool SourceIntact = true, MirrorExact = true, Regular = true, LoopsClosed = true;
    for (size_t Index = 0; Index < NewNames.size(); ++Index)
    {
        const std::string& Name = NewNames[Index];
        const NurbsSurface& Surface = After->Body.Faces[Index].Surface;
        if (Name.starts_with("Main_") && !Changed.contains(std::stoi(Name.substr(5))))
        {
            SourceIntact = SourceIntact && Lookup.contains(Name) && SameSurface(Before->Body.Faces[Lookup[Name]].Surface, Surface);
            ++Unchanged;
        }
        if (Name.starts_with("Roof_Repair_Left_"))
        {
            const NurbsSurface& Other = After->Body.Faces[Index + 1].Surface;
            for (int U = 0; U <= 24; ++U) for (int V = 0; V <= 24; ++V)
            {
                Vec3 Position = Surface.Sample(U / 24.0, V / 24.0); Position.Y *= -1;
                MirrorExact = MirrorExact && Position.Distance(Other.Sample(1 - U / 24.0, V / 24.0)) < 1e-11;
                Vec3 Point, Along, Across; Surface.Derivatives(U / 24.0, V / 24.0, Point, Along, Across);
                Regular = Regular && Along.Cross(Across).Z > 1e-8;
            }
        }
    }
    for (const BrepLoop& Loop : After->Body.Loops)
        for (size_t Index = 0; Index < Loop.Coedges.size(); ++Index)
        {
            const auto& A = After->Body.Coedges[Loop.Coedges[Index]];
            const auto& B = After->Body.Coedges[Loop.Coedges[(Index + 1) % Loop.Coedges.size()]];
            const auto& First = After->Body.Edges[A.Edge]; const auto& Second = After->Body.Edges[B.Edge];
            LoopsClosed = LoopsClosed && (A.Reversed ? First.VertexStart : First.VertexEnd) == (B.Reversed ? Second.VertexEnd : Second.VertexStart);
        }
    Panel.Expect("1060 unaffected body surfaces are coefficient-identical", SourceIntact && Unchanged == 1060);
    Panel.Expect("replacement layout is mirrored", MirrorExact);
    Panel.Expect("replacement has no sampled fold or collapsed fan", Regular);
    Panel.Expect("split boundary loops close in vertex order", LoopsClosed);
    bool GuidesIntact = true, JunctionsIntact = true;
    size_t Guides = 0, Junctions = 0, HiddenRepairs = 0;
    for (const auto& Figure : Original.AllFigures())
    {
        const auto* Other = Repaired.Document().Find(Figure.Name);
        if (Figure.Classification == FigureClassification::Curve)
        {
            ++Guides;
            GuidesIntact = GuidesIntact && Other && Other->Feature == Figure.Feature && std::equal(std::begin(Other->Tint), std::end(Other->Tint), std::begin(Figure.Tint));
            if (!Other) continue;
            for (int Sample = 0; Sample <= 32; ++Sample)
            {
                const double Parameter = Figure.Curve.DomainStart() + (Figure.Curve.DomainEnd() - Figure.Curve.DomainStart()) * Sample / 32.0;
                GuidesIntact = GuidesIntact && Figure.Curve.Sample(Parameter).Distance(Other->Curve.Sample(Parameter)) < 1e-12;
            }
            if (Other->Hidden) ++HiddenRepairs;
        }
        else if (Figure.Name.starts_with("Main_Junction_"))
        {
            ++Junctions; JunctionsIntact = JunctionsIntact && Other && SameSurface(Figure.Surface, Other->Surface);
        }
    }
    Panel.Expect("all 1800 accepted feature and orange curves retained unchanged", GuidesIntact && Guides == 1800 && HiddenRepairs == 30);
    Panel.Expect("all 24 independent source junction sheets preserved", JunctionsIntact && Junctions == 24);
    const uint64_t Fingerprint = UndoSequence::Fingerprint(Repaired.Document());
    const std::string Saved = (Destination / "RoofRoundtrip.arc").generic_string();
    Panel.Expect("save repair", Repaired.Execute("save \"" + Saved + "\""));
    Panel.Expect("reopen repair", Repaired.Execute("open \"" + Saved + "\""));
    Panel.Expect("repair geometry and annotations survive replay", Fingerprint == UndoSequence::Fingerprint(Repaired.Document()));
    std::filesystem::remove(Saved);
    if (Count == 4)
    {
        const char* Views[] = { "Front_Quarter", "Rear_Quarter", "Side", "Top" };
        const char* Cameras[] = { "view front; view orbit 55 23; view persp", "view front; view orbit -55 23; view persp", "view front", "view top" };
        for (ConsoleHost* Host : { &Original, &Repaired })
            Panel.Expect("annotation-ready display", Host->Execute("show cages off; show iso off; show edges on; show features off; show shading plastic"));
        for (int Index = 0; Index < 4; ++Index)
        {
            Panel.Expect("original camera", Original.Execute(Cameras[Index]) && Original.Execute("view fit; view dolly 1.5"));
            Repaired.Camera() = Original.Camera();
            Panel.Expect("original render", Original.Execute(std::string("render Liger_Roof_Original_") + Views[Index] + " --size=1600x1000"));
            Panel.Expect("repaired render", Repaired.Execute(std::string("render Liger_Roof_New_") + Views[Index] + " --size=1600x1000"));
        }
        Panel.Expect("close-up camera", Original.Execute("line (-.005,.27,1.135) (.31,.46,1.135) --name=Inspection_Frame; select Inspection_Frame; view top; view fit selected; delete Inspection_Frame"));
        Repaired.Camera() = Original.Camera();
        Panel.Expect("original close-up", Original.Execute("render Liger_Roof_Original_Closeup --size=1600x1000"));
        Panel.Expect("repaired close-up", Repaired.Execute("render Liger_Roof_New_Closeup --size=1600x1000"));
    }
    return Panel.Conclude();
}
