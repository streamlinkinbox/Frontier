//============================================================================================================================================
//                                                     OUTLINERPANEL.H
//============================================================================================================================================
// 🧩 Development editor outliner — the instance roster as an outline. The celestial page's outliner, spoken in
//    ImGui, measure for measure: the panel head with its compact toggle, the two census tiles, the search pill,
//    the per-host filter dropdown, the 36 px rows (chevron · SVG glyph · name · tag · live meta · standing dot · eye)
//    with drag-and-drop reparenting, and the four-column readout strip at the foot. The panel borrows the roster
//    each tick and edits it in place: a toggle, a reparent or a rename lands in the project's own rows on the
//    same tick.

#pragma once

#include "EditorInstance.h"

#include <cstdint>

namespace Frontier {

class ControlPanel;

constexpr uint32_t kMaxOutlinerFilters = 8u;

struct OutlinerFilterEntry
{
    const char* Label = nullptr;
    uint32_t    Tint  = 0u;
    uint32_t    Mask  = 0u;
};

class OutlinerPanel final
{
public:
    void AssignControls(ControlPanel* Controls) noexcept;
    // The tab's close mark writes through this; null leaves the tab without one.
    void AssignTabOpen(bool* Open) noexcept;
    // Lets SolidArc seat this exact panel beside Project-Zero without sharing the same ImGui title/id.
    void AssignWindowTitle(const char* Title) noexcept;

    // The foot strip's five figures. Optional: without a readout the strip prints its resting figures.
    void AssignReadout(const EditorReadout* Readout) noexcept;
    // Lets each editor/tool seat its own filter vocabulary, colour chips and row masks.
    // Leaving it empty keeps Project-Zero's default game catalogue: Lights, Sky, Bodies, Geometry, Camera.
    void AssignFilterCatalog(const OutlinerFilterEntry* Entries, uint32_t Count) noexcept;
    void AssignFilter(uint32_t Slot, const char* Label, uint32_t Tint, uint32_t Mask) noexcept;
    void ClearFilterCatalog() noexcept;
    // Compatibility shim for callers that still address the game filter slots directly.
    void AssignNarrowingSlot(EditorNarrowing Slot, const char* Label, uint32_t Tint) noexcept;

    void Record(EditorInstance* Instances, uint32_t InstanceCount) noexcept;

    [[nodiscard]] uint32_t QueryPicked() const noexcept;                 // the primary pick, or kNoEditorInstance
    [[nodiscard]] uint32_t QueryPickedCount() const noexcept;
    [[nodiscard]] uint32_t QueryPickedAt(uint32_t Slot) const noexcept;
    void PickInstance(uint32_t Index) noexcept;                            // the test seam; the proof drives the pick

    // Selection operations are public because EditorHost and RenderScheduler expose the same editor seams to the
    // runtime and to the headless proof harness. Row-click policy (plain/Ctrl/Shift) remains private in HandleRowClick.
    [[nodiscard]] bool IsPicked(uint32_t Index) const noexcept;
    void TogglePick(uint32_t Index) noexcept;
    void AddPick(uint32_t Index) noexcept;

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
    [[nodiscard]] uint32_t QueryFilterCount() const noexcept;
    [[nodiscard]] const char* QueryFilterLabel(uint32_t Slot) const noexcept;
    [[nodiscard]] uint32_t QueryFilterTint(uint32_t Slot) const noexcept;
    [[nodiscard]] uint32_t QueryFilterMask(uint32_t Slot) const noexcept;
    [[nodiscard]] uint32_t QuerySelectedFilterMask() const noexcept;

    void RemovePick(uint32_t Index) noexcept;
    void HandleRowClick(uint32_t Index, uint32_t InstanceCount) noexcept;

    // The reparent: lifts the row and everything under it, and seats the run before Target (Before) or as
    //    Target's last row (Into); Target == kNoEditorInstance seats it at the root's end. Refuses a cycle.
    bool MoveRun(EditorInstance* Instances, uint32_t InstanceCount, uint32_t Lifted, uint32_t Target, bool Before) noexcept;

    ControlPanel*        Controls_ = nullptr;
    bool*                TabOpen_ = nullptr;
    const EditorReadout* Readout_  = nullptr;
    const char*          WindowTitle_ = "Outliner";

    char     QueryText_[64] = {};
    bool     FilterOn_[kMaxOutlinerFilters] = {};                             // the lit filters; none lit shows all
    const char* FilterLabels_[kMaxOutlinerFilters] = {};
    uint32_t FilterTints_[kMaxOutlinerFilters] = {};
    uint32_t FilterMasks_[kMaxOutlinerFilters] = {};
    uint32_t FilterCount_ = 0u;                                                // 0 means the default game catalogue
    uint32_t Picked_[kMaxEditorPicked] = {};
    uint32_t PickedCount_ = 0u;
    uint32_t Revealed_    = kNoEditorInstance;   // the pick last scrolled into view (the page's scrollIntoView on select)
    uint32_t Anchor_      = kNoEditorInstance;
    bool     Shut_[kMaxEditorInstances] = {};                                  // false reads open
    bool     PoseSeated_[kMaxEditorInstances] = {};                            // the feed's opening pose, taken once

    bool     Compact_      = false;
    bool     SearchFocus_  = false;   // Ctrl+Shift+F lands the caret next tick
    bool     NarrowMenuOpen_ = false;
    uint32_t DragLifted_   = kNoEditorInstance;
    uint32_t OrderRevision_ = 0u;
    bool     Shown_[kMaxEditorInstances] = {};                                 // this tick's search / narrowing hits
    bool     TreeHovered_  = false;
};

} // namespace Frontier
