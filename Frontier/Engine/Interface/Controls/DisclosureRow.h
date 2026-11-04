/*==========================================================================================================================================
                                                              DISCLOSUREROW.H
==========================================================================================================================================*/
// 🧩 A keyboard-accessible details/summary row; expansion belongs to each inspector instance.

#pragma once

namespace Frontier
{

class ControlColumn;

//------------------------------------------------------------------------------------------------------------------------
//                                                    DISCLOSURE ROW
//------------------------------------------------------------------------------------------------------------------------

class DisclosureRow
{
  public:
    static bool Construct(ControlColumn &Column, const char *Id, const char *Caption, bool &Expanded);
};

} // namespace Frontier
