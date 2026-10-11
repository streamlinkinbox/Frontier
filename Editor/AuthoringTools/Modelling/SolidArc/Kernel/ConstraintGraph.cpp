//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Kernel/ConstraintGraph.cpp — Phase 18: persistent constraint graph state
//============================================================================================================================================
// See ConstraintGraph.h. This is a small in-memory graph; the only nontrivial method is AddAnchor, which
//    dedupes by PointRef.
#include "ConstraintGraph.h"
#include <algorithm>

namespace Frontier
{

uint32_t ConstraintGraph::AddConstraint(const Constraint& C, const std::string& Note) noexcept
{
    ConstraintEntry E; E.Id = NextId_++; E.C = C; E.Note = Note;
    Entries.push_back(E);
    return E.Id;
}

void ConstraintGraph::RemoveConstraint(uint32_t Id) noexcept
{
    Entries.erase(std::remove_if(Entries.begin(), Entries.end(), [Id](const ConstraintEntry& E) { return E.Id == Id; }), Entries.end());
}

size_t ConstraintGraph::AddAnchor(const PointRef& Ref, const std::string& Figure, int Slot, int SubIndex, int Component) noexcept
{
    size_t Existing = AnchorIndex(Ref);
    if (Existing != SIZE_MAX) return Existing;
    ConstraintAnchor A; A.Ref = Ref; A.Figure = Figure; A.Slot = Slot; A.SubIndex = SubIndex; A.Component = Component;
    Anchors.push_back(A);
    return Anchors.size() - 1;
}

size_t ConstraintGraph::AnchorIndex(const PointRef& Ref) const noexcept
{
    for (size_t I = 0; I < Anchors.size(); ++I)
    {
        const auto& A = Anchors[I];
        if (A.Ref.Figure == Ref.Figure && A.Ref.Kind == Ref.Kind && A.Ref.Index == Ref.Index) return I;
    }
    return SIZE_MAX;
}

void ConstraintGraph::SetFixed(const PointRef& Ref, bool Fixed) noexcept
{
    for (auto& A : Anchors) if (A.Ref == Ref) A.Fixed = Fixed;
}

} // namespace Frontier
