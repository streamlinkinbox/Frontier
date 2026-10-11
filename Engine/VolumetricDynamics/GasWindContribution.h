//==============================================================================================================================================
//                                                         GASWINDCONTRIBUTION.H
//==============================================================================================================================================
// 📦 The gas pushing back: one more contributor to the wind a body already samples, and the drag that contribution exerts.
//
// 🔴 TWO-WAY COUPLING IS OFF UNTIL SOMETHING TURNS IT ON, AND THE SWITCH IS PER OBJECT.
//    GasCouplingConsent::CouplingEnabled is false by default. Which bodies should carry it is a design decision
//    that has not been taken — debris, cloth and ragdolls are obvious, a vehicle pushed by the smoke it is
//    itself producing almost certainly is not — so the decision is left in the scene where it can be changed
//    without a recompile, rather than being guessed here and hard-coded.
//
// 💡 WHY A WIND CONTRIBUTION AND NOT A PRESSURE READBACK.
//    The obvious coupling reads the pressure image back off the device and hands its gradient to the rigid-body
//    solver. That is wrong three times over, and the alternative removes all three at once:
//
//    | Property          | Pressure readback                        | This contribution                     |
//    |-------------------|------------------------------------------|---------------------------------------|
//    | Moved per frame   | about 16 MB at 128 cubed                 | nothing — the reading is already here |
//    | Pipeline          | a stall, or three images of staging      | none; CoarseGasField is on the CPU    |
//    | Stability         | a gradient can add energy to a body      | drag is dissipative and cannot        |
//    | Reproducible      | no, and that ends deterministic play     | yes, by CoarseGasField's construction |
//    | New interface     | an entire coupling path                  | one contributor and one force term    |
//
//    The force the body feels is ordinary aerodynamic drag against the relative velocity. Because it always
//    opposes the body's motion through the gas, it can only remove kinetic energy from the pair, which is what
//    makes it safe to hand to a rigid-body solver that knows nothing about fluids.
//
// 📐 F = ½ ρ Cd A ‖𝑣gas − 𝑣body‖ (𝑣gas − 𝑣body)
//    Quadratic in the relative speed, as drag is above the creeping-flow regime. Buoyancy from the temperature
//    reading is the same shape and rides along with it.
//
// ⚠️ A body also displaces gas, and that is the other admission — GasCollisionIntake.h. The two are separate
//    opt-ins on purpose: obstructing the gas is cheap and almost always wanted, being pushed by it is neither.

#pragma once

#include "CoarseGasField.h"
#include "../DisplayPresentation/WindField.h"

#include <cmath>
#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE CONSENT
//------------------------------------------------------------------------------------------------------------------------

// Held per object. Everything here is inert while CouplingEnabled is false, which is the resting condition.
struct GasCouplingConsent
{
    bool  CouplingEnabled   = false;   // [-]      - 🚩 the switch; nothing below applies until it is true
    bool  BuoyancyEnabled   = false;   // [-]      - hot gas lifts the body as well as dragging it
    float DragCoefficient   = 1.05f;   // [-]      - Cd; 1.05 is a cube, 0.47 a sphere, 1.28 a flat plate
    float ReferenceArea     = 0.25f;   // [m²]     - frontal area presented to the flow
    float Mass              = 10.0f;   // [kg]     - used only by the stability clamp below
    float Strength          = 1.0f;    // [-]      - authored scale; 0 is equivalent to not consenting
    float AmbientDensity    = 1.225f;  // [kg/m³]  - air at sea level; smoke is denser but not by enough to care
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE COMBINED FLOW
//------------------------------------------------------------------------------------------------------------------------

/// 📦 The air a body at this position is actually moving through: the weather's wind, plus every gas field that
///    encloses the position. One sample point, one answer, which is the reason this sits behind WindField's
///    vocabulary instead of beside it.
/// in    Weather      [-]    the wind settings already driving cloud, fog and precipitation
/// in    Fields       [-]    gas fields to consider; those not enclosing the position contribute nothing
/// in    FieldCount   [-]    how many
/// in    Position     [m]    world position
/// in    Time         [s]    scene time, for the wind's gust envelope
/// out   OutVelocity  [m/s]  three components
/// note  gas contributions are summed in ascending order so two machines with the same fields agree exactly;
///       the weather term is added last and is the only part of the sum that is not reproducible, which is
///       harmless because every machine runs the same authored weather
/// cost  🚩  WindField::Sample is six noise evaluations; call this a handful of times per frame, not per voxel
/// tag   api, nonallocating, nonthrowing
inline void SampleCombinedFlow(const WindSettings& Weather, const CoarseGasField* const* Fields,
                               uint32_t FieldCount, const float Position[3], float Time,
                               float OutVelocity[3]) noexcept
{
    float Total[3] = { 0.0f, 0.0f, 0.0f };
    for (uint32_t Entry = 0u; Entry < FieldCount; ++Entry)
    {
        const CoarseGasField* Field = Fields[Entry];
        if (Field == nullptr) continue;
        if (!PositionEnclosed(*Field, Position)) continue;
        float Local[3];
        SampleGasVelocity(*Field, Position, Local);
        Total[0] += Local[0];
        Total[1] += Local[1];
        Total[2] += Local[2];
    }

    float Weatherly[3];
    WindField::Sample(Weather, Position, Time, Weatherly);
    OutVelocity[0] = Total[0] + Weatherly[0];
    OutVelocity[1] = Total[1] + Weatherly[1];
    OutVelocity[2] = Total[2] + Weatherly[2];
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE FORCE
//------------------------------------------------------------------------------------------------------------------------

/// 📦 Drag exerted on a consenting body by the flow it sits in. Zero, and zero cost, when the body has not
///    opted in.
/// in    Consent      [-]    the per-object switch and its coefficients
/// in    FlowVelocity [m/s]  the combined flow at the body, from SampleCombinedFlow
/// in    BodyVelocity [m/s]  the body's own velocity
/// in    Interval     [s]    Δτ the solver is about to integrate over; used only by the clamp
/// out   OutForce     [N]    three components, ready to be added to the body's accumulated force
/// err   a non-consenting body, a zero Strength or a non-positive mass all yield exactly zero force
/// post  ‖OutForce‖ · Interval ≤ Mass · ‖FlowVelocity − BodyVelocity‖ — see the clamp note
/// note  ⚠️ THE CLAMP IS WHAT KEEPS THE RIGID-BODY SOLVER STABLE AND IS NOT OPTIONAL. Drag is dissipative in
///       the continuum, but integrated explicitly it can overshoot: a light body in a fast flow can be handed
///       an impulse larger than the one that would equalise the two velocities, and it then oscillates with
///       growing amplitude. Capping the impulse at exactly that equalising value makes overshoot impossible
///       for any mass, any interval and any coefficient — the body can be carried along with the gas, never
///       flung past it.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline void ResolveDragForce(const GasCouplingConsent& Consent, const float FlowVelocity[3],
                             const float BodyVelocity[3], float Interval, float OutForce[3]) noexcept
{
    OutForce[0] = OutForce[1] = OutForce[2] = 0.0f;
    if (!Consent.CouplingEnabled)   return;
    if (Consent.Strength <= 0.0f)   return;
    if (Consent.Mass     <= 0.0f)   return;
    if (Interval         <= 0.0f)   return;

    const float Relative[3] = { FlowVelocity[0] - BodyVelocity[0],
                                FlowVelocity[1] - BodyVelocity[1],
                                FlowVelocity[2] - BodyVelocity[2] };
    const float Speed = std::sqrt(Relative[0] * Relative[0] + Relative[1] * Relative[1] + Relative[2] * Relative[2]);
    if (Speed <= 1.0e-6f) return;

    const float Magnitude = 0.5f * Consent.AmbientDensity * Consent.DragCoefficient
                          * Consent.ReferenceArea * Consent.Strength * Speed * Speed;

    // The impulse that would bring the body exactly to the flow's velocity in this interval. Nothing may exceed
    //    it, because overshooting it is the instability described above.
    const float Equalising = Consent.Mass * Speed / Interval;
    const float Applied    = Magnitude < Equalising ? Magnitude : Equalising;

    const float Direction[3] = { Relative[0] / Speed, Relative[1] / Speed, Relative[2] / Speed };
    OutForce[0] = Direction[0] * Applied;
    OutForce[1] = Direction[1] * Applied;
    OutForce[2] = Direction[2] * Applied;
}


/// 📦 Upward force from hot gas beneath a consenting body, on the same opt-in.
/// in    Consent     [-]  BuoyancyEnabled must also be true; CouplingEnabled alone is not sufficient
/// in    Field       [-]  the gas field to read the temperature from
/// in    Position    [m]  world position of the body
/// out   float       [N]  along axis 2, which is up; zero when either switch is off or the body is outside
/// note  the displaced volume is taken as ReferenceArea raised to three halves, which is a deliberate
///       approximation: a body consenting to be lifted by smoke does not warrant a second authored dimension
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline float ResolveBuoyantLift(const GasCouplingConsent& Consent, const CoarseGasField& Field,
                                const float Position[3]) noexcept
{
    if (!Consent.CouplingEnabled) return 0.0f;
    if (!Consent.BuoyancyEnabled) return 0.0f;
    if (!PositionEnclosed(Field, Position)) return 0.0f;

    float Coordinate[3];
    LatticeCoordinate(Field, Position, Coordinate);
    const float Excess = ReadTrilinear(Field.Temperature, Coordinate[0], Coordinate[1], Coordinate[2]);
    if (Excess <= 0.0f) return 0.0f;

    // 📐 An ideal gas expands with absolute temperature, so the density deficit is ΔT / T₀ about an ambient
    //    288 K. The lift is that deficit acting on the displaced weight.
    const float Deficit = Excess / 288.0f;
    const float Volume  = std::pow(Consent.ReferenceArea, 1.5f);
    return Consent.AmbientDensity * Volume * 9.80665f * Deficit * Consent.Strength;
}


/// 📦 Whether anything at all would be computed for this body, for a caller deciding what to even sample.
/// in    Consent   [-]  the per-object switch
/// out   bool      [-]  true only when coupling is enabled with a positive strength and mass
/// use   skip SampleCombinedFlow entirely for bodies this refuses; the sampling is the expensive half
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline bool CouplingAdmitted(const GasCouplingConsent& Consent) noexcept
{
    return Consent.CouplingEnabled && Consent.Strength > 0.0f && Consent.Mass > 0.0f;
}

}   // namespace Frontier
