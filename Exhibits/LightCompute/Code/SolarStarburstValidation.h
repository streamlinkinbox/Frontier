/*==========================================================================================================================================
                                               SOLARSTARBURSTVALIDATION.H
==========================================================================================================================================*/
// 🧩 Isolate Vulkan aperture spikes, rotation, occlusion and simultaneous flare layers.
#pragma once

namespace
{
//------------------------------------------------------------------------------------------------------------------------
//                                                  POLAR READBACK MEASUREMENT
//------------------------------------------------------------------------------------------------------------------------

struct StarburstCircle
{
    std::array<double,720> Radiance{};
    uint32_t Spikes = 0;
    double Peak = 0, AlignmentError = 0;
};

StarburstCircle MeasureStarburstCircle(const std::vector<std::array<float,4>>& Image, uint32_t Width,
                                        uint32_t Height, double Radius, uint32_t Expected, double Rotation)
{
    constexpr double Pi = 3.14159265358979323846;
    constexpr uint32_t Samples = 720;
    std::array<double,Samples> Raw{};
    for (uint32_t I = 0; I < Samples; ++I)
    {
        const double Angle = 2 * Pi * I / Samples;
        const double X = .5 * Width + .5 * Height * Radius * std::cos(Angle) - .5;
        const double Y = .5 * Height - .5 * Height * Radius * std::sin(Angle) - .5;
        const int X0 = int(std::floor(X)), Y0 = int(std::floor(Y));
        Require(X0 >= 0 && Y0 >= 0 && X0 + 1 < int(Width) && Y0 + 1 < int(Height),
                "Starburst measurement circle is outside the image");
        const double Fx = X - X0, Fy = Y - Y0;
        auto Green = [&](int Px, int Py) { return double(Image[size_t(Py) * Width + size_t(Px)][1]); };
        Raw[I] = (1 - Fy) * ((1 - Fx) * Green(X0, Y0) + Fx * Green(X0 + 1, Y0)) +
                 Fy * ((1 - Fx) * Green(X0, Y0 + 1) + Fx * Green(X0 + 1, Y0 + 1));
    }
    StarburstCircle Result;
    for (uint32_t I = 0; I < Samples; ++I)
    {
        for (int J = -4; J <= 4; ++J) Result.Radiance[I] += Raw[(int(I) + J + int(Samples)) % int(Samples)] / 9;
        Result.Peak = std::max(Result.Peak, Result.Radiance[I]);
    }
    Require(Result.Peak > 0, "Starburst measurement circle is dark");
    const double Threshold = .25 * Result.Peak;
    for (uint32_t I = 0; I < Samples; ++I)
    {
        const uint32_t Previous = (I + Samples - 1) % Samples;
        if (Result.Radiance[I] >= Threshold && Result.Radiance[Previous] < Threshold) ++Result.Spikes;
    }
    for (uint32_t Spike = 0; Spike < Expected; ++Spike)
    {
        const double Direction = Rotation + 2 * Pi * Spike / Expected;
        double Best = -1, BestAngle = 0;
        for (uint32_t I = 0; I < Samples; ++I)
        {
            const double Angle = 2 * Pi * I / Samples;
            const double Separation = std::abs(std::remainder(Angle - Direction, 2 * Pi));
            if (Separation < Pi / Expected && Result.Radiance[I] > Best)
            {
                Best = Result.Radiance[I];
                BestAngle = Separation;
            }
        }
        Result.AlignmentError = std::max(Result.AlignmentError, BestAngle);
    }
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  VULKAN SPIKE AND LAYER TESTS
//------------------------------------------------------------------------------------------------------------------------

void ValidateSolarStarburst(const char* Driver, const std::string& Shaders, const std::string& Directory)
{
    constexpr uint32_t Width = 256, Height = 256;
    constexpr double Rotation = .137;
    auto Scene = SolarFixture(2);
    Scene.Width = Width;
    Scene.Height = Height;
    Scene.Receivers.resize(size_t(Width) * Height);
    LightComputeSubmission Compute(Driver);
    std::printf("STARBURST preparing Vulkan on %s (%ux%u)\n", Compute.Properties.deviceName, Width, Height);
    std::fflush(stdout);
    Compute.Prepare(Scene, Shaders, true);
    SolarOpticsParameters P;
    P.DiscEnabled = false;
    P.SunDirection = {0, 1, 0};
    P.DiscIrradiance = {80, 70, 60};
    P.Flare = QuietFlare();
    P.Flare.ApertureBlades = 6;
    P.Flare.ApertureRotationRad = Rotation;
    P.Flare.StarburstStrength = 1;
    const auto Six = ExecuteSolar(Compute, Scene, &P);
    const auto SixCircle = MeasureStarburstCircle(Six, Width, Height, .50, 6, Rotation);
    double CpuPeak = 0, CpuError = 0;
    for (uint32_t Y = 0; Y < Height; ++Y)
        for (uint32_t X = 0; X < Width; ++X)
        {
            const double ScreenX = (double(X) + .5 - Width * .5) / (Height * .5);
            const double ScreenY = (Height * .5 - double(Y) - .5) / (Height * .5);
            const auto Expected = EvaluateDiffractionStarburst(ScreenX, ScreenY, 0, 0, P.DiscIrradiance, 1, P.Flare);
            const std::array<double,3> Channels{Expected.R, Expected.G, Expected.B};
            const auto& Pixel = Six[size_t(Y) * Width + X];
            for (uint32_t C = 0; C < 3; ++C)
            {
                CpuPeak = std::max(CpuPeak, std::abs(Channels[C]));
                CpuError = std::max(CpuError, std::abs(double(Pixel[C]) - Channels[C]));
            }
        }
    Require(CpuPeak > 0 && CpuError / CpuPeak < .001 && SixCircle.Spikes == 6 && SixCircle.AlignmentError < .06,
            "Six-blade Vulkan starburst failed spike, rotation or CPU comparison");
    P.Flare.ApertureBlades = 5;
    const auto Five = ExecuteSolar(Compute, Scene, &P);
    const auto FiveCircle = MeasureStarburstCircle(Five, Width, Height, .50, 10, Rotation);
    Require(FiveCircle.Spikes == 10 && FiveCircle.AlignmentError < .06,
            "Five-blade Vulkan starburst did not produce ten oriented spikes");
    P.Flare.ApertureBlades = 6;
    P.Flare.ApertureRotationRad = Rotation + .2;
    const auto Rotated = ExecuteSolar(Compute, Scene, &P);
    const auto RotatedCircle = MeasureStarburstCircle(Rotated, Width, Height, .50, 6, Rotation + .2);
    Require(RotatedCircle.Spikes == 6 && RotatedCircle.AlignmentError < .06 && Rotated != Six,
            "Aperture rotation did not rotate the Vulkan starburst");
    std::printf("STARBURST spikes_6=%u spikes_5=%u rotated_spikes=%u alignment_rad=%.9g/%.9g/%.9g "
                "cpu_error_over_peak=%.9g\n", SixCircle.Spikes, FiveCircle.Spikes, RotatedCircle.Spikes,
                SixCircle.AlignmentError, FiveCircle.AlignmentError, RotatedCircle.AlignmentError, CpuError / CpuPeak);
    P.Flare.ApertureRotationRad = Rotation;
    P.Flare.StarburstStrength = 0;
    P.Flare.StreakStrength = 1;
    P.Flare.StreakAngleRad = .35;
    const auto Warm = ExecuteSolar(Compute, Scene, &P);
    P.Flare.StreakStrength = 0;
    P.Flare.AnamorphicStrength = 1;
    const auto Blue = ExecuteSolar(Compute, Scene, &P);
    P.Flare.StreakStrength = 1;
    P.Flare.StarburstStrength = 1;
    const auto Mixed = ExecuteSolar(Compute, Scene, &P);
    double Peak = 0, Additive = 0, StarSum = 0, WarmSum = 0, BlueSum = 0;
    for (size_t I = 0; I < Six.size(); ++I)
    {
        StarSum += Six[I][1];
        WarmSum += Warm[I][0];
        BlueSum += Blue[I][2];
        for (uint32_t C = 0; C < 3; ++C)
        {
            Peak = std::max(Peak, std::abs(double(Mixed[I][C])));
            Additive = std::max(Additive, std::abs(double(Mixed[I][C]) - Six[I][C] - Warm[I][C] - Blue[I][C]));
        }
    }
    Require(Peak > 0 && StarSum > 0 && WarmSum > 0 && BlueSum > 0 && Additive / Peak < 1e-5,
            "Starburst, warm and anamorphic flare layers failed simultaneous addition");
    auto BlockedScene = SolarFixture(-2);
    BlockedScene.Width = Width;
    BlockedScene.Height = Height;
    BlockedScene.Receivers.resize(size_t(Width) * Height);
    const auto Blocked = ExecuteSolar(Compute, BlockedScene, &P);
    double BlockedPeak = 0;
    for (const auto& Pixel : Blocked)
        for (uint32_t C = 0; C < 3; ++C) BlockedPeak = std::max(BlockedPeak, std::abs(double(Pixel[C])));
    Require(BlockedPeak == 0, "Opaque source occlusion did not remove all three flare layers");
    std::printf("STARBURST_MIX star_green_sum=%.9g warm_red_sum=%.9g blue_blue_sum=%.9g "
                "additive_error_over_peak=%.9g blocked_peak=%.9g\n", StarSum, WarmSum, BlueSum,
                Additive / Peak, BlockedPeak);
    std::filesystem::create_directories(Directory);
    WriteImage(Directory + "/StarburstSix.ppm", Six, Width, Height);
    WriteImage(Directory + "/StarburstFive.ppm", Five, Width, Height);
    WriteImage(Directory + "/StarburstMixed.ppm", Mixed, Width, Height);
    std::printf("PASS isolated Vulkan starburst, rotation, occlusion and three simultaneous flare layers\n");
}
} // namespace
