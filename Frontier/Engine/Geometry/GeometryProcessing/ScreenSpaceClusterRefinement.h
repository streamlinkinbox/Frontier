/*==========================================================================================================================================
                                                   SCREEN-SPACE CLUSTER REFINEMENT
                                     ------------------------------------------------------------------
             Nanite-like virtualized Cluster-DAG: locked-border Garland-Heckbert QEM tier refinement sequence,
             curved-glass IOR optical deviation boost, Karis group-atomic antichain cut, two-phase
             Hi-Z occlusion (glass exempt from depth occlusion writes & backface cone cull), 64 KB
             pinned-root LRU streaming pages, 3-tier virtualized material LODs (Full / Toksvig / Proxy),
                           distance-curve detail controls, and RAM + VRAM byte telemetry.
==========================================================================================================================================*/
// 🧩 Virtualized screen-space cluster-DAG refinement, material LOD virtualization, 64 KB page residency, and memory telemetry.

#pragma once

#include "Engine/Geometry/GeometryProcessing/QuadricTierBuild.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Frontier
{

struct ClusterFloat3
{
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
};

//------------------------------------------------------------------------------------------------------------------------
//                  1. VIRTUALIZED MATERIAL PROFILES & 3-TIER NANITE-LIKE MATERIAL SCALING
//------------------------------------------------------------------------------------------------------------------------

enum class ClusterMaterialTier : uint8_t
{
    FullStrata      = 0, // Near range: full multi-strata reflectance + UV micro-detail (mip 0..1)
    FilteredToksvig = 1, // Mid range: mip-filtered reflectance + Toksvig normal-cone roughness lengthening
    CompactProxy    = 2, // Horizon range: zero-texture 16-byte pre-integrated diffuse + lobe proxy
};

struct ClusterMaterialProfile
{
    uint32_t      ClassificationId            = 0u;
    bool          TransmissiveGlass           = false;
    float         RefractiveIndexIor          = 1.52f;
    ClusterFloat3 SpectralAbsorptionPerMeter  = {0.12f, 0.04f, 0.06f};
    ClusterFloat3 AlbedoRgb                   = {0.72f, 0.74f, 0.77f};
    float         Roughness                   = 0.34f;
    float         Metallic                    = 0.0f;
    float         ClearcoatWeight             = 0.0f;
    float         ClearcoatRoughness          = 0.08f;
    ClusterFloat3 EmissiveRadianceRgb         = {0.0f, 0.0f, 0.0f};
    float         ProceduralDetailStrength    = 0.16f;
};

struct EvaluatedClusterMaterial
{
    ClusterMaterialTier ActiveTier          = ClusterMaterialTier::FullStrata;
    ClusterFloat3       EffectiveAlbedoRgb  = {0.72f, 0.74f, 0.77f};
    float               EffectiveRoughness  = 0.34f; // Includes Toksvig normal-variance lengthening on Tier 1/2
    float               EffectiveMetallic   = 0.0f;
    float               EffectiveClearcoat  = 0.0f;
    float               TextureMipBias      = 0.0f;
    uint32_t            EstimatedFetchBytes = 64u;   // 64 B FullStrata, 24 B FilteredToksvig, 16 B CompactProxy
};

//------------------------------------------------------------------------------------------------------------------------
//                  2. CLUSTER RECORD, 64 KB STREAMING PAGES, DISTANCE CONTROLS & TELEMETRY
//------------------------------------------------------------------------------------------------------------------------

struct ClusterRefinementRecord
{
    uint32_t              TierIndex                  = 0u; // 0 = finest leaf .. N-1 = coarsest root
    uint32_t              ClusterIndexInTier         = 0u;
    uint32_t              GlobalClusterId            = 0u;
    uint32_t              ClassificationId           = 0u;
    bool                  TransmissiveGlass          = false;
    float                 RefractiveIndexIor         = 1.0f;
    float                 OpticalDeviationBoost      = 1.0f;
    double                SelfDeviationMeters        = 0.0;
    double                CoarserDeviationMeters     = 1.0e12;
    FacetPoint3D          SphereCentre               = {};
    double                SphereRadiusMeters         = 0.0;
    FacetPoint3D          CoarserSphereCentre        = {};
    double                CoarserSphereRadiusMeters  = 0.0;
    FacetPoint3D          ConeAxis                   = {};
    double                ConeCutoff                 = -1.0;
    ClusterFloat3         BoundsMin                  = {};
    ClusterFloat3         BoundsMax                  = {};
    uint32_t              TriangleCount              = 0u;
    uint32_t              PageSlotIndex              = 0u;
    std::vector<uint32_t> EnclosedFinerGlobals{};
};

struct ClusterStreamingPage
{
    uint32_t              PageIndex        = 0u;
    uint32_t              ByteSize         = 0u;
    bool                  PinnedRoot       = false;
    bool                  ResidentInMemory = true;
    uint64_t              LastTouchedTick  = 0u;
    std::vector<uint32_t> MemberGlobalIds{};
};

struct ClusterDetailParameters
{
    float    NearFullDetailMeters     = 12.0f;             // [m] 100% leaf detail & FullStrata materials inside Near
    float    FarHorizonMeters         = 180.0f;            // [m] Asymptotic retention floor reached at Far
    float    MinimumRetentionFraction = 0.18f;             // [0.02..1.0] Far-horizon quality floor
    float    FalloffCurvePower        = 1.35f;             // [0.35..3.5] Distance falloff exponent
    float    MaterialQualityScale     = 0.85f;             // [0..1] Biases FullStrata -> FilteredToksvig -> CompactProxy
    float    BaseErrorThresholdPixels = 1.15f;             // [px] Screen-space error threshold at NearFullDetailMeters
    uint32_t VramPageBudgetBytes      = 16u * 1024u * 1024u; // [B] Resident 64 KB streaming page budget
};

[[nodiscard]] float EvaluateDistanceQualityFactor(float DistanceMeters, const ClusterDetailParameters& Parameters) noexcept;

struct ClusterCameraView
{
    ClusterFloat3           CameraOrigin     = {4.6f, 2.2f, 5.8f};
    ClusterFloat3           CameraTarget     = {0.0f, 0.85f, 0.0f};
    ClusterFloat3           CameraUp         = {0.0f, 1.0f, 0.0f};
    float                   VerticalFovRad   = 0.82f;
    uint32_t                ViewportWidthPx  = 800u;
    uint32_t                ViewportHeightPx = 450u;
    ClusterDetailParameters Detail{};
};

struct ClusterRuntimeFrameReport
{
    std::vector<uint32_t>            SelectedOpaqueGlobals{};
    std::vector<uint32_t>            SelectedGlassGlobals{};
    std::vector<ClusterMaterialTier> SelectedOpaqueMaterialTiers{};
    uint32_t                         TotalCutClusters          = 0u;
    uint32_t                         FrustumCulledCount        = 0u;
    uint32_t                         BackfaceConeCulledCount   = 0u;
    uint32_t                         HiZOccludedOpaqueCount    = 0u;
    uint32_t                         GlassExemptFromBackface   = 0u;
    uint32_t                         OpaqueBehindGlassSurvived = 0u;
    uint32_t                         FallbackToResidentCoarser = 0u;
    uint32_t                         ActiveTrianglesAfterCull  = 0u;
    uint32_t                         SourceLeafTriangles       = 0u;
    std::array<uint32_t, 3>          MaterialTierClusterCounts = {0u, 0u, 0u};
};

struct ClusterMemoryTelemetry
{
    uint64_t                RawUnindexedRamBytes      = 0u;
    uint64_t                WeldedLeafRamBytes        = 0u;
    uint64_t                RefinementDagRamBytes      = 0u;
    uint64_t                ClusterMetadataRamBytes   = 0u;
    uint64_t                TotalHostRamBytes         = 0u;
    uint32_t                RefinementTierCount        = 0u;
    uint32_t                TotalClusterCount         = 0u;
    uint32_t                TotalStreamingPages       = 0u;
    uint32_t                PinnedRootPages           = 0u;
    uint32_t                ResidentStreamingPages    = 0u;
    uint64_t                TotalPagedVramBytes       = 0u;
    uint64_t                ResidentVramBytes         = 0u;
    uint64_t                EvictedPageBytes          = 0u;
    uint64_t                ConfiguredVramBudgetBytes = 0u;
    uint32_t                SourceLeafTriangles       = 0u;
    uint32_t                ActiveCutTriangles        = 0u;
    float                   TriangleRetentionFraction = 1.0f;
    std::array<uint32_t, 3> MaterialTierClusterCounts = {0u, 0u, 0u};
    float                   MeanMaterialBytesPerCluster = 64.0f;
};

//------------------------------------------------------------------------------------------------------------------------
//                                      3. SCREEN-SPACE CLUSTER REFINEMENT COORDINATOR
//------------------------------------------------------------------------------------------------------------------------

class ScreenSpaceClusterRefinement
{
  public:
    static constexpr uint32_t StreamingPageCapacityBytes = 65536u; // Fixed 64 KB virtualized geometry page size

    ScreenSpaceClusterRefinement() = default;

    void RegisterMaterialProfile(const ClusterMaterialProfile& Profile);
    void RegisterControlVehicleMaterials();

    bool BuildFromFacetField(const FacetField&             SourceFacets,
                             const TierBuildSpecification& Specification = TierBuildSpecification{});

    void EnforcePageResidencyBudget(uint32_t                 MaxResidentBytes,
                                    const ClusterCameraView& Camera,
                                    uint64_t                 FrameTick);

    [[nodiscard]] ClusterRuntimeFrameReport ExecuteCutAndTwoPhaseHiZ(const ClusterCameraView& Camera) const;

    [[nodiscard]] EvaluatedClusterMaterial EvaluateClusterMaterial(uint32_t                       GlobalClusterId,
                                                                   float                          DistanceMeters,
                                                                   float                          ProjectedErrorPixels,
                                                                   const ClusterDetailParameters& Detail) const noexcept;

    [[nodiscard]] ClusterMemoryTelemetry QueryMemoryTelemetry(
        const ClusterRuntimeFrameReport& LastFrameReport,
        uint32_t                         ConfiguredVramBudgetBytes) const noexcept;

    [[nodiscard]] const ClusterTierSequence&                     TierSequence() const noexcept { return Sequence_; }
    [[nodiscard]] const std::vector<ClusterRefinementRecord>&     ClusterRecords() const noexcept { return Records_; }
    [[nodiscard]] const std::vector<ClusterStreamingPage>&       StreamingPages() const noexcept { return Pages_; }
    [[nodiscard]] const std::vector<ClusterMaterialProfile>&     MaterialProfiles() const noexcept { return Materials_; }
    [[nodiscard]] const ClusterMaterialProfile&                  FindMaterial(uint32_t ClassificationId) const noexcept;

  private:
    uint64_t                             RawUnindexedRamBytes_ = 0u;
    std::vector<ClusterMaterialProfile>  Materials_{};
    ClusterMaterialProfile               DefaultMaterial_{};
    ClusterTierSequence                  Sequence_{};
    std::vector<ClusterRefinementRecord>  Records_{};
    std::vector<std::vector<uint32_t>>   TierClusterToGlobal_{};
    std::vector<ClusterStreamingPage>    Pages_{};
};

} // namespace Frontier
