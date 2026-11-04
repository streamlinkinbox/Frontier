/*==========================================================================================================================================
                                                           SCENEWORLD.CPP
==========================================================================================================================================*/
// 🧩 Dynamic scene world implementation with .levelspace container serialization and composable record simulation.

#include "Engine/Scene/SceneWorld.h"

#include "Engine/Scene/SceneDirectory.h"
#include "Engine/PhysicalDynamics/ForceSourceCatalogue.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <unordered_map>

namespace Frontier
{

namespace
{

float ReadFloat(const toml::table& Table, std::string_view Key, float Fallback)
{
    const auto Quantity = Table[Key].value<double>();
    if (Quantity && std::isfinite(*Quantity) && std::abs(*Quantity) <= std::numeric_limits<float>::max())
        return static_cast<float>(*Quantity);
    return Fallback;
}

uint32_t ReadUint32(const toml::table& Table, std::string_view Key, uint32_t Fallback)
{
    if (const auto IntVal = Table[Key].value<int64_t>())
    {
        return static_cast<uint32_t>(std::clamp<int64_t>(*IntVal, 0, std::numeric_limits<uint32_t>::max()));
    }
    return Fallback;
}

int ReadInt(const toml::table& Table, std::string_view Key, int Fallback)
{
    if (const auto IntVal = Table[Key].value<int64_t>())
    {
        if (*IntVal >= std::numeric_limits<int>::min() && *IntVal <= std::numeric_limits<int>::max())
            return static_cast<int>(*IntVal);
    }
    return Fallback;
}

bool ReadBool(const toml::table& Table, std::string_view Key, bool Fallback)
{
    if (const auto BoolVal = Table[Key].value<bool>())
    {
        return *BoolVal;
    }
    return Fallback;
}

void WriteFloat3(toml::table& OutTable, std::string_view Key, const float Values[3])
{
    toml::array Array;
    Array.push_back(static_cast<double>(Values[0]));
    Array.push_back(static_cast<double>(Values[1]));
    Array.push_back(static_cast<double>(Values[2]));
    OutTable.insert_or_assign(Key, std::move(Array));
}

void ReadFloat3(const toml::table& InTable, std::string_view Key, float OutValues[3])
{
    if (const toml::array* Array = InTable[Key].as_array())
    {
        for (size_t Index = 0; Index < 3 && Index < Array->size(); ++Index)
        {
            if (const auto Val = (*Array)[Index].value<double>(); Val && std::isfinite(*Val) && std::abs(*Val) <= std::numeric_limits<float>::max())
            {
                OutValues[Index] = static_cast<float>(*Val);
            }
            else if (const auto IntVal = (*Array)[Index].value<int64_t>())
            {
                OutValues[Index] = static_cast<float>(*IntVal);
            }
        }
    }
}

double ReadDouble(const toml::table& Table, std::string_view Key, double Fallback)
{
    if (const auto Quantity = Table[Key].value<double>(); Quantity && std::isfinite(*Quantity)) return *Quantity;
    if (const auto Whole = Table[Key].value<int64_t>()) return static_cast<double>(*Whole);
    return Fallback;
}

void ReadDoubles(const toml::table& Table, std::string_view Key, double* OutValues, size_t Count)
{
    if (const toml::array* Array = Table[Key].as_array())
    {
        for (size_t Index = 0; Index < Count && Index < Array->size(); ++Index)
        {
            if (const auto Value = (*Array)[Index].value<double>(); Value && std::isfinite(*Value)) OutValues[Index] = *Value;
            else if (const auto Whole = (*Array)[Index].value<int64_t>()) OutValues[Index] = static_cast<double>(*Whole);
        }
    }
}

void WriteDoubles(toml::table& Table, std::string_view Key, const double* Values, size_t Count)
{
    toml::array Array;
    for (size_t Index = 0; Index < Count; ++Index) Array.push_back(Values[Index]);
    Table.insert_or_assign(Key, std::move(Array));
}

// 📝 The Acts on list as receiver names ([] = every receiver).
toml::array WriteReceivers(uint32_t Receivers)
{
    toml::array Array;
    for (const ForceReceiverDescriptor& Receiver : EnumerateForceReceivers())
        if (Receivers & Receiver.Receiver) Array.push_back(std::string(Receiver.Identifier));
    return Array;
}

uint32_t ReadReceivers(const toml::table& Table, std::string_view Key, uint32_t Fallback)
{
    const toml::array* Array = Table[Key].as_array();
    if (!Array) return Fallback;
    uint32_t Receivers = 0;
    for (const toml::node& Entry : *Array)
        if (const auto Name = Entry.value<std::string>())
            for (const ForceReceiverDescriptor& Receiver : EnumerateForceReceivers())
                if (*Name == Receiver.Identifier) Receivers |= Receiver.Receiver;
    return Receivers;
}

template <typename Choice, size_t Count>
const char* NameChoice(Choice Value, const char* const (&Names)[Count])
{
    return Names[std::min<size_t>(static_cast<size_t>(Value), Count - 1)];
}

template <typename Choice, size_t Count>
Choice ReadChoice(const toml::table& Table, std::string_view Key, Choice Fallback, const char* const (&Names)[Count])
{
    if (const auto Name = Table[Key].value<std::string>())
        for (size_t Index = 0; Index < Count; ++Index)
            if (*Name == Names[Index]) return static_cast<Choice>(Index);
    return Fallback;
}

constexpr const char* ContributionNames[] = {"flow", "accelerate", "damp"};
constexpr const char* QuantityNames[]     = {"speed", "along_x", "along_z", "vertical", "vorticity", "divergence"};
constexpr const char* ProjectionNames[]   = {"map", "oblique"};

void WriteAtmosphereOptics(toml::table& Table, const AtmosphereOpticalConfiguration& Optical)
{
    Table.insert_or_assign("rayleigh",double(Optical.RayleighScattering));
    Table.insert_or_assign("mie_turbidity",double(Optical.MieTurbidity));
    Table.insert_or_assign("mie_g",double(Optical.MieAnisotropyG));
    Table.insert_or_assign("ozone",double(Optical.OzoneAbsorptionScale));
    Table.insert_or_assign("planet_radius_km",double(Optical.PlanetRadiusKm));
    Table.insert_or_assign("atmosphere_thickness_km",double(Optical.AtmosphereThicknessKm));
    Table.insert_or_assign("rayleigh_height_m",double(Optical.RayleighHeightMetres));
    Table.insert_or_assign("mie_height_m",double(Optical.MieHeightMetres));
    WriteFloat3(Table,"ground_albedo",Optical.GroundAlbedo);
    Table.insert_or_assign("horizon_glow",double(Optical.HorizonGlow));
    Table.insert_or_assign("multiple_scattering",Optical.MultiScatteringLut);
    Table.insert_or_assign("tier_samples",int64_t(Optical.TierSamples));
}

void ReadAtmosphereOptics(const toml::table& Table, AtmosphereOpticalConfiguration& Optical)
{
    Optical.RayleighScattering = ReadFloat(Table,"rayleigh",Optical.RayleighScattering);
    Optical.MieTurbidity = ReadFloat(Table,"mie_turbidity",Optical.MieTurbidity);
    Optical.MieAnisotropyG = ReadFloat(Table,"mie_g",Optical.MieAnisotropyG);
    Optical.OzoneAbsorptionScale = ReadFloat(Table,"ozone",Optical.OzoneAbsorptionScale);
    Optical.PlanetRadiusKm = ReadFloat(Table,"planet_radius_km",Optical.PlanetRadiusKm);
    Optical.AtmosphereThicknessKm = ReadFloat(Table,"atmosphere_thickness_km",Optical.AtmosphereThicknessKm);
    Optical.RayleighHeightMetres = ReadFloat(Table,"rayleigh_height_m",Optical.RayleighHeightMetres);
    Optical.MieHeightMetres = ReadFloat(Table,"mie_height_m",Optical.MieHeightMetres);
    ReadFloat3(Table,"ground_albedo",Optical.GroundAlbedo);
    Optical.HorizonGlow = ReadFloat(Table,"horizon_glow",Optical.HorizonGlow);
    Optical.MultiScatteringLut = ReadBool(Table,"multiple_scattering",Optical.MultiScatteringLut);
    Optical.TierSamples = ReadUint32(Table,"tier_samples",Optical.TierSamples);
}

void SerializeSpatialSubtable(toml::table& OutTable, const SpatialConfiguration& Spatial)
{
    WriteFloat3(OutTable, "position_m", Spatial.PositionMetres);
    WriteFloat3(OutTable, "orientation_deg", Spatial.OrientationDegrees);
    WriteFloat3(OutTable, "scale", Spatial.Scale);
}

void DeserializeSpatialSubtable(const toml::table& InTable, SpatialConfiguration& Spatial)
{
    ReadFloat3(InTable, "position_m", Spatial.PositionMetres);
    ReadFloat3(InTable, "orientation_deg", Spatial.OrientationDegrees);
    ReadFloat3(InTable, "scale", Spatial.Scale);
    Spatial.AdvanceRevision();
}

void SerializePhysicsSubtable(toml::table& OutTable, const PhysicsConfiguration& Physics)
{
    OutTable.insert_or_assign("physics_enabled", Physics.Enabled);
    OutTable.insert_or_assign("mass_kg", static_cast<double>(Physics.Mass.MassKilograms));
    OutTable.insert_or_assign("density_kg_m3", static_cast<double>(Physics.Mass.DensityKgPerCubicMetre));
    OutTable.insert_or_assign("volume_m3", static_cast<double>(Physics.Mass.VolumeCubicMetres));
    WriteFloat3(OutTable, "linear_velocity_mps", Physics.LinearVelocityMps);
    OutTable.insert_or_assign("restitution", static_cast<double>(Physics.Restitution));
    OutTable.insert_or_assign("static_friction", static_cast<double>(Physics.StaticFriction));
    OutTable.insert_or_assign("dynamic_friction", static_cast<double>(Physics.DynamicFriction));
    OutTable.insert_or_assign("linear_damping", static_cast<double>(Physics.LinearDamping));
    OutTable.insert_or_assign("dynamic_body", Physics.DynamicBody);
    OutTable.insert_or_assign("gravity_coupled", Physics.GravityCoupled);
}

void DeserializePhysicsSubtable(const toml::table& InTable, PhysicsConfiguration& Physics)
{
    Physics.Enabled                     = ReadBool(InTable, "physics_enabled", Physics.Enabled);
    Physics.Mass.MassKilograms          = ReadFloat(InTable, "mass_kg", Physics.Mass.MassKilograms);
    Physics.Mass.DensityKgPerCubicMetre = ReadFloat(InTable, "density_kg_m3", Physics.Mass.DensityKgPerCubicMetre);
    Physics.Mass.VolumeCubicMetres      = ReadFloat(InTable, "volume_m3", Physics.Mass.VolumeCubicMetres);
    ReadFloat3(InTable, "linear_velocity_mps", Physics.LinearVelocityMps);
    Physics.Restitution     = ReadFloat(InTable, "restitution", Physics.Restitution);
    Physics.StaticFriction  = ReadFloat(InTable, "static_friction", Physics.StaticFriction);
    Physics.DynamicFriction = ReadFloat(InTable, "dynamic_friction", Physics.DynamicFriction);
    Physics.LinearDamping   = ReadFloat(InTable, "linear_damping", Physics.LinearDamping);
    Physics.DynamicBody     = ReadBool(InTable, "dynamic_body", Physics.DynamicBody);
    Physics.GravityCoupled  = ReadBool(InTable, "gravity_coupled", Physics.GravityCoupled);
    Physics.AdvanceRevision();
}

void SerializeThermalSubtable(toml::table& OutTable, const ThermalConfiguration& Thermal)
{
    OutTable.insert_or_assign("thermal_enabled", Thermal.Enabled);
    OutTable.insert_or_assign("temperature_k", static_cast<double>(Thermal.TemperatureKelvin));
    OutTable.insert_or_assign("specific_heat_j_kg_k", static_cast<double>(Thermal.SpecificHeatJPerKgK));
    OutTable.insert_or_assign("conductivity_w_m_k", static_cast<double>(Thermal.ThermalConductivityWPerMK));
    OutTable.insert_or_assign("emissivity", static_cast<double>(Thermal.SurfaceEmissivity));
    OutTable.insert_or_assign("convection_w_m2_k", static_cast<double>(Thermal.ConvectionCoefficientWPerM2K));
    OutTable.insert_or_assign("conductive_exchange", Thermal.ConductiveExchange);
    OutTable.insert_or_assign("radiative_exchange", Thermal.RadiativeExchange);
}

void DeserializeThermalSubtable(const toml::table& InTable, ThermalConfiguration& Thermal)
{
    Thermal.Enabled                      = ReadBool(InTable, "thermal_enabled", Thermal.Enabled);
    Thermal.TemperatureKelvin            = ReadFloat(InTable, "temperature_k", Thermal.TemperatureKelvin);
    Thermal.SpecificHeatJPerKgK          = ReadFloat(InTable, "specific_heat_j_kg_k", Thermal.SpecificHeatJPerKgK);
    Thermal.ThermalConductivityWPerMK    = ReadFloat(InTable, "conductivity_w_m_k", Thermal.ThermalConductivityWPerMK);
    Thermal.SurfaceEmissivity            = ReadFloat(InTable, "emissivity", Thermal.SurfaceEmissivity);
    Thermal.ConvectionCoefficientWPerM2K = ReadFloat(InTable, "convection_w_m2_k", Thermal.ConvectionCoefficientWPerM2K);
    Thermal.ConductiveExchange           = ReadBool(InTable, "conductive_exchange", Thermal.ConductiveExchange);
    Thermal.RadiativeExchange            = ReadBool(InTable, "radiative_exchange", Thermal.RadiativeExchange);
    Thermal.AdvanceRevision();
}

void SerializeChemicalSubtable(toml::table& OutTable, const ChemicalConfiguration& Chemical)
{
    OutTable.insert_or_assign("chemical_enabled", Chemical.Enabled);
    OutTable.insert_or_assign("molar_mass_g_mol", static_cast<double>(Chemical.MolarMassGramsPerMole));
    OutTable.insert_or_assign("amount_mol", static_cast<double>(Chemical.AmountMoles));
    OutTable.insert_or_assign("concentration_mol_l", static_cast<double>(Chemical.ConcentrationMolPerLitre));
    OutTable.insert_or_assign("activation_energy_kj_mol", static_cast<double>(Chemical.ActivationEnergyKjPerMol));
    OutTable.insert_or_assign("enthalpy_delta_kj_mol", static_cast<double>(Chemical.EnthalpyDeltaKjPerMol));
    OutTable.insert_or_assign("matter_phase", static_cast<int64_t>(Chemical.MatterPhaseIndex));
    OutTable.insert_or_assign("reversible", Chemical.ReversibleEquilibrium);
    OutTable.insert_or_assign("catalyzed", Chemical.CatalyzedPathway);
}

void DeserializeChemicalSubtable(const toml::table& InTable, ChemicalConfiguration& Chemical)
{
    Chemical.Enabled                  = ReadBool(InTable, "chemical_enabled", Chemical.Enabled);
    Chemical.MolarMassGramsPerMole    = ReadFloat(InTable, "molar_mass_g_mol", Chemical.MolarMassGramsPerMole);
    Chemical.AmountMoles              = ReadFloat(InTable, "amount_mol", Chemical.AmountMoles);
    Chemical.ConcentrationMolPerLitre = ReadFloat(InTable, "concentration_mol_l", Chemical.ConcentrationMolPerLitre);
    Chemical.ActivationEnergyKjPerMol = ReadFloat(InTable, "activation_energy_kj_mol", Chemical.ActivationEnergyKjPerMol);
    Chemical.EnthalpyDeltaKjPerMol    = ReadFloat(InTable, "enthalpy_delta_kj_mol", Chemical.EnthalpyDeltaKjPerMol);
    Chemical.MatterPhaseIndex         = std::clamp(ReadInt(InTable, "matter_phase", Chemical.MatterPhaseIndex), 0, 3);
    Chemical.ReversibleEquilibrium    = ReadBool(InTable, "reversible", Chemical.ReversibleEquilibrium);
    Chemical.CatalyzedPathway         = ReadBool(InTable, "catalyzed", Chemical.CatalyzedPathway);
    Chemical.AdvanceRevision();
}

void SerializeOpticsSubtable(toml::table& OutTable, const SurfaceOpticsConfiguration& Optics)
{
    OutTable.insert_or_assign("optics_enabled", Optics.Enabled);
    WriteFloat3(OutTable, "albedo_rgb", Optics.AlbedoRgb);
    OutTable.insert_or_assign("roughness", static_cast<double>(Optics.Roughness));
    OutTable.insert_or_assign("metallic", static_cast<double>(Optics.Metallic));
    OutTable.insert_or_assign("ior", static_cast<double>(Optics.IndexOfRefraction));
    OutTable.insert_or_assign("cast_shadows", Optics.CastShadows);
    OutTable.insert_or_assign("receive_gi", Optics.ReceiveGlobalIllumination);
}

void DeserializeOpticsSubtable(const toml::table& InTable, SurfaceOpticsConfiguration& Optics)
{
    Optics.Enabled = ReadBool(InTable, "optics_enabled", Optics.Enabled);
    ReadFloat3(InTable, "albedo_rgb", Optics.AlbedoRgb);
    Optics.Roughness                 = ReadFloat(InTable, "roughness", Optics.Roughness);
    Optics.Metallic                  = ReadFloat(InTable, "metallic", Optics.Metallic);
    Optics.IndexOfRefraction         = ReadFloat(InTable, "ior", Optics.IndexOfRefraction);
    Optics.CastShadows               = ReadBool(InTable, "cast_shadows", Optics.CastShadows);
    Optics.ReceiveGlobalIllumination = ReadBool(InTable, "receive_gi", Optics.ReceiveGlobalIllumination);
    Optics.AdvanceRevision();
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    SCENERECORDBASE
//------------------------------------------------------------------------------------------------------------------------

SceneRecordBase::SceneRecordBase(RecordClassification InClassification,
                                 std::string_view     InTypeIdentifier,
                                 std::string          InDisplayName)
    : RecordEntry(InClassification)
    , TypeIdentifier(InTypeIdentifier)
    , StoredName(std::move(InDisplayName))
{
#if FRONTIER_EDITOR
    if (!StoredName.empty())
    {
        SceneDirectory::Instance().ConfigureName(Token(), StoredName);
    }
#endif
}

void SceneRecordBase::AssignRecordName(std::string NewName)
{
    StoredName = std::move(NewName);
#if FRONTIER_EDITOR
    SceneDirectory::Instance().ConfigureName(Token(), StoredName);
#endif
}

void SceneRecordBase::AssignParentToken(RecordToken ParentToken)
{
    StoredParent = ParentToken;
#if FRONTIER_EDITOR
    SceneDirectory::Instance().ConfigureParent(Token(), ParentToken);
#endif
}

void SceneRecordBase::ConfigureEditorPresentation(const char* GlyphIdentifier,
                                                  const char* GroupName,
                                                  const char* SubtitleCategory,
                                                  const char* AccentHex)
{
    (void)GlyphIdentifier;
    (void)GroupName;
    (void)SubtitleCategory;
    (void)AccentHex;
#if FRONTIER_EDITOR
    SceneDirectoryMetadata Meta = SceneDirectory::Instance().QueryMetadata(Token());
    if (GlyphIdentifier != nullptr && GlyphIdentifier[0] != '\0')
    {
        Meta.GlyphIdentifier = GlyphIdentifier;
    }
    if (GroupName != nullptr && GroupName[0] != '\0')
    {
        Meta.GroupName = GroupName;
    }
    if (SubtitleCategory != nullptr && SubtitleCategory[0] != '\0')
    {
        Meta.SubtitleCategory = SubtitleCategory;
    }
    if (AccentHex != nullptr && AccentHex[0] != '\0')
    {
        Meta.AccentHex = AccentHex;
    }
    SceneDirectory::Instance().ConfigureMetadata(Token(), std::move(Meta));
#endif
}

//------------------------------------------------------------------------------------------------------------------------
//                                       SINGLE-CONFIGURATION SERIALIZATION SPECIALIZATIONS
//------------------------------------------------------------------------------------------------------------------------

template <>
void RenderPipelineRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("hardware_ray_query", Parameters.HardwareRayQueryTier);
    OutTable.insert_or_assign("analytic_illumination", Parameters.AnalyticIllumination);
    OutTable.insert_or_assign("global_illumination", Parameters.GlobalIllumination);
    OutTable.insert_or_assign("multi_bounce_nee", Parameters.MultiBounceNee);
    OutTable.insert_or_assign("russian_roulette", Parameters.RussianRoulette);
    OutTable.insert_or_assign("temporal_reuse", Parameters.TemporalReuse);
    OutTable.insert_or_assign("spatial_reuse", Parameters.SpatialReuse);
    OutTable.insert_or_assign("visibility_reuse", Parameters.VisibilityReuse);
    OutTable.insert_or_assign("radiance_refresh", Parameters.RadianceRefresh);
    OutTable.insert_or_assign("card_radiosity", Parameters.CardRadiosity);
    OutTable.insert_or_assign("reflections", Parameters.Reflection);
    OutTable.insert_or_assign("ray_traced_reflections", Parameters.RayTracedReflections);
    OutTable.insert_or_assign("mirror_filter", Parameters.MirrorFilter);
    OutTable.insert_or_assign("mirror_trace", Parameters.MirrorTrace);
    OutTable.insert_or_assign("direct_shadow", Parameters.DirectShadow);
    OutTable.insert_or_assign("ray_traced_shadows", Parameters.RayTracedShadows);
    OutTable.insert_or_assign("firefly_filter", Parameters.FireflyFilter);
    OutTable.insert_or_assign("solar_disc", Parameters.SolarDisc);
    OutTable.insert_or_assign("stratified_sun", Parameters.StratifiedSun);
    OutTable.insert_or_assign("penumbra_filter", Parameters.PenumbraFilter);
    OutTable.insert_or_assign("variance_filter", Parameters.VarianceFilter);
    OutTable.insert_or_assign("temporal_aa", Parameters.TemporalAntiAliasing);
    OutTable.insert_or_assign("auto_exposure", Parameters.AutoExposure);
    OutTable.insert_or_assign("virtualized_geometry", Parameters.VirtualizedGeometry);
    OutTable.insert_or_assign("live_refresh", Parameters.LiveRefresh);
    OutTable.insert_or_assign("quality_tier", static_cast<int64_t>(Parameters.QualityTier));
    OutTable.insert_or_assign("trace_scale", static_cast<int64_t>(Parameters.TraceScale));
    OutTable.insert_or_assign("initial_candidates", static_cast<int64_t>(Parameters.InitialCandidates));
    OutTable.insert_or_assign("max_bounces", static_cast<int64_t>(Parameters.MaxBounces));
    OutTable.insert_or_assign("spatial_count", static_cast<int64_t>(Parameters.SpatialCount));
    OutTable.insert_or_assign("card_tile_budget", static_cast<int64_t>(Parameters.CardTileBudget));
    OutTable.insert_or_assign("reflection_bounces", static_cast<int64_t>(Parameters.ReflectionBounces));
    OutTable.insert_or_assign("diffuse_wavelet_levels", static_cast<int64_t>(Parameters.DiffuseWaveletLevels));
    OutTable.insert_or_assign("diffuse_tally_limit", static_cast<int64_t>(Parameters.DiffuseTallyLimit));
    OutTable.insert_or_assign("glossy_wavelet_levels", static_cast<int64_t>(Parameters.GlossyWaveletLevels));
    OutTable.insert_or_assign("glossy_tally_limit", static_cast<int64_t>(Parameters.GlossyTallyLimit));
    OutTable.insert_or_assign("virtualized_stream_slots", static_cast<int64_t>(Parameters.VirtualizedStreamSlots));
    OutTable.insert_or_assign("inspection_mode", static_cast<int64_t>(Parameters.InspectionModeIndex));
    OutTable.insert_or_assign("distance_inspection", static_cast<int64_t>(Parameters.DistanceInspection));
    OutTable.insert_or_assign("target_rate", static_cast<int64_t>(Parameters.TargetRate));
    OutTable.insert_or_assign("render_scale", static_cast<double>(Parameters.RenderScale));
    OutTable.insert_or_assign("spatial_radius", static_cast<double>(Parameters.SpatialRadius));
    OutTable.insert_or_assign("temporal_tally_limit", static_cast<double>(Parameters.TemporalTallyLimit));
    OutTable.insert_or_assign("indirect_scale", static_cast<double>(Parameters.IndirectScale));
    OutTable.insert_or_assign("sky_scale", static_cast<double>(Parameters.SkyScale));
    OutTable.insert_or_assign("incandescence_scale", static_cast<double>(Parameters.IncandescenceScale));
    OutTable.insert_or_assign("reflection_scale", static_cast<double>(Parameters.ReflectionScale));
    OutTable.insert_or_assign("reflection_roughness", static_cast<double>(Parameters.ReflectionRoughness));
    OutTable.insert_or_assign("diffuse_minimum_weight", static_cast<double>(Parameters.DiffuseMinimumWeight));
    OutTable.insert_or_assign("diffuse_colour_sigma", static_cast<double>(Parameters.DiffuseColourSigma));
    OutTable.insert_or_assign("glossy_minimum_weight", static_cast<double>(Parameters.GlossyMinimumWeight));
    OutTable.insert_or_assign("glossy_colour_sigma", static_cast<double>(Parameters.GlossyColourSigma));
    OutTable.insert_or_assign("exposure_ev", static_cast<double>(Parameters.ExposureCompensationEv));
}

// 📝 The bloom, filmic tonemap, screen-space reflection and probe-range keys of earlier levels had no reader; they are ignored.
template <>
void RenderPipelineRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.Enabled                = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.HardwareRayQueryTier   = ReadBool(InTable, "hardware_ray_query", Parameters.HardwareRayQueryTier);
    Parameters.AnalyticIllumination   = ReadBool(InTable, "analytic_illumination", Parameters.AnalyticIllumination);
    Parameters.GlobalIllumination     = ReadBool(InTable, "global_illumination", Parameters.GlobalIllumination);
    Parameters.MultiBounceNee         = ReadBool(InTable, "multi_bounce_nee", Parameters.MultiBounceNee);
    Parameters.RussianRoulette        = ReadBool(InTable, "russian_roulette", Parameters.RussianRoulette);
    Parameters.TemporalReuse          = ReadBool(InTable, "temporal_reuse", Parameters.TemporalReuse);
    Parameters.SpatialReuse           = ReadBool(InTable, "spatial_reuse", Parameters.SpatialReuse);
    Parameters.VisibilityReuse        = ReadBool(InTable, "visibility_reuse", Parameters.VisibilityReuse);
    Parameters.RadianceRefresh        = ReadBool(InTable, "radiance_refresh", Parameters.RadianceRefresh);
    Parameters.CardRadiosity          = ReadBool(InTable, "card_radiosity", Parameters.CardRadiosity);
    Parameters.Reflection             = ReadBool(InTable, "reflections", Parameters.Reflection);
    Parameters.RayTracedReflections   = ReadBool(InTable, "ray_traced_reflections", Parameters.RayTracedReflections);
    Parameters.MirrorFilter           = ReadBool(InTable, "mirror_filter", Parameters.MirrorFilter);
    Parameters.MirrorTrace            = ReadBool(InTable, "mirror_trace", Parameters.MirrorTrace);
    Parameters.DirectShadow           = ReadBool(InTable, "direct_shadow", Parameters.DirectShadow);
    Parameters.RayTracedShadows       = ReadBool(InTable, "ray_traced_shadows", Parameters.RayTracedShadows);
    Parameters.FireflyFilter          = ReadBool(InTable, "firefly_filter", Parameters.FireflyFilter);
    Parameters.SolarDisc              = ReadBool(InTable, "solar_disc", Parameters.SolarDisc);
    Parameters.StratifiedSun          = ReadBool(InTable, "stratified_sun", Parameters.StratifiedSun);
    Parameters.PenumbraFilter         = ReadBool(InTable, "penumbra_filter", Parameters.PenumbraFilter);
    Parameters.VarianceFilter         = ReadBool(InTable, "variance_filter", Parameters.VarianceFilter);
    Parameters.TemporalAntiAliasing   = ReadBool(InTable, "temporal_aa", Parameters.TemporalAntiAliasing);
    Parameters.AutoExposure           = ReadBool(InTable, "auto_exposure", Parameters.AutoExposure);
    Parameters.VirtualizedGeometry    = ReadBool(InTable, "virtualized_geometry", Parameters.VirtualizedGeometry);
    Parameters.LiveRefresh            = ReadBool(InTable, "live_refresh", Parameters.LiveRefresh);
    Parameters.QualityTier            = std::clamp(ReadInt(InTable, "quality_tier", Parameters.QualityTier), 0, 2);
    Parameters.TraceScale             = std::clamp(ReadInt(InTable, "trace_scale", Parameters.TraceScale), 1, 4);
    Parameters.InitialCandidates      = std::clamp(ReadInt(InTable, "initial_candidates", Parameters.InitialCandidates), 1, 4);
    Parameters.MaxBounces             = std::clamp(ReadInt(InTable, "max_bounces", Parameters.MaxBounces), 1, 4);
    Parameters.SpatialCount           = std::clamp(ReadInt(InTable, "spatial_count", Parameters.SpatialCount), 0, 8);
    Parameters.CardTileBudget         = std::clamp(ReadInt(InTable, "card_tile_budget", Parameters.CardTileBudget), 0, 8192);
    Parameters.ReflectionBounces      = std::clamp(ReadInt(InTable, "reflection_bounces", Parameters.ReflectionBounces), 1, 4);
    Parameters.DiffuseWaveletLevels   = std::clamp(ReadInt(InTable, "diffuse_wavelet_levels", Parameters.DiffuseWaveletLevels), 1, 4);
    Parameters.DiffuseTallyLimit      = std::clamp(ReadInt(InTable, "diffuse_tally_limit", Parameters.DiffuseTallyLimit), 1, 64);
    Parameters.GlossyWaveletLevels    = std::clamp(ReadInt(InTable, "glossy_wavelet_levels", Parameters.GlossyWaveletLevels), 1, 4);
    Parameters.GlossyTallyLimit       = std::clamp(ReadInt(InTable, "glossy_tally_limit", Parameters.GlossyTallyLimit), 1, 64);
    Parameters.VirtualizedStreamSlots = std::clamp(ReadInt(InTable, "virtualized_stream_slots", Parameters.VirtualizedStreamSlots), 0, 64);
    Parameters.InspectionModeIndex    = std::clamp(ReadInt(InTable, "inspection_mode", Parameters.InspectionModeIndex), 0, 14);
    Parameters.DistanceInspection     = std::clamp(ReadInt(InTable, "distance_inspection", Parameters.DistanceInspection), 1, 3);
    Parameters.TargetRate             = std::clamp(ReadInt(InTable, "target_rate", Parameters.TargetRate), 30, 120);
    Parameters.RenderScale            = std::clamp(ReadFloat(InTable, "render_scale", Parameters.RenderScale), 0.25f, 2.0f);
    Parameters.SpatialRadius          = std::clamp(ReadFloat(InTable, "spatial_radius", Parameters.SpatialRadius), 2.0f, 48.0f);
    Parameters.TemporalTallyLimit     = std::clamp(ReadFloat(InTable, "temporal_tally_limit", Parameters.TemporalTallyLimit), 1.0f, 40.0f);
    Parameters.IndirectScale          = std::clamp(ReadFloat(InTable, "indirect_scale", Parameters.IndirectScale), 0.0f, 3.0f);
    Parameters.SkyScale               = std::clamp(ReadFloat(InTable, "sky_scale", Parameters.SkyScale), 0.0f, 3.0f);
    Parameters.IncandescenceScale     = std::clamp(ReadFloat(InTable, "incandescence_scale", Parameters.IncandescenceScale), 0.0f, 3.0f);
    Parameters.ReflectionScale        = std::clamp(ReadFloat(InTable, "reflection_scale", Parameters.ReflectionScale), 0.0f, 3.0f);
    Parameters.ReflectionRoughness    = std::clamp(ReadFloat(InTable, "reflection_roughness", Parameters.ReflectionRoughness), 0.15f, 1.0f);
    Parameters.DiffuseMinimumWeight   = std::clamp(ReadFloat(InTable, "diffuse_minimum_weight", Parameters.DiffuseMinimumWeight), 0.01f, 0.5f);
    Parameters.DiffuseColourSigma     = std::clamp(ReadFloat(InTable, "diffuse_colour_sigma", Parameters.DiffuseColourSigma), 0.5f, 16.0f);
    Parameters.GlossyMinimumWeight    = std::clamp(ReadFloat(InTable, "glossy_minimum_weight", Parameters.GlossyMinimumWeight), 0.01f, 0.5f);
    Parameters.GlossyColourSigma      = std::clamp(ReadFloat(InTable, "glossy_colour_sigma", Parameters.GlossyColourSigma), 0.5f, 16.0f);
    Parameters.ExposureCompensationEv = std::clamp(ReadFloat(InTable, "exposure_ev", Parameters.ExposureCompensationEv), -4.0f, 4.0f);
    // 📝 Only the demo's three trace resolutions: full, half and quarter.
    if (Parameters.TraceScale == 3) Parameters.TraceScale = 2;
    Parameters.AdvanceRevision();
}

template <>
void SolarRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("elevation_deg", static_cast<double>(Parameters.ElevationDegrees));
    OutTable.insert_or_assign("azimuth_deg", static_cast<double>(Parameters.AzimuthDegrees));
    OutTable.insert_or_assign("illuminance_klx", static_cast<double>(Parameters.IlluminanceKilolux));
    OutTable.insert_or_assign("angular_diameter_deg", static_cast<double>(Parameters.AngularDiameterDeg));
    OutTable.insert_or_assign("kelvin", static_cast<double>(Parameters.KelvinTemperature));
    OutTable.insert_or_assign("ray_shadow", Parameters.CastRaytracedShadow);
    OutTable.insert_or_assign("light_shafts", Parameters.VolumetricLightShaft);
    OutTable.insert_or_assign("disc_visible", Parameters.DiscVisible);
    OutTable.insert_or_assign("aureole_gain", static_cast<double>(Parameters.AureoleGain));
    OutTable.insert_or_assign("colour_source", static_cast<int64_t>(Parameters.ColourSource));
    WriteFloat3(OutTable, "tint_linear", Parameters.TintLinear.data());
    OutTable.insert_or_assign("time_linked", Parameters.TimeLinked);
    OutTable.insert_or_assign("day_cycle", Parameters.DayCycle);
    OutTable.insert_or_assign("local_solar_h", static_cast<double>(Parameters.LocalSolarHours));
    OutTable.insert_or_assign("day_duration_h", static_cast<double>(Parameters.DayDurationHours));
    OutTable.insert_or_assign("calendar_date", Parameters.CalendarDate);
    OutTable.insert_or_assign("latitude_deg", static_cast<double>(Parameters.LatitudeDegrees));
    OutTable.insert_or_assign("longitude_deg", static_cast<double>(Parameters.LongitudeDegrees));
    OutTable.insert_or_assign("utc_offset_h", static_cast<double>(Parameters.UtcOffsetHours));
}

template <>
void SolarRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.Enabled              = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.ElevationDegrees     = ReadFloat(InTable, "elevation_deg", Parameters.ElevationDegrees);
    Parameters.AzimuthDegrees       = ReadFloat(InTable, "azimuth_deg", Parameters.AzimuthDegrees);
    Parameters.IlluminanceKilolux   = ReadFloat(InTable, "illuminance_klx", Parameters.IlluminanceKilolux);
    Parameters.AngularDiameterDeg   = ReadFloat(InTable, "angular_diameter_deg", Parameters.AngularDiameterDeg);
    Parameters.KelvinTemperature    = ReadFloat(InTable, "kelvin", Parameters.KelvinTemperature);
    Parameters.CastRaytracedShadow  = ReadBool(InTable, "ray_shadow", Parameters.CastRaytracedShadow);
    Parameters.VolumetricLightShaft = ReadBool(InTable, "light_shafts", Parameters.VolumetricLightShaft);
    Parameters.DiscVisible          = ReadBool(InTable, "disc_visible", Parameters.DiscVisible);
    Parameters.AureoleGain          = ReadFloat(InTable, "aureole_gain", Parameters.AureoleGain);
    Parameters.ColourSource         = std::min<uint32_t>(1u, ReadUint32(InTable, "colour_source", Parameters.ColourSource));
    if (InTable.contains("tint_linear")) ReadFloat3(InTable, "tint_linear", Parameters.TintLinear.data());
    Parameters.TimeLinked           = ReadBool(InTable, "time_linked", Parameters.TimeLinked);
    Parameters.DayCycle             = ReadBool(InTable, "day_cycle", Parameters.DayCycle);
    Parameters.LocalSolarHours      = ReadFloat(InTable, "local_solar_h", Parameters.LocalSolarHours);
    Parameters.DayDurationHours     = ReadFloat(InTable, "day_duration_h", Parameters.DayDurationHours);
    if (const auto Date = InTable["calendar_date"].value<std::string>()) Parameters.CalendarDate = *Date;
    Parameters.LatitudeDegrees      = ReadFloat(InTable, "latitude_deg", Parameters.LatitudeDegrees);
    Parameters.LongitudeDegrees     = ReadFloat(InTable, "longitude_deg", Parameters.LongitudeDegrees);
    Parameters.UtcOffsetHours       = ReadFloat(InTable, "utc_offset_h", Parameters.UtcOffsetHours);
    Parameters.AdvanceRevision();
}

template <>
void OpticalFlareRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("intensity", static_cast<double>(Parameters.Intensity));
    OutTable.insert_or_assign("aperture_blades", static_cast<int64_t>(Parameters.ApertureBladeCount));
    OutTable.insert_or_assign("luminance_threshold", static_cast<double>(Parameters.LuminanceThreshold));
    OutTable.insert_or_assign("halo_radius", static_cast<double>(Parameters.HaloRadius));
    OutTable.insert_or_assign("chromatic_dispersion", static_cast<double>(Parameters.ChromaticDispersion));
    OutTable.insert_or_assign("anamorphic_streak", Parameters.AnamorphicStreak);
    OutTable.insert_or_assign("diffraction_starburst", Parameters.DiffractionStarburst);
    OutTable.insert_or_assign("preset", static_cast<int64_t>(Parameters.Preset));
    OutTable.insert_or_assign("spread", static_cast<double>(Parameters.Spread));
    OutTable.insert_or_assign("ghost_count", static_cast<int64_t>(Parameters.GhostCount));
    OutTable.insert_or_assign("ghost_brightness", static_cast<double>(Parameters.GhostBrightness));
    OutTable.insert_or_assign("ghost_shape", static_cast<int64_t>(Parameters.GhostShape));
    OutTable.insert_or_assign("ghost_spacing", static_cast<double>(Parameters.GhostSpacing));
    OutTable.insert_or_assign("halo_width", static_cast<double>(Parameters.HaloWidth));
    OutTable.insert_or_assign("halo_brightness", static_cast<double>(Parameters.HaloBrightness));
    OutTable.insert_or_assign("streak_strength", static_cast<double>(Parameters.StreakStrength));
    OutTable.insert_or_assign("rotation_degrees", static_cast<double>(Parameters.RotationDegrees));
    OutTable.insert_or_assign("ray_count", static_cast<int64_t>(Parameters.RayCount));
    OutTable.insert_or_assign("warm_streak", Parameters.WarmStreak);
    OutTable.insert_or_assign("preview_position_x", static_cast<double>(Parameters.PreviewPosition[0]));
    OutTable.insert_or_assign("preview_position_y", static_cast<double>(Parameters.PreviewPosition[1]));
}

template <>
void OpticalFlareRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.Enabled            = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.Intensity          = ReadFloat(InTable, "intensity", Parameters.Intensity);
    Parameters.ApertureBladeCount = ReadUint32(InTable, "aperture_blades", Parameters.ApertureBladeCount);
    Parameters.LuminanceThreshold = ReadFloat(InTable, "luminance_threshold", Parameters.LuminanceThreshold);
    Parameters.HaloRadius = ReadFloat(InTable, "halo_radius", Parameters.HaloRadius);
    Parameters.ChromaticDispersion = ReadFloat(InTable, "chromatic_dispersion", Parameters.ChromaticDispersion);
    Parameters.AnamorphicStreak = ReadBool(InTable, "anamorphic_streak", Parameters.AnamorphicStreak);
    Parameters.DiffractionStarburst = ReadBool(InTable, "diffraction_starburst", Parameters.DiffractionStarburst);
    Parameters.Preset = ReadUint32(InTable, "preset", Parameters.Preset);
    Parameters.Spread = ReadFloat(InTable, "spread", Parameters.Spread);
    Parameters.GhostCount = ReadUint32(InTable, "ghost_count", Parameters.GhostCount);
    Parameters.GhostBrightness = ReadFloat(InTable, "ghost_brightness", Parameters.GhostBrightness);
    Parameters.GhostShape = ReadUint32(InTable, "ghost_shape", Parameters.GhostShape);
    Parameters.GhostSpacing = ReadFloat(InTable, "ghost_spacing", Parameters.GhostSpacing);
    Parameters.HaloWidth = ReadFloat(InTable, "halo_width", Parameters.HaloWidth);
    Parameters.HaloBrightness = ReadFloat(InTable, "halo_brightness", Parameters.HaloBrightness);
    Parameters.StreakStrength = ReadFloat(InTable, "streak_strength", Parameters.StreakStrength);
    Parameters.RotationDegrees = ReadFloat(InTable, "rotation_degrees", Parameters.RotationDegrees);
    Parameters.RayCount = ReadUint32(InTable, "ray_count", Parameters.RayCount);
    Parameters.WarmStreak = ReadBool(InTable, "warm_streak", Parameters.WarmStreak);
    Parameters.PreviewPosition[0] = ReadFloat(InTable, "preview_position_x", Parameters.PreviewPosition[0]);
    Parameters.PreviewPosition[1] = ReadFloat(InTable, "preview_position_y", Parameters.PreviewPosition[1]);
    Parameters.AdvanceRevision();
}

template <>
void AtmosphereRecord::SerializeConfiguration(toml::table& OutTable) const
{
    WriteAtmosphereOptics(OutTable,Parameters);
    OutTable.insert_or_assign("aerial_perspective",Parameters.AerialPerspective);
    OutTable.insert_or_assign("use_baked",Parameters.UseBaked);
    if (Parameters.BakedAvailable)
    {
        toml::table Baked;
        WriteAtmosphereOptics(Baked,Parameters.Baked);
        OutTable.insert_or_assign("baked",std::move(Baked));
    }

    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("rayleigh", static_cast<double>(Parameters.RayleighScattering));
    OutTable.insert_or_assign("mie_turbidity", static_cast<double>(Parameters.MieTurbidity));
    OutTable.insert_or_assign("ozone", static_cast<double>(Parameters.OzoneAbsorptionScale));
    OutTable.insert_or_assign("mie_g", static_cast<double>(Parameters.MieAnisotropyG));
    OutTable.insert_or_assign("rayleigh_height_m", static_cast<double>(Parameters.RayleighHeightMetres));
    OutTable.insert_or_assign("mie_height_m", static_cast<double>(Parameters.MieHeightMetres));
    WriteFloat3(OutTable, "ground_albedo", Parameters.GroundAlbedo);
    WriteFloat3(OutTable, "sky_tint", Parameters.SkyTint);
    OutTable.insert_or_assign("sky_scale", static_cast<double>(Parameters.SkyScale));
    OutTable.insert_or_assign("horizon_glow", static_cast<double>(Parameters.HorizonGlow));
    OutTable.insert_or_assign("white_line", static_cast<double>(Parameters.WhiteLine));
    OutTable.insert_or_assign("line_at_civil_only", Parameters.LineAtCivilOnly);
    OutTable.insert_or_assign("air_mass", Parameters.AirMass);
    OutTable.insert_or_assign("multiple_scattering", Parameters.MultiScatteringLut);
    OutTable.insert_or_assign("tier_samples", static_cast<int64_t>(Parameters.TierSamples));
}

template <>
void AtmosphereRecord::DeserializeConfiguration(const toml::table& InTable)
{
    ReadAtmosphereOptics(InTable,Parameters);
    Parameters.AerialPerspective = ReadBool(InTable,"aerial_perspective",Parameters.AerialPerspective);
    Parameters.UseBaked = ReadBool(InTable,"use_baked",Parameters.UseBaked);
    if (const auto* Baked = InTable["baked"].as_table())
    {
        ReadAtmosphereOptics(*Baked,Parameters.Baked);
        Parameters.BakedAvailable = true;
    }

    Parameters.Enabled              = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.RayleighScattering   = ReadFloat(InTable, "rayleigh", Parameters.RayleighScattering);
    Parameters.MieTurbidity         = ReadFloat(InTable, "mie_turbidity", Parameters.MieTurbidity);
    Parameters.OzoneAbsorptionScale = ReadFloat(InTable, "ozone", Parameters.OzoneAbsorptionScale);
    Parameters.MieAnisotropyG       = ReadFloat(InTable, "mie_g", Parameters.MieAnisotropyG);
    Parameters.RayleighHeightMetres = ReadFloat(InTable, "rayleigh_height_m", Parameters.RayleighHeightMetres);
    Parameters.MieHeightMetres      = ReadFloat(InTable, "mie_height_m", Parameters.MieHeightMetres);
    ReadFloat3(InTable, "ground_albedo", Parameters.GroundAlbedo);
    ReadFloat3(InTable, "sky_tint", Parameters.SkyTint);
    Parameters.SkyScale             = ReadFloat(InTable, "sky_scale", Parameters.SkyScale);
    Parameters.HorizonGlow          = ReadFloat(InTable, "horizon_glow", Parameters.HorizonGlow);
    Parameters.WhiteLine            = ReadFloat(InTable, "white_line", Parameters.WhiteLine);
    Parameters.LineAtCivilOnly      = ReadBool(InTable, "line_at_civil_only", Parameters.LineAtCivilOnly);
    Parameters.AirMass              = ReadBool(InTable, "air_mass", Parameters.AirMass);
    Parameters.MultiScatteringLut   = ReadBool(InTable, "multiple_scattering", Parameters.MultiScatteringLut);
    Parameters.TierSamples          = ReadUint32(InTable, "tier_samples", Parameters.TierSamples);
    Parameters.AdvanceRevision();
}

template <>
void CloudRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("local_volume", Parameters.LocalVolume);
    OutTable.insert_or_assign("coverage", static_cast<double>(Parameters.Coverage));
    OutTable.insert_or_assign("base_altitude_m", static_cast<double>(Parameters.BaseAltitudeMetres));
    OutTable.insert_or_assign("thickness_m", static_cast<double>(Parameters.LayerThicknessMetres));
    OutTable.insert_or_assign("density", static_cast<double>(Parameters.ExtinctionDensity));
    OutTable.insert_or_assign("drift_mps", static_cast<double>(Parameters.WindDriftSpeed));
    OutTable.insert_or_assign("silver_lining", static_cast<double>(Parameters.SilverLiningPower));
    OutTable.insert_or_assign("temporal_reprojection", Parameters.TemporalReprojection);
    OutTable.insert_or_assign("cast_shadows", Parameters.CastGroundShadows);
    OutTable.insert_or_assign("formation", static_cast<int64_t>(Parameters.Formation));
    OutTable.insert_or_assign("ceiling_metres", static_cast<double>(Parameters.CeilingMetres));
    OutTable.insert_or_assign("feature_scale", static_cast<double>(Parameters.FeatureScale));
    OutTable.insert_or_assign("anvil_shaping", static_cast<double>(Parameters.AnvilShaping));
    OutTable.insert_or_assign("edge_erosion", static_cast<double>(Parameters.EdgeErosion));
    OutTable.insert_or_assign("phase_anisotropy", static_cast<double>(Parameters.PhaseAnisotropy));
    OutTable.insert_or_assign("light_absorption", static_cast<double>(Parameters.LightAbsorption));
    OutTable.insert_or_assign("powder_response", static_cast<double>(Parameters.PowderResponse));
    OutTable.insert_or_assign("light_samples", static_cast<int64_t>(Parameters.LightSamples));
    OutTable.insert_or_assign("resolution_percent", static_cast<int64_t>(Parameters.ResolutionPercent));
    OutTable.insert_or_assign("sky_samples", static_cast<int64_t>(Parameters.SkySamples));
    OutTable.insert_or_assign("local_samples", static_cast<int64_t>(Parameters.LocalSamples));
    OutTable.insert_or_assign("shadow_strength", static_cast<double>(Parameters.ShadowStrength));
    OutTable.insert_or_assign("shadow_softness_metres", static_cast<double>(Parameters.ShadowSoftnessMetres));
    OutTable.insert_or_assign("shadow_span_metres", static_cast<double>(Parameters.ShadowSpanMetres));
    OutTable.insert_or_assign("shadow_samples", static_cast<int64_t>(Parameters.ShadowSamples));
    OutTable.insert_or_assign("follow_wind", Parameters.FollowWind);
    OutTable.insert_or_assign("bounds_shape", static_cast<int64_t>(Parameters.BoundsShape));
    OutTable.insert_or_assign("local_formation", static_cast<int64_t>(Parameters.LocalFormation));
    OutTable.insert_or_assign("edge_softness", static_cast<double>(Parameters.EdgeSoftness));
    OutTable.insert_or_assign("local_feature_metres", static_cast<double>(Parameters.LocalFeatureMetres));
    WriteFloat3(OutTable, "centre_metres", Parameters.CentreMetres);
    WriteFloat3(OutTable, "half_extents_metres", Parameters.HalfExtentsMetres);
}

template <>
void CloudRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.Enabled              = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.LocalVolume          = ReadBool(InTable, "local_volume", Parameters.LocalVolume);
    Parameters.Coverage             = ReadFloat(InTable, "coverage", Parameters.Coverage);
    Parameters.BaseAltitudeMetres   = ReadFloat(InTable, "base_altitude_m", Parameters.BaseAltitudeMetres);
    Parameters.LayerThicknessMetres = ReadFloat(InTable, "thickness_m", Parameters.LayerThicknessMetres);
    Parameters.ExtinctionDensity = ReadFloat(InTable, "density", Parameters.ExtinctionDensity);
    Parameters.WindDriftSpeed = ReadFloat(InTable, "drift_mps", Parameters.WindDriftSpeed);
    Parameters.SilverLiningPower = ReadFloat(InTable, "silver_lining", Parameters.SilverLiningPower);
    Parameters.TemporalReprojection = ReadBool(InTable, "temporal_reprojection", Parameters.TemporalReprojection);
    Parameters.CastGroundShadows = ReadBool(InTable, "cast_shadows", Parameters.CastGroundShadows);
    Parameters.Formation = ReadUint32(InTable, "formation", Parameters.Formation);
    Parameters.CeilingMetres = ReadFloat(InTable, "ceiling_metres", Parameters.CeilingMetres);
    Parameters.FeatureScale = ReadFloat(InTable, "feature_scale", Parameters.FeatureScale);
    Parameters.AnvilShaping = ReadFloat(InTable, "anvil_shaping", Parameters.AnvilShaping);
    Parameters.EdgeErosion = ReadFloat(InTable, "edge_erosion", Parameters.EdgeErosion);
    Parameters.PhaseAnisotropy = ReadFloat(InTable, "phase_anisotropy", Parameters.PhaseAnisotropy);
    Parameters.LightAbsorption = ReadFloat(InTable, "light_absorption", Parameters.LightAbsorption);
    Parameters.PowderResponse = ReadFloat(InTable, "powder_response", Parameters.PowderResponse);
    Parameters.LightSamples = ReadUint32(InTable, "light_samples", Parameters.LightSamples);
    Parameters.ResolutionPercent = ReadUint32(InTable, "resolution_percent", Parameters.ResolutionPercent);
    Parameters.SkySamples = ReadUint32(InTable, "sky_samples", Parameters.SkySamples);
    Parameters.LocalSamples = ReadUint32(InTable, "local_samples", Parameters.LocalSamples);
    Parameters.ShadowStrength = ReadFloat(InTable, "shadow_strength", Parameters.ShadowStrength);
    Parameters.ShadowSoftnessMetres = ReadFloat(InTable, "shadow_softness_metres", Parameters.ShadowSoftnessMetres);
    Parameters.ShadowSpanMetres = ReadFloat(InTable, "shadow_span_metres", Parameters.ShadowSpanMetres);
    Parameters.ShadowSamples = ReadUint32(InTable, "shadow_samples", Parameters.ShadowSamples);
    Parameters.FollowWind = ReadBool(InTable, "follow_wind", Parameters.FollowWind);
    Parameters.BoundsShape = ReadUint32(InTable, "bounds_shape", Parameters.BoundsShape);
    Parameters.LocalFormation = ReadUint32(InTable, "local_formation", Parameters.LocalFormation);
    Parameters.EdgeSoftness = ReadFloat(InTable, "edge_softness", Parameters.EdgeSoftness);
    Parameters.LocalFeatureMetres = ReadFloat(InTable, "local_feature_metres", Parameters.LocalFeatureMetres);
    ReadFloat3(InTable, "centre_metres", Parameters.CentreMetres);
    ReadFloat3(InTable, "half_extents_metres", Parameters.HalfExtentsMetres);
    Parameters.AdvanceRevision();
}

template <>
void StarRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("hipparcos_count", static_cast<int64_t>(Parameters.HipparcosCount));
    OutTable.insert_or_assign("magnitude_gain", static_cast<double>(Parameters.MagnitudeGain));
    OutTable.insert_or_assign("milky_way", static_cast<double>(Parameters.MilkyWayLuminance));
    OutTable.insert_or_assign("sidereal_speed", static_cast<double>(Parameters.SiderealSpeed));
    OutTable.insert_or_assign("twinkle_depth", static_cast<double>(Parameters.AtmosphericScintillate));
    OutTable.insert_or_assign("limiting_magnitude", static_cast<double>(Parameters.LimitingMagnitude));
    OutTable.insert_or_assign("point_size", static_cast<double>(Parameters.PointSize));
    OutTable.insert_or_assign("halo_gain", static_cast<double>(Parameters.HaloGain));
    OutTable.insert_or_assign("rotation_degrees", static_cast<double>(Parameters.RotationDegrees));
    OutTable.insert_or_assign("scintillation_enabled", Parameters.ScintillationEnabled);
    OutTable.insert_or_assign("scintillation_rate_hz", static_cast<double>(Parameters.ScintillationRateHz));
}

template <>
void StarRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.Enabled        = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.HipparcosCount = ReadUint32(InTable, "hipparcos_count", Parameters.HipparcosCount);
    Parameters.MagnitudeGain  = ReadFloat(InTable, "magnitude_gain", Parameters.MagnitudeGain);
    Parameters.MilkyWayLuminance = ReadFloat(InTable, "milky_way", Parameters.MilkyWayLuminance);
    Parameters.SiderealSpeed = ReadFloat(InTable, "sidereal_speed", Parameters.SiderealSpeed);
    Parameters.AtmosphericScintillate = ReadFloat(InTable, "twinkle_depth", Parameters.AtmosphericScintillate);
    Parameters.LimitingMagnitude = ReadFloat(InTable, "limiting_magnitude", Parameters.LimitingMagnitude);
    Parameters.PointSize = ReadFloat(InTable, "point_size", Parameters.PointSize);
    Parameters.HaloGain = ReadFloat(InTable, "halo_gain", Parameters.HaloGain);
    Parameters.RotationDegrees = ReadFloat(InTable, "rotation_degrees", Parameters.RotationDegrees);
    Parameters.ScintillationEnabled = ReadBool(InTable, "scintillation_enabled", Parameters.ScintillationEnabled);
    Parameters.ScintillationRateHz = ReadFloat(InTable, "scintillation_rate_hz", Parameters.ScintillationRateHz);
    Parameters.AdvanceRevision();
}

template <>
void MoonRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("primary_phase", static_cast<double>(Parameters.PrimaryPhase));
    OutTable.insert_or_assign("primary_orbit_degrees", static_cast<double>(Parameters.PrimaryOrbitDegrees));
    OutTable.insert_or_assign("primary_albedo", static_cast<double>(Parameters.PrimaryAlbedo));
    OutTable.insert_or_assign("secondary_enabled", Parameters.SecondaryMoonEnabled);
    OutTable.insert_or_assign("secondary_orbit_degrees", static_cast<double>(Parameters.SecondaryOrbitDegrees));
    OutTable.insert_or_assign("earthshine", static_cast<double>(Parameters.EarthshineLuminance));
    OutTable.insert_or_assign("illuminate_scene", Parameters.IlluminateScene);
    OutTable.insert_or_assign("light_power", static_cast<double>(Parameters.LightPower));
    toml::array Instances;
    for (const auto& Moon : Parameters.Instances)
    {
        toml::table Entry;
        Entry.insert_or_assign("visible", Moon.Visible);
        Entry.insert_or_assign("follow_sky", Moon.FollowSky);
        Entry.insert_or_assign("body", static_cast<int64_t>(Moon.Body));
        Entry.insert_or_assign("phase_days", static_cast<double>(Moon.PhaseDays));
        Entry.insert_or_assign("azimuth_degrees", static_cast<double>(Moon.AzimuthDegrees));
        Entry.insert_or_assign("elevation_degrees", static_cast<double>(Moon.ElevationDegrees));
        Entry.insert_or_assign("roll_degrees", static_cast<double>(Moon.RollDegrees));
        Entry.insert_or_assign("pitch_degrees", static_cast<double>(Moon.PitchDegrees));
        Entry.insert_or_assign("spin_degrees", static_cast<double>(Moon.SpinDegrees));
        Entry.insert_or_assign("angular_diameter_degrees", static_cast<double>(Moon.AngularDiameterDegrees));
        Entry.insert_or_assign("brightness", static_cast<double>(Moon.Brightness));
        Entry.insert_or_assign("glow", static_cast<double>(Moon.Glow));
        Instances.push_back(std::move(Entry));
    }
    OutTable.insert_or_assign("instances", std::move(Instances));
}

template <>
void MoonRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.Enabled = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.PrimaryPhase = ReadFloat(InTable, "primary_phase", Parameters.PrimaryPhase);
    Parameters.PrimaryOrbitDegrees = ReadFloat(InTable, "primary_orbit_degrees", Parameters.PrimaryOrbitDegrees);
    Parameters.PrimaryAlbedo = ReadFloat(InTable, "primary_albedo", Parameters.PrimaryAlbedo);
    Parameters.SecondaryMoonEnabled = ReadBool(InTable, "secondary_enabled", Parameters.SecondaryMoonEnabled);
    Parameters.SecondaryOrbitDegrees = ReadFloat(InTable, "secondary_orbit_degrees", Parameters.SecondaryOrbitDegrees);
    Parameters.EarthshineLuminance = ReadFloat(InTable, "earthshine", Parameters.EarthshineLuminance);
    Parameters.IlluminateScene = ReadBool(InTable, "illuminate_scene", Parameters.IlluminateScene);
    Parameters.LightPower = std::clamp(ReadFloat(InTable, "light_power", Parameters.LightPower), 0.0f, 5.0f);
    Parameters.Instances[0].Visible = Parameters.Enabled;
    Parameters.Instances[0].PhaseDays = std::clamp(Parameters.PrimaryPhase, 0.0f, 1.0f) * 29.53f;
    Parameters.Instances[1].Visible = Parameters.SecondaryMoonEnabled;
    if (const auto* Instances = InTable["instances"].as_array())
    {
        for (size_t Index = 0; Index < std::min(Instances->size(), Parameters.Instances.size()); ++Index)
        {
            const auto* Entry = (*Instances)[Index].as_table();
            if (Entry == nullptr) continue;
            auto& Moon = Parameters.Instances[Index];
            Moon.Visible = ReadBool(*Entry, "visible", Moon.Visible);
            Moon.FollowSky = ReadBool(*Entry, "follow_sky", Moon.FollowSky);
            Moon.Body = std::min(5u, ReadUint32(*Entry, "body", Moon.Body));
            Moon.PhaseDays = std::clamp(ReadFloat(*Entry, "phase_days", Moon.PhaseDays), 0.0f, 29.53f);
            Moon.AzimuthDegrees = std::clamp(ReadFloat(*Entry, "azimuth_degrees", Moon.AzimuthDegrees), -180.0f, 180.0f);
            Moon.ElevationDegrees = std::clamp(ReadFloat(*Entry, "elevation_degrees", Moon.ElevationDegrees), -90.0f, 90.0f);
            Moon.RollDegrees = std::clamp(ReadFloat(*Entry, "roll_degrees", Moon.RollDegrees), -180.0f, 180.0f);
            Moon.PitchDegrees = std::clamp(ReadFloat(*Entry, "pitch_degrees", Moon.PitchDegrees), -180.0f, 180.0f);
            Moon.SpinDegrees = std::clamp(ReadFloat(*Entry, "spin_degrees", Moon.SpinDegrees), -180.0f, 180.0f);
            Moon.AngularDiameterDegrees = std::clamp(ReadFloat(*Entry, "angular_diameter_degrees", Moon.AngularDiameterDegrees), 0.1f, 180.0f);
            Moon.Brightness = std::clamp(ReadFloat(*Entry, "brightness", Moon.Brightness), 0.0f, 5.0f);
            Moon.Glow = std::clamp(ReadFloat(*Entry, "glow", Moon.Glow), 0.0f, 3.0f);
        }
    }
    Parameters.AdvanceRevision();
}

template <>
void PlanetRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("follow_sky",Parameters.FollowSky);
    WriteFloat3(OutTable,"surface_tint",Parameters.SurfaceTint);
    OutTable.insert_or_assign("azimuth_degrees",double(Parameters.AzimuthDegrees));
    OutTable.insert_or_assign("elevation_degrees",double(Parameters.ElevationDegrees));
    OutTable.insert_or_assign("spin_degrees",double(Parameters.SpinDegrees));
    OutTable.insert_or_assign("tilt_degrees",double(Parameters.TiltDegrees));
    OutTable.insert_or_assign("roll_degrees",double(Parameters.RollDegrees));
    OutTable.insert_or_assign("band_contrast",double(Parameters.BandContrast));
    OutTable.insert_or_assign("surface_detail",double(Parameters.SurfaceDetail));
    OutTable.insert_or_assign("cloud_veil",double(Parameters.CloudVeil));
    OutTable.insert_or_assign("atmosphere_height",double(Parameters.AtmosphereHeight));
    OutTable.insert_or_assign("limb_glow",double(Parameters.LimbGlow));
    OutTable.insert_or_assign("ring_inner_radius",double(Parameters.RingInnerRadius));
    OutTable.insert_or_assign("ring_outer_radius",double(Parameters.RingOuterRadius));
    OutTable.insert_or_assign("ring_optical_depth",double(Parameters.RingOpticalDepth));

    OutTable.insert_or_assign("body", static_cast<int64_t>(Parameters.Body));
    OutTable.insert_or_assign("apparent_diameter_deg", static_cast<double>(Parameters.ApparentDiameterDegrees));
    OutTable.insert_or_assign("brightness", static_cast<double>(Parameters.Brightness));
    OutTable.insert_or_assign("ring_system", Parameters.RingSystem);
    OutTable.insert_or_assign("atmosphere_shell", Parameters.AtmosphereShell);
}

template <>
void PlanetRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.Enabled                 = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.Body                    = std::min<uint32_t>(ReadUint32(InTable, "body", Parameters.Body), 3u);
    Parameters.ApparentDiameterDegrees = ReadFloat(InTable, "apparent_diameter_deg", Parameters.ApparentDiameterDegrees);
    Parameters.Brightness              = ReadFloat(InTable, "brightness", Parameters.Brightness);
    Parameters.RingSystem              = ReadBool(InTable, "ring_system", Parameters.RingSystem);
    Parameters.AtmosphereShell         = ReadBool(InTable, "atmosphere_shell", Parameters.AtmosphereShell);
    Parameters.FollowSky = ReadBool(InTable,"follow_sky",Parameters.FollowSky);
    ReadFloat3(InTable,"surface_tint",Parameters.SurfaceTint);
    Parameters.AzimuthDegrees = ReadFloat(InTable,"azimuth_degrees",Parameters.AzimuthDegrees);
    Parameters.ElevationDegrees = ReadFloat(InTable,"elevation_degrees",Parameters.ElevationDegrees);
    Parameters.SpinDegrees = ReadFloat(InTable,"spin_degrees",Parameters.SpinDegrees);
    Parameters.TiltDegrees = ReadFloat(InTable,"tilt_degrees",Parameters.TiltDegrees);
    Parameters.RollDegrees = ReadFloat(InTable,"roll_degrees",Parameters.RollDegrees);
    Parameters.BandContrast = ReadFloat(InTable,"band_contrast",Parameters.BandContrast);
    Parameters.SurfaceDetail = ReadFloat(InTable,"surface_detail",Parameters.SurfaceDetail);
    Parameters.CloudVeil = ReadFloat(InTable,"cloud_veil",Parameters.CloudVeil);
    Parameters.AtmosphereHeight = ReadFloat(InTable,"atmosphere_height",Parameters.AtmosphereHeight);
    Parameters.LimbGlow = ReadFloat(InTable,"limb_glow",Parameters.LimbGlow);
    Parameters.RingInnerRadius = ReadFloat(InTable,"ring_inner_radius",Parameters.RingInnerRadius);
    Parameters.RingOuterRadius = ReadFloat(InTable,"ring_outer_radius",Parameters.RingOuterRadius);
    Parameters.RingOpticalDepth = ReadFloat(InTable,"ring_optical_depth",Parameters.RingOpticalDepth);
    Parameters.AdvanceRevision();
}

template <>
void FogRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("aerial_only", Parameters.AerialOnly);
    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("local_volume", Parameters.LocalVolume);
    OutTable.insert_or_assign("base_density", static_cast<double>(Parameters.BaseDensity));
    OutTable.insert_or_assign("height_falloff", static_cast<double>(Parameters.HeightFalloff));
    OutTable.insert_or_assign("base_altitude_m", static_cast<double>(Parameters.BaseAltitudeMetres));
    OutTable.insert_or_assign("anisotropy", static_cast<double>(Parameters.PhaseAnisotropy));
    OutTable.insert_or_assign("start_distance_m", static_cast<double>(Parameters.StartDistanceMetres));
    OutTable.insert_or_assign("froxel_grid", Parameters.VolumetricFroxelGrid);
    OutTable.insert_or_assign("ray_shadows", Parameters.ReceiveRayShadow);
    WriteFloat3(OutTable, "tint_linear", Parameters.TintLinear);
    OutTable.insert_or_assign("sun_scatter", static_cast<double>(Parameters.SunScatter));
    OutTable.insert_or_assign("aerial_enabled", Parameters.AerialEnabled);
    OutTable.insert_or_assign("aerial_density", static_cast<double>(Parameters.AerialDensity));
    OutTable.insert_or_assign("aerial_start_metres", static_cast<double>(Parameters.AerialStartMetres));
    OutTable.insert_or_assign("aerial_mie_blend", static_cast<double>(Parameters.AerialMieBlend));
    OutTable.insert_or_assign("bounds_shape", static_cast<int64_t>(Parameters.BoundsShape));
    WriteFloat3(OutTable, "centre_metres", Parameters.CentreMetres);
    WriteFloat3(OutTable, "half_extents_metres", Parameters.HalfExtentsMetres);
    OutTable.insert_or_assign("density_falloff", Parameters.DensityFalloff);
    OutTable.insert_or_assign("edge_softness", static_cast<double>(Parameters.EdgeSoftness));
    OutTable.insert_or_assign("local_density", static_cast<double>(Parameters.LocalDensity));
    OutTable.insert_or_assign("heterogeneity", static_cast<double>(Parameters.Heterogeneity));
    OutTable.insert_or_assign("noise_scale_metres", static_cast<double>(Parameters.NoiseScaleMetres));
    OutTable.insert_or_assign("independent_wind_mps", static_cast<double>(Parameters.IndependentWindMps));
    OutTable.insert_or_assign("follow_wind", Parameters.FollowWind);
    WriteFloat3(OutTable, "albedo", Parameters.Albedo);
    OutTable.insert_or_assign("absorption", static_cast<double>(Parameters.Absorption));
    OutTable.insert_or_assign("self_shadow", Parameters.SelfShadow);
    OutTable.insert_or_assign("local_samples", static_cast<int64_t>(Parameters.LocalSamples));
}

template <>
void FogRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.AerialOnly = ReadBool(InTable, "aerial_only", Parameters.AerialOnly);
    Parameters.Enabled       = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.LocalVolume   = ReadBool(InTable, "local_volume", Parameters.LocalVolume);
    Parameters.BaseDensity   = ReadFloat(InTable, "base_density", Parameters.BaseDensity);
    Parameters.HeightFalloff = ReadFloat(InTable, "height_falloff", Parameters.HeightFalloff);
    Parameters.BaseAltitudeMetres = ReadFloat(InTable, "base_altitude_m", Parameters.BaseAltitudeMetres);
    Parameters.PhaseAnisotropy = ReadFloat(InTable, "anisotropy", Parameters.PhaseAnisotropy);
    Parameters.StartDistanceMetres = ReadFloat(InTable, "start_distance_m", Parameters.StartDistanceMetres);
    Parameters.VolumetricFroxelGrid = ReadBool(InTable, "froxel_grid", Parameters.VolumetricFroxelGrid);
    Parameters.ReceiveRayShadow = ReadBool(InTable, "ray_shadows", Parameters.ReceiveRayShadow);
    ReadFloat3(InTable, "tint_linear", Parameters.TintLinear);
    Parameters.SunScatter = ReadFloat(InTable, "sun_scatter", Parameters.SunScatter);
    Parameters.AerialEnabled = ReadBool(InTable, "aerial_enabled", Parameters.AerialEnabled);
    Parameters.AerialDensity = ReadFloat(InTable, "aerial_density", Parameters.AerialDensity);
    Parameters.AerialStartMetres = ReadFloat(InTable, "aerial_start_metres", Parameters.AerialStartMetres);
    Parameters.AerialMieBlend = ReadFloat(InTable, "aerial_mie_blend", Parameters.AerialMieBlend);
    Parameters.BoundsShape = ReadUint32(InTable, "bounds_shape", Parameters.BoundsShape);
    ReadFloat3(InTable, "centre_metres", Parameters.CentreMetres);
    ReadFloat3(InTable, "half_extents_metres", Parameters.HalfExtentsMetres);
    Parameters.DensityFalloff = ReadBool(InTable, "density_falloff", Parameters.DensityFalloff);
    Parameters.EdgeSoftness = ReadFloat(InTable, "edge_softness", Parameters.EdgeSoftness);
    Parameters.LocalDensity = ReadFloat(InTable, "local_density", Parameters.LocalDensity);
    Parameters.Heterogeneity = ReadFloat(InTable, "heterogeneity", Parameters.Heterogeneity);
    Parameters.NoiseScaleMetres = ReadFloat(InTable, "noise_scale_metres", Parameters.NoiseScaleMetres);
    Parameters.IndependentWindMps = ReadFloat(InTable, "independent_wind_mps", Parameters.IndependentWindMps);
    Parameters.FollowWind = ReadBool(InTable, "follow_wind", Parameters.FollowWind);
    ReadFloat3(InTable, "albedo", Parameters.Albedo);
    Parameters.Absorption = ReadFloat(InTable, "absorption", Parameters.Absorption);
    Parameters.SelfShadow = ReadBool(InTable, "self_shadow", Parameters.SelfShadow);
    Parameters.LocalSamples = ReadUint32(InTable, "local_samples", Parameters.LocalSamples);
    Parameters.AdvanceRevision();
}

template <>
void LightningRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("strikes_per_minute", static_cast<double>(Parameters.StrikesPerMinute));
    OutTable.insert_or_assign("auto_discharge", Parameters.AutoStormDischarge);
    OutTable.insert_or_assign("branching_factor", static_cast<double>(Parameters.BranchingFactor));
    OutTable.insert_or_assign("peak_current_ka", static_cast<double>(Parameters.PeakCurrentKiloamps));
    OutTable.insert_or_assign("cloud_sheet_luminance", static_cast<double>(Parameters.CloudSheetLuminance));
    OutTable.insert_or_assign("acoustic_shockwave", Parameters.AcousticShockwave);
    OutTable.insert_or_assign("triggered_strike_count", static_cast<int64_t>(Parameters.TriggeredStrikeCount));
    OutTable.insert_or_assign("discharge", static_cast<int64_t>(Parameters.Discharge));
    OutTable.insert_or_assign("cloud_source", static_cast<int64_t>(Parameters.CloudSource));
    OutTable.insert_or_assign("distance_metres", static_cast<double>(Parameters.DistanceMetres));
    OutTable.insert_or_assign("minimum_interval_seconds", static_cast<double>(Parameters.MinimumIntervalSeconds));
    OutTable.insert_or_assign("maximum_interval_seconds", static_cast<double>(Parameters.MaximumIntervalSeconds));
    OutTable.insert_or_assign("channel_span_metres", static_cast<double>(Parameters.ChannelSpanMetres));
    OutTable.insert_or_assign("bolt_brightness", static_cast<double>(Parameters.BoltBrightness));
    OutTable.insert_or_assign("cloud_flash_strength", static_cast<double>(Parameters.CloudFlashStrength));
    OutTable.insert_or_assign("flash_radius_metres", static_cast<double>(Parameters.FlashRadiusMetres));
}

template <>
void LightningRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.Enabled            = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.StrikesPerMinute   = ReadFloat(InTable, "strikes_per_minute", Parameters.StrikesPerMinute);
    Parameters.AutoStormDischarge = ReadBool(InTable, "auto_discharge", Parameters.AutoStormDischarge);
    Parameters.BranchingFactor = ReadFloat(InTable, "branching_factor", Parameters.BranchingFactor);
    Parameters.PeakCurrentKiloamps = ReadFloat(InTable, "peak_current_ka", Parameters.PeakCurrentKiloamps);
    Parameters.CloudSheetLuminance = ReadFloat(InTable, "cloud_sheet_luminance", Parameters.CloudSheetLuminance);
    Parameters.AcousticShockwave = ReadBool(InTable, "acoustic_shockwave", Parameters.AcousticShockwave);
    Parameters.TriggeredStrikeCount = ReadUint32(InTable, "triggered_strike_count", Parameters.TriggeredStrikeCount);
    Parameters.Discharge = ReadUint32(InTable, "discharge", Parameters.Discharge);
    Parameters.CloudSource = ReadUint32(InTable, "cloud_source", Parameters.CloudSource);
    Parameters.DistanceMetres = ReadFloat(InTable, "distance_metres", Parameters.DistanceMetres);
    Parameters.MinimumIntervalSeconds = ReadFloat(InTable, "minimum_interval_seconds", Parameters.MinimumIntervalSeconds);
    Parameters.MaximumIntervalSeconds = ReadFloat(InTable, "maximum_interval_seconds", Parameters.MaximumIntervalSeconds);
    Parameters.ChannelSpanMetres = ReadFloat(InTable, "channel_span_metres", Parameters.ChannelSpanMetres);
    Parameters.BoltBrightness = ReadFloat(InTable, "bolt_brightness", Parameters.BoltBrightness);
    Parameters.CloudFlashStrength = ReadFloat(InTable, "cloud_flash_strength", Parameters.CloudFlashStrength);
    Parameters.FlashRadiusMetres = ReadFloat(InTable, "flash_radius_metres", Parameters.FlashRadiusMetres);
    Parameters.AdvanceRevision();
}

template <>
void ForceFieldRecord::SerializeConfiguration(toml::table& Table) const
{
    Table.insert_or_assign("enabled",Parameters.Enabled);
    Table.insert_or_assign("visualize",Parameters.Visualize);
    Table.insert_or_assign("include_turbulence",Parameters.IncludeTurbulence);
    Table.insert_or_assign("grid_resolution",int64_t(Parameters.GridResolution));
    Table.insert_or_assign("arrow_seconds",double(Parameters.ArrowSeconds));
    WriteFloat3(Table,"centre_metres",Parameters.CentreMetres);
    WriteFloat3(Table,"half_extents_metres",Parameters.HalfExtentsMetres);
    Table.insert_or_assign("cascades", int64_t(Parameters.Cascades));
    Table.insert_or_assign("finest_cell_metres", double(Parameters.FinestCellMetres));
    Table.insert_or_assign("cells_per_side", int64_t(Parameters.CellsPerSide));
    Table.insert_or_assign("vertical_cells", int64_t(Parameters.VerticalCells));
    Table.insert_or_assign("extent_metres", double(Parameters.ExtentMetres));
    Table.insert_or_assign("receivers", WriteReceivers(Parameters.Receivers));
    Table.insert_or_assign("view_contribution", NameChoice(Parameters.ViewContribution, ContributionNames));
    Table.insert_or_assign("view_quantity", NameChoice(Parameters.Quantity, QuantityNames));
    Table.insert_or_assign("view_projection", NameChoice(Parameters.Projection, ProjectionNames));
    Table.insert_or_assign("automatic_scale", Parameters.AutomaticScale);
    Table.insert_or_assign("fixed_scale", double(Parameters.FixedScale));
    Table.insert_or_assign("heatmap", Parameters.Heatmap);
    Table.insert_or_assign("smooth_heatmap", Parameters.SmoothHeatmap);
    Table.insert_or_assign("arrows", Parameters.Arrows);
    Table.insert_or_assign("tracers", Parameters.Tracers);
    Table.insert_or_assign("show_cascades", Parameters.ShowCascades);
    Table.insert_or_assign("labels", Parameters.Labels);
    Table.insert_or_assign("scene_overlay", Parameters.SceneOverlay);
    Table.insert_or_assign("slice_height_metres", double(Parameters.SliceHeightMetres));
    Table.insert_or_assign("heatmap_opacity", double(Parameters.HeatmapOpacity));
    Table.insert_or_assign("arrow_spacing_pixels", double(Parameters.ArrowSpacingPixels));
    Table.insert_or_assign("tracer_count", int64_t(Parameters.TracerCount));
    Table.insert_or_assign("cell_pixels", double(Parameters.CellPixels));
    Table.insert_or_assign("timeline_duration_seconds", double(Parameters.TimelineDurationSeconds));
    Table.insert_or_assign("timeline_rate", double(Parameters.TimelineRate));
    Table.insert_or_assign("timeline_playing", Parameters.TimelinePlaying);
}

template <>
void ForceFieldRecord::DeserializeConfiguration(const toml::table& Table)
{
    Parameters.Enabled = ReadBool(Table,"enabled",Parameters.Enabled);
    Parameters.Visualize = ReadBool(Table,"visualize",Parameters.Visualize);
    Parameters.IncludeTurbulence = ReadBool(Table,"include_turbulence",Parameters.IncludeTurbulence);
    Parameters.GridResolution = std::clamp(ReadUint32(Table,"grid_resolution",Parameters.GridResolution),3u,8u);
    Parameters.ArrowSeconds = std::clamp(ReadFloat(Table,"arrow_seconds",Parameters.ArrowSeconds),.05f,2.0f);
    ReadFloat3(Table,"centre_metres",Parameters.CentreMetres);
    ReadFloat3(Table,"half_extents_metres",Parameters.HalfExtentsMetres);
    for (float& Half : Parameters.HalfExtentsMetres) Half = std::clamp(Half,1.0f,1000.0f);
    Parameters.Cascades = std::clamp(ReadUint32(Table, "cascades", Parameters.Cascades), 1u, kMaximumForceCascades);
    Parameters.FinestCellMetres = std::clamp(ReadFloat(Table, "finest_cell_metres", Parameters.FinestCellMetres), 0.25f, 1000.0f);
    Parameters.CellsPerSide = std::clamp(ReadUint32(Table, "cells_per_side", Parameters.CellsPerSide), 16u, 512u);
    Parameters.VerticalCells = std::clamp(ReadUint32(Table, "vertical_cells", Parameters.VerticalCells), 4u, 128u);
    Parameters.ExtentMetres = std::clamp(ReadFloat(Table, "extent_metres", Parameters.ExtentMetres), 100.0f, 400000.0f);
    Parameters.Receivers = ReadReceivers(Table, "receivers", Parameters.Receivers);
    Parameters.ViewContribution = ReadChoice(Table, "view_contribution", Parameters.ViewContribution, ContributionNames);
    Parameters.Quantity = ReadChoice(Table, "view_quantity", Parameters.Quantity, QuantityNames);
    Parameters.Projection = ReadChoice(Table, "view_projection", Parameters.Projection, ProjectionNames);
    Parameters.AutomaticScale = ReadBool(Table, "automatic_scale", Parameters.AutomaticScale);
    Parameters.FixedScale = std::clamp(ReadFloat(Table, "fixed_scale", Parameters.FixedScale), 0.01f, 1000.0f);
    Parameters.Heatmap = ReadBool(Table, "heatmap", Parameters.Heatmap);
    Parameters.SmoothHeatmap = ReadBool(Table, "smooth_heatmap", Parameters.SmoothHeatmap);
    Parameters.Arrows = ReadBool(Table, "arrows", Parameters.Arrows);
    Parameters.Tracers = ReadBool(Table, "tracers", Parameters.Tracers);
    Parameters.ShowCascades = ReadBool(Table, "show_cascades", Parameters.ShowCascades);
    Parameters.Labels = ReadBool(Table, "labels", Parameters.Labels);
    Parameters.SceneOverlay = ReadBool(Table, "scene_overlay", Parameters.SceneOverlay);
    Parameters.SliceHeightMetres = std::clamp(ReadFloat(Table, "slice_height_metres", Parameters.SliceHeightMetres), 0.0f, 12000.0f);
    Parameters.HeatmapOpacity = std::max(0.0f, ReadFloat(Table, "heatmap_opacity", Parameters.HeatmapOpacity));   // 📝 saturates at 1 where drawn
    Parameters.ArrowSpacingPixels = std::clamp(ReadFloat(Table, "arrow_spacing_pixels", Parameters.ArrowSpacingPixels), 16.0f, 120.0f);
    Parameters.TracerCount = std::clamp(ReadUint32(Table, "tracer_count", Parameters.TracerCount), 0u, 2000u);
    Parameters.CellPixels = std::clamp(ReadFloat(Table, "cell_pixels", Parameters.CellPixels), 2.0f, 24.0f);
    Parameters.TimelineDurationSeconds = std::clamp(ReadFloat(Table, "timeline_duration_seconds", Parameters.TimelineDurationSeconds), 5.0f, 3600.0f);
    Parameters.TimelineRate = std::clamp(ReadFloat(Table, "timeline_rate", Parameters.TimelineRate), 0.0f, 16.0f);
    Parameters.TimelinePlaying = ReadBool(Table, "timeline_playing", Parameters.TimelinePlaying);
    Parameters.AdvanceRevision();
}

template <>
void WindRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("direction_deg", static_cast<double>(Parameters.DirectionDegrees));
    OutTable.insert_or_assign("speed_mps", static_cast<double>(Parameters.SpeedMetresPerSecond));
    OutTable.insert_or_assign("turbulence", static_cast<double>(Parameters.CurlNoiseTurbulence));
    OutTable.insert_or_assign("gust_amplitude", static_cast<double>(Parameters.GustAmplitude));
    OutTable.insert_or_assign("curl_frequency_hz", static_cast<double>(Parameters.CurlFrequencyHz));
    OutTable.insert_or_assign("advect_clouds", Parameters.AdvectClouds);
    OutTable.insert_or_assign("advect_plumes", Parameters.AdvectPlumes);
    OutTable.insert_or_assign("advect_particles", Parameters.AdvectParticles);
    OutTable.insert_or_assign("gust_frequency_hz",double(Parameters.GustFrequencyHz));
    OutTable.insert_or_assign("turbulence_scale_metres",double(Parameters.TurbulenceScaleMetres));
    OutTable.insert_or_assign("gust_strength", static_cast<double>(Parameters.GustStrength));
    OutTable.insert_or_assign("shear_per_kilometre", static_cast<double>(Parameters.ShearPerKilometre));
    OutTable.insert_or_assign("veer_degrees_per_kilometre", static_cast<double>(Parameters.VeerDegreesPerKilometre));
    OutTable.insert_or_assign("ground_height_metres", static_cast<double>(Parameters.GroundHeightMetres));
    OutTable.insert_or_assign("seed", static_cast<int64_t>(Parameters.Seed));
    OutTable.insert_or_assign("mesoscale_scale_metres", static_cast<double>(Parameters.MesoscaleScaleMetres));
    OutTable.insert_or_assign("mesoscale_strength", static_cast<double>(Parameters.MesoscaleStrength));
}

template <>
void WindRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.Enabled              = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.DirectionDegrees     = ReadFloat(InTable, "direction_deg", Parameters.DirectionDegrees);
    Parameters.SpeedMetresPerSecond = ReadFloat(InTable, "speed_mps", Parameters.SpeedMetresPerSecond);
    Parameters.CurlNoiseTurbulence  = ReadFloat(InTable, "turbulence", Parameters.CurlNoiseTurbulence);
    Parameters.GustAmplitude = ReadFloat(InTable, "gust_amplitude", Parameters.GustAmplitude);
    Parameters.CurlFrequencyHz = ReadFloat(InTable, "curl_frequency_hz", Parameters.CurlFrequencyHz);
    Parameters.AdvectClouds = ReadBool(InTable, "advect_clouds", Parameters.AdvectClouds);
    Parameters.AdvectPlumes = ReadBool(InTable, "advect_plumes", Parameters.AdvectPlumes);
    Parameters.AdvectParticles = ReadBool(InTable, "advect_particles", Parameters.AdvectParticles);
    Parameters.GustFrequencyHz = ReadFloat(InTable,"gust_frequency_hz",Parameters.GustFrequencyHz);
    Parameters.TurbulenceScaleMetres = ReadFloat(InTable,"turbulence_scale_metres",Parameters.TurbulenceScaleMetres);
    Parameters.GustStrength = ReadFloat(InTable, "gust_strength", Parameters.GustStrength);
    Parameters.ShearPerKilometre = ReadFloat(InTable, "shear_per_kilometre", Parameters.ShearPerKilometre);
    Parameters.VeerDegreesPerKilometre = ReadFloat(InTable, "veer_degrees_per_kilometre", Parameters.VeerDegreesPerKilometre);
    Parameters.GroundHeightMetres = ReadFloat(InTable, "ground_height_metres", Parameters.GroundHeightMetres);
    Parameters.Seed = ReadUint32(InTable, "seed", Parameters.Seed);
    Parameters.MesoscaleScaleMetres = std::clamp(ReadFloat(InTable, "mesoscale_scale_metres", Parameters.MesoscaleScaleMetres), 0.0f, 200000.0f);
    Parameters.MesoscaleStrength = std::clamp(ReadFloat(InTable, "mesoscale_strength", Parameters.MesoscaleStrength), 0.0f, 1.0f);
    Parameters.AdvanceRevision();
}

// 📝 A ForceSource record names its category and keeps only what that category reads (its descriptor's parameters and
//    bearings), the reach, the timing and the receivers; everything else returns to the category's defaults on load.
template <>
void ForceSourceRecord::SerializeConfiguration(toml::table& Table) const
{
    const ForceSourceDescriptor& Descriptor = ResolveForceSourceDescriptor(Parameters.Category);
    Table.insert_or_assign("enabled", Parameters.Enabled);
    Table.insert_or_assign("category", std::string(Descriptor.Identifier));
    Table.insert_or_assign("contribution", std::string(ResolveForceContributionDescriptor(Descriptor.Contribution).Identifier));
    WriteDoubles(Table, "centre_metres", Parameters.CentreMetres, 3);
    Table.insert_or_assign("reach", std::string(ResolveForceReachIdentifier(Parameters.Reach)));
    Table.insert_or_assign("reach_metres", Parameters.ReachMetres);
    Table.insert_or_assign("falloff", std::string(ResolveForceFalloffIdentifier(Parameters.Falloff)));
    for (const ForceParameterDescriptor& Parameter : Descriptor.Parameters)
        Table.insert_or_assign(Parameter.Identifier, Parameters.*(Parameter.Member));
    for (const ForceBearingDescriptor& Bearing : Descriptor.Bearings)
        Table.insert_or_assign(Bearing.Identifier, Parameters.*(Bearing.Member));
    if (Descriptor.Spin) Table.insert_or_assign("spin", std::string(Parameters.Spin < 0 ? "anticyclonic" : "cyclonic"));
    if (Parameters.Reach == ForceReach::Column || Parameters.Reach == ForceReach::Box)
        Table.insert_or_assign("height_metres", Parameters.HeightMetres);
    if (Parameters.Reach == ForceReach::Box && Parameters.Category != ForceSourceCategory::JetCorridor &&
        Parameters.Category != ForceSourceCategory::VehicleWake)
    {
        WriteDoubles(Table, "half_extents_metres", Parameters.HalfExtentsMetres, 2);
        Table.insert_or_assign("bearing_deg", Parameters.BearingDegrees);
    }
    Table.insert_or_assign("activation", std::string(ResolveForceActivationIdentifier(Parameters.Activation)));
    Table.insert_or_assign("begins_seconds", Parameters.BeginsSeconds);
    Table.insert_or_assign("lasts_seconds", Parameters.LastsSeconds);
    Table.insert_or_assign("repeats_seconds", Parameters.RepeatsSeconds);
    Table.insert_or_assign("ramp_in_seconds", Parameters.RampInSeconds);
    Table.insert_or_assign("ramp_out_seconds", Parameters.RampOutSeconds);
    Table.insert_or_assign("acts_on", WriteReceivers(Parameters.ActsOn));
}

template <>
void ForceSourceRecord::DeserializeConfiguration(const toml::table& Table)
{
    const auto                   CategoryName = Table["category"].value<std::string>();
    const ForceSourceDescriptor* Descriptor   = CategoryName ? ResolveForceSourceDescriptor(*CategoryName) : nullptr;
    if (Descriptor)
    {
        const uint64_t Revision = Parameters.Revision;
        Parameters              = ConstructForceSource(Descriptor->Category);
        Parameters.Revision     = Revision;
    }
    const ForceSourceDescriptor& Active = ResolveForceSourceDescriptor(Parameters.Category);
    Parameters.Enabled = ReadBool(Table, "enabled", Parameters.Enabled);
    ReadDoubles(Table, "centre_metres", Parameters.CentreMetres, 3);
    if (const auto Name = Table["reach"].value<std::string>()) Parameters.Reach = ParseForceReach(*Name, Parameters.Reach);
    Parameters.ReachMetres = std::clamp(ReadDouble(Table, "reach_metres", Parameters.ReachMetres), 0.1, 400000.0);
    if (const auto Name = Table["falloff"].value<std::string>()) Parameters.Falloff = ParseForceFalloff(*Name, Parameters.Falloff);
    for (const ForceParameterDescriptor& Parameter : Active.Parameters)
        Parameters.*(Parameter.Member) = std::clamp(ReadDouble(Table, Parameter.Identifier, Parameters.*(Parameter.Member)),
                                                    std::min(Parameter.Minimum, 0.0), Parameter.Maximum * 4.0);
    for (const ForceBearingDescriptor& Bearing : Active.Bearings)
        Parameters.*(Bearing.Member) = ReadDouble(Table, Bearing.Identifier, Parameters.*(Bearing.Member));
    if (const auto Spin = Table["spin"].value<std::string>()) Parameters.Spin = *Spin == "anticyclonic" ? -1.0 : 1.0;
    Parameters.HeightMetres = std::clamp(ReadDouble(Table, "height_metres", Parameters.HeightMetres), 0.1, 40000.0);
    ReadDoubles(Table, "half_extents_metres", Parameters.HalfExtentsMetres, 2);
    Parameters.BearingDegrees = ReadDouble(Table, "bearing_deg", Parameters.BearingDegrees);
    if (const auto Name = Table["activation"].value<std::string>()) Parameters.Activation = ParseForceActivation(*Name, Parameters.Activation);
    Parameters.BeginsSeconds  = std::max(0.0, ReadDouble(Table, "begins_seconds", Parameters.BeginsSeconds));
    Parameters.LastsSeconds   = std::max(0.0, ReadDouble(Table, "lasts_seconds", Parameters.LastsSeconds));
    Parameters.RepeatsSeconds = std::max(0.0, ReadDouble(Table, "repeats_seconds", Parameters.RepeatsSeconds));
    Parameters.RampInSeconds  = std::max(0.0, ReadDouble(Table, "ramp_in_seconds", Parameters.RampInSeconds));
    Parameters.RampOutSeconds = std::max(0.0, ReadDouble(Table, "ramp_out_seconds", Parameters.RampOutSeconds));
    Parameters.ActsOn = ReadReceivers(Table, "acts_on", Parameters.ActsOn);
    Parameters.AdvanceRevision();
}

template <>
void FluidPlumeRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("buoyancy_lift", static_cast<double>(Parameters.BuoyancyLift));
    OutTable.insert_or_assign("vorticity", static_cast<double>(Parameters.VorticityConfinement));
    OutTable.insert_or_assign("blackbody_k", static_cast<double>(Parameters.BlackbodyCoreKelvin));
}

template <>
void FluidPlumeRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.Enabled              = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.BuoyancyLift         = ReadFloat(InTable, "buoyancy_lift", Parameters.BuoyancyLift);
    Parameters.VorticityConfinement = ReadFloat(InTable, "vorticity", Parameters.VorticityConfinement);
    Parameters.BlackbodyCoreKelvin  = ReadFloat(InTable, "blackbody_k", Parameters.BlackbodyCoreKelvin);
    Parameters.AdvanceRevision();
}

template <>
void FractureRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("voronoi_cells", static_cast<int64_t>(Parameters.VoronoiCellCount));
    OutTable.insert_or_assign("impulse_threshold_ns", static_cast<double>(Parameters.ImpulseThresholdNs));
}

template <>
void FractureRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.Enabled            = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.VoronoiCellCount   = ReadUint32(InTable, "voronoi_cells", Parameters.VoronoiCellCount);
    Parameters.ImpulseThresholdNs = ReadFloat(InTable, "impulse_threshold_ns", Parameters.ImpulseThresholdNs);
    Parameters.AdvanceRevision();
}

template <>
void ParticleRecord::SerializeConfiguration(toml::table& OutTable) const
{
    OutTable.insert_or_assign("enabled", Parameters.Enabled);
    OutTable.insert_or_assign("spark_rate", static_cast<double>(Parameters.SparkRatePerSecond));
    OutTable.insert_or_assign("rain_density", static_cast<double>(Parameters.RainDropDensity));
    OutTable.insert_or_assign("snow_density", static_cast<double>(Parameters.SnowFlakeDensity));
}

template <>
void ParticleRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.Enabled            = ReadBool(InTable, "enabled", Parameters.Enabled);
    Parameters.SparkRatePerSecond = ReadFloat(InTable, "spark_rate", Parameters.SparkRatePerSecond);
    Parameters.RainDropDensity    = ReadFloat(InTable, "rain_density", Parameters.RainDropDensity);
    Parameters.SnowFlakeDensity   = ReadFloat(InTable, "snow_density", Parameters.SnowFlakeDensity);
    Parameters.AdvanceRevision();
}

namespace
{

// 📝 Rounded to 0.1 mm / 0.0001: the record keeps floats, and the level file should read the figures the inspector shows.
void WriteRoundedFloat3(toml::table& OutTable, std::string_view Key, const float Values[3])
{
    toml::array Array;
    for (int Axis = 0; Axis < 3; ++Axis) Array.push_back(std::round(static_cast<double>(Values[Axis]) * 1.0e4) / 1.0e4);
    OutTable.insert_or_assign(Key, std::move(Array));
}

} // namespace

template <>
void GeometryRecord::SerializeConfiguration(toml::table& OutTable) const
{
    toml::array Stems;
    for (const std::string& Stem : Parameters.OccupantStems) Stems.push_back(Stem);
    OutTable.insert_or_assign("occupants", std::move(Stems));
    if (Parameters.OccupantFirst != 0) OutTable.insert_or_assign("occupant_first", static_cast<int64_t>(Parameters.OccupantFirst));
    if (Parameters.OccupantCount != 0) OutTable.insert_or_assign("occupant_count", static_cast<int64_t>(Parameters.OccupantCount));
    if (Parameters.BaseCopies != 0) OutTable.insert_or_assign("base_copies", static_cast<int64_t>(Parameters.BaseCopies));
    if (!Parameters.ClaimsArchive) OutTable.insert_or_assign("claims_archive", false);
    if (!Parameters.ShapeName.empty()) OutTable.insert_or_assign("shape", Parameters.ShapeName);
    if (Parameters.ArchiveOriginAuthored)
    {
        WriteRoundedFloat3(OutTable, "archive_origin_m", Parameters.ArchiveOriginMetres);
        OutTable.insert_or_assign("archive_yaw_deg", std::round(static_cast<double>(Parameters.ArchiveYawDegrees) * 1.0e4) / 1.0e4);
    }
    OutTable.insert_or_assign("group", Parameters.GroupName);
    OutTable.insert_or_assign("glyph", Parameters.GlyphName);
    if (!Parameters.Notes.empty()) OutTable.insert_or_assign("notes", Parameters.Notes);
    OutTable.insert_or_assign("visible", Parameters.Visible);
    OutTable.insert_or_assign("dynamic", Parameters.Dynamic);
    OutTable.insert_or_assign("cast_shadows", Parameters.CastShadows);
    OutTable.insert_or_assign("global_illumination", Parameters.GlobalIllumination);
    OutTable.insert_or_assign("physics", Parameters.Physics);
    OutTable.insert_or_assign("locked", Parameters.Locked);
    WriteRoundedFloat3(OutTable, "position_m", Parameters.Spatial.PositionMetres);
    WriteRoundedFloat3(OutTable, "orientation_deg", Parameters.Spatial.OrientationDegrees);
    WriteRoundedFloat3(OutTable, "scale", Parameters.Spatial.Scale);
    OutTable.insert_or_assign("fracture", Parameters.Fracture.Enabled);
    if (Parameters.Fracture.Enabled)
    {
        OutTable.insert_or_assign("voronoi_cells", static_cast<int64_t>(Parameters.Fracture.VoronoiCellCount));
        OutTable.insert_or_assign("impulse_threshold_ns", static_cast<double>(Parameters.Fracture.ImpulseThresholdNs));
    }
    toml::array Stack;
    for (const ArrayModifierConfiguration& Modifier : Parameters.Modifiers)
    {
        toml::table Entry;
        Entry.insert_or_assign("type", "Array");
        Entry.insert_or_assign("label", Modifier.Label);
        Entry.insert_or_assign("enabled", Modifier.Enabled);
        Entry.insert_or_assign("expanded", Modifier.Expanded);
        Entry.insert_or_assign("count", static_cast<int64_t>(Modifier.Count));
        Entry.insert_or_assign("relative_offset", Modifier.RelativeOffset);
        WriteRoundedFloat3(Entry, "relative_factor", Modifier.RelativeFactor);
        Entry.insert_or_assign("constant_offset", Modifier.ConstantOffset);
        WriteRoundedFloat3(Entry, "constant_m", Modifier.ConstantMetres);
        Entry.insert_or_assign("rotation_step_deg", std::round(static_cast<double>(Modifier.RotationStepDegrees) * 1.0e4) / 1.0e4);
        Entry.insert_or_assign("radial_pivot", Modifier.RadialPivot);
        Stack.push_back(std::move(Entry));
    }
    if (!Stack.empty()) OutTable.insert_or_assign("modifiers", std::move(Stack));
    // 📝 The surfaces the object owns: [[surfaces]] family = n, albedo = [r, g, b], roughness = …, as the shape archives name them.
    toml::array Surfaces;
    for (const SurfaceSlotConfiguration& Slot : Parameters.SurfaceSlots)
    {
        toml::table Entry;
        const auto& Channel = Slot.Channels;
        auto Rounded = [](float Scalar) { return std::round(static_cast<double>(Scalar) * 1.0e5) / 1.0e5; };
        Entry.insert_or_assign("family", static_cast<int64_t>(Slot.Family));
        Entry.insert_or_assign("albedo", toml::array{Rounded(Channel[0]), Rounded(Channel[1]), Rounded(Channel[2])});
        Entry.insert_or_assign("roughness", Rounded(Channel[3]));
        Entry.insert_or_assign("incandescence", toml::array{Rounded(Channel[4]), Rounded(Channel[5]), Rounded(Channel[6])});
        Entry.insert_or_assign("metalness", Rounded(Channel[7]));
        Entry.insert_or_assign("coat", Rounded(Channel[8]));
        Entry.insert_or_assign("coat_roughness", Rounded(Channel[9]));
        Entry.insert_or_assign("specular", Rounded(Channel[10]));
        Entry.insert_or_assign("transmission", Rounded(Channel[11]));
        Surfaces.push_back(std::move(Entry));
    }
    if (!Surfaces.empty()) OutTable.insert_or_assign("surfaces", std::move(Surfaces));
}

template <>
void GeometryRecord::DeserializeConfiguration(const toml::table& InTable)
{
    Parameters.OccupantStems.clear();
    if (const toml::array* Stems = InTable["occupants"].as_array())
        for (const toml::node& Stem : *Stems)
            if (const auto Text = Stem.value<std::string>()) Parameters.OccupantStems.push_back(*Text);
    Parameters.OccupantFirst      = ReadUint32(InTable, "occupant_first", 0u);
    Parameters.OccupantCount      = ReadUint32(InTable, "occupant_count", 0u);
    Parameters.BaseCopies         = ReadUint32(InTable, "base_copies", 0u);
    Parameters.ClaimsArchive      = ReadBool(InTable, "claims_archive", true);
    Parameters.ShapeName          = InTable["shape"].value_or(std::string());
    Parameters.ArchiveOriginAuthored = InTable.contains("archive_origin_m");
    if (Parameters.ArchiveOriginAuthored) ReadFloat3(InTable, "archive_origin_m", Parameters.ArchiveOriginMetres);
    Parameters.ArchiveYawDegrees  = ReadFloat(InTable, "archive_yaw_deg", 0.0f);
    Parameters.GroupName          = InTable["group"].value_or(std::string("Geometry"));
    Parameters.GlyphName          = InTable["glyph"].value_or(std::string("Box"));
    Parameters.Notes              = InTable["notes"].value_or(std::string());
    Parameters.Visible            = ReadBool(InTable, "visible", Parameters.Visible);
    Parameters.Dynamic            = ReadBool(InTable, "dynamic", Parameters.Dynamic);
    Parameters.CastShadows        = ReadBool(InTable, "cast_shadows", Parameters.CastShadows);
    Parameters.GlobalIllumination = ReadBool(InTable, "global_illumination", Parameters.GlobalIllumination);
    Parameters.Physics            = ReadBool(InTable, "physics", Parameters.Physics);
    Parameters.Locked             = ReadBool(InTable, "locked", Parameters.Locked);
    DeserializeSpatialSubtable(InTable, Parameters.Spatial);
    Parameters.Fracture.Enabled            = ReadBool(InTable, "fracture", false);
    Parameters.Fracture.VoronoiCellCount   = ReadUint32(InTable, "voronoi_cells", Parameters.Fracture.VoronoiCellCount);
    Parameters.Fracture.ImpulseThresholdNs = ReadFloat(InTable, "impulse_threshold_ns", Parameters.Fracture.ImpulseThresholdNs);
    Parameters.Modifiers.clear();
    if (const toml::array* Stack = InTable["modifiers"].as_array())
    {
        for (const toml::node& Node : *Stack)
        {
            const toml::table* Entry = Node.as_table();
            if (Entry == nullptr || (*Entry)["type"].value_or(std::string("Array")) != "Array") continue;
            ArrayModifierConfiguration Modifier;
            Modifier.Label               = (*Entry)["label"].value_or(std::string("Array"));
            Modifier.Enabled             = ReadBool(*Entry, "enabled", Modifier.Enabled);
            Modifier.Expanded            = ReadBool(*Entry, "expanded", Modifier.Expanded);
            Modifier.Count               = std::max(1u, ReadUint32(*Entry, "count", Modifier.Count));
            Modifier.RelativeOffset      = ReadBool(*Entry, "relative_offset", Modifier.RelativeOffset);
            ReadFloat3(*Entry, "relative_factor", Modifier.RelativeFactor);
            Modifier.ConstantOffset      = ReadBool(*Entry, "constant_offset", Modifier.ConstantOffset);
            ReadFloat3(*Entry, "constant_m", Modifier.ConstantMetres);
            Modifier.RotationStepDegrees = ReadFloat(*Entry, "rotation_step_deg", Modifier.RotationStepDegrees);
            Modifier.RadialPivot         = ReadBool(*Entry, "radial_pivot", Modifier.RadialPivot);
            Parameters.Modifiers.push_back(std::move(Modifier));
        }
    }
    Parameters.SurfaceSlots.clear();
    if (const toml::array* Surfaces = InTable["surfaces"].as_array())
    {
        for (const toml::node& Node : *Surfaces)
        {
            const toml::table* Entry = Node.as_table();
            if (Entry == nullptr) continue;
            SurfaceSlotConfiguration Slot;
            Slot.Family = ReadUint32(*Entry, "family", 0u);
            auto Read3 = [&](const char* Key, size_t First) {
                if (const toml::array* Values = (*Entry)[Key].as_array())
                    for (size_t Index = 0; Index < 3 && Index < Values->size(); ++Index)
                        Slot.Channels[First + Index] = static_cast<float>((*Values)[Index].value_or(0.0));
            };
            Read3("albedo", 0);
            Read3("incandescence", 4);
            Slot.Channels[3]  = ReadFloat(*Entry, "roughness", 0.5f);
            Slot.Channels[7]  = ReadFloat(*Entry, "metalness", 0.0f);
            Slot.Channels[8]  = ReadFloat(*Entry, "coat", 0.0f);
            Slot.Channels[9]  = ReadFloat(*Entry, "coat_roughness", 0.0f);
            Slot.Channels[10] = ReadFloat(*Entry, "specular", 0.04f);
            Slot.Channels[11] = ReadFloat(*Entry, "transmission", 0.0f);
            Parameters.SurfaceSlots.push_back(Slot);
        }
    }
    ConfigureEditorPresentation(Parameters.GlyphName.c_str(), Parameters.GroupName.c_str(), "Geometry", "");
    Parameters.AdvanceRevision();
}

//------------------------------------------------------------------------------------------------------------------------
//                                      COMPOSABLE MULTI-CONFIGURATION SCENE RECORDS
//------------------------------------------------------------------------------------------------------------------------

SpatialNodeRecord::SpatialNodeRecord(std::string InDisplayName)
    : SceneRecordBase(RecordClassification::Geometry, "SpatialNode", std::move(InDisplayName))
{
    ConfigureEditorPresentation("Target", "Scene objects", "SpatialNode", "#a8bbeb");
#if FRONTIER_EDITOR
    InspectorRuntime::Instance().BindInspector(QueryToken(), std::make_unique<SpatialInspector>(Spatial));
#endif
}

void SpatialNodeRecord::SerializeConfiguration(toml::table& OutTable) const
{
    SerializeSpatialSubtable(OutTable, Spatial);
}

void SpatialNodeRecord::DeserializeConfiguration(const toml::table& InTable)
{
    DeserializeSpatialSubtable(InTable, Spatial);
}

RigidBodyRecord::RigidBodyRecord(std::string InDisplayName)
    : SceneRecordBase(RecordClassification::Geometry, "RigidBody", std::move(InDisplayName))
{
    ConfigureEditorPresentation("Box", "Scene objects", "RigidBody", "#a8bbeb");
#if FRONTIER_EDITOR
    InspectorRuntime::Instance().BindInspector(QueryToken(), std::make_unique<SpatialInspector>(Spatial));
    InspectorRuntime::Instance().AttachInspector(QueryToken(), std::make_unique<PhysicsInspector>(Physics));
    InspectorRuntime::Instance().AttachInspector(QueryToken(), std::make_unique<SurfaceOpticsInspector>(Optics));
#endif
}

void RigidBodyRecord::SerializeConfiguration(toml::table& OutTable) const
{
    SerializeSpatialSubtable(OutTable, Spatial);
    SerializePhysicsSubtable(OutTable, Physics);
    SerializeOpticsSubtable(OutTable, Optics);
}

void RigidBodyRecord::DeserializeConfiguration(const toml::table& InTable)
{
    DeserializeSpatialSubtable(InTable, Spatial);
    DeserializePhysicsSubtable(InTable, Physics);
    DeserializeOpticsSubtable(InTable, Optics);
}

void RigidBodyRecord::StepSimulation(float DeltaSeconds)
{
    if (!Physics.Enabled || !Physics.DynamicBody || DeltaSeconds <= 0.0f)
    {
        return;
    }
    const float Damping = std::max(0.0f, 1.0f - Physics.LinearDamping * DeltaSeconds);
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        Physics.LinearVelocityMps[Axis] *= Damping;
        Spatial.PositionMetres[Axis] += Physics.LinearVelocityMps[Axis] * DeltaSeconds;
    }
    if (Physics.GravityCoupled && Spatial.PositionMetres[1] > 0.5f)
    {
        Physics.LinearVelocityMps[1] -= 9.80665f * DeltaSeconds;
        Spatial.PositionMetres[1] += Physics.LinearVelocityMps[1] * DeltaSeconds;
        if (Spatial.PositionMetres[1] <= 0.5f)
        {
            Spatial.PositionMetres[1]    = 0.5f;
            Physics.LinearVelocityMps[1] = -Physics.LinearVelocityMps[1] * Physics.Restitution;
        }
    }
}

ThermalBodyRecord::ThermalBodyRecord(std::string InDisplayName)
    : SceneRecordBase(RecordClassification::Physics, "ThermalBody", std::move(InDisplayName))
{
    ConfigureEditorPresentation("Thermometer", "Simulation", "ThermalBody", "#e8833a");
#if FRONTIER_EDITOR
    InspectorRuntime::Instance().BindInspector(QueryToken(), std::make_unique<SpatialInspector>(Spatial));
    InspectorRuntime::Instance().AttachInspector(QueryToken(), std::make_unique<PhysicsInspector>(Physics));
    InspectorRuntime::Instance().AttachInspector(QueryToken(), std::make_unique<ThermalInspector>(Thermal, &Physics.Mass));
    InspectorRuntime::Instance().AttachInspector(QueryToken(), std::make_unique<SurfaceOpticsInspector>(Optics));
#endif
}

void ThermalBodyRecord::SerializeConfiguration(toml::table& OutTable) const
{
    SerializeSpatialSubtable(OutTable, Spatial);
    SerializePhysicsSubtable(OutTable, Physics);
    SerializeThermalSubtable(OutTable, Thermal);
    SerializeOpticsSubtable(OutTable, Optics);
}

void ThermalBodyRecord::DeserializeConfiguration(const toml::table& InTable)
{
    DeserializeSpatialSubtable(InTable, Spatial);
    DeserializePhysicsSubtable(InTable, Physics);
    DeserializeThermalSubtable(InTable, Thermal);
    DeserializeOpticsSubtable(InTable, Optics);
}

void ThermalBodyRecord::StepSimulation(float DeltaSeconds)
{
    if (!Thermal.Enabled || DeltaSeconds <= 0.0f)
    {
        return;
    }
    constexpr float AmbientKelvin = 293.15f;
    const float     HeatCap       = std::max(1.0f, Physics.Mass.MassKilograms * Thermal.SpecificHeatJPerKgK);
    const float     ConvectiveW   = Thermal.ConvectionCoefficientWPerM2K * (AmbientKelvin - Thermal.TemperatureKelvin);
    Thermal.TemperatureKelvin     = std::max(1.0f, Thermal.TemperatureKelvin + (ConvectiveW / HeatCap) * DeltaSeconds);
}

ChemicalVesselRecord::ChemicalVesselRecord(std::string InDisplayName)
    : SceneRecordBase(RecordClassification::Physics, "ChemicalVessel", std::move(InDisplayName))
{
    ConfigureEditorPresentation("Droplets", "Simulation", "ChemicalVessel", "#e8833a");
#if FRONTIER_EDITOR
    InspectorRuntime::Instance().BindInspector(QueryToken(), std::make_unique<SpatialInspector>(Spatial));
    InspectorRuntime::Instance().AttachInspector(QueryToken(), std::make_unique<ThermalInspector>(Thermal));
    InspectorRuntime::Instance().AttachInspector(QueryToken(), std::make_unique<ChemicalInspector>(Chemical, &Thermal));
#endif
}

void ChemicalVesselRecord::SerializeConfiguration(toml::table& OutTable) const
{
    SerializeSpatialSubtable(OutTable, Spatial);
    SerializeThermalSubtable(OutTable, Thermal);
    SerializeChemicalSubtable(OutTable, Chemical);
}

void ChemicalVesselRecord::DeserializeConfiguration(const toml::table& InTable)
{
    DeserializeSpatialSubtable(InTable, Spatial);
    DeserializeThermalSubtable(InTable, Thermal);
    DeserializeChemicalSubtable(InTable, Chemical);
}

void ChemicalVesselRecord::StepSimulation(float DeltaSeconds)
{
    if (!Chemical.Enabled || DeltaSeconds <= 0.0f || Chemical.AmountMoles <= 1.0e-5f)
    {
        return;
    }
    const float RateConstant = Chemical.ComputeArrheniusRateConstant(Thermal.TemperatureKelvin);
    const float ReactedFrac  = 1.0f - std::exp(-std::min(RateConstant, 10.0f) * DeltaSeconds);
    const float DeltaMoles   = Chemical.AmountMoles * ReactedFrac;
    Chemical.AmountMoles     = std::max(0.0f, Chemical.AmountMoles - DeltaMoles);
    if (Thermal.Enabled)
    {
        const float ReleasedJoules = -DeltaMoles * Chemical.EnthalpyDeltaKjPerMol * 1000.0f;
        const float HeatCapacity   = std::max(10.0f, Chemical.ComputeSubstanceMassKilograms() * Thermal.SpecificHeatJPerKgK);
        Thermal.TemperatureKelvin  = std::clamp(Thermal.TemperatureKelvin + ReleasedJoules / HeatCapacity, 50.0f, 3500.0f);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SCENEWORLD
//------------------------------------------------------------------------------------------------------------------------

SceneWorld::SceneWorld()
{
    RegisterNativeEngineTypes();
}

SceneWorld::~SceneWorld()
{
    Clear();
}

void SceneWorld::RegisterType(SceneTypeRegistration Registration)
{
    for (SceneTypeRegistration& Existing : TypeRegistry)
    {
        if (Existing.TypeIdentifier == Registration.TypeIdentifier)
        {
            Existing = std::move(Registration);
            return;
        }
    }
    TypeRegistry.push_back(std::move(Registration));
}

std::vector<std::pair<std::string, std::string>> SceneWorld::EnumerateRegisteredTypes() const
{
    std::vector<std::pair<std::string, std::string>> Result;
    Result.reserve(TypeRegistry.size());
    for (const SceneTypeRegistration& Entry : TypeRegistry)
    {
        Result.emplace_back(Entry.TypeIdentifier, Entry.DisplayCategoryName);
    }
    return Result;
}

void SceneWorld::RegisterNativeEngineTypes()
{
    auto RegisterSimple = [this]<typename RecordType>(const char*          TypeId,
                                                      const char*          CategoryLabel,
                                                      RecordClassification Classification,
                                                      const char*          IconName,
                                                      const char*          GroupName,
                                                      const char*          AccentHex) {
        RegisterType({
            TypeId,
            CategoryLabel,
            Classification,
            [Classification, TypeId, IconName, GroupName, AccentHex](std::string DisplayName) -> std::unique_ptr<SceneRecordBase> {
                return std::make_unique<RecordType>(Classification, TypeId, std::move(DisplayName), IconName, GroupName, AccentHex);
            },
        });
    };

    // Composable Object & Scientific Simulation Types
    RegisterType({"SpatialNode", "Spatial · Coordinate Node", RecordClassification::Geometry,
                  [](std::string Name) { return std::make_unique<SpatialNodeRecord>(std::move(Name)); }});
    RegisterType({"RigidBody", "Mechanics · Rigid Body", RecordClassification::Geometry,
                  [](std::string Name) { return std::make_unique<RigidBodyRecord>(std::move(Name)); }});
    RegisterType({"ThermalBody", "Thermodynamics · Thermal Body", RecordClassification::Physics,
                  [](std::string Name) { return std::make_unique<ThermalBodyRecord>(std::move(Name)); }});
    RegisterType({"ChemicalVessel", "Chemistry · Reaction Vessel", RecordClassification::Physics,
                  [](std::string Name) { return std::make_unique<ChemicalVesselRecord>(std::move(Name)); }});

    // 📝 Glyphs, groups and colours as the demo's entity list gives them (InspectorPages.js ConstructEntities,
    //    EnvironmentInspector.js EnvironmentEntries): Native* names are the environment artwork, the rest lucide strokes.
    // Environment Types
    RegisterSimple.operator()<SolarRecord>("Solar", "Environment · Solar", RecordClassification::Illumination, "NativeSun", "Environment", "#e8c27a");
    RegisterSimple.operator()<OpticalFlareRecord>("OpticalFlare", "Environment · Optical Flare", RecordClassification::Illumination, "NativeFlare", "Environment",
                                                  "#99aafa");
    RegisterSimple.operator()<AtmosphereRecord>("Atmosphere", "Environment · Atmosphere", RecordClassification::Environment, "NativeAtmosphere", "Environment",
                                                "#99aafa");
    RegisterSimple.operator()<CloudRecord>("Clouds", "Environment · Clouds", RecordClassification::Environment, "NativeClouds", "Environment", "#99aafa");
    RegisterType({"LocalCloud", "Environment · Local Cloud", RecordClassification::Environment,
        [](std::string Name) -> std::unique_ptr<SceneRecordBase>
        {
            auto Entry = std::make_unique<CloudRecord>(RecordClassification::Environment,"Clouds",std::move(Name),"NativeLocalCloud","Environment","#99aafa");
            Entry->AccessConfiguration().LocalVolume = true;
            return Entry;
        }});
    RegisterType({"LocalFog", "Environment · Local Fog", RecordClassification::Environment,
        [](std::string Name) -> std::unique_ptr<SceneRecordBase>
        {
            auto Entry = std::make_unique<FogRecord>(RecordClassification::Environment,"Fog",std::move(Name),"NativeLocalFog","Environment","#99aafa");
            Entry->AccessConfiguration().LocalVolume = true;
            return Entry;
        }});
    RegisterSimple.operator()<StarRecord>("Stars", "Environment · Stars", RecordClassification::Environment, "NativeStars", "Environment", "#99aafa");
    RegisterSimple.operator()<MoonRecord>("Moons", "Environment · Moons", RecordClassification::Environment, "NativeMoon", "Environment", "#99aafa");
    RegisterSimple.operator()<PlanetRecord>("Planet", "Environment · Planet", RecordClassification::Environment, "Orbit", "Environment", "#99aafa");
    RegisterSimple.operator()<FogRecord>("Fog", "Environment · Fog", RecordClassification::Environment, "NativeFog", "Environment", "#99aafa");
    RegisterType({"AerialFog", "Environment · Atmospheric Fog", RecordClassification::Environment,
        [](std::string Name) -> std::unique_ptr<SceneRecordBase>
        {
            auto Entry = std::make_unique<FogRecord>(RecordClassification::Environment, "AerialFog", std::move(Name),
                "NativeFog", "Environment", "#99aafa");
            Entry->AccessConfiguration().AerialOnly = true;
            return Entry;
        }});
    RegisterSimple.operator()<LightningRecord>("Lightning", "Environment · Lightning", RecordClassification::Illumination, "Sparkles", "Environment",
                                               "#a6c7ff");
    RegisterSimple.operator()<ForceFieldRecord>("ForceField", "Forcefield · Atmospheric Field", RecordClassification::Environment,
        "Wind", "Forcefield", "#a7f3d0");
    RegisterSimple.operator()<WindRecord>("Wind", "Environment · Wind Field", RecordClassification::Environment, "Wind", "Forcefield", "#a7f3d0");
    RegisterType({"ForceSource", "Forcefield · Force Source", RecordClassification::Environment,
        [](std::string Name) -> std::unique_ptr<SceneRecordBase>
        {
            auto Entry = std::make_unique<ForceSourceRecord>(RecordClassification::Environment, "ForceSource", std::move(Name), "Wind",
                                                             "Forcefield", "#a7f3d0");
            Entry->AccessConfiguration() = ConstructForceSource(ForceSourceCategory::GustPuff);
            return Entry;
        }});

    // 📝 The scene's objects, edited by the geometry inspector: the scene archive's own (a record claims their occupants) and the
    //    editor's (engine shapes and duplicates, laid out from the reserve); each record names its own group and glyph. Removing
    //    an archive object hides its occupants for as long as the level has no record claiming them.
    RegisterType({"Geometry", "Scene · Geometry", RecordClassification::Geometry,
        [](std::string Name) -> std::unique_ptr<SceneRecordBase>
        {
            return std::make_unique<GeometryRecord>(RecordClassification::Geometry, "Geometry", std::move(Name), "Box", "Geometry", "#a8bbeb");
        }});

    // Render settings (one record for the whole renderer) & Continuum Solver Types
    RegisterType({"RenderPipeline", "Render settings", RecordClassification::Illumination,
        [](std::string Name) -> std::unique_ptr<SceneRecordBase>
        {
            auto Entry = std::make_unique<RenderPipelineRecord>(RecordClassification::Illumination, "RenderPipeline", std::move(Name), "Monitor",
                                                                "Rendering", "#9fb4d6");
#if FRONTIER_EDITOR
            // 📝 The renderer's one component: the editor offers no Remove for it (the level would render with defaults).
            SceneDirectoryMetadata Meta = SceneDirectory::Instance().QueryMetadata(Entry->QueryToken());
            Meta.RemovableInEditor      = false;
            (void)SceneDirectory::Instance().ConfigureMetadata(Entry->QueryToken(), std::move(Meta));
#endif
            return Entry;
        }});
    RegisterSimple.operator()<FluidPlumeRecord>("FluidPlume", "Solver · Fluid Plume", RecordClassification::Physics, "Flame", "Solvers", "#e8833a");
    RegisterSimple.operator()<FractureRecord>("Fracture", "Solver · Voronoi Fracture", RecordClassification::Physics, "Hammer", "Solvers", "#8fb3a4");
    RegisterSimple.operator()<ParticleRecord>("Particles", "Solver · GPU Particles", RecordClassification::Physics, "Sparkles", "Solvers", "#b494f4");
}

RecordToken SceneWorld::ConstructRecord(std::string_view TypeIdentifier,
                                        std::string      DisplayName,
                                        RecordToken      ParentToken)
{
    for (const SceneTypeRegistration& Registration : TypeRegistry)
    {
        if (Registration.TypeIdentifier == TypeIdentifier)
        {
            if (DisplayName.empty())
            {
                DisplayName = std::string(TypeIdentifier) + " " + std::to_string(OwnedRecords.size() + 1);
            }
            std::unique_ptr<SceneRecordBase> Created = Registration.ConstructCallback(std::move(DisplayName));
            if (!Created)
            {
                return {};
            }
            if (ParentToken.IsValid())
            {
                Created->AssignParentToken(ParentToken);
            }
            const RecordToken Issued = Created->QueryToken();
            OwnedRecords.push_back(std::move(Created));
            return Issued;
        }
    }
    return {};
}

bool SceneWorld::DestroyRecord(RecordToken Token)
{
    if (!Token.IsValid())
    {
        return false;
    }

    for (std::unique_ptr<SceneRecordBase>& Candidate : OwnedRecords)
    {
        if (Candidate && Candidate->QueryParentToken() == Token)
        {
            Candidate->AssignParentToken(RecordToken{});
        }
    }

    const auto It = std::find_if(OwnedRecords.begin(), OwnedRecords.end(),
                                 [Token](const std::unique_ptr<SceneRecordBase>& Entry) {
                                     return Entry && Entry->QueryToken() == Token;
                                 });
    if (It == OwnedRecords.end())
    {
        return false;
    }
    OwnedRecords.erase(It);
    return true;
}

void SceneWorld::Clear()
{
    while (!OwnedRecords.empty())
    {
        OwnedRecords.pop_back();
    }
}

void SceneWorld::AdvanceSimulation(float DeltaSeconds)
{
    for (std::unique_ptr<SceneRecordBase>& Record : OwnedRecords)
    {
        if (Record)
        {
            Record->StepSimulation(DeltaSeconds);
        }
    }
}

SceneRecordBase* SceneWorld::FindRecord(RecordToken Token) noexcept
{
    for (std::unique_ptr<SceneRecordBase>& Entry : OwnedRecords)
    {
        if (Entry && Entry->QueryToken() == Token)
        {
            return Entry.get();
        }
    }
    return nullptr;
}

SceneRecordBase* SceneWorld::FindFirstByType(std::string_view TypeIdentifier) noexcept
{
    for (std::unique_ptr<SceneRecordBase>& Entry : OwnedRecords)
    {
        if (Entry && Entry->QueryTypeIdentifier() == TypeIdentifier)
        {
            return Entry.get();
        }
    }
    return nullptr;
}

std::vector<const SceneRecordBase*> SceneWorld::FindAllByType(std::string_view TypeIdentifier) const
{
    std::vector<const SceneRecordBase*> Found;
    for (const std::unique_ptr<SceneRecordBase>& Entry : OwnedRecords)
    {
        if (Entry && Entry->QueryTypeIdentifier() == TypeIdentifier) Found.push_back(Entry.get());
    }
    return Found;
}

bool SceneWorld::LoadLevelSpace(const std::filesystem::path& LevelSpacePath, std::string* OutError)
{
    if (LevelSpacePath.extension() != LevelSpaceExtension)
    {
        if (OutError)
        {
            *OutError = "Level container must carry the .levelspace extension: " + LevelSpacePath.string();
        }
        return false;
    }

    toml::table Document;
    try
    {
        Document = toml::parse_file(LevelSpacePath.string());
    }
    catch (const toml::parse_error& Error)
    {
        if (OutError)
        {
            *OutError = std::string("Failed to parse .levelspace: ") + std::string(Error.description());
        }
        return false;
    }

    Clear();
    LoadedLevelPath = LevelSpacePath;

    if (const toml::table* LevelHeader = Document["level"].as_table())
    {
        if (const auto Name = (*LevelHeader)["name"].value<std::string>())
        {
            WorldName = *Name;
        }
    }

    std::unordered_map<std::string, RecordToken> IdentityMap;
    if (const toml::array* RecordsArray = Document["records"].as_array())
    {
        for (const toml::node& Node : *RecordsArray)
        {
            const toml::table* EntryTable = Node.as_table();
            if (!EntryTable)
            {
                continue;
            }
            const std::string TypeId   = (*EntryTable)["type"].value_or(std::string{});
            const std::string Name     = (*EntryTable)["name"].value_or(TypeId);
            const std::string Identity = (*EntryTable)["identity"].value_or(std::string{});
            const std::string ParentId = (*EntryTable)["parent"].value_or(std::string{});
            if (TypeId.empty())
            {
                continue;
            }

            RecordToken ParentToken{};
            if (!ParentId.empty())
            {
                if (const auto Found = IdentityMap.find(ParentId); Found != IdentityMap.end())
                {
                    ParentToken = Found->second;
                }
            }

            const RecordToken Created = ConstructRecord(TypeId, Name, ParentToken);
            if (!Created.IsValid())
            {
                continue;
            }
            if (!Identity.empty())
            {
                IdentityMap.emplace(Identity, Created);
            }
            if (SceneRecordBase* Record = FindRecord(Created))
            {
                if (const toml::table* ConfigTable = (*EntryTable)["configuration"].as_table())
                {
                    Record->DeserializeConfiguration(*ConfigTable);
                }
            }
        }
    }

    return true;
}

bool SceneWorld::SaveLevelSpace(const std::filesystem::path& LevelSpacePath, std::string* OutError) const
{
    if (LevelSpacePath.extension() != LevelSpaceExtension)
    {
        if (OutError)
        {
            *OutError = "Level container must carry the .levelspace extension: " + LevelSpacePath.string();
        }
        return false;
    }

    std::error_code FsErr;
    if (LevelSpacePath.has_parent_path())
    {
        std::filesystem::create_directories(LevelSpacePath.parent_path(), FsErr);
    }

    toml::table Root;
    toml::table Header;
    Header.insert_or_assign("format_version", int64_t{1});
    Header.insert_or_assign("name", WorldName);
    Root.insert_or_assign("level", std::move(Header));

    std::unordered_map<uint64_t, std::string> TokenIdentities;
    for (size_t Index = 0; Index < OwnedRecords.size(); ++Index)
    {
        if (OwnedRecords[Index])
        {
            TokenIdentities.emplace(OwnedRecords[Index]->QueryToken().Value, "rec_" + std::to_string(Index + 1));
        }
    }

    toml::array RecordsArray;
    for (const std::unique_ptr<SceneRecordBase>& Record : OwnedRecords)
    {
        if (!Record)
        {
            continue;
        }
        toml::table Entry;
        Entry.insert_or_assign("identity", TokenIdentities[Record->QueryToken().Value]);
        Entry.insert_or_assign("type", Record->QueryTypeIdentifier());
        Entry.insert_or_assign("name", Record->QueryRecordName());
        const RecordToken Parent = Record->QueryParentToken();
        if (Parent.IsValid())
        {
            if (const auto Found = TokenIdentities.find(Parent.Value); Found != TokenIdentities.end())
            {
                Entry.insert_or_assign("parent", Found->second);
            }
        }
        toml::table Config;
        Record->SerializeConfiguration(Config);
        Entry.insert_or_assign("configuration", std::move(Config));
        RecordsArray.push_back(std::move(Entry));
    }
    Root.insert_or_assign("records", std::move(RecordsArray));

    std::ofstream Output(LevelSpacePath, std::ios::binary | std::ios::trunc);
    if (!Output)
    {
        if (OutError)
        {
            *OutError = "Unable to open .levelspace for writing: " + LevelSpacePath.string();
        }
        return false;
    }
    Output << Root << "\n";
    return Output.good();
}

bool SceneWorld::LoadStartupLevelFromProject(const std::filesystem::path& ProjectSpacePath, std::string* OutError)
{
    toml::table ProjectDoc;
    try
    {
        ProjectDoc = toml::parse_file(ProjectSpacePath.string());
    }
    catch (const toml::parse_error& Error)
    {
        if (OutError)
        {
            *OutError = std::string("Failed to parse .projectspace: ") + std::string(Error.description());
        }
        return false;
    }

    const std::filesystem::path ProjectRoot = ProjectSpacePath.parent_path();
    std::string                 StartupId;
    if (const toml::table* EngineTable = ProjectDoc["engine"].as_table())
    {
        StartupId = (*EngineTable)["startup_level"].value_or(std::string{});
    }

    if (!StartupId.empty())
    {
        if (const toml::array* Levels = ProjectDoc["levels"].as_array())
        {
            for (const toml::node& Node : *Levels)
            {
                const toml::table* LevelEntry = Node.as_table();
                if (!LevelEntry)
                {
                    continue;
                }
                if ((*LevelEntry)["identity"].value_or(std::string{}) == StartupId)
                {
                    const std::string RelPath = (*LevelEntry)["path"].value_or(std::string{});
                    if (!RelPath.empty())
                    {
                        return LoadLevelSpace(ProjectRoot / RelPath, OutError);
                    }
                }
            }
        }
    }

    const std::filesystem::path DefaultLevel = ProjectRoot / "Content" / "Levels" / "Default.levelspace";
    if (std::filesystem::exists(DefaultLevel))
    {
        return LoadLevelSpace(DefaultLevel, OutError);
    }

    if (OutError)
    {
        *OutError = "No startup .levelspace found for project: " + ProjectSpacePath.string();
    }
    return false;
}

} // namespace Frontier
