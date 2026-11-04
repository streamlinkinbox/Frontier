/*==========================================================================================================================================
                                                              READOUTBLOCK.CPP
==========================================================================================================================================*/
// 🧩 .co-readout: label, light figure and unit on one baseline, as a block or placed freely.

#include "Engine/Interface/Controls/ReadoutBlock.h"

#include "Engine/Interface/Controls/ControlColumn.h"
#include "Engine/Interface/Controls/GlyphRun.h"
#include "Engine/Interface/InspectorControls.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    READOUT BLOCK
//------------------------------------------------------------------------------------------------------------------------

float ReadoutBlock::ConstructAt(ControlColumn& Column, ImVec2 Min, const char* Label, const char* Figure, const char* Unit, float FigureSize,
                                float UnitSize, float FigureTracking, float FigureLineHeight, ImU32 Ink, ImU32 LabelColour, ImU32 UnitColour,
                                float LabelSize, float LabelGap, bool UnitBeneath, float UnitGap)
{
    const ControlTheme&    Theme    = Column.QueryTheme();
    const EditorTypefaces& Faces    = Column.QueryFaces();
    const float            Scale    = Column.QueryScale();
    const float S      = Scale;
    float       Height = 0.0f;
    if (Label != nullptr && Label[0] != '\0')
    {
        (void)GlyphRun::Construct(Column, Faces.Regular, LabelSize * S, ImVec2(Min.x, GlyphRun::ResolveBaseline(Min.y, LabelSize * S, 1.4f)),
                                LabelColour != 0 ? LabelColour : Theme.Label, Label);
        Height += (LabelSize * 1.4f + LabelGap) * S;
    }
    const float FigureTop  = Min.y + Height;
    const float Baseline   = GlyphRun::ResolveBaseline(FigureTop, FigureSize * S, FigureLineHeight);
    const float FigureWide = GlyphRun::Construct(Column, Faces.Light, FigureSize * S, ImVec2(Min.x, Baseline), Ink != 0 ? Ink : Theme.Ink, Figure,
                                               FigureTracking * S);
    Height += FigureSize * FigureLineHeight * S;
    if (Unit != nullptr && Unit[0] != '\0')
    {
        if (UnitBeneath)
        {
            // 📝 The unit wraps onto its own line: the line box takes the readout's 14 px / 1.4 strut.
            const float LineTop = Min.y + Height + UnitGap * S;
            (void)GlyphRun::Construct(Column, Faces.Regular, UnitSize * S, ImVec2(Min.x, GlyphRun::ResolveBaseline(LineTop, 14.0f * S, 1.4f)),
                                    UnitColour != 0 ? UnitColour : Theme.ReadoutUnit, Unit);
            Height += (14.0f * 1.4f + UnitGap) * S;
        }
        else
        {
            (void)GlyphRun::Construct(Column, Faces.Regular, UnitSize * S, ImVec2(Min.x + FigureWide + 4.0f * S, Baseline),
                                    UnitColour != 0 ? UnitColour : Theme.ReadoutUnit, Unit);
        }
    }
    return Height;
}

void ReadoutBlock::Construct(ControlColumn& Column, const char* Label, const char* Figure, const char* Unit, float FigureSize, float UnitSize,
                             float FigureTracking, float FigureLineHeight)
{
    const float LabelBlock = (Label != nullptr && Label[0] != '\0') ? 9.0f * 1.4f + 5.0f : 0.0f;
    const ImVec2 Top       = Column.Space(LabelBlock + FigureSize * FigureLineHeight, 0.0f, 0.0f);
    (void)ReadoutBlock::ConstructAt(Column, Top, Label, Figure, Unit, FigureSize, UnitSize, FigureTracking, FigureLineHeight);
}

} // namespace Frontier
