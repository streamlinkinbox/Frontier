/*==========================================================================================================================================
                                                                 TABSTRIP.H
==========================================================================================================================================*/
// 🧩 .co-tabs: the equal section pills under the page header.

#pragma once


namespace Frontier
{

class ControlColumn;

//------------------------------------------------------------------------------------------------------------------------
//                                                    TAB STRIP
//------------------------------------------------------------------------------------------------------------------------

class TabStrip
{
  public:
    // 📝 True when the selection changed.
    static bool Construct(ControlColumn& Column, const char* Id, const char* const* Labels, int Count, int& InOutSelected);
};

} // namespace Frontier
