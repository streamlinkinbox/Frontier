/*==========================================================================================================================================
                                                               CHOICELIST.H
==========================================================================================================================================*/
// 🧩 Selectable rows under group headings, one chosen: an index chip, a caption, a dimmed note and an optional tag (the Render
//    settings page's debug views).

#pragma once

#include <span>

namespace Frontier
{

class ControlColumn;

//------------------------------------------------------------------------------------------------------------------------
//                                                    CHOICE LIST
//------------------------------------------------------------------------------------------------------------------------

struct ChoiceEntry
{
    const char* Group   = "";        // [-] heading drawn when it differs from the previous entry's
    const char* Chip    = "";        // [-] short index text ("06")
    const char* Caption = "";
    const char* Note    = nullptr;   // [-] dimmed text under the caption
    const char* Tag     = nullptr;   // [-] small pill at the right ("needs virtualized geometry")
    bool        Enabled = true;
};

class ChoiceList
{
  public:
    // 📝 True when the chosen row changed.
    static bool Construct(ControlColumn& Column, const char* Id, std::span<const ChoiceEntry> Entries, int& InOutChosen);
};

} // namespace Frontier
