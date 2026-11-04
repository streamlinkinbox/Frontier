/*==========================================================================================================================================
                                                        ILLUMINATIONCONSTANTS.H
==========================================================================================================================================*/
// 🧩 Quality grades, material family anchors and the host scene constants handed to every program (IlluminationHost.js).

#pragma once

#include "DeviceExchange/DeviceExchange.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier::GlobalIllumination
{

// 📝 Quality grades trade cascade resolution, card density and march length (?quality=low|medium|high in the demo).
struct QualityGrade
{
    uint32_t ClipResolution = 128;
    double   FinestVoxel    = 0.125;   // [m]
    uint32_t CardExtent     = 2048;    // [texel]
    double   CardDensity    = 1.0;     // [-]
    double   VoxelScale     = 1.0;     // [-]
    uint32_t ShadowExtent   = 2048;    // [texel]
    uint32_t Steps          = 96;      // [-]
    double   RenderScale    = 1.0;     // [-]
    uint32_t TileBudget     = 2048;    // [-]
    uint32_t PixelLimit     = 1920 * 1080;
};

[[nodiscard]] QualityGrade ResolveQualityGrade(const std::string& Name);

// 📝 CourseMaterial family indices of MaterialSpecification.js that the programs test against.
namespace CourseFamily
{
constexpr uint32_t Surround      = 2;
constexpr uint32_t VehicleGlass  = 7;
constexpr uint32_t Tyre          = 9;
constexpr uint32_t TyreRubber    = 32;
constexpr uint32_t ConeOrange    = 60;
constexpr uint32_t ConeBand      = 61;
constexpr uint32_t ConeBase      = 62;
constexpr uint32_t DerezGlow     = 63;
constexpr uint32_t DerezHot      = 64;
constexpr uint32_t DerezGlitch   = 65;
constexpr uint32_t CabinLeather  = 70;
constexpr uint32_t SteeringWheel = 73;
constexpr uint32_t DisplayGlass  = 74;
constexpr uint32_t MirrorGlass   = 75;
constexpr uint32_t SeatFabric    = 76;
constexpr uint32_t RimScrape     = 79;
constexpr uint32_t TabletHousing = 80;
constexpr uint32_t MarkedFirst      = 26;
constexpr uint32_t MarkedLast       = 31;
constexpr uint32_t MarkedExtraFirst = 58;
constexpr uint32_t MarkedExtraLast  = 59;
} // namespace CourseFamily

// 📝 TyreMarkLayout of TyreMarkDepot.js (the deposit the surface raster reads for the road families).
struct TyreMarkLayoutSpecification
{
    double OriginX    = -64.0;    // [m]
    double OriginY    = -64.0;    // [m]
    double ExtentX    = 224.0;    // [m]
    double ExtentY    = 128.0;    // [m]
    double PatchWidth = 0.27;     // [m]
};
inline constexpr TyreMarkLayoutSpecification TyreMarkLayout{};

struct SceneConstantInputs
{
    uint32_t MaximumMarchSteps   = 96;
    uint32_t GroundFirstCard     = 0;
    uint32_t GroundStripCard     = 0;
    uint32_t GroundOccupantIndex = 0;
};

// 📝 The specialization values in HostConstants.slang order (constant_id = position).
[[nodiscard]] std::vector<SpecializationConstant> ConstructSpecializationConstants(const SceneConstantInputs& Inputs);

} // namespace Frontier::GlobalIllumination
