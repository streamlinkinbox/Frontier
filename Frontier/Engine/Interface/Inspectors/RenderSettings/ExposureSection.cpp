/*==========================================================================================================================================
                                                            EXPOSURESECTION.CPP
==========================================================================================================================================*/
// 🧩 Exposure: eye adaptation and the exposure compensation.

#include "Engine/Interface/Inspectors/RenderSettings/RenderSettingsSections.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/InspectorControls.h"

#include <cstdio>

namespace Frontier
{

bool ExposureSection::Construct(ControlColumn& Page, RenderPipelineConfiguration& P, const RenderSettingsReading& Reading)
{
    using namespace SectionRows;
    char Status[48];
    std::snprintf(Status, sizeof(Status), "%s \xC2\xB7 %+.2f EV", P.AutoExposure ? "Auto" : "Fixed", P.ExposureCompensationEv);
    BeginSection(Page, "Exposure", "Gauge", Status, "The analytic illumination applies the compensation too.");
    bool Changed = Switch(Page, "AutoExposure", "Auto exposure", P.AutoExposure, RenderAvailability::Lighting(P), "Eye adaptation to the scene's luminance");
    Changed |= Amount(Page, "ExposureCompensation", "Exposure compensation", P.ExposureCompensationEv, -4.0f, 4.0f, 0.01f, "EV");
    (void)Reading;
    EndSection(Page);
    return Changed;
}

} // namespace Frontier

#endif
