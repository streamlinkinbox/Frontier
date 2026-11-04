/*==========================================================================================================================================
                                                          ILLUMINATIONHOST.CPP
==========================================================================================================================================*/
// 🧩 Bring-up of every illumination stage in the demo's order, then the refresh: invalidate, relight, gather, resolve, present.

#include "IlluminationHost.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace Frontier::GlobalIllumination
{

namespace
{
constexpr uint32_t RefreshConstantsBytes = 1072;    // [B] RefreshConstants (268 floats)
constexpr uint32_t VehicleProfileBytes   = 10448;   // [B] VehicleProfile
constexpr uint32_t EffectProfileBytes    = 1120;    // [B] EffectProfile
constexpr double   Pi                    = 3.141592653589793;

// 📝 The demo's EnvironmentSettings is one object: the keys AtmosphereIntegrator reads mirror the environment's own.
void SynchronizeAtmosphere(EnvironmentSettings& Environment)
{
    AtmosphereSettings& Air = Environment.Atmosphere;
    Air.SunEnabled        = Environment.SunEnabled;
    Air.SunTemperature    = Environment.SunTemperature;
    Air.MoonElevation     = Environment.PrimaryMoon.MoonElevation;
    Air.MoonAzimuth       = Environment.PrimaryMoon.MoonAzimuth;
    Air.MoonEnabled       = Environment.PrimaryMoon.MoonEnabled;
    Air.MoonBrightness    = Environment.PrimaryMoon.MoonBrightness;
    Air.MoonPhase         = Environment.PrimaryMoon.MoonPhase;
    Air.HorizonGlow       = Environment.HorizonGlow;
    Air.AtmosphereEnabled = Environment.AtmosphereEnabled;
}

// 📝 Trailing number of an occupant name ("Light pole head 7" → 7), −1 when absent.
int32_t ParseTrailingIndex(const std::string& Name)
{
    size_t Start = Name.size();
    while (Start > 0 && Name[Start - 1] >= '0' && Name[Start - 1] <= '9') --Start;
    return Start == Name.size() ? -1 : std::atoi(Name.c_str() + Start);
}
} // namespace

IlluminationHost::IlluminationHost(DeviceExchange& ExchangeInstance, std::filesystem::path SpirvFolder)
    : Exchange(ExchangeInstance), Shaders(ExchangeInstance, std::move(SpirvFolder))
{
}

bool IlluminationHost::Construct(const IlluminationAssets& Assets, const IlluminationSettings& Configuration, std::array<uint32_t, 2> Extent,
                                 std::string* OutDiagnostic)
{
    Settings = Configuration;
    Grade    = ResolveQualityGrade(Settings.Quality);
    if (Settings.CardTileBudget == 0) Settings.CardTileBudget = Grade.TileBudget;
    // 🔍 Each step names the submissions it makes, so a device fault reports where the bring-up stood.
    auto StepStart = std::chrono::steady_clock::now();
    auto Step = [&](const std::string& Finished, const std::string& Next) {
        const auto Now = std::chrono::steady_clock::now();
        if (Journal)
        {
            char Line[320];
            std::snprintf(Line, sizeof(Line), "bring-up %s (%.0f ms, %llu submissions so far)", Finished.c_str(),
                          std::chrono::duration<double, std::milli>(Now - StepStart).count(),
                          static_cast<unsigned long long>(Exchange.CompletedSubmissions()));
            Journal(Line);
        }
        StepStart = Now;
        Exchange.LabelSubmissions("bring-up: " + Next);
    };
    Exchange.LabelSubmissions("bring-up: facet space");
    if (!Scene.LoadFacetSpace(Assets.FacetSpace, OutDiagnostic)) return false;
    if (Assets.OmitVehicle)
    {
        // 📝 Inactive occupants are skipped by every step (draws, distance union, cards, TLAS), exactly like idle fragments.
        for (int32_t Index : {Scene.VehicleBody, Scene.VehicleWheels[0], Scene.VehicleWheels[1], Scene.VehicleWheels[2], Scene.VehicleWheels[3]})
            if (Index >= 0 && static_cast<size_t>(Index) < Scene.Occupants.size()) Scene.Occupants[Index].Inactive = true;
    }
    // 📝 Beyond the demo: the engine shapes after the archive's facet sets, their surfaces after its families, and the editor's reserve.
    std::vector<uint8_t> MaterialTable = Assets.MaterialProfiles;
    MaterialTable.resize(MaterialTable.size() / MaterialStride * MaterialStride);
    Scene.AppendShapeSets(Assets.Shapes, static_cast<uint32_t>(MaterialTable.size() / MaterialStride));
    for (const ShapeArchive& Shape : Assets.Shapes)
    {
        const auto* Channels = reinterpret_cast<const uint8_t*>(Shape.Surface.data());
        MaterialTable.insert(MaterialTable.end(), Channels, Channels + MaterialStride);
    }
    if (Assets.ReserveCapacity > 0) Scene.ConvertStressReserve(Assets.ReserveCapacity);
    MaterialFamilyCount = static_cast<uint32_t>(MaterialTable.size() / MaterialStride);
    ResolveLampPlacements();

    // ① records every stage shares
    const std::vector<uint8_t> VertexStream = Scene.EncodeVertexStream();
    VertexRegion    = Exchange.ConstructRegionFrom(VertexStream.data(), VertexStream.size(), "Facet vertices");
    // 📝 The family table, then room for the objects' material blocks (MaterialBlockBit).
    const VkDeviceSize MaterialBytes = static_cast<VkDeviceSize>(MaterialFamilyCount) * (1u + MaterialBlockCapacity) * MaterialStride;
    MaterialRegion  = Exchange.ConstructRegion(MaterialBytes, "Materials");
    Exchange.WriteRegion(MaterialRegion, 0, MaterialTable.data(), MaterialTable.size());
    RefreshRegion   = Exchange.ConstructRegion(RefreshConstantsBytes, "Refresh constants");
    LuminaireRegion = Exchange.ConstructRegion(LuminaireRegisterBytes, "Clustered luminaire profiles");
    OccupantRegion  = Exchange.ConstructRegion(static_cast<VkDeviceSize>(Scene.Occupants.size()) * OccupantStride, "Occupant profiles");

    Step("① facet space: " + std::to_string(Scene.Occupants.size()) + " occupants, " + std::to_string(Scene.FacetSets.size()) + " facet sets, " +
             std::to_string(VertexStream.size() >> 10) + " KiB of vertices (" + Settings.Quality + ")",
         "distance volumes");

    // ② per-occupant distance volumes
    Volumes = std::make_unique<OccupantDistanceSpace>(Exchange, Shaders);
    if (!Volumes->ConstructVolumes(Scene, Grade.VoxelScale, OutDiagnostic)) return false;
    Step("② distance volumes: " + std::to_string(Volumes->Volumes.size()) + " in " + std::to_string(Volumes->SpaceExtent[0]) + "x" +
             std::to_string(Volumes->SpaceExtent[1]) + "x" + std::to_string(Volumes->SpaceExtent[2]) + " texels",
         "surface cards and the ray tracing hierarchy");

    // ③ surface cards, then the occupant profiles that reference volumes and cards
    Cards = std::make_unique<SurfaceCardDepot>(Exchange, Shaders, SurfaceCardConfiguration{Grade.CardExtent, Grade.CardDensity});
    Cards->AllocateCards(Scene);
    const std::vector<uint8_t> Profiles = Scene.EncodeOccupantProfiles(&Volumes->Volumes, static_cast<uint32_t>(Scene.Occupants.size()));
    Exchange.WriteRegion(OccupantRegion, 0, Profiles.data(), Profiles.size());
    Scene.ConstructDrawRuns();

    // ③b two-level acceleration structure
    RayTracing = std::make_unique<RayTracingSpace>(Exchange);
    RayTracing->ConstructResources(Scene, MaterialRegion, LuminaireRegion);
    Shaders.ConfigureConstants(ConstructSpecializationConstants({Grade.Steps, Cards->GroundFirstCard, Cards->GroundStripCard,
                                                              static_cast<uint32_t>(Scene.GroundOccupant)}));

    Step("③ " + std::to_string(Scene.Occupants.size()) + " occupant profiles and the two-level ray tracing hierarchy", "scene distance field");

    // ④ the scene distance field
    Distance = std::make_unique<SceneDistanceSpace>(Exchange, Shaders,
                                                    SceneDistanceConfiguration{4, Grade.ClipResolution, Grade.FinestVoxel, 6.0, 0.2});
    Distance->ConstructResources(Volumes->TextureView, OccupantRegion);
    Scene.ConstructDistanceList();
    Distance->UploadDistanceList(Scene.DistanceList);
    Volumes->ConstructFragmentProgram();

    Step("④ scene distance field", "illumination stages");

    // ⑤ every remaining stage
    Atmosphere = std::make_unique<AtmosphereIntegrator>(Exchange, Shaders);
    Atmosphere->ConstructResources();
    Shadow = std::make_unique<ShadowProjection>(Exchange, Shaders, Grade.ShadowExtent);
    Shadow->ConstructResources(OccupantRegion);
    Cards->ConstructResources(RefreshRegion, OccupantRegion, MaterialRegion, Distance->MarchLayout, LuminaireRegion);
    {
        TextureSpecification Stars;
        Stars.Label  = "Native star catalogue cells + records";
        Stars.Width  = 1024;
        Stars.Height = std::max(1u, Assets.StarCatalogueRows);
        Stars.Format = TextureFormat::Rgba32Float;
        Stars.Usage  = TextureUsage::Sampled | TextureUsage::CopyTarget;
        StarCatalogueTexture = Exchange.ConstructTexture(Stars);
        if (!Assets.StarCatalogue.empty()) Exchange.WriteTexture(StarCatalogueTexture, 0, Assets.StarCatalogue.data(), Assets.StarCatalogue.size(), 1024, Stars.Height);
        TextureSpecification Moon;
        Moon.Label         = "Native six-body lunar atlas · 2K sRGB";
        Moon.Width         = 2048;
        Moon.Height        = 1024;
        Moon.DepthOrLayers = 6;
        Moon.Format        = TextureFormat::Rgba8UnormSrgb;
        Moon.Usage         = TextureUsage::Sampled | TextureUsage::CopyTarget;
        MoonAtlasTexture = Exchange.ConstructTexture(Moon);
        if (Assets.MoonAtlas.size() == static_cast<size_t>(2048) * 1024 * 4 * 6)
        {
            for (uint32_t Layer = 0; Layer < 6; ++Layer)
            {
                Exchange.WriteTexture(MoonAtlasTexture, Layer, Assets.MoonAtlas.data() + static_cast<size_t>(Layer) * 2048 * 1024 * 4,
                                      static_cast<size_t>(2048) * 1024 * 4, 2048, 1024);
            }
        }
    }
    TextureViewSpecification Layered;
    Layered.Dimension    = ViewDimension::PlanarLayered;
    Layered.DimensionSet = true;
    SamplingGroup = Cards->ConstructSamplingGroup(OccupantRegion, Atmosphere->SkyView, Atmosphere->SkySampler,
                                                  Exchange.ConstructView(MoonAtlasTexture, Layered), Exchange.ConstructView(StarCatalogueTexture));

    // 📝 Buffers of systems outside the port, at their idle content (tyre deposit, derez effect, vehicle visual, clouds).
    TextureSpecification Deposit;
    Deposit.Label  = "Rubber and rim scrape deposit";
    Deposit.Width  = 4096;
    Deposit.Height = 2340;
    Deposit.Format = TextureFormat::Rgba8Unorm;
    Deposit.Usage  = TextureUsage::Sampled | TextureUsage::CopyTarget;
    TyreMarkTexture = Exchange.ConstructTexture(Deposit);
    SamplerSpecification DepositSampling;
    DepositSampling.MagFilter = DepositSampling.MinFilter = FilterMode::Linear;
    auto DepositSampler = Exchange.ConstructSampler(DepositSampling);
    VehicleRegion     = Exchange.ConstructRegion(VehicleProfileBytes, "Vehicle record");
    EffectRegion      = Exchange.ConstructRegion(EffectProfileBytes, "Effect record");
    CloudShadowParams = Exchange.ConstructRegion(48, "Projected cloud shadows");
    CloudParams       = Exchange.ConstructRegion(544, "Cloud renderer v2");
    TextureSpecification CloudShadow;
    CloudShadow.Label  = "Cloud shadow optical depth";
    CloudShadow.Width  = 128;
    CloudShadow.Height = 128;
    CloudShadow.Usage  = TextureUsage::Storage | TextureUsage::Sampled;
    CloudShadowTexture = Exchange.ConstructTexture(CloudShadow);
    TextureSpecification CloudTarget;
    CloudTarget.Usage = TextureUsage::Storage | TextureUsage::Sampled;
    CloudTarget.Label = "Cloud optical integration";
    CloudColourTexture = Exchange.ConstructTexture(CloudTarget);
    CloudTarget.Label = "Cloud depth guide";
    CloudGuideTexture  = Exchange.ConstructTexture(CloudTarget);

    Shadow->ConstructMaskedProgram(OccupantRegion, EffectRegion, VehicleRegion);
    Surface = std::make_unique<SurfaceProjection>(Exchange, Shaders);
    Surface->ConstructPrograms(RefreshRegion, OccupantRegion, MaterialRegion, Distance->MarchLayout, Cards->SamplingLayout, RayTracing->MarchLayout,
                               Exchange.ConstructView(TyreMarkTexture), DepositSampler, EffectRegion, VehicleRegion);
    // 📝 I2: the cluster nestings stored beside the facet space at import (NestingArchiveExport); the editor never builds them.
    if (Surface->Virtualized) Surface->Virtualized->AssignArchive(std::filesystem::path(Assets.FacetSpace).replace_extension(".nestingspace"));
    Resampling = std::make_unique<ResamplingSequence>(Exchange, Shaders);
    Resampling->ConstructPrograms(RefreshRegion, MaterialRegion, Distance->MarchLayout, Cards->SamplingLayout, RayTracing->MarchLayout);
    Diffuse = std::make_unique<VarianceIntegrator>(Exchange, Shaders, "Diffuse", VarianceSettings{0.08f, 24, 3, 4.0f});
    Diffuse->ConstructPrograms(RefreshRegion);
    Glossy = std::make_unique<VarianceIntegrator>(Exchange, Shaders, "Glossy", VarianceSettings{0.12f, 12, 2, 3.0f});
    Glossy->ConstructPrograms(RefreshRegion);
    Display = std::make_unique<DisplayProjection>(Exchange, Shaders);
    Display->ConstructPrograms(RefreshRegion, MaterialRegion, OccupantRegion, TextureFormat::Rgba8UnormSrgb);
    Display->ConstructInspection(Distance->MarchLayout, Cards->SamplingLayout, RayTracing->MarchLayout);
    for (const auto& [Offset, Bytes] : Assets.MaterialRewrites)
    {
        Exchange.WriteRegion(MaterialRegion, Offset, Bytes.data(), Bytes.size());
    }

    Step("⑤ illumination stages and programs", "surface card capture");
    Cards->CaptureCards(Scene, VertexRegion);
    Step("⑥ surface card capture", "screen targets");
    ReconstructScreenTargets(Extent);
    Step("⑦ screen targets " + std::to_string(Extent[0]) + "x" + std::to_string(Extent[1]), "refresh");
    StagedRefreshes = 2;
    return true;
}

void IlluminationHost::ReconstructScreenTargets(std::array<uint32_t, 2> Extent)
{
    ++ScreenGeneration;
    ScreenExtent = Extent;
    TraceExtent  = {(Extent[0] + Settings.TraceScale - 1) / Settings.TraceScale, (Extent[1] + Settings.TraceScale - 1) / Settings.TraceScale};
    SamplerSpecification CloudSampling;
    CloudSampling.MagFilter = CloudSampling.MinFilter = FilterMode::Linear;
    SurfaceTargetInputs Inputs{Shadow->LayeredView, Shadow->ComparisonSampler, Shadow->ReceiverRegion, Atmosphere->SkyView, Atmosphere->SkySampler,
                               LuminaireRegion, CloudShadowParams, Exchange.ConstructView(CloudShadowTexture), Exchange.ConstructSampler(CloudSampling)};
    Surface->ConstructTargets(Extent[0], Extent[1], Inputs);
    Resampling->ConstructTargets(TraceExtent, *Surface);
    Diffuse->ConstructTargets(TraceExtent, ScreenExtent, *Surface, {Resampling->IrradianceView, Resampling->IrradianceView}, Resampling->GuideViews);
    Glossy->ConstructTargets(TraceExtent, ScreenExtent, *Surface, {Resampling->ReflectionView, Resampling->ReflectionView}, Resampling->GuideViews,
                             Resampling->MirrorView);
    auto CloudColour = Exchange.ConstructView(CloudColourTexture);
    DisplayNeutralResources Neutral{CloudParams, CloudColour, Exchange.ConstructView(CloudGuideTexture), CloudColour};
    Display->ConstructTargets(ScreenExtent, *Surface, *Resampling, *Diffuse, *Glossy, *Cards, Neutral);
    PriorValid = false;
    TextureSpecification Capture;
    Capture.Label       = "Capture";
    Capture.Width       = Extent[0];
    Capture.Height      = Extent[1];
    Capture.Format      = TextureFormat::Rgba8Unorm;
    Capture.Usage       = TextureUsage::RenderAttachment | TextureUsage::CopySource | TextureUsage::Sampled;
    Capture.ViewFormats = {TextureFormat::Rgba8UnormSrgb};
    CaptureTexture = Exchange.ConstructTexture(Capture);
    TextureViewSpecification SrgbView;
    SrgbView.Format    = TextureFormat::Rgba8UnormSrgb;
    SrgbView.FormatSet = true;
    CaptureView = Exchange.ConstructView(CaptureTexture, SrgbView);
}

void IlluminationHost::ResolveLampPlacements()
{
    // 📝 IlluminationHost.js filters ResolveLevelPlacements() by shape; each placement became the occupant of that name.
    std::vector<std::pair<int32_t, LampPlacement>> Poles, Lanterns;
    for (const OccupantEntry& Occupant : Scene.Occupants)
    {
        const bool Pole    = Occupant.Name.rfind("Light pole head ", 0) == 0;
        const bool Lantern = Occupant.Name.rfind("Point lamp lantern ", 0) == 0;
        if (!Pole && !Lantern) continue;
        const LampPlacement Placement{Occupant.Current[12], Occupant.Current[13], Occupant.Current[14], !Occupant.Inactive};
        (Pole ? Poles : Lanterns).emplace_back(ParseTrailingIndex(Occupant.Name), Placement);
    }
    auto Ordered = [](std::vector<std::pair<int32_t, LampPlacement>>& Entries) {
        std::stable_sort(Entries.begin(), Entries.end(), [](const auto& A, const auto& B) { return A.first < B.first; });
        std::vector<LampPlacement> Placements;
        for (const auto& Entry : Entries) Placements.push_back(Entry.second);
        return Placements;
    };
    StreetLampPoles = Ordered(Poles);
    PointLampPosts  = Ordered(Lanterns);
}

void IlluminationHost::SettleRelocatedOccupants()
{
    // 📝 Beyond the demo (OccupantRelocation): one profile write per occupant, after this refresh's dynamic profiles.
    auto WriteProfile = [this](uint32_t Index) {
        if (Index >= Scene.Occupants.size()) return;
        const std::vector<uint8_t> Profile = Scene.EncodeOccupantRange(&Volumes->Volumes, Index, 1u);
        Exchange.WriteRegion(OccupantRegion, static_cast<VkDeviceSize>(Index) * OccupantStride, Profile.data(), Profile.size());
    };
    for (const uint32_t Index : SettlingOccupants)
    {
        if (std::find(RelocatedOccupants.begin(), RelocatedOccupants.end(), Index) != RelocatedOccupants.end()) continue;
        Scene.RetainOccupantTransform(Index);
        WriteProfile(Index);
    }
    for (const uint32_t Index : RelocatedOccupants) WriteProfile(Index);
    SettlingOccupants = std::move(RelocatedOccupants);
    RelocatedOccupants.clear();
}

bool IlluminationHost::AssignReserveOccupant(uint32_t Index, const OccupantEntry& Template)
{
    if (!Scene.QueryReserved(Index) || Template.FacetSet >= Scene.FacetSets.size()) return false;
    Scene.AssignReserveTemplate(Index, Template);
    OccupantEntry& Occupant = Scene.Occupants[Index];
    // 📝 A shape without a distance volume cannot be unioned (only shapes some occupant unions at bring-up have one).
    if (Scene.FacetSets[Occupant.FacetSet].VolumeIndex < 0) Occupant.Participation &= ~OccupantParticipation::Distance;
    if (Cards)
    {
        const std::vector<uint32_t> Changed = Cards->ReassignOccupantCards(Scene, Index);
        // 📝 The capture reads the occupant's surface from its profile: write it ahead of the capture.
        const std::vector<uint8_t> Profile = Scene.EncodeOccupantRange(&Volumes->Volumes, Index, 1u);
        Exchange.WriteRegion(OccupantRegion, static_cast<VkDeviceSize>(Index) * OccupantStride, Profile.data(), Profile.size());
        Cards->RecaptureCards(Scene, VertexRegion, Changed);
        Cards->RebuildTileLists(Scene);
        Cards->WarmOccupants(Scene, {Index}, 3);
    }
    if (RayTracing && Index < RayTracing->OccupantBlasRoot.size())
        RayTracing->OccupantBlasRoot[Index] = RayTracing->FacetBlasRoots[Occupant.FacetSet];
    if (std::find(RelocatedOccupants.begin(), RelocatedOccupants.end(), Index) == RelocatedOccupants.end())
        RelocatedOccupants.push_back(Index);
    Scene.ConstructDrawRuns();
    return true;
}

void IlluminationHost::RecaptureOccupants(const std::vector<uint32_t>& Indices)
{
    if (!Cards || Indices.empty()) return;
    std::vector<uint32_t> CardIndices;
    for (const uint32_t Index : Indices)
    {
        if (Index >= Scene.Occupants.size()) continue;
        const OccupantEntry& Occupant = Scene.Occupants[Index];
        for (uint32_t Card = 0; Card < Occupant.CardCount; ++Card) CardIndices.push_back(Occupant.FirstCard + Card);
    }
    // 📝 The capture reads the occupant's material override from its profile: rewrite those first (the writes precede the capture).
    for (const uint32_t Index : Indices)
    {
        if (Index >= Scene.Occupants.size()) continue;
        const std::vector<uint8_t> Profile = Scene.EncodeOccupantRange(&Volumes->Volumes, Index, 1u);
        Exchange.WriteRegion(OccupantRegion, static_cast<VkDeviceSize>(Index) * OccupantStride, Profile.data(), Profile.size());
    }
    Cards->RecaptureCards(Scene, VertexRegion, CardIndices);
    Cards->WarmOccupants(Scene, Indices, 3);
}

bool IlluminationHost::WriteMaterialBlock(uint32_t Block, const std::vector<uint8_t>& Records)
{
    if (Block >= MaterialBlockCapacity || !MaterialRegion) return false;
    const size_t Bytes = std::min<size_t>(Records.size(), static_cast<size_t>(MaterialFamilyCount) * MaterialStride);
    Exchange.WriteRegion(MaterialRegion, static_cast<VkDeviceSize>(QueryMaterialBlockBase(Block)) * MaterialStride, Records.data(), Bytes);
    return true;
}

bool IlluminationHost::AdvanceRefresh(const RefreshInputs* Inputs, const RefreshWriteSupplier& Prepare, const RefreshWriteSupplier& Supplier,
                                      std::string* OutDiagnostic, const RefreshInterlude* Interlude)
{
    const uint32_t Parity = RefreshIndex & 1u;
    // ① scene motion (transforms of the dynamic occupants) is supplied before the host derives anything from it
    if (Prepare) Prepare(RefreshIndex);
    // ② celestial placement, light, refresh constants, the dynamic occupants' profiles, the luminaire table
    CelestialSolution Celestial{};
    EnvironmentSettings* Environment = Inputs != nullptr ? Inputs->Environment : nullptr;
    if (Environment != nullptr)
    {
        Celestial     = AdvanceCelestial(*Environment, Inputs->CelestialDelta);
        LastCelestial = Celestial;
        SynchronizeAtmosphere(*Environment);
        Settings.SunElevation     = Environment->SunElevation;
        Settings.SunAzimuth       = Environment->SunAzimuth;
        Settings.SolarIlluminance = Environment->SolarIlluminance;
        Settings.Atmosphere       = Environment->Atmosphere;
    }
    LightSolution Light = Atmosphere->ResolveLight(Settings.SunElevation, Settings.SunAzimuth, Settings.SolarIlluminance, Settings.Atmosphere);
    if (Interlude != nullptr && Interlude->AdjustLight)
    {
        Interlude->AdjustLight(Light);
        Atmosphere->Light = Light;
    }
    LastLight = Light;
    // 📝 A light that turns by more than 10° or changes brightness by more than 30 % relights every card and restarts history.
    const double Brightness = Light.Illuminance[0] + Light.Illuminance[1] + Light.Illuminance[2];
    if (PriorLight)
    {
        const double Cosine = Light.Direction[0] * PriorLight->first[0] + Light.Direction[1] * PriorLight->first[1] + Light.Direction[2] * PriorLight->first[2];
        const double Ratio  = std::abs(Brightness - PriorLight->second) / std::max({Brightness, PriorLight->second, 1.0e-4});
        if (Cosine < std::cos(10.0 * Pi / 180.0) || Ratio > 0.3)
        {
            Cards->InvalidateLighting();
            PriorValid = false;
        }
    }
    PriorLight = std::make_pair(Light.Direction, Brightness);
    if (Inputs != nullptr)
    {
        RefreshExternalInputs External = Inputs->External;
        if (Environment != nullptr)
        {
            EnvironmentLight Sky;
            Sky.SunDirection     = Light.SunDirection;
            Sky.SolarIlluminance = Light.SolarIlluminance;
            for (const HorizontalPlacement& Planet : Celestial.Planets) Sky.Planets.push_back(Planet.Direction);
            Sky.Sidereal   = ResolveSiderealAngle(Celestial.JulianDay, Environment->Longitude);
            Sky.PixelAngle = 2 * std::tan(Inputs->Camera.VerticalFieldOfView / 2) / ScreenExtent[1];
            External.EnvironmentBlock = EnvironmentEncoding.Encode(*Environment, Sky);
        }
        const std::vector<uint8_t> Constants = Encoder.Encode(Inputs->Camera, Light, Inputs->Toggles, External, ScreenExtent, TraceExtent,
                                                              Settings.TraceScale, RefreshIndex, Inputs->Focus, PriorValid);
        Exchange.WriteRegion(RefreshRegion, 0, Constants.data(), Constants.size());
        const std::vector<uint8_t> Dynamic = Scene.EncodeOccupantProfiles(&Volumes->Volumes, Scene.DynamicCount);
        Exchange.WriteRegion(OccupantRegion, 0, Dynamic.data(), Dynamic.size());
        if (!RelocatedOccupants.empty() || !SettlingOccupants.empty()) SettleRelocatedOccupants();
        if (Inputs->LuminairesLive)
        {
            const std::vector<uint8_t> Packed = Luminaires.Update(Inputs->VehicleTransform, StreetLampPoles, PointLampPosts);
            Exchange.WriteRegion(LuminaireRegion, 0, Packed.data(), Packed.size());
        }
    }
    // ③ invalidation and relighting schedules
    RayTracing->RefreshTlas(Scene, nullptr);
    if (Scene.ConstructDistanceList()) Distance->UploadDistanceList(Scene.DistanceList);
    if (Inputs != nullptr) Distance->ScheduleInvalidation(Inputs->Focus, ResolveMovedBounds(Scene, Distance->Frozen));
    Cards->ScheduleTiles(Settings.CardTileBudget);
    if (Inputs != nullptr) Shadow->ResolveCascades(Inputs->Camera, Light.Direction);
    if (Supplier) Supplier(RefreshIndex);
    if (Interlude != nullptr && Interlude->BeforeRecording && !Interlude->BeforeRecording())
    {
        if (OutDiagnostic != nullptr && OutDiagnostic->empty()) *OutDiagnostic = "outside light inputs failed";
        return false;
    }

    // ④ record, in the demo's order
    // 🔍 Staged: every step in a command stream of its own behind the same staged writes, each submitted and awaited on its
    //    own and named in a device fault report — the same work as one submission (the first refreshes after a bring-up, or
    //    FRONTIER_ILLUMINATION_STAGED_SUBMISSIONS=1).
    const bool Staged = StagedRefreshes > 0 || std::getenv("FRONTIER_ILLUMINATION_STAGED_SUBMISSIONS") != nullptr;
    if (StagedRefreshes > 0) --StagedRefreshes;
    std::vector<std::unique_ptr<CommandSequence>> Series;
    std::vector<std::string>                      Labels;
    auto Begin = [&](const char* Stage) {
        if (Series.empty() || Staged)
        {
            Series.push_back(std::make_unique<CommandSequence>(Exchange.BeginSequence()));
            Labels.push_back("refresh " + std::to_string(RefreshIndex) + ": " + Stage);
        }
        else
        {
            Labels.back() += std::string(", ") + Stage;
        }
        return std::ref(*Series.back());
    };
    auto SubmitSeries = [&]() {
        std::vector<CommandSequence*> Streams;
        for (const auto& Stream : Series) Streams.push_back(Stream.get());
        const bool Submitted = Exchange.SubmitSeries(Streams, Labels, OutDiagnostic);
        Series.clear();
        Labels.clear();
        return Submitted;
    };
    Atmosphere->RecordSkyView(Begin("sky view"), Settings.SolarIlluminance);
    if (Interlude != nullptr && Interlude->AfterSkyView) Interlude->AfterSkyView(Begin("environment light inputs"));
    Volumes->RecordFragments(Begin("fragment distances"));
    if (Inputs != nullptr)
    {
        Distance->RecordUnion(Begin("distance union"), Scene.QueryUnionCount(), RefreshIndex);
    }
    else
    {
        // 📝 Replay: the bricks and the distance constants were supplied; dispatch the union over them.
        const uint32_t BrickCount = static_cast<uint32_t>(Distance->RefreshBricks.size() / 4);
        CommandSequence& Sequence = Begin("distance union");
        if (BrickCount > 0)
        {
            Sequence.BindProgram(Distance->UnionProgram);
            Sequence.BindGroup(0, Distance->UnionGroup);
            Sequence.Dispatch(std::min(BrickCount, 65535u), (BrickCount + 65534u) / 65535u);
        }
    }
    Shadow->RecordCascades(Begin("shadow cascades"), Scene, VertexRegion);
    Cards->RecordLighting(Begin("card lighting"), Distance->MarchGroup, SamplingGroup, Settings.CardRadiosity);
    Surface->RecordSurface(Begin("surface raster"), Scene, VertexRegion);
    Surface->RecordDirect(Begin("direct radiance"), Distance->MarchGroup, SamplingGroup, RayTracing->MarchGroup);
    // 📝 Refinements beyond the demo (RefreshToggles::Refinements, Exhibits/RayTracedLighting): each records only when selected.
    const uint32_t Refinements = Inputs != nullptr ? Inputs->Toggles.Refinements : 0u;
    const bool     RayTraced   = Inputs != nullptr && Inputs->Toggles.RayTracing;
    if (RayTraced && Inputs->Toggles.RayTracedShadows && (Refinements & RefinementBit::PenumbraFilter) != 0u)
        Surface->RecordPenumbra(Begin("penumbra resolve"));
    if (Settings.Indirect)
    {
        Resampling->RecordResampling(Begin("ReSTIR resampling"), Parity, Distance->MarchGroup, SamplingGroup, RayTracing->MarchGroup);
        Diffuse->Record(Begin("diffuse variance filter"), Parity, Settings.VarianceFilter);
    }
    else if (Settings.Reflection)
    {
        // 📝 Beyond the demo: the reflections' variance filter needs this refresh's guide, which only the resampling wrote.
        Resampling->RecordGuide(Begin("resampling guide"), Parity);
    }
    if (Settings.Reflection)
    {
        Resampling->RecordReflection(Begin("reflection trace"), Distance->MarchGroup, SamplingGroup, RayTracing->MarchGroup);
        Glossy->Record(Begin("glossy variance filter"), Parity, Settings.VarianceFilter);
        if ((Refinements & RefinementBit::MirrorFilter) != 0u)
        {
            if (RayTraced && Inputs->Toggles.RayTracedReflections && (Refinements & RefinementBit::MirrorTrace) != 0u)
                Resampling->RecordMirrorTrace(Begin("mirror trace"), Distance->MarchGroup, SamplingGroup, RayTracing->MarchGroup);
            else
                Glossy->RecordMirrorProjection(Begin("mirror projection"), Parity);
        }
    }
    // 📝 Inspection mode 6: the demo's inspection march through the distance field (GTX) or the BVH (RTX) alone.
    if (Inputs != nullptr && Inputs->Toggles.Inspection == 6u)
        Display->RecordInspection(Begin("inspection march"), Distance->MarchGroup, SamplingGroup, RayTracing->MarchGroup);
    // 📝 Cluster inspection modes 10-14 come from the virtualized geometry's own targets; selecting one activates it on demand and,
    //    while activation is pending or no set is eligible, a checker fallback marks the inspection target instead of black.
    if (Inputs != nullptr && Inputs->Toggles.Inspection >= 10 && Surface->Virtualized)
        Surface->Virtualized->RecordInspectionView(Begin("cluster inspection"), Display->InspectionView, Inputs->Toggles.Inspection);
    else if (Surface->Virtualized)
        Surface->Virtualized->DismissInspectionStatus();
    if (Interlude == nullptr)
    {
        Display->RecordResolve(Begin("resolve"), Parity);
        Display->RecordPresent(Begin("present"), Parity, CaptureView);
        if (!SubmitSeries()) return false;
    }
    else
    {
        // 📝 Recombination → outside work on the same queue → its temporal antialiasing → present. The trailing dependency
        //    orders later submissions (the editor interface sampling the presented image) after the present raster.
        Display->RecordRecombination(Begin("recombination"));
        if (Interlude->AfterRecombination) Interlude->AfterRecombination(Begin("environment import"), Parity);
        if (!SubmitSeries()) return false;
        Exchange.LabelSubmissions("refresh " + std::to_string(RefreshIndex) + ": environment overlay");
        if (Interlude->Execute && !Interlude->Execute())
        {
            if (OutDiagnostic != nullptr && OutDiagnostic->empty()) *OutDiagnostic = "refresh interlude failed";
            return false;
        }
        CommandSequence& Presentation = Begin("antialias and present");
        if (Interlude->BeforeAntialias) Interlude->BeforeAntialias(Presentation, Parity);
        Display->RecordAntialias(Presentation, Parity, Interlude->AntialiasSource);
        Display->RecordPresent(Presentation, Parity, CaptureView);
        Presentation.OrderAfterEarlierWork();
        if (!SubmitSeries()) return false;
    }
    PriorValid = true;
    ++RefreshIndex;
    return true;
}

} // namespace Frontier::GlobalIllumination
