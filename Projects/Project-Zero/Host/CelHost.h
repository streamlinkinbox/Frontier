//================================================================================
// CelHost.h — shared Host helpers: panel-default params, panel camera math,
// PPM output. Used by SkyViewport / CpuPortDiff / ReSTIRConvergence.
//================================================================================
#ifndef PROJECT_ZERO_CEL_HOST_H
#define PROJECT_ZERO_CEL_HOST_H

#include "SlangCompat.h"
#include "CelestialCore.slang" // the SHIPPED shader core, compiled as C++
#include "SunPosition.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace ProjectZero {

// Panel defaults (the reference defaults() routine), in one place.
inline CelParams MakePanelParams(const SunState& sun) noexcept
{
    CelParams p;
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
    return p;
}

// The panel's own camera basis (frame(): yaw/pitch in degrees, fov in degrees).
inline CelCamera MakePanelCamera(double yawDeg, double pitchDeg, double fovDeg) noexcept
{
    const double yaw = yawDeg * kDeg2RadPanel;
    const double pitch = pitchDeg * kDeg2RadPanel;
    CelCamera c;
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
inline float3 PrimaryRay(const CelCamera& c, uint32_t x, uint32_t y,
                         uint32_t w, uint32_t h) noexcept
{
    const float2 uv = celViewportUV(uint(x), uint(y), uint(w), uint(h));
    return celViewportDir(c, uv);
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
