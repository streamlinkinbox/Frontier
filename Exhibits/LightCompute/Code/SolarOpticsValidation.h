/*==========================================================================================================================================
                                                  SOLAROPTICSVALIDATION.H
==========================================================================================================================================*/
// 🧩 Actual Vulkan readbacks against independent area integrals, the CPU solar model and invariants.
#pragma once
#include "Engine/Rendering/SolarDiscFilter.h"

namespace
{
LightComputeArchive SolarFixture(double Edge)
{
    LightComputeArchive Scene;
    Scene.Width = Scene.Height = 64;
    Scene.Receivers.resize(size_t(Scene.Width) * Scene.Height);
    Scene.Camera[0] = {0, 0, 0, .001f};
    Scene.Camera[1] = {0, 1, 0, 100};
    Scene.Camera[2] = {1, 0, 0, .032f};
    Scene.Camera[3] = {0, 0, 1, .032f};
    for (uint32_t I = 0; I < 4; ++I) Scene.Camera[4 + I] = Scene.Camera[I];
    Scene.Camera[10] = {0, 2, 0, 1};
    Scene.Camera[11] = {1, 0, 1, 0};
    for (uint32_t I = 0; I < 24; ++I) Scene.Angles.insert(Scene.Angles.end(), {.2f, .3f, .4f});
    LightComputeShape Directional;
    Directional.PositionReach = {0, -1, 0, 0};
    Directional.RadianceCategory = {1, .8f, .6f, 0};
    Scene.Lights.push_back(Directional);
    TriangleVisibilitySpace Space;
    const SpatialVector A{Edge, 10, -3}, B{Edge + 6, 10, -3}, C{Edge + 6, 10, 3}, D{Edge, 10, 3};
    Space.Build({{A, B, C}, {A, C, D}});
    Scene.Visibility = Space.PackFloat32();
    for (const auto& Corners : Scene.Visibility.Corners)
    {
        LightComputeFacet F;
        F.A = Corners.A; F.B = Corners.B; F.C = Corners.C;
        F.NormalA = F.NormalB = F.NormalC = {0, -1, 0, 0};
        F.AlbedoRoughness = {.6f, .3f, .15f, .6f};
        F.OpticalToken = {0, 1.5f, 1, 1};
        Scene.Surfaces.push_back(F);
    }
    return Scene;
}
FlareOpticsProfile QuietFlare()
{
    FlareOpticsProfile F;
    F.GhostStrength = F.StarburstStrength = F.AnamorphicStrength = F.StreakStrength = 0;
    F.GlowStrength = F.HaloStrength = F.VeilingStrength = 0;
    return F;
}
std::vector<std::array<float, 4>> ExecuteSolar(LightComputeSubmission& Compute, const LightComputeArchive& Scene,
                                               const SolarOpticsParameters* P, bool Environment = false)
{
    return Compute.Execute(Scene, 0, 16, 42, 64, 0, false, true, 0, Environment, false, false,
                           UINT32_MAX, UINT32_MAX, true, false, UINT32_MAX, P);
}
double SolarHalfPlaneReference(double Limit, const SunDiscProfile& P)
{
    constexpr int Grid = 700;
    double Sum = 0, Visible = 0;
    for (int Y = 0; Y < Grid; ++Y)
        for (int X = 0; X < Grid; ++X)
        {
            const double U = 2.0 * (X + .5) / Grid - 1, V = 2.0 * (Y + .5) / Grid - 1;
            const double R2 = U * U + V * V;
            if (R2 >= 1) continue;
            const double Q = 1 - std::sqrt(1 - R2);
            const double Weight = 1 - P.LimbLinear * Q - P.LimbQuadratic * Q * Q;
            Sum += Weight;
            if (U < Limit) Visible += Weight;
        }
    return Visible / Sum;
}
double SolarCartesianReference(double Radius, double Sigma, double Dx, double Dy, double Pitch,
                               const SunDiscProfile& P)
{
    constexpr int Grid = 500;
    const double RootTwo = std::sqrt(2.0);
    double Sum = 0, Weighted = 0;
    for (int Y = 0; Y < Grid; ++Y)
        for (int X = 0; X < Grid; ++X)
        {
            const double U = 2.0 * (X + .5) / Grid - 1, V = 2.0 * (Y + .5) / Grid - 1;
            const double R2 = U * U + V * V;
            if (R2 >= 1) continue;
            const double Q = 1 - std::sqrt(1 - R2);
            const double W = 1 - P.LimbLinear * Q - P.LimbQuadratic * Q * Q;
            const double X0 = (Dx - .5 * Pitch - U * Radius) / (RootTwo * Sigma);
            const double X1 = (Dx + .5 * Pitch - U * Radius) / (RootTwo * Sigma);
            const double Y0 = (Dy - .5 * Pitch - V * Radius) / (RootTwo * Sigma);
            const double Y1 = (Dy + .5 * Pitch - V * Radius) / (RootTwo * Sigma);
            Weighted += W * .25 * (std::erf(X1) - std::erf(X0)) * (std::erf(Y1) - std::erf(Y0));
            Sum += W;
        }
    return Weighted / (Sum * Pitch * Pitch);
}
void ValidateSolarOptics(const char* Driver, const std::string& Shaders)
{
    constexpr double Pitch = .001;
    auto Scene = SolarFixture(2);
    LightComputeSubmission Compute(Driver);
    std::printf("SOLAR preparing Vulkan pipelines on %s\n", Compute.Properties.deviceName);
    std::fflush(stdout);
    Compute.Prepare(Scene, Shaders, true);
    SolarOpticsParameters P;
    P.SunDirection = {0, 1, 0};
    P.DiscIrradiance = {1, 1, 1};
    P.Flare = QuietFlare();
    P.Disc.EdgeBlurRad = .0006;
    double WorstFlux = 0, WorstCpu = 0, WorstCartesian = 0;
    for (double Radius : {.4, 1.0, 2.8, 4.5, 8.0, 16.0})
    {
        P.Disc.AngularRadiusRad = Radius * Pitch;
        const auto Image = ExecuteSolar(Compute, Scene, &P);
        SolarDiscPixelFilter Filter(P.Disc);
        double Flux = 0, Error = 0, Peak = 0;
        for (uint32_t Y = 0; Y < Scene.Height; ++Y)
            for (uint32_t X = 0; X < Scene.Width; ++X)
            {
                const double Dx = (double(X) + .5 - 32) * Pitch, Dy = (32 - double(Y) - .5) * Pitch;
                const double Expected = Filter.Evaluate(Dx, Dy, Pitch, Pitch, {1, 1, 1}).R;
                const double Actual = Image[size_t(Y) * Scene.Width + X][0];
                Flux += Actual * Pitch * Pitch;
                Peak = std::max(Peak, std::abs(Expected));
                Error = std::max(Error, std::abs(Actual - Expected));
            }
        Require(Peak > 0, "GPU solar disc is dark");
        WorstFlux = std::max(WorstFlux, std::abs(Flux - 1));
        WorstCpu = std::max(WorstCpu, Error / Peak);
        if (Radius <= 2.8)
        {
            const uint32_t Points[][2] = {{31,31}, {32,31}, {33,31}, {35,32}};
            for (auto Point : Points)
            {
                const double Dx = (double(Point[0]) + .5 - 32) * Pitch;
                const double Dy = (32 - double(Point[1]) - .5) * Pitch;
                const double Expected = SolarCartesianReference(Radius * Pitch, P.Disc.EdgeBlurRad, Dx, Dy, Pitch, P.Disc);
                const double Actual = Image[size_t(Point[1]) * Scene.Width + Point[0]][0];
                WorstCartesian = std::max(WorstCartesian, std::abs(Actual - Expected) / Peak);
            }
        }
        std::printf("SOLAR radius_px=%.1f flux=%.9g cpu_error_over_peak=%.9g\n", Radius, Flux, Error / Peak);
    }
    std::printf("SOLAR worst_flux_error=%.9g cpu_max_over_peak=%.9g cartesian_max_over_peak=%.9g\n",
                WorstFlux, WorstCpu, WorstCartesian);
    Require(WorstFlux < .002 && WorstCpu < .001 && WorstCartesian < .01,
            "Finite solar disc failed flux or independent source-integration checks");

    P.Disc.AngularRadiusRad = .4 * Pitch;
    P.CloudTransmission = .3;
    P.PlanetHorizonZ = 0;
    const auto Horizon = ExecuteSolar(Compute, Scene, &P);
    const auto HorizonVisibility = Compute.SolarVisibility();
    double Flux = 0;
    for (auto Pixel : Horizon) Flux += Pixel[0] * Pitch * Pitch;
    std::printf("HORIZON gpu_visibility=%.9g cloud=%.3g disc_flux=%.9g\n", HorizonVisibility[0], P.CloudTransmission, Flux);
    Require(std::abs(HorizonVisibility[0] - .5) < 1e-5 && std::abs(Flux - .15) < .001,
            "Horizon and cloud transmission failed");
    P.CloudTransmission = 1;
    P.PlanetHorizonZ = -1;
    for (double Limit : {-2.0, 0.0, .5, 2.0})
    {
        const double Edge = Limit < -1 ? -2 : Limit > 1 ? 2 : 10 * P.Disc.AngularRadiusRad * Limit;
        const auto Image = ExecuteSolar(Compute, SolarFixture(Edge), &P);
        const auto Visibility = Compute.SolarVisibility();
        const double Expected = SolarHalfPlaneReference(Limit, P.Disc);
        double DiscFlux = 0;
        for (auto Pixel : Image) DiscFlux += Pixel[0] * Pitch * Pitch;
        std::printf("OCCLUSION limit=%.1f gpu=%.9g independent=%.9g disc_flux=%.9g\n",
                    Limit, Visibility[0], Expected, DiscFlux);
        Require(std::abs(Visibility[0] - Expected) < .012 && std::abs(DiscFlux - Visibility[0]) < .002,
                "Opaque partial solar occlusion differs from independent integral");
    }
    P.DiscEnabled = false;
    P.SunDirection = {.01, 1, .005};
    P.Flare = QuietFlare();
    const auto Dark = ExecuteSolar(Compute, Scene, &P);
    P.Flare.StreakStrength = 1;
    const auto Warm = ExecuteSolar(Compute, Scene, &P);
    P.Flare.StreakStrength = 0;
    P.Flare.AnamorphicStrength = 1;
    const auto Blue = ExecuteSolar(Compute, Scene, &P);
    P.Flare.StreakStrength = 1;
    const auto Mixed = ExecuteSolar(Compute, Scene, &P);
    double WarmSum = 0, BlueSum = 0, Peak = 0, Additive = 0, CpuError = 0;
    for (uint32_t Y = 0; Y < Scene.Height; ++Y)
        for (uint32_t X = 0; X < Scene.Width; ++X)
        {
            const size_t I = size_t(Y) * Scene.Width + X;
            const double ScreenX = (double(X) + .5 - 32) / 32, ScreenY = (32 - double(Y) - .5) / 32;
            const auto Expected = EvaluateLensFlareRadiance(ScreenX, ScreenY, .01 / .032, .005 / .032,
                                                             P.DiscIrradiance, 1, P.Flare);
            const double Channels[] = {Expected.R, Expected.G, Expected.B};
            WarmSum += Warm[I][0]; BlueSum += Blue[I][2];
            for (uint32_t C = 0; C < 3; ++C)
            {
                Peak = std::max(Peak, std::abs(double(Mixed[I][C])));
                Additive = std::max(Additive, std::abs(double(Mixed[I][C]) - Warm[I][C] - Blue[I][C] + Dark[I][C]));
                CpuError = std::max(CpuError, std::abs(double(Mixed[I][C]) - Channels[C]));
            }
        }
    std::printf("FLARES warm_sum=%.9g blue_sum=%.9g additive_error_over_peak=%.9g cpu_error_over_peak=%.9g\n",
                WarmSum, BlueSum, Additive / Peak, CpuError / Peak);
    Require(WarmSum > 0 && BlueSum > 0 && Peak > 0 && Additive / Peak < 1e-5 && CpuError / Peak < .001,
            "Concurrent warm and anamorphic flare mismatch");
    const auto Blocked = ExecuteSolar(Compute, SolarFixture(-2), &P);
    double BlockedPeak = 0;
    for (auto Pixel : Blocked) BlockedPeak = std::max(BlockedPeak, double(Pixel[0]));
    Require(BlockedPeak == 0 && ExecuteSolar(Compute, Scene, nullptr) == Dark,
            "Blocked flare or default-off composite failed");
    auto Lit = SolarFixture(.2);
    Lit.Lights[0].PeakActive[1] = 1;
    P.DiscEnabled = true; P.Flare = QuietFlare(); P.SunDirection = {0, 1, 0}; P.Disc.AngularRadiusRad = .0045;
    const auto Base = ExecuteSolar(Compute, Lit, nullptr, true);
    const auto Composite = ExecuteSolar(Compute, Lit, &P, true);
    const size_t GeometryPixel = 32 * Lit.Width + 60, SkyPixel = 32 * Lit.Width + 32;
    std::printf("COMPOSITE sky_red=%.9g surface_red=%.9g solar_added=%.9g geometry_unchanged=%u\n",
                Base[SkyPixel][0], Base[GeometryPixel][0], Composite[SkyPixel][0] - Base[SkyPixel][0],
                Base[GeometryPixel] == Composite[GeometryPixel] ? 1u : 0u);
    Require(Base[GeometryPixel] == Composite[GeometryPixel] && Composite[SkyPixel][0] > Base[SkyPixel][0],
            "Solar post-pass failed geometry preservation or sky addition");
    constexpr double Pi = 3.14159265358979323846;
    const double DiameterArcsec = 2 * SunDiscProfile{}.AngularRadiusRad * 180 * 3600 / Pi;
    std::printf("SUN_DIAMETER default_arcsec=%.9g NASA_mean_arcsec=1919 (1 AU)\n", DiameterArcsec);
    Require(std::abs(DiameterArcsec - 1919) < 1, "Default Sun diameter differs from NASA mean");
    std::printf("SENSOR synthetic_50mm_4um_2px_sigma=%.12g rad (not a lens measurement)\n",
                CalibrateSolarBlur({50, 4, 2}));
    std::printf("PASS Vulkan solar visibility, finite-pixel disc and simultaneous flare layers; device=%s\n",
                Compute.Properties.deviceName);
}
} // namespace
