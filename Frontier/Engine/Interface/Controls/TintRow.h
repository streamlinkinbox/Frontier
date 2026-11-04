/*==========================================================================================================================================
                                                                 TINTROW.H
==========================================================================================================================================*/
// 🧩 .co-data-row.so-tint-row: a label and the tint pill (dot, hex, ↗) opening the colour picker.

#pragma once


namespace Frontier
{

class ControlColumn;

//------------------------------------------------------------------------------------------------------------------------
//                                                    TINT ROW
//------------------------------------------------------------------------------------------------------------------------

class TintRow
{
  public:
    // 📝 True when the colour changed.
    static bool Construct(ControlColumn& Column, const char* Id, const char* Label, float (&InOutLinear)[3], bool Enabled = true,
                          const char* Tooltip = nullptr);
};

} // namespace Frontier
