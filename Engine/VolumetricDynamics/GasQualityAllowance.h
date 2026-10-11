//==============================================================================================================================================
//                                                         GASQUALITYALLOWANCE.H
//==============================================================================================================================================
// 📦 What one gas domain is allowed to spend — lattice extent, pressure sweeps, step rate, device bytes — and the governor that assigns it.
//
// The five allowances are the quality ladder of the gas port. A domain is placed on one of them by distance and
//    by what the ceilings have left, and the allowance fixes four numbers at once because they are not
//    independently choosable: the pressure solve scales with the cube of the extent and linearly with both the
//    sweep count and the step rate, so loosening one while tightening another produces a cost nobody predicted.
//
// 📐 Live simulation storage is 47 bytes per voxel, and that figure is structural rather than measured:
//        velocity  RGBA16F × 2 (current and the advected copy)   16 B
//        thermo    RGBA16F × 2 (density, temperature, fuel, soot) 16 B
//        pressure  R16F    × 2 (Jacobi reads one and writes one)   4 B
//        divergence R16F                                           2 B
//        vorticity RGB16F                                          6 B
//        obstacle  R8                                              1 B
//    Double-storage on velocity, thermo and pressure is not waste: semi-Lagrangian advection and Jacobi both
//    read the whole neighbourhood of the previous reading while writing the next, so writing in place corrupts
//    the samples still to be read.
//
// ⚠️ STEP RATE IS NOT DISPLAY RATE, AND CONFLATING THEM IS THE MISTAKE THIS HEADER EXISTS TO PREVENT.
//    Every domain is drawn on every displayed image. A Mid domain advances its solver thirty times a second and
//    the renderer interpolates between the two newest readings for the images in between. Halving the step rate
//    halves the pressure solve and costs no smoothness, which is where nearly all of the saving lives.
//
// 🔴 THE PER-SECOND CONVERSION BELOW IS A CORRECTNESS RULE, NOT A CONVENIENCE.
//    Advancing at 30 Hz means doubling Δτ. Applying a dissipation written per step then removes half as much
//    smoke per second as the same preset advanced at 60 Hz, so a distant plume would linger roughly twice as
//    long as a near one authored identically. Every loss-like setting is therefore declared per second and
//    converted through RemainingAfter(); nothing in the solver may apply a per-step loss directly.
//
// Header-only and dependency-free for the same reason WindField.h is: these numbers are read by the solver, by
//    the editor readout, by the headless checks and eventually by a shader transcription, and none of those
//    share a binary. One definition is what stops the copies drifting.

#pragma once

#include <cmath>
#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE QUALITY LADDER
//------------------------------------------------------------------------------------------------------------------------

// 📝 Spelled Quality rather than by a rank word, because the rung states what the viewer gets, not where it sits
//    in a list. Flipbook is a rung of the same ladder and not a separate mechanism: a domain demoted past Far
//    stops simulating and plays a pre-advanced sheet, which is the whole reason the ladder ends somewhere.
enum class GasQuality : uint32_t
{
    Hero     = 0u,   // [-] - the one effect the player is standing in
    Near     = 1u,   // [-] - foreground, still reacting to the world
    Mid      = 2u,   // [-] - mid-ground, advanced at half rate
    Far      = 3u,   // [-] - distant, advanced at quarter rate
    Flipbook = 4u,   // [-] - no solver at all; a pre-advanced sheet shared by every instance
};

constexpr uint32_t GasQualityCount = 5u;

// Bytes of device storage consumed by one voxel of a live domain. Derived in the file header; changing a channel
//    layout means changing this number in the same commit, because the governor trusts it completely.
constexpr uint32_t GasBytesPerVoxel = 47u;

// The master advance rate. Every allowance advances on an integer division of this one clock, which is what lets
//    two domains on different rungs share a timeline, and what keeps the coarse reading reproducible.
constexpr float GasMasterHertz = 60.0f;


struct GasAllowance
{
    uint32_t LatticeExtent    = 0u;      // [voxels] - cubic; 0 means this rung does not simulate
    uint32_t PressureSweeps   = 0u;      // [-]      - Jacobi iterations per advance
    uint32_t StepDivision     = 1u;      // [-]      - advances once every N ticks of GasMasterHertz
    uint32_t ConcurrentLimit  = 0u;      // [-]      - how many domains may hold this rung at once
    const char* Readout       = "";      // [-]      - what the editor shows beside the domain
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE ALLOWANCES
//------------------------------------------------------------------------------------------------------------------------

/// 📦 The four numbers one rung of the ladder is permitted to spend.
/// in    Quality        [-]  which rung
/// out   GasAllowance   [-]  extent, sweeps, step division and concurrency for that rung
/// err   an unrecognised rung yields the Flipbook allowance, which costs nothing and cannot overrun a ceiling
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline GasAllowance AllowanceFor(GasQuality Quality) noexcept
{
    switch (Quality)
    {
        case GasQuality::Hero:     return { 128u, 24u, 1u, 1u, "Hero - 128 cubed, full rate" };
        case GasQuality::Near:     return {  96u, 18u, 1u, 2u, "Near - 96 cubed, full rate"  };
        case GasQuality::Mid:      return {  64u, 12u, 2u, 4u, "Mid - 64 cubed, half rate"   };
        case GasQuality::Far:      return {  32u,  8u, 4u, 8u, "Far - 32 cubed, quarter rate" };
        case GasQuality::Flipbook: return {   0u,  0u, 0u, 0u, "Flipbook - sheet playback"   };
    }
    return { 0u, 0u, 0u, 0u, "Flipbook - sheet playback" };
}


/// 📦 Device storage one domain on this rung occupies, from the voxel count and the channel layout.
/// in    Quality    [-]      which rung
/// out   uint64_t   [bytes]  0 for Flipbook, whose sheet is shared and therefore not charged per domain
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline uint64_t DeviceBytesFor(GasQuality Quality) noexcept
{
    const uint64_t Extent = AllowanceFor(Quality).LatticeExtent;
    return Extent * Extent * Extent * GasBytesPerVoxel;
}


/// 📦 Voxel operations one advance of this rung costs, which is what the frame actually pays.
/// in    Quality    [-]  which rung
/// out   uint64_t   [-]  voxels multiplied by the sweep count; advection and the thermo stages ride along with it
/// note  this is the number that scales with the cube, and the reason Hero is limited to one domain
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline uint64_t SolveCostFor(GasQuality Quality) noexcept
{
    const GasAllowance Allowance = AllowanceFor(Quality);
    const uint64_t Extent = Allowance.LatticeExtent;
    return Extent * Extent * Extent * Allowance.PressureSweeps;
}


/// 📦 Seconds between two advances of this rung.
/// in    Quality   [-]  which rung
/// out   float     [s]  Δτ to advance the solver with; 0 for Flipbook, which never advances
/// err   returns 0 rather than dividing by zero when the rung does not simulate
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline float StepIntervalFor(GasQuality Quality) noexcept
{
    const uint32_t Division = AllowanceFor(Quality).StepDivision;
    if (Division == 0u) return 0.0f;
    return static_cast<float>(Division) / GasMasterHertz;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              PER-SECOND TO PER-STEP CONVERSION
//------------------------------------------------------------------------------------------------------------------------

/// 📦 Fraction of a quantity surviving one advance, from a loss declared per second.
/// in    LossPerSecond   [1/s]  fraction removed in one second; clamped to [0,1]
/// in    StepInterval    [s]    Δτ of the advance about to be taken
/// out   float           [-]    multiplier to apply once, this advance
/// use   smoke dissipation, temperature cooling, velocity damping and burn rate — every loss in the solver
/// err   a non-positive interval removes nothing, so an unadvanced domain never decays
/// note  🔴 the solver must never apply LossPerSecond directly; that is the bug described in the file header
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline float RemainingAfter(float LossPerSecond, float StepInterval) noexcept
{
    if (StepInterval <= 0.0f)   return 1.0f;
    if (LossPerSecond <= 0.0f)  return 1.0f;
    if (LossPerSecond >= 1.0f)  return 0.0f;
    return std::pow(1.0f - LossPerSecond, StepInterval);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE CEILINGS
//------------------------------------------------------------------------------------------------------------------------

// ⚠️ These are a proposal pinned to no particular hardware yet. They are declared here, mutable by the project,
//    so that overrunning them is a visible configuration decision rather than a frame-rate mystery.
struct GasBudgetCeiling
{
    uint64_t DeviceBytes       = 256ull * 1024ull * 1024ull;   // [bytes] - total live gas storage permitted
    uint64_t SolveCostCeiling  = 120ull * 1000ull * 1000ull;   // [-]     - summed voxel operations per master tick
    uint32_t DomainLimit       = 12u;                          // [-]     - live domains, flipbooks excluded
};


// One domain as the governor sees it. Distance is the only ranking input, deliberately: an importance score
//    would have to be authored on every emitter and would then disagree with what the player is looking at.
struct GasDomainClaim
{
    float      ViewerDistance   = 0.0f;                    // [m] - domain centre to camera
    GasQuality Requested        = GasQuality::Mid;         // [-] - what distance alone would grant
    GasQuality Assigned         = GasQuality::Flipbook;    // [-] - written by AssignQuality
    bool       DemotionApplied  = false;                   // [-] - true when a ceiling, not distance, decided
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     DISTANCE RANKING
//------------------------------------------------------------------------------------------------------------------------

/// 📦 The rung distance alone would grant, before any ceiling is consulted.
/// in    ViewerDistance   [m]  domain centre to camera
/// out   GasQuality       [-]  Hero inside 8 m, Near to 25 m, Mid to 60 m, Far to 150 m, Flipbook beyond
/// note  the thresholds are world-scale assumptions and belong beside the ceilings when those are pinned
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline GasQuality QualityForDistance(float ViewerDistance) noexcept
{
    if (ViewerDistance <   8.0f) return GasQuality::Hero;
    if (ViewerDistance <  25.0f) return GasQuality::Near;
    if (ViewerDistance <  60.0f) return GasQuality::Mid;
    if (ViewerDistance < 150.0f) return GasQuality::Far;
    return GasQuality::Flipbook;
}


/// 📦 One rung down the ladder; Flipbook is the floor and stays there.
/// in    Quality      [-]  current rung
/// out   GasQuality   [-]  the next cheaper rung
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline GasQuality Demoted(GasQuality Quality) noexcept
{
    const uint32_t Rung = static_cast<uint32_t>(Quality);
    return Rung + 1u >= GasQualityCount ? GasQuality::Flipbook : static_cast<GasQuality>(Rung + 1u);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE GOVERNOR
//------------------------------------------------------------------------------------------------------------------------

/// 📦 Assigns every claim a rung that distance would grant, then demotes the furthest claims until all three
///    ceilings hold. Nothing is ever dropped silently: a demoted claim records that a ceiling, not distance,
///    decided its rung, which is what the editor shows beside the domain.
/// in    Claims        [-]  claims with ViewerDistance filled in; Assigned and DemotionApplied are overwritten
/// in    ClaimCount    [-]  how many
/// in    Ceiling       [-]  the three limits to respect
/// out   bool          [-]  true when every ceiling holds on return
/// err   returns false only when every claim has already reached Flipbook and a ceiling is still exceeded,
///       which can happen solely if DomainLimit is below the count of claims
/// note  demotion walks from the furthest claim inward, so the domain the player is looking at is the last to
///       lose detail and the first to regain it
/// cost  🚩  O(ClaimCount²) worst case, over a list bounded by DomainLimit
/// tag   api, nonallocating, nonthrowing
inline bool AssignQuality(GasDomainClaim* Claims, uint32_t ClaimCount, const GasBudgetCeiling& Ceiling) noexcept
{
    for (uint32_t Slot = 0u; Slot < ClaimCount; ++Slot)
    {
        Claims[Slot].Requested       = QualityForDistance(Claims[Slot].ViewerDistance);
        Claims[Slot].Assigned        = Claims[Slot].Requested;
        Claims[Slot].DemotionApplied = false;
    }

    // 📝 Bounded by the total number of demotions available — every claim can fall at most GasQualityCount - 1
    //    rungs — so the walk terminates without needing a convergence argument.
    for (uint32_t Attempt = 0u; Attempt <= ClaimCount * GasQualityCount; ++Attempt)
    {
        uint64_t TotalBytes = 0ull;
        uint64_t TotalSolve = 0ull;
        uint32_t LiveCount  = 0u;
        for (uint32_t Slot = 0u; Slot < ClaimCount; ++Slot)
        {
            if (Claims[Slot].Assigned == GasQuality::Flipbook) continue;
            TotalBytes += DeviceBytesFor(Claims[Slot].Assigned);
            TotalSolve += SolveCostFor(Claims[Slot].Assigned) / AllowanceFor(Claims[Slot].Assigned).StepDivision;
            ++LiveCount;
        }

        uint32_t RungCount[GasQualityCount] = { 0u, 0u, 0u, 0u, 0u };
        for (uint32_t Slot = 0u; Slot < ClaimCount; ++Slot)
            ++RungCount[static_cast<uint32_t>(Claims[Slot].Assigned)];

        bool ConcurrencyExceeded = false;
        for (uint32_t Rung = 0u; Rung + 1u < GasQualityCount; ++Rung)
            if (RungCount[Rung] > AllowanceFor(static_cast<GasQuality>(Rung)).ConcurrentLimit)
                ConcurrencyExceeded = true;

        const bool CeilingsHold = TotalBytes <= Ceiling.DeviceBytes
                               && TotalSolve <= Ceiling.SolveCostCeiling
                               && LiveCount  <= Ceiling.DomainLimit
                               && !ConcurrencyExceeded;
        if (CeilingsHold) return true;

        // Furthest simulating claim loses one rung. Ties break toward the later slot, which is arbitrary but
        //    fixed, so two runs of the same scene demote the same domain.
        uint32_t Furthest = ClaimCount;
        for (uint32_t Slot = 0u; Slot < ClaimCount; ++Slot)
        {
            if (Claims[Slot].Assigned == GasQuality::Flipbook) continue;
            if (Furthest == ClaimCount || Claims[Slot].ViewerDistance >= Claims[Furthest].ViewerDistance)
                Furthest = Slot;
        }
        if (Furthest == ClaimCount) return false;   // everything is already a sheet and a ceiling still fails

        Claims[Furthest].Assigned        = Demoted(Claims[Furthest].Assigned);
        Claims[Furthest].DemotionApplied = true;
    }
    return false;
}


/// 📦 Why a domain is on the rung it is on, in the words the editor puts beside it.
/// in    Claim        [-]  a claim already passed through AssignQuality
/// out   const char*  [-]  a static sentence; never null, never owned by the caller
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
inline const char* AssignmentReason(const GasDomainClaim& Claim) noexcept
{
    if (Claim.Assigned == Claim.Requested)        return "distance";
    if (Claim.Assigned == GasQuality::Flipbook)   return "budget exhausted - playing a sheet";
    return "demoted to stay inside the budget";
}

}   // namespace Frontier
