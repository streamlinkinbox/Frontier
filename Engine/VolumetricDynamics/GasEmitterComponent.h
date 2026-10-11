//==============================================================================================================================================
//                                                           GASEMITTERCOMPONENT.H
//==============================================================================================================================================
// 📦 An emitter is a component on any object; a domain is a scene object with bounds. This is the seam that puts the first inside the second.
//
// A domain owns storage: an origin, a span, and a lattice it is charged for. An emitter owns nothing — it is a
//    transform, a shape, a preset and a strength, carried by whatever object happens to be smoking. A tyre and a
//    fracture piece both reduce to one, which is what lets a level hold forty of them and twelve domains.
//
//    ① RESOLVE WHERE IT IS — the carrier writes its world placement each tick; the emitter adds its own offset.
//    ② RESOLVE WHICH DOMAIN HOLDS IT — the nearest domain that encloses it, by centre distance, ties to the lowest
//       index. An emitter outside every domain gets an implicit one sized from its own radius, so an effect that
//       nobody authored a domain for still runs rather than silently doing nothing.
//    ③ RESOLVE WHAT IT INJECTS — the preset's emitter half, scaled by strength and by the shape's coverage.
//
// 💡 The carrier is a position and a velocity written in by the owner, not a pointer to the owner. A tyre emitter
//    that held a TyreSlipDynamics* would make this directory depend on the vehicle solver, and the headless gas
//    checks link neither. The dependency runs one way: gameplay knows about gas, gas knows about nothing.
//
// ⚠️ A one-shot emitter is fired by raising Requests, and ResolveEmitters consumes exactly one per call. Gameplay
//    raising two in a tick gets two injections on two ticks, never one silently dropped — a fracture spraying
//    nine pieces in one frame is ordinary, and nine puffs is the correct answer.
//
// 🔴 Nothing here allocates, calls std::sin, or reads a clock. The determinism contract in CoarseGasField.h covers
//    the solver; an emitter that resolved differently on two machines would break it one layer above the solver,
//    which is exactly where multiplayer would never think to look.

#pragma once

#include "CoarseGasField.h"
#include "GasPresetLibrary.h"

#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE COMPONENT
//------------------------------------------------------------------------------------------------------------------------

// How the source is spread over its radius. The shape is not geometry the solver sees — it is a coverage factor
//    and a velocity bias, because the gaussian injection in CoarseGasField.h takes one centre and one radius.
enum class GasEmitterSpread : uint32_t
{
    Point  = 0u,   // [-] - the whole rate at one place; a lamp wick, a muzzle
    Disc   = 1u,   // [-] - flat on the floor, pushed sideways rather than up; a tyre contact patch
    Ring   = 2u,   // [-] - a hollow annulus; a vent collar, a wheel arch
    Column = 3u,   // [-] - tall and thin, carrying its upward velocity; a chimney, a blowout
};

constexpr uint32_t GasEmitterSpreadCount = 4u;

constexpr uint32_t kNoGasCarrier = 0xffffffffu;   // [-] - an emitter nobody owns; valid, and used by the editor
constexpr uint32_t kNoGasDomain  = 0xffffffffu;   // [-] - no authored domain encloses this emitter


// One emitter component. Carrier and CarrierVelocity are written by the object each tick; everything else is
//    authored once. Deliberately a plain aggregate with no methods: it is saved, replicated and diffed.
struct GasEmitterComponent
{
    uint32_t         Carrier          = kNoGasCarrier;              // [-]     - identity of the object holding it
    const char*      Preset           = "camp_fire";                // [-]     - identity into GasPresetLibrary()
    float            CarrierAt[3]     = { 0.0f, 0.0f, 0.0f };       // [m]     - world placement of that object
    float            CarrierSpeed[3]  = { 0.0f, 0.0f, 0.0f };       // [m/s]   - how fast it is travelling
    float            Offset[3]        = { 0.0f, 0.0f, 0.0f };       // [m]     - the emitter's own place on it
    GasEmitterSpread Spread           = GasEmitterSpread::Point;    // [-]     - how the rate covers the radius
    float            Radius           = 0.25f;                      // [m]     - gaussian falloff radius
    float            Strength         = 1.0f;                       // [-]     - multiplier over the preset's half
    float            DragShare        = 0.0f;                       // [-]     - how much of the carrier's speed it inherits
    bool             Enabled          = true;                       // [-]     - authored off, or switched off by gameplay
    bool             Continuous       = true;                       // [-]     - false means it waits for a request
    uint32_t         Requests         = 0u;                         // [-]     - ⚠️ one-shot firings owed, see the header
};


/// 📦 Where the emitter actually is this tick: the carrier's placement plus the emitter's own offset.
/// in    Emitter        [-]  the component
/// out   OutPlacement   [m]  world position, three floats
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline void ResolvePlacement(const GasEmitterComponent& Emitter, float OutPlacement[3]) noexcept
{
    OutPlacement[0] = Emitter.CarrierAt[0] + Emitter.Offset[0];
    OutPlacement[1] = Emitter.CarrierAt[1] + Emitter.Offset[1];
    OutPlacement[2] = Emitter.CarrierAt[2] + Emitter.Offset[2];
}


/// 📦 The fraction of a point source's rate this spread delivers at the centre.
/// in    Spread    [-]  how the source covers its radius
/// out   float     [-]  1 for a point, less for anything that spends the same rate over more room
/// note  a ring is the thinnest because its mass sits off-centre, where the gaussian is already falling away
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline float SpreadCoverage(GasEmitterSpread Spread) noexcept
{
    switch (Spread)
    {
        case GasEmitterSpread::Point:  return 1.0f;
        case GasEmitterSpread::Disc:   return 0.72f;
        case GasEmitterSpread::Ring:   return 0.55f;
        case GasEmitterSpread::Column: return 0.84f;
    }
    return 1.0f;
}


/// 📦 The emitter's contribution for one tick, in the only currency the solver accepts.
/// in    Emitter    [-]  the component, with its carrier already written for this tick
/// in    Tuning     [-]  the resolved preset, whose emitter half supplies the rates
/// out   GasEmission  [-]  centre, radius, smoke rate, temperature rate and imparted velocity
/// note  a disc spends its upward velocity sideways along the carrier's travel, which is the difference between
///       a tyre laying smoke down the road and a tyre apparently venting a chimney
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline GasEmission ResolveEmission(const GasEmitterComponent& Emitter, const GasSettings& Tuning) noexcept
{
    GasEmission Emission;
    ResolvePlacement(Emitter, Emission.Position);

    const float Coverage = SpreadCoverage(Emitter.Spread) * Emitter.Strength * Tuning.EmitterRate;
    Emission.Radius          = Emitter.Radius > 0.01f ? Emitter.Radius : 0.01f;
    Emission.SmokeRate       = Tuning.EmitterSmoke * Coverage;
    Emission.TemperatureRate = Tuning.EmitterTemperature * Coverage;

    const float Rise = Tuning.EmitterUpwardVelocity * Emitter.Strength;
    const bool  Flat = Emitter.Spread == GasEmitterSpread::Disc;
    Emission.Velocity[0] = Emitter.CarrierSpeed[0] * Emitter.DragShare;
    Emission.Velocity[1] = Emitter.CarrierSpeed[1] * Emitter.DragShare;
    Emission.Velocity[2] = Emitter.CarrierSpeed[2] * Emitter.DragShare + (Flat ? Rise * 0.2f : Rise);
    return Emission;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 WHICH DOMAIN HOLDS IT
//------------------------------------------------------------------------------------------------------------------------

// A domain as the router sees it: the cube CoarseGasField.h already is, plus the identity the scene knows it by.
struct GasDomainExtent
{
    uint32_t Identity   = 0u;                      // [-] - what the scene and the outliner call it
    float    Origin[3]  = { 0.0f, 0.0f, 0.0f };    // [m] - minimum corner, matching CoarseGasField::Origin
    float    Span       = 8.0f;                    // [m] - edge length of the cube
};


/// 📦 Does this cube hold the emitter, with room for its radius?
/// in    Extent     [-]  the domain
/// in    Placement  [m]  where the emitter is
/// in    Radius     [m]  the emitter's falloff radius
/// out   bool       [-]  true when the emitter and most of its plume fit inside
/// note  the radius is required to fit on every axis. An emitter straddling a wall of the domain would have half
///       its injection clipped by the lattice and would read as mysteriously weak rather than as misplaced
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline bool ExtentEncloses(const GasDomainExtent& Extent, const float Placement[3], float Radius) noexcept
{
    for (uint32_t Axis = 0u; Axis < 3u; ++Axis)
    {
        if (Placement[Axis] - Radius < Extent.Origin[Axis]) return false;
        if (Placement[Axis] + Radius > Extent.Origin[Axis] + Extent.Span) return false;
    }
    return true;
}


/// 📦 The authored domain that should hold this emitter: the nearest enclosing one.
/// in    Extents      [-]  the authored domains, in scene order
/// in    ExtentCount  [-]  how many
/// in    Placement    [m]  where the emitter is
/// in    Radius       [m]  its falloff radius
/// out   uint32_t     [-]  index into Extents, or kNoGasDomain when none encloses it
/// note  🔴 nearest by centre distance, ties broken by the lower index. Nested domains are legal — a room inside
///       a street — and "nearest" is what makes the room win rather than whichever was authored first
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline uint32_t ResolveHostExtent(const GasDomainExtent* Extents, uint32_t ExtentCount,
                                  const float Placement[3], float Radius) noexcept
{
    uint32_t Chosen = kNoGasDomain;
    float    Closest = 0.0f;
    for (uint32_t Index = 0u; Index < ExtentCount; ++Index)
    {
        if (!ExtentEncloses(Extents[Index], Placement, Radius)) continue;

        float Reach = 0.0f;
        for (uint32_t Axis = 0u; Axis < 3u; ++Axis)
        {
            const float Centre = Extents[Index].Origin[Axis] + Extents[Index].Span * 0.5f;
            const float Apart  = Placement[Axis] - Centre;
            Reach += Apart * Apart;
        }
        if (Chosen == kNoGasDomain || Reach < Closest)
        {
            Chosen  = Index;
            Closest = Reach;
        }
    }
    return Chosen;
}


constexpr float GasImplicitSpanInRadii = 6.0f;    // [-] - how much room a plume needs above its own source
constexpr float GasImplicitSpanLeast   = 1.0f;    // [m] - a lattice smaller than this is all boundary
constexpr float GasImplicitSpanMost    = 24.0f;   // [m] - beyond this, author a domain and accept the budget


/// 📦 The cube to run an unhosted emitter in, sized from the emitter itself.
/// in    Emitter    [-]  the component, with its carrier written
/// out   GasDomainExtent  [-]  a cube centred on the emitter horizontally, with the source near its floor
/// note  the source sits a fifth of the way up rather than in the middle, because gas rises and a centred source
///       spends half the lattice on room the plume never reaches
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline GasDomainExtent ConstructImplicitExtent(const GasEmitterComponent& Emitter) noexcept
{
    float Placement[3];
    ResolvePlacement(Emitter, Placement);

    float Span = Emitter.Radius * GasImplicitSpanInRadii;
    if (Span < GasImplicitSpanLeast) Span = GasImplicitSpanLeast;
    if (Span > GasImplicitSpanMost)  Span = GasImplicitSpanMost;

    GasDomainExtent Extent;
    Extent.Identity  = Emitter.Carrier;
    Extent.Span      = Span;
    Extent.Origin[0] = Placement[0] - Span * 0.5f;
    Extent.Origin[1] = Placement[1] - Span * 0.5f;
    Extent.Origin[2] = Placement[2] - Span * 0.2f;
    return Extent;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE ROUTING
//------------------------------------------------------------------------------------------------------------------------

// What one emitter resolved to this tick. Implicit is not an error: it is how a fracture piece in open ground
//    smokes without a level designer having authored a cube around the wall it used to be part of.
struct GasEmitterRouting
{
    uint32_t        Emitter   = 0u;              // [-] - index into the emitters handed in
    uint32_t        Host      = kNoGasDomain;    // [-] - index into the authored extents, or kNoGasDomain
    bool            Implicit  = false;           // [-] - true when Extent below was constructed rather than authored
    GasDomainExtent Extent;                      // [-] - the cube this emission belongs in, either way
    GasEmission     Emission;                    // [-] - what to inject there
};


/// 📦 Resolve every emitter to a domain and an injection, in one deterministic sweep.
/// in    Emitters      [-]  the components; Carrier placement already written for this tick
/// in    EmitterCount  [-]  how many
/// in    Extents       [-]  the authored domains
/// in    ExtentCount   [-]  how many
/// out   OutRouting    [-]  one entry per emitter that contributes, in ascending emitter order
/// in    RoutingRoom   [-]  capacity of OutRouting
/// out   uint32_t      [-]  how many entries were written
/// note  ⚠️ a one-shot emitter has one request consumed here, which is why Emitters is not const — resolving
///       twice in a tick would fire a one-shot twice, and the caller would have no way to notice
/// err   a disabled emitter, a zero-strength one, and a one-shot with nothing owed all contribute nothing;
///       overrunning RoutingRoom stops writing and the return says how much was taken
/// cost  ✔️ linear in emitters times domains; both are small and neither allocates
/// tag   api, nonallocating, nonthrowing
inline uint32_t ResolveEmitters(GasEmitterComponent* Emitters, uint32_t EmitterCount,
                                const GasDomainExtent* Extents, uint32_t ExtentCount,
                                GasEmitterRouting* OutRouting, uint32_t RoutingRoom) noexcept
{
    uint32_t Written = 0u;
    for (uint32_t Index = 0u; Index < EmitterCount && Written < RoutingRoom; ++Index)
    {
        GasEmitterComponent& Emitter = Emitters[Index];
        if (!Emitter.Enabled || Emitter.Strength <= 0.0f) continue;
        if (!Emitter.Continuous)
        {
            if (Emitter.Requests == 0u) continue;
            --Emitter.Requests;
        }

        float Placement[3];
        ResolvePlacement(Emitter, Placement);

        GasEmitterRouting& Routed = OutRouting[Written];
        Routed.Emitter  = Index;
        Routed.Host     = ResolveHostExtent(Extents, ExtentCount, Placement, Emitter.Radius);
        Routed.Implicit = Routed.Host == kNoGasDomain;
        Routed.Extent   = Routed.Implicit ? ConstructImplicitExtent(Emitter) : Extents[Routed.Host];
        Routed.Emission = ResolveEmission(Emitter, ConstructPresetSettings(Emitter.Preset));
        ++Written;
    }
    return Written;
}


/// 📦 Inject one resolved routing into a field that is already sized to its extent.
/// in    Field     [-]  the domain's field, written in place
/// in    Routed    [-]  what ResolveEmitters produced
/// in    Interval  [s]  the advance interval the field is being stepped by
/// out   bool      [-]  false when the field is not the one this routing resolved to, in which case nothing happens
/// note  the check is what stops a routing from a demoted domain being injected into its neighbour after the
///       governor has reshuffled the rungs; the cost of being wrong is gas appearing in the wrong room
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline bool InjectRouting(CoarseGasField& Field, const GasEmitterRouting& Routed, float Interval) noexcept
{
    if (!PositionEnclosed(Field, Routed.Emission.Position)) return false;
    InjectEmission(Field, Routed.Emission, Interval);
    return true;
}

}   // namespace Frontier
