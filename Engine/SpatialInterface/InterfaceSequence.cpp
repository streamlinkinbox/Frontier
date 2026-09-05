//============================================================================================================================================
//                                                     INTERFACESEQUENCE.CPP
//============================================================================================================================================

#include "InterfaceSequence.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                    CATEGORY NAMES
//------------------------------------------------------------------------------------------------------------------------

const char* InterfaceCategoryName(InterfaceCategory Category) noexcept
{
    switch (Category)
    {
        case InterfaceCategory::Surface:     return "Surface";
        case InterfaceCategory::Arc:         return "Arc";
        case InterfaceCategory::TickRing:    return "TickRing";
        case InterfaceCategory::Needle:      return "Needle";
        case InterfaceCategory::SegmentCell: return "SegmentCell";
        case InterfaceCategory::Lamp:        return "Lamp";
        default:                             return "Unknown";
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      SORT KEY
//------------------------------------------------------------------------------------------------------------------------
// Ascending sort ⇒ submission order. Opaque figures carry bit 63 clear so they always precede transparents.
//
//    Depth is folded from its IEEE bits: for non-negative floats the bit pattern is monotone, so comparing the
//    bits compares the values. The opaque group inverts them (front-to-back, so early-Z rejects what follows);
//    the transparent group keeps them descending (back-to-front, so alpha-over composites correctly).

uint64_t ComposeInterfaceSortKey(bool Transparent, uint32_t OrderingRank, float ViewDepth) noexcept
{
    const float    Clamped = ViewDepth > 0.0f ? ViewDepth : 0.0f;
    uint32_t       Bits    = 0u;
    std::memcpy(&Bits, &Clamped, sizeof(Bits));

    // Transparent: far first  → larger depth must sort earlier → invert.
    // Opaque:      near first → smaller depth must sort earlier → keep.
    const uint32_t Ordered = Transparent ? (0xFFFFFFFFu - Bits) : Bits;

    const uint64_t Group = Transparent ? 1ull : 0ull;
    const uint64_t Rank  = static_cast<uint64_t>(OrderingRank & 0x7FFFFFu);

    return (Group << 63) | (Rank << 40) | (static_cast<uint64_t>(Ordered) << 8);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

InterfaceSequence::InterfaceSequence() noexcept
{
    Placements.reserve(64u);
    Instances.reserve(64u);
    SortKeys.reserve(64u);
}

float InterfaceSequence::MeasureViewDepth(const WorldPlacement& Placement) const noexcept
{
    const float DeltaX = Placement.Row[0][3] - View_.EyeX;
    const float DeltaY = Placement.Row[1][3] - View_.EyeY;
    const float DeltaZ = Placement.Row[2][3] - View_.EyeZ;
    return DeltaX * View_.ForwardX + DeltaY * View_.ForwardY + DeltaZ * View_.ForwardZ;
}

const WorldPlacement& InterfaceSequence::QueryPlacement(uint32_t Ordinal) const noexcept
{
    if (Ordinal >= Placements.size()) return AbsentPlacement;
    return Placements[Ordinal];
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       ADVANCE
//------------------------------------------------------------------------------------------------------------------------

void InterfaceSequence::Advance(const InterfaceStructure& Structure, double Elapsed) noexcept
{
    (void)Elapsed;   // ⑤ per-figure stagger delays will advance here; the call sites already pass the step

    using Clock = std::chrono::high_resolution_clock;
    const auto ComposeBegin = Clock::now();

    const uint32_t Total = Structure.QueryCount();
    Metrics = InterfaceMetrics{};
    Metrics.FigureTotal = Total;

    Placements.assign(Total, WorldPlacement{});
    Instances.clear();
    SortKeys.clear();

    // ── ① Compose ────────────────────────────────────────────────────────────────────────────────────────────────
    // Ordinals are assigned in construction order and Attach refuses cycles, but an ancestor may still be
    //    constructed AFTER its descendant. Two linear scans converge for any ordering: the first resolves every
    //    figure whose ancestry is already resolved, the second sweeps up the stragglers. Deeper interleavings
    //    resolve by repeating until nothing changes, bounded by DescentLimit.
    std::vector<uint8_t> Resolved(Total, 0u);
    uint32_t             Sweeps = 0u;
    bool                 Progress = true;

    while (Progress && Sweeps < InterfaceSpecification::DescentLimit)
    {
        Progress = false;
        ++Sweeps;

        for (uint32_t Ordinal = 0u; Ordinal < Total; ++Ordinal)
        {
            if (Resolved[Ordinal]) continue;

            const uint32_t Ancestor = Structure.QueryAncestor(Ordinal);
            if (Ancestor != InterfaceStructure::Detached && !Resolved[Ancestor]) continue;

            const WorldPlacement Local = ComposePlacement(Structure.Query(Ordinal).Placement);
            Placements[Ordinal] = Ancestor == InterfaceStructure::Detached
                                ? Local
                                : CombinePlacement(Placements[Ancestor], Local);
            Resolved[Ordinal] = 1u;
            Progress          = true;
        }
    }

    const auto ComposeEnd = Clock::now();
    Metrics.ComposeMicroseconds = std::chrono::duration<float, std::micro>(ComposeEnd - ComposeBegin).count();

    // ── ② Encode ─────────────────────────────────────────────────────────────────────────────────────────────────
    // A figure is skipped when it is hidden, when any ancestor is hidden, when it has collapsed to nothing, or when
    //    it is fully transparent — a skipped figure costs no instance slot and no fragment.
    const PaletteConfiguration& Palette = Structure.QueryPalette();

    for (uint32_t Ordinal = 0u; Ordinal < Total; ++Ordinal)
    {
        const InterfaceFigure& Figure = Structure.Query(Ordinal);

        bool Hidden = !Figure.Visible || !Resolved[Ordinal];
        for (uint32_t Walk = Structure.QueryAncestor(Ordinal), Steps = 0u;
             !Hidden && Walk != InterfaceStructure::Detached && Steps < InterfaceSpecification::DescentLimit;
             Walk = Structure.QueryAncestor(Walk), ++Steps)
        {
            if (!Structure.Query(Walk).Visible) Hidden = true;
        }

        const float EffectiveOpacity = std::clamp(Figure.Opacity, 0.0f, 1.0f) * Palette.QueryGroupOpacity();
        const bool  Degenerate       = Figure.HalfWidth <= 0.0f || Figure.HalfHeight <= 0.0f;

        if (Hidden || Degenerate || EffectiveOpacity <= 0.0f)
        {
            ++Metrics.SkippedCount;
            continue;
        }

        InterfaceInstanceFigure Slot{};
        InterfaceLayoutCodec::Encode(Figure, Placements[Ordinal], Palette, Slot);

        const bool Transparent = Slot.Opacity < InterfaceSpecification::OpaqueThreshold;
        if (!Transparent) ++Metrics.OpaqueCount;

        const uint64_t Key = ComposeInterfaceSortKey(Transparent, Figure.OrderingRank,
                                                     MeasureViewDepth(Placements[Ordinal]));

        SortKeys.push_back(Key);
        Instances.push_back(Slot);
    }

    // ── ③ Order ──────────────────────────────────────────────────────────────────────────────────────────────────
    const auto SortBegin = Clock::now();

    if (Instances.size() > 1u)
    {
        // Sort an ordinal span by key rather than the keys themselves: equal keys keep construction order (stable),
        //    and the mapping back to instances stays exact for any instance count.
        std::vector<uint32_t> Order(Instances.size());
        for (uint32_t I = 0u; I < Order.size(); ++I) Order[I] = I;

        std::stable_sort(Order.begin(), Order.end(),
                         [this](uint32_t Left, uint32_t Right) { return SortKeys[Left] < SortKeys[Right]; });

        std::vector<InterfaceInstanceFigure> Submission;
        Submission.reserve(Instances.size());
        for (const uint32_t Index : Order) Submission.push_back(Instances[Index]);

        Instances.swap(Submission);
    }

    const auto SortEnd = Clock::now();
    Metrics.SortMicroseconds = std::chrono::duration<float, std::micro>(SortEnd - SortBegin).count();

    Metrics.InstanceCount = static_cast<uint32_t>(Instances.size());
    Metrics.DrawCount     = Instances.empty() ? 0u : 1u;   // the acceptance number: one draw, whatever the count
}

} // namespace Frontier
