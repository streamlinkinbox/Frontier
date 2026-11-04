/*==========================================================================================================================================
                                                             TRANSFORMTABLE.H
==========================================================================================================================================*/
// 🧩 The Project Zero transform table (.transform-table): a component column and X / Y / Z drag fields per row, with one
//    reset pill per row under it.

#pragma once

#include <array>

namespace Frontier
{

class ControlColumn;

//------------------------------------------------------------------------------------------------------------------------
//                                                    TRANSFORM TABLE
//------------------------------------------------------------------------------------------------------------------------

struct TransformTableRow
{
    const char*          Label   = "";
    const char*          Unit    = "";
    float*               Values  = nullptr;     // [-] three
    float                Minimum = -1.0e5f;
    float                Maximum = 1.0e5f;
    float                Step    = 0.01f;       // [-] per pixel of drag
    const char*          Format  = "%.2f";
    std::array<float, 3> Reset{};               // [-] what the row's reset pill restores
};

class TransformTable
{
  public:
    // 📝 The header row (Component · X · Y · Z in the axis colours), one row per entry (label over its unit, three fields: drag
    //    to scrub, double-click or Ctrl-click to type), then the reset pills. A disabled table fades and ignores the pointer.
    //    Compact leaves out the header row and the reset pills (a vector field inside another card). True when a value changed.
    static bool Construct(ControlColumn& Column, const char* Id, const TransformTableRow* Rows, int Count, bool Enabled, bool Compact = false);
};

} // namespace Frontier
