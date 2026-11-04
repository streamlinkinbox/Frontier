/*==========================================================================================================================================
                                                        CELESTIALRENDERVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Native Vulkan evidence that authored lunar and planetary parameters alter the rendered pixels, including all four Moon slots.

#include "Engine/PlanetaryAtmosphere/CelestialAuthoring.h"
#include "Engine/PlanetaryAtmosphere/PlanetaryEnsemble.h"
#include <stb_image_write.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Frontier;
constexpr int Width = 320, Height = 240;
std::ofstream Report;
int Checks = 0;
void Require(bool Success, const std::string &Caption)
{
    ++Checks;
    Report << (Success ? "PASS" : "FAIL") << '\t' << (Caption.empty() ? "Native Vulkan setup" : Caption) << '\n';
    Report.flush();
    if (!Success)
        throw std::runtime_error(Caption);
}
double Difference(const std::vector<float> &First, const std::vector<float> &Second)
{
    double Sum = 0;
    for (size_t Index = 0; Index < First.size(); ++Index)
    {
        if (!std::isfinite(First[Index]) || !std::isfinite(Second[Index]))
            throw std::runtime_error("Non-finite celestial image");
        if (Index % 4 != 3)
            Sum += std::abs(double(First[Index]) - Second[Index]);
    }
    return Sum / (Width * Height * 3);
}
double Mean(const std::vector<float> &Image)
{
    double Sum = 0;
    for (size_t Index = 0; Index < Image.size(); ++Index)
        if (Index % 4 != 3)
            Sum += Image[Index];
    return Sum / (Width * Height * 3);
}
void Write(const std::filesystem::path &Path, const std::vector<float> &Radiance)
{
    std::vector<uint8_t> Pixels(Width * Height * 3);
    for (int Pixel = 0; Pixel < Width * Height; ++Pixel)
        for (int Channel = 0; Channel < 3; ++Channel)
        {
            const double Light = std::max(0.0f, Radiance[Pixel * 4 + Channel]);
            Pixels[Pixel * 3 + Channel] = uint8_t(std::lround(255 * std::pow(Light / (1 + Light), 1 / 2.2)));
        }
    Require(stbi_write_png(Path.string().c_str(), Width, Height, 3, Pixels.data(), Width * 3) != 0, "Native image written");
}
} // namespace

int main(int Count, char **Arguments)
{
    using namespace Frontier;
    try
    {
        if (Count != 4)
            throw std::runtime_error("Expected repository, shader directory, output directory");
        const auto Root = std::filesystem::absolute(Arguments[1]);
        const auto Output = std::filesystem::absolute(Arguments[3]);
        std::filesystem::create_directories(Output);
        Report.open(Output / "GpuChecks.tsv");
        VulkanComputeExchange Exchange;
        std::string Diagnostic;
        Require(Exchange.Initialize((Root / "ExternalPackages/VulkanCompute/SwiftShader/libvulkan.so.1").string(), &Diagnostic),
                Diagnostic);
        PlanetarySubmissionSequence Sequence;
        const auto Maps = Root / "EngineContent/EnvironmentArchives/CelestialMaps";
        Require(Sequence.Initialize(Exchange, Arguments[2], (Maps / "MoonAlbedoSource2048.jpg").string(),
                                    (Maps / "Hipparcos8.csv").string(), &Diagnostic),
                Diagnostic);
        PlanetaryRealmEnsemble Ensemble;
        Ensemble.Realms.resize(1);
        auto &Realm = Ensemble.Realms[0];
        Realm.Atmosphere.RayleighScattering = {0, 0, 0};
        Realm.Atmosphere.MieScattering = Realm.Atmosphere.MieAbsorption = 0;
        Realm.Atmosphere.OzoneAbsorption = {0, 0, 0};
        Realm.StarBrightnessScale = Realm.MilkyWayIntensity = 0;
        Realm.HeightFog.ReferenceDensity = 0;
        Realm.EnableAerialPerspective = false;
        Realm.GroundPlaneY = -10000;
        PlanetaryCameraView Camera;
        Camera.ViewportWidth = Width;
        Camera.ViewportHeight = Height;
        Camera.ForwardUnit = {0, .5f, .8660254f};
        Camera.RightUnit = {1, 0, 0};
        Camera.UpUnit = {0, .8660254f, -.5f};
        Camera.VerticalFovRad = .8f;
        auto Render = [&](const char *Name)
        {
            Require(Sequence.ExecuteFrame(Exchange, Ensemble, Camera), "Native celestial refresh");
            std::vector<float> Pixels;
            Require(Sequence.ReadbackHdrFrame(Exchange, Pixels), "Native celestial readback");
            Write(Output / (std::string(Name) + ".png"), Pixels);
            return Pixels;
        };
        MoonInstanceConfiguration Moon;
        Moon.Visible = true;
        Moon.AzimuthDegrees = -90;
        Moon.ElevationDegrees = 30;
        Moon.AngularDiameterDegrees = 12;
        Moon.PhaseDays = 14.765f;
        Moon.Glow = 0;
        Realm.Moons = {ResolveAuthoredMoon(Moon)};
        const auto Full = Render("Moon-Full");
        Moon.PhaseDays = 0;
        Realm.Moons = {ResolveAuthoredMoon(Moon)};
        const auto New = Render("Moon-New");
        Require(Mean(Full) > Mean(New) * 2, "Moon phase control changes lit fraction without requiring a visible Sun");
        Moon.PhaseDays = 14.765f;
        Moon.AngularDiameterDegrees = 4;
        Realm.Moons = {ResolveAuthoredMoon(Moon)};
        const auto Small = Render("Moon-Small");
        Require(Mean(Full) > Mean(Small) * 3, "Moon angular diameter changes native projected area");
        Moon.AngularDiameterDegrees = 12;
        Moon.SpinDegrees = 90;
        Realm.Moons = {ResolveAuthoredMoon(Moon)};
        const auto Rotated = Render("Moon-Rotated");
        Require(Difference(Full, Rotated) > 1e-4, "Moon spin changes actual atlas orientation");
        Moon.Brightness = 0;
        Moon.Glow = 0;
        Realm.Moons = {ResolveAuthoredMoon(Moon)};
        const auto Dark = Render("Moon-Dimmed");
        Require(Mean(Dark) < Mean(Full) * .2, "Moon brightness affects native radiance");
        Moon.Brightness = 1;
        Moon.Glow = 3;
        Realm.Moons = {ResolveAuthoredMoon(Moon)};
        const auto Glow = Render("Moon-Glow");
        Require(Difference(Rotated, Glow) > 1e-5, "Moon glow changes aureole pixels");
        Realm.Moons.clear();
        for (uint32_t Slot = 0; Slot < 4; ++Slot)
        {
            Moon.Body = Slot;
            Moon.AngularDiameterDegrees = 7;
            Moon.Glow = .2f;
            Moon.SpinDegrees = 0;
            Moon.AzimuthDegrees = -105 + Slot * 10.0f;
            Realm.Moons.push_back(ResolveAuthoredMoon(Moon));
        }
        const auto Four = Render("Four-Moons");
        Require(Realm.Moons.size() == 4, "All four authored slots reach native payload");
        Realm.Moons.pop_back();
        const auto Three = Render("Three-Moons");
        Require(Difference(Four, Three) > 1e-5, "Fourth Moon slot contributes rendered pixels");
        Realm.Moons.clear();
        RealmSolarSource Sun;
        Sun.DirectionUnit = {-.3f, .1f, -.94868f};
        Sun.DiscIrradiance = {4, 3.8f, 3.6f};
        Sun.EnableLensFlare = false;
        Realm.Suns = {Sun};
        ++Realm.SkyViewRevision;
        for (uint32_t Body = 0; Body < 4; ++Body)
        {
            PlanetConfiguration Planet;
            Planet.Body = Body;
            Planet.ApparentDiameterDegrees = 12;
            Planet.FollowSky = false;
            Planet.TiltDegrees = 30;
            Planet.RingSystem = Body == 3;
            Planet.AtmosphereShell = Body == 0;
            const auto Direction = ResolveCelestialDirection(-90, 30);
            Realm.Planets = {ResolveAuthoredPlanet(Planet, Direction)};
            const auto Initial = Render((std::string(PlanetBodyNames[Body]) + "-Initial").c_str());
            if (Body == 0)
                Planet.CloudVeil = 0;
            if (Body == 1)
                Planet.BandContrast = 0;
            if (Body == 2)
                Planet.SurfaceDetail = 0;
            if (Body == 3)
                Planet.RingSystem = false;
            Realm.Planets = {ResolveAuthoredPlanet(Planet, Direction)};
            const auto Edited = Render((std::string(PlanetBodyNames[Body]) + "-Edited").c_str());
            const double Delta = Difference(Initial, Edited);
            Report << "Image difference\t" << PlanetBodyNames[Body] << '\t' << Delta << '\n';
            Require(Delta > 1e-6, std::string(PlanetBodyNames[Body]) + " infographic control affects native pixels");
            Planet.Brightness = 0;
            Realm.Planets = {ResolveAuthoredPlanet(Planet, Direction)};
            const auto Invisible = Render((std::string(PlanetBodyNames[Body]) + "-Dimmed").c_str());
            Require(Mean(Invisible) < Mean(Edited), "Planet brightness reaches native renderer");
        }
        Realm.Planets.clear();
        Realm.Suns.clear();
        Realm.StarBrightnessScale = 1;
        Realm.StarLimitingMagnitude = 6;
        Realm.StarPointSize = 1;
        Realm.ScintillationStrength = 0;
        Camera.ElapsedSeconds = 0;
        const auto Stars = Render("Stars-Normal");
        Realm.StarPointSize = 3;
        const auto WideStars = Render("Stars-Large");
        Require(Difference(Stars, WideStars) > 1e-6, "Native star point size changes pixels");
        Realm.StarHalo = 2;
        const auto HaloStars = Render("Stars-Halo");
        Require(Difference(WideStars, HaloStars) > 1e-6, "Native star halo changes pixels");
        Camera.ElapsedSeconds = 4;
        const auto FrozenStars = Render("Stars-Twinkle-Off");
        Require(Difference(HaloStars, FrozenStars) < 1e-7, "Disabled scintillation is actually stationary");
        Realm.ScintillationStrength = 1;
        const auto TwinkleStars = Render("Stars-Twinkle-On");
        Require(Difference(FrozenStars, TwinkleStars) > 1e-6, "Scintillation switch changes native radiance");
        Camera.PositionMeters[1] = 150000;
        Realm.ObserverAltitudeKm = 150;
        ++Realm.SkyViewRevision;
        const auto Space = Render("Space-Navigation");
        Require(std::isfinite(Mean(Space)), "Space view produces finite native radiance");
        std::vector<float> Origins;
        Require(Sequence.ReadbackVisibilityLayer(Exchange, 1, Origins), "Space visibility readback");
        Require(std::abs(Origins[(Height / 2 * Width + Width / 2) * 4 + 1] - 150000) < 1,
                "Space camera origin retains float32 precision beyond half-float range");
        Sequence.Shutdown(Exchange);
        Report << "SUMMARY\t" << Checks << " checks passed\n";
        std::cout << Checks << " celestial GPU checks passed\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << Failure.what() << '\n';
        return 1;
    }
}
