//============================================================================================================================================
// 📦 Frontier/DisplayPresentation/FidelityClassifier.cpp — Graphics Quality Scalability Implementation
//============================================================================================================================================

#include "FidelityClassifier.h"

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

FidelityClassifier::FidelityClassifier() noexcept
    : ActiveCategory(FidelityCategory::StandardFidelity)
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
        case FidelityCategory::MinimalFidelity:
            Criteria.ResolutionScale             = 0.5f;
            Criteria.ReSTIRCandidateSampleCount  = 1;
            Criteria.ReSTIRExtraCandidateCount      = 0;
            Criteria.FluidVoxelGridResolution    = 16;
            Criteria.ParticleSimulationCapacity  = 2048;
            Criteria.ShadowTechnique             = ShadowTechniqueCategory::HardShadowMap;
            Criteria.ShadowMapSide               = 256;
            Criteria.ShadowFilterTapCount        = 1;
            Criteria.GlobalIlluminationEnabled   = false;
            Criteria.AntiAliasingEnabled         = false;
            Criteria.HardwareRayQueryEnabled     = false;
            break;

        case FidelityCategory::EconomyFidelity:
            Criteria.ResolutionScale             = 0.75f;
            Criteria.ReSTIRCandidateSampleCount  = 2;
            Criteria.ReSTIRExtraCandidateCount      = 1;
            Criteria.FluidVoxelGridResolution    = 24;
            Criteria.ParticleSimulationCapacity  = 4096;
            Criteria.ShadowTechnique             = ShadowTechniqueCategory::WidePercentageCloserFilter;
            Criteria.ShadowMapSide               = 512;
            Criteria.ShadowFilterTapCount        = 5;
            Criteria.GlobalIlluminationEnabled   = false;
            Criteria.AntiAliasingEnabled         = true;
            Criteria.HardwareRayQueryEnabled     = false;
            break;

        case FidelityCategory::StandardFidelity:
            Criteria.ResolutionScale             = 1.0f;
            Criteria.ReSTIRCandidateSampleCount  = 4;
            Criteria.ReSTIRExtraCandidateCount      = 2;
            Criteria.FluidVoxelGridResolution    = 32;
            Criteria.ParticleSimulationCapacity  = 8192;
            Criteria.ShadowTechnique             = ShadowTechniqueCategory::PercentageCloserSoftShadow;
            Criteria.ShadowMapSide               = 512;
            Criteria.ShadowFilterTapCount        = 5;
            Criteria.GlobalIlluminationEnabled   = true;
            Criteria.AntiAliasingEnabled         = true;
            Criteria.HardwareRayQueryEnabled     = false;
            break;

        case FidelityCategory::UltraFidelity:
            Criteria.ResolutionScale             = 1.0f;
            Criteria.ReSTIRCandidateSampleCount  = 8;
            Criteria.ReSTIRExtraCandidateCount      = 3;
            Criteria.FluidVoxelGridResolution    = 48;
            Criteria.ParticleSimulationCapacity  = 16384;
            Criteria.ShadowTechnique             = ShadowTechniqueCategory::PercentageCloserSoftShadow;
            Criteria.ShadowMapSide               = 1024;
            Criteria.ShadowFilterTapCount        = 7;
            Criteria.GlobalIlluminationEnabled   = true;
            Criteria.AntiAliasingEnabled         = true;
            Criteria.HardwareRayQueryEnabled     = true;
            break;

        case FidelityCategory::ReferenceFidelity:
        default:
            Criteria.ResolutionScale             = 1.0f;
            Criteria.ReSTIRCandidateSampleCount  = 16;
            Criteria.ReSTIRExtraCandidateCount      = 4;
            Criteria.FluidVoxelGridResolution    = 64;
            Criteria.ParticleSimulationCapacity  = 65536;
            Criteria.ShadowTechnique             = ShadowTechniqueCategory::PercentageCloserSoftShadow;
            Criteria.ShadowMapSide               = 2048;
            Criteria.ShadowFilterTapCount        = 9;
            Criteria.GlobalIlluminationEnabled   = true;
            Criteria.AntiAliasingEnabled         = true;
            Criteria.HardwareRayQueryEnabled     = true;
            break;
    }

    return Criteria;
}

} // namespace Frontier
