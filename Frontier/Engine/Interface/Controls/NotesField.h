/*==========================================================================================================================================
                                                               NOTESFIELD.H
==========================================================================================================================================*/
// 🧩 The optional notes of a record (.entity-notes): a titled box with a Hide action over a multi-line text field.

#pragma once

#include <string>

namespace Frontier
{

class ControlColumn;

//------------------------------------------------------------------------------------------------------------------------
//                                                    NOTES FIELD
//------------------------------------------------------------------------------------------------------------------------

class NotesField
{
  public:
    // 📝 True when the text changed; Open turns false when Hide is pressed.
    static bool Construct(ControlColumn& Column, const char* Id, std::string& Text, bool& Open);
};

} // namespace Frontier
