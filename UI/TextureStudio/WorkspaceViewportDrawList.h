/*==========================================================================================================================================
                                                     WORKSPACEVIEWPORTDRAWLIST.H
==========================================================================================================================================*/
// 🧩 Shared ImGui draw-list blitter for WorkspaceViewportCpuFrame across Material, Texture Paint, and Baking viewports.

#pragma once
#include "Engine/Render/WorkspaceViewportRendering.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace Frontier
{
inline void BlitWorkspaceViewportCpuFrame(ImDrawList *DrawList, const WorkspaceViewportCpuFrame &Frame,
    ImVec2 DestMin, ImVec2 DestMax, ImVec2 ClipMin, ImVec2 ClipMax)
{
    if (!DrawList || Frame.Width == 0 || Frame.Height == 0 || Frame.Rgba.size() < size_t(Frame.Width) * Frame.Height * 4)
        return;
    const float DestW = DestMax.x - DestMin.x;
    const float DestH = DestMax.y - DestMin.y;
    if (DestW <= 1.f || DestH <= 1.f) return;

    const float CellW = DestW / float(Frame.Width);
    const float CellH = DestH / float(Frame.Height);
    DrawList->PushClipRect(ClipMin, ClipMax, true);
    for (uint32_t Y = 0; Y < Frame.Height; ++Y)
    {
        const float RowTop = std::floor(DestMin.y + float(Y) * CellH);
        const float RowBottom = std::ceil(DestMin.y + float(Y + 1) * CellH);
        if (RowBottom <= ClipMin.y || RowTop >= ClipMax.y) continue;

        uint32_t X = 0;
        while (X < Frame.Width)
        {
            const uint8_t *Px = &Frame.Rgba[(size_t(Y) * Frame.Width + X) * 4];
            if (Px[3] == 0) { ++X; continue; }
            const uint8_t R = Px[0], G = Px[1], B = Px[2], A = Px[3];
            uint32_t RunEnd = X + 1;
            while (RunEnd < Frame.Width)
            {
                const uint8_t *Next = &Frame.Rgba[(size_t(Y) * Frame.Width + RunEnd) * 4];
                if (Next[3] != A || std::abs(int(Next[0]) - int(R)) > 2 ||
                    std::abs(int(Next[1]) - int(G)) > 2 || std::abs(int(Next[2]) - int(B)) > 2)
                    break;
                ++RunEnd;
            }
            const float Left = std::floor(DestMin.x + float(X) * CellW);
            const float Right = std::ceil(DestMin.x + float(RunEnd) * CellW);
            if (Right > ClipMin.x && Left < ClipMax.x)
                DrawList->AddRectFilled(ImVec2(Left, RowTop), ImVec2(Right, RowBottom), IM_COL32(R, G, B, A));
            X = RunEnd;
        }
    }
    DrawList->PopClipRect();
}
} // namespace Frontier
