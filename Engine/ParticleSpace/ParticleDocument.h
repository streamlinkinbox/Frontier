/*============================================================================================================================================
                                                            PARTICLEDOCUMENT.H
============================================================================================================================================*/
// 🧩 Authoritative in-memory .ParticleSpace document holding ParticleRig collection, ForceSources, InteractiveCollider & playback specs

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/ParticleSpace/ParticleRig.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldDescriptors.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ParticleViewportCameraSpec
{
    Mathematics::Axis3D EyePositionMeters { 0.0f, -6.5f, 2.2f }; // [m]   - Right-Hand +Z Up camera eye
    Mathematics::Axis3D TargetMeters      { 0.0f,  0.0f, 1.0f }; // [m]   - Look-at target (+Z Up)
    float               FieldOfViewDeg    = 48.0f;               // [deg] - Vertical field of view
    bool                ShowFloorGrid     = true;                // [-]   - Reference floor grid overlay
    bool                ShowVectorOverlay = false;               // [-]   - Force field vector overlay
    bool                ShowMinimap       = false;               // [-]   - Diagnostic infographic minimap
};

class ParticleDocument
{
public:
    std::string                                                DocumentTitle      { "ParticleStudio" }; // [-] - Document title
    std::string                                                ActivePresetId     { "sparks" };         // [-] - Active preset key
    uint32_t                                                   RandomSeed         = 1337u;            // [-] - Deterministic seed
    float                                                      TimeScale          = 1.0f;             // [-] - Simulation speed multiplier
    float                                                      PlaybackLoopSec    = 12.0f;            // [s] - Loop interval (0 = infinite)
    bool                                                       PlaybackPaused     = false;            // [-] - Transport pause toggle
    bool                                                       LoopPlayback       = true;             // [-] - Auto-restart loop toggle
    Mathematics::Axis3D                                        AmbientWindMps     { 0.0f, 0.0f, 0.0f }; // [m/s] - Global wind (+Z Up)
    InteractiveColliderSettings                                Collider           {};                 // [-] - Ball + floor + SDF settings
    ParticleViewportCameraSpec                                 ViewportCamera     {};                 // [-] - Editor camera defaults

    std::vector<ParticleRig>                                   Rigs               {};                 // [-] - Authored modular rigs
    std::vector<SimulationSpace::ForceFields::ForceSourceRecordEntry> ForceSources {};                // [-] - Coupled force sources
    std::vector<std::string>                                   LinkedVectorFields {};                 // [-] - Optional .VectorFieldSpace paths

    void PopulateDefaultSparksRig();
    void RebuildAllRigCaches();

    [[nodiscard]] const ParticleRig* FindRig(uint32_t RigId) const noexcept;
    [[nodiscard]] ParticleRig*       FindRigMutable(uint32_t RigId) noexcept;
    [[nodiscard]] const ParticleRig* FindRigByTitle(std::string_view Title) const noexcept;

    uint32_t AddRig(ParticleRig Rig);
    bool     RemoveRig(uint32_t RigId);

    [[nodiscard]] uint32_t QueryTotalAllocatedSlots() const noexcept;
};

} // namespace Frontier::ParticleSpace
