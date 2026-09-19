//============================================================================================================================================
// 📦 Frontier/DisplayPresentation/FidelityClassifier.cpp — Graphics Quality Scalability Implementation
//============================================================================================================================================

#include "FidelityClassifier.h"

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

FidelityClassifier::FidelityClassifier() noexcept
    : ActiveCategory(FidelityCategory::PerformanceFidelity)
{
}

//------------------------------------------------------------------------------------------------------------------------
//                                              CRITERIA CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

FidelityCriteria FidelityClassifier::ConstructCriteria(FidelityCategory Category) const noexcept
{
    FidelityCriteria Criteria{};
    Criteria.Category = Category;

    switch (Category)
    {
        case FidelityCategory::EconomicFidelity:
            Criteria.ResolutionScale             = 0.5f;
            Criteria.ReSTIRCandidateSampleCount  = 8;
            Criteria.AtmosphereRaymarchStepCount = 16;
            Criteria.FluidVoxelGridResolution    = 16;
            Criteria.ParticleSimulationCapacity  = 4096;
            Criteria.GlobalIlluminationEnabled   = false;
            Criteria.HardwareRayQueryEnabled     = false;
            break;

        case FidelityCategory::StandardFidelity:
            Criteria.ResolutionScale             = 0.75f;
            Criteria.ReSTIRCandidateSampleCount  = 16;
            Criteria.AtmosphereRaymarchStepCount = 32;
            Criteria.FluidVoxelGridResolution    = 32;
            Criteria.ParticleSimulationCapacity  = 8192;
            Criteria.GlobalIlluminationEnabled   = false;
            Criteria.HardwareRayQueryEnabled     = false;
            break;

        case FidelityCategory::PerformanceFidelity:
            Criteria.ResolutionScale             = 1.0f;
            Criteria.ReSTIRCandidateSampleCount  = 32;
            Criteria.AtmosphereRaymarchStepCount = 48;
            Criteria.FluidVoxelGridResolution    = 48;
            Criteria.ParticleSimulationCapacity  = 16384;
            Criteria.GlobalIlluminationEnabled   = true;
            Criteria.HardwareRayQueryEnabled     = true;
            break;

        case FidelityCategory::UltraFidelity:
        default:
            Criteria.ResolutionScale             = 1.0f;
            Criteria.ReSTIRCandidateSampleCount  = 64;
            Criteria.AtmosphereRaymarchStepCount = 96;
            Criteria.FluidVoxelGridResolution    = 64;
            Criteria.ParticleSimulationCapacity  = 65536;
            Criteria.GlobalIlluminationEnabled   = true;
            Criteria.HardwareRayQueryEnabled     = true;
            break;
    }

    return Criteria;
}

} // namespace Frontier
