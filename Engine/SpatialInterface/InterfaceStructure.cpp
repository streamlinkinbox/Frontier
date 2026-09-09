//============================================================================================================================================
//                                                    INTERFACESTRUCTURE.CPP
//============================================================================================================================================

#include "InterfaceStructure.h"

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

InterfaceStructure::InterfaceStructure() noexcept
{
    Figures.reserve(64u);
    Ancestors.reserve(64u);
    DirtyMarks.reserve(64u);
}

void InterfaceStructure::Reserve(uint32_t Count) noexcept
{
    const uint32_t Limit = Count < InterfaceSpecification::FigureLimit ? Count : InterfaceSpecification::FigureLimit;
    Figures.reserve(Limit);
    Ancestors.reserve(Limit);
    DirtyMarks.reserve(Limit);
}

uint32_t InterfaceStructure::Construct(const InterfaceFigure& Figure) noexcept
{
    if (Figures.size() >= InterfaceSpecification::FigureLimit) return Detached;

    const uint32_t Ordinal = static_cast<uint32_t>(Figures.size());
    Figures.push_back(Figure);
    Ancestors.push_back(Detached);
    DirtyMarks.push_back(1u);
    ++DirtyCount;
    return Ordinal;
}

bool InterfaceStructure::Attach(uint32_t Ordinal, uint32_t Ancestor) noexcept
{
    if (Ordinal >= Figures.size())                        return false;
    if (Ancestor != Detached && Ancestor >= Figures.size()) return false;
    if (Ancestor == Ordinal)                              return false;

    // Reject a cycle: walk the prospective ancestry and refuse if Ordinal is already up there. Bounded by
    //    DescentLimit so a pre-existing malformed chain cannot spin here either.
    uint32_t Walk  = Ancestor;
    uint32_t Steps = 0u;
    while (Walk != Detached && Steps < InterfaceSpecification::DescentLimit)
    {
        if (Walk == Ordinal) return false;
        Walk = Ancestors[Walk];
        ++Steps;
    }
    if (Steps >= InterfaceSpecification::DescentLimit) return false;

    Ancestors[Ordinal] = Ancestor;
    MarkDirty(Ordinal);
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       ACCESS
//------------------------------------------------------------------------------------------------------------------------

InterfaceFigure& InterfaceStructure::Access(uint32_t Ordinal) noexcept
{
    if (Ordinal >= Figures.size())
    {
        Absent = InterfaceFigure{};
        Absent.Visible = false;
        return Absent;
    }
    MarkDirty(Ordinal);
    return Figures[Ordinal];
}

const InterfaceFigure& InterfaceStructure::Query(uint32_t Ordinal) const noexcept
{
    if (Ordinal >= Figures.size()) return Absent;
    return Figures[Ordinal];
}

uint32_t InterfaceStructure::QueryAncestor(uint32_t Ordinal) const noexcept
{
    if (Ordinal >= Ancestors.size()) return Detached;
    return Ancestors[Ordinal];
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    DIRTY MARKS
//------------------------------------------------------------------------------------------------------------------------
// A mark means "this figure's encoded slot is stale". P0 re-encodes everything each frame (nine figures — the walk is
//    cheaper than the bookkeeping); the marks exist so P1's partial upload has the information already recorded.

void InterfaceStructure::MarkDirty(uint32_t Ordinal) noexcept
{
    if (Ordinal >= DirtyMarks.size()) return;
    if (DirtyMarks[Ordinal] != 0u)    return;
    DirtyMarks[Ordinal] = 1u;
    ++DirtyCount;
}

void InterfaceStructure::ClearDirty() noexcept
{
    for (uint8_t& Mark : DirtyMarks) Mark = 0u;
    DirtyCount = 0u;
}

bool InterfaceStructure::IsDirty(uint32_t Ordinal) const noexcept
{
    if (Ordinal >= DirtyMarks.size()) return false;
    return DirtyMarks[Ordinal] != 0u;
}

} // namespace Frontier
