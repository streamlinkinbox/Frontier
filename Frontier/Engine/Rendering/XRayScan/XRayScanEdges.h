/*==========================================================================================================================================
                                                          XRAYSCANEDGES.H
==========================================================================================================================================*/
// 🧩 The scan's wireframe: the car's feature edges, extracted once on the CPU exactly as THREE.EdgesGeometry does it for the reference
//    (Truck.tsx: body parts 14°, tyres 28°), one set per material family because the reference builds one mesh per part. A port of
//    VehicleEdges.js.

#pragma once

#include "XRayScanSpecification.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::XRayScan
{

// 📝 Which families carry a wireframe and at what crease angle [°]: the painted and clad body panels (BodyPart: EdgesGeometry(geo, 14)) and the
//    tyres (EdgesGeometry(tireGeo, 28)). Rims, hubs, glass, lamps and the interior have no wireframe in the reference.
struct ScanEdgeFamily
{
    uint32_t Family;      // [-]
    double   Threshold;   // [°]
};
inline constexpr std::array<ScanEdgeFamily, 3> EdgeFamilies = {{{BodyCoatFamily, 14.0}, {VehiclePlasticFamily, 14.0}, {TyreFamily, 28.0}}};

inline constexpr double WeldPrecision = 1.0e4;   // [1/unit] EdgesGeometry: precisionPoints = 4

// 📝 One facet of a vehicle's facet set in doubles, as the demo's facets are: the corners in the set's local frame and the material family.
struct ScanFacet
{
    std::array<std::array<double, 3>, 3> Corners{};   // [m]
    uint32_t                             Family = 0;  // [-]
};

// 📝 One vehicle occupant of the scan: its index in the occupant list and the facet set it draws.
struct ScanOccupantSource
{
    uint32_t Occupant = 0;   // [-]
    uint32_t FacetSet = 0;   // [-]
};

// 📝 The records the overlay shader reads: per segment two float4s — A.xyz and the tag (occupant | family << 8, as raw bits), B.xyz and zero.
struct ScanEdgeRecords
{
    std::vector<float> Records{};   // [m] 8 floats per segment
    uint32_t           Count = 0;   // [-] segments
};

// 📐 V8's Math.hypot: the largest magnitude scales the three terms and their squares are summed with Kahan's compensation.
[[nodiscard]] double ResolveHypot(double X, double Y, double Z);

// 📐 THREE.EdgesGeometry, step for step: vertices weld by their position rounded to four decimals; an edge shared by two triangles in opposite
//    directions is kept when the angle between their normals reaches the threshold (n0·n1 ≤ cos threshold); an edge only one triangle has is
//    a boundary and is kept. Returns the segment end points: ax, ay, az, bx, by, bz per segment.
[[nodiscard]] std::vector<double> ExtractFeatureEdges(std::span<const ScanFacet> Facets, double ThresholdDegrees);

// 📝 The hero's body is occupant 0 and its four wheels 1–4 in the demo; the wheel set is shared, so its edges are extracted once and
//    repeated per wheel. FacetSets is indexed by ScanOccupantSource::FacetSet.
[[nodiscard]] ScanEdgeRecords ConstructScanEdges(std::span<const ScanOccupantSource> Occupants,
                                                 std::span<const std::span<const ScanFacet>> FacetSets);

} // namespace Frontier::XRayScan
