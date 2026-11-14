/*============================================================================================================================================
                                                       PARTICLEWORKSPACESESSION.H
============================================================================================================================================*/
// 🧩 Headless Particle Workspace authoring session coupling live ParticleSimulations, ForceFields, .ParticleSpace Codec & RevisionTracker

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/ParticleCodec.h"
#include "Engine/Configuration/WorkspaceConfiguration.h"
#include "Engine/SimulationSpace/ForceFields/FieldLatticeCascade.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldAccumulator.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldDescriptors.h"
#include "Engine/SimulationSpace/Particles/ParticleSimulation.h"
#include "Engine/Workspaces/Particle/ParticlePresetCompiler.h"
#include "Engine/Workspaces/Particle/ParticlePresetRegistry.h"
#include "Engine/Workspaces/WorkspaceDocument.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ParticleSelectionDomain : uint8_t
{
    EnvironmentField = 0u, // [-] - Master coupled ForceFields environment selected
    AmbientWind      = 1u, // [-] - Primary ambient LinearWind force entry selected
    ForceSource      = 2u, // [-] - Individual C++ ForceSourceRecordEntry selected
    ParticleEffect   = 3u, // [-] - Individual non-quarantined ParticleEffectDescriptor selected
};

//------------------------------------------------------------------------------------------------------------------------
//                                             PARTICLEWORKSPACESESSION
//------------------------------------------------------------------------------------------------------------------------

class ParticleWorkspaceSession
{
public:
    static constexpr std::size_t AnemometerSampleCapacity = 64u; // [count] - Rolling Beaufort wind trace samples

    ParticleWorkspaceSession();

    void PopulateDefaultStudioScene();
    void ClearAllEntries();

    // Non-quarantined Particle Effect lifecycle
    uint32_t AddPresetEffect(std::string_view PresetIdentifier);
    uint32_t DuplicateEffect(uint32_t EffectToken);
    bool     RemoveEffect(uint32_t EffectToken) noexcept;
    bool     ResetEffectPreset(uint32_t EffectToken);
    void     TriggerEffectBurst(uint32_t EffectToken) noexcept;

    // Live compiled ParticleSimulation lifecycle (1:1 per authored effect token)
    void RebuildEffectSimulation(uint32_t EffectToken);
    void RebuildAllSimulations();
    void AdvanceSimulationClock(double DeltaSeconds);
    void AdvanceForceFieldsClock(double DeltaSeconds);

    [[nodiscard]] const SimulationSpace::Particles::ParticleSimulation* QueryEffectSimulation(uint32_t EffectToken) const noexcept;
    [[nodiscard]] SimulationSpace::Particles::ParticleSimulation*       AccessEffectSimulation(uint32_t EffectToken) noexcept;
    [[nodiscard]] uint32_t                                              QueryTotalAliveParticles() const noexcept;
    [[nodiscard]] float                                                 QueryPeakParticleSpeedMs() const noexcept;

    // Directly reused C++ ForceFields lifecycle (SimulationSpace::ForceFields)
    uint32_t AddForceSource(SimulationSpace::ForceFields::ForceModel Model);
    bool     RemoveForceSource(uint32_t ForceToken) noexcept;
    void     SynchronizeForceFieldsAccumulator();

    // Headless PlaybackClock controls (synced bidirectionally with PlaybackScrubber in UI hosts)
    void                                     SeekPlaybackSeconds(float TargetSeconds);
    void                                     ResetPlaybackClock();
    void                                     ConfigureScrubbingPlayhead(bool Scrubbing) noexcept;
    [[nodiscard]] const ParticlePlaybackClock& QueryPlaybackClock() const noexcept;
    [[nodiscard]] ParticlePlaybackClock&       AccessPlaybackClock() noexcept;
    [[nodiscard]] bool                         QueryScrubbingPlayhead() const noexcept;

    // .ParticleSpace Document Persistence & RevisionTracker Undo/Redo
    void                                     CommitRevision(std::string_view OperationLabel);
    [[nodiscard]] bool                       UndoRevision();
    [[nodiscard]] bool                       RedoRevision();
    [[nodiscard]] bool                       QueryUndoAvailable() const noexcept;
    [[nodiscard]] bool                       QueryRedoAvailable() const noexcept;
    [[nodiscard]] bool                       QueryModifiedSinceSave() const noexcept;
    [[nodiscard]] Codecs::ParticleSpaceDocument ConstructSpaceDocument() const;
    [[nodiscard]] bool                       ApplySpaceDocument(const Codecs::ParticleSpaceDocument& SpaceDoc, bool RecordBaseline = true);
    [[nodiscard]] bool                       SaveParticleSpace(const std::filesystem::path& TargetPath, std::string& OutDiagnostic, bool EmbedRevisionHistory = true);
    [[nodiscard]] bool                       LoadParticleSpace(const std::filesystem::path& SourcePath, std::string& OutDiagnostic);
    [[nodiscard]] bool                       ImportVectorFieldAsset(const std::filesystem::path& SourcePath, std::string& OutDiagnostic);

    // Selection routing
    void SelectEnvironmentField() noexcept;
    void SelectAmbientWind() noexcept;
    void SelectForceSource(uint32_t ForceToken) noexcept;
    void SelectParticleEffect(uint32_t EffectToken) noexcept;
    void IncrementRevision() noexcept;

    // Queries & mutable accessors
    [[nodiscard]] ParticleSelectionDomain                                         QuerySelectionDomain() const noexcept;
    [[nodiscard]] uint32_t                                                        QuerySelectedToken() const noexcept;
    [[nodiscard]] uint64_t                                                        QueryRevision() const noexcept;
    [[nodiscard]] double                                                          QuerySimulationTimeSec() const noexcept;
    [[nodiscard]] std::span<const ParticleEffectDescriptor>                       QueryEffects() const noexcept;
    [[nodiscard]] std::span<ParticleEffectDescriptor>                             AccessEffects() noexcept;
    [[nodiscard]] const ParticleEffectDescriptor*                                 FindEffect(uint32_t EffectToken) const noexcept;
    [[nodiscard]] ParticleEffectDescriptor*                                       AccessEffect(uint32_t EffectToken) noexcept;
    [[nodiscard]] ParticleEffectDescriptor*                                       AccessSelectedEffect() noexcept;
    [[nodiscard]] const ParticleEffectDescriptor*                                 QuerySelectedEffect() const noexcept;

    [[nodiscard]] std::span<const SimulationSpace::ForceFields::ForceSourceRecordEntry> QueryForceSources() const noexcept;
    [[nodiscard]] std::span<SimulationSpace::ForceFields::ForceSourceRecordEntry>       AccessForceSources() noexcept;
    [[nodiscard]] SimulationSpace::ForceFields::ForceSourceRecordEntry*                 FindForceSource(uint32_t ForceToken) noexcept;
    [[nodiscard]] const SimulationSpace::ForceFields::ForceSourceRecordEntry*           FindForceSource(uint32_t ForceToken) const noexcept;
    [[nodiscard]] SimulationSpace::ForceFields::ForceSourceRecordEntry*                 AccessActiveForceInspectorSource() noexcept;
    [[nodiscard]] SimulationSpace::ForceFields::ForceFieldAccumulator&                  AccessForceAccumulator() noexcept;
    [[nodiscard]] const SimulationSpace::ForceFields::ForceFieldAccumulator&            QueryForceAccumulator() const noexcept;
    [[nodiscard]] SimulationSpace::ForceFields::FieldLatticeCascade&                    AccessLatticeCascade() noexcept;
    [[nodiscard]] SimulationSpace::ForceFields::ForceOverlaySettings&                   AccessForceOverlay() noexcept;
    [[nodiscard]] const SimulationSpace::ForceFields::ForceOverlaySettings&             QueryForceOverlay() const noexcept;
    [[nodiscard]] std::span<const float>                                                QueryAnemometerSamples() const noexcept;
    [[nodiscard]] int32_t*                                                              AccessSelectedWaypointIndex() noexcept;
    [[nodiscard]] WorkspaceDocument&                                                    AccessDocument() noexcept;
    [[nodiscard]] const WorkspaceDocument&                                              InspectDocument() const noexcept;
    [[nodiscard]] Configuration::WorkspaceConfiguration&                                AccessWorkspaceConfiguration() noexcept;
    [[nodiscard]] const Configuration::WorkspaceConfiguration&                          InspectWorkspaceConfiguration() const noexcept;

    [[nodiscard]] uint32_t                                                        QueryVisibleCount() const noexcept;
    [[nodiscard]] uint32_t                                                        QueryHiddenCount() const noexcept;
    [[nodiscard]] uint32_t                                                        QueryTotalCount() const noexcept;
    [[nodiscard]] SimulationSpace::ForceFields::ForceDiagnosticsReading           QueryAmbientWindDiagnostics() const noexcept;
    [[nodiscard]] SimulationSpace::ForceFields::SpatialForceSample                SampleCoupledForce(
        const SimulationSpace::ForceFields::SpatialVector3& WorldPoint) const noexcept;

    // Live .ParticleSpace TOML descriptor preview for Spec & Export tab
    [[nodiscard]] std::string SerializeSelectedEffectTomlPreview() const;

private:
    void RecordAnemometerSample() noexcept;
    void ResetRevisionBaseline(std::string_view BaselineLabel);
    bool RestoreRevisionCursor();
    [[nodiscard]] ParticleEffectDescriptor ConstructEffectDescriptor(
        const ParticlePresetDescriptor& Descriptor,
        uint32_t                        AssignedToken) const;

    WorkspaceDocument                                                   Document               {
        WorkspaceToken{ 0u, 1u, Codecs::SpaceExtensionCategory::ParticleSpace },
        "ParticleStudioScene",
        std::filesystem::path("ParticleStudioScene.ParticleSpace")
    };
    Configuration::WorkspaceConfiguration                               WorkspaceConfig        {};
    std::vector<ParticleEffectDescriptor>                               Effects                {};
    std::unordered_map<uint32_t, std::unique_ptr<SimulationSpace::Particles::ParticleSimulation>> LiveSimulations {};
    std::vector<SimulationSpace::ForceFields::ForceSourceRecordEntry>   ForceSources           {};
    std::vector<std::string>                                            LinkedVectorFields     {};
    SimulationSpace::ForceFields::ForceFieldAccumulator                 ForceAccumulator       {};
    SimulationSpace::ForceFields::FieldLatticeCascade                   LatticeCascade         { 16u };
    SimulationSpace::ForceFields::ForceOverlaySettings                  ForceOverlay           {};
    std::array<float, AnemometerSampleCapacity>                         AnemometerRing         {};
    uint32_t                                                            AnemometerSampleCount  = 0u;
    int32_t                                                             SelectedWaypoint       = -1;

    ParticleSelectionDomain                                             SelectionDomain        = ParticleSelectionDomain::ParticleEffect;
    uint32_t                                                            SelectedToken          = 0u;
    uint32_t                                                            NextEffectToken        = 1u;
    uint32_t                                                            NextForceToken         = 1u;
    uint64_t                                                            AuthoringRevision      = 1u;
    uint64_t                                                            RevisionStampCounter   = 1ull;
    double                                                              SimulationTimeSec      = 0.0;
    ParticlePlaybackClock                                               PlaybackClock          {};
    bool                                                                ScrubbingPlayhead      = false;
};

} // namespace Frontier::Workspaces
