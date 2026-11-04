/*==========================================================================================================================================
                                                                 ACTIONROW.H
==========================================================================================================================================*/
// 🧩 Intrinsic-width action buttons with wrapping, optional trailing alignment and a leading caption.

#pragma once

#include <span>

namespace Frontier
{

class ControlColumn;

//------------------------------------------------------------------------------------------------------------------------
//                                                    ACTION ROW
//------------------------------------------------------------------------------------------------------------------------

struct ActionEntry
{
    const char *Id;
    const char *Caption;
    bool        Enabled = true;
    const char *Tooltip = nullptr;
};

struct ActionRowLook
{
    float       FontSize     = 11;
    float       PaddingSide  = 14;
    float       PaddingTop   = 8;
    float       MarginTop    = 18;
    float       MarginBottom = 20;
    bool        Trailing     = false;
    const char *Leading      = nullptr;
};

class ActionRow
{
  public:
    static int Construct(ControlColumn &Column, std::span<const ActionEntry> Entries, const ActionRowLook &Look = {});
};

} // namespace Frontier
