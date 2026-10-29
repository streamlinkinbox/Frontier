/*==========================================================================================================================================
                                                      FOGSAMPLINGVALIDATION.H
==========================================================================================================================================*/
// 🧩 Compare cloud-shadow quadrature budgets against a finite 128-sample Vulkan reference.

//------------------------------------------------------------------------------------------------------------------------
//                                              SHADOW QUADRATURE COMPARISON
//------------------------------------------------------------------------------------------------------------------------

void ValidateFogSampling(
    LightComputeSubmission& Compute,
    LightComputeArchive     Scene,
    CloudParameters         Cloud,
    SpatialVector           Sun,
    uint32_t                CloudOffset,
    uint32_t                FogOffset,
    const std::string&      Output,
    bool                    Capture)
{
    using namespace TransportReference;
    bool Improved = true;
    for (uint32_t Wind : {0u, 120u})
    {
        Scene.Angles[CloudOffset + 6] = float(Wind);
        Cloud.WindTimeS = Wind;
        auto Evaluate = [&](bool Resolve)
        {
            return Compute.Execute(Scene, 0, 16, 71, 64, 0, true, true, Capture ? 4u : 0u, true, Capture,
                                   false, CloudOffset, FogOffset, Resolve);
        };
        auto Background = Evaluate(false);
        auto Receivers = Compute.PrimaryReceivers();
        std::array<std::vector<std::array<float, 4>>, 4> Quadratures;
        std::array<double, 4> NativeErrors{};
        uint32_t Probes = 0;
        constexpr uint32_t Budgets[] = {16, 32, 64, 128};
        for (uint32_t B = 0; B < 4; ++B)
        {
            Scene.Angles[FogOffset + 7] = float(Budgets[B]);
            Quadratures[B] = Evaluate(true);
            for (const auto &RGB : Quadratures[B])
                for (uint32_t C = 0; C < 3; ++C)
                    Require(std::isfinite(RGB[C]), "Non-finite fog quadrature readback");
            FogRayReference Reference(Scene, FogOffset, Cloud, Sun);
            uint32_t Count = 0;
            for (uint32_t Pixel = 0; Pixel < Receivers.size(); Pixel += Capture ? 137 : 31)
            {
                auto Origin = Vector(Scene.Camera[0]), Ray = FogCameraRay(Scene, Pixel);
                auto Delta = Sub(Vector(Receivers[Pixel].PositionHit), Origin);
                double Distance =
                    Receivers[Pixel].PositionHit[3] != 0
                        ? std::min(std::sqrt(Dot(Delta, Delta)), double(Scene.Angles[FogOffset + 5]))
                        : Scene.Angles[FogOffset + 5];
                auto Integral = Reference.Integrate(Origin, Ray, Distance);
                std::array<double, 3> RGB{Integral.Radiance.R, Integral.Radiance.G, Integral.Radiance.B};
                for (uint32_t C = 0; C < 3; ++C)
                {
                    double Expected = Background[Pixel][C] * Integral.Transmittance + RGB[C];
                    NativeErrors[B] =
                        std::max(NativeErrors[B], std::abs(Expected - Quadratures[B][Pixel][C]));
                }
                ++Count;
            }
            Probes = Count;
            Require(NativeErrors[B] < .003, "Native fog shadow-budget comparison disagrees");
            if (Capture)
                WriteImage(Output + "/FogSampling" + std::to_string(Wind) + "_" + std::to_string(Budgets[B]) +
                               ".ppm",
                           Quadratures[B], Scene.Width, Scene.Height);
            std::cout << "FOG_SAMPLE_CAPTURE wind=" << Wind << " shadow_steps=" << Budgets[B] << std::endl;
        }
        std::array<double, 4> Rmse{};
        for (uint32_t B = 0; B < 4; ++B)
        {
            double Squared = 0, Maximum = 0;
            for (uint32_t Pixel = 0; Pixel < Receivers.size(); ++Pixel)
                for (uint32_t C = 0; C < 3; ++C)
                {
                    double Difference = double(Quadratures[B][Pixel][C]) - Quadratures[3][Pixel][C];
                    Squared += Difference * Difference;
                    Maximum = std::max(Maximum, std::abs(Difference));
                }
            Rmse[B] = std::sqrt(Squared / (Receivers.size() * 3));
            std::cout << "FOG_SAMPLING wind=" << Wind << " shadow_steps=" << Budgets[B]
                      << " width=" << Scene.Width << " height=" << Scene.Height << " rmse_to128=" << Rmse[B]
                      << " max_to128=" << Maximum << " native_probes=" << Probes
                      << " native_rgb_error=" << NativeErrors[B]
                      << " density_per_m=" << Scene.Angles[FogOffset]
                      << " height_scale_m=" << Scene.Angles[FogOffset + 2] << std::endl;
        }
        Improved = Improved && Rmse[2] < Rmse[0];
        Require(Rmse[0] > 0, "Fog quadrature comparison has no measurable baseline error");
    }
    Require(Improved, "64 cloud-shadow samples did not improve both wind fixtures");
    std::cout << "FOG_SAMPLING_PRESET shadow_steps=64 both_winds_improved=1 reference_finite=1\n";
}
