/*==========================================================================================================================================
                                                 VOLUME DISTANCE ARCHIVE
                                     ------------------------------------------------------------------
             Headless mesh-to-SDF baker (wrapping Engine/ContentInterchange/VolumeDistanceField's exact
             Voronoi triangle seeding + 3D Felzenszwalb-Huttenlocher separable EDT + 3-axis parity sign),
             compact IEEE-754 Float16 binary .sdfspace disk serialization, standalone project-content
             resolution, multi-instance world SDF union composition, and GPU float4 upload packing.
==========================================================================================================================================*/
// 🧩 Headless .facet -> .sdfspace baker, binary archive codec, world SDF composer, and GPU upload table builder.

#pragma once

#include "Engine/ContentInterchange/VolumeDistanceField.h"
#include "Engine/Geometry/PrimitiveGeometryGenerator.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Frontier
{

struct HeadlessSdfBakeOptions
{
    std::array<uint32_t, 3> Dimensions   = {72u, 40u, 96u};
    float                   PaddingRatio = 0.10f;
    bool                    SignedField  = true;
    float                   NarrowBandM  = 0.0f; // 0 = full-domain exact Euclidean distance
    bool                    SkipGlass    = true; // Exclude Class 7/12 glazing when baking opaque shadow/GI SDF
};

// Headlessly converts a FacetField into BakeMeshSet and invokes BakeVolumeDistanceField (zero UI dependency).
[[nodiscard]] VolumeDistanceField BakeFacetFieldDistanceVolume(const FacetField&             SourceFacets,
                                                               const HeadlessSdfBakeOptions& Options = {});

// Compact IEEE-754 Float16 binary .sdfspace container ("SDF1" 64-byte header + N_x*N_y*N_z uint16_t payload).
[[nodiscard]] bool SaveVolumeDistanceSpace(const VolumeDistanceField&   Field,
                                           const std::filesystem::path& OutputPath,
                                           std::string*                 OutDiagnostic = nullptr);

[[nodiscard]] bool LoadVolumeDistanceSpaceFromMemory(std::span<const uint8_t> Bytes,
                                                     VolumeDistanceField&     OutField,
                                                     std::string*             OutDiagnostic = nullptr);

// Resolves direct filesystem paths, staged <Project>/Content/<Rel>, or repository EngineContent/<Rel>.
[[nodiscard]] bool LoadVolumeDistanceSpace(const std::string&   PathOrContentRel,
                                           VolumeDistanceField& OutField,
                                           std::string*         OutDiagnostic = nullptr);

struct PlacedDistanceInstance
{
    const VolumeDistanceField* Archive           = nullptr;
    FacetPoint3D               TranslationMeters = {0.0, 0.0, 0.0};
    double                     YawRadians        = 0.0;
    double                     UniformScale      = 1.0;
};

// Evaluates trilinearly interpolated signed distance [m] in local archive coordinates (with exterior box shell growth).
[[nodiscard]] float SampleVolumeDistanceMeters(const VolumeDistanceField& Field,
                                               float                      WorldX,
                                               float                      WorldY,
                                               float                      WorldZ) noexcept;

// Evaluates normalized SDF gradient normal via 4-tap tetrahedron differences.
[[nodiscard]] std::array<float, 3> SampleVolumeGradientNormal(const VolumeDistanceField& Field,
                                                              float                      WorldX,
                                                              float                      WorldY,
                                                              float                      WorldZ) noexcept;

// Fast sphere-traced solar cone visibility in [0..1] over a VolumeDistanceField.
[[nodiscard]] float TraceVolumeConeVisibility(const VolumeDistanceField& Field,
                                              std::array<float, 3>       OriginMeters,
                                              std::array<float, 3>       DirectionUnit,
                                              float                      MaxDistanceMeters = 12.0f,
                                              float                      ConeSharpness     = 14.0f,
                                              uint32_t                   MaxSteps          = 16u) noexcept;

// Unions pre-baked part .sdfspace archives (e.g. ControlVehicleBody + 4x Rim + 4x Tyre) into a single world SDF grid
// in milliseconds without running runtime triangle EDT baking.
[[nodiscard]] VolumeDistanceField ComposeWorldDistanceVolume(std::span<const PlacedDistanceInstance> Instances,
                                                             std::array<float, 3>                    WorldBoundsMin,
                                                             std::array<float, 3>                    WorldBoundsMax,
                                                             std::array<uint32_t, 3>                 Dimensions,
                                                             bool                                    IncludeGroundHalfSpace = false,
                                                             float                                   GroundPlaneY           = 0.0f);

// Loads pre-baked ControlVehicle{Body,Rim,Tyre}.sdfspace archives and composes the complete vehicle world SDF.
[[nodiscard]] bool ComposeControlVehicleDistanceVolume(VolumeDistanceField&           OutWorldField,
                                                       const ControlVehiclePlacement& Placement         = {},
                                                       std::array<uint32_t, 3>        WorldDimensions   = {80u, 40u, 96u},
                                                       bool                           IncludeGroundHalf = false,
                                                       std::string*                   OutDiagnostic     = nullptr);

// Packs a VolumeDistanceField into a GPU StructuredBuffer<float4> upload array:
//   Slot 0: {MinX, MinY, MinZ, float(DimX)}
//   Slot 1: {MaxX, MaxY, MaxZ, float(DimY)}
//   Slot 2: {StepX, StepY, StepZ, float(DimZ)}
//   Slot 3: {InvSpanX, InvSpanY, InvSpanZ, ValidFlag (1.0 = active, 0.0 = empty)}
//   Slots 4..N: 4 consecutive fp32 voxel distances per float4.
void PackVolumeDistanceForGpu(const VolumeDistanceField&          Field,
                              std::vector<std::array<float, 4>>& OutGpuFloat4Table);

} // namespace Frontier
