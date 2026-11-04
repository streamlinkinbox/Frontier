/*==========================================================================================================================================
                                                           RENDERINGEXTENSION.CPP
==========================================================================================================================================*/
// 🧩 One refresh of a level's Scene Viewport: the level's environment and render-pipeline records drive the engine SceneViewport
//    (the ported VehicleHMI ReSTIR GI + the PlanetaryAtmosphere environment) on the host's device.

#include "Frontier/Extensions/Rendering/RenderingExtension.h"

#include "Engine/Diagnostics/DiagnosticJournal.h"
#include "Engine/Diagnostics/LogCategories.h"
#include "Engine/Diagnostics/LogStream.h"
#include "Engine/Interface/ProjectContent.h"
#include "Engine/PhysicalDynamics/Vehicle/TraceContactTyre.h"
#include "Engine/PhysicalDynamics/Vehicle/TyreCarcass.h"
#include "Engine/Scene/VehicleReadings.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace Frontier
{

namespace
{

template <typename EntryType> const EntryType* FindEntry(const Frontier::WorldSpace& World, const char* Type)
{
    return dynamic_cast<const EntryType*>(const_cast<Frontier::WorldSpace&>(World).FindFirstByType(Type));
}

// 📝 Copies an entry's configuration, or disables its effect when the level has no such entry.
template <typename EntryType, typename Configuration>
void ReadConfiguration(const Frontier::WorldSpace& World, const char* Type, Configuration& Target)
{
    if (const EntryType* Entry = FindEntry<EntryType>(World, Type)) Target = Entry->QueryConfiguration();
    else Target.Enabled = false;
}

// 📝 The level-wide medium of a type whose entries can also be bounded volumes (Clouds, Fog): the first entry that is not
//    a local volume. Local volumes (Local Cloud, Local Volumetric Fog) have no renderer here yet.
template <typename EntryType, typename Configuration>
void ReadGlobalConfiguration(const Frontier::WorldSpace& World, const char* Type, Configuration& Target)
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

Frontier::GlobalIllumination::SceneEnvironment ResolveSceneEnvironment(const Frontier::WorldSpace& World, double ElapsedSeconds)
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

RenderingExtension::RenderingExtension(std::filesystem::path ProjectRoot, std::filesystem::path ShaderRoot, const Frontier::VulkanDeviceAccess* HostDevice)
    : ProjectRoot_(std::move(ProjectRoot)), ShaderRoot_(std::move(ShaderRoot)), HostDeviceAttached_(HostDevice != nullptr)
{
    if (HostDevice != nullptr) Device_ = *HostDevice;
}

SceneViewportRefresh RenderingExtension::Render(const SceneViewportRequest& Request, const Frontier::WorldSpace& World, double ElapsedSeconds)
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
        Viewport_           = std::move(Candidate);
        VehicleShown_       = false;
        AuthoringSignature_ = 0;
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
            Pick_.Occupant = Seen.QueryOccupant(0, 0);
            Pick_.Record   = Geometry_.ResolvePickedRecord(Seen);
            if (!Pick_.Record.has_value() && VehicleShown_)
            {
                if (Pick_.Occupant == 0u && ActiveVehicleToken_.IsValid())
                {
                    Pick_.Record = ActiveVehicleToken_.Value;
                }
                else if (Pick_.Occupant >= 1u && Pick_.Occupant <= 4u)
                {
                    const RecordToken CornerToken = ActiveCornerTokens_[Pick_.Occupant - 1u];
                    if (CornerToken.IsValid()) Pick_.Record = CornerToken.Value;
                    else if (ActiveVehicleToken_.IsValid()) Pick_.Record = ActiveVehicleToken_.Value;
                }
            }
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
    SynchronizeVehicleVisuals(World);

    // ③ one refresh through the level's records; with live refresh off the last image stays (Render settings › Refresh timing)
    if (!Pipeline.LiveRefresh && Held_.View != VK_NULL_HANDLE && Held_.View == Viewport_->QueryPresentedView())
    {
        SceneViewportRefresh Paused = Held_;
        Paused.Readout.Held               = true;
        Paused.Readout.Geometry           = Geometry_.QueryResolutions();
        Paused.Readout.CollisionSurfaces  = Geometry_.QueryCollisionSurfaces();
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
    Outcome.Readout.CollisionSurfaces   = Geometry_.QueryCollisionSurfaces();
    Outcome.Readout.Pick                = Pick_;
    Outcome.Readout.GeometryGeneration  = Geometry_.QueryGeneration();
    Held_ = Outcome;
    return Outcome;
}

void RenderingExtension::SynchronizeVehicleVisuals(const Frontier::WorldSpace& World)
{
    if (!Viewport_ || Viewport_->QueryScene() == nullptr || Viewport_->QueryScene()->Occupants.size() < 5u) return;

    const Frontier::VehicleRecord* ShownVehicle = nullptr;
    for (const Frontier::SceneRecordBase* Candidate : World.FindAllByType("Vehicle"))
    {
        const auto* Typed = dynamic_cast<const Frontier::VehicleRecord*>(Candidate);
        if (Typed != nullptr && Typed->QueryConfiguration().Visible)
        {
            ShownVehicle = Typed;
            break;
        }
    }

    if (ShownVehicle == nullptr)
    {
        if (VehicleShown_)
        {
            std::vector<Frontier::GlobalIllumination::OccupantPlacement> Hidden;
            Hidden.reserve(5u);
            for (uint32_t Index = 0u; Index < 5u; ++Index)
            {
                Frontier::GlobalIllumination::OccupantPlacement Placement;
                Placement.Occupant = Index;
                Placement.Current  = { 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0 };
                Placement.Active   = false;
                Hidden.push_back(Placement);
            }
            (void)Viewport_->RelocateOccupants(Hidden);
            VehicleShown_       = false;
            ActiveVehicleToken_ = {};
            ActiveCornerTokens_ = {};
            AuthoringSignature_ = 0u;
        }
        return;
    }

    const Frontier::RecordToken           VehicleToken  = ShownVehicle->QueryToken();
    const Frontier::VehicleConfiguration& Configuration = ShownVehicle->QueryConfiguration();
    const Vehicle::VehicleGeometry&       Geometry      = Configuration.Specification.Geometry;

    constexpr double AuthoredWheelRadius = 0.50855;
    constexpr double AuthoredWheelWidth  = 0.39580;
    const double     RadiusScale         = std::clamp(static_cast<double>(Geometry.TyreRadius) / AuthoredWheelRadius, 0.25, 3.0);
    const double     WidthScale          = std::clamp(static_cast<double>(Geometry.TyreWidth) / AuthoredWheelWidth, 0.25, 3.0);

    const auto ToTransform = [](const std::array<float, 16>& Matrix, double ScaleXZ, double ScaleY)
    {
        Frontier::GlobalIllumination::Transform Out{};
        for (size_t Index = 0; Index < 16; ++Index) Out[Index] = static_cast<double>(Matrix[Index]);
        for (size_t Row = 0; Row < 3; ++Row)
        {
            Out[0 + Row] *= ScaleXZ;
            Out[4 + Row] *= ScaleY;
            Out[8 + Row] *= ScaleXZ;
        }
        Out[15] = 1.0;
        return Out;
    };

    const auto AssembleColumnMajor = [](const Vehicle::Vec3& Position, const Vehicle::Quat& Rotation, double ScaleXZ, double ScaleY)
    {
        const Vehicle::Vec3 X = Rotation.Rotate({ 1.0f, 0.0f, 0.0f });
        const Vehicle::Vec3 Y = Rotation.Rotate({ 0.0f, 1.0f, 0.0f });
        const Vehicle::Vec3 Z = Rotation.Rotate({ 0.0f, 0.0f, 1.0f });
        return Frontier::GlobalIllumination::Transform{
            X.x * ScaleXZ, X.y * ScaleXZ, X.z * ScaleXZ, 0.0,
            Y.x * ScaleY,  Y.y * ScaleY,  Y.z * ScaleY,  0.0,
            Z.x * ScaleXZ, Z.y * ScaleXZ, Z.z * ScaleXZ, 0.0,
            Position.x,    Position.y,    Position.z,    1.0 };
    };

    std::array<RecordToken, 4> CornerTokens{};
    std::array<bool, 4>        CornerFitted{ true, true, true, true };
    std::array<float, 4>       CornerColdPressure{
        Configuration.Specification.Solver.Tyre.InflationPressure,
        Configuration.Specification.Solver.Tyre.InflationPressure,
        Configuration.Specification.Solver.Tyre.InflationPressure,
        Configuration.Specification.Solver.Tyre.InflationPressure };
    uint64_t Signature = (static_cast<uint64_t>(Configuration.Revision) << 16u) ^ VehicleToken.Value ^ (VehicleShown_ ? 0x9E3779B9u : 0u);
    for (const Frontier::SceneRecordBase* Candidate : World.FindAllByType("Tyre"))
    {
        const auto* Tyre = dynamic_cast<const Frontier::TyreRecord*>(Candidate);
        if (Tyre == nullptr || Candidate->QueryParentToken() != VehicleToken) continue;
        const Frontier::TyreConfiguration& Fit    = Tyre->QueryConfiguration();
        const uint32_t                     Corner = Fit.Corner & 3u;
        CornerTokens[Corner]                      = Candidate->QueryToken();
        CornerFitted[Corner]                      = Fit.Enclosed;
        CornerColdPressure[Corner]                = Fit.ColdPressure;
        Signature ^= (static_cast<uint64_t>(Fit.Revision) + 0x9E3779B97F4A7C15ull + (Signature << 6u) + (Signature >> 2u));
    }
    ActiveVehicleToken_ = VehicleToken;
    ActiveCornerTokens_ = CornerTokens;

    Frontier::VehicleReading Live;
    if (Frontier::QueryVehicleReading(VehicleToken, Live) && Live.Simulating && Live.VisualValid)
    {
        AuthoringSignature_ = 0u;
        const bool FirstActivation = !VehicleShown_;
        VehicleShown_              = true;

        const double YawRadians = Configuration.Spatial.OrientationDegrees[2] * 3.14159265358979323846 / 180.0;
        const Vehicle::Quat HeadingQuat{ 0.0f, 0.0f, static_cast<float>(std::sin(0.5 * YawRadians)), static_cast<float>(std::cos(0.5 * YawRadians)) };
        const Vehicle::Vec3 GroundBase{
            Configuration.Spatial.PositionMetres[0],
            Configuration.Spatial.PositionMetres[1],
            Configuration.Spatial.PositionMetres[2] };

        std::vector<Frontier::GlobalIllumination::OccupantPlacement> Placements;
        Placements.reserve(5u);

        Frontier::GlobalIllumination::OccupantPlacement BodyPlacement;
        BodyPlacement.Occupant  = 0u;
        BodyPlacement.Current   = ToTransform(Live.BodyTransform, 1.0, 1.0);
        BodyPlacement.Active    = true;
        BodyPlacement.Recapture = FirstActivation;
        Placements.push_back(BodyPlacement);

        std::array<bool, 4>                   FieldActive{};
        std::array<std::array<float, 4>, 4>   Deformation{};
        std::array<const float*, 4>           FieldPointers{};
        for (uint32_t Corner = 0u; Corner < 4u; ++Corner)
        {
            Frontier::GlobalIllumination::OccupantPlacement WheelPlacement;
            WheelPlacement.Occupant = 1u + Corner;
            WheelPlacement.Active   = true;
            if (Live.CornerFitted[Corner])
            {
                WheelPlacement.Current = ToTransform(Live.WheelTransforms[Corner], RadiusScale, WidthScale);
                FieldActive[Corner]    = Live.CornerFieldValid[Corner];
                FieldPointers[Corner]  = Live.CornerFieldValid[Corner] ? Live.CornerFields[Corner].data() : nullptr;
                Deformation[Corner]    = {
                    Live.ContactAngle[Corner],
                    Live.Deflection[Corner],
                    Live.GaugePressure[Corner],
                    Live.LateralShift[Corner] };
            }
            else
            {
                // 📝 Detached pit-lane tyre: laid flat beside the vehicle on the pit apron.
                const float ForeOffset = Corner < 2u ? 0.72f : -0.72f;
                const float SideOffset = (Corner & 1u) == 0u ? 2.15f : -2.15f;
                const Vehicle::Vec3 PitSpot =
                    GroundBase + HeadingQuat.Rotate({ ForeOffset, SideOffset, 0.5f * Geometry.TyreWidth });
                const Vehicle::Quat FlatQuat =
                    (HeadingQuat * Vehicle::Quat::AxisAngle({ 1.0f, 0.0f, 0.0f }, 1.5707963f)).Normalized();
                WheelPlacement.Current = AssembleColumnMajor(PitSpot, FlatQuat, RadiusScale, WidthScale);
            }
            Placements.push_back(WheelPlacement);
        }
        (void)Viewport_->RelocateOccupants(Placements);
        Viewport_->WriteVehicleState(Live.SteeringColumnAngle, FieldActive, Deformation, FieldPointers);
        return;
    }

    // 📝 Editor authoring mode (simulation stopped): memoize by record revisions so idle refreshes cost nothing.
    if (VehicleShown_ && Signature == AuthoringSignature_) return;
    const bool FirstActivation = !VehicleShown_;
    VehicleShown_              = true;
    AuthoringSignature_        = Signature;

    const Vehicle::VehicleSolverConfiguration Solver = Vehicle::ResolveSolverConfiguration(Configuration.Specification);
    const double YawRadians = Configuration.Spatial.OrientationDegrees[2] * 3.14159265358979323846 / 180.0;
    const double PitchRadians = Configuration.Spatial.OrientationDegrees[1] * 3.14159265358979323846 / 180.0;
    const double RollRadians  = Configuration.Spatial.OrientationDegrees[0] * 3.14159265358979323846 / 180.0;
    const Vehicle::Quat Orientation =
        (Vehicle::Quat::AxisAngle({ 0.0f, 0.0f, 1.0f }, static_cast<float>(YawRadians)) *
         Vehicle::Quat::AxisAngle({ 0.0f, 1.0f, 0.0f }, static_cast<float>(PitchRadians)) *
         Vehicle::Quat::AxisAngle({ 1.0f, 0.0f, 0.0f }, static_cast<float>(RollRadians))).Normalized();

    const Vehicle::Vec3 GroundBase{
        Configuration.Spatial.PositionMetres[0],
        Configuration.Spatial.PositionMetres[1],
        Configuration.Spatial.PositionMetres[2] };

    std::array<Vehicle::CarcassField, 4>  RestingFields{};
    std::array<bool, 4>                   FieldActive{};
    std::array<std::array<float, 4>, 4>   Deformation{};
    std::array<const float*, 4>           FieldPointers{};
    std::array<float, 4>                  CornerDeflections{};

    const float TotalWeight = Geometry.Mass * 9.81f;
    for (uint32_t Corner = 0u; Corner < 4u; ++Corner)
    {
        if (!CornerFitted[Corner]) continue;
        const float Share      = Corner < 2u ? (0.5f * Geometry.FrontWeightFraction) : (0.5f * (1.0f - Geometry.FrontWeightFraction));
        const float TargetLoad = std::max(200.0f, TotalWeight * Share);
        const float Pressure   = std::max(5000.0f, CornerColdPressure[Corner]);

        if (Solver.Carcass == Vehicle::TyreCarcassCategory::Spring)
        {
            const float Stiffness     = Vehicle::PressureStiffness(Solver.TraceContact, Pressure);
            CornerDeflections[Corner] = std::clamp(TargetLoad / std::max(1000.0f, Stiffness), 0.0f, Solver.TraceContact.MaxDeflection);
            Deformation[Corner]       = { -1.5707963f, CornerDeflections[Corner], Pressure, 0.0f };
        }
        else if (auto Soft = Vehicle::ConstructTyreCarcass(Solver.Carcass, Solver.CarcassTuning, Geometry.TyreRadius, Geometry.TyreRimRadius, Geometry.TyreWidth))
        {
            const double EstimatedStiffness  = std::max(10000.0, Vehicle::ResolveCarcassStiffness(Solver.CarcassTuning, Pressure));
            float        EstimatedDeflection = std::clamp(static_cast<float>(TargetLoad / EstimatedStiffness), 0.002f, 0.095f);
            Vehicle::CarcassContact Contact;
            Contact.Ground        = true;
            Contact.GroundPoint   = { 0.0f, 0.0f, 0.0f };
            Contact.GroundNormal  = { 0.0f, 0.0f, 1.0f };
            Contact.HubRotation   = Vehicle::Quat{};
            Contact.GaugePressure = Pressure;
            Contact.Seconds       = 1.0f / 240.0f;

            if (Solver.Carcass == Vehicle::TyreCarcassCategory::Fem)
            {
                for (int Sweep = 0; Sweep < 4; ++Sweep)
                {
                    Contact.HubPosition                 = { 0.0f, 0.0f, Geometry.TyreRadius - EstimatedDeflection };
                    const Vehicle::CarcassResponse Step = Soft->Advance(Contact);
                    if (Step.NormalLoad > 1.0f)
                        EstimatedDeflection = std::clamp(EstimatedDeflection * std::sqrt(TargetLoad / Step.NormalLoad), 0.001f, 0.11f);
                }
            }
            else
            {
                Contact.HubPosition = { 0.0f, 0.0f, Geometry.TyreRadius - EstimatedDeflection };
                Soft->Reset(Contact);
                for (int Sweep = 0; Sweep < 28; ++Sweep) (void)Soft->Advance(Contact);
            }
            Contact.HubPosition                     = { 0.0f, 0.0f, Geometry.TyreRadius - EstimatedDeflection };
            const Vehicle::CarcassResponse Settled  = Soft->Advance(Contact);
            CornerDeflections[Corner]               = std::max(0.0005f, Settled.Deflection > 0.0f ? Settled.Deflection : EstimatedDeflection);
            Soft->SampleField(0.0f, RestingFields[Corner]);
            FieldActive[Corner]   = true;
            FieldPointers[Corner] = RestingFields[Corner].data();
            Deformation[Corner]   = { -1.5707963f, CornerDeflections[Corner], Pressure, 0.0f };
        }
    }

    const float MeanDeflection = 0.25f * (CornerDeflections[0] + CornerDeflections[1] + CornerDeflections[2] + CornerDeflections[3]);
    const Vehicle::Vec3 BodyOrigin =
        GroundBase + Orientation.Rotate({ 0.0f, 0.0f, Geometry.AuthoredGroundOffset() - MeanDeflection });

    std::vector<Frontier::GlobalIllumination::OccupantPlacement> Placements;
    Placements.reserve(5u);

    Frontier::GlobalIllumination::OccupantPlacement BodyPlacement;
    BodyPlacement.Occupant  = 0u;
    BodyPlacement.Current   = AssembleColumnMajor(BodyOrigin, Orientation, 1.0, 1.0);
    BodyPlacement.Active    = true;
    BodyPlacement.Recapture = FirstActivation;
    Placements.push_back(BodyPlacement);

    const float AxleX[4] = { Geometry.FrontAxleX(), Geometry.FrontAxleX(), -Geometry.RearAxleX(), -Geometry.RearAxleX() };
    const float AxleY[4] = { +0.5f * Geometry.TrackFront, -0.5f * Geometry.TrackFront, +0.5f * Geometry.TrackRear, -0.5f * Geometry.TrackRear };
    for (uint32_t Corner = 0u; Corner < 4u; ++Corner)
    {
        Frontier::GlobalIllumination::OccupantPlacement WheelPlacement;
        WheelPlacement.Occupant = 1u + Corner;
        WheelPlacement.Active   = true;
        if (CornerFitted[Corner])
        {
            const Vehicle::Vec3 HubWorld =
                GroundBase + Orientation.Rotate({ AxleX[Corner], AxleY[Corner], Geometry.TyreRadius - CornerDeflections[Corner] });
            WheelPlacement.Current = AssembleColumnMajor(HubWorld, Orientation, RadiusScale, WidthScale);
        }
        else
        {
            const float ForeOffset = Corner < 2u ? 0.72f : -0.72f;
            const float SideOffset = (Corner & 1u) == 0u ? 2.15f : -2.15f;
            const Vehicle::Vec3 PitSpot =
                GroundBase + Orientation.Rotate({ ForeOffset, SideOffset, 0.5f * Geometry.TyreWidth });
            const Vehicle::Quat FlatQuat =
                (Orientation * Vehicle::Quat::AxisAngle({ 1.0f, 0.0f, 0.0f }, 1.5707963f)).Normalized();
            WheelPlacement.Current = AssembleColumnMajor(PitSpot, FlatQuat, RadiusScale, WidthScale);
        }
        Placements.push_back(WheelPlacement);
    }

    (void)Viewport_->RelocateOccupants(Placements);
    Viewport_->WriteVehicleState(0.0f, FieldActive, Deformation, FieldPointers);
}

} // namespace Frontier
