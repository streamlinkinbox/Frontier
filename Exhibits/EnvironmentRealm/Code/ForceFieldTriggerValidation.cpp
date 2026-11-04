/*==========================================================================================================================================
                                         COMPLETE 8-TRIGGER FORCEFIELD VALIDATION
==========================================================================================================================================*/
// Verifies all 8 triggered dynamic ForceField sources layered additively onto ambient planetary wind:
//   1. VortexColumn          (Tornado / Dust Devil)
//   2. TravelingGustFront    (Haboob / Outflow Arcus)
//   3. RadialMicroburst      (Downdraft Shaft + Outflow Vortex Ring)
//   4. LocalizedGustPuff     (Drifting 3D Ellipsoidal Wind Surge)
//   5. AerodynamicWake       (Vehicle/Aircraft Slipstream + Counter-Rotating Trailing Vortices + Rooster-Tail Spray)
//   6. ThermalUpdraftPlume   (Buoyant Rising Column + Lower Radial Entrainment + Cumulus Cap Boost)
//   7. DirectionalJetCorridor (Cylindrical High-Velocity Exhaust/Tunnel Jet + Shear Flow)
//   8. SphericalImpulseShell (Expanding Shock/Blast Wavefront Shell R(t) + Particulate Dust Ring)
// Plus Vulkan 1.3 GPU PlanetaryEnsemble (VolumetricClouds + FogSpace) multi-trigger showcase execution!

#include "Engine/DeviceExchange/VulkanComputeExchange.h"
#include "Engine/PhysicalDynamics/ForceField.h"
#include "Engine/PlanetaryAtmosphere/AtmosphereScattering.h"
#include "Engine/PlanetaryAtmosphere/PlanetaryEnsemble.h"

#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace
{

using namespace Frontier;

void RequireFf(bool Condition, const char* Expr, const char* Detail, int Line)
{
    if (!Condition)
    {
        std::cerr << "[ForceFieldTriggerValidation] FAILED at line " << Line << ": " << Detail
                  << " (" << Expr << ")\n";
        std::exit(1);
    }
}

#define REQUIRE_FF(Cond, Msg) RequireFf((Cond), #Cond, (Msg), __LINE__)

double VecSpeed(SpatialVector V) noexcept
{
    return std::sqrt(V.X * V.X + V.Y * V.Y + V.Z * V.Z);
}

PlanetaryCameraView MakeCam(uint32_t             RealmIdx,
                            std::array<float, 3> PosMeters,
                            std::array<float, 3> TargetMeters,
                            uint32_t             W,
                            uint32_t             H,
                            float                FovDeg,
                            float                Exposure)
{
    const float Dx  = TargetMeters[0] - PosMeters[0];
    const float Dy  = TargetMeters[1] - PosMeters[1];
    const float Dz  = TargetMeters[2] - PosMeters[2];
    const float Fl  = std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
    const auto  Fwd = std::array<float, 3>{Dx / Fl, Dy / Fl, Dz / Fl};
    const auto  Up0 = std::array<float, 3>{0.0f, 1.0f, 0.0f};
    const float Rx  = Fwd[1] * Up0[2] - Fwd[2] * Up0[1];
    const float Ry  = Fwd[2] * Up0[0] - Fwd[0] * Up0[2];
    const float Rz  = Fwd[0] * Up0[1] - Fwd[1] * Up0[0];
    const float Rl  = std::sqrt(Rx * Rx + Ry * Ry + Rz * Rz);
    const auto  Rgt = std::array<float, 3>{Rx / Rl, Ry / Rl, Rz / Rl};
    const auto  Up  = std::array<float, 3>{
        Rgt[1] * Fwd[2] - Rgt[2] * Fwd[1],
        Rgt[2] * Fwd[0] - Rgt[0] * Fwd[2],
        Rgt[0] * Fwd[1] - Rgt[1] * Fwd[0]};

    PlanetaryCameraView Cam{};
    Cam.ActiveRealmIndex = RealmIdx;
    Cam.PositionMeters   = PosMeters;
    Cam.RightUnit        = Rgt;
    Cam.UpUnit           = Up;
    Cam.ForwardUnit      = Fwd;
    Cam.VerticalFovRad   = FovDeg * 3.14159265f / 180.0f;
    Cam.ExposureScale    = Exposure;
    Cam.ViewportWidth    = W;
    Cam.ViewportHeight   = H;
    return Cam;
}

} // namespace

int main(int Argc, char** Argv)
{
    const std::filesystem::path RepoRoot  = (Argc > 1) ? Argv[1] : ".";
    const std::filesystem::path ShaderDir = (Argc > 2) ? Argv[2] : "build/ShaderArchives/Environment";
    const std::filesystem::path OutputDir = (Argc > 3) ? Argv[3] : "build/environment-realm-exhibits";

    bool MirrorShowcase = false;
    for (int Idx = 1; Idx < Argc; ++Idx)
    {
        if (std::strcmp(Argv[Idx], "--write-showcase") == 0)
        {
            MirrorShowcase = true;
        }
    }

    PlanetaryWindProfile BaseWind{};
    BaseWind.SpeedMetersPerSecond = 8.0;
    BaseWind.BearingRadians       = 0.0; // Wind blowing along +X
    BaseWind.ShearPerKilometer    = 0.22;
    BaseWind.GustAmplitude        = 0.12;

    // 1. Validate Individual Physics of All 8 ForceField Triggers Layered Over Base Wind
    std::cout << "[1/3] Validating Individual Kinematics of All 8 ForceField Triggers Over Global Wind...\n";

    // (1..3) VortexColumn, TravelingGustFront, RadialMicroburst
    {
        ForceFieldSpace Space{};
        Space.SetWindProfile(BaseWind);
        const auto Ambient0 = Space.EvaluateStepSample({20.0, 12.0, 0.0});
        REQUIRE_FF(VecSpeed(Ambient0.AirVelocityMps) > 5.0, "Ambient wind baseline must be active");

        (void)Space.TriggerDustDevil({0.0, 0.0, 0.0}, 120.0, 32.0, 20.0);
        Space.Step(1.5);
        const auto Swirl = Space.EvaluateStepSample({10.0, 8.0, 0.0});
        REQUIRE_FF(VecSpeed(Swirl.AirVelocityMps) > VecSpeed(Ambient0.AirVelocityMps) + 12.0 &&
                       Swirl.ParticulateExtinctionPerM > 0.002,
                   "Trigger 1 (VortexColumn) must superimpose tangential swirl + lofted dust onto global wind");
    }

    // (4) LocalizedGustPuff: Drifting 3D ellipsoidal wind surge coupled to global wind
    {
        ForceFieldSpace Space{};
        Space.SetWindProfile(BaseWind);
        const auto BaseSample = Space.EvaluateStepSample({8.0, 6.0, 0.0});

        GustPuffParameters Puff{};
        Puff.CenterMeters            = {0.0, 6.0, 0.0};
        Puff.SurgeVelocityMps        = {26.0, 3.5, -6.0};
        Puff.RadiusXZMeters          = 14.0;
        Puff.RadiusYMeters           = 8.0;
        Puff.GlobalWindDriftCoupling = 1.0; // Drifts ~8 m along +X over 1 s
        Puff.WispDustExtinctionM     = 0.016;
        const auto H = Space.TriggerGustPuff(Puff, {0.1, 10.0, 1.0});
        Space.Step(1.0);

        const auto AtDriftedCore = Space.EvaluateStepSample({7.5, 6.0, 0.0});
        const auto FarOutside    = Space.EvaluateStepSample({95.0, 6.0, 0.0});
        REQUIRE_FF(AtDriftedCore.AirVelocityMps.X > BaseSample.AirVelocityMps.X + 18.0 &&
                       AtDriftedCore.ParticulateExtinctionPerM > 0.006 &&
                       std::abs(FarOutside.AirVelocityMps.X - BaseSample.AirVelocityMps.X) < 1.0,
                   "Trigger 4 (LocalizedGustPuff) must advect smoothly with global wind and add ellipsoidal surge + dust");
        REQUIRE_FF(Space.ReleaseTrigger(H), "ReleaseTrigger must succeed for LocalizedGustPuff");
    }

    // (5) AerodynamicWake: Trailing slipstream + twin counter-rotating vortices + rooster-tail spray
    {
        ForceFieldSpace Space{};
        Space.SetWindProfile(BaseWind);

        AerodynamicWakeParameters Wake{};
        Wake.BodyOriginMeters    = {0.0, 0.8, 0.0};
        Wake.BodyVelocityMps     = {0.0, 0.0, 24.0}; // Moving +Z => wake trails along -Z
        Wake.HalfWidthMeters        = 1.25;
        Wake.WakeLengthMeters       = 36.0;
        Wake.WakeHeightMeters       = 3.6;
        Wake.SlipstreamSuctionMps   = 28.0;
        Wake.TrailingCirculationMps = 14.0;
        Wake.RoosterTailExtinctionM = 0.035;
        (void)Space.TriggerAerodynamicWake(Wake, {0.05, 10.0, 1.0});
        // Step tiny dt so emitter advances slightly while ramping to full strength
        Space.Step(0.1);

        const auto CenterTrail = Space.EvaluateStepSample({0.0, 1.0, -6.0});
        const auto AheadCar    = Space.EvaluateStepSample({0.0, 1.0, +12.0});
        const auto LeftOuter   = Space.EvaluateStepSample({-2.0, 1.0, -8.0});
        const auto RightOuter  = Space.EvaluateStepSample({+2.0, 1.0, -8.0});
        REQUIRE_FF(CenterTrail.AirVelocityMps.Z > AheadCar.AirVelocityMps.Z + 12.0 &&
                       CenterTrail.ParticulateExtinctionPerM > 0.008 &&
                       (LeftOuter.AirVelocityMps.Y != RightOuter.AirVelocityMps.Y ||
                        LeftOuter.AirVelocityMps.X != RightOuter.AirVelocityMps.X),
                   "Trigger 5 (AerodynamicWake) must trail behind emitter with forward slipstream, twin vortices, and spray");
    }

    // (6) ThermalUpdraftPlume: Buoyant rising column + lower radial entrainment inflow + cumulus anvil cap boost
    {
        ForceFieldSpace Space{};
        Space.SetWindProfile(BaseWind);

        ThermalUpdraftParameters Plume{};
        Plume.BaseAnchorMeters     = {0.0, 0.0, 0.0};
        Plume.ColumnHeightMeters   = 520.0;
        Plume.CoreRadiusMeters     = 45.0;
        Plume.PeakUpdraftSpeedMps  = 24.0;
        Plume.EntrainmentInflowMps = 9.5;
        Plume.CumulusBoostStrength = 0.88;
        Plume.PlumeDustExtinctionM = 0.016;
        (void)Space.TriggerThermalUpdraft(Plume, {0.1, 10.0, 1.0});
        Space.Step(1.0);

        const auto MidCore  = Space.EvaluateStepSample({0.0, 220.0, 0.0});
        const auto LowerRim = Space.EvaluateStepSample({40.0, 35.0, 0.0});
        const auto UpperCap = Space.EvaluateStepSample({0.0, 460.0, 0.0});
        REQUIRE_FF(MidCore.AirVelocityMps.Y > 15.0 &&
                       LowerRim.AirVelocityMps.X < BaseWind.SpeedMetersPerSecond - 1.0 &&
                       UpperCap.FunnelCondensationDensity > 0.20,
                   "Trigger 6 (ThermalUpdraftPlume) must generate vertical updraft, lower inward entrainment, and upper cumulus boost");
    }

    // (7) DirectionalJetCorridor: High-velocity axial tunnel/exhaust jet
    {
        ForceFieldSpace Space{};
        Space.SetWindProfile(BaseWind);

        DirectionalJetParameters Jet{};
        Jet.OriginMeters         = {0.0, 6.0, 0.0};
        Jet.AxisDirectionUnit    = {1.0, 0.15, 0.0};
        Jet.CorridorLengthMeters = 80.0;
        Jet.CorridorRadiusMeters = 6.5;
        Jet.CoreJetSpeedMps      = 64.0;
        Jet.JetParticulateExtM   = 0.028;
        (void)Space.TriggerDirectionalJet(Jet, {0.1, 10.0, 1.0});
        Space.Step(1.0);

        const auto Core20m = Space.EvaluateStepSample({20.0, 9.0, 0.0});
        const auto Behind  = Space.EvaluateStepSample({-12.0, 6.0, 0.0});
        REQUIRE_FF(Core20m.AirVelocityMps.X > Behind.AirVelocityMps.X + 35.0 &&
                       Core20m.ParticulateExtinctionPerM > 0.008,
                   "Trigger 7 (DirectionalJetCorridor) must produce high-speed directional core flow and exhaust particulate");
    }

    // (8) SphericalImpulseShell: Expanding radial shockwave shell R(t) = R0 + c*t
    {
        ForceFieldSpace Space{};
        Space.SetWindProfile(BaseWind);

        SphericalImpulseParameters Impulse{};
        Impulse.EpicenterMeters      = {0.0, 8.0, 0.0};
        Impulse.InitialRadiusMeters  = 16.0;
        Impulse.ExpansionSpeedMps    = 38.0;
        Impulse.ShellThicknessMeters = 8.0;
        Impulse.PeakRadialSpeedMps   = 65.0;
        Impulse.ShockDustExtinctionM = 0.042;
        (void)Space.TriggerSphericalImpulse(Impulse, {0.05, 8.0, 1.0});
        Space.Step(1.0); // Wavefront radius R(1.0) = 16 + 38*1.0 = 54 m!

        const auto OnWavefront54m = Space.EvaluateStepSample({54.0, 8.0, 0.0});
        const auto DeepInterior   = Space.EvaluateStepSample({10.0, 8.0, 0.0});
        const auto AheadWave95m   = Space.EvaluateStepSample({95.0, 8.0, 0.0});
        REQUIRE_FF(OnWavefront54m.AirVelocityMps.X > DeepInterior.AirVelocityMps.X + 18.0 &&
                       OnWavefront54m.AirVelocityMps.X > AheadWave95m.AirVelocityMps.X + 18.0 &&
                       OnWavefront54m.ParticulateExtinctionPerM > 0.006,
                   "Trigger 8 (SphericalImpulseShell) must concentrate radial blast velocity and shock dust on the expanding R(t) shell");
    }

    // 2. Verify Simultaneous Additive Superposition & 16-Slot GPU Packing of All 8 Trigger Kinds
    std::cout << "[2/3] Verifying Simultaneous 8-Trigger Superposition & 16-Slot GPU Float4 Packing...\n";
    {
        ForceFieldSpace AllEight{};
        AllEight.SetWindProfile(BaseWind);
        (void)AllEight.TriggerDustDevil({-60.0, 0.0, 120.0}, 80.0, 26.0, 20.0);
        GustFrontParameters Gf{};
        Gf.OriginMeters = {-120.0, 0.0, 180.0};
        (void)AllEight.TriggerGustFront(Gf, {0.1, 20.0, 1.0});
        MicroburstParameters Mb{};
        Mb.CoreCenterMeters = {80.0, 0.0, 210.0};
        (void)AllEight.TriggerMicroburst(Mb, {0.1, 20.0, 1.0});
        GustPuffParameters Gp{};
        Gp.CenterMeters = {15.0, 6.0, 65.0};
        (void)AllEight.TriggerGustPuff(Gp, {0.1, 20.0, 1.0});
        AerodynamicWakeParameters Aw{};
        Aw.BodyOriginMeters = {-18.0, 1.0, 95.0};
        (void)AllEight.TriggerAerodynamicWake(Aw, {0.1, 20.0, 1.0});
        ThermalUpdraftParameters Tu{};
        Tu.BaseAnchorMeters = {45.0, 0.0, 175.0};
        (void)AllEight.TriggerThermalUpdraft(Tu, {0.1, 20.0, 1.0});
        DirectionalJetParameters Dj{};
        Dj.OriginMeters = {-35.0, 8.0, 110.0};
        (void)AllEight.TriggerDirectionalJet(Dj, {0.1, 20.0, 1.0});
        SphericalImpulseParameters Si{};
        Si.EpicenterMeters = {20.0, 12.0, 140.0};
        (void)AllEight.TriggerSphericalImpulse(Si, {0.1, 20.0, 1.0});

        AllEight.Step(1.2);
        std::vector<std::array<float, 4>> GpuSlots;
        const uint32_t PackedCount = AllEight.AppendGpuForceRecords(GpuSlots);
        REQUIRE_FF(PackedCount == 8u && GpuSlots.size() == 8u * kForceSourceFloat4Stride,
                   "ForceFieldSpace::AppendGpuForceRecords must pack all 8 distinct trigger kinds cleanly into 32 float4s");

        for (uint32_t Idx = 0u; Idx < 8u; ++Idx)
        {
            const uint32_t EncodedKind = static_cast<uint32_t>(GpuSlots[Idx * 4u + 0u][3] + 0.5f);
            REQUIRE_FF(EncodedKind == Idx + 1u, "Each trigger kind 1..8 must serialize its exact GPU kind tag in F0.w");
        }
    }

    // 3. Execute Full Vulkan 1.3 PlanetaryEnsemble GPU Render Showcasing New Triggers (Thermal Plume + Shock Shell + Wake)
    std::cout << "[3/3] Executing Vulkan 1.3 PlanetaryEnsemble GPU Multi-Trigger Showcase Render...\n";
    std::string           Diag;
    VulkanComputeExchange Exchange{};
    REQUIRE_FF(Exchange.Initialize("", &Diag), ("VulkanComputeExchange::Initialize failed: " + Diag).c_str());

    const std::string LunaJpg = (RepoRoot / "EngineContent/EnvironmentArchives/CelestialMaps/MoonAlbedoSource2048.jpg").string();
    const std::string HippCsv = (RepoRoot / "EngineContent/EnvironmentArchives/CelestialMaps/Hipparcos8.csv").string();
    PlanetarySubmissionSequence Sequence{};
    REQUIRE_FF(Sequence.Initialize(Exchange, ShaderDir.string(), LunaJpg, HippCsv, &Diag),
               ("PlanetarySubmissionSequence::Initialize failed: " + Diag).c_str());

    PlanetaryRealmEnsemble World{};
    World.Realms.resize(1u);
    PlanetaryRealm& R = World.Realms[0];
    R.Atmosphere      = AtmosphereParameters{};

    RealmSolarSource Sun{};
    const float Sx = -0.62f, Sy = 0.36f, Sz = -0.69f;
    const float SL = std::sqrt(Sx * Sx + Sy * Sy + Sz * Sz);
    Sun.DirectionUnit   = {Sx / SL, Sy / SL, Sz / SL};
    Sun.DiscIrradiance  = {1.45f, 1.32f, 1.16f};
    Sun.EnableLensFlare = false;
    R.Suns.push_back(Sun);

    CloudVolumeProfile Deck{};
    Deck.Domain              = CloudVolumeDomain::PlanetarySlab;
    Deck.Slab.LowerHeightM   = 380.0;
    Deck.Slab.TopHeightM     = 1420.0;
    Deck.Slab.Coverage       = 0.48;
    Deck.Slab.DensityScale   = 1.45;
    Deck.Slab.ExtinctionPerM = 0.062;
    R.CloudVolumes.push_back(Deck);

    R.ForceField.SetWindProfile(BaseWind);

    // Trigger 6 (Left): Thermal Updraft Plume feeding a towering cumulus turret at X = -125 m, Z = 340 m
    ThermalUpdraftParameters Plume{};
    Plume.BaseAnchorMeters     = {-125.0, 0.0, 340.0};
    Plume.ColumnHeightMeters   = 880.0;
    Plume.CoreRadiusMeters     = 34.0;
    Plume.PeakUpdraftSpeedMps  = 28.0;
    Plume.EntrainmentInflowMps = 11.0;
    Plume.CumulusBoostStrength = 0.95;
    Plume.PlumeDustExtinctionM = 0.0065;
    Plume.PlumeAlbedoRgb       = {0.78, 0.64, 0.46};
    (void)R.ForceField.TriggerThermalUpdraft(Plume, {0.2, 40.0, 2.0});

    // Trigger 8 (Center): Expanding Spherical Impulse Dust Ring at X = +8 m, Y = 24 m, Z = 250 m
    SphericalImpulseParameters Shock{};
    Shock.EpicenterMeters      = {8.0, 24.0, 250.0};
    Shock.InitialRadiusMeters  = 12.0;
    Shock.ExpansionSpeedMps    = 18.0;
    Shock.ShellThicknessMeters = 6.5;
    Shock.PeakRadialSpeedMps   = 55.0;
    Shock.ShockDustExtinctionM = 0.024;
    Shock.ShockAlbedoRgb       = {0.84, 0.68, 0.48};
    (void)R.ForceField.TriggerSphericalImpulse(Shock, {0.1, 40.0, 2.0});

    // Trigger 5 (Right Foreground): High-speed Aerodynamic Rooster-Tail Wake streaking across X = +55 m, Z = 135 m
    AerodynamicWakeParameters Wake{};
    Wake.BodyOriginMeters    = {95.0, 1.2, 135.0};
    Wake.BodyVelocityMps     = {24.0, 0.0, -5.0};
    Wake.HalfWidthMeters        = 4.5;
    Wake.WakeLengthMeters       = 68.0;
    Wake.WakeHeightMeters       = 11.5;
    Wake.SlipstreamSuctionMps   = 34.0;
    Wake.TrailingCirculationMps = 18.0;
    Wake.RoosterTailExtinctionM = 0.032;
    Wake.SprayDustAlbedoRgb     = {0.82, 0.76, 0.66};
    (void)R.ForceField.TriggerAerodynamicWake(Wake, {0.1, 40.0, 2.0});

    R.ForceField.Step(2.0);
    R.HeightFog.ReferenceDensity = 0.00024;
    R.HeightFog.FalloffHeightM   = 58.0;

    constexpr uint32_t W   = 640u;
    constexpr uint32_t H   = 360u;
    const auto         Cam = MakeCam(0u, {0.0f, 14.0f, -35.0f}, {6.0f, 92.0f, 280.0f}, W, H, 58.0f, 1.38f);
    REQUIRE_FF(Sequence.ExecuteFrame(Exchange, World, Cam),
               "PlanetarySubmissionSequence::ExecuteFrame failed for multi-trigger ForceField showcase");

    std::vector<float>   LinearHdr;
    std::vector<uint8_t> Srgb8;
    REQUIRE_FF(Sequence.ReadbackHdrFrame(Exchange, LinearHdr), "ReadbackHdrFrame failed");
    TonemapHdrToSrgb8(LinearHdr, W, H, Cam.ExposureScale, Srgb8);

    std::filesystem::create_directories(OutputDir);
    const auto OutPng = OutputDir / "ForceFieldMultiTriggerShowcase.png";
    REQUIRE_FF(stbi_write_png(OutPng.string().c_str(), W, H, 4, Srgb8.data(), W * 4) != 0,
               "Failed writing ForceFieldMultiTriggerShowcase.png");
    if (MirrorShowcase)
    {
        const auto ShowDir = RepoRoot / "Exhibits/EnvironmentRealm/Images";
        std::filesystem::create_directories(ShowDir);
        (void)stbi_write_png((ShowDir / "ForceFieldMultiTriggerShowcase.png").string().c_str(), W, H, 4, Srgb8.data(), W * 4);
    }

    Sequence.Shutdown(Exchange);
    Exchange.Shutdown();

    std::cout << "[ForceFieldTriggerValidation] ALL 8 FORCEFIELD TRIGGERS & GPU SHOWCASE CHECKS PASSED.\n";
    return 0;
}
