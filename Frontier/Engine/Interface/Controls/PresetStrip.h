/*==========================================================================================================================================
                                                               PRESETSTRIP.H
==========================================================================================================================================*/
// 🧩 .so-presets: equal preset buttons, one pressed.

#pragma once


namespace Frontier
{

class ControlColumn;

//------------------------------------------------------------------------------------------------------------------------
//                                                    PRESET STRIP
//------------------------------------------------------------------------------------------------------------------------

class PresetStrip
{
  public:
    // 📝 Returns the pressed index or -1.
    static int Construct(ControlColumn& Column, const char* Id, const char* const* Labels, int Count, int PressedIndex);
};

} // namespace Frontier
