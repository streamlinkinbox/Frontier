//================================================================================
// SkyViewport — renders the shipped SkySpecification.slang sky on the CPU.
//
// Mirrors the `skyViewport` entry point 1:1 (same ray convention, same post).
// Usage:
//   SkyViewport --sun 6.4 --yaw 35 --pitch 18 --width 480 --height 270
//       --out frame.ppm [--grain 0.1] [--linear stats.txt]
// With --linear, also writes per-pixel linear-HDR stats for the parity gate.
//================================================================================
#include "CelHost.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

struct Args
{
    double sun = 6.4;
    double yaw = 35.0;
    double pitch = 18.0;
    uint32_t width = 480;
    uint32_t height = 270;
    double fov = 72.0;
    float grain = 0.1f;
    int media = 1;
    int moon = 0;
    int cloud = 0;
    double stars = 1.0;
    double coverage = 0.45;
    double moonbright = 2.0;
    std::string out = "frame.ppm";
    std::string linear;
};

bool Parse(int argc, char** argv, Args& a) noexcept
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string k = argv[i];
        auto need = [&](std::string& v) -> bool
        {
            if (++i >= argc)
            {
                return false;
            }
            v = argv[i];
            return true;
        };
        std::string v;
        if (k == "--sun" && need(v))
        {
            a.sun = std::stod(v);
        }
        else if (k == "--yaw" && need(v))
        {
            a.yaw = std::stod(v);
        }
        else if (k == "--pitch" && need(v))
        {
            a.pitch = std::stod(v);
        }
        else if (k == "--width" && need(v))
        {
            a.width = uint32_t(std::stoul(v));
        }
        else if (k == "--fov" && need(v))
        {
            a.fov = std::stod(v);
        }
        else if (k == "--height" && need(v))
        {
            a.height = uint32_t(std::stoul(v));
        }
        else if (k == "--grain" && need(v))
        {
            a.grain = float(std::stod(v));
        }
        else if (k == "--media" && need(v))
        {
            a.media = std::stoi(v);
        }
        else if (k == "--moon" && need(v))
        {
            a.moon = std::stoi(v);
        }
        else if (k == "--cloud" && need(v))
        {
            a.cloud = std::stoi(v);
        }
        else if (k == "--stars" && need(v))
        {
            a.stars = std::stod(v);
        }
        else if (k == "--coverage" && need(v))
        {
            a.coverage = std::stod(v);
        }
        else if (k == "--moonbright" && need(v))
        {
            a.moonbright = std::stod(v);
        }
        else if (k == "--out" && need(v))
        {
            a.out = v;
        }
        else if (k == "--linear" && need(v))
        {
            a.linear = v;
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
        std::fprintf(stderr, "usage: SkyViewport --sun H --yaw Y --pitch P --width W "
                             "--height H --out F.ppm [--grain G] [--media 0/1] [--linear S.txt]\n");
        return 2;
    }

    const ProjectZero::SunState sun =
        ProjectZero::SolveSun(a.sun, -26.0, 0.0, 5800.0);
    SkyConfiguration p = ProjectZero::MakePanelParams(sun);
    p.grain = a.grain;
    p.moonOn = a.moon ? 1u : 0u;
    p.cloudOn = a.cloud ? 1u : 0u;
    p.starBright = float(a.stars);
    p.starCeil = 8.0f * 2.6e-3f * (p.starBright != 0.0f ? p.starBright : 1.0f)
               * 3.2f * 1.6f;
    p.cloudCoverage = float(a.coverage);
    p.moonBright = float(a.moonbright);
    if (!a.media)
    {
        p.fogOn = 0u;
        p.afOn = 0u;
    }
    const SkyProjection cam = ProjectZero::MakePanelCamera(a.yaw, a.pitch, a.fov);
    p.pixAngle = 2.0f * cam.tanHalf / float(a.height); // panel line 1219

    std::printf("sun %.2fh elev %.3f deg yaw %.1f pitch %.1f %ux%u grain %.2f\n",
                a.sun, sun.elevationDeg, a.yaw, a.pitch, a.width, a.height, a.grain);

    std::vector<float3> img(size_t(a.width) * a.height);
    double lumSum = 0.0;
    float lumMax = 0.0f;
    std::FILE* lin = a.linear.empty() ? nullptr : std::fopen(a.linear.c_str(), "w");
    if (lin)
    {
        std::fprintf(lin, "# x y dirX dirY dirZ linR linG linB\n");
    }
    for (uint32_t y = 0; y < a.height; ++y)
    {
        for (uint32_t x = 0; x < a.width; ++x)
        {
            const float3 dir = ProjectZero::PrimaryRay(cam, x, y, a.width, a.height);
            const float3 hdr = SkyPixelCompute(dir, cam, p);
            const float2 uv = ViewportUVCompute(uint(x), uint(y), uint(a.width), uint(a.height));
            img[size_t(y) * a.width + x] =
                SkyPostApply(hdr, uv.x, uv.y, float(a.width), float(a.height), uint(x), uint(y), p);
            const float lum = LuminanceCompute(hdr);
            lumSum += lum;
            lumMax = lum > lumMax ? lum : lumMax;
            if (lin)
            {
                std::fprintf(lin, "%u %u %.7f %.7f %.7f %.8e %.8e %.8e\n",
                             x, y, dir.x, dir.y, dir.z, hdr.x, hdr.y, hdr.z);
            }
        }
    }
    if (lin)
    {
        std::fclose(lin);
    }
    std::printf("linear-HDR mean lum %.6f max lum %.6f\n",
                lumSum / double(a.width * a.height), lumMax);

    if (!ProjectZero::WritePpm(a.out, a.width, a.height, img))
    {
        std::fprintf(stderr, "cannot write %s\n", a.out.c_str());
        return 1;
    }
    std::printf("wrote %s\n", a.out.c_str());
    return 0;
}
