/*==========================================================================================================================================
                                                    PRIMITIVE GEOMETRY GENERATOR
                                     ------------------------------------------------------------------
             Watertight procedural primitive geometry generators (Box, UV Sphere, Geodesic Sphere, Cylinder, Capsule,
             Torus, Cone, Arch Tunnel, Ground Grid), binary FCT1 FacetStream codec, and ControlVehicle
                    multi-archive assembly (Body + 4x Machined Rims + 4x Vulcanized Tyres + Cabin).
==========================================================================================================================================*/
// 🧩 Core procedural geometry builders & binary .facet archive loader for ScreenSpaceClusterRefinement & proving grounds.

#pragma once

#include "Engine/Geometry/GeometryProcessing/FacetTierBuild.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace Frontier
{

struct PrimitivePlacement
{
    FacetPoint3D TranslationMeters = {0.0, 0.0, 0.0};
    double       YawRadians        = 0.0;
    double       UniformScale      = 1.0;
    uint32_t     ClassificationId  = 0u;
    double       UvScale           = 1.0;
};

//------------------------------------------------------------------------------------------------------------------------
//                           1. BINARY FCT1 FACET ARCHIVE CODEC & CONTROL VEHICLE ASSEMBLY
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] bool LoadFacetStreamFromMemory(std::span<const uint8_t> Bytes,
                                             FacetField&              OutFacets,
                                             const PrimitivePlacement&    Placement           = {},
                                             bool                     ConvertBlenderZToY  = false,
                                             int32_t                  OverrideClassId     = -1);

[[nodiscard]] bool LoadFacetStreamFromFile(const std::string&    PathOrContentRel,
                                           FacetField&           OutFacets,
                                           const PrimitivePlacement& Placement          = {},
                                           bool                  ConvertBlenderZToY = false,
                                           int32_t               OverrideClassId    = -1);

struct ControlVehiclePlacement
{
    FacetPoint3D WorldOriginMeters     = {0.0, 0.0, 0.0};
    double       HeadingYawRadians     = 0.0;
    double       FrontSteerRadians     = 0.16; // Subtle heroic front-wheel turn angle
    double       WheelSpinRadians      = 0.0;
    bool         IncludeInteriorCabin  = true; // Opaque cabin seats/console behind Class-7 glass glazing
    std::string  ArchiveDirectory      = "EnvironmentArchives/ControlVehicle";
};

//Automotive material classification IDs emitted by AssembleControlVehicle:
//   6 = Metallic Clearcoat Bodywork
//   7 = Cabin Laminated Glazing (TransmissiveGlass = true, IOR = 1.52)
//   8 = Lower Carbon / Satin Aero Cladding
//   9 = Matte Interior Trim & Brake Hub
//  10 = Brushed Machined Alloy Rim
//  11 = Vulcanized Tread Tyre Rubber
//  12 = Headlamp Outer Polycarbonate Lens (TransmissiveGlass = true, IOR = 1.58)
//  13 = Headlamp LED Reflector Core
//  14 = Rear Crimson Taillamp Bar
//  15 = Front/Rear Intake Grille
//  16 = Interior Cabin Seats & Console (behind Class 7 windshield)
[[nodiscard]] bool AssembleControlVehicle(FacetField&                    OutFacets,
                                          const ControlVehiclePlacement& Placement = {},
                                          std::string*                   OutDiagnostic = nullptr);

//------------------------------------------------------------------------------------------------------------------------
//                                  2. CORE WATERTIGHT PROCEDURAL PRIMITIVE GEOMETRY GENERATORS
//------------------------------------------------------------------------------------------------------------------------

void AppendProceduralBox(FacetField&           InOutFacets,
                         FacetPoint3D          HalfExtentsMeters,
                         const PrimitivePlacement& Placement = {});

void AppendProceduralUvSphere(FacetField&           InOutFacets,
                              double                RadiusMeters,
                              uint32_t              LongitudeSegments = 32u,
                              uint32_t              LatitudeRings     = 18u,
                              const PrimitivePlacement& Placement         = {});

void AppendProceduralGeodesicSphere(FacetField&           InOutFacets,
                                    double                RadiusMeters,
                                    uint32_t              Subdivisions = 3u,
                                    const PrimitivePlacement& Placement    = {});

void AppendProceduralCylinder(FacetField&           InOutFacets,
                              double                RadiusMeters,
                              double                HeightMeters,
                              uint32_t              RadialSegments = 28u,
                              const PrimitivePlacement& Placement      = {});

void AppendProceduralCapsule(FacetField&           InOutFacets,
                             double                RadiusMeters,
                             double                CylinderSpanMeters,
                             uint32_t              RadialSegments = 28u,
                             uint32_t              CapRings       = 8u,
                             const PrimitivePlacement& Placement      = {});

void AppendProceduralTorus(FacetField&           InOutFacets,
                           double                MajorRadiusMeters,
                           double                MinorRadiusMeters,
                           uint32_t              MajorSegments = 36u,
                           uint32_t              MinorSegments = 18u,
                           const PrimitivePlacement& Placement     = {});

void AppendProceduralCone(FacetField&           InOutFacets,
                          double                BaseRadiusMeters,
                          double                HeightMeters,
                          uint32_t              RadialSegments = 28u,
                          const PrimitivePlacement& Placement      = {});

void AppendProceduralArchTunnel(FacetField&           InOutFacets,
                                double                InnerRadiusMeters,
                                double                WallThicknessMeters,
                                double                LengthMeters,
                                uint32_t              ArcSegments = 20u,
                                const PrimitivePlacement& Placement   = {});

void AppendProceduralGroundGrid(FacetField&           InOutFacets,
                                double                HalfExtentXMeters,
                                double                HalfExtentZMeters,
                                uint32_t              SubdivisionsX = 16u,
                                uint32_t              SubdivisionsZ = 16u,
                                const PrimitivePlacement& Placement     = {});

} // namespace Frontier
