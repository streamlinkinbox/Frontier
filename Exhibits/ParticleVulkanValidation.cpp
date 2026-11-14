/*============================================================================================================================================
                                                       PARTICLEVULKANVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Phase 4 Vulkan 1.3 Compute + Raster GPU validation rendering all visual carriers into 04-VulkanGpuCarriersContactSheet.png

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/ParticleSpace/Execution/ParticleVulkanExecutor.h"
#include "Engine/ParticleSpace/ParticleDocument.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

using namespace Frontier::ParticleSpace;
using namespace Frontier::ParticleSpace::Execution;
using Frontier::Components::RgbaColor8;
using Frontier::Exhibits::ResolveRepositoryRootPath;
using Frontier::Exhibits::SoftwareRasterCanvas;

bool VerifyCheck(bool Condition, const char* Message) noexcept
{
    if (!Condition)
    {
        std::fprintf(stderr, "[ParticleVulkanValidation] FAILED: %s\n", Message);
        return false;
    }
    return true;
}

} // namespace

int main()
{
    bool AllPassed = true;
    const std::filesystem::path RepoRoot = ResolveRepositoryRootPath();

    Frontier::GeometricRaster::VulkanInterchange Device;
    std::string VkDiag;
    if (!Device.InitializeInterchange(RepoRoot, &VkDiag))
    {
        std::fprintf(stderr, "[ParticleVulkanValidation] Vulkan init failed: %s\n", VkDiag.c_str());
        return 1;
    }

    constexpr uint32_t TileW = 480u;
    constexpr uint32_t TileH = 360u;
    SoftwareRasterCanvas Sheet(TileW * 3u, TileH * 2u, RgbaColor8{ 10u, 12u, 18u, 255u });

    auto MakeCaseDoc = [](auto&& ConfigureFn) -> ParticleDocument
    {
        ParticleDocument Doc;
        Doc.PopulateDefaultSparksRig();
        Doc.ViewportCamera.EyePositionMeters = { 0.0f, -6.2f, 2.5f };
        Doc.ViewportCamera.TargetMeters      = { 0.0f,  0.0f, 1.3f };
        Doc.Rigs[0].Sizing.CapacitySlots     = 800u;
        ConfigureFn(Doc, Doc.Rigs[0]);
        Doc.RebuildAllRigCaches();
        return Doc;
    };

    std::vector<ParticleDocument> Cases;

    // 1. Billboard Additive + ForceColumn + Interactive Chrome Ball
    Cases.push_back(MakeCaseDoc([](ParticleDocument& D, ParticleRig& R)
    {
        D.Collider.BallRadiusMeters   = 0.55f;
        D.Collider.BallPositionMeters = { 0.8f, 0.0f, 1.5f };
        R.Sizing.ReleaseRatePerSec    = 650.0f;
        R.Sizing.SizeStartMeters      = 0.14f;
        R.Sizing.SizeEndMeters        = 0.04f;
        Frontier::SimulationSpace::ForceFields::ForceSourceRecordEntry Col{};
        Col.UniqueToken           = 1u;
        Col.Model                 = Frontier::SimulationSpace::ForceFields::ForceModel::RotationalColumn;
        Col.CentreMeters          = { 0.0f, 0.0f, 1.5f, 0.0f };
        Col.EvaluatedCentreMeters = Col.CentreMeters;
        Col.RadiusMeters          = 8.0f;
        Col.Strength              = 6.5f;
        Col.Enabled               = true;
        D.ForceSources.push_back(Col);
    }));

    // 2. Ribbon Strand Carrier (Hanging Cloth Curtain)
    Cases.push_back(MakeCaseDoc([](ParticleDocument& D, ParticleRig& R)
    {
        D.AmbientWindMps = { 1.8f, 0.6f, 0.0f };
        R.Origin = ClothGridOriginSpec{ { 0.0f, 0.0f, 2.7f }, 20u, 14u, 3.0f, 2.4f };
        R.Carrier = RibbonStrandCarrier{};
        R.Dynamics.Strands.Mode = StrandDynamicsMode::FlagSail;
        R.Dynamics.Strands.WaveSpeedHz = 2.0f;
    }));

    // 3. Oriented Solid Carrier (MoGraph 3D Cubes)
    Cases.push_back(MakeCaseDoc([](ParticleDocument&, ParticleRig& R)
    {
        R.Sizing.CapacitySlots = 256u;
        R.Carrier = OrientedSolidCarrier{};
        R.Dynamics.EffectorGrid.Active = true;
        R.Dynamics.EffectorGrid.Columns = 16u;
        R.Dynamics.EffectorGrid.Rows = 16u;
        R.Dynamics.EffectorGrid.Driver = MoGraphEffectorDriver::ExpandingPulseRing;
        R.Dynamics.EffectorGrid.MaxHeightMeters = 1.2f;
        Frontier::ColourSpace::LinearFillSource Fill{};
        Fill.PrimaryLinear   = { 0.14f, 0.58f, 0.98f, 1.0f };
        Fill.SecondaryLinear = { 0.98f, 0.25f, 0.72f, 1.0f };
        R.TintBinding.SourceProfile.Source = Fill;
    }));

    // 4. Winged Creature Carrier (Boids Flock)
    Cases.push_back(MakeCaseDoc([](ParticleDocument& D, ParticleRig& R)
    {
        D.Collider.BallRadiusMeters   = 0.65f;
        D.Collider.BallPositionMeters = { 0.7f, 0.0f, 1.8f };
        R.Sizing.CapacitySlots = 320u;
        R.Sizing.SizeStartMeters = 0.18f;
        R.Origin = SphereVolumeOriginSpec{ { 0.0f, 0.0f, 1.8f }, 1.6f, false };
        R.Carrier = WingedCreatureCarrier{};
        R.Dynamics.Family = ParticleSimulationFamily::BoidsFlock;
        R.Dynamics.Boids.Active = true;
        Frontier::ColourSpace::LinearFillSource Fill{};
        Fill.PrimaryLinear   = { 0.95f, 0.85f, 0.28f, 1.0f };
        Fill.SecondaryLinear = { 0.35f, 0.88f, 1.00f, 1.0f };
        R.TintBinding.SourceProfile.Source = Fill;
    }));

    // 5. Dielectric Beam Carrier (Tesla Arc)
    Cases.push_back(MakeCaseDoc([](ParticleDocument& D, ParticleRig& R)
    {
        D.Collider.BallRadiusMeters   = 0.45f;
        D.Collider.BallPositionMeters = { 1.8f, 0.0f, 1.6f };
        R.Origin = PointOriginSpec{ { -1.8f, 0.0f, 1.6f } };
        R.Sizing.CapacitySlots = 96u;
        R.Carrier = DielectricBeamCarrier{};
        R.Dynamics.Pyro.BeamModeActive = 1.0f;
        Frontier::ColourSpace::LinearFillSource Fill{};
        Fill.PrimaryLinear   = { 0.35f, 0.88f, 1.0f, 1.0f };
        Fill.SecondaryLinear = { 0.78f, 0.42f, 1.0f, 1.0f };
        R.TintBinding.SourceProfile.Source = Fill;
    }));

    // 6. Refraction Lens Carrier (Black Hole Einstein Ring)
    Cases.push_back(MakeCaseDoc([](ParticleDocument&, ParticleRig& R)
    {
        R.Origin = PointOriginSpec{ { 0.0f, 0.0f, 1.5f } };
        R.Carrier = RefractionLensCarrier{};
        R.Dynamics.BlackHole.Enabled = true;
        R.Dynamics.BlackHole.EinsteinGain = 3.0f;
        Frontier::ColourSpace::LinearFillSource Fill{};
        Fill.PrimaryLinear   = { 1.0f, 0.65f, 0.18f, 1.0f };
        Fill.SecondaryLinear = { 0.88f, 0.22f, 0.08f, 1.0f };
        R.TintBinding.SourceProfile.Source = Fill;
    }));

    ParticleVulkanExecutor Executor;
    for (std::size_t Idx = 0u; Idx < Cases.size(); ++Idx)
    {
        if (Idx == 0u)
        {
            AllPassed &= VerifyCheck(Executor.InitializeExecutor(&Device, Cases[Idx]), "Failed to initialize ParticleVulkanExecutor");
        }
        else
        {
            AllPassed &= VerifyCheck(Executor.ResetDocument(Cases[Idx]), "Failed to reset ParticleDocument");
        }

        for (int Step = 0; Step < 35; ++Step)
        {
            const ParticleRenderViewportSpec VpSpec{ TileW, TileH, true, true, (Step == 34) };
            AllPassed &= VerifyCheck(Executor.AdvanceRenderStep(Cases[Idx], 1.0f / 60.0f, VpSpec), "AdvanceRenderStep failed");
        }

        const auto Pixels  = Executor.InspectRenderedRgba8();
        const auto Metrics = Executor.QueryMetrics();
        AllPassed &= VerifyCheck(Pixels.size() == static_cast<std::size_t>(TileW) * TileH * 4u, "Captured pixel buffer size mismatch");
        AllPassed &= VerifyCheck(Metrics.GpuComputeActive, "Metrics did not report GPU compute active");
        std::printf(
            "  Case %zu: alive=%u min=(%.2f,%.2f,%.2f) max=(%.2f,%.2f,%.2f) step=%.2fms\n",
            Idx,
            Metrics.AliveParticles,
            Metrics.BoundsMinMeters.X, Metrics.BoundsMinMeters.Y, Metrics.BoundsMinMeters.Z,
            Metrics.BoundsMaxMeters.X, Metrics.BoundsMaxMeters.Y, Metrics.BoundsMaxMeters.Z,
            Metrics.SmoothedStepMs);

        // Verify non-empty rendered pixels
        uint64_t SumLum = 0ull;
        for (std::size_t P = 0u; P + 3u < Pixels.size(); P += 4u)
        {
            SumLum += Pixels[P + 0u] + Pixels[P + 1u] + Pixels[P + 2u];
        }
        AllPassed &= VerifyCheck(SumLum > 100000ull, "Rendered carrier frame appears blank");

        Sheet.BlitRgbaRegion(
            static_cast<uint32_t>(Idx % 3u) * TileW,
            static_cast<uint32_t>(Idx / 3u) * TileH,
            TileW,
            TileH,
            Pixels.data());
    }

    const std::filesystem::path OutPng = RepoRoot / "Exhibits/Images/Particles/04-VulkanGpuCarriersContactSheet.png";
    AllPassed &= VerifyCheck(Sheet.WritePngFile(OutPng), "Failed to write 04-VulkanGpuCarriersContactSheet.png");

    const auto FinalMetrics = Executor.QueryMetrics();
    Executor.TeardownExecutor();
    Device.TeardownInterchange();

    if (!AllPassed)
    {
        return 1;
    }

    std::printf(
        "[ParticleVulkanValidation] All Phase 4 GPU checks passed on '%s' (last step %.2f ms, 6 carriers rendered).\n",
        std::string(Device.QueryAdapterName()).c_str(),
        FinalMetrics.SmoothedStepMs);
    return 0;
}
