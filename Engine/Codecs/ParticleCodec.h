/*============================================================================================================================================
                                                              PARTICLECODEC.H
============================================================================================================================================*/
// 🧩 Two-Tier Hybrid .ParticleSpace encoder & decoder (PascalCase TOML + 64B-aligned .ColourLut, .SurfaceAnchor, .StrandRest & .Revision)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/RevisionCodec.h"
#include "Engine/Codecs/SpaceContainerCodec.h"
#include "Engine/Configuration/BuildConfiguration.h"
#include "Engine/ParticleSpace/ParticleDocument.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

class ParticleEncoder
{
public:
    [[nodiscard]] static SpaceContainerPayload BuildPayload(
        const ParticleSpace::ParticleDocument& Document,
        const Workspaces::RevisionTracker*     OptionalRevision = nullptr);

    [[nodiscard]] static bool EncodeToFile(
        const std::filesystem::path&           TargetPath,
        const ParticleSpace::ParticleDocument& Document,
        std::string&                           OutDiagnostic,
        const Workspaces::RevisionTracker*     OptionalRevision = nullptr);
};

class ParticleDecoder
{
public:
    [[nodiscard]] static bool ExtractDocument(
        const SpaceContainerPayload&     Payload,
        ParticleSpace::ParticleDocument& OutDocument,
        std::string&                     OutDiagnostic,
        Workspaces::RevisionTracker*     OptionalOutRevision = nullptr);

    [[nodiscard]] static bool DecodeFromFile(
        const std::filesystem::path&     SourcePath,
        ParticleSpace::ParticleDocument& OutDocument,
        std::string&                     OutDiagnostic,
        Workspaces::RevisionTracker*     OptionalOutRevision = nullptr);
};

struct ParticleCodec
{
    using Encoder = ParticleEncoder;
    using Decoder = ParticleDecoder;
};

} // namespace Frontier::Codecs
