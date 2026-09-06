//============================================================================================================================================
//                                                     FREEREVPOWERTRAIN.CPP
//============================================================================================================================================
// 🧩 See FreeRevPowertrain.h. Expression order follows the editor's JavaScript so the two rev identically for the same input.

#include "FreeRevPowertrain.h"
#include "../../../Engine/PlatformInterchange/SignalSections.h"

#include <algorithm>
#include <cmath>

namespace Frontier {

namespace {

constexpr double RadPerSecondToRpm = 9.549296585513721;   // [rpm per rad/s] 60 / 2π

} // namespace

double FreeRevPowertrain::TorqueShape(double AtRpm) const noexcept
{
    const double Tp = V.PeakTorqueRpm;
    if (AtRpm < Tp) { const double X = (Tp - AtRpm) / std::max(1.0, Tp - V.IdleRpm); return ClampReal(1.0 - 0.45 * X * X, 0.3, 1.0); }
    const double X = (AtRpm - Tp) / std::max(1.0, V.RedlineRpm - Tp);
    return ClampReal(1.0 - 0.28 * X * X, 0.3, 1.0);
}

void FreeRevPowertrain::Advance(double Δτ, double ThrottleInput) noexcept
{
    const double AtRpm    = Rpm;
    const double Pedal    = ClampReal(ThrottleInput, 0.0, 1.0);
    const double IdleAir  = ClampReal((V.IdleRpm + 40.0 - AtRpm) / 120.0, 0.0, 0.4);   // idle-speed governor
    const double Effective = std::max(std::pow(Pedal, 1.2), IdleAir);
    if (AtRpm >= V.RedlineRpm) Cut = true; else if (AtRpm < V.RedlineRpm - 120.0) Cut = false;
    const bool Overrun = Pedal < 0.02 && AtRpm > V.IdleRpm * 1.6;
    FuelOn = !Cut && !Overrun;
    const double Torque   = FuelOn ? V.PeakTorqueNm * TorqueShape(AtRpm) * Effective : 0.0;
    const double Friction = V.FrictionNm + V.FrictionPerKrpmNm * AtRpm * 0.001 + 8.0 * (1.0 - Pedal) * AtRpm * 0.001;
    const double OmegaDot = (Torque - Friction) / std::max(0.01, V.InertiaKgm2);
    Rpm      = ClampReal(AtRpm + OmegaDot * Δτ * RadPerSecondToRpm, V.IdleRpm * 0.5, V.RedlineRpm + 300.0);
    Throttle = Pedal;
    Load     = ClampReal(Torque / V.PeakTorqueNm, 0.0, 1.0);
}

PowertrainRecord FreeRevPowertrain::QueryRecord() const noexcept
{
    PowertrainRecord R;
    R.Rpm = Rpm; R.Throttle = Throttle; R.Load = Load;
    return R;
}

} // namespace Frontier
