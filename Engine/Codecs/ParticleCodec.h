/*============================================================================================================================================
                                                            PARTICLECODEC.H
============================================================================================================================================*/
// 🧩 Two-Tier Hybrid .ParticleSpace encoder & decoder persisting ParticleEffectDescriptor sets, inline ForceSources, playback & .Revision

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/RevisionCodec.h"
#include "Engine/Codecs/SpaceContainerCodec.h"
#include "Engine/Codecs/VectorFieldCodec.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldDescriptors.h"
#include "Engine/Workspaces/Particle/ParticlePresetRegistry.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ParticleSpaceDocument
{
    std::string                                                       DocumentTitle           { "ParticleStudioScene" }; // [-] - .ParticleSpace title
    std::string                                                       CoordinateSystem        { "RightHandZUp" };        // [-] - Axis convention lock
    uint32_t                                                          CascadeResolution       = 16u;                     // [idx] - Coupled ForceField clipmap
    Workspaces::ParticlePlaybackClock                                 PlaybackClock           {};                        // [-] - Timeline & step settings
    std::vector<Workspaces::ParticleEffectDescriptor>                 Effects                 {};                        // [-] - Authored particle effects
    std::vector<SimulationSpace::ForceFields::ForceSourceRecordEntry> ForceSources            {};                        // [-] - Coupled C++ ForceSources
    std::vector<std::string>                                          LinkedVectorFieldAssets {};                        // [-] - External .VectorFieldSpace refs
};

class ParticleEncoder
{
public:
    [[nodiscard]] static SpaceContainerPayload ConstructPayload(
        const ParticleSpaceDocument&       Document,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);

    [[nodiscard]] static bool EncodeFile(
        const std::filesystem::path&       TargetPath,
        const ParticleSpaceDocument&       Document,
        std::string&                       OutDiagnostic,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);

    [[nodiscard]] static std::string FormatManifestToml(const ParticleSpaceDocument& Document);
};

class ParticleDecoder
{
public:
    [[nodiscard]] static bool ExtractDocument(
        const SpaceContainerPayload& Payload,
        ParticleSpaceDocument&       OutDocument,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalOutRevision = nullptr);

    [[nodiscard]] static bool DecodeFile(
        const std::filesystem::path& SourcePath,
        ParticleSpaceDocument&       OutDocument,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalOutRevision = nullptr);

    [[nodiscard]] static bool ParseManifestToml(
        std::string_view       ManifestToml,
        ParticleSpaceDocument& OutDocument,
        std::string&           OutDiagnostic);
};

struct ParticleCodec
{
    using Encoder = ParticleEncoder;
    using Decoder = ParticleDecoder;
};

} // namespace Frontier::Codecs
