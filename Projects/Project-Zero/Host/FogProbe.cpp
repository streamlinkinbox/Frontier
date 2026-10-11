//==============================================================================
// FogProbe — samples the shipped FogSpecification.slang fog math on the CPU.
//
// Writes a CSV row per sample: fog config + ray + light in, density/march out.
// RunFogParity.py transcribes the same math in numpy (F1 density, F2 march).
// Usage: FogProbe --rows 2000 --out fog.csv
//==============================================================================
#include "CelHost.h"

#include "FogSpecification.slang"

#include <cstdio>
#include <cstring>
#include <random>
#include <string>

namespace {

struct Args
{
    uint32_t rows = 2000;
    std::string out = "fog.csv";
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
        if (k == "--rows" && need())
        {
            a.rows = uint32_t(std::stoul(v));
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

float U(std::mt19937& rng, float lo, float hi)
{
    return lo + (hi - lo) * float(rng()) / 4294967296.0f;
}

float3 USphere(std::mt19937& rng)
{
    for (;;)
    {
        const float3 v = float3(U(rng, -1.0f, 1.0f), U(rng, -1.0f, 1.0f),
                                U(rng, -1.0f, 1.0f));
        const float l2 = dot(v, v);
        if (l2 > 0.04f && l2 <= 1.0f)
        {
            return v / std::sqrt(l2);
        }
    }
}

void WriteVolume(std::FILE* f, const FogVolumeStructure& v)
{
    std::fprintf(f, ",%u,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,"
                    "%.9g,%.9g,%.9g,%.9g,%.9g,%.9g",
                 v.shape, v.center.x, v.center.y, v.center.z, v.extents.x,
                 v.extents.y, v.extents.z, v.density, v.heightFalloff,
                 v.albedo.x, v.albedo.y, v.albedo.z, v.phaseG, v.emissive.x,
                 v.emissive.y, v.emissive.z, v.noiseAmount, v.noiseScale);
}

FogVolumeStructure RandomVolume(std::mt19937& rng)
{
    const uint shape = (rng() % 2u == 0u) ? FogShapeBox : FogShapeSphere;
    const float3 c = float3(U(rng, -15.0f, 15.0f), U(rng, -2.0f, 10.0f),
                             U(rng, -15.0f, 15.0f));
    const float3 e = shape == FogShapeBox
        ? float3(U(rng, 1.0f, 8.0f), U(rng, 0.5f, 4.0f), U(rng, 1.0f, 8.0f))
        : float3(U(rng, 1.0f, 8.0f), 1.0f, 1.0f);
    return FogVolumeMake(shape, c, e, std::exp(U(rng, -4.0f, -1.0f)),
                         U(rng, 0.0f, 0.6f),
                         float3(U(rng, 0.3f, 1.0f), U(rng, 0.3f, 1.0f),
                                U(rng, 0.3f, 1.0f)),
                         U(rng, -0.2f, 0.8f),
                         float3(U(rng, 0.0f, 2.0f), U(rng, 0.0f, 2.0f),
                                U(rng, 0.0f, 2.0f)),
                         U(rng, 0.0f, 0.8f), U(rng, 0.05f, 0.5f));
}

} // namespace

int main(int argc, char** argv)
{
    Args a;
    if (!Parse(argc, argv, a))
    {
        std::fprintf(stderr, "usage: FogProbe --rows N --out F.csv\n");
        return 2;
    }
    std::FILE* f = std::fopen(a.out.c_str(), "w");
    if (!f)
    {
        std::fprintf(stderr, "cannot write %s\n", a.out.c_str());
        return 1;
    }
    std::fprintf(f, "# globalDensity,globalFalloff,globalBase,globalAlbedo(3),globalG,"
                    "ambient(3),time,marchSteps,maxMarch,volumeCount,vol0(18),vol1(18),"
                    "origin(3),dir(3),tMin,tMax,lightDir(3),lightRadiance(3),lightVis,"
                    "densityOrigin,densityMid,scatter(3),tr\n");
    std::mt19937 rng(777u);
    const uint stepset[4] = {4u, 8u, 12u, 16u};
    for (uint32_t r = 0; r < a.rows; ++r)
    {
        const float gd = (rng() % 4u == 0u) ? 0.0f
                                            : std::exp(U(rng, -9.0f, -4.0f));
        FogConfiguration cfg = FogConfigurationMake(
            gd, U(rng, 0.02f, 0.6f), U(rng, -2.0f, 4.0f),
            float3(U(rng, 0.3f, 1.0f), U(rng, 0.3f, 1.0f), U(rng, 0.3f, 1.0f)),
            U(rng, -0.2f, 0.8f),
            float3(U(rng, 0.0f, 1.5f), U(rng, 0.0f, 1.5f), U(rng, 0.0f, 1.5f)),
            U(rng, 0.0f, 100.0f), stepset[rng() % 4u], U(rng, 50.0f, 300.0f));
        cfg.volumeCount = rng() % 3u;
        for (uint i = 0u; i < cfg.volumeCount; ++i)
        {
            cfg.volumes[i] = RandomVolume(rng);
        }
        const float3 origin =
            float3(U(rng, -30.0f, 30.0f), U(rng, -5.0f, 20.0f),
                   U(rng, -30.0f, 30.0f));
        const float3 dir = USphere(rng);
        const float tMin = U(rng, 0.0f, 5.0f);
        const float tMax = tMin + U(rng, 5.0f, 120.0f);
        const float3 lightDir = USphere(rng);
        const float3 lightRadiance =
            float3(U(rng, 0.0f, 25.0f), U(rng, 0.0f, 25.0f),
                   U(rng, 0.0f, 25.0f));
        const float lightVis = (rng() % 2u == 0u) ? 0.0f : 1.0f;
        const float d0 = FogDensityQuery(origin, cfg);
        const float3 mid = origin + dir * ((tMin + tMax) * 0.5f);
        const float d1 = FogDensityQuery(mid, cfg);
        const FogMarchStructure m = FogMarchVolumes(
            origin, dir, tMin, tMax, lightDir, lightRadiance, lightVis, cfg);
        std::fprintf(f, "%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,"
                        "%.9g,%u,%.9g,%u",
                     cfg.globalDensity, cfg.globalHeightFalloff,
                     cfg.globalBaseHeight, cfg.globalAlbedo.x,
                     cfg.globalAlbedo.y, cfg.globalAlbedo.z, cfg.globalPhaseG,
                     cfg.ambientRadiance.x, cfg.ambientRadiance.y,
                     cfg.ambientRadiance.z, cfg.timeSeconds, cfg.marchSteps,
                     cfg.maxMarchDistance, cfg.volumeCount);
        WriteVolume(f, cfg.volumes[0]);
        WriteVolume(f, cfg.volumes[1]);
        std::fprintf(f, ",%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,"
                        "%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\n",
                     origin.x, origin.y, origin.z, dir.x, dir.y, dir.z, tMin,
                     tMax, lightDir.x, lightDir.y, lightDir.z, lightRadiance.x,
                     lightRadiance.y, lightRadiance.z, lightVis, d0, d1,
                     m.scatter.x, m.scatter.y, m.scatter.z, m.transmittance);
    }
    std::fclose(f);
    std::printf("wrote %s (%u rows)\n", a.out.c_str(), a.rows);
    return 0;
}
