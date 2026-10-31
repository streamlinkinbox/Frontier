/*==========================================================================================================================================
                                                      PROVINGGROUNDSCOURSE.H
==========================================================================================================================================*/
// 🧩 Exact 1:1 C++ geometry, 'FCT1' binary facet decoder, 82-slot linear Rec.709 material table, ControlVehicle assembly
//    (chassis, glazing, Slate cockpit, wing/rear mirrors, roof fixture, 4 wheel sockets), and Proving Grounds level layout
//    transcribed from the VehicleHMI reference (MaterialSpecification.js, FacetCodec.js, VehicleSpecification.js,
//    CabinStructure.js, CourseStructure.js, LevelStructure.js, LightingSystem.js, ChaseCameraSolver.js).

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  COURSE MATERIAL TABLE
//------------------------------------------------------------------------------------------------------------------------

enum class CourseMaterialId : uint32_t
{
    Default          = 0,
    VehicleBodyLegacy = 1,
    AsphaltLegacy    = 2,
    ConcreteLegacy   = 3,
    MarkingLegacy    = 4,
    EmitterLegacy    = 5,
    BodyCoat         = 6,
    VehicleGlass     = 7,
    VehiclePlastic   = 8,
    Tyre             = 9,
    Hub              = 10,
    Brake            = 11,
    VehicleTrim      = 12,
    Headlight        = 13,
    Taillight        = 14,
    Amber            = 15,
    Asphalt          = 26,
    AsphaltPatch     = 27,
    PaintWhite       = 28,
    PaintYellow      = 29,
    KerbRed          = 30,
    KerbWhite        = 31,
    Grass            = 32,
    Jersey           = 33,
    ConeOrange       = 34,
    ConeCollar       = 35,
    HangarWall       = 36,
    HangarRoof       = 37,
    HangarCrimson    = 38,
    HangarChalk      = 39,
    LampStrip        = 40,
    TunnelWall       = 41,
    TunnelCobalt     = 42,
    LampWarm         = 43,
    TowerConcrete    = 44,
    TowerGlass       = 45,
    Pole             = 46,
    PoleHead         = 47,
    TrafficHead      = 48,
    SignalRed        = 49,
    SignalAmber      = 50,
    SignalGreen      = 51,
    TrafficPost      = 52,
    RampDeck         = 53,
    RampSide         = 54,
    CrateWood        = 55,
    CrateBand        = 56,
    PaneGlass        = 57,
    PaneFrame        = 58,
    PillarConcrete   = 59,
    TemperedGlass    = 60,
    BarrierShell     = 61,
    WallStone        = 62,
    BlockConcrete    = 63,
    TyreStack        = 64,
    CabinetRed       = 65,
    SteelDark        = 66,
    StressCoral      = 67,
    StressAmber      = 68,
    StressTeal       = 69,
    StressSlate      = 70,
    CabinTrim        = 71,
    CabinSeat        = 72,
    SteeringWheel    = 73,
    TabletHousing    = 74,
    MirrorGlass      = 75,
    MirrorTrim       = 76,
    DisplayGlass     = 77,
    LampPost         = 78,
    LampLantern      = 79,
    LampLanternCool  = 80,
    RimBase          = 81,
    Count            = 82
};

struct CourseMaterialSpecification
{
    std::array<float, 3> Albedo        = { 0.50f, 0.50f, 0.50f };   // [-] - Linear Rec.709 base reflectance
    float                Metalness     = 0.0f;                      // [-]
    float                Roughness     = 0.60f;                     // [-]
    std::array<float, 3> Emission      = { 0.0f, 0.0f, 0.0f };      // [cd/m^2 scaled]
    float                CoatWeight    = 0.0f;                      // [-] - Clear-coat weight
    float                CoatRoughness = 0.08f;                     // [-] - Clear-coat roughness
    float                Transmission  = 0.0f;                      // [-] - Dielectric glazing transmission
};

[[nodiscard]] std::array<CourseMaterialSpecification, 82> ConstructCourseMaterials(
    std::array<float, 3> BodyPaintOverride = { -1.0f, -1.0f, -1.0f }) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                  FACET TRIANGLE STREAM
//------------------------------------------------------------------------------------------------------------------------

struct CourseTriangle
{
    std::array<float, 3> A        = { 0.0f, 0.0f, 0.0f };   // [m] - World/local +X fwd, +Y left, +Z up
    std::array<float, 3> B        = { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> C        = { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> NormalA  = { 0.0f, 0.0f, 1.0f };
    std::array<float, 3> NormalB  = { 0.0f, 0.0f, 1.0f };
    std::array<float, 3> NormalC  = { 0.0f, 0.0f, 1.0f };
    uint32_t             Material = 0u;
};

struct DecodedFacetArchive
{
    uint32_t                   VertexCount = 0u;
    uint32_t                   FacetCount  = 0u;
    bool                       Smooth      = false;
    std::array<float, 3>       BoundsMin   = { 0.0f, 0.0f, 0.0f };
    std::array<float, 3>       BoundsMax   = { 0.0f, 0.0f, 0.0f };
    std::vector<CourseTriangle> Triangles;
};

[[nodiscard]] bool DecodeFacetArchiveFile(const std::filesystem::path& FilePath,
                                          bool                         ForceFlatNormals,
                                          DecodedFacetArchive&         ArchiveDelivery);

//------------------------------------------------------------------------------------------------------------------------
//                                                  CONTROLVEHICLE ASSEMBLY
//------------------------------------------------------------------------------------------------------------------------

struct ControlVehicleConstants
{
    static constexpr float Mass                = 1300.0f;    // [kg]
    static constexpr float CoMHeight           = 0.53f;      // [m]
    static constexpr float TyreRadius          = 0.50855f;   // [m]
    static constexpr float TyreWidth           = 0.305f;     // [m]
    static constexpr float TyreRimRadius       = 0.34460f;   // [m]
    static constexpr float AuthoredRimRadius   = 0.344818f;  // [m]
    static constexpr float UnsprungMassWheel   = 25.0f;      // [kg]
    static constexpr float FrontWeightFraction = 0.54f;      // [-]
    static constexpr float AxleFrontX          = 1.7274f;    // [m]
    static constexpr float AxleRearX           = -1.6686f;   // [m]
    static constexpr float AxleHalfTrackY      = 1.0475f;    // [m]
    static constexpr float AxleSocketZ         = 0.0914f;    // [m]
    static constexpr float StrutMountZ         = 0.6436f;    // [m]

    [[nodiscard]] static constexpr float CoMAuthoredZ() noexcept
    {
        return CoMHeight - (TyreRadius - AxleSocketZ);   // 0.11285 m
    }
};

struct ControlVehicleAssembly
{
    bool                                      LoadedFromArchives = false;
    uint32_t                                  AuthoredBodyFacets = 0u;
    uint32_t                                  AuthoredRimFacets  = 0u;
    uint32_t                                  AuthoredTyreFacets = 0u;
    std::vector<CourseTriangle>               BodyOpaque;        // BodyCoat, Plastic, Trim, Headlight, Taillight, Cabin, Mirrors
    std::vector<CourseTriangle>               BodyGlazing;       // VehicleGlass (74 canopy/window triangles)
    std::vector<CourseTriangle>               RoofSlimLed;       // Optional Slim LED roof bar
    std::vector<CourseTriangle>               RoofRallyPod;      // Optional dual Rally Pod roof fixture
    std::vector<CourseTriangle>               WheelLeft;         // +Y left-side Tyre + RimBase + blue Hub spokes
    std::vector<CourseTriangle>               WheelRight;        // -Y mirrored right-side Tyre + RimBase + Hub
    std::vector<std::array<float, 2>>         SideHullSamples;   // (X, Z) body-local hull samples for Inspector diagram
};

[[nodiscard]] ControlVehicleAssembly ConstructControlVehicleAssembly(
    const std::filesystem::path& VehicleArchiveDirectory);

//------------------------------------------------------------------------------------------------------------------------
//                                                  PROVING GROUNDS SCENE
//------------------------------------------------------------------------------------------------------------------------

enum class ClusteredLightKind : uint32_t
{
    Omni       = 0u,
    StreetLamp = 1u,
    LowBeam    = 2u,
    HighBeam   = 3u,
    FogBeam    = 4u,
    Floodlight = 5u
};

struct ProvingGroundsLight
{
    ClusteredLightKind   Kind      = ClusteredLightKind::Omni;
    std::array<float, 3> Position  = { 0.0f, 0.0f, 5.0f };
    std::array<float, 3> Direction = { 0.0f, 0.0f, -1.0f };
    std::array<float, 3> Colour    = { 1.0f, 0.88f, 0.65f };
    float                Intensity = 42.0f;
    float                Reach     = 24.0f;
    float                InnerCos  = -1.0f;
    float                OuterCos  = -1.0f;
};

struct ArenaZoneEntry
{
    const char*          Name     = "";
    std::array<float, 2> Position = { -40.0f, 0.0f };
    float                Yaw      = 0.0f;   // [rad]
};

struct BreakablePropEntry
{
    std::string                 Name;
    std::string                 MaterialLabel;
    std::array<float, 3>        Position = { 0.0f, 0.0f, 0.0f };
    float                       Yaw      = 0.0f;
    bool                        Intact   = true;
    std::vector<CourseTriangle> Triangles;
};

struct ProvingGroundsAssembly
{
    std::vector<CourseTriangle>       StaticWorld;      // Grass, Asphalt, Markings, Kerbs, Ramp, Barriers, Hangar, Tunnel, Tower, Poles, Cones
    std::vector<BreakablePropEntry>   Breakables;       // 13 Smash Alley breakable props
    std::vector<ProvingGroundsLight>  StaticLights;     // 20 Roadway StreetLamp poles + 10 Omni LampPosts
    std::vector<ArenaZoneEntry>       Zones;            // 9 Proving Grounds teleport zones
    uint32_t                          ConeCount = 0u;
};

[[nodiscard]] ProvingGroundsAssembly ConstructProvingGroundsAssembly();

[[nodiscard]] float QueryProvingGroundsCourseHeight(float WorldX, float WorldY) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                  CAMERA RIGS
//------------------------------------------------------------------------------------------------------------------------

enum class EverestCameraMode : uint8_t
{
    Chase     = 0,
    Orbit     = 1,
    Cockpit   = 2,
    HmiTablet = 3,
    Hood      = 4,
    Free      = 5,
    TyreFl    = 6,
    TyreFr    = 7,
    TyreRr    = 8,
    TyreRl    = 9
};

struct ResolvedCameraFrame
{
    std::array<float, 3> Eye            = { -47.0f, 0.0f, 3.13f };
    std::array<float, 3> Target         = { -40.0f, 0.0f, 1.33f };
    std::array<float, 3> Up             = { 0.0f, 0.0f, 1.0f };
    float                VerticalFovDeg = 60.0f;
    bool                 Interior       = false;
};

[[nodiscard]] inline const std::array<ArenaZoneEntry, 9>& QueryProvingGroundsZones() noexcept
{
    static constexpr float HalfPi = 1.57079632679f;
    static constexpr std::array<ArenaZoneEntry, 9> Zones = {{
        { "Start grid",   { -40.0f,   0.0f }, 0.0f   },
        { "Jump ramp",    {   8.0f,   0.0f }, 0.0f   },
        { "Skidpad",      { -46.0f,  30.0f }, HalfPi },
        { "Slalom",       {  30.0f,   0.0f }, 0.0f   },
        { "Moose test",   {  44.0f,  17.2f }, 0.0f   },
        { "Hangar apron", {  14.0f,  38.5f }, 0.0f   },
        { "South tunnel", { -34.0f, -48.0f }, 0.0f   },
        { "Smash alley",  {   0.0f, -28.5f }, 0.0f   },
        { "Burnout box",  { -36.0f, -28.0f }, 0.0f   }
    }};
    return Zones;
}

[[nodiscard]] inline const char* QueryEverestCameraLabel(EverestCameraMode Mode) noexcept
{
    switch (Mode)
    {
        case EverestCameraMode::Chase:     return "Chase";
        case EverestCameraMode::Orbit:     return "Orbit";
        case EverestCameraMode::Cockpit:   return "Driver";
        case EverestCameraMode::HmiTablet: return "HMI Screen";
        case EverestCameraMode::Hood:      return "Hood";
        case EverestCameraMode::Free:      return "Overview";
        case EverestCameraMode::TyreFl:    return "Front Left";
        case EverestCameraMode::TyreFr:    return "Front Right";
        case EverestCameraMode::TyreRr:    return "Rear Right";
        case EverestCameraMode::TyreRl:    return "Rear Left";
    }
    return "Chase";
}

[[nodiscard]] ResolvedCameraFrame ResolveProvingGroundsCamera(
    EverestCameraMode    Mode,
    std::array<float, 3> VehiclePosition,
    float                VehicleYawRad,
    float                OrbitYawRad,
    float                OrbitPitchRad,
    float                OrbitDistanceM,
    float                AspectRatio) noexcept;

} // namespace Frontier
