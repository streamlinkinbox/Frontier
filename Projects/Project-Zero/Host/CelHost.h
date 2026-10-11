//================================================================================
// CelHost.h — shared Host helpers: panel-default params, panel camera math,
// PPM output. Used by SkyViewport / CpuPortDiff / ReSTIRConvergence.
//================================================================================
#ifndef PROJECT_ZERO_CEL_HOST_H
#define PROJECT_ZERO_CEL_HOST_H

#include "SlangInterchange.h"
#include "SkySpecification.slang" // the SHIPPED shader core, compiled as C++
#include "SunPosition.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace ProjectZero {

// Panel defaults (the reference defaults() routine), in one place.
inline SkyConfiguration MakePanelParams(const SunState& sun) noexcept
{
    SkyConfiguration p;
    p.sunDir = float3(float(sun.dirX), float(sun.dirY), float(sun.dirZ));
    p.sunColor = float3(float(sun.colorR), float(sun.colorG), float(sun.colorB));
    p.sunElevationDeg = float(sun.elevationDeg);
    p.sunIntensity = 22.0f;
    p.sunAngularRadius = 0.53f * 3.14159265f / 180.0f / 2.0f; // uSunAng
    p.sunSoftness = 0.25f;
    p.sunDiscBoost = 12.0f;
    p.rayleigh = 1.0f;
    p.mie = 1.0f;
    p.mieG = 0.78f;
    p.ozone = 1.2f;
    p.planetRadius = 6371000.0f;
    p.atmoHeight = 100000.0f;
    p.rayleighH = 8000.0f;
    p.mieH = 1200.0f;
    p.skyTint = float3(1.0f, 1.0f, 1.0f); // 1-(1-v)*.35 of #ffffff
    p.skyBright = 1.0f;
    p.groundAlbedo = float3(0.16862746f, 0.16078432f, 0.14117648f); // #2b2924
    p.groundBright = 1.0f;
    p.dawnIntensity = 1.0f;
    p.lineIntensity = 1.0f;
    p.lineAuto = 1u;
    p.exposureStops = 0.4f;
    p.tonemap = 1u;
    p.bloom = 1.0f;
    p.vignette = 0.28f;
    p.grain = 0.1f;
    p.camHeight = 2.0f;
    p.timeSeconds = 0.5f; // fract * 100 = 50, the oracle's grain seed
    p.fogOn = 0u; // the panel hardcodes uFogOn = 0
    p.fogDensity = 0.011f;
    p.fogHeight = 42.0f;
    p.fogScatter = 0.7f;
    p.fogColor = float3(0.5607843f, 0.6431373f, 0.73333335f); // #8fa4bb
    p.afOn = 1u;
    p.afDensity = 7.0f * 1e-5f;
    p.afHeight = 1200.0f;
    p.afStart = 0.0f;
    p.afMie = 0.35f;
    p.afG = 0.7f;
    p.afSky = 1.0f;
    p.afTint = float3(1.0f, 1.0f, 1.0f);
    p.skyAmb = SkyAmbientCompute(p); // the panel's 3-sample probe pass
    p.starsOn = 1u;
    p.starDensity = 1.0f;
    p.starBright = 1.0f;
    p.starSize = 1.0f;
    p.starGlow = 0.8f;
    p.starTwinkle = 0.5f;
    p.starColor = 1.0f;
    p.starLimit = 1.5f;
    p.starMilky = 1.0f;
    p.starRot = 40.0f * 3.14159265f / 180.0f;
    p.starTilt = -62.0f * 3.14159265f / 180.0f;
    p.starLayers = 3.0f; // High capped by the load-time Standard tier
    p.starAA = 1.0f; // High capped by the load-time Standard tier
    // Panel JS probe line: 8 * 2.6e-3 * (bright || 1) * 3.2 * 1.6.
    p.starCeil = 8.0f * 2.6e-3f * (p.starBright != 0.0f ? p.starBright : 1.0f)
               * 3.2f * 1.6f;
    p.pixAngle = 0.0f; // view-dependent: the caller sets 2*tanHalf/resY
    p.moonOn = 0u; // showcase-only; the mirror gates keep the moon off
    p.moonDir = float3(0.6113f, 0.6891f, 0.3890f); // norm(0.55,0.62,0.35)
    p.moonBright = 2.0f;
    p.cloudOn = 0u; // showcase-only; the mirror gates keep clouds off
    p.cloudCoverage = 0.45f;
    p.cloudScale = 2.0f;
    return p;
}

// The panel's own camera basis (frame(): yaw/pitch in degrees, fov in degrees).
inline SkyProjection MakePanelCamera(double yawDeg, double pitchDeg, double fovDeg) noexcept
{
    const double yaw = yawDeg * kDeg2RadPanel;
    const double pitch = pitchDeg * kDeg2RadPanel;
    SkyProjection c;
    c.fwd = float3(float(std::sin(yaw) * std::cos(pitch)), float(std::sin(pitch)),
                   float(-std::cos(yaw) * std::cos(pitch)));
    c.right = float3(float(std::cos(yaw)), 0.0f, float(std::sin(yaw)));
    c.up = float3(float(-std::sin(pitch) * std::sin(yaw)), float(std::cos(pitch)),
                  float(std::sin(pitch) * std::cos(yaw)));
    c.tanHalf = float(std::tan(fovDeg * 0.5 * kDeg2RadPanel));
    return c;
}

// Primary ray for pixel (x, y), via the shipped core's own viewport helpers
// (panel lines 1139-1140) — the harness cannot drift from the shader.
inline float3 PrimaryRay(const SkyProjection& c, uint32_t x, uint32_t y,
                         uint32_t w, uint32_t h) noexcept
{
    const float2 uv = ViewportUVCompute(uint(x), uint(y), uint(w), uint(h));
    return ViewportDirectionCompute(c, uv);
}

inline bool WritePpm(const std::string& path, uint32_t w, uint32_t h,
                     const std::vector<float3>& rgb) noexcept
{
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f)
    {
        return false;
    }
    std::fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (const float3& c : rgb)
    {
        const auto q = [](float v) -> unsigned char
        {
            v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
            return static_cast<unsigned char>(v * 255.0f);
        };
        const unsigned char px[3] = { q(c.x), q(c.y), q(c.z) };
        std::fwrite(px, 1, 3, f);
    }
    std::fclose(f);
    return true;
}

} // namespace ProjectZero

#endif // PROJECT_ZERO_CEL_HOST_H
