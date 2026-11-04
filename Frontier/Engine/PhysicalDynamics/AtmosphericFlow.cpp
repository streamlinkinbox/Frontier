/*==========================================================================================================================================
                                                             ATMOSPHERICFLOW.CPP
==========================================================================================================================================*/
// 🧩 One evaluated field drives density advection and world vectors; meteorological bearing is not mistaken for travel azimuth.

#include "Engine/PhysicalDynamics/AtmosphericFlow.h"
#include <algorithm>
#include <cmath>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    FIELD CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

void ConfigureAtmosphericFlow(ForceFieldSpace                          &Field,
                              const ForceFieldConfiguration            &Controls,
                              const WindConfiguration                  &Wind,
                              double                                    ElapsedSeconds,
                              std::span<const ForceSourceConfiguration> Sources)
{
    PlanetaryWindProfile Profile;
    Profile.BearingRadians          = (Wind.DirectionDegrees - 90.0) * 3.14159265358979323846 / 180;
    Profile.SpeedMetersPerSecond    = Controls.Enabled && Wind.Enabled ? std::clamp<double>(Wind.SpeedMetresPerSecond, 0, 40) : 0;
    Profile.GustAmplitude           = std::clamp<double>(Wind.GustStrength, 0, 1);
    Profile.GustFrequencyHz         = std::clamp<double>(Wind.GustFrequencyHz, .01, 2);
    Profile.ShearPerKilometer       = std::clamp<double>(Wind.ShearPerKilometre, 0, 2);
    Profile.VeerRadiansPerKilometer = Wind.VeerDegreesPerKilometre * 3.14159265358979323846 / 180;
    Profile.SwirlStrength           = Controls.IncludeTurbulence ? std::clamp<double>(Wind.CurlNoiseTurbulence, 0, 1) : 0;
    Profile.SwirlScaleMeters        = std::clamp<double>(Wind.TurbulenceScaleMetres, 1, 1000);
    Profile.MesoscaleScaleMeters    = std::clamp<double>(Wind.MesoscaleScaleMetres, 0, 200000);
    Profile.MesoscaleStrength       = std::clamp<double>(Wind.MesoscaleStrength, 0, 1);
    Profile.Seed                    = Wind.Seed;
    Field.ConfigureEnabled(Controls.Enabled);
    Field.SetWindProfile(Profile);
    Field.SetGroundPlaneYMeters(Wind.GroundHeightMetres);
    Field.SetElapsedSeconds(std::isfinite(ElapsedSeconds) ? std::max(0.0, ElapsedSeconds) : 0);
    Field.ConfigureAuthoredSources(Sources, Controls.TimelineSeconds);
}

std::array<double, 3> EvaluateWorldFlow(const ForceFieldSpace &Field, const std::array<double, 3> &Position, bool Turbulence)
{
    const auto Sample = Field.EvaluateStepSample({Position[0], Position[2], -Position[1]}, Turbulence);
    return {Sample.AirVelocityMps.X, -Sample.AirVelocityMps.Z, Sample.AirVelocityMps.Y};
}

} // namespace Frontier
