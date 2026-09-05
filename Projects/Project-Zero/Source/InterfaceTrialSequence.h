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
