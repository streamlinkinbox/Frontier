#pragma once
/*==========================================================================================================================================
                                                            PYROCAMERA.H
==========================================================================================================================================*/
// 🧩 The orbit camera of the Fluid volume and the frame it resolves to: plain data and arithmetic, so the editor's interface (which must not see
//    the Vulkan layer) and the solver share one definition. Moved out of PyroVolumeSolver.h unchanged.

#include <array>

namespace Frontier
{

//----------------------------------------------------------------------------------------------------------------------
//                                                   ORBIT CAMERA
//----------------------------------------------------------------------------------------------------------------------

struct PyroCameraFrame
{
    std::array<float, 3> Position   = { 0.0f, 1.2f, 3.0f };
    std::array<float, 3> Forward    = { 0.0f, 0.0f, -1.0f };
    std::array<float, 3> Right      = { 1.0f, 0.0f, 0.0f };
    std::array<float, 3> Up         = { 0.0f, 1.0f, 0.0f };
    float                FovRadians = 0.80285146f;   // [rad] - 46 deg
    float                Aspect     = 1.7777778f;    // [-]
};

class PyroOrbitCamera
{
  public:
    float                Theta      = 0.62f;                    // [rad]
    float                Phi        = 1.18f;                    // [rad]
    float                Distance   = 3.15f;                    // [m]
    std::array<float, 3> Target     = { 0.0f, 0.35f, 0.0f };    // [m]
    float                FovDegrees = 46.0f;                    // [deg]
    float                Aspect     = 16.0f / 9.0f;             // [-]
    bool                 AutoRotate = false;                    // [-]

    void Reset() noexcept;
    void ApplyPresetView(int ViewOrdinal) noexcept;
    void OrbitByPixels(float DeltaX, float DeltaY) noexcept;
    void PanByPixels(float DeltaX, float DeltaY) noexcept;
    void ZoomByWheel(float WheelSteps) noexcept;
    void AdvanceTurntable(float DtSeconds) noexcept;

    [[nodiscard]] PyroCameraFrame ComputeFrame(float AspectOverride = 0.0f) const noexcept;
    [[nodiscard]] bool RaycastBrushPoint(float NdcX, float NdcY, std::array<float, 3>& OutWorldPoint) const noexcept;
};

} // namespace Frontier
