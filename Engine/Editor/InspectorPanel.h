//============================================================================================================================================
//                                                      INSPECTORPANEL.H
//============================================================================================================================================
// 🧩 Development editor, right column — describes the picked record. Static sliders this step; nothing is bound yet.

#pragma once

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                     INSPECTOR PANEL
//------------------------------------------------------------------------------------------------------------------------

// The right dock column. Transform and surface controls for the picked record; the sliders move and hold, and
//    bind to the live scene in a later step. Nothing here allocates: every control writes a member.
class InspectorPanel
{
public:
    InspectorPanel() noexcept = default;
    ~InspectorPanel() noexcept = default;

    InspectorPanel(const InspectorPanel&)            = delete;
    InspectorPanel& operator=(const InspectorPanel&) = delete;

    // Records the panel into its dock column. Inert unless FRONTIER_DEVELOPMENT is defined.
    void Record() noexcept;

private:
    float Position[3]  = {};                        // [m]  picked record origin
    float Rotation[3]  = {};                        // [°]  picked record attitude
    float Scale[3]     = { 1.0f, 1.0f, 1.0f };      // [-]  picked record extent
    float Metallic     = 1.0f;                      // [-]  0 dielectric … 1 conductor
    float Roughness    = 0.25f;                     // [-]  0 mirror … 1 matte
    float ExposureBias = 0.0f;                      // [EV] picked record exposure trim
};

} // namespace Frontier
