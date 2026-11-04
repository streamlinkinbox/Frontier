/*==========================================================================================================================================
                                                                NEXTBUTTON.H
==========================================================================================================================================*/
// 🧩 .co-next: the full-width link button at the foot of a section.

#pragma once


namespace Frontier
{

class ControlColumn;

//------------------------------------------------------------------------------------------------------------------------
//                                                    NEXT BUTTON
//------------------------------------------------------------------------------------------------------------------------

class NextButton
{
  public:
    static bool Construct(ControlColumn& Column, const char* Id, const char* Label, bool Enabled = true, const char* Tooltip = nullptr);
};

} // namespace Frontier
