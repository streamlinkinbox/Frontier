#pragma once
/*==========================================================================================================================================
                                                            PYROCAMERA.H
==========================================================================================================================================*/
// 🧩 The orbit camera of the Fluid volume and the basis it resolves to: plain data and arithmetic, so the editor's interface (which must not see the
//    Vulkan layer) and the solver share one definition. A port of the prototype's camera.js OrbitCamera, number for number: the orbit, pan and zoom
//    rates, the limits, the smoothing toward an aim, the ray through the volume's box and the projection of its bounds (BoundsProjection.js).
//    Experimental/Fluid/src/camera.js is the reference; PyroVolumeValidation replays a scripted trace of it.

#include <array>
#include <vector>

namespace Frontier
{

//----------------------------------------------------------------------------------------------------------------------
//                                                   ORBIT CAMERA
//----------------------------------------------------------------------------------------------------------------------

struct PyroCameraBasis
{
    std::array<float, 3> Position   = { 0.0f, 1.2f, 3.0f };
    std::array<float, 3> Forward    = { 0.0f, 0.0f, -1.0f };
    std::array<float, 3> Right      = { 1.0f, 0.0f, 0.0f };
    std::array<float, 3> Up         = { 0.0f, 1.0f, 0.0f };
    float                FovRadians = 0.80285146f;   // [rad] - 46 deg
    float                Aspect     = 1.7777778f;    // [-]
    float                Near       = 0.05f;         // [m]
};

class PyroOrbitCamera
{
  public:
    // [unit] - where the camera is: it eases toward the aim below at 16 a second (Advance)
    float                Theta      = 0.62f;                    // [rad] - azimuth
    float                Phi        = 1.26f;                    // [rad] - polar angle from +Y
    float                Distance   = 3.35f;                    // [m]
    std::array<float, 3> Target     = { 0.0f, 0.22f, 0.0f };    // [m]

    // [unit] - where the camera is going: what the pointer, the keys and the views change
    float                TargetTheta    = 0.62f;                // [rad]
    float                TargetPhi      = 1.26f;                // [rad]
    float                TargetDistance = 3.35f;                // [m]
    std::array<float, 3> TargetCentre   = { 0.0f, 0.22f, 0.0f };// [m]

    float                FovDegrees = 46.0f;                    // [deg]
    float                Aspect     = 16.0f / 9.0f;             // [-]
    float                Near       = 0.05f;                    // [m]

    // [unit] - OrbitCamera.reset: the aim returns to the defaults (the view eases there)
    void Reset() noexcept;
    // [unit] - the view is the aim at once (a new OrbitCamera, or update(1))
    void Snap() noexcept;
    // [unit] - FluidPanel.FocusVolume: reset, then stand back 2.3 widths (or heights) of the domain, at most 9.5 m
    void FocusVolume(float Width, float Height) noexcept;
    // [unit] - the camera-view select: 0 perspective (FocusVolume), 1 front, 2 side, 3 top
    void ApplyPresetView(int ViewOrdinal, float Width, float Height) noexcept;
    void OrbitByPixels(float DeltaX, float DeltaY) noexcept;
    void PanByPixels(float DeltaX, float DeltaY) noexcept;
    // [unit] - DeltaPixels is the browser's wheel delta, already limited to -100 to 100: positive zooms out
    void ZoomByWheel(float DeltaPixels) noexcept;
    // [unit] - the turntable: the aim turns 0.35 rad a second, a long refresh counted as 0.05 s
    void AdvanceTurntable(float DtSeconds) noexcept;
    // [unit] - OrbitCamera.update(dt): the view eases toward the aim by min(1, 16 dt)
    void Advance(float DtSeconds) noexcept;

    [[nodiscard]] PyroCameraBasis ComputeBasis(float AspectOverride = 0.0f) const noexcept;
    // [unit] - raycastVolumeUVW: the point 45% of the way through the box along the ray, in the box's own 0 to 1, held inside 0.06-0.94 across and
    //          0.05-0.94 up; false when the ray misses the box
    [[nodiscard]] bool RaycastVolumeUvw(float NdcX, float NdcY, const std::array<float, 3>& BoxMin, const std::array<float, 3>& BoxMax,
                                        std::array<float, 3>& OutUvw) const noexcept;
};

// [unit] - BoundsProjection.js ProjectBounds: the box's twelve edges as screen segments x1 y1 x2 y2 in a Width by Height picture, clipped at the near plane
void ProjectBounds(const PyroCameraBasis& Basis, const std::array<float, 3>& BoxMin, const std::array<float, 3>& BoxMax, float Width, float Height,
                   std::vector<std::array<float, 4>>& OutSegments);

} // namespace Frontier
