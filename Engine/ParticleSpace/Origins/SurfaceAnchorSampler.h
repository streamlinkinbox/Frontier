/*============================================================================================================================================
                                                        SURFACEANCHORSAMPLER.H
============================================================================================================================================*/
// 🧩 17 procedural 3D surfaces (0..16) + GeometrySpace::PolygonNetwork area-weighted anchor sampling & backing triangle geometry

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Network/PolygonNetwork.h"
#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/Mathematics/CoordinateSpace.h"
#include "Engine/ParticleSpace/Origins/ReleaseOriginTypes.h"
#include "Engine/ParticleSpace/ParticleBuffers.h"

#include <cstdint>
#include <vector>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct SurfaceTriangleEntry
{
    Mathematics::Axis3D VertexA    { 0.0f, 0.0f, 0.0f }; // [m]   - First triangle corner
    Mathematics::Axis3D VertexB    { 0.0f, 0.0f, 0.0f }; // [m]   - Second triangle corner
    Mathematics::Axis3D VertexC    { 0.0f, 0.0f, 0.0f }; // [m]   - Third triangle corner
    Mathematics::Axis3D Normal     { 0.0f, 0.0f, 1.0f }; // [-]   - Outward unit normal
    float               Area       = 0.0f;               // [m^2] - Triangle surface area
    float               EdgeWeight = 0.0f;               // [m^2] - Perimeter-weighted ridge/crease weight
};

struct SurfaceBackingGeometry
{
    std::vector<SurfaceTriangleEntry> Triangles {};                      // [-] - Normalized triangles in unit radius ~1 m
    float                             TotalArea = 0.0f;                  // [m^2]
    Mathematics::Axis3D               BoundsMin { -1.0f, -1.0f, -1.0f }; // [m]
    Mathematics::Axis3D               BoundsMax {  1.0f,  1.0f,  1.0f }; // [m]
};

struct SurfaceAnchorPoint
{
    Mathematics::Axis3D PositionMeters { 0.0f, 0.0f, 0.0f }; // [m] - Sampled anchor coordinate
    Mathematics::Axis3D NormalAxis     { 0.0f, 0.0f, 1.0f }; // [-] - Outward surface normal
    float               LocalElevation = 0.0f;               // [m] - Local vertical coordinate prior to world offset
    float               PitchMeters    = 0.08f;              // [m] - Characteristic neighbor spacing
};

struct SurfaceAnchorSet
{
    SurfaceBackingGeometry          BackingGeometry {}; // [-] - Normalized backing surface triangles
    std::vector<SurfaceAnchorPoint> Anchors         {}; // [-] - Sampled surface/volume anchors
    std::vector<ParticleSlot>       PackedSnapSlots {}; // [-] - Pre-packed GPU ParticleSlot snap buffer
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] Mathematics::Axis3D RotateEulerXyzDegrees(
    Mathematics::Axis3D Point,
    Mathematics::Axis3D EulerDegrees) noexcept;

[[nodiscard]] SurfaceBackingGeometry ConstructProceduralSurfaceGeometry(
    SurfacePresetCategory             Preset,
    Mathematics::WorldCoordinateBasis TargetBasis = Mathematics::WorldCoordinateBasis::RightHandedZUp);

[[nodiscard]] SurfaceBackingGeometry ExtractSurfaceGeometryFromPolygonNetwork(
    const GeometrySpace::PolygonNetwork& Network,
    Mathematics::WorldCoordinateBasis    TargetBasis = Mathematics::WorldCoordinateBasis::RightHandedZUp);

[[nodiscard]] SurfaceAnchorSet SampleSurfaceAnchors(
    const SurfaceAnchorOriginSpec&       Spec,
    uint32_t                             AnchorCapacity,
    const GeometrySpace::PolygonNetwork* OptionalCustomNetwork = nullptr,
    Mathematics::WorldCoordinateBasis    TargetBasis           = Mathematics::WorldCoordinateBasis::RightHandedZUp);

} // namespace Frontier::ParticleSpace
