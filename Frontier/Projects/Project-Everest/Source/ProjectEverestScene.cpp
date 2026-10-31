/*==========================================================================================================================================
                                                        PROJECTEVERESTSCENE.CPP
==========================================================================================================================================*/
// 🧩 1:1 Proving Grounds.scene assembly: Render, Environment, Forcefield, ControlVehicle (8-tab VehicleSystemsInspector),
//    Effects, Destruction, and Level (9 ArenaZones teleports + 10 CameraRigs).

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
    Description.Identifier         = "render";
    Description.Eyebrow            = "RENDER / DUAL-TIER PATH TRACER & SDF";
    Description.DisplayName        = "Render settings";
    Description.Subtitle           = "RTX ReSTIR Path Tracer, GTX 4-cascade SDF + BVH reflections & inspection views.";
    Description.InstrumentCategory = InspectorInstrumentCategory::Custom;
    Description.AccentColour       = 0xFFD6B49Fu;
    Description.EnabledTarget      = &Enabled;
    Description.QueryLiveBadge     = [this]() {
        return TierSelectionIndex == 0 ? std::string("RTX \xC2\xB7 PT") : std::string("GTX \xC2\xB7 SDF");
    };
#if defined(FRONTIER_WITH_INTERFACE_SDK)
    Description.ConstructCustomInspector = [this](SceneDirectory&, SceneDirectoryPanel&) {
        const KitFaces Faces  = ResolveKitFaces();
        const KitTheme Theme  = ResolveKitTheme();
        ImDrawList*    Ink    = ImGui::GetWindowDrawList();
        const ImVec2   Origin = ImGui::GetCursorScreenPos();
        const float    Width  = std::max(140.0f, ImGui::GetContentRegionAvail().x - 12.0f);
        const float    CardH  = 112.0f;
        const ImU32    Accent = TierSelectionIndex == 0 ? KitInkHex(0x88b8ff) : KitInkHex(0x69dfa5);

        KitPillSurface(Ink, Origin, ImVec2(Origin.x + Width, Origin.y + CardH), Theme.Surface, Theme.Line, 8.0f);
        KitDrawTracked(Ink, Faces.Medium, 10.0f, ImVec2(Origin.x + 12.0f, Origin.y + 10.0f), Accent,
                       TierSelectionIndex == 0 ? "RTX \xC2\xB7 RESTIR PATH TRACER"
                                               : "GTX \xC2\xB7 TOROIDAL SDF + BVH FALLBACK",
                       0.9f);
        KitDrawText(Ink, Faces.SemiBold, 14.0f, ImVec2(Origin.x + 12.0f, Origin.y + 28.0f), Theme.Text,
                    TierSelectionIndex == 0
                        ? "Two-Level SAH BVH (TLAS + 8 BLAS) \xC2\xB7 ReSTIR GI \xC2\xB7 Dielectric Glazing"
                        : "4-Cascade SceneClip0..3 SDF GI \xC2\xB7 CardRadiosity \xC2\xB7 BVH Reflections");

        // Segmented RTX vs GTX selector buttons directly inside the Render architecture card
        const float SegY = Origin.y + 56.0f;
        const float SegW = (Width - 28.0f) * 0.5f;
        for (int Mode = 0; Mode < 2; ++Mode)
        {
            const float SX  = Origin.x + 12.0f + static_cast<float>(Mode) * (SegW + 4.0f);
            const bool  Sel = (TierSelectionIndex == Mode);
            bool        Hov = false;
            if (KitTarget(Mode == 0 ? "##ArchRtxBtn" : "##ArchGtxBtn", ImVec2(SX, SegY), ImVec2(SegW, 26.0f), Hov))
            {
                TierSelectionIndex = Mode;
            }
            KitPillSurface(Ink, ImVec2(SX, SegY), ImVec2(SX + SegW, SegY + 26.0f),
                           Sel ? KitInkWithAlpha(Accent, 0.22f) : (Hov ? Theme.Surface3 : Theme.Surface2),
                           Sel ? Accent : Theme.LineSoft, 6.0f);
            const char* Label = Mode == 0 ? "RTX \xC2\xB7 ReSTIR Path Tracer" : "GTX \xC2\xB7 Distance-Field GI";
            const float LW    = KitTextWidth(Faces.Medium, 11.0f, Label);
            KitDrawText(Ink, Faces.Medium, 11.0f, ImVec2(SX + std::max(6.0f, (SegW - LW) * 0.5f), SegY + 6.5f),
                        Sel ? Theme.Text : Theme.Muted, Label);
        }

        char Footer[128] = {};
        std::snprintf(Footer, sizeof(Footer),
                      "Reservoir M=%.0f \xC2\xB7 %d spatial taps \xC2\xB7 SDF span %.0f m \xC2\xB7 Refl rough <= %.2f",
                      TemporalHistoryCap, static_cast<int>(SpatialReuseNeighbors), SdfClipmapSpanMetres, BvhReflectionRoughnessCut);
        KitDrawText(Ink, Faces.Regular, 10.5f, ImVec2(Origin.x + 12.0f, Origin.y + 90.0f), Theme.Muted, Footer);

        ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + CardH + 8.0f));
        ImGui::Dummy(ImVec2(Width, 0.0f));
    };
#endif
    Description.OptionFields.push_back(RecordEditorOptionField{
        "RENDERER ARCHITECTURE",
        "GPU execution tier",
        {"RTX \xC2\xB7 ReSTIR Path Tracer", "GTX \xC2\xB7 Distance-Field GI"},
        &TierSelectionIndex,
    });
    Description.OptionFields.push_back(RecordEditorOptionField{
        "INSPECTION VIEW",
        "Viewport output buffer",
        {"Final image", "Indirect irradiance (GI)", "Glossy reflections", "BVH / Distance-field",
         "Dielectric glass", "Normals", "Direct sun only", "Albedo", "Surface cards"},
        &InspectionAovIndex,
    });
    Description.Switches.push_back(
        RecordEditorSwitchField{"TRANSPORT FEATURES", "Dielectric cabin glazing refraction", &DielectricGlazingActive});
    Description.Switches.push_back(
        RecordEditorSwitchField{"TRANSPORT FEATURES", "CardRadiosity multi-bounce surface cache", &SurfaceCardsActive});
    Description.Switches.push_back(
        RecordEditorSwitchField{"TRANSPORT FEATURES", "3D clustered IES & automotive lights", &ClusteredLightsActive});
    Description.Switches.push_back(
        RecordEditorSwitchField{"TRANSPORT FEATURES", "Top-right SDF distance slice inset", &DistanceSliceInsetActive});
    Description.ScalarFields.push_back(RecordEditorScalarField{
        "ILLUMINATION & EXPOSURE", "Indirect GI scale", "x", 0.0, 3.0, 0.05, 2, &IndirectBounceScale});
    Description.ScalarFields.push_back(RecordEditorScalarField{
        "ILLUMINATION & EXPOSURE", "Specular reflection scale", "x", 0.0, 3.0, 0.05, 2, &SpecularReflectionScale});
    Description.ScalarFields.push_back(RecordEditorScalarField{
        "ILLUMINATION & EXPOSURE", "Exposure bias", "EV", -4.0, 4.0, 0.1, 1, &ExposureCompensationEv});
    Description.ScalarFields.push_back(RecordEditorScalarField{
        "ACCELERATION BUDGET", "ReSTIR temporal history cap", "M", 1.0, 32.0, 1.0, 0, &TemporalHistoryCap});
    Description.ScalarFields.push_back(RecordEditorScalarField{
        "ACCELERATION BUDGET", "GTX SDF outer cascade span", "m", 40.0, 640.0, 8.0, 0, &SdfClipmapSpanMetres});
    Description.ScalarFields.push_back(RecordEditorScalarField{
        "ACCELERATION BUDGET", "BVH reflection roughness cutoff", "", 0.05, 1.0, 0.01, 2, &BvhReflectionRoughnessCut});
    Description.ScalarFields.push_back(RecordEditorScalarField{
        "ACCELERATION BUDGET", "SDF slice inset Z-height", "m", 0.1, 5.0, 0.05, 2, &DistanceSliceHeightMetres});
    RegisterEditorDescription(std::move(Description));
#endif
}

//========================================================================================================================
//                                        EVEREST ACCELERATION FIELD RECORD
//========================================================================================================================

EverestAccelerationFieldRecord::EverestAccelerationFieldRecord(EverestIlluminationRecord& LinkedIllumination)
    : RecordEntry(RecordClassification::Illumination)
    , Illumination(&LinkedIllumination)
{
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
    RecordEditorDescription Description;
    Description.Identifier         = "field";
    Description.Eyebrow            = "RENDER / ACCELERATION STRUCTURES";
    Description.DisplayName        = "BVH & distance field";
    Description.Subtitle           = "Two-level Binned-SAH BVH (TLAS + 8 BLAS) & 4 toroidal 64\xC2\xB3 SDF cascades.";
    Description.InstrumentCategory = InspectorInstrumentCategory::Standard;
    Description.AccentColour       = 0xFFD6B49Fu;
    Description.EnabledTarget      = &Enabled;
    Description.QueryLiveBadge     = [this]() {
        return (Illumination != nullptr && Illumination->TierSelectionIndex == 0)
            ? std::string("9 TLAS")
            : std::string("64\xC2\xB3");
    };
    Description.Switches.push_back(RecordEditorSwitchField{
        "DISTANCE FIELD DIAGNOSTICS", "Show top-right horizontal SDF slice inset", &LinkedIllumination.DistanceSliceInsetActive});
    Description.ScalarFields.push_back(RecordEditorScalarField{
        "DISTANCE FIELD DIAGNOSTICS", "Horizontal SDF slice Z-height", "m", 0.1, 5.0, 0.05, 2,
        &LinkedIllumination.DistanceSliceHeightMetres});
    Description.ScalarFields.push_back(RecordEditorScalarField{
        "DISTANCE FIELD DIAGNOSTICS", "Outer cascade world span", "m", 40.0, 640.0, 8.0, 0,
        &LinkedIllumination.SdfClipmapSpanMetres});
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
    Description.Subtitle           = "295/35 R20 sculpted tyre archive (3,968 smooth facets) + 5-spoke alloy rim.";
    Description.InstrumentCategory = InspectorInstrumentCategory::Standard;
    Description.AccentColour       = 0xFF4AD3E1u;
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

void EverestVehicleRecord::TeleportToZone(uint32_t ZoneIndex) noexcept
{
    const auto& Zones = QueryProvingGroundsZones();
    if (Zones.empty())
    {
        return;
    }
    const ArenaZoneEntry& Zone = Zones[ZoneIndex % Zones.size()];
    const float GroundZ = QueryProvingGroundsCourseHeight(Zone.Position[0], Zone.Position[1]);
    WorldPosition       = std::array<float, 3>{ Zone.Position[0], Zone.Position[1], GroundZ + ControlVehicleConstants::CoMHeight };
    YawRadians          = Zone.Yaw;
    FrontSteerRadians   = 0.0f;
}

SpectrumRgb EverestVehicleRecord::QueryBodyPaintAlbedo() const noexcept
{
    // Exact VehicleHMI MaterialSpecification.js BodyCoat default + VehicleSystemsInspector.js 4 paint swatches
    switch (std::clamp(PaintFinishPreset, 0, 4))
    {
        case 0: return { 0.5304, 0.6244, 0.0000 };   // Lime Chartreuse (VehicleHMI BodyCoat default)
        case 1: return { 0.3712, 0.6867, 0.5149 };   // #a4d8be Seafoam Mint
        case 2: return { 0.6584, 0.5089, 0.2462 };   // #d4bd88 Warm Champagne
        case 3: return { 0.3419, 0.4793, 0.7379 };   // #9eb8df Glacier Steel
        default: return { 0.7605, 0.4179, 0.3712 };  // #e2ada4 Rose Quartz
    }
}

EverestVehicleRecord::EverestVehicleRecord()
    : RecordEntry(RecordClassification::Vehicle)
{
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
    RecordEditorDescription Description;
    Description.Identifier         = "vehicle";
    Description.Eyebrow            = "VEHICLE / 8-MODULE SYSTEMS INSPECTOR";
    Description.DisplayName        = "ControlVehicle";
    Description.Subtitle           = "Chassis, collision & 8 vehicle systems (FCT1 archives).";
    Description.InstrumentCategory = InspectorInstrumentCategory::Custom;
    Description.AccentColour       = 0xFF4AD3E1u;
    Description.EnabledTarget      = &Enabled;
    Description.QueryLiveBadge     = [this]() {
        char Buffer[48] = {};
        std::snprintf(Buffer, sizeof(Buffer), "%.0f km/h", std::fabs(SpeedKilometresPerHour));
        return std::string(Buffer);
    };
#if defined(FRONTIER_WITH_INTERFACE_SDK)
    Description.ConstructCustomInspector = [this](SceneDirectory&, SceneDirectoryPanel&) {
        const KitFaces Faces  = ResolveKitFaces();
        const KitTheme Theme  = ResolveKitTheme();
        ImDrawList*    Ink    = ImGui::GetWindowDrawList();
        const ImVec2   Origin = ImGui::GetCursorScreenPos();
        const float    Width  = std::max(160.0f, ImGui::GetContentRegionAvail().x - 12.0f);
        const ImU32    Accent = KitInkHex(0xd4e24a);

        // Exact 8-tab VehicleSystemsInspector strip (.vsi-tabs)
        static constexpr std::array<const char*, 8> ModuleLabels{
            "Overview", "Chassis", "Engine", "Tyres",
            "Suspension", "Lights", "HMI & cabin", "Mirrors"};
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
            std::snprintf(Id, sizeof(Id), "##VehSysTab%u", Tab);
            if (KitTarget(Id, ImVec2(TX, TY), ImVec2(TabW, TabH), Hov))
            {
                ActiveModuleTab = Tab;
            }
            KitPillSurface(Ink, ImVec2(TX, TY), ImVec2(TX + TabW, TY + TabH),
                           Sel ? KitInkWithAlpha(Accent, 0.20f) : (Hov ? Theme.Surface3 : Theme.Surface2),
                           Sel ? Accent : Theme.LineSoft, 6.0f);
            const float TW = KitTextWidth(Faces.Medium, 10.8f, ModuleLabels[Tab]);
            KitDrawText(Ink, Faces.Medium, 10.8f, ImVec2(TX + std::max(3.0f, (TabW - TW) * 0.5f), TY + 6.5f),
                        Sel ? Theme.Text : Theme.Muted, ModuleLabels[Tab]);
        }

        // Paint Swatch Row (5 swatches matching VehicleHMI BodyCoat + 4 presets)
        const float SwatchY = Origin.y + 2.0f * (TabH + Gap) + 6.0f;
        KitDrawTracked(Ink, Faces.Medium, 9.5f, ImVec2(Origin.x + 2.0f, SwatchY + 6.0f), Theme.Muted, "BODY COAT", 0.9f);
        static constexpr std::array<uint32_t, 5> SwatchHex = { 0xc8dc00u, 0xa4d8beu, 0xd4bd88u, 0x9eb8dfu, 0xe2ada4u };
        for (int S = 0; S < 5; ++S)
        {
            const float SX  = Origin.x + 86.0f + static_cast<float>(S) * 30.0f;
            const bool  Sel = (PaintFinishPreset == S);
            bool        Hov = false;
            char        SId[32] = {};
            std::snprintf(SId, sizeof(SId), "##PaintSwatch%d", S);
            if (KitTarget(SId, ImVec2(SX, SwatchY), ImVec2(24.0f, 24.0f), Hov))
            {
                PaintFinishPreset = S;
            }
            KitPillSurface(Ink, ImVec2(SX, SwatchY), ImVec2(SX + 24.0f, SwatchY + 24.0f),
                           KitInkHex(SwatchHex[S]), Sel ? KitInkHex(0xffffff) : Theme.Line, 6.0f);
        }

        // Top-Down Chassis Plan + 4-Corner Telemetry Diagram
        const float CardTop = SwatchY + 30.0f;
        const float CardH   = 112.0f;
        KitPillSurface(Ink, ImVec2(Origin.x, CardTop), ImVec2(Origin.x + Width, CardTop + CardH),
                       Theme.Surface, Theme.Line, 8.0f);

        const ImVec2 CarCentre(Origin.x + 56.0f, CardTop + CardH * 0.52f);
        const ImU32  BodyTint = KitInkHex(SwatchHex[std::clamp(PaintFinishPreset, 0, 4)]);
        Ink->AddRectFilled(ImVec2(CarCentre.x - 18.0f, CarCentre.y - 36.0f),
                           ImVec2(CarCentre.x + 18.0f, CarCentre.y + 36.0f),
                           KitInkWithAlpha(BodyTint, 0.28f), 7.0f);
        Ink->AddRect(ImVec2(CarCentre.x - 18.0f, CarCentre.y - 36.0f),
                     ImVec2(CarCentre.x + 18.0f, CarCentre.y + 36.0f), BodyTint, 7.0f, 0, 1.5f);
        for (int WX : {-1, +1})
        {
            for (int WY : {-1, +1})
            {
                const ImVec2 WPos(CarCentre.x + WX * 21.0f, CarCentre.y + WY * 24.0f);
                Ink->AddRectFilled(ImVec2(WPos.x - 4.0f, WPos.y - 8.0f), ImVec2(WPos.x + 4.0f, WPos.y + 8.0f),
                                   KitInkHex(0x9fb4d6), 2.0f);
            }
        }
        if (CollisionOverlayEnabled)
        {
            Ink->AddCircle(CarCentre, 42.0f, KitInkHex(0xff6b8b), 28, 1.3f);
        }

        const float ReadoutX = Origin.x + 106.0f;
        KitDrawTracked(Ink, Faces.Medium, 9.5f, ImVec2(ReadoutX, CardTop + 10.0f), Accent,
                       ModuleLabels[ActiveModuleTab % 8u], 1.0f);
        char Line1[96] = {}, Line2[96] = {};
        switch (ActiveModuleTab % 8u)
        {
            case 0:
                std::snprintf(Line1, sizeof(Line1), "2,924 chassis + 4x5,536 wheel facets");
                std::snprintf(Line2, sizeof(Line2), "Pos [%.0f, %.0f, %.2f] m \xC2\xB7 Rig: %s",
                              WorldPosition[0], WorldPosition[1], WorldPosition[2], QueryEverestCameraLabel(CameraMode));
                break;
            case 1:
                std::snprintf(Line1, sizeof(Line1), "Mass %.0f kg \xC2\xB7 Wheelbase %.3f m", SprungMassKilograms, WheelbaseMetres);
                std::snprintf(Line2, sizeof(Line2), "Clearcoat R %.3f \xC2\xB7 CoM Z +0.113 m", ClearcoatRoughness);
                break;
            case 2:
                std::snprintf(Line1, sizeof(Line1), "%.0f kW \xC2\xB7 %.0f rpm \xC2\xB7 AWD %.0f%% R",
                              PeakPowerKilowatts, EngineRpm, AwdRearTorqueFraction * 100.0);
                std::snprintf(Line2, sizeof(Line2), "Redline %.0f rpm \xC2\xB7 SOC %.0f%%", RedlineRpm, BatterySocPercent);
                break;
            case 3:
                std::snprintf(Line1, sizeof(Line1), "295/35 R20 \xC2\xB7 R = 0.5086 m");
                std::snprintf(Line2, sizeof(Line2), "FL/FR 235 kPa -1.6\xC2\xB0 \xC2\xB7 mu 1.18");
                break;
            case 4:
                std::snprintf(Line1, sizeof(Line1), "Spring %.0f kN/m \xC2\xB7 Damper %.2f", SpringRateKilonewtonsM, DampingRatio);
                std::snprintf(Line2, sizeof(Line2), "Static ride height %.0f mm", RideHeightMillimetres);
                break;
            case 5:
                std::snprintf(Line1, sizeof(Line1), "Beam %d \xC2\xB7 Roof fixture %d", AutomotiveBeamMode, RoofFixtureIndex);
                std::snprintf(Line2, sizeof(Line2), "Headlamp 48.7x \xC2\xB7 Taillight 6.5x");
                break;
            case 6:
                std::snprintf(Line1, sizeof(Line1), "Slate cockpit + 74 glass canopy facets");
                std::snprintf(Line2, sizeof(Line2), "14\" infotainment + OLED cluster %.0f nits", CabinHmiLuminanceNits);
                break;
            default:
                std::snprintf(Line1, sizeof(Line1), "Wing mirror fold %.0f\xC2\xB0 \xC2\xB7 IOR 1.52", MirrorFoldDegrees);
                std::snprintf(Line2, sizeof(Line2), "Two-Level BVH rear-view reflection");
                break;
        }
        KitDrawText(Ink, Faces.SemiBold, 12.0f, ImVec2(ReadoutX, CardTop + 29.0f), Theme.Text, Line1);
        KitDrawText(Ink, Faces.Regular, 10.6f, ImVec2(ReadoutX, CardTop + 49.0f), Theme.Muted, Line2);

        bool XRayHov = false;
        const ImVec2 XMin(ReadoutX, CardTop + 74.0f);
        const ImVec2 XSize(Width - 118.0f, 24.0f);
        if (KitTarget("##VehXRayBtn", XMin, XSize, XRayHov))
        {
            CollisionOverlayEnabled = !CollisionOverlayEnabled;
        }
        KitPillSurface(Ink, XMin, ImVec2(XMin.x + XSize.x, XMin.y + XSize.y),
                       CollisionOverlayEnabled ? KitInkRgba(255, 107, 139, 48) : Theme.Surface2,
                       CollisionOverlayEnabled ? KitInkHex(0xff6b8b) : Theme.LineSoft, 6.0f);
        KitDrawText(Ink, Faces.Medium, 10.4f, ImVec2(XMin.x + 10.0f, XMin.y + 5.5f),
                    CollisionOverlayEnabled ? KitInkHex(0xff9bb0) : Theme.Muted,
                    CollisionOverlayEnabled ? "Collision & Wheel Hull X-Ray: ON" : "Collision & Wheel Hull X-Ray");

        ImGui::SetCursorScreenPos(ImVec2(Origin.x, CardTop + CardH + 8.0f));
        ImGui::Dummy(ImVec2(Width, 0.0f));
    };
#endif
    Description.OptionFields.push_back(RecordEditorOptionField{
        "EXTERIOR FINISH & LIGHTING",
        "Body coat paint preset",
        {"Lime Chartreuse", "Seafoam Mint", "Warm Champagne", "Glacier Steel", "Rose Quartz"},
        &PaintFinishPreset,
    });
    Description.OptionFields.push_back(RecordEditorOptionField{
        "EXTERIOR FINISH & LIGHTING",
        "Automotive headlamp beam",
        {"Off", "Low beam (dipped)", "High beam (main)", "Amber fog"},
        &AutomotiveBeamMode,
    });
    Description.OptionFields.push_back(RecordEditorOptionField{
        "EXTERIOR FINISH & LIGHTING",
        "Roof auxiliary fixture",
        {"None", "Slim LED bar", "Rally 4-pod"},
        &RoofFixtureIndex,
    });
    Description.OptionFields.push_back(RecordEditorOptionField{
        "POWERTRAIN & DYNAMICS",
        "Drive profile",
        {"Comfort", "Proving Sport", "Drift Telemetry"},
        &DriveModeIndex,
    });
    Description.Switches.push_back(
        RecordEditorSwitchField{"LIGHTING & SAFETY", "Projector headlamps", &HeadlampsEnabled});
    Description.Switches.push_back(
        RecordEditorSwitchField{"LIGHTING & SAFETY", "Rear LED brake bar", &BrakeLampsEnabled});
    Description.Switches.push_back(
        RecordEditorSwitchField{"LIGHTING & SAFETY", "Amber hazard indicators", &HazardLampsEnabled});
    Description.Switches.push_back(
        RecordEditorSwitchField{"LIGHTING & SAFETY", "Closed-loop yaw stability", &StabilityControlActive});
    Description.ScalarFields.push_back(RecordEditorScalarField{
        "CHASSIS & POWERTRAIN", "Peak dual-axle power", "kW", 120.0, 900.0, 10.0, 0, &PeakPowerKilowatts});
    Description.ScalarFields.push_back(RecordEditorScalarField{
        "CHASSIS & POWERTRAIN", "Sprung chassis mass", "kg", 900.0, 2800.0, 10.0, 0, &SprungMassKilograms});
    Description.ScalarFields.push_back(RecordEditorScalarField{
        "CHASSIS & POWERTRAIN", "Clearcoat roughness", "", 0.02, 0.65, 0.005, 3, &ClearcoatRoughness});
    Description.ScalarFields.push_back(RecordEditorScalarField{
        "CHASSIS & POWERTRAIN", "Steering angle", "rad", -0.60, 0.60, 0.02, 2, nullptr});
    // Replace null target safely by binding AwdRearTorqueFraction instead:
    Description.ScalarFields.back() = RecordEditorScalarField{
        "CHASSIS & POWERTRAIN", "AWD rear torque split", "", 0.0, 1.0, 0.01, 2, &AwdRearTorqueFraction};
    Description.ScalarFields.push_back(RecordEditorScalarField{
        "SUSPENSION & CABIN HMI", "Wheel spring rate", "kN/m", 20.0, 120.0, 1.0, 0, &SpringRateKilonewtonsM});
    Description.ScalarFields.push_back(RecordEditorScalarField{
        "SUSPENSION & CABIN HMI", "Static ride height", "mm", 75.0, 240.0, 1.0, 0, &RideHeightMillimetres});
    Description.ScalarFields.push_back(RecordEditorScalarField{
        "SUSPENSION & CABIN HMI", "Cabin screen luminance", "nits", 80.0, 1200.0, 20.0, 0, &CabinHmiLuminanceNits});
    RegisterEditorDescription(std::move(Description));
#endif
}

//========================================================================================================================
//                                           EVEREST DOMAIN ACTOR RECORD
//========================================================================================================================

EverestDomainActorRecord::EverestDomainActorRecord(RecordClassification Classification, const char* Identifier,
                                                   const char* Eyebrow, const char* DisplayName,
                                                   const char* Subtitle, DomainStudioCategory StudioCategory,
                                                   uint32_t AccentRgba, const char* BadgeLabel)
    : RecordEntry(Classification)
    , TargetStudio(StudioCategory)
{
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
    const std::string BadgeCopy = BadgeLabel != nullptr ? BadgeLabel : "STUDIO";
    RecordEditorDescription Description;
    Description.Identifier         = Identifier;
    Description.Eyebrow            = Eyebrow;
    Description.DisplayName        = DisplayName;
    Description.Subtitle           = Subtitle;
    Description.InstrumentCategory = InspectorInstrumentCategory::Custom;
    Description.AccentColour       = AccentRgba;
    Description.EnabledTarget      = &Enabled;
    Description.QueryLiveBadge     = [BadgeCopy]() { return BadgeCopy; };
#if defined(FRONTIER_WITH_INTERFACE_SDK)
    Description.ConstructCustomInspector = [this](SceneDirectory&, SceneDirectoryPanel&) {
        const KitFaces Faces  = ResolveKitFaces();
        const KitTheme Theme  = ResolveKitTheme();
        ImDrawList*    Ink    = ImGui::GetWindowDrawList();
        const ImVec2   Origin = ImGui::GetCursorScreenPos();
        const float    Width  = std::max(120.0f, ImGui::GetContentRegionAvail().x - 12.0f);
        const ImU32    Accent = KitInkHex(0x78aaff);

        bool Hovered = false;
        if (KitTarget("##OpenDomainStudioBtn", Origin, ImVec2(Width, 34.0f), Hovered) && OnLaunchStudio)
        {
            OnLaunchStudio(TargetStudio);
        }
        KitPillSurface(Ink, Origin, ImVec2(Origin.x + Width, Origin.y + 34.0f),
                       Hovered ? KitInkWithAlpha(Accent, 0.26f) : KitInkWithAlpha(Accent, 0.15f),
                       Accent, 7.0f);
        KitDrawText(Ink, Faces.SemiBold, 12.0f, ImVec2(Origin.x + 14.0f, Origin.y + 9.5f), Theme.Text,
                    "Open Authoring Workspace");
        ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 42.0f));
        ImGui::Dummy(ImVec2(Width, 0.0f));
    };
#endif
    Description.Switches.push_back(
        RecordEditorSwitchField{"SCENE PARTICIPATION", "Active in Proving Grounds", &Enabled});
    Description.ScalarFields.push_back(
        RecordEditorScalarField{"SCENE PARTICIPATION", "Emission / impulse gain", "x", 0.0, 4.0, 0.05, 2, &GainMultiplier});
    RegisterEditorDescription(std::move(Description));
#endif
}

//========================================================================================================================
//                                        EVEREST PROVING GROUNDS & CAMERA RIGS
//========================================================================================================================

EverestProvingGroundsRecord::EverestProvingGroundsRecord(EverestVehicleRecord& TargetVehicle)
    : RecordEntry(RecordClassification::Geometry)
    , Vehicle(&TargetVehicle)
{
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
    RecordEditorDescription Description;
    Description.Identifier         = "level";
    Description.Eyebrow            = "LEVEL / PROVING GROUNDS ARENA";
    Description.DisplayName        = "Proving grounds";
    Description.Subtitle           = "212 m x 116 m asphalt proving grounds, hangar, south tunnel, control tower & 9 zones.";
    Description.InstrumentCategory = InspectorInstrumentCategory::Custom;
    Description.AccentColour       = 0xFFEBBBA8u;
    Description.EnabledTarget      = &Enabled;
    Description.QueryLiveBadge     = []() { return std::string("9 zones"); };
#if defined(FRONTIER_WITH_INTERFACE_SDK)
    Description.ConstructCustomInspector = [this](SceneDirectory&, SceneDirectoryPanel&) {
        const KitFaces Faces  = ResolveKitFaces();
        const KitTheme Theme  = ResolveKitTheme();
        ImDrawList*    Ink    = ImGui::GetWindowDrawList();
        const ImVec2   Origin = ImGui::GetCursorScreenPos();
        const float    Width  = std::max(140.0f, ImGui::GetContentRegionAvail().x - 12.0f);
        const auto&    Zones  = QueryProvingGroundsZones();

        KitDrawTracked(Ink, Faces.Medium, 10.0f, Origin, Theme.Muted, "TELEPORT CONTROLVEHICLE TO ARENA ZONE", 0.9f);
        float CursorY = Origin.y + 20.0f;
        for (uint32_t Z = 0u; Z < Zones.size(); ++Z)
        {
            const bool Sel = (ActiveZoneIndex == Z);
            bool       Hov = false;
            char       BtnId[32] = {};
            std::snprintf(BtnId, sizeof(BtnId), "##ZoneTeleport%u", Z);
            if (KitTarget(BtnId, ImVec2(Origin.x, CursorY), ImVec2(Width, 28.0f), Hov) && Vehicle != nullptr)
            {
                ActiveZoneIndex = Z;
                Vehicle->TeleportToZone(Z);
            }
            KitPillSurface(Ink, ImVec2(Origin.x, CursorY), ImVec2(Origin.x + Width, CursorY + 28.0f),
                           Sel ? KitInkRgba(168, 187, 235, 42) : (Hov ? Theme.Surface3 : Theme.Surface2),
                           Sel ? KitInkHex(0xa8bbeb) : Theme.LineSoft, 6.0f);
            KitDrawText(Ink, Faces.Medium, 11.4f, ImVec2(Origin.x + 10.0f, CursorY + 6.5f),
                        Sel ? Theme.Text : Theme.Muted, Zones[Z].Name);
            char Coords[48] = {};
            std::snprintf(Coords, sizeof(Coords), "[%.0f, %.0f] m", Zones[Z].Position[0], Zones[Z].Position[1]);
            const float CW = KitTextWidth(Faces.Regular, 10.2f, Coords);
            KitDrawText(Ink, Faces.Regular, 10.2f, ImVec2(Origin.x + Width - CW - 10.0f, CursorY + 7.5f), Theme.Faint, Coords);
            CursorY += 32.0f;
        }
        ImGui::SetCursorScreenPos(ImVec2(Origin.x, CursorY + 4.0f));
        ImGui::Dummy(ImVec2(Width, 0.0f));
    };
#endif
    RegisterEditorDescription(std::move(Description));
#endif
}

EverestCameraRigRecord::EverestCameraRigRecord(EverestVehicleRecord& TargetVehicle)
    : RecordEntry(RecordClassification::Camera)
    , Vehicle(&TargetVehicle)
{
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
    RecordEditorDescription Description;
    Description.Identifier         = "camera_rigs";
    Description.Eyebrow            = "LEVEL / EXTERIOR & CABIN CAMERA RIGS";
    Description.DisplayName        = "Camera rigs";
    Description.Subtitle           = "10 VehicleHMI camera rigs: Chase, Orbit, Hood, Driver, Center screen, Cluster, Top...";
    Description.InstrumentCategory = InspectorInstrumentCategory::Custom;
    Description.AccentColour       = 0xFFEBBBA8u;
    Description.EnabledTarget      = &Enabled;
    Description.QueryLiveBadge     = [this]() {
        return std::string(Vehicle != nullptr ? QueryEverestCameraLabel(Vehicle->CameraMode) : "Chase");
    };
#if defined(FRONTIER_WITH_INTERFACE_SDK)
    Description.ConstructCustomInspector = [this](SceneDirectory&, SceneDirectoryPanel&) {
        const KitFaces Faces  = ResolveKitFaces();
        const KitTheme Theme  = ResolveKitTheme();
        ImDrawList*    Ink    = ImGui::GetWindowDrawList();
        const ImVec2   Origin = ImGui::GetCursorScreenPos();
        const float    Width  = std::max(140.0f, ImGui::GetContentRegionAvail().x - 12.0f);
        const float    ColW   = (Width - 6.0f) * 0.5f;

        for (uint32_t M = 0u; M < 10u; ++M)
        {
            const auto  Mode  = static_cast<EverestCameraMode>(M);
            const float BX    = Origin.x + static_cast<float>(M % 2u) * (ColW + 6.0f);
            const float BY    = Origin.y + static_cast<float>(M / 2u) * 32.0f;
            const bool  Sel   = (Vehicle != nullptr && Vehicle->CameraMode == Mode);
            bool        Hov   = false;
            char        Id[32] = {};
            std::snprintf(Id, sizeof(Id), "##CamRigBtn%u", M);
            if (KitTarget(Id, ImVec2(BX, BY), ImVec2(ColW, 28.0f), Hov) && Vehicle != nullptr)
            {
                Vehicle->CameraMode = Mode;
            }
            KitPillSurface(Ink, ImVec2(BX, BY), ImVec2(BX + ColW, BY + 28.0f),
                           Sel ? KitInkRgba(168, 187, 235, 44) : (Hov ? Theme.Surface3 : Theme.Surface2),
                           Sel ? KitInkHex(0xa8bbeb) : Theme.LineSoft, 6.0f);
            KitDrawText(Ink, Faces.Medium, 11.2f, ImVec2(BX + 10.0f, BY + 6.5f),
                        Sel ? Theme.Text : Theme.Muted, QueryEverestCameraLabel(Mode));
        }
        ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 5.0f * 32.0f + 4.0f));
        ImGui::Dummy(ImVec2(Width, 0.0f));
    };
#endif
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

    (void)Directory.ConfigureName(SurfaceCache.Token(), "Surface cache");

    // 1:1 Proving Grounds.scene hierarchy matching EditorShell.js + InspectorPages.js + EnvironmentInspector.js
    (void)Directory.ConfigureParent(Illumination.Token(), RenderFolder.Token());
    (void)Directory.ConfigureParent(AccelerationField.Token(), RenderFolder.Token());
    (void)Directory.ConfigureParent(SurfaceCache.Token(), RenderFolder.Token());

    (void)Directory.ConfigureParent(Sun.Token(), EnvironmentFolder.Token());
    (void)Directory.ConfigureParent(LensFlare.Token(), Sun.Token());
    (void)Directory.ConfigureParent(Clouds.Token(), EnvironmentFolder.Token());
    (void)Directory.ConfigureParent(Atmosphere.Token(), EnvironmentFolder.Token());
    (void)Directory.ConfigureParent(Stars.Token(), EnvironmentFolder.Token());
    (void)Directory.ConfigureParent(Moon.Token(), EnvironmentFolder.Token());
    (void)Directory.ConfigureParent(Fog.Token(), EnvironmentFolder.Token());
    (void)Directory.ConfigureParent(Weather.Token(), EnvironmentFolder.Token());

    (void)Directory.ConfigureParent(Wind.Token(), ForcefieldFolder.Token());

    (void)Directory.ConfigureParent(Vehicle.Token(), VehicleFolder.Token());
    (void)Directory.ConfigureParent(FrontLeftTyre.Token(), Vehicle.Token());
    (void)Directory.ConfigureParent(FrontRightTyre.Token(), Vehicle.Token());
    (void)Directory.ConfigureParent(RearLeftTyre.Token(), Vehicle.Token());
    (void)Directory.ConfigureParent(RearRightTyre.Token(), Vehicle.Token());

    (void)Directory.ConfigureParent(WheelSprayEmitter.Token(), EffectsFolder.Token());
    (void)Directory.ConfigureParent(ExhaustPlume.Token(), EffectsFolder.Token());

    (void)Directory.ConfigureParent(FractureBarrier.Token(), DestructionFolder.Token());

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

EverestViewportInputs ProjectEverestScene::BuildViewportInputs(double OrbitYawDegrees,
                                                               double OrbitPitchDegrees,
                                                               double OrbitDistanceMetres) const noexcept
{
    EverestViewportInputs Inputs;
    Inputs.ExecutionMode = (Illumination.TierSelectionIndex == 0)
                               ? DualTierExecutionMode::HardwareRayQueryRtx
                               : DualTierExecutionMode::ToroidalSdfBvhGtx;
    Inputs.InspectionMode = static_cast<ViewportInspectionMode>(std::clamp(Illumination.InspectionAovIndex, 0, 8));

    Inputs.CameraMode           = Vehicle.CameraMode;
    Inputs.CameraYawDegrees     = OrbitYawDegrees;
    Inputs.CameraPitchDegrees   = OrbitPitchDegrees;
    Inputs.CameraDistanceMetres = OrbitDistanceMetres;

    Inputs.VehiclePosition      = Vehicle.WorldPosition;
    Inputs.VehicleYawRadians    = Vehicle.YawRadians;
    Inputs.FrontSteerRadians    = Vehicle.FrontSteerRadians;
    Inputs.WheelSpinRadians     = Vehicle.WheelSpinRadians;

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
    Inputs.AerialStartMetres    = Fog.Enabled ? Fog.AerialStartMetres : 95.0;
    Inputs.AerialDensity        = Fog.Enabled ? Fog.AerialDensity : 0.00075;

    Inputs.MoonDirection              = Moon.QueryMoonDirection();
    Inputs.MoonAngularDiameterDegrees = Moon.AngularDiameterDegrees;
    Inputs.MoonIlluminationFraction   = Moon.QueryLunarPhase().Illumination;
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
    Inputs.IndirectScale                = Illumination.IndirectBounceScale;
    Inputs.ReflectionScale              = Illumination.SpecularReflectionScale;
    Inputs.DielectricGlazingEnabled     = Illumination.DielectricGlazingActive;
    Inputs.SurfaceCardsEnabled          = Illumination.SurfaceCardsActive;
    Inputs.ClusteredLightsEnabled       = Illumination.ClusteredLightsActive;
    Inputs.LensFlareEnabled             = LensFlare.Enabled;
    Inputs.LensFlareIntensity           = LensFlare.Intensity;
    Inputs.DistanceSliceInsetEnabled    = Illumination.DistanceSliceInsetActive;
    Inputs.DistanceSliceHeightMetres    = static_cast<float>(Illumination.DistanceSliceHeightMetres);
    Inputs.BreakablesEnabled            = FractureBarrier.Enabled;

    Inputs.VehicleBodyAlbedo       = Vehicle.QueryBodyPaintAlbedo();
    Inputs.VehicleBodyRoughness    = Vehicle.ClearcoatRoughness;
    Inputs.VehicleSpeedKmh         = Vehicle.SpeedKilometresPerHour;
    Inputs.HeadlampsEnabled        = Vehicle.HeadlampsEnabled && (Vehicle.AutomotiveBeamMode != 0);
    Inputs.AutomotiveBeamMode      = static_cast<uint32_t>(std::clamp(Vehicle.AutomotiveBeamMode, 0, 3));
    Inputs.BrakeLampsEnabled       = Vehicle.BrakeLampsEnabled;
    Inputs.HazardLampsEnabled      = Vehicle.HazardLampsEnabled;
    Inputs.RoofLightEnabled        = (Vehicle.RoofFixtureIndex != 0);
    Inputs.RoofRallyPodMode        = (Vehicle.RoofFixtureIndex == 2);
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
        auto Created = std::make_unique<FolderRecord>(FolderName, "CUSTOM GROUP", 0xFFD6B49Fu);
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
