/*==========================================================================================================================================
                                                        SMOKESEQUENCE.CPP
==========================================================================================================================================*/
// 🧩 See SmokeSequence.h.

#include "SmokeSequence.h"

#include <algorithm>

namespace Frontier::PlumeSimulation
{

namespace
{

const PlumeGrade& ResolveGrade(std::string_view GradeName)
{
    const auto& Grades = FluidDynamics::QueryPlumeGrades();
    if (GradeName == "low") { return Grades[0]; }
    if (GradeName == "medium") { return Grades[1]; }
    return Grades[2];
}

} // namespace

SmokeSequence::SmokeSequence(std::string_view GradeName) : Schedule_(ResolveGrade(GradeName))
{
}

void SmokeSequence::Advance(double Seconds, const SmokeAdvanceInput& Input)
{
    const GasParameters&  Gas      = *Input.Gas;
    PlumeSettings         Settings = FluidDynamics::MapPlumeSettings(Gas, Input.Enabled);
    Settings.Inspection            = Input.Inspection;
    const double Step = std::min(0.1, Seconds * Gas.TimeScale);
    FluidDynamics::PlumeProjectionOptions Options;
    Options.EmissionRate = Settings.PlumeEmissionRate;
    Options.SmokeYield   = Settings.PlumeDensity;
    Options.HeatYield    = Settings.PlumeHeat;
    Options.SootYield    = Settings.PlumeSoot;
    Options.FireLight    = Settings.PlumeFireLight;
    Sources_.Project(FluidDynamics::ProjectPlumeTelemetry(*Input.Vehicle, *Input.Readings, *Input.Thermal), Step, Options);
    // Preserve editor/fracture/burst plumes without duplicating the four tyre sources.
    if (Input.Emissions != nullptr)
    {
        for (size_t Index = 4; Index < Input.Emissions->size(); ++Index)
        {
            const PlumeEmission& Emission = (*Input.Emissions)[Index];
            if (!Emission.Active || Sources_.QueryCount() >= 24)
            {
                continue;
            }
            Sources_.AppendRecord({static_cast<float>(Emission.Position[0]), static_cast<float>(Emission.Position[1]), static_cast<float>(Emission.Position[2]),
                                   static_cast<float>(Emission.Radius != 0.0 ? Emission.Radius : 0.3),
                                   static_cast<float>(Emission.Velocity[0]), static_cast<float>(Emission.Velocity[1]), static_cast<float>(Emission.Velocity[2]), 0.0f,
                                   static_cast<float>(Emission.SmokeRate), static_cast<float>(Emission.HeatRate), static_cast<float>(Emission.FlameRate),
                                   static_cast<float>(Emission.SootRate)});
        }
    }
    const std::array<int32_t, 2> Prior = Schedule_.QueryOrigin();
    Schedule_.AssignPressureSweeps(Gas.PressureSweeps);
    Schedule_.Schedule(Step, Seconds, Input.Vehicle->Position, Sources_, Settings, Input.LightDirection);
    static const std::vector<FluidDynamics::PlumeFireLight> None;
    const std::vector<FluidDynamics::PlumeFireLight>&       Lamps = Input.Enabled ? Sources_.QueryLights() : None;
    Schedule_.AssignFireLights(Lamps);
    Metrics_.Sources = Sources_.QueryCount();
    Metrics_.Ticks   = Schedule_.QueryStepsTaken();
    if (Prior != Schedule_.QueryOrigin())
    {
        ++Metrics_.Relocations;
    }
    double Total = 0.0;
    for (const FluidDynamics::PlumeFireLight& Lamp : Lamps)
    {
        Total += Lamp.Radiance[0];
    }
    FireLight_.Intensity = Total;
    if (Total > 0)
    {
        for (size_t Axis = 0; Axis < 3; ++Axis)
        {
            double Sum = 0.0;
            for (const FluidDynamics::PlumeFireLight& Lamp : Lamps)
            {
                Sum += Lamp.Position[Axis] * Lamp.Radiance[0];
            }
            FireLight_.Position[Axis] = Sum / Total;
        }
    }
}

void SmokeSequence::Clear()
{
    Schedule_.Clear();
    Sources_.Reset();
    FireLight_.Intensity = 0.0;
}

} // namespace Frontier::PlumeSimulation
