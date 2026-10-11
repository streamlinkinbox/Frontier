//============================================================================================================================================
//                                                           GUIDEVERIFICATION.CPP
//============================================================================================================================================
// 📦 Native surface projection, unchanged-skin checks and named candidate-guide review rendering.

#include "Console/ConsoleHost.h"
#include "Document/UndoSequence.h"
#include "VerificationPanel.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace Frontier;

namespace
{
struct SurfaceProjection
{
    const BrepBody& Body;
    std::vector<Box3> Bounds;
    std::vector<std::array<Vec3, 81>> Seeds;

    explicit SurfaceProjection(const BrepBody& Skin) : Body(Skin)
    {
        for (const auto& Face : Body.Faces)
        {
            Bounds.push_back(Face.Surface.Bounds());
            std::array<Vec3, 81> Positions;
            for (int U = 0; U <= 8; ++U) for (int V = 0; V <= 8; ++V)
                Positions[U * 9 + V] = Face.Surface.Sample(U / 8.0, V / 8.0);
            Seeds.push_back(Positions);
        }
    }

    double Refine(const NurbsSurface& Surface, Vec3 Target, double& U, double& V) const
    {
        double Distance = Surface.Sample(U, V).Distance(Target);
        for (int Iteration = 0; Iteration < 60 && Distance > 1e-10; ++Iteration)
        {
            Vec3 Position, Along, Across; Surface.Derivatives(U, V, Position, Along, Across);
            const Vec3 Difference = Target - Position;
            const double A = Along.Dot(Along), B = Along.Dot(Across), C = Across.Dot(Across), Determinant = A * C - B * B;
            if (std::fabs(Determinant) < 1e-28) break;
            const double StepAlong = (Difference.Dot(Along) * C - Difference.Dot(Across) * B) / Determinant;
            const double StepAcross = (Difference.Dot(Across) * A - Difference.Dot(Along) * B) / Determinant;
            bool Improved = false;
            for (double Scale = 1; Scale >= 1.0 / 1024; Scale *= 0.5)
            {
                const double NextU = ScalarCriteria::Clamp(U + Scale * StepAlong, Surface.DomainStartU(), Surface.DomainEndU());
                const double NextV = ScalarCriteria::Clamp(V + Scale * StepAcross, Surface.DomainStartV(), Surface.DomainEndV());
                const double NextDistance = Surface.Sample(NextU, NextV).Distance(Target);
                if (NextDistance < Distance)
                {
                    Improved = true; U = NextU; V = NextV; Distance = NextDistance; break;
                }
            }
            if (!Improved) break;
        }
        return Distance;
    }

    Vec3 Project(Vec3 Point, int& FaceIndex, double& Along, double& Across, double& Distance) const
    {
        std::vector<std::pair<double, int>> Candidates;
        for (size_t Index = 0; Index < Bounds.size(); ++Index)
        {
            const auto& BoundsOfFace = Bounds[Index];
            const Vec3 Closest = Vec3::Max(BoundsOfFace.Low, Vec3::Min(BoundsOfFace.High, Point));
            Candidates.emplace_back(Closest.Distance(Point), static_cast<int>(Index));
        }
        std::sort(Candidates.begin(), Candidates.end());
        Distance = ScalarCriteria::Infinity; FaceIndex = -1;
        Vec3 Result;
        for (const auto& [LowerDistance, Index] : Candidates)
        {
            if (LowerDistance > Distance + 1e-9) break;
            std::array<std::pair<double, int>, 81> Ranked;
            for (int Seed = 0; Seed < 81; ++Seed) Ranked[Seed] = { Seeds[Index][Seed].Distance(Point), Seed };
            std::partial_sort(Ranked.begin(), Ranked.begin() + 3, Ranked.end());
            for (int Slot = 0; Slot < 3; ++Slot)
            {
                double U = (Ranked[Slot].second / 9) / 8.0, V = (Ranked[Slot].second % 9) / 8.0;
                const double Error = Refine(Body.Faces[Index].Surface, Point, U, V);
                if (Error < Distance)
                {
                    Distance = Error; FaceIndex = Index; Along = U; Across = V;
                    Result = Body.Faces[Index].Surface.Sample(U, V);
                }
            }
        }
        return Result;
    }
};

bool SameSurface(const NurbsSurface& First, const NurbsSurface& Second)
{
    if (First.CountU != Second.CountU || First.CountV != Second.CountV || First.DegreeU != Second.DegreeU ||
        First.DegreeV != Second.DegreeV || First.KnotsU != Second.KnotsU || First.KnotsV != Second.KnotsV) return false;
    for (size_t Index = 0; Index < First.Poles.size(); ++Index)
    {
        const auto& A = First.Poles[Index]; const auto& B = Second.Poles[Index];
        if (A.X != B.X || A.Y != B.Y || A.Z != B.Z || A.W != B.W) return false;
    }
    return true;
}
void VerifyLayout(VerificationPanel& Panel, const BrepBody& Before, const BrepBody& After, const std::string& Path)
{
    std::ifstream Input(std::filesystem::path(Path).replace_extension(".layout.tsv"));
    std::vector<std::array<double, 6>> Rows;
    for (std::array<double, 6> Row; Input >> Row[0] >> Row[1] >> Row[2] >> Row[3] >> Row[4] >> Row[5];) Rows.push_back(Row);
    bool Valid = Input.eof() && Rows.size() == After.Faces.size();
    std::vector<bool> Seen(After.Faces.size());
    std::vector<double> Area(Before.Faces.size());
    std::vector<std::vector<int>> Children(Before.Faces.size());
    double Maximum = 0;
    size_t Unchanged = 0;
    for (const auto& Row : Rows)
    {
        const int Old = static_cast<int>(Row[0]), New = static_cast<int>(Row[1]);
        if (Old < 0 || New < 0 || Old >= static_cast<int>(Before.Faces.size()) || New >= static_cast<int>(After.Faces.size()) ||
            Seen[New] || Row[0] != Old || Row[1] != New || Row[2] < 0 || Row[3] > 1 || Row[4] < 0 || Row[5] > 1 ||
            Row[2] >= Row[3] || Row[4] >= Row[5]) { Valid = false; continue; }
        Seen[New] = true;
        Area[Old] += (Row[3] - Row[2]) * (Row[5] - Row[4]);
        Children[Old].push_back(New);
        const auto& A = Before.Faces[Old].Surface;
        const auto& B = After.Faces[New].Surface;
        if (Row[2] == 0 && Row[3] == 1 && Row[4] == 0 && Row[5] == 1)
        {
            Valid = Valid && SameSurface(A, B);
            ++Unchanged;
        }
        for (int U = 0; U <= 16; ++U) for (int V = 0; V <= 16; ++V)
            Maximum = std::max(Maximum, A.Sample(Row[2] + (Row[3] - Row[2]) * U / 16.0,
                Row[4] + (Row[5] - Row[4]) * V / 16.0).Distance(B.Sample(U / 16.0, V / 16.0)));
    }
    for (double Value : Area) Valid = Valid && std::fabs(Value - 1) < 1e-12;
    for (size_t A = 0; A < Rows.size(); ++A) for (size_t B = A + 1; B < Rows.size(); ++B)
        if (Rows[A][0] == Rows[B][0])
            Valid = Valid && std::max(0.0, std::min(Rows[A][3], Rows[B][3]) - std::max(Rows[A][2], Rows[B][2])) *
                std::max(0.0, std::min(Rows[A][5], Rows[B][5]) - std::max(Rows[A][4], Rows[B][4])) < 1e-14;
    Panel.Expect("complete nonoverlapping source-face coverage", Valid && Unchanged == 1066);
    Panel.Within("native restricted-face position error [m]", Maximum, 1e-10);
    size_t Cuts = 0;
    bool Joined = true;
    for (const auto& Pair : Children)
    {
        if (Pair.size() == 1) continue;
        ++Cuts;
        bool Shared = false;
        if (Pair.size() != 2) { Joined = false; continue; }
        for (const auto& Edge : After.Edges)
        {
            if (Edge.Coedges.size() != 2) continue;
            const int A = After.Coedges[Edge.Coedges[0]].Face, B = After.Coedges[Edge.Coedges[1]].Face;
            if ((A == Pair[0] && B == Pair[1]) || (B == Pair[0] && A == Pair[1])) Shared = true;
        }
        Joined = Joined && Shared;
    }
    Panel.Expect("eight roof supports split into genuinely sewn face pairs", Joined && Cuts == 8);
}
}

int main(int Count, char** Arguments)
{
    if (Count == 5 && std::string(Arguments[1]) == "--project")
    {
        ConsoleHost Host(SOLIDARC_PROOF_FOLDER);
        if (!Host.Execute(std::string("open \"") + Arguments[2] + "\"")) return 2;
        const auto* Skin = Host.Document().Find("Liger_Main_Body"); if (!Skin) return 2;
        SurfaceProjection Projection(Skin->Body);
        std::ifstream Input(Arguments[3]); std::ofstream Output(Arguments[4]);
        Output << std::setprecision(17);
        size_t Samples = 0;
        for (Vec3 Point; Input >> Point.X >> Point.Y >> Point.Z;)
        {
            int Face = -1; double U = 0, V = 0, Distance = 0;
            const Vec3 Projected = Projection.Project(Point, Face, U, V, Distance);
            if (Face < 0 || !std::isfinite(Distance)) return 2;
            Output << Projected.X << ' ' << Projected.Y << ' ' << Projected.Z << ' ' << Face << ' ' << U << ' ' << V << ' ' << Distance << '\n';
            ++Samples;
        }
        std::cout << "PROJECTED_NATIVE_SURFACE_SAMPLES " << Samples << '\n';
        return Input.eof() && Samples && Output.good() ? 0 : 2;
    }
    const bool Layout = Count >= 4 && std::string(Arguments[1]) == "--layout";
    const bool Consolidated = Layout || (Count >= 4 && std::string(Arguments[1]) == "--consolidated");
    if (Consolidated)
    {
        --Count;
        ++Arguments;
    }
    if (Count != 3 && Count != 4) return 2;
    VerificationPanel Panel("SolidArc · on-surface curve candidates");
    const std::filesystem::path Destination = Count == 4 ? Arguments[3] :
        (Layout ? SOLIDARC_PROOF_FOLDER "/LayoutVerification" : Consolidated ? SOLIDARC_PROOF_FOLDER "/ConsolidationVerification" : SOLIDARC_PROOF_FOLDER "/GuideVerification");
    std::filesystem::create_directories(Destination);
    ConsoleHost Before(Destination.string(), 2000, 1250), After(Destination.string(), 2000, 1250);
    Panel.Expect("repaired reference opens", Before.Execute(std::string("open \"") + Arguments[1] + "\""));
    Panel.Expect("candidate document opens", After.Execute(std::string("open \"") + Arguments[2] + "\""));
    const auto* Original = Before.Document().Find("Liger_Main_Body");
    const auto* Current = After.Document().Find("Liger_Main_Body");
    if (!Original || !Current) return 2;
    bool Intact = Original->Body.Faces.size() == Current->Body.Faces.size();
    if (Intact) for (size_t Index = 0; Index < Original->Body.Faces.size(); ++Index)
        Intact = Intact && SameSurface(Original->Body.Faces[Index].Surface, Current->Body.Faces[Index].Surface);
    if (Layout) VerifyLayout(Panel, Original->Body, Current->Body, Arguments[2]);
    else Panel.Expect("all repaired body surface coefficients unchanged", Intact);
    const auto Report = Current->Body.Validate();
    std::cout << "LAYOUT_TOPOLOGY faces=" << Report.Faces << " edges=" << Report.Edges << " open=" << Report.OpenEdges
              << " nonmanifold=" << Report.NonManifoldEdges << " misoriented=" << Report.MisorientedEdges << " hulls=" << Report.Hulls << '\n';
    Panel.Expect(Layout ? "new roof faces, no new holes or orientation faults" : "skin topology unchanged",
        (Layout ? Report.Faces == 1082 && Report.OpenEdges <= 732 : Report.Faces == 1074 && Report.Edges == 9107 && Report.OpenEdges == 732) &&
        Report.NonManifoldEdges == 0 && Report.MisorientedEdges == 0 && Report.Hulls == 1);
    bool Existing = true;
    for (const auto& Figure : Before.AllFigures())
    {
        const auto* Other = After.Document().Find(Figure.Name);
        if (!Other) { Existing = false; continue; }
        if (Figure.Classification == FigureClassification::Surface) Existing = Existing && SameSurface(Figure.Surface, Other->Surface);
        if (Figure.Classification == FigureClassification::Curve)
            for (int Sample = 0; Sample <= 16; ++Sample)
            {
                const double Parameter = Figure.Curve.DomainStart() + (Figure.Curve.DomainEnd() - Figure.Curve.DomainStart()) * Sample / 16.0;
                Existing = Existing && Figure.Curve.Sample(Parameter).Distance(Other->Curve.Sample(Parameter)) < 1e-12 && Figure.Feature == Other->Feature &&
                    (Figure.Feature != FeaturePurpose::Repair || Figure.Hidden == Other->Hidden);
            }
    }
    Panel.Expect("accepted guides and orange selections retained", Existing);
    SurfaceProjection Projection(Current->Body);
    size_t Guides = 0, Proposed = 0, MirrorPairs = 0, LongCurves = 0; double Maximum = 0, MirrorError = 0;
    bool Tagged = true;
    std::ofstream Measurements(Destination / "GuideNative.csv"); Measurements << std::setprecision(17) << "name,maximumSurfaceDistanceMm,samples\n";
    for (const auto& Figure : After.AllFigures())
    {
        if (!Figure.Name.starts_with("Guide_") || (Consolidated && Figure.Hidden)) continue;
        ++Guides;
        Tagged = Tagged && Figure.Classification == FigureClassification::Curve && Figure.Feature == FeaturePurpose::Design;
        if ((Figure.Name[6] == 'B' || Figure.Name[6] == 'G') && std::stoi(Figure.Name.substr(7, 2)) <= 56) ++LongCurves;
        if (Figure.Name.ends_with("_Left"))
        {
            const auto* Opposite = After.Document().Find(Figure.Name.substr(0, Figure.Name.size() - 5) + "_Right");
            if (!Opposite) return 2;
            Tagged = Tagged && (!Consolidated || !Opposite->Hidden);
            ++MirrorPairs;
            for (int Sample = 0; Sample <= 128; ++Sample)
            {
                Vec3 Point = Figure.Curve.Sample(Sample / 128.0); Point.Y *= -1;
                MirrorError = std::max(MirrorError, Point.Distance(Opposite->Curve.Sample(Sample / 128.0)));
            }
        }
        if (Figure.Name.starts_with("Guide_Y") || Figure.Name.starts_with("Guide_M") || Figure.Name.starts_with("Guide_R")) ++Proposed;
        double Error = 0;
        for (int Sample = 0; Sample <= 128; ++Sample)
        {
            const Vec3 Point = Figure.Curve.Sample(Sample / 128.0);
            int Face = -1; double U = 0, V = 0, Distance = 0;
            (void)Projection.Project(Point, Face, U, V, Distance);
            Error = std::max(Error, Distance);
        }
        Maximum = std::max(Maximum, Error);
        Measurements << Figure.Name << ',' << Error * 1000 << ",129\n";
    }
    Panel.Expect(Consolidated ? "selected consolidated guides are native features" : "all long chains and proposed guides are native features",
        Tagged && (Consolidated ? Guides == 53 && Proposed == 4 : Guides == 179 && LongCurves == 81 && Proposed == 26));
    Panel.Expect("bilateral candidate curves are paired", MirrorPairs == (Consolidated ? 16u : 73u));
    Panel.Within("native reflected candidate position error [m]", MirrorError, 1e-10);
    Panel.Within("sampled guide distance to actual native skin [mm]", Maximum * 1000, 1.2);
    if (Consolidated)
    {
        bool Excluded = true;
        for (const auto& Figure : After.AllFigures())
            if (Figure.Name.starts_with("Guide_Y") || Figure.Name.starts_with("Guide_M") ||
                Figure.Name.starts_with("Guide_R03_") || Figure.Name.starts_with("Guide_R04_") ||
                Figure.Name.starts_with("Guide_R05_")) Excluded = Excluded && Figure.Hidden;
        Panel.Expect("rejected options and superseded join fragments hidden", Excluded);
        const char* Borders[] = { "Guide_B20_RearInnerArch", "Guide_B22_FrontOuterArch", "Guide_G23_FrontInnerArch",
            "Guide_C01_JoinedRoofRail", "Guide_C02_JoinedRearOuterArch" };
        bool Separate = true;
        for (const auto* Name : Borders) for (const auto* Side : { "_Left", "_Right" })
        {
            const auto* Figure = After.Document().Find(std::string(Layout && std::string(Name) == "Guide_C01_JoinedRoofRail" ? "Guide_C01_Extended" : Name) + Side);
            Separate = Separate && Figure && !Figure->Hidden;
        }
        Panel.Expect("joined rails and all four arch borders are shown bilaterally", Separate);
    }
    const auto Fingerprint = UndoSequence::Fingerprint(After.Document());
    const std::string Saved = (Destination / "GuideRoundtrip.arc").generic_string();
    Panel.Expect("save candidates", After.Execute("save \"" + Saved + "\""));
    Panel.Expect("reopen candidates", After.Execute("open \"" + Saved + "\""));
    Panel.Expect("names, geometry, colour and visibility survive replay", Fingerprint == UndoSequence::Fingerprint(After.Document()));
    std::filesystem::remove(Saved);
    if (Count == 4)
    {
        std::ofstream Pixels(Destination / "GuidePixels.csv"); Pixels << "view,name,sample,x,y,z,depth\n";
        const char* Views[] = { "Front_Quarter", "Rear_Quarter", "Side", "Top", "Roof_Detail", "Rear_Arch_Detail", "Front_Arch_Detail" };
        const char* Commands[] = { "view front; view orbit 55 23; view persp", "view front; view orbit -55 23; view persp", "view front", "view top" };
        Panel.Expect("review display", After.Execute(Consolidated ?
            "show cages off; show iso off; show edges off; show features on; show shading plastic" :
            "show cages off; show iso off; show edges on; show features on; show shading plastic"));
        for (int Index = 0; Index < 7; ++Index)
        {
            if (Consolidated && Index == 4) Panel.Expect("detail patch layout", After.Execute("show edges on"));
            if (Index < 4)
                Panel.Expect("review camera", Before.Execute(Commands[Index]) && Before.Execute("view fit; view dolly 1.5"));
            else
            {
                const char* Inspection[] = {
                    "line (-.35,-.57,1.14) (1.25,.57,1.14) --name=GuideInspection; select GuideInspection; view top; view fit selected; delete GuideInspection",
                    "line (-1.68,-1.15,.1) (-.48,-1.15,1.0) --name=GuideInspection; select GuideInspection; view front; view fit selected; delete GuideInspection",
                    "line (1.23,-1.15,.1) (2.3,-1.15,.98) --name=GuideInspection; select GuideInspection; view front; view fit selected; delete GuideInspection" };
                Panel.Expect("detail camera", Before.Execute(Inspection[Index - 4]));
            }
            After.Camera() = Before.Camera();
            Panel.Expect("native review image", After.Execute(std::string(Layout ? "render Liger_Layout_" : Consolidated ? "render Liger_Consolidated_" : "render Liger_Guide_") + Views[Index] + " --size=2000x1250"));
            for (const auto& Figure : After.AllFigures())
            {
                if (Figure.Hidden || !Figure.Name.starts_with("Guide_")) continue;
                for (int Sample = 0; Sample <= 64; ++Sample)
                {
                    const Vec3 Point = Figure.Curve.Sample(Sample / 64.0); double X = 0, Y = 0;
                    if (!After.Camera().WorldToPixel(Point, 2000, 1250, X, Y)) continue;
                    if (X < 0 || Y < 0 || X >= 2000 || Y >= 1250) continue;
                    Pixels << Views[Index] << ',' << Figure.Name << ',' << Sample << ',' << X << ',' << Y << ',' << (After.Camera().ProjectionMatrix(1.6) * After.Camera().ViewMatrix()).TransformPoint(Point).Z << ',' << After.Raster().Depth(static_cast<uint32_t>(X), static_cast<uint32_t>(Y)) << '\n';
                }
            }
        }
    }
    if (Layout && Count == 4)
    {
        for (auto* Host : { &Before, &After })
            Panel.Expect("topology proof display", Host->Execute("show cages off; show iso off; show edges on; show features off; show shading plastic"));
        const char* Frames[] = {
            "line (.055,.54,1.1) (.40,.73,1.1) --name=LayoutInspection; select LayoutInspection; view top; view fit selected; delete LayoutInspection",
            "line (1.10,.19,1.07) (1.245,.35,1.07) --name=LayoutInspection; select LayoutInspection; view top; view fit selected; delete LayoutInspection" };
        for (int Index = 0; Index < 2; ++Index)
        {
            Panel.Expect("matched topology camera", Before.Execute(Frames[Index]));
            After.Camera() = Before.Camera();
            Panel.Expect("before topology", Before.Execute("render Liger_Layout_Before_" + std::to_string(Index) + " --size=2000x1250"));
            Panel.Expect("after topology", After.Execute("render Liger_Layout_After_" + std::to_string(Index) + " --size=2000x1250"));
        }
    }
    return Panel.Conclude();
}
