//==============================================================================================================================================
//                                                          GASGAMEPLAYEMISSION.H
//==============================================================================================================================================
// 📦 The two places the game itself makes gas: a fracture piece separating, and a tyre slipping. Both reduce to a GasEmitterComponent and nothing else.
//
// Neither the fracture runtime nor TyreSlipDynamics includes this file. They describe what happened — a piece of
//    this volume came away here at this speed; this contact patch is slipping this hard — and whoever owns the
//    gas asks for an emitter. That is the whole dependency, and it points one way on purpose:
//
//        TyreSlipDynamics  ──▶  GasTyreContact  ──▶  ConstructTyreEmitter  ──▶  ResolveEmitters  ──▶  the solver
//
// 💡 Why two hooks and not a general event: an emitter needs a radius, a strength and a preset, and the honest
//    way to produce those from a fracture is not the honest way to produce them from a tyre. A fracture scales
//    its radius off the cube root of the piece's volume; a tyre scales its rate off how far past the slip
//    threshold the patch is and lays the cloud flat along the road instead of letting it climb.
//
// ⚠️ Both constructors are pure: they read the event and return a component. They do not ignite a residency, do
//    not claim a rung and do not touch a budget — the governor in GasQualityAllowance.h decides all three, after
//    it has seen every claim in the level. A hook that ignited on its own would be a way for a wall collapsing
//    off-screen to evict the fire the player is standing in front of.
//
// 🔴 Deterministic: fixed-count Newton for the cube root, no std::cbrt, no std::sin, no clock. A tyre that smoked
//    on one machine and not another would desynchronise nothing in the physics and everything in what two
//    players believe they are looking at.

#pragma once

#include "GasDomainResidency.h"
#include "GasEmitterComponent.h"
#include "GasQualityAllowance.h"

#include <cstdint>
#include <cstring>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                   SHARED ARITHMETIC
//------------------------------------------------------------------------------------------------------------------------

/// 📦 Cube root by an exponent-divided seed and a fixed four Newton steps, so two machines agree on it.
/// in    Reading   [-]  a non-negative quantity
/// out   float     [-]  its cube root to within a part in a million; 0 for 0 and for anything negative
/// note  std::cbrt is a libm call whose last bit is not promised to match across platforms, and this answer
///       scales an emitter radius that multiplayer compares. The seed divides the IEEE exponent by three in
///       integer arithmetic, which costs nothing and lands close enough that four steps finish the job for
///       every volume a fracture piece can have
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline float CubeRootOf(float Reading) noexcept
{
    if (!(Reading > 0.0f)) return 0.0f;

    uint32_t Bits = 0u;
    std::memcpy(&Bits, &Reading, sizeof(Bits));
    Bits = Bits / 3u + 709921077u;          // ⅓ of the exponent, with the mantissa bias folded back in
    float Root = 0.0f;
    std::memcpy(&Root, &Bits, sizeof(Root));

    for (uint32_t Step = 0u; Step < 4u; ++Step)
    {
        Root = (2.0f * Root + Reading / (Root * Root)) / 3.0f;
    }
    return Root;
}


inline float ClampedTo(float Reading, float Low, float High) noexcept
{
    return Reading < Low ? Low : (Reading > High ? High : Reading);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  A FRACTURE PIECE
//------------------------------------------------------------------------------------------------------------------------

// What the fracture runtime knows at the moment a piece stops being part of the wall. Nothing in it mentions gas.
struct GasFractureSeparation
{
    uint32_t Carrier      = kNoGasCarrier;          // [-]    - the piece, so its emitter can follow it
    float    Position[3]  = { 0.0f, 0.0f, 0.0f };   // [m]    - where the break happened
    float    PieceVolume  = 0.02f;                  // [m³]   - how much came away
    float    PartingSpeed = 1.0f;                   // [m/s]  - how hard
};

constexpr float GasFractureRadiusInRoots = 0.9f;   // [-]  - plume radius per cube-root metre of piece
constexpr float GasFractureRadiusLeast   = 0.08f;  // [m]  - below this the dust is not worth a lattice
constexpr float GasFractureRadiusMost    = 1.6f;   // [m]  - a single piece, however large, is not a demolition
constexpr float GasFractureHeroRange     = 12.0f;  // [m]  - inside this a puff is simulated; beyond it, a card


/// 📦 The one-shot dust emitter a separating piece is owed.
/// in    Separation   [-]  the event
/// out   GasEmitterComponent  [-]  a one-shot with one request owed, carrying the brick dust preset
/// note  radius from the cube root of the volume, so a piece eight times the size makes a puff twice as wide,
///       which is the relationship between a brick and a wall rather than between two numbers
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline GasEmitterComponent ConstructFractureEmitter(const GasFractureSeparation& Separation) noexcept
{
    GasEmitterComponent Emitter;
    Emitter.Carrier       = Separation.Carrier;
    Emitter.Preset        = "brick_fracture_dust";
    Emitter.CarrierAt[0]  = Separation.Position[0];
    Emitter.CarrierAt[1]  = Separation.Position[1];
    Emitter.CarrierAt[2]  = Separation.Position[2];
    Emitter.Spread        = GasEmitterSpread::Point;
    Emitter.Radius        = ClampedTo(CubeRootOf(Separation.PieceVolume) * GasFractureRadiusInRoots,
                                      GasFractureRadiusLeast, GasFractureRadiusMost);
    Emitter.Strength      = ClampedTo(0.45f + Separation.PartingSpeed * 0.18f, 0.45f, 2.0f);
    Emitter.DragShare     = 0.0f;
    Emitter.Continuous    = false;
    Emitter.Requests      = 1u;
    return Emitter;
}


/// 📦 How long the dust from a separation is allowed to live.
/// out   GasResidencyConsent   [-]  a retiring one-shot; emission stops well before the cloud does
/// note  🔴 Retire is true and the policy is Triggered, which is the only combination GasRetires() admits. A wall
///       of forty pieces that each kept their storage would be the whole 256 MB ceiling on one collapse
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline GasResidencyConsent ConstructFractureConsent() noexcept
{
    GasResidencyConsent Consent;
    Consent.Policy   = GasRunPolicy::Triggered;
    Consent.Lifetime = 0.35f;
    Consent.Retire   = true;
    return Consent;
}


/// 📦 The best rung a fracture puff may ask for at this distance.
/// in    ViewerDistance   [m]  break to camera
/// out   GasQuality       [-]  Mid at best, and a card beyond twelve metres
/// note  deliberately capped below Hero: a puff lasting a third of a second cannot earn the one Hero slot, and
///       a collapsing wall produces enough of them to take every slot there is
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline GasQuality QualityForFracture(float ViewerDistance) noexcept
{
    if (ViewerDistance > GasFractureHeroRange) return GasQuality::Flipbook;
    const GasQuality ByDistance = QualityForDistance(ViewerDistance);
    return static_cast<uint32_t>(ByDistance) < static_cast<uint32_t>(GasQuality::Mid) ? GasQuality::Mid : ByDistance;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   A SLIPPING TYRE
//------------------------------------------------------------------------------------------------------------------------

// What the tyre solver already computes, restated without a word about gas. Forward is the direction of travel of
//    the contact patch, which is where the smoke is dragged — a burnout lays its cloud behind the car, not above it.
struct GasTyreContact
{
    uint32_t Carrier      = kNoGasCarrier;          // [-]    - the wheel
    float    Position[3]  = { 0.0f, 0.0f, 0.0f };   // [m]    - the contact patch
    float    Forward[3]   = { 1.0f, 0.0f, 0.0f };   // [m/s]  - velocity of the patch over the road
    float    SlipRatio    = 0.0f;                   // [-]    - κ from TyreSlipDynamics
    float    NormalLoad   = 4000.0f;                // [N]    - how hard the tyre is pressed down
    float    PatchRadius  = 0.16f;                  // [m]    - half the contact width
};

constexpr float GasTyreSlipOnset   = 0.18f;     // [-]  - below this a tyre is gripping, and gripping is not smoke
constexpr float GasTyreSlipFull    = 0.95f;     // [-]  - at and beyond this the emitter is at full strength
constexpr float GasTyreLoadRated   = 4000.0f;   // [N]  - the load the strength curve is written against
constexpr float GasTyreDragShare   = 0.35f;     // [-]  - how much of the patch's travel the cloud inherits


/// 📦 Is this contact patch smoking at all?
/// in    Contact   [-]  the patch this tick
/// out   bool      [-]  true once slip passes the onset and there is load on the tyre
/// note  the onset exists so that a car driving normally does not hold a domain open for its four wheels, which
///       would spend the entire concurrency ceiling on an effect nobody can see
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline bool TyreSmokes(const GasTyreContact& Contact) noexcept
{
    const float Slip = Contact.SlipRatio < 0.0f ? -Contact.SlipRatio : Contact.SlipRatio;
    return Slip > GasTyreSlipOnset && Contact.NormalLoad > 1.0f;
}


/// 📦 The continuous burnout emitter a slipping patch is owed.
/// in    Contact   [-]  the patch this tick
/// out   GasEmitterComponent  [-]  a flat disc source, dragged along the patch's travel; disabled when not slipping
/// note  strength rises from nothing at the onset rather than switching on, because a tyre that begins smoking at
///       full rate the instant κ crosses a threshold is the single most recognisable tell of a scripted effect
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline GasEmitterComponent ConstructTyreEmitter(const GasTyreContact& Contact) noexcept
{
    const float Slip  = Contact.SlipRatio < 0.0f ? -Contact.SlipRatio : Contact.SlipRatio;
    const float Past  = ClampedTo((Slip - GasTyreSlipOnset) / (GasTyreSlipFull - GasTyreSlipOnset), 0.0f, 1.0f);
    const float Press = ClampedTo(Contact.NormalLoad / GasTyreLoadRated, 0.25f, 1.75f);

    GasEmitterComponent Emitter;
    Emitter.Carrier        = Contact.Carrier;
    Emitter.Preset         = "tyre_burnout";
    Emitter.CarrierAt[0]   = Contact.Position[0];
    Emitter.CarrierAt[1]   = Contact.Position[1];
    Emitter.CarrierAt[2]   = Contact.Position[2];
    Emitter.CarrierSpeed[0] = Contact.Forward[0];
    Emitter.CarrierSpeed[1] = Contact.Forward[1];
    Emitter.CarrierSpeed[2] = Contact.Forward[2];
    Emitter.Spread         = GasEmitterSpread::Disc;
    Emitter.Radius         = ClampedTo(Contact.PatchRadius * 1.4f, 0.08f, 0.6f);
    Emitter.Strength       = Past * Press;
    Emitter.DragShare      = GasTyreDragShare;
    Emitter.Continuous     = true;
    Emitter.Enabled        = TyreSmokes(Contact);
    return Emitter;
}


/// 📦 How long a burnout cloud lives once the tyre stops slipping.
/// out   GasResidencyConsent   [-]  a retiring one-shot, re-ignited every tick the tyre is still slipping
/// note  a burnout is modelled as a short one-shot that gameplay keeps re-firing rather than as an always-on
///       domain, so that letting off the throttle ends it with no second decision anywhere
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline GasResidencyConsent ConstructTyreConsent() noexcept
{
    GasResidencyConsent Consent;
    Consent.Policy   = GasRunPolicy::Triggered;
    Consent.Lifetime = 0.5f;
    Consent.Retire   = true;
    return Consent;
}


/// 📦 The best rung a burnout may ask for at this distance.
/// in    ViewerDistance   [m]  patch to camera
/// out   GasQuality       [-]  Near at best; the player's own wheels are close, and four of them share the level
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline GasQuality QualityForTyre(float ViewerDistance) noexcept
{
    const GasQuality ByDistance = QualityForDistance(ViewerDistance);
    return static_cast<uint32_t>(ByDistance) < static_cast<uint32_t>(GasQuality::Near) ? GasQuality::Near : ByDistance;
}

}   // namespace Frontier
