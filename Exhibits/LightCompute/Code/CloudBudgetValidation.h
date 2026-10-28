/*==========================================================================================================================================
                                                     CLOUDBUDGETVALIDATION.H
==========================================================================================================================================*/
// 🧩 Compare practical cloud/fog shadow budgets with a finite higher-sample vehicle render.

//------------------------------------------------------------------------------------------------------------------------
//                                              WORKING AND REFERENCE BUDGETS
//------------------------------------------------------------------------------------------------------------------------

void ValidateCloudBudget(
    LightComputeSubmission& Compute,
    LightComputeArchive     Scene,
    CloudParameters         Cloud,
    SpatialVector           Sun,
    uint32_t                CloudOffset,
    uint32_t                FogOffset,
    const std::string&      Output,
    bool                    Compare = true)
{
    using namespace TransportReference;
    for (uint32_t Wind : {0u, 120u})
    {
        Cloud.WindTimeS = Wind;
        Scene.Angles[CloudOffset + 6] = float(Wind);
        std::array<std::vector<std::array<float, 4>>, 2> Images;
        for (uint32_t B = 0; B < (Compare ? 2u : 1u); ++B)
        {
            uint32_t Steps = B == 0 ? 32 : 64;
            Scene.Angles[CloudOffset + 11] = float(Steps);
            Scene.Angles[FogOffset + 7] = float(Steps);
            auto Execute = [&](bool Resolve)
            {
                return Compute.Execute(Scene, 0, 16, 71, 64, 0, true, true, 4, true, true, false,
                                       CloudOffset, FogOffset, Resolve);
            };
            auto Background = Execute(false);
            auto Receivers = Compute.PrimaryReceivers();
            Images[B] = Execute(true);
            FogRayReference Reference(Scene, FogOffset, Cloud, Sun);
            double Error = 0, SkyError = 0;
            uint32_t Probes = 0, SkyProbes = 0;
            for (uint32_t Pixel = 0; Pixel < Receivers.size(); Pixel += 137)
            {
                auto Origin = Vector(Scene.Camera[0]), Ray = FogCameraRay(Scene, Pixel);
                auto Delta = Sub(Vector(Receivers[Pixel].PositionHit), Origin);
                double Distance = Receivers[Pixel].PositionHit[3] != 0
                    ? std::min(std::sqrt(Dot(Delta, Delta)), double(Scene.Angles[FogOffset + 5]))
                    : Scene.Angles[FogOffset + 5];
                auto Integral = Reference.Integrate(Origin, Ray, Distance);
                std::array<double, 3> RGB{Integral.Radiance.R, Integral.Radiance.G, Integral.Radiance.B};
                for (uint32_t C = 0; C < 3; ++C)
                    Error = std::max(Error, std::abs(Background[Pixel][C] * Integral.Transmittance + RGB[C] -
                                                   Images[B][Pixel][C]));
                if (Receivers[Pixel].PositionHit[3] == 0)
                {
                    SpectrumRgb Solar{Scene.Angles[CloudOffset + 16], Scene.Angles[CloudOffset + 17],
                                      Scene.Angles[CloudOffset + 18]};
                    auto Native = MarchClouds(Cloud, Origin, Ray, Sun, Solar, 40000, Steps, 6);
                    auto Sky = Environment(Scene, Ray);
                    std::array<double, 3> Expected{Native.Radiance.R + Native.Transmittance * Sky.R,
                                                 Native.Radiance.G + Native.Transmittance * Sky.G,
                                                 Native.Radiance.B + Native.Transmittance * Sky.B};
                    for (uint32_t C = 0; C < 3; ++C)
                        SkyError = std::max(SkyError, std::abs(Expected[C] - Background[Pixel][C]));
                    ++SkyProbes;
                }
                ++Probes;
            }
            Require(Error < .003 && SkyError < .003 && SkyProbes > 0, "Low-budget native cloud/fog mismatch");
            std::string Name = "CloudBudget" + std::to_string(Wind) + "_" + std::to_string(Steps);
            WriteImage(Output + "/" + Name + ".ppm", Images[B], Scene.Width, Scene.Height);
            std::cout << "CLOUD_BUDGET wind=" << Wind << " steps=" << Steps << " probes=" << Probes
                      << " max_rgb_error=" << Error << " sky_probes=" << SkyProbes
                      << " sky_max_rgb_error=" << SkyError << std::endl;
        }
        if (!Compare)
            continue;
        double Squared = 0, Maximum = 0;
        for (uint32_t Pixel = 0; Pixel < Images[0].size(); ++Pixel)
            for (uint32_t C = 0; C < 3; ++C)
            {
                double Difference = double(Images[0][Pixel][C]) - Images[1][Pixel][C];
                Require(std::isfinite(Difference), "Nonfinite budget comparison");
                Squared += Difference * Difference;
                Maximum = std::max(Maximum, std::abs(Difference));
            }
        std::cout << "CLOUD_BUDGET_ERROR wind=" << Wind
                  << " rmse_to64=" << std::sqrt(Squared / (Images[0].size() * 3))
                  << " max_to64=" << Maximum << std::endl;
    }
}
