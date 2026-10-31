/*==========================================================================================================================================
                                                        PROJECTEVERESTSCENE.CPP
==========================================================================================================================================*/
// 🧩 1:1 Proving Grounds.scene assembly: Render, Environment, Forcefield, ControlVehicle (all 8 VehicleSystemsInspector
//    tabs + live ControlVehicleSolver + 32x5 Neo-Hookean SolidContinuumWheel), Effects, Destruction, and Level.

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
#include <cstring>
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

void EverestVehicleRecord::ConfigureChassisHullSamples(std::vector<std::array<float, 2>> Samples)
{
    ChassisHullSamples = std::move(Samples);
}

void EverestVehicleRecord::SynchronizeFromSolver() noexcept
{
    WorldPosition = {
        static_cast<float>(DynamicsSolver.Position[0]),
        static_cast<float>(DynamicsSolver.Position[1]),
        static_cast<float>(DynamicsSolver.Position[2])
    };
    YawRadians             = static_cast<float>(DynamicsSolver.YawRadians);
    PitchRadians           = static_cast<float>(DynamicsSolver.PitchRadians);
    RollRadians            = static_cast<float>(DynamicsSolver.RollRadians);
    FrontSteerRadians      = static_cast<float>(DynamicsSolver.SteeringAngleRad);
    WheelSpinRadians       = static_cast<float>(DynamicsSolver.QueryWheels()[0].SpinAngleRad);
    SpeedKilometresPerHour = DynamicsSolver.SpeedKilometresHour;
    GearNumber             = DynamicsSolver.QueryGearNumber();
    EngineRpm              = DynamicsSolver.EngineRpm;
    DynamicsSolver.EncodeVisualRecord(VisualStateBuffer);
}

void EverestVehicleRecord::StepDynamics(double DeltaSeconds) noexcept
{
    double Remaining = std::clamp(DeltaSeconds, 0.0, 10.0);
    while (Remaining > 1.0e-5)
    {
        const double StepDt  = std::min(Remaining, 1.0 / 120.0);
        const float  GroundZ = QueryProvingGroundsCourseHeight(
            static_cast<float>(DynamicsSolver.Position[0]),
            static_cast<float>(DynamicsSolver.Position[1]));
        DynamicsSolver.Advance(DriverControls, StepDt, static_cast<double>(GroundZ));
        Remaining -= StepDt;
    }
    SynchronizeFromSolver();
}

void EverestVehicleRecord::TeleportToZone(uint32_t ZoneIndex) noexcept
{
    const auto& Zones = QueryProvingGroundsZones();
    if (Zones.empty())
    {
        return;
    }
    const ArenaZoneEntry& Zone    = Zones[ZoneIndex % Zones.size()];
    const float           GroundZ = QueryProvingGroundsCourseHeight(Zone.Position[0], Zone.Position[1]);
    DriverControls                = VehicleDriverInputs{};
    DynamicsSolver.Respawn({ static_cast<double>(Zone.Position[0]),
                             static_cast<double>(Zone.Position[1]),
                             static_cast<double>(GroundZ) + ControlVehicleSpecification::CentreOfMassHeightM },
                           static_cast<double>(Zone.Yaw));
    SynchronizeFromSolver();
}

SpectrumRgb EverestVehicleRecord::QueryBodyPaintAlbedo() const noexcept
{
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
    DynamicsSolver.Respawn({ -40.0, 0.0, ControlVehicleSpecification::CentreOfMassHeightM }, 0.0);
    SynchronizeFromSolver();

#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
    RecordEditorDescription Description;
    Description.Identifier         = "vehicle";
    Description.Eyebrow            = "ASSEMBLY / CONTROLVEHICLE / SYSTEMS";
    Description.DisplayName        = "ControlVehicle";
    Description.Subtitle           = "8-module engineering assembly · Pacejka MF6.1 · 32x5 Hex8 FEM · 10,448 B GPU state.";
    Description.InstrumentCategory = InspectorInstrumentCategory::Custom;
    Description.AccentColour       = 0xFF4AD3E1u;
    Description.EnabledTarget      = &Enabled;
    Description.QueryLiveBadge     = [this]() {
        char Buffer[48] = {};
        std::snprintf(Buffer, sizeof(Buffer), "%.0f km/h \xC2\xB7 %s",
                      std::fabs(SpeedKilometresPerHour), DynamicsSolver.QueryGearLabel());
        return std::string(Buffer);
    };
#if defined(FRONTIER_WITH_INTERFACE_SDK)
    Description.ConstructCustomInspector = [this](SceneDirectory&, SceneDirectoryPanel&) {
        const KitFaces Faces  = ResolveKitFaces();
        const KitTheme Theme  = ResolveKitTheme();
        ImDrawList*    Ink    = ImGui::GetWindowDrawList();
        const ImVec2   Origin = ImGui::GetCursorScreenPos();
        const float    Width  = std::max(180.0f, ImGui::GetContentRegionAvail().x - 12.0f);
        const ImU32    Accent = KitInkHex(0xd4e24a);
        const ImU32    Cyan   = KitInkHex(0x6bd4e8);
        const ImU32    Mint   = KitInkHex(0x69dfa5);
        const ImU32    Amber  = KitInkHex(0xf2b25c);
        const ImU32    Rose   = KitInkHex(0xff6b8b);

        static constexpr std::array<const char*, 8> ModuleLabels{
            "Overview", "Chassis", "Engine", "Tyres",
            "Suspension", "Lights", "HMI", "Mirrors"
        };
        static constexpr std::array<const char*, 8> ModuleUpper{
            "OVERVIEW", "CHASSIS", "ENGINE", "TYRES",
            "SUSPENSION", "LIGHTS", "CABIN HMI", "MIRRORS"
        };
        static constexpr std::array<uint32_t, 5> SwatchHex = {
            0xc8dc00u, 0xa4d8beu, 0xd4bd88u, 0x9eb8dfu, 0xe2ada4u
        };

        float Y = Origin.y;

        // 1. ASSEMBLY / CONTROLVEHICLE / <MODULE> Header + 4-KPI Strip (VehicleSystemsInspector.js)
        KitPillSurface(Ink, ImVec2(Origin.x, Y), ImVec2(Origin.x + Width, Y + 64.0f), Theme.Surface, Theme.Line, 8.0f);
        char Crumb[96] = {};
        std::snprintf(Crumb, sizeof(Crumb), "ASSEMBLY / CONTROLVEHICLE / %s", ModuleUpper[ActiveModuleTab % 8u]);
        KitDrawTracked(Ink, Faces.Medium, 9.2f, ImVec2(Origin.x + 10.0f, Y + 8.0f), Accent, Crumb, 0.85f);

        const char* LiveTag = "8 modules \xC2\xB7 live";
        const float LiveW   = KitTextWidth(Faces.Medium, 9.5f, LiveTag) + 12.0f;
        KitPillSurface(Ink, ImVec2(Origin.x + Width - LiveW - 8.0f, Y + 6.0f),
                       ImVec2(Origin.x + Width - 8.0f, Y + 23.0f),
                       KitInkWithAlpha(Mint, 0.16f), Mint, 5.0f);
        KitDrawText(Ink, Faces.Medium, 9.5f, ImVec2(Origin.x + Width - LiveW - 2.0f, Y + 8.0f), Mint, LiveTag);

        const std::array<std::pair<const char*, const char*>, 4> Kpis = {{
            { "MASS",    "1300 kg" },
            { "COM Z",   "0.53 m"  },
            { "TYRE R",  "0.509 m" },
            { "CHASSIS", "1186 tri"}
        }};
        const float KpiW = (Width - 20.0f) * 0.25f;
        for (size_t K = 0u; K < 4u; ++K)
        {
            const float KX = Origin.x + 10.0f + static_cast<float>(K) * KpiW;
            KitDrawTracked(Ink, Faces.Medium, 8.5f, ImVec2(KX, Y + 29.0f), Theme.Faint, Kpis[K].first, 0.7f);
            KitDrawText(Ink, Faces.SemiBold, 11.4f, ImVec2(KX, Y + 42.0f), Theme.Text, Kpis[K].second);
        }
        Y += 70.0f;

        // 2. 8-Tab Module Selector Strip (.vsi-tabs)
        const float Gap  = 4.0f;
        const float TabW = (Width - Gap * 3.0f) * 0.25f;
        const float TabH = 25.0f;
        for (uint32_t Tab = 0u; Tab < 8u; ++Tab)
        {
            const float TX  = Origin.x + static_cast<float>(Tab % 4u) * (TabW + Gap);
            const float TY  = Y + static_cast<float>(Tab / 4u) * (TabH + Gap);
            const bool  Sel = (ActiveModuleTab == Tab);
            bool        Hov = false;
            char        Id[40] = {};
            std::snprintf(Id, sizeof(Id), "##VehSysTab%u", Tab);
            if (KitTarget(Id, ImVec2(TX, TY), ImVec2(TabW, TabH), Hov))
            {
                ActiveModuleTab = Tab;
            }
            KitPillSurface(Ink, ImVec2(TX, TY), ImVec2(TX + TabW, TY + TabH),
                           Sel ? KitInkWithAlpha(Accent, 0.20f) : (Hov ? Theme.Surface3 : Theme.Surface2),
                           Sel ? Accent : Theme.LineSoft, 6.0f);
            const float TW = KitTextWidth(Faces.Medium, 10.4f, ModuleLabels[Tab]);
            KitDrawText(Ink, Faces.Medium, 10.4f, ImVec2(TX + std::max(3.0f, (TabW - TW) * 0.5f), TY + 6.0f),
                        Sel ? Theme.Text : Theme.Muted, ModuleLabels[Tab]);
        }
        Y += 2.0f * (TabH + Gap) + 6.0f;

        // Helper row renderer for clean key/value engineering tables
        const auto DrawKeyValRow = [&](const char* Key, const char* Val, ImU32 ValCol) {
            Ink->AddLine(ImVec2(Origin.x + 8.0f, Y + 19.0f), ImVec2(Origin.x + Width - 8.0f, Y + 19.0f),
                         KitInkWithAlpha(Theme.LineSoft, 0.45f));
            KitDrawText(Ink, Faces.Regular, 10.4f, ImVec2(Origin.x + 10.0f, Y + 3.0f), Theme.Muted, Key);
            const float VW = KitTextWidth(Faces.Medium, 10.4f, Val);
            KitDrawText(Ink, Faces.Medium, 10.4f, ImVec2(Origin.x + Width - VW - 10.0f, Y + 3.0f), ValCol, Val);
            Y += 20.0f;
        };

        // 3. Per-Tab 1:1 VehicleSystemsInspector Body
        switch (ActiveModuleTab % 8u)
        {
            //----------------------------------------------------------------------------------------------------
            // TAB 0: OVERVIEW — Clickable Vehicle Assembly Tree + Paint Swatches + Live Telemetry
            //----------------------------------------------------------------------------------------------------
            case 0u:
            {
                KitDrawTracked(Ink, Faces.Medium, 9.2f, ImVec2(Origin.x + 2.0f, Y + 5.0f), Theme.Muted, "BODY COAT", 0.85f);
                for (int S = 0; S < 5; ++S)
                {
                    const float SX  = Origin.x + 82.0f + static_cast<float>(S) * 28.0f;
                    const bool  Sel = (PaintFinishPreset == S);
                    bool        Hov = false;
                    char        SId[32] = {};
                    std::snprintf(SId, sizeof(SId), "##PaintSwatch%d", S);
                    if (KitTarget(SId, ImVec2(SX, Y), ImVec2(22.0f, 22.0f), Hov))
                    {
                        PaintFinishPreset = S;
                    }
                    KitPillSurface(Ink, ImVec2(SX, Y), ImVec2(SX + 22.0f, Y + 22.0f),
                                   KitInkHex(SwatchHex[S]), Sel ? KitInkHex(0xffffff) : Theme.Line, 5.0f);
                }
                Y += 28.0f;

                KitDrawTracked(Ink, Faces.Medium, 9.4f, ImVec2(Origin.x + 2.0f, Y), Theme.Muted,
                               "VEHICLE ASSEMBLY TREE (CLICK TO INSPECT)", 0.85f);
                Y += 16.0f;

                struct TreeEntry
                {
                    const char* Label;
                    const char* Receipt;
                    uint32_t    TargetTab;
                    int         Indent;
                };
                static constexpr std::array<TreeEntry, 7> TreeRows = {{
                    { "Chassis & Body Hull",      "1,186 + 74 glass", 1u, 0 },
                    { "Engine + 6-Speed AMT",     "520 N\xC2\xB7m \xC2\xB7 1.2 bar",  2u, 0 },
                    { "Tyre Assembly (x4)",       "32x5 Hex8 FEM",    3u, 0 },
                    { "Suspension Struts (x4)",   "2.0 Hz digressive",4u, 0 },
                    { "Automotive Lights",        "IES + LED bar",    5u, 0 },
                    { "Cabin OLED & 14\" HMI",    "DisplayGlass #77", 6u, 0 },
                    { "Rear-View Mirrors (x3)",   "Planar + SAH BVH", 7u, 0 }
                }};
                for (size_t R = 0u; R < TreeRows.size(); ++R)
                {
                    bool Hov = false;
                    char RId[32] = {};
                    std::snprintf(RId, sizeof(RId), "##VsiTreeRow%zu", R);
                    if (KitTarget(RId, ImVec2(Origin.x, Y), ImVec2(Width, 25.0f), Hov))
                    {
                        ActiveModuleTab = TreeRows[R].TargetTab;
                    }
                    KitPillSurface(Ink, ImVec2(Origin.x, Y), ImVec2(Origin.x + Width, Y + 24.0f),
                                   Hov ? Theme.Surface3 : Theme.Surface2,
                                   Hov ? Cyan : Theme.LineSoft, 5.5f);
                    Ink->AddCircleFilled(ImVec2(Origin.x + 11.0f, Y + 12.0f), 3.0f, Cyan);
                    KitDrawText(Ink, Faces.Medium, 10.6f, ImVec2(Origin.x + 20.0f, Y + 5.0f), Theme.Text, TreeRows[R].Label);
                    const float RW = KitTextWidth(Faces.Regular, 9.8f, TreeRows[R].Receipt);
                    KitDrawText(Ink, Faces.Regular, 9.8f, ImVec2(Origin.x + Width - RW - 8.0f, Y + 5.5f), Theme.Muted,
                                TreeRows[R].Receipt);
                    Y += 27.0f;
                }

                Y += 4.0f;
                KitPillSurface(Ink, ImVec2(Origin.x, Y), ImVec2(Origin.x + Width, Y + 86.0f), Theme.Surface, Theme.Line, 7.0f);
                Y += 4.0f;
                char Buf[96] = {};
                std::snprintf(Buf, sizeof(Buf), "%.1f km/h \xC2\xB7 Gear %s \xC2\xB7 %.0f rpm",
                              SpeedKilometresPerHour, DynamicsSolver.QueryGearLabel(), EngineRpm);
                DrawKeyValRow("Live powertrain", Buf, Mint);
                std::snprintf(Buf, sizeof(Buf), "[%.1f, %.1f, %.2f] m", WorldPosition[0], WorldPosition[1], WorldPosition[2]);
                DrawKeyValRow("World CoM pose", Buf, Theme.Text);
                std::snprintf(Buf, sizeof(Buf), "%zu B (52 + 2560 floats)", sizeof(VehicleVisualRecord));
                DrawKeyValRow("GPU visual buffer", Buf, Cyan);
                DrawKeyValRow("Active camera rig", QueryEverestCameraLabel(CameraMode), Amber);
                Y += 6.0f;
                break;
            }

            //----------------------------------------------------------------------------------------------------
            // TAB 1: CHASSIS — BODY-LOCAL SIDE VIEW / ACTUAL HULL SAMPLES + Sockets + Geometry Ownership
            //----------------------------------------------------------------------------------------------------
            case 1u:
            {
                const float DiagH = 148.0f;
                KitPillSurface(Ink, ImVec2(Origin.x, Y), ImVec2(Origin.x + Width, Y + DiagH),
                               KitInkHex(0x0f1216), Theme.Line, 8.0f);
                KitDrawTracked(Ink, Faces.Medium, 8.8f, ImVec2(Origin.x + 10.0f, Y + 7.0f), Cyan,
                               "BODY-LOCAL SIDE VIEW / ACTUAL HULL SAMPLES", 0.85f);

                const float PadX   = 14.0f;
                const float PlotX0 = Origin.x + PadX;
                const float PlotX1 = Origin.x + Width - PadX;
                const float PlotW  = std::max(80.0f, PlotX1 - PlotX0);
                const float ScaleM = PlotW / 6.30f; // [-3.15..+3.15 m] longitudinal span
                const float MidX   = 0.5f * (PlotX0 + PlotX1);
                const float GndY   = Y + DiagH - 22.0f;
                const float ComZ   = static_cast<float>(ControlVehicleSpecification::CentreOfMassHeightM);
                const auto  MapXZ  = [&](float LocalX, float WorldAboveGroundZ) -> ImVec2 {
                    return ImVec2(MidX + LocalX * ScaleM, GndY - WorldAboveGroundZ * ScaleM);
                };

                // Ground contact datum + longitudinal grid
                Ink->AddLine(ImVec2(PlotX0, GndY), ImVec2(PlotX1, GndY), KitInkHex(0x2d3642), 1.2f);
                for (int Meter = -3; Meter <= 3; ++Meter)
                {
                    const float GX = MidX + static_cast<float>(Meter) * ScaleM;
                    Ink->AddLine(ImVec2(GX, Y + 24.0f), ImVec2(GX, GndY), KitInkRgba(90, 108, 130, 32));
                }

                // Plot actual ControlVehicleBody.facet hull samples (or fallback wedge envelope)
                const ImU32 BodyCol = KitInkHex(SwatchHex[std::clamp(PaintFinishPreset, 0, 4)]);
                const float AuthShiftZ = ComZ - ControlVehicleConstants::CoMAuthoredZ();
                if (ChassisHullSamples.size() >= 4u)
                {
                    for (const auto& Sample : ChassisHullSamples)
                    {
                        const ImVec2 Pt = MapXZ(Sample[0], Sample[1] + AuthShiftZ);
                        Ink->AddCircleFilled(Pt, 1.55f, KitInkWithAlpha(BodyCol, 0.88f));
                    }
                }
                else
                {
                    static constexpr std::array<std::array<float, 2>, 9> Wedge = {{
                        { -2.92f, 0.24f }, { -2.85f, 0.92f }, { -1.25f, 1.28f }, { 0.42f, 1.26f },
                        {  1.58f, 0.86f }, {  2.88f, 0.52f }, {  2.92f, 0.19f }, { -2.92f, 0.19f }, { -2.92f, 0.24f }
                    }};
                    for (size_t I = 0u; I + 1u < Wedge.size(); ++I)
                    {
                        Ink->AddLine(MapXZ(Wedge[I][0], Wedge[I][1]), MapXZ(Wedge[I + 1u][0], Wedge[I + 1u][1]),
                                     BodyCol, 1.7f);
                    }
                }

                // Front & Rear AxleMount + SuspensionMount struts + Tyre/Rim circles
                const float TyreR = static_cast<float>(ControlVehicleSpecification::TyreOuterRadiusM);
                const float RimR  = static_cast<float>(ControlVehicleSpecification::TyreRimRadiusM);
                for (float AxleX : { ControlVehicleConstants::AxleFrontX, ControlVehicleConstants::AxleRearX })
                {
                    const ImVec2 HubPt   = MapXZ(AxleX, TyreR);
                    const ImVec2 MountPt = MapXZ(AxleX, TyreR + static_cast<float>(
                                                 ControlVehicleSpecification::SuspensionMountModel[0][2] -
                                                 ControlVehicleSpecification::AxleMountModel[0][2]));
                    Ink->AddLine(MountPt, HubPt, Amber, 2.0f);
                    Ink->AddCircleFilled(MountPt, 3.2f, Amber);
                    Ink->AddCircle(HubPt, TyreR * ScaleM, Cyan, 28, 1.5f);
                    Ink->AddCircle(HubPt, RimR  * ScaleM, KitInkHex(0x9fb4d6), 24, 1.1f);
                    Ink->AddCircleFilled(HubPt, 2.8f, KitInkHex(0xffffff));
                }

                // CoM marker (0.00, 0.53 m)
                const ImVec2 ComPt = MapXZ(0.0f, ComZ);
                Ink->AddCircleFilled(ComPt, 4.2f, Rose);
                Ink->AddLine(ImVec2(ComPt.x - 7.0f, ComPt.y), ImVec2(ComPt.x + 7.0f, ComPt.y), Rose, 1.3f);
                Ink->AddLine(ImVec2(ComPt.x, ComPt.y - 7.0f), ImVec2(ComPt.x, ComPt.y + 7.0f), Rose, 1.3f);
                KitDrawText(Ink, Faces.Regular, 9.2f, ImVec2(PlotX0, GndY + 4.0f), Theme.Muted,
                            "Rear -1.669 m                 CoM (0.53 m)                 Front +1.727 m");
                Y += DiagH + 6.0f;

                KitPillSurface(Ink, ImVec2(Origin.x, Y), ImVec2(Origin.x + Width, Y + 146.0f), Theme.Surface, Theme.Line, 7.0f);
                Y += 4.0f;
                DrawKeyValRow("Total / sprung mass", "1,300 kg / 1,220 kg", Theme.Text);
                DrawKeyValRow("Front weight fraction", "49.14 % (wheelbase 3.396 m)", Cyan);
                DrawKeyValRow("Track width (F / R)", "2.095 m (\xC2\xB1" "1.0475 m)", Theme.Text);
                DrawKeyValRow("Body / glass facets", "1,186 opaque + 74 VehicleGlass", Accent);
                DrawKeyValRow("Tyre / rim facets", "4 x 3,968 smooth + 4 x 1,568 rigid", Mint);
                DrawKeyValRow("Clearcoat roughness", "0.0775 (IOR 1.50 automotive coat)", Theme.Text);
                DrawKeyValRow("Drag CdA / Downforce", "0.62 m\xC2\xB2 / ClA 0.35", Theme.Muted);
                Y += 6.0f;
                break;
            }

            //----------------------------------------------------------------------------------------------------
            // TAB 2: ENGINE — Live Powertrain Cards + 18-Point GT-R Torque Curve (800 -> 8,500 rpm) + Bench
            //----------------------------------------------------------------------------------------------------
            case 2u:
            {
                const float ChartH = 142.0f;
                KitPillSurface(Ink, ImVec2(Origin.x, Y), ImVec2(Origin.x + Width, Y + ChartH),
                               KitInkHex(0x0f1216), Theme.Line, 8.0f);
                KitDrawTracked(Ink, Faces.Medium, 8.8f, ImVec2(Origin.x + 10.0f, Y + 7.0f), Amber,
                               "TORQUE CURVE - 800 -> 8,500 RPM (520 NM + 1.2 BAR TURBO)", 0.75f);

                const float PX0 = Origin.x + 14.0f;
                const float PX1 = Origin.x + Width - 14.0f;
                const float PY0 = Y + 26.0f;
                const float PY1 = Y + ChartH - 20.0f;
                const auto  MapTorque = [&](float Rpm, float Nm) -> ImVec2 {
                    const float U = std::clamp((Rpm - 700.0f) / (8500.0f - 700.0f), 0.0f, 1.0f);
                    const float V = std::clamp(Nm / 1000.0f, 0.0f, 1.0f);
                    return ImVec2(PX0 + U * (PX1 - PX0), PY1 - V * (PY1 - PY0));
                };

                Ink->AddLine(ImVec2(PX0, PY1), ImVec2(PX1, PY1), KitInkHex(0x2d3642), 1.1f);
                // Redline vertical dashed line at 7,200 rpm
                const ImVec2 RedTop = MapTorque(static_cast<float>(ControlVehicleSpecification::RedlineRpm), 1000.0f);
                Ink->AddLine(ImVec2(RedTop.x, PY0), ImVec2(RedTop.x, PY1), KitInkWithAlpha(Rose, 0.65f), 1.2f);

                // Naturally aspirated 18-point GT-R curve + 1.2 bar boosted curve
                const float MaxTurboMult = static_cast<float>(
                    ControlVehicleSpecification::SampleTurboMultiplier(ControlVehicleSpecification::WastegateMaxBoostBar));
                ImVec2 PrevNa{}, PrevBoost{};
                for (int S = 0; S <= 38; ++S)
                {
                    const float Rpm   = 700.0f + (8500.0f - 700.0f) * (static_cast<float>(S) / 38.0f);
                    const float NaNm  = static_cast<float>(ControlVehicleSpecification::SampleEngineTorqueNm(Rpm));
                    const float BstNm = NaNm * (1.0f + (MaxTurboMult - 1.0f) *
                                                std::clamp((Rpm - 1800.0f) / 1600.0f, 0.0f, 1.0f));
                    const ImVec2 PtNa    = MapTorque(Rpm, NaNm);
                    const ImVec2 PtBoost = MapTorque(Rpm, BstNm);
                    if (S > 0)
                    {
                        Ink->AddLine(PrevNa, PtNa, Cyan, 1.6f);
                        Ink->AddLine(PrevBoost, PtBoost, Amber, 1.8f);
                    }
                    PrevNa    = PtNa;
                    PrevBoost = PtBoost;
                }

                // Live RPM operating point cursor
                const float LiveRpm = static_cast<float>(DynamicsSolver.EngineRpm);
                const float LiveNm  = static_cast<float>(std::max(0.0, DynamicsSolver.QueryFlywheelTorqueNm()));
                const float BaseAtRpm = static_cast<float>(ControlVehicleSpecification::SampleEngineTorqueNm(LiveRpm));
                const ImVec2 CurPt  = MapTorque(LiveRpm, std::max(BaseAtRpm * 0.35f, LiveNm));
                Ink->AddLine(ImVec2(CurPt.x, PY0), ImVec2(CurPt.x, PY1), KitInkWithAlpha(Mint, 0.55f), 1.0f);
                Ink->AddCircleFilled(CurPt, 4.5f, Mint);
                KitDrawText(Ink, Faces.Regular, 9.2f, ImVec2(PX0, PY1 + 3.0f), Theme.Muted,
                            "800 rpm        Peak 520 N\xC2\xB7m (962 boosted)        Redline 7,200");
                Y += ChartH + 6.0f;

                // Live Engine & Dyno Test Bench Buttons
                const float BtnW = (Width - 8.0f) / 3.0f;
                const auto DrawBenchBtn = [&](int Col, const char* Id, const char* Label, bool Active, auto&& OnClick) {
                    const float BX  = Origin.x + static_cast<float>(Col) * (BtnW + 4.0f);
                    bool        Hov = false;
                    if (KitTarget(Id, ImVec2(BX, Y), ImVec2(BtnW, 25.0f), Hov))
                    {
                        OnClick();
                    }
                    KitPillSurface(Ink, ImVec2(BX, Y), ImVec2(BX + BtnW, Y + 25.0f),
                                   Active ? KitInkWithAlpha(Mint, 0.22f) : (Hov ? Theme.Surface3 : Theme.Surface2),
                                   Active ? Mint : Theme.LineSoft, 5.5f);
                    const float LW = KitTextWidth(Faces.Medium, 10.0f, Label);
                    KitDrawText(Ink, Faces.Medium, 10.0f, ImVec2(BX + (BtnW - LW) * 0.5f, Y + 5.5f),
                                Active ? Theme.Text : Theme.Muted, Label);
                };
                DrawBenchBtn(0, "##EngWotBtn", DriverControls.Throttle > 0.5 ? "WOT: ON" : "Toggle WOT",
                             DriverControls.Throttle > 0.5, [&]() {
                                 DriverControls.Throttle = DriverControls.Throttle > 0.5 ? 0.0 : 1.0;
                                 DriverControls.Brake    = 0.0;
                             });
                DrawBenchBtn(1, "##EngStepBtn", "Step +0.5 s", false, [&]() {
                    if (DriverControls.Throttle < 0.1)
                    {
                        DriverControls.Throttle = 0.85;
                    }
                    StepDynamics(0.50);
                });
                DrawBenchBtn(2, "##EngResetBtn", "Reset Spawn", false, [&]() {
                    TeleportToZone(0u);
                });
                Y += 31.0f;

                KitPillSurface(Ink, ImVec2(Origin.x, Y), ImVec2(Origin.x + Width, Y + 126.0f), Theme.Surface, Theme.Line, 7.0f);
                Y += 4.0f;
                char Buf[96] = {};
                std::snprintf(Buf, sizeof(Buf), "%.0f rpm  (Gear %s)", DynamicsSolver.EngineRpm, DynamicsSolver.QueryGearLabel());
                DrawKeyValRow("Crankshaft speed", Buf, Mint);
                std::snprintf(Buf, sizeof(Buf), "%.1f N\xC2\xB7m  (Boost %.2f bar)",
                              DynamicsSolver.QueryFlywheelTorqueNm(), DynamicsSolver.TurboBoostBar);
                DrawKeyValRow("Flywheel torque / boost", Buf, Amber);
                std::snprintf(Buf, sizeof(Buf), "%.1f km/h", SpeedKilometresPerHour);
                DrawKeyValRow("Ground speed", Buf, Theme.Text);
                DrawKeyValRow("Final drive / AWD split", "3.55 : 1  \xC2\xB7  35% F / 65% R", Cyan);
                DrawKeyValRow("6-speed forward ratios", "4.20 / 3.10 / 2.50 / 1.90 / 1.50 / 1.20", Theme.Muted);
                DrawKeyValRow("Shift cut window", "160 ms AMT torque cut", Theme.Muted);
                Y += 6.0f;
                break;
            }

            //----------------------------------------------------------------------------------------------------
            // TAB 3: TYRES — 4-Wheel Selector + 5 Sub-Tabs + Continuum Switcher + Live 32x5 Hex8 Bulge Diagram
            //----------------------------------------------------------------------------------------------------
            case 3u:
            {
                static constexpr std::array<const char*, 4> CornerNames = { "FL", "FR", "RL", "RR" };
                const float WBtn = (Width - 12.0f) * 0.25f;
                for (uint32_t W = 0u; W < 4u; ++W)
                {
                    const float BX  = Origin.x + static_cast<float>(W) * (WBtn + 4.0f);
                    const bool  Sel = (SelectedTyreWheel == W);
                    bool        Hov = false;
                    char        Id[32] = {};
                    std::snprintf(Id, sizeof(Id), "##TyreCorner%u", W);
                    if (KitTarget(Id, ImVec2(BX, Y), ImVec2(WBtn, 23.0f), Hov))
                    {
                        SelectedTyreWheel = W;
                    }
                    KitPillSurface(Ink, ImVec2(BX, Y), ImVec2(BX + WBtn, Y + 23.0f),
                                   Sel ? KitInkWithAlpha(Cyan, 0.22f) : (Hov ? Theme.Surface3 : Theme.Surface2),
                                   Sel ? Cyan : Theme.LineSoft, 5.0f);
                    const float LW = KitTextWidth(Faces.SemiBold, 10.2f, CornerNames[W]);
                    KitDrawText(Ink, Faces.SemiBold, 10.2f, ImVec2(BX + (WBtn - LW) * 0.5f, Y + 4.5f),
                                Sel ? Theme.Text : Theme.Muted, CornerNames[W]);
                }
                Y += 27.0f;

                // 5 Sub-tabs: Tyre · Marks · Smoke · Sparks · Burnout (VehicleSystemsInspector.js)
                static constexpr std::array<const char*, 5> SubTabs = { "Tyre", "Marks", "Smoke", "Sparks", "Burnout" };
                const float SBtn = (Width - 16.0f) * 0.20f;
                for (uint32_t S = 0u; S < 5u; ++S)
                {
                    const float BX  = Origin.x + static_cast<float>(S) * (SBtn + 4.0f);
                    const bool  Sel = (SelectedTyreSubTab == S);
                    bool        Hov = false;
                    char        Id[32] = {};
                    std::snprintf(Id, sizeof(Id), "##TyreSub%u", S);
                    if (KitTarget(Id, ImVec2(BX, Y), ImVec2(SBtn, 21.0f), Hov))
                    {
                        SelectedTyreSubTab = S;
                    }
                    KitPillSurface(Ink, ImVec2(BX, Y), ImVec2(BX + SBtn, Y + 21.0f),
                                   Sel ? KitInkWithAlpha(Accent, 0.18f) : Theme.Surface2,
                                   Sel ? Accent : Theme.LineSoft, 4.5f);
                    const float LW = KitTextWidth(Faces.Medium, 9.4f, SubTabs[S]);
                    KitDrawText(Ink, Faces.Medium, 9.4f, ImVec2(BX + std::max(2.0f, (SBtn - LW) * 0.5f), Y + 4.0f),
                                Sel ? Theme.Text : Theme.Muted, SubTabs[S]);
                }
                Y += 26.0f;

                // Continuum Model Switcher (Solid Hex8 FEM · Orthotropic · Analytical)
                static constexpr std::array<const char*, 3> ModelNames = { "Hex8 FEM", "Membrane", "Analytical" };
                static constexpr std::array<TyreContinuumModel, 3> ModelEnums = {
                    TyreContinuumModel::Solid3DFem,
                    TyreContinuumModel::OrthotropicMembrane,
                    TyreContinuumModel::LinearSpring
                };
                const float MBtn = (Width - 8.0f) / 3.0f;
                for (size_t M = 0u; M < 3u; ++M)
                {
                    const float BX  = Origin.x + static_cast<float>(M) * (MBtn + 4.0f);
                    const bool  Sel = (DynamicsSolver.QueryTyreModel() == ModelEnums[M]);
                    bool        Hov = false;
                    char        Id[32] = {};
                    std::snprintf(Id, sizeof(Id), "##ContModel%zu", M);
                    if (KitTarget(Id, ImVec2(BX, Y), ImVec2(MBtn, 22.0f), Hov))
                    {
                        DynamicsSolver.AssignTyreModel(ModelEnums[M]);
                        DynamicsSolver.EncodeVisualRecord(VisualStateBuffer);
                    }
                    KitPillSurface(Ink, ImVec2(BX, Y), ImVec2(BX + MBtn, Y + 22.0f),
                                   Sel ? KitInkWithAlpha(Mint, 0.20f) : Theme.Surface2,
                                   Sel ? Mint : Theme.LineSoft, 5.0f);
                    const float LW = KitTextWidth(Faces.Medium, 9.6f, ModelNames[M]);
                    KitDrawText(Ink, Faces.Medium, 9.6f, ImVec2(BX + (MBtn - LW) * 0.5f, Y + 4.5f),
                                Sel ? Theme.Text : Theme.Muted, ModelNames[M]);
                }
                Y += 27.0f;

                const uint32_t              WIdx = SelectedTyreWheel % 4u;
                const WheelCornerTelemetry& WT   = DynamicsSolver.QueryWheels()[WIdx];
                const SolidContinuumWheel&  Fem  = DynamicsSolver.QueryContinuumWheels()[WIdx];

                // Meridional Cross-Section Bulge Diagram (active ground contact sector across spinning cage)
                const float DiagH = 128.0f;
                KitPillSurface(Ink, ImVec2(Origin.x, Y), ImVec2(Origin.x + Width, Y + DiagH),
                               KitInkHex(0x0f1216), Theme.Line, 8.0f);
                KitDrawTracked(Ink, Faces.Medium, 8.6f, ImVec2(Origin.x + 10.0f, Y + 6.0f), Mint,
                               "MERIDIONAL CROSS-SECTION - 32x5 NEO-HOOKEAN HEX8 CAGE", 0.7f);

                const float CX    = Origin.x + Width * 0.5f;
                const float GndY  = Y + DiagH - 18.0f;
                const float SclY  = 470.0f;
                const float SclX  = 430.0f;
                const float TyreR = static_cast<float>(ControlVehicleSpecification::TyreOuterRadiusM);
                Ink->AddLine(ImVec2(Origin.x + 16.0f, GndY), ImVec2(Origin.x + Width - 16.0f, GndY),
                             KitInkHex(0x2d3642), 1.2f);

                // Locate circumferential sector currently in deepest radial compression (handles spinning wheel at any angle)
                uint32_t ContactSector  = 24u;
                float    PeakRadialComp = -1.0f;
                for (uint32_t S = 0u; S < SolidContinuumWheel::CircumferentialCount; ++S)
                {
                    const uint32_t CrownNode = S * SolidContinuumWheel::MeridionalCount + 2u;
                    const auto     RestC     = Fem.RestNodePosition(CrownNode);
                    const auto     DispC     = Fem.QueryNodeDisplacement(CrownNode);
                    const float    RestRad   = std::hypot(RestC[0], RestC[2]);
                    const float    DefRad    = std::hypot(RestC[0] + DispC[0], RestC[2] + DispC[2]);
                    const float    RadialIn  = RestRad - DefRad;
                    if (RadialIn > PeakRadialComp)
                    {
                        PeakRadialComp = RadialIn;
                        ContactSector  = S;
                    }
                }

                // Undeformed profile vs Live deformed contact-sector profile (projected into radial/meridional plane)
                ImVec2 PrevRest{}, PrevDef{};
                for (uint32_t M = 0u; M < SolidContinuumWheel::MeridionalCount; ++M)
                {
                    const uint32_t NodeIdx = ContactSector * SolidContinuumWheel::MeridionalCount + M;
                    const auto     Rest    = Fem.RestNodePosition(NodeIdx);
                    const auto     Disp    = Fem.QueryNodeDisplacement(NodeIdx);
                    const float    RestRad = std::hypot(Rest[0], Rest[2]);
                    const float    DefRad  = std::hypot(Rest[0] + Disp[0], Rest[2] + Disp[2]);
                    const float    RadComp = RestRad - DefRad;
                    const float    RestH   = TyreR - RestRad;
                    const float    DefH    = std::max(0.0f, RestH + RadComp * 2.4f);
                    const float    DefY    = Rest[1] + Disp[1] * 3.2f;
                    const ImVec2   PRest(CX + Rest[1] * SclX, GndY - RestH * SclY);
                    const ImVec2   PDef (CX + DefY    * SclX, GndY - DefH  * SclY);
                    if (M > 0u)
                    {
                        Ink->AddLine(PrevRest, PRest, KitInkRgba(140, 160, 185, 95), 1.2f);
                        Ink->AddLine(PrevDef,  PDef,  WT.Shredded ? Rose : Mint, 2.2f);
                    }
                    Ink->AddCircleFilled(PDef, 3.0f, WT.Shredded ? Rose : Cyan);
                    PrevRest = PRest;
                    PrevDef  = PDef;
                }
                const float BulgeMm = (Fem.QueryNodeDisplacement(ContactSector * 5u + 3u)[1] -
                                       Fem.QueryNodeDisplacement(ContactSector * 5u + 1u)[1]) * 1000.0f;
                char DiagFoot[112] = {};
                std::snprintf(DiagFoot, sizeof(DiagFoot),
                              "Crown comp +%.1f mm \xC2\xB7 Sidewall bulge %+.1f mm \xC2\xB7 Mean J %.3f",
                              std::max(0.0f, PeakRadialComp) * 1000.0f, BulgeMm, Fem.MeanJacobian);
                KitDrawText(Ink, Faces.Regular, 9.2f, ImVec2(Origin.x + 12.0f, GndY + 3.0f), Theme.Muted, DiagFoot);
                Y += DiagH + 6.0f;

                // Shred / Restore action button
                bool ShredHov = false;
                if (KitTarget("##ShredTyreBtn", ImVec2(Origin.x, Y), ImVec2(Width, 24.0f), ShredHov))
                {
                    if (WT.Shredded) DynamicsSolver.RestoreTyre(WIdx);
                    else             DynamicsSolver.ShredTyre(WIdx);
                    SynchronizeFromSolver();
                }
                KitPillSurface(Ink, ImVec2(Origin.x, Y), ImVec2(Origin.x + Width, Y + 24.0f),
                               WT.Shredded ? KitInkWithAlpha(Mint, 0.18f) : KitInkWithAlpha(Rose, 0.16f),
                               WT.Shredded ? Mint : Rose, 5.5f);
                KitDrawText(Ink, Faces.Medium, 10.0f, ImVec2(Origin.x + 12.0f, Y + 5.0f),
                            WT.Shredded ? Mint : Rose,
                            WT.Shredded ? "Restore Sculpted Tyre Carcass (220 kPa)"
                                        : "Shred Selected Tyre to Rigid Alloy Rim (0.345 m)");
                Y += 29.0f;

                KitPillSurface(Ink, ImVec2(Origin.x, Y), ImVec2(Origin.x + Width, Y + 126.0f), Theme.Surface, Theme.Line, 7.0f);
                Y += 4.0f;
                char Buf[96] = {};
                const double SurfaceC = WT.SurfaceTempKelvin - 273.15;
                if (SelectedTyreSubTab == 0u)
                {
                    std::snprintf(Buf, sizeof(Buf), "kappa %+.3f  \xC2\xB7  alpha %+.2f\xC2\xB0",
                                  WT.SlipRatio, WT.SlipAngleRad * 57.29578);
                    DrawKeyValRow("Pacejka MF6.1 slip", Buf, Cyan);
                    std::snprintf(Buf, sizeof(Buf), "Fx %.0f N \xC2\xB7 Fy %.0f N \xC2\xB7 Fz %.0f N",
                                  WT.LongitudinalFx, WT.LateralFy, WT.NormalLoadN);
                    DrawKeyValRow("Contact patch forces", Buf, Mint);
                    std::snprintf(Buf, sizeof(Buf), "%.1f mm  (max node %.1f mm)",
                                  WT.TyreDeflectionM * 1000.0, Fem.QueryMaxDisplacementMetres() * 1000.0f);
                    DrawKeyValRow("Radial deflection", Buf, Theme.Text);
                    std::snprintf(Buf, sizeof(Buf), "%.1f\xC2\xB0" "C surface  \xC2\xB7  %.0f J slide",
                                  SurfaceC, WT.SlidingEnergyJ);
                    DrawKeyValRow("Thermal solver", Buf, Amber);
                    std::snprintf(Buf, sizeof(Buf), "%.1f kPa  \xC2\xB7  tread %.1f%%",
                                  WT.GaugePressurePa * 0.001, WT.TreadFraction * 100.0);
                    DrawKeyValRow("Inflation & tread", Buf, Theme.Text);
                    DrawKeyValRow("Hex8 FEM topology", "32 x 5 = 160 nodes (128 Hex8)", Theme.Muted);
                }
                else
                {
                    const double Smoke01 = std::clamp((std::fabs(WT.SlipRatio) - 0.12) * 2.5, 0.0, 1.0);
                    const double Spark01 = WT.Shredded ? std::clamp(std::fabs(SpeedKilometresPerHour) / 60.0, 0.0, 1.0) : 0.0;
                    std::snprintf(Buf, sizeof(Buf), "%.2f", Smoke01);
                    DrawKeyValRow("Plume / skid emission", Buf, Mint);
                    std::snprintf(Buf, sizeof(Buf), "%.2f", Spark01);
                    DrawKeyValRow("Rim-on-asphalt sparks", Buf, Amber);
                    DrawKeyValRow("Skidmark decal ribbon", "|kappa| > 0.14 or |alpha| > 8\xC2\xB0", Theme.Text);
                    DrawKeyValRow("Eulerian smoke source", "Coupled to Effects / Smoke & fire", Cyan);
                    DrawKeyValRow("Burnout thermal threshold", "Surface > 95\xC2\xB0" "C -> carcass shred", Rose);
                    DrawKeyValRow("Wheel mesh partition", "3,968 tyre + 1,568 rim facets", Theme.Muted);
                }
                Y += 6.0f;
                break;
            }

            //----------------------------------------------------------------------------------------------------
            // TAB 4: SUSPENSION — 4-Corner Solved Strut Cards + 2.0 Hz Digressive Cubic Spec
            //----------------------------------------------------------------------------------------------------
            case 4u:
            {
                static constexpr std::array<const char*, 4> CornerTitles = {
                    "FL \xC2\xB7 Front Left", "FR \xC2\xB7 Front Right",
                    "RL \xC2\xB7 Rear Left",  "RR \xC2\xB7 Rear Right"
                };
                const float ColW = (Width - 6.0f) * 0.5f;
                for (uint32_t W = 0u; W < 4u; ++W)
                {
                    const float CX  = Origin.x + static_cast<float>(W % 2u) * (ColW + 6.0f);
                    const float CY  = Y + static_cast<float>(W / 2u) * 66.0f;
                    const auto& WT  = DynamicsSolver.QueryWheels()[W];
                    const auto& Sp  = DynamicsSolver.QueryStruts()[W];
                    KitPillSurface(Ink, ImVec2(CX, CY), ImVec2(CX + ColW, CY + 62.0f), Theme.Surface, Theme.Line, 7.0f);
                    KitDrawTracked(Ink, Faces.Medium, 8.8f, ImVec2(CX + 8.0f, CY + 6.0f),
                                   WT.Grounded ? Mint : Rose, CornerTitles[W], 0.6f);
                    char LineA[64] = {}, LineB[64] = {};
                    std::snprintf(LineA, sizeof(LineA), "Comp %.1f mm (sag %.0f)",
                                  WT.CompressionM * 1000.0, Sp.StaticCompressionM * 1000.0);
                    std::snprintf(LineB, sizeof(LineB), "Load %.0f N \xC2\xB7 k %.1f kN/m",
                                  WT.NormalLoadN, Sp.SpringRateNm * 0.001);
                    KitDrawText(Ink, Faces.SemiBold, 10.2f, ImVec2(CX + 8.0f, CY + 21.0f), Theme.Text, LineA);
                    KitDrawText(Ink, Faces.Regular, 9.5f, ImVec2(CX + 8.0f, CY + 36.0f), Theme.Muted, LineB);
                    const float Ratio = static_cast<float>(std::clamp(WT.CompressionM / std::max(0.05, Sp.MaxTravelM), 0.0, 1.0));
                    Ink->AddRectFilled(ImVec2(CX + 8.0f, CY + 52.0f), ImVec2(CX + ColW - 8.0f, CY + 56.0f),
                                       Theme.Surface2, 2.0f);
                    Ink->AddRectFilled(ImVec2(CX + 8.0f, CY + 52.0f),
                                       ImVec2(CX + 8.0f + (ColW - 16.0f) * Ratio, CY + 56.0f), Cyan, 2.0f);
                }
                Y += 136.0f;

                KitPillSurface(Ink, ImVec2(Origin.x, Y), ImVec2(Origin.x + Width, Y + 126.0f), Theme.Surface, Theme.Line, 7.0f);
                Y += 4.0f;
                DrawKeyValRow("Natural frequency", "2.00 Hz  (\xCE\xB6 = 0.60 critical)", Cyan);
                DrawKeyValRow("Cubic hardening / bump", "1.50x cubic  \xC2\xB7  280 kN/m stop", Amber);
                DrawKeyValRow("Anti-roll stiffness", "80.0 kN/m  (1,500 N\xC2\xB7s/m damp)", Mint);
                DrawKeyValRow("Front SuspensionMount", "[+1.7274, \xC2\xB1" "1.0475, +0.6436] m", Theme.Text);
                DrawKeyValRow("Rear SuspensionMount", "[-1.6686, \xC2\xB1" "1.0475, +0.6436] m", Theme.Text);
                DrawKeyValRow("AxleMount Z socket", "+0.0914 m  (stroke 0.250 m)", Theme.Muted);
                Y += 6.0f;
                break;
            }

            //----------------------------------------------------------------------------------------------------
            // TAB 5: LIGHTS — Beam Selector + Roof Fixture + Emissive Lamps
            //----------------------------------------------------------------------------------------------------
            case 5u:
            {
                static constexpr std::array<const char*, 4> BeamLabels = { "Off", "Low Beam", "High Beam", "Amber Fog" };
                const float BBtn = (Width - 12.0f) * 0.25f;
                for (int B = 0; B < 4; ++B)
                {
                    const float BX  = Origin.x + static_cast<float>(B) * (BBtn + 4.0f);
                    const bool  Sel = (AutomotiveBeamMode == B);
                    bool        Hov = false;
                    char        Id[32] = {};
                    std::snprintf(Id, sizeof(Id), "##BeamModeBtn%d", B);
                    if (KitTarget(Id, ImVec2(BX, Y), ImVec2(BBtn, 25.0f), Hov))
                    {
                        AutomotiveBeamMode = B;
                        HeadlampsEnabled   = (B != 0);
                    }
                    KitPillSurface(Ink, ImVec2(BX, Y), ImVec2(BX + BBtn, Y + 25.0f),
                                   Sel ? KitInkWithAlpha(Amber, 0.22f) : Theme.Surface2,
                                   Sel ? Amber : Theme.LineSoft, 5.5f);
                    const float LW = KitTextWidth(Faces.Medium, 9.8f, BeamLabels[B]);
                    KitDrawText(Ink, Faces.Medium, 9.8f, ImVec2(BX + (BBtn - LW) * 0.5f, Y + 5.5f),
                                Sel ? Theme.Text : Theme.Muted, BeamLabels[B]);
                }
                Y += 30.0f;

                static constexpr std::array<const char*, 3> RoofLabels = { "No Roof Bar", "Slim LED Bar", "Rally 4-Pod" };
                const float RBtn = (Width - 8.0f) / 3.0f;
                for (int R = 0; R < 3; ++R)
                {
                    const float BX  = Origin.x + static_cast<float>(R) * (RBtn + 4.0f);
                    const bool  Sel = (RoofFixtureIndex == R);
                    bool        Hov = false;
                    char        Id[32] = {};
                    std::snprintf(Id, sizeof(Id), "##RoofModeBtn%d", R);
                    if (KitTarget(Id, ImVec2(BX, Y), ImVec2(RBtn, 24.0f), Hov))
                    {
                        RoofFixtureIndex = R;
                    }
                    KitPillSurface(Ink, ImVec2(BX, Y), ImVec2(BX + RBtn, Y + 24.0f),
                                   Sel ? KitInkWithAlpha(Cyan, 0.22f) : Theme.Surface2,
                                   Sel ? Cyan : Theme.LineSoft, 5.5f);
                    const float LW = KitTextWidth(Faces.Medium, 9.8f, RoofLabels[R]);
                    KitDrawText(Ink, Faces.Medium, 9.8f, ImVec2(BX + (RBtn - LW) * 0.5f, Y + 5.0f),
                                Sel ? Theme.Text : Theme.Muted, RoofLabels[R]);
                }
                Y += 30.0f;

                // Brake & Hazard quick toggles
                const float HalfW = (Width - 4.0f) * 0.5f;
                for (int T = 0; T < 2; ++T)
                {
                    bool* TargetFlag = (T == 0) ? &BrakeLampsEnabled : &HazardLampsEnabled;
                    bool  Hov        = false;
                    const float BX   = Origin.x + static_cast<float>(T) * (HalfW + 4.0f);
                    if (KitTarget(T == 0 ? "##BrakeToggle" : "##HazardToggle", ImVec2(BX, Y), ImVec2(HalfW, 24.0f), Hov))
                    {
                        *TargetFlag = !(*TargetFlag);
                    }
                    KitPillSurface(Ink, ImVec2(BX, Y), ImVec2(BX + HalfW, Y + 24.0f),
                                   *TargetFlag ? KitInkWithAlpha(Rose, 0.20f) : Theme.Surface2,
                                   *TargetFlag ? Rose : Theme.LineSoft, 5.5f);
                    KitDrawText(Ink, Faces.Medium, 10.0f, ImVec2(BX + 10.0f, Y + 5.0f),
                                *TargetFlag ? Theme.Text : Theme.Muted,
                                T == 0 ? "Rear LED Brake Bar" : "Amber Hazard Lamps");
                }
                Y += 30.0f;

                KitPillSurface(Ink, ImVec2(Origin.x, Y), ImVec2(Origin.x + Width, Y + 106.0f), Theme.Surface, Theme.Line, 7.0f);
                Y += 4.0f;
                DrawKeyValRow("Projector headlamps", "5,800 K  \xC2\xB7  IES dual-lobe", Amber);
                DrawKeyValRow("Headlight material #13", "Emission 48.7 cd/m\xC2\xB2 scaled", Theme.Text);
                DrawKeyValRow("Taillight material #14", "Full-width crimson LED bar", Rose);
                DrawKeyValRow("Amber indicator #15", "2,100 K turn / hazard", Theme.Text);
                DrawKeyValRow("Clustered light budget", "32 dynamic + static roadway lamps", Cyan);
                Y += 6.0f;
                break;
            }

            //----------------------------------------------------------------------------------------------------
            // TAB 6: CABIN HMI — Live HDR OLED Cluster + 14" Infotainment Preview & Camera Jump
            //----------------------------------------------------------------------------------------------------
            case 6u:
            {
                const float HmiH = 98.0f;
                KitPillSurface(Ink, ImVec2(Origin.x, Y), ImVec2(Origin.x + Width, Y + HmiH),
                               KitInkHex(0x0c1016), Theme.Line, 8.0f);
                KitDrawTracked(Ink, Faces.Medium, 8.8f, ImVec2(Origin.x + 10.0f, Y + 7.0f), Cyan,
                               "LIVE HDR OLED CLUSTER & 14\" INFOTAINMENT (DISPLAYGLASS #77)", 0.75f);
                char BigReadout[64] = {};
                std::snprintf(BigReadout, sizeof(BigReadout), "%3.0f km/h   [%s]   %.0f rpm",
                               std::fabs(SpeedKilometresPerHour), DynamicsSolver.QueryGearLabel(), EngineRpm);
                KitDrawText(Ink, Faces.SemiBold, 16.0f, ImVec2(Origin.x + 12.0f, Y + 28.0f), Mint, BigReadout);
                KitDrawText(Ink, Faces.Regular, 10.2f, ImVec2(Origin.x + 12.0f, Y + 54.0f), Theme.Muted,
                            "Slate cockpit + 74 solid dielectric VehicleGlass facets (IOR 1.52)");

                const float HalfW = (Width - 24.0f) * 0.5f;
                for (int C = 0; C < 2; ++C)
                {
                    const EverestCameraMode Rig = (C == 0) ? EverestCameraMode::Cockpit : EverestCameraMode::HmiTablet;
                    const float BX  = Origin.x + 10.0f + static_cast<float>(C) * (HalfW + 4.0f);
                    const bool  Sel = (CameraMode == Rig);
                    bool        Hov = false;
                    if (KitTarget(C == 0 ? "##JumpDriverCam" : "##JumpHmiCam", ImVec2(BX, Y + 71.0f), ImVec2(HalfW, 21.0f), Hov))
                    {
                        CameraMode = Rig;
                    }
                    KitPillSurface(Ink, ImVec2(BX, Y + 71.0f), ImVec2(BX + HalfW, Y + 92.0f),
                                   Sel ? KitInkWithAlpha(Cyan, 0.22f) : Theme.Surface2,
                                   Sel ? Cyan : Theme.LineSoft, 5.0f);
                    KitDrawText(Ink, Faces.Medium, 9.6f, ImVec2(BX + 10.0f, Y + 74.5f),
                                Sel ? Theme.Text : Theme.Muted,
                                C == 0 ? "Driver Cluster View" : "14\" HMI Tablet View");
                }
                Y += HmiH + 6.0f;

                KitPillSurface(Ink, ImVec2(Origin.x, Y), ImVec2(Origin.x + Width, Y + 106.0f), Theme.Surface, Theme.Line, 7.0f);
                Y += 4.0f;
                DrawKeyValRow("Driver cluster binnacle", "12.3\" curved HDR OLED (680 nits)", Cyan);
                DrawKeyValRow("Centre infotainment", "14.0\" tilted DisplayGlass (0.42 rad)", Mint);
                DrawKeyValRow("Live paint sync", "3D car card tracks BodyCoat swatch", Accent);
                DrawKeyValRow("Cabin trim / seats", "Slate CabinTrim #71 + CabinSeat #72", Theme.Text);
                DrawKeyValRow("Canopy glazing", "Fresnel + Beer-Lambert refraction", Theme.Muted);
                Y += 6.0f;
                break;
            }

            //----------------------------------------------------------------------------------------------------
            // TAB 7: MIRRORS — Left Wing, Right Wing & Interior Rear-View BVH Specular Surfaces
            //----------------------------------------------------------------------------------------------------
            default:
            {
                KitPillSurface(Ink, ImVec2(Origin.x, Y), ImVec2(Origin.x + Width, Y + 146.0f), Theme.Surface, Theme.Line, 7.0f);
                Y += 4.0f;
                DrawKeyValRow("Left wing mirror", "[+0.68, +1.06, +0.76] m \xC2\xB7 MirrorGlass #75", Cyan);
                DrawKeyValRow("Right wing mirror", "[+0.68, -1.06, +0.76] m \xC2\xB7 MirrorGlass #75", Cyan);
                DrawKeyValRow("Interior rear-view", "[+0.42,  0.00, +1.08] m \xC2\xB7 Cabin header", Mint);
                DrawKeyValRow("RTX reflection path", "Exact VK_KHR_ray_query Two-Level BVH", Accent);
                DrawKeyValRow("GTX reflection path", "OrderedSoftwareBvh specular trace", Amber);
                DrawKeyValRow("Surface roughness", "0.015 (metallic 1.0 silvered glass)", Theme.Text);
                DrawKeyValRow("Housing material", "MirrorTrim #76 aerodynamic stalk", Theme.Muted);
                Y += 6.0f;
                break;
            }
        }

        // Bottom X-Ray Collision & Wheel Hull toggle shared across tabs
        bool XRayHov = false;
        if (KitTarget("##VehXRayBtn", ImVec2(Origin.x, Y), ImVec2(Width, 25.0f), XRayHov))
        {
            CollisionOverlayEnabled = !CollisionOverlayEnabled;
        }
        KitPillSurface(Ink, ImVec2(Origin.x, Y), ImVec2(Origin.x + Width, Y + 25.0f),
                       CollisionOverlayEnabled ? KitInkRgba(255, 107, 139, 48) : Theme.Surface2,
                       CollisionOverlayEnabled ? Rose : Theme.LineSoft, 6.0f);
        KitDrawText(Ink, Faces.Medium, 10.2f, ImVec2(Origin.x + 12.0f, Y + 5.0f),
                    CollisionOverlayEnabled ? KitInkHex(0xff9bb0) : Theme.Muted,
                    CollisionOverlayEnabled ? "3D Collision & Wheel Hull X-Ray Overlay: ON"
                                            : "3D Collision & Wheel Hull X-Ray Overlay");
        Y += 30.0f;

        ImGui::SetCursorScreenPos(ImVec2(Origin.x, Y));
        ImGui::Dummy(ImVec2(Width, 0.0f));
    };
#endif
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
    EverestViewportInputs Inputs{};

    Inputs.ExecutionMode = (Illumination.TierSelectionIndex == 0)
                               ? DualTierExecutionMode::HardwareRayQueryRtx
                               : DualTierExecutionMode::ToroidalSdfBvhGtx;
    Inputs.InspectionMode = static_cast<ViewportInspectionMode>(std::clamp(Illumination.InspectionAovIndex, 0, 8));

    Inputs.CameraMode           = Vehicle.CameraMode;
    Inputs.CameraYawDegrees     = OrbitYawDegrees;
    Inputs.CameraPitchDegrees   = OrbitPitchDegrees;
    Inputs.CameraDistanceMetres = OrbitDistanceMetres;

    Inputs.VehiclePosition       = Vehicle.WorldPosition;
    Inputs.VehicleYawRadians     = Vehicle.YawRadians;
    Inputs.VehiclePitchRadians   = Vehicle.PitchRadians;
    Inputs.VehicleRollRadians    = Vehicle.RollRadians;
    Inputs.FrontSteerRadians     = Vehicle.FrontSteerRadians;
    Inputs.WheelSpinRadians      = Vehicle.WheelSpinRadians;
    Inputs.UsePerCornerWheelPose = Vehicle.UseSolvedWheelPose;
    for (size_t W = 0u; W < 4u; ++W)
    {
        const auto& WT                   = Vehicle.DynamicsSolver.QueryWheels()[W];
        const auto& Sp                   = Vehicle.DynamicsSolver.QueryStruts()[W];
        Inputs.WheelSteerPerCorner[W]    = static_cast<float>(WT.SteerAngleRad);
        Inputs.WheelSpinPerCorner[W]     = static_cast<float>(WT.SpinAngleRad);
        Inputs.WheelVerticalOffsetM[W]   = static_cast<float>(WT.CompressionM - Sp.StaticCompressionM);
    }
    Inputs.VehicleEngineRpm      = Vehicle.EngineRpm;
    Inputs.VehicleGearNumber     = Vehicle.GearNumber;

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
    Inputs.MoonlightStrength          = Moon.MoonlightStrength;
    Inputs.MoonEnabled                = Moon.Enabled;
    Inputs.StarsEnabled               = Stars.Enabled;
    Inputs.StarLimitingMagnitude      = Stars.LimitingMagnitude;
    Inputs.StarRotationDegrees        = Stars.RotationDegrees;

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
    Inputs.AutomotiveBeamMode      = static_cast<uint8_t>(std::clamp(Vehicle.AutomotiveBeamMode, 0, 3));
    Inputs.BrakeLampsEnabled       = Vehicle.BrakeLampsEnabled || (Vehicle.DriverControls.Brake > 0.1f);
    Inputs.HazardLampsEnabled      = Vehicle.HazardLampsEnabled;
    Inputs.RoofLightEnabled        = (Vehicle.RoofFixtureIndex != 0);
    Inputs.RoofRallyPodMode        = (Vehicle.RoofFixtureIndex == 2);
    Inputs.CollisionOverlayEnabled = Vehicle.CollisionOverlayEnabled;
    Inputs.TyreSmokeActive         = (std::fabs(Vehicle.DynamicsSolver.QueryWheels()[2].SlipRatio) > 0.16 ||
                                      std::fabs(Vehicle.DynamicsSolver.QueryWheels()[3].SlipRatio) > 0.16);
    return Inputs;
}

void ProjectEverestScene::ProcessSpawnRequest(SceneDirectoryPanel& DirectoryPanel)
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
