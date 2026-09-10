//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Document/SceneDocument.cpp — Figure store
//============================================================================================================================================

#include "SceneDocument.h"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace Frontier
{

SceneFigure& SceneDocument::AddCurve(std::string Name, NurbsCurve Curve) noexcept
{
    SceneFigure Figure;
    Figure.Identity = NextIdentity++;
    Figure.Classification = FigureClassification::Curve;
    Figure.Name = UniqueName(Name.empty() ? "Curve" : Name);
    Figure.Curve = std::move(Curve);
    Entries.push_back(std::move(Figure));
    return Entries.back();
}

SceneFigure& SceneDocument::Duplicate(const SceneFigure& Original) noexcept
{
    SceneFigure Figure = Original;
    Figure.Identity = NextIdentity++;
    Figure.Selected = false; Figure.SelectedPoles.clear(); Figure.SelectedFaces.clear(); Figure.SelectedEdges.clear();
    std::string Stem = Original.Name; size_t Dot = Stem.rfind('.'); if (Dot != std::string::npos && Dot + 1 < Stem.size() && std::isdigit(uint8_t(Stem[Dot + 1]))) Stem.resize(Dot);
    Figure.Name = UniqueName(Stem);
    Entries.push_back(std::move(Figure));
    return Entries.back();
}

SceneFigure& SceneDocument::AddBody(std::string Name, BrepBody Body) noexcept
{
    SceneFigure Figure;
    Figure.Identity = NextIdentity++;
    Figure.Classification = FigureClassification::Body;
    Figure.Name = UniqueName(Name.empty() ? "Body" : Name);
    Figure.Body = std::move(Body);
    Entries.push_back(std::move(Figure));
    return Entries.back();
}

SceneFigure& SceneDocument::AddSurface(std::string Name, NurbsSurface Surface) noexcept
{
    SceneFigure Figure;
    Figure.Identity = NextIdentity++;
    Figure.Classification = FigureClassification::Surface;
    Figure.Name = UniqueName(Name.empty() ? "Surface" : Name);
    Figure.Surface = std::move(Surface);
    Entries.push_back(std::move(Figure));
    return Entries.back();
}

bool SceneDocument::Remove(uint32_t Identity) noexcept
{
    auto It = std::find_if(Entries.begin(), Entries.end(), [&](const SceneFigure& I) { return I.Identity == Identity; });
    if (It == Entries.end()) return false;
    Entries.erase(It);
    return true;
}

SceneFigure* SceneDocument::Find(uint32_t Identity) noexcept
{
    for (SceneFigure& I : Entries) if (I.Identity == Identity) return &I;
    return nullptr;
}

SceneFigure* SceneDocument::Find(const std::string& Name) noexcept
{
    for (SceneFigure& I : Entries) if (I.Name == Name) return &I;
    return nullptr;
}

Box3 SceneDocument::Bounds(bool SelectedOnly) const noexcept
{
    Box3 B;
    for (const SceneFigure& I : Entries)
        if (!I.Hidden && (!SelectedOnly || I.Selected)) B.Include(I.Bounds());
    return B;
}

std::string SceneDocument::UniqueName(const std::string& Stem) const noexcept
{
    auto Taken = [&](const std::string& N) { return std::any_of(Entries.begin(), Entries.end(), [&](const SceneFigure& I) { return I.Name == N; }); };
    if (!Taken(Stem)) return Stem;
    for (int K = 2;; ++K)
    {
        std::string Candidate = Stem + "." + std::to_string(K);
        if (!Taken(Candidate)) return Candidate;
    }
}

} // namespace Frontier

//------------------------------------------------------------------------------------------------------------------------
//                                                  SKETCH AREAS
//------------------------------------------------------------------------------------------------------------------------
namespace Frontier
{

void SceneDocument::RebuildAreas(const Workplane& Plane) noexcept
{
    // remember the fills of the current cells before they are thrown away
    for (const SketchArea& A : Cells)
    {
        bool Found = false;
        for (FillChoice& M : Fills) if (M.Centroid.Coincident(A.Centroid, 1e-6) && std::fabs(M.Area - A.Cell.Area) < 1e-9) { M.Filled = A.Filled; Found = true; }
        if (!Found) Fills.push_back({ A.Centroid, A.Cell.Area, A.Filled });
    }
    std::vector<uint32_t> Selected; for (const SketchArea& A : Cells) if (A.Selected) Selected.push_back(A.Identity);
    Cells.clear();
    std::vector<NurbsCurve> Curves; std::vector<uint32_t> Owners;
    Frontier::Plane Sheet = Plane.ToPlane();
    for (const SceneFigure& F : Entries)
    {
        if (F.Classification != FigureClassification::Curve || F.Hidden || F.Construction) continue;
        bool InPlane = true;
        for (const Vec4& P : F.Curve.Poles) if (std::fabs(Sheet.SignedDistance(P.Divide())) > ScalarCriteria::MergeTolerance) { InPlane = false; break; }
        if (!InPlane) continue;
        Curves.push_back(F.Curve); Owners.push_back(F.Identity);
    }
    if (Curves.empty()) return;
    std::vector<PlanarCell> Found = ProfileSolver::Cells(Curves, Plane.Normal());
    uint32_t Next = AreaIdentityBase;
    for (PlanarCell& C : Found)
    {
        SketchArea A; A.Identity = Next++; A.Cell = std::move(C); A.Normal = Plane.Normal();
        std::vector<Vec3> Pts; A.Cell.Outer.Tessellate(Pts, nullptr, 1e-3);
        Vec3 Sum; for (const Vec3& P : Pts) Sum = Sum + P; A.Centroid = Sum * (1.0 / double(Pts.size()));
        for (uint32_t S : A.Cell.Origins) A.BoundingIdentities.push_back(Owners[S]);
        for (const FillChoice& M : Fills) if (M.Centroid.Coincident(A.Centroid, 1e-6) && std::fabs(M.Area - A.Cell.Area) < 1e-9) A.Filled = M.Filled;
        for (uint32_t Id : Selected) if (Id == A.Identity) A.Selected = true;
        Cells.push_back(std::move(A));
    }
}

SketchArea* SceneDocument::FindArea(uint32_t Identity) noexcept
{
    for (SketchArea& A : Cells) if (A.Identity == Identity) return &A;
    return nullptr;
}

SketchArea* SceneDocument::AreaAt(Vec3 P) noexcept
{
    SketchArea* Best = nullptr;
    for (SketchArea& A : Cells)
    {
        bool In = ProfileSolver::Winding(A.Cell.Outer, A.Normal, P) != 0;
        for (const NurbsCurve& H : A.Cell.Holes) if (ProfileSolver::Winding(H, A.Normal, P) != 0) In = false;
        if (In && (!Best || A.Cell.Area < Best->Cell.Area)) Best = &A;
    }
    return Best;
}

std::vector<SketchArea*> SceneDocument::AreasOf(uint32_t FigureIdentity) noexcept
{
    std::vector<SketchArea*> Out;
    for (SketchArea& A : Cells) for (uint32_t S : A.BoundingIdentities) if (S == FigureIdentity) { Out.push_back(&A); break; }
    return Out;
}

} // namespace Frontier

//------------------------------------------------------------------------------------------------------------------------
//                                                  DERIVED FIGURES
//------------------------------------------------------------------------------------------------------------------------
namespace Frontier
{

const SketchArea* SceneDocument::FindArea(uint32_t Identity) const noexcept
{
    for (const SketchArea& A : Cells) if (A.Identity == Identity) return &A;
    return nullptr;
}

const SketchArea* SceneDocument::AreaBySignature(Vec3 Centroid) const noexcept
{
    const SketchArea* Best = nullptr; double BestD = 1e-3;
    for (const SketchArea& A : Cells) { double D = A.Centroid.Distance(Centroid); if (D < BestD) { BestD = D; Best = &A; } }
    return Best;
}

std::vector<const SceneFigure*> SceneDocument::DerivedFrom(uint32_t Identity) const noexcept
{
    std::vector<const SceneFigure*> Out;
    for (const SceneFigure& F : Entries)
    {
        if (!F.Recipe.Live()) continue;
        bool Uses = false;
        for (const RecipeInput& In : F.Recipe.Sections) for (uint32_t Id : In.Figures) Uses |= Id == Identity;
        for (uint32_t Id : F.Recipe.Path.Figures) Uses |= Id == Identity;
        if (Uses) Out.push_back(&F);
    }
    return Out;
}

std::vector<std::string> SceneDocument::Regenerate(const Workplane& Plane) noexcept
{
    std::vector<std::string> Changed;
    // Recipes may chain (a loft of an edge of an extrusion): iterate until nothing moves, bounded.
    for (int Round = 0; Round < 4; ++Round)
    {
        bool Any = false;
        for (size_t I = 0; I < Entries.size(); ++I)
        {
            SceneFigure& F = Entries[I];
            if (!F.Recipe.Live()) continue;
            uint64_t Now = F.Recipe.FingerprintInputs(*this, Plane);
            if (Now == F.Recipe.InputFingerprint) continue;
            Deliver<FigureRecipe::Product> P = F.Recipe.Produce(*this, Plane);
            F.Recipe.InputFingerprint = Now;
            if (!P) { F.Recipe.Complaint = P.Denial.Detail; continue; }
            F.Recipe.Complaint.clear();
            if (P.Payload.IsBody) { F.Classification = FigureClassification::Body; F.Body = std::move(P.Payload.Body); F.Surface = NurbsSurface(); }
            else { F.Classification = FigureClassification::Surface; F.Surface = std::move(P.Payload.Sheet); F.Body = BrepBody(); }
            F.SelectedFaces.clear(); F.SelectedEdges.clear(); F.SelectedPoles.clear();
            Changed.push_back(F.Name); Any = true;
        }
        if (!Any) break;
        RebuildAreas(Plane);
    }
    return Changed;
}

} // namespace Frontier
