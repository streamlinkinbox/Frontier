/*==========================================================================================================================================
                                                           ILLUMINATIONHOST.H
==========================================================================================================================================*/
// 🧩 Bring-up of every illumination stage in the demo's order, then the refresh: invalidate, relight, gather, resolve, present.

#pragma once

#include "AtmosphereIntegrator.h"
#include "DisplayProjection.h"
#include "EnvironmentEncoding.h"
#include "IlluminationConstants.h"
#include "LuminaireRegister.h"
#include "OccupantDistanceSpace.h"
#include "RefreshEncoding.h"
#include "RayTracingSpace.h"
#include "ResamplingSequence.h"
#include "SceneDistanceSpace.h"
#include "SceneStructure.h"
#include "ShadowProjection.h"
#include "SurfaceCardDepot.h"
#include "SurfaceProjection.h"

#include <filesystem>
#include <functional>

namespace Frontier::GlobalIllumination
{

// 📝 The demo's FeatureBits (IlluminationHost.js) and the defaults of its Settings.
namespace FeatureBit
{
constexpr uint32_t Indirect             = 1u;
constexpr uint32_t TemporalReuse        = 2u;
constexpr uint32_t SpatialReuse         = 4u;
constexpr uint32_t VisibilityReuse      = 8u;
constexpr uint32_t VarianceFilter       = 16u;
constexpr uint32_t Reflection           = 32u;
constexpr uint32_t CardRadiosity        = 64u;
constexpr uint32_t Antialias            = 128u;
constexpr uint32_t DirectShadow         = 256u;
constexpr uint32_t AutoExposure         = 512u;
constexpr uint32_t RadianceRefresh      = 1024u;
constexpr uint32_t TemporalRestart      = 2048u;
constexpr uint32_t RayTracing           = 8192u;
constexpr uint32_t RayTracedShadows     = 16384u;
constexpr uint32_t RussianRoulette      = 32768u;
constexpr uint32_t FireflyFilter        = 65536u;
constexpr uint32_t MultiBounceNee       = 131072u;
constexpr uint32_t RayTracedReflections = 262144u;
} // namespace FeatureBit

struct IlluminationSettings
{
    std::string Quality            = "high";
    bool        RayTracing         = true;    // [-] RTX ReSTIR path tracer (false = GTX distance-field GI)
    bool        Indirect           = true;
    bool        Reflection         = true;
    bool        VarianceFilter     = true;
    bool        CardRadiosity      = true;
    double      SunElevation       = 38.0;    // [°]
    double      SunAzimuth         = 145.0;   // [°]
    double      SolarIlluminance   = 12.0;    // [-]
    uint32_t    TraceScale         = 2;       // [-]
    uint32_t    CardTileBudget     = 0;       // [-] 0 = the quality grade's budget
    AtmosphereSettings Atmosphere{};
};

struct IlluminationAssets
{
    std::filesystem::path FacetSpace{};                 // [-] Scene.facetspace
    std::vector<uint8_t>  MaterialProfiles{};           // [B] encoded MaterialSpecification records
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> MaterialRewrites{};   // [B] offset + bytes (derez palette)
    std::vector<uint8_t>  MoonAtlas{};                  // [B] 2048×1024×6 sRGB texels, empty = black
    std::vector<uint8_t>  StarCatalogue{};              // [B] rgba32float texels, 1024 wide
    uint32_t              StarCatalogueRows = 1;
    bool                  OmitVehicle       = false;    // [-] deactivate the vehicle body and wheels before bring-up (no car)
    // 📝 Beyond the demo: the engine shapes join the scene as facet sets (their surfaces as families after Materials.bin's), and the
    //    stress pool becomes the editor's reserve of ReserveCapacity occupants (SceneStructure::ConvertStressReserve).
    std::vector<ShapeArchive> Shapes{};
    uint32_t                  ReserveCapacity = 0;          // [-] 0 keeps the demo's stress pool
};

// 📝 Material blocks per object (MaterialBlockBit): each a whole copy of the family table, after the table itself.
constexpr uint32_t MaterialBlockCapacity = 96;   // [-]
constexpr uint32_t MaterialStride        = 48;   // [B] MaterialSpecification

// 📝 Per-refresh writes not produced by the ported host modules (camera-, physics- and effect-driven buffers); a replay
//    supplies them from a capture, the editor from its own scene.
using RefreshWriteSupplier = std::function<void(uint32_t RefreshIndex)>;

// 📝 Live inputs of one refresh: the camera the editor (or a capture) resolved, its cascade focus, and the inputs owned
//    by systems outside the port. Without them the refresh is a pure device replay of supplied writes.
//    With Environment set the host runs the demo's own sky path: AdvanceCelestial → ResolveLight → EncodeEnvironment
//    (the environment's sun and atmosphere replace Settings.SunElevation/SunAzimuth/SolarIlluminance/Atmosphere).
//    With LuminairesLive set the host packs the clustered luminaire table (LightingSystem.Update) every refresh.
struct RefreshInputs
{
    CameraPose              Camera{};
    Vector3                  Focus{};
    RefreshToggles           Toggles{};
    RefreshExternalInputs    External{};
    EnvironmentSettings*     Environment    = nullptr;
    double                   CelestialDelta = 0.0;    // [s] elapsed since the previous refresh, for DayAnimate
    bool                     LuminairesLive = false;
    std::optional<Transform> VehicleTransform{};      // [-] body transform when a vehicle carries beams
};

// 📝 Outside work drawn into the refresh (the environment overlay): AfterRecombination records right after the recombination,
//    Execute runs once that submission is in, BeforeAntialias records ahead of the temporal antialiasing, and AntialiasSource
//    (optional) is the radiance the antialiasing integrates with its history instead of the recombined image. The overlay is
//    drawn with this refresh's jittered depth and rays, so it must be antialiased with the image it is drawn on.
struct RefreshInterlude
{
    std::function<void(CommandSequence&, uint32_t Parity)> AfterRecombination{};
    std::function<bool()>                                  Execute{};
    std::function<void(CommandSequence&, uint32_t Parity)> BeforeAntialias{};
    std::shared_ptr<DeviceTextureView>                     AntialiasSource{};

    // 📝 Outside light inputs (all optional; unset, the refresh keeps the demo's own): AdjustLight sees the resolved light before
    //    anything is derived from it, BeforeRecording runs once the refresh's writes are staged (outside work the refresh will
    //    sample), and AfterSkyView records right after the demo's sky view so it can overwrite the refresh's light inputs.
    std::function<void(LightSolution&)>                    AdjustLight{};
    std::function<bool()>                                  BeforeRecording{};
    std::function<void(CommandSequence&)>                  AfterSkyView{};
};

class IlluminationHost
{
  public:
    IlluminationHost(DeviceExchange& Exchange, std::filesystem::path SpirvFolder);

    bool Construct(const IlluminationAssets& Assets, const IlluminationSettings& Settings, std::array<uint32_t, 2> ScreenExtent,
                   std::string* OutDiagnostic = nullptr);
    void ReconstructScreenTargets(std::array<uint32_t, 2> ScreenExtent);

    // 📝 One refresh in the demo's recording order; the supplier runs after the host's own writes (later writes win).
    bool AdvanceRefresh(const RefreshInputs* Inputs, const RefreshWriteSupplier& Prepare, const RefreshWriteSupplier& Supplier,
                        std::string* OutDiagnostic = nullptr, const RefreshInterlude* Interlude = nullptr);

    // 📝 Street-lamp heads and lamp-post lanterns of the loaded scene, in placement order (LevelStructure.js); an inactive
    //    head or lantern keeps its slot unlit.
    void ResolveLampPlacements();

    // 📝 Writes the profiles of the relocated occupants: settles those relocated before the last refresh, then carries the newly
    //    relocated ones' motion for this refresh.
    void SettleRelocatedOccupants();

    // 📝 Beyond the demo. A reserve occupant takes a template occupant's facet set, participation, cards and surface: its cards are
    //    laid out and captured again, its ray tracing root follows and its profile is rewritten at the next refresh. Returns false
    //    for an occupant outside the reserve.
    bool AssignReserveOccupant(uint32_t Occupant, const OccupantEntry& Template);
    // 📝 Captures the cards of these occupants again (their surface changed) and relights them first.
    void RecaptureOccupants(const std::vector<uint32_t>& Occupants);
    // 📝 Writes material block Block (MaterialFamilyCount records of MaterialStride bytes); false past the capacity.
    bool WriteMaterialBlock(uint32_t Block, const std::vector<uint8_t>& Records);
    [[nodiscard]] uint32_t QueryMaterialBlockBase(uint32_t Block) const noexcept { return MaterialFamilyCount * (1u + Block); }
    // 🔍 One line per bring-up step with its duration (the editor's diagnostic journal); unset = silent.
    std::function<void(const std::string&)> Journal{};
    // 🔍 Refreshes still to submit step by step (a device fault then names its step); a bring-up sets two.
    uint32_t StagedRefreshes = 0;

    DeviceExchange&                     Exchange;
    ShaderInterchange                   Shaders;
    IlluminationSettings                Settings{};
    QualityGrade                        Grade{};
    SceneStructure                      Scene{};
    std::unique_ptr<OccupantDistanceSpace> Volumes{};
    std::unique_ptr<SurfaceCardDepot>   Cards{};
    std::unique_ptr<RayTracingSpace>    RayTracing{};
    std::unique_ptr<SceneDistanceSpace> Distance{};
    std::unique_ptr<AtmosphereIntegrator> Atmosphere{};
    std::unique_ptr<ShadowProjection>   Shadow{};
    std::unique_ptr<SurfaceProjection>  Surface{};
    std::unique_ptr<ResamplingSequence> Resampling{};
    std::unique_ptr<VarianceIntegrator> Diffuse{};
    std::unique_ptr<VarianceIntegrator> Glossy{};
    std::unique_ptr<DisplayProjection>  Display{};

    std::shared_ptr<DeviceRegion>       VertexRegion{}, MaterialRegion{}, RefreshRegion{}, LuminaireRegion{}, OccupantRegion{};
    std::shared_ptr<DeviceRegion>       VehicleRegion{}, EffectRegion{}, CloudShadowParams{}, CloudParams{};
    std::shared_ptr<DeviceTexture>      MoonAtlasTexture{}, StarCatalogueTexture{}, TyreMarkTexture{}, CloudShadowTexture{}, CloudColourTexture{},
                                        CloudGuideTexture{}, CaptureTexture{};
    std::shared_ptr<DeviceTextureView>  CaptureView{};
    std::shared_ptr<DescriptorGroup>    SamplingGroup{};
    std::array<uint32_t, 2>             ScreenExtent{0, 0};
    std::array<uint32_t, 2>             TraceExtent{0, 0};
    RefreshEncoder                      Encoder{};
    EnvironmentEncoder                  EnvironmentEncoding{};
    LuminaireRegister                      Luminaires{};
    std::vector<LampPlacement>          StreetLampPoles{}, PointLampPosts{};
    // 📝 Beyond the demo: occupants the editor relocated since the last refresh (OccupantRelocation), and those it relocated
    //    before the last refresh, whose previous transform catches up at the next one.
    std::vector<uint32_t>               RelocatedOccupants{}, SettlingOccupants{};
    LightSolution                          LastLight{};
    CelestialSolution                   LastCelestial{};
    std::optional<std::pair<Vector3, double>> PriorLight{};   // [-] direction, summed illuminance of the previous refresh
    uint32_t                            MaterialFamilyCount = 0;   // [-] Materials.bin's families and the engine shapes'
    uint32_t                            RefreshIndex = 0;
    uint32_t                            ScreenGeneration = 0;   // [-] advances whenever the screen targets are rebuilt
    bool                                PriorValid   = false;
};

} // namespace Frontier::GlobalIllumination
