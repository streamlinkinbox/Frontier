/*============================================================================================================================================
                                                        PARTICLEPRESETCOMPILER.H
============================================================================================================================================*/
// 🧩 Headless compiler translating any of the 93 active ParticlePresetRegistry entries or ParticleEffectDescriptor entries into ParticleSimulations

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Particles/ParticleDomainSolvers.h"
#include "Engine/SimulationSpace/Particles/ParticleSimulation.h"
#include "Engine/Workspaces/Particle/ParticlePresetRegistry.h"

#include <optional>
#include <string_view>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  PARTICLE PRESET COMPILER
//------------------------------------------------------------------------------------------------------------------------

class ParticlePresetCompiler
{
public:
    // Compiles any active non-quarantined preset slug into a ready-to-tick modular ParticleSimulation.
    // Returns std::nullopt for retired Quarantine slugs or unknown identifiers.
    [[nodiscard]] static std::optional<SimulationSpace::Particles::ParticleSimulation> CompilePresetSlug(
        std::string_view PresetSlug,
        uint32_t         DeterministicSeed = 1337u);

    // Compiles a live authored ParticleEffectDescriptor from ParticleWorkspaceSession / ParticleCodec into a ParticleSimulation.
    [[nodiscard]] static std::optional<SimulationSpace::Particles::ParticleSimulation> CompileEffectDescriptor(
        const ParticleEffectDescriptor& EffectDescriptor);
};

} // namespace Frontier::Workspaces
