//==============================================================================================================================================
//                                                        GASDOMAINRESIDENCY.H
//==============================================================================================================================================
// 📦 How long a gas domain holds the storage it was granted — and when it hands it back.
//
// 🔴 MOST GAS IS A ONE-SHOT, AND A ONE-SHOT THAT STAYS RESIDENT IS A LEAK WITH A SCHEDULE.
//    A detonation, a dust hit, a pipe burst: fires once, runs a few seconds, finished. Finished has to mean
//    destroyed. The outliner row survives — it is still an entity with a transform, still re-firable — but
//    the velocity, smoke and temperature fields are released the instant the last wisp is gone. Forty
//    one-shots in a corridor otherwise spend the whole level holding forty allowances that nothing reads,
//    and the governor in GasQualityAllowance.h would refuse the twelfth live domain to pay for them.
//
// 📐 FOUR PHASES, AND THE THIRD ONE EXISTS BECAUSE THE ALTERNATIVE LOOKS BROKEN.
//
//      Dormant    in the scene, costing nothing. Where a one-shot starts and where it returns.
//      Burning    emission is on and the solver advances. Ends at Lifetime.
//      Fading     emission is off, the solver still advances, the plume dissipates by itself. Cutting at
//                 Lifetime instead would make a column of smoke vanish in mid-air, which reads as a bug
//                 however correct the bookkeeping is. Charged to the budget in full, because it is still
//                 a live domain by every measure the governor uses.
//      Released   nothing advances; the fields are gone and the bytes are back.
//
//    Fading ends on a measurement rather than a second clock: the solver reports its peak remaining smoke,
//    and the domain is released when that falls under FaintestVisible. A fixed fade would be too long for a
//    puff and too short for a smoke column, and both mistakes are visible.
//
// ⚠️ RELEASE IS REQUESTED HERE, NOT PERFORMED HERE. This header owns the decision and the arithmetic; the
//    field itself is freed by whoever owns the allocation, on the tick after Retired() first answers true.
//    A header that frees memory it did not allocate is a header that has to be trusted rather than read.
//
// Reference: the HTML design this is ported from — Experimental/ProjectZeroEditor/GasSpecification.js,
//    GasRetires / GasResidency / GasFadeSeconds — and the card that shows it, GasPanel.jsx.

#pragma once

#include "GasQualityAllowance.h"

#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE PHASES
//------------------------------------------------------------------------------------------------------------------------

enum class GasPhase : uint32_t
{
    Dormant  = 0u,   // [-] - present, not simulating, holding nothing
    Burning  = 1u,   // [-] - emitting and advancing
    Fading   = 2u,   // [-] - no longer emitting, still advancing, dissipating
    Released = 3u,   // [-] - finished; the fields have been handed back
};

constexpr uint32_t GasPhaseCount = 4u;


/// 📦 The phase as the inspector spells it, so the card and the runtime cannot drift into two vocabularies.
/// in    Phase          [-]  which phase
/// out   const char*    [-]  a static string; never null
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline const char* GasPhaseReadout(GasPhase Phase) noexcept
{
    switch (Phase)
    {
        case GasPhase::Dormant:  return "Dormant - in the scene, not simulating";
        case GasPhase::Burning:  return "Burning - emitting and advancing";
        case GasPhase::Fading:   return "Fading - dissipating, still charged to the budget";
        case GasPhase::Released: return "Released - destroyed, fields handed back";
    }
    return "Dormant - in the scene, not simulating";
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE RUN POLICY
//------------------------------------------------------------------------------------------------------------------------

// The four policies of the inspector card, in the inspector's order. Dormant is first because in a level it
//    is the normal case: a row exists from the moment it is placed, and simulating is a separate thing
//    something has to ask for.
enum class GasRunPolicy : uint32_t
{
    Dormant   = 0u,   // [-] - never, until something fires it
    Triggered = 1u,   // [-] - fires, runs, retires. Most gas is this one
    Proximity = 2u,   // [-] - while the viewer is inside the far band
    Always    = 3u,   // [-] - persistent: a camp fire, a chimney, a vent
};

constexpr uint32_t GasRunPolicyCount = 4u;


struct GasResidencyConsent
{
    GasRunPolicy Policy          = GasRunPolicy::Dormant;  // [-]        - what asks it to simulate
    float        Lifetime        = 4.0f;                   // [s]        - emission duration for a one-shot
    bool         Retire          = true;                   // [-]        - 🔴 hand the fields back when finished
    float        FaintestVisible = 0.006f;                 // [density]  - peak smoke under which nothing is seen
    float        FadeCeiling     = 12.0f;                  // [s]        - a fade this long is a stuck domain
    float        ProximityRange  = 150.0f;                 // [m]        - the far band, from QualityForDistance
};


/// 📦 Does this domain destroy itself when it finishes?
/// in    Consent    [-]  the domain's run policy
/// out   bool       [-]  true for a retiring one-shot
/// note  only a triggered domain can retire. An always-on chimney has no "finished" to retire at, and a
///       proximity domain is answering a question that can be asked again a second later
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline bool GasRetires(const GasResidencyConsent& Consent) noexcept
{
    return Consent.Policy == GasRunPolicy::Triggered && Consent.Retire;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE RESIDENCY
//------------------------------------------------------------------------------------------------------------------------

// 📝 One domain's own clock. Not a scheduler and not an owner: it is advanced by whoever advances the field,
//    and answers two questions — should the solver run this tick, and may the storage go back.
struct GasDomainResidency
{
    GasPhase   Phase        = GasPhase::Dormant;        // [-]      - where it is in its life
    float      Elapsed      = 0.0f;                     // [s]      - inside the current phase
    float      Lived        = 0.0f;                     // [s]      - since it was ignited
    uint64_t   HeldBytes    = 0ull;                     // [bytes]  - what it is charged, 0 unless live
    GasQuality Quality      = GasQuality::Flipbook;     // [-]      - the rung it was granted
    uint32_t   Ignitions    = 0u;                       // [-]      - how many times it has been fired
};


/// 📦 Fire a dormant domain, charging it the storage of the rung it was granted.
/// in    Residency   [-]  the domain's clock, written in place
/// in    Quality     [-]  the rung the governor assigned this frame
/// out   bool        [-]  false when it was already live, in which case nothing changes
/// note  re-igniting a burning domain is a no-op rather than a restart: two triggers on one frame is an
///       ordinary thing for gameplay to do, and restarting would make the second one cancel the first
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline bool Ignite(GasDomainResidency& Residency, GasQuality Quality) noexcept
{
    if (Residency.Phase == GasPhase::Burning || Residency.Phase == GasPhase::Fading) return false;
    Residency.Phase     = GasPhase::Burning;
    Residency.Elapsed   = 0.0f;
    Residency.Lived     = 0.0f;
    Residency.Quality   = Quality;
    Residency.HeldBytes = DeviceBytesFor(Quality);
    Residency.Ignitions += 1u;
    return true;
}


/// 📦 Advance one domain's clock by one solver step and move it between phases.
/// in    Residency      [-]       the domain's clock, written in place
/// in    Consent        [-]       its run policy
/// in    StepSeconds    [s]       how much time this advance covered
/// in    PeakSmoke      [density] the largest remaining smoke reading in the field, for the fade test
/// in    ViewerDistance [m]       for a proximity domain; ignored otherwise
/// out   GasPhase       [-]       the phase after the step
/// note  a non-retiring one-shot returns to Dormant still holding its bytes — that is the whole difference,
///       and it is the choice the card makes you make deliberately
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline GasPhase Advance(GasDomainResidency& Residency, const GasResidencyConsent& Consent,
                        float StepSeconds, float PeakSmoke, float ViewerDistance) noexcept
{
    if (StepSeconds < 0.0f) StepSeconds = 0.0f;

    // Always-on and proximity domains are not one-shots and have no finish to reach; proximity simply stops
    //    being charged when the viewer leaves, which is a release like any other.
    if (Consent.Policy == GasRunPolicy::Always)
    {
        if (Residency.Phase != GasPhase::Burning) Ignite(Residency, Residency.Quality);
        Residency.Elapsed += StepSeconds;
        Residency.Lived   += StepSeconds;
        return Residency.Phase;
    }
    if (Consent.Policy == GasRunPolicy::Proximity)
    {
        const bool Inside = ViewerDistance < Consent.ProximityRange;
        if (Inside && Residency.Phase != GasPhase::Burning) Ignite(Residency, Residency.Quality);
        if (!Inside && Residency.Phase == GasPhase::Burning)
        {
            Residency.Phase     = GasPhase::Released;
            Residency.Elapsed   = 0.0f;
            Residency.HeldBytes = 0ull;
            return Residency.Phase;
        }
        Residency.Elapsed += StepSeconds;
        Residency.Lived   += StepSeconds;
        return Residency.Phase;
    }

    if (Residency.Phase == GasPhase::Dormant || Residency.Phase == GasPhase::Released) return Residency.Phase;

    Residency.Elapsed += StepSeconds;
    Residency.Lived   += StepSeconds;

    if (Residency.Phase == GasPhase::Burning)
    {
        if (Residency.Elapsed < Consent.Lifetime) return Residency.Phase;
        Residency.Phase   = GasPhase::Fading;     // [-] emission off, solver still advancing
        Residency.Elapsed = 0.0f;
        return Residency.Phase;
    }

    // Fading. Out on the measurement, or on the ceiling if the field never settles — a domain that fades
    //    forever is a bug somewhere else, and holding its storage while that bug is found helps nobody.
    const bool Faint  = PeakSmoke <= Consent.FaintestVisible;
    const bool Stuck  = Residency.Elapsed >= Consent.FadeCeiling;
    if (!Faint && !Stuck) return Residency.Phase;

    if (GasRetires(Consent))
    {
        Residency.Phase     = GasPhase::Released;
        Residency.HeldBytes = 0ull;   // 🔴 the bytes are back in the budget on this tick, not the next frame
    }
    else
    {
        Residency.Phase = GasPhase::Dormant;   // kept resident on purpose; still charged
    }
    Residency.Elapsed = 0.0f;
    return Residency.Phase;
}


/// 📦 May the owner free this domain's fields?
/// in    Residency   [-]  the domain's clock
/// out   bool        [-]  true once it has been released and is holding nothing
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline bool Retired(const GasDomainResidency& Residency) noexcept
{
    return Residency.Phase == GasPhase::Released && Residency.HeldBytes == 0ull;
}


/// 📦 Does the solver advance this domain this tick?
/// in    Residency   [-]  the domain's clock
/// out   bool        [-]  true while burning or fading
/// note  a fading domain still advances. That is the point of the phase
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline bool Simulating(const GasDomainResidency& Residency) noexcept
{
    return Residency.Phase == GasPhase::Burning || Residency.Phase == GasPhase::Fading;
}


/// 📦 Does this domain still inject at its emitters this tick?
/// in    Residency   [-]  the domain's clock
/// out   bool        [-]  true only while burning
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline bool Emitting(const GasDomainResidency& Residency) noexcept
{
    return Residency.Phase == GasPhase::Burning;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE LIVE OCCUPANCY
//------------------------------------------------------------------------------------------------------------------------

struct GasOccupancy
{
    uint64_t HeldBytes    = 0ull;   // [bytes] - charged across every live domain
    uint32_t LiveDomains  = 0u;     // [-]     - burning or fading
    uint32_t Retiring     = 0u;     // [-]     - fading; their bytes are about to come back
    uint64_t ReturningSoon = 0ull;  // [bytes] - what the fading ones will hand back
    bool     OverCeiling  = false;  // [-]     - something has to be demoted or refused
};


/// 📦 What the live set currently costs, and what is about to be handed back.
/// in    Residencies   [-]  array of domain clocks
/// in    Count         [-]  how many
/// in    Ceiling       [-]  the budget from GasQualityAllowance.h
/// out   GasOccupancy  [-]  the summed charge
/// note  ReturningSoon is why a one-shot corridor works at all: the governor can admit a new domain against
///       storage that is three tenths of a second from being free, rather than refusing it outright
/// cost  ✔️ linear in Count
/// tag   api, nonallocating, nonthrowing
inline GasOccupancy ResolveOccupancy(const GasDomainResidency* Residencies, uint32_t Count,
                                     const GasBudgetCeiling& Ceiling) noexcept
{
    GasOccupancy Occupancy;
    if (Residencies == nullptr) return Occupancy;
    for (uint32_t Index = 0u; Index < Count; ++Index)
    {
        const GasDomainResidency& One = Residencies[Index];
        if (!Simulating(One)) continue;
        Occupancy.HeldBytes   += One.HeldBytes;
        Occupancy.LiveDomains += 1u;
        if (One.Phase == GasPhase::Fading)
        {
            Occupancy.Retiring      += 1u;
            Occupancy.ReturningSoon += One.HeldBytes;
        }
    }
    Occupancy.OverCeiling = Occupancy.HeldBytes > Ceiling.DeviceBytes ||
                            Occupancy.LiveDomains > Ceiling.DomainLimit;
    return Occupancy;
}


/// 📦 May one more domain be ignited at this rung right now?
/// in    Occupancy   [-]  the current live charge
/// in    Quality     [-]  the rung the newcomer wants
/// in    Ceiling     [-]  the budget
/// out   bool        [-]  true when it fits without counting on anything retiring
/// note  deliberately does NOT count ReturningSoon. Admitting against storage that is only probably about to
///       be free is how a budget becomes a suggestion; a refused domain is demoted a rung and admitted again
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline bool Admits(const GasOccupancy& Occupancy, GasQuality Quality, const GasBudgetCeiling& Ceiling) noexcept
{
    if (Quality == GasQuality::Flipbook) return true;   // costs no live storage; that is the rung's purpose
    if (Occupancy.LiveDomains + 1u > Ceiling.DomainLimit) return false;
    return Occupancy.HeldBytes + DeviceBytesFor(Quality) <= Ceiling.DeviceBytes;
}

}   // namespace Frontier
