/*==========================================================================================================================================
                                                              METRICTILES.CPP
==========================================================================================================================================*/
// 🧩 .co-kpis: equal metric tiles, the last one lit.

#include "Engine/Interface/Controls/MetricTiles.h"

#include "Engine/Interface/Controls/ControlColumn.h"
#include "Engine/Interface/Controls/GlyphRun.h"
#include "Engine/Interface/Controls/ReadoutBlock.h"
#include "Engine/Interface/InspectorControls.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    METRIC TILES
//------------------------------------------------------------------------------------------------------------------------

void MetricTiles::Construct(ControlColumn& Column, const MetricTile* Metrics, int Count, int LeadIndex)
{
    const ControlTheme&    Theme    = Column.QueryTheme();
    ImDrawList* const      DrawList = Column.QueryDrawList();
    const float            Scale    = Column.QueryScale();
    const float            Width    = Column.QueryWidth();
    if (Count <= 0) return;
    // 📝 .co-kpis (narrow): grid, gap 7 px, margin-bottom 12 px; each readout padding 13 px 10 px, radius 17 px; label 8 px,
    //    figure 24 px block (letter-spacing −1 px, line-height 1.25), the unit 8 px on the next line.
    const float  S        = Scale;
    const float FigureSize = Theme.CompactMetrics ? 23.0f : 24.0f;
    const float Border = Theme.CompactMetrics ? 0.0f : 1.0f;
    const float Radius = Theme.CompactMetrics ? 15.0f : 17.0f;
    const float  Content  = 8.0f * 1.4f + 5.0f + FigureSize * 1.25f + 14.0f * 1.4f;
    const float  TileTall = Border + 13.0f + Content + 13.0f + Border;
    const ImVec2 Top      = Column.Space(TileTall, 0.0f, Theme.CompactMetrics ? 16.0f : 12.0f);
    const float  Gap      = 7.0f * S;
    const float  TileWide = (Width - Gap * static_cast<float>(Count - 1)) / static_cast<float>(Count);
    for (int Index = 0; Index < Count; ++Index)
    {
        const bool   Lead = Index == (LeadIndex == LastLead ? Count - 1 : LeadIndex);
        const ImVec2 Min(Top.x + static_cast<float>(Index) * (TileWide + Gap), Top.y);
        const ImVec2 Max(Min.x + TileWide, Min.y + TileTall * S);
        DrawList->AddRectFilled(Min, Max, Lead ? Theme.MetricLeadFill : Theme.MetricFill, Radius * S);
        if (Theme.MetricBorder != 0 && !Lead) DrawList->AddRect(Min, Max, Theme.MetricBorder, Radius * S, ImDrawFlags_None, S);
        const ImU32 Ink   = Lead ? Theme.MetricLeadInk : Theme.Ink;
        const ImU32 Muted = Lead ? Theme.MetricLeadMuted : Theme.Label;
        const ImU32 Unit  = Lead ? Theme.MetricLeadMuted : Theme.ReadoutUnit;
        (void)ReadoutBlock::ConstructAt(Column, ImVec2(Min.x + (10.0f + Border) * S, Min.y + (13.0f + Border) * S), Metrics[Index].Label.c_str(), Metrics[Index].Figure.c_str(),
                                 Metrics[Index].Unit.c_str(), FigureSize, 8.0f, -1.0f, 1.25f, Ink, Muted, Unit, 8.0f, 5.0f, true, 0.0f);
    }
}

} // namespace Frontier
