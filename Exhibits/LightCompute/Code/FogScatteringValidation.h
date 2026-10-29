/*==========================================================================================================================================
                                                     FOGSCATTERINGVALIDATION.H
==========================================================================================================================================*/
// 🧩 Independent fog-column and shadowed single-scattering checks with actual Vulkan vehicle captures.

uint32_t AppendFogParameters(LightComputeArchive &Scene)
{
    FogRadianceSpecification Specification;
    Specification.Medium.DensityAtBase = .0015;
    Specification.Medium.BaseHeightM = 0;
    Specification.Medium.FalloffHeightM = 450;
    const auto Payload = CompileFogRadiancePayload(Specification);
    const uint32_t Offset = uint32_t(Scene.Angles.size());
    Scene.Angles.insert(Scene.Angles.end(), Payload.begin(), Payload.end());
    return Offset;
}
SpatialVector FogCameraRay(const LightComputeArchive &Scene, uint32_t Pixel)
{
    using namespace TransportReference;
    double X = 2 * (double(Pixel % Scene.Width) + .5) / Scene.Width - 1;
    double Y = 1 - 2 * (double(Pixel / Scene.Width) + .5) / Scene.Height;
    return Unit(Add(Vector(Scene.Camera[1]), Add(Mul(Vector(Scene.Camera[2]), X * Scene.Camera[2][3]),
                                                 Mul(Vector(Scene.Camera[3]), Y * Scene.Camera[3][3]))));
}
void SetFogCamera(LightComputeArchive &Scene, uint32_t Width, uint32_t Height)
{
    using namespace TransportReference;
    SetCloudCamera(Scene, false, Width, Height);
    auto Forward = Unit(Sub(SpatialVector{0, 0, 4}, Vector(Scene.Camera[0])));
    auto Right = Unit(SpatialVector{Forward.Y, -Forward.X, 0}), Up = Cross(Right, Forward);
    Scene.Camera[1] = {float(Forward.X), float(Forward.Y), float(Forward.Z), 6000};
    Scene.Camera[2] = {float(Right.X), float(Right.Y), float(Right.Z), float(std::tan(.5) * Width / Height)};
    Scene.Camera[3] = {float(Up.X), float(Up.Y), float(Up.Z), float(std::tan(.5))};
    for (uint32_t I = 0; I < 4; ++I)
        Scene.Camera[4 + I] = Scene.Camera[I];
}
#include "FogFacetScattering.h"

struct FogRayReference
{
    TransportReference::RaySpace Geometry;
    const LightComputeArchive &Scene;
    uint32_t Offset;
    CloudParameters Cloud;
    SpatialVector Sun;
    uint32_t CloudSteps;
    FogRayReference(const LightComputeArchive &Input, uint32_t FogOffset, CloudParameters Density,
                    SpatialVector SolarDirection, uint32_t CloudSamples = 64)
        : Geometry(Input), Scene(Input), Offset(FogOffset), Cloud(Density), Sun(SolarDirection),
          CloudSteps(CloudSamples)
    {
    }
    SpectrumRgb Surface(const LightComputeReceiver &R) const
    {
        using namespace TransportReference;
        const float *F = Scene.Angles.data() + Offset;
        FogParameters P;
        P.DensityAtBase = F[0];
        P.BaseHeightM = F[1];
        P.FalloffHeightM = F[2];
        auto At = Vector(R.PositionHit), Normal = Vector(R.NormalRoughness);
        SpectrumRgb Sum = Colour(R.ResidualSpecular);
        double CloudT = MarchClouds(Cloud, At, Sun, Sun, {}, 40000, CloudSteps, 0).Transmittance;
        for (const auto &L : Geometry.Sources)
        {
            SpatialVector Toward;
            double Far;
            auto Incident = ReferenceLightArrival(L, At, Toward, Far);
            if (Incident.R == 0 && Incident.G == 0 && Incident.B == 0)
                continue;
            if (Geometry.Space.Occluded(Add(At, Mul(Normal, .002)), Toward, 0, Far - .004))
                continue;
            double T = FogTransmittance(P, At, Toward, std::min(Far, double(F[14])));
            if (L.Kind == ReferenceLight::Kind::Directional && Dot(L.Vector, Sun) > .9999)
                T *= CloudT;
            auto Response = EvaluateReferenceBrdf(Material(R), Normal, Vector(R.ViewMetallic), Toward);
            T *= std::max(Dot(Normal, Toward), 0.);
            Sum.R += Response.R * Incident.R * T;
            Sum.G += Response.G * Incident.G * T;
            Sum.B += Response.B * Incident.B * T;
        }
        return Sum;
    }
    FogRadianceIntegral Integrate(SpatialVector Origin, SpatialVector Ray, double Distance,
                                  uint32_t OverrideSteps = 0, uint32_t OverrideShadowSteps = 0,
                                  uint32_t Pixel = 0, uint32_t Seed = 71) const
    {
        using namespace TransportReference;
        const float *F = Scene.Angles.data() + Offset;
        FogParameters P;
        P.DensityAtBase = F[0];
        P.BaseHeightM = F[1];
        P.FalloffHeightM = F[2];
        uint32_t Bits = Seed ^ (Pixel * 747796405u + 2891336453u);
        auto Incident = [&](SpatialVector At) {
            SpectrumRgb Sum{F[8], F[9], F[10]};
            double CloudT = F[12] != 0
                                ? MarchClouds(Cloud, At, Sun, Sun, {}, 40000,
                                              OverrideShadowSteps ? OverrideShadowSteps : uint32_t(F[7]), 0)
                                      .Transmittance
                                : 1;
            if (F[13] != 0)
            {
                for (const auto &L : Geometry.Sources)
                {
                    SpatialVector Toward;
                    double Far;
                    auto Light = ReferenceLightArrival(L, At, Toward, Far);
                    if (Light.R == 0 && Light.G == 0 && Light.B == 0)
                        continue;
                    Far = std::min(Far, double(F[14]));
                    if (F[11] != 0 &&
                        Geometry.Space.Occluded(Add(At, Mul(Toward, .002)), Toward, 0, std::max(Far - .004, 0.)))
                        continue;
                    double Shade =
                        L.Kind == ReferenceLight::Kind::Directional && Dot(L.Vector, Sun) > .9999 ? CloudT : 1;
                    double Scale = Shade * HenyeyGreensteinPhase(Dot(Ray, Toward), F[4]) *
                                   FogTransmittance(P, At, Toward, Far);
                    Sum.R += Light.R * Scale;
                    Sum.G += Light.G * Scale;
                    Sum.B += Light.B * Scale;
                }
            }
            if (F[15] > 0)
            {
                auto Area = IntegrateFogFacets(Scene, Geometry.Space, P, At, Ray, F[4], F[14],
                                               F[11] != 0, Bits, uint32_t(F[15]));
                Sum.R += Area.R;
                Sum.G += Area.G;
                Sum.B += Area.B;
            }
            return Sum;
        };
        return IntegrateFogRadiance(P, Origin, Ray, Distance, OverrideSteps ? OverrideSteps : uint32_t(F[6]),
                                    F[3], Incident);
    }
};
#include "FogSamplingValidation.h"
#include "CloudBudgetValidation.h"

void ValidateFogScattering(const char *Driver, const std::string &Shaders,
                           const LightComputeArchive &Original, const std::string &Output, bool Capture,
                           bool SkyOnly = false, bool Sampling = false, bool Budget = false,
                           bool CompareBudget = true)
{
    using namespace TransportReference;
    constexpr double π = 3.141592653589793;
    FogParameters Uniform;
    Uniform.DensityAtBase = .03;
    Uniform.BaseHeightM = 10000;
    auto Closed = IntegrateFogRadiance(Uniform, {0, 0, 2}, {1, 0, 0}, 30, 32, .8,
                                       [](SpatialVector) { return SpectrumRgb{.25, .5, .75}; });
    double Beer = std::exp(-.9);
    Require(std::abs(Closed.Transmittance - Beer) < 1e-12 &&
                std::abs(Closed.Radiance.G - .8 * .5 * (1 - Beer)) < 1e-12,
            "Native fog scattering violates uniform closed form");
    FogParameters Height;
    double Horizontal = FogTransmittance(Height, {0, 0, 10}, {1, 0, 0}, 100);
    double NearHorizontal = FogTransmittance(Height, {0, 0, 10}, {1, 0, 1e-10}, 100);
    Require(std::abs(Horizontal - NearHorizontal) < 1e-9, "Near-horizontal fog column is unstable");
    std::cout << "FOG_ANALYTIC uniform_integral=1 beer=1 near_horizontal=1\n";
    auto Scene = Original;
    SetFogCamera(Scene, Capture ? ((Sampling || Budget) ? 160 : 320) : 80,
                 Capture ? ((Sampling || Budget) ? 100 : 200) : 50);
    auto Forward = Unit(SpatialVector{Scene.Camera[1][0], Scene.Camera[1][1], 0});
    auto Right = Vector(Scene.Camera[2]);
    auto HorizontalSun = Unit(Add(Mul(Forward, .9), Mul(Right, .45)));
    double Elevation = 20 * π / 180;
    auto Sun = SpatialVector{float(HorizontalSun.X * std::cos(Elevation)),
                             float(HorizontalSun.Y * std::cos(Elevation)), float(std::sin(Elevation))};
    AtmosphereParameters Atmosphere;
    auto Ground = TransmittanceToTop(Atmosphere, Atmosphere.GroundRadiusKm + .004, Sun.Z);
    auto Altitude = TransmittanceToTop(Atmosphere, Atmosphere.GroundRadiusKm + 1.3, Sun.Z);
    for (auto &L : Scene.Lights)
        if (L.RadianceCategory[3] == 0)
        {
            L.PositionReach = {float(Sun.X), float(Sun.Y), float(Sun.Z), 0};
            L.RadianceCategory = {float(20 * Ground.R), float(20 * Ground.G), float(20 * Ground.B), 0};
        }
    SpectrumRgb TopSun{20, 20, 20};
    Prepare(Scene, 4, 24, true, &TopSun);
    CloudParameters Cloud;
    Cloud.Coverage = .5;
    Cloud.DensityScale = 1.5;
    Cloud.AmbientRadiance = {.08, .10, .14};
    uint32_t CloudSteps = (Sampling || Budget) ? 64 : 32;
    uint32_t CloudOffset =
        AppendCloudParameters(Scene, Cloud, Sun, {20 * Altitude.R, 20 * Altitude.G, 20 * Altitude.B}, CloudSteps);
    Cloud.Coverage = Scene.Angles[CloudOffset + 2];
    Cloud.ExtinctionPerM = Scene.Angles[CloudOffset + 4];
    uint32_t FogOffset = AppendFogParameters(Scene);
    EmissiveQuadrature::AppendDistribution(Scene);
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Scene, Shaders);
    auto Execute = [&](const LightComputeArchive &Input, bool Fog, bool Resolve = true) {
        return Compute.Execute(Input, 0, 16, 71, 64, 0, true, true, Capture ? 4u : 0u, true, Capture, false,
                               CloudOffset, Fog ? FogOffset : UINT32_MAX, Resolve);
    };
    if (Budget)
    {
        ValidateCloudBudget(Compute, Scene, Cloud, Sun, CloudOffset, FogOffset, Output, CompareBudget);
        return;
    }
    if (Sampling)
    {
        ValidateFogSampling(Compute, Scene, Cloud, Sun, CloudOffset, FogOffset, Output, Capture);
        return;
    }
    if (SkyOnly)
    {
        auto Sky = Scene;
        auto Direction = Unit(Add(Mul(Forward, .96), SpatialVector{0, 0, .28}));
        auto Across = Unit(SpatialVector{Direction.Y, -Direction.X, 0}), Up = Cross(Across, Direction);
        Sky.Camera[1] = {float(Direction.X), float(Direction.Y), float(Direction.Z), 6000};
        Sky.Camera[2] = {float(Across.X), float(Across.Y), float(Across.Z), float(std::tan(.5) * 1.6)};
        Sky.Camera[3] = {float(Up.X), float(Up.Y), float(Up.Z), float(std::tan(.5))};
        Sky.Angles[CloudOffset + 1] = 1100;
        Sky.Angles[CloudOffset + 3] = 2.5f;
        Sky.Angles[CloudOffset + 4] = .02f;
        Cloud.TopHeightM = 1100;
        Cloud.DensityScale = 2.5;
        Cloud.ExtinctionPerM = Sky.Angles[CloudOffset + 4];
        for (uint32_t Case = 0; Case < 2; ++Case)
        {
            Sky.Angles[FogOffset + 12] = float(Case);
            auto Background = Execute(Sky, true, false);
            auto Pixels = Execute(Sky, true);
            auto Surfaces = Compute.PrimaryReceivers();
            FogRayReference Oracle(Sky, FogOffset, Cloud, Sun, CloudSteps);
            double Maximum = 0;
            uint32_t Probes = 0;
            for (uint32_t Pixel = 0; Pixel < Pixels.size(); Pixel += 997)
            {
                auto Origin = Vector(Sky.Camera[0]), Ray = FogCameraRay(Sky, Pixel);
                auto Delta = Sub(Vector(Surfaces[Pixel].PositionHit), Origin);
                double Distance =
                    Surfaces[Pixel].PositionHit[3] != 0
                        ? std::min(std::sqrt(Dot(Delta, Delta)), double(Sky.Angles[FogOffset + 5]))
                        : Sky.Angles[FogOffset + 5];
                auto Expected = Oracle.Integrate(Origin, Ray, Distance);
                std::array<double, 3> RGB{Expected.Radiance.R, Expected.Radiance.G, Expected.Radiance.B};
                for (uint32_t C = 0; C < 3; ++C)
                    Maximum = std::max(Maximum, std::abs(Background[Pixel][C] * Expected.Transmittance +
                                                         RGB[C] - Pixels[Pixel][C]));
                ++Probes;
            }
            Require(Maximum < .003, "Native thin-layer cloud shaft mismatch");
            std::cout << "FOG_SKY case=" << Case << " probes=" << Probes << " max_rgb_error=" << Maximum
                      << "\n";
            WriteImage(Output + "/FogSky" + std::to_string(Case) + ".ppm", Pixels, Sky.Width, Sky.Height);
        }
        return;
    }
    auto Dry = Execute(Scene, false);
    auto Receivers = Compute.PrimaryReceivers();
    if (Capture)
        WriteImage(Output + "/FogDry.ppm", Dry, Scene.Width, Scene.Height);
    for (uint32_t Case = 0; Case < (Capture ? 3u : 5u); ++Case)
    {
        auto Shot = Scene;
        float *F = Shot.Angles.data() + FogOffset;
        if (!Capture && Case == 0)
            F[0] = 0;
        if (!Capture && Case == 1)
        {
            F[0] = .03f;
            F[1] = 10000;
            F[3] = .8f;
            F[8] = .25f;
            F[9] = .5f;
            F[10] = .75f;
            F[13] = 0;
        }
        if (!Capture && Case == 2)
        {
            F[3] = 0;
            F[1] = 8;
        }
        if (!Capture && Case == 3)
            F[1] = 2;
        if (Capture && Case == 0)
            F[12] = 0;
        if ((Capture && Case == 2) || (!Capture && Case == 4))
        {
            Shot.Angles[CloudOffset + 6] = 120;
            Cloud.WindTimeS = 120;
        }
        else
            Cloud.WindTimeS = 0;
        auto Background = Execute(Shot, true, false);
        auto Fog = Execute(Shot, true);
        if (!Capture && Case == 0)
            Require(Fog == Dry, "Zero-density fog changed image bits");
        FogRayReference Oracle(Shot, FogOffset, Cloud, Sun, CloudSteps);
        double Maximum = 0, Mean = 0, SourceEnergy = 0, Convergence = 0, SurfaceError = 0,
               ShadowConvergence = 0;
        uint32_t SurfaceProbes = 0;
        uint32_t Probes = 0;
        uint32_t Stride = Capture ? 997 : 37;
        for (uint32_t Pixel = 0; Pixel < Fog.size(); Pixel += Stride)
        {
            auto Origin = Vector(Scene.Camera[0]), Ray = FogCameraRay(Scene, Pixel);
            double Distance =
                Receivers[Pixel].PositionHit[3] != 0
                    ? std::min(std::sqrt(Dot(Sub(Vector(Receivers[Pixel].PositionHit), Origin),
                                             Sub(Vector(Receivers[Pixel].PositionHit), Origin))),
                               double(F[5]))
                    : F[5];
            if (!Capture && Receivers[Pixel].PositionHit[3] != 0)
            {
                auto Lit = Oracle.Surface(Receivers[Pixel]);
                std::array<double, 3> RGB{Lit.R, Lit.G, Lit.B};
                for (uint32_t C = 0; C < 3; ++C)
                    SurfaceError = std::max(SurfaceError, std::abs(RGB[C] - Background[Pixel][C]));
                ++SurfaceProbes;
            }
            auto Expected = Oracle.Integrate(Origin, Ray, Distance);
            std::array<double, 3> RGB{Expected.Radiance.R, Expected.Radiance.G, Expected.Radiance.B};
            for (uint32_t C = 0; C < 3; ++C)
            {
                double Error =
                    std::abs(Background[Pixel][C] * Expected.Transmittance + RGB[C] - Fog[Pixel][C]);
                Maximum = std::max(Maximum, Error);
                Mean += Error;
                SourceEnergy += RGB[C];
            }
            if (Probes < 12)
            {
                auto Finer = Oracle.Integrate(Origin, Ray, Distance, 128);
                Convergence = std::max(Convergence, std::abs(Finer.Radiance.R - Expected.Radiance.R));
                auto ShadowFiner = Oracle.Integrate(Origin, Ray, Distance, 0, 128);
                ShadowConvergence =
                    std::max(ShadowConvergence, std::abs(ShadowFiner.Radiance.R - Expected.Radiance.R));
            }
            ++Probes;
        }
        Require(Maximum < .003 && SurfaceError < .003, "Native/GPU shadowed fog disagrees");
        std::cout << "FOG_RAY capture=" << Capture << " case=" << Case << " probes=" << Probes
                  << " max_rgb_error=" << Maximum << " mean_rgb_error=" << Mean / (Probes * 3)
                  << " source_rgb_sum=" << SourceEnergy << " view_128step_max_red_delta=" << Convergence
                  << " surface_probes=" << SurfaceProbes << " surface_max_rgb_error=" << SurfaceError
                  << " shadow_128step_max_red_delta=" << ShadowConvergence << "\n";
        if (Capture)
            WriteImage(Output + "/FogShafts" + std::to_string(Case) + ".ppm", Fog, Scene.Width, Scene.Height);
    }
    auto Night = Scene;
    Night.Camera[10][3] = 0;
    for (auto &L : Night.Lights)
        if (L.RadianceCategory[3] == 0)
            L.RadianceCategory = {0, 0, 0, 0};
    float *N = Night.Angles.data() + FogOffset;
    N[0] = .07f;
    N[2] = 10;
    N[4] = .35f;
    N[5] = 70;
    N[6] = 64;
    N[8] = 0;
    N[9] = 0;
    N[10] = 0;
    N[12] = 0;
    auto NightDry = Execute(Night, false);
    auto NightBackground = Execute(Night, true, false), NightFog = Execute(Night, true);
    auto NightReceivers = Compute.PrimaryReceivers();
    FogRayReference NightOracle(Night, FogOffset, Cloud, Sun, CloudSteps);
    double NightError = 0, NightEnergy = 0;
    uint32_t NightProbes = 0;
    for (uint32_t Pixel = 0; Pixel < NightFog.size(); Pixel += Capture ? 997 : 37)
    {
        auto Origin = Vector(Scene.Camera[0]), Ray = FogCameraRay(Scene, Pixel);
        auto Delta = Sub(Vector(NightReceivers[Pixel].PositionHit), Origin);
        double Distance = NightReceivers[Pixel].PositionHit[3] != 0
                              ? std::min(std::sqrt(Dot(Delta, Delta)), double(N[5]))
                              : N[5];
        auto Expected = NightOracle.Integrate(Origin, Ray, Distance);
        std::array<double, 3> RGB{Expected.Radiance.R, Expected.Radiance.G, Expected.Radiance.B};
        for (uint32_t C = 0; C < 3; ++C)
        {
            NightError = std::max(NightError, std::abs(NightBackground[Pixel][C] * Expected.Transmittance +
                                                       RGB[C] - NightFog[Pixel][C]));
            NightEnergy += RGB[C];
        }
        ++NightProbes;
    }
    Require(NightError < .003 && NightEnergy > 0, "Native point/IES/spot fog illumination disagrees");
    std::cout << "FOG_LOCAL capture=" << Capture << " probes=" << NightProbes
              << " max_rgb_error=" << NightError << " source_rgb_sum=" << NightEnergy << "\n";
    if (Capture)
    {
        WriteImage(Output + "/FogLocalDry.ppm", NightDry, Scene.Width, Scene.Height);
        WriteImage(Output + "/FogLocal.ppm", NightFog, Scene.Width, Scene.Height);
    }
    if (!Capture)
    {
        auto Open = Night;
        Open.Angles[FogOffset + 11] = 0;
        auto OpenFog = Execute(Open, true);
        Require(OpenFog != NightFog, "Geometry did not occlude any local-light fog samples");
        std::cout << "FOG_OCCLUSION geometry_changes_scattering=1\n";
        {
            LightComputeSubmission DryHistory(Driver);
            DryHistory.Prepare(Scene, Shaders);
            Compute.Execute(Scene, 3, 16, 71, 64, 3, true, true, 0, true, false, false, CloudOffset,
                            FogOffset);
            DryHistory.Execute(Scene, 3, 16, 71, 64, 3, true, true, 0, true, false, false, CloudOffset,
                               FogOffset, false);
            auto Removed = Compute.Execute(Scene, 3, 16, 72, 64, 3, false, true, 0, true, false, false,
                                           CloudOffset, FogOffset, false);
            auto DryUpdate = DryHistory.Execute(Scene, 3, 16, 72, 64, 3, false, true, 0, true, false, false,
                                                CloudOffset, FogOffset, false);
            Require(Removed == DryUpdate, "Fog contaminated persistent surface-light history");
            std::cout << "FOG_HISTORY dry_history_bitwise_equal=1\n";
            auto Off =
                Compute.Execute(Scene, 3, 16, 71, 64, 3, false, true, 0, true, false, false, CloudOffset);
            auto OffCold =
                DryHistory.Execute(Scene, 3, 16, 71, 64, 3, true, true, 0, true, false, false, CloudOffset);
            Require(Off == OffCold, "Disabling fog retained stale surface lighting");
            auto On = Compute.Execute(Scene, 3, 16, 71, 64, 3, false, true, 0, true, false, false,
                                      CloudOffset, FogOffset);
            auto OnCold = DryHistory.Execute(Scene, 3, 16, 71, 64, 3, true, true, 0, true, false, false,
                                             CloudOffset, FogOffset);
            Require(On == OnCold, "Enabling fog retained stale surface lighting");
            std::cout << "FOG_TOGGLE enabled_cold_equal=1 disabled_cold_equal=1\n";
        }
    }
    if (!Capture)
    {
        auto Invalid = Scene;
        Invalid.Angles[FogOffset + 6] = 129;
        bool Rejected = false;
        try
        {
            (void)Execute(Invalid, true);
        }
        catch (const std::runtime_error &)
        {
            Rejected = true;
        }
        Require(Rejected, "Unbounded fog march reached dispatch");
        std::cout << "FOG_BOUNDS invalid_steps_rejected=1\n";
    }
}
