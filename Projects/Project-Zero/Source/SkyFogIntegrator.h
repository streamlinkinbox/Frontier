//============================================================================================================================================
// 📦 Project-Zero/Source/SkyFogIntegrator.h — Celestial Sky And Fog Volume Integrator Over The Shipped Slang Core
//============================================================================================================================================

#pragma once

#include "../../../Engine/DeviceExchange/OrientationClassifier.h"
#include <cstdint>

namespace Frontier::ProjectZero {

//------------------------------------------------------------------------------------------------------------------------
//                                               SKY FOG INTEGRATOR
//------------------------------------------------------------------------------------------------------------------------

class SkyFogIntegrator
{
public:
    enum class FogScenario : uint32_t
    {
        Clear   = 0,                                                        // [scenario] thin global veil only
        Morning = 1,                                                        // [scenario] ground mist box plus veil
        Backlit = 2                                                         // [scenario] glowing local sphere plus veil
    };

    SkyFogIntegrator() noexcept;

    void                    AssignSunHour(double LocalHours) noexcept;
    void                    AssignFogScenario(FogScenario Scenario) noexcept;
    void                    AssignCloudShadow(bool Enabled, float CloudTimeSeconds, uint32_t CloudType, float Coverage, float Scale, float Base, float Thickness, float Density) noexcept;

    [[nodiscard]] Vector3   QuerySunDirectionRender() const noexcept;        // [-] unit sun vector, Y-up render frame
    [[nodiscard]] Vector3   QuerySunDirectionWorld() const noexcept;         // [-] unit sun vector, Z-up world frame
    [[nodiscard]] Vector3   QuerySunRadiance() const noexcept;              // [lux] atmosphere-attenuated sun
    [[nodiscard]] Vector3   QueryMoonDirectionRender() const noexcept;       // [-] unit moon vector, Y-up render frame
    [[nodiscard]] Vector3   QueryMoonDirectionWorld() const noexcept;        // [-] unit moon vector, Z-up world frame
    [[nodiscard]] Vector3   QueryMoonRadiance() const noexcept;             // [lux] showcase moonlight, bluish
    [[nodiscard]] Vector3   QuerySunFlareTint() const noexcept;           // [-] panel flare scale: colour * (intensity * 0.09)
    [[nodiscard]] Vector3   QueryFogLightDirection() const noexcept;         // [-] brighter luminaire, render frame
    [[nodiscard]] Vector3   QueryFogLightRadiance() const noexcept;          // [lux] brighter luminaire radiance
    [[nodiscard]] Vector3   QueryAmbientRadiance() const noexcept;          // [lux] three-sample sky ambient
    [[nodiscard]] float     QueryFogFarDistance() const noexcept;           // [m] march cutoff for sky rays
    [[nodiscard]] float     QueryCloudShadow(const Vector3& WorldPos, const Vector3& SunWorldDir) const noexcept; // [-] marched sun transmittance

    [[nodiscard]] Vector3   ComputeSkyRadiance(const Vector3& RenderDir) const noexcept;
    [[nodiscard]] Vector3   ApplyPanelPost(const Vector3& LinearColor, uint32_t PixelX, uint32_t PixelY, uint32_t ImageWidth, uint32_t ImageHeight) const noexcept;
    [[nodiscard]] Vector3   MarchFog(const Vector3& RenderOrigin, const Vector3& RenderDir, float TMin, float TMax, float LightVisibility, float& Transmittance) const noexcept;
    [[nodiscard]] Vector3   ApplyAerialPerspective(const Vector3& SurfaceColor, const Vector3& RenderOrigin, const Vector3& RenderDir, float Distance) const noexcept;

    [[nodiscard]] static Vector3 RenderFromWorld(const Vector3& World) noexcept; // (x, z, -y) rotation, Y-up render
    [[nodiscard]] static Vector3 WorldFromRender(const Vector3& Render) noexcept; // (x, -z, y) rotation, Z-up world
};

} // namespace Frontier::ProjectZero
