/*==========================================================================================================================================
                                                                 TAGPILL.H
==========================================================================================================================================*/
// 🧩 .co-tag (and the pressable .so-mode pill).

#pragma once

#include <imgui.h>

namespace Frontier
{

class ControlColumn;

//------------------------------------------------------------------------------------------------------------------------
//                                                    TAG PILL
//------------------------------------------------------------------------------------------------------------------------

class TagPill
{
  public:
    // 📝 The pill at Min; returns its size. Pressed reports a click when Pressable.
    static ImVec2 ConstructAt(ControlColumn& Column, const char* Id, ImVec2 Min, const char* Text, bool Pressable, bool* Pressed,
                              float FontSize = 9.0f, float PadX = 10.0f, float PadY = 6.0f, float MinHeight = 0.0f, ImU32 Fill = 0,
                              ImU32 TextColour = 0, const char* Tooltip = nullptr);
    [[nodiscard]] static float Measure(const ControlColumn& Column, const char* Text, float FontSize = 9.0f, float PadX = 10.0f);
};

} // namespace Frontier
