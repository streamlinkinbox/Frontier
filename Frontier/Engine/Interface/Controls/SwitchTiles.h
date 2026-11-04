/*==========================================================================================================================================
                                                               SWITCHTILES.H
==========================================================================================================================================*/
// 🧩 .ne-tiles: quick switch tiles (symbol disc, caption, ON / OFF). A null Target is the demo's disabled tile
//    ("UNAVAILABLE"); Pending marks a tile whose switch has no native field yet (drawn disabled with its Tooltip).

#pragma once

#include <cstdint>

namespace Frontier
{

class ControlColumn;

//------------------------------------------------------------------------------------------------------------------------
//                                                    SWITCH TILES
//------------------------------------------------------------------------------------------------------------------------

enum class TileGlyph : uint8_t
{
    Sun,     // Lucide "sun"
    Orbit,   // the demo's NativeOrbit mark
};

struct SwitchTile
{
    const char* Caption        = "";
    bool*       Target         = nullptr;
    TileGlyph   Glyph          = TileGlyph::Sun;
    bool        Pending        = false;
    bool        PendingReading = false;   // the demo's default reading while pending
    const char* Tooltip        = nullptr;
    const char* Symbol         = nullptr;
};

class SwitchTiles
{
  public:
    // 📝 Columns: 3 (or 2 for .ne-sun-quick below its breakpoint); true when a switch flipped.
    static bool Construct(ControlColumn& Column, const char* Id, const SwitchTile* Tiles, int Count, int Columns,
                          float MarginTop = 12.0f, float MarginBottom = 16.0f, bool Compact = false);
};

} // namespace Frontier
