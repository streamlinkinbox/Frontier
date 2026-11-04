/*============================================================================================================================================
                                                           INDUCTIONCHARGER.CPP
============================================================================================================================================*/
// 🧩 Unified forced induction advancement: NA passthrough, first-order turbo (demo TurbochargerSheet law),
//    belt-driven superchargers (Project Drive Drivetrain::StepSupercharger law with bypass valve and parasitic tax).

#include "Engine/Physics/InductionCharger.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{
    inline double ClampMagnitude(double V, double Lo, double Hi) noexcept { return V < Lo ? Lo : (V > Hi ? Hi : V); }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  CONFIGURATION FACTORIES
//------------------------------------------------------------------------------------------------------------------------

InductionChargerConfiguration StockTurboChargerConfiguration()
{
    InductionChargerConfiguration C;
    C.Arrangement = InductionArrangement::TurboFirstOrder;
    return C;
}

InductionChargerConfiguration TwinScrewSuperchargerConfiguration()
{
    InductionChargerConfiguration C;
    C.Arrangement = InductionArrangement::SuperchargerTwinScrew;
    return C;
}

InductionChargerConfiguration CentrifugalSuperchargerConfiguration()
{
    InductionChargerConfiguration C;
    C.Arrangement         = InductionArrangement::SuperchargerCentrifugal;
    C.CentrifugalRpmSlope = 1.6;   // [-] centrifugal boost rise exponent
    return C;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

InductionCharger::InductionCharger(const InductionChargerConfiguration& InConfiguration)
    : Configuration(InConfiguration)
{
}

void InductionCharger::AssignConfiguration(const InductionChargerConfiguration& InConfiguration)
{
    Configuration = InConfiguration;
    Reset();
}

void InductionCharger::Reset() noexcept
{
    BoostBar = 0.0;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       ADVANCE
//------------------------------------------------------------------------------------------------------------------------

InductionChargeReading InductionCharger::Advance(double EngineRPM, double Throttle, double Seconds) noexcept
{
    InductionChargeReading Reading;
    const double τdecay = std::fmin(1.0, Seconds / Configuration.TurboSpoolTimeConstant);

    switch (Configuration.Arrangement)
    {
    case InductionArrangement::NaturallyAspirated:
        BoostBar                 = 0.0;
        Reading.BoostBar         = 0.0;
        Reading.TorqueMultiplier = 1.0;
        break;

    case InductionArrangement::TurboFirstOrder:
    {
        // 📝 Identical law to the demo VehicleSolver stock path: off-throttle decay toward 0, otherwise a
        //    throttle × wastegate × rpm-gated target approached with the same first-order constant.
        const double Gate = ClampMagnitude((EngineRPM - Configuration.TurboBoostRpmStart)
                                     / std::fmax(1.0, Configuration.TurboBoostRpmFull - Configuration.TurboBoostRpmStart),
                                     0.0, 1.0);
        const double BoostTarget = Throttle * Configuration.TurboWastegateMaxBoost * Gate;
        if (Throttle <= 0.02)
        {
            BoostBar += (0.0 - BoostBar) * τdecay;
        }
        else
        {
            BoostBar += (BoostTarget - BoostBar) * τdecay;
        }
        Reading.BoostBar         = BoostBar;
        Reading.TorqueMultiplier = SampleInductionCurve(Configuration.TurboMultiplierBoost,
                                                        Configuration.TurboMultiplierScale,
                                                        Configuration.TurboMultiplierPoints, BoostBar);
        Reading.ChargerRPM       = 0.0;
        Reading.BypassVenting    = Throttle <= 0.02 && BoostBar > 1.0e-6;
        break;
    }

    case InductionArrangement::SuperchargerRoots:
    case InductionArrangement::SuperchargerTwinScrew:
    case InductionArrangement::SuperchargerCentrifugal:
    {
        // 📝 Belt-driven: charger speed follows the crank instantly; boost target is the displacement curve capped at
        //    the tune ceiling and gated by throttle; the recirculation valve slews delivery at BypassValveRate.
        const double ClampedThrottle = ClampMagnitude(Throttle, 0.0, 1.0);
        const double ChargerRpm      = EngineRPM * Configuration.ChargerDriveRatio;
        const double TuneCeiling     = Configuration.RaceTune ? Configuration.MaxBoostRaceBar
                                                              : Configuration.MaxBoostBaseBar;
        double RawBoost              = SampleInductionCurve(Configuration.BoostCurveRpm, Configuration.BoostCurveBar,
                                                            Configuration.BoostCurvePoints, ChargerRpm);
        if (Configuration.Arrangement == InductionArrangement::SuperchargerCentrifugal)
        {
            const double Reference = Configuration.BoostCurveRpm[std::max(0, Configuration.BoostCurvePoints - 2)];
            RawBoost *= std::pow(ClampMagnitude(ChargerRpm / std::fmax(1.0, Reference), 0.0, 1.25),
                                 Configuration.CentrifugalRpmSlope - 1.0);
        }
        const double TargetBoost = std::fmin(RawBoost, TuneCeiling) * ClampedThrottle;
        const double ΔBoostMax   = Configuration.BypassValveRateBarPerSecond * Seconds;
        BoostBar                += ClampMagnitude(TargetBoost - BoostBar, -ΔBoostMax, ΔBoostMax);
        BoostBar                 = ClampMagnitude(BoostBar, 0.0, TuneCeiling);

        const double DragFull    = SampleInductionCurve(Configuration.ParasiticCurveRpm,
                                                        Configuration.ParasiticCurveNewtonMetre,
                                                        Configuration.ParasiticCurvePoints, ChargerRpm);

        Reading.BoostBar                 = BoostBar;
        Reading.TorqueMultiplier         = SampleInductionCurve(Configuration.MultiplierCurveBar,
                                                                Configuration.MultiplierCurveScale,
                                                                Configuration.MultiplierCurvePoints, BoostBar);
        Reading.ParasiticDragNewtonMetre = DragFull * (Configuration.ParasiticLossFloor
                                                       + (1.0 - Configuration.ParasiticLossFloor) * ClampedThrottle);
        Reading.ChargerRPM               = ChargerRpm;
        Reading.BypassVenting            = TargetBoost < BoostBar - 1.0e-9;
        break;
    }
    }
    return Reading;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     CURVE LOOKUP
//------------------------------------------------------------------------------------------------------------------------

double InductionCharger::SampleInductionCurve(const double* Xs, const double* Ys, int Count, double X) noexcept
{
    if (Count <= 0 || Xs == nullptr || Ys == nullptr) return 0.0;
    if (X <= Xs[0]) return Ys[0];
    if (X >= Xs[Count - 1]) return Ys[Count - 1];
    int Lo = 0, Hi = Count - 1;
    while (Hi - Lo > 1)
    {
        const int Mid = (Lo + Hi) >> 1;
        (Xs[Mid] <= X ? Lo : Hi) = Mid;
    }
    const double T = (Xs[Hi] == Xs[Lo]) ? 0.0 : (X - Xs[Lo]) / (Xs[Hi] - Xs[Lo]);
    return Ys[Lo] + T * (Ys[Hi] - Ys[Lo]);
}

} // namespace Frontier
