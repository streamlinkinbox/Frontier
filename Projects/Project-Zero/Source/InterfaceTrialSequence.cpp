//============================================================================================================================================
//                                                  INTERFACETRIALSEQUENCE.CPP
//============================================================================================================================================

#include "InterfaceTrialSequence.h"

#include "../../../Engine/DisplayPresentation/MotionIntegrator.h"

#include <algorithm>
#include <cmath>

namespace Frontier {
namespace ProjectZero {

namespace {

// Panel geometry, in metres on the panel's own plane. A 0.36 × 0.22 m face is roughly a large tablet — legible from
//    a metre away, which is the distance the Showroom camera stands at.
constexpr float PanelHalfWidth  = 0.180f;
constexpr float PanelHalfHeight = 0.110f;

constexpr float MeterRadius     = 0.062f;
constexpr float MeterCentreX    = -0.098f;
constexpr float MeterCentreY    =  0.000f;

constexpr float ButtonHalfWidth = 0.038f;
constexpr float ButtonHalfHeight = 0.017f;

// The needle is asked for the full sweep in one step by the proof; these constants give it a visible overshoot that
//    settles inside 0.6 s — the same envelope the Control Centre shade uses, tuned a little livelier.
constexpr double SweepStiffness = 260.0;   // [1/s²]
constexpr double SweepDamping   =  22.0;   // [1/s]   ζ ≈ 0.68 → ~7 % overshoot
constexpr double FillStiffness  = 220.0;
constexpr double FillDamping    =  30.0;   // ζ ≈ 1.01 → critically damped, no overshoot
constexpr double KnobStiffness  = 300.0;
constexpr double KnobDamping    =  26.0;
constexpr double PressStiffness = 420.0;
constexpr double PressDamping   =  30.0;

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

void InterfaceTrialSequence::Construct(InterfaceStructure& Structure, MotionIntegrator& Motion) noexcept
{
    // Default placement: the panel stands upright in the world XZ plane, its face looking along −Y at the camera.
    //    A quarter turn about X carries the local +Y (panel up) onto world +Z (world up); the Showroom overrides this
    //    with its own placement before Construct when it wants the panel somewhere else.
    if (PanelPlacement.RotationX == 0.0f && PanelPlacement.RotationY == 0.0f && PanelPlacement.RotationZ == 0.0f)
        PanelPlacement.RotationX = 1.57079633f;   // [rad] π/2

    ConstructTrialLayout(Structure);

    SweepChannel  = Motion.Register(0.0);
    FillChannel   = Motion.Register(0.0);
    ToggleChannel = Motion.Register(0.0);
    ButtonChannel = Motion.Register(0.0);

    Motion.Spring(SweepChannel).Stiffness  = SweepStiffness;
    Motion.Spring(SweepChannel).Damping    = SweepDamping;
    Motion.Spring(FillChannel).Stiffness   = FillStiffness;
    Motion.Spring(FillChannel).Damping     = FillDamping;
    Motion.Spring(ToggleChannel).Stiffness = KnobStiffness;
    Motion.Spring(ToggleChannel).Damping   = KnobDamping;
    Motion.Spring(ButtonChannel).Stiffness = PressStiffness;
    Motion.Spring(ButtonChannel).Damping   = PressDamping;

    ChannelsReady = true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  CONSTRUCT TRIAL LAYOUT
//------------------------------------------------------------------------------------------------------------------------
// Reads top-down as a layout should: place the housing, then everything on it. Descendant placements are relative to
//    the housing, so moving the panel moves the whole interface — that is the entire point of the retained graph.

void InterfaceTrialSequence::ConstructTrialLayout(InterfaceStructure& Structure) noexcept
{
    Structure.Reserve(16u);

    // ── Housing ──────────────────────────────────────────────────────────────────────────────────────────────────
    {
        InterfaceFigure Figure;
        Figure.Category     = InterfaceCategory::Surface;
        Figure.Placement    = PanelPlacement;
        Figure.HalfWidth    = PanelHalfWidth;
        Figure.HalfHeight   = PanelHalfHeight;
        Figure.CornerRadius = 0.016f;
        Figure.Palette      = PaletteSlot::Housing;
        Figure.OrderingRank = 0u;
        Housing = Structure.Construct(Figure);
    }

    // Face inset — the lighter card the controls sit on, lifted 1 mm off the housing so it reads as a layer.
    uint32_t Face = InterfaceStructure::Detached;
    {
        InterfaceFigure Figure;
        Figure.Category     = InterfaceCategory::Surface;
        Figure.Placement.Origin = PlaneOrigin{ 0.0f, 0.0f, 0.0010f };
        Figure.HalfWidth    = PanelHalfWidth  - 0.008f;
        Figure.HalfHeight   = PanelHalfHeight - 0.008f;
        Figure.CornerRadius = 0.012f;
        Figure.Palette      = PaletteSlot::Surface;
        Figure.OrderingRank = 1u;
        Face = Structure.Construct(Figure);
        (void)Structure.Attach(Face, Housing);
    }

    // ── Arc meter: ring, ticks, needle ───────────────────────────────────────────────────────────────────────────
    {
        InterfaceFigure Figure;
        Figure.Category     = InterfaceCategory::Arc;
        Figure.Placement.Origin = PlaneOrigin{ MeterCentreX, MeterCentreY, 0.0020f };
        Figure.HalfWidth    = MeterRadius;
        Figure.HalfHeight   = MeterRadius;
        Figure.ScalarAlpha  = 0.0f;             // fill fraction, driven each frame
        Figure.ScalarBeta   = 0.0075f;          // track thickness [m]
        Figure.Palette      = PaletteSlot::Accent;
        Figure.OrderingRank = 2u;
        MeterRing = Structure.Construct(Figure);
        (void)Structure.Attach(MeterRing, Face);
    }
    {
        InterfaceFigure Figure;
        Figure.Category     = InterfaceCategory::TickRing;
        Figure.Placement.Origin = PlaneOrigin{ MeterCentreX, MeterCentreY, 0.0018f };
        Figure.HalfWidth    = MeterRadius - 0.010f;
        Figure.HalfHeight   = MeterRadius - 0.010f;
        Figure.ScalarBeta   = 11.0f;            // mark count
        Figure.Palette      = PaletteSlot::MarkingMute;
        Figure.OrderingRank = 3u;
        MeterTicks = Structure.Construct(Figure);
        (void)Structure.Attach(MeterTicks, Face);
    }
    {
        InterfaceFigure Figure;
        Figure.Category     = InterfaceCategory::Needle;
        Figure.Placement.Origin = PlaneOrigin{ MeterCentreX, MeterCentreY, 0.0030f };
        Figure.HalfWidth    = MeterRadius - 0.014f;
        Figure.HalfHeight   = MeterRadius - 0.014f;
        Figure.ScalarAlpha  = 0.0f;             // angle fraction, spring-driven
        Figure.ScalarBeta   = 0.0055f;          // hub radius [m]
        Figure.Palette      = PaletteSlot::Marking;
        Figure.OrderingRank = 4u;
        MeterNeedle = Structure.Construct(Figure);
        (void)Structure.Attach(MeterNeedle, Face);
    }

    // ── Two buttons ──────────────────────────────────────────────────────────────────────────────────────────────
    {
        InterfaceFigure Figure;
        Figure.Category      = InterfaceCategory::Surface;
        Figure.Placement.Origin = PlaneOrigin{ 0.052f, 0.062f, 0.0020f };
        Figure.HalfWidth     = ButtonHalfWidth;
        Figure.HalfHeight    = ButtonHalfHeight;
        Figure.CornerRadius  = 0.008f;
        Figure.Palette       = PaletteSlot::SurfaceSunk;
        Figure.PointerTarget = true;            // ③ reserved for P2 — unread today
        Figure.OrderingRank  = 5u;
        ButtonLeft = Structure.Construct(Figure);
        (void)Structure.Attach(ButtonLeft, Face);
    }
    {
        InterfaceFigure Figure;
        Figure.Category      = InterfaceCategory::Surface;
        Figure.Placement.Origin = PlaneOrigin{ 0.138f, 0.062f, 0.0020f };
        Figure.HalfWidth     = ButtonHalfWidth;
        Figure.HalfHeight    = ButtonHalfHeight;
        Figure.CornerRadius  = 0.008f;
        Figure.Palette       = PaletteSlot::SurfaceSunk;
        Figure.PointerTarget = true;
        Figure.OrderingRank  = 6u;
        ButtonRight = Structure.Construct(Figure);
        (void)Structure.Attach(ButtonRight, Face);
    }

    // ── Toggle: bed + sliding knob ───────────────────────────────────────────────────────────────────────────────
    {
        InterfaceFigure Figure;
        Figure.Category      = InterfaceCategory::Surface;
        Figure.Placement.Origin = PlaneOrigin{ 0.138f, 0.008f, 0.0020f };
        Figure.HalfWidth     = 0.024f;
        Figure.HalfHeight    = 0.012f;
        Figure.CornerRadius  = 0.012f;
        Figure.Palette       = PaletteSlot::SurfaceSunk;
        Figure.PointerTarget = true;
        Figure.OrderingRank  = 7u;
        ToggleBed = Structure.Construct(Figure);
        (void)Structure.Attach(ToggleBed, Face);
    }
    {
        InterfaceFigure Figure;
        Figure.Category     = InterfaceCategory::Lamp;
        Figure.Placement.Origin = PlaneOrigin{ -0.012f, 0.0f, 0.0012f };   // relative to the bed
        Figure.HalfWidth    = 0.0088f;
        Figure.HalfHeight   = 0.0088f;
        Figure.ScalarAlpha  = 1.0f;
        Figure.Palette      = PaletteSlot::MarkingMute;
        Figure.OrderingRank = 8u;
        ToggleKnob = Structure.Construct(Figure);
        (void)Structure.Attach(ToggleKnob, ToggleBed);
    }

    // ── Progress bar: trough + fill ──────────────────────────────────────────────────────────────────────────────
    {
        InterfaceFigure Figure;
        Figure.Category     = InterfaceCategory::Surface;
        Figure.Placement.Origin = PlaneOrigin{ 0.095f, -0.042f, 0.0020f };
        Figure.HalfWidth    = 0.081f;
        Figure.HalfHeight   = 0.0075f;
        Figure.CornerRadius = 0.0075f;
        Figure.Palette      = PaletteSlot::SurfaceSunk;
        Figure.OrderingRank = 9u;
        // P2: the trough is the one CONTINUOUS control — dragging along it sets a value, where the buttons and
        //    the toggle are discrete. That makes it the natural thing to bind a real quantity to.
        Figure.PointerTarget = true;
        BarTrough = Structure.Construct(Figure);
        (void)Structure.Attach(BarTrough, Face);
    }
    {
        // The fill is a descendant of the trough: its width and origin are rewritten each frame so it grows from the
        //    left edge. In P2 this becomes a fixed-width figure under an animated clip rectangle instead (⑥).
        InterfaceFigure Figure;
        Figure.Category     = InterfaceCategory::Surface;
        Figure.Placement.Origin = PlaneOrigin{ -0.081f, 0.0f, 0.0010f };
        Figure.HalfWidth    = 0.0005f;
        Figure.HalfHeight   = 0.0075f;
        Figure.CornerRadius = 0.0075f;
        Figure.Palette      = PaletteSlot::Accent;
        Figure.OrderingRank = 10u;
        BarFill = Structure.Construct(Figure);
        (void)Structure.Attach(BarFill, BarTrough);
    }

    // ── Two-digit readout ────────────────────────────────────────────────────────────────────────────────────────
    {
        InterfaceFigure Figure;
        Figure.Category     = InterfaceCategory::SegmentCell;
        Figure.Placement.Origin = PlaneOrigin{ 0.040f, -0.008f, 0.0020f };
        Figure.HalfWidth    = 0.0105f;
        Figure.HalfHeight   = 0.0175f;
        Figure.ScalarAlpha  = 0.0f;             // digit
        Figure.ScalarBeta   = 0.0034f;          // bar thickness [m]
        Figure.Palette      = PaletteSlot::Marking;
        Figure.OrderingRank = 11u;
        ReadoutTens = Structure.Construct(Figure);
        (void)Structure.Attach(ReadoutTens, Face);
    }
    {
        InterfaceFigure Figure;
        Figure.Category     = InterfaceCategory::SegmentCell;
        Figure.Placement.Origin = PlaneOrigin{ 0.070f, -0.008f, 0.0020f };
        Figure.HalfWidth    = 0.0105f;
        Figure.HalfHeight   = 0.0175f;
        Figure.ScalarAlpha  = 0.0f;
        Figure.ScalarBeta   = 0.0034f;
        Figure.Palette      = PaletteSlot::Marking;
        Figure.OrderingRank = 12u;
        ReadoutUnits = Structure.Construct(Figure);
        (void)Structure.Attach(ReadoutUnits, Face);
    }

    // ── Telltale lamp ────────────────────────────────────────────────────────────────────────────────────────────
    {
        InterfaceFigure Figure;
        Figure.Category     = InterfaceCategory::Lamp;
        Figure.Placement.Origin = PlaneOrigin{ 0.150f, -0.075f, 0.0020f };
        Figure.HalfWidth    = 0.0095f;
        Figure.HalfHeight   = 0.0095f;
        Figure.ScalarAlpha  = 0.0f;
        Figure.Palette      = PaletteSlot::Caution;
        Figure.OrderingRank = 13u;
        LampOrdinal = Structure.Construct(Figure);
        (void)Structure.Attach(LampOrdinal, Face);
    }

    FigureCount = Structure.QueryCount();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     VALUE ENTRY
//------------------------------------------------------------------------------------------------------------------------

void InterfaceTrialSequence::AssignSweepTarget(float Fraction) noexcept
{
    PendingSweep = std::clamp(Fraction, 0.0f, 1.0f);
    SweepPending = true;
}

void InterfaceTrialSequence::AssignFillTarget(float Fraction) noexcept
{
    PendingFill = std::clamp(Fraction, 0.0f, 1.0f);
    FillPending = true;
}

void InterfaceTrialSequence::AssignToggleEngaged(bool Engaged) noexcept
{
    ToggleEngaged = Engaged;
    TogglePending = true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 POINTER INTERACTION (P2)
//------------------------------------------------------------------------------------------------------------------------

void InterfaceTrialSequence::ApplyPointer(InterfaceStructure& Structure, const PointerContact& Contact, bool Pressed) noexcept
{
    // ── Hover highlight ──────────────────────────────────────────────────────────────────────────────────────────
    // Clearing the previous highlight before setting the new one means a pointer that leaves the panel entirely
    //    still clears it, which a "set on hit" implementation silently gets wrong.
    const uint32_t Previous = HoveredOrdinal;
    HoveredOrdinal = Contact.Valid ? Contact.Ordinal : InterfaceStructure::Detached;

    if (Previous != HoveredOrdinal)
    {
        const auto ApplyHighlight = [&](uint32_t Ordinal, bool Lit)
        {
            if (Ordinal == InterfaceStructure::Detached || Ordinal >= Structure.QueryCount()) return;
            InterfaceFigure& Figure = Structure.Access(Ordinal);
            // Opacity is the honest channel for a hover cue here: it is already animatable and already in the GPU
            //    slot, so a highlight costs nothing new. A tint override would fight the palette.
            Figure.Opacity = Lit ? 1.0f : 0.86f;
            Structure.MarkDirty(Ordinal);
        };
        ApplyHighlight(Previous, false);
        ApplyHighlight(HoveredOrdinal, true);
    }

    if (!Contact.Valid || !Pressed) return;

    // ── Press semantics ──────────────────────────────────────────────────────────────────────────────────────────
    // Once the user touches the panel the scripted loop stands down for good. It does not resume on a timer: a
    //    control that starts moving by itself a few seconds after you let go reads as a fault, not a feature.
    PointerDriven = true;

    if (Contact.Ordinal == ToggleBed)
    {
        AssignToggleEngaged(!ToggleEngaged);
    }
    else if (Contact.Ordinal == BarTrough)
    {
        // FractionX is −1 at the left edge and +1 at the right, so the map to [0,1] is (f + 1) / 2. The engine
        //    deliberately does not do this: for a knob the same fraction would mean an angle.
        AssignFillTarget(std::clamp((Contact.FractionX + 1.0f) * 0.5f, 0.0f, 1.0f));
    }
    else if (Contact.Ordinal == ButtonLeft)
    {
        // Left button nudges the meter down, right button up — the sweep is the spring-driven needle, so this is
        //    also the visible proof that interaction feeds the same motion channels the scripted loop drives.
        AssignSweepTarget(static_cast<float>(std::clamp(LastSweep - 0.2, 0.0, 1.0)));
    }
    else if (Contact.Ordinal == ButtonRight)
    {
        AssignSweepTarget(static_cast<float>(std::clamp(LastSweep + 0.2, 0.0, 1.0)));
    }
}

double InterfaceTrialSequence::QuerySweepValue() const noexcept { return LastSweep; }
double InterfaceTrialSequence::QueryFillValue()  const noexcept { return LastFill;  }
bool   InterfaceTrialSequence::IsSweepSettled()  const noexcept { return SweepSettled; }

//------------------------------------------------------------------------------------------------------------------------
//                                                      ADVANCE
//------------------------------------------------------------------------------------------------------------------------
// The scripted cycle: idle, two blips, a long pull to full scale, then a release. Every value is a spring target —
//    nothing is re-tessellated, and interrupting the script mid-motion simply bends the curve.

void InterfaceTrialSequence::AdvanceTrial(InterfaceStructure& Structure, MotionIntegrator& Motion,
                                          double Elapsed, bool RunLoop) noexcept
{
    if (!ChannelsReady) return;

    // P2: the pointer wins. Once the user has driven a control, the scripted demonstration stops overwriting the
    //    targets — otherwise the loop and the user fight each other every frame and the panel appears to snap back.
    if (PointerDriven) RunLoop = false;

    if (RunLoop)
    {
        LoopTime += Elapsed;
        if (LoopTime >= LoopSeconds) LoopTime -= LoopSeconds;

        const double Phase = LoopTime / LoopSeconds;

        double Sweep = 0.05;
        if      (Phase < 0.10) Sweep = 0.05;                                  // idle
        else if (Phase < 0.16) Sweep = 0.38;                                  // blip
        else if (Phase < 0.22) Sweep = 0.08;
        else if (Phase < 0.28) Sweep = 0.52;                                  // blip
        else if (Phase < 0.34) Sweep = 0.08;
        else if (Phase < 0.62) Sweep = 1.00;                                  // pull to full scale
        else if (Phase < 0.72) Sweep = 0.72;                                  // over-run
        else                   Sweep = 0.05;                                  // release

        Motion.Spring(SweepChannel).Depart(Sweep);
        Motion.Spring(FillChannel).Depart(Phase < 0.5 ? Phase * 2.0 : (1.0 - Phase) * 2.0);
        Motion.Spring(ToggleChannel).Depart(Phase > 0.45 ? 1.0 : 0.0);
        Motion.Spring(ButtonChannel).Depart((Phase > 0.14 && Phase < 0.20) || (Phase > 0.26 && Phase < 0.32) ? 1.0 : 0.0);
        ToggleEngaged = Phase > 0.45;
    }
    else
    {
        if (SweepPending)  { Motion.Spring(SweepChannel).Depart(PendingSweep);   SweepPending  = false; }
        if (FillPending)   { Motion.Spring(FillChannel).Depart(PendingFill);     FillPending   = false; }
        if (TogglePending) { Motion.Spring(ToggleChannel).Depart(ToggleEngaged ? 1.0 : 0.0); TogglePending = false; }
    }

    (void)Motion.Advance(Elapsed);

    const double Sweep  = Motion.Spring(SweepChannel).Current;
    const double Fill   = Motion.Spring(FillChannel).Current;
    const double Knob   = Motion.Spring(ToggleChannel).Current;
    const double Press  = Motion.Spring(ButtonChannel).Current;

    LastSweep    = Sweep;
    LastFill     = Fill;
    SweepSettled = Motion.Spring(SweepChannel).Settled;

    // ── Push values into figures. Every one of these is a scalar write on an existing figure. ────────────────────
    const float SweepFraction = static_cast<float>(std::clamp(Sweep, 0.0, 1.0));
    const float FillFraction  = static_cast<float>(std::clamp(Fill,  0.0, 1.0));

    Structure.Access(MeterRing).ScalarAlpha   = SweepFraction;
    Structure.Access(MeterNeedle).ScalarAlpha = SweepFraction;

    // Past 80 % of the sweep the meter turns to the warning colour — the project owns that judgement, not the engine.
    Structure.Access(MeterRing).Palette = SweepFraction > 0.80f ? PaletteSlot::Warning : PaletteSlot::Accent;

    // Bar fill grows from the left edge of the trough.
    {
        constexpr float TroughHalf = 0.081f;
        const float     HalfWidth  = std::max(FillFraction * TroughHalf, 0.0005f);
        InterfaceFigure& Fill_ = Structure.Access(BarFill);
        Fill_.HalfWidth       = HalfWidth;
        Fill_.Placement.Origin.X = -TroughHalf + HalfWidth;
    }

    // Toggle knob slides across its bed and warms to the confirm colour when engaged.
    {
        InterfaceFigure& Knob_ = Structure.Access(ToggleKnob);
        Knob_.Placement.Origin.X = static_cast<float>(-0.012 + 0.024 * std::clamp(Knob, 0.0, 1.0));
        Knob_.Palette            = Knob > 0.5 ? PaletteSlot::Confirm : PaletteSlot::MarkingMute;
    }

    // Buttons: the pressed one sinks slightly and picks up the accent.
    {
        InterfaceFigure& Left = Structure.Access(ButtonLeft);
        Left.Placement.Origin.Z = static_cast<float>(0.0020 - 0.0008 * Press);
        Left.Palette            = Press > 0.5 ? PaletteSlot::Accent : PaletteSlot::SurfaceSunk;

        InterfaceFigure& Right = Structure.Access(ButtonRight);
        Right.Palette = ToggleEngaged ? PaletteSlot::Accent : PaletteSlot::SurfaceSunk;
    }

    // Readout counts 00..99 with the bar; the engine only ever sees a digit.
    {
        const int Count = static_cast<int>(std::clamp(Fill, 0.0, 1.0) * 99.0 + 0.5);
        Structure.Access(ReadoutTens).ScalarAlpha  = static_cast<float>((Count / 10) % 10);
        Structure.Access(ReadoutUnits).ScalarAlpha = static_cast<float>(Count % 10);
    }

    // Telltale pulses while the meter sits in the warning band.
    {
        InterfaceFigure& Lamp = Structure.Access(LampOrdinal);
        const bool Warning = SweepFraction > 0.80f;
        Lamp.ScalarAlpha = Warning ? 1.0f : 0.0f;
        Lamp.Opacity     = Warning ? static_cast<float>(0.45 + 0.55 * std::fabs(std::sin(LoopTime * 7.0))) : 0.18f;
    }
}

} // namespace ProjectZero
} // namespace Frontier
