/*====================================================================================================================================
                                                ATMOSPHEREINSPECTORDASHBOARD.H
====================================================================================================================================*/
// Native C++/ImGui port of VehicleHMI's Atmosphere observatory dashboard.

#pragma once

#if defined(FRONTIER_WITH_INTERFACE_SDK)

namespace Frontier
{

struct AtmosphereParameters;

// Draws the source-ordered Overview, Medium, Light response and Baking pages into the current Inspector child.
// Controls bind only to the owning AtmosphereRecord's existing AtmosphereParameters and Enabled state.
void ConstructAtmosphereInspectorDashboard(AtmosphereParameters& Parameters, bool& Enabled);

#if defined(FRONTIER_WITH_WORKSPACE_CAPTURE)
// Selects a deterministic page before the native workspace evidence frame is rendered.
void ConfigureAtmosphereInspectorPageForCapture(int Page) noexcept;
#endif

} // namespace Frontier

#endif // FRONTIER_WITH_INTERFACE_SDK
