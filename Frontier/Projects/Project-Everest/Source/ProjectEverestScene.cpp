// 🧩 Project Everest scene assembly: global Engine Folders + Observatories + 8-Tab ControlVehicle + Tier-3 Studios.

#include "ProjectEverestScene.h"

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Interface/SceneDirectoryPanel.h"
#include "Engine/Scene/SceneDirectory.h"

#if defined(FRONTIER_WITH_INTERFACE_SDK)
#include "Engine/Interface/StackRailControls.h"
#include <imgui.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
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
        const float    CardH  = 96.0f;
        const ImU32    Accent = TierSelectionIndex == 0 ? KitInkHex(0x2fe6a7) : KitInkHex(0x63a8ff);

        KitPillSurface(Ink, Origin, ImVec2(Origin.x + Width, Origin.y + CardH), Theme.Surface,
                       KitInkWithAlpha(Accent, 0.55f), 10.0f);
        KitDrawTracked(Ink, Faces.Medium, 9.5f, ImVec2(Origin.x + 12.0f, Origin.y + 10.0f), Accent,
                       TierSelectionIndex == 0 ? "ACTIVE TIER A  /  RTX HARDWARE RAYTRACING"
                                               : "ACTIVE TIER B  /  GTX SDF + BVH FALLBACK",
                       1.0f);
        KitDrawText(Ink, Faces.SemiBold, 13.2f, ImVec2(Origin.x + 12.0f, Origin.y + 28.0f), Theme.Text,
                    TierSelectionIndex == 0
                        ? "VK_KHR_ray_query + Path-Replay ReSTIR GI + Dielectric Glazing"
                        : "4-Cascade SceneClip0..3 SDF GI + SurfaceCards + OrderedSoftwareBvh");
        char BudgetLine[120] = {};
        if (TierSelectionIndex == 0)
        {
            std::snprintf(BudgetLine, sizeof(BudgetLine),
                          "Reservoir cap M=%.0f  \xC2\xB7  %d spatial taps  \xC2\xB7  Snell + Fresnel cabin canopy",
                          TemporalHistoryCap, static_cast<int>(SpatialReuseNeighbors));
        }
        else
        {
            std::snprintf(BudgetLine, sizeof(BudgetLine),
                          "Toroidal SDF span %.0f m  \xC2\xB7  SAH BVH roughness <= %.2f  \xC2\xB7  zero RT-cores",
                          SdfClipmapSpanMetres, BvhReflectionRoughnessCut);
        }
        KitDrawText(Ink, Faces.Regular, 10.8f, ImVec2(Origin.x + 12.0f, Origin.y + 52.0f), Theme.Muted,
                    BudgetLine);
        KitDrawText(Ink, Faces.Regular, 9.8f, ImVec2(Origin.x + 12.0f, Origin.y + 72.0f), Theme.Faint,
                    "Raytracing runs exclusively on RTX GPUs; GTX / non-RT adapters execute SDF + BVH reflections.");

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
    Description.OptionFields.push_back(RecordEditorOptionField{
        "VIEWPORT DIAGNOSTIC AOV",
        "Displayed render buffer",
        {"Final Lit", "Indirect GI", "Reflections", "SDF Cascades", "Dielectric Glass", "World Normals"},
        &InspectionAovIndex,
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
    Description.Subtitle           = "Corner inflation, camber geometry, and Pacejka thermal contact configuration.";
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
        RecordEditorScalarField{"TYRE SETUP", "Peak friction mu", "mu", 0.50, 1.80, 0.02, 2, &PeakGripMu});
    RegisterEditorDescription(std::move(Description));
#endif
}

//========================================================================================================================
//                                             EVEREST VEHICLE RECORD
//========================================================================================================================

SpectrumRgb EverestVehicleRecord::QueryBodyPaintAlbedo() const noexcept
{
    switch (std::clamp(PaintFinishPreset, 0, 3))
    {
        case 0: return {0.11, 0.34, 0.52};   // Everest Glacier Blue
        case 1: return {0.62, 0.24, 0.10};   // Solar Copper Metallic
        case 2: return {0.10, 0.44, 0.32};   // Alpine Racing Mint
        default: return {0.09, 0.10, 0.12};  // Obsidian Carbon
    }
}

EverestVehicleRecord::EverestVehicleRecord()
    : RecordEntry(RecordClassification::Vehicle)
{
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
    RecordEditorDescription Description;
    Description.Identifier         = "vehicle";
    Description.Eyebrow            = "VEHICLE / 8-MODULE CUSTOM INSPECTOR";
    Description.DisplayName        = "ControlVehicle";
    Description.Subtitle           = "8-module calibration: Overview, Chassis, Engine, Tyres, Suspension, Lights, HMI, Mirrors.";
    Description.InstrumentCategory = InspectorInstrumentCategory::Custom;
    Description.AccentColour       = 0xFFA7E62Fu;
    Description.EnabledTarget      = &Enabled;
    Description.QueryLiveBadge     = [this]() {
        char Buffer[48] = {};
        std::snprintf(Buffer, sizeof(Buffer), "%.0f kW \xC2\xB7 AWD %.0f%% R", PeakPowerKilowatts,
                      AwdRearTorqueFraction * 100.0);
        return std::string(Buffer);
    };
#if defined(FRONTIER_WITH_INTERFACE_SDK)
    Description.ConstructCustomInspector = [this](SceneDirectory&, SceneDirectoryPanel&) {
        const KitFaces Faces  = ResolveKitFaces();
        const KitTheme Theme  = ResolveKitTheme();
        ImDrawList*    Ink    = ImGui::GetWindowDrawList();
        const ImVec2   Origin = ImGui::GetCursorScreenPos();
        const float    Width  = std::max(140.0f, ImGui::GetContentRegionAvail().x - 24.0f);
        const ImU32    Accent = KitInkHex(0x2fe6a7);

        // 2x4 8-Module Selector Grid!
        static constexpr std::array<const char*, 8> ModuleLabels{
            "Overview", "Chassis", "Engine", "Tyres",
            "Suspension", "Lights", "HMI & Cabin", "Mirrors"};
        const float Gap  = 4.0f;
        const float TabW = (Width - Gap * 3.0f) * 0.25f;
        const float TabH = 26.0f;
        for (uint32_t Tab = 0; Tab < 8u; ++Tab)
        {
            const uint32_t Row = Tab / 4u;
            const uint32_t Col = Tab % 4u;
            const float    TX  = Origin.x + static_cast<float>(Col) * (TabW + Gap);
            const float    TY  = Origin.y + static_cast<float>(Row) * (TabH + Gap);
            const bool     Sel = (ActiveModuleTab == Tab);
            bool           Hov = false;
            char           Id[40] = {};
            std::snprintf(Id, sizeof(Id), "##VehModTab%u", Tab);
            if (KitTarget(Id, ImVec2(TX, TY), ImVec2(TabW, TabH), Hov))
            {
                ActiveModuleTab = Tab;
            }
            KitPillSurface(Ink, ImVec2(TX, TY), ImVec2(TX + TabW, TY + TabH),
                           Sel ? KitInkWithAlpha(Accent, 0.22f) : (Hov ? Theme.Surface3 : Theme.Surface2),
                           Sel ? KitInkWithAlpha(Accent, 0.70f) : Theme.LineSoft, 6.5f);
            const float TW = KitTextWidth(Faces.Medium, 10.6f, ModuleLabels[Tab]);
            KitDrawText(Ink, Faces.Medium, 10.6f, ImVec2(TX + std::max(3.0f, (TabW - TW) * 0.5f), TY + 6.5f),
                        Sel ? Theme.Text : Theme.Muted, ModuleLabels[Tab]);
        }

        // Top-Down Chassis Plan + 4-Corner Telemetry Diagram + Collision X-Ray pill
        const float CardTop = Origin.y + 2.0f * (TabH + Gap) + 6.0f;
        const float CardH   = 112.0f;
        KitPillSurface(Ink, ImVec2(Origin.x, CardTop), ImVec2(Origin.x + Width, CardTop + CardH),
                       Theme.Surface, Theme.Line, 10.0f);

        const ImVec2 CarCentre(Origin.x + 68.0f, CardTop + CardH * 0.52f);
        Ink->AddRectFilled(ImVec2(CarCentre.x - 19.0f, CarCentre.y - 36.0f),
                           ImVec2(CarCentre.x + 19.0f, CarCentre.y + 36.0f),
                           KitInkWithAlpha(Accent, 0.20f), 8.0f);
        Ink->AddRect(ImVec2(CarCentre.x - 19.0f, CarCentre.y - 36.0f),
                     ImVec2(CarCentre.x + 19.0f, CarCentre.y + 36.0f), Accent, 8.0f, 0, 1.4f);
        // 4 wheel blocks (FL, FR, RL, RR)
        for (int WX : {-1, +1})
        {
            for (int WY : {-1, +1})
            {
                const ImVec2 WPos(CarCentre.x + WX * 22.0f, CarCentre.y + WY * 24.0f);
                Ink->AddRectFilled(ImVec2(WPos.x - 4.0f, WPos.y - 8.0f), ImVec2(WPos.x + 4.0f, WPos.y + 8.0f),
                                   KitInkHex(0xffa863), 2.0f);
            }
        }
        if (CollisionOverlayEnabled)
        {
            Ink->AddCircle(CarCentre, 42.0f, KitInkHex(0xff6b8b), 28, 1.3f);
        }

        // Module-specific telemetry readout on right side of hero card!
        const float ReadoutX = Origin.x + 128.0f;
        KitDrawTracked(Ink, Faces.Medium, 9.5f, ImVec2(ReadoutX, CardTop + 12.0f), Accent,
                       ModuleLabels[ActiveModuleTab % 8u], 1.1f);
        char Line1[96] = {}, Line2[96] = {};
        switch (ActiveModuleTab % 8u)
        {
            case 0:
                std::snprintf(Line1, sizeof(Line1), "Mass %.0f kg  \xC2\xB7  AWD %.0f%% Rear", SprungMassKilograms, AwdRearTorqueFraction * 100.0);
                std::snprintf(Line2, sizeof(Line2), "Downforce Cl %.2f  \xC2\xB7  SOC %.0f%%", AeroDownforceCoefficient, BatterySocPercent);
                break;
            case 1:
                std::snprintf(Line1, sizeof(Line1), "Wheelbase %.2f m  \xC2\xB7  Clearcoat R %.2f", WheelbaseMetres, ClearcoatRoughness);
                std::snprintf(Line2, sizeof(Line2), "Collision X-Ray: %s", CollisionOverlayEnabled ? "ACTIVE IN VIEWPORT" : "Hidden");
                break;
            case 2:
                std::snprintf(Line1, sizeof(Line1), "Dual E-Axle %.0f kW (%.0f PS)", PeakPowerKilowatts, PeakPowerKilowatts * 1.3596);
                std::snprintf(Line2, sizeof(Line2), "Redline %.0f rpm  \xC2\xB7  Pack %.0f%% SOC", RedlineRpm, BatterySocPercent);
                break;
            case 3:
                std::snprintf(Line1, sizeof(Line1), "FL/FR 235 kPa -1.6\xC2\xB0  \xC2\xB7  RL/RR 235 kPa -1.2\xC2\xB0");
                std::snprintf(Line2, sizeof(Line2), "Pacejka Peak Grip mu = 1.18  \xC2\xB7  68\xC2\xB0" "C");
                break;
            case 4:
                std::snprintf(Line1, sizeof(Line1), "Spring %.0f kN/m  \xC2\xB7  Damper ratio %.2f", SpringRateKilonewtonsM, DampingRatio);
                std::snprintf(Line2, sizeof(Line2), "Static ride height %.0f mm", RideHeightMillimetres);
                break;
            case 5:
                std::snprintf(Line1, sizeof(Line1), "Headlamps %s (%.0f K)", HeadlampsEnabled ? "ON" : "OFF", HeadlampKelvin);
                std::snprintf(Line2, sizeof(Line2), "Tail brake %s  \xC2\xB7  Hazards %s",
                              BrakeLampsEnabled ? "ON" : "OFF", HazardLampsEnabled ? "FLASH" : "OFF");
                break;
            case 6:
                std::snprintf(Line1, sizeof(Line1), "Cluster OLED %.0f nits (visible through glass)", CabinHmiLuminanceNits);
                std::snprintf(Line2, sizeof(Line2), "Acoustic 6 mm laminated glazing IOR 1.52");
                break;
            default:
                std::snprintf(Line1, sizeof(Line1), "Wing mirror fold %.0f\xC2\xB0", MirrorFoldDegrees);
                std::snprintf(Line2, sizeof(Line2), "Planar / OrderedSoftwareBvh rear-view trace");
                break;
        }
        KitDrawText(Ink, Faces.SemiBold, 12.2f, ImVec2(ReadoutX, CardTop + 32.0f), Theme.Text, Line1);
        KitDrawText(Ink, Faces.Regular, 10.8f, ImVec2(ReadoutX, CardTop + 52.0f), Theme.Muted, Line2);

        // Quick Collision X-Ray toggle pill right inside the hero card!
        bool XRayHov = false;
        const ImVec2 XMin(ReadoutX, CardTop + 76.0f);
        const ImVec2 XSize(Width - 140.0f, 24.0f);
        if (KitTarget("##VehXRayBtn", XMin, XSize, XRayHov))
        {
            CollisionOverlayEnabled = !CollisionOverlayEnabled;
        }
        KitPillSurface(Ink, XMin, ImVec2(XMin.x + XSize.x, XMin.y + XSize.y),
                       CollisionOverlayEnabled ? KitInkRgba(255, 107, 139, 48) : Theme.Surface2,
                       CollisionOverlayEnabled ? KitInkHex(0xff6b8b) : Theme.LineSoft, 6.0f);
        KitDrawText(Ink, Faces.Medium, 10.5f, ImVec2(XMin.x + 10.0f, XMin.y + 5.5f),
                    CollisionOverlayEnabled ? KitInkHex(0xff9bb0) : Theme.Muted,
                    CollisionOverlayEnabled ? "Viewport Collision X-Ray: ON" : "Toggle Viewport Collision X-Ray");

        ImGui::SetCursorScreenPos(ImVec2(Origin.x, CardTop + CardH + 10.0f));
        ImGui::Dummy(ImVec2(Width, 0.0f));
    };
#endif
    Description.OptionFields.push_back(RecordEditorOptionField{
        "EXTERIOR FINISH & POWERTRAIN",
        "Metallic clearcoat finish",
        {"Glacier Blue", "Solar Copper", "Alpine Mint", "Obsidian"},
        &PaintFinishPreset,
    });
    Description.OptionFields.push_back(RecordEditorOptionField{
        "EXTERIOR FINISH & POWERTRAIN",
        "Drive dynamics profile",
        {"Comfort", "Proving Sport", "Drift Telemetry"},
        &DriveModeIndex,
    });
    Description.Switches.push_back(
        RecordEditorSwitchField{"LIGHTING & SAFETY", "LED matrix headlamps", &HeadlampsEnabled});
    Description.Switches.push_back(
        RecordEditorSwitchField{"LIGHTING & SAFETY", "Rear brake light bar", &BrakeLampsEnabled});
    Description.Switches.push_back(
        RecordEditorSwitchField{"LIGHTING & SAFETY", "Amber hazard flashers", &HazardLampsEnabled});
    Description.Switches.push_back(
        RecordEditorSwitchField{"LIGHTING & SAFETY", "Closed-loop yaw stability vectoring", &StabilityControlActive});
    Description.ScalarFields.push_back(RecordEditorScalarField{
        "POWERTRAIN & CHASSIS", "Peak dual-axle power", "kW", 120.0, 900.0, 10.0, 0, &PeakPowerKilowatts});
    Description.ScalarFields.push_back(RecordEditorScalarField{
        "POWERTRAIN & CHASSIS", "Sprung chassis mass", "kg", 900.0, 2800.0, 10.0, 0, &SprungMassKilograms});
    Description.ScalarFields.push_back(RecordEditorScalarField{
        "POWERTRAIN & CHASSIS", "Clearcoat roughness", "", 0.03, 0.65, 0.01, 2, &ClearcoatRoughness});
    Description.ScalarFields.push_back(RecordEditorScalarField{
        "POWERTRAIN & CHASSIS", "AWD rear torque split", "", 0.0, 1.0, 0.01, 2, &AwdRearTorqueFraction});
    Description.ScalarFields.push_back(RecordEditorScalarField{
        "SUSPENSION & HMI", "Wheel spring rate", "kN/m", 20.0, 120.0, 1.0, 0, &SpringRateKilonewtonsM});
    Description.ScalarFields.push_back(RecordEditorScalarField{
        "SUSPENSION & HMI", "Static ride height", "mm", 75.0, 240.0, 1.0, 0, &RideHeightMillimetres});
    Description.ScalarFields.push_back(RecordEditorScalarField{
        "SUSPENSION & HMI", "Cabin HMI cluster luminance", "nits", 80.0, 1200.0, 20.0, 0, &CabinHmiLuminanceNits});
    Description.ScalarFields.push_back(RecordEditorScalarField{
        "SUSPENSION & HMI", "Wing mirror fold angle", "deg", 0.0, 68.0, 1.0, 0, &MirrorFoldDegrees});
    RegisterEditorDescription(std::move(Description));
#endif
}

//========================================================================================================================
//                                           EVEREST DOMAIN ACTOR RECORD
//========================================================================================================================

EverestDomainActorRecord::EverestDomainActorRecord(RecordClassification Classification, const char* Identifier,
                                                   const char* Eyebrow, const char* DisplayName,
                                                   const char* Subtitle, DomainStudioCategory StudioCategory,
                                                   uint32_t AccentRgba)
    : RecordEntry(Classification)
    , TargetStudio(StudioCategory)
{
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
    RecordEditorDescription Description;
    Description.Identifier         = Identifier;
    Description.Eyebrow            = Eyebrow;
    Description.DisplayName        = DisplayName;
    Description.Subtitle           = Subtitle;
    Description.InstrumentCategory = InspectorInstrumentCategory::Custom;
    Description.AccentColour       = AccentRgba;
    Description.EnabledTarget      = &Enabled;
    Description.QueryLiveBadge     = []() { return std::string("STUDIO"); };
#if defined(FRONTIER_WITH_INTERFACE_SDK)
    Description.ConstructCustomInspector = [this](SceneDirectory&, SceneDirectoryPanel&) {
        const KitFaces Faces  = ResolveKitFaces();
        const KitTheme Theme  = ResolveKitTheme();
        ImDrawList*    Ink    = ImGui::GetWindowDrawList();
        const ImVec2   Origin = ImGui::GetCursorScreenPos();
        const float    Width  = std::max(120.0f, ImGui::GetContentRegionAvail().x - 24.0f);
        const ImU32    Accent = KitInkHex(0x63a8ff);

        bool Hovered = false;
        if (KitTarget("##OpenDomainStudioBtn", Origin, ImVec2(Width, 34.0f), Hovered) && OnLaunchStudio)
        {
            OnLaunchStudio(TargetStudio);
        }
        KitPillSurface(Ink, Origin, ImVec2(Origin.x + Width, Origin.y + 34.0f),
                       Hovered ? KitInkWithAlpha(Accent, 0.28f) : KitInkWithAlpha(Accent, 0.16f),
                       Accent, 8.5f);
        KitDrawText(Ink, Faces.SemiBold, 12.0f, ImVec2(Origin.x + 14.0f, Origin.y + 9.5f), Theme.Text,
                    "Open Dedicated Tier-3 Domain Studio");
        ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 44.0f));
        ImGui::Dummy(ImVec2(Width, 0.0f));
    };
#endif
    Description.ScalarFields.push_back(
        RecordEditorScalarField{"INSTANCE COUPLING", "Scene emission / impulse gain", "x", 0.0, 4.0, 0.05, 2, &GainMultiplier});
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

    // Enclose records inside their global FolderRecords (matching Outliner GroupOrder).
    (void)Directory.ConfigureParent(Illumination.Token(), RenderFolder.Token());

    (void)Directory.ConfigureParent(Atmosphere.Token(), EnvironmentFolder.Token());
    (void)Directory.ConfigureParent(Clouds.Token(), EnvironmentFolder.Token());
    (void)Directory.ConfigureParent(Fog.Token(), EnvironmentFolder.Token());
    (void)Directory.ConfigureParent(Sun.Token(), EnvironmentFolder.Token());
    (void)Directory.ConfigureParent(Moon.Token(), EnvironmentFolder.Token());
    (void)Directory.ConfigureParent(Stars.Token(), EnvironmentFolder.Token());
    (void)Directory.ConfigureParent(LensFlare.Token(), EnvironmentFolder.Token());
    (void)Directory.ConfigureParent(Weather.Token(), EnvironmentFolder.Token());

    (void)Directory.ConfigureParent(Wind.Token(), ForcefieldFolder.Token());

    (void)Directory.ConfigureParent(Vehicle.Token(), VehicleFolder.Token());
    (void)Directory.ConfigureParent(FrontLeftTyre.Token(), Vehicle.Token());
    (void)Directory.ConfigureParent(FrontRightTyre.Token(), Vehicle.Token());
    (void)Directory.ConfigureParent(RearLeftTyre.Token(), Vehicle.Token());
    (void)Directory.ConfigureParent(RearRightTyre.Token(), Vehicle.Token());

    (void)Directory.ConfigureParent(FractureBarrier.Token(), EffectsFolder.Token());
    (void)Directory.ConfigureParent(ExhaustPlume.Token(), EffectsFolder.Token());
    (void)Directory.ConfigureParent(WheelSprayEmitter.Token(), EffectsFolder.Token());

    (void)Directory.ConfigureParent(CourseGeometry.Token(), LevelFolder.Token());
    (void)Directory.ConfigureParent(ChaseCamera.Token(), LevelFolder.Token());
#endif
}

void ProjectEverestScene::BindDomainStudio(DomainStudioPanel& StudioPanel)
{
    auto Launcher = [&StudioPanel](DomainStudioCategory Category) {
        StudioPanel.ActivateStudio(Category);
    };
    FractureBarrier.OnLaunchStudio   = Launcher;
    ExhaustPlume.OnLaunchStudio      = Launcher;
    WheelSprayEmitter.OnLaunchStudio = Launcher;
}

EverestViewportInputs ProjectEverestScene::BuildViewportInputs(double CameraYawDegrees,
                                                               double CameraPitchDegrees,
                                                               double CameraDistanceMetres) const noexcept
{
    EverestViewportInputs Inputs;
    Inputs.ExecutionMode = (Illumination.TierSelectionIndex == 0)
                               ? DualTierExecutionMode::HardwareRayQueryRtx
                               : DualTierExecutionMode::ToroidalSdfBvhGtx;
    Inputs.InspectionMode = static_cast<ViewportInspectionMode>(std::clamp(Illumination.InspectionAovIndex, 0, 5));

    Inputs.CameraYawDegrees     = CameraYawDegrees;
    Inputs.CameraPitchDegrees   = CameraPitchDegrees;
    Inputs.CameraDistanceMetres = CameraDistanceMetres;

    Inputs.SunDirection         = Sun.QuerySunDirection();
    Inputs.SunElevationDegrees  = Sun.ElevationDegrees;
    Inputs.SunTemperatureKelvin = Sun.TemperatureKelvin;
    Inputs.SolarIlluminance     = Sun.SolarIlluminance;
    Inputs.SunEnabled           = Sun.Enabled;
    Inputs.SunDiscEnabled       = Sun.DiscEnabled;
    Inputs.Atmosphere           = Atmosphere.Parameters;
    Inputs.AtmosphereEnabled    = Atmosphere.Enabled;
    Inputs.SkyBrightnessScale   = Atmosphere.SkyBrightnessScale;

    Inputs.CloudsEnabled        = Clouds.Enabled;
    Inputs.CloudCoverage        = Clouds.Coverage;
    Inputs.Fog                  = Fog.Parameters;
    Inputs.FogEnabled           = Fog.Enabled;
    Inputs.AerialStartMetres    = Fog.AerialStartMetres;
    Inputs.AerialDensity        = Fog.AerialDensity;

    Inputs.MoonDirection            = Moon.QueryMoonDirection();
    Inputs.MoonIlluminationFraction = Moon.QueryLunarPhase().Illumination;
    Inputs.MoonlightStrength        = Moon.MoonlightStrength;
    Inputs.MoonEnabled              = Moon.Enabled;
    Inputs.StarsEnabled             = Stars.Enabled;
    Inputs.StarLimitingMagnitude    = Stars.LimitingMagnitude;
    Inputs.StarRotationDegrees      = Stars.RotationDegrees;

    Inputs.LightningActive         = Weather.Enabled;
    Inputs.LightningFlashIntensity = Weather.QueryCurrentFlashIntensity();
    Inputs.PrecipitationRate       = Weather.Enabled ? Weather.PrecipitationRate : 0.0;
    Inputs.SnowMode                = (Weather.PrecipitationModeIndex == 3);
    Inputs.SurfaceWetnessOrSnow    = Weather.Enabled ? std::clamp(Weather.SurfaceDepositMm / 3.0, 0.0, 1.0) : 0.0;

    Inputs.BvhReflectionRoughnessCutoff = Illumination.BvhReflectionRoughnessCut;
    Inputs.SdfOuterSpanMetres           = Illumination.SdfClipmapSpanMetres;
    Inputs.ExposureBiasStops            = Illumination.ExposureCompensationEv;
    Inputs.DielectricGlazingEnabled     = Illumination.DielectricGlazingActive;
    Inputs.SurfaceCardsEnabled          = Illumination.SurfaceCardsActive;

    Inputs.VehicleBodyAlbedo       = Vehicle.QueryBodyPaintAlbedo();
    Inputs.VehicleBodyRoughness    = Vehicle.ClearcoatRoughness;
    Inputs.HeadlampsEnabled        = Vehicle.HeadlampsEnabled;
    Inputs.BrakeLampsEnabled       = Vehicle.BrakeLampsEnabled;
    Inputs.HazardLampsEnabled      = Vehicle.HazardLampsEnabled;
    Inputs.CollisionOverlayEnabled = Vehicle.CollisionOverlayEnabled;
    return Inputs;
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
