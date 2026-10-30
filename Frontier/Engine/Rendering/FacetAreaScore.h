/*============================================================================================================================================
                                                          FACETAREASCORE.H
============================================================================================================================================*/
// 🧩 Evaluate compensated continuous-area geometry and an unoccluded diffuse target without tracing or history.

#pragma once
#include "FacetAreaProposals.h"

namespace Frontier
{
struct FacetAreaScoreInput
{
    FacetAreaTriangle    Triangle;                         // [m] - Source corners,not a rounded sampled point.
    float                U = 0, V = 0;                    // [-] - Source barycentrics.
    std::array<float, 3>  SourceNormal{0, 0, 1};           // [-] - Caller-resolved unit shading normal.
    std::array<float, 3>  SourceAlbedo{1, 1, 1};           // [-] - Nonnegative diffuse reflectance.
    std::array<float, 3>  Receiver{};                      // [m] - Primary surface position.
    std::array<float, 3>  ReceiverNormal{0, 0, -1};        // [-] - Caller-resolved unit shading normal.
    std::array<float, 3>  ReceiverAlbedo{1, 1, 1};         // [-] - Nonnegative diffuse reflectance.
    std::array<float, 3>  LightDirection{.8f, 0, .6f};     // [-] - Unit direction toward the source light.
    std::array<float, 3>  LightRadiance{1, .7f, .4f};       // [radiance] - Nonnegative light RGB.
    float                TargetFloor = 1e-6f;             // [target] - Strictly positive support floor.
};

struct FacetAreaScore
{
    bool                 Accepted = false;               // [-] - Invalid/unrepresentable arithmetic is rejected.
    std::array<float, 3>  Geometry{};                     // [1/m²] - Diffuse receiver BRDF times projected area.
    std::array<float, 3>  ProxyRgb{};                     // [radiance/m²] - Unoccluded source RGB times geometry.
    float                Target = 0;                     // [target] - Floor+mean proxy RGB.
    float                ReceiverProjection = 0;         // [m] - Compensated nonnegative receiver projection.
    float                SourceProjection = 0;           // [m] - Nonnegative source projection.
    float                SquaredDistance = 0;            // [m²] - Corrected displacement squared.
};

// No shading-material resolution,random generation,visibility,selection or history is performed here.
// Finite near-unit normals are required (squared-length tolerance2e-4);barycentric sum tolerance2e-6.
[[nodiscard]] FacetAreaScore EvaluateFacetAreaScore(const FacetAreaScoreInput& Input) noexcept;
}
