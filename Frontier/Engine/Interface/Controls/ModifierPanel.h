/*==========================================================================================================================================
                                                              MODIFIERPANEL.H
==========================================================================================================================================*/
// 🧩 The header of one modifier in a Blender-style modifier stack: the fold chevron, the modifier's glyph, its label and
//    summary, the viewport switch, move up, move down and remove.

#pragma once

#include <cstdint>

namespace Frontier
{

class ControlColumn;

//------------------------------------------------------------------------------------------------------------------------
//                                                    MODIFIER PANEL
//------------------------------------------------------------------------------------------------------------------------

enum class ModifierAction : uint8_t
{
    None,
    MoveUp,
    MoveDown,
    Remove,
};

struct ModifierHeader
{
    const char* Label    = "Array";
    const char* Summary  = nullptr;   // [-] muted, after the label ("54 copies")
    const char* Glyph    = "Copy";
    bool*       Expanded = nullptr;   // [-] flipped by the chevron and the label
    bool*       Enabled  = nullptr;   // [-] flipped by the viewport switch
    bool        First    = false;     // [-] no move up
    bool        Last     = false;     // [-] no move down
    bool        Locked   = false;     // [-] the object refuses edits: only folding works
};

class ModifierPanel
{
  public:
    // 📝 One 34 px row; the caller lays the modifier's rows out under it while *Expanded. Returns the button pressed.
    static ModifierAction ConstructHeader(ControlColumn& Column, const char* Id, const ModifierHeader& Header);
};

} // namespace Frontier
