//================================================================================
// CpuPortDiff — renders the vendored upstream C++ transcription (CpuPort/)
// with the panel's sky scope (sun + sky + analytic media + stars + post;
// moons/clouds/volumes/plane/flare off), for the parity gate vs SkyViewport.
// Usage: same flags as SkyViewport.
//   CpuPortDiff --sun 6.4 --yaw 35 --pitch 18 --width 480 --height 270
//       --out frame.ppm [--grain 0.1]
// The display coordinates passed to ResolveDisplay are the panel-uv scaled by
// (H/W, 1): through the port's own r = sqrt(u^2+v^2)*0.9 formula this
// reproduces the panel's aspect-corrected vignette to the last ulp. (The
// upstream stage passes raw +/-1 coords instead; that is the stage's framing
// choice, not the port's formula, and it is not what the panel computes.)
//================================================================================
#include "CelHost.h"
#include "CelestialIntegrator.h"

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
    float grain = 0.1f;
    std::string out = "frame.ppm";
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
        else if (k == "--height" && need(v))
        {
            a.height = uint32_t(std::stoul(v));
        }
        else if (k == "--grain" && need(v))
        {
            a.grain = float(std::stod(v));
        }
        else if (k == "--out" && need(v))
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

} // namespace

int main(int argc, char** argv)
{
    Args a;
    if (!Parse(argc, argv, a))
    {
        std::fprintf(stderr, "usage: CpuPortDiff --sun H --yaw Y --pitch P --width W "
                             "--height H --out F.ppm [--grain G]\n");
        return 2;
    }

    Frontier::ProjectZero::CelestialCriteria c;
    c.Sun.LocalHours = float(a.sun);
    c.Stars.Visible = true;
    c.Stars.Supersample = 1.0f; // load-time Standard tier (High would be 2)
    c.MoonCount = 0u;
    // NOTE: the vendored port's ApplyMedia has a confirmed transcription bug
    // (La is scaled by (1 - Ta.x) on ALL channels instead of per-channel
    // (1 - Ta); see CpuPort/PROVENANCE.md), so the port renders with AF off.
    // G2 verifies sky/glow/stars/disc/aureole/post; the media function itself
    // is verified independently by the numpy gate (G3 in RunCelestialParity).
    c.AtmosphericFog.Visible = false;
    c.LocalFog.Visible = false;
    c.LocalCloud.Visible = false;
    c.CloudLayer.Visible = false;
    c.VolumetricCloud.Visible = false;
    c.GroundPlane.Visible = false;
    c.Post.Grain = a.grain;

    Frontier::ProjectZero::CelestialIntegrator sky(c);
    sky.SolveFrame(0.5f); // fract * 100 = 50, the oracle's grain seed

    const SkyProjection cam = ProjectZero::MakePanelCamera(a.yaw, a.pitch, 72.0);
    Frontier::ProjectZero::ObserverFrame obs;
    obs.Position = Frontier::Vector3{ 0.0f, 2.0f, 0.0f };
    obs.Forward = Frontier::Vector3{ cam.fwd.x, cam.fwd.y, cam.fwd.z };
    obs.Right = Frontier::Vector3{ cam.right.x, cam.right.y, cam.right.z };
    obs.Upward = Frontier::Vector3{ cam.up.x, cam.up.y, cam.up.z };
    obs.TangentHalf = cam.tanHalf;
    obs.Height = 2.0f;
    const float pixelAngle = 2.0f * cam.tanHalf / float(a.height);

    std::printf("cpu-port sun %.2fh yaw %.1f pitch %.1f %ux%u grain %.2f\n",
                a.sun, a.yaw, a.pitch, a.width, a.height, a.grain);

    std::vector<float3> img(size_t(a.width) * a.height);
    for (uint32_t y = 0; y < a.height; ++y)
    {
        for (uint32_t x = 0; x < a.width; ++x)
        {
            const float3 dir = ProjectZero::PrimaryRay(cam, x, y, a.width, a.height);
            const Frontier::Vector3 d{ dir.x, dir.y, dir.z };
            const Frontier::Vector3 rad =
                sky.SampleSkyRadiance(d, obs, pixelAngle, x, y);
            const float2 uv = ViewportUVCompute(uint(x), uint(y), uint(a.width), uint(a.height));
            const float su = uv.x * (float(a.height) / float(a.width));
            const Frontier::Vector3 ldr =
                sky.ResolveDisplay(rad, su, uv.y, x, y);
            img[size_t(y) * a.width + x] = float3(ldr.x, ldr.y, ldr.z);
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
