/*============================================================================================================================================
                                                        PARTICLEPRESETCOMPILER.H
============================================================================================================================================*/
// 🧩 Compiles any of the 93 active non-quarantined ParticlePresetRegistry entries or live ParticleEffectRecordEntry entries into ParticleSimulations

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Particles/ParticleDomainSolvers.h"
#include "Engine/SimulationSpace/Particles/ParticleSimulation.h"
#include "Engine/Workspaces/Particle/ParticlePresetRegistry.h"
#include "Engine/Workspaces/Particle/ParticleWorkspaceSession.h"

#include <optional>
#include <string_view>

namespace Frontier::SimulationSpace::Particles
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  PARTICLE PRESET COMPILER
//------------------------------------------------------------------------------------------------------------------------

class ParticlePresetCompiler
{
public:
    // Compiles any active non-quarantined preset slug into a ready-to-tick modular ParticleSimulation.
    // Returns std::nullopt for retired Quarantine slugs or unknown identifiers.
    [[nodiscard]] static std::optional<ParticleSimulation> CompilePresetSlug(
        std::string_view PresetSlug,
        uint32_t         DeterministicSeed = 1337u);

    // Compiles a live authored ParticleEffectRecordEntry from ParticleWorkspaceSession / Inspector into a ParticleSimulation.
    [[nodiscard]] static std::optional<ParticleSimulation> CompileEffectEntry(
        const Workspaces::ParticleEffectRecordEntry& EffectEntry);
};

} // namespace Frontier::SimulationSpace::Particles
