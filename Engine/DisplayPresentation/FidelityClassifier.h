//============================================================================================================================================
// 📦 Frontier/DisplayPresentation/FidelityClassifier.h — Graphics Quality Profiles and Scalability Criteria
//============================================================================================================================================

#pragma once

#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                  FIDELITY CATEGORY
//------------------------------------------------------------------------------------------------------------------------

enum class FidelityCategory : uint32_t
{
    MinimalFidelity                     = 0,                    // Lowest hardware load: half resolution, 1 candidate, no GI, no AA
    EconomyFidelity                     = 1,                    // Light load: 3/4 resolution, 2 candidates, no GI
    StandardFidelity                    = 2,                    // Balanced baseline: full resolution, 4 candidates, GI on
    UltraFidelity                       = 3,                    // High: 8 candidates, 3 spatial passes, GI on
    ReferenceFidelity                   = 4,                    // Offline-grade: 16 candidates, 4 spatial passes, everything on
    Count                               = 5
};

// The tiers form a strict ladder; the Control Centre quality tile advances through them with each tap and wraps.
[[nodiscard]] constexpr FidelityCategory NextFidelity(FidelityCategory Category) noexcept
{
    const uint32_t Ordinal = static_cast<uint32_t>(Category) + 1u;
    return static_cast<FidelityCategory>(Ordinal % static_cast<uint32_t>(FidelityCategory::Count));
}

[[nodiscard]] constexpr const char* FidelityLabel(FidelityCategory Category) noexcept
{
    switch (Category)
    {
        case FidelityCategory::MinimalFidelity:   return "Minimal";
        case FidelityCategory::EconomyFidelity:   return "Economy";
        case FidelityCategory::StandardFidelity:  return "Standard";
        case FidelityCategory::UltraFidelity:     return "Ultra";
        case FidelityCategory::ReferenceFidelity: return "Reference";
        default:                                  return "Standard";
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                FIDELITY CRITERIA
//------------------------------------------------------------------------------------------------------------------------

struct FidelityCriteria
{
    FidelityCategory        Category;                           // [category] active graphics quality rank
    float                   ResolutionScale;                    // [0..1] internal render scale factor
    uint32_t                ReSTIRCandidateSampleCount;         // [count] ReSTIR initial sample count M0
    uint32_t                AtmosphereRaymarchStepCount;        // [steps] volumetric media sample count
    uint32_t                FluidVoxelGridResolution;           // [cells] 3D fluid domain resolution
    uint32_t                ParticleSimulationCapacity;         // [count] maximum active compute particles
    uint32_t                ReSTIRExtraCandidateCount;        // [count] extra same-pixel RIS candidates (R6 row 3: renamed; true spatial reuse is fixed)
    bool                    GlobalIlluminationEnabled;          // [bool] indirect radiosity ReSTIR GI
    bool                    AntiAliasingEnabled;                // [bool] sub-pixel jitter + temporal accumulation
    bool                    HardwareRayQueryEnabled;            // [bool] hardware ray tracing acceleration
};

//------------------------------------------------------------------------------------------------------------------------
//                                                FIDELITY CLASSIFIER
//------------------------------------------------------------------------------------------------------------------------

class FidelityClassifier
{
public:
    FidelityClassifier() noexcept;
    ~FidelityClassifier() noexcept = default;

    [[nodiscard]] FidelityCriteria ConstructCriteria(FidelityCategory Category) const noexcept;
    void                    AssignCategory(FidelityCategory NewCategory) noexcept { ActiveCategory = NewCategory; }
    void                    AdvanceCategory() noexcept { ActiveCategory = NextFidelity(ActiveCategory); }

    [[nodiscard]] FidelityCategory QueryCategory() const noexcept { return ActiveCategory; }
    [[nodiscard]] FidelityCriteria QueryActiveCriteria() const noexcept { return ConstructCriteria(ActiveCategory); }

    // Single unified conversion operator for active criteria
    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

private:
    FidelityCategory        ActiveCategory;                     // [category] active profile setting
};

template<>
inline FidelityCriteria FidelityClassifier::Convert<FidelityCriteria>() const noexcept
{
    return QueryActiveCriteria();
}

template<>
inline FidelityCategory FidelityClassifier::Convert<FidelityCategory>() const noexcept
{
    return ActiveCategory;
}

} // namespace Frontier
