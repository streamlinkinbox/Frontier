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
        Mix(H, (I.Construction ? 1 : 0) | (I.Hidden ? 2 : 0) | (I.Selected ? 4 : 0)); Mix(H, I.Matcap); Mix(H, uint64_t(I.Feature));
        Mix(H, Bits(I.Tint[0])); Mix(H, Bits(I.Tint[1])); Mix(H, Bits(I.Tint[2]));
        for (int P : I.SelectedPoles) Mix(H, uint64_t(P) + 7);
        for (int P : I.SelectedFaces) Mix(H, uint64_t(P) + 11);
        for (int P : I.SelectedEdges) Mix(H, uint64_t(P) + 13);
        Mix(H, uint64_t(I.Recipe.Operation)); Mix(H, I.Recipe.Sections.size()); Mix(H, Bits(I.Recipe.Length)); Mix(H, Bits(I.Recipe.Angle)); Mix(H, Bits(I.Recipe.Radius));
        if (I.Recipe.Operation == RecipeOperation::SurfaceOffset)
        {
            Mix(H, I.Recipe.Path.Support);
            for (auto Id : I.Recipe.Path.Figures) Mix(H, Id);
            Mix(H, I.Recipe.OffsetFailureHidden);
            Mix(H, std::hash<std::string>{}(I.Recipe.Complaint));
        }
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

size_t UndoSequence::GeometryBytes(const SceneDocument& Scene) noexcept
{
    size_t Bytes = sizeof(SceneDocument) + Scene.Figures().size() * sizeof(SceneFigure) + Scene.Areas().size() * sizeof(SketchArea);
    auto CurveBytes = [](const NurbsCurve& Curve)
    {
        return Curve.Poles.size() * sizeof(Vec4) + Curve.Knots.size() * sizeof(double);
    };
    auto SurfaceBytes = [](const NurbsSurface& PatchSurface)
    {
        return PatchSurface.Poles.size() * sizeof(Vec4) +
            (PatchSurface.KnotsU.size() + PatchSurface.KnotsV.size()) * sizeof(double);
    };
    for (const SceneFigure& Figure : Scene.Figures())
    {
        Bytes += CurveBytes(Figure.Curve) + SurfaceBytes(Figure.Surface);
        Bytes += Figure.Blueprint.PolylinePoints.size() * sizeof(Vec3);
        const BrepBody& Body = Figure.Body;
        Bytes += Body.Vertices.size() * sizeof(BrepVertex) + Body.Edges.size() * sizeof(BrepEdge) +
            Body.Coedges.size() * sizeof(BrepCoedge) + Body.Loops.size() * sizeof(BrepLoop) + Body.Faces.size() * sizeof(BrepFace);
        for (const BrepEdge& Edge : Body.Edges) Bytes += CurveBytes(Edge.Curve) + Edge.Coedges.size() * sizeof(int);
        for (const BrepFace& Face : Body.Faces) Bytes += SurfaceBytes(Face.Surface) + Face.Loops.size() * sizeof(int);
        for (const BrepLoop& Loop : Body.Loops) Bytes += Loop.Coedges.size() * sizeof(int);
        for (const BrepCoedge& Coedge : Body.Coedges) Bytes += Coedge.Trace.size() * sizeof(Vec2);
    }
    return Bytes;
}

size_t UndoSequence::RetainedGeometryBytes() const noexcept
{
    size_t Bytes = 0;
    for (const Entry& Snapshot : UndoStack) Bytes += Snapshot.GeometryBytes;
    for (const Entry& Snapshot : RedoStack) Bytes += Snapshot.GeometryBytes;
    return Bytes;
}

void UndoSequence::TrimGeometry(bool PreserveUndo) noexcept
{
    size_t Bytes = RetainedGeometryBytes();
    while (Bytes > GeometryLimit && UndoStack.size() + RedoStack.size() > 1)
    {
        auto& Preferred = PreserveUndo ? UndoStack : RedoStack;
        auto& Other = PreserveUndo ? RedoStack : UndoStack;
        auto& Discard = Other.empty() ? Preferred : Other;
        Bytes -= Discard.front().GeometryBytes;
        Discard.pop_front();
    }
}

void UndoSequence::Record(const SceneDocument& Before, std::string Label) noexcept
{
    PendingEntry.GeometryBytes = GeometryBytes(Before);
    PendingEntry.Before = Before;
    PendingEntry.Label = std::move(Label);
    PendingFingerprint = Fingerprint(Before);
    Pending = true;
}

bool UndoSequence::Settle(const SceneDocument& After) noexcept
{
    if (!Pending) return false;
    Pending = false;
    if (Fingerprint(After) == PendingFingerprint) { PendingEntry = {}; return false; }
    UndoStack.push_back(std::move(PendingEntry));
    RedoStack.clear();
    while (UndoStack.size() > Limit) UndoStack.pop_front();
    TrimGeometry(true);
    return true;
}

std::string UndoSequence::Undo(SceneDocument& Current) noexcept
{
    if (UndoStack.empty()) return {};
    Entry E = std::move(UndoStack.back()); UndoStack.pop_back();
    RedoStack.push_back({ E.Label, GeometryBytes(Current), Current });
    Current = std::move(E.Before);
    TrimGeometry(false);
    return RedoStack.back().Label;
}

std::string UndoSequence::Redo(SceneDocument& Current) noexcept
{
    if (RedoStack.empty()) return {};
    Entry E = std::move(RedoStack.back()); RedoStack.pop_back();
    UndoStack.push_back({ E.Label, GeometryBytes(Current), Current });
    Current = std::move(E.Before);
    TrimGeometry(true);
    return UndoStack.back().Label;
}

} // namespace Frontier
