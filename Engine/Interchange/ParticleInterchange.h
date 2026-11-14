/*============================================================================================================================================
                                                         PARTICLEINTERCHANGE.H
============================================================================================================================================*/
// 🧩 External Particle Studio HTML JSON interchange (all 129+ PE.baseParams keys + Y-Up <-> +Z Up), handoff packets & 3D mesh import

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/ParticleDocument.h"
#include "Engine/ParticleSpace/ParticleRig.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace Frontier::Interchange
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ParticleHandoffPacket
{
    std::string PresetId               {}; // [-] - Source HTML/Frontier preset identifier
    std::string RigTitle               {}; // [-] - Authored rig label
    std::string ParameterOverridesJson {}; // [-] - Serialized JSON dictionary of PE.baseParams fields
};

class ParticleInterchange
{
public:
    // 📝 HTML Particle Studio JSON (PE.baseParams / presets.js) <-> ParticleRig (delegates colour to ColourInterchange)
    [[nodiscard]] static bool ImportHtmlStudioPresetJson(
        std::string_view            PresetJson,
        std::string_view            PresetId,
        std::string_view            RigTitle,
        bool                        Quarantined,
        ParticleSpace::ParticleRig& OutRig,
        std::string&                OutDiagnostic);

    [[nodiscard]] static std::string ExportHtmlStudioPresetJson(const ParticleSpace::ParticleRig& Rig);

    // 📝 Full scene JSON snapshot <-> ParticleDocument
    [[nodiscard]] static bool ImportHtmlStudioSceneJson(
        std::string_view                 SceneJson,
        ParticleSpace::ParticleDocument& OutDocument,
        std::string&                     OutDiagnostic);

    [[nodiscard]] static std::string ExportHtmlStudioSceneJson(const ParticleSpace::ParticleDocument& Document);

    // 📝 Host handoff packets (mirrors Engine/handoff.js)
    [[nodiscard]] static ParticleHandoffPacket BuildHandoffPacket(const ParticleSpace::ParticleRig& Rig);
    [[nodiscard]] static bool                  ApplyHandoffPacket(
        const ParticleHandoffPacket& Packet,
        ParticleSpace::ParticleRig&  TargetRig,
        std::string&                 OutDiagnostic);

    // 📝 External 3D surface intake (.obj / .gltf) -> PolygonNetwork -> SurfaceAnchorSet
    [[nodiscard]] static bool ImportExternalBackingSurface(
        const std::filesystem::path& SourcePath,
        ParticleSpace::ParticleRig&  TargetRig,
        std::string&                 OutDiagnostic);
};

} // namespace Frontier::Interchange
