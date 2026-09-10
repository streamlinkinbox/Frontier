//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Interaction/CameraProjection.h — Z-up orbit camera: turntable, pan, dolly, ortho/perspective, canonical views
//============================================================================================================================================
// Blender / Plasticity conventions: turntable orbit about a pivot (yaw around world Z, pitch clamped short of the
//    poles), pan in the view plane, dolly toward the pivot, numpad views (1 front −Y, 3 right +X, 7 top +Z, Ctrl for
//    opposite), 5 toggles projection, fit-selected re-centres the pivot and fits the distance. Fills a ViewRecord.
#pragma once

#include "Kernel/VectorSpecification.h"
#include "Presentation/RasterExchange.h"

namespace Frontier
{

enum class CanonicalView : uint8_t { Front, Back, Right, Left, Top, Bottom, Isometric };

class CameraProjection
{
public:
    Vec3   Pivot     = {};                                                              // [m]
    double Yaw       = ScalarCriteria::Radians(-35.0);                                  // [rad] around +Z, 0 = looking along +Y
    double Pitch     = ScalarCriteria::Radians(30.0);                                   // [rad] above the ground plane
    double Distance  = 12.0;                                                            // [m]
    double FovY      = ScalarCriteria::Radians(42.0);                                   // [rad]
    bool   Orthographic = false;                                                        // [-]
    double NearPlane = 0.02;                                                            // [m] recomputed by Fit
    double FarPlane  = 2000.0;                                                          // [m]

    [[nodiscard]] Vec3 Eye() const noexcept;
    [[nodiscard]] Vec3 Forward() const noexcept;                                        // unit, eye → pivot
    [[nodiscard]] Vec3 Right() const noexcept;
    [[nodiscard]] Vec3 Up() const noexcept;
    [[nodiscard]] Mat4 ViewMatrix() const noexcept;
    [[nodiscard]] Mat4 ProjectionMatrix(double Aspect) const noexcept;
    [[nodiscard]] double OrthographicHalfHeight() const noexcept { return Distance * std::tan(FovY * 0.5); }

    void Orbit(double DeltaYaw, double DeltaPitch) noexcept;                            // [rad]
    void Pan(double DeltaRightPixels, double DeltaUpPixels, double ViewportHeight) noexcept;
    void Dolly(double Steps) noexcept;                                                  // wheel notches, ±
    void Look(CanonicalView View) noexcept;
    void Fit(const Box3& Bounds, double Aspect) noexcept;                             // fit bounds, keep orientation

    // World → pixel (top-left origin); returns false when behind the eye.
    [[nodiscard]] bool WorldToPixel(Vec3 P, double ViewportWidth, double ViewportHeight, double& PixelX, double& PixelY) const noexcept;
    // Pixel → world ray (perspective) or parallel ray (ortho). Pixel origin top-left, matches raster.
    [[nodiscard]] Ray PixelRay(double PixelX, double PixelY, double ViewportWidth, double ViewportHeight) const noexcept;

    [[nodiscard]] ViewRecord ToViewRecord(uint32_t Width, uint32_t Height, double LatticeCell = 1.0) const noexcept;
};

} // namespace Frontier
