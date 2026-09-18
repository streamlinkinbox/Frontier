//==============================================================================
// FogViewport — renders an outdoor courtyard (ground + block + sky + fog) on
// the CPU through the shipped Sky/FogSpecification.slang sources.
//
// Per pixel: primary ray -> ground/box intersect -> ReSTIR-DI-lit fog march
// (sun = winning light) -> lambertian surface + FogAtmosphereApply, or sky
// miss -> SkyRadianceCompute attenuated by the march. Uniform SkyPostApply.
// Usage:
//   FogViewport --sun 6.4 --yaw 35 --pitch -6 --cam 0,2.2,14 --fog morning
//       --width 480 --height 270 --out frame.ppm [--grain 0.1]
// Scenarios: clear | morning | backlit.
//==============================================================================
#include "CelHost.h"

#include "FogSpecification.slang"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

struct Args
{
    double sun = 6.4;
    double yaw = 35.0;
    double pitch = -6.0;
    float3 camPos = float3(0.0f, 2.2f, 14.0f);
    std::string fog = "morning";
    uint32_t width = 480;
    uint32_t height = 270;
    double fov = 60.0;
    float grain = 0.1f;
    std::string out = "frame.ppm";
};

bool Parse(int argc, char** argv, Args& a) noexcept
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string k = argv[i];
        std::string v;
        auto need = [&]() -> bool
        {
            if (++i >= argc)
            {
                return false;
            }
            v = argv[i];
            return true;
        };
        if (k == "--sun" && need())
        {
            a.sun = std::stod(v);
        }
        else if (k == "--yaw" && need())
        {
            a.yaw = std::stod(v);
        }
        else if (k == "--pitch" && need())
        {
            a.pitch = std::stod(v);
        }
        else if (k == "--cam" && need())
        {
            std::sscanf(v.c_str(), "%f,%f,%f", &a.camPos.x, &a.camPos.y,
                        &a.camPos.z);
        }
        else if (k == "--fog" && need())
        {
            a.fog = v;
        }
        else if (k == "--width" && need())
        {
            a.width = uint32_t(std::stoul(v));
        }
        else if (k == "--height" && need())
        {
            a.height = uint32_t(std::stoul(v));
        }
        else if (k == "--fov" && need())
        {
            a.fov = std::stod(v);
        }
        else if (k == "--grain" && need())
        {
            a.grain = float(std::stod(v));
        }
        else if (k == "--out" && need())
        {
            a.out = v;
        }
        else
        {
            return false;
        }
    }
    return true;
}

struct SurfaceHit
{
    bool hit = false;
    float t = 1e30f;
    float3 normal = float3(0.0f, 1.0f, 0.0f);
    float3 albedo = float3(0.3f, 0.28f, 0.24f);
};

SurfaceHit IntersectCourtyard(const float3& origin, const float3& dir,
                              const float3& boxCenter, const float3& boxExt)
{
    SurfaceHit h;
    if (dir.y < -1e-9f)
    {
        const float t = -origin.y / dir.y;
        if (t > 0.0f && t < h.t)
        {
            h.hit = true;
            h.t = t;
            h.normal = float3(0.0f, 1.0f, 0.0f);
            h.albedo = float3(0.30f, 0.28f, 0.24f);
        }
    }
    const float2 seg =
        FogBoxIntersect(origin, dir, boxCenter, boxExt);
    if (seg.x < seg.y && seg.y > 0.0f)
    {
        const float t = seg.x > 0.0f ? seg.x : seg.y;
        if (t > 0.0f && t < h.t)
        {
            const float3 p = origin + dir * t;
            const float3 q = (p - boxCenter) / boxExt;
            const float ax =
                (std::fabs(q.x) >= std::fabs(q.y) &&
                 std::fabs(q.x) >= std::fabs(q.z))
                ? 1.0f
                : 0.0f;
            const float az =
                (ax == 0.0f && std::fabs(q.z) >= std::fabs(q.y)) ? 1.0f : 0.0f;
            const float ay = (ax == 0.0f && az == 0.0f) ? 1.0f : 0.0f;
            const float3 n =
                float3(ax * (q.x >= 0.0f ? 1.0f : -1.0f),
                       ay * (q.y >= 0.0f ? 1.0f : -1.0f),
                       az * (q.z >= 0.0f ? 1.0f : -1.0f));
            h.hit = true;
            h.t = t;
            h.normal = n;
            h.albedo = float3(0.55f, 0.53f, 0.50f);
        }
    }
    return h;
}

bool OccludedByBox(const float3& origin, const float3& dir, float maxT,
                   const float3& boxCenter, const float3& boxExt)
{
    const float2 seg = FogBoxIntersect(origin, dir, boxCenter, boxExt);
    return seg.x < seg.y && seg.y > 0.0f && seg.x < maxT;
}

FogConfiguration FogScenario(const std::string& name, const float3& ambient)
{
    FogConfiguration c = FogConfigurationMake(0.0006f, 0.15f, 0.0f,
        float3(0.75f, 0.78f, 0.82f), 0.55f, ambient, 0.0f, 12u, 400.0f);
    if (name == "morning")
    {
        c.globalDensity = 0.004f;
        c.globalHeightFalloff = 0.25f;
        c.volumeCount = 1u;
        c.volumes[0] = FogVolumeMake(FogShapeBox, float3(0.0f, 1.0f, 2.0f),
            float3(12.0f, 1.2f, 10.0f), 0.020f, 0.35f,
            float3(0.80f, 0.82f, 0.85f), 0.55f, float3(0.0f, 0.0f, 0.0f),
            0.6f, 0.15f);
    }
    else if (name == "backlit")
    {
        c.globalDensity = 0.0015f;
        c.volumeCount = 1u;
        c.volumes[0] = FogVolumeMake(FogShapeSphere, float3(10.0f, 3.0f, 5.1f),
            float3(5.0f, 5.0f, 5.0f), 0.030f, 0.10f,
            float3(0.85f, 0.82f, 0.78f), 0.70f, float3(0.0f, 0.0f, 0.0f),
            0.5f, 0.20f);
    }
    return c;
}

} // namespace

int main(int argc, char** argv)
{
    Args a;
    if (!Parse(argc, argv, a))
    {
        std::fprintf(stderr, "usage: FogViewport --sun H --yaw Y --pitch P --cam X,Y,Z "
                             "--fog clear|morning|backlit --width W --height H --out F.ppm "
                             "[--fov F] [--grain G]\n");
        return 2;
    }
    const ProjectZero::SunState sun =
        ProjectZero::SolveSun(a.sun, -26.0, 0.0, 5800.0);
    SkyConfiguration p = ProjectZero::MakePanelParams(sun);
    p.grain = a.grain;
    const SkyProjection cam =
        ProjectZero::MakePanelCamera(a.yaw, a.pitch, a.fov);
    p.pixAngle = 2.0f * cam.tanHalf / float(a.height);
    const float3 ambient = SkyAmbientCompute(p);
    FogConfiguration fog = FogScenario(a.fog, ambient);
    // Sun radiance toward the fog: the panel's own view-path transmittance.
    const float3 roSky =
        float3(0.0f, p.planetRadius + 2.0f, 0.0f);
    const AtmosphereStructure fSun =
        AtmosphereCompute(roSky, p.sunDir, p);
    // Same sun that lights the surfaces (sunIntensity, no disc boost: the disc
    // is sub-pixel for volumes, and surfaces use the identical factor).
    const float3 sunRadiance = fSun.trans * p.sunColor * p.sunIntensity;
    const float3 boxCenter = float3(3.0f, 1.5f, 2.0f);
    const float3 boxExt = float3(1.5f, 1.5f, 1.5f);
    std::printf("sun %.2fh elev %.3f fog %s %ux%u\n", a.sun,
                sun.elevationDeg, a.fog.c_str(), a.width, a.height);

    std::vector<float3> img(size_t(a.width) * a.height);
    for (uint32_t y = 0; y < a.height; ++y)
    {
        for (uint32_t x = 0; x < a.width; ++x)
        {
            const float3 dir =
                ProjectZero::PrimaryRay(cam, x, y, a.width, a.height);
            const SurfaceHit h =
                IntersectCourtyard(a.camPos, dir, boxCenter, boxExt);
            const float tMax = h.hit ? h.t : fog.maxMarchDistance;
            const float3 mid = a.camPos + dir * (tMax * 0.5f);
            const float vis = OccludedByBox(mid, p.sunDir, 1e30f, boxCenter,
                                            boxExt)
                ? 0.0f
                : 1.0f;
            const FogMarchStructure march = FogMarchVolumes(
                a.camPos, dir, 0.0f, tMax, p.sunDir, sunRadiance, vis, fog);
            float3 col;
            if (h.hit)
            {
                const float3 hp = a.camPos + dir * h.t;
                const float svis =
                    OccludedByBox(hp + h.normal * 1e-3f, p.sunDir, 1e30f,
                                  boxCenter, boxExt)
                    ? 0.0f
                    : 1.0f;
                const float ndl =
                    max(dot(h.normal, p.sunDir), 0.0f) * svis;
                // NOTE: surfaces share the fog's atmosphere-attenuated
                // sunRadiance (trans * sunColor * sunIntensity): the direct
                // sun crosses ~10 air masses at this elevation.
                const float3 shaded =
                    h.albedo * (sunRadiance * ndl + ambient);
                const float facing = SunFacingCompute(dir, p.sunDir);
                const float3 hzGlow = DawnGlowCompute(dir, p.sunElevationDeg,
                                                      facing, p);
                const float3 atmos = FogAtmosphereApply(shaded, a.camPos, dir,
                                                        h.t, hzGlow, p);
                col = march.scatter + atmos * march.transmittance;
            }
            else
            {
                const float3 sky = SkyRadianceCompute(dir, p);
                col = march.scatter + sky * march.transmittance;
            }
            const float2 uv = ViewportUVCompute(uint(x), uint(y),
                                                uint(a.width), uint(a.height));
            img[size_t(y) * a.width + x] = SkyPostApply(
                col, uv.x, uv.y, float(a.width), float(a.height), uint(x),
                uint(y), p);
        }
    }
    if (!ProjectZero::WritePpm(a.out, a.width, a.height, img))
    {
        std::fprintf(stderr, "cannot write %s\n", a.out.c_str());
        return 1;
    }
    std::printf("wrote %s\n", a.out.c_str());
    return 0;
}
