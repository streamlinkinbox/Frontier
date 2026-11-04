/*==========================================================================================================================================
                                                      STARSCINTILLATIONCAPTURE.CPP
==========================================================================================================================================*/
// 🧩 How much the night sky's stars twinkle on screen, through the editor's own scene viewport (antialiasing included): the
//    temporal variation of every star's brightness under the Stars record's scintillation controls.

// 📝 Usage: StarScintillationCapture <GlobalIllumination shaders> <Environment shaders> <ProvingGround> <CelestialMaps> <out>
//    <variant[,variant…]> [width] [height] [warm refreshes] [measured refreshes] [refresh rate Hz]. Moons, planets, clouds and
//    fog are off and the camera looks up, so nothing but the stars changes from one refresh to the next. Stars are the isolated
//    peaks of the first variant's mean image (list Still first); each star's brightness per refresh is the brightest linear
//    luminance within a pixel of its peak, so the antialiasing's sub-pixel jitter does not count as twinkle.

#include "Frontier/Engine/Rendering/SceneViewport/SceneViewport.h"

#include "stb_image_write.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

using namespace Frontier;
using namespace Frontier::GlobalIllumination;

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       VARIANTS
//------------------------------------------------------------------------------------------------------------------------

struct VariantSpecification
{
    const char* Name;
    bool        ScintillationEnabled;
    float       TwinkleDepth;   // [0-1] StarConfiguration::AtmosphericScintillate
    float       TwinkleRate;    // [Hz]  StarConfiguration::ScintillationRateHz
};

const VariantSpecification Variants[] = {
    {"Still", false, 0.35f, 1.0f},     // scintillation off: the jitter and noise floor
    {"Default", true, 0.35f, 1.0f},    // the Stars record's defaults
    {"Shallow", true, 0.10f, 1.0f},
    {"Deep", true, 1.00f, 1.0f},
    {"Fast", true, 0.35f, 3.0f},
};

// 📝 The level's night (SceneViewportValidation's NightMoonsPlanetsStars) with everything but the stars switched off.
SceneEnvironment ConstructStarlitEnvironment()
{
    SceneEnvironment S;
    S.Solar.ElevationDegrees      = -28.0f;
    S.Solar.AzimuthDegrees        = 250.0f;
    S.Atmosphere.MieTurbidity     = 2.3f;
    S.Stars.HipparcosCount        = 5000;
    S.Stars.MagnitudeGain         = 1.15f;
    S.Moons.Enabled               = false;
    S.Clouds.Enabled              = false;
    S.Clouds.Coverage             = 0.0f;
    S.Fog.Enabled                 = false;
    S.Fog.BaseDensity             = 0.0f;
    for (PlanetConfiguration& Planet : S.Planets) Planet.Enabled = false;
    return S;
}

double LinearLuminance(const uint8_t* Texel)
{
    const auto Linear = [](uint8_t Encoded) {
        const double Unit = Encoded / 255.0;
        return Unit <= 0.04045 ? Unit / 12.92 : std::pow((Unit + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * Linear(Texel[0]) + 0.7152 * Linear(Texel[1]) + 0.0722 * Linear(Texel[2]);
}

struct StarSite
{
    int    X = 0, Y = 0;
    double Peak = 0.0;   // [-] linear luminance in the mean image
};

// 📝 Isolated peaks of the sky: brighter than every pixel within two, well above the sky around them, above the horizon band.
std::vector<StarSite> IdentifyStars(const std::vector<double>& Mean, int Width, int Height)
{
    std::vector<StarSite> Sites;
    for (int Y = 3; Y < Height * 2 / 3; ++Y)
        for (int X = 3; X < Width - 3; ++X)
        {
            const double Centre = Mean[static_cast<size_t>(Y) * Width + X];
            bool   Highest = true;
            double Ring    = 0.0;
            int    RingCount = 0;
            for (int Dy = -2; Dy <= 2 && Highest; ++Dy)
                for (int Dx = -2; Dx <= 2; ++Dx)
                {
                    if (Dx == 0 && Dy == 0) continue;
                    const double Neighbour = Mean[static_cast<size_t>(Y + Dy) * Width + X + Dx];
                    if (Neighbour >= Centre) { Highest = false; break; }
                    if (std::abs(Dx) == 2 || std::abs(Dy) == 2) { Ring += Neighbour; ++RingCount; }
                }
            if (!Highest || RingCount == 0) continue;
            const double Background = Ring / RingCount;
            if (Centre > 0.004 && Centre > Background * 2.5 + 0.002) Sites.push_back({X, Y, Centre});
        }
    return Sites;
}

double Percentile(std::vector<double> Samples, double Share)
{
    if (Samples.empty()) return 0.0;
    std::sort(Samples.begin(), Samples.end());
    return Samples[std::min(Samples.size() - 1, static_cast<size_t>(Share * (Samples.size() - 1) + 0.5))];
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         MAIN
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char** Arguments)
{
    if (ArgumentCount < 7)
    {
        std::printf("usage: StarScintillationCapture <GlobalIllumination shaders> <Environment shaders> <ProvingGround> <CelestialMaps> <out> "
                    "<variant[,variant...]> [width] [height] [warm] [measured] [refresh Hz]\n");
        return 2;
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SceneViewportConfiguration Configuration;
    Configuration.GlobalIlluminationShaders = Arguments[1];
    Configuration.EnvironmentShaders        = Arguments[2];
    Configuration.SceneArchive              = Arguments[3];
    Configuration.CelestialMaps             = Arguments[4];
    Configuration.Quality                   = "low";
    const std::filesystem::path OutFolder   = Arguments[5];
    std::vector<std::string>    VariantNames;
    for (std::stringstream Stream(Arguments[6]); Stream.good();)
    {
        std::string Name;
        std::getline(Stream, Name, ',');
        if (!Name.empty()) VariantNames.push_back(Name);
    }
    const uint32_t Width    = ArgumentCount > 7 ? static_cast<uint32_t>(std::atoi(Arguments[7])) : 480u;
    const uint32_t Height   = ArgumentCount > 8 ? static_cast<uint32_t>(std::atoi(Arguments[8])) : 270u;
    const uint32_t Warm     = ArgumentCount > 9 ? static_cast<uint32_t>(std::atoi(Arguments[9])) : 8u;
    const uint32_t Measured = ArgumentCount > 10 ? static_cast<uint32_t>(std::atoi(Arguments[10])) : 40u;
    const double   RefreshHz = ArgumentCount > 11 ? std::atof(Arguments[11]) : 30.0;
    Configuration.Journal = [](const std::string& Line) { std::printf("  %s\n", Line.c_str()); };
    std::filesystem::create_directories(OutFolder);

    SceneViewport Viewport;
    std::string   Diagnostic;
    if (!Viewport.Construct(nullptr, Configuration, {Width, Height}, &Diagnostic))
    {
        std::printf("bring-up: %s\n", Diagnostic.c_str());
        return 2;
    }
    std::printf("device: %s, %u × %u, %u warm + %u measured refreshes at %.0f Hz\n", Viewport.QueryAdapter().c_str(), Width, Height, Warm,
                Measured, RefreshHz);

    // 📝 From the course's south-east, 50° up into a sky with no moon, planet or cloud.
    ViewportCamera Camera;
    Camera.Position = {20.0, -30.0, 2.5};
    Camera.Target   = {20.0 - 25.0, -30.0 + 10.0, 2.5 + 32.0};
    const SceneEnvironment Night = ConstructStarlitEnvironment();
    const size_t PixelCount = static_cast<size_t>(Width) * Height;
    std::vector<StarSite> Sites;
    double Clock = 0.0;
    for (const std::string& VariantName : VariantNames)
    {
        const auto Found = std::find_if(std::begin(Variants), std::end(Variants),
                                        [&](const VariantSpecification& Candidate) { return VariantName == Candidate.Name; });
        if (Found == std::end(Variants))
        {
            std::printf("unknown variant %s\n", VariantName.c_str());
            return 2;
        }
        SceneEnvironment Environment             = Night;
        Environment.Stars.ScintillationEnabled   = Found->ScintillationEnabled;
        Environment.Stars.AtmosphericScintillate = Found->TwinkleDepth;
        Environment.Stars.ScintillationRateHz    = Found->TwinkleRate;
        std::vector<std::vector<double>> Luminance;
        std::vector<double>              Mean(PixelCount, 0.0);
        const auto Start = std::chrono::steady_clock::now();
        for (uint32_t Refresh = 0; Refresh < Warm + Measured; ++Refresh)
        {
            Clock += 1.0 / RefreshHz;
            Environment.ElapsedSeconds = Clock;
            if (!Viewport.Refresh(Camera, Environment, {}, 1.0 / RefreshHz, {Width, Height}, &Diagnostic))
            {
                std::printf("[FAIL] %s refresh %u: %s\n", Found->Name, Refresh, Diagnostic.c_str());
                return 1;
            }
            if (Refresh < Warm) continue;
            std::vector<uint8_t> Rgba;
            if (!Viewport.ReadPresented(Rgba) || Rgba.size() != PixelCount * 4)
            {
                std::printf("[FAIL] %s readback\n", Found->Name);
                return 1;
            }
            std::vector<double> Linear(PixelCount);
            for (size_t Texel = 0; Texel < PixelCount; ++Texel)
            {
                Linear[Texel] = LinearLuminance(&Rgba[Texel * 4]);
                Mean[Texel] += Linear[Texel] / Measured;
            }
            Luminance.push_back(std::move(Linear));
            if (Refresh == Warm)
            {
                const std::string Path = (OutFolder / (std::string(Found->Name) + "-First.png")).string();
                stbi_write_png(Path.c_str(), static_cast<int>(Width), static_cast<int>(Height), 4, Rgba.data(), static_cast<int>(Width * 4));
            }
        }
        if (Sites.empty()) Sites = IdentifyStars(Mean, static_cast<int>(Width), static_cast<int>(Height));
        // ① each star's brightness per refresh: the brightest pixel within one of its peak
        std::vector<double> Variation, Swing, Brightness;
        for (const StarSite& Site : Sites)
        {
            std::vector<double> Series;
            for (const std::vector<double>& Image : Luminance)
            {
                double Brightest = 0.0;
                for (int Dy = -1; Dy <= 1; ++Dy)
                    for (int Dx = -1; Dx <= 1; ++Dx)
                        Brightest = std::max(Brightest, Image[static_cast<size_t>(Site.Y + Dy) * Width + Site.X + Dx]);
                Series.push_back(Brightest);
            }
            double Sum = 0.0, Squares = 0.0;
            for (double Sample : Series) { Sum += Sample; Squares += Sample * Sample; }
            const double Average = Sum / Series.size();
            const double Spread  = std::sqrt(std::max(0.0, Squares / Series.size() - Average * Average));
            const auto [Lowest, Highest] = std::minmax_element(Series.begin(), Series.end());
            Variation.push_back(Average > 0.0 ? Spread / Average : 0.0);
            Swing.push_back(Average > 0.0 ? (*Highest - *Lowest) / Average : 0.0);
            Brightness.push_back(Average);
        }
        const double Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count();
        double MeanBrightness = 0.0;
        for (double Sample : Brightness) MeanBrightness += Sample / std::max<size_t>(1, Brightness.size());
        std::printf("%-8s depth %.2f rate %.1f Hz %s: %zu stars, brightness variation median %.1f %% (p90 %.1f %%), peak-to-peak median %.1f %%, "
                    "mean star luminance %.4f, %.0f s\n",
                    Found->Name, Found->TwinkleDepth, Found->TwinkleRate, Found->ScintillationEnabled ? "on " : "off", Sites.size(),
                    100.0 * Percentile(Variation, 0.5), 100.0 * Percentile(Variation, 0.9), 100.0 * Percentile(Swing, 0.5), MeanBrightness, Seconds);
        // ② the brightest star's own series, for the report's trace
        if (!Sites.empty())
        {
            const auto Brightest = std::max_element(Sites.begin(), Sites.end(), [](const StarSite& A, const StarSite& B) { return A.Peak < B.Peak; });
            std::printf("  brightest star at (%d, %d):", Brightest->X, Brightest->Y);
            for (const std::vector<double>& Image : Luminance)
            {
                double Peak = 0.0;
                for (int Dy = -1; Dy <= 1; ++Dy)
                    for (int Dx = -1; Dx <= 1; ++Dx)
                        Peak = std::max(Peak, Image[static_cast<size_t>(Brightest->Y + Dy) * Width + Brightest->X + Dx]);
                std::printf(" %.4f", Peak);
            }
            std::printf("\n");
        }
    }
    return Sites.empty() ? 1 : 0;
}
