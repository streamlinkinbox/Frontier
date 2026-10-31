/*==========================================================================================================================================
                                                        PROJECT EVEREST SCENE
                                     ------------------------------------------------------------------
             1:1 Proving Grounds.scene assembly: Render, Environment, Forcefield, ControlVehicle, Effects, Destruction & Level.
==========================================================================================================================================*/

// 🧩 1:1 Proving Grounds.scene assembly matching VehicleHMI's EditorShell, ControlVehicle, CameraRigs, and 9 ArenaZones.

#pragma once

#include "Engine/Interface/DomainStudioPanels.h"
#include "Engine/Physics/ControlVehiclePhysics.h"
#include "Engine/Rendering/DualTierRenderer.h"
#include "Engine/Rendering/ProvingGroundsCourse.h"
#include "Engine/Scene/EnvironmentRecords.h"
#include "Engine/Scene/SceneRecord.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace Frontier
{

class SceneDirectoryPanel;

//------------------------------------------------------------------------------------------------------------------------
//                                              CUSTOM PROJECT EVEREST RECORDS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Render settings ("Render settings" in Outliner):
//    Tier 0 (RTX): Two-Level SAH BVH + VK_KHR_ray_query + ReSTIR GI + DielectricRefraction canopy.
//    Tier 1 (GTX): 4-cascade toroidal SDF cone-traced GI + CardRadiosity + OrderedSoftwareBvh reflections.
class EverestIlluminationRecord final : public RecordEntry
{
  public:
    EverestIlluminationRecord();

    int32_t TierSelectionIndex        = 0;      // [-] - 0 = RTX · ReSTIR Path Tracer, 1 = GTX · Distance-Field GI
    int32_t InspectionAovIndex        = 0;      // [-] - ViewportInspectionMode ordinal (0..8)
    double  TemporalHistoryCap        = 20.0;   // [M] - [1..32] ReSTIR temporal reservoir cap
    double  SpatialReuseNeighbors     = 4.0;    // [taps] - [0..8] spatial reservoir taps
    double  SdfClipmapSpanMetres      = 179.2;  // [m] - [40..640] outer toroidal SDF cascade span
    double  BvhReflectionRoughnessCut = 0.62;   // [-] - [0.05..1.0] BVH reflection roughness cutoff
    double  ExposureCompensationEv    = 0.0;    // [EV] - [-4..4]
    double  IndirectBounceScale       = 1.0;    // [x] - [0..3]
    double  SpecularReflectionScale   = 1.0;    // [x] - [0..3]
    double  DistanceSliceHeightMetres = 0.90;   // [m] - [0..6] Z-height of top-right SDF slice inset
    bool    Enabled                   = true;
    bool    DielectricGlazingActive   = true;
    bool    SurfaceCardsActive        = true;
    bool    ClusteredLightsActive     = true;
    bool    DistanceSliceInsetActive  = false;
};

class EverestAccelerationFieldRecord final : public RecordEntry
{
  public:
    explicit EverestAccelerationFieldRecord(EverestIlluminationRecord& LinkedIllumination);

    EverestIlluminationRecord* Illumination = nullptr;
    bool                       Enabled      = true;
};

class EverestTyreRecord final : public RecordEntry
{
  public:
    EverestTyreRecord(const char* CornerLabel, double DefaultCamberDeg);

    double PressureKilopascals = 220.0;   // [kPa] - [160..320]
    double CamberDegrees       = -1.4;    // [deg] - [-5..2]
    double CarcassCelsius      = 68.0;    // [C] - [15..130]
    double PeakGripMu          = 1.15;    // [-] - [0.5..1.8]
    bool   Enabled             = true;
};

// 📝 Exact 1:1 ControlVehicle record ("ControlVehicle" in Outliner):
//    Embeds live ControlVehicleSolver (Pacejka MF6.1 + 32x5 Neo-Hookean Hex8 SolidContinuumWheel + 10,448-byte GPU record)
//    and renders the 1:1 8-tab VehicleSystemsInspector: Overview, Chassis, Engine, Tyres, Suspension, Lights, HMI, Mirrors.
class EverestVehicleRecord final : public RecordEntry
{
  public:
    EverestVehicleRecord();

    uint32_t                          ActiveModuleTab          = 0u;     // [-] - [0..7] 8-tab VehicleSystemsInspector
    uint32_t                          SelectedTyreWheel        = 0u;     // [-] - [0..3] FL, FR, RL, RR
    uint32_t                          SelectedTyreSubTab       = 0u;     // [-] - [0..4] Tyre, Marks, Smoke, Sparks, Burnout
    EverestCameraMode                 CameraMode               = EverestCameraMode::Chase;
    std::array<float, 3>              WorldPosition            = { -40.0f, 0.0f, 0.53f }; // [m] - Exact Proving Grounds spawn
    float                             YawRadians               = 0.0f;   // [rad] - Heading (+X East at 0)
    float                             PitchRadians             = 0.0f;   // [rad]
    float                             RollRadians              = 0.0f;   // [rad]
    float                             FrontSteerRadians        = 0.0f;   // [rad]
    float                             WheelSpinRadians         = 0.0f;   // [rad]
    bool                              UseSolvedWheelPose       = true;   // [-] - Drive 4 corners from ControlVehicleSolver
    double                            SpeedKilometresPerHour   = 0.0;    // [km/h] - Displayed HUD & Outliner speed
    int32_t                           GearNumber               = 1;      // [-] - Current transmission gear
    double                            EngineRpm                = 700.0;  // [rpm] - Current crankshaft speed

    ControlVehicleSolver              DynamicsSolver{};
    VehicleDriverInputs               DriverControls{};
    VehicleVisualRecord               VisualStateBuffer{};               // 10,448-byte GPU upload layout
    std::vector<std::array<float, 2>> ChassisHullSamples{};              // Body-local (X, Z) hull profile from FCT1

    int32_t                           DriveModeIndex           = 1;      // [-] - 0 = Comfort, 1 = Proving Sport, 2 = Drift
    int32_t                           PaintFinishPreset        = 0;      // [-] - 0 = Lime Chartreuse (#d0e000), 1..4 = swatches
    int32_t                           AutomotiveBeamMode       = 1;      // [-] - 0 = Off, 1 = Low Beam, 2 = High Beam, 3 = Fog
    int32_t                           RoofFixtureIndex         = 0;      // [-] - 0 = None, 1 = Slim LED Bar, 2 = Rally 4-Pod
    double                            SprungMassKilograms      = 1300.0; // [kg] - Exact ControlVehicleSpecification::Mass
    double                            WheelbaseMetres          = 3.396;  // [m] - Exact 1.7274 - (-1.6686)
    double                            PeakPowerKilowatts       = 420.0;  // [kW]
    double                            RedlineRpm               = 7200.0; // [rpm]
    double                            BatterySocPercent        = 84.0;   // [%]
    double                            AwdRearTorqueFraction    = 0.65;   // [-]
    double                            FrontBrakeBias           = 0.65;   // [-]
    double                            SpringRateKilonewtonsM   = 48.2;   // [kN/m]
    double                            DampingRatio             = 0.60;   // [-]
    double                            RideHeightMillimetres    = 132.0;  // [mm]
    double                            AeroDownforceCoefficient = 0.35;   // [-]
    double                            ClearcoatRoughness       = 0.0775; // [-] - Exact BodyCoat roughness
    double                            HeadlampKelvin           = 5800.0; // [K]
    double                            CabinHmiLuminanceNits    = 680.0;  // [cd/m2]
    double                            MirrorFoldDegrees        = 0.0;    // [deg]
    bool                              Enabled                  = true;
    bool                              StabilityControlActive   = true;
    bool                              HeadlampsEnabled         = true;
    bool                              BrakeLampsEnabled        = true;
    bool                              HazardLampsEnabled       = false;
    bool                              CollisionOverlayEnabled  = false;

    void                        ConfigureChassisHullSamples(std::vector<std::array<float, 2>> Samples);
    void                        SynchronizeFromSolver() noexcept;
    void                        StepDynamics(double DeltaSeconds) noexcept;
    void                        TeleportToZone(uint32_t ZoneIndex) noexcept;
    [[nodiscard]] SpectrumRgb   QueryBodyPaintAlbedo() const noexcept;
};

// 📝 Tier-3 Domain Studio bridge record (Fracture props, Smoke & fire gas domain, or Particle workspace).
class EverestDomainActorRecord final : public RecordEntry
{
  public:
    EverestDomainActorRecord(RecordClassification Classification, const char* Identifier, const char* Eyebrow,
                             const char* DisplayName, const char* Subtitle, DomainStudioCategory StudioCategory,
                             uint32_t AccentRgba, const char* BadgeLabel = "STUDIO");

    DomainStudioCategory                      TargetStudio = DomainStudioCategory::FractureShatter;
    std::function<void(DomainStudioCategory)> OnLaunchStudio;
    double                                    GainMultiplier = 1.0;
    bool                                      Enabled        = true;
};

// 📝 Level & Teleport inspector ("Proving grounds" in Outliner): 9 ArenaZones teleport buttons + course metrics.
class EverestProvingGroundsRecord final : public RecordEntry
{
  public:
    explicit EverestProvingGroundsRecord(EverestVehicleRecord& TargetVehicle);

    EverestVehicleRecord* Vehicle          = nullptr;
    uint32_t              ActiveZoneIndex  = 0u;
    bool                  Enabled          = true;
};

// 📝 Camera rigs inspector ("Camera rigs" in Outliner): 10 exterior & interior cabin rigs.
class EverestCameraRigRecord final : public RecordEntry
{
  public:
    explicit EverestCameraRigRecord(EverestVehicleRecord& TargetVehicle);

    EverestVehicleRecord* Vehicle = nullptr;
    bool                  Enabled = true;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 PROJECT EVEREST SCENE
//------------------------------------------------------------------------------------------------------------------------

class ProjectEverestScene
{
  public:
    ProjectEverestScene();

    // Wires Tier-3 studio launch buttons in EverestDomainActorRecords to the docked DomainStudioPanel.
    void BindDomainStudio(DomainStudioPanel& StudioPanel);

    // Processes any pending "+ Folder" or "+ Add" request from the Outliner panel.
    void ProcessSpawnRequest(SceneDirectoryPanel& DirectoryPanel);

    // Packages every live scene record into the 3D DualTierProvingGroundsRenderer frame contract.
    [[nodiscard]] EverestViewportInputs BuildViewportInputs(double OrbitYawDegrees     = 202.9183,
                                                            double OrbitPitchDegrees   = 17.1887,
                                                            double OrbitDistanceMetres = 8.0) const noexcept;

    FolderRecord                    RenderFolder{"Render", "RENDER", 0xFFD6B49Fu};
    FolderRecord                    EnvironmentFolder{"Environment", "ENVIRONMENT", 0xFF7AC2E8u};
    FolderRecord                    ForcefieldFolder{"Forcefield", "FORCEFIELD", 0xFFD0F3A7u};
    FolderRecord                    VehicleFolder{"Vehicle", "VEHICLE", 0xFF4AD3E1u};
    FolderRecord                    EffectsFolder{"Effects", "EFFECTS", 0xFF3A83E8u};
    FolderRecord                    DestructionFolder{"Destruction", "DESTRUCTION", 0xFFA4B38Fu};
    FolderRecord                    LevelFolder{"Level", "LEVEL", 0xFFEBBBA8u};

    EverestIlluminationRecord       Illumination;
    EverestAccelerationFieldRecord  AccelerationField{Illumination};
    SceneRecord                     SurfaceCache{RecordClassification::Illumination};

    SunRecord                       Sun;
    LensFlareRecord                 LensFlare;
    CloudRecord                     Clouds;
    AtmosphereRecord                Atmosphere;
    StarsRecord                     Stars;
    MoonRecord                      Moon;
    FogRecord                       Fog;
    WeatherRecord                   Weather;
    WindRecord                      Wind;

    EverestVehicleRecord            Vehicle;
    EverestTyreRecord               FrontLeftTyre{"Front Left Tyre", -1.6};
    EverestTyreRecord               FrontRightTyre{"Front Right Tyre", -1.6};
    EverestTyreRecord               RearLeftTyre{"Rear Left Tyre", -1.2};
    EverestTyreRecord               RearRightTyre{"Rear Right Tyre", -1.2};

    EverestDomainActorRecord        WheelSprayEmitter{
        RecordClassification::Content, "particles", "EFFECTS / PARTICLE AUTHORING WORKSPACE",
        "Particles", "Particle Editor, Flux, Fracture and GPU precipitation.",
        DomainStudioCategory::ParticleEmitter, 0xFFF494B4u, "3 tools"};
    EverestDomainActorRecord        ExhaustPlume{
        RecordClassification::Content, "gas", "EFFECTS / GAS DOMAIN",
        "Smoke & fire", "Eulerian smoke · combustion · tyre sources.",
        DomainStudioCategory::FluidPlume, 0xFF3A83E8u, "resident"};
    EverestDomainActorRecord        FractureBarrier{
        RecordClassification::Physics, "fracture", "DESTRUCTION / BREAKABLE PROPS",
        "Fracture", "Energy-limited fracture · 13 breakable proving-grounds props.",
        DomainStudioCategory::FractureShatter, 0xFFA4B38Fu, "13/13"};

    EverestProvingGroundsRecord     CourseGeometry{Vehicle};
    EverestCameraRigRecord          ChaseCamera{Vehicle};

  private:
    uint32_t                                    DynamicFolderSequence = 0;
    std::vector<std::unique_ptr<FolderRecord>>  SpawnedFolders;
    std::vector<std::unique_ptr<SceneRecord>>   SpawnedRecords;
};

} // namespace Frontier
