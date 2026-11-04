/*==========================================================================================================================================
                                                  EVERESTSIMULATIONRECORDS.CPP
==========================================================================================================================================*/
// 🧩 Project-defined damped harmonic oscillator simulation record implementation.

#include "EverestSimulationRecords.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)
#include "Engine/Interface/DiagramControls.h"
#include "Engine/Interface/InspectorControls.h"
#endif

#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Everest
{

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

void HarmonicOscillatorInspector::ConstructInspector()
{
    const Frontier::InspectorPalette Palette = Frontier::InspectorPalette::Resolve();
    const Frontier::EditorTypefaces  Faces   = Frontier::EditorTypefaces::Resolve();

    const float OmegaN = Oscillator.ComputeNaturalFrequencyRadSec(Mass.MassKilograms);
    const float Zeta   = std::clamp(Oscillator.DampingRatio, 0.0f, 0.99f);
    const float OmegaD = OmegaN * std::sqrt(std::max(0.001f, 1.0f - Zeta * Zeta));

    std::array<float, 48> ResponseSamples{};
    for (size_t Index = 0; Index < ResponseSamples.size(); ++Index)
    {
        const float TimeSec  = static_cast<float>(Index) * 0.08f;
        const float Envelope = std::exp(-Zeta * OmegaN * TimeSec);
        const float Normalized = 0.5f + 0.45f * Envelope * std::cos(OmegaD * TimeSec);
        ResponseSamples[Index] = std::clamp(Normalized, 0.0f, 1.0f);
    }

    char Header[80]{};
    std::snprintf(Header, sizeof(Header), "TRANSIENT RESPONSE x(t) · wn=%.2f rad/s", OmegaN);
    Frontier::DiagramControls::ConstructCurvePlot(Palette, Faces, Header,
                                                  ResponseSamples.data(), ResponseSamples.size(),
                                                  Frontier::ParseHexSpectrum("#62c3a5", Palette.LiveGreen));

    bool Changed = false;
    Changed |= Frontier::InspectorControls::ConstructSliderRow(Palette, Faces, "##StiffK", "Spring Stiffness k", Oscillator.StiffnessNewtonPerM, 1.0f, 2000.0f, "%.1f N/m");
    Changed |= Frontier::InspectorControls::ConstructSliderRow(Palette, Faces, "##Zeta", "Damping Ratio zeta", Oscillator.DampingRatio, 0.0f, 1.5f, "%.3f");
    if (Frontier::InspectorControls::ConstructSliderRow(Palette, Faces, "##InitDisp", "Initial Amplitude", Oscillator.InitialDisplacementM, -5.0f, 5.0f, "%.2f m"))
    {
        Oscillator.CurrentDisplacementM = Oscillator.InitialDisplacementM;
        Changed                         = true;
    }
    Changed |= Frontier::InspectorControls::ConstructSliderRow(Palette, Faces, "##DrvHz", "Drive Frequency", Oscillator.DriveFrequencyHz, 0.0f, 20.0f, "%.2f Hz");

    char EnergyBuf[48]{};
    char DispBuf[48]{};
    std::snprintf(EnergyBuf, sizeof(EnergyBuf), "%.2f J", Oscillator.ComputeStoredElasticEnergyJoules());
    std::snprintf(DispBuf, sizeof(DispBuf), "%+.3f m", Oscillator.CurrentDisplacementM);
    const std::array<Frontier::InspectorReadoutCell, 2> Cells = {{
        {"ELASTIC ENERGY", EnergyBuf},
        {"DISPLACEMENT", DispBuf},
    }};
    Frontier::InspectorControls::ConstructReadoutStrip(Palette, Faces, Cells.data(), Cells.size());

    if (Changed)
    {
        Oscillator.AdvanceRevision();
    }
}

#elif FRONTIER_EDITOR

void HarmonicOscillatorInspector::ConstructInspector() {}

#endif

HarmonicOscillatorRecord::HarmonicOscillatorRecord(std::string InDisplayName)
    : Frontier::SceneRecordBase(Frontier::RecordClassification::Physics, "HarmonicOscillator", std::move(InDisplayName))
{
    Physics.Mass.MassKilograms          = 4.5f;
    Physics.Mass.DensityKgPerCubicMetre = 7850.0f;
    Physics.Mass.SyncVolumeFromMassAndDensity();
    ConfigureEditorPresentation("Activity", "Project simulation", "HarmonicOscillator", "#69dfa5");
#if FRONTIER_EDITOR
    Frontier::InspectorRuntime::Instance().BindInspector(QueryToken(), std::make_unique<Frontier::SpatialInspector>(Spatial));
    Frontier::InspectorRuntime::Instance().AttachInspector(QueryToken(), std::make_unique<Frontier::PhysicsInspector>(Physics));
    Frontier::InspectorRuntime::Instance().AttachInspector(QueryToken(), std::make_unique<HarmonicOscillatorInspector>(Oscillator, Physics.Mass));
#endif
}

void HarmonicOscillatorRecord::SerializeConfiguration(toml::table& OutTable) const
{
    toml::array PosArray;
    PosArray.push_back(static_cast<double>(Spatial.PositionMetres[0]));
    PosArray.push_back(static_cast<double>(Spatial.PositionMetres[1]));
    PosArray.push_back(static_cast<double>(Spatial.PositionMetres[2]));
    OutTable.insert_or_assign("position_m", std::move(PosArray));
    OutTable.insert_or_assign("mass_kg", static_cast<double>(Physics.Mass.MassKilograms));
    OutTable.insert_or_assign("density_kg_m3", static_cast<double>(Physics.Mass.DensityKgPerCubicMetre));
    OutTable.insert_or_assign("volume_m3", static_cast<double>(Physics.Mass.VolumeCubicMetres));
    OutTable.insert_or_assign("stiffness_n_m", static_cast<double>(Oscillator.StiffnessNewtonPerM));
    OutTable.insert_or_assign("damping_ratio", static_cast<double>(Oscillator.DampingRatio));
    OutTable.insert_or_assign("initial_displacement_m", static_cast<double>(Oscillator.InitialDisplacementM));
    OutTable.insert_or_assign("drive_frequency_hz", static_cast<double>(Oscillator.DriveFrequencyHz));
}

void HarmonicOscillatorRecord::DeserializeConfiguration(const toml::table& InTable)
{
    if (const toml::array* Pos = InTable["position_m"].as_array())
    {
        for (size_t Index = 0; Index < 3 && Index < Pos->size(); ++Index)
        {
            if (const auto Val = (*Pos)[Index].value<double>())
            {
                Spatial.PositionMetres[Index] = static_cast<float>(*Val);
            }
        }
    }
    if (const auto MassKg = InTable["mass_kg"].value<double>())
    {
        Physics.Mass.MassKilograms = static_cast<float>(*MassKg);
    }
    if (const auto Density = InTable["density_kg_m3"].value<double>())
    {
        Physics.Mass.DensityKgPerCubicMetre = static_cast<float>(*Density);
    }
    Physics.Mass.SyncVolumeFromMassAndDensity();
    if (const auto Stiff = InTable["stiffness_n_m"].value<double>())
    {
        Oscillator.StiffnessNewtonPerM = static_cast<float>(*Stiff);
    }
    if (const auto Zeta = InTable["damping_ratio"].value<double>())
    {
        Oscillator.DampingRatio = static_cast<float>(*Zeta);
    }
    if (const auto Disp = InTable["initial_displacement_m"].value<double>())
    {
        Oscillator.InitialDisplacementM = static_cast<float>(*Disp);
        Oscillator.CurrentDisplacementM = Oscillator.InitialDisplacementM;
    }
    if (const auto Freq = InTable["drive_frequency_hz"].value<double>())
    {
        Oscillator.DriveFrequencyHz = static_cast<float>(*Freq);
    }
    Spatial.AdvanceRevision();
    Physics.AdvanceRevision();
    Oscillator.AdvanceRevision();
}

void HarmonicOscillatorRecord::StepSimulation(float DeltaSeconds)
{
    if (!Oscillator.Enabled || DeltaSeconds <= 0.0f)
    {
        return;
    }
    const float MassKg  = std::max(1.0e-3f, Physics.Mass.MassKilograms);
    const float OmegaN  = Oscillator.ComputeNaturalFrequencyRadSec(MassKg);
    const float Damping = 2.0f * Oscillator.DampingRatio * OmegaN * MassKg;
    const float ForceN  = -Oscillator.StiffnessNewtonPerM * Oscillator.CurrentDisplacementM -
                          Damping * Oscillator.CurrentVelocityMps;
    Oscillator.CurrentVelocityMps += (ForceN / MassKg) * DeltaSeconds;
    Oscillator.CurrentDisplacementM += Oscillator.CurrentVelocityMps * DeltaSeconds;
    Physics.LinearVelocityMps[1] = Oscillator.CurrentVelocityMps;
}

void RegisterEverestSimulationTypes(Frontier::SceneWorld& World)
{
    World.RegisterType({
        "HarmonicOscillator",
        "Project · Harmonic Oscillator",
        Frontier::RecordClassification::Physics,
        [](std::string DisplayName) -> std::unique_ptr<Frontier::SceneRecordBase> {
            return std::make_unique<HarmonicOscillatorRecord>(std::move(DisplayName));
        },
    });
}

} // namespace Everest
