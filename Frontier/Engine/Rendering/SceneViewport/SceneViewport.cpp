/*==========================================================================================================================================
                                                           SCENEVIEWPORT.CPP
==========================================================================================================================================*/
// 🧩 The live scene viewport: the ported VehicleHMI ReSTIR GI refresh of the proving ground (no vehicle), its own sky driven by the
//    level's environment records, and the PlanetaryAtmosphere environment overlaid on it, presented as one image.

#include "SceneViewport.h"

#include "Engine/DeviceExchange/VulkanComputeExchange.h"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace Frontier::GlobalIllumination
{

namespace
{

constexpr double Pi = 3.141592653589793;

bool ReadArchive(const std::filesystem::path& Path, std::vector<uint8_t>& OutBytes, std::string* OutDiagnostic)
{
    std::ifstream Stream(Path, std::ios::binary);
    if (!Stream)
    {
        if (OutDiagnostic != nullptr) *OutDiagnostic = "scene archive missing: " + Path.string();
        return false;
    }
    OutBytes.assign(std::istreambuf_iterator<char>(Stream), std::istreambuf_iterator<char>());
    return true;
}

} // namespace

SceneViewport::SceneViewport() = default;

SceneViewport::~SceneViewport()
{
    // 📝 Device resources die before their exchanges; the borrowed environment views before the environment images.
    PresentedView_.reset();
    Pick_.reset();
    Overlay_.reset();
    Host_.reset();
    if (Environment_) Environment_->Shutdown();
    Environment_.reset();
    if (Exchange_) Exchange_->Shutdown();
}

std::array<uint32_t, 2> SceneViewport::ResolveScreenExtent(double PanelWidth, double PanelHeight, double PixelDensity, const std::string& Quality,
                                                           double ViewportScale)
{
    const QualityGrade Grade = ResolveQualityGrade(Quality);
    double Ratio = std::min(PixelDensity > 0.0 ? PixelDensity : 1.0, 2.0) * Grade.RenderScale * std::clamp(ViewportScale, 0.25, 2.0);
    const double Pixels = PanelWidth * PanelHeight * Ratio * Ratio;
    if (Pixels > Grade.PixelLimit) Ratio *= std::sqrt(Grade.PixelLimit / Pixels);
    return {static_cast<uint32_t>(std::max(64.0, std::floor(PanelWidth * Ratio))), static_cast<uint32_t>(std::max(64.0, std::floor(PanelHeight * Ratio)))};
}

bool SceneViewport::Construct(const VulkanDeviceAccess* HostDevice, const SceneViewportConfiguration& Configuration, std::array<uint32_t, 2> ScreenExtent,
                              std::string* OutDiagnostic)
{
    Configuration_ = Configuration;
    Exchange_      = std::make_unique<DeviceExchange>();
    if (HostDevice != nullptr ? !Exchange_->AttachDevice(*HostDevice, OutDiagnostic) : !Exchange_->Initialize("", OutDiagnostic)) return false;
    Adapter_ = Exchange_->AdapterDescription();
    if (Configuration.Journal)
    {
        Exchange_->FaultObserver = [Journal = Configuration.Journal](const std::string& Fault) { Journal("device fault: " + Fault); };
    }
    // 🔍 A device the editor already lost fails every submission; say so instead of blaming the first one made here.
    if (const VkResult Health = Exchange_->ProbeDevice(); Health != VK_SUCCESS)
    {
        if (OutDiagnostic != nullptr)
        {
            *OutDiagnostic = std::string("the Vulkan device was lost before the scene viewport submitted anything (vkQueueWaitIdle returned ") +
                             (Health == VK_ERROR_DEVICE_LOST ? "VK_ERROR_DEVICE_LOST" : std::to_string(static_cast<int>(Health))) + ")";
        }
        return false;
    }
    if (Configuration.Journal) Configuration.Journal("bring-up on " + Adapter_ + " (" + (HostDevice != nullptr ? "the editor's device" : "an own device") + ")");

    // ① the proving ground exactly as the demo built it, the vehicle deactivated (no car yet)
    IlluminationAssets Assets;
    Assets.FacetSpace  = Configuration.SceneArchive / "ProvingGround.facetspace";
    Assets.OmitVehicle = true;
    if (!ReadArchive(Configuration.SceneArchive / "Materials.bin", Assets.MaterialProfiles, OutDiagnostic)) return false;
    // 📝 Beyond the demo: the engine shapes (EngineContent/ShapeArchives) and the editor's reserve of occupants for copies and
    //    constructed objects. A shape without a matching bake is transferred on the device like any facet set.
    if (!Configuration.ShapeArchives.empty())
    {
        std::vector<std::string> Diagnostics;
        Shapes_ = ReadShapeArchives(Configuration.ShapeArchives, &Diagnostics);
        for (const ShapeArchive& Shape : Shapes_)
            if (!Shape.Distance) Diagnostics.push_back(Shape.Name + ": " + Shape.Diagnostic);
        if (Configuration.Journal)
        {
            Configuration.Journal("engine shapes: " + std::to_string(Shapes_.size()) + " from " + Configuration.ShapeArchives.string());
            for (const std::string& Diagnostic : Diagnostics) Configuration.Journal("engine shapes: " + Diagnostic);
        }
    }
    Assets.Shapes          = Shapes_;
    Assets.ReserveCapacity = Configuration.ReserveCapacity;
    IlluminationSettings Settings;
    Settings.Quality    = Configuration.Quality;
    Settings.RayTracing = Configuration.RayTracing;
    Settings.TraceScale = std::max(1u, Configuration.TraceScale);
    Host_ = std::make_unique<IlluminationHost>(*Exchange_, Configuration.GlobalIlluminationShaders);
    Host_->Journal = Configuration.Journal;
    ScreenExtent = {std::max(64u, ScreenExtent[0]), std::max(64u, ScreenExtent[1])};
    if (!Host_->Construct(Assets, Settings, ScreenExtent, OutDiagnostic)) return false;

    // ② idle profiles of the parts the editor does not run yet (vehicle visual, effects, the demo's own clouds)
    const std::pair<const char*, std::shared_ptr<DeviceRegion>> Profiles[] = {{"VehicleProfile.bin", Host_->VehicleRegion},
                                                                             {"EffectProfile.bin", Host_->EffectRegion},
                                                                             {"CloudProfile.bin", Host_->CloudParams},
                                                                             {"CloudShadowProfile.bin", Host_->CloudShadowParams}};
    for (const auto& [Name, Region] : Profiles)
    {
        std::vector<uint8_t> Bytes;
        if (!ReadArchive(Configuration.SceneArchive / Name, Bytes, OutDiagnostic)) return false;
        Exchange_->WriteRegion(Region, 0, Bytes.data(), std::min<VkDeviceSize>(Bytes.size(), Region->Capacity));
    }

    // ③ the PlanetaryAtmosphere environment on the same device and queue (the viewport keeps the demo's own sky without it)
    if (!Configuration.EnvironmentShaders.empty())
    {
        Environment_ = std::make_unique<VulkanComputeExchange>();
        std::string EnvironmentDiagnostic;
        if (Environment_->AttachDevice(Exchange_->QueryDeviceAccess(), &EnvironmentDiagnostic))
        {
            Overlay_ = std::make_unique<EnvironmentOverlay>(*Exchange_, *Environment_, Host_->Shaders);
            if (!Overlay_->Construct(Configuration.EnvironmentShaders, Configuration.CelestialMaps, &EnvironmentDiagnostic)) Overlay_.reset();
        }
        if (!Overlay_)
        {
            if (OutDiagnostic != nullptr) *OutDiagnostic = "environment unavailable: " + EnvironmentDiagnostic;
            return false;
        }
        if (Configuration.Journal) Configuration.Journal("bring-up ⑧ PlanetaryAtmosphere environment on the same queue");
    }
    PresentedView_ = Exchange_->ConstructView(Host_->CaptureTexture);
    return PresentedView_ != nullptr;
}

void SceneViewport::ResolveSky(const SceneEnvironment& S)
{
    // ① the level's sun drives the demo's own light (exhibit scale: 98 klx ≙ the demo's SolarIlluminance 12)
    Sky.SunEnabled       = S.Solar.Enabled;
    Sky.SunAutoPosition  = false;
    Sky.DayAnimate       = false;
    Sky.SunElevation     = S.Solar.ElevationDegrees;
    Sky.SunAzimuth       = S.Solar.AzimuthDegrees;
    Sky.SolarIlluminance = 12.0 * std::max(0.0f, S.Solar.IlluminanceKilolux) / 98.0;
    Sky.SunTemperature   = std::clamp(static_cast<double>(S.Solar.KelvinTemperature), 1000.0, 40000.0);
    Sky.SunRadius        = 0.5 * std::max(0.05f, S.Solar.AngularDiameterDeg);
    Sky.SunDiscEnabled   = S.Solar.DiscVisible;
    Sky.SunGlow          = std::max(0.0f, S.Solar.AureoleGain);
    Sky.Atmosphere.SunColourIsRgb = S.Solar.ColourSource == 1u;
    Sky.Atmosphere.SunTint        = {S.Solar.TintLinear[0], S.Solar.TintLinear[1], S.Solar.TintLinear[2]};

    // 📝 The day clock's date, place and time: the editor already wrote the solved sun into the record when time-linked,
    //    so the port keeps manual placement; its moons, planets and sidereal stars follow the same clock.
    Sky.SunTime     = std::clamp(static_cast<double>(S.Solar.LocalSolarHours), 0.0, 24.0);
    Sky.DayDuration = std::max(0.01, static_cast<double>(S.Solar.DayDurationHours));
    Sky.SkyDate     = S.Solar.CalendarDate;
    Sky.Latitude    = std::clamp(static_cast<double>(S.Solar.LatitudeDegrees), -90.0, 90.0);
    Sky.Longitude   = std::clamp(static_cast<double>(S.Solar.LongitudeDegrees), -180.0, 180.0);
    Sky.TimeZone    = std::clamp(static_cast<double>(S.Solar.UtcOffsetHours), -12.0, 14.0);

    // ② fog, aerial perspective and the flare come from the overlaid environment when it is present
    const bool Overlaid = static_cast<bool>(Overlay_);
    Sky.FlareEnabled  = false;
    Sky.FogEnabled    = !Overlaid && S.Fog.Enabled;
    Sky.FogDensity    = std::max(0.0f, S.Fog.BaseDensity);
    Sky.FogHeight     = 1.0 / std::max(1.0e-3f, S.Fog.HeightFalloff);
    Sky.AerialEnabled = !Overlaid && S.Atmosphere.Enabled && S.Atmosphere.AerialPerspective;

    // ③ the primary moon on the same sky path the environment draws it on; its phase follows the sun–moon geometry
    const Vector3 Moon = ResolveMoonDirection(S.Moons.PrimaryOrbitDegrees, Sky.Latitude);
    const Vector3 Sun  = ResolveSkyDirection(Sky.SunElevation, Sky.SunAzimuth);
    Sky.PrimaryMoon.MoonEnabled   = S.Moons.Enabled;
    Sky.PrimaryMoon.MoonAzimuth   = std::atan2(Moon[1], Moon[0]) * 180.0 / Pi;
    Sky.PrimaryMoon.MoonElevation = std::asin(std::clamp(Moon[2], -1.0, 1.0)) * 180.0 / Pi;
    Sky.PrimaryMoon.MoonPhase     = std::acos(std::clamp(DotVector(Sun, Moon), -1.0, 1.0)) / Pi * 14.765;

    // ④ stars and planets (each planet its own Scene Directory entry), atmosphere scales
    Sky.StarsEnabled      = S.Stars.Enabled;
    // 📝 The port's own night sky (NightStars, seen in reflections) from the same Stars record as the overlaid sky: the demo's
    //    Stars page keys, with the Milky Way on the overlay's scale (0.55 the demo's 1).
    Sky.StarBrightness    = std::max(0.0f, S.Stars.MagnitudeGain);
    Sky.StarLimit         = std::clamp(static_cast<double>(S.Stars.LimitingMagnitude), 0.0, 8.0);
    Sky.StarSize          = std::clamp(static_cast<double>(S.Stars.PointSize), 0.2, 3.0);
    Sky.StarRotation      = S.Stars.RotationDegrees;
    Sky.StarHalo          = std::clamp(static_cast<double>(S.Stars.HaloGain), 0.0, 2.0);
    Sky.MilkyWay          = std::max(0.0, static_cast<double>(S.Stars.MilkyWayLuminance) / 0.55);
    Sky.TwinkleEnabled    = S.Stars.ScintillationEnabled;
    Sky.TwinkleDepth      = std::clamp(static_cast<double>(S.Stars.AtmosphericScintillate), 0.0, 1.0);
    Sky.TwinkleRate       = std::clamp(static_cast<double>(S.Stars.ScintillationRateHz), 0.2, 3.0);
    Sky.PlanetsEnabled    = std::any_of(S.Planets.begin(), S.Planets.end(), [](const PlanetConfiguration& Planet) { return Planet.Enabled; });
    Sky.AtmosphereEnabled = S.Atmosphere.Enabled;
    Sky.Atmosphere.Rayleigh = std::max(0.0f, S.Atmosphere.RayleighScattering);
    Sky.Atmosphere.Mie      = std::max(0.0, (static_cast<double>(S.Atmosphere.MieTurbidity) - 1.0) / 1.4);
    Sky.Atmosphere.Ozone    = std::max(0.0f, S.Atmosphere.OzoneAbsorptionScale);
    Sky.Atmosphere.MieG     = std::clamp(static_cast<double>(S.Atmosphere.MieAnisotropyG), -0.95, 0.95);
    // 📝 The rest of the demo's Atmosphere page keys, as the port's own integrator reads them.
    Sky.Atmosphere.RayleighHeight     = std::clamp(static_cast<double>(S.Atmosphere.RayleighHeightMetres), 1000.0, 16000.0);
    Sky.Atmosphere.MieHeight          = std::clamp(static_cast<double>(S.Atmosphere.MieHeightMetres), 500.0, 8000.0);
    Sky.Atmosphere.GroundAlbedo       = {S.Atmosphere.GroundAlbedo[0], S.Atmosphere.GroundAlbedo[1], S.Atmosphere.GroundAlbedo[2]};
    Sky.Atmosphere.SkyTint            = {S.Atmosphere.SkyTint[0], S.Atmosphere.SkyTint[1], S.Atmosphere.SkyTint[2]};
    Sky.Atmosphere.HorizonGlow        = std::clamp(static_cast<double>(S.Atmosphere.HorizonGlow), 0.0, 3.0);
    Sky.Atmosphere.MultipleScattering = S.Atmosphere.MultiScatteringLut;
    Sky.Atmosphere.AirMass            = S.Atmosphere.AirMass;
    Sky.Atmosphere.TierSamples        = std::clamp(static_cast<double>(S.Atmosphere.TierSamples), 8.0, 64.0);
}

bool SceneViewport::Refresh(const ViewportCamera& Camera, const SceneEnvironment& Environment, const ViewportRendering& Rendering, double DeltaSeconds,
                            std::array<uint32_t, 2> ScreenExtent, std::string* OutDiagnostic)
{
    if (!Host_) return false;
    ScreenExtent = {std::max(64u, ScreenExtent[0]), std::max(64u, ScreenExtent[1])};
    // 📝 Render settings › Trace resolution: the resampling targets follow it as they follow the screen extent.
    const uint32_t TraceScale = Rendering.TraceScale != 0 ? std::clamp(Rendering.TraceScale, 1u, 4u) : std::max(1u, Configuration_.TraceScale);
    if (ScreenExtent != Host_->ScreenExtent || TraceScale != Host_->Settings.TraceScale)
    {
        // 📝 A borrowing owner may still sample the old presented image in flight: retire everything before replacing it.
        Exchange_->AwaitIdle();
        Host_->Settings.TraceScale = TraceScale;
        Host_->ReconstructScreenTargets(ScreenExtent);
        PresentedView_ = Exchange_->ConstructView(Host_->CaptureTexture);
    }
    ElapsedSeconds_ += std::max(0.0, DeltaSeconds);
    if (Rendering.AnalyticIllumination)
    {
        // 📝 The analytic illumination draws the loaded scene into the same presented image; the global illumination rests.
        if (!Analytic_)
        {
            auto Candidate = std::make_unique<AnalyticIllumination::AnalyticIlluminationSequence>(
                *Exchange_, Configuration_.GlobalIlluminationShaders.parent_path() / "AnalyticIllumination");
            if (!Candidate->Construct(Host_->Scene, Host_->VertexRegion, Host_->OccupantRegion, Host_->MaterialRegion, OutDiagnostic)) return false;
            Analytic_ = std::move(Candidate);
            if (Configuration_.Journal) Configuration_.Journal("analytic illumination up");
        }
        AnalyticIllumination::AnalyticCamera View;
        View.Position            = Camera.Position;
        View.Target              = Camera.Target;
        View.Up                  = Camera.Up;
        View.VerticalFieldOfView = Camera.VerticalFieldOfView;
        View.Near                = Camera.Near;
        AnalyticIllumination::AnalyticSun Sun;
        Sun.Enabled              = Environment.Solar.Enabled;
        Sun.ElevationDegrees     = Environment.Solar.ElevationDegrees;
        Sun.AzimuthDegrees       = Environment.Solar.AzimuthDegrees;
        Sun.IlluminanceKilolux   = Environment.Solar.IlluminanceKilolux;
        Sun.KelvinTemperature    = Environment.Solar.KelvinTemperature;
        Sun.AngularDiameter      = Environment.Solar.AngularDiameterDeg;
        Sun.ExposureCompensation = Rendering.ExposureCompensation;
        // 📝 The analytic illumination reads the occupant profiles too: relocated occupants are written before it draws.
        if (!Host_->RelocatedOccupants.empty() || !Host_->SettlingOccupants.empty()) Host_->SettleRelocatedOccupants();
        if (!Analytic_->Refresh(View, Sun, ScreenExtent, Host_->CaptureView, OutDiagnostic)) return false;
        PickCamera_ = Camera;
        PickExtent_ = ScreenExtent;
        return true;
    }
    ResolveSky(Environment);

    RefreshInputs Inputs;
    Inputs.Camera.Position            = Camera.Position;
    Inputs.Camera.Target              = Camera.Target;
    Inputs.Camera.Forward             = NormalizeVector(SubtractVector(Camera.Target, Camera.Position));
    Inputs.Camera.Up                  = Camera.Up;
    Inputs.Camera.VerticalFieldOfView = Camera.VerticalFieldOfView;
    Inputs.Camera.Aspect              = static_cast<double>(ScreenExtent[0]) / static_cast<double>(ScreenExtent[1]);
    Inputs.Camera.Near                = Camera.Near;
    // 📝 IlluminationHost.js: the cascades centre 0.45 of a cascade-0 span ahead of the camera.
    Inputs.Focus = MultiplyAddVector(Camera.Position, Inputs.Camera.Forward, 0.45 * Host_->Grade.ClipResolution * Host_->Grade.FinestVoxel);
    Inputs.Toggles                      = Rendering.Toggles;
    Inputs.Toggles.Indirect             = Rendering.GlobalIllumination;
    Inputs.Toggles.Antialias            = Rendering.TemporalAntiAliasing;
    Inputs.Toggles.ExposureCompensation = Rendering.ExposureCompensation;
    Inputs.Toggles.RayTracing           = Configuration_.RayTracing && Rendering.Toggles.RayTracing;
    Inputs.Toggles.Inspection           = std::min<uint32_t>(Rendering.Inspection, 14u);
    Inputs.Toggles.Refinements          = Rendering.Refinements;
    // 📝 The demo's Settings object is its toggles: the host skips the dispatches of what the toggles switch off.
    Host_->Settings.Indirect            = Rendering.GlobalIllumination;
    Host_->Settings.Reflection          = Inputs.Toggles.Reflection;
    Host_->Settings.VarianceFilter      = Inputs.Toggles.VarianceFilter;
    Host_->Settings.CardRadiosity       = Inputs.Toggles.CardRadiosity;
    Host_->Settings.CardTileBudget      = Rendering.CardTileBudget != 0 ? Rendering.CardTileBudget : Host_->Grade.TileBudget;
    const auto Differs = [](const VarianceSettings& Left, const VarianceSettings& Right) {
        return Left.MinimumWeight != Right.MinimumWeight || Left.TallyLimit != Right.TallyLimit || Left.WaveletIterations != Right.WaveletIterations ||
               Left.ColourSigma != Right.ColourSigma;
    };
    if (Host_->Diffuse && Differs(Host_->Diffuse->Settings, Rendering.DiffuseFilter)) Host_->Diffuse->ApplySettings(Rendering.DiffuseFilter);
    if (Host_->Glossy && Differs(Host_->Glossy->Settings, Rendering.GlossyFilter)) Host_->Glossy->ApplySettings(Rendering.GlossyFilter);
    // 📝 The levelspace key raises the takeover and lowers it again; an inspection mode that activated it keeps it, as in the demo.
    if (Host_->Surface && Host_->Surface->Virtualized)
    {
        VirtualizedGeometry::NestingProjection& Virtualized = *Host_->Surface->Virtualized;
        if (Rendering.VirtualizedGeometry != VirtualizedConfigured_) Virtualized.Enabled = Rendering.VirtualizedGeometry;
        VirtualizedConfigured_   = Rendering.VirtualizedGeometry;
        Virtualized.StreamSlots  = Rendering.VirtualizedStreamSlots;
    }
    // 📝 Idle inputs of the parts outside the port, as the demo writes them with no cabin and nothing burning; bit 0 of the
    //    presentation word carries only exposure/scale metadata.
    Inputs.External.ElapsedSeconds = ElapsedSeconds_;
    Inputs.External.PresentationControl        = (1u << 8) | (1000u << 20);
    Inputs.External.FireBlock      = {0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.46f, 0.14f, 14.0f};
    Inputs.Environment             = &Sky;
    Inputs.CelestialDelta          = std::max(0.0, DeltaSeconds);
    Inputs.LuminairesLive          = true;
    if (ToggleAdjustment) ToggleAdjustment(Inputs.Toggles);
    LastToggles_ = Inputs.Toggles;

    RefreshInterlude Interlude;
    if (Overlay_ && !Overlay_->PrepareInterlude(*Host_, Environment, Inputs.Camera, Sky.Latitude, Sky.Longitude, Configuration_.EnvironmentLighting,
                                                Interlude, OutDiagnostic))
    {
        return false;
    }
    // 📝 Inspection modes 6-14 show a buffer, not the scene (the demo presents 6, 8, 9 and 10-14 un-tonemapped): the environment
    //    keeps lighting the refresh but is not composited over them, as the demo, which has no overlay, shows them.
    if (Overlay_ && Inputs.Toggles.Inspection >= 6u)
    {
        Interlude.AfterRecombination = nullptr;
        Interlude.BeforeAntialias    = nullptr;
        Interlude.AntialiasSource    = nullptr;
    }
    if (!Host_->AdvanceRefresh(&Inputs, nullptr, nullptr, OutDiagnostic, &Interlude)) return false;
    PickCamera_ = Camera;
    PickExtent_ = ScreenExtent;
    return true;
}

ViewportStatistics SceneViewport::QueryStatistics() const noexcept
{
    ViewportStatistics Statistics;
    const RefreshToggles& Toggles = LastToggles_;
    Statistics.RayTracing        = Configuration_.RayTracing && Toggles.RayTracing;
    Statistics.MaxBounces        = Toggles.MaxBounces;
    Statistics.ReflectionBounces = Toggles.ReflectionBounces;
    Statistics.InitialCandidates = Toggles.InitialCandidates;
    if (Host_)
    {
        for (const auto& Run : Host_->Scene.DrawRuns)
            if (Run.FacetSet < Host_->Scene.FacetSets.size())
                Statistics.SceneTriangles += Host_->Scene.FacetSets[Run.FacetSet].VertexCount / 3 * Run.Count;
        Statistics.ScreenExtent   = Host_->ScreenExtent;
        Statistics.TraceExtent    = Host_->TraceExtent;
        Statistics.ClipResolution = Host_->Grade.ClipResolution;
        if (Host_->Cards) Statistics.SurfaceCards = static_cast<uint32_t>(Host_->Cards->Cards.size());
        if (Host_->Surface && Host_->Surface->Clusters) Statistics.GeometryClusters = Host_->Surface->Clusters->CandidateCount;
        if (Host_->RayTracing)
        {
            Statistics.TlasInstances = Host_->RayTracing->ActiveInstances;
            Statistics.TlasEntries   = Host_->RayTracing->UsedTlasEntries;
            Statistics.BlasTriangles = Host_->RayTracing->StaticTriCount;
        }
    }
    return Statistics;
}

VkImageView SceneViewport::QueryPresentedView() const noexcept
{
    return PresentedView_ ? PresentedView_->Handle : VK_NULL_HANDLE;
}

std::array<uint32_t, 2> SceneViewport::QueryScreenExtent() const noexcept
{
    return Host_ ? Host_->ScreenExtent : std::array<uint32_t, 2>{0, 0};
}

uint32_t SceneViewport::RelocateOccupants(const std::vector<OccupantPlacement>& Placements)
{
    return Host_ ? GlobalIllumination::RelocateOccupants(*Host_, Placements) : 0u;
}

Transform SceneViewport::ResolveViewProjection(const ViewportCamera& Camera, std::array<uint32_t, 2> ScreenExtent) noexcept
{
    const double Aspect = static_cast<double>(ScreenExtent[0]) / static_cast<double>(std::max(1u, ScreenExtent[1]));
    return MultiplyTransform(ConstructPerspectiveTransform(Camera.VerticalFieldOfView, Aspect, Camera.Near),
                             ConstructViewTransform(Camera.Position, Camera.Target, Camera.Up));
}

bool SceneViewport::PickOccupants(std::array<double, 2> Point, OccupantPick& Outcome, std::string* OutDiagnostic)
{
    if (!PickCamera_ || PickExtent_[0] == 0 || PickExtent_[1] == 0)
    {
        if (OutDiagnostic != nullptr) *OutDiagnostic = "no refreshed image to pick in";
        return false;
    }
    // 📝 The image is shown stretched over the stage: the point's pixel is its fraction of the image's extent.
    std::array<uint32_t, 2> Pixel{};
    for (size_t Axis = 0; Axis < 2; ++Axis)
    {
        const double Scaled = std::floor(std::clamp(Point[Axis], 0.0, 1.0) * PickExtent_[Axis]);
        Pixel[Axis]         = static_cast<uint32_t>(std::min(Scaled, static_cast<double>(PickExtent_[Axis] - 1u)));
    }
    return PickOccupants(*PickCamera_, PickExtent_, Pixel, Outcome, OutDiagnostic);
}

bool SceneViewport::PickOccupants(const ViewportCamera& Camera, std::array<uint32_t, 2> ScreenExtent, std::array<uint32_t, 2> Pixel,
                                  OccupantPick& Outcome, std::string* OutDiagnostic)
{
    if (!Host_ || !Host_->VertexRegion || !Host_->OccupantRegion)
    {
        if (OutDiagnostic != nullptr) *OutDiagnostic = "the scene viewport is not up";
        return false;
    }
    if (!Pick_)
    {
        try
        {
            auto Candidate = std::make_unique<PickProjection>(*Exchange_, Host_->Shaders);
            Candidate->Construct(Host_->OccupantRegion);
            Pick_ = std::move(Candidate);
        }
        catch (const std::exception& Failure)
        {
            if (OutDiagnostic != nullptr) *OutDiagnostic = std::string("GPU picking unavailable: ") + Failure.what();
            return false;
        }
    }
    // 📝 The occupant profiles stay as the last refresh wrote them (relocations wait for the next refresh's motion bookkeeping), so
    //    the pick sees what the image shows.
    return Pick_->Pick(Host_->Scene, Host_->VertexRegion, ResolveViewProjection(Camera, ScreenExtent), ScreenExtent, Pixel, Outcome, OutDiagnostic);
}

bool SceneViewport::ReadPresented(std::vector<uint8_t>& OutRgba)
{
    return Host_ && Exchange_->ReadTexture(Host_->CaptureTexture, 0, 0, OutRgba);
}

} // namespace Frontier::GlobalIllumination
