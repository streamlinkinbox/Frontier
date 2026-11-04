/*==========================================================================================================================================
                                                          LOCALMEDIAVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Native Vulkan comparisons of local-media wind, heterogeneous extinction and bounded visibility; no browser or painted substitutes.

#include "Engine/PlanetaryAtmosphere/PlanetaryEnsemble.h"
#include "Engine/PhysicalDynamics/ForceField.h"

#include <stb_image_write.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

using namespace Frontier;
constexpr uint32_t Width  = 256;
constexpr uint32_t Height = 160;

std::array<float, 3> Normalize(std::array<float, 3> Direction)
{
    const float Length = std::hypot(Direction[0], Direction[1], Direction[2]);
    for (auto &Axis : Direction)
        Axis /= Length;
    return Direction;
}

void Require(bool Condition, const std::string &Diagnostic)
{
    if (!Condition) throw std::runtime_error(Diagnostic);
}

double Difference(const std::vector<float> &First, const std::vector<float> &Second)
{
    Require(First.size() == Second.size(), "Mismatched image extents");
    double Sum = 0;
    for (size_t Channel = 0; Channel < First.size(); ++Channel)
    {
        if (Channel % 4 == 3) continue;
        Require(std::isfinite(First[Channel]) && std::isfinite(Second[Channel]), "Non-finite native radiance");
        Sum += std::abs(double(First[Channel]) - Second[Channel]);
    }
    return Sum / (Width * Height * 3);
}

void EncodeImage(const std::filesystem::path &Filename, const std::vector<float> &Radiance)
{
    std::vector<uint8_t> Pixels(Width * Height * 3);
    for (size_t Pixel = 0; Pixel < Width * Height; ++Pixel)
    {
        for (size_t Band = 0; Band < 3; ++Band)
        {
            const double Light       = std::max(0.0f, Radiance[Pixel * 4 + Band]);
            Pixels[Pixel * 3 + Band] = static_cast<uint8_t>(std::lround(255 * std::pow(Light / (1 + Light), 1 / 2.2)));
        }
    }
    Require(stbi_write_png(Filename.string().c_str(), Width, Height, 3, Pixels.data(), Width * 3), "PNG output failed");
}

PlanetaryRealmEnsemble ConstructScene()
{
    PlanetaryRealmEnsemble Ensemble;
    Ensemble.Realms.resize(1);
    auto &Realm                      = Ensemble.Realms[0];
    Realm.Name                       = "Local media verification";
    Realm.HeightFog.ReferenceDensity = 0;
    Realm.EnableAerialPerspective    = false;
    Realm.StarBrightnessScale = Realm.MilkyWayIntensity = Realm.ScintillationStrength = 0;
    Realm.GroundAlbedoRgb                                                             = {0.17f, 0.19f, 0.21f};
    RealmSolarSource Sun;
    Sun.DirectionUnit   = Normalize({-0.6f, 0.8f, -0.3f});
    Sun.DiscIrradiance  = {3.0f, 2.8f, 2.5f};
    Sun.MasterFlareGain = 0;
    Realm.Suns.push_back(Sun);
    Realm.SolidOccluders.push_back({{11, 3, 17}, 3, {0.65f, 0.25f, 0.12f}, 0.3f});
    Realm.SolidOccluders.push_back({{-12, 2, 20}, 2, {0.14f, 0.4f, 0.55f}, 0.3f});
    PlanetaryWindProfile Wind;
    Wind.SpeedMetersPerSecond = 0;
    Wind.GustAmplitude = Wind.ShearPerKilometer = Wind.VeerRadiansPerKilometer = Wind.SwirlStrength = 0;
    Realm.ForceField.SetWindProfile(Wind);
    return Ensemble;
}

PlanetaryCameraView ConstructCamera()
{
    PlanetaryCameraView Camera;
    Camera.PositionMeters = {0, 7, -28};
    Camera.ForwardUnit    = Normalize({0, -3, 40});
    Camera.RightUnit      = {1, 0, 0};
    Camera.UpUnit         = {0, Camera.ForwardUnit[2], -Camera.ForwardUnit[1]};
    Camera.VerticalFovRad = 0.85f;
    Camera.ViewportWidth  = Width;
    Camera.ViewportHeight = Height;
    return Camera;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    NATIVE PIPELINE COMPARISONS
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char **Arguments)
{
    using namespace Frontier;
    try
    {
        Require(ArgumentCount >= 4, "Expected repository, shader directory, output directory, optional --baseline");
        const auto Root     = std::filesystem::absolute(Arguments[1]);
        const auto Shaders  = std::filesystem::absolute(Arguments[2]);
        const auto Output   = std::filesystem::absolute(Arguments[3]);
        const bool Baseline = ArgumentCount > 4 && std::string(Arguments[4]) == "--baseline";
        std::filesystem::create_directories(Output);
        std::ofstream         Report(Output / "Checks.tsv");
        VulkanComputeExchange Exchange;
        std::string           Diagnostic;
        Require(Exchange.Initialize((Root / "ExternalPackages/VulkanCompute/SwiftShader/libvulkan.so.1").string(), &Diagnostic),
                Diagnostic);
        Report << "Device\t" << Exchange.DeviceName() << '\n';
        PlanetarySubmissionSequence Sequence;
        const auto                  Maps = Root / "EngineContent/EnvironmentArchives/CelestialMaps";
        Require(Sequence.Initialize(Exchange, Shaders.string(), (Maps / "MoonAlbedoSource2048.jpg").string(),
                                    (Maps / "Hipparcos8.csv").string(), &Diagnostic),
                Diagnostic);
        auto Ensemble = ConstructScene();
        auto Camera   = ConstructCamera();
        auto Render   = [&](const char *Name)
        {
            std::vector<float> Radiance;
            Require(Sequence.ExecuteFrame(Exchange, Ensemble, Camera), "Native sequence execution failed");
            Require(Sequence.ReadbackHdrFrame(Exchange, Radiance), "Native radiance readback failed");
            Require(Radiance.size() == Width * Height * 4, "Native image extent mismatch");
            EncodeImage(Output / (std::string(Name) + ".png"), Radiance);
            return Radiance;
        };
        auto                 &Realm = Ensemble.Realms[0];
        LocalFogVolumeProfile Fog;
        Fog.BoxCenterM                           = {0, 4, 12};
        Fog.BoxHalfExtentsM                      = {9, 5, 12};
        Fog.BoxEdgeFalloffM                      = 2;
        Fog.ExtinctionPerM                       = .14;
        Fog.NoiseScaleM                          = 2;
        Fog.NoiseModulation                      = 1;
        Fog.PhaseAsymmetryG                      = .25;
        Fog.HeightFalloffM                       = 10000;
        Fog.WindTimeS                            = 4;
        Fog.WindVelocityMps                      = {5, 0, 0};
        Realm.LocalFogVolumes                    = {Fog};
        Camera.ElapsedSeconds                    = 4;
        const auto East                          = Render("Fog-East");
        Realm.LocalFogVolumes[0].WindVelocityMps = {-5, 0, 0};
        const auto West                          = Render("Fog-West");
        Realm.LocalFogVolumes[0].WindVelocityMps = {0, 0, 0};
        Camera.ElapsedSeconds                    = 0;
        const auto StillFirst                    = Render("Fog-Still-0");
        Camera.ElapsedSeconds                    = 4;
        const auto StillLater                    = Render("Fog-Still-4");
        Realm.LocalFogVolumes[0].NoiseModulation = 0;
        const auto   Uniform                     = Render("Fog-Uniform");
        const double WindDifference              = Difference(East, West);
        const double FrozenDifference            = Difference(StillFirst, StillLater);
        const double NoiseDifference             = Difference(Uniform, StillLater);
        Report << "Fog wind reversal\tExpected > 0.00001\t" << WindDifference << '\n';
        Report << "Fog stopped velocity\tExpected < 0.000001\t" << FrozenDifference << '\n';
        Report << "Fog heterogeneous density\tExpected > 0.00001\t" << NoiseDifference << '\n';
        std::cout << "Fog wind reversal: " << WindDifference << "; stopped velocity: " << FrozenDifference
                  << "; density variation: " << NoiseDifference << '\n';
        if (!Baseline)
        {
            for (VolumeShape Shape : {VolumeShape::Box, VolumeShape::Ellipsoid, VolumeShape::Sphere, VolumeShape::Hemisphere})
            {
                Realm.LocalFogVolumes[0] = Fog;
                auto &Volume             = Realm.LocalFogVolumes[0];
                Volume.WindVelocityMps   = {0, 0, 0};
                Volume.BoundsShape       = Shape;
                Volume.HeightFalloffM    = 0;
                Camera.FogMarchSamples   = 64;
                Render(("Fog-Shape-" + std::to_string(static_cast<uint32_t>(Shape))).c_str());
                Require(EvaluateVolumeEnvelope({100, 0, 0}, {0, 0, 0}, {5, 5, 5}, Shape, 1, 1) == 0, "Volume extends outside its bounds");
                Require(EvaluateVolumeEnvelope({4.99, 0, 0}, {0, 0, 0}, {5, 5, 5}, Shape, 1, 1) < .001,
                        "Volume edge does not fade smoothly");
            }
            Require(EvaluateVolumeEnvelope({0, -1, 0}, {0, 0, 0}, {5, 5, 5}, VolumeShape::Hemisphere, 1, 1) == 0,
                    "Hemisphere extends below its base");
            auto Wind                 = Realm.ForceField.WindProfile();
            Wind.SpeedMetersPerSecond = 4;
            Wind.BearingRadians       = 0;
            Realm.ForceField.SetWindProfile(Wind);
            Realm.ForceField.SetElapsedSeconds(4);
            Realm.LocalFogVolumes[0]             = Fog;
            Realm.LocalFogVolumes[0].FollowField = true;
            const auto FollowingEast             = Render("Fog-Field-East");
            Wind.BearingRadians                  = 3.141592653589793;
            Realm.ForceField.SetWindProfile(Wind);
            const auto   FollowingWest   = Render("Fog-Field-West");
            const double FieldDifference = Difference(FollowingEast, FollowingWest);
            Report << "Fog Force Field reversal\tExpected > 0.00001\t" << FieldDifference << '\n';
            Require(FieldDifference > .00001, "Local fog does not sample its Force Field");
            Realm.LocalFogVolumes.clear();
            CloudVolumeProfile Cloud;
            Cloud.Domain                = CloudVolumeDomain::BoundedLocalBox;
            Cloud.BoundsShape           = VolumeShape::Ellipsoid;
            Cloud.BoxCenterM            = {0, 6, 12};
            Cloud.BoxHalfExtentsM       = {8, 5, 9};
            Cloud.BoxEdgeFalloffM       = 2;
            Cloud.FeatureScaleMetres    = 5;
            Cloud.Slab.Coverage         = .95;
            Cloud.Slab.DensityScale     = 3;
            Cloud.Slab.ExtinctionPerM   = .12;
            Cloud.Slab.WindXMps         = 0;
            Cloud.WorleyErosionStrength = .05;
            Cloud.FollowField           = true;
            Cloud.ViewSamples           = 64;
            Realm.CloudVolumes          = {Cloud};
            Wind.BearingRadians         = 0;
            Realm.ForceField.SetWindProfile(Wind);
            const auto CloudEast = Render("Cloud-Field-East");
            Wind.BearingRadians  = 3.141592653589793;
            Realm.ForceField.SetWindProfile(Wind);
            const auto   CloudWest       = Render("Cloud-Field-West");
            const double CloudDifference = Difference(CloudEast, CloudWest);
            Report << "Cloud Force Field reversal\tExpected > 0.00001\t" << CloudDifference << '\n';
            Require(CloudDifference > .00001, "Local cloud does not sample its Force Field");
            Realm.CloudVolumes[0].ViewSamples = 128;
            const auto Reference              = Render("Cloud-128");
            Realm.CloudVolumes[0].ViewSamples = 64;
            const auto MediumQuality          = Render("Cloud-64");
            Realm.CloudVolumes[0].ViewSamples = 32;
            const auto LowQuality             = Render("Cloud-32");
            Report << "Cloud 32 to 128 mean RGB error\t" << Difference(Reference, LowQuality) << '\n';
            Report << "Cloud 64 to 128 mean RGB error\t" << Difference(Reference, MediumQuality) << '\n';
            Require(Difference(Reference, MediumQuality) < Difference(Reference, LowQuality), "Cloud samples did not converge");
            Camera.CloudResolutionPercent = 25;
            Render("Cloud-QuarterResolution");
            const auto Extent = Sequence.Clouds().QueryTraceExtent();
            Require(Extent[0] == 64 && Extent[1] == 40, "Cloud resolution did not change actual trace dispatch extent");
            Report << "Cloud 25 percent trace extent\t64 x 40\t" << Extent[0] << " x " << Extent[1] << '\n';
        }
        if (!Baseline)
        {
            // 📝 A deliberately backlit photographic comparison makes interior density variation visible.
            //    These remain native volume integrals; no noise or fog is drawn into the exported PNG afterward.
            Realm.CloudVolumes.clear();
            Realm.Suns[0].DirectionUnit  = Normalize({-.25f, .18f, .95f});
            Realm.Suns[0].DiscIrradiance = {18, 17, 16};
            ++Realm.SkyViewRevision;
            Camera.PositionMeters         = {0, 6, -18};
            Camera.ForwardUnit            = {0, 0, 1};
            Camera.RightUnit              = {1, 0, 0};
            Camera.UpUnit                 = {0, 1, 0};
            Camera.CloudResolutionPercent = 100;
            Camera.FogMarchSamples        = 128;
            LocalFogVolumeProfile Showcase;
            Showcase.BoxCenterM                      = {0, 5, 10};
            Showcase.BoxHalfExtentsM                 = {10, 6, 10};
            Showcase.BoxEdgeFalloffM                 = 2;
            Showcase.BoundsShape                     = VolumeShape::Ellipsoid;
            Showcase.ExtinctionPerM                  = .05;
            Showcase.NoiseScaleM                     = 6;
            Showcase.HeightFalloffM                  = 0;
            Showcase.PhaseAsymmetryG                 = .7;
            Showcase.NoiseModulation                 = 0;
            Realm.LocalFogVolumes                    = {Showcase};
            const auto Smooth                        = Render("Showcase-Fog-Uniform");
            Realm.LocalFogVolumes[0].NoiseModulation = 1;
            const auto   Patchy                      = Render("Showcase-Fog-Noisy");
            const double VisibleNoise                = Difference(Smooth, Patchy);
            Report << "Backlit heterogeneous fog\t" << VisibleNoise << '\n';
            Require(VisibleNoise > .0001, "Backlit fog heterogeneity is not visible in native pixels");
            Realm.LocalFogVolumes[0].WindVelocityMps = {2, 0, 0};
            for (int Time = 0; Time < 4; ++Time)
            {
                Realm.LocalFogVolumes[0].WindTimeS = Time * 2;
                Render(("Showcase-Fog-Wind-" + std::to_string(Time)).c_str());
            }
            Realm.LocalFogVolumes[0].SelfShadow = true;
            Render("Showcase-Fog-SelfShadow");
        }
        Sequence.Shutdown(Exchange);
        if (Baseline)
        {
            Report << "Baseline\tRecorded existing behaviour, not an acceptance pass\n";
            return 0;
        }
        Require(WindDifference > 0.00001, "Fog did not respond to wind reversal");
        Require(FrozenDifference < 0.000001, "Fog moved despite zero velocity");
        Require(NoiseDifference > 0.00001, "Fog heterogeneity control has no effect");
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << Failure.what() << '\n';
        return 1;
    }
}
