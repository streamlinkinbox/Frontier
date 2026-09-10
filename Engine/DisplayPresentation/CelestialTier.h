//============================================================================================================================================
// 📦 Engine/DisplayPresentation/CelestialTier.h — one place that turns a quality tier into celestial settings
//============================================================================================================================================
// Celestial port, step 9.
//
// The budgets have lived in FidelityClassifier since step 0 and every caller has been assembling them by hand:
//    the sky proof copies AtmosphereSampleCount and AtmosphereLightSampleCount into its own struct, the
//    volumetrics proof copies five more into a VolumetricBudget, and each does it slightly differently. That is
//    the shape a ladder rots in — not because any one copy is wrong, but because the next system to need a
//    budget will copy whichever call site it happens to read, and the two drift from there.
//
// So the translation lives here, once. `FidelityClassifier` owns the NUMBERS; this owns the MAPPING from those
//    numbers onto the settings structs the simulation actually takes. Nothing else may do either.
//
// ⚠️ THE AUTO LADDER IS DELIBERATELY NOT A FEATURE OF THE RENDERER. It walks the tier list to hold a target
//    frame rate, which means it is a closed loop over a measurement — and a closed loop over a WRONG measurement
//    is worse than no loop at all. Round 10's timestamp work is what makes the measurement trustworthy
//    (References/GpuTimestamps.md): the shadow and ReSTIR spans are read WITH_AVAILABILITY so a stage that did
//    not run cannot poison the figure, and sky/volumetrics spans are reserved for the same treatment. Until the
//    engine runs on real hardware those spans report nothing, so `AutoTierLadder` below is written and tested
//    against SUPPLIED frame times and is not wired to a clock anywhere.

#pragma once

#include "FidelityClassifier.h"
#include "VolumetricMedia.h"

#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                              TIER → CELESTIAL SETTINGS
//------------------------------------------------------------------------------------------------------------------------

// Everything the celestial systems need from a tier, in the shapes they actually take.
struct CelestialBudget
{
    VolumetricBudget Volumetrics{};       // clouds, fog, god rays
    uint32_t AtmosphereSamples      = 16u;
    uint32_t AtmosphereLightSamples = 6u;
    uint32_t StarLayers             = 3u;
    uint32_t StarSuperSamples       = 1u;
    float    CloudResolutionScale   = 0.5f;
    uint32_t ParticleCapacity       = 8192u;   // the precipitation pool
};

class CelestialTier
{
public:
    // The ONLY translation from a tier to celestial settings. Every call site takes this rather than reading
    //    FidelityCriteria field by field — `CheckCelestialTiers.sh` enforces that.
    [[nodiscard]] static CelestialBudget BudgetFor(const FidelityCriteria& Criteria) noexcept
    {
        CelestialBudget Budget{};
        Budget.Volumetrics.CloudSteps     = Criteria.CloudMarchStepCount;
        Budget.Volumetrics.LocalSteps     = Criteria.LocalVolumeStepCount;
        Budget.Volumetrics.LightTaps      = Criteria.CloudLightTapCount;
        Budget.Volumetrics.CoverageMargin = Criteria.CloudCoverageMargin;

        Budget.AtmosphereSamples      = Criteria.AtmosphereSampleCount;
        Budget.AtmosphereLightSamples = Criteria.AtmosphereLightSampleCount;
        Budget.StarLayers             = Criteria.StarLayerCount;
        Budget.StarSuperSamples       = Criteria.StarSuperSampleCount;
        Budget.CloudResolutionScale   = Criteria.CloudResolutionScale;
        Budget.ParticleCapacity       = Criteria.ParticleSimulationCapacity;
        return Budget;
    }

    // A single scalar for "how much work is this tier asking for", used by the Auto ladder to reason about
    //    relative cost without pretending to know milliseconds. It is a weighted count of the dominant loops:
    //    the cloud march and its per-step light taps dominate, the atmosphere integral is next, and the shaft
    //    term rides on the march.
    //
    //    ⚠️ This is an ORDERING, not a prediction. It cannot say a tier will cost 8 ms; it can say Ultra asks
    //    for more than Standard, which is all the ladder needs to step in the right direction.
    [[nodiscard]] static float RelativeCost(const CelestialBudget& Budget) noexcept
    {
        const float March = static_cast<float>(Budget.Volumetrics.CloudSteps)
                          * (1.0f + static_cast<float>(Budget.Volumetrics.LightTaps) * 0.5f);
        const float Local = static_cast<float>(Budget.Volumetrics.LocalSteps) * 0.6f;
        const float Sky   = static_cast<float>(Budget.AtmosphereSamples)
                          * static_cast<float>(Budget.AtmosphereLightSamples) * 0.05f;
        const float Stars = static_cast<float>(Budget.StarLayers)
                          * static_cast<float>(Budget.StarSuperSamples) * 0.4f;
        return March + Local + Sky + Stars;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE AUTO LADDER
//------------------------------------------------------------------------------------------------------------------------

// Holds a target frame rate by walking the tier list. Kept as a small state machine over SUPPLIED frame times so
//    it can be exercised without a GPU, and so the thing being tested is the control law rather than a clock.
//
// The three behaviours that matter, and that a naive implementation gets wrong:
//    · it must not oscillate. A ladder that steps down on one slow frame and back up on the next will do that
//      forever, and the flicker is far more objectionable than either tier.
//    · it must settle. Given a steady frame time it has to stop moving, not hunt around the boundary.
//    · it must respond faster to being too slow than to being too fast. Dropping frames is a fault; having
//      headroom is not, so the ladder climbs cautiously and falls promptly.
struct AutoTierSettings
{
    bool     Enabled        = false;   // opt-in, always
    float    TargetFps      = 45.0f;
    float    IntervalSeconds= 1.5f;    // how often a decision may be taken
    float    DropBelow      = 0.85f;   // fraction of target that triggers a step down
    float    RiseAbove      = 1.35f;   // fraction of target required to step up — deliberately wider
};

class AutoTierLadder
{
public:
    void Reset(FidelityCategory Start) noexcept
    {
        Current = Start;
        Elapsed = 0.0f;
        Consecutive = 0;
        LastDirection = 0;
    }

    [[nodiscard]] FidelityCategory Tier() const noexcept { return Current; }
    [[nodiscard]] uint32_t Changes() const noexcept { return ChangeCount; }

    // Feed one frame. Returns true when the tier changed.
    bool Observe(const AutoTierSettings& Settings, float FramesPerSecond, float DeltaSeconds) noexcept
    {
        if (!Settings.Enabled) return false;
        Elapsed += DeltaSeconds;
        if (Elapsed < Settings.IntervalSeconds) return false;
        Elapsed = 0.0f;

        const float Low  = Settings.TargetFps * Settings.DropBelow;
        const float High = Settings.TargetFps * Settings.RiseAbove;

        int Direction = 0;
        if (FramesPerSecond < Low)       Direction = -1;
        else if (FramesPerSecond > High) Direction = +1;
        if (Direction == 0) { Consecutive = 0; LastDirection = 0; return false; }

        // Hysteresis. A step UP must be earned by two consecutive intervals of headroom, because climbing into a
        //    tier the machine cannot hold is what starts an oscillation: the next interval drops straight back
        //    and the pair repeats forever. Falling is allowed immediately — dropped frames are a fault to fix
        //    now, not to confirm.
        if (Direction == +1)
        {
            Consecutive = (LastDirection == +1) ? Consecutive + 1 : 1;
            LastDirection = +1;
            if (Consecutive < 2) return false;
        }
        else
        {
            Consecutive = 0;
            LastDirection = -1;
        }

        const int Index = IndexOf(Current) + Direction;
        if (Index < 0 || Index >= static_cast<int>(kTierCount)) return false;
        Current = kOrder[Index];
        Consecutive = 0;
        ++ChangeCount;
        return true;
    }

    static constexpr uint32_t kTierCount = 5u;
    static constexpr FidelityCategory kOrder[kTierCount] = {
        FidelityCategory::MinimalFidelity, FidelityCategory::EconomyFidelity,
        FidelityCategory::StandardFidelity, FidelityCategory::UltraFidelity,
        FidelityCategory::ReferenceFidelity };

    [[nodiscard]] static int IndexOf(FidelityCategory Category) noexcept
    {
        for (uint32_t I = 0; I < kTierCount; ++I) if (kOrder[I] == Category) return static_cast<int>(I);
        return 2;   // Standard, the sane default if an unknown tier ever arrives
    }

private:
    FidelityCategory Current = FidelityCategory::StandardFidelity;
    float    Elapsed     = 0.0f;
    int      Consecutive = 0;
    int      LastDirection = 0;
    uint32_t ChangeCount = 0u;
};

} // namespace Frontier
