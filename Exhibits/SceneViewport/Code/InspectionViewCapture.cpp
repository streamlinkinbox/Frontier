/*==========================================================================================================================================
                                                       INSPECTIONVIEWCAPTURE.CPP
==========================================================================================================================================*/
// 🧩 The renderer's fifteen inspection views of one settled daylight view of the level, offscreen through the editor's own scene
//    viewport: the debug-view thumbnails of the Render settings inspector (Experimental/RenderSettingsInspector).

// 📝 Usage: InspectionViewCapture <GlobalIllumination shaders> <Environment shaders> <ProvingGround> <CelestialMaps> <out>
//    [quality] [width] [height] [warm refreshes] [virtualized 0|1] [first view] [last view]. Time is frozen, so only the
//    estimators settle during the warm refreshes (the first view, antialiased when it is view 0). Each further view is one refresh
//    with the antialiasing off: its history would otherwise carry the previous view. With virtualized geometry on, the cluster
//    views (10–14) show the nesting takeover's clusters instead of the demo's orange "no virtualized geometry" checker; those
//    captures are written as Inspection-NN-Virtualized.jpg.

#include "Frontier/Engine/Rendering/SceneViewport/SceneViewport.h"

#include "stb_image_write.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

using namespace Frontier;
using namespace Frontier::GlobalIllumination;

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    LEVEL AND VIEWS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The values Projects/Everest/Content/Levels/Default.levelspace overrides (as SceneViewportValidation's level environment).
SceneEnvironment ConstructLevelEnvironment()
{
    SceneEnvironment S;
    S.Solar.ElevationDegrees          = 38.0f;
    S.Solar.AzimuthDegrees            = 132.0f;
    S.Solar.IlluminanceKilolux        = 98.0f;
    S.Solar.KelvinTemperature         = 5778.0f;
    S.Flare.Intensity                 = 0.62f;
    S.Flare.ApertureBladeCount        = 7;
    S.Atmosphere.RayleighScattering   = 1.0f;
    S.Atmosphere.MieTurbidity         = 2.3f;
    S.Atmosphere.OzoneAbsorptionScale = 1.0f;
    S.Clouds.Coverage                 = 0.42f;
    S.Clouds.BaseAltitudeMetres       = 1750.0f;
    S.Clouds.LayerThicknessMetres     = 1350.0f;
    S.Stars.HipparcosCount            = 5000;
    S.Stars.MagnitudeGain             = 1.15f;
    S.Moons.PrimaryPhase              = 0.68f;
    S.Moons.SecondaryMoonEnabled      = true;
    S.Fog.BaseDensity                 = 0.012f;
    S.Fog.HeightFalloff               = 0.14f;
    S.Wind.DirectionDegrees           = 45.0f;
    S.Wind.SpeedMetresPerSecond       = 6.5f;
    S.Wind.CurlNoiseTurbulence        = 0.45f;
    return S;
}

// 📝 The demo's InspectionModes (InspectorPages.js), index = RefreshToggles::Inspection.
const char* const InspectionNames[15] = {
    "Final image",           "Direct sun only",           "Indirect diffuse (GI)",     "Indirect irradiance (denoised)",
    "ReSTIR irradiance (raw)", "Glossy reflections",      "BVH / distance field",      "Surface cards (texel space)",
    "Normals",               "Albedo",                    "Clusters: visibility IDs",  "Clusters: deferred resolve",
    "Clusters: Hi-Z pyramid", "Clusters: glass layers",   "Clusters: LOD levels"};

double MeanLuminance(const std::vector<uint8_t>& Rgba)
{
    double Sum = 0.0;
    for (size_t Texel = 0; Texel + 3 < Rgba.size(); Texel += 4)
        Sum += (0.2126 * Rgba[Texel] + 0.7152 * Rgba[Texel + 1] + 0.0722 * Rgba[Texel + 2]) / 255.0;
    return Rgba.empty() ? 0.0 : Sum / (Rgba.size() / 4);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         MAIN
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char** Arguments)
{
    if (ArgumentCount < 6)
    {
        std::printf("usage: InspectionViewCapture <GlobalIllumination shaders> <Environment shaders> <ProvingGround> <CelestialMaps> <out> "
                    "[quality] [width] [height] [warm refreshes] [virtualized 0|1] [first view] [last view] [indirect 0|1] [ray tracing 0|1]\n");
        return 2;
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SceneViewportConfiguration Configuration;
    Configuration.GlobalIlluminationShaders = Arguments[1];
    Configuration.EnvironmentShaders        = Arguments[2];
    Configuration.SceneArchive              = Arguments[3];
    Configuration.CelestialMaps             = Arguments[4];
    const std::filesystem::path OutFolder   = Arguments[5];
    Configuration.Quality                   = ArgumentCount > 6 ? Arguments[6] : "high";
    const uint32_t Width                    = ArgumentCount > 7 ? static_cast<uint32_t>(std::atoi(Arguments[7])) : 960u;
    const uint32_t Height                   = ArgumentCount > 8 ? static_cast<uint32_t>(std::atoi(Arguments[8])) : 540u;
    const uint32_t Warm                     = ArgumentCount > 9 ? static_cast<uint32_t>(std::atoi(Arguments[9])) : 12u;
    const bool     Virtualized              = ArgumentCount > 10 && std::atoi(Arguments[10]) != 0;
    const uint32_t FirstView                = ArgumentCount > 11 ? std::min(14u, static_cast<uint32_t>(std::atoi(Arguments[11]))) : 0u;
    const uint32_t LastView                 = ArgumentCount > 12 ? std::min(14u, static_cast<uint32_t>(std::atoi(Arguments[12]))) : 14u;
    const bool     Indirect                 = ArgumentCount <= 13 || std::atoi(Arguments[13]) != 0;
    Configuration.RayTracing                = ArgumentCount <= 14 || std::atoi(Arguments[14]) != 0;
    Configuration.Journal = [](const std::string& Line) { std::printf("  %s\n", Line.c_str()); };
    std::filesystem::create_directories(OutFolder);

    SceneViewport Viewport;
    std::string   Diagnostic;
    if (!Viewport.Construct(nullptr, Configuration, {Width, Height}, &Diagnostic))
    {
        std::printf("bring-up: %s\n", Diagnostic.c_str());
        return 2;
    }
    std::printf("device: %s, %u × %u, quality %s, %u warm refreshes, virtualized geometry %s, indirect %s, %s, views %u–%u\n",
                Viewport.QueryAdapter().c_str(), Width, Height, Configuration.Quality.c_str(), Warm, Virtualized ? "on" : "off", Indirect ? "on" : "off",
                Configuration.RayTracing ? "RTX" : "GTX", FirstView, LastView);

    // 📝 SceneViewportValidation's "Daylight" camera: the course from its south-west corner.
    ViewportCamera Course;
    Course.Position = {-70.0, -46.0, 13.0};
    Course.Target   = {-5.0, 10.0, 2.0};
    const SceneEnvironment Level = ConstructLevelEnvironment();
    int Failures = 0;
    for (uint32_t View = FirstView; View <= LastView; ++View)
    {
        ViewportRendering Rendering;
        Rendering.Inspection           = View;
        Rendering.TemporalAntiAliasing = View == 0;
        Rendering.VirtualizedGeometry  = Virtualized;
        Rendering.GlobalIllumination   = Indirect;
        const uint32_t Refreshes       = View == FirstView ? std::max(Warm, 1u) : 1u;
        const auto     Start           = std::chrono::steady_clock::now();
        for (uint32_t Refresh = 0; Refresh < Refreshes; ++Refresh)
        {
            if (!Viewport.Refresh(Course, Level, Rendering, 1.0 / 30.0, {Width, Height}, &Diagnostic))
            {
                std::printf("[FAIL] view %u refresh %u: %s\n", View, Refresh, Diagnostic.c_str());
                return 1;
            }
        }
        const double         Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count();
        std::vector<uint8_t> Rgba;
        if (!Viewport.ReadPresented(Rgba) || Rgba.size() != static_cast<size_t>(Width) * Height * 4)
        {
            std::printf("[FAIL] view %u readback\n", View);
            return 1;
        }
        // 📝 Opaque for the JPEG writer (the presented alpha carries nothing a thumbnail needs).
        std::vector<uint8_t> Rgb(static_cast<size_t>(Width) * Height * 3);
        for (size_t Texel = 0; Texel < static_cast<size_t>(Width) * Height; ++Texel)
            for (size_t Channel = 0; Channel < 3; ++Channel) Rgb[Texel * 3 + Channel] = Rgba[Texel * 4 + Channel];
        char Name[80];
        std::snprintf(Name, sizeof(Name), "Inspection-%02u%s%s%s.jpg", View, Virtualized ? "-Virtualized" : "", Indirect ? "" : "-IndirectOff",
                      Configuration.RayTracing ? "" : "-Gtx");
        const std::string Path    = (OutFolder / Name).string();
        const bool        Written = stbi_write_jpg(Path.c_str(), static_cast<int>(Width), static_cast<int>(Height), 3, Rgb.data(), 90) != 0;
        const double      Mean    = MeanLuminance(Rgba);
        // 🔍 A view that wrote nothing (all black or all one level) is a failed view, not a thumbnail.
        const bool Lit = Mean > 0.01 && Mean < 0.99;
        if (!Written || !Lit) ++Failures;
        std::printf("%s view %2u %-32s %u refresh%s, %.1f s, mean %.3f → %s\n", Written && Lit ? "[PASS]" : "[FAIL]", View,
                    InspectionNames[View], Refreshes, Refreshes == 1 ? "" : "es", Seconds, Mean, Path.c_str());
    }
    std::printf("%d failing\n", Failures);
    return Failures == 0 ? 0 : 1;
}
