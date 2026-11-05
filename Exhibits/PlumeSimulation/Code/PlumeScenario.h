/*==========================================================================================================================================
                                                        PLUMESCENARIO.H
==========================================================================================================================================*/
// 🧩 The plume exhibit's scripted inputs, in C++: the burnout of 420 refreshes and the fade of 1000, repeating PlumeScenario.mjs generator for
//    generator (Mulberry32, Tri, Ramp — integer operations and + − × ÷ only, so the doubles are the script's own bit for bit). The validation
//    hashes what it generates against the exporter's; the capture drives the device with it.

#pragma once

#include "Frontier/Engine/Rendering/PlumeSimulation/SmokeSequence.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
#include <vector>

namespace PlumeScenario
{

using Frontier::FluidDynamics::ApplyGasPreset;
using Frontier::FluidDynamics::FindGasParameter;
using Frontier::FluidDynamics::GasParameters;
using Frontier::FluidDynamics::GasPreset;
using Frontier::FluidDynamics::PlumeVehicleBody;
using Frontier::FluidDynamics::PlumeWheelReading;
using Frontier::FluidDynamics::PlumeWheelThermal;
using Frontier::FluidDynamics::QueryGasPresets;
using Frontier::FluidDynamics::Vector3;
using Frontier::PlumeSimulation::PlumeEmission;

class Mulberry32
{
public:
    explicit Mulberry32(uint32_t Seed) : Seed_(Seed) {}

    double operator()()
    {
        Seed_ += 0x6D2B79F5u;
        uint32_t T = (Seed_ ^ (Seed_ >> 15)) * (1u | Seed_);
        T          = (T + (T ^ (T >> 7)) * (61u | T)) ^ T;
        return static_cast<double>(T ^ (T >> 14)) / 4294967296.0;
    }

private:
    uint32_t Seed_;
};

inline double Tri(int Step, int Period)
{
    const double X = static_cast<double>(Step % Period) / Period;
    return X < 0.5 ? 2 * X : 2 - 2 * X;
}

inline double Ramp(int Step, int From, int To)
{
    return std::min(1.0, std::max(0.0, static_cast<double>(Step - From) / (To - From)));
}

inline constexpr int      BurnoutSteps = 420;
inline constexpr double   BurnoutStepDt[8] = {1.0 / 60, 1.0 / 60, 1.0 / 120, 1.0 / 60, 1.0 / 30, 1.0 / 60, 0.08, 1.0 / 60};
inline constexpr Vector3  LightDirection   = {0.3, 0.2, 0.9327379053088815};

struct GasChoice
{
    const char*                                         Preset;
    std::vector<std::pair<std::string_view, double>>    Overrides;
};

inline GasChoice ChooseGas(int Step)
{
    if (Step >= 380) { return {"Crosswind", {{"TimeScale", 2.0}, {"Emission", 0.0}, {"PressureSweeps", 40}}}; }
    if (Step >= 300) { return {"Crosswind", {{"TimeScale", 0.5}}}; }
    if (Step >= 200) { return {"Inferno", {{"PressureSweeps", 13}}}; }
    if (Step >= 100) { return {"Thick burnout", {}}; }
    return {"Tyre smoke", {}};
}

inline GasParameters ConstructGas(const GasChoice& Choice)
{
    GasParameters Gas;
    for (const GasPreset& Preset : QueryGasPresets())
    {
        if (Preset.Name == Choice.Preset)
        {
            Gas = ApplyGasPreset(Preset);
        }
    }
    for (const auto& [Key, Value] : Choice.Overrides)
    {
        Gas.*(FindGasParameter(Key)->Field) = Value;
    }
    return Gas;
}

struct BurnoutRefresh
{
    double                         Seconds = 0.0;
    PlumeVehicleBody               Vehicle;
    std::vector<PlumeWheelReading> Readings;
    std::vector<PlumeWheelThermal> Thermal;
    std::vector<PlumeEmission>     Emissions;
    bool                           Enabled = true;
    GasParameters                  Gas;
};

inline BurnoutRefresh ConstructBurnout(int Step, const std::array<double, 8>& Noise)
{
    BurnoutRefresh Refresh;
    Refresh.Seconds = BurnoutStepDt[Step % 8];
    const double T = static_cast<double>(Step) / 60;
    const double Qx = 0.04 * Tri(Step, 110) - 0.02, Qy = 0.03 * Tri(Step, 70) - 0.015;
    const double A = 0.6 * Tri(Step, 200) - 0.3, B = 1.0, Length = std::sqrt(Qx * Qx + Qy * Qy + A * A + B * B);
    const Vector3 Position = {6 * T, 0.8 * Tri(Step, 300), 0.55};
    const Vector3 Velocity = {6 + 2 * Tri(Step, 90), 0.5 * Tri(Step, 45) - 0.25, 0};
    Refresh.Vehicle.Position       = Position;
    Refresh.Vehicle.LinearVelocity = Velocity;
    Refresh.Vehicle.Orientation    = {Qx / Length, Qy / Length, A / Length, B / Length};
    for (int Index = 0; Index < 4; ++Index)
    {
        const bool Front = Index < 2;
        Refresh.Vehicle.Wheels.push_back({(Front ? 0.35 : 0.0) * (Tri(Step, 150) - 0.5) * 2, 0.34 + 0.01 * Index, 20 + 15 * Tri(Step, 120) + Index});
        const Vector3 Contact = {Position[0] + (Front ? 1.3 : -1.3), Position[1] + ((Index & 1) ? -0.8 : 0.8), 0.0};
        PlumeWheelReading Reading;
        Reading.ContactFraction = (Step % 97 < 8 && Index == 1) ? 0.3 : ((Step % 101 < 6 && Index == 2) ? 0.55 : 0.92);
        Reading.OnDebris        = Step % 131 < 5 && Index == 0;
        Reading.Shredded        = Index == 3 && Step >= 330;
        Reading.SlidingPower    = (Front ? 3000 : 22000) * Ramp(Step, 20 + 10 * Index, 80 + 10 * Index) * (1 - Ramp(Step, 360, 400)) * (0.35 + 0.65 * Noise[Index]);
        Reading.SlipSpeed       = 0.2 + 5.8 * Ramp(Step, 15 + 10 * Index, 70 + 10 * Index) * (0.5 + 0.5 * Noise[4 + Index]);
        Reading.ContactPoint    = Contact;
        Reading.ContactVelocity = {Velocity[0] * 0.9, Velocity[1], 0};
        Reading.HubPosition     = {Contact[0], Contact[1], 0.34};
        Refresh.Readings.push_back(Reading);
        PlumeWheelThermal Heat;
        Heat.Ignited         = (Index == 2 && Step >= 150 && Step < 330) || (Index == 0 && Step >= 260 && Step < 300) || (Index == 3 && Step >= 340 && Step < 380);
        Heat.CombustionPower = 8000 + 12000 * Noise[Index];
        Heat.Surface         = 300 + 350 * Ramp(Step, 10, 200);
        Heat.Burst           = Index == 2 && Step >= 200 && Step < 204;
        Refresh.Thermal.push_back(Heat);
    }
    for (int Slot = 0; Slot < 6; ++Slot)
    {
        PlumeEmission Emission;
        Emission.Active    = Slot == 4 ? (Step >= 120 && Step < 200) : Slot == 5 ? (Step >= 250 && Step < 300) : true;
        Emission.Position  = {Position[0] + Slot, 2.0 + 0.1 * Slot, 0.3 * Slot};
        Emission.Radius    = Slot == 5 ? 0 : 0.2 + 0.05 * Slot;
        Emission.Velocity  = {0.1 * Slot, 0, 1.0 + Slot};
        Emission.SmokeRate = 2.0 + Slot;
        Emission.HeatRate  = 1.5 * Slot;
        Emission.FlameRate = Slot == 4 ? 3.0 : 0;
        Emission.SootRate  = 0.5 * Slot;
        Refresh.Emissions.push_back(Emission);
    }
    Refresh.Enabled = !(Step >= 400 && Step < 410);
    Refresh.Gas     = ConstructGas(ChooseGas(Step));
    return Refresh;
}

// 📝 Fade: the burnout's first 60 refreshes, 840 of silence at 16 Hz — the plume's twelve seconds of decay end exactly on a refresh (191 × 1/16 s and
//    four of 1/64 s, the step clock holding a partial step when residency stops) — and the burnout's script again, so residency stops and restarts.
inline BurnoutRefresh ConstructFade(int Step, const std::array<double, 8>& Noise)
{
    BurnoutRefresh Refresh = ConstructBurnout(Step % 300, Noise);
    Refresh.Seconds = (Step >= 251 && Step <= 254) ? 1.0 / 64 : 0.0625;
    Refresh.Gas     = ConstructGas({"Tyre smoke", {}});
    Refresh.Enabled = true;
    if (Step >= 60 && Step < 900)
    {
        for (PlumeWheelReading& Reading : Refresh.Readings) { Reading.SlidingPower = 0; }
        for (PlumeWheelThermal& Heat : Refresh.Thermal) { Heat.Ignited = false; }
        for (PlumeEmission& Emission : Refresh.Emissions) { Emission.Active = false; }
    }
    return Refresh;
}

} // namespace PlumeScenario
