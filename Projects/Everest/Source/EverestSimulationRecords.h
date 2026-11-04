/*==========================================================================================================================================
                                                   EVERESTSIMULATIONRECORDS.H
==========================================================================================================================================*/
// 🧩 Project-defined damped harmonic oscillator simulation record demonstrating custom C++ SceneRecordBase + multi-inspector composition.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Interface/InspectorRuntime.h"
#include "Engine/Interface/NativeInspectors.h"
#include "Engine/Scene/EngineConfigurations.h"
#include "Engine/Scene/SceneWorld.h"

#include <cmath>
#include <string>

namespace Everest
{

struct HarmonicOscillatorConfiguration : Frontier::RevisionCounter
{
    bool  Enabled                = true;
    float StiffnessNewtonPerM    = 180.0f; // [N/m]
    float DampingRatio           = 0.14f;  // [dimensionless zeta]
    float InitialDisplacementM   = 0.65f;  // [m]
    float DriveFrequencyHz       = 1.0f;   // [Hz]
    float CurrentDisplacementM   = 0.65f;  // [m]
    float CurrentVelocityMps     = 0.0f;   // [m/s]

    [[nodiscard]] float ComputeNaturalFrequencyRadSec(float MassKilograms) const noexcept
    {
        return std::sqrt(StiffnessNewtonPerM / std::max(1.0e-3f, MassKilograms));
    }

    [[nodiscard]] float ComputeStoredElasticEnergyJoules() const noexcept
    {
        return 0.5f * StiffnessNewtonPerM * CurrentDisplacementM * CurrentDisplacementM;
    }
};

#if FRONTIER_EDITOR

class HarmonicOscillatorInspector final : public Frontier::RecordInspector
{
  public:
    HarmonicOscillatorInspector(HarmonicOscillatorConfiguration&     InOscillator,
                                const Frontier::MassConfiguration&   InMass) noexcept
        : Oscillator(InOscillator)
        , Mass(InMass)
    {
    }

    [[nodiscard]] const char* QueryEyebrowText() const noexcept override { return "PROJECT · HARMONIC OSCILLATOR"; }
    [[nodiscard]] const char* QuerySectionNote() const noexcept override { return "2nd-Order Spring-Damper"; }
    [[nodiscard]] bool        QueryToggleSupported() const noexcept override { return true; }
    [[nodiscard]] bool        QueryToggleEnabled() const noexcept override { return Oscillator.Enabled; }
    void                      ConfigureToggle(bool Enabled) override { Oscillator.Enabled = Enabled; Oscillator.AdvanceRevision(); }
    [[nodiscard]] uint64_t    QueryRevision() const noexcept override { return Oscillator.Revision; }
    void                      ConstructInspector() override;

  private:
    HarmonicOscillatorConfiguration&   Oscillator;
    const Frontier::MassConfiguration& Mass;
};

#endif // FRONTIER_EDITOR

class HarmonicOscillatorRecord final : public Frontier::SceneRecordBase
{
  public:
    explicit HarmonicOscillatorRecord(std::string InDisplayName);

    [[nodiscard]] Frontier::SpatialConfiguration&       AccessSpatial() noexcept { return Spatial; }
    [[nodiscard]] Frontier::PhysicsConfiguration&       AccessPhysics() noexcept { return Physics; }
    [[nodiscard]] HarmonicOscillatorConfiguration&      AccessOscillator() noexcept { return Oscillator; }
    [[nodiscard]] const HarmonicOscillatorConfiguration& QueryOscillator() const noexcept { return Oscillator; }

    void SerializeConfiguration(toml::table& OutTable) const override;
    void DeserializeConfiguration(const toml::table& InTable) override;
    void StepSimulation(float DeltaSeconds) override;

  private:
    Frontier::SpatialConfiguration  Spatial{};
    Frontier::PhysicsConfiguration  Physics{};
    HarmonicOscillatorConfiguration Oscillator{};
};

void RegisterEverestSimulationTypes(Frontier::SceneWorld& World);

} // namespace Everest
