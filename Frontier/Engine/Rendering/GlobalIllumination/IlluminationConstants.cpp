/*==========================================================================================================================================
                                                       ILLUMINATIONCONSTANTS.CPP
==========================================================================================================================================*/
// 🧩 Quality grades, material family anchors and the host scene constants handed to every program (IlluminationHost.js).

#include "IlluminationConstants.h"

#include "SceneStructure.h"

namespace Frontier::GlobalIllumination
{

QualityGrade ResolveQualityGrade(const std::string& Name)
{
    if (Name == "medium")
    {
        return {96, 0.16, 2048, 0.75, 1.4, 2048, 80, 0.85, 1024, 1600 * 900};
    }
    if (Name == "low")
    {
        return {64, 0.25, 1024, 0.5, 2.2, 1024, 64, 0.6, 384, 1280 * 720};
    }
    return {128, 0.125, 2048, 1.0, 1.0, 2048, 96, 1.0, 2048, 1920 * 1080};
}

std::vector<SpecializationConstant> ConstructSpecializationConstants(const SceneConstantInputs& Inputs)
{
    using V = SpecializationConstant;
    return {
        V::FromUnsigned(Inputs.MaximumMarchSteps),
        V::FromFloat(static_cast<float>(GroundLayout.PadMinimumX)),
        V::FromFloat(static_cast<float>(GroundLayout.PadMinimumY)),
        V::FromFloat(static_cast<float>(GroundLayout.TileExtent)),
        V::FromUnsigned(GroundLayout.TileCountX),
        V::FromUnsigned(GroundLayout.TileCountY),
        V::FromUnsigned(Inputs.GroundFirstCard),
        V::FromUnsigned(Inputs.GroundStripCard),
        V::FromUnsigned(Inputs.GroundOccupantIndex),
        V::FromUnsigned(CourseFamily::Surround),
        V::FromFloat(static_cast<float>(TyreMarkLayout.OriginX)),
        V::FromFloat(static_cast<float>(TyreMarkLayout.OriginY)),
        V::FromFloat(static_cast<float>(TyreMarkLayout.ExtentX)),
        V::FromFloat(static_cast<float>(TyreMarkLayout.ExtentY)),
        V::FromFloat(static_cast<float>(TyreMarkLayout.PatchWidth)),
        V::FromUnsigned(CourseFamily::RimScrape),
        V::FromUnsigned(CourseFamily::TyreRubber),
        V::FromUnsigned(CourseFamily::MarkedFirst),
        V::FromUnsigned(CourseFamily::MarkedLast),
        V::FromUnsigned(CourseFamily::MarkedExtraFirst),
        V::FromUnsigned(CourseFamily::MarkedExtraLast),
        V::FromUnsigned(CourseFamily::DerezGlow),
        V::FromUnsigned(CourseFamily::DerezHot),
        V::FromUnsigned(CourseFamily::DerezGlitch),
        V::FromUnsigned(CourseFamily::VehicleGlass),
        V::FromUnsigned(CourseFamily::SteeringWheel),
        V::FromUnsigned(CourseFamily::TabletHousing),
        V::FromUnsigned(CourseFamily::DisplayGlass),
        V::FromUnsigned(CourseFamily::Tyre),
        V::FromUnsigned(CourseFamily::CabinLeather),
        V::FromUnsigned(CourseFamily::DisplayGlass),
        V::FromUnsigned(CourseFamily::SeatFabric),
        V::FromUnsigned(CourseFamily::MirrorGlass),
        V::FromUnsigned(CourseFamily::ConeOrange),
        V::FromUnsigned(CourseFamily::ConeBand),
        V::FromUnsigned(CourseFamily::ConeBase),
    };
}

} // namespace Frontier::GlobalIllumination
