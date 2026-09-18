//================================================================================
// MediaProbe — samples the shipped core's SkyMediaApply over seeded random
// inputs (plus the panel's dS regimes) and dumps a CSV. The parity gate's G3
// recomputes every row with an independent numpy transcription of the panel's
// applyMedia (lines 947-962) and bounds the difference.
// Usage: MediaProbe --out rows.csv [--samples N]
// Columns: elev,fogOn,afOn,dir(3),d,col(3),hz(3),trans(3),out(3)
//================================================================================
#include "CelHost.h"

#include <cstdio>
#include <cstring>
#include <random>
#include <string>

namespace {

struct Args
{
    uint32_t samples = 3000;
    std::string out = "media.csv";
};

bool Parse(int argc, char** argv, Args& a) noexcept
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string k = argv[i];
        if (k == "--out" && ++i < argc)
        {
            a.out = argv[i];
        }
        else if (k == "--samples" && ++i < argc)
        {
            a.samples = uint32_t(std::stoul(argv[i]));
        }
        else
        {
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    Args a;
    if (!Parse(argc, argv, a))
    {
        std::fprintf(stderr, "usage: MediaProbe --out F.csv [--samples N]\n");
        return 2;
    }

    std::FILE* f = std::fopen(a.out.c_str(), "w");
    if (!f)
    {
        std::fprintf(stderr, "cannot write %s\n", a.out.c_str());
        return 1;
    }
    std::fprintf(f, "# elev,fogOn,afOn,dirX,dirY,dirZ,d,colR,colG,colB,"
                    "hzR,hzG,hzB,trR,trG,trB,sunX,sunY,sunZ,"
                    "scR,scG,scB,ambR,ambG,ambB,outR,outG,outB\n");

    std::mt19937 rng(1234u);
    std::uniform_real_distribution<float> uni(0.0f, 1.0f);
    const double suns[3] = { 7.6, 5.889, 0.0 }; // day, twilight, night
    uint32_t rows = 0;
    for (int s = 0; s < 3; ++s)
    {
        const ProjectZero::SunState sun =
            ProjectZero::SolveSun(suns[s], -26.0, 0.0, 5800.0);
        for (int combo = 0; combo < 4; ++combo)
        {
            SkyConfiguration p = ProjectZero::MakePanelParams(sun);
            const int fogOn = (combo & 1) != 0 ? 1 : 0;
            const int afOn = (combo & 2) != 0 ? 1 : 0;
            p.fogOn = uint(fogOn);
            p.afOn = uint(afOn);
            for (uint32_t i = 0; i < a.samples; ++i)
            {
                // Uniform random direction (sphere pick).
                const float u1 = uni(rng);
                const float u2 = uni(rng);
                const float z = 1.0f - 2.0f * u1;
                const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
                const float ph = 6.2831853f * u2;
                const float3 dir = float3(r * std::cos(ph), z, r * std::sin(ph));
                // Log-uniform distance across the panel's dS regimes.
                const float d = std::exp(uni(rng)
                                         * (std::log(60000.0f) - std::log(100.0f))
                                         + std::log(100.0f));
                float3 col, hz, tr;
                col.x = std::exp(uni(rng) * (std::log(100.0f) - std::log(1e-6f))
                                 + std::log(1e-6f));
                col.y = std::exp(uni(rng) * (std::log(100.0f) - std::log(1e-6f))
                                 + std::log(1e-6f));
                col.z = std::exp(uni(rng) * (std::log(100.0f) - std::log(1e-6f))
                                 + std::log(1e-6f));
                hz.x = std::exp(uni(rng) * (std::log(10.0f) - std::log(1e-6f))
                                + std::log(1e-6f));
                hz.y = std::exp(uni(rng) * (std::log(10.0f) - std::log(1e-6f))
                                + std::log(1e-6f));
                hz.z = std::exp(uni(rng) * (std::log(10.0f) - std::log(1e-6f))
                                + std::log(1e-6f));
                tr.x = uni(rng);
                tr.y = uni(rng);
                tr.z = uni(rng);
                const float3 o = SkyMediaApply(col, dir, d, hz, tr, p);
                std::fprintf(f, "%.6g,%d,%d,%.9g,%.9g,%.9g,%.9g,"
                               "%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,"
                               "%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,"
                               "%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,"
                               "%.9g,%.9g,%.9g\n",
                             sun.elevationDeg, fogOn, afOn, dir.x, dir.y, dir.z,
                             d, col.x, col.y, col.z, hz.x, hz.y, hz.z,
                             tr.x, tr.y, tr.z, p.sunDir.x, p.sunDir.y,
                             p.sunDir.z, p.sunColor.x, p.sunColor.y,
                             p.sunColor.z, p.skyAmb.x, p.skyAmb.y, p.skyAmb.z,
                             o.x, o.y, o.z);
                ++rows;
            }
        }
    }
    std::fclose(f);
    std::printf("wrote %s (%u rows)\n", a.out.c_str(), rows);
    return 0;
}
