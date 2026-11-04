/*==========================================================================================================================================
                                                              CARDTITLEROW.H
==========================================================================================================================================*/
// 🧩 The title line of a Project Zero card (.transform-card header, .fracture-card header): the light title, an optional
//    tracked caption under or beside it, and an optional round action at the right.

#pragma once

namespace Frontier
{

class ControlColumn;

//------------------------------------------------------------------------------------------------------------------------
//                                                    CARD TITLE ROW
//------------------------------------------------------------------------------------------------------------------------

struct CardTitleLook
{
    const char* Glyph         = nullptr;   // [-] GlyphSymbol drawn before the title
    float       TitleSize     = 14.0f;     // [px]
    const char* Caption       = nullptr;   // [-] tracked capitals
    bool        CaptionBeside = true;      // [-] right-aligned on the title line; false: under the title
    const char* ActionGlyph   = nullptr;   // [-] a 30 px round button at the right (↗ expand)
    const char* ActionTooltip = nullptr;
    bool        ActionEnabled = true;
    float       MarginBottom  = 12.0f;     // [px]
};

class CardTitleRow
{
  public:
    // 📝 True when the round action was pressed.
    static bool Construct(ControlColumn& Column, const char* Id, const char* Title, const CardTitleLook& Look);
};

} // namespace Frontier
