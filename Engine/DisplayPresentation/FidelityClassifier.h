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
//                                                 SHADOW TECHNIQUE
//------------------------------------------------------------------------------------------------------------------------
// The GI-off path resolves light visibility from rasterized shadow maps; which filter runs over that map is a tier
//    decision, not a per-scene one. The ladder is strictly increasing in cost and in realism:
//
//    HardShadowMap  — one depth comparison per light tap. Binary, aliased, cheapest; the Minimal tier's shadow.
//    WidePercentageCloserFilter — a fixed-radius PCF kernel. The penumbra is a constant-width ramp: it does not
//       widen with occluder distance (that is physically wrong), but it is stable, cheap, and hides map aliasing.
//    PercentageCloserSoftShadow — true PCSS: a blocker search estimates the average occluder depth, the penumbra
//       width follows the similar-triangles relation w = (Receiver − Blocker) / Blocker × LightSize, and the PCF
//       kernel is sized from it. Contact points stay sharp and the shadow softens with distance, as in reality.
//
// Ultra and Reference both run PCSS; they differ in kernel taps (see ShadowFilterTapCount), so the top of the ladder
//    is the most physically faithful shadow the path can produce.

enum class ShadowTechniqueCategory : uint32_t
{
    HardShadowMap              = 0,
    WidePercentageCloserFilter = 1,
    PercentageCloserSoftShadow = 2,
    Count                      = 3
};

[[nodiscard]] constexpr const char* ShadowTechniqueLabel(ShadowTechniqueCategory Technique) noexcept
{
    switch (Technique)
    {
        case ShadowTechniqueCategory::HardShadowMap:              return "Hard";
        case ShadowTechniqueCategory::WidePercentageCloserFilter: return "PCF";
        case ShadowTechniqueCategory::PercentageCloserSoftShadow: return "PCSS";
        default:                                                  return "PCF";
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                             SHADOW RESOLUTION OVERRIDE
//------------------------------------------------------------------------------------------------------------------------
// The Control Centre's Render page carries a shadow-resolution dropdown that outranks the tier. FollowQualityTier
//    keeps the tier's own map size (the default); every other entry pins the map at that side regardless of tier,
//    so a Minimal machine can still be given a 2048² map and a Reference one can be dropped to 256².

enum class ShadowResolutionCategory : uint32_t
{
    FollowQualityTier = 0,
    Side256           = 1,
    Side512           = 2,
    Side1024          = 3,
    Side2048          = 4,
    Count             = 5
};

[[nodiscard]] constexpr const char* ShadowResolutionLabel(ShadowResolutionCategory Resolution) noexcept
{
    switch (Resolution)
    {
        case ShadowResolutionCategory::Side256:  return "256 x 256";
        case ShadowResolutionCategory::Side512:  return "512 x 512";
        case ShadowResolutionCategory::Side1024: return "1024 x 1024";
        case ShadowResolutionCategory::Side2048: return "2048 x 2048";
        default:                                 return "Auto (tier)";
    }
}

// Map side in texels for a pinned entry; 0 for FollowQualityTier, whose side comes from the tier criteria.
[[nodiscard]] constexpr uint32_t ShadowResolutionSide(ShadowResolutionCategory Resolution) noexcept
{
    switch (Resolution)
    {
        case ShadowResolutionCategory::Side256:  return 256u;
        case ShadowResolutionCategory::Side512:  return 512u;
        case ShadowResolutionCategory::Side1024: return 1024u;
        case ShadowResolutionCategory::Side2048: return 2048u;
        default:                                 return 0u;
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
    uint32_t                FluidVoxelGridResolution;           // [cells] 3D fluid domain resolution
    uint32_t                ParticleSimulationCapacity;         // [count] maximum active compute particles
    uint32_t                ReSTIRExtraCandidateCount;        // [count] extra same-pixel RIS candidates (R6 row 3: renamed)
    uint32_t                ReSTIRSpatialTapCount;              // [count] spatial-reuse neighbours per pixel (0 = no cross)
    uint32_t                DenoiseLevelCount;                  // [count] a-trous levels dispatched, 1 … kDenoiseLevelCount
    ShadowTechniqueCategory ShadowTechnique;                    // [category] GI-off shadow filter: hard · wide PCF · PCSS
    uint32_t                ShadowMapSide;                      // [px] shadow map side in texels (the tier's default)
    uint32_t                ShadowFilterTapCount;               // [count] filter kernel side in taps (1 = single comparison)
    // ── Celestial port. The demo ships seven tiers (Low · Economic · Standard · Ultra · Cinematic · Reference,
    //    plus Auto); we have five, so Cinematic collapses into Reference — steps from Reference, render scale
    //    from Cinematic, since our Reference already means "most realistic, no compromise". The ladder lives
    //    HERE and nowhere else: the panel reads these, it does not restate them.
    uint32_t                CloudMarchStepCount;                // [count] steps through the cloud shell
    uint32_t                CloudLightTapCount;                 // [count] sun-shadow taps inside the cloud march
    uint32_t                LocalVolumeStepCount;               // [count] steps through local fog / local cloud
    float                   CloudResolutionScale;               // [0..1] cloud shell resolution vs the frame
    uint32_t                AtmosphereSampleCount;              // [count] view-ray samples through the atmosphere
    uint32_t                AtmosphereLightSampleCount;         // [count] sun-ray samples per atmosphere sample
    uint32_t                StarLayerCount;                     // [count] star field layers
    uint32_t                StarSuperSampleCount;               // [count] star AA samples (1 = none)
    float                   CloudCoverageMargin;                // [0..1] early-out slack on the coverage probe
    bool                    GlobalIlluminationEnabled;          // [bool] indirect radiosity ReSTIR GI
    bool                    AntiAliasingEnabled;                // [bool] sub-pixel jitter + temporal accumulation
    bool                    HardwareRayQueryEnabled;            // [bool] hardware ray tracing acceleration
};

// Applies the Control Centre's shadow-resolution dropdown over a tier's criteria. FollowQualityTier leaves the tier
//    map size untouched; any pinned entry replaces it. The technique and tap count never change — the dropdown sizes
//    the map, the tier still chooses how it is filtered.
[[nodiscard]] constexpr FidelityCriteria WithShadowResolution(FidelityCriteria Criteria,
                                                              ShadowResolutionCategory Override) noexcept
{
    const uint32_t Side = ShadowResolutionSide(Override);
    if (Side != 0u)
        Criteria.ShadowMapSide = Side;
    return Criteria;
}

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
