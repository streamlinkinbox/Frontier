/*============================================================================================================================================
                                                            INDUCTIONCHARGER.H
============================================================================================================================================*/
// 🧩 Unified forced induction: naturally aspirated, first-order turbo, and belt-driven superchargers (Roots /
//    TwinScrew / Centrifugal) feeding one torque multiplier, boost gauge and parasitic crank drag.

#pragma once

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  INDUCTION ARRANGEMENT
//------------------------------------------------------------------------------------------------------------------------

// 📝 Supercharger drive categories follow GRIT's ESuperchargerDriveType (SuperchargerSpecifications.h): Roots and
//    TwinScrew are positive displacement (near rpm-linear boost), Centrifugal builds boost on a steeper rpm slope —
//    all belt-driven off the crank, so boost tracks engine speed instantly with no exhaust spool lag.
enum class InductionArrangement
{
    NaturallyAspirated      = 0,  // [-] no forced induction, multiplier stays 1
    TurboFirstOrder         = 1,  // [-] exhaust turbo, the demo's first-order spool law (TurbochargerSheet parity)
    SuperchargerRoots       = 2,  // [-] Roots positive-displacement blower
    SuperchargerTwinScrew   = 3,  // [-] twin-screw positive-displacement blower (GRIT preset-1 defaults)
    SuperchargerCentrifugal = 4   // [-] crank-driven centrifugal compressor (steeper rpm rise)
};

//------------------------------------------------------------------------------------------------------------------------
//                                              INDUCTION CHARGER CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Curves are open point arrays (X ascends); SampleInductionCurve clamps at both ends and lerp-interpolates —
//    the same lookup law the Project Drive Drivetrain uses for its maps.
struct InductionChargerConfiguration
{
    InductionArrangement Arrangement = InductionArrangement::NaturallyAspirated; // [-]

    // Turbocharger block (TurboFirstOrder path; defaults = the demonstration's TurbochargerSheet).
    double TurboSpoolTimeConstant  = 0.55;   // [s]   first-order boost lag τ
    double TurboWastegateMaxBoost  = 1.2;    // [bar] wastegate ceiling
    double TurboBoostRpmStart      = 1500.0; // [rpm] boost gate opens
    double TurboBoostRpmFull       = 4000.0; // [rpm] boost gate fully open (Start + 2500)
    int    TurboMultiplierPoints   = 4;      // [-] count used below
    double TurboMultiplierBoost[8] = { 0.0, 0.5, 1.0, 1.2, 0.0, 0.0, 0.0, 0.0 };     // [bar]
    double TurboMultiplierScale[8] = { 1.0, 1.3, 1.7, 1.85, 1.0, 1.0, 1.0, 1.0 };    // [-]

    // Supercharger block (Project Drive / GRIT preset-1 twin-screw defaults).
    int    BoostCurvePoints          = 6;    // [-]
    double BoostCurveRpm[8]          = { 2000.0, 5000.0, 10000.0, 15000.0, 20000.0, 24000.0, 0.0, 0.0 };   // [rpm] charger speed
    double BoostCurveBar[8]          = { 0.0, 0.15, 0.38, 0.62, 0.82, 0.95, 0.0, 0.0 };                    // [bar]
    int    MultiplierCurvePoints     = 4;    // [-]
    double MultiplierCurveBar[8]     = { 0.0, 0.4, 0.8, 1.2, 0.0, 0.0, 0.0, 0.0 };                        // [bar]
    double MultiplierCurveScale[8]   = { 1.0, 1.25, 1.55, 1.75, 1.0, 1.0, 1.0, 1.0 };                      // [-]
    int    ParasiticCurvePoints      = 6;    // [-]
    double ParasiticCurveRpm[8]      = { 2000.0, 5000.0, 10000.0, 15000.0, 20000.0, 24000.0, 0.0, 0.0 };   // [rpm] charger speed
    double ParasiticCurveNewtonMetre[8] = { 5.0, 10.0, 24.0, 45.0, 68.0, 82.0, 0.0, 0.0 };                 // [N·m] crank load
    double ChargerDriveRatio         = 3.2;  // [-]   charger rpm = engine rpm × ratio
    double MaxBoostBaseBar           = 0.8;  // [bar] pump-fuel tune ceiling
    double MaxBoostRaceBar           = 1.2;  // [bar] race-fuel tune ceiling
    bool   RaceTune                  = false;// [-]
    double BypassValveRateBarPerSecond = 8.0;// [bar/s] recirculation valve slew rate
    double ParasiticLossFloor        = 0.08; // [-]   residual drag fraction when fully bypassed
    double CentrifugalRpmSlope       = 1.6;  // [-]   boost ∝ (rpm/ref)^slope shaping for the centrifugal blower
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    INDUCTION READING
//------------------------------------------------------------------------------------------------------------------------

// 📝 One advancement's outcome; BoostBar also feeds the vehicle's boost gauge/telemetry field.
struct InductionChargeReading
{
    double BoostBar                 = 0.0;  // [bar]  delivered manifold boost
    double TorqueMultiplier         = 1.0;  // [-]    engine torque scale
    double ParasiticDragNewtonMetre = 0.0;  // [N·m]  crank load taxed by the supercharger (0 for NA/turbo)
    double ChargerRPM               = 0.0;  // [rpm]  turbo shaft or charger rotor speed
    bool   BypassVenting            = false;// [-]    recirculation valve is blowing charge off-throttle
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    INDUCTION CHARGER
//------------------------------------------------------------------------------------------------------------------------

class InductionCharger
{
public:
    InductionCharger() = default;
    explicit InductionCharger(const InductionChargerConfiguration& InConfiguration);

    void AssignConfiguration(const InductionChargerConfiguration& InConfiguration);
    [[nodiscard]] const InductionChargerConfiguration& QueryConfiguration() const noexcept { return Configuration; }

    void Reset() noexcept;

    // 📝 Advance one host substep Δτ. Turbo: the demo's first-order law (decay off-throttle; throttle × wastegate ×
    //    rpm-gate target otherwise). Supercharger: raw displacement boost capped at the tune ceiling and gated by
    //    throttle, the bypass valve slewing at BypassValveRate; parasitic drag follows the drag curve and falls to
    //    the ParasiticLossFloor share when the throttle closes.
    InductionChargeReading Advance(double EngineRPM, double Throttle, double Seconds) noexcept;

    [[nodiscard]] static double SampleInductionCurve(const double* Xs, const double* Ys, int Count, double X) noexcept;

private:
    InductionChargerConfiguration Configuration{};
    double BoostBar = 0.0;   // [bar] integrated boost (turbo lag or bypass-slewed supercharger)
};

// 📝 Stock-equivalent turbo defaults: the demonstration's TurbochargerSheet, so TurboFirstOrder reproduces the
//    unmodified solver's boost trajectory exactly.
InductionChargerConfiguration StockTurboChargerConfiguration();

// 📝 GRIT preset-1 twin-screw defaults (Project Drive Drivetrain parity).
InductionChargerConfiguration TwinScrewSuperchargerConfiguration();

// 📝 Centrifugal variant: same maps, steeper boost build via CentrifugalRpmSlope shaping on the curve sample.
InductionChargerConfiguration CentrifugalSuperchargerConfiguration();

} // namespace Frontier
