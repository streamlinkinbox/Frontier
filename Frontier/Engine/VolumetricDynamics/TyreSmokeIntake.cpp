/*==========================================================================================================================================
                                                           TYRESMOKEINTAKE.CPP
==========================================================================================================================================*/
// 🧩 1:1 C++ port of TyreSlipSolver.js, TyreThermalSolver.js, and PlumeSourceProjection.js.

#include "Frontier/Engine/VolumetricDynamics/TyreSmokeIntake.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{

constexpr float PiVal                  = 3.14159265358979323846f;
constexpr float AmbientKelvin          = 293.15f;      // [K]
constexpr float RoadKelvin             = 293.15f;      // [K]
constexpr float SurfaceHeatCap         = 1800.0f;      // [J/K]
constexpr float CarcassHeatCap         = 14400.0f;     // [J/K]
constexpr float SurfaceCarcassCond     = 28.0f;        // [W/K]
constexpr float CarcassGasCond         = 3.0f;         // [W/K]
constexpr float SurfaceAirCond         = 12.0f;        // [W/K]
constexpr float CarcassAirCond         = 3.0f;         // [W/K]
constexpr float RoadCond               = 18.0f;        // [W/K]
constexpr float SlidingHeatShare       = 0.75f;        // [-]
constexpr float AirGasConstant         = 287.05f;      // [J/kg/K]
constexpr float AirSpecificHeat        = 718.0f;       // [J/kg/K]
constexpr float OptimumKelvin          = 353.15f;      // [K]
constexpr float GripFadeKelvin         = 473.15f;      // [K]
constexpr float MinimumGrip            = 0.60f;        // [-]
constexpr float PressureGripSens       = 0.30f;        // [-]
constexpr float InitialCombustibleMass = 2.0f;         // [kg]
constexpr float RubberBurnRate         = 0.002f;       // [kg/s]
constexpr float HeatOfCombustion       = 30000000.0f;  // [J/kg]
constexpr float CombustionHeatShare    = 0.10f;        // [-]
constexpr float RestAirVolume          = 0.034f;       // [m^3]
constexpr float ColdGaugePressure      = 220000.0f;    // [Pa]
constexpr float AtmosphericPressure    = 101325.0f;    // [Pa]

constexpr float InitialTreadMass       = 1.1f;         // [kg]
constexpr float WearCoefficient        = 6.0e-8f;      // [kg/J]
constexpr float WearHeatSpan           = 80.0f;        // [K]
constexpr float VapourShare            = 0.55f;        // [-]
constexpr float PyrolysisHeat          = 1.6e6f;       // [J/kg]

constexpr float SourcePowerOnset       = 200.0f;       // [W]
constexpr float SourceReleaseRatio     = 0.60f;        // [-]
constexpr float SourceOnsetSeconds     = 0.05f;        // [s]
constexpr float SourceReleaseSeconds   = 0.08f;        // [s]
constexpr float SourcePowerFull        = 18000.0f;     // [W]
constexpr float MinimumSlipSpeed       = 0.35f;        // [m/s]
constexpr float BaseDensityRate        = 18.0f;        // [1/s]
constexpr float CarrierVelocityShare   = 0.15f;        // [-]
constexpr float SmokeHeatRate          = 0.04f;        // [K/s]
constexpr float FireHeatPowerScale     = 0.00018f;     // [K/W] scaled to Pyro solver normalised T range [0..12]
constexpr float PatchRadiusMetres      = 0.16f;        // [m]
constexpr float WarmBaseKelvin         = 333.15f;      // [K]
constexpr float WarmSpanKelvin         = 190.0f;       // [K]
constexpr float FlingShare             = 0.35f;        // [-]
constexpr float FlingCeiling           = 9.0f;         // [m/s]
constexpr float ArchShare              = 0.35f;        // [-]
constexpr float FlameSidewallReach     = 0.24f;        // [m]
constexpr float FlameRiseSpeed         = 2.6f;         // [m/s]

float ComputeAirMass(float GaugePa, float GasKelvin) noexcept
{
    return (GaugePa + AtmosphericPressure) * RestAirVolume / (AirGasConstant * std::max(200.0f, GasKelvin));
}

// Convert Frontier Z-up world position (Wx, Wy, Wz) into normalised [0..1]^3 UVW inside Pyro Y-up domain anchored at OriginWorld
std::array<float, 3> WorldToDomainUvw(const std::array<float, 3>& WorldPos,
                                      const std::array<float, 3>& OriginWorld,
                                      const PyroBoundsBox& Bounds) noexcept
{
    const float PyroX = WorldPos[0] - OriginWorld[0];
    const float PyroY = WorldPos[2] - OriginWorld[2] - 0.60f;
    const float PyroZ = WorldPos[1] - OriginWorld[1];
    return {
        (PyroX - Bounds.BoxMin[0]) / std::max(1e-4f, Bounds.BoxMax[0] - Bounds.BoxMin[0]),
        (PyroY - Bounds.BoxMin[1]) / std::max(1e-4f, Bounds.BoxMax[1] - Bounds.BoxMin[1]),
        (PyroZ - Bounds.BoxMin[2]) / std::max(1e-4f, Bounds.BoxMax[2] - Bounds.BoxMin[2]),
    };
}

// Convert Frontier Z-up velocity (Vx, Vy, Vz) into Pyro Y-up velocity (Vx, Vz, Vy)
std::array<float, 3> WorldVelToPyro(const std::array<float, 3>& WorldVel) noexcept
{
    return { WorldVel[0], WorldVel[2], WorldVel[1] };
}

} // namespace

TyreSmokeIntake::TyreSmokeIntake()
{
    ResetCold();
}

void TyreSmokeIntake::ResetCold() noexcept
{
    for (WheelThermalReading& Wheel : Wheels)
    {
        Wheel                 = WheelThermalReading{};
        Wheel.SurfaceKelvin   = AmbientKelvin;
        Wheel.CarcassKelvin   = AmbientKelvin;
        Wheel.GasKelvin       = AmbientKelvin;
        Wheel.AirMassKg       = ComputeAirMass(ColdGaugePressure, AmbientKelvin);
        Wheel.GaugePressurePa = ColdGaugePressure;
        Wheel.GripFactor      = 1.0f;
        Wheel.TreadMassKg     = InitialTreadMass;
        Wheel.RemainingRubber = InitialCombustibleMass;
    }
    HasPreviousPose = false;
}

void TyreSmokeIntake::ForceIgniteDrivenTyres() noexcept
{
    for (int Index = 2; Index < 4; ++Index)
    {
        WheelThermalReading& Wheel = Wheels[static_cast<size_t>(Index)];
        Wheel.SurfaceKelvin        = 665.0f;
        Wheel.CarcassKelvin        = 445.0f;
        Wheel.OverheatSeconds      = 5.0f;
        Wheel.Ignited              = true;
        Wheel.CombustionPowerW     = RubberBurnRate * HeatOfCombustion;
    }
}

void TyreSmokeIntake::SolvePacejkaCombined(float SlipRatio, float SlipAngle, float NormalLoadN,
                                           float& OutFx, float& OutFy) const noexcept
{
    if (NormalLoadN <= 1.0e-3f)
    {
        OutFx = 0.0f;
        OutFy = 0.0f;
        return;
    }

    // Pacejka MF6.1 road-sport coefficients matching VehicleSpecification.js
    const float Kappa = std::clamp(SlipRatio, -0.92f, 1.50f);
    const float Alpha = std::clamp(SlipAngle, -1.30f, 1.30f);
    const float FzKn  = NormalLoadN * 0.001f;
    const float Fz0   = 4.2f;
    const float Dfz   = (FzKn - Fz0) / Fz0;

    // Pure longitudinal
    const float Cx      = 1.65f;
    const float Dx      = (1.18f - 0.08f * Dfz) * FzKn * std::max(0.65f, 1.0f - 0.06f * std::fabs(Dfz));
    const float BcdX    = (14.5f - 1.8f * Dfz) * FzKn;
    const float Bx      = BcdX / std::max(Cx * Dx, 1e-5f);
    const float Ex      = 0.34f;
    const float ProdX   = Bx * Kappa;
    float Fx            = Dx * std::sin(Cx * std::atan(ProdX - Ex * (ProdX - std::atan(ProdX)))) * 1000.0f;

    // Pure lateral
    const float Cy      = 1.38f;
    const float Dy      = (1.15f - 0.09f * Dfz) * FzKn * std::max(0.65f, 1.0f - 0.07f * std::fabs(Dfz));
    const float BcdY    = (13.2f - 1.5f * Dfz) * FzKn;
    const float By      = BcdY / std::max(Cy * Dy, 1e-5f);
    const float Ey      = -0.55f;
    const float ProdY   = By * Alpha;
    float Fy            = Dy * std::sin(Cy * std::atan(ProdY - Ey * (ProdY - std::atan(ProdY)))) * 1000.0f;

    // MF6.1 cosine combined-slip weighting
    const float Ks   = Kappa / std::max(BcdX, 1e-5f);
    const float As   = std::tan(Alpha) / std::max(BcdY, 1e-5f);
    const float Comb = std::hypot(Ks, As);
    if (Comb > 1e-7f)
    {
        const float Gx = std::cos(1.02f * std::atan(12.5f * As));
        const float Gy = std::cos(1.01f * std::atan(10.8f * Ks * Comb));
        Fx *= Gx;
        Fy *= Gy;
    }

    OutFx = Fx;
    OutFy = Fy;
}

void TyreSmokeIntake::IntegrateWheelThermal(WheelThermalReading& Wheel, float AirspeedMps, float DtSeconds) noexcept
{
    if (DtSeconds <= 0.0f)
        return;

    bool PolicyEnabled  = IgnitionPolicy != TyreIgnitionPolicy::Disabled;
    float IgnitionK     = IgnitionPolicy == TyreIgnitionPolicy::Diagnostic ? 433.15f : 623.15f;
    float HoldThreshold = IgnitionPolicy == TyreIgnitionPolicy::Diagnostic ? 1.0f : 4.0f;
    float ExtinctionK   = IgnitionPolicy == TyreIgnitionPolicy::Diagnostic ? 373.15f : 473.15f;

    Wheel.CombustionPowerW = 0.0f;
    if (PolicyEnabled && Wheel.SurfaceKelvin >= IgnitionK)
        Wheel.OverheatSeconds += DtSeconds;
    else
        Wheel.OverheatSeconds = 0.0f;

    if (PolicyEnabled && Wheel.RemainingRubber > 0.0f && Wheel.OverheatSeconds >= HoldThreshold)
        Wheel.Ignited = true;
    if (!PolicyEnabled || Wheel.RemainingRubber <= 0.0f || Wheel.SurfaceKelvin < ExtinctionK)
        Wheel.Ignited = false;

    if (Wheel.Ignited)
    {
        const float Burned     = std::min(Wheel.RemainingRubber, RubberBurnRate * DtSeconds);
        Wheel.RemainingRubber -= Burned;
        Wheel.CombustionPowerW = Burned * HeatOfCombustion / DtSeconds;
    }

    const float HeatFactor  = 1.0f + std::max(0.0f, Wheel.SurfaceKelvin - OptimumKelvin) / WearHeatSpan;
    const float WearRate    = (!Wheel.Burst && Wheel.TreadMassKg > 0.0f)
                            ? (WearCoefficient * Wheel.SlidingPowerW * HeatFactor) : 0.0f;
    const float WornKg      = std::min(Wheel.TreadMassKg, WearRate * DtSeconds);
    Wheel.TreadMassKg      -= WornKg;
    const float VapourHeat  = WornKg * VapourShare * PyrolysisHeat;

    const float SlidingHeat = std::max(0.0f, Wheel.SlidingPowerW * SlidingHeatShare * DtSeconds - VapourHeat);
    const float BurnHeat    = Wheel.CombustionPowerW * CombustionHeatShare * DtSeconds;
    const float AirFactor   = 1.0f + 0.35f * std::sqrt(std::max(0.0f, AirspeedMps));
    const float AirSurf     = SurfaceAirCond * AirFactor;
    const float AirCarc     = CarcassAirCond * AirFactor;

    const float Cap0 = SurfaceHeatCap;
    const float Cap1 = CarcassHeatCap;
    const float Cap2 = std::max(10.0f, Wheel.AirMassKg * AirSpecificHeat);

    // 3x3 backward-Euler Gauss-Jordan system matching TyreThermalSolver.js
    double Eq[3][4] = {
        { Cap0 / DtSeconds + AirSurf + RoadCond + SurfaceCarcassCond,
          -SurfaceCarcassCond,
          0.0,
          Cap0 * Wheel.SurfaceKelvin / DtSeconds + (SlidingHeat + BurnHeat) / DtSeconds
              + AirSurf * AmbientKelvin + RoadCond * RoadKelvin },
        { -SurfaceCarcassCond,
          Cap1 / DtSeconds + AirCarc + SurfaceCarcassCond + CarcassGasCond,
          -CarcassGasCond,
          Cap1 * Wheel.CarcassKelvin / DtSeconds + AirCarc * AmbientKelvin },
        { 0.0,
          -CarcassGasCond,
          Cap2 / DtSeconds + CarcassGasCond,
          Cap2 * Wheel.GasKelvin / DtSeconds },
    };

    for (int Pivot = 0; Pivot < 3; ++Pivot)
    {
        const double Diag = Eq[Pivot][Pivot];
        if (std::fabs(Diag) < 1e-9)
            continue;
        for (int Col = Pivot; Col < 4; ++Col)
            Eq[Pivot][Col] /= Diag;
        for (int Row = 0; Row < 3; ++Row)
        {
            if (Row == Pivot)
                continue;
            const double Factor = Eq[Row][Pivot];
            for (int Col = Pivot; Col < 4; ++Col)
                Eq[Row][Col] -= Factor * Eq[Pivot][Col];
        }
    }

    Wheel.SurfaceKelvin   = static_cast<float>(Eq[0][3]);
    Wheel.CarcassKelvin   = static_cast<float>(Eq[1][3]);
    Wheel.GasKelvin       = static_cast<float>(Eq[2][3]);
    Wheel.GaugePressurePa = std::max(0.0f, Wheel.AirMassKg * AirGasConstant * Wheel.GasKelvin / RestAirVolume - AtmosphericPressure);

    const float Hot       = std::clamp((Wheel.SurfaceKelvin - OptimumKelvin) / (GripFadeKelvin - OptimumKelvin), 0.0f, 1.0f);
    const float Dev       = std::clamp(std::fabs(Wheel.GaugePressurePa - ColdGaugePressure) / ColdGaugePressure, 0.0f, 1.0f);
    Wheel.GripFactor      = (1.0f - Hot * (1.0f - MinimumGrip)) * std::max(0.25f, 1.0f - PressureGripSens * Dev);
}

bool TyreSmokeIntake::AdvanceSmokeOnset(WheelThermalReading& Wheel, bool InContact, float DtSeconds) noexcept
{
    if (!InContact || DtSeconds <= 0.0f)
    {
        Wheel.RequestSeconds = 0.0f;
        Wheel.QuietSeconds   = 0.0f;
        Wheel.SmokingActive  = false;
        return false;
    }
    const bool AboveEntry   = Wheel.SlidingPowerW > SourcePowerOnset && Wheel.SlipSpeedMps >= MinimumSlipSpeed;
    const bool AboveRelease = Wheel.SlidingPowerW > SourcePowerOnset * SourceReleaseRatio
                            && Wheel.SlipSpeedMps >= MinimumSlipSpeed;
    if (AboveEntry)
    {
        Wheel.RequestSeconds += DtSeconds;
        Wheel.QuietSeconds    = 0.0f;
        if (Wheel.RequestSeconds + 1.0e-6f >= SourceOnsetSeconds)
            Wheel.SmokingActive = true;
    }
    else if (Wheel.SmokingActive && AboveRelease)
    {
        Wheel.QuietSeconds = 0.0f;
    }
    else
    {
        Wheel.RequestSeconds = 0.0f;
        Wheel.QuietSeconds  += DtSeconds;
        if (Wheel.QuietSeconds >= SourceReleaseSeconds)
            Wheel.SmokingActive = false;
    }
    return Wheel.SmokingActive;
}

void TyreSmokeIntake::Advance(const DeployableUnit& Vehicle, float DtSeconds, float GroundHeightZ)
{
    if (DtSeconds <= 0.0f)
        return;

    ElapsedSeconds += DtSeconds;

    const auto& Pos     = Vehicle.QueryPositionMetres();
    const float YawDeg  = Vehicle.QueryYawDegrees();
    const float YawRad  = YawDeg * PiVal / 180.0f;
    const float CosYaw  = std::cos(YawRad);
    const float SinYaw  = std::sin(YawRad);
    const std::array<float, 3> Fwd   = { CosYaw, SinYaw, 0.0f };
    const std::array<float, 3> LeftV = { -SinYaw, CosYaw, 0.0f };

    std::array<float, 3> ChassisVel = {
        Fwd[0] * Vehicle.QueryForwardSpeedMps(),
        Fwd[1] * Vehicle.QueryForwardSpeedMps(),
        0.0f,
    };
    float YawRateRad = 0.0f;
    if (HasPreviousPose && DtSeconds > 1e-4f)
    {
        ChassisVel = {
            (Pos[0] - PreviousVehiclePos[0]) / DtSeconds,
            (Pos[1] - PreviousVehiclePos[1]) / DtSeconds,
            (Pos[2] - PreviousVehiclePos[2]) / DtSeconds,
        };
        float DeltaYaw = YawDeg - PreviousYawDeg;
        while (DeltaYaw > 180.0f)  DeltaYaw -= 360.0f;
        while (DeltaYaw < -180.0f) DeltaYaw += 360.0f;
        YawRateRad = (DeltaYaw * PiVal / 180.0f) / DtSeconds;
    }
    PreviousVehiclePos = Pos;
    PreviousYawDeg     = YawDeg;
    HasPreviousPose    = true;

    const InputPacket& Input = Vehicle.QueryLatestInput();
    float Throttle = std::clamp(Input.ThrottleAxis, 0.0f, 1.0f);
    float Brake    = std::clamp(Input.BrakeAxis, 0.0f, 1.0f);
    float Handbrake= std::max(std::clamp(Input.HandbrakeAxis, 0.0f, 1.0f),
                              Input.HasAction(InputActionHandbrake) ? 1.0f : 0.0f);
    const bool Boost = Input.HasAction(InputActionBoost);
    const float SteerAngleRad = std::clamp(Input.SteerAxis, -1.0f, 1.0f) * 0.56f;

    if (BurnoutLineLock)
    {
        Throttle = std::max(Throttle, 0.92f);
        Brake    = std::max(Brake, 0.80f);
    }

    // Exact ControlVehicle axle mounts matching SoftwarePresentation.cpp
    static constexpr float LocalWheelOffsets[4][2] = {
        {  1.7274f,  1.0475f },   // 0: Front-Left
        {  1.7274f, -1.0475f },   // 1: Front-Right
        { -1.6686f,  1.0475f },   // 2: Rear-Left
        { -1.6686f, -1.0475f },   // 3: Rear-Right
    };

    const float SpeedAbs = std::hypot(ChassisVel[0], ChassisVel[1]);

    for (int Index = 0; Index < 4; ++Index)
    {
        WheelThermalReading& Wheel = Wheels[static_cast<size_t>(Index)];
        const bool IsFront         = Index < 2;
        const float LocalX         = LocalWheelOffsets[Index][0];
        const float LocalY         = LocalWheelOffsets[Index][1];

        const float OffsetWorldX   = LocalX * CosYaw - LocalY * SinYaw;
        const float OffsetWorldY   = LocalX * SinYaw + LocalY * CosYaw;

        Wheel.WheelRadius  = 0.5085f;
        Wheel.HubWorld     = { Pos[0] + OffsetWorldX, Pos[1] + OffsetWorldY, Pos[2] - 0.02145f };
        Wheel.ContactWorld = { Pos[0] + OffsetWorldX, Pos[1] + OffsetWorldY, GroundHeightZ };

        // Contact travel velocity = V_cm + omega_z x r
        Wheel.ContactVelocity = {
            ChassisVel[0] - YawRateRad * OffsetWorldY,
            ChassisVel[1] + YawRateRad * OffsetWorldX,
            ChassisVel[2],
        };

        Wheel.SteerRadians = IsFront ? SteerAngleRad : 0.0f;
        const float WheelYaw = YawRad + Wheel.SteerRadians;
        Wheel.HeadingWorld   = { std::cos(WheelYaw), std::sin(WheelYaw), 0.0f };
        const std::array<float, 3> WheelLat = { -std::sin(WheelYaw), std::cos(WheelYaw), 0.0f };

        const float VLong = Wheel.ContactVelocity[0] * Wheel.HeadingWorld[0]
                          + Wheel.ContactVelocity[1] * Wheel.HeadingWorld[1];
        const float VLat  = Wheel.ContactVelocity[0] * WheelLat[0]
                          + Wheel.ContactVelocity[1] * WheelLat[1];

        // Driven wheel surface speed & slip
        float TargetSurfaceSpeed = VLong;
        if (!IsFront)
        {
            if (BurnoutLineLock || (Throttle > 0.25f && (Brake > 0.25f || Handbrake > 0.25f || Boost)))
            {
                const float BurnoutSpin = (14.5f + 9.5f * (Boost ? 1.0f : 0.0f)) * Throttle;
                TargetSurfaceSpeed      = std::max(VLong + BurnoutSpin, BurnoutSpin);
            }
            else if (Handbrake > 0.35f && SpeedAbs > 1.2f)
            {
                TargetSurfaceSpeed = VLong * (1.0f - 0.92f * Handbrake);
            }
            else if (Throttle > 0.45f)
            {
                // Wheelspin under launch, boost, or aggressive steering corner exit
                const float LaunchSlip = std::max(0.0f, 1.0f - SpeedAbs / 11.0f) * 8.5f * Throttle;
                const float DriftSlip  = std::fabs(Input.SteerAxis) * Throttle * (4.8f + (Boost ? 5.5f : 0.0f));
                TargetSurfaceSpeed     = VLong + LaunchSlip + DriftSlip;
            }
            else if (Brake > 0.65f && SpeedAbs > 2.5f)
            {
                TargetSurfaceSpeed = VLong * (1.0f - 0.55f * Brake);
            }
        }
        else
        {
            if (BurnoutLineLock)
                TargetSurfaceSpeed = 0.0f;
            else if (Brake > 0.72f && SpeedAbs > 3.0f)
                TargetSurfaceSpeed = VLong * (1.0f - 0.62f * Brake);
        }

        // Add subtle lateral drift angle during steering at speed or handbrake yaw
        float EffectiveVLat = VLat;
        if (!IsFront && std::fabs(Input.SteerAxis) > 0.25f && (SpeedAbs > 4.0f || Handbrake > 0.2f || Throttle > 0.6f))
        {
            const float DriftLat = -Input.SteerAxis * (Handbrake > 0.2f ? 5.6f : 2.6f) * std::min(1.0f, SpeedAbs / 6.0f + Throttle * 0.5f);
            EffectiveVLat += DriftLat;
        }

        Wheel.SurfaceSlipMps = TargetSurfaceSpeed - VLong;
        Wheel.AngularVelRad  = TargetSurfaceSpeed / Wheel.WheelRadius;
        Wheel.SpinRadians    = std::fmod(Wheel.SpinRadians + Wheel.AngularVelRad * DtSeconds, 2.0f * PiVal);

        const float RefDenom = std::max({ std::fabs(VLong), std::fabs(TargetSurfaceSpeed) * 0.5f, 1.2f });
        Wheel.SlipRatio      = std::clamp(Wheel.SurfaceSlipMps / RefDenom, -0.92f, 1.50f);
        Wheel.SlipAngleRad   = std::atan2(EffectiveVLat, std::max(std::fabs(VLong), 1.0f));
        Wheel.SlipSpeedMps   = std::hypot(Wheel.SurfaceSlipMps, EffectiveVLat);

        // Static + longitudinal weight transfer normal load [N]
        const float StaticLoadN = IsFront ? 4150.0f : 4450.0f;
        float Fx = 0.0f, Fy = 0.0f;
        SolvePacejkaCombined(Wheel.SlipRatio, Wheel.SlipAngleRad, StaticLoadN, Fx, Fy);
        Fx *= Wheel.GripFactor;
        Fy *= Wheel.GripFactor;

        Wheel.SlidingPowerW = std::fabs(Fx * Wheel.SurfaceSlipMps) + std::fabs(Fy * EffectiveVLat);
        const bool InContact = (Pos[2] - GroundHeightZ) < 0.85f;
        if (!InContact)
            Wheel.SlidingPowerW = 0.0f;

        IntegrateWheelThermal(Wheel, SpeedAbs, DtSeconds);
        AdvanceSmokeOnset(Wheel, InContact, DtSeconds);
    }
}

int TyreSmokeIntake::ProjectIntoDomain(const std::array<float, 3>& DomainOriginWorld, const PyroBoundsBox& Bounds,
                                       PyroExternalSource* OutSources, int MaxSources,
                                       std::vector<DynamicFireLight>& OutFireLights) const
{
    if (!OutSources || MaxSources <= 0)
        return 0;

    int Written = 0;
    auto PushSource = [&](const std::array<float, 3>& WorldPos, float WorldRadiusMetres,
                          const std::array<float, 3>& WorldVel, float Smoke, float Temp, float Fuel, float Soot)
    {
        if (Written >= MaxSources || (Smoke <= 0.0f && Temp <= 0.0f && Fuel <= 0.0f && Soot <= 0.0f))
            return;
        const auto Uvw = WorldToDomainUvw(WorldPos, DomainOriginWorld, Bounds);
        if (Uvw[0] < 0.02f || Uvw[0] > 0.98f || Uvw[1] < 0.01f || Uvw[1] > 0.96f || Uvw[2] < 0.02f || Uvw[2] > 0.98f)
            return;
        PyroExternalSource Src{};
        Src.PositionUvw      = Uvw;
        Src.NormalizedRadius = std::clamp(WorldRadiusMetres / std::max(1.0f, Bounds.EffectiveWidth), 0.004f, 0.16f);
        Src.VelocityPyro     = WorldVelToPyro(WorldVel);
        Src.SmokeRate        = Smoke;
        Src.Temperature      = Temp;
        Src.FuelRate         = Fuel;
        Src.SootRate         = Soot;
        OutSources[Written++] = Src;
    };

    const float Rate      = DischargeGain;
    const float EffSmoke  = SmokeYield * Rate;
    const float EffHeat   = HeatYield * Rate;
    const float EffSoot   = SootYield * Rate;

    for (int Index = 0; Index < 4; ++Index)
    {
        const WheelThermalReading& Wheel = Wheels[static_cast<size_t>(Index)];
        const bool Burning               = Wheel.Ignited && Wheel.CombustionPowerW > 0.0f;
        const bool Sliding               = Wheel.SmokingActive;
        if (!Sliding && !Burning)
            continue;

        const float Strength = Sliding
            ? std::clamp((Wheel.SlidingPowerW - SourcePowerOnset) / std::max(1.0f, SourcePowerFull - SourcePowerOnset), 0.0f, 4.0f)
            : 0.0f;
        const float Warm     = std::clamp((Wheel.SurfaceKelvin - WarmBaseKelvin) / WarmSpanKelvin, 0.0f, 1.0f);
        const float Smoke    = BaseDensityRate * Strength * (0.38f + 0.62f * Warm) * (Wheel.Burst ? 0.25f : 1.0f);

        const float Fling    = std::clamp(FlingShare * Wheel.SurfaceSlipMps, -FlingCeiling, FlingCeiling);
        const std::array<float, 3> PatchPos = { Wheel.ContactWorld[0], Wheel.ContactWorld[1], Wheel.ContactWorld[2] + 0.08f };
        const std::array<float, 3> ThrownVel = {
            Wheel.ContactVelocity[0] * CarrierVelocityShare - Wheel.HeadingWorld[0] * Fling,
            Wheel.ContactVelocity[1] * CarrierVelocityShare - Wheel.HeadingWorld[1] * Fling,
            Wheel.ContactVelocity[2] * CarrierVelocityShare + 0.45f,
        };

        if (Sliding && Smoke > 0.01f)
        {
            PushSource(PatchPos, PatchRadiusMetres * 1.65f, ThrownVel,
                       Smoke * EffSmoke * 0.42f, (0.28f + SmokeHeatRate * Strength * 4.0f) * EffHeat, 0.0f, 0.0f);

            if (std::fabs(Fling) > 1.0f && Written < MaxSources)
            {
                const float BackSign = Fling >= 0.0f ? -1.0f : 1.0f;
                const std::array<float, 3> ArchPos = {
                    Wheel.HubWorld[0] + Wheel.HeadingWorld[0] * BackSign * Wheel.WheelRadius * 0.85f,
                    Wheel.HubWorld[1] + Wheel.HeadingWorld[1] * BackSign * Wheel.WheelRadius * 0.85f,
                    Wheel.HubWorld[2] + Wheel.WheelRadius * 0.35f,
                };
                const std::array<float, 3> ArchVel = {
                    Wheel.HeadingWorld[0] * BackSign * 0.45f * std::fabs(Fling),
                    Wheel.HeadingWorld[1] * BackSign * 0.45f * std::fabs(Fling),
                    1.05f,
                };
                PushSource(ArchPos, 0.30f, ArchVel, ArchShare * Smoke * EffSmoke * 0.42f, 0.22f * EffHeat, 0.0f, 0.0f);
            }
        }

        if (Burning)
        {
            const float OutSign = (Index & 1) ? -1.0f : 1.0f;
            const std::array<float, 3> Outward = { -Wheel.HeadingWorld[1] * OutSign, Wheel.HeadingWorld[0] * OutSign, 0.0f };
            const std::array<float, 3> SidewallPos = {
                Wheel.HubWorld[0] + Outward[0] * FlameSidewallReach,
                Wheel.HubWorld[1] + Outward[1] * FlameSidewallReach,
                Wheel.HubWorld[2] - 0.15f * Wheel.WheelRadius,
            };
            const std::array<float, 3> SidewallVel = {
                Wheel.ContactVelocity[0] * CarrierVelocityShare + Outward[0] * 0.45f,
                Wheel.ContactVelocity[1] * CarrierVelocityShare + Outward[1] * 0.45f,
                FlameRiseSpeed,
            };
            const float PlumeHeat = std::clamp(Wheel.CombustionPowerW * FireHeatPowerScale * EffHeat, 4.2f, 9.5f);
            PushSource(SidewallPos, 0.34f, SidewallVel, 1.8f * EffSmoke, PlumeHeat, 6.8f * Rate, 3.8f * EffSoot);

            const float Flicker = 0.78f + 0.22f * std::sin(ElapsedSeconds * 17.0f + static_cast<float>(Index) * 2.1f)
                                                * std::sin(ElapsedSeconds * 6.3f + static_cast<float>(Index));
            const float Gain = (Wheel.CombustionPowerW / 60000.0f) * 3.2f * Flicker;
            DynamicFireLight Light{};
            Light.PositionWorld  = { SidewallPos[0], SidewallPos[1], SidewallPos[2] + 0.42f };
            Light.RadiusMetres   = 5.5f;
            Light.RadianceLinear = { 1.0f * Gain, 0.46f * Gain, 0.14f * Gain };
            OutFireLights.push_back(Light);
        }
    }

    return Written;
}

void TyreSmokeIntake::AdaptTyreDomainAppearance(GasSettings& InOutSettings) const noexcept
{
    if (HasAnyBurningTyre())
    {
        // Burning rubber: bright orange blackbody flame + thick black carbonaceous soot
        InOutSettings.SmokeAlbedo   = 0.20f;
        InOutSettings.FireIntensity = 8.2f;
        InOutSettings.BurnRate      = 2.2f;
        InOutSettings.BurnHeat      = 3.1f;
        InOutSettings.SootGeneration= 2.1f;
        InOutSettings.EmbersShown   = true;
        InOutSettings.EmberCount    = 360;
    }
    else
    {
        // Pure friction tyre burnout: dense white/light-grey vulcanised rubber vapour, zero flame
        InOutSettings.SmokeAlbedo   = 0.80f;
        InOutSettings.FireIntensity = 0.0f;
        InOutSettings.EmbersShown   = false;
    }
}

int TyreSmokeIntake::QueryActiveSmokingWheelCount() const noexcept
{
    int Count = 0;
    for (const WheelThermalReading& Wheel : Wheels)
    {
        if (Wheel.SmokingActive || Wheel.Ignited)
            ++Count;
    }
    return Count;
}

bool TyreSmokeIntake::HasAnyBurningTyre() const noexcept
{
    for (const WheelThermalReading& Wheel : Wheels)
    {
        if (Wheel.Ignited && Wheel.CombustionPowerW > 0.0f)
            return true;
    }
    return false;
}

float TyreSmokeIntake::QueryPeakSurfaceCelsius() const noexcept
{
    float PeakK = AmbientKelvin;
    for (const WheelThermalReading& Wheel : Wheels)
        PeakK = std::max(PeakK, Wheel.SurfaceKelvin);
    return PeakK - 273.15f;
}

float TyreSmokeIntake::QueryTotalSlidingPowerKw() const noexcept
{
    float SumW = 0.0f;
    for (const WheelThermalReading& Wheel : Wheels)
        SumW += Wheel.SlidingPowerW;
    return SumW * 0.001f;
}

} // namespace Frontier
