/*====================================================================================================================================
                                                    SUNINSPECTORDASHBOARD.H
====================================================================================================================================*/
// 🧩 Native C++/ImGui port of VehicleHMI's four-section Sun inspector dashboard.

#pragma once

#if defined(FRONTIER_WITH_INTERFACE_SDK)

#include <functional>

namespace Frontier
{

struct SunPlacement;

// Draws the Overview, Daylight, Light & colour, and Disc & optics pages into the current Inspector child.
// All mutations stay on the record's existing SunPlacement; this page does not create a renderer or a second model.
// The source "Open Lens Flare" action delegates selection to the Inspector host.
void ConstructSunInspectorDashboard(SunPlacement& Placement, const std::function<void()>& OpenLensFlare);

#if defined(FRONTIER_WITH_WORKSPACE_CAPTURE)
// Selects a deterministic page before the native workspace evidence frame is rendered.
void ConfigureSunInspectorPageForCapture(int Page) noexcept;
#endif

} // namespace Frontier

#endif // FRONTIER_WITH_INTERFACE_SDK
