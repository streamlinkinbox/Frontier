/*============================================================================================================================================
                                                        PARTICLERECIPECOMPILER.H
============================================================================================================================================*/
// 🧩 Compiles any of the 93 active non-quarantined ParticlePresetRegistry entries or live ParticleEffectRecordEntry configs into ParticleSimulations

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Particles/ParticleDomainSolvers.h"
#include "Engine/SimulationSpace/Particles/ParticleSimulation.h"
#include "Engine/Workspaces/Particles/ParticlePresetRegistry.h"
#include "Engine/Workspaces/Particles/ParticleWorkspaceSession.h"

#include <optional>
#include <string_view>

namespace Frontier::SimulationSpace::Particles
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  PARTICLE RECIPE COMPILER
//------------------------------------------------------------------------------------------------------------------------

class ParticleRecipeCompiler
{
public:
    // Compiles any active non-quarantined preset slug into a ready-to-step modular ParticleSimulation.
    // Returns std::nullopt for retired Quarantine slugs or unknown identifiers.
    [[nodiscard]] static std::optional<ParticleSimulation> ConstructFromPresetSlug(
        std::string_view PresetSlug,
        uint32_t         DeterministicSeed = 1337u);

    // Compiles a live authored ParticleEffectRecordEntry from ParticleWorkspaceSession / Inspector into a ParticleSimulation.
    [[nodiscard]] static std::optional<ParticleSimulation> ConstructFromRecordEntry(
        const Workspaces::ParticleEffectRecordEntry& EffectEntry);
};

} // namespace Frontier::SimulationSpace::Particles
