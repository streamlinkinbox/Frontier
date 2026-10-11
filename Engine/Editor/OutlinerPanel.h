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
    void RevealInstance(uint32_t Index, const EditorInstance* Rows, uint32_t Count) noexcept;
    void PickInstance(uint32_t Index) noexcept;                            // the test seam; the proof drives the pick

    // Selection operations are public because EditorHost and RenderScheduler expose the same editor seams to the
    // runtime and to the headless proof harness. Row-click policy (plain/Ctrl/Shift) remains private in HandleRowClick.
    [[nodiscard]] bool IsPicked(uint32_t Index) const noexcept;
    void TogglePick(uint32_t Index) noexcept;
    // Seats a whole pick at once (the host mirrors the document's selection here): the first row leads. Rows past
    //    kMaxEditorPicked do not fit the pick and are left out; none clears the pick.
    void AssignPicks(const uint32_t* Rows, uint32_t Count) noexcept;
    void AddPick(uint32_t Index) noexcept;

    // SolidArc's document look: the head pill, the Figures / Selected tiles, two-line rows with a status glyph and the
    //    census foot. Off keeps the game outliner exactly as it was.
    void AssignDocumentStyle(bool On) noexcept { DocumentStyle_ = On; }

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
    // `Squeeze` is the row's share of its ancestors' open animation: 1 draws the row at full height, 0 is fully
    //    collapsed. It scales the row's height and its ink, which is what makes a folder fold instead of blink.
    void RecordRow(EditorInstance* Instances, uint32_t InstanceCount, uint32_t Index, bool HasKids,
                   float Squeeze = 1.0f) noexcept;
    void RecordEmpty(float Width) noexcept;
    void RecordFooter() noexcept;
    void RecordDocumentTiles(EditorInstance* Instances, uint32_t InstanceCount) noexcept;
    void RecordDocumentFooter() noexcept;
    void TakeCensus(const EditorInstance* Instances, uint32_t InstanceCount) noexcept;
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
    // ── Micro-animation state ────────────────────────────────────────────────────────────────────────────────
    // One open phase per row: 1 = the subtree stands at full height, 0 = fully folded away. It chases Shut_ at a
    //    fixed time constant, so a fold is a movement rather than a jump, and a row whose INDEX changed (the
    //    roster is rebuilt every tick) snaps instead of animating from a stranger's phase.
    float    Phase_[kMaxEditorInstances] = {};
    float    ScrollNow_    = 0.0f;   // [px] the scroll the tree is drawn at this tick
    float    ScrollTarget_ = 0.0f;   // [px] where the wheel/reveal asked it to be
    bool     ScrollSeated_ = false;  // false until the first tick seats both from the live scroll
    bool     Revealing_    = false;  // a pick scrolled itself into view this tick; the glide stands aside
    uint64_t RosterKeys_[kMaxEditorInstances] = {};
    uint32_t RosterCount_=0;
    bool ExplicitPick_=false;

    bool     Compact_      = false;
    bool     SearchFocus_  = false;   // Ctrl+Shift+F lands the caret next tick
    bool     NarrowMenuOpen_ = false;
    uint32_t DragLifted_   = kNoEditorInstance;
    uint32_t OrderRevision_ = 0u;
    bool     Shown_[kMaxEditorInstances] = {};                                 // this tick's search / narrowing hits
    bool     TreeHovered_  = false;

    // The document style: this tick's census, taken once from the roster before anything draws.
    bool     DocumentStyle_ = false;
    uint32_t CensusSymbol_[8] = {};      // figures per EditorSymbol (index = the mark), folders excluded
    uint32_t CensusShown_   = 0u;
    uint32_t CensusHidden_  = 0u;
    uint32_t CensusIssues_  = 0u;
};

} // namespace Frontier
