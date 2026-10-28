/*==========================================================================================================================================
                                                       CLOUDSHADOWVALIDATION.H
==========================================================================================================================================*/
// 🧩 Native surface-Sun integration, secondary-hit checks and moving
// cloud-shadow readbacks.

void SetCloudCamera(LightComputeArchive &Scene, bool Overhead, uint32_t Width, uint32_t Height)
{
    using namespace TransportReference;
    Scene.Width = Width;
    Scene.Height = Height;
    Scene.Receivers.resize(size_t(Width) * Height);
    auto Eye = Overhead ? SpatialVector{0, -4, 700} : Vector(Scene.Camera[0]);
    auto Forward = Overhead ? SpatialVector{0, 0, -1} : Unit(Sub(SpatialVector{0, 0, 3}, Eye));
    auto Right = Overhead ? SpatialVector{1, 0, 0} : Unit(SpatialVector{Forward.Y, -Forward.X, 0});
    auto Up = Cross(Right, Forward);
    double Vertical = Overhead ? 1.75 : std::tan(.9 * .5);
    Scene.Camera[0] = {float(Eye.X), float(Eye.Y), float(Eye.Z), .01f};
    Scene.Camera[1] = {float(Forward.X), float(Forward.Y), float(Forward.Z), 6000};
    Scene.Camera[2] = {float(Right.X), float(Right.Y), float(Right.Z), float(Vertical * Width / Height)};
    Scene.Camera[3] = {float(Up.X), float(Up.Y), float(Up.Z), float(Vertical)};
    for (uint32_t I = 0; I < 4; ++I)
        Scene.Camera[4 + I] = Scene.Camera[I];
}
void ValidateCloudShadows(const char *Driver, const std::string &Shaders, const LightComputeArchive &Original,
                          const std::string &Output, bool Capture, uint32_t FirstCapture = 0)
{
    using namespace TransportReference;
    constexpr double π = 3.141592653589793;
    double Elevation = 35 * π / 180;
    auto Sun = Unit(SpatialVector{std::cos(Elevation) * .6, std::cos(Elevation) * -.8, std::sin(Elevation)});
    Sun = {float(Sun.X), float(Sun.Y), float(Sun.Z)};
    auto Numeric = Original;
    SetCloudCamera(Numeric, true, 160, 100);
    Numeric.Lights.clear();
    LightComputeShape Solar{};
    Solar.PositionReach = {float(Sun.X), float(Sun.Y), float(Sun.Z), 0};
    Solar.RadianceCategory = {20, 18, 15, 0};
    Solar.PeakActive[1] = 1;
    Numeric.Lights.push_back(Solar);
    for (auto &F : Numeric.Surfaces)
        F.Emissive = {};
    Prepare(Numeric, 16, 8, false);
    CloudParameters P;
    uint32_t Offset = AppendCloudParameters(Numeric, P, Sun, {}, 64);
    P.ExtinctionPerM = Numeric.Angles[Offset + 4];
    if (FirstCapture == 0)
    {
        LightComputeSubmission Compute(Driver);
        Compute.Prepare(Numeric, Shaders);
        std::vector<std::array<float, 4>> Earlier;
        for (uint32_t Case = 0; Case < 5; ++Case)
        {
            auto Scene = Numeric;
            P.DensityScale = Case == 0 ? 0 : 1;
            P.UniformDensityOverride = Case == 1 ? .125 : -1;
            P.WindTimeS = Case == 3 ? 120 : 0;
            if (Case == 4)
                Sun = Unit(SpatialVector{.6 * std::cos(3 * π / 180), -.8 * std::cos(3 * π / 180),
                                         std::sin(3 * π / 180)});
            Scene.Angles[Offset + 3] = float(P.DensityScale);
            Scene.Angles[Offset + 6] = float(P.WindTimeS);
            Scene.Angles[Offset + 7] = float(P.UniformDensityOverride);
            Scene.Lights[0].PositionReach = {float(Sun.X), float(Sun.Y), float(Sun.Z), 0};
            for (uint32_t C = 0; C < 3; ++C)
                Scene.Angles[Offset + 12 + C] = Scene.Lights[0].PositionReach[C];
            Sun = Vector(Scene.Lights[0].PositionReach);
            auto Pixels = Compute.Execute(Scene, 0, 16, 71, 64, 0, true, true, 0, true, false, false, Offset);
            auto Receivers = Compute.PrimaryReceivers();
            RaySpace Oracle(Scene);
            auto Light = Lights(Scene)[0];
            double Maximum = 0, MinimumT = 1, MaximumT = 0;
            uint32_t Probes = 0;
            for (uint32_t I = 0; I < Pixels.size(); I += 37)
            {
                const auto &R = Receivers[I];
                if (R.PositionHit[3] == 0)
                    continue;
                auto Position = Vector(R.PositionHit), Normal = Vector(R.NormalRoughness);
                double T = MarchClouds(P, Position, Sun, Sun, {}, 40000, 64, 0).Transmittance;
                MinimumT = std::min(MinimumT, T);
                MaximumT = std::max(MaximumT, T);
                SpatialVector Direction;
                double Distance;
                auto Incident = ReferenceLightArrival(Light, Position, Direction, Distance);
                auto Response = EvaluateReferenceBrdf(Material(R), Normal, Vector(R.ViewMetallic), Direction);
                double Scale = std::max(0., Dot(Normal, Direction)) * T;
                if (Oracle.Space.Occluded(Add(Position, Mul(Normal, .002)), Direction, 0, Distance - .004))
                    Scale = 0;
                std::array<double, 3> Expected{Response.R * Incident.R * Scale,
                                               Response.G * Incident.G * Scale,
                                               Response.B * Incident.B * Scale};
                for (uint32_t C = 0; C < 3; ++C)
                    Maximum = std::max(Maximum, std::abs(Expected[C] - Pixels[I][C]));
                ++Probes;
            }
            Require(Probes > 300 && Maximum < .0002, "Native spatial cloud shadow mismatch");
            std::cout << "CLOUD_SHADOW case=" << Case << " probes=" << Probes << " max_rgb_error=" << Maximum
                      << " min_transmittance=" << MinimumT << " max_transmittance=" << MaximumT << "\n";
            auto Clustered =
                Compute.Execute(Scene, 1, 1, 71, 64, 0, true, true, 0, true, false, false, Offset);
            Require(Pixels == Clustered, "Cloud shadow changed clustered/fallback equality");
            auto Ris = Compute.Execute(Scene, 3, 16, 71, 64, 3, true, true, 0, true, false, false, Offset);
            // 📝 A second unchanged-weather update must exercise temporal and spatial reuse at current
            // positions.
            auto Temporal =
                Compute.Execute(Scene, 3, 16, 72, 64, 3, false, true, 0, true, false, false, Offset);
            auto ReuseCounts = Compute.HistoryStatistics();
            Require(ReuseCounts[4] > 0 && ReuseCounts[8] > 0,
                    "Cloud fixture did not exercise reservoir reuse");
            double RisError = 0;
            for (uint32_t I = 0; I < Pixels.size(); ++I)
                for (uint32_t C = 0; C < 3; ++C)
                {
                    RisError = std::max(RisError, std::abs(double(Ris[I][C]) - Pixels[I][C]));
                    RisError = std::max(RisError, std::abs(double(Temporal[I][C]) - Pixels[I][C]));
                }
            Require(RisError < .00002, "Single-Sun reservoir reuse changed spatial attenuation");
            if (Case == 0)
            {
                auto Clear = Compute.Execute(Scene, 0, 16, 71, 64, 0, true, true, 0, true);
                Require(Pixels == Clear, "Zero density changed clear spatial shadow output");
            }
            if (Case == 2)
            {
                Require(MaximumT - MinimumT > .25, "Shadow fixture did not expose spatial variation");
                Earlier = Pixels;
            }
            if (Case == 3)
                Require(Earlier != Pixels, "Wind did not move surface cloud shadows");
            std::cout << "CLOUD_SHADOW_REUSE case=" << Case << " max_rgb_error=" << RisError
                      << " cluster_equal=1 temporal_accepted=" << ReuseCounts[4]
                      << " spatial_accepted=" << ReuseCounts[8] << "\n";
        }
        // 📝 A different directional source and point/IES/spot lights must not
        // inherit Sun-only extinction.
        {
            auto NonSolar = Numeric;
            NonSolar.Lights = Original.Lights;
            for (auto &L : NonSolar.Lights)
                if (L.RadianceCategory[3] == 0)
                    L.PositionReach = {0, 0, 1, 0};
            LightComputeSubmission SourceCompute(Driver);
            SourceCompute.Prepare(NonSolar, Shaders);
            auto NoCloud = SourceCompute.Execute(NonSolar, 0, 16, 71, 64, 0, true, true, 0, true);
            auto WithCloud =
                SourceCompute.Execute(NonSolar, 0, 16, 71, 64, 0, true, true, 0, true, false, false, Offset);
            Require(NoCloud == WithCloud, "Cloud Sun attenuation contaminated non-Solar lights");
            std::cout << "CLOUD_SHADOW_SOURCES non_solar_bitwise_equal=1\n";
        }

        // 📝 Isolate secondary analytic arrivals: black sky, no material emission,
        // regular hemisphere directions.
        auto Secondary = Numeric;
        Secondary.Camera[0] = Original.Camera[0];
        SetCloudCamera(Secondary, false, 160, 100);
        Sun = Vector(Secondary.Lights[0].PositionReach);
        auto Direct = Compute.Execute(Secondary, 0, 16, 71, 64, 0, true, true, 0, true, false, false, Offset);
        auto Receivers = Compute.PrimaryReceivers();
        auto Transport =
            Compute.Execute(Secondary, 0, 16, 71, 64, 0, true, true, 16, true, false, false, Offset);
        P.DensityScale = 1;
        P.UniformDensityOverride = -1;
        P.WindTimeS = 0;
        RaySpace Oracle(Secondary, &P, Sun);
        double BounceError = 0, Energy = 0;
        uint32_t BounceProbes = 0;
        for (uint32_t I = 0; I < Receivers.size(); I += 79)
        {
            if (Receivers[I].PositionHit[3] == 0)
                continue;
            auto Expected = Oracle.Integrate(Receivers[I], 16);
            std::array<double, 3> RGB{Expected.R, Expected.G, Expected.B};
            for (uint32_t C = 0; C < 3; ++C)
            {
                BounceError = std::max(BounceError, std::abs(RGB[C] - (Transport[I][C] - Direct[I][C])));
                Energy += RGB[C];
            }
            ++BounceProbes;
        }
        Require(BounceProbes > 100 && Energy > 0 && BounceError < .002,
                "Native secondary-hit cloud attenuation mismatch");
        std::cout << "CLOUD_SHADOW_BOUNCE probes=" << BounceProbes << " max_rgb_error=" << BounceError
                  << " native_rgb_sum=" << Energy << "\n";

        // 📝 Weather is inside the exact environment revision prefix: changed wind
        // must equal a cold restart.
        auto Weather = Numeric;
        Compute.Execute(Weather, 3, 16, 71, 64, 3, true, true, 16, true, false, true, Offset);
        Weather.Angles[Offset + 6] = 120;
        auto Changed = Compute.Execute(Weather, 3, 16, 71, 64, 3, false, true, 16, true, false, true, Offset);
        LightComputeSubmission Cold(Driver);
        Cold.Prepare(Weather, Shaders);
        auto Restart = Cold.Execute(Weather, 3, 16, 71, 64, 3, true, true, 16, true, false, true, Offset);
        Require(Changed == Restart, "Weather revision retained stale direct/indirect illumination");
        std::cout << "CLOUD_SHADOW_RESET wind_cold_equal=1\n";
        auto Disabled = Compute.Execute(Weather, 3, 16, 71, 64, 3, false, true, 16, true, false, true);
        auto DisabledCold = Cold.Execute(Weather, 3, 16, 71, 64, 3, true, true, 16, true, false, true);
        Require(Disabled == DisabledCold, "Disabling clouds retained stale illumination");
        auto Enabled = Compute.Execute(Weather, 3, 16, 71, 64, 3, false, true, 16, true, false, true, Offset);
        auto EnabledCold = Cold.Execute(Weather, 3, 16, 71, 64, 3, true, true, 16, true, false, true, Offset);
        Require(Enabled == EnabledCold, "Enabling clouds retained stale illumination");
        std::cout << "CLOUD_SHADOW_TOGGLE disabled_cold_equal=1 enabled_cold_equal=1\n";
    }
    if (!Capture)
        return;
    for (uint32_t Pose = FirstCapture; Pose < 4; ++Pose)
    {
        auto Scene = Original;
        Sun = Vector(Numeric.Lights[0].PositionReach);
        SpectrumRgb TopSun{20, 20, 20};
        AtmosphereParameters Atmosphere;
        auto Ground = TransmittanceToTop(Atmosphere, Atmosphere.GroundRadiusKm + .004, Sun.Z);
        auto Altitude = TransmittanceToTop(Atmosphere, Atmosphere.GroundRadiusKm + 1.3, Sun.Z);
        for (auto &L : Scene.Lights)
            if (L.RadianceCategory[3] == 0)
            {
                L.PositionReach = Numeric.Lights[0].PositionReach;
                L.RadianceCategory = {float(20 * Ground.R), float(20 * Ground.G), float(20 * Ground.B), 0};
            }
        Prepare(Scene, 16, 48, true, &TopSun);
        P.WindTimeS = Pose * 60;
        P.AmbientRadiance = {.1, .12, .15};
        uint32_t CloudOffset =
            AppendCloudParameters(Scene, P, Sun, {20 * Altitude.R, 20 * Altitude.G, 20 * Altitude.B}, 64);
        EmissiveQuadrature::AppendDistribution(Scene);
        for (bool Overhead : {false, true})
        {
            auto View = Scene;
            SetCloudCamera(View, Overhead, 640, 400);
            LightComputeSubmission Render(Driver);
            Render.Prepare(View, Shaders);
            auto Pixels = Render.Execute(View, 0, 16, 71, 64, 0, true, true, Overhead ? 0 : 16, true,
                                         !Overhead, false, CloudOffset);
            WriteImage(Output + (Overhead ? "/CloudShadowGround" : "/CloudShadowVehicle") +
                           std::to_string(Pose) + ".ppm",
                       Pixels, View.Width, View.Height);
        }
        std::cout << "CLOUD_SHADOW_CAPTURE pose=" << Pose << " wind_seconds=" << Pose * 60 << "\n";
    }
}
