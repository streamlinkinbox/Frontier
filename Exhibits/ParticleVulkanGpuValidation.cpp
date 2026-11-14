/*============================================================================================================================================
                                                    PARTICLEVULKANGPUVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Headless Vulkan 1.3 .slang Compute & Graphics validation: CPU-vs-GPU numerical parity, 8192-slot reduction, XPBD cloth & GPU PNG proofs

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/MathConstants.h"
#include "Engine/SimulationSpace/ForceFields/FieldLatticeCascade.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldAccumulator.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldDescriptors.h"
#include "Engine/SimulationSpace/Particles/ParticleChannelBank.h"
#include "Engine/SimulationSpace/Particles/ParticleChannelLayouts.h"
#include "Engine/SimulationSpace/Particles/ParticleDynamics.h"
#include "Engine/SimulationSpace/Particles/ParticleKinematicsSolvers.h"
#include "Engine/SimulationSpace/Particles/ParticleSimulation.h"
#include "Engine/SimulationSpace/Particles/ParticleVulkanExecutor.h"
#include "Engine/Workspaces/Particle/ParticleViewportRasterization.h"

#if defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#if defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace
{

using namespace Frontier;
using namespace Frontier::SimulationSpace::ForceFields;
using namespace Frontier::SimulationSpace::Particles;
using namespace Frontier::Workspaces;

[[nodiscard]] bool Require(bool Condition, const char* Message)
{
    if (!Condition)
    {
        std::cerr << "[ParticleVulkanGpuValidation] FAIL: " << Message << "\n";
        return false;
    }
    return true;
}

[[nodiscard]] std::filesystem::path LocateShaderDirectory(int Argc, char** Argv)
{
    for (int Idx = 1; Idx < Argc; ++Idx)
    {
        const std::string Arg = Argv[Idx];
        constexpr std::string_view Prefix = "--shader-dir=";
        if (Arg.rfind(Prefix, 0) == 0)
        {
            return std::filesystem::path(Arg.substr(Prefix.size()));
        }
    }
    const std::array<std::filesystem::path, 3u> Candidates = {{
        std::filesystem::path("_Build/CompiledParticleShaders"),
        std::filesystem::path("build/CompiledShaders"),
        std::filesystem::path("CompiledShaders"),
    }};
    for (const auto& Dir : Candidates)
    {
        if (std::filesystem::exists(Dir / "ParticleAdvectCompute.spv"))
        {
            return Dir;
        }
    }
    return std::filesystem::path("_Build/CompiledParticleShaders");
}

void WriteRgbaPng(
    const std::filesystem::path& OutPath,
    uint32_t                     WidthPx,
    uint32_t                     HeightPx,
    std::span<const uint8_t>     Pixels)
{
    std::filesystem::create_directories(OutPath.parent_path());
    stbi_write_png(
        OutPath.string().c_str(),
        static_cast<int>(WidthPx),
        static_cast<int>(HeightPx),
        4,
        Pixels.data(),
        static_cast<int>(WidthPx * 4u));
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    VALIDATION ENTRY
//------------------------------------------------------------------------------------------------------------------------

int main(int Argc, char** Argv)
{
    const std::filesystem::path ShaderDir      = LocateShaderDirectory(Argc, Argv);
    const std::filesystem::path SwiftShaderDir = std::filesystem::path("ExternalPackages/VulkanCompute/SwiftShader");
    const std::filesystem::path ImageDir       = std::filesystem::path("Exhibits/Images/Particles");

    ParticleVulkanExecutor Executor;
    std::string Diagnostic;
    if (!Require(Executor.Initialize(ShaderDir, SwiftShaderDir, Diagnostic),
                 ("ParticleVulkanExecutor::Initialize failed: " + Diagnostic).c_str()))
    {
        return 1;
    }

    ParticleViewportRasterization Rasterizer;
    if (!Require(Rasterizer.Initialize(Executor, ShaderDir, Diagnostic),
                 ("ParticleViewportRasterization::Initialize failed: " + Diagnostic).c_str()))
    {
        return 1;
    }

    std::cout << "[ParticleVulkanGpuValidation] Vulkan 1.3 Device: " << Executor.QueryDeviceName() << "\n";
    FieldLatticeCascade GpuForcePacker(8u);

    //--------------------------------------------------------------------------------------------------------------------
    // TEST 1 — CPU-VS-GPU NUMERICAL PARITY (COUPLED MULTI-SOURCE FORCEFIELDS + GRAVITY + CURL + COLLIDERS + CURVES)
    //--------------------------------------------------------------------------------------------------------------------
    {
        constexpr uint32_t ParticleCount = 640u;
        constexpr uint32_t StepCount     = 60u;
        constexpr float    FixedDt       = 1.0f / 60.0f;

        ForceFieldAccumulator ForceField;
        {
            ForceSourceRecordEntry Wind{};
            Wind.UniqueToken           = 1u;
            Wind.Label                 = "ParityWind";
            Wind.Model                 = ForceModel::LinearWind;
            Wind.Strength              = 6.4f;
            Wind.EulerDegrees          = { 0.0f, -4.0f, 32.0f };
            Wind.RadiusMeters          = 40.0f;
            Wind.Attenuation           = ForceAttenuationMode::SmoothStep;
            Wind.CentreMeters          = { 0.0f, 0.0f, 2.0f };
            Wind.EvaluatedCentreMeters = Wind.CentreMeters;

            ForceSourceRecordEntry Column{};
            Column.UniqueToken           = 2u;
            Column.Label                 = "ParityVortex";
            Column.Model                 = ForceModel::RotationalColumn;
            Column.Strength              = 8.5f;
            Column.RadiusMeters          = 16.0f;
            Column.CoreRadiusMeters      = 1.4f;
            Column.SwirlRatio            = 0.82f;
            Column.Attenuation           = ForceAttenuationMode::SmoothStep;
            Column.CentreMeters          = { 0.8f, 0.4f, 1.5f };
            Column.EvaluatedCentreMeters = Column.CentreMeters;

            ForceSourceRecordEntry Updraft{};
            Updraft.UniqueToken           = 3u;
            Updraft.Label                 = "ParityUpdraft";
            Updraft.Model                 = ForceModel::FlowStream;
            Updraft.StreamProfile         = FlowStreamProfile::ThermalUpdraft;
            Updraft.Strength              = 5.2f;
            Updraft.RadiusMeters          = 14.0f;
            Updraft.CentreMeters          = { -0.4f, 0.2f, 0.8f };
            Updraft.EvaluatedCentreMeters = Updraft.CentreMeters;

            const std::array<ForceSourceRecordEntry, 3u> Rig = { Wind, Column, Updraft };
            ForceField.AssignSources(Rig);
        }

        // Build identical initial SoA particle bank
        ParticleChannelBank InitialBank;
        InitialBank.ConfigureCapacity(ParticleCount, ChannelStoragePolicy::FixedSlotOrder);
        auto InitKin    = InitialBank.RequireChannel(ChannelLayouts::Kinematics);
        auto InitLife   = InitialBank.RequireChannel(ChannelLayouts::Lifespan);
        auto InitAppear = InitialBank.RequireChannel(ChannelLayouts::Appearance);
        uint32_t BeginSlot = 0u;
        (void)InitialBank.AcquireSlots(ParticleCount, BeginSlot);

        ParticleRandomGenerator Rng(20261114u);
        for (uint32_t Idx = 0u; Idx < ParticleCount; ++Idx)
        {
            const float Angle  = Rng.NextRange(0.0f, Constants::TauFloat);
            const float Radius = Rng.NextRange(0.15f, 2.4f);
            InitKin[Idx].PosX        = std::cos(Angle) * Radius;
            InitKin[Idx].PosY        = std::sin(Angle) * Radius;
            InitKin[Idx].PosZ        = Rng.NextRange(0.4f, 3.2f);
            InitKin[Idx].InverseMass = 1.0f;
            InitKin[Idx].VelX        = Rng.NextRange(-1.8f, 1.8f);
            InitKin[Idx].VelY        = Rng.NextRange(-1.8f, 1.8f);
            InitKin[Idx].VelZ        = Rng.NextRange(0.5f, 3.6f);
            InitKin[Idx].DragScale   = 1.0f;

            InitLife[Idx].AgeSeconds     = 0.0f;
            InitLife[Idx].MaxLifeSeconds = Rng.NextRange(0.65f, 3.5f); // Includes live expiry verification
            InitLife[Idx].SeedUint32     = Rng.NextUint32();
            InitLife[Idx].LivenessMask   = 1u;

            InitAppear[Idx].RadiusMeters  = 0.08f;
            InitAppear[Idx].RollRadians   = Rng.NextRange(0.0f, Constants::TauFloat);
            InitAppear[Idx].SpinRadPerSec = Rng.NextRange(-1.5f, 1.5f);
            InitAppear[Idx].ColorRampT    = 0.0f;
        }

        // CPU reference simulation with exact matching solvers
        ParticleChannelBank CpuBank;
        CpuBank.ConfigureCapacity(ParticleCount, ChannelStoragePolicy::FixedSlotOrder);
        auto CpuKin    = CpuBank.RequireChannel(ChannelLayouts::Kinematics);
        auto CpuLife   = CpuBank.RequireChannel(ChannelLayouts::Lifespan);
        auto CpuAppear = CpuBank.RequireChannel(ChannelLayouts::Appearance);
        (void)CpuBank.AcquireSlots(ParticleCount, BeginSlot);
        std::memcpy(CpuKin.data(), InitKin.data(), ParticleCount * sizeof(KinematicsChannel));
        std::memcpy(CpuLife.data(), InitLife.data(), ParticleCount * sizeof(LifespanChannel));
        std::memcpy(CpuAppear.data(), InitAppear.data(), ParticleCount * sizeof(AppearanceChannel));

        GravityBuoyancyParameters    GravCfg{ 0.55f, 0.8f, 0.28f };
        ForceFieldCouplingParameters ForceCfg{ 1.15f, 1.0f, 0.0f, 1.0f, false, 1u };
        CurlTurbulenceParameters     CurlCfg{ 0.9f, 0.72f, 0.5f };
        AppearanceCurveParameters    AppCfg{};
        AppCfg.Driver          = ColorSampleDriver::SpeedMagnitude;
        AppCfg.WrapMode        = RampWrapMode::Clamp;
        AppCfg.SpeedNormMaxMs  = 12.0f;
        AppCfg.SizeStartMeters = 0.09f;
        AppCfg.SizeEndMeters   = 0.03f;
        AppCfg.SizeMultiplier  = 1.0f;
        AppCfg.ColorA          = { 0.25f, 0.65f, 1.0f, 1.0f };
        AppCfg.ColorC          = { 0.98f, 0.78f, 0.30f, 0.92f };
        AppCfg.ColorB          = { 1.0f, 0.32f, 0.14f, 0.40f };
        GroundPlaneParameters GroundCfg{ 0.0f, 0.42f, 0.18f };

        if (!Require(Executor.UploadChannelBank(InitialBank, Diagnostic),
                     ("UploadChannelBank failed: " + Diagnostic).c_str()))
        {
            return 1;
        }

        DynamicSphereCollider TestSphere{};
        TestSphere.CentreMeters = { 1.1f, 0.5f, 1.2f };
        TestSphere.RadiusMeters = 0.55f;
        TestSphere.VelocityMs   = { -0.4f, 0.2f, 0.0f };
        TestSphere.Restitution  = 0.50f;
        TestSphere.ImpulseScale = 1.0f;
        TestSphere.Active       = true;
        const std::array<DynamicSphereCollider, 1u> Spheres = { TestSphere };

        std::vector<ForceSourceShaderEntry> PackedSources;
        std::vector<SpatialVector3>         PackedWaypoints;
        ParticleReadbackSummary             GpuSummary{};

        GravityBuoyancySolver       CpuGrav(GravCfg);
        ForceFieldCouplingSolver    CpuForce(ForceCfg);
        CurlTurbulenceSolver        CpuCurl(CurlCfg);
        SemiImplicitEulerIntegrator CpuInt;
        GroundPlaneCollider         CpuGround(GroundCfg);
        SphereContactCollider       CpuSphere;
        AppearanceCurveSolver       CpuApp(AppCfg);

        for (uint32_t Step = 0u; Step < StepCount; ++Step)
        {
            const double SimTime = static_cast<double>(Step) * FixedDt;
            ForceField.StepAnimation(SimTime, false);

            SimulationTickInputs CpuIn{};
            CpuIn.TimeSeconds      = SimTime;
            CpuIn.DeltaTimeSeconds = FixedDt;
            CpuIn.ForceAccumulator = &ForceField;
            CpuIn.SphereColliders  = Spheres;
            CpuIn.TickIndex        = Step;

            for (uint32_t Idx = 0u; Idx < ParticleCount; ++Idx)
            {
                if ((CpuLife[Idx].LivenessMask & 1u) == 0u)
                {
                    continue;
                }
                CpuGrav.EvaluateRange(CpuBank, Idx, Idx + 1u, CpuIn);
                CpuForce.EvaluateRange(CpuBank, Idx, Idx + 1u, CpuIn);
                CpuCurl.EvaluateRange(CpuBank, Idx, Idx + 1u, CpuIn);
                CpuInt.AdvanceRange(CpuBank, Idx, Idx + 1u, CpuIn);
                CpuGround.EnforceRange(CpuBank, Idx, Idx + 1u, CpuIn);
                CpuSphere.EnforceRange(CpuBank, Idx, Idx + 1u, CpuIn);
                CpuApp.EvaluateRange(CpuBank, Idx, Idx + 1u, CpuIn);
                CpuAppear[Idx].RollRadians += CpuAppear[Idx].SpinRadPerSec * FixedDt;
                CpuLife[Idx].AgeSeconds    += FixedDt;
                if (CpuLife[Idx].MaxLifeSeconds > 0.0f && CpuLife[Idx].AgeSeconds >= CpuLife[Idx].MaxLifeSeconds)
                {
                    CpuLife[Idx].LivenessMask &= ~1u;
                }
            }

            GpuForcePacker.PackGpuSourceBuffers(ForceField, SimTime, PackedSources, PackedWaypoints);
            ParticleGpuStepRequest GpuReq{};
            GpuReq.PackedForceSources          = PackedSources;
            GpuReq.PackedForceWaypoints        = PackedWaypoints;
            GpuReq.Uniforms.DeltaTimeSec       = FixedDt;
            GpuReq.Uniforms.TimeSeconds        = static_cast<float>(SimTime);
            GpuReq.Uniforms.ActiveCount        = ParticleCount;
            GpuReq.Uniforms.Capacity           = ParticleCount;
            GpuReq.Uniforms.GravityDragCurl    = { GravCfg.GravityScale, GravCfg.BuoyancyAccelMs2, GravCfg.DragCoefficient, CurlCfg.AmplitudeMs };
            GpuReq.Uniforms.CurlTimingBounds   = { CurlCfg.FrequencyPerM, CurlCfg.TimeScrollSpeed, GroundCfg.FloorZMeters, GroundCfg.Restitution };
            GpuReq.Uniforms.ForceCoupling      = { ForceCfg.CouplingGain, ForceCfg.AccelScale, ForceCfg.DragScale, GroundCfg.SlidingFriction };
            GpuReq.Uniforms.SphereCenterRadius = { TestSphere.CentreMeters.X, TestSphere.CentreMeters.Y, TestSphere.CentreMeters.Z, TestSphere.RadiusMeters };
            GpuReq.Uniforms.SphereVelBounce    = { TestSphere.VelocityMs.X, TestSphere.VelocityMs.Y, TestSphere.VelocityMs.Z, TestSphere.Restitution };
            GpuReq.Uniforms.ColliderFlags      = { 1u, 1u, 0u, Step + 1u };
            GpuReq.Uniforms.ColorStopA         = AppCfg.ColorA;
            GpuReq.Uniforms.ColorStopC         = AppCfg.ColorC;
            GpuReq.Uniforms.ColorStopB         = AppCfg.ColorB;
            GpuReq.Uniforms.SizeSpinMetrics    = { AppCfg.SizeStartMeters, AppCfg.SizeEndMeters, AppCfg.SizeMultiplier, 1.5f };
            GpuReq.Uniforms.ColorDriverNorm    = { AppCfg.SpeedNormMaxMs, AppCfg.HeightNormMaxM, AppCfg.PhaseOffset, 0.9f };
            GpuReq.Uniforms.ColorDriverMode    = { static_cast<uint32_t>(AppCfg.Driver), static_cast<uint32_t>(AppCfg.WrapMode), 1u, 0u };
            GpuReq.Uniforms.ReleaseSpeedLife   = { 4.0f, 1.0f, 2.0f, TestSphere.ImpulseScale };

            if (!Require(Executor.DispatchStep(GpuReq, GpuSummary, Diagnostic),
                         ("DispatchStep failed in parity test: " + Diagnostic).c_str()))
            {
                return 1;
            }
        }

        ParticleChannelBank GpuBank;
        GpuBank.ConfigureCapacity(ParticleCount, ChannelStoragePolicy::FixedSlotOrder);
        (void)GpuBank.RequireChannel(ChannelLayouts::Kinematics);
        (void)GpuBank.RequireChannel(ChannelLayouts::Lifespan);
        (void)GpuBank.RequireChannel(ChannelLayouts::Appearance);
        (void)GpuBank.AcquireSlots(ParticleCount, BeginSlot);
        if (!Require(Executor.DownloadChannelBank(GpuBank, Diagnostic),
                     ("DownloadChannelBank failed: " + Diagnostic).c_str()))
        {
            return 1;
        }

        const auto GpuKin    = GpuBank.QueryChannel(ChannelLayouts::Kinematics);
        const auto GpuLife   = GpuBank.QueryChannel(ChannelLayouts::Lifespan);
        const auto GpuAppear = GpuBank.QueryChannel(ChannelLayouts::Appearance);

        uint32_t CpuAliveCount = 0u;
        double   PosErrSum     = 0.0;
        double   PosErrMax     = 0.0;
        double   VelErrSum     = 0.0;
        double   ColorErrMax   = 0.0;
        float    CpuPeakSpeed  = 0.0f;
        double   SumX = 0.0, SumY = 0.0, SumZ = 0.0;
        std::vector<double> PosErrors;
        PosErrors.reserve(ParticleCount);
        uint32_t WorstIdx = 0u;

        for (uint32_t Idx = 0u; Idx < ParticleCount; ++Idx)
        {
            const bool CpuLive = (CpuLife[Idx].LivenessMask & 1u) != 0u;
            const bool GpuLive = (GpuLife[Idx].LivenessMask & 1u) != 0u;
            if (!Require(CpuLive == GpuLive, "CPU vs GPU liveness mask diverged!"))
            {
                return 1;
            }
            if (!CpuLive)
            {
                continue;
            }
            ++CpuAliveCount;

            const double Dx   = static_cast<double>(CpuKin[Idx].PosX) - GpuKin[Idx].PosX;
            const double Dy   = static_cast<double>(CpuKin[Idx].PosY) - GpuKin[Idx].PosY;
            const double Dz   = static_cast<double>(CpuKin[Idx].PosZ) - GpuKin[Idx].PosZ;
            const double PErr = std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
            PosErrSum += PErr;
            PosErrors.push_back(PErr);
            if (PErr > PosErrMax)
            {
                PosErrMax = PErr;
                WorstIdx  = Idx;
            }

            const double Dvx = static_cast<double>(CpuKin[Idx].VelX) - GpuKin[Idx].VelX;
            const double Dvy = static_cast<double>(CpuKin[Idx].VelY) - GpuKin[Idx].VelY;
            const double Dvz = static_cast<double>(CpuKin[Idx].VelZ) - GpuKin[Idx].VelZ;
            VelErrSum += std::sqrt(Dvx * Dvx + Dvy * Dvy + Dvz * Dvz);

            const double Cr = std::abs(CpuAppear[Idx].ColorR - GpuAppear[Idx].ColorR);
            const double Cg = std::abs(CpuAppear[Idx].ColorG - GpuAppear[Idx].ColorG);
            const double Cb = std::abs(CpuAppear[Idx].ColorB - GpuAppear[Idx].ColorB);
            const double Rd = std::abs(CpuAppear[Idx].RadiusMeters - GpuAppear[Idx].RadiusMeters);
            ColorErrMax = std::max({ ColorErrMax, Cr, Cg, Cb, Rd });

            const float Speed = std::sqrt(
                CpuKin[Idx].VelX * CpuKin[Idx].VelX
                + CpuKin[Idx].VelY * CpuKin[Idx].VelY
                + CpuKin[Idx].VelZ * CpuKin[Idx].VelZ);
            CpuPeakSpeed = std::max(CpuPeakSpeed, Speed);
            SumX += CpuKin[Idx].PosX;
            SumY += CpuKin[Idx].PosY;
            SumZ += CpuKin[Idx].PosZ;
        }

        const double MeanPosErr = (CpuAliveCount > 0u) ? (PosErrSum / CpuAliveCount) : 0.0;
        const double MeanVelErr = (CpuAliveCount > 0u) ? (VelErrSum / CpuAliveCount) : 0.0;
        const double Cx = (CpuAliveCount > 0u) ? (SumX / CpuAliveCount) : 0.0;
        const double Cy = (CpuAliveCount > 0u) ? (SumY / CpuAliveCount) : 0.0;
        const double Cz = (CpuAliveCount > 0u) ? (SumZ / CpuAliveCount) : 0.0;

        float CpuBoundsR = 0.0f;
        for (uint32_t Idx = 0u; Idx < ParticleCount; ++Idx)
        {
            if ((CpuLife[Idx].LivenessMask & 1u) == 0u) { continue; }
            const float Dx = CpuKin[Idx].PosX - static_cast<float>(Cx);
            const float Dy = CpuKin[Idx].PosY - static_cast<float>(Cy);
            const float Dz = CpuKin[Idx].PosZ - static_cast<float>(Cz);
            CpuBoundsR = std::max(CpuBoundsR, std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz));
        }

        std::sort(PosErrors.begin(), PosErrors.end());
        const double MedianPosErr = PosErrors.empty() ? 0.0 : PosErrors[PosErrors.size() / 2u];
        const double P95PosErr    = PosErrors.empty() ? 0.0 : PosErrors[(PosErrors.size() * 95u) / 100u];
        std::cout << "[TEST 1] CPU-vs-GPU Parity (60 ticks, 3 ForceModels + Curl + Colliders): "
                  << "Alive=" << GpuSummary.AliveCount << "/" << CpuAliveCount
                  << " MedianPosErr=" << MedianPosErr << "m P95PosErr=" << P95PosErr << "m MaxPosErr=" << PosErrMax << "m (slot " << WorstIdx << ")"
                  << " MeanVelErr=" << MeanVelErr << "m/s ColorRadiusMaxErr=" << ColorErrMax
                  << " PeakSpeed(GPU/CPU)=" << GpuSummary.PeakSpeedMs << "/" << CpuPeakSpeed
                  << " BoundsR(GPU/CPU)=" << GpuSummary.BoundsRadiusMeters << "/" << CpuBoundsR << "\n";
        std::cout << "  Worst slot CPU=(" << CpuKin[WorstIdx].PosX << "," << CpuKin[WorstIdx].PosY << "," << CpuKin[WorstIdx].PosZ << ")"
                  << " GPU=(" << GpuKin[WorstIdx].PosX << "," << GpuKin[WorstIdx].PosY << "," << GpuKin[WorstIdx].PosZ << ")\n";

        if (!Require(CpuAliveCount > 200u && CpuAliveCount < ParticleCount, "Parity test must exercise both active & expired slots.") ||
            !Require(GpuSummary.AliveCount == CpuAliveCount, "32B GPU reduction AliveCount mismatch.") ||
            !Require(MedianPosErr < 1.0e-4, "Median CPU-vs-GPU position error exceeded 0.10 mm!") ||
            !Require(PosErrMax < 1.0e-3, "Max CPU-vs-GPU position error exceeded 1.0 mm!") ||
            !Require(MeanPosErr < 2.5e-4, "Mean CPU-vs-GPU position error exceeded 0.25 mm!") ||
            !Require(MeanVelErr < 1.0e-3, "Mean CPU-vs-GPU velocity error exceeded 1 mm/s!") ||
            !Require(ColorErrMax < 1.0e-3, "CPU-vs-GPU color/radius curve error exceeded 1e-3!") ||
            !Require(std::abs(GpuSummary.PeakSpeedMs - CpuPeakSpeed) < 5.0e-3f, "32B GPU PeakSpeedMs mismatch.") ||
            !Require(std::abs(GpuSummary.BoundsRadiusMeters - CpuBoundsR) < 5.0e-3f, "32B GPU BoundsRadiusMeters mismatch."))
        {
            return 1;
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    // TEST 2 — PURE-GPU 8,192-PARTICLE RELEASE + FORCEFIELDS VORTEX + 32 B REDUCTION + GPU VISUAL PROOF #1
    //--------------------------------------------------------------------------------------------------------------------
    {
        constexpr uint32_t CloudCapacity = 8192u;
        constexpr uint32_t SimTicks      = 68u;
        constexpr float    FixedDt       = 1.0f / 60.0f;

        ForceFieldAccumulator VortexScene;
        {
            ForceSourceRecordEntry Column{};
            Column.UniqueToken           = 10u;
            Column.Label                 = "HeroVortexColumn";
            Column.Model                 = ForceModel::RotationalColumn;
            Column.Strength              = 11.5f;
            Column.RadiusMeters          = 18.0f;
            Column.CoreRadiusMeters      = 1.6f;
            Column.SwirlRatio            = 0.88f;
            Column.CentreMeters          = { 0.0f, 0.0f, 2.2f };
            Column.EvaluatedCentreMeters = Column.CentreMeters;

            ForceSourceRecordEntry Updraft{};
            Updraft.UniqueToken           = 11u;
            Updraft.Label                 = "HeroThermal";
            Updraft.Model                 = ForceModel::FlowStream;
            Updraft.StreamProfile         = FlowStreamProfile::ThermalUpdraft;
            Updraft.Strength              = 4.6f;
            Updraft.RadiusMeters          = 12.0f;
            Updraft.CentreMeters          = { 0.0f, 0.0f, 0.5f };
            Updraft.EvaluatedCentreMeters = Updraft.CentreMeters;

            ForceSourceRecordEntry Curl{};
            Curl.UniqueToken           = 12u;
            Curl.Label                 = "HeroCurl";
            Curl.Model                 = ForceModel::CurlNoise;
            Curl.Strength              = 3.4f;
            Curl.RadiusMeters          = 24.0f;
            Curl.TurbulenceScaleMeters = 3.2f;
            Curl.CentreMeters          = { 0.0f, 0.0f, 2.0f };
            Curl.EvaluatedCentreMeters = Curl.CentreMeters;

            const std::array<ForceSourceRecordEntry, 3u> Rig = { Column, Updraft, Curl };
            VortexScene.AssignSources(Rig);
        }

        if (!Require(Executor.EnsureChannelCapacity(CloudCapacity, false, Diagnostic),
                     ("EnsureChannelCapacity(8192) failed: " + Diagnostic).c_str()))
        {
            return 1;
        }

        std::vector<ForceSourceShaderEntry> PackedSources;
        std::vector<SpatialVector3>         PackedWaypoints;
        ParticleReadbackSummary             Summary{};
        uint32_t                            ReleasedCursor = 0u;

        for (uint32_t Tick = 0u; Tick < SimTicks; ++Tick)
        {
            const uint32_t BatchSize = std::min<uint32_t>(120u, CloudCapacity - ReleasedCursor);
            const double   SimTime   = static_cast<double>(Tick) * FixedDt;
            VortexScene.StepAnimation(SimTime, false);
            GpuForcePacker.PackGpuSourceBuffers(VortexScene, SimTime, PackedSources, PackedWaypoints);

            ParticleGpuStepRequest Req{};
            Req.PackedForceSources           = PackedSources;
            Req.PackedForceWaypoints         = PackedWaypoints;
            Req.Uniforms.DeltaTimeSec        = FixedDt;
            Req.Uniforms.TimeSeconds         = static_cast<float>(SimTime);
            Req.Uniforms.ReleaseStartSlot    = ReleasedCursor;
            Req.Uniforms.ReleaseCount        = BatchSize;
            Req.Uniforms.ActiveCount         = ReleasedCursor + BatchSize;
            Req.Uniforms.Capacity            = CloudCapacity;
            Req.Uniforms.MasterSeed          = 0xF00DCAFEu;
            Req.Uniforms.GravityDragCurl     = { 0.16f, 0.45f, 0.22f, 1.35f };
            Req.Uniforms.CurlTimingBounds    = { 0.65f, 0.85f, 0.02f, 0.52f };
            Req.Uniforms.ForceCoupling       = { 1.35f, 0.85f, 0.4f, 0.12f };
            Req.Uniforms.ColliderFlags       = { 1u, 0u, 0u, Tick + 1u };
            Req.Uniforms.ColorStopA          = { 0.16f, 0.72f, 1.00f, 0.34f };
            Req.Uniforms.ColorStopC          = { 0.98f, 0.68f, 0.22f, 0.28f };
            Req.Uniforms.ColorStopB          = { 1.00f, 0.24f, 0.12f, 0.22f };
            Req.Uniforms.SizeSpinMetrics     = { 0.062f, 0.022f, 1.0f, 2.0f };
            Req.Uniforms.ColorDriverNorm     = { 11.5f, 6.0f, 0.0f, 0.9f };
            Req.Uniforms.ColorDriverMode     = { 1u, 0u, 2u, 0u }; // SpeedMagnitude + DiscVolume
            Req.Uniforms.ReleaseOriginRadius = { 0.0f, 0.0f, 0.25f, 2.2f };
            Req.Uniforms.ReleaseDirSpread    = { 0.0f, 0.0f, 1.0f, 0.65f };
            Req.Uniforms.ReleaseBoxExtents   = { 1.0f, 1.0f, 1.0f, 1.8f };
            Req.Uniforms.ReleaseSpeedLife    = { 4.8f, 2.2f, 4.5f, 1.0f };

            if (!Require(Executor.DispatchStep(Req, Summary, Diagnostic),
                         ("GPU 8192 step failed: " + Diagnostic).c_str()))
            {
                return 1;
            }
            ReleasedCursor += BatchSize;
        }

        std::cout << "[TEST 2] Pure-GPU 8192-Slot Release & ForceFields Vortex: "
                  << "Alive=" << Summary.AliveCount
                  << " PeakSpeed=" << Summary.PeakSpeedMs << "m/s"
                  << " BoundsR=" << Summary.BoundsRadiusMeters << "m"
                  << " TickUs=" << Summary.TickDurationUs << "us\n";

        if (!Require(Summary.AliveCount >= 7000u, "Pure-GPU 8192-slot cloud has unexpectedly low AliveCount.") ||
            !Require(std::isfinite(Summary.PeakSpeedMs) && Summary.PeakSpeedMs > 2.0f, "GPU PeakSpeedMs out of range.") ||
            !Require(std::isfinite(Summary.BoundsRadiusMeters) && Summary.BoundsRadiusMeters > 1.0f, "GPU BoundsRadiusMeters out of range."))
        {
            return 1;
        }

        ParticleViewportRenderRequest RenderReq{};
        RenderReq.WidthPx               = 1280u;
        RenderReq.HeightPx              = 720u;
        RenderReq.Camera.TargetMeters   = { 0.0f, 0.0f, 2.5f };
        RenderReq.Camera.YawRadians     = -0.78f;
        RenderReq.Camera.PitchRadians   = 0.34f;
        RenderReq.Camera.DistanceMeters = 13.8f;
        RenderReq.TimeSeconds           = static_cast<float>(SimTicks) * FixedDt;
        RenderReq.ActiveCount           = ReleasedCursor;
        RenderReq.Silhouette            = ParticleGpuSilhouette::SparkStreak;
        RenderReq.BlendMode             = ParticleGpuBlendMode::Additive;
        RenderReq.SizeScale             = 0.92f;
        RenderReq.EmissiveBoost         = 0.85f;
        RenderReq.ShowFloorGrid         = true;
        RenderReq.RenderStrands         = false;
        RenderReq.RenderBillboards      = true;

        if (!Require(Rasterizer.RenderOffscreen(RenderReq, Diagnostic),
                     ("GPU Visual Proof #1 rasterization failed: " + Diagnostic).c_str()))
        {
            return 1;
        }

        const auto Pixels = Rasterizer.QueryColorRgbaPixels();
        uint64_t BrightPixels = 0u;
        for (std::size_t Idx = 0u; Idx + 3u < Pixels.size(); Idx += 4u)
        {
            if (Pixels[Idx] > 90u || Pixels[Idx + 1u] > 90u || Pixels[Idx + 2u] > 90u)
            {
                ++BrightPixels;
            }
        }
        if (!Require(BrightPixels > 12000u, "GPU Visual Proof #1 rendered image is unexpectedly dark!"))
        {
            return 1;
        }
        WriteRgbaPng(ImageDir / "ParticleGpu_VulkanCompute_CoupledForceFields.png", 1280u, 720u, Pixels);
    }

    //--------------------------------------------------------------------------------------------------------------------
    // TEST 3 — GPU XPBD STRAND / WOVEN CLOTH CONSTRAINT SOLVER + GPU VISUAL PROOF #2
    //--------------------------------------------------------------------------------------------------------------------
    {
        constexpr uint32_t Cols     = 18u;
        constexpr uint32_t Rows     = 14u;
        constexpr uint32_t VertCnt  = Cols * Rows;
        constexpr float    Spacing  = 0.24f;
        constexpr uint32_t SimTicks = 60u;
        constexpr float    FixedDt  = 1.0f / 60.0f;

        ParticleChannelBank ClothBank;
        ClothBank.ConfigureCapacity(VertCnt, ChannelStoragePolicy::FixedSlotOrder);
        auto Kin    = ClothBank.RequireChannel(ChannelLayouts::Kinematics);
        auto Life   = ClothBank.RequireChannel(ChannelLayouts::Lifespan);
        auto Appear = ClothBank.RequireChannel(ChannelLayouts::Appearance);
        auto Anchor = ClothBank.RequireChannel(ChannelLayouts::StrandAnchor);
        auto Link   = ClothBank.RequireChannel(ChannelLayouts::StrandConnectivity);
        uint32_t BeginSlot = 0u;
        (void)ClothBank.AcquireSlots(VertCnt, BeginSlot);

        for (uint32_t Row = 0u; Row < Rows; ++Row)
        {
            for (uint32_t Col = 0u; Col < Cols; ++Col)
            {
                const uint32_t Idx = Row * Cols + Col;
                const float    X   = -1.8f + static_cast<float>(Col) * Spacing;
                const float    Y   = 0.0f;
                const float    Z   = 0.85f + static_cast<float>(Rows - 1u - Row) * Spacing;
                const bool     Pin = (Col == 0u);

                Kin[Idx].PosX        = X;
                Kin[Idx].PosY        = Y;
                Kin[Idx].PosZ        = Z;
                Kin[Idx].InverseMass = Pin ? 0.0f : 1.0f;
                Kin[Idx].VelX        = 0.0f;
                Kin[Idx].VelY        = 0.0f;
                Kin[Idx].VelZ        = 0.0f;
                Kin[Idx].DragScale   = 1.0f;

                Life[Idx].AgeSeconds     = 0.0f;
                Life[Idx].MaxLifeSeconds = 0.0f; // Immortal woven cloth
                Life[Idx].SeedUint32     = (Idx + 1u) * 2654435761u;
                Life[Idx].LivenessMask   = 1u;

                Anchor[Idx].PrevPosX         = X;
                Anchor[Idx].PrevPosY         = Y;
                Anchor[Idx].PrevPosZ         = Z;
                Anchor[Idx].RestLengthMeters = Spacing;
                Anchor[Idx].AnchorX          = X;
                Anchor[Idx].AnchorY          = Y;
                Anchor[Idx].AnchorZ          = Z;
                Anchor[Idx].PinWeight        = Pin ? 1.0f : 0.0f;

                Link[Idx].StrandIndex   = Row;
                Link[Idx].SegmentIndex  = Col;
                Link[Idx].LinkPrevSlot  = (Col > 0u) ? (Idx - 1u) : 0xFFFFFFFFu;
                Link[Idx].LinkCrossSlot = (Row > 0u) ? (Idx - Cols) : 0xFFFFFFFFu;

                const float U = static_cast<float>(Col) / static_cast<float>(Cols - 1u);
                const float V = static_cast<float>(Row) / static_cast<float>(Rows - 1u);
                Appear[Idx].ColorR       = 0.92f - 0.55f * U;
                Appear[Idx].ColorG       = 0.68f + 0.16f * V;
                Appear[Idx].ColorB       = 0.30f + 0.62f * U;
                Appear[Idx].ColorA       = 0.96f;
                Appear[Idx].RadiusMeters = 0.055f;
            }
        }

        if (!Require(Executor.UploadChannelBank(ClothBank, Diagnostic),
                     ("UploadChannelBank(Cloth) failed: " + Diagnostic).c_str()))
        {
            return 1;
        }

        ForceFieldAccumulator WindRig;
        {
            ForceSourceRecordEntry Wind{};
            Wind.UniqueToken           = 20u;
            Wind.Label                 = "BannerWind";
            Wind.Model                 = ForceModel::LinearWind;
            Wind.Strength              = 9.2f;
            Wind.EulerDegrees          = { 0.0f, -5.0f, 24.0f };
            Wind.RadiusMeters          = 32.0f;
            Wind.CentreMeters          = { 0.0f, 0.0f, 2.2f };
            Wind.EvaluatedCentreMeters = Wind.CentreMeters;

            ForceSourceRecordEntry Gust{};
            Gust.UniqueToken           = 21u;
            Gust.Label                 = "BannerGust";
            Gust.Model                 = ForceModel::AtmosphericGust;
            Gust.Strength              = 4.5f;
            Gust.EulerDegrees          = { 0.0f, -8.0f, 45.0f };
            Gust.RadiusMeters          = 28.0f;
            Gust.FrequencyHertz        = 0.75f;
            Gust.CentreMeters          = { 0.0f, 0.0f, 2.2f };
            Gust.EvaluatedCentreMeters = Gust.CentreMeters;

            const std::array<ForceSourceRecordEntry, 2u> Rig = { Wind, Gust };
            WindRig.AssignSources(Rig);
        }

        std::vector<ForceSourceShaderEntry> PackedSources;
        std::vector<SpatialVector3>         PackedWaypoints;
        ParticleReadbackSummary             ClothSummary{};
        const float                         CrossStiffness = 0.85f;
        uint32_t                            CrossStiffBits = 0u;
        std::memcpy(&CrossStiffBits, &CrossStiffness, sizeof(uint32_t));

        for (uint32_t Tick = 0u; Tick < SimTicks; ++Tick)
        {
            const double SimTime = static_cast<double>(Tick) * FixedDt;
            WindRig.StepAnimation(SimTime, false);
            GpuForcePacker.PackGpuSourceBuffers(WindRig, SimTime, PackedSources, PackedWaypoints);

            ParticleGpuStepRequest Req{};
            Req.PackedForceSources          = PackedSources;
            Req.PackedForceWaypoints        = PackedWaypoints;
            Req.StrandRelaxIterations       = 6u;
            Req.Uniforms.DeltaTimeSec       = FixedDt;
            Req.Uniforms.TimeSeconds        = static_cast<float>(SimTime);
            Req.Uniforms.ActiveCount        = VertCnt;
            Req.Uniforms.Capacity           = VertCnt;
            Req.Uniforms.GravityDragCurl    = { 0.45f, 0.0f, 0.32f, 0.65f };
            Req.Uniforms.CurlTimingBounds   = { 1.1f, 0.9f, 0.05f, 0.2f };
            Req.Uniforms.ForceCoupling      = { 1.85f, 1.0f, 0.35f, 0.15f };
            Req.Uniforms.SphereCenterRadius = { 0.35f, 0.35f, 2.25f, 0.68f };
            Req.Uniforms.SphereVelBounce    = { 0.0f, 0.0f, 0.0f, 0.25f };
            Req.Uniforms.ColliderFlags      = { 1u, 1u, 0u, Tick + 1u };
            Req.Uniforms.ColorStopA         = { 0.94f, 0.72f, 0.34f, 0.98f };
            Req.Uniforms.ColorStopC         = { 0.46f, 0.78f, 0.88f, 0.96f };
            Req.Uniforms.ColorStopB         = { 0.24f, 0.42f, 0.88f, 0.95f };
            Req.Uniforms.SizeSpinMetrics    = { 0.06f, 0.06f, 1.0f, 0.0f };
            Req.Uniforms.ColorDriverNorm    = { 9.0f, 4.5f, 0.0f, 0.92f };
            Req.Uniforms.ColorDriverMode    = { 2u, 0u, 0u, CrossStiffBits }; // HeightAltitude ramp

            if (!Require(Executor.DispatchStep(Req, ClothSummary, Diagnostic),
                         ("GPU XPBD cloth step failed: " + Diagnostic).c_str()))
            {
                return 1;
            }
        }

        if (!Require(Executor.DownloadChannelBank(ClothBank, Diagnostic),
                     ("DownloadChannelBank(Cloth) failed: " + Diagnostic).c_str()))
        {
            return 1;
        }

        const auto FinalKin = ClothBank.QueryChannel(ChannelLayouts::Kinematics);
        const auto FinalAnc = ClothBank.QueryChannel(ChannelLayouts::StrandAnchor);
        float MaxPinDrift   = 0.0f;
        float MaxSegRatio   = 0.0f;
        float MeanTipDispY  = 0.0f;

        for (uint32_t Row = 0u; Row < Rows; ++Row)
        {
            for (uint32_t Col = 0u; Col < Cols; ++Col)
            {
                const uint32_t Idx = Row * Cols + Col;
                if (!Require(std::isfinite(FinalKin[Idx].PosX) && std::isfinite(FinalKin[Idx].PosY) && std::isfinite(FinalKin[Idx].PosZ),
                             "GPU XPBD cloth produced NaN/Inf position!"))
                {
                    return 1;
                }
                if (Col == 0u)
                {
                    const float Dx = FinalKin[Idx].PosX - FinalAnc[Idx].AnchorX;
                    const float Dy = FinalKin[Idx].PosY - FinalAnc[Idx].AnchorY;
                    const float Dz = FinalKin[Idx].PosZ - FinalAnc[Idx].AnchorZ;
                    MaxPinDrift = std::max(MaxPinDrift, std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz));
                }
                else
                {
                    const uint32_t Prev = Idx - 1u;
                    const float Dx = FinalKin[Idx].PosX - FinalKin[Prev].PosX;
                    const float Dy = FinalKin[Idx].PosY - FinalKin[Prev].PosY;
                    const float Dz = FinalKin[Idx].PosZ - FinalKin[Prev].PosZ;
                    MaxSegRatio = std::max(MaxSegRatio, std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz) / Spacing);
                }
                if (Col == Cols - 1u)
                {
                    MeanTipDispY += FinalKin[Idx].PosY;
                }
            }
        }
        MeanTipDispY /= static_cast<float>(Rows);

        std::cout << "[TEST 3] GPU XPBD Cloth Banner (" << VertCnt << " verts, 6 iters/tick): "
                  << "Alive=" << ClothSummary.AliveCount
                  << " MaxPinDrift=" << MaxPinDrift << "m"
                  << " MaxSegRatio=" << MaxSegRatio << "x"
                  << " TipWindDeflectionY=" << MeanTipDispY << "m\n";

        if (!Require(ClothSummary.AliveCount == VertCnt, "Cloth AliveCount mismatch.") ||
            !Require(MaxPinDrift < 1.0e-4f, "Pinned mast vertices drifted!") ||
            !Require(MaxSegRatio < 1.45f, "XPBD cloth segments over-stretched!") ||
            !Require(std::abs(MeanTipDispY) > 0.35f, "XPBD cloth failed to billow under wind!"))
        {
            return 1;
        }

        ParticleViewportRenderRequest ClothRender{};
        ClothRender.WidthPx               = 1280u;
        ClothRender.HeightPx              = 720u;
        ClothRender.Camera.TargetMeters   = { 0.0f, -0.45f, 2.35f };
        ClothRender.Camera.YawRadians     = -1.12f;
        ClothRender.Camera.PitchRadians   = 0.24f;
        ClothRender.Camera.DistanceMeters = 7.4f;
        ClothRender.TimeSeconds           = static_cast<float>(SimTicks) * FixedDt;
        ClothRender.ActiveCount           = VertCnt;
        ClothRender.Silhouette            = ParticleGpuSilhouette::GlowOrb;
        ClothRender.BlendMode             = ParticleGpuBlendMode::Alpha;
        ClothRender.SizeScale             = 0.65f;
        ClothRender.EmissiveBoost         = 1.18f;
        ClothRender.RibbonWidthMeters     = 0.052f;
        ClothRender.ShowFloorGrid         = true;
        ClothRender.RenderStrands         = true;
        ClothRender.RenderBillboards      = true;

        if (!Require(Rasterizer.RenderOffscreen(ClothRender, Diagnostic),
                     ("GPU Visual Proof #2 rasterization failed: " + Diagnostic).c_str()))
        {
            return 1;
        }

        const auto Pixels = Rasterizer.QueryColorRgbaPixels();
        uint64_t ClothPixels = 0u;
        for (std::size_t Idx = 0u; Idx + 3u < Pixels.size(); Idx += 4u)
        {
            if (Pixels[Idx + 2u] > 100u)
            {
                ++ClothPixels;
            }
        }
        if (!Require(ClothPixels > 20000u, "GPU Visual Proof #2 (XPBD Cloth Ribbons) has too few rendered cloth ribbon pixels."))
        {
            return 1;
        }
        WriteRgbaPng(ImageDir / "ParticleGpu_VulkanCompute_XpbdClothRibbons.png", 1280u, 720u, Pixels);
    }

    Rasterizer.Finalize();
    Executor.Finalize();

    std::cout << "[ParticleVulkanGpuValidation] PASS — Vulkan 1.3 .slang compute & graphics verified (CPU-vs-GPU parity, 8192-slot reduction, XPBD cloth, 2 GPU PNG proofs written).\n";
    return 0;
}
