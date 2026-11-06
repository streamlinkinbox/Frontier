#pragma once
/*==========================================================================================================================================
                                                            TYRESMOKEINTAKE.H
==========================================================================================================================================*/
// 🧩 1:1 C++ port of Experimental/VehicleHmiPrototype's TyreSlipSolver.js, TyreThermalSolver.js, and PlumeSourceProjection.js.
//    Evaluates Pacejka MF6.1 combined slip, 3-node implicit tyre heat conduction (tread, carcass, enclosed gas),
//    sustained-overheat rubber ignition, and projects contact-patch, wheel-arch, and burning-tyre plume sources.

#include "Frontier/Engine/Scene/DeployableUnit.h"
#include "Frontier/Engine/VolumetricDynamics/PyroVolumeSolver.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Frontier
{

//----------------------------------------------------------------------------------------------------------------------
//                                                 IGNITION & TELEMETRY
//----------------------------------------------------------------------------------------------------------------------

enum class TyreIgnitionPolicy : uint8_t
{
    Authored   = 0,   // [-] - 350 C (623.15 K) held for 4.0 s, extinguishes below 200 C
    Diagnostic = 1,   // [-] - 160 C (433.15 K) held for 1.0 s, extinguishes below 100 C
    Disabled   = 2,   // [-] - thermal heating only, never ignites
};

inline const char* TyreIgnitionCaption(TyreIgnitionPolicy Policy) noexcept
{
    switch (Policy)
    {
        case TyreIgnitionPolicy::Diagnostic: return "Diagnostic (160 \xc2\xb0""C held 1 s)";
        case TyreIgnitionPolicy::Disabled:   return "Off";
        default:                             return "Authored (350 \xc2\xb0""C held 4 s)";
    }
}

struct WheelThermalReading
{
    std::array<float, 3> HubWorld        = { 0.0f, 0.0f, 0.34f };   // [m]   - Frontier Z-up world hub position
    std::array<float, 3> ContactWorld    = { 0.0f, 0.0f, 0.0f };    // [m]   - Frontier Z-up patch position
    std::array<float, 3> ContactVelocity = { 0.0f, 0.0f, 0.0f };    // [m/s] - world patch travel velocity
    std::array<float, 3> HeadingWorld    = { 1.0f, 0.0f, 0.0f };    // [-]   - wheel longitudinal unit vector
    float                SteerRadians    = 0.0f;                    // [rad]
    float                SpinRadians     = 0.0f;                    // [rad] - integrated visual wheel spin
    float                AngularVelRad   = 0.0f;                    // [rad/s]
    float                WheelRadius     = 0.34f;                   // [m]

    float                SurfaceKelvin   = 293.15f;                 // [K]   - fast tread rubber layer
    float                CarcassKelvin   = 293.15f;                 // [K]   - bulk rubber carcass
    float                GasKelvin       = 293.15f;                 // [K]   - enclosed air
    float                AirMassKg       = 0.0f;                    // [kg]
    float                GaugePressurePa = 220000.0f;               // [Pa]
    float                GripFactor      = 1.0f;                    // [-]
    float                TreadMassKg     = 1.1f;                    // [kg]
    float                RemainingRubber = 2.0f;                    // [kg]

    float                SlipRatio       = 0.0f;                    // [-]
    float                SlipAngleRad    = 0.0f;                    // [rad]
    float                SlipSpeedMps    = 0.0f;                    // [m/s]
    float                SurfaceSlipMps  = 0.0f;                    // [m/s]
    float                SlidingPowerW   = 0.0f;                    // [W]
    float                CombustionPowerW= 0.0f;                    // [W]
    float                OverheatSeconds = 0.0f;                    // [s]

    float                RequestSeconds  = 0.0f;                    // [s]   - onset hysteresis timer
    float                QuietSeconds    = 0.0f;                    // [s]   - release hysteresis timer
    bool                 SmokingActive   = false;                   // [-]
    bool                 Ignited         = false;                   // [-]
    bool                 Burst           = false;                   // [-]
};

struct DynamicFireLight
{
    std::array<float, 3> PositionWorld = { 0.0f, 0.0f, 0.5f };      // [m] - Frontier Z-up world coordinates
    float                RadiusMetres  = 2.5f;                      // [m]
    std::array<float, 3> RadianceLinear= { 1.0f, 0.46f, 0.14f };    // [-] - linear RGB intensity
};

//----------------------------------------------------------------------------------------------------------------------
//                                                TYRE SMOKE & FIRE SOLVER
//----------------------------------------------------------------------------------------------------------------------

class TyreSmokeIntake
{
  public:
    TyreSmokeIntake();

    void ResetCold() noexcept;
    void ForceIgniteDrivenTyres() noexcept;

    void Advance(const DeployableUnit& Vehicle, float DtSeconds, float GroundHeightZ = 0.0f);

    // Projects active wheel contact-patch, arch, and burning-tyre sources into a Pyro domain anchored at DomainOriginWorld.
    int ProjectIntoDomain(const std::array<float, 3>& DomainOriginWorld, const PyroBoundsBox& Bounds,
                          PyroExternalSource* OutSources, int MaxSources,
                          std::vector<DynamicFireLight>& OutFireLights) const;

    // Smoothly adapts optical albedo & fire intensity of the tyre plume domain between white rubber smoke and black burning-rubber soot.
    void AdaptTyreDomainAppearance(GasSettings& InOutSettings) const noexcept;

    TyreIgnitionPolicy IgnitionPolicy  = TyreIgnitionPolicy::Diagnostic;
    bool               BurnoutLineLock = false;   // [-] - holds front axle while spinning rear tyres
    float              DischargeGain   = 1.0f;    // [-] - overall tyre smoke/fire yield multiplier
    float              SmokeYield      = 1.0f;    // [-]
    float              HeatYield       = 1.0f;    // [-]
    float              SootYield       = 1.0f;    // [-]

    [[nodiscard]] const std::array<WheelThermalReading, 4>& QueryWheels() const noexcept { return Wheels; }
    [[nodiscard]] int QueryActiveSmokingWheelCount() const noexcept;
    [[nodiscard]] bool HasAnyBurningTyre() const noexcept;
    [[nodiscard]] float QueryPeakSurfaceCelsius() const noexcept;
    [[nodiscard]] float QueryTotalSlidingPowerKw() const noexcept;

  private:
    void SolvePacejkaCombined(float SlipRatio, float SlipAngle, float NormalLoadN,
                              float& OutFx, float& OutFy) const noexcept;
    void IntegrateWheelThermal(WheelThermalReading& Wheel, float AirspeedMps, float DtSeconds) noexcept;
    bool AdvanceSmokeOnset(WheelThermalReading& Wheel, bool InContact, float DtSeconds) noexcept;

    std::array<WheelThermalReading, 4> Wheels{};
    std::array<float, 3>               PreviousVehiclePos = { 0.0f, 0.0f, 0.53f };
    float                              PreviousYawDeg     = 0.0f;
    bool                               HasPreviousPose    = false;
    float                              ElapsedSeconds     = 0.0f;
};

} // namespace Frontier
