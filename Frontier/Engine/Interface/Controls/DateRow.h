/*==========================================================================================================================================
                                                                 DATEROW.H
==========================================================================================================================================*/
// 🧩 .co-data-row holding a text field (the calendar date).

#pragma once

#include <string>

namespace Frontier
{

class ControlColumn;

//------------------------------------------------------------------------------------------------------------------------
//                                                    DATE ROW
//------------------------------------------------------------------------------------------------------------------------

class DateRow
{
  public:
    // 📝 True when the text changed.
    static bool Construct(ControlColumn& Column, const char* Id, const char* Label, std::string& InOutText, bool Enabled = true,
                          const char* Tooltip = nullptr);
};

} // namespace Frontier
