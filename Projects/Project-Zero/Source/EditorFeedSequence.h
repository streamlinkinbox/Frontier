//============================================================================================================================================
//                                                  EDITORFEEDSEQUENCE.H
//============================================================================================================================================
// 🧩 The development editor's live feed: the outliner roster and the inspector sheet, both read off the loaded
//    level every tick. No tables, no scene names — the same walk feeds the Cornell box, the showroom and the
//    shader ball, because every figure comes from placements, instances, materials or the camera.
//
//    Roster layout (preorder, folders always in this order):
//        Room        — static scenery placements (no emissive triangles, no luminaire, no camera)
//        Objects     — dynamic placements (the DYN badge: --animate and physics drive these)
//        Lighting    — emissive placements and luminaire carriers
//        Cameras     — the fly camera ("Main Camera"), then any cameras the file carries
//    A folder's rows follow it, deepened by Depth, exactly as the panel renders them.
//
//    Sheets are UI mirrors: every figure is read live at pick time, and the panel edits the mirror until a
//    write-back lands. Ranges repeat the configuration comments, so no slider can propose a figure its owner
//    cannot hold. One write-back crosses the seam (see the tick in GameExecution): the folder tint mirror.

#pragma once

#include "../../../Engine/Editor/EditorInstance.h"
#include "../../../Engine/GeometricRaster/SceneStructure.h"
#include "FlyThroughSolver.h"
#include "CameraInspectorBinding.h"

#include <cstdint>
#include <vector>

namespace Frontier::ProjectZero {

class EditorFeedSequence
{
public:
    void ApplyCameraSheet(uint32_t Index,uint32_t RowCount,const SceneStructure& Level,const EditorSheet& Sheet,FlyThroughSolver& Camera) noexcept;
    // Fills Instances (capacity Capacity) in preorder from the level's placements; returns the rows
    //    written. Folders are virtual and always present; placement rows stop at capacity.
    [[nodiscard]] uint32_t FillRoster(EditorInstance* Instances, const SceneStructure& Level,
                                      uint32_t Capacity = kMaxEditorInstances) const noexcept;

    // Builds the picked row's sheet. Returns the folder tint mirror when one is open — the single write-back
    //    this turn — so the tick can carry it back onto the row; null otherwise. LiveInstances is the frame's
    //    instance rows (AnimatedInstances in the game): positions and rotations read the live transform when it
    //    differs from the level's, so a driven body shows where it IS, not where it was baked.
    [[nodiscard]] EditorProperty* BuildSheet(uint32_t Index, EditorInstance* Instances, uint32_t RowCount,
                                            EditorSheet* Sheet, const FlyThroughSolver& Camera,
                                            const SceneStructure& Level,
                                            const std::vector<InstanceRecord>& LiveInstances) const noexcept;

    CameraOpticsSettings MainLens{},CineLens{}; // Project-owned session optics; cine remains an inactive lens study.

    // The first contiguous run of dynamic placements that own instances, as instance ordinals — the scripted
    //    driver and the physics bridge animate exactly this run. False when the level flags nothing.
    [[nodiscard]] bool QueryAnimatedSpan(uint32_t* First, uint32_t* Count,
                                        const SceneStructure& Level) const noexcept;

    // Row ↔ instance seam for GPU picking and the selection outline: one span per roster row, aligned with
    //    FillRoster's exact walk. A placement row carries its instance run and its placement ordinal; folders,
    //    cameras and figure-only rows carry zero instances. The GPU pick hands back an instance ordinal; the
    //    row whose span holds it is the outliner row to light, and the reverse walk feeds the outline.
    struct RosterSpan
    {
        uint32_t FirstInstance = 0u;                 // [idx] first InstanceRecord this row owns
        uint32_t InstanceCount = 0u;                 // [cnt] zero for folders / cameras / figure rows
        uint32_t Placement     = 0xFFFFFFFFu;        // [idx] the placement behind the row, none for the rest
    };
    [[nodiscard]] uint32_t FillRosterSpans(RosterSpan* Spans, const SceneStructure& Level,
                                           uint32_t Capacity = kMaxEditorInstances) const noexcept;
};

// The level's middle: the midpoint of its triangles' bounds (the placements' translations when the level
//    carries no triangles), for seating the viewport orbit's target. The origin when both are empty.
void QueryLevelCentre(const SceneStructure& Level, float Centre[3]) noexcept;

} // namespace Frontier::ProjectZero
