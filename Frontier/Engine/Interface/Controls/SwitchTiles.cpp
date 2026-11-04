/*==========================================================================================================================================
                                                              SWITCHTILES.CPP
==========================================================================================================================================*/
// 🧩 .ne-tiles: quick switch tiles (symbol disc, caption, ON / OFF).

#include "Engine/Interface/Controls/SwitchTiles.h"

#include "Engine/Interface/Controls/ControlColumn.h"
#include "Engine/Interface/Controls/GlyphRun.h"
#include "Engine/Interface/Controls/GlyphSymbol.h"
#include "Engine/Interface/InspectorControls.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Frontier
{

namespace
{

constexpr float PiSingle = 3.14159265358979f;

void RasterizeTileGlyph(ImDrawList* DrawList, TileGlyph Glyph, ImVec2 Min, float Extent, ImU32 Colour)
{
    const float  Unit   = Extent / 24.0f;
    const float  Stroke = 1.6f * Unit;
    auto         At     = [&](float X, float Y) { return ImVec2(Min.x + X * Unit, Min.y + Y * Unit); };
    switch (Glyph)
    {
    case TileGlyph::Sun:
    {
        DrawList->AddCircle(At(12.0f, 12.0f), 4.0f * Unit, Colour, 24, Stroke);
        const float Rays[8][4] = {{12, 2, 12, 4},         {12, 20, 12, 22},       {4.93f, 4.93f, 6.34f, 6.34f}, {17.66f, 17.66f, 19.07f, 19.07f},
                                  {2, 12, 4, 12},         {20, 12, 22, 12},       {6.34f, 17.66f, 4.93f, 19.07f}, {19.07f, 4.93f, 17.66f, 6.34f}};
        for (const auto& Ray : Rays) DrawList->AddLine(At(Ray[0], Ray[1]), At(Ray[2], Ray[3]), Colour, Stroke);
        break;
    }
    case TileGlyph::Orbit:
    {
        // 📝 M19.1 4.9 A10 10 0 1 1 9.4 2.3: a 300° arc about the centre, then the two satellites and the hub.
        DrawList->PathArcTo(At(12.0f, 12.0f), 10.0f * Unit, -0.25f * PiSingle, 1.4167f * PiSingle, 40);
        DrawList->PathStroke(Colour, ImDrawFlags_None, Stroke);
        DrawList->AddCircle(At(19.0f, 5.0f), 2.0f * Unit, Colour, 16, Stroke);
        DrawList->AddCircle(At(5.0f, 19.0f), 2.0f * Unit, Colour, 16, Stroke);
        DrawList->AddCircle(At(12.0f, 12.0f), 3.0f * Unit, Colour, 20, Stroke);
        break;
    }
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    SWITCH TILES
//------------------------------------------------------------------------------------------------------------------------

bool SwitchTiles::Construct(ControlColumn& Column, const char* Id, const SwitchTile* Tiles, int Count, int Columns,
                            float MarginTop, float MarginBottom, bool Compact)
{
    const EditorTypefaces& Faces    = Column.QueryFaces();
    ImDrawList* const      DrawList = Column.QueryDrawList();
    const float            Scale    = Column.QueryScale();
    const float            Width    = Column.QueryWidth();
    if (Count <= 0) return false;
    // 📝 .ne-tiles: gap 9 px, margin-top 12 px; .ne-quick margin-bottom 16 px; tiles 106 px tall, radius 16 px, a 31 px
    //    symbol disc (on #69c884 / off #cc7673), the caption 10 px #c8c8c8, ON / OFF 8 px.
    const float S       = Scale;
    const int   Across  = std::max(1, Columns);
    const int   Rows    = (Count + Across - 1) / Across;
    const float Gap     = (Compact ? 12.0f : 9.0f) * S;
    const float Tall    = Compact ? 65.0f : 106.0f;
    const ImVec2 Top    = Column.Space(Tall * static_cast<float>(Rows) + 9.0f * static_cast<float>(Rows - 1), MarginTop, MarginBottom);
    const float TileWide = std::min(Compact ? 132.0f * S : Width, (Width - Gap * static_cast<float>(Across - 1)) / static_cast<float>(Across));
    bool        Flipped  = false;
    ImGui::PushID(Id);
    for (int Index = 0; Index < Count; ++Index)
    {
        const SwitchTile& Tile = Tiles[Index];
        const int    Lane   = Index % Across;
        const int    Row    = Index / Across;
        const ImVec2 Min(Top.x + static_cast<float>(Lane) * (TileWide + Gap), Top.y + static_cast<float>(Row) * (Tall * S + Gap));
        const ImVec2 Max(Min.x + TileWide, Min.y + Tall * S);
        const bool   Live   = Tile.Target != nullptr && !Tile.Pending;
        const bool   Lit    = Tile.Target != nullptr ? *Tile.Target : Tile.PendingReading;
        ImGui::SetCursorScreenPos(Min);
        ImGui::PushID(Index);
        if (!Live) ImGui::BeginDisabled();
        if (ImGui::InvisibleButton("##Tile", ImVec2(TileWide, Tall * S), ImGuiButtonFlags_EnableNav) && Live)
        {
            *Tile.Target = !*Tile.Target;
            Flipped      = true;
        }
        if (!Live) ImGui::EndDisabled();
        ControlColumn::ConstructTooltip(Tile.Tooltip);
        const bool Hovered = Live && ImGui::IsItemHovered();
        ImGui::PopID();
        const float Fade = Live ? 1.0f : 0.55f;
        DrawList->AddRectFilled(Min, Max, FadeColour(Hovered ? ResolveCssColour(0x303030FF) : ResolveCssColour(0x202020FF), Fade), 16.0f * S);
        DrawList->AddRect(Min, Max, FadeColour(Hovered ? ResolveCssColour(0x777777FF) : (Lit ? ResolveCssColour(0x323232FF) : ResolveCssColour(0x493232FF)), Fade),
                          16.0f * S, ImDrawFlags_None, S);
        const ImVec2 Disc(0.5f * (Min.x + Max.x), Min.y + (Compact ? 22.5f : 29.5f) * S);
        if (!Compact) DrawList->AddCircleFilled(Disc, 15.5f * S,
            FadeColour(Lit ? ResolveCssColour(0x69C884FF) : ResolveCssColour(0xCC7673FF), Fade), 40);
        const ImU32 SymbolInk = FadeColour(Compact ? (Lit ? ResolveCssColour(0x83CF9EFF) : ResolveCssColour(0xDD8989FF)) :
            (Lit ? ResolveCssColour(0x16321EFF) : ResolveCssColour(0x371D1CFF)), Fade);
        if (Tile.Symbol != nullptr)
            GlyphSymbol::Construct(DrawList, Tile.Symbol, {Disc.x - 9 * S, Disc.y - 9 * S}, 18 * S, 1.5f, SymbolInk);
        else RasterizeTileGlyph(DrawList, Tile.Glyph, {Disc.x - 9 * S, Disc.y - 9 * S}, 18 * S, SymbolInk);
        const float CaptionTop = Min.y + (Compact ? 38.0f : 56.0f) * S;
        const float CaptionWide = GlyphRun::Measure(Column, Faces.Regular, 10.0f * S, Tile.Caption);
        (void)GlyphRun::Construct(Column, Faces.Regular, 10.0f * S, ImVec2(Disc.x - 0.5f * CaptionWide, CaptionTop + GlyphRun::EmAscent * 10.0f * S),
                                FadeColour(ResolveCssColour(0xC8C8C8FF), Fade), Tile.Caption);
        if (Compact) continue;
        const char* Reading     = Tile.Target == nullptr && !Tile.Pending ? "UNAVAILABLE" : (Lit ? "ON" : "OFF");
        const float ReadingTop  = CaptionTop + (10.0f * GlyphRun::EmPixelHeight + 8.0f) * S;
        const float ReadingWide = GlyphRun::Measure(Column, Faces.Regular, 8.0f * S, Reading, 0.3f * S);
        (void)GlyphRun::Construct(Column, Faces.Regular, 8.0f * S, ImVec2(Disc.x - 0.5f * ReadingWide, ReadingTop + GlyphRun::EmAscent * 8.0f * S),
                                FadeColour(Lit ? ResolveCssColour(0x69C884FF) : ResolveCssColour(0xCC7673FF), Fade), Reading, 0.3f * S);
    }
    ImGui::PopID();
    return Flipped;
}

} // namespace Frontier
