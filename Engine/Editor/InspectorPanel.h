//============================================================================================================================================
//                                                    INSPECTORPANEL.H
//============================================================================================================================================
// 🧩 Development editor inspector — the picked record as a property sheet. Ident strip, schema cards drawn from
//    the project's sheet, the record standing, the notes card. Every control edits the project's own figures.

#pragma once

#include "EditorRecord.h"

#include <imgui.h>

#include <cstdint>

namespace Frontier {

class EditorKit;

class InspectorPanel final
{
public:
    void AssignKit(EditorKit* Kit) noexcept;

    void Record(EditorRecord* Picked, uint32_t PickedIndex, EditorSheet* Sheet) noexcept;

private:
    void  RecordEmpty() noexcept;
    void  RecordIdent(EditorRecord* Picked, uint32_t PickedIndex) noexcept;
    void  RecordCard(EditorPropertyGroup& Group, uint32_t Card) noexcept;
    void  RecordStanding(EditorRecord* Picked, uint32_t PickedIndex) noexcept;
    void  RecordNotes(EditorRecord* Picked) noexcept;
    float RecordCaps(const char* Text, const ImVec2& At, ImU32 Tint) noexcept;

    EditorKit* Kit_ = nullptr;

    bool     CardShut_[8] = {};                        // false reads open; sheet cards, then the notes card
    uint32_t SheetFor_    = kNoEditorRecord;
    uint32_t NameFor_     = kNoEditorRecord;
    char     NameText_[48] = {};
};

} // namespace Frontier
