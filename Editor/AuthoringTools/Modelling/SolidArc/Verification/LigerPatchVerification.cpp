//============================================================================================================================================
//                                                       LIGERPATCHVERIFICATION.CPP
//============================================================================================================================================
// 📦 Native patch-replacement validation, unaffected-support comparison, mirrored layout and review rendering.

#include "Console/ConsoleHost.h"
#include "Document/UndoSequence.h"
#include "VerificationPanel.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

using namespace Frontier;

namespace
{
std::vector<std::string> ParseSurfaceNames(const std::string& Path)
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

double MeasureOpenPerimeterDiscrepancy(const BrepBody& Source, const BrepBody& Target)
{
    std::vector<const NurbsCurve*> Curves;
    std::vector<Box3> Bounds;
    for (const auto& Edge : Target.Edges) if (Edge.Coedges.size() == 1)
    {
        Curves.push_back(&Edge.Curve);
        Bounds.push_back(Edge.Curve.Bounds());
    }
    double Maximum = 0;
    for (const auto& Edge : Source.Edges) if (Edge.Coedges.size() == 1)
        for (int Sample = 0; Sample <= 16; ++Sample)
        {
            const Vec3 Point = Edge.Curve.Sample(Edge.Curve.DomainStart() +
                (Edge.Curve.DomainEnd() - Edge.Curve.DomainStart()) * Sample / 16.0);
            double Closest = ScalarCriteria::Infinity;
            for (size_t Index = 0; Index < Curves.size(); ++Index)
            {
                const Vec3 Near = Vec3::Max(Bounds[Index].Low, Vec3::Min(Bounds[Index].High, Point));
                if (Near.Distance(Point) > std::min(Closest, 1e-5)) continue;
                double Distance = 0;
                (void)Curves[Index]->ClosestParameter(Point, &Distance);
                Closest = std::min(Closest, Distance);
            }
            Maximum = std::max(Maximum, Closest);
        }
    return Maximum;
}

bool CompareSurfaceCoefficients(const NurbsSurface& First, const NurbsSurface& Second)
{
    if (First.DegreeU != Second.DegreeU || First.DegreeV != Second.DegreeV || First.CountU != Second.CountU ||
        First.CountV != Second.CountV || First.KnotsU != Second.KnotsU || First.KnotsV != Second.KnotsV ||
        First.Poles.size() != Second.Poles.size()) return false;
    for (size_t Index = 0; Index < First.Poles.size(); ++Index)
    {
        const Vec4& FirstPole = First.Poles[Index];
        const Vec4& SecondPole = Second.Poles[Index];
        if (FirstPole.X != SecondPole.X || FirstPole.Y != SecondPole.Y || FirstPole.Z != SecondPole.Z || FirstPole.W != SecondPole.W) return false;
    }
    return true;
}
}

int main(int Count, char** Arguments)
{
    if (Count != 4 && Count != 5) return 2;
    if (Count == 5 && std::string(Arguments[4]) != "--orange") return 2;
    const std::filesystem::path Destination = Arguments[3];
    std::filesystem::create_directories(Destination);
    VerificationPanel Panel("Liger guide-bounded patch repairs");
    ConsoleHost Before(Destination.string(), 2000, 1250), After(Destination.string(), 2000, 1250);
    Panel.Expect("approved review opens", Before.Execute(std::string("open \"") + Arguments[1] + "\""));
    Panel.Expect("repaired body and live roof offset open", After.Execute(std::string("open \"") + Arguments[2] + "\""));
    const auto* Original = Before.Document().Find("Liger_Main_Body");
    const auto* Repaired = After.Document().Find("Liger_Main_Body");
    if (!Original || !Repaired) return 2;
    const auto OldReport = Original->Body.Validate();
    const auto NewReport = Repaired->Body.Validate();
    std::cout << "PATCH_REPAIR faces=" << NewReport.Faces << " edges=" << NewReport.Edges
              << " open=" << NewReport.OpenEdges << " nonmanifold=" << NewReport.NonManifoldEdges
              << " misoriented=" << NewReport.MisorientedEdges << " hulls=" << NewReport.Hulls
              << " area=" << NewReport.Area << std::endl;
    Panel.Expect("real body face count reduced", NewReport.Faces < OldReport.Faces);
    Panel.Expect("one connected oriented skin", NewReport.Hulls == 1 && NewReport.NonManifoldEdges == 0 && NewReport.MisorientedEdges == 0);
    Panel.Expect("source remains an open exterior, not invented solid thickness", !NewReport.Closed);
    const double OpenDiscrepancy = std::max(MeasureOpenPerimeterDiscrepancy(Original->Body, Repaired->Body),
                                           MeasureOpenPerimeterDiscrepancy(Repaired->Body, Original->Body));
    Panel.Within("sampled bidirectional open-perimeter discrepancy [m]", OpenDiscrepancy, 5e-6);
    double OldLength = 0, NewLength = 0;
    for (const auto& Edge : Original->Body.Edges) if (Edge.Coedges.size() == 1) OldLength += Edge.Curve.Length();
    for (const auto& Edge : Repaired->Body.Edges) if (Edge.Coedges.size() == 1) NewLength += Edge.Curve.Length();
    Panel.Within("open-perimeter total length change [m]", std::fabs(NewLength - OldLength), 1e-5);
    const auto OldNames = ParseSurfaceNames(Arguments[1]), NewNames = ParseSurfaceNames(Arguments[2]);
    Panel.Expect("source and replacement surface inventories match native body", OldNames.size() == Original->Body.Faces.size() && NewNames.size() == Repaired->Body.Faces.size());
    if (OldNames.size() != Original->Body.Faces.size() || NewNames.size() != Repaired->Body.Faces.size()) return 2;
    std::map<std::string, size_t> Prior, Current;
    for (size_t Index = 0; Index < OldNames.size(); ++Index) Prior[OldNames[Index]] = Index;
    for (size_t Index = 0; Index < NewNames.size(); ++Index) Current[NewNames[Index]] = Index;
    bool Unaffected = true, Reflection = true, Regular = true, Loops = true;
    size_t Retained = 0, Replacements = 0;
    for (size_t Index = 0; Index < NewNames.size(); ++Index)
    {
        const auto& Name = NewNames[Index];
        const auto& Surface = Repaired->Body.Faces[Index].Surface;
        if (Prior.contains(Name))
        {
            ++Retained;
            Unaffected &= CompareSurfaceCoefficients(Original->Body.Faces[Prior[Name]].Surface, Surface);
        }
        else
        {
            ++Replacements;
            Regular &= !Surface.Validate();
            for (int Along = 1; Along < 40; ++Along) for (int Across = 1; Across < 40; ++Across)
            {
                Vec3 Position, First, Second;
                Surface.Derivatives(Along / 40.0, Across / 40.0, Position, First, Second);
                Regular &= First.Cross(Second).Length() > 1e-10;
            }
            const auto Position = Name.find("_Left_");
            if (Position == std::string::npos) continue;
            std::string Opposite = Name;
            Opposite.replace(Position, 6, "_Right_");
            if (!Current.contains(Opposite)) { Reflection = false; continue; }
            const auto& Other = Repaired->Body.Faces[Current[Opposite]].Surface;
            for (int Along = 0; Along <= 24; ++Along) for (int Across = 0; Across <= 24; ++Across)
            {
                Vec3 Point = Surface.Sample(Along / 24.0, Across / 24.0); Point.Y *= -1;
                Reflection &= Point.Distance(Other.Sample(1 - Along / 24.0, Across / 24.0)) < 1e-10;
            }
        }
    }
    std::cout << "PATCH_SUPPORTS retained=" << Retained << " replacements=" << Replacements << std::endl;
    Panel.Expect("unaffected surfaces coefficient-identical", Unaffected && Retained > 0);
    Panel.Expect("replacement layout reflected on both sides", Reflection && Replacements > 0);
    Panel.Expect("replacement splines valid and sampled tangents regular", Regular);
    for (const auto& Loop : Repaired->Body.Loops) for (size_t Index = 0; Index < Loop.Coedges.size(); ++Index)
    {
        const auto& FirstUse = Repaired->Body.Coedges[Loop.Coedges[Index]];
        const auto& SecondUse = Repaired->Body.Coedges[Loop.Coedges[(Index + 1) % Loop.Coedges.size()]];
        const auto& First = Repaired->Body.Edges[FirstUse.Edge]; const auto& Second = Repaired->Body.Edges[SecondUse.Edge];
        Loops &= (FirstUse.Reversed ? First.VertexStart : First.VertexEnd) == (SecondUse.Reversed ? Second.VertexEnd : Second.VertexStart);
    }
    Panel.Expect("all boundary loops close in vertex order", Loops);
    bool Guides = true, Junctions = true;
    size_t HiddenOrange = 0;
    for (const auto& Figure : Before.AllFigures())
    {
        const auto* Other = After.Document().Find(Figure.Name);
        if (Figure.Classification == FigureClassification::Curve && Figure.Name != "Guide_O01_RoofSurfaceOffset_Across")
        {
            Guides &= Other && Other->Feature == Figure.Feature;
            if (!Other) continue;
            for (int Sample = 0; Sample <= 32; ++Sample)
            {
                const double Parameter = Figure.Curve.DomainStart() + (Figure.Curve.DomainEnd() - Figure.Curve.DomainStart()) * Sample / 32.0;
                Guides &= Figure.Curve.Sample(Parameter).Distance(Other->Curve.Sample(Parameter)) < 1e-12;
            }
            if (Figure.Feature == FeaturePurpose::Repair && !Figure.Hidden && Other->Hidden) ++HiddenOrange;
        }
        if (Figure.Name.starts_with("Main_Junction_")) Junctions &= Other && CompareSurfaceCoefficients(Figure.Surface, Other->Surface);
    }
    Panel.Expect("original guide shapes and orange archive retained", Guides);
    Panel.Expect("independent source junction sheets untouched", Junctions);
    std::cout << "COMPLETED_INTERNAL_ORANGE_COPIES_HIDDEN " << HiddenOrange << std::endl;
    const auto* Offset = After.Document().Find("Guide_O01_RoofSurfaceOffset_Across");
    Panel.Expect("25 mm roof offset remains live and visible", Offset && Offset->Recipe.Operation == RecipeOperation::SurfaceOffset &&
                 std::fabs(Offset->Recipe.Length + .025) < 1e-12 && !Offset->Hidden && Offset->Recipe.Complaint.empty());
    const uint64_t Fingerprint = UndoSequence::Fingerprint(After.Document());
    const auto Saved = (Destination / "PatchRoundtrip.arc").generic_string();
    Panel.Expect("save native repair", After.Execute("save \"" + Saved + "\""));
    Panel.Expect("reopen native repair", After.Execute("open \"" + Saved + "\""));
    Panel.Expect("native replay retains geometry and recipes", Fingerprint == UndoSequence::Fingerprint(After.Document()));
    std::filesystem::remove(Saved);
    for (auto* Host : {&Before, &After})
        Panel.Expect("review display", Host->Execute("show cages off; show iso off; show edges on; show features on; show shading plastic"));
    const char* Views[] = {"Front_Quarter", "Rear_Quarter", "Side", "Top"};
    const char* Cameras[] = {"view front; view orbit 55 23; view persp", "view front; view orbit -55 23; view persp", "view front", "view top"};
    for (int Index = 0; Index < 4; ++Index)
    {
        Panel.Expect("review camera", Before.Execute(Cameras[Index]) && Before.Execute("view fit; view dolly 1.5"));
        After.Camera() = Before.Camera();
        Panel.Expect("original review image", Before.Execute(std::string("render Liger_Patch_Before_") + Views[Index] + " --size=2000x1250"));
        Panel.Expect("repaired review image", After.Execute(std::string("render Liger_Patch_After_") + Views[Index] + " --size=2000x1250"));
    }
    Panel.Expect("roof detail camera", Before.Execute("line (.05,-.90,1.1) (1.5,.90,1.1) --name=PatchInspection; select PatchInspection; view top; view fit selected; delete PatchInspection"));
    After.Camera() = Before.Camera();
    Panel.Expect("original roof detail", Before.Execute("render Liger_Patch_Before_Detail --size=2000x1400"));
    Panel.Expect("repaired roof detail", After.Execute("render Liger_Patch_After_Detail --size=2000x1400"));
    if (Count == 5)
    {
        const char* DetailNames[] = {"Front_Orange", "Rear_Orange"};
        const char* DetailCameras[] = {
            "line (1.35,-1.22,.4) (2.62,0,1.02) --name=OrangeInspection; select OrangeInspection; view front; view orbit 55 27; view fit selected; delete OrangeInspection",
            "line (-1.9,-1.22,.4) (-.6,0,1.05) --name=OrangeInspection; select OrangeInspection; view front; view orbit -55 27; view fit selected; delete OrangeInspection"
        };
        for (int Index = 0; Index < 2; ++Index)
        {
            Panel.Expect("orange detail camera", Before.Execute(DetailCameras[Index]));
            After.Camera() = Before.Camera();
            Panel.Expect("original orange detail", Before.Execute(std::string("render Liger_Patch_Before_") + DetailNames[Index] + " --size=2000x1400"));
            Panel.Expect("repaired orange detail", After.Execute(std::string("render Liger_Patch_After_") + DetailNames[Index] + " --size=2000x1400"));
        }
    }
    return Panel.Conclude();
}
