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
    EconomicFidelity                    = 0,                    // Lowest hardware load, aggressive culling
    StandardFidelity                    = 1,                    // Balanced baseline fidelity
    PerformanceFidelity                 = 2,                    // High-framerate competitive profile
    UltraFidelity                       = 3                     // Maximum visual realism, full ReSTIR GI
};

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
    bool                    GlobalIlluminationEnabled;          // [bool] indirect radiosity ReSTIR GI
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
