//============================================================================================================================================
//                                                   INTERFACETRIALSEQUENCE.H
//============================================================================================================================================
// 🧩 Project-Zero's trial interface — the P0 spike, and the project half of the engine/project seam (CLAUDE.md §6).
//    The engine draws rounded rectangles, arcs, needles and segment cells; THIS file decides that six of them make a
//    small control panel, where they sit, and what values they show. Nothing here is reusable and nothing here is
//    meant to be: the real product builds its own composition against the same engine.
//
// The trial deliberately stays small — a housing, two buttons, a toggle, a progress bar, one arc meter with a spring
//    needle, and a two-digit readout. Enough to prove the chain end to end (retained graph → one draw → springs),
//    not a cockpit. Gauges proper, glyph text, input and transitions arrive in P1–P5.
//
// Layout is the VERB here: ConstructTrialLayout places the parts, exactly as ConstructControlLayout does in the 2D
//    overlay. It is not the name of a drawable.

#pragma once

#include "../../../Engine/SpatialInterface/InterfaceStructure.h"
#include "../../../Engine/SpatialInterface/InterfacePointerProjection.h"

#include <cstdint>

namespace Frontier {

class MotionIntegrator;

namespace ProjectZero {

//------------------------------------------------------------------------------------------------------------------------
//                                                 INTERFACE TRIAL SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

class InterfaceTrialSequence
{
public:
    InterfaceTrialSequence() noexcept = default;

    // Builds the figures and registers the spring channels. Call once, after the structure exists.
    void Construct(InterfaceStructure& Structure, MotionIntegrator& Motion) noexcept;

    // Advances the demonstration. RunLoop true → the scripted 6 s cycle drives every value; false → values hold at
    //    whatever was last assigned, which is how the proof measures a clean step response.
    void AdvanceTrial(InterfaceStructure& Structure, MotionIntegrator& Motion, double Elapsed, bool RunLoop) noexcept;

    // Direct value entry [0..1] — the project's own semantics live on this side of the seam, so a real cluster would
    //    convert rpm or km/h to a fraction here before the engine ever sees it.
    void AssignSweepTarget   (float Fraction) noexcept;
    void AssignFillTarget    (float Fraction) noexcept;
    void AssignToggleEngaged (bool  Engaged)  noexcept;

    [[nodiscard]] double QuerySweepValue()  const noexcept;
    [[nodiscard]] double QueryFillValue()   const noexcept;
    [[nodiscard]] bool   IsSweepSettled()   const noexcept;

    [[nodiscard]] uint32_t QueryFigureCount() const noexcept { return FigureCount; }
    [[nodiscard]] double   QueryLoopSeconds() const noexcept { return LoopSeconds; }
    [[nodiscard]] double   QueryLoopTime()    const noexcept { return LoopTime; }

    // World placement of the whole panel — the Showroom decides where it hangs.
    void AssignPanelPlacement(const PlanePlacement& Placement) noexcept { PanelPlacement = Placement; }

    //--------------------------------------------------------------------------------------------------------------------
    // P2 — interaction. The MEANING of a hit lives here, on the project side of the seam.
    //--------------------------------------------------------------------------------------------------------------------
    // InterfacePointerProjection answers "which figure, and where on it". It cannot answer "so the toggle flips",
    //    because the engine does not know one rounded rectangle is a toggle and another is a bar. This turns that
    //    geometric answer into the demonstration's own semantics.
    //
    //    Contact.Valid false is a legitimate state, not an error — the pointer is simply off the panel, and any
    //    hover highlight must clear.
    //
    //    Pressed is the edge, not the level: pass true only on the frame the button goes down. Holding is handled
    //    by the caller so this stays free of input history.
    void ApplyPointer(InterfaceStructure& Structure, const PointerContact& Contact, bool Pressed) noexcept;

    // Ordinal currently under the pointer, or Detached. Read for diagnostics; the highlight itself is applied by
    //    ApplyPointer so the tint and the reported state cannot disagree.
    [[nodiscard]] uint32_t QueryHoveredOrdinal() const noexcept { return HoveredOrdinal; }

    // True while the demonstration is being driven by the pointer rather than the scripted loop. The loop yields
    //    on first contact so a user's input is never fought by the animation, and never resumes on its own —
    //    a control that starts moving by itself after a few seconds reads as a bug.
    [[nodiscard]] bool IsPointerDriven() const noexcept { return PointerDriven; }

private:
    void ConstructTrialLayout(InterfaceStructure& Structure) noexcept;

    static constexpr double LoopSeconds = 6.0;   // [s] one full demonstration cycle

    // Figure ordinals, resolved at construction.
    uint32_t Housing      = InterfaceStructure::Detached;
    uint32_t ButtonLeft   = InterfaceStructure::Detached;
    uint32_t ButtonRight  = InterfaceStructure::Detached;
    uint32_t ToggleBed    = InterfaceStructure::Detached;
    uint32_t ToggleKnob   = InterfaceStructure::Detached;
    uint32_t BarTrough    = InterfaceStructure::Detached;
    uint32_t BarFill      = InterfaceStructure::Detached;
    uint32_t MeterRing    = InterfaceStructure::Detached;
    uint32_t MeterTicks   = InterfaceStructure::Detached;
    uint32_t MeterNeedle  = InterfaceStructure::Detached;
    uint32_t ReadoutTens  = InterfaceStructure::Detached;
    uint32_t ReadoutUnits = InterfaceStructure::Detached;
    uint32_t LampOrdinal  = InterfaceStructure::Detached;

    // Spring channels (MotionIntegrator ordinals).
    uint32_t SweepChannel   = 0u;    // needle sweep fraction — underdamped, overshoots like a stepper motor
    uint32_t FillChannel    = 0u;    // progress bar — critically damped, no overshoot
    uint32_t ToggleChannel  = 0u;    // knob travel
    uint32_t ButtonChannel  = 0u;    // press depth of the left button
    bool     ChannelsReady  = false;

    PlanePlacement PanelPlacement;

    uint32_t HoveredOrdinal = InterfaceStructure::Detached;   // [-] figure under the pointer, for the highlight
    bool     PointerDriven  = false;                          // [-] true once the user has touched the panel
    double         LoopTime      = 0.0;
    uint32_t       FigureCount   = 0u;
    bool           ToggleEngaged = false;

    // Direct entry, consumed on the next Advance when the scripted loop is not running.
    double PendingSweep  = 0.0;
    double PendingFill   = 0.0;
    bool   SweepPending  = false;
    bool   FillPending   = false;
    bool   TogglePending = false;

    // Last integrated values, so a caller can measure the response without reaching into the integrator.
    double LastSweep     = 0.0;
    double LastFill      = 0.0;
    bool   SweepSettled  = true;
};

} // namespace ProjectZero
} // namespace Frontier
