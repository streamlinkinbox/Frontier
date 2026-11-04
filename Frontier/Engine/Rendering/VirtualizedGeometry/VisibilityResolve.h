/*==========================================================================================================================================
                                                          VISIBILITYRESOLVE.H
==========================================================================================================================================*/
// 🧩 1:1 port of the demo's visibility-buffer resolve (VisibilityResolve.js): cluster-triangle identity raster without material
//    work, deferred material resolve from interpolated UV and family, and a two-layer refraction resolve for transmissive clusters.

// 📝 Transmissive geometry never enters the opaque visibility or depth buffers; it is depth-peeled into a front and a back glass
//    layer so the resolve refracts through an entry and an exit surface. This is the CPU reference of the GPU visibility raster and
//    material resolve (Shaders/VirtualizedGeometry/VisibilityRaster.slang, MaterialResolve.slang).
#pragma once
#include "NestingSelection.h"
namespace Frontier::VirtualizedGeometry
{
inline constexpr double GlassIndexOfRefraction = 1.52;   // [-]

// 📝 Identity packing: 0 is the background, otherwise record slot · 256 + local triangle + 1 (clusters hold ≤ 128 triangles).
[[nodiscard]] constexpr uint32_t PackVisibilityIdentity(uint32_t RecordSlot, uint32_t LocalTriangle) noexcept
{
    return RecordSlot * 256 + LocalTriangle + 1;
}
struct VisibilityIdentity
{
    uint32_t RecordSlot = 0, LocalTriangle = 0;
};
[[nodiscard]] std::optional<VisibilityIdentity> UnpackVisibilityIdentity(uint32_t Identity) noexcept;

struct VisibilityRaster
{
    uint32_t                          Width = 0, Height = 0;
    std::vector<uint32_t>             OpaqueVis{};
    std::vector<float>                OpaqueDepth{};       // [-] reversed-Z: larger is nearer, 0 is clear
    std::vector<float>                OpaqueBary{};        // [-] two per pixel (corner 1 and corner 2 weights)
    std::array<std::vector<uint32_t>, 2> GlassVis{};       // [-] front, back
    std::array<std::vector<float>, 2>    GlassDepth{};
    std::array<std::vector<float>, 2>    GlassBary{};
    std::vector<RefreshCluster>       Items{};
};
// 📝 Opaque clusters depth-test into the opaque buffer first; glass then depth-tests against it and peels into two layers.
[[nodiscard]] VisibilityRaster RasterizeVisibility(const std::vector<RefreshCluster>& Items, uint32_t Width, uint32_t Height,
                                                   const ColumnMatrix& ViewProjection);

// 📝 Procedural palette by family (the CPU stand-in for the material strata the GPU resolve samples).
[[nodiscard]] const std::array<double, 3>& FamilyBaseColour(uint32_t Family) noexcept;

struct VisibilityResolution
{
    uint32_t           Width = 0, Height = 0;
    std::vector<float> OpaqueColor{};   // [-] RGB per pixel
    std::vector<float> FinalColor{};    // [-] RGB per pixel, glass refracted
    uint32_t           GlassPixels = 0, TwoLayerPixels = 0;
};
// 📝 Opaque visibility to lit colour; every glass pixel refracts through its front and back layers, gathers the resolved opaque
//    colour at the exit projection, absorbs over the travelled thickness and mixes a Schlick Fresnel sky reflection.
[[nodiscard]] VisibilityResolution ResolveVisibility(const VisibilityRaster& Raster, const NestingCamera& Camera,
                                                     std::optional<double> IndexOfRefraction = std::nullopt,
                                                     const std::optional<ColumnMatrix>& ViewProjection = std::nullopt);
} // namespace Frontier::VirtualizedGeometry
