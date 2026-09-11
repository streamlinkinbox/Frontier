//============================================================================================================================================
//                                                     OUTLINERPANEL.H
//============================================================================================================================================
// 🧩 Development editor outliner — the instance roster as an outline. SolidArc's outliner, spoke in ImGui: the
//    glowing title, the census tiles, the filter menu and pills, the two-line rows, the census-bar footer. The
//    panel borrows the roster each tick and edits it in place: renames, toggles and the folder tint land in
//    the project's own rows on the same tick.

#pragma once

#include "EditorInstance.h"

#include <cstdint>

namespace Frontier {

class ControlPanel;

class OutlinerPanel final
{
public:
    void AssignControls(ControlPanel* Controls) noexcept;

    void Record(EditorInstance* Instances, uint32_t InstanceCount) noexcept;

    [[nodiscard]] uint32_t QueryPicked() const noexcept;                 // the primary pick, or kNoEditorInstance
    [[nodiscard]] uint32_t QueryPickedCount() const noexcept;
    [[nodiscard]] uint32_t QueryPickedAt(uint32_t Slot) const noexcept;
    void PickInstance(uint32_t Index) noexcept;                            // the test seam; the proof drives the pick

private:
    void RecordHeader() noexcept;
    void RecordTiles(EditorInstance* Instances, uint32_t InstanceCount) noexcept;
    void RecordSearch(EditorInstance* Instances, uint32_t InstanceCount) noexcept;
    void RecordChips(EditorInstance* Instances, uint32_t InstanceCount) noexcept;
    uint32_t RecordOutline(EditorInstance* Instances, uint32_t InstanceCount) noexcept;
    void RecordRow(EditorInstance* Instances, uint32_t InstanceCount, uint32_t Index, bool MatchOn) noexcept;
    void RecordFolderRow(EditorInstance* Instances, uint32_t InstanceCount, uint32_t Index, bool MatchOn) noexcept;
    void RecordLeafRow(EditorInstance* Instances, uint32_t InstanceCount, uint32_t Index) noexcept;
    void RecordFooter(EditorInstance* Instances, uint32_t InstanceCount, uint32_t HitCount) noexcept;

    [[nodiscard]] bool IsPicked(uint32_t Index) const noexcept;
    void AddPick(uint32_t Index) noexcept;
    void RemovePick(uint32_t Index) noexcept;
    void HandleRowClick(EditorInstance& Row, uint32_t Index, uint32_t InstanceCount) noexcept;

    ControlPanel* Controls_ = nullptr;

    char     QueryText_[64] = {};
    bool     CategoryPicked_[static_cast<uint32_t>(EditorInstanceCategory::Count)] = {};   // the narrowing; empty shows all
    uint32_t Picked_[kMaxEditorPicked] = {};
    uint32_t PickedCount_ = 0u;
    uint32_t Anchor_      = kNoEditorInstance;
    uint32_t ServedPick_ = kNoEditorInstance;
    int      RevealTicks_ = 0;
    bool     FolderShut_[kMaxEditorInstances] = {};                                 // false reads open

    bool     Renaming_     = false;
    uint32_t RenameIndex_  = kNoEditorInstance;
    char     RenameText_[48] = {};
    bool     FocusRename_  = false;

    bool     SearchFocus_  = false;   // the search ring lags one tick (the push precedes the field)
    bool     RenameFocus_  = false;   // the rename ring lags one tick
    bool     CategoryMenuWasOpen_  = false;
    double   CategoryMenuOpenedAt_ = 0.0;
    float    CategoryChevronAnim_  = 0.0f;
};

} // namespace Frontier
