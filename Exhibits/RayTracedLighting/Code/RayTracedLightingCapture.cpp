/*==========================================================================================================================================
                                                     RAYTRACEDLIGHTINGCAPTURE.CPP
==========================================================================================================================================*/
// 🧩 Ray-traced reflections and sun shadows of the RTX path in the editor's own scene viewport, offscreen: one camera view under
//    a set of variants (the demo, its options, the refinements), each measured by fireflies, temporal noise, distance from a
//    converged reference, refresh time and the cost of every refresh stage.

// 📝 Usage: RayTracedLightingCapture <GlobalIllumination shaders> <Environment shaders> <ProvingGround> <CelestialMaps> <out>
//    <view[,view…]> <variant[,variant…]> [warm refreshes] [measured refreshes] [indirect 0|1] [width] [height]. Time is frozen so
//    nothing but the estimators changes between refreshes; each variant starts from a cleared history. Measure whole multiples of
//    the antialiasing's 16-refresh jitter cycle so every mean covers the same sub-pixel offsets. A …Reference variant, when listed,
//    runs first (converged: 64 sun rays per pixel, or 16 reflection samples per screen pixel); every later refresh is then compared
//    with its mean. Views and variants: the tables below.
#include "Frontier/Engine/Rendering/SceneViewport/SceneViewport.h"
#include "stb_image_write.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <sstream>
#include <string>
#include <vector>
using namespace Frontier;
using namespace Frontier::GlobalIllumination;

namespace
{
struct ViewSpecification
{
    const char*        Name;
    Vector3            Position, Target;
    std::array<int, 4> Region;   // [px] x0 y0 x1 y1 of the measured region at 480×270, scaled to the extent
};
// 📝 The proving ground's glossy surfaces: the glass and tempered panes (roughness 0.04 / 0.06), the cones (0.22), the control
//    tower's cabin glass (0.04); shadows: a light pole's head and mast (11 m, penumbra 0 → ~17 cm) and the tower's.
const ViewSpecification Views[] = {
    {"PaneNear", {3.0, -23.6, 1.5}, {6.0, -27.2, 1.0}, {150, 74, 278, 212}},
    {"ConesNear", {-31.07, 16.39, 0.8}, {-30.0, 15.2, 0.3}, {196, 68, 284, 196}},
    {"PenumbraNear", {-3.0, -4.5, 2.5}, {-5.8, -1.0, 0.0}, {0, 0, 480, 270}},
    {"TowerShadow", {-37.0, -13.0, 4.0}, {-46.0, -20.0, 0.0}, {0, 100, 480, 270}},
    // 📝 The pane mirrors the sun: the view ray (0.527, 0.586, 0.616) reflects off its −x face toward the sun at 38° / 132°.
    {"PaneSunGlint", {4.72, -27.93, 0.3}, {6.0, -26.5, 1.8}, {150, 60, 330, 210}},
};

struct VariantSpecification
{
    const char* Name;
    uint32_t    Refinements;
    bool        RayTracedShadows;
    bool        VarianceFilter;
    uint32_t    Inspection = 0;   // [-] the demo's inspection view: 1 direct light only, 5 specular reflection only
};
namespace R = RefinementBit;
const VariantSpecification Variants[] = {
    {"Demo", 0u, true, true},                       // the 1:1 port
    {"Unfiltered", 0u, true, false},                // the demo without its variance filters ("without the blur")
    {"Cascades", 0u, false, true},                  // the demo's raster cascades instead of ray-traced sun shadows (as GTX)
    {"SolarDisc", R::SolarDisc, true, true},
    {"StratifiedSun", R::SolarDisc | R::StratifiedSun, true, true},
    {"PenumbraFilter", R::SolarDisc | R::StratifiedSun | R::PenumbraFilter, true, true},
    {"MirrorFilter", R::SolarDisc | R::MirrorFilter, true, true},
    {"MirrorTrace", R::SolarDisc | R::MirrorFilter | R::MirrorTrace, true, true},
    {"Refined", R::SolarDisc | R::StratifiedSun | R::PenumbraFilter | R::MirrorFilter | R::MirrorTrace, true, true},
    // 📝 Converged references, each changing only what its views test: 64 sun rays per pixel (shadow views); every reflective pixel
    //    traced 16 times at screen resolution and taken unfiltered (reflection views).
    {"ShadowReference", R::SolarDisc | R::ShadowReference, true, true},
    {"MirrorReference", R::SolarDisc | R::MirrorFilter | R::MirrorTrace | R::MirrorReference, true, true},
    // 📝 Where fireflies come from: the direct term alone and the specular reflection alone, before and after the solar disc.
    {"DemoDirect", 0u, true, true, 1u},
    {"DemoSpecular", 0u, true, true, 5u},
    {"SolarDiscDirect", R::SolarDisc, true, true, 1u},
    {"SolarDiscSpecular", R::SolarDisc, true, true, 5u},
    // 📝 Where the grain without the variance filters comes from: the diffuse term and the specular term, filters on and off.
    {"DemoDiffuse", 0u, true, true, 2u},
    {"UnfilteredDiffuse", 0u, true, false, 2u},
    {"UnfilteredSpecular", 0u, true, false, 5u},
    {"MirrorFilterSpecular", R::SolarDisc | R::MirrorFilter, true, true, 5u},
};

double DisplayLuminance(const uint8_t* Texel)
{
    return (0.2126 * Texel[0] + 0.7152 * Texel[1] + 0.0722 * Texel[2]) / 255.0;
}

// 📝 A firefly: a pixel brighter than every one of its 8 neighbours by over 0.12 of full scale and over 1.6× their mean.
uint32_t CountFireflies(const std::vector<uint8_t>& Rgba, uint32_t Width, uint32_t Height, const std::array<int, 4>& Region)
{
    uint32_t Count = 0;
    for (int Y = std::max(1, Region[1]); Y < std::min<int>(Height - 1, Region[3]); ++Y)
        for (int X = std::max(1, Region[0]); X < std::min<int>(Width - 1, Region[2]); ++X)
        {
            const double Centre = DisplayLuminance(&Rgba[(static_cast<size_t>(Y) * Width + X) * 4]);
            double Brightest = 0.0, Mean = 0.0;
            for (int DY = -1; DY <= 1; ++DY)
                for (int DX = -1; DX <= 1; ++DX)
                {
                    if (DX == 0 && DY == 0) continue;
                    const double Neighbour = DisplayLuminance(&Rgba[(static_cast<size_t>(Y + DY) * Width + X + DX) * 4]);
                    Brightest = std::max(Brightest, Neighbour);
                    Mean += Neighbour / 8.0;
                }
            if (Centre - Brightest > 0.12 && Centre > 1.6 * Mean) ++Count;
        }
    return Count;
}

struct Measurement
{
    std::string         Variant{};
    double              Fireflies = 0.0;        // [-] per refresh, measured region
    double              VisibleErrors = 0.0;    // [-] per refresh: region pixels over 12 levels of luminance from the reference mean
    double              FrameRms = 0.0;         // [levels] luminance RMS of a single refresh from the reference mean, region
    double              TransitionFrameRms = 0.0;   // [levels] the same over the transition pixels
    double              TransitionNoise = 0.0;  // [levels] temporal standard deviation, transition mean
    double              TransitionBias = 0.0;   // [levels] RMS of the variant's mean from the reference mean, transitions
    double              RefreshSeconds = 0.0;   // [s] mean
    std::map<std::string, double> Stages{};     // [ms] mean per refresh
    std::vector<double> Mean{};                 // [levels] luminance per pixel
    std::vector<double> Deviation{};            // [levels] temporal standard deviation of luminance per pixel
};
} // namespace

int main(int ArgumentCount, char** Arguments)
{
    if (ArgumentCount < 8)
    {
        std::printf("usage: RayTracedLightingCapture <GlobalIllumination shaders> <Environment shaders> <ProvingGround> <CelestialMaps> <out> "
                    "<view> <variant[,variant...]> [warm] [measured] [indirect 0|1] [width] [height]\n");
        return 2;
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    // 📝 Every stage a submission of its own, so the submission timing names each stage's cost.
    setenv("FRONTIER_ILLUMINATION_STAGED_SUBMISSIONS", "1", 1);
    SceneViewportConfiguration Configuration;
    Configuration.GlobalIlluminationShaders = Arguments[1];
    Configuration.EnvironmentShaders        = Arguments[2];
    Configuration.SceneArchive              = Arguments[3];
    Configuration.CelestialMaps             = Arguments[4];
    Configuration.Quality                   = "low";
    const std::filesystem::path OutFolder   = Arguments[5];
    auto SplitList = [](const char* List) {
        std::vector<std::string> Names;
        for (std::stringstream Stream(List); Stream.good();)
        {
            std::string Name;
            std::getline(Stream, Name, ',');
            if (!Name.empty()) Names.push_back(Name);
        }
        return Names;
    };
    const std::vector<std::string> ViewNames    = SplitList(Arguments[6]);
    const std::vector<std::string> VariantNames = SplitList(Arguments[7]);
    const uint32_t Warm     = ArgumentCount > 8 ? static_cast<uint32_t>(std::atoi(Arguments[8])) : 12u;
    const uint32_t Measured = ArgumentCount > 9 ? static_cast<uint32_t>(std::atoi(Arguments[9])) : 12u;
    const bool     Indirect = ArgumentCount > 10 ? std::atoi(Arguments[10]) != 0 : true;
    const uint32_t Width    = ArgumentCount > 11 ? static_cast<uint32_t>(std::atoi(Arguments[11])) : 480u;
    const uint32_t Height   = ArgumentCount > 12 ? static_cast<uint32_t>(std::atoi(Arguments[12])) : 270u;
    std::filesystem::create_directories(OutFolder);
    Configuration.Journal = [](const std::string& Line) { std::printf("  %s\n", Line.c_str()); };

    SceneViewport Viewport;
    std::string   Diagnostic;
    if (!Viewport.Construct(nullptr, Configuration, {Width, Height}, &Diagnostic))
    {
        std::printf("bring-up: %s\n", Diagnostic.c_str());
        return 2;
    }
    IlluminationHost& Host = *Viewport.AccessHost();
    std::printf("device: %s; %u warm + %u measured refreshes; indirect %s; %ux%u\n", Viewport.QueryAdapter().c_str(), Warm, Measured,
                Indirect ? "on" : "off", Width, Height);
    std::map<std::string, double> StageTotals;
    bool Measuring = false;
    Host.Exchange.SubmissionTiming = [&](const std::string& Label, double Milliseconds) {
        if (!Measuring) return;
        const size_t Colon = Label.find(": ");
        StageTotals[Colon == std::string::npos ? Label : Label.substr(Colon + 2)] += Milliseconds;
    };

    SceneEnvironment Environment;
    Environment.Solar.ElevationDegrees   = 38.0f;
    Environment.Solar.AzimuthDegrees     = 132.0f;
    Environment.Solar.IlluminanceKilolux = 98.0f;
    Environment.Clouds.Coverage          = 0.42f;
    Environment.ElapsedSeconds           = 0.0;

    for (const std::string& ViewName : ViewNames)
    {
        const ViewSpecification* View = nullptr;
        for (const ViewSpecification& Candidate : Views)
            if (ViewName == Candidate.Name) View = &Candidate;
        if (!View)
        {
            std::printf("unknown view %s\n", ViewName.c_str());
            return 2;
        }
        const std::array<int, 4> Region = {View->Region[0] * static_cast<int>(Width) / 480, View->Region[1] * static_cast<int>(Height) / 270,
                                           View->Region[2] * static_cast<int>(Width) / 480, View->Region[3] * static_cast<int>(Height) / 270};
        const size_t PixelCount = static_cast<size_t>(Width) * Height;
        std::vector<uint8_t> InRegion(PixelCount, 0u);
        for (size_t Pixel = 0; Pixel < PixelCount; ++Pixel)
        {
            const int X = static_cast<int>(Pixel % Width), Y = static_cast<int>(Pixel / Width);
            InRegion[Pixel] = X >= Region[0] && X < Region[2] && Y >= Region[1] && Y < Region[3] ? 1u : 0u;
        }
        ViewportCamera Camera;
        Camera.Position = View->Position;
        Camera.Target   = View->Target;

        // 📝 The reference runs first; every other variant's refreshes are then measured against its mean.
        std::vector<std::string> Order = VariantNames;
        const auto ReferenceAt = std::find_if(Order.begin(), Order.end(), [](const std::string& Name) { return Name.find("Reference") != std::string::npos; });
        if (ReferenceAt != Order.end()) std::rotate(Order.begin(), ReferenceAt, ReferenceAt + 1);
        std::vector<double> ReferenceLuma;
        std::vector<uint8_t> Transition(PixelCount, 0u);
        std::vector<Measurement> Results;
        for (const std::string& VariantName : Order)
        {
            const VariantSpecification* Variant = nullptr;
            for (const VariantSpecification& Candidate : Variants)
                if (VariantName == Candidate.Name) Variant = &Candidate;
            if (!Variant)
            {
                std::printf("unknown variant %s\n", VariantName.c_str());
                return 2;
            }
            const bool IsReference = VariantName.find("Reference") != std::string::npos;
            Host.Settings.VarianceFilter = Variant->VarianceFilter;
            Host.PriorValid              = false;   // 📝 a cleared history: the temporal reuse restarts, the antialiasing takes the refresh
            Viewport.ToggleAdjustment = [Variant, Indirect](RefreshToggles& Toggles) {
                Toggles.RayTracedShadows = Variant->RayTracedShadows;
                Toggles.Indirect         = Indirect;
            };
            ViewportRendering Rendering;
            Rendering.Refinements        = Variant->Refinements;
            Rendering.Inspection         = Variant->Inspection;
            Rendering.GlobalIllumination = Indirect;   // 📝 the viewport sets the host's indirect switch from this every refresh
            Measurement Result;
            Result.Variant = Variant->Name;
            std::vector<double> Sum(PixelCount, 0.0), Square(PixelCount, 0.0);
            std::vector<uint8_t> Rgba;
            StageTotals.clear();
            double RegionSquare = 0.0, TransitionSquare = 0.0;
            const uint32_t TransitionCount = static_cast<uint32_t>(std::count(Transition.begin(), Transition.end(), 1u));
            const uint32_t RegionCount     = static_cast<uint32_t>(std::count(InRegion.begin(), InRegion.end(), 1u));
            const uint32_t Count = Measured;
            // 📝 Every reference refresh is already converged; four refreshes settle its antialiasing history.
            const uint32_t WarmCount = IsReference ? std::min(Warm, 4u) : Warm;
            for (uint32_t Refresh = 0; Refresh < WarmCount + Count; ++Refresh)
            {
                Measuring = Refresh >= WarmCount;
                const auto Start = std::chrono::steady_clock::now();
                if (!Viewport.Refresh(Camera, Environment, Rendering, 0.0, {Width, Height}, &Diagnostic))
                {
                    std::printf("[FAIL] %s refresh %u: %s\n", Variant->Name, Refresh, Diagnostic.c_str());
                    return 1;
                }
                const double Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count();
                if (!Measuring) continue;
                Result.RefreshSeconds += Seconds / Count;
                if (!Viewport.ReadPresented(Rgba))
                {
                    std::printf("[FAIL] %s readback\n", Variant->Name);
                    return 1;
                }
                Result.Fireflies += static_cast<double>(CountFireflies(Rgba, Width, Height, Region)) / Count;
                uint32_t Visible = 0;
                for (size_t Pixel = 0; Pixel < PixelCount; ++Pixel)
                {
                    const double Luma = 255.0 * DisplayLuminance(&Rgba[Pixel * 4]);
                    Sum[Pixel] += Luma;
                    Square[Pixel] += Luma * Luma;
                    if (ReferenceLuma.empty() || !InRegion[Pixel]) continue;
                    const double Error = Luma - ReferenceLuma[Pixel];
                    RegionSquare += Error * Error;
                    if (Transition[Pixel]) TransitionSquare += Error * Error;
                    if (std::abs(Error) > 12.0) ++Visible;
                }
                Result.VisibleErrors += static_cast<double>(Visible) / Count;
            }
            Measuring = false;
            for (const auto& [Stage, Total] : StageTotals) Result.Stages[Stage] = Total / Count;
            Result.Mean.resize(PixelCount);
            Result.Deviation.resize(PixelCount);
            for (size_t Pixel = 0; Pixel < PixelCount; ++Pixel)
            {
                Result.Mean[Pixel]      = Sum[Pixel] / Count;
                Result.Deviation[Pixel] = std::sqrt(std::max(0.0, Square[Pixel] / Count - Result.Mean[Pixel] * Result.Mean[Pixel]));
            }
            if (IsReference)
            {
                // 📝 Transitions: pixels whose 9×9 neighbourhood in the reference spans over 24 levels and whose own luminance lies in
                //    the middle 80 % of that span — penumbrae and reflected edges rather than the flat sides of an edge.
                ReferenceLuma = Result.Mean;
                for (int Y = 4; Y < static_cast<int>(Height) - 4; ++Y)
                    for (int X = 4; X < static_cast<int>(Width) - 4; ++X)
                    {
                        double Low = 1e9, High = -1e9;
                        for (int DY = -4; DY <= 4; ++DY)
                            for (int DX = -4; DX <= 4; ++DX)
                            {
                                const double L = ReferenceLuma[static_cast<size_t>(Y + DY) * Width + X + DX];
                                Low = std::min(Low, L);
                                High = std::max(High, L);
                            }
                        const size_t Pixel = static_cast<size_t>(Y) * Width + X;
                        const double Share = (ReferenceLuma[Pixel] - Low) / std::max(1e-6, High - Low);
                        Transition[Pixel] = InRegion[Pixel] && High - Low > 24.0 && Share > 0.1 && Share < 0.9 ? 1u : 0u;
                    }
                // 📝 The reference against its own mean: its noise.
                double Own = 0.0, OwnTransition = 0.0;
                for (size_t Pixel = 0; Pixel < PixelCount; ++Pixel)
                {
                    if (!InRegion[Pixel]) continue;
                    Own += Result.Deviation[Pixel] * Result.Deviation[Pixel];
                    if (Transition[Pixel]) OwnTransition += Result.Deviation[Pixel] * Result.Deviation[Pixel];
                }
                const uint32_t Transitions = static_cast<uint32_t>(std::count(Transition.begin(), Transition.end(), 1u));
                Result.FrameRms           = std::sqrt(Own / std::max(1u, RegionCount));
                Result.TransitionFrameRms = std::sqrt(OwnTransition / std::max(1u, Transitions));
            }
            else
            {
                Result.FrameRms           = std::sqrt(RegionSquare / std::max(1.0, static_cast<double>(RegionCount) * Count));
                Result.TransitionFrameRms = std::sqrt(TransitionSquare / std::max(1.0, static_cast<double>(TransitionCount) * Count));
            }
            double NoiseSum = 0.0, BiasSquare = 0.0;
            uint32_t Transitions = 0;
            for (size_t Pixel = 0; Pixel < PixelCount && !ReferenceLuma.empty(); ++Pixel)
            {
                if (!Transition[Pixel]) continue;
                NoiseSum += Result.Deviation[Pixel];
                const double Bias = Result.Mean[Pixel] - ReferenceLuma[Pixel];
                BiasSquare += Bias * Bias;
                ++Transitions;
            }
            Result.TransitionNoise = NoiseSum / std::max(1u, Transitions);
            Result.TransitionBias  = std::sqrt(BiasSquare / std::max(1u, Transitions));
            // 📝 The last refresh and the mean over the measured refreshes.
            const std::string Stem = (OutFolder / (std::string(View->Name) + "-" + Variant->Name)).string();
            stbi_write_png((Stem + ".png").c_str(), static_cast<int>(Width), static_cast<int>(Height), 4, Rgba.data(), static_cast<int>(Width * 4));
            std::printf("%s/%s: fireflies %.1f, visible errors %.0f per refresh, refresh %.3f s\n", View->Name, Variant->Name, Result.Fireflies,
                        Result.VisibleErrors, Result.RefreshSeconds);
            for (const auto& [Stage, Milliseconds] : Result.Stages) std::printf("    stage %-28s %9.1f ms\n", Stage.c_str(), Milliseconds);
            Results.push_back(std::move(Result));
        }
        std::printf("\n%s (%u transition pixels of %u in the measured region)\n"
                    "| Variant | Fireflies / refresh | Visible errors / refresh | Refresh RMS | Transition RMS | Transition noise | Transition bias | Refresh [s] |\n"
                    "|---|---:|---:|---:|---:|---:|---:|---:|\n",
                    View->Name, static_cast<unsigned>(std::count(Transition.begin(), Transition.end(), 1u)),
                    static_cast<unsigned>(std::count(InRegion.begin(), InRegion.end(), 1u)));
        for (const Measurement& Result : Results)
            std::printf("| %s | %.1f | %.0f | %.2f | %.2f | %.2f | %.2f | %.3f |\n", Result.Variant.c_str(), Result.Fireflies, Result.VisibleErrors,
                        Result.FrameRms, Result.TransitionFrameRms, Result.TransitionNoise, Result.TransitionBias, Result.RefreshSeconds);
        std::printf("\n");
    }
    return 0;
}
