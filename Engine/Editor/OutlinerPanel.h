//============================================================================================================================================
//                                                     OUTLINERPANEL.H
//============================================================================================================================================
// 🧩 Development editor outliner — the record register as a tree. Header, search, kind chips, thirty-pixel rows,
//    footer. The panel borrows the register each tick and edits it in place: renames, toggles and the folder
//    tint land in the project's own rows on the same tick.

#pragma once

#include "EditorRecord.h"

#include <cstdint>

namespace Frontier {

class EditorKit;

class OutlinerPanel final
{
public:
    void AssignKit(EditorKit* Kit) noexcept;

    void Record(EditorRecord* Records, uint32_t RecordCount) noexcept;

    [[nodiscard]] uint32_t QueryPicked() const noexcept;                 // the primary pick, or kNoEditorRecord
    [[nodiscard]] uint32_t QueryPickedCount() const noexcept;
    [[nodiscard]] uint32_t QueryPickedAt(uint32_t Slot) const noexcept;
    void PickRecord(uint32_t Index) noexcept;                            // the test seam; the proof drives the pick

private:
    void RecordHeader(EditorRecord* Records, uint32_t RecordCount, uint32_t EntityCount, uint32_t GroupCount) noexcept;
    void RecordSearch() noexcept;
    void RecordChips() noexcept;
    uint32_t RecordTree(EditorRecord* Records, uint32_t RecordCount) noexcept;
    void RecordRow(EditorRecord* Records, uint32_t RecordCount, uint32_t Index, bool MatchOn) noexcept;
    void RecordFooter(uint32_t HitCount, uint32_t TotalCount) noexcept;

    [[nodiscard]] bool IsPicked(uint32_t Index) const noexcept;
    void AddPick(uint32_t Index) noexcept;
    void RemovePick(uint32_t Index) noexcept;

    EditorKit* Kit_ = nullptr;

    char     QueryText_[64] = {};
    bool     KindPicked_[static_cast<uint32_t>(EditorRecordKind::Count)] = {};   // the narrowing; empty shows all
    uint32_t Picked_[kMaxEditorPicked] = {};
    uint32_t PickedCount_ = 0u;
    uint32_t Anchor_      = kNoEditorRecord;
    uint32_t ServedPick_ = kNoEditorRecord;
    int      RevealTicks_ = 0;
    bool     FolderShut_[kMaxEditorRecords] = {};                                 // false reads open

    bool     Renaming_     = false;
    uint32_t RenameIndex_  = kNoEditorRecord;
    char     RenameText_[48] = {};
    bool     FocusRename_  = false;
};

} // namespace Frontier
