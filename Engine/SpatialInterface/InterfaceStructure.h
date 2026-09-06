//============================================================================================================================================
//                                                     INTERFACESTRUCTURE.H
//============================================================================================================================================
// 🧩 The retained figure graph — layer ① of the spatial interface. A host constructs figures once, attaches them into
//    a descent chain, then mutates values (fill fractions, angles, tints, opacity) frame to frame. Nothing is
//    re-tessellated: a needle sweeping to the redline is a single float write on an existing figure.
//
// Retained, not immediate: this is the opposite of PixelSpace. An immediate surface rebuilds its command list every
//    frame because it composites onto the swapchain; a spatial interface persists in the scene, so the graph persists
//    with it and only dirty figures are re-encoded.
//
// Transform: a figure carries a local placement (origin, rotation about the three world axes, uniform scale) relative
//    to its ancestor. InterfaceSequence composes those into world rows. UI planes never shear, so three rows suffice.
//
// Local plane convention: +X right, +Y UP, origin at the figure centre, metres (InterfaceSpecification.h).

#pragma once

#include "InterfaceSpecification.h"
#include "PaletteConfiguration.h"

#include <cstdint>
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                    PLANE ORIGIN
//------------------------------------------------------------------------------------------------------------------------

struct PlaneOrigin
{
    float X = 0.0f;    // [m]
    float Y = 0.0f;    // [m]
    float Z = 0.0f;    // [m]  out of the figure plane — lifts a knob off its bed, a needle off its dial
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  PLANE PLACEMENT
//------------------------------------------------------------------------------------------------------------------------

struct PlanePlacement
{
    PlaneOrigin Origin;                 // [m]   relative to the ancestor's plane
    float       RotationX  = 0.0f;      // [rad] pitch — tilts a screen toward the eye
    float       RotationY  = 0.0f;      // [rad] yaw
    float       RotationZ  = 0.0f;      // [rad] roll — the in-plane spin, the common one
    float       Scale      = 1.0f;      // [-]   uniform
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  INTERFACE FIGURE
//------------------------------------------------------------------------------------------------------------------------
// One drawable. Every category reads the same fields; the meaning of ScalarAlpha / ScalarBeta is per-category and
//    documented in InterfaceSpecification.h. Semantic ranges (rpm, km/h, gear) never appear here — the project
//    normalises before writing (CLAUDE.md §6).

struct InterfaceFigure
{
    InterfaceCategory Category     = InterfaceCategory::Surface;

    PlanePlacement    Placement;                        // [-]   local, relative to the ancestor
    float             HalfWidth    = 0.05f;             // [m]   local half extent along +X
    float             HalfHeight   = 0.05f;             // [m]   local half extent along +Y
    float             CornerRadius = 0.0f;              // [m]   < 0 → the palette token radius

    float             ScalarAlpha  = 0.0f;              // [-]   fill fraction / angle fraction / digit / luminance
    float             ScalarBeta   = 0.0f;              // [-]   thickness / mark count / warn threshold

    PaletteSlot       Palette      = PaletteSlot::Surface;
    uint32_t          TintOverride = 0u;                // [-]   RGBA8; 0 = use the palette slot
    float             Opacity      = 1.0f;              // [-]   ⑤ animatable; multiplied by the group opacity

    // ⑦ Surface response — how the figure behaves as a physical surface, not what it shows. Defaults keep every
    //    existing figure a pure emitter, so adding these changed no pixel until a caller opts in: an LCD-style
    //    panel sets a dark BaseColour with EmissiveWeight 0 on the housing and 1 on the lit elements.
    uint32_t          BaseColour     = 0u;              // [-]   RGBA8 albedo; a = 0 → fall back to the resolved tint
    float             EmissiveWeight = 1.0f;            // [-]   0 = pure albedo (receives room light), 1 = pure emitter

    // ⑥ Clip extent in the figure's OWN local plane, metres. Left wide open in P0; P2 animates it for masked wipes
    //    and intersects ancestor rectangles on the CPU so a batch group emits one tightest rectangle.
    float             ClipMinimumX = -1.0e9f;           // [m]
    float             ClipMinimumY = -1.0e9f;           // [m]
    float             ClipMaximumX =  1.0e9f;           // [m]
    float             ClipMaximumY =  1.0e9f;           // [m]
    float             ClipRadius   =  0.0f;             // [m]

    // ③ Reserved for P2 raycasts: a figure that accepts the pointer reports its plane hit extent. Unread in P0.
    bool              PointerTarget = false;            // [-]
    uint32_t          OrderingRank  = 0u;               // [-]   whole-screen layer ordinal for the sort key (⑤)
    bool              Visible       = true;             // [-]   false → skipped entirely, descendants included
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 INTERFACE STRUCTURE
//------------------------------------------------------------------------------------------------------------------------

class InterfaceStructure
{
public:
    InterfaceStructure() noexcept;

    InterfaceStructure(const InterfaceStructure&)            = delete;
    InterfaceStructure& operator=(const InterfaceStructure&) = delete;

    static constexpr uint32_t Detached = 0xFFFFFFFFu;   // "no ancestor" — a root figure

    // Adds a figure and returns its ordinal. Ordinals are stable for the lifetime of the structure.
    [[nodiscard]] uint32_t Construct(const InterfaceFigure& Figure) noexcept;

    // Places Ordinal beneath Ancestor. An ancestor must already exist and must not be a descendant of Ordinal —
    //    the check is performed here so the per-frame walk never has to guard against a cycle.
    bool                   Attach(uint32_t Ordinal, uint32_t Ancestor) noexcept;

    // Mutation goes through Access so the dirty mark is never forgotten; Query is the read path.
    [[nodiscard]] InterfaceFigure&       Access(uint32_t Ordinal) noexcept;
    [[nodiscard]] const InterfaceFigure& Query (uint32_t Ordinal) const noexcept;

    void                   MarkDirty(uint32_t Ordinal) noexcept;
    void                   ClearDirty() noexcept;
    [[nodiscard]] bool     IsDirty(uint32_t Ordinal) const noexcept;
    [[nodiscard]] uint32_t QueryDirtyCount() const noexcept { return DirtyCount; }

    [[nodiscard]] uint32_t QueryCount()    const noexcept { return static_cast<uint32_t>(Figures.size()); }
    [[nodiscard]] uint32_t QueryAncestor(uint32_t Ordinal) const noexcept;

    [[nodiscard]] PaletteConfiguration&       AccessPalette() noexcept       { return Palette; }
    [[nodiscard]] const PaletteConfiguration& QueryPalette()  const noexcept { return Palette; }

    void                   Reserve(uint32_t Count) noexcept;

    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

private:
    std::vector<InterfaceFigure> Figures;
    std::vector<uint32_t>        Ancestors;
    std::vector<uint8_t>         DirtyMarks;
    PaletteConfiguration         Palette;
    uint32_t                     DirtyCount = 0u;
    InterfaceFigure              Absent{};      // returned for out-of-range ordinals so callers never dereference null
};

template<>
inline uint32_t InterfaceStructure::Convert<uint32_t>() const noexcept
{
    return static_cast<uint32_t>(Figures.size());
}

} // namespace Frontier
