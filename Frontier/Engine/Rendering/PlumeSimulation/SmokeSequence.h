/*==========================================================================================================================================
                                                         SMOKESEQUENCE.H
==========================================================================================================================================*/
// 🧩 The vehicle's tyre plume, per refresh: the editor's gas mapped to settings, telemetry projected into sources, the schedule advanced, the fire
//    lights assigned and summed. A port of SmokeSequence.js (the adapter over PlumeSpace and PlumeSourceProjection); PlumeSimulationSequence
//    holds the device half.

#pragma once

#include "PlumeSchedule.h"

#include "Frontier/Engine/PhysicalDynamics/FluidDynamics/GasParameterCatalogue.h"
#include "Frontier/Engine/PhysicalDynamics/FluidDynamics/PlumeSourceProjection.h"
#include "Frontier/Engine/PhysicalDynamics/FluidDynamics/PlumeTelemetryProjection.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace Frontier::PlumeSimulation
{

using FluidDynamics::GasParameters;
using FluidDynamics::PlumeVehicleBody;
using FluidDynamics::PlumeWheelReading;
using FluidDynamics::PlumeWheelThermal;

// 📝 An emitter the host keeps beside the tyres: editor, fracture and burst plumes. The first four entries of the host's list are the tyres'
//    own sources and are skipped, so they are not duplicated.
struct PlumeEmission
{
    bool    Active    = false;
    Vector3 Position{};
    double  Radius    = 0.0;                    // 0 reads as 0.3
    Vector3 Velocity{0.0, 0.0, 1.0};
    double  SmokeRate = 0.0;
    double  HeatRate  = 0.0;
    double  FlameRate = 0.0;
    double  SootRate  = 0.0;
};

struct SmokeAdvanceInput
{
    bool                            Enabled = true;
    const PlumeVehicleBody*         Vehicle = nullptr;
    Vector3                         LightDirection{0.0, 0.0, 1.0};
    const GasParameters*            Gas = nullptr;
    const std::vector<PlumeWheelReading>* Readings = nullptr;
    const std::vector<PlumeWheelThermal>* Thermal  = nullptr;
    const std::vector<PlumeEmission>*     Emissions = nullptr;
    int32_t                         Inspection = 0;
};

struct SmokeMetrics
{
    uint32_t Sources     = 0;
    uint32_t Ticks       = 0;
    uint32_t Relocations = 0;
};

// 📝 The brightest fire light's stand-in the host lights the scene with: the sum of the lamps' red radiance at their weighted centre.
struct SmokeFireLight
{
    Vector3 Position{};
    double  Intensity = 0.0;
    Vector3 Colour{1.0, 0.46, 0.14};
};

class SmokeSequence
{
public:
    // 📝 GradeName: low, medium or high; anything else reads as high.
    explicit SmokeSequence(std::string_view GradeName);

    void Advance(double Seconds, const SmokeAdvanceInput& Input);
    void Clear();

    [[nodiscard]] PlumeSchedule&                          AccessSchedule() noexcept { return Schedule_; }
    [[nodiscard]] const PlumeSchedule&                    QuerySchedule() const noexcept { return Schedule_; }
    [[nodiscard]] FluidDynamics::PlumeSourceProjection&   AccessSources() noexcept { return Sources_; }
    [[nodiscard]] const FluidDynamics::PlumeSourceProjection& QuerySources() const noexcept { return Sources_; }
    [[nodiscard]] const SmokeMetrics&                     QueryMetrics() const noexcept { return Metrics_; }
    [[nodiscard]] const SmokeFireLight&                   QueryFireLight() const noexcept { return FireLight_; }

private:
    PlumeSchedule                         Schedule_;
    FluidDynamics::PlumeSourceProjection  Sources_;
    SmokeMetrics                          Metrics_{};
    SmokeFireLight                        FireLight_{};
};

} // namespace Frontier::PlumeSimulation
