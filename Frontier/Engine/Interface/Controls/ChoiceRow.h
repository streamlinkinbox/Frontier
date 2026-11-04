/*==========================================================================================================================================
                                                                CHOICEROW.H
==========================================================================================================================================*/
// 🧩 A label (with an optional note beside it) over equal segments, one chosen: the Render settings page's trace resolution,
//    quality grade, budget and view selectors.

#pragma once

namespace Frontier
{

class ControlColumn;

//------------------------------------------------------------------------------------------------------------------------
//                                                    CHOICE ROW
//------------------------------------------------------------------------------------------------------------------------

class ChoiceRow
{
  public:
    // 📝 True when the choice changed. Note (optional) sits right of the label, dimmed ("rebuilds targets"); a disabled row
    //    fades and ignores the pointer, and Tooltip says why.
    static bool Construct(ControlColumn& Column, const char* Id, const char* Label, const char* const* Options, int Count, int& InOutIndex,
                          bool Enabled = true, const char* Note = nullptr, const char* Tooltip = nullptr);
};

} // namespace Frontier
