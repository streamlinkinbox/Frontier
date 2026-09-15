//============================================================================================================================================
//                                                    INSPECTORPANEL.H
//============================================================================================================================================
// 🧩 Development editor inspector — the picked instance as a property sheet. Ident strip, schema cards drawn from
//    the project's sheet, the instance standing, the notes card. Every control edits the project's own figures.

#pragma once

#include "EditorInstance.h"

#include <imgui.h>

#include <cstdint>

namespace Frontier {

class ControlPanel;

class InspectorPanel final
{
public:
    void AssignControls(ControlPanel* Controls) noexcept;

    void Record(EditorInstance* Picked, uint32_t PickedIndex, EditorSheet* Sheet) noexcept;

private:
    void  RecordEmpty() noexcept;
    void  RecordIdent(EditorInstance* Picked, uint32_t PickedIndex) noexcept;
    void  RecordCard(EditorPropertyGroup& Group, uint32_t Card) noexcept;
    void  RecordStanding(EditorInstance* Picked, uint32_t PickedIndex) noexcept;
    void  RecordNotes(EditorInstance* Picked) noexcept;
    void  RecordFooter(EditorInstance* Picked) noexcept;
    float RecordCaps(const char* Text, const ImVec2& At, ImU32 Tint) noexcept;

    ControlPanel* Controls_ = nullptr;

    bool     CardShut_[8] = {};                        // false reads open; sheet cards, then the notes card
    uint32_t SheetFor_    = kNoEditorInstance;
    uint32_t NameFor_     = kNoEditorInstance;
    char     NameText_[48] = {};
    bool     NotesFocus_  = false;   // the notes ring lags one tick (the push precedes the field)
};

} // namespace Frontier
