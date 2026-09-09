//============================================================================================================================================
//                                                  EDITORFEEDSEQUENCE.H
//============================================================================================================================================
// 🧩 The development editor's live feed: the outliner roster and the inspector sheet, both read off the loaded
//    level every tick. No tables, no scene names — the same walk feeds the Cornell box, the showroom and the
//    shader ball, because every figure comes from placements, instances, materials or the sky clock.
//
//    Roster layout (preorder, folders always in this order):
//        Room        — static scenery placements (no emissive triangles, no luminaire, no camera)
//        Objects     — dynamic placements (the DYN badge: --animate and physics drive these)
//        Lighting    — emissive placements and luminaire carriers
//        Cameras     — the fly camera ("Main Camera"), then any cameras the file carries
//        Environment — Sky / Sun / Moon (the atmosphere and orbit cards)
//    A folder's rows follow it, deepened by Depth, exactly as the panel renders them.
//
//    Sheets are UI mirrors: every figure is read live at pick time, and the panel edits the mirror until a
//    write-back lands. Ranges repeat the configuration comments, so no slider can propose a figure its owner
//    cannot hold. Only the folder tint mirror writes back (see the tick in GameExecution).

#pragma once

#include "../../../Engine/Editor/EditorInstance.h"
#include "../../../Engine/GeometricRaster/SceneStructure.h"
#include "../../../Engine/GeometricRaster/CelestialSolver.h"
#include "../../../Engine/DisplayPresentation/ReSTIRIntegrator.h"
#include "FlyThroughSolver.h"

#include <cstdint>
#include <vector>

namespace Frontier::ProjectZero {

class EditorFeedSequence
{
public:
    // Fills Instances (capacity kMaxEditorInstances) in preorder from the level's placements; returns the rows
    //    written. Folders are virtual and always present; placement rows stop at capacity.
    [[nodiscard]] uint32_t FillRoster(EditorInstance* Instances, const SceneStructure& Level) const noexcept;

    // Builds the picked row's sheet. Returns the folder tint mirror when one is open — the single write-back
    //    this turn — so the tick can carry it back onto the row; null otherwise. LiveInstances is the frame's
    //    instance rows (AnimatedInstances in the game): positions and rotations read the live transform when it
    //    differs from the level's, so a driven body shows where it IS, not where it was baked.
    [[nodiscard]] EditorProperty* BuildSheet(uint32_t Index, EditorInstance* Instances, uint32_t RowCount,
                                            EditorSheet* Sheet, const ReSTIRIntegratorConfiguration& Config,
                                            const CelestialSolver& Sky, const FlyThroughSolver& Camera,
                                            const SceneStructure& Level,
                                            const std::vector<InstanceRecord>& LiveInstances) const noexcept;

    // The first contiguous run of dynamic placements that own instances, as instance ordinals — the scripted
    //    driver and the physics bridge animate exactly this run. False when the level flags nothing.
    [[nodiscard]] bool QueryAnimatedSpan(uint32_t* First, uint32_t* Count,
                                        const SceneStructure& Level) const noexcept;
};

} // namespace Frontier::ProjectZero
