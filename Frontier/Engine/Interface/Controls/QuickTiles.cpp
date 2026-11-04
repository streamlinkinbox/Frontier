/*==========================================================================================================================================
                                                              QUICKTILES.CPP
==========================================================================================================================================*/
// 🧩 The Project Zero quick control tiles (.quick-tile): a tinted icon seat, the caption and ON / OFF, green when on and
//    coral when off; a tile without a target reads UNAVAILABLE.

#include "Engine/Interface/Controls/QuickTiles.h"

#include "Engine/Interface/Controls/ControlColumn.h"
#include "Engine/Interface/Controls/GlyphRun.h"
#include "Engine/Interface/Controls/GlyphSymbol.h"
#include "Engine/Interface/InspectorControls.h"

#include <imgui.h>

#include <algorithm>

namespace Frontier
{

namespace
{

// 📝 The tint at a fraction over the card (color-mix(in srgb, tint P%, transparent) as straight alpha).
ImU32 ResolveTint(uint32_t Tint, float Fraction)
{
    return (ResolveCssColour(Tint) & 0x00FFFFFFu) | (static_cast<ImU32>(std::clamp(Fraction, 0.0f, 1.0f) * 255.0f + 0.5f) << IM_COL32_A_SHIFT);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    QUICK TILES
//------------------------------------------------------------------------------------------------------------------------

bool QuickTiles::Construct(ControlColumn& Column, const char* Id, const QuickTile* Tiles, int Count, int Columns, bool Enabled)
{
    // 📝 .quick-tile: 108 px, padding 12 px 6 px 10 px, radius 17 px, 1 px #ffffff12, a 145° #ffffff06 → #ffffff01 wash; the seat
    //    40 px, radius 13 px, the tint 24 % → 7 % with a 14 % ring; the glyph 25 px in the ink (#5bdb87 on, #e19483 off, #8b8b8b
    //    unavailable); caption 11 px #d1d1d1; status 8 px, tracking 1.1 px, in the ink.
    if (Count <= 0) return false;
    const EditorTypefaces& Faces    = Column.QueryFaces();
    ImDrawList* const      DrawList = Column.QueryDrawList();
    const float            S        = Column.QueryScale();
    const float            Width    = Column.QueryWidth();
    const int              Across   = std::max(1, Columns);
    const int              Rows     = (Count + Across - 1) / Across;
    constexpr float        Tall = 108.0f, Gap = 8.0f;
    const ImVec2 Top      = Column.Space(Tall * static_cast<float>(Rows) + Gap * static_cast<float>(Rows - 1), 0.0f, 14.0f);
    const float  TileWide = (Width - Gap * S * static_cast<float>(Across - 1)) / static_cast<float>(Across);
    bool         Flipped  = false;
    ImGui::PushID(Id);
    for (int Index = 0; Index < Count; ++Index)
    {
        const QuickTile& Tile = Tiles[Index];
        const ImVec2     Min(Top.x + static_cast<float>(Index % Across) * (TileWide + Gap * S), Top.y + static_cast<float>(Index / Across) * (Tall + Gap) * S);
        const ImVec2     Max(Min.x + TileWide, Min.y + Tall * S);
        const bool       Live = Enabled && Tile.Target != nullptr;
        const bool       Lit  = Tile.Target != nullptr && *Tile.Target;
        ImGui::SetCursorScreenPos(Min);
        ImGui::PushID(Index);
        if (!Live) ImGui::BeginDisabled();
        if (ImGui::InvisibleButton("##QuickTile", ImVec2(TileWide, Tall * S), ImGuiButtonFlags_EnableNav) && Live)
        {
            *Tile.Target = !*Tile.Target;
            Flipped      = true;
        }
        if (!Live) ImGui::EndDisabled();
        ControlColumn::ConstructTooltip(Tile.Tooltip);
        const bool Hovered = Live && ImGui::IsItemHovered();
        ImGui::PopID();
        const uint32_t Ink  = Tile.Target == nullptr ? 0x8B8B8BFFu : (Lit ? 0x5BDB87FFu : 0xE19483FFu);
        const uint32_t Tint = Tile.Target == nullptr ? 0x777777FFu : (Lit ? 0x4CC778FFu : 0xCB7768FFu);
        const float    Fade = Enabled ? 1.0f : 0.55f;
        DrawList->AddRectFilled(Min, Max, Hovered ? ResolveCssColour(0x252525FF) : ResolveCssColour(0xFFFFFF04u), 17.0f * S);
        if (Hovered) DrawList->AddRectFilled(Min, Max, ResolveTint(Tint, 0.09f), 17.0f * S);
        DrawList->AddRect(Min, Max, Hovered ? ResolveTint(Ink, 0.45f) : FadeColour(ResolveCssColour(0xFFFFFF12u), Fade), 17.0f * S, ImDrawFlags_None, S);
        // 📝 The content block (seat 40, gap 7, caption, gap 7, status 8) centred in the tile's padded height.
        const float  Block  = 40.0f + 7.0f + 11.0f * 1.15f + 7.0f + 8.0f;
        const float  SeatY  = Min.y + (12.0f + (Tall - 22.0f - Block) * 0.5f) * S;
        const ImVec2 SeatMin(0.5f * (Min.x + Max.x) - 20.0f * S, SeatY);
        const ImVec2 SeatMax(SeatMin.x + 40.0f * S, SeatMin.y + 40.0f * S);
        DrawList->AddRectFilled(SeatMin, SeatMax, FadeColour(ResolveTint(Tint, 0.15f), Fade), 13.0f * S);
        DrawList->AddRect(SeatMin, SeatMax, FadeColour(ResolveTint(Tint, 0.14f), Fade), 13.0f * S, ImDrawFlags_None, S);
        GlyphSymbol::Construct(DrawList, Tile.Symbol, {SeatMin.x + 8.5f * S, SeatMin.y + 8.5f * S}, 23.0f * S, 1.7f, FadeColour(ResolveCssColour(Ink), Fade));
        const float CaptionTop  = SeatMax.y + 7.0f * S;
        const float CaptionWide = GlyphRun::Measure(Column, Faces.Regular, 11.0f * S, Tile.Caption);
        (void)GlyphRun::Construct(Column, Faces.Regular, 11.0f * S,
                                  ImVec2(0.5f * (Min.x + Max.x) - 0.5f * CaptionWide, GlyphRun::ResolveBaseline(CaptionTop, 11.0f * S, 1.15f)),
                                  FadeColour(ResolveCssColour(0xD1D1D1FF), Fade), Tile.Caption);
        const char* Reading     = Tile.Target == nullptr ? "UNAVAILABLE" : (Lit ? "ON" : "OFF");
        const float ReadingTop  = CaptionTop + (11.0f * 1.15f + 7.0f) * S;
        const float ReadingWide = GlyphRun::Measure(Column, Faces.Regular, 8.0f * S, Reading, 1.1f * S);
        (void)GlyphRun::Construct(Column, Faces.Regular, 8.0f * S,
                                  ImVec2(0.5f * (Min.x + Max.x) - 0.5f * ReadingWide, GlyphRun::ResolveBaseline(ReadingTop, 8.0f * S, 1.0f)),
                                  FadeColour(ResolveCssColour(Ink), Fade), Reading, 1.1f * S);
    }
    ImGui::PopID();
    return Flipped;
}

} // namespace Frontier
