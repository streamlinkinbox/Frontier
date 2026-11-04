/*==========================================================================================================================================
                                                                SELECTROW.H
==========================================================================================================================================*/
// 🧩 .ne-control with .ne-select: a label and its dropdown.

#pragma once

#include <imgui.h>


namespace Frontier
{

class ControlColumn;

//------------------------------------------------------------------------------------------------------------------------
//                                                    SELECT ROW
//------------------------------------------------------------------------------------------------------------------------

class SelectRow
{
  public:
    // 📝 True when the choice changed.
    static bool Construct(ControlColumn& Column, const char* Id, const char* Label, const char* const* Options, int Count, int& InOutIndex,
                          bool Enabled = true, const char* Tooltip = nullptr);
    static bool ConstructAt(ControlColumn& Column, const char* Id, ImVec2 Minimum, float Width,
                            const char* const* Options, int Count, int& InOutIndex,
                            bool Enabled = true, const char* Tooltip = nullptr);
};

} // namespace Frontier
