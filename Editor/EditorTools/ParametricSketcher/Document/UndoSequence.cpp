//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Document/UndoSequence.cpp — Snapshot record
//============================================================================================================================================
#include "UndoSequence.h"
#include <cstring>

namespace Frontier
{

namespace
{
    inline void Mix(uint64_t& H, uint64_t V) noexcept { H ^= V + 0x9e3779b97f4a7c15ull + (H << 6) + (H >> 2); }
    inline uint64_t Bits(double D) noexcept { uint64_t U; std::memcpy(&U, &D, sizeof U); return U; }
}

uint64_t UndoSequence::Fingerprint(const SceneDocument& Scene) noexcept
{
    uint64_t H = 1469598103934665603ull;
    Mix(H, Scene.Figures().size());
    for (const SketchArea& A : Scene.Areas()) { Mix(H, A.Identity); Mix(H, (A.Filled ? 1 : 0) | (A.Selected ? 2 : 0)); Mix(H, Bits(A.Cell.Area)); }
    for (const SceneFigure& I : Scene.Figures())
    {
        Mix(H, I.Identity); Mix(H, uint64_t(I.Classification)); Mix(H, std::hash<std::string>{}(I.Name));
        Mix(H, (I.Construction ? 1 : 0) | (I.Hidden ? 2 : 0) | (I.Selected ? 4 : 0)); Mix(H, I.Matcap);
        Mix(H, Bits(I.Tint[0])); Mix(H, Bits(I.Tint[1])); Mix(H, Bits(I.Tint[2]));
        for (int P : I.SelectedPoles) Mix(H, uint64_t(P) + 7);
        for (int P : I.SelectedFaces) Mix(H, uint64_t(P) + 11);
        for (int P : I.SelectedEdges) Mix(H, uint64_t(P) + 13);
        Mix(H, uint64_t(I.Recipe.Operation)); Mix(H, I.Recipe.Sections.size()); Mix(H, Bits(I.Recipe.Length)); Mix(H, Bits(I.Recipe.Angle)); Mix(H, Bits(I.Recipe.Radius));
        if (I.Classification == FigureClassification::Curve)
        {
            Mix(H, I.Curve.Degree); for (const Vec4& P : I.Curve.Poles) { Mix(H, Bits(P.X)); Mix(H, Bits(P.Y)); Mix(H, Bits(P.Z)); Mix(H, Bits(P.W)); }
            for (double K : I.Curve.Knots) Mix(H, Bits(K));
        }
        else if (I.Classification == FigureClassification::Body)
        {
            Mix(H, I.Body.Faces.size()); Mix(H, I.Body.Edges.size());
            for (const BrepVertex& V : I.Body.Vertices) { Mix(H, Bits(V.Point.X)); Mix(H, Bits(V.Point.Y)); Mix(H, Bits(V.Point.Z)); }
            for (const BrepFace& F : I.Body.Faces) { Mix(H, F.Reversed); for (const Vec4& P : F.Surface.Poles) { Mix(H, Bits(P.X)); Mix(H, Bits(P.Y)); Mix(H, Bits(P.Z)); } }
        }
        else
        {
            Mix(H, I.Surface.DegreeU); Mix(H, I.Surface.DegreeV); Mix(H, I.Surface.CountU);
            for (const Vec4& P : I.Surface.Poles) { Mix(H, Bits(P.X)); Mix(H, Bits(P.Y)); Mix(H, Bits(P.Z)); Mix(H, Bits(P.W)); }
            for (double K : I.Surface.KnotsU) Mix(H, Bits(K));
            for (double K : I.Surface.KnotsV) Mix(H, Bits(K));
        }
    }
    return H;
}

void UndoSequence::Record(const SceneDocument& Before, std::string Label) noexcept
{
    PendingEntry.Before = Before;
    PendingEntry.Label = std::move(Label);
    PendingFingerprint = Fingerprint(Before);
    Pending = true;
}

bool UndoSequence::Settle(const SceneDocument& After) noexcept
{
    if (!Pending) return false;
    Pending = false;
    if (Fingerprint(After) == PendingFingerprint) return false;
    UndoStack.push_back(std::move(PendingEntry));
    RedoStack.clear();
    while (UndoStack.size() > Limit) UndoStack.pop_front();
    return true;
}

std::string UndoSequence::Undo(SceneDocument& Current) noexcept
{
    if (UndoStack.empty()) return {};
    Entry E = std::move(UndoStack.back()); UndoStack.pop_back();
    RedoStack.push_back({ E.Label, Current });
    Current = std::move(E.Before);
    return RedoStack.back().Label;
}

std::string UndoSequence::Redo(SceneDocument& Current) noexcept
{
    if (RedoStack.empty()) return {};
    Entry E = std::move(RedoStack.back()); RedoStack.pop_back();
    UndoStack.push_back({ E.Label, Current });
    Current = std::move(E.Before);
    return UndoStack.back().Label;
}

} // namespace Frontier
