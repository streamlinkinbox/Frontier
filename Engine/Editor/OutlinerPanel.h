//============================================================================================================================================
//                                                     OUTLINERPANEL.H
//============================================================================================================================================
// 🧩 Development editor outliner — the instance roster as an outline. The celestial page's outliner, spoken in
//    ImGui, measure for measure: the panel head with its compact toggle, the two census tiles, the search pill,
//    the five narrowing pills, the 36 px rows (chevron · SVG glyph · name · tag · live meta · standing dot · eye)
//    with drag-and-drop reparenting, and the four-column readout strip at the foot. The panel borrows the roster
//    each tick and edits it in place: a toggle, a reparent or a rename lands in the project's own rows on the
//    same tick.

#pragma once

#include "EditorInstance.h"

#include <cstdint>

namespace Frontier {

class ControlPanel;

class OutlinerPanel final
{
public:
    void AssignControls(ControlPanel* Controls) noexcept;
    // The tab's close mark writes through this; null leaves the tab without one.
    void AssignTabOpen(bool* Open) noexcept;

    // The foot strip's five figures. Optional: without a readout the strip prints its resting figures.
    void AssignReadout(const EditorReadout* Readout) noexcept;

    void Record(EditorInstance* Instances, uint32_t InstanceCount) noexcept;

    [[nodiscard]] uint32_t QueryPicked() const noexcept;                 // the primary pick, or kNoEditorInstance
    [[nodiscard]] uint32_t QueryPickedCount() const noexcept;
    [[nodiscard]] uint32_t QueryPickedAt(uint32_t Slot) const noexcept;
    void PickInstance(uint32_t Index) noexcept;                            // the test seam; the proof drives the pick

    // The page's Tab: the panel narrows to 236 px and drops its tiles, pills and metas.
    [[nodiscard]] bool QueryCompact() const noexcept { return Compact_; }
    void AssignCompact(bool On) noexcept { Compact_ = On; }

    // Bumps whenever a drag lands: the project re-reads the roster order after a reparent.
    [[nodiscard]] uint32_t QueryOrderRevision() const noexcept { return OrderRevision_; }

private:
    void RecordHeader(EditorInstance* Instances, uint32_t InstanceCount) noexcept;
    void RecordTiles(EditorInstance* Instances, uint32_t InstanceCount) noexcept;
    void RecordSearch() noexcept;
    void RecordChips() noexcept;
    uint32_t RecordOutline(EditorInstance* Instances, uint32_t InstanceCount) noexcept;
    void RecordRow(EditorInstance* Instances, uint32_t InstanceCount, uint32_t Index, bool HasKids) noexcept;
    void RecordEmpty(float Width) noexcept;
    void RecordFooter() noexcept;

    [[nodiscard]] bool IsPicked(uint32_t Index) const noexcept;
    void AddPick(uint32_t Index) noexcept;
    void RemovePick(uint32_t Index) noexcept;
    void HandleRowClick(uint32_t Index, uint32_t InstanceCount) noexcept;

    // The reparent: lifts the row and everything under it, and seats the run before Target (Before) or as
    //    Target's last row (Into); Target == kNoEditorInstance seats it at the root's end. Refuses a cycle.
    bool MoveRun(EditorInstance* Instances, uint32_t InstanceCount, uint32_t Lifted, uint32_t Target, bool Before) noexcept;

    ControlPanel*        Controls_ = nullptr;
    bool*                TabOpen_ = nullptr;
    const EditorReadout* Readout_  = nullptr;

    char     QueryText_[64] = {};
    bool     NarrowOn_[static_cast<uint32_t>(EditorNarrowing::Count)] = {};   // the lit pills; none lit shows all
    uint32_t Picked_[kMaxEditorPicked] = {};
    uint32_t PickedCount_ = 0u;
    uint32_t Revealed_    = kNoEditorInstance;   // the pick last scrolled into view (the page's scrollIntoView on select)
    uint32_t Anchor_      = kNoEditorInstance;
    bool     Shut_[kMaxEditorInstances] = {};                                  // false reads open
    bool     PoseSeated_[kMaxEditorInstances] = {};                            // the feed's opening pose, taken once

    bool     Compact_      = false;
    bool     SearchFocus_  = false;   // Ctrl+Shift+F lands the caret next tick
    uint32_t DragLifted_   = kNoEditorInstance;
    uint32_t OrderRevision_ = 0u;
    bool     Shown_[kMaxEditorInstances] = {};                                 // this tick's search / narrowing hits
    bool     TreeHovered_  = false;

    EditorInstance Scratch_[kMaxEditorInstances] = {};                         // MoveRun's lifted run
};

} // namespace Frontier
