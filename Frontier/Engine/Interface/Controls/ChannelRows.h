/*==========================================================================================================================================
                                                               CHANNELROWS.H
==========================================================================================================================================*/
// 🧩 The Project Zero material channel list (.channel-list): one row per channel with its swatch or glyph, name, source,
//    value and mode.

#pragma once

#include <cstdint>

namespace Frontier
{

class ControlColumn;

//------------------------------------------------------------------------------------------------------------------------
//                                                    CHANNEL ROWS
//------------------------------------------------------------------------------------------------------------------------

struct ChannelRow
{
    const char* Name   = "";
    const char* Source = "Surface";
    const char* Value  = "";
    const char* Mode   = "constant";
    uint32_t    Swatch = 0;          // [-] 0xRRGGBBAA of the colour swatch; 0 draws the scalar glyph
};

class ChannelRows
{
  public:
    // 📝 Rows 40 px tall, gap 4 px, radius 12 px; the Selected row is lit. Returns the row pressed, or −1.
    static int Construct(ControlColumn& Column, const char* Id, const ChannelRow* Rows, int Count, int Selected);
};

} // namespace Frontier
