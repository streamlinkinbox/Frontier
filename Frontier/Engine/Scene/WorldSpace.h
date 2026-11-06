/*==========================================================================================================================================
                                                            WORLDSPACE.H
==========================================================================================================================================*/
// 🧩 Dynamic scene world owning polymorphic RecordEntry instances, .levelspace serialization, and extensible type factories.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Interface/NativeInspectors.h"
#include "Engine/Scene/EngineConfigurations.h"
#include "Engine/Scene/RecordEntry.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace toml
{
inline namespace v3
{
class table;
}
} // namespace toml

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    POLYMORPHIC SCENE RECORD
//------------------------------------------------------------------------------------------------------------------------

class SceneRecordBase : public RecordEntry
{
  public:
    SceneRecordBase(RecordClassification InClassification, std::string_view InTypeIdentifier, std::string InDisplayName);
    ~SceneRecordBase() override = default;

    [[nodiscard]] RecordToken        QueryToken() const noexcept { return Token(); }
    [[nodiscard]] const std::string& QueryTypeIdentifier() const noexcept { return TypeIdentifier; }
    [[nodiscard]] const std::string& QueryRecordName() const noexcept { return StoredName; }
    [[nodiscard]] RecordToken        QueryParentToken() const noexcept { return StoredParent; }

    void AssignRecordName(std::string NewName);
    void AssignParentToken(RecordToken ParentToken);
    void ConfigureEditorPresentation(const char* GlyphIdentifier,
                                     const char* GroupName        = "",
                                     const char* SubtitleCategory = "",
                                     const char* AccentHex        = "");

    virtual void SerializeConfiguration(toml::table& OutTable) const = 0;
    virtual void DeserializeConfiguration(const toml::table& InTable) = 0;
    virtual void StepSimulation(float /*DeltaSeconds*/) {}

  private:
    std::string TypeIdentifier;
    std::string StoredName;
    RecordToken StoredParent{};
};

//------------------------------------------------------------------------------------------------------------------------
//                                            SINGLE-CONFIGURATION ENGINE RECORD TEMPLATE
//------------------------------------------------------------------------------------------------------------------------

template <typename ConfigurationType, typename InspectorType = void>
class TypedSceneRecord final : public SceneRecordBase
{
  public:
    TypedSceneRecord(RecordClassification InClassification,
                     std::string_view     InTypeIdentifier,
                     std::string          InDisplayName,
                     const char*          InIconIdentifier,
                     const char*          InGroupName = "",
                     const char*          InAccentHex = "")
        : SceneRecordBase(InClassification, InTypeIdentifier, std::move(InDisplayName))
    {
        ConfigureEditorPresentation(InIconIdentifier, InGroupName, InTypeIdentifier.data(), InAccentHex);
#if FRONTIER_EDITOR
        if constexpr (!std::is_void_v<InspectorType>)
        {
            InspectorRuntime::Instance().BindInspector(this->QueryToken(), std::make_unique<InspectorType>(Parameters));
        }
#endif
    }

    [[nodiscard]] ConfigurationType&       AccessConfiguration() noexcept { return Parameters; }
    [[nodiscard]] const ConfigurationType& QueryConfiguration() const noexcept { return Parameters; }

    void SerializeConfiguration(toml::table& OutTable) const override;
    void DeserializeConfiguration(const toml::table& InTable) override;

  private:
    ConfigurationType Parameters{};
};

#if FRONTIER_EDITOR
using RenderPipelineRecord = TypedSceneRecord<RenderPipelineConfiguration, RenderSettingsInspector>;
using SolarRecord          = TypedSceneRecord<SolarConfiguration, SolarInspector>;
using OpticalFlareRecord   = TypedSceneRecord<OpticalFlareConfiguration, OpticalFlareInspector>;
using AtmosphereRecord     = TypedSceneRecord<AtmosphereConfiguration, AtmosphereInspector>;
using CloudRecord          = TypedSceneRecord<CloudConfiguration, CloudInspector>;
using StarRecord           = TypedSceneRecord<StarConfiguration, StarInspector>;
using MoonRecord           = TypedSceneRecord<MoonConfiguration, MoonInspector>;
using PlanetRecord         = TypedSceneRecord<PlanetConfiguration, PlanetInspector>;
using FogRecord            = TypedSceneRecord<FogConfiguration, FogInspector>;
using LightningRecord      = TypedSceneRecord<LightningConfiguration, LightningInspector>;
using WindRecord           = TypedSceneRecord<WindConfiguration, WindInspector>;
using ForceFieldRecord     = TypedSceneRecord<ForceFieldConfiguration, ForceFieldInspector>;
using ForceSourceRecord    = TypedSceneRecord<ForceSourceConfiguration, ForceSourceInspector>;
using FluidPlumeRecord     = TypedSceneRecord<FluidPlumeConfiguration, FluidPlumeInspector>;
using FractureRecord       = TypedSceneRecord<FractureConfiguration, FractureInspector>;
using ParticleRecord       = TypedSceneRecord<ParticleConfiguration, ParticleInspector>;
using GeometryRecord       = TypedSceneRecord<GeometryConfiguration, GeometryInspector>;
using VehicleRecord        = TypedSceneRecord<VehicleConfiguration, VehicleInspector>;
using TyreRecord           = TypedSceneRecord<TyreConfiguration, TyreInspector>;
#else
using RenderPipelineRecord = TypedSceneRecord<RenderPipelineConfiguration>;
using SolarRecord          = TypedSceneRecord<SolarConfiguration>;
using OpticalFlareRecord   = TypedSceneRecord<OpticalFlareConfiguration>;
using AtmosphereRecord     = TypedSceneRecord<AtmosphereConfiguration>;
using CloudRecord          = TypedSceneRecord<CloudConfiguration>;
using StarRecord           = TypedSceneRecord<StarConfiguration>;
using MoonRecord           = TypedSceneRecord<MoonConfiguration>;
using PlanetRecord         = TypedSceneRecord<PlanetConfiguration>;
using FogRecord            = TypedSceneRecord<FogConfiguration>;
using LightningRecord      = TypedSceneRecord<LightningConfiguration>;
using WindRecord           = TypedSceneRecord<WindConfiguration>;
using ForceFieldRecord     = TypedSceneRecord<ForceFieldConfiguration>;
using ForceSourceRecord    = TypedSceneRecord<ForceSourceConfiguration>;
using FluidPlumeRecord     = TypedSceneRecord<FluidPlumeConfiguration>;
using FractureRecord       = TypedSceneRecord<FractureConfiguration>;
using ParticleRecord       = TypedSceneRecord<ParticleConfiguration>;
using GeometryRecord       = TypedSceneRecord<GeometryConfiguration>;
using VehicleRecord        = TypedSceneRecord<VehicleConfiguration>;
using TyreRecord           = TypedSceneRecord<TyreConfiguration>;
#endif

//------------------------------------------------------------------------------------------------------------------------
//                                      COMPOSABLE MULTI-CONFIGURATION SCENE RECORDS
//------------------------------------------------------------------------------------------------------------------------

// SpatialNodeRecord: pure coordinate frame with zero mass and zero surface material.
class SpatialNodeRecord final : public SceneRecordBase
{
  public:
    explicit SpatialNodeRecord(std::string InDisplayName);

    [[nodiscard]] SpatialConfiguration&       AccessSpatial() noexcept { return Spatial; }
    [[nodiscard]] const SpatialConfiguration& QuerySpatial() const noexcept { return Spatial; }

    void SerializeConfiguration(toml::table& OutTable) const override;
    void DeserializeConfiguration(const toml::table& InTable) override;

  private:
    SpatialConfiguration Spatial{};
};

// RigidBodyRecord: spatial transform + mechanical mass/kinematics + surface optics.
class RigidBodyRecord final : public SceneRecordBase
{
  public:
    explicit RigidBodyRecord(std::string InDisplayName);

    [[nodiscard]] SpatialConfiguration&             AccessSpatial() noexcept { return Spatial; }
    [[nodiscard]] const SpatialConfiguration&       QuerySpatial() const noexcept { return Spatial; }
    [[nodiscard]] PhysicsConfiguration&             AccessPhysics() noexcept { return Physics; }
    [[nodiscard]] const PhysicsConfiguration&       QueryPhysics() const noexcept { return Physics; }
    [[nodiscard]] SurfaceOpticsConfiguration&       AccessOptics() noexcept { return Optics; }
    [[nodiscard]] const SurfaceOpticsConfiguration& QueryOptics() const noexcept { return Optics; }

    void SerializeConfiguration(toml::table& OutTable) const override;
    void DeserializeConfiguration(const toml::table& InTable) override;
    void StepSimulation(float DeltaSeconds) override;

  private:
    SpatialConfiguration       Spatial{};
    PhysicsConfiguration       Physics{};
    SurfaceOpticsConfiguration Optics{};
};

// ThermalBodyRecord: spatial transform + mechanical mass + conductive/radiative thermodynamics + surface optics.
class ThermalBodyRecord final : public SceneRecordBase
{
  public:
    explicit ThermalBodyRecord(std::string InDisplayName);

    [[nodiscard]] SpatialConfiguration&             AccessSpatial() noexcept { return Spatial; }
    [[nodiscard]] const SpatialConfiguration&       QuerySpatial() const noexcept { return Spatial; }
    [[nodiscard]] PhysicsConfiguration&             AccessPhysics() noexcept { return Physics; }
    [[nodiscard]] const PhysicsConfiguration&       QueryPhysics() const noexcept { return Physics; }
    [[nodiscard]] ThermalConfiguration&             AccessThermal() noexcept { return Thermal; }
    [[nodiscard]] const ThermalConfiguration&       QueryThermal() const noexcept { return Thermal; }
    [[nodiscard]] SurfaceOpticsConfiguration&       AccessOptics() noexcept { return Optics; }
    [[nodiscard]] const SurfaceOpticsConfiguration& QueryOptics() const noexcept { return Optics; }

    void SerializeConfiguration(toml::table& OutTable) const override;
    void DeserializeConfiguration(const toml::table& InTable) override;
    void StepSimulation(float DeltaSeconds) override;

  private:
    SpatialConfiguration       Spatial{};
    PhysicsConfiguration       Physics{};
    ThermalConfiguration       Thermal{};
    SurfaceOpticsConfiguration Optics{};
};

// ChemicalVesselRecord: spatial transform + thermodynamics + chemical stoichiometry/Arrhenius kinetics (no surface material required).
class ChemicalVesselRecord final : public SceneRecordBase
{
  public:
    explicit ChemicalVesselRecord(std::string InDisplayName);

    [[nodiscard]] SpatialConfiguration&        AccessSpatial() noexcept { return Spatial; }
    [[nodiscard]] const SpatialConfiguration&  QuerySpatial() const noexcept { return Spatial; }
    [[nodiscard]] ThermalConfiguration&        AccessThermal() noexcept { return Thermal; }
    [[nodiscard]] const ThermalConfiguration&  QueryThermal() const noexcept { return Thermal; }
    [[nodiscard]] ChemicalConfiguration&       AccessChemical() noexcept { return Chemical; }
    [[nodiscard]] const ChemicalConfiguration& QueryChemical() const noexcept { return Chemical; }

    void SerializeConfiguration(toml::table& OutTable) const override;
    void DeserializeConfiguration(const toml::table& InTable) override;
    void StepSimulation(float DeltaSeconds) override;

  private:
    SpatialConfiguration  Spatial{};
    ThermalConfiguration  Thermal{};
    ChemicalConfiguration Chemical{};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    DYNAMIC SCENE WORLD
//------------------------------------------------------------------------------------------------------------------------

struct SceneTypeRegistration
{
    std::string                                                  TypeIdentifier;
    std::string                                                  DisplayCategoryName;
    RecordClassification                                         DefaultClassification = RecordClassification::Unclassified;
    std::function<std::unique_ptr<SceneRecordBase>(std::string)> ConstructCallback;
};

class WorldSpace
{
  public:
    static constexpr const char* LevelSpaceExtension = ".levelspace";

    WorldSpace();
    ~WorldSpace();

    WorldSpace(const WorldSpace&)            = delete;
    WorldSpace& operator=(const WorldSpace&) = delete;

    void RegisterType(SceneTypeRegistration Registration);
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> EnumerateRegisteredTypes() const;

    RecordToken ConstructRecord(std::string_view TypeIdentifier,
                                std::string      DisplayName = {},
                                RecordToken      ParentToken = {});
    bool        DestroyRecord(RecordToken Token);
    void        Clear();

    void AdvanceSimulation(float DeltaSeconds);

    [[nodiscard]] size_t             QueryRecordCount() const noexcept { return OwnedRecords.size(); }
    [[nodiscard]] SceneRecordBase*   FindRecord(RecordToken Token) noexcept;
    [[nodiscard]] SceneRecordBase*   FindFirstByType(std::string_view TypeIdentifier) noexcept;
    [[nodiscard]] std::vector<const SceneRecordBase*> FindAllByType(std::string_view TypeIdentifier) const;
    // 📝 Every record in level order (the WorldRegister's survey).
    [[nodiscard]] std::vector<const SceneRecordBase*> EnumerateRecords() const;
    [[nodiscard]] const std::string& QueryWorldName() const noexcept { return WorldName; }
    void                             ConfigureWorldName(std::string Name) { WorldName = std::move(Name); }

    [[nodiscard]] const std::filesystem::path& QueryLoadedLevelPath() const noexcept { return LoadedLevelPath; }

    bool LoadLevelSpace(const std::filesystem::path& LevelSpacePath, std::string* OutError = nullptr);
    bool SaveLevelSpace(const std::filesystem::path& LevelSpacePath, std::string* OutError = nullptr) const;
    bool LoadStartupLevelFromProject(const std::filesystem::path& ProjectSpacePath, std::string* OutError = nullptr);

  private:
    void RegisterNativeEngineTypes();

    std::string                                   WorldName = "Default";
    std::filesystem::path                         LoadedLevelPath;
    std::vector<SceneTypeRegistration>            TypeRegistry;
    std::vector<std::unique_ptr<SceneRecordBase>> OwnedRecords;
};

} // namespace Frontier
