/*====================================================================================================================================
                                                    SUNINSPECTORDASHBOARD.H
====================================================================================================================================*/
// 🧩 Native C++/ImGui port of VehicleHMI's four-section Sun inspector dashboard.

#pragma once

#if defined(FRONTIER_WITH_INTERFACE_SDK)

namespace Frontier
{

struct SunPlacement;

// Draws the Overview, Daylight, Light & colour, and Disc & optics pages into the current Inspector child.
// All mutations stay on the record's existing SunPlacement; this page does not create a renderer or a second model.
void ConstructSunInspectorDashboard(SunPlacement& Placement);

} // namespace Frontier

#endif // FRONTIER_WITH_INTERFACE_SDK
