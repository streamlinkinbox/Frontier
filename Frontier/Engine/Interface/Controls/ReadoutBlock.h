/*==========================================================================================================================================
                                                               READOUTBLOCK.H
==========================================================================================================================================*/
// 🧩 .co-readout: label (9 px), light figure and its unit on one baseline, as a block or placed freely.

#pragma once

#include <imgui.h>

namespace Frontier
{

class ControlColumn;

//------------------------------------------------------------------------------------------------------------------------
//                                                    READOUT BLOCK
//------------------------------------------------------------------------------------------------------------------------

class ReadoutBlock
{
  public:
    static void  Construct(ControlColumn& Column, const char* Label, const char* Figure, const char* Unit, float FigureSize = 31.0f,
                           float UnitSize = 10.0f, float FigureTracking = -1.3f, float FigureLineHeight = 1.25f);
    // 📝 The same readout drawn at Min without taking layout space; returns its height.
    static float ConstructAt(ControlColumn& Column, ImVec2 Min, const char* Label, const char* Figure, const char* Unit, float FigureSize,
                             float UnitSize, float FigureTracking, float FigureLineHeight, ImU32 Ink = 0, ImU32 LabelColour = 0,
                             ImU32 UnitColour = 0, float LabelSize = 9.0f, float LabelGap = 5.0f, bool UnitBeneath = false,
                             float UnitGap = 0.0f);
};

} // namespace Frontier
