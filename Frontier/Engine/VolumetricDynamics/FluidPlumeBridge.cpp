/*==========================================================================================================================================
                                                          FLUIDPLUMEBRIDGE.CPP
==========================================================================================================================================*/

#include "Engine/VolumetricDynamics/FluidPlumeBridge.h"
#include "Engine/Interface/FluidPlumeEditor.h"
#include "Engine/Scene/VehicleReadings.h"
#include "Engine/VolumetricDynamics/GasPresetLibrary.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{

constexpr float DegToRad = 3.14159265358979323846f / 180.0f;

bool IsSoftwareCpuDevice(const VulkanContext& Ctx) noexcept
{
    const std::string& Summary = Ctx.QueryDeviceSummary();
    return Summary.find("SwiftShader") != std::string::npos ||
           Summary.find("llvmpipe")    != std::string::npos ||
           Summary.find("lavapipe")    != std::string::npos ||
           Summary.find("(CPU)")       != std::string::npos;
}

int ResolveGradeLattice(uint32_t Grade, bool SoftwareCpu = false) noexcept
{
    if (SoftwareCpu)
        return Grade == 0u ? 36 : 48;
    switch (Grade)
    {
        case 0u: return 48;
        case 1u: return 64;
        case 2u: return 80;
        default: return 80;
    }
}

uint32_t ExtractWorldQualityGrade(const WorldSpace& World, float SunOut[2], float& ExposureOut) noexcept
{
    SunOut[0]   = 35.0f;
    SunOut[1]   = 42.0f;
    ExposureOut = 1.15f;
    uint32_t QualityGrade = 1u;

    for (const SceneRecordBase* Entry : World.FindAllByType("Solar"))
    {
        const auto* Sol = dynamic_cast<const SolarRecord*>(Entry);
        if (!Sol) continue;
        SunOut[0] = Sol->QueryConfiguration().AzimuthDegrees;
        SunOut[1] = Sol->QueryConfiguration().ElevationDegrees;
        break;
    }
    for (const SceneRecordBase* Entry : World.FindAllByType("RenderPipeline"))
    {
        const auto* Pipe = dynamic_cast<const RenderPipelineRecord*>(Entry);
        if (!Pipe) continue;
        QualityGrade = static_cast<uint32_t>(std::clamp(Pipe->QueryConfiguration().QualityTier, 0, 3));
        break;
    }
    return QualityGrade;
}

} // namespace

//----------------------------------------------------------------------------------------------------------------------
//                             FLUIDDYNAMICS::GASPARAMETERS -> PYRO GASSETTINGS PROJECTION
//----------------------------------------------------------------------------------------------------------------------

void SyncGasParametersToPyroSettings(const FluidDynamics::GasParameters& Source, GasSettings& Target,
                                     uint32_t QualityGrade) noexcept
{
    Target.LatticeResolution    = ResolveGradeLattice(QualityGrade);
    Target.PressureIterations   = std::clamp(static_cast<int32_t>(std::lround(Source.PressureSweeps)), 6, 48);
    Target.Buoyancy             = std::clamp(static_cast<float>(Source.Buoyancy), 0.0f, 12.0f);
    Target.SmokeDissipation     = std::clamp(static_cast<float>(Source.Dissipation), 0.01f, 3.0f);
    Target.CoolingRate          = std::clamp(static_cast<float>(Source.Cooling), 0.02f, 4.0f);
    Target.VorticityConfinement = std::clamp(static_cast<float>(Source.Vorticity), 0.0f, 14.0f);
    Target.TurbulenceStrength   = std::clamp(static_cast<float>(Source.Turbulence) * 2.4f, 0.0f, 10.0f);
    Target.TurbulenceScale      = std::clamp(static_cast<float>(Source.TurbulenceScale), 0.25f, 10.0f);
    Target.CombustionExpansion  = std::clamp(static_cast<float>(Source.Expansion) * 3.2f, 0.0f, 10.0f);
    Target.SootGeneration       = std::clamp(static_cast<float>(Source.Soot) * 1.15f, 0.0f, 4.0f);

    const float WindRad         = static_cast<float>(Source.WindHeading) * DegToRad;
    const float WindSpd         = std::clamp(static_cast<float>(Source.WindSpeed), 0.0f, 30.0f);
    Target.WindX                = std::cos(WindRad) * WindSpd;
    Target.WindZ                = -std::sin(WindRad) * WindSpd;

    Target.DensityExtinction    = std::clamp(static_cast<float>(Source.Density) * 16.5f, 1.0f, 60.0f);
    Target.SmokeAlbedo          = std::clamp(static_cast<float>(Source.Albedo), 0.03f, 0.99f);
    Target.PhaseAnisotropy      = std::clamp(static_cast<float>(Source.Phase), -0.85f, 0.85f);
    Target.InternalScattering   = std::clamp(static_cast<float>(Source.MultiScatter) * 2.5f, 0.0f, 6.0f);
    Target.AmbientIntensity     = std::clamp(static_cast<float>(Source.Ambient) * 0.42f, 0.05f, 3.0f);

    Target.FireIntensity        = std::clamp(static_cast<float>(Source.FlameIntensity) * 5.2f, 0.0f, 20.0f);
    Target.TemperatureScale     = std::clamp(static_cast<float>(Source.FlameTemperature) * 1.15f, 0.1f, 4.0f);

    Target.ShadowSteps          = QualityGrade == 0u ? 6 : 10;
    Target.RaymarchSteps        = QualityGrade == 0u ? 64 : (QualityGrade == 1u ? 96 : 120);
}

//----------------------------------------------------------------------------------------------------------------------
//                                              EVEREST PLUME SCENE RUNTIME
//----------------------------------------------------------------------------------------------------------------------

bool EverestPlumeSceneRuntime::Initialize(VulkanContext& Ctx, const std::filesystem::path& ArchiveRoot,
                                          std::string& OutDiagnostic)
{
    BodyFacet = LoadFacetArchive((ArchiveRoot / "ControlVehicleBody.facet").string());
    TyreFacet = LoadFacetArchive((ArchiveRoot / "ControlVehicleTyre.facet").string());
    RimFacet  = LoadFacetArchive((ArchiveRoot / "ControlVehicleRim.facet").string());

    if (!SdfWorld.Initialize(Ctx,
                             BodyFacet.IsValid() ? &BodyFacet : nullptr,
                             TyreFacet.IsValid() ? &TyreFacet : nullptr,
                             RimFacet.IsValid()  ? &RimFacet  : nullptr,
                             OutDiagnostic))
    {
        return false;
    }

    // Continuous toroidal world-grid across the Proving Grounds driving course (38.4m x 38.4m x 11.52m)
    WorldSettings                      = ConstructPresetSettings("tyre_burnout");
    WorldSettings.LatticeResolution    = 64;
    WorldSettings.BoundsWidth          = 38.4f;
    WorldSettings.BoundsHeight         = 11.52f;
    WorldSettings.EnclosedBox          = true;
    WorldSettings.DynamicBounds        = false;
    WorldSettings.EmitterEnabled       = false;
    WorldSettings.EmitterRate          = 0.0f;
    WorldSettings.SmokeDissipation     = 0.05f;
    WorldSettings.Buoyancy             = 1.45f;
    WorldSettings.TurbulenceStrength   = 1.85f;
    WorldSettings.VorticityConfinement = 4.6f;
    WorldSettings.DensityExtinction    = 18.0f;
    WorldSettings.SmokeAlbedo          = 0.86f;

    if (!WorldSolver.Initialize(Ctx, WorldSettings, OutDiagnostic))
        return false;

    WorldSolver.SetWorldGridMode(true);
    WorldSolver.UpdateWorldGridFocus(0.0f, 0.0f, WorldSettings);
    LastWorldGridOrg = WorldSolver.QueryWorldGridOrigin();

    EditorSettings                   = ConstructPresetSettings("ue5_pyro_default");
    EditorSettings.LatticeResolution = 64;
    if (!EditorSolver.Initialize(Ctx, EditorSettings, OutDiagnostic))
        return false;

    SimVehicle.Place({ 0.0f, 0.0f, 0.53f }, 0.0f);
    TyreIntake.ResetCold();
    TyreIntake.IgnitionPolicy = TyreIgnitionPolicy::Diagnostic;

    Initialized = true;
    return true;
}

void EverestPlumeSceneRuntime::Shutdown(VulkanContext& Ctx) noexcept
{
    if (!Initialized)
        return;
    EditorSolver.Shutdown(Ctx);
    WorldSolver.Shutdown(Ctx);
    SdfWorld.Shutdown(Ctx);
    Initialized = false;
}

void EverestPlumeSceneRuntime::ResetWorldSimulation(VulkanContext& Ctx, WorldSpace& World)
{
    if (!Initialized)
        return;

    std::array<float, 3> SpawnPos = { 0.0f, 0.0f, 0.53f };
    float SpawnYawDeg = 0.0f;
    for (const SceneRecordBase* Entry : World.FindAllByType("Vehicle"))
    {
        if (const auto* Veh = dynamic_cast<const VehicleRecord*>(Entry))
        {
            const auto& Sp = Veh->QueryConfiguration().Spatial;
            // Clamp spawn onto the 3D SDF Proving Grounds pad if level spawns far outside pad
            if (std::fabs(Sp.PositionMetres[0]) <= 24.0f && std::fabs(Sp.PositionMetres[1]) <= 24.0f)
            {
                SpawnPos[0] = Sp.PositionMetres[0];
                SpawnPos[1] = Sp.PositionMetres[1];
                SpawnYawDeg = Sp.OrientationDegrees[2];
            }
            SpawnPos[2] = WorldSdfScene::ResolveCourseHeight(SpawnPos[0], SpawnPos[1], ProvingGroundsMode) + 0.53f;
            break;
        }
    }

    SimVehicle.Place(SpawnPos, SpawnYawDeg);
    TyreIntake.ResetCold();
    WorldSolver.UpdateWorldGridFocus(SpawnPos[0], SpawnPos[1], WorldSettings);
    WorldSolver.ClearLattice(Ctx);
    LastWorldGridOrg     = WorldSolver.QueryWorldGridOrigin();
    WorldGridRelocations = 0u;
    WorldTicks           = 0u;
}

void EverestPlumeSceneRuntime::StepWorldSimulation(VulkanContext& Ctx, WorldSpace& World, double DtSeconds,
                                                   float DriverThrottle, float DriverBrake, float DriverSteer,
                                                   bool DriverHandbrake, bool ForceReset)
{
    if (!Initialized)
        return;

    if (ForceReset)
    {
        ResetWorldSimulation(Ctx, World);
        return;
    }

    for (const std::string& Req : FluidPlumeEditor::TakeRequests())
    {
        if (Req == "clear-smoke")
            WorldSolver.ClearLattice(Ctx);
    }

    const float Dt = std::clamp(static_cast<float>(DtSeconds), 1.0f / 240.0f, 1.0f / 24.0f);

    InputPacket Pkt{};
    Pkt.ThrottleAxis  = std::clamp(DriverThrottle, 0.0f, 1.0f);
    Pkt.BrakeAxis     = std::clamp(DriverBrake, 0.0f, 1.0f);
    Pkt.SteerAxis     = std::clamp(DriverSteer, -1.0f, 1.0f);
    Pkt.HandbrakeAxis = DriverHandbrake ? 1.0f : 0.0f;

    TyreIntake.BurnoutLineLock = (Pkt.ThrottleAxis > 0.35f && (Pkt.BrakeAxis > 0.35f || DriverHandbrake));
    SimVehicle.ApplyInputPacket(Pkt, Dt);

    std::array<float, 3> Pos = SimVehicle.QueryPositionMetres();
    const float GroundZ      = WorldSdfScene::ResolveCourseHeight(Pos[0], Pos[1], ProvingGroundsMode);
    Pos[2]                   = GroundZ + 0.53f;
    SimVehicle.Place(Pos, SimVehicle.QueryYawDegrees());

    VehicleRecord* ActiveVehRecord = nullptr;
    if (SceneRecordBase* FirstVeh = World.FindFirstByType("Vehicle"))
        ActiveVehRecord = dynamic_cast<VehicleRecord*>(FirstVeh);

    if (ActiveVehRecord)
    {
        auto& Sp                 = ActiveVehRecord->AccessConfiguration().Spatial;
        Sp.PositionMetres[0]     = Pos[0];
        Sp.PositionMetres[1]     = Pos[1];
        Sp.PositionMetres[2]     = Pos[2];
        Sp.OrientationDegrees[2] = SimVehicle.QueryYawDegrees();
    }

    const FluidPlumeRecord* PlumeRec = nullptr;
    for (const SceneRecordBase* Entry : World.FindAllByType("FluidPlume"))
    {
        const auto* Candidate = dynamic_cast<const FluidPlumeRecord*>(Entry);
        if (Candidate && Candidate->QueryConfiguration().Enabled)
        {
            PlumeRec = Candidate;
            break;
        }
    }
    if (PlumeRec)
    {
        const auto& Gas = PlumeRec->QueryConfiguration().Gas;
        TyreIntake.SmokeYield = std::clamp(static_cast<float>(Gas.Emission), 0.05f, 4.0f);
        TyreIntake.HeatYield  = std::clamp(static_cast<float>(Gas.Heat), 0.05f, 4.0f);
        TyreIntake.SootYield  = std::clamp(static_cast<float>(Gas.Soot), 0.05f, 4.0f);
    }

    TyreIntake.Advance(SimVehicle, Dt, GroundZ);

    if (ActiveVehRecord)
    {
        VehicleReading Reading{};
        Reading.Simulating             = true;
        Reading.Steps                  = WorldTicks + 1u;
        Reading.StepHz                 = 1.0 / std::max(DtSeconds, 1e-4);
        Reading.Throttle               = { Pkt.ThrottleAxis, Pkt.BrakeAxis, Pkt.SteerAxis, Pkt.HandbrakeAxis };
        Reading.SteeringColumnAngle    = Pkt.SteerAxis * 4.5f;
        Reading.Telemetry.SpeedMetresPerSecond = std::fabs(SimVehicle.QueryForwardSpeedMps());
        Reading.Telemetry.ForwardSpeed         = SimVehicle.QueryForwardSpeedMps();
        Reading.Telemetry.EngineRPM            = std::clamp(900.0f + std::fabs(SimVehicle.QueryForwardSpeedMps()) * 210.0f + Pkt.ThrottleAxis * 4400.0f, 850.0f, 7600.0f);
        Reading.Telemetry.GearIndex            = SimVehicle.QueryForwardSpeedMps() < -0.5f ? 0 : std::clamp(3 + static_cast<int>(std::fabs(SimVehicle.QueryForwardSpeedMps()) / 11.0f), 3, 8);
        const auto& Wheels = TyreIntake.QueryWheels();
        for (size_t Idx = 0u; Idx < 4u; ++Idx)
        {
            Reading.GaugePressure[Idx] = Wheels[Idx].GaugePressurePa;
            Reading.CornerFitted[Idx]  = !Wheels[Idx].Burst;
        }
        PublishVehicleReading(ActiveVehRecord->QueryToken(), Reading);
    }

    float SunAngles[2] = { 35.0f, 42.0f };
    float ExposureVal  = 1.15f;
    const uint32_t QualityGrade = ExtractWorldQualityGrade(World, SunAngles, ExposureVal);

    if (PlumeRec)
    {
        SyncGasParametersToPyroSettings(PlumeRec->QueryConfiguration().Gas, WorldSettings, QualityGrade);
    }
    if (IsSoftwareCpuDevice(Ctx))
    {
        WorldSettings.LatticeResolution  = 48;
        WorldSettings.PressureIterations = std::min(WorldSettings.PressureIterations, 12);
        WorldSettings.RaymarchSteps      = std::min(WorldSettings.RaymarchSteps, 64);
    }
    WorldSettings.BoundsWidth      = 38.4f;
    WorldSettings.BoundsHeight     = 11.52f;
    WorldSettings.EnclosedBox      = true;
    WorldSettings.DynamicBounds    = false;
    WorldSettings.EmitterEnabled   = false;
    WorldSettings.EmitterRate      = 0.0f;
    WorldSettings.SmokeDissipation = std::min(WorldSettings.SmokeDissipation, 0.08f);
    WorldSettings.SunAzimuth       = SunAngles[0];
    WorldSettings.SunElevation     = SunAngles[1];
    WorldSettings.Exposure         = ExposureVal;
    TyreIntake.AdaptTyreDomainAppearance(WorldSettings);
    // Calibrate Beer-Lambert optical extinction & sunlit rubber albedo for the 38.4m world-grid domain
    WorldSettings.DensityExtinction = std::clamp(WorldSettings.DensityExtinction * 0.22f, 2.2f, 4.8f);
    WorldSettings.ShadowDensity     = std::clamp(WorldSettings.ShadowDensity * 0.28f, 1.4f, 3.6f);
    WorldSettings.SmokeAlbedo       = TyreIntake.HasAnyBurningTyre() ? 0.66f : std::max(WorldSettings.SmokeAlbedo, 0.84f);
    if (TyreIntake.HasAnyBurningTyre())
        WorldSettings.FireIntensity = std::max(WorldSettings.FireIntensity, 9.5f);

    WorldSolver.EnsureLatticeResolution(Ctx, WorldSettings.LatticeResolution);
    WorldSolver.UpdateWorldGridFocus(Pos[0], Pos[1], WorldSettings);
    const auto CurOrg = WorldSolver.QueryWorldGridOrigin();
    if (CurOrg != LastWorldGridOrg)
    {
        ++WorldGridRelocations;
        LastWorldGridOrg = CurOrg;
    }

    const float YawRad = SimVehicle.QueryYawDegrees() * DegToRad;
    WorldVehicleRig CarRig{};
    CarRig.Visual.PositionMetres = Pos;
    CarRig.Visual.YawDegrees     = SimVehicle.QueryYawDegrees();
    CarRig.VelocityMps           = {
        std::cos(YawRad) * SimVehicle.QueryForwardSpeedMps(),
        std::sin(YawRad) * SimVehicle.QueryForwardSpeedMps(),
        0.0f,
    };
    const auto& Wheels = TyreIntake.QueryWheels();
    for (size_t W = 0u; W < 4u; ++W)
    {
        CarRig.WheelSteerRad[W]  = Wheels[W].SteerRadians;
        CarRig.WheelSpinRad[W]   = Wheels[W].SpinRadians;
        CarRig.WheelHeatRatio[W] = std::clamp((Wheels[W].SurfaceKelvin - 330.0f) / 300.0f, 0.0f, 1.0f);
    }

    std::vector<WorldVehicleRig> Vehicles = { CarRig };
    const PyroBoundsBox WorldBox = WorldSolver.ComputeBoundsBox(WorldSettings);
    const std::array<float, 3> ZeroOrigin = { 0.0f, 0.0f, 0.0f };

    if ((WorldTicks % 2u) == 0u)
    {
        std::vector<float> SdfAtlas;
        SdfWorld.BuildDomainSdfAtlas(ZeroOrigin, WorldBox, WorldSolver.QueryLatticeResolution(),
                                     WorldSolver.QueryTilesX(), WorldSolver.QueryTilesY(),
                                     ProvingGroundsMode, true, Vehicles, SdfAtlas);
        if (!SdfAtlas.empty())
            WorldSolver.UploadWorldSdfAtlas(Ctx, SdfAtlas.data(), SdfAtlas.size());
    }

    PyroExternalSource ExtSources[8]{};
    std::vector<DynamicFireLight> FireLights;
    int SrcCount = TyreIntake.ProjectIntoDomain(ZeroOrigin, WorldBox, ExtSources, 7, FireLights);

    for (const SceneRecordBase* Entry : World.FindAllByType("FluidPlume"))
    {
        const auto* Rec = dynamic_cast<const FluidPlumeRecord*>(Entry);
        if (!Rec || !Rec->QueryConfiguration().Enabled || SrcCount >= 8) continue;
        const uint32_t TestEm = Rec->QueryConfiguration().TestEmitter;
        if (TestEm == 0u)
            continue;

        PyroExternalSource& Src = ExtSources[SrcCount++];
        Src.PositionUvw      = { 0.5f, 0.08f, 0.5f };
        Src.NormalizedRadius = std::clamp(0.85f / WorldBox.EffectiveWidth, 0.008f, 0.12f);
        Src.VelocityPyro     = { 0.0f, 2.2f, 0.0f };
        Src.SmokeRate        = TestEm == 2u ? 2.4f : 3.2f;
        Src.Temperature      = TestEm == 2u ? 4.8f : 1.1f;
        Src.FuelRate         = TestEm == 2u ? 3.8f : 0.0f;
        Src.SootRate         = TestEm == 2u ? 0.68f : 0.12f;
    }

    LastWorldActiveSrcs = static_cast<uint32_t>(SrcCount);
    WorldSolver.AssignExternalSources(ExtSources, SrcCount);
    WorldSolver.AdvanceSimulation(Ctx, WorldSettings, Dt);
    ++WorldTicks;
}

SceneViewportImage EverestPlumeSceneRuntime::PresentSceneViewport(VulkanContext& Ctx, WorldSpace& World,
                                                                  const SceneViewportRequest& Request)
{
    SceneViewportImage Out{};
    if (!Initialized)
        return Out;

    const auto StartClock = std::chrono::steady_clock::now();
    const bool SoftCpu    = IsSoftwareCpuDevice(Ctx);
    const float Density   = std::clamp(Request.PixelDensity * (SoftCpu ? 0.65f : 1.0f), 0.5f, 2.0f);
    const int W = static_cast<int>(std::clamp(std::lround(Request.PanelWidth  * Density), 64L, 2560L));
    const int H = static_cast<int>(std::clamp(std::lround(Request.PanelHeight * Density), 64L, 1440L));

    // Hold cached background scene image while a modal standalone *Editor covers the workspace
    if (InspectorRuntime::Instance().QueryActiveEditorToken().IsValid() && SdfWorld.QueryColorUiToken() != 0u)
    {
        Out.Texture                  = SdfWorld.QueryColorUiToken();
        Out.Readout.Presenting       = true;
        Out.Readout.Held             = true;
        Out.Readout.Adapter          = Ctx.QueryDeviceSummary();
        Out.Readout.RendererTag      = "Vulkan 1.1 SDF + Toroidal Pyro 3D";
        Out.Readout.ScreenExtent     = { static_cast<uint32_t>(SdfWorld.QueryWidth()), static_cast<uint32_t>(SdfWorld.QueryHeight()) };
        Out.Readout.TraceExtent      = Out.Readout.ScreenExtent;
        Out.Status                   = "Held behind active modal editor";
        return Out;
    }

    float SunAngles[2] = { 35.0f, 42.0f };
    float ExposureVal  = 1.15f;
    const uint32_t QualityGrade = ExtractWorldQualityGrade(World, SunAngles, ExposureVal);

    const float Fx = static_cast<float>(Request.CameraTarget[0] - Request.CameraPosition[0]);
    const float Fy = static_cast<float>(Request.CameraTarget[1] - Request.CameraPosition[1]);
    const float Fz = static_cast<float>(Request.CameraTarget[2] - Request.CameraPosition[2]);
    const float FNorm = std::max(std::sqrt(Fx * Fx + Fy * Fy + Fz * Fz), 1e-5f);
    const std::array<float, 3> Fwd = { Fx / FNorm, Fy / FNorm, Fz / FNorm };

    const float UxIn = static_cast<float>(Request.CameraUp[0]);
    const float UyIn = static_cast<float>(Request.CameraUp[1]);
    const float UzIn = static_cast<float>(Request.CameraUp[2]);
    float Rx = Fwd[1] * UzIn - Fwd[2] * UyIn;
    float Ry = Fwd[2] * UxIn - Fwd[0] * UzIn;
    float Rz = Fwd[0] * UyIn - Fwd[1] * UxIn;
    const float RNorm = std::max(std::sqrt(Rx * Rx + Ry * Ry + Rz * Rz), 1e-5f);
    const std::array<float, 3> Right = { Rx / RNorm, Ry / RNorm, Rz / RNorm };
    const std::array<float, 3> UpVec = {
        Right[1] * Fwd[2] - Right[2] * Fwd[1],
        Right[2] * Fwd[0] - Right[0] * Fwd[2],
        Right[0] * Fwd[1] - Right[1] * Fwd[0],
    };

    WorldCameraView Cam{};
    Cam.Eye = {
        static_cast<float>(Request.CameraPosition[0]),
        static_cast<float>(Request.CameraPosition[1]),
        static_cast<float>(Request.CameraPosition[2]),
    };
    Cam.Forward    = Fwd;
    Cam.Right      = Right;
    Cam.Up         = UpVec;
    Cam.FovRadians = std::clamp(static_cast<float>(Request.VerticalFieldOfView), 0.35f, 1.92f);
    Cam.Aspect     = static_cast<float>(W) / static_cast<float>(std::max(H, 1));

    WorldEnvironmentLight Env{};
    Env.SunAzimuthDeg    = SunAngles[0];
    Env.SunElevationDeg  = SunAngles[1];
    Env.SunIntensity     = 2.35f;
    Env.AmbientIntensity = 0.42f;
    Env.Exposure         = ExposureVal;

    WorldVehicleRig CarRig{};
    CarRig.Visual.PositionMetres = SimVehicle.QueryPositionMetres();
    CarRig.Visual.YawDegrees     = SimVehicle.QueryYawDegrees();
    const auto& Wheels = TyreIntake.QueryWheels();
    for (size_t Idx = 0u; Idx < 4u; ++Idx)
    {
        CarRig.WheelSteerRad[Idx]  = Wheels[Idx].SteerRadians;
        CarRig.WheelSpinRad[Idx]   = Wheels[Idx].SpinRadians;
        CarRig.WheelHeatRatio[Idx] = std::clamp((Wheels[Idx].SurfaceKelvin - 330.0f) / 300.0f, 0.0f, 1.0f);
    }
    std::vector<WorldVehicleRig> Vehicles = { CarRig };

    PyroExternalSource UnusedSrcs[8]{};
    std::vector<DynamicFireLight> FireLights;
    const PyroBoundsBox WorldBox = WorldSolver.ComputeBoundsBox(WorldSettings);
    TyreIntake.ProjectIntoDomain({ 0.0f, 0.0f, 0.0f }, WorldBox, UnusedSrcs, 8, FireLights);

    if (SdfWorld.BeginWorldFrame(Ctx, W, H, ProvingGroundsMode, true, Cam, Env, Vehicles, FireLights))
    {
        SdfWorld.CompositeDomainIntoWorld(Ctx, WorldSolver, WorldSettings, { 0.0f, 0.0f, 0.0f }, Cam);
        SdfWorld.EndWorldFrame(Ctx);
    }

    Out.Texture = SdfWorld.QueryColorUiToken();

    const auto EndClock = std::chrono::steady_clock::now();
    const double Ms     = std::chrono::duration<double, std::milli>(EndClock - StartClock).count();

    Out.Readout.Presenting          = (Out.Texture != 0u);
    Out.Readout.Adapter             = Ctx.QueryDeviceSummary();
    Out.Readout.Quality             = QualityGrade == 0u ? "low" : (QualityGrade == 1u ? "medium" : "high");
    Out.Readout.RendererTag         = "Vulkan 1.1 SDF + Toroidal Pyro 3D";
    Out.Readout.ScreenExtent        = { static_cast<uint32_t>(W), static_cast<uint32_t>(H) };
    Out.Readout.TraceExtent         = Out.Readout.ScreenExtent;
    Out.Readout.RefreshMilliseconds = Ms;
    Out.Readout.LatestMilliseconds  = Ms;
    Out.Readout.ClipResolution      = static_cast<uint32_t>(WorldSolver.QueryLatticeResolution());

    char StatusBuf[160];
    std::snprintf(StatusBuf, sizeof(StatusBuf), "Vulkan 1.1 SDF + Pyro World-Grid · %dx%d · %.1f ms", W, H, Ms);
    Out.Status = StatusBuf;

    FluidPlumeReadout Readout{};
    Readout.Grade       = QualityGrade;
    Readout.Resident    = true;
    Readout.Sources     = LastWorldActiveSrcs;
    Readout.Ticks       = static_cast<uint32_t>(WorldTicks);
    Readout.Relocations = static_cast<uint32_t>(WorldGridRelocations);
    FluidPlumeEditor::AssignReadout(Readout);

    return Out;
}

PlumeViewportImage EverestPlumeSceneRuntime::PresentPlumeViewport(VulkanContext& Ctx,
                                                                  const PlumeViewportRequest& Request)
{
    PlumeViewportImage Out{};
    if (!Initialized || Request.Gas == nullptr)
        return Out;

    const auto StartClock = std::chrono::steady_clock::now();
    const int W = static_cast<int>(std::clamp(Request.Extent[0] > 0u ? Request.Extent[0] : 640u, 64u, 2048u));
    const int H = static_cast<int>(std::clamp(Request.Extent[1] > 0u ? Request.Extent[1] : 420u, 64u, 1440u));

    EditorSettings = ConstructPresetSettings(Request.TestEmitter == 2u ? "ue5_pyro_default" : "tyre_burnout");
    SyncGasParametersToPyroSettings(*Request.Gas, EditorSettings, IsSoftwareCpuDevice(Ctx) ? 0u : 1u);
    if (IsSoftwareCpuDevice(Ctx))
    {
        EditorSettings.LatticeResolution  = 40;
        EditorSettings.PressureIterations = 10;
        EditorSettings.RaymarchSteps      = 56;
    }
    if (Request.TestEmitter == 0u || !Request.Enabled)
    {
        EditorSettings.EmitterEnabled = false;
        EditorSettings.EmitterRate    = 0.0f;
    }
    else if (Request.TestEmitter == 1u)
    {
        EditorSettings.EmitterEnabled     = true;
        EditorSettings.EmitterRate        = 1.0f;
        EditorSettings.EmitterSmoke       = std::clamp(static_cast<float>(Request.Gas->Emission) * 2.6f, 0.2f, 8.0f);
        EditorSettings.EmitterTemperature = std::clamp(static_cast<float>(Request.Gas->Heat) * 1.4f, 0.1f, 5.0f);
        EditorSettings.EmitterFuel        = 0.0f;
        EditorSettings.FireIntensity      = 0.0f;
    }
    else
    {
        EditorSettings.EmitterEnabled     = true;
        EditorSettings.EmitterRate        = 1.0f;
        EditorSettings.EmitterSmoke       = std::clamp(static_cast<float>(Request.Gas->Emission) * 2.0f, 0.2f, 8.0f);
        EditorSettings.EmitterTemperature = std::clamp(static_cast<float>(Request.Gas->Heat) * 3.8f, 0.5f, 10.0f);
        EditorSettings.EmitterFuel        = std::clamp(static_cast<float>(Request.Gas->FlameIntensity) * 3.2f, 0.5f, 10.0f);
    }

    EditorSolver.SetWorldGridMode(false);
    EditorSolver.DisableWorldSdf();
    EditorSolver.EnsureLatticeResolution(Ctx, EditorSettings.LatticeResolution);
    EditorSolver.AssignExternalSources(nullptr, 0);

    if (Request.Clear)
    {
        EditorSolver.ClearLattice(Ctx);
        EditorTicks = 0u;
    }

    if (Request.Seconds > 1e-6)
    {
        const float Dt = std::clamp(static_cast<float>(Request.Seconds * Request.Gas->TimeScale), 1.0f / 120.0f, 1.0f / 24.0f);
        EditorSolver.AdvanceSimulation(Ctx, EditorSettings, Dt);
        ++EditorTicks;
    }

    PyroOrbitCamera Orbit{};
    Orbit.Target     = { 0.0f, 0.35f, 0.0f };
    Orbit.Theta      = static_cast<float>(Request.Yaw);
    Orbit.Phi        = std::clamp(1.5707963f - static_cast<float>(Request.Pitch), 0.18f, 2.95f);
    Orbit.Distance   = std::clamp(static_cast<float>(Request.Distance) * 0.32f, 1.8f, 14.0f);
    Orbit.FovDegrees = 48.0f;

    const float Aspect = static_cast<float>(W) / static_cast<float>(std::max(H, 1));
    const PyroCameraFrame Cam = Orbit.ComputeFrame(Aspect);
    EditorSolver.RenderStandaloneViewport(Ctx, EditorSettings, Cam, W, H, false);

    const uint64_t UiTok = EditorSolver.QueryViewportUiToken();
    if (UiTok != 0u)
    {
        Out.Texture = UiTok;
        Out.Extent  = { static_cast<uint32_t>(W), static_cast<uint32_t>(H) };
    }

    const auto EndClock = std::chrono::steady_clock::now();
    Out.RefreshMilliseconds = std::chrono::duration<double, std::milli>(EndClock - StartClock).count();
    Out.Adapter             = Ctx.QueryDeviceSummary();
    Out.Solving             = true;

    Out.Readout.Grade       = 1u;
    Out.Readout.Resident    = true;
    Out.Readout.Sources     = (Request.Enabled && Request.TestEmitter != 0u) ? 1u : 0u;
    Out.Readout.Ticks       = static_cast<uint32_t>(EditorTicks);
    Out.Readout.Relocations = 0u;
    FluidPlumeEditor::AssignReadout(Out.Readout);

    return Out;
}

} // namespace Frontier
