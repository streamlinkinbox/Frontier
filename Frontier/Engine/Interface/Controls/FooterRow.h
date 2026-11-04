/*==========================================================================================================================================
                                                                FOOTERROW.H
==========================================================================================================================================*/
// 🧩 .co-footer and .co-atlas-footer: the leading / trailing note rows closing a page.

#pragma once


namespace Frontier
{

class ControlColumn;

//------------------------------------------------------------------------------------------------------------------------
//                                                    FOOTER ROW
//------------------------------------------------------------------------------------------------------------------------

class FooterRow
{
  public:
    static void ConstructAtlas(ControlColumn& Column, const char* Leading, const char* Trailing, float FontSize = 8.0f, float PaddingTop = 12.0f,
                               float PaddingBottom = 12.0f);
    static void Construct(ControlColumn& Column, const char* Leading, const char* Trailing);
};

} // namespace Frontier
