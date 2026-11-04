/*==========================================================================================================================================
                                                                TOGGLEROW.H
==========================================================================================================================================*/
// 🧩 .co-toggle: a label and ON / OFF.

#pragma once


namespace Frontier
{

class ControlColumn;

//------------------------------------------------------------------------------------------------------------------------
//                                                    TOGGLE ROW
//------------------------------------------------------------------------------------------------------------------------

class ToggleRow
{
  public:
    // 📝 True when flipped.
    static bool Construct(ControlColumn& Column, const char* Id, const char* Label, bool& InOutEnabled, bool Enabled = true,
                          const char* Tooltip = nullptr);
};

} // namespace Frontier
