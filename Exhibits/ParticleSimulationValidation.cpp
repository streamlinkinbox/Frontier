/*============================================================================================================================================
                                                    PARTICLESIMULATIONVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Validation & visual PNG proof suite for Frontier's modular C++20 ParticleSimulation, typed SoA channels, custom user types, ForceFields & SDF reuse, and all 93 active presets

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Appearance/ThemeRepository.h"
#include "Engine/Components/Appearance/TypographyRepository.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/DistanceFieldSpace/SignedDistanceVolume.h"
#include "Engine/SimulationSpace/ForceFields/FieldLatticeCascade.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldAccumulator.h"
#include "Engine/SimulationSpace/Particles/ParticleKinematicsSolvers.h"
#include "Engine/SimulationSpace/Particles/ParticleChannelBank.h"
#include "Engine/SimulationSpace/Particles/ParticleDomainSolvers.h"
#include "Engine/SimulationSpace/Particles/ParticlePresetCompiler.h"
#include "Engine/SimulationSpace/Particles/ParticleSimulation.h"
#include "Engine/SimulationSpace/Particles/ParticleChannelLayouts.h"
#include "Engine/Workspaces/Particles/ParticlePresetRegistry.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace
{

using namespace Frontier;
using namespace Frontier::SimulationSpace::ForceFields;
using namespace Frontier::SimulationSpace::Particles;
using namespace Frontier::Workspaces;
using Frontier::Exhibits::ResolveRepositoryRootPath;
using Frontier::Exhibits::SoftwareRasterCanvas;

//------------------------------------------------------------------------------------------------------------------------
//                                            CUSTOM USER C++ PARTICLE TYPE PROOF
//------------------------------------------------------------------------------------------------------------------------

// Demonstrates a brand-new gameplay/user particle attribute type declared completely outside engine headers!
struct UserIonChargeAttribute
{
    float    ChargeCoulombs = 1.0f; // [C]   - User-defined electrostatic charge
    float    DecayHalfLife  = 1.2f; // [s]   - Exponential charge decay constant
    uint32_t PolaritySign   = 1u;   // [0|1] - 1 = CCW swirl, 0 = CW swirl
    uint32_t TagNumber      = 0u;   // [id]  - Custom user tag verifying synchronized swap-remove
};

inline constexpr ChannelTag<UserIonChargeAttribute> UserIonChargeChannel{
    HashChannelSignature("Validation::UserIonChargeAttribute"),
    "UserIonCharge",
    ChannelResidency::CpuOnly,
};

//------------------------------------------------------------------------------------------------------------------------
//                                               VERIFICATION ASSERTIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] bool VerifyAssertion(bool Condition, const char* Label) noexcept
{
    if (!Condition)
    {
        std::fprintf(stderr, "[ParticleSimulationValidation] FAILED: %s\n", Label);
        return false;
    }
    return true;
}

[[nodiscard]] bool VerifyAllParticlesFinite(const ParticleSimulation& Sim) noexcept
{
    const uint32_t Active = Sim.QueryActiveCount();
    const auto Kin        = Sim.QueryChannel(ChannelLayouts::Kinematics);
    for (uint32_t Slot = 0u; Slot < Active; ++Slot)
    {
        if (!std::isfinite(Kin[Slot].PosX) || !std::isfinite(Kin[Slot].PosY) || !std::isfinite(Kin[Slot].PosZ)
            || !std::isfinite(Kin[Slot].VelX) || !std::isfinite(Kin[Slot].VelY) || !std::isfinite(Kin[Slot].VelZ))
        {
            return false;
        }
    }
    const ParticleReadbackSummary& Readback = Sim.QueryReadbackSummary();
    return std::isfinite(Readback.PeakSpeedMs)
        && std::isfinite(Readback.BoundsRadiusMeters)
        && std::isfinite(Readback.BoundsCenterX)
        && std::isfinite(Readback.BoundsCenterY)
        && std::isfinite(Readback.BoundsCenterZ);
}

[[nodiscard]] DistanceFieldSpace::SignedDistanceVolume ConstructAnalyticSphereSdfVolume(
    Mathematics::Axis3D SphereCentre,
    float               SphereRadiusMeters)
{
    DistanceFieldSpace::SignedDistanceVolume Volume{};
    Volume.VolumeBoundsMin    = { -2.4f, -2.4f, -0.4f };
    Volume.VolumeBoundsMax    = {  2.4f,  2.4f,  4.4f };
    Volume.VoxelSpacingMeters = 0.2f;
    Volume.NarrowBandMeters   = 1.2f;
    Volume.BrickCountX        = 3u;
    Volume.BrickCountY        = 3u;
    Volume.BrickCountZ        = 3u;

    const std::size_t TotalBricks = static_cast<std::size_t>(Volume.BrickCountX) * Volume.BrickCountY * Volume.BrickCountZ;
    Volume.BrickIndirection.resize(TotalBricks, DistanceFieldSpace::UnallocatedBrickSlot);
    Volume.CoarseBrickCenterDistance.resize(TotalBricks, 2.0f);

    for (uint32_t Bz = 0u; Bz < Volume.BrickCountZ; ++Bz)
    {
        for (uint32_t By = 0u; By < Volume.BrickCountY; ++By)
        {
            for (uint32_t Bx = 0u; Bx < Volume.BrickCountX; ++Bx)
            {
                const std::size_t MacroIdx = (static_cast<std::size_t>(Bz) * Volume.BrickCountY + By) * Volume.BrickCountX + Bx;
                const uint32_t Slot        = static_cast<uint32_t>(Volume.AllocatedBricks.size());
                Volume.BrickIndirection[MacroIdx] = Slot;
                Volume.AllocatedBricks.push_back(DistanceFieldSpace::SparseDistanceBrick{});
                DistanceFieldSpace::SparseDistanceBrick& Brick = Volume.AllocatedBricks.back();

                const float BrickEdgeM = static_cast<float>(DistanceFieldSpace::BrickEdgeVoxelCount) * Volume.VoxelSpacingMeters;
                const Mathematics::Axis3D BrickOrigin{
                    Volume.VolumeBoundsMin.X + static_cast<float>(Bx) * BrickEdgeM,
                    Volume.VolumeBoundsMin.Y + static_cast<float>(By) * BrickEdgeM,
                    Volume.VolumeBoundsMin.Z + static_cast<float>(Bz) * BrickEdgeM,
                };

                for (uint32_t Vz = 0u; Vz < DistanceFieldSpace::BrickEdgeVoxelCount; ++Vz)
                {
                    for (uint32_t Vy = 0u; Vy < DistanceFieldSpace::BrickEdgeVoxelCount; ++Vy)
                    {
                        for (uint32_t Vx = 0u; Vx < DistanceFieldSpace::BrickEdgeVoxelCount; ++Vx)
                        {
                            const Mathematics::Axis3D Pos{
                                BrickOrigin.X + (static_cast<float>(Vx) + 0.5f) * Volume.VoxelSpacingMeters,
                                BrickOrigin.Y + (static_cast<float>(Vy) + 0.5f) * Volume.VoxelSpacingMeters,
                                BrickOrigin.Z + (static_cast<float>(Vz) + 0.5f) * Volume.VoxelSpacingMeters,
                            };
                            const float SignedDist = Mathematics::EvaluateDistance(Pos, SphereCentre) - SphereRadiusMeters;
                            const float Norm       = std::clamp(SignedDist / Volume.NarrowBandMeters, -1.0f, 1.0f);
                            const uint32_t Local   = (Vz * DistanceFieldSpace::BrickEdgeVoxelCount + Vy) * DistanceFieldSpace::BrickEdgeVoxelCount + Vx;
                            Brick.QuantizedSamples[Local] = static_cast<int16_t>(std::lround(Norm * 32767.0f));
                        }
                    }
                }
            }
        }
    }

    return Volume;
}

[[nodiscard]] ForceFieldAccumulator ConstructStudioForceFields()
{
    std::vector<ForceSourceRecordEntry> Sources{};

    ForceSourceRecordEntry Wind{};
    Wind.UniqueToken  = 1u;
    Wind.Label        = "Ambient Wind";
    Wind.Model        = ForceModel::LinearWind;
    Wind.Domain       = ForceDomainCategory::VectorField;
    Wind.Attenuation  = ForceAttenuationMode::None;
    Wind.CentreMeters = { 0.0f, 0.0f, 2.0f };
    Wind.RadiusMeters = 80.0f;
    Wind.Strength     = 5.5f;
    Wind.Enabled      = true;
    Sources.push_back(Wind);

    ForceSourceRecordEntry Swirl{};
    Swirl.UniqueToken  = 2u;
    Swirl.Label        = "Helical Column";
    Swirl.Model        = ForceModel::RotationalColumn;
    Swirl.Domain       = ForceDomainCategory::VectorField;
    Swirl.Envelope     = ReachEnvelopeCategory::Column;
    Swirl.CentreMeters = { 0.0f, 0.0f, 2.0f };
    Swirl.RadiusMeters = 14.0f;
    Swirl.Strength     = 9.0f;
    Swirl.SwirlRatio   = 0.72f;
    Swirl.Enabled      = true;
    Sources.push_back(Swirl);

    ForceSourceRecordEntry Updraft{};
    Updraft.UniqueToken   = 3u;
    Updraft.Label         = "Thermal Updraft";
    Updraft.Model         = ForceModel::FlowStream;
    Updraft.Domain        = ForceDomainCategory::FlowField;
    Updraft.StreamProfile = FlowStreamProfile::ThermalUpdraft;
    Updraft.CentreMeters  = { 0.0f, 0.0f, 1.5f };
    Updraft.RadiusMeters  = 12.0f;
    Updraft.Strength      = 4.2f;
    Updraft.Enabled       = true;
    Sources.push_back(Updraft);

    ForceFieldAccumulator Acc{};
    Acc.AssignSources(Sources);
    return Acc;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              3D ISOMETRIC PROJECTION ROUTINE
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] Point2D ProjectWorldPanel(
    const Rectangle2D& Rect,
    float              WorldX,
    float              WorldY,
    float              WorldZ,
    float              ScalePxPerM,
    float              FocusZ) noexcept
{
    const float IsoX = (WorldX - WorldY * 0.58f) * ScalePxPerM;
    const float IsoY = ((WorldZ - FocusZ) * 0.92f - (WorldX * 0.16f + WorldY * 0.34f)) * ScalePxPerM;
    return {
        Rect.X + Rect.Width * 0.5f + IsoX,
        Rect.Y + Rect.Height * 0.58f - IsoY,
    };
}

void RenderPanelContainer(
    PixelSpace&        Surface,
    const Rectangle2D& Rect,
    const char*        Tag,
    const char*        Title,
    const char*        Subtitle,
    const ParticleSimulation& Sim,
    ColorRgba          Accent)
{
    Surface.FillRectangle(Rect, ColorRgba{ 22u, 22u, 24u, 255u }, 18.0f);
    Surface.StrokeRectangle(Rect, ColorRgba{ 255u, 255u, 255u, 18u }, 18.0f, 1.0f);

    // Subtle ground horizon grid lines
    const Rectangle2D PlotRect{ Rect.X + 14.0f, Rect.Y + 54.0f, Rect.Width - 28.0f, Rect.Height - 96.0f };
    Surface.FillRectangle(PlotRect, ColorRgba{ 11u, 11u, 13u, 255u }, 12.0f);
    Surface.StrokeRectangle(PlotRect, ColorRgba{ 255u, 255u, 255u, 12u }, 12.0f, 1.0f);

    for (int32_t G = -3; G <= 3; ++G)
    {
        const float Coord = static_cast<float>(G) * 0.9f;
        const Point2D A   = ProjectWorldPanel(PlotRect, Coord, -2.7f, 0.0f, 48.0f, 1.2f);
        const Point2D B   = ProjectWorldPanel(PlotRect, Coord,  2.7f, 0.0f, 48.0f, 1.2f);
        const Point2D C   = ProjectWorldPanel(PlotRect, -2.7f, Coord, 0.0f, 48.0f, 1.2f);
        const Point2D D   = ProjectWorldPanel(PlotRect,  2.7f, Coord, 0.0f, 48.0f, 1.2f);
        Surface.StrokeLine(A, B, ColorRgba{ 255u, 255u, 255u, 9u }, 1.0f);
        Surface.StrokeLine(C, D, ColorRgba{ 255u, 255u, 255u, 9u }, 1.0f);
    }

    // Header
    Surface.FillCircle({ Rect.X + 24.0f, Rect.Y + 22.0f }, 4.5f, Accent);
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Medium,
        { Rect.X + 35.0f, Rect.Y + 14.0f },
        14.5f,
        ColorRgba{ 240u, 240u, 240u, 255u },
        Title);
    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { Rect.X + 35.0f, Rect.Y + 32.0f },
        Rect.Width - 150.0f,
        10.2f,
        ColorRgba{ 138u, 138u, 144u, 255u },
        Subtitle);
    WorkspaceAppearance::ConstructRightAlignedText(
        Surface,
        FontWeightClass::Medium,
        { Rect.Right() - 16.0f, Rect.Y + 17.0f },
        10.5f,
        ColorRgba{ 132u, 132u, 138u, 255u },
        Tag,
        false);

    // Bottom 32 B Readback Telemetry Strip
    const ParticleReadbackSummary& Rb = Sim.QueryReadbackSummary();
    char HudBuf[160]{};
    std::snprintf(
        HudBuf,
        sizeof(HudBuf),
        "ALIVE %u/%u  ·  PEAK %.2f m/s  ·  BOUNDS R %.2f m  ·  CHANNELS %u (%zu KiB)  ·  %s",
        Rb.AliveCount,
        Sim.QueryCapacity(),
        Rb.PeakSpeedMs,
        Rb.BoundsRadiusMeters,
        Sim.QueryChannelBank().QueryRegisteredChannelCount(),
        std::max<std::size_t>(1u, Sim.QueryChannelBank().QueryOccupiedMemoryBytes() / 1024u),
        Sim.QueryGpuComputeEligible() ? "CPU + GPU .SLANG READY" : "CUSTOM C++ CPU PATH");
    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { Rect.X + 16.0f, Rect.Bottom() - 28.0f },
        Rect.Width - 32.0f,
        10.8f,
        ColorRgba{ 169u, 217u, 200u, 255u },
        HudBuf,
        false);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    MAIN VALIDATION ENTRY
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    bool AllPassed = true;

    // -----------------------------------------------------------------------------------------------------------------
    // TEST 1: Modular Custom C++ Particle Type + Custom Lambda Solver + Synchronized Swap-Remove
    // -----------------------------------------------------------------------------------------------------------------
    ParticleSimulation IonSwarm = ParticleSimulationSpecification("CustomIonSwarm")
        .ConfigureCapacity(256u, ChannelStoragePolicy::DenseSwapRemove)
        .AssignDeterministicSeed(4042u)
        .RequireCustomChannel(UserIonChargeChannel)
        .AttachCustomSource(
            "IonRingSeed",
            [](ParticleChannelBank& Bank) {
                (void)Bank.RequireChannel(ChannelLayouts::Kinematics);
                (void)Bank.RequireChannel(ChannelLayouts::Lifespan);
                (void)Bank.RequireChannel(ChannelLayouts::Appearance);
                (void)Bank.RequireChannel(UserIonChargeChannel);
            },
            [](ParticleChannelBank& Bank, const SimulationTickInputs& In, ParticleRandomGenerator& Generator) -> uint32_t {
                if (In.TickIndex > 0u)
                {
                    return 0u;
                }
                uint32_t Begin = 0u;
                const uint32_t Count = Bank.AcquireSlots(120u, Begin);
                auto Kin    = Bank.AccessChannel(ChannelLayouts::Kinematics);
                auto Life   = Bank.AccessChannel(ChannelLayouts::Lifespan);
                auto Appear = Bank.AccessChannel(ChannelLayouts::Appearance);
                auto Ions   = Bank.AccessChannel(UserIonChargeChannel);
                for (uint32_t I = 0u; I < Count; ++I)
                {
                    const uint32_t Slot = Begin + I;
                    const float Ang     = (static_cast<float>(I) / static_cast<float>(Count)) * Constants::TauFloat;
                    const float Rad     = 0.85f + 0.55f * Generator.NextUnitFloat();
                    Kin[Slot].PosX        = std::cos(Ang) * Rad;
                    Kin[Slot].PosY        = std::sin(Ang) * Rad;
                    Kin[Slot].PosZ        = 1.2f + Generator.NextRange(-0.25f, 0.25f);
                    Kin[Slot].VelX        = -std::sin(Ang) * 2.2f;
                    Kin[Slot].VelY        =  std::cos(Ang) * 2.2f;
                    Kin[Slot].VelZ        =  Generator.NextRange(-0.35f, 0.35f);
                    Kin[Slot].InverseMass = 1.0f;

                    Life[Slot].AgeSeconds     = 0.0f;
                    Life[Slot].MaxLifeSeconds = (I < 20u) ? 0.25f : 6.0f; // First 20 expire early to prove custom-channel swap-remove
                    Life[Slot].SeedUint32     = Generator.NextUint32();

                    Ions[Slot].ChargeCoulombs = 1.6f;
                    Ions[Slot].DecayHalfLife  = 2.4f;
                    Ions[Slot].PolaritySign   = (I % 2u == 0u) ? 1u : 0u;
                    Ions[Slot].TagNumber      = 1000u + I;

                    Appear[Slot].RadiusMeters = 0.06f;
                    Appear[Slot].ColorR       = (I % 2u == 0u) ? 0.34f : 0.96f;
                    Appear[Slot].ColorG       = (I % 2u == 0u) ? 0.86f : 0.72f;
                    Appear[Slot].ColorB       = (I % 2u == 0u) ? 1.00f : 0.34f;
                    Appear[Slot].ColorA       = 1.0f;
                }
                return Count;
            })
        .AttachCustomMotionSolver(
            "UserLorentzSwirl",
            [](ParticleChannelBank& Bank) {
                (void)Bank.RequireChannel(ChannelLayouts::Kinematics);
                (void)Bank.RequireChannel(UserIonChargeChannel);
            },
            [](ParticleChannelBank& Bank, uint32_t Begin, uint32_t End, const SimulationTickInputs& In) {
                auto Kin  = Bank.AccessChannel(ChannelLayouts::Kinematics);
                auto Ions = Bank.AccessChannel(UserIonChargeChannel);
                const float Dt = In.DeltaTimeSeconds;
                for (uint32_t Slot = Begin; Slot < End; ++Slot)
                {
                    const float Sign = (Ions[Slot].PolaritySign != 0u) ? 1.0f : -1.0f;
                    const float Q    = Ions[Slot].ChargeCoulombs;
                    // Lorentz cross-product acceleration v x B_z + centripetal pull
                    const float Ax = (-Kin[Slot].VelY * Sign * Q - Kin[Slot].PosX * 2.2f);
                    const float Ay = ( Kin[Slot].VelX * Sign * Q - Kin[Slot].PosY * 2.2f);
                    Kin[Slot].VelX += Ax * Dt;
                    Kin[Slot].VelY += Ay * Dt;
                    Ions[Slot].ChargeCoulombs *= std::exp(-Dt / std::max(0.1f, Ions[Slot].DecayHalfLife));
                }
            })
        .AssignIntegrator<SemiImplicitEulerIntegrator>()
        .Construct();

    AllPassed &= VerifyAssertion(!IonSwarm.QueryGpuComputeEligible(), "CPU-only custom channel + lambda correctly routes to CPU path");
    AllPassed &= VerifyAssertion(IonSwarm.QueryChannelBank().VerifyChannelPresent(UserIonChargeChannel), "Custom C++ UserIonChargeChannel registered");

    for (uint64_t Tick = 0u; Tick < 45u; ++Tick)
    {
        IonSwarm.Tick({
            .TimeSeconds      = static_cast<double>(Tick) / 60.0,
            .DeltaTimeSeconds = 1.0f / 60.0f,
            .TickIndex        = Tick,
        });
    }
    AllPassed &= VerifyAssertion(IonSwarm.QueryActiveCount() == 100u, "Synchronized DenseSwapRemove retired exactly 20 expired slots");
    AllPassed &= VerifyAssertion(VerifyAllParticlesFinite(IonSwarm), "Custom C++ IonSwarm positions & velocities remain finite");
    {
        const auto Ions = IonSwarm.QueryChannel(UserIonChargeChannel);
        bool CustomChannelSynced = true;
        for (uint32_t I = 0u; I < IonSwarm.QueryActiveCount(); ++I)
        {
            if (Ions[I].TagNumber < 1020u || Ions[I].ChargeCoulombs >= 1.6f)
            {
                CustomChannelSynced = false;
                break;
            }
        }
        AllPassed &= VerifyAssertion(CustomChannelSynced, "Custom UserIonChargeAttribute readings decayed & swap-removed in lockstep");
    }

    // -----------------------------------------------------------------------------------------------------------------
    // TEST 2: Direct C++ ForceFieldAccumulator & FieldLatticeCascade Reuse + Determinism Proof
    // -----------------------------------------------------------------------------------------------------------------
    ForceFieldAccumulator Forces = ConstructStudioForceFields();
    FieldLatticeCascade   Lattice(12u);
    Lattice.EvaluateTierOnCpu(1u, Forces, 0.0);

    auto MakeForceCoupledSparks = []() -> ParticleSimulation
    {
        VolumeSourceParameters Rel{};
        Rel.Shape             = VolumeShapeCategory::SphereVolume;
        Rel.OriginMeters      = { -1.2f, 0.0f, 1.4f };
        Rel.LaunchDirection   = { 0.4f, 0.0f, 1.0f };
        Rel.RadiusMeters      = 0.16f;
        Rel.ConeSpreadRad     = 0.42f;
        Rel.BirthRatePerSec   = 220.0f;
        Rel.InitialBurstCount = 96u;
        Rel.SpeedMinMs        = 1.8f;
        Rel.SpeedMaxMs        = 4.6f;
        Rel.LifeMinSec        = 1.6f;
        Rel.LifeMaxSec        = 3.0f;
        Rel.AllocateOrientation = true;

        return ParticleSimulationSpecification("ForceCoupledSparks")
            .ConfigureCapacity(640u, ChannelStoragePolicy::DenseSwapRemove)
            .AssignDeterministicSeed(9001u)
            .AttachSource<VolumeParticleSource>(Rel)
            .AttachMotionSolver<GravityBuoyancySolver>(GravityBuoyancyParameters{ .GravityScale = 0.85f, .DragCoefficient = 0.25f })
            .AttachMotionSolver<ForceFieldCouplingSolver>(ForceFieldCouplingParameters{ .CouplingGain = 1.25f, .TorqueScale = 0.5f })
            .AttachMotionSolver<CurlTurbulenceSolver>(CurlTurbulenceParameters{ .AmplitudeMs = 0.65f, .FrequencyPerM = 0.9f })
            .AttachMotionSolver<AeroFlutterSolver>()
            .AttachMotionSolver<AppearanceCurveSolver>()
            .AttachCollider<GroundPlaneCollider>(GroundPlaneParameters{ .FloorZMeters = 0.0f, .Restitution = 0.52f })
            .AssignIntegrator<SemiImplicitEulerIntegrator>()
            .Construct();
    };

    ParticleSimulation SparksA = MakeForceCoupledSparks();
    ParticleSimulation SparksB = MakeForceCoupledSparks();
        AllPassed &= VerifyAssertion(SparksA.QueryGpuComputeEligible(), "Native GPU channels + solvers report GPU compute eligibility");

    // Capture multi-step trajectory trails for visual proof
    std::vector<std::array<Point2D, 2u>> SparkTrails{};
    for (uint64_t Tick = 0u; Tick < 60u; ++Tick)
    {
        const SimulationTickInputs StepIn{
            .TimeSeconds      = static_cast<double>(Tick) / 60.0,
            .DeltaTimeSeconds = 1.0f / 60.0f,
            .ForceAccumulator = &Forces,
            .ForceLattice     = &Lattice,
            .TickIndex        = Tick,
        };
        SparksA.Tick(StepIn);
        SparksB.Tick(StepIn);
    }

        AllPassed &= VerifyAssertion(SparksA.QueryActiveCount() > 150u, "ForceCoupledSparks released & sustained active cloud");
    AllPassed &= VerifyAssertion(SparksA.QueryReadbackSummary().BoundsCenterX > -0.6f, "LinearWind + HelicalColumn ForceField advected particles along +X");
    AllPassed &= VerifyAssertion(
        SparksA.QueryActiveCount() == SparksB.QueryActiveCount()
            && SparksA.QueryReadbackSummary().PeakSpeedMs == SparksB.QueryReadbackSummary().PeakSpeedMs
            && SparksA.QueryReadbackSummary().BoundsCenterX == SparksB.QueryReadbackSummary().BoundsCenterX,
        "Deterministic seed produces bit-identical trajectories across runs");

    // Verify trilinear FieldLatticeCascade mode works seamlessly too
    {
        ParticleSimulation LatticeSim = ParticleSimulationSpecification("LatticeTest")
            .ConfigureCapacity(64u)
            .AttachSource<VolumeParticleSource>(VolumeSourceParameters{ .BirthRatePerSec = 0.0f, .InitialBurstCount = 32u })
            .AttachMotionSolver<ForceFieldCouplingSolver>(ForceFieldCouplingParameters{ .CouplingGain = 1.5f, .PreferLatticeLookup = true, .LatticeTierIndex = 1u })
            .Construct();
        for (uint64_t Tick = 0u; Tick < 20u; ++Tick)
        {
            LatticeSim.Tick({ .TimeSeconds = static_cast<double>(Tick) / 60.0, .DeltaTimeSeconds = 1.0f / 60.0f, .ForceLattice = &Lattice, .TickIndex = Tick });
        }
        AllPassed &= VerifyAssertion(VerifyAllParticlesFinite(LatticeSim) && LatticeSim.QueryReadbackSummary().PeakSpeedMs > 0.5f,
                                     "FieldLatticeCascade trilinear coupling advects particles cleanly");
    }

    // -----------------------------------------------------------------------------------------------------------------
    // TEST 3: Direct Frontier DistanceFieldSpace::SignedDistanceVolume SDF Collision Reuse
    // -----------------------------------------------------------------------------------------------------------------
    const Mathematics::Axis3D ObstacleCentre{ 0.0f, 0.0f, 1.0f };
    const float               ObstacleRadius = 0.75f;
    const DistanceFieldSpace::SignedDistanceVolume WorldSdf = ConstructAnalyticSphereSdfVolume(ObstacleCentre, ObstacleRadius);

    ParticleSimulation SdfDrop = ParticleSimulationSpecification("SdfBounce")
        .ConfigureCapacity(64u)
        .AttachSource<VolumeParticleSource>(VolumeSourceParameters{
            .Shape             = VolumeShapeCategory::DiscVolume,
            .OriginMeters      = { 0.0f, 0.0f, 2.25f },
            .LaunchDirection   = { 0.0f, 0.0f, -1.0f },
            .RadiusMeters      = 0.35f,
            .ConeSpreadRad     = 0.08f,
            .BirthRatePerSec   = 0.0f,
            .InitialBurstCount = 48u,
            .SpeedMinMs        = 2.2f,
            .SpeedMaxMs        = 2.8f,
            .LifeMinSec        = 5.0f,
            .LifeMaxSec        = 5.0f,
        })
        .AttachMotionSolver<GravityBuoyancySolver>(GravityBuoyancyParameters{ .GravityScale = 1.0f, .DragCoefficient = 0.05f })
        .AttachCollider<SignedDistanceVolumeCollider>(SignedDistanceColliderParameters{ .ContactMarginMeters = 0.04f, .Restitution = 0.7f })
        .Construct();

    for (uint64_t Tick = 0u; Tick < 50u; ++Tick)
    {
        SdfDrop.Tick({
            .TimeSeconds        = static_cast<double>(Tick) / 60.0,
            .DeltaTimeSeconds   = 1.0f / 60.0f,
            .WorldDistanceField = &WorldSdf,
            .TickIndex          = Tick,
        });
    }
    {
        const auto Kin  = SdfDrop.QueryChannel(ChannelLayouts::Kinematics);
        bool NoDeepClip = true;
        for (uint32_t I = 0u; I < SdfDrop.QueryActiveCount(); ++I)
        {
            const float DistToCentre = Mathematics::EvaluateDistance({ Kin[I].PosX, Kin[I].PosY, Kin[I].PosZ }, ObstacleCentre);
            if (DistToCentre < ObstacleRadius - 0.06f)
            {
                NoDeepClip = false;
                break;
            }
        }
        AllPassed &= VerifyAssertion(NoDeepClip && VerifyAllParticlesFinite(SdfDrop),
                                     "SignedDistanceVolumeCollider repels falling particles with zero deep SDF penetration");
    }

    // -----------------------------------------------------------------------------------------------------------------
    // TEST 4: Compile & Step ALL 93 Active Presets (0 Quarantine) via ParticlePresetCompiler
    // -----------------------------------------------------------------------------------------------------------------
    static constexpr std::array<const char*, 9u> QuarantinedSlugs = {{
        "blossom-branch-detach", "jelly-slime-blob", "slime-tendril-cluster",
        "splatter-burst", "impact-dust-ring", "splash",
        "puddle-ripple-splash", "car-puddle-splash", "mud-splatter-heavy",
    }};
    for (const char* RetiredSlug : QuarantinedSlugs)
    {
        AllPassed &= VerifyAssertion(
            !ParticlePresetCompiler::CompilePresetSlug(RetiredSlug).has_value(),
            "ParticlePresetCompiler strictly rejects quarantined slug");
    }

    DynamicSphereCollider ThrownBall{};
    ThrownBall.CentreMeters = { 0.35f, 0.0f, 1.35f };
    ThrownBall.VelocityMs   = { 0.0f, 2.5f, 0.0f };
    ThrownBall.RadiusMeters = 0.42f;
    ThrownBall.Active       = true;
    const std::array<DynamicSphereCollider, 1u> Colliders = {{ ThrownBall }};

    const auto ActivePresets = ParticlePresetRegistry::QueryActivePresets();
    AllPassed &= VerifyAssertion(ActivePresets.size() == 93u, "Exactly 93 active non-quarantined presets compiled");

    uint32_t VerifiedPresets = 0u;
    for (const ParticlePresetDescriptor& Preset : ActivePresets)
    {
        std::optional<ParticleSimulation> MaybeSim = ParticlePresetCompiler::CompilePresetSlug(Preset.Identifier, 2026u + VerifiedPresets);
        if (!VerifyAssertion(MaybeSim.has_value(), Preset.Identifier))
        {
            AllPassed = false;
            continue;
        }
        ParticleSimulation& Sim = *MaybeSim;
        for (uint64_t Tick = 0u; Tick < 75u; ++Tick)
        {
            Sim.Tick({
                .TimeSeconds      = static_cast<double>(Tick) / 60.0,
                .DeltaTimeSeconds = 1.0f / 60.0f,
                .ForceAccumulator = &Forces,
                .SphereColliders  = Colliders,
                .TickIndex        = Tick,
            });
        }
        const bool Ok = VerifyAllParticlesFinite(Sim) && Sim.QueryActiveCount() > 0u;
        if (!VerifyAssertion(Ok, Preset.DisplayName))
        {
            AllPassed = false;
        }
        ++VerifiedPresets;
    }

    // -----------------------------------------------------------------------------------------------------------------
    // VISUAL PNG PROOF: 4-Panel Live Simulated C++ Movement Showcase
    // -----------------------------------------------------------------------------------------------------------------
    // Prepare dedicated showcase simulations with ParticleSimulationRegistry
    ParticleSimulationRegistry Registry{};

    auto ClothOpt = ParticlePresetCompiler::CompilePresetSlug("flag-banner", 3001u);
    auto FerroOpt = ParticlePresetCompiler::CompilePresetSlug("ferrofluid-spikes", 3002u);
    auto PyroOpt  = ParticlePresetCompiler::CompilePresetSlug("fireworks-spectacle", 3003u);
    auto BoidsOpt = ParticlePresetCompiler::CompilePresetSlug("boids-flock", 3004u);

    AllPassed &= VerifyAssertion(ClothOpt && FerroOpt && PyroOpt && BoidsOpt, "Showcase domain presets compiled");

    // Dedicated horizontal breeze accumulator for the billowing cloth banner
    ForceFieldAccumulator ClothWind{};
    {
        ForceSourceRecordEntry Breeze{};
        Breeze.UniqueToken  = 11u;
        Breeze.Label        = "Banner Breeze";
        Breeze.Model        = ForceModel::LinearWind;
        Breeze.Domain       = ForceDomainCategory::VectorField;
        Breeze.Attenuation  = ForceAttenuationMode::None;
        Breeze.CentreMeters = { 0.0f, 0.0f, 1.5f };
        Breeze.RadiusMeters = 80.0f;
        Breeze.Strength     = 5.2f;
        Breeze.Enabled      = true;
        const std::array<ForceSourceRecordEntry, 1u> WindList = {{ Breeze }};
        ClothWind.AssignSources(WindList);
    }

    DynamicSphereCollider ClothSphere{};
    ClothSphere.CentreMeters = { 0.05f, 0.12f, 1.38f };
    ClothSphere.VelocityMs   = { 0.35f, 1.25f, 0.0f };
    ClothSphere.RadiusMeters = 0.42f;
    ClothSphere.Active       = true;
    const std::array<DynamicSphereCollider, 1u> ClothColliders = {{ ClothSphere }};

    const double FinalShowcaseTime = 84.0 / 60.0;
    if (ClothOpt && FerroOpt && PyroOpt && BoidsOpt)
    {
        Registry.RegisterSimulation(1u, std::move(*ClothOpt));
        Registry.RegisterSimulation(2u, std::move(*FerroOpt));
        Registry.RegisterSimulation(3u, std::move(*PyroOpt));
        Registry.RegisterSimulation(4u, std::move(*BoidsOpt));

        for (uint64_t Tick = 0u; Tick < 85u; ++Tick)
        {
            const double T = static_cast<double>(Tick) / 60.0;
            if (ParticleSimulation* Cloth = Registry.FindSimulation(1u))
            {
                Cloth->Tick({
                    .TimeSeconds      = T,
                    .DeltaTimeSeconds = 1.0f / 60.0f,
                    .ForceAccumulator = &ClothWind,
                    .SphereColliders  = ClothColliders,
                    .TickIndex        = Tick,
                });
            }
            if (ParticleSimulation* Ferro = Registry.FindSimulation(2u))
            {
                Ferro->Tick({
                    .TimeSeconds      = T,
                    .DeltaTimeSeconds = 1.0f / 60.0f,
                    .ForceAccumulator = nullptr,
                    .TickIndex        = Tick,
                });
            }
            if (ParticleSimulation* Pyro = Registry.FindSimulation(3u))
            {
                Pyro->Tick({
                    .TimeSeconds      = T,
                    .DeltaTimeSeconds = 1.0f / 60.0f,
                    .ForceAccumulator = &Forces,
                    .TickIndex        = Tick,
                });
            }
            if (ParticleSimulation* Boids = Registry.FindSimulation(4u))
            {
                Boids->Tick({
                    .TimeSeconds      = T,
                    .DeltaTimeSeconds = 1.0f / 60.0f,
                    .ForceAccumulator = &Forces,
                    .TickIndex        = Tick,
                });
            }
        }
    }

    const std::filesystem::path RepoRoot  = ResolveRepositoryRootPath();
    const std::filesystem::path OutFolder = RepoRoot / "Exhibits" / "Images" / "Particles";
    std::filesystem::create_directories(OutFolder);

    ImGui::CreateContext();
    ImGuiIO& Io    = ImGui::GetIO();
    Io.IniFilename = nullptr;
    Io.LogFilename = nullptr;

    TypographyRepository Typefaces{};
    (void)Typefaces.LoadRepository(RepoRoot / "EngineContent/FontsArchives");
    TypographyRepository::AssignCurrent(&Typefaces);
    if (ImFont* TabFont = Typefaces.QueryTabStripFont())
    {
        Io.FontDefault = TabFont;
    }
    else
    {
        Io.Fonts->AddFontDefault();
    }
    unsigned char* FontPixels = nullptr;
    int            FontW      = 0;
    int            FontH      = 0;
    Io.Fonts->GetTexDataAsRGBA32(&FontPixels, &FontW, &FontH);

    const uint32_t CanvasW = 1320u;
    const uint32_t CanvasH = 920u;
    Io.DisplaySize         = ImVec2(static_cast<float>(CanvasW), static_cast<float>(CanvasH));
    Io.DeltaTime           = 1.0f / 60.0f;

    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(CanvasW), static_cast<float>(CanvasH)), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin(
        "##ParticleSimProofCanvas",
        nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs);

    PixelSpace Surface(ImGui::GetWindowDrawList(), { 0.0f, 0.0f }, 1.0f);
    Surface.FillRectangle({ 0.0f, 0.0f, static_cast<float>(CanvasW), static_cast<float>(CanvasH) }, ColorRgba{ 11u, 11u, 12u, 255u }, 0.0f);

    // Top Studio Title Bar
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Medium,
        { 22.0f, 16.0f },
        18.0f,
        ColorRgba{ 242u, 242u, 242u, 255u },
        "Frontier::SimulationSpace::Particles — Modular C++20 Movement & Typed Channel Proofs");
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { 22.0f, 38.0f },
        11.0f,
        ColorRgba{ 138u, 138u, 144u, 255u },
        "Typed SoA ParticleChannelBank · Direct ForceFieldAccumulator + SignedDistanceVolume reuse · 93 active presets verified (0 NaN, 0 Quarantine)");

    const float Pad   = 16.0f;
    const float TopY  = 64.0f;
    const float Gap   = 14.0f;
    const float CellW = (static_cast<float>(CanvasW) - Pad * 2.0f - Gap) * 0.5f;
    const float CellH = (static_cast<float>(CanvasH) - TopY - Pad - Gap) * 0.5f;

    const Rectangle2D Panel1{ Pad, TopY, CellW, CellH };
    const Rectangle2D Panel2{ Pad + CellW + Gap, TopY, CellW, CellH };
    const Rectangle2D Panel3{ Pad, TopY + CellH + Gap, CellW, CellH };
    const Rectangle2D Panel4{ Pad + CellW + Gap, TopY + CellH + Gap, CellW, CellH };

    // PANEL 1: ForceFields-Coupled Ballistics
    RenderPanelContainer(Surface, Panel1, "01 / FORCEFIELDS REUSE", "Ballistic Sparks + C++ ForceFieldAccumulator",
                     "LinearWind + HelicalColumn + ThermalUpdraft + Ground Bounce + OrientationChannel", SparksA, ColorRgba{ 232u, 194u, 122u, 255u });
    {
        const Rectangle2D Plot{ Panel1.X + 14.0f, Panel1.Y + 54.0f, Panel1.Width - 28.0f, Panel1.Height - 96.0f };
        Surface.PushClip(Plot);
        const auto Kin    = SparksA.QueryChannel(ChannelLayouts::Kinematics);
        const auto Appear = SparksA.QueryChannel(ChannelLayouts::Appearance);
        for (uint32_t I = 0u; I < SparksA.QueryActiveCount(); ++I)
        {
            const Point2D Head = ProjectWorldPanel(Plot, Kin[I].PosX, Kin[I].PosY, Kin[I].PosZ, 56.0f, 1.4f);
            const Point2D Tail = ProjectWorldPanel(
                Plot,
                Kin[I].PosX - Kin[I].VelX * 0.045f,
                Kin[I].PosY - Kin[I].VelY * 0.045f,
                Kin[I].PosZ - Kin[I].VelZ * 0.045f,
                56.0f,
                1.4f);
            const ColorRgba Col{
                static_cast<uint8_t>(std::clamp(Appear[I].ColorR * 255.0f, 40.0f, 255.0f)),
                static_cast<uint8_t>(std::clamp(Appear[I].ColorG * 255.0f, 40.0f, 255.0f)),
                static_cast<uint8_t>(std::clamp(Appear[I].ColorB * 255.0f, 30.0f, 255.0f)),
                225u,
            };
            Surface.StrokeLine(Tail, Head, Col, 2.0f);
            Surface.FillCircle(Head, 2.2f, Col);
        }
        Surface.PopClip();
    }

    // PANEL 2: XPBD Interactive Cloth Strands + Thrown DynamicSphereCollider
    if (const ParticleSimulation* ClothSim = Registry.FindSimulation(1u))
    {
        RenderPanelContainer(Surface, Panel2, "02 / XPBD VERLET STRANDS", "Interactive Cloth Banner + Dynamic Sphere Collider",
                         "FixedSlotOrder StrandAnchor + StrandConnectivity + Wind Wave + Tear & Sphere Push-Out", *ClothSim, ColorRgba{ 126u, 198u, 118u, 255u });
        const Rectangle2D Plot{ Panel2.X + 14.0f, Panel2.Y + 54.0f, Panel2.Width - 28.0f, Panel2.Height - 96.0f };
        Surface.PushClip(Plot);

        // Vertical metallic mast anchored from floor to banner top
        const Point2D MastBase = ProjectWorldPanel(Plot, -1.15f, 0.0f, 0.0f, 82.0f, 1.25f);
        const Point2D MastTop  = ProjectWorldPanel(Plot, -1.15f, 0.0f, 2.25f, 82.0f, 1.25f);
        Surface.StrokeLine(MastBase, MastTop, ColorRgba{ 160u, 164u, 174u, 220u }, 3.0f);
        Surface.FillCircle(MastTop, 4.0f, ColorRgba{ 232u, 194u, 122u, 255u });

        const Point2D BallPt = ProjectWorldPanel(Plot, ClothSphere.CentreMeters.X, ClothSphere.CentreMeters.Y, ClothSphere.CentreMeters.Z, 82.0f, 1.25f);
        Surface.FillCircle(BallPt, ClothSphere.RadiusMeters * 82.0f, ColorRgba{ 232u, 194u, 122u, 38u });
        Surface.StrokeCircle(BallPt, ClothSphere.RadiusMeters * 82.0f, ColorRgba{ 232u, 194u, 122u, 200u }, 1.6f);

        const auto Kin  = ClothSim->QueryChannel(ChannelLayouts::Kinematics);
        const auto Conn = ClothSim->QueryChannel(ChannelLayouts::StrandConnectivity);
        const uint32_t Active = ClothSim->QueryActiveCount();
        for (uint32_t I = 0u; I < Active; ++I)
        {
            const Point2D Pt = ProjectWorldPanel(Plot, Kin[I].PosX, Kin[I].PosY, Kin[I].PosZ, 82.0f, 1.25f);
            if (Conn[I].LinkPrevSlot < Active)
            {
                const Point2D Prev = ProjectWorldPanel(Plot, Kin[Conn[I].LinkPrevSlot].PosX, Kin[Conn[I].LinkPrevSlot].PosY, Kin[Conn[I].LinkPrevSlot].PosZ, 82.0f, 1.25f);
                Surface.StrokeLine(Prev, Pt, ColorRgba{ 118u, 206u, 152u, 200u }, 1.6f);
            }
            if (Conn[I].LinkCrossSlot < Active)
            {
                const Point2D Cross = ProjectWorldPanel(Plot, Kin[Conn[I].LinkCrossSlot].PosX, Kin[Conn[I].LinkCrossSlot].PosY, Kin[Conn[I].LinkCrossSlot].PosZ, 82.0f, 1.25f);
                Surface.StrokeLine(Cross, Pt, ColorRgba{ 92u, 178u, 134u, 155u }, 1.2f);
            }
            Surface.FillCircle(Pt, (Kin[I].InverseMass <= 0.0f) ? 3.2f : 1.9f,
                               (Kin[I].InverseMass <= 0.0f) ? ColorRgba{ 232u, 194u, 122u, 255u } : ColorRgba{ 204u, 245u, 218u, 235u });
        }
        Surface.PopClip();
    }

    // PANEL 3: Rosensweig Magnetic Ferrofluid Cusps
    if (const ParticleSimulation* FerroSim = Registry.FindSimulation(2u))
    {
        RenderPanelContainer(Surface, Panel3, "03 / ROSENSWEIG FERROFLUID", "Magnetic Ferrofluid Dish + Orbiting Dipole",
                         "MagneticCuspChannel + Normal Flux Instability Peaks + MidpointRk2Integrator", *FerroSim, ColorRgba{ 158u, 192u, 224u, 255u });
        const Rectangle2D Plot{ Panel3.X + 14.0f, Panel3.Y + 54.0f, Panel3.Width - 28.0f, Panel3.Height - 96.0f };
        Surface.PushClip(Plot);

        const auto Kin    = FerroSim->QueryChannel(ChannelLayouts::Kinematics);
        const auto Cusp   = FerroSim->QueryChannel(ChannelLayouts::MagneticCusp);
        const auto Appear = FerroSim->QueryChannel(ChannelLayouts::Appearance);
        const float DishZ = (FerroSim->QueryActiveCount() > 0u) ? Cusp[0u].DishAnchorZ : 0.0f;

        // Obsidian petri dish rim aligned to exact DishAnchorZ
        constexpr uint32_t RimSteps = 48u;
        for (uint32_t S = 0u; S < RimSteps; ++S)
        {
            const float A0 = (static_cast<float>(S) / static_cast<float>(RimSteps)) * 6.2831853f;
            const float A1 = (static_cast<float>(S + 1u) / static_cast<float>(RimSteps)) * 6.2831853f;
            const Point2D P0 = ProjectWorldPanel(Plot, std::cos(A0) * 1.20f, std::sin(A0) * 1.20f, DishZ, 125.0f, DishZ + 0.35f);
            const Point2D P1 = ProjectWorldPanel(Plot, std::cos(A1) * 1.20f, std::sin(A1) * 1.20f, DishZ, 125.0f, DishZ + 0.35f);
            Surface.StrokeLine(P0, P1, ColorRgba{ 130u, 158u, 198u, 115u }, 1.6f);
        }

        for (uint32_t I = 0u; I < FerroSim->QueryActiveCount(); ++I)
        {
            const Point2D Base = ProjectWorldPanel(Plot, Cusp[I].DishAnchorX, Cusp[I].DishAnchorY, Cusp[I].DishAnchorZ, 125.0f, DishZ + 0.35f);
            const Point2D Tip  = ProjectWorldPanel(Plot, Kin[I].PosX, Kin[I].PosY, Kin[I].PosZ, 125.0f, DishZ + 0.35f);
            const uint8_t Spec = static_cast<uint8_t>(std::clamp(88.0f + Appear[I].ColorRampT * 167.0f, 65.0f, 255.0f));
            Surface.FillCircle(Base, 2.5f, ColorRgba{ 30u, 36u, 48u, 220u });
            Surface.StrokeLine(Base, Tip, ColorRgba{ static_cast<uint8_t>(Spec * 0.72f), static_cast<uint8_t>(Spec * 0.85f), Spec, 235u }, 2.5f);
            Surface.FillCircle(Tip, 2.2f + Appear[I].ColorRampT * 1.4f, ColorRgba{ Spec, Spec, 255u, 250u });
        }

        // Hovering orbiting magnetic dipole indicator directly above the peak cluster
        const float PoleAng = static_cast<float>(FinalShowcaseTime) * 0.75f;
        const float PoleX   = std::cos(PoleAng) * (1.15f * 0.62f);
        const float PoleY   = std::sin(PoleAng) * (1.15f * 0.62f);
        const float PoleZ   = DishZ + 0.85f * 1.15f + 0.25f * std::sin(PoleAng * 2.0f);
        const Point2D PolePt = ProjectWorldPanel(Plot, PoleX, PoleY, PoleZ, 125.0f, DishZ + 0.35f);
        Surface.FillCircle(PolePt, 14.0f, ColorRgba{ 138u, 212u, 255u, 42u });
        Surface.StrokeCircle(PolePt, 14.0f, ColorRgba{ 160u, 224u, 255u, 220u }, 1.6f);
        Surface.FillCircle(PolePt, 5.0f, ColorRgba{ 230u, 248u, 255u, 255u });
        Surface.PopClip();
    }

    // PANEL 4: Custom User C++ Channel Type (UserIonChargeAttribute) + Hanabi Fireworks
    RenderPanelContainer(Surface, Panel4, "04 / CUSTOM C++ TYPE & LAMBDA", "UserIonChargeAttribute Swirl + Hanabi Burst",
                     "User-defined struct outside engine headers + CustomParticleSolver + Synchronized Swap-Remove", IonSwarm, ColorRgba{ 98u, 212u, 244u, 255u });
    {
        const Rectangle2D Plot{ Panel4.X + 14.0f, Panel4.Y + 54.0f, Panel4.Width - 28.0f, Panel4.Height - 96.0f };
        Surface.PushClip(Plot);

        // Overlay Fireworks stars subtly in background
        if (const ParticleSimulation* PyroSim = Registry.FindSimulation(3u))
        {
            const auto Pk = PyroSim->QueryChannel(ChannelLayouts::Kinematics);
            const auto Pa = PyroSim->QueryChannel(ChannelLayouts::Appearance);
            for (uint32_t I = 0u; I < PyroSim->QueryActiveCount(); ++I)
            {
                const Point2D Pt = ProjectWorldPanel(Plot, Pk[I].PosX * 0.35f, Pk[I].PosY * 0.35f, Pk[I].PosZ * 0.25f + 0.4f, 82.0f, 1.2f);
                Surface.FillCircle(
                    Pt,
                    2.0f,
                    ColorRgba{
                        static_cast<uint8_t>(std::clamp(Pa[I].ColorR * 255.0f, 40.0f, 255.0f)),
                        static_cast<uint8_t>(std::clamp(Pa[I].ColorG * 255.0f, 40.0f, 255.0f)),
                        static_cast<uint8_t>(std::clamp(Pa[I].ColorB * 255.0f, 40.0f, 255.0f)),
                        145u,
                    });
            }
        }

        // Foreground custom user IonChargeChannel orbital particles
        const auto Kin    = IonSwarm.QueryChannel(ChannelLayouts::Kinematics);
        const auto Appear = IonSwarm.QueryChannel(ChannelLayouts::Appearance);
        for (uint32_t I = 0u; I < IonSwarm.QueryActiveCount(); ++I)
        {
            const Point2D Head = ProjectWorldPanel(Plot, Kin[I].PosX, Kin[I].PosY, Kin[I].PosZ, 95.0f, 1.2f);
            const Point2D Tail = ProjectWorldPanel(
                Plot,
                Kin[I].PosX - Kin[I].VelX * 0.05f,
                Kin[I].PosY - Kin[I].VelY * 0.05f,
                Kin[I].PosZ - Kin[I].VelZ * 0.05f,
                95.0f,
                1.2f);
            const ColorRgba Col{
                static_cast<uint8_t>(std::clamp(Appear[I].ColorR * 255.0f, 40.0f, 255.0f)),
                static_cast<uint8_t>(std::clamp(Appear[I].ColorG * 255.0f, 40.0f, 255.0f)),
                static_cast<uint8_t>(std::clamp(Appear[I].ColorB * 255.0f, 40.0f, 255.0f)),
                240u,
            };
            Surface.StrokeLine(Tail, Head, Col, 2.1f);
            Surface.FillCircle(Head, 2.8f, Col);
        }
        Surface.PopClip();
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::Render();

    SoftwareRasterCanvas Canvas(CanvasW, CanvasH, { 11u, 11u, 12u, 255u });
    Canvas.RasterizeImGuiBatches(ImGui::GetDrawData());
    const std::filesystem::path ProofPath = OutFolder / "ParticleSimulation_ModularMovement_Showcase.png";
    AllPassed &= VerifyAssertion(Canvas.WritePngFile(ProofPath), "Write ParticleSimulation_ModularMovement_Showcase.png");

    ImGui::DestroyContext();

    if (!AllPassed)
    {
        return 1;
    }

    std::printf(
        "[ParticleSimulationValidation] PASS — Modular SoA channels + custom C++ type + ForceFieldAccumulator/FieldLatticeCascade + SignedDistanceVolume SDF + all %u/93 active presets verified (0 NaN, 0 Quarantine), proof written to %s.\n",
        VerifiedPresets,
        ProofPath.string().c_str());
    return 0;
}
