/*==========================================================================================================================================
                                                      SCENEVIEWPORTVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Renders the editor's scene viewport offscreen — the same SceneViewport the Everest workspace presents — through the level's
//    environment records at day, low sun, dusk fog and night, and checks what each image must show.

#include "Frontier/Engine/Rendering/SceneViewport/SceneViewport.h"

#include "stb_image_write.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace Frontier;
using namespace Frontier::GlobalIllumination;

namespace
{

int Failures = 0;

void ReportCheck(bool Passed, const std::string& Name, const std::string& Detail = "")
{
    std::printf("%s %s%s%s\n", Passed ? "[PASS]" : "[FAIL]", Name.c_str(), Detail.empty() ? "" : " — ", Detail.c_str());
    if (!Passed) ++Failures;
}

// 📝 The records of Projects/Everest/Content/Levels/Default.levelspace (values the level overrides, defaults elsewhere).
SceneEnvironment ConstructLevelEnvironment()
{
    SceneEnvironment S;
    S.Solar.ElevationDegrees      = 38.0f;
    S.Solar.AzimuthDegrees        = 132.0f;
    S.Solar.IlluminanceKilolux    = 98.0f;
    S.Solar.KelvinTemperature     = 5778.0f;
    S.Flare.Intensity             = 0.62f;
    S.Flare.ApertureBladeCount    = 7;
    S.Atmosphere.RayleighScattering   = 1.0f;
    S.Atmosphere.MieTurbidity         = 2.3f;
    S.Atmosphere.OzoneAbsorptionScale = 1.0f;
    S.Clouds.Coverage             = 0.42f;
    S.Clouds.BaseAltitudeMetres   = 1750.0f;
    S.Clouds.LayerThicknessMetres = 1350.0f;
    S.Stars.HipparcosCount        = 5000;
    S.Stars.MagnitudeGain         = 1.15f;
    S.Moons.PrimaryPhase          = 0.68f;
    S.Moons.SecondaryMoonEnabled  = true;
    S.Fog.BaseDensity             = 0.012f;
    S.Fog.HeightFalloff           = 0.14f;
    S.Wind.DirectionDegrees       = 45.0f;
    S.Wind.SpeedMetresPerSecond   = 6.5f;
    S.Wind.CurlNoiseTurbulence    = 0.45f;
    return S;
}

struct ImageStatistics
{
    double MeanLuminance   = 0.0;   // [-] mean over the image
    double SkyLuminance    = 0.0;   // [-] mean over the top eighth
    double GroundLuminance = 0.0;   // [-] mean over the bottom quarter
    double Brightest       = 0.0;   // [-] brightest pixel
    uint32_t StarPixels    = 0;     // [-] isolated bright pixels in the top half
};

// 📝 IEEE half → float (the Rgba16Float targets read back).
float DecodeHalf(uint16_t Bits)
{
    const uint32_t Sign = (Bits >> 15) & 1u, Exponent = (Bits >> 10) & 31u, Mantissa = Bits & 1023u;
    float Magnitude;
    if (Exponent == 0) Magnitude = std::ldexp(static_cast<float>(Mantissa), -24);
    else if (Exponent == 31) Magnitude = Mantissa ? NAN : INFINITY;
    else Magnitude = std::ldexp(static_cast<float>(Mantissa + 1024u), static_cast<int>(Exponent) - 25);
    return Sign ? -Magnitude : Magnitude;
}

// 📝 A host Rgba16Float texture as floats (four per texel).
std::vector<float> ReadHalfTexture(DeviceExchange& Exchange, const std::shared_ptr<DeviceTexture>& Texture)
{
    std::vector<uint8_t> Bytes;
    std::vector<float>   Floats;
    if (!Texture || !Exchange.ReadTexture(Texture, 0, 0, Bytes)) return Floats;
    Floats.resize(Bytes.size() / 2);
    for (size_t Index = 0; Index < Floats.size(); ++Index) Floats[Index] = DecodeHalf(static_cast<uint16_t>(Bytes[Index * 2] | (Bytes[Index * 2 + 1] << 8)));
    return Floats;
}

// 📝 What the environment fed the refresh this scenario: key light, sky view, projected cloud shadows.
struct LightInputs
{
    std::string Source{};
    Vector3     Direction{0.0, 0.0, 1.0};
    Vector3     Illuminance{0.0, 0.0, 0.0};
    Vector3     Solar{0.0, 0.0, 0.0};
    Vector3     Zenith{0.0, 0.0, 0.0};         // [-] sky view, zenith row mean
    double      ShadowedFraction = 0.0;        // [-] cloud shadow texels with τ > 0.05
    double      DeepestShadow    = 0.0;        // [-] largest τ
};

LightInputs MeasureLightInputs(SceneViewport& Viewport)
{
    LightInputs       Result;
    IlluminationHost& Host = *Viewport.AccessHost();
    const LightSolution& Light = Host.LastLight;
    Result.Source      = Light.Source;
    Result.Direction   = Light.Direction;
    Result.Illuminance = Light.Illuminance;
    Result.Solar       = Light.SolarIlluminance;
    const std::vector<float> Sky = ReadHalfTexture(Host.Exchange, Host.Atmosphere->SkyTexture);
    const uint32_t SkyWidth = Host.Atmosphere->SkyTexture->Specification.Width;
    if (Sky.size() >= static_cast<size_t>(SkyWidth) * 4)
    {
        for (uint32_t X = 0; X < SkyWidth; ++X)
            for (int Channel = 0; Channel < 3; ++Channel) Result.Zenith[Channel] += Sky[X * 4 + Channel] / SkyWidth;
    }
    const std::vector<float> Shadow = ReadHalfTexture(Host.Exchange, Host.CloudShadowTexture);
    size_t Shadowed = 0;
    for (size_t Texel = 0; Texel + 3 < Shadow.size(); Texel += 4)
    {
        Result.DeepestShadow = std::max<double>(Result.DeepestShadow, Shadow[Texel]);
        if (Shadow[Texel] > 0.05f) ++Shadowed;
    }
    Result.ShadowedFraction = Shadow.empty() ? 0.0 : static_cast<double>(Shadowed) / (Shadow.size() / 4);
    return Result;
}

// 📝 Mean luminance of linear RGBA floats (non-finite texels skipped).
double MeanLinearLuminance(const std::vector<float>& Rgba)
{
    double Sum = 0.0;
    size_t Count = 0;
    for (size_t Texel = 0; Texel + 3 < Rgba.size(); Texel += 4)
    {
        const double L = 0.2126 * Rgba[Texel] + 0.7152 * Rgba[Texel + 1] + 0.0722 * Rgba[Texel + 2];
        if (!std::isfinite(L)) continue;
        Sum += L;
        ++Count;
    }
    return Count ? Sum / Count : 0.0;
}

double Luminance(const uint8_t* Pixel)
{
    return (0.2126 * Pixel[0] + 0.7152 * Pixel[1] + 0.0722 * Pixel[2]) / 255.0;
}

ImageStatistics Measure(const std::vector<uint8_t>& Rgba, uint32_t Width, uint32_t Height)
{
    ImageStatistics Result;
    size_t SkyCount = 0, GroundCount = 0;
    for (uint32_t Y = 0; Y < Height; ++Y)
    {
        for (uint32_t X = 0; X < Width; ++X)
        {
            const double L = Luminance(&Rgba[(static_cast<size_t>(Y) * Width + X) * 4]);
            Result.Brightest = std::max(Result.Brightest, L);
            Result.MeanLuminance += L / (static_cast<double>(Width) * Height);
            if (Y < Height / 8)
            {
                Result.SkyLuminance += L;
                ++SkyCount;
            }
            if (Y >= Height * 3 / 4)
            {
                Result.GroundLuminance += L;
                ++GroundCount;
            }
            if (Y > 0 && Y < Height / 2 && X > 0 && X + 1 < Width)
            {
                const double Around = (Luminance(&Rgba[(static_cast<size_t>(Y) * Width + X - 1) * 4]) +
                                       Luminance(&Rgba[(static_cast<size_t>(Y) * Width + X + 1) * 4]) +
                                       Luminance(&Rgba[(static_cast<size_t>(Y - 1) * Width + X) * 4])) / 3.0;
                if (L > 0.25 && L > Around + 0.15) ++Result.StarPixels;
            }
        }
    }
    Result.SkyLuminance /= std::max<size_t>(1, SkyCount);
    Result.GroundLuminance /= std::max<size_t>(1, GroundCount);
    return Result;
}

} // namespace

int main(int ArgumentCount, char** Arguments)
{
    if (ArgumentCount < 6)
    {
        std::printf("usage: SceneViewportValidation <GlobalIlluminationShaders> <EnvironmentShaders> <SceneArchive> <CelestialMaps> <OutFolder> "
                    "[Quality] [Width] [Height] [Refreshes]\n");
        return 2;
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SceneViewportConfiguration Configuration;
    Configuration.GlobalIlluminationShaders = Arguments[1];
    Configuration.EnvironmentShaders  = Arguments[2];
    Configuration.SceneArchive        = Arguments[3];
    Configuration.CelestialMaps       = Arguments[4];
    const std::filesystem::path OutFolder = Arguments[5];
    Configuration.Quality             = ArgumentCount > 6 ? Arguments[6] : "low";
    const uint32_t Width     = ArgumentCount > 7 ? static_cast<uint32_t>(std::atoi(Arguments[7])) : 480u;
    const uint32_t Height    = ArgumentCount > 8 ? static_cast<uint32_t>(std::atoi(Arguments[8])) : 270u;
    const uint32_t Refreshes = ArgumentCount > 9 ? static_cast<uint32_t>(std::atoi(Arguments[9])) : 16u;
    // 📝 "demo": the demo's own light inputs, the environment only drawn over the image (Option A off).
    Configuration.EnvironmentLighting = !(ArgumentCount > 10 && std::string(Arguments[10]) == "demo");
    std::filesystem::create_directories(OutFolder);
    Configuration.Journal = [](const std::string& Line) { std::printf("  %s\n", Line.c_str()); };

    SceneViewport Viewport;
    std::string   Diagnostic;
    const auto    BringUp = std::chrono::steady_clock::now();
    if (!Viewport.Construct(nullptr, Configuration, {Width, Height}, &Diagnostic))
    {
        std::printf("bring-up: %s\n", Diagnostic.c_str());
        return 2;
    }
    const double BringUpSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - BringUp).count();
    IlluminationHost& Host = *Viewport.AccessHost();
    std::printf("device: %s\nbring-up: %.1f s, %zu occupants (%zu inactive), %u luminaires, environment %s\n", Viewport.QueryAdapter().c_str(),
                BringUpSeconds, Host.Scene.Occupants.size(),
                static_cast<size_t>(std::count_if(Host.Scene.Occupants.begin(), Host.Scene.Occupants.end(), [](const OccupantEntry& O) { return O.Inactive; })),
                static_cast<uint32_t>(Host.StreetLampPoles.size() + Host.PointLampPosts.size()), Viewport.QueryEnvironmentActive() ? "overlaid" : "absent");
    ReportCheck(Viewport.QueryEnvironmentActive(), "PlanetaryAtmosphere overlaid on the illumination device");
    bool VehicleInactive = Host.Scene.VehicleBody >= 0 && Host.Scene.Occupants[Host.Scene.VehicleBody].Inactive;
    for (int32_t Wheel : Host.Scene.VehicleWheels) VehicleInactive = VehicleInactive && Wheel >= 0 && Host.Scene.Occupants[Wheel].Inactive;
    ReportCheck(VehicleInactive, "vehicle body and wheels deactivated (no car)");
    ReportCheck(Host.StreetLampPoles.size() == 20 && Host.PointLampPosts.size() == 10, "street lamps and lamp posts placed from the scene",
                std::to_string(Host.StreetLampPoles.size()) + " + " + std::to_string(Host.PointLampPosts.size()));

    struct Scenario
    {
        const char*      Name;
        ViewportCamera   Camera;
        SceneEnvironment Environment;
        bool             BracketShafts = false;   // [-] settle, then the fog's sun shafts unshadowed between two shafted refreshes
    };
    const SceneEnvironment Level = ConstructLevelEnvironment();
    std::vector<Scenario> Scenarios;
    ViewportCamera Course;
    Course.Position = {-70.0, -46.0, 13.0};
    Course.Target   = {-5.0, 10.0, 2.0};
    Scenarios.push_back({"Daylight", Course, Level});
    {
        Scenario LowSun{"LowSunFlare", {}, Level};
        // 📝 A line of sight between the lamp posts (no fixture within 4° of the sun's azimuth from here).
        LowSun.Environment.Solar.ElevationDegrees = 8.0f;
        LowSun.Environment.Solar.AzimuthDegrees   = 20.0f;
        LowSun.Environment.Fog.BaseDensity        = 0.004f;
        LowSun.BracketShafts                      = true;
        LowSun.Camera.Position = {-62.0, -48.0, 3.0};
        LowSun.Camera.Target   = {-62.0 + 60.0 * std::cos(28.0 * 3.14159265358979 / 180.0), -48.0 + 60.0 * std::sin(28.0 * 3.14159265358979 / 180.0), 8.0};
        Scenarios.push_back(LowSun);
    }
    {
        Scenario Dusk{"DuskFogLamps", {}, Level};
        Dusk.Environment.Solar.ElevationDegrees = -4.0f;
        Dusk.Environment.Solar.AzimuthDegrees   = 200.0f;
        Dusk.Environment.Fog.BaseDensity        = 0.02f;
        Dusk.Environment.Fog.HeightFalloff      = 0.08f;
        Dusk.Environment.Clouds.Coverage        = 0.6f;
        Dusk.Camera.Position = {-52.0, -14.0, 5.0};
        Dusk.Camera.Target   = {-10.0, 4.0, 4.0};
        Scenarios.push_back(Dusk);
    }
    {
        Scenario Night{"NightMoonsPlanetsStars", {}, Level};
        Night.Environment.Solar.ElevationDegrees  = -28.0f;
        Night.Environment.Solar.AzimuthDegrees    = 250.0f;
        // 📝 Luna and Ember in view with Jupiter and Mars (the demo's sky for 2026-10-20) above the north-west course.
        Night.Environment.Moons.PrimaryOrbitDegrees   = 138.0f;
        Night.Environment.Moons.SecondaryOrbitDegrees = 158.0f;
        Night.Environment.Fog.BaseDensity         = 0.0015f;
        Night.Environment.Clouds.Coverage         = 0.18f;
        Night.Camera.Position = {20.0, -30.0, 2.5};
        Night.Camera.Target   = {20.0 - 40.0 * 0.86, -30.0 + 40.0 * 0.347, 2.5 + 40.0 * 0.375};
        Scenarios.push_back(Night);
    }

    std::vector<ImageStatistics> Statistics;
    std::vector<LightInputs>     Inputs;
    std::array<double, 3>        ShaftBracket{};   // [-] overlaid mean luminance: shafted, unshadowed, shafted
    std::printf("environment lighting: %s\n", Configuration.EnvironmentLighting ? "PlanetaryAtmosphere lights the scene" : "demo inputs");
    for (const Scenario& Case : Scenarios)
    {
        double Slowest = 0.0, Total = 0.0;
        for (uint32_t Refresh = 0; Refresh < Refreshes; ++Refresh)
        {
            SceneEnvironment Environment = Case.Environment;
            Environment.ElapsedSeconds   = Refresh / 30.0;
            const auto Start = std::chrono::steady_clock::now();
            if (!Viewport.Refresh(Case.Camera, Environment, {}, 1.0 / 30.0, {Width, Height}, &Diagnostic))
            {
                ReportCheck(false, std::string(Case.Name) + " refresh " + std::to_string(Refresh), Diagnostic);
                return 1;
            }
            const double Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count();
            Slowest = std::max(Slowest, Seconds);
            Total += Seconds;
        }
        std::vector<uint8_t> Rgba;
        if (!Viewport.ReadPresented(Rgba) || Rgba.size() != static_cast<size_t>(Width) * Height * 4)
        {
            ReportCheck(false, std::string(Case.Name) + " readback");
            return 1;
        }
        const std::string Path = (OutFolder / (std::string(Case.Name) + ".png")).string();
        stbi_write_png(Path.c_str(), static_cast<int>(Width), static_cast<int>(Height), 4, Rgba.data(), static_cast<int>(Width * 4));
        const ImageStatistics Measured = Measure(Rgba, Width, Height);
        Statistics.push_back(Measured);
        const LightInputs Light = MeasureLightInputs(Viewport);
        Inputs.push_back(Light);
        std::printf("  key light %s (%.4f, %.4f, %.4f) from (%.2f, %.2f, %.2f); sun (%.4f, %.4f, %.4f); sky zenith (%.4f, %.4f, %.4f); "
                    "cloud shadow %.1f %% of the map, deepest τ %.2f\n",
                    Light.Source.c_str(), Light.Illuminance[0], Light.Illuminance[1], Light.Illuminance[2], Light.Direction[0], Light.Direction[1],
                    Light.Direction[2], Light.Solar[0], Light.Solar[1], Light.Solar[2], Light.Zenith[0], Light.Zenith[1], Light.Zenith[2],
                    100.0 * Light.ShadowedFraction, Light.DeepestShadow);
        char Line[256];
        std::snprintf(Line, sizeof(Line), "%u refreshes, mean %.2f s, slowest %.2f s; image %.4f, sky %.3f, ground %.3f, brightest %.3f, isolated bright pixels %u",
                      Refreshes, Total / Refreshes, Slowest, Measured.MeanLuminance, Measured.SkyLuminance, Measured.GroundLuminance, Measured.Brightest,
                      Measured.StarPixels);
        std::printf("%s: %s → %s\n", Case.Name, Line, Path.c_str());
        ReportCheck(Measured.Brightest > 0.05, std::string(Case.Name) + " presented a lit image");
        if (Case.BracketShafts)
        {
            // 🔍 Sun shafts A/B on the settled scenario: Fog.ReceiveRayShadow off for one refresh between two shafted ones, all at the
            //    clock of the last refresh. The refresh's own image, history and eye adaptation never see the overlay, so only the
            //    fog differs between the three, and a drift of the history brackets out.
            std::vector<float> Radiance;
            EnvironmentOverlay* Overlay = Viewport.AccessOverlay();
            ShaftBracket[0] = Overlay != nullptr && Overlay->ReadOverlaidRadiance(Radiance) ? MeanLinearLuminance(Radiance) : 0.0;
            for (uint32_t Step = 1; Step < 3; ++Step)
            {
                SceneEnvironment Environment     = Case.Environment;
                Environment.ElapsedSeconds       = (std::max(Refreshes, 1u) - 1) / 30.0;
                Environment.Fog.ReceiveRayShadow = Step != 1;
                if (!Viewport.Refresh(Case.Camera, Environment, {}, 1.0 / 30.0, {Width, Height}, &Diagnostic))
                {
                    ReportCheck(false, std::string(Case.Name) + " shaft bracket refresh " + std::to_string(Step), Diagnostic);
                    return 1;
                }
                ShaftBracket[Step] = Overlay != nullptr && Overlay->ReadOverlaidRadiance(Radiance) ? MeanLinearLuminance(Radiance) : 0.0;
                if (Step == 1 && Viewport.ReadPresented(Rgba) && Rgba.size() == static_cast<size_t>(Width) * Height * 4)
                {
                    const std::string Unshadowed = (OutFolder / (std::string(Case.Name) + "UnshadowedFog.png")).string();
                    stbi_write_png(Unshadowed.c_str(), static_cast<int>(Width), static_cast<int>(Height), 4, Rgba.data(), static_cast<int>(Width * 4));
                }
            }
            std::printf("  fog sun shafts: overlaid mean luminance %.5f shafted, %.5f unshadowed, %.5f shafted (%.2f %% of the unshadowed)\n",
                        ShaftBracket[0], ShaftBracket[1], ShaftBracket[2],
                        100.0 * std::max(ShaftBracket[0], ShaftBracket[2]) / std::max(ShaftBracket[1], 1.0e-12));
        }
    }
    // 🔍 what the images must show relative to each other
    ReportCheck(Statistics[0].SkyLuminance > 0.35, "daylight sky is bright", std::to_string(Statistics[0].SkyLuminance));
    ReportCheck(Statistics[1].Brightest > 0.97, "low sun saturates the disc and flare", std::to_string(Statistics[1].Brightest));
    ReportCheck(Statistics[3].SkyLuminance < Statistics[0].SkyLuminance * 0.5, "night sky darker than day",
                std::to_string(Statistics[3].SkyLuminance) + " vs " + std::to_string(Statistics[0].SkyLuminance));
    ReportCheck(Statistics[3].StarPixels > 8, "night sky shows stars", std::to_string(Statistics[3].StarPixels) + " isolated bright pixels");
    ReportCheck(ShaftBracket[1] > std::max(ShaftBracket[0], ShaftBracket[2]) * 1.001, "fog sun shafts shadowed by the scene's sun cascades",
                std::to_string(ShaftBracket[0]) + " / " + std::to_string(ShaftBracket[1]) + " unshadowed / " + std::to_string(ShaftBracket[2]));
    if (Configuration.EnvironmentLighting && Inputs.size() == 4)
    {
        // 🔍 Option A: what the environment must have fed the refresh
        const LightInputs &Day = Inputs[0], &Low = Inputs[1], &Night = Inputs[3];
        ReportCheck(Day.Zenith[2] > Day.Zenith[0] && Day.Zenith[2] > 0.05, "daylight sky view is the environment's blue sky",
                    std::to_string(Day.Zenith[0]) + ", " + std::to_string(Day.Zenith[2]));
        ReportCheck(Day.ShadowedFraction > 0.01 && Day.DeepestShadow > 0.2, "clouds shade the ground (projected cloud shadow map)",
                    std::to_string(100.0 * Day.ShadowedFraction) + " %");
        const double DayRatio = Day.Solar[0] / std::max(1.0e-9, Day.Solar[2]), LowRatio = Low.Solar[0] / std::max(1.0e-9, Low.Solar[2]);
        ReportCheck(LowRatio > DayRatio * 1.15, "low sun reddened by the environment's air", std::to_string(DayRatio) + " → " + std::to_string(LowRatio));
        ReportCheck(Night.Source == "Moon" && Night.Illuminance[2] > 1.0e-4 && Night.Direction[2] > 0.0, "Luna and Ember light the night",
                    Night.Source + " " + std::to_string(Night.Illuminance[0] + Night.Illuminance[1] + Night.Illuminance[2]));
        ReportCheck(Night.Zenith[2] < Day.Zenith[2] * 0.05, "night sky view far darker than day",
                    std::to_string(Night.Zenith[2]) + " vs " + std::to_string(Day.Zenith[2]));
    }
    const std::vector<std::string>& Faults = Host.Exchange.Faults();
    ReportCheck(Faults.empty(), "no rejected transfers or failed submissions", Faults.empty() ? "" : Faults.front());
    std::printf("\n=== scene viewport: %d failing checks\n", Failures);
    return Failures == 0 ? 0 : 1;
}
