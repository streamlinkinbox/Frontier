/*====================================================================================================================================
                                                    CLOUDINSPECTORDASHBOARD.H
====================================================================================================================================*/
// Native C++/ImGui port of VehicleHMI's Clouds observatory dashboard.

#pragma once

#if defined(FRONTIER_WITH_INTERFACE_SDK)

namespace Frontier
{

struct CloudParameters;

// Draws the source-ordered Overview, Formation, Rendering and Weather pages into the current Inspector child.
// Controls bind only to the owning CloudRecord's existing CloudParameters and Enabled state.
void ConstructCloudInspectorDashboard(CloudParameters& Parameters, bool& Enabled);

#if defined(FRONTIER_WITH_WORKSPACE_CAPTURE)
// Selects a deterministic page before the native workspace evidence frame is rendered.
void ConfigureCloudInspectorPageForCapture(int Page) noexcept;
#endif

} // namespace Frontier

#endif // FRONTIER_WITH_INTERFACE_SDK
