/*==========================================================================================================================================
                                                               QUICKTILES.H
==========================================================================================================================================*/
// 🧩 The Project Zero quick control tiles (.quick-tile): a tinted icon seat, the caption and ON / OFF, green when on and
//    coral when off; a tile without a target reads UNAVAILABLE.

#pragma once

namespace Frontier
{

class ControlColumn;

//------------------------------------------------------------------------------------------------------------------------
//                                                    QUICK TILES
//------------------------------------------------------------------------------------------------------------------------

struct QuickTile
{
    const char* Caption = "";
    const char* Symbol  = "Eye";     // [-] GlyphSymbol name
    bool*       Target  = nullptr;   // [-] null: UNAVAILABLE
    const char* Tooltip = nullptr;
};

class QuickTiles
{
  public:
    // 📝 Columns across, gap 8 px, 108 px tiles. True when a tile flipped its target.
    static bool Construct(ControlColumn& Column, const char* Id, const QuickTile* Tiles, int Count, int Columns, bool Enabled = true);
};

} // namespace Frontier
