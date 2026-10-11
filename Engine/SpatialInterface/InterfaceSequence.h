//============================================================================================================================================
//                                                      INTERFACESEQUENCE.H
//============================================================================================================================================
// 🧩 The per-frame walk — layer ② (CPU half) and layer ⑦. Deterministic and ordered:
//
//    ① compose  : local placements → world placements, ancestor-first in one linear scan (no recursion)
//    ② encode   : each visible figure → its 96-byte instance slot
//    ③ order    : one ascending sort over packed keys — opaque group first (front-to-back), transparents
//                 back-to-front by view depth, whole screens sorting as single layers via OrderingRank
//
// The result is a flat span of instance slots in submission order. InterfaceExchange uploads that span and issues
//    exactly one vkCmdDraw(4, InstanceCount) — the draw count is independent of the figure count, which is the
//    entire reason this architecture does not bog down the way per-figure render targets do.
//
// Sorting a few hundred keys costs microseconds on the CPU and buys correct transparency without a depth pre-pass,
//    a stencil buffer, or order-independent blending machinery.

#pragma once

#include "InterfaceLayoutCodec.h"
#include "InterfaceStructure.h"

#include <cstdint>
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                   VIEW CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------
// Only what the ordering needs: the eye and the forward axis, to measure view depth per figure. The full
//    world → clip transform lives in the raster's own constants, not here.

struct InterfaceViewConfiguration
{
    float EyeX = 0.0f, EyeY = 0.0f, EyeZ = 0.0f;             // [m]
    float ForwardX = 0.0f, ForwardY = 1.0f, ForwardZ = 0.0f; // [-]  unit, world +Y forward by default (Z-up engine)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       METRICS
//------------------------------------------------------------------------------------------------------------------------

struct InterfaceMetrics
{
    uint32_t FigureTotal    = 0u;    // [cnt] figures in the structure
    uint32_t InstanceCount  = 0u;    // [cnt] figures actually emitted this frame
    uint32_t OpaqueCount    = 0u;    // [cnt]
    uint32_t SkippedCount   = 0u;    // [cnt] invisible, fully transparent, or degenerate
    uint32_t DrawCount      = 0u;    // [cnt] always 1 while any instance is emitted — the acceptance number
    float    ComposeMicroseconds = 0.0f;
    float    SortMicroseconds    = 0.0f;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  INTERFACE SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

class InterfaceSequence
{
public:
    InterfaceSequence() noexcept;

    InterfaceSequence(const InterfaceSequence&)            = delete;
    InterfaceSequence& operator=(const InterfaceSequence&) = delete;

    void AssignView(const InterfaceViewConfiguration& View) noexcept { View_ = View; }
    [[nodiscard]] const InterfaceViewConfiguration& QueryView() const noexcept { return View_; }

    // Walks the structure and rebuilds the instance span. Elapsed is accepted so a later phase can advance
    //    per-figure stagger delays here (⑤) without changing any call site; P0 ignores it.
    void Advance(const InterfaceStructure& Structure, double Elapsed = 0.0) noexcept;

    [[nodiscard]] const InterfaceInstanceFigure* QueryInstances() const noexcept
    {
        return Instances.empty() ? nullptr : Instances.data();
    }
    [[nodiscard]] uint32_t QueryInstanceCount() const noexcept { return static_cast<uint32_t>(Instances.size()); }
    [[nodiscard]] uint64_t QueryInstanceByteCount() const noexcept
    {
        return static_cast<uint64_t>(Instances.size()) * sizeof(InterfaceInstanceFigure);
    }

    // World placement of a figure after the last Advance — P2's raycasts read this to build hit planes.
    [[nodiscard]] const WorldPlacement& QueryPlacement(uint32_t Ordinal) const noexcept;

    [[nodiscard]] const InterfaceMetrics& QueryMetrics() const noexcept { return Metrics; }

    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

private:
    [[nodiscard]] float MeasureViewDepth(const WorldPlacement& Placement) const noexcept;

    InterfaceViewConfiguration           View_;
    std::vector<WorldPlacement>          Placements;    // indexed by figure ordinal
    std::vector<InterfaceInstanceFigure> Instances;     // submission order
    std::vector<uint64_t>                SortKeys;      // key in the high bits, source ordinal in the low 16
    InterfaceMetrics                     Metrics;
    WorldPlacement                       AbsentPlacement{};
};

template<>
inline uint32_t InterfaceSequence::Convert<uint32_t>() const noexcept
{
    return static_cast<uint32_t>(Instances.size());
}

} // namespace Frontier
