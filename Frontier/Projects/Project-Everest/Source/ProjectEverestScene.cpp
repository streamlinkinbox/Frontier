// 🧩 Project Everest scene assembly: global Engine Folders + Sun/Flare/Atmosphere/Clouds/Wind + custom Vehicle & GPU tiers.

#include "ProjectEverestScene.h"

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Interface/SceneDirectoryPanel.h"
#include "Engine/Scene/SceneDirectory.h"

#if defined(FRONTIER_WITH_INTERFACE_SDK)
#include "Engine/Interface/StackRailControls.h"
#include <imgui.h>
#endif

#include <array>
#include <cstdio>
#include <string>
#include <utility>

namespace Frontier
{

//========================================================================================================================
//                                           EVEREST ILLUMINATION RECORD
//========================================================================================================================

EverestIlluminationRecord::EverestIlluminationRecord()
    : RecordEntry(RecordClassification::Illumination)
{
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
    RecordEditorDescription Description;
    Description.Identifier         = "illumination";
    Description.Eyebrow            = "RENDER / DUAL-TIER GPU ARCHITECTURE";
    Description.DisplayName        = "Illumination & GI";
    Description.Subtitle           = "RTX hardware raytracing + ReSTIR GI with GTX SDF + software BVH fallback.";
    Description.InstrumentCategory = InspectorInstrumentCategory::Custom;
    Description.AccentColour       = 0xFFA3D97Au;
    Description.EnabledTarget      = &Enabled;
    Description.QueryLiveBadge     = [this]() {
        return TierSelectionIndex == 0 ? std::string("RTX RayQuery + ReSTIR") : std::string("GTX SDF + BVH");
    };
#if defined(FRONTIER_WITH_INTERFACE_SDK)
    Description.ConstructCustomInspector = [this](SceneDirectory&, SceneDirectoryPanel&) {
        const KitFaces Faces  = ResolveKitFaces();
        const KitTheme Theme  = ResolveKitTheme();
        ImDrawList*    Ink    = ImGui::GetWindowDrawList();
        const ImVec2   Origin = ImGui::GetCursorScreenPos();
        const float    Width  = std::max(120.0f, ImGui::GetContentRegionAvail().x - 24.0f);
        const float    CardH  = 92.0f;
        const ImU32    Accent = TierSelectionIndex == 0 ? KitInkHex(0x2fe6a7) : KitInkHex(0x63a8ff);

        KitPillSurface(Ink, Origin, ImVec2(Origin.x + Width, Origin.y + CardH), KitInkHex(0x0b1012),
                       KitInkWithAlpha(Accent, 0.55f), 10.0f);
        KitDrawTracked(Ink, Faces.Medium, 9.5f, ImVec2(Origin.x + 12.0f, Origin.y + 10.0f), Accent,
                       TierSelectionIndex == 0 ? "ACTIVE TIER A  /  RTX HARDWARE RAYTRACING"
                                               : "ACTIVE TIER B  /  GTX SDF + BVH FALLBACK",
                       1.0f);
        KitDrawText(Ink, Faces.SemiBold, 13.5f, ImVec2(Origin.x + 12.0f, Origin.y + 28.0f), KitInkHex(0xeef3f0),
                    TierSelectionIndex == 0
                        ? "VK_KHR_ray_query + Path-Replay ReSTIR GI + Dielectric Glazing"
                        : "4-Cascade Toroidal SDF Cone GI + SurfaceCards + OrderedSoftwareBvh");
        char BudgetLine[112] = {};
        if (TierSelectionIndex == 0)
        {
            std::snprintf(BudgetLine, sizeof(BudgetLine),
                          "Reservoir cap M=%.0f  \xC2\xB7  %d spatial taps  \xC2\xB7  exact hardware BVH reflections",
                          TemporalHistoryCap, static_cast<int>(SpatialReuseNeighbors));
        }
        else
        {
            std::snprintf(BudgetLine, sizeof(BudgetLine),
                          "SDF span %.0f m  \xC2\xB7  BVH roughness <= %.2f  \xC2\xB7  zero RT-core requirement",
                          SdfClipmapSpanMetres, BvhReflectionRoughnessCut);
        }
        KitDrawText(Ink, Faces.Regular, 11.0f, ImVec2(Origin.x + 12.0f, Origin.y + 52.0f), KitInkHex(0x97a29b),
                    BudgetLine);
        KitDrawText(Ink, Faces.Regular, 10.0f, ImVec2(Origin.x + 12.0f, Origin.y + 71.0f), Theme.Faint,
                    "Raytracing is restricted to RTX GPUs; GTX / non-RT adapters run SDF + BVH reflections.");

        ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + CardH + 10.0f));
        ImGui::Dummy(ImVec2(Width, 0.0f));
    };
#endif
    Description.OptionFields.push_back(RecordEditorOptionField{
        "GPU EXECUTION TIER",
        "Active GPU path",
        {"RTX Raytracing + ReSTIR", "GTX SDF + BVH Fallback"},
        &TierSelectionIndex,
    });
    Description.Switches.push_back(
        RecordEditorSwitchField{"TRANSPORT FEATURES", "Dielectric cabin glazing transmittance", &DielectricGlazingActive});
    Description.Switches.push_back(
        RecordEditorSwitchField{"TRANSPORT FEATURES", "Multi-bounce surface cards cache", &SurfaceCardsActive});
    Description.ScalarFields.push_back(RecordEditorScalarField{
        "RTX RESTIR & GTX FALLBACK BUDGET", "Temporal reservoir cap", "M", 1.0, 32.0, 1.0, 0, &TemporalHistoryCap});
    Description.ScalarFields.push_back(RecordEditorScalarField{
        "RTX RESTIR & GTX FALLBACK BUDGET", "Spatial reuse neighbours", "taps", 0.0, 8.0, 1.0, 0,
        &SpatialReuseNeighbors});
    Description.ScalarFields.push_back(RecordEditorScalarField{
        "RTX RESTIR & GTX FALLBACK BUDGET", "GTX SDF clipmap outer span", "m", 40.0, 640.0, 10.0, 0,
        &SdfClipmapSpanMetres});
    Description.ScalarFields.push_back(RecordEditorScalarField{
        "RTX RESTIR & GTX FALLBACK BUDGET", "GTX BVH reflection roughness cutoff", "", 0.05, 1.0, 0.01, 2,
        &BvhReflectionRoughnessCut});
    Description.ScalarFields.push_back(RecordEditorScalarField{
        "RTX RESTIR & GTX FALLBACK BUDGET", "Camera exposure bias", "EV", -4.0, 4.0, 0.1, 1, &ExposureCompensationEv});
    RegisterEditorDescription(std::move(Description));
#endif
}

//========================================================================================================================
//                                              EVEREST TYRE RECORD
//========================================================================================================================

EverestTyreRecord::EverestTyreRecord(const char* CornerLabel, double DefaultCamberDeg)
    : RecordEntry(RecordClassification::Tyre)
    , CamberDegrees(DefaultCamberDeg)
{
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
    RecordEditorDescription Description;
    Description.Identifier         = "tyre";
    Description.Eyebrow            = "VEHICLE / PACEJKA & CONTINUUM CARCASS";
    Description.DisplayName        = CornerLabel;
    Description.Subtitle           = "Corner inflation, camber geometry, and Pacejka thermal contact state.";
    Description.InstrumentCategory = InspectorInstrumentCategory::Standard;
    Description.AccentColour       = 0xFFFFA863u;
    Description.EnabledTarget      = &Enabled;
    Description.QueryLiveBadge     = [this]() {
        char Buffer[48] = {};
        std::snprintf(Buffer, sizeof(Buffer), "%.0f kPa \xC2\xB7 %.0f\xC2\xB0" "C", PressureKilopascals, CarcassCelsius);
        return std::string(Buffer);
    };
    Description.ScalarFields.push_back(
        RecordEditorScalarField{"TYRE SETUP", "Cold inflation pressure", "kPa", 160.0, 320.0, 2.0, 0, &PressureKilopascals});
    Description.ScalarFields.push_back(
        RecordEditorScalarField{"TYRE SETUP", "Static camber", "\xC2\xB0", -5.0, 2.0, 0.1, 1, &CamberDegrees});
    Description.ScalarFields.push_back(
        RecordEditorScalarField{"TYRE SETUP", "Carcass temperature", "\xC2\xB0" "C", 15.0, 130.0, 1.0, 0, &CarcassCelsius});
    Description.ScalarFields.push_back(
        RecordEditorScalarField{"TYRE SETUP", "Peak friction mu", "\xCE\xBC", 0.50, 1.80, 0.02, 2, &PeakGripMu});
    RegisterEditorDescription(std::move(Description));
#endif
}

//========================================================================================================================
//                                             EVEREST VEHICLE RECORD
//========================================================================================================================

EverestVehicleRecord::EverestVehicleRecord()
    : RecordEntry(RecordClassification::Vehicle)
{
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
    RecordEditorDescription Description;
    Description.Identifier         = "vehicle";
    Description.Eyebrow            = "VEHICLE / CUSTOM PROJECT INSPECTOR";
    Description.DisplayName        = "ControlVehicle";
    Description.Subtitle           = "Sprung chassis dynamics, 4-corner suspension, and AWD powertrain calibration.";
    Description.InstrumentCategory = InspectorInstrumentCategory::Custom;
    Description.AccentColour       = 0xFFA7E62Fu;
    Description.EnabledTarget      = &Enabled;
    Description.QueryLiveBadge     = [this]() {
        char Buffer[48] = {};
        std::snprintf(Buffer, sizeof(Buffer), "%.0f kg \xC2\xB7 AWD %.0f%% R", SprungMassKilograms,
                      AwdRearTorqueFraction * 100.0);
        return std::string(Buffer);
    };
#if defined(FRONTIER_WITH_INTERFACE_SDK)
    Description.ConstructCustomInspector = [this](SceneDirectory&, SceneDirectoryPanel&) {
        const KitFaces Faces  = ResolveKitFaces();
        const KitTheme Theme  = ResolveKitTheme();
        ImDrawList*    Ink    = ImGui::GetWindowDrawList();
        const ImVec2   Origin = ImGui::GetCursorScreenPos();
        const float    Width  = std::max(120.0f, ImGui::GetContentRegionAvail().x - 24.0f);
        const float    CardH  = 104.0f;
        const ImU32    Accent = KitInkHex(0x2fe6a7);

        KitPillSurface(Ink, Origin, ImVec2(Origin.x + Width, Origin.y + CardH), KitInkHex(0x0b1012), Theme.Line, 10.0f);
        KitDrawTracked(Ink, Faces.Medium, 9.5f, ImVec2(Origin.x + 12.0f, Origin.y + 9.0f), Accent,
                       "CHASSIS TELEMETRY  /  4-CORNER SUSPENSION", 1.0f);

        const std::array<const char*, 4> Corners{"FL", "FR", "RL", "RR"};
        const float                      SlotW = (Width - 28.0f) * 0.25f;
        for (size_t Index = 0; Index < 4; ++Index)
        {
            const float X = Origin.x + 12.0f + static_cast<float>(Index) * (SlotW + 2.0f);
            const float Y = Origin.y + 30.0f;
            KitPillSurface(Ink, ImVec2(X, Y), ImVec2(X + SlotW - 4.0f, Y + 60.0f), Theme.Surface, Theme.LineSoft, 7.0f);
            KitDrawText(Ink, Faces.SemiBold, 11.0f, ImVec2(X + 8.0f, Y + 6.0f), Accent, Corners[Index]);
            char RateText[32] = {};
            std::snprintf(RateText, sizeof(RateText), "%.0f kN/m",
                          SpringRateKilonewtonsM * (Index >= 2 ? 1.06 : 1.0));
            KitDrawText(Ink, Faces.Regular, 10.0f, ImVec2(X + 8.0f, Y + 24.0f), KitInkHex(0xeef3f0), RateText);
            char RideText[32] = {};
            std::snprintf(RideText, sizeof(RideText), "%.0f mm", RideHeightMillimetres);
            KitDrawText(Ink, Faces.Regular, 9.5f, ImVec2(X + 8.0f, Y + 40.0f), KitInkHex(0x97a29b), RideText);
        }
        ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + CardH + 10.0f));
        ImGui::Dummy(ImVec2(Width, 0.0f));
    };
#endif
    Description.OptionFields.push_back(RecordEditorOptionField{
        "POWERTRAIN CALIBRATION",
        "Drive dynamics profile",
        {"Comfort", "Proving Sport", "Drift Telemetry"},
        &DriveModeIndex,
    });
    Description.Switches.push_back(
        RecordEditorSwitchField{"STABILITY", "Closed-loop yaw stability vectoring", &StabilityControlActive});
    Description.ScalarFields.push_back(RecordEditorScalarField{
        "CHASSIS & SUSPENSION", "Sprung chassis mass", "kg", 900.0, 2800.0, 10.0, 0, &SprungMassKilograms});
    Description.ScalarFields.push_back(RecordEditorScalarField{
        "CHASSIS & SUSPENSION", "AWD rear torque split", "", 0.0, 1.0, 0.01, 2, &AwdRearTorqueFraction});
    Description.ScalarFields.push_back(RecordEditorScalarField{
        "CHASSIS & SUSPENSION", "Front brake bias", "", 0.40, 0.80, 0.01, 2, &FrontBrakeBias});
    Description.ScalarFields.push_back(RecordEditorScalarField{
        "CHASSIS & SUSPENSION", "Wheel spring rate", "kN/m", 20.0, 120.0, 1.0, 0, &SpringRateKilonewtonsM});
    Description.ScalarFields.push_back(RecordEditorScalarField{
        "CHASSIS & SUSPENSION", "Damper critical ratio", "", 0.20, 1.40, 0.02, 2, &DampingRatio});
    Description.ScalarFields.push_back(RecordEditorScalarField{
        "CHASSIS & SUSPENSION", "Static ride height", "mm", 75.0, 240.0, 1.0, 0, &RideHeightMillimetres});
    Description.ScalarFields.push_back(RecordEditorScalarField{
        "CHASSIS & SUSPENSION", "Aerodynamic downforce Cl", "Cl", 0.0, 1.50, 0.02, 2, &AeroDownforceCoefficient});
    RegisterEditorDescription(std::move(Description));
#endif
}

//========================================================================================================================
//                                               PROJECT EVEREST SCENE
//========================================================================================================================

ProjectEverestScene::ProjectEverestScene()
{
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
    SceneDirectory& Directory = SceneDirectory::Instance();

    (void)Directory.ConfigureName(CourseGeometry.Token(), "ProvingGroundsCourse");
    (void)Directory.ConfigureName(ChaseCamera.Token(), "ChaseCamera");

    // Enclose records inside their global FolderRecords (matching VehicleHMI EditorShell.js GroupOrder).
    (void)Directory.ConfigureParent(Illumination.Token(), RenderFolder.Token());

    (void)Directory.ConfigureParent(Atmosphere.Token(), EnvironmentFolder.Token());
    (void)Directory.ConfigureParent(Clouds.Token(), EnvironmentFolder.Token());
    (void)Directory.ConfigureParent(Sun.Token(), EnvironmentFolder.Token());
    (void)Directory.ConfigureParent(LensFlare.Token(), EnvironmentFolder.Token());

    (void)Directory.ConfigureParent(Wind.Token(), ForcefieldFolder.Token());

    (void)Directory.ConfigureParent(Vehicle.Token(), VehicleFolder.Token());
    (void)Directory.ConfigureParent(FrontLeftTyre.Token(), Vehicle.Token());
    (void)Directory.ConfigureParent(FrontRightTyre.Token(), Vehicle.Token());
    (void)Directory.ConfigureParent(RearLeftTyre.Token(), Vehicle.Token());
    (void)Directory.ConfigureParent(RearRightTyre.Token(), Vehicle.Token());

    (void)Directory.ConfigureParent(CourseGeometry.Token(), LevelFolder.Token());
    (void)Directory.ConfigureParent(ChaseCamera.Token(), LevelFolder.Token());
#endif
}

void ProjectEverestScene::ServiceSpawnRequest(SceneDirectoryPanel& DirectoryPanel)
{
    const std::optional<RecordClassification> Requested = DirectoryPanel.TakeSpawnRequest();
    if (!Requested.has_value())
    {
        return;
    }

#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
    SceneDirectory& Directory = SceneDirectory::Instance();
    if (*Requested == RecordClassification::Folder)
    {
        ++DynamicFolderSequence;
        char FolderName[48] = {};
        std::snprintf(FolderName, sizeof(FolderName), "Folder %02u", DynamicFolderSequence);
        auto Created = std::make_unique<FolderRecord>(FolderName, "CUSTOM GROUP", 0xFFA3D97Au);
        const RecordToken CurrentSelected = DirectoryPanel.QuerySelected();
        if (CurrentSelected.IsValid() &&
            Directory.QueryClassification(CurrentSelected) == RecordClassification::Folder)
        {
            (void)Directory.ConfigureParent(Created->Token(), CurrentSelected);
        }
        const RecordToken CreatedToken = Created->Token();
        SpawnedFolders.push_back(std::move(Created));
        DirectoryPanel.Synchronize(Directory);
        DirectoryPanel.Select(CreatedToken);
        DirectoryPanel.Synchronize(Directory);
        return;
    }

    auto Spawned = std::make_unique<SceneRecord>(*Requested);
    const RecordToken CurrentSelected = DirectoryPanel.QuerySelected();
    if (CurrentSelected.IsValid() && Directory.QueryClassification(CurrentSelected) == RecordClassification::Folder)
    {
        (void)Directory.ConfigureParent(Spawned->Token(), CurrentSelected);
    }
    const RecordToken CreatedToken = Spawned->Token();
    SpawnedRecords.push_back(std::move(Spawned));
    DirectoryPanel.Synchronize(Directory);
    DirectoryPanel.Select(CreatedToken);
    DirectoryPanel.Synchronize(Directory);
#endif
}

} // namespace Frontier
