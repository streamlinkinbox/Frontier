//============================================================================================================================================
//                                                      TYREGENERATORSEQUENCE.H
//============================================================================================================================================
// 📦 Project-Drive's tyre authoring seam — the project-owned exchange that turns the generated tyre into outliner
//    rows and inspector sheets, the counterpart to VehicleInspectorSequence for the vehicle's dynamics.
//
//    The vehicle already owns five scene spans: the body and four wheels. This adds the authoring side of the
//    tyre — the thing that DECIDES what the rubber looks like — as one node per wheel station that expands into
//    its carcass, its tread layer sequence, its sidewall decals and its XPBD lattice.
//
//    Split of surfaces, settled by the HTML prototype in References/TyreEditor.html:
//        • the always-present Inspector gets the quick values — one scalar with a fixed home
//        • Tyre Generator, a separate dockable window, gets the layer sequence — ordered and arbitrarily long
//    The widget each value wants is decided HERE, when the sheet is filled, not by the panel that draws it.
//    A slider is continuous, bounded and found by watching the viewport; a type-in is discrete, standards-defined
//    or topology-changing, because dragging a slider through a repeat count asks for a boolean rebuild per step.

#pragma once

#include "../../../Engine/Generators/Tyre/TreadSpecification.h"
#include "../../../Engine/Generators/Tyre/TreadPatternSpecification.h"
#include "../../../Engine/Editor/EditorInstance.h"

#include <cstdint>

namespace Frontier {
namespace Drive {

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE KEY SPACE
//------------------------------------------------------------------------------------------------------------------------

/// 📦 Which part of the tyre a row addresses. The key survives a move or a rename, exactly as family 6 does.
/// note  Celestial is family 2, camera 1, wind 4, vehicle 6. The tyre takes 7.
enum class TyreSection : uint32_t
{
    Tyre = 1u,      // the generated body: inflation, tread depth, rim bottoming
    Carcass,        // the marked size and the moulded shape
    Pattern,        // the tread layer sequence — opens Tyre Generator
    Layer,          // one groove layer; the layer ordinal rides in the low byte
    Decals,         // sidewall lettering, phase 4
    Lattice,        // the XPBD solver lattice
    Count
};

[[nodiscard]] inline uint64_t TyreInspectorKey(TyreSection Section, uint32_t Ordinal = 0u) noexcept
{
    return (uint64_t(7) << 32) | (uint32_t(Section) << 8) | (Ordinal & 0xFFu);
}

[[nodiscard]] inline bool IsTyreKey(uint64_t Key) noexcept
{
    return (Key >> 32) == 7u;
}

[[nodiscard]] inline TyreSection TyreSectionOf(uint64_t Key) noexcept
{
    return TyreSection((Key >> 8) & 0xFFu);
}

[[nodiscard]] inline uint32_t TyreOrdinalOf(uint64_t Key) noexcept
{
    return uint32_t(Key & 0xFFu);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE AUTHORED TYRE
//------------------------------------------------------------------------------------------------------------------------

/// 📦 What the XPBD carcass is authored by. Inflation pressure is the authored value and the compliances are
///    derived from it, so the slider means something physical instead of being a raw solver number.
/// note  📐 Hoop compliance falls as pressure rises: a harder tyre resists hoop strain. The constant is chosen so
///       240 kPa reproduces the compliance Part 4 shipped, which keeps every existing Drive result unchanged.
/// tag   schema
struct TyreCarcassAuthoring
{
    float    InflationPressure        = 240.0f;    // [kPa] - the authored value
    float    RimStopClearance         = 6.0f;      // [mm]  - travel before the rim stop engages
    float    RimBottomingDampingRatio = 0.25f;     // [-]   - Part 4 settled this; 1.00 pumped energy back in
    uint32_t Iterations               = 8u;        // [cnt] - solver iterations per step
    uint32_t Rings                    = 16u;       // [cnt] - lattice rings
    uint32_t Segments                 = 72u;       // [cnt] - lattice segments
    bool     Advanced                 = false;     // [-]   - reveal the derived compliances as overrides

    [[nodiscard]] float HoopCompliance() const noexcept
    {
        return 1.0e-7f * (240.0f / (InflationPressure > 1.0f ? InflationPressure : 1.0f));
    }

    [[nodiscard]] float SpokeCompliance() const noexcept
    {
        return 4.0f * HoopCompliance();
    }
};

/// 📦 One authored tyre: the carcass the mesh is cut from, the pattern cut into it, and the solver that deforms it.
struct TyreDocument
{
    TreadSpecification          Tread{};
    TreadPatternSpecification   Pattern{};
    TyreCarcassAuthoring        Carcass{};
    uint32_t                    PickedLayer = 0u;   // [idx] the layer Tyre Generator has open
};

/// 📦 Seats the off-road preset the tread mesh gate runs against — 285/70 R17 "Grizzly Magnum".
/// note  The same preset as Exhibits/Workbench/Tyre/TreadMeshProof.cpp, so the editor and the gate author the
///       same tyre and a disagreement between them is a real disagreement.
/// tag   api
void SeatGrizzlyMagnum(TyreDocument& Document) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE SEAM
//------------------------------------------------------------------------------------------------------------------------

class TyreGeneratorSequence
{
public:
    /// 📦 Appends the tyre's rows in preorder under the row the caller has already written for the wheel.
    /// in    Rows      [-]   the roster being filled
    /// in    First     [idx] where to write
    /// in    Capacity  [cnt] the roster's capacity
    /// in    Depth     [-]   the depth of the Tyre node itself; children deepen from it
    /// out   rows written
    /// tag   api, nonallocating
    [[nodiscard]] uint32_t FillRoster(EditorInstance* Rows, uint32_t First, uint32_t Capacity,
                                      uint32_t Depth = 0u) const noexcept;

    /// 📦 Fills the sheet for a tyre row. Returns false when the key is not ours.
    /// note  Group 0 of the Pattern sheet is the layer sequence, which is what Tyre Generator reads; the quick strip
    ///       never draws it, because an arbitrarily long ordered list does not belong in a strip.
    /// tag   api, nonallocating
    [[nodiscard]] bool BuildSheet(uint64_t Key, EditorSheet* Sheet) const noexcept;

    /// 📦 Writes an edited sheet back into the document. Returns true when something actually changed.
    /// tag   api, nonallocating
    [[nodiscard]] bool ApplySheet(uint64_t Key, const EditorSheet& Sheet) noexcept;

    TyreDocument Document{};
};

}   // namespace Drive
}   // namespace Frontier
