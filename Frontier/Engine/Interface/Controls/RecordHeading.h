/*==========================================================================================================================================
                                                              RECORDHEADING.H
==========================================================================================================================================*/
// 🧩 The Project Zero inspector heading (.inspector-heading): the breadcrumb, the dashed notes pill, the title and the
//    subtitle of one record's page.

#pragma once

namespace Frontier
{

class ControlColumn;

//------------------------------------------------------------------------------------------------------------------------
//                                                    RECORD HEADING
//------------------------------------------------------------------------------------------------------------------------

class RecordHeading
{
  public:
    // 📝 110 px tall: the breadcrumb (9 px) at the top left, the dashed pill (Glyph + Label, 30 px) at the top right, the title
    //    (25 px) from 41 px and the subtitle (10 px) from 80 px. True when the pill was pressed.
    static bool Construct(ControlColumn& Column,
                          const char*    Id,
                          const char*    Breadcrumb,
                          const char*    Title,
                          const char*    Subtitle,
                          const char*    PillLabel,
                          const char*    PillGlyph = "Plus");
};

} // namespace Frontier
