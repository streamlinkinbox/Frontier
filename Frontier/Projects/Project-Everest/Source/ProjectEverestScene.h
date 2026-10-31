/*==========================================================================================================================================
                                                        PROJECT EVEREST SCENE
                                     ------------------------------------------------------------------
             Project Everest scene assembly: global Engine Folders + Observatories + 8-Tab ControlVehicle + Tier-3 Studios.
==========================================================================================================================================*/

// 🧩 Project Everest scene assembly: global Engine Folders + Observatories + 8-Tab ControlVehicle + Tier-3 Studios.

#pragma once

#include "Engine/Interface/DomainStudioPanels.h"
#include "Engine/Rendering/DualTierRenderer.h"
#include "Engine/Scene/EnvironmentRecords.h"
#include "Engine/Scene/SceneRecord.h"

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

// 📝 Custom dual-tier GPU illumination record for Project Everest:
//    Tier 0 (RTX): VK_KHR_ray_query + Path-Replay ReSTIR GI + DielectricBoundary reflections.
//    Tier 1 (GTX fallback): 4-cascade clipmap SDF cone-traced GI + SurfaceCards + OrderedSoftwareBvh reflections.
class EverestIlluminationRecord final : public RecordEntry
{
  public:
    EverestIlluminationRecord();

    int32_t TierSelectionIndex        = 0;      // [-] - 0 = RTX Raytracing + ReSTIR, 1 = GTX SDF + BVH Fallback
    int32_t InspectionAovIndex        = 0;      // [-] - ViewportInspectionMode ordinal (0..5)
    double  TemporalHistoryCap        = 20.0;   // [M] - [1..32] ReSTIR temporal reservoir cap
    double  SpatialReuseNeighbors     = 4.0;    // [taps] - [0..8] spatial reservoir taps
    double  SdfClipmapSpanMetres      = 160.0;  // [m] - [40..640] outer toroidal SDF cascade span
    double  BvhReflectionRoughnessCut = 0.55;   // [-] - [0.05..1.0] GTX software BVH reflection threshold
    double  ExposureCompensationEv    = 0.0;    // [EV] - [-4..4]
    bool    Enabled                   = true;
    bool    DielectricGlazingActive   = true;
    bool    SurfaceCardsActive        = true;
};

class EverestTyreRecord final : public RecordEntry
{
  public:
    EverestTyreRecord(const char* CornerLabel, double DefaultCamberDeg);

    double PressureKilopascals = 235.0;   // [kPa] - [160..320]
    double CamberDegrees       = -1.4;    // [deg] - [-5..2]
    double CarcassCelsius      = 68.0;    // [C] - [15..130]
    double PeakGripMu          = 1.18;    // [-] - [0.5..1.8]
    bool   Enabled             = true;
};

// 📝 Project Everest custom 8-tab vehicle record ("ControlVehicle"):
//    0=Overview, 1=Chassis, 2=Engine, 3=Tyres, 4=Suspension, 5=Lights, 6=HMI & cabin, 7=Mirrors.
class EverestVehicleRecord final : public RecordEntry
{
  public:
    EverestVehicleRecord();

    uint32_t ActiveModuleTab          = 0;        // [-] - [0..7] 8-tab Vehicle Modules Inspector
    int32_t  DriveModeIndex           = 1;        // [-] - 0 = Comfort, 1 = Proving Sport, 2 = Drift Telemetry
    int32_t  PaintFinishPreset        = 0;        // [-] - 0 = Glacier Blue, 1 = Solar Copper, 2 = Alpine Mint, 3 = Obsidian
    double   SprungMassKilograms      = 1780.0;   // [kg] - [900..2800]
    double   WheelbaseMetres          = 2.76;     // [m] - [2.2..3.4]
    double   PeakPowerKilowatts       = 420.0;    // [kW] - [120..900]
    double   RedlineRpm               = 8500.0;   // [rpm] - [4500..12000]
    double   BatterySocPercent        = 84.0;     // [%] - [5..100]
    double   AwdRearTorqueFraction    = 0.65;     // [-] - [0..1]
    double   FrontBrakeBias           = 0.62;     // [-] - [0.4..0.8]
    double   SpringRateKilonewtonsM   = 54.0;     // [kN/m] - [20..120]
    double   DampingRatio             = 0.68;     // [-] - [0..2..1.4]
    double   RideHeightMillimetres    = 132.0;    // [mm] - [75..240]
    double   AeroDownforceCoefficient = 0.42;     // [-] - [0..1.5]
    double   ClearcoatRoughness       = 0.14;     // [-] - [0.03..0.65]
    double   HeadlampKelvin           = 5800.0;   // [K] - [3200..7500]
    double   CabinHmiLuminanceNits    = 680.0;    // [cd/m2] - [80..1200]
    double   MirrorFoldDegrees        = 0.0;      // [deg] - [0..68]
    bool     Enabled                  = true;
    bool     StabilityControlActive   = true;
    bool     HeadlampsEnabled         = true;
    bool     BrakeLampsEnabled        = true;
    bool     HazardLampsEnabled       = false;
    bool     CollisionOverlayEnabled  = false;

    [[nodiscard]] SpectrumRgb QueryBodyPaintAlbedo() const noexcept;
};

// 📝 Tier-3 Domain Studio bridge record (Fracture barrier, Plume lattice, or GPU Particle spray).
class EverestDomainActorRecord final : public RecordEntry
{
  public:
    EverestDomainActorRecord(RecordClassification Classification, const char* Identifier, const char* Eyebrow,
                             const char* DisplayName, const char* Subtitle, DomainStudioCategory StudioCategory,
                             uint32_t AccentRgba);

    DomainStudioCategory                        TargetStudio = DomainStudioCategory::FractureShatter;
    std::function<void(DomainStudioCategory)>   OnLaunchStudio;
    double                                      GainMultiplier = 1.0;
    bool                                        Enabled        = true;
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

    // Services any pending "+ Folder" or "+ Add" request from the Outliner panel.
    void ServiceSpawnRequest(SceneDirectoryPanel& DirectoryPanel);

    // Packages every live scene record into the 3D DualTierProvingGroundsRenderer frame contract.
    [[nodiscard]] EverestViewportInputs BuildViewportInputs(double CameraYawDegrees      = -36.0,
                                                            double CameraPitchDegrees    = 16.5,
                                                            double CameraDistanceMetres  = 5.85) const noexcept;

    FolderRecord              RenderFolder{"Render", "RENDER", 0xFFA3D97Au};
    FolderRecord              EnvironmentFolder{"Environment", "ENVIRONMENT", 0xFF4FD18Fu};
    FolderRecord              ForcefieldFolder{"Forcefield", "FORCEFIELD", 0xFFACD8C7u};
    FolderRecord              VehicleFolder{"Vehicle", "VEHICLE", 0xFFA7E62Fu};
    FolderRecord              EffectsFolder{"Effects & Studios", "TIER-3 STUDIOS", 0xFF78A6FAu};
    FolderRecord              LevelFolder{"Level", "PROVING GROUNDS", 0xFFFF8CB1u};

    EverestIlluminationRecord Illumination;
    AtmosphereRecord          Atmosphere;
    CloudRecord               Clouds;
    FogRecord                 Fog;
    SunRecord                 Sun;
    MoonRecord                Moon;
    StarsRecord               Stars;
    LensFlareRecord           LensFlare;
    WeatherRecord             Weather;
    WindRecord                Wind;

    EverestVehicleRecord      Vehicle;
    EverestTyreRecord         FrontLeftTyre{"Front Left Tyre", -1.6};
    EverestTyreRecord         FrontRightTyre{"Front Right Tyre", -1.6};
    EverestTyreRecord         RearLeftTyre{"Rear Left Tyre", -1.2};
    EverestTyreRecord         RearRightTyre{"Rear Right Tyre", -1.2};

    EverestDomainActorRecord  FractureBarrier{
        RecordClassification::Physics, "fracture_barrier", "DESTRUCTION / TIER-3 VORONOI STUDIO",
        "ShatterTestBarrier", "Breakable proving-grounds impact barrier authored in the Fracture Studio.",
        DomainStudioCategory::FractureShatter, 0xFF5EA6F2u};
    EverestDomainActorRecord  ExhaustPlume{
        RecordClassification::Content, "exhaust_plume", "FLUIDS / TIER-3 PLUME LATTICE STUDIO",
        "ThermalPlumeEmitter", "Buoyant Eulerian gas & smoke lattice authored in the Fluid & Plume Studio.",
        DomainStudioCategory::FluidPlume, 0xFFB2CE5Eu};
    EverestDomainActorRecord  WheelSprayEmitter{
        RecordClassification::Content, "wheel_spray", "PARTICLES / TIER-3 EMITTER STUDIO",
        "WakeSprayParticles", "Ballistic SDF-colliding tyre wake emitter authored in the Particle Studio.",
        DomainStudioCategory::ParticleEmitter, 0xFFfab08au};

    SceneRecord               CourseGeometry{RecordClassification::Geometry};
    SceneRecord               ChaseCamera{RecordClassification::Camera};

  private:
    uint32_t                                      DynamicFolderSequence = 0;
    std::vector<std::unique_ptr<FolderRecord>>    SpawnedFolders;
    std::vector<std::unique_ptr<SceneRecord>>     SpawnedRecords;
};

} // namespace Frontier
