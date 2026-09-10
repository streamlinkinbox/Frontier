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
            Criteria.ReSTIRSpatialTapCount       = 0;
            Criteria.DenoiseLevelCount           = 4;
            Criteria.FluidVoxelGridResolution    = 16;
            Criteria.ParticleSimulationCapacity  = 2048;
            Criteria.ShadowTechnique             = ShadowTechniqueCategory::HardShadowMap;
            Criteria.ShadowMapSide               = 256;
            Criteria.ShadowFilterTapCount        = 1;
            Criteria.GlobalIlluminationEnabled   = false;
            Criteria.AntiAliasingEnabled         = false;
            Criteria.HardwareRayQueryEnabled     = false;
            Criteria.CloudMarchStepCount         = 16;
            Criteria.CloudLightTapCount          = 3;
            Criteria.LocalVolumeStepCount        = 12;
            Criteria.CloudResolutionScale        = 0.25f;
            Criteria.AtmosphereSampleCount       = 8;
            Criteria.AtmosphereLightSampleCount  = 3;
            Criteria.StarLayerCount              = 2;
            Criteria.StarSuperSampleCount        = 1;
            Criteria.CloudCoverageMargin         = 0.10f;
            break;

        case FidelityCategory::EconomyFidelity:
            Criteria.ResolutionScale             = 0.75f;
            Criteria.ReSTIRCandidateSampleCount  = 2;
            Criteria.ReSTIRExtraCandidateCount      = 1;
            Criteria.ReSTIRSpatialTapCount       = 1;
            Criteria.DenoiseLevelCount           = 4;
            Criteria.FluidVoxelGridResolution    = 24;
            Criteria.ParticleSimulationCapacity  = 4096;
            Criteria.ShadowTechnique             = ShadowTechniqueCategory::WidePercentageCloserFilter;
            Criteria.ShadowMapSide               = 512;
            Criteria.ShadowFilterTapCount        = 5;
            Criteria.GlobalIlluminationEnabled   = false;
            Criteria.AntiAliasingEnabled         = true;
            Criteria.HardwareRayQueryEnabled     = false;
            Criteria.CloudMarchStepCount         = 20;
            Criteria.CloudLightTapCount          = 3;
            Criteria.LocalVolumeStepCount        = 16;
            Criteria.CloudResolutionScale        = 0.50f;
            Criteria.AtmosphereSampleCount       = 12;
            Criteria.AtmosphereLightSampleCount  = 4;
            Criteria.StarLayerCount              = 3;
            Criteria.StarSuperSampleCount        = 1;
            Criteria.CloudCoverageMargin         = 0.06f;
            break;

        case FidelityCategory::StandardFidelity:
            Criteria.ResolutionScale             = 1.0f;
            Criteria.ReSTIRCandidateSampleCount  = 4;
            Criteria.ReSTIRExtraCandidateCount      = 2;
            Criteria.ReSTIRSpatialTapCount       = 2;
            Criteria.DenoiseLevelCount           = 4;
            Criteria.FluidVoxelGridResolution    = 32;
            Criteria.ParticleSimulationCapacity  = 8192;
            Criteria.ShadowTechnique             = ShadowTechniqueCategory::PercentageCloserSoftShadow;
            Criteria.ShadowMapSide               = 512;
            Criteria.ShadowFilterTapCount        = 5;
            Criteria.GlobalIlluminationEnabled   = true;
            Criteria.AntiAliasingEnabled         = true;
            Criteria.HardwareRayQueryEnabled     = false;
            Criteria.CloudMarchStepCount         = 28;
            Criteria.CloudLightTapCount          = 4;
            Criteria.LocalVolumeStepCount        = 28;
            Criteria.CloudResolutionScale        = 0.50f;
            Criteria.AtmosphereSampleCount       = 16;
            Criteria.AtmosphereLightSampleCount  = 6;
            Criteria.StarLayerCount              = 3;
            Criteria.StarSuperSampleCount        = 1;
            Criteria.CloudCoverageMargin         = 0.03f;
            break;

        case FidelityCategory::UltraFidelity:
            Criteria.ResolutionScale             = 1.0f;
            Criteria.ReSTIRCandidateSampleCount  = 8;
            Criteria.ReSTIRExtraCandidateCount      = 3;
            Criteria.ReSTIRSpatialTapCount       = 3;
            Criteria.DenoiseLevelCount           = 5;
            Criteria.FluidVoxelGridResolution    = 48;
            Criteria.ParticleSimulationCapacity  = 16384;
            Criteria.ShadowTechnique             = ShadowTechniqueCategory::PercentageCloserSoftShadow;
            Criteria.ShadowMapSide               = 1024;
            Criteria.ShadowFilterTapCount        = 7;
            Criteria.GlobalIlluminationEnabled   = true;
            Criteria.AntiAliasingEnabled         = true;
            Criteria.HardwareRayQueryEnabled     = true;
            Criteria.CloudMarchStepCount         = 36;
            Criteria.CloudLightTapCount          = 5;
            Criteria.LocalVolumeStepCount        = 36;
            Criteria.CloudResolutionScale        = 0.50f;
            Criteria.AtmosphereSampleCount       = 20;
            Criteria.AtmosphereLightSampleCount  = 8;
            Criteria.StarLayerCount              = 3;
            Criteria.StarSuperSampleCount        = 2;
            Criteria.CloudCoverageMargin         = 0.00f;
            break;

        case FidelityCategory::ReferenceFidelity:
        default:
            Criteria.ResolutionScale             = 1.0f;
            Criteria.ReSTIRCandidateSampleCount  = 16;
            Criteria.ReSTIRExtraCandidateCount      = 4;
            Criteria.ReSTIRSpatialTapCount       = 4;
            Criteria.DenoiseLevelCount           = 5;
            Criteria.FluidVoxelGridResolution    = 64;
            Criteria.ParticleSimulationCapacity  = 65536;
            Criteria.ShadowTechnique             = ShadowTechniqueCategory::PercentageCloserSoftShadow;
            Criteria.ShadowMapSide               = 2048;
            Criteria.ShadowFilterTapCount        = 9;
            Criteria.GlobalIlluminationEnabled   = true;
            Criteria.AntiAliasingEnabled         = true;
            Criteria.HardwareRayQueryEnabled     = true;
            Criteria.CloudMarchStepCount         = 64;
            Criteria.CloudLightTapCount          = 5;
            Criteria.LocalVolumeStepCount        = 64;
            Criteria.CloudResolutionScale        = 1.00f;
            Criteria.AtmosphereSampleCount       = 32;
            Criteria.AtmosphereLightSampleCount  = 12;
            Criteria.StarLayerCount              = 4;
            Criteria.StarSuperSampleCount        = 2;
            Criteria.CloudCoverageMargin         = 0.00f;
            break;
    }

    return Criteria;
}

} // namespace Frontier
