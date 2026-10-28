/*==========================================================================================================================================
                                                        CLOUDCUBEVALIDATION.H
==========================================================================================================================================*/
// 🧩 Native cloud-ray and Beer-Lambert references for a Vulkan atmospheric radiance cube.

uint32_t AppendCloudParameters(LightComputeArchive& Scene, const CloudParameters& Medium, SpatialVector Sun,
                               SpectrumRgb Solar, uint32_t Steps)
{
    CloudRadianceSpecification Specification;
    Specification.Medium = Medium;
    Specification.SolarDirection = Sun;
    Specification.SolarRadiance = Solar;
    Specification.ObserverHeightM = Scene.Camera[0][2];
    Specification.ViewSamples = Steps;
    const auto Payload = CompileCloudRadiancePayload(Specification);
    const uint32_t Offset = uint32_t(Scene.Angles.size());
    Scene.Angles.insert(Scene.Angles.end(), Payload.begin(), Payload.end());
    return Offset;
}
void ValidateCloudCube(const char* Driver, const std::string& Shaders, const LightComputeArchive& Original,
                       const std::string& Output)
{
    using namespace TransportReference;
    constexpr uint32_t Size = 48;
    constexpr double π = 3.141592653589793;
    auto SkyCamera = [](LightComputeArchive& Scene)
    {
        auto Eye = Vector(Scene.Camera[0]), Forward = Unit(Sub(SpatialVector{0, 0, 3}, Eye));
        auto Right = Unit(SpatialVector{Forward.Y, -Forward.X, 0}), Up = Cross(Right, Forward);
        Scene.Camera[1] = {float(Forward.X), float(Forward.Y), float(Forward.Z), 4000};
        Scene.Camera[2] = {float(Right.X), float(Right.Y), float(Right.Z), float(std::tan(.9 * .5) * 1.6)};
        Scene.Camera[3] = {float(Up.X), float(Up.Y), float(Up.Z), float(std::tan(.9 * .5))};
        for (uint32_t I = 0; I < 4; ++I)
            Scene.Camera[4 + I] = Scene.Camera[I];
    };
    std::vector<std::array<float, 4>> PreviousWind;
    for (uint32_t Case = 0; Case < 5; ++Case)
    {
        auto Scene = Original;
        SkyCamera(Scene);
        CloudParameters P;
        P.Coverage = .55;
        P.WindTimeS = Case == 3 ? 120 : 0;
        if (Case == 0)
            P.DensityScale = 0;
        if (Case == 1)
            P.UniformDensityOverride = .125;
        double Elevation = Case == 4 ? 3 * π / 180 : 35 * π / 180;
        auto Sun =
            Unit(SpatialVector{std::cos(Elevation) * .6, std::cos(Elevation) * -.8, std::sin(Elevation)});
        SpectrumRgb TopSun{20, 20, 20};
        AtmosphereParameters Atmosphere;
        auto Ground = TransmittanceToTop(Atmosphere, Atmosphere.GroundRadiusKm + .004, Sun.Z);
        auto CloudAltitude = TransmittanceToTop(Atmosphere, Atmosphere.GroundRadiusKm + 1.3, Sun.Z);
        SpectrumRgb CloudSolar{TopSun.R * CloudAltitude.R, TopSun.G * CloudAltitude.G,
                               TopSun.B * CloudAltitude.B};
        for (auto& L : Scene.Lights)
            if (L.RadianceCategory[3] == 0)
            {
                L.PositionReach = {float(Sun.X), float(Sun.Y), float(Sun.Z), 0};
                L.RadianceCategory = {float(TopSun.R * Ground.R), float(TopSun.G * Ground.G),
                                      float(TopSun.B * Ground.B), 0};
            }
        Prepare(Scene, 16, Size, true, &TopSun);
        // 📝 Approximate the upper-hemisphere sky's isotropic incident component, integrated with solid-angle weights.
        double Weight = 0;
        SpectrumRgb Ambient;
        for (uint32_t Face = 0; Face < 6; ++Face)
            for (uint32_t Y = 0; Y < Size; ++Y)
                for (uint32_t X = 0; X < Size; ++X)
                {
                    double U = 2 * (X + .5) / Size - 1, V = 2 * (Y + .5) / Size - 1;
                    SpatialVector D = Face == 0   ? SpatialVector{1, U, V}
                                      : Face == 1 ? SpatialVector{-1, U, V}
                                      : Face == 2 ? SpatialVector{U, 1, V}
                                      : Face == 3 ? SpatialVector{U, -1, V}
                                      : Face == 4 ? SpatialVector{U, V, 1}
                                                  : SpatialVector{U, V, -1};
                    double W = 1 / std::pow(1 + U * U + V * V, 1.5);
                    Weight += W;
                    if (D.Z > 0)
                    {
                        uint32_t I = uint32_t(Scene.Camera[10][0]) + ((Face * Size + Y) * Size + X) * 3;
                        Ambient.R += Scene.Angles[I] * W;
                        Ambient.G += Scene.Angles[I + 1] * W;
                        Ambient.B += Scene.Angles[I + 2] * W;
                    }
                }
        P.AmbientRadiance = {Ambient.R / Weight, Ambient.G / Weight, Ambient.B / Weight};
        uint32_t Offset = AppendCloudParameters(Scene, P, Sun, CloudSolar, 64);
        EmissiveQuadrature::AppendDistribution(Scene);
        LightComputeSubmission Compute(Driver);
        Compute.Prepare(Scene, Shaders);
        auto Image = Compute.Execute(Scene, 0, 16, 71, 64, 0, true, true, 16, true, true, false, Offset);
        auto Cube = Compute.CloudCube(Size);
        if (Case == 0)
        {
            auto Clear = Compute.Execute(Scene, 0, 16, 71, 64, 0, true, true, 16, true, true, false);
            Require(Image == Clear, "Zero-density cloud path differs from clear atmosphere");
            std::cout << "CLOUD_CLEAR bitwise_equal=1\n";
        }
        double MaxT = 0, MaxRgb = 0, Mean = 0, Beer = 0;
        uint32_t Probes = 0, Opaque = 0;
        // Use the actual uploaded float parameters in the double-precision native oracle.
        const auto* F = Scene.Angles.data() + Offset;
        P.Coverage = F[2];
        P.DensityScale = F[3];
        P.ExtinctionPerM = F[4];
        P.PhaseG = F[5];
        P.WindTimeS = F[6];
        P.UniformDensityOverride = F[7];
        P.AmbientRadiance = {F[24], F[25], F[26]};
        Sun = {F[12], F[13], F[14]};
        CloudSolar = {F[16], F[17], F[18]};
        for (uint32_t I = 0; I < Cube.size(); I += 31)
        {
            uint32_t Face = I / (Size * Size), X = I % Size, Y = (I / Size) % Size;
            double U = 2 * (X + .5) / Size - 1, V = 2 * (Y + .5) / Size - 1;
            auto D = Unit(Face == 0   ? SpatialVector{1, U, V}
                          : Face == 1 ? SpatialVector{-1, U, V}
                          : Face == 2 ? SpatialVector{U, 1, V}
                          : Face == 3 ? SpatialVector{U, -1, V}
                          : Face == 4 ? SpatialVector{U, V, 1}
                                      : SpatialVector{U, V, -1});
            auto Native = MarchClouds(P, Vector(Scene.Camera[0]), D, Sun, CloudSolar, 40000, 64, 6);
            uint32_t Index = uint32_t(Scene.Camera[10][0]) + I * 3;
            std::array<double, 3> Expected{Native.Radiance.R + Native.Transmittance * Scene.Angles[Index],
                                           Native.Radiance.G + Native.Transmittance * Scene.Angles[Index + 1],
                                           Native.Radiance.B +
                                               Native.Transmittance * Scene.Angles[Index + 2]};
            MaxT = std::max(MaxT, std::abs(Native.Transmittance - Cube[I][3]));
            for (uint32_t C = 0; C < 3; ++C)
            {
                double Error = std::abs(Expected[C] - Cube[I][C]);
                MaxRgb = std::max(MaxRgb, Error);
                Mean += Error;
            }
            if (P.UniformDensityOverride >= 0 && D.Z > 0)
            {
                double Length = std::max(0., std::min(40000., (P.TopHeightM - Scene.Camera[0][2]) / D.Z) -
                                                 std::max(0., (P.BaseHeightM - Scene.Camera[0][2]) / D.Z));
                Beer = std::max(Beer,
                                std::abs(Cube[I][3] - std::exp(-P.ExtinctionPerM * P.UniformDensityOverride *
                                                               P.DensityScale * Length)));
            }
            Opaque += Cube[I][3] < .9;
            ++Probes;
        }
        if (Case == 1)
        {
            Require(P.AmbientRadiance.R > 0 && P.AmbientRadiance.G > 0 && P.AmbientRadiance.B > 0,
                    "Ambient input was not uploaded");
            CloudParameters Uniform = P;
            auto Ambient = MarchClouds(Uniform, Vector(Scene.Camera[0]), {0, 0, 1}, Sun, {}, 40000, 64, 0);
            double T = std::exp(-Uniform.ExtinctionPerM * Uniform.UniformDensityOverride *
                                Uniform.DensityScale * (Uniform.TopHeightM - Uniform.BaseHeightM));
            Require(std::abs(Ambient.Radiance.R - Uniform.AmbientRadiance.R * (1 - T)) < 1e-12,
                    "Native ambient term violates uniform-medium integral");
            std::cout << "CLOUD_AMBIENT uniform_closed_form=1\n";
        }
        auto SunNative = MarchClouds(P, Vector(Scene.Camera[0]), Sun, Sun, CloudSolar, 40000, 64, 6);
        double SunError = std::abs(Compute.CloudSun(Size) - SunNative.Transmittance);
        std::cout << "CLOUD_CUBE case=" << Case << " probes=" << Probes << " max_transmittance_error=" << MaxT
                  << " mean_rgb_error=" << Mean / (Probes * 3) << " max_rgb_error=" << MaxRgb
                  << " beer_error=" << Beer << " sun_error=" << SunError << " attenuated_probes=" << Opaque
                  << "\n";
        Require(MaxT < .001 && MaxRgb < .002 && Beer < .0001 && SunError < .001,
                "Native/Vulkan cloud integration disagreement");
        if (Case == 0)
            for (auto C : Cube)
                Require(C[3] == 1, "Clear cloud medium attenuated sky");
        else
            Require(Opaque > 0, "Cloud density did not attenuate any tested rays");
        if (Case == 2)
            PreviousWind = Cube;
        if (Case == 3)
        {
            Require(Cube != PreviousWind, "Wind did not change the cloud field");
            std::cout << "CLOUD_WIND changed=1\n";
        }
        if (Case == 2)
        {
            Scene.Save(Output + "/CloudScene.bin");
            auto Invalid = Scene;
            Invalid.Angles[Offset + 11] = 129;
            bool Rejected = false;
            try
            {
                (void)Compute.Execute(Invalid, 0, 16, 71, 64, 0, true, true, 16, true, true, false, Offset);
            }
            catch (const std::runtime_error&)
            {
                Rejected = true;
            }
            Require(Rejected, "Unbounded cloud march reached shader dispatch");
            std::cout << "CLOUD_BOUNDS invalid_steps_rejected=1\n";
        }
        WriteImage(Output + "/CloudVehicle" + std::to_string(Case) + ".ppm", Image, Scene.Width,
                   Scene.Height);
        // A separate sky-camera image exposes cloud shape beyond the narrow vehicle horizon.
        if (Case != 1)
        {
            auto Sky = Scene;
            Sky.Camera[10][2] = 0;
            Sky.Visibility.Enclosures.clear();
            Sky.Visibility.Corners.clear();
            Sky.Surfaces.clear();
            auto Direction = Unit(SpatialVector{.6, -.8, .45}),
                 Right = Unit(SpatialVector{Direction.Y, -Direction.X, 0}), Up = Cross(Right, Direction);
            Sky.Camera[1] = {float(Direction.X), float(Direction.Y), float(Direction.Z), 4000};
            Sky.Camera[2] = {float(Right.X), float(Right.Y), float(Right.Z), float(std::tan(.9 * .5) * 1.6)};
            Sky.Camera[3] = {float(Up.X), float(Up.Y), float(Up.Z), float(std::tan(.9 * .5))};
            LightComputeSubmission SkyCompute(Driver);
            SkyCompute.Prepare(Sky, Shaders);
            auto SkyImage =
                SkyCompute.Execute(Sky, 0, 16, 71, 64, 0, true, true, 0, true, false, false, Offset);
            double CameraError = 0;
            uint32_t CameraProbes = 0;
            for (uint32_t Pixel = 0; Pixel < SkyImage.size(); Pixel += 2600)
            {
                double X = 2 * (double(Pixel % Sky.Width) + .5) / Sky.Width - 1,
                       Y = 1 - 2 * (double(Pixel / Sky.Width) + .5) / Sky.Height;
                auto Ray =
                    Unit(Add(Vector(Sky.Camera[1]), Add(Mul(Vector(Sky.Camera[2]), X * Sky.Camera[2][3]),
                                                        Mul(Vector(Sky.Camera[3]), Y * Sky.Camera[3][3]))));
                auto Native = MarchClouds(P, Vector(Sky.Camera[0]), Ray, Sun, CloudSolar, 40000, 64, 6);
                auto Atmos = Environment(Sky, Ray);
                std::array<double, 3> Colour{Native.Radiance.R + Native.Transmittance * Atmos.R,
                                             Native.Radiance.G + Native.Transmittance * Atmos.G,
                                             Native.Radiance.B + Native.Transmittance * Atmos.B};
                for (uint32_t C = 0; C < 3; ++C)
                    CameraError = std::max(CameraError, std::abs(Colour[C] - SkyImage[Pixel][C]));
                ++CameraProbes;
            }
            std::cout << "CLOUD_CAMERA case=" << Case << " probes=" << CameraProbes
                      << " max_rgb_error=" << CameraError << "\n";
            Require(CameraError < .002, "Native/Vulkan full-resolution cloud camera mismatch");
            WriteImage(Output + "/CloudSky" + std::to_string(Case) + ".ppm", SkyImage, Sky.Width, Sky.Height);
        }
    }
}
