/*==========================================================================================================================================
                                                        PROJECT EVEREST SCENE
                                     ------------------------------------------------------------------
             Project Everest scene assembly: global Engine Folders + Sun/Flare/Atmosphere/Clouds/Wind + custom Vehicle & GPU tiers.
==========================================================================================================================================*/

// 🧩 Project Everest scene assembly: global Engine Folders + Sun/Flare/Atmosphere/Clouds/Wind + custom Vehicle & GPU tiers.

#pragma once

#include "Engine/Scene/EnvironmentRecords.h"
#include "Engine/Scene/SceneRecord.h"

#include <cstdint>
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
//    Tier 1 (GTX fallback): 4-cascade clipmap SDF cone-traced GI + OrderedSoftwareBvh reflections.
class EverestIlluminationRecord final : public RecordEntry
{
  public:
    EverestIlluminationRecord();

    int32_t TierSelectionIndex        = 0;      // [-] - 0 = RTX Raytracing + ReSTIR, 1 = GTX SDF + BVH Fallback
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

// 📝 Project Everest custom vehicle record ("ControlVehicle") with user-created chassis/suspension inspector.
class EverestVehicleRecord final : public RecordEntry
{
  public:
    EverestVehicleRecord();

    int32_t DriveModeIndex           = 1;        // [-] - 0 = Comfort, 1 = Proving Sport, 2 = Drift Telemetry
    double  SprungMassKilograms      = 1780.0;   // [kg] - [900..2800]
    double  AwdRearTorqueFraction    = 0.65;     // [-] - [0..1]
    double  FrontBrakeBias           = 0.62;     // [-] - [0.4..0.8]
    double  SpringRateKilonewtonsM   = 54.0;     // [kN/m] - [20..120]
    double  DampingRatio             = 0.68;     // [-] - [0.2..1.4]
    double  RideHeightMillimetres    = 132.0;    // [mm] - [75..240]
    double  AeroDownforceCoefficient = 0.42;     // [-] - [0..1.5]
    bool    Enabled                  = true;
    bool    StabilityControlActive   = true;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 PROJECT EVEREST SCENE
//------------------------------------------------------------------------------------------------------------------------

class ProjectEverestScene
{
  public:
    ProjectEverestScene();

    // Services any pending "+ Folder" or "+ Add" request from the Outliner panel.
    void ServiceSpawnRequest(SceneDirectoryPanel& DirectoryPanel);

    FolderRecord              RenderFolder{"Render", "RENDER", 0xFFA3D97Au};
    FolderRecord              EnvironmentFolder{"Environment", "ENVIRONMENT", 0xFF4FD18Fu};
    FolderRecord              ForcefieldFolder{"Forcefield", "FORCEFIELD", 0xFFACD8C7u};
    FolderRecord              VehicleFolder{"Vehicle", "VEHICLE", 0xFFA7E62Fu};
    FolderRecord              LevelFolder{"Level", "PROVING GROUNDS", 0xFFFF8CB1u};

    EverestIlluminationRecord Illumination;
    AtmosphereRecord          Atmosphere;
    CloudRecord               Clouds;
    SunRecord                 Sun;
    LensFlareRecord           LensFlare;
    WindRecord                Wind;

    EverestVehicleRecord      Vehicle;
    EverestTyreRecord         FrontLeftTyre{"Front Left Tyre", -1.6};
    EverestTyreRecord         FrontRightTyre{"Front Right Tyre", -1.6};
    EverestTyreRecord         RearLeftTyre{"Rear Left Tyre", -1.2};
    EverestTyreRecord         RearRightTyre{"Rear Right Tyre", -1.2};

    SceneRecord               CourseGeometry{RecordClassification::Geometry};
    SceneRecord               ChaseCamera{RecordClassification::Camera};

  private:
    uint32_t                                      DynamicFolderSequence = 0;
    std::vector<std::unique_ptr<FolderRecord>>    SpawnedFolders;
    std::vector<std::unique_ptr<SceneRecord>>     SpawnedRecords;
};

} // namespace Frontier
