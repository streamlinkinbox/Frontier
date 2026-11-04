/*==========================================================================================================================================
                                                         ENVIRONMENTOVERLAY.CPP
==========================================================================================================================================*/
// 🧩 Draws the PlanetaryAtmosphere environment (atmosphere, sun, six-body moons, ringed planets, Hipparcos stars, 3D clouds, height
//    and froxel fog, lens flare) over the ported illumination refresh, on the same Vulkan device and queue.

#include "EnvironmentOverlay.h"

#include "Engine/PlanetaryAtmosphere/PlanetaryEnsemble.h"
#include "Engine/PhysicalDynamics/AtmosphericFlow.h"
#include "Engine/PlanetaryAtmosphere/CelestialAuthoring.h"
#include <unordered_map>
#include <unordered_set>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Frontier::GlobalIllumination
{

namespace
{

constexpr double Pi              = 3.141592653589793;
constexpr double DegreesToRadian = Pi / 180.0;
// 📝 Local sidereal angle the Hipparcos catalogue is baked for (DecodeHipparcosCatalogue defaults: J2000.0 at 0°, 0°).
constexpr double CatalogueSidereal = 280.46061837 * DegreesToRadian;
constexpr double SiderealRate      = 2.0 * Pi / 86164.0905;   // [rad/s]

using D = DescriptorCode;

uint64_t HashBytes(uint64_t Hash, const void* Bytes, size_t Count) noexcept
{
    const auto* Cursor = static_cast<const uint8_t*>(Bytes);
    for (size_t Index = 0; Index < Count; ++Index) Hash = (Hash ^ Cursor[Index]) * 1099511628211ull;
    return Hash;
}

template <typename Number> uint64_t HashScalar(uint64_t Hash, const Number& Scalar) noexcept
{
    return HashBytes(Hash, &Scalar, sizeof(Scalar));
}

Vec3 ToVec3(const std::array<float, 3>& Vector) noexcept
{
    return {Vector[0], Vector[1], Vector[2]};
}

SpatialVector ToSpatial(const std::array<float, 3>& Vector) noexcept
{
    return {Vector[0], Vector[1], Vector[2]};
}

// 📝 Rodrigues rotation of a direction about a unit axis.
Vector3 RotateAbout(const Vector3& Direction, const Vector3& Axis, double Angle) noexcept
{
    const double  Cosine = std::cos(Angle), Sine = std::sin(Angle);
    const Vector3 Cross  = CrossVector(Axis, Direction);
    const double  Along  = DotVector(Axis, Direction);
    return {Direction[0] * Cosine + Cross[0] * Sine + Axis[0] * Along * (1.0 - Cosine),
            Direction[1] * Cosine + Cross[1] * Sine + Axis[1] * Along * (1.0 - Cosine),
            Direction[2] * Cosine + Cross[2] * Sine + Axis[2] * Along * (1.0 - Cosine)};
}

// 📝 The level's atmosphere record on the environment's medium: its scattering scales on the Earth reference (turbidity 2.4 ≙
//    the reference aerosol load). The realm's sky and the sun's and moons' transmittance all come from it.
AtmosphereParameters ResolveAtmosphereParameters(const SceneEnvironment& S)
{
    AtmosphereParameters Air = MakeEarthAtmosphere();
    if (S.Atmosphere.Enabled)
    {
        const double Rayleigh = std::max(0.0, static_cast<double>(S.Atmosphere.RayleighScattering));
        Air.RayleighScattering = {Air.RayleighScattering.R * Rayleigh, Air.RayleighScattering.G * Rayleigh, Air.RayleighScattering.B * Rayleigh};
        const double Aerosol = std::max(0.0, (static_cast<double>(S.Atmosphere.MieTurbidity) - 1.0) / 1.4);
        Air.MieScattering *= Aerosol;
        Air.MieAbsorption *= Aerosol;
        Air.MiePhaseG = std::clamp(static_cast<double>(S.Atmosphere.MieAnisotropyG), -0.95, 0.95);
        const double Ozone = std::max(0.0, static_cast<double>(S.Atmosphere.OzoneAbsorptionScale));
        Air.OzoneAbsorption = {Air.OzoneAbsorption.R * Ozone, Air.OzoneAbsorption.G * Ozone, Air.OzoneAbsorption.B * Ozone};
        // 📝 The page's scale heights and ground albedo (the environment's scalar albedo: the RGB well's luminance).
        Air.RayleighScaleHeightKm = std::clamp(static_cast<double>(S.Atmosphere.RayleighHeightMetres), 1000.0, 16000.0) / 1000.0;
        Air.MieScaleHeightKm      = std::clamp(static_cast<double>(S.Atmosphere.MieHeightMetres), 500.0, 8000.0) / 1000.0;
        Air.GroundAlbedo          = std::clamp(0.2126 * S.Atmosphere.GroundAlbedo[0] + 0.7152 * S.Atmosphere.GroundAlbedo[1] +
                                                   0.0722 * S.Atmosphere.GroundAlbedo[2], 0.0, 1.0);
        Air.GroundRadiusKm = std::max(100.0, static_cast<double>(S.Atmosphere.PlanetRadiusKm));
        Air.TopRadiusKm    = Air.GroundRadiusKm + std::max(1.0, static_cast<double>(S.Atmosphere.AtmosphereThicknessKm));
    }
    else
    {
        Air.RayleighScattering = {0.0, 0.0, 0.0};
        Air.MieScattering      = 0.0;
        Air.MieAbsorption      = 0.0;
        Air.OzoneAbsorption    = {0.0, 0.0, 0.0};
    }
    return Air;
}

constexpr float ObserverAltitudeKm = 0.05f;   // [km] the realm's observer above the ground

// 📝 Transmittance of the environment's air from the observer toward a direction of the illumination world (z up).
Vector3 ResolveAirTransmittance(const AtmosphereParameters& Air, const Vector3& Direction)
{
    const SpectrumRgb T = TransmittanceToTop(Air, Air.GroundRadiusKm + ObserverAltitudeKm, std::clamp(Direction[2], -1.0, 1.0), 48);
    return {T.R, T.G, T.B};
}

// 📝 Planet records in the demo solver's order (Venus, Jupiter, Mars, Saturn): surface archetype and procedural seed.
constexpr std::array<PlanetarySurfaceArchetype, 4> PlanetArchetypes = {PlanetarySurfaceArchetype::OceanicTerrestrial,
                                                                       PlanetarySurfaceArchetype::BandedGasGiant,
                                                                       PlanetarySurfaceArchetype::RustyDesertWorld,
                                                                       PlanetarySurfaceArchetype::BandedGasGiant};

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                   SKY PLACEMENT
//------------------------------------------------------------------------------------------------------------------------

Vector3 ResolveSkyDirection(double ElevationDegrees, double AzimuthDegrees) noexcept
{
    const double Elevation = ElevationDegrees * DegreesToRadian, Azimuth = AzimuthDegrees * DegreesToRadian;
    return {std::cos(Elevation) * std::cos(Azimuth), std::cos(Elevation) * std::sin(Azimuth), std::sin(Elevation)};
}

Vector3 ResolveMoonDirection(double OrbitDegrees, double LatitudeDegrees) noexcept
{
    // 📝 Declination 0, hour angle = orbit − 90°: east horizon, culmination towards the equator, west horizon.
    const double θ = OrbitDegrees * DegreesToRadian, φ = LatitudeDegrees * DegreesToRadian;
    return {std::cos(θ), -std::sin(θ) * std::sin(φ), std::sin(θ) * std::cos(φ)};
}

std::array<float, 3> ProjectEnvironmentAxes(const Vector3& Vector) noexcept
{
    return {static_cast<float>(Vector[0]), static_cast<float>(Vector[2]), static_cast<float>(-Vector[1])};
}

double ResolveEnvironmentRadianceScale(const SceneEnvironment& Environment) noexcept
{
    const double Elevation = Environment.Solar.Enabled ? static_cast<double>(Environment.Solar.ElevationDegrees) : -90.0;
    const double Skylight  = std::clamp(std::exp((Elevation - 6.0) / 3.0), 0.002, 1.0);
    return EnvironmentRadianceScale / Skylight;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   OVERLAY
//------------------------------------------------------------------------------------------------------------------------

struct EnvironmentOverlay::PlanetaryStore
{
    struct Motion
    {
        std::array<double, 3> Offset{};
    };
    std::unordered_map<uint64_t, Motion> Motions;
    double PreviousTime = -1;
    double TwinkleClock = 0.0;    // [s] elapsed time × the twinkle rate, integrated refresh by refresh
    double TwinkleTime  = -1.0;   // [s] the elapsed time it was last advanced to

    PlanetarySubmissionSequence Sequence{};
    PlanetaryRealmEnsemble      Ensemble{};
    PlanetaryCameraView         View{};
    DeviceMemorySpan            SceneShadowFrame{};   // [-] the refresh's sun cascades for the fog (FogSceneShadow::Frame)
    DeviceImageArraySlot        SceneShadowDepth{};   // [-] the refresh's cascade depth array, borrowed
    FogSceneShadow              SceneShadow{};
};

EnvironmentOverlay::EnvironmentOverlay(DeviceExchange& Exchange, VulkanComputeExchange& Environment, ShaderInterchange& Shaders)
    : Exchange_(Exchange), Environment_(Environment), Shaders_(Shaders), Planetary_(std::make_unique<PlanetaryStore>())
{
}

const PlanetaryRealmEnsemble& EnvironmentOverlay::QueryEnsemble() const noexcept
{
    return Planetary_->Ensemble;
}

const PlanetaryCameraView& EnvironmentOverlay::QueryView() const noexcept
{
    return Planetary_->View;
}

bool EnvironmentOverlay::ReadOverlaidRadiance(std::vector<float>& OutLinearRgba)
{
    return Constructed_ && Planetary_->Sequence.ReadbackHdrFrame(Environment_, OutLinearRgba);
}

EnvironmentOverlay::~EnvironmentOverlay()
{
    // 📝 The borrowed views go first; the images belong to the environment exchange.
    ImportGroup_.reset();
    RadianceView_.reset();
    VisibilityTexture_.reset();
    RadianceTexture_.reset();
    LightingGroup_.reset();
    BorrowedSky_.reset();
    BorrowedCloudShadow_.reset();
    if (Constructed_)
    {
        Planetary_->Sequence.ConfigureFogSceneShadow(nullptr);
        Environment_.ReleaseMemorySpan(Planetary_->SceneShadowFrame);
        Planetary_->Sequence.Shutdown(Environment_);
    }
}

bool EnvironmentOverlay::Construct(const std::filesystem::path& EnvironmentShaders, const std::filesystem::path& CelestialMaps, std::string* OutDiagnostic)
{
    const std::filesystem::path Luna      = CelestialMaps / "MoonAlbedoSource2048.jpg";
    const std::filesystem::path Hipparcos = CelestialMaps / "Hipparcos8.csv";
    if (!Planetary_->Sequence.Initialize(Environment_, EnvironmentShaders.string(), Luna.string(), Hipparcos.string(), OutDiagnostic))
    {
        if (OutDiagnostic != nullptr && OutDiagnostic->empty()) *OutDiagnostic = "PlanetaryAtmosphere initialization failed";
        return false;
    }
    Constructed_ = true;
    const std::vector<DescriptorSlot> Slots = {{D::Uniform}, {D::Texture2D}, {D::TextureDepth2D},
                                               {D::StorageTexture2D, TextureFormat::Rgba32Float}, {D::StorageTexture2D, TextureFormat::Rgba32Float},
                                               {D::StorageTexture2D, TextureFormat::Rgba32Float}, {D::StorageTexture2D, TextureFormat::Rgba32Float},
                                               {D::Uniform}, {D::StorageTexture2D, TextureFormat::Rgba16Float}};
    ImportLayout_  = Exchange_.ConstructDescriptorLayout(Slots, "Environment visibility");
    ImportRegion_  = Exchange_.ConstructRegion(16, "Environment import profile");
    ImportProgram_  = Shaders_.ConstructComputeProgram("EnvironmentVisibility", "ProjectEnvironmentVisibility", {ImportLayout_});
    RestoreProgram_ = Shaders_.ConstructComputeProgram("EnvironmentVisibility", "RestoreEnvironmentRadiance", {ImportLayout_});
    if (!ImportProgram_ || !RestoreProgram_)
    {
        if (OutDiagnostic != nullptr) *OutDiagnostic = "environment visibility program: " + Shaders_.LastDiagnostic();
        return false;
    }
    // 📝 The environment-lighting bridge: profile, the environment's sky view and cloud shadow map, the refresh's two targets.
    const std::vector<DescriptorSlot> LightingSlots = {{D::Uniform}, {D::Texture2DLayered}, {D::Texture2DLayered}, {D::Sampler},
                                                       {D::StorageTexture2D, TextureFormat::Rgba16Float},
                                                       {D::StorageTexture2D, TextureFormat::Rgba16Float}};
    LightingLayout_     = Exchange_.ConstructDescriptorLayout(LightingSlots, "Environment lighting");
    LightingRegion_     = Exchange_.ConstructRegion(48, "Environment lighting profile");
    SkyProgram_         = Shaders_.ConstructComputeProgram("EnvironmentLighting", "ProjectEnvironmentSky", {LightingLayout_});
    CloudShadowProgram_ = Shaders_.ConstructComputeProgram("EnvironmentLighting", "ProjectCloudShadow", {LightingLayout_});
    SamplerSpecification Linear;
    Linear.MagFilter = Linear.MinFilter = FilterMode::Linear;
    LightingSampler_ = Exchange_.ConstructSampler(Linear);
    if (!LightingLayout_ || !LightingRegion_ || !SkyProgram_ || !CloudShadowProgram_ || !LightingSampler_)
    {
        if (OutDiagnostic != nullptr) *OutDiagnostic = "environment lighting program: " + Shaders_.LastDiagnostic();
        return false;
    }
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   ENVIRONMENT LIGHTING
//------------------------------------------------------------------------------------------------------------------------

void EnvironmentOverlay::AdjustLight(const IlluminationHost& Host, const SceneEnvironment& S, double Latitude, LightSolution& Light)
{
    const AtmosphereParameters Air  = ResolveAtmosphereParameters(S);
    const Vector3&             Tint = Host.Atmosphere->SolarTint;   // the level's colour temperature, as the demo derives it
    const double               E    = Host.Settings.SolarIlluminance;

    // ① the sun through the environment's air, replacing the demo's own 64-step air mass (same sun, same fade)
    //    (the Atmosphere page's "Apply air-mass attenuation" off: the sun arrives unattenuated, as the demo's AirMass false)
    const Vector3 Sun = S.Atmosphere.AirMass ? ResolveAirTransmittance(Air, Light.SunDirection) : Vector3{1.0, 1.0, 1.0};
    for (int Channel = 0; Channel < 3; ++Channel) Light.SolarIlluminance[Channel] = Sun[Channel] * Tint[Channel] * E * Light.SunVisible;
    if (Light.Source == "Sun") Light.Illuminance = Light.SolarIlluminance;

    // ② night (the demo's moon key light below −4°): Luna at the Moons record's phase and albedo, Ember lit by its elongation from
    //    the sun; the brighter body casts the shadows, both add their light
    if (Light.Source == "Moon")
    {
        const double Fade = !S.Solar.Enabled ? 1.0 : std::clamp((-static_cast<double>(S.Solar.ElevationDegrees) - 4.0) / 6.0, 0.0, 1.0);
        auto Moonlight = [&](const Vector3& Direction, double Lit, double Gain, const Vector3& Colour) {
            const double  Elevation = std::asin(std::clamp(Direction[2], -1.0, 1.0)) / DegreesToRadian;
            const double  Horizon   = std::clamp((Elevation + 0.5) / 2.0, 0.0, 1.0);
            const Vector3 Through   = ResolveAirTransmittance(Air, Direction);
            Vector3       Arrival{};
            for (int Channel = 0; Channel < 3; ++Channel) Arrival[Channel] = Colour[Channel] * Through[Channel] * Horizon * Fade * Lit * Gain;
            return Arrival;
        };
        const auto& Primary = S.Moons.Instances[0];
        const Vector3 Direction = ResolveSkyDirection(Primary.ElevationDegrees,Primary.AzimuthDegrees);
        const double Lit = .5 - .5 * std::cos(2 * Pi * Primary.PhaseDays / 29.53);
        const double Gain = S.Moons.Enabled && S.Moons.IlluminateScene && Primary.Visible ?
            std::clamp<double>(S.Moons.LightPower,0,5) * std::clamp<double>(Primary.Brightness,0,5) : 0;
        Light.Direction = Direction;
        Light.Illuminance = Moonlight(Direction,Lit,Gain,{.08,.10,.14});
    }
    AdjustedLight_ = Light;
}

bool EnvironmentOverlay::ReconstructLighting(IlluminationHost& Host, const SceneEnvironment& S)
{
    const DeviceImageArraySlot& Sky    = Planetary_->Sequence.AtmosphereLuts().SkyViewArray();
    const DeviceImageArraySlot& Shadow = Planetary_->Sequence.Clouds().CloudShadowArray();
    if (Sky.Image == VK_NULL_HANDLE || Shadow.Image == VK_NULL_HANDLE || Planetary_->Ensemble.Realms.empty()) return false;

    // ① borrow the environment's sky view and cloud shadow map (rebuilt only when the environment replaced an image)
    if (!LightingGroup_ || BorrowedImages_[0] != Sky.Image || BorrowedImages_[1] != Shadow.Image)
    {
        LightingGroup_.reset();
        BorrowedSky_.reset();
        BorrowedCloudShadow_.reset();
        auto Borrow = [this](const DeviceImageArraySlot& Slot, const char* Label) {
            TextureSpecification Specification;
            Specification.Label         = Label;
            Specification.Width         = Slot.Width;
            Specification.Height        = Slot.Height;
            Specification.DepthOrLayers = std::max(1u, Slot.ArrayLayers);
            Specification.Format        = TextureFormat::Rgba16Float;
            Specification.Usage         = TextureUsage::Storage | TextureUsage::Sampled;
            return Exchange_.WrapTexture(Slot.Image, Specification);
        };
        BorrowedSky_         = Borrow(Sky, "Environment sky view (borrowed)");
        BorrowedCloudShadow_ = Borrow(Shadow, "Environment cloud shadow map (borrowed)");
        if (!BorrowedSky_ || !BorrowedCloudShadow_) return false;
        TextureViewSpecification Layered;
        Layered.Dimension    = ViewDimension::PlanarLayered;
        Layered.DimensionSet = true;
        LightingGroup_ = Exchange_.ConstructDescriptorGroup(
            LightingLayout_, {LightingRegion_, Exchange_.ConstructView(BorrowedSky_, Layered), Exchange_.ConstructView(BorrowedCloudShadow_, Layered),
                              LightingSampler_, Host.Atmosphere->SkyView, Exchange_.ConstructView(Host.CloudShadowTexture)},
            "Environment lighting");
        if (!LightingGroup_) return false;
        BorrowedImages_ = {Sky.Image, Shadow.Image};
    }

    // ② the bridge profile: the environment's primary sun and planet, its scale, the cloud shadow square
    const PlanetaryRealm& R      = Planetary_->Ensemble.Realms[0];
    const bool            Sunlit = !R.Suns.empty();
    const std::array<float, 3> SunDirection = Sunlit ? R.Suns[0].DirectionUnit : std::array<float, 3>{0.0f, 1.0f, 0.0f};
    const bool  Shadowing = Sunlit && S.Clouds.Enabled && S.Clouds.CastGroundShadows && !R.CloudVolumes.empty();
    const float Profile[12] = {SunDirection[0], SunDirection[1], SunDirection[2], static_cast<float>(1.0 / std::max(RadianceScale_, 1.0e-9)),
                               static_cast<float>(R.Atmosphere.GroundRadiusKm), static_cast<float>(R.Atmosphere.GroundRadiusKm + R.ObserverAltitudeKm),
                               0.0f, 1.0f,
                               R.CloudShadowHalfSpanM, 0.0f, Shadowing ? 1.0f : 0.0f, 0.0f};
    Exchange_.WriteRegion(LightingRegion_, 0, Profile, sizeof(Profile));

    // ③ the demo's projected cloud shadow parameters (CloudShadowLayouts.slang) over that square, along this refresh's sun
    const float    Half     = R.CloudShadowHalfSpanM;
    const Vector3& Sun      = Host.LastLight.SunDirection;
    const float    Base     = Shadowing ? static_cast<float>(R.CloudVolumes[0].Slab.LowerHeightM) : 0.0f;
    const float    Top      = Shadowing ? static_cast<float>(R.CloudVolumes[0].Slab.TopHeightM) : 0.0f;
    const float    Shadows[12] = {-Half, -Half, 2.0f * Half, Shadowing ? 1.0f : 0.0f,
                                  static_cast<float>(Sun[0]), static_cast<float>(Sun[1]), static_cast<float>(Sun[2]), 1.0f,
                                  12.0f, 4.0f, Base, Top};
    Exchange_.WriteRegion(Host.CloudShadowParams, 0, Shadows, sizeof(Shadows));
    return true;
}

void EnvironmentOverlay::ConfigureFogShadow(const IlluminationHost& Host, const SceneEnvironment& S)
{
    // 📝 Sun shafts through the scene: the fog samples the refresh's own sun cascades (rendered by now) when the sun is the key
    //    light; at night the cascades follow the moon, and the environment's sun lies below the horizon anyway.
    PlanetaryStore& P      = *Planetary_;
    const bool      Shafts = S.Solar.Enabled && S.Solar.VolumetricLightShaft && S.Fog.Enabled && S.Fog.ReceiveRayShadow && Host.Shadow &&
                        Host.Shadow->DepthTexture && Host.Shadow->LayeredView && Host.Shadow->LastReceiverBytes.size() >= 56 * sizeof(float) &&
                        Host.LastLight.Source == "Sun";
    if (!Shafts)
    {
        P.Sequence.ConfigureFogSceneShadow(nullptr);
        return;
    }
    std::array<float, kFogSceneShadowFloat4Count * 4> Frame{};
    std::memcpy(Frame.data(), Host.Shadow->LastReceiverBytes.data(), 56 * sizeof(float));
    Frame[56] = 1.0f;      // enabled
    Frame[57] = 0.0015f;   // depth bias (light clip depth)
    if (!Environment_.UploadMemorySpan(P.SceneShadowFrame, Frame.data(), sizeof(Frame)))
    {
        P.Sequence.ConfigureFogSceneShadow(nullptr);
        return;
    }
    const TextureSpecification& Depth = Host.Shadow->DepthTexture->Specification;
    P.SceneShadowDepth = {Host.Shadow->DepthTexture->Image, VK_NULL_HANDLE, Host.Shadow->LayeredView->Handle, VK_FORMAT_D32_SFLOAT,
                          Depth.Width, Depth.Height, Depth.DepthOrLayers, VK_IMAGE_LAYOUT_GENERAL};
    P.SceneShadow = {&P.SceneShadowFrame, &P.SceneShadowDepth};
    P.Sequence.ConfigureFogSceneShadow(&P.SceneShadow);
}

bool EnvironmentOverlay::ReconstructTargets(IlluminationHost& Host, uint32_t Width, uint32_t Height)
{
    // 📝 Identity by extent and generation, not by handle: a released handle value may come back for the next image.
    const std::array<uint32_t, 2> Extent = {Width, Height};
    if (Extent == TargetExtent_ && Host.ScreenGeneration == ScreenGeneration_ && ImportGroup_) return true;
    // 📝 The borrowed views go before the environment replaces the images they describe.
    ImportGroup_.reset();
    RadianceView_.reset();
    VisibilityTexture_.reset();
    RadianceTexture_.reset();
    if (!Planetary_->Sequence.EnsureViewportTargets(Environment_, Width, Height)) return false;
    const DeviceImageArraySlot& Visibility = Planetary_->Sequence.Visibility().VisibilityArray();
    const DeviceImageArraySlot& Radiance   = Planetary_->Sequence.HdrTarget();

    // 📝 Borrow the environment's images: VisibilityArray (4 layers) is written per layer, HdrTarget is what present tone-maps.
    TextureSpecification Layers;
    Layers.Label         = "Environment visibility (borrowed)";
    Layers.Width         = Visibility.Width;
    Layers.Height        = Visibility.Height;
    Layers.DepthOrLayers = Visibility.ArrayLayers;
    Layers.Format        = TextureFormat::Rgba32Float;
    Layers.Usage         = TextureUsage::Storage | TextureUsage::Sampled;
    VisibilityTexture_   = Exchange_.WrapTexture(Visibility.Image, Layers);
    TextureSpecification Overlaid = Layers;
    Overlaid.Label         = "Environment radiance (borrowed)";
    Overlaid.Format        = TextureFormat::Rgba16Float;
    Overlaid.DepthOrLayers = 1;
    RadianceTexture_       = Exchange_.WrapTexture(Radiance.Image, Overlaid);
    if (!VisibilityTexture_ || !RadianceTexture_) return false;
    std::array<std::shared_ptr<DeviceTextureView>, 4> LayerViews;
    for (uint32_t Layer = 0; Layer < 4; ++Layer)
    {
        TextureViewSpecification Slice;
        Slice.Dimension    = ViewDimension::Planar;
        Slice.DimensionSet = true;
        Slice.BaseLayer    = Layer;
        Slice.LayerCount   = 1;
        LayerViews[Layer]  = Exchange_.ConstructView(VisibilityTexture_, Slice);
    }
    TextureViewSpecification Single;
    Single.Dimension    = ViewDimension::Planar;
    Single.DimensionSet = true;
    RadianceView_ = Exchange_.ConstructView(RadianceTexture_, Single);
    // 📝 The recombined refresh, before its temporal antialiasing: the environment is drawn with this refresh's jittered depth and
    //    rays (EnvironmentVisibility.slang reconstructs them through the jittered view projection), and the antialiasing then
    //    integrates the overlaid image with its history, as it integrates the demo's own sky.
    ImportGroup_ = Exchange_.ConstructDescriptorGroup(
        ImportLayout_, {Host.RefreshRegion, Host.Display->RecombinedView, Host.Surface->DepthView, LayerViews[0], LayerViews[1],
                        LayerViews[2], LayerViews[3], ImportRegion_, RadianceView_},
        "Environment visibility");
    TargetExtent_     = Extent;
    ScreenGeneration_ = Host.ScreenGeneration;
    return RadianceView_ && ImportGroup_;
}

void EnvironmentOverlay::ResolveRealm(const IlluminationHost& Host, const SceneEnvironment& S, const CameraPose& Camera, double Latitude, double Longitude)
{
    const double Sidereal = ResolveSiderealAngle(Host.LastCelestial.JulianDay, Longitude);
    PlanetaryRealmEnsemble& Ensemble = Planetary_->Ensemble;
    PlanetaryCameraView&    View     = Planetary_->View;
    Ensemble.Portals.clear();
    Ensemble.Realms.resize(1);
    PlanetaryRealm& R = Ensemble.Realms[0];
    R.Name = "SceneViewport";

    // ① atmosphere: the level's scattering scales on the Earth reference (turbidity 2.4 ≙ the reference aerosol load)
    const AtmosphereParameters Air = ResolveAtmosphereParameters(S);
    R.Atmosphere              = Air;
    R.AtmosphereSamples = std::clamp(S.Atmosphere.TierSamples,8u,64u);
    R.MultipleScattering = S.Atmosphere.MultiScatteringLut;
    R.ObserverAltitudeKm = static_cast<float>(std::clamp(Camera.Position[2] / 1000.0,.001,1000.0));
    R.GroundPlaneY            = 0.0f;
    R.EnableAerialPerspective = S.Atmosphere.Enabled && S.Atmosphere.AerialPerspective;
    R.TwilightStrength        = 1.15f * std::clamp(S.Atmosphere.HorizonGlow, 0.0f, 3.0f);   // the page's Horizon glow

    // ② the sun the illumination refresh just resolved, at the top of the atmosphere, in the environment's radiometric scale
    R.Suns.clear();
    if (S.Solar.Enabled)
    {
        RealmSolarSource Sun;
        Sun.DirectionUnit = ProjectEnvironmentAxes(Host.LastLight.SunDirection);
        const Vector3& Tint = Host.Atmosphere->SolarTint;
        const double   E    = Host.Settings.SolarIlluminance * RadianceScale_;
        Sun.DiscIrradiance  = {static_cast<float>(Tint[0] * E), static_cast<float>(Tint[1] * E), static_cast<float>(Tint[2] * E)};
        Sun.Disc.AngularRadiusRad   = 0.5 * std::max(0.05, static_cast<double>(S.Solar.AngularDiameterDeg)) * DegreesToRadian;
        Sun.EnableLensFlare         = S.Flare.Enabled;
        Sun.MasterFlareGain         = std::max(0.0f, S.Flare.Intensity);
        Sun.HaloRadiusNdc           = std::clamp(S.Flare.HaloRadius, 0.05f, 1.2f);
        Sun.ChromaticShift          = 0.012f * std::max(0.0f, S.Flare.ChromaticDispersion) / 0.18f;
        Sun.Flare.ApertureBlades    = std::clamp<uint32_t>(S.Flare.ApertureBladeCount, 3u, 12u);
        Sun.Flare.AnamorphicStrength = S.Flare.AnamorphicStreak ? 1.0 : 0.0;
        Sun.Flare.StarburstStrength  = S.Flare.DiffractionStarburst ? 1.0 : 0.0;
        R.Suns.push_back(Sun);
    }

    // 📝 Every authored slot reaches the native lunar atlas, including phase and artistic diameter.
    R.Moons.clear();
    if (S.Moons.Enabled)
    {
        for (auto Moon : S.Moons.Instances)
        {
            if (Moon.FollowSky)
            {
                Moon.AzimuthDegrees=float(Host.LastCelestial.Moon.Azimuth);
                Moon.ElevationDegrees=float(Host.LastCelestial.Moon.Elevation);
                Moon.PhaseDays=float(Host.LastCelestial.MoonPhase);
            }
            if (!Moon.Visible) continue;
            auto Descriptor = ResolveAuthoredMoon(Moon,static_cast<float>(2.9 / .82 * RadianceScale_));
            Descriptor.EarthshineStrength = .0022f * std::max(0.0f,S.Moons.EarthshineLuminance / .22f);
            R.Moons.push_back(Descriptor);
        }
    }

    // ④ Hipparcos stars and the planets of the demo's own orbital solver, turned with the sky
    const double Drift = S.ElapsedSeconds * std::max(0.0f, S.Stars.SiderealSpeed) * SiderealRate;
    const double φ     = Latitude * DegreesToRadian;
    const double Δ     = Sidereal + Drift - CatalogueSidereal + S.Stars.RotationDegrees * DegreesToRadian;
    R.StarBasisRow0          = {static_cast<float>(std::cos(Δ)), static_cast<float>(std::cos(φ) * std::sin(Δ)), static_cast<float>(std::sin(φ) * std::sin(Δ))};
    R.StarBasisRow1          = {0.0f, static_cast<float>(std::sin(φ)), static_cast<float>(-std::cos(φ))};
    // 📝 Stars and the Milky Way carry fixed radiance: scaled so that, under the refresh's night exposure (≤ 0.82), they read
    //    as under the environment's own night exposure (2.9) — 2.9 / 0.82 in environment units, then the refresh scale.
    const float Starlight    = static_cast<float>(2.9 / 0.82 * RadianceScale_);
    R.StarBrightnessScale    = S.Stars.Enabled ? std::max(0.0f, S.Stars.MagnitudeGain) * Starlight : 0.0f;
    R.MilkyWayIntensity      = S.Stars.Enabled ? std::max(0.0f, S.Stars.MilkyWayLuminance / 0.55f) * Starlight : 0.0f;
    // 📝 Twinkle depth is the peak brightness swing at the zenith (the sky deepens it toward the horizon). The clock advances
    //    at the twinkle rate, so changing the rate speeds or slows the twinkle without a jump in its phase; wrapped hourly so
    //    the shader's single-precision phase stays exact.
    R.ScintillationStrength  = S.Stars.ScintillationEnabled ? std::clamp(S.Stars.AtmosphericScintillate, 0.0f, 1.0f) : 0.0f;
    const double TwinkleRate = std::clamp(static_cast<double>(S.Stars.ScintillationRateHz), 0.2, 3.0);
    if (Planetary_->TwinkleTime >= 0.0 && S.ElapsedSeconds > Planetary_->TwinkleTime)
    {
        const double Advance     = std::min(S.ElapsedSeconds - Planetary_->TwinkleTime, 0.25) * TwinkleRate;   // [s] of twinkle
        Planetary_->TwinkleClock = std::fmod(Planetary_->TwinkleClock + Advance, 3600.0);
    }
    Planetary_->TwinkleTime = S.ElapsedSeconds;
    R.StarTwinkleClock       = static_cast<float>(Planetary_->TwinkleClock);
    // 📝 N(m) ≈ 10^(0.4·m + 1.3) stars brighter than magnitude m over the whole sky.
    R.StarLimitingMagnitude = std::clamp(S.Stars.LimitingMagnitude,0.0f,8.0f);
    R.StarPointSize = std::clamp(S.Stars.PointSize,.2f,3.0f);
    R.StarHalo = std::clamp(S.Stars.HaloGain,0.0f,2.0f);
    // 📝 The planets of the demo's own orbital solver, each from its Scene Directory entry (Venus, Jupiter, Mars, Saturn).
    R.Planets.clear();
    {
        const Vector3 Pole = {0.0, std::cos(φ), std::sin(φ)};
        for (const PlanetConfiguration& Planet : S.Planets)
        {
            const size_t Body = std::min<size_t>(Planet.Body, PlanetArchetypes.size() - 1);
            if (!Planet.Enabled || Body >= Host.LastCelestial.Planets.size()) continue;
            const auto Direction = Planet.FollowSky ? ToVec3(ProjectEnvironmentAxes(RotateAbout(Host.LastCelestial.Planets[Body].Direction,Pole,-Drift))) :
                ResolveCelestialDirection(Planet.AzimuthDegrees,Planet.ElevationDegrees);
            auto Descriptor = ResolveAuthoredPlanet(Planet,Direction);
            R.Planets.push_back(Descriptor);
        }
    }

    // 📝 Advection moves density coordinates, not the authored volume. Velocity changes never multiply a new speed
    //    by the entire scene age: each receiver retains an integrated displacement under its record token.
    if (Planetary_->PreviousTime > S.ElapsedSeconds) Planetary_->Motions.clear();
    const double Delta = Planetary_->PreviousTime < 0 ? 0 : std::clamp(S.ElapsedSeconds - Planetary_->PreviousTime, 0.0, 1.0);
    Planetary_->PreviousTime = S.ElapsedSeconds;
    ConfigureAtmosphericFlow(R.ForceField, S.Forces, S.Wind, S.ElapsedSeconds, S.ForceSources);
    std::unordered_set<uint64_t> Receivers;
    auto Advect = [&](uint64_t Token, const float Centre[3], bool Enabled, bool Follow, double Independent)
    {
        Receivers.insert(Token);
        auto& Motion = Planetary_->Motions[Token];
        auto Velocity = Follow ? EvaluateWorldFlow(R.ForceField, {Centre[0], Centre[1], Centre[2]}, S.Forces.IncludeTurbulence) :
            std::array<double,3>{Independent,0,0};
        for (size_t Axis = 0; Axis < 3; ++Axis)
        {
            if (!std::isfinite(Velocity[Axis])) Velocity[Axis] = 0;
            if (Enabled) Motion.Offset[Axis] += Velocity[Axis] * Delta;
        }
        return ToSpatial(ProjectEnvironmentAxes(Motion.Offset));
    };
    auto CloudProfile = [&](const CloudConfiguration& Settings, uint64_t Token)
    {
        CloudVolumeProfile Cloud;
        Cloud.Domain = Settings.LocalVolume ? CloudVolumeDomain::BoundedLocalBox : CloudVolumeDomain::PlanetarySlab;
        Cloud.HeightProfile = static_cast<TroposphericCloudProfile>(Settings.LocalVolume ?
            (Settings.LocalFormation == 0 ? 0 : Settings.LocalFormation == 2 ? 5 : 2) : std::min(5u, Settings.Formation));
        Cloud.Slab.LowerHeightM = std::max(50.0f, Settings.BaseAltitudeMetres);
        Cloud.Slab.TopHeightM = std::max(Cloud.Slab.LowerHeightM,
            std::min<double>(Settings.CeilingMetres, Cloud.Slab.LowerHeightM + std::max(0.0f, Settings.LayerThicknessMetres)));
        Cloud.Slab.Coverage = std::clamp(Settings.Coverage, 0.0f, 1.0f);
        Cloud.Slab.DensityScale = std::max(0.0f, Settings.ExtinctionDensity / 0.85f);
        Cloud.Slab.WindXMps = Cloud.Slab.WindYMps = 0;
        Cloud.Slab.PhaseG = std::clamp(Settings.PhaseAnisotropy, -0.8f, 0.8f);
        Cloud.SilverLiningWeight = .2 * std::clamp(Settings.SilverLiningPower / .72f, 0.0f, 2.0f);
        Cloud.PowderStrength = std::clamp(Settings.PowderResponse, 0.0f, 1.0f);
        Cloud.ScatteringAlbedo = 1 - std::clamp(Settings.LightAbsorption, 0.0f, 1.0f);
        Cloud.WorleyErosionStrength = std::clamp(Settings.EdgeErosion, 0.0f, 1.0f);
        Cloud.AnvilShaping = std::clamp(Settings.AnvilShaping, 0.0f, 1.0f);
        Cloud.BoundsShape = static_cast<VolumeShape>(std::min(3u, Settings.BoundsShape));
        Cloud.BoxCenterM = ToSpatial(ProjectEnvironmentAxes({Settings.CentreMetres[0],Settings.CentreMetres[1],Settings.CentreMetres[2]}));
        Cloud.BoxHalfExtentsM = {std::max(1.0f,Settings.HalfExtentsMetres[0]),std::max(1.0f,Settings.HalfExtentsMetres[2]),std::max(1.0f,Settings.HalfExtentsMetres[1])};
        Cloud.BoxEdgeFalloffM = std::max(.01f, Settings.EdgeSoftness) * std::min({Cloud.BoxHalfExtentsM.X,Cloud.BoxHalfExtentsM.Y,Cloud.BoxHalfExtentsM.Z});
        Cloud.FeatureScaleMetres = Settings.LocalVolume ? std::clamp<double>(Settings.LocalFeatureMetres,2,200) : std::clamp<double>(Settings.FeatureScale,.2,3) / .00115;
        Cloud.ViewSamples = std::clamp(Settings.LocalVolume ? (S.CloudToken ? S.Clouds.LocalSamples : Settings.LocalSamples) : Settings.SkySamples,8u,192u);
        Cloud.ShadowSamples = std::min(12u,Settings.LocalVolume && S.CloudToken ? S.Clouds.LightSamples : Settings.LightSamples);
        if (Settings.LocalVolume && S.CloudToken)
        {
            Cloud.ScatteringAlbedo = 1 - std::clamp(S.Clouds.LightAbsorption,0.0f,1.0f);
            Cloud.PowderStrength = std::clamp(S.Clouds.PowderResponse,0.0f,1.0f);
        }
        float Centre[3] = {Settings.CentreMetres[0],Settings.CentreMetres[1],Settings.CentreMetres[2]};
        if (!Settings.LocalVolume) { Centre[0] = 0; Centre[1] = 0; Centre[2] = static_cast<float>((Cloud.Slab.LowerHeightM + Cloud.Slab.TopHeightM) * .5); }
        Cloud.WindDisplacementM = Advect(Token,Centre,Settings.Enabled,Settings.FollowWind && S.Wind.AdvectClouds,0);
        Cloud.DisplacementReady = true;
        return Cloud;
    };
    R.CloudVolumes.clear();
    R.CloudWindSeconds = 0;
    const auto GlobalCloud = CloudProfile(S.Clouds,S.CloudToken);
    if (S.Clouds.Enabled) R.CloudVolumes.push_back(GlobalCloud);
    for (const auto& Local : S.LocalClouds)
    {
        const auto Volume = CloudProfile(Local.Parameters,Local.Token);
        if (Local.Parameters.Enabled) R.CloudVolumes.push_back(Volume);
    }

    // ⑥ analytic height fog, lit by the scene's own street and lamp-post luminaires
    R.HeightFog              = FogParameters{};
    R.HeightFog.ReferenceDensity = S.Fog.Enabled ? std::max(0.0f, S.Fog.BaseDensity) : 0.0;
    R.HeightFog.ReferenceHeightM = S.Fog.BaseAltitudeMetres;
    R.HeightFog.FalloffHeightM   = 1.0 / std::max(1.0e-3f, S.Fog.HeightFalloff);
    R.HeightFogPhaseG            = std::clamp(S.Fog.PhaseAnisotropy, -0.9f, 0.9f);
    R.LocalFogVolumes.clear();
    for (const auto& Local : S.LocalFogVolumes)
    {
        const auto& Settings = Local.Parameters;
        LocalFogVolumeProfile Volume;
        Volume.BoundsShape = static_cast<VolumeShape>(std::min(3u,Settings.BoundsShape));
        Volume.BoxCenterM = ToSpatial(ProjectEnvironmentAxes({Settings.CentreMetres[0],Settings.CentreMetres[1],Settings.CentreMetres[2]}));
        Volume.BoxHalfExtentsM = {std::max(.5f,Settings.HalfExtentsMetres[0]),std::max(.5f,Settings.HalfExtentsMetres[2]),std::max(.5f,Settings.HalfExtentsMetres[1])};
        Volume.BoxEdgeFalloffM = std::max(.02f,Settings.EdgeSoftness) * std::min({Volume.BoxHalfExtentsM.X,Volume.BoxHalfExtentsM.Y,Volume.BoxHalfExtentsM.Z});
        Volume.ExtinctionPerM = std::clamp(Settings.LocalDensity,0.0f,2.0f);
        Volume.ScatteringAlbedo = 1 - std::clamp(Settings.Absorption,0.0f,1.0f);
        Volume.ScatteringTint = {Settings.Albedo[0],Settings.Albedo[1],Settings.Albedo[2]};
        Volume.PhaseAsymmetryG = std::clamp(Settings.PhaseAnisotropy,0.0f,.95f);
        Volume.HeightFalloffM = Settings.DensityFalloff ? Volume.BoxHalfExtentsM.Y * 2 : 0;
        Volume.VerticalAxis = 1;
        Volume.NoiseScaleM = std::clamp(Settings.NoiseScaleMetres,.5f,30.0f);
        Volume.NoiseModulation = std::clamp(Settings.Heterogeneity,0.0f,1.0f);
        Volume.SelfShadow = Settings.SelfShadow;
        Volume.WindDisplacementM = Advect(Local.Token,Settings.CentreMetres,Settings.Enabled,Settings.FollowWind,Settings.IndependentWindMps);
        Volume.DisplacementReady = true;
        if (Settings.Enabled) R.LocalFogVolumes.push_back(Volume);
    }
    for (auto At = Planetary_->Motions.begin(); At != Planetary_->Motions.end();)
        if (!Receivers.contains(At->first)) At = Planetary_->Motions.erase(At); else ++At;
    R.Luminaires.clear();
    if ((S.Fog.Enabled && S.Fog.BaseDensity > 0.0f) || !R.LocalFogVolumes.empty())
    {
        for (const LuminaireProfile& Lamp : Host.Luminaires.Luminaires)
        {
            if (Lamp.Category != LuminaireCategory::StreetLamp && Lamp.Category != LuminaireCategory::Omni) continue;
            PhotometricFogLuminaire Fixture;
            Fixture.PositionM     = ToSpatial(ProjectEnvironmentAxes(Lamp.Position));
            Fixture.AxisUnit      = ToSpatial(ProjectEnvironmentAxes(Lamp.Direction));
            const double Peak     = Lamp.Intensity * RadianceScale_;
            Fixture.PeakIntensity = {Lamp.Colour[0] * Peak, Lamp.Colour[1] * Peak, Lamp.Colour[2] * Peak};
            Fixture.OuterConeCos  = Lamp.Category == LuminaireCategory::StreetLamp ? Lamp.Params[1] : -1.0;
            Fixture.InnerConeCos  = Lamp.Category == LuminaireCategory::StreetLamp ? Lamp.Params[0] : -1.0;
            Fixture.RangeMeters   = Lamp.Reach;
            R.Luminaires.push_back(Fixture);
        }
    }
    R.LightningChannels.clear();
    R.RadiantFacets.clear();
    R.SolidOccluders.clear();
    R.GlassPanes.clear();
    R.CustomStars.clear();

    // 📝 The LUTs rebake only when their inputs change: the atmosphere revision hashes the medium, the sky-view revision the
    //    medium and the sun.
    uint64_t Medium = 1469598103934665603ull;
    Medium = HashScalar(Medium, Air.GroundRadiusKm);
    Medium = HashScalar(Medium, Air.TopRadiusKm);
    Medium = HashScalar(Medium, Air.RayleighScattering);
    Medium = HashScalar(Medium, Air.MieScattering);
    Medium = HashScalar(Medium, Air.MieAbsorption);
    Medium = HashScalar(Medium, Air.MiePhaseG);
    Medium = HashScalar(Medium, Air.OzoneAbsorption);
    Medium = HashScalar(Medium, R.ObserverAltitudeKm);
    Medium = HashScalar(Medium, Air.RayleighScaleHeightKm);
    Medium = HashScalar(Medium, Air.MieScaleHeightKm);
    Medium = HashScalar(Medium, Air.GroundAlbedo);
    Medium = HashScalar(Medium,S.Atmosphere.BakeRequested);
    Medium = HashScalar(Medium,S.AtmosphereToken);
    Medium = HashScalar(Medium,R.AtmosphereSamples);
    Medium = HashScalar(Medium,R.MultipleScattering);
    uint64_t Sky = Medium;
    for (const RealmSolarSource& Sun : R.Suns)
    {
        Sky = HashScalar(Sky, Sun.DirectionUnit);
        Sky = HashScalar(Sky, Sun.DiscIrradiance);
    }
    Sky = HashScalar(Sky, R.TwilightStrength);
    R.AtmosphereRevision = Medium | 1ull;
    R.SkyViewRevision    = Sky | 1ull;

    // ⑦ the illumination camera, un-jittered, on the environment's axes
    const Vector3 Forward = NormalizeVector(SubtractVector(Camera.Target, Camera.Position));
    const Vector3 Right   = NormalizeVector(CrossVector(Forward, Camera.Up));
    const Vector3 Up      = CrossVector(Right, Forward);
    View.ActiveRealmIndex = 0;
    View.PositionMeters   = ProjectEnvironmentAxes(Camera.Position);
    View.RightUnit        = ProjectEnvironmentAxes(Right);
    View.UpUnit           = ProjectEnvironmentAxes(Up);
    View.ForwardUnit      = ProjectEnvironmentAxes(Forward);
    View.VerticalFovRad   = static_cast<float>(Camera.VerticalFieldOfView);
    View.ExposureScale    = 1.0f;
    View.ElapsedSeconds   = static_cast<float>(S.ElapsedSeconds);
    View.FrameIndex       = RefreshIndex_;
    View.ViewportWidth    = Host.ScreenExtent[0];
    View.ViewportHeight   = Host.ScreenExtent[1];
    View.CloudResolutionPercent = std::clamp(S.Clouds.ResolutionPercent,25u,100u);
    if (!S.CloudToken)
    {
        View.CloudResolutionPercent = 25;
        for (const auto& Local : S.LocalClouds) if (Local.Parameters.Enabled)
            View.CloudResolutionPercent = std::max(View.CloudResolutionPercent,std::clamp(Local.Parameters.ResolutionPercent,25u,100u));
    }
    View.FogMarchSamples = R.LocalFogVolumes.empty() ? 36 : 8;
    for (const auto& Local : S.LocalFogVolumes) if (Local.Parameters.Enabled)
        View.FogMarchSamples = std::max(View.FogMarchSamples,std::clamp(Local.Parameters.LocalSamples,8u,192u));
}

void EnvironmentOverlay::PublishEnvironmentReadout(const SceneEnvironment& Settings)
{
    const auto& Realm = Planetary_->Ensemble.Realms[0];
    auto Result = std::make_shared<EnvironmentRenderReadout>();
    Result->Ready = true;
    for (const auto& [Token, Motion] : Planetary_->Motions) Result->ReceiverMotion.push_back({Token,Motion.Offset});
    Result->Refresh = RefreshIndex_;
    Result->LocalClouds = static_cast<uint32_t>(std::count_if(Realm.CloudVolumes.begin(),Realm.CloudVolumes.end(),
        [](const CloudVolumeProfile& Volume) { return Volume.Domain == CloudVolumeDomain::BoundedLocalBox; }));
    Result->LocalFogVolumes = static_cast<uint32_t>(Realm.LocalFogVolumes.size());
    Result->CloudTraceExtent = Planetary_->Sequence.Clouds().QueryTraceExtent();
    Result->FogMarchSamples = Planetary_->View.FogMarchSamples;
    const auto& Eye = Planetary_->View.PositionMeters;
    Result->WindAtCamera = EvaluateWorldFlow(Realm.ForceField,{Eye[0],-Eye[2],Eye[1]},Settings.Forces.IncludeTurbulence);
    if (Settings.Forces.Enabled && Settings.Wind.Enabled) Result->Contributions.emplace_back("Wind · ambient flow + gust + shear + curl");
    if (Settings.Forces.Enabled)
    {
        for (const auto& Source : Realm.ForceField.ActiveSources())
            Result->Contributions.emplace_back(std::string(ResolveForceSourceDescriptor(Source.Category).Name) + " · triggered");
        for (const auto& Prepared : Realm.ForceField.AuthoredSources())
            if (Prepared.Contribution == ForceContribution::Flow && Prepared.Activation.Envelope > 1e-3)
                Result->Contributions.emplace_back(std::string(ResolveForceSourceDescriptor(Prepared.Source.Category).Name) + " · authored");
    }
    if (Settings.Forces.Visualize)
    {
        const uint32_t Edge = std::clamp(Settings.Forces.GridResolution,3u,8u);
        for (uint32_t Z = 0; Z < Edge; ++Z)
            for (uint32_t Y = 0; Y < Edge; ++Y)
                for (uint32_t X = 0; X < Edge; ++X)
                {
                    const uint32_t Coordinate[] = {X,Y,Z};
                    FieldVectorSample Sample;
                    for (size_t Axis = 0; Axis < 3; ++Axis)
                        Sample.Position[Axis] = Settings.Forces.CentreMetres[Axis] +
                            (double(Coordinate[Axis]) / (Edge - 1) * 2 - 1) * Settings.Forces.HalfExtentsMetres[Axis];
                    Sample.Velocity = EvaluateWorldFlow(Realm.ForceField,Sample.Position,Settings.Forces.IncludeTurbulence);
                    Result->Vectors.push_back(Sample);
                }
    }
    const bool NewRequest = Settings.Atmosphere.BakeRequested != LastBakeRequest_ || Settings.AtmosphereToken != LastBakeToken_;
    const bool MediumChanged = Realm.AtmosphereRevision != LastBakeMedium_;
    const bool SkyChanged = Realm.SkyViewRevision != LastBakeSky_ && Settings.ElapsedSeconds - LastBakeTime_ >= .5;
    if (!BakeReadout_ || !BakeReadout_->Ready || NewRequest || MediumChanged || SkyChanged)
    {
        auto Bake = std::make_shared<AtmosphereBakeReadout>();
        Bake->SourceRecord = Settings.AtmosphereToken;
        Bake->RequestSerial = Settings.Atmosphere.BakeRequested;
        Bake->Generation = BakeReadout_ ? BakeReadout_->Generation + 1 : 1;
        Bake->Inputs = Settings.Atmosphere;
        Bake->ObserverAltitudeKm = Realm.ObserverAltitudeKm;
        if (!Realm.Suns.empty()) Bake->SolarDirection = Realm.Suns[0].DirectionUnit;
        auto& Projection = Planetary_->Sequence.AtmosphereLuts();
        Bake->Ready = Projection.ReadbackTransmittanceLayer(Environment_,0,Bake->Transmittance) &&
            Projection.ReadbackMultiScatteringLayer(Environment_,0,Bake->MultipleScattering) &&
            Projection.ReadbackSkyViewLayer(Environment_,0,Bake->SkyView);
        Bake->Ready = Bake->Ready && Bake->Transmittance.size() == 256u * 64u * 4u &&
            Bake->MultipleScattering.size() == 32u * 32u * 4u && Bake->SkyView.size() == 192u * 108u * 4u;
        for (const auto* Pixels : {&Bake->Transmittance,&Bake->MultipleScattering,&Bake->SkyView})
            for (float Channel : *Pixels) if (!std::isfinite(Channel)) Bake->Ready = false;
        if (Bake->Ready)
        {
            LastBakeRequest_ = Bake->RequestSerial;
            LastBakeToken_ = Bake->SourceRecord;
            LastBakeMedium_ = Realm.AtmosphereRevision;
            LastBakeSky_ = Realm.SkyViewRevision;
            LastBakeTime_ = Settings.ElapsedSeconds;
            Bake->Diagnostic = "GPU bake/readback ready";
        }
        else Bake->Diagnostic = "GPU lookup readback failed; no baked snapshot acknowledged";
        BakeReadout_ = std::move(Bake);
    }
    Result->AtmosphereBake = BakeReadout_;
    Readout_ = std::move(Result);
}

bool EnvironmentOverlay::PrepareInterlude(IlluminationHost& Host, const SceneEnvironment& Authored, const CameraPose& Camera, double Latitude,
                                          double Longitude, bool LightScene, RefreshInterlude& OutInterlude, std::string* OutDiagnostic)
{
    SceneEnvironment Environment = Authored;
    const bool Requested = Authored.Atmosphere.BakeRequested != LastBakeRequest_ || Authored.AtmosphereToken != LastBakeToken_;
    if (Authored.Atmosphere.UseBaked && Authored.Atmosphere.BakedAvailable && !(Requested && Authored.Atmosphere.BakeRequested > 0))
        static_cast<AtmosphereOpticalConfiguration&>(Environment.Atmosphere) = Authored.Atmosphere.Baked;

    if (!Constructed_ || !ReconstructTargets(Host, Host.ScreenExtent[0], Host.ScreenExtent[1]))
    {
        if (OutDiagnostic != nullptr) *OutDiagnostic = "environment targets unavailable";
        return false;
    }
    // 📝 Written before the resolve submission the import pass belongs to (queue writes land ahead of it).
    RadianceScale_ = ResolveEnvironmentRadianceScale(Environment);
    const float Profile[4] = {static_cast<float>(RadianceScale_), 0.0f, 0.0f, 0.0f};
    Exchange_.WriteRegion(ImportRegion_, 0, Profile, sizeof(Profile));
    const uint32_t GroupsX = (Host.ScreenExtent[0] + 7) / 8, GroupsY = (Host.ScreenExtent[1] + 7) / 8;
    OutInterlude.AfterRecombination = [this, GroupsX, GroupsY](CommandSequence& Sequence, uint32_t) {
        Sequence.BindProgram(ImportProgram_);
        Sequence.BindGroup(0, ImportGroup_);
        Sequence.Dispatch(GroupsX, GroupsY);
    };
    OutInterlude.BeforeAntialias = [this, GroupsX, GroupsY](CommandSequence& Sequence, uint32_t) {
        Sequence.BindProgram(RestoreProgram_);
        Sequence.BindGroup(0, ImportGroup_);
        Sequence.Dispatch(GroupsX, GroupsY);
    };
    if (LightScene)
    {
        OutInterlude.AdjustLight = [this, &Host, Environment, Latitude](LightSolution& Light) { AdjustLight(Host, Environment, Latitude, Light); };
        OutInterlude.BeforeRecording = [this, &Host, Environment, Camera, Latitude, Longitude]() {
            // 📝 The light of this refresh is final: the realm, its sky and its cloud shadows first, so the refresh samples them.
            ResolveRealm(Host, Environment, Camera, Latitude, Longitude);
            return Planetary_->Sequence.ExecuteSkyAndShadows(Environment_, Planetary_->Ensemble, Planetary_->View) && ReconstructLighting(Host, Environment);
        };
        const TextureSpecification& Sky    = Host.Atmosphere->SkyTexture->Specification;
        const TextureSpecification& Clouds = Host.CloudShadowTexture->Specification;
        OutInterlude.AfterSkyView = [this, SkyGroups = std::array<uint32_t, 2>{(Sky.Width + 7) / 8, (Sky.Height + 7) / 8},
                                     CloudGroups = std::array<uint32_t, 2>{(Clouds.Width + 7) / 8, (Clouds.Height + 7) / 8}](CommandSequence& Sequence) {
            Sequence.BindProgram(SkyProgram_);
            Sequence.BindGroup(0, LightingGroup_);
            Sequence.Dispatch(SkyGroups[0], SkyGroups[1]);
            Sequence.BindProgram(CloudShadowProgram_);
            Sequence.BindGroup(0, LightingGroup_);
            Sequence.Dispatch(CloudGroups[0], CloudGroups[1]);
        };
    }
    OutInterlude.Execute = [this, &Host, Environment, Camera, Latitude, Longitude, LightScene]() {
        // 📝 Runs after the resolve submission: the light, celestial placement, luminaires and sun cascades of this refresh are final.
        if (!LightScene) ResolveRealm(Host, Environment, Camera, Latitude, Longitude);
        ConfigureFogShadow(Host, Environment);
        ++RefreshIndex_;
        if (!Planetary_->Sequence.ExecuteOverVisibility(Environment_, Planetary_->Ensemble, Planetary_->View)) return false;
        PublishEnvironmentReadout(Environment);
        return true;
    };
    OutInterlude.AntialiasSource = RadianceView_;
    return true;
}

} // namespace Frontier::GlobalIllumination
