/*==========================================================================================================================================
                                                     PREVIEW RASTER
                                     ------------------------------------------------------------------
             Editor & Development ONLY GPU Vulkan 1.3 cluster preview & inspection rasterizer with
             modular fast shadow modes (None, ContactEllipsoid, OrthoClusterDepth, SignedDistanceCone,
             HybridOrthoAndDistance). Strictly compiled out under FRONTIER_SHIPPING_PROFILE
             (FRONTIER_EDITOR == 0) so Shipping binaries never include preview rasterization or shaders.
==========================================================================================================================================*/
// 🧩 Editor/Development GPU preview rasterizer: simple sky/atmosphere + modular fast shadows + 10 cluster/glass modes.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"

#if FRONTIER_EDITOR

#include "Engine/DeviceExchange/VulkanComputeExchange.h"
#include "Engine/Geometry/GeometryProcessing/ScreenSpaceClusterRefinement.h"
#include "Engine/Geometry/GeometryProcessing/VolumeDistanceArchive.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

enum class PreviewViewMode : uint32_t
{
    ShadedPreview      = 0, // Analytic sky/atmosphere + modular fast shadow + 3-tier material + 2-layer glass
    ShadedWireframe    = 1, // ShadedPreview + 1 px barycentric cluster triangle wireframe
    WireframeTopology  = 2, // Dark theme blueprint + tier-colored triangle wireframe
    ClusterIdentity    = 3, // Golden-ratio per-cluster ID palette + translucent cyan glass highlight
    RefinementTierHeat = 4, // Finest Tier 0 warm coral-amber -> coarsest root cool slate-blue
    ProjectedErrorHeat = 5, // Screen-space pixel error heatmap relative to BaseErrorThresholdPixels
    MaterialTierHeat   = 6, // FullStrata (cyan-blue) / FilteredToksvig (amber) / CompactProxy (violet)
    SurfaceNormals     = 7, // World-space geometric normals
    TextureCoordinates = 8, // Triplanar/UV checker & mip density
    GlassLayerPeel     = 9, // Opaque behind glass + 1-layer vs 2-layer dielectric glazing peel
};

enum class PreviewShadowMode : uint32_t
{
    None                   = 0, // Unshadowed lit preview
    ContactEllipsoid       = 1, // Zero-cost analytic ground contact shadow
    OrthoClusterDepth      = 2, // Default fast directional shadow: 1024x1024 ortho cluster depth + 4-tap PCF
    SignedDistanceCone     = 3, // Fast sphere-traced solar cone shadow over attached .sdfspace VolumeDistanceField
    HybridOrthoAndDistance = 4, // Combined OrthoClusterDepth + SignedDistanceCone contact hardening & SDF AO
};

struct PreviewAtmosphereBackdrop
{
    ClusterFloat3     SolarDirectionUnit   = {0.48f, 0.72f, 0.50f};
    ClusterFloat3     ZenithSkyRgb         = {0.13f, 0.28f, 0.62f};
    ClusterFloat3     HorizonSkyRgb        = {0.56f, 0.68f, 0.84f};
    float             ExposureScale        = 1.05f;
    PreviewShadowMode ShadowMode           = PreviewShadowMode::OrthoClusterDepth;
    float             ShadowHalfSpanMeters = 5.2f; // Half-width [m] of solar orthographic shadow frustum around target
};

struct PreviewFrameExecution
{
    ClusterRuntimeFrameReport CutReport{};
    ClusterMemoryTelemetry    Memory{};
    uint32_t                  UploadedClusterCount        = 0u;
    uint32_t                  UploadedTriangleCount       = 0u;
    uint32_t                  UploadedOpaqueTriangleCount = 0u;
    PreviewShadowMode         ActiveShadowMode            = PreviewShadowMode::OrthoClusterDepth;
};

class PreviewRaster
{
  public:
    static constexpr uint32_t kShadowMapResolution = 1024u;

    bool Initialize(VulkanComputeExchange& Exchange,
                    const std::string&     ShaderBinaryDirectory,
                    std::string*           OutDiagnostic = nullptr);
    void Shutdown(VulkanComputeExchange& Exchange) noexcept;

    // Optional: binds a pre-baked .sdfspace VolumeDistanceField for SignedDistanceCone & HybridOrthoAndDistance modes.
    bool AttachDistanceVolume(VulkanComputeExchange&     Exchange,
                              const VolumeDistanceField* DistanceVolume);

    bool RenderView(VulkanComputeExchange&              Exchange,
                    const ScreenSpaceClusterRefinement& Refinement,
                    const ClusterCameraView&            Camera,
                    PreviewViewMode                     ViewMode,
                    const PreviewAtmosphereBackdrop&    Backdrop,
                    PreviewFrameExecution*              OutExecution = nullptr);

    bool ReadbackLinearRgba(VulkanComputeExchange& Exchange, std::vector<float>& OutLinearRgbaFloats);
    bool ReadbackTonemappedSrgb8(VulkanComputeExchange& Exchange,
                                 float                  ExposureScale,
                                 std::vector<uint8_t>&  OutSrgbRgbaBytes);

    [[nodiscard]] const DeviceImageArraySlot& TargetImage() const noexcept { return TargetImage_; }

  private:
    ComputeProgramSlot   ClearShadowProgram_{};
    ComputeProgramSlot   ProjectShadowProgram_{};
    ComputeProgramSlot   TileBinProgram_{};
    ComputeProgramSlot   ResolveProgram_{};
    DeviceMemorySpan     SceneHeaderSpan_{};
    DeviceMemorySpan     TriangleSpan_{};
    DeviceMemorySpan     TileBinsSpan_{};
    DeviceMemorySpan     ShadowDepthSpan_{};
    DeviceMemorySpan     DistanceVolumeSpan_{};
    DeviceImageArraySlot TargetImage_{};
    bool                 HasDistanceVolume_ = false;
};

} // namespace Frontier

#endif // FRONTIER_EDITOR
