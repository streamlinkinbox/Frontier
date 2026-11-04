/*==========================================================================================================================================
                                                      EVERESTSCENEVIEWPORT.CPP
==========================================================================================================================================*/
// 🧩 Project Everest's live Scene Viewport: the level's environment and render-pipeline records drive the engine SceneViewport
//    (ported VehicleHMI ReSTIR GI of the proving ground + the PlanetaryAtmosphere environment) on the host's device.

#include "EverestSceneViewport.h"

#include "Engine/Diagnostics/DiagnosticJournal.h"
#include "Engine/Diagnostics/LogCategories.h"
#include "Engine/Diagnostics/LogStream.h"
#include "Engine/Interface/ProjectContent.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace Everest
{

namespace
{

template <typename EntryType> const EntryType* FindEntry(const Frontier::SceneWorld& World, const char* Type)
{
    return dynamic_cast<const EntryType*>(const_cast<Frontier::SceneWorld&>(World).FindFirstByType(Type));
}

// 📝 Copies an entry's configuration, or disables its effect when the level has no such entry.
template <typename EntryType, typename Configuration>
void ReadConfiguration(const Frontier::SceneWorld& World, const char* Type, Configuration& Target)
{
    if (const EntryType* Entry = FindEntry<EntryType>(World, Type)) Target = Entry->QueryConfiguration();
    else Target.Enabled = false;
}

// 📝 The level-wide medium of a type whose entries can also be bounded volumes (Clouds, Fog): the first entry that is not
//    a local volume. Local volumes (Local Cloud, Local Volumetric Fog) have no renderer here yet.
template <typename EntryType, typename Configuration>
void ReadGlobalConfiguration(const Frontier::SceneWorld& World, const char* Type, Configuration& Target)
{
    for (const Frontier::SceneRecordBase* Entry : World.FindAllByType(Type))
    {
        const auto* Typed = dynamic_cast<const EntryType*>(Entry);
        if (Typed != nullptr && !Typed->QueryConfiguration().LocalVolume)
        {
            Target = Typed->QueryConfiguration();
            return;
        }
    }
    Target.Enabled = false;
}

std::filesystem::path ResolveArchive(const std::filesystem::path& ProjectRoot, const char* Archive)
{
    // 📝 The project's staged import first, the engine's tracked archive as the fallback (unstaged checkouts).
    const std::filesystem::path Staged = ProjectRoot / "Content" / "EnvironmentArchives" / Archive;
    if (std::filesystem::exists(Staged)) return Staged;
    const std::filesystem::path Engine = ProjectRoot.parent_path().parent_path() / "EngineContent" / "EnvironmentArchives" / Archive;
    return std::filesystem::exists(Engine) ? Engine : Staged;
}

uint32_t ViewportLogCategory()
{
    return Frontier::LogCategories().Register("Viewport", Frontier::LogColour::Mint, true);
}

} // namespace

Frontier::SolarPlacement SolveSolarPlacement(const Frontier::SolarPlacementQuery& Query)
{
    Frontier::GlobalIllumination::EnvironmentSettings Settings;
    Settings.SkyDate   = Query.CalendarDate;
    Settings.Latitude  = Query.LatitudeDegrees;
    Settings.Longitude = Query.LongitudeDegrees;
    Settings.TimeZone  = Query.UtcOffsetHours;
    Settings.SunTime   = Query.LocalSolarHours;
    const Frontier::GlobalIllumination::CelestialSolution Solved = Frontier::GlobalIllumination::SolveCelestial(Settings);
    return {Solved.Sun.Elevation, Solved.Sun.Azimuth};
}

bool AdvanceSolarDay(Frontier::SolarConfiguration& Sun, double DeltaSeconds)
{
    // 📝 AdvanceCelestial: SunTime = (SunTime + Δt · 24 / (DayDuration · 3600)) mod 24, and the cycle links the sun.
    if (Sun.DayCycle)
    {
        const double Advanced = static_cast<double>(Sun.LocalSolarHours) +
                                std::max(0.0, DeltaSeconds) * 24.0 / (std::max(0.01, static_cast<double>(Sun.DayDurationHours)) * 3600.0);
        Sun.LocalSolarHours = static_cast<float>(std::fmod(Advanced, 24.0));
        Sun.TimeLinked      = true;
    }
    if (!Sun.TimeLinked) return false;
    const Frontier::SolarPlacement Placement = SolveSolarPlacement(
        {Sun.CalendarDate, Sun.LatitudeDegrees, Sun.LongitudeDegrees, Sun.UtcOffsetHours, Sun.LocalSolarHours});
    const auto Elevation = static_cast<float>(Placement.ElevationDegrees);
    const auto Azimuth   = static_cast<float>(Placement.AzimuthDegrees);
    if (Elevation == Sun.ElevationDegrees && Azimuth == Sun.AzimuthDegrees) return false;
    Sun.ElevationDegrees = Elevation;
    Sun.AzimuthDegrees   = Azimuth;
    return true;
}

Frontier::GlobalIllumination::SceneEnvironment ResolveSceneEnvironment(const Frontier::SceneWorld& World, double ElapsedSeconds)
{
    Frontier::GlobalIllumination::SceneEnvironment Environment;
    ReadConfiguration<Frontier::SolarRecord>(World, "Solar", Environment.Solar);
    ReadConfiguration<Frontier::OpticalFlareRecord>(World, "OpticalFlare", Environment.Flare);
    ReadConfiguration<Frontier::AtmosphereRecord>(World, "Atmosphere", Environment.Atmosphere);
    ReadGlobalConfiguration<Frontier::CloudRecord>(World, "Clouds", Environment.Clouds);
    ReadConfiguration<Frontier::StarRecord>(World, "Stars", Environment.Stars);
    ReadConfiguration<Frontier::MoonRecord>(World, "Moons", Environment.Moons);
    ReadGlobalConfiguration<Frontier::FogRecord>(World, "Fog", Environment.Fog);
    ReadConfiguration<Frontier::WindRecord>(World, "Wind", Environment.Wind);
    if (const auto Entries = World.FindAllByType("ForceField"); !Entries.empty())
        if (const auto* Entry = dynamic_cast<const Frontier::ForceFieldRecord*>(Entries.front())) Environment.Forces = Entry->QueryConfiguration();
    for (const auto* Entry : World.FindAllByType("ForceSource"))
        if (const auto* Source = dynamic_cast<const Frontier::ForceSourceRecord*>(Entry); Source && Source->QueryConfiguration().Enabled)
            Environment.ForceSources.push_back(Source->QueryConfiguration());
    if (const auto Entries = World.FindAllByType("Atmosphere"); !Entries.empty()) Environment.AtmosphereToken = Entries.front()->QueryToken().Value;
    for (const auto* Entry : World.FindAllByType("Clouds"))
    {
        const auto* Cloud = dynamic_cast<const Frontier::CloudRecord*>(Entry);
        if (!Cloud) continue;
        if (Cloud->QueryConfiguration().LocalVolume)
            Environment.LocalClouds.push_back({Entry->QueryToken().Value, Cloud->QueryConfiguration()});
        else if (!Environment.CloudToken) Environment.CloudToken = Entry->QueryToken().Value;
    }
    for (const auto* Entry : World.FindAllByType("Fog"))
    {
        const auto* Fog = dynamic_cast<const Frontier::FogRecord*>(Entry);
        if (Fog && Fog->QueryConfiguration().LocalVolume)
            Environment.LocalFogVolumes.push_back({Entry->QueryToken().Value, Fog->QueryConfiguration()});
    }
    // 📝 Each planet entry drives the body it names; a level without planet entries keeps the four default planets.
    const std::vector<const Frontier::SceneRecordBase*> Planets = World.FindAllByType("Planet");
    if (!Planets.empty())
    {
        for (Frontier::PlanetConfiguration& Planet : Environment.Planets) Planet.Enabled = false;
        for (const Frontier::SceneRecordBase* Entry : Planets)
        {
            if (const auto* Planet = dynamic_cast<const Frontier::PlanetRecord*>(Entry))
            {
                const Frontier::PlanetConfiguration& Configuration = Planet->QueryConfiguration();
                Environment.Planets[std::min<uint32_t>(Configuration.Body, 3u)] = Configuration;
            }
        }
    }
    // 📝 The flare belongs to the sun: no sun, no flare.
    Environment.Flare.Enabled  = Environment.Flare.Enabled && Environment.Solar.Enabled;
    Environment.ElapsedSeconds = ElapsedSeconds;
    return Environment;
}

EverestSceneViewport::EverestSceneViewport(std::filesystem::path ProjectRoot, std::filesystem::path ShaderRoot, const Frontier::VulkanDeviceAccess* HostDevice)
    : ProjectRoot_(std::move(ProjectRoot)), ShaderRoot_(std::move(ShaderRoot)), HostDeviceAttached_(HostDevice != nullptr)
{
    if (HostDevice != nullptr) Device_ = *HostDevice;
}

SceneViewportRefresh EverestSceneViewport::Render(const SceneViewportRequest& Request, const Frontier::SceneWorld& World, double ElapsedSeconds)
{
    SceneViewportRefresh Outcome;
    Frontier::RenderPipelineConfiguration Pipeline;
    if (const auto* Entry = FindEntry<Frontier::RenderPipelineRecord>(World, "RenderPipeline")) Pipeline = Entry->QueryConfiguration();
    const std::string Quality    = Pipeline.QualityTier <= 0 ? "low" : (Pipeline.QualityTier == 1 ? "medium" : "high");
    const bool        RayTracing = Pipeline.HardwareRayQueryTier;
    Outcome.Readout.Quality = Quality;
    if (!Pipeline.Enabled)
    {
        Outcome.Status          = "Render pipeline disabled";
        Outcome.Readout.Failure = Outcome.Status;
        return Outcome;
    }
    if (HostDeviceAttached_ && !Device_.Valid())
    {
        Outcome.Status          = "Scene viewport needs a Vulkan 1.3 adapter with dynamic rendering";
        Outcome.Readout.Failure = Outcome.Status;
        return Outcome;
    }

    // ① (re)build when the tiers change; a failed bring-up is not retried until they change again
    if (!Viewport_ || Quality != Quality_ || RayTracing != RayTracing_)
    {
        if (!Failure_.empty() && Quality == Quality_ && RayTracing == RayTracing_)
        {
            Outcome.Status          = Failure_;
            Outcome.Readout.Failure = Failure_;
            return Outcome;
        }
        Viewport_.reset();
        Quality_    = Quality;
        RayTracing_ = RayTracing;
        Failure_.clear();
        Frontier::GlobalIllumination::SceneViewportConfiguration Configuration;
        Configuration.GlobalIlluminationShaders = ShaderRoot_ / "GlobalIllumination";
        Configuration.EnvironmentShaders        = ShaderRoot_ / "Environment";
        Configuration.SceneArchive              = ResolveArchive(ProjectRoot_, "ProvingGround");
        Configuration.CelestialMaps             = ResolveArchive(ProjectRoot_, "CelestialMaps");
        // 📝 The engine shapes sit beside the shader archives (EngineContent/ShapeArchives); shaders compiled into a build folder
        //    have none beside them, so the engine content's own folder serves (the Construct dialog lists the same one).
        const std::filesystem::path ShaderFolder = ShaderRoot_.has_filename() ? ShaderRoot_ : ShaderRoot_.parent_path();
        std::error_code             Missing;
        Configuration.ShapeArchives             = ShaderFolder.parent_path() / "ShapeArchives";
        if (!std::filesystem::is_directory(Configuration.ShapeArchives, Missing))
            Configuration.ShapeArchives = Frontier::QueryEngineContentDirectory() / "ShapeArchives";
        Configuration.Quality                   = Quality;
        Configuration.RayTracing                = RayTracing;
        // 🔍 Every bring-up step and device fault lands in the diagnostic journal as it happens (a lost device still leaves
        //    the step it struck in the log).
        Configuration.Journal = [](const std::string& Line) { Frontier::DiagnosticJournal::Append("SceneViewport", Line); };
        const auto Extent = Frontier::GlobalIllumination::SceneViewport::ResolveScreenExtent(Request.PanelWidth, Request.PanelHeight, Request.PixelDensity, Quality,
                                                                                             Pipeline.RenderScale);
        auto Candidate = std::make_unique<Frontier::GlobalIllumination::SceneViewport>();
        std::string Diagnostic;
        const auto  Start = std::chrono::steady_clock::now();
        if (!Candidate->Construct(HostDeviceAttached_ ? &Device_ : nullptr, Configuration, Extent, &Diagnostic))
        {
            Failure_ = "Scene viewport unavailable: " + Diagnostic;
            Frontier::WriteLogFormat(ViewportLogCategory(), Frontier::LogSeverity::Error, Frontier::LogVerbosity::Log, "SceneViewport", "%s",
                                     Failure_.c_str());
            Outcome.Status          = Failure_;
            Outcome.Readout.Failure = Failure_;
            return Outcome;
        }
        Viewport_ = std::move(Candidate);
        Frontier::WriteLogFormat(ViewportLogCategory(), Frontier::LogSeverity::Info, Frontier::LogVerbosity::Log, "SceneViewport",
                                 "Proving ground up on %s in %.1f s (%s, %s, vehicle omitted).", Viewport_->QueryAdapter().c_str(),
                                 std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count(), Quality.c_str(),
                                 RayTracing ? "RTX ReSTIR" : "GTX distance field");
    }

    // ①½ GPU picking: a new pick serial asks for the record drawn under the stage point in the image the click was made on — the
    //    last refreshed image, before this request's placements and refresh change anything
    if (Request.PickSerial != Pick_.Serial)
    {
        Pick_        = {};
        Pick_.Serial = Request.PickSerial;
        Frontier::GlobalIllumination::OccupantPick Seen;
        std::string                                Diagnostic;
        if (Viewport_->QueryScene() != GeometryScene_)
        {
            Pick_.Failure = "the scene was rebuilt since the image was made";
        }
        else if (Viewport_->PickOccupants(Request.PickPoint, Seen, &Diagnostic))
        {
            Pick_.Occupant     = Seen.QueryOccupant(0, 0);
            Pick_.Record       = Geometry_.ResolvePickedRecord(Seen);
            Pick_.Milliseconds = Seen.Milliseconds;
        }
        else
        {
            Pick_.Failure = Diagnostic;
        }
    }

    // ② the level's geometry records on the scene's occupants: a new scene (a rebuilt viewport) is bound afresh, then only the
    //    records that changed are placed again
    if (Viewport_->QueryScene() != GeometryScene_)
    {
        std::vector<uint8_t> Materials;
        if (std::ifstream Stream(Viewport_->QueryConfiguration().SceneArchive / "Materials.bin", std::ios::binary); Stream)
            Materials.assign(std::istreambuf_iterator<char>(Stream), std::istreambuf_iterator<char>());
        Geometry_.Reset(Viewport_->QueryScene(), std::move(Materials), Viewport_->QueryShapes());
        GeometryScene_ = Viewport_->QueryScene();
    }
    if (const auto Placements = Geometry_.ResolvePlacements(World); !Placements.empty())
    {
        // 📝 The objects' own surfaces first: the placements capture their cards again from them.
        for (const auto& [Block, Records] : Geometry_.ConsumeMaterialBlocks())
            if (auto* Host = Viewport_->AccessHost()) (void)Host->WriteMaterialBlock(Block, Records);
        (void)Viewport_->RelocateOccupants(Placements);
    }

    // ③ one refresh through the level's records; with live refresh off the last image stays (Render settings › Refresh timing)
    if (!Pipeline.LiveRefresh && Held_.View != VK_NULL_HANDLE && Held_.View == Viewport_->QueryPresentedView())
    {
        SceneViewportRefresh Paused = Held_;
        Paused.Readout.Held               = true;
        Paused.Readout.Geometry           = Geometry_.QueryResolutions();
        Paused.Readout.Pick               = Pick_;
        Paused.Readout.GeometryGeneration = Geometry_.QueryGeneration();
        Paused.Status                     = "HELD: live refresh is off  |  " + Held_.Status;
        return Paused;
    }
    Frontier::GlobalIllumination::ViewportCamera Camera;
    Camera.Position            = {Request.CameraPosition[0], Request.CameraPosition[1], Request.CameraPosition[2]};
    Camera.Target              = {Request.CameraTarget[0], Request.CameraTarget[1], Request.CameraTarget[2]};
    Camera.VerticalFieldOfView = Request.VerticalFieldOfView;
    Camera.Up = Request.CameraUp;
    Frontier::GlobalIllumination::ViewportRendering Rendering;
    Rendering.GlobalIllumination   = Pipeline.GlobalIllumination;
    Rendering.TemporalAntiAliasing = Pipeline.TemporalAntiAliasing;
    Rendering.ExposureCompensation = Pipeline.ExposureCompensationEv;
    Rendering.Inspection           = static_cast<uint32_t>(std::clamp(Pipeline.InspectionModeIndex, 0, 14));
    Rendering.VirtualizedGeometry  = Pipeline.VirtualizedGeometry;
    Rendering.VirtualizedStreamSlots = static_cast<uint32_t>(std::max(0, Pipeline.VirtualizedStreamSlots));
    Rendering.AnalyticIllumination   = Pipeline.AnalyticIllumination;
    namespace Bit = Frontier::GlobalIllumination::RefinementBit;
    Rendering.Refinements = (Pipeline.SolarDisc ? Bit::SolarDisc : 0u) | (Pipeline.StratifiedSun ? Bit::StratifiedSun : 0u) |
                            (Pipeline.PenumbraFilter ? Bit::PenumbraFilter : 0u) | (Pipeline.MirrorFilter ? Bit::MirrorFilter : 0u) |
                            (Pipeline.MirrorTrace ? Bit::MirrorTrace : 0u);
    // 📝 The rest of the Render settings record, setting for setting onto the demo's toggles, host settings and variance filters.
    auto& Toggles                = Rendering.Toggles;
    Toggles.InitialCandidates    = static_cast<uint32_t>(std::clamp(Pipeline.InitialCandidates, 1, 4));
    Toggles.MaxBounces           = static_cast<uint32_t>(std::clamp(Pipeline.MaxBounces, 1, 4));
    Toggles.MultiBounceNee       = Pipeline.MultiBounceNee;
    Toggles.RussianRoulette      = Pipeline.RussianRoulette;
    Toggles.TemporalReuse        = Pipeline.TemporalReuse;
    Toggles.SpatialReuse         = Pipeline.SpatialReuse;
    Toggles.VisibilityReuse      = Pipeline.VisibilityReuse;
    Toggles.RadianceRefresh      = Pipeline.RadianceRefresh;
    Toggles.SpatialCount         = static_cast<uint32_t>(std::clamp(Pipeline.SpatialCount, 0, 8));
    Toggles.SpatialRadius        = Pipeline.SpatialRadius;
    Toggles.TemporalTallyLimit   = Pipeline.TemporalTallyLimit;
    Toggles.IndirectScale        = Pipeline.IndirectScale;
    Toggles.SkyScale             = Pipeline.SkyScale;
    Toggles.IncandescenceScale   = Pipeline.IncandescenceScale;
    Toggles.CardRadiosity        = Pipeline.CardRadiosity;
    Toggles.Reflection           = Pipeline.Reflection;
    Toggles.RayTracedReflections = Pipeline.RayTracedReflections;
    Toggles.ReflectionBounces    = static_cast<uint32_t>(std::clamp(Pipeline.ReflectionBounces, 1, 4));
    Toggles.ReflectionScale      = Pipeline.ReflectionScale;
    Toggles.ReflectionRoughness  = Pipeline.ReflectionRoughness;
    Toggles.DirectShadow         = Pipeline.DirectShadow;
    Toggles.RayTracedShadows     = Pipeline.RayTracedShadows;
    Toggles.FireflyFilter        = Pipeline.FireflyFilter;
    Toggles.VarianceFilter       = Pipeline.VarianceFilter;
    Toggles.AutoExposure         = Pipeline.AutoExposure;
    Toggles.DistanceInspection   = static_cast<uint32_t>(std::clamp(Pipeline.DistanceInspection, 1, 3));
    Rendering.TraceScale         = static_cast<uint32_t>(std::clamp(Pipeline.TraceScale, 1, 4));
    Rendering.CardTileBudget     = static_cast<uint32_t>(std::max(0, Pipeline.CardTileBudget));
    const auto Filter = [](int Levels, int Tally, float Weight, float Sigma) {
        return Frontier::GlobalIllumination::VarianceSettings{Weight, static_cast<uint32_t>(std::clamp(Tally, 1, 64)),
                                                              static_cast<uint32_t>(std::clamp(Levels, 1, 4) - 1), Sigma};
    };
    Rendering.DiffuseFilter = Filter(Pipeline.DiffuseWaveletLevels, Pipeline.DiffuseTallyLimit, Pipeline.DiffuseMinimumWeight, Pipeline.DiffuseColourSigma);
    Rendering.GlossyFilter  = Filter(Pipeline.GlossyWaveletLevels, Pipeline.GlossyTallyLimit, Pipeline.GlossyMinimumWeight, Pipeline.GlossyColourSigma);
    // 📝 Screen targets follow the panel once its size held still for six interface ticks (a dragged splitter would rebuild
    //    them on every tick); meanwhile the last extent stretches over the panel.
    const auto Requested = Frontier::GlobalIllumination::SceneViewport::ResolveScreenExtent(Request.PanelWidth, Request.PanelHeight, Request.PixelDensity, Quality,
                                                                                            Pipeline.RenderScale);
    StableExtentCount_ = Requested == RequestedExtent_ ? StableExtentCount_ + 1 : 0;
    RequestedExtent_   = Requested;
    const auto Extent = StableExtentCount_ >= 6 ? Requested : Viewport_->QueryScreenExtent();
    std::string Diagnostic;
    const auto  Start = std::chrono::steady_clock::now();
    if (!Viewport_->Refresh(Camera, ResolveSceneEnvironment(World, ElapsedSeconds), Rendering, Request.DeltaSeconds, Extent, &Diagnostic))
    {
        Failure_ = "Scene viewport refresh failed: " + Diagnostic;
        Frontier::WriteLogFormat(ViewportLogCategory(), Frontier::LogSeverity::Error, Frontier::LogVerbosity::Log, "SceneViewport", "%s",
                                 Failure_.c_str());
        Viewport_.reset();
        Outcome.Status          = Failure_;
        Outcome.Readout.Failure = Failure_;
        return Outcome;
    }
    const double Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
    RefreshMilliseconds_      = RefreshMilliseconds_ <= 0.0 ? Milliseconds : RefreshMilliseconds_ * 0.9 + Milliseconds * 0.1;
    Outcome.Readout.LatestMilliseconds = Milliseconds;
    if (auto* Overlay = Viewport_->AccessOverlay()) Outcome.Readout.Environment = Overlay->QueryReadout();
    Outcome.View   = Viewport_->QueryPresentedView();
    Outcome.Extent = Viewport_->QueryScreenExtent();
    char Status[160];
    if (Pipeline.AnalyticIllumination)
        std::snprintf(Status, sizeof(Status), "LIVE: Analytic illumination (fast preview)  |  %u x %u  |  %.1f ms", Outcome.Extent[0], Outcome.Extent[1],
                      RefreshMilliseconds_);
    else
        std::snprintf(Status, sizeof(Status), "LIVE: ReSTIR GI %s + PlanetaryAtmosphere  |  %u x %u  |  %.1f ms", RayTracing ? "RTX" : "GTX", Outcome.Extent[0],
                      Outcome.Extent[1], RefreshMilliseconds_);
    Outcome.Status = Status;

    // 📝 The demo's readout figures (IlluminationHost.js PresentMeasurements): renderer tag and the TLAS line.
    const Frontier::GlobalIllumination::ViewportStatistics Statistics = Viewport_->QueryStatistics();
    char Tag[96];
    if (Pipeline.AnalyticIllumination)
    {
        std::snprintf(Tag, sizeof(Tag), "Analytic illumination (no rays)");
    }
    else if (Statistics.RayTracing)
    {
        std::snprintf(Tag, sizeof(Tag), "RTX ReSTIR PT (%uB GI \xC2\xB7 %uB Refl \xC2\xB7 M=%u)", Statistics.MaxBounces, Statistics.ReflectionBounces,
                      Statistics.InitialCandidates);
        char Acceleration[160];
        std::snprintf(Acceleration, sizeof(Acceleration), "BVH hierarchy   TLAS %u instances (%u nodes) \xC2\xB7 BLAS %u tris", Statistics.TlasInstances,
                      Statistics.TlasEntries, Statistics.BlasTriangles);
        Outcome.Readout.AccelerationLine = Acceleration;
    }
    else
    {
        std::snprintf(Tag, sizeof(Tag), "GTX SDF GI");
    }
    Outcome.Readout.Presenting          = Outcome.View != VK_NULL_HANDLE;
    Outcome.Readout.Adapter             = Viewport_->QueryAdapter();
    Outcome.Readout.RendererTag         = Tag;
    Outcome.Readout.SceneTriangles = Statistics.SceneTriangles;
    Outcome.Readout.ScreenExtent        = Statistics.ScreenExtent;
    Outcome.Readout.TraceExtent         = Statistics.TraceExtent;
    Outcome.Readout.RefreshMilliseconds = RefreshMilliseconds_;
    Outcome.Readout.RayTracing          = Statistics.RayTracing;
    Outcome.Readout.TlasInstances       = Statistics.TlasInstances;
    Outcome.Readout.ClipResolution      = Statistics.ClipResolution;
    Outcome.Readout.SurfaceCards        = Statistics.SurfaceCards;
    Outcome.Readout.GeometryClusters    = Statistics.GeometryClusters;
    Outcome.Readout.Geometry            = Geometry_.QueryResolutions();
    Outcome.Readout.Pick                = Pick_;
    Outcome.Readout.GeometryGeneration  = Geometry_.QueryGeneration();
    Held_ = Outcome;
    return Outcome;
}

} // namespace Everest
